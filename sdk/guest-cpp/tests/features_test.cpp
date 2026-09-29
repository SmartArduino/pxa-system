#include <pxa/core.hpp>

#include <cassert>
#include <cstdio>
#include <expected>
#include <span>
#include <type_traits>
#include <utility>
#if defined(__cpp_lib_inplace_vector) && __cpp_lib_inplace_vector >= 202406L
#include <inplace_vector>
#endif
#if defined(__cpp_lib_function_ref) && __cpp_lib_function_ref >= 202306L
#include <functional>
#endif

struct Value {
    int n;
    template<class Self> constexpr decltype(auto) next(this Self&& self) {
        ++self.n;
        return std::forward<Self>(self);
    }
};
extern "C" std::int32_t pxa_submit(const std::uint8_t*, std::uint32_t) { return -3; }
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*,
                                  std::uint32_t) { return -3; }
static_assert(pxa::features::language > 202302L);
static_assert(std::same_as<pxa::detail::PackElement<1, char, int>, int>);
static_assert(std::same_as<decltype(std::declval<Value&>().next()), Value&>);
static_assert(std::same_as<decltype(Value{}.next()), Value&&>);
static_assert([] { Value value{1}; return value.next().n == 2; }());

int main() {
    Value value{3};
    assert(&value.next() == &value && value.n == 4);
    std::expected<int, int> result{7};
    assert(*result == 7);
    int values[]{1, 2};
    assert(std::span{values}.size() == 2);
#if defined(__cpp_lib_inplace_vector) && __cpp_lib_inplace_vector >= 202406L
    // Clang with libstdc++ 16 diagnoses the library's own is_trivial_v use.
#if defined(__clang__) && defined(__GLIBCXX__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    std::inplace_vector<int, 1> bounded;
    assert(bounded.try_push_back(1) && !bounded.try_push_back(2));
#if defined(__clang__) && defined(__GLIBCXX__)
#pragma clang diagnostic pop
#endif
#endif
#if defined(__cpp_lib_function_ref) && __cpp_lib_function_ref >= 202306L
    auto callback = [&] { ++value.n; };
    std::function_ref<void()> borrowed{callback};
    borrowed();
    assert(value.n == 5);
#endif
    std::printf("PXA features: language=%ld explicit_this=1 pack_indexing=1 "
                "inplace_vector=%d function_ref=%d\n",
                pxa::features::language, pxa::features::inplace_vector,
                pxa::features::function_ref);
}
