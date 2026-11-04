/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
#include <array>
#include <type_traits>

struct byte_view {
    const uint8_t* payload = nullptr;
    std::size_t len = 0;
};

class read_cursor
{
public:
    read_cursor() {}

    read_cursor(std::shared_ptr<const std::vector<std::uint8_t>> data)
        : data_(std::move(data)), cur_(data_->data()), end_(data_->data() + data_->size())
    {
    }

    template <typename T, bool LE = true>
    T read()
    {
        if (remaining() < sizeof(T)) {
            throw std::invalid_argument("sizeof read > remaning size");
        }

        auto value = read_host_endianness<T>();

        if constexpr (LE) {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
            value = byteswap(value);
#endif
        } else {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            value = byteswap(value);
#endif
        }

        return value;
    }

    void read_bytes(byte_view& dst, std::size_t nb_bytes)
    {
        if (remaining() < nb_bytes) {
            throw std::invalid_argument("nb_bytes > remaning size");
        }

        dst.payload = cur_;
        dst.len = nb_bytes;

        cur_ += nb_bytes;
    }

    template <std::size_t N>
    void read_bytes(std::array<std::uint8_t, N>& dst)
    {
        if (remaining() < dst.size()) {
            throw std::invalid_argument("array size > remaning size");
        }

        std::memcpy(dst.data(), cur_, dst.size());

        cur_ += dst.size();
    }

    template <typename T>
    T peek() const
    {
        if (remaining() < sizeof(T)) {
            throw std::invalid_argument("sizeof(T) > remaning size");
        }

        T value;
        std::memcpy(&value, cur_, sizeof(T));
        return value;
    }

    const std::uint8_t* skip_bytes(std::size_t nb_bytes)
    {
        if (remaining() < nb_bytes) {
            throw std::invalid_argument("nb_bytes > remaning size");
        }

        auto* cur = cur_;
        cur_ += nb_bytes;
        return cur;
    }

    std::size_t remaining() const { return static_cast<std::size_t>(end_ - cur_); }

    const std::uint8_t* pos() const { return cur_; }

private:
    template <typename T>
    T byteswap(T value)
    {
        static_assert(std::is_integral_v<T>);

        if constexpr (sizeof(T) == 1) {
            return value;
        } else if constexpr (sizeof(T) == 2) {
            return static_cast<T>(__builtin_bswap16(value));
        } else if constexpr (sizeof(T) == 4) {
            return static_cast<T>(__builtin_bswap32(value));
        } else if constexpr (sizeof(T) == 8) {
            return static_cast<T>(__builtin_bswap64(value));
        } else {
            static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);
        }
    }

    template <typename T>
    T read_host_endianness()
    {
        if (remaining() < sizeof(T)) {
            throw std::invalid_argument("sizeof read > remaning size");
        }

        T value;
        std::memcpy(&value, cur_, sizeof(T));
        cur_ += sizeof(T);
        return value;
    }

    std::shared_ptr<const std::vector<uint8_t>> data_ = nullptr;
    const std::uint8_t* cur_ = nullptr;
    const std::uint8_t* end_ = nullptr;
};