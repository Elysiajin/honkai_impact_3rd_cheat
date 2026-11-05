# 无冷却修复 Spec（仅此一项）

状态：已按用户指示收窄范围
日期：2026-11-05
适用构建：当前 `sdk/bh3_methods_named.tsv` 对应构建

## 1. 问题与根因

`overlay/ov_native.h` 只对 `RVA_IS_SKILL_IN_CD = 0x8013070` 写入
`xor eax,eax; ret`。该 RVA 在当前构建的 132,607 个方法中**不存在**；
同组的 `0x8009530`、`0x80152A0`、`0x8011F70` 也不存在。而
`Camera.get_main 0x121E6560`、`BaseMonoAvatar.BeHit 0x4A73910`、
`BaseMonoMonster.DieNow 0x51E5960`、`MonoFuka.AllowUseUltra 0x9BAE670`
均命中。因此旧 patch 落在了无名代码上，大招/武器技能不经过它，冷却照旧。

当前构建真实门控：

| 方法 | RVA | 处理 |
|---|---|---|
| `MonoSkillButton.IsSkillInCD()` | `0x730B300` | 返回 false |
| `MonoSkillButton.CanUseSkill(object)` | `0x730B200` | 返回 true |
| `BaseMonoAvatar.CanUseSkill(object)` | `0x4A6ADC0` | 返回 true |

## 2. 代码改动（只改 `overlay/ov_native.h`）

删除 `RVA_IS_SKILL_IN_CD = 0x8013070`，新增：

```cpp
static const uint32_t RVA_UI_IS_SKILL_IN_CD = 0x730B300;
static const uint32_t RVA_UI_CAN_USE_SKILL = 0x730B200;
static const uint32_t RVA_AVATAR_CAN_USE_SKILL = 0x4A6ADC0;
```

三份独立备份：

```cpp
static uint8_t g_cd_ui_bak[3] = {0};
static bool    g_cd_ui_on = false;
static uint8_t g_cd_uican_bak[3] = {0};
static bool    g_cd_uican_on = false;
static uint8_t g_cd_actor_bak[3] = {0};
static bool    g_cd_actor_on = false;
```

`no_skill_cd`：

```cpp
inline bool no_skill_cd(ov::Data& d, bool on) {
    bool ok = true;
    if (!patch_ret_false(d.ua + RVA_UI_IS_SKILL_IN_CD, g_cd_ui_bak, g_cd_ui_on, on)) ok = false;
    if (!patch_ret_true(d.ua + RVA_UI_CAN_USE_SKILL, g_cd_uican_bak, g_cd_uican_on, on)) ok = false;
    if (!patch_ret_true(d.ua + RVA_AVATAR_CAN_USE_SKILL, g_cd_actor_bak, g_cd_actor_on, on)) ok = false;
    if (!ok) {
        if (g_cd_ui_on == on) patch_ret_false(d.ua + RVA_UI_IS_SKILL_IN_CD, g_cd_ui_bak, g_cd_ui_on, !on);
        if (g_cd_uican_on == on) patch_ret_true(d.ua + RVA_UI_CAN_USE_SKILL, g_cd_uican_bak, g_cd_uican_on, !on);
        if (g_cd_actor_on == on) patch_ret_true(d.ua + RVA_AVATAR_CAN_USE_SKILL, g_cd_actor_bak, g_cd_actor_on, !on);
        return false;
    }
    return true;
}
```

`cd_applied()` 改为 `g_cd_ui_on && g_cd_uican_on && g_cd_actor_on`。

不修改 `infinite_sp`、无敌、秒杀、ESP、相机、任务或活动逻辑。

## 3. 验收

1. `overlay/build_overlay.bat` 编译通过。
2. 3D 关卡内开启“无冷却”：大招、武器技能可连续释放，按钮不进入 CD。
3. 关闭“无冷却”：冷却恢复正常，多次切换不崩溃。
4. 退出关卡回大厅不崩溃。

## 4. 风险

- `BaseMonoAvatar.CanUseSkill` 被无条件置真，可能允许受控状态下放技能；
  若出现异常，优先回退该项，仅保留两个 UI 入口。
- 若服务器二次校验技能释放，客户端去 CD 可能被拒绝；不在本次修复范围。
