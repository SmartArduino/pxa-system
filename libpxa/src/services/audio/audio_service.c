#include "pxa/audio.h"
#include "common/checked_math.h"
#include "common/status_internal.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#define PXA_AUDIO_MAGIC UINT32_C(0x50584155)
#define PXA_AUDIO_SLOT_MAGIC UINT32_C(0x50584153)
#define PXA_AUDIO_SLOT_NONE UINT16_MAX

typedef struct pxa_audio_session pxa_audio_session_t;

struct pxa_audio_service {
    uint32_t magic;
    pxa_runtime_t *runtime;
    pxa_permission_service_t *permissions;
    pxa_audio_backend_t backend;
    pxa_audio_session_t *sessions;
    uint16_t max_sessions;
    uint16_t max_sessions_per_component;
    uint16_t free_head;
    uint16_t active_head;
    uint8_t max_eq_bands;
    uint8_t registered;
};

struct pxa_audio_session {
    uint32_t magic;
    pxa_audio_service_t *service;
    pxa_component_t component;
    pxa_handle_t handle;
    uint64_t provider_session;
    pxa_audio_format_t format;
    uint16_t next;
    uint16_t previous;
};

typedef struct {
    pxa_handle_t permission;
    uint16_t usage;
    uint8_t seen;
} pxa_audio_open_request_t;

static int config_valid(const pxa_audio_config_t *config) {
    return config != NULL && config->struct_size >= sizeof(*config) &&
           config->max_sessions != 0 &&
           config->max_sessions_per_component != 0 &&
           config->max_sessions_per_component <= config->max_sessions &&
           config->max_eq_bands <= PXA_AUDIO_MAX_EQ_BANDS &&
           config->backend.struct_size >= sizeof(config->backend) &&
           config->backend.open != NULL && config->backend.commit != NULL &&
           config->backend.submit != NULL && config->backend.close != NULL &&
           config->permissions != NULL;
}

size_t pxa_audio_service_workspace_size(const pxa_audio_config_t *config) {
    size_t sessions_size;
    size_t size = PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    if (!config_valid(config) ||
        sizeof(pxa_audio_session_t) >
            SIZE_MAX / (size_t)config->max_sessions) {
        return 0;
    }
    sessions_size =
        (size_t)config->max_sessions * sizeof(pxa_audio_session_t);
    if (sizeof(pxa_audio_service_t) > SIZE_MAX - size) return 0;
    size += sizeof(pxa_audio_service_t);
    if (sessions_size > SIZE_MAX - size) return 0;
    return size + sessions_size;
}

pxa_status_t pxa_audio_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_audio_config_t *config, pxa_audio_service_t **output) {
    size_t required;
    uintptr_t cursor;
    uintptr_t end;
    uint16_t index;
    pxa_audio_service_t *service;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    required = pxa_audio_service_workspace_size(config);
    if (workspace == NULL || runtime == NULL || required == 0 ||
        workspace_size < required ||
        (uintptr_t)workspace > UINTPTR_MAX - workspace_size) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    end = (uintptr_t)workspace + workspace_size;
    cursor = pxa_internal_align_pointer((uintptr_t)workspace, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    service = (pxa_audio_service_t *)cursor;
    memset(service, 0, sizeof(*service));
    cursor += sizeof(*service);
    service->sessions = (pxa_audio_session_t *)cursor;
    cursor += (size_t)config->max_sessions * sizeof(service->sessions[0]);
    if (cursor > end) return PXA_STATUS_INVALID_ARGUMENT;
    service->runtime = runtime;
    service->permissions = config->permissions;
    service->backend = config->backend;
    service->max_sessions = config->max_sessions;
    service->max_sessions_per_component =
        config->max_sessions_per_component;
    service->max_eq_bands = config->max_eq_bands;
    service->free_head = 0;
    service->active_head = PXA_AUDIO_SLOT_NONE;
    memset(service->sessions, 0,
           (size_t)service->max_sessions * sizeof(service->sessions[0]));
    for (index = 0; index < service->max_sessions; ++index) {
        service->sessions[index].magic = PXA_AUDIO_SLOT_MAGIC;
        service->sessions[index].service = service;
        service->sessions[index].next =
            index + 1u < service->max_sessions
                ? (uint16_t)(index + 1u)
                : PXA_AUDIO_SLOT_NONE;
        service->sessions[index].previous = PXA_AUDIO_SLOT_NONE;
    }
    service->magic = PXA_AUDIO_MAGIC;
    *output = service;
    return PXA_STATUS_OK;
}

static int service_valid(const pxa_audio_service_t *service) {
    return service != NULL && service->magic == PXA_AUDIO_MAGIC;
}

static int format_valid(const pxa_audio_format_t *format) {
    return format->sample_rate >= 8000 && format->sample_rate <= 48000 &&
           (format->channels == 1 || format->channels == 2) &&
           format->frame_ms >= 5 && format->frame_ms <= 120;
}

static int16_t read_i16(const uint8_t *data) {
    uint16_t bits = pxa_read_u16(data);
    int32_t value = bits <= (uint16_t)INT16_MAX
                        ? (int32_t)bits
                        : (int32_t)bits - INT32_C(65536);
    return (int16_t)value;
}

static pxa_status_t parse_open(pxa_bytes_t payload,
                               pxa_audio_open_request_t *output) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    pxa_status_t status;
    memset(output, 0, sizeof(*output));
    pxa_record_iterator_init(&iterator, payload);
    for (;;) {
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.raw_tag < previous) return PXA_STATUS_INVALID_ARGUMENT;
        previous = record.raw_tag;
        if (record.optional) continue;
        if (record.tag == 1 && (output->seen & 1u) == 0 &&
            record.payload.size == 4) {
            output->permission = pxa_read_u32(record.payload.data);
            output->seen |= 1u;
        } else if (record.tag == 2 && (output->seen & 2u) == 0 &&
                   record.payload.size == 2) {
            output->usage = pxa_read_u16(record.payload.data);
            output->seen |= 2u;
            if (output->usage != PXA_AUDIO_USAGE_MEDIA) {
                return PXA_STATUS_UNSUPPORTED;
            }
        } else {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
    }
    return output->seen == 3u && output->permission != PXA_HANDLE_INVALID
               ? PXA_STATUS_OK
               : PXA_STATUS_INVALID_ARGUMENT;
}

static pxa_status_t parse_graph(pxa_bytes_t payload,
                                uint8_t max_eq_bands,
                                pxa_handle_t *session_handle,
                                pxa_audio_graph_t *graph) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    uint8_t seen = 0;
    pxa_status_t status;
    *session_handle = PXA_HANDLE_INVALID;
    memset(graph, 0, sizeof(*graph));
    pxa_record_iterator_init(&iterator, payload);
    for (;;) {
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.raw_tag < previous) return PXA_STATUS_INVALID_ARGUMENT;
        previous = record.raw_tag;
        if (record.optional) continue;
        if (record.tag == 1 && (seen & 1u) == 0 &&
            record.payload.size == 4) {
            *session_handle = pxa_read_u32(record.payload.data);
            seen |= 1u;
        } else if (record.tag == 2 && (seen & 2u) == 0 &&
                   record.payload.size == 2) {
            graph->gain_db_q8 = read_i16(record.payload.data);
            seen |= 2u;
            if (graph->gain_db_q8 < -48 * 256 ||
                graph->gain_db_q8 > 12 * 256) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
        } else if (record.tag == 3 && record.payload.size == 6 &&
                   graph->eq_band_count < max_eq_bands) {
            pxa_audio_eq_band_t *band =
                &graph->eq_bands[graph->eq_band_count];
            band->frequency_hz = pxa_read_u16(record.payload.data);
            band->gain_db_q8 = read_i16(record.payload.data + 2);
            band->q_q8 = pxa_read_u16(record.payload.data + 4);
            if (band->frequency_hz < 20 || band->frequency_hz > 20000 ||
                band->gain_db_q8 < -12 * 256 ||
                band->gain_db_q8 > 12 * 256 || band->q_q8 < 64 ||
                band->q_q8 > 4096) {
                return PXA_STATUS_INVALID_ARGUMENT;
            }
            graph->eq_band_count++;
        } else if (record.tag == 4 && (seen & 4u) == 0 &&
                   record.payload.size == 2) {
            graph->route = pxa_read_u16(record.payload.data);
            seen |= 4u;
            if (graph->route != PXA_AUDIO_ROUTE_SPEAKER) {
                return PXA_STATUS_UNSUPPORTED;
            }
        } else {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
    }
    return seen == 7u && *session_handle != PXA_HANDLE_INVALID
               ? PXA_STATUS_OK
               : PXA_STATUS_INVALID_ARGUMENT;
}

static uint16_t sessions_for_component(const pxa_audio_service_t *service,
                                       pxa_component_t component) {
    uint16_t index = service->active_head;
    uint16_t count = 0;
    while (index != PXA_AUDIO_SLOT_NONE) {
        const pxa_audio_session_t *session = &service->sessions[index];
        if (session->component == component) {
            count++;
        }
        index = session->next;
    }
    return count;
}

static pxa_audio_session_t *allocate_session(pxa_audio_service_t *service) {
    uint16_t index = service->free_head;
    pxa_audio_session_t *session;
    if (index == PXA_AUDIO_SLOT_NONE) return NULL;
    session = &service->sessions[index];
    service->free_head = session->next;
    session->next = service->active_head;
    session->previous = PXA_AUDIO_SLOT_NONE;
    if (service->active_head != PXA_AUDIO_SLOT_NONE) {
        service->sessions[service->active_head].previous = index;
    }
    service->active_head = index;
    session->provider_session = 0;
    memset(&session->format, 0, sizeof(session->format));
    session->handle = PXA_HANDLE_INVALID;
    return session;
}

static void release_session(pxa_audio_session_t *session) {
    pxa_audio_service_t *service = session->service;
    uint16_t index = (uint16_t)(session - service->sessions);
    if (session->previous == PXA_AUDIO_SLOT_NONE) {
        service->active_head = session->next;
    } else {
        service->sessions[session->previous].next = session->next;
    }
    if (session->next != PXA_AUDIO_SLOT_NONE) {
        service->sessions[session->next].previous = session->previous;
    }
    session->component = PXA_COMPONENT_INVALID;
    session->handle = PXA_HANDLE_INVALID;
    session->provider_session = 0;
    memset(&session->format, 0, sizeof(session->format));
    session->next = service->free_head;
    session->previous = PXA_AUDIO_SLOT_NONE;
    service->free_head = index;
}

static void close_session(void *context) {
    pxa_audio_session_t *session = (pxa_audio_session_t *)context;
    if (session == NULL || session->magic != PXA_AUDIO_SLOT_MAGIC ||
        session->provider_session == 0) {
        return;
    }
    session->service->backend.close(session->service->backend.context,
                                    session->provider_session);
    release_session(session);
}

static int32_t audio_stream_io(void *context, uint32_t operation,
                               uint8_t *data, size_t size) {
    pxa_audio_session_t *session = (pxa_audio_session_t *)context;
    size_t sample_bytes;
    uint64_t frame_bytes;
    pxa_status_t status;
    if (session == NULL || session->magic != PXA_AUDIO_SLOT_MAGIC ||
        session->provider_session == 0 || session->service == NULL) {
        return PXA_STATUS_NOT_FOUND;
    }
    if (operation == PXA_AUDIO_IO_PLAY_TONE) {
        pxa_audio_tone_t tone;
        if (data == NULL || size != 8 ||
            session->service->backend.play_tone == NULL) {
            return data == NULL || size != 8 ? PXA_STATUS_INVALID_ARGUMENT
                                             : PXA_STATUS_UNSUPPORTED;
        }
        tone.frequency_hz = pxa_read_u16(data);
        tone.duration_ms = pxa_read_u16(data + 2);
        tone.gain_db_q8 = read_i16(data + 4);
        tone.waveform = data[6];
        if (data[7] != 0 || tone.frequency_hz < 40 ||
            tone.frequency_hz > 8000 || tone.duration_ms < 10 ||
            tone.duration_ms > 1000 || tone.gain_db_q8 > 0 ||
            tone.gain_db_q8 < -60 * 256 ||
            tone.waveform > PXA_AUDIO_TONE_NOISE) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        status = pxa_status_normalize(session->service->backend.play_tone(
            session->service->backend.context, session->provider_session,
            &tone));
        return status == PXA_STATUS_OK ? (int32_t)size : status;
    }
    if (operation == PXA_AUDIO_IO_PLAY_ASSET) {
        pxa_audio_asset_t asset;
        uint16_t path_size;
        if (data == NULL || size < 9 ||
            session->service->backend.play_asset == NULL) {
            return data == NULL || size < 9 ? PXA_STATUS_INVALID_ARGUMENT
                                            : PXA_STATUS_UNSUPPORTED;
        }
        path_size = pxa_read_u16(data);
        asset.gain_db_q8 = read_i16(data + 2);
        asset.flags = data[4];
        asset.path = data + 8;
        asset.path_size = path_size;
        if ((size_t)path_size != size - 8u || path_size == 0 ||
            data[5] != 0 || data[6] != 0 || data[7] != 0 ||
            (asset.flags & ~PXA_AUDIO_ASSET_LOOP) != 0 ||
            asset.gain_db_q8 > 0 || asset.gain_db_q8 < -60 * 256) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        status = pxa_status_normalize(session->service->backend.play_asset(
            session->service->backend.context, session->provider_session,
            &asset));
        return status == PXA_STATUS_OK ? (int32_t)size : status;
    }
    if (operation == PXA_AUDIO_IO_CONTROL_ASSET) {
        pxa_audio_asset_control_t control;
        if (data == NULL || size != 4 ||
            session->service->backend.control_asset == NULL) {
            return data == NULL || size != 4 ? PXA_STATUS_INVALID_ARGUMENT
                                             : PXA_STATUS_UNSUPPORTED;
        }
        control.action = data[0];
        control.gain_db_q8 = read_i16(data + 2);
        if (data[1] != 0 || control.action < PXA_AUDIO_ASSET_PAUSE ||
            control.action > PXA_AUDIO_ASSET_SET_GAIN ||
            (control.action == PXA_AUDIO_ASSET_SET_GAIN
                 ? (control.gain_db_q8 < -60 * 256 ||
                    control.gain_db_q8 > 0)
                 : control.gain_db_q8 != 0)) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        status = pxa_status_normalize(session->service->backend.control_asset(
            session->service->backend.context, session->provider_session,
            &control));
        return status == PXA_STATUS_OK ? (int32_t)size : status;
    }
    if (operation != PXA_IO_WRITE) return PXA_STATUS_UNSUPPORTED;
    if (size == 0) return 0;
    sample_bytes = (size_t)session->format.channels * 2u;
    if (data == NULL || sample_bytes == 0 || size % sample_bytes != 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    frame_bytes = (uint64_t)session->format.sample_rate *
                  session->format.frame_ms * sample_bytes / 1000u;
    if (frame_bytes == 0 || frame_bytes > SIZE_MAX || size > frame_bytes) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    status = pxa_status_normalize(session->service->backend.submit(
        session->service->backend.context, session->provider_session, data,
        size));
    return status == PXA_STATUS_OK ? (int32_t)size : status;
}

static const pxa_resource_ops_t k_audio_resource_ops = {
    sizeof(pxa_resource_ops_t), audio_stream_io,
};

static pxa_status_t encode_open_result(uint8_t output[27],
                                       pxa_handle_t handle,
                                       const pxa_audio_format_t *format,
                                       size_t *result_size) {
    uint8_t value[4];
    pxa_writer_t writer;
    pxa_status_t status;
    pxa_writer_init(&writer, output, 27);
    pxa_write_u32(value, handle);
    status = pxa_writer_record(&writer, 3, value, 4);
    if (status == PXA_STATUS_OK) {
        pxa_write_u32(value, format->sample_rate);
        status = pxa_writer_record(&writer, 4, value, 4);
    }
    if (status == PXA_STATUS_OK) {
        status = pxa_writer_record(&writer, 5, &format->channels, 1);
    }
    if (status == PXA_STATUS_OK) {
        pxa_write_u16(value, format->frame_ms);
        status = pxa_writer_record(&writer, 6, value, 2);
    }
    if (status != PXA_STATUS_OK) return PXA_STATUS_INTERNAL;
    *result_size = writer.size;
    return PXA_STATUS_OK;
}

static pxa_status_t prepare_open(pxa_audio_service_t *service,
                                 pxa_component_t component,
                                 pxa_bytes_t payload,
                                 pxa_audio_open_request_t *request,
                                 pxa_authority_t *authority) {
    static const uint8_t permission_name[] = "audio.playback";
    static const uint8_t scope[] = "media";
    pxa_status_t status = parse_open(payload, request);
    if (status != PXA_STATUS_OK) return status;
    return pxa_permission_resolve(
        service->permissions, component, request->permission,
        (pxa_bytes_t){permission_name, sizeof(permission_name) - 1},
        (pxa_bytes_t){scope, sizeof(scope) - 1}, authority);
}

static pxa_status_t open_session(pxa_audio_service_t *service,
                                 pxa_component_t component,
                                 const pxa_audio_open_request_t *request,
                                 pxa_authority_t authority,
                                 uint8_t result[27], size_t *result_size,
                                 pxa_handle_t *opened_handle) {
    pxa_audio_format_t format;
    pxa_audio_session_t *session;
    pxa_resource_t resource;
    uint64_t provider_session = 0;
    pxa_status_t status;
    if (sessions_for_component(service, component) >=
        service->max_sessions_per_component) {
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    session = allocate_session(service);
    if (session == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    memset(&format, 0, sizeof(format));
    status = pxa_status_normalize(service->backend.open(
        service->backend.context, request->usage, &format, &provider_session));
    if (status != PXA_STATUS_OK) {
        if (provider_session != 0) {
            service->backend.close(service->backend.context,
                                   provider_session);
        }
        release_session(session);
        return status;
    }
    if (!format_valid(&format) || provider_session == 0) {
        if (provider_session != 0) {
            service->backend.close(service->backend.context,
                                   provider_session);
        }
        release_session(session);
        return PXA_STATUS_INTERNAL;
    }
    session->component = component;
    session->provider_session = provider_session;
    session->format = format;
    resource.context = session;
    resource.operations = &k_audio_resource_ops;
    resource.close = close_session;
    status = pxa_handle_open(service->runtime, component,
                             PXA_RESOURCE_AUDIO_GRAPH, authority, &resource,
                             opened_handle);
    if (status != PXA_STATUS_OK) {
        close_session(session);
        return status;
    }
    session->handle = *opened_handle;
    status = encode_open_result(result, *opened_handle, &format, result_size);
    if (status != PXA_STATUS_OK) {
        (void)pxa_handle_close(service->runtime, component, *opened_handle);
        *opened_handle = PXA_HANDLE_INVALID;
    }
    return status;
}

static pxa_status_t commit_graph(pxa_audio_service_t *service,
                                 pxa_component_t component,
                                 const pxa_message_view_t *message) {
    pxa_handle_t handle;
    pxa_audio_graph_t graph;
    pxa_resource_t resource;
    pxa_audio_session_t *session;
    pxa_status_t status = parse_graph(message->payload, service->max_eq_bands,
                                      &handle, &graph);
    if (status != PXA_STATUS_OK) return status;
    status = pxa_handle_get(service->runtime, component, handle,
                            PXA_RESOURCE_AUDIO_GRAPH, &resource);
    if (status != PXA_STATUS_OK) return status;
    session = (pxa_audio_session_t *)resource.context;
    if (session == NULL || session->magic != PXA_AUDIO_SLOT_MAGIC ||
        session->provider_session == 0 || session->service != service ||
        session->component != component || session->handle != handle) {
        return PXA_STATUS_INTERNAL;
    }
    return pxa_status_normalize(service->backend.commit(
        service->backend.context, session->provider_session, &graph));
}

static pxa_status_t resolve_session(pxa_audio_service_t *service,
                                    pxa_component_t component,
                                    pxa_bytes_t payload,
                                    pxa_audio_session_t **output) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    pxa_resource_t resource;
    pxa_handle_t handle;
    pxa_status_t status;
    *output = NULL;
    pxa_record_iterator_init(&iterator, payload);
    status = pxa_record_next(&iterator, &record);
    if (status != PXA_STATUS_OK || record.optional || record.tag != 1 ||
        record.payload.size != 4)
        return PXA_STATUS_INVALID_ARGUMENT;
    handle = pxa_read_u32(record.payload.data);
    if (pxa_record_next(&iterator, &record) != PXA_STATUS_WOULD_BLOCK)
        return PXA_STATUS_INVALID_ARGUMENT;
    status = pxa_handle_get(service->runtime, component, handle,
                            PXA_RESOURCE_AUDIO_GRAPH, &resource);
    if (status != PXA_STATUS_OK) return status;
    *output = (pxa_audio_session_t *)resource.context;
    if (*output == NULL || (*output)->magic != PXA_AUDIO_SLOT_MAGIC ||
        (*output)->provider_session == 0 || (*output)->service != service ||
        (*output)->component != component || (*output)->handle != handle) {
        *output = NULL;
        return PXA_STATUS_INTERNAL;
    }
    return PXA_STATUS_OK;
}

static pxa_status_t encode_state(uint8_t output[40],
                                 const pxa_audio_state_t *state,
                                 size_t *result_size) {
    uint8_t value[8];
    pxa_writer_t writer;
    pxa_status_t status;
    pxa_writer_init(&writer, output, 40);
    pxa_write_u64(value, state->submitted_samples);
    status = pxa_writer_record(&writer, 2, value, 8);
    if (status == PXA_STATUS_OK) {
        pxa_write_u64(value, state->accepted_samples);
        status = pxa_writer_record(&writer, 3, value, 8);
    }
    if (status == PXA_STATUS_OK) {
        pxa_write_u32(value, state->queued_samples);
        status = pxa_writer_record(&writer, 4, value, 4);
    }
    if (status == PXA_STATUS_OK) {
        pxa_write_u32(value, state->flags);
        status = pxa_writer_record(&writer, 5, value, 4);
    }
    if (status != PXA_STATUS_OK) return PXA_STATUS_INTERNAL;
    *result_size = writer.size;
    return PXA_STATUS_OK;
}

static pxa_status_t query_state(pxa_audio_service_t *service,
                                pxa_component_t component,
                                pxa_bytes_t payload, uint8_t result[40],
                                size_t *result_size) {
    pxa_audio_session_t *session;
    pxa_audio_state_t state;
    pxa_status_t status;
    if (service->backend.query == NULL) return PXA_STATUS_UNSUPPORTED;
    status = resolve_session(service, component, payload, &session);
    if (status != PXA_STATUS_OK) return status;
    memset(&state, 0, sizeof(state));
    status = pxa_status_normalize(service->backend.query(
        service->backend.context, session->provider_session, &state));
    if (status == PXA_STATUS_OK &&
        (state.accepted_samples > state.submitted_samples ||
         state.queued_samples > state.submitted_samples))
        return PXA_STATUS_INTERNAL;
    return status == PXA_STATUS_OK
               ? encode_state(result, &state, result_size)
               : status;
}

static pxa_status_t flush_session(pxa_audio_service_t *service,
                                  pxa_component_t component,
                                  pxa_bytes_t payload) {
    pxa_audio_session_t *session;
    pxa_status_t status;
    if (service->backend.flush == NULL) return PXA_STATUS_UNSUPPORTED;
    status = resolve_session(service, component, payload, &session);
    if (status != PXA_STATUS_OK) return status;
    return pxa_status_normalize(service->backend.flush(
        service->backend.context, session->provider_session));
}

static pxa_status_t audio_control(void *context, pxa_runtime_t *runtime,
                                  pxa_component_t component,
                                  const pxa_message_view_t *message) {
    pxa_audio_service_t *service = (pxa_audio_service_t *)context;
    uint8_t result[40] = {0};
    size_t result_size = 0;
    pxa_handle_t opened_handle = PXA_HANDLE_INVALID;
    pxa_audio_open_request_t open_request;
    pxa_authority_t authority = 0;
    pxa_status_t status;
    pxa_status_t begin;
    pxa_status_t complete;
    (void)runtime;
    if (!service_valid(service) || message->request_id == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (message->opcode != PXA_AUDIO_OPEN_SESSION &&
        message->opcode != PXA_AUDIO_COMMIT_GRAPH &&
        message->opcode != PXA_AUDIO_QUERY_STATE &&
        message->opcode != PXA_AUDIO_FLUSH) {
        return PXA_STATUS_UNSUPPORTED;
    }
    if (message->opcode == PXA_AUDIO_OPEN_SESSION) {
        status = prepare_open(service, component, message->payload,
                              &open_request, &authority);
    } else {
        status = PXA_STATUS_OK;
    }
    begin = pxa_request_begin(service->runtime, component, message->request_id,
                              PXA_AUDIO_SERVICE_ID, message->opcode,
                              status == PXA_STATUS_OK ? authority : 0);
    if (begin != PXA_STATUS_OK) {
        return begin;
    }
    if (status == PXA_STATUS_OK &&
        message->opcode == PXA_AUDIO_OPEN_SESSION) {
        status = open_session(service, component, &open_request, authority,
                              result, &result_size, &opened_handle);
    } else if (message->opcode == PXA_AUDIO_COMMIT_GRAPH) {
        status = commit_graph(service, component, message);
    } else if (message->opcode == PXA_AUDIO_QUERY_STATE) {
        status = query_state(service, component, message->payload, result,
                             &result_size);
    } else if (message->opcode == PXA_AUDIO_FLUSH) {
        status = flush_session(service, component, message->payload);
    }
    complete = pxa_request_complete(
        service->runtime, component, message->request_id, status,
        status == PXA_STATUS_OK &&
                (message->opcode == PXA_AUDIO_OPEN_SESSION ||
                 message->opcode == PXA_AUDIO_QUERY_STATE)
            ? result
            : NULL,
        status == PXA_STATUS_OK &&
                (message->opcode == PXA_AUDIO_OPEN_SESSION ||
                 message->opcode == PXA_AUDIO_QUERY_STATE)
            ? result_size
            : 0);
    if (complete != PXA_STATUS_OK) {
        if (opened_handle != PXA_HANDLE_INVALID) {
            (void)pxa_handle_close(service->runtime, component, opened_handle);
        }
        (void)pxa_request_cancel(service->runtime, component,
                                 message->request_id);
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_audio_service_register(pxa_audio_service_t *service) {
    pxa_service_ops_t operations;
    pxa_status_t status;
    if (!service_valid(service)) return PXA_STATUS_INVALID_ARGUMENT;
    if (service->registered) return PXA_STATUS_BAD_STATE;
    memset(&operations, 0, sizeof(operations));
    operations.struct_size = sizeof(operations);
    operations.service_id = PXA_AUDIO_SERVICE_ID;
    operations.major = PXA_AUDIO_SERVICE_MAJOR;
    operations.minor = PXA_AUDIO_SERVICE_MINOR;
    operations.context = service;
    operations.control = audio_control;
    status = pxa_service_register(service->runtime, &operations);
    if (status == PXA_STATUS_OK) service->registered = 1;
    return status;
}

int pxa_audio_has_active_sessions(const pxa_audio_service_t *service) {
    return service_valid(service) &&
           service->active_head != PXA_AUDIO_SLOT_NONE;
}
