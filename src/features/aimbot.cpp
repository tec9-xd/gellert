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
#include <cmath>

static bool g_aim_firing = false;

static Vec3 aim_point(Pawn* pawn) {
    Vec3 origin = pawn->get_abs_origin();
    Vec3 bone = pawn->get_bone_location(6);
    float dx = bone.x - origin.x, dy = bone.y - origin.y, dz = bone.z - origin.z;
    float d2 = dx * dx + dy * dy + dz * dz;
    if (d2 > 4.f && d2 < 120.f * 120.f && bone.z > origin.z)
        return bone;
    return Vec3{origin.x, origin.y, origin.z + 72.f};
}

static void fire(Pawn* localpawn, bool down) {
    if (valid_ptr(localpawn))
        localpawn->set_button(IN_ATTACK, down);
    if (down != g_aim_firing) {
        aim_inject_lmb(down);
        g_aim_firing = down;
    } else if (down) {
        localpawn->set_button(IN_ATTACK, true);
    }
}

struct AimbotFeature final : IFeature {
    const char* name() const override { return "Aimbot"; }
    const char* tab()  const override { return "Aimbot"; }

    // PRE, sonst setzt Original-CreateMove Attack aus dem echten Mauszustand zurück.
    void on_create_move_pre() override {
        if (!config.aimbot.master || menu_focused) {
            if (g_aim_firing) {
                Pawn* p = (entity_system && valid_ptr(entity_system))
                    ? entity_system->get_localpawn() : nullptr;
                fire(p, false);
            }
            target_pawn = nullptr;
            return;
        }
        if (!entity_system || !valid_ptr(entity_system) || !input) return;

        bool key = is_down(config.aimbot.key);
        if (!key && !config.aimbot.auto_shoot) {
            if (g_aim_firing) {
                Pawn* p = entity_system->get_localpawn();
                fire(p, false);
            }
            target_pawn = nullptr;
            return;
        }

        Pawn* localpawn = entity_system->get_localpawn();
        Entity* localentity = entity_system->get_localentity();
        if (!valid_ptr(localpawn) || !valid_ptr(localentity)) return;
        if (localpawn->get_lifestate()) return;

        int local_team = (int)localpawn->get_cs_team();
        if (local_team != 2 && local_team != 3) return;

        Vec3 va_match = localpawn->get_v_angle();
        if (Input::view_angle_off < 0)
            Input::view_angle_off = Input::detect_va_off(input, va_match);

        Vec3 original_view_angles = input->get_view_angles(va_match);
        Vec3 eye = localpawn->get_eye_position();
        if (eye.x == 0.f && eye.y == 0.f && eye.z == 0.f)
            eye = localpawn->get_abs_origin();

        static bool once = false;
        if (!once) {
            print("aimbot live  va_off=0x%x  va=%.1f %.1f  pawn_va=%.1f %.1f  team=%d\n",
                  Input::view_angle_off,
                  original_view_angles.x, original_view_angles.y,
                  va_match.x, va_match.y, local_team);
            once = true;
        }

        Pawn* best = nullptr;
        float best_fov = config.aimbot.fov;
        Vec3 best_delta{};

        for (unsigned i = 1; i <= 64; ++i) {
            Entity* entity = entity_system->entity_from_index(i);
            if (!valid_ptr(entity) || entity == localentity) continue;
            int handle = entity->get_pawn_handle();
            if (handle == -1) continue;
            Pawn* pawn = entity_system->pawn_from_pawn_handle(handle);
            if (!valid_ptr(pawn) || pawn == localpawn) continue;
            if (pawn->get_lifestate() || pawn->is_dormant()) continue;
            int team = (int)pawn->get_cs_team();
            if (team != 2 && team != 3) continue;
            if (team == local_team) continue;
            int hp = pawn->get_health();
            if (hp <= 0 || hp > 200) continue;

            Vec3 bone = aim_point(pawn);
            Vec3 diff{bone.x - eye.x, bone.y - eye.y, bone.z - eye.z};
            float yaw_hyp = sqrtf(diff.x * diff.x + diff.y * diff.y);
            float pitch = atan2f(diff.z, yaw_hyp) * radpi;
            float yaw   = atan2f(diff.y, diff.x) * radpi;
            Vec3 desired{-pitch, yaw, 0.f};

            float x = remainderf(desired.x - original_view_angles.x, 360.f);
            float y = remainderf(desired.y - original_view_angles.y, 360.f);
            if (x > 89.f) x = 89.f; else if (x < -89.f) x = -89.f;
            float fov = hypotf(x, y);
            if (fov < best_fov) {
                best_fov = fov;
                best = pawn;
                best_delta = {x, y, 0.f};
            }
        }

        target_pawn = best;
        if (!best) {
            fire(localpawn, false);
            return;
        }

        float smooth = config.aimbot.smooth;
        if (smooth < 1.f) smooth = 1.f;
        Vec3 final_ang{
            original_view_angles.x + best_delta.x / smooth,
            original_view_angles.y + best_delta.y / smooth,
            0.f
        };
        if (final_ang.x > 89.f) final_ang.x = 89.f;
        else if (final_ang.x < -89.f) final_ang.x = -89.f;
        final_ang.y = remainderf(final_ang.y, 360.f);

        input->set_view_angles(final_ang, va_match);

        if (config.aimbot.auto_shoot)
            fire(localpawn, true);
        else if (g_aim_firing)
            fire(localpawn, false);
    }

    void on_draw() override {
        if (!config.aimbot.draw_fov) return;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImVec2 center = vp->GetCenter();
        float r = (tanf(config.aimbot.fov / 180.f * 3.14159265f) /
                   tanf((90.f / 2.f) / 180.f * 3.14159265f) *
                   (vp->GetCenter().x / 2.f)) / 1.55f;
        dl->AddCircle(center, r, IM_COL32(255, 255, 255, 210), 64);
    }

    void on_menu() override {
        ImGui::Checkbox("Master", &config.aimbot.master);
        ImGui::Text("Key");
        ImGui::SameLine();
        ImGui::KeybindBox(&config.aimbot.key.waiting, &config.aimbot.key.button);
        ImGui::SliderFloatHeightPad("FOV", &config.aimbot.fov, 0.1f, 180.f, 1, "%.0f\xC2\xB0");
        ImGui::SliderFloatHeightPad("Smoothness", &config.aimbot.smooth, 1.f, 20.f, 1, "%.1f");
        ImGui::Checkbox("Draw FOV", &config.aimbot.draw_fov);
        ImGui::Checkbox("Recoil Compensation", &config.aimbot.recoil);
        ImGui::Checkbox("Auto Shoot", &config.aimbot.auto_shoot);
        ImGui::Text("va_off=0x%x  firing=%s", Input::view_angle_off, g_aim_firing ? "YES" : "no");
    }
};
REGISTER_FEATURE(AimbotFeature);
