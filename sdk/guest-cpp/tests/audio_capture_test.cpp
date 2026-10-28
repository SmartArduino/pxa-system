// SPDX-License-Identifier: MIT
#include "../../testing/pxa_control_capture.h"
#include <pxa/audio_capture.hpp>
#include <cassert>

static std::uint64_t token;
static unsigned closes;
static bool done;
static constexpr std::uint64_t handle = UINT64_C(0x100000003);
extern "C" std::int32_t pxa_test_control(const std::uint8_t *data, std::uint32_t size) {
  auto p = reinterpret_cast<const std::byte *>(data);
  if (pxa::wire::get16(p) == 1) {
    assert(pxa::wire::get16(p + 2) == 2);
    ++closes;
    return 0;
  }
  assert(size == 28 && pxa::wire::get16(p) == 10 && pxa::wire::get16(p + 2) == 6);
  assert(pxa::wire::get64(p + 20) == UINT64_C(0x100000002));
  token = pxa::wire::get64(p + 4);
  return 0;
}
extern "C" std::int32_t pxa_io(std::uint64_t id, std::uint32_t op,
                                std::uint8_t *out, std::uint32_t size) {
  assert(id == handle && op == 1 && size == 1275);
  out[0] = 0x18;
  return 1;
}
static pxa::Task<void> check(pxa::Task<pxa::AudioCapture> task, unsigned duration) {
  auto capture = co_await std::move(task);
  if (duration == 20 || duration == 40 || duration == 60) {
    assert(capture && capture->frame_ms() == duration);
    std::array<std::byte, 1275> bytes{};
    assert(capture->read(bytes) == 1 && bytes[0] == std::byte{0x18});
  } else
    assert(!capture && capture.error() == pxa::Error::protocol_error);
  done = true;
  co_return pxa::Result<void>{};
}
int main() {
  pxa::Transport transport;
  transport.phase(pxa::Phase::event);
  pxa::RequestTable requests;
  pxa::TaskScope tasks;
  pxa::Permission permission(transport, UINT64_C(0x100000002));
  for (unsigned duration : {20, 40, 60, 0, 10, 120}) {
    done = false;
    assert(tasks.start(check(pxa::AudioCaptureService(transport, requests).open(permission), duration)));
    assert(token && !done);
    std::array<std::byte, 24> reply{};
    pxa::wire::put64(reply.data() + 4, handle);
    pxa::wire::put32(reply.data() + 12, 16000);
    reply[16] = reply[17] = std::byte{1};
    pxa::wire::put16(reply.data() + 18, duration);
    pxa::wire::put32(reply.data() + 20, 1275);
    auto previous = closes;
    assert(requests.dispatch({10, 6, token, reply}));
    assert(done && closes == previous + 1); // Invalid metadata also retires its handle.
    tasks.reap();
  }
}
