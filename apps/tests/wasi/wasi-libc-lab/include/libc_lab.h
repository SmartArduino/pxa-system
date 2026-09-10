#ifndef PXA_WASI_LIBC_LAB_H
#define PXA_WASI_LIBC_LAB_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t passed;
    uint32_t total;
} libc_lab_result_t;

uint32_t libc_lab_check_strings(void);
uint32_t libc_lab_check_memory(void);
libc_lab_result_t libc_lab_run(void);
int libc_lab_format(char *output, size_t capacity,
                    const libc_lab_result_t *result);

#endif
