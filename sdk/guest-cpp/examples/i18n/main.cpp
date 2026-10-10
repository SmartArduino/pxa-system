#include <pxa/app.hpp>
#include <pxa/events.hpp>
#include <pxa/i18n_ui.hpp>
#include "pxa_app_messages.hpp"

namespace messages = pxa::messages::app_pxa_cpp_i18n;
using namespace pxa::ui;
using namespace pxa::ui::literals;

struct Internationalization {
    pxa::i18n::Translator strings{messages::bundle};
    State<std::string_view> title{std::string_view{}}, add{std::string_view{}}, language{std::string_view{}};
#if PXA_I18N_DYNAMIC_TEXT
    State<std::string> count_text{std::string{}};
#else
    State<pxa::i18n::TextBuffer<96>> count_text{pxa::i18n::TextBuffer<96>{}};
#endif
    std::uint32_t count = 1;

    pxa::Result<void> refresh() {
#if PXA_I18N_DYNAMIC_TEXT
        auto next = strings.format(messages::args::items{.count=count});
        if (!next) return std::unexpected(next.error());
#else
        pxa::i18n::TextBuffer<96> next;
        auto formatted = next.format(strings, messages::args::items{.count=count});
        if (!formatted) return formatted;
#endif
        title.set(strings.text(messages::title));
        add.set(strings.text(messages::add));
        language.set(strings.text(messages::language));
#if PXA_I18N_DYNAMIC_TEXT
        count_text.set(std::move(*next));
#else
        count_text.set(next);
#endif
        return {};
    }
    pxa::Result<void> on_start(pxa::Context& context, std::span<const std::byte> config) {
        auto changed = strings.initialize(config);
        if (!changed) return std::unexpected(changed.error());
        auto result = refresh();
        if (result) (void)context.log().write(pxa::LogLevel::info, strings.locale().tag());
        return result;
    }
    pxa::Result<bool> on_event(pxa::Context& context, const pxa::Event& event) {
        if (!event.is<pxa::SystemEnvironment>()) return false;
        auto changed = strings.update(event);
        if (!changed) return std::unexpected(changed.error());
        if (*changed) {
            auto refreshed = refresh();
            if (!refreshed) return std::unexpected(refreshed.error());
            (void)context.log().write(pxa::LogLevel::info, strings.locale().tag());
        }
        return true;
    }
    auto view() {
        return Column(Text(title).font(Font::title), Text(count_text).font(Font::headline),
            Button(add).on_click([this] {
                if (count < UINT32_MAX) ++count;
                (void)refresh();
            }), Button(language).on_click([this] {
                const auto resolved = strings.resolved_locale(messages::title);
                const auto next = resolved == "en" ? "zh-CN" : resolved == "zh-CN" ? "zh-TW" :
                    resolved == "zh-Hant" ? "ru" : "en";
                if (strings.set_locale(next)) (void)refresh();
            })).gap(8_dp).padding(24_dp);
    }
};
PXA_APPLICATION(Internationalization)
