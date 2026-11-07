"""完整反汇编若干不一致槽位的解码块, 统计块内 lea 数量, 判断 195 槽差异成因."""
import struct

import capstone

from api_table2 import PE, UA, NSLOT, find_table

RVA_TABLE = 0xEB9BE8
PREF = 0x180000000


def main():
    pe = PE(UA)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    tb = find_table(pe)
    ents = struct.unpack_from("<%di" % NSLOT, pe.read(tb, NSLOT * 4))
    starts = [tb + e for e in ents]
    st = [v - PREF for v in struct.unpack_from("<%dQ" % NSLOT, pe.read(RVA_TABLE, NSLOT * 8))]

    # 统计每块 lea 数
    from collections import Counter
    hist = Counter()
    multi = []
    for i, s in enumerate(starts):
        leas = []
        for ins in md.disasm(pe.read(s, 0x200), s):
            if ins.mnemonic == "jmp":
                break
            if ins.mnemonic == "lea":
                leas.append((ins.address, ins.op_str, (ins.address + ins.size + ins.operands[1].mem.disp)))
        hist[len(leas)] += 1
        if len(leas) > 1:
            multi.append((i, leas))
    print("lea-count histogram:", dict(hist))
    print("blocks with >1 lea:", len(multi))
    for i, leas in multi[:6]:
        print(" slot", i, "static 0x%X" % st[i], [(hex(a), o, hex(t)) for a, o, t in leas])

    # 完整打印 slot 5
    for i in (5, 47):
        print("\n=== slot %d @0x%X  static=0x%X" % (i, starts[i], st[i]))
        for ins in md.disasm(pe.read(starts[i], 0x200), starts[i]):
            print("  %X  %-24s %s %s" % (ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str))
            if ins.mnemonic == "jmp":
                break


if __name__ == "__main__":
    main()
