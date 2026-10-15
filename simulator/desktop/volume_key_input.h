#ifndef PXSYS_VOLUME_KEY_INPUT_H
#define PXSYS_VOLUME_KEY_INPUT_H

#include "SDL.h"
#include <stdbool.h>
#include <stdint.h>

/* SDL watchers may run outside the UI thread. Keep press/release order,
 * including multiple taps in one UI turn, without allocating input events. */
typedef struct {
    SDL_SpinLock lock;
    uint8_t keys[32];
    uint8_t head, count;
} pxsys_volume_key_input_t;

static inline uint32_t pxsys_volume_key_from_sdl(const SDL_Event* event) {
    if ((event->type != SDL_KEYDOWN && event->type != SDL_KEYUP) || event->key.repeat)
        return 0;
    uint32_t key = event->key.keysym.sym == SDLK_VOLUMEUP ? 1u :
                   event->key.keysym.sym == SDLK_VOLUMEDOWN ? 2u : 0u;
    return key && event->type == SDL_KEYUP ? key + 2u : key;
}

static inline bool pxsys_volume_key_push(pxsys_volume_key_input_t* input, uint32_t key) {
    if (key < 1 || key > 4) return false;
    SDL_AtomicLock(&input->lock);
    const bool fits = input->count < sizeof(input->keys);
    if (fits) input->keys[(input->head + input->count++) % sizeof(input->keys)] = (uint8_t)key;
    SDL_AtomicUnlock(&input->lock);
    return fits;
}

static inline uint32_t pxsys_volume_key_front(pxsys_volume_key_input_t* input) {
    SDL_AtomicLock(&input->lock);
    const uint32_t key = input->count ? input->keys[input->head] : 0;
    SDL_AtomicUnlock(&input->lock);
    return key;
}

static inline void pxsys_volume_key_pop(pxsys_volume_key_input_t* input) {
    SDL_AtomicLock(&input->lock);
    if (input->count) {
        input->head = (input->head + 1u) % sizeof(input->keys);
        --input->count;
    }
    SDL_AtomicUnlock(&input->lock);
}

#endif
