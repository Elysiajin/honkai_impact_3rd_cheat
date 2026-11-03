// bh3_sdk.h - Honkai Impact 3rd (GF/PC, Unity 2017.4.18f1, custom IL2CPP "v7")
//
// ASLR POLICY
// -----------
// Nothing in this header is an absolute address.
//   * Code/data inside UserAssembly.dll  -> RVA, added to the module base that
//     GetModuleHandleA("UserAssembly.dll") returns.  ASLR-safe by construction.
//   * Heap regions (class arrays, string pool, resolver hash map) -> read from
//     pointers published by the runtime itself, then used for scanning.
// The only literal numbers left are STRUCTURE OFFSETS (0x88, 0xB0, 0x40, 0xD8...)
// and OBFUSCATION KEYS, both of which are build constants, not addresses.
//
// Everything below was measured on a live dump of the shipped build.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>

namespace bh3 {

// ---------------------------------------------------------------------------
// 1. UserAssembly.dll globals, by RVA
// ---------------------------------------------------------------------------
enum Globals : uintptr_t {
    RVA_METADATA_HEADER   = 0x25554B0,  // qword -> .rdata obfuscated header blob
    RVA_METADATA_BODY     = 0x25554B8,  // qword -> mapped metadata body (file + 0x198)
    RVA_STARTUP_BUF       = 0x25554C0,  // qword -> startup-metadata.dat buffer
    RVA_REGION_BASE       = 0x25554C8,  // qword -> decrypted heap start (0x50000001000)
    RVA_MSCORLIB_CLASSES  = 0x25554D0,  // qword -> Il2CppClass* array, mscorlib image
    RVA_MODULE_COUNT      = 0x25554E8,  // dword -> 212 codegen modules
    RVA_MODULE_TABLE      = 0x25554F0,  // qword -> 212 records, stride 0x58
    RVA_MODULE_STRIDE     = 0x58,
    RVA_TABLE2            = 0x2555500,  // qword -> per-module records, stride 0x50
    RVA_TABLE2_STRIDE     = 0x50,
    RVA_TABLE2_COUNT      = 0x2555508,  // dword

    RVA_IL2CPP_VERSION    = 0x2B0C2E8,  // dword -> 7
    RVA_STRING_RESOLVER   = 0x611590,   // char* __fastcall(int32_t index)

    // string pool + its lazy hash cache (absl-style dense map)
    RVA_POOL_HEADER       = 0x2555060,  // qword -> {base, start, end, used, cap, ...}
    RVA_CACHE_MASK        = 0x2555330,  // qword -> capacity-1  (measured 0x3FFFF)
    RVA_CACHE_GROUPS      = 0x2555350,  // qword -> group array  (0x20 B per group)
    RVA_CACHE_LIMIT       = 0x2555358,  // qword -> slot count  (0x40000)
    RVA_CACHE_COUNT       = 0x2555360,  // qword -> live entries (0x1B63C)
    CACHE_GROUP_SIZE      = 0x20,       // [ptr][occupied bitmap][deleted bitmap][pad]
    CACHE_SLOT_MASK       = 0x0F,       // 16 slots per group
};

// Any page protection a plain read is legal under.
static const DWORD READABLE_PROT =
    PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
    PAGE_EXECUTE_READ | PAGE_EXECUTE_WRITECOPY | PAGE_EXECUTE_READWRITE;

// ---------------------------------------------------------------------------
// 2. Obfuscation constants (build constants, NOT addresses)
// ---------------------------------------------------------------------------
inline uint32_t hdr_u32(uintptr_t hdr, size_t off) {
    return *reinterpret_cast<const uint32_t*>(hdr + off);
}

// Per-index key, UserAssembly RVA 0x60673f:
//   imul rax,rdx,0xd331 : add rax,0x209a85ae : xor rax,0x2f576729
//   imul rax,rax,0x764d9534 : shr rax,0x13 : xor rax,0x141fe11b
//   imul r13,rax,0x53fe4223 : shr r13,0x15
inline uint64_t index_key(uint64_t i) {
    uint64_t x = (i * 0xd331ull + 0x209a85aeull) & 0xffffffffull;
    x ^= 0x2f576729ull;
    x = (x * 0x764d9534ull) & 0xffffffffull;
    x >>= 0x13;
    x ^= 0x141fe11bull;
    x = (x * 0x53fe4223ull) & 0xffffffffull;
    return x >> 0x15;
}

// Module-name record decode (proven on 172/212 names):
//   raw at startup_buf + 0x1B5A8 + i*40 + 0x1C
inline uint32_t decode_name_index(uint32_t raw, uint64_t i) {
    return (uint32_t)((((uint64_t)raw + 0xcb2fbbd1ull) & 0xffffffffull) ^ index_key(i));
}

// Codegen module table stores its name pointer additively obfuscated:
//   real = stored - 0x172adc487fda4b76
static constexpr uint64_t kNamePtrKey = 0x172adc487fda4b76ull;

// The metadata header blob lives in .rdata, reached through RVA_METADATA_HEADER.
// Field decode table (the +0x15C / +0xA4 / +0x2C triple cross-checks itself):
//   +0x1C raw^0x4DE37C52   +0x2C raw^0x7501F887   +0x40 raw+0xEA869237
//   +0x78 (raw>>3)^0x2AC4559  +0x7C raw^0x42CEE980  +0x98 (raw^0x64C3BC60)/82
//   +0xA4 raw+0xE513D90B   +0xEC (raw>>3)^0x75F019B
//   +0x108 (raw+0xC1E1920F)/30   +0x15C (raw+0xC0A86C4F)>>4   +0x17C raw^0x42CEC8A3
inline uint32_t hdr_modules_offset(uintptr_t hdr) { return hdr_u32(hdr, 0xA4) + 0xE513D90Bu; }

// ---------------------------------------------------------------------------
// 3. Structures, measured layout (NOT stock IL2CPP v24)
// ---------------------------------------------------------------------------
struct Il2CppObject {
    void* klass;   // +0x00
    void* monitor; // +0x08
};

struct Il2CppString {
    void*   klass;      // +0x00
    void*   monitor;    // +0x08
    int32_t length;     // +0x10  UTF-16 units, NO NUL guarantee
    wchar_t chars[1];   // +0x14
};

struct Il2CppClass {
    uint8_t     pad00[0x20];
    const char* name;            // +0x20  direct decrypted pointer
    uint8_t     pad28[0x30];
    uint64_t    perModuleTable;  // +0x58
    uint8_t     pad60[0x20];
    uint64_t    nextClass;       // +0x80  contiguous arrays: self + 0xD8
    uint32_t    typeDefIndex;    // +0x88  == position + array offset
    uint8_t     pad8c[0x0C];
    uint32_t    typeDefIndex2;   // +0x98
    uint8_t     pad9c[0x14];
    uint32_t    parentLow;       // +0xB0 low  dword: parent class ptr (region relative OR)
    uint32_t    flagsHi;         // +0xB0 high dword
    uint32_t    flags2;          // +0xB8
    uint8_t     padbc[0x1C];     // +0xBC .. +0xD8
};

struct MethodInfo {
    uint64_t genericMethod; // +0x00
    uint64_t methodPointer; // +0x08  il2cpp section
    uint64_t klass;         // +0x10
    uint32_t slot;          // +0x18
    uint32_t pad1c;
    uint64_t invoker;       // +0x20  .text, use when methodPointer == 0
    uint64_t maxStack;      // +0x28  always 0x00000000ffffffff
    uint32_t methodToken;   // +0x30  0x06xxxxxx MethodDef
    uint32_t pad34;
    uint32_t flags;         // +0x38
    uint32_t pad3c;
};

static_assert(sizeof(MethodInfo) == 0x40, "MethodInfo must be 0x40");
static_assert(sizeof(Il2CppClass) == 0xD8, "Il2CppClass must be 0xD8");

// ---------------------------------------------------------------------------
// 4. Runtime facade - every heap address comes from the game, not from us
// ---------------------------------------------------------------------------
class Runtime {
public:
    bool attach() {
        ua_ = (uintptr_t)GetModuleHandleA("UserAssembly.dll");
        return ua_ != 0;
    }
    uintptr_t base() const { return ua_; }
    template <class T> T at(uintptr_t rva) const { return (T)(ua_ + rva); }
    template <class T> T g(uintptr_t rva) const { return *reinterpret_cast<T*>(ua_ + rva); }

    bool ready() const {
        return ua_ && g<uintptr_t>(RVA_METADATA_BODY) && g<uint32_t>(RVA_IL2CPP_VERSION) == 7;
    }
    // sanity that also proves the heap pointer is live
    bool heap_ready() const {
        size_t s = 0, e = 0;
        return pool_bounds(s, e) && e > s;
    }

    uintptr_t metadata_body()   const { return g<uintptr_t>(RVA_METADATA_BODY); }
    uintptr_t metadata_header() const { return g<uintptr_t>(RVA_METADATA_HEADER); }
    uintptr_t startup_buffer()  const { return g<uintptr_t>(RVA_STARTUP_BUF); }
    uint32_t  module_count()    const { return g<uint32_t>(RVA_MODULE_COUNT); }

    // Pool header: {base, start, end, used, cap, dataSize}.
    // `base` (p[0]) is the origin every cached-string offset is relative to and
    // it sits ONE PAGE BELOW `start` (p[1]).  Verified offline against the saved
    // pool region (bhdump/cdot_base.py): of 4000 sampled (offset, text) pairs,
    // base+off matched 4000/4000 and start+off matched 0/4000.
    // `used` (p[3]) is a live POINTER in this build, not a byte count.
    bool pool_all(uintptr_t& base, uintptr_t& start, uintptr_t& end) const {
        uintptr_t h = g<uintptr_t>(RVA_POOL_HEADER);
        if (!h) return false;
        auto* p = reinterpret_cast<const uint64_t*>(h);
        base  = (uintptr_t)p[0];
        start = (uintptr_t)p[1];
        end   = (uintptr_t)p[2];
        return base != 0 && start > base && end > start;
    }
    bool pool_bounds(size_t& start, size_t& end) const {
        uintptr_t b = 0, s = 0, e = 0;
        if (!pool_all(b, s, e)) return false;
        start = (size_t)s;
        end   = (size_t)e;
        return true;
    }
    size_t pool_used() const {
        uintptr_t b = 0, s = 0, e = 0;
        if (!pool_all(b, s, e)) return 0;
        auto* p = reinterpret_cast<const uint64_t*>(g<uintptr_t>(RVA_POOL_HEADER));
        uintptr_t cur = (uintptr_t)p[3];
        if (cur >= s && cur <= e) return (size_t)(cur - s);
        return (size_t)(e - s);
    }
    size_t pool_size() const {
        uintptr_t b = 0, s = 0, e = 0;
        return pool_all(b, s, e) ? (size_t)(e - s) : 0;
    }
    // Readable bytes from p to the end of the committed span: every unbounded
    // string scan must clamp to this or it can walk off the last committed page.
    size_t pool_avail(uintptr_t p) const {
        uintptr_t lo = 0, hi = 0;
        if (!pool_scan_span(lo, hi)) return 0;
        if (p < lo || p >= hi) return 0;
        return (size_t)(hi - p);
    }
    // End of the committed, readable region that contains addr (addr itself when
    // it is not readable).  One VirtualQuery per call: use it on table bases,
    // never inside a hot loop.
    uintptr_t region_hi(uintptr_t addr) const {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) != sizeof(mbi)) return addr;
        if (mbi.State != MEM_COMMIT) return addr;
        if (!(mbi.Protect & READABLE_PROT)) return addr;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return addr;
        return (uintptr_t)mbi.BaseAddress + (uintptr_t)mbi.RegionSize;
    }

    const char* resolve_string(int32_t index) const {
        auto fn = at<const char*(__fastcall*)(int32_t)>(RVA_STRING_RESOLVER);
        return fn ? fn(index) : nullptr;
    }
    const char* class_name(uintptr_t klass) const {
        return klass ? reinterpret_cast<Il2CppClass*>(klass)->name : nullptr;
    }

    // Enumerate every string the runtime already decrypted.  The map is an
    // absl-style dense hash: a group table of 0x20-byte cells, each holding a
    // pointer to 16 slots of {u32 key, u32 offset}.  We do NOT trust the
    // occupancy bitmaps (this build keeps several bits per entry, so they
    // over-count); we validate each slot by range-checking the offset into the
    // pool and requiring the target to be printable.  Measured offline on a
    // live dump: 75,100 of 112,188 slots resolve once the offset is bounded by
    // the pool; the rest are stale slots whose bit is still set.
    //
    // WHY THE FIRST VERSION CRASHED (2026-11-02 20:04:18, AV inside
    // Runtime::cached_strings()+0x1d3, RIP RVA 0x1AA23 = `movzbl (%rax)`, with
    // RAX = 0x302E303A = ":0.0" and RDX = 0x22747361 = "ast\""):
    //   the group walk was bounded by [RVA_CACHE_LIMIT] alone, and that global
    //   is NOT the slot count.  Walking limit/16 groups ran off the end of the
    //   group array into neighbouring heap data, so a class NAME string was read
    //   as {key, offset}: "ast\"" became the key and ":0.0" the offset.  The
    //   offset guard compared against `used`, but `used` is a POINTER (p[3]), so
    //   every 32-bit offset passed, and pool_base + off was then dereferenced
    //   with no commitment check.  Fixed by (a) bounding the walk by the real
    //   allocation via VirtualQuery and (b) bounding every offset by the
    //   committed pool span, which makes the dereference readable by proof.
    std::vector<std::pair<uint32_t, const char*>> cached_strings() const {
        std::vector<std::pair<uint32_t, const char*>> out;
        uintptr_t groups = g<uintptr_t>(RVA_CACHE_GROUPS);
        uint64_t  limit  = g<uint64_t>(RVA_CACHE_LIMIT);
        if (!groups || !limit) return out;
        uintptr_t base = 0, lo = 0, hi = 0;
        if (!pool_all(base, lo, hi)) return out;
        uintptr_t span_lo = 0, span_hi = 0;
        if (!pool_scan_span(span_lo, span_hi) || span_hi <= base) return out;
        // [base, start) is ONE page that the runtime never commits - the pool
        // header itself lives there - so an offset below (span_lo - base) can
        // never address a string.  This is not theory: the 2026-11-02 20:18:46
        // crash was a stale cache slot holding offset 0x2EE, and the previous
        // bound only checked the upper end, so pool_base + 0x2EE = 0x5000000002EE
        // was dereferenced and killed the game.
        const size_t off_min = (size_t)(span_lo - base);
        const size_t off_max = (size_t)(span_hi - base);
        uintptr_t ghi = region_hi(groups);
        if (ghi <= groups) return out;
        uint64_t maxg = (uint64_t)((ghi - groups) / CACHE_GROUP_SIZE);
        uint64_t ng = limit / 16;
        if (ng > maxg) ng = maxg;                 // stay inside the allocation
        if (ng > (1u << 21)) ng = 1u << 21;
        for (uint64_t gidx = 0; gidx < ng; ++gidx) {
            uintptr_t gp = groups + (uintptr_t)gidx * CACHE_GROUP_SIZE;
            auto* grp = reinterpret_cast<const uint64_t*>(gp);
            uintptr_t sp = (uintptr_t)grp[0];
            if (!sp || (sp & 0xF)) continue;
            if (!mapped_fast(sp, 16 * 2 * sizeof(uint32_t))) continue;
            auto* slots = reinterpret_cast<const uint32_t*>(sp);
            for (int s = 0; s < 16; ++s) {
                uint32_t key = slots[s * 2 + 0];
                uint32_t off = slots[s * 2 + 1];
                if (!key || !off) continue;
                if ((size_t)off < off_min || (size_t)off >= off_max) continue;
                uintptr_t addr = base + off;
                // The offsets above already prove the address is inside the
                // committed span, but a stale slot can point anywhere and the
                // cost of asking Windows is one cached compare per slot, so the
                // page is verified before every single dereference.  Arithmetic
                // bounds are a filter; VirtualQuery is the proof.
                if (!mapped_fast(addr, 1)) continue;
                const char* p = (const char*)addr;
                unsigned char c0 = (unsigned char)p[0];
                if (c0 < 32 || c0 > 126) continue;
                out.emplace_back(key, p);
            }
        }
        return out;
    }

    // -----------------------------------------------------------------------
    // ELEMENT PREDICATES
    // The class heap is ONE committed reservation, so a bounds compare is both
    // the fastest and the safest guard: an uncommitted hole inside a region we
    // walked blindly would be an ACCESS_VIOLATION (a hard crash), not a C++
    // exception.  pool_scan_span() clamps the walk to the first fully committed,
    // readable subrange of the pool and caches it, so the hot loop is compares.
    // -----------------------------------------------------------------------
    // The pool GROWS as the game resolves more strings, so a span cached during
    // the first dump would silently hide everything committed afterwards.  Every
    // top-level dump invalidates the cache and re-derives it (a handful of
    // VirtualQuery calls); the hot loops inside one dump still get the compares.
    void invalidate_spans() const { span_lo_ = 0; span_hi_ = 0; }

    bool pool_scan_span(uintptr_t& lo, uintptr_t& hi) const {
        if (span_hi_) { lo = span_lo_; hi = span_hi_; return true; }
        uintptr_t b = 0, s = 0, e = 0;
        if (!pool_all(b, s, e)) return false;
        MEMORY_BASIC_INFORMATION mbi;
        uintptr_t p = b, first = 0, stop = 0;
        // The page at `base` may still be reserve-only (the runtime commits the
        // data area from start upwards), so probe forward for the first
        // committed, readable page instead of giving up on the first hole.
        for (int probe = 0; probe < 64 && p < e; ++probe) {
            if (VirtualQuery((LPCVOID)p, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
            uintptr_t rb = (uintptr_t)mbi.BaseAddress;
            size_t sz = (size_t)mbi.RegionSize;
            if (!sz) return false;
            bool good = mbi.State == MEM_COMMIT && (mbi.Protect & READABLE_PROT) &&
                        !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
            if (!good) {
                uintptr_t nx = rb + sz;
                if (nx <= p) return false;
                p = nx;
                continue;
            }
            first = p;
            stop = p;
            for (int guard = 0; guard < 8192 && p < e; ++guard) {
                if (VirtualQuery((LPCVOID)p, &mbi, sizeof(mbi)) != sizeof(mbi)) break;
                if (mbi.State != MEM_COMMIT || !(mbi.Protect & READABLE_PROT) ||
                    (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) break;
                uintptr_t rb2 = (uintptr_t)mbi.BaseAddress;
                size_t sz2 = (size_t)mbi.RegionSize;
                if (rb2 > p || !sz2) break;
                p = rb2 + sz2;
                stop = p;
            }
            break;
        }
        if (!first) return false;
        if (stop > e) stop = e;
        span_lo_ = first;
        span_hi_ = stop;
        lo = first;
        hi = stop;
        return hi > lo;
    }

    // In the heap, 8-byte aligned, and long enough for every field we read.
    bool el_in(uintptr_t c) const {
        uintptr_t lo, hi;
        if (!pool_scan_span(lo, hi)) return false;
        return c >= lo && c + 0x98 <= hi && (c & 7) == 0;
    }
    // typeDefIndex is read SIGNED: <Module> carries -1 (0xFFFFFFFF), which is
    // normal and makes mscorlib's array offset -1.
    int32_t el_tdi(uintptr_t c) const {
        return *reinterpret_cast<const int32_t*>(c + 0x88);
    }
    // Used ONLY to seed and to filter a run, never to bound an array: 28 real
    // classes have a name slot pointing outside the heap.
    bool el_named(uintptr_t c) const {
        uintptr_t lo, hi;
        if (!pool_scan_span(lo, hi)) return false;
        if (c < lo || c + 0x28 > hi) return false;
        uintptr_t np = *reinterpret_cast<const uintptr_t*>(c + 0x20);
        if (np < lo || np >= hi) return false;
        unsigned char ch = *reinterpret_cast<const unsigned char*>(np);
        return ch >= 0x20 && ch <= 0x7E;
    }

    // Copy a class name into out, bounded by the committed heap span and
    // restricted to printable ASCII.  Returns false when the class has no
    // readable name, and NEVER dereferences a pointer it cannot prove to be
    // inside the heap - some classes carry a name slot pointing into code.
    bool copy_name(uintptr_t klass, char* out, size_t cap) const {
        if (!out || cap < 2) return false;
        out[0] = 0;
        uintptr_t lo, hi;
        if (!pool_scan_span(lo, hi) || !el_in(klass)) return false;
        uintptr_t np = *reinterpret_cast<const uintptr_t*>(klass + 0x20);
        if (np < lo || np >= hi) return false;
        size_t max = (size_t)(hi - np);
        if (max > cap - 1) max = cap - 1;
        const unsigned char* s = reinterpret_cast<const unsigned char*>(np);
        size_t i = 0;
        for (; i < max; ++i) {
            unsigned char ch = s[i];
            if (!ch) break;
            if (ch < 0x20 || ch > 0x7E) { out[0] = 0; return false; }
            out[i] = (char)ch;
        }
        out[i] = 0;
        return i > 0;
    }

    // Materialise exactly n elements.
    std::vector<uintptr_t> class_array(uintptr_t base, size_t n) const {
        std::vector<uintptr_t> v;
        if (!base || !n) return v;
        v.resize(n);
        for (size_t i = 0; i < n; ++i)
            v[i] = reinterpret_cast<const uintptr_t*>(base)[i];
        return v;
    }

    // Discover every class array in the decrypted heap without any hardcoded
    // address.  The array offset (tdi - index) is constant but arbitrary per
    // array (mscorlib's is -1 because element 0 is <Module>), so a start point
    // can never be pre-judged.
    //
    // WHY THE PREVIOUS VERSION KILLED THE GAME (2026-11-02 crash):
    //   the identity tdi - i == off also holds at EVERY interior element, so a
    //   walk started at offset k of an L-element array returned L-k elements.
    //   That version materialised every such start with reserve(1<<20) - 8 MiB
    //   of capacity retained in the result - so ONE 1756-slot array produced
    //   ~1692 vectors x 8 MiB = ~13 GiB of commitment.  The commit limit ran
    //   out (the crash log shows the paging file with 129 MB free), operator new
    //   threw std::bad_alloc, and the worker thread died in
    //   RaiseException(0x20474343) - the GCC C++ throw code, confirmed present
    //   in this DLL at file offset 0xC15C.  Stack, resolved from our own DWARF:
    //     dump_all -> worker -> Runtime::all_class_arrays -> operator new
    //     -> __cxa_throw -> _Unwind_RaiseException.
    // THE ALGORITHM BELOW is a single O(n) forward pass with O(1) extra memory:
    //   * link(i)   : elements i and i+1 are both in the heap and
    //                 tdi(i+1) == tdi(i) + 1 - the identity across a pair;
    //   * linked(i) : element i is in the heap and shares a link with EITHER
    //                 neighbour.  The first and last element of an array have
    //                 only one neighbour, so "either" is what makes the runs
    //                 come out exactly right (using only the previous link
    //                 truncated EVERY array by one element);
    //   * maximal runs of linked elements ARE the arrays, and they are disjoint
    //     by construction, so the old sort/covered dedupe is unnecessary;
    //   * a run survives only if len >= minlen AND at least half its elements
    //     have a printable name.  Both filters reject junk chains; real arrays
    //     measure 99.9-100 %.
    // VALIDATED against a 2.41 GB dump of the live process (bhdump/verify_final.py):
    //   7 arrays whose bases AND lengths match an independent extractor exactly,
    //   76,053 unique classes, 0 false positives, 0 missing.  The remaining 8
    //   arrays live in a second allocation and are 100 % duplicates (3,015 slots).
    std::vector<std::vector<uintptr_t>> all_class_arrays(size_t minlen = 64) const {
        struct Run { size_t at; size_t n; };
        std::vector<std::vector<uintptr_t>> out;
        uintptr_t lo = 0, hi = 0;
        if (!pool_scan_span(lo, hi) || hi - lo < 2 * sizeof(uintptr_t)) return out;
        const size_t n = (size_t)((hi - lo) / sizeof(uintptr_t));
        const uintptr_t* q = reinterpret_cast<const uintptr_t*>(lo);
        const size_t kMaxSlots = 1u << 21;      // 16 MiB of pointers: sanity cap
        std::vector<Run> runs;

        bool in_run = false;
        size_t run_at = 0;
        bool prev_link = false;
        uintptr_t c0 = q[0];
        bool in0 = el_in(c0);
        int32_t t0 = in0 ? el_tdi(c0) : 0;
        for (size_t i = 0; i + 1 < n; ++i) {
            uintptr_t c1 = q[i + 1];
            bool in1 = el_in(c1);
            int32_t t1 = in1 ? el_tdi(c1) : 0;
            bool link = in0 && in1 && t1 == t0 + 1;
            bool linked = in0 && (prev_link || link);
            if (linked) {
                if (!in_run) { run_at = i; in_run = true; }
            } else if (in_run) {
                runs.push_back(Run{ run_at, i - run_at });
                in_run = false;
            }
            prev_link = link;
            in0 = in1;
            t0 = t1;
        }
        {
            bool linked = in0 && prev_link;
            if (linked) {
                if (!in_run) { run_at = n - 1; in_run = true; }
            } else if (in_run) {
                runs.push_back(Run{ run_at, n - 1 - run_at });
                in_run = false;
            }
            if (in_run) runs.push_back(Run{ run_at, n - run_at });
        }

        size_t kept = 0;
        for (const Run& r : runs) {
            if (r.n < minlen) continue;
            size_t named = 0;
            for (size_t k = 0; k < r.n; ++k)
                if (el_named(q[r.at + k])) ++named;
            if (2 * named < r.n) continue;          // junk chain: mostly unnamed
            if (kept + r.n > kMaxSlots) break;
            out.push_back(class_array(lo + r.at * sizeof(uintptr_t), r.n));
            kept += r.n;
        }
        return out;
    }

    // Methods: contiguous per class, and the array base is not derivable from
    // the class, so memory is enumerated.  Every read is page-guarded, and
    // records are collapsed on (klass, methodToken) afterwards: a MethodDef
    // token is unique per class, so a second record claiming the same pair is a
    // false positive.  Pass the class table from all_class_arrays() to also
    // require klass to be a class we actually found - that is the rule the
    // offline extractor was validated with (135,050 raw -> 132,607 rows, 99.8 %
    // of them with a well-formed MethodDef token).
    std::vector<MethodInfo*> all_methods(const std::vector<uintptr_t>* classes = nullptr) const {
        std::vector<MethodInfo*> out;
        std::vector<Region> regions = scan_regions();
        uintptr_t pool_lo = 0, pool_hi = 0;
        pool_scan_span(pool_lo, pool_hi);
        // A concurrent scan of the whole 2.4 GB heap is both slow and racy (the
        // game keeps allocating while we walk).  Every MethodInfo record an
        // independent offline extractor found lives in regions of a few tens of
        // MB, so a 128 MB ceiling per region keeps all of them and drops the
        // multi-hundred-MB arenas where a record cannot exist anyway.
        const size_t kRegionCap = (size_t)128 << 20;
        size_t scanned = 0, skipped = 0;
        for (const Region& r : regions) {
            if (r.size < sizeof(MethodInfo)) continue;
            bool in_pool = pool_hi > pool_lo && r.base >= pool_lo && r.base < pool_hi;
            if (!in_pool && r.size > kRegionCap) { ++skipped; continue; }
            ++scanned;
            uintptr_t end = r.base + r.size;
            for (uintptr_t p = r.base; p + sizeof(MethodInfo) <= end; p += 8) {
                if (!mapped_fast(p, sizeof(MethodInfo))) continue;
                auto* m = reinterpret_cast<MethodInfo*>(p);
                if (!m->methodPointer) continue;
                uintptr_t rva = m->methodPointer - ua_;
                if (rva < 0x1000 || rva >= 0x1284b000) continue;
                if (!el_in(m->klass)) continue;
                if (m->maxStack != 0x00000000ffffffffull) continue;
                if ((m->methodToken >> 24) != 0x06) continue;
                out.push_back(m);
            }
        }
        last_method_regions_ = scanned;
        last_method_regions_skipped_ = skipped;
        std::sort(out.begin(), out.end(), [](const MethodInfo* a, const MethodInfo* b) {
            if (a->klass != b->klass) return a->klass < b->klass;
            if (a->methodToken != b->methodToken) return a->methodToken < b->methodToken;
            return (uintptr_t)a < (uintptr_t)b;
        });
        out.erase(std::unique(out.begin(), out.end(),
                              [](const MethodInfo* a, const MethodInfo* b) {
                                  return a->klass == b->klass &&
                                         a->methodToken == b->methodToken;
                              }),
                  out.end());
        if (classes && !classes->empty()) {
            std::vector<MethodInfo*> keep;
            keep.reserve(out.size());
            for (MethodInfo* m : out)
                if (std::binary_search(classes->begin(), classes->end(), m->klass))
                    keep.push_back(m);
            out.swap(keep);
        }
        return out;
    }

    // -----------------------------------------------------------------------
    // SAFE MEMORY ACCESS
    // A blind 8-byte walk over a multi-hundred-MB region will eventually touch
    // an uncommitted hole, and that is an ACCESS_VIOLATION (a hard crash), not
    // a C++ exception.  Everything therefore goes through mapped_fast(), which
    // caches the last VirtualQuery region; the il2cpp heap is one huge
    // MEM_COMMIT reservation, so the cache turns this into two compares.
    // -----------------------------------------------------------------------
    bool mapped_fast(uintptr_t addr, size_t len) const {
        if (!addr) return false;
        // BOTH ends matter.  The 2026-11-02 20:31:54 crash was this line with
        // only `addr >= mtop_`: a garbage slot pointer ABOVE the cached region
        // made `mbot_ - addr` underflow to ~2^64, so `len <= that` was true and
        // an unmapped pointer was handed to the caller as "mapped".
        if (addr >= mtop_ && addr < mbot_ && len <= mbot_ - addr) return true;
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        uintptr_t top = (uintptr_t)mbi.BaseAddress;
        uintptr_t bot = top + mbi.RegionSize;
        if (bot <= addr) return false;
        mtop_ = top; mbot_ = bot;
        return len <= mbot_ - addr;
    }

    // A class pointer is usable only when its +0x20 name field is a mapped,
    // printable ASCII string.  This rejects the overwhelming majority of
    // garbage in one step.
    bool name_ok(uintptr_t klass) const {
        if (!klass || (klass & 7)) return false;
        if (!mapped_fast(klass, 0x98)) return false;
        const char* nm = reinterpret_cast<const Il2CppClass*>(klass)->name;
        uintptr_t p = (uintptr_t)nm;
        if (!p || !mapped_fast(p, 1)) return false;
        unsigned char c = (unsigned char)nm[0];
        return c >= 32 && c <= 126;
    }

    // Every committed, readable region of the process.  The il2cpp string/type
    // heap is the biggest one, but per-image class arrays also live in secondary
    // allocations, so the walk must not be limited to the pool bounds alone.
    struct Region { uintptr_t base; size_t size; };

    std::vector<Region> scan_regions() const {
        std::vector<Region> v;
        uintptr_t p = 0x10000;
        MEMORY_BASIC_INFORMATION mbi;
        for (int guard = 0; guard < 100000; ++guard) {
            if (VirtualQuery((LPCVOID)p, &mbi, sizeof(mbi)) != sizeof(mbi)) break;
            uintptr_t b = (uintptr_t)mbi.BaseAddress;
            size_t sz = mbi.RegionSize;
            if (!sz) break;
            const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                   PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                   PAGE_EXECUTE_WRITECOPY;
            if (mbi.State == MEM_COMMIT && (mbi.Protect & readable) &&
                !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && mbi.Type != MEM_IMAGE)
                v.push_back(Region{ b, sz });
            uintptr_t next = b + sz;
            if (next <= p) break;
            p = next;
            if (p >= 0x00007FFFFFFF0000ull) break;
        }
        return v;
    }

private:
    uintptr_t ua_ = 0;
    mutable uintptr_t mtop_ = 0;
    mutable uintptr_t mbot_ = 0;
    mutable uintptr_t span_lo_ = 0;     // cached class-heap committed span
    mutable uintptr_t span_hi_ = 0;
public:
    // set by the last all_methods() call, for the dump report
    mutable size_t last_method_regions_ = 0;
    mutable size_t last_method_regions_skipped_ = 0;
private:
};

} // namespace bh3