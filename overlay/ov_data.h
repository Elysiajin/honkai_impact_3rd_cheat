#pragma once
#include "../sdk/bh3_sdk.h"
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
    char last_error[160] = "";
    bh3::Runtime rt;
    std::unordered_map<std::string, uintptr_t> clsmap;

    uintptr_t find(const char* n) const {
        auto it = clsmap.find(n);
        return it == clsmap.end() ? 0 : it->second;
    }

    bool init();
    bool ready_ok() const { return ua != 0; }

    const char* name_of(uintptr_t klass) const {
        const char* n = klass ? rt.class_name(klass) : nullptr;
        return n ? n : "?";
    }

    static bool readable(uintptr_t a, size_t n) {
        if (!a) return false;
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery((void*)a, &mbi, sizeof mbi) != sizeof mbi) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        size_t avail = (size_t)((uintptr_t)mbi.BaseAddress + mbi.RegionSize - a);
        return n <= avail;
    }

    template <class T>
    bool rd(uintptr_t a, T& out) const {
        if (!readable(a, sizeof(T))) return false;
        memcpy(&out, (void*)a, sizeof(T));
        return true;
    }

    bool rd_u64(uintptr_t a, uintptr_t& out) const {
        uint64_t v = 0;
        if (!rd<uint64_t>(a, v)) return false;
        out = (uintptr_t)v;
        return true;
    }

    bool rd_f32(uintptr_t a, float& out) const { return rd<float>(a, out); }

    bool build_index();
};

inline bool Data::init() {
    if (ua) return true;
    if (!rt.attach()) { snprintf(last_error, sizeof last_error, "UserAssembly not loaded"); return false; }
    if (!rt.ready()) { snprintf(last_error, sizeof last_error, "il2cpp not ready"); return false; }
    ua = rt.base();
    if (!build_index()) { snprintf(last_error, sizeof last_error, "class table scan failed"); ua = 0; return false; }
    return true;
}

inline bool Data::build_index() {
    std::vector<std::vector<uintptr_t>> all = rt.all_class_arrays(4);
    int n = 0;
    str_klass = 0;
    bool have_a = false, have_m = false;
    for (size_t i = 0; i < all.size(); ++i) {
        for (size_t j = 0; j < all[i].size(); ++j) {
            uintptr_t k = all[i][j];
            ++n;
            if (!readable(k, 0x40)) continue;
            if (have_a && have_m && str_klass) continue;
            const char* nm = rt.class_name(k);
            if (!nm) continue;
            if (!str_klass && std::strcmp(nm, "String") == 0) str_klass = k;
            if (std::strcmp(nm, "AvatarManager") == 0) { clsmap[nm] = k; have_a = true; }
            else if (std::strcmp(nm, "MonsterManager") == 0) { clsmap[nm] = k; have_m = true; }
        }
    }
    count = n;
    return n > 0;
}

}