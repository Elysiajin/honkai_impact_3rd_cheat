"""跟随 IL2CPP 的 jmp [rip+disp] 间接跳板, 解析出真实函数体."""
import struct

import dump as D


def resolve_thunk(d, va, depth=0):
    """若 va 处是 `48 FF 25 disp32` (jmp qword ptr [rip+disp]) 或
    `48 8B 05 disp32; 48 FF E0` (mov rax,[rip+disp]; jmp rax), 跟随并返回最终目标."""
    seen = []
    cur = va
    while depth < 4:
        b = d.read(cur, 8)
        if len(b) < 8:
            break
        if b[:3] == b"\x48\xff\x25":
            disp = struct.unpack_from("<i", b, 3)[0]
            slot = cur + 7 + disp
            nxt = d.q(slot)
            seen.append((cur, "jmp[rip]", slot, nxt))
            if not (D.UA_BASE <= nxt < D.UA_BASE + D.UA_SIZE):
                break
            cur = nxt
            depth += 1
            continue
        if b[:3] == b"\x48\x8b\x05" and b[7:10] == b"\x48\xff\xe0":
            disp = struct.unpack_from("<i", b, 3)[0]
            slot = cur + 7 + disp
            nxt = d.q(slot)
            seen.append((cur, "movrax;jmp", slot, nxt))
            if not (D.UA_BASE <= nxt < D.UA_BASE + D.UA_SIZE):
                break
            cur = nxt
            depth += 1
            continue
        break
    return cur, seen


def main():
    d = D.Dump()
    tests = [
        ("Camera.get_main", 0x121E6560),
        ("Camera.get_pixelWidth", 0x121E5A80),
        ("Camera.get_pixelHeight", 0x121E5A90),
        ("Camera.INTERNAL_get_worldToCameraMatrix", 0x121E5B90),
        ("Camera.INTERNAL_get_projectionMatrix", 0x121E5C40),
        ("BaseMonoMonster.DieNow", 0x51E5960),
    ]
    for name, rva in tests:
        final, chain = resolve_thunk(d, D.UA_BASE + rva)
        print("%-42s rva=0x%-9X -> final rva=0x%-9X hops=%d" % (
            name, rva, final - D.UA_BASE, len(chain)))
        for cur, kind, slot, nxt in chain:
            print("      %#x %s slot=%#x -> %#x (rva %#x)" % (cur - D.UA_BASE, kind, slot - D.UA_BASE, nxt, nxt - D.UA_BASE))
        print("      bytes:", d.read(final, 16).hex())


if __name__ == "__main__":
    main()
