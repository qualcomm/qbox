-- Virtual platform configuration

local args = {...}
if args[1] ~= "--included" then
  print("You should not invoke the vp passing the SoC lua config. Instead,\n"..
         "provide the BSP config, which will automatically load this file.")
  os.exit(1)
end

dofile(top().."../../utils.lua");
print ("Lua config running. . . ");

-- Luafile version

PLATFORM_MAX_ADDR_SIZE = 0x1000000000

INITIAL_DDR_SPACE_14GB = 0x80000000
CMD_DB_HEADER_LOAD_ADDR = 0x0C3F0000
CMD_DB_BIN_LOAD_ADDR = 0x80860000

local HEX_DIGITS='0123456789ABCDEF'
local OEM_QUALCOMM = 0x0
local PRODUCT_ID_SA8775 = 0x0

JTAG_ID_ADDR = 0x00786130
local SERIAL_NUM_ADDR = 0x00786134
local OEM_ID_ADDR = 0x00786138

local IPC_ROUTER_TOP = 0x00400000

local APSS_GIC600_GICD_APSS = 0x17A00000
local OFFSET_APSS_ALIAS0_GICR_CTLR = 0x60000

-- local UART0 = 0x10000000

local TCSR_SOC_HW_VERSION_ADDR = 0x1FC8000;
local TCSR_SOC_EMULATION_TYPE_ADDR = TCSR_SOC_HW_VERSION_ADDR+4;
SMEM_TCSR_TZ_WONCE_ADDR=0x01fd4000;
SMEM_TARG_INFO_ADDR=0x90aff320; -- must correspond to smem*.bin
local RPMH_PDC_COMPUTE_PDC_PARAM_RESOURCE_DRVd = 0xb2c1004;
local RPMH_PDC_COMPUTE_PDC_PARAM_SEQ_CONFIG_DRVd = 0xb2c1008;
local RPMH_PDC_NSP_PDC_PARAM_RESOURCE_DRVd = 0xb2f1004;
local RPMH_PDC_NSP_PDC_PARAM_SEQ_CONFIG_DRVd = 0xb2f1008;
local TURING_SS_0TURING_QDSP6SS_RSCC_RSC_PARAM_RSC_CONFIG_DRVd = 0x20CB0008;
local TURING_SS_0TURING_RSCC_RSC_PARAM_RSC_CONFIG_DRVd = 0x208A4008;
local TURING_SS_1TURING_QDSP6SS_RSCC_RSC_PARAM_RSC_CONFIG_DRVd = 0x263B0008;
local TURING_SS_1TURING_RSCC_RSC_PARAM_RSC_CONFIG_DRVd = 0x260A4008;
local HWKM_MASTER_CFG_KW_RNG_EEx_base=0x010D0000;

local LPASS_QDSP6SS_RSCC_RSC_PARAM_RSC_CONFIG_DRVd = 0x30B0008;
local LPASS_RSCC_RSC_PARAM_RSC_CONFIG_DRVd = 0x3480008;

local IS_SHARED_MEM = false

-- these values are helpful for local configuration, so make them global
 OFFSET_MIFS_DDR_SPACE  = 0x60000000
 OFFSET_SMEM_DDR_SPACE  = 0x10900000

if ACCEL == nil then
    ACCEL = GET("ACCEL");
end
if ACCEL == nil then
    ACCEL = "tcg"
end
print("Virtual acceleration: " .. ACCEL)

if ACCEL ~= "tcg" then
    print("Loading arm64 bootloader")
    _KERNEL64_LOAD_ADDR = INITIAL_DDR_SPACE_14GB + OFFSET_MIFS_DDR_SPACE
    local ARM64_BOOTLOADER = valid_file(top() .. "../../arm64_bootloader.lua")
    dofile(ARM64_BOOTLOADER)
end

if (ACCEL == "kvm") then
    kvm_discrim = {data={0x5}, address=0x17000000};
else
    kvm_discrim = {};
end

local ARCH_TIMER_VIRT_IRQ = 16 + 11
local ARCH_TIMER_S_EL1_IRQ = 16 + 13
local ARCH_TIMER_NS_EL1_IRQ = 16 + 14
local ARCH_TIMER_NS_EL2_IRQ = 16 + 10

ARM_NUM_CPUS = 8;
local NUM_REDISTS = 1;

platform = {
    qmp = {
        moduletype = "qmp";
        args = {"&platform.qemu_inst"};
        monitor = true;
        refresh_interval_ms=100;
        qmp_str = "unix:qmp.sock,server,nowait";
    };

    qmp_json = {
        moduletype = "qmp";
        args = {"&platform.qemu_inst"};
        monitor = false;
        qmp_str = "unix:qmp_json.sock,server,nowait";
    };


    monitor = {
        moduletype = "monitor";
        server_port = 18088;
    };

    moduletype="Container";

    quantum_ns = 10000000;

    gen_top = {
        moduletype = "SA8775P";
        args = {"&platform.router", "&platform.backup_reg_memory"};
        zipfile = valid_file(top().."../../SA8775P.cc_config.zip");
        shared_platform_memories = IS_SHARED_MEM;
    },

    router = {
        moduletype="router";
        log_level=0;
        lazy_init = true;
    },

    backup_reg_memory = {
        moduletype = "gs_memory";
        target_socket={address=0, size= PLATFORM_MAX_ADDR_SIZE};
    },

    qemu_inst_mgr = {
        moduletype = "QemuInstanceManager";
    },

    qemu_inst= {
        moduletype="QemuInstance";
        args = {"&platform.qemu_inst_mgr", "AARCH64"};
        accel = ACCEL,
        tcg_mode="MULTI",
        sync_policy = "multithread-unconstrained",
        time_sync_strategy = "quantum_keeper"; -- to switch to mcips replace quantum_keeper by mcips
    },

    gic_0 =  {
                moduletype = "arm_gicv3",
                args = {"&platform.qemu_inst"},
                dist_iface    = {address=APSS_GIC600_GICD_APSS, size= OFFSET_APSS_ALIAS0_GICR_CTLR, bind = "&router.initiator_socket"};
                redist_iface_0= {address=APSS_GIC600_GICD_APSS+OFFSET_APSS_ALIAS0_GICR_CTLR, size=0x100000, bind = "&router.initiator_socket"};
                num_cpus = ARM_NUM_CPUS,
                redist_region = {ARM_NUM_CPUS / NUM_REDISTS};
                num_spi=960
            };
    virtionet0_0= {
                    moduletype = "virtio_mmio_net",
                    args = {"&platform.qemu_inst"};
                    mem    =   {address=0x1c120000, size=0x10000, bind = "&router.initiator_socket"},
                    irq_out = {bind = "&gic_0.spi_in_18"},
                    netdev_str="type=user,hostfwd=tcp::2222-:22,hostfwd=tcp::2221-:21"};

    rtl8139_0= {
        moduletype = "rtl8139_pci",
        args = {"&platform.qemu_inst", "&platform.gpex_0"},
        netdev_str = "type=user",
    };

    mpm_0 = {
        moduletype="mpm_control",
        socket = {address=0x0C210000, size=0x1000, bind="&router.initiator_socket"},
    };

    qtimer_0=   {
                    moduletype = "qemu_hexagon_qtimer",
                    args = {"&platform.qemu_inst"};
                    mem     =   {address=0x17c20000, size=0x1000, bind = "&router.initiator_socket"};
                    mem_view=   {address=0x17c21000, size=0x1000*2*7, bind = "&router.initiator_socket"}; -- 0x1000*nr_frames*nr_views
                    irq_0 = {bind = "&gic_0.spi_in_40"},
                    irq_1 = {bind = "&gic_0.spi_in_41"},
                    irq_2 = {bind = "&gic_0.spi_in_42"},
                    irq_3 = {bind = "&gic_0.spi_in_43"},
                    irq_4 = {bind = "&gic_0.spi_in_44"},
                    irq_5 = {bind = "&gic_0.spi_in_45"},
                    irq_6 = {bind = "&gic_0.spi_in_46"},
                    nr_frames=7;nr_views=2;cnttid_0=0x1111515};

    -- charbackend_stdio_0 = {
    --     moduletype = "CharBackendStdio";
    --     args = {false};
    -- };

    -- pl011 = {
    --     moduletype = "Pl011",
    --     args = {"&platform.charbackend_stdio_0"};
    --     target_socket = {address= UART0,
    --                               size=0x1000,
    --                               bind = "&router.initiator_socket"},
    --     irq = {bind = "&gic_0.spi_in_379"},
    -- };

    ipcc_0= {
            moduletype="ipcc",
            target_socket = {address=IPC_ROUTER_TOP, size=0xfc000, bind = "&router.initiator_socket"},
            irq_8 = {bind = "&gic_0.spi_in_229"},
            irq_18 = {bind = "&hex_plugin.nsp1_ss_l2vic_irq_in_30"},
            irq_6 = {bind = "&hex_plugin.nsp0_ss_l2vic_irq_in_30"},
            irq_3 = {bind = "&hex_plugin.adsp_ss_l2vic_irq_in_153"},
        };
    -- mpm = { socket = {address=0x0C210000, size=0x1000}};

    gpex_0 = {
        moduletype = "qemu_gpex";
        args = {"&platform.qemu_inst"};
        bus_master = {bind = "&router.target_socket"};
        pio_iface = { address = 0x60200000, size = 0x0000100000, bind= "&router.initiator_socket"};
        mmio_iface = { address = 0x60300000, size = 0x001fd00000, bind= "&router.initiator_socket" };
        ecam_iface = { address = 0x43B50000, size = 0x0010000000, bind= "&router.initiator_socket" };
        mmio_iface_high = { address = 0x400000000, size = 0x200000000, bind= "&router.initiator_socket" },

        irq_out_0 = {bind = "&gic_0.spi_in_541"};
        irq_out_1 = {bind = "&gic_0.spi_in_542"};
        irq_out_2 = {bind = "&gic_0.spi_in_543"};
        irq_out_3 = {bind = "&gic_0.spi_in_544"};
    };

    usb_0 = {
        moduletype = "qemu_xhci",
        args = {"&platform.qemu_inst", "&platform.gpex_0"},
    };

    kbd_0 = {
        moduletype = "qemu_kbd",
        args = {"&platform.qemu_inst", "&platform.usb_0"},
    };

    tablet_0 = {
        moduletype = "qemu_tablet",
        args = {"&platform.qemu_inst", "&platform.usb_0"},
    };

    -- qtb = { control_socket = {address=0x15180000, size=0x80000}};
    -- The QTB model is not active in the system, left here for debug purposes.

    -- Turing_NSP_0, ARID 14, AC_VM_CDSP_Q6_ELF:

    tbu_0 = {
        moduletype = "smmu500_tbu",
        args = {"&platform.smmu_0"},
        topology_id=0x21C0,
        upstream_socket = {bind = "&hex_plugin.nsp0_ss_to_tbu_initiator_socket"},
        downstream_socket = {bind = "&router.target_socket"};
      };

    tbu_1 = {
       moduletype = "smmu500_tbu",
       args = {"&platform.smmu_0"},
       topology_id=0x29C0,
       upstream_socket = {bind = "&hex_plugin.nsp1_ss_to_tbu_initiator_socket"},
       downstream_socket = {bind = "&router.target_socket"};
      };

    tbu_2 = {
        moduletype = "smmu500_tbu",
        args = {"&platform.smmu_0"},
        topology_id=0x3000,
        upstream_socket = {bind = "&hex_plugin.adsp_ss_to_tbu_initiator_socket"},
        downstream_socket = {bind = "&router.target_socket"};
       };

    memory_dumper_0 = {
        moduletype = "memory_dumper",
        initiator_socket = {bind = "&router.target_socket"};
        target_socket={address=0x1B300040,
                        size=0x10,
                        bind = "&router.initiator_socket"}
            },


    global_peripheral_initiator_arm_0 = {
            moduletype = "global_peripheral_initiator",
            args = {"&platform.qemu_inst", "&platform.cpu_0"},
            global_initiator = {bind = "&router.target_socket"},
    };

    reset_qemu = {
        moduletype = "reset_gpio",
        args = { "&platform.qemu_inst" },
        reset_out = {bind="&reset.reset_in"}
    };

    reset = {
        moduletype = "aoss_pmic_arb_pmic_arb_core";
        args={"&router", "&backup_reg_memory"};
        target_socket = {address=0xC600000, size=0x2000000, relative_addresses=false, bind = "&router.initiator_socket";},
        reset = { bind = "HEX_RESET_SUB_STR".. -- substitute this entry with the hex_plugin reset signals string
        "QUP_RESET_SUB_STR".. -- substitute this entry with the concatenated qup reset signals in platforms conf.lua
        "&reset_qemu.reset_in;"..
        "&ipcc_0.reset;"..
        "&smmu_0.reset;"..
        "&smmu_1.reset;"..
        "&smmu_2.reset;"..
        "&gen_top.reset;"..
        "&backup_reg_memory.reset;"..
        "&load.reset"
        };
    };

    rtl = {
        moduletype="realtimelimiter";
        log_level=4;
        SCTimeout_ms=180000;
    };
    timeprinter = {
        moduletype="timeprinter";
    };

    apss_hm_apss_shared_apss_intu = {
        moduletype = "apss_hm_apss_shared_apss_intu",
        target_socket = {address = 0x17c00000, size=0x10000, bind="&router.initiator_socket"},
        cdsp_ipc_0 = {bind = "&hex_plugin.nsp0_ss_l2vic_irq_in_72"},
        cdsp_ipc_1 = {bind = "&hex_plugin.nsp0_ss_l2vic_irq_in_59"},
        cdsp_ipc_2 = {bind = "&hex_plugin.nsp0_ss_l2vic_irq_in_34"},
    };

    load={
        moduletype = "loader",
        initiator_socket = {bind = "&router.target_socket"};
        {data={0x60190200}, address=TCSR_SOC_HW_VERSION_ADDR};
        {data={0x04}, address=TCSR_SOC_EMULATION_TYPE_ADDR};
        {data={SMEM_TARG_INFO_ADDR}, address=SMEM_TCSR_TZ_WONCE_ADDR};
        {data={0x00005381},
         address=RPMH_PDC_COMPUTE_PDC_PARAM_RESOURCE_DRVd};
        {data={0x00180411},
         address=RPMH_PDC_COMPUTE_PDC_PARAM_SEQ_CONFIG_DRVd};
        {data={0x00005381},
         address=RPMH_PDC_NSP_PDC_PARAM_RESOURCE_DRVd};
        {data={0x00180411},
         address=RPMH_PDC_NSP_PDC_PARAM_SEQ_CONFIG_DRVd};
        {data={0x01300214},
         address=TURING_SS_0TURING_QDSP6SS_RSCC_RSC_PARAM_RSC_CONFIG_DRVd};
        {data={0x01180214},
         address=TURING_SS_0TURING_RSCC_RSC_PARAM_RSC_CONFIG_DRVd};
        {data={0x01300214},
         address=TURING_SS_1TURING_QDSP6SS_RSCC_RSC_PARAM_RSC_CONFIG_DRVd};
        {data={0x01180214},
         address=TURING_SS_1TURING_RSCC_RSC_PARAM_RSC_CONFIG_DRVd};
        {data={0x01300214},
         address=LPASS_QDSP6SS_RSCC_RSC_PARAM_RSC_CONFIG_DRVd};
        {data={0x01180214},
         address=LPASS_RSCC_RSC_PARAM_RSC_CONFIG_DRVd};
        -- RPMH_PDC_AUDIO_PDC_PARAM_RESOURCE_DRVd
        {data={0x000A5381}, address=0xB251004};

        -- SECURITY_CONTROL_CORE
            -- JTAG_ID
        {data={0x001D20E1}, address=JTAG_ID_ADDR},
            -- SERIAL_NUM
        {data={0x12345678}, address=SERIAL_NUM_ADDR},
            -- OEM_ID
        {data={(OEM_QUALCOMM << 16) | PRODUCT_ID_SA8775}, address=OEM_ID_ADDR},

        -- GCC_QUPV3_WRAP0_S3_CFG_RCGR
        {data={0x2601}, address=0x1234f4};
        -- GCC_QUPV3_WRAP0_S3_M
        {data={0x180}, address=0x1234f8};
        -- GCC_QUPV3_WRAP0_S3_N
        {data={0xc476}, address=0x1234fc};
        -- GCC_QUPV3_WRAP0_S3_D
        {data={0xc2f6}, address=0x123500};
        kvm_discrim,

--      {data={0x00020400}, address=0x10E0000};
--      {data={0x60000003}, address=0x10E000C};

        -- GCC_TURING_1_CFG_AHB_CBCR
        {data={0x00000000}, address=0x122028};
        -- GCC_TURING_0_CFG_AHB_CBCR
        {data={0x00000000}, address=0x151028};
        -- GCC_HYP_VOTE_QDSS_APB_CLK
        {data={0x00000000}, address=0x175100};
        -- GCC_HYP_VOTE_MMU_TCU_CLK
        {data={0x00000000}, address=0x18c02c};
        -- GPU SMMU GDS power on BIT
        {data={0x00000000}, address=0x18c0a8};
        {data={0x00000000}, address=0x3d96000};
        {data={0x80000000}, address=0x3d96004};
        -- TURINGNSP_0_TURING_VAPSS_GDSCR :: POWER ON BIT
        {data={0x80000000}, address=0x26008080};
        -- TURINGNSP_1_TURING_VAPSS_GDSCR :: POWER ON BIT
        {data={0x80000000}, address=0x2a008080};

        { bin_file = valid_file(top() .. "cmd_db_header.bin"), address = CMD_DB_HEADER_LOAD_ADDR};
        { bin_file = valid_file(top() .. "cmd_db.bin"), address = CMD_DB_BIN_LOAD_ADDR};
    };
};

-- PRNG values for HWKM_MASTER_CFG_KM_RNG_EE{2 to 15}
local PRNG_table = {}
for id = 3, #HEX_DIGITS do
  local data_addr = HWKM_MASTER_CFG_KW_RNG_EEx_base +
                    tonumber("0x" .. HEX_DIGITS:sub(id,id) .. "000")
  local status_addr = data_addr + 0x4
  table.insert(PRNG_table, {data={0x4}, address=data_addr})
  table.insert(PRNG_table, {data={0x01}, address=status_addr})
end
tableJoin(platform["load"], PRNG_table)

local irq_context = 103
local irq_global = 65
local num_cb = 128

local smmu_index = 0;

local smmu = {
    moduletype = "smmu500",
    dma = {bind = "&router.target_socket"},
    target_socket = {address=0x15000000, size=0x100000, bind= "&router.initiator_socket"},
    num_tbu = 2,
    num_pages = 128,
    num_cb = num_cb,
    num_smr = 224,
    irq_global = {bind = "&gic_0.spi_in_"..irq_global},
}
for i = 0, num_cb - 1 do
    smmu["irq_context_" .. i] = {bind = "&gic_0.spi_in_" .. irq_context + i}
end
platform["smmu_"..tostring(smmu_index)] = smmu;


irq_context = 896
irq_global = 920
num_cb = 64
smmu_index = 1;

smmu = {
    moduletype = "smmu500",
    dma = {bind = "&router.target_socket"},
    target_socket = {address=0x15200000, size=0x100000, bind= "&router.initiator_socket"},
    num_tbu = 0,
    num_pages = 64,
    num_cb = num_cb,
    num_smr = 64,
    irq_global = {bind = "&gic_0.spi_in_"..irq_global},
}

for i = 0, num_cb - 1 do
    smmu["irq_context_" .. i] = {bind = "&gic_0.spi_in_" .. irq_context + i}
end
platform["smmu_"..tostring(smmu_index)] = smmu;

irq_context = 678
irq_global = 672
num_cb = 10
smmu_index = 2;

smmu = {
    moduletype = "smmu500",
    dma = {bind = "&router.target_socket"},
    target_socket = {address=0x3DA0000, size=0x100000, bind= "&router.initiator_socket"},
    num_tbu = 0,
    num_pages = 16,
    num_cb = num_cb,
    num_smr = 10,
    irq_global = {bind = "&gic_0.spi_in_"..irq_global},
}
for i = 0, num_cb - 1 do
    smmu["irq_context_" .. i] = {bind = "&gic_0.spi_in_" .. irq_context + i}
end
platform["smmu_"..tostring(smmu_index)] = smmu;

if (ARM_NUM_CPUS > 0) then
    local psci_conduit = "smc";
    local has_el2 = false;
    if ACCEL == "kvm" then
        psci_conduit = "hvc";
    elseif ACCEL == "tcg" then
        if PSCI_CONDUIT ~= nil then
            psci_conduit = PSCI_CONDUIT;
        end
        if HAS_EL2 ~= nil then
            has_el2 = HAS_EL2;
        end
    end
    print("PSCI conduit: "..psci_conduit)
    print("HAS EL2: "..tostring(has_el2))

    for i=0,(ARM_NUM_CPUS-1) do
        local cpu = {
            moduletype = "cpu_arm_cortexA76";
            args = {"&platform.qemu_inst"};
            mem = {bind = "&router.target_socket"};
            has_el3 = (ACCEL == "tcg");
            has_el2 = has_el2;
            irq_timer_phys_out = {bind = "&gic_0.ppi_in_cpu_"..i.."_"..ARCH_TIMER_NS_EL1_IRQ},
            irq_timer_virt_out = {bind = "&gic_0.ppi_in_cpu_"..i.."_"..ARCH_TIMER_VIRT_IRQ},
            irq_timer_hyp_out = {bind = "&gic_0.ppi_in_cpu_"..i.."_"..ARCH_TIMER_NS_EL2_IRQ},
            irq_timer_sec_out = {bind = "&gic_0.ppi_in_cpu_"..i.."_"..ARCH_TIMER_S_EL1_IRQ},
            gicv3_maintenance_interrupt = {bind = "&gic_0.ppi_in_cpu_"..i.."_25"},
            pmu_interrupt = {bind = "&gic_0.ppi_in_cpu_"..i.."_23"},
            psci_conduit = psci_conduit,
            mp_affinity = (math.floor(i / 8) << 8) | (i % 8);
            start_powered_off = (psci_conduit ~= "disabled");
            rvbar = INITIAL_DDR_SPACE_14GB;
            cntfrq_hz = 62500000,
            insn_per_second =  1000000000;
        };
        if (i==0) then
            cpu["start_powered_off"] = false;
        end
        platform["cpu_"..tostring(i)]=cpu;

        platform["gic_0"]["irq_out_" .. i] = {bind="&cpu_"..i..".irq_in"}
        platform["gic_0"]["fiq_out_" .. i] = {bind="&cpu_"..i..".fiq_in"}
        platform["gic_0"]["virq_out_" .. i] = {bind="&cpu_"..i..".virq_in"}
        platform["gic_0"]["vfiq_out_" .. i] = {bind="&cpu_"..i..".vfiq_in"}
    end
end

if GET("platform.disable_hexagon") == false or GET("platform.disable_hexagon") == nil
then
    local NSP_CONF = valid_file(top() .. "nsp_conf.lua")
    dofile(NSP_CONF)
else
    platform.reset.reset.bind = replace_str(platform.reset.reset.bind, "HEX_RESET_SUB_STR", "")
end

