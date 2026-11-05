# QBox Monitor

The QBox monitor is a web-based interface for a running
simulation. It serves REST endpoints and WebSocket connections
through the Crow web framework. It can also serve an HTML
dashboard and stream data in real time.

## Features

- Query the current simulation time (`/sc_time`).
- Pause and resume the simulation (`/pause`, `/continue`).
- Browse the SystemC object hierarchy and inspect CCI
  parameters (`/object/`, `/object/<str>`). Object details can
  include `child_dbg_ports`.
- Read monitor status (`/qk_status`, `/mcips_plugin_status`,
  `/sc_suspended`) and the dashboard refresh interval
  (`/refresh_interval`).
- **Current implementation bug:** `/transport_dbg/<int>/<str>`
  parses the address but ignores it. It always sends a four-byte
  transaction at address 0. This is a defect, not intended API
  behavior; the current implementation does not provide
  arbitrary-address reads.
- List biflows (`/biflows`) and connect to a named biflow with
  WebSocket (`/biflow/<str>`), for example for a QMP or serial
  connection.

The monitor discovers `biflow_multibindable` sockets during
elaboration. Not every object named `biflow` is necessarily a
discoverable socket. Use `/biflows` to find the names available
in a running platform.

The dashboard refresh interval is `refresh_interval_ms`, with
a current default of 100 ms. The dashboard HTML is loaded from
the executable-relative `static/` directory by default.

## CCI Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `server_port` | `uint32_t` | `18080` | HTTP port the monitor listens on |
| `html_doc_template_dir_path` | `string` | (executable-relative `static/`) | Directory containing HTML files |
| `html_doc_name` | `string` | `"monitor.html"` | Name of the main HTML document |
| `use_html_presentation` | `bool` | `true` | Serve the HTML dashboard; when false, the root URL returns a plain-text API listing |
| `refresh_interval_ms` | `uint32_t` | `100` | Dashboard refresh interval in milliseconds |

## Example Configuration

```lua
platform["monitor_0"] = {
    moduletype = "monitor",
    server_port = 18080,
    use_html_presentation = true,
    html_doc_template_dir_path = "/path/to/html/templates",
    html_doc_name = "monitor.html",
}
```

After the simulation starts, open `http://localhost:18080/` in
a browser to access the dashboard. For command-line examples,
see the [CLI debugging guide](monitor-debugging-guide.md).
