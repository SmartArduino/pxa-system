#include "libc_lab.h"

#include <stdlib.h>
#include <string.h>

uint32_t libc_lab_check_strings(void) {
    const char text[] = "capability-runtime";
    char* end = NULL;
    uint32_t passed = 0;

    passed += strlen(text) == 18u;
    passed += strcmp(text + 11, "runtime") == 0;
    passed += strstr(text, "ability") == text + 3;
    passed += strtol("2048px", &end, 10) == 2048 && end != NULL && strcmp(end, "px") == 0;
    return passed;
}
