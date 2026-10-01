#pragma once
#include "vec.hpp"
#include "imgui/dearimgui.hpp"
#include <cmath>

#define radpi 57.295779513082f
#define pideg 0.017453293f

inline VMatrix* view_matrix = nullptr;

inline bool view_matrix_ok() {
    if (!view_matrix) return false;
    float a00 = (*view_matrix)[0][0];
    float a11 = (*view_matrix)[1][1];
    if (!std::isfinite(a00) || !std::isfinite(a11)) return false;
    if (fabsf(a00) < 0.01f || fabsf(a11) < 0.01f) return false;
    if (fabsf(a00) > 1.e4f || fabsf(a11) > 1.e4f) return false;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (!std::isfinite((*view_matrix)[r][c])) return false;
    return true;
}

inline bool world_to_screen(Vec3 point, Vec3* screen) {
    if (!view_matrix) return false;
    const float w = (*view_matrix)[3][0] * point.x + (*view_matrix)[3][1] * point.y
                  + (*view_matrix)[3][2] * point.z + (*view_matrix)[3][3];
    if (w <= 0.01f) return false;
    const float invw = 1.f / w;
    const float x = ImGui::GetIO().DisplaySize.x * 0.5f;
    const float y = ImGui::GetIO().DisplaySize.y * 0.5f;
    if (x < 1.f || y < 1.f) return false;
    screen->x = x + (((*view_matrix)[0][0] * point.x + (*view_matrix)[0][1] * point.y
                    + (*view_matrix)[0][2] * point.z + (*view_matrix)[0][3]) * invw * x);
    screen->y = y - (((*view_matrix)[1][0] * point.x + (*view_matrix)[1][1] * point.y
                    + (*view_matrix)[1][2] * point.z + (*view_matrix)[1][3]) * invw * y);
    return true;
}
