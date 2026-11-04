#pragma once
#include "ov_data.h"

#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace ovn {

static const uint32_t RVA_GET_MAIN = 0x121E6560;
static const uint32_t RVA_PIXEL_W = 0x121E5A80;
static const uint32_t RVA_PIXEL_H = 0x121E5A90;
static const uint32_t RVA_W2C = 0x121E5B90;
static const uint32_t RVA_PROJ = 0x121E5C40;
static const uint32_t RVA_DIE_NOW = 0x51E5960;
static const uint32_t RVA_BEHIT = 0x4A73910;

static const uint32_t G_AVATAR_MGR = 0x2641588;
static const uint32_t G_MONSTER_MGR = 0x2642EA8;

struct Ent {
    uintptr_t p = 0;
    uintptr_t k = 0;
    std::string name;
    float x = 0, y = 0, z = 0;
};

struct State {
    bool ready = false;
    uintptr_t avatar_mgr = 0, monster_mgr = 0;
    std::vector<Ent> monsters, avatars;
    bool cam_ok = false;
    uintptr_t camera = 0;
    float vp[16] = { 0 };
    float w = 0, h = 0;
    char msg[160] = "未初始化";
    uint64_t t = 0;
};

inline uintptr_t mgr_from(ov::Data& d, uint32_t g_rva, uintptr_t want_klass) {
    uintptr_t mi = 0;
    if (!d.rd(d.ua + g_rva, mi) || !mi) return 0;
    uintptr_t klass = 0;
    if (!d.rd(mi + 8, klass) || !klass) return 0;
    uintptr_t c = 0;
    if (!d.rd(klass + 0x18, c) || !c) return 0;
    uintptr_t x = 0;
    if (!d.rd(c + 0x10, x) || !x) return 0;
    uintptr_t e = 0;
    if (!d.rd(x + 0x30, e) || !e) return 0;
    uintptr_t m = 0;
    if (!d.rd(e, m) || !m) return 0;
    uintptr_t mk = 0;
    if (!d.rd(m, mk) || (want_klass && mk != want_klass)) return 0;
    return m;
}

inline bool read_list(ov::Data& d, uintptr_t list, std::vector<uintptr_t>& out, size_t cap = 1024) {
    out.clear();
    if (!list) return false;
    uintptr_t items = 0;
    int32_t size = 0;
    if (!d.rd(list + 0x10, items) || !d.rd(list + 0x18, size)) return false;
    if (!items || size < 0 || size > (int32_t)cap) return false;
    for (int32_t i = 0; i < size; ++i) {
        uintptr_t p = 0;
        if (!d.rd(items + 0x20 + (uintptr_t)i * 8, p) || !p) continue;
        out.push_back(p);
    }
    return true;
}

inline void fill_ents(ov::Data& d, const std::vector<uintptr_t>& ps, std::vector<Ent>& out) {
    out.clear();
    out.reserve(ps.size());
    for (uintptr_t p : ps) {
        Ent e;
        e.p = p;
        if (!d.rd(p, e.k) || !e.k) continue;
        const char* nm = d.name_of(e.k);
        e.name = nm ? nm : "?";
        d.rd(p + 0x734, e.x);
        d.rd(p + 0x738, e.y);
        d.rd(p + 0x73C, e.z);
        out.push_back(e);
    }
}

inline void mul_m4(const float A[16], const float B[16], float R[16]) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += A[r + k * 4] * B[k + c * 4];
            R[r + c * 4] = s;
        }
}

inline bool w2s(const float VP[16], float x, float y, float z, float W, float H,
                float& sx, float& sy) {
    float cx = VP[0] * x + VP[4] * y + VP[8] * z + VP[12];
    float cy = VP[1] * x + VP[5] * y + VP[9] * z + VP[13];
    float cw = VP[3] * x + VP[7] * y + VP[11] * z + VP[15];
    if (!(cw > 0.0001f)) return false;
    float nx = cx / cw, ny = cy / cw;
    if (!(nx == nx) || !(ny == ny)) return false;
    if (nx < -20.f || nx > 20.f || ny < -20.f || ny > 20.f) return false;
    sx = (nx * 0.5f + 0.5f) * W;
    sy = (0.5f - ny * 0.5f) * H;
    return true;
}

inline void refresh_camera(ov::Data& d, State& s) {
    typedef void* (*GetMain)(void*);
    typedef int (*GetInt)(void*, void*);
    typedef void (*GetMat)(void*, void*, void*);
    uintptr_t ua = d.ua;
    void* cam = ((GetMain)(ua + RVA_GET_MAIN))(nullptr);
    s.camera = (uintptr_t)cam;
    s.cam_ok = false;
    if (!cam) { lstrcpyA(s.msg, "Camera.get_main 返回空"); return; }
    int w = ((GetInt)(ua + RVA_PIXEL_W))(cam, nullptr);
    int h = ((GetInt)(ua + RVA_PIXEL_H))(cam, nullptr);
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) { lstrcpyA(s.msg, "像素尺寸无效"); return; }
    float v[16] = { 0 }, p[16] = { 0 };
    ((GetMat)(ua + RVA_W2C))(cam, v, nullptr);
    ((GetMat)(ua + RVA_PROJ))(cam, p, nullptr);
    for (int i = 0; i < 16; ++i) {
        if (!(v[i] == v[i]) || !(p[i] == p[i])) { lstrcpyA(s.msg, "矩阵 NaN"); return; }
        if (v[i] > 1e7f || v[i] < -1e7f || p[i] > 1e7f || p[i] < -1e7f) {
            lstrcpyA(s.msg, "矩阵数值异常"); return;
        }
    }
    mul_m4(p, v, s.vp);
    s.w = (float)w;
    s.h = (float)h;
    s.cam_ok = true;
    wsprintfA(s.msg, "相机 %p %dx%d", cam, w, h);
}

inline void refresh(ov::Data& d, State& s) {
    if (!d.ready_ok()) return;
    s.avatar_mgr = mgr_from(d, G_AVATAR_MGR, d.find("AvatarManager"));
    s.monster_mgr = mgr_from(d, G_MONSTER_MGR, d.find("MonsterManager"));
    std::vector<uintptr_t> ps;
    if (s.avatar_mgr) {
        uintptr_t lst = 0;
        if (d.rd(s.avatar_mgr + 0x50, lst) && read_list(d, lst, ps)) fill_ents(d, ps, s.avatars);
    }
    if (s.monster_mgr) {
        uintptr_t lst = 0;
        if (d.rd(s.monster_mgr + 0x18, lst) && read_list(d, lst, ps)) fill_ents(d, ps, s.monsters);
    }
    refresh_camera(d, s);
    s.ready = s.avatar_mgr || s.monster_mgr;
    s.t = GetTickCount64();
    if (!s.ready) lstrcpyA(s.msg, "管理器链为空（还没进关卡？）");
}

static uint8_t g_behit_backup[12] = { 0 };
static bool g_behit_on = false;

extern "C" void __cdecl behit_stub(void) { return; }

inline bool god_mode(ov::Data& d, bool on) {
    uintptr_t t = d.ua + RVA_BEHIT;
    if (on == g_behit_on) return true;
    if (on) {
        memcpy(g_behit_backup, (void*)t, 12);
        uint8_t patch[12];
        patch[0] = 0x48; patch[1] = 0xB8;
        *(uint64_t*)&patch[2] = (uint64_t)&behit_stub;
        patch[10] = 0xFF; patch[11] = 0xE0;
        DWORD old = 0;
        if (!VirtualProtect((void*)t, 12, PAGE_EXECUTE_READWRITE, &old)) return false;
        memcpy((void*)t, patch, 12);
        VirtualProtect((void*)t, 12, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void*)t, 12);
    } else {
        DWORD old = 0;
        if (!VirtualProtect((void*)t, 12, PAGE_EXECUTE_READWRITE, &old)) return false;
        memcpy((void*)t, g_behit_backup, 12);
        VirtualProtect((void*)t, 12, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void*)t, 12);
    }
    g_behit_on = on;
    return true;
}

inline int kill_all(ov::Data& d, const State& s) {
    typedef void (*DieNow)(void*, bool, void*);
    int n = 0;
    for (const Ent& e : s.monsters) {
        if (!e.p) continue;
        ((DieNow)(d.ua + RVA_DIE_NOW))((void*)e.p, true, nullptr);
        if (++n >= 64) break;
    }
    return n;
}

}
