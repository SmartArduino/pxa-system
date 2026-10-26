cmake_minimum_required(VERSION 3.20)
project(pxa_browser C)
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "Configure with emcmake cmake")
endif()
get_filename_component(emscripten_root "${CMAKE_C_COMPILER}" DIRECTORY)
configure_file("${emscripten_root}/LICENSE" "${CMAKE_BINARY_DIR}/LICENSE-Emscripten" COPYONLY)
configure_file("${emscripten_root}/system/lib/libc/musl/COPYRIGHT" "${CMAKE_BINARY_DIR}/LICENSE-musl" COPYONLY)
configure_file("${emscripten_root}/system/lib/compiler-rt/LICENSE.TXT" "${CMAKE_BINARY_DIR}/LICENSE-compiler-rt" COPYONLY)
set(PXA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PXA_BUILD_ADAPTERS OFF CACHE BOOL "" FORCE)
add_subdirectory(../../libpxa pxa)
include(../../cmake/PxaDesktopLvgl.cmake)
pxsys_prepare_desktop_lvgl("${CMAKE_CURRENT_SOURCE_DIR}")
pxa_restrict_lvgl(lvgl)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(browser_license_gate
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/licensing/check_system_licenses.py"
    --compile-commands "${CMAKE_BINARY_DIR}/compile_commands.json"
  VERBATIM)
set(adapter ../../libpxa/adapters)
add_executable(pxa-browser host.c fonts.c services.c audio.c network.c graphics.c
  ${adapter}/posix/pxa_posix_fs.c
  ${adapter}/posix/pxa_posix_storage.c
  ${adapter}/lvgl/pxa_lvgl_ui.c
  ${adapter}/lvgl/pxa_lvgl_ui_symbols.c
  ${adapter}/lvgl/pxa_lvgl_gif.c
  ${adapter}/lvgl/pxa_lvgl_gif_native.c)
target_include_directories(pxa-browser PRIVATE ${adapter}/include)
target_link_libraries(pxa-browser PRIVATE pxa lvgl)
add_dependencies(pxa-browser browser_license_gate)
target_compile_options(pxa-browser PRIVATE -Wall -Wextra)
target_link_options(pxa-browser PRIVATE
  --no-entry -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,worker,node
  -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=134217728 -sINITIAL_MEMORY=33554432
  -sSTACK_SIZE=1048576 -sWASM_BIGINT=1 -sFORCE_FILESYSTEM=1 -lidbfs.js
  "-sEXPORTED_RUNTIME_METHODS=['FS','IDBFS']"
  "-sEXPORTED_FUNCTIONS=['_malloc','_free']")
set_target_properties(pxa-browser PROPERTIES SUFFIX .mjs)
