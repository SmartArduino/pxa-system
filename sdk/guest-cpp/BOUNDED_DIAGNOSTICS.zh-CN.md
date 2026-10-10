# 可选 libc++ 有界诊断

`PxaCppDiagnostics.cmake` 提供 `pxa_use_bounded_libcpp_diagnostics(target)`。
需要节省 AOT 加载临时内存的 WASI C++ 应用可以显式启用；其他应用不新增源文件、
缓冲、编译定义或运行状态。它保持 O3、容器硬化检查及异常终止，不降低检查等级。

```cmake
include(PxaGuest)
pxa_add_component(pxa_main CPP COMPONENT_ID main SOURCES main.cpp)
include(PxaCppDiagnostics)
pxa_use_bounded_libcpp_diagnostics(pxa_main)
```

该函数在应用和 `pxa_guest_cpp` 的所有 C++ 翻译单元中先包含
`pxa/libcpp_diagnostics.hpp`，使用 libc++ 的 `_LIBCPP_VERBOSE_ABORT` 定制机制。
必须对同一应用的所有组件一致启用；SDK 仍按应用独立构建。
此约束来自 [LLVM 的定制说明](https://releases.llvm.org/17.0.1/projects/libcxx/docs/UsingLibcxx.html)。
WASI 还显式链接 `src/libcpp_diagnostics.cpp`，将预编译 libc++ / libc++abi 的两个
终止入口转入同一处理器；原生构建不替换系统运行库入口。

当前验证工具链为 **wasi-sdk 34.0 / LLVM 23.1**。适配器涉及运行库内部 ABI，
升级工具链时须重新验证链接、错误路径和 AOT 符号，不承诺其他版本的内部 ABI。
它不替代应用的日志、用户错误处理器或通用 `printf`。

仅在致命错误路径上使用 512 B 局部缓冲，无堆分配及常驻状态。输出保留文件名、
行号、断言条件和错误上下文，过长诊断截断，随后仍调用 `abort()`。
支持 `%s`、`%d` / `%i`、`%u`、`%zu`、`%td` / `%ti`、`%%`；
遇到其他格式后原样输出剩余格式字符串，停止读取可变参数，避免错误推断参数类型。

`tests/libcpp_diagnostics_test.cpp` 在子进程检查 SIGABRT、混合整数 / 字符串输出、
空指针、超长消息和不支持格式；ASan / UBSan 通过。它已经加入
`tools/package/test_guest_cpp.sh`。阅读器另完成三个架构 AOT 链接、完整系统五种
屏幕规格和 pai-touch 正式包运行验证；它的内存对照见应用排版验收报告。
