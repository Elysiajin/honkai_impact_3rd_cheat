"""校验游戏目录 bh3_methods.tsv 的 mp_rva 是否可用, 并评估与 sdk 名字表 join 的覆盖率."""
import csv
import os
import struct

import dump as D

GAME = r"D:\Games\miHoYo Launcher\games\Honkai Impact 3rd Game"
METH = os.path.join(GAME, "bh3_methods.tsv")
NAMED = r"D:\My Projects\Honkai Impact\sdk\bh3_methods_named.tsv"


def main():
    d = D.Dump()
    rows = []
    with open(METH, encoding="utf-8", errors="replace") as f:
        r = csv.DictReader(f, delimiter="\t")
        for row in r:
            rows.append(row)
    print("rows", len(rows))
    nz = [x for x in rows if x["mp_rva"] not in ("0", "0x0", "")]
    print("mp_rva non-zero", len(nz))
    print("methodinfo_va non-zero", sum(1 for x in rows if x["methodinfo_va"] not in ("0", "")))

    # 抽样 200 条在 dump 里校验: MethodInfo+0x08 == UA_BASE + mp_rva
    import random
    random.seed(7)
    sample = random.sample([x for x in rows if x["methodinfo_va"] not in ("0", "")], 200)
    ok = bad = miss = 0
    for x in sample:
        try:
            va = int(x["methodinfo_va"], 16)
            rva = int(x["mp_rva"], 16)
        except ValueError:
            miss += 1
            continue
        mp = d.q(va + 8)
        if mp == D.UA_BASE + rva:
            ok += 1
        elif mp == 0:
            miss += 1
        else:
            bad += 1
    print("sample200: ok", ok, "bad", bad, "zero", miss)

    # join 名字表
    named = []
    with open(NAMED, encoding="utf-8", errors="replace") as f:
        for line in f:
            named.append(line.rstrip("\n").split("\t"))
    hdr = named[0]
    print("named header", hdr, "rows", len(named) - 1)
    i_tok = hdr.index("token")
    i_rva = hdr.index("rva")
    i_cls = hdr.index("class")
    i_mth = hdr.index("method")
    by_tok = {}
    for row in rows:
        try:
            by_tok[int(row["token"], 16)] = row
        except ValueError:
            pass
    filled = 0
    zero_before = 0
    for row in named[1:]:
        try:
            tok = int(row[i_tok], 16)
        except ValueError:
            continue
        try:
            if int(row[i_rva], 16) == 0:
                zero_before += 1
                src = by_tok.get(tok)
                if src and src["mp_rva"] not in ("0", "0x0", ""):
                    filled += 1
        except ValueError:
            pass
    print("named rva==0:", zero_before, " fillable from game tsv:", filled)


if __name__ == "__main__":
    main()
