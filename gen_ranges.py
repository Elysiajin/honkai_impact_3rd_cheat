# 扫描 overlay 源码里实际出现的字符，生成字体图集用的字符表
import glob
import os
import re

ROOT = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(ROOT, "overlay", "ui", "_ranges.inc")


def decode(path):
    raw = open(path, "rb").read()
    # C++ 源码既有 \xNN 转义也有直接写入的 UTF-8，统一还原成字节再解码
    data = re.sub(rb"\\x([0-9a-fA-F]{2})", lambda m: bytes([int(m.group(1), 16)]), raw)
    return data.decode("utf-8", errors="ignore")


chars = set()
for f in glob.glob(os.path.join(ROOT, "overlay", "**", "*.cpp"), recursive=True) + \
        glob.glob(os.path.join(ROOT, "overlay", "**", "*.h"), recursive=True):
    chars.update(ord(c) for c in decode(f) if ord(c) >= 0x80)

lines = []
line = "   "
for cp in sorted(chars):
    esc = "".join("\\x%02X" % b for b in chr(cp).encode("utf-8"))
    if len(line) + len(esc) > 100:
        lines.append(line)
        line = "   "
    line += esc

lines.append(line)
body = "\n".join('    "%s"' % l.strip() for l in lines if l.strip())

with open(OUT, "w", encoding="utf-8", newline="\n") as fp:
    fp.write("// 由根目录 gen_ranges.py 生成：overlay 源码用到的非 ASCII 字符\n")
    fp.write("static const char kUiGlyphs[] =\n")
    fp.write(body + ";\n")

print("chars:", len(chars), "lines:", len(lines), "->", OUT)
