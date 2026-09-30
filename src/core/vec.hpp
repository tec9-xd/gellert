#pragma once
#include "dearimgui.hpp"
#include <cmath>

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 v) const { return {x + v.x, y + v.y, z + v.z}; }
    Vec3 operator-(Vec3 v) const { return {x - v.x, y - v.y, z - v.z}; }
    Vec3 operator*(float v) const { return {x * v, y * v, z * v}; }
    Vec3& operator+=(Vec3 v) { x += v.x; y += v.y; z += v.z; return *this; }
    bool operator!=(Vec3 v) const { return x != v.x || y != v.y || z != v.z; }
    float length() const { return sqrtf(x * x + y * y + z * z); }
};

struct RGBA_float {
    float r = 1, g = 1, b = 1, a = 1;
    ImU32 to_ImU32() const {
        return IM_COL32(int(r * 255), int(g * 255), int(b * 255), int(a * 255));
    }
    float* to_arr() { return &r; }
};

using VMatrix = float[4][4];
