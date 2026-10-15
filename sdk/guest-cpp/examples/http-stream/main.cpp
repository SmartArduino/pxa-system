#include <pxa/app.hpp>
#include <pxa/http.hpp>
#include <pxa/log.hpp>
#include <pxa/fs.hpp>
#include <pxa/ui_layout.hpp>
#include <pxa/ui_environment.hpp>
#include <cstdio>
#include <ctime>

namespace {
constexpr std::string_view fixture = PXA_HTTP_FIXTURE_URL;
// Keep both baseline paths in either package: identical code mapping and
// module metadata, with only this selected initial value changing.
volatile unsigned baseline_mode = PXA_HTTP_BASELINE;
std::uint64_t now_ms() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<std::uint64_t>(t.tv_sec) * 1000 + t.tv_nsec / 1000000;
}
struct App {
    pxa::ui::Environment<> environment;
    pxa::ui::State<std::string> status{"Starting"};
    std::array<std::byte, 2048> buffer;
    char line[256], url[512];
    std::array<std::byte, 128> packet;
    pxa::Permission permission;
    pxa::File result_file;
    auto view() {
        using namespace pxa::ui;
        using namespace pxa::ui::literals;
        return SafeArea(environment.display(),
                        Column(Text("HTTP stream").font(Font::headline), Text(status))
                            .gap(8_dp)
                            .padding(8_dp));
    }
    void report(pxa::Context &context, std::string_view message) {
        (void)context.log().write(pxa::LogLevel::info, message);
        if (result_file) {
            auto bytes = std::as_bytes(std::span{message.data(), message.size()});
            while (!bytes.empty()) {
                auto n = result_file.write(bytes);
                if (!n || !*n)
                    break;
                bytes = bytes.subspan(*n);
            }
            const std::byte newline{10};
            (void)result_file.write(std::span{&newline, 1});
        }
        status.set(std::string(message));
    }
    pxa::Task<void> baseline(pxa::Context &context) {
        std::snprintf(url, sizeof(url), "%.*s/baseline",
                      static_cast<int>(fixture.size()), fixture.data());
        const auto start = now_ms();
        std::size_t total = 0, reads = 0, partial = 0;
        std::uint64_t first = 0, header = 0;
        const unsigned mode = baseline_mode;
        if (mode == 1) {
            auto response = co_await context.net().request(
                {.url = url, .max_response_bytes = 262144, .timeout_ms = 60000},
                permission, packet);
            if (!response)
                co_return std::unexpected(response.error());
            header = now_ms() - start;
            for (;;) {
                auto n = response->body.read(buffer);
                if (!n)
                    co_return std::unexpected(n.error());
                if (!*n)
                    break;
                if (!first)
                    first = now_ms() - start;
                for (std::size_t i = 0; i < *n; ++i)
                    if (buffer[i] != std::byte((total + i) * 31u + 7u))
                        co_return std::unexpected(pxa::Error::protocol_error);
                total += *n; ++reads; if (*n < buffer.size()) ++partial;
            }
        } else {
            auto response = co_await context.http().get(
                url, permission, {.max_body_bytes = 262144, .timeout_ms = 60000});
            if (!response)
                co_return std::unexpected(response.error());
            header = now_ms() - start;
            for (;;) {
                auto n = co_await response->body.read_some(buffer);
                if (!n)
                    co_return std::unexpected(n.error());
                if (!*n)
                    break;
                if (!first)
                    first = now_ms() - start;
                for (std::size_t i = 0; i < *n; ++i)
                    if (buffer[i] != std::byte((total + i) * 31u + 7u))
                        co_return std::unexpected(pxa::Error::protocol_error);
                total += *n; ++reads; if (*n < buffer.size()) ++partial;
            }
        }
        if (total != 262144)
            co_return std::unexpected(pxa::Error::protocol_error);
        std::snprintf(line, sizeof(line),
                      "HTTP_STREAM BASELINE mode=%u head_ms=%llu first_ms=%llu "
                      "total_ms=%llu bytes=%zu reads=%zu partial=%zu PASS",
                      mode, static_cast<unsigned long long>(header),
                      static_cast<unsigned long long>(first),
                      static_cast<unsigned long long>(now_ms() - start), total, reads, partial);
        report(context, line);
        co_return pxa::Result<void>{};
    }
    pxa::Task<void> run(pxa::Context &context) {
        auto output = co_await context.fs().open(
            "result.txt",
            pxa::OpenMode::write | pxa::OpenMode::create | pxa::OpenMode::truncate);
        if (!output)
            co_return std::unexpected(output.error());
        result_file = std::move(*output);
        std::snprintf(line, sizeof(line), "HTTP_STREAM START time_ms=%llu",
                      static_cast<unsigned long long>(now_ms()));
        report(context, line);
        status.set("Permission");
        auto grant = co_await context.permissions().acquire("net.client", "web");
        if (!grant)
            co_return std::unexpected(grant.error());
        permission = std::move(*grant);
        if constexpr (PXA_HTTP_BASELINE != 0) {
            if constexpr (PXA_HTTP_BASELINE == 3) {
                // First pair warms both paths. Twelve measured transfers
                // then alternate without package reloads or Wi-Fi resets.
                for (unsigned index = 0; index < 14; ++index) {
                    baseline_mode = (index & 1u) + 1u;
                    auto result = co_await baseline(context);
                    if (!result)
                        co_return std::unexpected(result.error());
                }
                report(context, "HTTP_STREAM ALL PASS");
            } else {
                auto result = co_await baseline(context);
                if (!result)
                    co_return std::unexpected(result.error());
            }
            co_return result_file.close();
        }
        status.set("Capabilities");
        auto caps = co_await context.http().capabilities();
        if (!caps || !caps->streaming_download())
            co_return std::unexpected(pxa::Error::unsupported);
        for (unsigned test = 0; test < 3; ++test) {
            status.set(test == 1   ? "Chunked download"
                       : test == 2 ? "Cancel after headers"
                                   : "Known-length download");
            std::snprintf(url, sizeof(url), "%.*s/%s", static_cast<int>(fixture.size()),
                          fixture.data(), test == 1 ? "chunked" : "large");
            auto start = now_ms();
            auto response =
                co_await context.http().get(url, permission,
                                            {.buffer_bytes = 4096,
                                             .header_bytes = 1024,
                                             .max_body_bytes = 2 * 1024 * 1024,
                                             .timeout_ms = 60000});
            if (!response)
                co_return std::unexpected(response.error());
            auto header_ms = now_ms() - start;
            unsigned cookies = 0;
            for (auto value : response->headers.values("Set-Cookie")) {
                (void)value;
                ++cookies;
            }
            if (response->status_code != 200 || cookies != 2)
                co_return std::unexpected(pxa::Error::protocol_error);
            if (test == 2) {
                (void)response->body.close();
                report(context, "HTTP_STREAM cancel-after-headers PASS");
                continue;
            }
            status.set(test == 1 ? "Receiving chunked body"
                                 : "Receiving known-length body");
            std::size_t total = 0, reads = 0, partial = 0;
            std::uint64_t first_ms = 0;
            for (;;) {
                auto n = co_await response->body.read_some(buffer);
                if (!n)
                    co_return std::unexpected(n.error());
                if (!*n)
                    break;
                if (!first_ms)
                    first_ms = now_ms() - start;
                for (std::size_t i = 0; i < *n; ++i)
                    if (buffer[i] != std::byte((total + i) * 31u + 7u))
                        co_return std::unexpected(pxa::Error::protocol_error);
                total += *n; ++reads; if (*n < buffer.size()) ++partial;
            }
            if (total != 1024 * 1024)
                co_return std::unexpected(pxa::Error::protocol_error);
            std::snprintf(line, sizeof(line),
                          "HTTP_STREAM download chunked=%u head_ms=%llu first_ms=%llu "
                          "total_ms=%llu bytes=%zu reads=%zu partial=%zu PASS",
                          test, static_cast<unsigned long long>(header_ms),
                          static_cast<unsigned long long>(first_ms),
                          static_cast<unsigned long long>(now_ms() - start), total, reads, partial);
            report(context, line);
        }
        std::snprintf(url, sizeof(url), "%.*s/upload", static_cast<int>(fixture.size()),
                      fixture.data());
        status.set("Uploading 100 KiB");
        auto exchange = co_await context.http().open({.url = url,
                                                      .method = pxa::HttpMethod::post,
                                                      .upload_bytes = 100 * 1024,
                                                      .options = {.timeout_ms = 60000}},
                                                     permission);
        if (!exchange)
            co_return std::unexpected(exchange.error());
        for (std::size_t total = 0, reads = 0, partial = 0; total < 100 * 1024;) {
            std::size_t count = 100 * 1024 - total;
            if (count > buffer.size())
                count = buffer.size();
            for (std::size_t i = 0; i < count; ++i)
                buffer[i] = std::byte((total + i) * 31u + 7u);
            auto n = co_await exchange->write_some(std::span{buffer}.first(count));
            if (!n)
                co_return std::unexpected(n.error());
            total += *n;
        }
        auto finished = exchange->finish_upload();
        if (!finished)
            co_return std::unexpected(finished.error());
        auto response = co_await std::move(*exchange).response();
        if (!response)
            co_return std::unexpected(response.error());
        std::size_t reply_bytes = 0;
        for (;;) {
            auto n = co_await response->body.read_some(
                std::span{buffer}.subspan(reply_bytes));
            if (!n)
                co_return std::unexpected(n.error());
            if (!*n)
                break;
            reply_bytes += *n;
            if (reply_bytes > 6)
                co_return std::unexpected(pxa::Error::protocol_error);
        }
        if (reply_bytes != 6 || std::memcmp(buffer.data(), "102400", 6))
            co_return std::unexpected(pxa::Error::protocol_error);
        report(context, "HTTP_STREAM upload bytes=102400 PASS");
        std::snprintf(url, sizeof(url), "%.*s/reject", static_cast<int>(fixture.size()),
                      fixture.data());
        status.set("Early upload response");
        auto reject = co_await context.http().open({.url = url,
                                                    .method = pxa::HttpMethod::post,
                                                    .upload_bytes = 1024 * 1024,
                                                    .options = {.timeout_ms = 5000}},
                                                   permission);
        if (!reject)
            co_return std::unexpected(reject.error());
        auto rejection = co_await std::move(*reject).response();
        if (!rejection || rejection->status_code != 413)
            co_return std::unexpected(pxa::Error::protocol_error);
        report(context, "HTTP_STREAM early-upload-response PASS");
        report(context, "HTTP_STREAM ALL PASS");
        auto closed = result_file.close();
        if (!closed)
            co_return std::unexpected(closed.error());
        co_return pxa::Result<void>{};
    }
    void on_error(pxa::Context &context, pxa::Error error) noexcept {
        std::snprintf(line, sizeof(line), "HTTP_STREAM FAILED error=%d",
                      static_cast<int>(error));
        status.set(std::string(line));
        (void)context.log().write(pxa::LogLevel::error, line);
        if (result_file)
            (void)result_file.write(std::as_bytes(std::span{line, std::strlen(line)}));
        (void)result_file.close();
    }
    pxa::Result<bool> on_event(pxa::Context &, const pxa::Event &event) {
        return environment.update(event);
    }
    pxa::Result<void> on_start(pxa::Context &context,
                               std::span<const std::byte> config) {
        auto initialized = environment.initialize(config);
        if (!initialized)
            return initialized;
        (void)context.log().write(pxa::LogLevel::info, "HTTP_STREAM START");
        return context.tasks().start(run(context));
    }
};
} // namespace
PXA_APPLICATION(App)
