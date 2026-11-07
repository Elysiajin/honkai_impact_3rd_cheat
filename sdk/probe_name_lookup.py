"""按类名字符串反查 klass（正确做法：klass+0x20 是「指向类名字符串的指针字段」）。

上一版错误：把「字符串地址 - 0x20」当 klass。
实际布局：*(klass + 0x20) == 字符串地址，所以要在堆里搜「值 == 字符串地址」的
qword 字段 f，klass = f - 0x20。

流程：
  1. 在堆里按字节搜类名 C 字符串，得到字符串地址 pos。
  2. 在堆里搜所有 qword == pos 的字段地址 f。
  3. klass = f - 0x20，再检查 klass+0x80 是合法方法数组。
  4. 用 klass+0x80 → 数组项 -8 → 比 token → +0x08 取 mp。
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

TARGETS = ["Camera", "BaseMonoMonster", "BaseMonoAvatar", "MonoSkillButton",
           "MHEMGJMNDJI", "BaseMonoAnimatorEntity", "AvatarManager",
           "MonsterManager", "Singleton`1"]
ULTRA = ["MonoFuka", "MonoFuka_BattleY", "MonoFukaFL", "MonoFukaFL_BattleY",
         "MonoFukaIN", "MonoFukaIN_BattleY", "MonoFukaJW", "MonoFukaJW_BattleY",
         "MonoFukaND", "MonoFukaND_BattleY", "MonoFukaNL", "MonoFukaNL_BattleY"]
TARGETS += ULTRA

print("[0] 载入堆区")
blobs = []
for base, size in HEAPS:
    b = d.read(base, size)
    print("    0x%X + 0x%X -> %d bytes" % (base, size, len(b)))
    blobs.append((base, b))

# 建一次全堆 qword 索引（numpy），用于「值 == pos」反查
print()
print("[1] 建 qword 索引")
arrs = []
for base, b in blobs:
    n = len(b) // 8
    a = np.frombuffer(b[:n * 8], dtype=np.uint64)
    arrs.append((base, a))
    print("    0x%X: %d qwords" % (base, n))


def find_cstr(name):
    pat = name.encode("ascii")
    hits = []
    for base, b in blobs:
        start = 0
        while True:
            i = b.find(pat, start)
            if i < 0:
                break
            end = i + len(pat)
            if end < len(b) and b[end] == 0 and (i == 0 or b[i - 1] == 0):
                hits.append(base + i)
            start = i + 1
    return hits


def find_qword_refs(value):
    out = []
    q = np.uint64(value)
    for base, a in arrs:
        idx = np.flatnonzero(a == q)
        for i in idx.tolist():
            out.append(base + i * 8)
    return out


print()
print("[2] 反查 klass")
klass_of = {}
for name in TARGETS:
    poss = find_cstr(name)
    if not poss:
        print("    %-24s NO-STRING" % name)
        continue
    found = None
    for pos in poss:
        for f in find_qword_refs(pos):
            k = f - 0x20
            if not (HEAP_LO <= k < HEAP_HI):
                continue
            try:
                if d.q(k + 0x20) != pos:
                    continue
                tab = d.q(k + 0x80)
            except Exception:
                continue
            if HEAP_LO <= tab < HEAP_HI:
                found = (k, pos, f, tab)
                break
        if found:
            break
    if found:
        klass_of[name] = found[0]
        print("    %-24s klass=0x%X strpos=0x%X field=0x%X tab=0x%X"
              % (name, found[0], found[1], found[2], found[3]))
    else:
        print("    %-24s NO-KLASS (str=%d)" % (name, len(poss)))

print()
print("[3] 按 token 解析 27 个符号")
SYMS = [
    ("camera_main", "Camera", 0x06000264, 0x121E6560),
    ("camera_pixel_w", "Camera", 0x0600020A, 0x121E5A80),
    ("camera_pixel_h", "Camera", 0x0600020B, 0x121E5A90),
    ("camera_w2c", "Camera", 0x06000212, 0x121E5B90),
    ("camera_proj", "Camera", 0x06000218, 0x121E5C40),
    ("die_now", "BaseMonoMonster", 0x0600C4EE, 0x051E5960),
    ("be_hit", "BaseMonoAvatar", 0x0600A5A6, 0x04A73910),
    ("ui_skill_in_cd", "MonoSkillButton", 0x0602927A, 0x0730B300),
    ("ui_can_use_skill", "MonoSkillButton", 0x06029279, 0x0730B200),
    ("avatar_can_use_skill", "BaseMonoAvatar", 0x0600A581, 0x04A6ADC0),
    ("crit_main", "MHEMGJMNDJI", 0x06003B87, 0x0793FB50),
    ("crit_phys", "MHEMGJMNDJI", 0x06003B89, 0x0793FF90),
    ("crit_elem", "MHEMGJMNDJI", 0x06003B88, 0x07940040),
    ("anim_speed_apply", "BaseMonoAnimatorEntity", 0x0600B03C, 0x099646E0),
    ("anim_speed_cancel", "BaseMonoAnimatorEntity", 0x0600B03D, 0x09964930),
]
UTOK = [0x0600AB8C, 0x0600ABA5, 0x0600ABB4, 0x0600ABBC, 0x0600ABC6, 0x0600ABC9,
        0x0600ABCC, 0x0600ABCF, 0x0600ABD2, 0x0600ABD5, 0x0600ABD8, 0x0600ABDB]
PROFILE_ULTRA = [0x09BAE670, 0x09DD97D0, 0x0863B560, 0x0863BF90, 0x076C3560,
                 0x09BAF090, 0x0505FF60, 0x058468F0, 0x081252E0, 0x06B86AF0,
                 0x0863CA20, 0x080BE120]
for i, (c, t, w) in enumerate(zip(ULTRA, UTOK, PROFILE_ULTRA)):
    SYMS.append(("ultra%d" % i, c, t, w))


def resolve(k, tok):
    tab = d.q(k + 0x80)
    if not (HEAP_LO <= tab < HEAP_HI):
        return None
    for i in range(4000):
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
for key, cls, tok, want in SYMS:
    k = klass_of.get(cls)
    if not k:
        print("    MISS-CLASS  %-22s %-24s tok=%08X" % (key, cls, tok))
        bad += 1
        continue
    r = resolve(k, tok)
    if not r:
        print("    MISS-METHOD %-22s %-24s tok=%08X" % (key, cls, tok))
        bad += 1
        continue
    rva = r[1] - UA_BASE
    mark = "OK  " if rva == want else "DIFF"
    if rva == want:
        ok += 1
    else:
        bad += 1
    print("    %s %-22s %-24s tok=%08X rva=0x%08X want=0x%08X"
          % (mark, key, cls, tok, rva, want))

print()
print("RESULT: ok=%d bad=%d" % (ok, bad))
sys.stdout.flush()
