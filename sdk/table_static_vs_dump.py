"""决定性对比: 静态 .rdata API 表 (rva 0xEB9BE8) vs dump 里同一位置的运行期内容.

若 194 个不一致槽位在 dump 中被改写, 即证明: .rdata 表是初始模板, 加载期/运行期被补丁.
"""
import struct

import dump as D
from api_table2 import PE, UA, NSLOT

RVA_TABLE = 0xEB9BE8
PREF_BASE = 0x180000000
N = NSLOT


def main():
    pe = PE(UA)
    d = D.Dump()

    st = [v - PREF_BASE for v in struct.unpack_from("<%dQ" % N, pe.read(RVA_TABLE, N * 8))]
    du_raw = list(struct.unpack_from("<%dQ" % N, d.read(D.UA_BASE + RVA_TABLE, N * 8)))
    du = [v - D.UA_BASE for v in du_raw]

    print("static[0..3] ", [hex(x) for x in st[:4]])
    print("dump  [0..3] ", [hex(x) for x in du_raw[:4]], "-> rva", [hex(x) for x in du[:4]])

    diff = [i for i in range(N) if st[i] != du[i]]
    print("diff count:", len(diff))
    for i in diff[:15]:
        print("  slot %-4d static 0x%-10X dump 0x%-10X" % (i, st[i], du[i]))

    # dump 表值是否落在 il2cpp
    inimg = sum(1 for v in du if 0x3332000 <= v < 0x12674200)
    print("dump table entries inside il2cpp:", inimg, "/", N)
    print("dump unique:", len(set(du)))

    # lea 结果与 dump 表比对
    leas = []
    with open("api_table_lea.tsv", encoding="utf-8") as f:
        next(f)
        for line in f:
            leas.append(int(line.split()[1], 16))
    print("lea == static:", sum(1 for a, b in zip(leas, st) if a == b))
    print("lea == dump  :", sum(1 for a, b in zip(leas, du) if a == b))


if __name__ == "__main__":
    main()
