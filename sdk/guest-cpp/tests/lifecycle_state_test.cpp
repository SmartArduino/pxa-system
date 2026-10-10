#include <pxa/app.hpp>
#include <pxa/events.hpp>
#include <cassert>
#include <cstdio>

using namespace pxa;
static unsigned submits, closes, reports, cancels, stops;
static int submit_result;
static int io_result;
static std::uint64_t request_token;
extern "C" std::int32_t pxa_submit(const std::uint8_t* data, std::uint32_t size) {
    const auto* p=reinterpret_cast<const std::byte*>(data); ++submits;
    if (wire::get16(p)==1) {
        assert(size==28);
        if (wire::get16(p+2)==2) ++closes;
        else ++cancels;
    } else request_token=wire::get64(p+4);
    return submit_result;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) {
    return io_result;
}

struct Mutator { int operator()(int& n) const { return ++n; } };
struct Increment { int operator()(const int& n) const { return n+1; } };
template<class S, class F> concept Updatable = requires(S& s, F f) { s.update(f); };
static_assert(!Updatable<ui::State<int>, Mutator>);
static_assert(!Updatable<ui::Ref<int>, Mutator>);
static_assert(Updatable<ui::State<int>, Increment>);

struct ErrorApp {
    static Task<void> immediate() { co_return std::unexpected(Error::io_error); }
    static Task<void> pending(Context& ctx) {
        auto result=co_await ctx.request(42,1);
        if (!result) co_return std::unexpected(result.error());
        co_return std::unexpected(Error::denied);
    }
    Result<void> on_start(Context& ctx) {
        assert(ctx.tasks().start(immediate()));
        return ctx.foreground_tasks().start(pending(ctx));
    }
    void on_error(Context& ctx, Error error) {
        assert(ctx.transport().phase()==Phase::start || ctx.transport().phase()==Phase::event);
        assert(error==Error::io_error || error==Error::denied); ++reports;
    }
    Result<bool> on_event(Context& ctx, const Event& event) {
        if (event.service!=43) return false;
        auto result=ctx.tasks().start(pending(ctx));
        if (!result) return std::unexpected(result.error());
        return true;
    }
    void on_stop(StopReason) { ++stops; }
};
static int deliver(std::uint16_t service, std::uint16_t opcode,
                   std::uint64_t token=0, std::span<const std::byte> payload={}) {
    std::array<std::byte, 24> packet{};
    wire::put16(packet.data(),service); wire::put16(packet.data()+2,opcode);
    wire::put64(packet.data()+4,token); wire::put32(packet.data()+12,payload.size());
    std::copy(payload.begin(),payload.end(),packet.begin()+20);
    return AppRuntime<ErrorApp>::event(reinterpret_cast<const std::uint8_t*>(packet.data()),20+payload.size());
}

int main() {
    {
        Transport transport; transport.phase(Phase::event);
        Resource<FileTag> resource(transport,UINT64_C(0x100000001));
        const auto original=resource.handle();
        submit_result=static_cast<int>(Error::busy);
        assert(resource.close().error()==Error::busy && resource.handle()==original);
        transport.phase(Phase::inactive); auto before=submits;
        assert(resource.close().error()==Error::bad_state && resource.handle()==original && submits==before);
        transport.phase(Phase::event); submit_result=0;
        assert(resource.close() && !resource);
        before=submits; assert(resource.close() && submits==before);
        Resource<FileTag> moved(transport,original); auto owner=std::move(moved);
        assert(!moved && owner);
        transport.phase(Phase::stopped); before=submits;
        owner.reset(); assert(!owner && submits==before);
        RequestTable requests; FilesystemService files(transport,requests);
        before=task_pool_stats().active_slots;
        assert(!files.open("../invalid").valid());
        assert(task_pool_stats().active_slots==before);
        transport.phase(Phase::event);
        NetBody body(transport,original); std::array<std::byte,4> data{};
        io_result=5; assert(body.read(data).error()==Error::protocol_error);
        io_result=2; assert(body.read(data)==2);
        submit_result=static_cast<int>(Error::busy);
        assert(body.close().error()==Error::busy && body);
        submit_result=0; assert(body.close() && !body);
    }
    {
        ui::State<int> count(1); std::uint64_t dirty=0;
        ui::Subscription sub{nullptr,&dirty,4}; count.subscribe(sub);
        count.update(Increment{}); assert(count.get()==2 && dirty==4);
        dirty=0; count.update([](const int& n) { return n; });
        assert(count.get()==2 && dirty==0);
        count.unsubscribe(sub);
    }
    for (unsigned restart=0;restart<3;++restart) {
        auto previous=reports;
        assert(AppRuntime<ErrorApp>::start(nullptr,0)==0 && reports==previous+1);
        const auto abandoned=request_token;
        const std::array<std::byte,1> background{};
        assert(deliver(17,0x8005,0,background)==1 && reports==previous+1);
        const std::array<std::byte,4> success{};
        assert(deliver(42,1,abandoned,success)==1 && reports==previous+1);
        assert(deliver(43,1)==1);
        assert(deliver(42,1,request_token,success)==1 && reports==previous+2);
        assert(task_pool_stats().active_slots==0);
        assert(deliver(43,1)==1);
        auto before=submits; AppRuntime<ErrorApp>::stop(0);
        assert(submits==before && reports==previous+2 && task_pool_stats().active_slots==0);
    }
    assert(stops==3 && cancels==3 && closes==4);
    std::printf("Lifecycle/state: retryable close, no stop imports, App task errors/restarts/cancel, immutable updates; Resource=%zu B\n",sizeof(Resource<FileTag>));
}
