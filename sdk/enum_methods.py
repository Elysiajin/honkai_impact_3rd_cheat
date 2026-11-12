# -*- coding: utf-8 -*-
"""活进程枚举 klass+0x80 方法表，把 token 补全（尤其 Manager 类）。"""
import sys

from mcp import Mcp

HEAP_LO, HEAP_HI = 0x50000000000, 0x50040000000


def enum_methods(m, klass, limit=4096):
    tab = m.q(klass + 0x80)
    if not tab:
        return []
    out = []
    for i in range(limit):
        p = m.q(tab + i * 8)
        if not p or not (HEAP_LO <= p < HEAP_HI):
            break
        mi = p - 8
        k = m.q(mi + 0x10)
        if k != klass:
            continue
        tok = m.u32(mi + 0x30)
        mp = m.q(mi + 0x08)
        slot = m.u32(mi + 0x18)
        out.append((tok, mp, slot, mi))
    return out


def main():
    m = Mcp()
    st = m.status()
    ua = int(st["ua"], 16)
    targets = [
        ("AvatarManager", 0x50000CB3950),
        ("MonsterManager", 0x50000CB7900),
        ("Camera", 0x50000B0D1A8),
    ]
    for name, k in targets:
        nm = m.cstr(m.q(k + 0x20), 48)
        ms = enum_methods(m, k)
        print("\n== %s klass=0x%X realname=%r methods=%d" % (name, k, nm, len(ms)))
        if not ms:
            continue
        toks = [t for t, _, _, _ in ms]
        print("   token range 0x%08X .. 0x%08X" % (min(toks), max(toks)))
        for tok, mp, slot, mi in ms[:12]:
            print("   tok=0x%08X slot=%-5d mp=0x%X rva=0x%X" %
                  (tok, slot, mp, mp - ua))


if __name__ == "__main__":
    sys.exit(main())
