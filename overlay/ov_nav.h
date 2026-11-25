#pragma once
// 3D 寻路：复用游戏 NavMesh。
// 方案D（主）：NavMesh.CalculateTriangulation() 拿整张导航网格
//   （NavMeshTriangulation{ vertices:Vector3[], indices:int[], areas:int[] }），
//   C++ 侧自建几何 A* + 漏斗/直线剪裁，全程不碰 il2cpp 对象分配，
//   绕开本 build 无法 il2cpp_object_new 的限制。
// 方案C（对比验证）：NavMesh.CalculatePath(src,dst,areaMask,path) —— 需要 NavMeshPath
//   实例；借怪物 AI 现成实例（GetNavPathDistance._navMeshPath，tdi 33685，
//   token 0x040049D7，字段 bh3_fields.tsv:142354）。两条链结果互验。
//
// 线程约定：nav_tick 必须在游戏主线程（tick_game_thread）里调用 —— NavMesh 静态
// 函数内部走 NavMesh 系统单例，非主线程调用可能崩。渲染线程只读 nav_state() 画线。
//
// 所有 RVA 经 ovp::Rva 取（PE 指纹 + prologue 校验，build 0x6AA18D63）。
#include "ov_data.h"
#include "ov_native.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ovnav {

struct Vec3 { float x = 0, y = 0, z = 0; };

inline float dist2(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

// ============================ 状态（渲染线程只读） ============================

struct NavState {
    std::vector<Vec3> pts;       // 最终路径拐点（世界坐标，玩家 -> 目标）
    bool ok = false;             // 本次请求是否算出了路径
    bool tri_ok = false;         // Triangulation 网格是否拿到了
    int tri_v = 0, tri_i = 0;    // 网格顶点数 / 三角形索引数
    bool via_engine = false;     // true=引擎 CalculatePath 拐点, false=A* 自算
    char msg[160] = "";
    uint64_t t = 0;              // 上次完成请求的时间戳
    // ---- 诊断计数（UI 直接显示，用于确认"点击到底有没有走到主线程"）----
    uint32_t req_n = 0;          // 累计点击次数（渲染线程写）
    uint32_t tick_n = 0;         // 主线程实际消费次数（主线程写）
    uint32_t last_ms = 0;        // 上次耗时
    int32_t  stage = 0;          // 失败阶段码（见 fetch_stage_text）
    uint32_t rva_tri = 0;        // triangulation 符号 RVA（0=未解析）
    bool     sym_ok = false;     // 该符号 prologue 校验是否通过
    bool     player_ok = false;  // 上次寻路时玩家坐标是否可用
    uintptr_t icall_ptr = 0;
    char      icall_mod[48] = "";
    char     diag[256] = "";
};

inline NavState& nav_state() {
    static NavState s;
    return s;
}

inline std::atomic<bool>& nav_req() {
    static std::atomic<bool> v(false);
    return v;
}

// UI 指定的寻路目标（世界坐标）。渲染线程 PageEsp "寻路"按钮写入，
// 游戏主线程 nav_tick 消费。has=false 时回退"最近目标"逻辑。
inline std::atomic<bool>& nav_has_target() {
    static std::atomic<bool> v(false);
    return v;
}
inline Vec3& nav_target() {
    static Vec3 v;                 // 12B，主线程消费前渲染线程刚写完，v1 不加锁
    return v;
}
// UI 触发入口：指定目标坐标请求一次寻路。
inline void nav_request_to(const Vec3& dst) {
    nav_target() = dst;
    nav_has_target().store(true);
    nav_req().store(true);
    ++nav_state().req_n;
}

// 自检请求：不依赖目标/玩家坐标，只跑一遍网格取数流程，结果写 nav_state().diag。
inline std::atomic<bool>& nav_diag_req() {
    static std::atomic<bool> v(false);
    return v;
}

// ============================ 静态方法 mi 解析 ============================
// NavMesh 的静态方法在 dump 中是"全局槽存 MethodInfo*"（与 camera_main 等同构）。
// ov_profile 的这些符号 RVA 指向 static field storage 槽；解引用一次得 MethodInfo*。

inline uintptr_t nav_mi(ov::Data& d, ovp::Sym sym) {
    const uint32_t rva = ovp::Rva(sym);
    if (!rva) return 0;
    uintptr_t mi = 0;
    if (!d.rd(d.ua + rva, mi) || !mi) return 0;
    // mi 必须指向可读内存且 mi+8 处是合法 klass 指针（ method->klass ），
    // 双重校验防止把"槽里存的不是 mi"的版本错配当真。
    uintptr_t klass = 0;
    if (!d.rd(mi + 8, klass) || !klass) return 0;
    return mi;
}

// ============================ il2cpp 数组读取 ============================

// Vector3[]：Il2CppArray length@+0x18，元素@+0x20，步长 12B。
inline bool read_vec3_array(ov::Data& d, uintptr_t arr, std::vector<Vec3>& out, int cap) {
    out.clear();
    if (!arr) return false;
    int32_t len = 0;
    if (!d.rd(arr + 0x18, len) || len < 0 || len > cap) return false;
    out.resize(len);
    for (int i = 0; i < len; ++i) {
        struct V { float x, y, z; } v{};
        if (!d.rd(arr + 0x20 + (size_t)i * 12, v)) return false;
        if (!(v.x == v.x) || !(v.y == v.y) || !(v.z == v.z)) return false;
        out[i] = { v.x, v.y, v.z };
    }
    return !out.empty();
}

// int[]：Il2CppArray length@+0x18，元素@+0x20，步长 4B。
inline bool read_i32_array(ov::Data& d, uintptr_t arr, std::vector<int32_t>& out, int cap) {
    out.clear();
    if (!arr) return false;
    int32_t len = 0;
    if (!d.rd(arr + 0x18, len) || len < 0 || len > cap) return false;
    out.resize(len);
    // 批量读：一次 ReadProcessMemory 拷整段，比逐元素快一个数量级
    const size_t bytes = (size_t)len * 4;
    if (ov::Data::readable(arr + 0x20, bytes)) {
        SIZE_T copied = 0;
        if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(arr + 0x20), out.data(),
                              bytes, &copied) && copied == bytes)
            return true;
    }
    for (int i = 0; i < len; ++i)
        if (!d.rd(arr + 0x20 + (size_t)i * 4, out[i])) return false;
    return true;
}

// ============================ 方案D：Triangulation + A* ============================
// CalculateTriangulation 0x1256D2A0 反汇编结论（build 0x6AA18D63）：
//   - rdx（mi 参数）完全未使用，函数内部走 icall 槽取数据。
//   - 无 NavMesh 的图上会 call 0x697E40 → int3 抛托管异常，直接调会崩游戏。
// 因此不调用该函数，改为复刻其取数流程（全程无异常路径）：
//   1) init 标志字节 @0x281333B（0x1256d2a8 rip-rel）为 0 时调 0x6259E0(ecx=0x5DCB) 注册 icall
//   2) icall 槽 @0x2554690（0x1256d2b1/0x1256d341 rip-rel 同槽）→ 空则失败
//   3) 调 icall() 得 NavigationSystem 单例指针 rax；rax==0 即此图无 NavMesh（不抛异常，安全返回）
//   4) 读单例字段：vertices=[rax+0x10]、indices=[rax+0x18]、areas=[rax+0x20]
//      （对应函数尾部 retbuf = {movups xmm0,[rax+0x10], mov rcx,[rax+0x20]}）
//   ⚠ 原函数 0x1256d328 的 call 0x698060 是【版本校验+抛托管异常】函数
//     （构造 "' to type '" 消息 → int3），只有网格过期才走，绝不能主动调
//     —— 曾经无条件调它 → strlen(null) 崩（Crash_2026-11-16_231402）。
//     网格过期由引擎自己处理；换图后 NavigationSystem 单例本身会换。
// 以上 RVA 全部 build 专用；入口用 triangulation 符号的 prologue 校验门禁防错配。

// 复刻流程用到的辅助 RVA：从 triangulation 入口的 rip-relative 指令动态解析。
namespace navutil {
    constexpr uint32_t kInitFlagRva   = 0x281333Bu;   // fallback
    constexpr uint32_t kIcallSlotRva  = 0x2554690u;
    constexpr uint32_t kIcallInitRva  = 0x6259E0u;
    constexpr uint32_t kIcallInitId   = 0x5DCBu;

    struct NavLayout {
        uint32_t init_flag = kInitFlagRva;
        uint32_t icall_slot = kIcallSlotRva;
        uint32_t init_fn = kIcallInitRva;
        uint32_t init_id = kIcallInitId;
    };

    inline NavLayout& layout() {
        static NavLayout v;
        static uint64_t stamp = 0;
        const uint64_t now = GetTickCount64();
        if (stamp && now - stamp < 2000) return v;
        stamp = now;
        const uint32_t rv = ovp::Rva(ovp::SymNavmeshCalculateTriangulation);
        if (!rv) return v;
        uint8_t b[0x120] = {};
        HMODULE ua = GetModuleHandleW(L"UserAssembly.dll");
        if (!ua || !ovp::ReadBytes((uintptr_t)ua + rv, b, sizeof b)) return v;
        auto rip_target = [&](size_t i, size_t len) -> uint64_t {
            if (i + len > sizeof b) return 0;
            const int32_t disp = (int32_t)(b[i] | (b[i + 1] << 8) |
                                           (b[i + 2] << 16) | ((uint32_t)b[i + 3] << 24));
            return (uint64_t)rv + i + len + (int64_t)disp;
        };
        if (b[0] == 0x80 && b[1] == 0x3D && b[7] == 0x74) {
            uint64_t t = rip_target(2, 4);
            if (t > 0x1000 && t < 0x1284B000ULL) v.init_flag = (uint32_t)t;
        }
        if (b[8] == 0xFF && b[9] == 0x15) {
            uint64_t t = rip_target(10, 4);
            if (t > 0x1000 && t < 0x1284B000ULL) v.icall_slot = (uint32_t)t;
        }
        for (size_t i = 0; i + 9 < sizeof b; ++i) {
            if (b[i] != 0xB9 || b[i + 5] != 0xE8) continue;
            const uint32_t id = (uint32_t)(b[i + 1] | (b[i + 2] << 8) |
                                           (b[i + 3] << 16) | ((uint32_t)b[i + 4] << 24));
            const int32_t rel = (int32_t)(b[i + 6] | (b[i + 7] << 8) |
                                          (b[i + 8] << 16) | ((uint32_t)b[i + 9] << 24));
            const int64_t target = (int64_t)rv + i + 10 + rel;
            if (target > 0x1000 && target < 0x1284B000ULL) {
                v.init_id = id;
                v.init_fn = (uint32_t)target;
                break;
            }
        }
        return v;
    }

    inline bool exec_ptr(uintptr_t p) {
        if (p < 0x10000ull) return false;
        MEMORY_BASIC_INFORMATION mbi;
        memset(&mbi, 0, sizeof mbi);
        if (VirtualQuery((void*)p, &mbi, sizeof mbi) != sizeof mbi) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        const DWORD prot = mbi.Protect & 0xFFu;
        return prot == PAGE_EXECUTE || prot == PAGE_EXECUTE_READ ||
               prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY;
    }

    inline void module_of(uintptr_t p, char* out, size_t cap) {
        if (out && cap) out[0] = 0;
        if (!p || !out || cap < 2) return;
        HMODULE m = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCWSTR)p, &m) || !m)
            return;
        char full[MAX_PATH] = {};
        if (!GetModuleFileNameA(m, full, MAX_PATH)) return;
        const char* b = strrchr(full, '\\');
        snprintf(out, cap, "%s", b ? b + 1 : full);
    }
}

// 取网格失败阶段码（UI 显示用，便于定位卡在哪一步）
enum : int32_t {
    kTriOk           = 0,
    kTriSymInvalid   = 1,   // 符号 prologue 校验未过（build 不匹配）
    kTriFlagUnread   = 2,   // 读 icall 初始化标志失败
    kTriSlotEmpty    = 3,   // icall 槽为空（引擎还没注册导航 icall）
    kTriSlotBadPtr   = 4,   // icall 槽值不在 UA 镜像内（防野指针）
    kTriSingletonZero= 5,   // NavigationSystem 单例为空 = 此图无 NavMesh
    kTriFieldUnread  = 6,   // 单例 +0x10/+0x18/+0x20 读失败
    kTriVertFail     = 7,   // 顶点数组读取失败
    kTriIdxFail      = 8,   // 索引数组读取失败
    kTriTooSmall     = 9,   // 网格太小/索引不是 3 的倍数
};

inline const char* fetch_stage_text(int32_t st) {
    switch (st) {
    case kTriOk:            return "网格已取到";
    case kTriSymInvalid:    return "导航符号校验未过（游戏版本不匹配）";
    case kTriFlagUnread:    return "读导航初始化标志失败";
    case kTriSlotEmpty:     return "导航 icall 未注册（还没进过有 NavMesh 的地图）";
    case kTriSlotBadPtr:    return "导航 icall 指针不可执行";
    case kTriSingletonZero: return "此图无 NavMesh（引擎未加载导航网格）";
    case kTriFieldUnread:   return "导航网格结构读取失败";
    case kTriVertFail:      return "顶点数组读取失败";
    case kTriIdxFail:       return "索引数组读取失败";
    case kTriTooSmall:      return "导航网格为空或损坏";
    default:                return "未知";
    }
}

// 返回 kTriOk 或失败阶段码；verts/idx 仅在 kTriOk 时有效。
inline int32_t fetch_triangulation_ex(ov::Data& d,
                                      std::vector<Vec3>& verts, std::vector<int32_t>& idx) {
    verts.clear();
    idx.clear();
    NavState& ns = nav_state();
    ns.rva_tri = ovp::Rva(ovp::SymNavmeshCalculateTriangulation);
    ns.sym_ok = ovp::Valid(ovp::SymNavmeshCalculateTriangulation);
    // 入口门禁：triangulation 符号必须静态校验通过（prologue 匹配当前 build），
    // 否则下面复刻用的硬编码 RVA 也不可信。
    if (!ns.sym_ok || !ns.rva_tri) return kTriSymInvalid;

    const navutil::NavLayout& nav = navutil::layout();
    uint8_t flag = 0;
    if (!d.rd(d.ua + nav.init_flag, flag)) return kTriFlagUnread;
    if (!flag) {
        typedef void (*InitFn)(int);
        ((InitFn)(d.ua + nav.init_fn))(nav.init_id);
    }

    uintptr_t icall = 0;
    if (!d.rd(d.ua + nav.icall_slot, icall) || !icall) return kTriSlotEmpty;
    ns.icall_ptr = icall;
    navutil::module_of(icall, ns.icall_mod, sizeof ns.icall_mod);
    if (!navutil::exec_ptr(icall)) return kTriSlotBadPtr;

    // 取 NavigationSystem 单例；null = 此图无 NavMesh —— 安全返回，不进引擎抛异常路径
    typedef void* (*IcallFn)();
    const uintptr_t singleton = (uintptr_t)((IcallFn)icall)();
    if (!singleton) return kTriSingletonZero;

    // 不调 0x698060：那是版本校验+抛托管异常函数（构造 "' to type '" 消息 → int3），
    // 网格过期时引擎自己会走；我们无条件调它 = 崩（Crash_2026-11-16_231402 实锤）。

    uintptr_t arr_v = 0, arr_i = 0, arr_a = 0;
    if (!d.rd(singleton + 0x10, arr_v) || !d.rd(singleton + 0x18, arr_i) ||
        !d.rd(singleton + 0x20, arr_a))
        return kTriFieldUnread;
    if (!arr_v || !arr_i) return kTriFieldUnread;

    if (!read_vec3_array(d, arr_v, verts, 65536)) return kTriVertFail;
    if (!read_i32_array(d, arr_i, idx, 262144)) return kTriIdxFail;
    (void)arr_a;  // areas 暂不使用
    if (!(verts.size() >= 3 && idx.size() >= 3 && (idx.size() % 3) == 0)) return kTriTooSmall;
    return kTriOk;
}

inline bool fetch_triangulation(ov::Data& d,
                                std::vector<Vec3>& verts, std::vector<int32_t>& idx) {
    return fetch_triangulation_ex(d, verts, idx) == kTriOk;
}

// ---- 几何 A*：三角形邻接图 ----

struct NavMeshGraph {
    std::vector<Vec3> verts;             // 顶点（复制到本进程内存，查找期不再碰游戏内存）
    std::vector<int32_t> idx;            // 三角形索引（3 个/三角形）
    std::vector<int> tri_of_vert_hint;   // 顶点 -> 任一相邻三角形（起点提示）
    std::vector<int> adj_start, adj;     // CSR 邻接：三角形 -> 相邻三角形

    bool build() {
        const int ntri = (int)(idx.size() / 3);
        const int nv = (int)verts.size();
        if (ntri <= 0 || nv <= 0) return false;
        // 顶点 key -> 三角形列表：用排序向量建（顶点可能被多三角形共享）
        std::vector<std::pair<int32_t, int>> vmap;  // (vert, tri)
        vmap.reserve(idx.size());
        for (int t = 0; t < ntri; ++t)
            for (int k = 0; k < 3; ++k) vmap.emplace_back(idx[t * 3 + k], t);
        std::sort(vmap.begin(), vmap.end());

        // CSR 邻接：共享一条边（两个公共顶点）即相邻
        std::vector<std::pair<int64_t, int>> edges;  // (min_nv*100000+max_nv 编码, tri) 太大，改用边->三角形
        // 更稳妥：对每三角形三条边，边 key = a*NV + b（a<b），边表排序后同 key 的三角形两两相邻。
        const int64_t NV = nv;
        std::vector<std::pair<int64_t, int>> emap;  // (edge_key, tri)
        emap.reserve((size_t)ntri * 3);
        for (int t = 0; t < ntri; ++t) {
            for (int k = 0; k < 3; ++k) {
                const int32_t a = idx[t * 3 + k], b = idx[t * 3 + (k + 1) % 3];
                if (a < 0 || a >= nv || b < 0 || b >= nv) return false;
                const int64_t lo = a < b ? a : b, hi = a < b ? b : a;
                if (lo == hi) return false;  // 退化
                emap.emplace_back(lo * NV + hi, t);
            }
        }
        std::sort(emap.begin(), emap.end());

        // 去重相邻对（两三角形可能共享 2 条边？不会——共边唯一；但同边可能 >2 三角形，全部互连）
        std::vector<int> adj_list;
        adj_list.reserve(emap.size());
        std::vector<int> deg(ntri, 0);
        for (size_t i = 0; i < emap.size();) {
            size_t j = i;
            while (j < emap.size() && emap[j].first == emap[i].first) ++j;
            for (size_t a = i; a < j; ++a)
                for (size_t b = i; b < j; ++b)
                    if (emap[a].second != emap[b].second) {
                        adj_list.push_back(emap[b].second);
                        ++deg[emap[a].second];
                    }
            i = j;
        }
        adj_start.resize(ntri + 1, 0);
        for (int t = 0; t < ntri; ++t) adj_start[t + 1] = adj_start[t] + deg[t];
        adj = std::move(adj_list);
        return (int)adj_start.size() == ntri + 1;
    }

    int ntri() const { return (int)(idx.size() / 3); }

    // 点 -> 三角形：暴力太慢；开放世界几万三角形，主线程一次请求可接受
    // （~50k 次 6 次乘加 ≈ 亚毫秒）。找到"点在 XZ 投影内"的第一个三角形。
    int tri_at(const Vec3& p) const {
        const int n = ntri();
        for (int t = 0; t < n; ++t) {
            const Vec3& a = verts[idx[t * 3]];
            const Vec3& b = verts[idx[t * 3 + 1]];
            const Vec3& c = verts[idx[t * 3 + 2]];
            // XZ 平面 2D 重心测试（Y 分量忽略：跨层地图由三角形自身分离）
            const float d1 = (p.x - b.x) * (a.z - b.z) - (a.x - b.x) * (p.z - b.z);
            const float d2 = (p.x - c.x) * (b.z - c.z) - (b.x - c.x) * (p.z - c.z);
            const float d3 = (p.x - a.x) * (c.z - a.z) - (c.x - a.x) * (p.z - a.z);
            const bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
            const bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
            if (!(neg && pos)) return t;
        }
        return -1;
    }

    float tri_center_d2(int t, const Vec3& p) const {
        Vec3 c{};
        for (int k = 0; k < 3; ++k) {
            const Vec3& v = verts[idx[t * 3 + k]];
            c.x += v.x; c.y += v.y; c.z += v.z;
        }
        c.x /= 3; c.y /= 3; c.z /= 3;
        return dist2(c, p);
    }

    // 两三角形相邻边的共享边中点（portal 中心，漏斗简化的走廊节点）
    bool portal(int ta, int tb, Vec3& mid) const {
        for (int ka = 0; ka < 3; ++ka) {
            const int32_t a0 = idx[ta * 3 + ka], a1 = idx[ta * 3 + (ka + 1) % 3];
            for (int kb = 0; kb < 3; ++kb) {
                const int32_t b0 = idx[tb * 3 + kb], b1 = idx[tb * 3 + (kb + 1) % 3];
                if ((a0 == b0 && a1 == b1) || (a0 == b1 && a1 == b0)) {
                    const Vec3& p0 = verts[a0];
                    const Vec3& p1 = verts[a1];
                    mid = { (p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f, (p0.z + p1.z) * 0.5f };
                    return true;
                }
            }
        }
        // 仅顶点接触（对角相邻）：取共享顶点
        for (int ka = 0; ka < 3; ++ka) {
            const int32_t a0 = idx[ta * 3 + ka];
            for (int kb = 0; kb < 3; ++kb)
                if (a0 == idx[tb * 3 + kb]) { mid = verts[a0]; return true; }
        }
        mid = { 0, 0, 0 };
        return false;
    }
};

// ---- 三角形序列 -> 拐点：直线可见性剪裁 ----

// 线段可见性（保守）：整段按 2m 步长采样，每个采样点 tri_at 命中的三角形
// 必须在走廊集合内；采样失败（在网格外）视为不可见。
inline bool segment_in_corridor(const NavMeshGraph& g, const Vec3& a, const Vec3& b,
                                const std::vector<int>& corridor) {
    std::vector<int> cs = corridor;
    std::sort(cs.begin(), cs.end());
    const float len = std::sqrt(dist2(a, b));
    const int steps = std::max(2, (int)(len / 2.0f));
    for (int i = 0; i <= steps; ++i) {
        const float f = (float)i / (float)steps;
        Vec3 p{ a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f };
        const int t = g.tri_at(p);
        if (t < 0) return false;
        if (!std::binary_search(cs.begin(), cs.end(), t)) return false;
    }
    return true;
}

inline void simplify_corridor(const NavMeshGraph& g, const std::vector<int>& corridor,
                              const Vec3& src, const Vec3& dst,
                              std::vector<Vec3>& out_pts) {
    out_pts.clear();
    if (corridor.empty()) { out_pts.push_back(dst); return; }
    // 走廊 portal 中点
    std::vector<Vec3> portals;
    portals.reserve(corridor.size());
    for (size_t i = 0; i + 1 < corridor.size(); ++i) {
        Vec3 mid{};
        g.portal(corridor[i], corridor[i + 1], mid);
        portals.push_back(mid);
    }
    portals.push_back(dst);

    // 视点拉绳：当前锚点 src，尽量跳远
    Vec3 anchor = src;
    size_t i = 0;
    while (i < portals.size()) {
        // 从最远往回找第一个"锚点->portal 线段全程在走廊内"的 portal
        size_t reach = i;
        for (size_t j = portals.size() - 1; j > i; --j) {
            if (segment_in_corridor(g, anchor, portals[j], corridor)) { reach = j; break; }
        }
        out_pts.push_back(portals[reach]);
        anchor = portals[reach];
        i = reach + 1;
    }
    // 最后一个点若是 dst 已含；确保终点
    if (out_pts.empty() || dist2(out_pts.back(), dst) > 1e-6f) out_pts.push_back(dst);
}

// ---- A* 主流程 ----

// 借怪物 AI 现成 NavMeshPath 的方案C（v2 再做）：
// 找 GetNavPathDistance（tdi 33685）行为树任务实例的 _navMeshPath 字段
// （token 0x040049D7，bh3_fields.tsv:142354），该实例在怪物开战后存在。
// v1 占位：方案D 优先，失败时提示而不做堆扫描（堆扫描开销大且字段偏移未实测）。
inline uintptr_t borrow_navmesh_path(ov::Data& d, uintptr_t anchor) {
    (void)d; (void)anchor;
    return 0;  // v1: 方案C 兜底未实现，纯方案D
}

inline bool run_pathfind(ov::Data& d, ovn::State& s, const Vec3& dst) {
    NavState& ns = nav_state();
    ns.ok = false;
    ns.msg[0] = 0;
    ns.stage = 0;

    Vec3 src{};
    ns.player_ok = s.player_ok;
    if (s.player_ok) src = { s.player_pos[0], s.player_pos[1], s.player_pos[2] };
    else { snprintf(ns.msg, sizeof ns.msg, "无玩家坐标（等 ESP 读到角色位置）"); ns.stage = -1; return false; }

    // 1) 拿网格
    std::vector<Vec3> verts;
    std::vector<int32_t> idx;
    const int32_t st = fetch_triangulation_ex(d, verts, idx);
    ns.stage = st;
    if (st != kTriOk) {
        snprintf(ns.msg, sizeof ns.msg, "取网格失败：%s", fetch_stage_text(st));
        ns.tri_ok = false;
        return false;
    }
    ns.tri_ok = true;
    ns.tri_v = (int)verts.size();
    ns.tri_i = (int)idx.size();

    // 2) 建图
    NavMeshGraph g;
    g.verts = std::move(verts);
    g.idx = std::move(idx);
    if (!g.build()) { snprintf(ns.msg, sizeof ns.msg, "建图失败"); return false; }

    // 3) 起终点三角形（找不到就取中心最近的三角形）
    int ts = g.tri_at(src);
    if (ts < 0) {
        // 起点略浮空/陷地：中心最近兜底
        float bd = 1e18f; int bt = -1;
        const int n = g.ntri();
        for (int t = 0; t < n; ++t) {
            const float dd = g.tri_center_d2(t, src);
            if (dd < bd) { bd = dd; bt = t; }
        }
        ts = bt;
    }
    int td = g.tri_at(dst);
    if (td < 0) {
        float bd = 1e18f; int bt = -1;
        const int n = g.ntri();
        for (int t = 0; t < n; ++t) {
            const float dd = g.tri_center_d2(t, dst);
            if (dd < bd) { bd = dd; bt = t; }
        }
        td = bt;
    }
    if (ts < 0 || td < 0) { snprintf(ns.msg, sizeof ns.msg, "起终点均不在网格"); return false; }
    if (ts == td) { ns.pts.clear(); ns.pts.push_back(dst); ns.ok = true;
                    snprintf(ns.msg, sizeof ns.msg, "同三角形：直达"); return true; }

    // 4) A*（三角形图，代价 = portal 中点距离）
    const int n = g.ntri();
    std::vector<float> g_cost(n, 1e18f);
    std::vector<int> came(n, -1);
    std::vector<uint8_t> closed(n, 0);
    // 简单二叉堆用 vector + push_pop（n 几万，O(E log V) 足够）
    struct QN { float f; int t; };
    auto cmp = [](const QN& a, const QN& b) { return a.f > b.f; };
    std::vector<QN> heap;
    auto hdist = [&](int t) { return std::sqrt(g.tri_center_d2(t, dst)); };
    auto tri_center = [&](int t) {
        Vec3 c{};
        for (int k = 0; k < 3; ++k) { const Vec3& v = g.verts[g.idx[t * 3 + k]];
            c.x += v.x; c.y += v.y; c.z += v.z; }
        c.x /= 3; c.y /= 3; c.z /= 3;
        return c;
    };
    auto push = [&](float f, int t) { heap.push_back({ f, t }); std::push_heap(heap.begin(), heap.end(), cmp); };
    auto pop = [&]() { std::pop_heap(heap.begin(), heap.end(), cmp); QN q = heap.back(); heap.pop_back(); return q; };

    g_cost[ts] = 0;
    push(hdist(ts), ts);
    bool found = false;
    int expanded = 0;
    while (!heap.empty()) {
        QN q = pop();
        const int t = q.t;
        if (closed[t]) continue;
        closed[t] = 1;
        if (++expanded > 200000) break;
        if (t == td) { found = true; break; }
        for (int ai = g.adj_start[t]; ai < g.adj_start[t + 1]; ++ai) {
            const int nb = g.adj[ai];
            if (closed[nb]) continue;
            Vec3 mid{};
            g.portal(t, nb, mid);
            const float w = std::sqrt(dist2(tri_center(t), mid)) + std::sqrt(dist2(mid, tri_center(nb)));
            const float ng = g_cost[t] + w;
            if (ng < g_cost[nb]) {
                g_cost[nb] = ng;
                came[nb] = t;
                push(ng + hdist(nb), nb);
            }
        }
    }
    if (!found) { snprintf(ns.msg, sizeof ns.msg, "A* 无解（区域隔离，expanded=%d）", expanded); return false; }

    // 5) 回溯走廊 + 拉直
    std::vector<int> corridor;
    for (int t = td; t >= 0; t = came[t]) { corridor.push_back(t); if ((int)corridor.size() > n) break; }
    std::reverse(corridor.begin(), corridor.end());
    std::vector<Vec3> pts;
    simplify_corridor(g, corridor, src, dst, pts);

    ns.pts = std::move(pts);
    ns.ok = !ns.pts.empty();
    ns.via_engine = false;
    snprintf(ns.msg, sizeof ns.msg, "A* %zu 拐点，走廊 %zu 三角形，网格 %d/%d",
             ns.pts.size(), corridor.size(), ns.tri_v, ns.tri_i / 3);
    return ns.ok;
}

// 游戏主线程 tick 入口：消费 nav_req()，算一次路径。
// UI 指定了目标（nav_has_target）→ 用之；否则目标 = 最近的宝箱/采集物/NPC。
inline void nav_tick(ov::Data& d, ovn::State& s) {
    NavState& ns = nav_state();

    // 自检：不依赖玩家/目标，只验证能不能取到 NavMesh 网格
    if (nav_diag_req().exchange(false)) {
        ++ns.tick_n;
        const uint64_t t0 = GetTickCount64();
        std::vector<Vec3> v;
        std::vector<int32_t> ix;
        const int32_t st = fetch_triangulation_ex(d, v, ix);
        if (st == kTriOk) {
            snprintf(ns.diag, sizeof ns.diag,
                     "自检 OK：网格 %zu 顶点 / %zu 三角形，符号 RVA %08X，icall %llX %s",
                     v.size(), ix.size() / 3, ns.rva_tri,
                     (unsigned long long)ns.icall_ptr, ns.icall_mod);
        } else {
            snprintf(ns.diag, sizeof ns.diag,
                     "自检失败[%d]：%s（符号 RVA %08X%s，icall %llX %s）",
                     st, fetch_stage_text(st), ns.rva_tri,
                     ns.sym_ok ? "" : "，未通过 prologue 校验",
                     (unsigned long long)ns.icall_ptr, ns.icall_mod);
        }
        ns.last_ms = (uint32_t)(GetTickCount64() - t0);
    }

    if (!nav_req().exchange(false)) return;
    ++ns.tick_n;
    const uint64_t t0 = GetTickCount64();

    Vec3 dst{};
    if (nav_has_target().exchange(false)) {
        dst = nav_target();
    } else {
    const float rx = s.player_ok ? s.player_pos[0] : s.cam_pos[0];
    const float ry = s.player_ok ? s.player_pos[1] : s.cam_pos[1];
    const float rz = s.player_ok ? s.player_pos[2] : s.cam_pos[2];
    const ovn::Ent* best = nullptr;
    float bestd = 1e18f;
    auto consider = [&](const std::vector<ovn::Ent>& list) {
        for (const ovn::Ent& e : list) {
            const float dx = e.x - rx, dy = e.y - ry, dz = e.z - rz;
            const float dd = dx * dx + dy * dy + dz * dz;
            if (dd < bestd) { bestd = dd; best = &e; }
        }
    };
    consider(s.props);
    consider(s.gadgets);
    consider(s.npcs);
    if (!best) {
        NavState& ns = nav_state();
        snprintf(ns.msg, sizeof ns.msg, "无可用目标（先开 ESP 扫描）");
        ns.ok = false;
        ns.t = GetTickCount64();
        return;
    }
    dst = { best->x, best->y, best->z };
    }

    run_pathfind(d, s, dst);
    ns.t = GetTickCount64();
    ns.last_ms = (uint32_t)(ns.t - t0);
    char tail[64];
    snprintf(tail, sizeof tail, "，耗时 %llu ms", (unsigned long long)ns.last_ms);
    const size_t len = strlen(ns.msg);
    if (len + strlen(tail) < sizeof ns.msg) strcat_s(ns.msg, tail);
}

} // namespace ovnav
