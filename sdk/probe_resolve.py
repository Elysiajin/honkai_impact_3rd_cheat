"""验证「按 token + 类名」在运行期解析方法地址的完整算法，并确定类枚举边界。

目标：
  1. 精确确认 MethodInfo 字段偏移（+0x08 mp / +0x10 klass / +0x18 slot /
     +0x20 invoker / +0x28 ? / +0x30 token / +0x38 flags）
  2. 确认 klass 结构可枚举、+0x20 是名字 C 字符串、+0x80 是方法数组
  3. 用「扫 0x50000001000 区 + token 过滤 + 类名过滤」跑通全部 27 个符号
  4. 查清 avatar_mgr_global / monster_mgr_global 到底是什么
"""
import struct
import sys

from dump import Dump, UA_BASE

d = Dump()

HEAP = 0x50000001000
HEAP_N = 0x3BF5000


def cls_name(klass):
    p = d.q(klass + 0x20)
    if not p:
        return None
    try:
        s = d.cstr(p, 96)
    except Exception:
        return None
    if not s or len(s) < 1:
        return None
    return s


print("=" * 70)
print("[1] MethodInfo 布局：以 Camera.get_main (tok 0x06000264) 为样本")
print("=" * 70)

CAM = 0x50000B0D1A8
tab = d.q(CAM + 0x80)
target = None
for i in range(400):
    p = d.q(tab + i * 8)
    if not p:
        break
    mi = p - 8
    tok = d.u32(mi + 0x30)
    if tok == 0x06000264:
        target = mi
        break
print("found mi = 0x%X" % target if target else "NOT FOUND")
if target:
    for off in range(0, 0x48, 8):
        v = d.q(target + off)
        print("  +0x%02X  %016X" % (off, v))
    mp = d.q(target + 0x08)
    print("  mp rva = 0x%X  (profile camera_main = 0x121E6560)" % (mp - UA_BASE))

print()
print("=" * 70)
print("[2] 扫堆：token -> (mi, klass, name) 全部记录")
print("=" * 70)

hits = {}
recs = 0
for off in range(0, HEAP_N, 8):
    va = HEAP + off
    b = d.read(va + 0x28, 0x10)
    if len(b) != 0x10:
        continue
    maxstack, pad = struct.unpack_from("<II", b, 0)
    if maxstack != 0xFFFFFFFF:
        continue
    tok = struct.unpack_from("<I", b, 8)[0]
    if (tok >> 24) != 6:
        continue
    mi = va
    mp = d.q(mi + 0x08)
    if not (UA_BASE <= mp < UA_BASE + 0x1284B000):
        continue
    klass = d.q(mi + 0x10)
    recs += 1
    hits.setdefault(tok, []).append((mi, klass, mp))

print("total records = %d, distinct tokens = %d" % (recs, len(hits)))

WANT = [
    ("Camera", "get_main", 0x06000264, 0x121E6560),
    ("Camera", "get_pixelWidth", 0x0600020A, 0x121E5A80),
    ("Camera", "get_pixelHeight", 0x0600020B, 0x121E5A90),
    ("Camera", "INTERNAL_get_worldToCameraMatrix", 0x06000212, 0x121E5B90),
    ("Camera", "INTERNAL_get_projectionMatrix", 0x06000218, 0x121E5C40),
    ("BaseMonoMonster", "DieNow", 0x0600C4EE, 0x051E5960),
    ("BaseMonoAvatar", "BeHit", 0x0600A5A6, 0x04A73910),
    ("MonoSkillButton", "IsSkillInCD", 0x0602927A, 0x0730B300),
    ("MonoSkillButton", "CanUseSkill", 0x06029279, 0x0730B200),
    ("BaseMonoAvatar", "CanUseSkill", 0x0600A581, 0x04A6ADC0),
    ("MHEMGJMNDJI", "TryApplyCrit", 0x06003B87, 0x0793FB50),
    ("MHEMGJMNDJI", "TryApplyPhysicCrit", 0x06003B89, 0x0793FF90),
    ("MHEMGJMNDJI", "TryApplyElementCrit", 0x06003B88, 0x07940040),
    ("BaseMonoAnimatorEntity", "ApplyOverrideAnimatorSpeed", 0x0600B03C, 0x099646E0),
    ("BaseMonoAnimatorEntity", "CancelOverrideAnimatorSpeed", 0x0600B03D, 0x09964930),
]
for i in range(12):
    WANT.append(None)

ULTRA = ["MonoFuka", "MonoFuka_BattleY", "MonoFukaFL", "MonoFukaFL_BattleY",
         "MonoFukaIN", "MonoFukaIN_BattleY", "MonoFukaJW", "MonoFukaJW_BattleY",
         "MonoFukaND", "MonoFukaND_BattleY", "MonoFukaNL", "MonoFukaNL_BattleY"]
ULTRA_TOK = [0x0600AB8C, 0x0600ABA5, 0x0600ABB4, 0x0600ABBC, 0x0600ABC6, 0x0600ABC9,
             0x0600ABCC, 0x0600ABCF, 0x0600ABD2, 0x0600ABD5, 0x0600ABD8, 0x0600ABDB]

print()
print("=" * 70)
print("[3] 逐个解析 27 个符号（token + 类名）")
print("=" * 70)
ok = 0
bad = 0
for cls, meth, tok, want_rva in [w for w in WANT if w]:
    cands = hits.get(tok, [])
    found = None
    for mi, klass, mp in cands:
        if cls_name(klass) == cls:
            found = (mi, klass, mp)
            break
    if found:
        mi, klass, mp = found
        rva = mp - UA_BASE
        mark = "OK " if rva == want_rva else "DIFF"
        if rva == want_rva:
            ok += 1
        else:
            bad += 1
        print("  %s %-24s %-32s tok=%08X mi=0x%X mp=0x%X rva=0x%X want=0x%X cands=%d"
              % (mark, cls, meth, tok, mi, mp, rva, want_rva, len(cands)))
    else:
        bad += 1
        print("  MISS %-24s %-32s tok=%08X cands=%d"
              % (cls, meth, tok, len(cands)))

for cls, tok in zip(ULTRA, ULTRA_TOK):
    cands = hits.get(tok, [])
    found = None
    for mi, klass, mp in cands:
        if cls_name(klass) == cls:
            found = (mi, klass, mp)
            break
    if found:
        mi, klass, mp = found
        ok += 1
        print("  OK  %-24s %-32s tok=%08X mi=0x%X rva=0x%X cands=%d"
              % (cls, "AllowUseUltra", tok, mi, mp - UA_BASE, len(cands)))
    else:
        bad += 1
        print("  MISS %-24s %-32s tok=%08X cands=%d" % (cls, "AllowUseUltra", tok, len(cands)))

print()
print("RESULT: ok=%d bad=%d" % (ok, bad))

print()
print("=" * 70)
print("[4] avatar_mgr_global 0x2641588 / monster_mgr_global 0x2642EA8 真相")
print("=" * 70)
for name, rva in [("avatar_mgr_global", 0x2641588), ("monster_mgr_global", 0x2642EA8)]:
    va = UA_BASE + rva
    print(" %s  rva=0x%X va=0x%X" % (name, rva, va))
    for k in range(-4, 5):
        v = d.q(va + k * 8)
        print("   [%+d] 0x%X = 0x%X" % (k, va + k * 8, v))

sys.stdout.flush()
