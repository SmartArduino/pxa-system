/* PXA App Store.
 *
 * Browses the signed device catalog (GET /api/v2/catalog), shows compatibility,
 * permission and artifact metadata, and reports what installation would need.
 *
 * The Guest SDK currently exposes no package-installer service, so this App
 * cannot hand a downloaded container to the Host installer. The installation
 * panel states that gap precisely; see README.md. */

#include "pxa_app_messages.h"
#include "pxa_device.h"
#include "pxa_i18n.h"
#include "pxa_log.h"
#include "pxa_net.h"
#include "pxa_permission.h"
#include "pxa_ui.h"

#include "store_client.h"
#include "store_json.h"
#include "store_model.h"

#define STORE_SCREEN_CATALOG 0
#define STORE_SCREEN_DETAIL 1
#define STORE_SCREEN_SEARCH 2

#define STORE_IDLE 0
#define STORE_LOADING 1
#define STORE_READY 2
#define STORE_DENIED 3
#define STORE_FAILED 4

#define STORE_ERROR_NONE 0
#define STORE_ERROR_PERMISSION 1
#define STORE_ERROR_UNSUPPORTED 2
#define STORE_ERROR_UNAVAILABLE 3
#define STORE_ERROR_TIMEOUT 4
#define STORE_ERROR_LIMIT 5
#define STORE_ERROR_PROTOCOL 6
#define STORE_ERROR_NOT_FOUND 7
#define STORE_ERROR_HTTP 8
#define STORE_ERROR_FAILED 9

/* Empty catalog pages are possible because the Host applies rollout policy per
 * item, so a bounded number of pages is chained without user interaction. */
#define STORE_MAX_CHAIN 4

/* Set to 1 to trace the request state machine through the Host Log service. */
#define STORE_TRACE 0

/* Search field shape. The text-input variant reports edits through text events
 * and lets a Host input method attach to the field; it needs a Host that
 * forwards PXA_UI_EVENT_TEXT, so it stays off while the firmware in the field
 * does not carry the text payload yet. */
#define STORE_SEARCH_TEXT_INPUT 1

/* Debug: open the search screen directly so the text input and the system
 * input method can be checked without tapping the header. */
#define STORE_DEBUG_START_SEARCH 0

#define NODE_ROOT 1u
#define NODE_HEADER 2u
#define NODE_HEADER_TITLE 3u
#define NODE_REFRESH 4u
#define NODE_REFRESH_ICON 5u
#define NODE_REFRESH_LABEL 6u
#define NODE_FILTERS 7u
/* The status text lives in the footer next to the load-more button. */
#define NODE_STATUS_INLINE 49u
#define NODE_STATUS 8u
#define NODE_STATUS_TEXT 22u
#define NODE_COUNT 9u
#define NODE_LIST 10u
#define NODE_EMPTY 11u
#define NODE_FOOTER 12u
#define NODE_LOAD_MORE 13u
#define NODE_LOAD_MORE_LABEL 14u
#define NODE_BACK 15u
#define NODE_BACK_LABEL 16u
#define NODE_DETAIL_SCROLL 17u
#define NODE_DETAIL_STATUS 20u
#define NODE_HEADER_SEPARATOR 21u
#define NODE_INSTALL_BUTTON 18u
#define NODE_INSTALL_LABEL 19u
#define NODE_KIND_ALL 96u
#define NODE_KIND_BASE 100u
#define NODE_CATEGORY_ALL 97u
#define NODE_CATEGORY_BASE 120u
#define NODE_CHIP_STRIDE 4u
/* Search screen: fixed nodes 40..53, then one row per keyboard line. */
#define NODE_SEARCH_BACK 40u
#define NODE_SEARCH_BACK_LABEL 41u
#define NODE_SEARCH_TITLE 42u
#define NODE_SEARCH_CLEAR 43u
#define NODE_SEARCH_CLEAR_LABEL 44u
#define NODE_SEARCH_FIELD 45u
#define NODE_SEARCH_TEXT 46u
#define NODE_SEARCH_KEYBOARD 47u
#define NODE_SEARCH_ROW_BASE 50u
/* Catalog header search action; only used on the catalog surface. */
#define NODE_SEARCH_BUTTON 60u
#define NODE_SEARCH_BUTTON_LABEL 61u
/* Bottom tab bar and the header status line. */
#define NODE_TABBAR 62u
#define NODE_TAB_APPS 63u
#define NODE_TAB_APPS_LABEL 64u
#define NODE_TAB_APPS_MARK 65u
#define NODE_TAB_GAMES 66u
#define NODE_TAB_GAMES_LABEL 67u
#define NODE_TAB_GAMES_MARK 68u
#define NODE_HEADER_STATUS 69u
#define NODE_HEADER_ROW 70u
#define NODE_KEY_BASE 300u
#define NODE_KEY_COUNT 28u
#define NODE_KEY_STRIDE 2u
#define NODE_DETAIL_HERO 30u
#define NODE_DETAIL_HERO_ICON 31u
#define NODE_DETAIL_HERO_LETTER 32u
#define NODE_DETAIL_HERO_INFO 33u
#define NODE_DETAIL_HERO_NAME 34u
#define NODE_DETAIL_HERO_META 35u
#define NODE_CARD_BASE 200u
#define NODE_CARD_STRIDE 12u
#define NODE_DETAIL_SECTION_BASE 900u
#define NODE_DETAIL_SECTION_STRIDE 4u
#define NODE_DETAIL_ROW_BASE 1000u
#define NODE_DETAIL_ROW_STRIDE 4u

#define SECTION_SUMMARY 0u
#define SECTION_CHANGELOG 1u
#define SECTION_PERMISSIONS 2u
#define SECTION_SERVICES 3u
#define SECTION_INSTALL 4u

#define ROW_VERSION 0u
#define ROW_SIZE 1u
#define ROW_CHANNEL 2u
#define ROW_SEQUENCE 3u
#define ROW_PUBLISHER 4u
#define ROW_DIGEST 5u
#define ROW_PROFILE 6u
#define ROW_SDK 7u
#define ROW_PLATFORMS 8u

#define ICON_REFRESH "\xef\x80\xa1"

typedef struct {
    pxa_i18n_t i18n;
    store_client_t client;
    store_catalog_t catalog;
    store_app_t detail;
    uint8_t detail_ready;
    uint8_t detail_state;
    uint8_t screen;
    uint8_t state;
    uint8_t error;
    uint8_t selected;
    uint8_t busy;
    uint8_t chain;
    uint8_t install_feedback;
    /* A query submitted while a catalog request is in flight is applied as
     * soon as that request completes. */
    char pending_search[STORE_MAX_QUERY];
    uint8_t pending_search_valid;
    /* A restart (chip, tab, refresh or search) requested while a catalog
     * request is in flight is replayed when that request completes. */
    uint8_t pending_restart;
    /* Node the current drag started on, so its samples share one origin. */
    uint32_t pointer_node;
    /* Set while a gesture is a drag; a drag must never count as a click. */
    uint8_t drag_active;
    /* Last pointer sample on the list, to read the drag direction even when
     * the content has no room left to scroll. */
    int32_t pointer_y;
    uint8_t detail_target;
    uint8_t has_environment;
    uint8_t kind_index;
    int16_t category_entry;
    uint32_t pending_request;
    uint32_t safe_insets[4];
    uint32_t bar_insets[4];
    uint32_t width;
    uint32_t height;
    int32_t last_scroll;
    uint8_t chrome_hidden;
    uint8_t auto_load_mark;
    /* Item count of the last viewport fill request, so a server that stops
     * producing items cannot spin. */
    uint8_t fill_count;
    uint8_t snapshot_received;
    uint8_t snapshot_attempts;
    store_taxonomy_t taxonomy;
    char draft[STORE_MAX_QUERY];
    int32_t list_scroll;
    int32_t detail_scroll;
    uint32_t generation;
    uint64_t request_cursor;
    char query[STORE_MAX_QUERY];
    char url[512];
    char device_id[24];
    char scratch[384];
    char status[64];
    uint8_t packet[3072];
    uint8_t payload[1024];
    uint8_t body[PXA_NET_MAX_RESPONSE_BODY_BYTES];
} store_state_t;

static store_state_t app;

/* Layout metrics follow the Host-reported logical size so the same App is
 * comfortable on a 296x240 panel and on a large phone display. */
typedef struct {
    uint16_t header_height;
    uint16_t status_height;
    uint16_t card_height;
    uint16_t header_pad_h;
    uint16_t header_pad_v;
    uint16_t action_height;
    uint16_t action_pad_h;
    uint16_t chip_height;
    uint16_t chip_pad_h;
    uint16_t chip_gap;
    uint16_t chips_row_height;
    uint16_t card_pad;
    uint16_t card_gap;
    uint16_t card_radius;
    uint16_t card_icon;
    uint16_t list_pad_h;
    uint16_t list_gap;
    uint16_t tab_height;
    uint16_t hero_icon;
    uint16_t search_height;
    uint16_t key_height;
    uint16_t key_gap;
    uint8_t title_role;
} store_metrics_t;

/* header_height, status_height, card_height, header_pad_h, header_pad_v,
 * action_height, action_pad_h, chip_height, chip_pad_h, chip_gap,
 * chips_row_height, card_pad, card_gap, card_radius, card_icon, list_pad_h,
 * list_gap, tab_height, hero_icon, search_height, key_height, key_gap,
 * title_role */
static const store_metrics_t metrics_compact = {
    40, 0, 64, 12, 3, 24, 9, 24, 10, 6, 30, 6, 6, 9, 34, 12, 6, 34, 44, 32,
    25, 3, PXA_UI_FONT_ROLE_TITLE};
static const store_metrics_t metrics_regular = {
    60, 18, 84, 16, 4, 32, 12, 32, 14, 8, 42, 10, 8, 12, 48, 16, 9, 44, 56,
    42, 30, 4, PXA_UI_FONT_ROLE_TITLE};
static const store_metrics_t metrics_large = {
    74, 22, 104, 22, 8, 38, 16, 38, 18, 10, 52, 14, 10, 16, 60, 22, 12, 52,
    72, 50, 34, 5, PXA_UI_FONT_ROLE_HEADLINE};

static int set_chrome_visible(uint8_t visible);

static const store_metrics_t *metrics(void) {
    if (app.width < 340u) return &metrics_compact;
    if (app.width >= 480u) return &metrics_large;
    return &metrics_regular;
}


static const char *message(pxa_i18n_message_id_t id) {
    return pxa_i18n_cstr(&app.i18n, id);
}

/* The detail screen prefers the fetched single-item payload and falls back to
 * the catalog entry while the request is pending or after a failure. */
static const store_app_t *detail_item(void) {
    return app.detail_ready ? &app.detail
                           : store_catalog_at_const(&app.catalog, app.selected);
}

static size_t string_length(const char *value) {
    size_t size = 0;
    if (value == NULL) return 0;
    while (value[size] != '\0') ++size;
    return size;
}

static int text_equal(const char *left, const char *right) {
    size_t index = 0;
    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) return 0;
        ++index;
    }
    return left[index] == right[index];
}

static void copy_text(char *output, size_t capacity, const char *value) {
    size_t index = 0;
    if (capacity == 0) return;
    while (value != NULL && value[index] != '\0' && index + 1u < capacity) {
        output[index] = value[index];
        ++index;
    }
    output[index] = '\0';
}

static size_t append_text(char *output, size_t capacity, size_t offset,
                          const char *value) {
    size_t index = 0;
    if (capacity == 0) return 0;
    while (value != NULL && value[index] != '\0' && offset + 1u < capacity)
        output[offset++] = value[index++];
    output[offset] = '\0';
    return offset;
}

static size_t append_u64(char *output, size_t capacity, size_t offset,
                         uint64_t value) {
    char reverse[20];
    size_t count = 0;
    do {
        reverse[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0 && count < sizeof(reverse));
    while (count != 0 && offset + 1u < capacity)
        output[offset++] = reverse[--count];
    output[offset] = '\0';
    return offset;
}

/* Diagnostic channel used while bringing up a Host. It stays compiled out of
 * release packages. */
static void store_trace(const char *tag, uint32_t value) {
#if STORE_TRACE
    char message[64];
    size_t offset = append_text(message, sizeof(message), 0, "store ");
    offset = append_text(message, sizeof(message), offset, tag);
    offset = append_text(message, sizeof(message), offset, "=");
    (void)append_u64(message, sizeof(message), offset, value);
    (void)pxa_log_info(message);
#else
    (void)tag;
    (void)value;
#endif
}

static void format_size(char *output, size_t capacity, uint64_t bytes) {
    static const char units[] = "KMGT";
    uint64_t unit = 1024;
    uint8_t index = 0;
    size_t offset = 0;
    if (capacity == 0) return;
    output[0] = '\0';
    if (bytes < 1024u) {
        offset = append_u64(output, capacity, offset, bytes);
        (void)append_text(output, capacity, offset, " B");
        return;
    }
    while (index + 1u < sizeof(units) - 1u && bytes >= unit * 1024u) {
        unit *= 1024u;
        ++index;
    }
    offset = append_u64(output, capacity, offset, bytes / unit);
    if ((bytes % unit) * 10u / unit != 0) {
        offset = append_text(output, capacity, offset, ".");
        offset = append_u64(output, capacity, offset,
                            (bytes % unit) * 10u / unit);
    }
    offset = append_text(output, capacity, offset, " ");
    output[offset] = units[index];
    output[offset + 1u] = 'B';
    output[offset + 2u] = '\0';
}

/* Shows only the leading bytes of a long digest or key identifier. */
static void format_short_id(char *output, size_t capacity, const char *value) {
    size_t index = 0;
    if (capacity == 0) return;
    while (value != NULL && value[index] != '\0' && index + 4u < capacity &&
           index < 16u) {
        output[index] = value[index];
        ++index;
    }
    if (value != NULL && value[index] != '\0' && index + 4u < capacity) {
        output[index++] = '.';
        output[index++] = '.';
        output[index++] = '.';
    }
    output[index] = '\0';
}

static void format_service_requirement(char *output, size_t capacity,
                                       const store_service_t *service) {
    size_t offset = append_text(output, capacity, 0, service->name);
    offset = append_text(output, capacity, offset, " ");
    offset = append_u64(output, capacity, offset, service->min_major);
    offset = append_text(output, capacity, offset, ".");
    offset = append_u64(output, capacity, offset, service->min_minor);
    if (service->max_major != service->min_major ||
        service->max_minor != UINT16_MAX) {
        offset = append_text(output, capacity, offset, "-");
        offset = append_u64(output, capacity, offset, service->max_major);
        offset = append_text(output, capacity, offset, ".");
        (void)append_u64(output, capacity, offset, service->max_minor);
    }
}

static const char *error_text(void) {
    pxa_i18n_argument_t argument;
    char code[12];
    switch (app.error) {
        case STORE_ERROR_PERMISSION: return message(PXA_MSG_STATUS_PERMISSION);
        case STORE_ERROR_UNSUPPORTED: return message(PXA_MSG_STATUS_UNSUPPORTED);
        case STORE_ERROR_UNAVAILABLE: return message(PXA_MSG_STATUS_UNAVAILABLE);
        case STORE_ERROR_TIMEOUT: return message(PXA_MSG_STATUS_TIMEOUT);
        case STORE_ERROR_LIMIT: return message(PXA_MSG_STATUS_LIMIT);
        case STORE_ERROR_PROTOCOL: return message(PXA_MSG_STATUS_PROTOCOL);
        case STORE_ERROR_NOT_FOUND: return message(PXA_MSG_STATUS_NOT_FOUND);
        case STORE_ERROR_HTTP:
            code[0] = '\0';
            (void)append_u64(code, sizeof(code), 0, app.client.http_status);
            argument.name = "code";
            argument.name_size = 4u;
            argument.type = PXA_I18N_ARGUMENT_STRING;
            argument.value.string.data = code;
            argument.value.string.size = string_length(code);
            (void)pxa_i18n_format(&app.i18n, PXA_MSG_STATUS_HTTP, &argument, 1u,
                                  app.status, sizeof(app.status));
            return app.status;
        default: return message(PXA_MSG_STATUS_FAILED);
    }
}

/* Store rows show a generated icon placeholder: the first character of the
 * application name on the accent color. Guest Apps cannot decode a remote
 * image, so this keeps the list readable without shipping icons. */
static void format_icon_letter(char *output, size_t capacity,
                               const char *name) {
    size_t length = 0;
    size_t size;
    if (capacity == 0) return;
    output[0] = '\0';
    if (name == NULL || name[0] == '\0' || capacity < 2u) return;
    if ((uint8_t)name[0] < 0x80u) {
        char value = name[0];
        if (value >= 'a' && value <= 'z') value = (char)(value - 'a' + 'A');
        output[0] = value;
        length = 1;
    } else {
        size = (uint8_t)name[0] >= 0xf0u ? 4u
               : (uint8_t)name[0] >= 0xe0u ? 3u
                                           : 2u;
        while (length < size && name[length] != '\0' &&
               length + 1u < capacity)
            output[length] = name[length], ++length;
    }
    output[length] = '\0';
}

static int create_app_icon(pxa_ui_transaction_t *transaction, uint32_t box,
                           uint32_t letter, uint32_t parent, int32_t size,
                           const char *name) {
    char initial[8];
    format_icon_letter(initial, sizeof(initial), name);
    return pxa_ui_create(transaction, box, parent, 0, PXA_UI_NODE_BOX) &&
           pxa_ui_set_length(transaction, box, PXA_UI_PROPERTY_WIDTH,
                             PXA_UI_LENGTH_PX, (uint16_t)size) &&
           pxa_ui_set_length(transaction, box, PXA_UI_PROPERTY_HEIGHT,
                             PXA_UI_LENGTH_PX, (uint16_t)size) &&
           pxa_ui_set_u8(transaction, box, PXA_UI_PROPERTY_LAYOUT,
                         PXA_UI_LAYOUT_ROW) &&
           pxa_ui_set_u8(transaction, box, PXA_UI_PROPERTY_JUSTIFY,
                         PXA_UI_ALIGN_CENTER) &&
           pxa_ui_set_u8(transaction, box, PXA_UI_PROPERTY_ALIGN,
                         PXA_UI_ALIGN_CENTER) &&
           pxa_ui_set_dp(transaction, box, PXA_UI_PROPERTY_RADIUS,
                         size / 4) &&
           pxa_ui_set_theme_color(transaction, box, PXA_UI_PROPERTY_BACKGROUND,
                                  PXA_UI_THEME_PRIMARY) &&
           pxa_ui_create(transaction, letter, box, 0, PXA_UI_NODE_TEXT) &&
           pxa_ui_set_text(transaction, letter, initial,
                           string_length(initial)) &&
           pxa_ui_set_font_role(transaction, letter,
                                size >= 48 ? PXA_UI_FONT_ROLE_TITLE
                                           : PXA_UI_FONT_ROLE_BODY) &&
           pxa_ui_set_theme_color(transaction, letter,
                                  PXA_UI_PROPERTY_FOREGROUND,
                                  PXA_UI_THEME_ON_PRIMARY);
}

static int locale_is_chinese(void) {
    return app.i18n.locale_size >= 2u && app.i18n.locale[0] == 'z' &&
           app.i18n.locale[1] == 'h';
}

/* The device API ships both store locales; pick the one the Host reported. */
static const char *taxonomy_label(const char *label, const char *label_en) {
    if (locale_is_chinese())
        return label[0] != '\0' ? label : label_en;
    return label_en[0] != '\0' ? label_en : label;
}

/* Entry index + 1 for a canonical kind value, or 0 when the taxonomy has not
 * arrived yet. */
static uint8_t kind_entry_for(const char *value) {
    uint8_t index;
    for (index = 0; index < app.taxonomy.kind_count; ++index) {
        if (text_equal(app.taxonomy.kinds[index].value, value))
            return (uint8_t)(index + 1u);
    }
    return 0;
}

static const char *selected_kind_value(void) {
    if (app.kind_index == 0 || app.kind_index > app.taxonomy.kind_count)
        return PXA_STORE_DEFAULT_KIND;
    return app.taxonomy.kinds[app.kind_index - 1u].value;
}

static const char *selected_category_value(void) {
    if (app.category_entry < 0 ||
        app.category_entry >= (int16_t)app.taxonomy.category_count)
        return "";
    if (!text_equal(app.taxonomy.categories[app.category_entry].kind,
                    selected_kind_value()))
        return "";
    return app.taxonomy.categories[app.category_entry].value;
}

static uint8_t visible_category_count(void) {
    const char *kind = selected_kind_value();
    uint8_t index;
    uint8_t count = 0;
    if (kind[0] == '\0') return 0;
    for (index = 0; index < app.taxonomy.category_count; ++index) {
        if (text_equal(app.taxonomy.categories[index].kind, kind)) ++count;
    }
    return count;
}

static const char *status_text(void) {
    if (app.state == STORE_LOADING) return message(PXA_MSG_STATUS_LOADING);
    if (app.state == STORE_DENIED) return message(PXA_MSG_STATUS_DENIED);
    if (app.state == STORE_FAILED) return error_text();
    return message(PXA_MSG_STATUS_READY);
}

static const char *detail_status_text(void) {
    if (app.detail_state == STORE_LOADING)
        return message(PXA_MSG_STATUS_LOADING);
    if (app.detail_state == STORE_FAILED) return error_text();
    if (app.detail_state == STORE_READY) return message(PXA_MSG_STATUS_READY);
    return message(PXA_MSG_STATUS_WAITING);
}

static uint8_t status_color(void) {
    if (app.state == STORE_FAILED || app.state == STORE_DENIED)
        return PXA_UI_THEME_DANGER;
    if (app.state == STORE_READY && app.catalog.count != 0)
        return PXA_UI_THEME_SUCCESS;
    return PXA_UI_THEME_MUTED;
}

static uint8_t detail_status_color(void) {
    if (app.detail_state == STORE_FAILED) return PXA_UI_THEME_DANGER;
    if (app.detail_state == STORE_READY) return PXA_UI_THEME_SUCCESS;
    return PXA_UI_THEME_MUTED;
}


static uint8_t error_from_status(int32_t status) {
    switch (status) {
        case PXA_STATUS_UNSUPPORTED: return STORE_ERROR_UNSUPPORTED;
        case PXA_STATUS_DENIED: return STORE_ERROR_PERMISSION;
        case PXA_STATUS_TIMED_OUT: return STORE_ERROR_TIMEOUT;
        case PXA_STATUS_LIMIT_EXCEEDED: return STORE_ERROR_LIMIT;
        case PXA_STATUS_PROTOCOL_ERROR: return STORE_ERROR_PROTOCOL;
        case PXA_STATUS_UNAVAILABLE: return STORE_ERROR_UNAVAILABLE;
        case PXA_STATUS_NOT_FOUND: return STORE_ERROR_NOT_FOUND;
        default: return STORE_ERROR_FAILED;
    }
}

/* ------------------------------------------------------------------ */
/* Rendering                                                          */
/* ------------------------------------------------------------------ */

static int footer_status_text(char *output, size_t capacity);
static uint8_t catalog_columns(void);
static int create_chip(pxa_ui_transaction_t *transaction, uint32_t chip,
                       const char *text, uint8_t active);
static uint16_t effective_inset(uint8_t edge);
static uint16_t inset_padding(uint8_t edge, uint16_t margin);
static void ensure_window_snapshot(void);

static int create_action_button(pxa_ui_transaction_t *transaction, uint32_t node,
                                uint32_t label_node, const char *text,
                                uint8_t with_icon, uint8_t enabled) {
    const store_metrics_t *m = metrics();
    int ok = pxa_ui_create_typed(transaction, node, NODE_HEADER_ROW, 0,
                                 PXA_UI_NODE_CONTROL, PXA_UI_CONTROL_BUTTON) &&
             pxa_ui_set_length(transaction, node, PXA_UI_PROPERTY_HEIGHT,
                               PXA_UI_LENGTH_PX, m->action_height) &&
             pxa_ui_set_u8(transaction, node, PXA_UI_PROPERTY_LAYOUT,
                           PXA_UI_LAYOUT_ROW) &&
             pxa_ui_set_u8(transaction, node, PXA_UI_PROPERTY_JUSTIFY,
                           PXA_UI_ALIGN_CENTER) &&
             pxa_ui_set_u8(transaction, node, PXA_UI_PROPERTY_ALIGN,
                           PXA_UI_ALIGN_CENTER) &&
             pxa_ui_set_dp(transaction, node, PXA_UI_PROPERTY_GAP, 5) &&
             pxa_ui_set_padding(transaction, node, m->action_pad_h, 2,
                        m->action_pad_h, 2) &&
             pxa_ui_set_dp(transaction, node, PXA_UI_PROPERTY_RADIUS,
                          m->action_height / 2) &&
             pxa_ui_set_dp(transaction, node, PXA_UI_PROPERTY_BORDER_WIDTH, 1) &&
             pxa_ui_set_theme_color(transaction, node,
                                    PXA_UI_PROPERTY_BORDER_COLOR,
                                    PXA_UI_THEME_BORDER) &&
             pxa_ui_set_theme_color(transaction, node,
                                    PXA_UI_PROPERTY_BACKGROUND,
                                    PXA_UI_THEME_SURFACE) &&
             pxa_ui_set_u8(transaction, node, PXA_UI_PROPERTY_ENABLED, enabled) &&
             pxa_ui_set_event_mask(transaction, node,
                                   PXA_UI_EVENT_MASK_CLICK) &&
             pxa_ui_create(transaction, label_node, node, 0, PXA_UI_NODE_TEXT) &&
             pxa_ui_set_text(transaction, label_node, text,
                             string_length(text)) &&
             pxa_ui_set_font_role(transaction, label_node,
                                  PXA_UI_FONT_ROLE_CAPTION) &&
             pxa_ui_set_theme_color(transaction, label_node,
                                    PXA_UI_PROPERTY_FOREGROUND,
                                    PXA_UI_THEME_PRIMARY);
    if (ok && with_icon) {
        ok = pxa_ui_create(transaction, node + 1u, node, 0, PXA_UI_NODE_TEXT) &&
             pxa_ui_set_text(transaction, node + 1u, ICON_REFRESH,
                             sizeof(ICON_REFRESH) - 1u) &&
             pxa_ui_set_font_role(transaction, node + 1u,
                                  PXA_UI_FONT_ROLE_ICON) &&
             pxa_ui_set_theme_color(transaction, node + 1u,
                                    PXA_UI_PROPERTY_FOREGROUND,
                                    PXA_UI_THEME_PRIMARY);
    }
    return ok;
}

/* Flat title row on the page background, like the system application pages.
 * The catalog adds the request state as a second line. */
/* A floating header/chip row/tab bar keeps the catalog in place when the
 * chrome hides: the content keeps its position and stops jumping, which is
 * what makes the hide feel like a phone instead of a relayout. */
static int create_header(pxa_ui_transaction_t *transaction, uint8_t mode,
                         const char *title, size_t title_size,
                         uint8_t floating) {
    const store_metrics_t *m = metrics();
    uint8_t back = (uint8_t)(mode == STORE_SCREEN_DETAIL);
    int ok = pxa_ui_create(transaction, NODE_ROOT, 0, 0, PXA_UI_NODE_ROOT) &&
             pxa_ui_set_u8(transaction, NODE_ROOT, PXA_UI_PROPERTY_LAYOUT,
                           PXA_UI_LAYOUT_COLUMN) &&
             pxa_ui_set_theme_color(transaction, NODE_ROOT,
                                    PXA_UI_PROPERTY_BACKGROUND,
                                    PXA_UI_THEME_BACKGROUND) &&
             pxa_ui_set_padding(transaction, NODE_ROOT,
                                inset_padding(3, m->list_pad_h),
                                inset_padding(0, m->header_pad_v),
                                inset_padding(1, m->list_pad_h),
                                inset_padding(2, 6)) &&
             pxa_ui_create(transaction, NODE_HEADER, NODE_ROOT, 0,
                           PXA_UI_NODE_BOX) &&
             pxa_ui_set_u8(transaction, NODE_HEADER, PXA_UI_PROPERTY_POSITION,
                           floating) &&
             pxa_ui_set_length(transaction, NODE_HEADER, PXA_UI_PROPERTY_X,
                               PXA_UI_LENGTH_PX, 0) &&
             pxa_ui_set_length(transaction, NODE_HEADER, PXA_UI_PROPERTY_Y,
                               PXA_UI_LENGTH_PX, 0) &&
             pxa_ui_set_length(transaction, NODE_HEADER, PXA_UI_PROPERTY_WIDTH,
                               PXA_UI_LENGTH_FILL, 0) &&
             pxa_ui_set_length(transaction, NODE_HEADER, PXA_UI_PROPERTY_HEIGHT,
                               PXA_UI_LENGTH_PX, m->header_height) &&
             pxa_ui_set_u8(transaction, NODE_HEADER, PXA_UI_PROPERTY_LAYOUT,
                           PXA_UI_LAYOUT_COLUMN) &&
             pxa_ui_set_u8(transaction, NODE_HEADER, PXA_UI_PROPERTY_JUSTIFY,
                           PXA_UI_ALIGN_CENTER) &&
             pxa_ui_set_padding(transaction, NODE_HEADER, m->header_pad_h,
                               m->header_pad_v, m->header_pad_h, 4) &&
             pxa_ui_set_dp(transaction, NODE_HEADER, PXA_UI_PROPERTY_GAP, 3) &&
             pxa_ui_set_theme_color(transaction, NODE_HEADER,
                                    PXA_UI_PROPERTY_BACKGROUND,
                                    PXA_UI_THEME_BACKGROUND) &&
             pxa_ui_create(transaction, NODE_HEADER_ROW, NODE_HEADER, 0,
                           PXA_UI_NODE_BOX) &&
             pxa_ui_set_length(transaction, NODE_HEADER_ROW,
                               PXA_UI_PROPERTY_WIDTH, PXA_UI_LENGTH_FILL, 0) &&
             pxa_ui_set_length(transaction, NODE_HEADER_ROW,
                               PXA_UI_PROPERTY_HEIGHT, PXA_UI_LENGTH_PX,
                               (uint16_t)(m->header_height -
                                          m->status_height -
                                          m->header_pad_v - 6)) &&
             pxa_ui_set_u8(transaction, NODE_HEADER_ROW, PXA_UI_PROPERTY_LAYOUT,
                           PXA_UI_LAYOUT_ROW) &&
             pxa_ui_set_u8(transaction, NODE_HEADER_ROW, PXA_UI_PROPERTY_ALIGN,
                           PXA_UI_ALIGN_CENTER) &&
             pxa_ui_set_dp(transaction, NODE_HEADER_ROW, PXA_UI_PROPERTY_GAP,
                           6);
    if (!ok) return 0;
    if (back) {
        ok = create_action_button(transaction, NODE_BACK, NODE_BACK_LABEL,
                                  message(PXA_MSG_ACTION_BACK), 0, 1);
    } else {
        ok = pxa_ui_create(transaction, NODE_HEADER_TITLE, NODE_HEADER_ROW, 0,
                           PXA_UI_NODE_TEXT) &&
             pxa_ui_set_u16(transaction, NODE_HEADER_TITLE,
                            PXA_UI_PROPERTY_GROW, 1) &&
             pxa_ui_set_text(transaction, NODE_HEADER_TITLE, title,
                             title_size) &&
             pxa_ui_set_font_role(transaction, NODE_HEADER_TITLE, m->title_role) &&
             pxa_ui_set_theme_color(transaction, NODE_HEADER_TITLE,
                                    PXA_UI_PROPERTY_FOREGROUND,
                                    PXA_UI_THEME_TEXT) &&
             create_action_button(transaction, NODE_SEARCH_BUTTON,
                                  NODE_SEARCH_BUTTON_LABEL,
                                  message(PXA_MSG_ACTION_SEARCH), 0, 1) &&
             create_action_button(transaction, NODE_REFRESH,
                                  NODE_REFRESH_LABEL,
                                  message(PXA_MSG_ACTION_REFRESH), 1,
                                  (uint8_t)(!app.busy));
    }
    if (!ok) return 0;
    if (back) {
        ok = pxa_ui_create(transaction, NODE_HEADER_TITLE, NODE_HEADER_ROW, 0,
                           PXA_UI_NODE_TEXT) &&
             pxa_ui_set_u16(transaction, NODE_HEADER_TITLE,
                            PXA_UI_PROPERTY_GROW, 1) &&
             pxa_ui_set_text(transaction, NODE_HEADER_TITLE, title,
                             title_size) &&
             pxa_ui_set_font_role(transaction, NODE_HEADER_TITLE, m->title_role) &&
             pxa_ui_set_theme_color(transaction, NODE_HEADER_TITLE,
                                    PXA_UI_PROPERTY_FOREGROUND,
                                    PXA_UI_THEME_TEXT);
    }
    if (!ok) return 0;
    if (!back && m->status_height != 0) {
        char status[64];
        (void)footer_status_text(status, sizeof(status));
        ok = pxa_ui_create(transaction, NODE_HEADER_STATUS, NODE_HEADER, 0,
                           PXA_UI_NODE_TEXT) &&
             pxa_ui_set_length(transaction, NODE_HEADER_STATUS,
                               PXA_UI_PROPERTY_WIDTH, PXA_UI_LENGTH_FILL, 0) &&
             pxa_ui_set_length(transaction, NODE_HEADER_STATUS,
                               PXA_UI_PROPERTY_HEIGHT, PXA_UI_LENGTH_PX,
                               m->status_height) &&
             pxa_ui_set_text(transaction, NODE_HEADER_STATUS, status,
                             string_length(status)) &&
             pxa_ui_set_font_role(transaction, NODE_HEADER_STATUS,
                                  PXA_UI_FONT_ROLE_CAPTION) &&
             pxa_ui_set_theme_color(transaction, NODE_HEADER_STATUS,
                                    PXA_UI_PROPERTY_FOREGROUND,
                                    status_color());
    }
    return ok;
}

/* One horizontally scrollable row: kind filters first, then the categories of
 * the selected kind. */
static int create_chip(pxa_ui_transaction_t *transaction, uint32_t chip,
                       const char *text, uint8_t active) {
    const store_metrics_t *m = metrics();
    return pxa_ui_create_typed(transaction, chip, NODE_FILTERS, 0,
                               PXA_UI_NODE_CONTROL,
                               PXA_UI_CONTROL_BUTTON) &&
           pxa_ui_set_length(transaction, chip, PXA_UI_PROPERTY_HEIGHT,
                             PXA_UI_LENGTH_PX, m->chip_height) &&
           pxa_ui_set_u8(transaction, chip, PXA_UI_PROPERTY_LAYOUT,
                         PXA_UI_LAYOUT_ROW) &&
           pxa_ui_set_u8(transaction, chip, PXA_UI_PROPERTY_JUSTIFY,
                         PXA_UI_ALIGN_CENTER) &&
           pxa_ui_set_u8(transaction, chip, PXA_UI_PROPERTY_ALIGN,
                         PXA_UI_ALIGN_CENTER) &&
           pxa_ui_set_padding(transaction, chip, m->chip_pad_h, 2, m->chip_pad_h, 2) &&
           pxa_ui_set_dp(transaction, chip, PXA_UI_PROPERTY_RADIUS,
                          m->chip_height / 2) &&
           pxa_ui_set_dp(transaction, chip, PXA_UI_PROPERTY_BORDER_WIDTH, 1) &&
           pxa_ui_set_theme_color(transaction, chip,
                                  PXA_UI_PROPERTY_BORDER_COLOR,
                                  active ? PXA_UI_THEME_PRIMARY
                                         : PXA_UI_THEME_BORDER) &&
           pxa_ui_set_theme_color(transaction, chip,
                                  PXA_UI_PROPERTY_BACKGROUND,
                                  active ? PXA_UI_THEME_PRIMARY
                                         : PXA_UI_THEME_SURFACE) &&
           pxa_ui_set_event_mask(transaction, chip, PXA_UI_EVENT_MASK_CLICK) &&
           pxa_ui_create(transaction, chip + 1u, chip, 0, PXA_UI_NODE_TEXT) &&
           pxa_ui_set_text(transaction, chip + 1u, text,
                           string_length(text)) &&
           pxa_ui_set_font_role(transaction, chip + 1u,
                                PXA_UI_FONT_ROLE_CAPTION) &&
           pxa_ui_set_theme_color(transaction, chip + 1u,
                                  PXA_UI_PROPERTY_FOREGROUND,
                                  active ? PXA_UI_THEME_ON_PRIMARY
                                         : PXA_UI_THEME_TEXT);
}

static int create_filters(pxa_ui_transaction_t *transaction,
                          uint8_t floating) {
    const store_metrics_t *m = metrics();
    uint8_t index;
    if (!pxa_ui_create(transaction, NODE_FILTERS, NODE_ROOT, 0,
                       PXA_UI_NODE_SCROLL) ||
        !pxa_ui_set_u8(transaction, NODE_FILTERS, PXA_UI_PROPERTY_POSITION,
                       floating) ||
        !pxa_ui_set_length(transaction, NODE_FILTERS, PXA_UI_PROPERTY_X,
                           PXA_UI_LENGTH_PX, 0) ||
        !pxa_ui_set_length(transaction, NODE_FILTERS, PXA_UI_PROPERTY_Y,
                           PXA_UI_LENGTH_PX, m->header_height) ||
        !pxa_ui_set_length(transaction, NODE_FILTERS, PXA_UI_PROPERTY_WIDTH,
                           PXA_UI_LENGTH_FILL, 0) ||
        !pxa_ui_set_length(transaction, NODE_FILTERS, PXA_UI_PROPERTY_HEIGHT,
                           PXA_UI_LENGTH_PX, m->chips_row_height) ||
        !pxa_ui_set_u8(transaction, NODE_FILTERS, PXA_UI_PROPERTY_LAYOUT,
                       PXA_UI_LAYOUT_ROW) ||
        !pxa_ui_set_u8(transaction, NODE_FILTERS, PXA_UI_PROPERTY_ALIGN,
                       PXA_UI_ALIGN_CENTER) ||
        !pxa_ui_set_u8(transaction, NODE_FILTERS, PXA_UI_PROPERTY_SCROLL_AXIS,
                       1) ||
        !pxa_ui_set_u8(transaction, NODE_FILTERS, PXA_UI_PROPERTY_SCROLLBAR,
                       0) ||
        !pxa_ui_set_padding(transaction, NODE_FILTERS, m->list_pad_h,
                            (uint16_t)((m->chips_row_height -
                                        m->chip_height) / 2),
                            m->list_pad_h,
                            (uint16_t)((m->chips_row_height -
                                        m->chip_height) / 2)) ||
        !pxa_ui_set_dp(transaction, NODE_FILTERS, PXA_UI_PROPERTY_GAP, m->chip_gap))
        return 0;
    if (!create_chip(transaction, NODE_CATEGORY_ALL,
                     message(PXA_MSG_FILTER_ALL),
                     (uint8_t)(app.category_entry < 0)))
        return 0;
    for (index = 0; index < app.taxonomy.category_count; ++index) {
        const store_taxonomy_entry_t *entry = &app.taxonomy.categories[index];
        if (!text_equal(entry->kind, selected_kind_value())) continue;
        if (!create_chip(transaction,
                         NODE_CATEGORY_BASE + (uint32_t)index * NODE_CHIP_STRIDE,
                         taxonomy_label(entry->label, entry->label_en),
                         (uint8_t)(app.category_entry == (int16_t)index)))
            return 0;
    }
    return 1;
}

static int footer_status_text(char *output, size_t capacity) {
    pxa_i18n_argument_t argument;
    char count[12];
    output[0] = '\0';
    if (app.state == STORE_LOADING) {
        copy_text(output, capacity, message(PXA_MSG_STATUS_LOADING));
        return 1;
    }
    if (app.state == STORE_FAILED || app.state == STORE_DENIED) {
        copy_text(output, capacity, status_text());
        return 1;
    }
    if (app.catalog.count == 0) {
        copy_text(output, capacity, message(PXA_MSG_STATUS_READY));
        return 1;
    }
    count[0] = '\0';
    (void)append_u64(count, sizeof(count), 0, app.catalog.count);
    argument.name = "count";
    argument.name_size = 5u;
    argument.type = PXA_I18N_ARGUMENT_STRING;
    argument.value.string.data = count;
    argument.value.string.size = string_length(count);
    (void)pxa_i18n_format(&app.i18n, PXA_MSG_LIST_LOADED, &argument, 1u, output,
                          capacity);
    return 1;
}

static int create_card(pxa_ui_transaction_t *transaction, uint8_t index) {
    const store_metrics_t *m = metrics();
    const store_app_t *item = store_catalog_at_const(&app.catalog, index);
    uint32_t card = NODE_CARD_BASE + (uint32_t)index * NODE_CARD_STRIDE;
    uint32_t icon = card + 1u;
    uint32_t letter = card + 2u;
    uint32_t info = card + 3u;
    uint32_t head = card + 4u;
    uint32_t name = card + 5u;
    uint32_t version = card + 6u;
    uint32_t summary = card + 7u;
    uint32_t meta = card + 8u;
    size_t offset;
    if (!pxa_ui_create_typed(transaction, card, NODE_LIST, 0,
                             PXA_UI_NODE_CONTROL, PXA_UI_CONTROL_BUTTON) ||
        !pxa_ui_set_length(transaction, card, PXA_UI_PROPERTY_WIDTH,
                           PXA_UI_LENGTH_FILL, 0) ||
        !pxa_ui_set_length(transaction, card, PXA_UI_PROPERTY_HEIGHT,
                           PXA_UI_LENGTH_PX, m->card_height) ||
        !pxa_ui_set_u8(transaction, card, PXA_UI_PROPERTY_LAYOUT,
                       PXA_UI_LAYOUT_ROW) ||
        !pxa_ui_set_u8(transaction, card, PXA_UI_PROPERTY_ALIGN,
                       PXA_UI_ALIGN_CENTER) ||
        !pxa_ui_set_padding(transaction, card, m->card_pad, m->card_pad,
                           m->card_pad, m->card_pad) ||
        !pxa_ui_set_dp(transaction, card, PXA_UI_PROPERTY_GAP, m->card_gap) ||
        !pxa_ui_set_dp(transaction, card, PXA_UI_PROPERTY_RADIUS, m->card_radius) ||
        !pxa_ui_set_dp(transaction, card, PXA_UI_PROPERTY_BORDER_WIDTH, 1) ||
        !pxa_ui_set_theme_color(transaction, card, PXA_UI_PROPERTY_BACKGROUND,
                                PXA_UI_THEME_SURFACE) ||
        !pxa_ui_set_theme_color(transaction, card, PXA_UI_PROPERTY_BORDER_COLOR,
                                PXA_UI_THEME_BORDER) ||
        !pxa_ui_set_event_mask(transaction, card,
                               app.busy ? 0 : PXA_UI_EVENT_MASK_CLICK) ||
        !create_app_icon(transaction, icon, letter, card, m->card_icon,
                        item->name) ||
        !pxa_ui_create(transaction, info, card, 0, PXA_UI_NODE_BOX) ||
        !pxa_ui_set_u16(transaction, info, PXA_UI_PROPERTY_GROW, 1) ||
        !pxa_ui_set_length(transaction, info, PXA_UI_PROPERTY_HEIGHT,
                           PXA_UI_LENGTH_PX,
                           (uint16_t)(m->card_height - 2u * m->card_pad)) ||
        !pxa_ui_set_u8(transaction, info, PXA_UI_PROPERTY_LAYOUT,
                       PXA_UI_LAYOUT_COLUMN) ||
        !pxa_ui_set_dp(transaction, info, PXA_UI_PROPERTY_GAP, 2) ||
        !pxa_ui_create(transaction, head, info, 0, PXA_UI_NODE_BOX) ||
        !pxa_ui_set_length(transaction, head, PXA_UI_PROPERTY_WIDTH,
                           PXA_UI_LENGTH_FILL, 0) ||
        !pxa_ui_set_u8(transaction, head, PXA_UI_PROPERTY_LAYOUT,
                       PXA_UI_LAYOUT_ROW) ||
        !pxa_ui_set_u8(transaction, head, PXA_UI_PROPERTY_ALIGN,
                       PXA_UI_ALIGN_CENTER) ||
        !pxa_ui_set_dp(transaction, head, PXA_UI_PROPERTY_GAP, 6) ||
        !pxa_ui_create(transaction, name, head, 0, PXA_UI_NODE_TEXT) ||
        !pxa_ui_set_u16(transaction, name, PXA_UI_PROPERTY_GROW, 1) ||
        !pxa_ui_set_text(transaction, name, item->name,
                         string_length(item->name)) ||
        !pxa_ui_set_font_role(transaction, name, PXA_UI_FONT_ROLE_BODY) ||
        !pxa_ui_set_theme_color(transaction, name, PXA_UI_PROPERTY_FOREGROUND,
                                PXA_UI_THEME_TEXT))
        return 0;
    {
        uint8_t columns = catalog_columns();
        if (columns > 1u) {
            if (!pxa_ui_set_u8(transaction, card, PXA_UI_PROPERTY_ALIGN,
                               PXA_UI_ALIGN_STRETCH) ||
                !pxa_ui_set_grid_cell(transaction, card,
                                      (uint16_t)(index % columns),
                                      (uint16_t)(index / columns), 1u, 1u))
                return 0;
        }
    }
    app.scratch[0] = '\0';
    offset = append_text(app.scratch, sizeof(app.scratch), 0, "v");
    (void)append_text(app.scratch, sizeof(app.scratch), offset, item->version);
    if (!pxa_ui_create(transaction, version, head, 0, PXA_UI_NODE_TEXT) ||
        !pxa_ui_set_text(transaction, version, app.scratch,
                         string_length(app.scratch)) ||
        !pxa_ui_set_font_role(transaction, version, PXA_UI_FONT_ROLE_CAPTION) ||
        !pxa_ui_set_theme_color(transaction, version,
                                PXA_UI_PROPERTY_FOREGROUND,
                                PXA_UI_THEME_PRIMARY))
        return 0;
    copy_text(app.scratch, sizeof(app.scratch), item->summary);
    if (!pxa_ui_create(transaction, summary, info, 0, PXA_UI_NODE_TEXT) ||
        !pxa_ui_set_length(transaction, summary, PXA_UI_PROPERTY_WIDTH,
                           PXA_UI_LENGTH_FILL, 0) ||
        !pxa_ui_set_text(transaction, summary, app.scratch,
                         string_length(app.scratch)) ||
        !pxa_ui_set_font_role(transaction, summary, PXA_UI_FONT_ROLE_CAPTION) ||
        !pxa_ui_set_theme_color(transaction, summary,
                                PXA_UI_PROPERTY_FOREGROUND,
                                PXA_UI_THEME_MUTED))
        return 0;
    app.scratch[0] = '\0';
    offset = 0;
    if (item->platforms[0] != '\0')
        offset = append_text(app.scratch, sizeof(app.scratch), offset,
                             item->platforms);
    if (item->size != 0) {
        char size[24];
        format_size(size, sizeof(size), item->size);
        if (offset != 0)
            offset = append_text(app.scratch, sizeof(app.scratch), offset,
                                 " · ");
        (void)append_text(app.scratch, sizeof(app.scratch), offset, size);
    }
    return pxa_ui_create(transaction, meta, info, 0, PXA_UI_NODE_TEXT) &&
           pxa_ui_set_length(transaction, meta, PXA_UI_PROPERTY_WIDTH,
                             PXA_UI_LENGTH_FILL, 0) &&
           pxa_ui_set_text(transaction, meta, app.scratch,
                           string_length(app.scratch)) &&
           pxa_ui_set_font_role(transaction, meta, PXA_UI_FONT_ROLE_CAPTION) &&
           pxa_ui_set_theme_color(transaction, meta, PXA_UI_PROPERTY_FOREGROUND,
                                  PXA_UI_THEME_MUTED);
}

/* Wide displays get a multi-column card grid instead of a single list. */
static uint8_t catalog_columns(void) {
    if (app.width >= 720u) return 3u;
    if (app.width >= 480u) return 2u;
    return 1u;
}

static int create_catalog_list(pxa_ui_transaction_t *transaction) {
    const store_metrics_t *m = metrics();
    uint8_t index;
    int ok = pxa_ui_create(transaction, NODE_LIST, NODE_ROOT, 0,
                           PXA_UI_NODE_SCROLL) &&
             pxa_ui_set_length(transaction, NODE_LIST, PXA_UI_PROPERTY_WIDTH,
                               PXA_UI_LENGTH_FILL, 0) &&
             pxa_ui_set_length(transaction, NODE_LIST, PXA_UI_PROPERTY_HEIGHT,
                               PXA_UI_LENGTH_PX, 0) &&
             pxa_ui_set_u16(transaction, NODE_LIST, PXA_UI_PROPERTY_GROW, 1) &&
             pxa_ui_set_u8(transaction, NODE_LIST, PXA_UI_PROPERTY_LAYOUT,
                           PXA_UI_LAYOUT_COLUMN) &&
             pxa_ui_set_u8(transaction, NODE_LIST, PXA_UI_PROPERTY_SCROLL_AXIS,
                           2) &&
             pxa_ui_set_u8(transaction, NODE_LIST, PXA_UI_PROPERTY_SCROLLBAR,
                           1) &&
             pxa_ui_set_event_mask(transaction, NODE_LIST,
                                   PXA_UI_EVENT_MASK_SCROLL |
                                       PXA_UI_EVENT_MASK_POINTER) &&
             pxa_ui_set_padding(transaction, NODE_LIST, m->list_pad_h,
                                (uint16_t)(m->header_height +
                                           m->chips_row_height),
                                m->list_pad_h,
                                (uint16_t)(m->tab_height + m->card_gap)) &&
             pxa_ui_set_dp(transaction, NODE_LIST, PXA_UI_PROPERTY_GAP, m->list_gap);
    if (!ok) return 0;
    {
        uint8_t columns = catalog_columns();
        uint8_t rows = (uint8_t)((app.catalog.count + columns - 1u) / columns);
        if (columns > 1u && rows != 0 && rows <= PXA_UI_GRID_MAX_TRACKS) {
            pxa_ui_grid_track_t column_tracks[3];
            pxa_ui_grid_track_t row_tracks[PXA_UI_GRID_MAX_TRACKS];
            uint8_t index;
            for (index = 0; index < columns; ++index) {
                column_tracks[index].kind = PXA_UI_GRID_FRACTION;
                column_tracks[index].value = 1u;
            }
            for (index = 0; index < rows; ++index) {
                row_tracks[index].kind = PXA_UI_GRID_CONTENT;
                row_tracks[index].value = 0;
            }
            ok = pxa_ui_set_u8(transaction, NODE_LIST, PXA_UI_PROPERTY_LAYOUT,
                               PXA_UI_LAYOUT_GRID) &&
                 pxa_ui_set_grid_columns(transaction, NODE_LIST, column_tracks,
                                         columns) &&
                 pxa_ui_set_grid_rows(transaction, NODE_LIST, row_tracks, rows);
            if (!ok) return 0;
        }
    }
    /* LVGL scrolls a focusable child into view while the surface is laid out,
     * which clips the first card. Restore the tracked offset last so the
     * transaction commit overrides it. */
    ok = pxa_ui_set_dp(transaction, NODE_LIST, PXA_UI_PROPERTY_SCROLL_POSITION,
                       app.list_scroll);
    if (!ok) return 0;
    if (app.catalog.count == 0) {
        const char *text = app.state == STORE_LOADING
                               ? message(PXA_MSG_LIST_LOADING)
                               : message(PXA_MSG_LIST_EMPTY);
        return pxa_ui_create(transaction, NODE_EMPTY, NODE_LIST, 0,
                             PXA_UI_NODE_TEXT) &&
               pxa_ui_set_length(transaction, NODE_EMPTY, PXA_UI_PROPERTY_WIDTH,
                                 PXA_UI_LENGTH_FILL, 0) &&
               pxa_ui_set_length(transaction, NODE_EMPTY, PXA_UI_PROPERTY_HEIGHT,
                                 PXA_UI_LENGTH_PX, 60) &&
               pxa_ui_set_padding(transaction, NODE_EMPTY, 2, 8, 2, 8) &&
               pxa_ui_set_text(transaction, NODE_EMPTY, text,
                               string_length(text)) &&
               pxa_ui_set_font_role(transaction, NODE_EMPTY,
                                    PXA_UI_FONT_ROLE_BODY) &&
               pxa_ui_set_theme_color(transaction, NODE_EMPTY,
                                      PXA_UI_PROPERTY_FOREGROUND,
                                      PXA_UI_THEME_MUTED);
    }
    for (index = 0; ok && index < app.catalog.count; ++index)
        ok = create_card(transaction, index);
    return ok;
}

#define STORE_AUTO_LOAD_STEP 70

static int create_footer(pxa_ui_transaction_t *transaction) {
    const store_metrics_t *m = metrics();
    const char *label;
    if (!app.busy && !app.catalog.has_more) return 1;
    label = app.busy ? message(PXA_MSG_STATUS_LOADING)
                     : message(PXA_MSG_STATUS_WAITING);
    /* Incremental loading is automatic; this row only reports it. */
    return pxa_ui_create(transaction, NODE_FOOTER, NODE_LIST, 0,
                         PXA_UI_NODE_BOX) &&
           pxa_ui_set_length(transaction, NODE_FOOTER, PXA_UI_PROPERTY_WIDTH,
                             PXA_UI_LENGTH_FILL, 0) &&
           pxa_ui_set_length(transaction, NODE_FOOTER, PXA_UI_PROPERTY_HEIGHT,
                             PXA_UI_LENGTH_PX, m->status_height) &&
           pxa_ui_set_u8(transaction, NODE_FOOTER, PXA_UI_PROPERTY_LAYOUT,
                         PXA_UI_LAYOUT_ROW) &&
           pxa_ui_set_u8(transaction, NODE_FOOTER, PXA_UI_PROPERTY_JUSTIFY,
                         PXA_UI_ALIGN_CENTER) &&
           pxa_ui_set_theme_color(transaction, NODE_FOOTER,
                                  PXA_UI_PROPERTY_BACKGROUND,
                                  PXA_UI_THEME_BACKGROUND) &&
           pxa_ui_create(transaction, NODE_LOAD_MORE, NODE_FOOTER, 0,
                         PXA_UI_NODE_TEXT) &&
           pxa_ui_set_text(transaction, NODE_LOAD_MORE, label,
                           string_length(label)) &&
           pxa_ui_set_font_role(transaction, NODE_LOAD_MORE,
                                PXA_UI_FONT_ROLE_CAPTION) &&
           pxa_ui_set_theme_color(transaction, NODE_LOAD_MORE,
                                  PXA_UI_PROPERTY_FOREGROUND,
                                  PXA_UI_THEME_MUTED);
}

/* Two text tabs pinned to the bottom, above the system navigation bar. */
static int create_tab(pxa_ui_transaction_t *transaction, uint32_t tab,
                      uint32_t label, uint32_t mark, const char *text,
                      uint8_t active) {
    return pxa_ui_create_typed(transaction, tab, NODE_TABBAR, 0,
                               PXA_UI_NODE_CONTROL, PXA_UI_CONTROL_BUTTON) &&
           pxa_ui_set_u16(transaction, tab, PXA_UI_PROPERTY_GROW, 1) &&
           pxa_ui_set_length(transaction, tab, PXA_UI_PROPERTY_HEIGHT,
                             PXA_UI_LENGTH_FILL, 0) &&
           pxa_ui_set_u8(transaction, tab, PXA_UI_PROPERTY_LAYOUT,
                         PXA_UI_LAYOUT_COLUMN) &&
           pxa_ui_set_u8(transaction, tab, PXA_UI_PROPERTY_JUSTIFY,
                         PXA_UI_ALIGN_CENTER) &&
           pxa_ui_set_u8(transaction, tab, PXA_UI_PROPERTY_ALIGN,
                         PXA_UI_ALIGN_CENTER) &&
           pxa_ui_set_dp(transaction, tab, PXA_UI_PROPERTY_GAP, 4) &&
           pxa_ui_set_theme_color(transaction, tab, PXA_UI_PROPERTY_BACKGROUND,
                                  PXA_UI_THEME_BACKGROUND) &&
           pxa_ui_set_event_mask(transaction, tab, PXA_UI_EVENT_MASK_CLICK) &&
           pxa_ui_create(transaction, label, tab, 0, PXA_UI_NODE_TEXT) &&
           pxa_ui_set_text(transaction, label, text, string_length(text)) &&
           pxa_ui_set_font_role(transaction, label, PXA_UI_FONT_ROLE_CAPTION) &&
           pxa_ui_set_theme_color(transaction, label,
                                  PXA_UI_PROPERTY_FOREGROUND,
                                  active ? PXA_UI_THEME_PRIMARY
                                         : PXA_UI_THEME_MUTED) &&
           pxa_ui_create(transaction, mark, tab, 0, PXA_UI_NODE_BOX) &&
           pxa_ui_set_length(transaction, mark, PXA_UI_PROPERTY_WIDTH,
                             PXA_UI_LENGTH_PX, 26) &&
           pxa_ui_set_length(transaction, mark, PXA_UI_PROPERTY_HEIGHT,
                             PXA_UI_LENGTH_PX, 3) &&
           pxa_ui_set_dp(transaction, mark, PXA_UI_PROPERTY_RADIUS, 2) &&
           pxa_ui_set_theme_color(transaction, mark, PXA_UI_PROPERTY_BACKGROUND,
                                  active ? PXA_UI_THEME_PRIMARY
                                         : PXA_UI_THEME_BACKGROUND);
}

static int create_tabbar(pxa_ui_transaction_t *transaction, uint8_t mode) {
    const store_metrics_t *m = metrics();
    const char *apps = message(PXA_MSG_KIND_APPS);
    const char *games = message(PXA_MSG_KIND_GAMES);
    uint8_t apps_entry = kind_entry_for("app");
    uint8_t games_entry = kind_entry_for("game");
    uint8_t apps_active =
        (uint8_t)(app.kind_index == apps_entry ||
                  (app.kind_index == 0 && text_equal(PXA_STORE_DEFAULT_KIND, "app")));
    uint8_t games_active =
        (uint8_t)(app.kind_index == games_entry ||
                  (app.kind_index == 0 && text_equal(PXA_STORE_DEFAULT_KIND, "game")));
    uint16_t tab_y = 0;
    if (mode == STORE_SCREEN_DETAIL || app.screen != STORE_SCREEN_CATALOG)
        return 1;
    /* The bar sits on the bottom edge of the padded content area. */
    {
        int32_t content_height = (int32_t)app.height -
                                 (int32_t)inset_padding(0, m->header_pad_v) -
                                 (int32_t)inset_padding(2, 6);
        int32_t bar_y = content_height - (int32_t)m->tab_height;
        if (bar_y < 0) bar_y = 0;
        tab_y = (uint16_t)bar_y;
    }
    return pxa_ui_create(transaction, NODE_TABBAR, NODE_ROOT, 0,
                         PXA_UI_NODE_BOX) &&
           pxa_ui_set_u8(transaction, NODE_TABBAR, PXA_UI_PROPERTY_POSITION,
                         1) &&
           pxa_ui_set_length(transaction, NODE_TABBAR, PXA_UI_PROPERTY_X,
                             PXA_UI_LENGTH_PX, 0) &&
           pxa_ui_set_length(transaction, NODE_TABBAR, PXA_UI_PROPERTY_Y,
                             PXA_UI_LENGTH_PX, tab_y) &&
           pxa_ui_set_length(transaction, NODE_TABBAR, PXA_UI_PROPERTY_WIDTH,
                             PXA_UI_LENGTH_FILL, 0) &&
           pxa_ui_set_length(transaction, NODE_TABBAR, PXA_UI_PROPERTY_HEIGHT,
                             PXA_UI_LENGTH_PX, m->tab_height) &&
           pxa_ui_set_u8(transaction, NODE_TABBAR, PXA_UI_PROPERTY_LAYOUT,
                         PXA_UI_LAYOUT_ROW) &&
           pxa_ui_set_theme_color(transaction, NODE_TABBAR,
                                  PXA_UI_PROPERTY_BACKGROUND,
                                  PXA_UI_THEME_BACKGROUND) &&
           create_tab(transaction, NODE_TAB_APPS, NODE_TAB_APPS_LABEL,
                      NODE_TAB_APPS_MARK, apps, apps_active) &&
           create_tab(transaction, NODE_TAB_GAMES, NODE_TAB_GAMES_LABEL,
                      NODE_TAB_GAMES_MARK, games, games_active);
}

static int render_catalog(void) {
    pxa_ui_transaction_t transaction = {0};
    uint32_t next = app.generation + 1u;
    int ok;
    if (next == 0 ||
        !pxa_ui_transaction_begin(&transaction, next,
                                  PXA_UI_TRANSACTION_REPLACE_SURFACE,
                                  app.packet, sizeof(app.packet)))
        return 0;
    ok = create_header(&transaction, STORE_SCREEN_CATALOG,
                       message(PXA_MSG_SCREEN_TITLE),
                       pxa_i18n_size(&app.i18n, PXA_MSG_SCREEN_TITLE), 1) &&
         create_filters(&transaction, 1) && create_catalog_list(&transaction) &&
         create_footer(&transaction) &&
         create_tabbar(&transaction, STORE_SCREEN_CATALOG);
    if (!ok || !pxa_ui_transaction_commit(&transaction)) {
        if (transaction.active) (void)pxa_ui_transaction_cancel(&transaction);
        return 0;
    }
    app.generation = next;
    {
        /* A render recreates the chrome visible: keep the state the scroll
         * asked for so loading a page does not pop the bars back. */
        uint8_t chrome_was_hidden = app.chrome_hidden;
        app.chrome_hidden = 0;
        store_trace("render hidden was", chrome_was_hidden);
        if (chrome_was_hidden) (void)set_chrome_visible(0);
    }
    return 1;
}

static int create_section(pxa_ui_transaction_t *transaction, uint8_t index,
                          const char *title, const char *body,
                          uint8_t warning) {
    const store_metrics_t *m = metrics();
    uint32_t box = NODE_DETAIL_SECTION_BASE +
                   (uint32_t)index * NODE_DETAIL_SECTION_STRIDE;
    uint32_t title_node = box + 1u;
    uint32_t body_node = box + 2u;
    return pxa_ui_create(transaction, box, NODE_DETAIL_SCROLL, 0,
                         PXA_UI_NODE_BOX) &&
           pxa_ui_set_length(transaction, box, PXA_UI_PROPERTY_WIDTH,
                             PXA_UI_LENGTH_FILL, 0) &&
           pxa_ui_set_u8(transaction, box, PXA_UI_PROPERTY_LAYOUT,
                         PXA_UI_LAYOUT_COLUMN) &&
           pxa_ui_set_padding(transaction, box, m->card_pad + 2, m->card_pad,
                           m->card_pad + 2, m->card_pad) &&
           pxa_ui_set_dp(transaction, box, PXA_UI_PROPERTY_GAP, 4) &&
           pxa_ui_set_dp(transaction, box, PXA_UI_PROPERTY_RADIUS, m->card_radius) &&
           pxa_ui_set_dp(transaction, box, PXA_UI_PROPERTY_BORDER_WIDTH, 1) &&
           pxa_ui_set_theme_color(transaction, box, PXA_UI_PROPERTY_BACKGROUND,
                                  PXA_UI_THEME_SURFACE) &&
           pxa_ui_set_theme_color(transaction, box,
                                  PXA_UI_PROPERTY_BORDER_COLOR,
                                  warning ? PXA_UI_THEME_WARNING
                                          : PXA_UI_THEME_BORDER) &&
           pxa_ui_create(transaction, title_node, box, 0, PXA_UI_NODE_TEXT) &&
           pxa_ui_set_text(transaction, title_node, title,
                           string_length(title)) &&
           pxa_ui_set_font_role(transaction, title_node,
                                PXA_UI_FONT_ROLE_CAPTION) &&
           pxa_ui_set_theme_color(transaction, title_node,
                                  PXA_UI_PROPERTY_FOREGROUND,
                                  warning ? PXA_UI_THEME_WARNING
                                          : PXA_UI_THEME_MUTED) &&
           pxa_ui_create(transaction, body_node, box, 0, PXA_UI_NODE_TEXT) &&
           pxa_ui_set_length(transaction, body_node, PXA_UI_PROPERTY_WIDTH,
                             PXA_UI_LENGTH_FILL, 0) &&
           pxa_ui_set_text(transaction, body_node, body,
                           string_length(body)) &&
           pxa_ui_set_font_role(transaction, body_node,
                                PXA_UI_FONT_ROLE_CAPTION) &&
           pxa_ui_set_theme_color(transaction, body_node,
                                  PXA_UI_PROPERTY_FOREGROUND,
                                  PXA_UI_THEME_TEXT);
}

static int create_row(pxa_ui_transaction_t *transaction, uint8_t index,
                      const char *label, const char *value) {
    uint32_t box = NODE_DETAIL_ROW_BASE +
                   (uint32_t)index * NODE_DETAIL_ROW_STRIDE;
    uint32_t label_node = box + 1u;
    uint32_t value_node = box + 2u;
    return pxa_ui_create(transaction, box, NODE_DETAIL_SCROLL, 0,
                         PXA_UI_NODE_BOX) &&
           pxa_ui_set_length(transaction, box, PXA_UI_PROPERTY_WIDTH,
                             PXA_UI_LENGTH_FILL, 0) &&
           pxa_ui_set_u8(transaction, box, PXA_UI_PROPERTY_LAYOUT,
                         PXA_UI_LAYOUT_ROW) &&
           pxa_ui_set_u8(transaction, box, PXA_UI_PROPERTY_ALIGN,
                         PXA_UI_ALIGN_START) &&
           pxa_ui_set_padding(transaction, box, 2, 0, 2, 0) &&
           pxa_ui_set_dp(transaction, box, PXA_UI_PROPERTY_GAP, 8) &&
           pxa_ui_create(transaction, label_node, box, 0, PXA_UI_NODE_TEXT) &&
           pxa_ui_set_length(transaction, label_node, PXA_UI_PROPERTY_WIDTH,
                             PXA_UI_LENGTH_PX, 86) &&
           pxa_ui_set_text(transaction, label_node, label,
                           string_length(label)) &&
           pxa_ui_set_font_role(transaction, label_node,
                                PXA_UI_FONT_ROLE_CAPTION) &&
           pxa_ui_set_theme_color(transaction, label_node,
                                  PXA_UI_PROPERTY_FOREGROUND,
                                  PXA_UI_THEME_MUTED) &&
           pxa_ui_create(transaction, value_node, box, 0, PXA_UI_NODE_TEXT) &&
           pxa_ui_set_u16(transaction, value_node, PXA_UI_PROPERTY_GROW, 1) &&
           pxa_ui_set_text(transaction, value_node, value,
                           string_length(value)) &&
           pxa_ui_set_font_role(transaction, value_node,
                                PXA_UI_FONT_ROLE_CAPTION) &&
           pxa_ui_set_theme_color(transaction, value_node,
                                  PXA_UI_PROPERTY_FOREGROUND,
                                  PXA_UI_THEME_TEXT);
}

static void build_permissions_text(char *output, size_t capacity) {
    const store_app_t *item = detail_item();
    size_t offset = 0;
    uint8_t index;
    output[0] = '\0';
    if (item->permission_count == 0) {
        copy_text(output, capacity, message(PXA_MSG_DETAIL_NONE));
        return;
    }
    for (index = 0; index < item->permission_count; ++index) {
        const store_permission_t *permission = &item->permissions[index];
        if (index != 0) offset = append_text(output, capacity, offset, "\n");
        offset = append_text(output, capacity, offset, permission->name);
        if (permission->scope[0] != '\0') {
            offset = append_text(output, capacity, offset, " (");
            offset = append_text(output, capacity, offset, permission->scope);
            offset = append_text(output, capacity, offset, ")");
        }
        offset = append_text(output, capacity, offset, " · ");
        (void)append_text(output, capacity, offset,
                          permission->required
                              ? message(PXA_MSG_DETAIL_REQUIRED)
                              : message(PXA_MSG_DETAIL_OPTIONAL));
    }
}

static void build_services_text(char *output, size_t capacity) {
    const store_app_t *item = detail_item();
    size_t offset = 0;
    uint8_t index;
    output[0] = '\0';
    if (item->service_count == 0) {
        copy_text(output, capacity, message(PXA_MSG_DETAIL_NONE));
        return;
    }
    for (index = 0; index < item->service_count; ++index) {
        if (index != 0) offset = append_text(output, capacity, offset, "\n");
        format_service_requirement(output + offset, capacity - offset,
                                   &item->services[index]);
        offset = string_length(output);
    }
}

static void build_install_text(char *output, size_t capacity) {
    const store_app_t *item = detail_item();
    pxa_i18n_argument_t arguments[2];
    char size[24];
    char limit[24];
    size_t offset = 0;
    output[0] = '\0';
    format_size(size, sizeof(size), item->size);
    format_size(limit, sizeof(limit), PXA_NET_MAX_RESPONSE_BODY_BYTES);
    if (item->size > PXA_NET_MAX_RESPONSE_BODY_BYTES) {
        arguments[0].name = "size";
        arguments[0].name_size = 4u;
        arguments[0].type = PXA_I18N_ARGUMENT_STRING;
        arguments[0].value.string.data = size;
        arguments[0].value.string.size = string_length(size);
        arguments[1].name = "limit";
        arguments[1].name_size = 5u;
        arguments[1].type = PXA_I18N_ARGUMENT_STRING;
        arguments[1].value.string.data = limit;
        arguments[1].value.string.size = string_length(limit);
        offset = pxa_i18n_format(&app.i18n, PXA_MSG_INSTALL_BLOCKED_SIZE,
                                 arguments, 2u, output, capacity);
    } else {
        offset = pxa_i18n_format(&app.i18n, PXA_MSG_INSTALL_READY, NULL, 0u,
                                 output, capacity);
    }
    offset = append_text(output, capacity, offset, "\n");
    offset = append_text(output, capacity, offset,
                         message(PXA_MSG_INSTALL_BLOCKED_ABI));
    if (app.install_feedback) {
        offset = append_text(output, capacity, offset, "\n");
        (void)append_text(output, capacity, offset,
                          message(PXA_MSG_STATUS_INSTALL_BLOCKED));
    }
}

static int render_detail(void) {
    const store_metrics_t *m = metrics();
    pxa_ui_transaction_t transaction = {0};
    const store_app_t *item = detail_item();
    uint32_t next = app.generation + 1u;
    char id_text[STORE_MAX_KEY_ID + 8];
    char size_text[24];
    char sequence_text[24];
    char sdk_text[40];
    int ok;
    if (next == 0 ||
        !pxa_ui_transaction_begin(&transaction, next,
                                  PXA_UI_TRANSACTION_REPLACE_SURFACE,
                                  app.packet, sizeof(app.packet)))
        return 0;
    ok = create_header(&transaction, STORE_SCREEN_DETAIL, item->name,
                       string_length(item->name), 0) &&
         pxa_ui_create(&transaction, NODE_DETAIL_SCROLL, NODE_ROOT, 0,
                       PXA_UI_NODE_SCROLL) &&
         pxa_ui_set_length(&transaction, NODE_DETAIL_SCROLL,
                           PXA_UI_PROPERTY_WIDTH, PXA_UI_LENGTH_FILL, 0) &&
         pxa_ui_set_length(&transaction, NODE_DETAIL_SCROLL,
                           PXA_UI_PROPERTY_HEIGHT, PXA_UI_LENGTH_PX, 0) &&
         pxa_ui_set_u16(&transaction, NODE_DETAIL_SCROLL, PXA_UI_PROPERTY_GROW,
                        1) &&
         pxa_ui_set_u8(&transaction, NODE_DETAIL_SCROLL, PXA_UI_PROPERTY_LAYOUT,
                       PXA_UI_LAYOUT_COLUMN) &&
         pxa_ui_set_u8(&transaction, NODE_DETAIL_SCROLL,
                       PXA_UI_PROPERTY_SCROLL_AXIS, 2) &&
         pxa_ui_set_u8(&transaction, NODE_DETAIL_SCROLL,
                       PXA_UI_PROPERTY_SCROLLBAR, 1) &&
         pxa_ui_set_event_mask(&transaction, NODE_DETAIL_SCROLL,
                               PXA_UI_EVENT_MASK_SCROLL) &&
         pxa_ui_set_padding(&transaction, NODE_DETAIL_SCROLL, 12, 8, 12, 8) &&
         pxa_ui_set_dp(&transaction, NODE_DETAIL_SCROLL, PXA_UI_PROPERTY_GAP,
                       8) &&
         pxa_ui_set_dp(&transaction, NODE_DETAIL_SCROLL,
                       PXA_UI_PROPERTY_SCROLL_POSITION, app.detail_scroll);
    ok = ok &&
         pxa_ui_create(&transaction, NODE_DETAIL_STATUS, NODE_DETAIL_SCROLL, 0,
                       PXA_UI_NODE_TEXT) &&
         pxa_ui_set_length(&transaction, NODE_DETAIL_STATUS,
                           PXA_UI_PROPERTY_WIDTH, PXA_UI_LENGTH_FILL, 0) &&
         pxa_ui_set_text(&transaction, NODE_DETAIL_STATUS, detail_status_text(),
                         string_length(detail_status_text())) &&
         pxa_ui_set_font_role(&transaction, NODE_DETAIL_STATUS,
                              PXA_UI_FONT_ROLE_CAPTION) &&
         pxa_ui_set_theme_color(&transaction, NODE_DETAIL_STATUS,
                                PXA_UI_PROPERTY_FOREGROUND,
                                detail_status_color());
    /* Hero: generated icon, display name and the release line. */
    {
        char hero_meta[64];
        char size_text[24];
        size_t offset;
        format_size(size_text, sizeof(size_text), item->size);
        hero_meta[0] = '\0';
        offset = append_text(hero_meta, sizeof(hero_meta), 0, "v");
        offset = append_text(hero_meta, sizeof(hero_meta), offset,
                             item->version);
        if (item->platforms[0] != '\0') {
            offset = append_text(hero_meta, sizeof(hero_meta), offset, " · ");
            offset = append_text(hero_meta, sizeof(hero_meta), offset,
                                 item->platforms);
        }
        if (item->size != 0) {
            offset = append_text(hero_meta, sizeof(hero_meta), offset, " · ");
            (void)append_text(hero_meta, sizeof(hero_meta), offset, size_text);
        }
        ok = ok && pxa_ui_create(&transaction, NODE_DETAIL_HERO,
                                 NODE_DETAIL_SCROLL, 0, PXA_UI_NODE_BOX) &&
             pxa_ui_set_length(&transaction, NODE_DETAIL_HERO,
                               PXA_UI_PROPERTY_WIDTH, PXA_UI_LENGTH_FILL, 0) &&
             pxa_ui_set_u8(&transaction, NODE_DETAIL_HERO,
                           PXA_UI_PROPERTY_LAYOUT, PXA_UI_LAYOUT_ROW) &&
             pxa_ui_set_u8(&transaction, NODE_DETAIL_HERO,
                           PXA_UI_PROPERTY_ALIGN, PXA_UI_ALIGN_CENTER) &&
             pxa_ui_set_padding(&transaction, NODE_DETAIL_HERO, 2, 2, 2, 6) &&
             pxa_ui_set_dp(&transaction, NODE_DETAIL_HERO, PXA_UI_PROPERTY_GAP,
                           12) &&
             create_app_icon(&transaction, NODE_DETAIL_HERO_ICON,
                             NODE_DETAIL_HERO_LETTER, NODE_DETAIL_HERO,
                             m->hero_icon, item->name) &&
             pxa_ui_create(&transaction, NODE_DETAIL_HERO_INFO,
                           NODE_DETAIL_HERO, 0, PXA_UI_NODE_BOX) &&
             pxa_ui_set_u16(&transaction, NODE_DETAIL_HERO_INFO,
                            PXA_UI_PROPERTY_GROW, 1) &&
             pxa_ui_set_u8(&transaction, NODE_DETAIL_HERO_INFO,
                           PXA_UI_PROPERTY_LAYOUT, PXA_UI_LAYOUT_COLUMN) &&
             pxa_ui_set_dp(&transaction, NODE_DETAIL_HERO_INFO,
                           PXA_UI_PROPERTY_GAP, 3) &&
             pxa_ui_create(&transaction, NODE_DETAIL_HERO_NAME,
                           NODE_DETAIL_HERO_INFO, 0, PXA_UI_NODE_TEXT) &&
             pxa_ui_set_u16(&transaction, NODE_DETAIL_HERO_NAME,
                            PXA_UI_PROPERTY_GROW, 1) &&
             pxa_ui_set_text(&transaction, NODE_DETAIL_HERO_NAME, item->name,
                             string_length(item->name)) &&
             pxa_ui_set_font_role(&transaction, NODE_DETAIL_HERO_NAME,
                                  PXA_UI_FONT_ROLE_TITLE) &&
             pxa_ui_set_theme_color(&transaction, NODE_DETAIL_HERO_NAME,
                                    PXA_UI_PROPERTY_FOREGROUND,
                                    PXA_UI_THEME_TEXT) &&
             pxa_ui_create(&transaction, NODE_DETAIL_HERO_META,
                           NODE_DETAIL_HERO_INFO, 0, PXA_UI_NODE_TEXT) &&
             pxa_ui_set_length(&transaction, NODE_DETAIL_HERO_META,
                               PXA_UI_PROPERTY_WIDTH, PXA_UI_LENGTH_FILL, 0) &&
             pxa_ui_set_text(&transaction, NODE_DETAIL_HERO_META, hero_meta,
                             string_length(hero_meta)) &&
             pxa_ui_set_font_role(&transaction, NODE_DETAIL_HERO_META,
                                  PXA_UI_FONT_ROLE_CAPTION) &&
             pxa_ui_set_theme_color(&transaction, NODE_DETAIL_HERO_META,
                                    PXA_UI_PROPERTY_FOREGROUND,
                                    PXA_UI_THEME_MUTED);
    }
    app.scratch[0] = '\0';
    (void)append_text(app.scratch, sizeof(app.scratch), 0, item->summary);
    ok = ok && create_section(&transaction, SECTION_SUMMARY,
                              message(PXA_MSG_DETAIL_SUMMARY), app.scratch, 0);
    format_size(size_text, sizeof(size_text), item->size);
    sequence_text[0] = '\0';
    (void)append_u64(sequence_text, sizeof(sequence_text), 0,
                     item->release_sequence);
    sdk_text[0] = '\0';
    {
        size_t offset = append_text(sdk_text, sizeof(sdk_text), 0,
                                    item->min_sdk);
        offset = append_text(sdk_text, sizeof(sdk_text), offset, " - ");
        (void)append_text(sdk_text, sizeof(sdk_text), offset, item->target_sdk);
    }
    format_short_id(id_text, sizeof(id_text), item->publisher_key_id);
    ok = ok && create_row(&transaction, ROW_VERSION,
                          message(PXA_MSG_DETAIL_VERSION), item->version) &&
         create_row(&transaction, ROW_SIZE, message(PXA_MSG_DETAIL_SIZE),
                    size_text) &&
         create_row(&transaction, ROW_CHANNEL, message(PXA_MSG_DETAIL_CHANNEL),
                    item->channel) &&
         create_row(&transaction, ROW_SEQUENCE,
                    message(PXA_MSG_DETAIL_SEQUENCE), sequence_text) &&
         create_row(&transaction, ROW_PUBLISHER,
                    message(PXA_MSG_DETAIL_PUBLISHER), id_text);
    format_short_id(id_text, sizeof(id_text), item->sha256);
    ok = ok && create_row(&transaction, ROW_DIGEST, message(PXA_MSG_DETAIL_DIGEST),
                          id_text) &&
         create_row(&transaction, ROW_PROFILE, message(PXA_MSG_DETAIL_PROFILE),
                    item->target_profile) &&
         create_row(&transaction, ROW_SDK, message(PXA_MSG_DETAIL_SDK),
                    sdk_text) &&
         create_row(&transaction, ROW_PLATFORMS,
                    message(PXA_MSG_DETAIL_PLATFORMS), item->platforms);
    if (ok) {
        copy_text(app.scratch, sizeof(app.scratch),
                  item->changelog[0] != '\0' ? item->changelog
                                             : message(PXA_MSG_DETAIL_NONE));
        ok = create_section(&transaction, SECTION_CHANGELOG,
                            message(PXA_MSG_DETAIL_CHANGELOG), app.scratch, 0);
    }
    if (ok) {
        build_permissions_text(app.scratch, sizeof(app.scratch));
        ok = create_section(&transaction, SECTION_PERMISSIONS,
                            message(PXA_MSG_DETAIL_PERMISSIONS), app.scratch,
                            0);
    }
    if (ok) {
        build_services_text(app.scratch, sizeof(app.scratch));
        ok = create_section(&transaction, SECTION_SERVICES,
                            message(PXA_MSG_DETAIL_SERVICES), app.scratch, 0);
    }
    if (ok) {
        build_install_text(app.scratch, sizeof(app.scratch));
        ok = create_section(&transaction, SECTION_INSTALL,
                            message(PXA_MSG_INSTALL_TITLE), app.scratch, 1);
    }
    if (ok) {
        const char *label = message(PXA_MSG_ACTION_INSTALL);
        ok = pxa_ui_create(&transaction, NODE_FOOTER, NODE_ROOT, 0,
                           PXA_UI_NODE_BOX) &&
             pxa_ui_set_length(&transaction, NODE_FOOTER, PXA_UI_PROPERTY_WIDTH,
                               PXA_UI_LENGTH_FILL, 0) &&
             pxa_ui_set_padding(&transaction, NODE_FOOTER, 12, 2, 12, 6) &&
             pxa_ui_set_theme_color(&transaction, NODE_FOOTER,
                                    PXA_UI_PROPERTY_BACKGROUND,
                                    PXA_UI_THEME_BACKGROUND) &&
             pxa_ui_create_typed(&transaction, NODE_INSTALL_BUTTON, NODE_FOOTER,
                                 0, PXA_UI_NODE_CONTROL,
                                 PXA_UI_CONTROL_BUTTON) &&
             pxa_ui_set_length(&transaction, NODE_INSTALL_BUTTON,
                               PXA_UI_PROPERTY_WIDTH, PXA_UI_LENGTH_FILL, 0) &&
             pxa_ui_set_length(&transaction, NODE_INSTALL_BUTTON,
                               PXA_UI_PROPERTY_HEIGHT, PXA_UI_LENGTH_PX,
                               (uint16_t)(m->action_height + 8)) &&
             pxa_ui_set_u8(&transaction, NODE_INSTALL_BUTTON,
                           PXA_UI_PROPERTY_LAYOUT, PXA_UI_LAYOUT_ROW) &&
             pxa_ui_set_u8(&transaction, NODE_INSTALL_BUTTON,
                           PXA_UI_PROPERTY_JUSTIFY, PXA_UI_ALIGN_CENTER) &&
             pxa_ui_set_u8(&transaction, NODE_INSTALL_BUTTON,
                           PXA_UI_PROPERTY_ALIGN, PXA_UI_ALIGN_CENTER) &&
             pxa_ui_set_dp(&transaction, NODE_INSTALL_BUTTON,
                           PXA_UI_PROPERTY_RADIUS,
                           (m->action_height + 8) / 2) &&
             pxa_ui_set_dp(&transaction, NODE_INSTALL_BUTTON,
                           PXA_UI_PROPERTY_BORDER_WIDTH, 1) &&
             pxa_ui_set_theme_color(&transaction, NODE_INSTALL_BUTTON,
                                    PXA_UI_PROPERTY_BORDER_COLOR,
                                    PXA_UI_THEME_PRIMARY) &&
             pxa_ui_set_theme_color(&transaction, NODE_INSTALL_BUTTON,
                                    PXA_UI_PROPERTY_BACKGROUND,
                                    PXA_UI_THEME_PRIMARY) &&
             pxa_ui_set_u8(&transaction, NODE_INSTALL_BUTTON,
                           PXA_UI_PROPERTY_ENABLED, (uint8_t)(!app.busy)) &&
             pxa_ui_set_event_mask(&transaction, NODE_INSTALL_BUTTON,
                                   PXA_UI_EVENT_MASK_CLICK) &&
             pxa_ui_create(&transaction, NODE_INSTALL_LABEL,
                           NODE_INSTALL_BUTTON, 0, PXA_UI_NODE_TEXT) &&
             pxa_ui_set_text(&transaction, NODE_INSTALL_LABEL, label,
                             string_length(label)) &&
             pxa_ui_set_font_role(&transaction, NODE_INSTALL_LABEL,
                                  PXA_UI_FONT_ROLE_BODY) &&
             pxa_ui_set_theme_color(&transaction, NODE_INSTALL_LABEL,
                                    PXA_UI_PROPERTY_FOREGROUND,
                                    PXA_UI_THEME_ON_PRIMARY);
    }
    if (!ok || !pxa_ui_transaction_commit(&transaction)) {
        if (transaction.active) (void)pxa_ui_transaction_cancel(&transaction);
        return 0;
    }
    app.generation = next;
    return 1;
}

/* Rows of the compact on-screen keyboard: 7 keys per row, 28 keys total. */
static const char *keyboard_labels[4] = {
    "abcdefg",
    "hijklmn",
    "opqrstu",
    "vwxyz..",
};

static const char *keyboard_key_text(uint8_t key, char *scratch,
                                     size_t capacity) {
    if (key < 26u) {
        scratch[0] = keyboard_labels[key / 7u][key % 7u];
        scratch[1] = '\0';
        return scratch;
    }
    if (key == 26u) return message(PXA_MSG_KEY_DELETE);
    return message(PXA_MSG_ACTION_SEARCH);
}

static int create_key(pxa_ui_transaction_t *transaction, uint32_t parent,
                      uint8_t key) {
    const store_metrics_t *m = metrics();
    uint32_t box = NODE_KEY_BASE + (uint32_t)key * NODE_KEY_STRIDE;
    uint32_t label = box + 1u;
    char scratch[8];
    const char *text = keyboard_key_text(key, scratch, sizeof(scratch));
    uint8_t accent = (uint8_t)(key == 27u);
    return pxa_ui_create_typed(transaction, box, parent, 0,
                               PXA_UI_NODE_CONTROL,
                               PXA_UI_CONTROL_BUTTON) &&
           pxa_ui_set_length(transaction, box, PXA_UI_PROPERTY_HEIGHT,
                             PXA_UI_LENGTH_FILL, 0) &&
           pxa_ui_set_u16(transaction, box, PXA_UI_PROPERTY_GROW, 1) &&
           pxa_ui_set_u8(transaction, box, PXA_UI_PROPERTY_LAYOUT,
                         PXA_UI_LAYOUT_ROW) &&
           pxa_ui_set_u8(transaction, box, PXA_UI_PROPERTY_JUSTIFY,
                         PXA_UI_ALIGN_CENTER) &&
           pxa_ui_set_u8(transaction, box, PXA_UI_PROPERTY_ALIGN,
                         PXA_UI_ALIGN_CENTER) &&
           pxa_ui_set_dp(transaction, box, PXA_UI_PROPERTY_RADIUS, 8) &&
           pxa_ui_set_theme_color(transaction, box, PXA_UI_PROPERTY_BACKGROUND,
                                  accent ? PXA_UI_THEME_PRIMARY
                                         : PXA_UI_THEME_SURFACE) &&
           pxa_ui_set_event_mask(transaction, box, PXA_UI_EVENT_MASK_CLICK) &&
           pxa_ui_create(transaction, label, box, 0, PXA_UI_NODE_TEXT) &&
           pxa_ui_set_text(transaction, label, text, string_length(text)) &&
           pxa_ui_set_font_role(transaction, label,
                                key < 26u ? PXA_UI_FONT_ROLE_BODY
                                          : PXA_UI_FONT_ROLE_CAPTION) &&
           pxa_ui_set_theme_color(transaction, label,
                                  PXA_UI_PROPERTY_FOREGROUND,
                                  accent ? PXA_UI_THEME_ON_PRIMARY
                                         : PXA_UI_THEME_TEXT);
}

static int render_search(void) {
    const store_metrics_t *m = metrics();
    pxa_ui_transaction_t transaction = {0};
    uint32_t next = app.generation + 1u;
    uint8_t row;
    int ok;
    if (next == 0 ||
        !pxa_ui_transaction_begin(&transaction, next,
                                  PXA_UI_TRANSACTION_REPLACE_SURFACE,
                                  app.packet, sizeof(app.packet)))
        return 0;
    ok = pxa_ui_create(&transaction, NODE_ROOT, 0, 0, PXA_UI_NODE_ROOT) &&
         pxa_ui_set_u8(&transaction, NODE_ROOT, PXA_UI_PROPERTY_LAYOUT,
                       PXA_UI_LAYOUT_COLUMN) &&
         pxa_ui_set_theme_color(&transaction, NODE_ROOT,
                                PXA_UI_PROPERTY_BACKGROUND,
                                PXA_UI_THEME_BACKGROUND) &&
         pxa_ui_set_padding(&transaction, NODE_ROOT,
                            inset_padding(3, m->list_pad_h),
                            inset_padding(0, m->header_pad_v),
                            inset_padding(1, m->list_pad_h),
                            inset_padding(2, 6)) &&
         pxa_ui_create(&transaction, NODE_HEADER, NODE_ROOT, 0,
                       PXA_UI_NODE_BOX) &&
         pxa_ui_set_length(&transaction, NODE_HEADER, PXA_UI_PROPERTY_WIDTH,
                           PXA_UI_LENGTH_FILL, 0) &&
         pxa_ui_set_u8(&transaction, NODE_HEADER, PXA_UI_PROPERTY_LAYOUT,
                       PXA_UI_LAYOUT_ROW) &&
         pxa_ui_set_u8(&transaction, NODE_HEADER, PXA_UI_PROPERTY_ALIGN,
                       PXA_UI_ALIGN_CENTER) &&
         pxa_ui_set_padding(&transaction, NODE_HEADER, m->header_pad_h, 6,
                       m->header_pad_h, 6) &&
         pxa_ui_set_dp(&transaction, NODE_HEADER, PXA_UI_PROPERTY_GAP, 8) &&
         pxa_ui_set_theme_color(&transaction, NODE_HEADER,
                                PXA_UI_PROPERTY_BACKGROUND,
                                PXA_UI_THEME_BACKGROUND) &&
         pxa_ui_create_typed(&transaction, NODE_SEARCH_BACK, NODE_HEADER, 0,
                             PXA_UI_NODE_CONTROL, PXA_UI_CONTROL_BUTTON) &&
         pxa_ui_set_length(&transaction, NODE_SEARCH_BACK,
                           PXA_UI_PROPERTY_HEIGHT, PXA_UI_LENGTH_PX, 30) &&
         pxa_ui_set_u8(&transaction, NODE_SEARCH_BACK, PXA_UI_PROPERTY_LAYOUT,
                       PXA_UI_LAYOUT_ROW) &&
         pxa_ui_set_u8(&transaction, NODE_SEARCH_BACK, PXA_UI_PROPERTY_JUSTIFY,
                       PXA_UI_ALIGN_CENTER) &&
         pxa_ui_set_padding(&transaction, NODE_SEARCH_BACK, 12, 2, 12, 2) &&
         pxa_ui_set_dp(&transaction, NODE_SEARCH_BACK, PXA_UI_PROPERTY_RADIUS,
                       15) &&
         pxa_ui_set_dp(&transaction, NODE_SEARCH_BACK,
                       PXA_UI_PROPERTY_BORDER_WIDTH, 1) &&
         pxa_ui_set_theme_color(&transaction, NODE_SEARCH_BACK,
                                PXA_UI_PROPERTY_BORDER_COLOR,
                                PXA_UI_THEME_BORDER) &&
         pxa_ui_set_theme_color(&transaction, NODE_SEARCH_BACK,
                                PXA_UI_PROPERTY_BACKGROUND,
                                PXA_UI_THEME_SURFACE) &&
         pxa_ui_set_event_mask(&transaction, NODE_SEARCH_BACK,
                               PXA_UI_EVENT_MASK_CLICK) &&
         pxa_ui_create(&transaction, NODE_SEARCH_BACK_LABEL, NODE_SEARCH_BACK, 0,
                       PXA_UI_NODE_TEXT) &&
         pxa_ui_set_text(&transaction, NODE_SEARCH_BACK_LABEL,
                         message(PXA_MSG_ACTION_BACK),
                         string_length(message(PXA_MSG_ACTION_BACK))) &&
         pxa_ui_set_font_role(&transaction, NODE_SEARCH_BACK_LABEL,
                              PXA_UI_FONT_ROLE_CAPTION) &&
         pxa_ui_set_theme_color(&transaction, NODE_SEARCH_BACK_LABEL,
                                PXA_UI_PROPERTY_FOREGROUND,
                                PXA_UI_THEME_PRIMARY) &&
         pxa_ui_create(&transaction, NODE_SEARCH_TITLE, NODE_HEADER, 0,
                       PXA_UI_NODE_TEXT) &&
         pxa_ui_set_u16(&transaction, NODE_SEARCH_TITLE, PXA_UI_PROPERTY_GROW,
                        1) &&
         pxa_ui_set_text(&transaction, NODE_SEARCH_TITLE,
                         message(PXA_MSG_SEARCH_TITLE),
                         pxa_i18n_size(&app.i18n, PXA_MSG_SEARCH_TITLE)) &&
         pxa_ui_set_font_role(&transaction, NODE_SEARCH_TITLE,
                              PXA_UI_FONT_ROLE_TITLE) &&
         pxa_ui_set_theme_color(&transaction, NODE_SEARCH_TITLE,
                                PXA_UI_PROPERTY_FOREGROUND,
                                PXA_UI_THEME_TEXT) &&
         pxa_ui_create_typed(&transaction, NODE_SEARCH_CLEAR, NODE_HEADER, 0,
                             PXA_UI_NODE_CONTROL, PXA_UI_CONTROL_BUTTON) &&
         pxa_ui_set_length(&transaction, NODE_SEARCH_CLEAR,
                           PXA_UI_PROPERTY_HEIGHT, PXA_UI_LENGTH_PX, 30) &&
         pxa_ui_set_u8(&transaction, NODE_SEARCH_CLEAR, PXA_UI_PROPERTY_LAYOUT,
                       PXA_UI_LAYOUT_ROW) &&
         pxa_ui_set_u8(&transaction, NODE_SEARCH_CLEAR, PXA_UI_PROPERTY_JUSTIFY,
                       PXA_UI_ALIGN_CENTER) &&
         pxa_ui_set_padding(&transaction, NODE_SEARCH_CLEAR, 12, 2, 12, 2) &&
         pxa_ui_set_dp(&transaction, NODE_SEARCH_CLEAR, PXA_UI_PROPERTY_RADIUS,
                       15) &&
         pxa_ui_set_dp(&transaction, NODE_SEARCH_CLEAR,
                       PXA_UI_PROPERTY_BORDER_WIDTH, 1) &&
         pxa_ui_set_theme_color(&transaction, NODE_SEARCH_CLEAR,
                                PXA_UI_PROPERTY_BORDER_COLOR,
                                PXA_UI_THEME_BORDER) &&
         pxa_ui_set_theme_color(&transaction, NODE_SEARCH_CLEAR,
                                PXA_UI_PROPERTY_BACKGROUND,
                                PXA_UI_THEME_SURFACE) &&
         pxa_ui_set_event_mask(&transaction, NODE_SEARCH_CLEAR,
                               PXA_UI_EVENT_MASK_CLICK) &&
         pxa_ui_create(&transaction, NODE_SEARCH_CLEAR_LABEL, NODE_SEARCH_CLEAR,
                       0, PXA_UI_NODE_TEXT) &&
         pxa_ui_set_text(&transaction, NODE_SEARCH_CLEAR_LABEL,
                         message(PXA_MSG_ACTION_CLEAR),
                         string_length(message(PXA_MSG_ACTION_CLEAR))) &&
         pxa_ui_set_font_role(&transaction, NODE_SEARCH_CLEAR_LABEL,
                              PXA_UI_FONT_ROLE_CAPTION) &&
         pxa_ui_set_theme_color(&transaction, NODE_SEARCH_CLEAR_LABEL,
                                PXA_UI_PROPERTY_FOREGROUND,
                                PXA_UI_THEME_PRIMARY);
    if (!ok) goto failed;
    /* Search field. */
    {
        const char *text = app.draft[0] != '\0'
                               ? app.draft
                               : message(PXA_MSG_SEARCH_PLACEHOLDER);
        uint8_t empty = (uint8_t)(app.draft[0] == '\0');
        (void)text;
        (void)empty;
#if STORE_SEARCH_TEXT_INPUT
        /* A real text input: the system input method can attach to it and
         * reports edits as text events. The label underneath only shows the
         * placeholder while the query is empty. */
        ok = pxa_ui_create_typed(&transaction, NODE_SEARCH_FIELD, NODE_ROOT, 0,
                                 PXA_UI_NODE_CONTROL,
                                 PXA_UI_CONTROL_TEXT_INPUT) &&
             pxa_ui_set_length(&transaction, NODE_SEARCH_FIELD,
                               PXA_UI_PROPERTY_WIDTH, PXA_UI_LENGTH_FILL, 0) &&
             pxa_ui_set_length(&transaction, NODE_SEARCH_FIELD,
                               PXA_UI_PROPERTY_HEIGHT, PXA_UI_LENGTH_PX,
                               m->search_height) &&
             pxa_ui_set_padding(&transaction, NODE_SEARCH_FIELD, 14, 6, 14, 6) &&
             pxa_ui_set_dp(&transaction, NODE_SEARCH_FIELD,
                           PXA_UI_PROPERTY_RADIUS, 12) &&
             pxa_ui_set_dp(&transaction, NODE_SEARCH_FIELD,
                           PXA_UI_PROPERTY_BORDER_WIDTH, 1) &&
             pxa_ui_set_theme_color(&transaction, NODE_SEARCH_FIELD,
                                    PXA_UI_PROPERTY_BORDER_COLOR,
                                    PXA_UI_THEME_BORDER) &&
             pxa_ui_set_theme_color(&transaction, NODE_SEARCH_FIELD,
                                    PXA_UI_PROPERTY_BACKGROUND,
                                    PXA_UI_THEME_SURFACE) &&
             pxa_ui_set_event_mask(&transaction, NODE_SEARCH_FIELD,
                                   PXA_UI_EVENT_MASK_CLICK |
                                       PXA_UI_EVENT_MASK_TEXT) &&
             pxa_ui_set_text(&transaction, NODE_SEARCH_FIELD, app.draft,
                             string_length(app.draft));
#else
        ok = pxa_ui_create_typed(&transaction, NODE_SEARCH_FIELD, NODE_ROOT, 0,
                                 PXA_UI_NODE_CONTROL,
                                 PXA_UI_CONTROL_BUTTON) &&
             pxa_ui_set_length(&transaction, NODE_SEARCH_FIELD,
                               PXA_UI_PROPERTY_WIDTH, PXA_UI_LENGTH_FILL, 0) &&
             pxa_ui_set_length(&transaction, NODE_SEARCH_FIELD,
                               PXA_UI_PROPERTY_HEIGHT, PXA_UI_LENGTH_PX,
                               m->search_height) &&
             pxa_ui_set_u8(&transaction, NODE_SEARCH_FIELD,
                           PXA_UI_PROPERTY_LAYOUT, PXA_UI_LAYOUT_ROW) &&
             pxa_ui_set_u8(&transaction, NODE_SEARCH_FIELD,
                           PXA_UI_PROPERTY_ALIGN, PXA_UI_ALIGN_CENTER) &&
             pxa_ui_set_padding(&transaction, NODE_SEARCH_FIELD, 14, 6, 14, 6) &&
             pxa_ui_set_dp(&transaction, NODE_SEARCH_FIELD,
                           PXA_UI_PROPERTY_RADIUS, 12) &&
             pxa_ui_set_dp(&transaction, NODE_SEARCH_FIELD,
                           PXA_UI_PROPERTY_BORDER_WIDTH, 1) &&
             pxa_ui_set_theme_color(&transaction, NODE_SEARCH_FIELD,
                                    PXA_UI_PROPERTY_BORDER_COLOR,
                                    PXA_UI_THEME_BORDER) &&
             pxa_ui_set_theme_color(&transaction, NODE_SEARCH_FIELD,
                                    PXA_UI_PROPERTY_BACKGROUND,
                                    PXA_UI_THEME_SURFACE) &&
             pxa_ui_set_event_mask(&transaction, NODE_SEARCH_FIELD,
                                   PXA_UI_EVENT_MASK_CLICK) &&
             pxa_ui_create(&transaction, NODE_SEARCH_TEXT, NODE_SEARCH_FIELD, 0,
                           PXA_UI_NODE_TEXT) &&
             pxa_ui_set_u16(&transaction, NODE_SEARCH_TEXT,
                            PXA_UI_PROPERTY_GROW, 1) &&
             pxa_ui_set_text(&transaction, NODE_SEARCH_TEXT, text,
                             string_length(text)) &&
             pxa_ui_set_theme_color(&transaction, NODE_SEARCH_TEXT,
                                    PXA_UI_PROPERTY_FOREGROUND,
                                    empty ? PXA_UI_THEME_MUTED
                                          : PXA_UI_THEME_TEXT);
#endif
    }
    if (!ok) goto failed;
#if !STORE_SEARCH_TEXT_INPUT
    ok = pxa_ui_create(&transaction, NODE_SEARCH_KEYBOARD, NODE_ROOT, 0,
                       PXA_UI_NODE_BOX) &&
         pxa_ui_set_length(&transaction, NODE_SEARCH_KEYBOARD,
                           PXA_UI_PROPERTY_WIDTH, PXA_UI_LENGTH_FILL, 0) &&
         pxa_ui_set_u16(&transaction, NODE_SEARCH_KEYBOARD,
                        PXA_UI_PROPERTY_GROW, 1) &&
         pxa_ui_set_u8(&transaction, NODE_SEARCH_KEYBOARD,
                       PXA_UI_PROPERTY_LAYOUT, PXA_UI_LAYOUT_COLUMN) &&
         pxa_ui_set_padding(&transaction, NODE_SEARCH_KEYBOARD, m->list_pad_h, 4,
                       m->list_pad_h, 8) &&
         pxa_ui_set_dp(&transaction, NODE_SEARCH_KEYBOARD, PXA_UI_PROPERTY_GAP,
                       m->key_gap);
    for (row = 0; ok && row < 4u; ++row) {
        uint32_t row_node = NODE_SEARCH_ROW_BASE + row;
        uint8_t column;
        ok = pxa_ui_create(&transaction, row_node, NODE_SEARCH_KEYBOARD, 0,
                           PXA_UI_NODE_BOX) &&
             pxa_ui_set_length(&transaction, row_node, PXA_UI_PROPERTY_WIDTH,
                               PXA_UI_LENGTH_FILL, 0) &&
             pxa_ui_set_length(&transaction, row_node, PXA_UI_PROPERTY_HEIGHT,
                               PXA_UI_LENGTH_PX, 0) &&
             pxa_ui_set_u16(&transaction, row_node, PXA_UI_PROPERTY_GROW, 1) &&
             pxa_ui_set_u8(&transaction, row_node, PXA_UI_PROPERTY_LAYOUT,
                           PXA_UI_LAYOUT_ROW) &&
             pxa_ui_set_dp(&transaction, row_node, PXA_UI_PROPERTY_GAP,
                       m->key_gap);
        for (column = 0; ok && column < 7u; ++column) {
            uint8_t key = (uint8_t)(row * 7u + column);
            if (key == 27u && column != 6u) {
                /* only the last cell of the last row holds the search key */
                ok = pxa_ui_create(&transaction, NODE_KEY_BASE +
                                   (uint32_t)key * NODE_KEY_STRIDE, row_node, 0,
                                   PXA_UI_NODE_BOX) &&
                     pxa_ui_set_u16(&transaction, NODE_KEY_BASE +
                                    (uint32_t)key * NODE_KEY_STRIDE,
                                    PXA_UI_PROPERTY_GROW, 1);
                continue;
            }
            ok = create_key(&transaction, row_node, key);
        }
    }
#endif /* !STORE_SEARCH_TEXT_INPUT */
    if (!ok || !pxa_ui_transaction_commit(&transaction)) {
failed:
        store_trace("search build ok", ok ? 1u : 0u);
        if (transaction.active) (void)pxa_ui_transaction_cancel(&transaction);
        return 0;
    }
    app.generation = next;
    return 1;
}

#if STORE_SEARCH_TEXT_INPUT
/* Mirrors the draft query into the text input. The system input method writes
 * the field itself, so this only runs for edits made on the built-in
 * keyboard. */
static int sync_search_field(void) {
    pxa_ui_transaction_t transaction = {0};
    uint32_t next = app.generation + 1u;
    uint8_t empty = (uint8_t)(app.draft[0] == '\0');
    const char *hint = message(PXA_MSG_SEARCH_PLACEHOLDER);
    if (app.screen != STORE_SCREEN_SEARCH || next == 0 ||
        !pxa_ui_transaction_begin(&transaction, next,
                                  PXA_UI_TRANSACTION_PATCH, app.packet,
                                  sizeof(app.packet)))
        return 0;
    if (!pxa_ui_set_text(&transaction, NODE_SEARCH_FIELD, app.draft,
                         string_length(app.draft)) ||
        !pxa_ui_set_text(&transaction, NODE_SEARCH_TEXT,
                         empty ? hint : "",
                         empty ? string_length(hint) : 0) ||
        !pxa_ui_transaction_commit(&transaction)) {
        if (transaction.active) (void)pxa_ui_transaction_cancel(&transaction);
        return 0;
    }
    app.generation = next;
    return 1;
}

/* Copies the text the system input method reported into the draft query. */
static void adopt_field_text(const char *text, size_t length) {
    size_t copy = length < sizeof(app.draft) ? length : sizeof(app.draft) - 1u;
    size_t index;
    for (index = 0; index < copy; ++index) app.draft[index] = text[index];
    app.draft[copy] = '\0';
}
#endif

static int render(void) {
    if (app.screen == STORE_SCREEN_SEARCH) return render_search();
    return app.screen == STORE_SCREEN_DETAIL ? render_detail()
                                             : render_catalog();
}

/* ------------------------------------------------------------------ */
/* Catalog requests                                                   */
/* ------------------------------------------------------------------ */

/* Chrome visibility is a small patch transaction: scrolling down hides the
 * header, the filter row and the tab bar, scrolling up brings them back. */
#define STORE_CHROME_HIDE_DELTA 12
/* A drag this far in one direction reads as "scroll up" / "scroll down". */
#define STORE_CHROME_DRAG_DELTA 4
/* Near the top the bars always come back, the way a phone behaves. */
#define STORE_CHROME_TOP_MARGIN 8
#define STORE_CHROME_SHOW_DELTA 4

static int set_chrome_visible(uint8_t visible) {
    pxa_ui_transaction_t transaction = {0};
    uint32_t next = app.generation + 1u;
    int ok;
    if (visible == (uint8_t)(!app.chrome_hidden)) return 1;
    if (next == 0 ||
        !pxa_ui_transaction_begin_target(&transaction, next, next,
                                         PXA_UI_PRIMARY_SURFACE, 0,
                                         PXA_UI_TRANSACTION_PATCH, app.packet,
                                         sizeof(app.packet)))
        return 0;
    ok = pxa_ui_set_u8(&transaction, NODE_HEADER, PXA_UI_PROPERTY_VISIBLE,
                       visible) &&
         pxa_ui_set_u8(&transaction, NODE_FILTERS, PXA_UI_PROPERTY_VISIBLE,
                       visible) &&
         pxa_ui_set_u8(&transaction, NODE_TABBAR, PXA_UI_PROPERTY_VISIBLE,
                       visible);
    if (!ok || !pxa_ui_transaction_commit(&transaction)) {
        if (transaction.active) (void)pxa_ui_transaction_cancel(&transaction);
        return 0;
    }
    app.generation = next;
    app.chrome_hidden = (uint8_t)(!visible);
    store_trace("chrome", visible);
    return 1;
}

/* LVGL scrolls a newly focused card into view after the surface layout, which
 * would leave the list scrolled on entry. Re-apply the tracked offset with a
 * patch transaction so the commit's scroll restore wins. */
static int restore_scroll(uint32_t node, int32_t offset) {
    pxa_ui_transaction_t transaction = {0};
    uint32_t next = app.generation + 1u;
    int ok;
    if (next == 0 ||
        !pxa_ui_transaction_begin_target(&transaction, next, next,
                                         PXA_UI_PRIMARY_SURFACE, 0,
                                         PXA_UI_TRANSACTION_PATCH, app.packet,
                                         sizeof(app.packet)))
        return 0;
    ok = pxa_ui_set_dp(&transaction, node, PXA_UI_PROPERTY_SCROLL_POSITION,
                       offset);
    if (!ok || !pxa_ui_transaction_commit(&transaction)) {
        if (transaction.active) (void)pxa_ui_transaction_cancel(&transaction);
        return 0;
    }
    app.generation = next;
    return 1;
}

static void show_chrome(void) {
    if (app.chrome_hidden) (void)set_chrome_visible(1);
}

static int fail_request(uint8_t error) {
    store_trace("catalog failure", error);
    (void)pxa_clock_set_period(0);
    if (app.client.body_handle != 0)
        (void)store_client_close_body(&app.client, app.packet,
                                      sizeof(app.packet));
    app.state = STORE_FAILED;
    app.error = error;
    app.busy = 0;
    return render();
}

static int start_catalog_fetch(void) {
    store_trace("catalog fetch cursor", (uint32_t)app.request_cursor);
    app.busy = 1;
    app.state = STORE_LOADING;
    app.pending_request = STORE_REQUEST_CATALOG;
    if (!store_client_build_catalog_url(app.url, sizeof(app.url),
                                        app.device_id, app.request_cursor,
                                        app.query, selected_kind_value(),
                                        selected_category_value()))
        return 0;
    return store_client_fetch(
        &app.client, STORE_REQUEST_CATALOG, app.url, string_length(app.url),
        (uint32_t)sizeof(app.body), app.payload, sizeof(app.payload),
        app.packet, sizeof(app.packet));
}

/* Fetches the full metadata for one catalog entry. The compact catalog page
 * omits compatibility detail, so the detail screen requests it separately. */
static int start_detail_fetch(void) {
    const store_app_t *item = detail_item();
    store_trace("detail fetch index", app.selected);
    app.detail_ready = 0;
    app.detail_state = STORE_LOADING;
    if (!app.client.has_network_permission) {
        app.detail_state = STORE_FAILED;
        return 0;
    }
    if (!store_client_build_app_url(app.url, sizeof(app.url), item->app_id,
                                    app.device_id))
        return 0;
    app.pending_request = STORE_REQUEST_DETAIL;
    app.detail_target = app.selected;
    if (!store_client_fetch(
            &app.client, STORE_REQUEST_DETAIL, app.url, string_length(app.url),
            (uint32_t)sizeof(app.body), app.payload, sizeof(app.payload),
            app.packet, sizeof(app.packet)))
        return 0;
    app.busy = 1;
    return 1;
}

/* Queues the permission, identity and catalog steps in order. A send failure
 * means the Host has no such service, which the SDK helper reports only as
 * failure. */
/* Queues the next step of the catalog sequence: device permission, device
 * identity, network permission, then the catalog request itself. Result
 * handlers call this to continue, so it must not check app.busy. */
static int advance_catalog_request(void) {
    if (!app.client.has_device_permission) {
        store_trace("acquire device permission", 1);
        if (!store_client_acquire_device_permission(
                &app.client, app.payload, sizeof(app.payload), app.packet,
                sizeof(app.packet))) {
            (void)fail_request(STORE_ERROR_UNSUPPORTED);
            return 0;
        }
        return 1;
    }
    if (!app.client.has_mac) {
        store_trace("fetch device mac", 1);
        if (!store_client_fetch_mac(&app.client, app.payload,
                                    sizeof(app.payload), app.packet,
                                    sizeof(app.packet))) {
            (void)fail_request(STORE_ERROR_UNSUPPORTED);
            return 0;
        }
        return 1;
    }
    if (!app.client.has_network_permission) {
        store_trace("acquire network permission", 1);
        if (!store_client_acquire_network_permission(
                &app.client, app.payload, sizeof(app.payload), app.packet,
                sizeof(app.packet))) {
            (void)fail_request(STORE_ERROR_UNSUPPORTED);
            return 0;
        }
        return 1;
    }
    if (!start_catalog_fetch()) {
        (void)fail_request(STORE_ERROR_UNSUPPORTED);
        return 0;
    }
    return 1;
}

static int start_catalog_request(void) {
    if (app.busy) {
        /* Do not drop the request: replay it when the running one finishes. */
        app.pending_restart = 1;
        return 1;
    }
    app.busy = 1;
    app.state = STORE_LOADING;
    app.error = STORE_ERROR_NONE;
    return advance_catalog_request();
}

static int restart_catalog(const char *query);

/* Drops the last UTF-8 codepoint from the draft query. */
static void draft_backspace(void) {
    size_t length = string_length(app.draft);
    if (length == 0) return;
    --length;
    while (length > 0 && ((uint8_t)app.draft[length] & 0xc0u) == 0x80u) --length;
    app.draft[length] = '\0';
}

static int open_search(void) {
    copy_text(app.draft, sizeof(app.draft), app.query);
    app.screen = STORE_SCREEN_SEARCH;
    show_chrome();
    return render();
}

static int apply_search(void) {
    app.screen = STORE_SCREEN_CATALOG;
    if (text_equal(app.query, app.draft)) return render();
    if (app.busy) {
        /* The catalog is still loading; keep the query and apply it when the
         * running request completes instead of dropping the user's input. */
        copy_text(app.pending_search, sizeof(app.pending_search), app.draft);
        app.pending_search_valid = 1;
        return render();
    }
    return restart_catalog(app.draft);
}

/* Applies a query that arrived while a request was running. */
static int apply_pending_search(void) {
    if (!app.pending_search_valid) return 1;
    app.pending_search_valid = 0;
    if (text_equal(app.query, app.pending_search)) return 1;
    return restart_catalog(app.pending_search);
}

static int select_kind(uint8_t chip_index) {
    app.kind_index = chip_index;
    app.category_entry = -1;
    app.list_scroll = 0;
    return restart_catalog(app.query);
}

static int select_category(int16_t entry) {
    app.category_entry = entry;
    app.list_scroll = 0;
    return restart_catalog(app.query);
}

static int restart_catalog(const char *query) {
    copy_text(app.query, sizeof(app.query), query == NULL ? "" : query);
    app.catalog.head = 0;
    app.catalog.count = 0;
    app.catalog.dropped = 0;
    app.catalog.has_more = 0;
    app.catalog.next_cursor = 0;
    app.request_cursor = 0;
    app.last_scroll = 0;
    app.auto_load_mark = 0;
    app.chain = 0;
    app.selected = 0;
    app.install_feedback = 0;
    app.list_scroll = 0;
    app.last_scroll = 0;
    app.detail_scroll = 0;
    return start_catalog_request();
}

static int continue_body(void);

/* A page that does not fill the list leaves it barely scrollable, which would
 * stall the incremental loading that the scroll offset drives. Keep fetching
 * while the content is shorter than the viewport plus one card. */
static int catalog_needs_fill(void) {
    const store_metrics_t *m = metrics();
    int32_t view;
    int32_t content;
    if (!app.catalog.has_more) return 0;
    if (app.catalog.count == app.fill_count) return 0;
    view = (int32_t)app.height -
           (int32_t)(m->header_height + m->chips_row_height) -
           (int32_t)(m->tab_height + m->card_gap);
    if (view < 0) view = 0;
    content = (int32_t)app.catalog.count *
              (int32_t)(m->card_height + m->list_gap);
    return content < view + (int32_t)m->card_height;
}

static int finish_catalog_body(void) {
    uint8_t before = app.catalog.count;
    (void)pxa_clock_set_period(0);
    if (app.client.body_handle != 0 &&
        !store_client_close_body(&app.client, app.packet, sizeof(app.packet)))
        return fail_request(STORE_ERROR_FAILED);
    if ((app.client.response_flags & PXA_NET_RESPONSE_BODY_LENGTH_KNOWN) != 0 &&
        app.client.response_length != app.client.response_size)
        return fail_request(STORE_ERROR_PROTOCOL);
    app.catalog.has_more = 0;
    if (!store_json_parse_catalog(app.body, app.client.response_size,
                                  &app.catalog, &app.taxonomy))
        return fail_request(STORE_ERROR_PROTOCOL);
    if (app.kind_index > app.taxonomy.kind_count)
        app.kind_index = kind_entry_for(PXA_STORE_DEFAULT_KIND);
    if (app.category_entry >= (int16_t)app.taxonomy.category_count)
        app.category_entry = -1;
    app.request_cursor = app.catalog.next_cursor;
    if (app.catalog.dropped) {
        /* The window slid by one card; keep the viewport where it was. */
        store_metrics_t metrics_value = *metrics();
        int32_t removed = metrics_value.card_height + metrics_value.card_gap;
        app.list_scroll -= removed;
        if (app.list_scroll < 0) app.list_scroll = 0;
        app.last_scroll = app.list_scroll;
        app.catalog.dropped = 0;
    }
    app.busy = 0;
    if (app.pending_restart) {
        app.pending_restart = 0;
        app.pending_search_valid = 0;
        return restart_catalog(app.query);
    }
    if (app.pending_search_valid) {
        /* A query submitted while this request was running. */
        app.pending_search_valid = 0;
        if (!text_equal(app.query, app.pending_search))
            return restart_catalog(app.pending_search);
    }
    if (app.catalog.count == before && app.catalog.has_more &&
        app.chain < STORE_MAX_CHAIN) {
        ++app.chain;
        app.state = STORE_READY;
        if (!render()) return 0;
        if (!start_catalog_fetch()) return fail_request(STORE_ERROR_UNSUPPORTED);
        return 1;
    }
    app.state = STORE_READY;
    ensure_window_snapshot();
    if (!render()) return 0;
    (void)restore_scroll(NODE_LIST, app.list_scroll);
    if (catalog_needs_fill()) {
        app.fill_count = app.catalog.count;
        app.request_cursor = app.catalog.next_cursor;
        app.chain = 0;
        if (!start_catalog_fetch()) return fail_request(STORE_ERROR_UNSUPPORTED);
    }
    return 1;
}

static int fail_detail(uint8_t error) {
    store_trace("detail failure", error);
    (void)pxa_clock_set_period(0);
    if (app.client.body_handle != 0)
        (void)store_client_close_body(&app.client, app.packet,
                                      sizeof(app.packet));
    app.busy = 0;
    if (app.detail_target != app.selected) {
        /* The user already opened another entry; drop this result. */
        app.detail_state = STORE_IDLE;
        app.detail_ready = 0;
        return render();
    }
    app.detail_state = STORE_FAILED;
    app.detail_ready = 0;
    app.error = error;
    return render();
}

static int finish_detail_body(void) {
    (void)pxa_clock_set_period(0);
    if (app.client.body_handle != 0 &&
        !store_client_close_body(&app.client, app.packet, sizeof(app.packet)))
        return fail_detail(STORE_ERROR_FAILED);
    if ((app.client.response_flags & PXA_NET_RESPONSE_BODY_LENGTH_KNOWN) != 0 &&
        app.client.response_length != app.client.response_size)
        return fail_detail(STORE_ERROR_PROTOCOL);
    app.busy = 0;
    if (app.detail_target != app.selected) {
        app.detail_state = STORE_IDLE;
        app.detail_ready = 0;
        return render();
    }
    if (!store_json_parse_app_detail(app.body, app.client.response_size,
                                     &app.detail))
        return fail_detail(STORE_ERROR_PROTOCOL);
    app.detail_ready = 1;
    app.detail_state = STORE_READY;
    store_trace("detail ready", 1);
    if (!render()) return 0;
    (void)restore_scroll(NODE_DETAIL_SCROLL, app.detail_scroll);
    return 1;
}

static int continue_body(void) {
    int result;
    store_trace("continue body", app.client.response_size);
    result = store_client_consume_body(
        &app.client, app.body, sizeof(app.body), app.packet, sizeof(app.packet));
    if (result == STORE_BODY_WAITING) return render();
    if (result == STORE_BODY_DONE) {
        if (app.pending_request == STORE_REQUEST_DETAIL)
            return finish_detail_body();
        return finish_catalog_body();
    }
    if (app.pending_request == STORE_REQUEST_DETAIL)
        return fail_detail(result == STORE_BODY_LIMIT ? STORE_ERROR_LIMIT
                                                      : STORE_ERROR_FAILED);
    return fail_request(result == STORE_BODY_LIMIT ? STORE_ERROR_LIMIT
                                                   : STORE_ERROR_FAILED);
}

static int handle_json_result(const pxa_event_t *parsed) {
    pxa_net_http_result_t result;
    uint8_t detail = (uint8_t)(parsed->request_id == STORE_REQUEST_DETAIL);
    store_trace("net result parsed", pxa_net_parse_http_result(parsed, &result));
    if (!pxa_net_parse_http_result(parsed, &result))
        return detail ? fail_detail(STORE_ERROR_PROTOCOL)
                      : fail_request(STORE_ERROR_PROTOCOL);
    if (result.status != PXA_STATUS_OK) {
        app.error = error_from_status(result.status);
        if (detail) {
            app.detail_state = STORE_FAILED;
            app.detail_ready = 0;
        } else {
            app.state = STORE_FAILED;
        }
        app.busy = 0;
        if (app.pending_restart) {
            app.pending_restart = 0;
            return restart_catalog(app.query);
        }
        if (!apply_pending_search()) return 0;
        return render();
    }
    app.client.http_status = result.status_code;
    app.client.response_flags = result.flags;
    app.client.response_length = result.body_length;
    app.client.body_handle = result.body_handle;
    if (result.status_code != 200 || result.body_handle == 0) {
        if (result.body_handle != 0)
            (void)store_client_close_body(&app.client, app.packet,
                                          sizeof(app.packet));
        app.error = result.status_code == 404 ? STORE_ERROR_NOT_FOUND
                                              : STORE_ERROR_HTTP;
        if (detail) {
            app.detail_state = STORE_FAILED;
            app.detail_ready = 0;
        } else {
            app.state = STORE_FAILED;
        }
        app.busy = 0;
        return render();
    }
    return continue_body();
}

static int handle_permission_result(const pxa_event_t *parsed) {
    pxa_permission_acquire_result_t result;
    store_trace("permission result parsed", pxa_permission_parse_acquire(parsed, &result));
    if (!pxa_permission_parse_acquire(parsed, &result))
        return fail_request(STORE_ERROR_PROTOCOL);
    if (result.status != PXA_STATUS_OK) {
        app.state = STORE_DENIED;
        app.busy = 0;
        return render();
    }
    if (parsed->request_id == STORE_REQUEST_DEVICE_PERMISSION) {
        app.client.device_permission = result.handle;
        app.client.has_device_permission = 1;
    } else {
        app.client.network_permission = result.handle;
        app.client.has_network_permission = 1;
    }
    if (!advance_catalog_request()) return PXA_EVENT_HANDLED;
    return render() ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
}

static int handle_mac_result(const pxa_event_t *parsed) {
    pxa_device_mac_result_t result;
    store_trace("mac result parsed", pxa_device_parse_mac(parsed, &result));
    if (!pxa_device_parse_mac(parsed, &result) ||
        result.status != PXA_STATUS_OK ||
        result.kind != PXA_DEVICE_MAC_KIND_WIFI_STATION_HARDWARE) {
        /* A stable device identity only affects rollout bucketing. */
        copy_text(app.device_id, sizeof(app.device_id), "");
    } else {
        (void)pxa_device_format_mac_colon(result.mac, app.device_id,
                                          sizeof(app.device_id));
    }
    app.client.has_mac = 1;
    if (!advance_catalog_request()) return PXA_EVENT_HANDLED;
    return render() ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
}

/* An ordinary application window: the system keeps its status and navigation
 * bars visible, which also keeps the system back gesture available. Games
 * request the fullscreen edge-to-edge window instead. */
static int configure_window(void) {
    uint8_t records[15];
    pxa_writer_t writer;
    const uint8_t edge_to_edge = 0;
    const uint8_t visible = PXA_WINDOW_BAR_VISIBLE;
    pxa_writer_init(&writer, records, sizeof(records));
    return pxa_record(&writer, PXA_WINDOW_EDGE_TO_EDGE, &edge_to_edge, 1) &&
           pxa_record(&writer, PXA_WINDOW_STATUS_BAR_MODE, &visible, 1) &&
           pxa_record(&writer, PXA_WINDOW_NAVIGATION_BAR_MODE, &visible, 1) &&
           pxa_send(PXA_SERVICE_WINDOW, PXA_WINDOW_CONFIGURE, 0, writer.data,
                    writer.length);
}

static void apply_environment(const pxa_ui_environment_t *environment) {
    uint8_t index;
    for (index = 0; index < 4u; ++index)
        app.safe_insets[index] = environment->safe_insets[index];
    if (environment->width != 0) app.width = environment->width;
    if (environment->height != 0) app.height = environment->height;
    app.has_environment = 1;
}

/* The UI environment reports the panel's physical safe area. The Window
 * service additionally reports the status and navigation bar insets, so an
 * ordinary application window uses the larger value per edge. */
static uint16_t effective_inset(uint8_t edge) {
    uint32_t safe = app.safe_insets[edge];
    uint32_t bar = app.bar_insets[edge];
    uint32_t value = safe > bar ? safe : bar;
    if (value > 512u) value = 512u;
    return (uint16_t)value;
}

/* The screen's own margins already keep content away from an edge, so the
 * root only adds the part of the safe area they do not cover. Without this the
 * gesture strip and the layout margin stack up and the content drifts. */
static uint16_t inset_padding(uint8_t edge, uint16_t margin) {
    uint16_t inset = effective_inset(edge);
    return inset > margin ? (uint16_t)(inset - margin) : 0;
}

static int request_window_snapshot(void);

/* Applies a window snapshot: the panel safe area and the system bar insets. */
static int parse_window_snapshot(const uint8_t *data, size_t size,
                                 int with_status) {
    pxa_window_insets_view_t view;
    if (!pxa_window_parse_snapshot(data, size, with_status, &view)) return 0;
    if (view.has_safe_insets) {
        uint8_t index;
        for (index = 0; index < 4u; ++index)
            app.safe_insets[index] = view.safe_insets[index];
    }
    if (view.has_bar_insets) {
        uint8_t index;
        for (index = 0; index < 4u; ++index)
            app.bar_insets[index] = view.bar_insets[index];
    }
    app.snapshot_received = 1;
    return 1;
}

/* Some Hosts only have a snapshot once the component is fully active, so the
 * request is best effort and retried a few times. */
static void ensure_window_snapshot(void) {
    if (app.snapshot_received || app.snapshot_attempts >= 3u) return;
    ++app.snapshot_attempts;
    (void)request_window_snapshot();
}

static int request_window_snapshot(void) {
    return pxa_send(PXA_SERVICE_WINDOW, PXA_WINDOW_GET_SNAPSHOT,
                    STORE_REQUEST_WINDOW_SNAPSHOT, NULL, 0);
}

/* ------------------------------------------------------------------ */
/* Application lifecycle                                              */
/* ------------------------------------------------------------------ */

int32_t pxa_app_start(const uint8_t *config, uint32_t config_length) {
    (void)pxa_i18n_init_from_start_config(&app.i18n, &pxa_app_i18n_bundle,
                                          config, config_length);
    store_client_init(&app.client);
    app.screen = STORE_SCREEN_CATALOG;
#if STORE_DEBUG_START_SEARCH
    app.screen = STORE_SCREEN_SEARCH;
#endif
    app.state = STORE_IDLE;
    app.error = STORE_ERROR_NONE;
    app.selected = 0;
    app.busy = 0;
    app.chain = 0;
    app.install_feedback = 0;
    app.generation = 0;
    app.request_cursor = 0;
    app.catalog.head = 0;
    app.catalog.count = 0;
    app.catalog.dropped = 0;
    app.catalog.has_more = 0;
    app.catalog.next_cursor = 0;
    app.auto_load_mark = 0;
    app.detail_ready = 0;
    app.detail_state = STORE_IDLE;
    app.detail_target = 0;
    app.pending_request = 0;
    app.kind_index = 0;
    app.category_entry = -1;
    app.list_scroll = 0;
    app.last_scroll = 0;
    app.chrome_hidden = 0;
    app.detail_scroll = 0;
    app.width = 0;
    app.height = 0;
    app.draft[0] = '\0';
    app.taxonomy.kind_count = 0;
    app.taxonomy.category_count = 0;
    copy_text(app.query, sizeof(app.query), "");
    copy_text(app.device_id, sizeof(app.device_id), "");
    store_trace("start", 1);
    {
        pxa_ui_environment_t environment;
        if (pxa_ui_parse_start_environment(config, config_length, &environment))
            apply_environment(&environment);
    }
    if (!configure_window()) return PXA_STATUS_INTERNAL;
    ensure_window_snapshot();
    if (start_catalog_request()) {
        if (!render()) return PXA_STATUS_INTERNAL;
    }
    return PXA_STATUS_OK;
}

int32_t pxa_app_on_event(const uint8_t *event, uint32_t length) {
    pxa_event_t parsed;
    pxa_ui_event_data_t ui_event;
    if (!pxa_parse_event(event, length, &parsed)) {
        store_trace("unparsed event length", length);
        return PXA_EVENT_UNHANDLED;
    }
    store_trace("event service", parsed.service);
    store_trace("event opcode", parsed.opcode);
    {
        int locale_result = pxa_i18n_handle_event(&app.i18n, &parsed);
        if (locale_result != 0)
            return locale_result == 1 && !render() ? PXA_STATUS_INTERNAL
                                                   : PXA_EVENT_HANDLED;
    }
    if (parsed.service == PXA_SERVICE_WINDOW &&
        parsed.opcode == PXA_WINDOW_GET_SNAPSHOT &&
        parsed.request_id == STORE_REQUEST_WINDOW_SNAPSHOT) {
        if (!parse_window_snapshot(parsed.payload, parsed.payload_length, 1))
            return PXA_EVENT_UNHANDLED;
        return render() ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
    }
    if (parsed.service == PXA_SERVICE_WINDOW &&
        parsed.opcode == 0x8001u) {
        /* metrics-changed carries a complete snapshot without a status. */
        if (!parse_window_snapshot(parsed.payload, parsed.payload_length, 0))
            return PXA_EVENT_UNHANDLED;
        return render() ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
    }
    if (parsed.service == PXA_SERVICE_UI &&
        parsed.opcode == PXA_UI_ENVIRONMENT_CHANGED) {
        pxa_ui_environment_t environment;
        if (!pxa_ui_parse_environment_event(&parsed, &environment))
            return PXA_EVENT_UNHANDLED;
        apply_environment(&environment);
        ensure_window_snapshot();
        return render() ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
    }
    if (parsed.service == PXA_SERVICE_WINDOW &&
        parsed.opcode == PXA_WINDOW_BACK_REQUESTED) {
        if (app.screen == STORE_SCREEN_SEARCH) {
            app.screen = STORE_SCREEN_CATALOG;
            return render() ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
        }
        if (app.screen == STORE_SCREEN_DETAIL) {
            app.screen = STORE_SCREEN_CATALOG;
            app.install_feedback = 0;
            app.detail_ready = 0;
            app.detail_state = STORE_IDLE;
            return render() ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
        }
        return PXA_EVENT_UNHANDLED;
    }
    if (parsed.service == PXA_SERVICE_PERMISSION &&
        parsed.opcode == PXA_PERMISSION_ACQUIRE)
        return handle_permission_result(&parsed);
    if (parsed.service == PXA_SERVICE_DEVICE &&
        parsed.opcode == PXA_DEVICE_GET_MAC &&
        parsed.request_id == STORE_REQUEST_DEVICE_MAC)
        return handle_mac_result(&parsed);
    if (parsed.service == PXA_SERVICE_NET &&
        parsed.opcode == PXA_NET_HTTP_REQUEST &&
        (parsed.request_id == STORE_REQUEST_CATALOG ||
         parsed.request_id == STORE_REQUEST_DETAIL))
        return handle_json_result(&parsed);
    if (parsed.service == PXA_SERVICE_CLOCK && parsed.opcode == PXA_CLOCK_TICK) {
        if (app.client.stream_waiting && app.client.body_handle != 0) {
            app.client.stream_waiting = 0;
            return continue_body() ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
        }
        return PXA_EVENT_UNHANDLED;
    }
    if (!pxa_ui_parse_event(&parsed, &ui_event)) return PXA_EVENT_UNHANDLED;
    if (ui_event.kind == PXA_UI_EVENT_POINTER_KIND) {
        /* The list owns scrolling, and the chrome follows its offset. While
         * the content cannot scroll any further there are no scroll events
         * left, so the drag direction brings the bars back. */
        pxa_ui_pointer_data_t pointer;
        if ((ui_event.node == NODE_LIST || ui_event.node == NODE_FILTERS) &&
            pxa_ui_parse_pointer(&parsed, &pointer)) {
            if (pointer.phase == PXA_POINTER_DOWN ||
                app.pointer_node != ui_event.node) {
                app.pointer_node = ui_event.node;
                app.pointer_y = pointer.y;
                app.drag_active = 0;
            } else if (pointer.phase == PXA_POINTER_MOVE) {
                int32_t dy = pointer.y - app.pointer_y;
                /* The drag direction decides, like a phone: dragging the
                 * content down brings the bars back, dragging it up hides
                 * them. Scroll offsets alone cannot be trusted because every
                 * page load re-bases them. */
                if (dy >= STORE_CHROME_DRAG_DELTA) {
                    app.pointer_y = pointer.y;
                    app.drag_active = 1;
                    (void)set_chrome_visible(1);
                } else if (dy <= -STORE_CHROME_DRAG_DELTA) {
                    app.pointer_y = pointer.y;
                    app.drag_active = 1;
                    (void)set_chrome_visible(0);
                }
            }
        }
        return PXA_EVENT_HANDLED;
    }
    if (ui_event.kind == PXA_UI_EVENT_SCROLL_KIND) {
        if (ui_event.node == NODE_LIST) {
            /* The raw value goes negative while the content is pulled down
             * past its top, which is the phone gesture that brings the chrome
             * back. */
            int32_t raw = ui_event.value;
            int32_t offset = raw > 0 ? raw : 0;
            int32_t last = app.last_scroll;
            app.list_scroll = offset;
            app.drag_active = 1;
            if (offset <= STORE_CHROME_TOP_MARGIN)
                store_trace("scroll top", (uint32_t)offset);
            /* Never hide near the top: the elastic bounce that follows a
             * pull-down reports a positive delta and used to hide the bars
             * right after they came back. */
            /* The drag drives the bars; the offset only guarantees that they
             * are there at the top, where a phone always shows them. */
            (void)last;
            if (offset <= STORE_CHROME_TOP_MARGIN || raw < 0)
                (void)set_chrome_visible(1);
            app.last_scroll = offset;
            /* Incremental loading: no explicit load-more button, the next page
             * is requested once the user scrolls past the previous mark. */
            if (!app.busy && app.catalog.has_more &&
                offset >= (int32_t)app.auto_load_mark +
                              (int32_t)STORE_AUTO_LOAD_STEP) {
                app.auto_load_mark = (uint8_t)(offset > 255 ? 255 : offset);
                app.request_cursor = app.catalog.next_cursor;
                app.chain = 0;
                (void)start_catalog_request();
            }
        } else if (ui_event.node == NODE_DETAIL_SCROLL) {
            app.detail_scroll = ui_event.value > 0 ? ui_event.value : 0;
        }
        return PXA_EVENT_HANDLED;
    }
#if STORE_SEARCH_TEXT_INPUT
    if (ui_event.kind == PXA_UI_EVENT_TEXT_KIND &&
        app.screen == STORE_SCREEN_SEARCH &&
        ui_event.node == NODE_SEARCH_FIELD) {
        char text[PXA_UI_EVENT_TEXT_MAX_BYTES + 1u];
        size_t length;
        if (!pxa_ui_event_text(&ui_event, text, sizeof(text)))
            return PXA_EVENT_HANDLED;
        length = string_length(text);
        if (!text_equal(text, app.draft)) adopt_field_text(text, length);
        /* The field already shows the text the input method wrote; editing
         * never restarts a search in flight, the query is applied when the
         * input is submitted or searched. */
        return PXA_EVENT_HANDLED;
    }
#endif
    if (ui_event.kind != PXA_UI_EVENT_CLICK_KIND) return PXA_EVENT_UNHANDLED;
    /* A drag is not a click. When the list cannot scroll - a short page, or
     * content already at its end - the release still arrives as a click on
     * the card under the finger, which opened a detail instead of scrolling. */
    if (app.drag_active) return PXA_EVENT_HANDLED;
    if (ui_event.node == NODE_SEARCH_BUTTON &&
        app.screen == STORE_SCREEN_CATALOG) {
        if (!open_search()) return PXA_EVENT_HANDLED;
        return PXA_EVENT_HANDLED;
    }
    if (app.screen == STORE_SCREEN_SEARCH) {
        if (ui_event.node == NODE_SEARCH_BACK) {
            app.screen = STORE_SCREEN_CATALOG;
            return render() ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
        }
        if (ui_event.node == NODE_SEARCH_CLEAR) {
            app.draft[0] = '\0';
            return render() ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
        }
        if (ui_event.node == NODE_SEARCH_FIELD) {
            if (app.busy) return PXA_EVENT_HANDLED;
            if (!apply_search()) return PXA_EVENT_HANDLED;
            return render() ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
        }
        if (ui_event.node >= NODE_KEY_BASE &&
            ui_event.node < NODE_KEY_BASE +
                               NODE_KEY_COUNT * NODE_KEY_STRIDE &&
            (ui_event.node - NODE_KEY_BASE) % NODE_KEY_STRIDE == 0) {
            uint8_t key = (uint8_t)((ui_event.node - NODE_KEY_BASE) /
                                    NODE_KEY_STRIDE);
            if (key < 26u) {
                size_t length = string_length(app.draft);
                if (length + 1u < sizeof(app.draft)) {
                    app.draft[length] = (char)('a' + key);
                    app.draft[length + 1u] = '\0';
                }
            } else if (key == 26u) {
                draft_backspace();
            } else {
                if (app.busy) return PXA_EVENT_HANDLED;
                if (!apply_search()) return PXA_EVENT_HANDLED;
                return render() ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
            }
#if STORE_SEARCH_TEXT_INPUT
            return sync_search_field() ? PXA_EVENT_HANDLED
                                       : PXA_STATUS_INTERNAL;
#else
            return render() ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
#endif
        }
        return PXA_EVENT_HANDLED;
    }
    if (ui_event.node == NODE_TAB_APPS || ui_event.node == NODE_TAB_GAMES) {
        uint8_t entry = kind_entry_for(ui_event.node == NODE_TAB_APPS ? "app"
                                                                      : "game");
        uint8_t next = entry != 0 ? entry : app.kind_index;
        if (entry == 0 || app.kind_index == next) return PXA_EVENT_HANDLED;
        if (!select_kind(next)) return PXA_EVENT_HANDLED;
        return render() ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
    }
    if (ui_event.node == NODE_CATEGORY_ALL) {
        if (app.category_entry < 0) return PXA_EVENT_HANDLED;
        if (!select_category(-1)) return PXA_EVENT_HANDLED;
        return render() ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
    }
    if (ui_event.node >= NODE_CATEGORY_BASE &&
        ui_event.node <
            NODE_CATEGORY_BASE + STORE_MAX_TAXONOMY * NODE_CHIP_STRIDE &&
        (ui_event.node - NODE_CATEGORY_BASE) % NODE_CHIP_STRIDE == 0) {
        int16_t entry = (int16_t)((ui_event.node - NODE_CATEGORY_BASE) /
                                  NODE_CHIP_STRIDE);
        if (entry >= (int16_t)app.taxonomy.category_count ||
            app.category_entry == entry)
            return PXA_EVENT_HANDLED;
        if (!select_category(entry)) return PXA_EVENT_HANDLED;
        return render() ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
    }
    if (ui_event.node == NODE_REFRESH && app.screen == STORE_SCREEN_CATALOG) {
        if (!restart_catalog(app.query)) return PXA_EVENT_HANDLED;
        return render() ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
    }
    if (ui_event.node == NODE_BACK) {
        app.screen = STORE_SCREEN_CATALOG;
        app.install_feedback = 0;
        app.detail_ready = 0;
        app.detail_state = STORE_IDLE;
        return render() ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
    }
    if (ui_event.node == NODE_INSTALL_BUTTON) {
        app.install_feedback = 1;
        return render() ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
    }
    if (ui_event.node >= NODE_CARD_BASE &&
        ui_event.node < NODE_CARD_BASE + STORE_MAX_APPS * NODE_CARD_STRIDE &&
        (ui_event.node - NODE_CARD_BASE) % NODE_CARD_STRIDE == 0) {
        uint8_t index =
            (uint8_t)((ui_event.node - NODE_CARD_BASE) / NODE_CARD_STRIDE);
        if (index >= app.catalog.count) return PXA_EVENT_HANDLED;
        app.selected = index;
        app.install_feedback = 0;
        app.screen = STORE_SCREEN_DETAIL;
        show_chrome();
        if (app.busy) return render() ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
        (void)start_detail_fetch();
        return render() ? PXA_EVENT_HANDLED : PXA_STATUS_INTERNAL;
    }
    return PXA_EVENT_UNHANDLED;
}

void pxa_app_stop(uint32_t reason) {
    (void)reason;
    (void)pxa_clock_set_period(0);
    if (app.client.body_handle != 0)
        (void)store_client_close_body(&app.client, app.packet,
                                      sizeof(app.packet));
}
