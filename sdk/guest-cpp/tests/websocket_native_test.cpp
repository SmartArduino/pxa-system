// SPDX-License-Identifier: MIT
#include "../../testing/pxa_control_capture.h"
#include <pxa/websocket.hpp>
#include <cassert>
#include <cstring>
#include <iostream>
using namespace pxa;
static constexpr std::uint64_t grant=UINT64_C(0x100000001),handle=UINT64_C(0x200000002);
static std::uint64_t token;
static unsigned closes=0;
static bool done=false;
extern "C" std::int32_t pxa_test_control(const std::uint8_t*p,std::uint32_t n){
  auto b=reinterpret_cast<const std::byte*>(p);auto service=wire::get16(b),op=wire::get16(b+2);
  if(service==9){
    assert(op==9&&n>44);token=wire::get64(b+4);
    assert(wire::get64(b+20)==grant);
    auto size=wire::get16(b+28);assert(n==44u+size);
    assert(wire::get32(b+32)==32768&&wire::get32(b+36)==4096);
    assert(std::memcmp(p+44,"ws://127.0.0.1:31889/dewfield",size)==0);
  }else{assert(service==1);if(op==2)++closes;else assert(op==1||op==4);}
  return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t h,std::uint32_t op,std::uint8_t*data,std::uint32_t n){
  assert(h==handle);
  if(op==4)return 1;
  if(op==2){assert(n==4&&data[0]==2&&!std::memcmp(data+1,"abc",3));return 4;}
  if(op==1){if(n<4)return -16;std::memcpy(data,"\2xyz",4);return 4;}
  return -3;
}
static Task<void> exercise(Transport&t,RequestTable&r,Permission&p){
  auto socket=co_await WebSocketClient(t,r).open_native("ws://127.0.0.1:31889/dewfield",p);
  assert(socket);
  std::array<std::byte,4> bytes{std::byte{2},std::byte{'a'},std::byte{'b'},std::byte{'c'}};
  auto wrote=socket->write(bytes);assert(wrote&&*wrote==4);
  auto short_read=socket->read(std::span{bytes}.first(3));assert(!short_read&&short_read.error()==Error::limit_exceeded);
  auto read=socket->read(bytes);assert(read&&*read==4&&bytes[0]==std::byte{2}&&bytes[1]==std::byte{'x'});
  assert(socket->close());done=true;co_return Result<void>{};
}
int main(){
  Transport t;t.phase(Phase::event);RequestTable r;TaskScope tasks;
  Permission permission(t,grant);
  assert(tasks.start(exercise(t,r,permission)));
  std::array<std::byte,12> result{};wire::put64(result.data()+4,handle);
  assert(r.dispatch({9,9,token,result}));
  assert(done&&closes==1);
  std::cout<<"PASS: native WebSocket wire, whole-message IO, short-read retry, RAII and temporary client lifetime\n";
}
