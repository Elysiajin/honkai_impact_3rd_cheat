# 崩坏3 SDK 解密与使用说明（2026-11-03）

面向当前游戏版本（UserAssembly.dll `0x1284B000`、global-metadata.dat 71 MB）。
所有表基址/常量都由真机 dump 抽样验证；游戏更新后需要重新定位。

---

## 1. 产物与代码

| 路径 | 说明 |
|---|---|
| `sdk/bh3_dump.cs` | Il2CppDumper 风格 dump：77,801 类 / 360,077 字段 / 543,532 方法 |
| `sdk/bh3_fields.tsv` | `tdi class namespace field type_tdi type_name token` |
| `sdk/bh3_methods_named.tsv` | `idx tdi class method args token rva va flags` |
| `sdk/sdk_classes.tsv` | 运行时类表：`tdi klass_va name image_va` |
| `sdk/sdk_methods.tsv` | 运行时 MethodInfo 表：`idx tdi method_va code_rva invoker_rva ...` |
| `sdk/bh3_call.h` | 进程内调用 IL2CPP 方法的 C++ 小工具（Vec3/Matrix/字符串） |
| `sdk/bh3dump.dll` + `dumper.cpp` | 进程内转储器（生成 sdk_* 表） |
| `sdk/sdkgen.py` + `invsig.py` | 离线一键生成 dump（`python sdkgen.py --dump`） |
| `overlay/` | ImGui 覆盖层源码与成品 `bh3_overlay.dll` |
| `libs/` | 覆盖层依赖（ImGui / MinHook / stb） |

`sdkgen.py` 依赖：`UserAssembly.dll`、`global-metadata.dat`、`sdk/sdk_classes.tsv`、`sdk/sdk_methods.tsv`；
`--dump` 额外读取 `D:\Cheat Table\CE Search\regions.csv`（CE 内存 dump），用于补 MethodInfo flags。

---

## 2. 元数据解密

头部在 `UserAssembly.dll RVA 0x129AA48`（0x200 字节，全部字段加密）。正文 = 文件偏移 `0x198`。

```
TYPE  base = body + (header[0x1C] ^ 0x3AC76B0F)   stride 0x52
FIELD base = body + (header[0x128] + 0xCA7D051D)  stride 0x0C
METH  base = body + (header[0xD4] ^ 0x2360832F)   stride 0x1E
```

### 2.1 类（typeDefinitions, 0x52）

```
+00 byrefTypeIndex  (+0xBB19C377, ^key)
+0C nameIndex      (+0xF2244D08, ^key)
+10 parentType     (+0x8EA6581A, ^key)
+18 flags          (^0x41E26BAF, ^key)
+20 namespaceIndex (^0x28F643CE, ^key)
+24 fieldStart     (^0x2B5AE409, ^key)
+2C token          (+0x9AA8B940, ^key)
+30 methodStart    (^0x58E32EE7, ^key)
+34 byvalTypeIndex (+0xBC20238F, ^key)
+42 u16 fieldCount (-0x4E06, ^key)
+4E u16 methodCount(-0x3277, ^key)
key = TKEY(typeIndex)
TKEY(x): (((x*0x392472F7 + 0x3D3DB4FB) ^ 0x521B13DC) * 0x2AC9F9F7 + 0x5C4A9DF5) >> 0x13
```

### 2.2 字段（FieldDefinition, 0x0C）

```
+00 nameIndex   (raw ^ K0 ^ 0x1DE059D1)
+04 token       ((raw + 0x9F8CE24C) ^ K1 ^ 0x5B7A007D)
+08 typeIndex   ((raw + 0x99DB16AB) ^ K1)
K0 = FKEY(fieldIndex), K1 = K0 ^ 0x6C0D9DD8
FKEY(x): (((x*0x6947 + 0x3D6595AB) ^ 0x211C466E) * 0x1D02A562 >> 0x11) * 0x3BB4F156 >> 0x17
```

### 2.3 方法（MethodDefinition, 0x1E）

```
+00 returnType   (+0xE63F1DC0, ^key)
+04 declaringType(^key ^0x651439A0)
+08 nameIndex    (+0xB509F32B, ^key)
+0C genericContainer (raw ^ key ^ 常量；通常 0/-1)
+10 token        (+0xF3C23881, ^key)
+14 u16 slot      (+0x595B, ^key)
+16 u16 flags     (-0x6255, ^key)       MethodAttributes
+18 u16 iflags    (^0x49B3, ^key)
+1A u16           常量（^0x0xDB1F 关系）
+1C u16 可见性/flags 字节
key = MKEY(globalMethodIndex)
MKEY(i): ((((i*0xDB9B) ^ 0x1A58D018) * 0x1339AE8B ^ 0x1FFFA93E) * 0x27F47E6A + 0xBF9490F71BB2A5A) >> 0x14
```

### 2.4 字符串

字符串索引 = `(len << 24) | src`；数据在 `body + 0x1EB360 + src`，每 8 字节：
`raw ^= ((src*0x34A3D32F255870B0 + 0xFFC51DF184FB6EAE) ^ 0x574A806E26B56AD3) + j*0x735E1E977F020CDD`

---

## 3. 类型表（字段/返回类型）

`*(UA+0x2555498)` = Il2CppMetadataRegistration；`+0x70` = types 指针 = **`UA+0x78E320`**（.rdata，静态文件里就有）。
每个 TypeIndex 16 字节：

```
+00 data(u64)  +08 bits(u64)
enum = (bits >> 16) & 0xFF
```

| enum | 含义 | data |
|---|---|---|
| 0x01–0x0D | void/bool/char/I1/U1/I2/U2/I4/U4/I8/U8/R4/R8 | 无 |
| 0x0E | string | tdi 313 |
| 0x11/0x12 | valuetype/class | `TypeDefIndex` |
| 0x1C | object | tdi 403 |
| 0x1D/0x0F/0x10/0x14 | SZARRAY/PTR/BYREF/ARRAY | 元素 `Il2CppType*` |
| 0x13/0x1E | 泛型参数 | — |
| 0x15 | GENERICINST | 泛型类索引 |

验证：`SmallXmlParser` 字段 = `StringBuilder/TextReader/Stack/AttrListImpl/char[]/IContentHandler/string/int/bool`；
`Camera.onPreCull = CameraCallback`；`Vector3.x/y/z = float`。

---

## 4. 方法参数

本 build 的 MethodDefinition **没有 parameterStart/parameterCount**，元数据里也没有 ParameterDefinition 表。
参数类型从运行时 invoker thunk 反推：

```
invoker(MethodInfo* rcx, void* this rdx, void** args r8, void* ret r9)
mov rcx, [r9]        ; 参数指针
mov edx, [rcx]       ; int
movss xmm1, [rcx]    ; float
```

已解出 137,182 个运行时方法的参数类型（int/float/double/object/byte/short），53,891 个有参数。
参数**名字**不可得；未实例化方法仍是 `(...)`。

---

## 5. 运行时地址（ESP / 战斗）

`UA` = 运行时 `UserAssembly.dll` 基址（每次启动会变）。

### 5.1 相机

| 方法 | RVA |
|---|---|
| `Camera.get_main` | `0x121E6560` |
| `Camera.get_pixelWidth / get_pixelHeight` | `0x121E5A80 / 0x121E5A90` |
| `Camera.INTERNAL_get_worldToCameraMatrix` | `0x121E5B90` |
| `Camera.INTERNAL_get_projectionMatrix` | `0x121E5C40` |
| `Camera.WorldToScreenPoint` | `0x121E6240` |
| `Component.get_transform` | `0x1217ED00` |

矩阵是列主序，VP = P * V；屏幕坐标：
`sx=(ndc.x*0.5+0.5)*W`，`sy=(0.5-ndc.y*0.5)*H`。

### 5.2 实体列表（已用战斗 dump 验证）

```
AvatarManager  = *(UA+0x2641588) -> MethodInfo -> klass -> +0x18 -> +0x10 -> +0x30 -> [E]
MonsterManager = *(UA+0x2642EA8) 同链
```

- `AvatarManager +0x50` = `List<BaseMonoAvatar>`
- `MonsterManager +0x18` = `List<BaseMonoMonster>`
- `List`：`+0x10` items 数组、`+0x18` size（`+0x1C` version）、数组 `+0x20` 起元素指针
- 对象：`+0x00` klass；`klass+0x20` 类名字符串
- 怪物坐标：对象 `+0x734`（float x/y/z）

实测：3 个角色（MonoFukaNL/MonoFuka/MonoSakuraYS_BattleY）、3 个怪物（Mono_DG_030/020_BattleY）。
管理员方法也可直接调：`get_AvatarManager 0x43BE010`、`get_MonsterManager 0xA0AE480`、
`GetOnlyOnStageMonsters 0x7A792A0`、`GetAllAvatars 0x43E24A0`。

### 5.3 战斗

| 方法 | RVA |
|---|---|
| `BaseMonoMonster.DieNow(bool)` | `0x51E5960` |
| `BaseMonoMonster.KillDead(bool)` | `0x51E62A0` |
| `BaseMonoMonster.SetDied(bool)` | `0x51E55D0` |
| `BaseMonoAvatar.SetDied(bool)` | `0x4A63810` |
| `BaseMonoAvatar.Revive(...)` | `0x4A63980` |
| `BaseMonoAvatar.BeHit(...)` | `0x4A73910` |

无敌 = 12 字节 patch `BeHit` 入口为 `mov rax, stub; jmp rax`，关闭时恢复原 12 字节。

---

## 6. 覆盖层使用

产物：`overlay/bh3_overlay.dll`。

1. 进关卡后注入 DLL；
2. `INSERT` 打开菜单；
3. 「游戏内API」页：查看管理器指针、怪物/角色数量、坐标与屏幕坐标；
4. ESP 默认开启，直接画怪物框和名字/距离；
5. `F5` 秒杀全部怪物（对每个怪物调 `DieNow(true)`）；
6. `F6` 无敌开关（hook `BaseMonoAvatar.BeHit`）。

已知限制：

- 角色坐标目前读 `+0x734`，dump 里在出生点为 0；怪物坐标已验证正常；
- 秒杀是托管方法，覆盖层从工作线程调用，当前版本实测可用；
- 参数名、字段偏移、静态字段 VA 仍未解出；
- 所有 RVA/全局偏移都是**当前 build 专用**，游戏更新需按第 2、3 节重新跑 `sdkgen.py` 并核对 5.2 的链。

---

## 7. 重新生成 SDK

```powershell
python sdk/sdkgen.py --dump
```

输出会覆盖 `sdk/bh3_dump.cs`、`sdk/bh3_fields.tsv`、`sdk/bh3_methods_named.tsv`。
如果只想用内存里的 MethodInfo/参数签名，去掉 `--dump` 即可（更快，但 flags/args 会少一部分）。
