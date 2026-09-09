if(NOT TARGET lvgl)
    message(FATAL_ERROR "PXA reference LVGL UI requires an lvgl target")
endif()
if(NOT TARGET pxsys::reference_layout)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/.." pxa_reference_layout)
endif()

add_library(pxa_system_reference_lvgl src/reference_lvgl.c)
add_library(pxsys::reference_lvgl ALIAS pxa_system_reference_lvgl)
set_target_properties(pxa_system_reference_lvgl PROPERTIES EXPORT_NAME reference_lvgl)
target_compile_features(pxa_system_reference_lvgl PUBLIC c_std_99)
target_include_directories(pxa_system_reference_lvgl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>)
target_link_libraries(pxa_system_reference_lvgl PUBLIC
    pxsys::standard pxsys::reference_layout lvgl)
if(DEFINED PXSYS_REFERENCE_UI_ANIMATIONS AND NOT PXSYS_REFERENCE_UI_ANIMATIONS)
    target_compile_definitions(pxa_system_reference_lvgl PRIVATE
        PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS=0)
endif()
if(DEFINED PXSYS_REFERENCE_UI_WIFI AND NOT PXSYS_REFERENCE_UI_WIFI)
    target_compile_definitions(pxa_system_reference_lvgl PRIVATE
        PXSYS_REFERENCE_UI_WIFI=0)
endif()
if(DEFINED PXSYS_REFERENCE_UI_CELLULAR AND NOT PXSYS_REFERENCE_UI_CELLULAR)
    target_compile_definitions(pxa_system_reference_lvgl PRIVATE
        PXSYS_REFERENCE_UI_CELLULAR=0)
endif()
if(DEFINED PXSYS_REFERENCE_UI_BATTERY_PERCENT AND
   NOT PXSYS_REFERENCE_UI_BATTERY_PERCENT)
    target_compile_definitions(pxa_system_reference_lvgl PRIVATE
        PXSYS_REFERENCE_UI_BATTERY_PERCENT=0)
endif()
if(DEFINED PXSYS_REFERENCE_UI_GESTURE_HANDLE AND
   PXSYS_REFERENCE_UI_GESTURE_HANDLE)
    target_compile_definitions(pxa_system_reference_lvgl PRIVATE
        PXSYS_REFERENCE_UI_GESTURE_HANDLE=1)
endif()
if(DEFINED PXSYS_REFERENCE_UI_TASK_SWITCHER)
    if(PXSYS_REFERENCE_UI_TASK_SWITCHER STREQUAL "cards")
        target_compile_definitions(pxa_system_reference_lvgl PRIVATE
            PXSYS_REFERENCE_UI_TASK_SWITCHER=PXSYS_TASK_SWITCHER_CARDS)
    elseif(PXSYS_REFERENCE_UI_TASK_SWITCHER STREQUAL "list")
        target_compile_definitions(pxa_system_reference_lvgl PRIVATE
            PXSYS_REFERENCE_UI_TASK_SWITCHER=PXSYS_TASK_SWITCHER_LIST)
    else()
        message(FATAL_ERROR
            "PXSYS_REFERENCE_UI_TASK_SWITCHER must be 'cards' or 'list'")
    endif()
endif()
if(DEFINED PXSYS_REFERENCE_UI_TASK_PREVIEW_COUNT)
    target_compile_definitions(pxa_system_reference_lvgl PRIVATE
        PXSYS_REFERENCE_UI_TASK_PREVIEW_COUNT=${PXSYS_REFERENCE_UI_TASK_PREVIEW_COUNT})
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pxa_system_reference_lvgl PRIVATE -Wall -Wextra -Wpedantic)
    if(PXSYS_WARNINGS_AS_ERRORS)
        target_compile_options(pxa_system_reference_lvgl PRIVATE -Werror)
    endif()
endif()
