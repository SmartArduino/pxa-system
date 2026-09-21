# Configure the WAMR checkout owned by PXA System. Source compatibility
# patches are materialized in the build directory; the submodule stays clean.

get_filename_component(PXSYS_WAMR_ESP_IDF_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(PXSYS_WAMR_ROOT "${PXSYS_WAMR_ESP_IDF_DIR}/../.." ABSOLUTE)
include("${PXSYS_WAMR_ROOT}/cmake/PxaWamrMetadata.cmake")
set(PXSYS_WAMR_PATCH_SERIES
    "${PXSYS_WAMR_ESP_IDF_DIR}/wamr/patches/series")
set(PXSYS_WAMR_OVERLAY_TOOL
    "${PXSYS_WAMR_ROOT}/tools/wamr/prepare_overlay.py")
set(PXSYS_WAMR_ESP_IDF_COMPAT_HEADER
    "${PXSYS_WAMR_ESP_IDF_DIR}/wamr/wamr_espidf_compat.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PXSYS_WAMR_METADATA}"
    "${PXSYS_WAMR_OVERLAY_TOOL}"
    "${PXSYS_WAMR_PATCH_SERIES}"
    "${PXSYS_WAMR_ESP_IDF_COMPAT_HEADER}")
file(STRINGS "${PXSYS_WAMR_PATCH_SERIES}" pxsys_wamr_patch_names)
foreach(pxsys_wamr_patch_name IN LISTS pxsys_wamr_patch_names)
    string(STRIP "${pxsys_wamr_patch_name}" pxsys_wamr_patch_name)
    if(NOT pxsys_wamr_patch_name STREQUAL "" AND
       NOT pxsys_wamr_patch_name MATCHES "^#")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${PXSYS_WAMR_ESP_IDF_DIR}/wamr/patches/${pxsys_wamr_patch_name}")
    endif()
endforeach()

function(pxsys_replace_wamr_source wamr_component original_source replacement_source)
    get_property(wamr_component_sources TARGET ${wamr_component} PROPERTY SOURCES)
    list(FIND wamr_component_sources "${original_source}" original_source_index)
    if(original_source_index EQUAL -1)
        message(FATAL_ERROR
            "WAMR target does not contain source to replace: ${original_source}")
    endif()
    list(REMOVE_AT wamr_component_sources ${original_source_index})
    set_property(TARGET ${wamr_component} PROPERTY SOURCES "${wamr_component_sources}")
    target_sources(${wamr_component} PRIVATE "${replacement_source}")
endfunction()

function(pxsys_configure_wamr_esp_idf wamr_component)
    if(NOT TARGET ${wamr_component})
        message(FATAL_ERROR "WAMR component target is missing: ${wamr_component}")
    endif()
    if(CONFIG_WAMR_AOT_CODE_IN_PSRAM AND CONFIG_IDF_TARGET_ESP32S31 AND
       CONFIG_ESP_SYSTEM_MEMPROT AND CONFIG_ESP_SYSTEM_MEMPROT_PMP AND
       CONFIG_SPIRAM_PRE_CONFIGURE_MEMORY_PROTECTION)
        message(FATAL_ERROR
            "ESP32-S31 WAMR AOT code uses the dynamic PSRAM heap, but "
            "CONFIG_SPIRAM_PRE_CONFIGURE_MEMORY_PROTECTION locks that heap "
            "non-executable. Disable the PSRAM preconfigured protection or "
            "disable CONFIG_WAMR_AOT_CODE_IN_PSRAM.")
    endif()
    if(NOT EXISTS "${PXSYS_WAMR_ESP_IDF_COMPAT_HEADER}")
        message(FATAL_ERROR
            "PXA System WAMR compatibility header is missing: "
            "${PXSYS_WAMR_ESP_IDF_COMPAT_HEADER}")
    endif()

    set(wamr_root "${PXSYS_WAMR_ROOT}/wamr")
    set(wamr_overlay "${CMAKE_CURRENT_BINARY_DIR}/pxsys-wamr-overlay")
    pxsys_find_python(pxsys_python)
    execute_process(
        COMMAND "${pxsys_python}" "${PXSYS_WAMR_OVERLAY_TOOL}"
                --source "${wamr_root}"
                --output "${wamr_overlay}"
                --metadata "${PXSYS_WAMR_METADATA}"
                --series "${PXSYS_WAMR_PATCH_SERIES}"
        RESULT_VARIABLE patch_status
        OUTPUT_VARIABLE patch_output
        ERROR_VARIABLE patch_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT patch_status EQUAL 0)
        message(FATAL_ERROR "Cannot prepare PXA WAMR overlay: ${patch_error}")
    endif()
    message(STATUS "${patch_output}")

    set(relative_platform_header "core/shared/platform/esp-idf/platform_internal.h")
    set(relative_file_source "core/shared/platform/esp-idf/espidf_file.c")
    set(relative_memmap_source "core/shared/platform/esp-idf/espidf_memmap.c")
    set(relative_xtensa_reloc "core/iwasm/aot/arch/aot_reloc_xtensa.c")

    set(required_overlay_sources
        ${relative_platform_header} ${relative_file_source})
    if(CONFIG_IDF_TARGET_ARCH_XTENSA)
        list(APPEND required_overlay_sources ${relative_xtensa_reloc})
    endif()
    foreach(relative_source IN LISTS required_overlay_sources)
        if(NOT EXISTS "${wamr_overlay}/${relative_source}")
            message(FATAL_ERROR "Patched WAMR source is missing: ${relative_source}")
        endif()
    endforeach()
    if(NOT EXISTS "${wamr_root}/${relative_memmap_source}")
        message(FATAL_ERROR "Unsupported WAMR source layout: ${relative_memmap_source}")
    endif()

    target_include_directories(${wamr_component} BEFORE PRIVATE
        "${wamr_overlay}/core/shared/platform/esp-idf")
    pxsys_replace_wamr_source(${wamr_component}
        "${wamr_root}/${relative_file_source}"
        "${wamr_overlay}/${relative_file_source}")
    if(CONFIG_IDF_TARGET_ARCH_XTENSA)
        pxsys_replace_wamr_source(${wamr_component}
            "${wamr_root}/${relative_xtensa_reloc}"
            "${wamr_overlay}/${relative_xtensa_reloc}")
    endif()
    pxsys_replace_wamr_source(${wamr_component}
        "${wamr_root}/${relative_memmap_source}"
        "${PXSYS_WAMR_ESP_IDF_DIR}/wamr/overrides/espidf_memmap.c")

    target_compile_options(${wamr_component} PRIVATE
        $<$<COMPILE_LANGUAGE:C,CXX>:-include>
        $<$<COMPILE_LANGUAGE:C,CXX>:${PXSYS_WAMR_ESP_IDF_COMPAT_HEADER}>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-error=dangling-pointer>)
    target_link_libraries(${wamr_component} PRIVATE __idf_esp_mm)
endfunction()
