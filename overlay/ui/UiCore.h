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

// 统一主题 Token：默认值对应参考项目「瓷白」，切换主题时由 Glass.h 原地改写。
// 大块底板由 Glass.h 的材质绘制，这里的 alpha 只兜底 ImGui 原生控件。
namespace theme
{
    inline unsigned Accent     = 0x3C5063u;
    inline unsigned AccentSoft = 0x7593A9u;
    inline unsigned Cyan       = 0x4E93B8u;
    inline unsigned TextHi     = 0x1D252Du;
    inline unsigned TextMid    = 0x3C4E5Fu;
    inline unsigned TextLow    = 0x6F8494u;
    inline unsigned Success    = 0x3F8168u;
    inline unsigned Warning    = 0x9A7139u;
    inline unsigned Danger     = 0xA95660u;
    inline unsigned WinBg      = 0xE9F0F7u;
    inline unsigned PanelBg    = 0xF1F6FBu;
    inline unsigned CardBg     = 0xFFFFFFu;
    inline unsigned CardHover  = 0xF7FAFDu;
    inline unsigned Border     = 0xD6E0EAu;

    inline float WinAlpha    = 0.30f;
    inline float PanelAlpha  = 0.34f;
    inline float CardAlpha   = 0.26f;
    inline float HoverAlpha  = 0.38f;
    inline float BorderAlpha = 0.55f;
    inline float EdgeAlpha   = 0.85f;
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
