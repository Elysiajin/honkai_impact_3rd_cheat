#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""gen_profile.py — 由 SDK 表 + UserAssembly.dll 生成 overlay/bh3_profile.inc

用法:
  python sdk/gen_profile.py [--ua PATH] [--named PATH] [--out PATH]

数据来源:
  * bh3_methods_named.tsv  离线元数据: class / method / token / rva
  * UserAssembly.dll       取 PE 指纹与函数前导字节，供运行时校验

符号分两类:
  * auto_resolved=True   用 (类名, 方法名) 查到当前 build 的 rva，更新后自动重算
  * auto_resolved=False  暂未找到符号身份，沿用上一版 rva，更新后需人工确认
"""
import argparse
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_UA = r"D:\Games\miHoYo Launcher\games\Honkai Impact 3rd Game\BH3_Data\Native\UserAssembly.dll"
DEFAULT_NAMED = os.path.join(ROOT, "sdk", "bh3_methods_named.tsv")
DEFAULT_OUT = os.path.join(ROOT, "overlay", "bh3_profile.inc")

ULTRA_CLASSES = [
    "MonoFuka", "MonoFuka_BattleY",
    "MonoFukaFL", "MonoFukaFL_BattleY",
    "MonoFukaIN", "MonoFukaIN_BattleY",
    "MonoFukaJW", "MonoFukaJW_BattleY",
    "MonoFukaND", "MonoFukaND_BattleY",
    "MonoFukaNL", "MonoFukaNL_BattleY",
]

# key, class, method, 从方法起点到实际 patch 点的偏移, 前导字节数
METHODS = [
    ("camera_main", "Camera", "get_main", 0, 16),
    ("camera_pixel_w", "Camera", "get_pixelWidth", 0, 16),
    ("camera_pixel_h", "Camera", "get_pixelHeight", 0, 16),
    ("camera_w2c", "Camera", "INTERNAL_get_worldToCameraMatrix", 0, 16),
    ("camera_proj", "Camera", "INTERNAL_get_projectionMatrix", 0, 16),
    ("die_now", "BaseMonoMonster", "DieNow", 0, 16),
    ("be_hit", "BaseMonoAvatar", "BeHit", 0, 16),
    ("ui_skill_in_cd", "MonoSkillButton", "IsSkillInCD", 0, 8),
    ("ui_can_use_skill", "MonoSkillButton", "CanUseSkill", 0, 8),
    ("avatar_can_use_skill", "BaseMonoAvatar", "CanUseSkill", 0, 8),
    ("crit_main", "MHEMGJMNDJI", "TryApplyCrit", 0x6F, 8),
    ("crit_phys", "MHEMGJMNDJI", "TryApplyPhysicCrit", 0x3D, 8),
    ("crit_elem", "MHEMGJMNDJI", "TryApplyElementCrit", 0x41, 8),
    ("dmg_final", "MHEMGJMNDJI", "ResolveAttackDataFinal", 0x53E, 13),
    ("anim_speed_apply", "BaseMonoAnimatorEntity", "ApplyOverrideAnimatorSpeed", 0, 16),
    ("anim_speed_cancel", "BaseMonoAnimatorEntity", "CancelOverrideAnimatorSpeed", 0, 16),
    ("navmesh_calculate_triangulation", "NavMesh", "CalculateTriangulation", 0, 16),
] + [("ultra%d" % i, cls, "AllowUseUltra", 0, 16) for i, cls in enumerate(ULTRA_CLASSES)]

# 尚未找到稳定身份: 沿用上一版地址，更新后需人工确认
# navmesh_sample_position: NavMesh.SamplePosition 有两个重载 (0x1256D250 / 0x1256D530)，
# (class,method) 查表不唯一，走 RAW 硬编码 —— 0x1256D250 是 (src, hit, maxDist, areaMask) 四参版
# navmesh_calculate_path: CalculatePath 同样两个重载 (0x1256D180 / 0x1256D5F0)，
# 走 RAW 硬编码 —— 0x1256D180 是 (src, dst, areaMask, path) 四参 areaMask 版
RAW = [
    ("navmesh_sample_position", 0x1256D250, 16),
    ("navmesh_calculate_path", 0x1256D180, 16),
    ("is_sp_enough", 0x8011F70, 8),
    ("get_skill_cd", 0x8009530, 8),
    ("is_skill_has_cd", 0x80152A0, 8),
    ("avatar_mgr_global", 0x2641588, 0),
    ("monster_mgr_global", 0x2642EA8, 0),
    ("avatar_skill_in_cd", 0x8013070, 8),
    ("get_skill_sp_cost", 0x8008F20, 8),
]


def pe_sections(path):
    with open(path, "rb") as f:
        h = f.read(0x1000)
    e = struct.unpack_from("<I", h, 0x3C)[0]
    coff = e + 4
    nsec = struct.unpack_from("<H", h, coff + 2)[0]
    optsz = struct.unpack_from("<H", h, coff + 16)[0]
    opt = coff + 20
    sec = opt + optsz
    out = []
    for i in range(nsec):
        o = sec + i * 40
        name = h[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, va, rawsize, raw = struct.unpack_from("<IIII", h, o + 8)
        out.append((name, va, vsize, raw, rawsize))
    return out


def pe_fingerprint(path):
    with open(path, "rb") as f:
        h = f.read(0x1000)
    e = struct.unpack_from("<I", h, 0x3C)[0]
    timestamp = struct.unpack_from("<I", h, e + 8)[0]
    image_size = struct.unpack_from("<I", h, e + 0x50)[0]
    return timestamp, image_size


def rva2off(sections, rva):
    for name, va, vsize, raw, rawsize in sections:
        if va <= rva < va + max(vsize, rawsize):
            return raw + (rva - va)
    return None


def prologue(path, sections, rva, n):
    if not n:
        return ""
    off = rva2off(sections, rva)
    if off is None:
        return ""
    with open(path, "rb") as f:
        f.seek(off)
        b = f.read(n)
    if len(b) != n:
        return ""
    return "".join("%02x" % x for x in b)


def load_named(path):
    rows = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        head = f.readline().rstrip("\n").split("\t")
        for line in f:
            d = dict(zip(head, line.rstrip("\n").split("\t")))
            try:
                rva = int(d.get("rva", "0x0"), 16)
            except ValueError:
                continue
            if rva:
                rows.setdefault((d.get("class", ""), d.get("method", "")), set()).add(rva)
    return rows


def camel(key):
    return "".join(p[:1].upper() + p[1:] for p in key.split("_"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ua", default=os.environ.get("BH3_UA", DEFAULT_UA))
    ap.add_argument("--named", default=DEFAULT_NAMED)
    ap.add_argument("--out", default=DEFAULT_OUT)
    args = ap.parse_args()

    sections = pe_sections(args.ua)
    timestamp, image_size = pe_fingerprint(args.ua)
    rows = load_named(args.named)

    entries = []
    unresolved = []

    for key, cls, method, delta, plen in METHODS:
        hits = sorted(rows.get((cls, method), set()))
        if len(hits) != 1:
            unresolved.append("%s (%s.%s): %s" % (
                key, cls, method,
                "未找到" if not hits else "有 %d 个候选 %s" % (len(hits), [hex(x) for x in hits])))
            continue
        rva = hits[0] + delta
        entries.append((key, rva, prologue(args.ua, sections, rva, plen), plen, True))

    for key, rva, plen in RAW:
        entries.append((key, rva, prologue(args.ua, sections, rva, plen), plen, False))

    if unresolved:
        print("[-] 以下符号无法从 SDK 表解析:")
        for u in unresolved:
            print("    " + u)
        print("[-] 已生成 profile，但这些符号会以缺失状态写入；请更新符号表后重跑")
        # 缺失的符号仍写 0，运行时校验会判为无效，不会误调用
        for key, cls, method, delta, plen in METHODS:
            if not any(e[0] == key for e in entries):
                entries.append((key, 0, "", 0, False))

    # 保持稳定顺序：METHODS -> ULTRA -> RAW 的声明顺序由上面的 entries 构造顺序保证，
    # 但“缺失补 0”的项会追加到尾部。为了让枚举顺序始终可预测，按声明顺序重排。
    order = [m[0] for m in METHODS] + [r[0] for r in RAW]
    entries.sort(key=lambda e: order.index(e[0]))

    enum_lines = []
    for i, (key, _, _, _, _) in enumerate(entries):
        enum_lines.append("    Sym%s = %d," % (camel(key), i))

    table_lines = []
    for key, rva, pro, plen, auto in entries:
        table_lines.append('    { "%s", 0x%08Xu, "%s", %d, %s },' % (
            key, rva, pro, plen, "true" if auto else "false"))

    out = []
    out.append("// 自动生成: python sdk/gen_profile.py —— 请勿手改")
    out.append("// UserAssembly TimeDateStamp=0x%08X SizeOfImage=0x%08X" % (timestamp, image_size))
    out.append("namespace ovp")
    out.append("{")
    out.append("")
    out.append("enum Sym : int")
    out.append("{")
    out.extend(enum_lines)
    out.append("    SymCount")
    out.append("};")
    out.append("")
    out.append("struct GenEntry")
    out.append("{")
    out.append("    const char* key;")
    out.append("    uint32_t    rva;")
    out.append("    const char* prologue;    // 小写十六进制, 空串表示不校验")
    out.append("    uint32_t    prologue_len;")
    out.append("    bool        auto_resolved;")
    out.append("};")
    out.append("")
    out.append("static const GenEntry kGenEntries[SymCount] =")
    out.append("{")
    out.extend(table_lines)
    out.append("};")
    out.append("")
    out.append("constexpr int      kUltraCount      = %d;" % len(ULTRA_CLASSES))
    out.append("constexpr uint32_t kBuildTimestamp  = 0x%08Xu;" % timestamp)
    out.append("constexpr uint32_t kBuildImageSize  = 0x%08Xu;" % image_size)
    out.append("")
    out.append("} // namespace ovp")
    out.append("")

    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))

    auto_n = sum(1 for e in entries if e[4])
    print("[+] 符号 %d 个: 自动解析 %d, 人工维护 %d" % (len(entries), auto_n, len(entries) - auto_n))
    if unresolved:
        print("[!] 未解析: " + ", ".join(u.split(" ")[0] for u in unresolved))
    print("[+] 已写入 " + args.out)


if __name__ == "__main__":
    main()
