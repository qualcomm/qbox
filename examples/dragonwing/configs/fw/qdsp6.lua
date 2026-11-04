dofile(top().."/utils.lua");

function get_SA8540P_nsp0_config_table()
 return {
    --/* captured from an SA840P-NSP0 via T32 */
        0x00001a00,   -- .l2tcm_base
        0x00000000,   -- .reserved
        0x00001b38,   -- .subsystem_base
        0x00001a19,   -- .etm_base
        0x00001a1a,   -- .l2cfg_base
        0x00001a1b,   -- .reserved2
        0x00001a80,   -- .l1s0_base
        0x00000000,   -- .axi2_lowaddr
        0x00001a1c,   -- .streamer_base
        0x00001a1d,   -- .clade_base
        0x00001a1e,   -- .fastl2vic_base
        0x00000080,   -- .jtlb_size_entries
        0x00000001,   -- .coproc_present
        0x00000004,   -- .ext_contexts
        0x00001a80,   -- .vtcm_base
        0x00002000,   -- .vtcm_size_kb
        0x00000400,   -- .l2tag_size
        0x00000400,   -- .l2ecomem_size
        0x0000003f,   -- .thread_enable_mask
        0x00001a1f,   -- .eccreg_base
        0x00000080,   -- .l2line_size
        0x00000000,   -- .tiny_core
        0x00000000,   -- .l2itcm_size
        0x00001a00,   -- .l2itcm_base
        0x00000000,   -- .clade2_base
        0x00000000,   -- .dtm_present
        0x00000001,   -- .dma_version
        0x00000007,   -- .hvx_vec_log_length
        0x00000000,   -- .core_id
        0x00000000,   -- .core_count
        0x00000040,   -- .hmx_int8_spatial
        0x00000020,   -- .hmx_int8_depth
        0x00000001,   -- .v2x_mode
        0x00000004,   -- .hmx_int8_rate
        0x00000020,   -- .hmx_fp16_spatial
        0x00000020,   -- .hmx_fp16_depth
        0x00000002,   -- .hmx_fp16_rate
        0x0000002e,   -- .hmx_fp16_acc_frac
        0x00000012,   -- .hmx_fp16_acc_int
        0x00000001,   -- .acd_preset
        0x00000001,   -- .mnd_preset
        0x00000010,   -- .l1d_size_kb
        0x00000020,   -- .l1i_size_kb
        0x00000002,   -- .l1d_write_policy
        0x00000040,   -- .vtcm_bank_width
        0x00000001,   -- .reserved3
        0x00000001,   -- .reserved4
        0x00000000,   -- .reserved5
        0x0000000a,   -- .hmx_cvt_mpy_size
        0x00000000,   -- .axi3_lowaddr
   };
end

function get_SA8540P_nsp1_config_table()
 return {
    --/* captured from an SA840P-NSP1 via T32 */
        0x00002000,
        0x00000000,
        0x00002138,
        0x00002019,
        0x0000201a,
        0x0000201b,
        0x00002080,
        0x00000000,
        0x0000201c,
        0x0000201d,
        0x0000201e,
        0x00000080,
        0x00000001,
        0x00000004,
        0x00002080,   -- .vtcm_base
        0x00002000,   -- .vtcm_size_kb
        0x00000400,
        0x00000400,
        0x0000003f,
        0x0000201f,
        0x00000080,
        0x00000000,
        0x00000000,
        0x00002000,
        0x00000000,
        0x00000000,
        0x00000001,
        0x00000007,
        0x00000000,
        0x00000000,
        0x00000040,
        0x00000020,
        0x00000001,
        0x00000004,
        0x00000020,
        0x00000020,
        0x00000002,
        0x0000002e,
        0x00000012,
        0x00000001,
        0x00000001,
        0x00000010,
        0x00000020,
        0x00000002,
        0x00000040,
        0x00000001,
        0x00000001,
        0x00000000,
        0x0000000a,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
        0x00000000,
     };
end
function get_SA8775P_nsp0_config_table()
 return {
    -- /* captured from email from Aaron Garofano <garofano@qti.qualcomm.com> */
        0x00002400, -- 000000 .l2tcm_base
        0x00000000, -- 000004 .reserved
        0x00002638, -- 000008 .subsystem_base
        0x00002419, -- 00000c .etm_base
        0x0000241a, -- 000010 .l2cfg_base
        0x0000241b, -- 000014 .reserved2
        0x00002500, -- 000018 .l1s0_base
        0x00000000, -- 00001c .axi2_lowaddr
        0x00000000, -- 000020 .streamer_base
        0x00000000, -- 000024 .clade_base
        0x0000241e, -- 000028 .fastl2vic_base
        0x00000080, -- 00002c .jtlb_size_entries
        0x00000001, -- 000030 .coproc_present
        0x00000004, -- 000034 .ext_contexts
        0x00002500, -- 000038 .vtcm_base
        0x00002000, -- 00003c .vtcm_size_kb
        0x00000400, -- 000040 .l2tag_size
        0x00000400, -- 000044 .l2ecomem_size
        0x0000003f, -- 000048 .thread_enable_mask
        0x0000241f, -- 00004c .eccreg_base
        0x00000080, -- 000050 .l2line_size
        0x00000000, -- 000054 .tiny_core
        0x00000000, -- 000058 .l2itcm_size
        0x00002400, -- 00005c .l2itcm_base
        0x00000000, -- 000060 .clade2_base
        0x00000000, -- 000064 .dtm_present
        0x00000003, -- 000068 .dma_version
        0x00000007, -- 00006c .hvx_vec_log_length
        0x00000000, -- 000070 .core_id
        0x00000000, -- 000074 .core_count
        0x00000040, -- 000078 .hmx_int8_spatial
        0x00000020, -- 00007c .hmx_int8_depth
        0x00000001, -- 000080 .v2x_mode
        0x00000008, -- 000084 .hmx_int8_rate
        0x00000020, -- 000088 .hmx_fp16_spatial
        0x00000000, -- 00008c .hmx_fp16_depth
        0x00000002, -- 000090 .hmx_fp16_rate
        0x00000016, -- 000094 .hmx_fp16_acc_frac
        0x00000006, -- 000098 .hmx_fp16_acc_int
        0x00000001, -- 00009c .acd_preset
        0x00000000, -- 0000a0 .mnd_preset
        0x00000010, -- 0000a4 .l1d_size_kb
        0x00000020, -- 0000a8 .l1i_size_kb
        0x00000002, -- 0000ac .l1d_write_policy
        0x00000080, -- 0000b0 .vtcm_bank_width
        0x00000001, -- 0000b4 .atomics_version
        0x00000000, -- 0000b8 .reserved4
        0x00000003, -- 0000bc .victim_cache_mode
        0x0000000a, -- 0000c0 .hmx_cvt_mpy_size
        0x000000e0, -- 0000c4 .axi3_lowaddr
        0x00000080, -- 0000c8 .capacity_domain
        0x00000000, -- 0000cc .reserved
        0x00000002, -- 0000d0 .hmx_int8_subcolumns
        0x00000005, -- 0000d4 .corecfg_present
        0x00000007, -- 0000d8 .hmx_fp16_acc_exp
        0x00000000, -- 0000dc .AXIM2_secondary_base
        0x00000000, -- 0000e0 .reserved
};
end

function get_SA8775P_nsp1_config_table()
 return {
    -- /* captured from email from Aaron Garofano <garofano@qti.qualcomm.com> */
        0x00002800, -- 000000 .l2tcm_base
        0x00000000, -- 000004 .reserved
        0x00002a38, -- 000008 .subsystem_base
        0x00002819, -- 00000c .etm_base
        0x0000281a, -- 000010 .l2cfg_base
        0x0000281b, -- 000014 .reserved2
        0x00002900, -- 000018 .l1s0_base
        0x00000000, -- 00001c .axi2_lowaddr
        0x00000000, -- 000020 .streamer_base
        0x00000000, -- 000024 .clade_base
        0x0000281e, -- 000028 .fastl2vic_base
        0x00000080, -- 00002c .jtlb_size_entries
        0x00000001, -- 000030 .coproc_present
        0x00000004, -- 000034 .ext_contexts
        0x00002900, -- 000038 .vtcm_base
        0x00002000, -- 00003c .vtcm_size_kb
        0x00000400, -- 000040 .l2tag_size
        0x00000400, -- 000044 .l2ecomem_size
        0x0000003f, -- 000048 .thread_enable_mask
        0x0000281f, -- 00004c .eccreg_base
        0x00000080, -- 000050 .l2line_size
        0x00000000, -- 000054 .tiny_core
        0x00000000, -- 000058 .l2itcm_size
        0x00002800, -- 00005c .l2itcm_base
        0x00000000, -- 000060 .clade2_base
        0x00000000, -- 000064 .dtm_present
        0x00000003, -- 000068 .dma_version
        0x00000007, -- 00006c .hvx_vec_log_length
        0x00000000, -- 000070 .core_id
        0x00000000, -- 000074 .core_count
        0x00000040, -- 000078 .hmx_int8_spatial
        0x00000020, -- 00007c .hmx_int8_depth
        0x00000001, -- 000080 .v2x_mode
        0x00000008, -- 000084 .hmx_int8_rate
        0x00000020, -- 000088 .hmx_fp16_spatial
        0x00000020, -- 00008c .hmx_fp16_depth
        0x00000002, -- 000090 .hmx_fp16_rate
        0x00000016, -- 000094 .hmx_fp16_acc_frac
        0x00000006, -- 000098 .hmx_fp16_acc_int
        0x00000001, -- 00009c .acd_preset
        0x00000001, -- 0000a0 .mnd_preset
        0x00000010, -- 0000a4 .l1d_size_kb
        0x00000020, -- 0000a8 .l1i_size_kb
        0x00000002, -- 0000ac .l1d_write_policy
        0x00000080, -- 0000b0 .vtcm_bank_width
        0x00000001, -- 0000b4 .atomics_version
        0x00000001, -- 0000b8 .reserved4
        0x00000003, -- 0000bc .victim_cache_mode
        0x0000000a, -- 0000c0 .hmx_cvt_mpy_size
        0x000000e0, -- 0000c4 .axi3_lowaddr
        0x00000080, -- 0000c8 .capacity_domain
        0x00000000, -- 0000cc .reserved
        0x00000002, -- 0000d0 .hmx_int8_subcolumns
        0x00000005, -- 0000d4 .corecfg_present
        0x00000007, -- 0000d8 .hmx_fp16_acc_exp
        0x00000000, -- 0000dc .AXIM2_secondary_base
        0x00000000, -- 0000e0 .reserved
};
end

-- From the DSP Silicon Validation team
function get_SA8775P_adsp_config_table()
 return {
        0x000002C0, -- L2TCM
        0x00000000, -- Reserved
        0x00000308, -- Subsystem_base
        0x000002F9, -- ETM_cfg_base
        0x000002FA, -- L2_cfg_base
        0x000002FB, -- Coproc_reserved
        0x000002E0, -- Coproc_reserved
        0x00000390, -- AXIM2_base
        0x000002FC, -- Stream_cfg_base
        0x000002FD, -- CLADE_cfg_base
        0x000002FE, -- Fastl2vic_base
        0x000000C0, -- JTLB_size
        0x00000000, -- Coproc_type
        0x00000000, -- Coproc_contexts
        0x00000000, -- VTCM_base
        0x00000000, -- VTCM_size
        0x00000400, -- L2_tag_size
        0x00000800, -- L2_array_size
        0x0000000F, -- Thread_enable_mask
        0x000002FF, -- ECC_cfg_base
        0x00000040, -- L2_line_size
        0x00000000, -- Audio_extension
 };
end

-- From the DSP Silicon Validation team
function get_8797_nsp_config_table()
 return {
        0x00003000, -- 0x0 L2TCM_BASE
        0x00000000, -- 0x4
        0x00003238, -- 0x8 SUBSYSTEM_BASE
        0x00003019, -- 0xc CORECFG_BASE | ETM_BASE
        0x0000301A, -- 0x10 L2CFG_BASE
        0x0000301B, -- 0x14
        0x00003100, -- 0x18 L1S_BASE
        0x00000000, -- 0x1c AXI2_LOWADDR
        0x00000000, -- 0x20 STREAMER_BASE
        0x00000000, -- 0x24
        0x0000301E, -- 0x28 FASTL2VIC_BASE
        0x00000080, -- 0x2c JTLB_SIZE
        0x0000000D, -- 0x30 COPROCESSOR_PRESENT
        0x00000008, -- 0x34 EXT_CONTEXTS
        0x00003100, -- 0x38 VTCM_ADDRESS
        0x00004000, -- 0x3c VTCM_SZ
        0x00000400, -- 0x40 L2TAG_SZ
        0x00000400, -- 0x44 L2ECOMEM_SIZE
        0x00000FFF, -- 0x48 THREAD_ENABLE_MASK
        0x0000301F, -- 0x4c ECCREG_BASE
        0x00000080, -- 0x50 L2LINE_SZ
        0x00000000, -- 0x54 AUDIO_EXTENSIONS
        0x00000000, -- 0x58 L2ITCM_SZ
        0x00003000, -- 0x5c L2ITCM_BASE
        0x00000000, -- 0x60
        0x00000000, -- 0x64 DTM_PRESENT
        0x00000008, -- 0x68 DMA_VERSION
        0x00000007, -- 0x6c HVX_VEC_LENGTH
        0x00000000, -- 0x70 CORE_ID
        0x00000000, -- 0x74 CORE_COUNT
        0x00000010, -- 0x78
        0x00000008, -- 0x7c
        0x00000001, -- 0x80 V2X_MODE
        0x00000000, -- 0x84
        0x00000000, -- 0x88
        0x00000000, -- 0x8c
        0x00000000, -- 0x90
        0x00000000, -- 0x94
        0x00000000, -- 0x98
        0x00000001, -- 0x9c ACD_PRESET
        0x00000001, -- 0xa0 MND_PRESET
        0x00000040, -- 0xa4 L1D_SZ
        0x00000040, -- 0xa8 L1I_SZ
        0x00000002, -- 0xac L1D_WRITE_POLICY
        0x00000080, -- 0xb0 VTCM_BANK_WIDTH
        0x00000001, -- 0xb4 ATOMICS_VERSION
        0x00000000, -- 0xb8 HVX_IEEE_PRESENT
        0x00000003, -- 0xbc VICTIM_CACHE_MODE
        0x00000000, -- 0xc0
        0x000000E0, -- 0xc4 CONSISTENCY_DOMAIN
        0x00000080, -- 0xc8 CAPACITY_DOMAIN
        0x00000000, -- 0xcc AXI3_LOWADDR
        0x00000000, -- 0xd0
        0x0000000D, -- 0xd4 CORECFG_PRESENT
        0x00000001, -- 0xd8
        0x00000000, -- 0xdc AXIM2_secondary_base
        0x00000100, -- 0xe0 AXIM_DATA_WIDTH
        0x00000008, -- 0xe4 L1D_WAYS
        0x00000004, -- 0xe8 L1I_WAYS
        0x00000008, -- 0xec L2WAY_NUM
        0x00000040, -- 0xf0 DMA_JTLB_SIZE
};
end

function get_SC8480XP_nsp0_config_table()
 return {
        0x00003000, -- .l2tcm_base
        0x00000000, -- .reserved
        0x00003238, -- .subsystem_base
        0x00003019, -- .etm_base
        0x0000301a, -- .l2cfg_base
        0x0000301b, -- .reserved2
        0x00003100, -- .l1s0_base
        0x00000000, -- .axi2_lowaddr
        0x00000000, -- .streamer_base
        0x00000000, -- .clade_base
        0x0000301e, -- .fastl2vic_base
        0x00000080, -- .jtlb_size_entries
        0x0000000d, -- .jtlb_size_entries
        0x00000008, -- .ext_contexts
        0x00003100, -- .ext_contexts
        0x00002000, -- .vtcm_base
        0x00000400, -- .vtcm_size_kb
        0x00000400, -- .l2ecomem_size
        0x00000fff, -- .thread_enable_mask
        0x00000000, -- .eccreg_base
        0x00000080, -- .l2line_size
        0x00000000, -- .tiny_core
        0x00000000, -- .l2itcm_size
        0x00003000, -- .l2itcm_base
        0x00000000, -- .clade2_base
        0x00000000, -- .dtm_present
        0x00000008, -- .dma_version
        0x00000007, -- .hvx_vec_log_length
        0x00000000, -- .core_id
        0x00000000, -- .core_count
        0x00000010, -- .hmx_int8_spatial
        0x00000008, -- .hmx_int8_depth
        0x00000001, -- .v2x_mode
        0x00000000, -- .hmx_int8_rate
        0x00000000, -- .hmx_fp16_spatial
        0x00000000, -- .hmx_fp16_depth
        0x00000000, -- .hmx_fp16_rate
        0x00000000, -- .hmx_fp16_acc_frac
        0x00000000, -- .hmx_fp16_acc_int
        0x00000001, -- .acd_preset
        0x00000001, -- .mnd_preset
        0x00000040, -- .l1d_size_kb
        0x00000040, -- .l1i_size_kb
        0x00000002, -- .l1d_write_policy
        0x00000080, -- .vtcm_bank_width
        0x00000001, -- .reserved3
        0x00000000, -- .reserved4
        0x00000003, -- .reserved5
        0x00000000, -- .hmx_cvt_mpy_size
        0x000000e0, -- .axi3_lowaddr
        0x00000080, -- .
        0x00000000, -- .
        0x00000000, -- .
        0x0000000d, -- .
        0x00000001, -- .
        0x00000000, -- .
        0x00000100, -- .
        0x00000008, -- .
        0x00000004, -- .
        0x00000008, -- .
        0x00000040, -- .
        0x00000000, -- .
        0x00000000, -- .
        0x00000000, -- .
        0x00000000, -- .
        0x00000000, -- .
        0x00000000, -- .
        0x00000000, -- .
  };
end

--
-- Places a stop(r0) insn at the entry address of each CDSP/ADSP so if no
-- code is loaded the processor will stop.
--
function set_dsp_entry_insn()
    local dsp_entry_points = {}
    local id = 0
    while true do
        local cluster = "hexagon_cluster_"..id
        if platform.hex_plugin.config[cluster] == nil then
            break
        end
        local entry_addr = platform.hex_plugin.config[cluster].hexagon_globalreg.hexagon_start_addr
        table.insert(dsp_entry_points, {data={0x6460c000}, address=entry_addr})
        id = id + 1
    end
    tableJoin(platform["load"], dsp_entry_points)
end

function coproc_path(dir)
    return dir .. "/coproc_rpc_remote"
end

function set_coproc(machine_has_coproc, hexagon, subsystem_id, th)

    hexagon["num_coproc_instance"] = 0
    hexagon["coproc"] = ""
    if not machine_has_coproc then
      return
    end

    local user_given_dir = GET("hex_coproc_dir")
    local default_dir = "/opt/qcom/HMX_plugin_QEMU/Tools/QEMUCoprocPlugin"
    local coproc_dir = default_dir

    if user_given_dir == "" then
        -- coproc explicitly disabled by the user
        return
    elseif user_given_dir ~= nil then
        if not file_exists(coproc_path(user_given_dir)) then
            print("\nERROR (subsystem " .. subsystem_id .. "):" ..
                  " Did not find DSP co-processor at user-specified path")
            print("'" .. coproc_path(user_given_dir) .. "' " .. "not found\n")
            os.exit(1)
        end
        coproc_dir = user_given_dir
    elseif not file_exists(coproc_path(default_dir)) then
        if th == 0 then
            print("\nWARNING (subsystem " .. subsystem_id .. "):" ..
                  " Did not find DSP co-processor, HMX ops disabled")
            print("'" .. coproc_path(default_dir) .. "' " .. "not found")
            print("Use `--params hex_coproc_dir=<path>` *before* the luafile to")
            print("specify a custom dir.\n")
            sleep(2)
        end
        -- Coproc not found, but warning already printed. Disable the coproc
        return
    end

    hexagon["num_coproc_instance"] = 1
    hexagon["coproc"] = coproc_dir
end

function get_dsp(arch, base, ahbs_base, cfgtable, start_addr,
                 subsystem_id, num_threads, inst_mgr, total_size,
                 num_of_vtcm, is_vtcm_shared)
    local cfgtable_base_addr = base + 0x180000;
    -- QNX/QQVP side-loaded boots retain the powered-on default. QLI/PAS
    -- boots set QLI_PAS_BOOT before constructing the Hexagon CPUs.
    local qli_pas_boot = (rawget(_G, "QLI_PAS_BOOT") == true)
    local sched_limit = qli_pas_boot;
    local jtlb_size = cfgtable[12];
    local dma_jtlb_size = cfgtable[61] or 0;
    local vtcm_base = cfgtable[15] << 16;
    local vtcm_size_kb = cfgtable[16];
    local machine_has_coproc = cfgtable[13] ~= 0;
    local hvx_contexts = cfgtable[14];
    local hmx_present = (cfgtable[31] ~= 0) and true or false;
    local l2vic_base = ahbs_base + 0x90000;
    local qtmr_rg0 = ahbs_base + 0xA1000;
    local is_vp_mode = true;
    assert(num_threads >= 1);

    local instance_id = subsystem_id
    if arch == "v81" then
        instance_id = instance_id << 8
        instance_id = instance_id | 0x2 -- NSP subsystem
    end

    local dsp = {
        moduletype = "ContainerWithArgs";
        args = {inst_mgr};

        qemu_hex_inst = {
            moduletype="QemuInstance";
            args = {inst_mgr, "HEXAGON"};
            tcg_mode="MULTI",
            sync_policy = "multithread-unconstrained",
            time_sync_strategy = "quantum_keeper"; -- to switch to mcips replace quantum_keeper by mcips
        },

        hexagon_globalreg = {
          moduletype =  "hexagon_globalreg";
          args = {"&qemu_hex_inst", "&qtimer", "&l2vic"};
          config_table_addr = cfgtable_base_addr;
          hexagon_start_addr = start_addr;
          dsp_arch = arch;
          isdben_trusted = true;
          isdben_secure = true;
        },

        hexagon_tlb = {
          moduletype = "hexagon_tlb";
          args = {"&qemu_hex_inst"};
          num_entries = jtlb_size;
          dma_entries = dma_jtlb_size;
        },

        reset = {
            moduletype = "reset_gpio",
            args = { "&qemu_hex_inst" },
        };

        router = {
            moduletype="router";
            log_level=0;
        },

        l2vic={
                 moduletype = "hexagon_l2vic";
                 mem           = {address=l2vic_base,  size=0x1000, bind = "&router.initiator_socket"};
                 fastmem       = {address=base      + 0x1e0000, size=0x10000, bind = "&router.initiator_socket"};
                 args = {"&qemu_hex_inst"};
                },

        qtimer={
                 moduletype = "qemu_hexagon_qtimer";
                 mem           = {address=ahbs_base + 0xA0000,  size=0x1000, bind = "&router.initiator_socket"};
                 mem_view      = {address=qtmr_rg0,  size=0x2000, bind = "&router.initiator_socket"};
                 args = {"&qemu_hex_inst"};
                 irq_0 = {bind = "&l2vic.irq_in_2"};
                 irq_1 = {bind = "&l2vic.irq_in_3"};
                },

        wdog  = { moduletype = "wdog"; socket = {address=ahbs_base + 0x84000,    size=0x1000, bind = "&router.initiator_socket"}},

        pll_0 = { moduletype = "pll"; socket = {address=ahbs_base + 0x40000,    size=0x10000, bind = "&router.initiator_socket"}},

        rom   = { moduletype = "gs_memory"; target_socket = {address=cfgtable_base_addr, size=#cfgtable*4, bind = "&router.initiator_socket"};
            read_only=true; load={data=cfgtable; offset=0}},

        pub = { moduletype = "pub"; socket = {address=ahbs_base, size=0x10000, bind = "&router.initiator_socket"};
                hex_halt = {bind = "&hexagon_thread_0.halt"};
            },

        csr = {
          moduletype = "csr";
          target_socket = {
            address=ahbs_base + 0x80000,
            size=0xD000,
            bind = "&router.initiator_socket",
            priority = 1,
          };
          instance_id = instance_id;
        },

        glob_per_init_hex = {
            moduletype = "global_peripheral_initiator";
            args = {"&qemu_hex_inst", "&hexagon_thread_0"};
            global_initiator = {bind = "&router.target_socket"};
        },

        fallback_memory = { moduletype = "gs_memory"; target_socket={address=base, size= total_size, priority = 1024, bind = "&router.initiator_socket"},
            dmi_allow=false; log_level = 0; verbose=false},
    };

    for th=0,num_threads-1 do
        local hexagon = {
            moduletype = "qemu_cpu_hexagon";
            args = {"&qemu_hex_inst", "&hexagon_globalreg", "&l2vic", "&hexagon_tlb"};
            start_powered_off = true;
            sched_limit=sched_limit;
            dsp_arch=arch;
            vp_mode = is_vp_mode;
            enable_semihosting = false;
            subsystem_id = subsystem_id;
            hexagon_num_threads = num_threads;
            vtcm_base_addr = vtcm_base;
            vtcm_size_kb = vtcm_size_kb;
            hvx_contexts = hvx_contexts;
            coproc2_present = hmx_present;
            mem = {bind = "&router.target_socket"};
            insn_per_second = 1000000000;
        };
        if th==0 then
            hexagon["start_powered_off"] = qli_pas_boot;
        end
        set_coproc(machine_has_coproc, hexagon, subsystem_id, th)
        dsp["hexagon_thread_" .. th] = hexagon;
    end

    local l2vic_num_output = 8; -- hardcoded in qemu
    for i=0,l2vic_num_output-1 do
        dsp["hexagon_thread_0"]["irq_in_" .. i] = {bind = "&l2vic.irq_out_" .. i};
    end

    if num_of_vtcm == 1 then
        dsp["vtcm_0"] = {
            moduletype = "gs_memory";
            target_socket = {address=vtcm_base, size=vtcm_size_kb*1024, bind = "&router.initiator_socket"},
            shared_memory_prefix = "vtcm_" .. subsystem_id .. "-";
            shared_memory=is_vtcm_shared,
        };
    end
    return dsp
end
