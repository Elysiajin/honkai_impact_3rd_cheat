"""逐槽打印 r8 / r9 / lea / 静态表值 / dump 表值, 找出 195 个不一致槽位的真实语义."""
import struct

import capstone

import dump as D
from api_table2 import PE, UA, NSLOT, ENTRY, find_table, emulate

RVA_TABLE = 0xEB9BE8
PREF_BASE = 0x180000000


def lea_of(pe, start, index, md):
    regs = {"rax": index}
    data = pe.read(start, 0x200)
    last = None
    for ins in md.disasm(data, start):
        if ins.mnemonic == "jmp":
            return last
        if ins.mnemonic == "lea" and len(ins.operands) > 1:
            mem = ins.operands[1].mem
            if mem.base and ins.reg_name(mem.base) == "rip":
                last = (ins.address + ins.size + mem.disp) & 0xFFFFFFFFFFFFFFFF
    return last


def main():
    pe = PE(UA)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    tb = find_table(pe)
    ents = struct.unpack_from("<%di" % NSLOT, pe.read(tb, NSLOT * 4))
    starts = [(e + tb) & 0xFFFFFFFFFFFFFFFF for e in ents]
    st = [v - PREF_BASE for v in struct.unpack_from("<%dQ" % NSLOT, pe.read(RVA_TABLE, NSLOT * 8))]

    d = D.Dump()
    du_raw = list(struct.unpack_from("<%dQ" % NSLOT, d.read(0x7FF8C5CBA320, NSLOT * 8)))
    du = [v - D.UA_BASE for v in du_raw]

    print("idx  lea         r8          r9              out         static      dump")
    for i in range(12):
        out, tr, regs = emulate(pe, starts[i], i, md)
        lea = lea_of(pe, starts[i], i, md)
        print("%-4d %-11X %-11X %-15X %-11X %-11X %-11X" % (
            i, lea or 0, regs["r8"], regs["r9"], out, st[i], du[i]))

    # 统计: 每个槽位 lea 与 static / dump 的匹配
    m_st = m_du = 0
    for i in range(NSLOT):
        lea = lea_of(pe, starts[i], i, md)
        if lea == st[i]:
            m_st += 1
        if lea == du[i]:
            m_du += 1
    print("\nlea==static", m_st, " lea==dump", m_du)


if __name__ == "__main__":
    main()
