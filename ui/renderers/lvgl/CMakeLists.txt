if(NOT COMMAND idf_component_register)
    cmake_minimum_required(VERSION 3.16)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PxaSystemSources.cmake")

if(COMMAND idf_component_register)
    idf_component_register(
        SRCS ${PXSYS_LVGL_RENDERER_SOURCES}
        INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include"
        REQUIRES pxa_system_core lvgl)
else()
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        project(pxa_system_renderer_lvgl VERSION 0.1.0 LANGUAGES C)
    endif()
    if(NOT TARGET pxa_system_core)
        add_subdirectory("${PXSYS_ROOT}/system/core" "pxa_system_core")
    endif()
    if(NOT TARGET lvgl)
        add_library(lvgl "tests/lvgl_stub.c")
        target_include_directories(lvgl PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tests/stubs>)
    endif()
    add_library(pxa_system_renderer_lvgl ${PXSYS_LVGL_RENDERER_SOURCES})
    add_library(pxsys::renderer_lvgl ALIAS pxa_system_renderer_lvgl)
    set_target_properties(pxa_system_renderer_lvgl PROPERTIES
        EXPORT_NAME renderer_lvgl)
    target_compile_features(pxa_system_renderer_lvgl PUBLIC c_std_99)
    target_include_directories(pxa_system_renderer_lvgl PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>)
    target_link_libraries(pxa_system_renderer_lvgl PUBLIC pxsys::core lvgl)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pxa_system_renderer_lvgl PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    if(PXSYS_BUILD_TESTS)
    add_executable(pxsys_lvgl_renderer_test "tests/lvgl_renderer_test.c")
    target_link_libraries(pxsys_lvgl_renderer_test PRIVATE
        pxsys::renderer_lvgl pxsys::core)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pxsys_lvgl_renderer_test PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME pxsys_lvgl_renderer_test COMMAND pxsys_lvgl_renderer_test)
    endif()
endif()
