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
#include <cstdio>

static constexpr uint64_t kBtnMax = 0x3FFFFFull; // IN_* live in the low 22 bits

static bool     g_space = false;
static bool     g_grounded = false;
static int      g_n_off = 0;
static int      g_offs[16];
static uint64_t g_seen[16];
static int      g_stripped = 0;
static int      g_forced = 0;
static void*    g_ms = nullptr;
static uint64_t g_ms_btn = 0;

static bool& bhop_flag() { return config.movement.bhop; }

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

static bool looks_btn(uint64_t v) {
    return v != 0 && v <= kBtnMax;
}

static void* input_obj() {
    if (valid_ptr(g_csgo_input)) return g_csgo_input;
    return (void*)input;
}

// find every uint64 in CCSGOInput that looks like a button mask with IN_JUMP
static void discover(void* in) {
    if (g_n_off > 0 || !valid_ptr(in) || !g_space) return;
    uint8_t* p = (uint8_t*)in;
    for (int o = 0x40; o < 0x1800; o += 8) {
        uint64_t v = *(uint64_t*)(p + o);
        if (!(v & IN_JUMP) || !looks_btn(v)) continue;
        g_offs[g_n_off] = o;
        g_seen[g_n_off] = v;
        g_cmd_btn_off = o;
        if (++g_n_off >= 16) break;
    }
    // also: heap CUserCmd* whose nButtons.nValue is at +0x60
    if (g_n_off < 16) {
        for (int o = 0x0; o < 0x800; o += 8) {
            void* maybe = *(void**)(p + o);
            if (!valid_ptr(maybe)) continue;
            uint64_t bv = *(uint64_t*)((uintptr_t)maybe + 0x60);
            if (!(bv & IN_JUMP) || !looks_btn(bv)) continue;
            g_user_cmd = maybe;
            // encode as negative so strip() knows to use pointer+0x60
            g_offs[g_n_off] = -(o + 1);
            g_seen[g_n_off] = bv;
            if (++g_n_off >= 16) break;
        }
    }
}

static uint64_t* slot(void* in, int enc) {
    if (enc >= 0)
        return (uint64_t*)((uintptr_t)in + enc);
    void* cmd = *(void**)((uintptr_t)in + (-enc - 1));
    if (!valid_ptr(cmd)) return nullptr;
    g_user_cmd = cmd;
    return (uint64_t*)((uintptr_t)cmd + 0x60);
}

static void apply_jump(void* in, bool want) {
    if (!valid_ptr(in)) return;
    g_stripped = 0;
    g_forced = 0;
    for (int i = 0; i < g_n_off; ++i) {
        uint64_t* b = slot(in, g_offs[i]);
        if (!b) continue;
        uint64_t before = *b;
        if (!looks_btn(before) && before != 0) continue;
        if (want) {
            if (!(before & IN_JUMP)) { *b |= IN_JUMP; ++g_forced; }
        } else {
            if (before & IN_JUMP) { *b &= ~IN_JUMP; ++g_stripped; }
        }
        g_seen[i] = *b;
        g_cmd_buttons = *b;
        // CInButtonState: nValueChanged is +8
        uint64_t* ch = b + 1;
        if (looks_btn(*ch) || *ch == 0)
            *ch |= IN_JUMP;
    }
}

static void apply_ms(Pawn* pawn, bool want) {
    void* ms = pawn->movement_services();
    g_ms = ms;
    if (!valid_ptr(ms)) return;
    auto* nval = (uint64_t*)((uintptr_t)ms + off::m_nButtons + 8);
    if (want) *nval |= IN_JUMP;
    else      *nval &= ~IN_JUMP;
    auto* nchg = (uint64_t*)((uintptr_t)ms + off::m_nButtons + 16);
    *nchg |= IN_JUMP;
    g_ms_btn = *nval;
    *(bool*)((uintptr_t)ms + off::m_LegacyJump + off::m_bOldJumpPressed) = false;
}

static void update_state(Pawn* pawn) {
    g_space = space_held();
    if (!valid_ptr(pawn) || pawn->get_lifestate()) {
        g_grounded = false;
        g_in_air = false;
        g_bhop_eat_space = false;
        return;
    }
    g_grounded = pawn->on_ground();
    g_in_air = !g_grounded;
    g_bhop_eat_space = bhop_flag() && g_space && g_in_air;
}

static void hop() {
    Pawn* pawn = local_pawn();
    update_state(pawn);
    if (!valid_ptr(pawn) || pawn->get_lifestate() || !bhop_flag() || !g_space)
        return;

    uint8_t mt = *(uint8_t*)((uintptr_t)pawn + 0x69E);
    if (mt == 8 || mt == 9 || mt == 10) return;

    void* in = input_obj();
    discover(in);

    // air: release jump so the landing tick is a NEW press
    // ground: leave/force jump (space is physically down)
    apply_jump(in, g_grounded);
    apply_ms(pawn, g_grounded);
}

struct BunnyhopFeature final : IFeature {
    const char* name() const override { return "Bunnyhop"; }
    const char* tab()  const override { return "Movement"; }

    void on_create_move_pre() override { hop(); }
    void on_create_move()     override { hop(); }

    void on_draw() override {
        update_state(local_pawn());
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        if (!dl) return;
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "BHOP on:%d sp:%d gnd:%d eat:%d cm:%u n:%d off:%d btn:%llx strip:%d force:%d",
                 (int)bhop_flag(), (int)g_space, (int)g_grounded, (int)g_bhop_eat_space,
                 g_cm_ticks, g_n_off, g_cmd_btn_off,
                 (unsigned long long)g_cmd_buttons, g_stripped, g_forced);
        dl->AddText(ImVec2(20, 70), IM_COL32(80, 255, 80, 255), buf);
    }

    void on_menu() override {
        ImGui::Checkbox("Bunnyhop", &bhop_flag());
        ImGui::Text("space: %s   grounded: %s   air: %s",
                    g_space ? "YES" : "no",
                    g_grounded ? "YES" : "no",
                    g_in_air ? "YES" : "no");
        ImGui::Text("eat_space: %s   (YES only in AIR — that is correct)",
                    g_bhop_eat_space ? "YES" : "no");
        ImGui::Text("cm: %u   slots: %d   first_off: 0x%x",
                    g_cm_ticks, g_n_off, g_cmd_btn_off);
        ImGui::Text("btn: 0x%llx  strip:%d  force:%d  ms:%p  msbtn:0x%llx",
                    (unsigned long long)g_cmd_buttons, g_stripped, g_forced,
                    g_ms, (unsigned long long)g_ms_btn);
        ImGui::TextWrapped("Hold space + W. eat_space YES in air, no on landing. slots must be >0.");
    }
};
REGISTER_FEATURE(BunnyhopFeature);

extern "C" void* bhop_keep() { return (void*)&_inst_BunnyhopFeature; }
