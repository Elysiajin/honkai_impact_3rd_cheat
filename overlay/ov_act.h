#pragma once
// 活动战斗：TurnBattle 属性袋读写，仅适配玩具小屋。

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "ov_data.h"

namespace ova
{

static const uintptr_t KL_NAME_OFF  = 0x20;
static const uintptr_t OBJ_MONITOR  = 0x08;
static const uintptr_t DICT_ENTRIES = 0x20;
static const uintptr_t ARR_LEN      = 0x18;
static const uintptr_t ARR_DATA     = 0x20;
static const uintptr_t STR_LEN      = 0x10;
static const uintptr_t STR_CHARS    = 0x14;
static const int       ENTRY_STRIDE = 24;
static const uintptr_t TRACE_VALUE  = 0x38;   // 旧缓存，勿写
static const uintptr_t TRACE_LIST   = 0x10;   // TracedValues 容器
static const uintptr_t LIST_ARR     = 0x10;
static const uintptr_t LIST_SIZE    = 0x18;
static const uintptr_t TV_INT       = 0x18;   // 真值：低 int 高 float

static const uintptr_t ACT_BAG   = 0x10;
static const uintptr_t MGR_ARRAY = 0x38;

static const int kMaxActors  = 64;
static const int kRefreshMs  = 200;

struct Actor
{
    uintptr_t p          = 0;
    int       camp       = 0;    // 1=我 2=敌 0=中立（由 AvatarID/MonsterID 语义判定）
    int       raw_camp   = 0;    // 原始 Actor_Camp，仅用于调试显示
    int       hp         = 0;
    int       max_hp     = 0;
    int       raw_max    = 0;
    float     hp_ratio   = 0.0f;
    int       avatar_id  = 0;
    int       monster_id = 0;
    uintptr_t hp_vp      = 0;   // 属性值对象，写时再解真值槽
    uintptr_t ratio_vp   = 0;
    uintptr_t un_dmg_vp  = 0;
    uintptr_t pd_vp      = 0;
    uintptr_t dt_vp      = 0;   // 承伤倍率增量，-1 等于不吃伤害
    bool      kill_req   = false;
};

struct ActState
{
    bool      ready    = false;
    bool      in_act   = false;
    int       n        = 0;
    int       ally_n   = 0;
    int       enemy_n  = 0;
    int       ally_camp_raw = -1;  // 我方原始 Camp 编号，用于二次排除友军
    int       scan_ms  = 0;
    uintptr_t manager  = 0;
    uintptr_t act_arr  = 0;
    int       act_len  = 0;
    int       fail_n   = 0;   // 连续读取失败次数，避免一次瞬时失败就全量重扫
    uint64_t  t        = 0;
    uint64_t  empty_t  = 0;
    char      msg[160] = { 0 };
    Actor     list[kMaxActors];
};

struct ActCtx
{
    uintptr_t pool_lo    = 0;
    uintptr_t pool_hi    = 0;
    uintptr_t klass_n    = 0;
    uintptr_t klass_i    = 0;
    uint64_t  next_klass = 0;
    uintptr_t mgr_klass  = 0;
    uintptr_t act_klass  = 0;

    uintptr_t mgr_addr  = 0;
    uintptr_t mgr_zero  = 0;   // 列表为空的候选管理器，全量扫描结束仍无活跃管理器时采用
    uintptr_t last_mgr  = 0;   // 上一次成功连接的管理器，重扫时优先扫它所在的区域
    std::vector<bh3::Runtime::Region> regions;
    int       rcur      = 0;
    int       ridx      = 0;
    uint64_t  next_scan = 0;
    bool      found     = false;

    void reset_scan()
    {
        mgr_addr = 0;
        mgr_zero = 0;
        regions.clear();
        rcur = 0;
        ridx = 0;
        found = false;
        next_scan = 0;
    }
};

inline bool ok_obj(ov::Data& d, uintptr_t p)
{
    if (!p || (p & 7)) return false;
    uintptr_t mon = 0;
    if (!d.rd_u64(p + OBJ_MONITOR, mon)) return false;
    return mon < 0x1000;
}

inline int lo_i32(uint64_t v) { return (int32_t)(uint32_t)(v & 0xFFFFFFFFu); }

inline float hi_f32(uint64_t v)
{
    const uint32_t hi = (uint32_t)(v >> 32);
    float f = 0.0f;
    memcpy(&f, &hi, 4);
    return f;
}

inline bool sane_int(int v) { return v > 0 && v < 100000000; }
inline bool sane_flt(float v) { return v >= 0.0f && v < 1.0e8f; }

// 低 32 位是 int，高 32 位是同值 float，哪个可用取哪个
inline int pick_int(uint64_t payload)
{
    const int lo = lo_i32(payload);
    if (sane_int(lo)) return lo;
    const float hi = hi_f32(payload);
    if (sane_flt(hi)) return (int)(hi + 0.5f);
    return 0;
}

inline float pick_flt(uint64_t payload)
{
    const float hi = hi_f32(payload);
    if (sane_flt(hi)) return hi;
    const int lo = lo_i32(payload);
    return sane_int(lo) ? (float)lo : 0.0f;
}

inline uint64_t pack_pair(int iv, float fv)
{
    uint32_t fb = 0;
    memcpy(&fb, &fv, 4);
    return ((uint64_t)fb << 32) | (uint32_t)iv;
}

// 真值槽 = TracedValues 末元素 + 0x18，游戏只认这里
inline uintptr_t tv_slot(ov::Data& d, uintptr_t vp)
{
    if (!vp) return 0;
    uintptr_t cont = 0, arr = 0, ep = 0;
    int32_t sz = 0;
    if (!d.rd_u64(vp + TRACE_LIST, cont) || !cont) return 0;
    if (!d.rd_u64(cont + LIST_ARR, arr) || !arr) return 0;
    if (!d.rd<int32_t>(cont + LIST_SIZE, sz) || sz <= 0 || sz > 64) return 0;
    if (!d.rd_u64(arr + ARR_DATA + (uintptr_t)(sz - 1) * 8, ep) || !ep) return 0;
    return ep + TV_INT;
}

// 写属性真值，槽每帧重解，防列表被重建
inline bool write_prop(ov::Data& d, uintptr_t vp, int iv, float fv)
{
    const uintptr_t s = tv_slot(d, vp);
    if (!s) return false;
    const uint64_t v = pack_pair(iv, fv);
    SIZE_T put = 0;
    return WriteProcessMemory(GetCurrentProcess(), (LPVOID)s, &v, 8, &put) && put == 8;
}

// 分块读 qword，返回实际读到的数量
inline uintptr_t read_chunk(uintptr_t base, uintptr_t* buf, uintptr_t max_q)
{
    if (!base || !max_q) return 0;
    SIZE_T got = 0;
    const SIZE_T want = (SIZE_T)max_q * 8;
    if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)base, buf, want, &got) || !got)
        return 0;
    return (uintptr_t)(got / 8);
}

// 分帧扫类名
inline void scan_klasses(ov::Data& d, ActCtx& c, int budget_ms)
{
    if (c.mgr_klass) return;
    const uint64_t now = GetTickCount64();
    if (now < c.next_klass) return;

    if (!c.pool_lo)
    {
        size_t lo = 0, hi = 0;
        if (!d.rt.pool_bounds(lo, hi) || hi <= lo) { c.next_klass = now + 1000; return; }
        const size_t used = d.rt.pool_used();
        if (used && used < hi - lo) hi = lo + used;
        c.pool_lo = (uintptr_t)lo;
        c.pool_hi = (uintptr_t)hi;
        c.klass_n = (c.pool_hi - c.pool_lo) / 8;
        c.klass_i = 0;
    }

    const uintptr_t lo = c.pool_lo;
    const uintptr_t hi = c.pool_hi;
    const char* kMgr = "TurnBattleActorManager";
    const size_t nMgr = strlen(kMgr);
    const uint64_t deadline = now + (uint64_t)budget_ms;

    static thread_local uintptr_t buf[8192];
    static thread_local char      sbuf[64];

    while (c.klass_i < c.klass_n)
    {
        const uintptr_t left = c.klass_n - c.klass_i;
        const uintptr_t want = left < 8192 ? left : 8192;
        const uintptr_t base = lo + c.klass_i * 8;
        const uintptr_t cnt = read_chunk(base, buf, want);
        if (!cnt)
        {
            c.klass_i += want;
            if (GetTickCount64() >= deadline) return;
            continue;
        }
        c.klass_i += cnt;

        for (uintptr_t j = 0; j < cnt; ++j)
        {
            const uintptr_t v = buf[j];
            if (v < lo || v + 64 > hi) continue;
            SIZE_T got = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)v, sbuf, 32, &got) || got != 32)
                continue;
            if (sbuf[0] != 'T') continue;
            if (memcmp(sbuf, kMgr, nMgr) == 0 && sbuf[nMgr] == 0)
            {
                c.mgr_klass = base + j * 8 - KL_NAME_OFF;
                return;
            }
        }
        if (GetTickCount64() >= deadline) return;
    }

    c.klass_i = 0;
    c.next_klass = GetTickCount64() + 3000;
}

// 一次遍历取多个属性：每条目只读一次键名
struct BagKey
{
    const char* name;
    int*        i;
    float*      f;
    uintptr_t*  slot;
    bool        got;
};

inline int bag_read(ov::Data& d, uintptr_t bag, BagKey* keys, int nk)
{
    for (int k = 0; k < nk; ++k)
    {
        keys[k].got = false;
        if (keys[k].i) keys[k].i[0] = 0;
        if (keys[k].f) keys[k].f[0] = 0.0f;
        if (keys[k].slot) keys[k].slot[0] = 0;
    }

    uintptr_t entries = 0;
    if (!d.rd_u64(bag + DICT_ENTRIES, entries) || !entries) return 0;
    int32_t len = 0;
    if (!d.rd<int32_t>(entries + ARR_LEN, len) || len <= 0 || len > 512) return 0;

    int found = 0;
    for (int32_t i = 0; i < len && found < nk; ++i)
    {
        const uintptr_t e = entries + ARR_DATA + (uintptr_t)i * ENTRY_STRIDE;
        int32_t hash = 0;
        if (!d.rd<int32_t>(e, hash) || hash == 0) continue;
        uintptr_t kp = 0;
        if (!d.rd_u64(e + 8, kp) || !kp) continue;

        int32_t slen = 0;
        if (!d.rd<int32_t>(kp + STR_LEN, slen)) continue;
        if (slen <= 0 || slen > 48) continue;

        uint16_t wbuf[48];
        SIZE_T copied = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(kp + STR_CHARS), wbuf,
                               (SIZE_T)slen * 2, &copied) || copied != (SIZE_T)slen * 2) continue;

        for (int k = 0; k < nk; ++k)
        {
            if (keys[k].got) continue;
            const char* nm = keys[k].name;
            const size_t nl = strlen(nm);
            if ((size_t)slen != nl) continue;
            bool eq = true;
            for (int32_t ch = 0; ch < slen; ++ch)
            {
                if (wbuf[ch] != (uint16_t)(unsigned char)nm[ch]) { eq = false; break; }
            }
            if (!eq) continue;

            uintptr_t val = 0;
            if (!d.rd_u64(e + 16, val) || !val) continue;
            // 只认真值栈，+0x38 是旧缓存写不进游戏
            uintptr_t slot = tv_slot(d, val);
            uint64_t payload = 0;
            if (!slot || !d.rd_u64(slot, payload)) continue;

            if (keys[k].i) keys[k].i[0] = pick_int(payload);
            if (keys[k].f) keys[k].f[0] = pick_flt(payload);
            if (keys[k].slot) keys[k].slot[0] = val;
            keys[k].got = true;
            ++found;
            break;
        }
    }
    return found;
}

inline bool read_actor(ov::Data& d, uintptr_t p, Actor& a)
{
    a = Actor();
    a.p = p;
    uintptr_t bag = 0;
    if (!d.rd_u64(p + ACT_BAG, bag) || !bag) return false;

    int camp = 0, hp = 0, mx = 0, raw = 0, aid = 0, mid = 0;
    int un_dmg = 0, prev_death = 0;
    float ratio = 0.0f, dt_ratio = 0.0f;
    BagKey keys[10] = {
        { "Actor_Camp",            &camp,   nullptr, nullptr,           false },
        { "Actor_Hp",              &hp,     nullptr, &a.hp_vp,          false },
        { "Actor_MaxHp",           &mx,     nullptr, nullptr,           false },
        { "Actor_RawMaxHp",        &raw,    nullptr, nullptr,           false },
        { "Actor_HpRatio",         nullptr, &ratio,  &a.ratio_vp,       false },
        { "Actor_AvatarID",        &aid,    nullptr, nullptr,           false },
        { "Actor_HIG_MonsterID",   &mid,    nullptr, nullptr,           false },
        { "Actor_UnDamage",        &un_dmg, nullptr, &a.un_dmg_vp,      false },
        { "Actor_PreventDeath",    &prev_death, nullptr, &a.pd_vp,      false },
        { "Actor_DmgTakenRatioDelta", nullptr, &dt_ratio, &a.dt_vp,     false },
    };
    if (bag_read(d, bag, keys, 10) <= 0) return false;

    a.camp       = (camp >= 0 && camp <= 8) ? camp : 0;
    a.raw_camp   = a.camp;
    a.hp         = sane_int(hp) ? hp : 0;
    a.max_hp     = sane_int(mx) ? mx : 0;
    a.raw_max    = sane_int(raw) ? raw : 0;
    a.hp_ratio   = (ratio >= 0.0f && ratio <= 1.5f) ? ratio : 0.0f;
    a.avatar_id  = (aid > 0 && aid < 100000) ? aid : 0;
    a.monster_id = (mid > 0 && mid < 10000000) ? mid : 0;

    if (a.max_hp <= 0) a.max_hp = a.raw_max;
    if (a.max_hp <= 0) a.max_hp = a.hp;
    if (a.hp <= 0 && a.hp_ratio > 0.0f && a.max_hp > 0)
        a.hp = (int)(a.hp_ratio * (float)a.max_hp + 0.5f);

    // 以身份字段判定阵营：AvatarID 一定是我方，MonsterID 一定是敌方。
    // 不能只信 Actor_Camp，部分活动里它的编号与 1=我 2=敌 的约定相反。
    if (a.avatar_id > 0) a.camp = 1;
    else if (a.monster_id > 0) a.camp = 2;
    return true;
}

// 分帧扫实例，扫完退避
inline bool find_manager(ov::Data& d, ActCtx& c, int budget_ms, uintptr_t* out)
{
    *out = 0;
    // 已经找到过，先验一次还活着。空列表也是合法状态，不能因此丢弃管理器。
    if (c.mgr_addr)
    {
        uintptr_t k = 0;
        if (d.rd_u64(c.mgr_addr, k) && c.mgr_klass && k == c.mgr_klass &&
            ok_obj(d, c.mgr_addr))
        {
            uintptr_t arr = 0;
            if (d.rd_u64(c.mgr_addr + MGR_ARRAY, arr))
            {
                c.last_mgr = c.mgr_addr;
                *out = c.mgr_addr;
                return true;
            }
        }
        c.mgr_addr = 0;
        c.reset_scan();
    }

    if (!c.mgr_klass) return false;

    const uint64_t now = GetTickCount64();
    if (!c.found && now < c.next_scan) return false;

    if (c.regions.empty())
    {
        c.mgr_zero = 0;
        c.regions = d.rt.scan_regions();
        c.rcur = 0;
        c.ridx = 0;
        // 优先扫上一次管理器所在的内存块，战斗切换后通常仍分配在同一区域。
        if (c.last_mgr)
        {
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery((LPCVOID)c.last_mgr, &mbi, sizeof mbi) == sizeof mbi &&
                mbi.State == MEM_COMMIT && mbi.RegionSize >= 16)
            {
                bh3::Runtime::Region r{};
                r.base = (uintptr_t)mbi.BaseAddress;
                r.size = (size_t)mbi.RegionSize;
                c.regions.insert(c.regions.begin(), r);
            }
        }
        if (c.regions.empty()) { c.next_scan = now + 2000; return false; }
    }

    const uint64_t deadline = now + (uint64_t)budget_ms;
    static thread_local uintptr_t rbuf[8192];
    while (c.rcur < (int)c.regions.size())
    {
        const bh3::Runtime::Region& r = c.regions[(size_t)c.rcur];
        if (!r.base || r.size < 16) { ++c.rcur; c.ridx = 0; continue; }

        const size_t n = r.size / 8;
        if (c.ridx >= (int)n) { ++c.rcur; c.ridx = 0; continue; }

        const size_t left = n - (size_t)c.ridx;
        const size_t want = left < 8192 ? left : 8192;
        const uintptr_t base = r.base + (uintptr_t)c.ridx * 8;
        const uintptr_t cnt = read_chunk(base, rbuf, (uintptr_t)want);
        if (!cnt)
        {
            ++c.rcur;
            c.ridx = 0;
            continue;
        }

        for (uintptr_t i = 0; i < cnt; ++i)
        {
            if (rbuf[i] == c.mgr_klass && ok_obj(d, base + i * 8))
            {
                const uintptr_t p = base + i * 8;
                uintptr_t arr = 0;
                if (!d.rd_u64(p + MGR_ARRAY, arr)) continue;
                int32_t len = 0;
                if (arr && (!d.rd<int32_t>(arr + ARR_LEN, len) || len < 0 || len > 4096))
                    continue;
                // 优先采用有实体的管理器；空列表管理器先记下，全扫完仍没有活跃的再用。
                if (arr && len > 0)
                {
                    c.found = true;
                    c.mgr_addr = p;
                    c.last_mgr = p;
                    c.mgr_zero = 0;
                    *out = p;
                    return true;
                }
                if (!c.mgr_zero) c.mgr_zero = p;
            }
        }
        c.ridx += (int)cnt;
        if (GetTickCount64() >= deadline) return false;
    }

    if (c.mgr_zero)
    {
        c.found = true;
        c.mgr_addr = c.mgr_zero;
        c.last_mgr = c.mgr_zero;
        *out = c.mgr_zero;
        c.mgr_zero = 0;
    }
    else
    {
        c.next_scan = GetTickCount64() + 3000;
    }
    c.regions.clear();
    c.rcur = 0;
    c.ridx = 0;
    return *out != 0;
}

inline bool read_all(ov::Data& d, uintptr_t mgr, ActState& st)
{
    uintptr_t arr = 0;
    if (!d.rd_u64(mgr + MGR_ARRAY, arr)) return false;
    // arr 为空或长度为 0 是合法的“空场”状态，不能因此丢弃管理器，
    // 否则波次之间会反复全堆重扫，下一场战斗的信息迟迟读不到。
    int32_t len = 0;
    if (arr && (!d.rd<int32_t>(arr + ARR_LEN, len) || len < 0 || len > 4096)) return false;
    if (!arr) len = 0;

    uintptr_t old_p[kMaxActors];
    bool      old_k[kMaxActors];
    int       old_id[kMaxActors];
    const int old_n = st.n < kMaxActors ? st.n : kMaxActors;
    for (int i = 0; i < old_n; ++i)
    {
        old_p[i] = st.list[i].p;
        old_k[i] = st.list[i].kill_req;
        old_id[i] = st.list[i].monster_id;
    }

    st.manager = mgr;
    st.act_arr = arr;
    st.act_len = len;
    st.n = 0;
    st.ally_n = 0;
    st.enemy_n = 0;

    const int cap = len < kMaxActors ? len : kMaxActors;
    for (int i = 0; i < cap; ++i)
    {
        uintptr_t p = 0;
        if (!d.rd_u64(arr + ARR_DATA + (uintptr_t)i * 8, p) || !p) continue;
        Actor a;
        if (!read_actor(d, p, a)) continue;
        // 地址和怪物 ID 都一致才算同一只，防止池复用地址继承秒杀标记
        for (int k = 0; k < old_n; ++k)
            if (old_p[k] == p && old_id[k] == a.monster_id) { a.kill_req = old_k[k]; break; }
        st.list[st.n++] = a;
    }

    // 同一活动内我方 Camp 编号是一致的：只要有一个带 AvatarID 的单位，
    // 就把同 Camp 的所有单位都划为我方，避免某些友军没有 AvatarID 时被误杀。
    int ally_raw = -1;
    for (int i = 0; i < st.n; ++i)
        if (st.list[i].avatar_id > 0 && st.list[i].monster_id <= 0)
        {
            ally_raw = st.list[i].raw_camp;
            break;
        }
    if (ally_raw >= 0)
    {
        st.ally_camp_raw = ally_raw;
        for (int i = 0; i < st.n; ++i)
            if (st.list[i].raw_camp == ally_raw) st.list[i].camp = 1;
    }
    else if (st.n > 0)
    {
        st.ally_camp_raw = -1;
    }
    // st.n == 0 时保留上一波的我方 Camp 编号，避免下一波先出敌人时丢失保护。

    for (int i = 0; i < st.n; ++i)
    {
        if (st.list[i].camp == 1) ++st.ally_n;
        else if (st.list[i].camp == 2) ++st.enemy_n;
    }

    st.ready = true;
    st.in_act = st.ally_n > 0 && st.enemy_n > 0;
    st.t = GetTickCount64();
    if (st.n == 0)
    {
        if (!st.empty_t) st.empty_t = st.t;
    }
    else st.empty_t = 0;

    if (st.in_act)
        snprintf(st.msg, sizeof st.msg, "战斗中 我%d 敌%d", st.ally_n, st.enemy_n);
    else if (st.n == 0)
        snprintf(st.msg, sizeof st.msg, "无实体");
    else
        snprintf(st.msg, sizeof st.msg, "就绪 %d", st.n);
    return true;
}

inline void refresh(ov::Data& d, ActState& st, ActCtx& c, int budget_ms)
{
    const uint64_t t0 = GetTickCount64();
    scan_klasses(d, c, budget_ms);

    if (!st.ready)
    {
        if (c.mgr_addr)
        {
            if (read_all(d, c.mgr_addr, st))
            {
                st.scan_ms = (int)(GetTickCount64() - t0);
                return;
            }
            c.reset_scan();
        }

        if (!c.mgr_klass)
        {
            snprintf(st.msg, sizeof st.msg, "非活动场景");
        }
        else
        {
            uintptr_t mgr = 0;
            if (find_manager(d, c, budget_ms, &mgr) && mgr) read_all(d, mgr, st);
            else snprintf(st.msg, sizeof st.msg, "扫描 %d/%d", c.rcur, (int)c.regions.size());
        }
        st.scan_ms = (int)(GetTickCount64() - t0);
        return;
    }

    // 数值只按间隔刷新，写血走每帧路径
    if (t0 - st.t < (uint64_t)kRefreshMs)
    {
        st.scan_ms = (int)(GetTickCount64() - t0);
        return;
    }

    if (!read_all(d, st.manager, st))
    {
        if (++st.fail_n >= 3)
        {
            st = ActState();
            c.reset_scan();
            snprintf(st.msg, sizeof st.msg, "已失效，重扫");
        }
        else
        {
            snprintf(st.msg, sizeof st.msg, "读取失败，重试 %d/3", st.fail_n);
        }
    }
    else
    {
        st.fail_n = 0;
    }

    st.scan_ms = (int)(GetTickCount64() - t0);
}

// 我方压满血并开免伤免死，每帧都压
inline int god_ally(ov::Data& d, ActState& st)
{
    int n = 0;
    for (int i = 0; i < st.n; ++i)
    {
        Actor& a = st.list[i];
        // 优先按“与我方同 Camp”判定；没有身份样本时退回 ID 判定。
        const bool is_ally = (a.avatar_id > 0 && a.monster_id <= 0) ||
                             (st.ally_camp_raw >= 0 && a.raw_camp == st.ally_camp_raw) ||
                             (st.ally_camp_raw < 0 && a.camp == 1 && a.monster_id <= 0);
        if (!is_ally) continue;
        const int target = a.max_hp > 0 ? a.max_hp : a.raw_max;
        if (a.hp_vp && target > 0 && write_prop(d, a.hp_vp, target, (float)target)) ++n;
        // 免伤免死一起开，光满血挡不住回合结算
        if (a.un_dmg_vp) write_prop(d, a.un_dmg_vp, 2, 2.0f);
        if (a.pd_vp) write_prop(d, a.pd_vp, 1, 1.0f);
        if (a.ratio_vp) write_prop(d, a.ratio_vp, 1, 1.0f);
        // 承伤增量压到 -1，抵消基础倍率
        if (a.dt_vp) write_prop(d, a.dt_vp, -1, -1.0f);
    }
    return n;
}

// 标记并压血，之后每帧由 enforce_kills 压着
inline bool mark_kill(ov::Data& d, Actor& a)
{
    // 只杀“有怪物 ID 且没有角色 ID”的目标；带 AvatarID 的我方单位永久排除。
    if (a.camp != 2 || a.avatar_id > 0 || a.monster_id <= 0 || !a.hp_vp) return false;
    if (a.kill_req) return false;  // 已标记的交给 enforce_kills 每帧压血
    a.kill_req = true;
    // 先撤免伤免死，否则血写 0 也打不死
    if (a.un_dmg_vp) write_prop(d, a.un_dmg_vp, 0, 0.0f);
    if (a.pd_vp) write_prop(d, a.pd_vp, 0, 0.0f);
    write_prop(d, a.hp_vp, 0, 0.0f);
    if (a.ratio_vp) write_prop(d, a.ratio_vp, 0, 0.0f);
    a.hp = 0;
    a.hp_ratio = 0.0f;
    return true;
}

inline int kill_all(ov::Data& d, ActState& st)
{
    int n = 0;
    for (int i = 0; i < st.n; ++i)
        if (mark_kill(d, st.list[i])) ++n;
    return n;
}

// 每帧压制已标记敌人
inline int enforce_kills(ov::Data& d, ActState& st)
{
    int n = 0;
    for (int i = 0; i < st.n; ++i)
    {
        Actor& a = st.list[i];
        if (!a.kill_req || a.camp != 2 || !a.hp_vp) continue;
        // 二次保险：绝不对带 AvatarID 的单位或与我方同 Camp 的单位写血。
        if (a.avatar_id > 0 || a.monster_id <= 0 ||
            (st.ally_camp_raw >= 0 && a.raw_camp == st.ally_camp_raw))
        {
            a.kill_req = false;
            continue;
        }
        if (a.un_dmg_vp) write_prop(d, a.un_dmg_vp, 0, 0.0f);
        if (a.pd_vp) write_prop(d, a.pd_vp, 0, 0.0f);
        if (write_prop(d, a.hp_vp, 0, 0.0f)) ++n;
        if (a.ratio_vp) write_prop(d, a.ratio_vp, 0, 0.0f);
        a.hp = 0;
        a.hp_ratio = 0.0f;
    }
    return n;
}

} // namespace ova
