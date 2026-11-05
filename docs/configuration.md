# Configuration

QBox uses the SystemC CCI (Configuration, Control, and
Inspection) standard for configuration, with Lua files as the
primary configuration language.

## Command-Line Parameters

```bash
# Load a Lua configuration file
--gs_luafile <FILE.lua>
# or
-l <FILE.lua>

# Set an individual parameter
--param path.to.param=<value>
# or
-p path.to.param=<value>
```

Order matters: the last option on the command line to set a
parameter takes precedence.

## Lua Configuration

Platform configurations are written in Lua. This excerpt shows
module arguments and socket bindings. It is not a complete
bootable platform; see the
[in-tree AArch64 configuration](../platforms/ubuntu/conf_aarch64.lua)
for a full example.

```lua
platform = {
    router = {
        moduletype = "router"
    },
    memory = {
        moduletype = "gs_memory",
        target_socket = {
            address = 0x80000000,
            size = 0x100000000,
            bind = "&router.initiator_socket"
        }
    },
    qemu_inst_mgr = {
        moduletype = "QemuInstanceManager"
    },
    qemu_inst = {
        moduletype = "QemuInstance",
        args = { "&platform.qemu_inst_mgr", "AARCH64" }
    },
    cpu_0 = {
        moduletype = "cpu_arm_cortexA76",
        args = { "&platform.qemu_inst" },
        mem = {bind = "&router.target_socket"}
    }
}
```

## YAML Configuration

If you prefer YAML, the example below uses the external Lua
module `lyaml`. Install and configure that module separately
for your Lua environment before using this example:
https://github.com/gvvaughan/lyaml.

The following Lua snippet loads a `conf.yaml` file:

```lua
local lyaml = require "lyaml"

function readAll(file)
    local f = assert(io.open(file, "rb"))
    local content = f:read("*all")
    f:close()
    return content
end

print "Loading conf.yaml"
yamldata = readAll("conf.yaml")
ytab = lyaml.load(yamldata)
for k, v in pairs(ytab) do
    _G[k] = v
end
yamldata = nil
ytab = nil
```

## ConfigurableBroker

The `gs::ConfigurableBroker` self-registers in the SystemC CCI
hierarchy. It inherits from the standard CCI consuming broker
and adds the ability to explicitly hide parameters from the
parent broker.

### Instantiation Modes

**1. Default broker:** `ConfigurableBroker()`

For a nested broker, the default constructor starts with an
empty hidden-parameter list. It forwards parameters to the
parent unless they are added to that list. This is not the
same as hiding every parameter.

**2. Selective broker:**
`ConfigurableBroker({{"key1","value1"}, {"key2","value2"}, ...})`

Sets and hides the listed keys. All other keys are passed
through (exported). This is useful when only selected
parameters must remain local.

A pass-through broker can be created with an empty list:
`ConfigurableBroker({})`. This provides a local broker without
hiding any parameters.
