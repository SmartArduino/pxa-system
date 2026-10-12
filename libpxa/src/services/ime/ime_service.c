#include "pxa/ime.h"
#include "pxa/service.h"
#include "common/bytes_internal.h"
#include "common/checked_math.h"

#include <string.h>

#define IME_MAGIC UINT32_C(0x5058494d)
struct pxa_ime_service {
    uint32_t magic;
    pxa_runtime_t *runtime;
    pxa_ime_config_t config;
    uint64_t generation, session;
    pxa_component_t provider;
    uint32_t sent, received;
    uint16_t flags;
    uint8_t registered;
};

static int text_valid(pxa_bytes_t text) {
    return (text.data != NULL || text.size == 0) &&
        (text.size == 0 || memchr(text.data, 0, text.size) == NULL) &&
        pxa_utf8_validate(text.data, text.size, 0);
}

pxa_status_t pxa_ime_decode_input(pxa_bytes_t bytes, pxa_ime_input_t *out) {
    const uint8_t *p = bytes.data;
    if (out == NULL || p == NULL || bytes.size < PXA_IME_INPUT_HEADER_BYTES ||
        bytes.size > PXA_WIRE_MAX_CONTROL_MESSAGE - PXA_WIRE_SIZE)
        return PXA_STATUS_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    out->session = pxa_read_u64(p); out->sequence = pxa_read_u32(p + 8);
    out->action = pxa_read_u16(p + 12); out->flags = pxa_read_u16(p + 14);
    out->layout = p[16]; out->language = p[17];
    out->value = pxa_read_u32(p + 20);
    out->text = (pxa_bytes_t){p + 24, bytes.size - 24};
    if (out->session == 0 || out->sequence == 0 ||
        out->action < PXA_IME_RESET || out->action > PXA_IME_CHOOSE_PINYIN ||
        (out->flags & ~PXA_IME_SENSITIVE) || out->layout > PXA_IME_K9 ||
        out->language > PXA_IME_ENGLISH || pxa_read_u16(p + 18) ||
        !text_valid(out->text) ||
        (out->action != PXA_IME_TEXT && out->text.size != 0))
        return PXA_STATUS_INVALID_ARGUMENT;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_ime_frame_item(const pxa_ime_frame_t *frame, uint16_t index,
                                pxa_bytes_t *out) {
    size_t offset = 0;
    if (frame == NULL || out == NULL ||
        (uint32_t)index >= (uint32_t)frame->candidate_count + frame->pinyin_count)
        return PXA_STATUS_INVALID_ARGUMENT;
    for (uint32_t i = 0; i <= index; ++i) {
        if (frame->items.data == NULL || offset > frame->items.size ||
            frame->items.size - offset < 2) return PXA_STATUS_INVALID_ARGUMENT;
        size_t length = pxa_read_u16(frame->items.data + offset); offset += 2;
        if (length > frame->items.size - offset) return PXA_STATUS_INVALID_ARGUMENT;
        if (i == index) {
            *out = (pxa_bytes_t){frame->items.data + offset, length};
            return PXA_STATUS_OK;
        }
        offset += length;
    }
    return PXA_STATUS_INVALID_ARGUMENT;
}

pxa_status_t pxa_ime_decode_frame(pxa_bytes_t bytes, pxa_ime_frame_t *out) {
    const uint8_t *p = bytes.data;
    size_t offset = PXA_IME_FRAME_HEADER_BYTES;
    if (out == NULL || p == NULL || bytes.size < offset ||
        bytes.size > PXA_WIRE_MAX_CONTROL_MESSAGE - PXA_WIRE_SIZE)
        return PXA_STATUS_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    out->session = pxa_read_u64(p); out->sequence = pxa_read_u32(p + 8);
    out->flags = pxa_read_u16(p + 12); out->edit = pxa_read_u16(p + 14);
    out->page_start = pxa_read_u32(p + 16);
    size_t preedit = pxa_read_u16(p + 20), commit = pxa_read_u16(p + 22);
    out->candidate_count = pxa_read_u16(p + 24);
    out->pinyin_count = pxa_read_u16(p + 26);
    if (out->session == 0 || out->sequence == 0 ||
        (out->flags & ~(PXA_IME_MORE_CANDIDATES | PXA_IME_SUBMIT)) || out->edit > PXA_IME_EDIT_CLEAR ||
        pxa_read_u32(p + 28) || preedit > bytes.size - offset ||
        out->page_start > UINT32_MAX - out->candidate_count)
        return PXA_STATUS_INVALID_ARGUMENT;
    out->preedit = (pxa_bytes_t){p + offset, preedit}; offset += preedit;
    if (commit > bytes.size - offset) return PXA_STATUS_INVALID_ARGUMENT;
    out->commit = (pxa_bytes_t){p + offset, commit}; offset += commit;
    if (!text_valid(out->preedit) || !text_valid(out->commit) ||
        (out->edit != PXA_IME_EDIT_INSERT && commit != 0) ||
        (out->edit == PXA_IME_EDIT_INSERT && commit == 0))
        return PXA_STATUS_INVALID_ARGUMENT;
    out->items = (pxa_bytes_t){p + offset, bytes.size - offset};
    for (uint32_t i = 0; i < (uint32_t)out->candidate_count + out->pinyin_count; ++i) {
        if (bytes.size - offset < 2) return PXA_STATUS_INVALID_ARGUMENT;
        size_t length = pxa_read_u16(p + offset); offset += 2;
        if (length == 0 || length > bytes.size - offset ||
            !text_valid((pxa_bytes_t){p + offset, length}))
            return PXA_STATUS_INVALID_ARGUMENT;
        offset += length;
    }
    return offset == bytes.size ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
}

size_t pxa_ime_service_workspace_size(void) {
    return sizeof(pxa_ime_service_t) + PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
}

pxa_status_t pxa_ime_service_init(void *workspace, size_t bytes,
    pxa_runtime_t *runtime, const pxa_ime_config_t *config,
    pxa_ime_service_t **out) {
    if (out == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *out = NULL;
    if (workspace == NULL || bytes < pxa_ime_service_workspace_size() ||
        runtime == NULL || config == NULL || config->struct_size < sizeof(*config) ||
        config->present == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_ime_service_t *s = (pxa_ime_service_t *)pxa_internal_align_pointer(
        (uintptr_t)workspace, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    memset(s, 0, sizeof(*s)); s->magic = IME_MAGIC;
    s->runtime = runtime; s->config = *config; *out = s;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_ime_begin(pxa_ime_service_t *s, pxa_component_t provider,
    uint16_t flags, uint64_t *session) {
    pxa_component_snapshot_t state;
    if (s == NULL || s->magic != IME_MAGIC || session == NULL ||
        (flags & ~PXA_IME_SENSITIVE)) return PXA_STATUS_INVALID_ARGUMENT;
    if (pxa_component_snapshot(s->runtime, provider, &state) != PXA_STATUS_OK ||
        state.state != PXA_COMPONENT_RUNNING) return PXA_STATUS_BAD_STATE;
    if (s->generation == UINT64_MAX) return PXA_STATUS_RESOURCE_LIMIT;
    s->session = ++s->generation; s->provider = provider;
    s->flags = flags; s->sent = s->received = 0; *session = s->session;
    return PXA_STATUS_OK;
}

void pxa_ime_end(pxa_ime_service_t *s, uint64_t session) {
    if (s != NULL && s->magic == IME_MAGIC && s->session == session) {
        s->session = 0; s->provider = PXA_COMPONENT_INVALID;
        s->sent = s->received = 0;
    }
}

pxa_status_t pxa_ime_send(pxa_ime_service_t *s, uint64_t session,
    uint16_t action, uint8_t layout, uint8_t language, uint32_t value,
    pxa_bytes_t text) {
    uint8_t header[PXA_IME_INPUT_HEADER_BYTES] = {0};
    pxa_ime_input_t checked;
    if (s == NULL || s->magic != IME_MAGIC || session == 0 || s->session != session)
        return PXA_STATUS_BAD_STATE;
    if (s->sent == UINT32_MAX || text.size > PXA_WIRE_MAX_CONTROL_MESSAGE -
        PXA_WIRE_SIZE - sizeof(header))
        return PXA_STATUS_RESOURCE_LIMIT;
    if (!text_valid(text)) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_write_u64(header, session); pxa_write_u32(header + 8, s->sent + 1);
    pxa_write_u16(header + 12, action); pxa_write_u16(header + 14, s->flags);
    header[16] = layout; header[17] = language; pxa_write_u32(header + 20, value);
    if (pxa_ime_decode_input((pxa_bytes_t){header, sizeof(header)}, &checked) != PXA_STATUS_OK ||
        (action != PXA_IME_TEXT && text.size != 0)) return PXA_STATUS_INVALID_ARGUMENT;
    const pxa_bytes_t parts[] = {{header, sizeof(header)}, text};
    pxa_status_t status = pxa_event_post_messagev(s->runtime, s->provider,
        PXA_IME_SERVICE_ID, PXA_IME_INPUT, 0, parts, 2, 1, 0);
    if (status == PXA_STATUS_OK) ++s->sent;
    return status;
}

uint32_t pxa_ime_pending(const pxa_ime_service_t *s) {
    return s != NULL && s->magic == IME_MAGIC ? s->sent - s->received : 0;
}

static pxa_status_t control(void *context, pxa_runtime_t *runtime,
    pxa_component_t component, const pxa_message_view_t *message) {
    pxa_ime_service_t *s = context;
    pxa_ime_frame_t frame;
    (void)runtime;
    if (message == NULL || message->request_token != 0)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (message->opcode != PXA_IME_PRESENT) return PXA_STATUS_UNSUPPORTED;
    if (component != s->provider || s->session == 0) return PXA_STATUS_DENIED;
    pxa_status_t status = pxa_ime_decode_frame(message->payload, &frame);
    if (status != PXA_STATUS_OK) return status;
    if (frame.session != s->session || frame.sequence != s->received + 1 ||
        frame.sequence > s->sent) return PXA_STATUS_BAD_STATE;
    status = s->config.present(s->config.context, &frame);
    if (status == PXA_STATUS_OK) ++s->received;
    return status;
}

static void stopped(void *context, pxa_runtime_t *runtime, pxa_component_t component) {
    pxa_ime_service_t *s = context;
    (void)runtime;
    if (component == s->provider && s->session != 0) {
        pxa_ime_end(s, s->session);
        (void)s->config.present(s->config.context, NULL);
    }
}

pxa_status_t pxa_ime_service_register(pxa_ime_service_t *s) {
    if (s == NULL || s->magic != IME_MAGIC) return PXA_STATUS_INVALID_ARGUMENT;
    if (s->registered) return PXA_STATUS_BAD_STATE;
    pxa_service_ops_t ops = {0}; ops.struct_size = sizeof(ops);
    ops.service_id = PXA_IME_SERVICE_ID; ops.major = PXA_IME_SERVICE_MAJOR;
    ops.minor = PXA_IME_SERVICE_MINOR; ops.features = PXA_IME_FEATURE_ALL;
    ops.context = s; ops.control = control; ops.component_stopped = stopped;
    pxa_status_t status = pxa_service_register(s->runtime, &ops);
    if (status == PXA_STATUS_OK) s->registered = 1;
    return status;
}

int pxa_ime_is_provider(const pxa_package_component_t *component) {
    if (component == NULL || component->kind != PXA_COMPONENT_KIND_SERVICE) return 0;
    for (uint16_t i = 0; i < component->service_count; ++i)
        if (component->services[i].service == PXA_IME_SERVICE_ID &&
            (component->services[i].required_features & PXA_IME_FEATURE_PROVIDER)) return 1;
    return 0;
}

pxa_status_t pxa_ime_select_provider(const pxa_package_manifest_t *manifest,
    pxa_bytes_t component_id, const pxa_package_activation_profile_t *services,
    const pxa_package_host_profile_t *host, pxa_activation_entry_t *out) {
    if (manifest == NULL || services == NULL || host == NULL || out == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    for (uint16_t i = 0; i < manifest->component_count; ++i) {
        const pxa_package_component_t *c = &manifest->components[i];
        if (!pxa_ime_is_provider(c) || (component_id.size &&
            (c->id.size != component_id.size || component_id.data == NULL ||
             memcmp(c->id.data, component_id.data, c->id.size)))) continue;
        pxa_status_t status = pxa_package_requirements_validate(manifest, c, services);
        if (status != PXA_STATUS_OK) return status;
        status = pxa_package_artifact_select(c, host, &out->artifact);
        if (status != PXA_STATUS_OK) return status;
        out->component = c; out->core_major = manifest->min_sdk.major;
        out->core_minor = manifest->min_sdk.minor;
        return PXA_STATUS_OK;
    }
    return PXA_STATUS_NOT_FOUND;
}
