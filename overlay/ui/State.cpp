#include "State.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <windows.h>

namespace ui
{

namespace
{
Settings g_cfg;
Status   g_st;
Requests g_req;

struct ToastRec
{
    char  text[192] = { 0 };
    float life      = 0.0f;
};
constexpr int kToastSlots = 4;
ToastRec g_toasts[kToastSlots];
int      g_toast_w = 0;

}

Settings& Cfg() { return g_cfg; }
Status&   St()  { return g_st; }
Requests& Req() { return g_req; }

void Toast(const char* msg)
{
    if (!msg || !msg[0]) return;
    ToastRec& t = g_toasts[g_toast_w % kToastSlots];
    g_toast_w = (g_toast_w + 1) % kToastSlots;
    snprintf(t.text, sizeof t.text, "%s", msg);
    t.life = 1.6f;
}

void ToastTick(float dt)
{
    for (int i = 0; i < kToastSlots; ++i)
    {
        if (g_toasts[i].life > 0.0f)
        {
            g_toasts[i].life -= dt;
            if (g_toasts[i].life < 0.0f) g_toasts[i].life = 0.0f;
        }
    }
}

int ToastList(const char** out, float* alpha, int cap)
{
    int n = 0;
    for (int i = 0; i < kToastSlots && n < cap; ++i)
    {
        if (g_toasts[i].life <= 0.0f) continue;
        out[n] = g_toasts[i].text;
        const float l = g_toasts[i].life;
        float a = 1.0f;
        if (l > 1.35f) a = (1.6f - l) / 0.25f;
        else if (l < 0.4f) a = l / 0.4f;
        if (a < 0.0f) a = 0.0f;
        if (a > 1.0f) a = 1.0f;
        alpha[n] = a;
        ++n;
    }
    return n;
}

const char* AppVersion()   { return "v2.1"; }
const char* AppBuildTime() { return __DATE__ " " __TIME__; }

void StateInit()
{
    g_cfg = Settings();
    g_st  = Status();
    g_req = Requests();
    for (int i = 0; i < kToastSlots; ++i) g_toasts[i] = ToastRec();
}

namespace
{

const char* ConfigPath()
{
    static char path[MAX_PATH] = { 0 };
    if (path[0]) return path;
    HMODULE mod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&ConfigPath, &mod);
    wchar_t wpath[MAX_PATH] = { 0 };
    GetModuleFileNameW(mod, wpath, MAX_PATH);
    wchar_t* slash = wcsrchr(wpath, L'\\');
    if (slash) *slash = 0;
    char dir[MAX_PATH] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, wpath, -1, dir, MAX_PATH, nullptr, nullptr);
    snprintf(path, sizeof path, "%s\\bh3_overlay.json", dir);
    return path;
}

const char* FindValue(const char* json, const char* key)
{
    char pat[96] = { 0 };
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char* p = strstr(json, pat);
    if (!p) return nullptr;
    p = strchr(p + strlen(pat), ':');
    return p ? p + 1 : nullptr;
}

float GetF(const char* json, const char* key, float def)
{
    const char* p = FindValue(json, key);
    return p ? (float)atof(p) : def;
}

int GetI(const char* json, const char* key, int def)
{
    const char* p = FindValue(json, key);
    return p ? atoi(p) : def;
}

bool GetB(const char* json, const char* key, bool def)
{
    const char* p = FindValue(json, key);
    if (!p) return def;
    while (*p == ' ') ++p;
    return strncmp(p, "true", 4) == 0;
}

} // namespace

void LoadConfig()
{
    FILE* f = fopen(ConfigPath(), "rb");
    if (!f) return;
    std::string text;
    char buf[4096];
    size_t n = 0;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    fclose(f);
    const char* js = text.c_str();
    Settings& c = g_cfg;
    c.page        = GetI(js, "page", c.page);
    c.theme       = GetI(js, "theme", c.theme);
    c.glass_alpha = GetF(js, "glass_alpha", c.glass_alpha);
    c.backdrop_alpha = GetF(js, "backdrop_alpha", c.backdrop_alpha);
    c.star_count  = GetI(js, "star_count", c.star_count);
    c.ui_scale    = GetF(js, "ui_scale", c.ui_scale);
    c.anim_on     = GetB(js, "anim_on", c.anim_on);
    c.show_grid   = GetB(js, "show_grid", c.show_grid);
}

void SaveConfig()
{
    const Settings& c = g_cfg;
    std::string s;
    char buf[192];
    s += "{\n";
#define PUT_I(k) snprintf(buf,sizeof buf,"  \"%s\": %d,\n",#k,(int)c.k); s+=buf;
#define PUT_F(k) snprintf(buf,sizeof buf,"  \"%s\": %.4f,\n",#k,(float)c.k); s+=buf;
#define PUT_B(k) snprintf(buf,sizeof buf,"  \"%s\": %s,\n",#k,c.k?"true":"false"); s+=buf;
    PUT_I(page) PUT_I(theme)
    PUT_F(glass_alpha) PUT_F(backdrop_alpha) PUT_I(star_count)
    PUT_F(ui_scale) PUT_B(anim_on) PUT_B(show_grid)
#undef PUT_I
#undef PUT_F
#undef PUT_B
    s += "  \"_end\": 0\n}\n";
    FILE* f = fopen(ConfigPath(), "wb");
    if (!f) return;
    fwrite(s.data(), 1, s.size(), f);
    fclose(f);
}

}
