#include "../../testing/pxa_control_capture.h"
#include <pxa/fs.hpp>
#include <cassert>
#include <cstring>
#include <string>
using namespace pxa;
static std::array<std::byte,4096> captured{};
static unsigned closes;
extern "C" std::int32_t pxa_test_control(const std::uint8_t *p,std::uint32_t n) {
    assert(n<=captured.size());
    if(wire::get16(reinterpret_cast<const std::byte *>(p))==1) { ++closes; return 0; }
    std::memcpy(captured.data(),p,n); return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t,std::uint32_t,std::uint8_t *,std::uint32_t) { return -3; }
template<class T> static Result<T> complete(Task<T> task,RequestTable& requests,
    std::span<const std::byte> result) {
    assert(task.valid()); auto c=task.release();c.resume();
    if(!c.done()) assert(requests.dispatch({wire::get16(captured.data()),
        wire::get16(captured.data()+2),wire::get64(captured.data()+4),result}));
    assert(c.done() && c.promise().result); auto out=std::move(*c.promise().result);
    c.destroy();return out;
}
int main() {
    Transport t;t.phase(Phase::event);RequestTable requests;
    Permission permission(t,UINT64_C(0x100000003));
    std::array<std::byte,32> grant_result{};
    wire::put64(grant_result.data()+4,UINT64_C(0x100000001));
    wire::put32(grant_result.data()+12,3);grant_result[16]=std::byte{7};
    auto grant=complete(FilesystemService(t,requests).request_directory(permission,"Game data"),requests,grant_result);
    assert(grant && grant->rights==3 && grant->id[0]==std::byte{7});
    assert(wire::get16(captured.data()+2)==10 && wire::get32(captured.data()+28)==3);
    std::array<std::byte,32> opened{};wire::put64(opened.data()+4,UINT64_C(0x100000002));
    opened[12]=std::byte{1};wire::put64(opened.data()+24,8192);
    auto pending=[&]{std::string path="pak0.pak";return grant->root.open(path);}();
    auto file=complete(std::move(pending),requests,opened);
    assert(file && wire::get16(captured.data()+32)==8);
    assert(std::memcmp(captured.data()+34,"pak0.pak",8)==0);
    assert(grant->root.close() && file->handle()==UINT64_C(0x100000002));
    assert(file->close());assert(closes==2);
    auto root=complete(FilesystemService(t,requests).open_grant(permission,grant->id),requests,
        std::span{grant_result}.first(16));
    assert(root);
    auto malformed=opened;malformed[13]=std::byte{1};
    auto bad=complete(root->open("x"),requests,malformed);
    assert(!bad && bad.error()==Error::protocol_error && closes==3);
    std::array<std::byte,76> info{};wire::put64(info.data()+20,77);
    wire::put16(info.data()+40,255);wire::put16(info.data()+42,255);
    auto metadata=complete(root->info(),requests,info);
    assert(metadata && metadata->mount_epoch==77);
    std::array<std::byte,1038> external{};
    std::string long_path(300,'a');
    auto long_file=complete(root->open(long_path,OpenMode::read,external),requests,opened);
    assert(long_file && wire::get16(captured.data()+32)==300);
    assert(long_file->close());assert(root->close());assert(permission.close());
    auto media=complete(FilesystemService(t,requests).app_media(),requests,
        std::span{grant_result}.first(16));
    assert(media && captured[20]==std::byte{5});
    for(unsigned i=21;i<48;++i) assert(captured[i]==std::byte{0});
    assert(media->close());
    assert(task_pool_stats().active_slots==0 && task_pool_stats().allocation_failures==0);
}
