#pragma once
#include <SDL3/SDL.h>
#include "../core/vec.hpp"

struct Keybind {
    int  button = 0;
    bool waiting = false;
};

inline bool is_down(const Keybind& b) {
    if (b.button >= 0) {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        return keys && keys[b.button];
    }
    return SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_MASK(-b.button);
}

class Pawn;

struct Config {
    struct {
        bool    master = false;
        Keybind key{ -SDL_BUTTON_X1 };
        float   fov = 5.f;
        float   smooth = 5.f;
        bool    draw_fov = false;
        bool    recoil = true;
        bool    auto_shoot = false;
    } aimbot;

    struct {
        bool master = false;
        bool skip_team = true;
        struct {
            bool       box = true;
            RGBA_float box_color{1.f, 0.2f, 0.2f, 1.f};
            bool       health_bar = true;
            bool       health_text = false;
            bool       name = false;
            RGBA_float name_color{1, 1, 1, 1};
            bool       skeleton = false;
            RGBA_float skeleton_color{1, 1, 1, 1};
            bool       target_indicator = true;
            RGBA_float target_color{1, 0, 1, 1};
        } player;
    } esp;

    struct {
        bool    override_fov = false;
        float   custom_fov = 90;
        int     custom_viewmodel_fov = 90;
        struct {
            Keybind key{ SDL_SCANCODE_LALT };
            bool    enabled = false;
        } thirdperson;
        struct {
            bool       enabled = false;
            bool       rice = false;
            RGBA_float color{1, 1, 0, 1};
            float      radius = 10;
            float      z_base = 11;
            float      z_tip = 14;
        } hat;
    } visuals;

    struct {
        bool bhop = false;
    } movement;
};

inline Config config;
inline Pawn*  target_pawn = nullptr;
inline bool   g_in_air = false;
inline bool     g_bhop_released  = false;
inline unsigned g_bhop_inj_up    = 0;
inline unsigned g_bhop_inj_down  = 0;
inline bool   g_bhop_eat_space = false;
inline unsigned g_cm_ticks = 0;
inline void*    g_user_cmd = nullptr;
inline void*    g_csgo_input = nullptr;
inline uint64_t g_cmd_buttons = 0;
inline int      g_cmd_btn_off = -1;
