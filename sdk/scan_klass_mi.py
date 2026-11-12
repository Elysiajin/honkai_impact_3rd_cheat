# -*- coding: utf-8 -*-
"""在堆 dump 里枚举某个 klass 的全部 MethodInfo（不依赖 klass+0x80）。"""
import struct
import sys

import numpy as np

BASE = 0x50000001000
PATH = r"D:\My Projects\Honkai Impact\mcp_file\read_50000001000_0001.bin"


def scan(klass, path=PATH, base=BASE):
    data = np.fromfile(path, dtype=np.uint8)
    n = (len(data) // 8) * 8
    q = data[:n].view(np.uint64)
    koff = np.uint64(0x10)
    # mi 起点 = 命中地址 - 0x10
    idx = np.nonzero(q == np.uint64(klass))[0]
    res = []
    for i in idx:
        mi_off = int(i) * 8 - 0x10
        if mi_off < 0 or mi_off + 0x40 > len(data):
            continue
        mp, k, slot = struct.unpack_from("<QQQ", data, mi_off + 0x08)[0], \
            struct.unpack_from("<Q", data, mi_off + 0x10)[0], \
            struct.unpack_from("<Q", data, mi_off + 0x18)[0]
        tok = struct.unpack_from("<I", data, mi_off + 0x30)[0]
        mx = struct.unpack_from("<I", data, mi_off + 0x28)[0]
        res.append((tok, mp, slot, base + mi_off, mx))
    return res


if __name__ == "__main__":
    targets = [
        ("AvatarManager", 0x50000CB3950),
        ("MonsterManager", 0x50000CB7900),
        ("BaseMonoMonster", 0x50000C74DC0),
    ]
    for name, k in targets:
        r = scan(k)
        r.sort(key=lambda t: t[0])
        print("\n== %s klass=0x%X  MethodInfo=%d" % (name, k, len(r)))
        for tok, mp, slot, va, mx in r[:16]:
            print("   tok=0x%08X slot=%-8d mi=0x%X mp=0x%X max=0x%X" %
                  (tok, slot, va, mp, mx))
