"""Export the Honkai Impact 3rd 9.2.0 Windows IL2CPP metadata."""

import argparse
import json
import mmap
import sqlite3
import struct
from functools import cache
from pathlib import Path


U32 = 0xFFFFFFFF
U64 = 0xFFFFFFFFFFFFFFFF
PE_HEADER_RVA = 0x12B47C8
CODE_REGISTRATION_RVA = 0x125B738
METADATA_REGISTRATION_RVA = 0x12B7E88


def u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def u64(data, offset):
    return struct.unpack_from("<Q", data, offset)[0]


def signed(value):
    return value if value < 0x80000000 else value - 0x100000000


def require(condition, message):
    if not condition:
        raise ValueError(message)


class PE:
    def __init__(self, data):
        self.data = data
        nt = u32(data, 0x3C)
        optional = nt + 24
        require(data[nt:nt + 4] == b"PE\0\0" and u16(data, optional) == 0x20B,
                "Expected a PE32+ UserAssembly.dll")
        self.base = u64(data, optional + 24)
        table = optional + u16(data, nt + 20)
        self.sections = []
        for n in range(u16(data, nt + 6)):
            virtual_size, rva, raw_size, raw = struct.unpack_from("<4I", data, table + 40*n + 8)
            flags = u32(data, table + 40*n + 36)
            self.sections.append((rva, virtual_size, raw_size, raw, flags))

    def offset(self, rva):
        for start, _, raw_size, raw, _ in self.sections:
            if start <= rva < start + raw_size:
                return raw + rva - start
        raise ValueError(f"RVA 0x{rva:X} has no file data")

    def pointer(self, offset):
        return self.offset(u64(self.data, offset) - self.base)

    def executable(self, rva):
        return any(start <= rva < start + virtual_size and flags & 0x20000000
                   for start, virtual_size, _, _, flags in self.sections)


SCHEMA = """
CREATE TABLE images (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE,
    type_start INTEGER, type_count INTEGER);
CREATE TABLE types (id INTEGER PRIMARY KEY, image_id INTEGER NOT NULL REFERENCES images(id),
    token INTEGER, namespace TEXT, name TEXT, full_name TEXT, flags INTEGER,
    declaring_type_id INTEGER, parent_type_index INTEGER, byval_type_index INTEGER,
    method_start INTEGER, method_count INTEGER, field_start INTEGER, field_count INTEGER);
CREATE TABLE methods (id INTEGER PRIMARY KEY, type_id INTEGER NOT NULL REFERENCES types(id),
    token INTEGER, name TEXT, flags INTEGER, impl_flags INTEGER, slot INTEGER,
    return_type_index INTEGER, return_type TEXT, parameter_count INTEGER,
    rva INTEGER, file_offset INTEGER);
CREATE TABLE parameters (id INTEGER PRIMARY KEY, method_id INTEGER NOT NULL REFERENCES methods(id),
    position INTEGER, token INTEGER, name TEXT, type_index INTEGER, type_name TEXT);
CREATE TABLE fields (id INTEGER PRIMARY KEY, type_id INTEGER NOT NULL REFERENCES types(id),
    position INTEGER, token INTEGER, name TEXT, type_index INTEGER, type_name TEXT,
    offset INTEGER, offset_raw INTEGER);
CREATE TABLE metadata (key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE VIEW method_symbols AS
    SELECT i.name AS assembly, t.full_name AS type_name, m.*
    FROM methods m JOIN types t ON m.type_id=t.id JOIN images i ON t.image_id=i.id;
"""


def export(native, data, startup, destination, matched_dumps):
    pe = PE(native)
    header_offset = pe.offset(PE_HEADER_RVA)
    header = native[header_offset:header_offset + 0x198]
    require(header.startswith(b"MHY\0") and data[:4] == b"MHY\0",
            "Input does not match the 9.2.0 profile")

    def add(offset, key):
        return signed((u32(header, offset) + key) & U32)

    def xor(offset, key):
        return signed(u32(header, offset) ^ key)

    strings = 0x198 + add(0xC4, 0xC2BA10D1)
    type_base = 0x198 + xor(0xDC, 0x5C5AB949)
    method_base = 0x198 + add(0x194, 0x88A4C15B)
    param_base = 0x198 + add(0x4C, 0xEB93DE2E)
    field_base = 0x198 + add(0xF4, 0xC92997D8)
    field_offsets = 0x198 + xor(0x154, 0x0EF69252)
    type_map = 0x198 + add(0x88, 0xF1B8D6BB)
    type_aux = 0x198 + xor(0x118, 0x579075D3)
    type_count = add(0x12C, 0x9478C078) // 0x52
    method_count = xor(0x134, 0x4370569E) // 0x1E
    image_base = xor(0xD0, 0x171BC3AF)
    image_count = add(0xBC, 0x932069D0) // 40

    @cache
    def symbol(index):
        if index == U32:
            return ""
        length, relative = index >> 24, index & 0xFFFFFF
        position = strings + relative
        require(0 <= position <= len(data) - length, f"Bad string index 0x{index:X}")
        seed = (0x27CB2CBF2819AA46 *
                (0x7C09509C0DDC926F ^ (0x4F723631EDAEA32D * relative)))
        clear = bytearray()
        for j in range(0, length, 8):
            size = min(8, length - j)
            raw = int.from_bytes(data[position + j:position + j + size], "little")
            key = (seed + (j // 8) * 0x3B79E22029309A9) & ((1 << (size * 8)) - 1)
            clear.extend((raw ^ key).to_bytes(size, "little"))
        name = clear.decode("utf-8")
        require(all(ch.isprintable() for ch in name), f"Bad symbol at 0x{index:X}")
        return name

    def image_row(i):
        p = image_base + i * 40
        key = (((((i * 0xFF07 ^ 0x3E4C7913) * 0x692061A0) & U64) >> 18)
               ^ 0x12D503B8)
        key = ((key * 0x7B9CB670) & U64) >> 12
        return (i, symbol(((u32(startup, p + 0x18) ^ 0x1983D46E) - key) & U32),
                signed(((u32(startup, p + 4) ^ 0x2278A806) - key) & U32),
                ((u32(startup, p + 8) ^ 0x617041A6) - key) & U32)

    images = [image_row(i) for i in range(image_count)]
    modules = pe.pointer(pe.offset(CODE_REGISTRATION_RVA) + 0x30)
    descriptors = pe.pointer(pe.offset(METADATA_REGISTRATION_RVA) + 0x40)
    image_for_type = [-1] * type_count
    method_arrays = []
    for image, name, first, count in images:
        mod = pe.pointer(modules + image * 8)
        start = pe.pointer(mod + 0x18)
        stop = native.find(b"\0", start, start + 1024)
        require(stop >= 0 and native[start:stop].decode("utf-8") == name,
                f"Code registration mismatch for {name}")
        method_arrays.append(pe.pointer(mod + 8) if u64(native, mod + 8) else None)
        if count:
            require(0 <= first <= type_count - count, f"Invalid type range for {name}")
        for i in range(first, first + count):
            require(image_for_type[i] == -1, f"Duplicate image assignment for type {i}")
            image_for_type[i] = image
    exported_type_count = max(first + count for _, _, first, count in images if count)
    require(-1 not in image_for_type[:exported_type_count], "Some types lack an image")
    for i in range(exported_type_count, type_count):
        p = type_base + i * 0x52
        require((u32(data, p + 0x0C) + 0xEF71230D) & U32 == 0x02000000
                and not symbol((u32(data, p + 0x30) + 0xB221FE7B) & U32)
                and u16(data, p + 0x3A) == 0xFB98
                and u16(data, p + 0x4C) == 0x3AFC,
                f"Unexpected unassigned TypeDef {i}")

    types = []
    for i, image in enumerate(image_for_type[:exported_type_count]):
        p = type_base + i * 0x52
        token = (u32(data, p + 0x0C) + 0xEF71230D) & U32
        name = symbol((u32(data, p + 0x30) + 0xB221FE7B) & U32)
        ns = symbol((u32(data, p + 0x1C) + 0xAABB23D2) & U32)
        declaring = signed((u32(data, p + 0x28) + 0xBA50B1AF) & U32)
        parent = signed((u32(data, p + 0x10) + 0x88B62898) & U32)
        byval = signed((u32(data, p + 4) + 0xE1F55417) & U32)
        mstart = signed(u32(data, p + 8) ^ 0x456955CC)
        mcount = u16(data, p + 0x3A) ^ 0xFB98
        fstart = signed(u32(data, p + 0x24) ^ 0x71DB3DCA)
        fcount = (u16(data, p + 0x4C) - 0x3AFC) & 0xFFFF
        require(token >> 24 == 2, f"Invalid TypeDef {i}")
        types.append((i, image, token, ns, name, u32(data, p) ^ 0x5DDCADED,
                      declaring, parent, byval, mstart, mcount, fstart, fcount))

    def declaring_id(index):
        if index < 0:
            return None
        p = descriptors + index * 16
        value = u32(native, p)
        return value if native[p + 10] in (0x11, 0x12, 0x15) and value < len(types) else None

    @cache
    def full_name(i):
        row = types[i]
        parent = declaring_id(row[6])
        if parent is not None and parent != i:
            return full_name(parent) + "+" + (row[4] or f"TypeDef_{i}")
        return ".".join(part for part in (row[3], row[4] or f"TypeDef_{i}") if part)

    primitive = {
        1: "void", 2: "bool", 3: "char", 4: "sbyte", 5: "byte", 6: "short",
        7: "ushort", 8: "int", 9: "uint", 10: "long", 11: "ulong",
        12: "float", 13: "double", 14: "string", 0x16: "System.TypedReference",
        0x18: "System.IntPtr", 0x19: "System.UIntPtr", 0x1C: "object",
    }

    def render_descriptor(p, depth=0):
        value = u64(native, p)
        kind, modifiers = native[p + 10:p + 12]
        result = primitive.get(kind)
        if kind in (0x11, 0x12, 0x15) and value < len(types):
            result = full_name(value)
        elif kind in (0x0F, 0x10, 0x1D) and value >= pe.base and depth < 8:
            suffix = {0x0F: "*", 0x10: "&", 0x1D: "[]"}[kind]
            result = render_descriptor(pe.offset(value - pe.base), depth + 1) + suffix
        if result is None:
            result = f"TypeKind_0x{kind:02X}_0x{value:X}"
        return ("ref " if modifiers & 0x40 else "") + result

    @cache
    def type_name(index):
        return f"TypeIndex_{index}" if index < 0 else render_descriptor(descriptors + index * 16)

    output = destination / "sdk.sqlite"
    text_output = destination / "dump.cs"
    database = destination / "sdk.building.sqlite"
    draft = destination / "dump.building.cs"
    for path in (database, draft):
        require(not path.exists(), f"Previous export still exists at {path}")
    db = sqlite3.connect(database)
    counts = dict(images=image_count, types=len(types), methods=0, methods_with_rva=0,
                  fields=0, parameters=0)
    try:
        db.execute("PRAGMA foreign_keys=ON")
        db.executescript(SCHEMA)
        db.executemany("INSERT INTO images VALUES (?,?,?,?)", images)
        db.executemany("INSERT INTO types VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)", (
            (*r[:5], full_name(r[0]), r[5], declaring_id(r[6]), *r[7:]) for r in types
        ))
        seen_methods = bytearray(method_count)
        with draft.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write("// Honkai Impact 3rd 9.2.0 IL2CPP reference dump\n")
            stream.write("// RVA is relative to UserAssembly.dll. FileOffset is its PE file offset.\n")
            stream.write("// C#-style reference text. Use sdk.sqlite for numeric metadata.\n\n")
            previous_image = None
            for row in types:
                i, image, token, ns, name, flags, _, parent, _, ms, mc, fs, fc = row
                if previous_image != image:
                    stream.write(f"// Assembly: {images[image][1]}\n\n")
                    previous_image = image
                stream.write(f"// {full_name(i)} TypeDefIndex: {i} Token: 0x{token:08X}\n")
                parent_decl = f" : {type_name(parent)}" if parent >= 0 else ""
                kind = "interface" if flags & 0x20 else "class"
                stream.write(f"{kind} {name or f'TypeDef_{i}'}{parent_decl}\n{{\n")
                if fc:
                    start = u32(data, type_aux + 12 * u32(data, type_map + i * 4) + 8)
                    fields = []
                    for j in range(fc):
                        index = fs + j
                        p = field_base + index * 12
                        k = (((index * 0xE141C3886C4 + 0x1B2824700417) & U64) >> 13)
                        k = (k * 0x194A2CA3) & U32
                        fname = symbol(u32(data, p) ^ k ^ 0x2D7AE3F7)
                        ftype = signed(((u32(data, p + 8) + 0xECEDDC0B) & U32) ^ k)
                        ftok = u32(data, p + 4) ^ k ^ 0x7E2B1E92
                        require(ftok >> 24 == 4, f"Bad field token at {index}")
                        raw = u32(data, field_offsets + 4 * (start + j))
                        offset = (raw & 0xFF000000) if raw & 0x01000000 else raw
                        offset = signed(offset)
                        ft = type_name(ftype)
                        fields.append((index, i, j, ftok, fname, ftype, ft, offset, raw))
                        label = f"0x{offset:X}" if offset >= 0 else str(offset)
                        stream.write(f"    {ft} {fname}; // Offset: {label} Token: 0x{ftok:08X}\n")
                    db.executemany("INSERT INTO fields VALUES (?,?,?,?,?,?,?,?,?)", fields)
                    counts["fields"] += len(fields)
                if mc:
                    require(0 <= ms <= method_count - mc, f"Bad method range for type {i}")
                methods, parameters = [], []
                for index in range(ms, ms + mc):
                    require(not seen_methods[index], f"Duplicate method index {index}")
                    seen_methods[index] = 1
                    p = method_base + index * 0x1E
                    x = (index * 0xB060) ^ 0x3EDAA02A
                    key = ((x * 0x08C6714773B2 + 0x1247B92FF364) >> 14) & U32
                    nk = ((x * 0x07C548C6714773B2 + 0x08A91247B92FF364) >> 14) & U32
                    owner = u32(data, p + 0x10) ^ key ^ 0x3A6C68A0
                    mtok = ((u32(data, p + 0x0C) + 0xB417C5FC) & U32) ^ nk
                    require(owner == i and mtok >> 24 == 6, f"Bad MethodDef {index}")
                    mname = symbol(u32(data, p + 8) ^ nk ^ 0x6C826DC6)
                    mflags = (u16(data, p + 0x18) ^ nk ^ 0xCC95) & 0xFFFF
                    iflags = ((u16(data, p + 0x16) + 0x757D) ^ nk) & 0xFFFF
                    slot = ((u16(data, p + 0x1A) + 0x5663) ^ nk) & 0xFFFF
                    ret = signed(u32(data, p) ^ key ^ 0x145B4810)
                    rt = type_name(ret)
                    argc = (data[p + 0x1C] ^ nk ^ 0xB6) & 0xFF
                    entry = ((u32(data, p + 0x0C) + 0x17C5FC) ^ nk) & 0xFFFFFF
                    method_array = method_arrays[image]
                    pointer = u64(native, method_array + (entry - 1) * 8) if entry and method_array is not None else 0
                    rva = pointer - pe.base if pointer else None
                    if rva is not None:
                        require(pe.executable(rva), f"Method {index} points outside executable code")
                    file_offset = pe.offset(rva) if rva is not None else None
                    methods.append((index, i, mtok, mname, mflags, iflags, slot, ret, rt,
                                    argc, rva, file_offset))
                    counts["methods_with_rva"] += rva is not None
                    args = []
                    astart = ((u32(data, p + 4) + 0xC6859FFA) & U32) ^ nk
                    for j in range(argc):
                        ai = astart + j
                        ap = param_base + ai * 12
                        ak = ((((ai * 0xFFD317E8) & U32) ^ 0x4E169601) + 0x2CA95D87) & U32
                        an = symbol(((u32(data, ap + 4) + 0x9918EA8A) & U32) ^ ak)
                        at = signed(((u32(data, ap) + 0xBB7C2BF4) & U32) ^ ak)
                        atok = ((u32(data, ap + 8) + 0xE8F78A85) & U32) ^ ak
                        require(atok >> 24 == 8, f"Bad parameter token at {ai}")
                        ats = type_name(at)
                        parameters.append((ai, index, j, atok, an, at, ats))
                        args.append(f"{ats} {an or f'arg{j}'}")
                    address = f"RVA: 0x{rva:X} FileOffset: 0x{file_offset:X}" if rva is not None else "RVA: unavailable"
                    static = "static " if mflags & 0x10 else ""
                    stream.write(f"    // {address} MethodDefIndex: {index} Token: 0x{mtok:08X}\n")
                    stream.write(f"    {static}{rt} {mname}({', '.join(args)});\n")
                db.executemany("INSERT INTO methods VALUES (?,?,?,?,?,?,?,?,?,?,?,?)", methods)
                db.executemany("INSERT INTO parameters VALUES (?,?,?,?,?,?,?)", parameters)
                counts["methods"] += len(methods)
                counts["parameters"] += len(parameters)
                stream.write("}\n\n")
        require(counts["methods"] == method_count and all(seen_methods), "Missing method records")
        metadata = {
            "game_version": "9.2.0", "module": "UserAssembly.dll",
            "preferred_image_base": f"0x{pe.base:X}", "matched_dump_files": matched_dumps,
            "counts": counts, "format": "IL2CPP symbol and metadata reference",
        }
        db.executemany("INSERT INTO metadata VALUES (?,?)",
                       ((key, json.dumps(value)) for key, value in metadata.items()))
        db.executescript("""
            CREATE INDEX types_full_name ON types(full_name);
            CREATE INDEX types_image_token ON types(image_id, token);
            CREATE INDEX methods_type_token ON methods(type_id, token);
            CREATE INDEX methods_name ON methods(name);
            CREATE INDEX methods_rva ON methods(rva) WHERE rva IS NOT NULL;
            CREATE INDEX parameters_method_position ON parameters(method_id, position);
            CREATE INDEX fields_type_position ON fields(type_id, position);
        """)
        require(db.execute("PRAGMA quick_check").fetchone()[0] == "ok", "SQLite integrity error")
        require(db.execute("PRAGMA foreign_key_check").fetchone() is None, "Invalid foreign key")
    except BaseException:
        db.close()
        database.unlink(missing_ok=True)
        draft.unlink(missing_ok=True)
        raise
    db.close()
    database.replace(output)
    draft.replace(text_output)
    print(json.dumps(metadata, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("--dump", type=Path, help="Directory of CE memory segments to match")
    parser.add_argument("--out", type=Path, default=Path("BH3_SDK_9.2.0"))
    args = parser.parse_args()
    game = args.game / "BH3_Data"
    metadata = game / "Managed" / "Metadata"
    data = (metadata / "global-metadata.dat").read_bytes()
    startup = (metadata / "startup-metadata.dat").read_bytes()
    matched = []
    if args.dump:
        for path in sorted(args.dump.glob("*.bin")):
            if path.stat().st_size < len(data) + 0x40:
                continue
            with path.open("rb") as file:
                file.seek(0x40)
                if file.read(32) == data[:32] and file.read(len(data) - 32) == data[32:]:
                    matched.append(path.name)
        require(matched, "No byte-for-byte copy of global metadata in the dump")
    args.out.mkdir(parents=True, exist_ok=True)
    with (game / "Native" / "UserAssembly.dll").open("rb") as file:
        with mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ) as native:
            export(native, data, startup, args.out, matched)


if __name__ == "__main__":
    main()
