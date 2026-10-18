#ifndef PXSYS_DESKTOP_CONTROLLER_H
#define PXSYS_DESKTOP_CONTROLLER_H
#include <stdio.h>
#include <stdint.h>
#include <SDL2/SDL.h>
#include "pxa/ui.h"

typedef struct {
    SDL_atomic_t keyboard;
    SDL_GameController *pad;
    uint32_t window_id, next_scan;
} desktop_controller_t;

static uint32_t desktop_controller_key(SDL_Keycode key) {
    switch (key) {
    case SDLK_UP: return PXA_UI_CONTROLLER_UP;
    case SDLK_DOWN: return PXA_UI_CONTROLLER_DOWN;
    case SDLK_LEFT: return PXA_UI_CONTROLLER_LEFT;
    case SDLK_RIGHT: return PXA_UI_CONTROLLER_RIGHT;
    case SDLK_z: return PXA_UI_CONTROLLER_A;
    case SDLK_x: return PXA_UI_CONTROLLER_B;
    case SDLK_RETURN: return PXA_UI_CONTROLLER_START;
    case SDLK_TAB: return PXA_UI_CONTROLLER_SELECT;
    case SDLK_a: return PXA_UI_CONTROLLER_X;
    case SDLK_s: return PXA_UI_CONTROLLER_Y;
    case SDLK_q: return PXA_UI_CONTROLLER_L;
    case SDLK_w: return PXA_UI_CONTROLLER_R;
    default: return 0;
    }
}
static int SDLCALL desktop_controller_watch(void *context, SDL_Event *event) {
    desktop_controller_t *c = context;
    if ((event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) &&
        event->key.windowID == c->window_id) {
        const int bit = (int)desktop_controller_key(event->key.keysym.sym);
        int old, next;
        do {
            old = SDL_AtomicGet(&c->keyboard);
            next = event->type == SDL_KEYDOWN ? old | bit : old & ~bit;
        } while (!SDL_AtomicCAS(&c->keyboard, old, next));
    } else if (event->type == SDL_WINDOWEVENT &&
        event->window.windowID == c->window_id &&
        event->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        SDL_AtomicSet(&c->keyboard, 0);
    }
    return 0;
}
static uint32_t desktop_controller_pad(SDL_GameController *pad) {
    static const SDL_GameControllerButton sdl[] = {
        SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
        SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
        SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
        SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK,
        SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
    };
    uint32_t buttons = 0;
    for (unsigned i=0; i<sizeof(sdl)/sizeof(sdl[0]); ++i)
        if (SDL_GameControllerGetButton(pad, sdl[i])) buttons |= UINT32_C(1)<<i;
    /* Stick deflection becomes the same digital directions as the device.
     * A quarter-range dead zone prevents idle stick drift. */
    const int x = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    const int y = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    if (x < -8192) buttons |= PXA_UI_CONTROLLER_LEFT;
    if (x > 8192) buttons |= PXA_UI_CONTROLLER_RIGHT;
    if (y < -8192) buttons |= PXA_UI_CONTROLLER_UP;
    if (y > 8192) buttons |= PXA_UI_CONTROLLER_DOWN;
    return buttons;
}
static void desktop_controller_init(desktop_controller_t *c, uint32_t window) {
    c->window_id = window;
    (void)SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    SDL_AddEventWatch(desktop_controller_watch, c);
    fprintf(stderr, "PXA controller: arrows=direction Z=A X=B A=X S=Y Q=L W=R Enter=Start Tab=Select; SDL gamepad/left stick enabled\n");
}
static uint32_t desktop_controller_poll(desktop_controller_t *c, int focused) {
    const uint32_t now = SDL_GetTicks();
    if (c->pad && !SDL_GameControllerGetAttached(c->pad)) {
        SDL_GameControllerClose(c->pad); c->pad = NULL; c->next_scan = now;
    }
    if (!c->pad && (int32_t)(now-c->next_scan) >= 0) {
        c->next_scan = now+1000;
        for (int i=0; i<SDL_NumJoysticks(); ++i) if (SDL_IsGameController(i)) {
            c->pad = SDL_GameControllerOpen(i);
            if (c->pad) break;
        }
    }
    if (!focused) {SDL_AtomicSet(&c->keyboard, 0); return 0;}
    SDL_GameControllerUpdate();
    return (uint32_t)SDL_AtomicGet(&c->keyboard) |
           (c->pad ? desktop_controller_pad(c->pad) : 0);
}
static void desktop_controller_deinit(desktop_controller_t *c) {
    SDL_DelEventWatch(desktop_controller_watch, c);
    if (c->pad) SDL_GameControllerClose(c->pad);
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}
#endif
