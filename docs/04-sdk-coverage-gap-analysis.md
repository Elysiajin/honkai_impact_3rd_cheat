# SDK RVA 与参数缺失调研报告

日期：2026-11-06
范围：当前仓库的 IL2CPP SDK 生成链路、游戏目录 dump、CE 内存快照
状态：仅调研与记录，本次未修改生成器或 dumper 代码

---

## 1. 结论摘要

当前 `sdk/bh3_methods_named.tsv` 中的 `RVA=0` 和参数 `...` 不能直接理解为“这些方法真的没有地址”或“这些方法真的没有参数”。

已确认的核心事实：

1. `543,532` 条方法中，`406,350` 条 `RVA=0`，占 `74.78%`。
2. `406,350` 条 `RVA=0` 的方法，`args` 全部是字面量 `...`；非零 RVA 的方法没有一条是 `...`。两者完全一一对应。
3. 这 `406,350` 条的 `token` 和 `flags` 也全部是 `0`。这说明当前表把“没有在运行时 `MethodInfo` 表中匹配到”的记录统一写成了占位值，而不是从 metadata 解出了真实的 `token/flags`。
4. 剩余 `137,182` 条非零 RVA 方法中：
   - `83,291` 条参数列为空，表示零参数；
   - `53,891` 条有参数类型。
5. 运行时 `MethodInfo` 本身没有参数指针或参数计数。仅靠 `MethodInfo` 不能恢复完整参数列表。
6. metadata 的方法记录尾部仍是加密的；`parameterStart/parameterCount` 的精确解码尚未完成。
7. 现有运行时方法表存在明显覆盖缺口：同一份 CE 快照的不同扫描产物分别只有 `132,607`、`137,161`、`140,065`、`147,485` 条，说明 dumper/扫描器至少漏掉一部分方法。
8. 类地址表不能跨快照直接使用。相同 `tdi` 的 `klass_va` 在不同 dump 中可能相差 `0x40` 或更多，旧表里的绝对地址不能直接用于当前 CE 快照。

因此，当前最优先修复的不是“补几个 RVA”，而是：

- 取消假的 `0/...` 占位输出；
- 把“未解析”“无代码”“仅有 invoker”“抽象/泛型/PInvoke”区分成明确状态；
- 修复运行时方法枚举的覆盖；
- 再尝试恢复 metadata 中的参数起始与参数数量。

---

## 2. 现有数据量化

### 2.1 离线 SDK 表

数据源：`sdk/bh3_methods_named.tsv`

| 指标 | 数量 | 占比 |
|---|---:|---:|
| 方法总数 | 543,532 | 100% |
| `RVA=0` | 406,350 | 74.78% |
| `RVA!=0` | 137,182 | 25.22% |
| `args` 为 `...` | 406,350 | 74.78% |
| 零参数（`args` 为空） | 83,291 | 15.32% |
| 有参数类型 | 53,891 | 9.91% |

关键交叉验证：

| 组合 | 数量 |
|---|---:|
| `RVA=0` 且 `args=...` | 406,350 |
| `RVA=0` 且参数已知 | 0 |
| `RVA!=0` 且 `args=...` | 0 |
| `RVA!=0` 且参数已知 | 137,182 |

这说明当前 `args` 列不是独立缺失，而是跟随运行时方法匹配结果一起缺失。

另外，所有 `RVA=0` 行的 `token` 和 `flags` 都是 `0`。因此这些行无法用于判断方法是否为抽象方法、泛型方法、PInvoke 或未实例化方法。

### 2.2 运行时方法表

| 文件 | 行数 | 说明 |
|---|---:|---|
| `D:\Games\...\bh3_methods.tsv` | 137,161 | 旧版进程内 dumper 产物 |
| `sdk/sdk_methods.tsv` | 132,607 | 仓库内运行时方法表 |
| `sdk/bh3_methods_scan.tsv` | 140,065 | CE 快照扫描产物 |
| `sdk/pool_methodinfo_scan.tsv` | 147,485 | CE 快照池扫描产物 |
| `sdk/method_pointers_map.tsv` | 102,268 | 不是 MethodInfo 表，见下文 |

同一份 CE 快照得到多个不同行数，说明当前枚举逻辑没有形成稳定、可证明完整的结果集。

### 2.3 `method_pointers_map.tsv` 的误读风险

该文件字段为：

```text
rid  rva  array_base  size
```

实际统计：

- `102,268` 行全部 `size=0`；
- `rva` 全部为负数；
- 内容更像“RID 到某个数组基址”的映射，而不是“方法 RVA”表。

因此不能把它当作方法地址缺失的证据，也不应直接并入 SDK 的 RVA 列。

---

## 3. 已确认的结构与行为

### 3.1 MethodInfo 布局

当前 build 的 `MethodInfo` 为 `0x40` 字节：

| 偏移 | 含义 |
|---|---|
| `+0x00` | 泛型/附加元数据指针，不能当参数表使用 |
| `+0x08` | methodPointer |
| `+0x10` | klass |
| `+0x18` | metadata 方法索引 |
| `+0x20` | invoker |
| `+0x28` | `0x00000000ffffffff` |
| `+0x30` | MethodDef token |
| `+0x38` | flags |

已用样本验证 `+0x18` 与 metadata 方法索引一致，例如：

- `Object` 的 `MethodInfo+0x18 = 4182`，对应 token `0x06001059`；
- `ProtobufSerializerPartone` 的 `.ctor` 为索引 `148279`，对应 token `0x06000583`。

这比用“类名 + 方法名”连接可靠得多。后续 join 应以 `(tdi, methodIndex)` 或 `(tdi, token)` 为主键。

### 3.2 MethodInfo 不含参数表

已检查 `+0x00`、`+0x08`、`+0x10`、`+0x18`、`+0x20`、`+0x28`、`+0x30`、`+0x38` 全部字段，没有发现“参数数组指针”或“参数数量”字段。

`MethodInfo+0x00` 指向的对象也不是标准 `Il2CppMethodDefinition` 参数数组，而是另一种运行时/加密结构。

结论：即使把运行时方法枚举补全，参数类型仍必须从 metadata、invoker thunk 或代码分析中恢复。

### 3.3 metadata 方法记录

方法记录 stride 为 `0x1E`，前三个 dword 已能稳定解出：

```text
+0x00  returnType
+0x04  declaringType
+0x08  nameIndex
```

记录尾部 18 字节仍然加密。标准 IL2CPP 布局通常包含：

```text
+0x0C  parameterStart
+0x10  parameterCount(u16) + genericContainer(u16)
+0x14  token
+0x18  flags(u16) + iflags(u16)
+0x1C  slot(u16)
```

这与 `0x1E` 的 stride 吻合，但目前尚未验证这些字段的逐方法解密公式。

已验证的样本中，尾部 dword 与 `method_key` 的简单 `+/-/xor` 组合并不能稳定还原 token。因此不能直接把“标准 IL2CPP 布局”当成已解出的数据写入 SDK。

### 3.4 参数恢复现状

当前 `sdk/invsig.py` 从 invoker thunk 反推参数类型，存在两类风险：

1. **漏识别会被误当成零参数**
   - `invoke_sig()` 在找不到 `[r9+disp]` 模式时返回 `()`；
   - 生成器把 `()` 当成合法零参数签名；
   - 因此部分 `83,291` 条“零参数”可能是 invoker 识别失败，而不是真正无参数。

2. **类型粒度很粗**
   - 主要识别 `int/float/double/object/byte/short/ptr`；
   - 结构体、枚举、泛型实例、引用类型通常退化为 `object`；
   - 不能替代 metadata 参数类型。

因此当前“参数已恢复”的 137,182 条也需要标注置信度，不能全部视为精确签名。

---

## 4. 覆盖缺口的具体证据

### 4.1 部分类只 dump 到极少方法

| 类 | 当前 `RVA=0` 方法数 | 当前已解析方法数 | 说明 |
|---|---:|---:|---|
| `ProtobufSerializerPartone` | 1,410 | 2 | 只命中 `.ctor/.cctor` |
| `LevelDesignManager` | 3,783 | 0 | 运行时表完全没有该类方法 |
| `GDEKCJJCODH` | 4,720 | 0 | 同上 |
| `CEGKIEFDHBD` | 666 | 11 | 大量缺失 |
| `ProtobufSerializerPartactivity3` | 2,303 | 2 | 同上 |

这不是“少量边缘方法缺失”，而是整类方法表没有进入当前运行时扫描结果。

### 4.2 缺失类并非全部未加载

按 `tdi` 统计：

- 只出现 `RVA=0` 方法的类：`52,954` 个；
- 其中在 `sdk/sdk_classes.tsv` 中能找到运行时类记录的：`51,559` 个；
- 同时存在已解析方法的类：`2,571` 个。

也就是说，很多缺失方法的类其实已经有运行时 class 对象。当前表不能简单解释为“这些程序集没有加载”。

可能原因包括：

- class 已创建，但方法表尚未初始化；
- 方法只在类被首次使用时才创建 `MethodInfo`；
- dumper 扫描区域不足；
- dumper 跳过了 `methodPointer=0` 但 `invoker!=0` 的记录；
- 扫描去重或区域上限导致部分方法被丢弃。

这些原因需要分别验证，不能合并成一个“解密没完成”。

### 4.3 类表地址跨快照不一致

同一 `tdi=1` 的 `Locale`：

- `sdk/sdk_classes.tsv`：`klass_va = 0x50002ce85d0`
- 游戏目录 `bh3_classes.tsv`：`klass_va = 0x50002ce8610`

相差 `0x40`。此外，旧 `sdk_classes.tsv` 中部分类的 `+0x38` 字段读出来是乱码，而当前 CE 快照中的 `Object` 类则能读出合理的指针。

结论：

- 不能用旧 `klass_va` 直接读当前进程；
- 不能跨 dump 使用绝对 VA；
- 应使用 `tdi`、方法索引、token 或重新定位后的 VA 做连接。

---

## 5. 根因排序

### P0：生成器把“未解析”写成假数据

`sdk/sdkgen.py` 对未匹配运行时方法的记录使用默认值：

```text
rva=0, token=0, flags=0, args=...
```

这会让下游无法区分：

- 真正无代码；
- 抽象方法；
- 泛型方法；
- PInvoke；
- 未实例化方法；
- dumper 漏掉的方法；
- 解析失败。

这是当前最影响 SDK 可用性的 bug。

### P1：运行时方法枚举不完整

已确认的代码级风险：

- `bh3_sdk.h` 的 `all_methods()` 对非 pool 区域有 `128 MB` 上限，超过即跳过；
- 只保留 `methodPointer != 0` 的记录，可能丢失仅 invoker 可用的方法；
- 依赖盲扫堆，而不是优先使用 per-class / per-module 方法表；
- 多个扫描脚本各自实现范围与过滤，结果不一致。

### P2：metadata 尾部字段未解码

`parameterStart`、`parameterCount`、`token`、`flags` 很可能在方法记录尾部，但当前没有可靠解码公式。

### P3：invoker 签名推断过于脆弱

`invsig.py` 的正则/指令模式只能覆盖一部分 thunk，且会把未识别情况误判为零参数。

### P4：旧类表地址失效

跨快照的绝对地址不能复用，导致按 `klass_va` 读取方法表时出现乱码或错误结果。

---

## 6. 建议的修复方案

### 第一阶段：先让 SDK 说真话

1. 给方法输出增加状态列，例如：

```text
status = resolved | invoker_only | no_code | abstract | generic | pinvoke | unresolved
```

2. 未解析方法不再写 `rva=0`，而写 `null` 或空值，并保留 `methodIndex/tdi/name`。
3. `args` 增加来源列：

```text
args_source = metadata | invoker | none
```

4. 未识别 invoker 时输出 `unknown`，不要输出 `()`。
5. 统计口径改为“已证明有代码 / 已证明无代码 / 未知”，不要只统计 `rva=0`。

### 第二阶段：修复运行时方法枚举

1. 优先枚举 `Il2CppClass` 自带的方法表，而不是盲扫整个堆。
2. 保留 `methodPointer=0` 但 `invoker!=0` 的 MethodInfo，单独标记为 `invoker_only`。
3. 取消或显式记录 `128 MB` 区域跳过规则，输出被跳过区域清单。
4. 以 `(klass, methodIndex)` 为唯一键去重，不用类名。
5. 在同一次进程会话中多次 dump 并合并，记录每次新增方法数。
6. 对关键类验证 `MethodInfo` 数量与 metadata `mcount` 是否一致。

### 第三阶段：恢复 metadata 参数表

1. 用已知 `token/flags` 的样本批量拟合尾部 18 字节。
2. 先恢复 `parameterCount`，再恢复 `parameterStart`。
3. 用已知 `args` 的 137k 条方法做交叉验证：
   - 参数数量是否一致；
   - 基本类型是否一致；
   - 零参数方法是否一致。
4. 若 `parameterStart` 可恢复，再定位参数类型表并解析 `Il2CppType`。
5. 参数名如果确实不在 metadata 中，统一生成 `arg0/arg1/...`，不要伪造名称。

### 第四阶段：重建连接与验证

1. 所有 join 改用 `(tdi, methodIndex)` 或 `(tdi, token)`。
2. 类表按 `tdi` 重新定位，不直接使用旧 `klass_va`。
3. 每次生成输出覆盖报告：
   - 总方法数；
   - 已解析 RVA；
   - 仅 invoker；
   - 无代码；
   - 参数数量已知；
   - 参数类型已知；
   - 参数名不可得。

---

## 7. 下一次实际执行的建议顺序

建议下一次只做前两步，不要一开始就尝试破解全部 metadata：

1. 修改 `sdkgen.py` 的状态输出，消除假 `RVA=0` 与假 `...`。
2. 修改 `dumper.cpp` / `bh3_sdk.h`，补全方法枚举并输出 `methodIndex`。
3. 重新 dump，比较新表与现有四套表的差集。
4. 选取 20 个高价值类，人工核对 metadata 方法数与运行时方法数。
5. 再决定是否投入 metadata 尾部解码。

高价值验证类建议：

- `LevelDesignManager`
- `ProtobufSerializerPartone`
- `ProtobufSerializerPartactivity3`
- `GDEKCJJCODH`
- `CEGKIEFDHBD`
- `BaseMonoAvatar`
- `BaseMonoMonster`
- `Camera`
- `AvatarManager`
- `MonsterManager`

---

## 8. 已验证与未验证事项

### 已验证

- `406,350` 条 `RVA=0` 与 `args=...` 完全重合；
- 这些行的 `token/flags` 全为 `0`；
- `MethodInfo+0x18` 是 metadata 方法索引；
- `MethodInfo` 没有参数数组指针或参数计数；
- metadata 方法记录 stride 为 `0x1E`，前三个 dword 可解；
- 运行时方法表行数不一致，存在覆盖缺口；
- `ProtobufSerializerPartone` 等类只命中极少数方法；
- 类表绝对地址跨快照不一致；
- `method_pointers_map.tsv` 不是方法 RVA 表；
- `invsig.py` 存在把识别失败误判为零参数的风险。

### 尚未验证

- 406,350 条中有多少属于抽象/泛型/PInvoke/真正无代码；
- metadata 尾部 18 字节的精确解密公式；
- 参数表是否完整存在；
- 参数起始与参数数量能否批量恢复；
- 参数类型表能否与参数索引稳定对应；
- 补全 dumper 后，实际可恢复的 RVA 上限；
- 类初始化是否会显著增加运行时 MethodInfo 数量。

---

## 9. 复现统计的命令

以下命令仅用于复核本报告数据，不修改文件。

### 9.1 统计 RVA 与参数

```powershell
@'
import csv, collections
p = r'sdk\bh3_methods_named.tsv'
n = z = ell = zero_args = known_args = 0
with open(p, encoding='utf-8') as f:
    for d in csv.DictReader(f, delimiter='\t'):
        n += 1
        if int(d['rva'], 16) == 0:
            z += 1
        a = d['args'].strip()
        if a == '...':
            ell += 1
        elif a == '':
            zero_args += 1
        else:
            known_args += 1
print(dict(total=n, rva0=z, args_ellipsis=ell,
           zero_args=zero_args, known_args=known_args))
'@ | python -
```

### 9.2 统计运行时表

```powershell
@'
import csv, os
paths = [
    r'D:\Games\miHoYo Launcher\games\Honkai Impact 3rd Game\bh3_methods.tsv',
    r'sdk\sdk_methods.tsv',
    r'sdk\bh3_methods_scan.tsv',
    r'sdk\pool_methodinfo_scan.tsv',
]
for p in paths:
    with open(p, encoding='utf-8') as f:
        n = sum(1 for _ in csv.DictReader(f, delimiter='\t'))
    print(os.path.basename(p), n)
'@ | python -
```

### 9.3 统计缺失类

```powershell
@'
import csv
z = set()
nz = set()
with open(r'sdk\bh3_methods_named.tsv', encoding='utf-8') as f:
    for d in csv.DictReader(f, delimiter='\t'):
        (z if int(d['rva'], 16) == 0 else nz).add(int(d['tdi']))
rt = set()
with open(r'sdk\sdk_classes.tsv', encoding='utf-8') as f:
    for d in csv.DictReader(f, delimiter='\t'):
        rt.add(int(d['tdi']))
print('zero-only tdi:', len(z - nz))
print('zero-only tdi in runtime class table:', len((z - nz) & rt))
'@ | python -
```

---

## 10. 相关文件

| 文件 | 作用 |
|---|---|
| `sdk/sdkgen.py` | 离线 SDK 生成器，当前假 `0/...` 来源 |
| `sdk/invsig.py` | invoker thunk 参数推断 |
| `sdk/dumper.cpp` | 进程内 dumper 入口 |
| `sdk/bh3_sdk.h` | 运行时结构与方法枚举 |
| `sdk/bh3_methods_named.tsv` | 当前离线方法表 |
| `sdk/sdk_methods.tsv` | 运行时 MethodInfo 表 |
| `sdk/sdk_classes.tsv` | 旧类表，地址不能跨快照使用 |
| `D:\Games\...\bh3_methods.tsv` | 旧进程内 dump 产物 |
| `D:\Games\...\bh3_classes.tsv` | 旧进程类表 |
| `D:\Cheat Table\CE Search\regions.csv` | CE 快照区域索引 |

---

## 11. 最终判断

当前问题的性质不是“SDK 还有一小部分没解密”，而是：

1. 生成链路把未匹配方法伪装成了确定的 `RVA=0`；
2. 运行时方法枚举覆盖不足；
3. metadata 参数字段尚未解码；
4. invoker 推断不可靠且会误判零参数；
5. 旧类地址表跨快照失效。

因此，继续在现有 `bh3_methods_named.tsv` 上做局部修补不会显著提高可用性。正确的下一步是先把“未知”显式表达出来，再重建方法枚举，最后才处理 metadata 参数表。





