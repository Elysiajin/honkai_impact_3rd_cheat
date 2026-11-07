// 自动生成: python sdk/gen_profile.py —— 请勿手改
// UserAssembly TimeDateStamp=0x6AA18D63 SizeOfImage=0x1284B000
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
    SymAnimSpeedApply = 13,
    SymAnimSpeedCancel = 14,
    SymUltra0 = 15,
    SymUltra1 = 16,
    SymUltra2 = 17,
    SymUltra3 = 18,
    SymUltra4 = 19,
    SymUltra5 = 20,
    SymUltra6 = 21,
    SymUltra7 = 22,
    SymUltra8 = 23,
    SymUltra9 = 24,
    SymUltra10 = 25,
    SymUltra11 = 26,
    SymIsSpEnough = 27,
    SymGetSkillCd = 28,
    SymIsSkillHasCd = 29,
    SymAvatarMgrGlobal = 30,
    SymMonsterMgrGlobal = 31,
    SymCount
};

struct GenEntry
{
    const char* key;
    uint32_t    rva;
    const char* prologue;    // 小写十六进制, 空串表示不校验
    uint32_t    prologue_len;
    bool        auto_resolved;
};

static const GenEntry kGenEntries[SymCount] =
{
    { "camera_main", 0x121E6560u, "48ff25d19136f0660f1f840000000000", 16, true },
    { "camera_pixel_w", 0x121E5A80u, "48ff25c99a36f0660f1f840000000000", 16, true },
    { "camera_pixel_h", 0x121E5A90u, "48ff25c19a36f0660f1f840000000000", 16, true },
    { "camera_w2c", 0x121E5B90u, "488b05e19936f048ffe0660f1f440000", 16, true },
    { "camera_proj", 0x121E5C40u, "488b05499936f048ffe0660f1f440000", 16, true },
    { "die_now", 0x051E5960u, "41565657534881ec880000000f297424", 16, true },
    { "be_hit", 0x04A73910u, "4157415641554154565755534881ecf8", 16, true },
    { "ui_skill_in_cd", 0x0730B300u, "4883ec284889ca80", 8, true },
    { "ui_can_use_skill", 0x0730B200u, "56574883ec284889", 8, true },
    { "avatar_can_use_skill", 0x04A6ADC0u, "41565657534883ec", 8, true },
    { "crit_main", 0x0793FBBFu, "762df30f10869000", 8, true },
    { "crit_phys", 0x0793FFCDu, "7622f30f10869000", 8, true },
    { "crit_elem", 0x07940081u, "764ff30f1086d000", 8, true },
    { "anim_speed_apply", 0x099646E0u, "5657534883ec300f297424200f28f148", 16, true },
    { "anim_speed_cancel", 0x09964930u, "564883ec300f297424204889ce803dc9", 16, true },
    { "ultra0", 0x09BAE670u, "5657534883ec404889ce803d1a8cc7f8", 16, true },
    { "ultra1", 0x09DD97D0u, "5657534883ec404889ce803d2ac1a6f8", 16, true },
    { "ultra2", 0x0863B560u, "5657534883ec404889ce803d97f71ffa", 16, true },
    { "ultra3", 0x0863BF90u, "5657534883ec404889ce803d6bed1ffa", 16, true },
    { "ultra4", 0x076C3560u, "5657534883ec404889ce803d76d416fb", 16, true },
    { "ultra5", 0x09BAF090u, "5657534883ec404889ce803dfd81c7f8", 16, true },
    { "ultra6", 0x0505FF60u, "5657534883ec404889ce803d67847efd", 16, true },
    { "ultra7", 0x058468F0u, "5657534883ec404889ce803d111bfffc", 16, true },
    { "ultra8", 0x081252E0u, "5657534883ec404889ce803d1ed472fa", 16, true },
    { "ultra9", 0x06B86AF0u, "5657534883ec404889ce803d4e78cbfb", 16, true },
    { "ultra10", 0x0863CA20u, "5657534883ec404889ce803de0e21ffa", 16, true },
    { "ultra11", 0x080BE120u, "5657534883ec404889ce803d4a0177fa", 16, true },
    { "is_sp_enough", 0x08011F70u, "56574883ec780f29", 8, false },
    { "get_skill_cd", 0x08009530u, "5657534883ec600f", 8, false },
    { "is_skill_has_cd", 0x080152A0u, "56574883ec284889", 8, false },
    { "avatar_mgr_global", 0x02641588u, "", 0, false },
    { "monster_mgr_global", 0x02642EA8u, "", 0, false },
};

constexpr int      kUltraCount      = 12;
constexpr uint32_t kBuildTimestamp  = 0x6AA18D63u;
constexpr uint32_t kBuildImageSize  = 0x1284B000u;

} // namespace ovp
