/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2022
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _GREENSOCS_BASE_COMPONENTS_LOADER_H
#define _GREENSOCS_BASE_COMPONENTS_LOADER_H

#include <fstream>

#include <cci_configuration>
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <scp/report.h>
#include <module_factory_registery.h>
#include <ports/target-signal-socket.h>
#include <tlm_sockets_buswidth.h>

#include "onmethod.h"

#include <cinttypes>
#include <fcntl.h>
#include <libelf.h>
#include <list>
#include <string>
#include <unistd.h>
#include <vector>
#include <limits>
#include <filesystem>
#include <zip.h>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#endif

#define BINFILE_READ_CHUNK_SIZE (1024 * 1024)

#ifdef _WIN32
// On Windows,  libzip subsitutes close with _close.
// We disable this behaviour that breaks calling close methods.
#undef close
#endif

namespace gs {

/**
 * @class Loader
 *
 * @brief A loader that cope with several formats
 *
 * @details
 */

/* simple class to load csv file*/
class CSVRow
{
public:
    std::string operator[](std::size_t index) const
    {
        return std::string(&m_line[m_data[index] + 1], m_data[index + 1] - (m_data[index] + 1));
    }
    std::size_t size() const { return m_data.size() - 1; }
    void readNextRow(std::istream& str)
    {
        std::getline(str, m_line);

        m_data.clear();
        m_data.emplace_back(-1);
        std::string::size_type pos = 0;
        while ((pos = m_line.find(',', pos)) != std::string::npos) {
            m_data.emplace_back(pos);
            ++pos;
        }
        // This checks for a trailing comma with no data after it.
        pos = m_line.size();
        m_data.emplace_back(pos);
    }

private:
    std::string m_line;
    std::vector<int> m_data;
};

static std::istream& operator>>(std::istream& str, CSVRow& data)
{
    data.readNextRow(str);
    return str;
}

template <unsigned int BUSWIDTH = DEFAULT_TLM_BUSWIDTH>
class loader : public sc_core::sc_module
{
    SCP_LOGGER();
    cci::cci_broker_handle m_broker;

    template <typename MODULE, typename TYPES = tlm::tlm_base_protocol_types>
    class simple_initiator_socket_zero
        : public tlm_utils::simple_initiator_socket_b<MODULE, BUSWIDTH, TYPES, sc_core::SC_ZERO_OR_MORE_BOUND>
    {
        using typename tlm_utils::simple_initiator_socket_b<MODULE, BUSWIDTH, TYPES,
                                                            sc_core::SC_ZERO_OR_MORE_BOUND>::base_target_socket_type;
        typedef tlm_utils::simple_initiator_socket_b<MODULE, BUSWIDTH, TYPES, sc_core::SC_ZERO_OR_MORE_BOUND> socket_b;

    public:
        simple_initiator_socket_zero(const char* name): socket_b(name) {}
    };

public:
    simple_initiator_socket_zero<loader<BUSWIDTH>> initiator_socket;
    TargetSignalSocket<bool> reset;
    onMethodHelper m_onMethod;

private:
    uint64_t m_address = 0;

    std::function<void(const uint8_t* data, uint64_t offset, uint64_t len)> write_cb;
    bool m_use_callback = false;

    std::list<std::string> sc_cci_children(sc_core::sc_module_name name)
    {
        std::list<std::string> children;
        int l = strlen(name) + 1;
        auto uncon = m_broker.get_unconsumed_preset_values([&name](const std::pair<std::string, cci::cci_value>& iv) {
            return iv.first.find(std::string(name) + ".") == 0;
        });
        for (auto p : uncon) {
            children.push_back(p.first.substr(l, p.first.find(".", l) - l));
        }
        children.sort();
        children.unique();
        return children;
    }

    void send(uint64_t addr, uint8_t* data, uint64_t len)
    {
        if (m_use_callback) {
            write_cb(data, addr, len);
        } else {
            tlm::tlm_generic_payload trans;

            trans.set_command(tlm::TLM_WRITE_COMMAND);
            trans.set_address(addr);
            trans.set_data_ptr(data);
            trans.set_data_length(len);
            trans.set_streaming_width(len);
            trans.set_byte_enable_length(0);
            if (initiator_socket->transport_dbg(trans) != len) {
                SCP_FATAL(()) << name() << " : Error loading data to memory @ "
                              << "0x" << std::hex << addr;
            }
        }
    }

    template <typename T>
    T cci_get(std::string name)
    {
        return gs::cci_get<T>(m_broker, name);
    }

    uint32_t byte_swap32(uint32_t in)
    {
        uint32_t out;
        out = in & 0xff;
        in >>= 8;
        out <<= 8;
        out |= in & 0xff;
        in >>= 8;
        out <<= 8;
        out |= in & 0xff;
        in >>= 8;
        out <<= 8;
        out |= in & 0xff;
        in >>= 8;
        return out;
    }

public:
    loader(sc_core::sc_module_name name)
        : m_broker(cci::cci_get_broker())
        , initiator_socket("initiator_socket") //, [&](std::string s) -> void { register_boundto(s); })
        , reset("reset")
    {
        SCP_TRACE(())("default constructor");
        // Respect the single function call semantics,
        // but ensure execution happens on the main SystemC thread, not on a light weight process,
        // as the loader uses a lot of stack
        reset.register_value_changed_cb([&](bool value) { m_onMethod.run([&]() { doreset(value); }); });
    }
    void doreset(bool value)
    {
        SCP_WARN(())("Reset");
        if (value) {
            end_of_elaboration();
        }
    }
    loader(sc_core::sc_module_name name, std::function<void(const uint8_t* data, uint64_t offset, uint64_t len)> _write)
        : m_broker(cci::cci_get_broker())
        , initiator_socket("initiator_socket") //, [&](std::string s) -> void { register_boundto(s); })
        , reset("reset")
        , write_cb(_write)
    {
        SCP_TRACE(())("constructor with callback");
        m_use_callback = true;
        reset.register_value_changed_cb([&](bool value) { m_onMethod.run([&]() { doreset(value); }); });
    }

    ~loader() {}

protected:
    void load(std::string name)
    {
        bool read = false;
        if (m_broker.has_preset_value(name + ".elf_file")) {
            if (m_use_callback) {
                SCP_WARN(()) << "elf file loading into local memory will interpret addresses "
                                "relative to the memory - probably not what you want?";
            }
            std::string file = gs::cci_get<std::string>(m_broker, name + ".elf_file");
            elf_load(file);
            read = true;
        } else {
            uint64_t addr = 0;
            if (m_use_callback) {
                addr = gs::cci_get<uint64_t>(m_broker, name + ".offset");
            } else {
                addr = gs::cci_get<uint64_t>(m_broker, name + ".address");
            }
            std::string file;
            if (gs::cci_get<std::string>(m_broker, name + ".bin_file", file)) {
                uint64_t file_offset = 0, file_data_len = std::numeric_limits<uint64_t>::max();
                if (!((gs::cci_get<uint64_t>(m_broker, name + ".bin_file_offset", file_offset) &&
                       gs::cci_get<uint64_t>(m_broker, name + ".bin_file_size", file_data_len)))) {
                    file_data_len = std::filesystem::file_size(std::filesystem::path(file));
                    file_offset = 0;
                }
                SCP_INFO(())("Loading binary file: {} starting at offset: {:#x} with size: {:#x} to addr: {:#x}", file,
                             file_offset, file_data_len, addr);
                file_load(file, addr, file_offset, file_data_len);
                read = true;
            }
            if (gs::cci_get<std::string>(m_broker, name + ".zip_archive", file)) {
                uint64_t file_offset = 0, file_data_len = 0;
                if (!((gs::cci_get<uint64_t>(m_broker, name + ".archived_file_offset", file_offset) &&
                       gs::cci_get<uint64_t>(m_broker, name + ".archived_file_size", file_data_len)))) {
                    // can't stat file size here, as the actual file inside archive is accessed inside zip_file_load()
                    file_data_len = 0;
                    file_offset = 0;
                }
                std::string archived_file_name;
                if (!gs::cci_get<std::string>(m_broker, name + ".archived_file_name", archived_file_name))
                    archived_file_name = "";
                SCP_INFO(()) << "Loading " << archived_file_name << " from zip file: " << file
                             << " starting at offset: " << file_offset << " to addr: " << addr;
                zip_file_load(nullptr, file, addr, archived_file_name, file_offset, file_data_len);
                read = true;
            }
            if (gs::cci_get<std::string>(m_broker, name + ".csv_file", file)) {
                std::string addr_str = gs::cci_get<std::string>(m_broker, name + ".addr_str");
                std::string val_str = gs::cci_get<std::string>(m_broker, name + ".value_str");
                bool byte_swap = false;
                byte_swap = gs::cci_get<bool>(m_broker, name + ".byte_swap", byte_swap);
                SCP_INFO(())("Loading csv file {}, ({}) to {:#x}", file, (name + ".csv_file"), addr);
                csv_load(file, addr, addr_str, val_str, byte_swap);
                read = true;
            }
            std::string param;
            if (gs::cci_get<std::string>(m_broker, name + ".param", param)) {
                cci::cci_param_typed_handle<std::string> data(m_broker.get_param_handle(param));
                if (!data.is_valid()) {
                    SCP_FATAL(()) << "Unable to find valid source param '" << param << "' for '" << name << "'";
                }
                SCP_INFO(())("Loading string parameter {} to {:#x}", param, addr);
                str_load(data.get_value(), addr);
                read = true;
            }
            if (sc_cci_children((name + ".data").c_str()).size()) {
                bool byte_swap = false;
                gs::cci_get<bool>(m_broker, name + ".byte_swap", byte_swap);
                SCP_INFO(())("Loading config data to {:#x}", addr);
                data_load(name + ".data", addr, byte_swap);
                read = true;
            }
        }
        if (!read) {
            SCP_FATAL(()) << "Unknown loader type: '" << name << "'";
        }
    }
    void end_of_elaboration()
    {
        int i = 0;
        auto children = sc_cci_children(name());
        for (std::string s : children) {
            if (std::count_if(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); })) {
                load(std::string(name()) + "." + s);
                i++;
            }
        }
        if (i == 0 && children.size() > 0) {
            load(name());
        }
    }

public:
    /**
     * @brief This function reads a file into memory and can be used to load an image.
     *
     * @param filename Name of the file
     * @param addr the address where the memory file is to be read
     * @return size_t
     */
    void file_load(std::string& filename, uint64_t addr, uint64_t file_offset = 0,
                   uint64_t file_data_len = std::numeric_limits<uint64_t>::max())
    {
        std::ifstream fin(filename, std::ios::in | std::ios::binary);

        if (!fin.good()) {
            SCP_FATAL(()) << "Memory::load(): error file not found (" << filename << ")";
        }

        uint64_t rem_len = file_data_len;
        uint64_t read_len = 0;
        fin.seekg(file_offset, std::ios::beg);

        std::vector<uint8_t> buffer(BINFILE_READ_CHUNK_SIZE);
        uint64_t c = 0;

        while (!fin.eof() && (rem_len > 0)) {
            read_len = std::min(rem_len, static_cast<uint64_t>(BINFILE_READ_CHUNK_SIZE));
            fin.read(reinterpret_cast<char*>(buffer.data()), read_len);
            size_t r = fin.gcount();
            send(addr + c, buffer.data(), r);
            c += r;
            rem_len = ((rem_len >= r) ? rem_len - r : 0);
        }

        fin.close();
    }

    /*
     - p_archive: pointer to a zip_t struct if the zip archive is opened outside this function.
     - archive_name: the name of the zip archive.
     - file_name: name of the file to be extracted from archive_name.
    */
    void zip_file_load(zip_t* p_archive, const std::string& archive_name, uint64_t addr,
                       const std::string& file_name = "", uint64_t file_offset = 0, uint64_t file_data_len = 0,
                       bool is_compressed = false, const std::string& uncompressed_file_name = "")
    {
        if (archive_name.empty()) SCP_FATAL(()) << "Missing zip archive name!";
        zip_t* z_archive = p_archive;
        if (!z_archive) {
            z_archive = zip_open(archive_name.c_str(), 0, nullptr);
            if (!z_archive) SCP_FATAL(()) << "Can't open zip archive: " << archive_name;
        }
        zip_int64_t num_entries = zip_get_num_entries(z_archive, ZIP_FL_UNCHANGED);
        if (num_entries < 0)
            SCP_FATAL(()) << "Can't get the number of the entries in zip archive: " << archive_name
                          << " may be the archive file is corrupted!";
        if (num_entries == 0) SCP_FATAL(()) << "There is no files in the zip archive: " << archive_name;
        zip_stat_t z_stat;
        if (num_entries > 1) {
            if (file_name.empty())
                SCP_FATAL(()) << "many files exist in zip arcive: " << archive_name
                              << " but file name is not specified";
            if (zip_stat(z_archive, file_name.c_str(), ZIP_FL_NOCASE, &z_stat) < 0)
                SCP_FATAL(()) << "Can't find any file named: " << file_name << " in the zip archive: " << archive_name;
        } else { // num of entries = 1
            if (zip_stat_index(z_archive, 0, ZIP_FL_NOCASE, &z_stat) < 0)
                SCP_FATAL(()) << "Can't get status os the file inside zip archive: " << archive_name;
        }
        zip_int64_t used_data_len = 0;
        if (!is_compressed) {
            std::vector<uint8_t> bin_info(z_stat.size);
            used_data_len = zip_read_file(z_archive, archive_name, z_stat, bin_info, file_offset, file_data_len);
            SCP_DEBUG(()) << "load data from zip archive " << archive_name << " to addr: 0x" << std::hex << addr
                          << " len: 0x" << std::hex << used_data_len;
            send(addr, reinterpret_cast<uint8_t*>(&bin_info[0]) + file_offset, used_data_len);
        } else {
            std::vector<uint8_t> compressed_file_data(z_stat.size);
            zip_read_file(z_archive, file_name, z_stat, compressed_file_data, 0, z_stat.size);
            zip_t* f_archive = zip_open_from_source(
                zip_source_buffer_create(reinterpret_cast<uint8_t*>(&compressed_file_data[0]), z_stat.size, 0, nullptr),
                ZIP_RDONLY, nullptr);
            if (!f_archive) SCP_FATAL(()) << "Can't open zip archive: " << file_name;
            zip_int64_t f_num_entries = zip_get_num_entries(f_archive, ZIP_FL_UNCHANGED);
            if (f_num_entries != 1)
                SCP_FATAL(()) << "Compressed file: " << file_name << " in: " << archive_name << " is corrupted!";
            zip_stat_t f_stat;
            if (zip_stat(f_archive, uncompressed_file_name.c_str(), ZIP_FL_NOCASE, &f_stat) < 0)
                SCP_FATAL(()) << "Can't find any file named: " << uncompressed_file_name
                              << " in the zip archive: " << file_name << " extracted from: " << archive_name;
            std::vector<uint8_t> bin_info(f_stat.size);
            used_data_len = zip_read_file(f_archive, file_name, f_stat, bin_info, file_offset, file_data_len);
            SCP_DEBUG(()) << "load data from zip archive " << file_name << " to addr: 0x" << std::hex << addr
                          << " len: 0x" << std::hex << used_data_len;
            send(addr, reinterpret_cast<uint8_t*>(&bin_info[0]) + file_offset, used_data_len);
        }
    }

    zip_int64_t zip_read_file(zip_t* z_archive, const std::string& archive_name, const zip_stat_t& z_stat,
                              std::vector<uint8_t>& data, uint64_t file_offset, uint64_t file_data_len)
    {
        uint8_t* bin_info_t = &data[0];
        zip_file_t* fd = zip_fopen(z_archive, z_stat.name, ZIP_FL_NOCASE);
        if (!fd) SCP_FATAL(()) << "Can't open file: " << z_stat.name << "in zip archive: " << archive_name;
        zip_int64_t used_file_data_len = 0;
        if (file_data_len == 0)
            used_file_data_len = z_stat.size;
        else if ((file_offset < z_stat.size) && ((file_data_len + file_offset) > z_stat.size))
            used_file_data_len = z_stat.size - file_offset;
        else if (file_offset > z_stat.size)
            SCP_FATAL(()) << "file offset (" << file_offset << " )is bigger than the size (" << z_stat.size << ") of "
                          << z_stat.name << "in zip archive: " << archive_name;
        else
            used_file_data_len = file_data_len;

        zip_int64_t read_bytes_num = 0, rem = z_stat.size;
        while (rem > 0) {
            read_bytes_num = zip_fread(fd, reinterpret_cast<uint8_t*>(bin_info_t), rem);
            if (read_bytes_num < 0) {
                SCP_FATAL(()) << "Can't read " << used_file_data_len << " from " << z_stat.name
                              << " in zip archive: " << archive_name;
            }
            bin_info_t += read_bytes_num;
            rem -= read_bytes_num;
        }
        zip_fclose(fd);
        return used_file_data_len;
    }

    void csv_load(std::string filename, uint64_t offset, std::string addr_str, std::string value_str, bool byte_swap)
    {
        std::ifstream file(filename);

        CSVRow row;
        if (!(file >> row)) {
            SCP_FATAL(()) << "Unable to find " << filename;
        }
        int addr_i = -1;
        int value_i = -1;
        for (std::size_t i = 0; i < row.size(); i++) {
            if (row[i] == addr_str) {
                addr_i = i;
            }
            if (row[i] == value_str) {
                value_i = i;
            }
        }
        if (addr_i == -1 || value_i == -1) {
            SCP_FATAL(()) << "Unable to find " << filename;
        }
        std::size_t min_field_count = std::max(addr_i, value_i);
        while (file >> row) {
            if (row.size() <= min_field_count) {
                continue;
            }
            const std::string addr = std::string(row[addr_i]);
            uint64_t value = std::stoll(std::string(row[value_i]), nullptr, 0);
            if (byte_swap) value = byte_swap32(value);
            uint64_t addr_p = std::stoll(addr, nullptr, 0) + offset;
            send(addr_p, (uint8_t*)(&value), sizeof(value));
        }
    }

private:
    // it makes no sense to expose this function, you could only sensibly use it from a config
    // anyway
    void data_load(std::string param, uint64_t addr, bool byte_swap)
    {
        for (std::string s : sc_cci_children(param.c_str())) {
            if (std::count_if(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); })) {
                union {
                    uint32_t d;
                    uint8_t b[sizeof(uint32_t)];
                } data;
                data.d = gs::cci_get<uint32_t>(m_broker, param + "." + std::string(s));
                if (byte_swap) {
                    data.d = byte_swap32(data.d);
                }
                send(addr + (stoi(s) * 0x4), &(data.b[0]), 4);
            } else {
                SCP_FATAL(()) << "unknown format for parameter load";
                break;
            }
        }
    }

public:
    void str_load(std::string data, uint64_t addr)
    {
        send(addr, reinterpret_cast<uint8_t*>(const_cast<char*>(data.c_str())), data.length());
    }

    void ptr_load(uint8_t* data, uint64_t addr, uint64_t len) { send(addr, data, len); }

    void elf_load(const std::string& path)
    {
        elf_reader(path, [&](uint64_t addr, uint8_t* data, uint64_t len) -> void { send(addr, data, len); });
    }

    /* Elf reader helper class */
private:
    /* elf loading */
    typedef enum _endianess {
        ENDIAN_UNKNOWN = 0,
        ENDIAN_LITTLE = 1,
        ENDIAN_BIG = 2,
    } endianess;

    struct elf_segment {
        const uint64_t virt;
        const uint64_t phys;
        const uint64_t size;
        const uint64_t filesz;
        const uint64_t offset;
        const bool r, w, x;
    };

    class elf_reader
    {
    private:
        std::vector<struct elf_segment> m_segments;

        std::function<void(uint64_t, uint8_t*, uint64_t)> m_send;

        std::string m_filename;
        int m_fd;
        uint64_t m_entry;
        uint64_t m_machine;
        endianess m_endian;

    public:
        uint64_t entry() const { return m_entry; }
        uint64_t machine() const { return m_machine; }

        endianess endian() const { return m_endian; }
        bool is_little_endian() const { return m_endian == ENDIAN_LITTLE; }
        bool is_big_endian() const { return m_endian == ENDIAN_BIG; }

        const char* filename() const { return m_filename.c_str(); }

        const std::vector<elf_segment>& segments() const { return m_segments; }

        elf_reader(const elf_reader&) = delete;

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

        static endianess elf_endianess(Elf* elf)
        {
            const char* ident = elf_getident(elf, nullptr);
            if (ident == nullptr) return ENDIAN_UNKNOWN;

            switch (ident[EI_DATA]) {
            case ELFDATA2LSB:
                return ENDIAN_LITTLE;
            case ELFDATA2MSB:
                return ENDIAN_BIG;
            default:
                return ENDIAN_UNKNOWN;
            }
        }

        template <typename T>
        static std::vector<elf_segment> elf_segments(Elf* elf)
        {
            size_t count = 0;
            int err = elf_getphdrnum(elf, &count);
            if (err) SCP_FATAL("elf_reader") << "elf_begin failed: " << elf_errmsg(err);

            std::vector<elf_segment> segments;
            typename T::Elf_Phdr* hdr = T::elf_getphdr(elf);
            for (uint64_t i = 0; i < count; i++, hdr++) {
                if (hdr->p_type == PT_LOAD) {
                    bool r = hdr->p_flags & PF_R;
                    bool w = hdr->p_flags & PF_W;
                    bool x = hdr->p_flags & PF_X;
                    segments.push_back(
                        { hdr->p_vaddr, hdr->p_paddr, hdr->p_memsz, hdr->p_filesz, hdr->p_offset, r, w, x });
                }
            }

            return segments;
        }

        template <typename T, typename ELF>
        void read_sections(ELF* elf)
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

        uint64_t to_phys(uint64_t virt) const
        {
            for (auto& seg : m_segments) {
                if ((virt >= seg.virt) && virt < (seg.virt + seg.size)) return seg.phys + virt - seg.virt;
            }

            return virt;
        }

        elf_reader(const std::string& path, std::function<void(uint64_t, uint8_t*, uint64_t)> _send)
            : m_send(_send), m_filename(path), m_fd(-1), m_entry(0), m_machine(0), m_endian(ENDIAN_UNKNOWN)
        {
            if (elf_version(EV_CURRENT) == EV_NONE) SCP_FATAL("elf_reader") << "failed to read libelf version";

#ifdef _WIN32
            m_fd = open(filename(), O_RDONLY | O_BINARY, 0);
#else
            m_fd = open(filename(), O_RDONLY, 0);
#endif

            if (m_fd < 0) SCP_FATAL("elf_reader") << "cannot open elf file " << filename();

            Elf* elf = elf_begin(m_fd, ELF_C_READ, nullptr);
            if (elf == nullptr) SCP_FATAL("elf_reader") << "error reading " << filename() << " : " << elf_errmsg(-1);

            if (elf_kind(elf) != ELF_K_ELF) SCP_FATAL("elf_reader") << "ELF version error in " << filename();

            Elf32_Ehdr* ehdr32 = elf32_getehdr(elf);
            Elf64_Ehdr* ehdr64 = elf64_getehdr(elf);
            if (ehdr32) {
                m_entry = ehdr32->e_entry;
                m_machine = ehdr32->e_machine;
                m_endian = elf_endianess(elf);
                m_segments = elf_segments<elf32_traits>(elf);
                read_sections<elf32_traits>(elf);
            }

            if (ehdr64) {
                m_entry = ehdr64->e_entry;
                m_machine = ehdr64->e_machine;
                m_endian = elf_endianess(elf);
                m_segments = elf_segments<elf64_traits>(elf);
                read_sections<elf64_traits>(elf);
            }

            elf_end(elf);

            for (auto s : m_segments) {
                read_segment(s);
            }
        }

        ~elf_reader()
        {
            if (m_fd >= 0) close(m_fd);
        }

        uint64_t read_segment(const elf_segment& segment)
        {
            if (m_fd < 0) SCP_FATAL("elf_reader") << "ELF file '" << filename() << "' not open";

            if (lseek(m_fd, segment.offset, SEEK_SET) != (ssize_t)segment.offset)
                SCP_FATAL("elf_reader") << "cannot seek within ELF file " << filename();

            uint8_t buff[1024];
            size_t sz = segment.filesz;
            size_t offset = 0;
            while (sz) {
                int s = read(m_fd, buff, (sz < sizeof(buff) ? sz : sizeof(buff)));
                sz -= s;
                if (!s) {
                    SCP_FATAL("elf_reader") << "cannot read ELF file " << filename();
                    exit(0);
                }
                m_send(segment.phys + offset, buff, s);
                offset += s;
            }
            if (lseek(m_fd, 0, SEEK_SET) != 0) SCP_FATAL("elf_reader") << "cannot seek within ELF file " << filename();

            return segment.size;
        }
    };
};
} // namespace gs

extern "C" void module_register();
#endif
