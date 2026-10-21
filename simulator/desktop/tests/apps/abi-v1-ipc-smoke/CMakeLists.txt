cmake_minimum_required(VERSION 3.20)
project(pxa_abi_v1_ipc_smoke CXX)
list(APPEND CMAKE_MODULE_PATH "${PXA_CMAKE_MODULE_DIR}")
include(PxaGuest)
pxa_add_component(pxa_echo CPP NO_TRANSLATIONS NO_ALLOCATOR COMPONENT_ID echo
    SOURCES provider.cpp)
pxa_add_component(pxa_main CPP NO_TRANSLATIONS NO_ALLOCATOR COMPONENT_ID main
    SOURCES main.cpp)
target_compile_definitions(pxa_echo PRIVATE PXA_GUEST_CPP_TRANSPORT=1)
target_compile_definitions(pxa_main PRIVATE PXA_GUEST_CPP_TRANSPORT=1)
