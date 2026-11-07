#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""probe_rec.py — 直接检查某个 MethodInfo 记录与它的 klass。"""
import numpy as np

from dump import Dump, UA_BASE

MI_DTYPE = np.dtype([
    ("generic", "<u8"), ("mp", "<u8"), ("klass", "<u8"),
    ("slot", "<u4"), ("pad1c", "<u4"), ("invoker", "<u8"), ("maxstack", "<u8"),
    ("token", "<u4"), ("pad34", "<u4"), ("flags", "<u4"), ("pad3c", "<u4"),
])


def dump_rec(d, va):
    b = d.read(va, 0x40)
    if len(b) != 0x40:
        print("  read fail @", hex(va))
        return
    r = np.frombuffer(b, dtype=MI_DTYPE)[0]
    print("  mi     = 0x%016X" % va)
    print("  generic= 0x%016X" % r["generic"])
    print("  mp     = 0x%016X  rva=0x%08X  inUA=%s"
          % (r["mp"], r["mp"] - UA_BASE, 0 < r["mp"] - UA_BASE < 0x1284B000))
    print("  klass  = 0x%016X  inHeap=%s" % (r["klass"], 0x50000000000 <= r["klass"] < 0x50100000000))
    print("  slot   = 0x%X" % r["slot"])
    print("  invoker= 0x%016X" % r["invoker"])
    print("  maxstk = 0x%016X  %s" % (r["maxstack"], "OK" if r["maxstack"] == 0xFFFFFFFF else "MISMATCH"))
    print("  token  = 0x%08X  table=0x%02X  %s" % (r["token"], r["token"] >> 24, "OK" if r["token"] >> 24 == 6 else "BAD"))
    print("  flags  = 0x%08X" % r["flags"])
    kl = int(r["klass"])
    if 0x50000000000 <= kl < 0x50100000000:
        np_ = d.q(kl + 0x20)
        print("  klass+0x20 = 0x%016X -> %r" % (np_, d.cstr(np_, 64) if np_ else None))


def main():
    d = Dump()
    for va in (0x50000b193b8, 0x50000ba7b40, 0x50003262700):
        print("=== record @ 0x%X ===" % va)
        dump_rec(d, va)
        print()

    print("=== Camera klass 0x50000b0d1a8 ===")
    for off in (0x00, 0x08, 0x18, 0x20, 0x58, 0x80, 0x88):
        print("  +0x%02X = 0x%016X" % (off, d.q(0x50000b0d1a8 + off) or 0))

    print("\n=== 从 0x50000b193b8 前后扫 near-by records ===")
    base = 0x50000b193b8
    for i in range(-3, 4):
        va = base + i * 0x40
        b = d.read(va, 0x40)
        if len(b) != 0x40:
            continue
        r = np.frombuffer(b, dtype=MI_DTYPE)[0]
        print("  %+2d 0x%X mp=0x%X tok=0x%08X ms=0x%X kl=0x%X"
              % (i, va, r["mp"], r["token"], r["maxstack"], r["klass"]))


if __name__ == "__main__":
    main()
