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
    0x06000264u,   // camera_main  rva=0x125B7360  token=0x06000264
    0x0600020Au,   // camera_pixel_w  rva=0x125B6880  token=0x0600020A
    0x0600020Bu,   // camera_pixel_h  rva=0x125B6890  token=0x0600020B
    0x06000212u,   // camera_w2c  rva=0x125B6990  token=0x06000212
    0x06000218u,   // camera_proj  rva=0x125B6A40  token=0x06000218
    0x0600C4EEu,   // die_now  rva=0x091CC400  token=0x0600C4EE
    0x0600A5A6u,   // be_hit  rva=0x0586E790  token=0x0600A5A6
    0x0602927Au,   // ui_skill_in_cd  rva=0x00000000  token=0x0602927A
    0x06029279u,   // ui_can_use_skill  rva=0x00000000  token=0x06029279
    0x0600A581u,   // avatar_can_use_skill  rva=0x05858BB0  token=0x0600A581
    0x06003B87u,   // crit_main  rva=0x00000000  token=0x06003B87
    0x06003B89u,   // crit_phys  rva=0x00000000  token=0x06003B89
    0x06003B88u,   // crit_elem  rva=0x00000000  token=0x06003B88
    0x06003B8Au,   // dmg_final  rva=0x00000000  token=0x06003B8A
    0x0600B03Cu,   // anim_speed_apply  rva=0x09BED960  token=0x0600B03C
    0x0600B03Du,   // anim_speed_cancel  rva=0x09BED9A0  token=0x0600B03D
    0x0600002Du,   // navmesh_calculate_triangulation  rva=0x1293CC50  token=0x0600002D
    0x0600AB8Cu,   // ultra0  rva=0x0647CFE0  token=0x0600AB8C
    0x0600ABA5u,   // ultra1  rva=0x09387820  token=0x0600ABA5
    0x0600ABB4u,   // ultra2  rva=0x09388420  token=0x0600ABB4
    0x0600ABBCu,   // ultra3  rva=0x093884A0  token=0x0600ABBC
    0x0600ABC6u,   // ultra4  rva=0x09388D80  token=0x0600ABC6
    0x0600ABC9u,   // ultra5  rva=0x09389020  token=0x0600ABC9
    0x0600ABCCu,   // ultra6  rva=0x0938A2E0  token=0x0600ABCC
    0x0600ABCFu,   // ultra7  rva=0x0938AE20  token=0x0600ABCF
    0x0600ABD2u,   // ultra8  rva=0x0938AE50  token=0x0600ABD2
    0x0600ABD5u,   // ultra9  rva=0x049AEDF0  token=0x0600ABD5
    0x0600ABD8u,   // ultra10  rva=0x049AEFE0  token=0x0600ABD8
    0x0600ABDBu,   // ultra11  rva=0x08E0E650  token=0x0600ABDB
    0x0600B115u,   // root_node_position  rva=0x09C021F0  token=0x0600B115
    0x0600FB1Fu,   // get_local_avatar  rva=0x00000000  token=0x0600FB1F
    0x00000000u,   // teleport_to  rva=0x09BEB240  token=无
    0x0603320Cu,   // get_singleton_impl  rva=0x06708A20  token=0x0603320C
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

constexpr int kTokenCount = 42;

} // namespace ovp
