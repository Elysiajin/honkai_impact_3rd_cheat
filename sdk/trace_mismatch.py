"""打印不匹配槽位的完整解码块, 找出 194 个差异的原因."""
import struct

import capstone

from api_table2 import PE, find_table, UA, NSLOT, MASK
from refine_api_table import RVA_RDATA_TABLE, PREF_BASE, emulate_slot


def main():
    pe = PE(UA)
    tb = find_table(pe)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    ents = struct.unpack_from("<%di" % NSLOT, pe.read(tb, NSLOT * 4))
    starts = [(e + tb) & MASK for e in ents]
    table = [v - PREF_BASE for v in
             struct.unpack_from("<%dQ" % NSLOT, pe.read(RVA_RDATA_TABLE, NSLOT * 8))]

    for slot in (5, 32, 47):
        s = starts[slot]
        print("=" * 70)
        print("slot %d start 0x%X, rdata table target 0x%X" % (slot, s, table[slot]))
        for ins in md.disasm(pe.read(s, 0x60), s):
            print("   %X  %-18s %s %s" % (ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str))
            if ins.mnemonic == "jmp":
                break
        d, v, t = emulate_slot(pe, s, slot, md)
        print("   -> lea dst %s value 0x%X tail %s" % (d, v or 0, hex(t) if t else t))
        # 表项指向的字节
        print("   table target bytes:", pe.read(table[slot], 16).hex())


if __name__ == "__main__":
    main()
