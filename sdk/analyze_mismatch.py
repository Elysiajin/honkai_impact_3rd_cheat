"""分析 194 个不匹配槽位: lea 值/表值 是否互为集合成员, 累加器与表值的关系."""
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

    res = [emulate_slot(pe, s, i, md) for i, s in enumerate(starts)]
    leas = [r[1] for r in res]
    tabset = set(table)
    leaset = set(v for v in leas if v)

    bad = [i for i in range(NSLOT) if leas[i] != table[i]]
    print("mismatch count", len(bad))
    in_tab = sum(1 for i in bad if leas[i] in tabset)
    print("  of which lea value IS in table set:", in_tab)
    # 表值是否出现在 lea 集合
    in_lea = sum(1 for i in bad if table[i] in leaset)
    print("  of which table value IS in lea set:", in_lea)
    print("  table set size %d, lea set size %d, inter %d" % (len(tabset), len(leaset), len(tabset & leaset)))
    print("  lea values not in table:", len(leaset - tabset))
    print("  table values not in lea:", len(tabset - leaset))

    # 重复 lea 值
    from collections import Counter
    cl = Counter(v for v in leas if v)
    dup = [(v, c) for v, c in cl.items() if c > 1]
    print("  duplicate lea values:", [(hex(v), c) for v, c in dup])

    # 逐槽看 accumulator 与表值关系
    print("\n=== r9/acc vs table^lea for first 10 mismatches")
    for i in bad[:10]:
        d, v, t = res[i]
        # 重跑一次拿 regs
        regs = emulate_regs(pe, starts[i], i, md)
        acc = regs.get("r9", 0)
        print("  slot %-4d table 0x%-10X lea 0x%-9X r9 0x%X  table^lea 0x%X" %
              (i, table[i], v or 0, acc, (table[i] ^ (v or 0))))


def emulate_regs(pe, start, index, md):
    regs = {r: 0 for r in "rax rcx rdx rbx rsi rdi r8 r9 r10 r11 r12 r13 r14 r15".split()}
    regs["rax"] = index
    regs["rcx"] = 0x10000000
    for ins in md.disasm(pe.read(start, 0x100), start):
        m, ops = ins.mnemonic, ins.operands
        if m == "jmp":
            break
        if m == "nop":
            continue
        if m == "movabs":
            regs[ins.reg_name(ops[0].reg)] = ops[1].imm & MASK
        elif m == "mov":
            d = ins.reg_name(ops[0].reg)
            regs[d] = ops[1].imm & MASK if ops[1].type == capstone.x86.X86_OP_IMM else regs[ins.reg_name(ops[1].reg)]
        elif m == "imul":
            if len(ops) == 3:
                regs[ins.reg_name(ops[0].reg)] = (regs[ins.reg_name(ops[1].reg)] * (ops[2].imm & MASK)) & MASK
            else:
                d = ins.reg_name(ops[0].reg)
                b = regs[ins.reg_name(ops[1].reg)] if ops[1].type == capstone.x86.X86_OP_REG else ops[1].imm & MASK
                regs[d] = (regs[d] * b) & MASK
        elif m in ("add", "sub", "xor", "and", "or"):
            d = ins.reg_name(ops[0].reg)
            a = regs[d]
            b = regs[ins.reg_name(ops[1].reg)] if ops[1].type == capstone.x86.X86_OP_REG else ops[1].imm & MASK
            regs[d] = {"add": (a + b) & MASK, "sub": (a - b) & MASK,
                       "xor": a ^ b, "and": a & b, "or": a | b}[m]
        elif m in ("shr", "sar", "shl"):
            d = ins.reg_name(ops[0].reg)
            n = ops[1].imm if ops[1].type == capstone.x86.X86_OP_IMM else regs[ins.reg_name(ops[1].reg)]
            a = regs[d]
            regs[d] = (a >> n) & MASK if m == "shr" else ((a << n) & MASK if m == "shl" else a)
        elif m == "lea":
            mem = ops[1].mem
            base = (ins.address + ins.size) if (mem.base and ins.reg_name(mem.base) == "rip") else \
                   (regs[ins.reg_name(mem.base)] if mem.base else 0)
            idx = regs[ins.reg_name(mem.index)] if mem.index else 0
            regs[ins.reg_name(ops[0].reg)] = (base + idx * mem.scale + mem.disp) & MASK
    return regs


if __name__ == "__main__":
    main()
