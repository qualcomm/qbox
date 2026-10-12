/*
 * This file is part of libqbox
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Some code snippets are borrowed from https://github.com/qemu/qemu/blob/master/hw/core/loader.c
 * (Gunzip functionality has not been implemented here)
 *
 * QEMU Executable loader
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Gunzip functionality in this file is derived from u-boot:
 *
 * (C) Copyright 2008 Semihalf
 *
 * (C) Copyright 2000-2005
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

/**
 * Some code snippets are borrowed from https://github.com/qemu/qemu/blob/master/hw/core/loader.c
 *
 * The EFI zboot header is described here:
 * https://github.com/torvalds/linux/blob/master/drivers/firmware/efi/libstub/zboot-header.S
 *
 * This c file is used to extract a compressed AARCH64 linux kernel Image from a PE32+ EFI zboot image.
 */

#define EFI_PE_LINUX_MAGIC    "\xcd\x23\x82\x81"
#define EFI_PE_MSDOS_MAGIC    "MZ"
#define MAX_ZIP_FILE_PATH_LEN 4096

struct linux_efi_zboot_header {
    uint8_t msdos_magic[2]; /* PE/COFF 'MZ' magic number */
    uint8_t reserved0[2];
    uint8_t zimg[4];         /* "zimg" for Linux EFI zboot images */
    uint32_t payload_offset; /* LE offset to compressed payload */
    uint32_t payload_size;   /* LE size of the compressed payload */
    uint8_t reserved1[8];
    char compression_type[32]; /* Compression type, NULL terminated */
    uint8_t linux_magic[4];    /* Linux header magic */
    uint32_t pe_header_offset; /* LE offset to the PE header */
};

int main(int argc, char** argv)
{
    const struct linux_efi_zboot_header* header;
    FILE* fp_read = NULL;
    FILE* fp_write = NULL;
    uint8_t* input_buffer = NULL;
    uint8_t* zip_buffer = NULL;
    size_t file_size = 0;
    size_t ret = 0;
    uint32_t pl_offset;
    uint32_t pl_size;
    char gzip_file[MAX_ZIP_FILE_PATH_LEN];
    int status = EXIT_SUCCESS;

    if (argc != 2) {
        fprintf(stderr, "please enter the path of the efi_zboot image\n");
        status = EXIT_FAILURE;
        goto out;
    }

    if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)) {
        fprintf(stdout,
                "\n****   extract a compressed AARCH64 linux kernel Image from a PE32+ EFI zboot image   ****\n");
        fprintf(stdout, "options:\nget_arm64_gzip_img_from_zboot_efi <PE32+ EFI zboot image>\n\n");
        return status;
    }

    sprintf(gzip_file, "%s.gz", argv[1]);

    fp_read = fopen(argv[1], "rb");
    if (!fp_read) {
        fprintf(stderr, "failed to open efi_zboot image %s, Error: %s\n", argv[1], strerror(errno));
        status = EXIT_FAILURE;
        goto out;
    }

    fseek(fp_read, 0, SEEK_END);
    file_size = ftell(fp_read);
    fseek(fp_read, 0, SEEK_SET);

    if (file_size < sizeof(*header)) {
        fprintf(stderr, "efi_zboot image file: %s is corrupted\n", argv[1]);
        status = EXIT_FAILURE;
        goto out;
    }

    input_buffer = (uint8_t*)calloc(file_size, sizeof(*input_buffer));
    if (!input_buffer) {
        fprintf(stderr, "failed to allocate input_buffer with size: %ld, Error: %s\n", file_size, strerror(errno));
        status = EXIT_FAILURE;
        goto out;
    }

    ret = fread(input_buffer, sizeof(input_buffer[0]), file_size, fp_read);
    if (ret != file_size) {
        fprintf(stderr, "failed to read efi_zboot image file: %s, Error: %s\n", argv[1], strerror(errno));
        status = EXIT_FAILURE;
        goto out;
    }

    header = (struct linux_efi_zboot_header*)input_buffer;

    if (memcmp(&header->msdos_magic, EFI_PE_MSDOS_MAGIC, 2) != 0 || memcmp(&header->zimg, "zimg", 4) != 0 ||
        memcmp(&header->linux_magic, EFI_PE_LINUX_MAGIC, 4) != 0) {
        fprintf(stderr, "efi_zboot image header of: %s is corrupted\n", argv[1]);
        status = EXIT_FAILURE;
        goto out;
    }

    if (strcmp(header->compression_type, "gzip") != 0) {
        fprintf(stderr, "unable to handle EFI zboot image with \"%.*s\" compression\n",
                (int)sizeof(header->compression_type) - 1, header->compression_type);
        status = EXIT_FAILURE;
        goto out;
    }

    pl_offset = header->payload_offset;
    pl_size = header->payload_size;

    fprintf(stdout,
            "Extract compressed kernel Image from PE32+ EFI application: [%s] from offset 0x%x with size: %u MB\n",
            argv[1], pl_offset, pl_size / (1024 * 1024));

    zip_buffer = (uint8_t*)malloc(pl_size * sizeof(uint8_t));
    if (!zip_buffer) {
        fprintf(stderr, "failed to allocate zip_buffer with size: %d, Error: %s\n", pl_size, strerror(errno));
        status = EXIT_FAILURE;
        goto out;
    }

    memcpy(zip_buffer, input_buffer + pl_offset, pl_size);

    fp_write = fopen(gzip_file, "wb");
    if (!fp_write) {
        fprintf(stderr, "failed to open output zip file: %s, Error: %s\n", gzip_file, strerror(errno));
        status = EXIT_FAILURE;
        goto out;
    }

    ret = fwrite(zip_buffer, sizeof zip_buffer[0], pl_size, fp_write);
    if (ret != pl_size) {
        fprintf(stderr, "failed to read efi_zboot image file: %s, Error: %s\n", argv[1], strerror(errno));
        status = EXIT_FAILURE;
        goto out;
    }

    fprintf(stdout, "Generated %s\n", gzip_file);

out:
    if (input_buffer) free(input_buffer);
    if (zip_buffer) free(zip_buffer);
    if (fp_read) fclose(fp_read);
    if (fp_write) fclose(fp_write);

    return status;
}