if(NOT COMMAND idf_component_register)
    cmake_minimum_required(VERSION 3.16)
endif()

add_library(pxa_system_reference_layout src/reference_layout.c)
add_library(pxsys::reference_layout ALIAS pxa_system_reference_layout)
set_target_properties(pxa_system_reference_layout PROPERTIES EXPORT_NAME reference_layout)
target_compile_features(pxa_system_reference_layout PUBLIC c_std_99)
target_include_directories(pxa_system_reference_layout PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>)
target_link_libraries(pxa_system_reference_layout PUBLIC pxsys::core)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pxa_system_reference_layout PRIVATE -Wall -Wextra -Wpedantic)
    if(PXSYS_WARNINGS_AS_ERRORS)
        target_compile_options(pxa_system_reference_layout PRIVATE -Werror)
    endif()
endif()

if(PXSYS_BUILD_TESTS)
    add_executable(pxsys_reference_layout_test tests/reference_layout_test.c)
    target_link_libraries(pxsys_reference_layout_test PRIVATE pxsys::reference_layout)
    add_test(NAME pxsys_reference_layout_test COMMAND pxsys_reference_layout_test)
endif()
