"""按 int token join named tsv 与 game tsv, 统计可用覆盖率."""
import csv

GAME = r"D:\Games\miHoYo Launcher\games\Honkai Impact 3rd Game\bh3_methods.tsv"
NAMED = r"D:\My Projects\Honkai Impact\sdk\bh3_methods_named.tsv"


def itok(s):
    s = s.strip().lower()
    if s.startswith("0x"):
        s = s[2:]
    return int(s, 16)


def main():
    game = list(csv.DictReader(open(GAME, encoding="utf-8", errors="replace"), delimiter="\t"))
    named = list(csv.DictReader(open(NAMED, encoding="utf-8", errors="replace"), delimiter="\t"))
    print("game rows", len(game), "named rows", len(named))

    by_tok = {}
    for g in game:
        by_tok.setdefault(itok(g["token"]), []).append(g)

    nz = [r for r in named if itok(r["rva"]) != 0]
    print("named nonzero", len(nz))

    hit = agree = 0
    for r in nz:
        t = itok(r["token"])
        c = by_tok.get(t)
        if c:
            hit += 1
            if any(itok(x["mp_rva"]) == itok(r["rva"]) for x in c):
                agree += 1
    print("named-nonzero matched by token:", hit, "/", len(nz), " rva agree:", agree)

    # 反向: game token 是否都在 named 里
    ntok = set(itok(r["token"]) for r in named)
    ing = sum(1 for g in game if itok(g["token"]) in ntok)
    print("game tokens present in named:", ing, "/", len(game))


if __name__ == "__main__":
    main()
