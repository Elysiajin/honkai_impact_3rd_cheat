#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Find token -> method-pointer maps inside UserAssembly.dll's image dump."""
from __future__ import annotations

import struct
import sys

from live_probe import Dump, UA_BASE


TOKEN_LO = 0x06000000
TOKEN_HI = 0x06FFFFFF


def read_region(dump: Dump, base: int, size: int) -> bytes:
    return dump.read(base, size)


def scan(dump: Dump, base: int, data: bytes) -> list[tuple[int, int, int, int, int]]:
    """Return (offset, count, first_token, last_token, first_rva) runs."""
    n = len(data) // 16
    runs = []
    i = 0
    while i < n:
        off = i * 16
        tok, ptr = struct.unpack_from("<QQ", data, off)
        ok = (
            TOKEN_LO <= tok <= TOKEN_HI
            and base + 0x1000 <= ptr < base + 0x1284B000
            and (ptr & 0xF) in (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF)
        )
        if not ok:
            i += 1
            continue
        start = i
        prev_tok = tok
        while i + 1 < n:
            tok2, ptr2 = struct.unpack_from("<QQ", data, (i + 1) * 16)
            if not (TOKEN_LO <= tok2 <= TOKEN_HI):
                break
            if not (base + 0x1000 <= ptr2 < base + 0x1284B000):
                break
            if tok2 <= prev_tok or tok2 - prev_tok > 0x10000:
                break
            i += 1
            prev_tok = tok2
        count = i - start + 1
        if count >= 8:
            tok0, ptr0 = struct.unpack_from("<QQ", data, start * 16)
            tok1 = prev_tok
            runs.append((start * 16, count, tok0, tok1, ptr0 - base))
        i += 1
    return runs


def main() -> None:
    d = Dump()
    regions = [
        ("rdata", 0x7FF8C3F79000, 0x1880000),
        ("data", 0x7FF8C5805000, 0x432000),
    ]
    for name, va, size in regions:
        data = read_region(d, va, size)
        runs = scan(d, UA_BASE, data)
        total = sum(r[1] for r in runs)
        print(f"{name}: {len(runs)} runs, {total} pairs")
        for off, count, t0, t1, rva0 in runs[:40]:
            print(
                f"  rva=0x{va - UA_BASE + off:08X} count={count:6d} "
                f"token=0x{t0:08X}..0x{t1:08X} rva0=0x{rva0:08X}"
            )


if __name__ == "__main__":
    main()
