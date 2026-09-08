# Native C SDK

Native applications use the installed `pxsys/*` headers and link the targets
they need. They do not include the PXA Guest SDK and do not call libpxa.

For an in-tree CMake application:

```cmake
target_link_libraries(my_native_app PRIVATE pxsys::native pxsys::core)
```

For an installed SDK:

```cmake
find_package(pxsys 0.1 REQUIRED)
target_link_libraries(my_native_app PRIVATE pxsys::native pxsys::core)
```

Register `pxsys_native_app_t` callbacks with the standard system's native
runtime. Navigation, RPC and topics then go through `pxsys_native_client_t`,
which binds the system-authenticated application identity to every operation.
