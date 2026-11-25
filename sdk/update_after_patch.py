# -*- coding: utf-8 -*-
"""游戏更新后一键重建偏移/SDK。

用法:
  python sdk/update_after_patch.py                 # 完整重建
  python sdk/update_after_patch.py --dry-run       # 只报告，不写文件
  python sdk/update_after_patch.py --game-dir "D:\\new\\game" --dump-dir "D:\\new\\dump"

环境变量:
  BH3_GAME_DIR  游戏根目录（含 BH3_Data）
  BH3_UA        UserAssembly.dll 路径
  BH3_META      global-metadata.dat 路径
  BH3_DUMP_DIR  CE regions.csv + *.bin 转储目录（gen_sigs 命中计数用，可缺）

流程:
  1. 校验新 DLL/metadata，打印 PE 指纹
  2. sdkgen.py --dump  重建 bh3_fields.tsv / bh3_methods_named.tsv（需 regions.csv）
  3. gen_profile.py    重建 overlay/bh3_profile.inc
  4. gen_names.py      重建 overlay/bh3_names.inc
  5. gen_tokens.py     重建 overlay/bh3_tokens.inc
  6. gen_sigs.py       重建 overlay/bh3_sigs.inc
  7. 汇总每个符号的解析状态，列出仍需人工处理的项

步骤 2 失败不影响 3-6：profile 生成同时读 named 和 full 表，
只要其中一份含新 build 的 RVA 就能继续。
"""
import argparse
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PY = sys.executable

GAME_DIR = os.environ.get("BH3_GAME_DIR",
                          r"D:\Games\miHoYo Launcher\games\Honkai Impact 3rd Game")
UA = os.environ.get("BH3_UA",
                    os.path.join(GAME_DIR, "BH3_Data", "Native", "UserAssembly.dll"))
META = os.environ.get("BH3_META",
                      os.path.join(GAME_DIR, "BH3_Data", "Managed", "Metadata",
                                   "global-metadata.dat"))
DUMP = os.environ.get("BH3_DUMP_DIR", r"D:\Cheat Table\CE Search")


def pe_fingerprint(path):
    with open(path, "rb") as f:
        h = f.read(0x1000)
    e = struct.unpack_from("<I", h, 0x3C)[0]
    if h[:2] != b"MZ" or h[e:e + 4] != b"PE\0\0":
        return None, None
    ts = struct.unpack_from("<I", h, e + 8)[0]
    img = struct.unpack_from("<I", h, e + 0x50)[0]
    return ts, img


def run(name, args, env, dry):
    print("\n=== %s ===" % name)
    if dry:
        print("[dry-run] %s %s" % (PY, " ".join(args)))
        return True
    p = subprocess.run([PY] + args, cwd=HERE, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    txt = p.stdout.decode("utf-8", "replace")
    print(txt.rstrip())
    if p.returncode:
        print("[!] %s 退出码 %d" % (name, p.returncode))
        return False
    return True


def summary():
    prof = os.path.join(ROOT, "overlay", "bh3_profile.inc")
    if not os.path.exists(prof):
        return
    txt = open(prof, encoding="utf-8", errors="replace").read()
    import re
    ents = re.findall(r'\{\s*"([a-z0-9_]+)"\s*,\s*0x([0-9A-Fa-f]+)u?\s*,\s*"[0-9a-f]*"\s*,\s*(\d+)\s*,\s*(true|false)\s*\}', txt)
    bad = [k for k, rv, _, auto in ents if int(rv, 16) == 0]
    manual = [k for k, rv, _, auto in ents if auto == "false" and int(rv, 16)]
    print("\n=== 汇总 ===")
    print("符号总数 %d，RVA 缺失 %d，人工维护 %d" % (len(ents), len(bad), len(manual)))
    if bad:
        print("[!] 需人工定位 RVA: " + ", ".join(bad))
    if manual:
        print("[i] 沿用上一版、建议复核: " + ", ".join(manual))
    print("\n运行时保障:")
    print("  - PE 指纹变化时静态 RVA 全部作废，自动走按名/签名解析")
    print("  - 注册表槽 / 玩家坐标 / 传送 / NavMesh 辅助 RVA 已从函数入口动态解析")
    print("  - 若上面有缺失项，进入游戏后打开 Lua 控制台刷新一次状态可复检")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--game-dir", default=GAME_DIR)
    ap.add_argument("--dump-dir", default=DUMP)
    ap.add_argument("--skip-dump", action="store_true",
                    help="跳过 sdkgen 重建（当新 build 的 named/full 表已就位时）")
    args = ap.parse_args()

    ua = os.environ.get("BH3_UA", os.path.join(args.game_dir, "BH3_Data", "Native", "UserAssembly.dll"))
    meta = os.environ.get("BH3_META", os.path.join(args.game_dir, "BH3_Data", "Managed",
                                                   "Metadata", "global-metadata.dat"))
    print("UA:   %s" % ua)
    print("META: %s" % meta)
    print("DUMP: %s" % args.dump_dir)

    ok = True
    for label, path in (("UserAssembly.dll", ua), ("global-metadata.dat", meta)):
        if not os.path.exists(path):
            print("[!] 缺少 %s: %s" % (label, path))
            ok = False
        else:
            print("[+] %s 存在 (%d 字节)" % (label, os.path.getsize(path)))
    if not ok:
        print("\n请先用 --game-dir 指向新版本游戏根目录，或设置 BH3_GAME_DIR。")
        return 1

    ts, img = pe_fingerprint(ua)
    print("PE 指纹: TimeDateStamp=0x%08X SizeOfImage=0x%08X" % (ts or 0, img or 0))

    env = dict(os.environ)
    env["BH3_UA"] = ua
    env["BH3_META"] = meta
    env["BH3_GAME_DIR"] = args.game_dir
    env["BH3_DUMP_DIR"] = args.dump_dir

    if not args.skip_dump:
        regions = os.path.join(args.dump_dir, "regions.csv")
        if os.path.exists(regions):
            run("1. SDK dump 重建", ["sdkgen.py", "--dump"], env, args.dry_run)
        else:
            print("\n=== 1. SDK dump 重建 ===")
            print("[skip] 无 %s，跳过；将使用现有 SDK 表" % regions)
    else:
        print("\n=== 1. SDK dump 重建 ===")
        print("[skip] --skip-dump")

    steps = [
        ("2. profile 生成", ["gen_profile.py"],
         {"BH3_UA": ua, "BH3_DUMP_DIR": args.dump_dir}),
        ("3. names 生成", ["gen_names.py"], {}),
        ("4. tokens 生成", ["gen_tokens.py"], {}),
        ("5. sigs 生成", ["gen_sigs.py"], {"BH3_UA": ua}),
    ]
    for name, cmd, extra_env in steps:
        e = dict(env)
        e.update(extra_env)
        if not run(name, cmd, e, args.dry_run):
            ok = False

    if not args.dry_run:
        summary()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
