# C++ 多点触摸

`App::on_pointer(Context&, const input::Pointer&)` 可直接接收窗口指针事件，
GameRender / Surface 应用无需创建 Canvas 或 `view()`。控件区域继续使用
`Canvas(...).on_pointer(callback)` 或 `Button(...).on_pointer(callback)`；
`ui::CanvasPointer` 是 `input::Pointer` 的兼容别名。事件包含独立 `pointer_id`、时间戳、坐标以及
DOWN / MOVE / UP / CANCEL；支持几根手指由硬件和 Host 决定，不由 C++ 回调限制。
pai-touch 的 CST826 驱动提供两个独立 LVGL pointer indev。

原来的接口只有事件解码，应用需要自行维护 ID 和释放条件。现在提供可选的
`input::PointerCapture`，让每项持续操作独立拥有一根手指：

```cpp
#include <pxa/app.hpp>

struct GameInput {
    pxa::input::PointerCapture walk, look;
    int middle_x = 148;
    int walk_origin_y = 0, look_last_x = 0;
    int forward_pixels = 0, turn_pixels = 0;

    void pointer(const pxa::input::Pointer& event) {
        using enum pxa::input::PointerPhase;
        if (event.action() == down) {
            // Do not let a replayed DOWN acquire the other control.
            if (walk.owns(event) || look.owns(event)) return;
            if (event.x < middle_x) {
                if (walk.try_capture(event)) walk_origin_y = event.y;
            } else if (look.try_capture(event)) look_last_x = event.x;
            return;
        }
        if (walk.owns(event)) {
            if (event.action() != cancel)
                forward_pixels = walk_origin_y - event.y;
            if (walk.release(event)) forward_pixels = 0;
        }
        if (look.owns(event)) {
            // UP carries the final coordinate even if MOVE was coalesced.
            if (event.action() != cancel) {
                turn_pixels += event.x - look_last_x;
                look_last_x = event.x;
            }
            look.release(event);
        }
    }

    void on_pointer(pxa::Context&, const pxa::input::Pointer& event) { pointer(event); }
    void on_background(pxa::Context&) { reset(); }

    void reset() {
        walk.reset(); look.reset();
        forward_pixels = turn_pixels = 0;
    }
};
```

`PointerCapture` 只接受 DOWN，已经占用时不会被另一根手指覆盖。
`owns()` 匹配 surface、node、generation 和 pointer_id，避免页面重建、冒泡到其他
节点、其他手指的事件误释放当前操作。`release()` 同时接受匹配的 UP 和 CANCEL，
返回是否释放。点击、射击、蓄力结束等动作需要额外检查 `action() == PointerPhase::up`，
不能将 CANCEL 当成点击。拖过控制区边界仍归最初的控件所有。

每个捕获对象固定 **16 B**，无动态分配、事件副本、虚函数、全局触点表或框架默认
缓存。坐标、速度和手势策略由需要它们的控件保存；无需使用整个手势识别器。
`CanvasPointer` 对象布局、事件线格式和已有 `phase` 字节均未改变；`action()`
提供强类型枚举，`ended()` 识别 UP / CANCEL。整个捕获生命周期可在常量求值中运行。

注意：

- ID 是触点身份，不是数组下标；不能假定只会收到 0、1 或连续编号。
- 同一根手指可能向子节点和订阅事件的祖先分别发送事件。游戏通常只订阅一个输入节点。
- MOVE 可以被合并；UP 的最终坐标仍须处理，CANCEL 的坐标不应产生移动或点击。
- 应用进入后台、页面卸载和重建时调用 `reset()`，不能依赖一定收到最后一个 UP。
- Host 从最早注册的 pointer indev 起编号；增加输入设备不会重命名已有手指。
  当前 LVGL 适配器要求交互期间保留这些 indev；删除较早的设备前必须取消全部手势。
  任意热插拔时仍保持全局永久 ID，不在当前保证范围内。
- 能区分多指事件不等于提供双指缩放/旋转识别器；此类手势可以在两个捕获对象上按需实现。

验证包括 SDK 两指捕获常量求值测试、真实 LVGL 双 indev 的交错移动/独立取消、
注册新设备时身份不漂移，以及 Maze Evil 模拟器和 pai-touch 的双指行走/转向。
设备及内存测试细节见 pxa-apps 的 Maze Evil 多点触摸验证报告。
