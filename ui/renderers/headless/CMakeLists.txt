if(NOT COMMAND idf_component_register)
    cmake_minimum_required(VERSION 3.16)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PxaSystemSources.cmake")

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${PXSYS_HEADLESS_RENDERER_SOURCES}
        INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include"
        REQUIRES pxa_system_core)
else()
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        project(pxa_system_renderer_headless VERSION 0.1.0 LANGUAGES C)
    endif()
    if(NOT TARGET pxa_system_core)
        add_subdirectory("${PXSYS_ROOT}/system/core" "pxa_system_core")
    endif()
    add_library(pxa_system_renderer_headless ${PXSYS_HEADLESS_RENDERER_SOURCES})
    add_library(pxsys::renderer_headless ALIAS pxa_system_renderer_headless)
    set_target_properties(pxa_system_renderer_headless PROPERTIES
        EXPORT_NAME renderer_headless)
    target_compile_features(pxa_system_renderer_headless PUBLIC c_std_99)
    target_include_directories(pxa_system_renderer_headless PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>)
    target_link_libraries(pxa_system_renderer_headless PUBLIC pxsys::core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pxa_system_renderer_headless PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    if(PXSYS_BUILD_TESTS)
    add_executable(pxsys_headless_renderer_test tests/headless_renderer_test.c)
    target_link_libraries(pxsys_headless_renderer_test PRIVATE pxsys::renderer_headless)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pxsys_headless_renderer_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME pxsys_headless_renderer_test COMMAND pxsys_headless_renderer_test)
    endif()
endif()
