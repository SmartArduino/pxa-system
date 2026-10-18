#include "wasm_export.h"
#include "wasm_runtime_common.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
  assert(argc == 2);
  FILE *f = fopen(argv[1], "rb");
  assert(f);
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  rewind(f);
  unsigned char *bytes = malloc((size_t)size);
  assert(bytes && fread(bytes, 1, (size_t)size, f) == (size_t)size);
  fclose(f);
  RuntimeInitArgs init = {0};
  init.mem_alloc_type = Alloc_With_System_Allocator;
  assert(wasm_runtime_full_init(&init));
  char error[256] = {0};
  wasm_module_t module =
      wasm_runtime_load(bytes, (uint32_t)size, error, sizeof error);
  if (!module) {
    fprintf(stderr, "load: %s\n", error);
    return 2;
  }
  wasm_module_inst_t instance =
      wasm_runtime_instantiate(module, 16384, 0, error, sizeof error);
  if (!instance) {
    fprintf(stderr, "instantiate: %s\n", error);
    return 2;
  }
  wasm_exec_env_t env = wasm_runtime_create_exec_env(instance, 16384);
  assert(env);
  const unsigned long actual =
      (unsigned long)(env->aux_stack_bottom - env->aux_stack_boundary);
  wasm_function_inst_t function =
      wasm_runtime_lookup_function(instance, "exercise");
  assert(function);
  uint32_t values[1] = {0};
  int ok = wasm_runtime_call_wasm(env, function, 0, values);
#if PXA_TEST_FULL_AUX_STACK
  assert(actual == 16384 && ok && values[0] == 522240);
#else
  assert(
      actual == 3264 && !ok &&
      strstr(wasm_runtime_get_exception(instance), "auxiliary stack overflow"));
#endif
  printf("{\"heap_aux\":%u,\"declared_bytes\":16384,\"main_usable_bytes\":%lu,"
         "\"guest_4096_byte_work_succeeded\":%s}\n",
         PXA_TEST_FULL_AUX_STACK, actual, ok ? "true" : "false");
  wasm_runtime_clear_exception(instance);
  wasm_function_inst_t thread =
      wasm_runtime_lookup_function(instance, "thread_work");
  if (thread) {
    wasm_function_inst_t counts =
        wasm_runtime_lookup_function(instance, "allocator_counts");
    assert(counts);
    uint32_t before[1] = {0}, after[1] = {0};
    assert(wasm_runtime_call_wasm(env, counts, 0, before));
    assert(before[0] == 0);
    assert(wasm_runtime_call_wasm(env, thread, 0, values) && values[0] == 3);
    assert(wasm_runtime_call_wasm(env, counts, 0, after));
    const uint32_t expected = PXA_TEST_FULL_AUX_STACK ? 3 : 0;
    assert((after[0] >> 16) - (before[0] >> 16) == expected);
    assert((after[0] & 65535) - (before[0] & 65535) == expected);
    printf("{\"joined_threads\":3,\"lazy_stack_allocations\":%u,\"stack_"
           "releases\":%u}\n",
           expected, expected);
  }
  wasm_runtime_destroy_exec_env(env);
  wasm_runtime_deinstantiate(instance);
  wasm_runtime_unload(module);
  wasm_runtime_destroy();
  free(bytes);
  return 0;
}
