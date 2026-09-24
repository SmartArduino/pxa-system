#include "pxa_app_messages.h"
#include "pxa_i18n.h"
#include "pxa_ui.h"

static pxa_i18n_t i18n;
static uint8_t packet[1024];
static uint32_t generation;

static int render(void) {
    pxa_ui_transaction_t transaction = {0};
    uint32_t next = generation + 1u;
    const char *title = pxa_i18n_cstr(&i18n, PXA_MSG_TITLE);
    const char *body = pxa_i18n_cstr(&i18n, PXA_MSG_BODY);
    if (next == 0 || !pxa_ui_transaction_begin(&transaction, next,
            PXA_UI_TRANSACTION_REPLACE_SURFACE, packet, sizeof(packet))) return 0;
    int ok = pxa_ui_create(&transaction, 1, 0, 0, PXA_UI_NODE_ROOT) &&
             pxa_ui_set_u8(&transaction, 1, PXA_UI_PROPERTY_LAYOUT,
                           PXA_UI_LAYOUT_COLUMN) &&
             pxa_ui_set_padding(&transaction, 1, 20, 20, 20, 20) &&
             pxa_ui_set_theme_color(&transaction, 1, PXA_UI_PROPERTY_BACKGROUND,
                                    PXA_UI_THEME_BACKGROUND) &&
             pxa_ui_create(&transaction, 2, 1, 0, PXA_UI_NODE_TEXT) &&
             pxa_ui_set_text(&transaction, 2, title, pxa_i18n_size(&i18n, PXA_MSG_TITLE)) &&
             pxa_ui_set_font_role(&transaction, 2, PXA_UI_FONT_ROLE_TITLE) &&
             pxa_ui_set_theme_color(&transaction, 2, PXA_UI_PROPERTY_FOREGROUND,
                                    PXA_UI_THEME_TEXT) &&
             pxa_ui_create(&transaction, 3, 1, 0, PXA_UI_NODE_TEXT) &&
             pxa_ui_set_text(&transaction, 3, body, pxa_i18n_size(&i18n, PXA_MSG_BODY)) &&
             pxa_ui_set_font_role(&transaction, 3, PXA_UI_FONT_ROLE_BODY) &&
             pxa_ui_set_theme_color(&transaction, 3, PXA_UI_PROPERTY_FOREGROUND,
                                    PXA_UI_THEME_TEXT);
    if (!ok || !pxa_ui_transaction_commit(&transaction)) {
        if (transaction.active) (void)pxa_ui_transaction_cancel(&transaction);
        return 0;
    }
    generation = next;
    return 1;
}

int32_t pxa_app_start(const uint8_t *config, uint32_t config_length) {
    generation = 0;
    (void)pxa_i18n_init_from_start_config(&i18n, &pxa_app_i18n_bundle,
                                           config, config_length);
    return render() ? PXA_STATUS_OK : PXA_STATUS_INTERNAL;
}

int32_t pxa_app_on_event(const uint8_t *event, uint32_t length) {
    pxa_event_t parsed;
    if (!pxa_parse_event(event, length, &parsed)) return PXA_EVENT_UNHANDLED;
    int locale_changed = pxa_i18n_handle_event(&i18n, &parsed);
    if (locale_changed != 0)
        return locale_changed == 1 && !render() ? PXA_STATUS_INTERNAL :
                                                PXA_EVENT_HANDLED;
    return PXA_EVENT_UNHANDLED;
}

void pxa_app_stop(uint32_t reason) {
    (void)reason;
}
