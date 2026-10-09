/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "container_builder-bench.h"
#include <module_factory_registery.h>
#include <tests/initiator-tester.h>
#include <tlm_utils/simple_initiator_socket.h>

class manually_created_module : public sc_core::sc_module
{
public:
    manually_created_module(sc_core::sc_module_name n): sc_core::sc_module(n) {}
};

class reuse_platform : public ContainerDeferModulesConstruct
{
public:
    manually_created_module existing;
    ContainerDeferModulesConstruct existing_container;

    reuse_platform(sc_core::sc_module_name n)
        : ContainerDeferModulesConstruct(n), existing("existing"), existing_container("existing_container")
    {
        ModulesConstruct();
    }
};

class router_order_probe : public sc_core::sc_module
{
public:
    tlm_utils::simple_initiator_socket<router_order_probe> initiator_socket;
    bool target_was_constructed;

    router_order_probe(const sc_core::sc_module_name& n)
        : sc_core::sc_module(n), initiator_socket("initiator_socket"), target_was_constructed(false)
    {
        const std::string probe_name = name();
        const size_t parent_end = probe_name.rfind('.');
        const std::string parent = probe_name.substr(0, parent_end + 1);
        const std::string probe_prefix = "a_router_order_probe_";
        const std::string probe_leaf = probe_name.substr(parent_end + 1);
        const std::string target_name = parent + "z_order_memory_" + probe_leaf.substr(probe_prefix.size());
        target_was_constructed = sc_core::sc_find_object(target_name.c_str()) != nullptr;
    }
};

GSC_MODULE_REGISTER(router_order_probe);

class nested_router_order_probe : public sc_core::sc_module
{
public:
    bool target_was_constructed;

    nested_router_order_probe(const sc_core::sc_module_name& n)
        : sc_core::sc_module(n)
        , target_was_constructed(sc_core::sc_find_object("AllTests.platform.z_nested_router_order_memory") != nullptr)
    {
    }
};

GSC_MODULE_REGISTER(nested_router_order_probe);

class socket_redirect_cycle_detector : public gs::container_builder
{
public:
    using gs::container_builder::get_socket_redirect_cycle;
};

TEST(container_builder, detects_socket_redirect_cycles)
{
    EXPECT_EQ(socket_redirect_cycle_detector::get_socket_redirect_cycle(
                  { { "first_hop", "second_hop" }, { "second_hop", "first_hop" } }),
              "first_hop -> second_hop -> first_hop");
    EXPECT_EQ(socket_redirect_cycle_detector::get_socket_redirect_cycle(
                  { { "first_hop", "second_hop" }, { "second_hop", "router.target_socket" } }),
              "");
}
ContainerBuilderTestBench::ContainerBuilderTestBench(const sc_core::sc_module_name& n): TestBench(n)
{
    m_reuse_platform = std::make_unique<reuse_platform>("reuse_platform");
    m_platform = std::make_unique<Container>("platform");
}

TEST_BENCH(ContainerBuilderTestBench, AllTests)
{
    ASSERT_TRUE(m_platform != nullptr) << "Failed to create platform";
    EXPECT_FALSE(m_reuse_platform->is_container_type("existing"));
    EXPECT_TRUE(m_reuse_platform->is_container_type("existing_container"));
    EXPECT_FALSE(m_reuse_platform->m_broker.get_param_handle("__GS.ModuleFactory.UnregisteredModule").is_valid());
    EXPECT_FALSE(m_reuse_platform->m_broker.get_param_handle("__GS.ModuleFactory.UnregisteredContainer").is_valid());

    InitiatorTester* initiator = dynamic_cast<InitiatorTester*>(sc_core::sc_find_object("AllTests.platform.initiator"));
    ASSERT_TRUE(initiator != nullptr) << "Failed to find main initiator";

    InitiatorTester* initiator1 = dynamic_cast<InitiatorTester*>(
        sc_core::sc_find_object("AllTests.platform.container1.initiator1"));
    ASSERT_TRUE(initiator1 != nullptr) << "Failed to find container1 initiator1";

    InitiatorTester* initiator2 = dynamic_cast<InitiatorTester*>(
        sc_core::sc_find_object("AllTests.platform.container2.initiator2"));
    ASSERT_TRUE(initiator2 != nullptr) << "Failed to find container2 initiator2";

    InitiatorTester* initiator_nested = dynamic_cast<InitiatorTester*>(
        sc_core::sc_find_object("AllTests.platform.container1.nested_container.initiator_nested"));
    ASSERT_TRUE(initiator_nested != nullptr) << "Failed to find nested initiator";

    InitiatorTester* chain_initiator = dynamic_cast<InitiatorTester*>(
        sc_core::sc_find_object("AllTests.platform.chain_initiator"));
    ASSERT_TRUE(chain_initiator != nullptr) << "Failed to find chained-alias initiator";

    InitiatorTester* fan_in_initiator = dynamic_cast<InitiatorTester*>(
        sc_core::sc_find_object("AllTests.platform.fan_in_initiator"));
    ASSERT_TRUE(fan_in_initiator != nullptr) << "Failed to find fan-in-alias initiator";

    InitiatorTester* reverse_chain_initiator = dynamic_cast<InitiatorTester*>(
        sc_core::sc_find_object("AllTests.platform.reverse_chain_initiator"));
    ASSERT_TRUE(reverse_chain_initiator != nullptr) << "Failed to find reverse chained-alias initiator";

    uint32_t write_data, read_data;
    tlm::tlm_response_status status;

    for (const auto& source : { "first", "second", "third" }) {
        const std::string source_path = std::string("AllTests.platform.alias_binding.") + source;
        auto* alias_initiator = dynamic_cast<InitiatorTester*>(sc_core::sc_find_object(source_path.c_str()));
        ASSERT_NE(alias_initiator, nullptr);
        EXPECT_EQ(gs::cci_get<std::string>(m_platform->m_broker, source_path + ".initiator_socket.bind"),
                  "&AllTests.platform.alias_binding.memory.target_socket");
        write_data = 0x10203040;
        ASSERT_EQ(alias_initiator->do_write<uint32_t>(0, write_data), tlm::TLM_OK_RESPONSE);
        read_data = 0;
        ASSERT_EQ(alias_initiator->do_read<uint32_t>(0, read_data), tlm::TLM_OK_RESPONSE);
        EXPECT_EQ(read_data, write_data);
    }

    write_data = 0xDEADBEEF;
    status = initiator->do_write<uint32_t>(0x10000000, write_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Write to memory1 failed";

    read_data = 0;
    status = initiator->do_read<uint32_t>(0x10000000, read_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Read from memory1 failed";
    ASSERT_EQ(read_data, write_data) << "Memory1 read/write mismatch";

    write_data = 0xCAFEBABE;
    status = initiator->do_write<uint32_t>(0x20000000, write_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Write to memory2 failed";

    read_data = 0;
    status = initiator->do_read<uint32_t>(0x20000000, read_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Read from memory2 failed";
    ASSERT_EQ(read_data, write_data) << "Memory2 read/write mismatch";

    write_data = 0x12345678;
    status = initiator->do_write<uint32_t>(0x30000000, write_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Write to memory3 failed";

    read_data = 0;
    status = initiator->do_read<uint32_t>(0x30000000, read_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Read from memory3 failed";
    ASSERT_EQ(read_data, write_data) << "Memory3 read/write mismatch";

    write_data = 0xAABBCCDD;
    status = initiator1->do_write<uint32_t>(0x40000000, write_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Write to container1.memory_a failed";

    read_data = 0;
    status = initiator1->do_read<uint32_t>(0x40000000, read_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Read from container1.memory_a failed";
    ASSERT_EQ(read_data, write_data) << "Container1.memory_a read/write mismatch";

    write_data = 0x11223344;
    status = initiator1->do_write<uint32_t>(0x50000000, write_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Write to container1.memory_b failed";

    read_data = 0;
    status = initiator1->do_read<uint32_t>(0x50000000, read_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Read from container1.memory_b failed";
    ASSERT_EQ(read_data, write_data) << "Container1.memory_b read/write mismatch";

    write_data = 0x55667788;
    status = initiator_nested->do_write<uint32_t>(0x60000000, write_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Write to nested_container.memory_nested failed";

    read_data = 0;
    status = initiator_nested->do_read<uint32_t>(0x60000000, read_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Read from nested_container.memory_nested failed";
    ASSERT_EQ(read_data, write_data) << "Nested_container.memory_nested read/write mismatch";

    write_data = 0x99AABBCC;
    status = initiator2->do_write<uint32_t>(0x70000000, write_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Write to container2.memory_x failed";

    read_data = 0;
    status = initiator2->do_read<uint32_t>(0x70000000, read_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Read from container2.memory_x failed";
    ASSERT_EQ(read_data, write_data) << "Container2.memory_x read/write mismatch";

    write_data = 0xDDEEFF00;
    status = initiator2->do_write<uint32_t>(0x80000000, write_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Write to container2.memory_y failed";

    read_data = 0;
    status = initiator2->do_read<uint32_t>(0x80000000, read_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Read from container2.memory_y failed";
    ASSERT_EQ(read_data, write_data) << "Container2.memory_y read/write mismatch";

    write_data = 0xABCD0123;
    status = initiator->do_write<uint32_t>(0x10000004, write_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Second write to memory1 failed";

    read_data = 0;
    status = initiator->do_read<uint32_t>(0x10000004, read_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Second read from memory1 failed";
    ASSERT_EQ(read_data, write_data) << "Memory1 second read/write mismatch";

    write_data = 0x13579BDF;
    status = chain_initiator->do_write<uint32_t>(0x90000000, write_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Write through chained alias failed";

    read_data = 0;
    status = chain_initiator->do_read<uint32_t>(0x90000000, read_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Read through chained alias failed";
    ASSERT_EQ(read_data, write_data) << "Chained alias read/write mismatch";

    write_data = 0x2468ACE0;
    status = fan_in_initiator->do_write<uint32_t>(0xB0000000, write_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Write through fan-in alias failed";

    read_data = 0;
    status = fan_in_initiator->do_read<uint32_t>(0xB0000000, read_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Read through fan-in alias failed";
    ASSERT_EQ(read_data, write_data) << "Fan-in alias read/write mismatch";

    write_data = 0x11223344;
    status = reverse_chain_initiator->do_write<uint32_t>(0xD0000000, write_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Write through reverse chained alias failed";

    read_data = 0;
    status = reverse_chain_initiator->do_read<uint32_t>(0xD0000000, read_data);
    ASSERT_EQ(status, tlm::TLM_OK_RESPONSE) << "Read through reverse chained alias failed";
    ASSERT_EQ(read_data, write_data) << "Reverse chained alias read/write mismatch";

    sc_core::sc_object* container1_obj = sc_core::sc_find_object("AllTests.platform.container1");
    ASSERT_TRUE(container1_obj != nullptr) << "container1 not found";

    sc_core::sc_object* container2_obj = sc_core::sc_find_object("AllTests.platform.container2");
    ASSERT_TRUE(container2_obj != nullptr) << "container2 not found";

    sc_core::sc_object* router1 = sc_core::sc_find_object("AllTests.platform.container1.router");
    ASSERT_TRUE(router1 != nullptr) << "container1.router not created from config - .config parameter not working!";

    sc_core::sc_object* router2 = sc_core::sc_find_object("AllTests.platform.container2.router");
    ASSERT_TRUE(router2 != nullptr) << "container2.router not created from config - .config parameter not working!";

    sc_core::sc_object* memory_a = sc_core::sc_find_object("AllTests.platform.container1.memory_a");
    ASSERT_TRUE(memory_a != nullptr) << "container1.memory_a not created from config";

    sc_core::sc_object* memory_x = sc_core::sc_find_object("AllTests.platform.container2.memory_x");
    ASSERT_TRUE(memory_x != nullptr) << "container2.memory_x not created from config";
    sc_core::sc_object* memory_z = sc_core::sc_find_object("AllTests.platform.container3.memory_z");
    ASSERT_TRUE(memory_z != nullptr) << "container3.memory_z not created from chained alias config";

    const std::string chain_address = "AllTests.platform.container3.router.target_socket.address";
    const std::string chain_size = "AllTests.platform.container3.router.target_socket.size";
    const std::string chain_bind = "AllTests.platform.chain_initiator.initiator_socket.bind";
    ASSERT_TRUE(m_platform->m_broker.has_preset_value(chain_address)) << "chained alias address was not forwarded";
    ASSERT_TRUE(m_platform->m_broker.has_preset_value(chain_size)) << "chained alias size was not forwarded";
    ASSERT_EQ(gs::cci_get<uint64_t>(m_platform->m_broker, chain_address), 0x90000000);
    ASSERT_EQ(gs::cci_get<uint64_t>(m_platform->m_broker, chain_size), 0x1000);
    ASSERT_EQ(gs::cci_get<std::string>(m_platform->m_broker, chain_bind),
              "&AllTests.platform.container3.router.target_socket");

    const std::string fan_in_address = "AllTests.platform.container4.router.target_socket.address";
    const std::string fan_in_size = "AllTests.platform.container4.router.target_socket.size";
    ASSERT_EQ(gs::cci_get<uint64_t>(m_platform->m_broker, fan_in_address), 0xB0000000);
    ASSERT_EQ(gs::cci_get<uint64_t>(m_platform->m_broker, fan_in_size), 0x2000);
    ASSERT_EQ(
        gs::cci_get<std::string>(m_platform->m_broker, "AllTests.platform.fan_in_initiator.initiator_socket.bind"),
        "&AllTests.platform.container4.router.target_socket");
    ASSERT_EQ(gs::cci_get<std::string>(m_platform->m_broker,
                                       "AllTests.platform.reverse_chain_initiator.initiator_socket.bind"),
              "&AllTests.platform.container5.router.target_socket");
    for (unsigned int i = 1; i <= 16; ++i) {
        const std::string suffix = i < 10 ? "0" + std::to_string(i) : std::to_string(i);
        auto* order_probe = dynamic_cast<router_order_probe*>(
            sc_core::sc_find_object(("AllTests.platform.a_router_order_probe_" + suffix).c_str()));
        ASSERT_TRUE(order_probe != nullptr) << "router ordering probe was not constructed";
        ASSERT_TRUE(order_probe->target_was_constructed)
            << "router bind target was constructed after " << order_probe->name();
    }

    auto* nested_order_probe = dynamic_cast<nested_router_order_probe*>(
        sc_core::sc_find_object("AllTests.platform.a_nested_router_order_probe"));
    ASSERT_TRUE(nested_order_probe != nullptr) << "nested router ordering probe was not constructed";
    ASSERT_TRUE(nested_order_probe->target_was_constructed)
        << "nested router bind target was constructed after the router";
}

int sc_main(int argc, char* argv[])
{
    scp::LoggingGuard logging_guard(scp::LogConfig()
                                        .fileInfoFrom(sc_core::SC_ERROR)
                                        .logAsync(false)
                                        .logLevel(scp::log::INFO)
                                        .msgTypeFieldWidth(50));

    gs::ConfigurableBroker m_broker{};
    cci::cci_originator orig{ "sc_main" };
    auto broker_h = m_broker.create_broker_handle(orig);

    ArgParser ap{ broker_h, argc, argv };

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
