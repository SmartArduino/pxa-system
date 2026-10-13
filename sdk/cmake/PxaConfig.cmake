# Guest SDK package configuration. The toolchain supplies Pxa_DIR explicitly:
# it is a host-side build package, outside the WASI target sysroot.
get_filename_component(_pxa_sdk_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(PXA_CPP_SDK_DIR "${_pxa_sdk_root}/guest-cpp")
if(NOT DEFINED PXA_ARTIFACT_DIR)
    set(PXA_ARTIFACT_DIR "${CMAKE_BINARY_DIR}/artifacts")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/PxaGuest.cmake")
