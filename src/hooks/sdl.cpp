#include "hooks.hpp"
#include "../gui/menu.hpp"
#include "../gui/config.hpp"
#include "imgui_impl_sdl3.h"
#include <SDL3/SDL.h>
#include <cstring>

int  (*peep_events_original)(SDL_Event*, int, SDL_EventAction, int, int) = nullptr;
void (*get_window_size_original)(SDL_Window* window, int* w, int* h) = nullptr;
SDL_Window* (*get_keyboard_focus_original)(void) = nullptr;
const bool* (*get_keyboard_state_original)(int*) = nullptr;

void bhop_inject_space(bool down) {
    if (!peep_events_original) return;
    SDL_Event e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.type = (SDL_EventType)e.type;
    e.key.scancode = SDL_SCANCODE_SPACE;
    e.key.key = SDLK_SPACE;
    e.key.down = down;
    e.key.repeat = false;
    e.key.timestamp = SDL_GetTicksNS();
    if (sdl_window)
        e.key.windowID = SDL_GetWindowID(sdl_window);
    // original, NOT SDL_PushEvent — PushEvent re-enters this hook and crashed you
    peep_events_original(&e, 1, SDL_ADDEVENT, 0, 0);
}

int peep_events_hook(SDL_Event* events, int numevents, SDL_EventAction action, int min, int max) {
    int ret = peep_events_original(events, numevents, action, min, max);

    // while we have faked a release, eat real SPACE downs so the bind stays up
    if (ret > 0 && events && config.movement.bhop && g_bhop_released && action == SDL_GETEVENT) {
        for (int i = 0; i < ret; ++i) {
            if (events[i].type == SDL_EVENT_KEY_DOWN &&
                events[i].key.scancode == SDL_SCANCODE_SPACE &&
                !events[i].key.repeat) {
                events[i].type = SDL_EVENT_KEY_UP;
                events[i].key.type = SDL_EVENT_KEY_UP;
                events[i].key.down = false;
            }
        }
    }

    if (ret > 0 && sdl_window && ImGui::GetCurrentContext()) {
        for (int i = 0; i < ret; ++i) {
            ImGui_ImplSDL3_ProcessEvent(&events[i]);
            get_input(&events[i]);
        }
    }
    return ret;
}

const bool* get_keyboard_state_hook(int* nkeys) {
    const bool* orig = get_keyboard_state_original(nkeys);
    if (!orig) return orig;
    if (!config.movement.bhop || !g_bhop_released)
        return orig;

    static bool keys[512];
    int n = 512;
    if (nkeys && *nkeys > 0 && *nkeys < 512)
        n = *nkeys;
    memcpy(keys, orig, (size_t)n * sizeof(bool));
    if (SDL_SCANCODE_SPACE < n)
        keys[SDL_SCANCODE_SPACE] = false;
    return keys;
}

void get_window_size_hook(SDL_Window* window, int* w, int* h) {
    sdl_window = window;
    get_window_size_original(window, w, h);
}

SDL_Window* get_keyboard_focus_hook(void) {
    return get_keyboard_focus_original();
}
