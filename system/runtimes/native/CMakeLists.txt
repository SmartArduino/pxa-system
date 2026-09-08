if(NOT COMMAND idf_component_register)
    cmake_minimum_required(VERSION 3.16)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PxaSystemSources.cmake")

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${PXSYS_NATIVE_SOURCES}
        INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include"
        REQUIRES pxa_system_core)
else()
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        project(pxa_system_native VERSION 0.1.0 LANGUAGES C)
    endif()

    option(PXSYS_NATIVE_BUILD_TESTS "Build native runtime host tests"
           ${PXSYS_BUILD_TESTS})
    option(PXSYS_NATIVE_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)

    if(NOT TARGET pxa_system_core)
        add_subdirectory("${PXSYS_ROOT}/system/core" "pxa_system_core")
    endif()

    add_library(pxa_system_native ${PXSYS_NATIVE_SOURCES})
    add_library(pxsys::native ALIAS pxa_system_native)
    set_target_properties(pxa_system_native PROPERTIES EXPORT_NAME native)
    target_compile_features(pxa_system_native PUBLIC c_std_99)
    target_include_directories(pxa_system_native PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>)
    target_link_libraries(pxa_system_native PUBLIC pxsys::core)

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pxa_system_native PRIVATE -Wall -Wextra -Wpedantic)
        if(PXSYS_NATIVE_WARNINGS_AS_ERRORS)
            target_compile_options(pxa_system_native PRIVATE -Werror)
        endif()
    endif()

    if(PXSYS_BUILD_TESTS AND PXSYS_NATIVE_BUILD_TESTS)
        enable_testing()
        add_executable(pxsys_native_runtime_test tests/native_runtime_test.c)
        target_link_libraries(pxsys_native_runtime_test PRIVATE pxsys::native)
        add_executable(pxsys_native_client_test tests/native_client_test.c)
        target_link_libraries(pxsys_native_client_test PRIVATE pxsys::native)
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(pxsys_native_runtime_test PRIVATE
                -Wall -Wextra -Wpedantic)
            target_compile_options(pxsys_native_client_test PRIVATE
                -Wall -Wextra -Wpedantic)
            if(PXSYS_NATIVE_WARNINGS_AS_ERRORS)
                target_compile_options(pxsys_native_runtime_test PRIVATE -Werror)
                target_compile_options(pxsys_native_client_test PRIVATE -Werror)
            endif()
        endif()
        add_test(NAME pxsys_native_runtime_test COMMAND pxsys_native_runtime_test)
        add_test(NAME pxsys_native_client_test COMMAND pxsys_native_client_test)
    endif()
endif()
