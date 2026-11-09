include_guard(GLOBAL)
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../VERSION" PXA_RELEASE_VERSION LIMIT_COUNT 1)
if(NOT PXA_RELEASE_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)([-+].*)?$")
    message(FATAL_ERROR "Invalid PXA VERSION: ${PXA_RELEASE_VERSION}")
endif()
set(PXA_RELEASE_NUMERIC "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
# Native Host linkage is independent of the Guest Core and SDK release numbers.
# UI environment/configuration and Window snapshots grew in 0.10. Native
# consumers must relink; the negotiated Guest Core/AOT ABI is unchanged.
set(PXA_HOST_C_ABI 2)
