#include "feature.hpp"
#include "../gui/config.hpp"
#include "../gui/menu.hpp"
#include "../core/math.hpp"
#include "../core/log.hpp"
#include "../sdk/input.hpp"
#include "../sdk/entity_system.hpp"
#include "../sdk/pawn.hpp"
#include "../hooks/hooks.hpp"
#include "dearimgui.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>

static constexpr uintptr_t OFF_V_ANGLE      = 0x1330;
static constexpr uintptr_t OFF_V_ANGLE_PREV = 0x133C;
static constexpr uintptr_t OFF_ANG_EYE      = 0x4490;

static Pawn* g_sticky     = nullptr;
static Vec3  g_last_ang{};
static bool  g_last_ang_ok = false;
static bool  g_bind        = false;

static int   g_dbg_esp  = 0;
static int   g_dbg_fov  = 0;
static float g_dbg_px   = -1.f;
static char  g_dbg_name[32] = "-";

static bool bind_down() {
    const Keybind& b = config.aimbot.key;
    if (b.button >= 0) {
        const bool* keys = get_keyboard_state_original
            ? get_keyboard_state_original(nullptr)
            : SDL_GetKeyboardState(nullptr);
        return keys && keys[b.button];
    }
    return (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_MASK(-b.button)) != 0;
}

static bool bone_ok(const Vec3& b, const Vec3& origin) {
    if (b.x == 0.f && b.y == 0.f && b.z == 0.f) return false;
    if (!std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.z)) return false;
    float dx = b.x - origin.x, dy = b.y - origin.y, dz = b.z - origin.z;
    float d2 = dx * dx + dy * dy + dz * dz;
    return d2 > 1.f && d2 < 200.f * 200.f;
}

// same head ESP uses (origin+72), bone 6 only if it actually sits on the neck
static Vec3 aim_point(Pawn* pawn) {
    Vec3 origin = pawn->get_abs_origin();
    Vec3 bone = pawn->get_bone_location((unsigned)Bone::head);
    if (bone_ok(bone, origin) && (bone.z - origin.z) > 50.f && (bone.z - origin.z) < 90.f)
        return bone;
    return Vec3{origin.x, origin.y, origin.z + 72.f};
}

static Vec3 local_eye(Pawn* local) {
    Vec3 o = local->get_abs_origin();
    Vec3 e = local->get_eye_position();
    float dz = e.z - o.z;
    if (!std::isfinite(e.x) || dz < 40.f || dz > 90.f)
        return Vec3{o.x, o.y, o.z + 64.f};
    return e;
}

static Vec3 calc_angle(const Vec3& src, const Vec3& dst) {
    Vec3 d{dst.x - src.x, dst.y - src.y, dst.z - src.z};
    float hyp = sqrtf(d.x * d.x + d.y * d.y);
    if (hyp < 0.001f) hyp = 0.001f;
    Vec3 a{-atan2f(d.z, hyp) * radpi, atan2f(d.y, d.x) * radpi, 0.f};
    if (a.x > 89.f) a.x = 89.f;
    else if (a.x < -89.f) a.x = -89.f;
    a.y = remainderf(a.y, 360.f);
    return a;
}

// identical filters to ESP (no immunity, no CreateMove-only checks)
static bool esp_targetable(Pawn* pawn, Pawn* localpawn, int local_team, bool have_local) {
    if (!valid_ptr(pawn) || pawn == localpawn) return false;
    if (pawn->get_lifestate() || pawn->is_dormant()) return false;
    int team = (int)pawn->get_cs_team();
    if (team != 2 && team != 3) return false;
    if (have_local && (local_team == 2 || local_team == 3) && team == local_team) return false;
    int hp = pawn->get_health();
    if (hp <= 0 || hp > 200) return false;
    Vec3 o = pawn->get_abs_origin();
    if (o.x == 0.f && o.y == 0.f && o.z == 0.f) return false;
    return true;
}

static float fov_px() {
    float sw = ImGui::GetIO().DisplaySize.x;
    if (sw < 2.f) sw = 1920.f;
    if (config.aimbot.fov >= 179.f) return 1.e8f;
    float fov = config.aimbot.fov;
    if (fov < 0.1f) fov = 0.1f;
    return (tanf(fov * pideg) / tanf(45.f * pideg) * (sw * 0.5f)) / 1.55f;
}

static void commit(Pawn* local, const Vec3& ang) {
    g_last_ang = ang;
    g_last_ang_ok = true;
    if (input) input->set_view_angles(ang);
    if (!valid_ptr(local)) return;
    *(Vec3*)((uintptr_t)local + OFF_V_ANGLE)      = ang;
    *(Vec3*)((uintptr_t)local + OFF_V_ANGLE_PREV) = ang;
    *(Vec3*)((uintptr_t)local + OFF_ANG_EYE)      = ang;
}

static void snap_to_sticky() {
    if (!config.aimbot.master || menu_focused) {
        g_sticky = nullptr;
        g_last_ang_ok = false;
        target_pawn = nullptr;
        return;
    }
    if (!g_bind && !config.aimbot.auto_shoot) {
        g_sticky = nullptr;
        g_last_ang_ok = false;
        target_pawn = nullptr;
        return;
    }
    if (!entity_system || !valid_ptr(entity_system) || !input) return;

    Pawn* local = entity_system->get_localpawn();
    if (!valid_ptr(local) || local->get_lifestate()) return;

    if (g_sticky && esp_targetable(g_sticky, local, (int)local->get_cs_team(), true)) {
        Vec3 ang = calc_angle(local_eye(local), aim_point(g_sticky));
        commit(local, ang);
        target_pawn = g_sticky;
        if (config.aimbot.auto_shoot) input->set_shoot(true);
        return;
    }

    // dead / lost — freeze last snap, do NOT restore mouse
    g_sticky = nullptr;
    target_pawn = nullptr;
    if (g_last_ang_ok)
        commit(local, g_last_ang);
}

struct AimbotFeature final : IFeature {
    const char* name() const override { return "Aimbot"; }
    const char* tab()  const override { return "Aimbot"; }

    void on_create_move_pre() override { snap_to_sticky(); }
    void on_create_move()     override { snap_to_sticky(); }

    void on_draw() override {
        g_bind = bind_down();
        g_dbg_esp = g_dbg_fov = 0;
        g_dbg_px = -1.f;
        g_dbg_name[0] = '-', g_dbg_name[1] = 0;

        float sw = ImGui::GetIO().DisplaySize.x;
        float sh = ImGui::GetIO().DisplaySize.y;
        if (sw < 2.f) { sw = 1920.f; sh = 1080.f; }
        const float cx = sw * 0.5f, cy = sh * 0.5f;
        const float r  = fov_px();

        if (config.aimbot.draw_fov) {
            ImDrawList* dl = ImGui::GetBackgroundDrawList();
            if (dl) dl->AddCircle(ImVec2(cx, cy), r, IM_COL32(255, 255, 255, 210), 64);
        }

        if (!config.aimbot.master || !entity_system || !valid_ptr(entity_system)) {
            if (config.aimbot.master) {
                ImDrawList* dl = ImGui::GetBackgroundDrawList();
                if (dl) dl->AddText(ImVec2(20, 70), IM_COL32(255, 80, 80, 255),
                                    "aim: no entity_system");
            }
            return;
        }

        Entity* localentity = entity_system->get_localentity();
        Pawn*   localpawn   = entity_system->get_localpawn();
        bool have_local = valid_ptr(localentity) && valid_ptr(localpawn);
        int local_team = have_local ? (int)localpawn->get_cs_team() : 0;

        Pawn* best = nullptr;
        float best_d = 1.e9f;
        Vec3  best_head{};

        // EXACT ESP walk: controllers 1..64, same handle/hp/team/dormant/origin filters
        for (unsigned i = 1; i <= 64; ++i) {
            Entity* entity = entity_system->entity_from_index(i);
            if (!valid_ptr(entity) || entity == localentity) continue;
            int handle = entity->get_pawn_handle();
            if (handle == -1) continue;
            unsigned idx = (unsigned)(handle & 0x7FFF);
            if (idx == 0 || idx > 0x4000) continue;
            Pawn* pawn = entity_system->pawn_from_pawn_handle(handle);
            if (!esp_targetable(pawn, localpawn, local_team, have_local)) continue;

            Vec3 origin = pawn->get_abs_origin();
            Vec3 head{origin.x, origin.y, origin.z + 72.f};
            Vec3 s_feet, s_head;
            if (!world_to_screen(origin, &s_feet)) continue;
            if (!world_to_screen(head, &s_head)) continue;
            ++g_dbg_esp;

            float d_head = hypotf(s_head.x - cx, s_head.y - cy);
            float d_feet = hypotf(s_feet.x - cx, s_feet.y - cy);
            float d = d_head < d_feet ? d_head : d_feet;
            if (d > r) continue;
            ++g_dbg_fov;

            if (d < best_d) {
                best_d = d;
                best = pawn;
                best_head = aim_point(pawn);
            }
        }

        bool want = g_bind || config.aimbot.auto_shoot;
        if (!want || menu_focused) {
            g_sticky = nullptr;
            g_last_ang_ok = false;
            target_pawn = nullptr;
        } else {
            bool sticky_ok = g_sticky &&
                esp_targetable(g_sticky, localpawn, local_team, have_local);
            if (!sticky_ok)
                g_sticky = best;
            target_pawn = g_sticky;
        }

        if (g_sticky) {
            g_dbg_px = best_d;
            const char* nm = nullptr;
            // name lives on the controller, not the pawn — scan back for display only
            for (unsigned i = 1; i <= 64; ++i) {
                Entity* e = entity_system->entity_from_index(i);
                if (!valid_ptr(e)) continue;
                Pawn* p = entity_system->pawn_from_pawn_handle(e->get_pawn_handle());
                if (p == g_sticky) { nm = e->get_name(); break; }
            }
            if (valid_ptr(nm) && (unsigned char)nm[0] >= 32 && (unsigned char)nm[0] < 127)
                snprintf(g_dbg_name, sizeof(g_dbg_name), "%.24s", nm);
            else
                snprintf(g_dbg_name, sizeof(g_dbg_name), "pawn");

            Vec3 s;
            if (world_to_screen(aim_point(g_sticky), &s)) {
                ImDrawList* dl = ImGui::GetBackgroundDrawList();
                if (dl) {
                    dl->AddCircleFilled(ImVec2(s.x, s.y), 4.f, IM_COL32(255, 0, 255, 255));
                    dl->AddLine(ImVec2(cx, cy), ImVec2(s.x, s.y), IM_COL32(255, 0, 255, 180), 1.f);
                }
            }
        }

        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        if (dl) {
            char buf[192];
            snprintf(buf, sizeof(buf),
                     "aim bind:%s  esp:%d  in_fov:%d  lock:%s  px:%.0f  va_off=0x%x  cm=%u",
                     g_bind ? "DOWN" : "up",
                     g_dbg_esp, g_dbg_fov, g_dbg_name, g_dbg_px,
                     input ? (unsigned)Input::view_angle_off : 0u,
                     g_cm_ticks);
            dl->AddText(ImVec2(20, 70), IM_COL32(0, 255, 180, 255), buf);
        }
    }

    void on_menu() override {
        ImGui::Checkbox("Master", &config.aimbot.master);
        ImGui::Text("Key");
        ImGui::SameLine();
        ImGui::KeybindBox(&config.aimbot.key.waiting, &config.aimbot.key.button);
        ImGui::SliderFloatHeightPad("FOV", &config.aimbot.fov, 0.1f, 180.f, 1, "%.0f\xC2\xB0");
        ImGui::SliderFloatHeightPad("Smoothness (unused — snap)", &config.aimbot.smooth, 1.f, 20.f, 1, "%.1f");
        ImGui::Checkbox("Draw FOV", &config.aimbot.draw_fov);
        ImGui::Checkbox("Recoil Compensation", &config.aimbot.recoil);
        ImGui::Checkbox("Auto Shoot", &config.aimbot.auto_shoot);
        ImGui::Separator();
        ImGui::Text("bind: %s   esp-visible: %d   in circle: %d",
                    g_bind ? "DOWN" : "up", g_dbg_esp, g_dbg_fov);
        ImGui::Text("lock: %s   cm ticks: %u   va_off: 0x%x",
                    g_dbg_name, g_cm_ticks, input ? (unsigned)Input::view_angle_off : 0u);
        ImGui::TextWrapped("FOV 180 = anyone ESP can see. Cyan line under ESP debug: "
                           "bind must say DOWN when you hold the key. in_fov must be >0 "
                           "when someone is in the circle. Magenta dot = locked head.");
    }
};
REGISTER_FEATURE(AimbotFeature);

extern "C" void* aim_keep() { return (void*)&_inst_AimbotFeature; }
