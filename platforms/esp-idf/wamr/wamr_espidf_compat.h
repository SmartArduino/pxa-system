#pragma once

/* Declarations WAMR 2.4.x no longer receives through ESP-IDF headers. */
#include <stdio.h>
#include <sys/stat.h>

/* WAMR's ESP-IDF adapter defines renameat(), but recent libc headers do not
 * declare it for espidf_file.c. */
#ifdef ESP_PLATFORM
#include "freertos/idf_additions.h"

int renameat(int old_dirfd, const char *old_path,
             int new_dirfd, const char *new_path);

/* WAMR AF07 uses the older unprefixed spelling. ESP-IDF exports the same
 * current-task stack-base helper as pxTaskGetStackStart(). */
#define xTaskGetStackStart pxTaskGetStackStart
#endif
