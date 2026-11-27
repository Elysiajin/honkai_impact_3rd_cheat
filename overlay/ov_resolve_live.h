#pragma once

#include "ov_resolve.h"
#include "ov_profile.h"
#include "bh3_names.inc"
#include "bh3_sigs.inc"

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>

namespace ovl
{

struct LiveStats
{
    int  state = 0;
    int  found = 0;
    int  resolved = 0;
    int  mismatch = 0;
    uint64_t scanned = 0;
    DWORD  ms = 0;
    char   note[192] = "未开始";
};

inline LiveStats& Stats()
{
    static LiveStats s;
    return s;
}

inline uint32_t (&LiveRvas())[ovp::SymCount]
{
    static uint32_t v[ovp::SymCount] = { 0 };
    return v;
}

namespace detail
{

inline bool& ReadyFlag()
{
    static bool b = false;
    return b;
}

inline HANDLE& ThreadHandle()
{
    static HANDLE h = nullptr;
    return h;
}

inline uintptr_t& BaseRef()
{
    static uintptr_t b = 0;
    return b;
}

inline bool LiveRead(void*, uintptr_t a, void* dst, size_t n)
{
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)a, dst, n, &got)) return false;
    return got == n;
}

inline int LiveRegions(void*, ovr::Region* out, int cap)
{
    int n = 0;
    uintptr_t a = 0;
    MEMORY_BASIC_INFORMATION mbi;
    while (n < cap)
    {
        if (VirtualQuery((LPCVOID)a, &mbi, sizeof mbi) != sizeof mbi) break;
        const uintptr_t base = (uintptr_t)mbi.BaseAddress;
        const size_t size = (size_t)mbi.RegionSize;
        const uintptr_t next = base + size;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
            !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && size >= 0x100000)
        {
            out[n].base = base;
            out[n].size = size;
            out[n].index = (size_t)n;
            ++n;
        }
        if (next <= a) break;
        a = next;
    }
    return n;
}

} // namespace detail

inline uint32_t LiveRva(int sym)
{
    if (sym < 0 || sym >= ovp::SymCount) return 0;
    if (!detail::ReadyFlag()) return 0;
    return LiveRvas()[sym];
}

inline bool MaskHit(uintptr_t at, const ovp::GenSig& s)
{
    uint8_t got[32] = { 0 };
    if (!detail::LiveRead(nullptr, at, got, s.len)) return false;
    for (uint32_t i = 0; i < s.len; ++i)
        if (s.mask[i] && got[i] != s.bytes[i]) return false;
    return true;
}

struct MgrSlot
{
    uintptr_t klass = 0;
    uintptr_t obj = 0;
    DWORD     stamp = 0;
};

inline MgrSlot& MgrCache(int idx)
{
    static MgrSlot s[2];
    return s[idx];
}

struct MgrCtx
{
    uintptr_t off = 0;
    uintptr_t klass = 0;
};

inline bool MgrValid(uintptr_t obj, void* ctx)
{
    const MgrCtx* c = (const MgrCtx*)ctx;
    // 场景切换后旧管理器会被销毁/复用；先确认对象头的 klass 仍是目标类。
    if (c->klass)
    {
        uintptr_t k = 0;
        if (!detail::LiveRead(nullptr, obj, &k, sizeof k) || k != c->klass) return false;
    }
    const uintptr_t off = c->off;
    uintptr_t list = 0;
    if (!detail::LiveRead(nullptr, obj + off, &list, sizeof list) || !list) return false;
    // 空列表也要算有效：还没进关卡时 items 就是 0，否则管理器永远认不出来。
    uintptr_t items = 0;
    int32_t size = 0;
    if (!detail::LiveRead(nullptr, list + 0x10, &items, sizeof items)) return false;
    if (!detail::LiveRead(nullptr, list + 0x18, &size, sizeof size)) return false;
    if (size < 0 || size > 4096) return false;
    if (size > 0 && !items) return false;
    return true;
}

inline uintptr_t ResolveManager(int idx, const char* name, uintptr_t list_off)
{
    if (!name || !name[0]) return 0;
    MgrSlot& c = MgrCache(idx);
    if (!c.klass)
    {
        c.klass = ovr::FindKlass(name);
        if (!c.klass) return 0;
    }
    MgrCtx vc;
    vc.off = list_off;
    vc.klass = c.klass;
    if (c.obj)
    {
        if (MgrValid(c.obj, &vc)) return c.obj;
        c.obj = 0;
    }
    const DWORD now = GetTickCount();
    if (c.stamp && now - c.stamp < 2000) return 0;
    c.stamp = now;

    const uintptr_t obj = ovr::FindInstance(c.klass, &MgrValid, &vc, 192ULL << 20);
    if (!obj) return 0;
    c.obj = obj;
    return obj;
}

inline uintptr_t AvatarManager()
{
    // 本构建 AvatarManager 的角色列表在 +0x40（+0x50 已不存在）。
    return ResolveManager(0, "AvatarManager", 0x40);
}

inline uintptr_t MonsterManager()
{
    return ResolveManager(1, "MonsterManager", 0x18);
}

inline uintptr_t ScanSigRange(uintptr_t lo, uintptr_t hi, const ovp::GenSig& s)
{
    if (s.len < 8 || s.len > 32 || hi <= lo) return 0;
    size_t anchor = 0;
    while (anchor < s.len && !s.mask[anchor]) ++anchor;
    if (anchor >= s.len) return 0;
    const size_t chunk = 1u << 22;
    std::vector<uint8_t> buf;
    buf.resize(chunk + s.len);
    uintptr_t at = lo;
    while (at < hi)
    {
        size_t n = (size_t)(hi - at);
        if (n > chunk) n = chunk;
        if (!detail::LiveRead(nullptr, at, buf.data(), n)) { at += n; continue; }
        if (n < s.len) break;
        const size_t end = n - s.len + 1;
        const uint8_t key = s.bytes[anchor];
        for (size_t i = 0; i < end; ++i)
        {
            if (buf[i + anchor] != key) continue;
            bool ok = true;
            for (uint32_t j = 0; j < s.len; ++j)
            {
                if (!s.mask[j]) continue;
                if (buf[i + j] != s.bytes[j]) { ok = false; break; }
            }
            if (ok) return at + i;
        }
        at += n;
    }
    return 0;
}

inline bool Build()
{
    const uintptr_t ua = detail::BaseRef();
    if (!ua) return false;

    DWORD t0 = GetTickCount();
    uint32_t (&live)[ovp::SymCount] = LiveRvas();
    int resolved = 0, mismatch = 0;
    for (int i = 0; i < ovp::SymCount; ++i)
    {
        live[i] = ovp::StaticRva((ovp::Sym)i);
        if (!live[i]) continue;
        ++resolved;
        if (ovp::Raw((ovp::Sym)i) && ovp::Raw((ovp::Sym)i) != live[i]) ++mismatch;
    }

    LiveStats& s = Stats();
    s.ms = GetTickCount() - t0;
    s.found = 0;
    s.resolved = resolved;
    s.mismatch = mismatch;
    s.scanned = 0;
    std::snprintf(s.note, sizeof s.note, "SDK 静态 RVA %d 个（无扫描）", resolved);
    s.state = (resolved > 0) ? 2 : 1;
    detail::ReadyFlag() = (resolved > 0);
    return resolved > 0;
}

inline DWORD WINAPI ThreadProc(LPVOID)
{
    Stats().state = 1;
    std::snprintf(Stats().note, sizeof Stats().note, "扫描中");
    Build();
    return 0;
}

// Singleton`1 的收集单独走一次性后台线程：它要扫完所有元数据区段，
// 放在 Build() 里会把注入后的第一帧卡住好几秒。管理器缺实例时才会触发。
inline std::atomic<int>& SingleState()
{
    static std::atomic<int> v(0);   // 0=未开始 1=进行中 2=完成
    return v;
}

inline DWORD WINAPI SingleProc(LPVOID)
{
    ovr::CollectSingletons();
    SingleState().store(2);
    return 0;
}

inline void EnsureSingletonScan()
{
    int expect = 0;
    if (!SingleState().compare_exchange_strong(expect, 1)) return;
    HANDLE t = CreateThread(nullptr, 1u << 20, &SingleProc, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
    else SingleState().store(0);
}

inline int SingletonCount() { return (int)ovr::SingletonCandidates().size(); }

inline bool Start(uintptr_t ua)
{
    if (detail::ThreadHandle()) return true;
    if (!ua) return false;
    detail::BaseRef() = ua;

    ovr::Hooks h;
    h.read = &detail::LiveRead;
    h.read_ctx = nullptr;
    h.regions = &detail::LiveRegions;
    h.regions_ctx = nullptr;
    ovr::SetHooks(h);
    const int nr = ovr::LoadRegions();
    if (!nr)
    {
        std::snprintf(Stats().note, sizeof Stats().note, "没有可扫描的私有堆区");
        Stats().state = 1;
        return false;
    }
    std::snprintf(Stats().note, sizeof Stats().note, "枚举 %d 个堆区，等待扫描", nr);

    ovp::SetFallback(&LiveRva);
    HANDLE t = CreateThread(nullptr, 1u << 20, &ThreadProc, nullptr, 0, nullptr);
    if (!t) return false;
    detail::ThreadHandle() = t;
    return true;
}

inline bool Ready() { return detail::ReadyFlag(); }
inline int  State() { return Stats().state; }
inline const char* Note() { return Stats().note; }

inline bool WaitReady(DWORD ms)
{
    if (detail::ReadyFlag()) return true;
    const HANDLE t = detail::ThreadHandle();
    if (!t) return false;
    return WaitForSingleObject(t, ms) == WAIT_OBJECT_0;
}

} // namespace ovl
