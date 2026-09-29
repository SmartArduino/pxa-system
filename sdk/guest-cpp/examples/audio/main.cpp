#include <pxa/app.hpp>

#include <optional>

using namespace pxa::ui;
using namespace pxa::ui::literals;

struct AudioApp {
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
        session.emplace(std::move(*opened));
        auto committed = co_await session->graph(0);
        if (!committed) {
            status.set("Graph failed");
            co_return std::unexpected(committed.error());
        }
        status.set("Ready");
        co_return pxa::Result<void>{};
    }

    pxa::Result<void> on_start(pxa::Context& context) {
        return context.tasks().start(initialize(context));
    }

    auto view() {
        return Column(
            Text("Audio").font(Font::title),
            Text(status),
            Button("Play tone").on_click([this] {
                if (!session) return;
                auto played = session->tone({.frequency_hz = 660,
                                             .duration_ms = 200});
                status.set(played ? "Tone accepted" : "Tone failed");
            })
        ).gap(12_dp).padding(16_dp);
    }
};

PXA_APPLICATION(AudioApp)
