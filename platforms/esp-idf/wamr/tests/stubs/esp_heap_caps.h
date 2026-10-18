#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1u
#define MALLOC_CAP_8BIT 2u
#define MALLOC_CAP_EXEC 4u
void *heap_caps_malloc(size_t,unsigned);
size_t heap_caps_get_allocated_size(void *);
