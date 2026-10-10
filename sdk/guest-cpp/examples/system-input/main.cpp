#include <pxa/app.hpp>

using namespace pxa::ui;
using namespace pxa::ui::literals;

struct SystemInput {
    State<std::string> text{""};
    State<std::string> status{"点输入框，或按打开"};
    TextInputRef editor;
    pxa::Context* context = nullptr;
    void report(pxa::Result<void> result, const char* message) {
        status.set(result ? message : "Host 不支持或输入框不可见");
        (void)context->log().write(pxa::LogLevel::info,
            result ? message : "IME control failed");
    }
    auto view() {
        return Column(
            Text("系统输入法").font(Font::title),
            TextInput(text, editor).max_bytes(2048).single_line().on_submit([this] {
                report(editor.hide_keyboard(), "IME submitted");
                (void)context->log().write(pxa::LogLevel::info, text.get());
            }),
            Row(Button("打开").on_click([this] {
                    report(editor.show_keyboard(), "IME opened");
                }),
                Button("关闭").on_click([this] {
                    report(editor.hide_keyboard(), "IME closed");
                })).gap(8_dp),
            Text(status)
        ).gap(4_dp).padding(12_dp);
    }
    pxa::Result<void> on_start(pxa::Context& ctx, std::span<const std::byte>) {
        context = &ctx;
        return {};
    }
    void on_background(pxa::Context&) { (void)editor.hide_keyboard(); }
};

PXA_APPLICATION(SystemInput)
