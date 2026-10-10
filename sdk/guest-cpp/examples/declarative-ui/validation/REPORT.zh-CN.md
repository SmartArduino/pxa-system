# 声明式 UI 完善与验证（2026-10-10）

对比基线是本轮开始前的 SDK 工作区快照，包含此前已经实现且尚未提交的改动，
没有拿 SDK HEAD 的旧功能集替代基线。快照、完整构建日志和原始 BGRA 保留在
工作区 `local/ui-elegance-20261010/`；SDK 仓库内保留本报告、汇总 JSON、截图和复现脚本。

## 本轮改变及验证

| 原因 | 实现与收益 | 内存变化 | 验证 |
|---|---|---|---|
| 普通与 WidgetStyle 按钮回调写法不同 | 两者均支持拥有/借用的 click 和 pointer；借用注册真实对象地址，避免临时 builder 悬空 | 普通控件无新增字段；借用仅保存引用包装；拥有闭包按其实际类型内联存储 | 片段 render 后销毁 builder，再点击/指针和更新标签；ASan/UBSan |
| 格式化快照不会随 State 更新 | Computed 显式依赖；一个输出每次 flush 只计算一次；动态按钮标签保留节点 | 无结果缓存或全局依赖图；只增加所选闭包、依赖指针/订阅和输出绑定 | 多依赖合并、空闲零提交、失败编码/提交可重试、浮点/宽整数及 AOT |
| 局部组件 this 捕获容易失效 | Component 在 Page 最终地址构造模型和子视图；拥有/借用语义明确 | 内联模型和可选子视图，仅使用组件的页面承担；无组件堆 | 不可移动 State 模型、条件移除/重建、失败候选保留旧回调 |
| DPI/安全区需要每个页面自行算 | SafeArea 绑定指标；通用 Dp 尺寸、约束和样式直接修饰已有节点 | 修饰不增加 Host 节点；SafeArea 显式增加一个布局节点，可选一个绑定 | 五显示配置、非对称边距、圆屏、高 DPI、区域耗尽/恢复、小屏滚动 |
| 直接动作可能丢弃 Result 或未启动 Task | void 回调约束 + Action；作用域明确，错误进入应用钩子 | TaskScope 无新增字段；复用既有协程池 | 同步/异步错误、池满、取消回收；真实 AOT 异步完成与前后台状态 |
| 开关显示成浮动圆点 | Host 复用现有背景/圆角属性恢复轨道 | 无新字段、节点、命令或额外本地样式属性 | 模拟器检查背景/圆角、开关状态及截图 |
| 第二次独立模拟器启动的动画停住 | 在 LVGL 清理前复位 SDL 自身的初始化标志和事件 timer | 无新存储，解除残留状态 | 五次独立运行均有有效 tick；截图开关状态/动画正常 |
| Release Host 测试跳过初始化并崩溃 | 测试保留 assert；隔离指针动画后测 Canvas dirty 区域 | 仅测试修改 | LVGL Host 回归连续五次通过 |

## 同一普通计数器的前后比较

原生环境为 Linux x86-64、Clang 23、C++26、O3、关闭异常/RTTI。
每次预热 10,000 次，7 组各 100,000 次 State 更新与 flush；前后交替运行八轮。
Host 桩读取所有提交字节并累计，避免编译器删除编码工作。

| 指标 | 修改前 | 修改后 |
|---|---:|---:|
| 原生 Page | 328 B | 328 B |
| 原生 State<int> | 16 B | 16 B |
| 原生 Context | 1,736 B | 1,736 B |
| 原生 TaskScope | 208 B | 208 B |
| 既有协程池及元数据 | 8,208 B | 8,208 B |
| 每轮总导入 | 2,130,003 | 2,130,003 |
| 提交字节累计校验 | 972,901,129 | 972,901,129 |
| 空闲 flush 导入 | 0 | 0 |
| 每次编码/提交桩的轮次中位数之中位数 | 100.896 ns | 101.565 ns |
| 各轮中位数范围 | 94.593–110.541 ns | 99.446–104.719 ns |
| 原版 counter 示例 Wasm | 55,592 B | 55,594 B |
| counter Linux AOT | 86,440 B | 86,424 B |
| counter 初始线性内存 | 2 页 | 2 页 |

存储、提交次数和提交内容一致；耗时差约 0.7%，小于轮次波动，本轮不声称速度提升。
Wasm 的 DATA 段大小相同；AOT 的小幅变化也不作为性能收益。
完整样本与产物哈希分别见 [microbench-paired.json](microbench-paired.json) 和
[counter-comparison.json](counter-comparison.json)。这是原生 Guest 编码微测，
不含 Wasm 执行、Host 光栅化、显示排队或实际屏幕 FPS。

新增原生功能回归在挂载、更新、移除、失败回滚、任务池满和取消全过程统计 Guest
`operator new`：零次分配。该结论针对数字/短文字及小捕获闭包，不包括应用显式
返回长 std::string、捕获动态容器或 Host/LVGL 的控件分配。

## 真实模拟器

测试运行签名包中的 C++ AOT，通过实际 Host UI 事务和 LVGL 控件，不使用图片
模拟交互。点击事件送入原生控件，滚动到对应控件后操作；前后台切换使用实际
生命周期派发。它没有用物理鼠标手势替代硬件触摸验收。

所有配置均完成：禁用/重新启用、Add 标签变成 Again、Computed 值合并、异步
完成、局部计数保留、前后台状态、Details 分支插入/移除、浮点编码和错误钩子。
协程挂起取消和池满另由原生测试验证；Clock yield 在模拟器同轮调度中即完成，
不把它称为模拟器里的挂起取消验证。

| 配置 | 字体 body 实际行高 | 标题位置 | 画面 |
|---|---:|---:|---|
| 240×320，160 DPI | 23 px | 12,12 | [profile-0.png](profile-0.png) |
| 480×640，305 DPI | 45 px | 22,22 | [profile-1.png](profile-1.png) |
| 454×454 圆屏 | 23 px | 79,79 | [profile-2.png](profile-2.png) |
| 320×240，24 px 圆角，T18/R8/B12/L32 安全区 | 23 px | 44,30 | [profile-3.png](profile-3.png) |
| 200×200 小屏 | 23 px | 12,12 | [profile-4.png](profile-4.png) |

截图直接读取已刷新显示缓冲，包括真正的系统形状遮罩；没有后期补画圆屏边界。
截图等待状态动画完成，小屏/横屏截图为页面顶部，其余操作通过滚动到控件验证。

每个配置启动全新 WAMR engine。本次完整示例（包含可选的标准库浮点转字符）
最终 WAMR allocator 占用 153,569 B、本次初始化至交互的真实峰值 170,369 B；
Guest 线性内存稳定/峰值均为 196,608 B，事件缓冲 128 B 已包含在线性内存中，
保留的 artifact buffer 为 0 B。详情见 [profiles.json](profiles.json)。
峰值不是历史多次运行累计值。WAMR allocator 与线性内存统计可能重叠，不应
相加；这里也不包含全部 mmap 代码、LVGL/字体、帧缓冲和整机物理内存。
本示例使用了新的可选功能，不能拿它与基础 counter 的占用当作同负载前后比较。
未用浮点绑定时不会链接该标准库格式化路径。

## 回归及复现

```sh
# pxa-system 根目录：完整原生 C++ SDK（包含编译拒绝检查）
bash tools/package/test_guest_cpp.sh

# 配置桌面模拟器时启用测试，并指向项目使用的 LVGL 目录
cmake -S simulator/desktop -B /tmp/pxa-ui-tests -DCMAKE_BUILD_TYPE=Release \
  -DPXSYS_BUILD_TESTS=ON -DPXA_BUILD_TESTS=ON \
  -DPXSYS_LVGL_SOURCE_DIR=/path/to/lvgl
cmake --build /tmp/pxa-ui-tests --target pxsys_declarative_ui_test \
  pxsys_i18n_app_test pxsys_sdk_patterns_test pxsys_desktop_simulator pxa_lvgl_ui_test -j4
PXA_SDK_TEST_ARTIFACT_DIR=/tmp/pxa-ui-captures \
ctest --test-dir /tmp/pxa-ui-tests --output-on-failure \
  -R 'pxsys_(declarative_ui|i18n_app|sdk_patterns)_test|pxsys_desktop_simulator_(smoke|self_test)|pxa_lvgl_ui_test'

# 相同源码对比两个 SDK 目录，结果中自动核对尺寸/导入/字节校验
python3 sdk/guest-cpp/examples/declarative-ui/validation/compare_sdk.py \
  --before /path/to/before-sdk --after sdk/guest-cpp --output /tmp/ui-comparison.json

# 新功能生命周期与边界检查
clang++ -std=c++2c -O1 -g -fno-exceptions -fno-rtti -Wno-attributes \
  -fsanitize=address,undefined -fno-omit-frame-pointer -I sdk/guest-cpp/include \
  sdk/guest-cpp/tests/ui_declarative_test.cpp sdk/guest-cpp/src/runtime.cpp -o /tmp/ui-sanitized
ASAN_OPTIONS=detect_leaks=1 /tmp/ui-sanitized
```

原生全量、额外编译诊断回归、ASan/UBSan、模拟器 smoke/self-test、i18n、SDK
patterns、新声明式示例及 LVGL adapter 均通过。LVGL adapter 额外重复五次。
三个无效写法在 Clang 23 下分别产生 13/13/19 行诊断；有效 Action 组合无诊断，
见 [diagnostics.json](diagnostics.json)。行数只是本次工具链记录，回归检查的是
API 边界处拒绝，而非固定行数。

导出新的独立 C++ SDK 开发包后，本示例三目标均构建成功；三个 Wasm 逐字节一致。
模拟器实际运行的是导出包编译的 AOT，并重新完成五组显示配置及交互。
现有 novel-reader 使用临时 styled builder 与持久借用回调，其模拟器目标也
重新构建成功；本轮的借用回调生命周期测试专门覆盖该写法。

| 独立包目标 | Wasm | AOT | 线性内存页数：初始/最大 |
|---|---:|---:|---:|
| Linux x86-64 | 468,963 B | 313,484 B | 3 / 16 |
| ESP32-S3 | 468,963 B | 493,144 B | 3 / 16 |
| ESP32-S31 | 468,963 B | 495,764 B | 3 / 16 |

完整产物哈希见 [builds.json](builds.json)；设备目标的结果仅代表
交叉构建，不代表 ESP32 真机触摸、显示帧率或 SRAM/PSRAM 已验收。
剩余设计边界见 [声明式 UI 开发指南](../../../DECLARATIVE_UI.zh-CN.md)：
显式依赖、借用寿命、无自动树 diff、无自动多行断点布局和组件任务作用域。
