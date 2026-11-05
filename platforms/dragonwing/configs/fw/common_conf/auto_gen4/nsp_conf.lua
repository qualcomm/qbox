local QDSP6_CFG   = valid_file(top().."../../qdsp6.lua")
dofile(QDSP6_CFG);

local NSP0_BASE     = 0x24000000 -- TURING_SS_0TURING
local NSP1_BASE     = 0x28000000 -- TURING_SS_1TURING

-- ipcat says LPASS subsystem starts at 0x2c00000, with a 4MiB "unused"
-- region. However, our calculations from get_dsp() only match the expected
-- registers if we consider LPASS' base to start at 0x200000 further, right in
-- the middle of the "unused" region.
local LPASS_BASE = 0x2c00000 + 0x200000

-- These values were pulled out from the silicom by SiVal team
--local NSP0_AHB_LOW = 0x26000000 -- TURINGNSP_0_TURING_QDSP6SS_STRAP_AHBLOWER_STATUS
local NSP0_AHB_HIGH = 0x27000000 -- TURINGNSP_0_TURING_QDSP6SS_STRAP_AHBUPPER_STATUS
--local NSP0_AHB_SIZE = NSP0_AHB_HIGH - NSP0_AHB_LOW
--local NSP1_AHB_LOW = 0x2A000000 -- TURINGNSP_1_TURING_QDSP6SS_STRAP_AHBLOWER_STATUS
local NSP1_AHB_HIGH = 0x2B000000 -- TURINGNSP_1_TURING_QDSP6SS_STRAP_AHBUPPER_STATUS
--local NSP1_AHB_SIZE = NSP1_AHB_HIGH - NSP1_AHB_LOW
--local ADSP_AHB_LOW = 0x3000000 -- LPASS_QDSP6SS_STRAP_AHBLOWER_STATUS
local ADSP_AHB_HIGH =0x3900000 -- LPASS_QDSP6SS_STRAP_AHBUPPER_STATUS
--local ADSP_AHB_SIZE = ADSP_AHB_HIGH - ADSP_AHB_LOW
-- Unused:
-- local TURINGGDSP_0_TURING_QDSP6SS_STRAP_AHBUPPER_STATUS=0x2100000
-- local TURINGGDSP_0_TURING_QDSP6SS_STRAP_AHBLOWER_STATUS=0x2080000
-- local TURINGGDSP_1_TURING_QDSP6SS_STRAP_AHBUPPER_STATUS=0x2200000
-- local TURINGGDSP_1_TURING_QDSP6SS_STRAP_AHBLOWER_STATUS=0x2180000

-- ^^ This val is also described as 'TCM_BASE_ADDRESS' in
-- Sys arch spec "TCM region memory map"
local TURING_SS_0TURING_QDSP6V68SS_CSR= 0x26380000
local TURING_SS_1TURING_QDSP6V68SS_CSR= 0x2A380000
local LPASS_QDSP6V68SS_CSR= 0x03080000
-- The QDSP6v67SS private registers include CSR, L2VIC, QTMR registers.
-- The address base is determined by parameter QDSP6SS_PRIV_BASEADDR

-- LeMans: NSP0 configuration --
local SA8775P_nsp0_config_table= get_SA8775P_nsp0_config_table();
local nsp0ss = get_dsp(
        "v73",
        NSP0_BASE, -- TURING_SS_0TURING
        0x26300000, -- TURING_SS_0TURING_QDSP6V68SS
        SA8775P_nsp0_config_table,
        0x9B800000, -- entry point address from bootimage_lemans.cdsp0.prodQ.pbn
        0, -- subsystem_id
        6, -- threads
        "&qemu_inst_mgr_h",
        NSP0_AHB_HIGH - NSP0_BASE, -- region total size
        1, -- num_of_vtcm
        true -- is_vtcm_shared
        );
assert((SA8775P_nsp0_config_table[11] << 16) == nsp0ss.l2vic.fastmem.address)
assert((SA8775P_nsp0_config_table[3] << 16) == TURING_SS_0TURING_QDSP6V68SS_CSR)

-- NSP1 configuration TODO : need ramdump of config_table for NSP1. --
local SA8775P_nsp1_config_table= get_SA8775P_nsp1_config_table();
local nsp1ss = get_dsp(
        "v73",
        NSP1_BASE, -- TURING_SS_1TURING
        0x2A300000, -- TURING_SS_1TURING_QDSP6V68SS
        SA8775P_nsp1_config_table,
        0x9D700000, -- entry point address from bootimage_lemans.cdsp1.prodQ.pbn
        1, -- subsystem_id
        6, -- threads
        "&qemu_inst_mgr_h",
        NSP1_AHB_HIGH - NSP1_BASE, -- region total size
        1, -- num_of_vtcm
        true -- is_vtcm_shared
        );
assert((SA8775P_nsp1_config_table[11] << 16) == nsp1ss.l2vic.fastmem.address)
assert((SA8775P_nsp1_config_table[3] << 16) == TURING_SS_1TURING_QDSP6V68SS_CSR)

-- ADSP configuration
local SA8775P_adsp_config_table= get_SA8775P_adsp_config_table();
local adsp = get_dsp(
        "v66",
        LPASS_BASE,
        0x03000000, -- LPASS_QDSP6V68SS_PUB
        SA8775P_adsp_config_table,
        0x95C00000, -- entry point address from bootimage_lemans.adsp.prodQ.pbn
        2, -- subsystem_id
        2, -- threads
        "&qemu_inst_mgr_h",
        (0x03000000 - LPASS_BASE) + 0x100000, -- region total size
        0, -- num_of_vtcm
        true -- is_vtcm_shared
        );
assert((SA8775P_adsp_config_table[11] << 16) == adsp.l2vic.fastmem.address)
assert((SA8775P_adsp_config_table[3] << 16) == LPASS_QDSP6V68SS_CSR)


hex_plugin_configs = {
    qemu_inst_mgr_h = {
        moduletype = "QemuInstanceManager";
    },

    hexagon_cluster_0 = nsp0ss;
    hexagon_cluster_1 = nsp1ss;
    hexagon_cluster_2 = adsp;

    -- address translator with mapped_base_addr = target_socket.address because no translation is done in this case.
    addrtr_nsp0_to_app_low = {
        moduletype = "addrtr";
        target_socket = {address=0x0, size=NSP0_BASE, relative_addresses=false, bind = "&hexagon_cluster_0.router.initiator_socket"},
        mapped_base_addr = 0x0;
    };

    addrtr_nsp0_to_tbu = {
        moduletype = "addrtr";
        target_socket = {address=NSP0_AHB_HIGH, size= 0xF00000000-NSP0_AHB_HIGH, relative_addresses=false, bind = "&hexagon_cluster_0.router.initiator_socket"},
        mapped_base_addr = NSP0_AHB_HIGH;
    };


    addrtr_nsp1_to_app_low = {
        moduletype = "addrtr";
        target_socket = {address=0x0, size=NSP1_BASE, relative_addresses=false, bind = "&hexagon_cluster_1.router.initiator_socket"},
        mapped_base_addr = 0x0;
    };

    addrtr_nsp1_to_tbu = {
        moduletype = "addrtr";
        target_socket = {address=NSP1_AHB_HIGH, size= 0xF00000000-NSP1_AHB_HIGH, relative_addresses=false, bind = "&hexagon_cluster_1.router.initiator_socket"},
        mapped_base_addr = NSP1_AHB_HIGH;
    };

    addrtr_adsp_to_app_low = {
        moduletype = "addrtr";
        target_socket = {address=0x0, size=LPASS_BASE, relative_addresses=false, bind = "&hexagon_cluster_2.router.initiator_socket"},
        mapped_base_addr = 0x0;
    };

     addrtr_adsp_to_tbu = {
        moduletype = "addrtr";
        target_socket = {address=ADSP_AHB_HIGH, size= 0xF00000000-ADSP_AHB_HIGH, relative_addresses=false, bind = "&hexagon_cluster_2.router.initiator_socket"},
        mapped_base_addr = ADSP_AHB_HIGH;
    };

    addrtr_app_to_nsp0 = {
        moduletype = "addrtr";
        initiator_socket = {bind = "&hexagon_cluster_0.router.target_socket"};
        mapped_base_addr = NSP0_BASE;
    };

    addrtr_app_to_nsp1 = {
        moduletype = "addrtr";
        initiator_socket = {bind = "&hexagon_cluster_1.router.target_socket"};
        mapped_base_addr = NSP1_BASE;
    };

    addrtr_app_to_adsp = {
        moduletype = "addrtr";
        initiator_socket = {bind = "&hexagon_cluster_2.router.target_socket"};
        mapped_base_addr = LPASS_BASE;
    };

    sockets = {
        app_ss_to_nsp0_ss_target_socket = "&addrtr_app_to_nsp0.target_socket"; -- to access the address range of the NSP0 subsystem from the APP subsystem
        app_ss_to_nsp1_ss_target_socket = "&addrtr_app_to_nsp1.target_socket";
        app_ss_to_adsp_ss_target_socket = "&addrtr_app_to_adsp.target_socket";
        nsp0_ss_to_app_ss_low_initiator_socket = "&addrtr_nsp0_to_app_low.initiator_socket"; -- to access below NSP0_BASE address range
        nsp0_ss_to_tbu_initiator_socket = "&addrtr_nsp0_to_tbu.initiator_socket"; -- to access the TBU0  addess range
        nsp1_ss_to_app_ss_low_initiator_socket = "&addrtr_nsp1_to_app_low.initiator_socket";
        nsp1_ss_to_tbu_initiator_socket = "&addrtr_nsp1_to_tbu.initiator_socket";
        adsp_ss_to_app_ss_low_initiator_socket = "&addrtr_adsp_to_app_low.initiator_socket";
        adsp_ss_to_tbu_initiator_socket = "&addrtr_adsp_to_tbu.initiator_socket";
        nsp0_ss_l2vic_irq_in_30 = "&hexagon_cluster_0.l2vic.irq_in_30";
        nsp1_ss_l2vic_irq_in_30 = "&hexagon_cluster_1.l2vic.irq_in_30";
        adsp_ss_l2vic_irq_in_153 = "&hexagon_cluster_2.l2vic.irq_in_153";
        -- Support APSS_SHARED_APSS_INTU:CDSP_IPC
        -- 34=(high), 59=(medium), 72=(low)
        nsp0_ss_l2vic_irq_in_34 = "&hexagon_cluster_0.l2vic.irq_in_34";
        nsp0_ss_l2vic_irq_in_59 = "&hexagon_cluster_0.l2vic.irq_in_59";
        nsp0_ss_l2vic_irq_in_72 = "&hexagon_cluster_0.l2vic.irq_in_72";
        nsp0_ss_reset_gpio_reset_in = "&hexagon_cluster_0.reset.reset_in";
        nsp1_ss_reset_gpio_reset_in = "&hexagon_cluster_1.reset.reset_in";
        adsp_ss_reset_gpio_reset_in = "&hexagon_cluster_2.reset.reset_in";
        nsp0_ss_pub_reset = "&hexagon_cluster_0.pub.reset";
        nsp1_ss_pub_reset = "&hexagon_cluster_1.pub.reset";
        adsp_ss_pub_reset = "&hexagon_cluster_2.pub.reset";
    };
};

platform["hex_plugin"] = {
    moduletype = "container_builder";
    config = hex_plugin_configs;
    app_ss_to_nsp0_ss_target_socket = {address=NSP0_BASE , size= NSP0_AHB_HIGH - NSP0_BASE, relative_addresses=false, bind = "&platform.router.initiator_socket"};
    app_ss_to_nsp1_ss_target_socket = {address=NSP1_BASE , size= NSP1_AHB_HIGH - NSP1_BASE, relative_addresses=false, bind = "&platform.router.initiator_socket"};
    app_ss_to_adsp_ss_target_socket = {address=LPASS_BASE , size= (0x03000000 - LPASS_BASE) + 0x100000, relative_addresses=false, bind = "&platform.router.initiator_socket"};
    nsp0_ss_to_app_ss_low_initiator_socket = {bind = "&platform.router.target_socket"}, --nsp0ss
    nsp1_ss_to_app_ss_low_initiator_socket = {bind = "&platform.router.target_socket"}, --nsp1ss
    adsp_ss_to_app_ss_low_initiator_socket = {bind = "&platform.router.target_socket"}, --adsp
};
--
-- This places a stop(r0) insn at the entry address of each CDSP so if no
-- code is loaded the processor will stop.
--
set_dsp_entry_insn();


HEX_RESET_SUB_STR = "&hex_plugin.nsp0_ss_reset_gpio_reset_in;"..
                    "&hex_plugin.nsp1_ss_reset_gpio_reset_in;"..
                    "&hex_plugin.adsp_ss_reset_gpio_reset_in;"..
                    "&hex_plugin.nsp0_ss_pub_reset;"..
                    "&hex_plugin.nsp1_ss_pub_reset;"..
                    "&hex_plugin.adsp_ss_pub_reset;"..
                    "&apss_hm_apss_shared_apss_intu.reset;";

platform.reset.reset.bind = replace_str(platform.reset.reset.bind, "HEX_RESET_SUB_STR", HEX_RESET_SUB_STR)

