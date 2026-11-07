#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <map>

#include <windows.h>

#include "ov_resolve.h"

struct Blob
{
    uintptr_t base = 0;
    size_t size = 0;
    std::vector<uint8_t> data;
};

static std::vector<Blob> g_blobs;

static const Blob* FindBlob(uintptr_t a)
{
    for (size_t i = 0; i < g_blobs.size(); ++i)
    {
        const Blob& b = g_blobs[i];
        if (a >= b.base && a < b.base + b.size) return &b;
    }
    return nullptr;
}

static bool DumpRead(void*, uintptr_t addr, void* dst, size_t n)
{
    const Blob* b = FindBlob(addr);
    if (!b) return false;
    const size_t off = (size_t)(addr - b->base);
    if (off + n > b->size) return false;
    std::memcpy(dst, b->data.data() + off, n);
    return true;
}

static int DumpRegions(void*, ovr::Region* out, int cap)
{
    int n = 0;
    for (size_t i = 0; i < g_blobs.size() && n < cap; ++i)
    {
        out[n].base = g_blobs[i].base;
        out[n].size = g_blobs[i].size;
        out[n].index = n;
        ++n;
    }
    return n;
}

static bool LoadCsv(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) { std::printf("no csv: %s\n", path); return false; }
    char line[4096];
    bool first = true;
    int loaded = 0;
    while (fgets(line, sizeof line, f))
    {
        if (first) { first = false; continue; }
        char* p = line;
        char* base_s = p;
        char* c1 = std::strchr(p, ','); if (!c1) continue; *c1 = 0; p = c1 + 1;
        char* size_s = p;
        char* c2 = std::strchr(p, ','); if (!c2) continue; *c2 = 0; p = c2 + 1;
        char* c3 = std::strchr(p, ','); if (!c3) continue; *c3 = 0; p = c3 + 1;
        char* c4 = std::strchr(p, ','); if (!c4) continue; *c4 = 0; p = c4 + 1;
        char* type_s = p;
        char* c5 = std::strchr(p, ','); if (!c5) continue; *c5 = 0; p = c5 + 1;
        char* file_s = p;
        char* c6 = std::strchr(p, ','); if (!c6) continue; *c6 = 0; p = c6 + 1;

        const uintptr_t base = (uintptr_t)std::strtoull(base_s, nullptr, 16);
        const size_t size = (size_t)std::strtoull(size_s, nullptr, 16);
        if (size == 0) continue;
        if (std::strcmp(type_s, "private") != 0) continue;
        if (base < 0x50000000000ULL || base >= 0x50040000000ULL) continue;

        Blob b;
        b.base = base;
        b.size = size;
        b.data.resize(size);
        FILE* bf = fopen(file_s, "rb");
        if (!bf) continue;
        const size_t got = fread(b.data.data(), 1, size, bf);
        fclose(bf);
        b.data.resize(got);
        b.size = b.data.size();
        if (b.size == 0) continue;
        g_blobs.push_back(std::move(b));
        ++loaded;
    }
    fclose(f);
    std::printf("load %d heap regions\n", loaded);
    return loaded > 0;
}

struct Sym
{
    const char* key;
    const char* cls;
    uint32_t token;
    uint32_t want;
};

static const Sym kSyms[] = {
    { "camera_main",           "Camera",                   0x06000264, 0x121E6560 },
    { "camera_pixel_w",        "Camera",                   0x0600020A, 0x121E5A80 },
    { "camera_pixel_h",        "Camera",                   0x0600020B, 0x121E5A90 },
    { "camera_w2c",            "Camera",                   0x06000212, 0x121E5B90 },
    { "camera_proj",           "Camera",                   0x06000218, 0x121E5C40 },
    { "die_now",               "BaseMonoMonster",          0x0600C4EE, 0x051E5960 },
    { "be_hit",                "BaseMonoAvatar",           0x0600A5A6, 0x04A73910 },
    { "ui_skill_in_cd",        "MonoSkillButton",          0x0602927A, 0x0730B300 },
    { "ui_can_use_skill",      "MonoSkillButton",          0x06029279, 0x0730B200 },
    { "avatar_can_use_skill",  "BaseMonoAvatar",           0x0600A581, 0x04A6ADC0 },
    { "crit_main",             "MHEMGJMNDJI",              0x06003B87, 0x0793FB50 },
    { "crit_phys",             "MHEMGJMNDJI",              0x06003B89, 0x0793FF90 },
    { "crit_elem",             "MHEMGJMNDJI",              0x06003B88, 0x07940040 },
    { "anim_speed_apply",      "BaseMonoAnimatorEntity",   0x0600B03C, 0x099646E0 },
    { "anim_speed_cancel",     "BaseMonoAnimatorEntity",   0x0600B03D, 0x09964930 },
    { "ultra0",  "MonoFuka",           0x0600AB8C, 0x09BAE670 },
    { "ultra1",  "MonoFuka_BattleY",   0x0600ABA5, 0x09DD97D0 },
    { "ultra2",  "MonoFukaFL",         0x0600ABB4, 0x0863B560 },
    { "ultra3",  "MonoFukaFL_BattleY", 0x0600ABBC, 0x0863BF90 },
    { "ultra4",  "MonoFukaIN",         0x0600ABC6, 0x076C3560 },
    { "ultra5",  "MonoFukaIN_BattleY", 0x0600ABC9, 0x09BAF090 },
    { "ultra6",  "MonoFukaJW",         0x0600ABCC, 0x0505FF60 },
    { "ultra7",  "MonoFukaJW_BattleY", 0x0600ABCF, 0x058468F0 },
    { "ultra8",  "MonoFukaND",         0x0600ABD2, 0x081252E0 },
    { "ultra9",  "MonoFukaND_BattleY", 0x0600ABD5, 0x06B86AF0 },
    { "ultra10", "MonoFukaNL",         0x0600ABD8, 0x0863CA20 },
    { "ultra11", "MonoFukaNL_BattleY", 0x0600ABDB, 0x080BE120 },
};

int main(int argc, char** argv)
{
    const char* csv = (argc > 1) ? argv[1] : "D:\\Cheat Table\\CE Search\\regions.csv";
    if (!LoadCsv(csv)) return 1;

    ovr::Hooks h;
    h.read = &DumpRead;
    h.regions = &DumpRegions;
    ovr::SetHooks(h);
    ovr::LoadRegions();

    const int n = (int)(sizeof kSyms / sizeof kSyms[0]);

    std::vector<std::string> names;
    for (int i = 0; i < n; ++i)
    {
        bool seen = false;
        for (size_t j = 0; j < names.size(); ++j) if (names[j] == kSyms[i].cls) { seen = true; break; }
        if (!seen) names.push_back(std::string(kSyms[i].cls));
    }
    std::vector<const char*> carr;
    for (size_t i = 0; i < names.size(); ++i) carr.push_back(names[i].c_str());

    DWORD t0 = GetTickCount();
    const int nf = ovr::BuildIndexFor(carr.data(), (int)carr.size());
    DWORD t1 = GetTickCount();
    std::printf("index: scanned %llu bytes, klass %d, found %d/%d names, %lu ms\n",
                (unsigned long long)ovr::ScannedBytes(), ovr::ValidKlasses(),
                nf, (int)carr.size(), (unsigned long)(t1 - t0));

    std::printf("\n%-22s %-24s %-10s %-12s %-12s %s\n",
                "key", "class", "token", "resolved", "want", "result");
    int ok = 0, bad = 0;
    for (int i = 0; i < n; ++i)
    {
        const Sym& s = kSyms[i];
        const uintptr_t k = ovr::FindKlass(s.cls);
        if (!k)
        {
            std::printf("%-22s %-24s 0x%08X %-12s 0x%08X MISS-CLASS\n",
                        s.key, s.cls, s.token, "-", s.want);
            ++bad;
            continue;
        }
        ovr::Hit hit;
        if (!ovr::ResolveMethod(k, s.token, &hit))
        {
            std::printf("%-22s %-24s 0x%08X %-12s 0x%08X MISS-METHOD\n",
                        s.key, s.cls, s.token, "-", s.want);
            ++bad;
            continue;
        }
        const uint32_t rva = (uint32_t)(hit.fn - 0x7FF8C38A0000ULL);
        const bool good = (rva == s.want);
        if (good) ++ok; else ++bad;
        std::printf("%-22s %-24s 0x%08X 0x%08X 0x%08X %s\n",
                    s.key, s.cls, s.token, rva, s.want, good ? "OK" : "DIFF");
    }

    std::printf("\nRESULT: ok=%d bad=%d\n", ok, bad);
    return bad == 0 ? 0 : 2;
}
