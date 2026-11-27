#pragma once
#include "ov_data.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

// 游戏自己的管理器注册表（无扫描）：
//   *(UA + 0x25A5B70) = 元数据/静态区基址
//   基址 + 0x3B070     = Dictionary<string, manager>
//   Dictionary + 0x18  = Entry[]；Entry 步长 0x18：+8 = key(String)，+0x10 = value
namespace ovr {

inline uintptr_t RegistryInstance(ov::Data& d, const char* want)
{
    if (!d.ready_ok() || !want) return 0;
    struct Cached { uintptr_t val = 0; uint64_t t = 0; };
    static std::unordered_map<std::string, Cached> cache;
    const uint64_t now = GetTickCount64();
    auto ci = cache.find(want);
    if (ci != cache.end() && now - ci->second.t < 1000) return ci->second.val;

    uintptr_t found = 0;
    uintptr_t meta = 0, dict = 0;
    if (d.rd(d.ua + 0x25A5B70u, meta) && meta &&
        d.rd(meta + 0x3B070u, dict) && dict) {
        uintptr_t k = 0;
        char cn[64] = {};
        if (d.rd(dict, k) && k && d.copy_name(k, cn, sizeof cn) &&
            std::strcmp(cn, "Dictionary`2") == 0) {
            uintptr_t entries = 0;
            int32_t n = 0;
            if (d.rd(dict + 0x18, entries) && entries &&
                d.rd(entries + 0x18, n) && n > 0 && n <= 4096) {
                const uintptr_t data = entries + 0x20;
                for (int32_t i = 0; i < n; ++i) {
                    const uintptr_t e = data + (uintptr_t)i * 0x18;
                    uintptr_t key = 0;
                    if (!d.rd(e + 8, key) || !key) continue;
                    int32_t len = 0;
                    if (!d.rd(key + 0x10, len) || len <= 0 || len > 96) continue;
                    char tmp[104] = {};
                    bool ok = true;
                    for (int32_t j = 0; j < len; ++j) {
                        uint16_t ch = 0;
                        if (!d.rd(key + 0x14 + (uintptr_t)j * 2, ch) ||
                            ch < 0x20 || ch > 0x7E) { ok = false; break; }
                        tmp[j] = (char)ch;
                    }
                    if (!ok || std::strcmp(tmp, want) != 0) continue;
                    uintptr_t val = 0;
                    if (d.rd(e + 0x10, val)) found = val;
                    break;
                }
            }
        }
    }
    cache[want] = { found, now };
    return found;
}

// 从注册表 key（都是托管 String）取 String klass，避免 FindKlass("String")。
inline uintptr_t RegistryStringKlass(ov::Data& d)
{
    uintptr_t meta = 0, dict = 0;
    if (!d.rd(d.ua + 0x25A5B70u, meta) || !meta) return 0;
    if (!d.rd(meta + 0x3B070u, dict) || !dict) return 0;
    uintptr_t entries = 0;
    int32_t n = 0;
    if (!d.rd(dict + 0x18, entries) || !entries) return 0;
    if (!d.rd(entries + 0x18, n) || n <= 0 || n > 4096) return 0;
    for (int32_t i = 0; i < n; ++i) {
        uintptr_t key = 0, k = 0;
        if (!d.rd(entries + 0x20 + (uintptr_t)i * 0x18 + 8, key) || !key) continue;
        if (d.rd(key, k) && k) return k;
    }
    return 0;
}

} // namespace ovr
