/* clang-format off */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef qupv3_qupv3_se_wrapper_se0_gen_H
#define qupv3_qupv3_se_wrapper_se0_gen_H

/**
 * This header file is auto generated using scripts/template_module/get_ipcat_info.py.
 * please uncomment the registers that you need to use only and make sure to resolve
 * name collisions by renaming the registers and fields to the name you prefer in case
 * 2 or more registers or fields have the same name.
 *
 * Register of interest should be uncommnented in 3 places in the file:
 * 1- Declaration.
 * 2- Constructor Initialization.
 * 3- Binding.
 *
 * Field of interest should be uncommented in 2 places:
 * 1- Declaration.
 * 2- Constructor Initialization.
 */

#include <reg_model_maker/reg_model_maker.h>

namespace quic {
class qupv3_qupv3_se_wrapper_se0_gen
{
private:
    std::string m_name;

public:
    gs::gs_register<uint32_t> GENI_INIT_CFG_REVISION;
    gs::gs_register<uint32_t> GENI_S_INIT_CFG_REVISION;
    gs::gs_register<uint32_t> GENI_RX_FIFO_CTRL;
        gs::gs_field<uint32_t> SW_RX_FIFO_FLUSH;
    gs::gs_register<uint32_t> GENI_FORCE_DEFAULT_REG;
    gs::gs_register<uint32_t> GENI_OUTPUT_CTRL;
    gs::gs_register<uint32_t> GENI_CGC_CTRL;
    gs::gs_register<uint32_t> GENI_STATUS;
    gs::gs_register<uint32_t> GENI_SER_M_CLK_CFG;
        gs::gs_field<uint32_t> CLK_DIV_VALUE;
        gs::gs_field<uint32_t> SER_CLK_EN;
    gs::gs_register<uint32_t> GENI_FW_REVISION_RO;
    gs::gs_register<uint32_t> GENI_CLK_SEL;
    gs::gs_register<uint32_t> GENI_DFS_IF_CFG;
    gs::gs_register<uint32_t> GENI_CFG_SEQ_START;
    gs::gs_register<uint32_t> GENI_CFG_STATUS;
    gs::gs_register<uint32_t> GENI_CFG_REG80;
    gs::gs_register<uint32_t> GENI_DMA_MODE_EN;
    gs::gs_register<uint32_t> GENI_TX_PACKING_CFG0;
    gs::gs_register<uint32_t> GENI_TX_PACKING_CFG1;
    gs::gs_register<uint32_t> GENI_RX_PACKING_CFG0;
    gs::gs_register<uint32_t> GENI_RX_PACKING_CFG1;
    gs::gs_register<uint32_t> GENI_M_CMD0;
        gs::gs_field<uint32_t> OPCODE;
        gs::gs_field<uint32_t> PARAM;
    gs::gs_register<uint32_t> GENI_M_CMD_CTRL_REG;
        gs::gs_field<uint32_t> M_GENI_CMD_CANCEL;
        gs::gs_field<uint32_t> M_GENI_CMD_ABORT;
    gs::gs_register<uint32_t> GENI_M_IRQ_STATUS;
        gs::gs_field<uint32_t> TX_FIFO_WATERMARK;
        gs::gs_field<uint32_t> M_RX_FIFO_LAST;
        gs::gs_field<uint32_t> M_RX_FIFO_WATERMARK; // name updated to avoid compilation Error
        gs::gs_field<uint32_t> RX_FIFO_WR_ERR;
        gs::gs_field<uint32_t> RX_FIFO_RD_ERR;
        gs::gs_field<uint32_t> IO_DATA_ASSERT;
        gs::gs_field<uint32_t> IO_DATA_DEASSERT;
        gs::gs_field<uint32_t> M_GP_IRQ_4;
        gs::gs_field<uint32_t> M_GP_IRQ_3;
        gs::gs_field<uint32_t> M_GP_IRQ_1;
        gs::gs_field<uint32_t> M_GP_IRQ_0;
        gs::gs_field<uint32_t> M_GP_SYNC_IRQ_0;
        gs::gs_field<uint32_t> M_CMD_ABORT;
        gs::gs_field<uint32_t> M_CMD_CANCEL;
        gs::gs_field<uint32_t> M_ILLEGAL_CMD;
        gs::gs_field<uint32_t> M_CMD_OVERRUN;
        gs::gs_field<uint32_t> M_CMD_DONE;
    gs::gs_register<uint32_t> GENI_M_IRQ_ENABLE;
        gs::gs_field<uint32_t> M_GP_SYNC_IRQ_0_EN;
    gs::gs_register<uint32_t> GENI_M_IRQ_CLEAR;
    gs::gs_register<uint32_t> GENI_M_IRQ_EN_SET;
    gs::gs_register<uint32_t> GENI_M_IRQ_EN_CLEAR;
    gs::gs_register<uint32_t> GENI_S_CMD_CTRL_REG;
        gs::gs_field<uint32_t> S_GENI_CMD_CANCEL;
        gs::gs_field<uint32_t> S_GENI_CMD_ABORT;
    gs::gs_register<uint32_t> GENI_S_IRQ_STATUS;
        gs::gs_field<uint32_t> S_RX_FIFO_LAST; // name updated to avoid compilation Error
        gs::gs_field<uint32_t> S_RX_FIFO_WATERMARK; // name updated to avoid compilation Error
        gs::gs_field<uint32_t> S_CMD_ABORT;
        gs::gs_field<uint32_t> S_CMD_CANCEL;
        gs::gs_field<uint32_t> S_CMD_DONE;
    gs::gs_register<uint32_t> GENI_S_IRQ_ENABLE;
    gs::gs_register<uint32_t> GENI_S_IRQ_CLEAR;
    gs::gs_register<uint32_t> GENI_S_IRQ_EN_SET;
    gs::gs_register<uint32_t> GENI_S_IRQ_EN_CLEAR;
    gs::gs_register<uint32_t> GENI_TX_FIFO;
    gs::gs_register<uint32_t> GENI_RX_FIFO;
    gs::gs_register<uint32_t> GENI_TX_FIFO_STATUS;
        gs::gs_field<uint32_t> TX_FIFO_WC;
    gs::gs_register<uint32_t> GENI_RX_FIFO_STATUS;
        gs::gs_field<uint32_t> RX_LAST;
        gs::gs_field<uint32_t> RX_LAST_BYTE_VALID;
        gs::gs_field<uint32_t> RX_FIFO_WC;
    gs::gs_register<uint32_t> GENI_TX_WATERMARK_REG;
    gs::gs_register<uint32_t> GENI_RX_WATERMARK_REG;
    gs::gs_register<uint32_t> GENI_RX_RFR_WATERMARK_REG;
    gs::gs_register<uint32_t> DMA_TX_PTR_L;
    gs::gs_register<uint32_t> DMA_TX_PTR_H;
    gs::gs_register<uint32_t> DMA_TX_ATTR;
        gs::gs_field<uint32_t> TX_DMA_OPERATION;
        gs::gs_field<uint32_t> EOT;
    gs::gs_register<uint32_t> DMA_TX_LENGTH;
    gs::gs_register<uint32_t> DMA_TX_IRQ_STAT;
        gs::gs_field<uint32_t> TX_RESET_DONE; // name updated to avoid compilation Error
        gs::gs_field<uint32_t> TX_SBE; // name updated to avoid compilation Error
        gs::gs_field<uint32_t> TX_EOT; // name updated to avoid compilation Error
        gs::gs_field<uint32_t> TX_DMA_DONE; // name updated to avoid compilation Error
    gs::gs_register<uint32_t> DMA_TX_IRQ_CLR;
    gs::gs_register<uint32_t> DMA_TX_IRQ_EN;
    gs::gs_register<uint32_t> DMA_TX_IRQ_EN_SET;
    gs::gs_register<uint32_t> DMA_TX_IRQ_EN_CLR;
    gs::gs_register<uint32_t> DMA_TX_LENGTH_IN;
    gs::gs_register<uint32_t> DMA_TX_FSM_RST;
    gs::gs_register<uint32_t> DMA_RX_PTR_L;
    gs::gs_register<uint32_t> DMA_RX_PTR_H;
    gs::gs_register<uint32_t> DMA_RX_ATTR;
        gs::gs_field<uint32_t> RX_DMA_OPERATION;
    gs::gs_register<uint32_t> DMA_RX_LENGTH;
    gs::gs_register<uint32_t> DMA_RX_IRQ_STAT;
        gs::gs_field<uint32_t> RX_RESET_DONE; // name updated to avoid compilation Error
        gs::gs_field<uint32_t> RX_SBE; // name updated to avoid compilation Error
        gs::gs_field<uint32_t> RX_EOT; // name updated to avoid compilation Error
        gs::gs_field<uint32_t> RX_DMA_DONE; // name updated to avoid compilation Error
    gs::gs_register<uint32_t> DMA_RX_IRQ_CLR;
    gs::gs_register<uint32_t> DMA_RX_IRQ_EN;
    gs::gs_register<uint32_t> DMA_RX_IRQ_EN_SET;
    gs::gs_register<uint32_t> DMA_RX_IRQ_EN_CLR;
    gs::gs_register<uint32_t> DMA_RX_LENGTH_IN;
    gs::gs_register<uint32_t> DMA_RX_FSM_RST;
    gs::gs_register<uint32_t> DMA_RX_FLUSH;
        gs::gs_field<uint32_t> RX_FLUSH;
    gs::gs_register<uint32_t> SE_GSI_EVENT_EN;
    gs::gs_register<uint32_t> SE_IRQ_EN;
        gs::gs_field<uint32_t> GENI_S_IRQ_EN;
        gs::gs_field<uint32_t> GENI_M_IRQ_EN;
        gs::gs_field<uint32_t> SE_DMA_TX_IRQ_EN; // name updated to avoid compilation Error
        gs::gs_field<uint32_t> SE_DMA_RX_IRQ_EN; // name updated to avoid compilation Error
    gs::gs_register<uint32_t> SE_HW_PARAM_0;
    gs::gs_register<uint32_t> SE_HW_PARAM_1;
    gs::gs_register<uint32_t> DMA_GENERAL_CFG;
    gs::gs_register<uint32_t> GENI_FW_REVISION;
    gs::gs_register<uint32_t> GENI_RX_BYTE_GRANULARITY;

    qupv3_qupv3_se_wrapper_se0_gen()
        : m_name("qupv3_qupv3_se_wrapper_se0_gen")
        , GENI_INIT_CFG_REVISION("GENI_INIT_CFG_REVISION", "se0.geni4_cfg.GENI_INIT_CFG_REVISION", 0x0, 1)
        , GENI_S_INIT_CFG_REVISION("GENI_S_INIT_CFG_REVISION", "se0.geni4_cfg.GENI_S_INIT_CFG_REVISION", 0x4, 1)
        , GENI_RX_FIFO_CTRL("GENI_RX_FIFO_CTRL", "se0.geni4_cfg.GENI_RX_FIFO_CTRL", 0x14, 1)
            , SW_RX_FIFO_FLUSH(GENI_RX_FIFO_CTRL, GENI_RX_FIFO_CTRL.get_regname() + ".SW_RX_FIFO_FLUSH", 0, 1)
        , GENI_FORCE_DEFAULT_REG("GENI_FORCE_DEFAULT_REG", "se0.geni4_cfg.GENI_FORCE_DEFAULT_REG", 0x20, 1)
        , GENI_OUTPUT_CTRL("GENI_OUTPUT_CTRL", "se0.geni4_cfg.GENI_OUTPUT_CTRL", 0x24, 1)
        , GENI_CGC_CTRL("GENI_CGC_CTRL", "se0.geni4_cfg.GENI_CGC_CTRL", 0x28, 1)
        , GENI_STATUS("GENI_STATUS", "se0.geni4_cfg.GENI_STATUS", 0x40, 1)
        , GENI_SER_M_CLK_CFG("GENI_SER_M_CLK_CFG", "se0.geni4_cfg.GENI_SER_M_CLK_CFG", 0x48, 1)
            , CLK_DIV_VALUE(GENI_SER_M_CLK_CFG, GENI_SER_M_CLK_CFG.get_regname() + ".CLK_DIV_VALUE", 4, 20)
            , SER_CLK_EN(GENI_SER_M_CLK_CFG, GENI_SER_M_CLK_CFG.get_regname() + ".SER_CLK_EN", 0, 1)
        , GENI_FW_REVISION_RO("GENI_FW_REVISION_RO", "se0.geni4_cfg.GENI_FW_REVISION_RO", 0x68, 1)
        , GENI_CLK_SEL("GENI_CLK_SEL", "se0.geni4_cfg.GENI_CLK_SEL", 0x7c, 1)
        , GENI_DFS_IF_CFG("GENI_DFS_IF_CFG", "se0.geni4_cfg.GENI_DFS_IF_CFG", 0x80, 1)
        , GENI_CFG_SEQ_START("GENI_CFG_SEQ_START", "se0.geni4_cfg.GENI_CFG_SEQ_START", 0x84, 1)
        , GENI_CFG_STATUS("GENI_CFG_STATUS", "se0.geni4_cfg.GENI_CFG_STATUS", 0x88, 1)
        , GENI_CFG_REG80("GENI_CFG_REG80", "se0.geni4_image_regs.GENI_CFG_REG80", 0x140, 1)
        , GENI_DMA_MODE_EN("GENI_DMA_MODE_EN", "se0.geni4_image_regs.GENI_DMA_MODE_EN", 0x158, 1)
        , GENI_TX_PACKING_CFG0("GENI_TX_PACKING_CFG0", "se0.geni4_image_regs.GENI_TX_PACKING_CFG0", 0x160, 1)
        , GENI_TX_PACKING_CFG1("GENI_TX_PACKING_CFG1", "se0.geni4_image_regs.GENI_TX_PACKING_CFG1", 0x164, 1)
        , GENI_RX_PACKING_CFG0("GENI_RX_PACKING_CFG0", "se0.geni4_image_regs.GENI_RX_PACKING_CFG0", 0x184, 1)
        , GENI_RX_PACKING_CFG1("GENI_RX_PACKING_CFG1", "se0.geni4_image_regs.GENI_RX_PACKING_CFG1", 0x188, 1)
        , GENI_M_CMD0("GENI_M_CMD0", "se0.geni4_data.GENI_M_CMD0", 0x0, 1)
            , OPCODE(GENI_M_CMD0, GENI_M_CMD0.get_regname() + ".OPCODE", 27, 5)
            , PARAM(GENI_M_CMD0, GENI_M_CMD0.get_regname() + ".PARAM", 0, 27)
        , GENI_M_CMD_CTRL_REG("GENI_M_CMD_CTRL_REG", "se0.geni4_data.GENI_M_CMD_CTRL_REG", 0x4, 1)
            , M_GENI_CMD_CANCEL(GENI_M_CMD_CTRL_REG, GENI_M_CMD_CTRL_REG.get_regname() + ".M_GENI_CMD_CANCEL", 2, 1)
            , M_GENI_CMD_ABORT(GENI_M_CMD_CTRL_REG, GENI_M_CMD_CTRL_REG.get_regname() + ".M_GENI_CMD_ABORT", 1, 1)
        , GENI_M_IRQ_STATUS("GENI_M_IRQ_STATUS", "se0.geni4_data.GENI_M_IRQ_STATUS", 0x10, 1)
            , TX_FIFO_WATERMARK(GENI_M_IRQ_STATUS, GENI_M_IRQ_STATUS.get_regname() + ".TX_FIFO_WATERMARK", 30, 1)
            , M_RX_FIFO_LAST(GENI_M_IRQ_STATUS, GENI_M_IRQ_STATUS.get_regname() + ".M_RX_FIFO_LAST", 27, 1)
            , M_RX_FIFO_WATERMARK(GENI_M_IRQ_STATUS, GENI_M_IRQ_STATUS.get_regname() + ".M_RX_FIFO_WATERMARK", 26, 1)
            , RX_FIFO_WR_ERR(GENI_M_IRQ_STATUS, GENI_M_IRQ_STATUS.get_regname() + ".RX_FIFO_WR_ERR", 25, 1)
            , RX_FIFO_RD_ERR(GENI_M_IRQ_STATUS, GENI_M_IRQ_STATUS.get_regname() + ".RX_FIFO_RD_ERR", 24, 1)
            , IO_DATA_ASSERT(GENI_M_IRQ_STATUS, GENI_M_IRQ_STATUS.get_regname() + ".IO_DATA_ASSERT", 23, 1)
            , IO_DATA_DEASSERT(GENI_M_IRQ_STATUS, GENI_M_IRQ_STATUS.get_regname() + ".IO_DATA_DEASSERT", 22, 1)
            , M_GP_IRQ_4(GENI_M_IRQ_STATUS, GENI_M_IRQ_STATUS.get_regname() + ".M_GP_IRQ_4", 13, 1)
            , M_GP_IRQ_3(GENI_M_IRQ_STATUS, GENI_M_IRQ_STATUS.get_regname() + ".M_GP_IRQ_3", 12, 1)
            , M_GP_IRQ_1(GENI_M_IRQ_STATUS, GENI_M_IRQ_STATUS.get_regname() + ".M_GP_IRQ_1", 10, 1)
            , M_GP_IRQ_0(GENI_M_IRQ_STATUS, GENI_M_IRQ_STATUS.get_regname() + ".M_GP_IRQ_0", 9, 1)
            , M_GP_SYNC_IRQ_0(GENI_M_IRQ_STATUS, GENI_M_IRQ_STATUS.get_regname() + ".M_GP_SYNC_IRQ_0", 8, 1)
            , M_CMD_ABORT(GENI_M_IRQ_STATUS, GENI_M_IRQ_STATUS.get_regname() + ".M_CMD_ABORT", 5, 1)
            , M_CMD_CANCEL(GENI_M_IRQ_STATUS, GENI_M_IRQ_STATUS.get_regname() + ".M_CMD_CANCEL", 4, 1)
            , M_ILLEGAL_CMD(GENI_M_IRQ_STATUS, GENI_M_IRQ_STATUS.get_regname() + ".M_ILLEGAL_CMD", 2, 1)
            , M_CMD_OVERRUN(GENI_M_IRQ_STATUS, GENI_M_IRQ_STATUS.get_regname() + ".M_CMD_OVERRUN", 1, 1)
            , M_CMD_DONE(GENI_M_IRQ_STATUS, GENI_M_IRQ_STATUS.get_regname() + ".M_CMD_DONE", 0, 1)
        , GENI_M_IRQ_ENABLE("GENI_M_IRQ_ENABLE", "se0.geni4_data.GENI_M_IRQ_ENABLE", 0x14, 1)
            , M_GP_SYNC_IRQ_0_EN(GENI_M_IRQ_ENABLE, GENI_M_IRQ_ENABLE.get_regname() + ".M_GP_SYNC_IRQ_0_EN", 8, 1)
        , GENI_M_IRQ_CLEAR("GENI_M_IRQ_CLEAR", "se0.geni4_data.GENI_M_IRQ_CLEAR", 0x18, 1)
        , GENI_M_IRQ_EN_SET("GENI_M_IRQ_EN_SET", "se0.geni4_data.GENI_M_IRQ_EN_SET", 0x1c, 1)
        , GENI_M_IRQ_EN_CLEAR("GENI_M_IRQ_EN_CLEAR", "se0.geni4_data.GENI_M_IRQ_EN_CLEAR", 0x20, 1)
        , GENI_S_CMD_CTRL_REG("GENI_S_CMD_CTRL_REG", "se0.geni4_data.GENI_S_CMD_CTRL_REG", 0x34, 1)
            , S_GENI_CMD_CANCEL(GENI_S_CMD_CTRL_REG, GENI_S_CMD_CTRL_REG.get_regname() + ".S_GENI_CMD_CANCEL", 2, 1)
            , S_GENI_CMD_ABORT(GENI_S_CMD_CTRL_REG, GENI_S_CMD_CTRL_REG.get_regname() + ".S_GENI_CMD_ABORT", 1, 1)
        , GENI_S_IRQ_STATUS("GENI_S_IRQ_STATUS", "se0.geni4_data.GENI_S_IRQ_STATUS", 0x40, 1)
            , S_RX_FIFO_LAST(GENI_S_IRQ_STATUS, GENI_S_IRQ_STATUS.get_regname() + ".S_RX_FIFO_LAST", 27, 1)
            , S_RX_FIFO_WATERMARK(GENI_S_IRQ_STATUS, GENI_S_IRQ_STATUS.get_regname() + ".S_RX_FIFO_WATERMARK", 26, 1)
            , S_CMD_ABORT(GENI_S_IRQ_STATUS, GENI_S_IRQ_STATUS.get_regname() + ".S_CMD_ABORT", 5, 1)
            , S_CMD_CANCEL(GENI_S_IRQ_STATUS, GENI_S_IRQ_STATUS.get_regname() + ".S_CMD_CANCEL", 4, 1)
            , S_CMD_DONE(GENI_S_IRQ_STATUS, GENI_S_IRQ_STATUS.get_regname() + ".S_CMD_DONE", 0, 1)
        , GENI_S_IRQ_ENABLE("GENI_S_IRQ_ENABLE", "se0.geni4_data.GENI_S_IRQ_ENABLE", 0x44, 1)
        , GENI_S_IRQ_CLEAR("GENI_S_IRQ_CLEAR", "se0.geni4_data.GENI_S_IRQ_CLEAR", 0x48, 1)
        , GENI_S_IRQ_EN_SET("GENI_S_IRQ_EN_SET", "se0.geni4_data.GENI_S_IRQ_EN_SET", 0x4c, 1)
        , GENI_S_IRQ_EN_CLEAR("GENI_S_IRQ_EN_CLEAR", "se0.geni4_data.GENI_S_IRQ_EN_CLEAR", 0x50, 1)
        , GENI_TX_FIFO("GENI_TX_FIFO", "se0.geni4_data.GENI_TX_FIFO", 0x100, 16)
        , GENI_RX_FIFO("GENI_RX_FIFO", "se0.geni4_data.GENI_RX_FIFO", 0x180, 16)
        , GENI_TX_FIFO_STATUS("GENI_TX_FIFO_STATUS", "se0.geni4_data.GENI_TX_FIFO_STATUS", 0x200, 1)
            , TX_FIFO_WC(GENI_TX_FIFO_STATUS, GENI_TX_FIFO_STATUS.get_regname() + ".TX_FIFO_WC", 0, 28)
        , GENI_RX_FIFO_STATUS("GENI_RX_FIFO_STATUS", "se0.geni4_data.GENI_RX_FIFO_STATUS", 0x204, 1)
            , RX_LAST(GENI_RX_FIFO_STATUS, GENI_RX_FIFO_STATUS.get_regname() + ".RX_LAST", 31, 1)
            , RX_LAST_BYTE_VALID(GENI_RX_FIFO_STATUS, GENI_RX_FIFO_STATUS.get_regname() + ".RX_LAST_BYTE_VALID", 28, 3)
            , RX_FIFO_WC(GENI_RX_FIFO_STATUS, GENI_RX_FIFO_STATUS.get_regname() + ".RX_FIFO_WC", 0, 25)
        , GENI_TX_WATERMARK_REG("GENI_TX_WATERMARK_REG", "se0.geni4_data.GENI_TX_WATERMARK_REG", 0x20c, 1)
        , GENI_RX_WATERMARK_REG("GENI_RX_WATERMARK_REG", "se0.geni4_data.GENI_RX_WATERMARK_REG", 0x210, 1)
        , GENI_RX_RFR_WATERMARK_REG("GENI_RX_RFR_WATERMARK_REG", "se0.geni4_data.GENI_RX_RFR_WATERMARK_REG", 0x214, 1)
        , DMA_TX_PTR_L("DMA_TX_PTR_L", "se0.qupv3_se_dma.DMA_TX_PTR_L", 0x30, 1)
        , DMA_TX_PTR_H("DMA_TX_PTR_H", "se0.qupv3_se_dma.DMA_TX_PTR_H", 0x34, 1)
        , DMA_TX_ATTR("DMA_TX_ATTR", "se0.qupv3_se_dma.DMA_TX_ATTR", 0x38, 1)
            , TX_DMA_OPERATION(DMA_TX_ATTR, DMA_TX_ATTR.get_regname() + ".TX_DMA_OPERATION", 1, 2)
            , EOT(DMA_TX_ATTR, DMA_TX_ATTR.get_regname() + ".EOT", 0, 1)
        , DMA_TX_LENGTH("DMA_TX_LENGTH", "se0.qupv3_se_dma.DMA_TX_LENGTH", 0x3c, 1)
        , DMA_TX_IRQ_STAT("DMA_TX_IRQ_STAT", "se0.qupv3_se_dma.DMA_TX_IRQ_STAT", 0x40, 1)
            , TX_RESET_DONE(DMA_TX_IRQ_STAT, DMA_TX_IRQ_STAT.get_regname() + ".TX_RESET_DONE", 3, 1)
            , TX_SBE(DMA_TX_IRQ_STAT, DMA_TX_IRQ_STAT.get_regname() + ".TX_SBE", 2, 1)
            , TX_EOT(DMA_TX_IRQ_STAT, DMA_TX_IRQ_STAT.get_regname() + ".TX_EOT", 1, 1)
            , TX_DMA_DONE(DMA_TX_IRQ_STAT, DMA_TX_IRQ_STAT.get_regname() + ".TX_DMA_DONE", 0, 1)
        , DMA_TX_IRQ_CLR("DMA_TX_IRQ_CLR", "se0.qupv3_se_dma.DMA_TX_IRQ_CLR", 0x44, 1)
        , DMA_TX_IRQ_EN("DMA_TX_IRQ_EN", "se0.qupv3_se_dma.DMA_TX_IRQ_EN", 0x48, 1)
        , DMA_TX_IRQ_EN_SET("DMA_TX_IRQ_EN_SET", "se0.qupv3_se_dma.DMA_TX_IRQ_EN_SET", 0x4c, 1)
        , DMA_TX_IRQ_EN_CLR("DMA_TX_IRQ_EN_CLR", "se0.qupv3_se_dma.DMA_TX_IRQ_EN_CLR", 0x50, 1)
        , DMA_TX_LENGTH_IN("DMA_TX_LENGTH_IN", "se0.qupv3_se_dma.DMA_TX_LENGTH_IN", 0x54, 1)
        , DMA_TX_FSM_RST("DMA_TX_FSM_RST", "se0.qupv3_se_dma.DMA_TX_FSM_RST", 0x58, 1)
        , DMA_RX_PTR_L("DMA_RX_PTR_L", "se0.qupv3_se_dma.DMA_RX_PTR_L", 0x130, 1)
        , DMA_RX_PTR_H("DMA_RX_PTR_H", "se0.qupv3_se_dma.DMA_RX_PTR_H", 0x134, 1) 
        , DMA_RX_ATTR("DMA_RX_ATTR", "se0.qupv3_se_dma.DMA_RX_ATTR", 0x138, 1)
            , RX_DMA_OPERATION(DMA_RX_ATTR, DMA_RX_ATTR.get_regname() + ".RX_DMA_OPERATION", 1, 2)
        , DMA_RX_LENGTH("DMA_RX_LENGTH", "se0.qupv3_se_dma.DMA_RX_LENGTH", 0x13c, 1)
        , DMA_RX_IRQ_STAT("DMA_RX_IRQ_STAT", "se0.qupv3_se_dma.DMA_RX_IRQ_STAT", 0x140, 1)
            , RX_RESET_DONE(DMA_RX_IRQ_STAT, DMA_RX_IRQ_STAT.get_regname() + ".RX_RESET_DONE", 3, 1)
            , RX_SBE(DMA_RX_IRQ_STAT, DMA_RX_IRQ_STAT.get_regname() + ".RX_SBE", 2, 1)
            , RX_EOT(DMA_RX_IRQ_STAT, DMA_RX_IRQ_STAT.get_regname() + ".RX_EOT", 1, 1)
            , RX_DMA_DONE(DMA_RX_IRQ_STAT, DMA_RX_IRQ_STAT.get_regname() + ".RX_DMA_DONE", 0, 1)
        , DMA_RX_IRQ_CLR("DMA_RX_IRQ_CLR", "se0.qupv3_se_dma.DMA_RX_IRQ_CLR", 0x144, 1)
        , DMA_RX_IRQ_EN("DMA_RX_IRQ_EN", "se0.qupv3_se_dma.DMA_RX_IRQ_EN", 0x148, 1)
        , DMA_RX_IRQ_EN_SET("DMA_RX_IRQ_EN_SET", "se0.qupv3_se_dma.DMA_RX_IRQ_EN_SET", 0x14c, 1)
        , DMA_RX_IRQ_EN_CLR("DMA_RX_IRQ_EN_CLR", "se0.qupv3_se_dma.DMA_RX_IRQ_EN_CLR", 0x150, 1)
        , DMA_RX_LENGTH_IN("DMA_RX_LENGTH_IN", "se0.qupv3_se_dma.DMA_RX_LENGTH_IN", 0x154, 1)
        , DMA_RX_FSM_RST("DMA_RX_FSM_RST", "se0.qupv3_se_dma.DMA_RX_FSM_RST", 0x158, 1)
        , DMA_RX_FLUSH("DMA_RX_FLUSH", "se0.qupv3_se_dma.DMA_RX_FLUSH", 0x160, 1)
            , RX_FLUSH(DMA_RX_FLUSH, DMA_RX_FLUSH.get_regname() + ".RX_FLUSH", 0, 1)
        , SE_GSI_EVENT_EN("SE_GSI_EVENT_EN", "se0.qupv3_se_dma.SE_GSI_EVENT_EN", 0x218, 1)
        , SE_IRQ_EN("SE_IRQ_EN", "se0.qupv3_se_dma.SE_IRQ_EN", 0x21c, 1)
            , GENI_S_IRQ_EN(SE_IRQ_EN, SE_IRQ_EN.get_regname() + ".GENI_S_IRQ_EN", 3, 1)
            , GENI_M_IRQ_EN(SE_IRQ_EN, SE_IRQ_EN.get_regname() + ".GENI_M_IRQ_EN", 2, 1)
            , SE_DMA_TX_IRQ_EN(SE_IRQ_EN, SE_IRQ_EN.get_regname() + ".SE_DMA_TX_IRQ_EN", 1, 1)
            , SE_DMA_RX_IRQ_EN(SE_IRQ_EN, SE_IRQ_EN.get_regname() + ".SE_DMA_RX_IRQ_EN", 0, 1)
        , SE_HW_PARAM_0("SE_HW_PARAM_0", "se0.qupv3_se_dma.SE_HW_PARAM_0", 0x224, 1)
        , SE_HW_PARAM_1("SE_HW_PARAM_1", "se0.qupv3_se_dma.SE_HW_PARAM_1", 0x228, 1)
        , DMA_GENERAL_CFG("DMA_GENERAL_CFG", "se0.qupv3_se_dma.DMA_GENERAL_CFG", 0x230, 1)
        , GENI_FW_REVISION("GENI_FW_REVISION", "se0.geni4_image.GENI_FW_REVISION", 0x0, 1)
        , GENI_RX_BYTE_GRANULARITY("GENI_RX_BYTE_GRANULARITY", "se0.geni4_image_regs.GENI_RX_BYTE_GRANULARITY", 0x1C0,
                                   1)
    {
    }

    void bind_regs(gs::json_module& jm)
    {
        jm.bind_reg(GENI_INIT_CFG_REVISION);
        jm.bind_reg(GENI_S_INIT_CFG_REVISION);
        jm.bind_reg(GENI_RX_FIFO_CTRL);
        jm.bind_reg(GENI_FORCE_DEFAULT_REG);
        jm.bind_reg(GENI_OUTPUT_CTRL);
        jm.bind_reg(GENI_CGC_CTRL);
        jm.bind_reg(GENI_STATUS);
        jm.bind_reg(GENI_SER_M_CLK_CFG);
        jm.bind_reg(GENI_FW_REVISION_RO);
        jm.bind_reg(GENI_CLK_SEL);
        jm.bind_reg(GENI_DFS_IF_CFG);
        jm.bind_reg(GENI_CFG_SEQ_START);
        jm.bind_reg(GENI_CFG_STATUS);
        jm.bind_reg(GENI_CFG_REG80);
        jm.bind_reg(GENI_DMA_MODE_EN);
        jm.bind_reg(GENI_TX_PACKING_CFG0);
        jm.bind_reg(GENI_TX_PACKING_CFG1);
        jm.bind_reg(GENI_RX_PACKING_CFG0);
        jm.bind_reg(GENI_RX_PACKING_CFG1);
        jm.bind_reg(GENI_M_CMD0);
        jm.bind_reg(GENI_M_CMD_CTRL_REG);
        jm.bind_reg(GENI_M_IRQ_STATUS);
        jm.bind_reg(GENI_M_IRQ_ENABLE);
        jm.bind_reg(GENI_M_IRQ_CLEAR);
        jm.bind_reg(GENI_M_IRQ_EN_SET);
        jm.bind_reg(GENI_M_IRQ_EN_CLEAR);
        jm.bind_reg(GENI_S_CMD_CTRL_REG);
        jm.bind_reg(GENI_S_IRQ_STATUS);
        jm.bind_reg(GENI_S_IRQ_ENABLE);
        jm.bind_reg(GENI_S_IRQ_CLEAR);
        jm.bind_reg(GENI_S_IRQ_EN_SET);
        jm.bind_reg(GENI_S_IRQ_EN_CLEAR);
        jm.bind_reg(GENI_TX_FIFO);
        jm.bind_reg(GENI_RX_FIFO);
        jm.bind_reg(GENI_TX_FIFO_STATUS);
        jm.bind_reg(GENI_RX_FIFO_STATUS);
        jm.bind_reg(GENI_TX_WATERMARK_REG);
        jm.bind_reg(GENI_RX_WATERMARK_REG);
        jm.bind_reg(GENI_RX_RFR_WATERMARK_REG);
        jm.bind_reg(DMA_TX_PTR_L);
        jm.bind_reg(DMA_TX_PTR_H);
        jm.bind_reg(DMA_TX_ATTR);
        jm.bind_reg(DMA_TX_LENGTH);
        jm.bind_reg(DMA_TX_IRQ_STAT);
        jm.bind_reg(DMA_TX_IRQ_CLR);
        jm.bind_reg(DMA_TX_IRQ_EN);
        jm.bind_reg(DMA_TX_IRQ_EN_SET);
        jm.bind_reg(DMA_TX_IRQ_EN_CLR);
        jm.bind_reg(DMA_TX_LENGTH_IN);
        jm.bind_reg(DMA_TX_FSM_RST);
        jm.bind_reg(DMA_RX_PTR_L);
        jm.bind_reg(DMA_RX_PTR_H);
        jm.bind_reg(DMA_RX_ATTR);
        jm.bind_reg(DMA_RX_LENGTH);
        jm.bind_reg(DMA_RX_IRQ_STAT);
        jm.bind_reg(DMA_RX_IRQ_CLR);
        jm.bind_reg(DMA_RX_IRQ_EN);
        jm.bind_reg(DMA_RX_IRQ_EN_SET);
        jm.bind_reg(DMA_RX_IRQ_EN_CLR);
        jm.bind_reg(DMA_RX_LENGTH_IN);
        jm.bind_reg(DMA_RX_FSM_RST);
        jm.bind_reg(DMA_RX_FLUSH);
        jm.bind_reg(SE_GSI_EVENT_EN);
        jm.bind_reg(SE_IRQ_EN);
        jm.bind_reg(SE_HW_PARAM_0);
        jm.bind_reg(SE_HW_PARAM_1);
        jm.bind_reg(DMA_GENERAL_CFG);
        jm.bind_reg(GENI_FW_REVISION);
        jm.bind_reg(GENI_RX_BYTE_GRANULARITY);

        jm.log_end_of_binding_msg(m_name);
    }
};

class qupv3_qupv3_se_wrapper_se0_gen_uart
{
private:
    std::string m_name;

public:
    gs::gs_register<uint32_t> UART_TX_TRANS_LEN;
    gs::gs_register<uint32_t> UART_MANUAL_RFR;
        gs::gs_field<uint32_t> UART_MANUAL_RFR_EN;
        gs::gs_field<uint32_t> UART_RFR_NOT_READY;

    qupv3_qupv3_se_wrapper_se0_gen_uart()
        : m_name("qupv3_qupv3_se_wrapper_se0_gen_uart")
        , UART_TX_TRANS_LEN("UART_TX_TRANS_LEN", "se0.geni4_image_regs.UART_TX_TRANS_LEN", 0x170, 1)
        , UART_MANUAL_RFR("UART_MANUAL_RFR", "se0.geni4_image_regs.UART_MANUAL_RFR", 0x1ac, 1)
            , UART_MANUAL_RFR_EN(UART_MANUAL_RFR, UART_MANUAL_RFR.get_regname() + ".UART_MANUAL_RFR_EN", 31, 1)
            , UART_RFR_NOT_READY(UART_MANUAL_RFR, UART_MANUAL_RFR.get_regname() + ".UART_RFR_NOT_READY", 1, 1)
    {
    }
    void bind_regs_uart(gs::json_module& jm)
    {
        jm.bind_reg(UART_TX_TRANS_LEN);
        jm.bind_reg(UART_MANUAL_RFR);
    }
};

class qupv3_qupv3_se_wrapper_se0_gen_spi
{
private:
    std::string m_name;

public:
    gs::gs_register<uint32_t> SPI_CPHA;
    gs::gs_register<uint32_t> SPI_LOOPBACK_CFG;
    gs::gs_register<uint32_t> SPI_CPOL;
    gs::gs_register<uint32_t> SPI_DEMUX_OUTPUT_INV;
    gs::gs_register<uint32_t> SPI_DEMUX_SEL;
    gs::gs_register<uint32_t> SPI_TRANS_CFG;
    gs::gs_register<uint32_t> SPI_WORD_LEN;
    gs::gs_register<uint32_t> SPI_TX_TRANS_LEN;
    gs::gs_register<uint32_t> SPI_RX_TRANS_LEN;
    gs::gs_register<uint32_t> SPI_PRE_POST_CMD_DLY;
    gs::gs_register<uint32_t> SPI_DELAYS_COUNTERS;
    gs::gs_register<uint32_t> SPI_SLAVE_EN;

    qupv3_qupv3_se_wrapper_se0_gen_spi()
        : m_name("qupv3_qupv3_se_wrapper_se0_gen_spi")
        , SPI_CPHA("SPI_CPHA", "se0.geni4_image_regs.SPI_CPHA", 0x124, 1)
        , SPI_LOOPBACK_CFG("SPI_LOOPBACK_CFG", "se0.geni4_image_regs.SPI_LOOPBACK_CFG", 0x12c, 1)
        , SPI_CPOL("SPI_CPOL", "se0.geni4_image_regs.SPI_CPOL", 0x130, 1)
        , SPI_DEMUX_OUTPUT_INV("SPI_DEMUX_OUTPUT_INV", "se0.geni4_image_regs.SPI_DEMUX_OUTPUT_INV", 0x14c, 1)
        , SPI_DEMUX_SEL("SPI_DEMUX_SEL", "se0.geni4_image_regs.SPI_DEMUX_SEL", 0x150, 1)
        , SPI_TRANS_CFG("SPI_TRANS_CFG", "se0.geni4_image_regs.SPI_TRANS_CFG", 0x15c, 1)
        , SPI_WORD_LEN("SPI_WORD_LEN", "se0.geni4_image_regs.SPI_WORD_LEN", 0x168, 1)
        , SPI_TX_TRANS_LEN("SPI_TX_TRANS_LEN", "se0.geni4_image_regs.SPI_TX_TRANS_LEN", 0x16c, 1)
        , SPI_RX_TRANS_LEN("SPI_RX_TRANS_LEN", "se0.geni4_image_regs.SPI_RX_TRANS_LEN", 0x170, 1)
        , SPI_PRE_POST_CMD_DLY("SPI_PRE_POST_CMD_DLY", "se0.geni4_image_regs.SPI_PRE_POST_CMD_DLY", 0x174, 1)
        , SPI_DELAYS_COUNTERS("SPI_DELAYS_COUNTERS", "se0.geni4_image_regs.SPI_DELAYS_COUNTERS", 0x178, 1)
        , SPI_SLAVE_EN("SPI_SLAVE_EN", "se0.geni4_image_regs.SPI_SLAVE_EN", 0x1bc, 1)
    {
    }
    void bind_regs_spi(gs::json_module& jm)
    {
        jm.bind_reg(SPI_CPHA);
        jm.bind_reg(SPI_LOOPBACK_CFG);
        jm.bind_reg(SPI_CPOL);
        jm.bind_reg(SPI_DEMUX_OUTPUT_INV);
        jm.bind_reg(SPI_DEMUX_SEL);
        jm.bind_reg(SPI_TRANS_CFG);
        jm.bind_reg(SPI_WORD_LEN);
        jm.bind_reg(SPI_TX_TRANS_LEN);
        jm.bind_reg(SPI_RX_TRANS_LEN);
        jm.bind_reg(SPI_PRE_POST_CMD_DLY);
        jm.bind_reg(SPI_DELAYS_COUNTERS);
        jm.bind_reg(SPI_SLAVE_EN);
    }
};

class qupv3_qupv3_se_wrapper_se0_gen_i2c
{
private:
    std::string m_name;

public:
    gs::gs_register<uint32_t> I2C_DELAY_COUNTER;
    gs::gs_register<uint32_t> I2C_SCL_COUNTERS;
        gs::gs_field<uint32_t> I2C_SCL_HIGH_COUNTER;
        gs::gs_field<uint32_t> I2C_SCL_LOW_COUNTER;
        gs::gs_field<uint32_t> I2C_SCL_CYCLE_COUNTER;
    gs::gs_register<uint32_t> I2C_TX_TRANS_LEN;
    gs::gs_register<uint32_t> I2C_RX_TRANS_LEN;

    qupv3_qupv3_se_wrapper_se0_gen_i2c()
        : m_name("qupv3_qupv3_se_wrapper_se0_gen_i2c")
        , I2C_DELAY_COUNTER("I2C_DELAY_COUNTER", "se0.geni4_image_regs.I2C_DELAY_COUNTER", 0x174, 1)
        , I2C_SCL_COUNTERS("I2C_SCL_COUNTERS", "se0.geni4_image_regs.I2C_SCL_COUNTERS", 0x178, 1)
            , I2C_SCL_HIGH_COUNTER(I2C_SCL_COUNTERS, I2C_SCL_COUNTERS.get_regname() + ".I2C_SCL_HIGH_COUNTER", 20, 10)
            , I2C_SCL_LOW_COUNTER(I2C_SCL_COUNTERS, I2C_SCL_COUNTERS.get_regname() + ".I2C_SCL_LOW_COUNTER", 10, 10)
            , I2C_SCL_CYCLE_COUNTER(I2C_SCL_COUNTERS, I2C_SCL_COUNTERS.get_regname() + ".I2C_SCL_CYCLE_COUNTER", 0, 10)
        , I2C_TX_TRANS_LEN("I2C_TX_TRANS_LEN", "se0.geni4_image_regs.I2C_TX_TRANS_LEN", 0x16c, 1)
        , I2C_RX_TRANS_LEN("I2C_RX_TRANS_LEN", "se0.geni4_image_regs.I2C_RX_TRANS_LEN", 0x170, 1)

    {
    }
    void bind_regs_i2c(gs::json_module& jm)
    {
        jm.bind_reg(I2C_DELAY_COUNTER);
        jm.bind_reg(I2C_SCL_COUNTERS);
        jm.bind_reg(I2C_TX_TRANS_LEN);
        jm.bind_reg(I2C_RX_TRANS_LEN);
    }
};

} // namespace quic
#endif
