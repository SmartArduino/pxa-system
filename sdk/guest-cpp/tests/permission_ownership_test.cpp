#include <pxa/permission.hpp>
#include <pxa/binary.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>

static std::string_view expected_name;
static std::span<const std::byte> expected_scope;
static const std::byte* expected_packet;
static std::uint16_t opcode;
static std::uint64_t token;
static unsigned submits, closes, cancels, completed;
static std::int32_t submit_result;
static constexpr std::uint64_t handle = 0x1234567800000001ull;

extern "C" std::int32_t pxa_submit(const std::uint8_t* data, std::uint32_t size) {
    const auto* bytes = reinterpret_cast<const std::byte*>(data);
    if (pxa::wire::get16(bytes) == 1) {
        const auto op = pxa::wire::get16(bytes+2);
        assert(size == 28);
        if (op == 2) { assert(pxa::wire::get64(bytes+20) == handle); ++closes; }
        else { assert(op == 1); ++cancels; }
        return 0;
    }
    assert(pxa::wire::get16(bytes) == 11 && pxa::wire::get16(bytes+2) == opcode);
    token = pxa::wire::get64(bytes+4);
    assert(token && pxa::wire::get32(bytes+12) == size-20);
    assert(pxa::task_pool_stats().active_slots == 2);
    if (expected_packet) assert(bytes == expected_packet);
    assert(pxa::wire::get16(bytes+20) == 1 && pxa::wire::get16(bytes+22) == expected_name.size());
    assert(std::string_view(reinterpret_cast<const char*>(bytes+24),expected_name.size()) == expected_name);
    const auto* scope = bytes+24+expected_name.size();
    if (!expected_scope.empty()) {
        assert(pxa::wire::get16(scope) == 2 && pxa::wire::get16(scope+2) == expected_scope.size());
        assert(std::equal(expected_scope.begin(),expected_scope.end(),scope+4));
    }
    assert(size == 24+expected_name.size()+(expected_scope.empty()?0:4+expected_scope.size()));
    ++submits;
    return submit_result;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) {
    assert(false); return -1;
}
static pxa::Task<void> check(pxa::Task<bool> task, pxa::Result<bool> expected) {
    auto result = co_await std::move(task);
    assert(result == expected); ++completed;
    co_return pxa::Result<void>{};
}
static pxa::Task<void> acquire(pxa::Task<pxa::Permission> task,
                               std::optional<pxa::Error> error = {}) {
    auto result = co_await std::move(task);
    if (error) assert(!result && result.error() == *error);
    else assert(result && result->handle() == handle);
    ++completed;
    co_return pxa::Result<void>{};
}

int main() {
    pxa::Transport transport; transport.phase(pxa::Phase::event);
    pxa::RequestTable requests;
    pxa::PermissionService service(transport,requests);
    pxa::TaskScope tasks;
    const auto reserved = pxa::task_pool_stats().reserved_bytes;
    std::array<std::byte, 1200> packet;
    const std::array<std::byte, 5> allowed{std::byte{},std::byte{},std::byte{},std::byte{},std::byte{1}};
    auto finish = [&](std::span<const std::byte> body) {
        const auto before = completed;
        assert(requests.dispatch({11,opcode,token,body})); tasks.reap();
        assert(completed == before+1 && pxa::task_pool_stats().active_slots == 0);
    };
    const std::string_view media="media";
    expected_name="sensor.scope.long.enough.to.leave.the.sso";
    expected_scope=std::as_bytes(std::span{media}); opcode=1;
    auto owned_check = [&] {
        std::string name(expected_name), scope(media);
        auto pending=pxa::PermissionService(transport,requests).check(name,scope);
        name.assign("changed"); scope.assign("changed"); return pending;
    }();
    assert(tasks.start(check(std::move(owned_check),true))); finish(allowed);

    opcode=2;
    auto owned_acquire = [&] {
        std::string name(expected_name), scope(media);
        return pxa::PermissionService(transport,requests).acquire(name,scope);
    }();
    assert(tasks.start(acquire(std::move(owned_acquire))));
    std::array<std::byte, 13> granted{}; pxa::wire::put64(granted.data()+4,handle);
    finish(std::span{granted}.first(12)); assert(closes==1);

    opcode=1; expected_packet=packet.data();
    auto external = [&] {
        std::string name(expected_name), scope(media);
        return pxa::PermissionService(transport,requests).check(name,
            std::as_bytes(std::span{scope}),packet);
    }();
    assert(tasks.start(check(std::move(external),true))); finish(allowed);

    // In-place encoding snapshots name and uses overlap-safe scope copying.
    expected_name="alias";
    std::memcpy(packet.data(),expected_name.data(),5);
    std::memcpy(packet.data()+5,media.data(),5);
    auto alias=service.check({reinterpret_cast<const char*>(packet.data()),5},
                            std::span{packet}.subspan(5,5),packet);
    assert(tasks.start(check(std::move(alias),true))); finish(allowed);
    expected_packet=nullptr;

    std::array<std::byte,1025> scope; scope.fill(std::byte{0xaa});
    const std::string max_name(96,'a'); expected_name=max_name;
    expected_scope=std::span{scope}.first(388); // Exactly 512 B default packet.
    assert(tasks.start(check(service.check(expected_name,expected_scope),true))); finish(allowed);
    expected_scope=std::span{scope}.first(1024); expected_packet=packet.data();
    assert(tasks.start(check(service.check(expected_name,expected_scope,packet),true))); finish(allowed);
    expected_packet=nullptr;

    packet.fill(std::byte{0x55});
    auto failed = [&](auto task,pxa::Error error) {
        assert(!task.valid() && task.failure()==error && pxa::task_pool_stats().active_slots==0);
    };
    failed(service.check(max_name,std::span{scope}.first(389)),pxa::Error::resource_limit);
    failed(service.acquire(max_name,std::span{scope}.first(389)),pxa::Error::resource_limit);
    failed(service.check(max_name,scope,packet),pxa::Error::invalid_argument);
    failed(service.acquire("",{},packet),pxa::Error::invalid_argument);
    failed(service.check(std::string(97,'a'),{},packet),pxa::Error::invalid_argument);
    failed(service.check(std::string_view{"a\0b",3},{},packet),pxa::Error::invalid_argument);
    failed(service.check("a",{},std::span{packet}.first(24)),pxa::Error::resource_limit);
    assert(std::all_of(packet.begin(),packet.end(),[](auto b){return b==std::byte{0x55};}));

    expected_name="permission"; expected_scope={};
    assert(tasks.start(check(service.check(expected_name),std::unexpected(pxa::Error::protocol_error))));
    auto bad_bool=allowed; bad_bool[4]=std::byte{2}; finish(bad_bool);
    opcode=2;
    assert(tasks.start(acquire(service.acquire(expected_name),pxa::Error::protocol_error)));
    finish(granted); assert(closes==2); // Valid handle plus a trailing byte is reclaimed.
    assert(tasks.start(acquire(service.acquire(expected_name),pxa::Error::denied)));
    const auto denied=pxa::binary::encode(std::int32_t{-4}); finish(denied);

    submit_result=-4;
    assert(tasks.start(acquire(service.acquire(expected_name),pxa::Error::denied)));
    assert(pxa::task_pool_stats().active_slots==0); submit_result=0;

    assert(tasks.start(acquire(service.acquire(expected_name))));
    tasks.cancel(); assert(cancels==1 && pxa::task_pool_stats().active_slots==0);
    assert(requests.dispatch({11,2,token,std::span{granted}.first(12)})); assert(closes==3);
    assert(!requests.dispatch({11,2,token,std::span{granted}.first(12)}));

    assert(tasks.start(acquire(service.acquire(expected_name))));
    transport.phase(pxa::Phase::stopped); tasks.cancel();
    assert(cancels==1 && closes==3 && !requests.dispatch({11,2,token,granted}));
    const auto stats=pxa::task_pool_stats();
    assert(stats.active_slots==0 && stats.peak_slots==2 && stats.allocation_failures==0);
    assert(stats.reserved_bytes==reserved);
    std::printf("Permission ownership/boundaries/aliasing/late handles OK: %u submits, peak %u slots, reserved %zu B\n",
                submits,stats.peak_slots,reserved);
}
