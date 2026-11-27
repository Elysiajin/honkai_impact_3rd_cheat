#pragma once
// Lua 控制台：定位当前进程里的 LuaState，并调用 LuaState.DoString 执行脚本。
// 只做执行和结果读取；Lua VM 非线程安全，必须在游戏主线程调用。

#include "ov_data.h"
#include "ov_registry.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ovlua {

static const int    kMaxStates  = 8;
static const int    kStepMs     = 4;
static const size_t kReadQwords = 4096;

// 本构建实测（CE dump 逐字段校验）：
//   LuaState klass 名字 "LuaState"，DoString Token = 0x06000B17
//   RPSLuaManager 单例：Singleton`1 静态块 → [0] = 实例 → 实例 +0x10 = LuaState
static const uint32_t  kDoStringToken = 0x06000B17;

struct Session
{
    uintptr_t lua_klass = 0;
    uintptr_t str_klass = 0;
    uintptr_t script_klass = 0;
    uintptr_t mi        = 0;   // LuaState.DoString 的 MethodInfo
    uintptr_t fn        = 0;   // LuaState.DoString 的真实函数指针

    uintptr_t states[kMaxStates] = {};
    bool      trusted[kMaxStates] = {};
    uintptr_t mgr_klass[8] = {};
    int       mgr_n   = 0;
    int       n      = 0;
    bool      scanning = false;
    std::vector<bh3::Runtime::Region> regions;
    int       rcur   = 0;
    size_t    roff   = 0;
    char      msg[160] = "未连接";
};

inline void add_state(ov::Data& d, Session& s, uintptr_t p, bool trusted);

// SDK 9.2.0：RPSLuaManager 从管理器注册表取，+0x10 = LuaState（LuaManager._luaState）。
// LuaState 的 klass 直接从实例头取，DoString 用该 klass + token 解析，全程不扫类名。
inline bool resolve(ov::Data& d, Session& s)
{
    if (!d.ready_ok()) { lstrcpynA(s.msg, "内存未就绪", (int)sizeof s.msg); return false; }
    s.n = 0;

    uintptr_t mgr = ovr::RegistryInstance(d, "MoleMole.RPSLuaManager");
    if (!mgr) mgr = ovr::RegistryInstance(d, "MoleMole.LuaManager");
    uintptr_t ls = 0;
    if (mgr) d.rd(mgr + 0x10, ls);
    if (!ls) {
        lstrcpynA(s.msg, "注册表里没有 RPSLuaManager.LuaState", (int)sizeof s.msg);
        return false;
    }
    uintptr_t lk = 0;
    if (!d.rd(ls, lk) || !lk) {
        lstrcpynA(s.msg, "LuaState 实例头无效", (int)sizeof s.msg);
        return false;
    }
    s.lua_klass = lk;
    if (!s.mi || !s.fn) {
        ovr::Hit h;
        if (!ovr::ResolveMethod(lk, kDoStringToken, &h)) {
            lstrcpynA(s.msg, "LuaState.DoString 解析失败", (int)sizeof s.msg);
            return false;
        }
        s.mi = h.mi;
        s.fn = h.fn;
    }
    if (!s.str_klass) {
        // LuaState.errorTag(+0x38) 是 String；若为空，用注册表 key（也是 String）的 klass。
        uintptr_t tag = 0;
        if (d.rd(ls + 0x38, tag) && tag) d.rd(tag, s.str_klass);
        if (!s.str_klass) s.str_klass = ovr::RegistryStringKlass(d);
    }
    if (!s.str_klass) {
        lstrcpynA(s.msg, "String klass 未取到（LuaState.errorTag 为空）", (int)sizeof s.msg);
        return false;
    }
    add_state(d, s, ls, true);
    if (s.n == 0) {
        lstrcpynA(s.msg, "LuaState 校验失败", (int)sizeof s.msg);
        return false;
    }
    lstrcpynA(s.msg, "注册表直取 RPSLuaManager.LuaState", (int)sizeof s.msg);
    return true;
}

inline void add_state(ov::Data& d, Session& s, uintptr_t p, bool trusted)
{
    // 托管对象头：+0x00 klass、+0x08 monitor。
    uint64_t mon = 0;
    if (!d.rd(p + 0x08, mon)) return;
    if (mon != 0 && mon < 0x10000ULL) return;

    // +0x18 是 0x00000000ffffffff 说明 p 落在 MethodInfo 里，不是托管对象。
    uint64_t marker = 0;
    if (d.rd(p + 0x18, marker) && marker == 0x00000000ffffffffULL) return;

    // +0x60 是 LuaState.L，这个偏移已由反汇编确认。
    uintptr_t L = 0;
    if (!d.rd(p + 0x60, L) || !L) return;
    if (!d.readable(L, 8)) return;
    // Lua 状态在托管堆，不可能落在 UserAssembly 的代码段里。
    if (L >= d.ua && L < d.ua + (uint64_t)ovp::kBuildImageSize) return;

    for (int i = 0; i < s.n; ++i)
    {
        if (s.states[i] != p) continue;
        if (trusted) s.trusted[i] = true;
        return;
    }
    if (s.n < kMaxStates)
    {
        s.states[s.n] = p;
        s.trusted[s.n] = trusted;
        ++s.n;
    }
}

// 从 LuaManager 实例里找 LuaState：既看继承字段 _luaState(+0x18)，
// 也看子类自己的 LuaScriptMgr(_luaMgr).lua，避免某些活动管理器不初始化继承字段。
inline void add_states_from_object(ov::Data& d, Session& s, uintptr_t obj)
{
    if (!obj) return;
    for (uintptr_t off = 0x10; off < 0x400; off += 8)
    {
        uintptr_t q = 0;
        if (!d.rd(obj + off, q) || !q || !d.readable(q, 8)) continue;
        uintptr_t k = 0;
        if (!d.rd(q, k)) continue;
        if (k == s.lua_klass)
        {
            add_state(d, s, q, true);
            continue;
        }
        if (!s.script_klass || k != s.script_klass) continue;
        // LuaScriptMgr 里的 lua_State 包装对象
        for (uintptr_t o2 = 0x10; o2 < 0x100; o2 += 8)
        {
            uintptr_t q2 = 0;
            if (!d.rd(q + o2, q2) || !q2 || !d.readable(q2, 8)) continue;
            uintptr_t k2 = 0;
            if (d.rd(q2, k2) && k2 == s.lua_klass) add_state(d, s, q2, true);
        }
    }
}

// 不再扫堆：LuaState 只从管理器注册表取。
inline void start_scan(ov::Data& d, Session& s)
{
    s.scanning = false;
    resolve(d, s);
}

inline void step_scan(ov::Data&, Session&) {}

inline bool read_managed_string(ov::Data& d, uintptr_t str, std::string& out)
{
    int32_t len = 0;
    if (!d.rd(str + 0x10, len) || len < 0 || len > (1 << 19)) return false;
    if (len == 0) { out = "(空字符串)"; return true; }
    std::vector<uint16_t> w((size_t)len);
    const uintptr_t base = str + 0x14;
    const size_t total = (size_t)len * 2;
    for (size_t off = 0; off < total; )
    {
        const size_t chunk = (total - off) > (64u << 10) ? (64u << 10) : (total - off);
        SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(base + off),
                               (uint8_t*)w.data() + off, chunk, &got) || got != chunk)
            return false;
        off += chunk;
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)w.data(), len, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return false;
    out.resize((size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)w.data(), len, out.data(), n, nullptr, nullptr);
    return true;
}

inline std::string wrap_code(const char* code, bool guard_hook = true)
{
    std::string s;
    s += "local __out={}\n";
    s += "local __oldprint=print\n";
    s += "print=function(...) local t={} for i=1,select('#',...) do t[#t+1]=tostring(select(i,...)) end __out[#__out+1]=table.concat(t,'\\t') end\n";
    // 指令数硬上限：防止死循环/长循环把游戏主线程冻住；debug 不可用时降级为无限制。
    s += "local __steps=0\n";
    s += "local __hooked=false\n";
    if (guard_hook)
    {
        s += "if type(debug)=='table' and type(debug.sethook)=='function' then\n";
        s += "  pcall(function() debug.sethook(function() __steps=__steps+1 if __steps>200000 then error('Lua 指令超限') end end,'',1000) __hooked=true end)\n";
        s += "end\n";
    }
    s += "local __fn=function()\n";
    s += code ? code : "";
    s += "\nend\n";
    s += "local __res={pcall(__fn)}\n";
    s += "if __hooked then pcall(function() debug.sethook() end) end\n";
    s += "print=__oldprint\n";
    s += "local __ok=table.remove(__res,1)\n";
    s += "if not __ok then\n";
    s += "  __out[#__out+1]='ERROR: '..tostring(__res[1])\n";
    s += "else\n";
    s += "  for i=1,#__res do __out[#__out+1]=tostring(__res[i]) end\n";
    s += "end\n";
    s += "return table.concat(__out,'\\n')\n";
    return s;
}

inline bool exec(ov::Data& d, Session& s, int sel, const char* code, std::string& out,
                 size_t out_cap = 3900, bool need_trusted = true, bool guard_hook = true)
{
    out.clear();
    if (!code) { out = "空脚本"; return false; }
    const size_t clen = std::strlen(code);
    if (clen == 0 || clen > 4096) { out = "脚本为空或超过 4096 字节"; return false; }
    if (!s.mi || !s.fn)
    {
        if (!resolve(d, s)) { out = s.msg; return false; }
    }
    if (s.n == 0)
    {
        out = "没有找到 LuaState。先在控制台点“刷新状态”。";
        return false;
    }
    if (sel < 0 || sel >= s.n) sel = 0;
    if (need_trusted && !s.trusted[sel])
    {
        out = "该 LuaState 未通过 LuaManager 校验，请进入游戏/关卡后再刷新。";
        return false;
    }
    const uintptr_t p = s.states[sel];

    uintptr_t k = 0, mon = 0, L = 0;
    if (!d.rd(p, k) || k != s.lua_klass || !d.rd(p + 0x08, mon) || mon >= 0x1000 ||
        !d.rd(p + 0x60, L) || !L || !d.readable(L, 8))
    {
        out = "选中的 LuaState 已失效，请刷新状态";
        return false;
    }

    const std::string src = wrap_code(code, guard_hook);
    std::vector<uint16_t> w;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, src.c_str(), (int)src.size(), nullptr, 0);
    if (wlen <= 0) { out = "脚本编码失败"; return false; }
    w.resize((size_t)wlen);
    MultiByteToWideChar(CP_UTF8, 0, src.c_str(), (int)src.size(), (LPWSTR)w.data(), wlen);

    // 伪造一个 Il2CppString，仅供 DoString 同步读取；不进入 GC 堆。
    const size_t bytes = 0x14 + (size_t)wlen * 2 + 2;
    std::vector<uint8_t> fake(bytes, 0);
    *(void**)&fake[0]    = (void*)s.str_klass;
    *(void**)&fake[8]    = nullptr;
    *(int32_t*)&fake[0x10] = wlen;
    memcpy(&fake[0x14], w.data(), (size_t)wlen * 2);

    typedef void* (*DoStringFn)(void*, void*, void*);
    void* ret = ((DoStringFn)s.fn)((void*)p, fake.data(), (void*)s.mi);
    if (!ret) { out = "(执行完成，无返回值)"; return true; }

    // DoString 可能直接返回 string，也可能返回 object[]；两种都试。
    uintptr_t rk = 0;
    if (d.rd((uintptr_t)ret, rk) && rk == s.str_klass)
    {
        if (!read_managed_string(d, (uintptr_t)ret, out)) { out = "(字符串读取失败)"; return false; }
        if (out_cap && out.size() > out_cap) out.resize(out_cap);
        return true;
    }

    int32_t alen = 0;
    if (!d.rd((uintptr_t)ret + 0x18, alen) || alen <= 0)
    {
        out = "(空结果)";
        return true;
    }
    uintptr_t item = 0;
    if (!d.rd((uintptr_t)ret + 0x20, item) || !item)
    {
        out = "nil";
        return true;
    }
    uintptr_t ik = 0;
    if (!d.rd(item, ik)) { out = "(无法读取返回值)"; return false; }
    if (ik == s.str_klass)
    {
        if (!read_managed_string(d, item, out)) { out = "(字符串读取失败)"; return false; }
    }
    else
    {
        out = "(返回类型: " + d.name_ref(ik) + ")";
    }
    if (out_cap && out.size() > out_cap) out.resize(out_cap);
    return true;
}

} // namespace ovlua
