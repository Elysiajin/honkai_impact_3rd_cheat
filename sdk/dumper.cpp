// dumper.cpp - in-process SDK dumper for Honkai Impact 3rd (GF/PC)
//
// BUILD   (MinGW on this machine; MSVC works too)
//   g++ -shared -O2 -static -static-libgcc -static-libstdc++ -o bh3dump.dll dumper.cpp
//   cl /nologo /LD /MT /O2 /EHsc dumper.cpp /Fe:bh3dump.dll
//
// USAGE
//   Inject bh3dump.dll into BH3.exe once il2cpp has initialised (any
//   LoadLibrary-style injector: Process Hacker, Xenos, CreateRemoteThread +
//   LoadLibraryW).  A console opens and three files appear next to BH3.exe:
//       bh3_classes.tsv   tdi  name  parent  flags  klass_va  image_va
//       bh3_methods.tsv   tdi  class  slot  mp_rva  inv_rva  token  methodinfo_va
//       bh3_names.tsv     key  string   (every string the runtime decrypted)
//
// NO ABSOLUTE ADDRESSES.  Every code/data reference is
//   UserAssembly.dll base (GetModuleHandleA) + RVA
// and every heap reference is read out of the runtime's own globals, so the
// same binary works on every launch regardless of ASLR.
//
// SAFETY MODEL (written after the 2026-11-02 crash)
//   * no read is performed before a bounds/commitment check;
//   * no game function is CALLED - not even the string resolver, which needs a
//     thread attached to the il2cpp runtime.  Names come from the cache the
//     runtime already decrypted;
//   * every stage is isolated in its own try/catch, so a failure in one never
//     aborts the others and no C++ exception can reach the game;
//   * allocation is bounded: class arrays are materialised at their exact
//     length, never at a reserved ceiling.
//
#include "bh3_sdk.h"

// Printed on the first line: SizeOfImage stayed 0x48000 across two different
// builds, so the console line is the only reliable way to tell which binary is
// actually loaded.  Bump this string on every behavioural change.
static const char* kBuildId = "build 2026-11-02.5 (parent VA, method tdi, real image_va)";
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <exception>
#include <new>

static bh3::Runtime g_rt;

typedef std::unordered_map<unsigned, const char*> NameMap;

// ---------------------------------------------------------------------------
// Stage 3: every string the runtime has already decrypted.
// The name table is a lazy dense hash that decrypts on miss and appends to the
// pool, so its occupancy is a snapshot of what the game has touched so far.
// A fresh process has almost nothing; a long session has tens of thousands.
// ---------------------------------------------------------------------------
static void dump_names(FILE* out, NameMap& map) {
    auto v = g_rt.cached_strings();
    size_t written = 0;
    for (auto& kv : v) {
        if (!kv.second) continue;
        const char* p = kv.second;
        // The scan is clamped to the committed pool span, not to 256: a string
        // sitting in the last 256 bytes of the pool must not run off the end of
        // the last committed page.
        size_t avail = g_rt.pool_avail((uintptr_t)p);
        if (!avail) continue;
        size_t len = 0, lim = avail < 256 ? avail : 256;
        while (len < lim && p[len]) ++len;
        if (!len) continue;
        bool ok = true;
        for (size_t i = 0; i < len; ++i) {
            unsigned char c = (unsigned char)p[i];
            if (c < 32 || c > 126) { ok = false; break; }
        }
        if (!ok) continue;
        fprintf(out, "%08x\t%.*s\n", kv.first, (int)len, p);
        map[kv.first] = p;
        ++written;
    }
    fprintf(stderr, "[+] name cache: %zu slots, %zu printable strings\n",
            v.size(), written);
}

// ---------------------------------------------------------------------------
// Stage 1: class table.
// ---------------------------------------------------------------------------
static void dump_classes(FILE* out, std::vector<uintptr_t>& all) {
    auto arrays = g_rt.all_class_arrays(64);
    uintptr_t pool_base = 0, pool_start = 0, pool_end = 0;
    g_rt.pool_all(pool_base, pool_start, pool_end);
    size_t slots = 0, named = 0;
    char nm[192];
    for (auto& arr : arrays) {
        if (arr.empty()) continue;
        // Each slot holds a pointer to its own class, so slot 0 IS the array
        // base: the offline extractor called this column `img` (the class
        // array the class was found in).
        uintptr_t image_va = arr[0];
        for (uintptr_t c : arr) {
            auto* k = reinterpret_cast<bh3::Il2CppClass*>(c);
            ++slots;
            all.push_back(c);
            bool ok = g_rt.copy_name(c, nm, sizeof(nm));
            if (ok) ++named;
            // +0xB0 packs the parent as a POOL-RELATIVE offset in its low 36
            // bits (0 means "no parent") and the class flags in the high 32.
            // Printing the raw halves is useless as an SDK column: encode the
            // parent the same way the offline extractor did, i.e. base|offset,
            // which is what a hierarchy walk needs.
            uint64_t par = ((uint64_t)k->flagsHi << 32) | k->parentLow;
            uint64_t par_off = par & 0xFFFFFFFFFull;
            uintptr_t par_va = par ? (pool_base | par_off) : 0;
            fprintf(out, "%d\t%s\t%#llx\t%#x\t%#llx\t%#llx\n",
                    k->typeDefIndex,
                    ok ? nm : "?",
                    (unsigned long long)par_va, k->flagsHi,
                    (unsigned long long)c,
                    (unsigned long long)image_va);
        }
    }
    std::sort(all.begin(), all.end());
    all.erase(std::unique(all.begin(), all.end()), all.end());
    fprintf(stderr, "[+] %zu class arrays, %zu slots (%zu unique), %zu with a readable name\n",
            arrays.size(), slots, all.size(), named);
}

// ---------------------------------------------------------------------------
// Stage 2: method table.
// ---------------------------------------------------------------------------
static void dump_methods(FILE* out, const std::vector<uintptr_t>& classes) {
    auto v = g_rt.all_methods(classes.empty() ? nullptr : &classes);
    char nm[192];
    for (auto* m : v) {
        bool ok = g_rt.copy_name(m->klass, nm, sizeof(nm));
        uintptr_t rva = m->methodPointer - g_rt.base();
        uintptr_t inv = m->invoker ? m->invoker - g_rt.base() : 0;
        // Class names are NOT unique (56,410 distinct names for 76,053
        // classes), so the name alone cannot key a join.  typeDefIndex is the
        // stable, session-independent identity: (tdi, token) is unique.
        int32_t tdi = g_rt.el_tdi(m->klass);
        fprintf(out, "%d\t%s\t%u\t%#llx\t%#llx\t%#x\t%#llx\n",
                tdi, ok ? nm : "?", m->slot,
                (unsigned long long)rva, (unsigned long long)inv,
                m->methodToken, (unsigned long long)(uintptr_t)m);
    }
    fprintf(stderr, "[+] %zu MethodInfo records (%zu regions scanned, %zu oversized skipped)\n",
            v.size(), g_rt.last_method_regions_, g_rt.last_method_regions_skipped_);
}

// ---------------------------------------------------------------------------
// Module names: the index encoded in startup-metadata.dat resolves through the
// SAME cache, so no call into the runtime is needed.  Any index the game has
// not looked up yet simply stays unresolved.
// ---------------------------------------------------------------------------
static void dump_module_names(FILE* out, const NameMap& map) {
    uintptr_t startup = g_rt.startup_buffer();
    uintptr_t hdr = g_rt.metadata_header();
    if (!startup || !hdr) { fprintf(stderr, "[-] no startup metadata buffer\n"); return; }
    uint32_t off = bh3::hdr_modules_offset(hdr);
    uint32_t n = g_rt.module_count();
    if (!off || !n) { fprintf(stderr, "[-] module table not decoded\n"); return; }
    if (!g_rt.mapped_fast(startup + off, (size_t)n * 40)) {
        fprintf(stderr, "[-] module table not mapped\n");
        return;
    }
    size_t ok = 0;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t raw = *reinterpret_cast<const uint32_t*>(startup + off + i * 40 + 0x1C);
        uint32_t idx = bh3::decode_name_index(raw, i);
        NameMap::const_iterator it = map.find(idx);
        if (it != map.end() && it->second[0]) {
            fprintf(out, "%08x\t%s\t# module %u\n", idx, it->second, i);
            ++ok;
        }
    }
    fprintf(stderr, "[+] module names resolved from cache: %zu / %u\n", ok, n);
}

// ---------------------------------------------------------------------------
// Diagnostics: printed BEFORE any risky read, so that a crash still leaves the
// numbers we need on screen.  This is what the 20:04 run was missing - we knew
// it died right after the pool line but not a single field value.
// ---------------------------------------------------------------------------
static void print_diag() {
    uintptr_t base = 0, start = 0, end = 0;
    g_rt.pool_all(base, start, end);
    uintptr_t groups = g_rt.g<uintptr_t>(bh3::RVA_CACHE_GROUPS);
    uint64_t limit = g_rt.g<uint64_t>(bh3::RVA_CACHE_LIMIT);
    uint64_t count = g_rt.g<uint64_t>(bh3::RVA_CACHE_COUNT);
    uintptr_t ghi = g_rt.region_hi(groups);
    uintptr_t slo = 0, shi = 0;
    g_rt.pool_scan_span(slo, shi);
    fprintf(stderr, "[+] bh3 sdk dumper %s\n", kBuildId);
    fprintf(stderr, "[+] UserAssembly %p  il2cpp v%u  module count %u\n",
            (void*)g_rt.base(), g_rt.g<uint32_t>(bh3::RVA_IL2CPP_VERSION),
            g_rt.module_count());
    fprintf(stderr, "[+] pool  base %p  start %p  end %p  used %zu KB  readable %zu KB\n",
            (void*)base, (void*)start, (void*)end,
            g_rt.pool_used() / 1024u, (size_t)(shi - slo) / 1024u);
    fprintf(stderr, "[+] cache groups %p  region end %p (%llu KB)  limit %llu  count %llu\n",
            (void*)groups, (void*)ghi,
            (unsigned long long)((ghi > groups) ? (ghi - groups) / 1024u : 0),
            (unsigned long long)limit, (unsigned long long)count);
}

static void dump_all() {
    DWORD t0 = GetTickCount();
    g_rt.invalidate_spans();   // the pool grew since the last dump
    NameMap map;

    fprintf(stderr, "[>] stage 1/3 class table ...\n");
    std::vector<uintptr_t> classes;
    try {
        if (FILE* fc = fopen("bh3_classes.tsv", "w")) {
            fprintf(fc, "tdi\tname\tparent\tflags\tklass_va\timage_va\n");
            dump_classes(fc, classes);
            fclose(fc);
        } else fprintf(stderr, "[-] cannot create bh3_classes.tsv\n");
    } catch (const std::exception& e) {
        fprintf(stderr, "[-] classes: std::exception %s\n", e.what());
    } catch (...) {
        fprintf(stderr, "[-] classes: unknown exception\n");
    }

    fprintf(stderr, "[>] stage 2/3 method table ...\n");
    try {
        if (FILE* fm = fopen("bh3_methods.tsv", "w")) {
            fprintf(fm, "tdi\tclass\tslot\tmp_rva\tinv_rva\ttoken\tmethodinfo_va\n");
            dump_methods(fm, classes);
            fclose(fm);
        } else fprintf(stderr, "[-] cannot create bh3_methods.tsv\n");
    } catch (const std::exception& e) {
        fprintf(stderr, "[-] methods: std::exception %s\n", e.what());
    } catch (...) {
        fprintf(stderr, "[-] methods: unknown exception\n");
    }

    fprintf(stderr, "[>] stage 3/3 name cache ...\n");
    try {
        if (FILE* fn = fopen("bh3_names.tsv", "w")) {
            fprintf(fn, "key\tstring\n");
            dump_names(fn, map);
            dump_module_names(fn, map);
            fclose(fn);
        } else fprintf(stderr, "[-] cannot create bh3_names.tsv\n");
    } catch (const std::exception& e) {
        fprintf(stderr, "[-] names: std::exception %s\n", e.what());
    } catch (...) {
        fprintf(stderr, "[-] names: unknown exception\n");
    }

    fprintf(stderr, "[+] dump_all finished in %lu ms\n",
            (unsigned long)(GetTickCount() - t0));
}

static DWORD WINAPI worker(LPVOID) {
    if (AllocConsole()) {
        freopen("CONOUT$", "w", stderr);
        freopen("CONOUT$", "w", stdout);
        SetConsoleTitleA("bh3 sdk dumper");
    }

    if (!g_rt.attach()) {
        fprintf(stderr, "[-] UserAssembly.dll not loaded yet\n");
        return 1;
    }
    // 30 s is generous: il2cpp is up long before the loading screen ends.
    for (int i = 0; i < 600; ++i) {
        if (g_rt.ready() && g_rt.heap_ready()) break;
        Sleep(50);
    }
    if (!g_rt.ready()) { fprintf(stderr, "[-] il2cpp never became ready\n"); return 1; }
    if (!g_rt.heap_ready()) { fprintf(stderr, "[-] string pool header not usable\n"); return 1; }
    print_diag();

    // A young process has resolved almost no strings; give the cache time to
    // warm before dumping so the name column is not half empty.  Every call is
    // fenced: cached_strings() touches heap memory and must never escape.
    size_t prev = 0;
    for (int i = 0; i < 40; ++i) {
        size_t now = 0;
        try { now = g_rt.cached_strings().size(); }
        catch (...) { fprintf(stderr, "[-] warm-up threw\n"); break; }
        if (now == prev && now > 40000) break;
        prev = now;
        Sleep(250);
    }
    fprintf(stderr, "[+] name cache settled at %zu slots\n", prev);

    try {
        dump_all();
    } catch (...) {
        fprintf(stderr, "[-] dump_all escaped an exception\n");
    }
    fprintf(stderr, "[+] done - files are in the game directory\n");
    return 0;
}

// Manual re-dump after the game has touched more strings (the name pool only
// grows).  Never let an exception cross this boundary either.
extern "C" __declspec(dllexport) void Bh3DumpAgain() {
    try { dump_all(); } catch (...) { fprintf(stderr, "[-] Bh3DumpAgain caught\n"); }
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
    }
    return TRUE;
}