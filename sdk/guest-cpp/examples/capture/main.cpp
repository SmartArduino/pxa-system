// SPDX-License-Identifier: MIT
#include <pxa/app.hpp>
#include <pxa/ui_environment.hpp>
#include <pxa/ui_layout.hpp>
#include <chrono>
using namespace pxa::ui;
using namespace pxa::ui::literals;
struct CaptureApp {
    Environment<> environment;
    State<std::string> status{"Start records 30 local Opus packets"};
    pxa::Context* context = nullptr;
    pxa::Permission permission;
    pxa::AudioCapture capture;
    std::array<std::byte, 1275> packet{};
    std::array<std::byte, 65536> recording{};
    size_t used = 8;
    unsigned count = 0;
    bool busy = false;
    std::chrono::steady_clock::time_point deadline;
    void fail(std::string_view message, pxa::Error error) {
        (void)capture.close(); permission.reset(); busy = false;
        auto text = std::string(message) + " (" + std::to_string(static_cast<int>(error)) + ")";
        status.set(text); (void)context->log().write(pxa::LogLevel::warning, text);
    }
    pxa::Task<void> start() {
        auto clock = co_await context->clock().configure(16000);
        if (!clock) { fail("Clock", clock.error()); co_return pxa::Result<void>{}; }
        auto grant = co_await context->permissions().acquire("audio.capture", "microphone");
        if (!grant) { fail("Permission", grant.error()); co_return pxa::Result<void>{}; }
        permission = std::move(*grant);
        auto opened = co_await context->capture().open(permission);
        if (!opened) { fail("Capture open", opened.error()); co_return pxa::Result<void>{}; }
        capture = std::move(*opened); used = 8; count = 0;
        recording[0]=std::byte{'P'}; recording[1]=std::byte{'X'};
        recording[2]=std::byte{'A'}; recording[3]=std::byte{'C'};
        pxa::wire::put16(recording.data()+4,capture.frame_ms());
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        status.set("Recording..."); co_return pxa::Result<void>{};
    }
    pxa::Task<void> save() {
        auto file = co_await context->fs().open("capture.tmp", pxa::OpenMode::write | pxa::OpenMode::create | pxa::OpenMode::truncate);
        if (!file) { fail("File open", file.error()); co_return pxa::Result<void>{}; }
        size_t offset=0;
        while (offset<used) {
            auto n=file->write(std::span(recording).subspan(offset,std::min(size_t(4096),used-offset)));
            if (n) {
                if (!*n || *n>used-offset) { fail("File write",pxa::Error::protocol_error); co_return pxa::Result<void>{}; }
                offset+=*n;
            } else if(n.error()!=pxa::Error::would_block) { fail("File write",n.error()); co_return pxa::Result<void>{}; }
            if(std::chrono::steady_clock::now()>deadline) { fail("File deadline",pxa::Error::timed_out); co_return pxa::Result<void>{}; }
            auto yielded=co_await context->clock().yield();
            if(!yielded) { fail("Interrupted",yielded.error()); co_return pxa::Result<void>{}; }
        }
        auto closed=file->close();
        if(!closed) { fail("File close",closed.error()); co_return pxa::Result<void>{}; }
        auto renamed=co_await context->fs().rename("capture.tmp","capture.pxac");
        if(!renamed) { fail("File commit",renamed.error()); co_return pxa::Result<void>{}; }
        busy=false; status.set("Saved 30 packets in capture.pxac");
        (void)context->log().write(pxa::LogLevel::info,"Microphone saved 30 Opus packets bytes="+std::to_string(used));
        co_return pxa::Result<void>{};
    }
    void pump() {
        if(!capture) return;
        if(std::chrono::steady_clock::now()>deadline) { fail("Capture deadline",pxa::Error::timed_out); return; }
        auto n=capture.read(packet);
        if(!n) { if(n.error()!=pxa::Error::would_block)fail("Capture read",n.error()); return; }
        if(!*n || *n>packet.size() || *n+2>recording.size()-used) { fail("Packet size",pxa::Error::protocol_error); return; }
        pxa::wire::put16(recording.data()+used,static_cast<uint16_t>(*n));used+=2;
        std::copy_n(packet.begin(),*n,recording.begin()+used);used+=*n;
        if(++count==30) {
            (void)capture.close();permission.reset();
            pxa::wire::put16(recording.data()+6,30);
            deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
            status.set("Saving...");
            if(!context->foreground_tasks().start(save()))fail("Task limit",pxa::Error::resource_limit);
        }
    }
    pxa::Result<void> on_start(pxa::Context& ctx,std::span<const std::byte> config) { context=&ctx;return environment.initialize(config); }
    pxa::Result<bool> on_event(pxa::Context&,const pxa::Event& event) { auto result=environment.update(event);pump();return result; }
    void on_background(pxa::Context&) { (void)capture.close();permission.reset();busy=false;status.set("Paused; microphone released"); }
    void on_stop(pxa::StopReason) { (void)capture.close();permission.reset(); }
    auto view() { return SafeArea(environment.display(),Scroll(Column(Text("Microphone").font(Font::caption),
        Button("Start recording").on_click([this]{if(!busy){busy=true;status.set("Opening...");if(!context->foreground_tasks().start(start()))fail("Task limit",pxa::Error::resource_limit);}}),
        Text(status).font(Font::caption).fill(),
        Text("Mono 16 kHz Opus. Local files only.").font(Font::caption).fill()).gap(8_dp).padding(8_dp).fill()).fill()); }
};
PXA_APPLICATION(CaptureApp)
