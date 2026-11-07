"""精修: 记录 lea 的目标寄存器, 取"真值寄存器"而非最后一个 lea; 与静态 .rdata 表逐项比对.

同时:
  * 找引用 .rdata 表 0xEB9BE8 的代码
  * 检查该表是否在 .reloc 里(即是否为可重定位绝对指针)
  * 对比 .data 运行期表 0x241A320 与 lea 结果的关系
"""
import struct

import capstone

import dump as D
from api_table2 import PE, find_table, UA, NSLOT, MASK

RVA_RDATA_TABLE = 0xEB9BE8
PREF_BASE = 0x180000000
TAIL_A = 0x650160      # xor r8,r9 ; store r8
TAIL_B = 0x664E40      # xor r9,r8 ; store r9
DUMP_TABLE_RVA = 0x241A320


def emulate_slot(pe, start, index, md):
    regs = {r: 0 for r in "rax rcx rdx rbx rsi rdi r8 r9 r10 r11 r12 r13 r14 r15".split()}
    regs["rax"] = index
    regs["rcx"] = 0x10000000
    lea_dst = None
    lea_val = None
    tail = None
    for ins in md.disasm(pe.read(start, 0x100), start):
        m, ops = ins.mnemonic, ins.operands
        if m == "jmp":
            try:
                tail = int(ins.op_str, 16)
            except ValueError:
                tail = -1
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
            v = (base + idx * mem.scale + mem.disp) & MASK
            lea_dst = ins.reg_name(ops[0].reg)
            lea_val = v
            regs[lea_dst] = v
        elif m == "inc":
            d = ins.reg_name(ops[0].reg)
            regs[d] = (regs[d] + 1) & MASK
        else:
            return None, None, None
    return lea_dst, lea_val, tail


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
    dsts = {}
    for d, _, _ in res:
        dsts[d] = dsts.get(d, 0) + 1
    tails = {}
    for _, _, t in res:
        tails[t] = tails.get(t, 0) + 1
    print("lea dst histogram:", dsts)
    print("tail histogram:", {hex(k) if isinstance(k, int) and k > 0 else k: v for k, v in tails.items()})

    match_lea = sum(1 for i, (d, v, t) in enumerate(res) if v == table[i])
    print("lea-value == rdata-table : %d / %d" % (match_lea, NSLOT))

    # 按 tail 选择寄存器: tail A 存 r8, tail B 存 r9 -> 真值 = 那个寄存器在 xor 前的值
    # 真值寄存器 = lea 目标
    bad = [(i, table[i], res[i][1]) for i in range(NSLOT) if res[i][1] != table[i]]
    print("mismatches:", len(bad))
    for i, a, b in bad[:15]:
        d, v, t = res[i]
        print("  slot %d table 0x%X lea(%s) 0x%X tail %s" %
              (i, a, d, b if b is not None else 0, hex(t) if t else t))

    # .reloc 是否覆盖 0xEB9BE8 区间
    print("\n=== .reloc coverage of rdata table")
    rel = pe.sec_of(0xEB9BE8)
    print("  section:", rel[0], "va 0x%X" % rel[1])
    for name, va, vsize, rawptr, rawsize in pe.secs:
        if name == ".reloc":
            print("  .reloc va 0x%X vsize 0x%X rawsize 0x%X" % (va, vsize, rawsize))
            blob = pe.read(va, min(vsize, rawsize))
            lo, hi = 0xEB9BE8, 0xEB9BE8 + NSLOT * 8
            hit = 0
            off = 0
            while off + 8 <= len(blob):
                page_rva, block_sz = struct.unpack_from("<II", blob, off)
                if block_sz == 0:
                    break
                n = (block_sz - 8) // 2
                for k in range(n):
                    e = struct.unpack_from("<H", blob, off + 8 + k * 2)[0]
                    if e >> 12 == 10:  # IMAGE_REL_BASED_DIR64
                        target = page_rva + (e & 0xFFF)
                        if lo <= target < hi:
                            hit += 1
                off += block_sz
            print("  DIR64 relocs inside table range:", hit, "/", NSLOT)

    # 找引用 0xEB9BE8 的 lea
    print("\n=== code referencing rdata table 0xEB9BE8")
    text = pe.read(0x1000, 0x6D8E00)
    found = 0
    for off in range(len(text) - 7):
        if text[off] == 0x48 and text[off + 1] in (0x8D, 0x8B):
            modrm = text[off + 2]
            if (modrm & 0xC7) == 0x05:
                disp = struct.unpack_from("<i", text, off + 3)[0]
                tgt = 0x1000 + off + 7 + disp
                if tgt == RVA_RDATA_TABLE:
                    print("  at rva 0x%X" % (0x1000 + off))
                    found += 1
    print("  total", found)

    # dump 表 vs lea
    d = D.Dump()
    dvals = [d.q(D.UA_BASE + DUMP_TABLE_RVA + i * 8) - D.UA_BASE for i in range(NSLOT)]
    print("\n=== dump .data table vs rdata table")
    print("  identical:", sum(1 for a, b in zip(dvals, table) if a == b))
    print("  dump[0] 0x%X rdata[0] 0x%X" % (dvals[0], table[0]))


if __name__ == "__main__":
    main()
