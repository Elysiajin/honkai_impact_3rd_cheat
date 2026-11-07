#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""token_map.py — 在 CE dump 上真跑一遍「按 (类名, token) 反查方法地址」。

这正是 overlay 运行期要做的同一件事：
  1. 枚举 IL2CPP 托管堆里的全部 MethodInfo 记录
  2. 用 methodToken(+0x30) + 类名(+0x10 klass -> +0x20 name) 建索引
  3. 按 (class, token) 取回 methodPointer(+0x08)
  4. 与 bh3_profile.inc 里的硬编码 rva 比对

两个必须踩准的点（都是实测得来的，不是猜的）：
  * **记录不按 0x40 对齐**。Camera 的方法记录在 0x50000B193B8，
    相对 region 基址偏移 0xB183B8，%0x40 == 0x38。所以只能 8 字节步进扫描，
    再用「+0x28 == 0xFFFFFFFF」+「token 高字节 == 0x06」+「mp 落在 UA」
    三重过滤把记录挑出来。sdk/bh3_sdk.h 的 all_methods() 也是 p += 8。
  * **token 只在单个 module 内唯一**。0x06000264 在 dump 里同时属于
    `Camera` 和 `Scrollbar`；0x0600020A 属于 `Calltype_zfb_mouseMove`。
    所以必须同时约束类名，否则会拿错函数。

MethodInfo 布局（sdk/bh3_sdk.h + dump 实测）:
    +0x00 genericMethod  +0x08 methodPointer  +0x10 klass  +0x18 slot
    +0x20 invoker        +0x28 maxStack(0xFFFFFFFF)  +0x30 token  +0x38 flags
"""
import argparse
import os
import re
import sys

import numpy as np

from dump import Dump, UA_BASE, UA_SIZE

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOKENS_INC = os.path.join(ROOT, "overlay", "bh3_tokens.inc")
PROFILE_INC = os.path.join(ROOT, "overlay", "bh3_profile.inc")

UA_LO, UA_HI = UA_BASE + 0x1000, UA_BASE + UA_SIZE
MAXSTACK = 0x00000000FFFFFFFF
HEAP_LO, HEAP_HI = 0x50000000000, 0x50100000000


def parse_tokens(path):
    out = []
    for line in open(path, encoding="utf-8"):
        m = re.match(r"\s*0x([0-9A-Fa-f]+)u?\s*,\s*//\s*(\S+)", line)
        if m:
            out.append((m.group(2), int(m.group(1), 16)))
    return out


def parse_profile(path):
    text = open(path, encoding="utf-8").read()
    pat = re.compile(r'\{\s*"([a-z0-9_]+)"\s*,\s*0x([0-9A-Fa-f]+)u?\s*,\s*"([0-9a-f]*)"'
                     r'\s*,\s*(\d+)\s*,\s*(true|false)\s*\}')
    return {m.group(1): int(m.group(2), 16) for m in pat.finditer(text)}


def load_expected():
    """key -> (期望类名, token)"""
    sys.path.insert(0, os.path.join(ROOT, "sdk"))
    import importlib
    gt = importlib.import_module("gen_tokens")
    out = {}
    for key, tok in parse_tokens(TOKENS_INC):
        pair = gt.SYMBOLS.get(key)
        out[key] = (pair[0] if pair else "", tok)
    return out


def spans(d, cap_mb):
    out = []
    for base, end, f, prot, typ, path in d.regions:
        if typ == "image" or "r" not in prot.lower():
            continue
        size = end - base
        if size > (cap_mb << 20):
            continue
        out.append((base, size, f))
    return out


def klass_name(d, kl, cache):
    if kl in cache:
        return cache[kl]
    p = d.q(kl + 0x20)
    n = d.cstr(p, 64) if p else ""
    cache[kl] = n
    return n


def scan(d, list_):
    """返回 [(mi_va, mp_va, klass, token)]"""
    recs = []
    for base, size, f in list_:
        try:
            fsz = os.path.getsize(f)
        except OSError:
            continue
        avail = min(size, fsz) & ~7
        n = avail // 8
        if n < 8:
            continue
        q = np.memmap(f, dtype=np.uint64, mode="r", shape=(n,))
        cnt = n - 6
        if cnt <= 0:
            del q
            continue
        mp = q[1:1 + cnt]
        kl = q[2:2 + cnt]
        ms = q[5:5 + cnt]
        tok = q[6:6 + cnt] & 0xFFFFFFFF
        ok = ((mp >= UA_LO) & (mp < UA_HI) & (ms == MAXSTACK) &
              ((tok >> 24) == 6) & (kl >= HEAP_LO) & (kl < HEAP_HI))
        sel = np.nonzero(ok)[0]
        for i in sel:
            recs.append((base + int(i) * 8, int(mp[i]), int(kl[i]), int(tok[i])))
        del q, mp, kl, ms, tok
    return recs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cap-mb", type=int, default=400)
    args = ap.parse_args()

    d = Dump()
    sp = spans(d, args.cap_mb)
    print("[*] 候选区 %d 个, 合计 %.1f MB" % (len(sp), sum(s for _, s, _ in sp) / 1048576.0))
    recs = scan(d, sp)
    print("[+] MethodInfo 记录 %d 条" % len(recs))

    # (klass, token) 去重
    uniq = {}
    for mi_va, mp_va, kl, tok in recs:
        uniq.setdefault((kl, tok), (mi_va, mp_va))
    print("[+] (klass, token) 唯一 %d 个" % len(uniq))

    exp = load_expected()
    prof = parse_profile(PROFILE_INC)
    names = {}
    hit = miss = agree = 0
    print("\n%-20s %-22s %-11s %-11s %-9s %s"
          % ("key", "class", "dump_rva", "inc_rva", "delta", "状态"))
    for key, (cls, tok) in exp.items():
        inc_rva = prof.get(key, 0)
        if not tok:
            print("%-20s %-22s %-11s 0x%08X %-9s 无 token"
                  % (key, cls + "(未映射)", "-", inc_rva, "-"))
            continue
        found = None
        for (kl, t), (mi_va, mp_va) in uniq.items():
            if t == tok and klass_name(d, kl, names) == cls:
                found = (mi_va, mp_va, kl)
                break
        if not found:
            print("%-20s %-22s %-11s 0x%08X %-9s 未找到" % (key, cls, "-", inc_rva, "-"))
            miss += 1
            continue
        mi_va, mp_va, kl = found
        rva = mp_va - UA_BASE
        dlt = rva - inc_rva
        if dlt == 0:
            agree += 1
        print("%-20s %-22s 0x%08X 0x%08X %+-9d %s"
              % (key, cls, rva, inc_rva, dlt, "一致" if dlt == 0 else "不同"))
        hit += 1

    print("\n[=] 找到 %d / 需 %d, 与 profile 一致 %d" % (hit, hit + miss, agree))
    return 0 if (miss == 0 and agree == hit) else 2


if __name__ == "__main__":
    sys.exit(main())
