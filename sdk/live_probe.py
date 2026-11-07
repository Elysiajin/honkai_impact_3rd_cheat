#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Read-only probes over the Cheat Engine region dump.

The CE dump is a set of page files plus regions.csv.  This module exposes just
enough of the address space to inspect UserAssembly.dll globals without loading
the multi-hundred-MB code pages into Python.
"""
from __future__ import annotations

import argparse
import csv
import os
import struct
from dataclasses import dataclass


DEFAULT_REGIONS = r"D:\Cheat Table\CE Search\regions.csv"
UA_BASE = 0x7FF8C38A0000


@dataclass(frozen=True)
class Region:
    base: int
    size: int
    path: str
    protection: str


class Dump:
    def __init__(self, regions_csv: str = DEFAULT_REGIONS):
        self.regions: list[Region] = []
        with open(regions_csv, encoding="utf-8", errors="replace", newline="") as f:
            for row in csv.DictReader(f):
                try:
                    self.regions.append(
                        Region(
                            int(row["base"], 16),
                            int(row["size"], 16),
                            row["file"],
                            row.get("protection", ""),
                        )
                    )
                except (KeyError, ValueError):
                    continue
        self.regions.sort(key=lambda r: r.base)
        self._files: dict[str, object] = {}

    def region(self, addr: int) -> Region | None:
        lo, hi = 0, len(self.regions)
        while lo < hi:
            mid = (lo + hi) // 2
            r = self.regions[mid]
            if addr < r.base:
                hi = mid
            elif addr >= r.base + r.size:
                lo = mid + 1
            else:
                return r
        return None

    def read(self, addr: int, size: int) -> bytes:
        out = bytearray()
        while size:
            r = self.region(addr)
            if r is None:
                raise ValueError(f"unmapped read at 0x{addr:X}")
            n = min(size, r.base + r.size - addr)
            f = self._files.get(r.path)
            if f is None:
                f = open(r.path, "rb", buffering=0)
                self._files[r.path] = f
            f.seek(addr - r.base)
            chunk = f.read(n)
            if len(chunk) != n:
                raise ValueError(f"short read at 0x{addr:X} from {r.path}")
            out.extend(chunk)
            addr += n
            size -= n
        return bytes(out)

    def u32(self, addr: int) -> int:
        return struct.unpack("<I", self.read(addr, 4))[0]

    def i32(self, addr: int) -> int:
        return struct.unpack("<i", self.read(addr, 4))[0]

    def u64(self, addr: int) -> int:
        return struct.unpack("<Q", self.read(addr, 8))[0]

    def q(self, addr: int) -> int:
        return self.u64(addr)

    def cstr(self, addr: int, limit: int = 128) -> str:
        if not addr:
            return ""
        b = bytearray()
        while len(b) < limit:
            chunk = self.read(addr + len(b), 1)[0]
            if chunk == 0:
                break
            b.append(chunk)
        return b.decode("utf-8", "replace")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", type=lambda s: int(s, 0), default=UA_BASE)
    args = ap.parse_args()
    d = Dump()
    print("regions", len(d.regions))
    for name, rva in (
        ("metadata_header", 0x25554B0),
        ("metadata_body", 0x25554B8),
        ("startup_buf", 0x25554C0),
        ("region_base", 0x25554C8),
        ("mscorlib_classes", 0x25554D0),
        ("module_count", 0x25554E8),
        ("module_table", 0x25554F0),
        ("table2", 0x2555500),
        ("table2_count", 0x2555508),
    ):
        print(f"{name:20s} {d.u64(args.base + rva):#018x}")


if __name__ == "__main__":
    main()
