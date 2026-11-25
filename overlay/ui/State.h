#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

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
    bool  dmg_on    = false;
    float dmg_mult  = 2.0f;
    bool  ohk       = false;

    float esp_h     = 1.7f;
    float max_dist  = 300.0f;
    float esp_col[4] = { 1.00f, 0.40f, 0.40f, 1.00f };

    float esp_font  = 20.0f;
    bool  esp_bold  = true;
    float esp_off   = 6.0f;

    float ui_scale  = 1.30f;
    bool  anim_on   = true;
    bool  show_grid = true;
    int   page      = 0;

    // 空间材质：0=原深紫主题，1..7=参考项目七套主题。
    int   theme          = 1;     // 默认「瓷白」，对应你截图里的风格
    float glass_alpha    = 1.0f;  // 玻璃浓度倍率
    float backdrop_alpha = 0.42f; // 全屏蓝绿背景强度
    int   star_count     = 110;   // 星点数量

    bool  esp_smooth = true;
    bool  esp_team  = true;
    bool  gadget_on = true;
    bool  npc_on    = true;
    bool  ray_on    = true;
    float ray_w     = 2.0f;
    float gadget_col[4] = { 1.00f, 0.80f, 0.20f, 1.00f };
    float npc_col[4]    = { 0.20f, 1.00f, 1.00f, 1.00f };

    // 活动战斗
    int   act_pick   = 0;      // 0=相遇·回忆玩具小屋
    bool  act_on     = false;  // 活动模块开关
    bool  act_god    = false;  // 我方无敌
    bool  act_kill   = false;  // 持续秒杀（跨波次）

    // Lua 控制台
    int   lua_sel    = 0;      // 当前选中的 LuaState 下标

    // MCP 服务
    bool  mcp_on     = true;   // 注入后自动监听
    int   mcp_port   = 18517;
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
    int      gadget_n     = 0;
    int      npc_n        = 0;
    int      gadget_list_n = 0;
    int      gadget_cap_n  = 0;
    int      gadget_read_n = 0;
    int      gadget_drop_inst = 0;
    int      gadget_drop_cls  = 0;
    int      gadget_drop_cfg  = 0;
    int      gadget_drop_coord = 0;
    int      gadget_fail = 0;

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
    bool     dmg_applied    = false;
    bool     ohk_applied    = false;
    uint64_t stamp        = 0;

    // Lua 控制台
    bool     lua_ready      = false;
    bool     lua_scanning   = false;
    int      lua_state_n    = 0;
    int      lua_trusted_n  = 0;
    uintptr_t lua_state     = 0;
    bool     lua_ok         = false;
    char     lua_msg[160]   = { 0 };
    char     lua_out[2048]  = { 0 };

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
    bool lua_scan = false;
    bool lua_exec = false;
    bool lua_trust = false;
    std::string lua_code;
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
void LoadConfig();
void SaveConfig();

}
