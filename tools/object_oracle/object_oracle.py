#!/usr/bin/env python3
"""Independent oracle for the RotWK object model (lane OBJ-1, INI port steps 7-10).

It parses the retail Object / ChildObject / ObjectReskin definitions its OWN way and writes the
per-template module lists the engine's retail golden test compares against. It imports no engine
code and no tools/census; it reads

  * the retail archives (ROTWK_INSTALL, BFME2_INSTALL) through its own BIG reader and the mount
    order documented in engine/src/Common/RetailArchivePolicy.h (RotWK first, then BFME2; inside an
    install the policy's canonical names sorted by strcmp, then reversed; first provider wins),
    with the archive lists from engine/data/retail-archives/*.json (names, sizes, hashes only);
  * the module registry golden (engine/data/rotwk-201/module-registry.json): class -> type, interface
    mask and the two ModuleData predicates. That is binary data the oracle takes as input; what is
    under test is the INI parsing and the inheritance logic, not the registry.

How it finds structure (the engine uses the binary's per-class field tables; this does not):
the INI format has no generic grammar, a block opens wherever a field's parse function reads a
nested block, so a lexer alone cannot find the End that closes a module body. The oracle learns
which first tokens open blocks from the corpus itself: every file is parsed with a maximum
likelihood bracket parse (dynamic programming over nesting depth) whose priors are the indentation
statistics of the corpus (a token followed by a deeper-indented line almost always opens a block) and
whose hard constraint is that every End closes something and every file ends at depth 0. Files the
parse cannot balance are reported (never silently accepted).

Usage:
    ROTWK_INSTALL=... BFME2_INSTALL=... python tools/object_oracle/object_oracle.py \\
        --out <modules.tsv> [--histogram <hist.tsv>] [--census-json <census.json>] [--cinematics]

Output (deterministic, LF): M lines (id, class, tag) then one T line per template in load order,
    T <name> <Object|ChildObject|ObjectReskin> <module ids...>
"""
from __future__ import annotations

import argparse
import collections
import json
import math
import os
import re
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
POLICY_DIR = REPO / "engine" / "data" / "retail-archives"
REGISTRY_JSON = REPO / "engine" / "data" / "rotwk-201" / "module-registry.json"

HEADERS = ("object", "childobject", "objectreskin")
# The block keywords of the RotWK INI loader (spec ini-and-object-model.md 3.2, 131 entries): a line whose first
# token is one of them, outside every block, opens a top-level block (object files also hold OCLs, FX, ...).
# Kept here as a literal so the oracle shares no code with the engine; the keyword match is case sensitive.
TOP_LEVEL_KEYWORDS = frozenset((
    "AIBase",
    "AIData",
    "AIDozerAssignment",
    "AerialPathfindNoFlyZone",
    "AmbientStream",
    "AnimationSoundClientBehaviorGlobalSetting",
    "AptButtonTooltipMap",
    "Armor",
    "ArmyDefinition",
    "ArmySummaryDescription",
    "AudioEvent",
    "AudioLOD",
    "AudioLowMHz",
    "AudioSettings",
    "AutoResolveArmor",
    "AutoResolveBody",
    "AutoResolveCombatChain",
    "AutoResolveHandicapLevel",
    "AutoResolveLeadership",
    "AutoResolveReinforcementSchedule",
    "AutoResolveWeapon",
    "AwardSystem",
    "BannerType",
    "BenchProfile",
    "Bridge",
    "ChildObject",
    "CloudBreakEffect",
    "CloudEffect",
    "CommandButton",
    "CommandMap",
    "CommandSet",
    "ControlBarResizer",
    "ControlBarScheme",
    "CrateData",
    "CreateAHeroSystem",
    "CrowdResponse",
    "DamageFX",
    "DebugCommandMap",
    "DialogEvent",
    "DrawGroupInfo",
    "DynamicGameLOD",
    "EmotionNugget",
    "EvaEventForwardReference",
    "ExperienceLevel",
    "ExperienceScalarTable",
    "FXList",
    "FXParticleSystem",
    "FactionVictoryData",
    "Fire",
    "FireEffect",
    "FireLogicSystem",
    "FontDefaultSettings",
    "FontSubstitution",
    "FormationAssistant",
    "GameData",
    "GlowEffect",
    "HeaderTemplate",
    "HouseColor",
    "InGameNotificationBox",
    "LODPreset",
    "Language",
    "LargeGroupAudioMap",
    "LargeGroupAudioUnusedKnownKeys",
    "LightPointLevel",
    "LinearCampaign",
    "LivingWorldAITemplate",
    "LivingWorldAnimObject",
    "LivingWorldArmyIcon",
    "LivingWorldAutoResolveResourceBonus",
    "LivingWorldAutoResolveSciencePurchasePointBonus",
    "LivingWorldBuildPlotIcon",
    "LivingWorldBuilding",
    "LivingWorldBuildingIcon",
    "LivingWorldCampaign",
    "LivingWorldMapInfo",
    "LivingWorldObject",
    "LivingWorldPlayerArmy",
    "LivingWorldPlayerTemplate",
    "LivingWorldRegionCampaign",
    "LivingWorldRegionEffects",
    "LivingWorldSound",
    "LoadSubsystem",
    "Locomotor",
    "MappedImage",
    "MeshNameMatches",
    "MiscAudio",
    "MiscEvaData",
    "MissionObjectiveList",
    "ModifierList",
    "Mouse",
    "MouseCursor",
    "MultiplayerColor",
    "MultiplayerSettings",
    "Multisound",
    "MusicTrack",
    "NewEvaEvent",
    "Object",
    "ObjectCreationList",
    "ObjectReskin",
    "OnlineChatColors",
    "Pathfinder",
    "PlayerAIType",
    "PlayerTemplate",
    "PredefinedEvaEvent",
    "Rank",
    "ReallyLowMHz",
    "RingEffect",
    "Road",
    "Science",
    "ScoredKillEvaAnnouncer",
    "ScriptAction",
    "ScriptCondition",
    "ShadowMap",
    "ShellMenuScheme",
    "SkirmishAIData",
    "SkyboxTextureSet",
    "SpecialPower",
    "StanceTemplate",
    "StaticGameLOD",
    "StrategicHUD",
    "StreamedSound",
    "Terrain",
    "Upgrade",
    "VictorySystemData",
    "WaterSet",
    "WaterTextureList",
    "WaterTransparency",
    "Weapon",
    "Weather",
    "WeatherData",
    "WindowTransition",
))
MODULE_KEYS = {"behavior": 0, "body": 999, "draw": 1, "clientupdate": 2, "clientbehavior": 3}
EDIT_KEYS = ("addmodule", "inheritablemodule", "replacemodule", "removemodule")
BODY_MASK = 0x20


class OracleError(RuntimeError):
    pass


# ------------------------------------------------------------------------------------------
# mounting
# ------------------------------------------------------------------------------------------
def read_big(path: Path):
    data = path.read_bytes()
    if data[:4] not in (b"BIGF", b"BIG4"):
        raise OracleError(f"{path}: not a BIG archive")
    count = struct.unpack_from(">I", data, 8)[0]
    pos = 16
    entries = []
    for _ in range(count):
        off, size = struct.unpack_from(">II", data, pos)
        pos += 8
        end = data.index(b"\0", pos)
        entries.append((data[pos:end].decode("latin-1"), off, size))
        pos = end + 1
    return data, entries


def mount(rotwk: Path, bfme2: Path):
    """virtual path (lower case, backslashes) -> (archive path, offset, size); first provider wins."""
    files: dict[str, tuple] = {}
    for install, pol in ((rotwk, "rotwk-201-english-archives.json"), (bfme2, "bfme2-106-english-archives.json")):
        policy = json.loads((POLICY_DIR / pol).read_text(encoding="utf-8"))
        canon = [a["path"].replace("/", "\\") for a in policy["archives"]]
        order = sorted(canon)  # strcmp over the canonical names ...
        order.reverse()        # ... then reversed (RetailArchivePolicy.h)
        disk = {p.relative_to(install).as_posix().lower(): p for p in install.rglob("*") if p.is_file()}
        for name in order:
            path = disk.get(name.replace("\\", "/").lower())
            if path is None:
                raise OracleError(f"missing archive {name} under {install}")
            data, entries = read_big(path)
            for n, off, size in entries:
                key = n.replace("/", "\\").lower()
                if key not in files:
                    files[key] = (path, off, size)
    return files


def read_file(files, key):
    path, off, size = files[key]
    with open(path, "rb") as fh:
        fh.seek(off)
        return fh.read(size)


# ------------------------------------------------------------------------------------------
# lexing
# ------------------------------------------------------------------------------------------
def split_lines(raw: bytes):
    return re.split(r"\r\n|\n|\r", raw.decode("latin-1"))


def strip_comment(line: str) -> str:
    cut = len(line)
    for m in (line.find(";"), line.find("//")):
        if m != -1:
            cut = min(cut, m)
    return line[:cut]


INCLUDE = re.compile(r'^\s*#include\s+"?([^"\s]+)"?')


def normalize(path: str) -> str:
    parts = []
    for p in path.replace("/", "\\").lower().split("\\"):
        if p in ("", "."):
            continue
        if p == "..":
            if parts:
                parts.pop()
            continue
        parts.append(p)
    return "\\".join(parts)


def expand(files, key, errors, stack=()):
    """lines of the file with #include expanded in place: [(file key, line number, text)]"""
    if key in stack:
        errors.append(f"circular include {key}")
        return []
    out = []
    here = key.rsplit("\\", 1)[0] if "\\" in key else ""
    for n, raw in enumerate(split_lines(read_file(files, key)), 1):
        stripped = raw.lstrip()
        m = INCLUDE.match(raw)
        if m and not stripped.startswith((";", "//")):
            target = normalize((here + "\\" if here else "") + m.group(1))
            if target not in files:
                errors.append(f"{key}:{n}: include {m.group(1)} not found")
                continue
            out.extend(expand(files, target, errors, stack + (key,)))
        else:
            out.append((key, n, raw))
    return out


def indent_of(text: str) -> int:
    lead = text[: len(text) - len(text.lstrip(" \t"))]
    return len(lead.expandtabs(4))


FORCED_CLOSE_COST = 40.0
TOPLEVEL_JUNK_COST = 12.0  # a non-header line outside every block: other top-level blocks exist, but are rare


class Item:
    __slots__ = ("file", "line", "indent", "tokens", "is_end", "text", "header_col0")

    def __init__(self, file, line, indent, tokens, text):
        self.file = file
        self.line = line
        self.indent = indent
        self.tokens = tokens
        self.is_end = tokens[0].lower() == "end"
        self.text = text
        self.header_col0 = indent == 0 and tokens[0] in TOP_LEVEL_KEYWORDS and "=" not in text


def lex(lines):
    """items of one file: comments stripped, blank lines and script bodies dropped"""
    items = []
    inscript = False
    for fkey, n, raw in lines:
        s = strip_comment(raw)
        toks = s.replace("=", " ").split()
        if not toks:
            continue
        if inscript:
            if toks[0].lower() == "endscript":
                inscript = False
            continue
        if toks[0].lower() == "beginscript":
            inscript = True
            continue
        if toks[0].lower() == "#define":
            continue
        items.append(Item(fkey, n, indent_of(s), toks, s))
    return items


# ------------------------------------------------------------------------------------------
# structure: maximum likelihood bracket parse
# ------------------------------------------------------------------------------------------
def key2(it):
    """(first token, second token) with the second token lower-cased: some fields open a block only for one
    keyword after them (AddEmotion = OVERRIDE <name> ... End)"""
    return (it.tokens[0], it.tokens[1].lower() if len(it.tokens) > 1 else "")


class Priors:
    def __init__(self):
        self.votes = collections.defaultdict(lambda: [0, 0])
        self.votes2 = collections.defaultdict(lambda: [0, 0])

    def learn(self, items):
        for k, it in enumerate(items):
            if it.is_end or (it.tokens[0].lower() in HEADERS and "=" not in it.text):
                continue
            if k + 1 >= len(items):
                continue
            nxt = items[k + 1]
            if nxt.is_end:
                continue
            side = 0 if nxt.indent > it.indent else 1
            self.votes[it.tokens[0]][side] += 1
            self.votes2[key2(it)][side] += 1

    def p_open(self, it):
        o2, p2 = self.votes2.get(key2(it), (0, 0))
        # a pair is its own feature only when it was seen often enough and behaves differently from the bare token
        second = it.tokens[1] if len(it.tokens) > 1 else ""
        if o2 + p2 >= 8 and second.isalpha() and second.isupper():
            # a field that is almost always a plain line (>= 100 sightings, < 10 % openers) but opens a block
            # when its value is a keyword like OVERRIDE (AddEmotion = OVERRIDE <name> ... End)
            o1, p1 = self.votes.get(it.tokens[0], (0, 0))
            if o1 + p1 >= 100 and o1 / (o1 + p1) < 0.1 and o2 / (o2 + p2) > 0.9:
                return (o2 + 0.5) / (o2 + p2 + 1.0)
        o, p = self.votes.get(it.tokens[0], (0, 0))
        if o + p == 0:
            return 0.15
        return (o + 0.5) / (o + p + 1.0)


def parse_structure(items, priors):
    """depth of every item and, for openers, True. Returns (depths, openers) or raises OracleError."""
    n = len(items)
    INF = float("inf")
    DMAX = 40
    cost = [INF] * (DMAX + 2)
    cost[0] = 0.0
    back = []  # per item: dict depth -> (prev depth, is_opener)
    for k, it in enumerate(items):
        nxt = items[k + 1] if k + 1 < n else None
        new = [INF] * (DMAX + 2)
        bk = {}
        for d in range(DMAX + 1):
            c = cost[d]
            if c == INF:
                continue
            if it.is_end:
                if d >= 1 and c < new[d - 1]:
                    new[d - 1] = c
                    bk[d - 1] = (d, False)
                continue
            if d == 0:
                # top level: a header, or a top-level block of another kind (OCL, weapon...): opens
                is_header = it.tokens[0] in TOP_LEVEL_KEYWORDS and "=" not in it.text
                extra = 0.0 if is_header else TOPLEVEL_JUNK_COST
                if c + extra < new[1]:
                    new[1] = c + extra
                    bk[1] = (0, True)
                continue
            if it.header_col0:
                # an Object / ChildObject / ObjectReskin header in column 0 is a top-level line: whatever is
                # still open is force-closed, at a heavy price that marks the object before it as unbalanced
                if c + FORCED_CLOSE_COST * d < new[1]:
                    new[1] = c + FORCED_CLOSE_COST * d
                    bk[1] = (d, True)
                continue
            p = priors.p_open(it)
            c_open = -math.log(p)
            c_plain = -math.log(1.0 - p)
            if nxt is not None:
                if nxt.is_end:
                    if nxt.indent >= it.indent:
                        c_open += 0.5
                        c_plain += 0.5
                    else:
                        c_open += 1.5
                elif nxt.indent > it.indent:
                    c_plain += 1.5
                else:
                    c_open += 1.5
            # an Object/ChildObject/ObjectReskin line with no '=' at depth >= 1 is not a header here
            if d + 1 <= DMAX and c + c_open < new[d + 1]:
                new[d + 1] = c + c_open
                bk[d + 1] = (d, True)
            if c + c_plain < new[d]:
                new[d] = c + c_plain
                bk[d] = (d, False)
        back.append(bk)
        cost = new
    if cost[0] == INF:
        raise OracleError("file cannot be balanced")
    d = 0
    depths = [0] * n
    openers = [False] * n
    for k in range(n - 1, -1, -1):
        pd, op = back[k][d]
        depths[k] = pd
        openers[k] = op
        d = pd
    return depths, openers


# ------------------------------------------------------------------------------------------
# object definitions
# ------------------------------------------------------------------------------------------
class Decl:
    __slots__ = ("kind", "cls", "tag", "where", "mode")

    def __init__(self, kind, cls, tag, where, mode):
        self.kind = kind      # behavior | body | draw | clientupdate | clientbehavior | remove | replace-start
        self.cls = cls
        self.tag = tag
        self.where = where    # "file:line"
        self.mode = mode      # normal | add | inheritable | replace


class ObjectDef:
    def __init__(self, kind, name, parent, where):
        self.kind = kind      # Object | ChildObject | ObjectReskin
        self.name = name
        self.parent = parent
        self.where = where
        self.ops = []         # ordered Decl / ('remove', tag) / ('replace', tag) records


def collect_objects(items, depths, openers, problems):
    """The object definitions of one file, with their module declarations and edits in order."""
    objs = []
    n = len(items)
    k = 0
    while k < n:
        it = items[k]
        if (it.header_col0 or (depths[k] == 0 and openers[k])) and it.tokens[0].lower() in HEADERS and "=" not in it.text:
            kind = {"object": "Object", "childobject": "ChildObject", "objectreskin": "ObjectReskin"}[it.tokens[0].lower()]
            if len(it.tokens) < 2:
                raise OracleError(f"{it.file}:{it.line}: header without a name")
            parent = it.tokens[2] if kind != "Object" and len(it.tokens) > 2 else ""
            if kind != "Object" and not parent:
                raise OracleError(f"{it.file}:{it.line}: {kind} {it.tokens[1]} without a parent")
            obj = ObjectDef(kind, it.tokens[1], parent, f"{it.file}:{it.line}")
            # scan the body: depths[] is the depth BEFORE the line, so the End that closes the object
            # is the first End seen at depth 1
            j = k + 1
            edit_stack = []  # (depth of the editing block's header line, mode)
            while j < n:
                b = items[j]
                dj = depths[j]
                if b.header_col0 and (dj > 0 or depths[j - 1] > 0 and False):
                    problems.append(f"{obj.where}: {obj.kind} {obj.name} is unbalanced (the next header forced it closed)")
                    j -= 1
                    break
                if b.is_end:
                    if dj == 1:
                        break
                    if edit_stack and edit_stack[-1][0] == dj - 1:
                        edit_stack.pop()
                    j += 1
                    continue
                key = b.tokens[0].lower()
                where = f"{b.file}:{b.line}"
                if key == "removemodule" and dj == 1:
                    obj.ops.append(("remove", b.tokens[1] if len(b.tokens) > 1 else "", where))
                elif key in ("addmodule", "inheritablemodule", "replacemodule") and dj == 1:
                    if not openers[j]:
                        problems.append(f"{where}: {b.tokens[0]} is not a block in the oracle's structure")
                    if key == "replacemodule":
                        obj.ops.append(("replace", b.tokens[1] if len(b.tokens) > 1 else "", where))
                    edit_stack.append((dj, {"addmodule": "add", "inheritablemodule": "inheritable", "replacemodule": "replace"}[key]))
                elif key in MODULE_KEYS:
                    mode = None
                    if dj == 1:
                        mode = "normal"
                    elif dj == 2 and edit_stack and edit_stack[-1][0] == 1:
                        mode = edit_stack[-1][1]
                    if mode is not None:
                        if not openers[j]:
                            problems.append(f"{where}: module declaration {b.tokens[0]} {b.tokens[1] if len(b.tokens) > 1 else ''} is not a block in the oracle's structure")
                        if len(b.tokens) < 3:
                            problems.append(f"{where}: module declaration without class and tag")
                            j += 1
                            continue
                        obj.ops.append(Decl(key, b.tokens[1], b.tokens[2], where, mode))
                j += 1
            else:
                problems.append(f"{obj.where}: {obj.kind} {obj.name} runs to the end of the file")
            objs.append(obj)
            k = j + 1
            continue
        k += 1
    return objs


# ------------------------------------------------------------------------------------------
# inheritance (spec ini-and-object-model.md 4.2, 4.4, 4.6, 4.7; RW 0x73F24D, 0x73EF3B)
# ------------------------------------------------------------------------------------------
class Registry:
    def __init__(self, path):
        j = json.loads(Path(path).read_text(encoding="utf-8"))
        self.classes = {(c["name"], c["type"]): c for c in j["classes"]}

    def find(self, name, mtype):
        return self.classes.get((name, mtype))


class Nugget:
    __slots__ = ("cls", "tag", "mask", "ai", "slot7", "copied", "inherit")

    def __init__(self, cls, tag, mask, ai, slot7, inherit):
        self.cls, self.tag, self.mask, self.ai, self.slot7 = cls, tag, mask, ai, slot7
        self.copied, self.inherit = False, inherit

    def clone(self):
        n = Nugget(self.cls, self.tag, self.mask, self.ai, self.slot7, self.inherit)
        n.copied = self.copied
        return n


class Template:
    def __init__(self, name, kind):
        self.name, self.kind = name, kind
        self.lists = [[], [], [], []]  # behavior(+body), draw, clientupdate, clientbehavior

    def copy_from(self, other):
        self.lists = [[n.clone() for n in lst] for lst in other.lists]

    def mark_copied(self):
        for lst in self.lists:
            for n in lst:
                n.copied = True

    def flat(self):
        return [(n.cls, n.tag) for lst in self.lists for n in lst]


def apply_decl(tpl, d, reg, is_child, errors, replacing):
    mtype = MODULE_KEYS[d.kind]
    if mtype == 999:
        info = reg.find(d.cls, 0)
        if info is None or not (info["mask"] & BODY_MASK):
            errors.append(f"{d.where}: Body = {d.cls} is not a body class")
            return
        mtype = 0
    else:
        info = reg.find(d.cls, mtype)
        if info is None:
            errors.append(f"{d.where}: unknown module class {d.cls} (type {mtype})")
            return
        if info["mask"] & BODY_MASK:
            errors.append(f"{d.where}: {d.cls} is a body class declared as a non-Body module")
            return
    mask = info["mask"]
    if not is_child:  # load type 1: forget the copied-from-default modules sharing an interface bit
        for lst in tpl.lists:
            lst[:] = [n for n in lst if not ((n.mask & mask) and n.copied and not n.inherit)]
    if replacing:
        old_cls, old_tag = replacing
        if old_cls != d.cls:
            errors.append(f"{d.where}: ReplaceModule class {old_cls} vs {d.cls}")
        if old_tag == d.tag:
            errors.append(f"{d.where}: ReplaceModule keeps the tag {d.tag}")
    lst = tpl.lists[mtype]
    if info["isAiModuleData"]:
        lst[:] = [n for n in lst if not n.ai]
    if info["slot7"] and is_child:
        lst[:] = [n for n in lst if not n.slot7]
    # unique tag check across the four lists; a ChildObject replaces by tag
    for idx, other in enumerate(tpl.lists):
        hit = [n for n in other if n.tag == d.tag]
        if not hit:
            continue
        if is_child and (idx != 1 or hit[0].cls == d.cls):
            for l2 in tpl.lists:
                l2[:] = [n for n in l2 if n.tag != d.tag]
            continue
        errors.append(f"{d.where}: tag {d.tag} already used by {hit[0].cls}")
        return
    lst.append(Nugget(d.cls, d.tag, mask, info["isAiModuleData"], info["slot7"], d.mode == "inheritable"))


def build_templates(objdefs, reg, errors):
    templates = {}
    order = []
    for od in objdefs:
        existing = templates.get(od.name)
        if existing is not None:
            tpl = existing  # a repeated Object block parses into the existing template
        else:
            tpl = Template(od.name, od.kind)
            default = templates.get("DefaultThingTemplate")
            if default is not None:
                tpl.copy_from(default)
                tpl.mark_copied()
            templates[od.name] = tpl
            order.append(tpl)
        is_child = od.kind == "ChildObject"
        if od.parent:
            src = templates.get(od.parent)
            if src is None:
                errors.append(f"{od.where}: {od.kind} {od.name} before its original {od.parent}")
                continue
            tpl.copy_from(src)
            tpl.mark_copied()
        replacing = None
        for op in od.ops:
            if isinstance(op, Decl):
                apply_decl(tpl, op, reg, is_child, errors, replacing if op.mode == "replace" else None)
            elif op[0] == "remove":
                found = False
                for lst in tpl.lists:
                    before = len(lst)
                    lst[:] = [n for n in lst if n.tag != op[1]]
                    found |= len(lst) != before
                if not found:
                    errors.append(f"{op[2]}: RemoveModule {op[1]} not found for {od.name}")
            elif op[0] == "replace":
                cls = None
                for lst in tpl.lists:
                    for n in lst:
                        if n.tag == op[1]:
                            cls = n.cls
                    lst[:] = [n for n in lst if n.tag != op[1]]
                if cls is None:
                    errors.append(f"{op[2]}: ReplaceModule {op[1]} not found for {od.name}")
                replacing = (cls, op[1])
    return order


# ------------------------------------------------------------------------------------------
# which files the engine loads for TheThingFactory
# ------------------------------------------------------------------------------------------
def legend_entry(files, name="thefactory"):
    key = "data\\ini\\default\\subsystemlegendexpansion1.ini"
    entry = {"InitFile": [], "InitPath": [], "ExcludePath": [], "IncludePathCinematics": []}
    cur = None
    for fkey, n, raw in expand(files, key, []):
        toks = strip_comment(raw).replace("=", " ").split()
        if not toks:
            continue
        if toks[0] == "LoadSubsystem":
            cur = toks[1] if len(toks) > 1 else None
        elif toks[0].lower() == "end":
            cur = None
        elif cur == "TheThingFactory" and toks[0] in entry and len(toks) > 1:
            entry[toks[0]].append(toks[1])
    return entry


# Object blocks also appear in Data\INI\Crate.ini (23 of them), which the crate system loads, not the thing
# factory; the census counts only the thing factory's files. The oracle parses it after them.
EXTRA_OBJECT_FILES = ["data\\ini\\crate.ini"]


def object_files(files, entry, cinematics):
    """the *.ini files of the InitPath in the engine's order: the files directly in the directory first,
    then the files of the subdirectories, each group sorted case-insensitively by full path"""
    root = normalize(entry["InitPath"][0]) + "\\"
    excludes = [normalize(e) for e in entry["ExcludePath"]]
    cin = [normalize(e) for e in entry["IncludePathCinematics"]]
    all_ini = sorted((k for k in files if k.startswith(root) and k.endswith(".ini")))
    first = [k for k in all_ini if "\\" not in k[len(root):]]
    second = []
    for k in all_ini:
        if "\\" not in k[len(root):]:
            continue
        if any(k.startswith(e) for e in excludes):
            continue
        if not cinematics and any(k.startswith(c) for c in cin):
            continue
        second.append(k)
    return [normalize(i) for i in entry["InitFile"]] + first + second + EXTRA_OBJECT_FILES


def run(rotwk, bfme2, cinematics):
    files = mount(Path(rotwk), Path(bfme2))
    entry = legend_entry(files)
    flist = object_files(files, entry, cinematics)
    reg = Registry(REGISTRY_JSON)
    errors: list[str] = []
    priors = Priors()
    parsed = []
    for fkey in flist:
        if fkey not in files:
            errors.append(f"missing file {fkey}")
            continue
        items = lex(expand(files, fkey, errors))
        parsed.append((fkey, items))
        priors.learn(items)
    objdefs = []
    unresolved = []
    for fkey, items in parsed:
        try:
            depths, openers = parse_structure(items, priors)
        except OracleError as e:
            unresolved.append(f"{fkey}: {e}")
            continue
        objdefs.extend(collect_objects(items, depths, openers, unresolved))
    templates = build_templates(objdefs, reg, errors)
    return templates, objdefs, errors, unresolved


def render_golden(templates):
    """dictionary-compressed table: every (class, tag) pair once, then one line per template of pair ids"""
    ids: dict[tuple, int] = {}
    body = []
    for t in templates:
        row = []
        for pair in t.flat():
            if pair not in ids:
                ids[pair] = len(ids)
            row.append(str(ids[pair]))
        body.append(f"T\t{t.name}\t{t.kind}\t{' '.join(row)}")
    head = ["# OpenBFME object-model oracle golden v1 (tools/object_oracle/object_oracle.py)",
            "# M <id> <module class> <module tag>; T <template> <header kind> <ids of its modules in list order>"]
    head += [f"M\t{i}\t{c}\t{g}" for (c, g), i in ids.items()]
    return "\n".join(head + body) + "\n"


def census_style_histogram(objdefs):
    """What an INDEPENDENT count that ignores the default template and the module clearing rules sees: the
    modules of a template are its parent's (by tag) with its own removals, replacements and declarations
    applied. It exists to cross-check the oracle's parse against the census, which counts that way; it is
    NOT the engine's inheritance (build_templates is)."""
    resolved = {}
    objs = collections.Counter()
    for od in objdefs:
        mods = {}
        if od.parent and od.parent.lower() in resolved:
            mods = dict(resolved[od.parent.lower()])
        # the census applies a block's removals, then its replacements, then its plain declarations,
        # whatever their order in the text
        for op in od.ops:
            if not isinstance(op, Decl) and op[0] == "remove":
                mods.pop(op[1].lower(), None)
        for op in od.ops:
            if not isinstance(op, Decl) and op[0] == "replace":
                mods.pop(op[1].lower(), None)
        for op in od.ops:
            if isinstance(op, Decl) and op.mode == "replace":
                mods[op.tag.lower()] = op.cls
        for op in od.ops:
            if isinstance(op, Decl) and op.mode != "replace":
                mods[op.tag.lower()] = op.cls
        resolved[od.name.lower()] = mods
    for od in objdefs:
        for c in set(resolved[od.name.lower()].values()):
            objs[c] += 1
    return objs


def write_census_histogram(census_json, out):
    """class, objects, declarations as the census counted them (data copied from census.json)"""
    census = json.loads(Path(census_json).read_text(encoding="utf-8"))
    rows = [f"{h['class']}\t{h['objects']}\t{h['declarations']}" for h in sorted(census["objects"]["module_histogram"], key=lambda h: h["class"])]
    out.write_text("\n".join(rows) + "\n", encoding="utf-8", newline="\n")


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--histogram")
    ap.add_argument("--census-json")
    ap.add_argument("--cinematics", action="store_true", default=True)
    ap.add_argument("--no-cinematics", dest="cinematics", action="store_false")
    args = ap.parse_args(argv)
    rotwk, bfme2 = os.environ.get("ROTWK_INSTALL"), os.environ.get("BFME2_INSTALL")
    if not rotwk or not bfme2:
        print("ROTWK_INSTALL and BFME2_INSTALL must be set", file=sys.stderr)
        return 2
    templates, objdefs, errors, unresolved = run(rotwk, bfme2, args.cinematics)
    Path(args.out).write_text(render_golden(templates), encoding="utf-8", newline="\n")
    print(f"{len(templates)} templates, {len(errors)} errors, {len(unresolved)} unresolved files")
    for e in errors[:20]:
        print("  error:", e)
    for u in unresolved[:20]:
        print("  unresolved:", u)
    if args.census_json and args.histogram:
        write_census_histogram(args.census_json, Path(args.histogram).with_name("census_module_histogram.tsv"))
    if args.histogram:
        census_style = census_style_histogram([o for o in objdefs if o.where.split(":")[0].lower() != "data\\ini\\crate.ini"])
        objs = collections.Counter()
        decls = collections.Counter()
        for t in templates:
            for c in {c for c, _ in t.flat()}:
                objs[c] += 1
        for od in objdefs:
            for op in od.ops:
                if isinstance(op, Decl):
                    decls[op.cls] += 1
        rows = [f"{c}\t{objs[c]}\t{decls[c]}\t{census_style[c]}" for c in sorted(set(objs) | set(decls))]
        Path(args.histogram).write_text("\n".join(rows) + "\n", encoding="utf-8", newline="\n")
    return 1 if errors or unresolved else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
