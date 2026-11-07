"""在 dump 里定位 lea 候选 RVA 被写成函数指针的位置, 并反汇编 dump 表项."""
import os
import struct

import capstone

import dump as D
from api_table2 import PE, UA, MASK

UA_BASE = D.UA_BASE


def load_leas(path="api_table_lea.tsv"):
    out = []
    with open(path, encoding="utf-8") as f:
        next(f)
        for line in f:
            i, v = line.split()
            out.append((int(i), int(v, 16)))
    return out


def main():
    leas = load_leas()
    d = D.Dump()
    pe = PE(UA)

    # 1) 前 6 个 lea 的运行时 VA, 在 dump 里全局搜
    targets = [(i, v, UA_BASE + v) for i, v in leas[:6] if v]
    print("=== search lea targets as qwords")
    for i, v, va in targets:
        key = struct.pack("<Q", va)
        found = []
        for (s, e, path, prot, typ, pp) in d.regions:
            if not path or not os.path.exists(path):
                continue
            blob = d._blob(path)
            off = blob.find(key)
            while off >= 0:
                found.append(s + off)
                if len(found) > 20:
                    break
                off = blob.find(key, off + 1)
            if len(found) > 20:
                break
        print("  slot %d rva 0x%-10X hits %d %s" % (i, v, len(found), [hex(x) for x in found[:6]]))

    # 2) dump 表项 0x7ff8d4d94440 处是什么
    print("\n=== disasm dump table entry targets")
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    TABLE_VA = 0x7FF8C5CBA320
    for k in range(4):
        va = d.q(TABLE_VA + k * 8)
        print("  entry %d -> 0x%X (rva 0x%X)" % (k, va, va - UA_BASE))
        b = d.read(va, 48)
        print("    bytes", b.hex())
        for ins in md.disasm(b, va):
            print("      %X  %-20s %s %s" % (ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str))
            if ins.mnemonic in ("ret", "jmp") and ins.address > va:
                break

    # 3) 静态文件里 RVA 0x114F4440 的字节, 与 dump 比对
    print("\n=== static vs dump @ rva 0x114F4440")
    st = pe.read(0x114F4440, 32)
    du = d.read(UA_BASE + 0x114F4440, 32)
    print("  static", st.hex())
    print("  dump  ", du.hex())
    print("  same", st == du)

    # 4) lea 目标 rva 处静态 vs dump
    print("\n=== static vs dump @ lea[0] rva 0x463AAE0")
    st = pe.read(0x463AAE0, 32)
    du = d.read(UA_BASE + 0x463AAE0, 32)
    print("  static", st.hex())
    print("  dump  ", du.hex())
    print("  same", st == du)


if __name__ == "__main__":
    main()
