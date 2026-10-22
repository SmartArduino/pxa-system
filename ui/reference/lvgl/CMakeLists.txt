if(NOT TARGET lvgl)
    message(FATAL_ERROR "PXA reference LVGL UI requires an lvgl target")
endif()
if(NOT TARGET pxsys::reference_layout)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/.." pxa_reference_layout)
endif()

add_library(pxa_system_reference_lvgl
    src/reference_lvgl.c
    src/reference_ime.c
    src/reference_lock_digits_72.c)
add_library(pxsys::reference_lvgl ALIAS pxa_system_reference_lvgl)
set_target_properties(pxa_system_reference_lvgl PROPERTIES EXPORT_NAME reference_lvgl)
target_compile_features(pxa_system_reference_lvgl PUBLIC c_std_99)
target_include_directories(pxa_system_reference_lvgl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>)
target_link_libraries(pxa_system_reference_lvgl PUBLIC
    pxsys::standard pxsys::reference_layout lvgl)
# The keyboard UI has no runtime dependency. Reuse the adapter when libpxa is
# present; a native-only system compiles the same two UI sources directly.
get_filename_component(_ime_core_root "${CMAKE_CURRENT_LIST_DIR}/../../../libpxa" ABSOLUTE)
target_include_directories(pxa_system_reference_lvgl PRIVATE
    "${_ime_core_root}/include" "${_ime_core_root}/adapters/include")
if(TARGET pxa::adapters_lvgl)
    target_link_libraries(pxa_system_reference_lvgl PRIVATE pxa::adapters_lvgl)
else()
    include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PxaBuiltinIme.cmake")
    pxa_configure_builtin_ime(pxa_system_reference_lvgl)
    target_sources(pxa_system_reference_lvgl PRIVATE
        "${_ime_core_root}/adapters/lvgl/pxa_lvgl_ime.c"
        "${_ime_core_root}/adapters/lvgl/pxa_lvgl_ime_builtin.c")
endif()
if(PXSYS_BUILD_TESTS OR BUILD_TESTING)
    add_executable(pxsys_reference_ime_test ../tests/reference_ime_test.c)
    target_link_libraries(pxsys_reference_ime_test PRIVATE pxsys::reference_lvgl)
    target_include_directories(pxsys_reference_ime_test PRIVATE
        "${_ime_core_root}/adapters/lvgl" "${_ime_core_root}/adapters/include" "${_ime_core_root}/include")
    if(NOT MSVC)
        target_compile_options(pxsys_reference_ime_test PRIVATE -UNDEBUG)
    endif()
    add_test(NAME pxsys_reference_ime_test COMMAND pxsys_reference_ime_test)
    foreach(script RANGE 0 3)
        set(dict_target pxsys_ime_dictionary_${script}_test)
        math(EXPR simplified "${script} & 1")
        math(EXPR traditional "(${script} >> 1) & 1")
        add_executable(${dict_target} ../tests/ime_dictionary_test.c
            "${_ime_core_root}/adapters/lvgl/pxa_lvgl_ime_builtin.c")
        target_include_directories(${dict_target} PRIVATE
            "${_ime_core_root}/include" "${_ime_core_root}/adapters/lvgl")
        target_compile_definitions(${dict_target} PRIVATE EXPECT_SCRIPTS=${script}
            PXA_IME_BUILTIN_SIMPLIFIED=${simplified} PXA_IME_BUILTIN_TRADITIONAL=${traditional})
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${dict_target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME ${dict_target} COMMAND ${dict_target}
            "${CMAKE_CURRENT_LIST_DIR}/../../../tools/ime/data/tgh2013.tsv"
            "${CMAKE_CURRENT_LIST_DIR}/../../../tools/ime/data/traditional-readings.tsv")
    endforeach()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME pxsys_ime_dictionary_generated_test
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_LIST_DIR}/../../../tools/ime/generate_builtin.py" --check)

    function(pxsys_add_reference_feature_test name)
        add_executable(${name} ../tests/reference_features_test.c)
        target_link_libraries(${name} PRIVATE pxsys::reference_lvgl)
        target_compile_definitions(${name} PRIVATE ${ARGN})
        if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME ${name} COMMAND ${name})
    endfunction()
    add_executable(pxsys_launcher_focus_test ../tests/launcher_focus_test.c)
    target_link_libraries(pxsys_launcher_focus_test PRIVATE pxsys::reference_lvgl)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pxsys_launcher_focus_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME pxsys_launcher_focus_test COMMAND pxsys_launcher_focus_test)
    # Exercise independent switches: products may replace settings while
    # retaining radio chrome, or show battery text without either radio.
    foreach(settings RANGE 0 1)
        foreach(radios RANGE 0 3)
            math(EXPR wifi "${radios} & 1")
            math(EXPR cellular "(${radios} >> 1) & 1")
            set(config_target pxsys_reference_config_${settings}_${radios})
            add_library(${config_target} OBJECT src/reference_lvgl.c)
            target_link_libraries(${config_target} PRIVATE pxsys::reference_lvgl)
            target_compile_definitions(${config_target} PRIVATE
                PXSYS_REFERENCE_UI_BUILTIN_SETTINGS=${settings}
                PXSYS_REFERENCE_UI_WIFI=${wifi}
                PXSYS_REFERENCE_UI_CELLULAR=${cellular}
                PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS=0
                PXSYS_REFERENCE_UI_ENABLE_WALLPAPER=0
                PXSYS_REFERENCE_UI_BACK_GESTURE=0
                PXSYS_REFERENCE_UI_GESTURE_HANDLE=0
                PXSYS_REFERENCE_UI_BATTERY_PERCENT=1
                PXSYS_REFERENCE_UI_TASK_SWITCHER=PXSYS_TASK_SWITCHER_LIST)
            if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_options(${config_target} PRIVATE
                    -Wall -Wextra -Wpedantic -Werror)
            endif()
        endforeach()
        pxsys_add_reference_feature_test(pxsys_reference_features_${settings}_test
            PXSYS_REFERENCE_UI_BUILTIN_SETTINGS=${settings})
    endforeach()
    # Cards must fall back to a working list if the product's LVGL omits the
    # snapshot API. The test overrides the header switch before including the UI.
    pxsys_add_reference_feature_test(pxsys_reference_no_snapshot_test
        PXSYS_TEST_NO_SNAPSHOT=1 PXSYS_REFERENCE_UI_BUILTIN_SETTINGS=0
        PXSYS_REFERENCE_UI_TASK_SWITCHER=PXSYS_TASK_SWITCHER_CARDS)
    pxsys_add_reference_feature_test(pxsys_reference_no_pinyin_test PXSYS_TEST_PINYIN=0)
    pxsys_add_reference_feature_test(pxsys_reference_k26_test PXSYS_TEST_PINYIN=1)
    pxsys_add_reference_feature_test(pxsys_reference_no_animations_test
        PXSYS_REFERENCE_UI_ENABLE_ANIMATIONS=0)
endif()
if(DEFINED PXSYS_REFERENCE_UI_LAUNCHER_COLUMNS AND
   DEFINED PXSYS_REFERENCE_UI_LAUNCHER_ROWS)
    target_compile_definitions(pxa_system_reference_lvgl PRIVATE
        PXSYS_REFERENCE_UI_LAUNCHER_COLUMNS=${PXSYS_REFERENCE_UI_LAUNCHER_COLUMNS}
        PXSYS_REFERENCE_UI_LAUNCHER_ROWS=${PXSYS_REFERENCE_UI_LAUNCHER_ROWS})
endif()
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
if(DEFINED PXSYS_REFERENCE_UI_BACK_GESTURE AND
   NOT PXSYS_REFERENCE_UI_BACK_GESTURE)
    target_compile_definitions(pxa_system_reference_lvgl PRIVATE
        PXSYS_REFERENCE_UI_BACK_GESTURE=0)
endif()
if(DEFINED PXSYS_REFERENCE_UI_BUILTIN_SETTINGS AND
   NOT PXSYS_REFERENCE_UI_BUILTIN_SETTINGS)
    target_compile_definitions(pxa_system_reference_lvgl PRIVATE
        PXSYS_REFERENCE_UI_BUILTIN_SETTINGS=0)
endif()
if(DEFINED PXSYS_REFERENCE_UI_WALLPAPER AND NOT PXSYS_REFERENCE_UI_WALLPAPER)
    target_compile_definitions(pxa_system_reference_lvgl PRIVATE
        PXSYS_REFERENCE_UI_ENABLE_WALLPAPER=0)
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
