"""逐指令打印 slot 16 / 5 的寄存器演化, 定位累加器算错的原因."""
import struct

import capstone

from api_table2 import PE, find_table, UA, NSLOT, MASK
from refine_api_table import RVA_RDATA_TABLE, PREF_BASE

R64 = "rax rcx rdx rbx rsi rdi r8 r9 r10 r11 r12 r13 r14 r15".split()
R32 = "eax ecx edx ebx esi edi r8d r9d r10d r11d r12d r13d r14d r15d".split()
M32 = dict(zip(R32, R64))


def main():
    pe = PE(UA)
    tb = find_table(pe)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    ents = struct.unpack_from("<%di" % NSLOT, pe.read(tb, NSLOT * 4))
    starts = [(e + tb) & MASK for e in ents]
    table = [v - PREF_BASE for v in
             struct.unpack_from("<%dQ" % NSLOT, pe.read(RVA_RDATA_TABLE, NSLOT * 8))]

    for slot in (16, 5):
        s = starts[slot]
        print("=" * 72)
        print("slot %d start 0x%X table 0x%X" % (slot, s, table[slot]))
        regs = {r: 0 for r in R64}
        regs["rax"] = slot
        regs["rcx"] = 0x10000000
        for ins in md.disasm(pe.read(s, 0x80), s):
            m, ops = ins.mnemonic, ins.operands
            print("  %X  %-20s %-28s | before r8=0x%X r9=0x%X" %
                  (ins.address, ins.bytes.hex(), m + " " + ins.op_str, regs["r8"], regs["r9"]))
            if m == "jmp":
                break
            if m == "nop":
                continue
            # 记录操作数是否 32 位
            def rd(op):
                n = ins.reg_name(op.reg)
                if n in M32:
                    return regs[M32[n]]
                return regs[n]

            def wr(op, v):
                n = ins.reg_name(op.reg)
                if n in M32:
                    regs[M32[n]] = v & 0xFFFFFFFF
                else:
                    regs[n] = v & MASK

            if m == "movabs":
                wr(ops[0], ops[1].imm)
            elif m == "mov":
                wr(ops[0], ops[1].imm if ops[1].type == capstone.x86.X86_OP_IMM else rd(ops[1]))
            elif m == "imul":
                if len(ops) == 3:
                    wr(ops[0], rd(ops[1]) * (ops[2].imm & MASK))
                else:
                    wr(ops[0], rd(ops[0]) * (rd(ops[1]) if ops[1].type == capstone.x86.X86_OP_REG else ops[1].imm & MASK))
            elif m in ("add", "sub", "xor", "and", "or"):
                a = rd(ops[0])
                b = rd(ops[1]) if ops[1].type == capstone.x86.X86_OP_REG else ops[1].imm & MASK
                wr(ops[0], {"add": a + b, "sub": a - b, "xor": a ^ b, "and": a & b, "or": a | b}[m])
            elif m in ("shr", "sar", "shl"):
                a = rd(ops[0])
                n = ops[1].imm if ops[1].type == capstone.x86.X86_OP_IMM else rd(ops[1])
                wr(ops[0], a >> n if m == "shr" else (a << n if m == "shl" else a))
            elif m == "lea":
                mem = ops[1].mem
                base = (ins.address + ins.size) if (mem.base and ins.reg_name(mem.base) == "rip") else \
                       (rd(type("O", (), {"reg": mem.base})) if mem.base else 0)
                idx = rd(type("O", (), {"reg": mem.index})) if mem.index else 0
                wr(ops[0], base + idx * mem.scale + mem.disp)
        print("  final r8=0x%X r9=0x%X  out=0x%X  table=0x%X" %
              (regs["r8"], regs["r9"], regs["r8"] ^ regs["r9"], table[slot]))


if __name__ == "__main__":
    main()
