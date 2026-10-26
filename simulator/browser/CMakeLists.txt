cmake_minimum_required(VERSION 3.20)
project(pxa_browser C)
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "Configure with emcmake cmake")
endif()
set(PXA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PXA_BUILD_ADAPTERS OFF CACHE BOOL "" FORCE)
add_subdirectory(../../libpxa pxa)
include(../../cmake/PxaDesktopLvgl.cmake)
pxsys_prepare_desktop_lvgl("${CMAKE_CURRENT_SOURCE_DIR}")
pxa_restrict_lvgl(lvgl)
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
target_compile_options(pxa-browser PRIVATE -Wall -Wextra)
target_link_options(pxa-browser PRIVATE
  --no-entry -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,worker,node
  -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=134217728 -sINITIAL_MEMORY=33554432
  -sSTACK_SIZE=1048576 -sWASM_BIGINT=1 -sFORCE_FILESYSTEM=1 -lidbfs.js
  "-sEXPORTED_RUNTIME_METHODS=['FS','IDBFS']"
  "-sEXPORTED_FUNCTIONS=['_malloc','_free']")
set_target_properties(pxa-browser PROPERTIES SUFFIX .mjs)
