#pragma once
// 传送 UI 桥：PageEsp “传送”卡片 <-> ov_gui.cpp。
// 只负责把 ESP 已扫描到的实体列表拷给 UI，以及把传送请求转发给游戏主线程。
// 传送桥：只负责 ESP 实体列表与坐标传送。
#include <cstdint>

namespace ovtpui
{

struct Item
{
    char    name[64] = {};
    char    label[64] = {};
    float   x = 0, y = 0, z = 0;
    float   dist = 0;          // 距玩家距离（米）
    uintptr_t p = 0;
};

// 类别过滤：0=宝箱/采集(gadgets) 1=晶体采集物(props) 3=怪物(monsters)
bool fetch(int cat, Item* out, int cap, int& n);

// 指定目标坐标请求一次传送（渲染线程调用，游戏主线程执行）
void request_teleport(float x, float y, float z);

// 上次传送结果文本（未传送过为空串）
const char* tp_text();

// 实体绘制序号：每帧按距玩家距离升序统一编号，ESP 画面标签与传送列表共用。
// 入参是 Item::p（实体地址）；返回 0 表示未编号。
int rank_of(uintptr_t p);

} // namespace ovtpui
