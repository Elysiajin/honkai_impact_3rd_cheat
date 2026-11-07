"""对比: 离线仿真得到的 lea 目标 vs dump 里的真实 API 表."""
import struct
import capstone

from api_table2 import PE, find_table, UA, NSLOT, MASK

ENTRY = 0x650100


def emulate_lea(pe, start, index, md):
    regs = {r: 0 for r in ("rax rcx rdx rbx rsi rdi r8 r9 r10 r11 r12 r13 r14 r15".split())}
    regs["rax"] = index
    regs["rcx"] = 0x10000000
    lea_val = None
    data = pe.read(start, 0x200)
    for ins in md.disasm(data, start):
        m = ins.mnemonic
        ops = ins.operands
        if m == "jmp":
            break
        if m == "nop":
            continue
        if m == "movabs":
            regs[ins.reg_name(ops[0].reg)] = ops[1].imm & MASK
        elif m == "imul":
            if len(ops) == 3:
                regs[ins.reg_name(ops[0].reg)] = (regs[ins.reg_name(ops[1].reg)] * (ops[2].imm & MASK)) & MASK
            else:
                a = regs[ins.reg_name(ops[0].reg)]
                b = regs[ins.reg_name(ops[1].reg)] if ops[1].type == capstone.x86.X86_OP_REG else ops[1].imm & MASK
                regs[ins.reg_name(ops[0].reg)] = (a * b) & MASK
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
            if m == "shr":
                regs[d] = (a >> n) & MASK
            elif m == "sar":
                sa = a if a < (1 << 63) else a - (1 << 64)
                regs[d] = (sa >> n) & MASK
            else:
                regs[d] = (a << n) & MASK
        elif m == "lea":
            mem = ops[1].mem
            if mem.base and ins.reg_name(mem.base) == "rip":
                base = ins.address + ins.size
            else:
                base = regs[ins.reg_name(mem.base)] if mem.base else 0
            idx = regs[ins.reg_name(mem.index)] if mem.index else 0
            v = (base + idx * mem.scale + mem.disp) & MASK
            regs[ins.reg_name(ops[0].reg)] = v
            lea_val = v
        elif m == "inc":
            regs[ins.reg_name(ops[0].reg)] = (regs[ins.reg_name(ops[0].reg)] + 1) & MASK
        else:
            return None
    return lea_val


def main():
    pe = PE(UA)
    tb = find_table(pe)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    ents = struct.unpack_from("<%di" % NSLOT, pe.read(tb, NSLOT * 4))
    starts = [(e + tb) & MASK for e in ents]

    leas = []
    for i, s in enumerate(starts):
        leas.append(emulate_lea(pe, s, i, md))
    ok = [v for v in leas if v]
    print("lea resolved %d / %d" % (len(ok), NSLOT))
    print("first 8 lea:", [hex(v) for v in leas[:8]])
    inimg = sum(1 for v in ok if v < pe.size_image)
    print("in-image %d" % inimg)
    # 分布
    secs = {}
    for v in ok:
        s = pe.sec_of(v)
        secs[s[0] if s else "?"] = secs.get(s[0] if s else "?", 0) + 1
    print("sections:", secs)

    # dump 侧
    import dump as D
    d = D.Dump()
    TABLE_VA = 0x7FF8C5CBA320
    vals = [d.q(TABLE_VA + i * 8) for i in range(1215)]
    rvas = [v - D.UA_BASE for v in vals]
    print("dump table first 8 rva:", [hex(v) for v in rvas[:8]])

    # 逐个比对（可能整体位移）
    for shift in (0,):
        hits = sum(1 for a, b in zip(leas, rvas) if a == b)
        print("direct match:", hits)
    # 用集合交集
    sl = set(v for v in leas if v)
    sr = set(rvas)
    print("lea set %d, dump set %d, intersect %d" % (len(sl), len(sr), len(sl & sr)))

    # 反向: 在 dump 里找 lea[0] 出现的位置
    target = leas[0]
    pos = [i for i, v in enumerate(rvas) if v == target]
    print("lea[0]=0x%X appears in dump at %s" % (target, pos[:5]))

    with open("api_table_lea.tsv", "w", encoding="utf-8") as f:
        f.write("index\tlea_rva\n")
        for i, v in enumerate(leas):
            f.write("%d\t%s\n" % (i, ("0x%X" % v) if v else "0"))
    print("wrote api_table_lea.tsv")


if __name__ == "__main__":
    main()
