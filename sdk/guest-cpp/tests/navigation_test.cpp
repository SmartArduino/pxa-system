#include <pxa/app.hpp>

#include <cassert>
#include <cstdlib>
#include <new>

static unsigned allocations;
void* operator new(std::size_t size) {
    ++allocations;
    if (void* pointer = std::malloc(size)) return pointer;
    std::abort();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

static bool fail_commit;
static std::uint32_t host_generation;
static std::uint32_t transaction_generation;
static unsigned submissions;
static unsigned cancelled;
static unsigned started;
static unsigned destroyed;
static std::uint64_t token;

extern "C" std::int32_t pxa_submit(const std::uint8_t* bytes,
                                      std::uint32_t size) {
    assert(size >= 20);
    ++submissions;
    const auto* p = reinterpret_cast<const std::byte*>(bytes);
    const auto service = pxa::wire::get16(p);
    const auto opcode = pxa::wire::get16(p + 2);
    if (service == 3) {
        if (opcode == 1) transaction_generation = pxa::wire::get32(p + 24);
        if (opcode == 3) {
            if (fail_commit) return static_cast<std::int32_t>(pxa::Error::resource_limit);
            assert(transaction_generation > host_generation);
            host_generation = transaction_generation;
        }
    } else if (service == 4) {
        assert(opcode == 2);
        token = pxa::wire::get64(p + 4);
    } else {
        assert(service == 1 && opcode == 1);
        ++cancelled;
    }
    return 0;
}

extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t,
                                  std::uint8_t*, std::uint32_t) { return -3; }

pxa::Task<void> wait(pxa::Transport& transport, pxa::RequestTable& requests) {
    struct Guard { ~Guard() { ++destroyed; } } guard;
    ++started;
    auto response = co_await pxa::Response(transport, requests, 4, 2,
                                          std::span<const std::byte>{});
    (void)response;
    assert(false);
    co_return pxa::Result<void>{};
}

using Navigation = pxa::ui::Navigator<2>;
struct Model {
    pxa::ui::State<int> count{0};
    Navigation& navigation;
    pxa::Transport& transport;
    pxa::RequestTable& requests;
};

struct Details {
    Model& model;
    pxa::ui::State<int> local{7};
    explicit Details(Model& value) : model(value) {}
    auto view() {
        using namespace pxa::ui;
        return Column(Text(local), Button("Back").on_click([this] {
            assert(model.navigation.pop());
        }));
    }
};

struct Home {
    Model& model;
    explicit Home(Model& value) : model(value) {}
    auto view() {
        using namespace pxa::ui;
        return Column(Text(model.count), Button("Details").on_click([this] {
            assert(model.navigation.push<Details>(std::ref(model)));
        }));
    }
    void on_mount(pxa::TaskScope& tasks) {
        assert(tasks.start(wait(model.transport, model.requests)));
    }
};

static pxa::Event click(std::uint32_t generation, std::span<std::byte> payload) {
    pxa::wire::put32(payload.data(), 1);
    pxa::wire::put32(payload.data() + 4, 4);
    pxa::wire::put32(payload.data() + 8, generation);
    pxa::wire::put16(payload.data() + 12, 1);
    return {3, 0x8001, 0, payload};
}

static Navigation* runtime_navigation;
static unsigned ui_errors;
struct RuntimeDetails {
    auto view() { return pxa::ui::Text("Details"); }
};
struct RuntimeHome {
    Navigation& navigation;
    auto view() {
        using namespace pxa::ui;
        return Column(Text("Home"), Button("Details").on_click([this] {
            assert(navigation.push<RuntimeDetails>());
        }));
    }
};
struct RuntimeApp {
    Navigation routes;
    Navigation& navigation() { return routes; }
    pxa::Result<void> on_start(pxa::Context&) {
        runtime_navigation = &routes;
        return routes.push<RuntimeHome>(std::ref(routes));
    }
    void on_error(pxa::Context&, pxa::Error error) {
        assert(error == pxa::Error::resource_limit);
        ++ui_errors;
    }
};

static std::int32_t runtime_click(std::uint32_t generation) {
    std::array<std::byte, 44> packet{};
    pxa::wire::put16(packet.data(), 3);
    pxa::wire::put16(packet.data() + 2, 0x8001);
    pxa::wire::put32(packet.data() + 12, 24);
    (void)click(generation, std::span(packet).subspan(20));
    return pxa::AppRuntime<RuntimeApp>::event(
        reinterpret_cast<const std::uint8_t*>(packet.data()), packet.size());
}

int main() {
    pxa::Transport transport;
    transport.phase(pxa::Phase::event);
    pxa::RequestTable requests;
    Navigation navigation;
    Model model{pxa::ui::State<int>{0}, navigation, transport, requests};
    assert(navigation.push<Home>(std::ref(model)));
    assert(navigation.attach(transport));
    assert(navigation.depth() == 1 && started == 1);
    const auto home_generation = navigation.generation();
    const auto home_task_token = token;
    std::array<std::byte, 24> payload{};

    const auto before_idle = submissions;
    assert(navigation.flush());
    assert(submissions == before_idle);
    const auto baseline = allocations;
    fail_commit = true;
    assert(navigation.handle(click(home_generation, payload)));
    auto failed = navigation.flush();
    assert(!failed && failed.error() == pxa::Error::resource_limit);
    assert(navigation.depth() == 1 && destroyed == 0 && cancelled == 0);
    assert(navigation.generation() == home_generation);

    fail_commit = false;
    model.count.set(3);
    assert(navigation.flush());
    assert(navigation.generation() > home_generation);
    assert(!navigation.handle(click(home_generation, payload)));
    const auto updated_generation = navigation.generation();
    assert(navigation.handle(click(updated_generation, payload)));
    assert(navigation.flush());
    assert(navigation.depth() == 2 && destroyed == 1 && cancelled == 1);
    const auto details_generation = navigation.generation();
    assert(!navigation.handle(click(updated_generation, payload)));
    std::array<std::byte, 12> late{};
    assert(requests.dispatch({4, 2, home_task_token, late}));
    assert(!navigation.push<Home>(std::ref(model)));

    assert(navigation.handle(click(details_generation, payload)));
    assert(navigation.flush());
    assert(navigation.depth() == 1 && started == 2);
    assert(navigation.generation() > details_generation);
    assert(!navigation.pop());
    assert(allocations == baseline);
    navigation.reset();
    assert(destroyed == 2 && cancelled == 2);

    host_generation = 0;
    assert(pxa::AppRuntime<RuntimeApp>::start(nullptr, 0) == 0);
    const auto initial_generation = runtime_navigation->generation();
    fail_commit = true;
    assert(runtime_click(initial_generation) == 1);
    assert(ui_errors == 1 && runtime_navigation->depth() == 1);
    assert(runtime_navigation->generation() == initial_generation);
    fail_commit = false;
    assert(runtime_click(initial_generation) == 1);
    assert(runtime_navigation->depth() == 2);
    pxa::AppRuntime<RuntimeApp>::stop(0);
}
