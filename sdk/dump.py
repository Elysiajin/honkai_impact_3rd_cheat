"""CE Search 内存 dump 随机访问层.

用法:
    from dump import Dump
    d = Dump()
    d.q(0x7ff8c5df54b8)        # 读 8 字节
    d.read(va, n)              # 读 n 字节, 跨 region 自动拼接
    d.cstr(va, 64)             # 读 C 字符串
    d.regions_for(0x7ff8c38a0000)

regions.csv 每行: base,size,readable,protection,type,file,offset,path
每个 region 对应同目录一个 .bin, 内容即该 VA 区间原始字节.
"""
import csv
import os
import struct

DUMP_DIR = r"D:\Cheat Table\CE Search"
UA_BASE = 0x7FF8C38A0000
UA_SIZE = 0x1284B000


class Dump:
    def __init__(self, d=DUMP_DIR):
        self.dir = d
        self.regions = []  # (start, end, file, prot, type, path)
        self._cache = {}
        with open(os.path.join(d, "regions.csv"), encoding="utf-8", errors="replace") as f:
            for r in csv.DictReader(f):
                try:
                    b = int(r["base"], 16)
                    sz = int(r["size"], 16)
                except (ValueError, KeyError):
                    continue
                self.regions.append((b, b + sz, r["file"], r["protection"], r["type"], r.get("path", "")))
        self.regions.sort()
        self.starts = [x[0] for x in self.regions]

    # ---------- 低层 ----------
    def _blob(self, path):
        if path not in self._cache:
            try:
                self._cache[path] = open(path, "rb").read()
            except OSError:
                self._cache[path] = b""
        return self._cache[path]

    def _region_at(self, va):
        lo, hi = 0, len(self.starts) - 1
        best = None
        while lo <= hi:
            mid = (lo + hi) // 2
            s, e = self.regions[mid][0], self.regions[mid][1]
            if va < s:
                hi = mid - 1
            elif va >= e:
                lo = mid + 1
            else:
                return self.regions[mid]
            if s <= va:
                best = self.regions[mid]
        return None

    def mapped(self, va):
        return self._region_at(va) is not None

    def read(self, va, n):
        out = bytearray()
        cur = va
        while len(out) < n:
            r = self._region_at(cur)
            if r is None:
                break
            s, e, path = r[0], r[1], r[2]
            blob = self._blob(path)
            off = cur - s
            take = min(n - len(out), e - cur, len(blob) - off)
            if take <= 0:
                break
            out += blob[off:off + take]
            cur += take
        return bytes(out)

    # ---------- 标量 ----------
    def u8(self, va):
        b = self.read(va, 1)
        return b[0] if b else None

    def u16(self, va):
        b = self.read(va, 2)
        return struct.unpack("<H", b)[0] if len(b) == 2 else None

    def u32(self, va):
        b = self.read(va, 4)
        return struct.unpack("<I", b)[0] if len(b) == 4 else None

    def i32(self, va):
        b = self.read(va, 4)
        return struct.unpack("<i", b)[0] if len(b) == 4 else None

    def q(self, va):
        b = self.read(va, 8)
        return struct.unpack("<Q", b)[0] if len(b) == 8 else None

    def qs(self, va, n):
        b = self.read(va, n * 8)
        return list(struct.unpack("<%dQ" % (len(b) // 8), b))

    def cstr(self, va, maxlen=256):
        b = self.read(va, maxlen)
        z = b.find(b"\x00")
        if z >= 0:
            b = b[:z]
        try:
            return b.decode("utf-8")
        except UnicodeDecodeError:
            return b.decode("latin-1")

    def il2str(self, va, maxlen=512):
        """Il2CppString: len@+0x10 (i32), chars@+0x14 (utf16)"""
        if not va:
            return None
        n = self.i32(va + 0x10)
        if n is None or n < 0 or n > maxlen:
            return None
        raw = self.read(va + 0x14, n * 2)
        if len(raw) != n * 2:
            return None
        return raw.decode("utf-16-le", "replace")

    def rva(self, va):
        return va - UA_BASE


def main():
    d = Dump()
    print("regions", len(d.regions))
    print("UA image regions", sum(1 for r in d.regions if r[5] and "UserAssembly" in r[5]))
    print("IL2CPP_VER", d.q(UA_BASE + 0x2B0C2E8))
    print("MODULE_COUNT", d.q(UA_BASE + 0x25554E8))
    print("MODULE_TABLE", hex(d.q(UA_BASE + 0x25554F0) or 0))


if __name__ == "__main__":
    main()
