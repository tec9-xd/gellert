#include "feature.hpp"
#include "../gui/config.hpp"
#include "../sdk/input.hpp"
#include "../sdk/entity_system.hpp"
#include "dearimgui.hpp"

struct ThirdpersonFeature final : IFeature {
    const char* name() const override { return "Thirdperson"; }
    const char* tab()  const override { return "Visuals"; }

    void on_create_move() override {
        if (!input) return;
        if (!config.visuals.thirdperson.enabled) {
            input->set_thirdperson(false);
            return;
        }
        static bool was_pressed = false;
        static bool on = false;
        bool down = is_down(config.visuals.thirdperson.key);
        if (!was_pressed && down) { on = !on; was_pressed = true; }
        else if (was_pressed && !down) was_pressed = false;
        input->set_thirdperson(on);
    }

    void on_menu() override {
        ImGui::Text("Thirdperson key");
        ImGui::SameLine();
        ImGui::KeybindBox(&config.visuals.thirdperson.key.waiting, &config.visuals.thirdperson.key.button);
        ImGui::Checkbox("Thirdperson", &config.visuals.thirdperson.enabled);
    }
};
REGISTER_FEATURE(ThirdpersonFeature);
