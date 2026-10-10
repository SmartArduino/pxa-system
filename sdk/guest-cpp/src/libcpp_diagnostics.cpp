#include <pxa/libcpp_diagnostics.hpp>

#if defined(__wasi__) && defined(_LIBCPP_VERSION)
// The pinned WASI libc++/libc++abi archives also contain already-compiled calls
// to their termination entry points. Preserve their ABI and termination, and
// route those calls through the same bounded handler. Native builds retain
// their platform runtime; no global/default SDK target links this adapter.
#include <__verbose_abort>
namespace std { inline namespace _LIBCPP_ABI_NAMESPACE {
[[noreturn]] void __libcpp_verbose_abort(const char* format, ...) noexcept {
    va_list args;
    va_start(args, format);
    ::pxa::detail::bounded_libcpp_abort_v(format, args);
}
} }

extern "C" [[noreturn]] void __abort_message(const char* format, ...) {
    va_list args;
    va_start(args, format);
    pxa::detail::bounded_libcpp_abort_v(format, args);
}
#endif
