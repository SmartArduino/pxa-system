cmake_minimum_required(VERSION 3.20)
project(pxa_cmake_components_lab C)

list(APPEND CMAKE_MODULE_PATH "${PXA_CMAKE_MODULE_DIR}")
include(PxaGuest)

add_subdirectory(main)
add_subdirectory(responder)
