"""在 CE dump 中定位 il2cpp API 表 (1215 个函数指针).

假设: UserAssembly.dll 启动时调用 il2cpp_get_api_table(out) 把 1215 个真实
函数指针写进某个全局数组, dump 里应该能直接找到这段连续指针.

策略: 只扫 UserAssembly.dll 映射的 rw- region, 用 numpy 向量化找
      "连续 >=1100 个 qword 全部落在 il2cpp 代码节" 的窗口.
"""
import csv
import os
import struct

import numpy as np

DUMP_DIR = r"D:\Cheat Table\CE Search"
UA_BASE = 0x7FF8C38A0000
IL_LO = 0x7FF8C6BD2000
IL_HI = 0x7FF8D5F13000
NEED = 1215


def regions():
    out = []
    with open(os.path.join(DUMP_DIR, "regions.csv"), encoding="utf-8", errors="replace") as f:
        for r in csv.DictReader(f):
            try:
                b = int(r["base"], 16)
                sz = int(r["size"], 16)
            except (ValueError, KeyError):
                continue
            out.append((b, b + sz, r["file"], r["protection"], r["type"], r.get("path", "")))
    out.sort()
    return out


def scan():
    hits = []
    for b, e, path, prot, typ, mpath in regions():
        if typ != "image" or "UserAssembly" not in (mpath or ""):
            continue
        if not prot.startswith("rw"):
            continue
        if e - b < NEED * 8:
            continue
        data = open(path, "rb").read()
        n = len(data) // 8
        if n < NEED:
            continue
        arr = np.frombuffer(data, dtype="<u8", count=n)
        mask = ((arr >= IL_LO) & (arr < IL_HI)).astype(np.int32)
        # 前缀和, 快速判断长度 NEED 的窗口
        cs = np.concatenate(([0], np.cumsum(mask)))
        win = cs[NEED:] - cs[:-NEED]
        idx = np.nonzero(win >= NEED - 40)[0]
        for i in idx:
            hits.append((int(win[i]), b + int(i) * 8, path))
    hits.sort(reverse=True)
    return hits


def trim(arr_va, arr, lo=IL_LO, hi=IL_HI):
    """向前后扩展, 找出真正连续 il2cpp 指针的最长区间"""
    s = 0
    while s < len(arr) and lo <= arr[s] < hi:
        s += 1
    return s


def main():
    hits = scan()
    print("candidates:", len(hits))
    for cnt, va, path in hits[:20]:
        print("  %4d/%d  va=0x%012X  rva=0x%X  %s" % (cnt, NEED, va, va - UA_BASE, os.path.basename(path)))
    if not hits:
        return
    cnt, va, path = hits[0]
    data = open(path, "rb").read()
    off = va - int(os.path.basename(path)[:16], 16)
    arr = np.frombuffer(data, dtype="<u8", count=NEED + 8, offset=off - 8)
    print("\nbest va=0x%X rva=0x%X" % (va, va - UA_BASE))
    print("prev qword:", hex(int(arr[0])))
    body = arr[1:]
    print("first 8:", [hex(int(x)) for x in body[:8]])
    print("last 4 :", [hex(int(x)) for x in body[-4:]])
    inimg = sum(1 for x in body if IL_LO <= x < IL_HI)
    print("in il2cpp: %d/%d" % (inimg, NEED))
    uniq = len(set(int(x) for x in body))
    print("unique:", uniq)


if __name__ == "__main__":
    main()
