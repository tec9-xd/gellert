#include "feature.hpp"
#include "../gui/config.hpp"
#include "dearimgui.hpp"

struct MiscFeature final : IFeature {
    const char* name() const override { return "Misc"; }
    const char* tab()  const override { return "Misc"; }

    void on_menu() override {
        ImGui::Checkbox("Debug", &config.misc.debug);

        if (!config.misc.debug)
            return;

        ImGui::Separator();
        ImGui::TextDisabled("loaded:");
        for (auto* f : FeatureRegistry::get().all())
            ImGui::TextDisabled("  %s [%s]", f->name(), f->tab());
    }
};
REGISTER_FEATURE(MiscFeature);