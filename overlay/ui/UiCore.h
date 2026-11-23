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
extern ImFont* esp_s;
extern ImFont* esp_m;
extern ImFont* esp_l;

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

// 统一主题 Token：深蓝紫玻璃 + 雾紫主强调 + 冰蓝辅助。
// 颜色集中在此，避免散落在各绘制函数；alpha 按无实时模糊的伪磨砂设定。
namespace theme
{
    inline constexpr unsigned Accent     = 0xA970FFu; // 雾紫，主强调
    inline constexpr unsigned AccentSoft = 0xD4B8FFu; // 弱强调/高光
    inline constexpr unsigned Cyan       = 0x6FD8F0u; // 冰蓝，辅助点缀
    inline constexpr unsigned TextHi     = 0xF2EEFFu;
    inline constexpr unsigned TextMid    = 0xB0ABC4u;
    inline constexpr unsigned TextLow    = 0x858098u;
    inline constexpr unsigned Success    = 0x76D6B0u;
    inline constexpr unsigned Warning    = 0xF0B66Eu;
    inline constexpr unsigned Danger     = 0xF07891u;
    inline constexpr unsigned WinBg      = 0x111326u;
    inline constexpr unsigned PanelBg    = 0x191B34u;
    inline constexpr unsigned CardBg     = 0x242540u;
    inline constexpr unsigned CardHover  = 0x2D2B4Bu;
    inline constexpr unsigned Border     = 0x3B3652u;

    inline constexpr float WinAlpha    = 0.92f;
    inline constexpr float PanelAlpha  = 0.94f;
    inline constexpr float CardAlpha   = 0.90f;
    inline constexpr float HoverAlpha  = 0.96f;
    inline constexpr float BorderAlpha = 0.30f;
    inline constexpr float EdgeAlpha   = 0.60f;
}

inline ImVec4 C(unsigned hex, float a = 1.0f) { return Hex(hex, a); }
inline ImU32  CU(unsigned hex, float a = 1.0f) { return U32(Hex(hex, a)); }

inline float Smooth(float t)
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
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

ImFont* EspFont(float px);

void  ApplyTheme(float scale = -1.0f);
void  SetUiScale(float scale);
float UiScale();
float S();

// 界面缩放变更后重建字体图集：图集只能在 ImGui::NewFrame() 之前修改，
// 而 AppFrame() 运行在 NewFrame()..Render() 之间，所以这里只登记请求，
// 由 ov_gui.cpp 在帧首调用 TakeFontRebuild() 取走并真正重建。
void  RequestFontRebuild(float scale);
bool  TakeFontRebuild(float* out_scale);

float Dt();
void  SetDt(float dt);

void HLine(float x0, float x1, float y, ImU32 col, float th = 1.0f);

}
