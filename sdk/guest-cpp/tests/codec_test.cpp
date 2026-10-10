#include <pxa/storage.hpp>
#include "../examples/sdk-patterns/save_codec.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>

using namespace pxa;
static std::array<std::byte, 128> sent{};
static std::uint32_t sent_size;
static int submits;
void* operator new(std::size_t) { std::abort(); }
void operator delete(void*) noexcept {}
void operator delete(void*, std::size_t) noexcept {}
extern "C" std::int32_t pxa_submit(const std::uint8_t* p, std::uint32_t n) {
    assert(n <= sent.size()); std::memcpy(sent.data(), p, n); sent_size = n; ++submits; return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t, std::uint32_t, std::uint8_t*, std::uint32_t) { return 0; }
struct RoomyCodec : SaveCodec { static constexpr std::size_t max_bytes = 8; };

int main() {
    static_assert(binary::ValueCodec<SaveCodec>);
    std::array<std::byte, 8> bytes{};
    const Save original{1234, false};
    assert(binary::encode<SaveCodec>(original, bytes) == 7);
    const std::array<std::byte, 7> golden{std::byte{1}, std::byte{}, std::byte{0xd2}, std::byte{4}, std::byte{}, std::byte{}, std::byte{}};
    assert(std::equal(golden.begin(), golden.end(), bytes.begin()));
    assert(binary::decode<SaveCodec>(golden) == original);
    for (std::size_t n=0; n<7; ++n) assert(!binary::decode<SaveCodec>(std::span{golden}.first(n)));
    assert(!binary::decode<SaveCodec>(bytes));
    bytes[6] = std::byte{2};
    assert(!binary::decode<SaveCodec>(std::span{bytes}.first(7)));
    bytes[0] = std::byte{2};
    assert(binary::decode<SaveCodec>(std::span{bytes}.first(7)).error() == Error::unsupported);
    assert(!binary::encode<SaveCodec>(original, std::span{bytes}.first(6)));
    assert(!binary::encode<SaveCodec>(Save{-1, true}, bytes));
    std::copy(golden.begin(),golden.end(),bytes.begin());
    assert(!binary::decode<RoomyCodec>(bytes)); // Trailing data inside the declared bound.

    Transport transport; RequestTable requests; TaskScope tasks;
    transport.phase(Phase::start);
    StorageService storage(transport, requests);
    bool loaded = false;
    auto set = [&] { char key[]="save"; Save value=original; return storage.set_value<SaveCodec>(key, value); }();
    assert(submits == 0);
    assert(task_pool_stats().active_slots == 1);
    assert(tasks.start(std::move(set)));
    assert(sent_size == 39 && wire::get16(sent.data()) == 6 && wire::get16(sent.data()+2) == 2);
    assert(!std::memcmp(sent.data()+24,"save",4));
    assert(std::equal(golden.begin(),golden.end(),sent.begin()+32));
    std::array<std::byte, 4> success{};
    assert(requests.dispatch({6,2,wire::get64(sent.data()+4),success})); tasks.reap();
    std::array<std::byte,39> external{};
    std::memcpy(external.data()+32,"save",4);
    std::string_view aliased_key(reinterpret_cast<const char*>(external.data()+32),4);
    auto external_set=storage.set_value<SaveCodec>(aliased_key,original,external);
    assert(std::equal(golden.begin(),golden.end(),external.begin()+32));
    assert(tasks.start(std::move(external_set)));
    assert(!std::memcmp(sent.data()+24,"save",4));
    assert(requests.dispatch({6,2,wire::get64(sent.data()+4),success}));tasks.reap();
    assert(!storage.set_value<SaveCodec>("save",original,std::span{external}.first(38)).valid());
    auto run = [&]() -> Task<void> {
        auto result = co_await storage.get_value<SaveCodec>("save");
        assert(result == original); loaded=true; co_return Result<void>{};
    };
    assert(tasks.start(run()));
    assert(task_pool_stats().active_slots == 2);
    std::array<std::byte, 15> response{};
    wire::put16(response.data()+4, 2); wire::put16(response.data()+6, 7);
    std::copy(golden.begin(),golden.end(),response.begin()+8);
    assert(requests.dispatch({6,1,wire::get64(sent.data()+4),response})); tasks.reap();
    assert(loaded && task_pool_stats().active_slots == 0);
    for (unsigned mode=0;mode<3;++mode) {
        bool rejected=false;
        auto malformed = [&]() -> Task<void> {
            auto value=co_await storage.get_value<SaveCodec>("save");
            assert(!value);
            assert(value.error()==(mode==0?Error::unsupported:Error::protocol_error));
            rejected=true;co_return Result<void>{};
        };
        assert(tasks.start(malformed()));
        std::copy(golden.begin(),golden.end(),response.begin()+8);
        if (mode==0) response[8]=std::byte{2};
        else if (mode==1) response[14]=std::byte{2};
        else wire::put16(response.data()+6,6); // Malformed service record length.
        assert(requests.dispatch({6,1,wire::get64(sent.data()+4),response}));tasks.reap();
        wire::put16(response.data()+6,7);
        assert(rejected && task_pool_stats().active_slots==0);
    }
    assert(tasks.start(storage.get_value<SaveCodec>("save")));
    const auto abandoned=wire::get64(sent.data()+4);
    tasks.cancel();
    assert(task_pool_stats().active_slots==0 && wire::get16(sent.data())==1);
    assert(requests.dispatch({6,1,abandoned,response}));
    auto before=task_pool_stats();
    assert(!storage.set_value<SaveCodec>("save",Save{-1,true}).valid());
    assert(!storage.get_value<SaveCodec>("../save").valid());
    assert(task_pool_stats().active_slots == before.active_slots);
    std::printf("Codec: golden/version/truncation/tails/boolean; owned creation, two slots including caller, zero heap OK\n");
}
