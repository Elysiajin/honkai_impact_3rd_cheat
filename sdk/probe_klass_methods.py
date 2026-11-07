#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""probe_klass_methods.py — 运行期能否「由 klass 直接定位方法数组」？

token 反查已经验证可行（27/27），但全堆扫描 2.1GB 太慢，不能每次调用都做。
本脚本回答：Camera 的 klass(0x50000b0d1a8) 里有没有一个字段直接指向它的
MethodInfo 数组？如果有，运行期就能 O(1) 取到该类的全部方法，按 +0x30 token
匹配即可，完全不需要扫描。
"""
import numpy as np

from dump import Dump, UA_BASE

MI = np.dtype([
    ("generic", "<u8"), ("mp", "<u8"), ("klass", "<u8"),
    ("slot", "<u4"), ("pad1c", "<u4"), ("invoker", "<u8"), ("maxstack", "<u8"),
    ("token", "<u4"), ("pad34", "<u4"), ("flags", "<u4"), ("pad3c", "<u4"),
])

CAM_KLASS = 0x50000B0D1A8
CAM_MI = 0x50000B193B8          # token 0x06000264 的记录
KLASS_SIZE = 0xD8


def main():
    d = Dump()

    print("=== 1. 从已知记录向后/向前找 Camera 方法数组边界 ===")
    lo = CAM_MI
    while True:
        b = d.read(lo - 0x40, 0x40)
        if len(b) != 0x40:
            break
        r = np.frombuffer(b, dtype=MI)[0]
        if int(r["klass"]) != CAM_KLASS or int(r["maxstack"]) != 0xFFFFFFFF:
            break
        lo -= 0x40
    hi = CAM_MI
    n = 0
    while n < 4096:
        b = d.read(hi + 0x40, 0x40)
        if len(b) != 0x40:
            break
        r = np.frombuffer(b, dtype=MI)[0]
        if int(r["klass"]) != CAM_KLASS or int(r["maxstack"]) != 0xFFFFFFFF:
            break
        hi += 0x40
        n += 1
    cnt = (hi - lo) // 0x40 + 1
    print("  数组推断: base=0x%X .. 0x%X  记录数=%d" % (lo, hi, cnt))
    b = d.read(lo, 0x40)
    r = np.frombuffer(b, dtype=MI)[0]
    print("  首记录 token=0x%08X slot=0x%X mp=0x%X" % (r["token"], r["slot"], r["mp"]))
    b = d.read(hi, 0x40)
    r = np.frombuffer(b, dtype=MI)[0]
    print("  末记录 token=0x%08X slot=0x%X mp=0x%X" % (r["token"], r["slot"], r["mp"]))

    print("\n=== 2. Camera klass 全部 0xD8 字节（每 8 字节一个 qword）===")
    for off in range(0, KLASS_SIZE, 8):
        v = d.q(CAM_KLASS + off)
        note = ""
        if v is not None:
            if 0x50000000000 <= v < 0x50100000000:
                note = "heap"
            elif UA_BASE <= v < UA_BASE + 0x1284B000:
                note = "UA rva=0x%X" % (v - UA_BASE)
            elif lo <= v <= hi + 0x40:
                note = "*** 指向方法数组内 ***"
            elif v:
                note = "?"
        print("  +0x%02X = 0x%016X  %s" % (off, v or 0, note))

    print("\n=== 3. 全 0x500 堆搜『指向 lo..hi 的 qword』===")
    targets = set(range(lo, hi + 0x40, 8))
    for base, end, f, prot, typ, path in d.regions:
        if not (0x50000000000 <= base < 0x50100000000):
            continue
        import os
        try:
            fsz = os.path.getsize(f)
        except OSError:
            continue
        n = min(end - base, fsz) // 8
        if not n:
            continue
        mm = np.memmap(f, dtype=np.uint64, mode="r", shape=(n,))
        hit = np.isin(mm, np.fromiter(targets, dtype=np.uint64))
        idx = np.nonzero(hit)[0]
        for i in idx[:20]:
            print("  @ 0x%X -> 0x%X" % (base + int(i) * 8, int(mm[i])))
        if idx.size:
            print("  (region 0x%X: %d hits)" % (base, idx.size))
        del mm, hit

    print("\n=== 4. klass+0x58 perModuleTable 指向什么 ===")
    pmt = d.q(CAM_KLASS + 0x58)
    print("  perModuleTable = 0x%X" % pmt)
    if pmt:
        for off in range(0, 0x60, 8):
            v = d.q(pmt + off)
            note = ""
            if v and 0x50000000000 <= v < 0x50100000000:
                note = "heap"
                if lo <= v <= hi:
                    note = "*** 方法数组 ***"
            elif v and UA_BASE <= v < UA_BASE + 0x1284B000:
                note = "UA rva=0x%X" % (v - UA_BASE)
            print("    +0x%02X = 0x%016X  %s" % (off, v or 0, note))

    print("\n=== 5. klass+0x80 nextClass 链（是否连续 0xD8 自增）===")
    nc = d.q(CAM_KLASS + 0x80)
    print("  nextClass = 0x%X" % nc)
    if nc:
        for i in range(4):
            v = d.q(nc + i * 8)
            print("    [0x%X + %d*8] = 0x%X" % (nc, i, v or 0))


if __name__ == "__main__":
    main()
