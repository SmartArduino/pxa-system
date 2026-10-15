#include "../volume_key_input.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    pxsys_volume_key_input_t input = {0};
    SDL_Event event = {0};
    event.type = SDL_KEYDOWN; event.key.keysym.sym = SDLK_VOLUMEUP;
    assert(pxsys_volume_key_from_sdl(&event) == 1);
    event.type = SDL_KEYUP; assert(pxsys_volume_key_from_sdl(&event) == 3);
    event.key.keysym.sym = SDLK_VOLUMEDOWN;
    assert(pxsys_volume_key_from_sdl(&event) == 4);
    event.type = SDL_KEYDOWN; assert(pxsys_volume_key_from_sdl(&event) == 2);
    event.key.repeat = 1; assert(!pxsys_volume_key_from_sdl(&event));
    event.type = SDL_USEREVENT; assert(!pxsys_volume_key_from_sdl(&event));
    assert(!pxsys_volume_key_push(&input, 0) && !pxsys_volume_key_push(&input, 5));
    for (unsigned round = 0; round < 5; ++round) {
        // Multiple taps in one UI turn must retain press/release ordering.
        for (unsigned i = 0; i < 32; ++i) assert(pxsys_volume_key_push(&input, i % 4 + 1));
        assert(!pxsys_volume_key_push(&input, 1));
        for (unsigned i = 0; i < 32; ++i) {
            assert(pxsys_volume_key_front(&input) == i % 4 + 1);
            pxsys_volume_key_pop(&input);
        }
        assert(!pxsys_volume_key_front(&input)); pxsys_volume_key_pop(&input);
    }
    puts("Volume key input: mapping, repeated taps, wraparound and capacity OK");
}
