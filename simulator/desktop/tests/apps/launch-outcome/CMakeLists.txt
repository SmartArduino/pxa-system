cmake_minimum_required(VERSION 3.20)
project(pxa_launch_outcome C)
list(APPEND CMAKE_MODULE_PATH "${PXA_CMAKE_MODULE_DIR}")
include(PxaGuest)
pxa_add_component(pxa_main NO_TRANSLATIONS NO_ALLOCATOR COMPONENT_ID main
    SOURCES main.c)
