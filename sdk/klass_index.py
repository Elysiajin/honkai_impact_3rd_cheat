"""全堆 klass 名称索引：一次性扫出所有 klass，建 name -> klass 字典。

为什么需要它：
  「按类名字符串 - 0x20」只能命中「名字字符串恰好紧跟在 klass 后面」的情况，
  MHEMGJMNDJI / Singleton`1 / 多数 MonoFuka* 的名字字符串落在别处，所以查不到。

正确算法（运行期一次 O(堆大小) 扫描，之后 O(1) 查表）：
  1. 遍历堆里每个 8 字节对齐的 qword 位置 p，把它当作候选 klass 指针 k = *p。
  2. 校验 k 是合法 klass：
       - k 落在堆内
       - k 至少 0x90 字节对齐可读
       - name = *(k + 0x20) 指向堆内，且是一段合法 C 字符串（长度 1..256，
         字符全是 [A-Za-z0-9_`.<>$@+-] ）
       - *(k + 0x80) 落在堆内（方法数组）
       - *(k + 0x88) 是合理 tdi（< 0x20000）
  3. 用 name 建索引；同名的多个 klass 取「+0x80 方法数组最大」的那个
     （完整类 > 泛型实例）。
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

print("[0] 载入堆区")
blobs = []
for base, size in HEAPS:
    b = d.read(base, size)
    blobs.append((base, b))
    print("    0x%X + 0x%X -> %d bytes" % (base, size, len(b)))

# 全堆字节拼接 + VA 映射（用 base 递增判断连续）
# 这里用 numpy 找不到「字符串是否合法」的向量化方法，先用一次
# 「所有合法 C 字符串起点」的位图，再逐候选解码。
print()
print("[1] 建立合法 klass 集合")

name_of = {}   # klass -> name


def cstr_at(va):
    try:
        s = d.cstr(va, 300)
    except Exception:
        return None
    if not s or len(s) > 200:
        return None
    for ch in s:
        if not (ch.isalnum() or ch in "_`.<>$@+-[]"):
            return None
    return s


cands = set()
for base, b in blobs:
    n = len(b) // 8
    a = np.frombuffer(b[:n * 8], dtype=np.uint64)
    m = (a >= np.uint64(HEAP_LO)) & (a < np.uint64(HEAP_HI))
    idx = np.flatnonzero(m)
    print("    0x%X: %d heap-pointing qwords" % (base, idx.size))
    for i in idx.tolist():
        cands.add(int(a[i]))

print("    unique candidate klass ptrs: %d" % len(cands))

valid = {}
for k in cands:
    try:
        np_ = d.q(k + 0x20)
        if not (HEAP_LO <= np_ < HEAP_HI):
            continue
        tab = d.q(k + 0x80)
        if not (HEAP_LO <= tab < HEAP_HI):
            continue
        tdi = d.q(k + 0x88)
        if tdi >= 0x200000:
            continue
        s = cstr_at(np_)
        if not s:
            continue
    except Exception:
        continue
    valid[k] = s

print("    valid klass: %d" % len(valid))

for k, s in valid.items():
    cur = name_of.get(s)
    if cur is None:
        name_of[s] = k
    else:
        # 选方法数组更大的那个
        try:
            if d.q(k + 0x80) < d.q(cur + 0x80):
                name_of[s] = k
        except Exception:
            pass

print("    distinct names: %d" % len(name_of))

TARGETS = ["Camera", "BaseMonoMonster", "BaseMonoAvatar", "MonoSkillButton",
           "MHEMGJMNDJI", "BaseMonoAnimatorEntity", "AvatarManager",
           "MonsterManager", "Singleton`1"]
ULTRA = ["MonoFuka", "MonoFuka_BattleY", "MonoFukaFL", "MonoFukaFL_BattleY",
         "MonoFukaIN", "MonoFukaIN_BattleY", "MonoFukaJW", "MonoFukaJW_BattleY",
         "MonoFukaND", "MonoFukaND_BattleY", "MonoFukaNL", "MonoFukaNL_BattleY"]
TARGETS += ULTRA

print()
print("[2] 目标类解析")
klass_of = {}
for name in TARGETS:
    k = name_of.get(name)
    if k:
        klass_of[name] = k
        print("    %-24s klass=0x%X tab=0x%X tdi=0x%X"
              % (name, k, d.q(k + 0x80), d.q(k + 0x88)))
    else:
        print("    %-24s NOT FOUND" % name)

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
    try:
        tab = d.q(k + 0x80)
    except Exception:
        return None
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
