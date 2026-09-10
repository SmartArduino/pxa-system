include_guard(GLOBAL)

get_filename_component(PXSYS_WAMR_METADATA_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(PXSYS_WAMR_METADATA "${PXSYS_WAMR_METADATA_ROOT}/config/wamr.json")

function(pxsys_find_python output_variable)
    if(DEFINED PYTHON AND EXISTS "${PYTHON}")
        set(${output_variable} "${PYTHON}" PARENT_SCOPE)
        return()
    endif()
    find_program(pxsys_python NAMES python3 python REQUIRED)
    set(${output_variable} "${pxsys_python}" PARENT_SCOPE)
endfunction()

function(pxsys_read_wamr_metadata output_variable key)
    pxsys_find_python(pxsys_python)
    execute_process(
        COMMAND "${pxsys_python}"
                "${PXSYS_WAMR_METADATA_ROOT}/tools/wamr/metadata.py"
                "${key}" --metadata "${PXSYS_WAMR_METADATA}"
        RESULT_VARIABLE metadata_status
        OUTPUT_VARIABLE metadata_value
        ERROR_VARIABLE metadata_error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT metadata_status EQUAL 0)
        message(FATAL_ERROR "Cannot read PXA WAMR metadata: ${metadata_error}")
    endif()
    set(${output_variable} "${metadata_value}" PARENT_SCOPE)
endfunction()

function(pxsys_apply_wamr_metadata target)
    pxsys_read_wamr_metadata(engine_abi engine_abi)
    target_compile_definitions(${target} PRIVATE
        PXSYS_WAMR_ENGINE_ABI="${engine_abi}")
endfunction()
