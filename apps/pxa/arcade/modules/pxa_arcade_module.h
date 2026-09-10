#ifndef PXA_ARCADE_MODULE_H
#define PXA_ARCADE_MODULE_H

#include <stddef.h>
#include <stdint.h>

#include "pxa_app_messages.h"
#include "pxa_system.h"

#ifndef PXA_ARCADE_MODULE_PREFIX
#error "PXA_ARCADE_MODULE_PREFIX must be defined before including this header"
#endif

#define PXA_ARCADE_JOIN_INNER(prefix, name) prefix##name
#define PXA_ARCADE_JOIN(prefix, name) PXA_ARCADE_JOIN_INNER(prefix, name)
#define PXA_ARCADE_EXPORT(name) PXA_ARCADE_JOIN(PXA_ARCADE_MODULE_PREFIX, name)

#ifndef PXA_ARCADE_STANDALONE_TEST
#define pxa_app_start PXA_ARCADE_EXPORT(start)
#define pxa_app_on_event PXA_ARCADE_EXPORT(on_event)
#define pxa_app_stop PXA_ARCADE_EXPORT(stop)
extern uint32_t pxa_arcade_ui_generation;
const char *pxa_arcade_message(pxa_i18n_message_id_t id);
size_t pxa_arcade_text_size(const char *text);
#else
static uint32_t pxa_arcade_ui_generation;
static inline const char *pxa_arcade_message(pxa_i18n_message_id_t id) {
    (void)id;
    return "";
}
static inline size_t pxa_arcade_text_size(const char *text) {
    size_t size = 0;
    while (text != NULL && text[size] != '\0') ++size;
    return size;
}
#endif

#define PXA_ARCADE_MSG(id) pxa_arcade_message((id))

#endif
