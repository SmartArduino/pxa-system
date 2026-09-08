#include "pxa/scheduler.h"
#include "common/bytes_internal.h"
#include "common/checked_math.h"
#include "common/status_internal.h"

#include <stdint.h>
#include <string.h>

#define PXA_SCHEDULER_MAGIC UINT32_C(0x50584a42)

typedef struct {
    uint8_t bytes[PXA_SCHEDULER_MAX_COMPONENT_ID_BYTES];
    uint8_t size;
} pxa_scheduler_job_t;

struct pxa_scheduler_service {
    uint32_t magic;
    pxa_runtime_t *runtime;
    pxa_scheduler_job_t *jobs;
    pxa_scheduler_entry_t *entries;
    pxa_scheduler_entry_t *scratch;
    void *clock_context;
    pxa_scheduler_clock_fn clock;
    pxa_scheduler_store_t store;
    uint32_t next_id;
    uint32_t min_delay_ms;
    uint32_t max_delay_ms;
    uint32_t default_execution_ms;
    uint32_t max_execution_ms;
    void *work_context;
    pxa_work_complete_fn complete_work;
    pxa_work_cancel_fn cancel_work;
    uint16_t job_count;
    uint16_t job_capacity;
    uint16_t entry_count;
    uint16_t max_entries;
    uint8_t initialized;
    uint8_t registered;
};

typedef struct {
    pxa_bytes_t component;
    pxa_bytes_t input;
    uint32_t delay_ms;
    uint32_t max_execution_ms;
    uint32_t retry_delay_ms;
    uint8_t max_attempts;
    uint8_t seen;
} pxa_scheduler_request_t;

static int component_id_valid(pxa_bytes_t id) {
    size_t index;
    if (id.data == NULL || id.size == 0 ||
        id.size > PXA_SCHEDULER_MAX_COMPONENT_ID_BYTES || id.data[0] < 'a' ||
        id.data[0] > 'z') {
        return 0;
    }
    for (index = 1; index < id.size; ++index) {
        uint8_t value = id.data[index];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') || value == '.' ||
              value == '_' || value == '-')) {
            return 0;
        }
    }
    return 1;
}

void pxa_scheduler_config_init(pxa_scheduler_config_t *config) {
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    config->struct_size = sizeof(*config);
    config->max_entries = PXA_SCHEDULER_DEFAULT_MAX_ENTRIES;
    config->min_delay_ms = PXA_SCHEDULER_DEFAULT_MIN_DELAY_MS;
    config->max_delay_ms = PXA_SCHEDULER_DEFAULT_MAX_DELAY_MS;
    config->default_execution_ms = PXA_WORK_DEFAULT_EXECUTION_MS;
    config->max_execution_ms = PXA_SCHEDULER_DEFAULT_MAX_EXECUTION_MS;
    config->store.struct_size = sizeof(config->store);
}

static int config_valid(const pxa_scheduler_config_t *config) {
    uint16_t index;
    if (config == NULL || config->struct_size < sizeof(*config) ||
        (config->job_component_count != 0 &&
         config->job_components == NULL) ||
        config->max_entries == 0 || config->min_delay_ms == 0 ||
        config->min_delay_ms > config->max_delay_ms ||
        config->default_execution_ms == 0 ||
        config->default_execution_ms > config->max_execution_ms ||
        config->max_execution_ms == 0 || config->clock == NULL ||
        config->store.struct_size < sizeof(config->store) ||
        config->store.load == NULL || config->store.save == NULL) {
        return 0;
    }
    for (index = 0; index < config->job_component_count; ++index) {
        if (!component_id_valid(config->job_components[index])) return 0;
    }
    return 1;
}

size_t pxa_scheduler_service_workspace_size(
    const pxa_scheduler_config_t *config) {
    size_t jobs_size;
    size_t entries_size;
    size_t size = PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    if (!config_valid(config) ||
        (config->job_component_count != 0 &&
         sizeof(pxa_scheduler_job_t) >
             SIZE_MAX / (size_t)config->job_component_count) ||
        sizeof(pxa_scheduler_entry_t) >
            SIZE_MAX / (size_t)config->max_entries) {
        return 0;
    }
    jobs_size = (size_t)config->job_component_count *
                sizeof(pxa_scheduler_job_t);
    entries_size = (size_t)config->max_entries *
                   sizeof(pxa_scheduler_entry_t);
    if (sizeof(pxa_scheduler_service_t) > SIZE_MAX - size) return 0;
    size += sizeof(pxa_scheduler_service_t);
    if (jobs_size > SIZE_MAX - size) return 0;
    size += jobs_size;
    if (PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u > SIZE_MAX - size) return 0;
    size += PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    if (entries_size > (SIZE_MAX - size) / 2u) return 0;
    size += entries_size;
    if (PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u > SIZE_MAX - size) return 0;
    size += PXA_INTERNAL_WORKSPACE_ALIGNMENT - 1u;
    if (entries_size > SIZE_MAX - size) return 0;
    return size + entries_size;
}

static void sort_jobs(pxa_scheduler_job_t *jobs, uint16_t count) {
    uint16_t index;
    for (index = 1; index < count; ++index) {
        pxa_scheduler_job_t value = jobs[index];
        uint16_t position = index;
        while (position != 0 &&
               pxa_bytes_compare_internal(
                   (pxa_bytes_t){jobs[position - 1].bytes,
                                 jobs[position - 1].size},
                   (pxa_bytes_t){value.bytes, value.size}) > 0) {
            jobs[position] = jobs[position - 1];
            position--;
        }
        jobs[position] = value;
    }
}

pxa_status_t pxa_scheduler_service_init(
    void *workspace, size_t workspace_size, pxa_runtime_t *runtime,
    const pxa_scheduler_config_t *config, pxa_scheduler_service_t **output) {
    size_t required;
    uintptr_t cursor;
    uintptr_t end;
    uint16_t index;
    pxa_scheduler_service_t *service;
    if (output == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    *output = NULL;
    required = pxa_scheduler_service_workspace_size(config);
    if (workspace == NULL || runtime == NULL || required == 0 ||
        workspace_size < required ||
        (uintptr_t)workspace > UINTPTR_MAX - workspace_size) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    end = (uintptr_t)workspace + workspace_size;
    cursor = pxa_internal_align_pointer((uintptr_t)workspace, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    service = (pxa_scheduler_service_t *)cursor;
    memset(service, 0, sizeof(*service));
    cursor += sizeof(*service);
    service->jobs = (pxa_scheduler_job_t *)cursor;
    cursor += (size_t)config->job_component_count * sizeof(service->jobs[0]);
    cursor = pxa_internal_align_pointer(cursor, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    service->entries = (pxa_scheduler_entry_t *)cursor;
    cursor += (size_t)config->max_entries * sizeof(service->entries[0]);
    cursor = pxa_internal_align_pointer(cursor, PXA_INTERNAL_WORKSPACE_ALIGNMENT);
    service->scratch = (pxa_scheduler_entry_t *)cursor;
    cursor += (size_t)config->max_entries * sizeof(service->scratch[0]);
    if (cursor > end) return PXA_STATUS_INVALID_ARGUMENT;
    memset(service->jobs, 0,
           (size_t)config->job_component_count * sizeof(service->jobs[0]));
    for (index = 0; index < config->job_component_count; ++index) {
        service->jobs[index].size =
            (uint8_t)config->job_components[index].size;
        memcpy(service->jobs[index].bytes,
               config->job_components[index].data,
               config->job_components[index].size);
    }
    sort_jobs(service->jobs, config->job_component_count);
    for (index = 1; index < config->job_component_count; ++index) {
        if (pxa_bytes_compare_internal(
                (pxa_bytes_t){service->jobs[index - 1].bytes,
                              service->jobs[index - 1].size},
                (pxa_bytes_t){service->jobs[index].bytes,
                              service->jobs[index].size}) == 0) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
    }
    memset(service->entries, 0,
           (size_t)config->max_entries * sizeof(service->entries[0]));
    memset(service->scratch, 0,
           (size_t)config->max_entries * sizeof(service->scratch[0]));
    service->runtime = runtime;
    service->clock_context = config->clock_context;
    service->clock = config->clock;
    service->store = config->store;
    service->next_id = 1;
    service->min_delay_ms = config->min_delay_ms;
    service->max_delay_ms = config->max_delay_ms;
    service->default_execution_ms = config->default_execution_ms;
    service->max_execution_ms = config->max_execution_ms;
    service->work_context = config->work_context;
    service->complete_work = config->complete_work;
    service->cancel_work = config->cancel_work;
    service->job_count = config->job_component_count;
    service->job_capacity = config->job_component_count;
    service->max_entries = config->max_entries;
    service->magic = PXA_SCHEDULER_MAGIC;
    *output = service;
    return PXA_STATUS_OK;
}

static int service_valid(const pxa_scheduler_service_t *service) {
    return service != NULL && service->magic == PXA_SCHEDULER_MAGIC;
}

static int job_declared(const pxa_scheduler_service_t *service,
                        pxa_bytes_t component) {
    uint16_t begin = 0;
    uint16_t end = service->job_count;
    while (begin < end) {
        uint16_t middle = (uint16_t)(begin + (end - begin) / 2u);
        int comparison = pxa_bytes_compare_internal(
            (pxa_bytes_t){service->jobs[middle].bytes,
                          service->jobs[middle].size},
            component);
        if (comparison < 0) {
            begin = (uint16_t)(middle + 1u);
        } else {
            end = middle;
        }
    }
    return begin < service->job_count &&
           pxa_bytes_compare_internal(
               (pxa_bytes_t){service->jobs[begin].bytes,
                             service->jobs[begin].size},
               component) == 0;
}

static pxa_bytes_t entry_component(const pxa_scheduler_entry_t *entry) {
    return (pxa_bytes_t){entry->component_id, entry->component_id_size};
}

static void swap_entries(pxa_scheduler_entry_t *left,
                         pxa_scheduler_entry_t *right) {
    pxa_scheduler_entry_t value = *left;
    *left = *right;
    *right = value;
}

static void sift_entries_down(pxa_scheduler_entry_t *entries, size_t root,
                              size_t count) {
    while (root < count / 2u) {
        size_t child = root * 2u + 1u;
        if (child + 1u < count &&
            entries[child].id < entries[child + 1u].id) {
            child++;
        }
        if (entries[root].id >= entries[child].id) return;
        swap_entries(&entries[root], &entries[child]);
        root = child;
    }
}

static void sort_entries_by_id(pxa_scheduler_entry_t *entries, size_t count) {
    size_t index;
    if (count < 2u) return;
    for (index = count / 2u; index != 0; --index) {
        sift_entries_down(entries, index - 1u, count);
    }
    for (index = count; index > 1u; --index) {
        swap_entries(&entries[0], &entries[index - 1u]);
        sift_entries_down(entries, 0, index - 1u);
    }
}

static int sorted_entries_unique(const pxa_scheduler_entry_t *entries,
                                 size_t count) {
    size_t index;
    for (index = 1; index < count; ++index) {
        if (entries[index - 1u].id == entries[index].id) return 0;
    }
    return 1;
}

static uint16_t entry_lower_bound(const pxa_scheduler_service_t *service,
                                  uint32_t id) {
    uint16_t begin = 0;
    uint16_t end = service->entry_count;
    while (begin < end) {
        uint16_t middle = (uint16_t)(begin + (end - begin) / 2u);
        if (service->entries[middle].id < id) {
            begin = (uint16_t)(middle + 1u);
        } else {
            end = middle;
        }
    }
    return begin;
}

static int loaded_entry_valid(const pxa_scheduler_service_t *service,
                              const pxa_scheduler_entry_t *entry) {
    if (entry->id == 0 || !component_id_valid(entry_component(entry)) ||
        !job_declared(service, entry_component(entry)) ||
        entry->max_execution_ms == 0 ||
        entry->max_execution_ms > service->max_execution_ms) {
        return 0;
    }
    return entry->attempt != 0 && entry->max_attempts != 0 &&
           entry->attempt <= entry->max_attempts &&
           entry->max_attempts <= PXA_WORK_MAX_ATTEMPTS &&
           entry->input_size <= PXA_WORK_MAX_INPUT_BYTES &&
           (entry->max_attempts == 1 ||
            (entry->retry_delay_ms >= service->min_delay_ms &&
             entry->retry_delay_ms <= service->max_delay_ms));
}

pxa_status_t pxa_scheduler_load(pxa_scheduler_service_t *service) {
    size_t count = 0;
    uint32_t maximum;
    uint16_t index;
    pxa_status_t status;
    if (!service_valid(service)) return PXA_STATUS_INVALID_ARGUMENT;
    if (service->initialized) return PXA_STATUS_OK;
    status = pxa_status_normalize(service->store.load(
        service->store.context, service->entries, service->max_entries,
        &count));
    if (status != PXA_STATUS_OK) return status;
    if (count > service->max_entries) return PXA_STATUS_INTERNAL;
    for (index = 0; index < (uint16_t)count; ++index) {
        if (!loaded_entry_valid(service, &service->entries[index])) {
            return PXA_STATUS_INTERNAL;
        }
    }
    sort_entries_by_id(service->entries, count);
    if (!sorted_entries_unique(service->entries, count))
        return PXA_STATUS_INTERNAL;
    maximum = count == 0 ? 0 : service->entries[count - 1u].id;
    service->entry_count = (uint16_t)count;
    service->next_id = maximum == UINT32_MAX ? 1u : maximum + 1u;
    service->initialized = 1;
    return PXA_STATUS_OK;
}

static int job_list_valid(const pxa_bytes_t *jobs, uint16_t count) {
    uint16_t index;
    uint16_t other;
    if (count != 0 && jobs == NULL) return 0;
    for (index = 0; index < count; ++index) {
        if (!component_id_valid(jobs[index])) return 0;
        for (other = 0; other < index; ++other) {
            if (pxa_bytes_compare_internal(jobs[other], jobs[index]) == 0)
                return 0;
        }
    }
    return 1;
}

static int job_list_contains(const pxa_bytes_t *jobs, uint16_t count,
                             pxa_bytes_t component) {
    uint16_t index;
    for (index = 0; index < count; ++index) {
        if (pxa_bytes_compare_internal(jobs[index], component) == 0) return 1;
    }
    return 0;
}

pxa_status_t pxa_scheduler_rebind(
    pxa_scheduler_service_t *service, const pxa_bytes_t *job_components,
    uint16_t job_component_count) {
    size_t count = 0;
    uint32_t maximum;
    uint16_t index;
    pxa_status_t status;
    if (!service_valid(service) || !service->initialized ||
        job_component_count > service->job_capacity ||
        !job_list_valid(job_components, job_component_count)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    status = pxa_status_normalize(service->store.load(
        service->store.context, service->scratch, service->max_entries,
        &count));
    if (status != PXA_STATUS_OK) return status;
    if (count > service->max_entries) return PXA_STATUS_INTERNAL;
    for (index = 0; index < (uint16_t)count; ++index) {
        const pxa_scheduler_entry_t *entry = &service->scratch[index];
        if (entry->id == 0 || !component_id_valid(entry_component(entry)) ||
            !job_list_contains(job_components, job_component_count,
                               entry_component(entry)) ||
            entry->max_execution_ms == 0 ||
            entry->max_execution_ms > service->max_execution_ms ||
            entry->attempt == 0 || entry->max_attempts == 0 ||
            entry->attempt > entry->max_attempts ||
            entry->max_attempts > PXA_WORK_MAX_ATTEMPTS ||
            entry->input_size > PXA_WORK_MAX_INPUT_BYTES ||
            (entry->max_attempts > 1 &&
             (entry->retry_delay_ms < service->min_delay_ms ||
              entry->retry_delay_ms > service->max_delay_ms))) {
            return PXA_STATUS_INTERNAL;
        }
    }
    sort_entries_by_id(service->scratch, count);
    if (!sorted_entries_unique(service->scratch, count))
        return PXA_STATUS_INTERNAL;
    maximum = count == 0 ? 0 : service->scratch[count - 1u].id;
    memset(service->jobs, 0,
           (size_t)service->job_capacity * sizeof(service->jobs[0]));
    for (index = 0; index < job_component_count; ++index) {
        service->jobs[index].size = (uint8_t)job_components[index].size;
        memcpy(service->jobs[index].bytes, job_components[index].data,
               job_components[index].size);
    }
    sort_jobs(service->jobs, job_component_count);
    memset(service->entries, 0,
           (size_t)service->max_entries * sizeof(service->entries[0]));
    memcpy(service->entries, service->scratch,
           count * sizeof(service->entries[0]));
    service->job_count = job_component_count;
    service->entry_count = (uint16_t)count;
    service->next_id = maximum == UINT32_MAX ? 1u : maximum + 1u;
    return PXA_STATUS_OK;
}

static pxa_status_t parse_work(pxa_bytes_t payload,
                               pxa_scheduler_request_t *output) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    pxa_status_t status;
    memset(output, 0, sizeof(*output));
    pxa_record_iterator_init(&iterator, payload);
    for (;;) {
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.raw_tag < previous) return PXA_STATUS_INVALID_ARGUMENT;
        previous = record.raw_tag;
        if (record.optional) continue;
        if (record.tag == 1 && (output->seen & 1u) == 0 &&
            component_id_valid(record.payload)) {
            output->component = record.payload;
            output->seen |= 1u;
        } else if (record.tag == 2 && (output->seen & 2u) == 0 &&
                   record.payload.size == 4) {
            output->delay_ms = pxa_read_u32(record.payload.data);
            output->seen |= 2u;
        } else if (record.tag == 3 && (output->seen & 4u) == 0 &&
                   record.payload.size == 4) {
            output->max_execution_ms = pxa_read_u32(record.payload.data);
            output->seen |= 4u;
        } else if (record.tag == 5 && (output->seen & 8u) == 0 &&
                   record.payload.size <= PXA_WORK_MAX_INPUT_BYTES) {
            output->input = record.payload;
            output->seen |= 8u;
        } else if (record.tag == 6 && (output->seen & 16u) == 0 &&
                   record.payload.size == 4) {
            output->retry_delay_ms = pxa_read_u32(record.payload.data);
            output->seen |= 16u;
        } else if (record.tag == 7 && (output->seen & 32u) == 0 &&
                   record.payload.size == 1) {
            output->max_attempts = record.payload.data[0];
            output->seen |= 32u;
        } else {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
    }
    return (output->seen & 55u) == 55u
               ? PXA_STATUS_OK
               : PXA_STATUS_INVALID_ARGUMENT;
}

static pxa_status_t parse_cancel(pxa_bytes_t payload, uint32_t *id) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    pxa_status_t status;
    *id = 0;
    pxa_record_iterator_init(&iterator, payload);
    for (;;) {
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK) return status;
        if (record.optional) continue;
        if (record.tag != 4 || *id != 0 || record.payload.size != 4) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        *id = pxa_read_u32(record.payload.data);
    }
    return *id != 0 ? PXA_STATUS_OK : PXA_STATUS_INVALID_ARGUMENT;
}

static int id_in_use(const pxa_scheduler_service_t *service, uint32_t id) {
    uint16_t index = entry_lower_bound(service, id);
    return index < service->entry_count && service->entries[index].id == id;
}

static uint32_t allocate_id(pxa_scheduler_service_t *service) {
    uint16_t attempts;
    uint32_t id = service->next_id;
    if (id == 0) id = 1;
    for (attempts = 0; attempts <= service->entry_count; ++attempts) {
        if (!id_in_use(service, id)) {
            service->next_id = id == UINT32_MAX ? 1u : id + 1u;
            return id;
        }
        id = id == UINT32_MAX ? 1u : id + 1u;
    }
    return 0;
}

static pxa_status_t schedule_entry(pxa_scheduler_service_t *service,
                                   const pxa_message_view_t *message,
                                   uint8_t *result, size_t *result_size) {
    pxa_scheduler_request_t request;
    pxa_scheduler_entry_t *entry;
    uint64_t now;
    uint32_t id;
    uint32_t old_next_id;
    uint16_t index;
    pxa_writer_t writer;
    uint8_t id_bytes[4];
    pxa_status_t status = parse_work(message->payload, &request);
    if (status != PXA_STATUS_OK) return status;
    if (!job_declared(service, request.component) ||
        request.delay_ms > service->max_delay_ms ||
        request.max_attempts == 0 ||
        request.max_attempts > PXA_WORK_MAX_ATTEMPTS ||
        (request.max_attempts > 1 &&
         (request.retry_delay_ms < service->min_delay_ms ||
          request.retry_delay_ms > service->max_delay_ms))) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (request.max_execution_ms == 0) {
        request.max_execution_ms = service->default_execution_ms;
    } else if (request.max_execution_ms > service->max_execution_ms) {
        request.max_execution_ms = service->max_execution_ms;
    }
    if (service->entry_count >= service->max_entries) {
        return PXA_STATUS_RESOURCE_LIMIT;
    }
    now = service->clock(service->clock_context);
    if (now > UINT64_MAX - request.delay_ms) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    old_next_id = service->next_id;
    id = allocate_id(service);
    if (id == 0) return PXA_STATUS_RESOURCE_LIMIT;
    index = entry_lower_bound(service, id);
    memmove(&service->entries[index + 1u], &service->entries[index],
            (size_t)(service->entry_count - index) *
                sizeof(service->entries[0]));
    entry = &service->entries[index];
    memset(entry, 0, sizeof(*entry));
    entry->id = id;
    entry->due_at_ms = now + request.delay_ms;
    entry->max_execution_ms = request.max_execution_ms;
    entry->component_id_size = (uint8_t)request.component.size;
    memcpy(entry->component_id, request.component.data, request.component.size);
    entry->retry_delay_ms = request.retry_delay_ms;
    entry->attempt = 1;
    entry->max_attempts = request.max_attempts;
    entry->input_size = (uint8_t)request.input.size;
    if (request.input.size != 0) {
        memcpy(entry->input, request.input.data, request.input.size);
    }
    service->entry_count++;
    status = pxa_status_normalize(service->store.save(
        service->store.context, service->entries, service->entry_count));
    if (status != PXA_STATUS_OK) {
        memmove(&service->entries[index], &service->entries[index + 1u],
                (size_t)(service->entry_count - index - 1u) *
                    sizeof(service->entries[0]));
        service->entry_count--;
        memset(&service->entries[service->entry_count], 0,
               sizeof(service->entries[0]));
        service->next_id = old_next_id;
        return status;
    }
    pxa_write_u32(id_bytes, entry->id);
    pxa_writer_init(&writer, result, 16);
    status = pxa_writer_record(&writer, 4, id_bytes, 4);
    if (status == PXA_STATUS_OK) {
        uint8_t granted[4];
        pxa_write_u32(granted, entry->max_execution_ms);
        status = pxa_writer_record(&writer, 8, granted, sizeof(granted));
    }
    if (status != PXA_STATUS_OK) return PXA_STATUS_INTERNAL;
    *result_size = writer.size;
    return PXA_STATUS_OK;
}

static pxa_status_t parse_work_complete(pxa_bytes_t payload, uint32_t *id,
                                        pxa_work_result_t *result) {
    pxa_record_iterator_t iterator;
    pxa_record_view_t record;
    uint16_t previous = 0;
    uint8_t seen = 0;
    pxa_status_t status;
    *id = 0;
    *result = 0;
    pxa_record_iterator_init(&iterator, payload);
    for (;;) {
        status = pxa_record_next(&iterator, &record);
        if (status == PXA_STATUS_WOULD_BLOCK) break;
        if (status != PXA_STATUS_OK || record.raw_tag < previous) {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
        previous = record.raw_tag;
        if (record.optional) continue;
        if (record.tag == 4 && !(seen & 1u) && record.payload.size == 4) {
            *id = pxa_read_u32(record.payload.data);
            seen |= 1u;
        } else if (record.tag == 9 && !(seen & 2u) &&
                   record.payload.size == 1) {
            *result = record.payload.data[0];
            seen |= 2u;
        } else {
            return PXA_STATUS_INVALID_ARGUMENT;
        }
    }
    return seen == 3u && *id != 0 &&
                   *result >= PXA_WORK_RESULT_SUCCESS &&
                   *result <= PXA_WORK_RESULT_FAILURE
               ? PXA_STATUS_OK
               : PXA_STATUS_INVALID_ARGUMENT;
}

static pxa_status_t complete_work(pxa_scheduler_service_t *service,
                                  pxa_component_t component,
                                  const pxa_message_view_t *message) {
    uint32_t id;
    pxa_work_result_t result;
    pxa_status_t status = parse_work_complete(message->payload, &id, &result);
    if (status != PXA_STATUS_OK) return status;
    if (service->complete_work == NULL) return PXA_STATUS_UNAVAILABLE;
    return pxa_status_normalize(service->complete_work(
        service->work_context, component, id, result));
}

static pxa_status_t cancel_entry(pxa_scheduler_service_t *service,
                                 const pxa_message_view_t *message) {
    pxa_scheduler_entry_t removed;
    uint32_t id;
    uint16_t index;
    pxa_status_t status = parse_cancel(message->payload, &id);
    if (status != PXA_STATUS_OK) return status;
    index = entry_lower_bound(service, id);
    if (index == service->entry_count || service->entries[index].id != id) {
        return service->cancel_work == NULL
                   ? PXA_STATUS_NOT_FOUND
                   : pxa_status_normalize(
                         service->cancel_work(service->work_context, id));
    }
    removed = service->entries[index];
    memmove(&service->entries[index], &service->entries[index + 1],
            (size_t)(service->entry_count - index - 1u) *
                sizeof(service->entries[0]));
    service->entry_count--;
    status = pxa_status_normalize(service->store.save(
        service->store.context, service->entries, service->entry_count));
    if (status != PXA_STATUS_OK) {
        memmove(&service->entries[index + 1], &service->entries[index],
                (size_t)(service->entry_count - index) *
                    sizeof(service->entries[0]));
        service->entries[index] = removed;
        service->entry_count++;
    }
    return status;
}

static pxa_status_t scheduler_control(
    void *context, pxa_runtime_t *runtime, pxa_component_t component,
    const pxa_message_view_t *message) {
    pxa_scheduler_service_t *service = (pxa_scheduler_service_t *)context;
    uint8_t result[16] = {0};
    size_t result_size = 0;
    pxa_status_t status;
    pxa_status_t complete;
    (void)runtime;
    if (!service_valid(service) || !service->initialized ||
        message->request_id == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    if (message->opcode != PXA_WORK_ENQUEUE &&
        message->opcode != PXA_WORK_CANCEL &&
        message->opcode != PXA_WORK_COMPLETE) {
        return PXA_STATUS_UNSUPPORTED;
    }
    status = pxa_request_begin(service->runtime, component,
                               message->request_id,
                               PXA_WORK_SERVICE_ID, message->opcode, 0);
    if (status != PXA_STATUS_OK) return status;
    if (message->opcode == PXA_WORK_ENQUEUE) {
        status = schedule_entry(service, message, result, &result_size);
    } else if (message->opcode == PXA_WORK_COMPLETE) {
        status = complete_work(service, component, message);
    } else {
        status = cancel_entry(service, message);
    }
    complete = pxa_request_complete(
        service->runtime, component, message->request_id, status,
        status == PXA_STATUS_OK ? result : NULL,
        status == PXA_STATUS_OK ? result_size : 0);
    if (complete != PXA_STATUS_OK) {
        (void)pxa_request_cancel(service->runtime, component,
                                 message->request_id);
    }
    return PXA_STATUS_OK;
}

pxa_status_t pxa_scheduler_service_register(
    pxa_scheduler_service_t *service) {
    pxa_service_ops_t operations;
    pxa_status_t status;
    if (!service_valid(service)) return PXA_STATUS_INVALID_ARGUMENT;
    if (!service->initialized) return PXA_STATUS_BAD_STATE;
    if (service->registered) return PXA_STATUS_BAD_STATE;
    memset(&operations, 0, sizeof(operations));
    operations.struct_size = sizeof(operations);
    operations.service_id = PXA_WORK_SERVICE_ID;
    operations.major = PXA_WORK_SERVICE_MAJOR;
    operations.minor = PXA_WORK_SERVICE_MINOR;
    operations.context = service;
    operations.control = scheduler_control;
    status = pxa_service_register(service->runtime, &operations);
    if (status == PXA_STATUS_OK) service->registered = 1;
    return status;
}

int pxa_scheduler_has_pending(const pxa_scheduler_service_t *service) {
    return service_valid(service) && service->initialized &&
           service->entry_count != 0;
}

pxa_status_t pxa_scheduler_take_due(
    pxa_scheduler_service_t *service, pxa_scheduler_entry_t *output,
    size_t capacity, size_t *count) {
    uint64_t now;
    uint16_t index;
    uint16_t remaining = 0;
    size_t due_count = 0;
    pxa_status_t status;
    if (count == NULL || !service_valid(service) || !service->initialized ||
        (output == NULL && capacity != 0)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    now = service->clock(service->clock_context);
    for (index = 0; index < service->entry_count; ++index) {
        if (service->entries[index].due_at_ms <= now) due_count++;
    }
    *count = due_count;
    if (due_count > capacity) return PXA_STATUS_RESOURCE_LIMIT;
    if (due_count == 0) return PXA_STATUS_OK;
    due_count = 0;
    for (index = 0; index < service->entry_count; ++index) {
        if (service->entries[index].due_at_ms <= now) {
            output[due_count++] = service->entries[index];
        } else {
            service->scratch[remaining++] = service->entries[index];
        }
    }
    status = pxa_status_normalize(service->store.save(
        service->store.context, service->scratch, remaining));
    if (status != PXA_STATUS_OK) {
        *count = 0;
        return status;
    }
    memcpy(service->entries, service->scratch,
           (size_t)remaining * sizeof(service->entries[0]));
    service->entry_count = remaining;
    *count = due_count;
    return PXA_STATUS_OK;
}

static pxa_status_t requeue_entry(
    pxa_scheduler_service_t *service, const pxa_scheduler_entry_t *entry,
    uint32_t delay_ms, int consume_attempt) {
    pxa_scheduler_entry_t queued;
    uint16_t index;
    uint64_t now;
    pxa_status_t status;
    if (!service_valid(service) || !service->initialized || entry == NULL ||
        entry->attempt == 0 || delay_ms < service->min_delay_ms ||
        delay_ms > service->max_delay_ms ||
        (consume_attempt && entry->attempt >= entry->max_attempts) ||
        service->entry_count >= service->max_entries ||
        id_in_use(service, entry->id)) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    now = service->clock(service->clock_context);
    if (now > UINT64_MAX - delay_ms) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    queued = *entry;
    if (consume_attempt) queued.attempt++;
    queued.due_at_ms = now + delay_ms;
    index = entry_lower_bound(service, queued.id);
    memmove(&service->entries[index + 1u], &service->entries[index],
            (size_t)(service->entry_count - index) *
                sizeof(service->entries[0]));
    service->entries[index] = queued;
    service->entry_count++;
    status = pxa_status_normalize(service->store.save(
        service->store.context, service->entries, service->entry_count));
    if (status != PXA_STATUS_OK) {
        memmove(&service->entries[index], &service->entries[index + 1u],
                (size_t)(service->entry_count - index - 1u) *
                    sizeof(service->entries[0]));
        service->entry_count--;
        memset(&service->entries[service->entry_count], 0,
               sizeof(service->entries[0]));
    }
    return status;
}

pxa_status_t pxa_scheduler_retry(
    pxa_scheduler_service_t *service, const pxa_scheduler_entry_t *entry) {
    if (entry == NULL) return PXA_STATUS_INVALID_ARGUMENT;
    return requeue_entry(service, entry, entry->retry_delay_ms, 1);
}

pxa_status_t pxa_scheduler_defer(
    pxa_scheduler_service_t *service, const pxa_scheduler_entry_t *entry,
    uint32_t delay_ms) {
    return requeue_entry(service, entry, delay_ms, 0);
}

pxa_status_t pxa_scheduler_post_work_stop(
    pxa_scheduler_service_t *service, pxa_component_t component,
    uint32_t work_id, uint64_t deadline_ms) {
    uint8_t payload[12];
    if (!service_valid(service) || !service->initialized ||
        component == PXA_COMPONENT_INVALID || work_id == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    pxa_write_u32(payload, work_id);
    pxa_write_u64(payload + 4, deadline_ms);
    return pxa_event_post_message(
        service->runtime, component, PXA_WORK_SERVICE_ID,
        PXA_WORK_STOP_REQUESTED, 0,
        (pxa_bytes_t){payload, sizeof(payload)}, 1, 0);
}

pxa_status_t pxa_scheduler_encode_start_config(
    const pxa_scheduler_entry_t *entry, uint64_t deadline_ms,
    uint8_t *output, size_t capacity, size_t *output_size) {
    pxa_writer_t writer;
    uint8_t value[8];
    pxa_status_t status;
    if (entry == NULL || output == NULL || output_size == NULL ||
        entry->id == 0 || entry->attempt == 0 ||
        entry->input_size > PXA_WORK_MAX_INPUT_BYTES || deadline_ms == 0) {
        return PXA_STATUS_INVALID_ARGUMENT;
    }
    *output_size = 0;
    pxa_writer_init(&writer, output, capacity);
    pxa_write_u32(value, entry->id);
    status = pxa_writer_record(&writer, 7, value, 4);
    if (status == PXA_STATUS_OK) {
        value[0] = entry->attempt;
        status = pxa_writer_record(&writer, 9, value, 1);
    }
    if (status == PXA_STATUS_OK) {
        pxa_write_u64(value, deadline_ms);
        status = pxa_writer_record(&writer, 10, value, 8);
    }
    if (status == PXA_STATUS_OK && entry->input_size != 0) {
        status = pxa_writer_record(&writer, 11, entry->input,
                                   entry->input_size);
    }
    if (status != PXA_STATUS_OK) return status;
    *output_size = writer.size;
    return PXA_STATUS_OK;
}
