#include "pxa/asset_loader.h"
#include "asset_object_internal.h"
#include <string.h>
static pxa_status_t read_exact(const pxa_asset_input_t *, uint8_t *, size_t);

pxa_status_t pxa_asset_resident_info(const pxa_asset_info_t *source, pxa_asset_info_t *out) {
    if (!source || !out) return PXA_STATUS_INVALID_ARGUMENT;
    *out = *source;
    if (source->kind == PXA_ASSET_IMAGE && (source->encoding == PXA_ASSET_ENCODING_PNG ||
        source->encoding == PXA_ASSET_ENCODING_PNG_RGB565)) {
        const int opaque = source->encoding == PXA_ASSET_ENCODING_PNG_RGB565;
        const uint32_t offset = opaque ? PXA_ASSET_FILE_HEADER_BYTES : 0;
        if (source->format_version != (opaque ? 1 : 0) || source->payload_offset != offset ||
            source->stored_bytes < offset+33 || source->stored_bytes > offset+524288 ||
            !source->width || !source->height ||
            source->width > 1024 || source->height > 1024 ||
            (uint32_t)source->width * source->height > (opaque ? 131072u : 65536u) ||
            source->decoded_bytes != (uint32_t)source->width * source->height * (opaque ? 2u : 4u))
            return PXA_STATUS_UNSUPPORTED;
        out->encoding = opaque ? PXA_ASSET_ENCODING_RGB565 : PXA_ASSET_ENCODING_BGRA8888_PREMULTIPLIED;
        out->format_version = 1;
        out->payload_offset = PXA_ASSET_FILE_HEADER_BYTES;
        out->stored_bytes = out->decoded_bytes + PXA_ASSET_FILE_HEADER_BYTES;
    }
    return pxa_asset_object_required_bytes(out) ? PXA_STATUS_OK : PXA_STATUS_UNSUPPORTED;
}

size_t pxa_asset_resident_required_bytes(const pxa_asset_info_t *info) {
    pxa_asset_info_t resident;
    return pxa_asset_resident_info(info, &resident) ? 0 : pxa_asset_object_required_bytes(&resident);
}

pxa_status_t pxa_asset_load_png(const pxa_asset_info_t *info, const pxa_asset_input_t *input,
    const pxa_memory_allocator_t *pixels, const pxa_memory_allocator_t *temporary,
    const pxa_image_backend_t *backend, pxa_asset_object_t **output) {
    if (!output) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    pxa_asset_info_t resident;
    if (!info || (info->encoding != PXA_ASSET_ENCODING_PNG && info->encoding != PXA_ASSET_ENCODING_PNG_RGB565) ||
        pxa_asset_resident_info(info, &resident) || !input || !input->read)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (!pixels || !temporary || !backend) return PXA_STATUS_UNSUPPORTED;
    const int opaque = info->encoding == PXA_ASSET_ENCODING_PNG_RGB565;
    uint32_t consumed = 0;
    if (opaque) {
        uint8_t header[PXA_ASSET_FILE_HEADER_BYTES];
        pxa_asset_info_t checked;
        pxa_status_t checked_status = read_exact(input, header, sizeof(header));
        if (!checked_status) checked_status = pxa_asset_file_header(header, sizeof(header), info->stored_bytes, &checked);
        if (checked_status) return checked_status;
        if (!pxa_asset_info_equal(info, &checked)) return PXA_STATUS_PROTOCOL_ERROR;
        consumed = sizeof(header);
    }
    pxa_image_decode_options_t options = {
        .max_width=info->width, .max_height=info->height,
        .max_pixels=(uint32_t)info->width * info->height,
        .max_encoded_bytes=info->stored_bytes-consumed, .formats=1,
        .opaque_rgb565=(uint8_t)opaque};
    pxa_image_decoder_t *decoder = NULL;
    pxa_status_t status = pxa_image_decoder_create(&options, pixels, temporary, backend, &decoder);
    uint8_t buffer[1024];
    while (!status) {
        if (input->cancelled && input->cancelled(input->context)) {
            status = PXA_STATUS_CANCELLED; break;
        }
        size_t count = 0, capacity = info->stored_bytes - consumed;
        if (capacity > sizeof(buffer)) capacity = sizeof(buffer);
        /* One final bounded read proves EOF, including exact-size inputs. */
        status = input->read(input->context, buffer, capacity ? capacity : 1, &count);
        if (status) break;
        if (count > capacity) { status = PXA_STATUS_PROTOCOL_ERROR; break; }
        if (!count) {
            if (consumed != info->stored_bytes) status = PXA_STATUS_IO_ERROR;
            break;
        }
        consumed += (uint32_t)count;
        size_t at = 0;
        while (!status && at < count) {
            if (input->cancelled && input->cancelled(input->context)) {
                status = PXA_STATUS_CANCELLED; break;
            }
            size_t n = pxa_image_decoder_input_budget(decoder);
            if (n > count-at) n = count-at;
            status = pxa_image_decoder_write(decoder, (pxa_bytes_t){buffer+at,n});
            at += n;
            if (input->yield) input->yield(input->context);
        }
    }
    pxa_decoded_image_t decoded = {0};
    if (!status) status = pxa_image_decoder_finish(decoder, &decoded);
    if (!status && (!decoded.image || decoded.source_width != info->width ||
        decoded.source_height != info->height || decoded.info.width != info->width ||
        decoded.info.height != info->height ||
        decoded.info.encoding != (opaque ? PXA_ASSET_ENCODING_RGB565 : PXA_ASSET_ENCODING_BGRA8888)))
        status = PXA_STATUS_PROTOCOL_ERROR;
    if (!status && input->cancelled && input->cancelled(input->context)) status = PXA_STATUS_CANCELLED;
    if (!status) {
        /* Private decoder output has no published consumers yet. Match the
         * offline PXR conversion exactly, including transparent edge pixels. */
        if (!opaque) {
            pxa_asset_object_view_t view;
            pxa_asset_object_view(decoded.image, &view);
            uint8_t *data = (uint8_t *)view.data;
            for (size_t at=0; at<view.bytes; at+=4)
                for (unsigned c=0;c<3;++c)
                    data[at+c]=(uint8_t)(((unsigned)data[at+c]*data[at+3]+127)/255);
            decoded.image->encoding = PXA_ASSET_ENCODING_BGRA8888_PREMULTIPLIED;
        }
        *output = decoded.image;
        decoded.image = NULL;
    }
    pxa_asset_object_release(decoded.image);
    pxa_image_decoder_destroy(decoder);
    return status;
}

static pxa_status_t read_exact(const pxa_asset_input_t *input,
                                uint8_t *buffer, size_t bytes) {
    while (bytes != 0) {
        size_t count = 0;
        size_t chunk = bytes < PXA_ASSET_READ_CHUNK_BYTES
                           ? bytes : PXA_ASSET_READ_CHUNK_BYTES;
        pxa_status_t status;
        if (input->cancelled != NULL && input->cancelled(input->context))
            return PXA_STATUS_CANCELLED;
        status = input->read(input->context, buffer, chunk, &count);
        if (status != PXA_STATUS_OK) return status;
        if (count == 0 || count > chunk) return PXA_STATUS_IO_ERROR;
        buffer += count;
        bytes -= count;
        if (input->yield != NULL) input->yield(input->context);
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_asset_load_blob_block(const pxa_asset_blob_block_t *block,
    const pxa_asset_input_t *input, uint8_t *output, size_t capacity) {
    if (!block || block->info.kind != PXA_ASSET_BLOB) return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_asset_load_block(block, input, output, capacity);
}

pxa_status_t pxa_asset_load_block(const pxa_asset_block_t *block,
    const pxa_asset_input_t *input, uint8_t *output, size_t capacity) {
    if (!block || !input || !input->read ||
        !((block->info.kind == PXA_ASSET_BLOB && block->info.encoding == PXA_ASSET_ENCODING_RAW) ||
          (block->info.kind == PXA_ASSET_AUDIO && !block->info.decoded_bytes &&
           (block->info.encoding == PXA_ASSET_ENCODING_OGG_OPUS ||
            block->info.encoding == PXA_ASSET_ENCODING_OGG_VORBIS))) ||
        block->offset > block->info.stored_bytes || block->bytes > capacity ||
        block->bytes > PXA_ASSET_BLOB_BLOCK_BYTES ||
        block->bytes > block->info.stored_bytes - block->offset)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (!block->bytes) {
        if (block->offset != block->info.stored_bytes) return PXA_STATUS_INVALID_ARGUMENT;
        return input->cancelled && input->cancelled(input->context)
            ? PXA_STATUS_CANCELLED : PXA_STATUS_OK;
    }
    if (!output) return PXA_STATUS_INVALID_ARGUMENT;
    pxa_status_t status = read_exact(input, output, block->bytes);
    if (status == PXA_STATUS_OK && input->cancelled && input->cancelled(input->context))
        status = PXA_STATUS_CANCELLED;
    return status;
}

pxa_status_t pxa_asset_load_resident(
    const pxa_asset_info_t *info, const pxa_asset_input_t *input,
    pxa_raster_asset_alloc_fn allocate, pxa_raster_asset_free_fn deallocate,
    void *allocator_context, pxa_raster_asset_t **output) {
    uint8_t header[PXA_ASSET_FILE_HEADER_BYTES];
    pxa_asset_info_t decoded;
    pxa_raster_asset_t *asset = NULL;
    uint8_t *payload;
    size_t extra = 0;
    pxa_status_t status;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    if (pxa_asset_object_required_bytes(info) == 0 || info->file == NULL ||
        info->file->size != info->stored_bytes ||
        input == NULL || input->read == NULL)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (info->kind != PXA_ASSET_AUDIO) {
        status = read_exact(input, header, sizeof(header));
        if (status != PXA_STATUS_OK) return status;
        status = pxa_asset_file_header(header, sizeof(header), info->stored_bytes, &decoded);
        if (status != PXA_STATUS_OK) return status;
        if (!pxa_asset_info_equal(info, &decoded)) return PXA_STATUS_PROTOCOL_ERROR;
    }
    status = pxa_asset_object_create(info, allocate, deallocate, allocator_context,
                                     &asset, &payload);
    if (status != PXA_STATUS_OK) return status;
    status = read_exact(input, payload, info->decoded_bytes);
    if (status == PXA_STATUS_OK) {
        if (input->cancelled != NULL && input->cancelled(input->context))
            status = PXA_STATUS_CANCELLED;
        else {
            status = input->read(input->context, header, 1, &extra);
            if (status == PXA_STATUS_OK && extra != 0) status = PXA_STATUS_PROTOCOL_ERROR;
            if (status == PXA_STATUS_OK && input->cancelled != NULL &&
                input->cancelled(input->context))
                status = PXA_STATUS_CANCELLED;
        }
    }
    if (status != PXA_STATUS_OK) {
        pxa_raster_asset_release(asset);
        return status;
    }
    pxa_raster_asset_finish_loading(asset);
    *output = asset;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_asset_load_raster(const pxa_asset_info_t *info,
    const pxa_asset_input_t *input, pxa_raster_asset_alloc_fn allocate,
    pxa_raster_asset_free_fn deallocate, void *context, pxa_raster_asset_t **out) {
    if (!out) return PXA_STATUS_INVALID_ARGUMENT;
    *out = NULL;
    if (!pxa_raster_asset_required_bytes(info)) return PXA_STATUS_INVALID_ARGUMENT;
    return pxa_asset_load_resident(info,input,allocate,deallocate,context,out);
}
