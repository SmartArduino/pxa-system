include_guard(GLOBAL)
include(CMakeParseArguments)

if(DEFINED PXA_CPP_SDK_DIR AND IS_DIRECTORY "${PXA_CPP_SDK_DIR}/include"
   AND CMAKE_CXX_COMPILER_LOADED)
    include("${CMAKE_CURRENT_LIST_DIR}/PxaCppFeatures.cmake")
    pxa_check_cpp_features()
    add_library(pxa_guest_cpp STATIC "${PXA_CPP_SDK_DIR}/src/runtime.cpp")
    add_library(Pxa::Cpp ALIAS pxa_guest_cpp)
    target_include_directories(pxa_guest_cpp PUBLIC
        "${PXA_CPP_SDK_DIR}/include")
    if("cxx_std_26" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
        target_compile_features(pxa_guest_cpp PUBLIC cxx_std_26)
    else()
        target_compile_options(pxa_guest_cpp PUBLIC
            "$<$<COMPILE_LANGUAGE:CXX>:-std=c++2c>")
    endif()
    target_compile_options(pxa_guest_cpp PUBLIC -fno-exceptions -fno-rtti)
    target_compile_options(pxa_guest_cpp PRIVATE
        -O3 -ffunction-sections -fdata-sections)
endif()

if((NOT DEFINED PXA_GUEST_SDK_DIR OR
    NOT IS_DIRECTORY "${PXA_GUEST_SDK_DIR}/include") AND
   (NOT DEFINED PXA_CPP_SDK_DIR OR
    NOT IS_DIRECTORY "${PXA_CPP_SDK_DIR}/include"))
    message(FATAL_ERROR "Set PXA_GUEST_SDK_DIR or PXA_CPP_SDK_DIR")
endif()
if(NOT DEFINED PXA_ARTIFACT_DIR OR PXA_ARTIFACT_DIR STREQUAL "")
    message(FATAL_ERROR "PXA_ARTIFACT_DIR is required")
endif()

function(_pxa_guest_target_defaults target)
    if(DEFINED PXA_GUEST_SDK_DIR AND IS_DIRECTORY "${PXA_GUEST_SDK_DIR}/include")
        target_include_directories(${target} PUBLIC "${PXA_GUEST_SDK_DIR}/include")
    endif()
    target_compile_options(${target} PRIVATE
        -O3 -fno-builtin -ffunction-sections -fdata-sections)
    if(PXA_APP_DEFINITIONS)
        target_compile_definitions(${target} PRIVATE ${PXA_APP_DEFINITIONS})
    endif()
endfunction()

function(_pxa_collect_c_sources output)
    cmake_parse_arguments(PXA "" "" "SOURCES;SOURCE_DIRS" ${ARGN})
    set(_pxa_sources ${PXA_SOURCES})
    foreach(_pxa_source_dir IN LISTS PXA_SOURCE_DIRS)
        if(NOT IS_ABSOLUTE "${_pxa_source_dir}")
            set(_pxa_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/${_pxa_source_dir}")
        endif()
        if(NOT IS_DIRECTORY "${_pxa_source_dir}")
            message(FATAL_ERROR "PXA source directory does not exist: ${_pxa_source_dir}")
        endif()
        file(GLOB _pxa_directory_sources CONFIGURE_DEPENDS
            LIST_DIRECTORIES false "${_pxa_source_dir}/*.c"
            "${_pxa_source_dir}/*.cpp")
        if(NOT _pxa_directory_sources)
            message(FATAL_ERROR "PXA source directory has no .c or .cpp files: ${_pxa_source_dir}")
        endif()
        list(APPEND _pxa_sources ${_pxa_directory_sources})
    endforeach()
    list(REMOVE_DUPLICATES _pxa_sources)
    set(${output} "${_pxa_sources}" PARENT_SCOPE)
endfunction()

# A module is an object library so several source folders can be composed into
# one Component without creating an additional Wasm module or ambient imports.
function(pxa_add_module target)
    cmake_parse_arguments(PXA "" ""
        "SOURCES;SOURCE_DIRS;INCLUDE_DIRS;DEFINITIONS" ${ARGN})
    _pxa_collect_c_sources(_pxa_sources SOURCES ${PXA_SOURCES}
        SOURCE_DIRS ${PXA_SOURCE_DIRS})
    if(NOT _pxa_sources)
        message(FATAL_ERROR
            "pxa_add_module(${target}) requires SOURCES or SOURCE_DIRS")
    endif()
    add_library(${target} OBJECT ${_pxa_sources})
    _pxa_guest_target_defaults(${target})
    target_compile_features(${target} PRIVATE c_std_11)
    target_include_directories(${target} PUBLIC ${PXA_INCLUDE_DIRS})
    target_compile_definitions(${target} PRIVATE ${PXA_DEFINITIONS})
endfunction()

function(pxa_add_component target)
    cmake_parse_arguments(PXA "CPP" "COMPONENT_ID"
        "SOURCES;SOURCE_DIRS;MODULES;LIBRARIES;INCLUDE_DIRS;DEFINITIONS" ${ARGN})
    string(LENGTH "${PXA_COMPONENT_ID}" _pxa_component_id_length)
    if(_pxa_component_id_length LESS 1 OR _pxa_component_id_length GREATER 64 OR
       NOT PXA_COMPONENT_ID MATCHES "^[a-z][a-z0-9._-]*$")
        message(FATAL_ERROR "pxa_add_component(${target}) requires a valid COMPONENT_ID")
    endif()
    _pxa_collect_c_sources(_pxa_sources SOURCES ${PXA_SOURCES}
        SOURCE_DIRS ${PXA_SOURCE_DIRS})
    if(NOT _pxa_sources AND NOT PXA_MODULES)
        message(FATAL_ERROR
            "pxa_add_component(${target}) requires SOURCES, SOURCE_DIRS or MODULES")
    endif()
    add_executable(${target} ${_pxa_sources})
    _pxa_guest_target_defaults(${target})
    if(PXA_CPP)
        if(NOT TARGET Pxa::Cpp)
            message(FATAL_ERROR "PXA_CPP_SDK_DIR must point to the PXA C++ SDK")
        endif()
        target_link_libraries(${target} PRIVATE Pxa::Cpp)
        set(_pxa_imports_file "${PXA_CPP_SDK_DIR}/pxa-imports.txt")
    else()
        if(NOT DEFINED PXA_GUEST_SDK_DIR OR
           NOT EXISTS "${PXA_GUEST_SDK_DIR}/pxa-imports.txt")
            message(FATAL_ERROR "PXA_GUEST_SDK_DIR must point to the PXA Guest C SDK")
        endif()
        target_compile_features(${target} PRIVATE c_std_11)
        set(_pxa_imports_file "${PXA_GUEST_SDK_DIR}/pxa-imports.txt")
    endif()
    target_include_directories(${target} PRIVATE ${PXA_INCLUDE_DIRS})
    target_compile_definitions(${target} PRIVATE ${PXA_DEFINITIONS})
    target_link_libraries(${target} PRIVATE ${PXA_MODULES} ${PXA_LIBRARIES})
    target_link_options(${target} PRIVATE
        -mexec-model=reactor
        -Wl,--gc-sections
        "-Wl,--allow-undefined-file=${_pxa_imports_file}"
        -Wl,--export=pxa_app_start
        -Wl,--export=pxa_app_on_event
        -Wl,--export=pxa_app_stop
        -Wl,--export=__heap_base
        -Wl,--export=__data_end)
    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${PXA_COMPONENT_ID}"
        PREFIX ""
        SUFFIX ".wasm"
        RUNTIME_OUTPUT_DIRECTORY "${PXA_ARTIFACT_DIR}")
endfunction()

function(pxa_add_app target)
    pxa_add_component(${target} CPP ${ARGN})
endfunction()
