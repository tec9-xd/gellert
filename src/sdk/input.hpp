#pragma once
#include "../core/vec.hpp"
#include "../core/memory.hpp"
#include "offsets.hpp"
#include <cmath>
#include <cstdint>

class Input {
public:
    static int view_angle_off;

    static bool looks_like_qangle(const Vec3& v) {
        return v.x >= -89.2f && v.x <= 89.2f &&
               fabsf(v.z) < 0.05f &&
               fabsf(v.y) <= 361.f &&
               std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    // match = Pawn::v_angle aus dem Dump (0x1330). Kein Internet-Offset.
    static int detect_va_off(void* self, Vec3 match = {}) {
        static const int cands[] = {
            0x548, 0x5B8, 0x5C0, 0x610, 0x688, 0x6B0,
            0x6C0, 0x750, 0x790, 0x7B0, 0x812
        };

        bool have_match = looks_like_qangle(match) &&
                          (fabsf(match.x) + fabsf(match.y) > 0.5f);

        auto dist = [&](const Vec3& v) -> float {
            return hypotf(remainderf(v.x - match.x, 360.f),
                          remainderf(v.y - match.y, 360.f));
        };

        if (have_match) {
            for (int o : cands) {
                Vec3 v = *(Vec3*)((uintptr_t)self + o);
                if (looks_like_qangle(v) && dist(v) < 0.15f)
                    return o;
            }
            int best = -1;
            float best_d = 1e9f;
            for (int o = 0x400; o <= 0xA00; o += 4) {
                Vec3 v = *(Vec3*)((uintptr_t)self + o);
                if (!looks_like_qangle(v)) continue;
                float d = dist(v);
                if (d < best_d) { best_d = d; best = o; }
                if (d < 0.05f) return o;
            }
            if (best >= 0 && best_d < 2.f)
                return best;
        }

        for (int o : cands) {
            Vec3 v = *(Vec3*)((uintptr_t)self + o);
            if (looks_like_qangle(v))
                return o;
        }
        return 0x548;
    }

    void set_thirdperson(bool v) { *(bool*)((uintptr_t)this + off::input_thirdperson) = v; }
    bool is_thirdperson()        { return *(bool*)((uintptr_t)this + off::input_thirdperson); }

    void set_view_angles(Vec3 a, Vec3 match = {}) {
        if (view_angle_off < 0)
            view_angle_off = detect_va_off(this, match);
        *(Vec3*)((uintptr_t)this + view_angle_off) = a;
        Vec3 nxt = *(Vec3*)((uintptr_t)this + view_angle_off + 12);
        if (looks_like_qangle(nxt))
            *(Vec3*)((uintptr_t)this + view_angle_off + 12) = a;
    }

    Vec3 get_view_angles(Vec3 match = {}) {
        if (view_angle_off < 0)
            view_angle_off = detect_va_off(this, match);
        return *(Vec3*)((uintptr_t)this + view_angle_off);
    }

    // tot. Attack geht über Pawn::set_button(IN_ATTACK) + SDL-LMB.
    void set_shoot(bool) {}
};

inline int Input::view_angle_off = -1;
inline Input* input = nullptr;
