#pragma once
#include <SDL3/SDL.h>

inline SDL_Window* sdl_window = nullptr;
inline bool menu_focused = false;

void get_input(SDL_Event* event);
void draw_watermark();
void draw_menu();
