/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _QBOX_ELF_LOADER_H
#define _QBOX_ELF_LOADER_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gs {

/**
 * @brief ELF file loader.
 *
 * Reads an ELF binary and invokes a callback for each loadable segment.
 * The callback receives (physical_address, data_pointer, length).
 */
class elf_loader
{
public:
    using send_fn = std::function<void(uint64_t, uint8_t*, uint64_t)>;

    enum endianess {
        ENDIAN_UNKNOWN = 0,
        ENDIAN_LITTLE = 1,
        ENDIAN_BIG = 2,
    };

    struct segment {
        uint64_t virt;
        uint64_t phys;
        uint64_t size;
        uint64_t filesz;
        uint64_t offset;
        bool r, w, x;
    };

    /**
     * @brief Load an ELF file and send segments via the callback.
     */
    elf_loader(const std::string& path, send_fn send);
    ~elf_loader();

    elf_loader(const elf_loader&) = delete;
    elf_loader& operator=(const elf_loader&) = delete;

    uint64_t entry() const { return m_entry; }
    uint64_t machine() const { return m_machine; }
    endianess endian() const { return m_endian; }
    const std::vector<segment>& segments() const { return m_segments; }

private:
    std::vector<segment> m_segments;
    send_fn m_send;
    std::string m_filename;
    int m_fd;
    uint64_t m_entry;
    uint64_t m_machine;
    endianess m_endian;

    uint64_t read_segment(const segment& seg);
    uint64_t to_phys(uint64_t virt) const;
};

} // namespace gs

#endif
