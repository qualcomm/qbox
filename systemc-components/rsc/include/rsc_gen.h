/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef rsc_gen_H
#define rsc_gen_H

#include <reg_model_maker/reg_model_maker.h>
#include <vector>
#include <cstdint>
#include <string>

namespace quic {
class rsc_gen
{
private:
    std::string m_name;

public:
    using reg_bank = gs::gs_register<gs::gs_bank_of<uint32_t>>;

    reg_bank RSC_ID_DRV;
    reg_bank RSC_PARAM_RSC_PARENTCHILD_CONFIG_DRV;
    gs::gs_register<uint32_t> TCS_AMC_MODE_IRQ_ENABLE_DRV0;
    gs::gs_register<uint32_t> TCS_AMC_MODE_IRQ_STATUS_DRV0;
    gs::gs_register<uint32_t> TCS_AMC_MODE_IRQ_CLEAR_DRV0;
    reg_bank TCS_DRV0_CMD_WAIT_FOR_CMPL;
    reg_bank TCS_DRV0_CONTROL;
    reg_bank TCS_DRV0_STATUS;
    reg_bank TCS_DRV0_CMD_ENABLE;
    reg_bank TCS_CMD_DRV0_MSGID;
    reg_bank TCS_CMD_DRV0_ADDR;
    reg_bank TCS_CMD_DRV0_DATA;
    reg_bank TCS_CMD_DRV0_STATUS;
    reg_bank TCS_CMD_DRV0_READ_RESPONSE_DATA;
    gs::gs_register<uint32_t> TCS_AMC_MODE_IRQ_ENABLE_DRV1;
    gs::gs_register<uint32_t> TCS_AMC_MODE_IRQ_STATUS_DRV1;
    gs::gs_register<uint32_t> TCS_AMC_MODE_IRQ_CLEAR_DRV1;
    reg_bank TCS_DRV1_CMD_WAIT_FOR_CMPL;
    reg_bank TCS_DRV1_CONTROL;
    reg_bank TCS_DRV1_STATUS;
    reg_bank TCS_DRV1_CMD_ENABLE;
    reg_bank TCS_CMD_DRV1_MSGID;
    reg_bank TCS_CMD_DRV1_ADDR;
    reg_bank TCS_CMD_DRV1_DATA;
    reg_bank TCS_CMD_DRV1_STATUS;
    reg_bank TCS_CMD_DRV1_READ_RESPONSE_DATA;
    gs::gs_register<uint32_t> TCS_AMC_MODE_IRQ_ENABLE_DRV2;
    gs::gs_register<uint32_t> TCS_AMC_MODE_IRQ_STATUS_DRV2;
    gs::gs_register<uint32_t> TCS_AMC_MODE_IRQ_CLEAR_DRV2;
    reg_bank TCS_DRV2_CMD_WAIT_FOR_CMPL;
    reg_bank TCS_DRV2_CONTROL;
    reg_bank TCS_DRV2_STATUS;
    reg_bank TCS_DRV2_CMD_ENABLE;
    reg_bank TCS_CMD_DRV2_MSGID;
    reg_bank TCS_CMD_DRV2_ADDR;
    reg_bank TCS_CMD_DRV2_DATA;
    reg_bank TCS_CMD_DRV2_STATUS;
    reg_bank TCS_CMD_DRV2_READ_RESPONSE_DATA;
    gs::gs_register<uint32_t> TCS_AMC_MODE_IRQ_ENABLE_DRV3;
    gs::gs_register<uint32_t> TCS_AMC_MODE_IRQ_STATUS_DRV3;
    gs::gs_register<uint32_t> TCS_AMC_MODE_IRQ_CLEAR_DRV3;
    reg_bank TCS_DRV3_CMD_WAIT_FOR_CMPL;
    reg_bank TCS_DRV3_CONTROL;
    reg_bank TCS_DRV3_STATUS;
    reg_bank TCS_DRV3_CMD_ENABLE;
    reg_bank TCS_CMD_DRV3_MSGID;
    reg_bank TCS_CMD_DRV3_ADDR;
    reg_bank TCS_CMD_DRV3_DATA;
    reg_bank TCS_CMD_DRV3_STATUS;
    reg_bank TCS_CMD_DRV3_READ_RESPONSE_DATA;

    rsc_gen()
        : m_name("rsc_gen")
        , RSC_ID_DRV("RSC_ID_DRV", "apss_rsc_apps_rscc.rscc_rsc.RSC_ID_DRV", 0x0, 4, 0x10000)
        , RSC_PARAM_RSC_PARENTCHILD_CONFIG_DRV("RSC_PARAM_RSC_PARENTCHILD_CONFIG_DRV",
              "apss_rsc_apps_rscc.rscc_rsc.RSC_PARAM_RSC_PARENTCHILD_CONFIG_DRV", 0xC, 4, 0x10000)
        , TCS_AMC_MODE_IRQ_ENABLE_DRV0("TCS_AMC_MODE_IRQ_ENABLE_DRV0",
              "apss_rsc_apps_rscc.rscc_rsc.TCS_AMC_MODE_IRQ_ENABLE_DRV0", 0xd00, 1)
        , TCS_AMC_MODE_IRQ_STATUS_DRV0("TCS_AMC_MODE_IRQ_STATUS_DRV0",
              "apss_rsc_apps_rscc.rscc_rsc.TCS_AMC_MODE_IRQ_STATUS_DRV0", 0xd04, 1)
        , TCS_AMC_MODE_IRQ_CLEAR_DRV0("TCS_AMC_MODE_IRQ_CLEAR_DRV0",
              "apss_rsc_apps_rscc.rscc_rsc.TCS_AMC_MODE_IRQ_CLEAR_DRV0", 0xd08, 1)
        , TCS_DRV0_CMD_WAIT_FOR_CMPL("TCS_DRV0_CMD_WAIT_FOR_CMPL",
              "apss_rsc_apps_rscc.rscc_rsc.TCS_DRV0_CMD_WAIT_FOR_CMPL", 0xD10, 4, 0x2A0)
        , TCS_DRV0_CONTROL("TCS_DRV0_CONTROL", "apss_rsc_apps_rscc.rscc_rsc.TCS_DRV0_CONTROL", 0xD14, 4, 0x2A0)
        , TCS_DRV0_STATUS("TCS_DRV0_STATUS", "apss_rsc_apps_rscc.rscc_rsc.TCS_DRV0_STATUS", 0xD18, 4, 0x2A0)
        , TCS_DRV0_CMD_ENABLE("TCS_DRV0_CMD_ENABLE", "apss_rsc_apps_rscc.rscc_rsc.TCS_DRV0_CMD_ENABLE", 0xD1C, 4, 0x2A0)
        , TCS_CMD_DRV0_MSGID("TCS_CMD_DRV0_MSGID", "apss_rsc_apps_rscc.rscc_rsc.TCS_CMD_DRV0_MSGID", 0xD30,
              std::vector<uint64_t>{4, 16}, std::vector<uint64_t>{0x2A0, 0x14})
        , TCS_CMD_DRV0_ADDR("TCS_CMD_DRV0_ADDR", "apss_rsc_apps_rscc.rscc_rsc.TCS_CMD_DRV0_ADDR", 0xD34,
              std::vector<uint64_t>{4, 16}, std::vector<uint64_t>{0x2A0, 0x14})
        , TCS_CMD_DRV0_DATA("TCS_CMD_DRV0_DATA", "apss_rsc_apps_rscc.rscc_rsc.TCS_CMD_DRV0_DATA", 0xD38,
              std::vector<uint64_t>{4, 16}, std::vector<uint64_t>{0x2A0, 0x14})
        , TCS_CMD_DRV0_STATUS("TCS_CMD_DRV0_STATUS", "apss_rsc_apps_rscc.rscc_rsc.TCS_CMD_DRV0_STATUS", 0xD3C,
              std::vector<uint64_t>{4, 16}, std::vector<uint64_t>{0x2A0, 0x14})
        , TCS_CMD_DRV0_READ_RESPONSE_DATA("TCS_CMD_DRV0_READ_RESPONSE_DATA",
              "apss_rsc_apps_rscc.rscc_rsc.TCS_CMD_DRV0_READ_RESPONSE_DATA", 0xD40, std::vector<uint64_t>{4, 16},
              std::vector<uint64_t>{0x2A0, 0x14})
        , TCS_AMC_MODE_IRQ_ENABLE_DRV1("TCS_AMC_MODE_IRQ_ENABLE_DRV1",
              "apss_rsc_apps_rscc.rscc_rsc.TCS_AMC_MODE_IRQ_ENABLE_DRV1", 0x10d00, 1)
        , TCS_AMC_MODE_IRQ_STATUS_DRV1("TCS_AMC_MODE_IRQ_STATUS_DRV1",
              "apss_rsc_apps_rscc.rscc_rsc.TCS_AMC_MODE_IRQ_STATUS_DRV1", 0x10d04, 1)
        , TCS_AMC_MODE_IRQ_CLEAR_DRV1("TCS_AMC_MODE_IRQ_CLEAR_DRV1",
              "apss_rsc_apps_rscc.rscc_rsc.TCS_AMC_MODE_IRQ_CLEAR_DRV1", 0x10d08, 1)
        , TCS_DRV1_CMD_WAIT_FOR_CMPL("TCS_DRV1_CMD_WAIT_FOR_CMPL",
              "apss_rsc_apps_rscc.rscc_rsc.TCS_DRV1_CMD_WAIT_FOR_CMPL", 0x10D10, 4, 0x2A0)
        , TCS_DRV1_CONTROL("TCS_DRV1_CONTROL", "apss_rsc_apps_rscc.rscc_rsc.TCS_DRV1_CONTROL", 0x10D14, 4, 0x2A0)
        , TCS_DRV1_STATUS("TCS_DRV1_STATUS", "apss_rsc_apps_rscc.rscc_rsc.TCS_DRV1_STATUS", 0x10D18, 4, 0x2A0)
        , TCS_DRV1_CMD_ENABLE("TCS_DRV1_CMD_ENABLE", "apss_rsc_apps_rscc.rscc_rsc.TCS_DRV1_CMD_ENABLE", 0x10D1C, 4, 0x2A0)
        , TCS_CMD_DRV1_MSGID("TCS_CMD_DRV1_MSGID", "apss_rsc_apps_rscc.rscc_rsc.TCS_CMD_DRV1_MSGID", 0x10D30,
              std::vector<uint64_t>{4, 16}, std::vector<uint64_t>{0x2A0, 0x14})
        , TCS_CMD_DRV1_ADDR("TCS_CMD_DRV1_ADDR", "apss_rsc_apps_rscc.rscc_rsc.TCS_CMD_DRV1_ADDR", 0x10D34,
              std::vector<uint64_t>{4, 16}, std::vector<uint64_t>{0x2A0, 0x14})
        , TCS_CMD_DRV1_DATA("TCS_CMD_DRV1_DATA", "apss_rsc_apps_rscc.rscc_rsc.TCS_CMD_DRV1_DATA", 0x10D38,
              std::vector<uint64_t>{4, 16}, std::vector<uint64_t>{0x2A0, 0x14})
        , TCS_CMD_DRV1_STATUS("TCS_CMD_DRV1_STATUS", "apss_rsc_apps_rscc.rscc_rsc.TCS_CMD_DRV1_STATUS", 0x10D3C,
              std::vector<uint64_t>{4, 16}, std::vector<uint64_t>{0x2A0, 0x14})
        , TCS_CMD_DRV1_READ_RESPONSE_DATA("TCS_CMD_DRV1_READ_RESPONSE_DATA",
              "apss_rsc_apps_rscc.rscc_rsc.TCS_CMD_DRV1_READ_RESPONSE_DATA", 0x10D40, std::vector<uint64_t>{4, 16},
              std::vector<uint64_t>{0x2A0, 0x14})
        , TCS_AMC_MODE_IRQ_ENABLE_DRV2("TCS_AMC_MODE_IRQ_ENABLE_DRV2",
              "apss_rsc_apps_rscc.rscc_rsc.TCS_AMC_MODE_IRQ_ENABLE_DRV2", 0x20d00, 1)
        , TCS_AMC_MODE_IRQ_STATUS_DRV2("TCS_AMC_MODE_IRQ_STATUS_DRV2",
              "apss_rsc_apps_rscc.rscc_rsc.TCS_AMC_MODE_IRQ_STATUS_DRV2", 0x20d04, 1)
        , TCS_AMC_MODE_IRQ_CLEAR_DRV2("TCS_AMC_MODE_IRQ_CLEAR_DRV2",
              "apss_rsc_apps_rscc.rscc_rsc.TCS_AMC_MODE_IRQ_CLEAR_DRV2", 0x20d08, 1)
        , TCS_DRV2_CMD_WAIT_FOR_CMPL("TCS_DRV2_CMD_WAIT_FOR_CMPL",
              "apss_rsc_apps_rscc.rscc_rsc.TCS_DRV2_CMD_WAIT_FOR_CMPL", 0x20D10, 8, 0x2A0)
        , TCS_DRV2_CONTROL("TCS_DRV2_CONTROL", "apss_rsc_apps_rscc.rscc_rsc.TCS_DRV2_CONTROL", 0x20D14, 8, 0x2A0)
        , TCS_DRV2_STATUS("TCS_DRV2_STATUS", "apss_rsc_apps_rscc.rscc_rsc.TCS_DRV2_STATUS", 0x20D18, 8, 0x2A0)
        , TCS_DRV2_CMD_ENABLE("TCS_DRV2_CMD_ENABLE", "apss_rsc_apps_rscc.rscc_rsc.TCS_DRV2_CMD_ENABLE", 0x20D1C, 8, 0x2A0)
        , TCS_CMD_DRV2_MSGID("TCS_CMD_DRV2_MSGID", "apss_rsc_apps_rscc.rscc_rsc.TCS_CMD_DRV2_MSGID", 0x20D30,
              std::vector<uint64_t>{8, 16}, std::vector<uint64_t>{0x2A0, 0x14})
        , TCS_CMD_DRV2_ADDR("TCS_CMD_DRV2_ADDR", "apss_rsc_apps_rscc.rscc_rsc.TCS_CMD_DRV2_ADDR", 0x20D34,
              std::vector<uint64_t>{8, 16}, std::vector<uint64_t>{0x2A0, 0x14})
        , TCS_CMD_DRV2_DATA("TCS_CMD_DRV2_DATA", "apss_rsc_apps_rscc.rscc_rsc.TCS_CMD_DRV2_DATA", 0x20D38,
              std::vector<uint64_t>{8, 16}, std::vector<uint64_t>{0x2A0, 0x14})
        , TCS_CMD_DRV2_STATUS("TCS_CMD_DRV2_STATUS", "apss_rsc_apps_rscc.rscc_rsc.TCS_CMD_DRV2_STATUS", 0x20D3C,
              std::vector<uint64_t>{8, 16}, std::vector<uint64_t>{0x2A0, 0x14})
        , TCS_CMD_DRV2_READ_RESPONSE_DATA("TCS_CMD_DRV2_READ_RESPONSE_DATA",
              "apss_rsc_apps_rscc.rscc_rsc.TCS_CMD_DRV2_READ_RESPONSE_DATA", 0x20D40, std::vector<uint64_t>{8, 16},
              std::vector<uint64_t>{0x2A0, 0x14})
        , TCS_AMC_MODE_IRQ_ENABLE_DRV3("TCS_AMC_MODE_IRQ_ENABLE_DRV3",
              "apss_rsc_apps_rscc.rscc_rsc.TCS_AMC_MODE_IRQ_ENABLE_DRV3", 0x30d00, 1)
        , TCS_AMC_MODE_IRQ_STATUS_DRV3("TCS_AMC_MODE_IRQ_STATUS_DRV3",
              "apss_rsc_apps_rscc.rscc_rsc.TCS_AMC_MODE_IRQ_STATUS_DRV3", 0x30d04, 1)
        , TCS_AMC_MODE_IRQ_CLEAR_DRV3("TCS_AMC_MODE_IRQ_CLEAR_DRV3",
              "apss_rsc_apps_rscc.rscc_rsc.TCS_AMC_MODE_IRQ_CLEAR_DRV3", 0x30d08, 1)
        , TCS_DRV3_CMD_WAIT_FOR_CMPL("TCS_DRV3_CMD_WAIT_FOR_CMPL",
              "apss_rsc_apps_rscc.rscc_rsc.TCS_DRV3_CMD_WAIT_FOR_CMPL", 0x30D10, 3, 0x2A0)
        , TCS_DRV3_CONTROL("TCS_DRV3_CONTROL", "apss_rsc_apps_rscc.rscc_rsc.TCS_DRV3_CONTROL", 0x30D14, 3, 0x2A0)
        , TCS_DRV3_STATUS("TCS_DRV3_STATUS", "apss_rsc_apps_rscc.rscc_rsc.TCS_DRV3_STATUS", 0x30D18, 3, 0x2A0)
        , TCS_DRV3_CMD_ENABLE("TCS_DRV3_CMD_ENABLE", "apss_rsc_apps_rscc.rscc_rsc.TCS_DRV3_CMD_ENABLE", 0x30D1C, 3, 0x2A0)
        , TCS_CMD_DRV3_MSGID("TCS_CMD_DRV3_MSGID", "apss_rsc_apps_rscc.rscc_rsc.TCS_CMD_DRV3_MSGID", 0x30D30,
              std::vector<uint64_t>{3, 16}, std::vector<uint64_t>{0x2A0, 0x14})
        , TCS_CMD_DRV3_ADDR("TCS_CMD_DRV3_ADDR", "apss_rsc_apps_rscc.rscc_rsc.TCS_CMD_DRV3_ADDR", 0x30D34,
              std::vector<uint64_t>{3, 16}, std::vector<uint64_t>{0x2A0, 0x14})
        , TCS_CMD_DRV3_DATA("TCS_CMD_DRV3_DATA", "apss_rsc_apps_rscc.rscc_rsc.TCS_CMD_DRV3_DATA", 0x30D38,
              std::vector<uint64_t>{3, 16}, std::vector<uint64_t>{0x2A0, 0x14})
        , TCS_CMD_DRV3_STATUS("TCS_CMD_DRV3_STATUS", "apss_rsc_apps_rscc.rscc_rsc.TCS_CMD_DRV3_STATUS", 0x30D3C,
              std::vector<uint64_t>{3, 16}, std::vector<uint64_t>{0x2A0, 0x14})
        , TCS_CMD_DRV3_READ_RESPONSE_DATA("TCS_CMD_DRV3_READ_RESPONSE_DATA",
              "apss_rsc_apps_rscc.rscc_rsc.TCS_CMD_DRV3_READ_RESPONSE_DATA", 0x30D40, std::vector<uint64_t>{3, 16},
              std::vector<uint64_t>{0x2A0, 0x14})
    {
    }

    void bind_regs(gs::json_module& jm)
    {
        jm.bind_reg(RSC_ID_DRV);
        jm.bind_reg(RSC_PARAM_RSC_PARENTCHILD_CONFIG_DRV);
        jm.bind_reg(TCS_AMC_MODE_IRQ_ENABLE_DRV0);
        jm.bind_reg(TCS_AMC_MODE_IRQ_STATUS_DRV0);
        jm.bind_reg(TCS_AMC_MODE_IRQ_CLEAR_DRV0);
        jm.bind_reg(TCS_DRV0_CMD_WAIT_FOR_CMPL);
        jm.bind_reg(TCS_DRV0_CONTROL);
        jm.bind_reg(TCS_DRV0_STATUS);
        jm.bind_reg(TCS_DRV0_CMD_ENABLE);
        jm.bind_reg(TCS_CMD_DRV0_MSGID);
        jm.bind_reg(TCS_CMD_DRV0_ADDR);
        jm.bind_reg(TCS_CMD_DRV0_DATA);
        jm.bind_reg(TCS_CMD_DRV0_STATUS);
        jm.bind_reg(TCS_CMD_DRV0_READ_RESPONSE_DATA);
        jm.bind_reg(TCS_AMC_MODE_IRQ_ENABLE_DRV1);
        jm.bind_reg(TCS_AMC_MODE_IRQ_STATUS_DRV1);
        jm.bind_reg(TCS_AMC_MODE_IRQ_CLEAR_DRV1);
        jm.bind_reg(TCS_DRV1_CMD_WAIT_FOR_CMPL);
        jm.bind_reg(TCS_DRV1_CONTROL);
        jm.bind_reg(TCS_DRV1_STATUS);
        jm.bind_reg(TCS_DRV1_CMD_ENABLE);
        jm.bind_reg(TCS_CMD_DRV1_MSGID);
        jm.bind_reg(TCS_CMD_DRV1_ADDR);
        jm.bind_reg(TCS_CMD_DRV1_DATA);
        jm.bind_reg(TCS_CMD_DRV1_STATUS);
        jm.bind_reg(TCS_CMD_DRV1_READ_RESPONSE_DATA);
        jm.bind_reg(TCS_AMC_MODE_IRQ_ENABLE_DRV2);
        jm.bind_reg(TCS_AMC_MODE_IRQ_STATUS_DRV2);
        jm.bind_reg(TCS_AMC_MODE_IRQ_CLEAR_DRV2);
        jm.bind_reg(TCS_DRV2_CMD_WAIT_FOR_CMPL);
        jm.bind_reg(TCS_DRV2_CONTROL);
        jm.bind_reg(TCS_DRV2_STATUS);
        jm.bind_reg(TCS_DRV2_CMD_ENABLE);
        jm.bind_reg(TCS_CMD_DRV2_MSGID);
        jm.bind_reg(TCS_CMD_DRV2_ADDR);
        jm.bind_reg(TCS_CMD_DRV2_DATA);
        jm.bind_reg(TCS_CMD_DRV2_STATUS);
        jm.bind_reg(TCS_CMD_DRV2_READ_RESPONSE_DATA);
        jm.bind_reg(TCS_AMC_MODE_IRQ_ENABLE_DRV3);
        jm.bind_reg(TCS_AMC_MODE_IRQ_STATUS_DRV3);
        jm.bind_reg(TCS_AMC_MODE_IRQ_CLEAR_DRV3);
        jm.bind_reg(TCS_DRV3_CMD_WAIT_FOR_CMPL);
        jm.bind_reg(TCS_DRV3_CONTROL);
        jm.bind_reg(TCS_DRV3_STATUS);
        jm.bind_reg(TCS_DRV3_CMD_ENABLE);
        jm.bind_reg(TCS_CMD_DRV3_MSGID);
        jm.bind_reg(TCS_CMD_DRV3_ADDR);
        jm.bind_reg(TCS_CMD_DRV3_DATA);
        jm.bind_reg(TCS_CMD_DRV3_STATUS);
        jm.bind_reg(TCS_CMD_DRV3_READ_RESPONSE_DATA);
        jm.log_end_of_binding_msg(m_name);
    }
};
} // namespace quic
#endif
