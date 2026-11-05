#include "State.h"

#include <cstdio>
#include <cstring>

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

}
