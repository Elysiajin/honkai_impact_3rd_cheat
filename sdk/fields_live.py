# -*- coding: utf-8 -*-
"""在活进程里扫 klass 的 FieldInfo 数组，输出 字段名 -> 偏移。"""
import string

from mcp import Mcp

OKCH = set(string.ascii_letters + string.digits + "_./<>$@+-[] `")


def valid_name(m, p, cap=80):
    if not p or not (0x50000000000 <= p < 0x50040000000):
        return None
    b = m.read_bytes(p, cap)
    if not b:
        return None
    z = b.find(b"\x00")
    if z <= 0:
        return None
    try:
        t = b[:z].decode("ascii")
    except UnicodeDecodeError:
        return None
    if not t or len(t) > 60:
        return None
    if any(c not in OKCH for c in t):
        return None
    return t


def probe(m, klass, stride=0x20, prefix_off=0x00, off_off=0x18, try_count=8):
    """试 v = klass+off 作为 FieldInfo 数组，返回 [(name, offset)]。"""
    out = []
    tab = m.q(klass)
    for i in range(try_count):
        fi = tab + i * stride
        np = m.q(fi + prefix_off)
        nm = valid_name(m, np)
        ofs = m.u32(fi + off_off)
        out.append((nm, ofs))
    return out


def find_field_tables(m, klass):
    hits = []
    for off in range(0, 0x100, 8):
        v = m.q(klass + off)
        if not v or not (0x50000000000 <= v < 0x50040000000):
            continue
        for stride in (0x20, 0x18, 0x10):
            for name_off, ofs_off in ((0x00, 0x18), (0x10, 0x18), (0x00, 0x20),
                                      (0x08, 0x18), (0x00, 0x10)):
                names = []
                for i in range(4):
                    fi = v + i * stride
                    np = m.q(fi + name_off)
                    names.append(valid_name(m, np))
                good = sum(1 for n in names if n)
                if good >= 3:
                    ofs = [m.u32(v + i * stride + ofs_off) for i in range(4)]
                    hits.append((off, stride, name_off, ofs_off, names, ofs))
    return hits


if __name__ == "__main__":
    m = Mcp()
    targets = [
        ("BaseMonoMonster", 0x50000C74DC0),
        ("BaseMonoAvatar", 0x50000C72338),
        ("Camera", 0x50000B0D1A8),
        ("AvatarManager", 0x50000CB3950),
        ("MonsterManager", 0x50000CB7900),
    ]
    for name, k in targets:
        nm = m.cstr(m.q(k + 0x20), 64)
        print("== %s klass=0x%X name=%r" % (name, k, nm))
        hits = find_field_tables(m, k)
        if not hits:
            print("   (no field table found)")
        for h in hits[:6]:
            off, stride, name_off, ofs_off, names, ofs = h
            print("   klass+0x%02x stride=0x%X name@+0x%X ofs@+0x%X" %
                  (off, stride, name_off, ofs_off))
            for n, o in zip(names, ofs):
                print("       0x%-5x %s" % (o, n))
