/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file file-backend-test.cc
 * @brief this is a test for file backend with Pl011 uart
 * read test: read data from the file "read_file" until "EOF"and print it out
 * wtrite test:  write a string to the file "write_file"
 */

#include <systemc.h>

#include "uart-pl011.h"

#include <char_backend_file.h>

#include <tests/initiator-tester.h>
#include <tests/target-tester.h>
#include <tests/test-bench.h>

#include <ports/target-signal-socket.h>

#include <cci_configuration>
#include <scp/report.h>
#include <libgsutils.h>

#include <cci/utils/broker.h>

class TestFILE : public TestBench
{
    Uart m_uart;
    TargetSignalSocket<bool> m_irq_trigger;
    char_backend_file file_backend;

public:
    sc_event ev;
    InitiatorTester m_initiator;
    TestFILE(const sc_core::sc_module_name& n)
        : TestBench(n), m_uart("uart"), file_backend("backend"), m_irq_trigger("irq_trigger"), m_initiator("initiator")
    {
        m_uart.backend_socket.bind(file_backend.socket);
        m_initiator.socket.bind(m_uart.socket);
        m_uart.irq.bind(m_irq_trigger);
        auto cb = std::bind(&TestFILE::fn_cb, this, std::placeholders::_1);
        m_irq_trigger.register_value_changed_cb(cb);
    }
    void fn_cb(const bool& val)
    {
        std::cout << "the val from irq is: " << val << std::endl;
        if (val) {
            ev.notify(sc_core::SC_ZERO_TIME);
        }
    }
};

TEST_BENCH(TestFILE, FileReadWrite)
{
    m_initiator.do_write(14 << 2, 0x10); // #define PL011_INT_RX 0x10 - s->int_enabled
    m_initiator.do_write(11 << 2, 0x10); // pl011_set_read_trigger();
    unsigned char data = 0;
    // read test
    while (data != static_cast<unsigned char>(EOF)) {
        wait(ev);
        ASSERT_EQ(m_initiator.do_read(0x00, data), tlm::TLM_OK_RESPONSE);
        std::cout << "the recived character is: " << data << std::endl;
    }

    // write test
    m_initiator.do_write(0, 'Q');
    m_initiator.do_write(0, 'u');
    m_initiator.do_write(0, 'a');
    m_initiator.do_write(0, 'l');
    m_initiator.do_write(0, 'c');
    m_initiator.do_write(0, 'o');
    m_initiator.do_write(0, 'm');
    m_initiator.do_write(0, 'm');
    m_initiator.do_write(0, '\n');

    sc_core::wait(1, sc_core::SC_NS);
    sc_core::sc_stop();
}

int sc_main(int argc, char* argv[])
{
#ifdef _WIN32
    const char* null_device = "NUL";
#else
    const char* null_device = "/dev/null";
#endif

    gs::ConfigurableBroker m_broker({ { "FileReadWrite.backend.read_file", cci::cci_value(std::string(null_device)) },
                                      { "FileReadWrite.backend.write_file", cci::cci_value(std::string(null_device)) },
                                      { "FileReadWrite.backend.baudrate", cci::cci_value(0) } });

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
