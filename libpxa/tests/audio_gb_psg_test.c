#undef NDEBUG
#include "pxa/audio_gb_psg.h"
#include "pxa/wire.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
static uint8_t memory[1024], saved[1024], once[1024], command[16];
enum { STATE = 16, OUTPUT = 256 };
static void setup(unsigned frames) {
    memset(memory, 0, sizeof(memory)); memset(command, 0, sizeof(command));
    pxa_write_u32(command, STATE); pxa_write_u32(command + 4, OUTPUT);
    pxa_write_u16(command + 8, frames); pxa_write_u16(command + 10, 1);
    uint8_t *s = memory + STATE;
    pxa_write_u16(s, 1); pxa_write_u32(s + 8, 64); pxa_write_u32(s + 12, 2);
    pxa_write_u16(s + 24, 256); s[26] = s[27] = 7; s[28] = 1;
    pxa_write_u32(s + 40, 16384); pxa_write_u32(s + 56, 16384);
    pxa_write_u32(s + 72, 8192); pxa_write_u32(s + 100, 16);
}
static pxa_status_t run(void) { return pxa_audio_gb_psg_render(command, 16, memory, sizeof(memory)); }
static void rejected(pxa_status_t expected) {
    memcpy(saved, memory, sizeof(memory)); assert(run() == expected);
    assert(!memcmp(saved, memory, sizeof(memory)));
}
int main(void) {
    setup(128); assert(!run());
    assert(pxa_read_u16(memory + OUTPUT) == (uint16_t)-6144);
    assert(pxa_read_u32(memory + STATE + 4) == 8192);
    memcpy(once, memory, sizeof(memory));
    setup(16); assert(!run());
    pxa_write_u32(command + 4, OUTPUT + 16 * 4); pxa_write_u16(command + 8, 112);
    assert(!run()); assert(!memcmp(memory, once, sizeof(memory)));
    setup(32);
    for (unsigned b = 0; b < 128; ++b) {
        uint8_t old = memory[STATE + b]; memory[STATE + b] = 255;
        memcpy(saved, memory, sizeof(memory));
        if (run()) assert(!memcmp(saved, memory, sizeof(memory)));
        memcpy(memory, saved, sizeof(memory)); memory[STATE + b] = old;
    }
    memory[STATE] = 2; rejected(PXA_STATUS_UNSUPPORTED); memory[STATE] = 1;
    memory[STATE + 120] = 1; rejected(PXA_STATUS_INVALID_ARGUMENT); memory[STATE + 120] = 0;
    pxa_write_u32(command + 4, STATE); rejected(PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u32(command + 4, UINT32_MAX); rejected(PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u32(command + 4, OUTPUT);
    pxa_write_u16(command + 8, 129); rejected(PXA_STATUS_LIMIT_EXCEEDED);
    pxa_write_u16(command + 8, 0); rejected(PXA_STATUS_INVALID_ARGUMENT);
    pxa_write_u16(command + 8, 32); command[12] = 1; rejected(PXA_STATUS_INVALID_ARGUMENT);
    command[12] = 0;
    /* The positive signed-time interval can end just beyond INT32_MAX.
     * Work validation must consider the interval before that sign boundary. */
    memory[STATE + 29] = 8; pxa_write_u32(memory + STATE + 4, INT32_MAX - 31);
    rejected(PXA_STATUS_LIMIT_EXCEEDED);
    setup(128); memory[STATE + 29] = 8;
    pxa_write_u32(memory + STATE + 4, 4194304 - 127 * 64);
    assert(!run()); // Exactly 262144 noise steps.
    puts("GB PSG: DC/filter golden, split continuity, atomic rejection, range/work/time-wrap limits passed");
}
