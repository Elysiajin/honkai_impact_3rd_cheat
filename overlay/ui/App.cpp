#include "App.h"
#include "Pages.h"
#include "State.h"
#include "Widgets.h"
#include "Glass.h"

#include "saphire_colors.h"
#include "imgui.h"
#include "imgui_internal.h"

#include "../ov_mcp.h"

#include <cstdio>
#include <cfloat>
#include <cmath>

namespace ui
{

static bool  g_visible = true;
static float g_tab_alpha = 1.0f;
static int   g_tab = 0;
static int   g_active = 0;
static float g_ui_applied = -1.0f;
static int   g_theme_applied = -1;
static float g_glass_applied = -1.0f;
static ImVec2 g_pos(80.0f, 80.0f);

static const char* kEyebrows[] = {
    "OVERVIEW  /  01", "DETECTION  /  02", "COMBAT  /  03", "ACTIVITY  /  04",
    "SCRIPT  /  05", "SERVICE  /  06", "APPEARANCE  /  07", "ABOUT  /  08"
};
static const char* kSubtitles[] = {
    "运行状态与快捷操作", "人物与物资标记", "战斗功能开关", "活动辅助",
    "Lua 脚本控制台", "MCP 服务接口", "显示与布局", "项目信息"
};

// 侧栏矢量图标：直接用 ImDrawList 图元绘制，语义与页面功能一一对应，
// 取代原先 kIcons 里 A/B/C/D/G/H/E/F 的字母-字形映射。
// 绘制范围为以 center 为中心、半径 r 的正方形，坐标用 [-1,1] 归一化。
static void DrawNavIcon(ImDrawList* dl, Page page, const ImVec2& center, float r, ImU32 col)
{
    const auto P = [&](float x, float y) { return ImVec2(center.x + x * r, center.y + y * r); };
    const float t = ImMax(1.0f, r * 0.16f);
    const int   seg = 12;

    switch (page)
    {
    case Page::Home: // 仪表盘：半圆弧 + 指针 + 底座
    {
        ImVec2 arc[13];
        for (int i = 0; i <= 12; ++i)
        {
            const float a = 3.14159265f - 3.14159265f * i / 12.0f;
            arc[i] = P(cosf(a) * 0.82f, -sinf(a) * 0.82f);
        }
        dl->AddPolyline(arc, 13, col, false, t);
        dl->AddLine(P(-0.82f, 0.0f), P(-0.82f, 0.22f), col, t);
        dl->AddLine(P( 0.82f, 0.0f), P( 0.82f, 0.22f), col, t);
        dl->AddLine(center, P(0.34f, -0.42f), col, t);
        dl->AddCircleFilled(center, t * 0.9f, col, 8);
        break;
    }
    case Page::Esp: // 眼睛：杏仁形轮廓 + 瞳孔
    {
        ImVec2 eye[10];
        eye[0] = P(-1.00f,  0.00f);
        eye[1] = P(-0.50f, -0.60f);
        eye[2] = P( 0.00f, -0.78f);
        eye[3] = P( 0.50f, -0.60f);
        eye[4] = P( 1.00f,  0.00f);
        eye[5] = P( 0.50f,  0.60f);
        eye[6] = P( 0.00f,  0.78f);
        eye[7] = P(-0.50f,  0.60f);
        eye[8] = eye[0];
        dl->AddPolyline(eye, 9, col, false, t);
        dl->AddCircle(center, r * 0.30f, col, seg, t);
        dl->AddCircleFilled(center, t * 0.8f, col, 8);
        break;
    }
    case Page::Battle: // 交叉双剑：两条利刃 + 护手
    {
        dl->AddLine(P(-0.78f, -0.78f), P(0.72f, 0.72f), col, t);
        dl->AddLine(P( 0.78f, -0.78f), P(-0.72f, 0.72f), col, t);
        dl->AddLine(P(-0.92f, -0.34f), P(-0.34f, -0.92f), col, t);
        dl->AddLine(P( 0.92f, -0.34f), P( 0.34f, -0.92f), col, t);
        dl->AddCircleFilled(P(-0.80f, -0.80f), t * 0.7f, col, 8);
        dl->AddCircleFilled(P( 0.80f, -0.80f), t * 0.7f, col, 8);
        break;
    }
    case Page::Activity: // 日历：箱体 + 挂环 + 日期格
    {
        dl->AddRect(P(-0.86f, -0.60f), P(0.86f, 0.84f), col, t, 0, t);
        dl->AddLine(P(-0.86f, -0.20f), P(0.86f, -0.20f), col, t);
        dl->AddLine(P(-0.44f, -0.96f), P(-0.44f, -0.45f), col, t);
        dl->AddLine(P( 0.44f, -0.96f), P( 0.44f, -0.45f), col, t);
        dl->AddCircleFilled(P(-0.42f, 0.20f), t * 0.7f, col, 6);
        dl->AddCircleFilled(P( 0.00f, 0.20f), t * 0.7f, col, 6);
        dl->AddCircleFilled(P( 0.42f, 0.20f), t * 0.7f, col, 6);
        dl->AddCircleFilled(P(-0.42f, 0.58f), t * 0.7f, col, 6);
        dl->AddCircleFilled(P( 0.00f, 0.58f), t * 0.7f, col, 6);
        break;
    }
    case Page::Lua: // 终端：外框 + >_ 提示符
    {
        dl->AddRect(P(-0.92f, -0.74f), P(0.92f, 0.74f), col, t, 0, t);
        dl->AddLine(P(-0.58f, -0.34f), P(-0.12f, 0.00f), col, t);
        dl->AddLine(P(-0.12f,  0.00f), P(-0.58f, 0.34f), col, t);
        dl->AddLine(P(-0.02f,  0.46f), P(0.58f, 0.46f), col, t);
        break;
    }
    case Page::Mcp: // 网络服务：中心节点 + 三路连接
    {
        const ImVec2 n[3] = { P(0.0f, -0.78f), P(-0.76f, 0.52f), P(0.76f, 0.52f) };
        for (int i = 0; i < 3; ++i) dl->AddLine(center, n[i], col, t * 0.85f);
        dl->AddCircleFilled(center, r * 0.24f, col, seg);
        for (int i = 0; i < 3; ++i) dl->AddCircleFilled(n[i], r * 0.15f, col, seg);
        break;
    }
    case Page::Ui: // 滑块：三条轨道 + 手柄
    {
        const float ys[3] = { -0.60f, 0.0f, 0.60f };
        const float xs[3] = { -0.30f, 0.46f, -0.02f };
        for (int i = 0; i < 3; ++i)
        {
            dl->AddLine(P(-0.92f, ys[i]), P(0.92f, ys[i]), col, t * 0.85f);
            dl->AddCircleFilled(P(xs[i], ys[i]), r * 0.22f, col, seg);
        }
        break;
    }
    case Page::About: // 信息：圆圈 + i
    {
        dl->AddCircle(center, r * 0.88f, col, seg, t);
        dl->AddCircleFilled(P(0.0f, -0.44f), t * 0.85f, col, 8);
        dl->AddLine(P(0.0f, -0.16f), P(0.0f, 0.56f), col, t * 1.1f);
        break;
    }
    default:
        dl->AddRect(P(-0.7f, -0.7f), P(0.7f, 0.7f), col, t, 0, t);
        break;
    }
}

void AppInit()
{
    g_visible = true;
    g_tab = 0;
    g_active = 0;
    g_tab_alpha = 1.0f;
}

bool AppVisible() { return g_visible; }
void AppSetVisible(bool v) { g_visible = v; }

static void DrawToasts(float s)
{
    const char* msgs[4] = {};
    float alphas[4] = {};
    const int n = ToastList(msgs, alphas, 4);
    if (n <= 0) return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const int i = n - 1;
    const float fs = 13.0f * s;
    const float tw = ubu_child ? ubu_child->CalcTextSizeA(fs, FLT_MAX, 0.0f, msgs[i]).x
                               : ImGui::CalcTextSize(msgs[i]).x;
    const float w = tw + 42.0f * s;
    const float h = 34.0f * s;
    const ImVec2 p(io.DisplaySize.x * 0.5f - w * 0.5f, io.DisplaySize.y - 48.0f * s);

    glass::Panel(dl, p, ImVec2(w, h), h * 0.5f, s, alphas[i] * 0.95f, 0.42f);
    if (ubu_child)
        dl->AddText(ubu_child, fs, ImVec2(p.x + 21.0f * s, p.y + (h - fs) * 0.5f),
                    CU(theme::TextHi, alphas[i]), msgs[i]);
    else
        dl->AddText(ImVec2(p.x + 21.0f * s, p.y + (h - fs) * 0.5f),
                    CU(theme::TextHi, alphas[i]), msgs[i]);
}

void AppFrame(float dt)
{
    SetDt(dt);
    ToastTick(dt);

    Settings& cfg = Cfg();
    ovmcp::Sync(cfg.mcp_on, (unsigned short)cfg.mcp_port);

    if (!g_visible) return;

    if (cfg.theme != g_theme_applied || cfg.glass_alpha != g_glass_applied)
    {
        glass::SetOpacityScale(cfg.glass_alpha);
        glass::ApplyPaletteColors(cfg.theme);
        g_theme_applied = cfg.theme;
        g_glass_applied = cfg.glass_alpha;
    }

    // 缩放变更：只登记重建请求（图集重建必须在 NewFrame() 之前，见 ov_gui.cpp）。
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
    const glass::Palette pal = glass::Material();
    const float time = cfg.anim_on ? ImGui::GetTime() : 0.0f;

    // 整窗玻璃 + 星点：先铺底，再放侧栏与头部卡片。
    glass::Shell(dl, p, ImVec2(W, H), 20.0f * s, s, 1.0f);
    glass::Speckles(dl, p, ImVec2(W, H), s, 0.65f, time, cfg.star_count);
    glass::Panel(dl, ImVec2(p.x + 8.0f * s, p.y + 8.0f * s),
                 ImVec2(L - 16.0f * s, H - 16.0f * s), 16.0f * s, s, 0.75f, 0.06f);

    const ImVec2 badge(p.x + 20.0f * s, p.y + 18.0f * s);
    const float bs = 44.0f * s;
    glass::Panel(dl, badge, ImVec2(bs, bs), 13.0f * s, s, 0.85f, 0.12f);
    const ImVec2 ribbon[] = {
        ImVec2(badge.x + bs * 0.72f, badge.y + bs * 0.28f),
        ImVec2(badge.x + bs * 0.42f, badge.y + bs * 0.28f),
        ImVec2(badge.x + bs * 0.29f, badge.y + bs * 0.405f),
        ImVec2(badge.x + bs * 0.70f, badge.y + bs * 0.595f),
        ImVec2(badge.x + bs * 0.575f, badge.y + bs * 0.72f),
        ImVec2(badge.x + bs * 0.28f, badge.y + bs * 0.72f)
    };
    dl->AddPolyline(ribbon, 6, glass::Col(glass::Opacity(pal.ink, 0.95f)), 0, bs * 0.095f);
    float titleY = badge.y + 3.0f * s;
    if (cruin_0)
    {
        dl->AddText(cruin_0, 21.0f * s, ImVec2(badge.x + bs + 12.0f * s, titleY),
                    CU(theme::TextHi), "崩坏 3");
        titleY += 28.0f * s;
    }
    if (ubu_preview)
        dl->AddText(ubu_preview, 9.5f * s, ImVec2(badge.x + bs + 13.0f * s, titleY),
                    CU(theme::TextMid), "BH3  /  OVERLAY UI");

    dl->AddLine(ImVec2(p.x + 20.0f * s, p.y + 78.0f * s),
                ImVec2(p.x + L - 20.0f * s, p.y + 78.0f * s),
                CU(theme::TextLow, 0.22f), 1.0f * s);
    if (ubu_preview)
    {
        dl->AddText(ubu_preview, 10.0f * s, ImVec2(p.x + 22.0f * s, p.y + 88.0f * s),
                    CU(theme::TextMid), "功能空间");
        dl->AddText(ubu_preview, 10.0f * s, ImVec2(p.x + L - 42.0f * s, p.y + 88.0f * s),
                    CU(theme::Accent, 0.85f), "08");
    }

    // 侧栏导航：玻璃行 + 图标方块 + 主副标题 + 序号。
    for (int i = 0; i < (int)Page::Count; ++i)
    {
        const float y = 112.0f * s + i * 43.0f * s;
        ImGui::SetCursorScreenPos(ImVec2(p.x + 16.0f * s, p.y + y));
        const ImVec2 row_sz(L - 32.0f * s, 38.0f * s);
        const bool clicked = ImGui::InvisibleButton(PageName((Page)i), row_sz);
        const bool hovered = ImGui::IsItemHovered();
        if (clicked) g_tab = i;

        const ImVec2 rp(p.x + 16.0f * s, p.y + y);
        glass::Panel(dl, rp, row_sz, 10.0f * s, s, 1.0f,
                     g_tab == i ? 0.30f : (hovered ? 0.16f : 0.03f));
        if (g_tab == i)
        {
            dl->AddRectFilled(ImVec2(rp.x + 2.0f * s, rp.y + 9.0f * s),
                              ImVec2(rp.x + 4.5f * s, rp.y + row_sz.y - 9.0f * s),
                              CU(theme::Accent, 0.85f), 2.0f * s);
        }
        const float icon = 26.0f * s;
        const ImVec2 ip(rp.x + 9.0f * s, rp.y + (row_sz.y - icon) * 0.5f);
        dl->AddRectFilled(ip, ImVec2(ip.x + icon, ip.y + icon),
                          CU(g_tab == i ? theme::AccentSoft : theme::Border, 0.75f), 8.0f * s);
        DrawNavIcon(dl, (Page)i, ImVec2(ip.x + icon * 0.5f, ip.y + icon * 0.5f),
                    icon * 0.32f, CU(theme::TextHi, g_tab == i ? 1.0f : 0.82f));
        if (ubu_child)
            dl->AddText(ubu_child, 15.0f * s, ImVec2(rp.x + 44.0f * s, rp.y + 5.0f * s),
                        CU(theme::TextHi), PageName((Page)i));
        if (ubu_preview)
        {
            dl->AddText(ubu_preview, 9.5f * s, ImVec2(rp.x + 44.0f * s, rp.y + 23.0f * s),
                        CU(theme::TextMid), kSubtitles[i]);
            char idx[8];
            snprintf(idx, sizeof idx, "%02d", i + 1);
            dl->AddText(ubu_preview, 10.0f * s,
                        ImVec2(rp.x + row_sz.x - 29.0f * s, rp.y + 13.0f * s),
                        CU(theme::Accent, g_tab == i ? 0.95f : 0.40f), idx);
        }
    }

    if (ubu_preview)
        dl->AddText(ubu_preview, 9.0f * s, ImVec2(p.x + 22.0f * s, p.y + H - 26.0f * s),
                    CU(theme::TextLow), "BH3  /  OVERLAY  ·  V2.1");

    // 右上角关闭：浅色玻璃圆底 + 深墨叉。
    ImGui::SetCursorPos(ImVec2(L - 40.0f * s, 12.0f * s));
    if (ImGui::InvisibleButton("##close", ImVec2(24.0f * s, 24.0f * s)))
        g_visible = false;
    const ImVec2 cc(p.x + L - 28.0f * s, p.y + 24.0f * s);
    if (ImGui::IsItemHovered())
        dl->AddCircleFilled(cc, 12.0f * s, CU(theme::Accent, 0.14f), 20);
    else
        dl->AddCircleFilled(cc, 11.0f * s, CU(0xFFFFFFu, 0.28f), 20);
    const float cr = 4.5f * s;
    const ImU32 cx = CU(theme::TextMid, 0.9f);
    dl->AddLine(ImVec2(cc.x - cr, cc.y - cr), ImVec2(cc.x + cr, cc.y + cr), cx, 1.4f * s);
    dl->AddLine(ImVec2(cc.x - cr, cc.y + cr), ImVec2(cc.x + cr, cc.y - cr), cx, 1.4f * s);

    // 页眉：小字眉标 + 页面标题 + 副标题 + MCP 状态胶囊。
    const ImVec2 hp(p.x + L + 16.0f * s, p.y + 16.0f * s);
    const ImVec2 hs(W - L - 32.0f * s, 74.0f * s);
    glass::Panel(dl, hp, hs, 16.0f * s, s, 0.95f, 0.20f);
    const int page_i = (int)g_active;
    if (ubu_preview)
        dl->AddText(ubu_preview, 10.0f * s, ImVec2(hp.x + 16.0f * s, hp.y + 9.0f * s),
                    CU(theme::TextMid), kEyebrows[page_i]);
    if (ubu_1)
        dl->AddText(ubu_1, 25.0f * s, ImVec2(hp.x + 15.0f * s, hp.y + 23.0f * s),
                    CU(theme::TextHi), PageName((Page)g_active));
    if (ubu_preview)
        dl->AddText(ubu_preview, 11.0f * s, ImVec2(hp.x + 17.0f * s, hp.y + 54.0f * s),
                    CU(theme::TextMid), kSubtitles[page_i]);

    const char* mcp_txt = cfg.mcp_on ? "MCP 运行中" : "MCP 已停止";
    const float mcp_w = 96.0f * s;
    const ImVec2 mp(hp.x + hs.x - mcp_w - 14.0f * s, hp.y + 20.0f * s);
    glass::Panel(dl, mp, ImVec2(mcp_w, 34.0f * s), 17.0f * s, s, 0.95f, 0.15f);
    if (ubu_preview)
    {
        const float tw = ubu_preview->CalcTextSizeA(11.0f * s, FLT_MAX, 0.0f, mcp_txt).x;
        dl->AddText(ubu_preview, 11.0f * s, ImVec2(mp.x + (mcp_w - tw) * 0.5f, mp.y + 10.0f * s),
                    CU(cfg.mcp_on ? theme::Success : theme::TextLow), mcp_txt);
    }

    // 页面切换动效保留原有淡入淡出，只改内容区起点。
    if (!cfg.anim_on)
    {
        g_active = g_tab;
        g_tab_alpha = 1.0f;
    }
    else if (g_tab != g_active)
    {
        g_tab_alpha = ImClamp(g_tab_alpha - 6.0f * dt, 0.0f, 1.0f);
        if (g_tab_alpha <= 0.01f) { g_active = g_tab; g_tab_alpha = 0.0f; }
    }
    else
    {
        g_tab_alpha = ImClamp(g_tab_alpha + 6.0f * dt, 0.0f, 1.0f);
    }

    ImGui::SetCursorPos(ImVec2(L + 17.0f * s, 104.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.25f + 0.75f * Smooth(g_tab_alpha));
    ImGui::BeginChild("##content", ImVec2(W - L - 34.0f * s, H - 122.0f * s),
                      false, ImGuiWindowFlags_NoBackground);
    switch ((Page)g_active)
    {
    case Page::Home:   PageHome();   break;
    case Page::Esp:    PageEsp();    break;
    case Page::Battle: PageBattle(); break;
    case Page::Activity: PageActivity(); break;
    case Page::Lua:    PageLua();    break;
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
    DrawToasts(s);
}

}
