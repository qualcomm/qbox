# Character Backends

Character backends provide I/O connectivity for UART and other
serial devices via `biflow_socket`. Each backend exposes a
bidirectional flow socket that can be bound to a UART's
`backend_socket`, allowing data to travel in both directions
between the device model and the outside world.

## char_backend_stdio

Standard I/O backend. Connects a UART to the terminal's
stdin/stdout so that characters written by the simulated
device appear on the console and keyboard input is forwarded
into the simulation.

### CCI Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `read_write` | `bool` | `true` | When true, accept input from stdin |
| `expect` | `string` | `""` | Expect-like script of commands (see below) |
| `ansi_highlight` | `string` | `""` | ANSI escape code applied to output text |

The `expect` parameter accepts a newline-separated list of
commands that automate interaction with the serial output:

- `expect <regex>` -- wait until the output matches the
  regular expression before processing the next command.
- `send <string>` -- inject a string (plus newline) into
  the input buffer.
- `wait <seconds>` -- pause processing for the given number
  of simulated seconds.
- `exit` -- call `sc_stop()` to terminate the simulation.

The `ansi_highlight` parameter wraps every piece of output in
the specified ANSI escape sequence (reset is appended
automatically). This is useful for visually distinguishing
output from different UARTs in the same terminal.

### Socket

| Name | Type |
|------|------|
| `biflow_socket` | `gs::biflow_socket` (biflow) |

## char_backend_socket

TCP socket backend. Exposes the serial stream over a TCP
connection so that external tools (for example `telnet` or
`socat`) can interact with the simulated UART.

### CCI Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `address` | `string` | `"127.0.0.1:4001"` | Listen/connect address in `IP:Port` format |
| `server` | `bool` | `true` | Run as TCP server when true, client when false |
| `nowait` | `bool` | `true` | Use non-blocking mode; drop data when no peer is connected |
| `sigquit` | `bool` | `false` | When true, byte `0x1c` in the stream triggers `sc_stop()` |

When `server` is true the backend listens on the given address
and waits for an incoming connection. When false it connects to
a remote server at that address. The address format is always
`IP:Port` (for example `127.0.0.1:4001`).

If `sigquit` is enabled and the byte `0x1c` (ASCII FS) appears
in the incoming data stream, the simulation is stopped
immediately via `sc_stop()`.

### Socket

| Name | Type |
|------|------|
| `biflow_socket` | `gs::biflow_socket` (biflow) |

## char_backend_file

File backend. Reads serial input from one file and writes
serial output to another. This is convenient for batch
testing or logging UART traffic to disk.

### CCI Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `read_file` | `string` | `""` | Path to the file supplying input data |
| `write_file` | `string` | `""` | Path to the file receiving output data |
| `baudrate` | `unsigned int` | `0` | Read speed in bytes per second (0 = unlimited) |

When `baudrate` is non-zero, characters are read from the
input file at the specified rate, modelling realistic serial
timing. A value of 0 feeds data as fast as the simulation
allows.

### Socket

| Name | Type |
|------|------|
| `biflow_socket` | `gs::biflow_socket` (biflow) |

## Connecting a Backend to a UART

Every character backend exposes a `biflow_socket`. UART models
(such as the PL011) expose a `backend_socket`. To wire the two
together, bind the UART's `backend_socket` to the backend's
`biflow_socket`.

The following Lua configuration snippet shows a PL011 UART
connected to a stdio backend:

```lua
platform = {
    charbackend_stdio_0 = {
        moduletype = "char_backend_stdio",
        read_write = true,
    },

    pl011_uart_0 = {
        moduletype = "Pl011",
        dylib_path = "uart-pl011",
        target_socket = {
            address = 0x9000000,
            size = 0x1000,
            bind = "&router.initiator_socket",
        },
        irq = { bind = "&gic_0.spi_in_379" },
        backend_socket = { bind = "&charbackend_stdio_0.biflow_socket" },
    },
}
```

The key line is:

```lua
backend_socket = { bind = "&charbackend_stdio_0.biflow_socket" },
```

This tells the platform builder to bind the UART's
`backend_socket` to the stdio backend's `biflow_socket`,
completing the bidirectional data path. The same pattern
applies when using `char_backend_socket` or `char_backend_file`
-- simply replace the backend instance name and set the
appropriate CCI parameters.

Note the `dylib_path` parameter on the UART: it makes the
platform load the `Pl011` model from the `uart-pl011` shared
library at runtime. A library loaded this way must export an
`extern "C" void module_register()` hook that registers the
module type -- see
[Loading Components from Shared Libraries](configuration.md#loading-components-from-shared-libraries)
for the details of this contract.
