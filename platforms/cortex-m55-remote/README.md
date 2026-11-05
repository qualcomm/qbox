# Cortex-M55 Remote CPU Example

This example runs a Cortex-M55 model in a separate `remote_cpu` process.
The main SystemC virtual platform connects to it through the remote TLM
interface. The guest firmware writes to a PL011 UART and waits for
interrupts.

## Prerequisites

Use the standard QBox build prerequisites in
[Building QBox](../../docs/building.md). The old Ubuntu 18.04 reference
is not a current host recommendation. This guide does not state a
verified host support matrix.

The repository includes the `cortex-m55.bin` firmware image. To rebuild
it after changing the firmware source, install `arm-none-eabi-gcc`,
`arm-none-eabi-objcopy`, and `xxd`.

## Build

From the repository root:

```bash
cmake -S . -B build -DLIBQEMU_TARGETS=aarch64
cmake --build build --parallel
```

The root CMake build includes this example. It produces the dedicated
`build/cortex-m55-vp` runner and `build/remote_cpu` beside it.

To rebuild the bundled firmware, run `make` from
`platforms/cortex-m55-remote/fw/cortex-m55/` after installing the
firmware tools:

```bash
cd platforms/cortex-m55-remote/fw/cortex-m55
make
```

## Run

Run from the repository root:

```bash
./build/cortex-m55-vp \
    --gs_luafile platforms/cortex-m55-remote/conf.lua
```

Use this Cortex-M55-specific runner, not the generic `platforms-vp`
executable. The main program sets the `executable_path` parameter to its
own directory. The platform config uses it to find `remote_cpu` beside
the runner. The config loads the firmware image from this example's
`fw/` directory. The firmware writes its status and interrupt messages
to the terminal and then waits for interrupts.

## Optional tests

The CTest test for this example is separate from the normal build and
run steps. It requires Python 3 and the `pexpect` Python package. On
Linux, the test creates a TAP interface named `qbox0`, so the test
process needs permission to create it. These are test requirements,
not requirements for normal platform execution.

## Related guides

- [QBox build instructions](../../docs/building.md)
- [Documentation index](../../docs/README.md)
