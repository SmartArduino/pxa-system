include_guard(GLOBAL)

# Explicit opt-in; apply the same libc++ customization to application and SDK
# translation units, as required by libc++'s compile-time override contract.
function(pxa_use_bounded_libcpp_diagnostics target)
    if(NOT TARGET ${target} OR NOT TARGET pxa_guest_cpp)
        message(FATAL_ERROR "Call after pxa_add_component(... CPP ...)")
    endif()
    foreach(diagnostics_target IN ITEMS ${target} pxa_guest_cpp)
        get_target_property(enabled ${diagnostics_target} PXA_BOUNDED_LIBCPP_DIAGNOSTICS)
        if(NOT enabled)
            target_compile_options(${diagnostics_target} PRIVATE
                "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include pxa/libcpp_diagnostics.hpp>")
            set_target_properties(${diagnostics_target} PROPERTIES PXA_BOUNDED_LIBCPP_DIAGNOSTICS TRUE)
        endif()
    endforeach()
    get_target_property(adapter ${target} PXA_BOUNDED_LIBCPP_ADAPTER)
    if(NOT adapter)
        target_sources(${target} PRIVATE "${PXA_CPP_SDK_DIR}/src/libcpp_diagnostics.cpp")
        set_target_properties(${target} PROPERTIES PXA_BOUNDED_LIBCPP_ADAPTER TRUE)
    endif()
endfunction()
