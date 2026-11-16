# 一次性脚本：把 overlay/ui 源码里 \xNN 形式的中文字符串还原成真正的 UTF-8 中文
import re
import pathlib
import sys

ROOT = pathlib.Path(r"D:\My Projects\Honkai Impact")
FILES = [
    ROOT / "overlay" / "ui" / "App.cpp",
    ROOT / "overlay" / "ui" / "PageActivity.cpp",
    ROOT / "overlay" / "ui" / "Pages.cpp",
]

PAT = re.compile(rb"(?:\\x[0-9a-fA-F]{2})+")
HEX = re.compile(rb"\\x([0-9a-fA-F]{2})")


def repl(m):
    bs = bytes(int(h, 16) for h in HEX.findall(m.group(0)))
    try:
        return bs.decode("utf-8").encode("utf-8")
    except UnicodeDecodeError as e:
        sys.exit("非 UTF-8 转义序列: %r (%s)" % (m.group(0), e))


total = 0
for f in FILES:
    raw = f.read_bytes()
    n = len(PAT.findall(raw))
    out, cnt = PAT.subn(repl, raw)
    if cnt:
        f.write_bytes(out)
    total += cnt
    print("%-40s 替换 %d 组" % (f.name, cnt))
print("合计", total, "组")
