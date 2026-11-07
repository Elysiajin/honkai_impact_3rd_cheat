"""判定 il2cpp_get_api_table 每个槽位的真实输出语义.

问题: 静态 .rdata 表 0xEB9BE8 的 1215 项 == 真函数 RVA.
离线仿真取 lea 值 -> 与表 1020/1215 相同.
残余 195 槽: lea 指向共享存根, 而表里是独立函数.
本脚本要回答: 这 195 槽的 `out = lea ^ mask` 是否 == 表值?
即 mask 是否恰好等于 (lea ^ table), 从而说明运行时输出就是表值.
"""
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_64

import dump as D
import api_table2 as A
from api_table2 import PE, UA, NSLOT, find_table

RVA_TABLE = 0xEB9BE8
PREF_BASE = 0x180000000

REG64 = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
         "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]
R32 = {"eax": "rax", "ecx": "rcx", "edx": "rdx", "ebx": "rbx", "esp": "rsp",
       "ebp": "rbp", "esi": "rsi", "edi": "rdi", "r8d": "r8", "r9d": "r9",
       "r10d": "r10", "r11d": "r11", "r12d": "r12", "r13d": "r13",
       "r14d": "r14", "r15d": "r15"}
SUPPORTED = {"movabs", "mov", "imul", "add", "sub", "xor", "and", "or",
             "shr", "sar", "shl", "lea", "inc", "dec", "neg", "not", "nop", "jmp"}


def run_block(pe, start, index, md, max_ins=80):
    """解释执行一个槽位解码块, 返回 (lea_val, lea_dst, mask, out)."""
    code = pe.read(start, 512)
    regs = {r: 0 for r in REG64}
    regs["rax"] = index
    regs["rcx"] = 0x10000000
    lea_val = None
    lea_dst = None
    for ins in md.disasm(code, start):
        m = ins.mnemonic
        if m == "jmp":
            return lea_val, lea_dst, regs["r9"], regs["r8"] ^ regs["r9"], ins.op_str
        if m not in SUPPORTED:
            raise RuntimeError("unsupported %s at %#x" % (m, ins.address))
        if m in ("nop", "mov"):
            if m == "mov":
                pass
        if m == "movabs":
                    pass
        ops = ins.operands
        if m in ("nop",):
            continue
        if m == "jmp":
            continue
        # 只处理本混淆块用到的简单形态
        if m == "lea":
            dst = ops[0].reg
            dn = ins.reg_name(dst)
            if len(ops) > 1 and ops[1].type == 3:  # X86_OP_MEM
                mm = ops[1].mem
                if mm.base != 0:
                    raise RuntimeError("lea non-rip at %#x" % ins.address)
                val = (ins.address + ins.size + mm.disp) & 0xFFFFFFFFFFFFFFFF
                regs[dn] = val
                lea_val, lea_dst = val, dn
            continue
        if m in ("inc", "dec", "neg", "not"):
            dn = ins.reg_name(ops[0].reg)
            v = regs[dn]
            if m == "inc":
                v += 1
            elif m == "dec":
                v -= 1
            elif m == "neg":
                v = (-v) & 0xFFFFFFFFFFFFFFFF
            else:
                v = (~v) & 0xFFFFFFFFFFFFFFFF
            regs[dn] = v & 0xFFFFFFFFFFFFFFFF
            continue
        if m == "movabs":
            dn = ins.reg_name(ops[0].reg)
            regs[dn] = ops[1].imm & 0xFFFFFFFFFFFFFFFF
            continue
        if len(ops) < 2:
            raise RuntimeError("%s with %d ops at %#x" % (m, len(ops), ins.address))
        dn = ins.reg_name(ops[0].reg)
        if ops[1].type == 2:  # IMM
            b = ops[1].imm & 0xFFFFFFFFFFFFFFFF
            if m == "imul":
                a = regs[dn]
                v = (a * b) & 0xFFFFFFFFFFFFFFFF
            elif m == "add":
                v = (regs[dn] + b) & 0xFFFFFFFFFFFFFFFF
            elif m == "sub":
                v = (regs[dn] - b) & 0xFFFFFFFFFFFFFFFF
            elif m == "xor":
                v = regs[dn] ^ b
            elif m == "and":
                v = regs[dn] & b
            elif m == "or":
                v = regs[dn] | b
            elif m == "shr":
                v = regs[dn] >> b
            elif m == "sar":
                sv = regs[dn]
                if sv >= 1 << 63:
                    sv -= 1 << 64
                v = (sv >> b) & 0xFFFFFFFFFFFFFFFF
            elif m == "shl":
                v = (regs[dn] << b) & 0xFFFFFFFFFFFFFFFF
            else:
                raise RuntimeError("%s imm at %#x" % (m, ins.address))
            regs[dn] = v
        elif ops[1].type == 1:  # REG
            sn = ins.reg_name(ops[1].reg)
            b = regs.get(sn, 0)
            if m == "imul":
                v = (regs[dn] * b) & 0xFFFFFFFFFFFFFFFF
            elif m == "add":
                v = (regs[dn] + b) & 0xFFFFFFFFFFFFFFFF
            elif m == "sub":
                v = (regs[dn] - b) & 0xFFFFFFFFFFFFFFFF
            elif m == "xor":
                v = regs[dn] ^ b
            elif m == "and":
                v = regs[dn] & b
            elif m == "or":
                v = regs[dn] | b
            elif m == "shr":
                v = regs[dn] >> b
            elif m == "shl":
                v = (regs[dn] << b) & 0xFFFFFFFFFFFFFFFF
            else:
                raise RuntimeError("%s reg at %#x" % (m, ins.address))
            regs[dn] = v
        else:
            raise RuntimeError("op type %d at %#x" % (ops[1].type, ins.address))
    raise RuntimeError("block ran off at %#x" % start)


def main():
    pe = PE(UA)
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    tbl = find_table(pe)
    code = pe.read(tbl, NSLOT * 4)
    slot_rva = [tbl + struct.unpack_from("<i", code, i * 4)[0] for i in range(NSLOT)]
    st = [v - PREF_BASE for v in struct.unpack_from("<%dQ" % NSLOT, pe.read(RVA_TABLE, NSLOT * 8))]

    ok_lea = 0
    ok_out = 0
    ok_mask = 0
    bad = []
    for i in range(NSLOT):
        try:
            lea_val, lea_dst, mask, out, tail = run_block(pe, slot_rva[i], i, md)
        except Exception as e:
            bad.append((i, "ERR " + str(e)))
            continue
        if lea_val == st[i]:
            ok_lea += 1
        if out == st[i]:
            ok_out += 1
        if lea_val is not None and (lea_val ^ mask) == st[i]:
            ok_mask += 1
    print("total", NSLOT, "errors", len(bad))
    print("lea == table     :", ok_lea)
    print("out == table     :", ok_out)
    print("lea^mask==table  :", ok_mask)
    for b in bad[:8]:
        print("  bad", b)


if __name__ == "__main__":
    main()
