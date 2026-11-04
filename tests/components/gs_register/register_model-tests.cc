/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <register_model.h>
#include <router.h>
#include <libgsutils.h>

#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#define REGISTER_MODEL_CHECK(expression)                          \
    do {                                                          \
        if (!(expression)) throw std::runtime_error(#expression); \
    } while (false)

template <typename Function>
void expect_invalid(Function function)
{
    try {
        function();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("expected invalid register configuration to be rejected");
}

class test_model : public gs::register_model
{
public:
    using gs::register_model::bind_register;
    gs::gs_register<std::uint32_t> scalar;
    gs::gs_register<std::uint32_t> shadow;
    gs::gs_register<gs::gs_bank_of<std::uint32_t>> bank;
    unsigned writes = 0;
    unsigned shadow_writes = 0;
    unsigned accesses = 0;

    test_model(sc_core::sc_module_name name, gs::gs_memory<>& memory, bool relative,
               std::optional<std::uint64_t> mapped_address = {})
        : gs::register_model(
              name, memory, 0x1000, 64, relative, { { 0x1000, { 64, "fixture" } } },
              [this](bool found, std::uint64_t base) {
                  REGISTER_MODEL_CHECK(found && base == 0x1000);
                  ++accesses;
              },
              0, false, mapped_address)
        // Parameter paths intentionally differ from the SystemC socket names.
        , scalar("scalar", "scalar_parameters", 4, 1, 0xffu)
        , shadow("shadow", "shadow_parameters", 4)
        , bank("bank", "bank_parameters", 16, std::vector<std::uint64_t>{ 2, 2 }, std::vector<std::uint64_t>{ 16, 8 },
               0xffu)
    {
        bind_register(scalar, 3);
        bind_register(shadow);
        bind_register(bank);
        shadow.post_write([this](auto&, auto&) { ++shadow_writes; });
        scalar.post_write([this](auto& txn, auto&) {
            REGISTER_MODEL_CHECK(txn.get_address() == 0);
            ++writes;
        });
        bank.post_write([this](auto& txn, auto&) {
            REGISTER_MODEL_CHECK(txn.get_address() == 24);
            ++writes;
        });
    }
};

class bench : public sc_core::sc_module
{
public:
    gs::gs_memory<> memory{ "memory", 0x2000 };
    test_model relative{ "relative", memory, true };
    test_model absolute{ "absolute", memory, false };
    test_model mapped_relative{ "mapped_relative", memory, true, 0x8000 };
    test_model mapped_absolute{ "mapped_absolute", memory, false, 0x8000 };
    gs::router<> mapped_router{ "mapped_router" };
    tlm_utils::simple_initiator_socket<bench> mapped_bus{ "mapped_bus" };
    gs::gs_register<std::uint32_t> grouped_reg{ "grouped", "grouped", 0 };
    gs::register_model::binding_group group{ mapped_relative };
    gs::router<> router{ "router" };
    tlm_utils::simple_initiator_socket<bench> bus{ "bus" };
    gs::register_model top_address_model{ "top_address_model", memory, std::numeric_limits<std::uint64_t>::max() - 3,
                                          4 };
    gs::gs_register<std::uint32_t> top_word{ "top_word", "top_word", 0 };
    std::unique_ptr<gs::register_model> matching;
    bench(sc_core::sc_module_name name): sc_core::sc_module(name)
    {
        bus.bind(router.target_socket);
        router.add_target(memory.socket, 0, 0x2000, false, std::numeric_limits<int>::min());
        router.add_target(relative.target_socket(), 0x1000, 64, true, std::numeric_limits<int>::max());
        mapped_bus.bind(mapped_router.target_socket);
        mapped_router.initiator_socket.bind(mapped_relative.target_socket());

        group.bind_at(grouped_reg, 0x1030);
        REGISTER_MODEL_CHECK(group.size() == 1 && grouped_reg.get_offset() == 0x1030);
        gs::gs_register<std::uint32_t> bad_grouped_reg("bad_grouped", "bad_grouped", 4);
        expect_invalid([&] { group.bind_at(bad_grouped_reg, 0x103c); });
        expect_invalid([&] { group.bind_at(bad_grouped_reg, 0xfff); });
        expect_invalid([&] { group.bind_at(bad_grouped_reg, std::numeric_limits<std::uint64_t>::max()); });
        REGISTER_MODEL_CHECK(group.size() == 1 && bad_grouped_reg.get_offset() == 4);

        gs::gs_register<std::uint32_t> outside("outside", "outside", 64);
        expect_invalid([&] { relative.bind_register(outside); });
        gs::gs_register<gs::gs_bank_of<std::uint32_t>> crossing("crossing", "crossing", 60, 2);
        expect_invalid([&] { relative.bind_register(crossing); });
        gs::gs_register<std::uint32_t> wrapped("wrapped", "wrapped", 8);
        expect_invalid([&] { top_address_model.bind_register(wrapped); });
        gs::gs_register<std::uint64_t> overflowing_span("span", "span", 0);
        expect_invalid([&] { top_address_model.bind_register(overflowing_span); });
        top_address_model.bind_register(top_word);
        REGISTER_MODEL_CHECK(top_word.get_offset() == std::numeric_limits<std::uint64_t>::max() - 3);
        expect_invalid([&] {
            gs::register_model invalid("invalid_aperture", memory, std::numeric_limits<std::uint64_t>::max() - 1, 4);
        });
        expect_invalid([&] {
            gs::register_model invalid("invalid_mapping", memory, 0x1000, 64, true, {}, {}, 0, false,
                                       std::numeric_limits<std::uint64_t>::max() - 1);
        });

        auto broker = cci::cci_get_broker();
        const auto prefix = std::string(this->name()) + ".";
        broker.set_preset_cci_value(prefix + "conflict.target_socket.relative_addresses", cci::cci_value(false));
        expect_invalid([&] { gs::register_model conflict("conflict", memory, 0x1000, 64); });

        // Consistent presets may be supplied ahead of construction.
        broker.set_preset_cci_value(prefix + "matching.target_socket.address", cci::cci_value(std::uint64_t(0x1000)));
        matching = std::make_unique<gs::register_model>("matching", memory, 0x1000, 64);
        for (const auto& entry : { std::make_pair("address", cci::cci_value(std::uint64_t(0))),
                                   std::make_pair("size", cci::cci_value(std::uint64_t(128))),
                                   std::make_pair("relative_addresses", cci::cci_value(false)) }) {
            const auto parameter = prefix + "relative.target_socket." + entry.first;
            const auto previous = broker.get_preset_cci_value(parameter);
            bool rejected = false;
            try {
                broker.set_preset_cci_value(parameter, entry.second);
            } catch (const sc_core::sc_report&) {
                rejected = true;
            }
            REGISTER_MODEL_CHECK(rejected);
            REGISTER_MODEL_CHECK(broker.get_preset_cci_value(parameter) == previous);
        }
    }

private:
    std::uint32_t transfer(std::uint64_t address, bool write, std::uint32_t value, gs::register_model* direct = nullptr)
    {
        tlm::tlm_generic_payload txn;
        txn.set_address(address);
        txn.set_command(write ? tlm::TLM_WRITE_COMMAND : tlm::TLM_READ_COMMAND);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        txn.set_data_length(sizeof(value));
        txn.set_streaming_width(sizeof(value));
        auto delay = sc_core::SC_ZERO_TIME;
        if (direct)
            direct->b_transport(txn, delay);
        else
            bus->b_transport(txn, delay);
        REGISTER_MODEL_CHECK(txn.get_response_status() == tlm::TLM_OK_RESPONSE);
        REGISTER_MODEL_CHECK(txn.get_address() == address);
        REGISTER_MODEL_CHECK(!txn.is_dmi_allowed());
        REGISTER_MODEL_CHECK(txn.get_extension<gs::PathIDExtension>() == nullptr);
        return value;
    }

public:
    void run()
    {
        REGISTER_MODEL_CHECK(relative.scalar.get_offset() == 0x1004);
        REGISTER_MODEL_CHECK(relative.bank.get_offset() == 0x1010);
        std::uint32_t scalar = 0x12345678u;
        relative.scalar.set(&scalar, 0, 1, false);
        relative.bank.internal_write({ 1, 1 }, 0xaabbccddu);
        REGISTER_MODEL_CHECK(transfer(0x1004, false, 0) == scalar);
        transfer(0x1004, true, 0xffffffffu);
        REGISTER_MODEL_CHECK(static_cast<std::uint32_t>(relative.scalar) == 0x123456ffu);
        transfer(0x1028, true, 0x11223344u);
        REGISTER_MODEL_CHECK(static_cast<std::uint32_t>(relative.bank(1, 1)) == 0xaabbcc44u);
        REGISTER_MODEL_CHECK(relative.writes == 2);
        REGISTER_MODEL_CHECK(relative.shadow_writes == 0);
        REGISTER_MODEL_CHECK(transfer(4, false, 0, &relative) == 0x123456ffu);
        REGISTER_MODEL_CHECK(transfer(0x1004, false, 0, &absolute) == 0x123456ffu);
        REGISTER_MODEL_CHECK(transfer(4, false, 0, &mapped_relative) == 0x123456ffu);
        REGISTER_MODEL_CHECK(transfer(0x8004, false, 0, &mapped_absolute) == 0x123456ffu);
        std::uint32_t mapped_value = 0;
        tlm::tlm_generic_payload mapped;
        mapped.set_address(0x8004);
        mapped.set_command(tlm::TLM_READ_COMMAND);
        mapped.set_data_ptr(reinterpret_cast<unsigned char*>(&mapped_value));
        mapped.set_data_length(sizeof(mapped_value));
        mapped.set_streaming_width(sizeof(mapped_value));
        auto mapped_delay = sc_core::SC_ZERO_TIME;
        mapped_bus->b_transport(mapped, mapped_delay);
        REGISTER_MODEL_CHECK(mapped.get_response_status() == tlm::TLM_OK_RESPONSE);
        REGISTER_MODEL_CHECK(mapped.get_address() == 0x8004 && mapped_value == 0x123456ffu);
        mapped_value = 0;
        REGISTER_MODEL_CHECK(mapped_absolute.transport_dbg(mapped) == sizeof(mapped_value));
        REGISTER_MODEL_CHECK(mapped.get_address() == 0x8004 && mapped_value == 0x123456ffu);
        mapped.set_address(0x7fff);
        REGISTER_MODEL_CHECK(mapped_absolute.transport_dbg(mapped) == 0);
        REGISTER_MODEL_CHECK(mapped.get_response_status() == tlm::TLM_ADDRESS_ERROR_RESPONSE);
        REGISTER_MODEL_CHECK(mapped.get_address() == 0x7fff);

        // Unselected registers and bank gaps retain ordinary backing-memory access.
        transfer(0x1008, true, 0xabcdef01u);
        REGISTER_MODEL_CHECK(transfer(8, false, 0, &relative) == 0xabcdef01u);
        transfer(0x101c, true, 0x88776655u);
        REGISTER_MODEL_CHECK(transfer(0x101c, false, 0, &absolute) == 0x88776655u);

        // Debug transfers bypass masks and callbacks, but retain the access hook.
        std::uint32_t raw = 0x55667788u;
        tlm::tlm_generic_payload debug;
        debug.set_address(4);
        debug.set_command(tlm::TLM_WRITE_COMMAND);
        debug.set_data_ptr(reinterpret_cast<unsigned char*>(&raw));
        debug.set_data_length(sizeof(raw));
        debug.set_streaming_width(sizeof(raw));
        const auto accesses = relative.accesses;
        REGISTER_MODEL_CHECK(relative.transport_dbg(debug) == sizeof(raw));
        REGISTER_MODEL_CHECK(debug.get_address() == 4);
        REGISTER_MODEL_CHECK(relative.accesses == accesses + 1);
        REGISTER_MODEL_CHECK(relative.writes == 2 && relative.shadow_writes == 0);
        REGISTER_MODEL_CHECK(static_cast<std::uint32_t>(relative.scalar) == raw);

        // A direct request crossing the aperture must leave memory unchanged.
        std::uint32_t value = 0xffffffffu;
        tlm::tlm_generic_payload txn;
        txn.set_address(62);
        txn.set_command(tlm::TLM_WRITE_COMMAND);
        txn.set_data_ptr(reinterpret_cast<unsigned char*>(&value));
        txn.set_data_length(sizeof(value));
        txn.set_streaming_width(sizeof(value));
        auto delay = sc_core::SC_ZERO_TIME;
        relative.b_transport(txn, delay);
        REGISTER_MODEL_CHECK(txn.get_response_status() == tlm::TLM_ADDRESS_ERROR_RESPONSE);
        REGISTER_MODEL_CHECK(txn.get_address() == 62);
    }
};

int sc_main(int, char**)
{
    try {
        gs::ConfigurableBroker broker;
        auto presets = cci::cci_get_global_broker(cci::cci_originator("register_model_test"));
        presets.set_preset_cci_value("bench.memory.target_socket.address", cci::cci_value(std::uint64_t(0)));
        presets.set_preset_cci_value("bench.memory.target_socket.relative_addresses", cci::cci_value(false));
        presets.set_preset_cci_value("bench.memory.init_mem", cci::cci_value(true));
        bench test("bench");
        sc_core::sc_start(sc_core::SC_ZERO_TIME);
        test.run();
        std::cout << "Register model binding, aperture validation and metadata consistency passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
