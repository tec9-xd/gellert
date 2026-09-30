#pragma once
#include "vec.hpp"
#include "imgui/dearimgui.hpp"

#define radpi 57.295779513082f
#define pideg 0.017453293f

inline VMatrix* view_matrix = nullptr;

inline bool world_to_screen(Vec3 point, Vec3* screen) {
    if (!view_matrix) return false;
    const float w = (*view_matrix)[3][0] * point.x + (*view_matrix)[3][1] * point.y
                  + (*view_matrix)[3][2] * point.z + (*view_matrix)[3][3];
    if (w <= 0.01f) return false;
    const float invw = 1.f / w;
    const float x = ImGui::GetIO().DisplaySize.x * 0.5f;
    const float y = ImGui::GetIO().DisplaySize.y * 0.5f;
    screen->x = x + (((*view_matrix)[0][0] * point.x + (*view_matrix)[0][1] * point.y
                    + (*view_matrix)[0][2] * point.z + (*view_matrix)[0][3]) * invw * x);
    screen->y = y - (((*view_matrix)[1][0] * point.x + (*view_matrix)[1][1] * point.y
                    + (*view_matrix)[1][2] * point.z + (*view_matrix)[1][3]) * invw * y);
    return true;
}
