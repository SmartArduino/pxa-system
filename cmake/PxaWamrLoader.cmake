# Share the same owned-section loader contract on desktop and ESP-IDF.
# Materialize the small patch out of tree; never edit the pinned submodule.
function(pxa_configure_wamr_loader target wamr_root)
    get_filename_component(system_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(series "${system_root}/tools/wamr/patches/series")
    set(overlay "${CMAKE_CURRENT_BINARY_DIR}/pxa-wamr-loader")
    set(overlay_tool "${system_root}/tools/wamr/prepare_overlay.py")
    if(DEFINED PXSYS_WAMR_OVERLAY_TOOL)
        set(overlay_tool "${PXSYS_WAMR_OVERLAY_TOOL}")
    endif()
    if(DEFINED PXA_HOST_SDK_ROOT)
        set(overlay_tool "${PXA_HOST_SDK_ROOT}/tools/host-sdk/prepare_wamr_overlay.py")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${series}"
        "${overlay_tool}" "${system_root}/config/wamr.json")
    file(STRINGS "${series}" patch_names)
    foreach(patch_name IN LISTS patch_names)
        string(STRIP "${patch_name}" patch_name)
        if(NOT patch_name STREQUAL "" AND NOT patch_name MATCHES "^#")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                "${system_root}/tools/wamr/patches/${patch_name}")
        endif()
    endforeach()
    execute_process(COMMAND "${Python3_EXECUTABLE}"
        "${overlay_tool}" --metadata "${system_root}/config/wamr.json"
        --source "${wamr_root}" --output "${overlay}" --series "${series}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "Cannot prepare AOT loader: ${error}")
    endif()
    get_property(sources TARGET ${target} PROPERTY SOURCES)
    set(found FALSE)
    foreach(source IN LISTS sources)
        if(source MATCHES "/core/iwasm/aot/aot_loader\\.c$")
            list(REMOVE_ITEM sources "${source}")
            set(found TRUE)
        endif()
    endforeach()
    if(found)
        set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
        target_sources(${target} PRIVATE "${overlay}/core/iwasm/aot/aot_loader.c")
    endif()
    target_sources(${target} PRIVATE
        "${system_root}/libpxa/adapters/wamr/pxa_wamr_aot_reader.c")
    target_include_directories(${target} PRIVATE
        "${wamr_root}/core/iwasm/common"
        "${system_root}/libpxa/include"
        "${system_root}/libpxa/adapters/wamr"
        "${system_root}/libpxa/adapters/include")
endfunction()
