#include <pxa/app.hpp>

#include <array>
#include <optional>
#include <string_view>

using namespace pxa::ui;
using namespace pxa::ui::literals;

struct AudioApp {
    static constexpr auto notification = pxa::EffectTrack::track0;
    static constexpr std::array chime{
        pxa::Tone{.frequency_hz = 660, .duration_ms = 500, .release_ms = 150},
        pxa::Tone{.frequency_hz = 880, .duration_ms = 400, .release_ms = 150, .delay_ms = 100},
    };
    State<std::string> status{"Opening"};
    std::optional<pxa::Permission> permission;
    std::optional<pxa::AudioSession> session;

    pxa::Task<void> initialize(pxa::Context& context) {
        auto grant = co_await context.permissions().acquire(
            "audio.playback", "media");
        if (!grant) {
            status.set("Permission denied");
            co_return std::unexpected(grant.error());
        }
        permission.emplace(std::move(*grant));
        auto opened = co_await context.audio().open(*permission);
        if (!opened) {
            status.set("Open failed");
            co_return std::unexpected(opened.error());
        }
        auto committed = co_await opened->graph(0);
        if (!committed) {
            status.set("Graph failed");
            co_return std::unexpected(committed.error());
        }
        session.emplace(std::move(*opened));
        status.set("Ready");
        co_return pxa::Result<void>{};
    }

    pxa::Result<void> on_start(pxa::Context& context) {
        return context.tasks().start(initialize(context));
    }

    void show_result(pxa::Result<void> result, std::string_view accepted) {
        status.set(result ? std::string(accepted) :
            result.error() == pxa::Error::would_block ? "Audio busy" : "Audio failed");
    }

    auto view() {
        return Column(
            Text("Audio").font(Font::title),
            Text(status),
            Row(
                Button("One-shot").on_click([this] {
                    if (session) show_result(session->effect(chime), "One-shot accepted");
                }),
                Button("Replace").on_click([this] {
                    if (session) show_result(session->effect(chime, notification), "Track replaced");
                })
            ).gap(8_dp),
            Row(
                Button("Stop track").on_click([this] {
                    if (session) show_result(session->stop_effect(notification), "Track stopped");
                }),
                Button("Play tone").on_click([this] {
                    if (session) show_result(session->tone(), "Tone accepted");
                })
            ).gap(8_dp)
        ).gap(12_dp).padding(16_dp);
    }
};

PXA_APPLICATION(AudioApp)
