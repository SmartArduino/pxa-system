#ifndef PXSYS_PNG_WRITER_H
#define PXSYS_PNG_WRITER_H

#include <png.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *data;
    size_t size,capacity;
} pxsys_png_memory_t;

static void pxsys_png_file_write(png_structp png,png_bytep data,png_size_t size) {
    FILE *stream=png_get_io_ptr(png);
    if(fwrite(data,1,size,stream)!=size)png_error(png,"PNG write failed");
}

static void pxsys_png_append(png_structp png,png_bytep data,png_size_t size) {
    pxsys_png_memory_t *buffer=png_get_io_ptr(png);
    if(size>SIZE_MAX-buffer->size)png_error(png,"PNG size overflow");
    size_t required=buffer->size+size;
    if(required>buffer->capacity) {
        size_t capacity=buffer->capacity?buffer->capacity:4096;
        while(capacity<required)capacity=capacity>SIZE_MAX/2?required:capacity*2;
        uint8_t *next=realloc(buffer->data,capacity);
        if(!next)png_error(png,"PNG allocation failed");
        buffer->data=next;buffer->capacity=capacity;
    }
    memcpy(buffer->data+buffer->size,data,size);buffer->size+=size;
}

/* The caller owns the buffer even on failure; keep allocations outside the
 * stack frame containing setjmp. No POSIX memory stream or temporary file. */
static inline int pxsys_png_write_memory(png_structp png,png_infop info,
    pxsys_png_memory_t *buffer,png_uint_32 width,png_uint_32 height,png_bytep *rows) {
    if(setjmp(png_jmpbuf(png)))return 0;
    png_set_write_fn(png,buffer,pxsys_png_append,NULL);
    png_set_IHDR(png,info,width,height,8,PNG_COLOR_TYPE_RGBA,
        PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png,info);png_write_image(png,rows);png_write_end(png,NULL);
    return 1;
}

/* Keep libpng's longjmp in a separate scope from the caller's allocations
 * and cleanup. Parameters remain unchanged across the error jump. */
static inline int pxsys_png_write(png_structp png, png_infop info, FILE *stream,
    png_uint_32 width, png_uint_32 height, png_bytep *rows) {
    if (setjmp(png_jmpbuf(png))) return 0;
    /* Keep FILE operations in the caller's C runtime, including when a
     * dependency DLL was built with a different Windows runtime. */
    png_set_write_fn(png,stream,pxsys_png_file_write,NULL);
    png_set_IHDR(png,info,width,height,8,PNG_COLOR_TYPE_RGBA,
        PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png,info);
    png_write_image(png,rows);
    png_write_end(png,NULL);
    return 1;
}
#endif
