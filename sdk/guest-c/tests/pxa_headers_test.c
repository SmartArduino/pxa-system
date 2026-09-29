#undef NDEBUG
#include <assert.h>

#include "pxa_audio.h"
#include "pxa_canvas.h"
#include "pxa_device.h"
#include "pxa_fs.h"
#include "pxa_game_render.h"
#include "pxa_ipc.h"
#include "pxa_lease.h"
#include "pxa_log.h"
#include "pxa_net.h"
#include "pxa_permission.h"
#include "pxa_raster.h"
#include "pxa_sensor.h"
#include "pxa_storage.h"
#include "pxa_store_installer.h"
#include "pxa_surface.h"
#include "pxa_system.h"
#include "pxa_ui.h"
#include "pxa_work.h"
#include "pxa.h"

_Static_assert(PXA_HEADER_BYTES == PXA_WIRE_SIZE, "Core header size drift");
_Static_assert(PXA_MAX_CONTROL_BYTES == PXA_WIRE_MAX_CONTROL_MESSAGE,
               "Core control limit drift");
_Static_assert(sizeof(pxa_event_t) > 0, "Core event view is incomplete");

int main(void) {
    const uint8_t record[] = {1, 0, 2, 0, 'e', 'n'};
    pxa_event_t event = {PXA_SERVICE_SYSTEM,
                            PXA_SYSTEM_CONFIGURATION_EVENT, 0,
                            record, sizeof(record)};
    pxa_system_configuration_event_t config;
    assert(pxa_system_parse_configuration_event(&event, &config));
    assert(config.locale_size == 2 && config.locale[0] == 'e');
    assert(config.text_direction == PXA_SYSTEM_TEXT_DIRECTION_LTR);
    assert(!pxa_system_parse_configuration_event(NULL, &config));
    return 0;
}
