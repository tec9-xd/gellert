#pragma once
#include "offsets.hpp"
#include "../core/vec.hpp"
#include "../core/memory.hpp"
#include <cstdint>
#include <cmath>

enum Bone {
    hip = 0, spine1 = 1, spine2 = 2, spine3 = 3, spine4 = 4,
    neck = 5, head = 6,
    left_shoulder = 8, left_elbow = 9, left_hand = 10,
    right_shoulder = 13, right_elbow = 14, right_hand = 15,
    left_hip = 22, left_knee = 23, left_foot = 24,
    right_hip = 25, right_knee = 26, right_foot = 27,
};

enum class cs_team : uint8_t { none = 0, spec = 1, t = 2, ct = 3 };

class Pawn {
public:
    void* movement_services() {
        return *(void**)((uintptr_t)this + off::m_pMovementServices);
    }
    void* scene_node() {
        return *(void**)((uintptr_t)this + off::m_pGameSceneNode);
    }
    void* camera_services() {
        return *(void**)((uintptr_t)this + off::m_pCameraServices);
    }
    void* weapon_services() {
        return *(void**)((uintptr_t)this + off::m_pWeaponServices);
    }
    Vec3 get_abs_origin() {
        void* n = scene_node();
        if (valid_ptr(n)) {
            Vec3 o = *(Vec3*)((uintptr_t)n + off::m_vecAbsOrigin);
            if (o.x != 0.f || o.y != 0.f || o.z != 0.f)
                return o;
        }
        return *(Vec3*)((uintptr_t)this + off::m_vOldOrigin);
    }
    bool is_dormant() {
        void* n = scene_node();
        if (!valid_ptr(n)) return true;
        return *(bool*)((uintptr_t)n + off::m_bDormant);
    }
    Vec3 get_bone_location(unsigned i) {
        if (i > 128) return {};
        void* n = scene_node();
        if (!valid_ptr(n)) return {};
        void* bones = *(void**)((uintptr_t)n + off::m_modelState + off::bone_array);
        if (!valid_ptr(bones)) return {};
        Vec3 p = *(Vec3*)((uintptr_t)bones + i * off::bone_stride);
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return {};
        return p;
    }
    Vec3 get_eye_position() {
        Vec3 o = get_abs_origin();
        Vec3 offv = *(Vec3*)((uintptr_t)this + off::m_vecViewOffset);
        return o + offv;
    }
    Vec3 get_aim_punch() {
        return {};
    }
    bool get_lifestate() {
        return *(uint8_t*)((uintptr_t)this + off::m_lifeState) != 0;
    }
    cs_team get_cs_team() {
        return (cs_team)*(uint8_t*)((uintptr_t)this + off::m_iTeamNum);
    }
    int get_health() {
        return *(int*)((uintptr_t)this + off::m_iHealth);
    }
    int get_armor() {
        return *(int*)((uintptr_t)this + off::m_ArmorValue);
    }
    bool has_helmet() {
        void* is = *(void**)((uintptr_t)this + off::m_pItemServices);
        if (!valid_ptr(is)) return false;
        return *(bool*)((uintptr_t)is + off::is_bHasHelmet);
    }
    uint32_t get_flags() {
        return *(uint32_t*)((uintptr_t)this + off::m_fFlags);
    }
    bool on_ground() {
        return (get_flags() & FL_ONGROUND) != 0;
    }
    bool get_gun_game_immunity() {
        return *(bool*)((uintptr_t)this + off::m_bGunGameImmunity);
    }

    Vec3 get_v_angle() {
        return *(Vec3*)((uintptr_t)this + off::v_angle);
    }

    void set_button(uint64_t mask, bool down) {
        void* ms = movement_services();
        if (!valid_ptr(ms)) return;

        auto* nValue   = (uint64_t*)((uintptr_t)ms + off::m_nButtons + 0x8);
        auto* nChanged = (uint64_t*)((uintptr_t)ms + off::m_nButtons + 0x10);
        auto* queued   = (uint64_t*)((uintptr_t)ms + off::m_nQueuedButtonDownMask);
        auto* qchange  = (uint64_t*)((uintptr_t)ms + off::m_nQueuedButtonChangeMask);

        if (valid_ptr(nValue) && valid_ptr(nChanged)) {
            uint64_t prev = *nValue;
            if (down) *nValue |=  mask;
            else      *nValue &= ~mask;
            *nChanged |= (prev ^ *nValue);
        }
        if (valid_ptr(queued) && valid_ptr(qchange)) {
            if (down) {
                *queued  |= mask;
                *qchange |= mask;
            } else {
                *queued  &= ~mask;
            }
        }
    }
};
