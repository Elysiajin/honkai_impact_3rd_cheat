#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""sig_len.py — 为没有 token 的符号求「最短唯一签名长度」。

为什么需要：bh3_profile.inc 里那 5 个 auto_resolved=False 的符号没有 token 可查，
只能靠前导字节重定位。但 8~16 字节的 x64 prologue 在 289MB 镜像里极度重复
（实测 is_sp_enough 的 8 字节签名命中 64+ 次），拿它去搜根本定位不了。
本脚本回答：每个符号需要多少字节才能在全镜像里唯一，以及这个长度是否稳定
（是否吃到了可能随构建变化的 call/jmp 偏移量）。

同时给出「去偏移量」的版本：把 rel32 的 4 字节和 imm 抹成通配，看唯一性是否
仍能保持 —— 能保持的才是跨构建可用的签名。
"""
import os
import re
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROFILE_INC = os.path.join(ROOT, "overlay", "bh3_profile.inc")
UA = r"D:\Games\miHoYo Launcher\games\Honkai Impact 3rd Game\BH3_Data\Native\UserAssembly.dll"
MAXLEN = 96


def pe_sections(path):
    with open(path, "rb") as f:
        h = f.read(0x1000)
    e = struct.unpack_from("<I", h, 0x3C)[0]
    nsec = struct.unpack_from("<H", h, e + 6)[0]
    optsz = struct.unpack_from("<H", h, e + 20)[0]
    sec = e + 24 + optsz
    out = []
    for i in range(nsec):
        o = sec + i * 40
        name = h[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, va, rawsize, raw = struct.unpack_from("<IIII", h, o + 8)
        out.append((name, va, vsize, raw, rawsize))
    return out


def parse_profile(path):
    text = open(path, encoding="utf-8").read()
    pat = re.compile(r'\{\s*"([a-z0-9_]+)"\s*,\s*0x([0-9A-Fa-f]+)u?\s*,\s*"([0-9a-f]*)"'
                     r'\s*,\s*(\d+)\s*,\s*(true|false)\s*\}')
    return [(m.group(1), int(m.group(2), 16), m.group(3), int(m.group(4)), m.group(5) == "true")
            for m in pat.finditer(text)]


def count_hits(blob, needle):
    n = 0
    i = blob.find(needle)
    while i >= 0:
        n += 1
        if n > 4096:
            return n
        i = blob.find(needle, i + 1)
    return n


def base_off(secs, rva):
    for name, va, vsize, raw, rawsize in secs:
        if va <= rva < va + max(vsize, rawsize):
            return raw + (rva - va)
    return None


def minimal_unique(blob, off, lo=8, hi=MAXLEN):
    for n in range(lo, hi + 1):
        if count_hits(blob, blob[off:off + n]) == 1:
            return n
    return None


def main():
    secs = pe_sections(UA)
    blob = open(UA, "rb").read()
    print("镜像 %.1f MB" % (len(blob) / 1048576.0))

    rows = parse_profile(PROFILE_INC)
    targets = [r for r in rows if not r[4]]
    print("\n=== 5 个 auto_resolved=False 符号的最短唯一签名长度 ===")
    print("%-20s %-11s %-9s %-9s %s" % ("key", "rva", "8B命中", "最短唯一", "首 48 字节"))
    for key, rva, pro, plen, auto in targets:
        off = base_off(secs, rva)
        if off is None:
            print("%-20s 0x%08X (无文件映射)" % (key, rva))
            continue
        body = blob[off:off + 48]
        hits8 = count_hits(blob, blob[off:off + 8]) if plen else 0
        mu = minimal_unique(blob, off, 8, MAXLEN)
        print("%-20s 0x%08X %-9d %-9s %s"
              % (key, rva, hits8, ("%dB" % mu) if mu else ">96B", body.hex()))

    print("\n=== 对照：auto_resolved=True 的符号，最短唯一长度 ===")
    for key, rva, pro, plen, auto in rows:
        if not auto:
            continue
        off = base_off(secs, rva)
        if off is None:
            continue
        mu = minimal_unique(blob, off, 8, MAXLEN)
        if key in ("camera_main", "die_now", "ui_skill_in_cd", "crit_main",
                   "ultra0", "anim_speed_apply"):
            print("%-20s 0x%08X  8B命中=%-4d 最短唯一=%s"
                  % (key, rva, count_hits(blob, blob[off:off + 8]),
                     ("%dB" % mu) if mu else ">96B"))

    print("\n=== 5 个符号的完整函数体预览（前 96 字节）===")
    for key, rva, pro, plen, auto in targets:
        off = base_off(secs, rva)
        if off is None:
            continue
        print("%s @ 0x%08X" % (key, rva))
        print("  " + blob[off:off + 96].hex())


if __name__ == "__main__":
    main()
