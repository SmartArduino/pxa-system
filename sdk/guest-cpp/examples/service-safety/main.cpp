#include <pxa/app.hpp>
#include <pxa/binary.hpp>
#include <pxa/ui_layout.hpp>

using namespace pxa;
using namespace pxa::ui;
using namespace pxa::ui::literals;

struct Safety {
    State<std::string> status{"Checking"};
    State<std::string> target{"Runtime"};
    Context* context = nullptr;
    bool busy = true;
    const char* stage = "runtime";
    State<DisplayMetrics> display{DisplayMetrics{}};

    Task<void> check() {
        stage = "runtime";
        auto info = co_await context->device().runtime_info();
        if (!info) co_return std::unexpected(info.error());
        target.set(std::string(info->target.view()));
        stage = "clock";
        if (auto yielded = co_await context->clock().yield(); !yielded)
            co_return std::unexpected(yielded.error());
        stage = "file";
        auto opened = co_await context->fs().open("round-trip.bin",
            OpenMode::read | OpenMode::write | OpenMode::create | OpenMode::truncate);
        if (!opened) co_return std::unexpected(opened.error());
        constexpr auto encoded = binary::encode<std::uint64_t>(0x0123456789abcdef);
        auto written = opened->write(encoded);
        if (!written || *written != encoded.size()) co_return std::unexpected(Error::io_error);
        auto seek = opened->seek(0);
        File file(std::move(*opened));
        auto position = co_await std::move(seek);
        if (!position || *position) co_return std::unexpected(Error::protocol_error);
        std::array<std::byte, 8> read{};
        auto count = file.read(read);
        if (!count || *count != read.size() || read != encoded)
            co_return std::unexpected(Error::protocol_error);
        if (auto closed = file.close(); !closed) co_return std::unexpected(closed.error());
        if (auto removed = co_await context->fs().remove("round-trip.bin"); !removed)
            co_return std::unexpected(removed.error());

        stage = "surface";
        auto created = co_await context->surface().create_mapped(
            {.width = 8, .height = 4, .buffers = 2, .max_buffer_bytes = 128});
        if (!created) co_return std::unexpected(created.error());
        auto configure = created->configure({.width = 8, .height = 4, .visible = false});
        Surface surface(std::move(*created));
        if (auto result = co_await std::move(configure); !result)
            co_return std::unexpected(result.error());
        auto query = surface.query_state();
        Surface moved(std::move(surface));
        if (auto result = co_await std::move(query); !result)
            co_return std::unexpected(result.error());
        // reset is supported by both the baseline and the updated SDK.
        moved.reset();

        stage = "renderer";
        auto renderer = co_await context->game().create(
            {.width = 32, .height = 32, .buffers = 2, .max_draw_bytes = 256});
        if (!renderer) co_return std::unexpected(renderer.error());
        game::DrawBuffer<256> buffer;
        auto frame = renderer->frame(buffer);
        frame.clear({0x4a49});
        if (auto submitted = frame.submit(); !submitted) co_return std::unexpected(submitted.error());
        auto duplicate = frame.submit();
        if (duplicate || duplicate.error() != Error::bad_state)
            co_return std::unexpected(Error::protocol_error);
        status.set("Core checks OK"); busy = false;
        co_return Result<void>{};
    }

    Task<void> audio() {
        stage = "audio";
        auto grant = co_await context->permissions().acquire("audio.playback", "media");
        if (!grant) co_return std::unexpected(grant.error());
        auto open = context->audio().open(*grant);
        Permission permission(std::move(*grant));
        auto opened = co_await std::move(open);
        if (!opened) co_return std::unexpected(opened.error());
        std::array bands{EqBand{1000, -256, 256}};
        auto graph = opened->graph(-24 * 256, bands);
        bands[0].frequency_hz = 0; // Snapshot must already own the valid value.
        AudioSession session(std::move(*opened));
        if (auto applied = co_await std::move(graph); !applied)
            co_return std::unexpected(applied.error());
        if (auto tone = session.tone({.frequency_hz = 660, .duration_ms = 50,
                                     .gain_db_q8 = -36 * 256}); !tone)
            co_return std::unexpected(tone.error());
        auto query = session.query();
        AudioSession moved(std::move(session));
        if (auto state = co_await std::move(query); !state)
            co_return std::unexpected(state.error());
        auto flush = moved.flush();
        AudioSession last(std::move(moved));
        if (auto flushed = co_await std::move(flush); !flushed)
            co_return std::unexpected(flushed.error());
        if (auto closed = last.close(); !closed) co_return std::unexpected(closed.error());
        status.set("Audio checks OK"); busy = false;
        co_return Result<void>{};
    }
    void launch(bool sound) {
        if (busy) return;
        busy = true; status.set("Checking");
        if (auto started = context->tasks().start(sound ? audio() : check()); !started)
            on_error(*context, started.error());
    }
    Result<void> on_start(Context& ctx, std::span<const std::byte> config) {
        context = &ctx;
        auto metrics = decode_start_display(config);
        if (!metrics) return std::unexpected(metrics.error());
        display.set(*metrics);
        return ctx.tasks().start(check());
    }
    void on_error(Context&, Error error) {
        busy = false; status.set("Error " + std::to_string(static_cast<int>(error)) + " at " + stage);
    }
    auto view() {
        return SafeArea(display, Column(Text("Service safety").font(Font::title), Text(target), Text(status),
            Button("Repeat checks").on_click([this] { launch(false); }),
            Button("Test audio").on_click([this] { launch(true); })
        ).gap(8_dp).padding(12_dp));
    }
};
PXA_APPLICATION(Safety)
