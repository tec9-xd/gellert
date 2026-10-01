#pragma once
#include "entity.hpp"
#include "pawn.hpp"
#include "offsets.hpp"
#include "../core/memory.hpp"

inline Entity** localentity_ptr = nullptr;
inline Pawn**   localpawn_ptr   = nullptr;

class GameEntitySystem {
public:
    Entity* entity_from_index(unsigned i) {
        if (i > 0x4000) return nullptr;
        void** bucket_slot = (void**)((uintptr_t)this + off::ges_identity_chunks + 8ull * (i >> 9));
        if (!valid_ptr(bucket_slot)) return nullptr;
        void* bucket = *bucket_slot;
        if (!valid_ptr(bucket)) return nullptr;
        Entity* e = *(Entity**)((uintptr_t)bucket + off::entity_identity_size * (i & 0x1FF));
        return valid_ptr(e) ? e : nullptr;
    }
    Pawn* pawn_from_pawn_handle(int handle) {
        unsigned idx = (unsigned)(handle & 0x7FFF);
        if (idx == 0 || idx > 0x4000) return nullptr;
        return (Pawn*)entity_from_index(idx);
    }
    Entity* get_localentity() {
        if (valid_ptr(localentity_ptr)) {
            Entity* e = *localentity_ptr;
            if (valid_ptr(e)) return e;
        }
        for (unsigned i = 1; i <= 64; ++i) {
            Entity* e = entity_from_index(i);
            if (valid_ptr(e) && e->is_local_controller())
                return e;
        }
        return nullptr;
    }
    Pawn* get_localpawn() {
        if (valid_ptr(localpawn_ptr)) {
            Pawn* p = *localpawn_ptr;
            if (valid_ptr(p)) return p;
        }
        Entity* e = get_localentity();
        if (!e) return nullptr;
        return pawn_from_pawn_handle(e->get_pawn_handle());
    }
};

inline GameEntitySystem* entity_system = nullptr;
