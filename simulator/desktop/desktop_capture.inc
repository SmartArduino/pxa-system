/* SPDX-License-Identifier: MIT */
#include <opus/opus.h>
struct desktop_capture {
  SDL_AudioDeviceID device;
  OpusEncoder *encoder;
  uint64_t id;
  opus_int16 pcm[3200];
  uint32_t read, count;
};
static void desktop_capture_close(void *context, uint64_t id) {
  product_host_t *host = context;
  struct desktop_capture *capture = host->capture;
  if (!capture || capture->id != id)
    return;
  host->capture = NULL;
  SDL_CloseAudioDevice(capture->device);
  opus_encoder_destroy(capture->encoder);
  free(capture);
}
static void desktop_capture_callback(void *context, Uint8 *bytes, int size) {
  struct desktop_capture *capture = context;
  const opus_int16 *input = (const opus_int16 *)bytes;
  for (int i = 0; i < size / 2; i++) {
    if (capture->count == 3200) {
      capture->read = (capture->read + 320) % 3200;
      capture->count -= 320;
    }
    capture->pcm[(capture->read + capture->count) % 3200] = input[i];
    capture->count++;
  }
}
static pxa_status_t desktop_capture_open(void *context, uint64_t *id) {
  product_host_t *host = context;
  *id = 0;
  if (!host->focused)
    return PXA_STATUS_UNAVAILABLE;
  if (host->capture)
    return PXA_STATUS_BUSY;
  const char *delay = getenv("PXA_SIM_CAPTURE_START_DELAY_MS");
  if (delay && *delay) {
    char *end;
    unsigned long ms = strtoul(delay, &end, 10);
    if (!*end && ms && ms <= 30000) {
      if (!host->capture_ready_us) {
        host->capture_ready_us = now_us(NULL) + (uint64_t)ms * 1000;
        fprintf(stderr, "PXA_CAPTURE_WARMUP delay_ms=%lu\n", ms);
      }
      if (now_us(NULL) < host->capture_ready_us)
        return PXA_STATUS_UNAVAILABLE;
    }
  }
  // Capture also works when the app has never opened a playback device.
  if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) &&
      SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    return PXA_STATUS_UNAVAILABLE;
  struct desktop_capture *capture = calloc(1, sizeof(*capture));
  if (!capture)
    return PXA_STATUS_RESOURCE_LIMIT;
  SDL_AudioSpec want = {0};
  want.freq = 16000;
  want.format = AUDIO_S16SYS;
  want.channels = 1;
  want.samples = 320;
  want.callback = desktop_capture_callback;
  want.userdata = capture;
  capture->device = SDL_OpenAudioDevice(NULL, 1, &want, NULL, 0);
  int error = 0;
  if (capture->device)
    capture->encoder =
        opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &error);
  if (!capture->device || !capture->encoder) {
    if (capture->device)
      SDL_CloseAudioDevice(capture->device);
    free(capture);
    return PXA_STATUS_UNAVAILABLE;
  }
  opus_encoder_ctl(capture->encoder, OPUS_SET_BITRATE(24000));
  capture->id = ++host->capture_sequence;
  if (!capture->id)
    capture->id = ++host->capture_sequence;
  host->capture = capture;
  *id = capture->id;
  SDL_PauseAudioDevice(capture->device, 0);
  return PXA_STATUS_OK;
}
static int32_t desktop_capture_io(void *context, uint64_t id, uint32_t op,
                                  uint8_t *data, size_t size) {
  product_host_t *host = context;
  struct desktop_capture *capture = host->capture;
  if (!capture || capture->id != id || !host->focused)
    return PXA_STATUS_UNAVAILABLE;
  if (op == PXA_IO_POLL) {
    if (size)
      return PXA_STATUS_INVALID_ARGUMENT;
    SDL_LockAudioDevice(capture->device);
    int ready = capture->count >= 320;
    SDL_UnlockAudioDevice(capture->device);
    return ready;
  }
  if (op != PXA_IO_READ)
    return PXA_STATUS_UNSUPPORTED;
  if (!data || size < PXA_AUDIO_CAPTURE_MAX_PACKET)
    return PXA_STATUS_LIMIT_EXCEEDED;
  opus_int16 pcm[320];
  SDL_LockAudioDevice(capture->device);
  if (capture->count < 320) {
    SDL_UnlockAudioDevice(capture->device);
    return PXA_STATUS_WOULD_BLOCK;
  }
  for (unsigned i = 0; i < 320; i++)
    pcm[i] = capture->pcm[(capture->read + i) % 3200];
  capture->read = (capture->read + 320) % 3200;
  capture->count -= 320;
  SDL_UnlockAudioDevice(capture->device);
  int n = opus_encode(capture->encoder, pcm, 320, data,
                      PXA_AUDIO_CAPTURE_MAX_PACKET);
  return n > 0 ? n : PXA_STATUS_IO_ERROR;
}
