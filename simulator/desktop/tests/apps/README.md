# Product runtime test fixtures

These signed Guest fixtures exercise the actual WAMR product runtime. They are
internal test inputs, outside the product application catalog and store listings.
They build directly against the SDK and do not depend on the `pxa-apps` checkout.

- `abi-v1-smoke`: Core 2 request pressure, full-width tokens and Handles, service
  calls, private storage and file lifecycle.
- `abi-v1-ipc-smoke`: requests, responses and events between two Components.
- `resource-scenes`: asynchronous resources, texture ownership, audio, budgets
  and cancellation. See its [workload notes](resource-scenes/README.md).

The historical directory names and package IDs are retained for regression
compatibility; all three fixtures use Core 2. The shared `diagnostic_ui.hpp`
belongs only to these tests.

From the `pxa-system` root, after building the desktop test targets:

```sh
bash tools/package/test_core2_smoke.sh build/desktop/pxsys_product_simulator
bash tools/package/test_core2_ipc_smoke.sh build/desktop/pxsys_product_simulator
bash tools/package/test_resource_app.sh build/desktop/pxsys_resource_app_test
bash tools/package/test_resource_app.sh build/desktop/pxsys_resource_app_test exit-loading
```

Each script generates a temporary P-256 signing key, builds the fixture, runs it
with the matching public key, and removes temporary packages, keys and state.
The smoke tests and default resource run are registered with CTest.
