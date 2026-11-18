# -*- coding: utf-8 -*-
"""按 il2cpp-api-functions.h 的 DO_API 顺序给 out/il2cpp_api_table.json 的槽位定名,
并用反汇编 prologue 特征验证关键槽位 (object_new / string_new / thread_attach / domain_get / class_from_name).

用法: python sdk/name_api_slots.py
输出: out/il2cpp_api_named.json  (slot -> {name, fn_rva})
"""
import json
import os
import re
import struct
import sys

import capstone
import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UA = r"D:\Games\miHoYo Launcher\games\Honkai Impact 3rd Game\BH3_Data\Native\UserAssembly.dll"
TABLE_JSON = os.path.join(ROOT, "out", "il2cpp_api_table.json")
OUT_JSON = os.path.join(ROOT, "out", "il2cpp_api_named.json")
HDR = os.path.join(ROOT, "sdk", "il2cpp-api-functions.h")

NSLOT = 1215

# ---------------------------------------------------------------- 头文件顺序
def parse_hdr_order():
    """返回 (names_list, profiler_first_idx or None, profiler_last_idx or None).
    names 按 DO_API 出现顺序; profiler 段单独标记 (它可能被 #if 剔除)."""
    text = open(HDR, encoding="utf-8").read()
    names = []
    in_prof = False
    prof_range = [None, None]
    for m in re.finditer(r"^\s*DO_API(?:_NO_RETURN)?\s*\(\s*[\w:\*\s\.,&\[\]]+?\s*,\s*(il2cpp_\w+)\s*,", text, re.M):
        # 需要知道这条是否在 profiler #if 里 —— 改用逐行扫描
        pass
    names = []
    in_prof = False
    for i, line in enumerate(text.splitlines()):
        if "#if IL2CPP_ENABLE_PROFILER" in line:
            in_prof = True
            prof_range[0] = len(names)
            continue
        if "#endif" in line and in_prof:
            in_prof = False
            prof_range[1] = len(names) - 1
            continue
        m = re.match(r"\s*DO_API(?:_NO_RETURN)?\s*\(\s*[^,]+,\s*(il2cpp_\w+)\s*,", line)
        if m:
            names.append(m.group(1))
    return names, prof_range[0], prof_range[1]


# ---------------------------------------------------------------- 反汇编特征
def fn_bytes(pe_img, rva, n=48):
    if rva <= 0 or rva + n >= len(pe_img):
        return b""
    return bytes(pe_img[rva:rva + n])


def disas(pro, rva):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    out = []
    for ins in md.disasm(pro, rva):
        out.append("%s %s" % (ins.mnemonic, ins.op_str))
        if len(out) >= 14:
            break
    return out


def match_feature(rva, pe_img):
    """返回 (feature_name or None, disasm_text)"""
    pro = fn_bytes(pe_img, rva, 64)
    if not pro:
        return None, "<unmapped>"
    d = disas(pro, rva)
    txt = "; ".join(d)
    # il2cpp_object_new: jmp/thunk 到 vm::Object::New —— 典型: 短 thunk, 先调用再返回
    # 2017.4 实现: il2cpp_object_new -> vm::Object::New(klass), 是真函数而非 thunk.
    # 特征(GC alloc): call qword ptr [rip+X] (GC::Allocate) 或 call <alloc>,
    #   随后 mov [rax], rcx (写 klass). 抽查真函数体.
    if re.search(r"call[^;]*; mov \[rax\], rcx", txt) or \
       ("mov [rax], rcx" in txt) or ("mov qword ptr [rax], rcx" in txt):
        return "object_new?", txt
    if "object_new?" in txt:
        return "object_new?", txt
    return None, txt


def main():
    names, p0, p1 = parse_hdr_order()
    print("hdr names: %d (profiler idx %s..%s)" % (len(names), p0, p1))
    ents = json.load(open(TABLE_JSON))
    assert len(ents) == NSLOT, len(ents)

    pe = pefile.PE(UA, fast_load=True)
    img = pe.get_memory_mapped_image()

    # 两种假设: (A) profiler 计入槽位; (B) profiler 被剔除
    variants = {}
    for tag, drop_prof in (("with_profiler", False), ("no_profiler", True)):
        seq = []
        for i, n in enumerate(names):
            if drop_prof and p0 is not None and p0 <= i <= p1:
                continue
            seq.append(n)
        variants[tag] = seq
        print("%s: %d names for %d slots" % (tag, len(seq), NSLOT))

    # 选数量最接近 NSLOT 的假设
    best = min(variants.items(), key=lambda kv: abs(len(kv[1]) - NSLOT))
    print("chosen variant:", best[0])
    seq = best[1]

    named = {}
    for i, n in enumerate(seq):
        if i < NSLOT:
            named[i] = {"name": n, "fn_rva": ents[i]["fn_rva"]}

    # 特征验证关键槽位
    keys = ["il2cpp_object_new", "il2cpp_string_new", "il2cpp_thread_attach",
            "il2cpp_domain_get", "il2cpp_class_from_name", "il2cpp_array_new",
            "il2cpp_runtime_invoke", "il2cpp_alloc", "il2cpp_gc_collect"]
    out = {}
    for slot, d in sorted(named.items()):
        out[str(slot)] = d
    for k in keys:
        # 找槽位
        hit = [s for s, d in named.items() if d["name"] == k]
        for s in hit:
            rva = named[s]["fn_rva"]
            feat, txt = match_feature(rva, img)
            print("slot %d %s -> RVA 0x%X  feat=%s" % (s, k, rva, feat))
            print("   " + txt[:220])
    with open(OUT_JSON, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    print("wrote", OUT_JSON)


if __name__ == "__main__":
    main()
