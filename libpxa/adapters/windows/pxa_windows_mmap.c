/* SPDX-License-Identifier: MIT */
#include <stdint.h>
/* Keep the pinned Windows implementation and its original license intact. */
#define os_mmap pxa_windows_default_mmap
#define os_mprotect pxa_windows_default_mprotect
#include <win_memmap.c>
#undef os_mmap
#undef os_mprotect

void *os_mmap(void *hint, size_t size, int prot, int flags,
              os_file_handle file) {
#if defined(_WIN64)
    if (flags & MMAP_MAP_32BIT) {
        SYSTEM_INFO system;
        MEMORY_BASIC_INFORMATION region;
        const uintptr_t ceiling = (uintptr_t)INT32_MAX + 1u;
        uintptr_t cursor, granularity;
        size_t page_size = os_getpagesize();
        size_t request = (size + page_size - 1u) & ~(page_size - 1u);
        if (!size || request < size || request >= ceiling) return NULL;
        GetSystemInfo(&system);
        granularity = system.dwAllocationGranularity;
        if (!granularity) return NULL;
        cursor = hint ? (uintptr_t)hint : granularity;
        if (cursor >= ceiling || request > ceiling - cursor) return NULL;
        /* AOT ADDR32 relocations require low code/data addresses. Query whole
         * regions rather than probing every page; VirtualAlloc arbitrates
         * races without replacing existing allocations. Other mappings keep
         * the pinned implementation and its ordinary address selection. */
        while (cursor < ceiling && request <= ceiling - cursor) {
            uintptr_t end;
            if (!VirtualQuery((LPCVOID)cursor, &region, sizeof(region)))
                return NULL;
            end = (uintptr_t)region.BaseAddress + region.RegionSize;
            if (end <= cursor) return NULL;
            if (region.State == MEM_FREE && request <= end - cursor) {
                void *mapped = pxa_windows_default_mmap(
                    (void *)cursor, size, prot, flags, file);
                if (mapped) return mapped;
                /* A concurrent allocation may have changed this region. */
                end = cursor + granularity;
            }
            if (end > ceiling - (granularity - 1u)) return NULL;
            cursor = (end + granularity - 1u) / granularity * granularity;
        }
        return NULL;
    }
#endif
    return pxa_windows_default_mmap(hint, size, prot, flags, file);
}

int os_mprotect(void *addr, size_t size, int prot) {
    DWORD previous;
    size_t page_size = os_getpagesize();
    size_t request = (size + page_size - 1u) & ~(page_size - 1u);
    if (!addr || !size || request < size) return -1;
    /* Windows requires storage for the previous protection. Keep the common
     * platform API's zero-on-success contract. */
    return VirtualProtect(addr, request, access_to_win32_flags(prot), &previous)
        ? 0 : -1;
}
