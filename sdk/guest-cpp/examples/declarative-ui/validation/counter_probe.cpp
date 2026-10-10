// Native Guest encoding probe with a byte-consuming Host stub.
// This deliberately measures neither LVGL rasterization nor display FPS.
#include <pxa/app.hpp>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
using namespace pxa;
static unsigned calls;
static volatile unsigned long long digest;
extern "C" std::int32_t pxa_submit(const std::uint8_t* p, std::uint32_t n) {
    ++calls;
    unsigned long long sum = 0;
    for (unsigned i = 0; i < n; ++i) sum += p[i];
    digest = digest + sum;
    return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) {
    return -3;
}
int main() {
    using namespace pxa::ui;
    using namespace pxa::ui::literals;
    Transport tx; tx.phase(Phase::event); State<int> count{0};
    auto page = Page(tx, Column(Text("Counter").font(Font::title), Text(count),
        Button("Add").on_click([] {})).gap(8_dp).padding(16_dp));
    assert(page.mount());
    for (int i = 1; i <= 10000; ++i) { count.set(i); assert(page.flush()); }
    std::array<double, 7> samples;
    for (auto& sample : samples) {
        auto start = std::chrono::steady_clock::now();
        for (int i = 1; i <= 100000; ++i) { count.set(i); assert(page.flush()); }
        sample = std::chrono::duration<double, std::nano>(
            std::chrono::steady_clock::now() - start).count() / 100000;
    }
    std::sort(samples.begin(), samples.end());
    auto prior = calls; assert(page.flush() && calls == prior);
    std::printf("{\"page_bytes\":%zu,\"state_bytes\":%zu,\"context_bytes\":%zu,"
        "\"scope_bytes\":%zu,\"pool_bytes\":%zu,\"median_ns\":%.3f,\"min_ns\":%.3f,"
        "\"max_ns\":%.3f,\"imports\":%u,\"digest\":%llu,\"idle_imports\":0}\n",
        sizeof(page), sizeof(count), sizeof(Context), sizeof(TaskScope),
        task_pool_stats().reserved_bytes, samples[3], samples[0], samples[6], calls, digest);
}
