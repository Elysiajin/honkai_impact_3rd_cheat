#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Enumerate contiguous il2cpp-code pointer arrays in UserAssembly .data."""
from __future__ import annotations

import struct

from live_probe import Dump, UA_BASE


DATA_RVA = 0x1F59000
DATA_SIZE = 0x4E0A00
CODE_LO = UA_BASE + 0x3332000
CODE_HI = UA_BASE + 0x3332000 + 0xF341000


def main() -> None:
    d = Dump()
    data = d.read(UA_BASE + DATA_RVA, DATA_SIZE)
    q = struct.unpack_from("<%dQ" % (len(data) // 8), data, 0)
    runs = []
    i = 0
    n = len(q)
    while i < n:
        if not (CODE_LO <= q[i] < CODE_HI):
            i += 1
            continue
        start = i
        while i + 1 < n and CODE_LO <= q[i + 1] < CODE_HI:
            i += 1
        count = i - start + 1
        if count >= 8:
            runs.append((DATA_RVA + start * 8, count, q[start], q[i]))
        i += 1

    print(f"{len(runs)} runs, {sum(r[1] for r in runs)} pointers")
    for rva, count, first, last in runs:
        print(
            f"array=0x{rva:08X} count={count:6d} "
            f"first=0x{first - UA_BASE:08X} last=0x{last - UA_BASE:08X}"
        )


if __name__ == "__main__":
    main()
