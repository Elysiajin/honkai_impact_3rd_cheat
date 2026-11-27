// 2026-11-26 按新 SDK dump 的 token 复核（未走前导字节猜测）：
//   die_now / be_hit / avatar_can_use_skill 三个符号上一版用通用 push 前导
//   匹配到了同类函数（TriggerSwitchOut / IgnoreAudioPattern / WaitSetAvatarActive），
//   已按 dump 中同名同 token 的 RVA 修正。
// UserAssembly TimeDateStamp=0x6AF563D9 SizeOfImage=0x12C2C000
namespace ovp
{

enum Sym : int
{
    SymCameraMain = 0,
    SymCameraPixelW = 1,
    SymCameraPixelH = 2,
    SymCameraW2c = 3,
    SymCameraProj = 4,
    SymDieNow = 5,
    SymBeHit = 6,
    SymUiSkillInCd = 7,
    SymUiCanUseSkill = 8,
    SymAvatarCanUseSkill = 9,
    SymCritMain = 10,
    SymCritPhys = 11,
    SymCritElem = 12,
    SymDmgFinal = 13,
    SymAnimSpeedApply = 14,
    SymAnimSpeedCancel = 15,
    SymNavmeshCalculateTriangulation = 16,
    SymUltra0 = 17,
    SymUltra1 = 18,
    SymUltra2 = 19,
    SymUltra3 = 20,
    SymUltra4 = 21,
    SymUltra5 = 22,
    SymUltra6 = 23,
    SymUltra7 = 24,
    SymUltra8 = 25,
    SymUltra9 = 26,
    SymUltra10 = 27,
    SymUltra11 = 28,
    SymRootNodePosition = 29,
    SymGetLocalAvatar = 30,
    SymTeleportTo = 31,
    SymGetSingletonImpl = 32,
    SymNavmeshSamplePosition = 33,
    SymNavmeshCalculatePath = 34,
    SymIsSpEnough = 35,
    SymGetSkillCd = 36,
    SymIsSkillHasCd = 37,
    SymAvatarMgrGlobal = 38,
    SymMonsterMgrGlobal = 39,
    SymAvatarSkillInCd = 40,
    SymGetSkillSpCost = 41,
    SymSkillGate = 42,
    SymAvatarCanUseReal = 43,
    SymAvatarXyz = 44,
    SymCount
};

struct GenEntry
{
    const char* key;
    uint32_t    rva;
    const char* prologue;
    uint32_t    prologue_len;
    bool        auto_resolved;
};

static const GenEntry kGenEntries[SymCount] =
{
    { "camera_main", 0x125B7360u, "48ff258180feef660f1f840000000000", 16, true },
    { "camera_pixel_w", 0x125B6880u, "48ff257989feef660f1f840000000000", 16, true },
    { "camera_pixel_h", 0x125B6890u, "48ff257189feef660f1f840000000000", 16, true },
    { "camera_w2c", 0x125B6990u, "488b059188feef48ffe0660f1f440000", 16, true },
    { "camera_proj", 0x125B6A40u, "488b05f987feef48ffe0660f1f440000", 16, true },
    { "die_now", 0x077F7AC0u, "41565657534881EC880000000F297424", 16, true },
    { "be_hit", 0x0586E790u, "56574883ec380f297424204889ce803d", 16, true },
    { "ui_skill_in_cd", 0x07C2B9A0u, "4883ec284889ca803d6d7af6fa00751d", 16, true },
    { "ui_can_use_skill", 0x07C2B8A0u, "56574883ec284889d64889cf803d677b", 16, true },
    { "avatar_can_use_skill", 0x05858BB0u, "564883ec704889ce803d7aa702fd000f", 16, true },
    { "crit_main", 0x04DF711Fu, "7630f30f10868c", 7, true },
    { "crit_phys", 0x04DF768Du, "7622f30f10868c", 7, true },
    { "crit_elem", 0x04DF7741u, "764ff30f10860c", 7, true },
    { "dmg_final", 0x04DF7D24u, "c786b8010000020000004885ff", 13, true },
    { "anim_speed_apply", 0x09BED960u, "4883ec28803d0176f8f80075054883c4", 16, true },
    { "anim_speed_cancel", 0x09BED9A0u, "5657534883ec204889ce803dbc75f8f8", 16, true },
    { "navmesh_calculate_triangulation", 0x1293CC50u, "4883ec28803d0be9f3ef007415488b0d", 16, true },
    { "ultra0", 0x08E0EC50u, "5657534883ec404889ce803dbb32a7f9", 16, true },
    { "ultra1", 0x0938F100u, "5657534883ec404889ce803d291f51f9", 16, true },
    { "ultra2", 0x07E15AB0u, "5657534883ec404889ce803de403a8fa", 16, true },
    { "ultra3", 0x07E164E0u, "5657534883ec404889ce803db8f9a7fa", 16, true },
    { "ultra4", 0x06889180u, "5657534883ec404889ce803df21000fc", 16, true },
    { "ultra5", 0x08E0F660u, "5657534883ec404889ce803dae28a7f9", 16, true },
    { "ultra6", 0x05769870u, "5657534883ec404889ce803d32a313fd", 16, true },
    { "ultra7", 0x06323D90u, "5657534883ec404889ce803d71dd56fc", 16, true },
    { "ultra8", 0x06F49D00u, "5657534883ec404889ce803d104296fb", 16, true },
    { "ultra9", 0x063A1900u, "5657534883ec404889ce803d5c7d4ffc", 16, true },
    { "ultra10", 0x07E16F70u, "5657534883ec404889ce803d2defa7fa", 16, true },
    { "ultra11", 0x0747E100u, "5657534883ec404889ce803df09740fb", 16, true },
    { "root_node_position", 0x09C021F0u, "5657534883ec2048", 8, true },
    { "get_local_avatar", 0x0B067310u, "56574883ec284889ce", 9, true },
    { "teleport_to", 0x09BF0350u, "5657534883ec504889d74889ce803d2e", 16, true },
    { "get_singleton_impl", 0x06668A4Fu, "488b051ad1f3fb", 7, true },
    { "navmesh_sample_position", 0x00000000u, "", 0, false },
    { "navmesh_calculate_path", 0x00000000u, "", 0, false },
    { "is_sp_enough", 0x04DAF8E0u, "56574883ec780f297424", 10, true },
    { "get_skill_cd", 0x00000000u, "", 0, false },
    { "is_skill_has_cd", 0x00000000u, "", 0, false },
    { "avatar_mgr_global", 0x00000000u, "", 0, false },
    { "monster_mgr_global", 0x00000000u, "", 0, false },
    { "avatar_skill_in_cd", 0x04DB09E0u, "5657534883ec404889d6", 10, true },
    { "get_skill_sp_cost", 0x00000000u, "", 0, false },
    { "skill_gate", 0x07C31DF0u, "5657534883ec304889ce", 9, true },
    { "avatar_can_use_real", 0x00000000u, "", 0, false },
    { "avatar_xyz", 0x09BE4620u, "5657534883EC404889D74889CE803DE708", 16, true },
};

constexpr int      kUltraCount      = 12;
constexpr uint32_t kBuildTimestamp  = 0x6AF563D9u;
constexpr uint32_t kBuildImageSize  = 0x12C2C000u;

} // namespace ovp
