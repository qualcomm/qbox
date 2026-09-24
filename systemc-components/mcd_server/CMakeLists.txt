# Copyright (c) 2026 Qualcomm Innovation Center, Inc. All Rights Reserved.
# SPDX-License-Identifier: BSD-3-Clause

gs_create_dymod(mcd_server)
target_link_libraries(mcd_server PUBLIC router)
if(WIN32)
    target_link_libraries(mcd_server PRIVATE ws2_32)
endif()
