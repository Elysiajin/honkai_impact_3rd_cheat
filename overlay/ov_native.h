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

// 所有 RVA 都来自 bh3_profile.inc（sdk/gen_profile.py 生成），
// 并已在 ovp::Init 中按 PE 指纹 + 函数前导字节校验；校验失败返回 0。

struct Ent {
    uintptr_t p = 0;
    uintptr_t k = 0;
    std::string name;   // 类名
    std::string label;  // 可读名
    float x = 0, y = 0, z = 0;
};

struct State {
    bool ready = false;
    uintptr_t avatar_mgr = 0, monster_mgr = 0;
    std::vector<Ent> monsters, avatars;
    bool cam_ok = false;
    bool vp_valid = false;
    uintptr_t camera = 0;
    float vp[16] = { 0 };
    float w = 0, h = 0;
    float cam_pos[3] = { 0, 0, 0 };
    char msg[160] = "未初始化";
    uint64_t t = 0;
};

inline uintptr_t mgr_from(ov::Data& d, uint32_t g_rva, uintptr_t want_klass) {
    if (!d.ready_ok() || !want_klass) return 0;
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
    uintptr_t length = 0;
    if (!d.rd(items + 0x18, length) || length < (uintptr_t)size) return false;
    if (out.capacity() < (size_t)size) out.reserve((size_t)size);
    for (int32_t i = 0; i < size; ++i) {
        uintptr_t p = 0;
        if (!d.rd(items + 0x20 + (uintptr_t)i * 8, p) || !p) continue;
        out.push_back(p);
    }
    return true;
}

inline void monster_label(ov::Data& d, uintptr_t p, const std::string& fallback, std::string& out) {
    char id[64] = {}, arch[64] = {}, beh[64] = {};
    const bool have_id = d.read_il2str(p, 0x2C0, id, sizeof id);
    const bool have_arch = d.read_il2str(p, 0x878, arch, sizeof arch);
    if (have_id && have_arch) {
        out.assign(id);
        out.push_back(' ');
        out.append(arch);
    } else if (have_id) {
        out.assign(id);
    } else if (have_arch) {
        out.assign(arch);
    } else if (d.read_il2str(p, 0x830, beh, sizeof beh)) {
        out.assign(beh);
    } else {
        out = fallback;
    }
}

inline void fill_ents(ov::Data& d, const std::vector<uintptr_t>& ps, std::vector<Ent>& out, bool monster) {
    out.clear();
    if (out.capacity() < ps.size()) out.reserve(ps.size());
    for (uintptr_t p : ps) {
        Ent e;
        e.p = p;
        if (!d.rd(p, e.k) || !e.k) continue;
        e.name = d.name_of(e.k);
        if (monster) monster_label(d, p, e.name, e.label);
        else e.label = e.name;
        if (!d.rd(p + 0x734, e.x)) continue;
        if (!d.rd(p + 0x738, e.y)) continue;
        if (!d.rd(p + 0x73C, e.z)) continue;
        if (!std::isfinite(e.x) || !std::isfinite(e.y) || !std::isfinite(e.z)) continue;
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
    const float lim = 4.0f;
    if (nx < -lim) nx = -lim;
    else if (nx > lim) nx = lim;
    if (ny < -lim) ny = -lim;
    else if (ny > lim) ny = lim;
    sx = (nx * 0.5f + 0.5f) * W;
    sy = (0.5f - ny * 0.5f) * H;
    return true;
}

inline void cam_pos_from_v(const float V[16], float out[3]) {
    const float tx = V[12], ty = V[13], tz = V[14];
    out[0] = -(V[0] * tx + V[4] * ty + V[8] * tz);
    out[1] = -(V[1] * tx + V[5] * ty + V[9] * tz);
    out[2] = -(V[2] * tx + V[6] * ty + V[10] * tz);
    if (!(out[0] == out[0]) || !(out[1] == out[1]) || !(out[2] == out[2])) {
        out[0] = out[1] = out[2] = 0;
    }
}

inline void invalidate_camera(State& s, const char* reason) {
    s.cam_ok = false;
    s.vp_valid = false;
    s.camera = 0;
    s.w = s.h = 0;
    memset(s.vp, 0, sizeof s.vp);
    memset(s.cam_pos, 0, sizeof s.cam_pos);
    lstrcpynA(s.msg, reason, (int)sizeof s.msg);
}

// Call both camera stages in one window-thread tick, never with last frame's object.
inline bool cam_slow(ov::Data& d, State& s) {
    invalidate_camera(s, "等待当前场景相机");
    if (!d.ready_ok()) return false;
    const uint32_t rv_main = ovp::Rva(ovp::SymCameraMain);
    const uint32_t rv_w = ovp::Rva(ovp::SymCameraPixelW);
    const uint32_t rv_h = ovp::Rva(ovp::SymCameraPixelH);
    if (!rv_main || !rv_w || !rv_h) {
        invalidate_camera(s, "profile 相机符号未通过校验");
        return false;
    }
    typedef void* (*GetMain)(void*);
    typedef int (*GetInt)(void*, void*);
    uintptr_t ua = d.ua;
    void* cam = ((GetMain)(ua + rv_main))(nullptr);
    uintptr_t native_camera = 0;
    if (!cam || !d.rd((uintptr_t)cam + 0x10, native_camera) || !native_camera) {
        invalidate_camera(s, "当前场景无有效相机");
        return false;
    }
    int w = ((GetInt)(ua + rv_w))(cam, nullptr);
    int h = ((GetInt)(ua + rv_h))(cam, nullptr);
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
        invalidate_camera(s, "像素尺寸无效");
        return false;
    }
    s.camera = (uintptr_t)cam;
    s.w = (float)w;
    s.h = (float)h;
    return true;
}

inline bool cam_fast(ov::Data& d, State& s) {
    if (!s.camera) return false;
    uintptr_t native_camera = 0;
    if (!d.ready_ok() || !d.rd(s.camera + 0x10, native_camera) || !native_camera) {
        invalidate_camera(s, "当前场景相机已释放");
        return false;
    }
    const uint32_t rv_w2c = ovp::Rva(ovp::SymCameraW2c);
    const uint32_t rv_proj = ovp::Rva(ovp::SymCameraProj);
    if (!rv_w2c || !rv_proj) {
        invalidate_camera(s, "profile 相机矩阵符号未通过校验");
        return false;
    }
    typedef void (*GetMat)(void*, void*, void*);
    uintptr_t ua = d.ua;
    float v[16] = { 0 }, p[16] = { 0 };
    ((GetMat)(ua + rv_w2c))((void*)s.camera, v, nullptr);
    ((GetMat)(ua + rv_proj))((void*)s.camera, p, nullptr);
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(v[i]) || !std::isfinite(p[i]) ||
            std::fabs(v[i]) > 1e7f || std::fabs(p[i]) > 1e7f) {
            invalidate_camera(s, "当前相机矩阵无效");
            return false;
        }
    }
    mul_m4(p, v, s.vp);
    s.vp_valid = true;
    s.cam_ok = true;
    cam_pos_from_v(v, s.cam_pos);
    wsprintfA(s.msg, "相机 %p %dx%d", (void*)s.camera, (int)s.w, (int)s.h);
    return true;
}

inline void refresh_ents(ov::Data& d, State& s) {
    const uint32_t ga = ovp::Rva(ovp::SymAvatarMgrGlobal);
    const uint32_t gm = ovp::Rva(ovp::SymMonsterMgrGlobal);
    s.avatar_mgr = ga ? mgr_from(d, ga, d.find("AvatarManager")) : 0;
    s.monster_mgr = gm ? mgr_from(d, gm, d.find("MonsterManager")) : 0;

    std::vector<uintptr_t> ps;
    if (s.avatar_mgr) {
        uintptr_t lst = 0;
        if (d.rd(s.avatar_mgr + 0x50, lst)) {
            read_list(d, lst, ps);
            fill_ents(d, ps, s.avatars, false);
        } else {
            s.avatars.clear();
        }
    } else {
        s.avatars.clear();
    }

    if (s.monster_mgr) {
        uintptr_t lst = 0;
        if (d.rd(s.monster_mgr + 0x18, lst)) {
            read_list(d, lst, ps);
            fill_ents(d, ps, s.monsters, true);
        } else {
            s.monsters.clear();
        }
    } else {
        s.monsters.clear();
    }

    s.ready = s.avatar_mgr || s.monster_mgr;
    if (!s.ready) lstrcpyA(s.msg, "管理器链为空（还没进关卡？）");
}

inline void refresh(ov::Data& d, State& s) {
    if (!d.ready_ok()) return;
    d.clear_seg_cache();
    s.t = GetTickCount64();
    invalidate_camera(s, "等待战斗场景");
    refresh_ents(d, s);
    // No engine calls in the lobby/loading screen, or using last tick's camera.
    if (!s.avatars.empty() && cam_slow(d, s)) cam_fast(d, s);
}

static uint8_t g_behit_backup[12] = { 0 };
static bool g_behit_on = false;
static uint8_t g_ultra_bak[ovp::kUltraCount][3] = {};
static bool g_ultra_on[ovp::kUltraCount] = {};
static uint8_t g_sp_bak[3] = { 0 };
static bool g_sp_on = false;
static uint8_t g_cd_ui_bak[3] = { 0 };
static bool g_cd_ui_on = false;
static uint8_t g_cd_uican_bak[3] = { 0 };
static bool g_cd_uican_on = false;
static uint8_t g_cd_actor_bak[3] = { 0 };
static bool g_cd_actor_on = false;
static uint8_t g_crit_main_bak[2] = { 0 };
static bool g_crit_main_on = false;
static uint8_t g_crit_phys_bak[2] = { 0 };
static bool g_crit_phys_on = false;
static uint8_t g_crit_elem_bak[2] = { 0 };
static bool g_crit_elem_on = false;
static bool g_atk_speed_on = false;

extern "C" void __cdecl behit_stub(void) { return; }

inline bool write_code(uintptr_t at, const void* src, size_t n, void* bak) {
    DWORD old = 0;
    if (!VirtualProtect((void*)at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    if (bak) memcpy(bak, (void*)at, n);
    memcpy((void*)at, src, n);
    VirtualProtect((void*)at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)at, n);
    return true;
}

inline bool patch_ret_true(uintptr_t at, uint8_t* bak, bool& flag, bool on) {
    if (on == flag) return true;
    if (on) {
        const uint8_t bytes[3] = { 0xB0, 0x01, 0xC3 };
        if (!write_code(at, bytes, 3, bak)) return false;
    } else {
        if (!write_code(at, bak, 3, nullptr)) return false;
    }
    flag = on;
    return true;
}

inline bool patch_ret_false(uintptr_t at, uint8_t* bak, bool& flag, bool on) {
    if (on == flag) return true;
    if (on) {
        const uint8_t bytes[3] = { 0x31, 0xC0, 0xC3 };
        if (!write_code(at, bytes, 3, bak)) return false;
    } else {
        if (!write_code(at, bak, 3, nullptr)) return false;
    }
    flag = on;
    return true;
}

inline bool god_mode(ov::Data& d, bool on) {
    const uint32_t rv = ovp::Rva(ovp::SymBeHit);
    if (!rv) return false;
    uintptr_t t = d.ua + rv;
    if (on == g_behit_on) return true;
    if (on) {
        uint8_t patch[12];
        patch[0] = 0x48; patch[1] = 0xB8;
        *(uint64_t*)&patch[2] = (uint64_t)&behit_stub;
        patch[10] = 0xFF; patch[11] = 0xE0;
        if (!write_code(t, patch, 12, g_behit_backup)) return false;
    } else {
        if (!write_code(t, g_behit_backup, 12, nullptr)) return false;
    }
    g_behit_on = on;
    return true;
}

inline bool infinite_sp(ov::Data& d, bool on) {
    if (on == g_sp_on) return true;
    const uint32_t rv_sp = ovp::Rva(ovp::SymIsSpEnough);
    if (!rv_sp) return false;
    bool ok = true;
    if (!patch_ret_true(d.ua + rv_sp, g_sp_bak, g_sp_on, on)) ok = false;
    for (int i = 0; i < ovp::kUltraCount; ++i) {
        const uint32_t rv = ovp::UltraRva(i);
        if (!rv) { ok = false; continue; }
        if (!patch_ret_true(d.ua + rv, g_ultra_bak[i], g_ultra_on[i], on)) ok = false;
    }
    if (!ok) {
        for (int i = 0; i < ovp::kUltraCount; ++i) {
            const uint32_t rv = ovp::UltraRva(i);
            if (rv && g_ultra_on[i] == on) patch_ret_true(d.ua + rv, g_ultra_bak[i], g_ultra_on[i], !on);
        }
        if (g_sp_on == on) patch_ret_true(d.ua + rv_sp, g_sp_bak, g_sp_on, !on);
        return false;
    }
    return true;
}

inline bool no_skill_cd(ov::Data& d, bool on) {
    const uint32_t rv_ui = ovp::Rva(ovp::SymUiSkillInCd);
    const uint32_t rv_can = ovp::Rva(ovp::SymUiCanUseSkill);
    const uint32_t rv_actor = ovp::Rva(ovp::SymAvatarCanUseSkill);
    if (!rv_ui || !rv_can || !rv_actor) return false;
    bool ok = true;
    if (!patch_ret_false(d.ua + rv_ui, g_cd_ui_bak, g_cd_ui_on, on)) ok = false;
    if (!patch_ret_true(d.ua + rv_can, g_cd_uican_bak, g_cd_uican_on, on)) ok = false;
    if (!patch_ret_true(d.ua + rv_actor, g_cd_actor_bak, g_cd_actor_on, on)) ok = false;
    if (!ok) {
        if (g_cd_ui_on == on) patch_ret_false(d.ua + rv_ui, g_cd_ui_bak, g_cd_ui_on, !on);
        if (g_cd_uican_on == on) patch_ret_true(d.ua + rv_can, g_cd_uican_bak, g_cd_uican_on, !on);
        if (g_cd_actor_on == on) patch_ret_true(d.ua + rv_actor, g_cd_actor_bak, g_cd_actor_on, !on);
        return false;
    }
    return true;
}

// 把暴击随机比较的 jbe 改成 NOP，条件分支永远落入暴击处理。
inline bool patch_crit_jbe(uintptr_t at, uint8_t* bak, bool& flag, bool on) {
    if (on == flag) return true;
    if (on) {
        uint8_t cur[2] = { 0 };
        SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (void*)at, cur, 2, &got) || got != 2)
            return false;
        if (cur[0] != 0x76) return false;  // 只接受 jbe rel8，构建不匹配时不动代码
        const uint8_t nop[2] = { 0x90, 0x90 };
        if (!write_code(at, nop, 2, bak)) return false;
    } else {
        if (!write_code(at, bak, 2, nullptr)) return false;
    }
    flag = on;
    return true;
}

inline bool hundred_crit(ov::Data& d, bool on) {
    const uint32_t rv_main = ovp::Rva(ovp::SymCritMain);
    const uint32_t rv_phys = ovp::Rva(ovp::SymCritPhys);
    const uint32_t rv_elem = ovp::Rva(ovp::SymCritElem);
    if (!rv_main || !rv_phys || !rv_elem) return false;
    bool ok = true;
    if (!patch_crit_jbe(d.ua + rv_main, g_crit_main_bak, g_crit_main_on, on)) ok = false;
    if (!patch_crit_jbe(d.ua + rv_phys, g_crit_phys_bak, g_crit_phys_on, on)) ok = false;
    if (!patch_crit_jbe(d.ua + rv_elem, g_crit_elem_bak, g_crit_elem_on, on)) ok = false;
    if (!ok) {
        if (g_crit_main_on == on) patch_crit_jbe(d.ua + rv_main, g_crit_main_bak, g_crit_main_on, !on);
        if (g_crit_phys_on == on) patch_crit_jbe(d.ua + rv_phys, g_crit_phys_bak, g_crit_phys_on, !on);
        if (g_crit_elem_on == on) patch_crit_jbe(d.ua + rv_elem, g_crit_elem_bak, g_crit_elem_on, !on);
        return false;
    }
    return true;
}

// 倍攻：按倍率覆盖角色 Animator 播放速度，关闭时恢复游戏原速。
inline bool attack_speed(ov::Data& d, const State& s, bool on, float mult) {
    if (!on) {
        if (!g_atk_speed_on) return true;
        const uint32_t rv = ovp::Rva(ovp::SymAnimSpeedCancel);
        if (!rv) return false;
        typedef void (*CancelFn)(void*);
        for (const Ent& e : s.avatars) {
            if (!e.p) continue;
            uintptr_t k = 0;
            if (!d.rd(e.p, k) || k != e.k) continue;
            uintptr_t animator = 0;
            if (!d.rd(e.p + 0x270, animator) || !animator) continue;
            ((CancelFn)(d.ua + rv))((void*)e.p);
        }
        g_atk_speed_on = false;
        return true;
    }

    const uint32_t rv = ovp::Rva(ovp::SymAnimSpeedApply);
    if (!rv) return false;
    if (mult < 1.0f) mult = 1.0f;
    if (mult > 10.0f) mult = 10.0f;

    typedef void (*ApplyFn)(void*, float);
    int n = 0;
    for (const Ent& e : s.avatars) {
        if (!e.p) continue;
        uintptr_t k = 0;
        if (!d.rd(e.p, k) || k != e.k) continue;
        uintptr_t animator = 0;
        if (!d.rd(e.p + 0x270, animator) || !animator) continue;
        ((ApplyFn)(d.ua + rv))((void*)e.p, mult);
        ++n;
    }
    g_atk_speed_on = n > 0;
    return n > 0;
}

inline bool sp_applied() { return g_sp_on; }
inline bool cd_applied() { return g_cd_ui_on && g_cd_uican_on && g_cd_actor_on; }
inline bool crit_applied() { return g_crit_main_on && g_crit_phys_on && g_crit_elem_on; }
inline bool atk_speed_applied() { return g_atk_speed_on; }
inline bool god_applied() { return g_behit_on; }

inline int64_t kill_all(ov::Data& d, const State& s) {
    const uint32_t rv = ovp::Rva(ovp::SymDieNow);
    if (!rv) return 0;
    typedef void (*DieNow)(void*, bool, void*);
    int n = 0;
    for (const Ent& e : s.monsters) {
        if (!e.p) continue;
        uintptr_t klass = 0;
        if (!d.rd(e.p, klass) || klass != e.k) continue;
        ((DieNow)(d.ua + rv))((void*)e.p, true, nullptr);
        if (++n >= 64) break;
    }
    return n;
}

}
