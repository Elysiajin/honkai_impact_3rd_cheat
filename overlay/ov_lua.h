#pragma once
// Lua 控制台：定位当前进程里的 LuaState，并调用 LuaState.DoString 执行脚本。
// 只做执行和结果读取；Lua VM 非线程安全，必须在游戏主线程调用。

#include "ov_data.h"

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
static const uint32_t kDoStringToken = 0x06000B16;

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

inline bool probe_method_table(ov::Data& d, uintptr_t tab, uintptr_t klass,
                               uint32_t want_token, uintptr_t& mi, uintptr_t& fn)
{
    if (!tab || !d.readable(tab, 8)) return false;
    for (int i = 0; i < 256; ++i)
    {
        uintptr_t p = 0;
        if (!d.rd_u64(tab + (uintptr_t)i * 8, p) || !p)
        {
            if (i > 8) break;
            continue;
        }
        if (!d.readable(p, 0x40)) continue;

        // 布局 A：表项就是 MethodInfo*
        uintptr_t k = 0, f = 0;
        uint32_t tok = 0;
        if (d.rd_u64(p + 0x10, k) && k == klass &&
            d.rd<uint32_t>(p + 0x30, tok) && tok == want_token &&
            d.rd_u64(p + 0x08, f) && f)
        {
            mi = p;
            fn = f;
            return true;
        }

        // 布局 B：表项是 methodPointer，MethodInfo 在它前面 8 字节
        const uintptr_t m = p - 8;
        if (!d.readable(m, 0x40)) continue;
        k = 0; f = 0; tok = 0;
        if (d.rd_u64(m + 0x10, k) && k == klass &&
            d.rd<uint32_t>(m + 0x30, tok) && tok == want_token &&
            d.rd_u64(m + 0x08, f) && f)
        {
            mi = m;
            fn = f;
            return true;
        }
    }
    return false;
}

struct MiScanCtx
{
    ov::Data* d;
    uintptr_t klass;
    uintptr_t mi;
    uintptr_t fn;
};

inline bool mi_scan_cb(uintptr_t at, uintptr_t, void* vctx)
{
    MiScanCtx* c = (MiScanCtx*)vctx;
    if (!c || at < 0x10) return true;
    const uintptr_t mi = at - 0x10;
    uintptr_t k = 0, f = 0;
    uint32_t tok = 0;
    if (!c->d->rd_u64(mi + 0x10, k) || k != c->klass) return true;
    if (!c->d->rd<uint32_t>(mi + 0x30, tok) || tok != kDoStringToken) return true;
    if (!c->d->rd_u64(mi + 0x08, f) || !f) return true;
    c->mi = mi;
    c->fn = f;
    return false;
}

inline bool find_dostring_method(ov::Data& d, uintptr_t klass, uintptr_t& mi, uintptr_t& fn)
{
    mi = 0;
    fn = 0;
    if (!klass) return false;
    // 不同 IL2CPP image 的类结构不完全一样：把可疑的类内指针都试一遍。
    static const uint32_t offs[] = {
        0x58, 0x60, 0x68, 0x70, 0x78, 0x80, 0x88, 0x90,
        0x98, 0xA0, 0xA8, 0xB0, 0xB8, 0xC0, 0xC8, 0xD0,
    };
    for (int i = 0; i < (int)(sizeof offs / sizeof offs[0]); ++i)
    {
        uintptr_t tab = 0;
        if (!d.rd_u64(klass + offs[i], tab) || !tab) continue;
        if (probe_method_table(d, tab, klass, kDoStringToken, mi, fn)) return true;
    }

    // 兜底 A：dump 时的 MethodInfo 地址（部分构建的 IL2CPP 堆基址固定）。
    const uintptr_t fallback = 0x50002ce15e8ULL;
    if (d.readable(fallback, 0x40))
    {
        uintptr_t k = 0, f = 0;
        uint32_t tok = 0;
        if (d.rd_u64(fallback + 0x10, k) && k == klass &&
            d.rd<uint32_t>(fallback + 0x30, tok) && tok == kDoStringToken &&
            d.rd_u64(fallback + 0x08, f) && f)
        {
            mi = fallback;
            fn = f;
            return true;
        }
    }

    // 兜底 B：在堆里找 klass 指针，MethodInfo 的 +0x10 正好指向它。
    MiScanCtx ctx{ &d, klass, 0, 0 };
    ovr::ScanQwords(klass, mi_scan_cb, &ctx, 1u << 18, klass, (uint64_t)1 << 29);
    if (ctx.mi && ctx.fn)
    {
        mi = ctx.mi;
        fn = ctx.fn;
        return true;
    }
    return false;
}

inline bool resolve(ov::Data& d, Session& s)
{
    if (!d.ready_ok()) { lstrcpynA(s.msg, "内存未就绪", (int)sizeof s.msg); return false; }
    if (!s.lua_klass) s.lua_klass = ovr::FindKlass("LuaState");
    if (!s.str_klass) s.str_klass = ovr::FindKlass("String");
    if (!s.script_klass) s.script_klass = ovr::FindKlass("LuaScriptMgr");
    if (!s.lua_klass || !s.str_klass)
    {
        lstrcpynA(s.msg, "找不到 LuaState/String 类", (int)sizeof s.msg);
        return false;
    }
    if (!s.mi || !s.fn)
    {
        if (!find_dostring_method(d, s.lua_klass, s.mi, s.fn))
        {
            ovr::Hit h;
            if (!ovr::ResolveMethod(s.lua_klass, kDoStringToken, &h))
            {
                snprintf(s.msg, sizeof s.msg, "DoString 未解析 k=%p s=%p",
                         (void*)s.lua_klass, (void*)s.str_klass);
                return false;
            }
            s.mi = h.mi;
            s.fn = h.fn;
        }
    }
    lstrcpynA(s.msg, "已解析 LuaState.DoString", (int)sizeof s.msg);
    return true;
}

inline void add_state(ov::Data& d, Session& s, uintptr_t p, bool trusted)
{
    // 对象头校验：MethodInfo 的 +0x08 是 slot（小整数），真对象这里是 monitor。
    uint64_t mon = 0;
    if (!d.rd(p + 0x08, mon)) return;
    if (mon != 0 && mon < 0x10000ULL) return;

    // MethodInfo 的 +0x10 是 klass，后面 +0x28 固定是 0x00000000ffffffff；
    // 命中这个常量说明 p 是 MethodInfo+0x10，不是托管对象。
    uint64_t marker = 0;
    if (d.rd(p + 0x18, marker) && marker == 0x00000000ffffffffULL) return;

    // +0x60 是 LuaState.L，这个偏移已由反汇编确认。
    uintptr_t L = 0;
    if (!d.rd(p + 0x60, L) || !L) return;
    if (!d.readable(L, 8)) return;
    // Lua 状态在托管堆，不可能落在 UserAssembly 的代码段里。
    if (L >= d.ua && L < d.ua + 0x1284B000ULL) return;

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

// 快速路径：LuaScriptMgr 静态区里的 Instance → <lua> → LuaState → +0x60 = L，只探测静态区与单个对象，不扫堆。
inline bool find_state_fast(ov::Data& d, Session& s)
{
    if (!s.lua_klass) s.lua_klass = ovr::FindKlass("LuaState");
    if (!s.script_klass) s.script_klass = ovr::FindKlass("LuaScriptMgr");
    const uintptr_t sk = s.script_klass;
    const uintptr_t lk = s.lua_klass;
    if (!sk || !lk) return false;
    uintptr_t sf = 0;
    if (!d.rd_u64(sk + 0xB8, sf) || !sf || !d.readable(sf, 0x20)) return false;
    for (uintptr_t off = 0; off < 0x100; off += 8)
    {
        uintptr_t inst = 0;
        if (!d.rd_u64(sf + off, inst) || !inst || !d.readable(inst, 0x40)) continue;
        uintptr_t ik = 0;
        if (!d.rd_u64(inst, ik) || ik != sk) continue;
        for (uintptr_t fo = 0x10; fo < 0x300; fo += 8)
        {
            uintptr_t ls = 0;
            if (!d.rd_u64(inst + fo, ls) || !ls || !d.readable(ls, 0x80)) continue;
            uintptr_t lk2 = 0;
            if (!d.rd_u64(ls, lk2) || lk2 != lk) continue;
            add_state(d, s, ls, true);
            return s.n > 0;
        }
    }
    return false;
}

inline void start_scan(ov::Data& d, Session& s)
{
    if (!resolve(d, s)) return;
    s.n = 0;
    s.rcur = 0;
    s.roff = 0;
    s.regions = d.rt.scan_regions();
    s.scanning = true;
    s.mgr_n = 0;

    if (find_state_fast(d, s))
    {
        s.scanning = false;
        lstrcpynA(s.msg, "LuaState 快速定位（LuaScriptMgr 静态字段）", (int)sizeof s.msg);
        return;
    }

    // 只信任挂在 LuaManager._luaState(+0x18) 上的主状态；
    // 裸扫到的 LuaState 可能是子线程、未初始化实例或假阳性。
    static const char* kMgrNames[] = {
        "UILuaManager", "RPSLuaManager", "LevelDesignManager",
        "HonKaiTCGLuaManager", "TiledGameScriptor", "VisualNovelScriptor", "LuaManager",
    };
    for (int i = 0; i < (int)(sizeof kMgrNames / sizeof kMgrNames[0]); ++i)
    {
        const uintptr_t k = ovr::FindKlass(kMgrNames[i]);
        if (k && s.mgr_n < 8) s.mgr_klass[s.mgr_n++] = k;
    }
    lstrcpynA(s.msg, "扫描 LuaManager...", (int)sizeof s.msg);
}

// 分帧扫描，避免在游戏线程一次冻结太久
inline void step_scan(ov::Data& d, Session& s)
{
    if (!s.scanning) return;
    const uint64_t t0 = GetTickCount64();
    static thread_local uintptr_t buf[kReadQwords];

    while (s.rcur < (int)s.regions.size() &&
           GetTickCount64() - t0 < (uint64_t)kStepMs)
    {
        const bh3::Runtime::Region& r = s.regions[(size_t)s.rcur];
        if (!r.base || r.size < 0x68) { ++s.rcur; s.roff = 0; continue; }
        if (s.roff + 8 > r.size) { ++s.rcur; s.roff = 0; continue; }

        size_t want = (r.size - s.roff) / 8;
        if (want > kReadQwords) want = kReadQwords;
        SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(r.base + s.roff),
                               buf, want * 8, &got) || !got)
        {
            ++s.rcur;
            s.roff = 0;
            continue;
        }
        const size_t cnt = (size_t)got / 8;
        for (size_t i = 0; i < cnt; ++i)
        {
            const uintptr_t p = r.base + s.roff + i * 8;
            if (buf[i] == s.lua_klass)
            {
                // 裸扫命中的对象只有在通过 add_state 的对象头校验后才算有效状态。
                add_state(d, s, p, true);
                continue;
            }
            for (int j = 0; j < s.mgr_n; ++j)
            {
                if (buf[i] != s.mgr_klass[j]) continue;
                add_states_from_object(d, s, p);
                break;
            }
        }
        s.roff += cnt * 8;
        if (cnt < want) { ++s.rcur; s.roff = 0; }
    }

    if (s.rcur >= (int)s.regions.size() && s.scanning)
    {
        s.scanning = false;
        // 已校验的状态排到前面，UI 默认选中的就是可用状态。
        uintptr_t st[kMaxStates];
        bool tr[kMaxStates];
        int w = 0;
        for (int i = 0; i < s.n; ++i)
            if (s.trusted[i]) { st[w] = s.states[i]; tr[w] = true; ++w; }
        for (int i = 0; i < s.n; ++i)
            if (!s.trusted[i]) { st[w] = s.states[i]; tr[w] = false; ++w; }
        for (int i = 0; i < w; ++i) { s.states[i] = st[i]; s.trusted[i] = tr[i]; }
        int trusted_n = 0;
        for (int i = 0; i < w; ++i) if (s.trusted[i]) ++trusted_n;
        snprintf(s.msg, sizeof s.msg, "LuaState %d 个，已校验 %d 个，管理类 %d",
                 w, trusted_n, s.mgr_n);
    }
}

inline bool read_managed_string(ov::Data& d, uintptr_t str, std::string& out)
{
    int32_t len = 0;
    if (!d.rd(str + 0x10, len) || len < 0 || len > 8192) return false;
    if (len == 0) { out = "(空字符串)"; return true; }
    std::vector<uint16_t> w((size_t)len);
    for (int i = 0; i < len; ++i)
        if (!d.rd(str + 0x14 + (uintptr_t)i * 2, w[(size_t)i])) return false;
    int n = WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)w.data(), len, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return false;
    out.resize((size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)w.data(), len, out.data(), n, nullptr, nullptr);
    return true;
}

inline std::string wrap_code(const char* code)
{
    std::string s;
    s += "local __out={}\n";
    s += "local __oldprint=print\n";
    s += "print=function(...) local t={} for i=1,select('#',...) do t[#t+1]=tostring(select(i,...)) end __out[#__out+1]=table.concat(t,'\\t') end\n";
    // 指令数硬上限：防止死循环/长循环把游戏主线程冻住；debug 不可用时降级为无限制。
    s += "local __steps=0\n";
    s += "local __hooked=false\n";
    s += "if type(debug)=='table' and type(debug.sethook)=='function' then\n";
    s += "  pcall(function() debug.sethook(function() __steps=__steps+1 if __steps>200000 then error('Lua 指令超限') end end,'',1000) __hooked=true end)\n";
    s += "end\n";
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

inline bool exec(ov::Data& d, Session& s, int sel, const char* code, std::string& out)
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
    if (!s.trusted[sel])
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

    const std::string src = wrap_code(code);
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
        if (!read_managed_string(d, item, out)) out = "(字符串读取失败)";
    }
    else
    {
        out = "(返回类型: " + d.name_ref(ik) + ")";
    }
    if (out.size() > 3900) out.resize(3900);
    return true;
}

} // namespace ovlua
