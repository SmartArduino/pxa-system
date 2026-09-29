#include "pxa/audio_buffer.h"
void pxa_audio_buffer_start(pxa_audio_buffer_t *b,uint32_t threshold) {
    b->threshold=threshold ? threshold : 1; b->started=0; b->buffering=1;
}
int pxa_audio_buffer_publish(pxa_audio_buffer_t *b,uint32_t available,int eof) {
    if (b->high_water<available) b->high_water=available;
    if (b->buffering && (available>=b->threshold || (eof && available))) {
        b->buffering=0;
        if (!b->started) { b->started=1; return 1; }
        ++b->recoveries;
    }
    return 0;
}
uint32_t pxa_audio_buffer_take(pxa_audio_buffer_t *b,uint32_t available,uint32_t wanted,int eof) {
    if (!b->started) return 0;
    if (b->buffering) { if (!eof) b->missing_samples+=wanted; return 0; }
    uint32_t count=available<wanted ? available : wanted;
    if (!eof && (!b->low_water_valid || available-count<b->low_water)) {
        b->low_water=available-count;
        b->low_water_valid=1;
    }
    b->consumed_samples+=count;
    if (count<wanted && !eof) {
        ++b->underruns; b->missing_samples+=wanted-count; b->buffering=1;
    }
    return count;
}
