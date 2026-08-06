/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef GS_REGISTERS_H
#define GS_REGISTERS_H

#include <type_traits>

#include <systemc>
#include <cci_configuration>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <scp/report.h>
#include <cciutils.h>
#include <tlm_sockets_buswidth.h>
#include <vector>
#include <algorithm>
#include <limits>
#include <bitset>
#include <initializer_list>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace gs {

template <typename T>
static T gs_full_mask()
{
    return static_cast<T>(std::bitset<std::numeric_limits<T>::digits>(0).flip().to_ullong());
}

template <typename T>
static T gs_mask_for_field(uint32_t start, uint32_t length)
{
    constexpr uint32_t width = std::numeric_limits<T>::digits;
    if (length == 0 || start >= width || length > width - start) {
        sc_assert(false);
        return 0;
    }
    if (length == width) return gs_full_mask<T>();
    return static_cast<T>(gs_full_mask<T>() >> (width - length));
}

/** Bank descriptor: template parameter for the gs_register bank specialisation. */
template <class ELEM = uint32_t>
struct gs_bank_of {
    using elem_type = ELEM;
    static_assert(std::is_unsigned<ELEM>::value, "Bank element type must be unsigned");
};

template <class>
struct is_gs_bank : std::false_type {
};
template <class E>
struct is_gs_bank<gs_bank_of<E>> : std::true_type {
};

/** Result of decoding a transaction against a bank layout; `bool` == on-element. */
struct gs_bank_access {
    uint64_t index;                // flat row-major index
    uint64_t byte_offset;          // addr - base
    std::vector<uint64_t> indices; // per-dim, outer to inner
    bool aligned;
    bool in_range;
    explicit operator bool() const { return aligned && in_range; }
};

/**
 * @brief  Provide a class to provide b_transport callback
 *
 */
class tlm_fnct
{
public:
    typedef std::function<void(tlm::tlm_generic_payload&, sc_core::sc_time&)> TLMFUNC;

private:
    TLMFUNC m_fnct;

public:
    tlm_fnct(tlm_fnct&) = delete;
    tlm_fnct(TLMFUNC cb): m_fnct(cb) {}
    void operator()(tlm::tlm_generic_payload& txn, sc_core::sc_time& delay) { m_fnct(txn, delay); }
    virtual void dummy() {} // force polymorphism
};

/**
 * @brief A class that encapsulates a simple target port and a set of b_transport lambda functions for pre/post
 * read/write. when b_transport is called port, the correct lambda's will be invoked
 *
 */
class port_fnct : public tlm_utils::simple_target_socket<port_fnct, DEFAULT_TLM_BUSWIDTH>
{
    SCP_LOGGER((), "register");
    std::vector<std::shared_ptr<tlm_fnct>> m_pre_read_fncts;
    std::vector<std::shared_ptr<tlm_fnct>> m_pre_write_fncts;
    std::vector<std::shared_ptr<tlm_fnct>> m_post_read_fncts;
    std::vector<std::shared_ptr<tlm_fnct>> m_post_write_fncts;
    cci::cci_param<bool> p_is_callback;
    bool m_in_callback = false;

    std::function<unsigned int(tlm::tlm_generic_payload&)> transport_dbg_func;

    std::string my_name()
    {
        std::string n(name());
        n = n.substr(0, n.length() - strlen("_target_socket")); // get rid of last part of string
        n = n.substr(n.find_last_of(".") + 1);                  // take the last part.
        return n;
    }

    /* to be implemented !!!!*/
    unsigned int transport_dbg(tlm::tlm_generic_payload& txn)
    {
        if (transport_dbg_func) {
            return transport_dbg_func(txn);
        } else
            return 0;
    }

    void b_transport(tlm::tlm_generic_payload& txn, sc_core::sc_time& delay)
    {
        if (m_pre_read_fncts.size() || m_pre_write_fncts.size() || m_post_read_fncts.size() ||
            m_post_write_fncts.size()) {
            if (!m_in_callback) {
                m_in_callback = true;
                switch (txn.get_response_status()) {
                case tlm::TLM_INCOMPLETE_RESPONSE:
                    switch (txn.get_command()) {
                    case tlm::TLM_READ_COMMAND:
                        if (m_pre_read_fncts.size()) {
                            SCP_INFO(())("Pre-Read callbacks: {}", my_name());
                            for (auto& cb : m_pre_read_fncts) (*cb)(txn, delay);
                        }
                        break;
                    case tlm::TLM_WRITE_COMMAND:
                        if (m_pre_write_fncts.size()) {
                            SCP_INFO(())("Pre-Write callbacks: {}", my_name());
                            for (auto& cb : m_pre_write_fncts) (*cb)(txn, delay);
                        }
                        break;
                    default:
                        break;
                    }
                    capture_txn_pre(txn);
                    break;
                case tlm::TLM_OK_RESPONSE:
                    handle_mask_post(txn);
                    switch (txn.get_command()) {
                    case tlm::TLM_READ_COMMAND:
                        if (m_post_read_fncts.size()) {
                            SCP_INFO(())("Post-Read callbacks: {}", my_name());
                            for (auto& cb : m_post_read_fncts) (*cb)(txn, delay);
                        }
                        break;
                    case tlm::TLM_WRITE_COMMAND:
                        if (m_post_write_fncts.size()) {
                            SCP_INFO(())("Post-Write callbacks: {}", my_name());
                            for (auto& cb : m_post_write_fncts) (*cb)(txn, delay);
                        }
                        break;
                    default:
                        break;
                    }
                    break;
                default:
                    break;
                }
                m_in_callback = false;
            }
        }
        txn.set_dmi_allowed(false);
    }

    /*template <typename T>
    void is_a_do(std::shared_ptr<tlm_fnct> cb, std::function<void()> fn)
    {
        auto cbt = dynamic_cast<T*>(cb.get());
        if (cbt) fn();
    }*/

public:
    void pre_read(tlm_fnct::TLMFUNC cb) { m_pre_read_fncts.push_back(std::make_shared<tlm_fnct>(cb)); }
    void pre_write(tlm_fnct::TLMFUNC cb) { m_pre_write_fncts.push_back(std::make_shared<tlm_fnct>(cb)); }
    void post_read(tlm_fnct::TLMFUNC cb) { m_post_read_fncts.push_back(std::make_shared<tlm_fnct>(cb)); }
    void post_write(tlm_fnct::TLMFUNC cb) { m_post_write_fncts.push_back(std::make_shared<tlm_fnct>(cb)); }
    virtual void capture_txn_pre(tlm::tlm_generic_payload& txn) = 0;
    virtual void handle_mask_post(tlm::tlm_generic_payload& txn) = 0;

    port_fnct() = delete;
    port_fnct(std::string name, std::string path_name)
        : simple_target_socket<port_fnct, DEFAULT_TLM_BUSWIDTH>((name + "_target_socket").c_str())
        , p_is_callback(path_name + ".target_socket.is_callback", true, "Is a callback (true)")
    {
        SCP_LOGGER_NAME().features[0] = parent(sc_core::sc_object::name()) + "." + name;
        SCP_TRACE(())("Constructor");
        p_is_callback = true;
        p_is_callback.lock();
        tlm_utils::simple_target_socket<port_fnct, DEFAULT_TLM_BUSWIDTH>::register_b_transport(this,
                                                                                               &port_fnct::b_transport);
        tlm_utils::simple_target_socket<port_fnct, DEFAULT_TLM_BUSWIDTH>::register_transport_dbg(
            this, &port_fnct::transport_dbg);
    }

    void register_transport_dbg_func(std::function<unsigned int(tlm::tlm_generic_payload&)> fn)
    {
        transport_dbg_func = fn;
    }

protected:
    std::string parent(std::string name) { return name.substr(0, name.find_last_of('.')); }
};

/**
 * @brief  A proxy data class that stores it's value using a b_transport interface
 *  Data is passed by pointer, array indexing (operator[]) is supported.
 */
template <class TYPE>
class proxy_data_array
{
    scp::scp_logger_cache& SCP_LOGGER_NAME();
    TYPE* m_dmi = nullptr;

    void check_dmi()
    {
        tlm::tlm_generic_payload m_txn;
        tlm::tlm_dmi m_dmi_data;
        const uint64_t span = p_size.get_value();
        m_txn.set_byte_enable_length(0);
        m_txn.set_dmi_allowed(false);
        m_txn.set_command(tlm::TLM_IGNORE_COMMAND);
        m_txn.set_data_ptr(nullptr);
        m_txn.set_address(p_offset);
        m_txn.set_data_length(span);
        m_txn.set_streaming_width(span);
        m_txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        if (initiator_socket->get_direct_mem_ptr(m_txn, m_dmi_data)) {
            uint64_t start = m_dmi_data.get_start_address();
            unsigned char* ptr = m_dmi_data.get_dmi_ptr();
            ptr += (p_offset - start);
            sc_assert(m_dmi_data.get_end_address() >= start + p_offset + span);
            m_dmi = reinterpret_cast<TYPE*>(ptr);
        }
    }

    uint8_t* dmi_bytes_at(uint64_t idx) { return reinterpret_cast<uint8_t*>(m_dmi) + p_stride.get_value() * idx; }

public:
    std::string m_path_name;
    cci::cci_param<uint64_t> p_number;
    cci::cci_param<uint64_t> p_offset;
    cci::cci_param<uint64_t> p_size;
    cci::cci_param<uint64_t> p_stride;
    cci::cci_param<uint64_t> p_elem_size;
    cci::cci_param<TYPE> p_mask;
    cci::cci_param<bool> p_relative_addresses;

    tlm_utils::simple_initiator_socket<proxy_data_array, DEFAULT_TLM_BUSWIDTH> initiator_socket;

    void get(TYPE* dst, uint64_t idx = 0, uint64_t length = 1)
    {
        sc_assert(idx + length <= p_number);
        const uint64_t stride = p_stride.get_value();
        if (m_dmi) {
            for (uint64_t i = 0; i < length; i++) memcpy(&dst[i], dmi_bytes_at(idx + i), sizeof(TYPE));
            SCP_TRACE(())("Got value (DMI) : [{:#x}]", fmt::join(std::vector<TYPE>(&dst[0], &dst[length]), ","));
        } else if (length == 1 || stride == sizeof(TYPE)) {
            tlm::tlm_generic_payload m_txn;
            sc_core::sc_time dummy;
            m_txn.set_byte_enable_length(0);
            m_txn.set_dmi_allowed(false);
            m_txn.set_command(tlm::TLM_READ_COMMAND);
            m_txn.set_data_ptr(reinterpret_cast<unsigned char*>(dst));
            m_txn.set_address(p_offset + stride * idx);
            m_txn.set_data_length(sizeof(TYPE) * length);
            m_txn.set_streaming_width(sizeof(TYPE) * length);
            m_txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            initiator_socket->b_transport(m_txn, dummy);
            sc_assert(m_txn.get_response_status() == tlm::TLM_OK_RESPONSE);
            SCP_TRACE(())("Got value (transport) : [{:#x}]", fmt::join(std::vector<TYPE>(&dst[0], &dst[length]), ","));
            if (m_txn.is_dmi_allowed()) check_dmi();
        } else {
            // gapped bank: one transaction per element
            for (uint64_t i = 0; i < length; i++) {
                tlm::tlm_generic_payload m_txn;
                sc_core::sc_time dummy;
                m_txn.set_byte_enable_length(0);
                m_txn.set_dmi_allowed(false);
                m_txn.set_command(tlm::TLM_READ_COMMAND);
                m_txn.set_data_ptr(reinterpret_cast<unsigned char*>(&dst[i]));
                m_txn.set_address(p_offset + stride * (idx + i));
                m_txn.set_data_length(sizeof(TYPE));
                m_txn.set_streaming_width(sizeof(TYPE));
                m_txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
                initiator_socket->b_transport(m_txn, dummy);
                sc_assert(m_txn.get_response_status() == tlm::TLM_OK_RESPONSE);
                if (m_txn.is_dmi_allowed()) check_dmi();
            }
            SCP_TRACE(())
            ("Got value (transport strided) : [{:#x}]", fmt::join(std::vector<TYPE>(&dst[0], &dst[length]), ","));
        }
    }

    void set(TYPE* src, uint64_t idx = 0, uint64_t length = 1, bool use_mask = true)
    {
        sc_assert(idx + length <= p_number);
        const uint64_t stride = p_stride.get_value();
        const bool full_mask = (p_mask.get_value() == gs_full_mask<TYPE>());
        if (m_dmi) {
            SCP_TRACE(())("Set value (DMI) : [{:#x}]", fmt::join(std::vector<TYPE>(&src[0], &src[length]), ","));
            if (!use_mask || full_mask) {
                for (uint64_t i = 0; i < length; i++) memcpy(dmi_bytes_at(idx + i), &src[i], sizeof(TYPE));
            } else {
                for (uint64_t i = 0; i < length; i++) {
                    TYPE cur;
                    memcpy(&cur, dmi_bytes_at(idx + i), sizeof(TYPE));
                    write_with_mask(&src[i], &cur, 1);
                    memcpy(dmi_bytes_at(idx + i), &cur, sizeof(TYPE));
                }
            }
        } else if ((length == 1 || stride == sizeof(TYPE)) && (full_mask || !use_mask)) {
            tlm::tlm_generic_payload m_txn;
            sc_core::sc_time dummy;
            m_txn.set_data_ptr(reinterpret_cast<unsigned char*>(src));
            m_txn.set_byte_enable_length(0);
            m_txn.set_dmi_allowed(false);
            m_txn.set_command(tlm::TLM_WRITE_COMMAND);
            m_txn.set_address(p_offset + stride * idx);
            m_txn.set_data_length(sizeof(TYPE) * length);
            m_txn.set_streaming_width(sizeof(TYPE) * length);
            m_txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            SCP_TRACE(())("Set value (transport) : [{:#x}]", fmt::join(std::vector<TYPE>(&src[0], &src[length]), ","));
            initiator_socket->b_transport(m_txn, dummy);
            sc_assert(m_txn.get_response_status() == tlm::TLM_OK_RESPONSE);
            if (m_txn.is_dmi_allowed()) check_dmi();
        } else {
            for (uint64_t i = 0; i < length; i++) {
                tlm::tlm_generic_payload m_txn;
                sc_core::sc_time dummy;
                std::vector<unsigned char> curr_data;
                unsigned char* data_ptr;
                if (!use_mask || full_mask) {
                    data_ptr = reinterpret_cast<unsigned char*>(&src[i]);
                } else {
                    curr_data.resize(sizeof(TYPE));
                    get(reinterpret_cast<TYPE*>(curr_data.data()), idx + i, 1);
                    TYPE tmp = src[i];
                    write_with_mask(&tmp, reinterpret_cast<TYPE*>(curr_data.data()), 1);
                    data_ptr = curr_data.data();
                }
                m_txn.set_data_ptr(data_ptr);
                m_txn.set_byte_enable_length(0);
                m_txn.set_dmi_allowed(false);
                m_txn.set_command(tlm::TLM_WRITE_COMMAND);
                m_txn.set_address(p_offset + stride * (idx + i));
                m_txn.set_data_length(sizeof(TYPE));
                m_txn.set_streaming_width(sizeof(TYPE));
                m_txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
                initiator_socket->b_transport(m_txn, dummy);
                sc_assert(m_txn.get_response_status() == tlm::TLM_OK_RESPONSE);
                if (m_txn.is_dmi_allowed()) check_dmi();
            }
            SCP_TRACE(())
            ("Set value (transport strided) : [{:#x}]", fmt::join(std::vector<TYPE>(&src[0], &src[length]), ","));
        }
    }

    void write_with_mask(TYPE* src, TYPE* dst, uint64_t length)
    {
        write_with_mask(src, dst, length, p_mask.get_value());
    }

    void write_with_mask(TYPE* src, TYPE* dst, uint64_t length, TYPE mask)
    {
        if (!src) {
            SCP_FATAL(())("write_with_mask(): src pointer is NULL");
        }
        if (!dst) {
            SCP_FATAL(())("write_with_mask(): dst pointer is NULL");
        }
        std::vector<TYPE> mask_to_apply(length, mask);
        std::vector<TYPE> new_and_mask(length, 0);     // result of *src & mask
        std::vector<TYPE> old_and_not_mask(length, 0); // result of *dmi & ~mask
        std::transform(src, src + length, mask_to_apply.cbegin(), new_and_mask.begin(),
                       [](TYPE src_val, TYPE mask_val) { return src_val & mask_val; });
        std::transform(dst, dst + length, mask_to_apply.cbegin(), old_and_not_mask.begin(),
                       [](TYPE curr_val, TYPE mask_val) { return curr_val & (~mask_val); });
        std::transform(
            new_and_mask.cbegin(), new_and_mask.cend(), old_and_not_mask.cbegin(), dst,
            [](TYPE new_and_mask_val, TYPE old_and_not_mask_val) { return new_and_mask_val | old_and_not_mask_val; });
    }

    TYPE& operator[](int idx)
    {
        if (!m_dmi) {
            check_dmi();
            if (!m_dmi) {
                SCP_FATAL(())("Operator[] can only be used for DMI'able registers");
            }
        }
        SCP_TRACE(())("Access value (DMI) using operator [] at idx {}", idx);
        if (p_mask.get_value() != gs_full_mask<TYPE>()) {
            SCP_FATAL(()) << "operator[](): Register Mask: 0x" << std::hex << p_mask.get_value()
                          << " has readonly bits, please use set(TYPE* src, uint64_t idx, uint64_t length) and "
                             "get(TYPE* dst, uint64_t idx, uint64_t length) instead";
        }
        return *reinterpret_cast<TYPE*>(dmi_bytes_at(static_cast<uint64_t>(idx)));
    }

    /** Single-element read at a raw byte offset relative to p_offset. */
    void get_bytes(TYPE* dst, uint64_t byte_offset)
    {
        if (m_dmi) {
            memcpy(dst, reinterpret_cast<uint8_t*>(m_dmi) + byte_offset, sizeof(TYPE));
        } else {
            tlm::tlm_generic_payload m_txn;
            sc_core::sc_time dummy;
            m_txn.set_byte_enable_length(0);
            m_txn.set_dmi_allowed(false);
            m_txn.set_command(tlm::TLM_READ_COMMAND);
            m_txn.set_data_ptr(reinterpret_cast<unsigned char*>(dst));
            m_txn.set_address(p_offset + byte_offset);
            m_txn.set_data_length(sizeof(TYPE));
            m_txn.set_streaming_width(sizeof(TYPE));
            m_txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            initiator_socket->b_transport(m_txn, dummy);
            sc_assert(m_txn.get_response_status() == tlm::TLM_OK_RESPONSE);
            if (m_txn.is_dmi_allowed()) check_dmi();
        }
    }

    /** Single-element write at a raw byte offset; RMW-masked when use_mask && !full_mask. */
    void set_bytes(TYPE* src, uint64_t byte_offset, bool use_mask = true)
    {
        const TYPE mask = mask_for_byte_offset(byte_offset);
        const bool full_mask = (mask == gs_full_mask<TYPE>());
        if (m_dmi) {
            if (!use_mask || full_mask) {
                memcpy(reinterpret_cast<uint8_t*>(m_dmi) + byte_offset, src, sizeof(TYPE));
            } else {
                TYPE cur;
                memcpy(&cur, reinterpret_cast<uint8_t*>(m_dmi) + byte_offset, sizeof(TYPE));
                write_with_mask(src, &cur, 1, mask);
                memcpy(reinterpret_cast<uint8_t*>(m_dmi) + byte_offset, &cur, sizeof(TYPE));
            }
        } else {
            tlm::tlm_generic_payload m_txn;
            sc_core::sc_time dummy;
            std::vector<unsigned char> curr_data;
            unsigned char* data_ptr;
            if (!use_mask || full_mask) {
                data_ptr = reinterpret_cast<unsigned char*>(src);
            } else {
                curr_data.resize(sizeof(TYPE));
                get_bytes(reinterpret_cast<TYPE*>(curr_data.data()), byte_offset);
                TYPE tmp = *src;
                write_with_mask(&tmp, reinterpret_cast<TYPE*>(curr_data.data()), 1, mask);
                data_ptr = curr_data.data();
            }
            m_txn.set_data_ptr(data_ptr);
            m_txn.set_byte_enable_length(0);
            m_txn.set_dmi_allowed(false);
            m_txn.set_command(tlm::TLM_WRITE_COMMAND);
            m_txn.set_address(p_offset + byte_offset);
            m_txn.set_data_length(sizeof(TYPE));
            m_txn.set_streaming_width(sizeof(TYPE));
            m_txn.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
            initiator_socket->b_transport(m_txn, dummy);
            sc_assert(m_txn.get_response_status() == tlm::TLM_OK_RESPONSE);
            if (m_txn.is_dmi_allowed()) check_dmi();
        }
    }

    void invalidate_direct_mem_ptr(sc_dt::uint64 start, sc_dt::uint64 end) { m_dmi = nullptr; }

protected:
    virtual bool has_element_masks() const { return false; }
    virtual TYPE mask_for_element(uint64_t index) const { return p_mask.get_value(); }
    virtual TYPE mask_for_byte_offset(uint64_t byte_offset) const { return p_mask.get_value(); }

public:
    proxy_data_array(scp::scp_logger_cache& logger, std::string name, std::string path_name, uint64_t _offset = 0,
                     uint64_t number = 1, TYPE mask = gs_full_mask<TYPE>(), uint64_t stride = sizeof(TYPE),
                     uint64_t total_span_override = 0)
        : SCP_LOGGER_NAME()(logger)
        , m_path_name(path_name)
        , p_number(path_name + ".number", number, "number of elements in this register")
        , initiator_socket((name + "_initiator_socket").c_str())
        , p_offset(path_name + ".target_socket.address", _offset, "Offset of this register")
        , p_size(path_name + ".target_socket.size",
                 (total_span_override != 0 ? total_span_override
                                           : (number == 0 ? 0 : stride * (number - 1) + sizeof(TYPE))),
                 "size of this register")
        , p_stride(path_name + ".target_socket.stride", stride, "stride in bytes between elements")
        , p_elem_size(path_name + ".target_socket.elem_size", sizeof(TYPE), "size in bytes of one element")
        , p_mask(path_name + ".target_socket.mask", mask, " R/W mask, 0 means read only, and 1 means write/read")
        , p_relative_addresses(path_name + ".target_socket.relative_addresses", true,
                               "allow relative_addresses for this register")
    {
        initiator_socket.register_invalidate_direct_mem_ptr(this,
                                                            &gs::proxy_data_array<TYPE>::invalidate_direct_mem_ptr);
    }
};

/**
 * @brief  A proxy data class that stores it's value using a b_transport interface
 *  Data is passed by value.
 */
template <class TYPE = uint32_t>
class proxy_data : public proxy_data_array<TYPE>
{
    scp::scp_logger_cache& SCP_LOGGER_NAME();

public:
    TYPE get()
    {
        TYPE tmp;
        proxy_data_array<TYPE>::get(&tmp);
        return tmp;
    }
    operator TYPE() { return get(); }

    void set(TYPE value) { proxy_data_array<TYPE>::set(&value); }
    void operator=(TYPE value) { set(value); }

    proxy_data(scp::scp_logger_cache& logger, std::string name, std::string path_name, uint64_t offset = 0,
               uint64_t number = 1, uint64_t start = 0, uint64_t length = sizeof(TYPE) * 8,
               TYPE mask = gs_full_mask<TYPE>())
        : proxy_data_array<TYPE>(logger, name, path_name, offset, number, mask), SCP_LOGGER_NAME()(logger)
    {
        static_assert(std::is_unsigned<TYPE>::value, "Register types must be unsigned");
    }
};

/* forward declaration */
template <class TYPE = uint32_t>
class gs_bitfield;
template <class TYPE = uint32_t>
class gs_field;
template <class TYPE>
class gs_register_element;
/**
 * @brief Class that encapsulates a 'register' that proxies it's data via a tlm interface,
 * and uses callbacks (on a tlm interface)
 *
 */
template <class TYPE = uint32_t>
class gs_register : public port_fnct, public proxy_data<TYPE>
{
    SCP_LOGGER((), "register");

private:
    std::string m_regname;
    std::string m_path;
    std::vector<unsigned char> captured_txn_data;

public:
    gs_register() = delete;
    gs_register(std::string _name, std::string path = "", uint64_t offset = 0, uint64_t number = 1,
                TYPE mask = gs_full_mask<TYPE>())
        : m_regname(_name)
        , m_path(path)
        , port_fnct(_name, path)
        , proxy_data<TYPE>(SCP_LOGGER_NAME(), _name, path, offset, number, mask)
    {
        std::string n(name());
        SCP_LOGGER_NAME().features[0] = parent(n) + "." + _name;
        n = n.substr(0, n.length() - strlen("_target_socket")); // get rid of last part of string
        SCP_TRACE((), n)("constructor : {} attching in {}", _name, path);

        register_transport_dbg_func([&](tlm::tlm_generic_payload& txn) {
            if (txn.get_data_length() < sizeof(TYPE)) return static_cast<unsigned int>(0);
            unsigned char* data = txn.get_data_ptr();
            if (txn.get_command() == tlm::TLM_READ_COMMAND) {
                TYPE tmp = proxy_data<TYPE>::get();
                memset(data, 0, txn.get_data_length());
                memcpy(data, &tmp, sizeof(TYPE));
                return static_cast<unsigned int>(sizeof(TYPE));
            }
            if (txn.get_command() == tlm::TLM_WRITE_COMMAND) {
                TYPE tmp;
                memcpy(&tmp, data, sizeof(TYPE));
                proxy_data<TYPE>::set(tmp);
                return static_cast<unsigned int>(sizeof(TYPE));
            }
            return static_cast<unsigned int>(0);
        });
    }
    void operator=(TYPE value) { proxy_data<TYPE>::set(value); }
    operator TYPE() { return proxy_data<TYPE>::get(); }

    void operator=(gs_register<TYPE>& other) { proxy_data<TYPE>::set(proxy_data<TYPE>::get()); }
    void operator+=(TYPE other) { proxy_data<TYPE>::set(proxy_data<TYPE>::get() + other); }
    void operator-=(TYPE other) { proxy_data<TYPE>::set(proxy_data<TYPE>::get() - other); }
    void operator/=(TYPE other)
    {
        if (other == 0) SCP_FATAL(())("Trying to divide a register value by 0!");
        proxy_data<TYPE>::set(proxy_data<TYPE>::get() / other);
    }
    void operator*=(TYPE other) { proxy_data<TYPE>::set(proxy_data<TYPE>::get() * other); }
    void operator&=(TYPE other) { proxy_data<TYPE>::set(proxy_data<TYPE>::get() & other); }
    void operator|=(TYPE other) { proxy_data<TYPE>::set(proxy_data<TYPE>::get() | other); }
    void operator^=(TYPE other) { proxy_data<TYPE>::set(proxy_data<TYPE>::get() ^ other); }
    void operator<<=(TYPE other) { proxy_data<TYPE>::set(proxy_data<TYPE>::get() << other); }
    void operator>>=(TYPE other) { proxy_data<TYPE>::set(proxy_data<TYPE>::get() >> other); }

    gs_register_element<TYPE> operator[](int idx);
    void get(TYPE* dst, uint64_t idx, uint64_t length) { return proxy_data_array<TYPE>::get(dst, idx, length); }
    void set(TYPE* src, uint64_t idx, uint64_t length, bool use_mask = true)
    {
        return proxy_data_array<TYPE>::set(src, idx, length, use_mask);
    }

    gs_bitfield<TYPE> operator[](gs_field<TYPE>& f) { return gs_bitfield<TYPE>(*this, f); }
    std::string get_regname() const { return m_regname; }
    std::string get_path() const { return m_path; }
    uint64_t get_offset() const { return proxy_data<TYPE>::p_offset.get_value(); }
    uint64_t get_size() const { return proxy_data<TYPE>::p_size.get_value(); }
    void set_mask(TYPE mask) // mask is allowed to be set to implement e.g. write-once semantics
    {
        SCP_TRACE(()) << "Set Mask to 0x" << std::hex << mask;
        proxy_data<TYPE>::p_mask = mask;
    }
    TYPE get_mask() const { return proxy_data<TYPE>::p_mask.get_value(); }
    void capture_txn_pre(tlm::tlm_generic_payload& txn) override
    {
        if ((proxy_data<TYPE>::p_mask.get_value() == gs_full_mask<TYPE>()) ||
            (txn.get_command() != tlm::tlm_command::TLM_WRITE_COMMAND)) {
            return;
        }
        unsigned int txn_data_len = txn.get_data_length();
        if ((txn_data_len == 0) || (txn_data_len % sizeof(TYPE)))
            SCP_FATAL(()) << "capture_txn_pre(): txn data length should be n * sizeof(TYPE) where n >= 1";
        uint64_t txn_addr = txn.get_address();
        const uint64_t stride = proxy_data<TYPE>::p_stride.get_value();
        uint64_t idx = 0;
        if (proxy_data<TYPE>::p_relative_addresses.get_value()) {
            idx = txn_addr / stride;
        } else {
            idx = (txn_addr - proxy_data<TYPE>::p_offset.get_value()) / stride;
        }
        captured_txn_data.clear();
        captured_txn_data.resize(txn_data_len);
        get(reinterpret_cast<TYPE*>(captured_txn_data.data()), idx, txn_data_len / sizeof(TYPE));
    }
    void handle_mask_post(tlm::tlm_generic_payload& txn) override
    {
        if ((proxy_data<TYPE>::p_mask.get_value() == gs_full_mask<TYPE>()) ||
            (txn.get_command() != tlm::tlm_command::TLM_WRITE_COMMAND)) {
            return;
        }
        unsigned int txn_data_len = txn.get_data_length();
        if ((txn_data_len == 0) || (txn_data_len % sizeof(TYPE)))
            SCP_FATAL(()) << "handle_mask_post(): txn data length should be n * sizeof(TYPE) where n >= 1";
        uint64_t txn_addr = txn.get_address();
        const uint64_t stride = proxy_data<TYPE>::p_stride.get_value();
        uint64_t idx = 0;
        if (proxy_data<TYPE>::p_relative_addresses.get_value()) {
            idx = txn_addr / stride;
        } else {
            idx = (txn_addr - proxy_data<TYPE>::p_offset.get_value()) / stride;
        }
        unsigned char* txn_data_ptr = txn.get_data_ptr();
        proxy_data<TYPE>::write_with_mask(reinterpret_cast<TYPE*>(txn_data_ptr),
                                          reinterpret_cast<TYPE*>(captured_txn_data.data()),
                                          txn_data_len / sizeof(TYPE));
        set(reinterpret_cast<TYPE*>(captured_txn_data.data()), idx, txn_data_len / sizeof(TYPE), false);
        memcpy(txn_data_ptr, captured_txn_data.data(), txn_data_len);
    }
};

/**
 * @brief Proxy for bitfield access to a register.
 *
 */
template <class TYPE>
class gs_bitfield
{
    uint32_t start, length;
    gs_register<TYPE>& m_reg;

public:
    gs_bitfield(gs_register<TYPE>& r, uint32_t s, uint32_t l): m_reg(r), start(s), length(l) {}

    gs_bitfield(gs_register<TYPE>& r, gs_bitfield& f): m_reg(r), start(f.start), length(f.length) {}

    void operator=(TYPE value)
    {
        TYPE field_mask = gs_mask_for_field<TYPE>(start, length);
        sc_assert((value & ~field_mask) == 0);
        TYPE mask = static_cast<TYPE>(field_mask << start);
        m_reg &= static_cast<TYPE>(~mask);
        m_reg |= static_cast<TYPE>((value & field_mask) << start);
    }
    operator TYPE()
    {
        TYPE field_mask = gs_mask_for_field<TYPE>(start, length);
        return static_cast<TYPE>((m_reg >> start) & field_mask);
    }
};

/**
 * @brief fields within registered encapsulated using a bitfield proxy.
 * This field is constructed from a specific bit field in a specific register. The bitfield itself may be re-used to use
 * the same field in another register.
 */
template <class TYPE>
class gs_field
{
    SCP_LOGGER();
    std::string m_name;
    uint32_t m_bit_start;
    uint32_t m_bit_length;
    std::optional<gs_bitfield<TYPE>> m_bitfield;

    void require_bound() const
    {
        if (!m_bitfield) {
            SCP_FATAL(())("Field {} is metadata-only and is not attached to a scalar register", m_name);
        }
    }

public:
    /**
     * Construct a field for either a scalar register or a register bank.
     * For a bank, the register argument keeps declaration and initialization
     * uniform; the bank element supplies the actual storage on access.
     */
    template <class REG>
    gs_field(REG& reg, std::string name, uint32_t bit_start = 0, uint32_t bit_length = sizeof(TYPE) * 8)
        : m_name(std::move(name)), m_bit_start(bit_start), m_bit_length(bit_length), m_bitfield(std::nullopt)
    {
        using REG_TYPE = std::remove_reference_t<REG>;
        static_assert(std::is_same<REG_TYPE, gs_register<TYPE>>::value ||
                          std::is_same<REG_TYPE, gs_register<gs_bank_of<TYPE>>>::value,
                      "gs_field must be constructed with gs_register<T> or gs_register<gs_bank_of<T>>");
        SCP_TRACE(())("gs_field constructor");
        if (m_bit_length == 0) SCP_FATAL(())("Can't find bit length for {}", m_name);
        if constexpr (std::is_same<REG_TYPE, gs_register<TYPE>>::value) {
            m_bitfield.emplace(reg, m_bit_start, m_bit_length);
        }
    }

    void operator=(TYPE value)
    {
        require_bound();
        *m_bitfield = value;
    }
    void operator=(gs_field<TYPE>& value)
    {
        require_bound();
        *m_bitfield = static_cast<TYPE>(value);
    }

    operator TYPE()
    {
        require_bound();
        return static_cast<TYPE>(*m_bitfield);
    }

    operator gs::gs_bitfield<TYPE>&()
    {
        require_bound();
        return *m_bitfield;
    }

    uint32_t bit_start() const { return m_bit_start; }
    uint32_t bit_length() const { return m_bit_length; }
};

template <class TYPE>
class gs_register_element
{
    TYPE& m_ref;

    class field_proxy
    {
        TYPE& m_ref;
        uint32_t m_start, m_length;

    public:
        field_proxy(TYPE& ref, uint32_t start, uint32_t length): m_ref(ref), m_start(start), m_length(length) {}
        operator TYPE() const
        {
            TYPE field_mask = gs_mask_for_field<TYPE>(m_start, m_length);
            return static_cast<TYPE>((m_ref >> m_start) & field_mask);
        }
        field_proxy& operator=(TYPE value)
        {
            TYPE field_mask = gs_mask_for_field<TYPE>(m_start, m_length);
            TYPE mask = static_cast<TYPE>(field_mask << m_start);
            m_ref = static_cast<TYPE>((m_ref & ~mask) | ((value & field_mask) << m_start));
            return *this;
        }
    };

public:
    gs_register_element(TYPE& ref): m_ref(ref) {}
    operator TYPE&() { return m_ref; }
    operator TYPE() const { return m_ref; }
    TYPE* operator&() { return &m_ref; }
    gs_register_element& operator=(TYPE val)
    {
        m_ref = val;
        return *this;
    }
    field_proxy operator[](gs_field<TYPE>& f) { return field_proxy(m_ref, f.bit_start(), f.bit_length()); }
};

template <class TYPE>
gs_register_element<TYPE> gs_register<TYPE>::operator[](int idx)
{
    return gs_register_element<TYPE>(proxy_data_array<TYPE>::operator[](idx));
}

/**
 * Cursor into a gs_register<gs_bank_of<E>>. Either a partially-resolved slice
 * (narrows via operator[](size_t)) or a fully-resolved element (assign / read
 * / RMW / [gs_field]). is_full() reports which.
 */
template <class ELEM>
class gs_register_bank_element
{
    SCP_LOGGER();
    proxy_data_array<ELEM>& m_bank;
    const std::vector<uint64_t>* m_strides;
    const std::vector<uint64_t>* m_counts;
    uint64_t m_byte_off;
    size_t m_next_dim;

    class bit_proxy
    {
        gs_register_bank_element& m_elem;
        uint32_t m_start, m_length;

    public:
        bit_proxy(gs_register_bank_element& e, uint32_t s, uint32_t l): m_elem(e), m_start(s), m_length(l) {}
        operator ELEM() const
        {
            ELEM field_mask = gs_mask_for_field<ELEM>(m_start, m_length);
            return static_cast<ELEM>((static_cast<ELEM>(m_elem) >> m_start) & field_mask);
        }
        bit_proxy& operator=(ELEM value)
        {
            ELEM cur = m_elem;
            ELEM field_mask = gs_mask_for_field<ELEM>(m_start, m_length);
            ELEM mask = static_cast<ELEM>(field_mask << m_start);
            m_elem = static_cast<ELEM>((cur & ~mask) | ((value & field_mask) << m_start));
            return *this;
        }
    };

    void require_full(const char* op) const
    {
        if (!is_full()) {
            SCP_FATAL(())
            ("operation '{}' requires a fully-resolved bank element; {} of {} dims specified", op, m_next_dim,
             m_strides->size());
        }
    }

public:
    gs_register_bank_element(proxy_data_array<ELEM>& bank, const std::vector<uint64_t>& strides,
                             const std::vector<uint64_t>& counts, uint64_t byte_off, size_t next_dim)
        : m_bank(bank), m_strides(&strides), m_counts(&counts), m_byte_off(byte_off), m_next_dim(next_dim)
    {
    }

    bool is_full() const { return m_next_dim >= m_strides->size(); }
    uint64_t byte_offset() const { return m_byte_off; }
    size_t dims_resolved() const { return m_next_dim; }
    size_t dims_total() const { return m_strides->size(); }

    gs_register_bank_element operator[](size_t idx)
    {
        if (is_full()) {
            SCP_FATAL(())("operator[](size_t) on fully-resolved bank element ({} dims already consumed)", m_next_dim);
        }
        if (idx >= (*m_counts)[m_next_dim]) {
            SCP_FATAL(())("bank index {} at dim {} exceeds count {}", idx, m_next_dim, (*m_counts)[m_next_dim]);
        }
        return gs_register_bank_element(m_bank, *m_strides, *m_counts, m_byte_off + idx * (*m_strides)[m_next_dim],
                                        m_next_dim + 1);
    }

    operator ELEM() const
    {
        require_full("read");
        ELEM v;
        const_cast<proxy_data_array<ELEM>&>(m_bank).get_bytes(&v, m_byte_off);
        return v;
    }

    gs_register_bank_element& operator=(ELEM value)
    {
        require_full("assign");
        m_bank.set_bytes(&value, m_byte_off);
        return *this;
    }
    gs_register_bank_element& operator=(const gs_register_bank_element& other)
    {
        require_full("assign");
        ELEM v = static_cast<ELEM>(other);
        m_bank.set_bytes(&v, m_byte_off);
        return *this;
    }

#define GS_BANK_ELEM_RMW(op, name)                     \
    gs_register_bank_element& operator op##=(ELEM rhs) \
    {                                                  \
        require_full(name);                            \
        ELEM v = static_cast<ELEM>(*this) op rhs;      \
        m_bank.set_bytes(&v, m_byte_off);              \
        return *this;                                  \
    }
    GS_BANK_ELEM_RMW(+, "+=")
    GS_BANK_ELEM_RMW(-, "-=")
    GS_BANK_ELEM_RMW(*, "*=")
    GS_BANK_ELEM_RMW(&, "&=")
    GS_BANK_ELEM_RMW(|, "|=")
    GS_BANK_ELEM_RMW(^, "^=")
    GS_BANK_ELEM_RMW(<<, "<<=")
    GS_BANK_ELEM_RMW(>>, ">>=")
#undef GS_BANK_ELEM_RMW

    gs_register_bank_element& operator/=(ELEM rhs)
    {
        require_full("/=");
        if (rhs == 0) SCP_FATAL(())("division by zero on bank element");
        ELEM v = static_cast<ELEM>(*this) / rhs;
        m_bank.set_bytes(&v, m_byte_off);
        return *this;
    }

    /** Bit-field access on a fully-resolved element: bank[i][m][n][field] = v; */
    bit_proxy operator[](gs_field<ELEM>& f)
    {
        require_full("[gs_field]");
        return bit_proxy(*this, f.bit_start(), f.bit_length());
    }
};

/**
 * gs_register specialisation for a register bank. Covers a strided N-D layout
 * as a single target socket; every element access routes through one
 * gs_register instance.
 *   addr(i0..iD-1) = base + sum_k(i_k * strides[k])
 * Outer dim first; strides must be non-overlapping (checked at construction).
 */
template <class ELEM>
class gs_register<gs_bank_of<ELEM>> : public port_fnct, public proxy_data_array<ELEM>
{
    SCP_LOGGER((), "register");

    std::string m_regname;
    std::string m_path;
    std::vector<unsigned char> captured_txn_data;
    std::unordered_map<uint64_t, ELEM> m_element_masks;

    std::vector<uint64_t> m_dim_counts;
    std::vector<uint64_t> m_dim_strides;
    // m_row_major_div[k] = prod(counts[k+1..]); used to unravel flat indices.
    std::vector<uint64_t> m_row_major_div;
    uint64_t m_total_count = 0;
    uint64_t m_total_span = 0;

    static uint64_t compute_total_span(const std::vector<uint64_t>& counts, const std::vector<uint64_t>& strides)
    {
        if (counts.empty() || counts.size() != strides.size()) return 0;
        uint64_t span = 0;
        for (size_t k = 0; k < counts.size(); k++) {
            if (counts[k] == 0) return 0;
            span += (counts[k] - 1) * strides[k];
        }
        return span + sizeof(ELEM);
    }

    static uint64_t compute_total_count(const std::vector<uint64_t>& counts)
    {
        uint64_t c = 1;
        for (auto v : counts) c *= v;
        return c;
    }

    uint64_t flat_index_for_indices(const uint64_t* indices, size_t n) const
    {
        if (n != m_dim_counts.size())
            SCP_FATAL(())("bank {} indexed with {} dims, expected {}", m_regname, n, m_dim_counts.size());
        uint64_t flat = 0;
        for (size_t k = 0; k < n; k++) {
            if (indices[k] >= m_dim_counts[k])
                SCP_FATAL(())("bank {} index {} at dim {} exceeds count {}", m_regname, indices[k], k, m_dim_counts[k]);
            flat += indices[k] * m_row_major_div[k];
        }
        return flat;
    }

    bool try_flat_index_for_byte_offset(uint64_t byte_offset, uint64_t& flat) const
    {
        uint64_t remaining = byte_offset;
        flat = 0;
        for (size_t k = 0; k < m_dim_strides.size(); k++) {
            uint64_t idx = remaining / m_dim_strides[k];
            remaining %= m_dim_strides[k];
            if (idx >= m_dim_counts[k]) return false;
            flat += idx * m_row_major_div[k];
        }
        return remaining < sizeof(ELEM);
    }

    // Comma-separated decimal for CCI transport; reg_router parses via std::stoull.
    static std::string encode_vec(const std::vector<uint64_t>& v)
    {
        std::string out;
        for (size_t i = 0; i < v.size(); i++) {
            if (i) out += ',';
            out += std::to_string(v[i]);
        }
        return out;
    }

    void init_common(const std::string& _name, const std::string& path)
    {
        std::string n(name());
        SCP_LOGGER_NAME().features[0] = parent(n) + "." + _name;
        n = n.substr(0, n.length() - strlen("_target_socket"));

        m_total_count = compute_total_count(m_dim_counts);
        m_total_span = compute_total_span(m_dim_counts, m_dim_strides);

        m_row_major_div.assign(m_dim_counts.size(), 1);
        for (size_t k = m_dim_counts.size(); k-- > 1;) m_row_major_div[k - 1] = m_row_major_div[k] * m_dim_counts[k];

        // Each outer stride must cover the span of all inner dims; otherwise the
        // router's claim() cannot uniquely invert an address into per-dim indices.
        for (size_t k = 0; k + 1 < m_dim_strides.size(); k++) {
            uint64_t inner_span = 0;
            for (size_t j = k + 1; j < m_dim_strides.size(); j++)
                inner_span += (m_dim_counts[j] - 1) * m_dim_strides[j];
            inner_span += sizeof(ELEM);
            if (m_dim_strides[k] < inner_span) {
                SCP_FATAL(())
                ("bank {} dim {} stride 0x{:x} < inner span 0x{:x}: layout is ambiguous", _name, k, m_dim_strides[k],
                 inner_span);
            }
        }
        if (!m_dim_strides.empty() && m_dim_strides.back() < sizeof(ELEM)) {
            SCP_FATAL(())
            ("bank {} innermost stride 0x{:x} < sizeof(elem)={}", _name, m_dim_strides.back(), (uint64_t)sizeof(ELEM));
        }

        proxy_data_array<ELEM>::p_size = m_total_span;

        SCP_TRACE((), n)
        ("bank constructor : {} dims={} total_count={} span=0x{:x} in {}", _name, m_dim_counts.size(), m_total_count,
         m_total_span, path);

        register_transport_dbg_func([&](tlm::tlm_generic_payload& txn) -> unsigned int {
            if (txn.get_data_length() < sizeof(ELEM)) return 0u;
            uint64_t addr = txn.get_address();
            uint64_t byte_off = proxy_data_array<ELEM>::p_relative_addresses.get_value()
                                    ? addr
                                    : addr - proxy_data_array<ELEM>::p_offset.get_value();
            if (!byte_offset_is_element(byte_off)) return 0u;
            unsigned char* data = txn.get_data_ptr();
            if (txn.get_command() == tlm::TLM_READ_COMMAND) {
                ELEM tmp;
                proxy_data_array<ELEM>::get_bytes(&tmp, byte_off);
                memset(data, 0, txn.get_data_length());
                memcpy(data, &tmp, sizeof(ELEM));
                return static_cast<unsigned int>(sizeof(ELEM));
            }
            if (txn.get_command() == tlm::TLM_WRITE_COMMAND) {
                ELEM tmp;
                memcpy(&tmp, data, sizeof(ELEM));
                proxy_data_array<ELEM>::set_bytes(&tmp, byte_off);
                return static_cast<unsigned int>(sizeof(ELEM));
            }
            return 0u;
        });
    }

public:
    gs_register() = delete;

    /** 1D constructor. */
    gs_register(std::string _name, std::string path, uint64_t offset, uint64_t count, uint64_t stride = sizeof(ELEM),
                ELEM mask = gs_full_mask<ELEM>())
        : m_regname(_name)
        , m_path(path)
        , port_fnct(_name, path)
        , proxy_data_array<ELEM>(SCP_LOGGER_NAME(), _name, path, offset, count, mask, stride,
                                 (count == 0 ? 0 : stride * (count - 1) + sizeof(ELEM)))
    {
        m_dim_counts = { count };
        m_dim_strides = { stride };
        init_common(_name, path);
    }

    /** N-D constructor. strides[0] is outermost; strides.back() innermost. */
    gs_register(std::string _name, std::string path, uint64_t offset, std::vector<uint64_t> counts,
                std::vector<uint64_t> strides, ELEM mask = gs_full_mask<ELEM>())
        : m_regname(_name)
        , m_path(path)
        , port_fnct(_name, path)
        , proxy_data_array<ELEM>(SCP_LOGGER_NAME(), _name, path, offset, compute_total_count(counts), mask,
                                 (strides.empty() ? sizeof(ELEM) : strides.back()), compute_total_span(counts, strides))
    {
        if (counts.size() != strides.size() || counts.empty())
            SCP_FATAL(())("bank {} constructor: counts and strides must have equal, non-zero size", _name);
        for (uint64_t count : counts) {
            if (count == 0) SCP_FATAL(())("bank {} constructor: dimension counts must be non-zero", _name);
        }
        m_dim_counts = std::move(counts);
        m_dim_strides = std::move(strides);
        init_common(_name, path);
    }

    gs_register_bank_element<ELEM> operator[](size_t i)
    {
        if (i >= m_dim_counts[0])
            SCP_FATAL(())("bank {} dim 0 index {} exceeds count {}", m_regname, i, m_dim_counts[0]);
        return gs_register_bank_element<ELEM>(*this, m_dim_strides, m_dim_counts, i * m_dim_strides[0], 1);
    }

    template <class... Idx, typename = std::enable_if_t<(sizeof...(Idx) >= 1) && (std::is_integral<Idx>::value && ...)>>
    gs_register_bank_element<ELEM> operator()(Idx... indices)
    {
        const uint64_t idx_arr[] = { static_cast<uint64_t>(indices)... };
        return build_cursor(idx_arr, sizeof...(Idx));
    }

    gs_register_bank_element<ELEM> at(std::initializer_list<uint64_t> indices)
    {
        return build_cursor(indices.begin(), indices.size());
    }
    gs_register_bank_element<ELEM> at(const std::vector<uint64_t>& indices)
    {
        return build_cursor(indices.data(), indices.size());
    }

private:
    gs_register_bank_element<ELEM> build_cursor(const uint64_t* indices, size_t n)
    {
        if (n == 0 || n > m_dim_strides.size())
            SCP_FATAL(())("bank {} indexed with {} dims, expected 1..{}", m_regname, n, m_dim_strides.size());
        uint64_t off = 0;
        for (size_t k = 0; k < n; k++) {
            if (indices[k] >= m_dim_counts[k])
                SCP_FATAL(())("bank {} index {} at dim {} exceeds count {}", m_regname, indices[k], k, m_dim_counts[k]);
            off += indices[k] * m_dim_strides[k];
        }
        return gs_register_bank_element<ELEM>(*this, m_dim_strides, m_dim_counts, off, n);
    }

public:
    // Bulk access by flat row-major index. For a 1D bank this uses the fast
    // contiguous path when stride == sizeof(ELEM); otherwise it loops per
    // element via the byte-offset path.
    void get(ELEM* dst, uint64_t idx, uint64_t length)
    {
        if (m_dim_counts.size() == 1) {
            proxy_data_array<ELEM>::get(dst, idx, length);
        } else {
            for (uint64_t i = 0; i < length; i++)
                proxy_data_array<ELEM>::get_bytes(&dst[i], byte_offset_for_flat(idx + i));
        }
    }
    void set(ELEM* src, uint64_t idx, uint64_t length, bool use_mask = true)
    {
        if (m_dim_counts.size() == 1 && m_element_masks.empty()) {
            proxy_data_array<ELEM>::set(src, idx, length, use_mask);
        } else {
            for (uint64_t i = 0; i < length; i++)
                proxy_data_array<ELEM>::set_bytes(&src[i], byte_offset_for_flat(idx + i), use_mask);
        }
    }

    std::string get_regname() const { return m_regname; }
    std::string get_path() const { return m_path; }
    uint64_t get_offset() const { return proxy_data_array<ELEM>::p_offset.get_value(); }
    uint64_t get_size() const { return proxy_data_array<ELEM>::p_size.get_value(); }
    uint64_t get_total_count() const { return m_total_count; }
    size_t get_dims() const { return m_dim_counts.size(); }
    uint64_t get_dim_count(size_t k) const { return m_dim_counts.at(k); }
    uint64_t get_dim_stride(size_t k) const { return m_dim_strides.at(k); }
    const std::vector<uint64_t>& get_dim_counts() const { return m_dim_counts; }
    const std::vector<uint64_t>& get_dim_strides() const { return m_dim_strides; }
    ELEM get_mask() const { return proxy_data_array<ELEM>::p_mask.get_value(); }
    void set_mask(ELEM mask) { proxy_data_array<ELEM>::p_mask = mask; }

    void set_element_mask(uint64_t flat_index, ELEM mask)
    {
        if (flat_index >= m_total_count)
            SCP_FATAL(())("bank {} flat index {} exceeds total count {}", m_regname, flat_index, m_total_count);
        m_element_masks[flat_index] = mask;
    }

    ELEM get_element_mask(uint64_t flat_index) const
    {
        if (flat_index >= m_total_count)
            SCP_FATAL(())("bank {} flat index {} exceeds total count {}", m_regname, flat_index, m_total_count);
        return mask_for_element(flat_index);
    }

    void clear_element_mask(uint64_t flat_index)
    {
        if (flat_index >= m_total_count)
            SCP_FATAL(())("bank {} flat index {} exceeds total count {}", m_regname, flat_index, m_total_count);
        m_element_masks.erase(flat_index);
    }

    void set_element_mask(std::initializer_list<uint64_t> indices, ELEM mask)
    {
        set_element_mask(flat_index_for_indices(indices.begin(), indices.size()), mask);
    }

    ELEM get_element_mask(std::initializer_list<uint64_t> indices) const
    {
        return get_element_mask(flat_index_for_indices(indices.begin(), indices.size()));
    }

    void clear_element_mask(std::initializer_list<uint64_t> indices)
    {
        clear_element_mask(flat_index_for_indices(indices.begin(), indices.size()));
    }

    void set_element_mask(const std::vector<uint64_t>& indices, ELEM mask)
    {
        set_element_mask(flat_index_for_indices(indices.data(), indices.size()), mask);
    }

    ELEM get_element_mask(const std::vector<uint64_t>& indices) const
    {
        return get_element_mask(flat_index_for_indices(indices.data(), indices.size()));
    }

    void clear_element_mask(const std::vector<uint64_t>& indices)
    {
        clear_element_mask(flat_index_for_indices(indices.data(), indices.size()));
    }

protected:
    bool has_element_masks() const override { return !m_element_masks.empty(); }

    ELEM mask_for_element(uint64_t flat_index) const override
    {
        auto it = m_element_masks.find(flat_index);
        return it == m_element_masks.end() ? proxy_data_array<ELEM>::p_mask.get_value() : it->second;
    }

    ELEM mask_for_byte_offset(uint64_t byte_offset) const override
    {
        uint64_t flat_index = 0;
        if (!try_flat_index_for_byte_offset(byte_offset, flat_index)) return proxy_data_array<ELEM>::p_mask.get_value();
        return mask_for_element(flat_index);
    }

public:
    uint64_t byte_offset_for(const uint64_t* indices, size_t n) const
    {
        if (n != m_dim_strides.size())
            SCP_FATAL(())("bank {} indexed with {} dims, expected {}", m_regname, n, m_dim_strides.size());
        uint64_t off = 0;
        for (size_t k = 0; k < n; k++) {
            if (indices[k] >= m_dim_counts[k])
                SCP_FATAL(())("bank {} index {} at dim {} exceeds count {}", m_regname, indices[k], k, m_dim_counts[k]);
            off += indices[k] * m_dim_strides[k];
        }
        return off;
    }

    uint64_t byte_offset_for_flat(uint64_t flat_idx) const
    {
        if (flat_idx >= m_total_count)
            SCP_FATAL(())("bank {} flat index {} exceeds total count {}", m_regname, flat_idx, m_total_count);
        uint64_t off = 0;
        uint64_t remaining = flat_idx;
        for (size_t k = 0; k < m_dim_counts.size(); k++) {
            uint64_t idx_k = remaining / m_row_major_div[k];
            remaining = remaining % m_row_major_div[k];
            off += idx_k * m_dim_strides[k];
        }
        return off;
    }

    bool is_element_start(const gs_bank_access& access) const
    {
        return access && byte_offset_for_flat(access.index) == access.byte_offset;
    }

    uint64_t masked_transaction_element_count(const tlm::tlm_generic_payload& txn, const gs_bank_access& access) const
    {
        if (!is_element_start(access)) return 0;
        uint64_t length = txn.get_data_length();
        if (length == 0 || length % sizeof(ELEM) != 0) return 0;

        uint64_t count = length / sizeof(ELEM);
        if (count == 1) return count;
        if (m_dim_counts.size() != 1 || m_dim_strides[0] != sizeof(ELEM)) return 0;
        if (access.index >= m_total_count || count > m_total_count - access.index) return 0;
        return count;
    }

    bool byte_offset_is_element(uint64_t byte_off) const
    {
        uint64_t r = byte_off;
        for (size_t k = 0; k < m_dim_strides.size(); k++) {
            uint64_t idx = r / m_dim_strides[k];
            if (idx >= m_dim_counts[k]) return false;
            r = r % m_dim_strides[k];
        }
        return r < sizeof(ELEM);
    }

    /** Decode a transaction's address into per-dim indices; bool == on-element. */
    gs_bank_access decode_access(const tlm::tlm_generic_payload& txn) const
    {
        gs_bank_access r{};
        const uint64_t base = proxy_data_array<ELEM>::p_offset.get_value();
        const bool relative = proxy_data_array<ELEM>::p_relative_addresses.get_value();
        uint64_t addr = txn.get_address();
        r.byte_offset = relative ? addr : (addr - base);

        r.indices.assign(m_dim_strides.size(), 0);
        uint64_t rem = r.byte_offset;
        r.in_range = true;
        for (size_t k = 0; k < m_dim_strides.size(); k++) {
            uint64_t idx_k = rem / m_dim_strides[k];
            rem = rem % m_dim_strides[k];
            r.indices[k] = idx_k;
            if (idx_k >= m_dim_counts[k]) r.in_range = false;
        }
        r.aligned = rem < sizeof(ELEM);

        uint64_t flat = 0;
        for (size_t k = 0; k < m_dim_strides.size(); k++) flat += r.indices[k] * m_row_major_div[k];
        r.index = flat;
        return r;
    }

    void capture_txn_pre(tlm::tlm_generic_payload& txn) override
    {
        if ((!has_element_masks() && proxy_data_array<ELEM>::p_mask.get_value() == gs_full_mask<ELEM>()) ||
            (txn.get_command() != tlm::tlm_command::TLM_WRITE_COMMAND)) {
            return;
        }
        auto a = decode_access(txn);
        uint64_t count = masked_transaction_element_count(txn, a);
        if (count == 0) {
            captured_txn_data.clear();
            txn.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        captured_txn_data.resize(count * sizeof(ELEM));
        for (uint64_t i = 0; i < count; i++) {
            proxy_data_array<ELEM>::get_bytes(reinterpret_cast<ELEM*>(captured_txn_data.data()) + i,
                                              byte_offset_for_flat(a.index + i));
        }
    }

    void handle_mask_post(tlm::tlm_generic_payload& txn) override
    {
        if ((!has_element_masks() && proxy_data_array<ELEM>::p_mask.get_value() == gs_full_mask<ELEM>()) ||
            (txn.get_command() != tlm::tlm_command::TLM_WRITE_COMMAND)) {
            return;
        }
        auto a = decode_access(txn);
        uint64_t count = masked_transaction_element_count(txn, a);
        if (count == 0 || captured_txn_data.size() < count * sizeof(ELEM)) {
            txn.set_response_status(tlm::TLM_BURST_ERROR_RESPONSE);
            return;
        }
        unsigned char* txn_data_ptr = txn.get_data_ptr();
        for (uint64_t i = 0; i < count; i++) {
            ELEM incoming;
            ELEM stored;
            memcpy(&incoming, txn_data_ptr + i * sizeof(ELEM), sizeof(ELEM));
            memcpy(&stored, captured_txn_data.data() + i * sizeof(ELEM), sizeof(ELEM));
            proxy_data_array<ELEM>::write_with_mask(&incoming, &stored, 1, get_element_mask(a.index + i));
            proxy_data_array<ELEM>::set_bytes(&stored, byte_offset_for_flat(a.index + i), false);
            memcpy(txn_data_ptr + i * sizeof(ELEM), &stored, sizeof(ELEM));
        }
    }

protected:
    using port_fnct::parent;
};

} // namespace gs

#endif // GS_REGISTERS_H
