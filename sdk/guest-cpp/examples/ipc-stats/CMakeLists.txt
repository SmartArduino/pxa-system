cmake_minimum_required(VERSION 3.20)
project(pxa_cpp_ipc_stats CXX)

list(APPEND CMAKE_MODULE_PATH "${PXA_CMAKE_MODULE_DIR}")
include(PxaGuest)

pxa_add_ipc_contract(stats_contract CONTRACT stats.contract.json
                     OUTPUT stats_generated.hpp)
pxa_add_app(pxa_main COMPONENT_ID main SOURCES main.cpp
            LIBRARIES stats_contract)
pxa_add_app(pxa_provider COMPONENT_ID stats.provider SOURCES provider.cpp
            LIBRARIES stats_contract)
