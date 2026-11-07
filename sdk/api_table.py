#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""api_table.py - 离线解出 UserAssembly.dll 的 il2cpp_get_api_table 混淆表。

原理
----
`il2cpp_get_api_table(void** out)`（导出，RVA 0x650100）不做任何查表，
而是对每个槽位 i 现场算出一条混淆链，把结果写进调用方数组：

    xor eax, eax
    lea rdx, [rip + K]              ; rdx = 跳转表基址
  loop:
    cmp eax, 0x4be
    ja  tail
    mov r8d, eax
    movsxd r8, [rdx + r8*4]         ; 相对偏移
    add r8, rdx
    jmp r8                          ; -> 该槽位的解码块
  block_i:                          ; 只由 movabs / imul / add / xor / shr / lea 组成
    ...
    lea r8, [rip + C]               ; 诱饵常量
    xor r8, r9                      ; r8 = C ^ mask(i)
    mov [rcx + rax*8], r8           ; out[i] = r8
    inc rax
    cmp rax, 0x4bf
    je  tail
    ...

因此 out[i] 完全是 i 的函数，可以在**不加载 DLL** 的情况下离线算出来。
输出：sdk/api_table_rva.tsv —— i, rva（rva 相对模块基址）。
"""
import argparse
import os
import struct
import sys

try:
    import capstone
except ImportError:
    sys.exit("需要 capstone: pip install capstone")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_UA = r"D:\Games\miHoYo Launcher\games\Honkai Impact 3rd Game\BH3_Data\Native\UserAssembly.dll"
DEFAULT_OUT = os.path.join(ROOT, "sdk", "api_table_rva.tsv")

M64 = (1 << 64) - 1
SLOT_COUNT = 0x4BF


def u64(x):
    return x & M64


def s64(x):
    x &= M64
    return x - (1 << 64) if x >> 63 else x


class PE:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            head = f.read(0x1000)
        e = struct.unpack_from("<I", head, 0x3C)[0]
        coff = e + 4
        nsec = struct.unpack_from("<H", head, coff + 2)[0]
        optsz = struct.unpack_from("<H", head, coff + 16)[0]
        opt = coff + 20
        sec = opt + optsz
        self.sections = []
        for i in range(nsec):
            o = sec + i * 40
            name = head[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
            vsize, va, rawsize, raw = struct.unpack_from("<IIII", head, o + 8)
            self.sections.append((name, va, vsize, raw, rawsize))
        self.timestamp = struct.unpack_from("<I", head, e + 8)[0]
        self.image_size = struct.unpack_from("<I", head, e + 0x50)[0]
        with open(path, "rb") as f:
            self.data = f.read()

    def rva2off(self, rva):
        for _name, va, vsize, raw, rawsize in self.sections:
            if va <= rva < va + max(vsize, rawsize):
                return raw + (rva - va)
        return None

    def read(self, rva, n):
        off = self.rva2off(rva)
        if off is None:
            return b""
        return self.data[off:off + n]

    def section_of(self, rva):
        for name, va, vsize, raw, rawsize in self.sections:
            if va <= rva < va + max(vsize, rawsize):
                return name
        return None


md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True


def emulate_block(pe, start, index, max_ins=48):
    """从跳转表目标开始执行一个槽位的解码块，返回 out[index]（RVA）或 None。"""
    code = pe.read(start, 0x120)
    if len(code) < 8:
        return None
    regs = {"rax": index & M64, "r8": 0, "r9": 0, "rcx": 0}
    rip_const = None
    for ins in md.disasm(code, start):
        m = ins.mnemonic
        ops = ins.operands
        try:
            if m == "movabs":
                regs[_regname(ins, 0)] = _imm(ins, 1)
            elif m == "mov":
                d, s = ops[0], ops[1]
                if s.type == capstone.x86.X86_OP_IMM:
                    regs[_regname(ins, 0)] = u64(s.imm)
                elif s.type == capstone.x86.X86_OP_REG:
                    regs[_regname(ins, 0)] = regs.get(ins.reg_name(s.reg), 0)
                else:
                    return None
            elif m == "imul":
                if len(ops) == 3:
                    a = _val(ins, 1, regs)
                    b = _val(ins, 2, regs)
                    regs[_regname(ins, 0)] = u64(s64(a) * s64(b))
                else:
                    regs[_regname(ins, 0)] = u64(s64(regs.get(_regname(ins, 0), 0)) *
                                                 s64(_val(ins, 1, regs)))
            elif m == "add":
                regs[_regname(ins, 0)] = u64(regs.get(_regname(ins, 0), 0) + _val(ins, 1, regs))
            elif m == "sub":
                regs[_regname(ins, 0)] = u64(regs.get(_regname(ins, 0), 0) - _val(ins, 1, regs))
            elif m == "xor":
                regs[_regname(ins, 0)] = u64(regs.get(_regname(ins, 0), 0) ^ _val(ins, 1, regs))
            elif m == "and":
                regs[_regname(ins, 0)] = u64(regs.get(_regname(ins, 0), 0) & _val(ins, 1, regs))
            elif m == "or":
                regs[_regname(ins, 0)] = u64(regs.get(_regname(ins, 0), 0) | _val(ins, 1, regs))
            elif m == "shr":
                regs[_regname(ins, 0)] = u64(regs.get(_regname(ins, 0), 0) >> _val(ins, 1, regs))
            elif m == "sar":
                regs[_regname(ins, 0)] = u64(s64(regs.get(_regname(ins, 0), 0)) >> _val(ins, 1, regs))
            elif m == "shl":
                regs[_regname(ins, 0)] = u64(regs.get(_regname(ins, 0), 0) << _val(ins, 1, regs))
            elif m == "lea":
                d = _regname(ins, 0)
                src = ops[1]
                if src.type == capstone.x86.X86_OP_MEM:
                    base = ins.reg_name(src.mem.base) if src.mem.base else ""
                    if base == "rip":
                        regs[d] = u64(ins.address + ins.size + src.mem.disp)
                    else:
                        return None
                else:
                    return None
                if d == "r8":
                    rip_const = regs[d]
            elif m == "nop":
                pass
            elif m == "inc":
                r = _regname(ins, 0)
                regs[r] = u64(regs.get(r, 0) + 1)
            elif m == "jmp":
                # 所有解码块最终汇入两个共享尾巴：
                #   0x650160: xor r8, r9 ; mov [rcx+rax*8], r8 ; inc rax ; ...
                #   0x664E40: xor r9, r8 ; mov [rcx+rax*8], r9 ; jmp 0x650167
                # 两者都写 r8 ^ r9，xor 可交换，所以这里直接返回异或值。
                return u64(regs.get("r8", 0) ^ regs.get("r9", 0))
            elif m in ("je", "jne", "jz", "jnz", "jb", "ja"):
                # 解码块内部不应出现条件跳转；出现了就说明模型不适用
                return None
            else:
                return None
        except (IndexError, KeyError):
            return None
    return None


def _regname(ins, i):
    name = ins.reg_name(ins.operands[i].reg)
    # 32 位写入：清零高 32 位
    return name


def _imm(ins, i):
    return u64(ins.operands[i].imm)


def _val(ins, i, regs):
    op = ins.operands[i]
    if op.type == capstone.x86.X86_OP_IMM:
        return u64(op.imm)
    if op.type == capstone.x86.X86_OP_REG:
        return regs.get(ins.reg_name(op.reg), 0)
    return 0


def find_table_base(pe, entry_rva=0x650100):
    """从函数前导里取出跳转表基址（lea rdx, [rip + K]）。"""
    code = pe.read(entry_rva, 0x20)
    for ins in md.disasm(code, entry_rva):
        if ins.mnemonic == "lea" and ins.operands[1].type == capstone.x86.X86_OP_MEM:
            if ins.reg_name(ins.operands[1].mem.base) == "rip":
                return u64(ins.address + ins.size + ins.operands[1].mem.disp)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ua", default=os.environ.get("BH3_UA", DEFAULT_UA))
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--entry", default="0x650100")
    args = ap.parse_args()

    pe = PE(args.ua)
    entry = int(args.entry, 16)
    table = find_table_base(pe, entry)
    if not table:
        sys.exit("未找到跳转表基址")
    print("[*] PE: TimeDateStamp=0x%08X SizeOfImage=0x%08X" % (pe.timestamp, pe.image_size))
    print("[*] il2cpp_get_api_table @ RVA 0x%X, 跳转表 @ RVA 0x%X" % (entry, table))

    raw = pe.read(table, 4 * SLOT_COUNT)
    if len(raw) != 4 * SLOT_COUNT:
        sys.exit("跳转表读取不完整")

    rows = []
    bad = []
    for i in range(SLOT_COUNT):
        off = struct.unpack_from("<i", raw, i * 4)[0]
        target = u64(table + off)
        val = emulate_block(pe, target, i)
        if val is None:
            bad.append(i)
            rows.append((i, None))
        else:
            rows.append((i, val))

    ok = [r for r in rows if r[1] is not None]
    print("[+] 解出 %d/%d 个槽位" % (len(ok), SLOT_COUNT))
    if bad:
        print("[!] 未解出: %d 个 -> %s" % (len(bad), bad[:16]))

    sec_count = {}
    for i, v in ok:
        s = pe.section_of(v)
        sec_count[s] = sec_count.get(s, 0) + 1
    print("[*] 结果分布: %s" % sec_count)
    print("[*] 前 8 个: %s" % [hex(v) for _, v in ok[:8]])

    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("# il2cpp_get_api_table 解混淆结果（离线仿真）\n")
        f.write("# UA TimeDateStamp=0x%08X SizeOfImage=0x%08X\n" % (pe.timestamp, pe.image_size))
        f.write("index\trva\n")
        for i, v in rows:
            f.write("%d\t%s\n" % (i, "0x%X" % v if v is not None else "0"))
    print("[+] 已写入 " + args.out)


if __name__ == "__main__":
    main()
