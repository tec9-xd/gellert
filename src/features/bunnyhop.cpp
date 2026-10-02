#include "feature.hpp"
#include "../gui/config.hpp"
#include "../gui/menu.hpp"
#include "../core/memory.hpp"
#include "../sdk/entity_system.hpp"
#include "../sdk/pawn.hpp"
#include "../sdk/offsets.hpp"
#include "../hooks/hooks.hpp"
#include "dearimgui.hpp"
#include <SDL3/SDL.h>
#include <cstdint>

static bool g_space = false;
static bool g_grounded = false;
static bool g_was_air = false;

static bool space_held() {
    const bool* keys = get_keyboard_state_original
        ? get_keyboard_state_original(nullptr)
        : SDL_GetKeyboardState(nullptr);
    return keys && keys[SDL_SCANCODE_SPACE];
}

static Pawn* local_pawn() {
    if (entity_system && valid_ptr(entity_system)) {
        Pawn* p = entity_system->get_localpawn();
        if (valid_ptr(p)) return p;
    }
    if (!mem::client) return nullptr;
    Pawn* p = *(Pawn**)(mem::client + off::dwLocalPlayerPawn);
    return valid_ptr(p) ? p : nullptr;
}

static void write_ms_edge(Pawn* pawn, bool down) {
    void* ms = pawn->movement_services();
    if (!valid_ptr(ms)) return;

    auto* nValue   = (uint64_t*)((uintptr_t)ms + off::m_nButtons + 0x8);
    auto* nChanged = (uint64_t*)((uintptr_t)ms + off::m_nButtons + 0x10);
    auto* queued   = (uint64_t*)((uintptr_t)ms + off::m_nQueuedButtonDownMask);
    auto* qchange  = (uint64_t*)((uintptr_t)ms + off::m_nQueuedButtonChangeMask);

    if (valid_ptr(nValue) && valid_ptr(nChanged)) {
        uint64_t prev = *nValue;
        if (down) *nValue |=  IN_JUMP;
        else      *nValue &= ~IN_JUMP;
        *nChanged |= (prev ^ *nValue);
    }
    if (valid_ptr(queued) && valid_ptr(qchange)) {
        if (down) {
            *queued  |= IN_JUMP;
            *qchange |= IN_JUMP;
        } else {
            *queued  &= ~IN_JUMP;
        }
    }

    bool* old_pressed = (bool*)((uintptr_t)ms + off::m_LegacyJump + off::m_bOldJumpPressed);
    if (valid_ptr(old_pressed))
        *old_pressed = !down;
}

static void update_flags() {
    Pawn* pawn = local_pawn();
    if (!valid_ptr(pawn) || pawn->get_lifestate()) {
        g_in_air = false;
        g_grounded = false;
        g_space = space_held();
        return;
    }
    g_grounded = pawn->on_ground();
    g_in_air = !g_grounded;
    g_space = space_held();
}

static void hop() {
    update_flags();

    if (!config.movement.bhop || !g_space || menu_focused) {
        g_bhop_released = false;
        g_was_air = g_in_air;
        return;
    }

    Pawn* pawn = local_pawn();
    if (!valid_ptr(pawn) || pawn->get_lifestate()) {
        g_was_air = g_in_air;
        return;
    }

    if (g_in_air) {
        if (!g_bhop_released) {
            bhop_inject_space(false);   // -jump, so the next down is a new edge
            g_bhop_inj_up++;
            g_bhop_released = true;
            write_ms_edge(pawn, false);
        }
    } else if (g_bhop_released || g_was_air) {
        bhop_inject_space(true);        // +jump on landing
        g_bhop_inj_down++;
        g_bhop_released = false;
        write_ms_edge(pawn, true);
    }

    g_was_air = g_in_air;
}

struct BunnyhopFeature final : IFeature {
    const char* name() const override { return "Bunnyhop"; }
    const char* tab()  const override { return "Movement"; }

    void on_create_move_pre() override { hop(); }
    void on_draw() override { update_flags(); }

    void on_menu() override {
        ImGui::Checkbox("Bunnyhop", &config.movement.bhop);
        ImGui::Text("space: %s   grounded: %s   air: %s",
                    g_space ? "YES" : "no",
                    g_grounded ? "YES" : "no",
                    g_in_air ? "YES" : "no");
        ImGui::Text("released(spoof): %s", g_bhop_released ? "YES" : "no");
        ImGui::Text("inject  up: %u  down: %u", g_bhop_inj_up, g_bhop_inj_down);
        ImGui::Text("CreateMove ticks: %u", g_cm_ticks);
        ImGui::TextWrapped("Hold space. KEYUP in air, KEYDOWN on landing (via SDL_ADDEVENT).");
    }
};
REGISTER_FEATURE(BunnyhopFeature);

extern "C" void* bhop_keep() { return (void*)&_inst_BunnyhopFeature; }
