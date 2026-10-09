# CMake uses the numeric version; pxa.lock also pins prerelease identity.
get_filename_component(_pxa_version_prefix "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
file(STRINGS "${_pxa_version_prefix}/VERSION" _pxa_version LIMIT_COUNT 1)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" PACKAGE_VERSION "${_pxa_version}")
set(_pxa_version_major "${CMAKE_MATCH_1}")
set(_pxa_version_minor "${CMAKE_MATCH_2}")
set(PACKAGE_VERSION_COMPATIBLE FALSE)
set(PACKAGE_VERSION_EXACT FALSE)
if(NOT PACKAGE_FIND_VERSION VERSION_GREATER PACKAGE_VERSION AND
   PACKAGE_FIND_VERSION_MAJOR EQUAL _pxa_version_major AND
   (NOT _pxa_version_major EQUAL 0 OR PACKAGE_FIND_VERSION_MINOR EQUAL _pxa_version_minor))
    set(PACKAGE_VERSION_COMPATIBLE TRUE)
    if(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)
        set(PACKAGE_VERSION_EXACT TRUE)
    endif()
endif()
