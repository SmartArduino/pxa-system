#ifndef PXA_AUDIO_H
#define PXA_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#include "pxa/permission.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PXA_AUDIO_SERVICE_ID UINT16_C(10)
#define PXA_AUDIO_SERVICE_MAJOR UINT16_C(0)
#define PXA_AUDIO_SERVICE_MINOR UINT16_C(2)
#define PXA_AUDIO_SERVICE_PATCH UINT16_C(0)
#define PXA_AUDIO_OPEN_SESSION UINT16_C(1)
#define PXA_AUDIO_COMMIT_GRAPH UINT16_C(2)
#define PXA_AUDIO_QUERY_STATE UINT16_C(3)
#define PXA_AUDIO_FLUSH UINT16_C(4)
#define PXA_AUDIO_USAGE_MEDIA UINT16_C(1)
#define PXA_AUDIO_ROUTE_SPEAKER UINT16_C(1)
#define PXA_AUDIO_MAX_EQ_BANDS UINT8_C(5)

typedef struct {
    uint32_t sample_rate;
    uint8_t channels;
    uint16_t frame_ms;
} pxa_audio_format_t;

typedef struct {
    uint16_t frequency_hz;
    int16_t gain_db_q8;
    uint16_t q_q8;
} pxa_audio_eq_band_t;

typedef struct {
    int16_t gain_db_q8;
    uint16_t route;
    pxa_audio_eq_band_t eq_bands[PXA_AUDIO_MAX_EQ_BANDS];
    uint8_t eq_band_count;
} pxa_audio_graph_t;

#define PXA_AUDIO_STATE_ACCEPTED_IS_SINK_SUBMITTED UINT32_C(1)

typedef struct {
    uint64_t submitted_samples;
    uint64_t accepted_samples;
    uint32_t queued_samples;
    uint32_t flags;
} pxa_audio_state_t;

typedef pxa_status_t (*pxa_audio_open_fn)(
    void *context, uint16_t usage, pxa_audio_format_t *format,
    uint64_t *provider_session);
typedef pxa_status_t (*pxa_audio_commit_fn)(
    void *context, uint64_t provider_session,
    const pxa_audio_graph_t *graph);
typedef pxa_status_t (*pxa_audio_submit_fn)(
    void *context, uint64_t provider_session, const uint8_t *pcm,
    size_t size);
typedef pxa_status_t (*pxa_audio_query_fn)(
    void *context, uint64_t provider_session, pxa_audio_state_t *state);
typedef pxa_status_t (*pxa_audio_flush_fn)(
    void *context, uint64_t provider_session);
typedef void (*pxa_audio_close_fn)(
    void *context, uint64_t provider_session);

typedef struct {
    uint32_t struct_size;
    void *context;
    pxa_audio_open_fn open;
    pxa_audio_commit_fn commit;
    pxa_audio_submit_fn submit;
    pxa_audio_close_fn close;
    pxa_audio_query_fn query;
    pxa_audio_flush_fn flush;
} pxa_audio_backend_t;

typedef struct {
    uint32_t struct_size;
    uint16_t max_sessions;
    uint16_t max_sessions_per_component;
    uint8_t max_eq_bands;
    uint8_t reserved[3];
    pxa_audio_backend_t backend;
    pxa_permission_service_t *permissions;
} pxa_audio_config_t;

typedef struct pxa_audio_service pxa_audio_service_t;

size_t pxa_audio_service_workspace_size(const pxa_audio_config_t *config);
pxa_status_t pxa_audio_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_audio_config_t *config, pxa_audio_service_t **output);
pxa_status_t pxa_audio_service_register(pxa_audio_service_t *service);
int pxa_audio_has_active_sessions(const pxa_audio_service_t *service);

#ifdef __cplusplus
}
#endif

#endif
