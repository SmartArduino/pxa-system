#undef NDEBUG
#include "../png_writer.h"
#include <assert.h>

static int write_image(FILE *stream) {
    png_structp png=png_create_write_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);
    assert(png);
    png_infop info=png_create_info_struct(png); assert(info);
    png_byte pixel[]={255,128,0,255}; png_bytep rows[]={pixel};
    int result=pxsys_png_write(png,info,stream,1,1,rows);
    png_destroy_write_struct(&png,&info);
    return result;
}
int main(void) {
    FILE *stream=tmpfile(); assert(stream);
    assert(write_image(stream)); rewind(stream);
    png_byte signature[8]; assert(fread(signature,1,sizeof(signature),stream)==sizeof(signature));
    assert(!png_sig_cmp(signature,0,sizeof(signature))); assert(!fclose(stream));
    // Real I/O failure must return through libpng's error jump safely.
    stream=fopen("/dev/full","wb"); assert(stream);
    assert(!setvbuf(stream,NULL,_IONBF,0));
    assert(!write_image(stream)); (void)fclose(stream);
    // Error handling leaves later captures usable.
    stream=tmpfile(); assert(stream); assert(write_image(stream)); assert(!fclose(stream));
    puts("PNG capture: valid output, write failure and recovery passed");
}
