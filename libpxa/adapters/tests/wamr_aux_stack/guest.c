#include <stdint.h>
volatile uint32_t sink;
__attribute__((export_name("exercise"), noinline)) uint32_t exercise(void) {
  volatile unsigned char bytes[4096];
  uint32_t total = 0;
  for (uint32_t i = 0; i < sizeof bytes; ++i)
    bytes[i] = (unsigned char)i;
  for (uint32_t i = 0; i < sizeof bytes; ++i)
    total += bytes[i];
  sink = total;
  return total;
}
