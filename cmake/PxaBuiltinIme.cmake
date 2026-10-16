include_guard(GLOBAL)
option(PXA_IME_BUILTIN_SIMPLIFIED "Compile the builtin simplified Chinese dictionary" ON)
option(PXA_IME_BUILTIN_TRADITIONAL "Compile the builtin traditional Chinese dictionary" OFF)
function(pxa_configure_builtin_ime target)
    target_compile_definitions(${target} PRIVATE
        PXA_IME_BUILTIN_SIMPLIFIED=$<BOOL:${PXA_IME_BUILTIN_SIMPLIFIED}>
        PXA_IME_BUILTIN_TRADITIONAL=$<BOOL:${PXA_IME_BUILTIN_TRADITIONAL}>)
endfunction()
