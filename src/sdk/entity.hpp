#pragma once
#include "offsets.hpp"
#include "../core/memory.hpp"

class Entity {
public:
    int get_pawn_handle() {
        return *(int*)((uintptr_t)this + off::m_hPlayerPawn);
    }
    const char* get_name() {
        return (const char*)((uintptr_t)this + off::m_iszPlayerName);
    }
    void set_fov(int fov) {
        *(int*)((uintptr_t)this + off::m_iDesiredFOV) = fov;
    }
};
