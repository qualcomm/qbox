/*
 * This file is part of libqbox
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2021
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef TESTS_INCLUDE_TEST_CPU_H
#define TESTS_INCLUDE_TEST_CPU_H

#include <cstdint>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <vector>

#include <systemc>
#include <tlm_utils/tlm_quantumkeeper.h>
#include <tlm_utils/simple_target_socket.h>

#include <cci_configuration>
#include <cci/utils/broker.h>

#include <gs_memory.h>
#include <router.h>
#include <global_peripheral_initiator.h>

#include <ports/initiator-signal-socket.h>

#include <scp/report.h>

#include "qemu-instance.h"
#include "test/test.h"
#include "test/tester/tester.h"
#include <tlm_sockets_buswidth.h>

class CpuTestBenchBase : public TestBench, public CpuTesterCallbackIface
{
public:
    static constexpr uint64_t MEM_ADDR = 0x0;
    static constexpr size_t MEM_SIZE = 256 * 1024;

    static constexpr uint64_t BULKMEM_ADDR = 0x100000000;
    static constexpr size_t BULKMEM_SIZE = 1024 * 1024 * 1024;

protected:
    qemu::Target m_arch;

    cci::cci_param<int> p_num_cpu;
    cci::cci_param<int> p_quantum_ns;

    gs::router<> m_router;
    gs::gs_memory<> m_mem;
    gs::gs_memory<> m_bulkmem;

    /*
     * Load a pre-assembled firmware .bin (built at CMake-configure time by
     * llvm-mc + ld.lld + llvm-objcopy) into m_mem at load_addr.
     *
     * Each entry in the patches list overwrites a sizeof(T)-wide word at the
     * tail of the binary — the Nth entry lands at (size - (N+1)*sizeof(T)).
     * Firmware .S files declare matching placeholders at the end of their
     * .data section: .quad for uint64_t (aarch64), .word for uint32_t
     * (RISC-V / Hexagon).
     */
    template <class T = uint64_t>
    void load_firmware_binary(const char* bin_path, uint64_t load_addr, std::initializer_list<T> patches = {})
    {
        std::vector<uint8_t> data = read_firmware_file(bin_path);
        const size_t needed = patches.size() * sizeof(T);
        if (data.size() < needed) {
            SCP_FATAL(SCMOD) << "Firmware " << bin_path << " size " << data.size() << " < " << needed
                             << " bytes needed for patches";
            TEST_FAIL("Firmware too small for patch values");
        }
        size_t idx = 0;
        for (T v : patches) {
            size_t offset = data.size() - (patches.size() - idx) * sizeof(T);
            std::memcpy(data.data() + offset, &v, sizeof(T));
            ++idx;
        }
        m_mem.load.ptr_load(data.data(), load_addr, data.size());
    }

private:
    std::vector<uint8_t> read_firmware_file(const char* bin_path)
    {
        std::ifstream file(bin_path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            SCP_FATAL(SCMOD) << "Failed to open firmware file: " << bin_path;
            TEST_FAIL("Failed to open firmware file");
        }
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> data(size);
        if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
            SCP_FATAL(SCMOD) << "Failed to read firmware file: " << bin_path;
            TEST_FAIL("Failed to read firmware file");
        }
        return data;
    }

public:
    CpuTestBenchBase(const sc_core::sc_module_name& n, qemu::Target arch)
        : TestBench(n)
        , m_arch(arch)
        , p_num_cpu("num_cpu", 1, "Number of CPUs to instantiate in the test")
        , p_quantum_ns("quantum_ns", 10000000, "Value of the global TLM-2.0 quantum in ns")
        // NB it's important the quantum is bigger that the time it takes for QEMU to run all it's
        // realtime updates etc (every 100ms)
        , m_router("router")
        , m_mem("mem", MEM_SIZE)
        , m_bulkmem("bulkmem", BULKMEM_SIZE)
    {
        using tlm_utils::tlm_quantumkeeper;

        m_router.add_target(m_mem.socket, MEM_ADDR, MEM_SIZE);
        m_router.add_target(m_bulkmem.socket, BULKMEM_ADDR, BULKMEM_SIZE);

        sc_core::sc_time global_quantum(p_quantum_ns, sc_core::SC_NS);
        tlm_quantumkeeper::set_global_quantum(global_quantum);
    }

    void map_target(tlm::tlm_target_socket<DEFAULT_TLM_BUSWIDTH>& s, uint64_t addr, uint64_t size)
    {
        m_router.add_target(s, addr, size);
    }

    virtual uint64_t mmio_read(int id, uint64_t addr, size_t len) { TEST_FAIL("Unexpected CPU read"); }

    virtual void mmio_write(int id, uint64_t addr, uint64_t data, size_t len) { TEST_FAIL("Unexpected CPU write"); }

    virtual bool dmi_request(int id, uint64_t addr, size_t len, tlm::tlm_dmi& ret)
    {
        TEST_FAIL("Unexpected DMI request");
        return false;
    }
};

template <class CPU, class TESTER>
class CpuTestBench : public CpuTestBenchBase
{
protected:
    QemuInstanceManager m_inst_manager;
    QemuInstance m_inst_a;
    QemuInstance m_inst_b;

    bool ab = false;
    sc_core::sc_vector<CPU> m_cpus;
    TESTER m_tester;

    global_peripheral_initiator m_gpi; // this is required for KVM/HVF that read from the GPI.
                                       // We only need 1, because only inst_a will be accelerated.

public:
    CpuTestBench(const sc_core::sc_module_name& n)
        : CpuTestBenchBase(n, CPU::ARCH)
        , m_inst_a("inst_a", &m_inst_manager, CPU::ARCH)
        , m_inst_b("inst_b", &m_inst_manager, CPU::ARCH)
        , m_cpus("cpu", p_num_cpu,
                 [this](const char* n, int i) {
                     ab = !ab;
                     return new CPU(n, ab ? m_inst_a : m_inst_b);
                 })
        , m_tester("tester", *this)
        , m_gpi("gpi", m_inst_a, m_cpus[0])
    {
        int i = 0;
        for (CPU& cpu : m_cpus) {
            m_router.add_initiator(cpu.socket);
        }
        m_router.add_initiator(m_gpi.m_initiator);
    }

    virtual void map_irqs_to_cpus(sc_core::sc_vector<InitiatorSignalSocket<bool>>& irqs) { abort(); }

    void map_halt_to_cpus(sc_core::sc_vector<sc_core::sc_out<bool>>& halt)
    {
        int i = 0;

        for (auto& cpu : m_cpus) {
            halt[i++].bind(cpu.halt);
        }
    }

    void map_reset_to_cpus(sc_core::sc_vector<sc_core::sc_out<bool>>& reset)
    {
        int i = 0;

        for (auto& cpu : m_cpus) {
            reset[i++].bind(cpu.reset);
        }
    }
};

template <class CPU, class TESTER>
class CpuArmTestBench : public CpuTestBench<CPU, TESTER>
{
public:
    CpuArmTestBench(const sc_core::sc_module_name& n): CpuTestBench<CPU, TESTER>(n)
    {
        int i = 0;
        for (CPU& cpu : CpuTestBench<CPU, TESTER>::m_cpus) {
            cpu.p_mp_affinity = i++;
            cpu.p_has_el3 = false;
            cpu.p_has_el2 = false; // Un-needed and unavailable for HVF/KVM
        }
    }

    virtual void map_irqs_to_cpus(sc_core::sc_vector<InitiatorSignalSocket<bool>>& irqs) override
    {
        int i = 0;
        for (CPU& cpu : CpuTestBench<CPU, TESTER>::m_cpus) {
            irqs[i++].bind(cpu.irq_in);
        }
    }
};
#endif
