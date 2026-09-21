/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * Project-owned ESP-IDF memory mapping implementation for WAMR. AOT text is
 * allocated in PSRAM and separately mapped into the instruction bus through
 * the public ESP-IDF MMU API.
 */

#include "platform_api_vmcore.h"
#include "platform_api_extension.h"
#if (WASM_MEM_DUAL_BUS_MIRROR != 0)
#include "esp_cache.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mmu_map.h"
#include "soc/ext_mem_defs.h"

#define PXA_WAMR_MEMMAP_TAG "PXA_WAMR_MEMMAP"
#define PXA_EXT_ICACHE_LINE_SIZE 32U
#define PXA_EXT_DCACHE_LINE_SIZE 64U

#define in_ibus_ext(addr) \
    (((uintptr_t)(addr) >= SOC_IRAM0_CACHE_ADDRESS_LOW) && \
     ((uintptr_t)(addr) < SOC_IRAM0_CACHE_ADDRESS_HIGH))
#endif

void *
os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file)
{
    if (prot & MMAP_PROT_EXEC) {
#if (WASM_MEM_DUAL_BUS_MIRROR != 0)
        const size_t page_size = CONFIG_MMU_PAGE_SIZE;
        size_t map_size;
        void *dbus_code;
        void *ibus_code = NULL;
        esp_paddr_t paddr;
        mmu_target_t target;

        if (size == 0 || size > SIZE_MAX - (page_size - 1U)) {
            return NULL;
        }
        map_size = (size + page_size - 1U) & ~(page_size - 1U);

        /* TLSF splits and returns the leading alignment gap to the heap. */
        dbus_code = heap_caps_aligned_alloc(
            page_size, map_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!dbus_code) {
            return NULL;
        }

        if (esp_mmu_vaddr_to_paddr(dbus_code, &paddr, &target) != ESP_OK
            || target != MMU_TARGET_PSRAM0
            || paddr % page_size != 0
            || esp_mmu_map(paddr, map_size, target, MMU_MEM_CAP_EXEC,
                           ESP_MMU_MMAP_FLAG_PADDR_SHARED,
                           &ibus_code) != ESP_OK) {
            heap_caps_free(dbus_code);
            return NULL;
        }
        memset(dbus_code, 0, map_size);
        ESP_LOGI(PXA_WAMR_MEMMAP_TAG,
                 "AOT mapped: ibus=%p dbus=%p request=%u map=%u alloc=%u free_psram=%u",
                 ibus_code, dbus_code, (unsigned)size, (unsigned)map_size,
                 (unsigned)map_size,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return ibus_code;
#else
#if (WASM_MEM_EXEC_IN_PSRAM != 0)
        uint32_t mem_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#else
        uint32_t mem_caps = MALLOC_CAP_EXEC;
#endif
        void *buf_origin =
            heap_caps_malloc(size + 4 + sizeof(uintptr_t), mem_caps);
        if (!buf_origin) {
            return NULL;
        }
        void *buf_fixed = (uint8_t *)buf_origin + sizeof(void *);
        if ((uintptr_t)buf_fixed & (uintptr_t)0x7) {
            buf_fixed = (void *)((uintptr_t)((uint8_t *)buf_fixed + 4)
                                 & ~(uintptr_t)7);
        }

        uintptr_t *addr_field = (uintptr_t *)buf_fixed - 1;
        *addr_field = (uintptr_t)buf_origin;
        memset(buf_fixed, 0, size);
        return buf_fixed;
#endif
    }
    else {
#if (WASM_MEM_DUAL_BUS_MIRROR != 0)
        uint32_t mem_caps = MALLOC_CAP_SPIRAM;
#else
        uint32_t mem_caps = MALLOC_CAP_8BIT;
#endif
        void *buf_origin =
            heap_caps_malloc(size + 4 + sizeof(uintptr_t), mem_caps);
        if (!buf_origin) {
            return NULL;
        }

        void *buf_fixed = (uint8_t *)buf_origin + sizeof(void *);
        if ((uintptr_t)buf_fixed & (uintptr_t)0x7) {
            buf_fixed = (void *)((uintptr_t)((uint8_t *)buf_fixed + 4)
                                 & ~(uintptr_t)7);
        }

        uintptr_t *addr_field = (uintptr_t *)buf_fixed - 1;
        *addr_field = (uintptr_t)buf_origin;

        memset(buf_fixed, 0, size);
        return buf_fixed;
    }
}

void *
os_mremap(void *old_addr, size_t old_size, size_t new_size)
{
    return os_mremap_slow(old_addr, old_size, new_size);
}

void
os_munmap(void *addr, size_t size)
{
    void *ptr = addr;

#if (WASM_MEM_DUAL_BUS_MIRROR != 0)
    if (in_ibus_ext(ptr)) {
        void *dbus_ptr = os_get_dbus_mirror(ptr);
        esp_err_t unmap_status;
        size_t free_psram_before;
        size_t free_psram_after;
        int free_psram_delta;

        if (dbus_ptr == NULL) {
            ESP_LOGE(PXA_WAMR_MEMMAP_TAG,
                     "AOT unmap lost D-Bus mirror: ibus=%p size=%u; retaining PSRAM",
                     ptr, (unsigned)size);
            return;
        }

        unmap_status = esp_mmu_unmap(ptr);
        if (unmap_status != ESP_OK) {
            ESP_LOGE(PXA_WAMR_MEMMAP_TAG,
                     "AOT unmap failed: ibus=%p dbus=%p size=%u err=%s; retaining PSRAM",
                     ptr, dbus_ptr, (unsigned)size,
                     esp_err_to_name(unmap_status));
            return;
        }
        ptr = dbus_ptr;
        free_psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        heap_caps_free(ptr);
        free_psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        free_psram_delta = free_psram_after >= free_psram_before
                               ? (int)(free_psram_after - free_psram_before)
                               : -(int)(free_psram_before - free_psram_after);
        ESP_LOGI(PXA_WAMR_MEMMAP_TAG,
                 "AOT released: ibus=%p dbus=%p size=%u free_psram=%u->%u delta=%d",
                 addr, dbus_ptr, (unsigned)size, (unsigned)free_psram_before,
                 (unsigned)free_psram_after, free_psram_delta);
        return;
    }
#endif
    os_free(ptr);
}

int
os_mprotect(void *addr, size_t size, int prot)
{
    return 0;
}

void
os_dcache_flush(void)
{}

void
os_icache_flush(void *start, size_t len)
{
#if (WASM_MEM_DUAL_BUS_MIRROR != 0)
    if (start == NULL || len == 0) {
        return;
    }

    const uintptr_t ibus_begin =
        (uintptr_t)start & ~(PXA_EXT_ICACHE_LINE_SIZE - 1U);
    const uintptr_t ibus_end =
        ((uintptr_t)start + len + PXA_EXT_ICACHE_LINE_SIZE - 1U) &
        ~(PXA_EXT_ICACHE_LINE_SIZE - 1U);
    if (ibus_end > ibus_begin) {
        const uintptr_t dbus_code =
            (uintptr_t)os_get_dbus_mirror((void *)ibus_begin);
        if (dbus_code == 0) {
            return;
        }
        const uintptr_t dbus_begin =
            dbus_code & ~(PXA_EXT_DCACHE_LINE_SIZE - 1U);
        const uintptr_t dbus_end =
            (dbus_code + (ibus_end - ibus_begin)
             + PXA_EXT_DCACHE_LINE_SIZE - 1U) &
            ~(PXA_EXT_DCACHE_LINE_SIZE - 1U);

        if (dbus_end > dbus_begin) {
            (void)esp_cache_msync((void *)dbus_begin, dbus_end - dbus_begin,
                                  ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        }
        (void)esp_cache_msync((void *)ibus_begin, ibus_end - ibus_begin,
                              ESP_CACHE_MSYNC_FLAG_DIR_M2C |
                                  ESP_CACHE_MSYNC_FLAG_TYPE_INST);
    }
#else
#if (WASM_MEM_EXEC_IN_PSRAM != 0)
    __builtin___clear_cache((char *)start, (char *)start + len);
#else
    (void)start;
    (void)len;
#endif
#endif
}

#if (WASM_MEM_DUAL_BUS_MIRROR != 0)
void *
os_get_dbus_mirror(void *ibus)
{
    esp_paddr_t paddr;
    mmu_target_t target;
    void *dbus = NULL;

    if (in_ibus_ext(ibus)) {
        if (esp_mmu_vaddr_to_paddr(ibus, &paddr, &target) != ESP_OK
            || esp_mmu_paddr_to_vaddr(paddr, target, MMU_VADDR_DATA,
                                      &dbus) != ESP_OK) {
            return NULL;
        }
        return dbus;
    }
    return ibus;
}
#endif
