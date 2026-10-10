#pragma once

#include <cstddef>
#include <version>

#if __cplusplus <= 202302L
#error "PXA C++ SDK requires C++26 mode (-std=c++2c or -std=c++26)"
#endif
#if !defined(__cpp_explicit_this_parameter) || __cpp_explicit_this_parameter < 202110L
#error "PXA C++ SDK requires explicit object parameters; use locked WASI SDK 34"
#endif
#if !defined(__cpp_pack_indexing) || __cpp_pack_indexing < 202311L
#error "PXA C++ SDK requires pack indexing; use locked WASI SDK 34"
#endif
#if !defined(__cpp_deleted_function) || __cpp_deleted_function < 202403L
#error "PXA C++ SDK requires C++26 deleted-function explanations; use locked WASI SDK 34"
#endif
#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202202L
#error "PXA C++ SDK requires a standard library providing std::expected"
#endif

namespace pxa::features {
inline constexpr long language = __cplusplus;
#if defined(__cpp_lib_inplace_vector) && __cpp_lib_inplace_vector >= 202406L
inline constexpr bool inplace_vector = true;
#else
inline constexpr bool inplace_vector = false;
#endif
#if defined(__cpp_lib_function_ref) && __cpp_lib_function_ref >= 202306L
inline constexpr bool function_ref = true;
#else
inline constexpr bool function_ref = false;
#endif
} // namespace pxa::features

namespace pxa::detail {
template<std::size_t I, class... Types>
using PackElement = Types...[I];
}
