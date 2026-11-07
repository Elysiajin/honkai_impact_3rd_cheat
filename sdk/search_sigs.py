#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""search_sigs.py — 对没有 token 的 5 个符号做「前导字节签名」全镜像搜索。

这 5 个符号（is_sp_enough / get_skill_cd / is_skill_has_cd /
avatar_mgr_global / monster_mgr_global）在 named 表和游戏 tsv 里都查不到，
它们在 bh3_profile.inc 里 auto_resolved=False —— 永远是手工维护。

问题：换构建时它们会不会静默失效？只有「签名在镜像里唯一」时才敢说签名可用于重定位。
本脚本回答：给定 bh3_profile.inc 里的前导字节，
  1. 在 UserAssembly.dll 里出现几次？
  2. 命中位置是否包含 profile 里的 rva？
  3. 命中的位置前面是不是函数边界（对齐 / 有 int3 或 ret 填充）？
"""
import os
import re
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROFILE_INC = os.path.join(ROOT, "overlay", "bh3_profile.inc")
UA = r"D:\Games\miHoYo Launcher\games\Honkai Impact 3rd Game\BH3_Data\Native\UserAssembly.dll"

# 只查这四个：mgr_global 没有前导字节（plen=0），无法做签名
KEYS = ["is_sp_enough", "get_skill_cd", "is_skill_has_cd"]


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


def rva2off(sections, rva):
    for name, va, vsize, raw, rawsize in sections:
        if va <= rva < va + max(vsize, rawsize):
            return raw + (rva - va), name
    return None, None


def parse_profile(path):
    text = open(path, encoding="utf-8").read()
    pat = re.compile(r'\{\s*"([a-z0-9_]+)"\s*,\s*0x([0-9A-Fa-f]+)u?\s*,\s*"([0-9a-f]*)"'
                     r'\s*,\s*(\d+)\s*,\s*(true|false)\s*\}')
    return [(m.group(1), int(m.group(2), 16), m.group(3), int(m.group(4)), m.group(5) == "true")
            for m in pat.finditer(text)]


def find_all(blob, needle):
    out = []
    i = blob.find(needle)
    while i >= 0 and len(out) < 64:
        out.append(i)
        i = blob.find(needle, i + 1)
    return out


def main():
    secs = pe_sections(UA)
    blob = open(UA, "rb").read()
    prof = {k: (rva, pro, plen, auto) for k, rva, pro, plen, auto in parse_profile(PROFILE_INC)}

    print("镜像 %.1f MB, 节 %d 个" % (len(blob) / 1048576.0, len(secs)))
    print("%-18s %-10s %-8s %-10s %-8s %s"
          % ("key", "rva", "plen", "命中数", "含目标", "命中位置"))
    for key in KEYS:
        rva, pro, plen, auto = prof[key]
        needle = bytes.fromhex(pro)
        hits = find_all(blob, needle)
        # 转成 rva
        rvas = []
        for off in hits:
            for name, va, vsize, raw, rawsize in secs:
                if raw <= off < raw + rawsize:
                    rvas.append((va + off - raw, name))
                    break
        contains = any(r == rva for r, _ in rvas)
        pos = ", ".join("0x%X(%s)" % (r, n) for r, n in rvas[:6])
        print("%-18s 0x%08X %-8d %-10d %-8s %s"
              % (key, rva, plen, len(rvas), "是" if contains else "否", pos))

        # 目标位置前后字节，看是不是函数边界
        off, name = rva2off(secs, rva)
        if off is not None:
            pre = blob[max(0, off - 8):off]
            print("     前置 8 字节: %s   本函数首 24: %s"
                  % (pre.hex(), blob[off:off + 24].hex()))

    print("\n=== 对照：有 token 的符号，签名唯一性 ===")
    for key in ("camera_main", "die_now", "ui_skill_in_cd"):
        rva, pro, plen, auto = prof[key]
        if not pro:
            continue
        hits = find_all(blob, bytes.fromhex(pro))
        rvas = []
        for off in hits:
            for name, va, vsize, raw, rawsize in secs:
                if raw <= off < raw + rawsize:
                    rvas.append(va + off - raw)
                    break
        print("%-18s 签名命中 %d 次 %s  含目标=%s"
              % (key, len(rvas), [hex(x) for x in rvas[:5]],
                 any(r == rva for r in rvas)))


if __name__ == "__main__":
    main()
