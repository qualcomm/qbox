-- Virtual platform soc configuration --
dofile(top() .. "../utils.lua");
print("8755 vsoc Lua config running. . . ");

-- inherit HW base configurations --
dofile_with_args(top() .. "../common_conf/auto_gen4/auto_gen4.lua", "--included")

if rawget(_G, "QLI_PAS_BOOT") == true then
    dofile(top() .. "qli_cdsp_clock_models.lua")
end

-----------------------------------------------------------
--------- override SOC HW specific configurations ---------
-----------------------------------------------------------
tableJoin(platform["load"], {{
    bin_file = valid_file(rawget(_G, "QLI_SMEM_IMAGE") or (top() .. "smem_init.bin")),
    address = INITIAL_DDR_SPACE_14GB + OFFSET_SMEM_DDR_SPACE
}});

tableMerge(platform, {
    gpex_0 = {
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
            size = 0x001fd00000,
            bind = "&router.initiator_socket"
        },
        ecam_iface = {
            address = 0x43B50000,
            size = 0x0010000000,
            bind = "&router.initiator_socket"
        },
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
    }
});

dofile(top() .. "../qup.lua");

if (ARM_NUM_CPUS > 0) then
    local QUP_PITCH = 0x4000;
    local QUP_SIZE = 0x2000;
    -- QUP I2C qup_i.irq: 586, 587 or 357
    -- QUP SPI qup_i.irq: 373 or 584
    local QUP_BANKS = { -- QUPV3_2 / u_qupv3_wrapper_2 / qupv3_se_irq[*]:
    {
        addr = 0x880000,
        qgic_spi = {{
            irq = 373,
            backend = loop_back_backend {
                "spi",
                0,
                master = true
            }
        }, {
            irq = 583,
            backend = char_backend_stdio {
                "uart",
                1,
                rw = false
            }
        }, {
            irq = 584,
            backend = loop_back_backend {
                "spi",
                2,
                master = false
            }
        }, {
            irq = 585,
            backend = char_backend_stdio {
                "uart",
                3,
                rw = false
            }
        }, {
            irq = 586,
            backend = py_i2c_backend {
                "i2c",
                4,
                slave_addr = "8"
            }
        }, -- pass  the python_dir path as -p python_dir=\"/path/to/py-models/\"
        -- {irq = 586, backend = char_backend_stdio{"uart", 4, rw = false}},
        {
            irq = 587,
            backend = char_backend_stdio {
                "uart",
                5,
                rw = false
            }
        }, {
            irq = 833,
            backend = char_backend_stdio {
                "uart",
                6,
                rw = false
            }
        }}
    }, -- QUPV3_0 / u_qupv3_wrapper_0 / qupv3_se_irq[*]:
    {
        addr = 0x980000,
        qgic_spi = {{
            irq = 550,
            backend = char_backend_stdio {
                "uart",
                7,
                rw = false
            }
        }, {
            irq = 551,
            backend = char_backend_stdio {
                "uart",
                8,
                rw = false
            }
        }, {
            irq = 529,
            backend = char_backend_stdio {
                "uart",
                9,
                rw = false
            }
        }, {
            irq = 530,
            backend = char_backend_stdio {
                "uart",
                10,
                rw = false
            }
        }, {
            irq = 531,
            backend = char_backend_stdio {
                "uart",
                11,
                rw = false
            }
        }, {
            irq = 535,
            backend = char_backend_stdio {
                "uart",
                12,
                rw = false
            }
        }, {
            irq = 536,
            backend = char_backend_stdio {
                "uart",
                13,
                rw = false
            }
        }}
    }, -- QUPV3_1 / u_qupv3_wrapper_1 / qupv3_se_irq[*]:
    {
        addr = 0xa80000,
        qgic_spi = {{
            irq = 353,
            backend = char_backend_stdio {
                "uart",
                14,
                rw = false
            }
        }, {
            irq = 354,
            backend = char_backend_stdio {
                "uart",
                15,
                rw = false
            }
        }, {
            irq = 355,
            backend = char_backend_stdio {
                "uart",
                16,
                rw = false
            }
        }, -- {irq = 356, backend = char_backend_stdio{"uart", 17, rw = true}},
        -- {irq = 356, backend = py_uart_backend{"uart", 17}},  -- pass the python_dir path as -p python_dir=\"/path/to/py-models/\"
        {
            irq = 356,
            backend = char_backend_socket {
                "uart",
                17,
                port = 4000
            }
        }, -- to display the output pass -p use_socket=true and open another terminal and connect using "telnet 127.0.0.1 4000" otherwise the char_backend_stdio will be used
        -- {irq = 356, backend = legacy_char_backend_stdio{"uart", 17, args = {true}}}, -- This backend allows the old QUP UART to work on QQVP
                    {
            irq = 357,
            backend = char_backend_stdio {
                "uart",
                18,
                rw = false
            }
        }, {
            irq = 358,
            backend = char_backend_stdio {
                "uart",
                19,
                rw = false
            }
        }, {
            irq = 836,
            backend = char_backend_stdio {
                "uart",
                20,
                rw = false
            }
        }}
    }, -- QUPV3_3 / u_qupv3_wrapper_3 / qupv3_se_irq[*]:
    {
        addr = 0xB80000,
        qgic_spi = {{
            irq = 831,
            backend = char_backend_stdio {
                "uart",
                21,
                rw = false
            }
        }}
    }};

    qup_index = 0;
    qup_insts_concat = "";
    for _, bank in next, QUP_BANKS do
        for i, qup_i in next, bank.qgic_spi do
            local i_0 = i - 1;
            local qup = {
                target_socket = {
                    address = bank.addr + (i_0 * QUP_PITCH),
                    size = QUP_SIZE,
                    bind = "&router.initiator_socket"
                },
                dma_socket = {bind = "&router.target_socket"},
                irq = {
                    bind = "&gic_0.spi_in_" .. qup_i.irq
                }
            };
            platform["backend_" .. tostring(qup_index)] = qup_i.backend
            qup["moduletype"] = "qupv3_qupv3_se_wrapper_se0_" .. qup_i.backend.qup_type
            qup_inst_name = qup_i.backend.qup_type .. "_qup_" .. tostring(qup_index)
            platform[qup_inst_name] = qup
            qup_insts_concat = qup_insts_concat .. "&" .. qup_inst_name .. ".reset;"
            qup_index = qup_index + 1;
        end
    end
    platform.reset.reset.bind = replace_str(platform.reset.reset.bind, "QUP_RESET_SUB_STR", qup_insts_concat)

    platform["backend_router_17"] = {
        moduletype = "biflow_router",
        socket = {
            bind = "&uart_qup_17.backend_socket",
            sendto = "platform.uart_qup_17.backend_socket"
        }
    };
    platform["backend_17"]["biflow_socket"]["bind"] = "&backend_router_17.socket";

end

dofile(top() .. "../common_conf/after_soc_config.lua") -- keep last
print("8775 vsoc Lua config - Done. . . ");
