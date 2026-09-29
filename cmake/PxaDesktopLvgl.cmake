include_guard(GLOBAL)

function(pxsys_prepare_desktop_lvgl desktop_source_dir)
    if(TARGET lvgl)
        return()
    endif()
    set(PXSYS_LVGL_SOURCE_DIR "" CACHE PATH
        "Existing LVGL 9.6 source tree; empty downloads the pinned release")
    set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
    set(LV_BUILD_CONF_PATH "${desktop_source_dir}/lv_conf.h"
        CACHE PATH "" FORCE)
    if(PXSYS_LVGL_SOURCE_DIR)
        add_subdirectory("${PXSYS_LVGL_SOURCE_DIR}"
                         "${CMAKE_CURRENT_BINARY_DIR}/lvgl" EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(lvgl_source
            GIT_REPOSITORY https://github.com/lvgl/lvgl.git
            GIT_TAG v9.6.0
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(lvgl_source)
        set_property(DIRECTORY "${lvgl_source_SOURCE_DIR}"
                     PROPERTY EXCLUDE_FROM_ALL TRUE)
    endif()
endfunction()
