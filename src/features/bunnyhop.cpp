#include "feature.hpp"
#include "../gui/config.hpp"
#include "../gui/menu.hpp"
#include "../core/memory.hpp"
#include "../core/log.hpp"
#include "../sdk/entity_system.hpp"
#include "../sdk/pawn.hpp"
#include "../sdk/engine.hpp"
#include "../sdk/input.hpp"
#include "../sdk/offsets.hpp"
#include "../hooks/hooks.hpp"
#include "dearimgui.hpp"
#include <SDL3/SDL.h>
#include <cstdint>

static bool     g_space = false;
static bool     g_grounded = false;
static bool     g_was_grounded = true;
static uint64_t g_mask0 = 0;
static int      g_pushed = 0;

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

static void push_space(bool down) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = SDL_SCANCODE_SPACE;
    e.key.key = SDLK_SPACE;
    e.key.down = down;
    e.key.repeat = false;
    e.key.timestamp = SDL_GetTicksNS();
    SDL_PushEvent(&e);
    g_pushed++;
}

static void engine_cmd(const char* cmd) {
    if (!engine) return;
    void** vt = *(void***)engine;
    if (!valid_ptr(vt)) return;
    // Source2EngineToClient001: IsInGame is vt[33] in this cheat.
    // ExecuteClientCmd is a few slots after; try both common layouts.
    using FnA = void (*)(void*, int, const char*, bool);
    using FnB = void (*)(void*, const char*);
    static int idx = -1;
    static int kind = 0; // 1 = (this,0,cmd,true)  2 = (this,cmd)
    if (idx < 0) {
        // 47..55 windows-ish, linux often the same or +1
        static const int cands[] = { 49, 50, 47, 48, 51, 52, 53, 54, 46, 45 };
        idx = cands[0];
        kind = 1;
    }
    if (kind == 1 && valid_ptr(vt[idx]))
        ((FnA)vt[idx])(engine, 0, cmd, true);
    else if (valid_ptr(vt[idx]))
        ((FnB)vt[idx])(engine, cmd);
}

static void write_ms_jump(Pawn* pawn, bool down) {
    void* ms = pawn->movement_services();
    if (!valid_ptr(ms)) return;
    // CInButtonState at 0x50, nValue at +8 — NEVER write 0x50 itself (vtable)
    auto* n = (uint64_t*)((uintptr_t)ms + off::m_nButtons + off::m_pButtonStates);
    if (down) n[0] |= IN_JUMP;
    else      n[0] &= ~IN_JUMP;
    n[1] |= IN_JUMP;
    *(bool*)((uintptr_t)ms + off::m_LegacyJump + off::m_bOldJumpPressed) = !down;
}

static void write_input_jump(void* in, bool down) {
    if (!valid_ptr(in)) return;
    auto* b = (uint64_t*)((uintptr_t)in + off::input_shoot);
    if (down) b[0] |= IN_JUMP;
    else      b[0] &= ~IN_JUMP;
    g_mask0 = b[0];
}

static void hop() {
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

    if (!config.movement.bhop || !g_space) {
        g_was_grounded = g_grounded;
        return;
    }

    void* in = g_csgo_input ? g_csgo_input : (void*)input;

    if (g_grounded) {
        // new edge: up then down, every grounded tick while space is held
        push_space(false);
        push_space(true);
        write_input_jump(in, true);
        write_ms_jump(pawn, true);
        engine_cmd("+jump");
    } else {
        push_space(false);
        write_input_jump(in, false);
        write_ms_jump(pawn, false);
        engine_cmd("-jump");
    }
    g_was_grounded = g_grounded;
}

struct BunnyhopFeature final : IFeature {
    const char* name() const override { return "Bunnyhop"; }
    const char* tab()  const override { return "Movement"; }

    void on_create_move_pre() override { hop(); }

    void on_draw() override {
        // keep in-air flag live even if CreateMove skipped a frame
        Pawn* p = local_pawn();
        if (valid_ptr(p) && !p->get_lifestate()) {
            g_grounded = p->on_ground();
            g_in_air = !g_grounded;
        }
        g_space = space_held();
    }

    void on_menu() override {
        ImGui::Checkbox("Bunnyhop", &config.movement.bhop);
        ImGui::Text("space: %s   grounded: %s   air: %s",
                    g_space ? "YES" : "no",
                    g_grounded ? "YES" : "no",
                    g_in_air ? "YES" : "no");
        ImGui::Text("CreateMove ticks: %u", g_cm_ticks);
        ImGui::Text("input: %p  csgo_input: %p", input, g_csgo_input);
        ImGui::Text("mask0: 0x%llx   pushed: %d", (unsigned long long)g_mask0, g_pushed);
        ImGui::TextWrapped("Hold space. Spams jump edge on landing (SDL + IN_JUMP + +/-jump).");
    }
};
REGISTER_FEATURE(BunnyhopFeature);

extern "C" void* bhop_keep() { return (void*)&_inst_BunnyhopFeature; }
