#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""probe_cam.py — 为什么 camera_main 的 token 查出来的 rva 与 profile 不一致？"""
import numpy as np

from dump import Dump, UA_BASE, UA_SIZE

MAXSTACK = 0x00000000FFFFFFFF
MI = np.dtype([
    ("generic", "<u8"), ("mp", "<u8"), ("klass", "<u8"),
    ("slot", "<u4"), ("pad1c", "<u4"), ("invoker", "<u8"), ("maxstack", "<u8"),
    ("token", "<u4"), ("pad34", "<u4"), ("flags", "<u4"), ("pad3c", "<u4"),
])

TARGET_TOKENS = {0x06000264: "Camera.get_main",
                 0x0600020A: "Camera.get_pixelWidth",
                 0x0600020B: "Camera.get_pixelHeight"}
PROFILE_RVA = {0x06000264: 0x121E6560, 0x0600020A: 0x121E5A80, 0x0600020B: 0x121E5A90}


def dis(a, b):
    print("   bytes:", a.hex() if b is None else b.hex())


def main():
    d = Dump()

    print("=== 1. profile 里的地址在 dump 里是什么 ===")
    for tok, rva in PROFILE_RVA.items():
        b = d.read(UA_BASE + rva, 16)
        print("  %-24s rva=0x%08X  %s" % (TARGET_TOKENS[tok], rva, b.hex()))

    print("\n=== 2. 全 0x500 堆扫 MethodInfo，打印匹配这些 token 的记录 ===")
    for base, end, f, prot, typ, path in d.regions:
        if not (0x50000000000 <= base < 0x50100000000):
            continue
        import os
        try:
            fsz = os.path.getsize(f)
        except OSError:
            print("  region %s: no file" % hex(base)); continue
        n = min(end - base, fsz) // 0x40
        if not n:
            print("  region %s: empty" % hex(base)); continue
        mm = np.memmap(f, dtype=np.uint8, mode="r", shape=(n * 0x40,))
        a = mm.view(MI)
        for tok in TARGET_TOKENS:
            idx = np.nonzero(a["token"] == tok)[0]
            for i in idx:
                kl = int(a["klass"][i])
                namep = d.q(kl + 0x20)
                nm = d.cstr(namep, 48) if namep else "?"
                print("  tok=0x%08X @ %s  mp=0x%X (rva 0x%X)  klass=0x%X name=%r ms=0x%X flags=0x%X"
                      % (tok, hex(base + int(i) * 0x40), int(a["mp"][i]),
                         int(a["mp"][i]) - UA_BASE, kl, nm,
                         int(a["maxstack"][i]), int(a["flags"][i])))
        del a, mm

    print("\n=== 3. profile 地址 0x121E6560 作为 qword 在哪些 region 出现 ===")
    target = UA_BASE + 0x121E6560
    hits = 0
    for base, end, f, prot, typ, path in d.regions:
        import os
        try:
            fsz = os.path.getsize(f)
        except OSError:
            continue
        n = min(end - base, fsz) // 8
        if not n:
            continue
        mm = np.memmap(f, dtype=np.uint64, mode="r", shape=(n,))
        idx = np.nonzero(mm == target)[0]
        for i in idx:
            print("  hit @ %s (+0x%X in region)" % (hex(base + int(i) * 8), int(i) * 8))
            hits += 1
        del mm
        if hits > 40:
            break
    print("  total hits:", hits)


if __name__ == "__main__":
    main()
