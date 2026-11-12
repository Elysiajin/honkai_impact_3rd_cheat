# -*- coding: utf-8 -*-
"""在活进程里搜索「字段表」：{name_ptr, offset32} 交错数组。"""
import string

from mcp import Mcp

OKCH = set(string.ascii_letters + string.digits + "_./<>$@+-[] `")
HEAP_LO, HEAP_HI = 0x50000000000, 0x50040000000


def name_at(m, p, cap=72):
    if not p or not (HEAP_LO <= p < HEAP_HI):
        return None
    b = m.read_bytes(p, cap)
    if len(b) < 2:
        return None
    z = b.find(b"\x00")
    if z <= 0:
        return None
    try:
        t = b[:z].decode("ascii")
    except UnicodeDecodeError:
        return None
    if len(t) > 48 or not t:
        return None
    if any(c not in OKCH for c in t):
        return None
    return t


def try_table(m, at, stride, n=8, need=5):
    """把 at 当 {name_ptr,u32 offset} 交错表试探。"""
    names = []
    ok = 0
    for i in range(n):
        p = m.q(at + i * stride)
        nm = name_at(m, p)
        names.append(nm)
        if nm:
            ok += 1
    return ok >= need, ok, names


def main():
    m = Mcp()
    targets = [
        ("BaseMonoMonster", 0x50000C74DC0),
        ("BaseMonoAvatar", 0x50000C72338),
        ("AvatarManager", 0x50000CB3950),
        ("MonsterManager", 0x50000CB7900),
        ("Camera", 0x50000B0D1A8),
    ]
    for cname, k in targets:
        print("== %s klass=0x%X" % (cname, k))
        for off in range(0, 0x100, 8):
            v = m.q(k + off)
            if not v or not (HEAP_LO <= v < HEAP_HI):
                continue
            for stride in (0x10, 0x20, 0x18):
                good, ok, names = try_table(m, v, stride)
                if good:
                    offs = [m.u32(v + i * stride + 8) for i in range(8)]
                    print("   klass+0x%02x -> 0x%X stride=0x%X ok=%d" %
                          (off, v, stride, ok))
                    for nm, o in zip(names, offs):
                        print("        0x%-5x %s" % (o, nm))
        print()


if __name__ == "__main__":
    main()
