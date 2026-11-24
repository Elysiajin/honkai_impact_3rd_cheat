#pragma once
#include "UiCore.h"
#include "saphire_colors.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>

namespace ui
{
namespace glass
{

struct Palette
{
    ImVec4 shell, surface, field, hover, active;
    ImVec4 ink, muted, accent, on_accent, border, hero;
    bool   light = true;
};

enum ThemeId
{
    ThemeClassic   = 0,  // 原项目深紫主题
    ThemePorcelain = 1,  // 瓷白
    ThemeMist      = 2,  // 雾透
    ThemeGraphite  = 3,  // 石墨
    ThemeBay       = 4,  // 海湾
    ThemePine      = 5,  // 松林
    ThemeIris      = 6,  // 鸢尾
    ThemeSand      = 7,  // 暖砂
    ThemeCount     = 8
};

inline const char* const kThemeNames[ThemeCount] = {
    "原主题 · 深紫", "瓷白", "雾透", "石墨", "海湾", "松林", "鸢尾", "暖砂"
};

inline int&   CurrentTheme()   { static int v = ThemePorcelain; return v; }
inline float& OpacityScale()   { static float v = 1.0f; return v; }

inline float Clamp(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }

inline void SetTheme(int id)
{
    CurrentTheme() = (int)Clamp((float)id, 0.0f, (float)(ThemeCount - 1));
}
inline void SetOpacityScale(float v) { OpacityScale() = Clamp(v, 0.20f, 1.0f); }

inline ImVec4 Mix(ImVec4 a, ImVec4 b, float t)
{
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

inline ImVec4 Opacity(ImVec4 c, float a) { c.w = a; return c; }

inline ImU32 Col(ImVec4 c, float alpha = 1.0f)
{
    c.w = Clamp(c.w * alpha, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(c);
}

inline unsigned ToHex(ImVec4 c)
{
    const unsigned r = (unsigned)Clamp(std::lround(c.x * 255.0f), 0.0f, 255.0f);
    const unsigned g = (unsigned)Clamp(std::lround(c.y * 255.0f), 0.0f, 255.0f);
    const unsigned b = (unsigned)Clamp(std::lround(c.z * 255.0f), 0.0f, 255.0f);
    return (r << 16) | (g << 8) | b;
}

inline ImVec4 operator/(const ImVec4& v, float s)
{
    return ImVec4(v.x / s, v.y / s, v.z / s, v.w / s);
}

inline Palette Material(int theme)
{
    Palette p;
    ImVec4 shell, surface, field, hover, active, ink, muted, accent, on_accent, border, hero;
    bool light = true;

    switch (theme)
    {
    case ThemeClassic:
        light = false;
        shell   = ImVec4(0x11 / 255.0f, 0x13 / 255.0f, 0x26 / 255.0f, 0.92f);
        surface = ImVec4(0x19 / 255.0f, 0x1B / 255.0f, 0x34 / 255.0f, 0.94f);
        field   = ImVec4(0x24 / 255.0f, 0x25 / 255.0f, 0x40 / 255.0f, 0.90f);
        hover   = ImVec4(0x2D / 255.0f, 0x2B / 255.0f, 0x4B / 255.0f, 0.96f);
        active  = ImVec4(0x2D / 255.0f, 0x2B / 255.0f, 0x4B / 255.0f, 0.96f);
        ink     = ImVec4(0xF2 / 255.0f, 0xEE / 255.0f, 0xFF / 255.0f, 1.0f);
        muted   = ImVec4(0xB0 / 255.0f, 0xAB / 255.0f, 0xC4 / 255.0f, 1.0f);
        accent  = ImVec4(0xA9 / 255.0f, 0x70 / 255.0f, 0xFF / 255.0f, 1.0f);
        on_accent = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        border  = ImVec4(0x3B / 255.0f, 0x36 / 255.0f, 0x52 / 255.0f, 1.0f);
        hero    = ImVec4(0x11 / 255.0f, 0x13 / 255.0f, 0x26 / 255.0f, 1.0f);
        break;
    case ThemeMist:
        light = true;
        shell = ImVec4(249, 250, 252, 255) / 255.0f; shell.w = 0.72f;
        surface = shell; surface.w = 0.70f;
        field = ImVec4(246, 249, 252, 255) / 255.0f; field.w = 0.72f;
        hover = ImVec4(1.0f, 1.0f, 1.0f, 0.82f);
        active = ImVec4(231, 238, 245, 255) / 255.0f; active.w = 0.84f;
        ink = ImVec4(29, 37, 45, 255) / 255.0f;
        muted = ImVec4(47, 62, 75, 255) / 255.0f;
        accent = ImVec4(60, 80, 99, 255) / 255.0f;
        on_accent = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        border = ImVec4(1.0f, 1.0f, 1.0f, 0.41f);
        hero = ImVec4(224, 235, 245, 255) / 255.0f;
        break;
    case ThemeGraphite:
        light = false;
        shell = ImVec4(23, 27, 34, 255) / 255.0f; shell.w = 0.86f;
        surface = ImVec4(42, 49, 61, 255) / 255.0f; surface.w = 0.70f;
        field = ImVec4(11, 15, 22, 255) / 255.0f; field.w = 0.72f;
        hover = ImVec4(114, 128, 148, 255) / 255.0f; hover.w = 0.48f;
        active = ImVec4(140, 156, 178, 255) / 255.0f; active.w = 0.52f;
        ink = ImVec4(240, 243, 248, 255) / 255.0f;
        muted = ImVec4(174, 185, 201, 255) / 255.0f;
        accent = ImVec4(216, 225, 238, 255) / 255.0f;
        on_accent = ImVec4(0.10f, 0.13f, 0.18f, 1.0f);
        border = ImVec4(226, 234, 247, 255) / 255.0f; border.w = 0.22f;
        hero = ImVec4(13, 18, 26, 255) / 255.0f;
        break;
    case ThemeBay:
        light = false;
        shell = ImVec4(20, 45, 66, 255) / 255.0f; shell.w = 0.82f;
        surface = ImVec4(43, 83, 108, 255) / 255.0f; surface.w = 0.64f;
        field = ImVec4(8, 29, 46, 255) / 255.0f; field.w = 0.70f;
        hover = ImVec4(107, 182, 220, 255) / 255.0f; hover.w = 0.46f;
        active = ImVec4(120, 207, 255, 255) / 255.0f; active.w = 0.52f;
        ink = ImVec4(236, 247, 255, 255) / 255.0f;
        muted = ImVec4(170, 201, 222, 255) / 255.0f;
        accent = ImVec4(120, 207, 255, 255) / 255.0f;
        on_accent = ImVec4(0.04f, 0.17f, 0.26f, 1.0f);
        border = ImVec4(169, 220, 250, 255) / 255.0f; border.w = 0.26f;
        hero = ImVec4(10, 31, 49, 255) / 255.0f;
        break;
    case ThemePine:
        light = false;
        shell = ImVec4(24, 46, 40, 255) / 255.0f; shell.w = 0.84f;
        surface = ImVec4(47, 82, 68, 255) / 255.0f; surface.w = 0.64f;
        field = ImVec4(10, 30, 24, 255) / 255.0f; field.w = 0.70f;
        hover = ImVec4(125, 191, 163, 255) / 255.0f; hover.w = 0.46f;
        active = ImVec4(147, 222, 193, 255) / 255.0f; active.w = 0.52f;
        ink = ImVec4(238, 249, 242, 255) / 255.0f;
        muted = ImVec4(176, 207, 190, 255) / 255.0f;
        accent = ImVec4(147, 222, 193, 255) / 255.0f;
        on_accent = ImVec4(0.05f, 0.18f, 0.14f, 1.0f);
        border = ImVec4(185, 232, 207, 255) / 255.0f; border.w = 0.25f;
        hero = ImVec4(13, 33, 27, 255) / 255.0f;
        break;
    case ThemeIris:
        light = false;
        shell = ImVec4(40, 33, 57, 255) / 255.0f; shell.w = 0.84f;
        surface = ImVec4(75, 62, 101, 255) / 255.0f; surface.w = 0.64f;
        field = ImVec4(25, 18, 40, 255) / 255.0f; field.w = 0.70f;
        hover = ImVec4(167, 144, 212, 255) / 255.0f; hover.w = 0.46f;
        active = ImVec4(197, 176, 255, 255) / 255.0f; active.w = 0.52f;
        ink = ImVec4(247, 242, 255, 255) / 255.0f;
        muted = ImVec4(198, 186, 218, 255) / 255.0f;
        accent = ImVec4(197, 176, 255, 255) / 255.0f;
        on_accent = ImVec4(0.17f, 0.10f, 0.28f, 1.0f);
        border = ImVec4(222, 206, 252, 255) / 255.0f; border.w = 0.25f;
        hero = ImVec4(27, 20, 42, 255) / 255.0f;
        break;
    case ThemeSand:
        light = true;
        shell = ImVec4(240, 228, 211, 255) / 255.0f; shell.w = 0.86f;
        surface = ImVec4(255, 248, 237, 255) / 255.0f; surface.w = 0.78f;
        field = ImVec4(215, 197, 173, 255) / 255.0f; field.w = 0.78f;
        hover = ImVec4(201, 178, 149, 255) / 255.0f; hover.w = 0.62f;
        active = ImVec4(182, 153, 120, 255) / 255.0f; active.w = 0.66f;
        ink = ImVec4(61, 48, 36, 255) / 255.0f;
        muted = ImVec4(113, 89, 65, 255) / 255.0f;
        accent = ImVec4(126, 89, 55, 255) / 255.0f;
        on_accent = ImVec4(1.0f, 0.98f, 0.95f, 1.0f);
        border = ImVec4(255, 250, 240, 255) / 255.0f; border.w = 0.60f;
        hero = ImVec4(102, 74, 49, 255) / 255.0f;
        break;
    case ThemePorcelain:
    default:
        light = true;
        shell = ImVec4(239, 243, 247, 255) / 255.0f; shell.w = 0.88f;
        surface = ImVec4(235, 243, 251, 255) / 255.0f; surface.w = 0.78f;
        field = ImVec4(229, 232, 237, 255) / 255.0f; field.w = 0.78f;
        hover = ImVec4(245, 249, 253, 255) / 255.0f; hover.w = 0.90f;
        active = ImVec4(236, 244, 251, 255) / 255.0f; active.w = 0.92f;
        ink = ImVec4(39, 42, 46, 255) / 255.0f;
        muted = ImVec4(101, 111, 122, 255) / 255.0f;
        accent = ImVec4(60, 80, 99, 255) / 255.0f;
        on_accent = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        border = ImVec4(1.0f, 1.0f, 1.0f, 0.61f);
        hero = ImVec4(0.11f, 0.23f, 0.31f, 1.0f);
        break;
    }

    const float k = OpacityScale();
    p.shell = Opacity(shell, Clamp(shell.w * k, 0.0f, 0.98f));
    p.surface = Opacity(surface, Clamp(surface.w * k, 0.0f, 0.98f));
    p.field = Opacity(field, Clamp(field.w * k, 0.0f, 0.98f));
    p.hover = Opacity(hover, Clamp(hover.w * k, 0.0f, 0.98f));
    p.active = Opacity(active, Clamp(active.w * k, 0.0f, 0.98f));
    p.ink = ink; p.muted = muted; p.accent = accent;
    p.on_accent = on_accent; p.border = border; p.hero = hero;
    p.light = light;
    return p;
}

inline Palette Material() { return Material(CurrentTheme()); }

inline void ApplyPaletteColors(int theme)
{
    SetTheme(theme);
    const Palette p = Material(theme);

    theme::Accent     = ToHex(p.accent);
    theme::AccentSoft = ToHex(Mix(p.accent, p.on_accent, 0.35f));
    theme::Cyan       = p.light ? 0x4E93B8u : ToHex(Mix(p.accent, ImVec4(0.44f, 0.85f, 0.94f, 1.0f), 0.55f));
    theme::TextHi     = ToHex(p.ink);
    theme::TextMid    = ToHex(p.muted);
    theme::TextLow    = ToHex(Mix(p.muted, p.surface, p.light ? 0.34f : 0.24f));
    theme::Success    = p.light ? 0x3F8168u : 0x76D6B0u;
    theme::Warning    = p.light ? 0x9A7139u : 0xF0B66Eu;
    theme::Danger     = p.light ? 0xA95660u : 0xF07891u;
    theme::WinBg      = ToHex(p.shell);
    theme::PanelBg    = ToHex(p.surface);
    theme::CardBg     = ToHex(p.field);
    theme::CardHover  = ToHex(p.hover);
    theme::Border     = ToHex(p.border);
    theme::WinAlpha    = p.shell.w;
    theme::PanelAlpha  = p.surface.w;
    theme::CardAlpha   = p.field.w;
    theme::HoverAlpha  = p.hover.w;
    theme::BorderAlpha = p.border.w;
    theme::EdgeAlpha   = p.light ? 0.85f : 0.60f;

    if (theme == ThemeClassic)
    {
        // 原项目配色逐项还原，不套用浅色调色板推导。
        c::accent_color = ImColor(169, 112, 255, 255);
        c::tab_text_active = ImColor(242, 238, 255, 255);
        c::tab_text_hov = ImColor(212, 184, 255, 255);
        c::tab_text = ImColor(176, 171, 196, 255);
        c::tab_active = ImColor(36, 37, 64, 165);
        c::tab_active_fill = ImColor(169, 112, 255, 255);
        c::glow_active = ImColor(169, 112, 255, 210);
        c::text_active = ImColor(242, 238, 255, 255);
        c::text_hov = ImColor(212, 184, 255, 255);
        c::text = ImColor(176, 171, 196, 255);
        c::child_background = ImColor(36, 37, 64, 232);
        c::border_child = ImColor(59, 54, 82, 255);
        c::child_gradient = ImColor(120, 100, 175, 255);
        c::border_child_default = ImColor(45, 43, 75, 255);
        c::child_name = ImColor(176, 171, 196, 255);
        c::checkbox_active = ImColor(45, 43, 75, 255);
        c::checkbox_inactive = ImColor(31, 32, 58, 255);
        c::circle_inactive = ImColor(93, 88, 120, 255);
        c::circle_active = ImColor(169, 112, 255, 255);
        c::slider_background = ImColor(31, 32, 58, 255);
        c::circle_push_gradient = ImColor(59, 54, 82, 255);
        c::circle_pop_gradient = ImColor(72, 66, 100, 255);
        c::input_push_gradient = ImColor(36, 37, 64, 255);
        c::input_pop_gradient = ImColor(25, 27, 46, 255);
        c::scroll_background = ImColor(20, 22, 38, 255);
        c::scroll_keep = ImColor(59, 54, 82, 255);
        c::combo_background = ImColor(36, 37, 64, 255);
        c::button = ImColor(36, 37, 64, 255);
        c::button_push_gradient_active = ImColor(45, 43, 75, 255);
        c::button_pop_gradient_active = ImColor(36, 37, 64, 255);
        c::button_push_gradient_hovered = ImColor(45, 43, 75, 255);
        c::button_pop_gradient_hovered = ImColor(30, 31, 54, 255);
        c::button_push_gradient = ImColor(36, 37, 64, 255);
        c::button_pop_gradient = ImColor(25, 27, 46, 255);
        c::keybind_background = ImColor(36, 37, 64, 255);
        c::keybind_border = ImColor(45, 43, 75, 255);
        c::tab_push_gradient = ImColor(169, 112, 255, 255);
        c::tab_pop_gradient = ImColor(96, 62, 160, 255);
    }
    else
    {
        const ImVec4 ink = p.ink, muted = p.muted, accent = p.accent;
        c::accent_color = ImColor(accent);
        c::tab_text_active = ImColor(ink);
        c::tab_text_hov = ImColor(Mix(ink, muted, 0.35f));
        c::tab_text = ImColor(muted);
        c::tab_active = ImColor(Opacity(p.active, std::max(p.active.w, 0.72f)));
        c::tab_active_fill = ImColor(Opacity(accent, 0.85f));
        c::glow_active = ImColor(Opacity(accent, 0.60f));
        c::text_active = ImColor(ink);
        c::text_hov = ImColor(Mix(ink, muted, 0.35f));
        c::text = ImColor(muted);
        c::child_background = ImColor(Opacity(p.surface, std::max(p.surface.w, 0.82f)));
        c::border_child = ImColor(p.border);
        c::child_gradient = ImColor(Mix(p.hover, accent, 0.18f));
        c::border_child_default = ImColor(Opacity(p.border, p.border.w * 0.65f));
        c::child_name = ImColor(muted);
        c::checkbox_active = ImColor(Opacity(p.active, 0.95f));
        c::checkbox_inactive = ImColor(Opacity(p.field, 0.88f));
        c::circle_inactive = ImColor(Opacity(muted, 0.82f));
        c::circle_active = ImColor(accent);
        c::slider_background = ImColor(Opacity(p.field, 0.92f));
        c::circle_push_gradient = ImColor(Opacity(p.active, 0.96f));
        c::circle_pop_gradient = ImColor(Opacity(p.hover, 0.92f));
        c::input_push_gradient = ImColor(Opacity(p.field, 0.92f));
        c::input_pop_gradient = ImColor(Opacity(p.field, 0.80f));
        c::scroll_background = ImColor(Opacity(p.field, 0.50f));
        c::scroll_keep = ImColor(Opacity(accent, 0.82f));
        c::combo_background = ImColor(Opacity(p.surface, 0.95f));
        c::button = ImColor(Opacity(p.field, 0.90f));
        c::button_push_gradient_active = ImColor(Opacity(p.active, 0.95f));
        c::button_pop_gradient_active = ImColor(Opacity(p.hover, 0.92f));
        c::button_push_gradient_hovered = ImColor(Opacity(p.hover, 0.94f));
        c::button_pop_gradient_hovered = ImColor(Opacity(p.surface, 0.90f));
        c::button_push_gradient = ImColor(Opacity(p.field, 0.92f));
        c::button_pop_gradient = ImColor(Opacity(p.surface, 0.85f));
        c::keybind_background = ImColor(Opacity(p.field, 0.90f));
        c::keybind_border = ImColor(p.border);
        c::tab_push_gradient = ImColor(Opacity(p.active, 0.96f));
        c::tab_pop_gradient = ImColor(Opacity(p.field, 0.90f));
    }

    ImGuiStyle& st = ImGui::GetStyle();
    ImVec4* col = st.Colors;
    const ImVec4 accent = C(theme::Accent);
    const ImVec4 accentSoft = C(theme::AccentSoft);
    col[ImGuiCol_Text] = C(theme::TextHi);
    col[ImGuiCol_TextDisabled] = C(theme::TextLow);
    col[ImGuiCol_WindowBg] = A(accent, 0.0f);
    col[ImGuiCol_ChildBg] = C(theme::CardBg, theme::CardAlpha * 0.75f);
    col[ImGuiCol_PopupBg] = C(theme::WinBg, std::max(p.shell.w, 0.90f));
    col[ImGuiCol_Border] = C(theme::Border, theme::BorderAlpha);
    col[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_FrameBg] = C(theme::PanelBg, std::max(p.surface.w * 0.80f, 0.32f));
    col[ImGuiCol_FrameBgHovered] = C(theme::CardHover, std::max(p.hover.w * 0.84f, 0.42f));
    col[ImGuiCol_FrameBgActive] = C(theme::CardBg, std::max(p.field.w * 0.92f, 0.52f));
    col[ImGuiCol_TitleBg] = C(theme::WinBg, theme::WinAlpha);
    col[ImGuiCol_TitleBgActive] = C(theme::WinBg, theme::WinAlpha);
    col[ImGuiCol_TitleBgCollapsed] = C(theme::WinBg, theme::WinAlpha);
    col[ImGuiCol_MenuBarBg] = C(theme::PanelBg, theme::PanelAlpha);
    col[ImGuiCol_ScrollbarBg] = C(theme::PanelBg, 0.20f);
    col[ImGuiCol_ScrollbarGrab] = C(theme::TextLow, 0.48f);
    col[ImGuiCol_ScrollbarGrabHovered] = C(theme::Accent, 0.72f);
    col[ImGuiCol_ScrollbarGrabActive] = accent;
    col[ImGuiCol_CheckMark] = accent;
    col[ImGuiCol_SliderGrab] = accent;
    col[ImGuiCol_SliderGrabActive] = accentSoft;
    col[ImGuiCol_Button] = C(theme::CardBg, std::max(p.field.w * 0.80f, 0.35f));
    col[ImGuiCol_ButtonHovered] = C(theme::CardHover, std::max(p.hover.w * 0.85f, 0.45f));
    col[ImGuiCol_ButtonActive] = C(theme::AccentSoft, 0.75f);
    col[ImGuiCol_Header] = C(theme::CardBg, 0.38f);
    col[ImGuiCol_HeaderHovered] = C(theme::CardHover, 0.68f);
    col[ImGuiCol_HeaderActive] = C(theme::AccentSoft, 0.60f);
    col[ImGuiCol_Separator] = C(theme::TextLow, 0.28f);
    col[ImGuiCol_SeparatorHovered] = accentSoft;
    col[ImGuiCol_SeparatorActive] = accent;
    col[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_ResizeGripHovered] = C(theme::Accent, 0.45f);
    col[ImGuiCol_ResizeGripActive] = accent;
    col[ImGuiCol_Tab] = C(theme::CardBg, 0.28f);
    col[ImGuiCol_TabHovered] = C(theme::CardHover, 0.70f);
    col[ImGuiCol_TabActive] = C(theme::AccentSoft, 0.72f);
    col[ImGuiCol_TabUnfocused] = C(theme::Border, 0.35f);
    col[ImGuiCol_TabUnfocusedActive] = C(theme::AccentSoft, 0.45f);
    col[ImGuiCol_PlotLines] = C(theme::Cyan);
    col[ImGuiCol_PlotLinesHovered] = C(theme::Cyan, 0.85f);
    col[ImGuiCol_PlotHistogram] = accent;
    col[ImGuiCol_PlotHistogramHovered] = accentSoft;
    col[ImGuiCol_TableHeaderBg] = C(theme::PanelBg, 0.70f);
    col[ImGuiCol_TableBorderStrong] = C(theme::TextLow, 0.35f);
    col[ImGuiCol_TableBorderLight] = C(theme::TextLow, 0.20f);
    col[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    col[ImGuiCol_TableRowBgAlt] = C(theme::Accent, 0.06f);
    col[ImGuiCol_TextSelectedBg] = C(theme::Accent, 0.22f);
    col[ImGuiCol_DragDropTarget] = C(theme::Cyan);
    col[ImGuiCol_NavHighlight] = accentSoft;
    col[ImGuiCol_NavWindowingHighlight] = C(theme::Accent, 0.60f);
    col[ImGuiCol_NavWindowingDimBg] = ImVec4(0.04f, 0.10f, 0.14f, 0.18f);
    col[ImGuiCol_ModalWindowDimBg] = ImVec4(0.04f, 0.10f, 0.14f, 0.30f);
}

inline bool SurfaceValid(ImDrawList* d, ImVec2 p, ImVec2 size, float radius, float alpha)
{
    return d && size.x > 2.0f && size.y > 2.0f &&
           std::isfinite(p.x) && std::isfinite(p.y) &&
           std::isfinite(size.x) && std::isfinite(size.y) &&
           std::isfinite(radius) && std::isfinite(alpha) && alpha > 0.0f;
}

// 只着色圆角图元的顶点：抗锯齿覆盖率保留，渐变不会溢出圆角边界。
inline void Body(ImDrawList* d, ImVec2 p, ImVec2 size, float radius,
                 ImVec4 tint, float alpha)
{
    const int first = d->VtxBuffer.Size;
    d->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32_WHITE, radius);
    const ImVec4 white(1.0f, 1.0f, 1.0f, 1.0f);
    const float opacity = Clamp(tint.w, 0.0f, 1.0f);
    for (int i = first; i < d->VtxBuffer.Size; ++i)
    {
        ImDrawVert& v = d->VtxBuffer[i];
        const float x = Clamp((v.pos.x - p.x) / std::max(size.x, 1.0f), 0.0f, 1.0f);
        const float y = Clamp((v.pos.y - p.y) / std::max(size.y, 1.0f), 0.0f, 1.0f);
        // 左上偏亮的斜向反光，对应源项目 LiquidBody 的 reflection。
        const float diagonal = Clamp(1.0f - x * 0.62f - y * 1.22f, 0.0f, 1.0f);
        const float reflection = diagonal * diagonal;
        ImVec4 shade = Mix(tint, white, 0.30f * reflection);
        shade.w = opacity * (0.84f + 0.13f * reflection);
        const float coverage = ((v.col >> IM_COL32_A_SHIFT) & 255) / 255.0f;
        v.col = Col(shade, alpha * coverage);
    }
}

// 双层描边：外侧白高光 + 内侧冷色细线，构成 Vision Pro 式玻璃边缘。
inline void Rim(ImDrawList* d, ImVec2 p, ImVec2 size, float radius, float width,
                ImVec4 upper, ImVec4 lower, float alpha)
{
    if (size.x <= 0.0f || size.y <= 0.0f) return;
    const int first = d->VtxBuffer.Size;
    d->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32_WHITE, radius, 0, width);
    for (int i = first; i < d->VtxBuffer.Size; ++i)
    {
        ImDrawVert& v = d->VtxBuffer[i];
        const float x = Clamp((v.pos.x - p.x) / std::max(size.x, 1.0f), 0.0f, 1.0f);
        const float y = Clamp((v.pos.y - p.y) / std::max(size.y, 1.0f), 0.0f, 1.0f);
        const float diagonal = Clamp(y * 0.84f + x * 0.16f, 0.0f, 1.0f);
        const float coverage = ((v.col >> IM_COL32_A_SHIFT) & 255) / 255.0f;
        v.col = Col(Mix(upper, lower, diagonal), alpha * coverage);
    }
}

inline void Edges(ImDrawList* d, ImVec2 p, ImVec2 size, float radius, float scale,
                  const Palette& pal, float alpha, float emphasis = 0.0f)
{
    const ImVec4 white(1.0f, 1.0f, 1.0f, 1.0f);
    const ImVec4 shade = Mix(pal.hero, ImVec4(0.035f, 0.045f, 0.06f, 1.0f), 0.54f);
    const float s = std::min(scale, std::min(size.x, size.y) * 0.125f);
    const float e = Clamp(emphasis, 0.0f, 1.0f);
    Rim(d, p, size, radius, 0.85f * s,
        Opacity(white, 0.57f + 0.12f * e), Opacity(shade, 0.22f), alpha);
    const float inset = 1.4f * s;
    if (size.x > 2.0f * inset && size.y > 2.0f * inset)
    {
        Rim(d, ImVec2(p.x + inset, p.y + inset), ImVec2(size.x - 2.0f * inset, size.y - 2.0f * inset),
            std::max(0.0f, radius - inset), 0.65f * s,
            Opacity(white, 0.14f), Opacity(Mix(white, pal.accent, 0.12f), 0.38f), alpha);
    }
}

inline void Panel(ImDrawList* d, ImVec2 p, ImVec2 size, float radius, float scale,
                  float alpha, float emphasis = 0.0f, const Palette* palette = nullptr)
{
    const Palette local = Material();
    const Palette& pal = palette ? *palette : local;
    const float s = Clamp(scale, 0.1f, 4.0f);
    const float a = Clamp(alpha, 0.0f, 1.0f);
    if (!SurfaceValid(d, p, size, radius, a)) return;
    const float r = Clamp(radius, 0.0f, std::min(size.x, size.y) * 0.5f);
    Body(d, p, size, r, pal.surface, a);
    Edges(d, p, size, r, s, pal, a, emphasis);
}

inline void Shell(ImDrawList* d, ImVec2 p, ImVec2 size, float radius, float scale, float alpha)
{
    const Palette pal = Material();
    const float s = Clamp(scale, 0.1f, 4.0f);
    const float a = Clamp(alpha, 0.0f, 1.0f);
    if (!SurfaceValid(d, p, size, radius, a)) return;
    const float r = Clamp(radius, 0.0f, std::min(size.x, size.y) * 0.5f);

    // 中性投影，不产生饱和拖影。
    for (int i = 3; i >= 1; --i)
    {
        const float spread = std::max(0.0f, 5.0f * s - 1.0f) * i / 3.0f;
        d->AddRect(ImVec2(p.x - spread, p.y - spread),
                   ImVec2(p.x + size.x + spread, p.y + size.y + spread),
                   Col(Opacity(pal.hero, 0.025f * (4 - i) / 3.0f), a),
                   r + spread, 0, 0.9f * s);
    }
    ImVec4 body = pal.shell;
    body.w = std::min(body.w, 0.92f);
    Body(d, p, size, r, body, a);
    Edges(d, p, size, r, s, pal, a, 0.22f);
}

// 全屏蓝绿渐变：只影响菜单打开时，画在背景 draw list 上，位于 ESP 之下。
inline void Backdrop(ImDrawList* d, ImVec2 screen, float alpha, float time)
{
    if (!d || screen.x < 8.0f || screen.y < 8.0f) return;
    const float a = Clamp(alpha, 0.0f, 1.0f);
    if (a <= 0.0f) return;

    const ImVec4 top(0.17f, 0.39f, 0.47f, 1.0f);
    const ImVec4 bottom(0.035f, 0.13f, 0.24f, 1.0f);
    const int first = d->VtxBuffer.Size;
    d->AddRectFilled(ImVec2(0.0f, 0.0f), screen, IM_COL32_WHITE);
    for (int i = first; i < d->VtxBuffer.Size; ++i)
    {
        ImDrawVert& v = d->VtxBuffer[i];
        const float y = Clamp(v.pos.y / std::max(screen.y, 1.0f), 0.0f, 1.0f);
        const float x = Clamp(v.pos.x / std::max(screen.x, 1.0f), 0.0f, 1.0f);
        const float center = 1.0f - Clamp(std::sqrt((x - 0.52f) * (x - 0.52f) * 1.6f +
                                                    (y - 0.42f) * (y - 0.42f)) * 1.1f, 0.0f, 1.0f);
        ImVec4 shade = Mix(top, bottom, Clamp(y * 0.80f + 0.20f * x, 0.0f, 1.0f));
        shade = Mix(shade, ImVec4(0.42f, 0.63f, 0.67f, 1.0f), 0.18f * center);
        const float coverage = ((v.col >> IM_COL32_A_SHIFT) & 255) / 255.0f;
        v.col = Col(shade, a * coverage);
    }

    // 两团柔和冷光，避免整屏渐变过于平板。
    const float drift = std::sin(time * 0.12f) * screen.x * 0.012f;
    d->AddCircleFilled(ImVec2(screen.x * 0.34f + drift, screen.y * 0.30f),
                       screen.y * 0.52f, Col(ImVec4(0.52f, 0.75f, 0.78f, 0.10f), a), 32);
    d->AddCircleFilled(ImVec2(screen.x * 0.78f - drift, screen.y * 0.76f),
                       screen.y * 0.44f, Col(ImVec4(0.20f, 0.49f, 0.65f, 0.09f), a), 32);
}

inline float Hash(float n)
{
    const float v = std::sin(n * 12.9898f) * 43758.5453f;
    return v - std::floor(v);
}

// 源项目「星野」的简化版：固定分布 + 极慢闪烁，圆角内不会溢出。
inline void Speckles(ImDrawList* d, ImVec2 p, ImVec2 size, float scale,
                     float alpha, float time, int count = 96)
{
    if (!d || alpha <= 0.0f || size.x <= 8.0f || size.y <= 8.0f) return;
    const Palette pal = Material();
    d->PushClipRect(p, ImVec2(p.x + size.x, p.y + size.y), true);
    for (int i = 0; i < count; ++i)
    {
        const float seed = (float)i + 1.0f;
        const float x = Hash(seed * 2.13f);
        const float y = Hash(seed * 3.71f);
        const float depth = 0.35f + 0.65f * Hash(seed * 5.29f);
        const ImVec2 point(p.x + x * size.x, p.y + y * size.y);
        const float radius = (0.55f + 0.85f * depth) * scale;
        const float twinkle = 0.42f + 0.58f * (0.5f + 0.5f * std::sin(time * (0.5f + depth) + x * 17.0f));
        d->AddCircleFilled(point, radius,
                           Col(Opacity(pal.accent, alpha * (0.20f + 0.38f * twinkle)), 1.0f), 8);
        if (i % 19 == 0)
        {
            const float arm = radius * 2.8f;
            const ImU32 spark = Col(Opacity(pal.accent, alpha * 0.22f * twinkle), 1.0f);
            d->AddLine(ImVec2(point.x - arm, point.y), ImVec2(point.x + arm, point.y), spark, 0.7f * scale);
            d->AddLine(ImVec2(point.x, point.y - arm), ImVec2(point.x, point.y + arm), spark, 0.7f * scale);
        }
    }
    d->PopClipRect();
}

} // namespace glass
} // namespace ui
