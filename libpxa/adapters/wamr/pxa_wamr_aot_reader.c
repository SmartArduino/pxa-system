#include "pxa_wamr_aot_reader.h"

#include <stdio.h>
#include <string.h>

/* WAMR's platform mapping contract, including the Xtensa I/D-bus aliases. */
#include "platform_api_vmcore.h"
#if (defined(ESP_PLATFORM) && defined(WASM_LINEAR_MEMORY_RESERVE_MAX) \
     && WASM_LINEAR_MEMORY_RESERVE_MAX != 0) || defined(PXA_WAMR_TEST_LOAD_MEMORY)
#include "pxa_wamr_load_memory.h"
#define LOAD_MEMORY_RESERVATION 1
#endif
#if WASM_ENABLE_AOT != 0
#include "aot_runtime.h"
#endif

#define READ_CHUNK ((size_t)16384)

void *pxa_wamr_aot_temporary_allocate(size_t bytes) {
#ifdef LOAD_MEMORY_RESERVATION
    void *memory = pxa_wamr_load_memory_scratch(bytes);
    if (memory) memset(memory, 0, bytes);
    return memory;
#else
    (void)bytes;return NULL;
#endif
}
int pxa_wamr_aot_temporary_release(void *pointer) {
#ifdef LOAD_MEMORY_RESERVATION
    return pxa_wamr_load_memory_release(pointer);
#else
    (void)pointer;return 0;
#endif
}

pxa_status_t pxa_wamr_read_exact(pxa_wamr_artifact_reader_t *reader,
                                uint64_t offset, uint8_t *output, size_t size) {
    if (offset > reader->size || size > reader->size - offset)
        return PXA_STATUS_IO_ERROR;
    while (size) {
        size_t requested = size < READ_CHUNK ? size : READ_CHUNK, count = 0;
        pxa_status_t status = reader->read_at(reader->context, offset, output,
                                             requested, &count);
        if (status != PXA_STATUS_OK) return status;
        if (!count || count > requested) return PXA_STATUS_IO_ERROR;
        output += count; offset += count; size -= count;
    }
    return PXA_STATUS_OK;
}

#if WASM_ENABLE_AOT != 0
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

pxa_status_t pxa_wamr_read_aot(pxa_wamr_artifact_reader_t *reader,
                              wasm_module_t *module, int *buffered,
                              int pinned, char *error, uint32_t error_size) {
    aot_section_t sections[6] = {{0}};
    uint32_t offsets[6] = {0};
    uint8_t header[64];
    uint64_t offset = 8;
    unsigned count = 0;
    pxa_status_t status = PXA_STATUS_UNSUPPORTED;
    int reserved = 0;
    (void)pinned;(void)reserved;
#if defined(__APPLE__)
    bool code_writable = false;
#endif
    *module = NULL; *buffered = 0;
    if (reader->size < sizeof(header) || reader->size > UINT32_MAX)
        goto done;
    status = pxa_wamr_read_exact(reader, 0, header, sizeof(header));
    if (status != PXA_STATUS_OK) goto done;
    if (wasm_runtime_is_xip_file(header, sizeof(header))) {
        if (error_size) snprintf(error, error_size, "XIP is unsupported; build ordinary AOT");
        status = PXA_STATUS_UNSUPPORTED;
        goto done;
    }
    if (memcmp(header, "\0aot", 4) || u32(header + 4) != AOT_CURRENT_VERSION ||
        u32(header + 8) != 0 || u32(header + 12) != 48) {
        status = PXA_STATUS_UNSUPPORTED;
        goto done;
    }
    /* Validate the directory before allocating code, including trailing bytes,
     * lengths and order. File and section sizes are unsigned 32-bit in AOT. */
    while (offset < reader->size) {
        if (reader->size - offset < 8) { status = PXA_STATUS_IO_ERROR; goto done; }
        status = pxa_wamr_read_exact(reader, offset, header, 8);
        if (status != PXA_STATUS_OK) goto done;
        uint32_t type = u32(header), size = u32(header + 4);
        if (size > reader->size - offset - 8) { status = PXA_STATUS_IO_ERROR; goto done; }
        if (type == 100) { *buffered = 1; return PXA_STATUS_OK; }
        if (count >= 6 || type != count) { status = PXA_STATUS_UNSUPPORTED; goto done; }
        sections[count].section_type = (int)type;
        sections[count].section_body_size = size;
        offsets[count] = (uint32_t)(offset + 8);
        if (count) sections[count - 1].next = &sections[count];
        ++count;
        offset += 8 + (uint64_t)size;
        if (offset != reader->size) {
            offset = (offset + 3) & ~(uint64_t)3;
            if (offset >= reader->size) { status = PXA_STATUS_IO_ERROR; goto done; }
        }
    }
    if (count != 6 || sections[2].section_body_size < 4) {
        status = PXA_STATUS_UNSUPPORTED; goto done;
    }
#ifdef LOAD_MEMORY_RESERVATION
    /* This is only a placement hint. The normal loader still validates every
     * memory declaration, bound, import and relocation. Other memory layouts
     * and custom-section AOT retain their existing allocation path. */
    if (pinned && sections[1].section_body_size >= 24) {
        status = pxa_wamr_read_exact(reader, offsets[1], header, 24);
        if (status != PXA_STATUS_OK) goto done;
        if (u32(header) == 0 && u32(header + 4) == 1 && u32(header + 8) == 1
            && u32(header + 12) == 65536 && u32(header + 16) <= u32(header + 20)) {
            uint64_t bytes = (uint64_t)u32(header + 20) * 65536;
            if (bytes && bytes <= SIZE_MAX)
                reserved = pxa_wamr_load_memory_begin((size_t)bytes);
        }
    }
#endif
#if defined(__APPLE__)
    os_thread_jit_write_protect_np(false);
    code_writable = true;
#endif
    for (unsigned i = 0; i < 6; ++i) {
        uint32_t size = sections[i].section_body_size;
        if (i == 2) {
            uint64_t mapped = ((uint64_t)size + aot_get_plt_table_size() + 3) & ~(uint64_t)3;
            if (mapped >= UINT32_MAX) { status = PXA_STATUS_RESOURCE_LIMIT; goto done; }
            sections[i].section_body = os_mmap(NULL, (uint32_t)mapped,
                MMAP_PROT_READ | MMAP_PROT_WRITE | MMAP_PROT_EXEC,
                MMAP_MAP_32BIT, os_get_invalid_handle());
            if (!sections[i].section_body)
                sections[i].section_body = os_mmap(NULL, (uint32_t)mapped,
                    MMAP_PROT_READ | MMAP_PROT_WRITE | MMAP_PROT_EXEC,
                    MMAP_MAP_NONE, os_get_invalid_handle());
            if (sections[i].section_body == (void *)-1) sections[i].section_body = NULL;
            sections[i].section_body_size = (uint32_t)mapped;
        } else {
            void *scratch = NULL;
#ifdef LOAD_MEMORY_RESERVATION
            if (reserved) scratch = pxa_wamr_load_memory_scratch(size ? size : 1);
#endif
            sections[i].section_body = scratch ? scratch : wasm_runtime_malloc(size ? size : 1);
        }
        if (!sections[i].section_body) { status = PXA_STATUS_RESOURCE_LIMIT; goto done; }
        uint8_t *destination = sections[i].section_body;
#if WASM_MEM_DUAL_BUS_MIRROR != 0 || WASM_MEM_INTERNAL_DUAL_BUS_MIRROR != 0
        if (i == 2) destination = os_get_dbus_mirror(destination);
#endif
        if (!destination) { status = PXA_STATUS_INTERNAL; goto done; }
        status = pxa_wamr_read_exact(reader, offsets[i], destination, size);
        if (status != PXA_STATUS_OK) goto done;
        if (i == 2) {
            if (u32(destination) > size - 4) { status = PXA_STATUS_UNSUPPORTED; goto done; }
            memset(destination + size, 0, sections[i].section_body_size - size);
            os_dcache_flush();
            os_icache_flush(sections[i].section_body, sections[i].section_body_size);
        }
    }
    /* The patched section API consumes TEXT on both success and failure,
     * clones initial data before returning, and flushes the instruction cache.
     * Other section buffers remain ours and are promptly discarded. */
    *module = wasm_runtime_load_from_sections(sections, true, error, error_size);
    sections[2].section_body = NULL;
    status = *module ? PXA_STATUS_OK : PXA_STATUS_UNSUPPORTED;
done:
#if defined(__APPLE__)
    if (code_writable) os_thread_jit_write_protect_np(true);
#endif
    for (unsigned i = 0; i < 6; ++i) {
        if (!sections[i].section_body) continue;
        if (i == 2) os_munmap(sections[i].section_body, sections[i].section_body_size);
        else {
#ifdef LOAD_MEMORY_RESERVATION
            if (reserved && pxa_wamr_load_memory_release(sections[i].section_body)) continue;
#endif
            wasm_runtime_free(sections[i].section_body);
        }
    }
#ifdef LOAD_MEMORY_RESERVATION
    if (reserved && status != PXA_STATUS_OK) pxa_wamr_load_memory_end();
#endif
    if (status != PXA_STATUS_OK && error_size && !*error)
        snprintf(error, error_size, "AOT reader failed: status=%d", (int)status);
    return status;
}
#else
pxa_status_t pxa_wamr_read_aot(pxa_wamr_artifact_reader_t *reader,
                              wasm_module_t *module, int *buffered,
                              int pinned, char *error, uint32_t error_size) {
    (void)reader;(void)pinned;
    *module = NULL; *buffered = 0;
    if (error_size) snprintf(error, error_size, "AOT support is disabled");
    return PXA_STATUS_UNSUPPORTED;
}
#endif
