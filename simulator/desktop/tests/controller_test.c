#define _POSIX_C_SOURCE 200809L
#include "../desktop_controller.h"
#include <assert.h>

static void key(desktop_controller_t *c, SDL_Keycode code, int down) {
    SDL_Event e = {0};
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.windowID = c->window_id; e.key.keysym.sym = code;
    assert(SDL_PushEvent(&e) == 1);
}
int main(void) {
    desktop_controller_t c = {0}; SDL_Event event = {0};
    assert(!SDL_Init(SDL_INIT_VIDEO));
    SDL_Window *window = SDL_CreateWindow("controller test", 0, 0, 64, 64, 0);
    assert(window); desktop_controller_init(&c, SDL_GetWindowID(window));
    key(&c, SDLK_UP, 1); key(&c, SDLK_z, 1);
    assert(desktop_controller_poll(&c, 1) == (PXA_UI_CONTROLLER_UP | PXA_UI_CONTROLLER_A));
    key(&c, SDLK_z, 0);
    assert(desktop_controller_poll(&c, 1) == PXA_UI_CONTROLLER_UP);
    event.type = SDL_WINDOWEVENT; event.window.windowID = c.window_id;
    event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    assert(SDL_PushEvent(&event) == 1); assert(!desktop_controller_poll(&c, 1));
    int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,
        SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
    assert(index >= 0 && SDL_IsGameController(index));
    SDL_Joystick *joystick = SDL_JoystickOpen(index); assert(joystick);
    c.next_scan = 0; (void)desktop_controller_poll(&c, 1); assert(c.pad);
    assert(!SDL_JoystickSetVirtualButton(joystick, SDL_CONTROLLER_BUTTON_A, 1));
    assert(!SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_LEFTX, 16000));
    SDL_JoystickUpdate();
    assert(desktop_controller_poll(&c, 1) == (PXA_UI_CONTROLLER_A | PXA_UI_CONTROLLER_RIGHT));
    assert(!desktop_controller_poll(&c, 0));
    assert(!SDL_JoystickSetVirtualAxis(joystick, SDL_CONTROLLER_AXIS_LEFTX, 4000));
    assert(!SDL_JoystickSetVirtualButton(joystick, SDL_CONTROLLER_BUTTON_A, 0));
    SDL_JoystickUpdate(); assert(!desktop_controller_poll(&c, 1));
    assert(!SDL_JoystickDetachVirtual(index)); SDL_JoystickClose(joystick);
    assert(!desktop_controller_poll(&c, 1) && !c.pad);
    desktop_controller_deinit(&c); SDL_DestroyWindow(window); SDL_Quit();
    puts("controller: chords, releases, focus reset, virtual SDL pad, stick deadzone, disconnect passed");
    return 0;
}
