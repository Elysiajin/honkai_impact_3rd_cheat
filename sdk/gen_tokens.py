#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""gen_tokens.py — 生成 overlay/bh3_tokens.inc（只增不改，不修改任何现有文件）

为什么需要这张表
----------------
bh3_profile.inc 里每个符号只有一个 uint32 rva + 前导字节校验（PrologueOk）。
前导字节校验能挡住「构建完全变了」，但挡不住「构建只有局部变化，某个符号
恰好前导 8~16 字节没变、地址却落到了另一个函数上」——那时 PrologueOk 通过，
overlay 会拿着一个错误地址去调用，直接崩。

IL2CPP 的每个托管方法都有稳定的 methodToken（MethodInfo+0x30，0x06000000|rid）。
它与地址无关、跨构建语义稳定（实测 137,167/137,182 条与游戏侧 token 一致，
rva agreement 99.99%）。所以：**地址会变，token 不变**。

本表把每个符号的期望 token 固化下来，运行期可以：
    mi = 由 rva 反查到的 MethodInfo（或直接读 profile 里的 methodinfo）
    if (rd<uint32_t>(mi + 0x30) != kTokens[sym]) -> 地址不对，拒绝调用
即「宁可功能不生效，也不拿错地址去调」。

数据来源
--------
  * sdk/bh3_methods_named.tsv   (class, method) -> token / rva，唯一命中才采用
  * overlay/bh3_profile.inc     读取 Sym 枚举顺序，保证下标一一对应

未解析的符号写 0；运行期把 0 解释为「无 token 校验」，只做原有 prologue 校验。
"""
import argparse
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_PROFILE = os.path.join(ROOT, "overlay", "bh3_profile.inc")
DEFAULT_NAMED = os.path.join(ROOT, "sdk", "bh3_methods_named.tsv")
DEFAULT_OUT = os.path.join(ROOT, "overlay", "bh3_tokens.inc")

# key -> (class, method)。key 必须与 bh3_profile.inc 的 GenEntry 第一列一致。
# ultra0..ultra11 由 ULTRA_CLASSES 按 gen_profile.py 的同一顺序补齐。
SYMBOLS = {
    "camera_main": ("Camera", "get_main"),
    "camera_pixel_w": ("Camera", "get_pixelWidth"),
    "camera_pixel_h": ("Camera", "get_pixelHeight"),
    "camera_w2c": ("Camera", "INTERNAL_get_worldToCameraMatrix"),
    "camera_proj": ("Camera", "INTERNAL_get_projectionMatrix"),
    "die_now": ("BaseMonoMonster", "DieNow"),
    "be_hit": ("BaseMonoAvatar", "BeHit"),
    "ui_skill_in_cd": ("MonoSkillButton", "IsSkillInCD"),
    "ui_can_use_skill": ("MonoSkillButton", "CanUseSkill"),
    "avatar_can_use_skill": ("BaseMonoAvatar", "CanUseSkill"),
    "crit_main": ("MHEMGJMNDJI", "TryApplyCrit"),
    "crit_phys": ("MHEMGJMNDJI", "TryApplyPhysicCrit"),
    "crit_elem": ("MHEMGJMNDJI", "TryApplyElementCrit"),
    "anim_speed_apply": ("BaseMonoAnimatorEntity", "ApplyOverrideAnimatorSpeed"),
    "anim_speed_cancel": ("BaseMonoAnimatorEntity", "CancelOverrideAnimatorSpeed"),
}

ULTRA_CLASSES = [
    "MonoFuka", "MonoFuka_BattleY",
    "MonoFukaFL", "MonoFukaFL_BattleY",
    "MonoFukaIN", "MonoFukaIN_BattleY",
    "MonoFukaJW", "MonoFukaJW_BattleY",
    "MonoFukaND", "MonoFukaND_BattleY",
    "MonoFukaNL", "MonoFukaNL_BattleY",
]
for _i, _cls in enumerate(ULTRA_CLASSES):
    SYMBOLS["ultra%d" % _i] = (_cls, "AllowUseUltra")


def load_tokens(path):
    """(class, method) -> token。只收 rva 非 0 且 token 非 0 的行。"""
    out = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        head = f.readline().rstrip("\n").split("\t")
        idx = {name: i for i, name in enumerate(head)}
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) < len(head):
                continue
            try:
                token = int(parts[idx["token"]], 16)
                rva = int(parts[idx["rva"]], 16)
            except (ValueError, KeyError):
                continue
            if not token or not rva:
                continue
            key = (parts[idx["class"]], parts[idx["method"]])
            out.setdefault(key, set()).add(token)
    return out


def parse_profile(path):
    """从 bh3_profile.inc 读出 (key, rva, prologue_len, auto_resolved) 列表，保持文件顺序。"""
    text = open(path, encoding="utf-8").read()
    pat = re.compile(
        r'\{\s*"([a-z0-9_]+)"\s*,\s*0x([0-9A-Fa-f]+)u?\s*,\s*"([0-9a-f]*)"\s*,'
        r'\s*(\d+)\s*,\s*(true|false)\s*\}')
    rows = []
    for m in pat.finditer(text):
        rows.append((m.group(1), int(m.group(2), 16), int(m.group(4)),
                     m.group(5) == "true"))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--profile", default=DEFAULT_PROFILE)
    ap.add_argument("--named", default=DEFAULT_NAMED)
    ap.add_argument("--out", default=DEFAULT_OUT)
    args = ap.parse_args()

    entries = parse_profile(args.profile)
    table = load_tokens(args.named)

    resolved, missing = [], []
    out_rows = []
    for key, rva, plen, auto in entries:
        pair = SYMBOLS.get(key)
        hits = sorted(table.get(pair, set())) if pair else []
        if len(hits) == 1:
            out_rows.append((key, rva, hits[0]))
            resolved.append(key)
        else:
            out_rows.append((key, rva, 0))
            missing.append("%s(%s)" % (key, "未映射" if not pair else
                                       "未找到" if not hits else "%d 候选" % len(hits)))

    lines = []
    lines.append("// 自动生成: python sdk/gen_tokens.py —— 请勿手改")
    lines.append("// 用途: 每个符号的 IL2CPP methodToken(0x06xxxxxx)，与地址无关、跨构建稳定。")
    lines.append("// 运行期读 MethodInfo+0x30 与 kTokens[sym] 比对；0 表示该符号无 token 校验。")
    lines.append("// 下标与 bh3_profile.inc 的 enum Sym 严格同序，可直接 kTokens[SymCameraMain]。")
    lines.append("#pragma once")
    lines.append("#include <cstdint>")
    lines.append("")
    lines.append("namespace ovp")
    lines.append("{")
    lines.append("")
    lines.append("static const uint32_t kTokens[] =")
    lines.append("{")
    for key, rva, tok in out_rows:
        note = "0x%08X" % tok if tok else "无"
        lines.append("    %-10su,   // %s  rva=0x%08X  token=%s"
                     % ("0x%08X" % tok, key, rva, note))
    lines.append("};")
    lines.append("")
    lines.append("constexpr int kTokenCount = %d;" % len(out_rows))
    lines.append("")
    lines.append("} // namespace ovp")
    lines.append("")

    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))

    print("[+] 符号 %d 个: 有 token %d, 无 token %d"
          % (len(out_rows), len(resolved), len(missing)))
    if missing:
        print("[!] 无 token 校验: " + ", ".join(missing))
    print("[+] 已写入 " + args.out)


if __name__ == "__main__":
    main()
