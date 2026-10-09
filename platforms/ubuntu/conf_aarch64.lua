-- Virtual platform configuration

function top()
    local str = debug.getinfo(2, "S").source:sub(2)
    if str:match("(.*/)")
    then
        return str:match("(.*/)")
    else
        return "./"
    end
 end

dofile(top().."../fw/utils.lua");
print ("Lua config running. . . ");

INITIAL_DDR_SPACE = 0x80000000

_KERNEL64_LOAD_ADDR = INITIAL_DDR_SPACE + 0x01200000
_DTB_LOAD_ADDR      = INITIAL_DDR_SPACE + 0x07600000
_INITRD_LOAD_ADDR   = INITIAL_DDR_SPACE + 0x0A800000

dofile (top().."fw/arm64_bootloader.lua")

local HEX_DIGITS='0123456789ABCDEF'

local IPC_ROUTER_TOP = 0x00400000

local APSS_GIC600_GICD_APSS = 0x17A00000
local OFFSET_APSS_ALIAS0_GICR_CTLR = 0x60000

local UART0 = 0x10000000

local ARM_NUM_CPUS = 8;
local NUM_GPUS = 0;

local IS_SHARED_MEM = false

if ACCEL == nil then
    ACCEL = "tcg"
end
print("Virtual acceleration: " .. ACCEL)

local ARCH_TIMER_VIRT_IRQ = 16 + 11
local ARCH_TIMER_S_EL1_IRQ = 16 + 13
local ARCH_TIMER_NS_EL1_IRQ = 16 + 14
local ARCH_TIMER_NS_EL2_IRQ = 16 + 10

ARM_NUM_CPUS = 8;
local NUM_REDISTS = 1;

platform = {

    moduletype="Container";

    quantum_ns = 10000000;

    router = {
        moduletype="router";
        log_level=0;
    },

    ram_0=  {
        moduletype="gs_memory";
        target_socket = {address = INITIAL_DDR_SPACE; size = 0x100000000, bind= "&router.initiator_socket"},
        log_level=0,
        shared_memory=IS_SHARED_MEM};

    qemu_inst_mgr = {
        moduletype = "QemuInstanceManager";
    },

    qemu_inst= {
        moduletype="QemuInstance";
        args = {"&platform.qemu_inst_mgr", "AARCH64"};
        accel = ACCEL,
        tcg_mode="MULTI",
        sync_policy = "multithread-unconstrained"
    },

    gpex_0 ={
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

    gic_0 =  {
        moduletype = "arm_gicv3",
        args = {"&platform.qemu_inst"},
        dist_iface    = {address=APSS_GIC600_GICD_APSS, size= OFFSET_APSS_ALIAS0_GICR_CTLR, bind = "&router.initiator_socket"};
        redist_iface_0= {address=APSS_GIC600_GICD_APSS+OFFSET_APSS_ALIAS0_GICR_CTLR, size=0x1C0000, bind = "&router.initiator_socket"};
        num_cpus = ARM_NUM_CPUS,
        redist_region = {ARM_NUM_CPUS / NUM_REDISTS};
        num_spi=960
    };

    virtionet0_0= {
        moduletype = "virtio_mmio_net",
        args = {"&platform.qemu_inst"};
        mem    =   {address=0x1c120000, size=0x10000, bind = "&router.initiator_socket"},
        irq_out = {bind = "&gic_0.spi_in_18"},
        netdev_str="type=user,hostfwd=tcp::2222-:22,hostfwd=tcp::2221-:21,hostfwd=tcp::56283-:56283,hostfwd=tcp::55534-:65534,hostfwd=tcp::55535-:65535"};

    virtioblk_0 = {
        moduletype="virtio_mmio_blk",
        args = {"&platform.qemu_inst"};
        mem = { address = 0x1c0d0000, size = 0x2000, bind = "&router.initiator_socket"},
        irq_out = {bind = "&gic_0.spi_in_46"},
        blkdev_str = "file="..top().."fw/Artifacts/image_ext4.img"..",format=raw,if=none,readonly=off" };

    charbackend_stdio_0 = {
        moduletype = "char_backend_stdio";
        read_write = true;
    };

    pl011_uart_0 = {
        moduletype = "Pl011",
        dylib_path = "uart-pl011",
        target_socket = {address= UART0, size=0x1000, bind = "&router.initiator_socket"},
        irq = {bind = "&gic_0.spi_in_379"},
        backend_socket = {bind = "&charbackend_stdio_0.biflow_socket"},
    };

    global_peripheral_initiator_arm_0 = {
            moduletype = "global_peripheral_initiator",
            args = {"&platform.qemu_inst", "&platform.cpu_0"},
            global_initiator = {bind = "&router.target_socket"},
    };

    fallback_0=  {
        moduletype="gs_memory";
        target_socket = {address = 0x0; size = 0x800000000, bind= "&router.initiator_socket", priority=1},
        dmi_allow=false,
        log_level=0,
        shared_memory=IS_SHARED_MEM};

    load={
        moduletype = "loader",
        initiator_socket = {bind = "&router.target_socket"};
        { bin_file=top().."fw/Artifacts/Image.bin", address=_KERNEL64_LOAD_ADDR };
        { bin_file=top().."fw/Artifacts/ubuntu.dtb", address=_DTB_LOAD_ADDR };
        { bin_file=top().."fw/Artifacts/image_ext4_initrd.img", address= _INITRD_LOAD_ADDR };
        { data=_bootloader_aarch64, address = INITIAL_DDR_SPACE};
    };
};

print ("kernel is loaded at: 0x"..string.format("%x",_KERNEL64_LOAD_ADDR));
print ("dtb is loaded at:    0x"..string.format("%x",_DTB_LOAD_ADDR));
print ("initrd is loaded at: 0x"..string.format("%x",_INITRD_LOAD_ADDR));

if (ARM_NUM_CPUS > 0) then

    local psci_conduit = "smc";
    if ACCEL == "kvm" then
        psci_conduit = "hvc";
    end
    print("PSCI conduit: "..psci_conduit)

    for i=0,(ARM_NUM_CPUS-1) do
        local cpu = {
            moduletype = "cpu_arm_cortexA76";
            args = {"&platform.qemu_inst"};
            mem = {bind = "&router.target_socket"};
            has_el3 = false;
            has_el2 = false;
            irq_timer_phys_out = {bind = "&gic_0.ppi_in_cpu_"..i.."_"..ARCH_TIMER_NS_EL1_IRQ},
            irq_timer_virt_out = {bind = "&gic_0.ppi_in_cpu_"..i.."_"..ARCH_TIMER_VIRT_IRQ},
            irq_timer_hyp_out = {bind = "&gic_0.ppi_in_cpu_"..i.."_"..ARCH_TIMER_NS_EL2_IRQ},
            irq_timer_sec_out = {bind = "&gic_0.ppi_in_cpu_"..i.."_"..ARCH_TIMER_S_EL1_IRQ},
            gicv3_maintenance_interrupt = {bind = "&gic_0.ppi_in_cpu_"..i.."_25"},
            pmu_interrupt = {bind = "&gic_0.ppi_in_cpu_"..i.."_23"},
            psci_conduit = psci_conduit,
            mp_affinity = (math.floor(i / 8) << 8) | (i % 8);
            -- reset = { bind = "&reset.reset" },
            start_powered_off = true;
            rvbar = INITIAL_DDR_SPACE;
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
