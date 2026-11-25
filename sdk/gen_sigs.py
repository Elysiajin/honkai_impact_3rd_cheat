import csv
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from capstone import CS_ARCH_X86, CS_MODE_64, Cs

from dump import Dump, UA_BASE

PROFILE_RE = re.compile(
    r'\{\s*"([a-z0-9_]+)"\s*,\s*0x([0-9A-Fa-f]+)u?')
ENUM_RE = re.compile(r'^\s*Sym([A-Za-z0-9]+)\s*=\s*(\d+)\s*,', re.M)
SIG_KEYS = ["is_sp_enough", "get_skill_cd", "is_skill_has_cd"]


def camel(key):
    return "".join(p[:1].upper() + p[1:] for p in key.split("_"))


def profile_data():
    txt = open(os.path.join(ROOT, "overlay", "bh3_profile.inc"),
               encoding="utf-8", errors="replace").read()
    rvas = {m.group(1): int(m.group(2), 16) for m in PROFILE_RE.finditer(txt)}
    enums = {m.group(1): int(m.group(2)) for m in ENUM_RE.finditer(txt)}
    return rvas, enums


def build_sigs():
    rvas, enums = profile_data()
    out = []
    for key in SIG_KEYS:
        rva = rvas.get(key, 0)
        sym = enums.get(camel(key))
        if not rva or sym is None:
            raise SystemExit("profile 缺少签名符号 %s，先跑 gen_profile.py" % key)
        out.append((key, sym, rva))
    return out

TAKE = 32


UA_PATH = os.environ.get("BH3_UA", r"D:\Games\miHoYo Launcher\games\Honkai Impact 3rd Game\BH3_Data\Native\UserAssembly.dll")


def rva_to_off(path, rva):
    import struct
    with open(path, "rb") as f:
        h = f.read(0x1000)
    e = struct.unpack_from("<I", h, 0x3C)[0]
    nsec = struct.unpack_from("<H", h, e + 6)[0]
    optsz = struct.unpack_from("<H", h, e + 20)[0]
    sec = e + 24 + optsz
    for i in range(nsec):
        o = sec + i * 40
        vs, va, rs, ro = struct.unpack_from("<IIII", h, o + 8)
        if va <= rva < va + max(vs, rs):
            return ro + (rva - va)
    return None


def read_ua(rva, n):
    off = rva_to_off(UA_PATH, rva)
    if off is None:
        return b"\x00" * n
    with open(UA_PATH, "rb") as f:
        f.seek(off)
        return f.read(n)


def mask_pattern(d, rva):
    raw = bytearray(read_ua(rva, TAKE))
    mask = bytearray(b"\x01" * TAKE)
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    code = bytes(raw)
    for ins in md.disasm(code, UA_BASE + rva):
        off = ins.address - (UA_BASE + rva)
        if off >= TAKE:
            break
        for op in ins.operands:
            if op.type == 3 and op.mem.base == 41:
                start = ins.address + ins.size - 4
                s = start - (UA_BASE + rva)
                if 0 <= s and s + 4 <= TAKE:
                    for i in range(s, s + 4):
                        mask[i] = 0
            if op.type == 2 and op.size == 4:
                start = ins.address + ins.size - 4
                s = start - (UA_BASE + rva)
                if 0 <= s and s + 4 <= TAKE:
                    for i in range(s, s + 4):
                        mask[i] = 0
    return bytes(raw), bytes(mask)


def count_hits(d, raw, mask, want_va):
    n = len(raw)
    pat = [(i, raw[i]) for i in range(n) if mask[i]]
    hits = []
    for region in d.regions:
        start, end, fname, prot, typ, path = region
        if end - start < n:
            continue
        blob = d._blob(fname)
        if not blob:
            continue
        limit = min(len(blob), end - start)
        pos = 0
        first = next((o for o, _ in pat), None)
        if first is None:
            continue
        key = raw[first]
        while True:
            i = blob.find(bytes([key]), pos)
            if i < 0 or i + n > limit:
                break
            pos = i + 1
            ok = True
            for o, v in pat:
                if blob[i + o] != v:
                    ok = False
                    break
            if ok:
                hits.append(start + i)
    return hits


def main():
    sigs = build_sigs()
    try:
        d = Dump()
    except FileNotFoundError:
        d = None
    lines = []
    lines.append("namespace ovp")
    lines.append("{")
    lines.append("")
    lines.append("struct GenSig")
    lines.append("{")
    lines.append("    int      sym;")
    lines.append("    uint32_t len;")
    lines.append("    uint32_t rva;")
    lines.append("    uint8_t  bytes[32];")
    lines.append("    uint8_t  mask[32];")
    lines.append("};")
    lines.append("")
    lines.append("static const GenSig kGenSigs[] =")
    lines.append("{")
    for name, sym, rva in sigs:
        raw, mask = mask_pattern(d, rva)
        hx = ", ".join("0x%02X" % b for b in raw)
        mx = ", ".join("0x%02X" % b for b in mask)
        lines.append("    { %d, %du, 0x%08Xu," % (sym, TAKE, rva))
        lines.append("      { %s }," % hx)
        lines.append("      { %s } }, // %s" % (mx, name))
    lines.append("};")
    lines.append("")
    lines.append("constexpr int kSigCount = %d;" % len(sigs))
    lines.append("")
    lines.append("} // namespace ovp")
    lines.append("")

    out = os.path.join(ROOT, "overlay", "bh3_sigs.inc")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print("wrote %s" % out)
    for name, sym, rva in sigs:
        raw, mask = mask_pattern(d, rva)
        fixed = sum(1 for m in mask if m)
        if d:
            hits = count_hits(d, raw, mask, UA_BASE + rva)
            at = [h for h in hits if abs(h - (UA_BASE + rva)) < 0x10000]
            print("%-18s rva=0x%08X fixed=%d/%d hits=%d self=%d"
                  % (name, rva, fixed, TAKE, len(hits), len(at)))
        else:
            print("%-18s rva=0x%08X fixed=%d/%d (无 regions.csv，跳过命中计数)"
                  % (name, rva, fixed, TAKE))


if __name__ == "__main__":
    main()
