cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(pxsys_desktop_simulator VERSION 0.1.0 LANGUAGES C)
    set(PXSYS_DESKTOP_STANDALONE ON)
    include(CTest)
else()
    set(PXSYS_DESKTOP_STANDALONE OFF)
endif()

find_package(SDL2 REQUIRED)
find_package(PNG REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

if(NOT TARGET lvgl)
    set(PXSYS_LVGL_SOURCE_DIR "" CACHE PATH
        "Existing LVGL 9.5 source tree; empty downloads the pinned release")
    set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
    set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h"
        CACHE PATH "" FORCE)
    if(PXSYS_LVGL_SOURCE_DIR)
        add_subdirectory("${PXSYS_LVGL_SOURCE_DIR}"
                         "${CMAKE_CURRENT_BINARY_DIR}/lvgl" EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(lvgl_source
            GIT_REPOSITORY https://github.com/lvgl/lvgl.git
            GIT_TAG v9.5.0
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(lvgl_source)
        # LVGL is a private simulator dependency. Keep its install rules out
        # of a top-level PXA System SDK install.
        set_property(DIRECTORY "${lvgl_source_SOURCE_DIR}"
                     PROPERTY EXCLUDE_FROM_ALL TRUE)
    endif()
endif()

target_include_directories(lvgl PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(lvgl PUBLIC SDL2::SDL2)

if(PXSYS_DESKTOP_STANDALONE)
    set(PXSYS_BUILD_SIMULATOR OFF CACHE BOOL "" FORCE)
    set(PXSYS_BUILD_REFERENCE_LVGL ON CACHE BOOL "" FORCE)
    set(PXSYS_BUILD_RENDERER_LVGL ON CACHE BOOL "" FORCE)
    set(PXSYS_BUILD_WAMR_ADAPTER ON CACHE BOOL "" FORCE)
    set(PXA_LVGL_TARGET lvgl CACHE STRING "" FORCE)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../.."
                     "${CMAKE_CURRENT_BINARY_DIR}/pxa-system")
else()
    if(NOT TARGET pxsys::renderer_lvgl)
        add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../ui/renderers/lvgl"
                         "${CMAKE_CURRENT_BINARY_DIR}/renderer-lvgl")
    endif()
    if(NOT TARGET pxsys::reference_lvgl)
        add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../ui/reference/lvgl"
                         "${CMAKE_CURRENT_BINARY_DIR}/reference-lvgl")
    endif()
endif()

set(PXSYS_DESKTOP_APP_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../apps/pxa"
    CACHE PATH "PXA application source root used for the built-in simulator catalog")
file(GLOB PXSYS_DESKTOP_APP_MANIFESTS CONFIGURE_DEPENDS
    "${PXSYS_DESKTOP_APP_SOURCE_ROOT}/*/package.json")
set(PXSYS_DESKTOP_CATALOG_DATA
    "${CMAKE_CURRENT_BINARY_DIR}/generated/builtin_catalog_data.h")
add_custom_command(
    OUTPUT "${PXSYS_DESKTOP_CATALOG_DATA}"
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_LIST_DIR}/generate_builtin_catalog.py"
            --apps "${PXSYS_DESKTOP_APP_SOURCE_ROOT}"
            --output "${PXSYS_DESKTOP_CATALOG_DATA}"
    DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/generate_builtin_catalog.py"
        ${PXSYS_DESKTOP_APP_MANIFESTS}
    VERBATIM)

add_executable(pxsys_desktop_simulator
    main.c builtin_catalog.c simulator_runtime.c pxadb_control.c
    "${PXSYS_DESKTOP_CATALOG_DATA}")
target_compile_features(pxsys_desktop_simulator PRIVATE c_std_99)
target_include_directories(pxsys_desktop_simulator PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(pxsys_desktop_simulator PRIVATE
    PXSYS_DESKTOP_APP_SOURCE_ROOT="${PXSYS_DESKTOP_APP_SOURCE_ROOT}")
target_link_libraries(pxsys_desktop_simulator PRIVATE
    pxsys::standard pxsys::pxa pxsys::renderer_lvgl pxsys::reference_lvgl
    lvgl SDL2::SDL2 PNG::PNG m)

add_executable(pxsys_product_simulator product_runner.c pxadb_control.c)
target_compile_features(pxsys_product_simulator PRIVATE c_std_99)
target_link_libraries(pxsys_product_simulator PRIVATE
    pxa::pxa pxa_adapters_openssl pxa_adapters_posix pxa_adapters_lvgl
    pxa_adapters_wamr lvgl SDL2::SDL2 PNG::PNG m)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pxsys_product_simulator PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

add_executable(pxsys_package_installer package_installer.c)
target_compile_features(pxsys_package_installer PRIVATE c_std_99)
target_link_libraries(pxsys_package_installer PRIVATE
    pxa::pxa pxa_adapters_openssl pxa_adapters_posix)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pxsys_package_installer PRIVATE
        -Wall -Wextra -Wpedantic)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pxsys_desktop_simulator PRIVATE
        -Wall -Wextra -Wpedantic)
    if(PXSYS_WARNINGS_AS_ERRORS)
        target_compile_options(pxsys_desktop_simulator PRIVATE -Werror)
    endif()
endif()

if(PXSYS_BUILD_TESTS OR BUILD_TESTING)
    add_test(NAME pxsys_desktop_simulator_smoke
             COMMAND pxsys_desktop_simulator --smoke-test)
    set_tests_properties(pxsys_desktop_simulator_smoke PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    add_test(NAME pxsys_desktop_simulator_self_test
             COMMAND pxsys_desktop_simulator --self-test)
    set_tests_properties(pxsys_desktop_simulator_self_test PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

    function(pxsys_add_desktop_visual_test name width height)
        add_test(NAME "pxsys_desktop_visual_${name}"
            COMMAND ${Python3_EXECUTABLE}
                    "${CMAKE_CURRENT_LIST_DIR}/test_visual.py"
                    --simulator $<TARGET_FILE:pxsys_desktop_simulator>
                    --width ${width} --height ${height} -- ${ARGN})
    endfunction()
    pxsys_add_desktop_visual_test(compact_light_en 296 240
        --profile compact --light --locale en-US)
    pxsys_add_desktop_visual_test(compact_dark_zh 296 240
        --profile compact --dark --locale zh-CN
        --time 07:05 --battery 18 --network none --permission deny
        --storage-bytes 4096)
    pxsys_add_desktop_visual_test(phone_light_en 390 844
        --profile phone --light --locale en-US)
    pxsys_add_desktop_visual_test(phone_dark_zh 390 844
        --profile phone --dark --locale zh-CN)
    pxsys_add_desktop_visual_test(round_light_en 454 454
        --profile round --light --locale en-US)
    pxsys_add_desktop_visual_test(round_custom_zh 454 454
        --profile round --custom-theme --locale zh-CN)
endif()
