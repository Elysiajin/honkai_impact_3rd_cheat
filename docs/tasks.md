# Tasks（仅无冷却）

- [ ] 1. 修改 `overlay/ov_native.h`：删除 `RVA_IS_SKILL_IN_CD 0x8013070`，新增 `RVA_UI_IS_SKILL_IN_CD 0x730B300`、`RVA_UI_CAN_USE_SKILL 0x730B200`、`RVA_AVATAR_CAN_USE_SKILL 0x4A6ADC0`。
- [ ] 2. 增加三份独立备份与开关状态。
- [ ] 3. 重写 `no_skill_cd` 同时 patch 三个入口，失败时回滚。
- [ ] 4. `cd_applied()` 改为三个子项全部生效才为 true。
- [ ] 5. 运行 `overlay/build_overlay.bat` 编译。
- [ ] 6. 用户注入验证：大招/武器技能无 CD，关闭后恢复，切换不崩溃。
