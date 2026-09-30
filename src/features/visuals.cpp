#include "feature.hpp"
#include "../gui/config.hpp"
#include "../core/math.hpp"
#include "../core/memory.hpp"
#include "../sdk/input.hpp"
#include "../sdk/entity_system.hpp"
#include "../sdk/pawn.hpp"
#include "../sdk/cvar.hpp"
#include "dearimgui.hpp"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct VisualsFeature final : IFeature {
    const char* name() const override { return "Visuals"; }
    const char* tab()  const override { return "Visuals"; }

    void on_create_move() override {
        if (!config.visuals.override_fov) return;
        Entity* local = entity_system ? entity_system->get_localentity() : nullptr;
        if (valid_ptr(local))
            local->set_fov((int)config.visuals.custom_fov);
        if (cvar_system) {
            if (Convar* cv = cvar_system->get_convar("viewmodel_fov"))
                cv->set_value<float>((float)config.visuals.custom_viewmodel_fov);
        }
    }

    void on_draw() override {
        if (!config.visuals.hat.enabled) return;
        if (!input || !input->is_thirdperson()) return;
        if (!entity_system) return;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->Flags |= ImDrawListFlags_AntiAliasedLines;

        Pawn* localpawn = entity_system->get_localpawn();
        if (!valid_ptr(localpawn)) return;

        Vec3 location = localpawn->get_bone_location(Bone::head);
        Vec3 prev{};
        const float step = (float)(M_PI * 2.0 / 45.0);
        for (float rot = 0.f; rot <= (float)M_PI * 2.f + step; rot += step) {
            Vec3 pos{
                location.x + config.visuals.hat.radius * cosf(rot),
                location.y + config.visuals.hat.radius * sinf(rot),
                location.z + config.visuals.hat.z_base
            };
            Vec3 screen;
            if (!world_to_screen(pos, &screen)) continue;
            if (prev.x != 0.f || prev.y != 0.f || prev.z != 0.f) {
                dl->AddLine(ImVec2(prev.x, prev.y), ImVec2(screen.x, screen.y),
                            config.visuals.hat.color.to_ImU32(), 2.f);
                if (config.visuals.hat.rice) {
                    Vec3 tip = location;
                    tip.z += config.visuals.hat.z_base + config.visuals.hat.z_tip;
                    Vec3 stip;
                    if (world_to_screen(tip, &stip)) {
                        dl->AddLine(ImVec2(prev.x, prev.y), ImVec2(stip.x, stip.y),
                                    config.visuals.hat.color.to_ImU32());
                        dl->AddLine(ImVec2(screen.x, screen.y), ImVec2(stip.x, stip.y),
                                    config.visuals.hat.color.to_ImU32());
                    }
                }
            }
            prev = screen;
        }
    }

    void on_menu() override {
        ImGui::Checkbox("Override FOV", &config.visuals.override_fov);
        ImGui::SliderFloat("##fov", &config.visuals.custom_fov, 30.1f, 150.f, "%.0f\xC2\xB0");
        ImGui::SliderInt("##viewmodelfov", &config.visuals.custom_viewmodel_fov, 40, 150, "%d\xC2\xB0");
        ImGui::Separator();
        ImGui::Text("Hat");
        ImGui::Checkbox("Enable##hat", &config.visuals.hat.enabled);
        ImGui::SameLine();
        ImGui::ColorEdit4("##HatColor", config.visuals.hat.color.to_arr(),
                          ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs);
        ImGui::Checkbox("Rice", &config.visuals.hat.rice);
        ImGui::SliderFloat("Radius", &config.visuals.hat.radius, 1.f, 50.f, "%.0f");
        ImGui::SliderFloat("Base offset", &config.visuals.hat.z_base, 0.f, 50.f, "%.0f");
        ImGui::SliderFloat("Tip offset", &config.visuals.hat.z_tip, 0.f, 50.f, "%.0f");
    }
};
REGISTER_FEATURE(VisualsFeature);
