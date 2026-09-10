#include "libc_lab.h"

libc_lab_result_t libc_lab_run(void) {
    libc_lab_result_t result;
    result.passed = libc_lab_check_strings() + libc_lab_check_memory();
    result.total = 8;
    return result;
}
