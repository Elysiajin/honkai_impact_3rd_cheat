import csv
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from capstone import CS_ARCH_X86, CS_MODE_64, Cs

from dump import Dump, UA_BASE

SIGS = [
    ("is_sp_enough", 28, 0x08011F70),
    ("get_skill_cd", 29, 0x08009530),
    ("is_skill_has_cd", 30, 0x080152A0),
]

TAKE = 32


def mask_pattern(d, rva):
    raw = bytearray(d.read(UA_BASE + rva, TAKE))
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
    d = Dump()
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
    for name, sym, rva in SIGS:
        raw, mask = mask_pattern(d, rva)
        hx = ", ".join("0x%02X" % b for b in raw)
        mx = ", ".join("0x%02X" % b for b in mask)
        lines.append("    { %d, %du, 0x%08Xu," % (sym, TAKE, rva))
        lines.append("      { %s }," % hx)
        lines.append("      { %s } }, // %s" % (mx, name))
    lines.append("};")
    lines.append("")
    lines.append("constexpr int kSigCount = %d;" % len(SIGS))
    lines.append("")
    lines.append("} // namespace ovp")
    lines.append("")

    out = os.path.join(ROOT, "overlay", "bh3_sigs.inc")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print("wrote %s" % out)
    for name, sym, rva in SIGS:
        raw, mask = mask_pattern(d, rva)
        fixed = sum(1 for m in mask if m)
        hits = count_hits(d, raw, mask, UA_BASE + rva)
        at = [h for h in hits if abs(h - (UA_BASE + rva)) < 0x10000]
        print("%-18s rva=0x%08X fixed=%d/%d hits=%d self=%d"
              % (name, rva, fixed, TAKE, len(hits), len(at)))


if __name__ == "__main__":
    main()
