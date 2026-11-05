# libgssync -- Synchronization Library

The GreenSocs synchronization library provides policies to
synchronize an external simulator (often QEMU) with SystemC. It
also provides utilities such as a thread-safe `async_event` and
a real-time speed limiter for SystemC.

The library includes a compatibility layer for unpatched
SystemC. This guide makes no performance comparison between
SystemC versions.

## Suspend/Unsuspend Interface

Four functions extend the SystemC kernel:

```cpp
void sc_suspend_all()
void sc_unsuspend_all()
void sc_unsuspendable()
void sc_suspendable()
```

### suspend_all / unsuspend_all

This pair requests the kernel to atomically suspend all
processes (using the same semantics as the thread `suspend()`
call). The kernel suspends all processes together, so they can
be suspended and unsuspended without changing an individual
process's suspended status. Calls should be paired.

Multiple calls to `suspend_all()` from separate processes or
`sc_main` are allowed. The kernel keeps all processes suspended
while there are more `suspend_all()` calls than
`unsuspend_all()` calls.

### unsuspendable / suspendable

These functions let a process opt out of `suspend_all()`. If any
process has opted out, the kernel cannot suspend all processes
atomically. Call these functions from a process and pair them.
The default state is suspendable.

### Use Cases

**Save and restore:** Call `suspend_all()` when a save is
requested. If a model is unsuspendable, the simulation waits
until all processes can be suspended.

**External sync:** An external model can inject events into
SystemC (for example, through `async_request_update()`). A call
to `suspend_all()` stops SystemC from advancing while the update
runs. The update should mark required processes unsuspendable
before the current delta cycle ends.

## Synchronization Policies

`QemuInstance` uses the CCI parameter `sync_policy`. The
registered factory names are:

| Policy | Description |
|--------|-------------|
| `tlm2` | Standard TLM2 mode. The quantum keeper does not handle multiple threads. Use with `COROUTINE` TCG mode only. |
| `multithread` | Basic multi-thread synchronization policy. |
| `multithread-quantum` | Waits for the quantum boundary before processes advance. |
| `multithread-rolling` | Rolling synchronization policy. |
| `multithread-adaptive` | Adaptive synchronization policy. |
| `multithread-unconstrained` | Allows QEMU to run at its own pace. |
| `multithread-freerunning` | Free-running synchronization policy. |

The default for `QemuInstance.sync_policy` is
`multithread-quantum`. The separate `Remote` module also has a
`sync_policy` parameter, with default `multithread-unconstrained`.
See the [factory](../systemc-components/common/src/libgssync/qk_factory.cc),
[`QemuInstance`](../qemu-components/common/include/qemu-instance.h),
and [`Remote`](../systemc-components/common/include/remote.h)
source definitions.

See [libqbox](libqbox.md#tlm2-quantum-keeper-synchronization-mode)
for policy compatibility with QEMU TCG modes.

## Tests

The CMake configuration registers these unit tests under
`tests/sync/unit_tests/`: `qk_extendedif_test`,
`qkmultithread_test`, and `qkmulti-quantum_test`. For a
configured build, use:

```bash
ctest --test-dir build -R '^(qk_extendedif_test|qkmultithread_test|qkmulti-quantum_test)$'
```

This describes registered tests only; it does not report that
they have run or passed. See the
[test registration](../tests/sync/unit_tests/CMakeLists.txt).
