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

Platform configurations are written in Lua. A typical
configuration defines a hierarchy of modules with their
parameters and socket bindings:

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
    qemu_inst = {
        moduletype = "QemuInstance"
    },
    cpu_0 = {
        moduletype = "cpu_arm_cortexA76",
        mem = {bind = "&router.target_socket"}
    }
}
```

## Loading Components from Shared Libraries

A `moduletype` normally refers to a component built into the
platform binary. When the module factory does not find the type
among the registered components, it loads it from a shared
library at elaboration time. The library name is taken from the
module's `dylib_path` parameter if set, otherwise from its
`moduletype`, and the platform-specific extension (`.so` on
Linux, `.dylib` on macOS, `.dll` on Windows) is appended:

```lua
    uart_0 = {
        moduletype = "Pl011",
        dylib_path = "uart-pl011",
        -- ...
    },
```

### The `module_register()` Hook

A component loaded from a shared library must export a
registration hook with **C linkage**:

```cpp
extern "C" void module_register();
```

After loading the library, the module factory looks up the
symbol `module_register` by its unmangled name and calls it.
The hook must register the component's constructor with the
module factory under the same name used as `moduletype`, which
is done with the `GSC_MODULE_REGISTER_C` macro from
`module_factory_registery.h`. The macro takes the class name
followed by the types of the constructor arguments after the
initial `sc_module_name`:

```cpp
// mycomp.h
#include <module_factory_registery.h>

class mycomp : public sc_core::sc_module
{
public:
    mycomp(sc_core::sc_module_name name, sc_core::sc_object* obj);
    // ...
};

extern "C" void module_register();
```

```cpp
// mycomp.cc
#include <mycomp.h>

void module_register() { GSC_MODULE_REGISTER_C(mycomp, sc_core::sc_object*); }
```

The `extern "C"` declaration is essential. Without it, the hook
is exported under its C++ mangled name (for example
`_Z15module_registerv`), the factory cannot resolve the
`module_register` symbol, and registration is skipped with only
a warning. Elaboration then fails with an error that does not
point at the real cause:

```
Error: ModuleFactory: Can't find module type: mycomp
```

Note that the definition in `mycomp.cc` picks up C linkage from
the declaration in the included header, so `extern "C"` is only
needed in one place. All in-tree components follow this pattern
(see for example
`systemc-components/keep_alive/include/keep_alive.h`); a
definition compiled without such a declaration in scope exports
a mangled symbol and cannot be loaded.

## YAML Configuration

If you prefer YAML, the `lyaml` library provides a bridge.
Install it from
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

**1. Private broker:** `ConfigurableBroker()`

When constructed with no initialized parameters, **all**
parameters held within this broker are treated as private
(hidden from the parent broker).

**2. Selective broker:**
`ConfigurableBroker({{"key1","value1"}, {"key2","value2"}, ...})`

Sets and hides the listed keys. All other keys are passed
through (exported), making the broker invisible for unlisted
parameters. This is useful for structural parameters.

A pass-through broker can be created with an empty list:
`ConfigurableBroker({})`. This provides a local broker without
hiding any parameters.
