# ESP 绘制卡顿与"跑框追"根因调研

> 调研范围：`overlay/ov_gui.cpp`、`overlay/ov_native.h`、`overlay/ov_data.h`、`sdk/bh3_sdk.h`、`libs/ImGui/*`、运行时日志 `overlay/bh3_overlay.log`。
> 全程只读，未修改任何代码。
> 计时量级均为**估算**（无法向游戏进程注入做实测采样），凡估算处标注「估算」；凡由代码/日志直接证明处标注「确认」。
> 硬件与实测帧率未知，故耗时为典型 Windows 10/11 x64 桌面场景数量级。

---

## 结论摘要（按影响排序）

1. **「跑框追」的第一根因是数据刷新周期 200ms 与画面 16.7ms 的两个数量级错配**——框位置是 200ms 一档的阶梯函数，角色是 60fps 连续移动，平均滞后 6 帧、最差 13 帧（`ov_gui.cpp:165`）。
2. **「跑框追」的第二个机制是零阶保持本身**：快照不是连续量而是每 200ms 跳一次的阶梯，中间 11 帧画面里框完全静止、角色已经走远，于是人眼看到"停 → 瞬移 → 停"。这一条与第 1 条同源但表现不同：即使把延迟压到 0，只要采样仍低于帧率，跳变感就还在。
3. **ESP 的实际绘制极其便宜**（5 个怪 × 2 次 `w2s` + 4 个 draw call，估算 5–20µs/帧），它既不是卡顿主因，也不是跑框追主因。
4. **卡顿的真实大头是 UI 侧**：`ui_logs()` 在持有 `g_log_mtx` 期间对最多 400 条日志逐行 `ImGui::TextUnformatted`（`ov_gui.cpp:398-400`），估算 1–4ms/帧，打开"日志"标签页即掉帧。
5. **`hk_present` 每帧在渲染线程上同步执行一次全量 `ovn::refresh`**（`ov_gui.cpp:168`），构成每 12 帧一次的周期性尖峰（估算 100–600µs），是"顿挫感"的第二个来源。
6. **`ov::Data::rd()` 的每次读都用无缓存的 `VirtualQuery`**（`ov_data.h:35-43`），一次 `refresh` 约触发 60+ 次内核调用；`sdk/bh3_sdk.h:586` 里的 `mapped_fast()` 有区段缓存但**没被 overlay 的读路径使用**。
7. **`logf()` 是同步 `fopen`/`fprintf`/`fclose` + 两次 `OutputDebugStringA`**（`ov_gui.cpp:113-116`），挂在渲染线程上，3s 一次 + 每次 UI 交互一次 + 每次相机/无敌状态变化一次。
8. **坐标空间有两个互不相干的尺寸源**：`draw_esp` 用相机 `pixelWidth/pixelHeight`（`ov_native.h:136-137` → `s.w/s.h`），ImGui 用 `io.DisplaySize`（`imgui_impl_win32.cpp:391-392`）。两者不等时框整体缩放/偏移（"框偏"）。
9. `io.MouseDrawCursor` 每帧切换、`StyleColorsDark` 每帧重设、字体每帧构造——**三项全部排除**，见 `imgui_impl_win32.cpp:406-412`、`ov_gui.cpp:269`、`imgui_impl_dx11.cpp:594-601`。
10. `install_wndproc()` 每帧无条件调用**确认存在**（`ov_gui.cpp:450`），但它不是每帧 `fopen` 的来源——日志里 `WndProc 被改回去了` 出现 **0 次**。
11. **反向验证成立**：`g_esp_on=false` 时 `draw_esp` 在 `ov_gui.cpp:282` 提前 return，但 `install_wndproc`、`tick_render_thread`（含全量 refresh）、`ui_func` 的 State 拷贝、`ImGui_ImplDX11_RenderDrawData` 的全状态备份全部照旧 ⇒ **关掉 ESP 后卡顿不会消失，只会少一层框**。
12. `g_desc_check` 初值 `0` 时首次调用会多走一次 `GetDesc`（`ov_gui.cpp:201-202`），之后每 59 帧一次，**代价可忽略，排除**；但 `g_rtv` 创建失败时会退化成**每帧** `GetDesc + GetBuffer + CreateRenderTargetView`。
13. **「相机失效导致框全灭」经日志实证排除**：659 条快照行交叉统计，`角色 > 0 且相机 无` = **0 条**，`cam ok/无` 的 39 次翻转全部落在"还没进关卡、实体列表为空"的加载期；真正有怪的 22 条战斗快照 **22/22 全部 `cam_ok = true`**。代码上仍有缺陷（`ov_gui.cpp:288` 无兜底），但本次跑框追与它无关。
14. **实测规模极小**：怪物数分布为 `怪0`×637、`怪1`×5、`怪2`×4、`怪3`×4、`怪4`×2、`怪5`×7（最大 5 个）；角色数最大 5。因此 `State` 深拷贝、`w2s`、`AddRect` 的总量都在个位数到十几的量级，**任何"绘制复杂度导致卡顿"的假设都被数据否定**。

---

## 一、卡顿根因分析

### 1.1 `hk_present` 每帧同步工作总量清单

`ov_gui.cpp:422-491` 是每帧无条件执行的完整序列。逐项拆解：

| # | 位置 | 每帧动作 | 频率 | 估计耗时（估算） | 判定 |
|---|---|---|---|---|---|
| 1 | `ov_gui.cpp:425` | `init_started.compare_exchange_strong` | 每帧 | <0.01µs | 排除 |
| 2 | `ov_gui.cpp:431-448` | 6× `GetAsyncKeyState` | 每帧 | 3–15µs | 疑似（小） |
| 3 | `ov_gui.cpp:450` | `install_wndproc()` → `GetAncestor` + 1~2× `GetWindowLongPtrW` | 每帧 | 3–10µs | 疑似（小） |
| 4 | `ov_gui.cpp:451` | `tick_render_thread()` → `GetTickCount64` | 每帧 | ~0.1µs | 排除 |
| 5 | `ov_gui.cpp:165-178` | **全量 `ovn::refresh`** | **每 200ms** | **100–600µs** | **确认** |
| 6 | `ov_gui.cpp:180-188` | god_mode 轮询（仅状态变化时才 patch） | 每 60ms | ~0.2µs | 排除 |
| 7 | `ov_gui.cpp:453` → `200-229` | `ensure_rtv`：`--g_desc_check` | 每帧 | <0.01µs | 排除 |
| 8 | `ov_gui.cpp:205` | `sc->GetDesc(&d)` | 每 59 帧 | ~1µs（摊销 <0.02µs） | 排除 |
| 9 | `ov_gui.cpp:456` | `io.MouseDrawCursor = ...` | 每帧 | ~0.01µs | **排除** |
| 10 | `ov_gui.cpp:458-460` | `ImGui_ImplDX11_NewFrame` + `ImGui_ImplWin32_NewFrame` + `ImGui::NewFrame` | 每帧 | 10–40µs | 疑似（小） |
| 11 | `ov_gui.cpp:462-468` | FPS 统计 | 每帧 | ~0.1µs | 排除 |
| 12 | `ov_gui.cpp:470` → `281-327` | `draw_esp()`（含一次 `State` 深拷贝） | 每帧 | 5–25µs | 排除（主因无关） |
| 13 | `ov_gui.cpp:471` → `405-420` | `draw_ui()`（含一次 `State` 深拷贝 + 全 UI） | 每帧（菜单开时） | 200–800µs | 疑似 |
| 14 | `ov_gui.cpp:398-400` | `ui_logs()` 400 行持锁 `TextUnformatted` | 每帧（日志页开时） | **1–4ms** | **确认（条件）** |
| 15 | `ov_gui.cpp:475-487` | `OMGetRenderTargets` + `RSGetViewports` + `OMSetRenderTargets`×2 + `Release`×2 | 每帧 | 3–10µs | 排除 |
| 16 | `ov_gui.cpp:482` → `imgui_impl_dx11.cpp:201-309` | **`ImGui_ImplDX11_RenderDrawData` 全 DX11 状态备份/恢复** | 每帧 | **40–150µs** | **确认** |
| 17 | `ov_gui.cpp:490` | `g_present_orig(...)` | 每帧 | 游戏自身 | — |

**每帧基线总开销（菜单关、ESP 开）：估算 100–300µs ≈ 60fps 下 0.6%–1.8%。**
单看这张表，**overlay 框架本身不足以造成"极其卡顿"**。真正能造成肉眼可见掉帧的是第 14 项（ms 级）和第 5 项（周期性尖峰），详见下文。

---

### 1.2 `logf()` 同步磁盘 IO —— 确认存在，但不是每帧

```cpp
// ov_gui.cpp:102-117
static void logf(const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt);
    _vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    buf[sizeof buf - 1] = 0;
    {
        std::lock_guard<std::mutex> lk(g_log_mtx);
        if (g_logs.size() > 400) g_logs.erase(g_logs.begin(), g_logs.begin() + 150);
        g_logs.push_back(buf);
    }
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
    FILE* f = fopen("D:\\My Projects\\Honkai Impact\\overlay\\bh3_overlay.log", "a");
    if (f) { fprintf(f, "%s\n", buf); fclose(f); }
}
```

**确认**：每次调用 = 1 次 `CreateFile`（含路径解析 + NTFS + 可能的杀软过滤驱动）+ 1 次写 + 1 次 `CloseHandle` + 2 次 `OutputDebugStringA`。估算 100–500µs/次；**若有调试器（Visual Studio / WinDbg / x64dbg）附加，`OutputDebugStringA` 每次都要走一次调试事件，可达 0.5–2ms**。

调用点与频率（全部在渲染线程上，因为调用者都是 `hk_present` 的直接/间接子调用）：

| 调用点 | 触发条件 | 频率 |
|---|---|---|
| `ov_gui.cpp:175` | `tick_render_thread` 内 `now - last_log >= 3000` | **每 3 秒 1 次**（且只在 refresh 执行的那一帧，见 `:173` 缩进在 `:165` 块内） |
| `ov_gui.cpp:186` | 无敌状态变化时 | 每次点"无敌" 1 次 |
| `ov_gui.cpp:196` | 秒杀请求 | 每次 F5/按钮 1 次 |
| `ov_gui.cpp:440` | F3 切 ESP | 每次 1 次 |
| `ov_gui.cpp:446` | F6 切无敌 | 每次 1 次 |
| `ov_gui.cpp:366` | UI 里点无敌 checkbox | 每次 1 次 |
| `ov_gui.cpp:374` | UI 里点"秒杀全部怪物" | 每次 1 次 |
| `ov_gui.cpp:142` | `hook_one` 发现 WndProc 被改回 | **日志中 0 次** |
| `ov_gui.cpp:226` | `CreateRenderTargetView` 失败 | 日志中 0 次 |
| `ov_gui.cpp:263/267/277` 等 | 初始化 | 一次性 |

**判定：确认，但频率低（3s 档），不是"每帧卡顿"的主因；它是"每次按键/每 3 秒一次的小顿"的来源。**

**反向证据（排除一条重要怀疑）**：如果游戏每帧把 WndProc 改回去，`hook_one` 会在 `ov_gui.cpp:140` 检测到并走 `:142` 重新挂钩 + `logf` ⇒ 每帧一次 `fopen`，那将是致命的。实测日志：

```
358: [+] WndProc 已挂钩 hwnd=0000000001D40BE4
```

`overlay/bh3_overlay.log` 全文 **只有 1 次挂钩、0 次「被改回去了」** ⇒ **游戏没有回改 WndProc，这条路径不成立，排除。**

---

### 1.3 每帧 `ovn::State` 拷贝次数与代价

`State` 的定义：

```cpp
// ov_native.h:35-52
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
```

每帧/每次的拷贝点，逐个统计：

| 位置 | 拷贝内容 | 频率 | 备注 |
|---|---|---|---|
| `ov_gui.cpp:286` `s = g_nv;` | 完整 `State`（含 2 个 vector + 全部 `std::string`） | **每帧 1 次**（`g_esp_on` 为真时） | `draw_esp` |
| `ov_gui.cpp:336` `st = g_nv;` | 同上 | **每帧 1 次**（菜单开时） | `ui_func` |
| `ov_gui.cpp:171` `g_nv = s;` | 同上 | 每 200ms 1 次 | `tick_render_thread` 发布快照 |
| `ov_gui.cpp:193` `s = g_nv;` | 同上 | 每次秒杀请求 1 次 | kill 分支 |

**代价拆解**：
- `std::vector<Ent>` 拷贝 = 1 次 `malloc` + N × `sizeof(Ent)` 的 memcpy。
- `Ent::name` 是 `std::string`：`sizeof(Ent)` 在 x64 MSVC/MinGW 下约 64 字节（8+8+32+12 对齐）。日志里的名字如 `Mono_DG_030`、`MonoFukaNL` 都在 **SSO 范围内（≤15 字符）**，拷贝走内联 buffer，**不产生额外 malloc**。
- 日志实测实体数极小（659 条快照行精确统计）：`怪0`×637、`怪1`×5、`怪2`×4、`怪3`×4、`怪4`×2、`怪5`×7 ⇒ **怪物最大 5 个**；`角色0`×415、`角色1`×1、`角色2`×159、`角色3`×46、`角色4`×32、`角色5`×6 ⇒ **角色最大 5 个**。
- 因此 N = 怪物 + 角色 ≤ 10 时：**2 次 vector 分配 + 约 0.6KB memcpy ≈ 1–5µs/帧**。

**判定：确认结构上存在冗余拷贝（每帧 2 次完全不必要的全量深拷贝），但当前规模下代价 1–5µs，排序靠后，不是卡顿根因。**
**风险提示**：`read_list` 的 cap 是 1024（`ov_native.h:72`），`draw_esp` 的绘制上限也是 400（`ov_gui.cpp:324`）。若某关卡的怪物列表真的接近 1024，单次拷贝就变成 1024×64B ≈ 64KB memcpy（约 10–20µs），每帧 2 次 = 20–40µs，那时它才会进入主因量级。

**顺带确认一处数据竞争（低危）**：`ov_gui.cpp:394` `ImGui::TextDisabled("%zu 条", g_logs.size());` 在**不持锁**的情况下读 `g_logs.size()`，而同文件的 `logf` 会 `push_back`/`erase`。由于两者目前都在渲染线程上，**实际不会崩，但这是隐患**（一旦把采集挪到别的线程就会变成真 UB）。

---

### 1.4 `ovn::refresh()` 在渲染线程上每 200ms 执行 —— 确认，周期性尖峰

```cpp
// ov_gui.cpp:161-178
static void tick_render_thread() {
    static uint64_t last_nv = 0, last_god = 0, last_log = 0;
    if (!g_data_ready) return;
    uint64_t now = GetTickCount64();
    if (now - last_nv >= 200) {
        last_nv = now;
        ovn::State s;
        ovn::refresh(g_data, s);
        {
            std::lock_guard<std::mutex> lk(g_nv_mtx);
            g_nv = s;
        }
        ...
```

`refresh()` 的全部工作：

```cpp
// ov_native.h:155-172
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
```

**（a）内存读取的真实代价：每次 `rd()` 都是一次内核调用。**

```cpp
// ov_data.h:35-50
static bool readable(uintptr_t a, size_t n) {
    if (!a) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery((void*)a, &mbi, sizeof mbi) != sizeof mbi) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    size_t avail = (size_t)((uintptr_t)mbi.BaseAddress + mbi.RegionSize - a);
    return n <= avail;
}

template <class T>
bool rd(uintptr_t a, T& out) const {
    if (!readable(a, sizeof(T))) return false;
    memcpy(&out, (void*)a, sizeof(T));
    return true;
}
```

**关键发现：`readable()` 是无条件的 `VirtualQuery`，没有任何区段缓存。** 而 SDK 里**明明已经有带缓存的版本**：

```cpp
// sdk/bh3_sdk.h:586-602
bool mapped_fast(uintptr_t addr, size_t len) const {
    if (!addr) return false;
    if (addr >= mtop_ && addr < mbot_ && len <= mbot_ - addr) return true;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
    ...
    mtop_ = top; mbot_ = bot;
    return len <= mbot_ - addr;
}
```

注释本身就写明了它存在的理由（`bh3_sdk.h:578-585`）：
> "A blind 8-byte walk over a multi-hundred-MB region will eventually touch an uncommitted hole... Everything therefore goes through mapped_fast(), which caches the last VirtualQuery region; the il2cpp heap is one huge MEM_COMMIT reservation, so **the cache turns this into two compares**."

**overlay 的 `ov::Data::rd` 没有走这条路** ⇒ 每一字节读都付一次 `VirtualQuery` 的代价（估算 0.8–2µs，含 syscall）。

**（b）一次 `refresh` 的 `VirtualQuery` 计数（确认，按代码逐行累加）：**

| 步骤 | 位置 | 次数 |
|---|---|---|
| `mgr_from` × 2（8 级链，每级 1 次 `rd`） | `ov_native.h:54-70`，每个最多 7 次 `rd` | 2×7 = **14** |
| `read_list` 头（items/size） | `ov_native.h:77` | 2×2 = **4** |
| `read_list` 元素指针 | `ov_native.h:81` | 2×N |
| `fill_ents`：klass + x + y + z | `ov_native.h:93,96,97,98` | 4N |
| **总计（N 个实体）** | | **18 + 6N** |

N = 10（日志实测最大规模：5 怪 + 5 角色）⇒ **约 78 次 `VirtualQuery`**。日志里 637 条 `怪 0` 的空转期：N = 0 ⇒ 仍有 **18 次**。

**（c）每个实体的 `d.name_of()` → `rt.class_name()`：**

```cpp
// ov_data.h:30-33
const char* name_of(uintptr_t klass) const {
    const char* n = klass ? rt.class_name(klass) : nullptr;
    return n ? n : "?";
}

// sdk/bh3_sdk.h:232-234
const char* class_name(uintptr_t klass) const {
    return klass ? reinterpret_cast<Il2CppClass*>(klass)->name : nullptr;
}
```

字段布局：

```cpp
// sdk/bh3_sdk.h:113-116
struct Il2CppClass {
    uint8_t     pad00[0x20];
    const char* name;            // +0x20  direct decrypted pointer
```

**判定：`class_name` 本身是「一次指针解引用读 `klass+0x20`」，代价可忽略（~1ns）。** 但调用方 `fill_ents` 立刻把它变成一个 `std::string`：

```cpp
// ov_native.h:87-101
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
```

`e.name = nm` 是 **每实体一次 `std::string` 构造**（SSO 下无 malloc，但仍有 `strlen` + 16 字节内联写入 + 长度记录）。同理 `out.push_back(e)` 是一次 `Ent` 拷贝（若 `reserve` 已生效则不重分配，`ov_native.h:89` 已 `reserve`，**这一点做得对**）。

**判定：`class_name` 不是瓶颈（用户猜想的方向在此排除）；真正的小成本在 `std::string` 构造。**

**（d）4 个游戏函数调用 —— 确认存在，量级未知，是最大不确定性：**

```cpp
// ov_native.h:127-141
inline void refresh_camera(ov::Data& d, State& s) {
    typedef void* (*GetMain)(void*);
    typedef int (*GetInt)(void*, void*);
    typedef void (*GetMat)(void*, void*, void*);
    uintptr_t ua = d.ua;
    void* cam = ((GetMain)(ua + RVA_GET_MAIN))(nullptr);
    ...
    int w = ((GetInt)(ua + RVA_PIXEL_W))(cam, nullptr);
    int h = ((GetInt)(ua + RVA_PIXEL_H))(cam, nullptr);
    ...
    ((GetMat)(ua + RVA_W2C))(cam, v, nullptr);
    ((GetMat)(ua + RVA_PROJ))(cam, p, nullptr);
```

即 `Camera.get_main` / `pixelWidth` / `pixelHeight` / `worldToCameraMatrix` / `projectionMatrix` —— **5 次跨进 IL2CPP 托管方法的调用**（不是读字段，是执行游戏代码）。

估算：
- 若这些 getter 是纯字段返回：每次 0.1–1µs，总共 <5µs。
- 若涉及托管运行时检查、GC 状态切换、`il2cpp_thread_attach` 或返回 `Matrix4x4` 值类型（`GetMat(void*, void*, void*)` 的签名暗示通过 out 指针回写 16 个 float，走的是隐式 thiscall + 托管栈帧）：每次 1–20µs，**总计 10–100µs**。
- 若渲染线程尚未向 GC 注册（非主线程调托管方法），**每次调用可能触发额外的线程状态切换**，量级可能到几十微秒。

**判定：确认调用存在，具体代价必须实测。这是本次调研最大的未确认项（见第五节）。**

**（e）尖峰而非均值：**
`refresh` 只在命中 200ms 的那一帧执行，因此它给**每第 12 帧**带来一次 100–600µs 的额外延迟（估算），其余 11 帧为 0。这正是"周期性顿挫"的典型形态——**平均掉 0.5% 完全无感，但每 200ms 一次 3% 的尖峰，肉眼能看出节奏。**

**（f）`GetTickCount64` 的量化误差：**
`ov_gui.cpp:164` `uint64_t now = GetTickCount64();` 的分辨率是系统时钟中断周期（默认 **15.6ms**）。因此 `now - last_nv >= 200` 实际在 **200–215.6ms** 之间触发，平均 207.8ms。**这会额外增加约 8ms 的抖动**，也让快照间隔本身不稳定。

---

### 1.5 `ui_logs()` 持锁逐行绘制 400 行 —— 确认，是目前唯一能到 ms 级的项

```cpp
// ov_gui.cpp:391-403
static void ui_logs() {
    if (ImGui::Button("清空")) { std::lock_guard<std::mutex> lk(g_log_mtx); g_logs.clear(); }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu 条", g_logs.size());
    ImGui::Separator();
    ImGui::BeginChild("log", ImVec2(0, 0), true);
    {
        std::lock_guard<std::mutex> lk(g_log_mtx);
        for (auto& s : g_logs) ImGui::TextUnformatted(s.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
}
```

**确认的执行事实：**
1. 循环上限由 `logf` 维持：`if (g_logs.size() > 400) g_logs.erase(...)`（`ov_gui.cpp:110`）⇒ 最多 400 条。
2. **整段循环在 `g_log_mtx` 内部**（`:398-400`），即 400 次 `ImGui::TextUnformatted` 全部持锁。
3. 每条 `TextUnformatted` 会走 `CalcTextSizeA` + 逐字符 glyph 查找 + 生成顶点。中文每字一个 glyph，一行约 20–40 字 ⇒ **约 8000–16000 次 glyph 查找 + 上万顶点/帧**。估算 **1–4ms/帧**。
4. 且 `g_logs.erase(begin, begin+150)` 每次溢出都搬 250 个 `std::string`（`ov_gui.cpp:110`）。

**死锁风险判定：实际不死锁。** `logf` 与 `ui_logs` **都在渲染线程**（`hk_present` → `draw_ui` → `ui_logs`；`hk_present` → `tick_render_thread` → `logf`），同一线程不可重入同一 `std::mutex`——但两者不嵌套，`ui_logs` 内部不调 `logf`，所以**同一线程顺序获取/释放，不会自锁**。`boot()` 线程（`ov_gui.cpp:543`）只在启动期调 `logf`，与 UI 无关。

**但存在真实的"卡顿"机制**：持锁期间若 `boot` 线程或未来任何后台线程要 `logf`，会被阻塞在 `g_log_mtx` 上 1–4ms；反之亦然。**当前是"性能问题"，不是"死锁问题"，但把采集挪线程后会立刻变成死锁/长时间阻塞风险。**

**判定：确认（条件触发——只在"日志"标签页处于激活状态时）。这是 UI 交互时"极其卡顿"的直接解释。**

> 补充：ImGui 只绘制 `BeginTabItem` 返回 true 的分支（`ov_gui.cpp:414`），因此日志页未激活时该成本为 0。

---

### 1.6 `install_wndproc()` 每帧无条件调用 —— 确认，但代价小

```cpp
// ov_gui.cpp:450
        install_wndproc();
        tick_render_thread();
```

```cpp
// ov_gui.cpp:154-159
static void install_wndproc() {
    if (!g_hwnd) return;
    hook_one(g_hwnd);
    HWND root = GetAncestor(g_hwnd, GA_ROOT);
    if (root && root != g_hwnd) hook_one(root);
}
```

```cpp
// ov_gui.cpp:136-144
static void hook_one(HWND h) {
    if (!h || !IsWindow(h)) return;
    for (int i = 0; i < g_hook_n; ++i) {
        if (g_hooks[i].h != h) continue;
        if (GetWindowLongPtrW(h, GWLP_WNDPROC) == (LONG_PTR)hk_wndproc) return;
        g_hooks[i].orig = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)hk_wndproc);
        logf("[+] WndProc 被改回去了，已重新挂钩 hwnd=%p", (void*)h);
        return;
    }
```

**每帧固定代价**：`IsWindow` + `GetWindowLongPtrW`（可能 2 次）+ `GetAncestor`。这三个都是 `win32u` 全用户态调用（Windows 8+ 起窗口管理器在用户态），**不需要内核切换**。估算 **3–10µs/帧**。

**判定：确认每帧无条件调用（设计上应该只挂一次），但代价小，不是卡顿主因。**
**真正值得关注的是它的语义**：这是一个"每帧自愈"设计——只有当 `GetWindowLongPtrW != hk_wndproc` 时才会写。日志证明**从未触发过**，说明该自愈逻辑是纯开销。

---

### 1.7 `ensure_rtv()` 与 `g_desc_check` 计数逻辑

```cpp
// ov_gui.cpp:200-213
static bool ensure_rtv(IDXGISwapChain* sc) {
    if (--g_desc_check > 0 && g_rtv) return true;
    g_desc_check = 60;

    DXGI_SWAP_CHAIN_DESC d{};
    if (FAILED(sc->GetDesc(&d))) return g_rtv != nullptr;
    int bw = (int)d.BufferDesc.Width, bh = (int)d.BufferDesc.Height;
    if (!bw || !bh) {
        RECT r{};
        if (g_hwnd && GetClientRect(g_hwnd, &r)) { bw = r.right - r.left; bh = r.bottom - r.top; }
        if (!bw || !bh) return g_rtv != nullptr;
    }
    g_sw = (float)bw; g_sh = (float)bh;
```

`g_desc_check` 声明：`ov_gui.cpp:75` `static int g_desc_check = 0;`

**初值 0 时的行为（确认）**：
- 第 1 帧：`--g_desc_check` → **-1**，`-1 > 0` 为假 → 短路，不进 `&& g_rtv` → **落入慢路径**，`GetDesc` + 创建 RTV，并把 `g_desc_check` 设回 60。
- 第 2–59 帧：`--` 得 59…1，均 `> 0` → **快路径直接返回**。
- 第 60 帧：`--` 得 0，`0 > 0` 为假 → 慢路径。
⇒ **稳态是每 59 帧一次 `GetDesc`**。

**判定：初值 0 导致的"首帧多走一次慢路径"是良性的（而且首次本来就必须创建 RTV，所以是**正确**的初始化语义），不是 bug。`GetDesc` 每 59 帧一次 ≈ 摊销 <0.02µs/frame，完全可忽略，排除。**

**但有一个真实的退化路径（疑似）**：
```cpp
    if (g_rtv && g_rtv_w == bw && g_rtv_h == bh) return true;
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(sc->GetBuffer(0, kIID_ID3D11Texture2D, (void**)&bb)) || !bb) return false;
```
如果 `GetBuffer` 或 `CreateRenderTargetView` 持续失败，`g_rtv` 保持 null ⇒ 下一帧 `--g_desc_check > 0 && g_rtv` 的 `g_rtv` 为假 ⇒ **每帧都走慢路径**，每帧 `GetDesc` + `GetBuffer` + `CreateRenderTargetView`，且 `:226` 每帧一次 `logf` ⇒ **每帧一次 `fopen`/`fclose`**（估算 100–500µs）。
日志里没有 `CreateRenderTargetView 失败`，故**本次运行未触发，判定为条件性风险而非当前根因**。但这是整个 overlay 里唯一能造成"每帧磁盘 IO"的路径，值得留意。

---

### 1.8 ImGui 侧三项怀疑 —— 全部排除

**（a）`io.MouseDrawCursor` 每帧切换 —— 排除**

```cpp
// ov_gui.cpp:455-456
            if (!g_menu_open.load()) { io.WantCaptureMouse = false; io.WantCaptureKeyboard = false; }
            io.MouseDrawCursor = g_menu_open.load();
```

ImGui 1.90.6 里的实际用途：

```cpp
// libs/ImGui/imgui_impl_win32.cpp:406-412
    // Update OS mouse cursor with the cursor requested by imgui
    ImGuiMouseCursor mouse_cursor = io.MouseDrawCursor ? ImGuiMouseCursor_None : ImGui::GetMouseCursor();
    if (bd->LastMouseCursor != mouse_cursor)
    {
        bd->LastMouseCursor = mouse_cursor;
        ImGui_ImplWin32_UpdateMouseCursor();
    }
```

即它只参与一次三元判断，且 `UpdateMouseCursor()` 只在光标类型变化时才真正调用 `SetCursor`。**每帧赋一个 bool 的开销 <10ns，判定为完全排除。**
唯一的副作用是"菜单开关时光标形态切换"，与性能无关。

**（b）`StyleColorsDark()` 是否每帧重设 —— 排除**

`ov_gui.cpp:269` `ImGui::StyleColorsDark();` 位于 `init_gui()` 内，而 `init_gui` 由 `ov_gui.cpp:423-427` 的 `init_started.compare_exchange_strong` 保证**整个进程生命周期只执行一次**。全局 grep 确认全文件仅此一处调用 ⇒ **排除**。

**（c）字体是否每帧构造 —— 排除**

字体只在 `init_gui` 里加载（`ov_gui.cpp:251-267`，一次性失败回退 `AddFontDefault`）。DX11 侧的上传也是惰性的：

```cpp
// libs/ImGui/imgui_impl_dx11.cpp:594-601
void ImGui_ImplDX11_NewFrame()
{
    ImGui_ImplDX11_Data* bd = ImGui_ImplDX11_GetBackendData();
    IM_ASSERT(bd != nullptr && "Context or backend not initialized! Did you call ImGui_ImplDX11_Init()?");

    if (!bd->pFontSampler)
        ImGui_ImplDX11_CreateDeviceObjects();
}
```

只有 `pFontSampler` 为空（首次，或发生过 `InvalidateDeviceObjects`）才重建 ⇒ **排除**。

---

### 1.9 每帧 `new`/`delete`、字符串构造、vector 重分配

**确认存在的每帧堆操作：**

| 来源 | 位置 | 类型 | 估算 |
|---|---|---|---|
| `draw_esp` 的 `State` 深拷贝 | `ov_gui.cpp:286` | 2× `vector` malloc + N× `Ent` 拷贝 | 1–3µs |
| `ui_func` 的 `State` 深拷贝 | `ov_gui.cpp:336` | 同上 | 1–3µs |
| `refresh` 内的 `std::vector<uintptr_t> ps` | `ov_native.h:159` | 每次 1 次 malloc（`clear()` 保留容量，但 `ps` 是栈上新对象 ⇒ **每次新分配**） | 每 200ms 1 次 |
| `read_list` 的 `out.clear()` | `ov_native.h:73` | 保留容量，**无重分配**（做对了） | 0 |
| `fill_ents` 的 `out.reserve` | `ov_native.h:89` | 若容量够则无重分配 | 0 |
| `fill_ents` 的 `e.name = nm` | `ov_native.h:95` | 每实体 1 次 `std::string` 构造（SSO，无 malloc） | 每实体 <100ns |
| `logf` 的 `g_logs.push_back` | `ov_gui.cpp:111` | 3s 一次 | 忽略 |
| `logf` 的 `g_logs.erase(begin, begin+150)` | `ov_gui.cpp:110` | 溢出时搬 250 个 string | 3s 一次 |
| `draw_esp` 的 `snprintf` | `ov_gui.cpp:320-321` | 每怪一次，格式化到栈 buffer，**无堆** | 每怪 ~200ns |

**未发现的每帧堆操作**：ImGui 的 draw list 与 vertex buffer 都是池化/预分配的（`imgui_impl_dx11.cpp:132-158` 只在容量不足时增长，`VertexBufferSize = 5000` 初值），**不构成每帧 malloc**。

**判定：每帧堆分配确认存在（2 次 `State` 深拷贝），但当前规模下 2–6µs，排除为主因。注意 `ov_native.h:159` 的 `ps` 每次 refresh 都新分配一个新 vector——这是个可以直接消灭的分配。**

---

### 1.10 反向验证：`g_esp_on = false` 时还剩什么

```cpp
// ov_gui.cpp:281-288
static void draw_esp() {
    if (!g_esp_on.load()) return;
    ovn::State s;
    {
        std::lock_guard<std::mutex> lk(g_nv_mtx);
        s = g_nv;
    }
    if (!s.cam_ok) return;
```

`g_esp_on` 在函数**第一行**就被检查，所以关 ESP 会省掉：

- ✅ 省掉：1 次 `State` 深拷贝
- ✅ 省掉：全部 `w2s`、`AddRect`、`AddText`、`snprintf`

**仍然照旧执行的开销（这是判断"卡顿是 ESP 引起还是框架引起"的关键）：**

| 仍然存在的开销 | 位置 | 是否卡顿主因 |
|---|---|---|
| `install_wndproc()` 每帧 | `ov_gui.cpp:450` | 小 |
| **`tick_render_thread()` 含每 200ms 全量 `refresh`** | `ov_gui.cpp:451` | **是（周期尖峰）** |
| 每 3s `logf` 的 `fopen`/`fclose` | `ov_gui.cpp:173-178` | 中 |
| `ui_func` 的 `State` 深拷贝 | `ov_gui.cpp:336` | 小 |
| **`ui_logs` 400 行持锁重绘（日志页开时）** | `ov_gui.cpp:398-400` | **是（ms 级）** |
| `ImGui_ImplDX11_RenderDrawData` 全状态备份/恢复 | `imgui_impl_dx11.cpp:201-309` | **是（40–150µs）** |
| `OMGetRenderTargets` / `RSGetViewports` / 双重 `OMSetRenderTargets` | `ov_gui.cpp:475-487` | 小 |

**结论：卡顿的主体属于 overlay 框架层（采集时序 + UI 重绘 + DX11 状态开销），而非 ESP 绘制本身。**
⇒ **实验判据**：把 `g_esp_on` 关掉若卡顿基本不变，即可证实此结论；若明显好转，则需回头重估 1.3 的拷贝规模。

---

### 1.11 额外的每帧真正大头：ImGui DX11 后端的全状态备份

```cpp
// libs/ImGui/imgui_impl_dx11.cpp:202-226
    struct BACKUP_DX11_STATE
    {
        UINT                        ScissorRectsCount, ViewportsCount;
        D3D11_RECT                  ScissorRects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        ID3D11RasterizerState*      RS;
        ID3D11BlendState*           BlendState;
        FLOAT                       BlendFactor[4];
        UINT                        SampleMask;
        UINT                        StencilRef;
        ID3D11DepthStencilState*    DS;
        ID3D11ShaderResourceView*   PSShaderResource;
        ID3D11SamplerState*         PSSampler;
        ID3D11PixelShader*          PS;
        ID3D11VertexShader*         VS;
        ID3D11GeometryShader*       GS;
        UINT                        PSInstancesCount, VSInstancesCount, GSInstancesCount;
        ID3D11ClassInstance         *PSInstances[256], *VSInstances[256], *GSInstances[256];   // 256 is max according to PSSetShader documentation
        D3D11_PRIMITIVE_TOPOLOGY    PrimitiveTopology;
        ID3D11Buffer*               IndexBuffer, *VertexBuffer, *VSConstantBuffer;
        UINT                        IndexBufferOffset, VertexBufferStride, VertexBufferOffset;
        DXGI_FORMAT                 IndexBufferFormat;
        ID3D11InputLayout*          InputLayout;
    };
```

每帧要做：`RSGetScissorRects` / `RSGetViewports` / `RSGetState` / `OMGetBlendState` / `OMGetDepthStencilState` / `PSGetShaderResources` / `PSGetSamplers` / `PSGetShader` / `VSGetShader` / `VSGetConstantBuffers` / `GSGetShader` / `IAGetPrimitiveTopology` / `IAGetIndexBuffer` / `IAGetVertexBuffers` / `IAGetInputLayout`（约 15 次 Get），然后 `SetupRenderState` 里对等的 Set，渲染后再全部还原并在 `:294-308` 逐个 `Release()`。

**确认**：这是 ImGui 官方后端的标准做法，不是本项目写的，但代价真实存在（估算 40–150µs/帧，含 D3D11 runtime 的 COM 调用与状态去重）。**这是"ESP 关掉也依旧付"的固定税。**

---

## 二、"跑框追"根因分析

### 2.1 因果链（全部带行号）

```
[1] 游戏渲染线程每帧调用 Present
      → ov_gui.cpp:490  return g_present_orig(sc, sync, flags);

[2] 我们的 hk_present 在 Present 之前插入
      → ov_gui.cpp:451  tick_render_thread();

[3] tick_render_thread 用 200ms 节流决定是否重采样世界
      → ov_gui.cpp:165  if (now - last_nv >= 200) {
      → ov_gui.cpp:168  ovn::refresh(g_data, s);      // 读坐标 + 读 VP
      → ov_gui.cpp:171  g_nv = s;                     // 整份快照发布

[4] 相机 VP 与实体坐标在同一次 refresh 里取
      → ov_native.h:155-168  refresh(): 先 fill_ents(...)，再 refresh_camera(d, s);

[5] 绘制时无条件使用这份快照
      → ov_gui.cpp:285-286  s = g_nv;
      → ov_gui.cpp:301-302  w2s(s.vp, e.x, e.y, e.z, s.w, s.h, ...)

⇒ 框的位置 = 最多 200ms 前的世界状态
   而画面中的角色 = 当前帧世界状态
   ⇒ 框系统性滞后
```

### 2.2 量化：滞后多少帧

| 量 | 值 | 来源 |
|---|---|---|
| 快照周期 | 200ms（名义） / 207.8ms（含 15.6ms 时钟量化，平均） | `ov_gui.cpp:165` + `GetTickCount64` 分辨率 |
| 帧间隔 | 16.67ms（60fps） | 假定 |
| **快照年龄范围** | 0 → 200ms，**均值 100ms** | 均匀分布假设 |
| **均值滞后** | **100ms ≈ 6.0 帧** | |
| **最差滞后** | **200ms ≈ 12.0 帧**（+ `Present` 自身 1 帧 ⇒ **约 13 帧**） | |
| 含时钟量化最差 | 207.8 + 16.7 ≈ **224.5ms ≈ 13.5 帧** | |

**换算成"看得见的距离"**：崩坏3 角色冲刺/闪避速度量级约 8–15 m/s。

| 速度 | 均值误差 | 最差误差 |
|---|---|---|
| 8 m/s | 0.80 m | 1.60 m |
| 12 m/s | 1.20 m | 2.40 m |
| 15 m/s | 1.50 m | 3.00 m |

**屏幕上**：在 30m 距离上，1.7m 高的人形占屏约 (1.7/30)·f 像素。以 1080p / 垂直 FOV 60° 估算（f ≈ 1080/(2·tan30°) ≈ 935 px），人形框高 ≈ 53px。横向滞后 2.4m 相当于屏幕横向约 **75px**——**约 1.4 个身位的偏移**。

**这就是"跑框追"：框不是跟着人走，而是在原地停 12 帧、然后瞬移一大段，再停 12 帧。** 因为它是**阶梯函数**（零阶保持），人眼对"间歇性跳变"的感知远强于对"持续小偏差"的感知，所以主观上特别刺眼。

### 2.3 `cam_ok` 的处理方式：代码级缺陷，但经实证**排除**为本次跑框追肇因

```cpp
// ov_native.h:132-138
    void* cam = ((GetMain)(ua + RVA_GET_MAIN))(nullptr);
    s.camera = (uintptr_t)cam;
    s.cam_ok = false;
    if (!cam) { lstrcpyA(s.msg, "Camera.get_main 返回空"); return; }
    int w = ((GetInt)(ua + RVA_PIXEL_W))(cam, nullptr);
    int h = ((GetInt)(ua + RVA_PIXEL_H))(cam, nullptr);
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) { lstrcpyA(s.msg, "像素尺寸无效"); return; }
```

```cpp
// ov_gui.cpp:288
    if (!s.cam_ok) return;
```

**代码层面的缺陷确认**：`draw_esp` 对 `cam_ok == false` 的处理是**整帧清空**，没有任何"沿用上一份有效 VP"的兜底。而 `State` 在每轮采样时都是**全新构造**：

```cpp
// ov_gui.cpp:167
        ovn::State s;
```

⇒ 上一轮有效的 `vp[16]` / `w` / `h` 在 `refresh_camera` 走失败分支时被彻底丢弃（`refresh_camera` 只在成功路径的 `ov_native.h:148-152` 才写 `s.vp`/`s.w`/`s.h`）。**这是一个应当修掉的设计缺陷。**

**但必须纠正一个容易犯的误判——日志实证否定了"ESP 闪烁是由相机抖动引起"**：

对 `overlay/bh3_overlay.log` 全部 659 条快照行做交叉统计：

| 条件 | 行数 |
|---|---|
| `角色 == 0` 且 `相机 ok` | 62 |
| `角色 == 0` 且 `相机 无` | **353** |
| `角色 > 0` 且 `相机 无` | **0** |
| `角色 > 0` 且 `相机 ok` | 244 |
| **有怪（`怪 > 0`）的快照** | **22，全部 `相机 ok`** |

`cam ok` 306 次 / `cam 无` 353 次，状态翻转 39 次——但**翻转全部发生在"还没进关卡/角色数为 0"的加载期**（`怪 0 角色 0`，见 `:2`–`:17`，以及 637 条 `怪 0` 中的绝大多数）。

真正有怪可画的 22 条战斗快照：

```
L49  怪4 角色3 ok      L114 怪5 角色2 ok      L290 怪3 角色2 ok      L337 怪2 角色2 ok
L50  怪4 角色3 ok      L115 怪5 角色2 ok      L291 怪3 角色2 ok      L338 怪2 角色2 ok
                       L116 怪5 角色2 ok      L294 怪3 角色2 ok      L339 怪2 角色2 ok
                       L119 怪5 角色2 ok      L295 怪5 角色2 ok      L340 怪2 角色2 ok
                       L120 怪5 角色2 ok      L298 怪3 角色2 ok
                       L122 怪5 角色2 ok      L307 怪1 角色2 ok
                       L123 怪1 角色2 ok      L308 怪1 角色2 ok
                       L124 怪1 角色2 ok
                       L125 怪1 角色2 ok
```

**22/22 全部 `cam_ok = true`。**

**判定：`cam_ok` 抖动 → 框全灭 这条因果链在当前运行日志中【不成立，排除】。** 它发生在加载期（此时本来也没有框要画）。**2.1 的 200ms 阶梯滞后才是跑框追的唯一实证根因。**

（仍然建议保留 O5 作为健壮性加固：在过场动画、相机切换、切场景等未采样到的场景下，这条路径一旦命中就会整帧灭框，而修它只要 20 行。）


### 2.4 距离计算用 `s.avatars[0]`，不是当前操控角色

```cpp
// ov_gui.cpp:309-313
        float dist = 0;
        if (!s.avatars.empty()) {
            float dx = e.x - s.avatars[0].x, dy = e.y - s.avatars[0].y, dz = e.z - s.avatars[0].z;
            dist = sqrtf(dx * dx + dy * dy + dz * dz);
        }
```

日志实证 `AvatarManager+0x50` 的 List size = 3，内容为 `MonoFukaNL / MonoFuka / MonoSakuraYS_BattleY`（见 `docs/逆向方法论报告.md:245`）——**队伍里有 3 个角色对象**。

`avatars[0]` 取的是**列表顺序第 0 个**，而列表顺序 ≠ 当前操控角色。**确认**：切换角色后距离全部按第 0 个角色计算。

**后果**：
1. 距离数字错误（最大可差一个场景的尺度）。
2. 更关键的是 `ov_gui.cpp:314` `if (show_dist && dist > maxd) continue;` —— **距离过滤会错误地剔除/保留方框**。若你切到了远离 `avatars[0]` 的角色，近处的怪会被判成"太远"而不画。
3. **正确做法**：用相机世界坐标（VP 的逆矩阵第 4 列 / `get_transform`）或 `MainCamera` 位置作为测距原点，或直接取"当前 avatar"（通常可由 `AvatarManager` 的 current 字段或 `MonoAvatar` 的 isLocal 标志确定）。

### 2.5 两点投影、固定 `g_esp_h`、`bh > s.h` 过滤

```cpp
// ov_gui.cpp:300-307
        float sx = 0, sy = 0, hx = 0, hy = 0;
        if (!ovn::w2s(s.vp, e.x, e.y, e.z, s.w, s.h, sx, sy)) continue;
        if (!ovn::w2s(s.vp, e.x, e.y + g_esp_h, e.z, s.w, s.h, hx, hy)) continue;
        float bh = sy - hy;
        if (bh < 4.0f || bh > s.h) continue;
        float bw = bh * 0.45f;
        ImVec2 a(sx - bw * 0.5f, hy), b(sx + bw * 0.5f, sy);
```

`g_esp_h` 定义：`ov_gui.cpp:91` `static float g_esp_h = 1.7f;`（可通过 UI 滑杆 1.0–2.4 调，`ov_gui.cpp:356`）。

**（a）两点投影在高俯仰角时失真 —— 确认**
第二点取 `e.y + 1.7`，即**世界 Y 轴方向**加 1.7 米。该偏移的**屏幕投影长度**正比于相机右/上向量在 Y 轴上的分量。相机俯角 θ 越大，世界 Y 在屏幕竖直方向的分量越小：

```
bh ≈ (1.7 / dist) · f · cos(θ_pitch) · (其他项)
```

| 相机俯角 | `cos θ` | 框高相对水平视角 |
|---|---|---|
| 0°（平视） | 1.00 | 100% |
| 30° | 0.87 | 87% |
| 45° | 0.71 | 71% |
| 60° | 0.50 | **50%** |
| 75° | 0.26 | **26%** |

崩坏3 的战斗相机默认有一定俯角，玩家也常俯视。**因此框的"高矮"会随视角变化而剧烈变化**，且在俯视角下框会明显变矮——**看起来像"框在往上缩、追不上脚"**。这是"跑框追"的**第二种形态**（垂直方向的滞后/失真），与 2.1 的时间滞后叠加。

**（b）`bh > s.h` 的过滤 —— 确认不合理**
`bh` 是屏幕上框的像素高，`s.h` 是整屏像素高。当角色贴着相机（近距离）时，`bh` 会超过屏高 ⇒ **方框被整帧丢弃**，表现为"贴脸时框突然消失"。这个过滤本意是"剔除明显异常的投影"，但阈值取了整屏高，过于宽松的剔除反而不合直觉。合理做法是钳制 `bh` 而不是丢弃。

**（c）`g_esp_h = 1.7f` 对 Boss 是错的 —— 确认**
`fill_ents`（`ov_native.h:87-101`）只读 `p+0x734/0x738/0x73C` 三个坐标，**完全没有读碰撞体尺寸/模型缩放**。而日志里出现过 `怪 5`，崩坏3 的 Boss 尺寸是普通人形的数倍。**固定 1.7m 意味着 Boss 的框只有正确高度的几分之一**，视觉上同样是"框矮了一截 / 追不上头顶"。

**（d）`bw = bh * 0.45f` 固定宽高比 —— 确认**
宽度不来自任何模型数据，纯按高度乘系数，因此横向必然与真实轮廓不符（尤其对宽体怪物）。

### 2.6 200ms → 16ms 的量化改善

| 刷新周期 | 均值滞后 | 最差滞后 | 相对当前 |
|---|---|---|---|
| 200ms（现状） | 100ms / 6.0 帧 | 200ms / 12.0 帧 | 1.00× |
| 100ms | 50ms / 3.0 帧 | 100ms / 6.0 帧 | **2× 改善** |
| 50ms | 25ms / 1.5 帧 | 50ms / 3.0 帧 | **4× 改善** |
| 33.3ms | 16.7ms / 1.0 帧 | 33.3ms / 2.0 帧 | **6× 改善** |
| **16.7ms（每帧）** | **8.3ms / 0.5 帧** | **16.7ms / 1.0 帧** | **12× 改善** |

**注意**：单纯把 `200` 改成 `16` 会让 `refresh` 每帧在渲染线程上跑 —— 按 1.4 的估算（100–600µs），这等于每帧加 0.6%–3.6%，**会把"周期性顿挫"换成"持续性开销"**，而且 60 次 `VirtualQuery`/帧的代价在低配机上可能更糟。

**正确的组合是"每帧采样坐标（廉价）+ 低频采样 VP（昂贵）"**：
- 实体坐标 `p+0x734/0x738/0x73C` 只需 4 次 `rd`（若接入 `mapped_fast` 的区段缓存，实际是 4 次比较 + 4 次 memcpy）⇒ 每帧对 8 个实体做，估算 **<5µs**。
- 相机 VP 涉及 5 次托管调用，保持 100–200ms（相机变化本来就慢，且 VP 漂移只造成小幅整体偏移，不造成"跑框追"）。

**这样能把滞后从 6 帧降到 0.5–1 帧，同时几乎不增加每帧成本。这是收益/代价比最高的方向（见第四节 O1）。**

---

## 三、坐标系与尺寸一致性核查

### 3.1 两套尺寸源

| 用途 | 尺寸来源 | 代码位置 | 日志实测值 |
|---|---|---|---|
| **ESP 投影** | 相机 `pixelWidth` / `pixelHeight` | `ov_native.h:136-137` → `ov_native.h:149-150` `s.w = (float)w; s.h = (float)h;` | **未打印**（只打印在 `s.msg`，日志未记录该字段） |
| **ESP 绘制坐标系** | ImGui `io.DisplaySize` | `imgui_impl_win32.cpp:391-392` `io.DisplaySize = ImVec2(rect.right-rect.left, rect.bottom-rect.top);` | 客户端区域尺寸 |
| **overlay RTV / swapchain** | `DXGI_SWAP_CHAIN_DESC.BufferDesc` | `ov_gui.cpp:206` `bw/bh` → `ov_gui.cpp:212` `g_sw/g_sh` | **1920×1080**（`bh3_overlay.log:359`） |

**关键事实：`g_sw`/`g_sh` 在 `draw_esp` 里从未被使用。** grep 全文件确认只有三处读取（`:212`、`:242`、`:277`），且 `:277` 只是打印。`draw_esp` 完全依赖 `s.w`/`s.h`。

### 3.2 `w2s` 与其使用点对比

```cpp
// ov_native.h:113-125
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
```

使用点（`ov_gui.cpp:301-307`）：

```cpp
        if (!ovn::w2s(s.vp, e.x, e.y, e.z, s.w, s.h, sx, sy)) continue;
        if (!ovn::w2s(s.vp, e.x, e.y + g_esp_h, e.z, s.w, s.h, hx, hy)) continue;
        ...
        if (b.x < -8 || a.x > s.w + 8 || b.y < -8 || a.y > s.h + 8) continue;
```

然后直接把 `sx/sy/hy` 交给 `ImGui::GetBackgroundDrawList()`（`ov_gui.cpp:316-322`），而该 draw list 的坐标空间由 ImGui 后端确定：

```cpp
// libs/ImGui/imgui_impl_dx11.cpp:91-92
    vp.Width = draw_data->DisplaySize.x;
    vp.Height = draw_data->DisplaySize.y;
```

```cpp
// libs/ImGui/imgui.cpp:4924-4925
    draw_data->DisplaySize = viewport->Size;
    draw_data->DisplayFramebufferScale = io.DisplayFramebufferScale;
```

而 `DisplaySize` 来自 `io.DisplaySize`（`imgui_impl_win32.cpp:392`，即 `GetClientRect`）。

**⇒ 数学结论：`sx` 的数值被当作"io.DisplaySize 像素空间"来解释。**

### 3.3 会产生比例偏差的三种情形

| 情形 | `pixelWidth/Height` | `io.DisplaySize` | 结果 | 判定 |
|---|---|---|---|---|
| **渲染缩放 < 100%**（崩坏3 PC 有画面精度/渲染倍率设置） | 如 1280×720 | 1920×1080 | 框整体缩小到 **66.7%**，并向左上角聚集 | **疑似（最可能）** |
| **DPI 缩放 ≠ 100%** 且进程非 DPI-aware | 如 1920×1080 | 虚拟化后的 1536×864 | 框整体放大 1.25× | 疑似 |
| **全屏独占 / 无边框全屏切换** | 可能滞后一帧 | 切换后立即变 | 切换瞬间框整体错位 1 帧以上 | 疑似 |
| 分辨率 = 渲染分辨率，窗口化 = 客户区 | 1920×1080 | 1920×1080 | **完全一致，无偏差** | 当前日志即此情形（`bh3_overlay.log:359`） |

**关键判据（用户可直接自查，无需改代码）**：把视角中心对准一个已知居中的目标，看 ESP 框是否与该目标重合。若框始终在屏幕中心附近正确、但越靠近屏幕边缘偏移越大 ⇒ 是**比例偏差**（3.3 第一行）；若框整体等比缩小但中心对齐 ⇒ 是**缩放偏差**；若框整体平行偏移不缩放 ⇒ 是**相机 VP 与 viewport 的 offset/origin 不一致**。

**注意 `ov_gui.cpp:307` 的剔除也用了 `s.w/s.h`**，所以一旦两套尺寸不一致，"剔除边界"和"实际可见边界"也不一致 ⇒ 屏幕边缘会出现"框该出现却没出现"。

### 3.4 `DisplayFramebufferScale`

`imgui.cpp:1310` 默认 `DisplayFramebufferScale = ImVec2(1.0f, 1.0f)`，`ov_gui.cpp` 从未改写它（grep 无命中）。因此在 100% 缩放、非 Retina 的 Windows 桌面上，**ImGui 的 1 单位 = 1 物理像素**，没有问题。这也说明**当前唯一的潜在偏差来自"相机像素尺寸 vs 客户区尺寸"这一对**，与 ImGui 内部缩放无关。

---

## 四、优化建议（按代价/收益排序）

> 只给方向，不给实现。每条标注预期收益、风险与代价。

### O1 ★★★★★ 采样分层：实体坐标每帧、相机 VP 低频
**内容**：把 `ovn::refresh` 拆成两个函数——`sample_entities()`（只读 `+0x734/0x738/0x73C` 与列表）每帧调用；`sample_camera()`（5 次托管调用 + `mul_m4`）保持 100–200ms。`State` 拆成两个独立结构（`EntSnapshot` 与 `CamSnapshot`），各自双缓冲。
**预期收益**：跑框追的滞后从**均值 6 帧 / 最差 12 帧**降到 **均值 0.5 帧 / 最差 1 帧**（约 12× 改善）；同时**消除**每 200ms 一次的 `refresh` 尖峰（因为最贵的部分降频、最便宜的部分升频）。
**风险**：低。相机与坐标不同步会带来极小的整体平移误差（<1 帧），肉眼不可见。
**代价**：中等（约 80–150 行改动 + `State` 结构拆分）。

### O2 ★★★★★ `ov::Data::rd` 接入 `mapped_fast` 区段缓存
**内容**：`ov_data.h:35-43` 的 `readable()` 改为先走与 `sdk/bh3_sdk.h:586-602` 相同的"上次区段"快速判断，未命中才 `VirtualQuery`。SDK 已有现成实现，可复用同一策略（`mtop_`/`mbot_` 缓存）。
**预期收益**：一次 `refresh` 的 **~78 次内核调用**（N = 10 时）降到**接近 0**（il2cpp 堆是一整块 `MEM_COMMIT` 保留区，缓存命中率接近 100%）。这一项直接决定 O1 是否能做到"每帧采样仍便宜"。
**风险**：中——`mapped_fast` 的缓存必须同时校验**两端**（`sdk/bh3_sdk.h:588-591` 的注释记录了 2026-11-02 20:31:54 因只校验一端导致 `mbot_ - addr` 下溢、把未映射指针判为可读而崩溃的事故）。**必须照抄该双端校验逻辑，不能简化。**
**代价**：小（约 15 行）。

### O3 ★★★★☆ `logf` 改为异步队列 + 批量落盘
**内容**：`logf` 只做 `_vsnprintf` + 入队（无锁环形缓冲或独立小锁），由 `boot_thread` 或新建的低优先级线程每 1–2 秒 `fopen` 一次、批量 `fprintf`、`fclose` 一次；`OutputDebugStringA` 仅在有调试器时启用（`IsDebuggerPresent()` 门控）。
**预期收益**：渲染线程上**彻底消除** 100–500µs 的磁盘 IO（若附加调试器则消除 0.5–2ms）；同时把"每 3 秒一次顿"变成不可感知。
**风险**：低。需保证进程退出前 flush（`DllMain` 的 `DLL_PROCESS_DETACH` 有 `g_unhook = true`，可挂 flush）。
**代价**：小（约 40 行）。

### O4 ★★★★☆ UI 关闭时彻底跳过 + 日志页改虚拟滚动
**内容**：
- `draw_ui()` 在 `!g_menu_open` 时已经提前 return（`ov_gui.cpp:406`），但 `ui_func` 的 `State` 拷贝发生在 `draw_ui` 之内，所以没问题——**真正要做的是日志页**：把 `ImGui::TextUnformatted` 的全量循环换成只绘制可视区（`GetScrollY()` + `GetWindowHeight()` 计算起止行），或把 400 行改为 `ImGuiListClipper`。
- 另外把 `logf` 里的 `erase(begin, begin+150)` 改为环形缓冲，避免一次搬 250 个 string。
**预期收益**：日志页打开时的 1–4ms/帧 降到 0.1–0.3ms（**10× 以上**）；这是当前唯一能到 ms 级的项。
**风险**：极低。
**代价**：小（约 30 行）。

### O5 ★★★☆☆ 把 `cam_ok` 从"全灭"改为"沿用上次有效 VP"（健壮性，非当前根因）
**内容**：在 `State` 之外保留一份 `last_good_vp[16]` + `last_good_w/h`。`refresh_camera` 失败时不清空，绘制端在 `cam_ok == false` 但存在上次有效 VP 时**继续用旧 VP 绘制**（可给一个 `stale` 标志用于 UI 提示）。
**预期收益**：消除"过场/切场景/相机切换瞬间整帧灭框"的可能性。**注意：本次日志中该路径从未在有怪的战斗快照里命中（0/22），因此预期收益低于 O1/O2/O3/O4，降级为加固项。**
**风险**：低。旧 VP 会让框短暂偏移（相机已动、矩阵未更新），但远好于"全部消失"。
**代价**：小（约 20 行）。

### O6 ★★★☆☆ 去掉每帧 `install_wndproc()`
**内容**：`ov_gui.cpp:450` 改为仅在窗口句柄或前台窗口变化时调用（例如缓存 `GetForegroundWindow()` 结果，变化时才复检）。或退化为"每 60 帧一次"。
**预期收益**：省 3–10µs/帧（<0.1%），主要收益是**消除一个语义风险**（每帧改窗口过程的调用本身在反作弊/游戏自身重挂 WndProc 的场景下可能引发额外窗口消息）。
**风险**：低。日志证明自愈逻辑从未触发过，说明它本就是死代码路径。
**代价**：极小（3 行）。

### O7 ★★★☆☆ 消除每帧两次 `State` 深拷贝
**内容**：`draw_esp` 与 `ui_func` 各自拷一份完整 `State`（`ov_gui.cpp:286`、`:336`）。改为：`tick_render_thread` 用独立的"工作区"对象构造快照，再在锁内**交换指针**（`std::shared_ptr<const State>` 或双缓冲 + `atomic<int> index`），绘制端拿只读引用。
**预期收益**：**根本消除**每帧 2 次 vector 分配 + N×64B memcpy（当前 2–6µs）。在 1024 实体的极端场景下才明显（20–40µs）。
**风险**：低，但比前几项复杂（引入所有权/生命周期管理）。
**代价**：中（约 60 行，需小心 `shared_ptr` 引用计数原子操作本身也有成本——若用双缓冲 + seqlock 更省）。

### O8 ★★★☆☆ 让 ESP 只画"当前操控角色附近的怪"并使用真实尺寸
**内容**：
- 距离改用**相机世界位置**（从 VP 逆矩阵或 `MainCamera` 位置取，避免 `avatars[0]` 的语义错误，见 2.4）。
- 实体高度改用真实碰撞体/模型包围盒（若 `+0x734` 附近能读到 `+0x740` 之类的高度字段），而不是全局 `g_esp_h = 1.7f`。
- 框宽由高度推出改为"高度 × 实体自身宽高比"。
**预期收益**：修正**语义错误**（不是性能收益）。距离过滤不再误杀方框；Boss 框高度正确；俯视角下框不再剧烈变矮。
**风险**：中——需要新的逆变换/字段偏移验证，属于"未确认领域"。
**代价**：中（约 50 行 + 逆向验证）。

### O9 ★★☆☆☆ `w2s` 改为"钳制"而非"丢弃"
**内容**：`ov_gui.cpp:304` `if (bh < 4.0f || bh > s.h) continue;` 改为 `bh = clamp(bh, 4.0f, s.h * 4.0f)`（或直接钳制第二点），避免贴脸时框消失。
**预期收益**：修正"贴脸瞬间框不见"的观感问题。
**风险**：极低。
**代价**：极小（2 行）。

### O10 ★★☆☆☆ 减少 ImGui DX11 后端的状态往返
**内容**：本项目的 overlay 是"完整的 Present 钩子 + 独立 RTV"，每帧要对整个管线做一次备份/恢复。可考虑：
- 在 `RenderDrawData` 之后不再调用 `OMGetRenderTargets`/`RSGetViewports`（因为已知游戏下一帧会自己重设），但代价是**可能破坏游戏在 Present 后依赖的状态**——风险较高，不建议。
- 更安全的方向是**只在有可见 draw 内容时才渲染**（`draw_esp` 无怪且菜单关闭时，跳过整个 `NewFrame/Render/RenderDrawData` 序列）。
**预期收益**：省 40–150µs/帧（约 0.3%–1%），且只在"空场景 + 菜单关"时生效。
**风险**：低（跳过时不能调用 `NewFrame`，否则 ImGui 内部状态不匹配；需成对跳过）。
**代价**：小（约 10 行）。

### O11 ★☆☆☆☆ 用 QueryPerformanceCounter 替代 GetTickCount64 做节流
**内容**：`ov_gui.cpp:164` 的 `GetTickCount64()` 分辨率 15.6ms，导致 200ms 节流实际在 200–215.6ms 抖动。
**预期收益**：节流精度提升，减少约 8ms 的额外抖动。仅在与 O1 无关（O1 做了之后不再依赖 200ms 节流）时才有独立价值。
**风险**：无。
**代价**：极小（3 行）。

### 优先级建议（如果只能做三件事）
1. **O1 + O2**（分层采样 + 区段缓存）——直接消灭"跑框追"（滞后 12 帧 → 1 帧），且不引入新的每帧开销。**这是本次调研唯一被数据证实的根因修复。**
2. **O3 + O4**（日志异步化 + 日志页虚拟滚动）——消灭唯一到 ms 级的卡顿项与每 3 秒一次的顿。
3. **O7 + O10**（消除每帧 `State` 深拷贝 + 空场景跳过整个 ImGui 渲染序列）——降固定税，为将来把怪物上限提到三位数留余量。

（O5 降级为可选加固项，理由见其条目：本次日志中该路径 0/22 命中。）

---

## 五、尚未确认的疑点

1. **`refresh_camera` 里 5 次托管调用的真实代价未知。**
   尤其：渲染线程是否已向 IL2CPP/GC 注册（`il2cpp_thread_attach`）？若未注册，每次跨入托管方法可能触发线程状态切换。**这是本次最大的未量化项**，直接决定 O1 的收益上限。验证方式：在 `refresh_camera` 前后各取一次 `QueryPerformanceCounter` 并写入日志（属于改代码，超出本次只读范围）。

2. **`Camera.get_main` 为何在加载期周期性返回空 / 像素尺寸无效。**
   日志证实 `cam ok` ↔ `cam 无` 翻转 39 次，但**全部落在"实体列表为空"的加载期**（`角色 > 0 且相机 无` = 0 条），进入战斗后从未复现。原因未定位：是关卡加载、cinematic 相机切换、`get_main` 内部有 `isActiveAndEnabled` 判断，还是像素尺寸在窗口失焦/最小化时为 0？需要针对 `Camera.get_main`（`RVA_GET_MAIN = 0x121E6560`，`ov_native.h:24`）的汇编做进一步分析。**降级为低优先级：不影响战斗中的 ESP。**

3. **`pixelWidth/pixelHeight` 与 swapchain 尺寸、客户区尺寸三者是否一致。**
   日志只打印了 swapchain 的 1920×1080（`bh3_overlay.log:359`），**从未打印 `s.w`/`s.h`**（`s.msg` 里有，但 `logf` 只输出 `cam_ok` 的 ok/无，不输出 `msg`）。需要一次带 `s.msg` 的日志采样才能确认第三节的比例偏差是否真实发生。**这是"框偏"是否存在的唯一硬证据缺口。**

4. **`bw = bh * 0.45f`（`ov_gui.cpp:305`）的系数是否与实际模型比例匹配。**
   0.45 的来历没有任何注释。若团队里有原始标定数据，需要核对。

5. **实体列表的实际上限。**
   `read_list` cap = 1024（`ov_native.h:72`），`draw_esp` cap = 400（`ov_gui.cpp:324`）。日志精确统计的最大值是**怪物 5 个、角色 5 个**。若某些关卡（无尽深渊/多波次）怪物数真的接近三位数，则 1.3 的 `State` 拷贝代价需重估（可能从 2µs 升到 40µs 量级）。

6. **`g_rtv` 创建失败时会不会退化成每帧 `GetDesc` + `GetBuffer` + `logf`。**
   `ov_gui.cpp:201` 的 `&& g_rtv` 短路确实会导致这条退化路径，但本次日志中未触发。需要在 4K/多显示器/分辨率切换场景下刻意测试。**这是唯一能造成"每帧磁盘 IO"的路径，值得单独验证。**

7. **崩坏3 PC 是否存在"渲染精度/分辨率缩放"选项，当前是否开启。**
   这决定第三节 3.3 第一行（框整体缩小到 66.7%）是否成立。属于游戏设置侧信息，需要用户确认。

8. **实际帧率与单帧 GPU/CPU 占比未测。**
   本报告所有耗时均为代码结构推导的**数量级估算**，未使用 PIX / GPUView / ETW / PresentMon 等工具实测。若需要定量排序，建议下一步用 ETW 抓 `hk_present` 的时间分布（这需要运行游戏，不在本次只读范围）。

9. **`ImGui::GetDrawData()` 之外，是否存在游戏自身对 swapchain 的 backbuffer 写入与 overlay 的竞争。**
   `ov_gui.cpp:481` 在 `Present` 之前直接 `OMSetRenderTargets(1, &g_rtv, nullptr)` 写 backbuffer，而游戏的绘制命令可能仍在 GPU 队列中未完成。**理论上**这会造成 GPU 侧等待/闪烁。日志与代码不足以判断，标记为疑点。

10. **`ImGui_ImplDX11_Init` 在 `install_wndproc()` 之后调用（`ov_gui.cpp:274-275`）。**
    顺序看起来是"先挂 WndProc 再初始化 DX11 后端"。若 `Init` 失败，WndProc 已经挂上而 ImGui 上下文半初始化，`hk_wndproc`（`ov_gui.cpp:130`）里的 `ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(...)` 可能进入不完整状态。**这是健壮性问题，不是性能问题**，但值得记录。

---

*报告完成。全程未修改任何源码。*
