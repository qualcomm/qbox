/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <elf_loader.h>

#include <cassert>
#include <cstring>
#include <fcntl.h>
#include <libelf.h>
#include <scp/report.h>
#include <unistd.h>

namespace gs {

struct elf32_traits {
    typedef Elf32_Ehdr Elf_Ehdr;
    typedef Elf32_Phdr Elf_Phdr;
    typedef Elf32_Shdr Elf_Shdr;
    typedef Elf32_Sym Elf_Sym;

    static Elf_Ehdr* elf_getehdr(Elf* elf) { return elf32_getehdr(elf); }
    static Elf_Phdr* elf_getphdr(Elf* elf) { return elf32_getphdr(elf); }
    static Elf_Shdr* elf_getshdr(Elf_Scn* scn) { return elf32_getshdr(scn); }
};

struct elf64_traits {
    typedef Elf64_Ehdr Elf_Ehdr;
    typedef Elf64_Phdr Elf_Phdr;
    typedef Elf64_Shdr Elf_Shdr;
    typedef Elf64_Sym Elf_Sym;

    static Elf_Ehdr* elf_getehdr(Elf* elf) { return elf64_getehdr(elf); }
    static Elf_Phdr* elf_getphdr(Elf* elf) { return elf64_getphdr(elf); }
    static Elf_Shdr* elf_getshdr(Elf_Scn* scn) { return elf64_getshdr(scn); }
};

static elf_loader::endianess get_endianess(Elf* elf)
{
    const char* ident = elf_getident(elf, nullptr);
    if (ident == nullptr) return elf_loader::ENDIAN_UNKNOWN;

    switch (ident[EI_DATA]) {
    case ELFDATA2LSB:
        return elf_loader::ENDIAN_LITTLE;
    case ELFDATA2MSB:
        return elf_loader::ENDIAN_BIG;
    default:
        return elf_loader::ENDIAN_UNKNOWN;
    }
}

template <typename T>
static std::vector<elf_loader::segment> get_segments(Elf* elf)
{
    size_t count = 0;
    int err = elf_getphdrnum(elf, &count);
    if (err) SCP_FATAL("elf_loader") << "elf_getphdrnum failed: " << elf_errmsg(err);

    std::vector<elf_loader::segment> segments;
    typename T::Elf_Phdr* hdr = T::elf_getphdr(elf);
    for (uint64_t i = 0; i < count; i++, hdr++) {
        if (hdr->p_type == PT_LOAD) {
            bool r = hdr->p_flags & PF_R;
            bool w = hdr->p_flags & PF_W;
            bool x = hdr->p_flags & PF_X;
            segments.push_back({ hdr->p_vaddr, hdr->p_paddr, hdr->p_memsz, hdr->p_filesz, hdr->p_offset, r, w, x });
        }
    }

    return segments;
}

template <typename T>
static void read_sections(Elf* elf)
{
    Elf_Scn* scn = nullptr;
    while ((scn = elf_nextscn(elf, scn)) != nullptr) {
        typename T::Elf_Shdr* shdr = T::elf_getshdr(scn);
        if (shdr->sh_type != SHT_SYMTAB) continue;

        Elf_Data* data = elf_getdata(scn, nullptr);
        size_t num_symbols = shdr->sh_size / shdr->sh_entsize;
        typename T::Elf_Sym* syms = (typename T::Elf_Sym*)(data->d_buf);

        for (size_t i = 0; i < num_symbols; i++) {
            if (syms[i].st_size == 0) continue;
            char* name = elf_strptr(elf, shdr->sh_link, syms[i].st_name);
            if (name == nullptr || strlen(name) == 0) continue;
        }
    }
}

uint64_t elf_loader::to_phys(uint64_t virt) const
{
    for (auto& seg : m_segments) {
        if ((virt >= seg.virt) && virt < (seg.virt + seg.size)) return seg.phys + virt - seg.virt;
    }
    return virt;
}

uint64_t elf_loader::read_segment(const segment& seg)
{
    if (m_fd < 0) SCP_FATAL("elf_loader") << "ELF file '" << m_filename << "' not open";

    if (lseek(m_fd, seg.offset, SEEK_SET) != (ssize_t)seg.offset)
        SCP_FATAL("elf_loader") << "cannot seek within ELF file " << m_filename;

    uint8_t buff[1024];
    size_t sz = seg.filesz;
    size_t offset = 0;
    while (sz) {
        int s = read(m_fd, buff, (sz < sizeof(buff) ? sz : sizeof(buff)));
        sz -= s;
        if (!s) {
            SCP_FATAL("elf_loader") << "cannot read ELF file " << m_filename;
            exit(0);
        }
        m_send(seg.phys + offset, buff, s);
        offset += s;
    }
    if (lseek(m_fd, 0, SEEK_SET) != 0) SCP_FATAL("elf_loader") << "cannot seek within ELF file " << m_filename;

    return seg.size;
}

elf_loader::elf_loader(const std::string& path, send_fn send)
    : m_send(send), m_filename(path), m_fd(-1), m_entry(0), m_machine(0), m_endian(ENDIAN_UNKNOWN)
{
    if (elf_version(EV_CURRENT) == EV_NONE) SCP_FATAL("elf_loader") << "failed to read libelf version";

#ifdef _WIN32
    m_fd = open(m_filename.c_str(), O_RDONLY | O_BINARY, 0);
#else
    m_fd = open(m_filename.c_str(), O_RDONLY, 0);
#endif
    if (m_fd < 0) SCP_FATAL("elf_loader") << "cannot open elf file " << m_filename;

    Elf* elf = elf_begin(m_fd, ELF_C_READ, nullptr);
    if (elf == nullptr) SCP_FATAL("elf_loader") << "error reading " << m_filename << " : " << elf_errmsg(-1);

    if (elf_kind(elf) != ELF_K_ELF) SCP_FATAL("elf_loader") << "ELF version error in " << m_filename;

    Elf32_Ehdr* ehdr32 = elf32_getehdr(elf);
    Elf64_Ehdr* ehdr64 = elf64_getehdr(elf);
    if (ehdr32) {
        m_entry = ehdr32->e_entry;
        m_machine = ehdr32->e_machine;
        m_endian = get_endianess(elf);
        m_segments = get_segments<elf32_traits>(elf);
        read_sections<elf32_traits>(elf);
    }

    if (ehdr64) {
        m_entry = ehdr64->e_entry;
        m_machine = ehdr64->e_machine;
        m_endian = get_endianess(elf);
        m_segments = get_segments<elf64_traits>(elf);
        read_sections<elf64_traits>(elf);
    }

    elf_end(elf);

    for (auto s : m_segments) {
        read_segment(s);
    }
}

elf_loader::~elf_loader()
{
    if (m_fd >= 0) close(m_fd);
}

} // namespace gs
