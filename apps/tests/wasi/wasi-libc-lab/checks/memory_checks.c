#include "libc_lab.h"

#include <stdlib.h>
#include <string.h>

uint32_t libc_lab_check_memory(void) {
    static const uint8_t expected[] = {1, 1, 2, 3, 4, 5, 6, 8};
    uint8_t* buffer = (uint8_t*)calloc(8, 1);
    uint8_t* resized;
    uint8_t source[] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint32_t passed = 0;

    if (buffer == NULL)
        return 0;
    passed += buffer[0] == 0 && buffer[7] == 0;
    memcpy(buffer, source, sizeof(source));
    passed += memcmp(buffer, source, sizeof(source)) == 0;
    memmove(buffer + 1, buffer, 6);
    passed += memcmp(buffer, expected, sizeof(expected)) == 0;
    resized = (uint8_t*)realloc(buffer, 16);
    passed += resized != NULL;
    free(resized != NULL ? resized : buffer);
    return passed;
}
