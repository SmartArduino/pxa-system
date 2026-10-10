# 可选的声明式控件样式与页面刷新

`ui_widgets.hpp` 封装原生 Text、Button、Box 的定位、颜色、圆角和字体角色；
`ui_refresh.hpp` 根据 State 的版本重建一段声明式控件树。没有 Canvas 命令、
命中区域表或应用自行解析点击事件。普通 `Text` / `Button` 不引入这些模块的开销。

```cpp
#include <pxa/app.hpp>
#include <pxa/ui_widgets.hpp>
#include <pxa/ui_refresh.hpp>
using namespace pxa::ui;

struct Preferences {
    State<std::uint32_t> revision{0};
    bool automatic = false;
    struct ToggleAction {
        Preferences* owner;
        void operator()() {
            owner->automatic = !owner->automatic;
            owner->revision.update([](auto value) { return value + 1; });
        }
    } toggle{this};

    auto view() {
        return Refresh(revision, [this] {
            return Box(WidgetStyle{{0, 0, 296, 240}, 0, Color::background},
                Text("阅读偏好", WidgetStyle{{16, 16, 264, 40},
                     Color::text, 0, Font::title}),
                Button(automatic ? "自动翻页 · 开" : "自动翻页 · 关",
                       WidgetStyle{{16, 64, 264, 40}, Color::on_primary,
                                   Color::primary, Font::body, 12, 1})
                    .on_click_ref(toggle));
        });
    }
};
PXA_APPLICATION(Preferences)
```

示例使用固定逻辑像素。实际应用应从 DisplayMetrics 的分辨率、密度、安全边距
和形状计算 WidgetStyle；阅读器的 Controls 是对应的实际使用例。
字体角色由 Host 定义，不能将请求字号当作字形的实际行高。
原生控件目前没有任意像素字号属性，阅读器正文因此继续使用 sized Canvas text。

带样式的 Text / Button 使用借用字符串，必须存活到 render；提交后由 Host 复制。
禁止直接传入临时 std::string，避免视图存放悬空 string_view。
`on_click_ref` 明确借用回调，回调必须存活到 Page / 子树销毁。
可存放在 App，或像阅读器一样存放在 Fragment 的 View 中。
带样式控件也支持拥有闭包的 `on_click(callback)` 和 `on_pointer(callback)`，
可直接交给 Page 或 Refresh 持有；临时调用 render 后立即销毁的自定义 builder
应使用借用回调。通用流式修饰、Computed、Component、SafeArea 与 Action 见
[声明式 UI 开发指南](DECLARATIVE_UI.zh-CN.md)。

Refresh 用两个内联 Page 保存活动树和候选树。构建或提交失败会撤销候选，活动
回调继续有效；提交成功后释放旧 Page。不会为 Page 分配堆内存，但 Host 在提交
时短暂保留两个控件树，需要根据各页面最大节点数核对峰值。
共用同一个 Ref 的旧 / 新子树不可同时挂载，应将输入框或 Canvas 放在刷新区域外。
只更新输入内容的 State 会走现有属性绑定，不必增加 revision 重建菜单。

`ui_widgets_test.cpp` 验证零 Guest 堆分配、完整中文按钮文字、失败构建 / 失败提交
保留旧回调，以及提交后拒绝旧版本事件；该路径也通过 ASan / UBSan。

带样式的 `Text(State<std::string>&, WidgetStyle)` 也支持属性绑定。下载进度、状态
提示等频繁变化的内容应更新 State，保持按钮节点不变，避免手指按下与抬起之间
重建控件而丢失点击。`Button(...).on_pointer_ref(callback)` 接收标准 CanvasPointer，
可在原生控件上区分轻点、拖动取消及长按；它只订阅指针，抬起时不会再触发点击。
`ui_theme.hpp` 可选解码启动时的系统明暗配色，以及 UiAppearance 主题变更通知。

WidgetStyle 的 foreground / background 可以使用 `Color::text`、`Color::primary`、
`Color::on_primary`、`Color::background` 等系统颜色角色，或显式 RGBA。
系统角色由 Host 解析，切换系统主色或明暗后自动生效，无需复制调色板或重建控件。
`co_await context.theme().get()` 读取包含 10 个 RGBA 和 6 个字号的 UiAppearance；
Canvas 等需要直接颜色的可选绘制路径可使用该快照，并处理 UiAppearance 通知。
启动环境只含明暗信息，不包含完整调色板，不能用固定绿色代替系统主色。

同步 Flash 写入等长操作应分成有界块，在块间使用
`co_await context.clock().yield()` 让 Host 事件循环处理输入和调度。
它利用已有 Clock 接口完成一次异步往返，不引入计时器、堆分配或额外的 Task
协程池槽位；不是定时延迟，也不能打断单次已经开始的同步 I/O。
`clock_test.cpp` 覆盖成功、错误、协议长度、取消和仅占用调用者一个池槽位。

Host 的 `surface` 可能与 `background` 相同。按钮、卡片和输入区需要可辨认的层次时，
应使用 `Color::surface_container` / `surface_container_high`，搭配 `Color::text`；
主操作使用 `primary` / `on_primary`，柔和强调使用 `primary_container` /
`on_primary_container`，危险确认使用 `error_container` / `on_error_container`。
SDK Color 枚举与 Host 现有的 32 个角色一一对应；UiAppearance 线协议的颜色快照仍是
前 10 个基础角色，扩展角色交给原生控件的 Host 解析，不扩大通知载荷。
WidgetStyle 的可选尾字段 `border_width` / `border` 可指定主题边框，例如
`1, Color::outline_variant`。这些字段没有增加普通控件或应用的默认存储。
