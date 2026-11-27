#pragma once
#include "../sdk/bh3_sdk.h"
#include "ov_profile.h"
#include "ov_resolve_live.h"
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <unordered_map>

namespace ov {

class Data {
public:
    uintptr_t ua = 0;
    uintptr_t str_klass = 0;
    int count = 0;
    uint64_t single_t = 0;
    char last_error[160] = "";
    bh3::Runtime rt;
    std::unordered_map<std::string, uintptr_t> clsmap;
    std::unordered_map<std::string, uintptr_t> instmap;

    uintptr_t find(const char* n) const {
        auto it = clsmap.find(n);
        return it == clsmap.end() ? 0 : it->second;
    }

    bool init();
    bool ready_ok() const { return ua != 0; }
    // 管理器实例是进关卡时才创建的，启动时可能都还是 null，需要定期补建。
    void refresh_singletons();

    const std::string& name_ref(uintptr_t klass) const {
        static const std::string kUnknown = "?";
        if (!klass) return kUnknown;
        auto it = name_cache.find(klass);
        if (it != name_cache.end()) return it->second;
        if (name_cache.size() > 8192) name_cache.clear();
        char name[192] = {};
        copy_name(klass, name, sizeof name);
        auto r = name_cache.emplace(klass, name[0] ? name : "?").first;
        return r->second;
    }

    const char* name_of(uintptr_t klass) const {
        if (!klass) return "?";
        return name_ref(klass).c_str();
    }

    struct Seg { uintptr_t lo = 0; uintptr_t hi = 0; };
    static constexpr int kSegSlots = 64;

    static Seg* seg_slots() {
        static thread_local Seg s[kSegSlots];
        return s;
    }

    static int& seg_turn() {
        static thread_local int t = 0;
        return t;
    }

    static void clear_seg_cache() {
        Seg* s = seg_slots();
        for (int i = 0; i < kSegSlots; ++i) s[i] = Seg();
        seg_turn() = 0;
    }

    static bool readable(uintptr_t a, size_t n) {
        if (!a) return false;
        if (!n) return true;
        const uintptr_t end = a + n;
        if (end < a) return false;

        Seg* seg = seg_slots();
        for (int i = 0; i < kSegSlots; ++i) {
            const Seg& s = seg[i];
            if (s.lo && a >= s.lo && end <= s.hi) return true;
        }

        MEMORY_BASIC_INFORMATION mbi;
        memset(&mbi, 0, sizeof mbi);
        if (VirtualQuery((void*)a, &mbi, sizeof mbi) != sizeof mbi) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        const uintptr_t lo = (uintptr_t)mbi.BaseAddress;
        const uintptr_t hi = lo + (uintptr_t)mbi.RegionSize;
        if (end > hi) return false;
        Seg& slot = seg[seg_turn()];
        slot.lo = lo;
        slot.hi = hi;
        seg_turn() = (seg_turn() + 1) % kSegSlots;
        return true;
    }

    template <class T>
    bool rd(uintptr_t a, T& out) const {
        if (!readable(a, sizeof(T))) return false;
        // VirtualQuery/cache results can go stale during scene teardown.
        // Let the kernel validate and copy; never dereference a cached page.
        T value{};
        SIZE_T copied = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)a, &value,
                               sizeof value, &copied) || copied != sizeof value) return false;
        memcpy(&out, &value, sizeof value);
        return true;
    }

    bool rd_u64(uintptr_t a, uintptr_t& out) const {
        uint64_t v = 0;
        if (!rd<uint64_t>(a, v)) return false;
        out = (uintptr_t)v;
        return true;
    }

    bool rd_f32(uintptr_t a, float& out) const { return rd<float>(a, out); }

    bool copy_name(uintptr_t klass, char* out, size_t cap) const {
        if (!out || !cap) return false;
        out[0] = 0;
        uintptr_t name = 0;
        if (!klass || !rd(klass + 0x58, name) || !name) return false;
        for (size_t i = 0; i + 1 < cap; ++i) {
            unsigned char ch = 0;
            if (!rd(name + i, ch) || (ch && (ch < 32 || ch > 126))) {
                out[0] = 0;
                return false;
            }
            out[i] = (char)ch;
            out[i + 1] = 0;
            if (!ch) return i != 0;
        }
        out[0] = 0;
        return false;
    }

    bool read_il2str(uintptr_t obj, uintptr_t off, char* out, size_t cap) const {
        if (!out || cap < 2) return false;
        out[0] = 0;
        uintptr_t str = 0;
        if (!obj || !rd(obj + off, str) || !str) return false;
        int32_t len = 0;
        if (!rd(str + 0x10, len) || len <= 0 || len > 96) return false;
        size_t n = (size_t)len;
        if (n >= cap) n = cap - 1;
        for (size_t i = 0; i < n; ++i) {
            uint16_t ch = 0;
            if (!rd(str + 0x14 + i * 2, ch)) { out[0] = 0; return false; }
            if (ch < 0x20 || ch > 0x7E) { out[0] = 0; return false; }
            out[i] = (char)ch;
        }
        out[n] = 0;
        return n != 0;
    }

    bool build_index();

private:
    mutable std::unordered_map<uintptr_t, std::string> name_cache;
};

inline bool Data::init() {
    if (ua) return true;
    if (!rt.attach()) { snprintf(last_error, sizeof last_error, "UserAssembly not loaded"); return false; }
    // 先校验 profile：版本不匹配时不读取任何游戏数据，避免用到过期偏移。
    if (!ovp::Init(rt.base())) { snprintf(last_error, sizeof last_error, "%s", ovp::Error()); return false; }
    ua = rt.base();
    ovl::Start(ua);
    // 类名索引在后台线程建；没建好就下一帧再来，绝不阻塞游戏线程。
    if (!ovl::Ready()) {
        snprintf(last_error, sizeof last_error, "building class index");
        ua = 0;
        return false;
    }
    // 管理器实例由管理器注册表按全名直接取，不再扫描 Singleton 表或类名堆。
    build_index();
    last_error[0] = 0;
    return true;
}

inline bool Data::build_index() {
    clsmap.clear();
    instmap.clear();
    name_cache.clear();
    str_klass = 0;
    count = 0;
    return true;
}

inline void Data::refresh_singletons() {}

}
