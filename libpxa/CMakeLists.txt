cmake_minimum_required(VERSION 3.16)

project(libpxa VERSION 0.1.0 LANGUAGES C)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(PXA_TOP_LEVEL ON)
else()
    set(PXA_TOP_LEVEL OFF)
endif()
option(PXA_BUILD_TESTS "Build libpxa host tests" ${PXA_TOP_LEVEL})
option(PXA_WARNINGS_AS_ERRORS "Treat libpxa warnings as errors"
       ${PXA_TOP_LEVEL})
option(PXA_BUILD_ADAPTERS "Build Layer 2 POSIX/OpenSSL adapters"
       ${PXA_TOP_LEVEL})
if(PXA_BUILD_TESTS)
    find_package(Python3 COMPONENTS Interpreter QUIET)
endif()
set(_PXA_WAMR_DEFAULT_SOURCE_DIR "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../wamr/CMakeLists.txt")
    set(_PXA_WAMR_DEFAULT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../wamr")
endif()
set(PXA_WAMR_SOURCE_DIR "${_PXA_WAMR_DEFAULT_SOURCE_DIR}" CACHE PATH
    "Path to a WAMR source tree (built as vmlib) for the WAMR engine adapter")
set(PXA_WASI_SDK_DIR "" CACHE PATH
    "Optional WASI SDK root for the wasi-libc WAMR integration test")
set(PXA_WAMRC "" CACHE FILEPATH
    "Optional wamrc executable for WAMR AOT integration tests")
set(PXA_LVGL_SOURCE_DIR "" CACHE PATH
    "Path to an LVGL source tree for the LVGL canvas adapter")
set(PXA_LVGL_TARGET "" CACHE STRING
    "Existing configured LVGL target to use instead of compiling a source tree")
set(PXA_LZ4_SOURCE_DIR "" CACHE PATH
    "Optional path containing lz4.c and lz4.h; otherwise use system liblz4")
set(_PXA_PACKAGE_TOOLS_DEFAULT_DIR "")
if(EXISTS
   "${CMAKE_CURRENT_SOURCE_DIR}/../tools/package/build_package_manifest.py")
    set(_PXA_PACKAGE_TOOLS_DEFAULT_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/../tools/package")
endif()
set(PXA_PACKAGE_TOOLS_DIR "${_PXA_PACKAGE_TOOLS_DEFAULT_DIR}" CACHE PATH
    "Optional PXA package-tool directory for container integration tests")

include(cmake/pxaSources.cmake)
pxa_collect_sources(PXA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(pxa ${PXA_SOURCES})
add_library(pxa::pxa ALIAS pxa)
set_target_properties(pxa PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    WINDOWS_EXPORT_ALL_SYMBOLS ON)

target_compile_features(pxa PUBLIC c_std_99)
target_include_directories(pxa PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(pxa PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pxa PRIVATE -Wall -Wextra -Wpedantic)
    if(PXA_WARNINGS_AS_ERRORS)
        target_compile_options(pxa PRIVATE -Werror)
    endif()
endif()

if(PXA_BUILD_ADAPTERS)
    find_package(OpenSSL REQUIRED)

    set(_pxa_lz4_sources)
    set(_pxa_lz4_libraries)
    if(PXA_LZ4_SOURCE_DIR)
        if(NOT EXISTS "${PXA_LZ4_SOURCE_DIR}/lz4.c" OR
           NOT EXISTS "${PXA_LZ4_SOURCE_DIR}/lz4.h")
            message(FATAL_ERROR
                "PXA_LZ4_SOURCE_DIR must contain lz4.c and lz4.h")
        endif()
        list(APPEND _pxa_lz4_sources "${PXA_LZ4_SOURCE_DIR}/lz4.c")
        set(_pxa_lz4_include_dir "${PXA_LZ4_SOURCE_DIR}")
    else()
        find_path(PXA_LZ4_INCLUDE_DIR NAMES lz4.h)
        find_library(PXA_LZ4_LIBRARY NAMES lz4)
        if(NOT PXA_LZ4_INCLUDE_DIR OR NOT PXA_LZ4_LIBRARY)
            message(FATAL_ERROR
                "LZ4 is required for the POSIX installer. Install liblz4 or "
                "set PXA_LZ4_SOURCE_DIR to a directory containing lz4.c/lz4.h")
        endif()
        set(_pxa_lz4_include_dir "${PXA_LZ4_INCLUDE_DIR}")
        list(APPEND _pxa_lz4_libraries
            "$<BUILD_INTERFACE:${PXA_LZ4_LIBRARY}>"
            "$<INSTALL_INTERFACE:lz4>")
    endif()

    add_library(pxa_adapters_openssl adapters/openssl/pxa_openssl.c)
    add_library(pxa::adapters_openssl ALIAS pxa_adapters_openssl)
    set_target_properties(pxa_adapters_openssl PROPERTIES
        EXPORT_NAME adapters_openssl
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})
    target_compile_features(pxa_adapters_openssl PUBLIC c_std_99)
    target_include_directories(pxa_adapters_openssl PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/adapters/include>
        $<INSTALL_INTERFACE:include/pxa-adapters>)
    target_link_libraries(pxa_adapters_openssl PUBLIC pxa OpenSSL::Crypto)

    add_library(pxa_adapters_posix
        adapters/posix/pxa_posix_fs.c
        adapters/posix/pxa_posix_scheduler_store.c
        adapters/posix/pxa_posix_storage.c
        adapters/posix/pxa_posix_installer.c
        ${_pxa_lz4_sources})
    add_library(pxa::adapters_posix ALIAS pxa_adapters_posix)
    set_target_properties(pxa_adapters_posix PROPERTIES
        EXPORT_NAME adapters_posix
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})
    target_compile_features(pxa_adapters_posix PUBLIC c_std_99)
    target_include_directories(pxa_adapters_posix PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/adapters/include>
        $<INSTALL_INTERFACE:include/pxa-adapters>)
    target_include_directories(pxa_adapters_posix PRIVATE
        ${_pxa_lz4_include_dir})
    target_link_libraries(pxa_adapters_posix PUBLIC pxa_adapters_openssl
        PRIVATE ${_pxa_lz4_libraries})

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        foreach(adapter pxa_adapters_openssl pxa_adapters_posix)
            target_compile_options(${adapter} PRIVATE -Wall -Wextra -Wpedantic)
            if(PXA_WARNINGS_AS_ERRORS)
                target_compile_options(${adapter} PRIVATE -Werror)
            endif()
        endforeach()
    endif()

    if(PXA_BUILD_TESTS)
        add_executable(pxa_adapters_test adapters/tests/adapters_test.c)
        target_include_directories(pxa_adapters_test PRIVATE
            ${_pxa_lz4_include_dir})
        target_link_libraries(pxa_adapters_test PRIVATE pxa_adapters_posix)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(pxa_adapters_test PRIVATE
                -Wall -Wextra -Wpedantic)
            if(PXA_WARNINGS_AS_ERRORS)
                target_compile_options(pxa_adapters_test PRIVATE -Werror)
            endif()
        endif()
        add_test(NAME pxa_adapters_test COMMAND pxa_adapters_test)
        if(Python3_Interpreter_FOUND AND PXA_PACKAGE_TOOLS_DIR)
            foreach(_pxa_package_tool build_package_manifest.py
                    build_publisher_lineage.py build_pxa_container.py)
                if(NOT EXISTS
                   "${PXA_PACKAGE_TOOLS_DIR}/${_pxa_package_tool}")
                    message(FATAL_ERROR
                        "PXA_PACKAGE_TOOLS_DIR is missing ${_pxa_package_tool}")
                endif()
            endforeach()
            add_executable(pxa_container_install_tool
                adapters/tests/container_install_tool.c)
            target_link_libraries(pxa_container_install_tool PRIVATE
                pxa_adapters_posix)
            add_test(NAME pxa_container_tool_test
                COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/adapters/tests/container_tool_test.py
                    $<TARGET_FILE:pxa_container_install_tool>
                    ${PXA_PACKAGE_TOOLS_DIR})
        endif()
    endif()
endif()

if(PXA_BUILD_ADAPTERS AND (PXA_LVGL_TARGET OR PXA_LVGL_SOURCE_DIR))
    if(PXA_LVGL_TARGET)
        if(NOT TARGET ${PXA_LVGL_TARGET})
            message(FATAL_ERROR
                "PXA_LVGL_TARGET does not name an existing CMake target")
        endif()
        set(_pxa_lvgl_target ${PXA_LVGL_TARGET})
    else()
        message(STATUS
            "No PXA_LVGL_TARGET supplied; compiling the LVGL source-tree fallback")
        file(GLOB_RECURSE _pxa_lvgl_sources CONFIGURE_DEPENDS
            "${PXA_LVGL_SOURCE_DIR}/src/*.c")
        list(FILTER _pxa_lvgl_sources EXCLUDE REGEX "/test/|/tests/")
        add_library(pxa_lvgl STATIC ${_pxa_lvgl_sources})
        target_include_directories(pxa_lvgl PUBLIC
            ${PXA_LVGL_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/adapters/tests)
        target_compile_definitions(pxa_lvgl PUBLIC
            LV_CONF_PATH="${CMAKE_CURRENT_SOURCE_DIR}/adapters/tests/lv_conf_test.h")
        set(_pxa_lvgl_target pxa_lvgl)
    endif()

    add_library(pxa_adapters_lvgl
        adapters/lvgl/pxa_lvgl_ui.c)
    add_library(pxa::adapters_lvgl ALIAS pxa_adapters_lvgl)
    set_target_properties(pxa_adapters_lvgl PROPERTIES
        EXPORT_NAME adapters_lvgl
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})
    target_compile_features(pxa_adapters_lvgl PUBLIC c_std_99)
    target_include_directories(pxa_adapters_lvgl PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/adapters/include>
        $<INSTALL_INTERFACE:include/pxa-adapters>)
    target_link_libraries(pxa_adapters_lvgl PUBLIC pxa)
    target_link_libraries(pxa_adapters_lvgl PRIVATE
        $<BUILD_INTERFACE:${_pxa_lvgl_target}>)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pxa_adapters_lvgl PRIVATE
            -Wall -Wextra -Wpedantic)
        if(PXA_WARNINGS_AS_ERRORS)
            target_compile_options(pxa_adapters_lvgl PRIVATE -Werror)
        endif()
    endif()

    if(PXA_BUILD_TESTS)
        add_executable(pxa_lvgl_ui_test
            adapters/tests/lvgl_ui_test.c)
        target_link_libraries(pxa_lvgl_ui_test PRIVATE
            pxa_adapters_lvgl ${_pxa_lvgl_target})
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(pxa_lvgl_ui_test PRIVATE
                -Wall -Wextra -Wpedantic)
            if(PXA_WARNINGS_AS_ERRORS)
                target_compile_options(pxa_lvgl_ui_test PRIVATE -Werror)
            endif()
        endif()
        add_test(NAME pxa_lvgl_ui_test
                 COMMAND pxa_lvgl_ui_test)

    endif()
endif()

if(PXA_BUILD_ADAPTERS AND PXA_WAMR_SOURCE_DIR)
    set(WAMR_BUILD_INTERP 1)
    set(WAMR_BUILD_AOT 1)
    set(WAMR_BUILD_JIT 0)
    set(WAMR_BUILD_LIBC_BUILTIN 1)
    set(WAMR_BUILD_LIBC_WASI 1)
    set(WAMR_BUILD_LIBC_UVWASI 0)
    set(WAMR_BUILD_LIBC_EMCC 0)
    set(WAMR_BUILD_THREAD_MGR 0)
    set(WAMR_BUILD_SHARED_MEMORY 0)
    set(WAMR_BUILD_SIMD 0)
    add_subdirectory(${PXA_WAMR_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/wamr EXCLUDE_FROM_ALL)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        # WAMR's interpreter uses computed jumps and installs its own signal
        # handlers, which are incompatible with sanitizer instrumentation;
        # keep the third-party library unsanitized and cover the adapter and
        # tests instead.
        target_compile_options(vmlib PRIVATE -fno-sanitize=all)
    endif()

    add_library(pxa_adapters_wamr adapters/wamr/pxa_wamr_engine.c)
    add_library(pxa::adapters_wamr ALIAS pxa_adapters_wamr)
    set_target_properties(pxa_adapters_wamr PROPERTIES
        EXPORT_NAME adapters_wamr
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})
    target_compile_features(pxa_adapters_wamr PUBLIC c_std_99)
    target_include_directories(pxa_adapters_wamr PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/adapters/include>
        $<INSTALL_INTERFACE:include/pxa-adapters>)
    target_link_libraries(pxa_adapters_wamr PUBLIC pxa vmlib)
    target_compile_definitions(pxa_adapters_wamr PRIVATE PXA_WAMR_LIBC_WASI=1)
    if(WAMR_BUILD_ALLOC_WITH_USER_DATA EQUAL 1)
        target_compile_definitions(pxa_adapters_wamr PRIVATE
            WASM_MEM_ALLOC_WITH_USER_DATA=1)
    endif()
    if(WAMR_BUILD_ALLOC_WITH_USAGE EQUAL 1)
        target_compile_definitions(pxa_adapters_wamr PRIVATE
            WASM_MEM_ALLOC_WITH_USAGE=1)
    endif()
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pxa_adapters_wamr PRIVATE
            -Wall -Wextra -Wpedantic)
        if(PXA_WARNINGS_AS_ERRORS)
            target_compile_options(pxa_adapters_wamr PRIVATE -Werror)
        endif()
    endif()

    if(PXA_BUILD_TESTS)
        find_program(PXA_WASM_CLANG clang)
        get_filename_component(PXA_GUEST_SDK_DEFAULT
            "${CMAKE_CURRENT_SOURCE_DIR}/../sdk/guest-c/include"
            ABSOLUTE)
        set(PXA_GUEST_SDK_INCLUDE "${PXA_GUEST_SDK_DEFAULT}" CACHE PATH
            "PXA Guest SDK include directory" FORCE)
        if(PXA_WASM_CLANG AND EXISTS "${PXA_GUEST_SDK_INCLUDE}/pxa.h")
            function(pxa_build_guest name source)
                set(output "${CMAKE_CURRENT_BINARY_DIR}/${name}.wasm")
                add_custom_command(
                    OUTPUT ${output}
                    COMMAND ${PXA_WASM_CLANG} --target=wasm32 -O2 -nostdlib
                            -ffreestanding -Wall -Wextra -Werror
                            -I${PXA_GUEST_SDK_INCLUDE}
                            -Wl,--no-entry
                            -Wl,--export=pxa_app_start
                            -Wl,--export=pxa_app_on_event
                            -Wl,--export=pxa_app_stop
                            -Wl,--export=__heap_base
                            -Wl,--export=__data_end
                            -o ${output} ${source}
                    DEPENDS ${source}
                            "${PXA_GUEST_SDK_INCLUDE}/pxa.h"
                            "${PXA_GUEST_SDK_INCLUDE}/pxa_audio.h"
                            "${PXA_GUEST_SDK_INCLUDE}/pxa_canvas.h"
                            "${PXA_GUEST_SDK_INCLUDE}/pxa_net.h"
                    COMMENT "Compiling PXA test guest ${name} (wasm32)")
                add_custom_target(${name} DEPENDS ${output})
                set(${name}_WASM "${output}" PARENT_SCOPE)
            endfunction()
            pxa_build_guest(wamr_guest
                ${CMAKE_CURRENT_SOURCE_DIR}/adapters/tests/wamr_guest.c)
            pxa_build_guest(wamr_wasi_import_guest
                ${CMAKE_CURRENT_SOURCE_DIR}/adapters/tests/wamr_wasi_import_guest.c)
            pxa_build_guest(wamr_wasi_system_guest
                ${CMAKE_CURRENT_SOURCE_DIR}/adapters/tests/wamr_wasi_system_guest.c)
            pxa_build_guest(wamr_env_import_guest
                ${CMAKE_CURRENT_SOURCE_DIR}/adapters/tests/wamr_env_import_guest.c)
            if(PXA_WASI_SDK_DIR AND
               EXISTS "${PXA_WASI_SDK_DIR}/bin/clang" AND
               EXISTS "${PXA_WASI_SDK_DIR}/share/wasi-sysroot")
                set(wamr_wasi_libc_guest_WASM
                    "${CMAKE_CURRENT_BINARY_DIR}/wamr_wasi_libc_guest.wasm")
                add_custom_command(
                    OUTPUT ${wamr_wasi_libc_guest_WASM}
                    COMMAND "${PXA_WASI_SDK_DIR}/bin/clang"
                            --target=wasm32-wasip1
                            --sysroot=${PXA_WASI_SDK_DIR}/share/wasi-sysroot
                            -O2 -mexec-model=reactor
                            -I${PXA_GUEST_SDK_INCLUDE}
                            -I${CMAKE_CURRENT_SOURCE_DIR}/adapters/tests/wasi_libc_guest
                            -Wl,--export=pxa_app_start
                            -Wl,--export=pxa_app_on_event
                            -Wl,--export=pxa_app_stop
                            -o ${wamr_wasi_libc_guest_WASM}
                            ${CMAKE_CURRENT_SOURCE_DIR}/adapters/tests/wasi_libc_guest/main.c
                            ${CMAKE_CURRENT_SOURCE_DIR}/adapters/tests/wasi_libc_guest/arithmetic.c
                    DEPENDS
                        ${CMAKE_CURRENT_SOURCE_DIR}/adapters/tests/wasi_libc_guest/main.c
                        ${CMAKE_CURRENT_SOURCE_DIR}/adapters/tests/wasi_libc_guest/arithmetic.c
                    COMMENT "Compiling PXA wasi-libc integration guest")
                add_custom_target(wamr_wasi_libc_guest
                    DEPENDS ${wamr_wasi_libc_guest_WASM})
                if(PXA_WAMRC AND EXISTS "${PXA_WAMRC}")
                    set(wamr_wasi_libc_guest_AOT
                        "${CMAKE_CURRENT_BINARY_DIR}/wamr_wasi_libc_guest.aot")
                    add_custom_command(
                        OUTPUT ${wamr_wasi_libc_guest_AOT}
                        COMMAND "${PXA_WAMRC}" --target=x86_64
                                -o ${wamr_wasi_libc_guest_AOT}
                                ${wamr_wasi_libc_guest_WASM}
                        DEPENDS ${wamr_wasi_libc_guest_WASM}
                        COMMENT "Compiling PXA wasi-libc integration AOT")
                    add_custom_target(wamr_wasi_libc_guest_aot
                        DEPENDS ${wamr_wasi_libc_guest_AOT})
                endif()
            endif()
            add_executable(pxa_wamr_engine_test
                adapters/tests/wamr_engine_test.c)
            target_link_libraries(pxa_wamr_engine_test PRIVATE
                pxa_adapters_posix pxa_adapters_wamr)
            target_compile_definitions(pxa_wamr_engine_test PRIVATE
                PXA_WAMR_TEST_GUEST_PATH="${wamr_guest_WASM}"
                PXA_WAMR_TEST_WASI_IMPORT_PATH="${wamr_wasi_import_guest_WASM}"
                PXA_WAMR_TEST_WASI_SYSTEM_PATH="${wamr_wasi_system_guest_WASM}"
                PXA_WAMR_TEST_ENV_IMPORT_PATH="${wamr_env_import_guest_WASM}")
            add_dependencies(pxa_wamr_engine_test wamr_guest
                wamr_wasi_import_guest wamr_wasi_system_guest
                wamr_env_import_guest)
            if(TARGET wamr_wasi_libc_guest)
                target_compile_definitions(pxa_wamr_engine_test PRIVATE
                    PXA_WAMR_TEST_WASI_LIBC_PATH="${wamr_wasi_libc_guest_WASM}")
                add_dependencies(pxa_wamr_engine_test wamr_wasi_libc_guest)
            endif()
            if(TARGET wamr_wasi_libc_guest_aot)
                target_compile_definitions(pxa_wamr_engine_test PRIVATE
                    PXA_WAMR_TEST_WASI_LIBC_AOT_PATH="${wamr_wasi_libc_guest_AOT}")
                add_dependencies(pxa_wamr_engine_test
                    wamr_wasi_libc_guest_aot)
            endif()
            if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(pxa_wamr_engine_test PRIVATE
                    -Wall -Wextra -Wpedantic)
                if(PXA_WARNINGS_AS_ERRORS)
                    target_compile_options(pxa_wamr_engine_test PRIVATE -Werror)
                endif()
            endif()
            add_test(NAME pxa_wamr_engine_test
                     COMMAND pxa_wamr_engine_test)
        endif()
    endif()
endif()

if(PXA_BUILD_ADAPTERS AND PXA_WAMR_SOURCE_DIR AND
   (PXA_LVGL_TARGET OR PXA_LVGL_SOURCE_DIR))
    if(PXA_BUILD_TESTS)
        find_program(PXA_WASM_CLANG clang)
        get_filename_component(PXA_GUEST_SDK_DEFAULT
            "${CMAKE_CURRENT_SOURCE_DIR}/../sdk/guest-c/include"
            ABSOLUTE)
        set(PXA_GUEST_SDK_INCLUDE "${PXA_GUEST_SDK_DEFAULT}" CACHE PATH
            "PXA Guest SDK include directory" FORCE)
        if(PXA_WASM_CLANG AND EXISTS "${PXA_GUEST_SDK_INCLUDE}/pxa.h")
            function(pxa_build_guest name source)
                set(output "${CMAKE_CURRENT_BINARY_DIR}/${name}.wasm")
                add_custom_command(
                    OUTPUT ${output}
                    COMMAND ${PXA_WASM_CLANG} --target=wasm32 -O2 -nostdlib
                            -ffreestanding -Wall -Wextra -Werror
                            -I${PXA_GUEST_SDK_INCLUDE}
                            -Wl,--no-entry
                            -Wl,--export=pxa_app_start
                            -Wl,--export=pxa_app_on_event
                            -Wl,--export=pxa_app_stop
                            -Wl,--export=__heap_base
                            -Wl,--export=__data_end
                            -o ${output} ${source}
                    DEPENDS ${source}
                            "${PXA_GUEST_SDK_INCLUDE}/pxa.h"
                            "${PXA_GUEST_SDK_INCLUDE}/pxa_canvas.h"
                            "${PXA_GUEST_SDK_INCLUDE}/pxa_net.h"
                    COMMENT "Compiling PXA test guest ${name} (wasm32)")
                add_custom_target(${name} DEPENDS ${output})
                set(${name}_WASM "${output}" PARENT_SCOPE)
            endfunction()
            pxa_build_guest(ipc_provider_guest
                ${CMAKE_CURRENT_SOURCE_DIR}/adapters/tests/ipc_provider_guest.c)
            pxa_build_guest(ipc_caller_guest
                ${CMAKE_CURRENT_SOURCE_DIR}/adapters/tests/ipc_caller_guest.c)
            add_executable(pxa_ipc_engine_test
                adapters/tests/ipc_engine_test.c)
            target_link_libraries(pxa_ipc_engine_test PRIVATE
                pxa_adapters_wamr pxa_adapters_posix)
            target_compile_definitions(pxa_ipc_engine_test PRIVATE
                PXA_IPC_PROVIDER_GUEST_PATH="${ipc_provider_guest_WASM}"
                PXA_IPC_CALLER_GUEST_PATH="${ipc_caller_guest_WASM}")
            add_dependencies(pxa_ipc_engine_test ipc_provider_guest
                             ipc_caller_guest)
            if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(pxa_ipc_engine_test PRIVATE
                    -Wall -Wextra -Wpedantic)
                if(PXA_WARNINGS_AS_ERRORS)
                    target_compile_options(pxa_ipc_engine_test PRIVATE -Werror)
                endif()
            endif()
            add_test(NAME pxa_ipc_engine_test
                     COMMAND pxa_ipc_engine_test)

            pxa_build_guest(sensor_guest
                ${CMAKE_CURRENT_SOURCE_DIR}/adapters/tests/sensor_guest.c)
            add_executable(pxa_sensor_engine_test
                adapters/tests/sensor_engine_test.c)
            target_link_libraries(pxa_sensor_engine_test PRIVATE
                pxa_adapters_wamr pxa_adapters_posix)
            target_compile_definitions(pxa_sensor_engine_test PRIVATE
                PXA_SENSOR_TEST_GUEST_PATH="${sensor_guest_WASM}")
            add_dependencies(pxa_sensor_engine_test sensor_guest)
            if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(pxa_sensor_engine_test PRIVATE
                    -Wall -Wextra -Wpedantic)
                if(PXA_WARNINGS_AS_ERRORS)
                    target_compile_options(pxa_sensor_engine_test PRIVATE -Werror)
                endif()
            endif()
            add_test(NAME pxa_sensor_engine_test
                     COMMAND pxa_sensor_engine_test)

            pxa_build_guest(scheduler_guest
                ${CMAKE_CURRENT_SOURCE_DIR}/adapters/tests/scheduler_guest.c)
            add_executable(pxa_scheduler_engine_test
                adapters/tests/scheduler_engine_test.c)
            target_link_libraries(pxa_scheduler_engine_test PRIVATE
                pxa_adapters_wamr pxa_adapters_posix)
            target_compile_definitions(pxa_scheduler_engine_test PRIVATE
                PXA_SCHEDULER_TEST_GUEST_PATH="${scheduler_guest_WASM}")
            add_dependencies(pxa_scheduler_engine_test scheduler_guest)
            if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(pxa_scheduler_engine_test PRIVATE
                    -Wall -Wextra -Wpedantic)
                if(PXA_WARNINGS_AS_ERRORS)
                    target_compile_options(pxa_scheduler_engine_test PRIVATE -Werror)
                endif()
            endif()
            add_test(NAME pxa_scheduler_engine_test
                     COMMAND pxa_scheduler_engine_test)

            pxa_build_guest(net_audio_guest
                ${CMAKE_CURRENT_SOURCE_DIR}/adapters/tests/net_audio_guest.c)
            add_executable(pxa_net_audio_engine_test
                adapters/tests/net_audio_engine_test.c)
            target_link_libraries(pxa_net_audio_engine_test PRIVATE
                pxa_adapters_wamr pxa_adapters_posix)
            target_compile_definitions(pxa_net_audio_engine_test PRIVATE
                PXA_NET_AUDIO_TEST_GUEST_PATH="${net_audio_guest_WASM}")
            add_dependencies(pxa_net_audio_engine_test net_audio_guest)
            if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(pxa_net_audio_engine_test PRIVATE
                    -Wall -Wextra -Wpedantic)
                if(PXA_WARNINGS_AS_ERRORS)
                    target_compile_options(pxa_net_audio_engine_test PRIVATE -Werror)
                endif()
            endif()
            add_test(NAME pxa_net_audio_engine_test
                     COMMAND pxa_net_audio_engine_test)

        endif()
    endif()
endif()

if(PXA_BUILD_TESTS)
    enable_testing()
    add_executable(pxa_api_compile_test tests/api_compile_test.c)
    target_link_libraries(pxa_api_compile_test PRIVATE pxa)
    add_test(NAME pxa_api_compile_test COMMAND pxa_api_compile_test)

    add_executable(pxa_core_test tests/core_test.c)
    target_link_libraries(pxa_core_test PRIVATE pxa)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pxa_core_test PRIVATE
            -Wall -Wextra -Wpedantic)
        if(PXA_WARNINGS_AS_ERRORS)
            target_compile_options(pxa_core_test PRIVATE -Werror)
        endif()
    endif()
    add_test(NAME pxa_core_test COMMAND pxa_core_test)

    add_executable(pxa_surface_test tests/surface_test.c)
    target_link_libraries(pxa_surface_test PRIVATE pxa)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pxa_surface_test PRIVATE
            -Wall -Wextra -Wpedantic)
        if(PXA_WARNINGS_AS_ERRORS)
            target_compile_options(pxa_surface_test PRIVATE -Werror)
        endif()
    endif()
    add_test(NAME pxa_surface_test COMMAND pxa_surface_test)

    add_executable(pxa_game_render_test tests/game_render_test.c)
    target_link_libraries(pxa_game_render_test PRIVATE pxa)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pxa_game_render_test PRIVATE
            -Wall -Wextra -Wpedantic)
        if(PXA_WARNINGS_AS_ERRORS)
            target_compile_options(pxa_game_render_test PRIVATE -Werror)
        endif()
    endif()
    add_test(NAME pxa_game_render_test COMMAND pxa_game_render_test)

    add_executable(pxa_raster_test tests/raster_test.c)
    target_link_libraries(pxa_raster_test PRIVATE pxa)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pxa_raster_test PRIVATE
            -Wall -Wextra -Wpedantic)
        if(PXA_WARNINGS_AS_ERRORS)
            target_compile_options(pxa_raster_test PRIVATE -Werror)
        endif()
    endif()
    add_test(NAME pxa_raster_test COMMAND pxa_raster_test)

    add_executable(pxa_ui_test tests/ui_test.c)
    target_include_directories(pxa_ui_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(pxa_ui_test PRIVATE pxa)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pxa_ui_test PRIVATE
            -Wall -Wextra -Wpedantic)
        if(PXA_WARNINGS_AS_ERRORS)
            target_compile_options(pxa_ui_test PRIVATE -Werror)
        endif()
    endif()
    add_test(NAME pxa_ui_test COMMAND pxa_ui_test)

    add_executable(pxa_package_test tests/package_test.c)
    target_link_libraries(pxa_package_test PRIVATE pxa)
    target_compile_definitions(pxa_package_test PRIVATE
        PXA_TEST_GOLDEN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/package-vectors.json"
    )
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pxa_package_test PRIVATE
            -Wall -Wextra -Wpedantic)
        if(PXA_WARNINGS_AS_ERRORS)
            target_compile_options(pxa_package_test PRIVATE -Werror)
        endif()
    endif()
    add_test(NAME pxa_package_test COMMAND pxa_package_test)

    if(Python3_Interpreter_FOUND)
        add_test(
            NAME pxa_ui_vectors_test
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui_vectors_test.py
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/ui-vectors.json)
    endif()

    if(MSVC)
        target_compile_options(pxa_api_compile_test PRIVATE /UNDEBUG)
        target_compile_options(pxa_core_test PRIVATE /UNDEBUG)
        target_compile_options(pxa_package_test PRIVATE /UNDEBUG)
    else()
        target_compile_options(pxa_api_compile_test PRIVATE -UNDEBUG)
        target_compile_options(pxa_core_test PRIVATE -UNDEBUG)
        target_compile_options(pxa_package_test PRIVATE -UNDEBUG)
    endif()
endif()

install(TARGETS pxa EXPORT pxaTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES README.md ABI.md ARCHITECTURE.md AUDIT.md LICENSE
    DESTINATION "${CMAKE_INSTALL_DOCDIR}")

if(PXA_BUILD_ADAPTERS)
    set(_pxa_adapter_targets pxa_adapters_openssl pxa_adapters_posix)
    install(TARGETS ${_pxa_adapter_targets}
        EXPORT pxaAdaptersTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY adapters/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pxa-adapters)
    install(EXPORT pxaAdaptersTargets
        FILE pxaAdaptersTargets.cmake
        NAMESPACE pxa::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pxa")
endif()

configure_package_config_file(
    cmake/pxaConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/pxaConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pxa")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/pxaConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/pxaConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/pxaConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pxa")
install(EXPORT pxaTargets
    FILE pxaTargets.cmake
    NAMESPACE pxa::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pxa")
