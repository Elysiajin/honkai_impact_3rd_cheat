"""在 IL2CPP 堆里搜所有 klass == AvatarManager / MonsterManager 的对象实例.

思路: 任何托管对象首字段都是 klass 指针, 所以堆里 qword == klass_va 的位置
就是一个实例 (或其起始). 找到实例后再看哪个实例的 ::get_Instance 被引用,
从而定位真正该读的全局槽.
"""
import struct

import numpy as np

import dump as D

KLASSES = [
    ("AvatarManager", 0x50000CB3950),
    ("MonsterManager", 0x50000CB7900),
]
HEAP = [(0x50000001000, 0x50015E01000), (0x50015E01000, 0x50016D01000), (0x50016D01000, 0x5001738010)]


def heap_blobs(d):
    """返回 [(base, bytes)] 覆盖整个 0x500 堆."""
    out = []
    for lo, hi in HEAP:
        buf = bytearray()
        cur = lo
        while cur < hi:
            n = min(1 << 20, hi - cur)
            b = d.read(cur, n)
            if len(b) != n:
                buf.extend(b"\x00" * (n - len(b)))
                break
            buf.extend(b)
            cur += n
        out.append((lo, bytes(buf)))
    return out


def scan(blobs, value, limit=32):
    pat = struct.pack("<Q", value)
    hits = []
    for lo, buf in blobs:
        arr = np.frombuffer(buf, dtype=np.uint8)
        cand = np.flatnonzero(arr[: max(0, arr.size - 7)] == np.uint8(value & 0xFF))
        for c in cand:
            if buf[c:c + 8] == pat:
                hits.append(lo + int(c))
                if len(hits) >= limit:
                    return hits
    return hits


def main():
    d = D.Dump()
    blobs = heap_blobs(d)
    print("heap loaded:", [(hex(lo), len(b)) for lo, b in blobs])
    for name, klass in KLASSES:
        hits = scan(blobs, klass)
        print("=" * 70)
        print("%s klass=0x%X  instances=%d" % (name, klass, len(hits)))
        for h in hits[:8]:
            print("    obj 0x%X" % h)
            for off in (0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50):
                v = d.q(h + off)
                tag = ""
                vv = v or 0
                if 0x50000000000 <= vv < 0x60000000000:
                    np_ = d.q(vv + 0x20)
                    nm = d.cstr(np_, 32) if np_ else None
                    if nm and all(32 <= ord(c) < 127 for c in nm):
                        tag = " name=%r" % nm
                print("        +0x%-2X = 0x%016X%s" % (off, vv, tag))


if __name__ == "__main__":
    main()
