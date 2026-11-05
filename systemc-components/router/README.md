# SystemC Router Component

The SystemC router maps address ranges from initiator sockets to
target sockets. It provides `b_transport`, debug transport, and
DMI forwarding. The router adds regions from CCI socket
parameters or through `add_target`.

## Address mapping

The default type is `gs::router<BUSWIDTH>` and uses
`AddrMapNoCache`. Address ranges can overlap. The target with
the lowest numeric `priority` wins. If priorities are equal,
the mapping added most recently wins in the overlap. The default
priority is 0. Targets can receive either relative addresses
(the default) or absolute addresses.

The router's `add_target` convenience function accepts a target
socket, base address, size, a relative-address flag, and an
optional priority. Initiators bind to `target_socket`; the
router's `initiator_socket` connects to target sockets.

```cpp
gs::router<> router("router");
router.add_target(memory.socket, 0x1000, 0x1000); // size 0x1000
cpu.socket.bind(router.target_socket);
```

For CCI-based mapping, set the target socket's required
`address` and `size` parameters. The other parameters are:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `relative_addresses` | `true` | Pass the target an offset from the mapped base address |
| `priority` | `0` | Lower numeric values have higher priority |
| `chained` | `false` | Suppress debug output for this target when true |

## Cache choices

The default `AddrMapNoCache` does not cache address lookups.
`router.h` exposes a cache policy template parameter, and
`addrmap_cache_examples.h` contains the optional `LRUCache`,
`RegionFIFOCache`, and `RegionLRUCache` examples.
This documentation makes no performance claim for these
choices.

## Tests

The router CMake file registers these CTest names:

- `router-tests`
- `router-tests-extended`
- `router-addressmap-tests`
- `router-cache-bench-enhanced`
- `router-advanced-overlap-tests`
- `router-shadowing-warning-test`
- `router-tests-new`
- `router-thread-safety-test`
- `router-coverage-tests`

List the router tests other than `router-cache-bench-enhanced`
without running them:

```bash
ctest --test-dir build -N -R '^router-' -E '^router-cache-bench-enhanced$'
```

To run those tests from a configured build:

```bash
ctest --test-dir build -R '^router-' -E '^router-cache-bench-enhanced$' --output-on-failure
```

The test registration is in
[`tests/components/router/CMakeLists.txt`](../../tests/components/router/CMakeLists.txt).
No test results or performance measurements are reported here.

## See also

- [Router interface](include/router.h)
- [Cache examples](include/addrmap_cache_examples.h)
