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

int peep_events_hook(SDL_Event* events, int numevents, SDL_EventAction action, int min, int max) {
    int ret = peep_events_original(events, numevents, action, min, max);
    if (ret > 0 && sdl_window && ImGui::GetCurrentContext()) {
        for (int i = 0; i < ret; ++i) {
            if (g_bhop_eat_space &&
                (events[i].type == SDL_EVENT_KEY_DOWN || events[i].type == SDL_EVENT_KEY_UP) &&
                events[i].key.scancode == SDL_SCANCODE_SPACE) {
                events[i].type = SDL_EVENT_KEY_UP;
                events[i].key.down = false;
                events[i].key.repeat = false;
            }
            ImGui_ImplSDL3_ProcessEvent(&events[i]);
            get_input(&events[i]);
        }
    }
    return ret;
}

const bool* get_keyboard_state_hook(int* nkeys) {
    const bool* keys = get_keyboard_state_original(nkeys);
    if (!keys) return keys;

    static bool copy[512];
    int n = 512;
    if (nkeys && *nkeys > 0 && *nkeys <= 512) n = *nkeys;
    memcpy(copy, keys, (size_t)n);

    if (g_bhop_eat_space && n > (int)SDL_SCANCODE_SPACE)
        copy[SDL_SCANCODE_SPACE] = false;

    return copy;
}

void get_window_size_hook(SDL_Window* window, int* w, int* h) {
    sdl_window = window;
    get_window_size_original(window, w, h);
}

SDL_Window* get_keyboard_focus_hook(void) {
    return get_keyboard_focus_original();
}
