# -*- coding: utf-8 -*-
"""在 0x500 堆 dump 里找出所有「名字 == 目标」的 klass 候选，并统计各自方法表大小。"""
import struct
import sys

import numpy as np

BASE = 0x50000001000
PATH = r"D:\My Projects\Honkai Impact\mcp_file\read_50000001000_0001.bin"
HEAP_LO, HEAP_HI = 0x50000000000, 0x50040000000


def load_q(path=PATH):
    data = open(path, "rb").read()
    n = (len(data) // 8) * 8
    q = np.frombuffer(data, dtype=np.uint64, count=n // 8)
    return data, q


def cstr(data, va, cap=64):
    off = va - BASE
    if off < 0 or off >= len(data) - 1:
        return None
    end = data.find(b"\x00", off, off + cap)
    if end <= off:
        return None
    try:
        s = data[off:end].decode("ascii")
    except UnicodeDecodeError:
        return None
    return s


def main():
    data, q = load_q()
    names = sys.argv[1:] or ["AvatarManager", "MonsterManager"]
    for nm in names:
        needle = nm.encode() + b"\x00"
        pos = 0
        strs = []
        while True:
            i = data.find(needle, pos)
            if i < 0:
                break
            strs.append(BASE + i)
            pos = i + 1
        print("\n== %s : name strings %d %s" %
              (nm, len(strs), [hex(x) for x in strs[:6]]))
        for s in strs[:4]:
            # 找谁指向这个字符串
            hits = np.nonzero(q == np.uint64(s))[0]
            for h in hits[:4]:
                k = BASE + int(h) * 8
                # 候选 klass：k 是「字段在 +0x20」→ klass = k - 0x20
                kl = k - 0x20
                tab = struct.unpack_from("<Q", data, kl - BASE + 0x80)[0] \
                    if 0 <= kl - BASE + 0x88 <= len(data) else 0
                tdi = struct.unpack_from("<I", data, kl - BASE + 0x88)[0] \
                    if 0 <= kl - BASE + 0x8C <= len(data) else 0
                cnt = 0
                if tab and HEAP_LO <= tab < HEAP_HI:
                    for j in range(4096):
                        o = tab - BASE + j * 8
                        if o < 0 or o + 8 > len(data):
                            break
                        p = struct.unpack_from("<Q", data, o)[0]
                        if not p or not (HEAP_LO <= p < HEAP_HI):
                            break
                        mi = p - 8
                        mo = mi - BASE + 0x10
                        if mo < 0 or mo + 8 > len(data):
                            break
                        if struct.unpack_from("<Q", data, mo)[0] != kl:
                            break
                        cnt += 1
                print("   ref@0x%X -> klass=0x%X tab=0x%X tdi=0x%X methods=%d" %
                      (k, kl, tab, tdi, cnt))


if __name__ == "__main__":
    sys.exit(main())
