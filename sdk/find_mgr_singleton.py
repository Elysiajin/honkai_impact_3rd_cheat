"""查 manager 单例真实来源.

1) 彻底探测 RVA 0x2641588 / 0x2642EA8 指向的是什么对象
2) 在 dump 里全量搜 AvatarManager / MonsterManager 的 klass 指针引用
"""
import struct

import numpy as np

import dump as D

GLOBALS = [("avatar_mgr_global", 0x2641588), ("monster_mgr_global", 0x2642EA8)]
KLASSES = [("AvatarManager", 0x50000CB3950), ("MonsterManager", 0x50000CB7900)]


def scan_qword(d, value, limit=64):
    pat = struct.pack("<Q", value)
    needle = np.frombuffer(pat, dtype=np.uint8)
    hits = []
    for base, end, fname, prot, typ, path in d.regions:
        try:
            blob = d._blob(fname)
        except Exception:
            continue
        if not blob or len(blob) < 8:
            continue
        arr = np.frombuffer(blob, dtype=np.uint8)
        if arr.size < 8:
            continue
        # 向量化找首字节, 再逐点验证
        cand = np.flatnonzero(arr[: arr.size - 7] == needle[0])
        for c in cand:
            if arr[c:c + 8].tobytes() == pat:
                hits.append((base + int(c), path))
                if len(hits) >= limit:
                    return hits
    return hits


def probe(d, va, label):
    print("  %s @0x%X:" % (label, va))
    for off in (0, 8, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38):
        v = d.q(va + off)
        print("      +0x%-2X = 0x%016X" % (off, v if v is not None else 0))


def main():
    d = D.Dump()
    for gname, rva in GLOBALS:
        va = D.UA_BASE + rva
        print("=" * 74)
        print("%s rva=0x%X va=0x%X" % (gname, rva, va))
        obj = d.q(va)
        print("  *global = 0x%X" % obj)
        if not obj:
            continue
        probe(d, obj, "obj")
        # obj 是否像个 MethodInfo: +0x30 应为 0x06xxxxxx, +0x10 应为 0x500...
        tok = d.u32(obj + 0x30)
        print("  obj+0x30(token?) = 0x%X   0x06xxxxxx=%s" % (
            tok or 0, (tok or 0) >> 24 == 0x06))
        k10 = d.q(obj + 0x10)
        print("  obj+0x10(klass?) = 0x%X  name=%r" % (
            k10 or 0, d.cstr(d.q(k10 + 0x20), 40) if k10 and d.q(k10 + 0x20) else None))
        k8 = d.q(obj + 0x8)
        print("  obj+0x8(mp?) = 0x%X  inUA=%s" % (
            k8 or 0, bool(k8 and D.UA_BASE <= k8 < D.UA_BASE + D.UA_SIZE)))

    print("=" * 74)
    for name, klass in KLASSES:
        hits = scan_qword(d, klass)
        print("%s klass=0x%X refs=%d" % (name, klass, len(hits)))
        for h, path in hits[:24]:
            inua = D.UA_BASE <= h < D.UA_BASE + D.UA_SIZE
            print("   0x%016X  rva=%-10s %s" % (
                h, ("0x%X" % (h - D.UA_BASE)) if inua else "-", (path or "")[-24:]))


if __name__ == "__main__":
    main()
