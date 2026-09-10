# Project-owned compatibility setup for the WAMR revision pinned by PXA System.
# Patches are applied to a build-tree overlay; the submodule is never modified.

get_filename_component(PXSYS_WAMR_ESP_IDF_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(PXSYS_WAMR_ROOT "${PXSYS_WAMR_ESP_IDF_DIR}/../.." ABSOLUTE)
include("${PXSYS_WAMR_ROOT}/cmake/PxaWamrMetadata.cmake")
set(PXSYS_WAMR_PATCH_SERIES
    "${PXSYS_WAMR_ESP_IDF_DIR}/wamr/patches/series")
set(PXSYS_WAMR_OVERLAY_TOOL
    "${PXSYS_WAMR_ROOT}/tools/wamr/prepare_overlay.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PXSYS_WAMR_METADATA}"
    "${PXSYS_WAMR_OVERLAY_TOOL}"
    "${PXSYS_WAMR_PATCH_SERIES}")
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
    set(relative_aot_loader "core/iwasm/aot/aot_loader.c")
    set(relative_xtensa_reloc "core/iwasm/aot/arch/aot_reloc_xtensa.c")

    foreach(relative_source IN ITEMS
            ${relative_platform_header} ${relative_file_source}
            ${relative_aot_loader} ${relative_xtensa_reloc})
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
    pxsys_replace_wamr_source(${wamr_component}
        "${wamr_root}/${relative_aot_loader}"
        "${wamr_overlay}/${relative_aot_loader}")
    pxsys_replace_wamr_source(${wamr_component}
        "${wamr_root}/${relative_xtensa_reloc}"
        "${wamr_overlay}/${relative_xtensa_reloc}")
    pxsys_replace_wamr_source(${wamr_component}
        "${wamr_root}/${relative_memmap_source}"
        "${PXSYS_WAMR_ESP_IDF_DIR}/wamr/overrides/espidf_memmap.c")

    target_link_libraries(${wamr_component} PRIVATE __idf_esp_mm)
endfunction()
