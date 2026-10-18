#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef int os_file_handle;
#define MMAP_PROT_EXEC 4
#define MMAP_PROT_NONE 0
#define MMAP_MAP_NONE 0
void *os_mmap(void *,size_t,int,int,os_file_handle);
void *os_mremap(void *,size_t,size_t);
void *os_mremap_slow(void *,size_t,size_t);
void os_munmap(void *,size_t);
void os_free(void *);
