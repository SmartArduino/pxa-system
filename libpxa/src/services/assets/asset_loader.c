#include "pxa/asset_loader.h"

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
