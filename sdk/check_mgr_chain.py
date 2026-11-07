"""楠岃瘉 manager 鍗曚緥閾? ov_data.h 鐢ㄧ殑鏄?mi+8 褰?klass, bh3_sdk.h 璇?klass 鍦?mi+0x10.
鐢?CE dump 瀹為檯璧颁袱鏉￠摼, 鐪嬪摢鏉¤兘璇诲嚭 AvatarManager / MonsterManager.
"""
import dump as D

GLOBALS = [
    ("avatar_mgr_global", 0x2641588, "AvatarManager"),
    ("monster_mgr_global", 0x2642EA8, "MonsterManager"),
]


def walk(d, klass, tag):
    """klass -> +0x18 -> +0x10 -> +0x30 -> [0] -> [0] -> name"""
    steps = []
    c = d.q(klass + 0x18)
    steps.append(("klass+0x18", c))
    if not c:
        return None, steps
    x = d.q(c + 0x10)
    steps.append(("+0x10", x))
    if not x:
        return None, steps
    e = d.q(x + 0x30)
    steps.append(("+0x30", e))
    if not e:
        return None, steps
    m = d.q(e)
    steps.append(("[e]", m))
    if not m:
        return None, steps
    mk = d.q(m)
    steps.append(("[m]", mk))
    if not mk:
        return None, steps
    nm = d.q(mk + 0x20)
    steps.append(("mk+0x20", nm))
    s = d.cstr(nm, 64) if nm else None
    return s, steps


def main():
    d = D.Dump()
    for name, rva, want in GLOBALS:
        va = D.UA_BASE + rva
        print("=" * 70)
        print("%-22s rva=0x%X va=0x%X  want=%s" % (name, rva, va, want))
        raw = d.read(va, 0x30)
        print("  raw bytes:", raw.hex())
        for off in (0, 8, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38):
            v = d.q(va + off)
            print("    mi+0x%-2X = 0x%016X" % (off, v))
        mi = d.q(va)
        if not mi:
            print("  !! global 瑙ｅ紩鐢ㄤ负 0")
            continue
        for label, off in (("mi+8  (ov_data 鐜扮敤)", 8), ("mi+0x10 (bh3_sdk 澹版槑)", 0x10)):
            k = d.q(mi + off)
            print("  --- %s -> klass=0x%X" % (label, k))
            if not k:
                continue
            nm, steps = walk(d, k, label)
            for t, v in steps:
                print("        %-10s -> 0x%016X" % (t, v))
            print("        NAME = %r   match=%s" % (nm, nm == want))


if __name__ == "__main__":
    main()

