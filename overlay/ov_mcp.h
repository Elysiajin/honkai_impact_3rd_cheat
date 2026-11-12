#pragma once

#include <cstdint>
#include <string>

// 进程内 MCP 服务：RPM 读写、Zydis 反汇编、游戏线程函数调用。
// 不暴露实现细节，UI 与 ov_gui.cpp 只依赖本头文件。
namespace ovmcp
{

// 幂等同步服务状态：enabled=false 会停止，端口变化会延迟重启。
void Sync(bool enabled, unsigned short port);

// 0=停止 1=启动中 2=运行中 3=停止中
int State();
unsigned short Port();
std::string Error();
const char* FileDir();
int ToolCount();

// 由游戏主线程每帧调用，执行排队的函数调用。
void OnGameThreadTick();

// 由游戏主线程每帧调用，缓存给 MCP 的状态。
void UpdateGameStatus(uintptr_t ua, bool ready, int avatars, int monsters,
                      bool cam_ok, const char* msg);

}
