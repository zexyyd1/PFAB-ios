#include "bot.hpp"
#include "settings.hpp"
#include "trajectory.hpp"
#include "xs.hpp"

#include <Geode/Geode.hpp>
#include <imgui-cocos.hpp>

#include <algorithm>
#include <cfloat>

#ifdef GEODE_IS_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#endif

using namespace geode::prelude;

static bool g_open = false;

static void setupStyle() {
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding   = 6.f;
    style.ChildRounding    = 6.f;
    style.FrameRounding    = 4.f;
    style.GrabRounding     = 4.f;
    style.TabRounding      = 4.f;
    style.PopupRounding    = 4.f;
    style.WindowBorderSize = 1.f;
    style.WindowPadding    = ImVec2(12, 10);
    style.FramePadding     = ImVec2(8, 4);
    style.ItemSpacing      = ImVec2(8, 7);

    const ImVec4 accent    = ImVec4(0.89f, 0.19f, 0.38f, 1.00f);
    const ImVec4 accentDim = ImVec4(0.89f, 0.19f, 0.38f, 0.55f);
    auto& c = style.Colors;
    c[ImGuiCol_WindowBg]         = ImVec4(0.08f, 0.08f, 0.10f, 0.98f);
    c[ImGuiCol_Border]           = ImVec4(0.30f, 0.30f, 0.36f, 0.50f);
    c[ImGuiCol_Text]             = ImVec4(0.92f, 0.92f, 0.94f, 1.00f);
    c[ImGuiCol_TitleBg]          = accentDim;
    c[ImGuiCol_TitleBgActive]    = accent;
    c[ImGuiCol_TitleBgCollapsed] = accentDim;
    c[ImGuiCol_FrameBg]          = ImVec4(0.17f, 0.17f, 0.21f, 1.00f);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    c[ImGuiCol_FrameBgActive]    = ImVec4(0.30f, 0.30f, 0.36f, 1.00f);
    c[ImGuiCol_CheckMark]        = accent;
    c[ImGuiCol_SliderGrab]       = accent;
    c[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.32f, 0.50f, 1.00f);
    c[ImGuiCol_Button]           = ImVec4(0.20f, 0.20f, 0.25f, 1.00f);
    c[ImGuiCol_ButtonHovered]    = accentDim;
    c[ImGuiCol_ButtonActive]     = accent;
    c[ImGuiCol_Header]           = accentDim;
    c[ImGuiCol_HeaderHovered]    = accent;
    c[ImGuiCol_HeaderActive]     = accent;
    c[ImGuiCol_Tab]              = ImVec4(0.17f, 0.17f, 0.21f, 1.00f);
    c[ImGuiCol_TabHovered]       = accent;
    c[ImGuiCol_TabActive]        = ImVec4(0.55f, 0.15f, 0.28f, 1.00f);
    c[ImGuiCol_Separator]        = ImVec4(0.30f, 0.30f, 0.36f, 0.50f);
}

static void drawMenu() {
#ifdef GEODE_IS_WINDOWS
    static bool prevDel = false;
    bool del = (GetAsyncKeyState(VK_DELETE) & 0x8000) != 0;
    if (del && !prevDel) g_open = !g_open;
    prevDel = del;
#else
    // No keyboard on mobile: small translucent button to open the menu.
    if (!g_open) {
        ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.35f);
        if (ImGui::Begin("##pfabopen", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoFocusOnAppearing)) {
            if (ImGui::Button("PFAB")) g_open = true;
        }
        ImGui::End();
    }
#endif
    if (!g_open) return;

    int stage = cfg::stage();
    char const* stages[] = { XS("Off"), XS("2 ticks"), XS("4 ticks"), XS("8 ticks"), XS("16 ticks"), XS("32 ticks (test)"), XS("64 ticks (test)") };

    char const* title = XS("Rawrotic's Predictive Input Buffer & Adaptive Auto Bot");
    ImGui::SetNextWindowPos(ImVec2(60, 60), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::CalcTextSize(title).x + 40.f, 0.f), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::Begin(title, &g_open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::BeginTabBar(XS("##t"))) {
            if (ImGui::BeginTabItem(XS("Input"))) {
                ImGui::SetNextItemWidth(130);
                if (ImGui::Combo(XS("Predictive Buffer"), &stage, stages, IM_ARRAYSIZE(stages))) cfg::setStage(stage);
                ImGui::Checkbox(XS("Show Trajectory"), &traj::show);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(XS("Bot"))) {
                ImGui::Checkbox(XS("Auto Play"), &bot::on);
                ImGui::Checkbox(XS("Show Path"), &bot::showPath);
                if (ImGui::Button(XS("Forget Mistakes"))) bot::forget();
                ImGui::SameLine();
                ImGui::TextDisabled(XS("%zu learned"), bot::learnedSpots());
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

$on_mod(Loaded) {
    ImGuiCocos::get()
        .setup([] { setupStyle(); })
        .draw([] { drawMenu(); });
}
