print("Lua config running. . . ");

dofile(top() .. "../../../../utils.lua")
local ARM_NUM_CPUS = 8;
local NUM_HEXAGON_CLUSTERS = 3

-- QLI/PAS owns CDSP release. qdsp6.lua consumes this while constructing
-- the Hexagon CPU objects, before elaboration.
QLI_PAS_BOOT = true

-- Match the known-good QLI/QQVP CPU setup.  PSCI is handled by the EL3
-- monitor's SMC path; the QQVP CPU model must not install its own conduit.
PSCI_CONDUIT = "disabled"

QLI_SMEM_IMAGE = image_file("smem_init.bin")
dofile_with_args(top() .. "../../../SA8775P_conf.lua", "--included")

-- Disabled while testing the clean boss QBox, which does not provide the
-- thread_safe_reentrant SMMU TBU parameter.
-- platform.tbu_0.thread_safe_reentrant = true

-- simple1.dtb inherits these SMMU IRQs from lemans.dtsi.  The common
-- SA8775P table assumes a contiguous range and is not valid for this QLI
-- device tree.  SMMUv2 maps context-bank N to context interrupt N, after
-- the two global interrupts.  The model has one global output; use the
-- first global IRQ (the model currently does not generate global faults).
platform.smmu_0.irq_global = {bind = "&gic_0.spi_in_119"}
local qli_smmu_context_irqs = {
    0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d,
    0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75,
    0x76, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb,
    0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0x13b, 0x13c, 0x13d,
    0x13e, 0x13f, 0x140, 0x141, 0x142, 0x143, 0x144, 0x145,
    0x146, 0x147, 0x148, 0x149, 0x14a, 0x14b, 0x14c, 0x14d,
    0x14e, 0x14f, 0x150, 0x151, 0x152, 0x153, 0x154, 0x155,
    0x156, 0x157, 0x158, 0x159, 0x18b, 0x18c, 0x18d, 0x18e,
    0x18f, 0x190, 0x191, 0x192, 0x193, 0x194, 0x195, 0x196,
    0x197, 0x198, 0x199, 0x1a2, 0x1a3, 0x19c, 0x1a5, 0x2c2,
    0x1a7, 0x1a8, 0x1a9, 0x2b1, 0x2b2, 0x2b3, 0x2b4, 0x2b5,
    0x2b6, 0x2b7, 0x2b8, 0x19a, 0x19b, 0x1a4, 0x19d, 0x1a6,
    0x2c3, 0x2c4, 0x2c5, 0x2c6, 0x2c7, 0x19e, 0x2c8, 0x2c9,
    0x2ca, 0x2cb, 0x390, 0x38f, 0x38e, 0x38d, 0x38c, 0x38b,
    0x38a, 0x389, 0x388, 0x387, 0x386, 0x385, 0x384, 0x383,
    0x382, 0x381, 0x380, 0x37f, 0x37e, 0x37d, 0x37c, 0x37b,
}
for cb, irq in ipairs(qli_smmu_context_irqs) do
    platform.smmu_0["irq_context_" .. (cb - 1)] = {bind = "&gic_0.spi_in_" .. irq}
end

device_tree = image_file("simple1.dtb")


filesystem_image = image_file("rootfs.ext4")
linux_image = image_file("Image")

local filesystem_file = assert(io.open(filesystem_image, "rb"))
local filesystem_magic = filesystem_file:read(4)
filesystem_file:close()
local filesystem_format = filesystem_magic == "QFI\251" and "qcow2" or "raw"

_KERNEL64_LOAD_ADDR = INITIAL_DDR_SPACE_14GB + 0x01200000
_DTB_LOAD_ADDR = INITIAL_DDR_SPACE_14GB + 0x07600000
_INITRD_LOAD_ADDR = INITIAL_DDR_SPACE_14GB + 0x0A800000
_EL3_MONITOR_ADDR = INITIAL_DDR_SPACE_14GB

dofile(image_file("arm64_bootloader.lua"))

local UART0 = 0x10000000
local QUP_UART_SPIS = 22;

for bank = 0, (QUP_UART_SPIS - 1) do
    platform["backend_" .. tostring(bank)]["read_write"] = false;
end

tableMerge(platform, {
    -- CDSP0 Turing core Lucid-Evo PLLs.  These must provide the MODE
    -- lock/latch status behavior implemented by the QQVP PLL model instead
    -- of falling through to backup register memory.
    turing0_core0_pll = {
        moduletype = "pll";
        socket = {address = 0x26000000, size = 0x1000, bind = "&hex_plugin.hexagon_cluster_0.router.initiator_socket"};
    },
    turing0_core1_pll = {
        moduletype = "pll";
        socket = {address = 0x26001000, size = 0x1000, bind = "&hex_plugin.hexagon_cluster_0.router.initiator_socket"};
    },
    turing0_core2_pll = {
        moduletype = "pll";
        socket = {address = 0x26002000, size = 0x1000, bind = "&hex_plugin.hexagon_cluster_0.router.initiator_socket"};
    },
    -- QDSP6 PLL plus the Q6 core RCGR/CBCR window. The PLL model implements
    -- Lucid-Evo lock/latch state and SA8775 RCGR update completion.
    turing0_qdsp6_pll = {
        moduletype = "pll";
        socket = {address = 0x26340000, size = 0x10000, bind = "&hex_plugin.hexagon_cluster_0.router.initiator_socket"};
    },
    turing0_aon_rcgr = {
        moduletype = "generic_lua_model";
        args = {"&gen_top", "&backup_reg_memory"};
        initiator_socket = {bind = "&router.target_socket"};
        target_socket = {
            address = 0x26008000,
            size = 0x8,
            bind = "&hex_plugin.hexagon_cluster_0.router.initiator_socket"
        };
        registers = {
            {
                name = "turing_aon_cmd_rcgr";
                address = 0x0;
                default = 0x00000000;
                on_pre_read = {{value = 0x00000000, mask = 0x00000001}};
            };
            {
                name = "turing_aon_cfg_rcgr";
                address = 0x4;
                default = 0x00000000;
                on_post_write = {};
            };
        };
    },

    -- CDSP1 uses the corresponding NSP1 PLL and AON RCGR blocks.
    turing1_core0_pll = {
        moduletype = "pll";
        socket = {address = 0x2a000000, size = 0x1000, bind = "&hex_plugin.hexagon_cluster_1.router.initiator_socket"};
    },
    turing1_core1_pll = {
        moduletype = "pll";
        socket = {address = 0x2a001000, size = 0x1000, bind = "&hex_plugin.hexagon_cluster_1.router.initiator_socket"};
    },
    turing1_core2_pll = {
        moduletype = "pll";
        socket = {address = 0x2a002000, size = 0x1000, bind = "&hex_plugin.hexagon_cluster_1.router.initiator_socket"};
    },
    turing1_qdsp6_pll = {
        moduletype = "pll";
        socket = {address = 0x2a340000, size = 0x10000, bind = "&hex_plugin.hexagon_cluster_1.router.initiator_socket"};
    },
    turing1_aon_rcgr = {
        moduletype = "generic_lua_model";
        args = {"&gen_top", "&backup_reg_memory"};
        initiator_socket = {bind = "&router.target_socket"};
        target_socket = {
            address = 0x2a008000,
            size = 0x8,
            bind = "&hex_plugin.hexagon_cluster_1.router.initiator_socket"
        };
        registers = {
            {
                name = "turing1_aon_cmd_rcgr";
                address = 0x0;
                default = 0x00000000;
                on_pre_read = {{value = 0x00000000, mask = 0x00000001}};
            };
            {
                name = "turing1_aon_cfg_rcgr";
                address = 0x4;
                default = 0x00000000;
                on_post_write = {};
            };
        };
    },

    virtioblk_0 = {
        moduletype = "virtio_mmio_blk",
        args = {"&platform.qemu_inst"},
        mem = {
            address = 0x1c0d0000,
            size = 0x2000,
            bind = "&router.initiator_socket"
        },
        irq_out = {
            bind = "&gic_0.spi_in_46"
        },
        blkdev_str = "file=" .. filesystem_image .. ",format=" .. filesystem_format .. ",if=none,readonly=off"
    },

    -- virtioblk_1 = {
    --     moduletype = "virtio_mmio_blk",
    --     args = {"&platform.qemu_inst"},
    --     mem = {
    --         address = 0x1c0e0000,
    --         size = 0x2000,
    --         bind = "&router.initiator_socket"
    --     },
    --     irq_out = {
    --         bind = "&gic_0.spi_in_47"
    --     },
    --     blkdev_str = "file=" .. image_file("cdsp-fw.img") .. ",format=raw,if=none,readonly=off"
    -- },

    charbackend_stdio_0 = {
        moduletype = "char_backend_stdio",
        read_write = true,
        ansi_highlight = ""
    },

    virtionet0_0= {
        moduletype = "virtio_mmio_net",
        args = {"&platform.qemu_inst"};
        mem    =   {
            address=0x1c120000,
            size=0x10000,
            bind = "&router.initiator_socket"
        },
        irq_out = {
            bind = "&gic_0.spi_in_18"
        },
          netdev_str="type=user,hostfwd=tcp::2222-:22"
    },


    pl011_uart_0 = {
        moduletype = "Pl011",
        dylib_path = "uart-pl011",
        target_socket = {
            address = UART0,
            size = 0x1000,
            bind = "&router.initiator_socket"
        },
        irq = {
            bind = "&gic_0.spi_in_379"
        },
        backend_socket = {
            bind = "&charbackend_stdio_0.biflow_socket"
        }
    },

    -- EL3 SCM/PAS bridge. EL3 writes these registers during PAS shutdown
    -- and PAS_AUTH_AND_RESET; the callbacks sequence CDSP core and PUB reset.
    generic_lua_model_0 = {
        moduletype = "generic_lua_model";
        args = {"&gen_top", "&backup_reg_memory"};
        initiator_socket = {bind = "&router.target_socket"};
        system_reset = {bind = "&qemu_inst.reset"};
        target_socket = {address = 0x17d00000, size = 0x10000, bind = "&router.initiator_socket"};
        nsp0_ss_core_reset = {bind = "&hex_plugin.nsp0_ss_reset_gpio_reset_in"};
        nsp1_ss_core_reset = {bind = "&hex_plugin.nsp1_ss_reset_gpio_reset_in"};
        nsp0_ss_pub_reset = {bind = "&hex_plugin.nsp0_ss_pub_reset"};
        nsp1_ss_pub_reset = {bind = "&hex_plugin.nsp1_ss_pub_reset"};
        registers = {};
    }
});

table.insert(platform.generic_lua_model_0.registers, {
    name = "cdsp0_reset_release_el3";
    address = 0xe000;
    on_post_write = {
        -- QuRT stops at start+0x49c if QURT_system_state (start+0x48)
        -- is non-zero.  Clear it at the modeled PAS release boundary so
        -- reset/restart state from an earlier run cannot hold CDSP in crt0.
        {address = 0x9b800048, data = 0x00000000};
        -- Make the PUB registers accessible before the core starts executing
        -- from the EVB loaded by the QEMU reset path.
        {name = "nsp0_ss_pub_reset", output = false};
        {name = "nsp0_ss_core_reset", output = false};
    };
    -- PAS releases the modeled PUB and core resets here. BOOT_CMD below
    -- separately removes the core halt after authentication.
});
table.insert(platform.generic_lua_model_0.registers, {
    name = "cdsp1_reset_release_el3";
    address = 0xe008;
    on_post_write = {
        -- QuRT stops at start+0x49c if QURT_system_state (start+0x48)
        -- is non-zero.  Clear it at the modeled PAS release boundary so
        -- reset/restart state from an earlier run cannot hold CDSP in crt0.
        {address = 0x9d700048, data = 0x00000000};
        -- Release the core last so its first instruction observes a live PUB.
        {name = "nsp1_ss_pub_reset", output = false};
        {name = "nsp1_ss_core_reset", output = false};
    };
    -- Keep the VCPU powered off until BOOT_CMD removes halt after authentication.
});
table.insert(platform.generic_lua_model_0.registers, {
    name = "cdsp_reset_assert";
    address = 0xe004;
    on_post_write = {
        -- Stop both cores before resetting their PUB register blocks.
        {name = "nsp0_ss_core_reset", output = true};
        {name = "nsp1_ss_core_reset", output = true};
        {name = "nsp0_ss_pub_reset", output = true};
        {name = "nsp1_ss_pub_reset", output = true};
    };
});
table.insert(platform.generic_lua_model_0.registers, {
    name = "system_reset";
    address = 0x1ffc;
    on_post_write = {{name = "system_reset", output = true}};
});

-- PSCI mailbox consumed by the EL3 bootloader's secondary-CPU path.
-- Without these registers, mailbox reads fall through to backup_reg_memory
-- (notably offset 0x200) and secondary CPUs can poll forever.
local psci_mailbox_stride = 0x20;
for i=0,(ARM_NUM_CPUS-1) do
    local cpu_name = "cpu_"..tostring(i);
    local signal_name = "cpu_"..tostring(i).."_power_on";
    local mailbox = i * psci_mailbox_stride;
    table.insert(platform.generic_lua_model_0.registers, {
        name = "cpu_"..tostring(i).."_state";
        address = mailbox;
        on_post_write = {{name = signal_name, output = true}};
    });
    for _, item in ipairs({
        {suffix = "entry_lo",  offset = 0x4};
        {suffix = "entry_hi",  offset = 0x8};
        {suffix = "context_lo", offset = 0xc};
        {suffix = "context_hi", offset = 0x10};
    }) do
        table.insert(platform.generic_lua_model_0.registers, {
            name = "cpu_"..tostring(i).."_"..item.suffix;
            address = mailbox + item.offset;
            on_pre_write = {{value = 0, mask = 0}};
        });
    end
    table.insert(platform.generic_lua_model_0.registers, {
        name = "cpu_"..tostring(i).."_power_off";
        address = mailbox + 0x1c;
        on_post_write = {{name = signal_name, output = false}};
    });
    platform[cpu_name].power_on = {bind = "&generic_lua_model_0."..signal_name};
end

reset_string_with_mbox = "&ipcc_0.reset;".."&generic_lua_model_0.reset;"
platform.reset.reset.bind = replace_str(platform.reset.reset.bind, "&ipcc_0.reset;", reset_string_with_mbox)

print("Using arm64 bootloader")

tableJoin(platform["load"], {{
    bin_file = linux_image,
    address = _KERNEL64_LOAD_ADDR
}, {
    bin_file = device_tree,
    address = _DTB_LOAD_ADDR
}, {
    data = _bootloader_aarch64,
    address = INITIAL_DDR_SPACE_14GB
}, {
    -- auto_gen4 loads cmd_db.bin at 0x80860000, but the RIDE DTS exposes
    -- /reserved-memory/aop-cmd-db@90860000. Mirror the same blob there so the
    -- Linux qcom,cmd-db driver reads valid data from the DT-advertised address.
    bin_file = valid_file(top() .. "../../../../common_conf/auto_gen4/cmd_db.bin"),
    address = 0x90860000
}, {
    -- CDSP smem_targ_init() requires the heap metadata prepared by the
    -- platform firmware. Keep this preload; QLI still owns MBN loading.
    data = {0x17f000},
    address = 0x90900000 + 0xC8
}, {
    -- WONCE points the CDSP at its shared-memory target-info structure.
    data = {0x90AFF0E0},
    address = 0x1FD4000
}, {
    data = {0x00000000},
    address = 0x1FD4004
}, {
    -- GDSC status values consumed by the CDSP clock/power init code.
    data = {0x80000000},
    address = 0x26008080
}, {
    data = {0x00010000},
    address = 0x26008084
}, {
    -- TURINGNSP_0_TURING_NSPAUX_GDSCR/CFG_GDSCR.
    data = {0x80000000},
    address = 0x26008200
}, {
    data = {0x00010000},
    address = 0x26008204
}, {
    data = {0x80000000},
    address = 0x2a008080
}, {
    data = {0x00010000},
    address = 0x2a008084
}, {
    -- TURINGNSP_1_TURING_NSPAUX_GDSCR/CFG_GDSCR.
    data = {0x80000000},
    address = 0x2a008200
}, {
    data = {0x00010000},
    address = 0x2a008204
}, {
    -- GCC_TURING_0_GDSCR/CFG_GDSCR.  The CDSP clock driver clears
    -- SW_COLLAPSE and waits for PWR_ON and POWER_UP_COMPLETE.
    data = {0x80000000},
    address = 0x00151000
}, {
    data = {0x00010000},
    address = 0x00151004
}, {
    -- Matching GCC_TURING_1 GDSC state for CDSP1.
    data = {0x80000000},
    address = 0x00122000
}, {
    data = {0x00010000},
    address = 0x00122004
}});

-- QLI remoteproc is the sole CDSP firmware loader. Firmware is intentionally
-- not side-loaded here, so auth_and_reset() remains the first start event.

platform["gpex_0"] = {
    moduletype = "qemu_gpex",
    args = {"&platform.qemu_inst"},
    bus_master = {
        bind = "&router.target_socket"
    },
    pio_iface = {
        address = 0x60200000,
        size = 0x0000100000,
        bind = "&router.initiator_socket"
    },
    mmio_iface = {
        address = 0x60300000,
        size = 0x1fd00000,
        bind = "&router.initiator_socket"
    },
    ecam_iface = {
        address = 0x40000000,
        size    = 0x10000000,  -- 256MB
        bind    = "&router.initiator_socket"
    },
    -- ecam_iface = {
    --     address = 0x43B50000,
    --     size = 0x0010000000,
    --     bind = "&router.initiator_socket"
    -- },
    mmio_iface_high = {
        address = 0x400000000,
        size = 0x200000000,
        bind = "&router.initiator_socket"
    },
    irq_out_0 = {
        bind = "&gic_0.spi_in_541"
    },
    irq_out_1 = {
        bind = "&gic_0.spi_in_542"
    },
    irq_out_2 = {
        bind = "&gic_0.spi_in_543"
    },
    irq_out_3 = {
        bind = "&gic_0.spi_in_544"
    }
};

-- PCIe root complex (generic ECAM) matching DT: pcie@40000000
-- platform.pcie = {
--     type = "qemu_gpex",

--     -- ECAM: 256MB @ 0x40000000
--     ecam_iface = {
--         address = 0x40000000,
--         size    = 0x10000000,
--     },

--     -- IO window: 1MB @ 0x60200000
--     io_iface = {
--         address = 0x60200000,
--         size    = 0x00100000,
--     },

--     -- non-prefetchable MMIO: 0x60300000..0x7fffffff
--     mmio_iface = {
--         address = 0x60300000,
--         size    = 0x1fd00000,
--     },

--     -- prefetchable 64-bit MMIO (optional but recommended since DT now advertises it)
--     mmio_iface_high = {
--         address = 0x400000000,
--         size    = 0x200000000,
--     },

--     -- legacy INTx -> SPIs 541-544 (matches DT interrupt-map)
--     irq = { 541, 542, 543, 544 },
-- }

-- platform.virtgpu = {
--     type = "virtio_gpu_gl_pci",
--     pcie = platform.pcie,
-- }

platform.display = { type = "sdl", gl = true }

-- gpu = {
--   moduletype = "virtio_gpu_gl_pci";
--   args = {"&platform.qemu_inst", "&platform.pcie"};
-- }
-- gpu = {
--     moduletype = "virtio_gpu_gl_pci";
--     args = {"&platform.qemu_inst", "&platform.gpex_0"};
-- }
-- display = {
--     moduletype = "display";
--     args = {"&platform.gpu_0"};
-- }
-- platform.gpex_0 = platform.pcie;


                -- gpu = {
                --     moduletype = "virtio_gpu_gl_pci";
                --     args = {"&platform.qemu_inst", "&platform.gpex_0"};
                -- }
print("kernel is loaded at: 0x" .. string.format("%x", _KERNEL64_LOAD_ADDR));
print("dtb is loaded at:    0x" .. string.format("%x", _DTB_LOAD_ADDR));
print("initrd is loaded at: 0x" .. string.format("%x", _INITRD_LOAD_ADDR));

for i=0,(ARM_NUM_CPUS-1) do
    platform["cpu_"..tostring(i)]["has_el3"] = true;
    -- LeMans DT topology: four cores per cluster, with Aff1 in bits 16
    -- and Aff0 in bits 8..15.
    platform["cpu_"..tostring(i)]["mp_affinity"] = (math.floor(i / 4) << 16) | ((i % 4)<<8);
    if i ~= 0 then
        platform["cpu_"..tostring(i)]["start_powered_off"] = true;
    end
    platform["cpu_"..tostring(i)]["rvbar"] = _EL3_MONITOR_ADDR;
end

-- Hexagon DSPs are enabled: hexagon_thread_0 starts powered on (default from qdsp6.lua)
-- To disable Hexagon, pass --param platform.disable_hexagon=true at runtime

for i = 0, (ARM_NUM_CPUS - 1) do
    platform["cpu_" .. tostring(i)]["has_el3"] = true;
end
