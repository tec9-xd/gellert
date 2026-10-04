#include "menu.hpp"
#include "config.hpp"
#include "../features/feature.hpp"
#include "dearimgui.hpp"

void get_input(SDL_Event* event) {
    ImGui::KeybindEvent(event, &config.aimbot.key.waiting, &config.aimbot.key.button);
    ImGui::KeybindEvent(event, &config.visuals.thirdperson.key.waiting, &config.visuals.thirdperson.key.button);
}

void draw_watermark() {
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(80, 30));
    ImGui::Begin("##Watermark", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    ImGui::TextCentered("Gellert v1");
    ImGui::End();
}

static void draw_tab_btn(ImGuiStyle* style, const char* name, int* tab, int index) {
    ImVec4 orig_box_color = ImVec4(0.15f, 0.15f, 0.15f, 1.f);
    if (*tab == index)
        style->Colors[ImGuiCol_Button] = ImVec4(orig_box_color.x + 0.15f, orig_box_color.y + 0.15f, orig_box_color.z + 0.15f, 1.f);
    else
        style->Colors[ImGuiCol_Button] = ImVec4(0.15f, 0.15f, 0.15f, 1.f);
    if (ImGui::Button(name, ImVec2(100, 30)))
        *tab = index;
    style->Colors[ImGuiCol_Button] = ImVec4(0.15f, 0.15f, 0.15f, 1.f);
}

void draw_menu() {
    orig_style = ImGui::GetStyle();
    ImGui::SetNextWindowSize(ImVec2(600, 350));
    if (!ImGui::Begin("Gellert v1", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGuiStyle* style = &ImGui::GetStyle();

    static const char* tabs[] = { "Aimbot", "RageBot", "ESP", "Movement", "Visuals", "Sounds", "Misc" };
    constexpr int tab_count = (int)(sizeof(tabs) / sizeof(tabs[0]));

    static int tab = 0;
    if (tab >= tab_count) tab = 0;

    ImGui::BeginGroup();
    for (int i = 0; i < tab_count; ++i)
        draw_tab_btn(style, tabs[i], &tab, i);

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 27);
    ImGui::Text(" ");
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    ImGui::BeginChild("##TabBody");
    if (tab >= 0 && tab < tab_count)
        FeatureRegistry::get().menu_for_tab(tabs[tab]);
    ImGui::EndChild();

    ImGui::End();
}
