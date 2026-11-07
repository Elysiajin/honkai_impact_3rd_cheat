"""诊断 30 个 mp_rva 不匹配样本, 并核对 game tsv 与 named tsv 的关系."""
import csv
import random

import dump as D

GAME = r"D:\Games\miHoYo Launcher\games\Honkai Impact 3rd Game\bh3_methods.tsv"
NAMED = r"D:\My Projects\Honkai Impact\sdk\bh3_methods_named.tsv"


def main():
    d = D.Dump()
    rows = list(csv.DictReader(open(GAME, encoding="utf-8", errors="replace"), delimiter="\t"))
    random.seed(7)
    sample = random.sample(rows, 200)
    bad = []
    for x in sample:
        va = int(x["methodinfo_va"], 16)
        rva = int(x["mp_rva"], 16)
        mp = d.q(va + 8)
        if mp != D.UA_BASE + rva:
            bad.append((x, mp))
    print("bad count", len(bad))
    for x, mp in bad[:10]:
        va = int(x["methodinfo_va"], 16)
        print(" cls=%-22s slot=%-6s mp_rva=%-12s mi=%s dump+8=0x%X rva=0x%X inv=%s tok=%s" % (
            x["class"], x["slot"], x["mp_rva"], x["methodinfo_va"], mp, mp - D.UA_BASE, x["inv_rva"], x["token"]))
        # 该 methodinfo 附近有没有别的字段等于 mp_rva
        for off in range(0, 0x40, 8):
            if d.q(va + off) == D.UA_BASE + int(x["mp_rva"], 16):
                print("      matched at +0x%X" % off)
        print("      inv check:", hex(d.q(va + 0x20)), "expect", hex(D.UA_BASE + int(x["inv_rva"], 16)))

    # named vs game
    named = list(csv.DictReader(open(NAMED, encoding="utf-8", errors="replace"), delimiter="\t"))
    nz = [r for r in named if r["rva"] not in ("0", "0x0", "")]
    print("\nnamed total", len(named), "nonzero", len(nz), "game rows", len(rows))
    gtok = {}
    for x in rows:
        gtok.setdefault(x["token"].lower(), []).append(x)
    hit = miss = 0
    agree = disagree = 0
    for r in nz:
        t = r["token"].lower()
        cand = gtok.get(t)
        if not cand:
            miss += 1
            continue
        hit += 1
        if any(int(c["mp_rva"], 16) == int(r["rva"], 16) for c in cand):
            agree += 1
        else:
            disagree += 1
    print("named nonzero token found in game:", hit, "not found:", miss, "| rva agree", agree, "disagree", disagree)


if __name__ == "__main__":
    main()
