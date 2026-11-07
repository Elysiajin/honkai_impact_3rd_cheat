"""离线验证：能否在堆里找到「类名 -> klass」的通路。

思路：
  1. RVA_MSCORLIB_CLASSES 指向一个 klass 指针数组，第 0 项 klass 的名字是 "<Module>"。
  2. 每个 module 应该都有一个这样的数组。所以：扫 0x500 堆，找出所有
     「指向名字为 <Module> 的 klass 的 qword」，这些就是各 module 的类数组起点。
  3. 在找到的数组里按名字查 AvatarManager / MonsterManager / MonoSkillButton 等。
  4. 再从 klass+0x80 取方法数组，按 token 定位 MethodInfo。
"""
import struct
import sys

import numpy as np

from dump import Dump, UA_BASE

d = Dump()

HEAPS = [
    (0x50000001000, 0x3BF5000),
    (0x50015E01000, 0x778000),
    (0x50016D01000, 0x670000),
]
HEAP_LO = 0x50000000000
HEAP_HI = 0x50040000000


def klass_name(k):
    if not (HEAP_LO <= k < HEAP_HI):
        return None
    try:
        p = d.q(k + 0x20)
        if not (HEAP_LO <= p < HEAP_HI):
            return None
        s = d.cstr(p, 128)
        return s or None
    except Exception:
        return None


print("[1] 读 mscorlib 类数组，找 <Module> 的位置")
arr0 = d.q(UA_BASE + 0x25554D0)
print("    mscorlib array = 0x%X" % arr0)
for i in range(3):
    k = d.q(arr0 + i * 8)
    print("      [%d] klass=0x%X name=%r" % (i, k, klass_name(k)))
module_klass = d.q(arr0)
print("    <Module> klass = 0x%X" % module_klass)

print()
print("[2] 扫堆找所有指向 <Module> klass 的 qword")
found = []
for base, size in HEAPS:
    chunk = 1 << 20
    off = 0
    while off < size:
        n = min(chunk, size - off)
        try:
            b = d.read(base + off, n)
        except Exception:
            break
        if len(b) < 8:
            break
        a = np.frombuffer(b[: len(b) // 8 * 8], dtype=np.uint64)
        hit = np.nonzero(a == module_klass)[0]
        for h in hit:
            found.append(base + off + int(h) * 8)
        off += n
print("    hits = %d" % len(found))
for p in found[:20]:
    print("      0x%X  (arr0=0x%X, delta=%+d)" % (p, arr0, p - arr0))

print()
print("[3] 把这些位置当作类数组起点，读长度与首项")
arrays = []
for p in found:
    n = 0
    ok = True
    while n < 500000:
        try:
            k = d.q(p + n * 8)
        except Exception:
            ok = False
            break
        if not (HEAP_LO <= k < HEAP_HI):
            break
        nm = klass_name(k)
        if nm is None:
            break
        n += 1
    if n >= 4:
        arrays.append((p, n))
arrays.sort(key=lambda x: -x[1])
print("    candidate arrays with len>=4: %d" % len(arrays))
for p, n in arrays[:20]:
    k0 = d.q(p)
    print("      0x%X  len=%d  first=%r" % (p, n, klass_name(k0)))

print()
print("[4] 在所有候选数组里按名字查目标类")
TARGETS = ["AvatarManager", "MonsterManager", "MonoSkillButton", "MHEMGJMNDJI",
           "BaseMonoMonster", "BaseMonoAvatar", "BaseMonoAnimatorEntity",
           "MonoFuka", "Singleton`1", "Camera"]
loc = {}
for p, n in arrays:
    for i in range(n):
        k = d.q(p + i * 8)
        nm = klass_name(k)
        if nm in TARGETS and nm not in loc:
            loc[nm] = (p, i, k)
for t in TARGETS:
    if t in loc:
        p, i, k = loc[t]
        print("      %-24s klass=0x%X  array=0x%X idx=%d" % (t, k, p, i))
    else:
        print("      %-24s NOT FOUND" % t)

print()
print("[5] 用 klass+0x80 按 token 解析全部 27 个符号")
CAMS = [
    ("Camera", 0x06000264), ("Camera", 0x0600020A), ("Camera", 0x0600020B),
    ("Camera", 0x06000212), ("Camera", 0x06000218),
    ("BaseMonoMonster", 0x0600C4EE), ("BaseMonoAvatar", 0x0600A5A6),
    ("MonoSkillButton", 0x0602927A), ("MonoSkillButton", 0x06029279),
    ("BaseMonoAvatar", 0x0600A581),
    ("MHEMGJMNDJI", 0x06003B87), ("MHEMGJMNDJI", 0x06003B89),
    ("MHEMGJMNDJI", 0x06003B88),
    ("BaseMonoAnimatorEntity", 0x0600B03C), ("BaseMonoAnimatorEntity", 0x0600B03D),
]
ULTRA = ["MonoFuka", "MonoFuka_BattleY", "MonoFukaFL", "MonoFukaFL_BattleY",
         "MonoFukaIN", "MonoFukaIN_BattleY", "MonoFukaJW", "MonoFukaJW_BattleY",
         "MonoFukaND", "MonoFukaND_BattleY", "MonoFukaNL", "MonoFukaNL_BattleY"]
UTOK = [0x0600AB8C, 0x0600ABA5, 0x0600ABB4, 0x0600ABBC, 0x0600ABC6, 0x0600ABC9,
        0x0600ABCC, 0x0600ABCF, 0x0600ABD2, 0x0600ABD5, 0x0600ABD8, 0x0600ABDB]
for c, t in zip(ULTRA, UTOK):
    CAMS.append((c, t))

ok = 0
bad = 0
for cls, tok in CAMS:
    if cls not in loc:
        print("      MISS-CLASS %-24s tok=%08X" % (cls, tok))
        bad += 1
        continue
    k = loc[cls][2]
    tab = d.q(k + 0x80)
    hit = None
    if HEAP_LO <= tab < HEAP_HI:
        for i in range(2000):
            try:
                p = d.q(tab + i * 8)
            except Exception:
                break
            if not (HEAP_LO <= p < HEAP_HI):
                break
            mi = p - 8
            try:
                t = d.u32(mi + 0x30)
                kk = d.q(mi + 0x10)
            except Exception:
                continue
            if t == tok and kk == k:
                hit = (mi, d.q(mi + 0x08))
                break
    if hit:
        print("      OK   %-24s tok=%08X mi=0x%X rva=0x%X"
              % (cls, tok, hit[0], hit[1] - UA_BASE))
        ok += 1
    else:
        print("      MISS %-24s tok=%08X (tab=0x%X)" % (cls, tok, tab))
        bad += 1

print()
print("RESULT: ok=%d bad=%d" % (ok, bad))
sys.stdout.flush()
