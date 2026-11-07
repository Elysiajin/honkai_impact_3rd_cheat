"""找齐所有 module 的「类数组」，实现 类名 -> klass 的 O(1) 查表。

已知：mscorlib 类数组 = *(UA+0x25554D0) = 0x50000396C60，29691 项，
      首项 klass 名字是 "<Module>"，含 Camera(idx 3309) / Singleton`1(idx 28674)。

本脚本：
  1. 扫描 0x500 堆，把每个 qword 解释成「可能的 klass 指针」，
     判定标准 = klass+0x20 指向可读的、字符集合法的 C 字符串。
  2. 找连续 >= 16 个这样的指针构成的「run」，每个 run 就是一个 module 的类数组。
  3. 合并 run 后统计总类数、检查是否覆盖 kCategory 目标类。
  4. 用覆盖到的类跑 klass+0x80 的 token 解析。
"""
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

print("[1] 收集所有 0x500 堆 qword 与去重值")
vals = []
for base, size in HEAPS:
    off = 0
    chunk = 1 << 22
    while off < size:
        n = min(chunk, size - off)
        try:
            b = d.read(base + off, n)
        except Exception:
            break
        if len(b) < 8:
            break
        a = np.frombuffer(b[: len(b) // 8 * 8], dtype=np.uint64)
        vals.append(a)
        off += n
        if off >= size:
            break
q = np.concatenate(vals) if vals else np.zeros(0, dtype=np.uint64)
print("    total qwords = %d" % len(q))
inside = q[(q >= HEAP_LO) & (q < HEAP_HI)]
print("    pointing into heap = %d, unique = %d" % (len(inside), len(np.unique(inside))))

print()
print("[2] 逐个唯一值验证能否当作 klass（name 合法）")
uniq = np.unique(inside)
print("    unique candidates = %d" % len(uniq))
good = []
BADCH = set()
for v in uniq:
    va = int(v)
    try:
        p = d.q(va + 0x20)
    except Exception:
        continue
    if not (HEAP_LO <= p < HEAP_HI):
        continue
    s = d.cstr(p, 64)
    if not s or len(s) > 48:
        continue
    if any(c < 32 or c > 126 for c in (ord(ch) for ch in s)):
        continue
    good.append(va)
print("    valid klass = %d" % len(good))
gset = np.array(sorted(good), dtype=np.uint64)

print()
print("[3] 找连续 run（同一 module 的类数组）")
mask = np.isin(q, gset)
runs = []
i = 0
n = len(mask)
while i < n:
    if not mask[i]:
        i += 1
        continue
    j = i
    while j + 1 < n and mask[j + 1] and (int(q[j + 1]) - int(q[j])) == 0x8:
        j += 1
    if j - i + 1 >= 16:
        runs.append((i, j - i + 1))
    i = j + 1
print("    runs(>=16) = %d, total classes = %d" % (len(runs), sum(r[1] for r in runs)))
runs.sort(key=lambda r: -r[1])
for i, ln in runs[:12]:
    print("      run at q[%d] len=%d first=%r last=%r"
          % (i, ln, d.cstr(d.q(int(q[i]) + 0x20), 48),
             d.cstr(d.q(int(q[i + ln - 1]) + 0x20), 48)))


def name_of(k):
    try:
        p = d.q(k + 0x20)
        if not (HEAP_LO <= p < HEAP_HI):
            return None
        return d.cstr(p, 64) or None
    except Exception:
        return None


print()
print("[4] 在所有 run 里建 类名 -> klass")
cls = {}
for i, ln in runs:
    for k8 in range(ln):
        k = int(q[i + k8])
        nm = name_of(k)
        if nm and nm not in cls:
            cls[nm] = k
print("    distinct named classes = %d" % len(cls))

TARGETS = ["AvatarManager", "MonsterManager", "MonoSkillButton", "MHEMGJMNDJI",
           "BaseMonoMonster", "BaseMonoAvatar", "BaseMonoAnimatorEntity",
           "MonoFuka", "MonoFuka_BattleY", "MonoFukaFL", "MonoFukaFL_BattleY",
           "MonoFukaIN", "MonoFukaIN_BattleY", "MonoFukaJW", "MonoFukaJW_BattleY",
           "MonoFukaND", "MonoFukaND_BattleY", "MonoFukaNL", "MonoFukaNL_BattleY",
           "Camera", "Singleton`1"]
miss = []
for t in TARGETS:
    if t in cls:
        print("      %-24s klass=0x%X" % (t, cls[t]))
    else:
        print("      %-24s NOT FOUND" % t)
        miss.append(t)

print()
print("[5] klass+0x80 + token 解析全部 27 符号")
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

WANT = {
    ("Camera", 0x06000264): 0x121E6560, ("Camera", 0x0600020A): 0x121E5A80,
    ("Camera", 0x0600020B): 0x121E5A90, ("Camera", 0x06000212): 0x121E5B90,
    ("Camera", 0x06000218): 0x121E5C40, ("BaseMonoMonster", 0x0600C4EE): 0x051E5960,
    ("BaseMonoAvatar", 0x0600A5A6): 0x04A73910,
    ("MonoSkillButton", 0x0602927A): 0x0730B300,
    ("MonoSkillButton", 0x06029279): 0x0730B200,
    ("BaseMonoAvatar", 0x0600A581): 0x04A6ADC0,
    ("MHEMGJMNDJI", 0x06003B87): 0x0793FB50,
    ("MHEMGJMNDJI", 0x06003B89): 0x0793FF90,
    ("MHEMGJMNDJI", 0x06003B88): 0x07940040,
    ("BaseMonoAnimatorEntity", 0x0600B03C): 0x099646E0,
    ("BaseMonoAnimatorEntity", 0x0600B03D): 0x09964930,
}


def resolve(k, tok):
    tab = d.q(k + 0x80)
    if not (HEAP_LO <= tab < HEAP_HI):
        return None
    for i in range(3000):
        try:
            p = d.q(tab + i * 8)
        except Exception:
            return None
        if not (HEAP_LO <= p < HEAP_HI):
            return None
        mi = p - 8
        try:
            if d.u32(mi + 0x30) == tok and d.q(mi + 0x10) == k:
                return mi, d.q(mi + 0x08)
        except Exception:
            continue
    return None


ok = bad = 0
for c, t in CAMS:
    k = cls.get(c)
    if not k:
        print("      MISS-CLASS %-24s tok=%08X" % (c, t))
        bad += 1
        continue
    r = resolve(k, t)
    if not r:
        print("      MISS-METHOD %-24s tok=%08X" % (c, t))
        bad += 1
        continue
    rva = r[1] - UA_BASE
    ok += 1
    print("      OK  %-24s tok=%08X rva=0x%08X" % (c, t, rva))

print()
print("RESULT: ok=%d bad=%d" % (ok, bad))
print("期望值核对: 全部 27 项应与 bh3_profile.inc 一致（ultra* 亦然）")
sys.stdout.flush()
