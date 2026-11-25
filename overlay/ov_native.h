#pragma once
#include "ov_data.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace ovn {

// 所有 RVA 都来自 bh3_profile.inc（sdk/gen_profile.py 生成），
// 并已在 ovp::Init 中按 PE 指纹 + 函数前导字节校验；校验失败返回 0。

struct Ent {
    uintptr_t p = 0;
    uintptr_t k = 0;
    std::string name;   // 类名
    std::string label;  // 可读名
    int cat = 0;        // 0宝箱 1采集 2挑战 3数字游戏 4传送 5围栏 6其它交互
    float x = 0, y = 0, z = 0;
};

struct State {
    bool ready = false;
    uintptr_t avatar_mgr = 0, monster_mgr = 0;
    std::vector<Ent> monsters, avatars;
    std::vector<Ent> gadgets, npcs, props;
    uintptr_t gadget_mgr = 0;
    uint64_t gadget_t = 0;
    int gadget_list_n = 0;
    int gadget_cap_n = 0;
    int gadget_read_n = 0;
    int gadget_drop_inst = 0;
    int gadget_drop_cls = 0;
    int gadget_drop_cfg = 0;
    int gadget_drop_coord = 0;
    int gadget_fail = 0;
    float player_pos[3] = { 0, 0, 0 };
    bool player_ok = false;
    uint64_t player_t = 0;
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

// 容量以数组 length 为准；硬上限只挡野数据
inline constexpr uintptr_t kListHardCeiling = 1u << 20;

inline bool read_list(ov::Data& d, uintptr_t list, std::vector<uintptr_t>& out,
                      size_t* out_size = nullptr, size_t* out_length = nullptr)
{
    out.clear();
    if (out_size) *out_size = 0;
    if (out_length) *out_length = 0;
    if (!list) return false;
    uintptr_t items = 0;
    int32_t size = 0;
    if (!d.rd(list + 0x10, items) || !d.rd(list + 0x18, size)) return false;
    if (!items || size < 0) return false;
    uintptr_t length = 0;
    if (!d.rd(items + 0x18, length) || length < (uintptr_t)size) return false;
    if (out_size) *out_size = (size_t)size;
    if (out_length) *out_length = (size_t)length;
    if (length > kListHardCeiling) return false;  // 野数据：宁可失败也不遍历几十亿次
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
    // 不同场景/相机层返回的矩阵可能是行主序或列主序：
    // 行主序平移在 V[3],V[7],V[11]；列主序在 V[12],V[13],V[14]。
    // 用较大的那一组，避免把平移读成 0 导致距离变成“到原点的距离”。
    const float row_t = fabsf(V[3]) + fabsf(V[7]) + fabsf(V[11]);
    const float col_t = fabsf(V[12]) + fabsf(V[13]) + fabsf(V[14]);
    float tx, ty, tz;
    if (row_t > col_t && row_t > 1e-4f) {
        tx = V[3]; ty = V[7]; tz = V[11];
    } else {
        tx = V[12]; ty = V[13]; tz = V[14];
    }
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

// 按 ActionGraphPath 归类；返回 false 表示该类监听器不画（Empty）。
inline bool classify_gadget(const char* graph, int& cat, std::string& label)
{
    cat = 6;
    label = "交互物";
    if (!graph) return true;
    auto has = [&](const char* k) { return std::strstr(graph, k) != nullptr; };
    if (has("Empty")) return false;
    if (has("Chest") || has("Treasure") || has("Box")) {
        cat = 0;
        if (has("Elite")) label = "精英宝箱";
        else if (has("Rare") || has("Epic")) label = "稀有宝箱";
        else if (has("Luxury") || has("Legend")) label = "豪华宝箱";
        else label = "宝箱";
    } else if (has("Collect") || has("Gather") || has("Pick")) {
        cat = 1; label = "采集物";
    } else if (has("Challenge")) {
        cat = 2; label = "挑战点";
    } else if (has("Game") || has("Puzzle") || has("Number") || has("Digital")) {
        cat = 3; label = "数字游戏";
    } else if (has("Teleport")) {
        cat = 4; label = "传送点";
    } else if (has("Fence") || has("Ring")) {
        cat = 5; label = "围栏";
    }
    return true;
}

struct RegLayout {
    uint32_t host_rva = 0;
    uint32_t dict_off = 0;
};

inline bool resolve_reg_layout(ov::Data& d, RegLayout& out)
{
    const uint32_t rv = ovp::Rva(ovp::SymGetSingletonImpl);
    if (!rv) return false;
    const uintptr_t fn = d.ua + rv;
    uint8_t b[96] = {};
    if (!d.readable(fn, sizeof b) || !ovp::ReadBytes(fn, b, sizeof b)) return false;
    for (size_t i = 0; i + 9 < sizeof b; ++i)
    {
        if (b[i] != 0x48 || b[i + 1] != 0x8B || b[i + 2] != 0x05) continue;
        const int32_t disp = (int32_t)(b[i + 3] | (b[i + 4] << 8) |
                                       (b[i + 5] << 16) | ((uint32_t)b[i + 6] << 24));
        const uint64_t next = (uint64_t)rv + i + 7;
        const uint64_t target = next + (int64_t)disp;
        if (target < 0x1000 || target > 0x1284B000ULL) continue;
        const size_t j = i + 7;
        if (j + 7 >= sizeof b || b[j] != 0x48 || b[j + 1] != 0x8B) continue;
        if ((b[j + 2] & 0xC7) != 0x80) continue;
        const int32_t off = (int32_t)(b[j + 3] | (b[j + 4] << 8) |
                                      (b[j + 5] << 16) | ((uint32_t)b[j + 6] << 24));
        if (off == 0x3A3D0)
        {
            out.host_rva = (uint32_t)target;
            out.dict_off = (uint32_t)off;
            return true;
        }
    }
    return false;
}

inline uintptr_t registry_instance(ov::Data& d, const char* want)
{
    if (!d.ready_ok() || !want) return 0;
    static RegLayout layout = { 0x2555EB0u, 0x3A3D0u };
    static uint64_t last_t = 0;
    const uint64_t now = GetTickCount64();
    if (now - last_t >= 1000)
    {
        RegLayout fresh;
        if (resolve_reg_layout(d, fresh)) layout = fresh;
        last_t = now;
    }
    uintptr_t host = 0, dict = 0;
    if (!d.rd(d.ua + layout.host_rva, host) || !host) return 0;
    if (!d.rd(host + layout.dict_off, dict) || !dict) return 0;
    uintptr_t k = 0;
    if (!d.rd(dict, k) || !k) return 0;
    char cn[64] = {};
    if (!d.copy_name(k, cn, sizeof cn) || std::strcmp(cn, "Dictionary`2") != 0) return 0;
    uintptr_t entries = 0;
    int32_t n = 0;
    if (!d.rd(dict + 0x20, entries) || !entries) return 0;
    if (!d.rd(entries + 0x18, n) || n <= 0 || n > 4096) return 0;
    const uintptr_t data = entries + 0x20;
    for (int32_t i = 0; i < n; ++i) {
        const uintptr_t e = data + (uintptr_t)i * 0x18;
        uintptr_t key = 0;
        if (!d.rd(e + 8, key) || !key) continue;
        int32_t len = 0;
        if (!d.rd(key + 0x10, len) || len <= 0 || len > 96) continue;
        char tmp[100] = {};
        bool ok = true;
        for (int32_t j = 0; j < len; ++j) {
            uint16_t ch = 0;
            if (!d.rd(key + 0x14 + (uintptr_t)j * 2, ch) || ch < 0x20 || ch > 0x7E) {
                ok = false;
                break;
            }
            tmp[j] = (char)ch;
        }
        if (!ok || std::strcmp(tmp, want) != 0) continue;
        uintptr_t val = 0;
        return d.rd(e + 0x10, val) ? val : 0;
    }
    return 0;
}

// PJMSGadgetManager._allGadgetActors：注册表取管理器实例，实例 +0x38 是 List。
// actor +0x98/0x9C/0xA0 为坐标；+0xD8 是 PJMSGadgetConfig，+0x20 是 ActionGraphPath。
inline void refresh_gadget_mgr(ov::Data& d, State& s)
{
    s.gadget_list_n = s.gadget_cap_n = s.gadget_read_n = 0;
    s.gadget_drop_inst = s.gadget_drop_cls = s.gadget_drop_cfg = s.gadget_drop_coord = 0;
    s.gadget_fail = 0;

    if (!d.ready_ok()) { s.gadget_mgr = 0; s.gadget_fail = 1; return; }
    if (s.t - s.gadget_t >= 1000) {
        s.gadget_mgr = registry_instance(d, "MoleMole.PJMS.PJMSGadgetManager");
        s.gadget_t = s.t;
    }
    if (!s.gadget_mgr) { s.gadget_fail = 1; s.gadgets.clear(); return; }
    uintptr_t list = 0;
    std::vector<uintptr_t> ps;
    size_t lsize = 0, llen = 0;
    if (!d.rd(s.gadget_mgr + 0x38, list) || !list ||
        !read_list(d, list, ps, &lsize, &llen)) {
        // 场景切换后实例失效：清空并等下一次注册表查找。
        s.gadget_mgr = 0;
        s.gadgets.clear();
        s.gadget_list_n = (int)lsize;
        s.gadget_cap_n = (int)llen;
        s.gadget_fail = (llen > kListHardCeiling) ? 3 : 2;
        return;
    }
    s.gadget_list_n = (int)lsize;
    s.gadget_cap_n = (int)llen;
    s.gadget_read_n = (int)ps.size();
    std::vector<Ent> gs;
    gs.reserve(ps.size());
    // 配置/分类只随 actor 变化，缓存，避免每帧逐字符读字符串拖帧。
    struct GInfo {
        uintptr_t cfg = 0;
        bool draw = false;
        int cat = 6;
        int drop = 0;
        std::string name, label;
    };
    static std::unordered_map<uintptr_t, GInfo> info_cache;
    static uintptr_t info_mgr = 0;
    if (info_mgr != s.gadget_mgr || info_cache.size() > 20000) {
        info_cache.clear();
        info_mgr = s.gadget_mgr;
    }
    for (uintptr_t p : ps) {
        uintptr_t k = 0;
        if (!d.rd(p, k) || !k) { ++s.gadget_drop_inst; continue; }
        // 未实例化/已卸载的 actor（LOD、占位）没有实体，旧 Lua 的 FindObjectsOfType 也看不到。
        uintptr_t ent = 0, native = 0;
        if (!d.rd(p + 0x28, ent) || !ent) { ++s.gadget_drop_inst; continue; }
        if (!d.rd(ent + 0x10, native) || !native) { ++s.gadget_drop_inst; continue; }
        uintptr_t cfg = 0;
        if (!d.rd(p + 0xD8, cfg)) cfg = 0;
        auto it = info_cache.find(p);
        if (it == info_cache.end() || it->second.cfg != cfg) {
            GInfo gi;
            gi.cfg = cfg;
            gi.drop = 1;  // 默认：类名/配置不符
            char cls[96] = {};
            if (cfg && d.copy_name(k, cls, sizeof cls) && std::strstr(cls, "PJMSGadgetActor")) {
                char pn[96] = {}, lt[96] = {}, ag[128] = {};
                const bool has_pn = d.read_il2str(cfg, 0x10, pn, sizeof pn);
                const bool has_lt = d.read_il2str(cfg, 0x18, lt, sizeof lt);
                const bool has_ag = d.read_il2str(cfg, 0x20, ag, sizeof ag);
                // PointName/LOD/AG 带 Empty 的都是占位，和旧 Lua 的 name 过滤一致；
                // Listener 是不可见触发体，不是可交互对象。
                const bool bad = (has_pn && std::strstr(pn, "Empty")) ||
                                 (has_lt && std::strstr(lt, "Empty")) ||
                                 (has_ag && std::strstr(ag, "Empty")) ||
                                 (has_ag && std::strstr(ag, "Listener"));
                if (bad) {
                    gi.drop = 2;
                } else {
                    gi.name = has_pn ? pn : cls;
                    if (has_ag) {
                        gi.draw = classify_gadget(ag, gi.cat, gi.label);
                        if (!gi.draw) gi.drop = 3;  // ActionGraphPath 判为 Empty
                    } else {
                        gi.cat = 6;
                        gi.label = "交互物";
                        gi.draw = true;
                    }
                }
            }
            if (it == info_cache.end()) it = info_cache.emplace(p, std::move(gi)).first;
            else it->second = std::move(gi);
        }
        if (!it->second.draw) {
            if (it->second.drop == 1) ++s.gadget_drop_cls;
            else ++s.gadget_drop_cfg;  // 2=Empty/Listener，3=AG 分类 Empty
            continue;
        }
        Ent e;
        e.p = p;
        e.name = it->second.name;
        e.cat = it->second.cat;
        e.label = it->second.label;
        if (!d.rd(p + 0x98, e.x) || !d.rd(p + 0x9C, e.y) || !d.rd(p + 0xA0, e.z)) {
            ++s.gadget_drop_coord; continue;
        }
        if (!std::isfinite(e.x) || !std::isfinite(e.y) || !std::isfinite(e.z)) {
            ++s.gadget_drop_coord; continue;
        }
        if (std::fabs(e.x) > 1e5f || std::fabs(e.y) > 1e5f || std::fabs(e.z) > 1e5f) {
            ++s.gadget_drop_coord; continue;
        }
        gs.push_back(std::move(e));
    }
    s.gadgets.swap(gs);
}

// 玩家真实世界坐标：用 BaseMonoEntity.get_RootNodePosition，避免距离跟相机转动变化。
inline void refresh_player_pos(ov::Data& d, State& s) {
    if (!d.ua || s.avatars.empty()) {
        s.player_ok = false;
        return;
    }
    if (s.player_t && s.t - s.player_t < 100) return;
    s.player_t = s.t;
    const uint32_t rv_pos = ovp::Rva(ovp::SymRootNodePosition);
    if (!rv_pos) return;
    typedef void (*GetPosFn)(void*, void*, void*);
    const uint32_t rv_local = ovp::Rva(ovp::SymGetLocalAvatar);
    if (s.avatar_mgr && rv_local) {
        typedef void* (*GetLocalFn)(void*, void*);
        uintptr_t local = (uintptr_t)((GetLocalFn)(d.ua + rv_local))((void*)s.avatar_mgr, nullptr);
        uintptr_t lk = 0;
        if (local && d.rd(local, lk) && lk) {
            char lname[128] = {};
            if (d.copy_name(lk, lname, sizeof lname) && std::strstr(lname, "Avatar")) {
                float p[3] = {};
                ((GetPosFn)(d.ua + rv_pos))((void*)p, (void*)local, nullptr);
                if (std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]) &&
                    std::fabs(p[0]) + std::fabs(p[1]) + std::fabs(p[2]) > 0.01f) {
                    memcpy(s.player_pos, p, sizeof p);
                    s.player_ok = true;
                    return;
                }
            }
        }
    }
    const bool cam_ref = s.cam_ok &&
        (std::fabs(s.cam_pos[0]) + std::fabs(s.cam_pos[1]) + std::fabs(s.cam_pos[2])) > 0.01f;
    if (cam_ref && s.player_ok) {
        const float px = s.player_pos[0] - s.cam_pos[0];
        const float py = s.player_pos[1] - s.cam_pos[1];
        const float pz = s.player_pos[2] - s.cam_pos[2];
        if (px * px + py * py + pz * pz > 225.0f) s.player_ok = false;
    }
    bool found = false;
    float best[3] = {};
    float best_d2 = 0.0f;
    for (const Ent& a : s.avatars) {
        float p[3] = {};
        ((GetPosFn)(d.ua + rv_pos))((void*)p, (void*)a.p, nullptr);
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) continue;
        if (std::fabs(p[0]) + std::fabs(p[1]) + std::fabs(p[2]) < 0.01f) continue;
        if (std::fabs(p[0]) > 1e5f || std::fabs(p[1]) > 1e5f || std::fabs(p[2]) > 1e5f) continue;
        float d2 = 0.0f;
        if (cam_ref) {
            const float dx = p[0] - s.cam_pos[0];
            const float dy = p[1] - s.cam_pos[1];
            const float dz = p[2] - s.cam_pos[2];
            d2 = dx * dx + dy * dy + dz * dz;
        }
        if (!found || d2 < best_d2) {
            found = true;
            best_d2 = d2;
            memcpy(best, p, sizeof best);
        }
    }
    if (!found) return;
    if (cam_ref && best_d2 > 225.0f) return;
    memcpy(s.player_pos, best, sizeof best);
    s.player_ok = true;
}

inline std::atomic<bool>& tp_req() {
    static std::atomic<bool> v(false);
    return v;
}

inline float* tp_pos() {
    static float v[3] = { 0, 0, 0 };
    return v;
}

inline char* tp_msg() {
    static char v[96] = "";
    return v;
}

inline void request_teleport(float x, float y, float z) {
    float* p = tp_pos();
    p[0] = x;
    p[1] = y;
    p[2] = z;
    tp_req().store(true);
}

// 当前操作中的角色：AvatarManager.GetLocalAvatar，和玩家坐标用的是同一个对象。
// 关卡里 avatar 列表可能有多个（队伍/替身），拿 list[0] 会传到没加载的实体上。
inline uintptr_t local_avatar_ptr(ov::Data& d, State& s) {
    if (!d.ua || !s.avatar_mgr) return 0;
    const uint32_t rv = ovp::Rva(ovp::SymGetLocalAvatar);
    if (!rv) return 0;
    typedef void* (*GetLocalFn)(void*, void*);
    uintptr_t a = (uintptr_t)((GetLocalFn)(d.ua + rv))((void*)s.avatar_mgr, nullptr);
    uintptr_t k = 0;
    char name[128] = {};
    if (!a || !d.rd(a, k) || !k || !d.copy_name(k, name, sizeof name)) return 0;
    if (!std::strstr(name, "Avatar")) return 0;
    return a;
}

inline void do_teleport(ov::Data& d, State& s) {
    if (!tp_req().exchange(false)) return;
    if (!d.ua) {
        lstrcpynA(tp_msg(), "未初始化", 96);
        return;
    }
    uintptr_t avatar = local_avatar_ptr(d, s);
    if (!avatar && !s.avatars.empty()) avatar = s.avatars[0].p;
    if (!avatar) {
        lstrcpynA(tp_msg(), "没有角色实例", 96);
        return;
    }
    const uint32_t rv = ovp::Rva(ovp::SymTeleportTo);
    if (!rv) {
        lstrcpynA(tp_msg(), "传送符号未解析", 96);
        return;
    }
    typedef void (*TpFn)(void*, void*, void*);
    const float* t = tp_pos();
    float pos[3] = { t[0], t[1] + 0.5f, t[2] };
    ((TpFn)(d.ua + rv))((void*)avatar, (void*)pos, nullptr);
    lstrcpynA(tp_msg(), "已传送到目标", 96);
}

inline void refresh_ents(ov::Data& d, State& s) {
    const uint32_t ga = ovp::Rva(ovp::SymAvatarMgrGlobal);
    const uint32_t gm = ovp::Rva(ovp::SymMonsterMgrGlobal);
    s.avatar_mgr = ga ? mgr_from(d, ga, d.find("AvatarManager")) : 0;
    s.monster_mgr = gm ? mgr_from(d, gm, d.find("MonsterManager")) : 0;
    // 单例链拿不到实例（换版本 / 未进关卡时全局槽为空）时，退回按 klass 扫堆找实例。
    if (!s.avatar_mgr) s.avatar_mgr = ovl::AvatarManager();
    if (!s.monster_mgr) s.monster_mgr = ovl::MonsterManager();

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

    refresh_gadget_mgr(d, s);
    do_teleport(d, s);

    s.ready = s.avatar_mgr || s.monster_mgr;
    if (!s.ready) {
        // 区分「还没进关卡」和「版本变了解析不出来」，避免误导。
        const int n_res = ovl::Stats().resolved;
        if (n_res < 20)
            wsprintfA(s.msg, "符号解析 %d/%d，可能已换版本（%s）",
                      n_res, (int)ovp::SymCount, ovl::Stats().note);
        else
            lstrcpyA(s.msg, "管理器链为空（还没进关卡？）");
    }
}

inline void refresh(ov::Data& d, State& s) {
    if (!d.ready_ok()) return;
    d.clear_seg_cache();
    s.t = GetTickCount64();
    invalidate_camera(s, "等待战斗场景");
    refresh_ents(d, s);
    // No engine calls in the lobby/loading screen, or using last tick's camera.
    if (!s.avatars.empty() && cam_slow(d, s)) cam_fast(d, s);
    refresh_player_pos(d, s);
}

static uint8_t g_behit_backup[12] = { 0 };
static bool g_behit_on = false;
static uint8_t g_ultra_bak[ovp::kUltraCount][3] = {};
static bool g_ultra_on[ovp::kUltraCount] = {};
static uint8_t g_sp_bak[3] = { 0 };
static bool g_sp_on = false;
static uint8_t g_sp_cost_bak[4] = { 0 };
static bool g_sp_cost_on = false;
static uint8_t g_cd_ui_bak[3] = { 0 };
static bool g_cd_ui_on = false;
static uint8_t g_cd_uican_bak[3] = { 0 };
static bool g_cd_uican_on = false;
static uint8_t g_cd_actor_bak[3] = { 0 };
static bool g_cd_actor_on = false;
static uint8_t g_actor_cd_bak[3] = { 0 };
static bool g_actor_cd_on = false;
static uint8_t g_cd_val_bak[4] = { 0 };
static bool g_cd_val_on = false;
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

// 浮点返回 0：xorps xmm0,xmm0; ret
inline bool patch_ret_zero_float(uintptr_t at, uint8_t* bak, bool& flag, bool on) {
    if (on == flag) return true;
    if (on) {
        const uint8_t bytes[4] = { 0x0F, 0x57, 0xC0, 0xC3 };
        if (!write_code(at, bytes, 4, bak)) return false;
    } else {
        if (!write_code(at, bak, 4, nullptr)) return false;
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
    if (on == g_sp_on && on == g_sp_cost_on) return true;
    const uint32_t rv_sp = ovp::Rva(ovp::SymIsSpEnough);
    const uint32_t rv_cost = ovp::Rva(ovp::SymGetSkillSpCost);
    if (!rv_sp || !rv_cost) return false;
    bool ok = true;
    if (!patch_ret_true(d.ua + rv_sp, g_sp_bak, g_sp_on, on)) ok = false;
    if (!patch_ret_zero_float(d.ua + rv_cost, g_sp_cost_bak, g_sp_cost_on, on)) ok = false;
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
        if (g_sp_cost_on == on) patch_ret_zero_float(d.ua + rv_cost, g_sp_cost_bak, g_sp_cost_on, !on);
        if (g_sp_on == on) patch_ret_true(d.ua + rv_sp, g_sp_bak, g_sp_on, !on);
        return false;
    }
    return true;
}

inline bool no_skill_cd(ov::Data& d, bool on) {
    const uint32_t rv_ui = ovp::Rva(ovp::SymUiSkillInCd);
    const uint32_t rv_can = ovp::Rva(ovp::SymUiCanUseSkill);
    const uint32_t rv_actor = ovp::Rva(ovp::SymAvatarCanUseSkill);
    const uint32_t rv_actor_cd = ovp::Rva(ovp::SymAvatarSkillInCd);
    const uint32_t rv_cd_val = ovp::Rva(ovp::SymGetSkillCd);
    if (!rv_ui || !rv_can || !rv_actor || !rv_actor_cd || !rv_cd_val) return false;
    if (on == g_cd_ui_on && on == g_cd_uican_on && on == g_cd_actor_on &&
        on == g_actor_cd_on && on == g_cd_val_on) return true;
    bool ok = true;
    if (!patch_ret_false(d.ua + rv_ui, g_cd_ui_bak, g_cd_ui_on, on)) ok = false;
    if (!patch_ret_true(d.ua + rv_can, g_cd_uican_bak, g_cd_uican_on, on)) ok = false;
    if (!patch_ret_true(d.ua + rv_actor, g_cd_actor_bak, g_cd_actor_on, on)) ok = false;
    if (!patch_ret_false(d.ua + rv_actor_cd, g_actor_cd_bak, g_actor_cd_on, on)) ok = false;
    if (!patch_ret_zero_float(d.ua + rv_cd_val, g_cd_val_bak, g_cd_val_on, on)) ok = false;
    if (!ok) {
        if (g_cd_ui_on == on) patch_ret_false(d.ua + rv_ui, g_cd_ui_bak, g_cd_ui_on, !on);
        if (g_cd_uican_on == on) patch_ret_true(d.ua + rv_can, g_cd_uican_bak, g_cd_uican_on, !on);
        if (g_cd_actor_on == on) patch_ret_true(d.ua + rv_actor, g_cd_actor_bak, g_cd_actor_on, !on);
        if (g_actor_cd_on == on) patch_ret_false(d.ua + rv_actor_cd, g_actor_cd_bak, g_actor_cd_on, !on);
        if (g_cd_val_on == on) patch_ret_zero_float(d.ua + rv_cd_val, g_cd_val_bak, g_cd_val_on, !on);
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

// 伤害 hook：ResolveAttackDataFinal 收尾处处理怪物倍率/一击必杀与玩家免伤。
// 怪物 Actor 类名 AIOM/HAME；玩家 Actor 类名 PLKH/PIDF/FALI。
static uint8_t g_dmg_bak[13] = { 0 };
static bool g_dmg_on = false;
static void* g_dmg_stub = nullptr;
static volatile float g_dmg_mult = 1.0f;
static volatile uint32_t g_dmg_flags = 0;

static constexpr uint32_t kOhkValueBits = 0x4EFFFFFFu;

static constexpr uint32_t kDmgFlagMult = 1u;
static constexpr uint32_t kDmgFlagGod = 2u;
static constexpr uint32_t kDmgFlagOhk = 4u;

inline bool dmg_build_stub(uintptr_t resume) {
    if (g_dmg_stub) return true;
    static const uint8_t kHead[13] = {
        0xC7, 0x86, 0x9C, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x48, 0x85, 0xFF
    };
    static const uint32_t kOffs[4] = { 0x90, 0x80, 0x9C, 0x64 };
    static const uint32_t kOhkBits = kOhkValueBits;

    uint8_t code[512] = { 0 };
    uint8_t* p = code;
    uint8_t* L_orig = nullptr;
    uint8_t* L_monster = nullptr;
    uint8_t* L_player = nullptr;
    uint8_t* L_ohk = nullptr;

    struct Fix { uint8_t* at; uint8_t** label; };
    Fix fx[24];
    int nfx = 0;

    auto em8 = [&](uint8_t v) { *p++ = v; };
    auto em32 = [&](uint32_t v) { memcpy(p, &v, 4); p += 4; };
    auto em64 = [&](uint64_t v) { memcpy(p, &v, 8); p += 8; };
    auto jmp32 = [&](uint8_t*& lab) { em8(0xE9); fx[nfx].at = p; fx[nfx].label = &lab; ++nfx; p += 4; };
    auto je32 = [&](uint8_t*& lab) { em8(0x0F); em8(0x84); fx[nfx].at = p; fx[nfx].label = &lab; ++nfx; p += 4; };
    auto jne32 = [&](uint8_t*& lab) { em8(0x0F); em8(0x85); fx[nfx].at = p; fx[nfx].label = &lab; ++nfx; p += 4; };
    auto cmp_name = [&](uint32_t v) { em8(0x81); em8(0x38); em32(v); };

    em8(0x48); em8(0x85); em8(0xFF);                // test rdi,rdi
    je32(L_orig);
    em8(0x48); em8(0x8B); em8(0x07);                // rax = klass
    em8(0x48); em8(0x8B); em8(0x40); em8(0x20);     // rax = name
    cmp_name(0x4D4F4941u); je32(L_monster);         // AIOM
    cmp_name(0x454D4148u); je32(L_monster);         // HAME
    cmp_name(0x484B4C50u); je32(L_player);          // PLKH
    cmp_name(0x46444950u); je32(L_player);          // PIDF
    cmp_name(0x494C4146u); je32(L_player);          // FALI
    jmp32(L_orig);

    L_monster = p;
    em8(0x48); em8(0xB8); em64((uint64_t)(uintptr_t)&g_dmg_flags);
    em8(0xF6); em8(0x00); em8(0x04);                // 一击必杀优先
    jne32(L_ohk);
    em8(0xF6); em8(0x00); em8(0x01);                // 伤害倍率
    je32(L_orig);
    em8(0x48); em8(0xB8); em64((uint64_t)(uintptr_t)&g_dmg_mult);
    em8(0xF3); em8(0x0F); em8(0x10); em8(0x00);
    for (int i = 0; i < 4; ++i) {
        const uint32_t off = kOffs[i];
        em8(0xF3); em8(0x0F); em8(0x10); em8(0x8E); em32(off);
        em8(0xF3); em8(0x0F); em8(0x59); em8(0xC8);
        em8(0xF3); em8(0x0F); em8(0x11); em8(0x8E); em32(off);
    }
    jmp32(L_orig);

    L_ohk = p;
    for (int i = 0; i < 4; ++i) {
        em8(0xC7); em8(0x86); em32(kOffs[i]); em32(i == 0 ? kOhkBits : 0u);
    }
    jmp32(L_orig);

    L_player = p;
    em8(0x48); em8(0xB8); em64((uint64_t)(uintptr_t)&g_dmg_flags);
    em8(0xF6); em8(0x00); em8(0x02);                // 真无敌
    je32(L_orig);
    for (int i = 0; i < 4; ++i) {
        em8(0xC7); em8(0x86); em32(kOffs[i]); em32(0);
    }
    jmp32(L_orig);

    L_orig = p;
    memcpy(p, kHead, 13); p += 13;
    em8(0x48); em8(0xB8); em64((uint64_t)resume);
    em8(0xFF); em8(0xE0);

    for (int i = 0; i < nfx; ++i) {
        const int32_t rel = (int32_t)((int64_t)(*fx[i].label - (fx[i].at + 4)));
        memcpy(fx[i].at, &rel, 4);
    }

    void* mem = VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!mem) return false;
    const size_t n = (size_t)(p - code);
    memcpy(mem, code, n);
    FlushInstructionCache(GetCurrentProcess(), mem, n);
    DWORD old = 0;
    VirtualProtect(mem, n, PAGE_EXECUTE_READ, &old);
    g_dmg_stub = mem;
    return true;
}

inline bool damage_hook(ov::Data& d, bool mult_on, float mult, bool god_on, bool ohk_on) {
    if (!(mult >= 1.0f)) mult = 1.0f;
    if (mult > 1000.0f) mult = 1000.0f;
    g_dmg_mult = mult;
    g_dmg_flags = (mult_on ? kDmgFlagMult : 0u) |
                  (god_on ? kDmgFlagGod : 0u) |
                  (ohk_on ? kDmgFlagOhk : 0u);

    const bool want = g_dmg_flags != 0;
    if (want == g_dmg_on) return true;

    const uint32_t rv = ovp::Rva(ovp::SymDmgFinal);
    if (!rv) return false;
    uintptr_t at = d.ua + rv;
    if (want) {
        static const uint8_t kHead[13] = {
            0xC7, 0x86, 0x9C, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x48, 0x85, 0xFF
        };
        uint8_t cur[13] = { 0 };
        SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (void*)at, cur, 13, &got) || got != 13)
            return false;
        if (memcmp(cur, kHead, 13) != 0) return false;
        if (!dmg_build_stub(at + 13)) return false;
        uint8_t patch[13] = { 0x48, 0xB8 };
        const uint64_t stub = (uint64_t)(uintptr_t)g_dmg_stub;
        memcpy(&patch[2], &stub, 8);
        patch[10] = 0xFF; patch[11] = 0xE0; patch[12] = 0x90;
        if (!write_code(at, patch, 13, g_dmg_bak)) return false;
    } else {
        if (!write_code(at, g_dmg_bak, 13, nullptr)) return false;
    }
    g_dmg_on = want;
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

inline bool sp_applied() { return g_sp_on && g_sp_cost_on; }
inline bool cd_applied() { return g_cd_ui_on && g_cd_uican_on && g_cd_actor_on && g_actor_cd_on && g_cd_val_on; }
inline bool crit_applied() { return g_crit_main_on && g_crit_phys_on && g_crit_elem_on; }
inline bool dmg_applied() { return g_dmg_on; }
inline bool ohk_applied() { return g_dmg_on && (g_dmg_flags & kDmgFlagOhk) != 0; }
inline bool god_true_applied() { return g_dmg_on && (g_dmg_flags & kDmgFlagGod) != 0; }
inline bool atk_speed_applied() { return g_atk_speed_on; }
inline bool god_applied() { return g_behit_on && god_true_applied(); }

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
