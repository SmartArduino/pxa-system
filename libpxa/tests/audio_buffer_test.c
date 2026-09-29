#undef NDEBUG
#include "pxa/audio_buffer.h"
#include <assert.h>
int main(void) {
    pxa_audio_buffer_t b={0}; pxa_audio_buffer_start(&b,4096);
    assert(!pxa_audio_buffer_publish(&b,512,0));
    assert(!pxa_audio_buffer_take(&b,512,320,0) && !b.underruns && !b.missing_samples);
    assert(pxa_audio_buffer_publish(&b,4096,0));
    assert(!pxa_audio_buffer_publish(&b,8192,0));
    assert(pxa_audio_buffer_take(&b,8192,320,0)==320);
    assert(b.low_water_valid && b.low_water==7872);
    assert(pxa_audio_buffer_take(&b,100,320,0)==100 && b.underruns==1 && b.missing_samples==220);
    assert(!pxa_audio_buffer_publish(&b,4000,0));
    assert(!pxa_audio_buffer_take(&b,4000,320,0) && b.underruns==1 && b.missing_samples==540);
    assert(!pxa_audio_buffer_publish(&b,4096,0) && b.recoveries==1);
    assert(pxa_audio_buffer_take(&b,4096,320,0)==320);
    pxa_audio_buffer_start(&b,4096);
    assert(pxa_audio_buffer_publish(&b,100,1));
    assert(pxa_audio_buffer_take(&b,100,320,1)==100 && b.underruns==1);
    assert(!pxa_audio_buffer_take(&b,0,320,1) && b.underruns==1);
    assert(b.high_water==8192 && b.consumed_samples==840 && b.low_water==0);
    /* EOF must release a small tail after an underrun as well as at startup. */
    pxa_audio_buffer_start(&b,4096);
    assert(pxa_audio_buffer_publish(&b,4096,0));
    assert(pxa_audio_buffer_take(&b,4096,5000,0)==4096);
    assert(!pxa_audio_buffer_publish(&b,32,1) && b.recoveries==2);
    assert(pxa_audio_buffer_take(&b,32,320,1)==32 && b.underruns==2);
    return 0;
}
