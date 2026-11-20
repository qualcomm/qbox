/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace gs {

/* useful definitions */
inline constexpr std::uint64_t KiB = 1ULL << 10;
inline constexpr std::uint64_t MiB = 1ULL << 20;
inline constexpr std::uint64_t GiB = 1ULL << 30;
inline constexpr std::uint64_t TiB = 1ULL << 40;

/* Generic wrapper class for dwords / qwords */
template <typename Unit>
class unit_offset
{
    static_assert(std::is_integral_v<Unit> || std::is_enum_v<Unit>, "Unit must be an integral or enum type");

public:
    using unit_type = Unit;
    using units_type = std::uint64_t;

    static constexpr std::size_t unit_size = sizeof(unit_type);

    explicit constexpr unit_offset(units_type nb_units) noexcept: units_(nb_units) {}

    [[nodiscard]] static constexpr std::optional<unit_offset> from_bytes(units_type bytes) noexcept
    {
        if ((bytes % unit_size) != 0) {
            return std::nullopt;
        }

        return unit_offset{ bytes / unit_size };
    }

    [[nodiscard]] constexpr units_type units() const noexcept { return units_; }

    [[nodiscard]] constexpr units_type bytes() const noexcept { return units_ * unit_size; }

    friend constexpr bool operator==(unit_offset lhs, unit_offset rhs) noexcept { return lhs.units_ == rhs.units_; }

    friend constexpr bool operator!=(unit_offset lhs, unit_offset rhs) noexcept { return !(lhs == rhs); }

    friend constexpr bool operator<(unit_offset lhs, unit_offset rhs) noexcept { return lhs.units_ < rhs.units_; }

    friend constexpr bool operator<=(unit_offset lhs, unit_offset rhs) noexcept { return lhs.units_ <= rhs.units_; }

    friend constexpr bool operator>(unit_offset lhs, unit_offset rhs) noexcept { return lhs.units_ > rhs.units_; }

    friend constexpr bool operator>=(unit_offset lhs, unit_offset rhs) noexcept { return lhs.units_ >= rhs.units_; }

    friend constexpr unit_offset operator+(unit_offset lhs, unit_offset rhs) noexcept
    {
        return unit_offset{ lhs.units_ + rhs.units_ };
    }

    /* precondition: lhs >= rhs */
    friend constexpr unit_offset operator-(unit_offset lhs, unit_offset rhs)
    {
        if (lhs < rhs) {
            throw std::invalid_argument("lhs must be >= rhs");
        }

        return unit_offset{ lhs.units_ - rhs.units_ };
    }

private:
    units_type units_ = 0;
};

/* common aliases */
using dword = unit_offset<std::uint32_t>;
using qword = unit_offset<std::uint64_t>;

} // namespace gs