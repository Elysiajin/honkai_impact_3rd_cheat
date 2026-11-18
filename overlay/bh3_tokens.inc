// 自动生成: python sdk/gen_tokens.py —— 请勿手改
// 用途: 每个符号的 IL2CPP methodToken(0x06xxxxxx)，与地址无关、跨构建稳定。
// 运行期读 MethodInfo+0x30 与 kTokens[sym] 比对；0 表示该符号无 token 校验。
// 下标与 bh3_profile.inc 的 enum Sym 严格同序，可直接 kTokens[SymCameraMain]。
#pragma once
#include <cstdint>

namespace ovp
{

static const uint32_t kTokens[] =
{
    0x06000264u,   // camera_main  rva=0x121E6560  token=0x06000264
    0x0600020Au,   // camera_pixel_w  rva=0x121E5A80  token=0x0600020A
    0x0600020Bu,   // camera_pixel_h  rva=0x121E5A90  token=0x0600020B
    0x06000212u,   // camera_w2c  rva=0x121E5B90  token=0x06000212
    0x06000218u,   // camera_proj  rva=0x121E5C40  token=0x06000218
    0x0600C4EEu,   // die_now  rva=0x051E5960  token=0x0600C4EE
    0x0600A5A6u,   // be_hit  rva=0x04A73910  token=0x0600A5A6
    0x0602927Au,   // ui_skill_in_cd  rva=0x0730B300  token=0x0602927A
    0x06029279u,   // ui_can_use_skill  rva=0x0730B200  token=0x06029279
    0x0600A581u,   // avatar_can_use_skill  rva=0x04A6ADC0  token=0x0600A581
    0x06003B87u,   // crit_main  rva=0x0793FBBF  token=0x06003B87
    0x06003B89u,   // crit_phys  rva=0x0793FFCD  token=0x06003B89
    0x06003B88u,   // crit_elem  rva=0x07940081  token=0x06003B88
    0x06003B8Au,   // dmg_final  rva=0x0794065E  token=0x06003B8A
    0x0600B03Cu,   // anim_speed_apply  rva=0x099646E0  token=0x0600B03C
    0x0600B03Du,   // anim_speed_cancel  rva=0x09964930  token=0x0600B03D
    0x00000000u,   // navmesh_calculate_triangulation  rva=0x1256D2A0  token=无
    0x0600AB8Cu,   // ultra0  rva=0x09BAE670  token=0x0600AB8C
    0x0600ABA5u,   // ultra1  rva=0x09DD97D0  token=0x0600ABA5
    0x0600ABB4u,   // ultra2  rva=0x0863B560  token=0x0600ABB4
    0x0600ABBCu,   // ultra3  rva=0x0863BF90  token=0x0600ABBC
    0x0600ABC6u,   // ultra4  rva=0x076C3560  token=0x0600ABC6
    0x0600ABC9u,   // ultra5  rva=0x09BAF090  token=0x0600ABC9
    0x0600ABCCu,   // ultra6  rva=0x0505FF60  token=0x0600ABCC
    0x0600ABCFu,   // ultra7  rva=0x058468F0  token=0x0600ABCF
    0x0600ABD2u,   // ultra8  rva=0x081252E0  token=0x0600ABD2
    0x0600ABD5u,   // ultra9  rva=0x06B86AF0  token=0x0600ABD5
    0x0600ABD8u,   // ultra10  rva=0x0863CA20  token=0x0600ABD8
    0x0600ABDBu,   // ultra11  rva=0x080BE120  token=0x0600ABDB
    0x00000000u,   // navmesh_sample_position  rva=0x1256D250  token=无
    0x00000000u,   // navmesh_calculate_path  rva=0x1256D180  token=无
    0x00000000u,   // is_sp_enough  rva=0x08011F70  token=无
    0x00000000u,   // get_skill_cd  rva=0x08009530  token=无
    0x00000000u,   // is_skill_has_cd  rva=0x080152A0  token=无
    0x00000000u,   // avatar_mgr_global  rva=0x02641588  token=无
    0x00000000u,   // monster_mgr_global  rva=0x02642EA8  token=无
    0x00000000u,   // avatar_skill_in_cd  rva=0x08013070  token=无
    0x00000000u,   // get_skill_sp_cost  rva=0x08008F20  token=无
};

constexpr int kTokenCount = 38;

} // namespace ovp
