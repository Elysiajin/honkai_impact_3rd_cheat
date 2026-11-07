#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>

namespace ui
{

struct Settings
{
    bool  esp_on    = true;
    bool  esp_name  = true;
    bool  esp_dist  = true;
    bool  esp_box   = true;
    bool  god       = false;
    bool  inf_sp    = false;
    bool  no_cd     = false;
    bool  crit      = false;
    bool  atk_on    = false;
    float atk_mult  = 2.0f;

    float esp_h     = 1.7f;
    float max_dist  = 300.0f;
    float esp_col[4] = { 1.00f, 0.40f, 0.40f, 1.00f };

    float esp_font  = 20.0f;
    bool  esp_bold  = true;
    float esp_off   = 6.0f;

    float ui_scale  = 1.0f;
    bool  anim_on   = true;
    bool  show_grid = true;
    int   page      = 0;
    bool  esp_smooth = true;
    bool  esp_team  = true;

    // 活动战斗
    int   act_pick   = 0;      // 0=相遇·回忆玩具小屋
    bool  act_on     = false;  // 活动模块开关
    bool  act_god    = false;  // 我方无敌
    bool  act_kill   = false;  // 持续秒杀（跨波次）
};

Settings& Cfg();

struct Status
{
    bool     data_ready   = false;
    int      class_count  = 0;
    uintptr_t ua          = 0;
    uintptr_t avatar_mgr  = 0;
    uintptr_t monster_mgr = 0;
    int      avatar_n     = 0;
    int      monster_n    = 0;

    bool     cam_ok       = false;
    uintptr_t camera      = 0;
    float    cam_w        = 0.0f;
    float    cam_h        = 0.0f;
    char     cam_msg[160] = { 0 };

    float    fps          = 0.0f;
    double   frame_ms     = 0.0;
    int      esp_drawn    = 0;
    int      kill_count   = 0;
    bool     god_applied  = false;
    bool     inf_sp_applied = false;
    bool     no_cd_applied  = false;
    bool     crit_applied   = false;
    bool     atk_speed_applied = false;
    uint64_t stamp        = 0;

    // 活动战斗
    bool     act_ready    = false;
    bool     act_in       = false;
    int      act_n        = 0;
    int      act_ally     = 0;
    int      act_enemy    = 0;
    int      act_scan_ms  = 0;
    int      act_killed   = 0;
    char     act_msg[160] = { 0 };

    struct ActUnit
    {
        int camp    = 0;   // 1=我方 2=敌方 0=中立
        int raw_camp = 0;  // 原始 Actor_Camp，调试用
        int hp      = 0;
        int max_hp  = 0;
        int tag     = 0;   // 角色 ID 或怪物 ID
        int slot    = 0;   // 列表下标
    };
    ActUnit  act_u[32];
};

Status& St();

struct Requests
{
    bool kill_all  = false;
    bool rescan    = false;
    bool act_kill_all = false;
    bool act_scan_again = false;
    bool act_refresh = false;
};

Requests& Req();

void  Toast(const char* msg);

template <class... A>
void Toastf(const char* fmt, A&&... a)
{
    char buf[192] = { 0 };
    if (fmt) snprintf(buf, sizeof buf, fmt, a...);
    buf[sizeof buf - 1] = 0;
    Toast(buf);
}

void  ToastTick(float dt);
int   ToastList(const char** out, float* alpha, int cap);

const char* AppVersion();
const char* AppBuildTime();

void StateInit();

}
