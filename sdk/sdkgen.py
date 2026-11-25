# -*- coding: utf-8 -*-
"""
sdkgen.py — 用解出来的元数据公式生成 Il2CppDumper 风格的 dump.cs。

已解出（全部在真机上抽样验证）：
  typeDefinitions : stride 0x52, base = body + (header[0x1C] ^ 0x3AC76B0F)
      +00 byrefTypeIndex(+0xBB19C377) +0C name(+0xF2244D08) +10 parentType(+0x8EA6581A)
      +18 flags(^0x41E26BAF) +20 namespace(^0x28F643CE) +24 fieldStart(^0x2B5AE409)
      +2C token(+0x9AA8B940) +30 methodStart(^0x58E32EE7) +34 byvalType(+0xBC20238F)
      +42 u16 fieldCount(-0x4E06) +4E u16 methodCount(-0x3277)
  methods         : stride 0x1E, base = body + (header[0xD4] ^ 0x2360832F)
      +00 returnTypeIndex (+0xE63F1DC0, ^key)  +04 declaringType(^key ^0x651439A0)
      +08 nameIndex (+0xB509F32B, ^key)        key = hash(globalIndex)
  fields          : stride 0x0C, base = body + (header[0x128] + 0xCA7D051D)
      +00 name (^K0 ^0x1DE059D1)  +04 token((raw+0x9F8CE24C)^K1+0x5B7A007D)
      +08 typeIndex((raw+0x99DB16AB)^K1)   K0=hash(fieldIndex), K1=K0^0x6C0D9DD8

输出：sdk/bh3_dump.cs、sdk/bh3_fields.tsv、sdk/bh3_methods_named.tsv
"""
import argparse
import csv
import os
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
import invsig

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_GAME = os.environ.get("BH3_GAME_DIR",
                       r"D:\Games\miHoYo Launcher\games\Honkai Impact 3rd Game")
DLL = os.environ.get("BH3_UA", os.path.join(_GAME, "BH3_Data", "Native", "UserAssembly.dll"))
META = os.environ.get("BH3_META", os.path.join(_GAME, "BH3_Data", "Managed", "Metadata", "global-metadata.dat"))
CLASSES = os.path.join(ROOT, "sdk", "sdk_classes.tsv")
METHODS = os.path.join(ROOT, "sdk", "sdk_methods.tsv")
LIVE_METHODS = r"D:\Games\miHoYo Launcher\games\Honkai Impact 3rd Game\bh3_methods.tsv"
REGIONS = r"D:\Cheat Table\CE Search\regions.csv"
OUT_CS = os.path.join(ROOT, "sdk", "bh3_dump.cs")
OUT_FT = os.path.join(ROOT, "sdk", "bh3_fields.tsv")
OUT_MT = os.path.join(ROOT, "sdk", "bh3_methods_named.tsv")

M32 = 0xFFFFFFFF
M64 = (1 << 64) - 1
IMAGE_BASE = 0x180000000

# header blob
HDR_RVA = 0x129AA48

# string decrypt
KS_SRC = 0x34A3D32F255870B0
KS_ADD = 0xFFC51DF184FB6EAE
KS_XOR = 0x574A806E26B56AD3
STR_D = 0x735E1E977F020CDD
HDR_STR_BLOB = 0x164
HDR_STR_KEY = 0x758BB6D9

# type definitions
HDR_TYPES = 0x1C
HDR_TYPES_KEY = 0x3AC76B0F
TYPE_STRIDE = 0x52
T_NAME_ADD = 0xF2244D08
T_PARENT_ADD = 0x8EA6581A
T_FLAGS_XOR = 0x41E26BAF
T_NS_XOR = 0x28F643CE
T_FIELDSTART_XOR = 0x2B5AE409
T_TOKEN_ADD = 0x9AA8B940
T_METHSTART_XOR = 0x58E32EE7
T_BYVAL_ADD = 0xBC20238F
T_FIELDCOUNT_SUB = 0x4E06
T_METHCOUNT_SUB = 0x3277

# methods
HDR_METHODS = 0xD4
HDR_METHODS_KEY = 0x2360832F
METH_STRIDE = 0x1E
RET_ADD = 0xE63F1DC0
DECL_XOR = 0x651439A0
MNAME_ADD = 0xB509F32B
MKEY_BASE = 0xBF9490F71BB2A5A

# fields
HDR_FIELDS = 0x128
HDR_FIELDS_ADD = 0xCA7D051D
FIELD_STRIDE = 12
FNAME_XOR = 0x1DE059D1
FTOK_ADD = 0x9F8CE24C
FTOK_TAIL = 0x5B7A007D
FTYPE_ADD = 0x99DB16AB
FKEY_XOR = 0x6C0D9DD8


def pe_sections(path):
    f = open(path, "rb")
    h = f.read(0x1000)
    e = struct.unpack_from("<I", h, 0x3C)[0]
    coff = e + 4
    nsec, = struct.unpack_from("<H", h, coff + 2)
    optsz, = struct.unpack_from("<H", h, coff + 16)
    opt = coff + 20
    sec = opt + optsz
    out = []
    for i in range(nsec):
        o = sec + i * 40
        name = h[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, va, rawsize, raw = struct.unpack_from("<IIII", h, o + 8)
        out.append((name, va, vsize, raw, rawsize))
    return out


def rva2off(sections, rva):
    for name, va, vsize, raw, rawsize in sections:
        if va <= rva < va + max(vsize, rawsize):
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


class Strings:
    def __init__(self, meta, body, blob_off):
        self.meta = meta
        self.body = body
        self.blob = blob_off
        self.cache = {}

    def get(self, idx):
        if idx in self.cache:
            return self.cache[idx]
        if idx in (0, M32):
            self.cache[idx] = ""
            return ""
        src = idx & 0xFFFFFF
        ln = idx >> 24
        if not (0 < ln < 0x100) or src + 8 > len(self.meta) - self.body - self.blob:
            self.cache[idx] = ""
            return ""
        k0 = ((src * KS_SRC + KS_ADD) & M64) ^ KS_XOR
        out = []
        for j in range((ln + 7) >> 3):
            ct = struct.unpack_from("<Q", self.meta, self.body + self.blob + src + j * 8)[0]
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


def load_regions(path):
    regs = []
    if not os.path.exists(path):
        return regs
    for r in csv.DictReader(open(path, encoding="utf-8", errors="replace")):
        try:
            b = int(r["base"], 16)
            sz = int(r["size"], 16)
            regs.append((b, b + sz, r["file"]))
        except Exception:
            pass
    regs.sort()
    return regs


_fh = {}


def read_mem(regs, addr, n):
    for b, e, f in regs:
        if b <= addr < e:
            fh = _fh.get(f)
            if fh is None:
                if not os.path.exists(f):
                    return None
                fh = open(f, "rb")
                _fh[f] = fh
            fh.seek(addr - b)
            return fh.read(min(n, e - addr))
    return None


CS_NAMES = {
    "Boolean": "bool", "Byte": "byte", "SByte": "sbyte", "Int16": "short",
    "UInt16": "ushort", "Int32": "int", "UInt32": "uint", "Int64": "long",
    "UInt64": "ulong", "Single": "float", "Double": "double", "Char": "char",
    "String": "string", "Object": "object", "Void": "void",
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", action="store_true", help="从 CE 转储补方法 flags")
    args = ap.parse_args()

    secs = pe_sections(DLL)
    hoff = rva2off(secs, HDR_RVA)
    dll = open(DLL, "rb").read()
    hdr = dll[hoff:hoff + 0x400]
    meta = open(META, "rb").read()
    body = 0x198

    def hd32(h):
        return struct.unpack_from("<I", hdr, h)[0]

    type_base = body + (((hd32(HDR_TYPES) ^ HDR_TYPES_KEY) & M32))
    meth_base = body + (((hd32(HDR_METHODS) ^ HDR_METHODS_KEY) & M32))
    field_base = body + (((hd32(HDR_FIELDS) + HDR_FIELDS_ADD) & M32))
    blob_off = (hd32(HDR_STR_BLOB) ^ HDR_STR_KEY) & M32
    strings = Strings(meta, body, blob_off)

    # ---- 类表（运行时真值）：tdi -> name/parent/flags/klass ----
    rt = {}
    klass_to_tdi = {}
    with open(CLASSES, encoding="utf-8", errors="replace") as f:
        for line in f:
            p = line.rstrip("\n").split("\t")
            if len(p) < 6 or not p[0].isdigit():
                continue
            tdi = int(p[0])
            try:
                kv = int(p[4], 0)
                pv = int(p[2], 0)
                fl = int(p[3], 0)
            except ValueError:
                kv = pv = fl = 0
            rt[tdi] = (p[1], pv, fl, kv)
            klass_to_tdi[kv & M32] = tdi

    # ---- 解码 typeDefinitions ----
    types = {}
    tdi_count = (len(meta) - type_base) // TYPE_STRIDE
    while tdi_count > 0:
        r = type_base + (tdi_count - 1) * TYPE_STRIDE
        if struct.unpack_from("<I", meta, r + 0x0C)[0] != 0 or struct.unpack_from("<I", meta, r + 0x20)[0] != 0:
            break
        tdi_count -= 1
    for tdi in range(tdi_count):
        r = type_base + tdi * TYPE_STRIDE
        if r + TYPE_STRIDE > len(meta):
            break
        name_i = (struct.unpack_from("<I", meta, r + 0x0C)[0] + T_NAME_ADD) & M32
        ns_i = (struct.unpack_from("<I", meta, r + 0x20)[0] ^ T_NS_XOR) & M32
        token = (struct.unpack_from("<I", meta, r + 0x2C)[0] + T_TOKEN_ADD) & M32
        flags = struct.unpack_from("<I", meta, r + 0x18)[0] ^ T_FLAGS_XOR
        byval = (struct.unpack_from("<I", meta, r + 0x34)[0] + T_BYVAL_ADD) & M32
        fstart = struct.unpack_from("<I", meta, r + 0x24)[0] ^ T_FIELDSTART_XOR
        fcount = (struct.unpack_from("<H", meta, r + 0x42)[0] - T_FIELDCOUNT_SUB) & 0xFFFF
        mstart = struct.unpack_from("<I", meta, r + 0x30)[0] ^ T_METHSTART_XOR
        mcount = (struct.unpack_from("<H", meta, r + 0x4E)[0] - T_METHCOUNT_SUB) & 0xFFFF
        types[tdi] = dict(
            name=strings.get(name_i), ns=strings.get(ns_i), token=token, flags=flags,
            byval=byval, fstart=fstart, fcount=fcount, mstart=mstart, mcount=mcount,
        )

    byval_to_tdi = {v["byval"]: t for t, v in types.items()}

    # ---- 静态 Il2CppType 表（UserAssembly .rdata RVA 0x78E320，步长 0x10）----
    TYPE_TABLE_RVA = 0x78E320
    ENUM_NAMES = {
        0x01: "void", 0x02: "bool", 0x03: "char", 0x04: "sbyte", 0x05: "byte",
        0x06: "short", 0x07: "ushort", 0x08: "int", 0x09: "uint", 0x0A: "long",
        0x0B: "ulong", 0x0C: "float", 0x0D: "double", 0x0E: "string", 0x16: "typedref",
        0x1C: "object",
    }

    def dll_read(rva, n):
        o = rva2off(secs, rva)
        if o is None or o + n > len(dll):
            return None
        return dll[o:o + n]

    def type_entry(ti):
        b = dll_read(TYPE_TABLE_RVA + ti * 16, 16)
        if not b:
            return None, 0
        return struct.unpack_from("<Q", b, 0)[0], struct.unpack_from("<Q", b, 8)[0]

    def resolve_type(ti, depth=0):
        if ti in (M32, 0xFFFFFFFF):
            return "void"
        if depth > 8:
            return "object"
        data, bits = type_entry(ti)
        enum = (bits >> 16) & 0xFF
        if enum in ENUM_NAMES and enum not in (0x11, 0x12):
            return ENUM_NAMES[enum]
        if enum in (0x11, 0x12, 0x1C):
            v = types.get(data)
            if v and v["name"]:
                nm = v["name"]
                if nm in CS_NAMES:
                    return CS_NAMES[nm]
                return (v["ns"] + "." + nm) if v["ns"] else nm
            tdi = byval_to_tdi.get(ti)
            return full_name(tdi) if tdi is not None else "object"
        if enum in (0x1D, 0x14, 0x0F, 0x10):
            # data 是元素类型指针（ImageBase 相对）
            ptr = data - IMAGE_BASE if data >= IMAGE_BASE else data
            eti = (ptr - TYPE_TABLE_RVA) // 16 if ptr >= TYPE_TABLE_RVA else -1
            elem = resolve_type(eti, depth + 1) if eti >= 0 else "object"
            if enum == 0x1D:
                return elem + "[]"
            if enum == 0x0F:
                return elem + "*"
            if enum == 0x10:
                return "ref " + elem
            return elem + "[,]"
        if enum in (0x13, 0x1E):
            return "T"
        return "object /*ti 0x%X*/" % ti

    def type_name(ti):
        s = resolve_type(ti)
        return s

    def full_name(tdi):
        v = types.get(tdi)
        if not v:
            return rt.get(tdi, ("?", 0, 0, 0))[0]
        return (v["ns"] + "." + v["name"]) if v["ns"] else v["name"]

    # ---- 解码字段 ----
    fields = {}
    for tdi, v in types.items():
        fs, fc = v["fstart"], v["fcount"]
        if fs == M32 or fc == 0 or fc > 0x800:
            v["fields"] = []
            continue
        arr = []
        for k in range(fc):
            i = fs + k
            r = field_base + i * FIELD_STRIDE
            if r + FIELD_STRIDE > len(meta):
                break
            k0 = field_key(i)
            k1 = k0 ^ FKEY_XOR
            ni = ((struct.unpack_from("<I", meta, r)[0] ^ k0) ^ FNAME_XOR) & M32
            tok = ((struct.unpack_from("<I", meta, r + 4)[0] + FTOK_ADD) & M32) ^ k1
            ti = ((struct.unpack_from("<I", meta, r + 8)[0] + FTYPE_ADD) & M32) ^ k1
            arr.append((strings.get(ni), tok, ti))
        v["fields"] = arr

    # ---- 方法表：优先用注入 dumper 的产物，其次离线表 ----
    rows = []
    for path in ([LIVE_METHODS] if os.path.exists(LIVE_METHODS) else []) + [METHODS]:
        with open(path, encoding="utf-8", errors="replace") as f:
            first = True
            for line in f:
                if first:
                    first = False
                    continue
                p = line.rstrip("\n").split("\t")
                if len(p) < 6:
                    continue
                try:
                    idx = int(p[1])
                    mp = int(p[2], 0)
                    inv = int(p[3], 0)
                    tok = int(p[4], 0)
                    miv = int(p[5], 0)
                except ValueError:
                    continue
                rows.append((idx, mp, inv, tok, miv, p[0]))
    max_idx = max(r[0] for r in rows) if rows else 0
    for v in types.values():
        if v["mstart"] != M32 and v["mcount"] < 0x2000:
            max_idx = max(max_idx, v["mstart"] + v["mcount"])

    regs = load_regions(REGIONS) if args.dump else []
    md_ = Cs(CS_ARCH_X86, CS_MODE_64)
    sig_cache = {}

    def param_sig(inv):
        if not inv:
            return None
        if inv in sig_cache:
            return sig_cache[inv]
        s = invsig.invoke_sig(dll, secs, inv, md_)
        sig_cache[inv] = s
        return s
    methods = {}
    for idx in range(max_idx + 1):
        r = meth_base + idx * METH_STRIDE
        if r + METH_STRIDE > len(meta):
            break
        k = method_key(idx) & M32
        ret = ((struct.unpack_from("<I", meta, r)[0] + RET_ADD) & M32) ^ k
        decl = ((struct.unpack_from("<I", meta, r + 4)[0] ^ k) ^ 0x651439A0) & M32
        ni = ((struct.unpack_from("<I", meta, r + 8)[0] + MNAME_ADD) & M32) ^ k
        methods[idx] = (decl, strings.get(ni), ret)

    # ---- 方法 tsv 行按 idx 归并（token、RVA 都要合法，且类名与元数据一致）----
    by_idx = {}
    for idx, mp, inv, tok, miv, cname in rows:
        if idx in by_idx:
            continue
        if tok == 0 or (tok & 0xFF000000) != 0x06000000:
            continue
        if mp and not (0x1000 <= mp < 0x12850000):
            continue
        rec = methods.get(idx)
        if not rec:
            continue
        dv = types.get(rec[0])
        if not dv or not dv["name"] or dv["name"] != cname:
            continue
        by_idx[idx] = (mp, inv, tok, miv)

    # flags：从 CE 转储读 MethodInfo+0x38
    def read_flags(miv):
        if not regs or not miv:
            return 0
        b = read_mem(regs, miv + 0x38, 4)
        return struct.unpack_from("<I", b, 0)[0] if b else 0

    # ---- 写 dump.cs ----
    os.makedirs(os.path.dirname(OUT_CS), exist_ok=True)
    nf = nm = 0
    ft = open(OUT_FT, "w", encoding="utf-8")
    mt = open(OUT_MT, "w", encoding="utf-8")
    ft.write("tdi\tclass\tnamespace\tfield\ttype_tdi\ttype_name\ttoken\n")
    mt.write("idx\ttdi\tclass\tmethod\targs\ttoken\trva\tva\tflags\n")
    out = open(OUT_CS, "w", encoding="utf-8")
    out.write("// BH3 IL2CPP SDK dump (offline, MHY obfuscated metadata decoded)\n")
    out.write("// ImageBase: 0x%X   RVA->VA: 0x%X + RVA\n" % (IMAGE_BASE, IMAGE_BASE))
    out.write("// NOTE: 参数类型由 invoker thunk 反推（int/float/double/object/byte/short），参数名与字段偏移仍未解出。\n\n")

    for tdi in range(tdi_count):
        v = types.get(tdi)
        if not v:
            break
        name = v["name"]
        if not name:
            name = "TD_%d" % tdi
        ns = v["ns"]
        parent = None
        pv = rt.get(tdi, (None, 0, 0, 0))[1]
        if pv and (pv & M32):
            ptdi = klass_to_tdi.get(pv & M32)
            if ptdi is not None and ptdi != tdi and types.get(ptdi, {}).get("name"):
                parent = full_name(ptdi)
        out.write("// Namespace: %s\n" % ns)
        if parent:
            out.write("public class %s : %s // TypeDefIndex: %d, Token: 0x%08X, Flags: 0x%X\n"
                      % (name, parent, tdi, v["token"], v["flags"]))
        else:
            out.write("public class %s // TypeDefIndex: %d, Token: 0x%08X, Flags: 0x%X\n"
                      % (name, tdi, v["token"], v["flags"]))
        out.write("{\n")
        if v["fields"]:
            out.write("    // Fields\n")
            for fname, tok, ti in v["fields"]:
                tn = type_name(ti) or ("object /*type 0x%X*/" % ti)
                fname = fname or "field_0x%X" % tok
                out.write("    public %s %s; // Token: 0x%08X, TypeIndex: 0x%X\n"
                          % (tn, fname, tok, ti))
                ft.write("%d\t%s\t%s\t%s\t%s\t%s\t0x%08X\n"
                         % (tdi, name, ns, fname, hex(byval_to_tdi.get(ti, M32)),
                            tn, tok))
                nf += 1
        # methods of this class: 用 mstart/mcount
        ms, mc = v["mstart"], v["mcount"]
        ml = []
        if ms != M32 and mc and mc < 0x2000:
            for k in range(mc):
                idx = ms + k
                rec = methods.get(idx)
                if not rec:
                    continue
                decl, mname, ret = rec
                if decl != tdi:
                    continue
                mp, inv, tok, miv = by_idx.get(idx, (0, 0, 0, 0))
                fl = read_flags(miv)
                ml.append((mname, ret, tok, mp, fl, idx, inv))
        if ml:
            out.write("\n    // Methods\n")
            for mname, ret, tok, mp, fl, idx, inv in ml:
                rt_name = type_name(ret) or "object"
                sig = param_sig(inv)
                args = "..." if sig is None else ", ".join(sig)
                mods = "public " if (fl & 0x7) == 0x6 else ("private " if (fl & 0x7) == 0x1 else "")
                if fl & 0x10:
                    mods += "static "
                out.write("    %s%s %s(%s); // RVA: 0x%X VA: 0x%X Token: 0x%08X Flags: 0x%X Index: %d\n"
                          % (mods, rt_name, mname or "method_%d" % idx, args, mp, IMAGE_BASE + mp, tok, fl, idx))
                mt.write("%d\t%d\t%s\t%s\t%s\t0x%08X\t0x%X\t0x%X\t0x%X\n"
                         % (idx, tdi, name, mname, args, tok, mp, IMAGE_BASE + mp, fl))
                nm += 1
        out.write("}\n\n")
    out.close()
    ft.close()
    mt.close()
    print("classes %d  fields %d  methods %d" % (tdi_count, nf, nm))


if __name__ == "__main__":
    main()
