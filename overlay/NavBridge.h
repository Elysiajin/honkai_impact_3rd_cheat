#pragma once
// 寻路 UI 桥：PageEsp "寻路"卡片 ↔ ovnav/ov_gui.cpp。
// 渲染线程持有 g_nv（ESP 实体列表），fetch 在渲染线程调用直接拷贝；
// request_to 转发到 ovnav::nav_request_to（渲染线程写 atomic，游戏主线程消费）。
#include <cstdint>

namespace ovnavui
{

struct Item
{
    char    name[64] = {};
    char    label[64] = {};
    float   x = 0, y = 0, z = 0;
    float   dist = 0;          // 距玩家距离（米）
    uintptr_t p = 0;
};

// 类别过滤：0=宝箱/采集(gadgets) 1=晶体采集物(props) 2=npcs 3=monsters
bool fetch(int cat, Item* out, int cap, int& n);

// 指定目标坐标请求一次寻路（渲染线程调用）
void request_to(float x, float y, float z);

// 指定目标坐标请求一次传送（渲染线程调用）
void request_teleport(float x, float y, float z);

// 上次传送结果文本（未传送过为空串）
const char* tp_text();

// 上次寻路状态文本（读 ovnav::nav_state().msg）
const char* last_msg();

// 上次寻路是否成功
bool last_ok();

// 自检：不依赖玩家/目标，只验证能否取到 NavMesh 网格（异步，主线程执行）
void diag();

// 自检结果文本（未自检过为空串）
const char* diag_text();

// 一行调试摘要：请求数/消费数/耗时/符号 RVA/阶段码，用于确认链路是否打通
void debug_info(char* out, int cap);

} // namespace ovnavui
