# -*- coding: utf-8 -*-
"""Decode method parameter signature from invoker thunk.

invoker(MethodInfo* in rcx, void* this/obj in rdx, void** params in r8, void* ret in r9)
参数指针从 [r9+disp] 取出，再解引用得到真实类型。
"""
import re
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_64

UA = r"D:\Games\miHoYo Launcher\games\Honkai Impact 3rd Game\BH3_Data\Native\UserAssembly.dll"

ARG_RE = re.compile(r"\[r9(?: \+ (0x[0-9a-f]+|\d+))?\]")
DEREF_RE = re.compile(r"\[(r[a-z0-9]+)\]")


def sections(path):
    b = open(path, "rb").read()
    h = b[:0x1000]
    e = struct.unpack_from("<I", h, 0x3C)[0]
    coff = e + 4
    n = struct.unpack_from("<H", h, coff + 2)[0]
    optsz = struct.unpack_from("<H", h, coff + 16)[0]
    opt = coff + 20
    sec = opt + optsz
    out = []
    for i in range(n):
        o = sec + i * 40
        nm = h[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        vs, va, rs, ra = struct.unpack_from("<IIII", h, o + 8)
        out.append((nm, va, vs, ra, rs))
    return b, out


def r2o(secs, rva):
    for nm, va, vs, ra, rs in secs:
        if va <= rva < va + max(vs, rs):
            return ra + (rva - va)
    return None


def _type_from_ins(ins):
    s = ins.op_str
    if ins.mnemonic in ("movss",):
        return "float"
    if ins.mnemonic in ("movsd",):
        return "double"
    if ins.mnemonic == "mov":
        if "xmm" in s:
            return "float"
        for w in ("eax", "ebx", "ecx", "edx", "esi", "edi", "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d"):
            if w in s:
                return "int"
        return "object"
    if ins.mnemonic == "movzx":
        if "byte" in s:
            return "byte"
        if "word" in s:
            return "short"
    if ins.mnemonic == "lea":
        return "ptr"
    return None


def invoke_sig(b, secs, inv, md):
    """返回参数类型元组；解析失败返回 None。"""
    if not inv or inv < 0x1000:
        return None
    o = r2o(secs, inv)
    if o is None:
        return None
    pending = {}   # reg -> (disp, pending_type)
    got = {}
    insns = list(md.disasm(b[o:o + 0x160], inv))
    for idx, ins in enumerate(insns):
        s = ins.op_str
        if ins.mnemonic in ("call", "jmp"):
            break
        m = ARG_RE.search(s)
        if m and ins.mnemonic == "mov":
            d = int(m.group(1), 0) if m.group(1) else 0
            reg = ins.op_str.split(",")[0].strip()
            pending[reg] = d
            got.setdefault(d, "object")
            continue
        dm = DEREF_RE.search(s)
        if dm and dm.group(1) in pending:
            t = _type_from_ins(ins)
            if t:
                got[pending[dm.group(1)]] = t
            del pending[dm.group(1)]
    if not got:
        return ()
    return tuple(got[k] for k in sorted(got))


def main():
    b, secs = sections(UA)
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    for inv in (0x342AA0, 0x342BB0, 0x345350, 0x342C20, 0x343160, 0x349B00, 0x3505E0):
        print(hex(inv), invoke_sig(b, secs, inv, md))


if __name__ == "__main__":
    main()
