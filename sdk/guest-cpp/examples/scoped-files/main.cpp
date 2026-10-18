#include <pxa/app.hpp>
#include <pxa/fs.hpp>
#include <pxa/permission.hpp>
#include <pxa/log.hpp>

using namespace pxa::ui;
using namespace pxa::ui::literals;
struct ScopedFiles {
    State<std::string> status{"Starting"};
    pxa::Context *context=nullptr;
    std::optional<pxa::Permission> permission;
    bool busy=false;
    std::array<std::byte,257> buffer{};
    pxa::Task<void> fail(std::string text) {
        status.set(text); busy=false;
        (void)context->log().write(pxa::LogLevel::error,text);
        co_return pxa::Result<void>{};
    }
    pxa::Task<void> run() {
#ifdef PXA_FIXED_APP_MEDIA
        status.set("Opening app data folder");
        std::optional<pxa::Directory> root;
        auto media=co_await context->fs().app_media();
        if(!media) { co_await fail("SD TEST fixed root failed"); co_return pxa::Result<void>{}; }
        root.emplace(std::move(*media));
#else
        status.set("Requesting read permission");
        if(!permission) {
            auto acquired=co_await context->permissions().acquire("fs.user-directory","read");
            if(!acquired) { co_await fail("SD TEST permission failed"); co_return pxa::Result<void>{}; }
            permission.emplace(std::move(*acquired));
        }
        std::array<std::byte,16> id{};
        std::optional<pxa::Directory> root;
        auto saved=co_await context->fs().open("sd-grant.bin");
        if(saved) {
            auto n=saved->read(id);
            if(n && *n==id.size()) {
                auto opened=co_await context->fs().open_grant(*permission,id);
                if(opened) {
                    root.emplace(std::move(*opened));
                    (void)context->log().write(pxa::LogLevel::info,"SD TEST persistent grant reopened");
                }
            }
        }
        if(!root) {
            status.set("Select the PXA-tests folder");
            auto grant=co_await context->fs().request_directory(*permission,
                "Choose PXA-tests containing fixture.bin",true);
            if(!grant) { co_await fail("SD TEST selection failed"); co_return pxa::Result<void>{}; }
            id=grant->id; root.emplace(std::move(grant->root));
            auto save=co_await context->fs().open("sd-grant.bin",
                pxa::OpenMode::write|pxa::OpenMode::create|pxa::OpenMode::truncate);
            if(save) (void)save->write(id);
        }
#endif
        auto info=co_await root->info();
        if(!info || info->rights!=3 || info->naming!=2 || info->state!=2) {
            co_await fail("SD TEST root metadata failed"); co_return pxa::Result<void>{};
        }
        auto file=co_await root->open("fixture.bin");
        if(!file) { co_await fail("SD TEST open fixture failed"); co_return pxa::Result<void>{}; }
        std::uint32_t offset=0;
        while(offset<8192) {
            auto n=file->read(std::span{buffer}.first(std::min<std::size_t>(buffer.size(),8192-offset)));
            if(!n || !*n) { co_await fail("SD TEST short read failed"); co_return pxa::Result<void>{}; }
            for(std::uint32_t i=0;i<*n;++i)
                if(std::to_integer<unsigned>(buffer[i])!=(((offset+i)*37+11)&255)) {
                    co_await fail("SD TEST content mismatch"); co_return pxa::Result<void>{};
                }
            offset+=*n;
        }
        auto eof=file->read(buffer);
        auto seek=co_await file->seek(4091);
        auto n=file->read(std::span{buffer}.first(16));
        bool good=eof && !*eof && seek && *seek==4091 && n && *n==16;
        for(unsigned i=0;good && i<16;++i)
            good=std::to_integer<unsigned>(buffer[i])==(((4091+i)*37+11)&255);
        if(!good) { co_await fail("SD TEST seek or EOF failed"); co_return pxa::Result<void>{}; }
        auto denied=co_await root->open("fixture.bin",pxa::OpenMode::write);
        auto escape=co_await root->open("../outside.bin");
        if(denied || denied.error()!=pxa::Error::denied || escape ||
            escape.error()!=pxa::Error::invalid_argument) {
            co_await fail("SD TEST isolation failed"); co_return pxa::Result<void>{};
        }
        (void)root->close();
        seek=co_await file->seek(0);
        if(!seek) { co_await fail("SD TEST child lifetime failed"); co_return pxa::Result<void>{}; }
        auto stale=file->handle(); (void)file->close();
        auto old=context->transport().io(stale,1,buffer);
        if(old || old.error()!=pxa::Error::not_found) {
            co_await fail("SD TEST stale handle failed"); co_return pxa::Result<void>{};
        }
        status.set("PASS: 8192 bytes, seek, EOF, isolation"); busy=false;
        (void)context->log().write(pxa::LogLevel::info,"SD TEST PASS bytes=8192 seek=4091 eof=0 readonly=1 isolation=1 child_lifetime=1 stale=1");
        co_return pxa::Result<void>{};
    }
    pxa::Result<void> on_start(pxa::Context& c) {
        context=&c; busy=true; return c.tasks().start(run());
    }
    auto view() {
        return Column(Text("SD Files Test").font(Font::title),Text(status),
            Button("Run again").on_click([this]{
                if(busy) return; busy=true;
                auto result=context->tasks().start(run());
                if(!result) { busy=false; status.set("Task allocation failed"); }
            })).gap(8_dp).padding(12_dp).fill();
    }
};
PXA_APPLICATION(ScopedFiles)
