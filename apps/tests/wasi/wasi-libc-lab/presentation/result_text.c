#include "libc_lab.h"

#include <stdio.h>

int libc_lab_format(char* output, size_t capacity, const libc_lab_result_t* result) {
    int written;
    if (output == NULL || capacity == 0 || result == NULL)
        return 0;
    written = snprintf(output, capacity, "%u/%u checks passed", (unsigned)result->passed,
                       (unsigned)result->total);
    return written > 0 && (size_t)written < capacity;
}
