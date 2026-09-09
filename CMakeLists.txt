cmake_minimum_required(VERSION 3.16)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/PxaSystemSources.cmake")

if(COMMAND idf_component_register)
    include("${PXSYS_ROOT}/libpxa/cmake/pxaSources.cmake")
    pxa_collect_sources(PXSYS_LIBPXA_SOURCES "${PXSYS_ROOT}/libpxa")
    idf_component_register(
        SRCS
            ${PXSYS_LIBPXA_SOURCES}
            ${PXSYS_CORE_SOURCES}
            ${PXSYS_NATIVE_SOURCES}
            ${PXSYS_PXA_RUNTIME_SOURCES}
            ${PXSYS_STANDARD_SOURCES}
            ${PXSYS_REFERENCE_LAYOUT_SOURCES}
            ${PXSYS_HEADLESS_RENDERER_SOURCES}
        INCLUDE_DIRS
            ${PXSYS_PUBLIC_INCLUDE_DIRS}
            "${PXSYS_ROOT}/libpxa/include"
        PRIV_INCLUDE_DIRS "${PXSYS_ROOT}/libpxa/src")
    target_compile_features(${COMPONENT_LIB} PUBLIC c_std_99)
    return()
endif()

project(pxa_system VERSION 0.1.0 LANGUAGES C)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(PXSYS_TOP_LEVEL ON)
else()
    set(PXSYS_TOP_LEVEL OFF)
endif()

option(PXSYS_BUILD_TESTS "Build PXA System host tests" ${PXSYS_TOP_LEVEL})
option(PXSYS_WARNINGS_AS_ERRORS "Treat PXA System warnings as errors"
       ${PXSYS_TOP_LEVEL})
option(PXSYS_BUILD_LIBPXA "Build the bundled libpxa" ON)
option(PXSYS_BUILD_PXA_RUNTIME "Build the PXA application runtime" ON)
option(PXSYS_BUILD_STANDARD "Build the standard system composition" ON)
option(PXSYS_BUILD_REFERENCE_UI "Build the backend-neutral reference UI layout" ON)
option(PXSYS_BUILD_REFERENCE_LVGL "Build the LVGL standard UI adapter" OFF)
option(PXSYS_REFERENCE_UI_ANIMATIONS "Compile standard system UI animations" ON)
option(PXSYS_REFERENCE_UI_WIFI "Compile standard Wi-Fi status and settings UI" ON)
option(PXSYS_REFERENCE_UI_CELLULAR "Compile standard cellular status and settings UI" ON)
option(PXSYS_REFERENCE_UI_BATTERY_PERCENT
       "Show numeric battery percentage in the standard system UI" ON)
option(PXSYS_REFERENCE_UI_GESTURE_HANDLE
       "Show the gesture navigation handle" OFF)
set(PXSYS_REFERENCE_UI_TASK_SWITCHER "cards" CACHE STRING
    "Standard task switcher layout: cards or list")
set_property(CACHE PXSYS_REFERENCE_UI_TASK_SWITCHER PROPERTY STRINGS cards list)
set(PXSYS_REFERENCE_UI_TASK_PREVIEW_COUNT 4 CACHE STRING
    "Maximum cached task preview images in card mode")
option(PXSYS_BUILD_RENDERER_HEADLESS "Build the headless renderer" ON)
option(PXSYS_BUILD_RENDERER_LVGL "Build the LVGL renderer" OFF)
option(PXSYS_BUILD_SIMULATOR "Build the portable system simulator"
       ${PXSYS_TOP_LEVEL})

if(PXSYS_BUILD_TESTS)
    include(CTest)
endif()

if(PXSYS_BUILD_LIBPXA)
    set(PXA_BUILD_TESTS ${PXSYS_BUILD_TESTS} CACHE BOOL "" FORCE)
    set(PXA_WARNINGS_AS_ERRORS ${PXSYS_WARNINGS_AS_ERRORS} CACHE BOOL "" FORCE)
    set(PXA_BUILD_ADAPTERS OFF CACHE BOOL "" FORCE)
    add_subdirectory(libpxa)
endif()

add_subdirectory(system/core)
add_subdirectory(system/runtimes/native)

if(PXSYS_BUILD_PXA_RUNTIME)
    if(NOT TARGET pxa::pxa)
        message(FATAL_ERROR
            "PXSYS_BUILD_PXA_RUNTIME requires PXSYS_BUILD_LIBPXA or pxa::pxa")
    endif()
    add_subdirectory(system/runtimes/pxa)
endif()

if(PXSYS_BUILD_STANDARD)
    add_subdirectory(system/standard)
endif()

if(PXSYS_BUILD_REFERENCE_UI)
    add_subdirectory(ui/reference)
endif()

if(PXSYS_BUILD_RENDERER_HEADLESS)
    add_subdirectory(ui/renderers/headless)
endif()

if(PXSYS_BUILD_RENDERER_LVGL)
    add_subdirectory(ui/renderers/lvgl)
endif()

if(PXSYS_BUILD_REFERENCE_LVGL)
    if(NOT PXSYS_BUILD_REFERENCE_UI OR NOT PXSYS_BUILD_STANDARD)
        message(FATAL_ERROR
            "PXSYS_BUILD_REFERENCE_LVGL requires the reference layout and standard system")
    endif()
    add_subdirectory(ui/reference/lvgl)
endif()

if(PXSYS_BUILD_SIMULATOR)
    add_subdirectory(simulator)
endif()

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(PXSYS_INSTALL_TARGETS pxa_system_core pxa_system_native)
if(PXSYS_BUILD_PXA_RUNTIME)
    list(APPEND PXSYS_INSTALL_TARGETS pxa_system_pxa)
endif()
if(PXSYS_BUILD_STANDARD)
    list(APPEND PXSYS_INSTALL_TARGETS pxa_system_standard)
endif()
if(PXSYS_BUILD_REFERENCE_UI)
    list(APPEND PXSYS_INSTALL_TARGETS pxa_system_reference_layout)
endif()
if(PXSYS_BUILD_RENDERER_HEADLESS)
    list(APPEND PXSYS_INSTALL_TARGETS pxa_system_renderer_headless)
endif()
if(PXSYS_BUILD_RENDERER_LVGL)
    list(APPEND PXSYS_INSTALL_TARGETS pxa_system_renderer_lvgl)
endif()
if(PXSYS_BUILD_REFERENCE_LVGL)
    list(APPEND PXSYS_INSTALL_TARGETS pxa_system_reference_lvgl)
endif()

install(TARGETS ${PXSYS_INSTALL_TARGETS} EXPORT pxsysTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY
    system/core/include/
    system/runtimes/native/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(PXSYS_BUILD_PXA_RUNTIME)
    install(DIRECTORY system/runtimes/pxa/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
if(PXSYS_BUILD_STANDARD)
    install(DIRECTORY system/standard/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
if(PXSYS_BUILD_REFERENCE_UI)
    install(DIRECTORY ui/reference/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
if(PXSYS_BUILD_RENDERER_HEADLESS)
    install(DIRECTORY ui/renderers/headless/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
if(PXSYS_BUILD_RENDERER_LVGL)
    install(DIRECTORY ui/renderers/lvgl/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()
if(PXSYS_BUILD_REFERENCE_LVGL)
    install(DIRECTORY ui/reference/lvgl/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

set(PXSYS_CONFIG_NEEDS_LIBPXA ${PXSYS_BUILD_PXA_RUNTIME})
configure_package_config_file(
    cmake/pxsysConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/pxsysConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pxsys")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/pxsysConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/pxsysConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/pxsysConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pxsys")
install(EXPORT pxsysTargets
    FILE pxsysTargets.cmake
    NAMESPACE pxsys::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pxsys")
