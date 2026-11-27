#pragma once
#include "ov_data.h"
#include "ov_registry.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <climits>
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
    uintptr_t native = 0;   // +0x28：native 实体。为 0 的是未实例化的模板/占位
    std::string name;   // 可读名（交互物取 PointName）
    std::string label;  // 分类名
    int cat = 0;        // 0宝箱 1采集 2挑战 3数字游戏 4传送 5围栏 6其它交互 7入口
    float x = 0, y = 0, z = 0;
    bool pos_ok = true;   // 坐标无效时仍然保留实体（倍攻/秒杀要用），只是不画
};

struct State {
    bool ready = false;
    uintptr_t avatar_mgr = 0, monster_mgr = 0;
    int avatar_raw = 0, monster_raw = 0;
    std::vector<Ent> monsters, avatars;
    std::unordered_map<uint32_t, uint8_t> rid_kind;  // runtimeID -> 1角色 2怪物
    std::vector<Ent> gadgets, props;
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
    bool player_from_cam = false;
    uintptr_t local_avatar = 0;
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

// 伤害回调里按 BaseActor.runtimeID (+0x28) 查类型，避免按类名扫描。
static State* g_dmg_state = nullptr;

// 容量以数组 length 为准；硬上限只挡野数据
inline constexpr uintptr_t kListHardCeiling = 1u << 20;

// klass 继承判定：+0xD0 = 类型链深度，+0x78 = 从根到自身的类指针数组。
inline bool klass_isa(ov::Data& d, uintptr_t k, uintptr_t target)
{
    if (!k || !target) return false;
    uint8_t kd = 0, td = 0;
    uintptr_t kh = 0, e = 0;
    if (!d.rd(k + 0xD0, kd) || !d.rd(target + 0xD0, td)) return false;
    if (!td || td > kd) return false;
    if (!d.rd(k + 0x78, kh) || !kh) return false;
    if (!d.rd(kh + (uintptr_t)(td - 1) * 8, e)) return false;
    return e == target;
}

inline bool ent_pos_call(ov::Data& d, uintptr_t ent, float out[3])
{
    const uint32_t rv = ovp::Rva(ovp::SymRootNodePosition);
    if (!rv) return false;
    typedef void (*PosFn)(void*, void*, void*);
    float p[3] = { 0, 0, 0 };
    ((PosFn)(d.ua + rv))((void*)p, (void*)ent, nullptr);
    if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) return false;
    memcpy(out, p, sizeof p);
    return true;
}

// 实体坐标：直接用 BaseMonoEntity.get_RootNodePosition，一次调用，不扫任何字段。
inline bool ent_pos(ov::Data& d, uintptr_t ent, float out[3])
{
    return ent_pos_call(d, ent, out);
}

// 角色世界坐标：BaseMonoAnimatorEntity.get_XYZPosition RVA 0x09BE4620。
// 返回 UnityEngine.Vector3 的实例方法在 Win64 下是 (隐藏返回缓冲, this, MethodInfo*)。
inline bool avatar_xyz_call(ov::Data& d, uintptr_t ent, float out[3])
{
    const uint32_t rv = ovp::Rva(ovp::SymAvatarXyz);
    if (!rv || !ent) return false;
    uintptr_t k = 0;
    if (!d.rd(ent, k) || !k) return false;
    typedef void (*PosFn)(void*, void*, void*);
    float p[3] = { 0, 0, 0 };
    ((PosFn)(d.ua + rv))((void*)p, (void*)ent, nullptr);
    if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) return false;
    if (std::fabs(p[0]) + std::fabs(p[1]) + std::fabs(p[2]) < 0.01f) return false;
    memcpy(out, p, sizeof p);
    return true;
}

// AvatarManager._avatarDict(+0x68) 是 Dictionary<uint, BaseMonoAvatar>：
// entries 在 +0x18，长度在 entries+0x18，Entry 步长 0x18，key 在 +8，value 在 +0x10。
inline uintptr_t avatar_dict_lookup(ov::Data& d, uintptr_t dict, uint32_t key)
{
    if (!dict || !key) return 0;
    uintptr_t entries = 0;
    int32_t n = 0;
    if (!d.rd(dict + 0x18, entries) || !entries) return 0;
    if (!d.rd(entries + 0x18, n) || n <= 0 || n > 8192) return 0;
    for (int32_t i = 0; i < n; ++i) {
        const uintptr_t e = entries + 0x20 + (uintptr_t)i * 0x18;
        uint32_t k = 0;
        if (!d.rd(e + 8, k) || k != key) continue;
        uintptr_t v = 0;
        if (d.rd(e + 0x10, v)) return v;
    }
    return 0;
}

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
        uintptr_t nat = 0;
        if (d.rd(p + 0x28, nat)) e.native = nat;
        float pos[3] = {};
        bool ok = d.rd(p + 0x734, pos[0]) && d.rd(p + 0x738, pos[1]) && d.rd(p + 0x73C, pos[2]);
        if (ok && (!std::isfinite(pos[0]) || !std::isfinite(pos[1]) || !std::isfinite(pos[2])))
            ok = false;
        // 只读 +0x734，不再调用 RootNodePosition：本构建它恒返回 0，
        // 而且切场景时对半销毁的实体调用游戏函数会直接 fail-fast 崩溃。
        e.x = pos[0];
        e.y = pos[1];
        e.z = pos[2];
        e.pos_ok = (std::fabs(pos[0]) + std::fabs(pos[1]) + std::fabs(pos[2]) > 0.01f);
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

// cfg+0x28 PointName / +0x70 ActionGraphName / +0x80 ActionGraphPath
inline bool classify_gadget(const char* ag, const char* graph, const char* point,
                            int& cat, std::string& label)
{
    cat = 6;
    label = "交互物";
    auto has = [](const char* s, const char* k) {
        return s && s[0] && std::strstr(s, k) != nullptr;
    };
    if (has(ag, "Empty") || has(ag, "Listener") || has(graph, "Empty") || has(point, "Empty"))
        return false;
    if (has(ag, "AirWall") || has(ag, "InvisibleWall") || has(ag, "StageCollider") ||
        has(ag, "Collider") || has(ag, "Barrier"))
        return false;

    if (has(ag, "Chest") || has(ag, "Treasure") || has(ag, "Box") || has(ag, "Reward")) {
        cat = 0;
        if (has(ag, "Elite")) label = "精英宝箱";
        else if (has(ag, "Rare") || has(ag, "Epic")) label = "稀有宝箱";
        else if (has(ag, "Luxury") || has(ag, "Legend")) label = "豪华宝箱";
        else label = "宝箱";
    } else if (has(ag, "Coin")) {
        cat = 1; label = "金币";
    } else if (has(ag, "Collection") || has(ag, "Collect") || has(ag, "Gather") ||
               has(ag, "Pick") || has(ag, "Drop")) {
        cat = 1; label = "采集物";
    } else if (has(ag, "Challenge")) {
        cat = 2; label = "挑战点";
    } else if (has(ag, "Teleport")) {
        cat = 4; label = "传送点";
    } else if (has(ag, "BigLevel") || has(ag, "LevelGate") || has(ag, "Entrance") ||
               has(ag, "Door") || has(ag, "Gate")) {
        cat = 7; label = "关卡入口";
    } else if (has(ag, "Metro") || has(ag, "Subway") || has(ag, "Station")) {
        cat = 7; label = "地铁口";
    } else if (has(ag, "Puzzle") || has(ag, "Game") || has(ag, "Number") || has(ag, "Digital")) {
        cat = 3; label = "数字游戏";
    } else if (has(ag, "Fence") || has(ag, "Ring")) {
        cat = 5; label = "围栏";
    } else if (has(graph, "Teleport") || has(point, "Teleport")) {
        cat = 4; label = "传送点";
    }
    return true;
}

// 管理器注册表（无扫描）：实现见 ov_registry.h。
inline uintptr_t registry_instance(ov::Data& d, const char* want)
{
    return ovr::RegistryInstance(d, want);
}

// 2026-11-26 MCP 实测（开放世界）：
//   PJMSGadgetManager 从管理器注册表取，实例 +0x40 = List<PJMSGadgetActor>（52 个）
//   actor +0x38 = _partName，+0xD8 = PJMSGadgetConfig
//   世界坐标在 +0xC4/+0xC8/+0xCC（+0xC0 是另一个 float，读 +0xC0 会整体偏移 4 字节）
//   config +0x28 = PointName（例如 "SP_Gadget22225"）
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
    // 本构建的 _allGadgetActors 在管理器 +0x40（见 0x6CF7E60 的 [rsi+0x40]），
    // 不是上一版猜的 +0x38；+0x38 是别的对象，读进去只会得到空列表。
    uintptr_t list = 0;
    std::vector<uintptr_t> ps;
    size_t lsize = 0, llen = 0;
    bool list_ok = false;
    if (d.rd(s.gadget_mgr + 0x40, list) && list && read_list(d, list, ps, &lsize, &llen)) {
        list_ok = true;
    } else if (d.rd(s.gadget_mgr + 0x38, list) && list && read_list(d, list, ps, &lsize, &llen)) {
        list_ok = true;  // 兼容旧构建
    }
    if (!list_ok) {
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
        uintptr_t cfg = 0;
        if (!d.rd(p + 0xD8, cfg)) cfg = 0;
        auto it = info_cache.find(p);
        if (it == info_cache.end() || it->second.cfg != cfg) {
            GInfo gi;
            gi.cfg = cfg;
            gi.drop = 1;  // 默认：类名/配置不符
            char cls[96] = {};
            const bool is_actor = d.copy_name(k, cls, sizeof cls) && std::strstr(cls, "PJMSGadgetActor");
            if (is_actor) {
                gi.drop = 0;
                gi.cat = 6;
                gi.label = "交互物";
                gi.name = cls;
                gi.draw = true;
                if (cfg) {
                    char pn[96] = {}, gn[96] = {}, ag[160] = {};
                    const bool has_pn = d.read_il2str(cfg, 0x28, pn, sizeof pn);
                    const bool has_gn = d.read_il2str(cfg, 0x70, gn, sizeof gn);
                    const bool has_ag = d.read_il2str(cfg, 0x80, ag, sizeof ag);
                    if (has_pn && pn[0]) gi.name = pn;
                    if (!classify_gadget(has_ag ? ag : "", has_gn ? gn : "", has_pn ? pn : "",
                                         gi.cat, gi.label)) {
                        gi.drop = 3;
                        gi.draw = false;
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
        // 世界坐标在 +0xC4/+0xC8/+0xCC（MCP 实测；+0xC0 不是 x）。
        if (!d.rd(p + 0xC4, e.x) || !d.rd(p + 0xC8, e.y) || !d.rd(p + 0xCC, e.z)) {
            ++s.gadget_drop_coord; continue;
        }
        if (!std::isfinite(e.x) || !std::isfinite(e.y) || !std::isfinite(e.z)) {
            ++s.gadget_drop_coord; continue;
        }
        if (std::fabs(e.x) > 1e5f || std::fabs(e.y) > 1e5f || std::fabs(e.z) > 1e5f ||
            std::fabs(e.x) + std::fabs(e.y) + std::fabs(e.z) < 0.01f) {
            ++s.gadget_drop_coord; continue;
        }
        gs.push_back(std::move(e));
    }
    s.gadgets.swap(gs);
}

inline uintptr_t local_avatar_ptr(ov::Data& d, State& s);

// 角色世界坐标在 +0x734 只对怪物有效；开放世界角色的 +0x734 恒为 0，
// 没有坐标时用相机位置兜底，至少距离/编号可用。
inline void refresh_player_pos(ov::Data& d, State& s) {
    if (!d.ua) {
        s.player_ok = false;
        s.player_from_cam = false;
        return;
    }
    if (s.player_t && s.t - s.player_t < 250) return;
    s.player_t = s.t;

    // 新 SDK：BaseMonoAvatar.LastValidPosition +0x748；+0x734 是旧版/怪物偏移。
    auto read_avatar_pos = [&](uintptr_t p, float out[3]) -> bool {
        if (!p) return false;
        if (d.rd(p + 0x748, out[0]) && d.rd(p + 0x74C, out[1]) && d.rd(p + 0x750, out[2]) &&
            std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]) &&
            std::fabs(out[0]) + std::fabs(out[1]) + std::fabs(out[2]) >= 0.01f)
            return true;
        return d.rd(p + 0x734, out[0]) && d.rd(p + 0x738, out[1]) && d.rd(p + 0x73C, out[2]) &&
               std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]) &&
               std::fabs(out[0]) + std::fabs(out[1]) + std::fabs(out[2]) >= 0.01f;
    };

    const uintptr_t la = local_avatar_ptr(d, s);
    float lp[3] = { 0, 0, 0 };
    // 字段为空时直接问游戏：BaseMonoAnimatorEntity.get_XYZPosition。
    if (read_avatar_pos(la, lp) || avatar_xyz_call(d, la, lp)) {
        memcpy(s.player_pos, lp, sizeof lp);
        s.player_ok = true;
        s.player_from_cam = false;
        return;
    }

    uintptr_t best = 0;
    float best_d2 = FLT_MAX;
    float best_p[3] = { 0, 0, 0 };
    for (const Ent& e : s.avatars) {
        if (!e.p) continue;
        float p[3] = {};
        if (!read_avatar_pos(e.p, p)) continue;
        const float dx = p[0] - s.cam_pos[0], dy = p[1] - s.cam_pos[1], dz = p[2] - s.cam_pos[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < best_d2) {
            best_d2 = d2;
            best = e.p;
            memcpy(best_p, p, sizeof p);
        }
    }
    if (best) {
        memcpy(s.player_pos, best_p, sizeof best_p);
        s.player_ok = true;
        s.player_from_cam = false;
        return;
    }
    if (s.cam_ok) {
        memcpy(s.player_pos, s.cam_pos, sizeof s.player_pos);
        s.player_ok = true;
        s.player_from_cam = true;
        return;
    }
    s.player_ok = false;
    s.player_from_cam = false;
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

// 本地角色：AvatarManager._avatarDict(+0x68)，key = _localAvatarRuntimeID(+0x94)。
// 这正是 AvatarManager.GetLocalAvatar() 内部走的那条路，不需要遍历列表。
// 兜底才用 _playerAvatars(+0x58) / _avatarLs(+0x40)，runtimeID 读 +0x18 或 +0x28。
inline uintptr_t local_avatar_ptr(ov::Data& d, State& s) {
    static uintptr_t cached = 0;
    static uint64_t  cached_t = 0;
    if (cached && s.t - cached_t < 500) {
        uintptr_t k = 0;
        if (d.rd(cached, k) && k) return cached;
    }
    const uintptr_t mgr = s.avatar_mgr ? s.avatar_mgr
                                       : registry_instance(d, "MoleMole.AvatarManager");
    if (!mgr) return 0;
    uint32_t want = 0;
    d.rd(mgr + 0x94, want);

    uintptr_t dict = 0;
    if (want && d.rd(mgr + 0x68, dict) && dict) {
        uintptr_t a = avatar_dict_lookup(d, dict, want);
        if (a) {
            cached = a;
            cached_t = s.t;
            return a;
        }
    }

    const uint32_t offs[2] = { 0x58u, 0x40u };  // _playerAvatars, _avatarLs
    uintptr_t first = 0;
    for (uint32_t off : offs) {
        uintptr_t lst = 0;
        if (!d.rd(mgr + off, lst) || !lst) continue;
        std::vector<uintptr_t> ps;
        if (!read_list(d, lst, ps)) continue;
        for (uintptr_t p : ps) {
            if (!p) continue;
            uint32_t rid = 0;
            if (!d.rd(p + 0x18, rid) || !rid) d.rd(p + 0x28, rid);
            if (!rid) continue;
            if (!first) first = p;
            if (want && rid == want) {
                cached = p;
                cached_t = s.t;
                return p;
            }
        }
    }
    if (first) { cached = first; cached_t = s.t; }
    return first;
}

inline void do_teleport(ov::Data& d, State& s) {
    if (!tp_req().exchange(false)) return;
    if (!d.ua) {
        lstrcpynA(tp_msg(), "未初始化", 96);
        return;
    }
    const uintptr_t avatar = local_avatar_ptr(d, s);
    if (!avatar) {
        lstrcpynA(tp_msg(), "没有角色实例", 96);
        return;
    }
    uintptr_t animator = 0;
    if (!d.rd(avatar + 0x270, animator) || !animator) {
        lstrcpynA(tp_msg(), "角色未实例化（等出场完成）", 96);
        return;
    }
    const uint32_t rv = ovp::Rva(ovp::SymTeleportTo);
    if (!rv) {
        lstrcpynA(tp_msg(), "传送符号未解析", 96);
        return;
    }
    // 新 SDK：BaseMonoAnimatorEntity.TeleportTo(Vector3) RVA 0x9BF0350。
    // 汇编是 (rcx=this, rdx=&Vector3)，不是旧 profile 里的 0x9BEB240。
    typedef void (*TpFn)(void*, const float*, void*);
    const float* t = tp_pos();
    float pos[3] = { t[0], t[1] + 0.5f, t[2] };
    ((TpFn)(d.ua + rv))((void*)avatar, pos, nullptr);
    lstrcpynA(tp_msg(), "已传送到目标", 96);
}

inline void refresh_ents(ov::Data& d, State& s) {
    // 管理器实例只从游戏自己的注册表取，不做 Singleton/类名/堆扫描。
    s.avatar_mgr = registry_instance(d, "MoleMole.AvatarManager");
    s.monster_mgr = registry_instance(d, "MoleMole.MonsterManager");

    std::vector<uintptr_t> ps;
    auto load = [&](uintptr_t mgr, uint32_t off, std::vector<Ent>& out, bool monster,
                    int& raw) {
        out.clear();
        raw = 0;
        if (!mgr) return false;
        uintptr_t lst = 0;
        if (!d.rd(mgr + off, lst) || !lst) return false;
        int32_t size = 0;
        if (!d.rd(lst + 0x18, size) || size < 0 || size > 4096) return false;
        if (size == 0) return true;   // 空列表也是有效管理器，不能因此丢掉实例
        if (!read_list(d, lst, ps)) return false;
        raw = (int)ps.size();
        fill_ents(d, ps, out, monster);
        return true;
    };

    if (!load(s.avatar_mgr, 0x40u, s.avatars, false, s.avatar_raw)) {
        // 实例已被销毁/换场景：先置空，refresh_singletons 会用静态区里的新实例覆盖。
        s.avatar_mgr = 0;
        s.avatars.clear();
        s.avatar_raw = 0;
    }
    if (!load(s.monster_mgr, 0x18u, s.monsters, true, s.monster_raw)) {
        s.monster_mgr = 0;
        s.monsters.clear();
        s.monster_raw = 0;
    }

    s.local_avatar = local_avatar_ptr(d, s);

    // 伤害回调只需要 runtimeID -> 角色/怪物，不需要 base klass，更不需要扫类名。
    s.rid_kind.clear();
    for (const Ent& e : s.avatars) {
        uint32_t rid = 0;
        if (e.p && d.rd(e.p + 0x18, rid) && rid) s.rid_kind[rid] = 1;
    }
    for (const Ent& e : s.monsters) {
        uint32_t rid = 0;
        if (e.p && d.rd(e.p + 0x18, rid) && rid) s.rid_kind[rid] = 2;
    }
    if (s.avatar_mgr) {
        uint32_t rid = 0;
        if (d.rd(s.avatar_mgr + 0x94, rid) && rid) s.rid_kind[rid] = 1;
    }
    g_dmg_state = &s;

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
    // 相机不能依赖 avatars/monsters 是否非空：纯宝箱场景、管理器还没建好、
    // 或玩家/NPC 绘制删掉后列表为空时，旧条件会直接跳过取相机，
    // 结果就是宝箱数量正常但 ESP 一个都不画。这里无条件取当前场景相机。
    if (cam_slow(d, s)) cam_fast(d, s);
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
// SDK 9.2.0：NCKIHBBDPPK 的其它 CD 判定，全部直接 patch 返回 false。
static constexpr uint32_t kSkillInCdRva = 0x04DA68F0u;   // IsSkillInCD(string)
static constexpr uint32_t kSkillHasCdRva = 0x04DB0B30u;  // IsSkillHasCD(string)
static constexpr uint32_t kSwitchInCdRva = 0x04DB0C10u;  // IsSwitchInCD()
static uint8_t g_cd_in_bak[3] = { 0 };
static uint8_t g_cd_has_bak[3] = { 0 };
static uint8_t g_cd_sw_bak[3] = { 0 };
static bool g_cd_in_on = false;
static bool g_cd_has_on = false;
static bool g_cd_sw_on = false;
static uint8_t g_cd_val_bak[4] = { 0 };
static bool g_cd_val_on = false;
// 本构建真正的技能入口是 0x7C31DF0；它在调用 Avatar.CanUseSkill 前后有两个硬门：
//   [skill+0x11C] != 0  -> 直接失败（冷却/锁定位）
//   CanUseSkill 返回 false -> 直接失败
// 旧 profile 里的 ui_skill_in_cd / ui_can_use_skill 实际是滚动条 UI 函数，打补丁没用。
static uint8_t g_skill_gate_bak[8] = { 0 };
static bool g_skill_gate_on = false;
static uint8_t g_avatar_can_bak[3] = { 0 };
static bool g_avatar_can_on = false;
static bool g_inf_sp_on = false;
static bool g_nocd_sp_on = false;
static uint8_t g_crit_main_bak[6] = { 0 };
static bool g_crit_main_on = false;
static uint8_t g_crit_phys_bak[6] = { 0 };
static bool g_crit_phys_on = false;
static uint8_t g_crit_elem_bak[6] = { 0 };
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

// 带目标字节校验的 false 补丁，避免 SDK RVA 不匹配时写坏其它函数。
inline bool patch_ret_false_checked(uintptr_t at, uint8_t* bak, bool& flag, bool on) {
    if (on == flag) return true;
    uint8_t cur[3] = { 0 };
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (void*)at, cur, 3, &got) || got != 3)
        return false;
    if (on) {
        if (cur[0] == 0x31 && cur[1] == 0xC0 && cur[2] == 0xC3) { flag = true; return true; }
        if (cur[0] == 0xCC || cur[0] == 0x00 || cur[0] == 0x90) return false;
        memcpy(bak, cur, 3);
        const uint8_t bytes[3] = { 0x31, 0xC0, 0xC3 };
        if (!write_code(at, bytes, 3, nullptr)) return false;
    } else {
        if (!flag) return true;
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

// 无限 SP 和“无冷却”都会用到 IsSpEnough；用共享引用避免两个开关互相拆补丁。
inline bool apply_shared_sp_patch(ov::Data& d) {
    const uint32_t rv_sp = ovp::Rva(ovp::SymIsSpEnough);
    const bool want = g_inf_sp_on || g_nocd_sp_on;
    if (!rv_sp) return !want;
    if (want == g_sp_on) return true;
    return patch_ret_true(d.ua + rv_sp, g_sp_bak, g_sp_on, want);
}

inline bool infinite_sp(ov::Data& d, bool on) {
    const uint32_t rv_cost = ovp::Rva(ovp::SymGetSkillSpCost);
    g_inf_sp_on = on;
    bool ok = apply_shared_sp_patch(d);
    if (rv_cost && !patch_ret_zero_float(d.ua + rv_cost, g_sp_cost_bak, g_sp_cost_on, on)) ok = false;
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
        if (rv_cost && g_sp_cost_on == on) patch_ret_zero_float(d.ua + rv_cost, g_sp_cost_bak, g_sp_cost_on, !on);
        return false;
    }
    return true;
}

// 技能入口 0x7C31DF0 的两道门：
//   +0xAA: 0F 85 B1 00 00 00  jne fail   （skill+0x11C 的冷却/锁定位）
//   +0xE5: 74 7A              je  fail   （CanUseSkill 返回 false）
// 只 NOP 这两个分支，保留距离/空技能等安全检查。
inline bool patch_skill_gate(uintptr_t at, bool on) {
    const uintptr_t j1 = at + 0xAA;
    const uintptr_t j2 = at + 0xE5;
    if (on == g_skill_gate_on) return true;
    uint8_t b1[6] = { 0 }, b2[2] = { 0 };
    SIZE_T got = 0;
    if (on) {
        if (!ReadProcessMemory(GetCurrentProcess(), (void*)j1, b1, 6, &got) || got != 6) return false;
        if (!ReadProcessMemory(GetCurrentProcess(), (void*)j2, b2, 2, &got) || got != 2) return false;
        // 已经补过（例如旧 DLL 留下的 hook）就直接认，不重复备份。
        const bool already = b1[0] == 0x90 && b1[1] == 0x90 && b2[0] == 0x90 && b2[1] == 0x90;
        if (!already) {
            if (b1[0] != 0x0F || b1[1] != 0x85 || b2[0] != 0x74) return false;
            memcpy(g_skill_gate_bak + 0, b1, 6);
            memcpy(g_skill_gate_bak + 6, b2, 2);
            const uint8_t n1[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
            const uint8_t n2[2] = { 0x90, 0x90 };
            if (!write_code(j1, n1, 6, nullptr) || !write_code(j2, n2, 2, nullptr)) {
                write_code(j1, b1, 6, nullptr);
                write_code(j2, b2, 2, nullptr);
                return false;
            }
        }
    } else {
        if (g_skill_gate_bak[0] == 0) return false;
        if (!write_code(j1, g_skill_gate_bak + 0, 6, nullptr)) return false;
        if (!write_code(j2, g_skill_gate_bak + 6, 2, nullptr)) return false;
    }
    g_skill_gate_on = on;
    return true;
}

inline bool no_skill_cd(ov::Data& d, bool on) {
    const uint32_t rv_gate = ovp::Rva(ovp::SymSkillGate);
    bool ok = true;
    // SDK 9.2.0 的 CD 权威判定全在 NCKIHBBDPPK：直接按 RVA patch，不找类、不解析方法。
    if (!patch_ret_false_checked(d.ua + ovp::Rva(ovp::SymAvatarSkillInCd),
                                 g_actor_cd_bak, g_actor_cd_on, on)) ok = false;
    if (!patch_ret_false_checked(d.ua + kSkillInCdRva, g_cd_in_bak, g_cd_in_on, on)) ok = false;
    if (!patch_ret_false_checked(d.ua + kSkillHasCdRva, g_cd_has_bak, g_cd_has_on, on)) ok = false;
    if (!patch_ret_false_checked(d.ua + kSwitchInCdRva, g_cd_sw_bak, g_cd_sw_on, on)) ok = false;

    if (rv_gate) {
        if (on != g_skill_gate_on && !patch_skill_gate(d.ua + rv_gate, on))
            ok = false;
    } else {
        ok = ok && !on;
    }
    // 大招的门槛是 SP（0/150 时按钮亮但点不动），无冷却必须同时放开 IsSpEnough。
    g_nocd_sp_on = on;
    if (!apply_shared_sp_patch(d)) ok = false;
    if (!ok) {
        if (g_actor_cd_on == on)
            patch_ret_false_checked(d.ua + ovp::Rva(ovp::SymAvatarSkillInCd),
                                    g_actor_cd_bak, g_actor_cd_on, !on);
        if (g_cd_in_on == on)
            patch_ret_false_checked(d.ua + kSkillInCdRva, g_cd_in_bak, g_cd_in_on, !on);
        if (g_cd_has_on == on)
            patch_ret_false_checked(d.ua + kSkillHasCdRva, g_cd_has_bak, g_cd_has_on, !on);
        if (g_cd_sw_on == on)
            patch_ret_false_checked(d.ua + kSwitchInCdRva, g_cd_sw_bak, g_cd_sw_on, !on);
        if (rv_gate && g_skill_gate_on == on)
            patch_skill_gate(d.ua + rv_gate, !on);
        return false;
    }
    return true;
}

// 100% 暴击要做三处：
//   1) +0x1C 处的「是否允许暴击」门（je → NOP）
//   2) +0x13 处的「暴击有效」门（je → NOP）
//   3) 暴击率 <= 随机数 的 jbe → NOP
// 只 NOP 最后一条时，前两个门为 0 的攻击仍然不会暴击，这就是本构建里
// 「百暴显示已开但没效果」的原因。
inline bool patch_crit_fn(uintptr_t jbe, uint8_t bak[6], bool& flag, bool on) {
    if (on == flag) return true;
    const uintptr_t g1 = jbe - 0x1C;
    const uintptr_t g2 = jbe - 0x13;
    if (on) {
        uint8_t b1[2] = { 0 }, b2[2] = { 0 }, b3[2] = { 0 };
        SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (void*)g1, b1, 2, &got) || got != 2) return false;
        if (!ReadProcessMemory(GetCurrentProcess(), (void*)g2, b2, 2, &got) || got != 2) return false;
        if (!ReadProcessMemory(GetCurrentProcess(), (void*)jbe, b3, 2, &got) || got != 2) return false;
        if (b1[0] != 0x74 || b2[0] != 0x74 || b3[0] != 0x76) return false;
        memcpy(bak + 0, b1, 2);
        memcpy(bak + 2, b2, 2);
        memcpy(bak + 4, b3, 2);
        const uint8_t nop[2] = { 0x90, 0x90 };
        if (!write_code(g1, nop, 2, nullptr) ||
            !write_code(g2, nop, 2, nullptr) ||
            !write_code(jbe, nop, 2, nullptr)) {
            write_code(g1, b1, 2, nullptr);
            write_code(g2, b2, 2, nullptr);
            write_code(jbe, b3, 2, nullptr);
            return false;
        }
    } else {
        if (!write_code(g1, bak + 0, 2, nullptr)) return false;
        if (!write_code(g2, bak + 2, 2, nullptr)) return false;
        if (!write_code(jbe, bak + 4, 2, nullptr)) return false;
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
    if (!patch_crit_fn(d.ua + rv_main, g_crit_main_bak, g_crit_main_on, on)) ok = false;
    if (!patch_crit_fn(d.ua + rv_phys, g_crit_phys_bak, g_crit_phys_on, on)) ok = false;
    if (!patch_crit_fn(d.ua + rv_elem, g_crit_elem_bak, g_crit_elem_on, on)) ok = false;
    if (!ok) {
        if (g_crit_main_on == on) patch_crit_fn(d.ua + rv_main, g_crit_main_bak, g_crit_main_on, !on);
        if (g_crit_phys_on == on) patch_crit_fn(d.ua + rv_phys, g_crit_phys_bak, g_crit_phys_on, !on);
        if (g_crit_elem_on == on) patch_crit_fn(d.ua + rv_elem, g_crit_elem_bak, g_crit_elem_on, !on);
        return false;
    }
    return true;
}

// 伤害 hook：挂在 0x4DF7D24（写 AttackData+0x1B8 = 2 的那 13 字节上）。
// 本构建 AttackData 的四个伤害分量是 +0x6C / +0x84 / +0x8C / +0x9C（见 0x4DF7C27 的四处 maxss）；
// 受击实体在 rdi，用 +0x78 类型继承链判定：BaseMonoAvatar = 玩家，BaseMonoMonster = 怪物。
static uint8_t g_dmg_bak[13] = { 0 };
static bool g_dmg_on = false;
static void* g_dmg_stub = nullptr;
static volatile float g_dmg_mult = 1.0f;
static volatile uint32_t g_dmg_flags = 0;
static uintptr_t g_dmg_k_ply = 0;
static uintptr_t g_dmg_k_mon = 0;
static uint64_t g_dmg_kt = 0;
static volatile uint32_t g_dmg_hits = 0;
static volatile uint32_t g_dmg_mon = 0;
static volatile uint32_t g_dmg_ply = 0;

static constexpr uint32_t kOhkValueBits = 0x4EFFFFFFu;

static constexpr uint32_t kDmgFlagMult = 1u;
static constexpr uint32_t kDmgFlagGod = 2u;
static constexpr uint32_t kDmgFlagOhk = 4u;

static const uint32_t kDmgOffs[4] = { 0x6Cu, 0x84u, 0x8Cu, 0x9Cu };

static bool dmg_rd(uintptr_t a, void* dst, size_t n) {
    if (!a) return false;
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)a, dst, n, &got) && got == n;
}

// Il2CppClass: +0xD0 是 typeHierarchy 深度，+0x78 是从根到自身的类指针数组。
static bool dmg_isa(uintptr_t k, uintptr_t target) {
    if (!k || !target) return false;
    uint8_t kd = 0, td = 0;
    uintptr_t kh = 0, e = 0;
    if (!dmg_rd(k + 0xD0, &kd, 1) || !dmg_rd(target + 0xD0, &td, 1)) return false;
    if (!td || td > kd) return false;
    if (!dmg_rd(k + 0x78, &kh, 8) || !kh) return false;
    if (!dmg_rd(kh + (uintptr_t)(td - 1) * 8, &e, 8)) return false;
    return e == target;
}

static bool dmg_klass_named(uintptr_t k, const char* want) {
    uintptr_t np = 0;
    char buf[64] = {};
    if (!k || !want) return false;
    if (!dmg_rd(k + 0x58, &np, 8) || !np) return false;
    if (!dmg_rd(np, buf, sizeof buf - 1)) return false;
    return std::strcmp(buf, want) == 0;
}

static bool dmg_klass_has(uintptr_t k, const char* part) {
    uintptr_t np = 0;
    char buf[128] = {};
    if (!k || !part) return false;
    if (!dmg_rd(k + 0x58, &np, 8) || !np) return false;
    if (!dmg_rd(np, buf, sizeof buf - 1)) return false;
    return std::strstr(buf, part) != nullptr;
}

static uintptr_t dmg_hier_named(uintptr_t k, const char* want) {
    uint8_t depth = 0;
    uintptr_t arr = 0;
    if (!k || !want) return 0;
    if (!dmg_rd(k + 0xD0, &depth, 1) || !depth || depth > 32) return 0;
    if (!dmg_rd(k + 0x78, &arr, 8) || !arr) return 0;
    for (uint8_t i = 0; i < depth; ++i) {
        uintptr_t e = 0;
        if (!dmg_rd(arr + (uintptr_t)i * 8, &e, 8) || !e) continue;
        if (dmg_klass_named(e, want)) return e;
    }
    return 0;
}

// 受击者分类缓存：1=角色 2=怪物 0=其它。避免每次命中都走继承链。
static int dmg_kind(uintptr_t k, uintptr_t k_ply, uintptr_t k_mon) {
    static uintptr_t last_k = 0;
    static int last_kind = 0;
    if (k == last_k) return last_kind;
    int kind = 0;
    if (dmg_isa(k, k_ply) || dmg_klass_has(k, "BaseMonoAvatar") ||
        dmg_hier_named(k, "BaseMonoAvatar"))
        kind = 1;
    else if (dmg_isa(k, k_mon) || dmg_klass_has(k, "BaseMonoMonster") ||
             dmg_hier_named(k, "BaseMonoMonster"))
        kind = 2;
    last_k = k;
    last_kind = kind;
    return kind;
}

// 伤害管线的第一个参数是 Actor（新 SDK 的 IEAKAFMILDE / BaseActor），
// 怪物实体在 actor+0x370（IEAKAFMILDE.CIHCCPJCDCH : BaseMonoMonster）。
static uintptr_t dmg_target(uintptr_t obj, uintptr_t* out_k) {
    uintptr_t e = 0, k = 0;
    if (dmg_rd(obj + 0x370, &e, sizeof e) && e &&
        dmg_rd(e, &k, sizeof k) && k && dmg_hier_named(k, "BaseMonoEntity")) {
        *out_k = k;
        return e;
    }
    if (dmg_rd(obj + 0x1B0, &e, sizeof e) && e &&
        dmg_rd(e, &k, sizeof k) && k && dmg_hier_named(k, "BaseMonoEntity")) {
        *out_k = k;
        return e;
    }
    if (dmg_rd(obj, &k, sizeof k) && k) {
        *out_k = k;
        return obj;
    }
    *out_k = 0;
    return 0;
}

static void dmg_apply(uintptr_t atk, uintptr_t attackee) {
    const uint32_t flags = g_dmg_flags;
    if (!flags || !atk || !attackee) return;
    ++g_dmg_hits;

    // 新 SDK：ResolveAttackDataFinal(BaseActor attackee, AttackData)。
    // 用 BaseActor.runtimeID(+0x28) 对照实体列表，不按类名扫描。
    int kind = 0;
    uint32_t rid = 0;
    if (dmg_rd(attackee + 0x28, &rid, 4) && rid && g_dmg_state) {
        auto it = g_dmg_state->rid_kind.find(rid);
        if (it != g_dmg_state->rid_kind.end()) kind = it->second;
    }
    if (!kind) {
        uintptr_t k = 0;
        const uintptr_t ent = dmg_target(attackee, &k);
        if (!ent || !k) return;
        kind = dmg_kind(k, 0, 0);
    }
    if (kind == 1) {
        ++g_dmg_ply;
        // 角色受击：无敌开关下清零四个伤害分量；不影响伤害倍率。
        if (!(flags & kDmgFlagGod)) return;
        const uint32_t zero = 0;
        for (int i = 0; i < 4; ++i) memcpy((void*)(atk + kDmgOffs[i]), &zero, 4);
        return;
    }
    if (kind != 2) return;  // 机关/场景物件不处理，避免破坏解谜
    ++g_dmg_mon;

    // 同一份 AttackData 可能被多目标复用，重复乘会指数放大；50ms 内只处理一次。
    const uint64_t now = GetTickCount64();
    static uintptr_t seen_atk = 0;
    static uint64_t seen_t = 0;
    if (atk == seen_atk && now - seen_t < 50) return;
    seen_atk = atk;
    seen_t = now;

    if (flags & kDmgFlagOhk) {
        // 旧版实测行为：只把主伤害分量设为极大，其余清零。
        // 主分量在本构建是 +0x8C（TryApplyCrit 乘的就是它）。
        for (int i = 0; i < 4; ++i) {
            const uint32_t v = (kDmgOffs[i] == 0x8Cu) ? kOhkValueBits : 0u;
            memcpy((void*)(atk + kDmgOffs[i]), &v, 4);
        }
        return;
    }
    if (!(flags & kDmgFlagMult)) return;
    const float m = g_dmg_mult;
    for (int i = 0; i < 4; ++i) {
        float f = 0.0f;
        memcpy(&f, (const void*)(atk + kDmgOffs[i]), 4);
        // NaN/0 不乘，避免把原本没参与计算的分量变成脏数据。
        if (!std::isfinite(f) || f == 0.0f) continue;
        f *= m;
        memcpy((void*)(atk + kDmgOffs[i]), &f, 4);
    }
}

static const uint8_t kDmgHead[13] = {
    0xC7, 0x86, 0xB8, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x48, 0x85, 0xFF
};

inline bool dmg_build_stub(uintptr_t resume) {
    if (g_dmg_stub) return true;

    uint8_t code[512] = { 0 };
    uint8_t* p = code;
    auto em8 = [&](uint8_t v) { *p++ = v; };
    auto em32 = [&](uint32_t v) { memcpy(p, &v, 4); p += 4; };
    auto em64 = [&](uint64_t v) { memcpy(p, &v, 8); p += 8; };

    // 回调式 stub：保存所有易失寄存器（rax/rcx/rdx/r8-r11 + xmm0-15）
    // → 调用 dmg_apply → 原 13 字节 → 回原处。
    em8(0x50);                                          // push rax
    em8(0x51);                                          // push rcx
    em8(0x52);                                          // push rdx
    em8(0x41); em8(0x50);                               // push r8
    em8(0x41); em8(0x51);                               // push r9
    em8(0x41); em8(0x52);                               // push r10
    em8(0x41); em8(0x53);                               // push r11
    em8(0x53);                                          // push rbx
    em8(0x57);                                          // push rdi
    em8(0x56);                                          // push rsi
    em8(0x48); em8(0x89); em8(0xE3);                    // mov rbx, rsp
    em8(0x48); em8(0x83); em8(0xE4); em8(0xF0);         // and rsp, -16
    em8(0x48); em8(0x81); em8(0xEC); em32(0x120);       // sub rsp, 0x120
    for (int i = 0; i < 16; ++i) {
        const uint32_t d = 0x20u + (uint32_t)i * 0x10u;
        const uint8_t modrm = (uint8_t)(0x84 | ((i & 7) << 3));
        if (i < 8) { em8(0x0F); em8(0x11); em8(modrm); em8(0x24); }
        else { em8(0x44); em8(0x0F); em8(0x11); em8(modrm); em8(0x24); }
        em32(d);                                        // movups [rsp+d], xmm_i
    }
    em8(0x48); em8(0x89); em8(0xF1);                    // mov rcx, rsi (AttackData)
    em8(0x48); em8(0x89); em8(0xFA);                    // mov rdx, rdi (受击实体)
    em8(0x48); em8(0xB8); em64((uint64_t)(uintptr_t)&dmg_apply);
    em8(0xFF); em8(0xD0);                               // call rax
    for (int i = 0; i < 16; ++i) {
        const uint32_t d = 0x20u + (uint32_t)i * 0x10u;
        const uint8_t modrm = (uint8_t)(0x84 | ((i & 7) << 3));
        if (i < 8) { em8(0x0F); em8(0x10); em8(modrm); em8(0x24); }
        else { em8(0x44); em8(0x0F); em8(0x10); em8(modrm); em8(0x24); }
        em32(d);                                        // movups xmm_i, [rsp+d]
    }
    em8(0x48); em8(0x89); em8(0xDC);                    // mov rsp, rbx
    em8(0x5E);                                          // pop rsi
    em8(0x5F);                                          // pop rdi
    em8(0x5B);                                          // pop rbx
    em8(0x41); em8(0x5B);                               // pop r11
    em8(0x41); em8(0x5A);                               // pop r10
    em8(0x41); em8(0x59);                               // pop r9
    em8(0x41); em8(0x58);                               // pop r8
    em8(0x5A);                                          // pop rdx
    em8(0x59);                                          // pop rcx
    em8(0x58);                                          // pop rax

    memcpy(p, kDmgHead, 13); p += 13;
    em8(0x48); em8(0xB8); em64((uint64_t)resume);
    em8(0xFF); em8(0xE0);                               // jmp rax

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
        uint8_t cur[13] = { 0 };
        SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (void*)at, cur, 13, &got) || got != 13)
            return false;
        // 同一进程里重新注入 DLL 时，原地可能仍是上一个 DLL 留下的 movabs+jmp。
        // 旧逻辑 memcmp != kDmgHead 就直接失败，导致倍率/秒杀永远不生效；
        // 这里允许覆盖已有 hook，但关闭时恢复磁盘 DLL 里的原始 13 字节。
        const bool original = memcmp(cur, kDmgHead, 13) == 0;
        const bool old_hook = cur[0] == 0x48 && cur[1] == 0xB8;
        if (!original && !old_hook) return false;
        if (!dmg_build_stub(at + 13)) return false;
        uint8_t patch[13] = { 0x48, 0xB8 };
        const uint64_t stub = (uint64_t)(uintptr_t)g_dmg_stub;
        memcpy(&patch[2], &stub, 8);
        patch[10] = 0xFF; patch[11] = 0xE0; patch[12] = 0x90;
        if (!write_code(at, patch, 13, nullptr)) return false;
    } else {
        // 不用备份：备份可能是旧 hook 的 13 字节，写回去等于没关。
        if (!write_code(at, kDmgHead, 13, nullptr)) return false;
    }
    g_dmg_on = want;
    return true;
}

// 新 SDK：BaseMonoAnimatorEntity._overrideAnimatorSpeed(+0x4F4) /
// _overrideAnimatorTimeScale(+0x4F8) 是游戏自己用的倍攻字段；
// _animator(+0x270) 的 native speed 作为兜底。全程不找类、不解析方法。
inline bool set_animator_speed(ov::Data& d, uintptr_t avatar, float speed) {
    if (!avatar) return false;
    const uint8_t over = (speed > 1.001f) ? 1 : 0;
    memcpy((void*)(avatar + 0x4F4), &over, 1);
    memcpy((void*)(avatar + 0x4F8), &speed, 4);
    uintptr_t animator = 0;
    if (d.rd(avatar + 0x270, animator) && animator) {
        uintptr_t native = 0;
        if (d.rd(animator + 0x10, native) && native) {
            memcpy((void*)(native + 0x280), &speed, 4);
        }
    }
    return true;
}

// 倍攻：对本地角色 + _playerAvatars(+0x58) + _avatarLs(+0x40) 的全集生效。
inline bool attack_speed(ov::Data& d, State& s, bool on, float mult) {
    uintptr_t av[12] = {};
    int an = 0;
    auto add = [&](uintptr_t p) {
        if (!p) return;
        uintptr_t k = 0;
        if (!d.rd(p, k) || !k) return;
        uint32_t rid = 0;
        if (!d.rd(p + 0x18, rid) || !rid) return;
        for (int i = 0; i < an; ++i) if (av[i] == p) return;
        if (an < 12) av[an++] = p;
    };
    add(local_avatar_ptr(d, s));
    if (s.avatar_mgr) {
        const uint32_t offs[2] = { 0x58u, 0x40u };
        for (uint32_t off : offs) {
            uintptr_t lst = 0;
            if (!d.rd(s.avatar_mgr + off, lst) || !lst) continue;
            std::vector<uintptr_t> ps;
            if (!read_list(d, lst, ps)) continue;
            for (uintptr_t p : ps) add(p);
        }
    }
    for (const Ent& e : s.avatars) add(e.p);

    if (!on) {
        if (!g_atk_speed_on) return true;
        for (int i = 0; i < an; ++i) set_animator_speed(d, av[i], 1.0f);
        g_atk_speed_on = false;
        return true;
    }
    if (mult < 1.0f) mult = 1.0f;
    if (mult > 10.0f) mult = 10.0f;
    int n = 0;
    for (int i = 0; i < an; ++i)
        if (set_animator_speed(d, av[i], mult)) ++n;
    g_atk_speed_on = n > 0;
    return n > 0;
}

inline bool sp_applied() { return g_sp_on; }
inline bool cd_applied() {
    return g_skill_gate_on && g_actor_cd_on && g_cd_in_on && g_cd_has_on && g_cd_sw_on;
}
inline bool crit_applied() { return g_crit_main_on && g_crit_phys_on && g_crit_elem_on; }
inline bool dmg_applied() { return g_dmg_on; }
inline uint32_t dmg_hits() { return g_dmg_hits; }
inline uint32_t dmg_mon_hits() { return g_dmg_mon; }
inline uint32_t dmg_ply_hits() { return g_dmg_ply; }
inline bool ohk_applied() { return g_dmg_on && (g_dmg_flags & kDmgFlagOhk) != 0; }
inline bool god_true_applied() { return g_dmg_on && (g_dmg_flags & kDmgFlagGod) != 0; }
inline bool atk_speed_applied() { return g_atk_speed_on; }
inline bool behit_applied() { return g_behit_on; }
inline bool god_applied() { return g_behit_on || god_true_applied(); }

// SDK 9.2.0：BaseMonoMonster.DieNow(KillEffect) 的编译体就是 RVA 0x077F7AC0，
// 不是方法 stub。Win64 IL2CPP 实例方法约定：rcx=this，rdx=KillEffect，r8=MethodInfo。
// 直接对 MonsterManager._monsterLs 里的实体调用，不再走 QA 专用的
// ForceClearAllMonstersByQA（那个不会触发怪物死亡流程）。
inline int64_t kill_all(ov::Data& d, const State& s) {
    if (s.monsters.empty()) return 0;
    const uint32_t rv = ovp::Rva(ovp::SymDieNow);
    if (!rv) return 0;
    typedef void (*DieFn)(void*, int32_t, void*);
    int n = 0;
    for (const Ent& e : s.monsters) {
        if (!e.p || !e.k) continue;
        uintptr_t klass = 0;
        if (!d.rd(e.p, klass) || klass != e.k) continue;  // 池复用/已销毁
        ((DieFn)(d.ua + rv))((void*)e.p, 0, nullptr);
        if (++n >= 128) break;
    }
    return n;
}

}
