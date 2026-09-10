get_filename_component(PXSYS_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(PXSYS_CORE_SOURCES
    "${PXSYS_ROOT}/system/core/src/status.c"
    "${PXSYS_ROOT}/system/core/src/types.c"
    "${PXSYS_ROOT}/system/core/src/display.c"
    "${PXSYS_ROOT}/system/core/src/system_status.c"
    "${PXSYS_ROOT}/system/core/src/toast.c"
    "${PXSYS_ROOT}/system/core/src/window.c"
    "${PXSYS_ROOT}/system/core/src/app_registry.c"
    "${PXSYS_ROOT}/system/core/src/app_metadata.c"
    "${PXSYS_ROOT}/system/core/src/app_lifecycle.c"
    "${PXSYS_ROOT}/system/core/src/bound_client.c"
    "${PXSYS_ROOT}/system/core/src/event_broker.c"
    "${PXSYS_ROOT}/system/core/src/intent.c"
    "${PXSYS_ROOT}/system/core/src/intent_wire.c"
    "${PXSYS_ROOT}/system/core/src/locale.c"
    "${PXSYS_ROOT}/system/core/src/resources.c"
    "${PXSYS_ROOT}/system/core/src/task_manager.c"
    "${PXSYS_ROOT}/system/core/src/role_host.c"
    "${PXSYS_ROOT}/system/core/src/role_registry.c"
    "${PXSYS_ROOT}/system/core/src/theme.c"
    "${PXSYS_ROOT}/system/core/src/renderer_host.c"
    "${PXSYS_ROOT}/system/core/src/service_registry.c"
    "${PXSYS_ROOT}/system/core/src/runtime.c")

set(PXSYS_NATIVE_SOURCES
    "${PXSYS_ROOT}/system/runtimes/native/src/native_runtime.c"
    "${PXSYS_ROOT}/system/runtimes/native/src/native_client.c")

set(PXSYS_PXA_RUNTIME_SOURCES
    "${PXSYS_ROOT}/system/runtimes/pxa/src/pxa_binding.c"
    "${PXSYS_ROOT}/system/runtimes/pxa/src/pxa_catalog.c"
    "${PXSYS_ROOT}/system/runtimes/pxa/src/pxa_client.c"
    "${PXSYS_ROOT}/system/runtimes/pxa/src/pxa_gateway_wire.c"
    "${PXSYS_ROOT}/system/runtimes/pxa/src/pxa_runtime.c")

set(PXSYS_STANDARD_SOURCES
    "${PXSYS_ROOT}/system/standard/src/standard_system.c")

set(PXSYS_HEADLESS_RENDERER_SOURCES
    "${PXSYS_ROOT}/ui/renderers/headless/src/headless_renderer.c")

set(PXSYS_LVGL_RENDERER_SOURCES
    "${PXSYS_ROOT}/ui/renderers/lvgl/src/lvgl_renderer.c")

set(PXSYS_REFERENCE_LAYOUT_SOURCES
    "${PXSYS_ROOT}/ui/reference/src/reference_layout.c")

set(PXSYS_REFERENCE_LVGL_SOURCES
    "${PXSYS_ROOT}/ui/reference/lvgl/src/reference_lvgl.c")

set(PXSYS_PUBLIC_INCLUDE_DIRS
    "${PXSYS_ROOT}/system/core/include"
    "${PXSYS_ROOT}/system/runtimes/native/include"
    "${PXSYS_ROOT}/system/runtimes/pxa/include"
    "${PXSYS_ROOT}/system/standard/include"
    "${PXSYS_ROOT}/ui/reference/include"
    "${PXSYS_ROOT}/ui/renderers/headless/include")
