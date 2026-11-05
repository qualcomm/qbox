# PythonBinder

The PythonBinder component is a SystemC model that enables
initiating and reacting to SystemC TLM transactions from
Python. It exposes a minimal set of SystemC/TLM features to
Python, primarily for implementing Python-based backends for
I/O models (e.g., stdio backend for UART) and models that
react to SystemC-initiated transactions.

The model uses
[pybind11](https://pybind11.readthedocs.io/en/stable/) to
embed a Python interpreter within the virtual platform
process.

To build QBox without PythonBinder and its pybind11
dependencies, use: `-DENABLE_PYTHON_BINDER=OFF`.

`WITHOUT_PYTHON_BINDER` is deprecated. Use
`ENABLE_PYTHON_BINDER=OFF` instead.

## CCI Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `py_module_name` | `std::string` | `""` | Name of the Python script (with or without `.py` suffix) |
| `py_module_dir` | `std::string` | `""` | Directory containing the Python module |
| `py_module_args` | `std::string` | `""` | Command-line arguments string passed to the module |
| `tlm_initiator_ports_num` | `uint32_t` | `0` | Number of TLM initiator ports |
| `tlm_target_ports_num` | `uint32_t` | `0` | Number of TLM target ports |
| `initiator_signals_num` | `uint32_t` | `0` | Number of initiator signals |
| `target_signals_num` | `uint32_t` | `0` | Number of target signals |
| `current_mod_id_prefix` | `std::string` | `""` | Prefix used for dynamically created Python modules; use a unique value per PythonBinder instance |
| `biflow_socket_num` | `uint32_t` | `0` | Number of biflow sockets; supported values are `0` and `1` |

## Python API

### sc_core Module

```python
from sc_core import sc_time_unit
```

**sc_time_unit:** Maps to `sc_core::sc_time_unit` enum
(`SC_FS`, `SC_PS`, `SC_NS`, `SC_US`, `SC_MS`, `SC_SEC`).

**sc_time:** Maps to `sc_core::sc_time` with constructors:
- `sc_time()` -- default
- `sc_time(value: float, unit: sc_time_unit)` -- from
  value and unit
- `sc_time(value: float, unit: str)` -- from value and
  unit string (`"fs"`, `"ps"`, `"ns"`, `"us"`, `"ms"`,
  `"sec"`)
- `sc_time(value: float, scale: bool)` /
  `sc_time(value: int, scale: bool)`

Conversion methods: `from_seconds()`, `from_value()`,
`from_string()`, `to_default_time_units()`, `to_double()`,
`to_seconds()`, `to_string()`, `value()`.

Arithmetic operators: `+`, `-`, `/`, `*`, `%`, `+=`, `-=`,
`%=`, `*=`, `/=`, `==`, `!=`, `<=`, `>=`, `>`, `<`.

**sc_event:** Maps to `sc_core::sc_event`:
- `sc_event()` / `sc_event(name: str)` -- constructors
- `notify()` / `notify(t: sc_time)` /
  `notify(value: float, unit: sc_time_unit)`
- `notify_delayed()` / `notify_delayed(t: sc_time)` /
  `notify_delayed(value: float, unit: sc_time_unit)`

**sc_spawn_options:** Maps to `sc_core::sc_spawn_options`:
- `dont_initialize()`, `is_method()`,
  `set_stack_size(int)`, `set_sensitivity(event)`,
  `spawn_method()`

**Functions:**
- `sc_spawn(f: Callable, name: str,
  opts: sc_spawn_options)`

`sc_core.wait()` is not bound to Python. For Python callbacks
that need to suspend and resume, use the binder's generator/yield
handling, which schedules callbacks with SystemC `next_trigger`.
Do not call `sc_core.wait()` from Python.

### gs Module

**async_event:** Maps to `gs::async_event`:
- `async_event(start_attached: bool)` -- constructor
- `async_notify()`, `notify(delay: sc_time)`
- `async_attach_suspending()`,
  `async_detach_suspending()`,
  `enable_attach_suspending(e: bool)`

### tlm_generic_payload Module

**tlm_command:** `TLM_READ_COMMAND`,
`TLM_WRITE_COMMAND`, `TLM_IGNORE_COMMAND`

**tlm_response_status:** `TLM_OK_RESPONSE`,
`TLM_INCOMPLETE_RESPONSE`,
`TLM_GENERIC_ERROR_RESPONSE`,
`TLM_ADDRESS_ERROR_RESPONSE`,
`TLM_COMMAND_ERROR_RESPONSE`,
`TLM_BURST_ERROR_RESPONSE`,
`TLM_BYTE_ENABLE_ERROR_RESPONSE`

**tlm_generic_payload:** Maps to
`tlm::tlm_generic_payload` with member functions:
- Address: `get_address()`, `set_address(int)`
- Command: `is_read()`, `is_write()`, `set_read()`,
  `set_write()`, `get_command()`, `set_command()`
- Response: `is_response_ok()`,
  `is_response_error()`, `get_response_status()`,
  `set_response_status()`, `get_response_string()`
- Data: `set_data_length(int)`, `get_data_length()`,
  `set_data_ptr(buffer)`, `set_data(buffer)`,
  `get_data()`
- Streaming: `get_streaming_width()`,
  `set_streaming_width(int)`
- Byte enable: `set_byte_enable_length(int)`,
  `get_byte_enable_length()`,
  `set_byte_enable(buffer)`, `get_byte_enable()`

`set_data_ptr()` is for transactions created in Python.
`set_data()` / `set_byte_enable()` are for transactions created
in C++ and reused from Python.

**Known limitation:** `set_byte_enable_ptr()` is currently not
working. Its binding calls `set_data_ptr()` instead. Do not rely
on it as a supported feature.

## Dynamically Created Modules

These modules use the actual value of the
`current_mod_id_prefix` CCI parameter to make imports unique
per PythonBinder instance. For example, if
`current_mod_id_prefix = "i2c_"`, use
`import i2c_biflow_socket` instead of
`import biflow_socket`.

**cpp_shared_vars:**
- `module_args` -- command-line arguments from the
  `py_module_args` CCI parameter

**tlm_do_b_transport:**
- `do_b_transport(id: int, trans: tlm_generic_payload,
  delay: sc_time)` -- initiates a `b_transport` call,
  mapping to
  `initiator_sockets[id]->b_transport(trans, delay)`
  in C++

**initiator_signal_socket:**
- `write(id: int, value: bool)` -- writes to
  `initiator_signal_sockets[id]` in C++

**biflow_socket:**
- `can_receive_more(int)`, `can_receive_set(int)`,
  `can_receive_any()`
- `enqueue(int)`, `set_default_txn(tlm_generic_payload)`,
  `force_send(tlm_generic_payload)`, `reset()`

## Transaction Handling

**Incoming transactions:** Implement
`b_transport(id: int, trans: tlm_generic_payload,
delay: sc_time)` in your Python script to handle
transactions arriving on `target_sockets[id]`.

**Outgoing transactions:** Call
`tlm_do_b_transport.do_b_transport(id, trans, delay)` to
initiate a transaction on `initiator_sockets[id]`.

**Signal handling:** Implement
`target_signal_cb(id: int, value: bool)` to handle
incoming signals on `target_signal_sockets[id]`. Use
`initiator_signal_socket.write(id, value)` to write
outgoing signals.

## Simulation Callbacks

These can be implemented in the Python script:

- `before_end_of_elaboration()`
- `end_of_elaboration()`
- `start_of_simulation()`
- `end_of_simulation()`

## Examples

- `tests/components/python-binder` -- PythonBinder
  test bench
- `py-models/py-uart.py` -- stdio backend for PL011 UART
