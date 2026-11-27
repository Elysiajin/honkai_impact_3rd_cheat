#pragma once

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
#include <unordered_set>

#include <windows.h>

namespace ovr
{

struct Region
{
    uintptr_t base = 0;
    size_t size = 0;
    size_t index = 0;
};

using ReadFn = bool (*)(void* ctx, uintptr_t addr, void* dst, size_t n);
using RegionsFn = int (*)(void* ctx, Region* out, int cap);

struct Hooks
{
    ReadFn read = nullptr;
    void* read_ctx = nullptr;
    RegionsFn regions = nullptr;
    void* regions_ctx = nullptr;
};

struct Stats
{
    uint64_t candidates = 0;
    uint64_t scanned_bytes = 0;
    int valid_klasses = 0;
};

namespace detail
{

inline Hooks& HooksRef()
{
    static Hooks h;
    return h;
}

inline std::vector<Region>& RegionsRef()
{
    static std::vector<Region> r;
    return r;
}

inline std::vector<std::pair<std::string, uintptr_t> >& IndexRef()
{
    static std::vector<std::pair<std::string, uintptr_t> > v;
    return v;
}

// 所有 Singleton`1 泛型类的 klass。klass 地址每次启动都会重定位，
// 不能写死；只能靠这里扫出来，再由调用方读各自的静态区拿实例。
inline std::vector<uintptr_t>& SingletonsRef()
{
    static std::vector<uintptr_t> v;
    return v;
}

inline Stats& StatsRef()
{
    static Stats s;
    return s;
}

inline bool Rd(uintptr_t a, void* dst, size_t n)
{
    const Hooks& h = HooksRef();
    return h.read ? h.read(h.read_ctx, a, dst, n) : false;
}

inline bool Rq(uintptr_t a, uintptr_t& out) { return Rd(a, &out, sizeof out); }
inline bool R32(uintptr_t a, uint32_t& out) { return Rd(a, &out, sizeof out); }
inline bool R8(uintptr_t a, uint8_t& out) { return Rd(a, &out, 1); }

inline const Region* FindRegion(uintptr_t a)
{
    const std::vector<Region>& v = RegionsRef();
    size_t lo = 0, hi = v.size();
    while (lo < hi)
    {
        const size_t mid = (lo + hi) / 2;
        if (a < v[mid].base) hi = mid;
        else if (a >= v[mid].base + v[mid].size) lo = mid + 1;
        else return &v[mid];
    }
    return nullptr;
}

inline bool InRegions(uintptr_t a) { return FindRegion(a) != nullptr; }

inline bool CStrAt(uintptr_t a, char* dst, size_t cap)
{
    if (!cap) return false;
    size_t i = 0;
    while (i + 1 < cap)
    {
        uint8_t c = 0;
        if (!R8(a + i, c)) return false;
        if (c == 0)
        {
            if (i == 0) return false;
            dst[i] = 0;
            return true;
        }
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '`' || c == '.' ||
                        c == '<' || c == '>' || c == '$' || c == '@' || c == '+' ||
                        c == '-' || c == '[' || c == ']';
        if (!ok) return false;
        dst[i] = (char)c;
        ++i;
    }
    return false;
}

} // namespace detail

inline void SetHooks(const Hooks& h) { detail::HooksRef() = h; }

inline int LoadRegions()
{
    std::vector<Region>& v = detail::RegionsRef();
    v.clear();
    static Region buf[8192];
    const Hooks& h = detail::HooksRef();
    const int n = h.regions ? h.regions(h.regions_ctx, buf, 8192) : 0;
    for (int i = 0; i < n; ++i)
    {
        if (!buf[i].size) continue;
        Region r = buf[i];
        r.index = v.size();
        v.push_back(r);
    }
    for (size_t i = 0; i + 1 < v.size(); ++i)
        for (size_t j = 0; j + 1 < v.size() - i; ++j)
            if (v[j].base > v[j + 1].base)
            {
                Region t = v[j];
                v[j] = v[j + 1];
                v[j + 1] = t;
            }
    return (int)v.size();
}

inline const std::vector<Region>& Regions() { return detail::RegionsRef(); }
inline bool InAnyRegion(uintptr_t a) { return detail::InRegions(a); }
inline bool ReadAt(uintptr_t a, void* dst, size_t n) { return detail::Rd(a, dst, n); }
inline uint64_t ScannedBytes() { return detail::StatsRef().scanned_bytes; }
inline int ValidKlasses() { return detail::StatsRef().valid_klasses; }
inline int IndexCount() { return (int)detail::IndexRef().size(); }
inline void ResetIndex() { detail::IndexRef().clear(); detail::StatsRef() = Stats(); }

inline bool IsKlass(uintptr_t k, char* name_out, size_t cap)
{
    uintptr_t namep = 0, mtab = 0;
    if (!detail::Rq(k + 0x58, namep) || !detail::InRegions(namep)) return false;
    if (!detail::Rq(k + 0x60, mtab) || !detail::InRegions(mtab)) return false;
    return detail::CStrAt(namep, name_out, cap);
}

inline int BuildIndexFor(const char* const* names, int count, size_t chunk = 1u << 18,
                         size_t max_region = 96u << 20, size_t max_scan = 48u << 20)
{
    std::vector<std::pair<std::string, uintptr_t> >& idx = detail::IndexRef();
    idx.clear();
    detail::StatsRef() = Stats();
    if (count <= 0) return 0;

    // klass 结构和类名字符串都在几十 MB 的元数据区里，GB 级 GC 堆里不会有。
    // 只扫小区域并按从小到大，命中即停，避免注入时扫掉几百 MB。
    std::vector<size_t> order;
    const std::vector<Region>& rs = detail::RegionsRef();
    for (size_t i = 0; i < rs.size(); ++i)
        if (rs[i].size >= 0x10000 && rs[i].size <= max_region) order.push_back(i);
    for (size_t i = 0; i + 1 < order.size(); ++i)
        for (size_t j = 0; j + 1 < order.size() - i; ++j)
            if (rs[order[j]].size > rs[order[j + 1]].size)
            {
                size_t t = order[j];
                order[j] = order[j + 1];
                order[j + 1] = t;
            }


    std::vector<uint8_t> buf;
    buf.resize(chunk);
    std::vector<char> found(count, 0);
    int nfound = 0;
    char name[96];

    for (size_t oi = 0; oi < order.size() && nfound < count; ++oi)
    {
        const Region& r = rs[order[oi]];
        size_t off = 0;
        while (off + 8 <= r.size)
        {
            size_t n = r.size - off;
            if (n > chunk) n = chunk;
            n &= ~(size_t)7;
            if (!n) break;
            if (!detail::Rd(r.base + off, buf.data(), n)) { off += n; continue; }
            detail::StatsRef().scanned_bytes += n;
            if (detail::StatsRef().scanned_bytes >= max_scan) return nfound;  // 预算到顶就收工
            const size_t cnt = n / 8;
            const uint64_t* q = reinterpret_cast<const uint64_t*>(buf.data());
            for (size_t i = 0; i < cnt; ++i)
            {
                const uintptr_t v = (uintptr_t)q[i];
                if (!detail::InRegions(v)) continue;
                ++detail::StatsRef().candidates;
                if (!IsKlass(v, name, sizeof name)) continue;
                ++detail::StatsRef().valid_klasses;
                for (int w = 0; w < count; ++w)
                {
                    if (found[w] || std::strcmp(name, names[w]) != 0) continue;
                    idx.push_back(std::make_pair(std::string(names[w]), v));
                    found[w] = 1;
                    ++nfound;
                    break;
                }
            }
            off += n;
        }
    }
    return nfound;
}

inline uintptr_t FindKlass(const char* name)
{
    if (!name) return 0;
    const std::vector<std::pair<std::string, uintptr_t> >& idx = detail::IndexRef();
    for (size_t i = 0; i < idx.size(); ++i)
        if (idx[i].first == name) return idx[i].second;
    return 0;
}

inline const std::vector<uintptr_t>& SingletonCandidates() { return detail::SingletonsRef(); }

// 扫出全部 Singleton`1 泛型 klass。只扫元数据量级的区段，跳过 GB 级 GC 堆。
inline int CollectSingletons(size_t chunk = 1u << 18, size_t max_region = 512u << 20)
{
    std::vector<uintptr_t>& out = detail::SingletonsRef();
    out.clear();

    std::vector<size_t> order;
    const std::vector<Region>& rs = detail::RegionsRef();
    for (size_t i = 0; i < rs.size(); ++i)
        if (rs[i].size >= 0x1000 && rs[i].size <= max_region) order.push_back(i);
    for (size_t i = 0; i + 1 < order.size(); ++i)
        for (size_t j = 0; j + 1 < order.size() - i; ++j)
            if (rs[order[j]].size < rs[order[j + 1]].size)
            {
                const size_t t = order[j];
                order[j] = order[j + 1];
                order[j + 1] = t;
            }

    std::vector<uint8_t> buf;
    buf.resize(chunk);
    std::unordered_set<uintptr_t> seen;
    char name[96];

    for (size_t oi = 0; oi < order.size(); ++oi)
    {
        const Region& r = rs[order[oi]];
        size_t off = 0;
        while (off + 8 <= r.size)
        {
            size_t n = r.size - off;
            if (n > chunk) n = chunk;
            n &= ~(size_t)7;
            if (!n) break;
            if (!detail::Rd(r.base + off, buf.data(), n)) { off += n; continue; }

            const size_t cnt = n / 8;
            const uint64_t* q = reinterpret_cast<const uint64_t*>(buf.data());
            for (size_t i = 0; i < cnt; ++i)
            {
                const uintptr_t v = (uintptr_t)q[i];
                if (!detail::InRegions(v)) continue;
                if (!IsKlass(v, name, sizeof name)) continue;
                if (std::strcmp(name, "Singleton`1") != 0) continue;
                // 同一个 klass 会被大量引用，必须插入前去重，
                // 否则候选表被重复项塞满后提前截断，后面的单例永远扫不到。
                if (!seen.insert(v).second) continue;
                if (out.size() >= 4096) return (int)out.size();
                out.push_back(v);
            }
            off += n;
        }
    }
    return (int)out.size();
}

struct Hit
{
    uintptr_t mi = 0;
    uintptr_t fn = 0;
    uint32_t token = 0;
};

inline bool ResolveMethod(uintptr_t klass, uint32_t token, Hit* out, int limit = 8192)
{
    if (!klass || !token) return false;
    uintptr_t tab = 0;
    if (!detail::Rq(klass + 0x60, tab) || !detail::InRegions(tab)) return false;

    for (int i = 0; i < limit; ++i)
    {
        uintptr_t p = 0;
        if (!detail::Rq(tab + (uintptr_t)i * 8, p) || !detail::InRegions(p)) break;
        const uintptr_t mi = p;
        uintptr_t k2 = 0;
        uint32_t tok = 0;
        if (!detail::Rq(mi + 0x08, k2)) break;
        if (!detail::R32(mi + 0x28, tok)) break;
        if (k2 != klass || tok != token) continue;
        uintptr_t fn = 0;
        if (!detail::Rq(mi + 0x00, fn) || !fn) return false;
        if (out) { out->mi = mi; out->fn = fn; out->token = tok; }
        return true;
    }
    return false;
}

inline uintptr_t ByName(const char* className, uint32_t token, Hit* out = nullptr)
{
    const uintptr_t k = FindKlass(className);
    if (!k) return 0;
    Hit h;
    if (!ResolveMethod(k, token, &h)) return 0;
    if (out) *out = h;
    return h.fn;
}

using HitFn = bool (*)(uintptr_t addr, uintptr_t value, void* ctx);
using ValidFn = bool (*)(uintptr_t obj, void* ctx);

struct InstCtx
{
    uintptr_t klass = 0;
    uintptr_t out = 0;
    ValidFn valid = nullptr;
    void* vctx = nullptr;
    int max = 1;
    int hits = 0;
};

inline bool InstProbe(uintptr_t at, uintptr_t, void* ctx)
{
    InstCtx* c = (InstCtx*)ctx;
    uintptr_t k = 0;
    if (!detail::Rq(at, k) || k != c->klass) return true;
    if (c->valid && !c->valid(at, c->vctx)) return true;
    c->out = at;
    ++c->hits;
    return c->hits < c->max;
}

inline int ScanQwords(uintptr_t target, HitFn cb, void* ctx, size_t chunk = 1u << 18,
                      uintptr_t near_va = 0, uint64_t budget = 0)
{
    std::vector<uint8_t> buf;
    buf.resize(chunk);
    int hits = 0;
    const std::vector<Region>& rs = detail::RegionsRef();

    std::vector<size_t> order;
    order.reserve(rs.size());
    for (size_t i = 0; i < rs.size(); ++i)
        if (rs[i].size >= 8 && rs[i].size <= (64u << 20)) order.push_back(i);
    for (size_t i = 0; i + 1 < order.size(); ++i)
        for (size_t j = 0; j + 1 < order.size() - i; ++j)
            if (rs[order[j]].size > rs[order[j + 1]].size)
            {
                const size_t t = order[j]; order[j] = order[j + 1]; order[j + 1] = t;
            }

    uint64_t used = 0;
    for (size_t oi = 0; oi < order.size(); ++oi)
    {
        const Region& r = rs[order[oi]];
        if (r.size < 8) continue;
        size_t off = 0;
        while (off + 8 <= r.size)
        {
            size_t n = r.size - off;
            if (n > chunk) n = chunk;
            n &= ~(size_t)7;
            if (!n) break;
            if (!detail::Rd(r.base + off, buf.data(), n)) { off += n; continue; }
            used += n;
            const size_t cnt = n / 8;
            const uint64_t* q = reinterpret_cast<const uint64_t*>(buf.data());
            for (size_t i = 0; i < cnt; ++i)
            {
                if ((uintptr_t)q[i] != target) continue;
                ++hits;
                if (cb && !cb(r.base + off + i * 8, target, ctx)) return hits;
            }
            off += n;
            if (budget && used >= budget) return hits;
        }
    }
    return hits;
}

inline uintptr_t FindInstance(uintptr_t klass, ValidFn valid = nullptr, void* vctx = nullptr,
                             uint64_t budget = 0)
{
    if (!klass) return 0;
    InstCtx c;
    c.klass = klass;
    c.valid = valid;
    c.vctx = vctx;
    ScanQwords(klass, &InstProbe, &c, 1u << 18, klass, budget);
    return c.out;
}

inline int ScanBytes(const uint8_t* needle, size_t nlen, HitFn cb, void* ctx,
                     size_t chunk = 1u << 22)
{
    if (!needle || nlen < 4) return 0;
    std::vector<uint8_t> buf;
    buf.resize(chunk + nlen);
    int hits = 0;
    const std::vector<Region>& rs = detail::RegionsRef();
    for (size_t ri = 0; ri < rs.size(); ++ri)
    {
        const Region& r = rs[ri];
        if (r.size < nlen) continue;
        size_t off = 0;
        while (off < r.size)
        {
            size_t n = r.size - off;
            if (n > chunk) n = chunk;
            if (!detail::Rd(r.base + off, buf.data(), n)) { off += n; continue; }
            const size_t end = n >= nlen ? n - nlen + 1 : 0;
            for (size_t i = 0; i < end; ++i)
            {
                if (buf[i] != needle[0]) continue;
                if (std::memcmp(buf.data() + i, needle, nlen) != 0) continue;
                ++hits;
                if (cb && !cb(r.base + off + i, 0, ctx)) return hits;
            }
            off += n;
        }
    }
    return hits;
}

inline int ScanMasked(const uint8_t* bytes, const uint8_t* mask, size_t n,
                      HitFn cb, void* ctx, size_t chunk = 1u << 22)
{
    if (!bytes || !mask || n < 8) return 0;
    size_t anchor = 0;
    while (anchor < n && !mask[anchor]) ++anchor;
    if (anchor >= n) return 0;
    std::vector<uint8_t> buf;
    buf.resize(chunk + n);
    int hits = 0;
    const std::vector<Region>& rs = detail::RegionsRef();
    for (size_t ri = 0; ri < rs.size(); ++ri)
    {
        const Region& r = rs[ri];
        if (r.size < n) continue;
        size_t off = 0;
        while (off < r.size)
        {
            size_t len = r.size - off;
            if (len > chunk) len = chunk;
            if (!detail::Rd(r.base + off, buf.data(), len)) { off += len; continue; }
            if (len < n) break;
            const size_t end = len - n + 1;
            const uint8_t key = bytes[anchor];
            for (size_t i = 0; i < end; ++i)
            {
                if (buf[i + anchor] != key) continue;
                bool ok = true;
                for (size_t j = 0; j < n; ++j)
                {
                    if (!mask[j]) continue;
                    if (buf[i + j] != bytes[j]) { ok = false; break; }
                }
                if (!ok) continue;
                ++hits;
                if (cb && !cb(r.base + off + i, 0, ctx)) return hits;
            }
            off += len;
        }
    }
    return hits;
}

} // namespace ovr
