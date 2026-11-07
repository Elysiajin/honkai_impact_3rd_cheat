"""验证: 离线 lea 解混淆结果 == 静态 .rdata 里的 1215 项 API 表 (rva 0xEB9BE8, 带 ImageBase 0x180000000)."""
import struct

from api_table2 import PE, UA

RVA_TABLE = 0xEB9BE8
PREF_BASE = 0x180000000
N = 1215


def main():
    pe = PE(UA)
    ents = list(struct.unpack_from("<%dQ" % N, pe.read(RVA_TABLE, N * 8)))
    table = [v - PREF_BASE for v in ents]

    leas = []
    with open("api_table_lea.tsv", encoding="utf-8") as f:
        next(f)
        for line in f:
            i, v = line.split()
            leas.append(int(v, 16))

    same = sum(1 for a, b in zip(table, leas) if a == b)
    print("static table entries %d, lea %d, identical %d" % (len(table), len(leas), same))
    diff = [(i, table[i], leas[i]) for i in range(N) if table[i] != leas[i]]
    print("mismatches %d:" % len(diff))
    for i, a, b in diff[:10]:
        print("  slot %d table 0x%X lea 0x%X" % (i, a, b))

    secs = {}
    for v in table:
        s = pe.sec_of(v)
        secs[s[0] if s else "?out"] = secs.get(s[0] if s else "?out", 0) + 1
    print("sections:", secs)
    print("min 0x%X max 0x%X" % (min(table), max(table)))
    # 检查表项是否单调 / 有序
    asc = sum(1 for i in range(N - 1) if table[i] < table[i + 1])
    print("ascending pairs %d / %d" % (asc, N - 1))
    # 是否有重复
    print("unique", len(set(table)))


if __name__ == "__main__":
    main()
