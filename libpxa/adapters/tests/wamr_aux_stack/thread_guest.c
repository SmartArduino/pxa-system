#include "guest.c"

__attribute__((import_module("env"), import_name("pthread_create"))) int
create(uint32_t *thread, const void *attributes, uint32_t function,
       uint32_t argument);
__attribute__((import_module("env"), import_name("pthread_join"))) int
join(uint32_t thread, uint32_t *result);

extern unsigned char __heap_base;
static uint32_t heap_next, allocations, releases;

/* Small LIFO allocator for this fixture's one-at-a-time child stacks only. */
__attribute__((export_name("malloc"))) void *allocate(uint32_t size) {
  if (!heap_next)
    heap_next = (uint32_t)(uintptr_t)&__heap_base;
  const uint32_t start = (heap_next + 15) & ~15u;
  const uint32_t end = start + size;
  if (end < start || end > __builtin_wasm_memory_size(0) * 65536u)
    return 0;
  heap_next = end;
  ++allocations;
  return (void *)(uintptr_t)start;
}
__attribute__((export_name("free"))) void release(void *pointer) {
  if (pointer) {
    heap_next = (uint32_t)(uintptr_t)pointer;
    ++releases;
  }
}
static uint32_t child(uint32_t argument) { return argument + 5; }
__attribute__((export_name("thread_work"))) uint32_t thread_work(void) {
  for (uint32_t i = 0; i < 3; ++i) {
    uint32_t thread = 0, result = 0;
    if (create(&thread, 0, (uint32_t)(uintptr_t)child, 17 + i) ||
        join(thread, &result) || result != 22 + i)
      return 0;
  }
  return 3;
}
__attribute__((export_name("allocator_counts"))) uint32_t
allocator_counts(void) {
  return (allocations << 16) | releases;
}
