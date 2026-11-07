#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""probe_mi.py — 诊断 MethodInfo 扫描：每一步筛掉多少，记录到底在哪。"""
import os
import re

import numpy as np

from dump import Dump, UA_BASE, UA_SIZE

MAXSTACK = 0x00000000FFFFFFFF
MI = np.dtype([
    ("generic", "<u8"), ("mp", "<u8"), ("klass", "<u8"),
    ("slot", "<u4"), ("pad1c", "<u4"), ("invoker", "<u8"), ("maxstack", "<u8"),
    ("token", "<u4"), ("pad34", "<u4"), ("flags", "<u4"), ("pad3c", "<u4"),
])


def main():
    d = Dump()
    spans = [(b, e - b, f, prot, typ)
             for b, e, f, prot, typ, path in d.regions
             if typ != "image" and "r" in prot.lower()]
    spans.sort(key=lambda s: -s[1])
    print("total private spans %d, %.0f MB"
          % (len(spans), sum(s[1] for s in spans) / 1048576))

    print("\n%-18s %-12s %-5s %-6s %-10s %-10s %s"
          % ("base", "size_MB", "type", "prot", "mp_ok", "ms_ok", "tok_ok"))
    for base, size, f, prot, typ in spans[:20]:
        try:
            fsz = os.path.getsize(f)
        except OSError:
            print("%-18s %-12.1f %-5s %-6s (no file)" % (hex(base), size / 1048576, typ, prot))
            continue
        avail = min(size, fsz)
        n = avail // 0x40
        if n == 0:
            print("%-18s %-12.1f %-5s %-6s (empty)" % (hex(base), size / 1048576, typ, prot))
            continue
        mm = np.memmap(f, dtype=np.uint8, mode="r", shape=(n * 0x40,))
        a = mm.view(MI)
        mp_ok = (a["mp"] >= UA_BASE + 0x1000) & (a["mp"] < UA_BASE + UA_SIZE)
        ms_ok = mp_ok & (a["maxstack"] == MAXSTACK)
        tok_ok = ms_ok & ((a["token"] >> 24) == 6)
        print("%-18s %-12.1f %-5s %-6s %-10d %-10d %d"
              % (hex(base), size / 1048576, typ, prot,
                 int(mp_ok.sum()), int(ms_ok.sum()), int(tok_ok.sum())))
        del a, mm

    # 全量：不限定 klass 范围
    print("\n=== 全量统计（不限 klass 区间）===")
    total_mp = total_ms = total_tok = 0
    klass_hist = {}
    for base, size, f, prot, typ in spans:
        try:
            fsz = os.path.getsize(f)
        except OSError:
            continue
        avail = min(size, fsz)
        n = avail // 0x40
        if n == 0:
            continue
        mm = np.memmap(f, dtype=np.uint8, mode="r", shape=(n * 0x40,))
        a = mm.view(MI)
        mp_ok = (a["mp"] >= UA_BASE + 0x1000) & (a["mp"] < UA_BASE + UA_SIZE)
        ms_ok = mp_ok & (a["maxstack"] == MAXSTACK)
        tok_ok = ms_ok & ((a["token"] >> 24) == 6)
        total_mp += int(mp_ok.sum())
        total_ms += int(ms_ok.sum())
        total_tok += int(tok_ok.sum())
        for kl in a["klass"][tok_ok]:
            hi = int(kl) >> 32
            klass_hist[hi] = klass_hist.get(hi, 0) + 1
        del a, mm
    print("mp_ok=%d ms_ok=%d tok_ok=%d" % (total_mp, total_ms, total_tok))
    print("klass 高位分布(前10):",
          sorted(klass_hist.items(), key=lambda kv: -kv[1])[:10])


if __name__ == "__main__":
    main()
