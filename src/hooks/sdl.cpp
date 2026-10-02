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
Uint32 (*get_mouse_state_original)(float*, float*) = nullptr;
Uint32 (*get_relative_mouse_state_original)(float*, float*) = nullptr;
bool (*set_window_relative_mouse_mode_original)(SDL_Window*, bool) = nullptr;
bool (*get_window_relative_mouse_mode_original)(SDL_Window*) = nullptr;
void (*warp_mouse_in_window_original)(SDL_Window*, float, float) = nullptr;
bool (*warp_mouse_global_original)(float, float) = nullptr;
bool (*show_cursor_original)(void) = nullptr;
bool (*hide_cursor_original)(void) = nullptr;
SDL_Cursor* (*set_cursor_original)(SDL_Cursor*) = nullptr;
bool (*set_window_mouse_grab_original)(SDL_Window*, bool) = nullptr;
bool (*set_window_mouse_rect_original)(SDL_Window*, const SDL_Rect*) = nullptr;

static bool g_game_wants_relative = true;
static bool g_last_block = false;

static bool block_game_mouse() {
    return menu_focused && !config.misc.input_passthrough;
}

static bool is_mouse_event(Uint32 t) {
    return t == SDL_EVENT_MOUSE_MOTION ||
           t == SDL_EVENT_MOUSE_BUTTON_DOWN ||
           t == SDL_EVENT_MOUSE_BUTTON_UP ||
           t == SDL_EVENT_MOUSE_WHEEL;
}

void sync_menu_mouse() {
    if (!sdl_window)
        return;

    const bool block = block_game_mouse();

    // only toggle relative-mode on the edge — calling it every frame flushes motion and feels like delay
    if (block && !g_last_block) {
        if (get_window_relative_mouse_mode_original)
            g_game_wants_relative = get_window_relative_mouse_mode_original(sdl_window);
        if (set_window_relative_mouse_mode_original)
            set_window_relative_mouse_mode_original(sdl_window, false);
        if (set_window_mouse_grab_original)
            set_window_mouse_grab_original(sdl_window, false);
        if (set_window_mouse_rect_original)
            set_window_mouse_rect_original(sdl_window, nullptr);
    } else if (!block && g_last_block) {
        if (set_window_relative_mouse_mode_original)
            set_window_relative_mouse_mode_original(sdl_window, g_game_wants_relative);
    }

    if (block && hide_cursor_original)
        hide_cursor_original();

    g_last_block = block;
}

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
    peep_events_original(&e, 1, SDL_ADDEVENT, 0, 0);
}

int peep_events_hook(SDL_Event* events, int numevents, SDL_EventAction action, int min, int max) {
    int ret = peep_events_original(events, numevents, action, min, max);

    sync_menu_mouse();

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

    // ImGui always sees events so the menu stays clickable
    if (ret > 0 && sdl_window && ImGui::GetCurrentContext()) {
        for (int i = 0; i < ret; ++i) {
            ImGui_ImplSDL3_ProcessEvent(&events[i]);
            get_input(&events[i]);
        }
    }

    if (ret > 0 && events && block_game_mouse() && action == SDL_GETEVENT) {
        int out = 0;
        for (int i = 0; i < ret; ++i) {
            if (is_mouse_event(events[i].type))
                continue;
            if (out != i)
                events[out] = events[i];
            ++out;
        }
        ret = out;
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

Uint32 get_mouse_state_hook(float* x, float* y) {
    Uint32 buttons = get_mouse_state_original(x, y);
    if (block_game_mouse())
        return 0;
    return buttons;
}

Uint32 get_relative_mouse_state_hook(float* x, float* y) {
    Uint32 buttons = get_relative_mouse_state_original(x, y);
    if (block_game_mouse()) {
        if (x) *x = 0.f;
        if (y) *y = 0.f;
        return 0;
    }
    return buttons;
}

bool set_window_relative_mouse_mode_hook(SDL_Window* window, bool enabled) {
    if (block_game_mouse()) {
        g_game_wants_relative = enabled;
        if (enabled)
            return true; // swallow re-enable from CS2
    }
    return set_window_relative_mouse_mode_original(window, enabled);
}

bool get_window_relative_mouse_mode_hook(SDL_Window* window) {
    if (block_game_mouse())
        return false; // ImGui then uses absolute coords (no rubber-banding)
    return get_window_relative_mouse_mode_original(window);
}

void warp_mouse_in_window_hook(SDL_Window* window, float x, float y) {
    if (block_game_mouse())
        return;
    warp_mouse_in_window_original(window, x, y);
}

bool warp_mouse_global_hook(float x, float y) {
    if (block_game_mouse())
        return true;
    return warp_mouse_global_original(x, y);
}

bool show_cursor_hook(void) {
    if (block_game_mouse())
        return true; // pretend shown, keep OS cursor hidden — ImGui draws its own
    return show_cursor_original();
}

bool hide_cursor_hook(void) {
    return hide_cursor_original();
}

SDL_Cursor* set_cursor_hook(SDL_Cursor* cursor) {
    if (block_game_mouse())
        return cursor;
    return set_cursor_original(cursor);
}

bool set_window_mouse_grab_hook(SDL_Window* window, bool grabbed) {
    if (block_game_mouse() && grabbed)
        return true;
    return set_window_mouse_grab_original(window, grabbed);
}

bool set_window_mouse_rect_hook(SDL_Window* window, const SDL_Rect* rect) {
    if (block_game_mouse())
        return true;
    return set_window_mouse_rect_original(window, rect);
}

void get_window_size_hook(SDL_Window* window, int* w, int* h) {
    sdl_window = window;
    get_window_size_original(window, w, h);
}

SDL_Window* get_keyboard_focus_hook(void) {
    return get_keyboard_focus_original();
}
