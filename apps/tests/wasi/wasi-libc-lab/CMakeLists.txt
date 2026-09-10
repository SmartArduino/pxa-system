cmake_minimum_required(VERSION 3.20)
project(pxa_wasi_libc_lab C)

list(APPEND CMAKE_MODULE_PATH "${PXA_CMAKE_MODULE_DIR}")
include(PxaGuest)

add_subdirectory(checks)
add_subdirectory(presentation)

pxa_add_component(pxa_main
    COMPONENT_ID main
    SOURCES main.c
    MODULES libc_checks libc_presentation)
