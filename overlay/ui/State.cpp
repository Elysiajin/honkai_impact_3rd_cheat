#include "State.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ui
{

namespace
{
Settings g_cfg;
Status   g_st;
Requests g_req;

std::mutex               g_log_mtx;
std::vector<std::string> g_logs;

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

void LogPush(const char* line)
{
    if (!line || !line[0]) return;
    std::lock_guard<std::mutex> lk(g_log_mtx);
    if ((int)g_logs.size() >= kLogCap) g_logs.erase(g_logs.begin());
    g_logs.emplace_back(line);
}

void LogClear()
{
    std::lock_guard<std::mutex> lk(g_log_mtx);
    g_logs.clear();
}

int LogCount()
{
    std::lock_guard<std::mutex> lk(g_log_mtx);
    return (int)g_logs.size();
}

LogScope::LogScope() : lk_(g_log_mtx, std::defer_lock)
{
    lk_.lock();
    count_ = (int)g_logs.size();
}

LogScope::~LogScope() = default;

int LogScope::Count() const { return count_; }

const char* LogScope::At(int i) const
{
    if (i < 0 || i >= count_) return "";
    return g_logs[(size_t)i].c_str();
}

void Toast(const char* msg)
{
    if (!msg || !msg[0]) return;
    ToastRec& t = g_toasts[g_toast_w % kToastSlots];
    g_toast_w = (g_toast_w + 1) % kToastSlots;
    snprintf(t.text, sizeof t.text, "%s", msg);
    t.life = 1.6f;
}

void Toastf(const char* fmt, ...)
{
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    buf[sizeof buf - 1] = 0;
    Toast(buf);
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
        float l = g_toasts[i].life;
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

const char* AppVersion()   { return "v2.0"; }
const char* AppBuildTime() { return __DATE__ " " __TIME__; }

void StateInit()
{
    g_cfg = Settings();
    g_st  = Status();
    g_req = Requests();
    for (int i = 0; i < kToastSlots; ++i) g_toasts[i] = ToastRec();
}

}
