#include "feature.hpp"
#include "../gui/config.hpp"
#include "../core/math.hpp"
#include "../core/memory.hpp"
#include "../sdk/entity_system.hpp"
#include "../sdk/pawn.hpp"
#include "dearimgui.hpp"
#include <cstdio>

static int  g_dbg_ctrl  = 0;
static int  g_dbg_pawn  = 0;
static int  g_dbg_w2s   = 0;
static int  g_dbg_drawn = 0;
static int  g_dbg_team  = 0;
static bool g_dbg_local = false;
static bool g_dbg_vm    = false;
static bool g_dbg_es    = false;
static float g_dbg_vm00 = 0.f;
static float g_dbg_vm11 = 0.f;
static float g_dbg_dsx  = 0.f;
static float g_dbg_dsy  = 0.f;

static bool bone_ok(const Vec3& b, const Vec3& origin) {
    if (b.x == 0.f && b.y == 0.f && b.z == 0.f) return false;
    float dx = b.x - origin.x, dy = b.y - origin.y, dz = b.z - origin.z;
    float d2 = dx * dx + dy * dy + dz * dz;
    return d2 > 1.f && d2 < 200.f * 200.f;
}

static void bone_line(ImDrawList* dl, Pawn* pawn, const Vec3& origin, int a, int b, ImU32 col) {
    Vec3 ba = pawn->get_bone_location((unsigned)a);
    Vec3 bb = pawn->get_bone_location((unsigned)b);
    if (!bone_ok(ba, origin) || !bone_ok(bb, origin)) return;
    Vec3 sa, sb;
    if (!world_to_screen(ba, &sa) || !world_to_screen(bb, &sb)) return;
    dl->AddLine(ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y), col, 1.5f);
}

struct EspFeature final : IFeature {
    const char* name() const override { return "ESP"; }
    const char* tab()  const override { return "ESP"; }

    void on_draw() override {
        g_dbg_ctrl = g_dbg_pawn = g_dbg_w2s = g_dbg_drawn = 0;
        g_dbg_team = 0;
        g_dbg_local = false;
        g_dbg_es = valid_ptr(entity_system);
        g_dbg_vm = view_matrix_ok();
        g_dbg_vm00 = view_matrix ? (*view_matrix)[0][0] : 0.f;
        g_dbg_vm11 = view_matrix ? (*view_matrix)[1][1] : 0.f;
        g_dbg_dsx = ImGui::GetIO().DisplaySize.x;
        g_dbg_dsy = ImGui::GetIO().DisplaySize.y;

        if (!config.esp.master) return;

        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        if (!dl) return;
        dl->Flags &= ~ImDrawListFlags_AntiAliasedLines;

        char dbg[192];
        snprintf(dbg, sizeof(dbg),
                 "ESP es:%s vm:%s local:%s t:%d  c:%d p:%d w:%d d:%d  vm00=%.2f ds=%.0fx%.0f",
                 g_dbg_es ? "Y" : "N",
                 g_dbg_vm ? "Y" : "N",
                 g_dbg_local ? "Y" : "N",
                 g_dbg_team, g_dbg_ctrl, g_dbg_pawn, g_dbg_w2s, g_dbg_drawn,
                 g_dbg_vm00, g_dbg_dsx, g_dbg_dsy);

        if (!g_dbg_es) {
            dl->AddText(ImVec2(20, 50), IM_COL32(255, 80, 80, 255), dbg);
            return;
        }

        Entity* localentity = entity_system->get_localentity();
        Pawn*   localpawn   = entity_system->get_localpawn();
        g_dbg_local = valid_ptr(localentity) && valid_ptr(localpawn);
        if (g_dbg_local)
            g_dbg_team = (int)localpawn->get_cs_team();

        for (unsigned i = 1; i <= 64; ++i) {
            Entity* entity = entity_system->entity_from_index(i);
            if (!valid_ptr(entity)) continue;
            ++g_dbg_ctrl;
            if (entity == localentity) continue;

            int handle = entity->get_pawn_handle();
            if (handle == -1) continue;
            unsigned idx = (unsigned)(handle & 0x7FFF);
            if (idx == 0 || idx > 0x4000) continue;

            Pawn* pawn = entity_system->pawn_from_pawn_handle(handle);
            if (!valid_ptr(pawn) || pawn == localpawn) continue;
            ++g_dbg_pawn;
            if (pawn->get_lifestate() || pawn->is_dormant()) continue;

            int team = (int)pawn->get_cs_team();
            if (team != 2 && team != 3) continue;
            if (g_dbg_local && (g_dbg_team == 2 || g_dbg_team == 3)
                && config.esp.skip_team && team == g_dbg_team) continue;

            int hp = pawn->get_health();
            if (hp <= 0 || hp > 200) continue;

            Vec3 origin = pawn->get_abs_origin();
            if (origin.x == 0.f && origin.y == 0.f && origin.z == 0.f) continue;

            Vec3 s_feet, s_head;
            Vec3 head_world{ origin.x, origin.y, origin.z + 72.f };
            if (!world_to_screen(origin, &s_feet)) continue;
            if (!world_to_screen(head_world, &s_head)) continue;
            ++g_dbg_w2s;

            float hgt = s_feet.y - s_head.y;
            if (hgt < 8.f || hgt > 800.f) continue;
            float wid = hgt * 0.45f;

            ImVec2 minv(s_head.x - wid * 0.5f, s_head.y);
            ImVec2 maxv(s_head.x + wid * 0.5f, s_feet.y);
            ++g_dbg_drawn;

            if (config.esp.player.box)
                dl->AddRect(minv, maxv, config.esp.player.box_color.to_ImU32(), 0, 0, 1.5f);

            if (config.esp.player.health_bar) {
                float frac = ((hp > 100) ? 100 : hp) / 100.f;
                dl->AddRectFilled(ImVec2(minv.x - 5, maxv.y - hgt * frac),
                                  ImVec2(minv.x - 2, maxv.y),
                                  IM_COL32(0, 255, 0, 220));
            }
            if (config.esp.player.health_text) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", hp);
                dl->AddText(ImVec2(minv.x - 8, minv.y - 14), IM_COL32(255, 255, 255, 255), buf);
            }
            if (config.esp.player.name) {
                const char* name = entity->get_name();
                if (valid_ptr(name) && (unsigned char)name[0] >= 32 && (unsigned char)name[0] < 127)
                    dl->AddText(ImVec2(minv.x, minv.y - 28),
                                config.esp.player.name_color.to_ImU32(), name);
            }
            if (config.esp.player.skeleton) {
                ImU32 col = config.esp.player.skeleton_color.to_ImU32();
                bone_line(dl, pawn, origin, hip, spine1, col);
                bone_line(dl, pawn, origin, spine1, spine2, col);
                bone_line(dl, pawn, origin, spine2, spine3, col);
                bone_line(dl, pawn, origin, spine3, spine4, col);
                bone_line(dl, pawn, origin, spine4, neck, col);
                bone_line(dl, pawn, origin, neck, (int)Bone::head, col);
                bone_line(dl, pawn, origin, spine4, left_shoulder, col);
                bone_line(dl, pawn, origin, left_shoulder, left_elbow, col);
                bone_line(dl, pawn, origin, left_elbow, left_hand, col);
                bone_line(dl, pawn, origin, spine4, right_shoulder, col);
                bone_line(dl, pawn, origin, right_shoulder, right_elbow, col);
                bone_line(dl, pawn, origin, right_elbow, right_hand, col);
                bone_line(dl, pawn, origin, hip, left_hip, col);
                bone_line(dl, pawn, origin, left_hip, left_knee, col);
                bone_line(dl, pawn, origin, left_knee, left_foot, col);
                bone_line(dl, pawn, origin, hip, right_hip, col);
                bone_line(dl, pawn, origin, right_hip, right_knee, col);
                bone_line(dl, pawn, origin, right_knee, right_foot, col);
            }
            if (config.esp.player.target_indicator && pawn == target_pawn) {
                dl->AddText(ImVec2(minv.x, maxv.y + 2),
                            config.esp.player.target_color.to_ImU32(), "TARGET");
            }
        }

        snprintf(dbg, sizeof(dbg),
                 "ESP es:%s vm:%s local:%s t:%d  c:%d p:%d w:%d d:%d  vm00=%.2f ds=%.0fx%.0f",
                 g_dbg_es ? "Y" : "N",
                 g_dbg_vm ? "Y" : "N",
                 g_dbg_local ? "Y" : "N",
                 g_dbg_team, g_dbg_ctrl, g_dbg_pawn, g_dbg_w2s, g_dbg_drawn,
                 g_dbg_vm00, g_dbg_dsx, g_dbg_dsy);
        dl->AddText(ImVec2(20, 50), IM_COL32(255, 255, 0, 255), dbg);
    }

    void on_menu() override {
        ImGui::Checkbox("ESP master", &config.esp.master);
        ImGui::Checkbox("Skip teammates", &config.esp.skip_team);
        ImGui::Separator();
        ImGui::Checkbox("Box", &config.esp.player.box);
        ImGui::SameLine();
        ImGui::ColorEdit4("##box", config.esp.player.box_color.to_arr(),
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip);
        ImGui::Checkbox("Health bar", &config.esp.player.health_bar);
        ImGui::SameLine();
        ImGui::Checkbox("Health text", &config.esp.player.health_text);
        ImGui::Checkbox("Name", &config.esp.player.name);
        ImGui::SameLine();
        ImGui::ColorEdit4("##name", config.esp.player.name_color.to_arr(),
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip);
        ImGui::Checkbox("Skeleton", &config.esp.player.skeleton);
        ImGui::SameLine();
        ImGui::ColorEdit4("##sk", config.esp.player.skeleton_color.to_arr(),
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip);
        ImGui::Checkbox("Target flag", &config.esp.player.target_indicator);
        ImGui::Separator();
        ImGui::Text("es:%s  vm:%s  local:%s  team:%d",
                    g_dbg_es ? "Y" : "N", g_dbg_vm ? "Y" : "N",
                    g_dbg_local ? "Y" : "N", g_dbg_team);
        ImGui::Text("ctrl=%d  pawn=%d  w2s=%d  drawn=%d",
                    g_dbg_ctrl, g_dbg_pawn, g_dbg_w2s, g_dbg_drawn);
        ImGui::Text("vm00=%.3f  vm11=%.3f  ds=%.0fx%.0f",
                    g_dbg_vm00, g_dbg_vm11, g_dbg_dsx, g_dbg_dsy);
        ImGui::TextWrapped("ctrl=0 entity list/GRS | pawn=0 handle | w2s=0 viewmatrix | drawn=0 filters");
    }
};
REGISTER_FEATURE(EspFeature);
