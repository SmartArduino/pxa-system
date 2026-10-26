# System code must not compile bundled copyleft optional implementations.
# Do not patch/relicense vendor files: remove their translation units entirely.
function(pxa_restrict_lvgl target)
    get_target_property(sources ${target} SOURCES)
    list(FILTER sources EXCLUDE REGEX "/libs/frogfs/")
    list(FILTER sources EXCLUDE REGEX "/libs/thorvg/")
    set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
    target_compile_definitions(${target} PRIVATE
        LV_USE_FS_FROGFS=0 LV_USE_THORVG_INTERNAL=0 LV_USE_THORVG_EXTERNAL=0)
endfunction()

function(pxa_add_system_license_gate target sdk_root)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(gate "${sdk_root}/tools/licensing/check_system_licenses.py")
    add_custom_target(${target}
        COMMAND "${Python3_EXECUTABLE}" "${gate}"
            --source-root "${sdk_root}/libpxa"
            --source-root "${sdk_root}/system"
            --source-root "${sdk_root}/ui"
            --source-root "${sdk_root}/platforms"
        COMMENT "Checking system source license policy"
        VERBATIM)
endfunction()
