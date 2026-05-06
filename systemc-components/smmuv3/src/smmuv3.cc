/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#define INCBIN_SILENCE_BITCODE_WARNING
#include <reg_model_maker/incbin.h>

INCBIN(ZipArchive_smmuv3_, __FILE__ "_config.zip");

#include "smmuv3.h"
#include <module_factory_registery.h>

namespace gs {

const void* smmuv3_zip_archive_data() noexcept { return gZipArchive_smmuv3_Data; }
unsigned int smmuv3_zip_archive_size() noexcept { return gZipArchive_smmuv3_Size; }

template class smmuv3<32>;
template class smmuv3_tbu<32>;
} // namespace gs

typedef gs::smmuv3<> smmuv3;
typedef gs::smmuv3_tbu<> smmuv3_tbu;

void module_register()
{
    GSC_MODULE_REGISTER_C(smmuv3);
    GSC_MODULE_REGISTER_C(smmuv3_tbu, sc_core::sc_object*);
}
