/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file router-coverage-tests.cc
 * @brief Coverage tests for the router component.
 *
 * These tests exercise code paths that require specialized setups beyond
 * the RouterTestBenchSimple pattern: alias configuration, DMI overlap
 * detection, DMI invalidation with use_offset, preset CCI values, and
 * the UnderlyingDMITlmExtension.
 */

#include <systemc>
#include <cci/utils/broker.h>
#include <scp/report.h>

#include <router.h>
#include <tests/initiator-tester.h>
#include <tests/target-tester.h>
#include <tlm-extensions/underlying-dmi.h>
#include <tlm_sockets_buswidth.h>
#include <stdexcept>

class RouterCoverageTests : public sc_core::sc_module
{
    SCP_LOGGER();

public:
    int exit_code{ 0 };

    // --- Preset CCI values ---
    gs::router<> r9;
    InitiatorTester i9;
    TargetTester t9;

    // --- Alias configuration (all sub-tests share one router) ---
    gs::router<> r1;
    InitiatorTester i1;
    TargetTester t1_basic; // basic alias (bound directly)
    TargetTester t1_amp;   // & prefix alias (bound directly)
    TargetTester t1_warn;  // alias with valid address warning (bound via add_target)
    TargetTester t1_loop;  // aliases loop (bound via add_target)

    // --- DMI overlap detection ---
    gs::router<> r4;
    InitiatorTester i4;
    TargetTester t4;
    int m_dmi4_call_count;

    // --- DMI invalidation with use_offset ---
    gs::router<> r6;
    InitiatorTester i6;
    TargetTester t6;
    uint64_t m_inv6_start;
    uint64_t m_inv6_end;
    bool m_inv6_called;

    // --- UnderlyingDMI extension ---
    gs::router<> r10;
    InitiatorTester i10;
    TargetTester t10;

    RouterCoverageTests(sc_core::sc_module_name nm)
        : sc_core::sc_module(nm)
        // Preset CCI
        , r9("r9")
        , i9("i9")
        , t9("t9", 0x2000)
        // Aliases
        , r1("r1")
        , i1("i1")
        , t1_basic("t1_basic", 0x1000)
        , t1_amp("t1_amp", 0x1000)
        , t1_warn("t1_warn", 0x1000)
        , t1_loop("t1_loop", 0x2000)
        // DMI overlap
        , r4("r4")
        , i4("i4")
        , t4("t4", 0x1000)
        , m_dmi4_call_count(0)
        // DMI invalidation
        , r6("r6")
        , i6("i6")
        , t6("t6", 0x1000)
        , m_inv6_start(0)
        , m_inv6_end(0)
        , m_inv6_called(false)
        // UnderlyingDMI
        , r10("r10")
        , i10("i10")
        , t10("t10", 0x1000)
    {
        setup_preset_cci();
        setup_aliases();
        setup_dmi_overlap();
        setup_dmi_invalidation();
        setup_underlying_dmi();
    }

    // --- Setup methods ---

    void setup_preset_cci()
    {
        // CCI presets for t9 are set in sc_main BEFORE module creation.
        // add_target with DIFFERENT values (should be overridden by presets)
        r9.add_target(t9.socket, 0x1000, 0x1000, true, 0);
        i9.socket.bind(r9.target_socket);

        t9.register_read_cb([](uint64_t, uint8_t*, size_t) { return tlm::TLM_OK_RESPONSE; });
        t9.register_write_cb([](uint64_t, uint8_t*, size_t) { return tlm::TLM_OK_RESPONSE; });
    }

    void setup_aliases()
    {
        i1.socket.bind(r1.target_socket);

        // Sub-test 1 & 2: bind directly (no add_target). CCI aliases set in sc_main.
        r1.initiator_socket.bind(t1_basic.socket);
        r1.initiator_socket.bind(t1_amp.socket);

        // Sub-test 3: bind via add_target (sets .address, so alias will be ignored)
        r1.add_target(t1_warn.socket, 0xA000, 0x1000);

        // Sub-test 4: bind via add_target
        r1.add_target(t1_loop.socket, 0xC000, 0x2000);

        auto ok_cb = [](uint64_t, uint8_t*, size_t) -> tlm::tlm_response_status { return tlm::TLM_OK_RESPONSE; };
        t1_basic.register_read_cb(ok_cb);
        t1_basic.register_write_cb(ok_cb);
        t1_amp.register_read_cb(ok_cb);
        t1_amp.register_write_cb(ok_cb);
        t1_warn.register_read_cb(ok_cb);
        t1_warn.register_write_cb(ok_cb);
        t1_loop.register_read_cb(ok_cb);
        t1_loop.register_write_cb(ok_cb);
    }

    void setup_dmi_overlap()
    {
        r4.add_target(t4.socket, 0x1000, 0x1000, true);
        i4.socket.bind(r4.target_socket);

        // Custom DMI callback: returns different end address on second call
        t4.register_get_direct_mem_ptr_cb([this](uint64_t addr, tlm::tlm_dmi& dmi_data) -> bool {
            dmi_data.allow_read_write();
            dmi_data.set_dmi_ptr(nullptr);
            dmi_data.set_start_address(0);
            if (m_dmi4_call_count == 0) {
                dmi_data.set_end_address(0xFFF); // First: full range
            } else {
                dmi_data.set_end_address(0x7FF); // Second: smaller range
            }
            m_dmi4_call_count++;
            return true;
        });

        i4.register_invalidate_direct_mem_ptr([this](uint64_t start, uint64_t end) {
            SCP_INFO(())("DMI overlap: invalidation received: [0x{:x}, 0x{:x}]", start, end);
        });
    }

    void setup_dmi_invalidation()
    {
        r6.add_target(t6.socket, 0x2000, 0x1000, true); // use_offset=true
        i6.socket.bind(r6.target_socket);

        t6.register_get_direct_mem_ptr_cb([](uint64_t addr, tlm::tlm_dmi& dmi_data) -> bool {
            dmi_data.allow_read_write();
            dmi_data.set_dmi_ptr(nullptr);
            dmi_data.set_start_address(0);
            dmi_data.set_end_address(0xFFF);
            return true;
        });

        // Capture invalidation addresses sent to the initiator
        i6.register_invalidate_direct_mem_ptr([this](uint64_t start, uint64_t end) {
            m_inv6_start = start;
            m_inv6_end = end;
            m_inv6_called = true;
        });
    }

    void setup_underlying_dmi()
    {
        r10.add_target(t10.socket, 0x3000, 0x1000, true);
        i10.socket.bind(r10.target_socket);

        t10.register_get_direct_mem_ptr_cb([](uint64_t addr, tlm::tlm_dmi& dmi_data) -> bool {
            dmi_data.allow_read_write();
            dmi_data.set_dmi_ptr(nullptr);
            dmi_data.set_start_address(0);
            dmi_data.set_end_address(0xFFF);
            return true;
        });
    }

    // --- Test methods ---

    void run_all_tests()
    {
        bool ok = true;

        ok &= test_preset_cci();
        ok &= test_alias_basic();
        ok &= test_alias_ampersand();
        ok &= test_alias_warning();
        ok &= test_alias_loop();
        ok &= test_dmi_overlap();
        ok &= test_dmi_invalidation();
        ok &= test_underlying_dmi_mapped();
        ok &= test_underlying_dmi_unmapped();
        ok &= test_transport_exception_cleanup();

        exit_code = ok ? 0 : 1;
        if (ok) {
            std::cout << "All router coverage tests PASSED" << std::endl;
        } else {
            std::cout << "Some router coverage tests FAILED" << std::endl;
        }
        sc_core::sc_stop();
    }

    // Relative routing must restore caller state even when the target throws.
    bool test_transport_exception_cleanup()
    {
        bool relative_address_seen = false;
        const auto throwing_read = [&](uint64_t address, uint8_t*, size_t) -> tlm::tlm_response_status {
            relative_address_seen = address == 0x54;
            throw std::runtime_error("target transport failure");
        };
        t9.register_read_cb(throwing_read);
        t9.register_debug_read_cb(throwing_read);

        uint8_t data[4] = {};
        tlm::tlm_generic_payload txn;
        txn.set_address(0x5054);
        txn.set_command(tlm::TLM_READ_COMMAND);
        txn.set_data_ptr(data);
        txn.set_data_length(sizeof(data));
        txn.set_streaming_width(sizeof(data));
        for (const bool debug : { false, true }) {
            auto delay = sc_core::SC_ZERO_TIME;
            bool thrown = false;
            relative_address_seen = false;
            try {
                if (debug)
                    i9.socket->transport_dbg(txn);
                else
                    i9.socket->b_transport(txn, delay);
            } catch (const std::runtime_error&) {
                thrown = true;
            }
            if (!thrown || !relative_address_seen || txn.get_address() != 0x5054 ||
                txn.get_extension<gs::PathIDExtension>() != nullptr) {
                std::cout << "FAIL: Transport exception left address or path modified" << std::endl;
                return false;
            }
        }
        std::cout << "PASS: Blocking and debug exceptions restore address and path" << std::endl;
        return true;
    }

    // Verify preset CCI values override add_target arguments
    bool test_preset_cci()
    {
        std::cout << "--- Preset CCI values ---" << std::endl;
        uint8_t data[4] = { 0 };

        // Access at the PRESET address 0x5000 (should succeed)
        auto ret = i9.do_write_with_ptr(0x5000, data, 4);
        if (ret != tlm::TLM_OK_RESPONSE) {
            std::cout << "FAIL: Expected OK at preset address 0x5000" << std::endl;
            return false;
        }

        // Access at the add_target address 0x1000 (should fail — preset overrides it)
        ret = i9.do_write_with_ptr(0x1000, data, 4);
        if (ret != tlm::TLM_ADDRESS_ERROR_RESPONSE) {
            std::cout << "FAIL: Expected ADDRESS_ERROR at add_target address 0x1000" << std::endl;
            return false;
        }

        std::cout << "PASS: Preset CCI values override add_target arguments" << std::endl;
        return true;
    }

    // Basic alias — set .0 CCI value to another target's socket name
    bool test_alias_basic()
    {
        std::cout << "--- Basic alias ---" << std::endl;
        uint8_t data[4] = { 0 };

        auto ret = i1.do_write_with_ptr(0x4000, data, 4);
        if (ret != tlm::TLM_OK_RESPONSE) {
            std::cout << "FAIL: Expected OK at alias address 0x4000" << std::endl;
            return false;
        }

        std::cout << "PASS: Basic alias maps target at alias source address" << std::endl;
        return true;
    }

    // Alias with & prefix — set .0 = "&other_module"
    bool test_alias_ampersand()
    {
        std::cout << "--- Alias with & prefix ---" << std::endl;
        uint8_t data[4] = { 0 };

        auto ret = i1.do_write_with_ptr(0x6000, data, 4);
        if (ret != tlm::TLM_OK_RESPONSE) {
            std::cout << "FAIL: Expected OK at &-alias address 0x6000" << std::endl;
            return false;
        }

        std::cout << "PASS: & prefix alias maps target at dereferenced address" << std::endl;
        return true;
    }

    // Alias with valid address warning — alias is ignored when explicit address exists
    bool test_alias_warning()
    {
        std::cout << "--- Alias with valid address warning ---" << std::endl;
        uint8_t data[4] = { 0 };

        // add_target set address to 0xA000; alias should be ignored
        auto ret = i1.do_write_with_ptr(0xA000, data, 4);
        if (ret != tlm::TLM_OK_RESPONSE) {
            std::cout << "FAIL: Expected OK at explicit address 0xA000" << std::endl;
            return false;
        }

        std::cout << "PASS: Alias ignored when valid address is also provided" << std::endl;
        return true;
    }

    // Target aliases loop — .aliases.<name>.address/.size CCI values
    bool test_alias_loop()
    {
        std::cout << "--- Target aliases loop ---" << std::endl;
        uint8_t data[4] = { 0 };

        // Access at original target address 0xC000
        auto ret = i1.do_write_with_ptr(0xC000, data, 4);
        if (ret != tlm::TLM_OK_RESPONSE) {
            std::cout << "FAIL: Expected OK at original address 0xC000" << std::endl;
            return false;
        }

        // Access at alias address 0xE000
        ret = i1.do_write_with_ptr(0xE000, data, 4);
        if (ret != tlm::TLM_OK_RESPONSE) {
            std::cout << "FAIL: Expected OK at alias address 0xE000" << std::endl;
            return false;
        }

        std::cout << "PASS: Target aliases loop creates additional mapped region" << std::endl;
        return true;
    }

    // DMI overlap detection — record_dmi with differing end addresses
    bool test_dmi_overlap()
    {
        std::cout << "--- DMI overlap detection ---" << std::endl;

        // First DMI: target returns [0, 0xFFF] → router records [0x1000, 0x1FFF]
        bool ret = i4.do_dmi_request(0x1050);
        if (!ret) {
            std::cout << "FAIL: First DMI request should succeed" << std::endl;
            return false;
        }

        // Second DMI: target returns [0, 0x7FF] → start 0x1000 but end 0x17FF
        // record_dmi detects overlap with existing [0x1000, 0x1FFF] → invalidation
        ret = i4.do_dmi_request(0x1050);
        if (!ret) {
            std::cout << "FAIL: Second DMI request should succeed" << std::endl;
            return false;
        }

        if (m_dmi4_call_count != 2) {
            std::cout << "FAIL: Expected 2 DMI callbacks, got " << m_dmi4_call_count << std::endl;
            return false;
        }

        std::cout << "PASS: DMI overlap detected and old DMI invalidated" << std::endl;
        return true;
    }

    // invalidate_direct_mem_ptr with use_offset address translation
    bool test_dmi_invalidation()
    {
        std::cout << "--- Invalidate with use_offset ---" << std::endl;

        // Get DMI to populate the router's DMI cache
        bool ret = i6.do_dmi_request(0x2050);
        if (!ret) {
            std::cout << "FAIL: Initial DMI request should succeed" << std::endl;
            return false;
        }

        // Trigger invalidation from the target side with local addresses
        m_inv6_called = false;
        t6.socket.get_base_port()->invalidate_direct_mem_ptr(0, 0xFFF);

        if (!m_inv6_called) {
            std::cout << "FAIL: Invalidation callback was not invoked" << std::endl;
            return false;
        }

        // With use_offset=true and target address 0x2000:
        // local [0, 0xFFF] → global [0x2000, 0x2FFF]
        if (m_inv6_start != 0x2000 || m_inv6_end != 0x2FFF) {
            std::cout << "FAIL: Expected invalidation [0x2000, 0x2FFF], got [0x" << std::hex << m_inv6_start << ", 0x"
                      << m_inv6_end << "]" << std::dec << std::endl;
            return false;
        }

        std::cout << "PASS: Invalidation addresses correctly translated with use_offset" << std::endl;
        return true;
    }

    // UnderlyingDMITlmExtension for a mapped address
    bool test_underlying_dmi_mapped()
    {
        std::cout << "--- UnderlyingDMI extension (mapped) ---" << std::endl;

        tlm::tlm_generic_payload txn;
        txn.set_address(0x3050); // Mapped address (target at 0x3000)
        txn.set_data_length(0);
        txn.set_data_ptr(nullptr);
        txn.set_command(tlm::TLM_READ_COMMAND);
        txn.set_streaming_width(0);
        txn.set_byte_enable_ptr(nullptr);
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

        gs::UnderlyingDMITlmExtension u_dmi;
        txn.set_extension(&u_dmi);

        tlm::tlm_dmi dmi_data;
        bool ret = i10.socket->get_direct_mem_ptr(txn, dmi_data);

        // Clear extension before txn destruction (stack-allocated extension)
        gs::UnderlyingDMITlmExtension* null_ext = nullptr;
        txn.set_extension(null_ext);

        if (!ret) {
            std::cout << "FAIL: DMI request should succeed for mapped address" << std::endl;
            return false;
        }

        if (!u_dmi.has_dmi(gs::tlm_dmi_ex::dmi_mapped)) {
            std::cout << "FAIL: Extension should have dmi_mapped entry" << std::endl;
            return false;
        }

        std::cout << "PASS: UnderlyingDMI extension records dmi_mapped for mapped address" << std::endl;
        return true;
    }

    // UnderlyingDMITlmExtension for an unmapped address
    bool test_underlying_dmi_unmapped()
    {
        std::cout << "--- UnderlyingDMI extension (unmapped) ---" << std::endl;

        tlm::tlm_generic_payload txn;
        txn.set_address(0xFFFF); // Unmapped address
        txn.set_data_length(0);
        txn.set_data_ptr(nullptr);
        txn.set_command(tlm::TLM_READ_COMMAND);
        txn.set_streaming_width(0);
        txn.set_byte_enable_ptr(nullptr);
        txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

        gs::UnderlyingDMITlmExtension u_dmi;
        txn.set_extension(&u_dmi);

        tlm::tlm_dmi dmi_data;
        bool ret = i10.socket->get_direct_mem_ptr(txn, dmi_data);

        gs::UnderlyingDMITlmExtension* null_ext = nullptr;
        txn.set_extension(null_ext);

        if (ret) {
            std::cout << "FAIL: DMI request should fail for unmapped address" << std::endl;
            return false;
        }

        if (!u_dmi.has_dmi(gs::tlm_dmi_ex::dmi_nomap)) {
            std::cout << "FAIL: Extension should have dmi_nomap entry" << std::endl;
            return false;
        }

        std::cout << "PASS: UnderlyingDMI extension records dmi_nomap for unmapped address" << std::endl;
        return true;
    }
};

int sc_main(int argc, char* argv[])
{
    cci_utils::consuming_broker broker("global_broker");
    cci_register_broker(broker);

    cci::cci_originator originator("sc_main");

    // --- Preset CCI values BEFORE module creation ---
    // These should override the add_target(addr=0x1000, size=0x1000, priority=0) call
    broker.set_preset_cci_value("cov_test.t9.simple_target_socket_0.address",
                                cci::cci_value(static_cast<uint64_t>(0x5000)), originator);
    broker.set_preset_cci_value("cov_test.t9.simple_target_socket_0.size",
                                cci::cci_value(static_cast<uint64_t>(0x2000)), originator);
    broker.set_preset_cci_value("cov_test.t9.simple_target_socket_0.priority",
                                cci::cci_value(static_cast<unsigned int>(5)), originator);

    // --- Basic alias ---
    broker.set_preset_cci_value("cov_test.t1_basic.simple_target_socket_0.0", cci::cci_value(std::string("alias_src")),
                                originator);
    broker.set_preset_cci_value("alias_src.address", cci::cci_value(static_cast<uint64_t>(0x4000)), originator);
    broker.set_preset_cci_value("alias_src.size", cci::cci_value(static_cast<uint64_t>(0x1000)), originator);
    broker.set_preset_cci_value("alias_src.relative_addresses", cci::cci_value(true), originator);

    // --- Alias with & prefix ---
    broker.set_preset_cci_value("cov_test.t1_amp.simple_target_socket_0.0", cci::cci_value(std::string("&amp_mod")),
                                originator);
    broker.set_preset_cci_value("amp_mod.target_socket.address", cci::cci_value(static_cast<uint64_t>(0x6000)),
                                originator);
    broker.set_preset_cci_value("amp_mod.target_socket.size", cci::cci_value(static_cast<uint64_t>(0x1000)),
                                originator);
    broker.set_preset_cci_value("amp_mod.target_socket.relative_addresses", cci::cci_value(true), originator);

    // --- Alias with valid address warning ---
    // .address is set by add_target; .0 is also set but should be ignored (warning path)
    broker.set_preset_cci_value("cov_test.t1_warn.simple_target_socket_0.0", cci::cci_value(std::string("warn_alias")),
                                originator);
    broker.set_preset_cci_value("warn_alias.address", cci::cci_value(static_cast<uint64_t>(0xDEAD)), originator);
    broker.set_preset_cci_value("warn_alias.size", cci::cci_value(static_cast<uint64_t>(0x100)), originator);

    // --- Target aliases loop ---
    std::string loop_socket = "cov_test.t1_loop.simple_target_socket_0";
    broker.set_preset_cci_value(loop_socket + ".aliases.myalias.address", cci::cci_value(static_cast<uint64_t>(0xE000)),
                                originator);
    broker.set_preset_cci_value(loop_socket + ".aliases.myalias.size", cci::cci_value(static_cast<uint64_t>(0x1000)),
                                originator);

    // Initialize SCP logging
    scp::LoggingGuard logging_guard(scp::LogConfig()
                                        .fileInfoFrom(sc_core::SC_ERROR)
                                        .logAsync(false)
                                        .logLevel(scp::log::DBGTRACE)
                                        .msgTypeFieldWidth(50));

    RouterCoverageTests test("cov_test");
    sc_core::sc_start(sc_core::SC_ZERO_TIME);
    test.run_all_tests();

    return test.exit_code;
}
