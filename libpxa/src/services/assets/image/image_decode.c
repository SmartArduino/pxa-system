#include "pxa/image_decode.h"
#include <string.h>

struct pxa_image_target {
    const pxa_memory_allocator_t *pixels;
    pxa_image_decode_options_t options;
    pxa_asset_info_t info;
    pxa_asset_object_t *image;
    uint8_t *output;
    uint32_t source_width, source_height;
};
struct pxa_image_decoder {
    pxa_image_target_t target;
    const pxa_image_backend_t *backend;
    const pxa_image_codec_t *codec;
    void *session;
    const pxa_memory_allocator_t *temporary;
    size_t encoded, signature_size;
    pxa_status_t status;
    uint8_t signature[8], finished;
};
static void release_memory(void *context, void *memory) {
    (void)context;
    pxa_memory_release(memory);
}
static void *pixel_allocate(void *context, size_t bytes) {
    return pxa_memory_allocate(context, bytes);
}
pxa_status_t pxa_image_target_begin_frame(pxa_image_target_t *t, const pxa_asset_object_t *base) {
    if (!t || t->image || !t->info.decoded_bytes)
        return PXA_STATUS_BAD_STATE;
    pxa_asset_object_view_t view = {0};
    if (base) {
        pxa_asset_object_view(base, &view);
        if (view.kind != t->info.kind || view.encoding != t->info.encoding ||
            view.width != t->info.width || view.height != t->info.height ||
            view.bytes != t->info.decoded_bytes)
            return PXA_STATUS_INVALID_ARGUMENT;
    }
    pxa_status_t status = pxa_asset_object_create(&t->info, pixel_allocate, release_memory,
                                                 (void *)t->pixels, &t->image, &t->output);
    if (!status) {
        if (base) memcpy(t->output, view.data, view.bytes);
        else memset(t->output, 0, t->info.decoded_bytes);
    }
    return status;
}
pxa_asset_object_t *pxa_image_target_retain_frame(pxa_image_target_t *t) {
    if (!t) return NULL;
    pxa_asset_object_retain(t->image);
    return t->image;
}
pxa_status_t pxa_image_target_copy_frame(pxa_image_target_t *t, pxa_asset_object_t **out) {
    if (!out) return PXA_STATUS_INVALID_ARGUMENT;
    *out = NULL;
    if (!t || !t->image || !t->output) return PXA_STATUS_BAD_STATE;
    uint8_t *pixels = NULL;
    pxa_status_t status = pxa_asset_object_create(&t->info, pixel_allocate, release_memory,
                                                 (void *)t->pixels, out, &pixels);
    if (!status) memcpy(pixels, t->output, t->info.decoded_bytes);
    return status;
}
const pxa_image_decode_options_t *pxa_image_target_options(const pxa_image_target_t *t) {
    return t ? &t->options : NULL;
}
void pxa_image_target_dimensions(const pxa_image_target_t *t, uint16_t *w, uint16_t *h) {
    if (w)
        *w = t ? t->info.width : 0;
    if (h)
        *h = t ? t->info.height : 0;
}
pxa_status_t pxa_image_target_begin(pxa_image_target_t *d, uint32_t width, uint32_t height,
                                    int alpha) {
    if (!d || d->image)
        return PXA_STATUS_BAD_STATE;
    if (!width || !height || width > 4096 || height > 4096 || width > 1048576u / height)
        return PXA_STATUS_LIMIT_EXCEEDED;
    d->source_width = width;
    d->source_height = height;
    uint32_t w = width, h = height;
    if (w > d->options.max_width) {
        h = (uint32_t)((uint64_t)h * d->options.max_width / w);
        w = d->options.max_width;
    }
    if (!h)
        h = 1;
    if (h > d->options.max_height) {
        w = (uint32_t)((uint64_t)w * d->options.max_height / h);
        h = d->options.max_height;
    }
    if (!w)
        w = 1;
    /* Integer rescaling preserves aspect; this loop is at most 1024 steps. */
    while (w * h > d->options.max_pixels) {
        if (w >= h && w > 1) {
            --w;
            h = (uint32_t)((uint64_t)height * w / width);
            if (!h)
                h = 1;
        } else {
            --h;
            w = (uint32_t)((uint64_t)width * h / height);
            if (!w)
                w = 1;
        }
    }
    const uint32_t bytes = w * h * (alpha ? 4u : 2u);
    d->info = (pxa_asset_info_t){0};
    d->info.kind = PXA_ASSET_IMAGE;
    d->info.encoding = alpha ? PXA_ASSET_ENCODING_BGRA8888 : PXA_ASSET_ENCODING_RGB565;
    d->info.format_version = 1;
    d->info.width = (uint16_t)w;
    d->info.height = (uint16_t)h;
    d->info.payload_offset = PXA_ASSET_FILE_HEADER_BYTES;
    d->info.stored_bytes = bytes + PXA_ASSET_FILE_HEADER_BYTES;
    d->info.decoded_bytes = bytes;
    pxa_status_t status = pxa_asset_object_create(&d->info, pixel_allocate, release_memory,
                                                  (void *)d->pixels, &d->image, &d->output);
    if (!status)
        memset(d->output, 0, bytes);
    return status;
}
pxa_status_t pxa_image_target_blit(pxa_image_target_t *t, const pxa_image_tile_t *tile) {
    if (!t || !t->output || !tile || !tile->pixels || !tile->width || !tile->height ||
        !tile->pixel_width || !tile->pixel_height || tile->pixel_width > 4096 ||
        tile->pixel_height > 4096 || tile->format > PXA_IMAGE_RGB565 ||
        tile->x >= t->source_width || tile->y >= t->source_height)
        return PXA_STATUS_INVALID_ARGUMENT;
    const uint32_t bpp = tile->format == PXA_IMAGE_RGB565   ? 2u
                         : tile->format >= PXA_IMAGE_RGB888 ? 3u
                                                            : 4u;
    if (tile->pixel_width > UINT32_MAX / bpp || tile->stride < tile->pixel_width * bpp ||
        tile->pixel_height > SIZE_MAX / tile->stride)
        return PXA_STATUS_INVALID_ARGUMENT;
    uint32_t w = tile->width, h = tile->height;
    if (w > t->source_width - tile->x)
        w = t->source_width - tile->x;
    if (h > t->source_height - tile->y)
        h = t->source_height - tile->y;
    uint32_t dw = t->info.width, dh = t->info.height, sw = t->source_width, sh = t->source_height;
    uint32_t left = (tile->x * dw + sw - 1) / sw, right = ((tile->x + w) * dw + sw - 1) / sw;
    uint32_t top = (tile->y * dh + sh - 1) / sh, bottom = ((tile->y + h) * dh + sh - 1) / sh;
    for (uint32_t y = top; y < bottom; ++y) {
        uint32_t sy = tile->pixel_height == 1
                          ? 0
                          : (y * sh / dh - tile->y) * tile->pixel_height / tile->height;
        for (uint32_t x = left; x < right; ++x) {
            uint32_t sx = tile->pixel_width == 1
                              ? 0
                              : (x * sw / dw - tile->x) * tile->pixel_width / tile->width;
            const uint8_t *p = tile->pixels + (size_t)sy * tile->stride + sx * bpp;
            uint8_t r, g, b, a = 255;
            if (tile->format == PXA_IMAGE_RGB565) {
                /* Preserve native RGB565 samples without expanding and
                 * requantizing three channels. Other formats keep their
                 * original path, including single-pixel streaming PNG. */
                if (t->info.encoding == PXA_ASSET_ENCODING_RGB565) {
                    memcpy(t->output + 2 * (y * dw + x), p, 2);
                    continue;
                }
                uint16_t rgb;
                memcpy(&rgb, p, 2);
                r = (uint8_t)((rgb >> 11) * 255 / 31);
                g = (uint8_t)(((rgb >> 5) & 63) * 255 / 63);
                b = (uint8_t)((rgb & 31) * 255 / 31);
            } else {
                int reverse =
                    tile->format == PXA_IMAGE_BGRA8888 || tile->format == PXA_IMAGE_BGR888;
                r = p[reverse ? 2 : 0];
                g = p[1];
                b = p[reverse ? 0 : 2];
                if (bpp == 4)
                    a = p[3];
            }
            if (t->info.encoding == PXA_ASSET_ENCODING_RGB565)
                ((uint16_t *)t->output)[y * dw + x] =
                    (uint16_t)(((r & 248) << 8) | ((g & 252) << 3) | (b >> 3));
            else {
                uint8_t *q = t->output + 4 * (y * dw + x);
                q[0] = b;
                q[1] = g;
                q[2] = r;
                q[3] = a;
            }
        }
    }
    return PXA_STATUS_OK;
}
pxa_status_t pxa_image_decoder_create(const pxa_image_decode_options_t *options,
                                      const pxa_memory_allocator_t *pixels,
                                      const pxa_memory_allocator_t *temporary,
                                      const pxa_image_backend_t *backend,
                                      pxa_image_decoder_t **out) {
    if (!out)
        return PXA_STATUS_INVALID_ARGUMENT;
    *out = NULL;
    if (!options || !pixels || !temporary || !backend || !backend->codecs || !backend->count ||
        !options->max_width || !options->max_height || options->max_width > 1024 ||
        options->max_height > 1024 || !options->max_pixels || options->max_pixels > 65536 ||
        !options->max_encoded_bytes || options->max_encoded_bytes > 524288)
        return PXA_STATUS_INVALID_ARGUMENT;
    if ((options->formats & ~7u) || options->max_animation_pixels > 65536 ||
        options->max_animation_encoded_bytes > 524288)
        return PXA_STATUS_INVALID_ARGUMENT;
    for (size_t i = 0; i < backend->count; ++i) {
        const pxa_image_codec_t *c = backend->codecs[i];
        if (!c || !c->open || !c->write || !c->finish || !c->close)
            return PXA_STATUS_INVALID_ARGUMENT;
    }
    pxa_image_decoder_t *d = pxa_memory_allocate(temporary, sizeof(*d));
    if (!d)
        return PXA_STATUS_RESOURCE_LIMIT;
    memset(d, 0, sizeof(*d));
    d->target.pixels = pixels;
    d->target.options = *options;
    d->temporary = temporary;
    d->backend = backend;
    *out = d;
    return PXA_STATUS_OK;
}
pxa_status_t pxa_image_decoder_write(pxa_image_decoder_t *d, pxa_bytes_t input) {
    if (!d || !input.data || !input.size || input.size > 4096)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (d->finished)
        return PXA_STATUS_BAD_STATE;
    if (d->status)
        return d->status;
    if (input.size > d->target.options.max_encoded_bytes - d->encoded)
        return d->status = PXA_STATUS_LIMIT_EXCEEDED;
    d->encoded += input.size;
    if (!d->codec) {
        size_t n = sizeof(d->signature) - d->signature_size;
        if (n > input.size)
            n = input.size;
        memcpy(d->signature + d->signature_size, input.data, n);
        d->signature_size += n;
        input.data += n;
        input.size -= n;
        if (d->signature_size < sizeof(d->signature))
            return PXA_STATUS_OK;
        if (d->target.options.formats) {
            static const uint8_t png[8] = {137, 80, 78, 71, 13, 10, 26, 10};
            unsigned format = !memcmp(d->signature, png, 8) ? 1u :
                d->signature[0] == 255 && d->signature[1] == 216 && d->signature[2] == 255 ? 2u :
                (!memcmp(d->signature, "GIF87a", 6) || !memcmp(d->signature, "GIF89a", 6)) ? 4u : 0u;
            if (!(format & d->target.options.formats)) return d->status = PXA_STATUS_UNSUPPORTED;
        }
        d->status = PXA_STATUS_UNSUPPORTED;
        for (size_t i = 0; i < d->backend->count; ++i) {
            const pxa_image_codec_t *c = d->backend->codecs[i];
            void *session = NULL;
            pxa_image_decode_options_t ordinary = d->target.options;
            if (c->next) {
                uint32_t pixels = ordinary.max_animation_pixels, bytes = ordinary.max_animation_encoded_bytes;
                if (pixels && pixels < d->target.options.max_pixels) d->target.options.max_pixels = pixels;
                if (bytes && bytes < d->target.options.max_encoded_bytes) d->target.options.max_encoded_bytes = bytes;
            }
            d->status = c->open((pxa_bytes_t){d->signature, sizeof(d->signature)}, &d->target,
                                d->temporary, &session);
            /* A platform codec may fail after partially allocating state.
             * Ownership crosses here even on error, before trying another. */
            if (d->status && session)
                c->close(session);
            if (d->status == PXA_STATUS_UNSUPPORTED) {
                d->target.options = ordinary;
                continue;
            }
            if (d->status)
                return d->status;
            if (!session)
                return d->status = PXA_STATUS_INTERNAL;
            d->session = session;
            d->codec = c;
            if (d->encoded > d->target.options.max_encoded_bytes)
                return d->status = PXA_STATUS_LIMIT_EXCEEDED;
            break;
        }
        if (d->status)
            return d->status;
        d->status = d->codec->write(d->session, (pxa_bytes_t){d->signature, sizeof(d->signature)});
    }
    if (!d->status && input.size)
        d->status = d->codec->write(d->session, input);
    return d->status;
}
pxa_status_t pxa_image_decoder_next(pxa_image_decoder_t *d, pxa_decoded_image_t *out,
                                    pxa_image_frame_info_t *frame) {
    if (!d || !out || !frame)
        return PXA_STATUS_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    memset(frame, 0, sizeof(*frame));
    if (d->finished == 2) {
        frame->last = 1;
        return d->status;
    }
    if (!d->finished && !d->status)
        d->status = d->codec ? d->codec->finish(d->session) : PXA_STATUS_PROTOCOL_ERROR;
    d->finished = 1;
    if (!d->status && d->codec->next) {
        d->status = d->codec->next(d->session, frame);
        if (d->status == PXA_STATUS_NOT_FOUND) {
            d->status = PXA_STATUS_OK;
            d->finished = 2;
            frame->last = 1;
            return PXA_STATUS_OK;
        }
    } else if (!d->status) {
        frame->last = 1;
        d->finished = 2;
    }
    if (!d->status && !d->target.image)
        d->status = PXA_STATUS_INTERNAL;
    if (d->status)
        return d->status;
    out->image = d->target.image;
    out->info = d->target.info;
    out->source_width = (uint16_t)d->target.source_width;
    out->source_height = (uint16_t)d->target.source_height;
    d->target.image = NULL;
    d->target.output = NULL;
    return PXA_STATUS_OK;
}
pxa_status_t pxa_image_decoder_finish(pxa_image_decoder_t *d, pxa_decoded_image_t *out) {
    if (!d || !out) return PXA_STATUS_INVALID_ARGUMENT;
    if (d->finished) {
        memset(out, 0, sizeof(*out));
        return PXA_STATUS_BAD_STATE;
    }
    pxa_image_frame_info_t frame;
    pxa_status_t status = pxa_image_decoder_next(d, out, &frame);
    return !status && !out->image ? PXA_STATUS_PROTOCOL_ERROR : status;
}
void pxa_image_decoder_destroy(pxa_image_decoder_t *d) {
    if (!d)
        return;
    if (d->codec)
        d->codec->close(d->session);
    pxa_asset_object_release(d->target.image);
    pxa_memory_release(d);
}
