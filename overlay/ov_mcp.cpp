#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ov_mcp.h"

#include <windows.h>

#include <Zydis/Zydis.h>
#include <Zydis/Utils.h>

#include "cxxmcp/peer.hpp"
#include "cxxmcp/server.hpp"
#include "cxxmcp/service.hpp"

#include "ov_profile.h"
#include "../sdk/bh3_sdk.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ovmcp
{
namespace
{

using Json = mcp::protocol::Json;

constexpr size_t kInlineBytes   = 8u * 1024u;
constexpr size_t kMaxInline     = 64u * 1024u;
constexpr size_t kMaxTransfer   = 64u * 1024u * 1024u;
constexpr size_t kMaxTextDump   = 4u * 1024u * 1024u;
constexpr int    kMaxDisasm     = 4096;
constexpr size_t kMaxInlineArray = 1024;
constexpr int    kMaxJobQueue   = 64;
constexpr size_t kMaxLuaCode    = 4096;
constexpr unsigned short kDefaultPort = 18517;
const char* const kFileDir = "D:\\My Projects\\Honkai Impact\\mcp_file";

enum SvcState { kStopped = 0, kStarting = 1, kRunning = 2, kStopping = 3 };

std::atomic<int>          g_state{ kStopped };
std::atomic<unsigned short> g_port{ 0 };
std::atomic<bool>         g_enabled{ true };
std::atomic<uint64_t>     g_generation{ 0 };
std::atomic<uint64_t>     g_retry_tick{ 0 };

std::mutex                g_server_mu;
std::shared_ptr<mcp::RunningService<mcp::RoleServer>> g_server;

std::mutex                g_error_mu;
std::string               g_error;

std::mutex                g_sync_mu;

std::mutex                g_game_mu;
struct GameInfo
{
    uintptr_t ua = 0;
    bool      ready = false;
    int       avatars = 0;
    int       monsters = 0;
    bool      cam_ok = false;
    char      msg[160] = { 0 };
    char      diag[256] = { 0 };
    uint64_t  stamp = 0;
};
GameInfo g_game;

std::atomic<uint32_t> g_file_seq{ 0 };

struct OvCallArgs { uint64_t gpr[8]; float fpr[4]; };
struct OvCallResult { uint64_t rax; float xmm0; };

extern "C" void ov_x64_call(uint64_t fn, const OvCallArgs* args, OvCallResult* result);

struct CallJob
{
    std::mutex              mu;
    std::condition_variable cv;
    bool                    done = false;
    bool                    cancelled = false;
    bool                    ok = false;
    int                     kind = 0;   // 0 = x64 call, 1 = lua eval
    uint64_t                fn = 0;
    uint64_t                gpr[8] = {};
    float                   fpr[4] = {};
    uint64_t                deadline = 0;
    uint64_t                rax = 0;
    float                   xmm0 = 0.0f;
    uint64_t                exec_ms = 0;
    char                    error[160] = { 0 };
    std::string             code;
    std::string             out;
    std::string             err;
};

std::mutex g_jobs_mu;
std::vector<std::shared_ptr<CallJob>> g_jobs;

std::mutex        g_lua_handler_mu;
LuaEvalHandler    g_lua_handler;

// ---------------------------------------------------------------- 基础工具
std::string Hex64(uint64_t v)
{
    char b[32] = { 0 };
    std::snprintf(b, sizeof b, "0x%llX", (unsigned long long)v);
    return b;
}

std::string Hex32(unsigned v)
{
    char b[24] = { 0 };
    std::snprintf(b, sizeof b, "0x%X", v);
    return b;
}

void SetError(const std::string& e)
{
    std::lock_guard<std::mutex> lock(g_error_mu);
    g_error = e;
}

Json OkJson()
{
    Json j = Json::object();
    j["ok"] = true;
    return j;
}

Json ErrJson(const std::string& e)
{
    Json j = Json::object();
    j["ok"] = false;
    j["error"] = e;
    return j;
}

Json Prop(const char* type, const char* desc)
{
    Json p = Json::object();
    p["type"] = type;
    p["description"] = desc;
    return p;
}

Json EnumProp(const char* type, const char* desc,
              std::initializer_list<const char*> values)
{
    Json p = Prop(type, desc);
    Json a = Json::array();
    for (auto v : values) a.push_back(v);
    p["enum"] = std::move(a);
    return p;
}

Json SchemaOf(std::initializer_list<std::pair<const char*, Json>> props,
              std::initializer_list<const char*> required = {})
{
    Json s = Json::object();
    s["type"] = "object";
    Json p = Json::object();
    for (const auto& kv : props) p[kv.first] = kv.second;
    s["properties"] = std::move(p);
    if (required.size())
    {
        Json r = Json::array();
        for (auto k : required) r.push_back(k);
        s["required"] = std::move(r);
    }
    return s;
}

bool Contains(const Json& in, const char* key) { return in.is_object() && in.contains(key); }

void ParseAddrString(std::string s, uint64_t& out, bool& ok)
{
    ok = false;
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s = s.substr(2);
    if (s.empty()) return;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return;
    errno = 0;
    char* end = nullptr;
    const unsigned long long v = std::strtoull(s.c_str(), &end, 16);
    if (errno || !end || *end) return;
    out = (uint64_t)v;
    ok = true;
}

bool GetU64(const Json& in, const char* key, uint64_t& out, std::string& err)
{
    if (!Contains(in, key)) { err = std::string("缺少参数: ") + key; return false; }
    const Json& v = in.at(key);
    if (v.is_number_unsigned()) { out = v.get<uint64_t>(); return true; }
    if (v.is_number_integer())
    {
        const int64_t n = v.get<int64_t>();
        if (n < 0) { err = std::string(key) + " 不能为负数"; return false; }
        out = (uint64_t)n;
        return true;
    }
    if (v.is_string())
    {
        bool ok = false;
        ParseAddrString(v.get<std::string>(), out, ok);
        if (!ok) { err = std::string(key) + " 不是有效的十六进制值"; return false; }
        return true;
    }
    err = std::string(key) + " 类型错误";
    return false;
}

bool GetSize(const Json& in, const char* key, size_t& out, size_t maxv, std::string& err)
{
    uint64_t v = 0;
    if (!GetU64(in, key, v, err)) return false;
    if (v == 0 || v > maxv)
    {
        err = std::string(key) + " 超出范围 (1.." + std::to_string((unsigned long long)maxv) + ")";
        return false;
    }
    out = (size_t)v;
    return true;
}

std::string GetString(const Json& in, const char* key, const char* def = "")
{
    if (!Contains(in, key) || !in.at(key).is_string()) return def;
    return in.at(key).get<std::string>();
}

bool IsAbsolute(const std::string& p)
{
    if (p.size() >= 2 && p[1] == ':') return true;
    if (p.size() >= 2 && (p[0] == '\\' || p[0] == '/') && (p[1] == '\\' || p[1] == '/')) return true;
    return false;
}

std::string FileDirWithSlash()
{
    return std::string(kFileDir) + "\\";
}

bool EnsureFileDir(std::string& err)
{
    if (!CreateDirectoryA(kFileDir, nullptr))
    {
        const DWORD e = GetLastError();
        if (e != ERROR_ALREADY_EXISTS)
        {
            err = "无法创建目录 " + std::string(kFileDir) + " (错误 " + std::to_string((unsigned long)e) + ")";
            return false;
        }
    }
    return true;
}

bool ResolveFile(const std::string& in, std::string& full, std::string& err)
{
    if (in.empty()) { err = "文件名为空"; return false; }
    std::string p = in;
    if (!IsAbsolute(p)) p = FileDirWithSlash() + p;
    char buf[1024] = { 0 };
    const DWORD n = GetFullPathNameA(p.c_str(), (DWORD)(sizeof buf / sizeof buf[0]), buf, nullptr);
    if (n == 0 || n >= sizeof buf / sizeof buf[0]) { err = "路径无效"; return false; }
    full = buf;

    const std::string prefix = FileDirWithSlash();
    if (full.size() < prefix.size() ||
        _strnicmp(full.c_str(), prefix.c_str(), (int)prefix.size()) != 0)
    {
        err = "只允许访问 " + std::string(kFileDir);
        return false;
    }
    return true;
}

std::string NewFilePath(const char* tag, uintptr_t addr, const char* ext)
{
    const uint32_t seq = g_file_seq.fetch_add(1) + 1;
    char name[96] = { 0 };
    std::snprintf(name, sizeof name, "%s_%08llX_%04u.%s", tag,
                  (unsigned long long)addr, (unsigned)seq, ext);
    return FileDirWithSlash() + name;
}

bool WriteFileBytes(const std::string& path, const void* data, size_t n)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = n == 0 || std::fwrite(data, 1, n, f) == n;
    std::fclose(f);
    return ok;
}

bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& out, size_t maxv, std::string& err)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { err = "无法打开文件: " + path; return false; }
    _fseeki64(f, 0, SEEK_END);
    const int64_t sz = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
    if (sz < 0) { std::fclose(f); err = "无法读取文件大小"; return false; }
    if ((uint64_t)sz > (uint64_t)maxv)
    {
        std::fclose(f);
        err = "文件超过上限 " + std::to_string((unsigned long long)maxv) + " 字节";
        return false;
    }
    out.resize((size_t)sz);
    const bool ok = sz == 0 || std::fread(out.data(), 1, (size_t)sz, f) == (size_t)sz;
    std::fclose(f);
    if (!ok) { err = "读取文件失败"; return false; }
    return true;
}

bool HasBinExt(const std::string& path)
{
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    const std::string ext = path.substr(dot);
    return _stricmp(ext.c_str(), ".bin") == 0;
}

bool ParseHexBytes(const std::string& text, std::vector<uint8_t>& out, std::string& err)
{
    out.clear();
    out.reserve(text.size() / 2 + 1);
    size_t i = 0;
    while (i < text.size())
    {
        const size_t line_end = text.find('\n', i);
        const size_t end = (line_end == std::string::npos) ? text.size() : line_end;
        size_t p = i;
        while (p < end && (text[p] == ' ' || text[p] == '\t')) ++p;
        if (p < end && text[p] == '#') { i = (line_end == std::string::npos) ? text.size() : line_end + 1; continue; }

        int hi = -1;
        while (i < end)
        {
            const char c = text[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == ',' || c == ';') { ++i; continue; }
            if (c == '0' && i + 1 < end && (text[i + 1] == 'x' || text[i + 1] == 'X')) { i += 2; continue; }
            int v = -1;
            if (c >= '0' && c <= '9') v = c - '0';
            else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
            if (v < 0) { err = std::string("文件包含非十六进制字符: ") + c; return false; }
            if (hi < 0) hi = v;
            else { out.push_back((uint8_t)((hi << 4) | v)); hi = -1; }
            ++i;
        }
        if (hi >= 0) { err = "十六进制字节数为奇数"; return false; }
        i = (line_end == std::string::npos) ? text.size() : line_end + 1;
    }
    if (out.empty()) { err = "文件中没有数据"; return false; }
    return true;
}

bool ParseDataFile(const std::string& in, std::vector<uint8_t>& data, std::string& err)
{
    std::string full;
    if (!ResolveFile(in, full, err)) return false;
    if (HasBinExt(full)) return ReadFileBytes(full, data, kMaxTransfer, err);

    std::vector<uint8_t> raw;
    if (!ReadFileBytes(full, raw, kMaxTransfer, err)) return false;
    std::string text((const char*)raw.data(), raw.size());
    return ParseHexBytes(text, data, err);
}

std::string HexBytes(const uint8_t* p, size_t n, size_t per_line)
{
    std::string s;
    s.reserve(n * 3 + n / per_line * 2 + 8);
    for (size_t i = 0; i < n; ++i)
    {
        char b[8] = { 0 };
        std::snprintf(b, sizeof b, "%02X", (unsigned)p[i]);
        if (i) s += ((per_line && i % per_line == 0) ? '\n' : ' ');
        s += b;
    }
    return s;
}

bool WriteMemDump(uintptr_t addr, const std::vector<uint8_t>& data,
                  std::string& bin_path, std::string& txt_path, std::string& err)
{
    if (!EnsureFileDir(err)) return false;
    bin_path = NewFilePath("read", addr, "bin");
    txt_path.clear();
    if (!WriteFileBytes(bin_path, data.data(), data.size()))
    {
        err = "写入失败: " + bin_path;
        return false;
    }
    if (data.size() <= kMaxTextDump)
    {
        txt_path = NewFilePath("read", addr, "txt");
        std::string text = "# bh3 mcp memory dump\n# address " + Hex64(addr) +
                           " size " + std::to_string((unsigned long long)data.size()) + "\n" +
                           HexBytes(data.data(), data.size(), 16) + "\n";
        if (!WriteFileBytes(txt_path, text.data(), text.size())) txt_path.clear();
    }
    return true;
}

// ---------------------------------------------------------------- 内存访问
struct RegionInfo
{
    bool      ok = false;
    uintptr_t base = 0;
    size_t    size = 0;
    DWORD     protect = 0;
    DWORD     state = 0;
    DWORD     type = 0;
};

RegionInfo QueryRegion(uintptr_t addr)
{
    RegionInfo r;
    MEMORY_BASIC_INFORMATION mbi;
    std::memset(&mbi, 0, sizeof mbi);
    if (VirtualQuery((LPCVOID)addr, &mbi, sizeof mbi) != sizeof mbi) return r;
    r.ok = true;
    r.base = (uintptr_t)mbi.BaseAddress;
    r.size = (size_t)mbi.RegionSize;
    r.protect = mbi.Protect;
    r.state = mbi.State;
    r.type = mbi.Type;
    return r;
}

bool ProtectReadable(DWORD p)
{
    if (p & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return (p & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                 PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                 PAGE_EXECUTE_WRITECOPY)) != 0;
}

bool ProtectWritable(DWORD p)
{
    if (p & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return (p & (PAGE_READWRITE | PAGE_WRITECOPY |
                 PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

bool ProtectExecutable(DWORD p)
{
    if (p & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return (p & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                 PAGE_EXECUTE_WRITECOPY)) != 0;
}

// 只使用 ReadProcessMemory，分块跨区域读取，避免直接解引用。
size_t ReadRaw(uintptr_t addr, void* dst, size_t n)
{
    if (!addr || !dst || !n) return 0;
    if (n > UINTPTR_MAX - addr) return 0;
    uint8_t* p = (uint8_t*)dst;
    size_t done = 0;
    while (done < n)
    {
        const uintptr_t cur = addr + done;
        const RegionInfo r = QueryRegion(cur);
        if (!r.ok || r.state != MEM_COMMIT || !ProtectReadable(r.protect)) break;
        size_t room = (size_t)(r.base + r.size - cur);
        size_t chunk = n - done;
        if (chunk > room) chunk = room;
        if (chunk > 65536) chunk = 65536;
        SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)cur, p + done, chunk, &got)) break;
        if (!got) break;
        done += (size_t)got;
        if ((size_t)got < chunk) break;
    }
    return done;
}

size_t WriteRaw(uintptr_t addr, const void* src, size_t n, DWORD& first_old, bool& restored)
{
    if (!addr || !src || !n) return 0;
    if (n > UINTPTR_MAX - addr) return 0;
    const uint8_t* p = (const uint8_t*)src;
    size_t done = 0;
    first_old = 0;
    restored = true;
    while (done < n)
    {
        const uintptr_t cur = addr + done;
        const RegionInfo r = QueryRegion(cur);
        if (!r.ok || r.state != MEM_COMMIT || (r.protect & PAGE_NOACCESS)) break;
        size_t room = (size_t)(r.base + r.size - cur);
        size_t chunk = n - done;
        if (chunk > room) chunk = room;
        if (chunk > 65536) chunk = 65536;

        DWORD old = 0;
        bool changed = false;
        if (!ProtectWritable(r.protect))
        {
            const DWORD exec = r.protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                                            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
            const DWORD np = exec ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
            if (!VirtualProtect((LPVOID)cur, chunk, np, &old)) break;
            changed = true;
        }
        if (!done) first_old = changed ? old : r.protect;

        SIZE_T wrote = 0;
        const bool ok = WriteProcessMemory(GetCurrentProcess(), (LPVOID)cur,
                                           p + done, chunk, &wrote) && wrote == chunk;
        if (changed)
        {
            DWORD tmp = 0;
            if (!VirtualProtect((LPVOID)cur, chunk, old, &tmp)) restored = false;
        }
        if (!ok) break;
        done += chunk;
    }
    return done;
}

Json RegionJson(uintptr_t addr)
{
    Json j = Json::object();
    const RegionInfo r = QueryRegion(addr);
    if (!r.ok) return j;
    j["base"] = Hex64((uint64_t)r.base);
    j["size"] = (uint64_t)r.size;
    j["protect"] = Hex32(r.protect);
    j["state"] = Hex32(r.state);
    return j;
}

// ---------------------------------------------------------------- 调用队列
bool IsExecutable(uintptr_t fn)
{
    const RegionInfo r = QueryRegion(fn);
    return r.ok && r.state == MEM_COMMIT && ProtectExecutable(r.protect);
}

void FinishJob(const std::shared_ptr<CallJob>& job, bool ok, const char* error,
               uint64_t rax, float xmm0, uint64_t exec_ms)
{
    {
        std::lock_guard<std::mutex> lock(job->mu);
        job->ok = ok;
        job->rax = rax;
        job->xmm0 = xmm0;
        job->exec_ms = exec_ms;
        if (error) std::snprintf(job->error, sizeof job->error, "%s", error);
        job->done = true;
    }
    job->cv.notify_all();
}

Json SubmitCall(uint64_t fn, const uint64_t gpr[8], const float fpr[4], int timeout_ms)
{
    if (!fn) return ErrJson("函数地址为 0");
    if (!IsExecutable(fn)) return ErrJson("目标地址不在可执行内存: " + Hex64(fn));

    auto job = std::make_shared<CallJob>();
    job->fn = fn;
    std::memcpy(job->gpr, gpr, sizeof job->gpr);
    std::memcpy(job->fpr, fpr, sizeof job->fpr);
    job->deadline = GetTickCount64() + (uint64_t)timeout_ms;

    {
        std::lock_guard<std::mutex> lock(g_jobs_mu);
        if (g_jobs.size() >= (size_t)kMaxJobQueue) return ErrJson("调用队列已满");
        g_jobs.push_back(job);
    }

    bool finished = false;
    {
        std::unique_lock<std::mutex> lock(job->mu);
        finished = job->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                    [&] { return job->done; });
        if (!finished)
        {
            job->cancelled = true;
            lock.unlock();
            return ErrJson("等待游戏线程执行超时（OnGameThreadTick 未运行或函数阻塞）");
        }
        if (!job->ok)
        {
            const std::string e = job->error[0] ? job->error : "调用失败";
            return ErrJson(e);
        }
        Json j = OkJson();
        j["address"] = Hex64(fn);
        j["rax"] = Hex64(job->rax);
        j["xmm0"] = job->xmm0;
        j["elapsed_ms"] = job->exec_ms;
        j["thread"] = "game";
        return j;
    }
}

void FinishLuaJob(const std::shared_ptr<CallJob>& job, bool ok,
                  const std::string& err, const std::string& out, uint64_t exec_ms)
{
    {
        std::lock_guard<std::mutex> lock(job->mu);
        job->ok = ok;
        job->err = err;
        job->out = out;
        job->exec_ms = exec_ms;
        job->done = true;
    }
    job->cv.notify_all();
}

Json SubmitLua(const std::string& code, int timeout_ms)
{
    if (code.empty()) return ErrJson("Lua 代码为空");
    if (code.size() > kMaxLuaCode) return ErrJson("Lua 代码超过 4096 字节");
    if (code.find('\0') != std::string::npos) return ErrJson("Lua 代码包含 NUL 字节");

    {
        std::lock_guard<std::mutex> lock(g_lua_handler_mu);
        if (!g_lua_handler) return ErrJson("Lua 执行器未注册（等待 overlay 初始化）");
    }

    auto job = std::make_shared<CallJob>();
    job->kind = 1;
    job->code = code;
    job->deadline = GetTickCount64() + (uint64_t)timeout_ms;

    {
        std::lock_guard<std::mutex> lock(g_jobs_mu);
        if (g_jobs.size() >= (size_t)kMaxJobQueue) return ErrJson("调用队列已满");
        g_jobs.push_back(job);
    }

    bool finished = false;
    std::string out, err;
    uint64_t exec_ms = 0;
    bool ok = false;
    {
        std::unique_lock<std::mutex> lock(job->mu);
        finished = job->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                    [&] { return job->done; });
        if (!finished)
        {
            job->cancelled = true;
            return ErrJson("等待游戏线程执行 Lua 超时（OnGameThreadTick 未运行或脚本阻塞）");
        }
        ok = job->ok;
        out = job->out;
        err = job->err;
        exec_ms = job->exec_ms;
    }
    if (!ok) return ErrJson(err.empty() ? "Lua 执行失败" : err);

    Json j = OkJson();
    j["output"] = out;
    j["elapsed_ms"] = exec_ms;
    j["thread"] = "game";
    return j;
}

// ---------------------------------------------------------------- 工具实现
Json ToolStatus(const Json&)
{
    Json j = OkJson();
    GameInfo g;
    {
        std::lock_guard<std::mutex> lock(g_game_mu);
        g = g_game;
    }
    j["ua"] = Hex64((uint64_t)g.ua);
    j["data_ready"] = g.ready;
    j["avatars"] = g.avatars;
    j["monsters"] = g.monsters;
    j["cam_ok"] = g.cam_ok;
    j["msg"] = g.msg;
    j["diag"] = g.diag;
    j["stamp_ms"] = (uint64_t)g.stamp;

    if (g.ua)
    {
        IMAGE_DOS_HEADER dos;
        IMAGE_NT_HEADERS64 nt;
        if (ovp::ReadBytes(g.ua, &dos, sizeof dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
            ovp::ReadBytes(g.ua + (uintptr_t)dos.e_lfanew, &nt, sizeof nt) &&
            nt.Signature == IMAGE_NT_SIGNATURE)
        {
            j["ua_size"] = (uint64_t)nt.OptionalHeader.SizeOfImage;
            j["build_stamp"] = Hex32(nt.FileHeader.TimeDateStamp);
        }

        uintptr_t metadata_header = 0, metadata_body = 0, startup_buf = 0;
        uint32_t il2cpp_version = 0;
        if (ReadRaw(g.ua + bh3::RVA_METADATA_HEADER, &metadata_header, sizeof metadata_header))
            j["metadata_header"] = Hex64((uint64_t)metadata_header);
        if (ReadRaw(g.ua + bh3::RVA_METADATA_BODY, &metadata_body, sizeof metadata_body))
            j["metadata_body"] = Hex64((uint64_t)metadata_body);
        if (ReadRaw(g.ua + bh3::RVA_STARTUP_BUF, &startup_buf, sizeof startup_buf))
            j["startup_buf"] = Hex64((uint64_t)startup_buf);
        if (ReadRaw(g.ua + bh3::RVA_IL2CPP_VERSION, &il2cpp_version, sizeof il2cpp_version))
            j["il2cpp_version"] = il2cpp_version;
    }

    Json m = Json::object();
    m["state"] = State();
    m["port"] = (unsigned)Port();
    m["file_dir"] = FileDir();
    m["tools"] = ToolCount();
    m["error"] = Error();
    j["mcp"] = std::move(m);
    return j;
}

Json ToolSymbols(const Json& in)
{
    Json j = OkJson();
    GameInfo g;
    {
        std::lock_guard<std::mutex> lock(g_game_mu);
        g = g_game;
    }
    const std::string filter = GetString(in, "filter", "");
    const bool valid_only = Contains(in, "valid_only") && in.at("valid_only").is_boolean() &&
                            in.at("valid_only").get<bool>();

    Json arr = Json::array();
    int count = 0;
    for (int i = 0; i < (int)ovp::SymCount; ++i)
    {
        const ovp::Sym s = (ovp::Sym)i;
        const char* key = ovp::Key(s);
        const uint32_t rva = ovp::Rva(s);
        if (!key || !key[0]) continue;
        if (filter.size() && std::string(key).find(filter) == std::string::npos) continue;
        if (valid_only && !rva) continue;

        Json e = Json::object();
        e["name"] = key;
        e["rva"] = Hex32(rva);
        e["va"] = Hex64((uint64_t)(g.ua + rva));
        e["source"] = ovp::Source(s);
        e["valid"] = rva != 0;
        e["needs_review"] = ovp::NeedsReview(s);
        if (ovp::Valid(s) && g.ua)
        {
            uint8_t pro[8] = { 0 };
            if (ReadRaw((uintptr_t)(g.ua + rva), pro, sizeof pro))
            {
                char hex[32] = { 0 };
                for (int k = 0; k < 8; ++k)
                    std::snprintf(hex + k * 2, sizeof hex - (size_t)k * 2, "%02X", (unsigned)pro[k]);
                e["prologue8"] = hex;
            }
        }
        arr.push_back(std::move(e));
        ++count;
    }
    j["count"] = count;
    j["ua"] = Hex64((uint64_t)g.ua);
    j["symbols"] = std::move(arr);
    return j;
}

Json ToolReadMemory(const Json& in)
{
    try
    {
        uint64_t addr = 0;
        size_t size = 0;
        std::string err;
        if (!GetU64(in, "address", addr, err)) return ErrJson(err);
        if (!GetSize(in, "size", size, kMaxTransfer, err)) return ErrJson(err);
        if (!addr) return ErrJson("地址为 0");
        if ((uint64_t)size > UINTPTR_MAX - addr) return ErrJson("地址范围溢出");

        const std::string format = GetString(in, "format", "hex");
        const std::string mode = GetString(in, "mode", "auto");

        std::vector<uint8_t> data(size);
        const size_t got = ReadRaw((uintptr_t)addr, data.data(), size);
        if (!got) return ErrJson("读取失败，目标内存不可读（见 region）: " + Hex64(addr));
        data.resize(got);

        bool use_file = mode == "file";
        if (!use_file && mode != "inline")
        {
            if (format == "u32" || format == "u64" || format == "f32")
            {
                const size_t es = (format == "u64") ? 8u : 4u;
                use_file = data.size() / es > kMaxInlineArray;
            }
            else
            {
                use_file = got > kInlineBytes;
            }
        }

        Json j = OkJson();
        j["address"] = Hex64(addr);
        j["requested"] = (uint64_t)size;
        j["read"] = (uint64_t)got;
        j["truncated"] = got < size;
        j["region"] = RegionJson((uintptr_t)addr);

        if (use_file)
        {
            std::string bin_path, txt_path, werr;
            if (!WriteMemDump((uintptr_t)addr, data, bin_path, txt_path, werr))
                return ErrJson(werr);
            j["transfer"] = "file";
            j["raw_path"] = bin_path;
            if (!txt_path.empty()) j["text_path"] = txt_path;
            j["note"] = data.size() > kMaxTextDump
                            ? "原始数据超过 4MB，只生成 .bin"
                            : "可直接读取 .txt（纯十六进制，每行 16 字节，# 开头为注释）";
            j["preview"] = HexBytes(data.data(), (std::min)(got, (size_t)32), 32);
            return j;
        }

        if (mode == "inline" && got > kMaxInline)
            return ErrJson("inline 模式上限 64KB，请使用 mode=file");

        j["transfer"] = "inline";
        if (format == "utf8")
        {
            j["encoding"] = "utf8";
            j["data"] = std::string((const char*)data.data(), data.size());
        }
        else if (format == "u32" || format == "u64" || format == "f32")
        {
            const size_t es = (format == "u64") ? 8 : 4;
            const size_t n = data.size() / es;
            if (n > kMaxInlineArray) return ErrJson("数组元素过多，请改用 mode=file");
            Json arr = Json::array();
            for (size_t i = 0; i < n; ++i)
            {
                if (format == "u32") { uint32_t v = 0; std::memcpy(&v, data.data() + i * es, 4); arr.push_back(v); }
                else if (format == "f32") { float v = 0; std::memcpy(&v, data.data() + i * es, 4); arr.push_back(v); }
                else { uint64_t v = 0; std::memcpy(&v, data.data() + i * es, 8); arr.push_back(Hex64(v)); }
            }
            j["encoding"] = format;
            j["data"] = std::move(arr);
        }
        else
        {
            j["encoding"] = "hex";
            j["data"] = HexBytes(data.data(), data.size(), 16);
        }
        return j;
    }
    catch (const std::exception& e)
    {
        return ErrJson(e.what());
    }
}

Json ToolWriteMemory(const Json& in)
{
    try
    {
        uint64_t addr = 0;
        std::string err;
        if (!GetU64(in, "address", addr, err)) return ErrJson(err);
        if (!addr) return ErrJson("地址为 0");

        std::vector<uint8_t> data;
        if (Contains(in, "data_file") && in.at("data_file").is_string())
        {
            if (!ParseDataFile(in.at("data_file").get<std::string>(), data, err))
                return ErrJson(err);
        }
        else if (Contains(in, "data_hex") && in.at("data_hex").is_string())
        {
            if (!ParseHexBytes(in.at("data_hex").get<std::string>(), data, err))
                return ErrJson(err);
        }
        else
        {
            return ErrJson("需要 data_hex 或 data_file");
        }

        if (data.empty()) return ErrJson("没有可写入的数据");
        if (data.size() > kMaxTransfer) return ErrJson("写入数据超过 64MB");
        if ((uint64_t)data.size() > UINTPTR_MAX - addr) return ErrJson("地址范围溢出");

        DWORD old_protect = 0;
        bool restored = true;
        const size_t written = WriteRaw((uintptr_t)addr, data.data(), data.size(),
                                        old_protect, restored);
        if (!written) return ErrJson("写入失败，目标内存不可写: " + Hex64(addr));

        bool verified = false;
        if (!Contains(in, "verify") || !in.at("verify").is_boolean() ||
            in.at("verify").get<bool>())
        {
            std::vector<uint8_t> back(written);
            const size_t got = ReadRaw((uintptr_t)addr, back.data(), written);
            verified = got == written && std::memcmp(back.data(), data.data(), written) == 0;
        }

        Json j = OkJson();
        j["address"] = Hex64(addr);
        j["written"] = (uint64_t)written;
        j["requested"] = (uint64_t)data.size();
        j["verified"] = verified;
        j["old_protect"] = Hex32(old_protect);
        j["protect_restored"] = restored;
        j["region"] = RegionJson((uintptr_t)addr);
        if (written < data.size()) j["truncated"] = true;
        return j;
    }
    catch (const std::exception& e)
    {
        return ErrJson(e.what());
    }
}

Json ToolDisassemble(const Json& in)
{
    try
    {
        uint64_t addr = 0;
        std::string err;
        if (!GetU64(in, "address", addr, err)) return ErrJson(err);
        if (!addr) return ErrJson("地址为 0");

        size_t count = 40;
        if (Contains(in, "count"))
        {
            if (!GetSize(in, "count", count, (size_t)kMaxDisasm, err)) return ErrJson(err);
        }
        const std::string mode = GetString(in, "mode", "auto");
        const std::string syntax = GetString(in, "syntax", "intel");

        ZydisDecoder decoder;
        ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

        ZydisFormatter formatter;
        ZydisFormatterInit(&formatter,
                           _stricmp(syntax.c_str(), "att") == 0
                               ? ZYDIS_FORMATTER_STYLE_ATT
                               : ZYDIS_FORMATTER_STYLE_INTEL);

        struct Item
        {
            uint64_t addr = 0;
            uint8_t  len = 0;
            uint8_t  bytes[16] = {};
            bool     has_target = false;
            uint64_t target = 0;
            char     text[256] = {};
        };
        std::vector<Item> items;
        items.reserve(count);

        uintptr_t cur = (uintptr_t)addr;
        for (size_t i = 0; i < count; ++i)
        {
            uint8_t code[15] = { 0 };
            const size_t got = ReadRaw(cur, code, sizeof code);
            if (!got) break;

            ZydisDecodedInstruction insn;
            ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
            std::memset(&insn, 0, sizeof insn);
            std::memset(ops, 0, sizeof ops);
            const ZyanStatus st = ZydisDecoderDecodeFull(&decoder, code, got, &insn, ops);

            Item it;
            it.addr = (uint64_t)cur;
            it.len = (st == ZYAN_STATUS_SUCCESS && insn.length) ? insn.length : 1;
            std::memcpy(it.bytes, code, it.len);

            if (st == ZYAN_STATUS_SUCCESS)
            {
                char buf[256] = { 0 };
                if (ZYAN_SUCCESS(ZydisFormatterFormatInstruction(
                        &formatter, &insn, ops, insn.operand_count_visible,
                        buf, sizeof buf, (ZyanU64)cur, nullptr)))
                {
                    std::snprintf(it.text, sizeof it.text, "%s", buf);
                }
                else
                {
                    std::snprintf(it.text, sizeof it.text, "?");
                }

                for (ZyanU8 o = 0; o < insn.operand_count_visible; ++o)
                {
                    ZyanU64 abs = 0;
                    if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&insn, &ops[o], (ZyanU64)cur, &abs)))
                    {
                        it.has_target = true;
                        it.target = (uint64_t)abs;
                        break;
                    }
                }
            }
            else
            {
                std::snprintf(it.text, sizeof it.text, ".byte 0x%02X", (unsigned)code[0]);
            }

            items.push_back(it);
            cur += it.len;
            if (cur < (uintptr_t)addr) break;
            if (i == 0 && it.len == 1 && st != ZYAN_STATUS_SUCCESS) { /* keep stepping */ }
        }

        if (items.empty()) return ErrJson("反汇编失败，目标内存不可读: " + Hex64(addr));

        std::string text = "# bh3 mcp disassembly\n# start " + Hex64(addr) +
                           " count " + std::to_string((unsigned long long)items.size()) + "\n";
        for (const Item& it : items)
        {
            char line[512] = { 0 };
            std::string bytes;
            for (uint8_t k = 0; k < it.len; ++k)
            {
                char b[8] = { 0 };
                std::snprintf(b, sizeof b, "%s%02X", k ? " " : "", (unsigned)it.bytes[k]);
                bytes += b;
            }
            if (it.has_target)
                std::snprintf(line, sizeof line, "%s  %-44s %s  ; -> %s\n",
                              Hex64(it.addr).c_str(), bytes.c_str(), it.text, Hex64(it.target).c_str());
            else
                std::snprintf(line, sizeof line, "%s  %-44s %s\n",
                              Hex64(it.addr).c_str(), bytes.c_str(), it.text);
            text += line;
        }

        const bool use_file = mode == "file" ||
                              (mode != "inline" && (items.size() > 96 || text.size() > 32768));
        Json j = OkJson();
        j["address"] = Hex64(addr);
        j["count"] = (uint64_t)items.size();
        j["mode"] = mode;
        if (use_file)
        {
            if (!EnsureFileDir(err)) return ErrJson(err);
            const std::string path = NewFilePath("disasm", (uintptr_t)addr, "txt");
            if (!WriteFileBytes(path, text.data(), text.size())) return ErrJson("写入反汇编文件失败");
            j["transfer"] = "file";
            j["path"] = path;
            j["bytes"] = (uint64_t)(cur - (uintptr_t)addr);
            return j;
        }

        Json arr = Json::array();
        for (const Item& it : items)
        {
            Json e = Json::object();
            e["address"] = Hex64(it.addr);
            e["bytes"] = HexBytes(it.bytes, it.len, it.len);
            e["text"] = it.text;
            if (it.has_target) e["target"] = Hex64(it.target);
            arr.push_back(std::move(e));
        }
        j["transfer"] = "inline";
        j["bytes"] = (uint64_t)(cur - (uintptr_t)addr);
        j["instructions"] = std::move(arr);
        return j;
    }
    catch (const std::exception& e)
    {
        return ErrJson(e.what());
    }
}

int ClampTimeout(const Json& in)
{
    uint64_t t = 3000;
    if (Contains(in, "timeout_ms") && in.at("timeout_ms").is_number_integer())
        t = (uint64_t)in.at("timeout_ms").get<int64_t>();
    if (t < 100) t = 100;
    if (t > 10000) t = 10000;
    return (int)t;
}

Json ToolCallFunction(const Json& in)
{
    try
    {
        uint64_t fn = 0;
        std::string err;
        if (!GetU64(in, "address", fn, err)) return ErrJson(err);

        uint64_t gpr[8] = {};
        float fpr[4] = {};
        if (Contains(in, "gpr") && in.at("gpr").is_array())
        {
            const Json& a = in.at("gpr");
            if (a.size() > 8) return ErrJson("gpr 最多 8 项");
            for (size_t i = 0; i < a.size(); ++i)
            {
                uint64_t v = 0;
                if (a[i].is_string())
                {
                    bool ok = false;
                    ParseAddrString(a[i].get<std::string>(), v, ok);
                    if (!ok) return ErrJson("gpr 项不是有效十六进制");
                }
                else if (a[i].is_number_unsigned()) v = a[i].get<uint64_t>();
                else if (a[i].is_number_integer())
                {
                    const int64_t n = a[i].get<int64_t>();
                    if (n < 0) return ErrJson("gpr 项不能为负数");
                    v = (uint64_t)n;
                }
                else return ErrJson("gpr 项类型错误");
                gpr[i] = v;
            }
        }
        if (Contains(in, "fpr") && in.at("fpr").is_array())
        {
            const Json& a = in.at("fpr");
            if (a.size() > 4) return ErrJson("fpr 最多 4 项");
            for (size_t i = 0; i < a.size(); ++i)
            {
                if (!a[i].is_number()) return ErrJson("fpr 项必须是数字");
                fpr[i] = a[i].get<float>();
            }
        }
        return SubmitCall(fn, gpr, fpr, ClampTimeout(in));
    }
    catch (const std::exception& e)
    {
        return ErrJson(e.what());
    }
}

Json ToolCallMethod(const Json& in)
{
    try
    {
        uint64_t fn = 0, mi = 0, self = 0;
        std::string err;
        if (!GetU64(in, "address", fn, err)) return ErrJson(err);
        if (!GetU64(in, "method_info", mi, err)) return ErrJson(err);

        const bool has_self = Contains(in, "this");
        if (has_self && !GetU64(in, "this", self, err)) return ErrJson(err);

        uint64_t gpr[8] = {};
        size_t n = 0;
        if (has_self) gpr[n++] = self;

        if (Contains(in, "int_args") && in.at("int_args").is_array())
        {
            const Json& a = in.at("int_args");
            for (size_t i = 0; i < a.size(); ++i)
            {
                if (n + 1 >= 8) return ErrJson("参数过多（this + int_args + method_info 最多 8 项）");
                uint64_t v = 0;
                if (a[i].is_string())
                {
                    bool ok = false;
                    ParseAddrString(a[i].get<std::string>(), v, ok);
                    if (!ok) return ErrJson("int_args 项不是有效十六进制");
                }
                else if (a[i].is_number_unsigned()) v = a[i].get<uint64_t>();
                else if (a[i].is_number_integer())
                {
                    const int64_t x = a[i].get<int64_t>();
                    if (x < 0) return ErrJson("int_args 项不能为负数");
                    v = (uint64_t)x;
                }
                else return ErrJson("int_args 项类型错误");
                gpr[n++] = v;
            }
        }
        gpr[n++] = mi;

        float fpr[4] = {};
        if (Contains(in, "float_args") && in.at("float_args").is_array())
        {
            const Json& a = in.at("float_args");
            if (a.size() > 4) return ErrJson("float_args 最多 4 项");
            for (size_t i = 0; i < a.size(); ++i)
            {
                if (!a[i].is_number()) return ErrJson("float_args 项必须是数字");
                fpr[i] = a[i].get<float>();
            }
        }
        return SubmitCall(fn, gpr, fpr, ClampTimeout(in));
    }
    catch (const std::exception& e)
    {
        return ErrJson(e.what());
    }
}

Json ToolLuaEval(const Json& in)
{
    try
    {
        if (!Contains(in, "code") || !in.at("code").is_string())
            return ErrJson("缺少字符串参数 code");
        const std::string code = in.at("code").get<std::string>();
        return SubmitLua(code, ClampTimeout(in));
    }
    catch (const std::exception& e)
    {
        return ErrJson(e.what());
    }
}

// ---------------------------------------------------------------- 服务生命周期
Json ReadSchema()
{
    return SchemaOf(
        {
            { "address", Prop("string", "十六进制地址，例如 0x7FF612340000") },
            { "size", Prop("integer", "读取字节数，最大 64MB") },
            { "format", EnumProp("string", "输出编码", { "hex", "u32", "u64", "f32", "utf8" }) },
            { "mode", EnumProp("string", "传输方式：auto 超 8KB 落盘", { "auto", "inline", "file" }) },
        },
        { "address", "size" });
}

Json WriteSchema()
{
    return SchemaOf(
        {
            { "address", Prop("string", "十六进制地址") },
            { "data_hex", Prop("string", "十六进制数据（空格/换行分隔）") },
            { "data_file", Prop("string", "mcp_file 下的 .bin 或纯十六进制 .txt") },
            { "verify", Prop("boolean", "写后回读校验，默认 true") },
        },
        { "address" });
}

Json DisasmSchema()
{
    return SchemaOf(
        {
            { "address", Prop("string", "起始地址") },
            { "count", Prop("integer", "指令条数，默认 40，最大 4096") },
            { "mode", EnumProp("string", "传输方式", { "auto", "inline", "file" }) },
            { "syntax", EnumProp("string", "汇编语法", { "intel", "att" }) },
        },
        { "address" });
}

Json CallSchema()
{
    return SchemaOf(
        {
            { "address", Prop("string", "函数地址") },
            { "gpr", Prop("array", "整数/指针参数，最多 8 项，依次 RCX,RDX,R8,R9,栈") },
            { "fpr", Prop("array", "float 参数，最多 4 项，依次 XMM0..3") },
            { "timeout_ms", Prop("integer", "等待游戏线程的毫秒数，默认 3000") },
        },
        { "address" });
}

Json CallMethodSchema()
{
    return SchemaOf(
        {
            { "address", Prop("string", "函数地址") },
            { "this", Prop("string", "实例指针（静态方法省略）") },
            { "method_info", Prop("string", "IL2CPP MethodInfo*，按方签名约定放在最后一个整数参数") },
            { "int_args", Prop("array", "整数参数，最多 8 项（含 this/method_info）") },
            { "float_args", Prop("array", "float 参数，最多 4 项") },
            { "timeout_ms", Prop("integer", "等待游戏线程的毫秒数，默认 3000") },
        },
        { "address", "method_info" });
}

Json LuaSchema()
{
    return SchemaOf(
        {
            { "code", Prop("string", "Lua 5.1 脚本，最大 4096 字节，游戏主线程执行") },
            { "timeout_ms", Prop("integer", "等待游戏线程的毫秒数，默认 3000，范围 100-10000") },
        },
        { "code" });
}

Json SymbolsSchema()
{
    return SchemaOf(
        {
            { "filter", Prop("string", "按符号名子串过滤") },
            { "valid_only", Prop("boolean", "只返回当前构建有效的符号") },
        });
}

auto BuildServer(unsigned short port)
{
    return mcp::ServerPeer::builder()
        .name("bh3-mcp")
        .version("1.0.0")
        .instructions(
            "崩坏3 进程内 MCP。地址参数使用十六进制字符串。大块数据通过 "
            "D:\\My Projects\\Honkai Impact\\mcp_file 交换：read_memory 落盘后用文件工具读取，"
            "write_memory 可传 data_file。调用游戏函数一律排队到游戏线程执行。")
        .streamable_http("127.0.0.1", port, "/mcp")
        .tool(mcp::server::tool<Json, Json>("get_status")
                  .description("返回游戏数据就绪状态、UserAssembly 基址、MCP 服务状态。")
                  .handler([](const Json& in) { return ToolStatus(in); }))
        .tool(mcp::server::tool<Json, Json>("list_symbols")
                  .description("列出 profile 中已解密的符号（RVA/VA/来源/有效性）及函数前导字节。")
                  .input_schema(SymbolsSchema())
                  .handler([](const Json& in) { return ToolSymbols(in); }))
        .tool(mcp::server::tool<Json, Json>("read_memory")
                  .description("用 ReadProcessMemory 读取进程内存；大块数据写入 mcp_file。")
                  .input_schema(ReadSchema())
                  .handler([](const Json& in) { return ToolReadMemory(in); }))
        .tool(mcp::server::tool<Json, Json>("write_memory")
                  .description("用 WriteProcessMemory 写入进程内存，自动临时改页保护并回读校验；大块数据可来自文件。")
                  .input_schema(WriteSchema())
                  .handler([](const Json& in) { return ToolWriteMemory(in); }))
        .tool(mcp::server::tool<Json, Json>("disassemble")
                  .description("用 Zydis 反汇编，返回地址、原始字节、指令文本和跳转/ RIP 相对目标。")
                  .input_schema(DisasmSchema())
                  .handler([](const Json& in) { return ToolDisassemble(in); }))
        .tool(mcp::server::tool<Json, Json>("call_function")
                  .description("在游戏主线程调用任意 x64 函数；gpr[0..3] 走寄存器，gpr[4..7] 压栈。签名错误可能导致游戏崩溃，建议先用 disassemble 确认。")
                  .input_schema(CallSchema())
                  .handler([](const Json& in) { return ToolCallFunction(in); }))
        .tool(mcp::server::tool<Json, Json>("call_method")
                  .description("按 IL2CPP 约定调用实例/静态方法：this 可选，MethodInfo 放在最后一个整数参数。MethodInfo 无效会导致游戏崩溃。")
                  .input_schema(CallMethodSchema())
                  .handler([](const Json& in) { return ToolCallMethod(in); }))
        .tool(mcp::server::tool<Json, Json>("lua_eval")
                  .description("在游戏主线程执行 Lua 5.1 脚本并返回 print/返回值。仅使用已通过 LuaManager 校验的 LuaState；pcall 包裹并限制指令数，不会因 Lua 错误中断游戏。参数错误或调用不存在的 C# 方法仍可能崩溃，优先做只读探测。")
                  .input_schema(LuaSchema())
                  .handler([](const Json& in) { return ToolLuaEval(in); }))
        .build();
}

void StartWorker(unsigned short port, uint64_t gen)
{
    auto built = BuildServer(port);
    if (!built)
    {
        SetError("构建 MCP server 失败: " + built.error().message);
        if (g_generation.load() == gen)
        {
            g_retry_tick.store(GetTickCount64() + 3000);
            g_state.store(kStopped);
        }
        return;
    }

    auto served = mcp::serve(std::move(*built));
    if (!served)
    {
        SetError("启动 MCP 服务失败: " + served.error().message);
        if (g_generation.load() == gen)
        {
            g_retry_tick.store(GetTickCount64() + 3000);
            g_state.store(kStopped);
        }
        return;
    }

    auto svc = std::shared_ptr<mcp::RunningService<mcp::RoleServer>>(
        new mcp::RunningService<mcp::RoleServer>(std::move(*served)));

    svc->wait_until_ready();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    if (g_generation.load() != gen || !svc->running())
    {
        (void)svc->stop();
        if (g_generation.load() == gen)
        {
            SetError("无法监听 127.0.0.1:" + std::to_string((unsigned)port) + "（端口被占用？）");
            g_retry_tick.store(GetTickCount64() + 3000);
            g_state.store(kStopped);
        }
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_server_mu);
        if (g_generation.load() != gen)
        {
            // 启动期间被停止：不发布，由本线程收尾。
        }
        else
        {
            g_server = svc;
            g_port.store(port);
            g_retry_tick.store(0);
            g_state.store(kRunning);
            SetError("");
            return;
        }
    }

    (void)svc->stop();
    if (g_generation.load() == gen) g_state.store(kStopped);
}

void Start(unsigned short port)
{
    int expected = kStopped;
    if (!g_state.compare_exchange_strong(expected, kStarting)) return;
    const uint64_t gen = g_generation.fetch_add(1) + 1;
    g_port.store(port);
    std::thread(StartWorker, port, gen).detach();
}

void Stop()
{
    const int st = g_state.load();
    if (st == kStopped) return;
    g_generation.fetch_add(1);
    if (st == kStarting)
    {
        // 启动线程发现 generation 变化后会自行停止并置回 kStopped。
        return;
    }
    if (st == kStopping) return;

    int expected = kRunning;
    if (!g_state.compare_exchange_strong(expected, kStopping)) return;

    std::shared_ptr<mcp::RunningService<mcp::RoleServer>> svc;
    {
        std::lock_guard<std::mutex> lock(g_server_mu);
        svc = std::move(g_server);
    }
    if (!svc)
    {
        g_state.store(kStopped);
        return;
    }

    std::thread([svc]() mutable
    {
        (void)svc->stop();
        g_state.store(kStopped);
    }).detach();
}

} // namespace

// ---------------------------------------------------------------- 公共接口
void Sync(bool enabled, unsigned short port)
{
    std::lock_guard<std::mutex> sync_lock(g_sync_mu);
    if (port < 1024 || port > 65535) port = kDefaultPort;
    const bool changed = g_enabled.exchange(enabled) != enabled;
    if (changed) g_retry_tick.store(0);

    if (!enabled)
    {
        Stop();
        return;
    }

    const int st = g_state.load();
    if (st == kStopped)
    {
        if (GetTickCount64() < g_retry_tick.load()) return;
        Start(port);
        return;
    }
    if (st == kRunning && port != g_port.load())
    {
        // 端口编辑期由 UI 连续触发时，等待数值稳定再重启。
        static uint64_t change_tick = 0;
        static unsigned short pending = 0;
        if (pending != port) { pending = port; change_tick = GetTickCount64(); return; }
        if (GetTickCount64() - change_tick < 800) return;
        pending = 0;
        Stop();
    }
}

int State() { return g_state.load(); }

unsigned short Port() { return g_port.load(); }

std::string Error()
{
    std::lock_guard<std::mutex> lock(g_error_mu);
    return g_error;
}

const char* FileDir() { return kFileDir; }

void SetLuaEvalHandler(LuaEvalHandler handler)
{
    std::lock_guard<std::mutex> lock(g_lua_handler_mu);
    g_lua_handler = std::move(handler);
}

int ToolCount() { return 8; }

void UpdateGameStatus(uintptr_t ua, bool ready, int avatars, int monsters,
                      bool cam_ok, const char* msg, const char* diag)
{
    std::lock_guard<std::mutex> lock(g_game_mu);
    g_game.ua = ua;
    g_game.ready = ready;
    g_game.avatars = avatars;
    g_game.monsters = monsters;
    g_game.cam_ok = cam_ok;
    g_game.stamp = GetTickCount64();
    std::snprintf(g_game.msg, sizeof g_game.msg, "%s", msg ? msg : "");
    std::snprintf(g_game.diag, sizeof g_game.diag, "%s", diag ? diag : "");
}

void OnGameThreadTick()
{
    for (int round = 0; round < 2; ++round)
    {
        std::shared_ptr<CallJob> job;
        {
            std::lock_guard<std::mutex> lock(g_jobs_mu);
            if (g_jobs.empty()) return;
            job = g_jobs.front();
            g_jobs.erase(g_jobs.begin());
        }

        {
            std::lock_guard<std::mutex> lock(job->mu);
            if (job->cancelled || GetTickCount64() > job->deadline)
            {
                job->done = true;
                job->ok = false;
                std::snprintf(job->error, sizeof job->error,
                              job->cancelled ? "调用已取消" : "等待超时，未执行");
                job->cv.notify_all();
                continue;
            }
        }

        if (job->kind == 1)
        {
            LuaEvalHandler handler;
            {
                std::lock_guard<std::mutex> lock(g_lua_handler_mu);
                handler = g_lua_handler;
            }
            if (!handler)
            {
                FinishLuaJob(job, false, "Lua 执行器未注册", "", 0);
                continue;
            }

            std::string out, err;
            bool ok = false;
            const uint64_t t0 = GetTickCount64();
            try { ok = handler(job->code, out, err); }
            catch (const std::exception& e) { err = e.what(); }
            catch (...) { err = "Lua 执行器抛出未知异常"; }
            const uint64_t used = GetTickCount64() - t0;
            FinishLuaJob(job, ok, err, out, used);
            continue;
        }

        if (!IsExecutable((uintptr_t)job->fn))
        {
            FinishJob(job, false, "目标地址不在可执行内存", 0, 0.0f, 0);
            continue;
        }

        OvCallArgs args;
        std::memset(&args, 0, sizeof args);
        std::memcpy(args.gpr, job->gpr, sizeof args.gpr);
        std::memcpy(args.fpr, job->fpr, sizeof args.fpr);
        OvCallResult result;
        std::memset(&result, 0, sizeof result);

        const uint64_t t0 = GetTickCount64();
        ov_x64_call(job->fn, &args, &result);
        const uint64_t used = GetTickCount64() - t0;

        FinishJob(job, true, "", result.rax, result.xmm0, used);
    }
}

} // namespace ovmcp
