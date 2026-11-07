"""鎸囩汗璇嗗埆 1215 涓?il2cpp API 妲戒綅.

绛栫暐: 鐢ㄥ凡鐭ョ殑 IL2CPP 缁撴瀯甯冨眬鍋氱壒寰佸尮閰?
- 鍙跺瓙 getter: `mov rax,[rcx+OFF]; ret` -> 鍙傛暟鏄粨鏋勪綋鎸囬拡, 杩斿洖璇ュ瓧娈?- `mov eax,[rcx+OFF]; ret` -> 32 浣嶅瓧娈?- 閫氳繃 Il2CppClass/MethodInfo 鐨勫凡鐭ュ亸绉诲弽鎺?API 鍚嶅瓧
"""
import json
import struct

import capstone
from capstone import CS_ARCH_X86, CS_MODE_64

import dump as D
from api_table2 import PE, UA, NSLOT

RVA_TABLE = 0xEB9BE8
PREF = 0x180000000


def load_api(pe):
    st = [v - PREF for v in struct.unpack_from("<%dQ" % NSLOT, pe.read(RVA_TABLE, NSLOT * 8))]
    return st


def describe(md, pe, rva):
    """杩斿洖 (kind, detail): 瀵瑰彾瀛愬嚱鏁扮粰鍑?getter 鎻忚堪."""
    code = pe.read(rva, 0x40)
    ins = list(md.disasm(code, rva))
    if not ins:
        return None, None
    # 绾?getter: mov rax/ eax, [rcx+off] ; ret
    if len(ins) >= 2 and ins[0].mnemonic == "mov" and ins[-1].mnemonic == "ret" and len(ins) == 2:
        op0, op1 = ins[0].operands
        if op0.type == capstone.x86.X86_OP_REG and op1.type == capstone.x86.X86_OP_MEM:
            mem = op1.mem
            if mem.base and ins[0].reg_name(mem.base) == "rcx" and not mem.index:
                return "getter", (ins[0].reg_name(op0.reg), mem.disp)
    return None, None


def main():
    pe = PE(UA)
    md = capstone.Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    tbl = load_api(pe)

    getters = {}
    for i, rva in enumerate(tbl):
        k, d = describe(md, pe, rva)
        if k:
            getters.setdefault(d, []).append(i)

    print("pure getters:", sum(len(v) for v in getters.values()), "distinct offsets:", len(getters))
    for (reg, off), idxs in sorted(getters.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        print("  %-4s [rcx+0x%-3X]  slots %s" % (reg, off, idxs[:6]))

    json.dump({"%s_%X" % (r, o): v for (r, o), v in getters.items()},
              open("api_getters.json", "w"), indent=1)


if __name__ == "__main__":
    main()

