/* SPDX-License-Identifier: MIT */
#ifndef PXA_BROWSER_SERVICES_H
#define PXA_BROWSER_SERVICES_H
#include "pxa/runtime.h"
#include "pxa/permission.h"
#include "pxa/fs.h"
#include "pxa/audio.h"
#include "pxa/net.h"
pxa_status_t browser_socket_open(void *,const pxa_socket_request_t *,void **);
int32_t browser_socket_io(void *,void *,uint32_t,uint8_t *,size_t);
void browser_socket_close(void *,void *);
pxa_status_t browser_asset_info(pxa_bytes_t,pxa_asset_info_t *);
pxa_status_t browser_music_play(void *,uint64_t,const pxa_audio_asset_t *,uint64_t *);
pxa_status_t browser_music_control(void *,uint64_t,const pxa_audio_asset_control_t *);
pxa_status_t browser_music_peek(void *,pxa_audio_playback_event_t *);
pxa_status_t browser_music_consume(void *,const pxa_audio_playback_event_t *);
void browser_music_close(uint64_t);
void browser_music_gain(uint64_t,int16_t);
void browser_audio_poll(void);
pxa_status_t browser_assets_init(pxa_runtime_t *runtime);
void browser_assets_poll(void);
pxa_status_t browser_files_init(const pxa_fs_roots_backend_t **output);
void browser_files_bind(pxa_fs_service_t *service);
pxa_status_t browser_capture_open(void *,uint64_t *);
int32_t browser_capture_io(void *,uint64_t,uint32_t,uint8_t *,size_t);
void browser_capture_close(void *,uint64_t);
pxa_status_t browser_services_init(pxa_runtime_t *runtime);
void browser_services_tick(void);
pxa_status_t browser_audio_init(pxa_runtime_t *,pxa_permission_service_t *);
pxa_status_t browser_net_init(pxa_runtime_t *,pxa_permission_service_t *);
void browser_net_tick(void);
pxa_status_t browser_graphics_init(pxa_runtime_t *,uint16_t,uint16_t);
void browser_graphics_presented(void);
void browser_graphics_order(void);
void browser_graphics_poll(void);
#endif
