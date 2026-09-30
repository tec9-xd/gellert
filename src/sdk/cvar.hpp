#pragma once
#include "../core/memory.hpp"
#include <cstring>

class Convar {
public:
    template<typename T> T    get_value() { return *(T*)((uintptr_t)this + 0x50); }
    template<typename T> void set_value(T v) { *(T*)((uintptr_t)this + 0x50) = v; }
};

class CvarSystem {
public:
    Convar* get_convar(const char* name) {
        if (!name || !valid_ptr(this)) return nullptr;
        uintptr_t objects = *(uintptr_t*)((uintptr_t)this + 0x48);
        uintptr_t length  = *(uintptr_t*)((uintptr_t)this + 0xA0);
        if (!valid_ptr((void*)objects) || length == 0 || length > 8192) return nullptr;
        for (uintptr_t i = 0; i < length; ++i) {
            uintptr_t obj = *(uintptr_t*)(objects + i * 0x10);
            if (!valid_ptr((void*)obj)) continue;
            const char* n = *(const char**)obj;
            if (!valid_ptr(n)) continue;
            if (strstr(n, name)) return (Convar*)obj;
        }
        return nullptr;
    }
};
inline CvarSystem* cvar_system = nullptr;
