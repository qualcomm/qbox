-- Configuration table for container1 with nested hierarchy
container1_config = {
    initiator1 = {
        moduletype = "InitiatorTester";
        initiator_socket = {bind = "&router.target_socket"};
    };

    router = {
        moduletype = "router";
    };

    memory_a = {
        moduletype = "gs_memory";
        target_socket = {
            address = 0x40000000;
            size = 0x1000;
            bind = "&router.initiator_socket";
        };
    };

    memory_b = {
        moduletype = "gs_memory";
        target_socket = {
            address = 0x50000000;
            size = 0x1000;
            bind = "&router.initiator_socket";
        };
    };

    nested_container = {
        moduletype = "Container";

        initiator_nested = {
            moduletype = "InitiatorTester";
            initiator_socket = {bind = "&router_nested.target_socket"};
        };

        router_nested = {
            moduletype = "router";
        };

        memory_nested = {
            moduletype = "gs_memory";
            target_socket = {
                address = 0x60000000;
                size = 0x1000;
                bind = "&router_nested.initiator_socket";
            };
        };
    };

    sockets = {
        external_initiator_to_router = "&router.target_socket";
        router_to_external = "&router.initiator_socket";
    };
};

-- Configuration table for container2
container2_config = {
    initiator2 = {
        moduletype = "InitiatorTester";
        initiator_socket = {bind = "&router.target_socket"};
    };

    router = {
        moduletype = "router";
    };

    memory_x = {
        moduletype = "gs_memory";
        target_socket = {
            address = 0x70000000;
            size = 0x1000;
            bind = "&router.initiator_socket";
        };
    };

    memory_y = {
        moduletype = "gs_memory";
        target_socket = {
            address = 0x80000000;
            size = 0x1000;
            bind = "&router.initiator_socket";
        };
    };

    sockets = {
        external_initiator_to_router = "&router.target_socket";
        router_to_external = "&router.initiator_socket";
    };
};

-- Configuration table for an exported socket alias chain. Only the first
-- alias receives external address/bind parameters below; container_builder
-- must forward those parameters through second_hop to router.target_socket.
container3_config = {
    initiator3 = {
        moduletype = "InitiatorTester";
        initiator_socket = {bind = "&router.target_socket"};
    };

    router = {
        moduletype = "router";
    };

    memory_z = {
        moduletype = "gs_memory";
        target_socket = {
            address = 0x90000000;
            size = 0x1000;
            bind = "&router.initiator_socket";
        };
    };

    sockets = {
        first_hop = "&second_hop";
        second_hop = "&router.target_socket";
    };
};

-- Two exported aliases converge on shared_hop before reaching the router.
-- The second alias must replace the first alias's address and size.
container4_config = {
    initiator4 = {
        moduletype = "InitiatorTester";
        initiator_socket = {bind = "&router.target_socket"};
    };

    router = {
        moduletype = "router";
    };

    memory = {
        moduletype = "gs_memory";
        target_socket = {
            address = 0xB0000000;
            size = 0x1000;
            bind = "&router.initiator_socket";
        };
    };

    sockets = {
        first_hop = "&shared_hop";
        second_hop = "&shared_hop";
        shared_hop = "&router.target_socket";
    };
};

-- A chain whose dependency order is the reverse of lexicographic alias order.
container5_config = {
    router = {
        moduletype = "router";
    };

    memory = {
        moduletype = "gs_memory";
        target_socket = {
            address = 0xD0000000;
            size = 0x1000;
            bind = "&router.initiator_socket";
        };
    };

    sockets = {
        z_first_hop = "&a_second_hop";
        a_second_hop = "&router.target_socket";
    };
};

-- Bind targets are aliases too, including chains and both lexical orders.
alias_binding_config = {
    first = {moduletype = "InitiatorTester"};
    second = {moduletype = "InitiatorTester"};
    third = {moduletype = "InitiatorTester"};
    memory = {
        moduletype = "gs_memory";
        target_socket = {address = 0; size = 0x1000};
    };
    sockets = {
        a_source = "&first.initiator_socket";
        middle_sink = "&memory.target_socket";
        z_source = "&second.initiator_socket";
        chain_source = "&a_chain_source";
        a_chain_source = "&third.initiator_socket";
        z_sink = "&middle_sink";
    };
};

AllTests = {
    reuse_platform = {
        existing = {
            moduletype = "UnregisteredModule";
            dont_construct = true;
        };
        existing_container = {
            moduletype = "UnregisteredContainer";
            dont_construct = true;
        };
    };
    platform = {
        moduletype = "Container";

        alias_binding = {
            moduletype = "container_builder";
            config = alias_binding_config;
            a_source = {bind = "&AllTests.platform.alias_binding.middle_sink"};
            z_source = {bind = "&AllTests.platform.alias_binding.middle_sink"};
            chain_source = {bind = "&AllTests.platform.alias_binding.z_sink"};
        };

        initiator = {
            moduletype = "InitiatorTester";
            initiator_socket = {bind = "&router_main.target_socket"};
        };

        router_main = {
            moduletype = "router";
            target_socket = {
                address = 0x10000000;
                size = 0x20001000;
                relative_addresses = false;
            };
        };

        memory1 = {
            moduletype = "gs_memory";
            target_socket = {
                address = 0x10000000;
                size = 0x1000;
                bind = "&router_main.initiator_socket";
            };
        };

        memory2 = {
            moduletype = "gs_memory";
            target_socket = {
                address = 0x20000000;
                size = 0x1000;
                bind = "&router_main.initiator_socket";
            };
        };

        memory3 = {
            moduletype = "gs_memory";
            target_socket = {
                address = 0x30000000;
                size = 0x1000;
                bind = "&router_main.initiator_socket";
            };
        };

        container1 = {
            moduletype = "container_builder";
            config = container1_config;
            router_to_external = {
                bind = "&AllTests.platform.router_main.target_socket";
            };
        };

        container2 = {
            moduletype = "container_builder";
            config = container2_config;
        };

        container3 = {
            moduletype = "container_builder";
            config = container3_config;
            first_hop = {
                address = 0x90000000;
                size = 0x1000;
            };
        };

        chain_initiator = {
            moduletype = "InitiatorTester";
            initiator_socket = {bind = "&platform.container3.first_hop"};
        };

        container4 = {
            moduletype = "container_builder";
            config = container4_config;
            first_hop = {
                address = 0xA0000000;
                size = 0x1000;
            };
            second_hop = {
                address = 0xB0000000;
                size = 0x2000;
            };
        };

        fan_in_initiator = {
            moduletype = "InitiatorTester";
            initiator_socket = {bind = "&platform.container4.second_hop"};
        };

        container5 = {
            moduletype = "container_builder";
            config = container5_config;
            z_first_hop = {
                address = 0xD0000000;
                size = 0x1000;
            };
        };

        reverse_chain_initiator = {
            moduletype = "InitiatorTester";
            initiator_socket = {bind = "&platform.container5.z_first_hop"};
        };
    };
}

for i = 1, 16 do
    local suffix = string.format("%02d", i)
    AllTests.platform["a_router_order_probe_" .. suffix] = {
        moduletype = "router_order_probe";
        initiator_socket = {bind = "&z_order_memory_" .. suffix .. ".target_socket"};
    }
    AllTests.platform["z_order_memory_" .. suffix] = {
        moduletype = "gs_memory";
        target_socket = {
            address = 0xC0000000 + i * 0x1000;
            size = 0x1000;
        };
    }
end

AllTests.platform.a_nested_router_order_probe = {
    moduletype = "nested_router_order_probe";
    ports = {
        [0] = {bind = "&z_nested_router_order_memory.target_socket"};
    };
};
AllTests.platform.z_nested_router_order_memory = {
    moduletype = "gs_memory";
    target_socket = {
        address = 0xD1000000;
        size = 0x1000;
        bind = "&router_main.initiator_socket";
    };
};
