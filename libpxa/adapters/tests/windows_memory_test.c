/* SPDX-License-Identifier: MIT */
#include "platform_api_vmcore.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    void *blocks[16];
    const size_t size = 65536;
    for (size_t i = 0; i < 16; ++i) {
        blocks[i] = os_mmap(NULL, size, MMAP_PROT_READ | MMAP_PROT_WRITE,
                            MMAP_MAP_32BIT, os_get_invalid_handle());
        assert(blocks[i] && (uintptr_t)blocks[i] < INT32_MAX);
        assert(size <= (uintptr_t)INT32_MAX + 1u - (uintptr_t)blocks[i]);
        ((uint8_t *)blocks[i])[0] = (uint8_t)i;
        ((uint8_t *)blocks[i])[size - 1] = (uint8_t)(i + 1);
    }
    assert(!os_mmap(NULL, SIZE_MAX, MMAP_PROT_READ, MMAP_MAP_32BIT,
                    os_get_invalid_handle()));
    assert(!os_mmap(NULL, (size_t)INT32_MAX + 1u, MMAP_PROT_READ,
                    MMAP_MAP_32BIT, os_get_invalid_handle()));
    assert(!os_mmap((void *)((uintptr_t)INT32_MAX + 1u), size,
                    MMAP_PROT_READ, MMAP_MAP_32BIT,
                    os_get_invalid_handle()));
    for (size_t i = 0; i < 16; ++i) {
        assert(((uint8_t *)blocks[i])[0] == i);
        assert(((uint8_t *)blocks[i])[size - 1] == i + 1);
        MEMORY_BASIC_INFORMATION region;
        assert(os_mprotect(blocks[i], size, MMAP_PROT_READ) == 0);
        assert(VirtualQuery(blocks[i], &region, sizeof(region)));
        assert(region.Protect == PAGE_READONLY);
        os_munmap(blocks[i], size);
    }
    assert(os_mprotect(NULL, size, MMAP_PROT_READ) == -1);
    void *ordinary = os_mmap(NULL, size, MMAP_PROT_READ | MMAP_PROT_WRITE,
                             MMAP_MAP_NONE, os_get_invalid_handle());
    assert(ordinary);
    os_munmap(ordinary, size);
    puts("Windows AOT: low mappings, multiple regions, bounds, protection and ordinary mappings passed");
}
