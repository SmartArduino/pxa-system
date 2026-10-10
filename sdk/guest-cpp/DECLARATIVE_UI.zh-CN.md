# 声明式 UI：组合、绑定、布局与动作

本轮把普通控件、带样式控件和国际化文本的写法接到同一套能力上。
视图在挂载时构造，之后按显式 State 绑定发送属性 PATCH；没有每帧重建视图、
全树 diff 或自动依赖扫描。完整可运行示例在
[examples/declarative-ui/main.cpp](examples/declarative-ui/main.cpp)，
实测与复现方法见 [validation/REPORT.zh-CN.md](examples/declarative-ui/validation/REPORT.zh-CN.md)。
2026-10-11 的正确性修复、默认开销对照及独立包验证见
[validation/REPAIR-20261011.zh-CN.md](examples/declarative-ui/validation/REPAIR-20261011.zh-CN.md)。

## 基本写法

```cpp
#include <pxa/app.hpp>
#include <pxa/ui_layout.hpp>
#include <pxa/ui_component.hpp>
#include <pxa/ui_action.hpp>
using namespace pxa;
using namespace pxa::ui;
using namespace pxa::ui::literals;

struct CounterPanel {
    State<int> count{0};
    State<bool> editable{true};
    State<std::string> label{"Add"};

    auto view() {
        return Column(
            Text(Computed([](int n) { return n * 2; }, count))
                .font(Font::headline),
            Button(label).on_click([this] {
                count.update([](int n) { return n + 1; });
                label.set("Again");
            }).height(36_dp).enabled(editable),
            Toggle("Enabled", editable)
        ).gap(8_dp).fill();
    }
};

// display 来自启动配置和 DisplayMetrics 通知；保存在应用模型中。
// 父视图可写为：
// SafeArea(display, Scroll(Component<CounterPanel>())).padding(12_dp)
```

普通按钮、`Button(State<T>&)` 和 `Button(label, WidgetStyle)` 都支持拥有回调的
`on_click(callback)`，以及显式借用的 `on_click_ref(callback)`。
同样提供 `on_pointer` / `on_pointer_ref`；指针按钮只订阅指针事件，抬起时不会
再发一次点击。回调拥有其捕获对象；捕获引用仍由调用者保证生命周期。
`on_click_ref` 必须引用比挂载 Page/片段活得更久的对象。

借用回调被注册为实际回调对象的地址。已有自定义片段可以继续使用
`Button(...).on_click_ref(action).render(owner, parent)`：临时 builder 销毁后，
回调仍有效。普通拥有回调、Computed 和组件的视图应交给 Page 持有，不能先
在临时视图上 render 再销毁它。

## 文本与派生值

`Text(state)` 支持整数、布尔、浮点数、`std::string`、`std::string_view` 及
提供 `view()` 的固定文本缓冲。动态按钮标签使用同样的绑定；更新只修改文字，
保留按钮节点及输入状态。国际化 `TextBuffer` 与 `i18n_ui.hpp` 继续兼容。

```cpp
Text(Computed([](int count, bool enabled) {
    return std::string(enabled ? "Count: " : "Paused: ") + std::to_string(count);
}, count, enabled))

Button(Computed([](int count) { return count; }, count)).on_click(callback)
```

依赖显式列在 `Computed(function, states...)` 后面。多个依赖在同一周期改变时，
每次 flush 只计算一次输出，且只占一个 Page 绑定/脏位。整数、浮点和布尔输出
在有界栈缓冲中编码；浮点转字符的标准库实现只在使用浮点绑定时链接。
格式化器可返回 `Result<T>`；失败中止事务，保留脏位供重试。

Computed 不缓存结果字符串、不分配依赖图。它内联存放闭包、依赖指针和每个
依赖的订阅，随拥有它的视图一起存活。每个 Computed 只属于一个控件，用返回的
临时值或 `std::move(source)` 交给 Text/Button；依赖 State 必须存活更久。
格式化器应保持纯计算，不在计算过程中修改被订阅状态。
返回 `std::string` 或捕获动态容器可能分配内存，这是应用显式选择的行为。
需要确定的上限时，返回已有的固定容量文本缓冲或数字。

`Text(std::to_string(count.get()))` 是挂载时的快照，仍不会自动绑定；使用 State
或 Computed 才会更新。显式 `std::string_view` 借用的字节须存活到下一次编码；
State 本身存活并不保证它指向的字符串存活。直接 `Text(std::string)` 拥有文字。

## 通用修饰与安全区

| 修饰 | 用途 |
|---|---|
| `width/height/min_width/max_width/min_height/max_height(Dp)` | 明确尺寸和约束 |
| `fill()/fill_height()/grow(weight)` | 填充或分配剩余空间 |
| `padding/gap/radius/border_width(Dp)` | 间距与形状 |
| `padding(Padding{left, top, right, bottom})` | 明确四侧内容边距，包括 Row/Column 和 SafeArea |
| `align(Align)/justify(Justify)` | 子节点排列 |
| `font(Font)/color(Color)/rgba(value)/text_align(TextAlign)` | 字体角色与文字样式 |
| `background(Color 或 uint32_t)/border_color(...)` | 主题角色或 RGBA |
| `visible/enabled(bool 或 State<bool>&)` | 固定值或绑定的显示/交互标志 |
| `frame(CanvasRegion)` | 显式绝对布局，坐标与大小为逻辑像素 |

修饰器直接给控件根节点写属性，没有新增 Host 包装节点。按钮的文字属性写到
其文字节点；容器修饰不强制覆盖所有后代已经明确指定的字体和颜色。
重复设置同一属性时以最后一次为准；被覆盖的通用修饰器及其 State 绑定在编译期
移除，旧 State 的变化不会把新值覆盖回去。这也适用于封装后的 Component。
Toggle 的字体修饰定位文字节点，enabled 定位实际开关；禁用容器会阻止后代输入，
重新启用容器时仍保留各子控件自身的禁用设置。约束间的关系仍应由应用合理指定，例如
最小宽度不应大于最大宽度。`Justify` 仅公开 Host 已实现的值。

```cpp
SafeArea(display, Scroll(Column(
    Text("Settings").font(Font::title),
    Row(button_a, button_b).gap(6_dp).fill(),
    Component(preferences)
).gap(8_dp).fill())).padding(12_dp)
```

`SafeArea` 接受 DisplayMetrics 快照，或 `State<DisplayMetrics>&`。
它添加一个布局节点，使用现有安全矩形算法避开边距、圆角与圆屏外侧，按 DPI
换算四侧内边距并向安全方向取整。`.padding()` 是额外的内容边距。
绑定版本在指标变化时只更新边距和可见性；安全区和额外内容边距完全占用可用区域时
隐藏内容。`.visible(bool 或 State<bool>&)` 与这个条件共同决定可见性，指标恢复不能
覆盖应用指定的隐藏状态。指标与可见性两个来源合用一个 Page 输出绑定；只有同时
订阅两者时才保存额外订阅。组件 State 和输入节点不因调整安全区而重建。

普通可复制视图可以在局部变量上继续修饰；拥有候选页面的 Component、When、List、
Refresh 等视图需要 `std::move(view).width(...)`，或直接串联返回的临时视图。
List/Refresh 与控件使用同一套通用修饰器，修饰不增加布局包装节点。
`_dp` 字面量在编译期检查整数溢出；协议尺寸范围仍在事务编码时校验。

原生控件的 Dp 尺寸由 Host 根据密度转换，字体角色也由 Host 选择实际字体。
不要在应用中把角色字号再乘一次 DPI。小屏内容超过可用高度时应使用 Scroll；
SafeArea 不会自动缩小文字或把一排按钮改成多排。
流式换行、任意 grid DSL 和逐项响应式断点尚未封装；当前 Host 的 wrap 属性
并未实现，SDK 不暴露一个看似可用但不起作用的接口。
已有 WidgetStyle 绝对布局仍可用，新页面优先使用流式布局及这些修饰器。

## 组件所有权

```cpp
Component(model)              // 借用现有模型
Component(MovableModel{...})  // 拥有可移动模型
Component<LocalCounter>()     // 在最终地址构造，可包含不可移动的 State
Component<Panel>(args...)     // 拥有构造参数，在 render 时构造 Panel
```

模型提供返回视图的 `view()`。组件在 Page 中到达稳定地址后才构造子视图，
所以子回调可以捕获模型的 `this`。模型和子视图均为内联存储，没有隐式的
组件堆、全局缓存或额外 Host 节点。`Component<T>(args...)` 拥有衰减后的参数；
要显式借用参数可传 `std::ref`，并让模型构造函数接受相应类型。

组件挂载后不能移动。组件中的局部 State 随组件销毁；When/列表移除组件时
先解除订阅和回调，再销毁模型。失败的候选提交不会替换活动组件。
需要跨条件切换保留的数据放在外部模型，通过 `Component(model)` 借用。
不自动为每个组件创建 TaskScope；这避免扩大所有小组件的默认存储。

初次挂载编码或提交失败时，同一个 Page 可以再次 mount。SDK 先清理未激活的
引用、绑定、回调和候选视图，再重建；Component 的稳定模型保留，因此移动专用
构造参数不被消费两次。重试会重新调用 `view()`，它应只构造视图，不执行一次性的
业务操作。When/列表的失败候选仍可销毁后重新创建，不保证候选构造函数只执行一次。
自定义复合视图若缓存渲染过程，可提供 `void reset_mount() noexcept`，传播到其
缓存子视图；该钩子只清理失败的首次构建，不能销毁仍活动的页面。普通无缓存视图无需它。

## 按需环境状态

```cpp
#include <pxa/ui_environment.hpp>

struct SettingsApp {
    Environment<> environment; // 默认只保存 State<DisplayMetrics>
    Result<void> on_start(Context&, std::span<const std::byte> config) {
        return environment.initialize(config);
    }
    Result<bool> on_event(Context&, const Event& event) {
        return environment.update(event);
    }
    auto view() {
        return SafeArea(environment.display(), Scroll(Text("Settings"))).padding(12_dp);
    }
};
```

需要主题或能力时，显式选择 `Environment<DisplayMetrics, UiAppearance, UiCapabilities>`，
通过 `appearance()` / `capabilities()` 或 `state<T>()` 访问。仅订阅主题可使用
`Environment<UiAppearance>`；不存在的状态访问和重复类型会在接口处拒绝编译。
Environment 借用关系与普通 State 相同，应比绑定它的 Page 活得更久；不增加 Context
字段、全局缓存、默认服务请求或协程。initialize 校验全部所选字段后才发布状态，
update 处理屏幕/主题通知，返回 false 表示不是所选通知，错误保留已有状态。
这里的发布是串行事件模型中的完整校验后更新，不提供线程同步。

DisplayMetrics、UiCapabilities、UiAppearance 具有值比较，完全相同的通知不会标脏。
启动环境只提供主题的深浅色方案；初始 UiAppearance 的 generation 和调色板为零。
需要完整调色板时，显式调用 `ThemeService::get()`，再把结果写入 `appearance()`。
之后主题由 0x8006 主题通知更新，屏幕指标通知不会隐式发起主题查询。

## 可失败及异步动作

直接 `on_click` 回调返回 void；返回 `Result<void>` 或 `Task<void>` 的回调应显式
适配，避免丢弃错误或未启动的任务：

```cpp
Button("Save").on_click(Action(context.tasks(), [this]() -> Result<void> {
    return save_synchronously();
}))
Button("Load").on_click(Action(context.foreground_tasks(), [this] {
    return load_asynchronously(); // Task<void>
}))
```

Action 拥有闭包并借用 TaskScope；选择应用、前台或页面作用域，由所选作用域
决定取消时机。同步错误、任务启动失败和任务完成错误使用该作用域的错误钩子。
任务捕获组件/页面的 `this` 时，须在该对象销毁前取消对应作用域；可以让模型
显式拥有 TaskScope，由其析构取消，或捕获存活更久的应用模型。不能把短寿命
组件的捕获交给应用作用域后假设组件移除会自动取消它。
Context 的作用域连接到应用 `on_error(Context&, Error)`；独立作用域可调用
`scope.on_error(...)`。未安装钩子时保留原来的诊断输出。
指针动作也可以使用 Action，回调接收 `const CanvasPointer&`；异步任务应复制
需要长期使用的坐标等值，不跨挂起点借用事件对象。

ViewLike/ClickCallback/PointerCallback 在组合处检查参数。漏写按钮动作、
把普通模型直接放入 Column、直接丢弃 Result 动作，会在相应调用处拒绝编译。
把模型包在 Component 中，把失败动作包在 Action 中即可表达其真实语义。

## 开销与验证边界

普通 Page 的绑定、处理器、动态模块容量仍由视图类型推导，Context 和默认池
没有新增字段。只有实际使用的修饰器、依赖、组件模型和 SafeArea 才占内联存储。
When/Refresh 的候选树与活动树短暂共存仍需计入峰值；新接口不消除这个正确性
所需的事务成本。

验证覆盖原生回归、ASan/UBSan、真正的签名 C++ AOT、实际 LVGL/Host 提交，
以及矩形、圆屏、圆角/非对称安全区、305 DPI 和小屏滚动。它证明本轮 UI 行为
及默认存储兼容，不能据此推导 ESP32 显示帧率或整机 SRAM/PSRAM 峰值。
