#include "App.h"
#include "Pages.h"
#include "State.h"
#include "Widgets.h"

#include "saphire_colors.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <cstdio>
#include <cmath>

namespace ui
{

static bool  g_visible = true;
static float g_tab_alpha = 1.0f;
static int   g_tab = 0;
static int   g_active = 0;
static float g_ui_applied = -1.0f;
static ImVec2 g_pos(80.0f, 80.0f);

void AppInit()
{
    g_visible = true;
    g_tab = 0;
    g_active = 0;
    g_tab_alpha = 1.0f;
}

bool AppVisible() { return g_visible; }
void AppSetVisible(bool v) { g_visible = v; }

void AppFrame(float dt)
{
    SetDt(dt);

    if (!g_visible) return;

    Settings& cfg = Cfg();

    // 缩放变更：只登记重建请求（图集重建必须在 NewFrame() 之前，见 ov_gui.cpp）。
    // 样式与字体在重建时一起更新，这里不直接改 g_scale，避免字体与布局不同步。
    if (cfg.ui_scale != g_ui_applied)
    {
        RequestFontRebuild(cfg.ui_scale);
        g_ui_applied = cfg.ui_scale;
    }

    const float s = UiScale();
    const float W = 790.0f * s;
    const float H = 578.0f * s;
    const float L = 192.0f * s;

    ImGui::SetNextWindowSize(ImVec2(W, H), ImGuiCond_Always);
    ImGui::SetNextWindowPos(g_pos, ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    const ImGuiWindowFlags wf =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;

    if (!ImGui::Begin("##main", &g_visible, wf))
    {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        g_pos = ImGui::GetWindowPos();
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetWindowPos();

    dl->AddRectFilled(ImVec2(p.x + L, p.y), ImVec2(p.x + W, p.y + H), ImGui::GetColorU32(ImVec4(21.0f / 255.0f, 21.0f / 255.0f, 26.0f / 255.0f, 1.0f)), ImGui::GetStyle().WindowRounding, ImDrawFlags_RoundCornersRight);
    dl->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + L, p.y + H), ImGui::GetColorU32(ImVec4(35.0f / 255.0f, 32.0f / 255.0f, 45.0f / 255.0f, 200.0f / 255.0f)), ImGui::GetStyle().WindowRounding, ImDrawFlags_RoundCornersLeft);
    dl->AddRect(ImVec2(p.x, p.y), ImVec2(p.x + W, p.y + H), ImGui::GetColorU32(ImVec4(39.0f / 255.0f, 38.0f / 255.0f, 45.0f / 255.0f, 1.0f)), ImGui::GetStyle().WindowRounding, ImDrawFlags_None, 1.5f);
    dl->AddRectFilled(ImVec2(p.x + L, p.y), ImVec2(p.x + L + 1.5f * s, p.y + H), ImGui::GetColorU32(ImVec4(39.0f / 255.0f, 38.0f / 255.0f, 45.0f / 255.0f, 1.0f)), 0.0f);
    dl->AddLine(ImVec2(p.x + L, p.y + 45.0f * s), ImVec2(p.x + W, p.y + 45.0f * s), ImGui::GetColorU32(ImVec4(38.0f / 255.0f, 37.0f / 255.0f, 43.0f / 255.0f, 1.0f)), 0.5f);

    if (cruin_0)
        dl->AddText(cruin_0, 25.0f * s, ImVec2(p.x + 45.0f * s, p.y + 11.0f * s), ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), "崩坏 3");

    dl->AddRectFilledMultiColor(ImVec2(p.x + 47.0f * s, p.y + 64.0f * s), ImVec2(p.x + 91.0f * s, p.y + 65.0f * s),
                                ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.0f)), ImGui::GetColorU32(c::accent_color),
                                ImGui::GetColorU32(c::accent_color), ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.0f)));
    dl->AddRectFilledMultiColor(ImVec2(p.x + 91.0f * s, p.y + 64.0f * s), ImVec2(p.x + 141.0f * s, p.y + 65.0f * s),
                                ImGui::GetColorU32(c::accent_color), ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.0f)),
                                ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.0f)), ImGui::GetColorU32(c::accent_color));

    ImGui::SetCursorPos(ImVec2(L - 40.0f * s, 12.0f * s));
    if (ImGui::InvisibleButton("##close", ImVec2(24.0f * s, 24.0f * s)))
        g_visible = false;
    if (ImGui::IsItemHovered())
        dl->AddCircleFilled(ImVec2(p.x + L - 28.0f * s, p.y + 24.0f * s), 11.0f * s, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.08f)), 20);
    {
        const ImVec2 c0(p.x + L - 28.0f * s, p.y + 24.0f * s);
        const float r = 5.0f * s;
        dl->AddLine(ImVec2(c0.x - r, c0.y - r), ImVec2(c0.x + r, c0.y + r), ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.55f)), 1.5f);
        dl->AddLine(ImVec2(c0.x - r, c0.y + r), ImVec2(c0.x + r, c0.y - r), ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.55f)), 1.5f);
    }

    ImGui::SetCursorPosY(80.0f * s);
    ImGui::BeginGroup();
    {
        ImGui::SetCursorPosX(20.0f * s);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(77.0f / 255.0f, 80.0f / 255.0f, 97.0f / 255.0f, 1.0f));
        ImGui::TextUnformatted("MENU");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        for (int i = 0; i < (int)Page::Count; ++i)
        {
            if (ImGui::Tab(PageIcon((Page)i), PageName((Page)i), g_tab == i, ImVec2(200.0f * s, 37.0f * s), true, ImGuiButtonFlags_None))
                g_tab = i;
        }
    }
    ImGui::EndGroup();

    if (g_tab != g_active)
    {
        g_tab_alpha = ImClamp(g_tab_alpha - 5.0f * dt, 0.0f, 1.0f);
        if (g_tab_alpha <= 0.01f) { g_active = g_tab; g_tab_alpha = 0.0f; }
    }
    else
    {
        g_tab_alpha = ImClamp(g_tab_alpha + 5.0f * dt, 0.0f, 1.0f);
    }

    ImGui::SetCursorPos(ImVec2(205.0f * s, 62.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.15f + 0.85f * g_tab_alpha);
    ImGui::BeginChild("##content", ImVec2(566.0f * s, 495.0f * s), true, ImGuiWindowFlags_NoBackground);
    switch ((Page)g_active)
    {
    case Page::Home:   PageHome();   break;
    case Page::Esp:    PageEsp();    break;
    case Page::Battle: PageBattle(); break;
    case Page::Activity: PageActivity(); break;
    case Page::Ui:     PageUi();     break;
    case Page::About:  PageAbout();  break;
    default: break;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    g_pos = ImGui::GetWindowPos();
}

}
