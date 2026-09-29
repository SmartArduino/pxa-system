include_guard(GLOBAL)
include(CheckCXXSourceCompiles)

function(pxa_check_cpp_features)
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -std=c++2c -fno-exceptions -fno-rtti")
    check_cxx_source_compiles([=[
        #include <coroutine>
        #include <expected>
        #include <span>
        #include <utility>
        #if __cplusplus <= 202302L
        #error C++26 mode is required
        #endif
        template<unsigned I, class... T> using At = T...[I];
        struct Value {
            int n;
            template<class Self> constexpr decltype(auto) next(this Self&& self) {
                ++self.n;
                return std::forward<Self>(self);
            }
        };
        static_assert(std::is_same_v<At<1, char, int>, int>);
        static_assert([] { Value v{1}; return v.next().n == 2; }());
        static_assert(std::is_same_v<decltype(Value{}.next()), Value&&>);
        int main() {
            std::expected<int, int> value{1};
            return *value - 1;
        }
    ]=] PXA_CPP_REQUIRED_FEATURES)
    if(NOT PXA_CPP_REQUIRED_FEATURES)
        message(FATAL_ERROR
            "PXA C++ SDK requires C++26 mode, explicit object parameters, pack indexing, "
            "std::expected, std::span and coroutine support. Use locked WASI SDK 34; "
            "see sdk/guest-cpp/FEATURES.zh-CN.md. No language fallback is performed.")
    endif()
    check_cxx_source_compiles([=[
        #include <inplace_vector>
        int main() {
            std::inplace_vector<int, 1> values;
            return values.try_push_back(1) == nullptr;
        }
    ]=] PXA_CPP_HAS_INPLACE_VECTOR)
    check_cxx_source_compiles([=[
        #include <functional>
        int main() {
            int value = 0;
            auto callback = [&] { ++value; };
            std::function_ref<void()> borrowed{callback};
            borrowed();
            return value - 1;
        }
    ]=] PXA_CPP_HAS_FUNCTION_REF)
    message(STATUS "PXA optional C++ library features: "
        "inplace_vector=${PXA_CPP_HAS_INPLACE_VECTOR}; function_ref=${PXA_CPP_HAS_FUNCTION_REF}")
endfunction()
