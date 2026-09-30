#pragma once
#include "../core/vec.hpp"
#include "../core/memory.hpp"
#include "offsets.hpp"
#include <cmath>
#include <cstdint>

class Input {
public:
    static int view_angle_off;

    static int detect_va_off(void* self) {
        static const int cands[] = { 0x548, 0x688, 0x6C0, 0x6B0, 0x7B0, 0x812 };
        for (int o : cands) {
            Vec3 v = *(Vec3*)((uintptr_t)self + o);
            if (v.x >= -89.2f && v.x <= 89.2f && fabsf(v.z) < 0.02f &&
                fabsf(v.y) <= 361.f && std::isfinite(v.x) && std::isfinite(v.y))
                return o;
        }
        return 0x548;
    }
    void set_thirdperson(bool v) { *(bool*)((uintptr_t)this + off::input_thirdperson) = v; }
    bool is_thirdperson()        { return *(bool*)((uintptr_t)this + off::input_thirdperson); }
    void set_view_angles(Vec3 a) {
        if (view_angle_off < 0) view_angle_off = detect_va_off(this);
        *(Vec3*)((uintptr_t)this + view_angle_off) = a;
    }
    Vec3 get_view_angles() {
        if (view_angle_off < 0) view_angle_off = detect_va_off(this);
        return *(Vec3*)((uintptr_t)this + view_angle_off);
    }
    void set_shoot(bool v) { *(bool*)((uintptr_t)this + off::input_shoot) = v; }
};

inline int Input::view_angle_off = -1;
inline Input* input = nullptr;
