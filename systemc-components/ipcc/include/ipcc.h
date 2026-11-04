/*
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <ports/initiator-signal-socket.h>
#include <ports/target-signal-socket.h>
#include <systemc>
#include <cci_configuration>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <module_factory_registery.h>

#include <scp/report.h>
#include <tlm_sockets_buswidth.h>

#include <string>

/* registers as extracted

IPC_PROTOCOLp_CLIENTc_VERSION, 0x400000, Read, Yes, 0x10200
IPC_PROTOCOLp_CLIENTc_ID, 0x400004, Read, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_CONFIG, 0x400008, Read/Write, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_SEND, 0x40000C, Write, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_RECV_ID, 0x400010, Read, Yes, 0xFFFFFFFF
IPC_PROTOCOLp_CLIENTc_RECV_SIGNAL_ENABLE, 0x400014, Write, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_RECV_SIGNAL_DISABLE, 0x400018, Write, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_RECV_SIGNAL_CLEAR, 0x40001C, Write, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_GLOBAL_SIGNAL_STATUS_0, 0x400020, Read, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_GLOBAL_SIGNAL_STATUS_1, 0x400024, Read, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_GLOBAL_SIGNAL_CLEAR_0, 0x400028, Write, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_GLOBAL_SIGNAL_CLEAR_1, 0x40002C, Write, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_GLOBAL_SIGNAL_MASK_0, 0x400030, Read/Write, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_GLOBAL_SIGNAL_MASK_1, 0x400034, Read/Write, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_CLIENT_CLEAR, 0x400038, Write, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_GLOBAL_QUAL_SIGNAL_STATUS_0, 0x40003C, Read, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_GLOBAL_QUAL_SIGNAL_STATUS_1, 0x400040, Read, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_SEND16, 0x400044, Write, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_RECV_CLIENT_PRIORITY_n, 0x400100, Read/Write, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_RECV_CLIENT_ENABLE_0_n, 0x400200, Write, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_RECV_CLIENT_ENABLE_1_n, 0x400300, Write, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_RECV_CLIENT_DISABLE_0_n, 0x400400, Write, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_RECV_CLIENT_DISABLE_1_n, 0x400500, Write, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_CLIENT_SIGNAL_STATUS_0_n, 0x400600, Read, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_CLIENT_SIGNAL_STATUS_1_n, 0x400700, Read, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_CLIENT_ENABLE_STATUS_0_n, 0x400800, Read, Yes, 0x0
IPC_PROTOCOLp_CLIENTc_CLIENT_ENABLE_STATUS_1_n, 0x400900, Read, Yes, 0x0

*/

/**
 * @class ipcc
 *
 * @brief An IPCC implementation
 *
 */

class ipcc : public sc_core::sc_module
{
    SCP_LOGGER();

private:
    enum IPCC_Regs {
        VERSION,
        ID,
        CONFIG,
        SEND,
        RECV_ID,
        RECV_SIGNAL_ENABLE,
        RECV_SIGNAL_DISABLE,
        RECV_SIGNAL_CLEAR,
        GLOBAL_SIGNAL_STATUS_0,
        GLOBAL_SIGNAL_STATUS_1,
        GLOBAL_SIGNAL_CLEAR_0,
        GLOBAL_SIGNAL_CLEAR_1,
        GLOBAL_SIGNAL_MASK_0,
        GLOBAL_SIGNAL_MASK_1,
        CLIENT_CLEAR,
        GLOBAL_QUAL_SIGNAL_STATUS_0,
        GLOBAL_QUAL_SIGNAL_STATUS_1,
        SEND16,
        RECV_ID_PHYS = 0x48 / 4,
        RECV_CLIENT_PRIORITY_n = 0x100 / 4,
        RECV_CLIENT_ENABLE_0_n = 0x200 / 4,
        RECV_CLIENT_ENABLE_1_n = 0x300 / 4,
        RECV_CLIENT_DISABLE_0_n = 0x400 / 4,
        RECV_CLIENT_DISABLE_1_n = 0x500 / 4,
        // I think these means "Is a signal coming from client N active on this client"
        CLIENT_SIGNAL_STATUS_0_n = 0x600 / 4,
        CLIENT_SIGNAL_STATUS_1_n = 0x700 / 4,
        CLIENT_ENABLE_STATUS_0_n = 0x800 / 4,
        CLIENT_ENABLE_STATUS_1_n = 0x900 / 4,
        MAX_CLIENT_SIZE = 64
    };
    enum Protocol { MPROC = 0, COMPUTE_L0 = 1, COMPUTE_L1 = 2, PCIE_MSI = 3 };
    static constexpr unsigned int NUM_PROTOCOLS = 4;
    static_assert(NUM_PROTOCOLS <= 4, "IPCC supports at most four protocols");

    static constexpr int protocol_physical_client_count(unsigned int protocol)
    {
        return protocol == PCIE_MSI ? 60 : MAX_CLIENT_SIZE;
    }

    class IPC_client
    {
    public:
        uint32_t regs[0x1000 / 4];
        // These could be parameters, but then each IPC_client needs a unique name and would need to
        // be constructed during elaboration
        uint32_t p_client_size;
        uint32_t p_version;
        bool status(int client, int signal)
        {
            assert(client < p_client_size);
            assert(signal < p_client_size);
            if (signal < 32)
                return (regs[CLIENT_SIGNAL_STATUS_0_n + client] & (1 << signal)) != 0;
            else
                return (regs[CLIENT_SIGNAL_STATUS_1_n + client] & (1 << (signal - 32))) != 0;
        }
        bool enable(int client, int signal)
        {
            assert(client < p_client_size);
            assert(signal < p_client_size);
            if (signal < 32)
                return (regs[CLIENT_ENABLE_STATUS_0_n + client] & (1 << signal)) != 0;
            else
                return (regs[CLIENT_ENABLE_STATUS_1_n + client] & (1 << (signal - 32))) != 0;
        }

        void set_status(int client, int signal)
        {
            assert(client < p_client_size);
            assert(signal < p_client_size);
            if (signal < 32)
                regs[CLIENT_SIGNAL_STATUS_0_n + client] |= (1 << signal);
            else
                regs[CLIENT_SIGNAL_STATUS_1_n + client] |= (1 << (signal - 32));
        }

        void clear_status(int client, int signal)
        {
            assert(client < p_client_size);
            assert(signal < p_client_size);
            if (signal < 32)
                regs[CLIENT_SIGNAL_STATUS_0_n + client] &= ~(1 << signal);
            else
                regs[CLIENT_SIGNAL_STATUS_1_n + client] &= ~(1 << (signal - 32));
        }

        void set_enable(int client, int signal)
        {
            assert(client < p_client_size);
            assert(signal < p_client_size);
            if (signal < 32)
                regs[CLIENT_ENABLE_STATUS_0_n + client] |= (1 << signal);
            else
                regs[CLIENT_ENABLE_STATUS_1_n + client] |= (1 << (signal - 32));
        }

        void clear_enable(int client, int signal)
        {
            assert(client < p_client_size);
            assert(signal < p_client_size);
            if (signal < 32)
                regs[CLIENT_ENABLE_STATUS_0_n + client] &= ~(1 << signal);
            else
                regs[CLIENT_ENABLE_STATUS_1_n + client] &= ~(1 << (signal - 32));
        }

        void reset()
        {
            memset(regs, 0, sizeof(regs));
            regs[VERSION] = p_version;
            regs[RECV_ID] = 0xFFFFFFFF;
        }
        IPC_client(): p_client_size(MAX_CLIENT_SIZE), p_version(0x10200) { reset(); };
    };
    IPC_client client[NUM_PROTOCOLS][MAX_CLIENT_SIZE];
    sc_core::sc_event update_irq_ev;

    int logical_of_physical[MAX_CLIENT_SIZE];
    int physical_of_logical[MAX_CLIENT_SIZE];

    int physical_client(unsigned int protocol, int logical_client) const
    {
        for (int client = 0; client < protocol_physical_client_count(protocol); client++) {
            if (logical_of_physical[client] == logical_client) return client;
        }
        return -1;
    }

protected:
    cci::cci_param<unsigned int> p_num_irq;
    cci::cci_param<unsigned int> p_hw_version;
    cci::cci_param<std::string> p_logical_ids;

    template <class Port>
    static void safe_write(Port& p, bool v)
    {
        if (p.size() > 0) p->write(v);
    }

    uint32_t read(IPC_client& src, uint64_t addr)
    {
        uint32_t ret = src.regs[addr / 4];
        // Side effects
        switch (addr / 4) {
        case RECV_ID:
        case RECV_ID_PHYS: {
            uint32_t logical_id = src.regs[RECV_ID];
            if ((src.regs[CONFIG] & 0x1) && (logical_id != 0xffffffff)) {
                src.clear_status(logical_id >> 16, logical_id & 0xffff);
                src.regs[RECV_ID] = 0xffffffff;
                src.regs[RECV_ID_PHYS] = 0xffffffff;
            }
            break;
        }
        }

        return ret;
    }
    void write(IPC_client& src, uint64_t addr, uint32_t data)
    {
        src.regs[addr / 4] = data;
        // side effects
        switch (addr / 4) {
        case SEND: {
            int s = data & 0xffff;
            int src_protocol = src.regs[ID] & 0x3f;
            int src_logical = (src.regs[ID] >> 16) & 0x3f;
            if (data & 0x80000000) {
                SCP_INFO(()) << "IPCC_SEND broadcast src_logical=" << std::dec << src_logical
                             << " protocol=" << src_protocol << " signal=" << s << " raw=0x" << std::hex << data
                             << std::dec;
                for (int c = 0; c < src.p_client_size; c++) {
                    send(src, c, s);
                }
            } else {
                int target_logical = (data >> 16) & 0xffff;
                int c = physical_client(src_protocol, target_logical & 0x3f);
                SCP_INFO(()) << "IPCC_SEND src_logical=" << std::dec << src_logical << " protocol=" << src_protocol
                             << " target_logical=" << target_logical << " target_phys=" << c << " signal=" << s
                             << " raw=0x" << std::hex << data << std::dec;
                if ((src_logical == 6 || src_logical == 18) && target_logical == 8 && s == 2) {
                    SCP_WARN(()) << "IPCC_CDSP_READY_KICK src_logical=" << std::dec << src_logical
                                 << " target_logical=" << target_logical << " signal=" << s << " raw=0x" << std::hex
                                 << data << std::dec;
                }
                send(src, c, s);
            }
            break;
        }
        case RECV_SIGNAL_ENABLE: {
            int c = (data >> 16) & 0xffff; // Source client
            int s = (data & 0xffff);       // Signal
            int p = src.regs[ID] & 0x3f;
            int dst_c = (src.regs[ID] >> 16) & 0x3f;
            SCP_DEBUG(()) << "IPCC_LOG ENABLE: DstClient " << dst_c << " (Proto " << p << ") enabled Signal " << s
                          << " from SrcClient " << c;
            SCP_INFO(()) << "IPCC_RECV_ENABLE dst_logical=" << std::dec << dst_c << " src_logical=" << c
                         << " signal=" << s << " raw=0x" << std::hex << data << std::dec;
            src.set_enable(c, s);
            break;
        }
        case RECV_SIGNAL_DISABLE: {
            int c = (data >> 16) & 0xffff;
            int s = (data & 0xffff);
            src.clear_enable(c, s);
            break;
        }
        case RECV_SIGNAL_CLEAR: {
            int c = (data >> 16) & 0xffff;
            int s = (data & 0xffff);
            int dst_c = (src.regs[ID] >> 16) & 0x3f;
            SCP_INFO(()) << "IPCC_RECV_CLEAR dst_logical=" << std::dec << dst_c << " src_logical=" << c
                         << " signal=" << s << " raw=0x" << std::hex << data << std::dec;
            src.clear_status(c, s);
            break;
        }
        case CLIENT_CLEAR: {
            uint32_t id = src.regs[ID];
            if (data & 0x1) {
                memset(src.regs, 0, sizeof(src.regs));
                src.regs[VERSION] = p_hw_version.get_value();
                src.regs[RECV_ID] = 0xFFFFFFFF;
                src.regs[ID] = id;
            }
            break;
        }
        }
    }
    void send(IPC_client& src, int c, int s)
    {
        int p = src.regs[ID] & 0x3f;
        int sc = (src.regs[ID] >> 16) & 0x3f;
        if (p >= NUM_PROTOCOLS) {
            SCP_WARN(()) << "Ignoring send from unsupported protocol " << p;
            return;
        }
        if (c < 0 || c >= protocol_physical_client_count(p)) {
            SCP_WARN(()) << "Ignoring send to unsupported client " << c << " on protocol " << p;
            return;
        }
        IPC_client& dst = client[p][c];
        SCP_DEBUG(()) << "IPCC_LOG STATUS SET: DstClient " << c << " (Proto " << p << ") received Signal " << s
                      << " from SrcClient " << sc;
        dst.set_status(sc, s);
    }
    void update_irq()
    {
        int client_size = client[0][0].p_client_size;
        for (int c = 0; c < client_size; c++) {
            int allirqcount = 0;
            for (unsigned int p = 0; p < NUM_PROTOCOLS; p++) {
                if (c >= protocol_physical_client_count(p)) continue;
                int irqcount = 0;
                int prio = 0;
                int sig = 0;
                int cli = 0;

                for (int sc = 0; sc < client_size; sc++) {
                    for (int s = 0; s < client_size; s++) {
                        if (client[p][c].status(sc, s)) {
                            // there is an active signal s from p/sc to p/c
                            if ((client[p][c].regs[CONFIG] & 0x80000000) == 0) {
                                if (client[p][c].enable(sc, s)) {
                                    int pr = client[p][c].regs[RECV_CLIENT_PRIORITY_n + sc];
                                    if (irqcount == 0 || pr > prio) {
                                        prio = pr;
                                        sig = s;
                                        cli = sc;
                                    }
                                    irqcount++;
                                }
                            }
                        }
                    }
                }
                allirqcount += irqcount;
                if (irqcount) {
                    int phys_cli = (cli < MAX_CLIENT_SIZE && physical_of_logical[cli] >= 0) ? physical_of_logical[cli]
                                                                                            : cli;
                    client[p][c].regs[RECV_ID] = (cli << 16) + sig;
                    client[p][c].regs[RECV_ID_PHYS] = (phys_cli << 16) + sig;
                } else {
                    client[p][c].regs[RECV_ID] = 0xffffffff;
                    client[p][c].regs[RECV_ID_PHYS] = 0xffffffff;
                }
                switch (p) {
                case MPROC:
                    safe_write(irq_mproc[c], irqcount != 0);
                    break;
                case COMPUTE_L0:
                    safe_write(irq_compute_l0[c], irqcount != 0);
                    break;
                case COMPUTE_L1:
                    safe_write(irq_compute_l1[c], irqcount != 0);
                    break;
                case PCIE_MSI:
                    safe_write(irq_pcie_msi[c], irqcount != 0);
                    break;
                default:
                    SCP_FATAL(()) << "Unsupported IPCC protocol " << p;
                }
            }
            if (allirqcount >= 1) {
                if (!irq_status[c]) {
                    SCP_INFO(()) << "SEND IRQ " << c;
                    irq_status[c] = true;
                    safe_write(irq[c], true);
                }
            } else {
                if (irq_status[c]) {
                    SCP_INFO(()) << "CLEAR IRQ " << c;
                    irq_status[c] = false;
                    safe_write(irq[c], false);
                }
            }
        }
    }
    void b_transport(tlm::tlm_generic_payload& txn, sc_core::sc_time& delay)
    {
        unsigned int len = txn.get_data_length();
        unsigned char* ptr = txn.get_data_ptr();
        sc_dt::uint64 addr = txn.get_address();
        if (len > sizeof(client)) {
            txn.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        assert(len == 4);

        int c = (addr / 0x1000) & 0x3f;
        uint64_t p = addr / 0x40000;
        if (p >= NUM_PROTOCOLS || addr >= 0x100000ULL || (p == PCIE_MSI && c >= 60)) {
            SCP_WARN(()) << "Ignoring access to unsupported protocol " << p;
            txn.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        switch (txn.get_command()) {
        case tlm::TLM_READ_COMMAND: {
            uint32_t data = read(client[p][c], addr & 0xfff);
            memcpy(ptr, &data, len);
            SCP_INFO(()) << "b_transport read " << std::hex << addr << " " << data;
            break;
        }
        case tlm::TLM_WRITE_COMMAND: {
            uint32_t data;
            memcpy(&data, ptr, len);
            SCP_INFO(()) << "b_transport write " << std::hex << addr << " " << data;
            write(client[p][c], addr & 0xfff, data);
            break;
        }
        default:
            SCP_ERR(()) << "TLM command not supported";
            break;
        }

        txn.set_response_status(tlm::TLM_OK_RESPONSE);

        update_irq_ev.notify(sc_core::SC_ZERO_TIME);
    }

public:
    tlm_utils::simple_target_socket<ipcc, DEFAULT_TLM_BUSWIDTH> socket;
    // InitiatorSignalSocket<bool> irq[MAX_CLIENT_SIZE];
    sc_core::sc_vector<InitiatorSignalSocket<bool>> irq;
    bool irq_status[MAX_CLIENT_SIZE] = { false };
    sc_core::sc_vector<InitiatorSignalSocket<bool>> irq_mproc;
    sc_core::sc_vector<InitiatorSignalSocket<bool>> irq_compute_l0;
    sc_core::sc_vector<InitiatorSignalSocket<bool>> irq_compute_l1;
    sc_core::sc_vector<InitiatorSignalSocket<bool>> irq_pcie_msi;
    TargetSignalSocket<bool> reset;

    ipcc(sc_core::sc_module_name name)
        : p_num_irq("num_irq", MAX_CLIENT_SIZE, "Number of irq")
        , p_hw_version("hw_version", 0x10200, "IPCC HW VERSION register value")
        , p_logical_ids("logical_ids", "", "Comma-separated logical IDs per physical slot (empty=identity)")
        , socket("target_socket")
        , irq("irq", p_num_irq)
        , irq_mproc("irq_mproc", p_num_irq)
        , irq_compute_l0("irq_compute_l0", p_num_irq)
        , irq_compute_l1("irq_compute_l1", p_num_irq)
        , irq_pcie_msi("irq_pcie_msi", p_num_irq)
        , reset("reset")
    {
        SCP_TRACE(())("Init");
        for (int i = 0; i < MAX_CLIENT_SIZE; i++) {
            logical_of_physical[i] = i;
            physical_of_logical[i] = -1;
        }
        {
            std::string s = p_logical_ids.get_value();
            int i = 0;
            while (!s.empty() && i < MAX_CLIENT_SIZE) {
                size_t pos = s.find(',');
                int lid = std::stoi(pos == std::string::npos ? s : s.substr(0, pos));
                if (lid >= 0 && lid < MAX_CLIENT_SIZE) {
                    logical_of_physical[i] = lid;
                }
                if (pos == std::string::npos) break;
                s = s.substr(pos + 1);
                i++;
            }
        }
        for (int i = 0; i < MAX_CLIENT_SIZE; i++) {
            int lid = logical_of_physical[i];
            if (lid >= 0 && lid < MAX_CLIENT_SIZE && physical_of_logical[lid] < 0) {
                physical_of_logical[lid] = i;
            }
        }

        int client_size = client[0][0].p_client_size;
        uint32_t hw_ver = p_hw_version.get_value();
        for (unsigned int p = 0; p < NUM_PROTOCOLS; p++) {
            for (int c = 0; c < protocol_physical_client_count(p); c++) {
                client[p][c].p_version = hw_ver;
                client[p][c].regs[VERSION] = hw_ver;
                client[p][c].regs[ID] = (logical_of_physical[c] << 16) + p;
            }
        }
        socket.register_b_transport(this, &ipcc::b_transport);

        SC_METHOD(update_irq);
        sensitive << update_irq_ev;

        reset.register_value_changed_cb([&](bool value) {
            if (value) {
                SCP_WARN(())("Reset");
                int client_size = client[0][0].p_client_size;
                for (unsigned int p = 0; p < NUM_PROTOCOLS; p++) {
                    for (int c = 0; c < protocol_physical_client_count(p); c++) {
                        client[p][c].reset();
                        client[p][c].regs[ID] = (logical_of_physical[c] << 16) + p;
                    }
                }
                update_irq();
            }
        });
    }

    ipcc() = delete;
    ipcc(const ipcc&) = delete;

    ~ipcc() {}
};

extern "C" void module_register();
