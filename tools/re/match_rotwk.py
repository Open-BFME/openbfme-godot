"""Map every RotWK game.dat function to BFME2 1.06 / BFME1 1.03 functions and their source status.

Tiers (RotWK -> target):
  A  identical bytes after masking absolute addresses and external rel32 branch targets
     (referenced string literals must also be equal). Hash collisions (tiny getters that differ
     only by the global they read) are resolved to the candidate nearest the link-order
     prediction from neighbouring anchors.
  B  instruction-shape similarity >= --sim (mnemonic + operand kinds, registers ignored;
     Indel ratio = 2*LCS/(len a + len b)), or equal size with every callee already matched pairwise.
     Candidates come from the gap between neighbouring anchors, call-graph propagation
     (callees at the same call index of matched callers) and unique shape hashes.
  C  anchored by string literals that are referenced by exactly one function in each binary.
  D  diverged: shape similarity in [--diverged, --sim) for a candidate from the anchored gap or
     call-graph propagation only. The same function, modified for RotWK; the BFME2 source is a
     starting point, not a description.
Unmatched functions are RotWK-only candidates.

Inputs are inventories from tools/re/inventory.py, the BFME2 ledger from tools/re/ledger_lanes.py
and an Open-BFME-2 clone (--decomp: reverse/attempts, reverse/tu_map.csv). Nothing here embeds retail bytes; outputs go where you point them.

  python tools/re/match_rotwk.py --config <json>     (keys = the long option names)
"""
import argparse
import array
import bisect
import collections
import csv
import json
import os
import random
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import relib  # noqa: E402


def lcs_len(a, b):
    """Longest common subsequence length of two token sequences (bit-parallel, Allison-Dix)."""
    if len(a) < len(b):
        a, b = b, a
    masks = {}
    for i, tok in enumerate(a):
        masks[tok] = masks.get(tok, 0) | (1 << i)
    row = 0
    for tok in b:
        x = masks.get(tok, 0) | row
        row = x & ((x - ((row << 1) | 1)) ^ x)
    return row.bit_count()


def similarity(a, b):
    """Indel normalized similarity (same as rapidfuzz.distance.Indel.normalized_similarity)."""
    n = len(a) + len(b)
    return 1.0 if n == 0 else 2.0 * lcs_len(a, b) / n


LANE_RANK = {"dump": 1, "library": 2, "generated": 3, "vendored": 4, "authored": 5}
RANK_LANE = {v: k for k, v in LANE_RANK.items()}
CPP_LANES = ("authored", "vendored", "generated")


# --------------------------------------------------------------------------- matching
def lis_pairs(pairs):
    """Longest subsequence of (r, t) pairs (sorted by r) with strictly increasing t."""
    tails, tails_idx, prev = [], [], [None] * len(pairs)
    for i, (_, t) in enumerate(pairs):
        j = bisect.bisect_left(tails, t)
        if j == len(tails):
            tails.append(t)
            tails_idx.append(i)
        else:
            tails[j] = t
            tails_idx[j] = i
        prev[i] = tails_idx[j - 1] if j else None
    out, k = [], tails_idx[-1] if tails_idx else None
    while k is not None:
        out.append(pairs[k])
        k = prev[k]
    return out[::-1]


class Matcher:
    def __init__(self, R, T, sim_threshold=0.9, label="target"):
        self.R, self.T = R, T
        self.label = label
        self.sim = sim_threshold
        self.t_rvas = [f["rva"] for f in T]
        self.t_index = {f["rva"]: i for i, f in enumerate(T)}
        self.r_index = {f["rva"]: i for i, f in enumerate(R)}
        self.match = {}      # r idx -> (t idx, tier, score, note)
        self.t_taken = {}    # t idx -> r idx for 1:1 tiers (and unique A)
        self.anchor_r, self.anchor_t = [], []

    # ---- helpers
    def rebuild_anchors(self):
        pairs = sorted((self.R[r]["rva"], self.T[t]["rva"]) for r, (t, tier, _, note) in self.match.items()
                       if self.R[r]["blk"] >= 16 and not note.startswith("amb"))
        lis = lis_pairs(pairs)
        self.anchor_r = [p[0] for p in lis]
        self.anchor_t = [p[1] for p in lis]

    def predict(self, r_rva):
        i = bisect.bisect_right(self.anchor_r, r_rva) - 1
        if i < 0:
            return self.anchor_t[0] - (self.anchor_r[0] - r_rva) if self.anchor_r else r_rva, None, 0
        pred = self.anchor_t[i] + (r_rva - self.anchor_r[i])
        nxt = self.anchor_t[i + 1] if i + 1 < len(self.anchor_t) else None
        return pred, self.anchor_t[i], nxt

    def accept(self, r, t, tier, score, note=""):
        self.match[r] = (t, tier, score, note)
        if tier != "A" or not note.startswith("amb"):
            self.t_taken.setdefault(t, r)

    # ---- tier A
    def tier_a(self):
        tb = collections.defaultdict(list)
        for i, f in enumerate(self.T):
            tb[f["mhash"]].append(i)
        rb = collections.defaultdict(list)
        for i, f in enumerate(self.R):
            rb[f["mhash"]].append(i)
        ambiguous = []
        for h, rs in rb.items():
            ts = tb.get(h)
            if not ts:
                continue
            if len(rs) == 1 and len(ts) == 1:
                self.accept(rs[0], ts[0], "A", 1.0, "")
            else:
                ambiguous.append((rs, ts))
        self.rebuild_anchors()
        self.ambiguous = ambiguous
        self.resolve_ambiguous()

    def resolve_ambiguous(self, far=0x4000):
        """Identical-bytes groups: nearest-first global assignment to the link-order prediction
        (1:1 while candidates remain); a pick further than `far` from the prediction is noted
        amb-far: the code is identical, which BFME2 instance it is stays uncertain."""
        for rs, ts in self.ambiguous:
            for r in rs:
                if r in self.match and self.match[r][3].startswith("amb"):
                    del self.match[r]
            ts = sorted(ts, key=lambda i: self.T[i]["rva"])
            trv = [self.T[t]["rva"] for t in ts]
            pairs = []
            for r in rs:  # the 16 candidates nearest the prediction
                pred = self.predict(self.R[r]["rva"])[0]
                k = bisect.bisect_left(trv, pred)
                pairs += [(abs(trv[j] - pred), self.R[r]["rva"], r, ts[j])
                          for j in range(max(0, k - 8), min(len(ts), k + 8))]
            pairs.sort()
            used_r, used_t = set(), set()
            for d, _, r, t in pairs:
                if r in used_r or t in used_t:
                    continue
                used_r.add(r)
                used_t.add(t)
                self.accept(r, t, "A", 1.0, f"amb{len(rs)}x{len(ts)}" + ("-far" if d > far else ""))
            for r in rs:  # more RotWK copies than BFME2 ones: share the nearest
                if r not in used_r:
                    pred = self.predict(self.R[r]["rva"])[0]
                    t = min(ts, key=lambda i: abs(self.T[i]["rva"] - pred))
                    d = abs(self.T[t]["rva"] - pred)
                    self.accept(r, t, "A", 1.0, f"amb{len(rs)}x{len(ts)}-shared" + ("-far" if d > far else ""))
        self.rebuild_anchors()

    # ---- tier B
    def _score(self, r, t):
        a, b = self.R[r]["shape"], self.T[t]["shape"]
        la, lb = len(a), len(b)
        if not la or not lb or min(la, lb) / max(la, lb) < 0.8:
            return 0.0
        return similarity(a, b)

    def _callee_pattern(self, r, t):
        fr, ft = self.R[r], self.T[t]
        if fr["blk"] != ft["blk"] or not fr["callees"] or len(fr["callees"]) != len(ft["callees"]):
            return False
        for cr, ct in zip(fr["callees"], ft["callees"]):
            ri, ti = self.r_index.get(cr), self.t_index.get(ct)
            if ri is None or ti is None or ri not in self.match or self.match[ri][0] != ti:
                return False
        return True

    def tier_b_round(self, window=40, nearest=6, tier="B", sim=None, shape_hash=True):
        sim = self.sim if sim is None else sim
        cands = set()
        free_t = lambda t: t not in self.t_taken  # noqa: E731
        unmatched = [i for i in range(len(self.R)) if i not in self.match]
        # gap candidates
        for r in unmatched:
            pred, lo, hi = self.predict(self.R[r]["rva"])
            if lo is not None and hi is not None:
                a = bisect.bisect_right(self.t_rvas, lo)
                b = bisect.bisect_left(self.t_rvas, hi)
                if 0 <= b - a <= window:
                    cands.update((r, t) for t in range(a, b) if free_t(t))
                    continue
            k = bisect.bisect_left(self.t_rvas, pred)
            for t in range(max(0, k - nearest), min(len(self.T), k + nearest)):
                if free_t(t):
                    cands.add((r, t))
        # call-graph propagation
        for r, (t, _, _, _) in list(self.match.items()):
            cr_list, ct_list = self.R[r]["callees"], self.T[t]["callees"]
            if len(cr_list) != len(ct_list):
                continue
            for cr, ct in zip(cr_list, ct_list):
                ri, ti = self.r_index.get(cr), self.t_index.get(ct)
                if ri is not None and ti is not None and ri not in self.match and free_t(ti):
                    cands.add((ri, ti))
        # unique shape hashes
        sb_t = collections.defaultdict(list)
        for i, f in enumerate(self.T):
            if free_t(i):
                sb_t[f["shash"]].append(i)
        sb_r = collections.defaultdict(list)
        for r in unmatched:
            sb_r[self.R[r]["shash"]].append(r)
        for h, rs in sb_r.items() if shape_hash else ():
            ts = sb_t.get(h, [])
            if len(rs) == 1 and len(ts) == 1 and len(self.R[rs[0]]["shape"]) >= 8:
                cands.add((rs[0], ts[0]))
        scored = []
        for r, t in cands:
            s = self._score(r, t)
            if s >= sim:
                scored.append((s, r, t, "sim"))
            elif tier == "B" and self._callee_pattern(r, t):
                scored.append((max(s, 0.0), r, t, "callee-pattern"))
        scored.sort(key=lambda x: (-x[0], x[1]))
        added = 0
        for s, r, t, why in scored:
            if r in self.match or t in self.t_taken:
                continue
            self.accept(r, t, tier, round(s, 3), why)
            added += 1
        self.rebuild_anchors()
        return added

    # ---- tier C
    def tier_c(self):
        rs, ts = collections.defaultdict(set), collections.defaultdict(set)
        for i, f in enumerate(self.R):
            for s in f["strs"]:
                rs[s].add(i)
        for i, f in enumerate(self.T):
            for s in f["strs"]:
                ts[s].add(i)
        proposals = []
        for r, f in enumerate(self.R):
            if r in self.match:
                continue
            votes = collections.Counter()
            for s in set(f["strs"]):
                if len(s) >= 4 and len(rs[s]) == 1 and len(ts.get(s, ())) == 1:
                    votes[next(iter(ts[s]))] += 1
            if votes:
                t, n = votes.most_common(1)[0]
                proposals.append((n, r, t))
        proposals.sort(key=lambda x: -x[0])
        added = 0
        for n, r, t in proposals:
            if r in self.match or t in self.t_taken:
                continue
            self.accept(r, t, "C", round(self._score(r, t), 3), f"strings={n}")
            added += 1
        self.rebuild_anchors()
        return added

    def run(self, log=print, diverged=0.6):
        self.tier_a()
        log(f"[{self.label}] tier A: {len(self.match)}")
        for phase in ("B1", "C", "B2", "D"):
            if phase == "C":
                log(f"[{self.label}] tier C: +{self.tier_c()}")
                continue
            for rnd in range(6):
                if phase == "D":
                    n = self.tier_b_round(tier="D", sim=diverged, shape_hash=False)
                else:
                    n = self.tier_b_round()
                log(f"[{self.label}] tier {phase} round {rnd}: +{n}")
                if n == 0:
                    break
        self.resolve_ambiguous()  # again, with the anchors every tier added
        return self.match


# --------------------------------------------------------------------------- ledgers
class Ledger:
    """Byte-painted provenance lanes over a target binary's .text (higher lane rank wins)."""

    def __init__(self, path, text_start, text_end):
        self.t0, self.t1 = text_start, text_end
        n = text_end - text_start
        self.lane = bytearray(n)
        self.src = array.array("i", [-1]) * n
        self.sources = []   # (source file, row name)
        src_ids = {}
        rows = []
        with open(path, newline="", encoding="utf-8") as fh:
            for row in csv.DictReader(fh):
                rows.append((LANE_RANK[row["lane"]], int(row["target_rva"], 16), int(row["target_size"]),
                             row["source"], row["name"]))
        rows.sort()
        self.starts = {}    # target rva -> source id of the row starting there
        for rank, rva, size, source, name in rows:
            a, b = max(rva, self.t0) - self.t0, min(rva + size, self.t1) - self.t0
            if a >= b:
                continue
            sid = src_ids.setdefault((source, name), len(src_ids))
            if sid == len(self.sources):
                self.sources.append((source, name))
            self.lane[a:b] = bytes([rank]) * (b - a)
            self.src[a:b] = array.array("i", [sid]) * (b - a)
            self.starts[rva] = (sid, rank)

    def status(self, rva, size):
        """-> (status, (source, name) or None, C++ fraction of the extent's bytes).

        Status is the lane of the ledger row starting at rva when there is one, else the lane
        covering >= 95% of the extent, else partial-<dominant lane>, else none."""
        a, b = max(rva, self.t0) - self.t0, min(rva + size, self.t1) - self.t0
        if a >= b:
            return "none", None, 0.0
        lanes = self.lane[a:b]
        counts = [lanes.count(k) for k in range(6)]
        total = b - a
        cpp = sum(counts[LANE_RANK[k]] for k in CPP_LANES) / total
        covered = (total - counts[0]) / total
        head = self.starts.get(rva)
        if head is not None:
            # a ledger row starts at the entry: that row is the function (later rows inside the
            # extent are its EH funclets / split tails, which the ledger keeps separately)
            return RANK_LANE[head[1]], self.sources[head[0]], cpp
        ids = collections.Counter(x for x in self.src[a:b] if x >= 0)
        sid = ids.most_common(1)[0][0] if ids else None
        source = self.sources[sid] if sid is not None else None
        dom = RANK_LANE.get(max(range(1, 6), key=lambda k: counts[k]), "none") if covered else "none"
        if cpp >= 0.95:
            best = max(CPP_LANES, key=lambda k: counts[LANE_RANK[k]])
            return best, source, cpp
        if covered >= 0.95:
            return dom, source, cpp
        if covered > 0:
            return f"partial-{dom}", source, cpp
        return "none", None, 0.0


# --------------------------------------------------------------------------- decomp names
OPS = {"0": "{ctor}", "1": "{dtor}", "2": "operator new", "3": "operator delete", "4": "operator=",
       "8": "operator==", "9": "operator!=", "A": "operator[]", "C": "operator->", "D": "operator*",
       "E": "operator++", "F": "operator--", "G": "operator-", "H": "operator+", "M": "operator<",
       "O": "operator>", "R": "operator()", "Y": "operator+=", "Z": "operator-=",
       "_G": "`scalar deleting destructor'", "_E": "`vector deleting destructor'",
       "_7": "`vftable'", "_U": "operator new[]", "_V": "operator delete[]"}


def qualname(mangled):
    """Readable qualified name from an MSVC decorated name (enough to grep the source for)."""
    m = mangled
    if not m.startswith("?"):
        return m
    body = m[1:]
    op = None
    if body.startswith("?$"):
        body = body  # template function name: fall through to the plain split
    elif body.startswith("?"):
        code = body[1:3] if body[1] == "_" else body[1]
        op = OPS.get(code, "operator?" + code)
        body = body[1 + len(code):]
    head = body.split("@@", 1)[0]
    parts = []
    for p in head.split("@"):
        if not p:
            continue
        if p.startswith("?$"):
            p = p[2:] + "<>"
        parts.append(p)
    if op is None:
        name, scope = (parts[0], parts[1:]) if parts else (m, [])
    else:
        scope = parts
        cls = scope[0] if scope else ""
        name = cls if op == "{ctor}" else ("~" + cls if op == "{dtor}" else op)
    return "::".join(list(reversed(scope)) + [name])


# --------------------------------------------------------------------------- classification
FILE_RE = re.compile(r".*\\Code\\(.+\.(?:cpp|c|h|inl))$", re.I)  # RotWK __FILE__ strings
# Subsystem of a function from the decomp source path of its BFME2 counterpart (first hit wins).
PATH_SUBSYSTEMS = [
    ("library", re.compile(r"^(vendor/|Code/stlport|Code/Libraries/Source/(Compression|Lua|ZLib|jpeg|libpng|"
                           r"Benchmark|ATL|GDIPlus|CRT|msvc))|\.lib$|/crt/|stlport", re.I)),
    ("EH/compiler glue", re.compile(r"^Code/gen_(small|asm)/|^Code/masm_dumps/", re.I)),
    ("War of the Ring / LivingWorld", re.compile(r"LivingWorld|StrategicUi|WarOfTheRing|Campaign", re.I)),
    ("network / online", re.compile(r"GameNetwork|DirtySock|GameSpy|WWDownload|/Online|Lobby", re.I)),
    ("audio", re.compile(r"Audio|Miles|Sound|Speech|Music|Bink|vp6", re.I)),
    ("APT / UI / shell", re.compile(r"/GUI/|/Apt|EA/Apt|InGameUI|ControlBar|Shell|Palantir|/Input/|MessageStream", re.I)),
    ("script engine", re.compile(r"ScriptEngine|/Scripts?/", re.I)),
    ("AI / pathfinding", re.compile(r"/AI/|Pathfind|SkirmishAI|AIUpdate|/AI[A-Z]", re.I)),
    ("horde / locomotion", re.compile(r"Horde|Locomotor|Formation|Physics", re.I)),
    ("combat / weapons", re.compile(r"Weapon|Damage|Armor|Projectile|/Body/|Body\b|Death|Attack|Fire|SpecialPower", re.I)),
    ("castles / structures", re.compile(r"Castle|Wall|Gate|Build|Construct|Foundation|Garrison", re.I)),
    ("objects / modules", re.compile(r"GameLogic/Object|/Object/|Module|Behavior|Upgrade|/Update/|/Contain/|/Create/|/Collide/", re.I)),
    ("GameClient / draw", re.compile(r"W3DDevice|WW3D2|GameClient|Drawable|Particle|FX|Shader|Texture|Terrain|Water|Render|Radar|Display", re.I)),
    ("INI / templates", re.compile(r"/INI|Thing|Template|Parse", re.I)),
    ("map / terrain logic", re.compile(r"GameLogic/Map|TerrainLogic|PartitionManager|/Map/", re.I)),
    ("math / support libs", re.compile(r"WWMath|WWLib|WWDebug|WWSaveLoad|subsystem|/Math", re.I)),
    ("engine core / system", re.compile(r"Common/|GameLogic/System|GameEngine", re.I)),
]

# Same buckets from RotWK-side evidence (Ghidra names, RTTI classes, referenced strings).
EVIDENCE_SUBSYSTEMS = [
    ("library", re.compile(r"_STL::|basic_string|ios_base|locale|incorrect header check|\blua_|stack traceback|"
                           r"R60\d\d|runtime error|Microsoft Visual C\+\+|bad allocation|deflate|inflate", re.I)),
    ("Create-a-Hero", re.compile(r"CreateAHero|Create_?A_?Hero|\bCAH|CustomHero|HeroCreat", re.I)),
    ("War of the Ring / LivingWorld", re.compile(r"LivingWorld|WarOfTheRing|\bWOTR|Campaign|ArmyManager|Region|StrategicUi", re.I)),
    ("network / online", re.compile(r"GameNetwork|GameSpy|Network|Lobby|Transport|\bNAT\b|Peer|Connection|Online|Ladder|"
                                    r"Clan|Buddy|DirtySock|UDP|Firewall|Packet|gpcm|peerchat", re.I)),
    ("audio", re.compile(r"Audio|Miles|Sound|Speech|Music", re.I)),
    ("APT / UI / shell", re.compile(r"\\GUI\\|Apt|Shell|Menu|Window|Gadget|\.wnd|\.apt|InGameUI|ControlBar|Palantir|Tooltip", re.I)),
    ("script engine", re.compile(r"ScriptEngine|ScriptAction|ScriptCondition|\.lua|Lua", re.I)),
    ("AI / pathfinding", re.compile(r"\\AI\\|AIUpdate|Pathfind|AIPlayer|AIGroup|SkirmishAI|AIState", re.I)),
    ("horde / locomotion", re.compile(r"Horde|Locomotor|Formation|Physics", re.I)),
    ("combat / weapons", re.compile(r"Weapon|Damage|Armor|Projectile|Body\b|Death|Attack|SpecialPower", re.I)),
    ("castles / structures", re.compile(r"Castle|Wall|Gate|Construct|Foundation|Garrison", re.I)),
    ("objects / modules", re.compile(r"Module|Behavior|Upgrade|Update\b|Contain|Object", re.I)),
    ("GameClient / draw", re.compile(r"W3D|ww3d|Shader|Texture|Render|D3D|Terrain|Water|Particle|Mesh|Drawable", re.I)),
    ("INI / templates", re.compile(r"\bINI\b|\.ini|FieldParse|Template", re.I)),
]


def path_subsystem(path):
    for name, rx in PATH_SUBSYSTEMS:
        if rx.search(path):
            return name
    return "other"


def evidence_subsystem(evidence):
    for name, rx in EVIDENCE_SUBSYSTEMS:
        if rx.search(evidence):
            return name
    return ""


# --------------------------------------------------------------------------- decomp side
def load_attempts(clone):
    """reverse/attempts/<rva>.cpp: recovered C++ that does not byte-match yet."""
    out = {}
    d = Path(clone) / "reverse" / "attempts"
    if not d.is_dir():
        raise SystemExit(f"no attempts directory: {d}")
    for p in d.glob("0x*.cpp"):
        first = p.read_text(encoding="utf-8", errors="replace").splitlines()[:2]
        name = first[0][3:].strip() if first and first[0].startswith("// ") else ""
        score = ""
        if len(first) > 1:
            m = re.search(r"score=([0-9.]+)", first[1])
            score = m.group(1)[:5] if m else ""
        out[int(p.stem, 16)] = (p.relative_to(clone).as_posix(), name, score)
    return out


TU_CONF = {"approved": 3, "proposed": 2, "displaced": 1}


def load_tu_map(clone):
    """reverse/tu_map.csv code rows: rva -> translation unit the decomp assigns it to."""
    out = {}
    with open(Path(clone) / "reverse" / "tu_map.csv", newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            if not row["kind"].startswith("code"):
                continue
            rva = int(row["rva"], 16)
            rank = TU_CONF.get(row["confidence"], 0)
            if rva not in out or rank > out[rva][1]:
                out[rva] = (row["tu"], rank)
    return {k: v[0] for k, v in out.items()}


# --------------------------------------------------------------------------- main
FIELDS = ["rotwk_rva", "size", "tier", "score", "how", "bfme2_rva", "bfme2_size", "decomp_status",
          "decomp_cpp", "decomp_source", "decomp_name", "decomp_qualname", "tu", "near_source", "subsystem", "rotwk_name",
          "rtti", "rotwk_file"]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", help="JSON file whose keys are the options below (underscored)")
    for opt in ("rotwk", "rotwk-inv", "bfme2", "bfme2-inv", "bfme2-ledger", "decomp", "work", "out-csv",
                "out-json"):
        ap.add_argument("--" + opt)
    ap.add_argument("--added-sections", default="stxt774,stxt371,.mackt,.danetta")
    ap.add_argument("--sim", type=float, default=0.9)
    ap.add_argument("--diverged", type=float, default=0.6, help="tier-D similarity floor")
    ap.add_argument("--sample", type=int, default=30, help="tier-A and tier-B pairs to list for spot checks")
    args = ap.parse_args()
    if args.config:
        for k, v in json.load(open(args.config, encoding="utf-8")).items():
            if getattr(args, k.replace("-", "_"), None) in (None, ""):
                setattr(args, k.replace("-", "_"), v)
    missing = [k for k in ("rotwk", "rotwk_inv", "bfme2", "bfme2_inv", "bfme2_ledger", "decomp", "work",
                           "out_csv", "out_json") if not getattr(args, k)]
    if missing:
        raise SystemExit("missing options: " + ", ".join(missing))
    work = Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    added = tuple(args.added_sections.split(","))

    rimg, b2img = relib.Image(args.rotwk), relib.Image(args.bfme2)
    rinv, b2inv = relib.load_inventory(args.rotwk_inv), relib.load_inventory(args.bfme2_inv)
    print(f"inventories: rotwk {len(rinv)}, bfme2 {len(b2inv)}", flush=True)
    R, _ = relib.cached_features(rimg, rinv, work / "feat_rotwk.pkl", added)
    B2, _ = relib.cached_features(b2img, b2inv, work / "feat_bfme2.pkl")
    R = [f for f in R if rimg.text[0] <= f["rva"] < rimg.text[1]]
    B2 = [f for f in B2 if b2img.text[0] <= f["rva"] < b2img.text[1]]

    m2 = Matcher(R, B2, args.sim, "bfme2").run(log=lambda s: print(s, flush=True), diverged=args.diverged)

    led = Ledger(args.bfme2_ledger, *b2img.text)
    attempts = load_attempts(args.decomp)
    tu_map = load_tu_map(args.decomp)
    tu_starts = sorted(tu_map)
    rtti = relib.rtti_classes(rimg)

    # class -> subsystem learned from the decomp's matched rows (RTTI evidence for unmatched code)
    class_votes = collections.defaultdict(collections.Counter)
    for source, name in led.sources:
        q = qualname(name)
        if "::" in q:
            class_votes[q.rsplit("::", 1)[0].split("::")[-1]][path_subsystem(source)] += 1
    class_sub = {c: v.most_common(1)[0][0] for c, v in class_votes.items()}

    rows = []
    for i, f in enumerate(R):
        rf = next((m.group(1) for m in map(FILE_RE.search, f["strs"]) if m), "")
        rec = {"i": i, "rotwk_rva": f["rva"], "size": f["size"], "rotwk_name": f["name"], "tu": "",
               "rotwk_file": rf.replace("\\", "/"),
               "rtti": "|".join(sorted(rtti.get(f["rva"], ())))[:120]}
        if i in m2:
            t, tier, score, note = m2[i]
            tf = B2[t]
            k = bisect.bisect_left(tu_starts, tf["rva"])
            rec["tu"] = tu_map[tu_starts[k]] if k < len(tu_starts) and tu_starts[k] < tf["rva"] + tf["size"] else ""
            st, src, cpp = led.status(tf["rva"], tf["size"])
            source, dname = src if src else ("", "")
            if st == "none" or st.startswith("partial"):
                att = attempts.get(tf["rva"])
                if att and st == "none":
                    st, source, dname = "attempt", att[0], att[1]
                elif not source and rec["tu"]:
                    st = "tu-only" if st == "none" else st
                    source = rec["tu"]
            if tier == "B":
                note = (note + ",same-shape") if f["shash"] == tf["shash"] else (note + ",edited")
            rec.update(tier=tier, score=score, how=note, bfme2_rva=tf["rva"], bfme2_size=tf["size"],
                       decomp_status=st, decomp_cpp=round(cpp, 2), decomp_source=source, decomp_name=dname,
                       decomp_qualname=qualname(dname) if dname else "")
        else:
            rec.update(tier="-", score=0.0, how="", bfme2_rva=None, bfme2_size="", decomp_status="",
                       decomp_cpp="", decomp_source="", decomp_name="", decomp_qualname="")
        rows.append(rec)

    # subsystems: the counterpart's decomp path, else RTTI class known to the decomp, else RotWK-side
    # evidence, else the decomp paths of the nearest matched neighbours when both agree
    # link order keeps a translation unit contiguous: the decomp rows on both sides of an
    # undecompiled BFME2 function name its probable source file when they agree
    led_starts = sorted(led.starts)

    def near_source(b2rva, b2size):
        k = bisect.bisect_left(led_starts, b2rva)
        lo = led.sources[led.starts[led_starts[k - 1]][0]][0] if k > 0 else ""
        j = bisect.bisect_left(led_starts, b2rva + b2size)
        hi = led.sources[led.starts[led_starts[j]][0]][0] if j < len(led_starts) else ""
        return lo if lo and lo == hi and not lo.startswith("Code/gen_") else ""

    for rec in rows:
        rec["near_source"] = ""
        if rec["bfme2_rva"] is not None and not rec["decomp_source"]:
            rec["near_source"] = near_source(rec["bfme2_rva"], rec["bfme2_size"])

    def own_subsystem(rec):
        if rec.get("tu"):
            return path_subsystem(rec["tu"])
        if rec["decomp_source"] and rec["decomp_status"] != "attempt":
            return path_subsystem(rec["decomp_source"])
        if rec["decomp_status"] == "attempt" and "::" in rec["decomp_qualname"]:
            c = rec["decomp_qualname"].rsplit("::", 1)[0].split("::")[-1]
            if c in class_sub:
                return class_sub[c]
        f = R[rec["i"]]
        files = [m.group(1).replace("\\", "/") for m in map(FILE_RE.search, f["strs"]) if m]
        if files:
            return path_subsystem("Code/" + files[0])
        if rec["near_source"]:
            return path_subsystem(rec["near_source"])
        for cls in rec["rtti"].split("|"):
            c = cls.split("::")[-1]
            if c in class_sub:
                return class_sub[c]
        return evidence_subsystem("\n".join([rec["rotwk_name"], rec["rtti"]] + f["strs"]))

    for rec in rows:
        rec["subsystem"] = own_subsystem(rec)
    known = [k for k, r in enumerate(rows) if r["subsystem"]]
    for k, rec in enumerate(rows):
        if rec["subsystem"]:
            continue
        j = bisect.bisect_left(known, k)
        lo = rows[known[j - 1]]["subsystem"] if j > 0 else ""
        hi = rows[known[j]]["subsystem"] if j < len(known) else ""
        rec["subsystem"] = (lo + "?") if lo and lo == hi else "unclassified"

    def hx(v):
        return "" if v is None else "0x%X" % v

    with open(args.out_csv, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=FIELDS, extrasaction="ignore")
        w.writeheader()
        for rec in rows:
            out = dict(rec)
            out["rotwk_rva"], out["bfme2_rva"] = hx(rec["rotwk_rva"]), hx(rec["bfme2_rva"])
            w.writerow(out)

    # ------------------------------------------------------------------ summary
    def agg(sel):
        sel = list(sel)
        return {"functions": len(sel), "bytes": sum(r["size"] for r in sel)}

    def pct(part, whole):
        return {k: round(100.0 * part[k] / whole[k], 1) if whole[k] else 0.0 for k in ("functions", "bytes")}

    def block(sel):
        tot = agg(sel)
        b = {"total": tot}
        for name, pred in (
                ("tier_A", lambda r: r["tier"] == "A"),
                ("tier_B", lambda r: r["tier"] == "B"),
                ("tier_C", lambda r: r["tier"] == "C"),
                ("tier_D", lambda r: r["tier"] == "D"),
                ("rotwk_only", lambda r: r["tier"] == "-"),
                ("A_byte_matched_cpp", lambda r: r["tier"] == "A" and r["decomp_status"] in CPP_LANES),
                ("A_authored", lambda r: r["tier"] == "A" and r["decomp_status"] == "authored"),
                ("AB_byte_matched_cpp", lambda r: r["tier"] in ("A", "B") and r["decomp_status"] in CPP_LANES),
                ("any_matched_cpp", lambda r: r["tier"] != "-" and r["decomp_status"] in CPP_LANES),
                ("any_attempt", lambda r: r["tier"] != "-" and r["decomp_status"] == "attempt"),
                ("any_partial", lambda r: r["tier"] != "-" and r["decomp_status"].startswith("partial")),
                ("any_dump_or_lib", lambda r: r["tier"] != "-" and r["decomp_status"] in ("dump", "library")),
                ("any_tu_only", lambda r: r["tier"] != "-" and r["decomp_status"] == "tu-only"),
                ("any_nothing", lambda r: r["tier"] != "-" and r["decomp_status"] == "none")):
            a = agg(r for r in sel if pred(r))
            b[name] = dict(a, pct=pct(a, tot))
        return b

    game = [r for r in rows if r["subsystem"].rstrip("?") not in ("library", "EH/compiler glue")]
    summary = {"inputs": {"rotwk_md5": rimg.md5, "bfme2_md5": b2img.md5, "rotwk_functions": len(R),
                          "bfme2_functions": len(B2), "sim_threshold": args.sim,
                          "ledger_rows": len(led.sources), "attempts": len(attempts)},
               "all": block(rows), "game_code": block(game)}
    subs = sorted({r["subsystem"].rstrip("?") for r in rows})
    summary["by_subsystem"] = {s: block([r for r in rows if r["subsystem"].rstrip("?") == s]) for s in subs}

    # RotWK-only islands: runs of consecutive unmatched functions
    islands, cur = [], []
    for rec in rows:
        if rec["tier"] == "-":
            cur.append(rec)
        elif cur:
            islands.append(cur)
            cur = []
    if cur:
        islands.append(cur)
    islands.sort(key=lambda run: -sum(r["size"] for r in run))
    summary["top_rotwk_only_islands"] = []
    for run in islands[:40]:
        k0, k1 = run[0]["i"], run[-1]["i"]
        prev = rows[k0 - 1] if k0 > 0 else None
        nxt = rows[k1 + 1] if k1 + 1 < len(rows) else None
        strs = collections.Counter()
        for r in run:
            for s in R[r["i"]]["strs"]:
                if len(s) >= 6:
                    strs[s[:60]] += 1
        summary["top_rotwk_only_islands"].append({
            "start": hx(run[0]["rotwk_rva"]), "end": hx(run[-1]["rotwk_rva"] + run[-1]["size"]),
            "functions": len(run), "bytes": sum(r["size"] for r in run),
            "subsystems": collections.Counter(r["subsystem"] for r in run).most_common(3),
            "rtti": collections.Counter(c for r in run for c in r["rtti"].split("|") if c).most_common(4),
            "named": [r["rotwk_name"] for r in run if not r["rotwk_name"].startswith(("FUN_", "thunk_"))][:6],
            "strings": [s for s, _ in strs.most_common(6)],
            "between": [prev["decomp_source"] if prev else "", nxt["decomp_source"] if nxt else ""],
        })
    # RotWK-only bytes by RTTI class (new modules)
    cls = collections.Counter()
    for r in rows:
        if r["tier"] == "-":
            for c in r["rtti"].split("|"):
                if c:
                    cls[c] += r["size"]
    summary["rotwk_only_rtti_classes"] = cls.most_common(60)
    # strings only RotWK references, by the RotWK-only functions that use them (new modules, tactics,
    # GUI hooks); __FILE__ paths and short tokens are skipped
    b2_strs = {salt for f in B2 for salt in map(relib.salt_string, f["strs"])}
    new = collections.Counter()
    for r in rows:
        if r["tier"] == "-":
            for st in set(R[r["i"]]["strs"]):
                if len(st) >= 6 and relib.salt_string(st) not in b2_strs and not FILE_RE.search(st):
                    new[st[:80]] += r["size"]
    summary["rotwk_only_new_strings"] = new.most_common(150)
    # new or changed code per subsystem: RotWK-only + tier D + tier-B pairs whose shape was edited
    summary["new_or_changed_by_subsystem"] = {
        s: {"rotwk_only": agg(r for r in rows if r["subsystem"].rstrip("?") == s and r["tier"] == "-"),
            "changed": agg(r for r in rows if r["subsystem"].rstrip("?") == s and (
                r["tier"] == "D" or (r["tier"] == "B" and r["how"].endswith("edited"))))}
        for s in subs}

    rnd = random.Random(20261009)
    for tier in "AB":
        pool = [r for r in rows if r["tier"] == tier and r["size"] >= 24]
        summary[f"tier_{tier}_sample"] = [
            {"rotwk": hx(r["rotwk_rva"]), "bfme2": hx(r["bfme2_rva"]), "size": r["size"], "score": r["score"],
             "how": r["how"], "decomp": r["decomp_source"], "name": r["decomp_qualname"]}
            for r in rnd.sample(pool, min(args.sample, len(pool)))]
    with open(args.out_json, "w", encoding="utf-8") as fh:
        json.dump(summary, fh, indent=1)
    print(json.dumps({k: summary[k] for k in ("inputs", "all", "game_code")}, indent=1))


if __name__ == "__main__":
    main()
