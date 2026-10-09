cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/PxaVersion.cmake")
    project(pxsys_desktop_simulator VERSION ${PXA_RELEASE_NUMERIC} LANGUAGES C)
    set(PXSYS_DESKTOP_STANDALONE ON)
    include(CTest)
else()
    set(PXSYS_DESKTOP_STANDALONE OFF)
endif()

find_package(SDL2 REQUIRED)
find_package(PNG REQUIRED)
find_package(Freetype REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(VORBISFILE REQUIRED IMPORTED_TARGET vorbisfile)
pkg_check_modules(OPUSFILE REQUIRED IMPORTED_TARGET opusfile)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/PxaWamrMetadata.cmake")

set(PXSYS_DESKTOP_TEXT_FONT
    "${CMAKE_CURRENT_LIST_DIR}/../../../../factory/base/system/fonts/noto_sans_cjk_common.ttf"
    CACHE FILEPATH "TTF used for all desktop simulator text")
if(PXSYS_DESKTOP_TEXT_FONT AND NOT EXISTS "${PXSYS_DESKTOP_TEXT_FONT}")
    message(FATAL_ERROR "Desktop simulator text font is missing: ${PXSYS_DESKTOP_TEXT_FONT}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/PxaDesktopLvgl.cmake")
pxsys_prepare_desktop_lvgl("${CMAKE_CURRENT_LIST_DIR}")

target_include_directories(lvgl PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(lvgl PUBLIC SDL2::SDL2 Freetype::Freetype)

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
    main.c builtin_catalog.c simulator_runtime.c product_runner.c desktop_net.c
    pxadb_control.c "${PXSYS_DESKTOP_CATALOG_DATA}")
target_compile_features(pxsys_desktop_simulator PRIVATE c_std_99)
pxsys_apply_wamr_metadata(pxsys_desktop_simulator)
target_include_directories(pxsys_desktop_simulator PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(pxsys_desktop_simulator PRIVATE
    PXSYS_PRODUCT_RUNNER_LIBRARY
    PXSYS_DESKTOP_APP_SOURCE_ROOT="${PXSYS_DESKTOP_APP_SOURCE_ROOT}"
    PXSYS_DESKTOP_TEXT_FONT="${PXSYS_DESKTOP_TEXT_FONT}")
target_link_libraries(pxsys_desktop_simulator PRIVATE
    pxsys::standard pxsys::pxa pxsys::renderer_lvgl pxsys::reference_lvgl
    pxa_adapters_openssl pxa_adapters_posix pxa_adapters_lvgl
    pxa_adapters_wamr lvgl SDL2::SDL2 PNG::PNG CURL::libcurl Threads::Threads
    PkgConfig::VORBISFILE PkgConfig::OPUSFILE m)

add_executable(pxsys_product_simulator product_runner.c desktop_net.c
    pxadb_control.c)
target_compile_features(pxsys_product_simulator PRIVATE c_std_99)
pxsys_apply_wamr_metadata(pxsys_product_simulator)
target_compile_definitions(pxsys_product_simulator PRIVATE
    PXSYS_DESKTOP_TEXT_FONT="${PXSYS_DESKTOP_TEXT_FONT}")
target_link_libraries(pxsys_product_simulator PRIVATE
    pxsys::core pxa::pxa pxa_adapters_openssl pxa_adapters_posix pxa_adapters_lvgl
    pxa_adapters_wamr lvgl SDL2::SDL2 PNG::PNG CURL::libcurl Threads::Threads
    PkgConfig::VORBISFILE PkgConfig::OPUSFILE m)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pxsys_desktop_simulator PRIVATE
        -Werror=implicit-function-declaration -Werror=int-conversion)
    target_compile_options(pxsys_product_simulator PRIVATE
        -Wall -Wextra -Wpedantic
        -Werror=implicit-function-declaration -Werror=int-conversion)
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
    find_program(PXSYS_GUEST_CLANG clang)
    find_program(PXSYS_TEST_TIMEOUT timeout)
    set(PXSYS_V1_SMOKE_APP_ROOT
        "${CMAKE_CURRENT_LIST_DIR}/../../../../local/pxa-apps")
    if(PXSYS_GUEST_CLANG AND PXSYS_TEST_TIMEOUT AND
       EXISTS "${PXSYS_V1_SMOKE_APP_ROOT}/abi-v1-smoke/package.json" AND
       EXISTS "${PXSYS_V1_SMOKE_APP_ROOT}/.dev-signing/publisher-public.der")
        add_test(NAME pxsys_product_abi_v1_pressure
            COMMAND bash
                "${CMAKE_CURRENT_LIST_DIR}/../../tools/package/test_abi_v1_smoke.sh"
                $<TARGET_FILE:pxsys_product_simulator>)
        set_tests_properties(pxsys_product_abi_v1_pressure PROPERTIES
            TIMEOUT 45)
        if(EXISTS "${PXSYS_V1_SMOKE_APP_ROOT}/abi-v1-ipc-smoke/package.json")
            add_test(NAME pxsys_product_abi_v1_ipc
                COMMAND bash
                    "${CMAKE_CURRENT_LIST_DIR}/../../tools/package/test_abi_v1_ipc_smoke.sh"
                    $<TARGET_FILE:pxsys_product_simulator>)
            set_tests_properties(pxsys_product_abi_v1_ipc PROPERTIES
                TIMEOUT 45)
        endif()
    endif()
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

# These targets exercise the actual product backend, including in normal
# simulator build trees. Keep dependency configuration identical to the runner.
foreach(_test audio audio_app raster resource_app voxel_resources resource_budget ui_images store_images weather_images)
    if(_test STREQUAL "audio")
        set(_source tests/audio_backend_test.c)
    elseif(_test STREQUAL "audio_app")
        set(_source tests/audio_app_test.c)
    elseif(_test STREQUAL "resource_budget")
        set(_source tests/resource_budget_test.c)
    elseif(_test STREQUAL "resource_app")
        set(_source tests/resource_app_test.c)
    elseif(_test STREQUAL "voxel_resources")
        set(_source tests/voxel_resource_test.c)
    elseif(_test STREQUAL "store_images")
        set(_source tests/store_image_capture_test.c)
    elseif(_test STREQUAL "weather_images")
        set(_source tests/weather_image_capture_test.c)
    elseif(_test STREQUAL "ui_images")
        set(_source tests/ui_image_capture_test.c)
    else()
        set(_source tests/raster_backend_test.c)
    endif()
    add_executable(pxsys_${_test}_test ${_source} desktop_net.c pxadb_control.c)
    if(NOT PXSYS_BUILD_TESTS AND NOT BUILD_TESTING)
        set_target_properties(pxsys_${_test}_test PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
    pxsys_apply_wamr_metadata(pxsys_${_test}_test)
    target_compile_definitions(pxsys_${_test}_test PRIVATE
        PXSYS_DESKTOP_TEXT_FONT="${PXSYS_DESKTOP_TEXT_FONT}")
    target_link_libraries(pxsys_${_test}_test PRIVATE
        pxsys::core pxa::pxa pxa_adapters_openssl pxa_adapters_posix pxa_adapters_lvgl
        pxa_adapters_wamr lvgl SDL2::SDL2 PNG::PNG CURL::libcurl Threads::Threads
        PkgConfig::VORBISFILE PkgConfig::OPUSFILE m)
    if(PXSYS_BUILD_TESTS OR BUILD_TESTING)
        if(_test STREQUAL "audio")
            add_test(NAME pxsys_audio_test COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_LIST_DIR}/../../tools/package/test_audio_backend.py"
                $<TARGET_FILE:pxsys_audio_test>
                "${CMAKE_CURRENT_LIST_DIR}/tests/audio-assets")
        elseif(_test STREQUAL "resource_budget")
            add_test(NAME pxsys_resource_budget_test COMMAND pxsys_resource_budget_test)
        elseif(_test STREQUAL "raster")
            add_test(NAME pxsys_raster_test COMMAND pxsys_raster_test)
        endif()
    endif()
endforeach()

if(PXSYS_BUILD_TESTS OR BUILD_TESTING)
    include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/PxaTestAssertions.cmake")
    pxa_enable_test_assertions("${CMAKE_CURRENT_SOURCE_DIR}")
endif()

set(PXSYS_RASTER_BASELINE_SOURCE "" CACHE FILEPATH
    "Saved product_runner.c used for the pre-refactor raster benchmark")
set(PXSYS_RASTER_BASELINE_KERNEL "" CACHE FILEPATH
    "Saved raster.c including the pre-refactor validator, for baseline timing")
foreach(_bench current baseline)
    if(_bench STREQUAL "baseline" AND NOT PXSYS_RASTER_BASELINE_SOURCE)
        continue()
    endif()
    add_executable(pxsys_raster_bench_${_bench} EXCLUDE_FROM_ALL
        tests/raster_benchmark.c desktop_net.c pxadb_control.c)
    pxsys_apply_wamr_metadata(pxsys_raster_bench_${_bench})
    target_include_directories(pxsys_raster_bench_${_bench} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}")
    target_compile_definitions(pxsys_raster_bench_${_bench} PRIVATE
        PXSYS_DESKTOP_TEXT_FONT="${PXSYS_DESKTOP_TEXT_FONT}")
    if(_bench STREQUAL "baseline")
        target_compile_definitions(pxsys_raster_bench_${_bench} PRIVATE
            PXA_BENCH_RUNNER="${PXSYS_RASTER_BASELINE_SOURCE}")
        if(PXSYS_RASTER_BASELINE_KERNEL)
            # Override the archive's raster object so dependency collection
            # and other validator changes are measured only in the new path.
            target_sources(pxsys_raster_bench_${_bench} PRIVATE
                "${PXSYS_RASTER_BASELINE_KERNEL}")
        endif()
    endif()
    target_link_libraries(pxsys_raster_bench_${_bench} PRIVATE
        pxsys::core pxa::pxa pxa_adapters_openssl pxa_adapters_posix pxa_adapters_lvgl
        pxa_adapters_wamr lvgl SDL2::SDL2 PNG::PNG CURL::libcurl Threads::Threads
        PkgConfig::VORBISFILE PkgConfig::OPUSFILE m)
endforeach()
