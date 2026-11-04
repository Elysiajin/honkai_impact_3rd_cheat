#pragma once

#include "imgui.h"
#include <cstdint>

extern ImFont* ico_0;
extern ImFont* ico_1;
extern ImFont* ubu_0;
extern ImFont* ubu_1;
extern ImFont* ubu_2;
extern ImFont* ubu_child;
extern ImFont* ubu_preview;
extern ImFont* cruin_0;

namespace ui
{

inline constexpr ImVec4 Hex(unsigned int hex, float a = 1.0f)
{
    return ImVec4(static_cast<float>((hex >> 16) & 0xFFu) / 255.0f,
                  static_cast<float>((hex >> 8) & 0xFFu) / 255.0f,
                  static_cast<float>(hex & 0xFFu) / 255.0f,
                  a);
}

inline ImU32 U32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }
inline ImVec4 A(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, a); }

inline ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t)
{
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

struct FontSet
{
    ImFont* display = nullptr;
    ImFont* title   = nullptr;
    ImFont* button  = nullptr;
    ImFont* body    = nullptr;
    ImFont* small   = nullptr;
    ImFont* tiny    = nullptr;
    bool    cjk     = false;
    char    path[260] = {};
    const char* icon_path = "";
};

const FontSet& Fonts();

bool BuildFonts(ImFontAtlas* atlas, float scale, char* err, int err_cap);

void  ApplyTheme();
void  SetUiScale(float scale);
float UiScale();
float S();

float Dt();
void  SetDt(float dt);

void HLine(float x0, float x1, float y, ImU32 col, float th = 1.0f);

}
