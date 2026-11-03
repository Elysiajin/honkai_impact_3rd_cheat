#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bh3_find.py — 查 SDK 表的小工具（不需要游戏在跑）

用法:
  python bh3_find.py cls  <关键字> [关键字2 ...]      # 按名字查类（tdi / 父类 / 类地址 / 方法数）
  python bh3_find.py m    <类名>  [--n 40]            # 列某个类的方法（slot / mp_rva / token）
  python bh3_find.py mtdi <tdi>   [--n 40]            # 按 tdi 精确列方法（类名不唯一，推荐）
  python bh3_find.py rva  <mp_rva>                    # 反查：这个 RVA 属于哪个类的哪个方法
  python bh3_find.py name <子串>  [--n 60]            # 在字符串池里搜名字（含字段名）
  python bh3_find.py stat                             # 概览

数据来源: 游戏目录下的 bh3_classes.tsv / bh3_methods.tsv / bh3_names.tsv
           用 BH3_SDK_DIR 环境变量或改下面的 DIR 指向它们。
"""
import os
import sys
import collections

DIR = os.environ.get(
    "BH3_SDK_DIR",
    r"D:\Games\miHoYo Launcher\games\Honkai Impact 3rd Game",
)


def load(name):
    p = os.path.join(DIR, name)
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        head = f.readline().rstrip("\n").split("\t")
        rows = [dict(zip(head, l.rstrip("\n").split("\t"))) for l in f if l.strip()]
    return head, rows


def table():
    hc, cls = load("bh3_classes.tsv")
    hm, met = load("bh3_methods.tsv")
    hn, nam = load("bh3_names.tsv")
    return cls, met, nam


def argval(flag, default):
    """--flag <int>；缺省返回 default。"""
    if flag in sys.argv:
        i = sys.argv.index(flag)
        if i + 1 < len(sys.argv):
            try:
                return int(sys.argv[i + 1], 0)
            except ValueError:
                return default
    return default


def rest(after=2):
    """位置参数：跳过所有 --flag 及其取值（避免 '--n 6' 的 6 被当成关键字）。"""
    out = []
    i = after
    while i < len(sys.argv):
        a = sys.argv[i]
        if a.startswith("--"):
            i += 2
            continue
        out.append(a)
        i += 1
    return out


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    cls, met, nam = table()
    cnt = collections.Counter(r.get("tdi") or "" for r in met)
    has_tdi = bool(met and met[0].get("tdi"))
    if not has_tdi:
        # .4 版方法表没有 tdi 列，只能按类名统计（类名不唯一，仅供参考）
        cnt = collections.Counter(r.get("class") or "" for r in met)
    cmd = sys.argv[1]

    if cmd == "stat":
        print("classes %d   methods %d   names %d" % (len(cls), len(met), len(nam)))
        print("class cols :", list(cls[0].keys()))
        print("method cols:", list(met[0].keys()))
        top = collections.Counter(r.get("class") or "" for r in met).most_common(15)
        print("top classes by method count:")
        for n, c in top:
            print("   %-40s %d" % (n, c))
        return

    if cmd == "cls":
        kws = [k.lower() for k in rest(2)]
        hit = [r for r in cls if all(k in r["name"].lower() for k in kws)]
        key = (lambda r: -int(cnt.get(r["tdi"]) or cnt.get(r["name"]) or 0)) if has_tdi \
              else (lambda r: -int(cnt.get(r["name"]) or 0))
        hit.sort(key=key)
        print("%-8s %-10s %-40s %s" % ("tdi", "methods", "name", "parent"))
        for r in hit[:argval("--n", 80)]:
            print("%-8s %-10s %-40s %s" % (r["tdi"], cnt.get(r["tdi"], 0), r["name"], r.get("parent", "")))
        print("total %d" % len(hit))
        return

    if cmd == "m":
        keys = rest(2)
        rows = [r for r in met if r.get("class") in keys or r.get("tdi") in keys]
        print("%-8s %-10s %-12s %-12s %-12s %s" % ("tdi", "class", "slot", "mp_rva", "inv_rva", "token"))
        for r in rows[:argval("--n", 40)]:
            print("%-8s %-10s %-12s %-12s %-12s %s"
                  % (r.get("tdi", ""), r["class"], r["slot"], r["mp_rva"], r["inv_rva"], r["token"]))
        print("total %d" % len(rows))
        return

    if cmd == "mtdi":
        want = set(rest(2))
        rows = [r for r in met if r.get("tdi") in want]
        tmp = {r["tdi"]: r["name"] for r in cls}
        print("%-8s %-30s %-12s %-12s %-12s %s" % ("tdi", "class", "slot", "mp_rva", "inv_rva", "token"))
        for r in rows[:argval("--n", 40)]:
            print("%-8s %-30s %-12s %-12s %-12s %s"
                  % (r["tdi"], tmp.get(r["tdi"], "?"), r["slot"], r["mp_rva"], r["inv_rva"], r["token"]))
        print("total %d" % len(rows))
        return

    if cmd == "rva":
        want = rest(2)
        nm = {r["tdi"]: r["name"] for r in cls}
        for r in met:
            if r["mp_rva"] in want or r.get("inv_rva") in want:
                print("class=%-30s tdi=%-8s slot=%-8s mp_rva=%s inv_rva=%s token=%s klass_va=%s"
                      % (nm.get(r.get("tdi", ""), r["class"]), r.get("tdi", ""), r["slot"],
                         r["mp_rva"], r["inv_rva"], r["token"], r.get("methodinfo_va", "")))
        return

    if cmd == "name":
        subs = [s.lower() for s in rest(2)]
        hit = [r for r in nam if all(s in r["string"].lower() for s in subs)]
        for r in hit[:argval("--n", 60)]:
            print("%-10s %s" % (r["key"], r["string"]))
        print("total %d" % len(hit))
        return

    print(__doc__)


if __name__ == "__main__":
    main()
