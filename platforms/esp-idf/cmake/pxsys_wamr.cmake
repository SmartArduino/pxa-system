# The PXA runtime and the MicroPixel Host share the fork pinned by the parent
# project's third_party/micropixel gitlink. Do not create a patched overlay:
# PXA packages must run against the exact WAMR source and AOT ABI shipped by
# MicroPixel, so the upstream fork remains untouched and updateable.

get_filename_component(PXSYS_WAMR_ESP_IDF_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(PXSYS_WAMR_ROOT "${PXSYS_WAMR_ESP_IDF_DIR}/../.." ABSOLUTE)
get_filename_component(PXSYS_PROJECT_ROOT "${PXSYS_WAMR_ROOT}/.." ABSOLUTE)
set(PXSYS_MICROPIXEL_ROOT "${PXSYS_PROJECT_ROOT}/third_party/micropixel" CACHE PATH
    "Path to the unmodified MicroPixel source submodule")
set(PXSYS_MICROPIXEL_WAMR_COMPAT_HEADER
    "${PXSYS_MICROPIXEL_ROOT}/firmware/espressif/wamr_espidf_compat.h")

function(pxsys_configure_wamr_esp_idf wamr_component)
    if(NOT TARGET ${wamr_component})
        message(FATAL_ERROR "WAMR component target is missing: ${wamr_component}")
    endif()
    if(NOT EXISTS "${PXSYS_MICROPIXEL_WAMR_COMPAT_HEADER}")
        message(FATAL_ERROR
            "MicroPixel WAMR compatibility header is missing: "
            "${PXSYS_MICROPIXEL_WAMR_COMPAT_HEADER}. "
            "Run git submodule update --init --recursive third_party/micropixel")
    endif()

    # This is the same header injected by MicroPixel's ESP-IDF project. It
    # supplies declarations removed from ESP-IDF 6.1 transitive headers but
    # does not alter WAMR sources or ABI.
    target_compile_options(${wamr_component} PRIVATE
        $<$<COMPILE_LANGUAGE:C,CXX>:-include>
        $<$<COMPILE_LANGUAGE:C,CXX>:${PXSYS_MICROPIXEL_WAMR_COMPAT_HEADER}>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-error=dangling-pointer>)
endfunction()
