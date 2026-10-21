#ifndef PXA_GUEST_IME_H
#define PXA_GUEST_IME_H

#include "pxa_core.h"
#include "pxa_fs_path.h"
#include <string.h>

#define PXA_IME_SERVICE 22u
#define PXA_IME_PRESENT 1u
#define PXA_IME_INPUT 0x8001u
#define PXA_IME_ACTION_RESET 1u
#define PXA_IME_ACTION_TEXT 2u
#define PXA_IME_ACTION_BACKSPACE 3u
#define PXA_IME_ACTION_CONFIRM 4u
#define PXA_IME_ACTION_CHOOSE 5u
#define PXA_IME_ACTION_PAGE 6u
#define PXA_IME_ACTION_CHOOSE_PINYIN 7u
#define PXA_IME_LAYOUT_FULL 0u
#define PXA_IME_LAYOUT_NINE 1u
#define PXA_IME_LANGUAGE_CHINESE 0u
#define PXA_IME_LANGUAGE_ENGLISH 1u
#define PXA_IME_EDIT_NONE 0u
#define PXA_IME_EDIT_INSERT 1u
#define PXA_IME_EDIT_BACKSPACE 2u
#define PXA_IME_EDIT_SUBMIT 3u
#define PXA_IME_EDIT_CLEAR 4u

typedef struct {
    uint64_t session;
    uint32_t sequence;
    uint16_t action;
    uint8_t sensitive, layout, language;
    uint32_t value;
    const char *text; /* Borrowed from the input event. */
    size_t text_size;
} pxa_ime_input_t;

static inline int pxa_ime_text_valid(const char *text, size_t size) {
    return size == 0 || (text && !memchr(text, 0, size) && pxa_fs_valid_utf8(text, size));
}

static inline int pxa_ime_parse_input(const pxa_event_t *event, pxa_ime_input_t *out) {
    const uint8_t *p;
    if (!out) return 0;
    pxa_zero(out, sizeof(*out));
    if (!event || event->service != PXA_IME_SERVICE || event->opcode != PXA_IME_INPUT ||
        event->token || !event->payload || event->payload_size < 24 ||
        event->payload_size > PXA_MAX_CONTROL_BYTES - PXA_HEADER_BYTES) return 0;
    p = event->payload;
    out->session = pxa_load_u64(p);
    out->sequence = pxa_load_u32(p + 8);
    out->action = pxa_load_u16(p + 12);
    if (!out->session || !out->sequence || out->action < 1 || out->action > 7 ||
        pxa_load_u16(p + 14) > 1 || p[16] > 1 || p[17] > 1 || pxa_load_u16(p + 18) ||
        (out->action != PXA_IME_ACTION_TEXT && event->payload_size != 24) ||
        !pxa_ime_text_valid((const char *)p + 24, event->payload_size - 24)) return 0;
    out->sensitive = p[14]; out->layout = p[16]; out->language = p[17];
    out->value = pxa_load_u32(p + 20);
    out->text = (const char *)p + 24; out->text_size = event->payload_size - 24;
    return 1;
}

/* Caller-owned payload storage. No heap, implicit event loop or engine state.
 * Text passed to composition/item must not overlap the frame buffer. */
typedef struct {
    uint8_t *data;
    size_t capacity, size;
    uint16_t candidates, spellings;
} pxa_ime_frame_t;

static inline int32_t pxa_ime_frame_begin(pxa_ime_frame_t *frame,
    uint8_t *storage, size_t capacity, const pxa_ime_input_t *input,
    const char *preedit, size_t preedit_size, uint16_t edit,
    const char *commit, size_t commit_size) {
    if (!frame || !storage || !input || !input->session || !input->sequence ||
        capacity < 32 || preedit_size > UINT16_MAX || commit_size > UINT16_MAX ||
        preedit_size + commit_size > capacity - 32 ||
        preedit_size + commit_size > PXA_MAX_CONTROL_BYTES - PXA_HEADER_BYTES - 32 ||
        edit > 4 || ((edit == PXA_IME_EDIT_INSERT) != (commit_size != 0)) ||
        !pxa_ime_text_valid(preedit, preedit_size) ||
        !pxa_ime_text_valid(commit, commit_size)) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_zero(storage, 32);
    pxa_store_u64(storage, input->session);
    pxa_store_u32(storage + 8, input->sequence);
    pxa_store_u16(storage + 14, edit);
    pxa_store_u16(storage + 20, (uint16_t)preedit_size);
    pxa_store_u16(storage + 22, (uint16_t)commit_size);
    if (preedit_size) memcpy(storage + 32, preedit, preedit_size);
    if (commit_size) memcpy(storage + 32 + preedit_size, commit, commit_size);
    frame->data = storage;
    frame->capacity = capacity < PXA_MAX_CONTROL_BYTES - PXA_HEADER_BYTES
        ? capacity : PXA_MAX_CONTROL_BYTES - PXA_HEADER_BYTES;
    frame->size = 32 + preedit_size + commit_size;
    frame->candidates = frame->spellings = 0;
    return PXA_STATUS_OK;
}

static inline int32_t pxa_ime_frame_item(pxa_ime_frame_t *frame,
    const char *text, size_t size, int spelling) {
    if (!frame || !frame->data || frame->size < 32 || frame->size > frame->capacity)
        return PXA_STATUS_BAD_STATE;
    if ((!spelling && frame->spellings) ||
        (spelling ? frame->spellings : frame->candidates) == UINT16_MAX)
        return PXA_STATUS_BAD_STATE;
    if (!size || size > UINT16_MAX || !pxa_ime_text_valid(text, size))
        return PXA_STATUS_INVALID_ARGUMENT;
    if (size + 2 > frame->capacity - frame->size) return PXA_STATUS_RESOURCE_LIMIT;
    pxa_store_u16(frame->data + frame->size, (uint16_t)size);
    memcpy(frame->data + frame->size + 2, text, size);
    frame->size += 2 + size;
    if (spelling) ++frame->spellings; else ++frame->candidates;
    return PXA_STATUS_OK;
}

static inline int32_t pxa_ime_frame_candidate(pxa_ime_frame_t *frame,
    const char *text, size_t size) {
    return pxa_ime_frame_item(frame, text, size, 0);
}
static inline int32_t pxa_ime_frame_spelling(pxa_ime_frame_t *frame,
    const char *text, size_t size) {
    return pxa_ime_frame_item(frame, text, size, 1);
}
static inline int32_t pxa_ime_frame_present(pxa_ime_frame_t *frame,
    uint32_t first, int more, int submit) {
    if (!frame || !frame->data || frame->size < 32 || frame->size > frame->capacity)
        return PXA_STATUS_BAD_STATE;
    if (first > UINT32_MAX - frame->candidates) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_store_u16(frame->data + 12, (uint16_t)((more ? 1 : 0) | (submit ? 2 : 0)));
    pxa_store_u32(frame->data + 16, first);
    pxa_store_u16(frame->data + 24, frame->candidates);
    pxa_store_u16(frame->data + 26, frame->spellings);
    return pxa_control_call(PXA_IME_SERVICE, PXA_IME_PRESENT, 0,
                            frame->data, (uint32_t)frame->size);
}

#endif
