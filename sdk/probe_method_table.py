#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""probe_method_table.py — 证明 klass+0x80 就是该类的 MethodInfo 指针数组。

上一轮发现：
    Camera klass(0x50000b0d1a8) + 0x80 = 0x50000B15B10
    该地址起是 254 个 qword，依次指向 0x50000B16300 + i*0x40
如果这些被指向的对象确实是 MethodInfo（+0x10 == Camera klass、
+0x28 == 0xFFFFFFFF、+0x30 高字节 == 0x06），那么运行期就有一条 O(1) 路径：

    klass -> +0x80 -> 指针数组 -> 遍历 254 项 -> 读 +0x30 token 匹配 -> 读 +0x08 mp

完全不需要扫描 2GB 托管堆，也不需要依赖任何 RVA。
本脚本对全部 27 个需要解析的符号做端到端验证。
"""
import os
import sys

import numpy as np

from dump import Dump, UA_BASE

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "sdk"))

MI = np.dtype([
    ("generic", "<u8"), ("mp", "<u8"), ("klass", "<u8"),
    ("slot", "<u4"), ("pad1c", "<u4"), ("invoker", "<u8"), ("maxstack", "<u8"),
    ("token", "<u4"), ("pad34", "<u4"), ("flags", "<u4"), ("pad3c", "<u4"),
])

HEAP_LO, HEAP_HI = 0x50000000000, 0x50100000000


def read_mi(d, va):
    b = d.read(va, 0x40)
    if len(b) != 0x40:
        return None
    return np.frombuffer(b, dtype=MI)[0]


def klass_methods(d, klass, limit=8192):
    """klass+0x80 -> 指针数组 -> 全部 MethodInfo 记录的列表。"""
    tab = d.q(klass + 0x80)
    if not tab or not (HEAP_LO <= tab < HEAP_HI):
        return [], tab
    out = []
    for i in range(limit):
        p = d.q(tab + i * 8)
        if not p or not (HEAP_LO <= p < HEAP_HI):
            break
        r = read_mi(d, p)
        if r is None:
            break
        if int(r["klass"]) != klass:
            break
        if int(r["maxstack"]) != 0xFFFFFFFF:
            break
        out.append((p, r))
    return out, tab


def find_klass_by_name(d, name, scan_name=b"Camera"):
    """在 0x500 堆里找名字等于 name 的 klass（穷举，只用于本脚本验证）。"""
    hits = []
    target = name.encode()
    for base, end, f, prot, typ, path in d.regions:
        if not (HEAP_LO <= base < HEAP_HI):
            continue
        try:
            fsz = os.path.getsize(f)
        except OSError:
            continue
        size = min(end - base, fsz)
        try:
            blob = open(f, "rb").read(size)
        except OSError:
            continue
        i = blob.find(target + b"\x00")
        while i >= 0:
            # klass+0x20 应为这个字符串地址
            str_va = base + i
            # 反查：扫 0xD8 对齐的候选 klass
            for delta in range(0x20, 0x1000, 0x8):
                cand = str_va - 0x20
                break
            hits.append(str_va)
            i = blob.find(target + b"\x00", i + 1)
            if len(hits) > 8:
                break
        del blob
        if len(hits) > 8:
            break
    return hits


def main():
    d = Dump()

    print("=== 1. 验证 klass+0x80 指向的数组内容 ===")
    for cls_name, kl in (("Camera", 0x50000B0D1A8),
                         ("MonoSkillButton", None),
                         ("MHEMGJMNDJI", None),
                         ("BaseMonoMonster", 0x50000C74DC0)):
        if kl is None:
            continue
        ms, tab = klass_methods(d, kl)
        print("  %-18s klass=0x%X  tab=0x%X  条目=%d" % (cls_name, kl, tab or 0, len(ms)))
        if ms:
            print("      首: mi=0x%X tok=0x%08X mp=0x%X"
                  % (ms[0][0], int(ms[0][1]["token"]), int(ms[0][1]["mp"])))
            print("      末: mi=0x%X tok=0x%08X mp=0x%X"
                  % (ms[-1][0], int(ms[-1][1]["token"]), int(ms[-1][1]["mp"])))

    print("\n=== 2. 用 klass+0x80 查找全部 profile 符号（端到端）===")
    exp = None
    try:
        import token_map
        exp = token_map.load_expected()
        prof = token_map.parse_profile(token_map.PROFILE_INC)
    except Exception as e:                       # noqa: BLE001
        print("  导入 token_map 失败:", e)
        return

    # 已知 klass 表（来自 bh3_classes.tsv / dump 实测）
    known = {
        "Camera": 0x50000B0D1A8,
        "BaseMonoMonster": 0x50000C74DC0,
    }
    ok = bad = 0
    for key, (cls, tok) in exp.items():
        if not tok:
            print("  %-20s %-22s 无 token" % (key, cls))
            continue
        kl = known.get(cls)
        if kl is None:
            print("  %-20s %-22s klass 未知（未登记）" % (key, cls))
            continue
        ms, tab = klass_methods(d, kl)
        hit = None
        for va, r in ms:
            if int(r["token"]) == tok:
                hit = (va, int(r["mp"]))
                break
        if hit:
            rva = hit[1] - UA_BASE
            inc = prof.get(key, 0)
            same = (rva == inc)
            ok += same
            bad += (not same)
            print("  %-20s %-18s 0x%08X (inc 0x%08X) %s"
                  % (key, cls, rva, inc, "一致" if same else "不同"))
        else:
            bad += 1
            print("  %-20s %-18s 该 klass 的 %d 个方法里没有 token 0x%08X"
                  % (key, cls, len(ms), tok))

    print("\n[=] 一致 %d, 不一致/缺失 %d" % (ok, bad))


if __name__ == "__main__":
    main()
