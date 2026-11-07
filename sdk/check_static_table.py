"""确认: 静态 .rdata 里 0xEB9BE8 处的指针表是不是真 API 表, 与 dump 里 0x241A320 表的关系."""
import struct

import capstone

import dump as D
from api_table2 import PE, UA, NSLOT

UA_BASE = D.UA_BASE
RVA_STATIC_TABLE = 0xEB9BE8
RVA_DUMP_TABLE = 0x241A320


def main():
    pe = PE(UA)
    d = D.Dump()

    raw = pe.read(RVA_STATIC_TABLE, NSLOT * 8 + 64)
    ents = list(struct.unpack_from("<%dQ" % NSLOT, raw))
    print("static table @0x%X, first 6: %s" % (RVA_STATIC_TABLE, [hex(x) for x in ents[:6]]))
    print("  last 4:", [hex(x) for x in ents[-4:]])
    print("  unique", len(set(ents)))
    secs = {}
    for v in ents:
        s = pe.sec_of(v)
        secs[s[0] if s else "?out"] = secs.get(s[0] if s else "?out", 0) + 1
    print("  sections", secs)
    # 前一个 qword 是不是不属于 il2cpp, 判断表边界
    prev = struct.unpack_from("<Q", pe.read(RVA_STATIC_TABLE - 8, 8))[0]
    print("  prev qword 0x%X in %s" % (prev, pe.sec_of(prev)[0] if pe.sec_of(prev) else "?"))

    draw = d.read(UA_BASE + RVA_DUMP_TABLE, NSLOT * 8)
    dents = list(struct.unpack_from("<%dQ" % NSLOT, draw))
    print("\ndump table @0x%X, first 6: %s" % (RVA_DUMP_TABLE, [hex(x) for x in dents[:6]]))
    print("  unique", len(set(dents)))
    sset = set(ents)
    dset = set(dents)
    print("  static set %d, dump set %d, intersect %d" % (len(sset), len(dset), len(sset & dset)))
    print("  dump values in il2cpp rva range:",
          sum(1 for v in dents if 0x3332000 <= v - UA_BASE < 0x12674200))

    # 静态表里 rva -> 是否 il2cpp
    print("\n  static table rva range:", hex(min(ents)), hex(max(ents)))

    # 0x664E80..0x664EB0 反汇编
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    print("\n=== 0x664E30..0x664EA0")
    for ins in md.disasm(pe.read(0x664E30, 0x70), 0x664E30):
        print("  %X  %-20s %s %s" % (ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str))

    # 搜索 call 0x650100 的调用点
    print("\n=== callers of 0x650100 in .text")
    text = pe.read(0x1000, 0x6D8E00)
    pat = b"\xe8"
    hits = 0
    for off in range(0, len(text) - 5):
        if text[off] == 0xE8:
            rel = struct.unpack_from("<i", text, off + 1)[0]
            tgt = 0x1000 + off + 5 + rel
            if tgt == 0x650100:
                print("  call at 0x%X" % (0x1000 + off))
                hits += 1
    print("  total", hits)


if __name__ == "__main__":
    main()
