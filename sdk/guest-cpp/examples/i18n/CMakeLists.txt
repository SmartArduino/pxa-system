cmake_minimum_required(VERSION 3.20)
project(pxa_cpp_i18n CXX)
list(APPEND CMAKE_MODULE_PATH "${PXA_CMAKE_MODULE_DIR}")
include(PxaGuest)
# i18n/messages.yaml is compiled automatically for this C++ target.
pxa_add_app(pxa_main COMPONENT_ID main SOURCES main.cpp)
