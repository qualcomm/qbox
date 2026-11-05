-- QLI/PAS starts CDSP from an uninitialized virtual clock state.  Model the
-- status handshakes used by the Lemans CDSP clock driver instead of bypassing
-- those transitions in DSP firmware.
tableMerge(platform, {
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
                name = "turing0_aon_cmd_rcgr";
                address = 0x0;
                default = 0;
                on_pre_read = {{value = 0, mask = 0x1}};
            };
            {
                name = "turing0_aon_cfg_rcgr";
                address = 0x4;
                default = 0;
                on_post_write = {};
            };
        };
    },

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
                default = 0;
                on_pre_read = {{value = 0, mask = 0x1}};
            };
            {
                name = "turing1_aon_cfg_rcgr";
                address = 0x4;
                default = 0;
                on_post_write = {};
            };
        };
    },
})
