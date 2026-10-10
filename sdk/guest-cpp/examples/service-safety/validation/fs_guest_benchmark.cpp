#include <pxa/fs.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
static std::uint64_t token;static unsigned imports,digest=2166136261u;
extern "C" int pxa_submit(const std::uint8_t* data,std::uint32_t size){
 ++imports;const auto*p=reinterpret_cast<const std::byte*>(data);
 if(pxa::wire::get16(p)==5)token=pxa::wire::get64(p+4);
 for(unsigned i=0;i<size;++i)digest=(digest^data[i])*16777619u;
 return 0;
}
extern "C" int pxa_io(std::uint64_t,std::uint32_t,std::uint8_t*,std::uint32_t){std::abort();}
int main(int argc,char**argv){
 if(argc!=2)return 1;unsigned length=std::strtoul(argv[1],nullptr,10);
 std::string path(length,'x');for(unsigned i=64;i<length;i+=65)path[i]='/';pxa::Transport tx;tx.phase(pxa::Phase::event);pxa::RequestTable table;pxa::FilesystemService fs(tx,table);
 std::array<std::byte,12> response{};pxa::wire::put64(response.data()+4,0x100000019);
 auto begin=std::chrono::steady_clock::now();
 for(unsigned i=0;i<150000;++i){
  auto task=fs.open(path);auto h=task.release();if(!h)std::abort();h.resume();
  if(!table.dispatch({5,1,token,response})||!h.done()||!h.promise().result||!h.promise().result->has_value())std::abort();
  h.destroy();
 }
 auto end=std::chrono::steady_clock::now();const auto pool=pxa::task_pool_stats();
 printf("{\"ns\":%.3f,\"imports\":%u,\"digest\":%u,\"pool\":%zu,\"peak_slots\":%u}\n",std::chrono::duration<double,std::nano>(end-begin).count()/150000,imports,digest,pool.reserved_bytes,pool.peak_slots);
}
