/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2022
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SC_ALLOW_DEPRECATED_IEEE_API
#define SC_ALLOW_DEPRECATED_IEEE_API
#include "cci/cfg/cci_broker_manager.h"
#endif

#include "router.h"
#include "pass.h"
#include <tests/initiator-tester.h>
#include <systemc>
#include <tlm>
#include <scp/report.h>
#include <gtest/gtest.h> // For ASSERT_EQ, ASSERT_TRUE, ASSERT_FALSE
#include <tests/test-bench.h>
#include <tests/target-tester.h>  // Correct path for TargetTester
#include <tlm_sockets_buswidth.h> // Include for DEFAULT_TLM_BUSWIDTH

#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/peq_with_get.h>
#include <vector>
#include <limits> // Required for std::numeric_limits

static constexpr size_t NB_TARGETS = 4;

class RouterTestBenchSimple : public TestBench
{
    SCP_LOGGER();

public:
    using TlmResponseStatus = InitiatorTester::TlmResponseStatus;
    using TlmGenericPayload = InitiatorTester::TlmGenericPayload;
    using TlmDmi = InitiatorTester::TlmDmi;

    // Structure to hold target setup parameters
    struct TargetSetupParams {
        int id;
        uint64_t base_addr;
        size_t size;
        bool use_offset;
        int priority;
    };
    std::vector<TargetSetupParams> m_pending_target_setups; // Stores targets to be set up

    RouterTestBenchSimple(sc_core::sc_module_name name)
        : TestBench(name)
        , m_initiator("initiator")
        , m_router("router")
        , m_default_target(nullptr)
        , m_default_target_id(std::numeric_limits<int>::max()) // Assign a distinct ID for the default target
    {
        m_initiator.register_invalidate_direct_mem_ptr(
            [this](uint64_t start, uint64_t end) { invalidate_direct_mem_ptr(start, end); });

        m_initiator.socket.bind(m_router.target_socket);
    }

protected:
    InitiatorTester m_initiator;
    gs::router<DEFAULT_TLM_BUSWIDTH> m_router;
    TargetTester* m_default_target; // Default target to bind router's initiator socket

    std::vector<TargetTester*> m_targets;
    std::vector<uint64_t> m_target_bases; // Base address for each target
    std::vector<size_t> m_target_sizes;   // Size for each target

    uint8_t m_empty_data[1000] = { 0xDE, 0xAD, 0xBE, 0xEF };

    // This will store the last transaction received by a target tester
    struct TargetLastTxn {
        uint64_t addr;
        size_t len;
        tlm::tlm_command cmd;
        int id;     // ID of the target tester that received the transaction
        bool valid; // True if a transaction was received

        // Constructor to properly initialize members
        TargetLastTxn(): addr(0), len(0), cmd(tlm::TLM_IGNORE_COMMAND), id(-1), valid(false) {}

        // Constructor for direct assignment
        TargetLastTxn(uint64_t a, size_t l, tlm::tlm_command c, int i, bool v): addr(a), len(l), cmd(c), id(i), valid(v)
        {
        }
    };
    TargetLastTxn m_last_target_txn;

    // A distinct ID for the default target to be used in m_last_target_txn
    const int m_default_target_id;

public: // Public getters and setters for test access
    const TargetLastTxn& get_last_target_txn() const { return m_last_target_txn; }
    void reset_last_target_txn()
    {
        m_last_target_txn.valid = false;
        m_last_target_txn.id = -1;
    }
    const InitiatorTester& get_initiator() const { return m_initiator; }

protected:
    /* Initiator callback */
    void invalidate_direct_mem_ptr(uint64_t start_range, uint64_t end_range)
    {
        // This callback is expected for DMI invalidation tests, so do not fail here by default.
        // Specific tests will check if it's called.
    }

    /* Target callbacks */
    TlmResponseStatus target_access(int id, uint64_t addr, uint8_t* data, size_t len, tlm::tlm_command cmd)
    {
        uint64_t absolute_addr = addr;
        if (id != m_default_target_id) {
            // For explicitly added targets, addr is an offset from the target's base
            absolute_addr = addr + m_target_bases[id];
        }
        m_last_target_txn = { absolute_addr, len, cmd, id, true };

        // Only set DMI allowed for specific targets, not the default catch-all target
        if (id != m_default_target_id && id < m_targets.size()) {
            m_targets[id]->get_cur_txn().set_dmi_allowed(true);
        }
        return tlm::TLM_OK_RESPONSE;
    }

    bool get_direct_mem_ptr(int id, uint64_t addr, TlmDmi& dmi_data)
    {
        uint64_t target_size_or_end_addr;
        if (id == m_default_target_id) {
            target_size_or_end_addr = std::numeric_limits<uint64_t>::max(); // Default target covers max range
        } else {
            target_size_or_end_addr = m_target_sizes[id] - 1; // Last valid address
        }

        if (addr > target_size_or_end_addr) { // Check if address is beyond the valid range
            return false;
        } else {
            dmi_data.allow_read_write();
            dmi_data.set_dmi_ptr(nullptr);
            dmi_data.set_start_address(0);
            dmi_data.set_end_address(target_size_or_end_addr);
            return true;
        }
    }

    void check_txn(int expected_id, bool is_load, uint64_t addr, size_t len, TlmResponseStatus expected_ret)
    {
        tlm::tlm_command cmd = is_load ? tlm::TLM_READ_COMMAND : tlm::TLM_WRITE_COMMAND;

        if (expected_ret == tlm::TLM_OK_RESPONSE) {
            SCP_INFO(this->name())
            ("Transaction check: Expected ID {}, Addr {:#x}, Len {}, Cmd {}", expected_id, addr, len, cmd);
            ASSERT_TRUE(m_last_target_txn.valid);
            ASSERT_EQ(m_last_target_txn.id, expected_id);
            ASSERT_EQ(m_last_target_txn.addr, addr);
            ASSERT_EQ(m_last_target_txn.len, len);
            ASSERT_EQ(m_last_target_txn.cmd, cmd);
        } else {
            SCP_INFO(this->name())("Transaction check: Expected error for Addr {:#x}, Cmd {}", addr, cmd);
            ASSERT_FALSE(m_last_target_txn.valid); // Transaction should not reach any target
            // More specific checks for TLM_ADDRESS_ERROR_RESPONSE can be added if needed
        }
    }

    void check_txn_debug(int expected_id, bool is_load, uint64_t addr, size_t len, int expected_ret_dbg)
    {
        // For debug, ret is bytes transferred, 0 if error or unmapped
        tlm::tlm_command cmd = is_load ? tlm::TLM_READ_COMMAND : tlm::TLM_WRITE_COMMAND;

        if (expected_ret_dbg > 0) { // Bytes transferred, means it hit
            SCP_INFO(this->name())
            ("Debug transaction check: Expected ID {}, Addr {:#x}, Len {}, Cmd {}, Ret {}", expected_id, addr, len, cmd,
             expected_ret_dbg);
            ASSERT_TRUE(m_last_target_txn.valid);
            ASSERT_EQ(m_last_target_txn.id, expected_id);
            ASSERT_EQ(m_last_target_txn.addr, addr);
            ASSERT_EQ(m_last_target_txn.len, len);
            ASSERT_EQ(m_last_target_txn.cmd, cmd);
            ASSERT_EQ(m_initiator.get_last_transport_debug_ret(), expected_ret_dbg);
        } else { // 0 bytes transferred, means it missed or error
            SCP_INFO(this->name())
            ("Debug transaction check: Expected error for Addr {:#x}, Cmd {}, Ret {}", addr, cmd, expected_ret_dbg);
            ASSERT_FALSE(m_last_target_txn.valid);
            ASSERT_EQ(m_initiator.get_last_transport_debug_ret(), expected_ret_dbg);
        }
    }

public:
    // Set up and Tear Down functions to be overridden by GTest fixtures
    virtual void SetUp()
    {
        // Create a default target for the router's initiator_socket
        // This target will catch transactions not explicitly mapped by add_target
        std::string default_target_name = std::string(this->name()) + "_DefaultTarget";
        m_default_target = new TargetTester(default_target_name.c_str(),
                                            std::numeric_limits<uint64_t>::max()); // Max size for default
        m_router.initiator_socket.bind(m_default_target->socket);
        register_target_callbacks(m_default_target, m_default_target_id); // Register callbacks for default target
    }

    virtual void TearDown()
    {
        if (m_default_target) {
            delete m_default_target;
            m_default_target = nullptr;
        }
    }

    // SystemC lifecycle callbacks (These are not virtual in TestBench, so 'override' is removed)
    void before_end_of_elaboration() override
    {
        // Process any targets queued by the test logic before the router initializes
        process_pending_target_setups();
        // The CCI parameters for the default target are now set in sc_main.
    }
    void end_of_elaboration() override {}
    void start_of_simulation() override {}
    void end_of_simulation() override {}

    std::function<void()> m_test_logic; // Function to hold the test specific logic
    virtual void test_bench_body() override
    {
        if (m_test_logic) {
            m_test_logic();
        }
        // sc_core::sc_stop(); // Removed, handled by GTest fixture TearDown
    }

    void do_load_and_check(int id, uint64_t addr, size_t len, bool debug = false,
                           TlmResponseStatus expected_ret = tlm::TLM_OK_RESPONSE)
    {
        m_last_target_txn.valid = false; // Reset for each transaction
        m_initiator.do_read_with_ptr(addr, m_empty_data, len, debug);
        if (!debug) {
            check_txn(id, true, addr, len, expected_ret);
        } else {
            check_txn_debug(id, true, addr, len, expected_ret == tlm::TLM_OK_RESPONSE ? len : 0);
        }
    }

    void do_store_and_check(int id, uint64_t addr, size_t len, bool debug = false,
                            TlmResponseStatus expected_ret = tlm::TLM_OK_RESPONSE)
    {
        m_last_target_txn.valid = false; // Reset for each transaction
        m_initiator.do_write_with_ptr(addr, m_empty_data, len, debug);
        if (!debug) {
            check_txn(id, false, addr, len, expected_ret);
        } else {
            check_txn_debug(id, false, addr, len, expected_ret == tlm::TLM_OK_RESPONSE ? len : 0);
        }
    }

    void do_good_dmi_request_and_check(int expected_id, uint64_t addr, uint64_t exp_start, uint64_t exp_end)
    {
        SCP_INFO(this->name())("Performing good DMI request to address {:#x}", addr);
        using namespace tlm;
        bool ret = m_initiator.do_dmi_request(addr);
        const tlm_dmi& dmi_data = m_initiator.get_last_dmi_data();

        ASSERT_TRUE(ret);
        // DMI ranges from router include the target base address
        ASSERT_EQ(exp_start, dmi_data.get_start_address());
        ASSERT_EQ(exp_end, dmi_data.get_end_address());
        SCP_INFO(this->name())
        ("Good DMI request to address {:#x} successful. Range: {:#x}-{:#x}", addr, exp_start, exp_end);
    }

    void do_bad_dmi_request_and_check(uint64_t addr)
    {
        SCP_INFO(this->name())("Performing bad DMI request to address {:#x}", addr);
        using namespace tlm;
        bool ret = m_initiator.do_dmi_request(addr);
        ASSERT_FALSE(ret);
        SCP_INFO(this->name())("Bad DMI request to address {:#x} successful (expected failure)", addr);
    }

public:
    // Store target setup parameters to be processed later in before_end_of_elaboration
    void setup_target(int id, uint64_t base_addr, size_t size, bool use_offset = true, int priority = 0)
    {
        m_pending_target_setups.push_back({ id, base_addr, size, use_offset, priority });
        SCP_INFO(this->name())
        ("Queued target setup {}: base_addr={:#x}, size={:#x}, use_offset={}, priority={}", id, base_addr, size,
         use_offset, priority);
    }

    // Process pending target setups (called in before_end_of_elaboration)
    void process_pending_target_setups()
    {
        for (const auto& params : m_pending_target_setups) {
            char txt[20];
            snprintf(txt, 20, "Target_%d", params.id);
            TargetTester* new_target = new TargetTester(txt, params.size);
            SCP_INFO(this->name())
            ("Processing target {}: base_addr={:#x}, size={:#x}, use_offset={}, priority={}", params.id,
             params.base_addr, params.size, params.use_offset, params.priority);
            m_targets.push_back(new_target);
            m_target_bases.push_back(params.base_addr);
            m_target_sizes.push_back(params.size);

            // Register callbacks for the new target
            new_target->register_write_cb(
                [id = params.id, this](uint64_t addr, uint8_t* data, size_t len) -> TlmResponseStatus {
                    SCP_INFO(this->name())("Target {} received write access: addr={:#x}, len={}", id, addr, len);
                    return target_access(id, addr, data, len, tlm::TLM_WRITE_COMMAND);
                });
            new_target->register_read_cb(
                [id = params.id, this](uint64_t addr, uint8_t* data, size_t len) -> TlmResponseStatus {
                    SCP_INFO(this->name())("Target {} received read access: addr={:#x}, len={}", id, addr, len);
                    return target_access(id, addr, data, len, tlm::TLM_READ_COMMAND);
                });
            new_target->register_debug_read_cb(
                [id = params.id, this](uint64_t addr, uint8_t* data, size_t len) -> TlmResponseStatus {
                    SCP_INFO(this->name())("Target {} received debug read access: addr={:#x}, len={}", id, addr, len);
                    return target_access(id, addr, data, len, tlm::TLM_READ_COMMAND);
                });
            new_target->register_debug_write_cb(
                [id = params.id, this](uint64_t addr, uint8_t* data, size_t len) -> TlmResponseStatus {
                    SCP_INFO(this->name())("Target {} received debug write access: addr={:#x}, len={}", id, addr, len);
                    return target_access(id, addr, data, len, tlm::TLM_WRITE_COMMAND);
                });
            new_target->register_get_direct_mem_ptr_cb([id = params.id, this](uint64_t addr, TlmDmi& dmi_data) -> bool {
                SCP_INFO(this->name())("Target {} received DMI request for addr={:#x}", id, addr);
                return get_direct_mem_ptr(id, addr, dmi_data);
            });

            m_router.add_target(new_target->socket, params.base_addr, params.size, params.use_offset, params.priority);
            SCP_INFO(this->name())("Added target {} to router", params.id);
        }
        m_pending_target_setups.clear(); // Clear after processing
    }

    // Helper to register callbacks for a given target
    void register_target_callbacks(TargetTester* target, int id)
    {
        target->register_write_cb([id, this](uint64_t addr, uint8_t* data, size_t len) -> TlmResponseStatus {
            SCP_INFO(this->name())("Target {} received write access: addr={:#x}, len={}", id, addr, len);
            return target_access(id, addr, data, len, tlm::TLM_WRITE_COMMAND);
        });
        target->register_read_cb([id, this](uint64_t addr, uint8_t* data, size_t len) -> TlmResponseStatus {
            SCP_INFO(this->name())("Target {} received read access: addr={:#x}, len={}", id, addr, len);
            return target_access(id, addr, data, len, tlm::TLM_READ_COMMAND);
        });
        target->register_debug_read_cb([id, this](uint64_t addr, uint8_t* data, size_t len) -> TlmResponseStatus {
            SCP_INFO(this->name())("Target {} received debug read access: addr={:#x}, len={}", id, addr, len);
            return target_access(id, addr, data, len, tlm::TLM_READ_COMMAND);
        });
        target->register_debug_write_cb([id, this](uint64_t addr, uint8_t* data, size_t len) -> TlmResponseStatus {
            SCP_INFO(this->name())("Target {} received debug write access: addr={:#x}, len={}", id, addr, len);
            return target_access(id, addr, data, len, tlm::TLM_WRITE_COMMAND);
        });
        target->register_get_direct_mem_ptr_cb([id, this](uint64_t addr, TlmDmi& dmi_data) -> bool {
            SCP_INFO(this->name())("Target {} received DMI request for addr={:#x}", id, addr);
            return get_direct_mem_ptr(id, addr, dmi_data);
        });
    }

private:
    // Clear all targets - PRIVATE to prevent segfaults during simulation
    // Once SystemC modules are created and bound, they cannot be destroyed during simulation
    void clear_targets()
    {
        for (TargetTester* target : m_targets) {
            delete target;
        }
        m_targets.clear();
        m_target_bases.clear();
        m_target_sizes.clear();
    }

public:
    virtual ~RouterTestBenchSimple()
    {
        clear_targets();
        if (m_default_target) {
            delete m_default_target;
        }
    }
};
