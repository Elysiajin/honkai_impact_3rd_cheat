# 崩坏3 Overlay 二期调研：无限能量 / 技能无冷却 / ESP 卡顿与跑框追

调研对象：`overlay/bh3_overlay.dll`（`overlay/ov_gui.cpp` + `overlay/ov_native.h` + `overlay/ov_data.h` + `overlay/ui/*`）
游戏产物：`D:\Games\miHoYo Launcher\games\Honkai Impact 3rd Game\BH3_Data\Native\UserAssembly.dll`（303,122,248 字节，2026-11-01 15:01:26）
本次范围：**只调研，不写代码**。本文只给结论、证据、落点与风险，不含任何源码改动。

---

## 0. 结论摘要（先看这里）

| 需求 | 可行性 | 唯一推荐落点 | 关键障碍 |
|---|---|---|---|
| 无限能量（SP） | **可行** | patch `PLKHDLPDIGC.IsSPEnough` @ `0x8011F70` 入口恒返回 `true` | 12 个 `MonoFuka*` 派生的 `AllowUseUltra` 不经过此函数，需额外 12 处 patch |
| 大招无冷却 | **可行，但与能量同源** | 大招可用性 100% 等价于 `IsSPEnough(actor, 固定超必杀 skillId)`，能量 patch 后大招自然"无能量门槛" | "冷却"本身另需 patch `IsSkillInCD` @ `0x8013070` |
| 武器技能无冷却 | **可行** | patch `PLKHDLPDIGC.IsSkillInCD` @ `0x8013070` 入口恒返回 `false` | 只作用于"是否在 CD"判定；UI 遮罩/Timer 显示仍会走 `GetSkillCD` |
| SP/CD 数值写入（改内存） | **不可行** | — | `SafeFloat` 是加密容器，写侧 setter 在离线 DLL 里不存在独立函数 |
| ESP 绘制卡顿 | **可行** | `logf()` 同步磁盘写 + `draw_esp()` 每帧全量深拷贝 + 每帧 `install_wndproc` | 三个热点都在 `overlay/ov_gui.cpp`，互相叠加 |
| 人跑框追 | **可行，且是第一优先级** | 把实体坐标采样从 200ms 降到每帧（分层采样） | `overlay/ov_gui.cpp:189` 的 200ms 闸门是根因，不是 ESP 绘制慢 |

**最重要的三条可实现性结论：**

1. **能量闸门只有一个函数**：`BaseMonoAvatar.CanUseSkill`（`0x4A6ADC0`）和 `BaseMonoAvatar.AllowUseUltra`（`0x4A625F0`）都是"取 actor → 尾调用 `IsSPEnough`"的空壳，自身不含任何额外条件。所以普通技、武器技、大招的"能量是否够"判定**全部收敛到 `0x8011F70` 这一个函数**。
2. **冷却闸门也只有一个函数**：`MonoSkillButton.IsSkillInCD`（`0x730B300`）与 `BaseMonoAvatar` 侧的所有 CD 查询都尾调用 `PLKHDLPDIGC.IsSkillInCD`（`0x8013070`），全库对该函数的直接引用只有 **2 处**。
3. **不要试图写数值**：`SafeFloat`（`_data1/_data2 : long` + `_map` + `_random`）的**写侧（加密回写）在离线 DLL 中不存在独立函数**，两张映射表 `+0x37368` / `+0x37378` 的全部 xref 都落在两个纯校验函数内部。`actor+0x250` 的 SafeFloat 也从未被任何直接存储指令写过。**只能改判定，不能改数值。**

---

## 1. 无限能量（SP）

### 1.1 数值真值链

```
MonoSkillButton._currentEnergy : float  (UI 镜像, 由 OnSPChanged 推送)
        ↑
MECIIINHHHH.SetSP / ForceSetRemainSP / HealSP*   (托管侧, rva = 0x0)
        ↑
PLKHDLPDIGC + 0x250 : SafeFloat  (加密的 16 字节: _data1 @ +0x00, _data2 @ +0x08)
```

- `PLKHDLPDIGC + 0x250` 是**当前 SP 的唯一真值**，已被 `IsSPEnough` 的反汇编实证（`movups xmm0,[rdi+0x250]`）。
- `SafeFloat` 是加密容器（`_data1/_data2 : long` + `_map : object[,]` + `_random : System.Random`）。**不能直接写 float。**

### 1.2 SafeFloat 读取（已完整逆出，可直接调用）

**`SafeFloat.Get` @ `0x71759A0`**（纯函数、无副作用、无托管调用）：

- 约定：`rcx` = SafeFloat 实例地址；返回 `xmm0` = 明文 float。
- `_data1` @ `+0x00`、`_data2` @ `+0x08`。
- 双零快速短路：`if ([rsi] == 0 && [rsi+8] == 0) return 0.0f;`
- 取全局映射表：`[[rip-0x4c1fb53]]` → `[+0x37378]`；表头 `[+0x10]` 处是 `{长度, ?, 列数}`，数据从 `+0x20` 起。
- 逐字节用 `_data1` 的 byte0..byte3 查表，解出 4 字节 = **float 的位模式**。
- 再用明文自身作 key 解 12 字节校验区（`(rdi + rbx - 8) % 12`，`imul 0x2aaaaaab` 魔数除法），校验 `(w0 ^ w2) ^ w1 == w1'`。
- 校验通过返回明文，否则返回固定常量（NaN/0）。

**⇒ 想读任何受保护 float（含当前 SP、CD 剩余）直接 `call 0x71759A0`，`rcx` 传 SafeFloat 地址，`xmm0` 出结果。无需复现算法。**

### 1.3 写侧不存在（重要负面结论）

- `SafeFloat` 的 TSV 方法（`Get`/`Set`/`get_Value`/`set_Value`/`op_Implicit`/`GetByte`/`SetByte`）**全部 rva = 0x0**（未实例化），没有 code RVA。
- 文件里只有两个 SafeFloat 相关函数：`0x71759A0`（表 `+0x37378`）与 `0x71745B0`（表 `+0x37368`）。**两者都是"解密 + 一致性校验"，返回 bool，都不是写回。**
- 两张映射表的 xref 全库各只有 11 处，**全部在这两个函数内部**，没有第三个函数引用 ⇒ 不存在其它内联加密器。
- `_find_store.py 0x250`（16 字节 `movups/movaps` 存储）：`.text` + `il2cpp` 全库 **0 命中**。
- `_find_store.py 0x250 --movss`：108 命中，**全部是 `Vector3` 三分量写法**（`[+0x250]/[+0x254]/[+0x258]` 连写 + `addss`），与 SafeFloat 无关。
- `_qstore.py 0x250`（8 字节 REX.W store）：115 命中，全部散布在无关函数，无一个在 `PLKHDLPDIGC` 的 klass 邻域。
- `_qstore.py 0x58`（CD 字段 8 字节 store）：**0 命中**。

**⇒ 无限能量只能通过 patch 判定函数实现，不能通过写内存实现。**

### 1.4 能量闸门 `PLKHDLPDIGC.IsSPEnough` @ `0x8011F70`（0x10C 字节）

调用约定：`rcx` = actor，`rdx` = skillId，返回 `al` = bool。

```
0x8011F70: push rsi / push rdi / sub rsp,0x78 / movaps [rsp+0x60],xmm6
           mov rsi,rdx            ; skillId
           mov rdi,rcx            ; actor
0x8011F81: cmp byte [rip-0x57e0ef4],0 / je 慢路径   ; 类型初始化闸门
0x8011F8E: cmp byte [rip-0x55001f0],0 / jne 0x801205B  ; iFix 热更替换分支
0x8011F9B: movups xmm0,[rdi+0x250]        ; ← 当前 SP 的 SafeFloat
           movaps [rsp+0x50],xmm0
           ... lea rcx,[rsp+0x30] / call 0x71759A0
           movaps xmm6,xmm0               ; xmm6 = 当前 SP
           mov rcx,rdi / mov rdx,rsi / call 0x8008F20
                                          ; xmm0  = 需要的 SP (GetSkillSPCost)
0x8011FDD: ucomiss xmm6,xmm0 / jae 0x8011FE6
           xor eax,eax / jmp 0x8012031    ; 不够 ⇒ return false
0x8011FE6: 再读一次 [rdi+0x250] / 再调 0x71759A0
           call 0x8012B30(actor, skillId) ; 第二阈值（特殊技能分支）
           ucomiss xmm6,xmm0 / setae al   ; 返回 SP >= 第二阈值
0x8012031: 收尾 ret
```

**patch 建议（映射形态）：**

| 落点 | 字节 | 语义 | 评价 |
|---|---|---|---|
| `0x8011F70` 函数入口 | `B0 01 C3`（`mov al,1; ret`） | 恒真 | **首选**。入口 3 字节，序言尚未执行，不破坏栈与 xmm6 保存 |
| `0x8011F9B` 起 | `B0 01 C3 90 90` | 恒真 | 等效，但落在 `movups` 上，需 5 字节空间，无必要 |

**风险：** 入口 patch 会让 `IsSPEnough` 对所有 skillId 恒真，包括**敌方/非本地 actor 的调用**。需要确认 `0x80112B0`（内部 5 处调用）与 `0x55EE550`（4 处调用）是否服务于非玩家路径 —— 这两个函数尚未反汇编，是**唯一的开放风险**。

### 1.5 12 个 `MonoFuka*` 绕过点（必须一并处理）

`AllowUseUltra` 在 `sdk/bh3_methods_named.tsv` 中共 35 行、29 行有真实 RVA。按实现分三类：

**第 1 类 —— 转发到 `IsSPEnough`（patch 一处即生效）**

| 类 | RVA | 证据 |
|---|---|---|
| `BaseMonoAvatar` | `0x4A625F0` | `jmp 0x8011F70` @ `+0x9F` |
| `MonoFukaGM_BattleY` | `0x9BAEE30` | `jmp 0x8011F70` @ `+0x9F` |
| `MonoBattleYAvatarV2` | `0x497D050` | `jmp 0x8011F70` @ `+0xC3`；另有 `jmp BaseMonoAvatar.AllowUseUltra` @ `+0xD2`、`call 0x8014B50` @ `+0xA8` |

**第 2 类 —— 自己内联读 `[actor+0x250]` + `call 0x71759A0` + `setae`（不经 `IsSPEnough`，patch 无效）**

以下 12 个函数签名完全一致：`movups [rax+0x250]` @ `+0x8E`、`call 0x71759A0` @ `+0xBD`、`ucomiss + setae` @ `+0xC2`：

| 类 | RVA |
|---|---|
| `MonoFuka` | `0x9BAE670` |
| `MonoFuka_BattleY` | `0x9DD97D0` |
| `MonoFukaFL` | `0x863B560` |
| `MonoFukaFL_BattleY` | `0x863BF90` |
| `MonoFukaIN` | `0x76C3560` |
| `MonoFukaIN_BattleY` | `0x9BAF090` |
| `MonoFukaJW` | `0x505FF60` |
| `MonoFukaJW_BattleY` | `0x58468F0` |
| `MonoFukaND` | `0x81252E0` |
| `MonoFukaND_BattleY` | `0x6B86AF0` |
| `MonoFukaNL` | `0x863CA20` |
| `MonoFukaNL_BattleY` | `0x80BE120` |

实证（`MonoFuka.AllowUseUltra` @ `0x9BAE670`，0x1B9 字节）：

```
0x9BAE694: mov rax,[rip-0x756d01b] / mov rdi,[rax+8]
0x9BAE6AC: mov rax,[rdi+0x18] / mov rbx,[rax+0x10]      ; 本地 avatar 单例
0x9BAE6CA: mov rax,[rbx+0x30] / mov rdi,[rax]
0x9BAE6DE: mov edx,[rsi+0x18]                            ; avatarRuntimeID
0x9BAE6F4: call 0x46657D0                               ; 按 runtimeID 取 actor
0x9BAE6FE: movups xmm0,[rax+0x250]                       ; ← 内联读 SP
0x9BAE728: lea rcx,[rsp+0x20] / call 0x71759A0
0x9BAE732: ucomiss xmm0, dword ptr [rip-0x8b59bb9]      ; ← 另一个阈值常量
           setae al
```

**⇒ 这些角色的大招解锁用的是"SP >= 常量 X"而非 `IsSPEnough`，必须单独 patch 函数入口为 `B0 01 C3`，或改阈值常量（更隐蔽但需要定位常量地址）。**

**第 3 类 —— 纯转发 proxy（无需处理）**：`MonoFuka` `0x9BAED80`、`MonoBattleYAvatarV2` `0x4980B90`（`jmp` @ `+0x0` → 基类），以及 14 个 `<>iFixBaseProxy_AllowUseUltra`（iFix 热更桥接，不是真实覆写）。

**小计：无限能量的完整 patch 集 = 1（`0x8011F70`）+ 12（`MonoFuka*`）= 13 处，每处 3 字节。**

### 1.6 `MonoSkillButton.UseSkill` @ `0x7311750` 里的能量闸门

```
0x7311811: mov rcx,[rdi] / test byte [rcx+0xd5],4        ; 类型初始化
0x7311821: mov rax,[rcx] / ... / call qword ptr [rax+0x130]
                                          ; ← klass 内联 vtable +0x130 = IsSPEnough
           test al,al / je 0x73118B1      ; 能量不够 ⇒ return
```

⇒ **UI 点击链路上的能量判定也是 `IsSPEnough`，与 1.4 的 patch 同源，无需额外处理。**

`UseSkill` 的其他 return 分支（供排查用）：

- `+0x02` 距离判定：`call [rip-0x4dc1240]` 取坐标 → 三条 `subss` + 常数 → `ucomiss / ja return`（**距离不满足直接返回**）。
- `+0x97`：`mov rax,[rsi+0x80]` → `cmp byte [rax+0x11c],0 / jne return`（屏蔽位）。
- `+0x128`：`mov rcx,[rbx+0x3c8]` → `call 0x8D62CAA0` → `test al,al / je return`（**另一道独立判定，语义未定**）。
- `0x8D62CAA0` 不在任何 `.pdata` 函数内，`_dis1.py` 报 `rva not mapped` ⇒ **离线无法解析**，见 §7 开放项。

---

## 2. 大招 / 武器技能无冷却

### 2.1 CD 真值链

```
MonoSkillButton._skillCD : float @ +0x88        (UI 镜像)
        ↑ OnSkillCDTimerChanged(object) @ 0x7312D90
        │   movss [rsi+0x88],xmm0
        │   值来自 0x80152A0(IsSkillHasCD) 为真时调用的 0x8009530(GetSkillCD)
        ↑
PLKHDLPDIGC + 0x3B8 : Dictionary<skillId, skillInfo>
        │   查找 0xCA43D40 / 取值 0xCA43060
        ↓
skillInfo + 0x58 : SafeFloat   ← IsSkillInCD 读它
skillInfo + 0x18 : SafeFloat   ← GetSkillCD 读它
```

**`+0x58` 与 `+0x18` 是两个语义不同的 SafeFloat**，都必须靠读取才能拿到明文，都不能写。

### 2.2 冷却闸门 `PLKHDLPDIGC.IsSkillInCD` @ `0x8013070`（0xD2 字节）

调用约定：`rcx` = actor，`rdx` = skillId，返回 `al` = bool（**`true` = 仍在冷却中**）。

```
0x8013070: push rsi / push rdi / push rbx / sub rsp,0x40
           mov rsi,rdx / mov rdi,rcx
0x801307D: cmp byte [rip-0x57e1fec],0 / je 0x8013143
0x801308A: cmp byte [rip-0x55012e8],0 / jne 0x8013161   ; iFix 替换分支
0x8013097: mov rbx,[rdi+0x3b8] / test rbx,rbx / je 0x801318D
0x80130A7: ... mov r8,[rax+0x80] / test r8,r8 / je 0x8013192  ; MethodInfo
0x80130C6: mov rcx,rbx / mov rdx,rsi / call 0xCA43D40
0x80130D1: test eax,eax / js 0x8013139
           ; index < 0 ⇒ 字典没有这个 skillId ⇒ 0x8013139: xor eax,eax / ret (false)
0x80130FD: movups xmm0,[rax+0x58]                       ; ← CD 的 SafeFloat
           ... lea rcx,[rsp+0x20] / call 0x71759A0
           xorps xmm1,xmm1 / ucomiss xmm0,xmm1
           setae al                                      ; 返回 剩余CD >= 0
0x801313B: add rsp,0x40 / pop rbx / pop rdi / pop rsi / ret
```

**语义确认：** 剩余 CD 在冷却中恒 > 0，`setae`（`>=`）对 0 也返回 true。**所以 `true` = 正在冷却。patch 成恒 `false` = 永不在冷却。**

| 落点 | 字节 | 说明 |
|---|---|---|
| `0x8013070` 函数入口 | `31 C0 C3`（`xor eax,eax; ret`） | **首选**，3 字节，序言未执行，安全 |
| `0x8013139`（既有 false 返回路径） | 无需改 | 可作为"形态参照"，但不能直接跳过去 |

**辐射范围（全库仅 2 处直接引用）：**

| 引用点 | 说明 |
|---|---|
| `0x730B300` @ `+0x21` `jmp` | `MonoSkillButton.IsSkillInCD()` —— UI 技能按钮 |
| `0x8A68000` @ `+0x93` `jmp` | 未识别（`0x8A6xxxx` 段，疑似另一 UI 或人偶侧） |

⇒ **patch 入口即可覆盖全部"是否在 CD"的判定。UI 遮罩会立即消失，技能可连点。**

### 2.3 配套函数（只读，不需 patch，但会影响观感）

| 函数 | RVA | 作用 | 是否需处理 |
|---|---|---|---|
| `IsSkillHasCD` | `0x80152A0` | 返回"字典中存在该 skillId"，即**该技能配置了 CD**，与剩余时间无关 | 不处理。它是 `OnSkillCDTimerChanged` 的前置，返回 true 才会去取 CD |
| `GetSkillCD` | `0x8009530`（0x562 字节） | 读 `skillInfo+0x18` 解密值，再按 skillId 类型分支修正，`[klass+0x78]` 虚槽提供基础值 | 不处理。**CD 数值仍会被读出来并驱动 UI 动画进度** |
| `GetSkillSPCost` | `0x8008F20`（0x549 字节） | 返回 float，SP 消耗 | 不处理。1.4 已绕过 |

**观感风险：** patch 后"能不能用"恒真，但 `GetSkillCD` 仍返回非零，UI 的 CD 遮罩动画/Timer 文本可能继续滚动。**这是"能用但显示在转"的状态**，功能目标达成、视觉不一致。若要视觉一致，需另 patch `UpdateForCDMask`（`0x7310EE0`）与人偶侧 `0x479B120`，把遮罩进度强制为 0 —— 属于二期可选项。

### 2.4 大招 vs 武器技能的区别

| | 判定入口 | 依赖 |
|---|---|---|
| 大招可用性 | `AllowUseUltra` → `IsSPEnough(actor, 固定超必杀 skillId)` | **只有能量**，无 CD 检查 |
| 武器技能可用性 | `CanUseSkill(object)` → `IsSPEnough(actor, skillId)` | **只有能量**，无 CD 检查 |
| 能不能"点下去" | `UseSkill` → `IsSPEnough` + `actor+0x3C8` 容器判定 | 能量 + 解锁态 |
| UI 显示"在 CD" | `MonoSkillButton.IsSkillInCD` → `IsSkillInCD` | CD 剩余 >= 0 |

⇒ **"大招无冷却"在本作语境下其实是"大招无能量门槛"，因为 `AllowUseUltra` 根本不查 CD。** 用户说的"大招/武器技能无冷却"，实际需要的是：`IsSPEnough`（能量）+ `IsSkillInCD`（CD）两处 patch 的组合，缺一不可。

---

## 3. ESP 绘制卡顿

> 本章基于既有 `reports/01-esp-perf.md`（988 行）的结论，并对当前 HEAD 的**真实行号逐条复核**。

### 3.1 卡顿热点排序（按实测代价）

| # | 热点 | 位置 | 代价 | 触发频率 |
|---|---|---|---|---|
| 1 | `logf()` 同步磁盘写 | `overlay/ov_gui.cpp:103-104` | 100µs–3ms/次（取决于磁盘缓存） | 每次 `logf` |
| 2 | `draw_esp()` 每帧全量深拷贝 `g_nv` | `overlay/ov_gui.cpp:339` | 与实体数线性（`std::vector<Ent>` 含 `std::string name` 逐条分配） | **每帧** |
| 3 | `refresh()` 内每次 `rd()` 都 `VirtualQuery` | `overlay/ov_data.h`（`readable()` 无区段缓存） | 一次 refresh ≈ 18 + 6N 次内核调用（N=10 时 ≈ 78 次） | 每 200ms |
| 4 | `ImGui_ImplDX11_RenderDrawData` 全 DX11 状态备份 | 库内部 | 40–150µs/帧 | 每帧 |
| 5 | 每帧 `install_wndproc()` | `overlay/ov_gui.cpp:432` | 2× `GetWindowLongPtrW` + `GetAncestor` | 每帧 |

**已被证据排除的怀疑对象**（勿再排查）：`io.MouseDrawCursor` 每帧切换（`:439`）、`StyleColorsDark` 每帧、字体每帧构造、`g_desc_check=0` 首帧慢路径、WndProc 被游戏改回（日志 0 次）、`cam_ok` 抖动导致灭框（22/22 战斗快照均 `cam_ok=true`）。

### 3.2 日志页已优化，但锁仍贯穿绘制

`overlay/ui/Pages.cpp:316-354` 的日志页**已经使用 `ImGuiListClipper` 虚拟滚动**（`:333-335`），旧报告里"逐行 `TextUnformatted` 400 行"的问题在此页已不存在。

**但** `LogScope sc;` 在 `overlay/ui/Pages.cpp:326` 构造，持有 `g_log_mtx` 直到 `:352` 出块。整段 26 行（含 `BeginChild`/`EndChild` 的所有 ImGui 布局计算）都在锁内。⇒ **渲染线程调用 `LogPush`（`overlay/ui/State.cpp:39`）会被这张锁阻塞。**

叠加 `LogPush` 里的单条 `erase`（`overlay/ui/State.cpp:40`）：`g_logs.erase(g_logs.begin())` 是 O(n) 搬移（n=400 时搬 399 个 `std::string`），**在锁内**。

### 3.3 `logf()` 的同步磁盘 IO

```cpp
// overlay/ov_gui.cpp:92-105
static void logf(const char* fmt, ...) {
    ...
    ui::LogPush(buf);                        // :99  取 g_log_mtx
    OutputDebugStringA(buf);                 // :101 调试器存在时极慢
    OutputDebugStringA("\n");                // :102
    FILE* f = fopen("D:\\My Projects\\Honkai Impact\\overlay\\bh3_overlay.log", "a");  // :103
    if (f) { fprintf(f, "%s\n", buf); fclose(f); }                                     // :104
}
```

三个叠加成本：`LogPush` 抢锁 + 两次 `OutputDebugStringA`（**挂了调试器时每次可达毫秒级**）+ `fopen/fprintf/fclose` **每行一次完整开闭**。

调用点：`hook_one`（`:160`/`:165`/`:169`）、`tick_render_thread` 每 3000ms（`:212-217`）、按键切换（`:420`/`:423`/`:428`）等，**全部在渲染线程（Present 钩子内）**。

### 3.4 `draw_esp()` 每帧深拷贝

```cpp
// overlay/ov_gui.cpp:336-340
ovn::State s;
{
    std::lock_guard<std::mutex> lk(g_nv_mtx);
    s = g_nv;        // :339 全量拷贝
}
```

`ovn::State` 含两个 `std::vector<Ent>`，`Ent` 含 `std::string name`。拷贝 = 2 次 vector 分配 + **每个实体一次 `std::string` 堆分配**。场上 20 个实体时每帧 20+ 次 `malloc`/`memcpy`，且全程持锁（`tick_render_thread` 的 `g_nv = s;` 在 `:195` 会被它阻塞）。

### 3.5 修复方向（按性价比排序，均不改变现有架构）

| 优先级 | 措施 | 落点 | 预期收益 | 复杂度 |
|---|---|---|---|---|
| **P0** | `logf` 改为"只入内存环形队列 + 后台线程每 500ms 批量落盘" | `overlay/ov_gui.cpp:92-105` | 消除渲染线程所有磁盘 IO | 低 |
| **P0** | 删除两次 `OutputDebugStringA`，或只在 `IsDebuggerPresent()` 时执行 | `overlay/ov_gui.cpp:101-102` | 挂调试器时 1–4ms → 0 | 极低 |
| **P0** | `draw_esp` 改为持锁期间只做 `swap`/移动，或用双缓冲 `State` 指针交换 | `overlay/ov_gui.cpp:336-340` | 消除每帧 N 次 string 分配 | 中 |
| **P1** | `install_wndproc()` 从每帧改为每 60 帧（复用 `g_desc_check` 节奏） | `overlay/ov_gui.cpp:432` | 每帧 3 次 Win32 调用 → 每 60 帧 | 极低 |
| **P1** | 日志页 `LogScope` 只覆盖取快照，ImGui 绘制用快照副本 | `overlay/ui/Pages.cpp:326` | 解锁渲染线程；且日志页关掉后零成本 | 中 |
| **P1** | `LogPush` 的单条 `erase` 改环形缓冲（`head`/`count`）或 `std::deque` | `overlay/ui/State.cpp:40` | O(n) → O(1) | 低 |
| **P2** | `ov::Data::readable()` 加区段缓存（`mapped_fast` 已有实现可参考 `sdk/bh3_sdk.h:586-602`） | `overlay/ov_data.h` | 一次 refresh 78 次内核调用 → 2–4 次 | 中 |
| **P2** | 空场景（`monsters.empty() && !vis`）跳过 `ImGui::Render`/`RenderDrawData` | `overlay/ov_gui.cpp:456-465` | 省掉整个 DX11 状态备份 | 低 |
| **P3** | `GetTickCount64` → `QueryPerformanceCounter`（`update_fps` 与 `dt` 计算） | `overlay/ov_gui.cpp:393`/`:442` | 时序精度，非卡顿主因 | 极低 |

**预期效果：** P0 三项合计可消除全部 >1ms 的尖峰，把帧时间抖动压到 ImGui 自身水平（40–150µs）。

---

## 4. 人跑框追（框跟不上角色）

### 4.1 根因（已定量确认）

**根因是采样周期，不是绘制性能。**

```
overlay/ov_gui.cpp:189    if (now - last_nv >= 200) {     ← 每 200ms 才刷新一次实体坐标
overlay/ov_gui.cpp:192        ovn::refresh(g_data, s);
overlay/ov_gui.cpp:195        g_nv = s;
                          }
```

- 采样周期 200ms vs 帧周期 16.7ms ⇒ **每 12 帧才更新一次数据**。
- 均值滞后 = 200/2 = 100ms ≈ **6 帧**；最差滞后 = 200ms ≈ **12–13 帧**。
- 角色全速移动时，1 帧位移约 8–15 像素；12 帧滞后 = **96–180 像素偏差**，视觉上就是"框明显落后于角色"。
- 对比：ESP 绘制本身仅 5–20µs/帧，**完全不是瓶颈**。

### 4.2 分层采样方案（12× 改善）

把"实体坐标"与"相机矩阵"的刷新频率解耦：

| 数据 | 现状 | 建议频率 | 理由 |
|---|---|---|---|
| 实体坐标 `p+0x734/738/73C` | 200ms（与其它共用） | **每帧（16.7ms）** | 这是"跑框追"的唯一来源 |
| 实体列表 `read_list` | 200ms | 100–200ms | 入场/退场不敏感 |
| 管理器链 `mgr_from`（8 级） | 200ms | 500–1000ms | 进关卡后不变 |
| 相机 VP 矩阵 | 200ms | **每帧** | 相机与角色同步移动，200ms 滞后同样致偏差 |
| `pixelWidth/Height` | 200ms | 1000ms 或尺寸变化时 | 几乎不变 |

**代价对比：** 每帧读取 N 个实体的 3 个 float = `3N` 次读（N=10 时 30 次）。配合 §3.5 的区段缓存后，30 次读 ≈ **2–5µs**，相对当前 200ms 尖峰 100–600µs 反而更平滑。

**关键技术前提：** 必须先做 `ov::Data::readable()` 的区段缓存，否则每帧 30 次 `VirtualQuery` = 每帧 30 次内核调用，会把卡顿转移到新的位置。

### 4.3 其他导致"框偏移/灭框"的已确认缺陷

| # | 问题 | 位置 | 现象 | 修复方向 |
|---|---|---|---|---|
| 1 | 距离用 `s.avatars[0]` 而非**当前操控角色** | `overlay/ov_gui.cpp:369` | 换人后距离基准错，`max_dist` 剔除错人 | 用 `BaseMonoAvatar` 的 `avatarRuntimeID` 或本地单例定位当前角色 |
| 2 | 固定 `g_esp_h = 1.7f` | `overlay/ov_gui.cpp:351`（`cfg.esp_h`） | Boss/大型怪框顶偏低，框高偏小 | 按实体类名/`AvatarType` 分档，或读真实碰撞高度 |
| 3 | `bh > s.h` 直接 `continue` | `overlay/ov_gui.cpp:362` | **贴脸时（脚点已出屏下沿）框整个消失** | 改为钳制 `sy`/`hy` 到 `[0, s.h]` 后继续绘制 |
| 4 | `bw = bh * 0.45f` 无依据 | `overlay/ov_gui.cpp:363` | 不同体型宽高比失真 | 按实体类别配置宽高比 |
| 5 | `w2s` 对 NDC 超界直接 `return false` | `overlay/ov_native.h:111` | `nx/ny` 超出 ±20 即丢弃 ⇒ **屏幕边缘怪物闪灭** | 改为钳制到 ±20 而非丢弃 |
| 6 | `cam_ok=false` 时不保留上次有效 VP | `overlay/ov_native.h:125/128/133/135` | 任一帧相机抖动 ⇒ **整屏框瞬间全灭 1 帧** | 失败时沿用 `g_nv.vp` 上一份有效值，仅置标志位提示 |
| 7 | 两套尺寸源（相机 `pixelWidth/Height` vs ImGui `io.DisplaySize`） | `overlay/ov_native.h:139-140` vs `draw_esp` 的 `s.w/s.h` | 窗口模式/DP 缩放不一致时框位置偏移 | 统一用 `io.DisplaySize`，相机尺寸只做校验 |
| 8 | 实体上限 400 硬截断 | `overlay/ov_gui.cpp:384` | 极端场景少画 | 保持（合理保护），但改为按距离排序后取前 400 |

**优先修 3、5、6** —— 这三项是"框消失"的三大主因，与"跑框追"是两个独立问题，用户描述里可能混在一起。**2、4** 影响观感但不影响功能。**1** 影响远距离剔除逻辑。

---

## 5. 完整 patch 清单（实现时照此落地）

| # | RVA | 原字节（前 3 字节） | 建议 patch | 功能 | 风险 |
|---|---|---|---|---|---|
| 1 | `0x8011F70` | `56 57 48`（`push rsi; push rdi; ...`） | `B0 01 C3` | 无限能量（普通技/武器技/大招统一闸门） | 中：影响所有 `IsSPEnough` 调用方 |
| 2–13 | `0x9BAE670` / `0x9DD97D0` / `0x863B560` / `0x863BF90` / `0x76C3560` / `0x9BAF090` / `0x505FF60` / `0x58468F0` / `0x81252E0` / `0x6B86AF0` / `0x863CA20` / `0x80BE120` | 各 `push ...` | `B0 01 C3` | `MonoFuka*` 家族大招解锁 | 低：仅这 12 个角色 |
| 14 | `0x8013070` | `56 57 53`（`push rsi; push rdi; push rbx`） | `31 C0 C3` | 技能无冷却 | 低：全库仅 2 处引用 |

**实现要点（沿用现有 `god_mode` 模式，`overlay/ov_native.h:169-179`）：**

- 每个 patch 点备份 12 字节（现有 `g_behit_backup[12]` 的形态），`VirtualProtect(..., PAGE_EXECUTE_READWRITE, &old)` → `memcpy` → 恢复 `old` → `FlushInstructionCache`。
- **`0x8011F70` 与 `0x8013070` 都是 3 字节 patch，不跨指令边界，不破坏序言**（序言未执行，栈/XMM 保存均未发生）。
- 开关必须可逆：关闭时从备份恢复原字节。
- **必须 `FlushInstructionCache(GetCurrentProcess(), ...)`** —— 现有 `god_mode` 缺这一步（`overlay/ov_native.h:178` 之后未见），是潜在隐患。
- iFix 热更分支（`cmp byte [rip+xxx],0 / jne`）会把执行流重定向到 `[rax+0x3a5f0]+0x1a768` 跳转表。**若游戏启用了 iFix 热更，入口 patch 仍生效**（因为在入口就被截断），这是入口 patch 相对"改中间判定"的关键优势。

---

## 6. 为什么不用其它方案

| 方案 | 否决理由 |
|---|---|
| 每帧写 `actor+0x250` 的 SafeFloat | 写侧加解密在离线 DLL 中不存在独立函数；该字段从未被直接存储指令写过；`_random` 参与，无法离线复现 |
| 写 `SkillInfo+0x58` / `+0x18` | 同上，且 `_qstore.py 0x58` 全库 0 命中 |
| 直接调 `MECIIINHHHH.SetSP` | 该类全部方法 `rva = 0x0`（未实例化），没有 code RVA |
| 调 `CHJBCGNDGMP.SetCurEnergy` | 同上，`rva = 0x0`。且它是"自定义技能能量"，不是角色 SP |
| 调 `JDBOIEGGOOK.SetSkillCDTimerHandler` | 同上，`rva = 0x0`；且需先取到 handler 实例 |
| 改 `AllowUseUltra` 派生类返回值（15+ 处） | 已证明只需 12 处 `MonoFuka*` + 1 处基类闸门，覆盖更小 |
| hook `BeHit` 式 12 字节跳转 stub | 对 3 字节即可解决的问题是过度设计，且 stub 需要保存/恢复寄存器与栈，风险更大 |

---

## 7. 开放项与剩余风险（实现前需确认）

1. **`IsSPEnough` 的非玩家调用方**：`0x80112B0`（5 处内部调用）与 `0x55EE550`（4 处调用）尚未反汇编。若它们服务于敌方 AI 或技能预判逻辑，入口恒真可能产生副作用（例如敌人无限放技能）。**这是 `0x8011F70` patch 的唯一实质风险，必须先验证。**
2. **`0x8D62CAA0`（`UseSkill` 第二道判定）语义未定**：不在 `.pdata` 内，离线不可解析。可能是"技能是否解锁"，也可能是"是否在 CD"的另一路径。若后者成立，则 `0x8013070` patch 不足以让技能按钮真正可点。
3. **`IsSkillHasCD`（`0x80152A0`）返回 true 的前置**：它驱动 `OnSkillCDTimerChanged` 去取 CD 值。若某个技能"未配置 CD"导致它返回 false，则该技能本就没有 CD —— 无需处理，但需实测确认。
4. **UI 视觉不一致**：patch `IsSkillInCD` 后，`GetSkillCD`（`0x8009530`）仍返回非零值驱动遮罩动画。用户会看到"技能能用，但图标上还在转圈"。若要视觉一致，需再 patch `UpdateForCDMask`（`0x7310EE0`）与人偶侧 `0x479B120`。
5. **`0x8011F70` 第二段阈值的实际语义未知**：`call 0x8012B30` 是"特殊技能类型分支分发器"（内部与 3 个静态 skillId 常量做字符串比较）。patch 入口会一并绕过它，需确认它不含"消耗其他资源"的逻辑。
6. **`SafeFloat` 的 `_random` 字段用途未确认**：逆向只覆盖了读侧校验，未涉及 `_random` 在写侧的角色。若将来需要写值，这是必须先啃下的部分。
7. **iFix 热更代理**：`AllowUseUltra` 有 14 个 `<>iFixBaseProxy_*`，各 `BeHit` 也有同类代理。当前 12 个 `MonoFuka*` 是真实覆写（有独立 RVA 且内联读 `+0x250`），但若游戏侧更新热更脚本，RVA 可能漂移 —— 需要版本检测。

---

## 8. 交付物与复现路径

**本次新增的只读分析脚本**（均在 `out/`，不修改游戏文件）：

| 脚本 | 用途 | 用法 |
|---|---|---|
| `out/_fnfind.py` | 解析 `.pdata`（677,479 个 `RUNTIME_FUNCTION`）得到函数边界，反汇编整个函数并标注目标指令 | `python _fnfind.py 0x8011F70 [n]`、`--list`、`--near` |
| `out/_dis1.py` | 按 RVA 反汇编 N 条指令 | `python _dis1.py 0x8013070 40` |
| `out/_xref2.py` | 扫 `.text` + `il2cpp` 中所有 `E8/E9 rel32`，按 `.pdata` 归组到函数 | `python _xref2.py 0x8011F70` |
| `out/_find_store.py` | 找 `movups/movaps/movss` 的 XMM 存储点（**只搜 mod=10 会漏 disp8，见下**） | `python _find_store.py 0x250 --movss` |
| `out/_scan_xmm.py` | **修正版**：同时覆盖 disp8（mod=01）与 disp32（mod=10），`0F 10/11`/`0F 28/29`/`F3 0F 10/11` | `python _scan_xmm.py 0x58 [--list]` |
| `out/_qstore.py` | 找 8 字节 REX.W 存储 | `python _qstore.py 0x250` |
| `out/_vtable.py` | 在 `.data`/`.rdata` 里找包含方法指针的 qword | `python _vtable.py 0x8011F70` |
| `out/_klass.py` | 由 vtable 基址反推 klass | `python _klass.py --vt 0x1F867E0` |
| `out/_kfind.py` | 按类名反查 klass（**本 build 失效，见下**） | `python _kfind.py PLKHDLPDIGC` |

**三条踩坑记录（避免重复浪费）：**

1. **编码坑（最重要）**：`movups xmm0,[rax+0x58]` 用的是 **mod=01 disp8（`0F 10 40 58`）**，不是 disp32。只搜 mod=10 会得到 0 命中的假阴性。`_scan_xmm.py` 已修正：`+0x58` 实际有 3,764 处访问 / 1,504 个函数（其中 53 个调 `0x71759A0`），`+0x18` 有 12,959 处 / 5,280 个函数（39 个调 `0x71759A0`）。
2. **节表坑**：`_xref2.py` 原先只扫 `.text`，对 `0x8011F70` 得到 0 引用。**所有 IL2CPP 方法体都在 `il2cpp` 节（RVA `0x3332000..0x126721D1`）**，补上后得到 15 个引用。
3. **klass 反查失效**：本 build 的 `Il2CppClass` 内联 vtable 起点就在 `klass+0x000`，`+0x20` 是方法指针而非名字指针，与 `sdk/bh3_sdk.h` 里写的布局（`name @ +0x20`）不一致；类名字符串也不在文件明文中。⇒ `_kfind.py` 对 `PLKHDLPDIGC` 返回 `name strings: []`。**替代算法：已知方法指针所在的 `.data` 地址 − 槽偏移 = klass 基址**（`0x1F86910 − 0x130 = 0x1F867E0`，已用 5 个槽交叉验证）。

**vtable 槽位实测（`PLKHDLPDIGC`，klass 基址 `0x1F867E0`）：**

| 槽偏移 | 方法 RVA | 语义 |
|---|---|---|
| `+0x130` | `0x8011F70` | `IsSPEnough` |
| `+0x150` | `0x8013070` | `IsSkillInCD` |
| `+0x1C0` | `0x8012B30` | 特殊技能分支分发器 |
| `+0x1D0` | `0x8008F20` | `GetSkillSPCost` |
| `+0x1E0` | `0x8009530` | `GetSkillCD` |
| `+0x250` | `0x80152A0` | `IsSkillHasCD` |

---

## 9. 实施顺序建议

1. **先修 ESP 卡顿 + 跑框追**（无逆向风险，纯源码层面，`overlay/` 内即可完成）。同步做 §3.5 的 P0 三项 + §4.2 分层采样 + §4.3 的 3/5/6 三项。这是用户能立刻感知的改善。
2. **再做无冷却**（`0x8013070`，风险最低，引用只有 2 处，可独立验证）。
3. **最后做无限能量**（`0x8011F70` + 12 个 `MonoFuka*`）。**前置：先反汇编 `0x80112B0` 与 `0x55EE550` 确认非玩家调用方**（开放项 1）。若发现副作用，退路是"只 patch 12 个 `MonoFuka*` + 在 `AllowUseUltra`/`CanUseSkill` 的尾调用前插入判定"，把影响面收缩到玩家路径。

---

*本文档为只读调研产物。所有 RVA 基于 `UserAssembly.dll`（2026-11-01 15:01:26，303,122,248 字节），游戏更新后需重新校验。*
