#include "UiCore.h"
#include "imgui_internal.h"
#include "_icon_data.h"
#include "_ranges.inc"

#include <cstdio>
#include <cstring>

ImFont* ico_0       = nullptr;
ImFont* ico_1       = nullptr;
ImFont* ubu_0       = nullptr;
ImFont* ubu_1       = nullptr;
ImFont* ubu_2       = nullptr;
ImFont* ubu_child   = nullptr;
ImFont* ubu_preview = nullptr;
ImFont* cruin_0     = nullptr;
ImFont* esp_s       = nullptr;
ImFont* esp_m       = nullptr;
ImFont* esp_l       = nullptr;

namespace ui
{

static const ImWchar kExtraRanges[] = {
    0x0020, 0x00FF,   // 基本拉丁
    0x2000, 0x206F,   // 通用标点
    0x2190, 0x21FF,   // 箭头
    0x2200, 0x22FF,   // 数学运算符
    0x2500, 0x25FF,   // 几何图形
    0x2600, 0x27BF,   // 杂项符号
    0x3000, 0x303F,   // 中日韩标点
    0xFF00, 0xFFEF,   // 全角字符
    0x0000
};

// 常用汉字走 ImGui 内置的 2500 字表，再并入界面实际用字与符号段
static const ImWchar* RangeTable(ImFontAtlas* atlas)
{
    static ImVector<ImWchar> out;
    if (out.empty())
    {
        ImFontGlyphRangesBuilder b;
        if (atlas) b.AddRanges(atlas->GetGlyphRangesChineseSimplifiedCommon());
        b.AddRanges(kExtraRanges);
        b.AddText(kUiGlyphs);
        b.BuildRanges(&out);
    }
    return out.Data;
}

static const ImWchar kIconRanges[] = {
    0x0020, 0x0020,
    0x0041, 0x004A,
    0x0061, 0x006A,
    0x00A0, 0x00A0,
    0x0000
};

static FontSet g_fonts;
static float   g_scale = 1.0f;
static float   g_dt    = 1.0f / 60.0f;

// 字体图集重建请求（图集只能在 ImGui::NewFrame() 之前修改）
static bool    g_rebuild_pending = false;
static float   g_rebuild_scale   = 1.0f;
static float   g_rebuild_time    = -1000.0f;

const FontSet& Fonts() { return g_fonts; }

float UiScale() { return g_scale; }
float S()       { return g_scale; }

float Dt() { return g_dt; }
void  SetDt(float dt)
{
    g_dt = (dt > 0.0f && dt < 0.25f) ? dt : (1.0f / 60.0f);
    if (g_rebuild_pending) g_rebuild_time += g_dt;
}

void RequestFontRebuild(float scale)
{
    if (scale < 0.75f) scale = 0.75f;
    if (scale > 1.60f) scale = 1.60f;
    if (!g_rebuild_pending && scale == g_scale) return;   // 已在目标缩放上
    g_rebuild_pending = true;
    g_rebuild_scale   = scale;
    g_rebuild_time    = 0.0f;                             // 拖动滑块期间持续清零 → 去抖
}

bool TakeFontRebuild(float* out_scale)
{
    if (!g_rebuild_pending) return false;
    if (g_rebuild_time < 0.16f) return false;             // 等拖动停止
    g_rebuild_pending = false;
    g_rebuild_time    = 0.0f;
    if (out_scale) *out_scale = g_rebuild_scale;
    return true;
}

// 优先用项目自带字体，其余为运行环境兜底
static const char* kFontCandidates[] = {
    "D:\\My Projects\\Honkai Impact\\zh-cn.ttf",
    "zh-cn.ttf",
    "C:\\Windows\\Fonts\\hpsimplifiedhans-regular.ttf",
};
static const char* g_cjk_path = nullptr;

static bool FileExists(const char* p)
{
    if (!p || !p[0]) return false;
    FILE* f = nullptr;
    if (fopen_s(&f, p, "rb") != 0 || !f) return false;
    fclose(f);
    return true;
}

static const char* FindCjkFont()
{
    for (const char* p : kFontCandidates)
        if (FileExists(p)) return p;
    return nullptr;
}

static ImFont* AddCjk(ImFontAtlas* atlas, float px)
{
    if (!g_cjk_path) return nullptr;
    ImFontConfig cfg;
    cfg.SizePixels  = px;
    cfg.OversampleH = (px >= 22.0f) ? 1 : 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH  = true;
    return atlas->AddFontFromFileTTF(g_cjk_path, px, &cfg, RangeTable(atlas));
}

// 按字号就近取 ESP 专用字体
ImFont* EspFont(float px)
{
    ImFont* best = esp_m ? esp_m : (ubu_1 ? ubu_1 : ImGui::GetFont());
    if (!best) return nullptr;
    if (!esp_s || !esp_m || !esp_l) return best;

    float d0 = px - esp_s->FontSize; if (d0 < 0) d0 = -d0;
    float d1 = px - esp_m->FontSize; if (d1 < 0) d1 = -d1;
    float d2 = px - esp_l->FontSize; if (d2 < 0) d2 = -d2;

    ImFont* pick = esp_m;
    float d = d1;
    if (d0 < d) { d = d0; pick = esp_s; }
    if (d2 < d) { pick = esp_l; }
    return pick;
}

static ImFont* AddIcon(ImFontAtlas* atlas, float px)
{
    static unsigned char s_icon[4096];
    static bool s_copied = false;
    if (!s_copied)
    {
        unsigned int n = (kIconFontSize < sizeof s_icon) ? kIconFontSize : (unsigned int)sizeof s_icon;
        memcpy(s_icon, kIconFont, n);
        s_copied = true;
    }

    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    cfg.OversampleH          = 1;
    cfg.OversampleV          = 1;
    cfg.PixelSnapH           = true;
    return atlas->AddFontFromMemoryTTF(s_icon, (int)kIconFontSize, px, &cfg, kIconRanges);
}

bool BuildFonts(ImFontAtlas* atlas, float scale, char* err, int err_cap)
{
    if (!atlas)
    {
        if (err && err_cap) snprintf(err, (size_t)err_cap, "atlas null");
        return false;
    }
    if (scale < 0.5f) scale = 0.5f;
    if (scale > 2.5f) scale = 2.5f;

    // 缩放变更时需要重建整个图集：先清掉旧的字体数据与纹理数据。
    // 该函数只允许在 ImGui::NewFrame() 之前调用（图集 Locked 断言）。
    atlas->Clear();
    atlas->TexDesiredWidth = 4096;
    atlas->TexGlyphPadding = 1;

    g_fonts = FontSet();
    // Context recreation frees the previous atlas and every font in it.
    ico_0 = ico_1 = nullptr;
    ubu_0 = ubu_1 = ubu_2 = ubu_child = ubu_preview = cruin_0 = nullptr;
    esp_s = esp_m = esp_l = nullptr;

    g_cjk_path = FindCjkFont();
    if (!g_cjk_path)
    {
        if (err && err_cap) snprintf(err, (size_t)err_cap, "missing zh-cn.ttf");
        atlas->AddFontDefault();
        if (!atlas->Fonts.empty())
        {
            g_fonts.body = g_fonts.small = g_fonts.tiny = atlas->Fonts[0];
            g_fonts.display = g_fonts.title = g_fonts.button = atlas->Fonts[0];
            ubu_0 = ubu_1 = ubu_2 = ubu_child = ubu_preview = cruin_0 = atlas->Fonts[0];
            esp_s = esp_m = esp_l = atlas->Fonts[0];
        }
        return false;
    }

    ubu_child   = AddCjk(atlas, 15.0f * scale);
    ubu_preview = AddCjk(atlas, 12.0f * scale);
    ubu_0       = AddCjk(atlas, 18.0f * scale);
    ubu_2       = AddCjk(atlas, 21.0f * scale);
    ubu_1       = AddCjk(atlas, 25.0f * scale);
    cruin_0     = ubu_1;
    g_fonts.display = AddCjk(atlas, 30.0f * scale);

    // ESP 名称专用三档字号
    esp_s = AddCjk(atlas, 16.0f * scale);
    esp_m = AddCjk(atlas, 24.0f * scale);
    esp_l = AddCjk(atlas, 36.0f * scale);

    ico_0 = AddIcon(atlas, 20.0f * scale);
    ico_1 = AddIcon(atlas, 26.0f * scale);

    g_fonts.title  = ubu_2;
    g_fonts.button = ubu_0;
    g_fonts.body   = ubu_child;
    g_fonts.small  = ubu_preview;
    g_fonts.tiny   = ubu_preview;
    g_fonts.cjk    = (ubu_child != nullptr);
    snprintf(g_fonts.path, sizeof g_fonts.path, "%s", g_cjk_path ? g_cjk_path : "");
    g_fonts.icon_path = "icon";

    if (err && err_cap)
    {
        if (g_fonts.cjk) snprintf(err, (size_t)err_cap, "ok %s", g_cjk_path);
        else             snprintf(err, (size_t)err_cap, "load failed");
    }

    // 图集被清空后 io.FontDefault 会指向已释放的字体，必须重新指向新图集里的字体，
    // 否则 NewFrame() 里的 IM_ASSERT(g.Font->IsLoaded()) 会失败。
    if (ImGui::GetCurrentContext())
    {
        ImGuiIO& io = ImGui::GetIO();
        io.FontDefault = g_fonts.body ? g_fonts.body : (atlas->Fonts.empty() ? nullptr : atlas->Fonts[0]);
    }
    return g_fonts.cjk;
}

void SetUiScale(float scale)
{
    if (scale < 0.75f) scale = 0.75f;
    if (scale > 1.60f) scale = 1.60f;
    g_scale = scale;
}

void ApplyTheme(float scale)
{
    ImGuiStyle& st = ImGui::GetStyle();

    // 所有尺寸都按界面缩放放大，否则缩放只改了窗口大小、内边距与行距不变。
    if (scale <= 0.0f) scale = g_scale;
    if (scale < 0.5f) scale = 0.5f;
    if (scale > 2.5f) scale = 2.5f;

    st.WindowPadding            = ImVec2(0, 0);
    st.FramePadding             = ImVec2(6 * scale, 3 * scale);
    st.ItemSpacing              = ImVec2(0, 10 * scale);
    st.ItemInnerSpacing         = ImVec2(10 * scale, 0);
    st.CellPadding              = ImVec2(6 * scale, 3 * scale);
    st.IndentSpacing            = 20.0f * scale;
    st.ScrollbarSize            = 2.0f * scale;
    st.GrabMinSize              = 10.0f * scale;

    st.WindowBorderSize         = 0.0f;
    st.ChildBorderSize          = 1.0f * scale;
    st.PopupBorderSize          = 0.0f;
    st.FrameBorderSize          = 0.0f;
    st.TabBorderSize            = 0.0f;

    st.WindowRounding           = 8.0f * scale;
    st.ChildRounding            = 7.0f * scale;
    st.FrameRounding            = 4.0f * scale;
    st.PopupRounding            = 5.0f * scale;
    st.ScrollbarRounding        = 2.0f * scale;
    st.GrabRounding             = 4.0f * scale;
    st.TabRounding              = 3.0f * scale;

    st.WindowTitleAlign         = ImVec2(0.0f, 0.5f);
    st.WindowMenuButtonPosition = ImGuiDir_None;
    st.ButtonTextAlign          = ImVec2(0.5f, 0.5f);
    st.SelectableTextAlign      = ImVec2(0.0f, 0.5f);

    st.AntiAliasedLines         = true;
    st.AntiAliasedFill          = true;
    st.CurveTessellationTol     = 1.25f;

    ImVec4* c = st.Colors;
    const ImVec4 accent     = C(theme::Accent);
    const ImVec4 accentSoft = C(theme::AccentSoft);
    c[ImGuiCol_Text]                  = C(theme::TextHi);
    c[ImGuiCol_TextDisabled]          = C(theme::TextLow);
    c[ImGuiCol_WindowBg]              = C(theme::WinBg, theme::WinAlpha);
    c[ImGuiCol_ChildBg]               = C(theme::CardBg, theme::CardAlpha);
    c[ImGuiCol_PopupBg]               = C(theme::WinBg, 0.97f);
    c[ImGuiCol_Border]                = C(theme::Border, theme::BorderAlpha);
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]               = C(theme::PanelBg, 0.92f);
    c[ImGuiCol_FrameBgHovered]        = C(theme::CardHover, 0.96f);
    c[ImGuiCol_FrameBgActive]         = C(theme::CardBg, 1.0f);
    c[ImGuiCol_TitleBg]               = C(theme::WinBg, 1.0f);
    c[ImGuiCol_TitleBgActive]         = C(theme::WinBg, 1.0f);
    c[ImGuiCol_TitleBgCollapsed]      = C(theme::WinBg, 1.0f);
    c[ImGuiCol_MenuBarBg]             = C(theme::PanelBg, 1.0f);
    c[ImGuiCol_ScrollbarBg]           = C(theme::PanelBg, 1.0f);
    c[ImGuiCol_ScrollbarGrab]         = C(theme::Border, 0.95f);
    c[ImGuiCol_ScrollbarGrabHovered]  = C(theme::CardHover, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive]   = accent;
    c[ImGuiCol_CheckMark]             = accent;
    c[ImGuiCol_SliderGrab]            = accent;
    c[ImGuiCol_SliderGrabActive]      = accentSoft;
    c[ImGuiCol_Button]                = C(theme::CardBg, 0.92f);
    c[ImGuiCol_ButtonHovered]         = C(theme::CardHover, 1.0f);
    c[ImGuiCol_ButtonActive]          = accent;
    c[ImGuiCol_Header]                = C(theme::Border, 0.45f);
    c[ImGuiCol_HeaderHovered]         = C(theme::CardHover, 1.0f);
    c[ImGuiCol_HeaderActive]          = accent;
    c[ImGuiCol_Separator]             = C(theme::Border, 0.55f);
    c[ImGuiCol_SeparatorHovered]      = accentSoft;
    c[ImGuiCol_SeparatorActive]       = accent;
    c[ImGuiCol_ResizeGrip]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered]     = C(theme::Accent, 0.60f);
    c[ImGuiCol_ResizeGripActive]      = accent;
    c[ImGuiCol_Tab]                   = C(theme::Border, 0.45f);
    c[ImGuiCol_TabHovered]            = C(theme::CardHover, 1.0f);
    c[ImGuiCol_TabActive]             = accent;
    c[ImGuiCol_TabUnfocused]          = C(theme::Border, 0.35f);
    c[ImGuiCol_TabUnfocusedActive]    = C(theme::Accent, 0.70f);
    c[ImGuiCol_PlotLines]             = C(theme::Cyan);
    c[ImGuiCol_PlotLinesHovered]      = C(theme::Cyan, 0.85f);
    c[ImGuiCol_PlotHistogram]         = accent;
    c[ImGuiCol_PlotHistogramHovered]  = accentSoft;
    c[ImGuiCol_TableHeaderBg]         = C(theme::PanelBg, 1.0f);
    c[ImGuiCol_TableBorderStrong]     = C(theme::Border, 0.7f);
    c[ImGuiCol_TableBorderLight]      = C(theme::Border, 0.4f);
    c[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]         = C(theme::TextHi, 0.04f);
    c[ImGuiCol_TextSelectedBg]        = C(theme::Accent, 0.38f);
    c[ImGuiCol_DragDropTarget]        = C(theme::Cyan);
    c[ImGuiCol_NavHighlight]          = accentSoft;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1, 1, 1, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0, 0, 0, 0.20f);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0, 0, 0, 0.45f);

    st.WindowPadding = ImVec2(0, 0);
}

void HLine(float x0, float x1, float y, ImU32 col, float th)
{
    ImGui::GetWindowDrawList()->AddLine(ImVec2(x0, y), ImVec2(x1, y), col, th);
}

}
