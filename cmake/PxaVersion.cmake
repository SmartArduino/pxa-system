include_guard(GLOBAL)

get_filename_component(_pxa_version_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_pxa_version_root}/VERSION" "${_pxa_version_root}/config/host-abi.json")
file(STRINGS "${_pxa_version_root}/VERSION" PXA_RELEASE_VERSION LIMIT_COUNT 1)
if(NOT PXA_RELEASE_VERSION MATCHES
   "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?(\\+[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?$")
    message(FATAL_ERROR "Invalid PXA release VERSION: ${PXA_RELEASE_VERSION}")
endif()
set(PXA_RELEASE_MAJOR "${CMAKE_MATCH_1}")
set(PXA_RELEASE_MINOR "${CMAKE_MATCH_2}")
set(PXA_RELEASE_PATCH "${CMAKE_MATCH_3}")
set(PXA_RELEASE_NUMERIC
    "${PXA_RELEASE_MAJOR}.${PXA_RELEASE_MINOR}.${PXA_RELEASE_PATCH}")
if(PXA_RELEASE_VERSION MATCHES "-([^+]+)")
    string(REPLACE "." ";" _pxa_prerelease_identifiers "${CMAKE_MATCH_1}")
    foreach(_pxa_identifier IN LISTS _pxa_prerelease_identifiers)
        if(_pxa_identifier MATCHES "^0[0-9]+$")
            message(FATAL_ERROR "Numeric SemVer prerelease identifiers cannot have leading zeros")
        endif()
    endforeach()
endif()
# Host native linkage and Guest wire compatibility have independent versions.
file(READ "${_pxa_version_root}/config/host-abi.json" _pxa_host_abi_json)
string(REGEX MATCH "\"soversion\"[ \t\r\n]*:[ \t\r\n]*([0-9]+)"
    _pxa_host_abi_match "${_pxa_host_abi_json}")
if(NOT _pxa_host_abi_match)
    message(FATAL_ERROR "config/host-abi.json must define soversion")
endif()
set(PXA_HOST_C_ABI "${CMAKE_MATCH_1}")
