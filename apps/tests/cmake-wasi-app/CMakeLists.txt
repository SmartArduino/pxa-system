cmake_minimum_required(VERSION 3.20)
project(pxa_cmake_wasi_example C)

list(APPEND CMAKE_MODULE_PATH "${PXA_CMAKE_MODULE_DIR}")
include(PxaGuest)

add_subdirectory(logic)
pxa_add_component(pxa_main
    COMPONENT_ID main
    SOURCES main.c
    MODULES example_logic)
