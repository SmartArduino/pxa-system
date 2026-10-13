# Read the release authority instead of maintaining another version literal.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../../VERSION" PACKAGE_VERSION LIMIT_COUNT 1)
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)" _pxa_version "${PACKAGE_VERSION}")
set(_pxa_major "${CMAKE_MATCH_1}")
set(_pxa_minor "${CMAKE_MATCH_2}")
set(PACKAGE_VERSION_COMPATIBLE FALSE)
if(NOT PACKAGE_FIND_VERSION VERSION_GREATER PACKAGE_VERSION
   AND PACKAGE_FIND_VERSION_MAJOR EQUAL _pxa_major)
    # Pre-1.0 SDK minor releases may change the C++ source API.
    if(NOT _pxa_major EQUAL 0 OR PACKAGE_FIND_VERSION_MINOR EQUAL _pxa_minor)
        set(PACKAGE_VERSION_COMPATIBLE TRUE)
    endif()
endif()
if(PACKAGE_FIND_VERSION VERSION_EQUAL PACKAGE_VERSION)
    set(PACKAGE_VERSION_EXACT TRUE)
endif()
