/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2022
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _GREENSOCS_BASE_COMPONENTS_MEMORY_H
#define _GREENSOCS_BASE_COMPONENTS_MEMORY_H

#include <fstream>
#include <memory>

#include <cci_configuration>
#include <systemc>
#include <tlm>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <scp/report.h>
#include <scp/helpers.h>

#include <loader.h>
#include <memory_services.h>

#include <tlm-extensions/shmem_extension.h>
#include <module_factory_registery.h>
#include <tlm_sockets_buswidth.h>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#endif

namespace gs {

/**
 * @class gs_memory
 *
 * @brief A gs_memory component that can add memory to a virtual platform project
 *
 * @details This component models a memory. It has a simple target socket so any other component
 * with an initiator socket can connect to this component. It behaves as follows:
 *    - The gs_memory does not manage time in any way
 *    - It is only an LT model, and does not handle AT transactions
 *    - It does not manage exclusive accesses
 *    - You can manage the size of the memory during the initialization of the component
 *    - gs_memory does not allocate individual "pages" but a single large block
 *    - It supports DMI requests with the method `get_direct_mem_ptr`
 *    - DMI invalidates are not issued.
 */
#define ALIGNEDBITS 12

template <unsigned int BUSWIDTH = DEFAULT_TLM_BUSWIDTH>
class gs_memory : public sc_core::sc_module
{
    uint64_t m_size = 0;
    uint64_t m_address;
    bool m_address_valid = false;
    bool m_relative_addresses;

    SCP_LOGGER(());

    // Templated on the power of 2 to use to divide the blocks up
    template <unsigned int N = 2>
    class SubBlock
    {
        SCP_LOGGER();
        uint64_t m_len;     // size of the bloc
        uint64_t m_address; // this is an absolute address and this is the
                            // beginning of the bloc/Memory
        gs_memory<BUSWIDTH>& m_mem;

        uint8_t* m_ptr = nullptr;
        std::array<std::unique_ptr<SubBlock>, (1 << N)> m_sub_blocks;
        bool m_use_sub_blocks = false;

        bool m_mapped = false;
        ShmemIDExtension m_shmemID;
        bool m_aligned = false;

    public:
        SubBlock(uint64_t address, uint64_t len, gs_memory& mem): m_len(len), m_address(address), m_mem(mem)
        {
            SCP_TRACE(())("Init");
        }

        void doreset()
        {
            SCP_WARN((), m_mem.name())("Reset (block at offset {:x})", m_address);
            for (unsigned int i = 0; i < N; i++) {
                if (m_sub_blocks[i]) m_sub_blocks[i]->doreset();
            }
            if (m_mem.p_init_mem && m_ptr) {
                memset(m_ptr, m_mem.p_init_mem_val, m_len);
            }
        }

        SubBlock& access(uint64_t address)
        {
            // address is the address of where we want to write/read
            // len is the size of the data

            if (m_ptr && !m_use_sub_blocks) {
                assert(address >= m_address);
                return *this;
            }
            if (m_len > m_mem.p_max_block_size) {
                m_use_sub_blocks = true;
            }

            if (!m_use_sub_blocks) {
                if (!((std::string)m_mem.p_mapfile).empty()) {
                    if ((m_ptr = MemoryServices::get().map_file(((std::string)(m_mem.p_mapfile)), m_len, m_address)) !=
                        nullptr) {
                        m_mapped = true;
                        return *this;
                    }
                }
                if (m_mem.p_shmem) {
                    std::stringstream shmname_stream;
#if defined(_WIN32)
                    DWORD pid = GetCurrentProcessId();
#else
                    pid_t pid = getpid();
#endif
                    if (m_mem.p_shmem_prefix.get_value() != "")
                        shmname_stream << "/" << m_mem.p_shmem_prefix.get_value() << std::hex << pid;
                    else
                        shmname_stream << "/" << std::hex << pid << "-" << std::hex
                                       << MemoryServices::get().get_shmem_seg_num();
                    if (shmname_stream.str().size() > 31) { /*PSHMNAMLEN in Mac OS*/
                        size_t hash = std::hash<std::string>{}(
                            shmname_stream.str()); // hash length is 16 hex digits in 64 bit machines.
                        shmname_stream.str("");    // clear
                        shmname_stream << "/" << std::hex << hash;
                    }
                    std::string shmname = shmname_stream.str();
                    int shm_fd = -1;
                    if ((m_ptr = MemoryServices::get().map_mem_create(shmname, m_len, &shm_fd)) != nullptr) {
                        m_mapped = true;
                        m_shmemID = ShmemIDExtension(shmname, (uint64_t)m_ptr, m_len, shm_fd);
                        return *this;
                    }
                }

                AllocatedMemory alloc_mem = MemoryServices::get().alloc(m_len);
                if (alloc_mem.ptr != nullptr) {
                    m_ptr = alloc_mem.ptr;
                    m_aligned = alloc_mem.is_aligned;
                    if (m_mem.p_init_mem) memset(m_ptr, m_mem.p_init_mem_val, m_len);
                    return *this;
                }

                // else we failed to allocate, try with a smaller sub_block size.
                m_use_sub_blocks = true;
            }

            if (m_len < m_mem.p_min_block_size) {
                SCP_FATAL(m_mem.name()) << "Unable to allocate memory!"; // out of memory!
            }

            // return a index of sub_bloc
            assert((m_len & ~(-1llu << N)) == 0);
            uint64_t m_sub_size = m_len >> N;
            int i = (address - m_address) / (m_sub_size);

            if (!m_sub_blocks[i]) {
                m_sub_blocks[i] = std::make_unique<SubBlock<N>>((i * m_sub_size) + m_address, m_sub_size, m_mem);
            }
            return m_sub_blocks[i]->access(address);
        }

        uint64_t read_sub_blocks(uint8_t* data, uint64_t offset, uint64_t len)
        {
            uint64_t block_offset = offset - m_address;
            uint64_t block_len = m_len - block_offset;
            uint64_t remain_len = (len < block_len) ? len : block_len;

            memcpy(data, &m_ptr[block_offset], remain_len);

            return remain_len;
        }

        uint64_t write_sub_blocks(const uint8_t* data, uint64_t offset, uint64_t len)
        {
            uint64_t block_offset = offset - m_address;
            uint64_t block_len = m_len - block_offset;
            uint64_t remain_len = (len < block_len) ? len : block_len;

            memcpy(&m_ptr[block_offset], data, remain_len);

            return remain_len;
        }

        uint8_t* get_ptr() { return m_ptr; }

        uint64_t get_len() { return m_len; }

        uint64_t get_address() { return m_address; }

        ShmemIDExtension* get_extension()
        {
            if (m_shmemID.empty()) return nullptr;
            return &m_shmemID;
        }

        void free_raw_alloc()
        {
            if (!m_ptr) {
                return;
            }
            if (m_aligned) {
#ifdef _WIN32
                /*WINDOWS aligned alloc/free are done using _aligned_malloc/_aligned_free */
                _aligned_free(m_ptr);
#else
                free(m_ptr);
#endif
            } else {
                free(m_ptr);
            }
        }

        ~SubBlock()
        {
            if (m_mapped) {
                MemoryServices::get().unmap_file(m_ptr, m_len);
            } else {
                free_raw_alloc();
            }
        }
    };

private:
    std::unique_ptr<gs_memory<BUSWIDTH>::SubBlock<>> m_sub_block;
    cci::cci_broker_handle m_broker;

protected:
    virtual bool get_direct_mem_ptr(int id, tlm::tlm_generic_payload& txn, tlm::tlm_dmi& dmi_data)
    {
        if (!p_dmi) return false;
        sc_dt::uint64 addr = txn.get_address();

        if (!m_relative_addresses) {
            if (addr < m_address) {
                txn.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return false;
            }
            addr -= m_address;
        }
        if (addr >= m_size) {
            txn.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return false;
        }

        SCP_TRACE(()) << " : DMI access to address "
                      << "0x" << std::hex << addr;

        if (p_rom)
            dmi_data.allow_read();
        else
            dmi_data.allow_read_write();

        SubBlock<>& blk = m_sub_block->access(addr);

        uint8_t* ptr = blk.get_ptr();
        uint64_t size = blk.get_len();
        uint64_t block_address = blk.get_address();

        dmi_data.set_dmi_ptr(reinterpret_cast<unsigned char*>(ptr));
        if (!m_relative_addresses) {
            dmi_data.set_start_address(block_address + m_address);
            dmi_data.set_end_address((block_address + m_address + size) - 1);
        } else {
            dmi_data.set_start_address(block_address);
            dmi_data.set_end_address((block_address + size) - 1);
        }
        dmi_data.set_read_latency(p_latency);
        dmi_data.set_write_latency(p_latency);

        ShmemIDExtension* ext = blk.get_extension();
        if (ext) {
            txn.set_extension(ext);
        }

        return true;
    }

    virtual void b_transport(int id, tlm::tlm_generic_payload& txn, sc_core::sc_time& delay)
    {
        unsigned int len = txn.get_data_length();
        unsigned char* ptr = txn.get_data_ptr();
        sc_dt::uint64 addr = txn.get_address();
        unsigned char* byt = txn.get_byte_enable_ptr();
        unsigned int bel = txn.get_byte_enable_length();

        if (txn.get_streaming_width() < len) {
            SCP_FATAL(()) << "using streaming width: 0x" << std::hex << txn.get_streaming_width()
                          << " less than data length: 0x" << std::hex << len << " is not supported.";
        }
        SCP_INFO(()) << "Start b_transport :" << get_txn_command_str(txn) << " to address: 0x" << std::hex << addr
                     << " len: 0x" << std::hex << len;

        if (!m_relative_addresses) {
            if (addr < m_address) {
                txn.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
                return;
            }
            addr -= m_address;
        }
        if ((addr + len) > m_size) {
            txn.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        switch (txn.get_command()) {
        case tlm::TLM_READ_COMMAND:
            if (byt) {
                for (unsigned int i = 0; i < len; i++)
                    if (byt[i % bel] == TLM_BYTE_ENABLED) {
                        if (!read(&(ptr[i]), (addr + i), 1)) {
                            SCP_FATAL(()) << "Address + length is out of range of the memory size";
                        }
                    }
            } else {
                if (!read(ptr, addr, len)) {
                    SCP_FATAL(()) << "Address + length is out of range of the memory size";
                }
            }
            break;
        case tlm::TLM_WRITE_COMMAND:
            if (p_rom) {
                txn.set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
                return;
            }
            if (byt) {
                for (unsigned int i = 0; i < len; i++)
                    if (byt[i % bel] == TLM_BYTE_ENABLED) {
                        if (!write(&(ptr[i]), (addr + i), 1)) {
                            SCP_FATAL(()) << "Address + length is out of range of the memory size";
                        }
                    }
            } else {
                if (!write(ptr, addr, len)) {
                    SCP_FATAL(()) << "Address + length is out of range of the memory size";
                }
            }
            break;
        default:
            SCP_FATAL(()) << "TLM command not supported";
            break;
        }
        delay += p_latency;
        txn.set_response_status(tlm::TLM_OK_RESPONSE);
        SCP_INFO(()) << "Completed b_transport :" << scp::scp_txn_tostring(txn);

        if (p_dmi) txn.set_dmi_allowed(true);
    }

    const char* get_txn_command_str(const tlm::tlm_generic_payload& trans)
    {
        switch (trans.get_command()) {
        case tlm::TLM_READ_COMMAND:
            return "READ";
        case tlm::TLM_WRITE_COMMAND:
            return "WRITE";
        case tlm::TLM_IGNORE_COMMAND:
            return "IGNORE";
        default:
            break;
        }
        return "UNKNOWN";
    }

    virtual unsigned int transport_dbg(int id, tlm::tlm_generic_payload& txn)
    {
        unsigned int len = txn.get_data_length();
        sc_dt::uint64 addr = txn.get_address();
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        SCP_INFO(()) << "Start b_transport dbg :" << get_txn_command_str(txn) << " to address: 0x" << std::hex << addr
                     << " len: 0x" << std::hex << len;
        b_transport(id, txn, delay);
        SCP_INFO(()) << "Completed b_transport dbg :" << get_txn_command_str(txn) << " to address: 0x" << std::hex
                     << addr << " len: 0x" << std::hex << len << " status: " << txn.get_response_string();
        if (txn.get_response_status() == tlm::TLM_OK_RESPONSE)
            return len;
        else
            return 0;
    }

    bool read(uint8_t* data, uint64_t offset, uint64_t len)
    {
        // force end of elaboration to ensure we fix the sizes
        // this may happen if another model descides to load data into memory as
        // part of it's initialization during before_end_of_elaboration.

        if (!m_sub_block) before_end_of_elaboration();

        uint64_t remain_len = 0;
        uint64_t data_ptr_offset = 0;

        if (offset + len > m_size) {
            return false;
        }

        while (len > 0) {
            SubBlock<>& blk = m_sub_block->access(offset + data_ptr_offset);

            remain_len = blk.read_sub_blocks(&data[data_ptr_offset], offset + data_ptr_offset, len);

            data_ptr_offset += remain_len;
            len -= remain_len;
        }

        return true;
    }
    bool write(const uint8_t* data, uint64_t offset, uint64_t len)
    {
        if (!m_sub_block) before_end_of_elaboration();

        uint64_t remain_len = 0;
        uint64_t data_ptr_offset = 0;

        if (offset + len > m_size) {
            return false;
        }

        while (len > 0) {
            SubBlock<>& blk = m_sub_block->access(offset + data_ptr_offset);

            remain_len = blk.write_sub_blocks(&data[data_ptr_offset], offset + data_ptr_offset, len);

            data_ptr_offset += remain_len;
            len -= remain_len;
        }

        return true;
    }

public:
    tlm_utils::multi_passthrough_target_socket<gs_memory<BUSWIDTH>, BUSWIDTH> socket;
    TargetSignalSocket<bool> reset;
    cci::cci_param<bool> p_rom;
    cci::cci_param<bool> p_dmi;
    cci::cci_param<sc_core::sc_time> p_latency;
    cci::cci_param<std::string> p_mapfile;
    cci::cci_param<uint64_t> p_max_block_size;
    cci::cci_param<uint64_t> p_min_block_size;
    cci::cci_param<bool> p_shmem;
    cci::cci_param<std::string> p_shmem_prefix;
    cci::cci_param<bool> p_init_mem;
    cci::cci_param<int> p_init_mem_val; // to match the signature of memset

    gs::loader<> load;

    // NB
    // A size given by a config will always take precedence
    // A size set on the constructor will take precedence over
    // the size given on an e.g. 'add_target' in the router

    gs_memory(sc_core::sc_module_name name, uint64_t _size = 0)
        : m_sub_block(nullptr)
        , m_broker(cci::cci_get_broker())
        , socket("target_socket")
        , reset("reset")
        , p_rom("read_only", false, "Read Only memory (default false)")
        , p_dmi("dmi_allow", true, "DMI allowed (default true)")
        , p_latency("latency", sc_core::sc_time(10, sc_core::SC_NS), "Latency reported for DMI access")
        , p_mapfile("map_file", "", "(optional) file to map this memory")
        , p_max_block_size("max_block_size", 0x100000000, "Maximum size of the sub bloc")
        , p_min_block_size("min_block_size", get_page_size(), "Minimum size of the sub bloc")
        , p_shmem("shared_memory", false, "Allocate using shared memory")
        , p_shmem_prefix("shared_memory_prefix", "", "(optional) prefix_for shared memory file")
        , p_init_mem("init_mem", false, "Initialize allocated memory")
        , p_init_mem_val("init_mem_val", 0, "Value to initialize memory to")
        , load("load", [&](const uint8_t* data, uint64_t offset, uint64_t len) -> void {
            if (!write(data, offset, len)) {
                SCP_WARN(()) << " Offset : 0x" << std::hex << offset << " of the out of range";
            }
        })
    {
        SCP_DEBUG(()) << "Memory constructor";
        MemoryServices::get().init(); // allow any init required
        if (_size) {
            std::string ts_name = std::string(sc_module::name()) + ".target_socket";
            if (!m_broker.has_preset_value(ts_name + ".size")) {
                m_broker.set_preset_cci_value(ts_name + ".size", cci::cci_value(_size));
            }
        }

        socket.register_b_transport(this, &gs_memory::b_transport);
        socket.register_transport_dbg(this, &gs_memory::transport_dbg);
        socket.register_get_direct_mem_ptr(this, &gs_memory::get_direct_mem_ptr);

        reset.register_value_changed_cb([&](bool value) {
            if (value) {
                SCP_WARN(()) << "Reset";
                m_sub_block->doreset();
                load.doreset(value);
            }
        });
    }
    static size_t get_page_size()
    {
        size_t page_size;
#ifdef _WIN32
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        page_size = si.dwPageSize;
#else
        page_size = sysconf(_SC_PAGE_SIZE);
#endif
        return page_size;
    }

    void before_end_of_elaboration()
    {
        if (m_sub_block) {
            return;
        }

        std::string ts_name = std::string(sc_module::name()) + ".target_socket";

        m_address = base();
        m_size = size();

        m_sub_block = std::make_unique<gs_memory<BUSWIDTH>::SubBlock<>>(0, m_size, *this);

        SCP_DEBUG(()) << "m_address: " << m_address;
        SCP_DEBUG(()) << "m_size: " << m_size;

        m_relative_addresses = true;
        if (gs::cci_get<bool>(m_broker, ts_name + ".relative_addresses", m_relative_addresses)) {
            m_broker.lock_preset_value(ts_name + ".relative_addresses");
        }
    }

    gs_memory() = delete;
    gs_memory(const gs_memory&) = delete;

    ~gs_memory() {}

    /**
     * @brief this function returns the size of the memory
     *
     * @return the size of the memory of type uint64_t
     */
    uint64_t size()
    {
        if (!m_size) {
            std::string ts_name = std::string(sc_module::name()) + ".target_socket";
            std::string ts_name_alias;
            if (gs::cci_get<std::string>(m_broker, ts_name + ".0", ts_name_alias)) {
                // deal with an alias
                if (m_broker.has_preset_value(ts_name + ".size") &&
                    m_broker.get_preset_cci_value(ts_name + ".size").is_number()) {
                    SCP_WARN(())
                    ("The configuration alias provided ({}) will be ignored as a valid size is "
                     "also provided.",
                     ts_name);
                } else {
                    ts_name = ts_name_alias;
                    if (ts_name[0] == '&') ts_name = (ts_name.erase(0, 1)) + ".target_socket";
                }
            }
            m_size = gs::cci_get<uint64_t>(m_broker, ts_name + ".size");
        }
        return m_size;
    }

    /**
     * @brief this function returns the base address of the memory
     *
     * @return the address of the memory of type uint64_t
     */
    uint64_t base()
    {
        if (!m_address_valid) {
            std::string ts_name = std::string(sc_module::name()) + ".target_socket";
            std::string ts_name_alias;
            if (gs::cci_get<std::string>(m_broker, ts_name + ".0", ts_name_alias)) {
                // deal with an alias
                if (gs::cci_get<uint64_t>(m_broker, ts_name + ".address", m_address)) {
                    SCP_WARN(())
                    ("The configuration alias provided ({}) will be ignored as a valid address is "
                     "also provided.",
                     ts_name);
                } else {
                    ts_name = ts_name_alias;
                    if (ts_name[0] == '&') ts_name = (ts_name.erase(0, 1)) + ".target_socket";
                }
            }
            if (!m_broker.has_preset_value(ts_name + ".address")) {
                m_address = 0; // fine for relative addressing
                SCP_WARN(()) << "Can't find " << ts_name << ".address";
            } else {
                m_address = gs::cci_get<uint64_t>(m_broker, ts_name + ".address");
            }
            m_broker.lock_preset_value(ts_name + ".address");
            m_address_valid = true;
        }
        return m_address;
    }
};
} // namespace gs

extern "C" void module_register();
#endif
