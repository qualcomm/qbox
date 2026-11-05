# libgsutils -- Utilities Library

The GreenSocs utilities library provides CCI configuration
helpers, logging, test functions, and basic TLM port types.

## CCI Helpers

### Read a value

`cci_get` returns a CCI value, or converts it to the requested
type. These declarations are in
[`cciutils.h`](../systemc-components/common/include/cciutils.h):

```cpp
cci::cci_value cci_get(cci::cci_broker_handle broker, std::string name);

template <typename T>
T cci_get(cci::cci_broker_handle broker, std::string name);

template <typename T>
T cci_get_d(cci::cci_broker_handle broker, std::string name, T default_val);
```

The value lookup clears the parameter's unused marker. The
typed `cci_get_d` returns `default_val` if it cannot convert the
value to `T`.

### ConfigurableBroker

`gs::ConfigurableBroker` is a CCI broker that supports the QBox
configuration flow. See [Configuration](configuration.md) for
instantiation modes and parameter handling. Its declaration is
in [`cciutils.h`](../systemc-components/common/include/cciutils.h).

## Tests

Utility tests are configured under `tests/utils/`; there is no
dedicated `tests/libgsutils` directory or `libgsutils` CTest
target. CMake registers tests such as `cci_test`,
`cci_alias_test`, `factory_platform`, `logger_test`,
`lua_test`, and `scp_logging_test`. In a configured build, run
these registered tests with:

```bash
ctest --test-dir build -R '^(cci_test|cci_alias_test|factory_platform|logger_test|logger_test_gslog|lua_test|scp_logging_test)$'
```

This describes test registration only; it does not report that
the tests have run or passed. See the
[utility test CMake setup](../tests/utils/CMakeLists.txt) and
its subdirectories.
