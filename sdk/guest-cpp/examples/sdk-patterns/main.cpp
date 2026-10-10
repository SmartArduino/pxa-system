#include <pxa/app.hpp>
#include <pxa/io.hpp>
#include <pxa/ui_geometry.hpp>
#include "save_codec.hpp"

using namespace pxa;
using namespace pxa::ui;
using namespace pxa::ui::literals;

struct Patterns {
    State<int> count{0};
    State<std::string> status{"Loading"};
    Context* context=nullptr;
    bool busy=true;
    Dp padding{16};

    Task<void> load() {
        auto save=co_await context->storage().get_value<SaveCodec>("save");
        if (save) { count.set(save->count); status.set("Loaded"); }
        else if (save.error()==Error::not_found) status.set("New save");
        else co_return std::unexpected(save.error());
        busy=false; co_return Result<void>{};
    }
    Task<void> save() {
        auto saved=co_await context->storage().set_value<SaveCodec>("save",Save{count.get(),true});
        if (!saved) co_return std::unexpected(saved.error());
        status.set("Saved"); busy=false; co_return Result<void>{};
    }
    Task<void> round_trip() {
        auto file=co_await context->fs().open("transfer.bin",
            OpenMode::write|OpenMode::create|OpenMode::truncate);
        if (!file) co_return std::unexpected(file.error());
        std::array<std::byte,SaveCodec::max_bytes> bytes{};
        auto size=binary::encode<SaveCodec>(Save{count.get(),true},bytes);
        if (!size) co_return std::unexpected(size.error());
        WriteTransfer write(std::span{bytes}.first(*size));
        while (!write.complete()) {
            auto step=write.step(*file,2);
            if (!step) co_return std::unexpected(step.error());
            if (!write.complete()) {
                auto yielded=co_await context->clock().yield();
                if (!yielded) co_return std::unexpected(yielded.error());
            }
        }
        if (auto closed=file->close(); !closed) co_return std::unexpected(closed.error());
        auto input=co_await context->fs().open("transfer.bin");
        if (!input) co_return std::unexpected(input.error());
        ReadTransfer read(bytes);
        while (!read.complete()) {
            auto step=read.step(*input,2);
            if (!step) co_return std::unexpected(step.error());
            if (step->state==TransferState::end) co_return std::unexpected(Error::protocol_error);
            if (!read.complete()) {
                auto yielded=co_await context->clock().yield();
                if (!yielded) co_return std::unexpected(yielded.error());
            }
        }
        auto decoded=binary::decode<SaveCodec>(read.completed_bytes());
        if (!decoded || decoded->count!=count.get()) co_return std::unexpected(Error::protocol_error);
        if (auto closed=input->close(); !closed) co_return std::unexpected(closed.error());
        status.set("File round trip OK"); busy=false; co_return Result<void>{};
    }
    Task<void> fail() {
        auto missing=co_await context->fs().stat("missing.bin");
        co_return std::unexpected(missing ? Error::internal : missing.error());
    }
    void launch(Task<void> task) {
        auto started=context->tasks().start(std::move(task));
        if (!started) on_error(*context,started.error());
    }
    Result<void> on_start(Context& ctx,std::span<const std::byte> config) {
        context=&ctx;
        auto display=decode_start_display(config);
        if (!display) return std::unexpected(display.error());
        const auto safe=safe_rectangle(*display);
        const auto margin=std::max({safe.x,safe.y,
            int(display->width)-safe.x-safe.width,int(display->height)-safe.y-safe.height});
        padding=Dp{16+std::int32_t((std::uint64_t(margin)*65536u+
            display->density_q16-1)/display->density_q16)};
        return ctx.tasks().start(load());
    }
    void on_error(Context&,Error error) {
        status.set(error==Error::not_found ? "Task error handled" : "Operation failed");
        busy=false;
    }
    auto view() {
        return Column(
            Text("SDK patterns").font(Font::title), Text(count),
            Button("Add and save").on_click([this] {
                if (busy) return;
                busy=true; count.update([](int n) { return n+1; }); launch(save());
            }),
            Button("File round trip").on_click([this] {
                if (busy) return;
                busy=true; launch(round_trip());
            }),
            Button("Fail task").on_click([this] {
                if (busy) return;
                busy=true; launch(fail());
            }), Text(status)
        ).gap(6_dp).padding(padding);
    }
};
PXA_APPLICATION(Patterns)
