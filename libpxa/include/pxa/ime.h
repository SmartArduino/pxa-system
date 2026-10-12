#ifndef PXA_IME_H
#define PXA_IME_H

#include "pxa/package.h"
#include "pxa/activation.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Optional system-owned input-method channel. Providers never receive editor
 * pointers or surrounding/password text. All strings are well-formed UTF-8. */
#define PXA_IME_SERVICE_ID UINT16_C(22)
#define PXA_IME_SERVICE_MAJOR UINT16_C(0)
#define PXA_IME_SERVICE_MINOR UINT16_C(1)
#define PXA_IME_FEATURE_PROVIDER (UINT64_C(1) << 0)
#define PXA_IME_FEATURE_PINYIN (UINT64_C(1) << 1)
#define PXA_IME_FEATURE_LATIN (UINT64_C(1) << 2)
#define PXA_IME_FEATURE_T9 (UINT64_C(1) << 3)
#define PXA_IME_FEATURE_ALL UINT64_C(15)
#define PXA_IME_PRESENT UINT16_C(1)
#define PXA_IME_INPUT UINT16_C(0x8001)
#define PXA_IME_INPUT_HEADER_BYTES 24u
#define PXA_IME_FRAME_HEADER_BYTES 32u

typedef enum {
    PXA_IME_RESET = 1, PXA_IME_TEXT, PXA_IME_BACKSPACE, PXA_IME_CONFIRM,
    PXA_IME_CHOOSE, PXA_IME_PAGE, PXA_IME_CHOOSE_PINYIN,
} pxa_ime_action_t;
typedef enum { PXA_IME_K26, PXA_IME_K9 } pxa_ime_layout_t;
typedef enum { PXA_IME_CHINESE, PXA_IME_ENGLISH } pxa_ime_language_t;
/* Host preference only: not a Guest wire format. Zero preserves automatic
 * layout and Chinese input; password editors always use English. */
typedef enum { PXA_IME_LAYOUT_AUTO, PXA_IME_LAYOUT_FULL, PXA_IME_LAYOUT_NINE } pxa_ime_layout_preference_t;
typedef struct { uint8_t layout, language; } pxa_ime_preferences_t;
static inline bool pxa_ime_preferences_valid(pxa_ime_preferences_t value) {
    return value.layout<=PXA_IME_LAYOUT_NINE && value.language<=PXA_IME_ENGLISH;
}
typedef enum {
    PXA_IME_EDIT_NONE, PXA_IME_EDIT_INSERT, PXA_IME_EDIT_BACKSPACE,
    PXA_IME_EDIT_SUBMIT, PXA_IME_EDIT_CLEAR,
} pxa_ime_edit_t;
#define PXA_IME_SENSITIVE UINT16_C(1)
#define PXA_IME_MORE_CANDIDATES UINT16_C(1)
#define PXA_IME_SUBMIT UINT16_C(2)
#define PXA_IME_CONFIG_DICTIONARY UINT16_C(0x800d)

typedef struct {
    uint64_t session;
    uint32_t sequence;
    uint16_t action, flags;
    uint8_t layout, language;
    uint32_t value;
    pxa_bytes_t text;
} pxa_ime_input_t;

/* Borrowed only until the presentation callback returns. Items holds first
 * candidate_count candidate records, then pinyin_count spelling records.
 * Each record is length:u16 followed by nonempty UTF-8, without a NUL. */
typedef struct {
    uint64_t session;
    uint32_t sequence, page_start;
    uint16_t flags, edit, candidate_count, pinyin_count;
    pxa_bytes_t preedit, commit, items;
} pxa_ime_frame_t;

pxa_status_t pxa_ime_decode_input(pxa_bytes_t bytes, pxa_ime_input_t *output);
pxa_status_t pxa_ime_decode_frame(pxa_bytes_t bytes, pxa_ime_frame_t *output);
pxa_status_t pxa_ime_frame_item(const pxa_ime_frame_t *frame, uint16_t index,
                                pxa_bytes_t *output);

typedef pxa_status_t (*pxa_ime_present_fn)(void *context,
                                         const pxa_ime_frame_t *frame);
typedef struct {
    uint32_t struct_size;
    void *context;
    /* NULL frame reports provider loss. The UI must schedule fallback, never
     * enter another Guest or destroy the current Guest from this callback. */
    pxa_ime_present_fn present;
} pxa_ime_config_t;
typedef struct pxa_ime_service pxa_ime_service_t;

size_t pxa_ime_service_workspace_size(void);
pxa_status_t pxa_ime_service_init(void *workspace, size_t bytes,
    pxa_runtime_t *runtime, const pxa_ime_config_t *config,
    pxa_ime_service_t **output);
pxa_status_t pxa_ime_service_register(pxa_ime_service_t *service);
/* Host-only. A fresh session invalidates all replies from previous editors.
 * The provider must already be running; no Guest is entered synchronously. */
pxa_status_t pxa_ime_begin(pxa_ime_service_t *service,
    pxa_component_t provider, uint16_t flags, uint64_t *session);
void pxa_ime_end(pxa_ime_service_t *service, uint64_t session);
pxa_status_t pxa_ime_send(pxa_ime_service_t *service, uint64_t session,
    uint16_t action, uint8_t layout, uint8_t language, uint32_t value,
    pxa_bytes_t text);
uint32_t pxa_ime_pending(const pxa_ime_service_t *service);

/* Discovery uses signed service metadata, without starting the Guest. The
 * caller authenticates the package and retains its manifest through unload. */
int pxa_ime_is_provider(const pxa_package_component_t *component);
pxa_status_t pxa_ime_select_provider(const pxa_package_manifest_t *manifest,
    pxa_bytes_t component_id, const pxa_package_activation_profile_t *services,
    const pxa_package_host_profile_t *host, pxa_activation_entry_t *output);

#ifdef __cplusplus
}
#endif
#endif
