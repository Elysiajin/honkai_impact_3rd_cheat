# Honkai Impact 3rd — IL2CPP SDK / Overlay

崩坏3（当前 build）的 IL2CPP 元数据解密、SDK 生成器与 DX11 覆盖层。
仅用于逆向工程学习与单机研究，请勿用于在线游戏。

## 组成

```
SDK使用说明.md              解密结果、运行时地址、覆盖层用法
docs/逆向方法论报告.md       完整破解思路与同类游戏 playbook
overlay/                     DX11 Present + ImGui 覆盖层（ESP / 秒杀 / 无敌）
sdk/                         SDK 生成器、调用头文件、进程内 dumper
libs/ImGui/                  ImGui 1.90.6 源码
```

## 关键产物（未入库，需本地生成）

这些文件体积过大（103 MB / 47 MB / 33 MB），GitHub 不便托管，已加入 `.gitignore`：

- `sdk/bh3_dump.cs`：77,801 类 / 360,077 字段 / 543,532 方法
- `sdk/bh3_fields.tsv`
- `sdk/bh3_methods_named.tsv`

生成方式：

```powershell
python sdk/sdkgen.py --dump
```

依赖：

- `BH3_Data/Native/UserAssembly.dll`
- `BH3_Data/Managed/Metadata/global-metadata.dat`
- `sdk/sdk_classes.tsv`、`sdk/sdk_methods.tsv`（已入库）
- `D:\Cheat Table\CE Search\regions.csv`（仅 `--dump` 需要）

## 编译覆盖层

```powershell
cd overlay
build_overlay.bat
```

产物 `overlay/bh3_overlay.dll`（已入库，可直接注入）。

## 使用

1. 进入关卡后注入 `bh3_overlay.dll`；
2. `INSERT` 打开菜单；
3. 「ESP 绘制」默认开启，画怪物框 / 名字 / 距离；
4. `F5` 秒杀全部怪物；
5. `F6` 无敌（hook `BaseMonoAvatar.BeHit`）。

## 已验证的运行时链路

```
*(UA+0x2641588) -> MethodInfo -> klass(+8) -> +0x18 -> +0x10 -> +0x30 -> [E] = AvatarManager
*(UA+0x2642EA8) 同链 = MonsterManager
AvatarManager+0x50 / MonsterManager+0x18 = List
List: +0x10 items, +0x18 size, array +0x20 entries
BaseMonoMonster position = object + 0x734
```

相机、矩阵、DieNow、BeHit 的 RVA 见 `SDK使用说明.md` 第 5 节。

## 说明

- 所有 RVA / 全局偏移均对应当前游戏 build，更新后需要按 `docs/逆向方法论报告.md` 第 9 节重新定位。
- `FieldInfo` 没有 offset 字段，字段偏移仍需按布局或 getter 汇编补。
- 参数名在元数据中被裁掉，参数类型由 invoker thunk 反推（137,182 个运行时方法）。
