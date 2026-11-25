import csv
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

SYMBOLS = {
    "camera_main": ("Camera", "get_main"),
    "camera_pixel_w": ("Camera", "get_pixelWidth"),
    "camera_pixel_h": ("Camera", "get_pixelHeight"),
    "camera_w2c": ("Camera", "INTERNAL_get_worldToCameraMatrix"),
    "camera_proj": ("Camera", "INTERNAL_get_projectionMatrix"),
    "die_now": ("BaseMonoMonster", "DieNow"),
    "be_hit": ("BaseMonoAvatar", "BeHit"),
    "ui_skill_in_cd": ("MonoSkillButton", "IsSkillInCD"),
    "ui_can_use_skill": ("MonoSkillButton", "CanUseSkill"),
    "avatar_can_use_skill": ("BaseMonoAvatar", "CanUseSkill"),
    "crit_main": ("MHEMGJMNDJI", "TryApplyCrit"),
    "crit_phys": ("MHEMGJMNDJI", "TryApplyPhysicCrit"),
    "crit_elem": ("MHEMGJMNDJI", "TryApplyElementCrit"),
    "dmg_final": ("MHEMGJMNDJI", "ResolveAttackDataFinal"),
    "anim_speed_apply": ("BaseMonoAnimatorEntity", "ApplyOverrideAnimatorSpeed"),
    "anim_speed_cancel": ("BaseMonoAnimatorEntity", "CancelOverrideAnimatorSpeed"),
    # 寻路：RAW 符号也要给出 klass/token（live fallback 用）；
    # CalculatePath/SamplePosition 各 2 重载 → token 不唯一，先留空（无 live fallback，
    # 仅静态 RVA 生效），triangulation 唯一可给。
    "navmesh_calculate_triangulation": ("NavMesh", "CalculateTriangulation"),
    "root_node_position": ("BaseMonoEntity", "get_RootNodePosition"),
    "get_local_avatar": ("AvatarManager", "GetLocalAvatar"),
    "teleport_to": ("BaseMonoAnimatorEntity", "TeleportTo"),
    "get_singleton_impl": ("SingletonManager", "GetSingletonInstance"),
}

ULTRA = [
    "MonoFuka", "MonoFuka_BattleY",
    "MonoFukaFL", "MonoFukaFL_BattleY",
    "MonoFukaIN", "MonoFukaIN_BattleY",
    "MonoFukaJW", "MonoFukaJW_BattleY",
    "MonoFukaND", "MonoFukaND_BattleY",
    "MonoFukaNL", "MonoFukaNL_BattleY",
]


def load_tokens(path, extra=None):
    out = {}
    for p in (path, extra):
        if not p or not os.path.exists(p):
            continue
        with open(p, encoding="utf-8", errors="replace") as f:
            for row in csv.DictReader(f, delimiter="\t"):
                rva = int(row["rva"], 0)
                tok = int(row["token"], 0)
                if not rva or not tok:
                    continue
                cls = row["class"]
                if "." in cls:
                    cls = cls.rsplit(".", 1)[-1]
                out.setdefault((cls, row["method"]), set()).add(tok)
    return out


PROFILE_RE = re.compile(
    r'\{\s*"([a-z0-9_]+)"\s*,\s*0x([0-9A-Fa-f]+)u?\s*,\s*"([0-9a-f]*)"\s*,\s*(\d+)\s*,\s*(true|false)\s*\}')


def parse_profile(path):
    txt = open(path, encoding="utf-8", errors="replace").read()
    out = []
    for m in PROFILE_RE.finditer(txt):
        out.append({
            "key": m.group(1),
            "rva": int(m.group(2), 16),
            "plen": int(m.group(4)),
        })
    return out


def main():
    tokens = load_tokens(os.path.join(HERE, "bh3_methods_named.tsv"),
                         os.path.join(HERE, "bh3_methods_full.tsv"))
    entries = parse_profile(os.path.join(ROOT, "overlay", "bh3_profile.inc"))

    rows = []
    missing = []
    for i, e in enumerate(entries):
        key = e["key"]
        cls = ""
        tok = 0
        delta = 0
        if key in SYMBOLS:
            c, m = SYMBOLS[key]
            hits = tokens.get((c, m))
            if hits and len(hits) == 1:
                cls = c
                tok = next(iter(hits))
        elif key.startswith("ultra"):
            c = ULTRA[int(key[5:])]
            hits = tokens.get((c, "AllowUseUltra"))
            if hits and len(hits) == 1:
                cls = c
                tok = next(iter(hits))
        else:
            missing.append(key)

        if cls and key in SYMBOLS and key not in (
                "root_node_position", "get_local_avatar", "teleport_to", "get_singleton_impl"):
            fname = SYMBOLS[key][1]
            need = None
            for row in csv.DictReader(
                    open(os.path.join(HERE, "bh3_methods_named.tsv"),
                         encoding="utf-8", errors="replace"),
                    delimiter="\t"):
                if row["class"] == cls and row["method"] == fname and int(row["rva"], 0):
                    need = int(row["rva"], 0)
                    break
            if need:
                delta = e["rva"] - need

        rows.append((cls, tok, delta))

    lines = []
    lines.append("// 自动生成: python sdk/gen_names.py —— 请勿手改")
    lines.append("namespace ovp")
    lines.append("{")
    lines.append("")
    lines.append("struct GenName")
    lines.append("{")
    lines.append("    const char* klass;")
    lines.append("    uint32_t    token;")
    lines.append("    uint32_t    delta;")
    lines.append("};")
    lines.append("")
    lines.append("static const GenName kGenNames[SymCount] =")
    lines.append("{")
    ntok = 0
    for cls, tok, delta in rows:
        if cls:
            ntok += 1
        lines.append('    { "%s", 0x%08Xu, 0x%Xu },' % (cls, tok, delta))
    lines.append("};")
    lines.append("")
    lines.append("constexpr int kNameCount = %d;" % ntok)
    lines.append("")
    lines.append("} // namespace ovp")
    lines.append("")

    out = os.path.join(ROOT, "overlay", "bh3_names.inc")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print("wrote %s (%d entries, %d with token)" % (out, len(rows), ntok))
    print("no token: %s" % ", ".join(missing))


if __name__ == "__main__":
    main()
