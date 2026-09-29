#include "pxa_core.h"
#include "pxa_device.h"
#include "pxa_game_render.h"
#include "pxa_pending.h"
#include "pxa_window.h"
#include "pxa_permission.h"
#include "pxa_storage.h"
#include "pxa_fs.h"
#include "pxa_ipc.h"
#include "pxa_net.h"
#include "pxa_audio.h"
#include "pxa_sensor.h"
#include "pxa_lease.h"
#include "pxa_work.h"
#include "pxa_surface.h"
#include "pxa_clock.h"
#include "pxa_ui_wire.h"
#include "pxa_ui.h"
#include "pxa_raster.h"
#include "pxa_i18n.h"
#include "pxa_canvas.h"

static_assert(PXA_HEADER_BYTES == 20, "v1 envelope size");

int main() {
    uint8_t bytes[PXA_HEADER_BYTES] = {};
    pxa_event_t event{};
    return pxa_parse_event(bytes, sizeof(bytes), &event);
}
