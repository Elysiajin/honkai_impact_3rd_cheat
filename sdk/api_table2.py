"""il2cpp_get_api_table 离线解混淆 (v2).

v1 (api_table.py) 解出的 1214 个值不在镜像内, 说明解释器漏了指令语义.
v2 打印每个块用到的全部助记符, 遇到不支持的直接报出, 避免静默算错.
"""
import struct
import sys

import capstone

UA = r"D:\Games\miHoYo Launcher\games\Honkai Impact 3rd Game\BH3_Data\Native\UserAssembly.dll"
ENTRY = 0x650100
NSLOT = 0x4BF

MASK = (1 << 64) - 1


class PE:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.f.seek(0)
        dos = self.f.read(0x40)
        e_lfanew = struct.unpack_from("<I", dos, 0x3C)[0]
        self.f.seek(e_lfanew)
        nt = self.f.read(0x108)
        nsec = struct.unpack_from("<H", nt, 6)[0]
        opt_size = struct.unpack_from("<H", nt, 20)[0]
        self.image_base = struct.unpack_from("<Q", nt, 0x30)[0]
        self.size_image = struct.unpack_from("<I", nt, 0x50)[0]
        sec_off = e_lfanew + 24 + opt_size
        self.f.seek(sec_off)
        self.secs = []
        for _ in range(nsec):
            s = self.f.read(40)
            name = s[:8].rstrip(b"\x00").decode("latin-1")
            vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", s, 8)
            self.secs.append((name, va, vsize, rawptr, rawsize))

    def sec_of(self, rva):
        for name, va, vsize, rawptr, rawsize in self.secs:
            if va <= rva < va + max(vsize, rawsize):
                return name, va, vsize, rawptr, rawsize
        return None

    def read(self, rva, n):
        out = bytearray()
        cur = rva
        while len(out) < n:
            s = self.sec_of(cur)
            if s is None:
                break
            name, va, vsize, rawptr, rawsize = s
            off = cur - va
            if off >= rawsize:
                break
            take = min(n - len(out), rawsize - off)
            self.f.seek(rawptr + off)
            out += self.f.read(take)
            cur += take
        return bytes(out)


def find_table(pe):
    code = pe.read(ENTRY, 12)
    # 31c0 488d15 xx xx xx xx  -> lea rdx,[rip+disp]
    assert code[:2] == b"\x31\xc0", code.hex()
    assert code[2:5] == b"\x48\x8d\x15", code.hex()
    disp = struct.unpack_from("<i", code, 5)[0]
    return ENTRY + 9 + disp


SUPPORTED = {
    "movabs", "mov", "imul", "add", "sub", "xor", "and", "or",
    "shr", "sar", "shl", "lea", "inc", "dec", "neg", "not", "nop", "jmp",
}


def emulate(pe, start, index, md, verbose=False):
    regs = {"rax": index, "rcx": 0x10000000, "rdx": 0, "rbx": 0, "rsi": 0, "rdi": 0,
            "r8": 0, "r9": 0, "r10": 0, "r11": 0, "r12": 0, "r13": 0, "r14": 0, "r15": 0}
    data = pe.read(start, 0x200)
    trace = []
    for ins in md.disasm(data, start):
        m = ins.mnemonic
        if m not in SUPPORTED:
            raise RuntimeError("unsupported %s at %X: %s %s" % (m, ins.address, m, ins.op_str))
        if m == "jmp":
            trace.append("jmp %s" % ins.op_str)
            break
        if m == "nop":
            continue
        ops = ins.operands
        if m == "movabs":
            regs[ins.reg_name(ops[0].reg)] = ops[1].imm & MASK
        elif m == "mov":
            dst = ins.reg_name(ops[0].reg)
            if ops[1].type == capstone.x86.X86_OP_IMM:
                regs[dst] = ops[1].imm & MASK
            elif ops[1].type == capstone.x86.X86_OP_REG:
                regs[dst] = regs[ins.reg_name(ops[1].reg)]
            else:
                raise RuntimeError("mov from mem at %X" % ins.address)
        elif m == "imul":
            if len(ops) == 3:
                a = regs[ins.reg_name(ops[1].reg)]
                imm = ops[2].imm & MASK
                regs[ins.reg_name(ops[0].reg)] = (a * imm) & MASK
            else:
                a = regs[ins.reg_name(ops[0].reg)]
                b = regs[ins.reg_name(ops[1].reg)] if ops[1].type == capstone.x86.X86_OP_REG else ops[1].imm & MASK
                regs[ins.reg_name(ops[0].reg)] = (a * b) & MASK
        elif m in ("add", "sub", "xor", "and", "or"):
            dst = ins.reg_name(ops[0].reg)
            a = regs[dst]
            if ops[1].type == capstone.x86.X86_OP_REG:
                b = regs[ins.reg_name(ops[1].reg)]
            else:
                b = ops[1].imm & MASK
            if m == "add":
                regs[dst] = (a + b) & MASK
            elif m == "sub":
                regs[dst] = (a - b) & MASK
            elif m == "xor":
                regs[dst] = a ^ b
            elif m == "and":
                regs[dst] = a & b
            else:
                regs[dst] = a | b
        elif m in ("shr", "sar", "shl"):
            dst = ins.reg_name(ops[0].reg)
            n = ops[1].imm if ops[1].type == capstone.x86.X86_OP_IMM else regs[ins.reg_name(ops[1].reg)]
            a = regs[dst]
            if m == "shr":
                regs[dst] = (a >> n) & MASK
            elif m == "sar":
                sa = a if a < (1 << 63) else a - (1 << 64)
                regs[dst] = (sa >> n) & MASK
            else:
                regs[dst] = (a << n) & MASK
        elif m == "lea":
            dst = ins.reg_name(ops[0].reg)
            if ops[1].type != capstone.x86.X86_OP_MEM:
                raise RuntimeError("lea non-mem at %X" % ins.address)
            mem = ops[1].mem
            if mem.base and ins.reg_name(mem.base) == "rip":
                base = ins.address + ins.size
            else:
                base = regs[ins.reg_name(mem.base)] if mem.base else 0
            idx = regs[ins.reg_name(mem.index)] if mem.index else 0
            regs[dst] = (base + idx * mem.scale + mem.disp) & MASK
        elif m == "inc":
            dst = ins.reg_name(ops[0].reg)
            regs[dst] = (regs[dst] + 1) & MASK
        elif m == "dec":
            dst = ins.reg_name(ops[0].reg)
            regs[dst] = (regs[dst] - 1) & MASK
        elif m == "neg":
            dst = ins.reg_name(ops[0].reg)
            regs[dst] = (-regs[dst]) & MASK
        elif m == "not":
            dst = ins.reg_name(ops[0].reg)
            regs[dst] = (~regs[dst]) & MASK
        if verbose:
            trace.append("%X %s %s" % (ins.address, m, ins.op_str))
    # 共享尾巴: 两处都是 out[i] = r8 ^ r9
    return regs["r8"] ^ regs["r9"], trace, regs


def main():
    pe = PE(UA)
    tb = find_table(pe)
    print("table base rva = 0x%X" % tb)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    ents = struct.unpack_from("<%di" % NSLOT, pe.read(tb, NSLOT * 4))
    starts = [(e + tb) & MASK for e in ents]
    print("slot0 -> 0x%X" % starts[0])

    vals = []
    bad = 0
    lo, hi = 0, pe.size_image
    inimg = 0
    for i, s in enumerate(starts):
        try:
            v, tr, regs = emulate(pe, s, i, md)
        except RuntimeError as e:
            print("slot %d fail: %s" % (i, e))
            bad += 1
            v = 0
        vals.append(v)
        if lo <= v < hi:
            inimg += 1
    print("emulated %d, failures %d, in-image %d" % (len(vals), bad, inimg))
    print("first 8:", [hex(v) for v in vals[:8]])

    # 详细 trace 前 3 槽
    for i in range(3):
        v, tr, regs = emulate(pe, starts[i], i, md, verbose=True)
        print("\n--- slot %d -> 0x%X  result=0x%X" % (i, starts[i], v))
        for t in tr:
            print("   ", t)
        print("    r8=0x%X r9=0x%X" % (regs["r8"], regs["r9"]))


if __name__ == "__main__":
    main()
