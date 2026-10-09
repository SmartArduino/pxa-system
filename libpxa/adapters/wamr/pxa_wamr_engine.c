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
#include "pxa/wire_v1.h"
#include "wasm_export.h"

#if defined(ESP_PLATFORM)
#include "esp_log.h"
#include "pxa/esp/pxa_esp_posix_shim.h"
#define PXA_WAMR_LOG_TAG "PxaWamr"
#endif

#define PXA_WAMR_ENGINE_MAGIC UINT32_C(0x50574d52)
#define PXA_WAMR_ENGINE_ALIGNMENT ((size_t)16)
#define PXA_WAMR_DEFAULT_STACK_HEAP ((uint32_t)16384)
#define PXA_WAMR_DEFAULT_BYTES ((size_t)(1u << 20))
#define PXA_WAMR_MAX_MODULE_PATH ((size_t)512)
#define PXA_WAMR_V1_PENDING_REQUESTS 16u
#define PXA_WAMR_V1_MAX_PERMISSION_PAYLOAD (8u + 96u + 1024u)
#define PXA_WAMR_V1_MAX_STORAGE_PAYLOAD \
    (8u + PXA_STORAGE_MAX_KEY_BYTES + PXA_STORAGE_MAX_VALUE_BYTES)
#define PXA_WAMR_V1_MAX_PAYLOAD \
    (PXA_MAX_CONTROL_MESSAGE - PXA_V1_ENVELOPE_SIZE)
#ifndef PXA_WAMR_LIBC_WASI
#define PXA_WAMR_LIBC_WASI 0
#endif

typedef struct {
    uint64_t token;
    uint32_t host_id;
    uint16_t service;
    uint16_t opcode;
} pxa_wamr_v1_request_t;

typedef struct {
    struct pxa_wamr_engine *engine;
    uint64_t instance_id;
    uint64_t pending_instance_id;
    uint8_t pending;
    pxa_component_t component;
    const pxa_package_component_t *package_component;
    uint16_t core_major;
    uint32_t next_v1_host_id;
    pxa_wamr_v1_request_t v1_requests[PXA_WAMR_V1_PENDING_REQUESTS];
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
    pxa_status_t (*read_artifact)(void *context, pxa_bytes_t path,
                                  uint8_t *output, size_t capacity,
                                  size_t *size);
    uint64_t (*now_us)(void *context);
    pxa_status_t (*prepare_start)(void *context, pxa_component_t component,
                                  uint64_t instance_id, uint8_t kind);
    uint64_t call_timeout_us;
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
    uint8_t wasi_enabled;
    uint8_t *pool;
    pxa_wamr_entry_t entries[];
};

/* WAMR owns one process-global allocator. The adapter likewise supports one
 * live engine per process and keeps this pointer valid through runtime destroy. */
static pxa_wamr_engine_t *runtime_allocator_engine;
static NativeSymbol v1_symbols[2];

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
    pxa_status_t status = engine->read_artifact(engine->host_context, path,
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
        engine->read_artifact(engine->host_context, path, entry->module_bytes,
                              entry->module_capacity, &loaded_size);
    if (status != PXA_STATUS_OK || loaded_size != expected_size) {
        release_module_buffer(engine, entry);
        return status == PXA_STATUS_OK ? PXA_STATUS_INTERNAL : status;
    }
    entry->module_size = loaded_size;
    return PXA_STATUS_OK;
}

static pxa_status_t discard_entry(pxa_wamr_engine_t *engine,
                                  pxa_wamr_entry_t *entry,
                                  pxa_status_t status) {
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
    release_module_buffer(engine, entry);
    entry->config_size = 0;
    entry->has_config = 0;
    entry->pending_instance_id = 0;
    entry->pending = 0;
    entry->instance_id = 0;
    entry->component = PXA_COMPONENT_INVALID;
    entry->package_component = NULL;
    entry->core_major = 0;
    entry->next_v1_host_id = 0;
    memset(entry->v1_requests, 0, sizeof(entry->v1_requests));
    entry->kind = 0;
    entry->start_fn = NULL;
    entry->event_fn = NULL;
    entry->stop_fn = NULL;
    entry->occupied = 0;
    return status;
}

static pxa_status_t validate_module_imports(wasm_module_t module,
                                            const char *path, uint16_t core_major,
                                            int wasi_declared,
                                            uint64_t wasi_features) {
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
        if (strcmp(imported.module_name, "pxa.core.v1") == 0) {
            if (core_major != 1 ||
                (strcmp(imported.name, "pxa_submit") != 0 &&
                 strcmp(imported.name, "pxa_io") != 0)) {
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

static pxa_status_t v1_request_reserve(pxa_wamr_entry_t *entry,
                                       uint64_t token, uint16_t service,
                                       uint16_t opcode, uint32_t *host_id) {
    pxa_wamr_v1_request_t *free_slot = NULL;
    unsigned index;
    unsigned attempt;
    if (token == 0 || host_id == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    for (index = 0; index < PXA_WAMR_V1_PENDING_REQUESTS; ++index) {
        pxa_wamr_v1_request_t *slot = &entry->v1_requests[index];
        if (slot->host_id == 0) free_slot = slot;
        else if (slot->token == token) return PXA_STATUS_BUSY;
    }
    if (free_slot == NULL) return PXA_STATUS_RESOURCE_LIMIT;
    for (attempt = 0; attempt <= PXA_WAMR_V1_PENDING_REQUESTS; ++attempt) {
        uint32_t candidate = ++entry->next_v1_host_id;
        if (candidate == 0) candidate = ++entry->next_v1_host_id;
        for (index = 0; index < PXA_WAMR_V1_PENDING_REQUESTS; ++index) {
            if (entry->v1_requests[index].host_id == candidate) break;
        }
        if (index == PXA_WAMR_V1_PENDING_REQUESTS) {
            free_slot->token = token;
            free_slot->host_id = candidate;
            free_slot->service = service;
            free_slot->opcode = opcode;
            *host_id = candidate;
            return PXA_STATUS_OK;
        }
    }
    return PXA_STATUS_RESOURCE_LIMIT;
}

static pxa_wamr_v1_request_t *v1_request_find(pxa_wamr_entry_t *entry,
                                               uint32_t host_id) {
    unsigned index;
    for (index = 0; index < PXA_WAMR_V1_PENDING_REQUESTS; ++index) {
        if (entry->v1_requests[index].host_id == host_id)
            return &entry->v1_requests[index];
    }
    return NULL;
}

static pxa_wamr_v1_request_t *v1_request_find_token(
    pxa_wamr_entry_t *entry, uint64_t token) {
    unsigned index;
    for (index = 0; index < PXA_WAMR_V1_PENDING_REQUESTS; ++index) {
        if (entry->v1_requests[index].host_id != 0 &&
            entry->v1_requests[index].token == token)
            return &entry->v1_requests[index];
    }
    return NULL;
}

/* v1 admits only the migrated operations of these services. */
static int32_t native_submit_v1(void *opaque_exec_env, const uint8_t *data,
                                uint32_t size) {
    wasm_exec_env_t exec_env = (wasm_exec_env_t)opaque_exec_env;
    wasm_module_inst_t module = wasm_runtime_get_module_inst(exec_env);
    pxa_wamr_entry_t *entry =
        (pxa_wamr_entry_t *)wasm_runtime_get_custom_data(module);
    pxa_v1_message_view_t message;
    uint32_t host_id = 0;
    pxa_status_t status;
    if (entry == NULL || entry->core_major != 1 ||
        entry_engine(entry)->runtime == NULL)
        return PXA_STATUS_BAD_STATE;
    if (pxa_v1_message_decode(data, size,
            PXA_V1_ENVELOPE_SIZE + PXA_WAMR_V1_MAX_PAYLOAD,
            &message) != PXA_STATUS_OK)
        return PXA_STATUS_INVALID_ARGUMENT;
    if (message.service == PXA_SERVICE_CORE &&
        message.opcode == PXA_CORE_CANCEL_REQUEST) {
        pxa_wamr_v1_request_t *target;
        uint64_t token;
        if (message.request_token != 0 || message.payload.size != 8)
            return PXA_STATUS_INVALID_ARGUMENT;
        token = pxa_read_u64(message.payload.data);
        if (token == 0) return PXA_STATUS_INVALID_ARGUMENT;
        target = v1_request_find_token(entry, token);
        if (target == NULL) return PXA_STATUS_OK;
        return pxa_request_cancel(entry_engine(entry)->runtime,
                                  entry->component, target->host_id);
    }
    if (message.service == PXA_SERVICE_CORE &&
        message.opcode == PXA_CORE_CLOSE_HANDLE &&
        message.request_token == 0 && message.payload.size == 8) {
        return pxa_handle_close64(entry_engine(entry)->runtime,
                                  entry->component,
                                  pxa_read_u64(message.payload.data));
    }
    if (message.service == PXA_SERVICE_CORE &&
        message.opcode == PXA_LEASE_ACQUIRE &&
        message.request_token != 0 &&
        (message.payload.size == 6u || message.payload.size == 14u)) {
        status = v1_request_reserve(entry, message.request_token,
                                    message.service, message.opcode, &host_id);
        if (status != PXA_STATUS_OK) return status;
    } else if (message.service == PXA_LOG_SERVICE_ID &&
        message.opcode == PXA_LOG_WRITE && message.request_token == 0) {
        /* The Log payload is validated by the existing Service. */
    } else if (message.service == PXA_CLOCK_SERVICE_ID &&
               message.opcode == PXA_CLOCK_SET_PERIOD &&
               message.request_token == 0 && message.payload.size == 2) {
        /* The Host validates the requested period. */
    } else if (message.service == PXA_CLOCK_SERVICE_ID &&
               message.opcode == PXA_CLOCK_NOW &&
               message.request_token != 0 && message.payload.size == 0) {
        status = v1_request_reserve(entry, message.request_token,
                                    message.service, message.opcode, &host_id);
        if (status != PXA_STATUS_OK) return status;
    } else if (message.service == PXA_UI_SERVICE_ID &&
               message.opcode == PXA_UI_THEME_GET &&
               message.request_token != 0 && message.payload.size == 0) {
        status = v1_request_reserve(entry, message.request_token,
                                    message.service, message.opcode, &host_id);
        if (status != PXA_STATUS_OK) return status;
    } else if (message.service == PXA_UI_SERVICE_ID &&
               message.request_token == 0 &&
               ((message.opcode == PXA_UI_TX_BEGIN &&
                 message.payload.size == 20u) ||
                (message.opcode == PXA_UI_TX_WRITE &&
                 message.payload.size > 4u) ||
                ((message.opcode == PXA_UI_TX_COMMIT ||
                  message.opcode == PXA_UI_TX_CANCEL ||
                  message.opcode == PXA_UI_SURFACE_CLOSE) &&
                 message.payload.size == 4u) ||
                (message.opcode == PXA_UI_SURFACE_OPEN &&
                 message.payload.size == 8u) ||
                (message.opcode == PXA_UI_CANVAS_BEGIN &&
                 message.payload.size == 16u) ||
                (message.opcode == PXA_UI_CANVAS_WRITE &&
                 message.payload.size > 12u) ||
                (message.opcode == PXA_UI_CANVAS_PRESENT &&
                 message.payload.size >= 13u &&
                 message.payload.size <=
                     13u + PXA_UI_MAX_DIRTY_RECTS * 16u) ||
                (message.opcode == PXA_UI_CANVAS_STREAM_OPEN &&
                 message.payload.size == 12u))) {
        /* The UI service validates transaction state, commands and bounds. */
    } else if (message.service == PXA_DEVICE_SERVICE_ID &&
               message.opcode == PXA_DEVICE_GET_RUNTIME_INFO &&
               message.request_token != 0 && message.payload.size == 0) {
        status = v1_request_reserve(entry, message.request_token,
                                    message.service, message.opcode, &host_id);
        if (status != PXA_STATUS_OK) return status;
    } else if (message.service == PXA_DEVICE_SERVICE_ID &&
               message.opcode == PXA_DEVICE_GET_MAC &&
               message.request_token != 0 && message.payload.size == 18) {
        status = v1_request_reserve(entry, message.request_token,
                                    message.service, message.opcode, &host_id);
        if (status != PXA_STATUS_OK) return status;
    } else if (message.service == PXA_WINDOW_SERVICE_ID &&
               message.opcode == PXA_WINDOW_GET_SNAPSHOT &&
               message.request_token != 0 && message.payload.size == 0) {
        status = v1_request_reserve(entry, message.request_token,
                                    message.service, message.opcode, &host_id);
        if (status != PXA_STATUS_OK) return status;
    } else if (message.service == PXA_WINDOW_SERVICE_ID &&
               message.opcode == PXA_WINDOW_CONFIGURE &&
               message.request_token == 0 &&
               message.payload.size <= 41u) {
        /* Existing Window parser validates the individual records. */
    } else if (message.service == PXA_WINDOW_SERVICE_ID &&
               message.opcode == PXA_WINDOW_SHOW_TOAST &&
               message.request_token == 0 &&
               message.payload.size >= 3u &&
               message.payload.size <= PXA_WINDOW_TOAST_MAX_BYTES + 2u) {
        /* Existing Window parser validates duration and UTF-8. */
    } else if (message.service == PXA_PERMISSION_SERVICE_ID &&
               (message.opcode == PXA_PERMISSION_CHECK ||
                message.opcode == PXA_PERMISSION_ACQUIRE) &&
               message.request_token != 0 &&
               message.payload.size >= 5 &&
               message.payload.size <= PXA_WAMR_V1_MAX_PERMISSION_PAYLOAD) {
        status = v1_request_reserve(entry, message.request_token,
                                    message.service, message.opcode, &host_id);
        if (status != PXA_STATUS_OK) return status;
    } else if (message.service == PXA_STORAGE_SERVICE_ID &&
               message.request_token != 0 &&
               (((message.opcode == PXA_STORAGE_GET ||
                  message.opcode == PXA_STORAGE_REMOVE) &&
                    message.payload.size >= 5u &&
                    message.payload.size <= 4u + PXA_STORAGE_MAX_KEY_BYTES) ||
                (message.opcode == PXA_STORAGE_LIST &&
                    message.payload.size <= 4u + PXA_STORAGE_MAX_KEY_BYTES) ||
                (message.opcode == PXA_STORAGE_SET &&
                    message.payload.size >= 9u &&
                    message.payload.size <= PXA_WAMR_V1_MAX_STORAGE_PAYLOAD))) {
        status = v1_request_reserve(entry, message.request_token,
                                    message.service, message.opcode, &host_id);
        if (status != PXA_STATUS_OK) return status;
    } else if (message.service == PXA_FS_SERVICE_ID &&
               message.request_token != 0 &&
               (((message.opcode == PXA_FS_MAKE_DIRECTORY ||
                  message.opcode == PXA_FS_REMOVE ||
                  message.opcode == PXA_FS_STAT) &&
                    message.payload.size >= 5u &&
                    message.payload.size <= 4u + PXA_FS_MAX_PATH_BYTES) ||
                (message.opcode == PXA_FS_OPEN &&
                    message.payload.size >= 13u &&
                    message.payload.size <= 8u + PXA_FS_MAX_PATH_BYTES) ||
                (message.opcode == PXA_FS_RENAME &&
                    message.payload.size >= 10u &&
                    message.payload.size <= 8u + 2u * PXA_FS_MAX_PATH_BYTES) ||
                (message.opcode == PXA_FS_SEEK &&
                    message.payload.size == 17u) ||
                (message.opcode == PXA_FS_READ_DIRECTORY &&
                    message.payload.size == 8u))) {
        status = v1_request_reserve(entry, message.request_token,
                                    message.service, message.opcode, &host_id);
        if (status != PXA_STATUS_OK) return status;
    } else if (message.service == PXA_IPC_SERVICE_ID &&
               message.request_token != 0 &&
               (((message.opcode == PXA_IPC_CALL) &&
                 message.payload.size >= 5u &&
                 message.payload.size <= 4u + PXA_IPC_MAX_ENDPOINT_BYTES +
                                             4u + PXA_IPC_MAX_PAYLOAD_BYTES) ||
                ((message.opcode == PXA_IPC_REPLY) &&
                 message.payload.size >= 16u &&
                 message.payload.size <= 20u + PXA_IPC_MAX_PAYLOAD_BYTES))) {
        status = v1_request_reserve(entry, message.request_token,
                                    message.service, message.opcode, &host_id);
        if (status != PXA_STATUS_OK) return status;
    } else if (message.service == PXA_NET_SERVICE_ID &&
               (message.opcode == PXA_NET_FETCH ||
                message.opcode == PXA_NET_HTTP_REQUEST) &&
               message.request_token != 0 &&
               message.payload.size >= 26u &&
               message.payload.size <= PXA_WAMR_V1_MAX_PAYLOAD) {
        status = v1_request_reserve(entry, message.request_token,
                                    message.service, message.opcode, &host_id);
        if (status != PXA_STATUS_OK) return status;
    } else if (message.service == PXA_AUDIO_SERVICE_ID &&
               message.request_token != 0 &&
               (((message.opcode == PXA_AUDIO_OPEN_SESSION) &&
                 message.payload.size == 18u) ||
                ((message.opcode == PXA_AUDIO_COMMIT_GRAPH) &&
                 message.payload.size >= 24u &&
                 message.payload.size <= 74u) ||
                ((message.opcode == PXA_AUDIO_QUERY_STATE ||
                  message.opcode == PXA_AUDIO_FLUSH) &&
                 message.payload.size == 12u))) {
        status = v1_request_reserve(entry, message.request_token,
                                    message.service, message.opcode, &host_id);
        if (status != PXA_STATUS_OK) return status;
    } else if (message.service == PXA_SENSOR_SERVICE_ID &&
               message.request_token != 0 &&
               ((message.opcode == PXA_SENSOR_LIST &&
                 message.payload.size == 0) ||
                (message.opcode == PXA_SENSOR_SUBSCRIBE &&
                 message.payload.size == 26u))) {
        status = v1_request_reserve(entry, message.request_token,
                                    message.service, message.opcode, &host_id);
        if (status != PXA_STATUS_OK) return status;
    } else if (message.service == PXA_WORK_SERVICE_ID &&
               message.request_token != 0 &&
               ((message.opcode == PXA_WORK_ENQUEUE &&
                 message.payload.size >= 34u &&
                 message.payload.size <= 125u) ||
                (message.opcode == PXA_WORK_CANCEL &&
                 message.payload.size == 8u) ||
                (message.opcode == PXA_WORK_COMPLETE &&
                 message.payload.size == 13u))) {
        status = v1_request_reserve(entry, message.request_token,
                                    message.service, message.opcode, &host_id);
        if (status != PXA_STATUS_OK) return status;
    } else if (message.service == PXA_SURFACE_SERVICE_ID &&
               message.opcode == PXA_SURFACE_QUEUE_FRAME &&
               message.request_token == 0 &&
               message.payload.size >= 20u &&
               message.payload.size <=
                   20u + PXA_SURFACE_MAX_DAMAGE_RECTS * 8u) {
        /* The Surface service validates the damage rectangles and Handle. */
    } else if (message.service == PXA_SURFACE_SERVICE_ID &&
               message.request_token != 0 &&
               ((message.opcode == PXA_SURFACE_CREATE &&
                 message.payload.size == 8u) ||
                (message.opcode == PXA_SURFACE_CONFIGURE_LAYER &&
                 message.payload.size == 24u) ||
                (message.opcode == PXA_SURFACE_QUERY_STATE &&
                 message.payload.size == 8u) ||
                (message.opcode == PXA_SURFACE_CONFIGURE_OPAQUE_UI_REGIONS &&
                 message.payload.size >= 12u &&
                 message.payload.size <=
                     12u + PXA_SURFACE_MAX_OPAQUE_UI_REGIONS * 8u))) {
        status = v1_request_reserve(entry, message.request_token,
                                    message.service, message.opcode, &host_id);
        if (status != PXA_STATUS_OK) return status;
    } else if (message.service == PXA_GAME_RENDER_SERVICE_ID &&
               (message.opcode == PXA_GAME_RENDER_CREATE_CONTEXT ||
                message.opcode == PXA_GAME_RENDER_CREATE_AUTO_CONTEXT) &&
               message.request_token != 0 &&
               (message.payload.size == 8 || message.payload.size == 12)) {
        status = v1_request_reserve(entry, message.request_token,
                                    message.service, message.opcode, &host_id);
        if (status != PXA_STATUS_OK) return status;
    } else if (message.service == PXA_ASSETS_SERVICE_ID &&
               (message.opcode == PXA_ASSETS_QUERY || message.opcode == PXA_ASSETS_LOAD ||
                message.opcode == PXA_ASSETS_PREFETCH || message.opcode == PXA_ASSETS_STATUS ||
                message.opcode == PXA_ASSETS_READ) &&
               message.request_token != 0 && message.payload.size >= 5u &&
               message.payload.size <= 4u + PXA_ASSET_PATH_MAX) {
        status = v1_request_reserve(entry, message.request_token,
                                    message.service, message.opcode, &host_id);
        if (status != PXA_STATUS_OK) return status;
    } else if (message.service == PXA_STORE_INSTALLER_SERVICE_ID &&
               message.opcode >= 1u && message.opcode <= 8u &&
               message.request_token != 0 &&
               message.payload.size <= 362u) {
        /* The registered privileged Installer validates its own payload. */
        status = v1_request_reserve(entry, message.request_token,
                                    message.service, message.opcode, &host_id);
        if (status != PXA_STATUS_OK) return status;
    } else {
        return PXA_STATUS_UNSUPPORTED;
    }
    {
        const size_t payload_size = message.payload.size;
        pxa_wamr_engine_t *engine = entry_engine(entry);
        pxa_message_view_t forwarded;
        memset(&forwarded, 0, sizeof(forwarded));
        forwarded.service = message.service;
        forwarded.opcode = message.opcode;
        forwarded.request_id = host_id;
        /* WAMR has validated the native (*~) range. Guest execution is
         * suspended and serialized until this import returns; services
         * consume the borrowed payload synchronously and copy any data
         * they need to retain for asynchronous work. */
        forwarded.payload.data = payload_size == 0 ? NULL : message.payload.data;
        forwarded.payload.size = payload_size;
        status = pxa_runtime_control_view(engine->runtime,
                                          entry->component, &forwarded);
    }
    if (status != PXA_STATUS_OK && host_id != 0) {
        pxa_wamr_v1_request_t *slot = v1_request_find(entry, host_id);
        if (slot != NULL) memset(slot, 0, sizeof(*slot));
    }
    return status;
}

static int32_t native_io_v1(void *opaque_exec_env, uint64_t handle,
                            uint32_t operation, uint8_t *data,
                            uint32_t size) {
    wasm_exec_env_t exec_env = (wasm_exec_env_t)opaque_exec_env;
    wasm_module_inst_t module = wasm_runtime_get_module_inst(exec_env);
    pxa_wamr_entry_t *entry =
        (pxa_wamr_entry_t *)wasm_runtime_get_custom_data(module);
    if (entry == NULL || entry->core_major != 1 ||
        entry_engine(entry)->runtime == NULL)
        return PXA_STATUS_BAD_STATE;
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
    return pxa_runtime_io64(entry_engine(entry)->runtime, entry->component,
                            handle, operation, data, size);
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
    engine->executing_module = entry->module_instance;
    engine->executing_deadline_us = deadline;
    engine_leave_critical(engine);
    success = wasm_runtime_call_wasm(entry->exec_env, fn, argc, values);
    sample_linear_memory(engine, entry);
    if (!success &&
        wasm_runtime_get_exception(entry->module_instance) != NULL) {
        /* Guest exceptions surface as PXA_STATUS_INTERNAL; the message is
         * available through wasm_runtime_get_exception for host logging. */
        (void)wasm_runtime_get_exception(entry->module_instance);
    }
    engine_enter_critical(engine);
    if (engine->executing_module == entry->module_instance) {
        engine->executing_module = NULL;
        engine->executing_deadline_us = 0;
        engine->busy = 0;
    }
    engine_leave_critical(engine);
    return success;
}

static int config_valid(const pxa_wamr_engine_config_t *config) {
    return config != NULL && config->struct_size >= sizeof(*config) &&
           config->read_artifact != NULL && config->max_components != 0 &&
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
    /* WAMR keeps this array for the lifetime of the runtime: it must not be
     * stack storage. One WAMR runtime per process, so static is safe. */
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
    engine->read_artifact = config->read_artifact;
    engine->now_us = config->now_us;
    engine->prepare_start = config->prepare_start;
    engine->call_timeout_us = config->call_timeout_us;
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

    v1_symbols[0].symbol = "pxa_submit";
    {
        union {
            void *object;
            int32_t (*function)(void *, const uint8_t *, uint32_t);
        } conversion;
        conversion.function = native_submit_v1;
        v1_symbols[0].func_ptr = conversion.object;
    }
    v1_symbols[0].signature = "(*~)i";
    v1_symbols[0].attachment = NULL;
    v1_symbols[1].symbol = "pxa_io";
    {
        union {
            void *object;
            int32_t (*function)(void *, uint64_t, uint32_t, uint8_t *,
                                uint32_t);
        } conversion;
        conversion.function = native_io_v1;
        v1_symbols[1].func_ptr = conversion.object;
    }
    v1_symbols[1].signature = "(Ii*~)i";
    v1_symbols[1].attachment = NULL;
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
    arguments.native_module_name = "pxa.core.v1";
    arguments.native_symbols = v1_symbols;
    arguments.n_native_symbols = 2;
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

int pxa_wamr_engine_poll_deadlines(pxa_wamr_engine_t *engine, uint64_t now_us) {
    int terminated = 0;
    if (engine == NULL || engine->magic != PXA_WAMR_ENGINE_MAGIC) return 0;
    engine_enter_critical(engine);
    if (engine->executing_module != NULL &&
        engine->executing_deadline_us != 0 &&
        now_us >= engine->executing_deadline_us) {
        engine->executing_deadline_us = 0;
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
    LoadArgs load_args = {0};
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
    if (slot->core_major != 1)
        return discard_entry(engine, slot, PXA_STATUS_UNSUPPORTED);
    status = load_module_buffer(
        engine, slot, (pxa_bytes_t){(const uint8_t *)path, strlen(path)});
    if (status != PXA_STATUS_OK) {
#if defined(ESP_PLATFORM)
        ESP_LOGE(PXA_WAMR_LOG_TAG,
                 "read-artifact failed: artifact=%s status=%d", path,
                 (int)status);
#endif
        return discard_entry(engine, slot, status);
    }
    /* Standard AOT code is already copied to an executable mapping. Let the
     * loader own its strings and initial data so the duplicate source buffer
     * can be released before instantiation. Static workspaces and XIP retain
     * their source; interpreted Wasm keeps its existing ownership policy. */
    load_args.name = "";
    load_args.wasm_binary_freeable = slot->dynamic_module_bytes &&
        entry->artifact->kind == PXA_ARTIFACT_AOT &&
        standard_aot_sections(slot->module_bytes, slot->module_size) &&
        !wasm_runtime_is_xip_file(slot->module_bytes, (uint32_t)slot->module_size);
    slot->module = wasm_runtime_load_ex(slot->module_bytes,
                                        (uint32_t)slot->module_size,
                                        &load_args, error, sizeof(error));
    if (slot->module == NULL) {
        log_runtime_failure("load", path, error);
        return discard_entry(engine, slot, PXA_STATUS_UNSUPPORTED);
    }
    if (wasm_runtime_is_underlying_binary_freeable(slot->module)) {
        release_module_buffer(engine, slot);
    }
    wasi_declared = component_wasi_features(entry->component, &wasi_features);
    if (wasi_declared && !engine->wasi_enabled) {
        return discard_entry(engine, slot, PXA_STATUS_UNSUPPORTED);
    }
    status = validate_module_imports(slot->module, path, slot->core_major,
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
    slot->module_instance = wasm_runtime_instantiate(
        slot->module, engine->guest_stack_size, engine->host_managed_heap_size,
        error, sizeof(error));
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
    status = pxa_component_set_core_major(engine->runtime, component,
                                          slot->core_major);
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
    entry = find_by_component(engine, component);
    if (entry == NULL) return PXA_STATUS_NOT_FOUND;

    status = pxa_event_peek(runtime, component, &view);
    if (status != PXA_STATUS_OK) return status;
    if (view.size == 0 ||
        view.size > PXA_MAX_CONTROL_MESSAGE -
                        (PXA_V1_ENVELOPE_SIZE - PXA_ENVELOPE_SIZE)) {
        return PXA_STATUS_INTERNAL;
    }
    /* Core v1 adds eight envelope bytes. The only extra payload expansion,
     * Store progress 20 -> 24 bytes, always fits the minimum 64-byte buffer.
     * Reserving twelve for every event would turn a valid 4096-byte READ
     * completion into an unnecessary 8192-byte Guest allocation. */
    callback_size = view.size + PXA_V1_ENVELOPE_SIZE - PXA_ENVELOPE_SIZE;
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
        output->request_id = decoded.request_id;
        output->payload_size = decoded.payload.size;
    }
    {
        uint8_t *envelope = (uint8_t *)native;
        uint64_t guest_token = 0;
        int game_render_create = 0;
        size_t payload_size = decoded.payload.size;
        if (decoded.service == PXA_IPC_SERVICE_ID &&
            (decoded.opcode == PXA_IPC_REQUEST_EVENT ||
             decoded.opcode == PXA_IPC_REPLY_EVENT)) {
            if (decoded.request_id == 0) return PXA_STATUS_PROTOCOL_ERROR;
            guest_token = decoded.request_id;
        } else if (decoded.request_id != 0) {
            pxa_wamr_v1_request_t *slot =
                v1_request_find(entry, decoded.request_id);
            if (slot == NULL) return PXA_STATUS_PROTOCOL_ERROR;
            if (slot->service != decoded.service ||
                slot->opcode != decoded.opcode) {
                memset(slot, 0, sizeof(*slot));
                return PXA_STATUS_PROTOCOL_ERROR;
            }
            guest_token = slot->token;
            game_render_create = slot->service == PXA_GAME_RENDER_SERVICE_ID &&
                (slot->opcode == PXA_GAME_RENDER_CREATE_CONTEXT ||
                 slot->opcode == PXA_GAME_RENDER_CREATE_AUTO_CONTEXT);
            memset(slot, 0, sizeof(*slot));
        }
        memmove(envelope + PXA_V1_ENVELOPE_SIZE,
                envelope + PXA_ENVELOPE_SIZE, decoded.payload.size);
        if (decoded.service == PXA_STORE_INSTALLER_SERVICE_ID &&
            decoded.opcode == PXA_STORE_INSTALLER_DOWNLOAD_PROGRESS &&
            decoded.request_id == 0) {
            uint8_t *payload = envelope + PXA_V1_ENVELOPE_SIZE;
            pxa_wamr_v1_request_t *slot;
            if (payload_size != 20u) return PXA_STATUS_PROTOCOL_ERROR;
            slot = v1_request_find(entry, pxa_read_u32(payload));
            if (slot != NULL &&
                (slot->service != PXA_STORE_INSTALLER_SERVICE_ID ||
                 slot->opcode != 2u)) return PXA_STATUS_PROTOCOL_ERROR;
            memmove(payload + 8u, payload + 4u, 16u);
            pxa_wire_generated_store_u64(
                payload, slot == NULL ? 0u : slot->token);
            payload_size = 24u;
        }
        if (game_render_create) {
            uint8_t *payload = envelope + PXA_V1_ENVELOPE_SIZE;
            if (payload_size < 4u) return PXA_STATUS_PROTOCOL_ERROR;
            if ((int32_t)pxa_read_u32(payload) == PXA_STATUS_OK) {
                size_t expected = decoded.opcode ==
                    PXA_GAME_RENDER_CREATE_AUTO_CONTEXT ? 36u : 24u;
                if (payload_size != expected ||
                    pxa_read_u64(payload + 4) == PXA_HANDLE64_INVALID)
                    return PXA_STATUS_PROTOCOL_ERROR;
            } else if (payload_size != 4u) {
                return PXA_STATUS_PROTOCOL_ERROR;
            }
        }
        pxa_wire_generated_store_u64(
            envelope + PXA_WIRE_V1_REQUEST_TOKEN_OFFSET, guest_token);
        pxa_wire_generated_store_u32(
            envelope + PXA_WIRE_V1_PAYLOAD_LEN_OFFSET,
            (uint32_t)payload_size);
        pxa_wire_generated_store_u32(
            envelope + PXA_WIRE_V1_FLAGS_OFFSET, 0);
        callback_size = PXA_V1_ENVELOPE_SIZE + payload_size;
        if (callback_size > PXA_MAX_CONTROL_MESSAGE)
            return PXA_STATUS_PROTOCOL_ERROR;
        if (output != NULL) output->payload_size = payload_size;
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
        } else {
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
    (void)wasm_runtime_unregister_natives("pxa.core.v1", v1_symbols);
    wasm_runtime_destroy();
    if (runtime_allocator_engine == engine) runtime_allocator_engine = NULL;
    engine->magic = 0;
}
