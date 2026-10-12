#ifndef PXSYS_PNG_WRITER_H
#define PXSYS_PNG_WRITER_H

#include <png.h>
#include <stdio.h>

/* Keep libpng's longjmp in a separate scope from the caller's allocations
 * and cleanup. Parameters remain unchanged across the error jump. */
static inline int pxsys_png_write(png_structp png, png_infop info, FILE *stream,
    png_uint_32 width, png_uint_32 height, png_bytep *rows) {
    if (setjmp(png_jmpbuf(png))) return 0;
    png_init_io(png,stream);
    png_set_IHDR(png,info,width,height,8,PNG_COLOR_TYPE_RGBA,
        PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png,info);
    png_write_image(png,rows);
    png_write_end(png,NULL);
    return 1;
}
#endif
