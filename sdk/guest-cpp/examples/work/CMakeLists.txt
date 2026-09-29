cmake_minimum_required(VERSION 3.20)
project(pxa_cpp_work CXX)

list(APPEND CMAKE_MODULE_PATH "${PXA_CMAKE_MODULE_DIR}")
include(PxaGuest)

pxa_add_app(pxa_main COMPONENT_ID main SOURCES main.cpp)
pxa_add_app(pxa_job COMPONENT_ID sync.job SOURCES job.cpp)
