#ifndef PXSYS_TYPES_H
#define PXSYS_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PXSYS_PUBLISHER_ROOT_BYTES ((size_t)32)

typedef struct {
    const uint8_t* data;
    size_t size;
} pxsys_bytes_t;

typedef struct {
    const char* data;
    size_t size;
} pxsys_string_t;

typedef struct {
    uint8_t publisher_root[PXSYS_PUBLISHER_ROOT_BYTES];
    pxsys_string_t app_id;
} pxsys_app_identity_t;

typedef struct {
    uint32_t struct_size;
    pxsys_app_identity_t app;
    pxsys_string_t component_id;
} pxsys_caller_t;

typedef struct {
    uint16_t major;
    uint16_t minor;
} pxsys_version_t;

typedef void* (*pxsys_allocate_fn)(void* context, size_t size);
typedef void (*pxsys_release_fn)(void* context, void* memory);

typedef struct {
    uint32_t struct_size;
    void* context;
    pxsys_allocate_fn allocate;
    pxsys_release_fn release;
} pxsys_allocator_t;

pxsys_bytes_t pxsys_bytes(const void* data, size_t size);
pxsys_string_t pxsys_string(const char* data, size_t size);
pxsys_string_t pxsys_string_from_cstr(const char* text);
int pxsys_identifier_validate(pxsys_string_t value, size_t maximum);
int pxsys_display_text_validate(pxsys_string_t value, size_t maximum);

#ifdef __cplusplus
}
#endif

#endif
