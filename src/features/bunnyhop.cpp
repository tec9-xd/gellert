#include "feature.hpp"
#include "../gui/config.hpp"
#include "../gui/menu.hpp"
#include "../core/memory.hpp"
#include "../sdk/entity_system.hpp"
#include "../sdk/pawn.hpp"
#include "../sdk/input.hpp"
#include "../sdk/offsets.hpp"
#include "../hooks/hooks.hpp"
#include "dearimgui.hpp"
#include <SDL3/SDL.h>
#include <cstdint>

static bool     g_space = false;
static bool     g_grounded = false;
static void*    g_ms = nullptr;
static uint64_t g_btn_value = 0;

static bool space_held() {
    const bool* keys = get_keyboard_state_original
        ? get_keyboard_state_original(nullptr)
        : SDL_GetKeyboardState(nullptr);
    return keys && keys[SDL_SCANCODE_SPACE];
}

static bool& bhop_flag() { return config.movement.bhop; }

static Pawn* local_pawn() {
    if (entity_system && valid_ptr(entity_system)) {
        Pawn* p = entity_system->get_localpawn();
        if (valid_ptr(p)) return p;
    }
    if (!mem::client) return nullptr;
    Pawn* p = *(Pawn**)(mem::client + off::dwLocalPlayerPawn);
    return valid_ptr(p) ? p : nullptr;
}

static bool skip_move(Pawn* pawn) {
    uint8_t mt = *(uint8_t*)((uintptr_t)pawn + 0x69E); // m_nActualMoveType (dump)
    if (mt == 7 || mt == 8 || mt == 9) return true;   // noclip / observer / ladder
    float water = *(float*)((uintptr_t)pawn + 0x6A0);  // m_flWaterLevel
    return water >= 2.f;
}

static void write_ms_jump(Pawn* pawn, bool down) {
    void* ms = pawn->movement_services();
    g_ms = ms;
    if (!valid_ptr(ms)) return;
    auto* n = (uint64_t*)((uintptr_t)ms + off::m_nButtons + 8);
    if (down) { n[0] |= IN_JUMP; n[1] |= IN_JUMP; n[2] |= IN_JUMP; }
    else      { n[0] &= ~IN_JUMP; n[1] &= ~IN_JUMP; n[2] &= ~IN_JUMP; }
    g_btn_value = n[0];
    *(uint64_t*)((uintptr_t)ms + off::m_nQueuedButtonDownMask)   = down ? (n[0] | IN_JUMP) : (n[0] & ~IN_JUMP);
    *(uint64_t*)((uintptr_t)ms + off::m_nQueuedButtonChangeMask) = IN_JUMP;
    *(bool*)((uintptr_t)ms + off::m_LegacyJump + off::m_bOldJumpPressed) = false;
}

static void hop(bool post) {
    g_space = space_held();

    Pawn* pawn = local_pawn();
    if (!valid_ptr(pawn) || pawn->get_lifestate() || menu_focused || !bhop_flag()) {
        g_bhop_eat_space = false;
        g_grounded = valid_ptr(pawn) ? pawn->on_ground() : false;
        g_in_air = !g_grounded;
        return;
    }

    g_grounded = pawn->on_ground();
    int gh = *(int*)((uintptr_t)pawn + off::m_hGroundEntity);
    if (gh != -1) g_grounded = true;
    g_in_air = !g_grounded;

    if (!g_space || skip_move(pawn)) {
        g_bhop_eat_space = false;
        return;
    }

    // like sv_autobunnyhopping: held jump is a NEW press only on the grounded tick
    g_bhop_eat_space = !g_grounded;

    if (g_grounded) write_ms_jump(pawn, true);
    else            write_ms_jump(pawn, false);

    (void)post;
}

struct BunnyhopFeature final : IFeature {
    const char* name() const override { return "Bunnyhop"; }
    const char* tab()  const override { return "Movement"; }

    void on_create_move_pre() override { hop(false); }
    void on_create_move()     override { hop(true); }

    void on_draw() override {
        Pawn* p = local_pawn();
        g_space = space_held();
        if (valid_ptr(p) && !p->get_lifestate()) {
            g_grounded = p->on_ground();
            g_in_air = !g_grounded;
            if (bhop_flag() && g_space && !menu_focused)
                g_bhop_eat_space = !g_grounded;
            else
                g_bhop_eat_space = false;
        } else {
            g_bhop_eat_space = false;
        }
    }

    void on_menu() override {
        ImGui::Checkbox("Bunnyhop", &bhop_flag());
        ImGui::Text("space: %s   grounded: %s   air: %s",
                    g_space ? "YES" : "no",
                    g_grounded ? "YES" : "no",
                    g_in_air ? "YES" : "no");
        ImGui::Text("eat_space: %s   cm: %u", g_bhop_eat_space ? "YES" : "no", g_cm_ticks);
        ImGui::Text("ms: %p  buttons: 0x%llx", g_ms, (unsigned long long)g_btn_value);
        ImGui::TextWrapped("Hold space. In air the game sees space UP; on landing it sees a new press.");
    }
};
REGISTER_FEATURE(BunnyhopFeature);

extern "C" void* bhop_keep() { return (void*)&_inst_BunnyhopFeature; }
