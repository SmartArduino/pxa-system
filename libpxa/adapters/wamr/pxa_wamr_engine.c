#include "pxa/wamr/pxa_wamr_engine.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "pxa/wasi.h"
#include "pxa/surface.h"
#include "pxa/clock.h"
#include "pxa/ui.h"
#include "pxa/log.h"
#include "pxa/device.h"
#include "pxa/window.h"
#include "pxa/permission.h"
#include "pxa/storage.h"
#include "pxa/fs.h"
#include "pxa/ipc.h"
#include "pxa/net.h"
#include "pxa/audio.h"
#include "pxa/sensor.h"
#include "pxa/lease.h"
#include "pxa/scheduler.h"
#include "pxa/game_render.h"
#include "pxa/assets.h"
#include "pxa/service.h"
#include "pxa/wire.h"
#include "pxa/event_port.h"
#include "wasm_export.h"
#include "pxa_wamr_aot_reader.h"
#ifdef ESP_PLATFORM
#include "pxa_wamr_load_memory.h"
#endif

#if defined(ESP_PLATFORM)
#include "sdkconfig.h"
#if defined(CONFIG_WAMR_LINEAR_MEMORY_RESERVE_MAX) && \
    (!defined(WASM_LINEAR_MEMORY_RESERVE_MAX) || WASM_LINEAR_MEMORY_RESERVE_MAX == 0)
#error "WAMR pinned-memory feature must propagate from the runtime to its Host adapter"
#endif
#include "esp_log.h"
#include "pxa/esp/pxa_esp_posix_shim.h"
#define PXA_WAMR_LOG_TAG "PxaWamr"
#endif

#define PXA_WAMR_ENGINE_MAGIC UINT32_C(0x50574d52)
#define PXA_WAMR_ENGINE_ALIGNMENT ((size_t)16)
#define PXA_WAMR_DEFAULT_STACK_HEAP ((uint32_t)16384)
#define PXA_WAMR_DEFAULT_BYTES ((size_t)(1u << 20))
#define PXA_WAMR_MAX_MODULE_PATH ((size_t)512)
#ifndef PXA_WAMR_LIBC_WASI
#define PXA_WAMR_LIBC_WASI 0
#endif

typedef struct {
    struct pxa_wamr_engine *engine;
    uint64_t instance_id;
    uint64_t pending_instance_id;
    uint8_t pending;
    pxa_component_t component;
    const pxa_package_component_t *package_component;
    uint16_t core_major;
    uint16_t core_minor;
    uint16_t config_size;
    uint8_t kind;
    uint8_t occupied;
    uint8_t has_config;
    int wasi_null_fd;
    uint8_t *module_bytes;
    size_t module_size;
    size_t module_capacity;
    uint64_t linear_bytes;
    uint8_t dynamic_module_bytes;
    wasm_module_t module;
    wasm_module_inst_t module_instance;
    wasm_exec_env_t exec_env;
    wasm_function_inst_t start_fn;
    wasm_function_inst_t event_fn;
    wasm_function_inst_t stop_fn;
    uint32_t event_buffer;
    uint32_t event_capacity;
    uint64_t event_port;
    uint8_t cooperative_cancelled;
    uint8_t config[PXA_WAMR_ENGINE_MAX_CONFIG_BYTES];
} pxa_wamr_entry_t;

typedef union {
    struct {
        size_t size;
    } allocation;
    long double long_double_alignment;
    void *pointer_alignment;
    uint64_t integer_alignment;
} pxa_wamr_allocation_header_t;

struct pxa_wamr_engine {
    uint32_t magic;
    pxa_runtime_t *runtime;
    void *host_context;
    union {
        pxa_status_t (*read)(void *context, pxa_bytes_t path,
                             uint8_t *output, size_t capacity, size_t *size);
        pxa_wamr_artifact_open_fn open;
    } artifact;
    uint64_t (*now_us)(void *context);
    pxa_status_t (*prepare_start)(void *context, pxa_component_t component,
                                  uint64_t instance_id, uint8_t kind);
    uint64_t call_timeout_us;
    pxa_wamr_cooperate_fn cooperate;
    uint32_t guest_stack_size;
    uint32_t host_managed_heap_size;
    uint16_t max_components;
    size_t pool_bytes;
    size_t max_module_bytes;
    void *synchronization_context;
    pxa_wamr_synchronize_fn enter_critical;
    pxa_wamr_synchronize_fn leave_critical;
    void *artifact_allocator_context;
    pxa_wamr_artifact_allocate_fn allocate_artifact;
    pxa_wamr_artifact_release_fn release_artifact;
    void *runtime_allocator_context;
    pxa_wamr_runtime_allocate_fn allocate_runtime;
    pxa_wamr_runtime_reallocate_fn reallocate_runtime;
    pxa_wamr_runtime_release_fn release_runtime;
    size_t runtime_current_bytes;
    size_t runtime_peak_bytes;
    uint64_t linear_current_bytes;
    uint64_t linear_peak_bytes;
    uint64_t executing_deadline_us;
    wasm_module_inst_t executing_module;
    uint8_t busy;
    uint8_t terminate_requested;
    uint8_t wasi_enabled;
    uint8_t seekable_artifacts;
    uint8_t *pool;
    /* WAMR retains and sorts this array. Keep it in the engine workspace,
     * whose lifetime already encloses the runtime; no extra global SRAM. */
    NativeSymbol native_symbols[2];
    pxa_wamr_entry_t entries[];
};

/* WAMR owns one process-global allocator. The adapter likewise supports one
 * live engine per process and keeps this pointer valid through runtime destroy. */
static pxa_wamr_engine_t *runtime_allocator_engine;

static void update_runtime_peak(pxa_wamr_engine_t *engine) {
    if (engine->runtime_current_bytes > engine->runtime_peak_bytes) {
        engine->runtime_peak_bytes = engine->runtime_current_bytes;
    }
}

static void *dynamic_runtime_allocate(
#if WASM_MEM_ALLOC_WITH_USAGE != 0
    mem_alloc_usage_t usage,
#endif
#if WASM_MEM_ALLOC_WITH_USER_DATA != 0
    void *user_data,
#endif
    unsigned int size) {
#if WASM_MEM_ALLOC_WITH_USER_DATA != 0
    pxa_wamr_engine_t *engine = (pxa_wamr_engine_t *)user_data;
#else
    pxa_wamr_engine_t *engine = runtime_allocator_engine;
#endif
    pxa_wamr_allocation_header_t *header;
    size_t allocation_size = (size_t)size;
#if WASM_MEM_ALLOC_WITH_USAGE != 0
    (void)usage;
#endif
    if (engine == NULL || engine->allocate_runtime == NULL || size == 0 ||
        allocation_size > SIZE_MAX - sizeof(*header) ||
        allocation_size > SIZE_MAX - engine->runtime_current_bytes) {
        return NULL;
    }
    header = (pxa_wamr_allocation_header_t *)engine->allocate_runtime(
        engine->runtime_allocator_context, sizeof(*header) + allocation_size);
    if (header == NULL) return NULL;
    header->allocation.size = allocation_size;
    engine->runtime_current_bytes += allocation_size;
    update_runtime_peak(engine);
    return header + 1;
}

static void dynamic_runtime_release(
#if WASM_MEM_ALLOC_WITH_USAGE != 0
    mem_alloc_usage_t usage,
#endif
#if WASM_MEM_ALLOC_WITH_USER_DATA != 0
    void *user_data,
#endif
    void *memory) {
#if WASM_MEM_ALLOC_WITH_USER_DATA != 0
    pxa_wamr_engine_t *engine = (pxa_wamr_engine_t *)user_data;
#else
    pxa_wamr_engine_t *engine = runtime_allocator_engine;
#endif
    pxa_wamr_allocation_header_t *header;
#if WASM_MEM_ALLOC_WITH_USAGE != 0
    (void)usage;
#endif
    if (engine == NULL || engine->release_runtime == NULL || memory == NULL)
        return;
    header = (pxa_wamr_allocation_header_t *)memory - 1;
    if (header->allocation.size <= engine->runtime_current_bytes) {
        engine->runtime_current_bytes -= header->allocation.size;
    } else {
        engine->runtime_current_bytes = 0;
    }
    engine->release_runtime(engine->runtime_allocator_context, header);
}

static void *dynamic_runtime_reallocate(
#if WASM_MEM_ALLOC_WITH_USAGE != 0
    mem_alloc_usage_t usage, bool full_size_mapped,
#endif
#if WASM_MEM_ALLOC_WITH_USER_DATA != 0
    void *user_data,
#endif
    void *memory, unsigned int size) {
#if WASM_MEM_ALLOC_WITH_USER_DATA != 0
    pxa_wamr_engine_t *engine = (pxa_wamr_engine_t *)user_data;
#else
    pxa_wamr_engine_t *engine = runtime_allocator_engine;
#endif
    pxa_wamr_allocation_header_t *header;
    pxa_wamr_allocation_header_t *resized;
    size_t allocation_size = (size_t)size;
    size_t previous_size;
    size_t retained_size;
#if WASM_MEM_ALLOC_WITH_USAGE != 0
    (void)usage;
    (void)full_size_mapped;
#endif
    if (memory == NULL) {
        return dynamic_runtime_allocate(
#if WASM_MEM_ALLOC_WITH_USAGE != 0
            usage,
#endif
#if WASM_MEM_ALLOC_WITH_USER_DATA != 0
            user_data,
#endif
            size);
    }
    if (size == 0) {
        dynamic_runtime_release(
#if WASM_MEM_ALLOC_WITH_USAGE != 0
            usage,
#endif
#if WASM_MEM_ALLOC_WITH_USER_DATA != 0
            user_data,
#endif
            memory);
        return NULL;
    }
    if (engine == NULL || engine->reallocate_runtime == NULL ||
        allocation_size > SIZE_MAX - sizeof(*header)) {
        return NULL;
    }
    header = (pxa_wamr_allocation_header_t *)memory - 1;
    previous_size = header->allocation.size;
    retained_size = previous_size <= engine->runtime_current_bytes
                        ? engine->runtime_current_bytes - previous_size
                        : 0;
    if (allocation_size > SIZE_MAX - retained_size) return NULL;
    resized = (pxa_wamr_allocation_header_t *)engine->reallocate_runtime(
        engine->runtime_allocator_context, header,
        sizeof(*header) + allocation_size);
    if (resized == NULL) return NULL;
    resized->allocation.size = allocation_size;
    engine->runtime_current_bytes = retained_size + allocation_size;
    update_runtime_peak(engine);
    return resized + 1;
}

static void engine_enter_critical(const pxa_wamr_engine_t *engine) {
    if (engine->enter_critical != NULL)
        engine->enter_critical(engine->synchronization_context);
}

static void engine_leave_critical(const pxa_wamr_engine_t *engine) {
    if (engine->leave_critical != NULL)
        engine->leave_critical(engine->synchronization_context);
}

static uintptr_t align_up(uintptr_t value, size_t alignment) {
    uintptr_t mask = (uintptr_t)alignment - 1u;
    return (value + mask) & ~mask;
}

static void log_runtime_failure(const char *stage, const char *path,
                                const char *detail) {
#if defined(ESP_PLATFORM)
    mem_alloc_info_t memory = {0};
    if (wasm_runtime_get_mem_alloc_info(&memory)) {
        ESP_LOGE(PXA_WAMR_LOG_TAG,
                 "%s failed: artifact=%s detail=%s pool_free=%u/%u highmark=%u",
                 stage, path == NULL ? "-" : path,
                 detail == NULL || detail[0] == '\0' ? "-" : detail,
                 (unsigned)memory.total_free_size, (unsigned)memory.total_size,
                 (unsigned)memory.highmark_size);
    } else {
        ESP_LOGE(PXA_WAMR_LOG_TAG, "%s failed: artifact=%s detail=%s", stage,
                 path == NULL ? "-" : path,
                 detail == NULL || detail[0] == '\0' ? "-" : detail);
    }
#else
    (void)stage;
    (void)path;
    (void)detail;
#endif
}

static pxa_wamr_engine_t *entry_engine(pxa_wamr_entry_t *entry) {
    return entry == NULL ? NULL : entry->engine;
}

static pxa_wamr_entry_t *find_by_component(pxa_wamr_engine_t *engine,
                                           pxa_component_t component) {
    uint16_t index;
    for (index = 0; index < engine->max_components; ++index) {
        pxa_wamr_entry_t *entry = &engine->entries[index];
        if (entry->occupied && entry->component == component) return entry;
    }
    return NULL;
}

static pxa_wamr_entry_t *find_by_instance(pxa_wamr_engine_t *engine,
                                          uint64_t instance_id) {
    uint16_t index;
    for (index = 0; index < engine->max_components; ++index) {
        pxa_wamr_entry_t *entry = &engine->entries[index];
        if (entry->occupied && entry->instance_id == instance_id) return entry;
    }
    return NULL;
}

static pxa_wamr_entry_t *find_pending_by_instance(pxa_wamr_engine_t *engine,
                                                  uint64_t instance_id) {
    uint16_t index;
    for (index = 0; index < engine->max_components; ++index) {
        pxa_wamr_entry_t *entry = &engine->entries[index];
        if (!entry->occupied && entry->pending &&
            entry->pending_instance_id == instance_id) {
            return entry;
        }
    }
    return NULL;
}

static pxa_wamr_entry_t *reserve_instance_entry(pxa_wamr_engine_t *engine,
                                                uint64_t instance_id) {
    pxa_wamr_entry_t *entry = find_pending_by_instance(engine, instance_id);
    uint16_t index;
    if (entry != NULL) return entry;
    for (index = 0; index < engine->max_components; ++index) {
        entry = &engine->entries[index];
        if (!entry->occupied && !entry->pending) {
            entry->pending = 1;
            entry->pending_instance_id = instance_id;
            return entry;
        }
    }
    return NULL;
}

static int has_signature(wasm_module_inst_t module, wasm_function_inst_t fn,
                         uint32_t parameters, uint32_t results) {
    return fn != NULL && wasm_func_get_param_count(fn, module) == parameters &&
           wasm_func_get_result_count(fn, module) == results;
}

static int component_wasi_features(const pxa_package_component_t *component,
                                   uint64_t *features) {
    uint16_t index;
    if (features == NULL) return 0;
    *features = 0;
    if (component == NULL) return 0;
    for (index = 0; index < component->service_count; ++index) {
        if (component->services[index].service == PXA_WASI_SERVICE_ID) {
            *features = component->services[index].required_features;
            return 1;
        }
    }
    return 0;
}

static uint64_t wasi_import_feature(const char *name) {
    if (name == NULL) return UINT64_MAX;
    /* wasi-libc's formatting implementation retains these imports even for
     * snprintf. Base WASI binds descriptors 0..2 to a PXA-owned /dev/null,
     * so exposing them cannot reach Host stdio or files. */
    if (strcmp(name, "fd_close") == 0 || strcmp(name, "fd_seek") == 0 ||
        strcmp(name, "fd_write") == 0) {
        return 0;
    }
    if (strcmp(name, "fd_read") == 0 || strcmp(name, "fd_fdstat_get") == 0) {
        return PXA_WASI_FEATURE_STDIO;
    }
    if (strcmp(name, "clock_res_get") == 0 ||
        strcmp(name, "clock_time_get") == 0) {
        /* Preview 1 selects the clock at call time, so both clock classes
         * must be granted before exposing the shared import. */
        return PXA_WASI_FEATURE_CLOCKS;
    }
    if (strcmp(name, "random_get") == 0) return PXA_WASI_FEATURE_RANDOM;
    if (strcmp(name, "args_get") == 0 || strcmp(name, "args_sizes_get") == 0) {
        return PXA_WASI_FEATURE_ARGUMENTS;
    }
    if (strcmp(name, "environ_get") == 0 ||
        strcmp(name, "environ_sizes_get") == 0) {
        return PXA_WASI_FEATURE_ENVIRONMENT;
    }
    if (strncmp(name, "path_", 5) == 0 || strcmp(name, "fd_readdir") == 0 ||
        strcmp(name, "fd_prestat_get") == 0 ||
        strcmp(name, "fd_prestat_dir_name") == 0 ||
        strcmp(name, "fd_tell") == 0 || strcmp(name, "fd_sync") == 0 ||
        strcmp(name, "fd_datasync") == 0 || strcmp(name, "fd_advise") == 0 ||
        strcmp(name, "fd_allocate") == 0 ||
        strcmp(name, "fd_fdstat_set_flags") == 0 ||
        strcmp(name, "fd_filestat_get") == 0 ||
        strcmp(name, "fd_filestat_set_size") == 0 ||
        strcmp(name, "fd_filestat_set_times") == 0) {
        return PXA_WASI_FEATURE_PRIVATE_FS;
    }
    return UINT64_MAX;
}

static void close_wasi_null(pxa_wamr_entry_t *entry) {
    if (entry != NULL && entry->wasi_null_fd >= 0) {
#if !defined(ESP_PLATFORM)
        (void)close(entry->wasi_null_fd);
#endif
        entry->wasi_null_fd = -1;
    }
}

static void sample_linear_memory(pxa_wamr_engine_t *engine,
                                  pxa_wamr_entry_t *entry) {
    uint64_t begin = 0, end = 0;
    if (entry->module_instance != NULL)
        (void)wasm_runtime_get_app_addr_range(entry->module_instance, 0,
                                             &begin, &end);
    engine->linear_current_bytes -= entry->linear_bytes;
    entry->linear_bytes = end - begin;
    engine->linear_current_bytes += entry->linear_bytes;
    if (engine->linear_current_bytes > engine->linear_peak_bytes)
        engine->linear_peak_bytes = engine->linear_current_bytes;
}

static void release_module_buffer(pxa_wamr_engine_t *engine,
                                  pxa_wamr_entry_t *entry) {
    if (entry->dynamic_module_bytes && entry->module_bytes != NULL) {
        engine->release_artifact(engine->artifact_allocator_context,
                                 entry->module_bytes);
        entry->module_bytes = NULL;
        entry->module_capacity = 0;
        entry->dynamic_module_bytes = 0;
    }
    entry->module_size = 0;
}

static int standard_aot_sections(const uint8_t *bytes, size_t size) {
    /* Raw/custom sections may still borrow payload bytes in WAMR. Keep the
     * original ownership for extensions; reclaim only the six standard AOT
     * sections. The loader still validates their order, version and contents. */
    size_t at = 8;
    if (size < 8 || size > UINT32_MAX ||
        get_package_type(bytes, (uint32_t)size) != Wasm_Module_AoT) return 0;
    while (at < size) {
        if (size - at < 8 || pxa_read_u32(bytes + at) > 5) return 0;
        size_t length = pxa_read_u32(bytes + at + 4);
        if (length > size - at - 8) return 0;
        at += 8 + length;
        if (at == size) return 1;
        if (at > SIZE_MAX - 3) return 0;
        at = (at + 3) & ~(size_t)3;
    }
    return at == size;
}

static pxa_status_t load_module_buffer(pxa_wamr_engine_t *engine,
                                       pxa_wamr_entry_t *entry,
                                       pxa_bytes_t path) {
    size_t expected_size = 0;
    size_t loaded_size = 0;
    pxa_status_t status = engine->artifact.read(engine->host_context, path,
                                                NULL, 0, &expected_size);
    if (status != PXA_STATUS_OK) return status;
    if (expected_size == 0 || expected_size > UINT32_MAX ||
        (engine->max_module_bytes != 0 &&
         expected_size > engine->max_module_bytes)) {
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    if (engine->allocate_artifact != NULL) {
        entry->module_bytes = (uint8_t *)engine->allocate_artifact(
            engine->artifact_allocator_context, expected_size);
        if (entry->module_bytes == NULL) return PXA_STATUS_RESOURCE_LIMIT;
        entry->module_capacity = expected_size;
        entry->dynamic_module_bytes = 1;
    }
    if (entry->module_bytes == NULL || expected_size > entry->module_capacity) {
        release_module_buffer(engine, entry);
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    status =
        engine->artifact.read(engine->host_context, path, entry->module_bytes,
                              entry->module_capacity, &loaded_size);
    if (status != PXA_STATUS_OK || loaded_size != expected_size) {
        release_module_buffer(engine, entry);
        return status == PXA_STATUS_OK ? PXA_STATUS_INTERNAL : status;
    }
    entry->module_size = loaded_size;
    return PXA_STATUS_OK;
}

/* The reader is scoped to loading: no open file or storage callback remains
 * in the execution path. AOT-only packages never fall back to interpretation. */
static pxa_status_t load_artifact(pxa_wamr_engine_t *engine,
                                  pxa_wamr_entry_t *slot, pxa_bytes_t path,
                                  uint8_t kind, char *error, uint32_t error_size) {
    pxa_wamr_artifact_reader_t reader = {0};
    pxa_status_t status;
    LoadArgs args = {0};
    error[0] = 0;
    if (engine->seekable_artifacts) {
        uint8_t header[8];
        int buffered = 0;
        status = engine->artifact.open(engine->host_context, path, &reader);
        if (status != PXA_STATUS_OK) return status;
        if (!reader.read_at || !reader.close) {
            if (reader.close) reader.close(reader.context);
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        if (!reader.size || reader.size > UINT32_MAX ||
            (engine->max_module_bytes && reader.size > engine->max_module_bytes)) {
            status = PXA_STATUS_RESOURCE_LIMIT; goto close_reader;
        }
        status = pxa_wamr_read_exact(&reader, 0, header, sizeof(header));
        if (status != PXA_STATUS_OK) goto close_reader;
        if (kind == PXA_ARTIFACT_AOT && get_package_type(header, sizeof(header)) == Wasm_Module_AoT) {
            status = pxa_wamr_read_aot(&reader, &slot->module, &buffered,
                (slot->package_component->flags & PXA_PACKAGE_COMPONENT_FLAG_PINNED_MEMORY) != 0,
                error, error_size);
#if defined(ESP_PLATFORM)
            if (status == PXA_STATUS_OK && slot->module)
                ESP_LOGI(PXA_WAMR_LOG_TAG, "AOT streamed: file=%u source_buffer=0",
                         (unsigned)reader.size);
#endif
            if (status != PXA_STATUS_OK || !buffered) goto close_reader;
        }
        if (engine->allocate_artifact) {
            slot->module_bytes = engine->allocate_artifact(engine->artifact_allocator_context,
                                                           (size_t)reader.size);
            if (!slot->module_bytes) { status = PXA_STATUS_RESOURCE_LIMIT; goto close_reader; }
            slot->module_capacity = (size_t)reader.size;
            slot->dynamic_module_bytes = 1;
        }
        if (!slot->module_bytes || reader.size > slot->module_capacity) {
            status = PXA_STATUS_RESOURCE_LIMIT; goto close_reader;
        }
        status = pxa_wamr_read_exact(&reader, 0, slot->module_bytes, (size_t)reader.size);
        if (status == PXA_STATUS_OK) slot->module_size = (size_t)reader.size;
close_reader:
        reader.close(reader.context);
        if (status != PXA_STATUS_OK || slot->module) return status;
    } else {
        status = load_module_buffer(engine, slot, path);
        if (status != PXA_STATUS_OK) return status;
    }
    if (wasm_runtime_is_xip_file(slot->module_bytes, (uint32_t)slot->module_size)) {
        snprintf(error, error_size, "XIP is not supported; build ordinary AOT");
        return PXA_STATUS_UNSUPPORTED;
    }
    args.name = "";
    args.wasm_binary_freeable = slot->dynamic_module_bytes && kind == PXA_ARTIFACT_AOT &&
        standard_aot_sections(slot->module_bytes, slot->module_size);
    slot->module = wasm_runtime_load_ex(slot->module_bytes, (uint32_t)slot->module_size,
                                        &args, error, error_size);
    if (!slot->module) return PXA_STATUS_UNSUPPORTED;
    if (wasm_runtime_is_underlying_binary_freeable(slot->module))
        release_module_buffer(engine, slot);
    return PXA_STATUS_OK;
}

static pxa_status_t discard_entry(pxa_wamr_engine_t *engine,
                                  pxa_wamr_entry_t *entry,
                                  pxa_status_t status) {
    if (entry->event_port && engine->runtime)
        (void)pxa_handle_close64(engine->runtime, entry->component, entry->event_port);
    entry->event_port = 0;
    if (entry->module_instance != NULL) {
        if (entry->event_buffer != 0)
            wasm_runtime_module_free(entry->module_instance, entry->event_buffer);
        entry->event_buffer = 0;
        entry->event_capacity = 0;
        wasm_runtime_set_custom_data(entry->module_instance, NULL);
    }
    if (entry->exec_env != NULL) {
        wasm_runtime_destroy_exec_env(entry->exec_env);
        entry->exec_env = NULL;
    }
    if (entry->module_instance != NULL) {
        wasm_runtime_deinstantiate(entry->module_instance);
        entry->module_instance = NULL;
        sample_linear_memory(engine, entry);
    }
    close_wasi_null(entry);
    if (entry->module != NULL) {
        wasm_runtime_unload(entry->module);
        entry->module = NULL;
    }
#ifdef ESP_PLATFORM
    pxa_wamr_load_memory_end();
#endif
    release_module_buffer(engine, entry);
    entry->config_size = 0;
    entry->has_config = 0;
    entry->pending_instance_id = 0;
    entry->pending = 0;
    entry->instance_id = 0;
    entry->component = PXA_COMPONENT_INVALID;
    entry->package_component = NULL;
    entry->core_major = 0;
    entry->core_minor = 0;
    entry->kind = 0;
    entry->start_fn = NULL;
    entry->event_fn = NULL;
    entry->stop_fn = NULL;
    entry->occupied = 0;
    return status;
}

static pxa_status_t validate_module_imports(wasm_module_t module,
                                            const char *path, uint16_t core_major,
                                            uint16_t core_minor,
                                            int wasi_declared,
                                            uint64_t wasi_features) {
    (void)core_minor;
    int32_t count = wasm_runtime_get_import_count(module);
    int32_t index;
    if (count < 0) {
        log_runtime_failure("inspect-imports", path,
                            "import enumeration failed");
        return PXA_STATUS_UNSUPPORTED;
    }
    for (index = 0; index < count; ++index) {
        wasm_import_t imported;
        wasm_runtime_get_import_type(module, index, &imported);
        if (imported.kind != WASM_IMPORT_EXPORT_KIND_FUNC ||
            imported.module_name == NULL || imported.name == NULL) {
            log_runtime_failure("validate-import", path,
                                "non-function or unnamed import");
            return PXA_STATUS_UNSUPPORTED;
        }
        if (strcmp(imported.module_name, "pxa.core.v2") == 0) {
            if (core_major != 2 ||
                (strcmp(imported.name, "pxa_io") != 0 &&
                 strcmp(imported.name, "pxa_call") != 0)) {
                log_runtime_failure("validate-import", path, imported.name);
                return PXA_STATUS_UNSUPPORTED;
            }
            continue;
        }
        if (strcmp(imported.module_name, "wasi_snapshot_preview1") == 0) {
            uint64_t needed = wasi_import_feature(imported.name);
            if (!wasi_declared || needed == UINT64_MAX ||
                (needed & ~wasi_features) != 0) {
                log_runtime_failure("authorize-wasi-import", path,
                                    imported.name);
                return PXA_STATUS_DENIED;
            }
            continue;
        }
        log_runtime_failure("validate-import", path, imported.module_name);
        return PXA_STATUS_UNSUPPORTED;
    }
    return PXA_STATUS_OK;
}

static uint64_t import_started(pxa_wamr_entry_t *entry) {
#if defined(ESP_PLATFORM)
    pxa_wamr_engine_t *engine=entry_engine(entry);
    return engine->now_us ? engine->now_us(engine->host_context) : 0;
#else
    (void)entry; return 0;
#endif
}
static void import_finished(pxa_wamr_entry_t *entry,const char *kind,
                            uint32_t operation,uint32_t bytes,uint64_t start) {
#if defined(ESP_PLATFORM)
    uint64_t end=import_started(entry);
    if(start && end>=start && end-start>=250000) {
        ESP_LOGW(PXA_WAMR_LOG_TAG,
            "Slow Host import: component=%u kind=%s operation=%u bytes=%u elapsed_us=%llu",
            (unsigned)entry->component,kind,(unsigned)operation,(unsigned)bytes,
            (unsigned long long)(end-start));
    }
#else
    (void)entry; (void)kind; (void)operation; (void)bytes; (void)start;
#endif
}

static int main_loop_enabled(const pxa_wamr_entry_t *entry) {
    return entry->core_major == 2 && entry->core_minor >= 1 &&
        entry->package_component != NULL &&
        (entry->package_component->flags & PXA_PACKAGE_COMPONENT_FLAG_MAIN_LOOP) != 0;
}

static void close_event_port(void *context) {
    pxa_wamr_entry_t *entry = context;
    entry->event_port = 0;
}

static int32_t open_event_port(pxa_wamr_entry_t *entry, uint64_t token,
                               uint32_t size) {
    pxa_wamr_engine_t *engine = entry_engine(entry);
    pxa_status_t status = pxa_component_validate_import(engine->runtime, entry->component);
    uint8_t result[8];
    pxa_resource_t resource = {entry, NULL, close_event_port};
    if (status != PXA_STATUS_OK) return status;
    if (!main_loop_enabled(entry) || !engine->cooperate || !engine->now_us)
        return PXA_STATUS_UNSUPPORTED;
    if (!token || size) return PXA_STATUS_INVALID_ARGUMENT;
    if (entry->event_port) return PXA_STATUS_BAD_STATE;
    status = pxa_request_begin_reserved(engine->runtime, entry->component, token,
        PXA_SERVICE_CORE, PXA_CORE_OPEN_EVENT_PORT, 0, sizeof(result));
    if (status != PXA_STATUS_OK) return status;
    status = pxa_handle_open64(engine->runtime, entry->component,
        PXA_RESOURCE_EVENT_PORT, 0, &resource, &entry->event_port);
    if (status != PXA_STATUS_OK)
        return pxa_request_complete(engine->runtime, entry->component, token, status, NULL, 0);
    pxa_write_u64(result, entry->event_port);
    status = pxa_request_complete(engine->runtime, entry->component, token,
        PXA_STATUS_OK, result, sizeof(result));
    if (status != PXA_STATUS_OK)
        (void)pxa_handle_close64(engine->runtime, entry->component, entry->event_port);
    return status;
}

static int32_t wait_event(pxa_wamr_entry_t *entry, uint64_t handle,
                           uint8_t *data, uint32_t size) {
    pxa_wamr_engine_t *engine = entry_engine(entry);
    pxa_resource_t resource;
    pxa_component_snapshot_t component;
    pxa_status_t status;
    int32_t result;
    uint64_t entered, timeout, until;
    int paused = 0;
    if (!main_loop_enabled(entry) || !engine->cooperate || !engine->now_us)
        return PXA_STATUS_UNSUPPORTED;
    status = pxa_handle_get64(engine->runtime, entry->component, handle,
        PXA_RESOURCE_EVENT_PORT, &resource);
    if (status != PXA_STATUS_OK) return status;
    if (resource.context != entry || handle != entry->event_port)
        return PXA_STATUS_BAD_STATE;
    status = pxa_component_snapshot(engine->runtime, entry->component, &component);
    if (status != PXA_STATUS_OK) return status;
    if (component.state != PXA_COMPONENT_RUNNING || component.callback != PXA_GUEST_CALLBACK_EVENT)
        return PXA_STATUS_BAD_STATE;
    if (!data || size < PXA_ENVELOPE_SIZE || size > PXA_MAX_CONTROL_MESSAGE)
        return PXA_STATUS_INVALID_ARGUMENT;
    timeout = pxa_read_u64(data);
    if (timeout > PXA_EVENT_PORT_MAX_WAIT_US) return PXA_STATUS_LIMIT_EXCEEDED;
    entered = engine->now_us(engine->host_context);
    if (entered > UINT64_MAX - timeout) return PXA_STATUS_INVALID_ARGUMENT;
    until = entered + timeout;
    /* A wait is a cooperative execution boundary, like returning a callback.
     * Never erase an already overdue deadline or an operator decision. While
     * native maintenance owns the thread, UINT64_MAX suspends the deadline;
     * an external terminate sets it to zero and wins over restoration. */
    engine_enter_critical(engine);
    if (engine->executing_deadline_us > entered && engine->executing_deadline_us != UINT64_MAX) {
        engine->executing_deadline_us = UINT64_MAX;
        paused = 1;
    }
    engine_leave_critical(engine);
    /* Service Host input/completions even if the mailbox was already ready. */
    status = engine->cooperate(engine->host_context, entry->component, 0);
    for (;;) {
        pxa_event_view_t view;
        size_t popped = 0;
        engine_enter_critical(engine);
        int terminated = engine->terminate_requested;
        engine_leave_critical(engine);
        if (terminated) { result = PXA_STATUS_CANCELLED; break; }
        if (status != PXA_STATUS_OK && status != PXA_STATUS_WOULD_BLOCK) {
            result = status;
            break;
        }
        if (entry->event_port != handle) { result = PXA_STATUS_CANCELLED; break; }
        status = pxa_event_peek(engine->runtime, entry->component, &view);
        if (status == PXA_STATUS_OK) {
            if (!view.size || view.size > PXA_MAX_CONTROL_MESSAGE) {
                result = PXA_STATUS_INTERNAL; break;
            }
            if (view.size > size) { result = PXA_STATUS_RESOURCE_LIMIT; break; }
            status = pxa_event_pop(engine->runtime, entry->component, data, size, &popped);
            result = status == PXA_STATUS_OK ? (int32_t)popped : status;
            break;
        }
        if (status != PXA_STATUS_WOULD_BLOCK) { result = status; break; }
        if (!timeout) { result = PXA_STATUS_WOULD_BLOCK; break; }
        if (engine->now_us(engine->host_context) >= until) { result = PXA_STATUS_TIMED_OUT; break; }
        /* A maintenance pass can handle an already overdue call and receive
         * the operator's explicit "wait" decision. Suspend that newly granted
         * deadline before the subsequent native sleep, too. */
        engine_enter_critical(engine);
        uint64_t now = engine->now_us(engine->host_context);
        if (!paused && engine->executing_deadline_us > now &&
            engine->executing_deadline_us != UINT64_MAX) {
            engine->executing_deadline_us = UINT64_MAX;
            paused = 1;
        }
        engine_leave_critical(engine);
        status = engine->cooperate(engine->host_context, entry->component, until);
    }
    engine_enter_critical(engine);
    if (paused && engine->executing_module == entry->module_instance &&
        engine->executing_deadline_us == UINT64_MAX) {
        uint64_t now = engine->now_us(engine->host_context);
        engine->executing_deadline_us = now > UINT64_MAX - engine->call_timeout_us
            ? UINT64_MAX : now + engine->call_timeout_us;
    }
    engine_leave_critical(engine);
    if (result == PXA_STATUS_CANCELLED) {
        entry->cooperative_cancelled = 1;
        wasm_runtime_set_exception(entry->module_instance, "PXA cooperative stop");
    }
    return result;
}

/* Core 2 borrows the validated Guest range until this import returns.
 * Backends must copy any bytes retained after return. */
static int32_t native_call(void *opaque_exec_env, uint32_t route,
                           uint64_t token, const uint8_t *data, uint32_t size) {
    wasm_module_inst_t module = wasm_runtime_get_module_inst(
        (wasm_exec_env_t)opaque_exec_env);
    pxa_wamr_entry_t *entry = wasm_runtime_get_custom_data(module);
    pxa_message_view_t message;
    if (entry == NULL || entry->core_major != 2 ||
        entry_engine(entry)->runtime == NULL) return PXA_STATUS_BAD_STATE;
    if ((uint16_t)route == PXA_SERVICE_CORE &&
        (uint16_t)(route >> 16) == PXA_CORE_OPEN_EVENT_PORT)
        return open_event_port(entry, token, size);
    message.service = (uint16_t)route;
    message.opcode = (uint16_t)(route >> 16);
    message.request_token = token;
    message.payload = (pxa_bytes_t){data, size};
    uint64_t start=import_started(entry);
    int32_t result=pxa_runtime_control_view(entry_engine(entry)->runtime,
                                          entry->component,&message);
    import_finished(entry,"control",route,size,start);
    return result;
}

static int32_t native_io(void *opaque_exec_env, uint64_t handle,
                            uint32_t operation, uint8_t *data,
                            uint32_t size) {
    wasm_exec_env_t exec_env = (wasm_exec_env_t)opaque_exec_env;
    wasm_module_inst_t module = wasm_runtime_get_module_inst(exec_env);
    pxa_wamr_entry_t *entry =
        (pxa_wamr_entry_t *)wasm_runtime_get_custom_data(module);
    if (entry == NULL || entry->core_major != 2 ||
        entry_engine(entry)->runtime == NULL)
        return PXA_STATUS_BAD_STATE;
    if (operation == PXA_EVENT_PORT_IO_WAIT) {
        pxa_resource_t resource;
        if (pxa_handle_get64(entry_engine(entry)->runtime, entry->component,
            handle, PXA_RESOURCE_EVENT_PORT, &resource) == PXA_STATUS_OK)
            return wait_event(entry, handle, data, size);
    }
    if (operation == PXA_SURFACE_IO_REGISTER_BUFFERS &&
        (entry->package_component == NULL ||
         (entry->package_component->flags &
          PXA_PACKAGE_COMPONENT_FLAG_PINNED_MEMORY) == 0)) {
        pxa_resource_t resource;
        if (pxa_handle_get64(entry_engine(entry)->runtime,
                             entry->component, handle,
                             PXA_RESOURCE_SURFACE, &resource) ==
            PXA_STATUS_OK)
            return PXA_STATUS_UNSUPPORTED;
    }
    uint64_t start=import_started(entry);
    uint64_t begin = 0, end = 0;
    int32_t result;
    if (!wasm_runtime_get_app_addr_range(module, 0, &begin, &end) ||
        begin != 0 || end == 0 || end > SIZE_MAX)
        return PXA_STATUS_INVALID_ARGUMENT;
    result = pxa_runtime_io64_memory(entry_engine(entry)->runtime,
        entry->component, handle, operation, data, size,
        wasm_runtime_addr_app_to_native(module, 0), (size_t)end);
    import_finished(entry,"io",operation,size,start);
    return result;
}

static int call(pxa_wamr_engine_t *engine, pxa_wamr_entry_t *entry,
                wasm_function_inst_t fn, uint32_t argc, uint32_t *values) {
    uint64_t deadline = 0;
    int success;
    if (engine->call_timeout_us != 0 && engine->now_us != NULL) {
        uint64_t now = engine->now_us(engine->host_context);
        deadline = now > UINT64_MAX - engine->call_timeout_us
                       ? UINT64_MAX
                       : now + engine->call_timeout_us;
    }
    engine_enter_critical(engine);
    if (engine->busy) {
        engine_leave_critical(engine);
        return 0;
    }
    engine->busy = 1;
    engine->terminate_requested = 0;
    entry->cooperative_cancelled = 0;
    engine->executing_module = entry->module_instance;
    engine->executing_deadline_us = deadline;
    engine_leave_critical(engine);
    success = wasm_runtime_call_wasm(entry->exec_env, fn, argc, values);
    sample_linear_memory(engine, entry);
    engine_enter_critical(engine);
    if (engine->executing_module == entry->module_instance) {
        if (main_loop_enabled(entry) && engine->terminate_requested) {
            entry->cooperative_cancelled = 1;
            success = 0;
        }
        engine->executing_module = NULL;
        engine->executing_deadline_us = 0;
        engine->busy = 0;
    }
    engine_leave_critical(engine);
    if (!success && !entry->cooperative_cancelled &&
        wasm_runtime_get_exception(entry->module_instance) != NULL) {
        fprintf(stderr,"PXA Guest exception: component=%u %s\n",
            (unsigned)entry->component,wasm_runtime_get_exception(entry->module_instance));
    }
    return success;
}

static int config_valid(const pxa_wamr_engine_config_t *config) {
    return config != NULL && config->struct_size >= sizeof(*config) &&
           (config->read_artifact != NULL || config->open_artifact != NULL) &&
           config->max_components != 0 &&
           (config->cooperate == NULL || config->now_us != NULL) &&
           ((config->enter_critical == NULL &&
             config->leave_critical == NULL) ||
            (config->enter_critical != NULL &&
             config->leave_critical != NULL)) &&
           ((config->allocate_artifact == NULL &&
             config->release_artifact == NULL) ||
            (config->allocate_artifact != NULL &&
             config->release_artifact != NULL)) &&
           ((config->allocate_runtime == NULL &&
             config->reallocate_runtime == NULL &&
             config->release_runtime == NULL) ||
            (config->allocate_runtime != NULL &&
             config->reallocate_runtime != NULL &&
             config->release_runtime != NULL));
}

static pxa_status_t engine_instantiate(void *context, pxa_bytes_t package_root,
                                       const pxa_activation_entry_t *entry,
                                       pxa_component_t component,
                                       uint64_t instance_id);
static pxa_status_t engine_start(void *context, pxa_component_t component);
static void engine_stop(void *context, pxa_component_t component,
                        pxa_stop_reason_t reason);
static void engine_destroy(void *context, pxa_component_t component);

size_t pxa_wamr_engine_workspace_size(const pxa_wamr_engine_config_t *config) {
    size_t entries_size;
    size_t pool_bytes;
    size_t module_bytes;
    size_t module_storage_bytes;
    size_t size;
    if (!config_valid(config)) return 0;
    if (sizeof(pxa_wamr_entry_t) > SIZE_MAX / config->max_components) {
        return 0;
    }
    entries_size = (size_t)config->max_components * sizeof(pxa_wamr_entry_t);
    pool_bytes = config->allocate_runtime != NULL
                     ? 0
                     : config->pool_bytes == 0 ? PXA_WAMR_DEFAULT_BYTES
                                               : config->pool_bytes;
    module_bytes = config->max_module_bytes;
    if (config->allocate_artifact == NULL && module_bytes == 0) {
        module_bytes = PXA_WAMR_DEFAULT_BYTES;
    }
    if (config->allocate_artifact != NULL) {
        module_storage_bytes = 0;
    } else {
        if (module_bytes > SIZE_MAX / config->max_components) return 0;
        module_storage_bytes = module_bytes * config->max_components;
    }
    size = PXA_WAMR_ENGINE_ALIGNMENT - 1u;
    if (sizeof(pxa_wamr_engine_t) > SIZE_MAX - size) return 0;
    size += sizeof(pxa_wamr_engine_t);
    if (entries_size > SIZE_MAX - size) return 0;
    size += entries_size;
    if (pool_bytes > SIZE_MAX - size) return 0;
    size += pool_bytes;
    if (module_storage_bytes > SIZE_MAX - size) return 0;
    return size + module_storage_bytes;
}

pxa_status_t pxa_wamr_engine_init(void *workspace, size_t workspace_size,
                                  const pxa_wamr_engine_config_t *config,
                                  pxa_wamr_engine_t **output,
                                  pxa_component_engine_t *engine_output) {
    pxa_wamr_engine_t *engine;
    pxa_component_engine_t *engine_ops;
    size_t required;
    size_t entries_size;
    size_t pool_bytes;
    size_t module_bytes;
    size_t module_storage_bytes;
    uint8_t *cursor;
    uint16_t index;
    RuntimeInitArgs arguments;
    /* Native registrations are stored in this workspace until deinit. */
    if (output == NULL || engine_output == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *output = NULL;
    memset(engine_output, 0, sizeof(*engine_output));
    required = pxa_wamr_engine_workspace_size(config);
    if (workspace == NULL || required == 0 || workspace_size < required) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    engine = (pxa_wamr_engine_t *)align_up((uintptr_t)workspace,
                                           PXA_WAMR_ENGINE_ALIGNMENT);
    if ((uintptr_t)engine > (uintptr_t)workspace + workspace_size) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    entries_size = (size_t)config->max_components * sizeof(pxa_wamr_entry_t);
    pool_bytes = config->allocate_runtime != NULL
                     ? 0
                     : config->pool_bytes == 0 ? PXA_WAMR_DEFAULT_BYTES
                                               : config->pool_bytes;
    module_bytes = config->max_module_bytes;
    if (config->allocate_artifact == NULL && module_bytes == 0) {
        module_bytes = PXA_WAMR_DEFAULT_BYTES;
    }
    module_storage_bytes = config->allocate_artifact == NULL
                               ? module_bytes * config->max_components
                               : 0;
    if ((uintptr_t)engine + sizeof(*engine) + entries_size + pool_bytes +
            module_storage_bytes >
        (uintptr_t)workspace + workspace_size) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    memset(engine, 0, sizeof(*engine) + entries_size);
    engine->magic = PXA_WAMR_ENGINE_MAGIC;
    engine->host_context = config->host_context;
    engine->seekable_artifacts = config->open_artifact != NULL;
    if (engine->seekable_artifacts) engine->artifact.open = config->open_artifact;
    else engine->artifact.read = config->read_artifact;
    engine->now_us = config->now_us;
    engine->prepare_start = config->prepare_start;
    engine->call_timeout_us = config->call_timeout_us;
    engine->cooperate = config->cooperate;
    engine->guest_stack_size = config->guest_stack_size == 0
                                   ? PXA_WAMR_DEFAULT_STACK_HEAP
                                   : config->guest_stack_size;
    engine->host_managed_heap_size =
        config->host_managed_heap_size == 0
            ? PXA_WAMR_DEFAULT_STACK_HEAP
            : config->host_managed_heap_size;
    engine->max_components = config->max_components;
    engine->wasi_enabled = config->wasi_enabled && PXA_WAMR_LIBC_WASI;
    engine->pool_bytes = pool_bytes;
    engine->max_module_bytes = module_bytes;
    engine->synchronization_context = config->synchronization_context;
    engine->enter_critical = config->enter_critical;
    engine->leave_critical = config->leave_critical;
    engine->artifact_allocator_context = config->artifact_allocator_context;
    engine->allocate_artifact = config->allocate_artifact;
    engine->release_artifact = config->release_artifact;
    engine->runtime_allocator_context = config->runtime_allocator_context;
    engine->allocate_runtime = config->allocate_runtime;
    engine->reallocate_runtime = config->reallocate_runtime;
    engine->release_runtime = config->release_runtime;
    cursor = (uint8_t *)engine + sizeof(*engine) + entries_size;
    if (pool_bytes != 0) {
        engine->pool = cursor;
        cursor += pool_bytes;
        memset(engine->pool, 0, pool_bytes);
    }

    engine->native_symbols[0].symbol = "pxa_io";
    {
        union {
            void *object;
            int32_t (*function)(void *, uint64_t, uint32_t, uint8_t *,
                                uint32_t);
        } conversion;
        conversion.function = native_io;
        engine->native_symbols[0].func_ptr = conversion.object;
    }
    engine->native_symbols[0].signature = "(Ii*~)i";
    engine->native_symbols[0].attachment = NULL;
    engine->native_symbols[1].symbol = "pxa_call";
    {
        union {
            void *object;
            int32_t (*function)(void *, uint32_t, uint64_t,
                                const uint8_t *, uint32_t);
        } conversion;
        conversion.function = native_call;
        engine->native_symbols[1].func_ptr = conversion.object;
    }
    engine->native_symbols[1].signature = "(iI*~)i";
    engine->native_symbols[1].attachment = NULL;
    memset(&arguments, 0, sizeof(arguments));
    if (engine->allocate_runtime != NULL) {
        union {
            void *object;
            void *(*function)(
#if WASM_MEM_ALLOC_WITH_USAGE != 0
                mem_alloc_usage_t,
#endif
#if WASM_MEM_ALLOC_WITH_USER_DATA != 0
                void *,
#endif
                unsigned int);
        } allocate_conversion;
        union {
            void *object;
            void *(*function)(
#if WASM_MEM_ALLOC_WITH_USAGE != 0
                mem_alloc_usage_t, bool,
#endif
#if WASM_MEM_ALLOC_WITH_USER_DATA != 0
                void *,
#endif
                void *, unsigned int);
        } reallocate_conversion;
        union {
            void *object;
            void (*function)(
#if WASM_MEM_ALLOC_WITH_USAGE != 0
                mem_alloc_usage_t,
#endif
#if WASM_MEM_ALLOC_WITH_USER_DATA != 0
                void *,
#endif
                void *);
        } release_conversion;
        if (runtime_allocator_engine != NULL) return PXA_STATUS_UNAVAILABLE;
        runtime_allocator_engine = engine;
        allocate_conversion.function = dynamic_runtime_allocate;
        reallocate_conversion.function = dynamic_runtime_reallocate;
        release_conversion.function = dynamic_runtime_release;
        arguments.mem_alloc_type = Alloc_With_Allocator;
        arguments.mem_alloc_option.allocator.malloc_func =
            allocate_conversion.object;
        arguments.mem_alloc_option.allocator.realloc_func =
            reallocate_conversion.object;
        arguments.mem_alloc_option.allocator.free_func = release_conversion.object;
#if WASM_MEM_ALLOC_WITH_USER_DATA != 0
        arguments.mem_alloc_option.allocator.user_data = engine;
#endif
    } else {
        arguments.mem_alloc_type = Alloc_With_Pool;
        arguments.mem_alloc_option.pool.heap_buf = engine->pool;
        arguments.mem_alloc_option.pool.heap_size = (uint32_t)pool_bytes;
    }
    arguments.native_module_name = "pxa.core.v2";
    arguments.native_symbols = engine->native_symbols;
    arguments.n_native_symbols = sizeof(engine->native_symbols) / sizeof(engine->native_symbols[0]);
    if (!wasm_runtime_full_init(&arguments)) {
        if (runtime_allocator_engine == engine) runtime_allocator_engine = NULL;
        return PXA_STATUS_INTERNAL;
    }
    for (index = 0; index < engine->max_components; ++index) {
        engine->entries[index].component = PXA_COMPONENT_INVALID;
        engine->entries[index].wasi_null_fd = -1;
        if (engine->allocate_artifact == NULL) {
            engine->entries[index].module_bytes =
                cursor + (size_t)index * module_bytes;
            engine->entries[index].module_capacity = module_bytes;
        }
    }
    engine_ops = engine_output;
    engine_ops->struct_size = sizeof(*engine_ops);
    engine_ops->context = engine;
    engine_ops->instantiate = engine_instantiate;
    engine_ops->start = engine_start;
    engine_ops->stop = engine_stop;
    engine_ops->destroy = engine_destroy;
    *output = engine;
    return PXA_STATUS_OK;
}

void pxa_wamr_engine_set_runtime(pxa_wamr_engine_t *engine,
                                 pxa_runtime_t *runtime) {
    if (engine == NULL || engine->magic != PXA_WAMR_ENGINE_MAGIC) return;
    engine->runtime = runtime;
}

pxa_status_t
pxa_wamr_engine_memory_snapshot(const pxa_wamr_engine_t *engine,
                                pxa_wamr_memory_snapshot_t *output) {
    mem_alloc_info_t memory = {0};
    if (engine == NULL || engine->magic != PXA_WAMR_ENGINE_MAGIC ||
        output == NULL) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    memset(output, 0, sizeof(*output));
    output->linear_current_bytes = engine->linear_current_bytes;
    output->linear_peak_bytes = engine->linear_peak_bytes;
    for (uint16_t i = 0; i < engine->max_components; ++i) {
        if (engine->entries[i].module_bytes != NULL)
            output->artifact_buffer_bytes += engine->entries[i].module_capacity;
        output->event_buffer_bytes += engine->entries[i].event_capacity;
    }
    if (engine->allocate_runtime != NULL) {
        output->current_bytes =
            engine->runtime_current_bytes > UINT32_MAX
                ? UINT32_MAX
                : (uint32_t)engine->runtime_current_bytes;
        output->peak_bytes = engine->runtime_peak_bytes > UINT32_MAX
                                 ? UINT32_MAX
                                 : (uint32_t)engine->runtime_peak_bytes;
        return PXA_STATUS_OK;
    }
    if (!wasm_runtime_get_mem_alloc_info(&memory))
        return PXA_STATUS_UNSUPPORTED;
    if (memory.total_free_size > memory.total_size) return PXA_STATUS_INTERNAL;
    output->total_bytes = memory.total_size;
    output->current_bytes = memory.total_size - memory.total_free_size;
    output->peak_bytes = memory.highmark_size;
    return PXA_STATUS_OK;
}

pxa_status_t pxa_wamr_engine_set_config(pxa_wamr_engine_t *engine,
                                        uint64_t instance_id,
                                        pxa_bytes_t config) {
    pxa_wamr_entry_t *entry;
    if (engine == NULL || engine->magic != PXA_WAMR_ENGINE_MAGIC ||
        (config.data == NULL && config.size != 0) ||
        config.size > PXA_WAMR_ENGINE_MAX_CONFIG_BYTES) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    entry = find_by_instance(engine, instance_id);
    if (entry == NULL) {
        entry = reserve_instance_entry(engine, instance_id);
        if (entry == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    }
    if (config.size != 0) {
        memcpy(entry->config, config.data, config.size);
        entry->config_size = (uint16_t)config.size;
        entry->has_config = 1;
    } else {
        entry->config_size = 0;
        entry->has_config = 0;
    }
    return PXA_STATUS_OK;
}

int pxa_wamr_engine_busy(const pxa_wamr_engine_t *engine) {
    int busy;
    if (engine == NULL || engine->magic != PXA_WAMR_ENGINE_MAGIC) return 0;
    engine_enter_critical(engine);
    busy = engine->busy != 0;
    engine_leave_critical(engine);
    return busy;
}

uint32_t pxa_wamr_engine_execution_models(const pxa_wamr_engine_t *engine) {
    return engine && engine->magic == PXA_WAMR_ENGINE_MAGIC && engine->cooperate && engine->now_us
        ? PXA_EXECUTION_MODEL_MAIN_LOOP : 0;
}

int pxa_wamr_engine_poll_deadlines(pxa_wamr_engine_t *engine, uint64_t now_us) {
    int terminated = 0;
    if (engine == NULL || engine->magic != PXA_WAMR_ENGINE_MAGIC) return 0;
    engine_enter_critical(engine);
    if (engine->executing_module != NULL &&
        engine->executing_deadline_us != 0 &&
        now_us >= engine->executing_deadline_us) {
        engine->executing_deadline_us = 0;
        engine->terminate_requested = 1;
        wasm_runtime_terminate(engine->executing_module);
        terminated = 1;
    }
    engine_leave_critical(engine);
    return terminated;
}

int pxa_wamr_engine_take_expired_deadline(pxa_wamr_engine_t *engine,
                                          uint64_t now_us) {
    int expired = 0;
    if (engine == NULL || engine->magic != PXA_WAMR_ENGINE_MAGIC) return 0;
    engine_enter_critical(engine);
    if (engine->executing_module != NULL &&
        engine->executing_deadline_us != 0 &&
        now_us >= engine->executing_deadline_us) {
        engine->executing_deadline_us = 0;
        expired = 1;
    }
    engine_leave_critical(engine);
    return expired;
}

int pxa_wamr_engine_poll_component_deadline(pxa_wamr_engine_t *engine,
                                           pxa_component_t component,
                                           uint64_t now_us) {
    int terminated = 0;
    if (!engine || engine->magic != PXA_WAMR_ENGINE_MAGIC ||
        component == PXA_COMPONENT_INVALID) return 0;
    engine_enter_critical(engine);
    if (engine->executing_module && engine->executing_deadline_us &&
        now_us >= engine->executing_deadline_us) {
        for (uint16_t i = 0; i < engine->max_components; ++i) {
            pxa_wamr_entry_t *entry = &engine->entries[i];
            if (entry->component == component && entry->module_instance == engine->executing_module) {
                engine->executing_deadline_us = 0;
                engine->terminate_requested = 1;
                wasm_runtime_terminate(engine->executing_module);
                terminated = 1; break;
            }
        }
    }
    engine_leave_critical(engine);
    return terminated;
}

int pxa_wamr_engine_extend_active_deadline(pxa_wamr_engine_t *engine,
                                           uint64_t now_us) {
    int extended = 0;
    if (engine == NULL || engine->magic != PXA_WAMR_ENGINE_MAGIC ||
        now_us > UINT64_MAX - engine->call_timeout_us)
        return 0;
    engine_enter_critical(engine);
    if (engine->executing_module != NULL && engine->call_timeout_us != 0 &&
        engine->executing_deadline_us == 0) {
        engine->executing_deadline_us = now_us + engine->call_timeout_us;
        extended = 1;
    }
    engine_leave_critical(engine);
    return extended;
}

int pxa_wamr_engine_terminate_active_call(pxa_wamr_engine_t *engine) {
    int terminated = 0;
    if (engine == NULL || engine->magic != PXA_WAMR_ENGINE_MAGIC) return 0;
    engine_enter_critical(engine);
    if (engine->executing_module != NULL) {
        engine->executing_deadline_us = 0;
        engine->terminate_requested = 1;
        wasm_runtime_terminate(engine->executing_module);
        terminated = 1;
    }
    engine_leave_critical(engine);
    return terminated;
}

static pxa_status_t engine_instantiate(void *context, pxa_bytes_t package_root,
                                       const pxa_activation_entry_t *entry,
                                       pxa_component_t component,
                                       uint64_t instance_id) {
    pxa_wamr_engine_t *engine = (pxa_wamr_engine_t *)context;
    char path[PXA_WAMR_MAX_MODULE_PATH + 256];
    pxa_wamr_entry_t *slot = NULL;
    char error[192] = {0};
    pxa_status_t status;
    uint64_t wasi_features = 0;
    int wasi_declared;
    uint16_t index;
    if (engine == NULL || engine->magic != PXA_WAMR_ENGINE_MAGIC ||
        entry == NULL || entry->component == NULL || entry->artifact == NULL ||
        package_root.data == NULL || entry->artifact->path.size == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (find_by_component(engine, component) != NULL ||
        find_by_instance(engine, instance_id) != NULL) {
        return PXA_STATUS_BUSY;
    }
    if (package_root.size + entry->artifact->path.size + 2 > sizeof(path)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    memcpy(path, package_root.data, package_root.size);
    path[package_root.size] = '/';
    memcpy(path + package_root.size + 1, entry->artifact->path.data,
           entry->artifact->path.size);
    path[package_root.size + 1 + entry->artifact->path.size] = '\0';
    slot = find_pending_by_instance(engine, instance_id);
    for (index = 0; slot == NULL && index < engine->max_components; ++index) {
        if (!engine->entries[index].occupied &&
            !engine->entries[index].pending) {
            slot = &engine->entries[index];
            break;
        }
    }
    if (slot == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    slot->package_component = entry->component;
    slot->core_major = entry->core_major;
    slot->core_minor = entry->core_minor;
    if (slot->core_major != 2)
        return discard_entry(engine, slot, PXA_STATUS_UNSUPPORTED);
    if ((entry->component->flags & PXA_PACKAGE_COMPONENT_FLAG_MAIN_LOOP) &&
        (!main_loop_enabled(slot) || !engine->cooperate ||
         entry->component->kind != PXA_COMPONENT_KIND_UI))
        return discard_entry(engine, slot, PXA_STATUS_UNSUPPORTED);
    status = load_artifact(engine, slot,
        (pxa_bytes_t){(const uint8_t *)path, strlen(path)},
        entry->artifact->kind, error, sizeof(error));
    if (status != PXA_STATUS_OK) {
        log_runtime_failure("load", path, error);
        return discard_entry(engine, slot, status);
    }
    wasi_declared = component_wasi_features(entry->component, &wasi_features);
    if (wasi_declared && !engine->wasi_enabled) {
        return discard_entry(engine, slot, PXA_STATUS_UNSUPPORTED);
    }
    status = validate_module_imports(slot->module, path, slot->core_major,
                                     entry->core_minor,
                                     wasi_declared,
                                     wasi_features);
    if (status != PXA_STATUS_OK) {
        return discard_entry(engine, slot, status);
    }
    if (wasi_declared) {
        /* No preopens, argv or environment are ambient. Resource-bearing
         * imports are rejected above unless their signed feature is present. */
#if PXA_WAMR_LIBC_WASI
#if defined(ESP_PLATFORM)
        slot->wasi_null_fd = pxa_esp_wasi_null_fd();
#else
        slot->wasi_null_fd = open("/dev/null", O_RDWR);
#endif
        if (slot->wasi_null_fd < 0) {
            return discard_entry(engine, slot, PXA_STATUS_UNAVAILABLE);
        }
        wasm_runtime_set_wasi_args_ex(slot->module, NULL, 0, NULL, 0, NULL, 0,
                                      NULL, 0, slot->wasi_null_fd,
                                      slot->wasi_null_fd, slot->wasi_null_fd);
#endif
    }
#if defined(WASM_LINEAR_MEMORY_RESERVE_MAX) && \
    WASM_LINEAR_MEMORY_RESERVE_MAX != 0
    /* AF07 selects this policy for the next instantiation. Package metadata is
     * signed, unlike WebAssembly module contents supplied after installation. */
    wasm_runtime_set_linear_memory_reserve_max(
        (slot->package_component->flags &
         PXA_PACKAGE_COMPONENT_FLAG_PINNED_MEMORY) != 0);
#endif
#ifdef ESP_PLATFORM
    pxa_wamr_load_memory_ready();
#endif
    slot->module_instance = wasm_runtime_instantiate(
        slot->module, engine->guest_stack_size, engine->host_managed_heap_size,
        error, sizeof(error));
#ifdef ESP_PLATFORM
    pxa_wamr_load_memory_end();
#endif
#if defined(WASM_LINEAR_MEMORY_RESERVE_MAX) && \
    WASM_LINEAR_MEMORY_RESERVE_MAX != 0
    wasm_runtime_set_linear_memory_reserve_max(false);
#endif
    if (slot->module_instance == NULL) {
        log_runtime_failure("instantiate", path, error);
        return discard_entry(engine, slot, PXA_STATUS_RESOURCE_LIMIT);
    }
    sample_linear_memory(engine, slot);
    slot->exec_env = wasm_runtime_create_exec_env(slot->module_instance,
                                                  engine->guest_stack_size);
    if (slot->exec_env == NULL) {
        log_runtime_failure("create-exec-env", path, NULL);
        return discard_entry(engine, slot, PXA_STATUS_RESOURCE_LIMIT);
    }
    wasm_runtime_set_custom_data(slot->module_instance, slot);
    /* Cache the guest entry points once: avoids repeated lookups and keeps
     * behavior stable with several modules loaded. */
    slot->start_fn =
        wasm_runtime_lookup_function(slot->module_instance, "pxa_app_start");
    slot->event_fn =
        wasm_runtime_lookup_function(slot->module_instance, "pxa_app_on_event");
    slot->stop_fn =
        wasm_runtime_lookup_function(slot->module_instance, "pxa_app_stop");
    status = pxa_component_set_wire_profile(engine->runtime, component,
                                          PXA_WIRE_PROFILE_HANDLE64);
    if (status != PXA_STATUS_OK)
        return discard_entry(engine, slot, status);
    slot->pending = 0;
    slot->pending_instance_id = 0;
    slot->engine = engine;
    slot->instance_id = instance_id;
    slot->component = component;
    slot->kind = entry->component == NULL ? 0 : entry->component->kind;
    slot->occupied = 1;
    return PXA_STATUS_OK;
}

static pxa_status_t engine_start(void *context, pxa_component_t component) {
    pxa_wamr_engine_t *engine = (pxa_wamr_engine_t *)context;
    pxa_wamr_entry_t *entry;
    wasm_function_inst_t fn;
    uint32_t values[2];
    void *native = NULL;
    uint32_t offset = 0;
    int32_t status;
    if (engine == NULL || engine->magic != PXA_WAMR_ENGINE_MAGIC) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    entry = find_by_component(engine, component);
    if (entry == NULL) return PXA_STATUS_NOT_FOUND;
    if (engine->prepare_start != NULL) {
        pxa_status_t prepared = engine->prepare_start(
            engine->host_context, component, entry->instance_id, entry->kind);
        if (prepared != PXA_STATUS_OK) {
            log_runtime_failure("prepare-start", NULL, NULL);
            return prepared;
        }
    }
    fn = entry->start_fn;
    if (!has_signature(entry->module_instance, fn, 2, 1)) {
        log_runtime_failure("validate-start-export", NULL, NULL);
        return PXA_STATUS_UNSUPPORTED;
    }
    if (entry->has_config) {
        offset = wasm_runtime_module_malloc(entry->module_instance,
                                            entry->config_size, &native);
        if (offset == 0 || native == NULL) return PXA_STATUS_RESOURCE_LIMIT;
        memcpy(native, entry->config, entry->config_size);
    }
    values[0] = offset;
    values[1] = entry->config_size;
    if (!call(engine, entry, fn, 2, values)) {
        log_runtime_failure("call-start", NULL,
                            wasm_runtime_get_exception(entry->module_instance));
        if (offset != 0) {
            wasm_runtime_module_free(entry->module_instance, offset);
        }
        return PXA_STATUS_INTERNAL;
    }
    if (offset != 0) {
        wasm_runtime_module_free(entry->module_instance, offset);
    }
    status = (int32_t)values[0];
    if (status < 0) log_runtime_failure("guest-start", NULL, NULL);
    return pxa_status_is_known(status) ? (pxa_status_t)status
                                       : PXA_STATUS_INTERNAL;
}

static void engine_stop(void *context, pxa_component_t component,
                        pxa_stop_reason_t reason) {
    pxa_wamr_engine_t *engine = (pxa_wamr_engine_t *)context;
    pxa_wamr_entry_t *entry;
    wasm_function_inst_t fn;
    uint32_t values[1];
    if (engine == NULL || engine->magic != PXA_WAMR_ENGINE_MAGIC) {
        return;
    }
    entry = find_by_component(engine, component);
    if (entry == NULL) return;
    fn = entry->stop_fn;
    if (!has_signature(entry->module_instance, fn, 1, 0)) return;
    /* A cooperative unwind leaves a deliberate exception in WAMR. Clear only
     * that known cancellation before entering the separate stop callback. */
    if (entry->cooperative_cancelled)
        wasm_runtime_clear_exception(entry->module_instance);
    values[0] = (uint32_t)reason;
    (void)call(engine, entry, fn, 1, values);
}

static void engine_destroy(void *context, pxa_component_t component) {
    pxa_wamr_engine_t *engine = (pxa_wamr_engine_t *)context;
    pxa_wamr_entry_t *entry;
    if (engine == NULL || engine->magic != PXA_WAMR_ENGINE_MAGIC) {
        return;
    }
    entry = find_by_component(engine, component);
    if (entry == NULL) return;
    (void)discard_entry(engine, entry, PXA_STATUS_OK);
}

pxa_status_t pxa_wamr_engine_deliver_event_result(
    pxa_wamr_engine_t *engine, pxa_runtime_t *runtime,
    pxa_component_t component, pxa_wamr_event_result_t *output) {
    pxa_wamr_entry_t *entry;
    pxa_event_view_t view;
    pxa_message_view_t decoded;
    wasm_function_inst_t fn;
    uint32_t values[2];
    void *native = NULL;
    uint32_t offset;
    size_t popped = 0;
    size_t callback_size;
    int32_t result;
    pxa_status_t status;
    if (output != NULL) memset(output, 0, sizeof(*output));
    if (engine == NULL || engine->magic != PXA_WAMR_ENGINE_MAGIC) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    /* A Host widget callback can enqueue another event inside a Guest import.
     * Leave that event untouched until the owner returns to its dispatch loop;
     * popping first would turn harmless reentrancy into a Component fault. */
    if (engine->busy) return PXA_STATUS_WOULD_BLOCK;
    entry = find_by_component(engine, component);
    if (entry == NULL) return PXA_STATUS_NOT_FOUND;

    status = pxa_event_peek(runtime, component, &view);
    if (status != PXA_STATUS_OK) return status;
    if (view.size == 0 ||
        view.size > PXA_MAX_CONTROL_MESSAGE) {
        return PXA_STATUS_INTERNAL;
    }
    /* Runtime and Guest share the envelope: copy once into Guest memory.
     * Retain the existing geometric buffer growth, capped at the wire limit. */
    callback_size = view.size;
    if (callback_size > entry->event_capacity) {
        uint32_t capacity = 64;
        while (capacity < callback_size)
            capacity *= 2u;
        if (entry->event_buffer != 0)
            wasm_runtime_module_free(entry->module_instance, entry->event_buffer);
        entry->event_capacity = 0;
        entry->event_buffer = wasm_runtime_module_malloc(
            entry->module_instance, capacity, &native);
        if (entry->event_buffer == 0) return PXA_STATUS_RESOURCE_LIMIT;
        entry->event_capacity = capacity;
    }
    offset = entry->event_buffer;
    /* memory.grow can relocate linear memory between callbacks. */
    native = wasm_runtime_addr_app_to_native(entry->module_instance, offset);
    if (native == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    status = pxa_event_pop(runtime, component, native, view.size, &popped);
    if (status != PXA_STATUS_OK || popped != view.size) {
        return status == PXA_STATUS_OK ? PXA_STATUS_INTERNAL : status;
    }
    if (output != NULL) output->event_consumed = 1;
    status = pxa_message_decode((const uint8_t *)native, popped,
                                PXA_MAX_CONTROL_MESSAGE, &decoded);
    if (status != PXA_STATUS_OK) {
        return status;
    }
    if (output != NULL) {
        output->service = decoded.service;
        output->opcode = decoded.opcode;
        output->request_token = decoded.request_token;
        output->payload_size = decoded.payload.size;
    }
    status = pxa_component_begin_event(runtime, component);
    if (status != PXA_STATUS_OK) {
        return status;
    }
    fn = entry->event_fn;
    result = (int32_t)PXA_STATUS_INTERNAL;
    if (has_signature(entry->module_instance, fn, 2, 1)) {
        values[0] = offset;
        values[1] = (uint32_t)callback_size;
        if (call(engine, entry, fn, 2, values)) {
            result = (int32_t)values[0];
        } else if (entry->cooperative_cancelled) {
            result = 0;
            if (output != NULL) output->cooperative_cancelled = 1;
        }
    }

    status = pxa_component_finish_event(runtime, component, result);
    if (status != PXA_STATUS_OK) return status;
    if (output != NULL) output->guest_result = result;
    /* finish_event records a negative guest result as STOP_REQUESTED, but the
     * host also needs that result to tear down the activated component. */
    return result < 0 ? (pxa_status_t)result : PXA_STATUS_OK;
}

pxa_status_t pxa_wamr_engine_deliver_event(pxa_wamr_engine_t *engine,
                                           pxa_runtime_t *runtime,
                                           pxa_component_t component) {
    return pxa_wamr_engine_deliver_event_result(engine, runtime, component,
                                                NULL);
}

void pxa_wamr_engine_deinit(pxa_wamr_engine_t *engine) {
    uint16_t index;
    if (engine == NULL || engine->magic != PXA_WAMR_ENGINE_MAGIC) return;
    for (index = 0; index < engine->max_components; ++index) {
        if (engine->entries[index].occupied) {
            engine_destroy(engine, engine->entries[index].component);
        }
    }
    (void)wasm_runtime_unregister_natives("pxa.core.v2", engine->native_symbols);
    wasm_runtime_destroy();
    if (runtime_allocator_engine == engine) runtime_allocator_engine = NULL;
    engine->magic = 0;
}
