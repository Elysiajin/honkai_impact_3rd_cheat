# -*- coding: utf-8 -*-
"""离线合并：元数据 + 托管堆快照 + 反射签名 -> 完整 SDK 表。"""
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DLL = r"D:\Games\miHoYo Launcher\games\Honkai Impact 3rd Game\BH3_Data\Native\UserAssembly.dll"
META = r"D:\Games\miHoYo Launcher\games\Honkai Impact 3rd Game\BH3_Data\Managed\Metadata\global-metadata.dat"
POOL = os.path.join(ROOT, "mcp_file", "pool")
REFLECT = os.path.join(ROOT, "mcp_file", "reflect.tsv")
OUT_TSV = os.path.join(ROOT, "sdk", "bh3_methods_full.tsv")
OUT_CS = os.path.join(ROOT, "sdk", "bh3_dump_full.cs")
UA_BASE = 0x7FFCC5540000
POOL_LO = 0x50000001000

M32 = 0xFFFFFFFF
M64 = (1 << 64) - 1
IMAGE_BASE = 0x180000000
HDR_RVA = 0x129AA48
KS_SRC = 0x34A3D32F255870B0
KS_ADD = 0xFFC51DF184FB6EAE
KS_XOR = 0x574A806E26B56AD3
STR_D = 0x735E1E977F020CDD
HDR_STR_BLOB = 0x164
HDR_STR_KEY = 0x758BB6D9
HDR_TYPES = 0x1C
HDR_TYPES_KEY = 0x3AC76B0F
TYPE_STRIDE = 0x52
T_NAME_ADD = 0xF2244D08
T_NS_XOR = 0x28F643CE
T_FIELDSTART_XOR = 0x2B5AE409
T_TOKEN_ADD = 0x9AA8B940
T_METHSTART_XOR = 0x58E32EE7
T_BYVAL_ADD = 0xBC20238F
T_FIELDCOUNT_SUB = 0x4E06
T_METHCOUNT_SUB = 0x3277
HDR_METHODS = 0xD4
HDR_METHODS_KEY = 0x2360832F
METH_STRIDE = 0x1E
RET_ADD = 0xE63F1DC0
DECL_XOR = 0x651439A0
MNAME_ADD = 0xB509F32B
MTOK_ADD = 0xF3C23881
MFLAG_SUB = 0x6255
MKEY_BASE = 0xBF9490F71BB2A5A
HDR_FIELDS = 0x128
HDR_FIELDS_ADD = 0xCA7D051D
FIELD_STRIDE = 12
FNAME_XOR = 0x1DE059D1
FTOK_ADD = 0x9F8CE24C
FTOK_TAIL = 0x5B7A007D
FTYPE_ADD = 0x99DB16AB
FKEY_XOR = 0x6C0D9DD8
TYPE_TABLE_RVA = 0x78E320

CS_NAMES = {
    "Boolean": "bool", "Byte": "byte", "SByte": "sbyte", "Int16": "short",
    "UInt16": "ushort", "Int32": "int", "UInt32": "uint", "Int64": "long",
    "UInt64": "ulong", "Single": "float", "Double": "double", "Char": "char",
    "String": "string", "Object": "object", "Void": "void",
}
REFL_NAMES = {
    "System.Boolean": "bool", "System.Byte": "byte", "System.SByte": "sbyte",
    "System.Int16": "short", "System.UInt16": "ushort", "System.Int32": "int",
    "System.UInt32": "uint", "System.Int64": "long", "System.UInt64": "ulong",
    "System.Single": "float", "System.Double": "double", "System.Char": "char",
    "System.String": "string", "System.Object": "object", "System.Void": "void",
}


def pe_sections(path):
    with open(path, "rb") as f:
        h = f.read(0x1000)
    e = struct.unpack_from("<I", h, 0x3C)[0]
    coff = e + 4
    nsec = struct.unpack_from("<H", h, coff + 2)[0]
    optsz = struct.unpack_from("<H", h, coff + 16)[0]
    sec = coff + 20 + optsz
    out = []
    for i in range(nsec):
        o = sec + i * 40
        vsize, va, rawsize, raw = struct.unpack_from("<IIII", h, o + 8)
        out.append((va, max(vsize, rawsize), raw))
    return out


def rva2off(sections, rva):
    for va, sz, raw in sections:
        if va <= rva < va + sz:
            return raw + (rva - va)
    return None


def method_key(i):
    x = (i * 0xDB9B) & M64
    x ^= 0x1A58D018
    x = (x * 0x1339AE8B) & M64
    x ^= 0x1FFFA93E
    x = (x * 0x27F47E6A) & M64
    x = (x + MKEY_BASE) & M64
    return x >> 0x14


def field_key(i):
    x = (i * 0x6947 + 0x3D6595AB) & M64
    x ^= 0x211C466E
    x = (x * 0x1D02A562) & M64
    x >>= 0x11
    x = (x * 0x3BB4F156) & M64
    x >>= 0x17
    return x & M32


class Meta:
    def __init__(self):
        self.secs = pe_sections(DLL)
        self.dll = open(DLL, "rb").read()
        off = rva2off(self.secs, HDR_RVA)
        self.hdr = self.dll[off:off + 0x400]
        self.meta = open(META, "rb").read()
        self.body = 0x198
        self.cache = {}

    def hd32(self, o):
        return struct.unpack_from("<I", self.hdr, o)[0]

    def string(self, idx):
        if idx in self.cache:
            return self.cache[idx]
        if idx in (0, M32):
            return ""
        src = idx & 0xFFFFFF
        ln = idx >> 24
        base = self.body + ((self.hd32(HDR_STR_BLOB) ^ HDR_STR_KEY) & M32) + src
        if not (0 < ln < 0x100) or base + 8 > len(self.meta):
            self.cache[idx] = ""
            return ""
        k0 = ((src * KS_SRC + KS_ADD) & M64) ^ KS_XOR
        out = []
        for j in range((ln + 7) >> 3):
            ct = struct.unpack_from("<Q", self.meta, base + j * 8)[0]
            out.append(ct ^ ((k0 + j * STR_D) & M64))
        raw = b"".join(struct.pack("<Q", v) for v in out)[:ln]
        try:
            s = raw.decode("utf-8")
        except UnicodeDecodeError:
            s = raw.decode("latin-1")
        if not all(32 <= ord(c) < 127 for c in s):
            s = ""
        self.cache[idx] = s
        return s

    def type_entry(self, ti):
        o = rva2off(self.secs, TYPE_TABLE_RVA + ti * 16)
        if o is None or o + 16 > len(self.dll):
            return None, 0
        return struct.unpack_from("<QQ", self.dll, o)

    def tname(self, ti, depth=0):
        if ti in (M32, 0xFFFFFFFF):
            return "void"
        if depth > 8:
            return "object"
        data, bits = self.type_entry(ti)
        enum = (bits >> 16) & 0xFF
        if enum in (0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D):
            return {1: "void", 2: "bool", 3: "char", 4: "sbyte", 5: "byte", 6: "short",
                    7: "ushort", 8: "int", 9: "uint", 10: "long", 11: "ulong",
                    12: "float", 13: "double"}[enum]
        if enum == 0x0E:
            return "string"
        if enum == 0x1C:
            return "object"
        if enum in (0x11, 0x12):
            v = self.types.get(data)
            if v and v["name"]:
                nm = v["name"]
                return CS_NAMES.get(nm, (v["ns"] + "." + nm) if v["ns"] else nm)
            return "object"
        if enum in (0x1D, 0x14, 0x0F, 0x10):
            ptr = data - IMAGE_BASE if data >= IMAGE_BASE else data
            eti = (ptr - TYPE_TABLE_RVA) // 16 if ptr >= TYPE_TABLE_RVA else -1
            elem = self.tname(eti, depth + 1) if eti >= 0 else "object"
            return {0x1D: elem + "[]", 0x14: elem + "[,]", 0x0F: elem + "*",
                    0x10: "ref " + elem}[enum]
        return "object"


def name_short(full):
    return full.rsplit(".", 1)[-1].rsplit("+", 1)[-1]


def data_section(dll):
    h = dll[:0x1000]
    e = struct.unpack_from("<I", h, 0x3C)[0]
    c = e + 4
    nsec = struct.unpack_from("<H", h, c + 2)[0]
    osz = struct.unpack_from("<H", h, c + 16)[0]
    sec = c + 20 + osz
    for i in range(nsec):
        o = sec + i * 40
        nm = h[o:o + 8].rstrip(b"\0").decode()
        vs, va, rs, raw = struct.unpack_from("<IIII", h, o + 8)
        if nm == ".data":
            return raw, raw + rs
    return 0, 0


def solve_module_tables(dll, known):
    dlo, dhi = data_section(dll)
    pos = {}
    i = dlo
    while i + 8 <= dhi:
        v = struct.unpack_from("<Q", dll, i)[0] - 0x180000000
        if 0x1000 <= v < 0x12673000:
            pos.setdefault(v, []).append(i)
        i += 8
    votes = {}
    for m, rid, rva in known:
        if m is None or not rva:
            continue
        c = votes.setdefault(m, {})
        for off in pos.get(rva, ()):
            b = off - (rid - 1) * 8
            if b >= dlo:
                c[b] = c.get(b, 0) + 1
    bases = {}
    for m, c in votes.items():
        cands = sorted(c.items(), key=lambda kv: -kv[1])[:6]
        best, best_n = 0, -1
        for b, _ in cands:
            n = 0
            for mm, rid, rva in known:
                if mm != m:
                    continue
                o = b + (rid - 1) * 8
                if dlo <= o < dhi and struct.unpack_from("<Q", dll, o)[0] - 0x180000000 == rva:
                    n += 1
            if n > best_n:
                best, best_n = b, n
        if best_n > 0:
            bases[m] = best
    return bases, dlo, dhi


def load_pool():
    files = sorted(f for f in os.listdir(POOL) if f.endswith(".bin"))
    buf = b"".join(open(os.path.join(POOL, f), "rb").read() for f in files)
    return buf, POOL_LO, POOL_LO + len(buf)


def parse_classes(buf, lo, hi):
    n8 = len(buf) // 8
    qs = struct.unpack_from("<%dQ" % n8, buf, 0)

    def q(a):
        o = a - lo
        return struct.unpack_from("<Q", buf, o)[0] if 0 <= o and o + 8 <= len(buf) else 0

    def i32(a):
        o = a - lo
        return struct.unpack_from("<i", buf, o)[0] if 0 <= o and o + 4 <= len(buf) else 0

    def cstr(a, cap=96):
        o = a - lo
        if o < 0 or o >= len(buf):
            return ""
        e = buf.find(b"\0", o, min(len(buf), o + cap))
        if e < 0:
            e = min(len(buf), o + cap)
        try:
            return buf[o:e].decode("ascii")
        except UnicodeDecodeError:
            return ""

    def el_in(c):
        return lo <= c < hi and (c & 7) == 0 and c + 0x98 <= hi

    runs = []
    in_run = False
    run_at = 0
    prev = False
    c0 = qs[0]
    in0 = el_in(c0)
    t0 = i32(c0 + 0x88) if in0 else 0
    for i in range(n8 - 1):
        c1 = qs[i + 1]
        in1 = el_in(c1)
        t1 = i32(c1 + 0x88) if in1 else 0
        link = in0 and in1 and t1 == t0 + 1
        linked = in0 and (prev or link)
        if linked:
            if not in_run:
                run_at = i
                in_run = True
        elif in_run:
            runs.append((run_at, i - run_at))
            in_run = False
        prev = link
        in0 = in1
        t0 = t1
    if in_run:
        runs.append((run_at, n8 - run_at))

    klasses = []
    for at, n in runs:
        if n < 64:
            continue
        named = sum(1 for k in range(n) if cstr(q(qs[at + k] + 0x20), 8))
        if 2 * named < n:
            continue
        for k in range(n):
            klasses.append(qs[at + k])

    methods = {}
    mods = [q(c + 0x58) for c in klasses]
    mods = [m for m in mods if lo <= m < hi]
    modtab = min(mods) if mods else 0
    klass_meta = {}
    for c in klasses:
        o = c - lo
        tdi = struct.unpack_from("<i", buf, o + 0x88)[0]
        mp = q(c + 0x58)
        m = None
        if modtab and mp >= modtab and (mp - modtab) % 0x58 == 0:
            m = (mp - modtab) // 0x58
        klass_meta[c] = (tdi, m)
        tab = q(c + 0x80)
        if not (lo <= tab < hi):
            continue
        for i in range(4096):
            p = q(tab + i * 8)
            if not p or not (lo <= p < hi):
                break
            mi = p - 8
            if q(mi + 0x10) != c:
                continue
            tok = struct.unpack_from("<I", buf, mi - lo + 0x30)[0]
            if (tok >> 24) != 6:
                continue
            mpg = q(mi + 8)
            methods[mi] = (c, tok, mpg - UA_BASE if mpg else 0,
                           struct.unpack_from("<I", buf, mi - lo + 0x18)[0],
                           struct.unpack_from("<I", buf, mi - lo + 0x38)[0], m)
    return klasses, methods, klass_meta


def load_reflect():
    by_mi = {}
    by_name = {}
    if not os.path.exists(REFLECT):
        return by_mi, by_name
    with open(REFLECT, encoding="utf-8", errors="replace") as f:
        for line in f:
            p = line.rstrip("\n").split("\t")
            if len(p) < 6:
                continue
            ret = p[3]
            params = [x for x in p[4].split(";") if x]
            try:
                tok = int(p[0])
            except ValueError:
                continue
            decl = p[1]
            by_name[(decl, tok)] = (ret, params)
            hv = p[5].split(":")[0].strip()
            if hv.isdigit():
                mi = int(hv) - 8
                if mi > 0:
                    by_mi[mi] = (ret, params)
    return by_mi, by_name


def main():
    meta = Meta()
    hdr = meta.hdr
    hd32 = meta.hd32
    type_base = meta.body + ((hd32(HDR_TYPES) ^ HDR_TYPES_KEY) & M32)
    meth_base = meta.body + ((hd32(HDR_METHODS) ^ HDR_METHODS_KEY) & M32)
    field_base = meta.body + ((hd32(HDR_FIELDS) + HDR_FIELDS_ADD) & M32)
    count = (len(meta.meta) - type_base) // TYPE_STRIDE

    meta.types = {}
    tdi_count = count
    while tdi_count > 0:
        r = type_base + (tdi_count - 1) * TYPE_STRIDE
        if struct.unpack_from("<I", meta.meta, r + 0x0C)[0] or struct.unpack_from("<I", meta.meta, r + 0x20)[0]:
            break
        tdi_count -= 1

    max_idx = 0
    for tdi in range(tdi_count):
        r = type_base + tdi * TYPE_STRIDE
        name = meta.string((struct.unpack_from("<I", meta.meta, r + 0x0C)[0] + T_NAME_ADD) & M32)
        ns = meta.string(struct.unpack_from("<I", meta.meta, r + 0x20)[0] ^ T_NS_XOR)
        flags = struct.unpack_from("<I", meta.meta, r + 0x18)[0] ^ 0x41E26BAF
        fstart = struct.unpack_from("<I", meta.meta, r + 0x24)[0] ^ T_FIELDSTART_XOR
        fcount = (struct.unpack_from("<H", meta.meta, r + 0x42)[0] - T_FIELDCOUNT_SUB) & 0xFFFF
        mstart = struct.unpack_from("<I", meta.meta, r + 0x30)[0] ^ T_METHSTART_XOR
        mcount = (struct.unpack_from("<H", meta.meta, r + 0x4E)[0] - T_METHCOUNT_SUB) & 0xFFFF
        token = (struct.unpack_from("<I", meta.meta, r + 0x2C)[0] + T_TOKEN_ADD) & M32
        meta.types[tdi] = dict(name=name, ns=ns, flags=flags, fstart=fstart, fcount=fcount,
                              mstart=mstart, mcount=mcount, token=token)
        if mstart != M32 and mcount < 0x2000:
            max_idx = max(max_idx, mstart + mcount)

    for tdi, v in meta.types.items():
        fs, fc = v["fstart"], v["fcount"]
        fields = []
        if fs != M32 and 0 < fc < 0x800:
            for k in range(fc):
                i = fs + k
                r = field_base + i * FIELD_STRIDE
                if r + FIELD_STRIDE > len(meta.meta):
                    break
                k0 = field_key(i)
                k1 = k0 ^ FKEY_XOR
                ni = ((struct.unpack_from("<I", meta.meta, r)[0] ^ k0) ^ FNAME_XOR) & M32
                ftype = ((struct.unpack_from("<I", meta.meta, r + 8)[0] + FTYPE_ADD) & M32) ^ k1
                ftok = ((struct.unpack_from("<I", meta.meta, r + 4)[0] + FTOK_ADD) & M32) ^ k1 ^ FTOK_TAIL
                fields.append((meta.string(ni), ftype, ftok))
        v["fields"] = fields

    methods = {}
    for i in range(max_idx):
        r = meth_base + i * METH_STRIDE
        if r + METH_STRIDE > len(meta.meta):
            break
        k = method_key(i) & M32
        decl = ((struct.unpack_from("<I", meta.meta, r + 4)[0] ^ k) ^ DECL_XOR) & M32
        name = meta.string(((struct.unpack_from("<I", meta.meta, r + 8)[0] + MNAME_ADD) & M32) ^ k)
        ret = ((struct.unpack_from("<I", meta.meta, r)[0] + RET_ADD) & M32) ^ k
        tok = ((struct.unpack_from("<I", meta.meta, r + 0x10)[0] + MTOK_ADD) & M32) ^ k
        flags = ((struct.unpack_from("<H", meta.meta, r + 0x16)[0] - MFLAG_SUB) & 0xFFFF) ^ (k & 0xFFFF)
        methods[i] = (decl, name, ret, tok, flags)

    buf, lo, hi = load_pool()
    klasses, rt_methods, klass_meta = parse_classes(buf, lo, hi)
    tdi_of = {}
    mod_of_tdi = {}
    for c, (tdi, m) in klass_meta.items():
        if tdi < 0:
            continue
        tdi_of[c] = tdi
        mod_of_tdi.setdefault(tdi, m)

    rva_by_key = {}
    mi_by_key = {}
    known = []
    for mi, (c, tok, rva, slot, flags, m) in rt_methods.items():
        tdi = tdi_of.get(c)
        if tdi is None or tdi < 0:
            continue
        rva_by_key[(tdi, tok)] = rva
        mi_by_key[(tdi, tok)] = mi
        known.append((m, tok & 0xFFFFFF, rva))

    bases, dlo, dhi = solve_module_tables(meta.dll, known)
    print("module tables solved %d/%d" % (len(bases), len(set(m for _, _, _ in known if m is not None))))

    def table_rva(tdi, tok):
        m = mod_of_tdi.get(tdi)
        if m is None or m not in bases:
            return 0
        o = bases[m] + ((tok & 0xFFFFFF) - 1) * 8
        if not (dlo <= o < dhi):
            return 0
        v = struct.unpack_from("<Q", meta.dll, o)[0] - 0x180000000
        return v if 0x1000 <= v < 0x12673000 else 0

    reflect, reflect_name = load_reflect()

    def reflect_args(mi, full, tok):
        v = reflect.get(mi)
        if not v:
            v = reflect_name.get((full, tok))
        if not v:
            return None
        ret, params = v
        out = []
        for s in params:
            parts = s.split(" ", 1)
            tn = parts[0]
            pn = parts[1] if len(parts) > 1 else ""
            tn = REFL_NAMES.get(tn, tn)
            out.append((tn + " " + pn).strip())
        return out

    os.makedirs(os.path.dirname(OUT_TSV), exist_ok=True)
    tsv = open(OUT_TSV, "w", encoding="utf-8")
    tsv.write("idx\ttdi\tclass\tmethod\tret\targs\ttoken\trva\tflags\tmethodinfo\n")
    cs = open(OUT_CS, "w", encoding="utf-8")
    cs.write("// BH3 SDK full dump (offline: metadata + heap snapshot + reflection)\n")
    cs.write("// ImageBase: 0x%X\n\n" % IMAGE_BASE)
    n_all = n_rva = n_args = 0
    for tdi in range(tdi_count):
        v = meta.types.get(tdi)
        if not v:
            break
        name = v["name"] or ("TD_%d" % tdi)
        ns = v["ns"]
        full = (ns + "." + name) if ns else name
        cs.write("// Namespace: %s\n" % ns)
        cs.write("public class %s // TypeDefIndex: %d, Token: 0x%08X, Flags: 0x%X\n{\n"
                 % (name, tdi, v["token"], v["flags"]))
        if v["fields"]:
            cs.write("    // Fields\n")
            for fname, ftype, ftok in v["fields"]:
                cs.write("    public %s %s; // Token: 0x%08X\n"
                         % (meta.tname(ftype), fname or ("field_0x%X" % ftok), ftok))
        ms, mc = v["mstart"], v["mcount"]
        if ms != M32 and 0 < mc < 0x2000:
            cs.write("\n    // Methods\n")
            for k in range(mc):
                i = ms + k
                rec = methods.get(i)
                if not rec or rec[0] != tdi:
                    continue
                decl, mname, ret, tok, flags = rec
                rva = table_rva(tdi, tok) or rva_by_key.get((tdi, tok), 0)
                mi = mi_by_key.get((tdi, tok), 0)
                args = reflect_args(mi, full, tok)
                argtxt = ",".join(args) if args is not None else "..."
                rt = meta.tname(ret)
                mods = "public " if (flags & 0x7) == 0x6 else (
                    "private " if (flags & 0x7) == 0x1 else (
                        "protected " if (flags & 0x7) == 0x4 else ""))
                if flags & 0x10:
                    mods += "static "
                if flags & 0x400:
                    mods = "abstract " + mods
                cs.write("    %s%s %s(%s); // RVA: 0x%X VA: 0x%X Token: 0x%08X Flags: 0x%X Index: %d\n"
                         % (mods, rt, mname or ("method_%d" % i), argtxt, rva,
                            IMAGE_BASE + rva, tok, flags, i))
                tsv.write("%d\t%d\t%s\t%s\t%s\t%s\t0x%08X\t0x%X\t0x%X\t0x%X\n"
                          % (i, tdi, full, mname, rt, argtxt, tok, rva, flags, mi))
                n_all += 1
                if rva:
                    n_rva += 1
                if args is not None:
                    n_args += 1
        cs.write("}\n\n")
    cs.close()
    tsv.close()
    print("classes %d methods %d rva %d (%.1f%%) args %d (%.1f%%)"
          % (tdi_count, n_all, n_rva, 100.0 * n_rva / max(1, n_all),
             n_args, 100.0 * n_args / max(1, n_all)))
    print("snapshot classes", len(klasses), "runtime methodinfo", len(rt_methods),
          "reflect", len(reflect))


if __name__ == "__main__":
    main()
