#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Locate RIP-relative references to UserAssembly RVAs in the CE image dump."""
from __future__ import annotations

import argparse
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from live_probe import Dump, UA_BASE


TEXT_VA = UA_BASE + 0x1000
TEXT_SIZE = 0x6D8000


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("targets", nargs="+", type=lambda s: int(s, 0))
    ap.add_argument("--window", type=int, default=48)
    args = ap.parse_args()

    d = Dump()
    text = d.read(TEXT_VA, TEXT_SIZE)
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    targets = {t & 0xFFFFFFFF: t for t in args.targets}

    found = 0
    for i in range(len(text) - 4):
        disp = struct.unpack_from("<i", text, i)[0]
        rip = TEXT_VA + i + 4
        target = (rip + disp) & 0xFFFFFFFFFFFFFFFF
        if target not in targets:
            continue
        # The displacement is at the end of its instruction.  Try every start
        # in the previous 15 bytes so we do not depend on disassembly alignment.
        for back in range(1, 16):
            start = i + 4 - back
            if start < 0:
                break
            for ins in md.disasm(text[start:i + 4], TEXT_VA + start):
                if ins.address + ins.size != TEXT_VA + i + 4:
                    continue
                if not ins.op_str:
                    continue
                print(
                    f"target=0x{target:X} rva=0x{ins.address - UA_BASE:X} "
                    f"{ins.mnemonic} {ins.op_str}"
                )
                found += 1
                break
            else:
                continue
            break

    # Absolute 8-byte immediates / encoded pointers in .text are uncommon but
    # do occur in this build.
    for i in range(0, len(text) - 8, 8):
        v = struct.unpack_from("<Q", text, i)[0]
        if v in targets:
            print(f"abs target=0x{v:X} rva=0x{TEXT_VA + i - UA_BASE:X} qword")
            found += 1
    print(f"hits={found}")


if __name__ == "__main__":
    main()
