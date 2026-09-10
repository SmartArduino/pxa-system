#include <string.h>

#include "libc_lab.h"

int main(void) {
    char summary[48];
    libc_lab_result_t result = libc_lab_run();
    if (result.passed != 8 || result.total != 8) return 1;
    if (!libc_lab_format(summary, sizeof(summary), &result)) return 1;
    return strcmp(summary, "8/8 checks passed") == 0 ? 0 : 1;
}
