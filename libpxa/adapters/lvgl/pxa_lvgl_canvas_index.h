/* SPDX-License-Identifier: MIT */
/* Private to pxa_lvgl_ui.c. Immutable, frame-local Canvas metadata. */
#ifndef PXA_LVGL_CANVAS_INDEX_H
#define PXA_LVGL_CANVAS_INDEX_H

/* The reference build exercises the original parser with identical fixtures. */
#ifndef PXA_LVGL_CANVAS_INDEX
#define PXA_LVGL_CANVAS_INDEX 1
#endif
#define PXA_CANVAS_INDEX_BANDS 32u
#define PXA_CANVAS_INDEX_MIN_COMMANDS 16u
/* Optional metadata shared by all Canvases of one adapter. */
#define PXA_CANVAS_INDEX_BUDGET_BYTES (64u * 1024u)
#define PXA_CANVAS_INDEX_PROBE_DELAY 3u

typedef struct {
    uint32_t offset, resource;
    uint16_t length;
    uint8_t type, flags;
    lv_area_t clip, bounds;
} canvas_index_record_t;

struct pxa_lvgl_canvas_index {
    size_t bytes, capacity, count, words;
    uint32_t density;
    int32_t band_height;
    canvas_index_record_t records[];
};

static uint32_t *canvas_index_bands(pxa_lvgl_canvas_index_t *index) {
    return (uint32_t *)(index->records + index->capacity);
}

static size_t canvas_index_count(const uint8_t *data, size_t size) {
    size_t count = 0, offset = 0;
    if (!PXA_LVGL_CANVAS_INDEX || size > UINT32_MAX) return 0;
    while (size - offset >= 4) {
        uint8_t type = data[offset];
        uint16_t length = pxa_read_u16(data + offset + 2);
        offset += 4;
        if (length > size - offset) return 0;
        if (type != PXA_UI_CANVAS_CLIP_PUSH && type != PXA_UI_CANVAS_CLIP_POP) ++count;
        offset += length;
    }
    return offset == size && count >= PXA_CANVAS_INDEX_MIN_COMMANDS ? count : 0;
}

/* Paint-only updates keep offsets, bounds, clip scopes and resource ordinals.
 * Handle identity is deliberately ignored here: Core revalidates every IMAGE
 * binding before commit, independently of this geometry-only cache. */
static int canvas_index_same_geometry(const uint8_t *a, const uint8_t *b, size_t size) {
    size_t at = 0;
    while (size - at >= 4) {
        if (memcmp(a + at, b + at, 4)) return 0;
        uint8_t type = a[at];
        uint16_t length = pxa_read_u16(a + at + 2);
        at += 4;
        if (length > size - at) return 0;
        size_t geometry = type == PXA_UI_CANVAS_ARC ? 12 :
            type == PXA_UI_CANVAS_RECT || type == PXA_UI_CANVAS_ELLIPSE ||
            type == PXA_UI_CANVAS_LINE || type == PXA_UI_CANVAS_IMAGE ||
            type == PXA_UI_CANVAS_IMAGE_HANDLE || type == PXA_UI_CANVAS_BITMAP_RGB565 ||
            type == PXA_UI_CANVAS_CLIP_PUSH ? 16 : 0;
        if (geometry > length || memcmp(a + at, b + at, geometry)) return 0;
        if ((type == PXA_UI_CANVAS_LINE || type == PXA_UI_CANVAS_ARC) &&
            (length < 22 || memcmp(a + at + 20, b + at + 20, 2))) return 0;
        at += length;
    }
    return at == size;
}

static pxa_lvgl_canvas_index_t *canvas_index_allocate(pxa_lvgl_ui_t *ui, size_t count) {
    if (count > (PXA_CANVAS_INDEX_BUDGET_BYTES - sizeof(pxa_lvgl_canvas_index_t)) /
                    sizeof(canvas_index_record_t)) return NULL;
    size_t words = (count + 31) / 32;
    size_t extra = words * PXA_CANVAS_INDEX_BANDS * sizeof(uint32_t);
    size_t bytes = sizeof(pxa_lvgl_canvas_index_t) + count * sizeof(canvas_index_record_t) + extra;
    pxa_lvgl_ui_canvas_index_stats_t *stats = &ui->canvas_index_stats;
    if (bytes > PXA_CANVAS_INDEX_BUDGET_BYTES - stats->current_bytes) return NULL;
    pxa_lvgl_canvas_index_t *index = ui_allocate(ui, bytes);
    if (index) {
        index->bytes = bytes;
        index->capacity = count;
        index->words = words;
        stats->current_bytes += bytes;
        if (stats->current_bytes > stats->peak_bytes)
            stats->peak_bytes = stats->current_bytes;
    }
    return index;
}

static void canvas_index_release(pxa_lvgl_ui_t *ui, pxa_lvgl_canvas_index_t *index) {
    if (!index) return;
    ui->canvas_index_stats.current_bytes -= index->bytes;
    ui_release(ui, index);
}

static int32_t canvas_index_coord(int64_t value) {
    return value < INT32_MIN ? INT32_MIN : value > INT32_MAX ? INT32_MAX : (int32_t)value;
}

static bool canvas_intersect(lv_area_t *a, const lv_area_t *b) {
    a->x1 = LV_MAX(a->x1, b->x1); a->y1 = LV_MAX(a->y1, b->y1);
    a->x2 = LV_MIN(a->x2, b->x2); a->y2 = LV_MIN(a->y2, b->y2);
    return a->x1 <= a->x2 && a->y1 <= a->y2;
}

static lv_area_t canvas_index_box(pxa_lvgl_ui_t *ui, const uint8_t *value) {
    lv_area_t area;
    area.x1 = canvas_pixels(ui, (int32_t)pxa_read_u32(value));
    area.y1 = canvas_pixels(ui, (int32_t)pxa_read_u32(value + 4));
    area.x2 = canvas_index_coord((int64_t)area.x1 + canvas_pixels(ui, pxa_read_u32(value + 8)) - 1);
    area.y2 = canvas_index_coord((int64_t)area.y1 + canvas_pixels(ui, pxa_read_u32(value + 12)) - 1);
    return area;
}

/* Use dirty rectangles as an admission hint. Actual tile size is checked again
 * when drawing, since LVGL can merge invalidations or repaint for other nodes. */
static bool canvas_index_local_update(pxa_lvgl_ui_t *ui, const pxa_ui_canvas_view_t *view,
                                       lv_obj_t *object, lv_area_t *bounds) {
    if (!PXA_LVGL_CANVAS_INDEX || !view->dirty_count ||
        view->display_list.size < PXA_CANVAS_INDEX_MIN_COMMANDS * 4u)
        return false;
    lv_obj_get_coords(object, bounds);
    lv_area_t viewport = {0, 0, (int32_t)ui->primary_environment.width - 1,
                          (int32_t)ui->primary_environment.height - 1};
    lv_area_t visible = *bounds;
    if (!canvas_intersect(&visible, &viewport)) return false;
    uint64_t pixels = 0;
    for (unsigned i = 0; i < view->dirty_count; ++i) {
        const int32_t *dirty = view->dirty_rects[i];
        lv_area_t rect = {
            canvas_index_coord((int64_t)bounds->x1 + canvas_pixels(ui, dirty[0])),
            canvas_index_coord((int64_t)bounds->y1 + canvas_pixels(ui, dirty[1])),
            canvas_index_coord((int64_t)bounds->x1 +
                canvas_end_pixels(ui, (int64_t)dirty[0] + dirty[2]) - 1),
            canvas_index_coord((int64_t)bounds->y1 +
                canvas_end_pixels(ui, (int64_t)dirty[1] + dirty[3]) - 1)
        };
        if (canvas_intersect(&rect, &visible))
            pixels += (uint64_t)lv_area_get_width(&rect) * lv_area_get_height(&rect);
    }
    uint64_t visible_pixels = (uint64_t)lv_area_get_width(&visible) * lv_area_get_height(&visible);
    return pixels != 0 && pixels <= visible_pixels / 4;
}

static unsigned canvas_index_band(const pxa_lvgl_canvas_index_t *index, int64_t y) {
    int64_t band = y / index->band_height;
    return band < 0 ? 0 : band >= PXA_CANVAS_INDEX_BANDS ? PXA_CANVAS_INDEX_BANDS - 1 : (unsigned)band;
}

/* Build under the UI executor, into storage not used by the committed frame.
 * Bounds are conservative. Text retains its complete clip: glyph overhang and
 * theme/font changes must never make cached text bounds omit visible ink. */
static int canvas_index_build(pxa_lvgl_ui_t *ui, pxa_lvgl_canvas_index_t *index,
    const uint8_t *data, size_t size, lv_area_t *stack, size_t stack_capacity,
    int32_t height) {
    size_t offset = 0, depth = 0, bitmap = 0, image = 0;
    lv_area_t clip = {-LV_COORD_MAX, -LV_COORD_MAX, LV_COORD_MAX, LV_COORD_MAX};
    uint32_t *bands = canvas_index_bands(index);
    memset(bands, 0, index->words * PXA_CANVAS_INDEX_BANDS * sizeof(*bands));
    index->count = 0;
    index->density = ui->primary_environment.density_q16;
    index->band_height = (int32_t)LV_MAX(INT64_C(1), ((int64_t)height + 31) / 32);
    while (size - offset >= 4) {
        uint8_t type = data[offset], flags = data[offset + 1];
        uint16_t length = pxa_read_u16(data + offset + 2);
        offset += 4;
        if (length > size - offset) return 0;
        const uint8_t *value = data + offset;
        if (type == PXA_UI_CANVAS_CLIP_PUSH) {
            if (length != 16 || depth == stack_capacity) return 0;
            stack[depth++] = clip;
            lv_area_t box = canvas_index_box(ui, value);
            canvas_intersect(&clip, &box);
        } else if (type == PXA_UI_CANVAS_CLIP_POP) {
            if (length || !depth) return 0;
            clip = stack[--depth];
        } else {
            if (index->count == index->capacity) return 0;
            size_t at = index->count++;
            canvas_index_record_t *r = &index->records[at];
            r->offset = (uint32_t)offset; r->length = length;
            r->type = type; r->flags = flags; r->clip = clip; r->bounds = clip;
            r->resource = type == PXA_UI_CANVAS_BITMAP_RGB565 ? (uint32_t)bitmap++ :
                          type == PXA_UI_CANVAS_IMAGE_HANDLE ? (uint32_t)image++ : 0;
            if (((type == PXA_UI_CANVAS_RECT || type == PXA_UI_CANVAS_ELLIPSE) && length >= 26) ||
                ((type == PXA_UI_CANVAS_IMAGE || type == PXA_UI_CANVAS_IMAGE_HANDLE) && length >= 18) ||
                (type == PXA_UI_CANVAS_BITMAP_RGB565 && length >= 20)) {
                r->bounds = canvas_index_box(ui, value);
            } else if (type == PXA_UI_CANVAS_LINE && length == 22) {
                int32_t x1 = canvas_pixels(ui, (int32_t)pxa_read_u32(value));
                int32_t y1 = canvas_pixels(ui, (int32_t)pxa_read_u32(value + 4));
                int32_t x2 = canvas_pixels(ui, (int32_t)pxa_read_u32(value + 8));
                int32_t y2 = canvas_pixels(ui, (int32_t)pxa_read_u32(value + 12));
                int64_t margin = (int64_t)canvas_pixels(ui, pxa_read_u16(value + 20)) + 2;
                r->bounds = (lv_area_t){canvas_index_coord((int64_t)LV_MIN(x1,x2)-margin),
                    canvas_index_coord((int64_t)LV_MIN(y1,y2)-margin),
                    canvas_index_coord((int64_t)LV_MAX(x1,x2)+margin),
                    canvas_index_coord((int64_t)LV_MAX(y1,y2)+margin)};
            } else if (type == PXA_UI_CANVAS_ARC && length == 22) {
                int32_t x = canvas_pixels(ui, (int32_t)pxa_read_u32(value));
                int32_t y = canvas_pixels(ui, (int32_t)pxa_read_u32(value + 4));
                int64_t extent = (int64_t)canvas_pixels(ui, pxa_read_u32(value + 8)) +
                    canvas_pixels(ui, pxa_read_u16(value + 20)) + 2;
                r->bounds = (lv_area_t){canvas_index_coord(x-extent), canvas_index_coord(y-extent),
                                       canvas_index_coord(x+extent), canvas_index_coord(y+extent)};
            }
            if (canvas_intersect(&r->bounds, &clip))
                for (unsigned b = canvas_index_band(index, r->bounds.y1);
                     b <= canvas_index_band(index, r->bounds.y2); ++b)
                    bands[b * index->words + at / 32] |= UINT32_C(1) << (at % 32);
        }
        offset += length;
    }
    return offset == size && !depth;
}

typedef struct {
    pxa_lvgl_canvas_index_t *index;
    size_t next_word, word;
    uint32_t bits;
    unsigned first, last;
} canvas_index_iterator_t;

static const canvas_index_record_t *canvas_index_next(canvas_index_iterator_t *it) {
    uint32_t *bands = canvas_index_bands(it->index);
    while (!it->bits) {
        if (it->next_word == it->index->words) return NULL;
        it->word = it->next_word++;
        for (unsigned b = it->first; b <= it->last; ++b)
            it->bits |= bands[b * it->index->words + it->word];
    }
    unsigned bit = 0;
#if defined(__GNUC__) || defined(__clang__)
    bit = (unsigned)__builtin_ctz(it->bits);
#else
    uint32_t bits = it->bits;
    while (!(bits & 1u)) { ++bit; bits >>= 1; }
#endif
    it->bits &= it->bits - 1;
    return &it->index->records[it->word * 32 + bit];
}
#endif
