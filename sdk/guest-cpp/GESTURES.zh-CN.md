# 点击、长按与拖动取消

普通 `Button(...).on_click(...)` 在松手时确认点击。需要长按时叠加可选的
`on_long_press`，不必自己保存按下时间，也不必添加 `on_frame`：

```cpp
using namespace pxa::ui;
using namespace std::chrono_literals;

Button("书籍")
    .on_click([this] { open_book(); })
    .on_long_press([this] { manage_book(); }, {.long_press = 550ms, .slop = 8});
```

`on_click` 和 `on_long_press` 顺序不限；普通按钮、状态/Computed 标签按钮和带 `WidgetStyle` 的按钮均可使用。
只有长按的按钮可直接写 `Button("提示").on_long_press(...)`。选项必须是正的长按时间、
非负的移动容差；无效选项使页面挂载返回 `invalid_argument`。
组件清单须声明 `ui` 和 `clock` 服务；不需要新增 Host 能力或 ABI。
可运行示例见 [examples/gestures](examples/gestures/main.cpp)。

长按从 DOWN 事件的单调时间戳计时，阈值到达即触发一次，UP 不再触发点击。
沿任一轴超出容差后，本次手势永久取消点击与长按，移回原处也不会恢复。
容差单位是逻辑像素，与 `CanvasPointer` 一致，因此能随 DPI 缩放。
不同 pointer、surface、node、generation 的事件不会结束已有的捕获。
后台切换、页面卸载、条件分支替换与虚拟列表回收会取消待触发手势；
成功的属性 PATCH 和稳定键列表重排保留手势，失败的提交保留旧页面和回调。

`AppRuntime` 仅在还有待触发长按时将帧钟周期暂时设为最多 16ms；
全部触发或取消后恢复应用周期，无帧循环的应用恢复为 0。
不会持续空转或为每个按钮创建任务。若 Guest 正忙，长按回调仍须等事件调度；
若 UP 先于下一次帧钟到达，识别器会补发已经达到阈值的长按，避免误执行短按。
这里保证语义和调度精度，不承诺屏幕反馈在 16ms 内完成。

## Canvas 与自定义游戏输入

按需包含 `<pxa/ui_gesture.hpp>`，每个独立触摸区域保存一个 `PressGesture`，
用已有游戏帧钟推进，不会再申请定时器。一个识别器一次捕获一根手指；
双摇杆等多指操作应各持有自己的识别器。

```cpp
PressGesture press;
static constexpr PressOptions options{.long_press = 500ms, .slop = 8};

void pointer(const CanvasPointer& event) {
    switch (press.update(event, options)) {
    case PressResult::tap: open(); break;
    case PressResult::long_press: show_menu(); break; // 帧钟延迟的补发
    case PressResult::none: break;
    }
}
void on_frame(pxa::Context&, pxa::game::FrameTick tick) {
    if (press.advance(tick.timestamp_us, options)) show_menu();
}
void on_background(pxa::Context&) { press.reset(); }
```

`active()` 表示指针仍被捕获；`dragged()` 表示移动曾超出容差；
`long_pressed()` 表示长按已经消费；`owns(event)` 检查完整的捕获身份。
`start_x/y()` 可供拖动位移计算，`deadline(options)` 可供自定义调度器使用。
仅希望识别点击及拖动取消时设置 `.long_press = 0us`，按多久都不会变成长按。
同一轮事件与 `advance` 应传入相同选项；它们不会在识别器内重复保存选项。

Canvas 所在 Page 的属性 PATCH 可能更新 generation。若确认节点仍是同一个，
在 Page 成功提交后调用 `rebase(generation)`；应用的 Canvas 回调已经经过 Page
过滤，也可以在回调中用经验证的 `event.generation` 更新身份。不要用未经校验的
外部事件调用 `rebase`。切换场景、节点身份变化、屏幕几何改变或进入后台须 `reset()`。
声明式按钮会自动管理捕获，以及上述页面和前后台生命周期。

识别器和声明式注册表均使用内联固定容量存储，无堆分配。普通按钮不携带长按状态，
未使用此能力的页面不预留手势表。带长按的自定义组合视图必须将子视图
`capacity.gestures` 汇总到自身预算；SDK 的布局、修饰器、组件、When、Refresh、
KeyedList、VirtualList 已自动汇总。持有手势的视图必须活到页面卸载，不能临时
构造一个按钮调用 `.render(...)` 后立刻销毁；普通临时绘制请使用已有的借用回调接口。

跳跃蓄力松手起跳、滑动后惯性和连续挖矿属于不同交互，不应简单替换成长按菜单。
`PressGesture` 不自带拖动动画、惯性、自动连发或双击延迟，应用可基于捕获状态按需实现。
