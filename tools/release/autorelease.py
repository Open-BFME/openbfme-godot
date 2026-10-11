#!/usr/bin/env python3
"""Lane AUTOREL-1: the decisions of tools/release/autorelease.sh that are worth testing on their own (docs/RELEASE.md, "Automatic preview
releases").

    python3 tools/release/autorelease.py next-version [--remote origin] [--base v0.3.0] [--git-dir DIR]
        prints v0.3.0-preview.<n>, n = 1 + the highest v0.3.0-preview.<n> tag on the remote (git ls-remote); 1 when there is none.
        Tags outside the launcher's release grammar (make_manifest.TAG: no leading zeros, digits only) are ignored.
    python3 tools/release/autorelease.py changed --from A --to B [--git-dir DIR]
        exit 0 when something under RELEASE_PATHS changed between the two commits, 1 when nothing did (the release is skipped).
    python3 tools/release/autorelease.py notes --from A --to B --version V [--known-issues FILE] [--commit SHA] [--git-dir DIR]
        --out FILE [--title-out FILE] [--items-out FILE]
        the release notes for players from the first-parent subjects of A..B (see release_notes()).

Release notes. Every first-parent commit of A..B is one candidate (on the rebuild's branch a lane lands as one merge commit):
  * a `Release-note: <text>` line in its message is used as written (`Release-note: none` drops the commit; a text starting with
    `Fix:` goes under "What's fixed");
  * an integration round's lane merge ("Merge lane/x (<sha>) into integ/pN: ...") gives no note: the round's release-notes commit
    carries the player lines;
  * otherwise a commit that changes nothing a player runs (PLAYER_PATHS minus tests) is dropped, and the subject is reduced to plain
    words: the lane id, parenthesised details (binary addresses, stop ids, review verdicts) and everything after the first ';' are cut;
    a long first clause keeps only its head before ':'. Subjects that still read as lane jargon after that are dropped.
  * a line that says something got fixed (fix, regression, crash, no longer, stop ..., instead of) goes under "What's fixed".
Known issues come from known_issues.json (the README_TESTERS list, already in a player's words).
Every line (generated, `Release-note:` text, known issues) is neutralised before it reaches GitHub (Sol r1: injection through subjects):
links and URLs are removed, @mentions and #references are written with full-width signs (GitHub links neither), control characters go;
the Markdown notes and PR body also escape every Markdown / HTML special character (neutral(), markdown()).

    python3 tools/release/autorelease.py audit-tree --tree TREE [--git-dir DIR]
        the public sync's privacy audit, before anything is pushed (audit_tree()): only PUBLIC_TOP entries at the top, no workspace /
        archive / reference folder anywhere, no file a .gitignore rule of the tree ignores (tracked or not), and every file passes the
        commit gate's rules (tools/precommit.py check_file: retail suffixes, retail-format bytes, developer-machine paths), also inside
        containers (r3, Sol r2: a gzip-wrapped BIG passed): every file goes through audit_package.Audit (Godot packs, ZIP, gzip, bzip2,
        xz, tar, opened recursively to MAX_DEPTH and MAX_UNPACKED; encrypted, truncated, too deep, too large or unopenable kinds such as
        7z / RAR are findings) plus raw zlib streams (SyncAudit).
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import hashlib
import io
import tarfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from make_manifest import TAG  # noqa: E402  (the launcher's release grammar)
sys.path.insert(0, str(HERE.parent))
from precommit import check_file  # noqa: E402  (the commit gate's per-file rules)
import audit_package  # noqa: E402  (the package audit: containers opened recursively, fail closed)
import zlib  # noqa: E402

# what a release is made of: a change anywhere else (docs, roadmap, other tools) does not make a new release
RELEASE_PATHS = ("engine", "godot", "launcher", "tools/release")
# what a player runs: the notes only describe commits that change these (tests excluded)
PLAYER_PATHS = ("engine/", "godot/", "launcher/")
TEST_PATHS = ("engine/tests/", "godot/tests/", "launcher/tests/")

LANE_ID = re.compile(r"^(?:[A-Z][A-Z0-9]*(?:-[A-Z0-9]+)+|[A-Z][a-z]+\d+)(?: r\d+)?(?: \([^()]*\))?:?\s+")
JARGON = re.compile(r"\bRW\b|0x[0-9A-Fa-f]{3,}|\bS-\d+|\bFB-\d+|\bSol\b|\b[A-Z]{2,}[0-9]*-[0-9A-Z]+\b|#\d|\br\d+\b|\bstack\d+\b|"
                    r"\bpins?\b|\bgate\b|\.(?:cpp|h|py|sh|gd|md)\b|\b[a-z]+[A-Z][A-Za-z]*(?:'s)?\b")
FIXED = re.compile(r"\b(?:fix(?:es|ed)?|regression|crash(?:es|ed)?|no longer|stops?\b|instead of|bugs?|broken|wrong)\b", re.I)
# an integration round's lane merge, "Merge lane/x (<sha>) into integ/p3: ...": its words are the integrator's summary for
# reviewers; the round's player notes are its release-notes commit's `Release-note:` lines (preview.3 printed "Ce7997fd into integ/p3: ...")
INTEG_MERGE = re.compile(r"^Merge \S+ \([0-9a-f]{7,40}\) into \S+", re.I)
SKIP_START = re.compile(r"^(?:batch\b|roadmap\b|docs?\b|merge\b)", re.I)
MAX_ITEM = 140
# the public main's top level (the sync recipe: the archive sha's tree without archive/, plus PRIVACY.md); anything else is refused
PUBLIC_TOP = frozenset({".gitattributes", ".gitignore", ".gitmodules", "CLAUDE.md", "LICENSE", "NOTICE", "README.md", "PRIVACY.md",
                        "build.bat", "docs", "engine", "godot", "launcher", "pytest.ini", "run_tests.bat", "tools"})
PRIVATE_DIRS = frozenset({"workspace", "archive", "reference", "references"})
URL = re.compile(r"(?i)\b[a-z][a-z0-9+.-]*://\S*|\bwww\.\S+|\bmailto:\S+")
MD_SPECIAL = re.compile(r"([\\`*_{}\[\]()!|~+\-.:#])")


def git(git_dir: str | None, *args: str) -> str:
    cmd = ["git"] + (["-C", git_dir] if git_dir else []) + list(args)
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


# ---- version --------------------------------------------------------------------------------------------------------------------------
def next_version(tag_names: list[str], base: str = "v0.3.0") -> str:
    """1 + the highest <base>-preview.<n> among tag_names (ls-remote names, refs/tags/ and ^{} allowed); <base>-preview.1 when none."""
    if not TAG.fullmatch(base) or "-" in base:
        raise SystemExit(f"autorelease: base {base!r} is not v<major>.<minor>.<patch>")
    high = 0
    for name in tag_names:
        name = name.removeprefix("refs/tags/").removesuffix("^{}")
        m = TAG.fullmatch(name)
        if m and m.group(4) is not None and name.startswith(base + "-preview."):
            high = max(high, int(m.group(4)))
    return f"{base}-preview.{high + 1}"


def remote_tags(git_dir: str | None, remote: str, base: str) -> list[str]:
    out = git(git_dir, "ls-remote", "--tags", remote, f"refs/tags/{base}-preview.*")
    return [line.split("\t", 1)[1] for line in out.splitlines() if "\t" in line]


# ---- skip-when-unchanged --------------------------------------------------------------------------------------------------------------
def release_changed(git_dir: str | None, a: str, b: str) -> list[str]:
    """the files under RELEASE_PATHS that differ between commits a and b"""
    return [p for p in git(git_dir, "diff", "--name-only", a, b, "--", *RELEASE_PATHS).splitlines() if p]


# ---- release notes --------------------------------------------------------------------------------------------------------------------
def _strip_parens(s: str) -> str:
    out, depth = [], 0
    for ch in s:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth = max(0, depth - 1)
        elif depth == 0:
            out.append(ch)
    return "".join(out)


def _top_level_split(s: str, sep: str) -> str:
    depth = 0
    for i, ch in enumerate(s):
        depth += ch == "("
        depth -= ch == ")"
        if ch == sep and depth == 0:
            return s[:i]
    return s


def _unwrap(s: str) -> str:
    """'(text)' -> 'text' when the parentheses enclose all of s"""
    s = s.strip()
    while s.startswith("("):
        depth = 0
        for i, ch in enumerate(s):
            depth += ch == "("
            depth -= ch == ")"
            if depth == 0:
                break
        if i != len(s) - 1:
            break
        s = s[1:-1].strip()
    return s


def player_line(subject: str) -> str | None:
    """a commit subject in plain words, or None when nothing a player would understand is left"""
    s = subject.strip()
    if INTEG_MERGE.match(s):
        return None
    if s.startswith("Merge "):
        i = s.find("(")
        if i < 0:
            return None
        s = s[i:]
        if s.endswith(")") and s.count("(") == s.count(")"):
            s = _unwrap(s) if _unwrap(s) != s else s[1:-1]
        else:
            s = s[1:]
    for _ in range(3):
        s = _unwrap(LANE_ID.sub("", _unwrap(s), count=1))
    if SKIP_START.match(s):
        return None
    s = _top_level_split(s, ";")
    s = re.sub(r"\s+([,.:])", r"\1", re.sub(r"\s+", " ", _strip_parens(s))).strip(" ,.:")
    head, colon, tail = s.partition(": ")
    if colon and JARGON.search(head) and tail:
        s, head, colon, tail = tail, *tail.partition(": ")
    if len(s) > MAX_ITEM and colon:
        s = head
    s = s.strip(" ,.:")
    if not s or JARGON.search(s) or len(s) > MAX_ITEM or len(s.split()) < 3:
        return None
    return s[0].upper() + s[1:] + "."


def neutral(text: str) -> str:
    """text that GitHub turns into no link, mention or reference: URLs removed, @ and # full-width, control characters and line breaks
    gone (Sol r1: a subject or Release-note line could ping users, link issues or carry links into the release page)"""
    text = URL.sub("(link removed)", text)
    text = "".join(ch if ch.isprintable() else " " for ch in text)
    text = text.replace("@", "\uff20").replace("#", "\uff03")
    text = re.sub(r"\b(GH)-(?=\d)", "\\1\u2011", text, flags=re.I)        # GH-123 is an issue reference too
    return re.sub(r"\s+", " ", text).strip()


def markdown(text: str) -> str:
    """a neutral() line as literal Markdown text: HTML entities for & < >, a backslash before every other special character"""
    text = text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    return MD_SPECIAL.sub(r"\\\1", text)


def notes_of(subject: str, body: str, files: list[str]) -> list[tuple[str, str]]:
    """Every (section, line) of one first-parent commit. A commit with several `Release-note:` lines gives one note per line (a
    release-notes commit lists a whole preview's news this way); `Release-note: none` anywhere drops the commit; without any
    Release-note line it is note_of's single generated line, if any."""
    texts = [m.group(1).strip() for m in (re.match(r"^Release-note:\s*(.*)$", ln.strip(), re.I) for ln in body.splitlines()) if m]
    if not texts:
        n = note_of(subject, body, files)
        return [n] if n else []
    if any(not t or t.lower() == "none" for t in texts):
        return []
    return [("fixed", t[4:].strip()) if t.lower().startswith("fix:") else (("fixed" if FIXED.search(t) else "new"), t) for t in texts]


def note_of(subject: str, body: str, files: list[str]) -> tuple[str, str] | None:
    """(section, line) of one first-parent commit: section 'new' or 'fixed'; None when it is not news for a player"""
    for line in body.splitlines():
        m = re.match(r"^Release-note:\s*(.*)$", line.strip(), re.I)
        if m:
            text = m.group(1).strip()
            if not text or text.lower() == "none":
                return None
            if text.lower().startswith("fix:"):
                return "fixed", text[4:].strip()
            return ("fixed" if FIXED.search(text) else "new"), text
    if not any(f.startswith(PLAYER_PATHS) and not f.startswith(TEST_PATHS) for f in files):
        return None
    line = player_line(subject)
    if line is None:
        return None
    return ("fixed" if FIXED.search(line) else "new"), line


def first_parent_commits(git_dir: str | None, a: str, b: str) -> list[dict]:
    out = git(git_dir, "log", "--first-parent", "--reverse", "--format=%H%x1f%s%x1f%b%x1e", f"{a}..{b}")
    commits = []
    for rec in out.split("\x1e"):
        rec = rec.strip("\n")
        if not rec:
            continue
        sha, subject, body = rec.split("\x1f", 2)
        files = git(git_dir, "diff", "--name-only", f"{sha}^1", sha).splitlines()
        commits.append({"sha": sha, "subject": subject, "body": body, "files": files})
    return commits


def release_notes(version: str, commits: list[dict], known_issues: list[str], commit: str = "", date: str = "") -> tuple[str, list[str]]:
    """(markdown notes, the neutral() lines in order)"""
    new, fixed, seen = [], [], set()
    for c in commits:
        for n in notes_of(c["subject"], c.get("body", ""), c.get("files", [])):
            line = neutral(n[1])
            if line and line not in seen:
                seen.add(line)
                (fixed if n[0] == "fixed" else new).append(line)
    channel = "preview" if TAG.fullmatch(version) and TAG.fullmatch(version).group(4) else "stable"
    out = [f"OpenBFME {version}" + (" is a preview build for testers." if channel == "preview" else "."),
           "Install or update it with the OpenBFME Launcher. You need your own installs of The Rise of the Witch-king 2.01 and "
           "The Battle for Middle-earth II 1.06: the game loads its files from them.", ""]
    if new:
        out += ["## What's new", ""] + [f"- {markdown(x)}" for x in new] + [""]
    if fixed:
        out += ["## What's fixed", ""] + [f"- {markdown(x)}" for x in fixed] + [""]
    if not new and not fixed:
        out += ["No changes you will notice in the game: this build updates the release tooling and internal parts.", ""]
    if known_issues:
        out += ["## Known issues", ""] + [f"- {markdown(neutral(x))}" for x in known_issues] + [""]
    if commit:
        out += [f"Built from commit {markdown(neutral(commit[:10]))}" + (f" ({markdown(neutral(date))})." if date else ".")]
    return "\n".join(out).rstrip() + "\n", new + fixed


def title_summary(lines: list[str], limit: int = 90) -> str:
    """a short plain-language summary of the first lines (each one's first clause, up to 8 words), for the sync PR's title"""
    parts = []
    for line in lines:
        short = re.split(r"[:,;]", line.rstrip("."), maxsplit=1)[0].strip()
        if len(short.split()) > 8:
            continue
        if short[:1].isupper() and not short[1:2].isupper():
            short = short[0].lower() + short[1:]
        if len(", ".join(parts + [short])) > limit:
            break
        parts.append(short)
    return ", ".join(parts) if parts else "internal changes"


# ---- the public sync's privacy audit --------------------------------------------------------------------------------------------------
class SyncAudit(audit_package.Audit):
    """audit_package's recursive audit plus raw zlib streams (no gzip header), which the package audit does not open. Every decoded
    layer goes back through this blob() (r4, Sol r3: a twice-zlib-wrapped BIG passed because the decoded layer skipped the zlib check),
    and so do the gzip / bzip2 / xz / zip / tar / pack members the package audit opens (it recurses through self.blob). Bounds:
    MAX_DEPTH layers, MAX_UNPACKED bytes per layer; anything beyond a bound is a finding. A binary file with a valid zlib header must
    decode to its end, or it is a finding (fail closed); text with a zlib-looking start ("x^...") is decoded when it can be, and is
    text otherwise."""

    def blob(self, name: str, data: bytes, depth: int = 0) -> None:
        if len(data) >= 2 and data[0] & 0x0F == 8 and data[0] >> 4 <= 7 and (data[0] << 8 | data[1]) % 31 == 0:
            text = _is_text(data)
            if depth >= audit_package.MAX_DEPTH:
                self.problems.append(f"containers nested deeper than {audit_package.MAX_DEPTH}: {name}")
                return
            d = zlib.decompressobj()
            problem = ""
            try:
                out = d.decompress(data, audit_package.MAX_UNPACKED + 1)
                if len(out) > audit_package.MAX_UNPACKED or d.unconsumed_tail:
                    problem = f"{name}: zlib data expands beyond {audit_package.MAX_UNPACKED} bytes"
                elif not d.eof or d.unused_data:
                    problem = f"{name}: truncated zlib stream or bytes after it"
            except zlib.error as e:
                problem = f"{name}: unreadable zlib data ({e})"
            if not problem:
                self.files += 1
                self.blob(f"{name}[zlib]", out, depth + 1)
                return
            if not text:
                self.problems.append(problem)
                return
        super().blob(name, data, depth)


def _is_text(data: bytes) -> bool:
    try:
        data.decode("utf-8")
    except UnicodeDecodeError:
        return False
    return b"\x00" not in data


ALLOWLIST = HERE / "public_binaries.txt"
CONTAINER_SUFFIXES = (".zip", ".jar", ".apk", ".whl", ".egg", ".nupkg", ".gz", ".tgz", ".tar", ".bz2", ".tbz2", ".xz", ".txz", ".lzma",
                      ".7z", ".rar", ".cab", ".zst", ".lz4", ".lzo", ".z", ".zlib", ".pck", ".big", ".dmg", ".iso", ".cpio")


def load_allowlist(path: Path = ALLOWLIST) -> dict[str, str]:
    """{path: sha256} of the containers the public tree may hold: lines `<sha256>  <path>  <reason>`, # comments; a malformed line,
    a missing reason or a path listed twice is an error (fail closed)"""
    out: dict[str, str] = {}
    if not path.exists():
        raise SystemExit(f"autorelease: the container allowlist {path} is missing")
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        m = re.fullmatch(r"([0-9a-f]{64})  (\S+)  (\S.*)", line)
        if not m or m.group(2) in out:
            raise SystemExit(f"autorelease: {path.name}:{n} is not `<sha256>  <path>  <reason>` (or lists a path twice)")
        out[m.group(2)] = m.group(1)
    return out


def container_kind(path: str, data: bytes) -> str:
    """'' or what makes the file a container or a compressed file, by content as well as by name"""
    if path.lower().endswith(CONTAINER_SUFFIXES):
        return f"suffix {Path(path).suffix.lower()}"
    if data[:4] in (b"PK\x03\x04", b"PK\x05\x06", b"PK\x07\x08") or zipfile.is_zipfile(io.BytesIO(data)):
        return "zip"
    kind = audit_package.stream_kind(data)
    if kind:
        return kind
    if audit_package.is_tar(data):
        return "tar"
    if data[:4] == audit_package.PCK_MAGIC or audit_package.embedded_pck_base(data) is not None:
        return "Godot pack"
    for magic, what in audit_package.UNOPENABLE.items():
        if data.startswith(magic):
            return what
    if data[:4] in (b"BIGF", b"BIG4"):
        return "BIG archive"
    if data[:3] == b"\x5d\x00\x00":
        return "lzma"
    if len(data) >= 2 and data[0] & 0x0F == 8 and data[0] >> 4 <= 7 and (data[0] << 8 | data[1]) % 31 == 0:
        try:
            zlib.decompress(data)
            return "zlib"
        except zlib.error:
            if not _is_text(data):
                return "zlib (undecodable)"
    return ""


def container_metadata_problems(name: str, data: bytes, depth: int = 0) -> list[str]:
    """a listed container's metadata, all layers: ZIP without archive / member comments or extra fields, gzip without a file name,
    comment or extra field, tar with regular files and folders only (no links, devices, PAX / GNU headers, owner names); every member
    name passes the path rules; only zip, tar, gzip, bzip2, xz and zlib layers are allowed (anything else is refused even when listed)"""
    if depth > audit_package.MAX_DEPTH:
        return [f"{name}: containers nested deeper than {audit_package.MAX_DEPTH}"]
    probs: list[str] = []
    kind = container_kind(name if depth == 0 else "", data)
    if not kind:
        return probs
    try:
        if kind == "zip" or (depth == 0 and kind.startswith("suffix") and zipfile.is_zipfile(io.BytesIO(data))):
            with zipfile.ZipFile(io.BytesIO(data)) as z:
                if z.comment:
                    probs.append(f"{name}: a ZIP archive comment")
                for i in z.infolist():
                    if i.comment:
                        probs.append(f"{name}:{i.filename}: a ZIP member comment")
                    if i.extra:
                        probs.append(f"{name}:{i.filename}: a ZIP extra field")
                    if i.flag_bits & 1:
                        probs.append(f"{name}:{i.filename}: an encrypted ZIP member")
                        continue
                    p = path_problem(i.filename)
                    if p:
                        probs.append(f"{name}: {p}")
                    if not i.is_dir():
                        probs += container_metadata_problems(f"{name}:{i.filename}", z.read(i), depth + 1)
        elif kind == "gzip":
            if data[3] & 0x1C:
                probs.append(f"{name}: a gzip header with a file name, comment or extra field")
            probs += container_metadata_problems(f"{name}[gzip]", b"".join(audit_package.decompress_members("gzip", data)), depth + 1)
        elif kind in ("bzip2", "xz"):
            probs += container_metadata_problems(f"{name}[{kind}]", b"".join(audit_package.decompress_members(kind, data)), depth + 1)
        elif kind == "zlib":
            probs += container_metadata_problems(f"{name}[zlib]", zlib.decompress(data), depth + 1)
        elif kind == "tar" or (depth == 0 and kind.startswith("suffix") and audit_package.is_tar(data)):
            with tarfile.open(fileobj=io.BytesIO(data), mode="r:") as t:
                for m in t.getmembers():
                    if not (m.isfile() or m.isdir()) or m.linkname:
                        probs.append(f"{name}:{m.name}: a tar member that is not a file or a folder (link, device ...)")
                    if m.pax_headers:
                        probs.append(f"{name}:{m.name}: tar PAX headers")
                    if m.uname or m.gname:
                        probs.append(f"{name}:{m.name}: tar owner names")
                    p = path_problem(m.name)
                    if p:
                        probs.append(f"{name}: {p}")
                    if m.isfile():
                        probs += container_metadata_problems(f"{name}:{m.name}", t.extractfile(m).read(), depth + 1)
        else:
            probs.append(f"{name}: a {kind} file is refused even when listed (only zip, tar, gzip, bzip2, xz and zlib can be audited)")
    except (zipfile.BadZipFile, tarfile.TarError, ValueError, OSError, EOFError, zlib.error) as e:
        probs.append(f"{name}: a {kind} that cannot be decoded completely ({e})")
    return probs


def _user_names() -> set[str]:
    names = {Path.home().name}
    try:
        import getpass  # noqa: PLC0415
        names.add(getpass.getuser())
    except (KeyError, OSError):
        pass
    return {n.lower() for n in names if len(n) >= 2}


USER_NAMES = _user_names()


def path_problem(path: str) -> str:
    """a tracked path (or a member name) with a private path or a user name in it: '' when there is none"""
    if audit_package.MACHINE_PATH.search(path) or re.search(r"(?i)(^|[/\\])(home|users)[/\\]", path):
        return f"a private path in a file name: {path}"
    # a whole path component equal to a user name (a folder or file named after the account); not a file's stem: the CI user "claude"
    # would otherwise refuse the repository's CLAUDE.md on that host
    for c in re.split(r"[/\\]", path):
        if c.lower() in USER_NAMES:
            return f"a user name in a file name: {path}"
    return ""


# ---- the public metadata's privacy audit (r6) ------------------------------------------------------------------------------------------
# The sync commit message, the PR title and body, the release title and notes and the tag message go to the public repository: they pass
# these rules or the run stops before anything is pushed (Sol r5: a Release-note with a private path and the key path reached the public
# commit message, the PR and the release notes). Nothing is redacted here: a hit is a failure.
NOREPLY = re.compile(r"(?i)^(?:[0-9]+\+[a-z0-9-]+@users\.noreply\.github\.com|noreply@anthropic\.com)$")
EMAIL = re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}")
# our own fixed attribution lines (their words are not user or host names)
OWN_LINES = ("Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>", "🤖 Generated with [Claude Code](https://claude.com/claude-code)")
KEY_PATH = Path.home() / ".config" / "openbfme-release" / "release-ed25519.pem"


def _ssh_config_values(field: str) -> set[str]:
    out: set[str] = set()
    cfg = Path.home() / ".ssh" / "config"
    try:
        lines = cfg.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return out
    for line in lines:
        parts = line.split()
        if len(parts) >= 2 and parts[0].lower() == field.lower():
            out.update(v.lower() for v in parts[1:] if not any(c in v for c in "*?!"))
    return out


_PRIVATE_WORDS: tuple[set[str], set[str]] | None = None


def private_words() -> tuple[set[str], set[str]]:
    """(user names, host names) of this machine and the hosts it reaches: the local account, the ssh config's users and hosts (no DNS
    lookup: gethostname only; computed once)"""
    global _PRIVATE_WORDS
    if _PRIVATE_WORDS is None:
        import socket  # noqa: PLC0415
        users = set(USER_NAMES) | _ssh_config_values("User")
        hosts = {socket.gethostname().lower(), socket.gethostname().lower().split(".")[0]} | _ssh_config_values("Host")
        _PRIVATE_WORDS = ({u for u in users if len(u) >= 2}, {h for h in hosts if len(h) >= 2 and h != "localhost"})
    return _PRIVATE_WORDS


def public_text_problems(name: str, text: str) -> list[str]:
    """the privacy problems of text that goes to the public repository: home / Users paths, the release key's path (or its folder),
    user names in path or user@host form, host names, e-mail addresses other than the noreply ones. Checked as written, without Markdown escapes and NFKC-normalised
    (full-width @ / # back to ASCII), so an escaped or neutralised form is caught too."""
    import unicodedata  # noqa: PLC0415
    for own in OWN_LINES:
        text = text.replace(own, "")
    forms = {text, re.sub(r"\\(.)", r"\1", text)}
    forms |= {unicodedata.normalize("NFKC", f) for f in list(forms)}
    users, hosts = private_words()
    key = str(KEY_PATH)
    secrets = {key, str(KEY_PATH.parent), "~/.config/openbfme-release", KEY_PATH.name, str(Path.home())}
    problems: set[str] = set()
    for f in forms:
        if audit_package.MACHINE_PATH.search(f) or re.search(r"(?i)(^|[^A-Za-z0-9])(home|users)[/\\][A-Za-z0-9._-]", f):
            problems.add("a home or Users path")
        for sct in secrets:
            if len(sct) > 1 and sct in f:
                problems.add("the release key's path or folder (or the home folder)")
        low = f.lower()
        for u in users:
            # r7 (Sol r6): a user name counts only where it names an account: a path component (/deck, \\deck, ~deck) or user@host;
            # an ordinary word ("Steam Deck controls now work") is not one
            e = re.escape(u)
            if re.search(rf"(?:[/\\\\~]{e}(?![a-z0-9_-]))|(?:(?<![a-z0-9_.+-]){e}@[a-z0-9])", low):
                problems.add(f"a user name ({u})")
        for h in hosts:
            if re.search(rf"(?<![a-z0-9_.-]){re.escape(h)}(?![a-z0-9_-])", low):
                problems.add(f"a host name ({h})")
        for e in EMAIL.findall(f):
            if not NOREPLY.match(e):
                problems.add("an e-mail address that is not the noreply one")
    return [f"{name}: {p}" for p in sorted(problems)]


def audit_tree(git_dir: str | None, tree: str) -> list[str]:
    """the problems of a tree about to be pushed to the public repository (empty: it may go). Run before the first push (Sol r1: a tracked
    workspace/ file or a retail file outside archive/ went to the public branch)."""
    problems = []
    allowed = load_allowlist()
    for line in git(git_dir, "ls-tree", "-z", tree).split("\0"):
        if line and line.split("\t", 1)[1] not in PUBLIC_TOP:
            problems.append(f"not a public top-level entry: {line.split(chr(9), 1)[1]}")
    entries = []  # (mode, type, oid, path)
    for line in git(git_dir, "ls-tree", "-r", "-z", "--full-tree", tree).split("\0"):
        if line:
            meta, path = line.split("\t", 1)
            mode, kind, oid = meta.split()
            entries.append((mode, kind, oid, path))
    for _, _, _, path in entries:
        hit = [c for c in path.split("/")[:-1] if c.lower() in PRIVATE_DIRS]
        if hit:
            problems.append(f"inside a private folder ({hit[0]}/): {path}")
    # .gitignore rules of the tree itself, applied with --no-index: a tracked file that a rule ignores is still refused
    with tempfile.TemporaryDirectory() as tmp:
        env = {**os.environ, "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1"}
        subprocess.run(["git", "init", "-q", tmp], check=True, env=env)
        for mode, kind, oid, path in entries:
            target = Path(tmp, path)
            target.parent.mkdir(parents=True, exist_ok=True)
            if kind == "blob" and path.split("/")[-1] == ".gitignore":
                target.write_bytes(subprocess.run(["git"] + (["-C", git_dir] if git_dir else []) + ["cat-file", "blob", oid],
                                                  capture_output=True, check=True).stdout)
            elif kind != "tree":
                target.touch()
        r = subprocess.run(["git", "-C", tmp, "-c", f"core.excludesFile={os.devnull}", "check-ignore", "--no-index", "-z", "--stdin"],
                           input="\0".join(p for _, _, _, p in entries).encode(), capture_output=True, env=env)
        if r.returncode not in (0, 1):
            raise SystemExit(f"autorelease: git check-ignore failed: {r.stderr.decode(errors='replace')}")
        problems += [f"a .gitignore rule ignores it (tracked anyway): {p}" for p in r.stdout.decode().split("\0") if p]
    # the commit gate's rules on every file (names and bytes)
    blobs = [(oid, path) for mode, kind, oid, path in entries if kind == "blob"]
    cat = subprocess.run(["git"] + (["-C", git_dir] if git_dir else []) + ["cat-file", "--batch"],
                         input="".join(f"{oid}\n" for oid, _ in blobs).encode(), capture_output=True, check=True).stdout
    pos = 0
    for oid, path in blobs:
        header_end = cat.index(b"\n", pos)
        size = int(cat[pos:header_end].split()[2])
        data = cat[header_end + 1:header_end + 1 + size]
        pos = header_end + 1 + size + 1
        problem = check_file(path, verb="in the public tree") or check_file(path, data, verb="in the public tree")
        if problem:
            problems.append(problem)
        if problem:
            continue
        binary = not _is_text(data)
        if binary and path not in allowed:
            # r6: the public tree is text only (Sol r5: a prefixed gzip, a shell stub with gzip, PNG + gzip passed the detectors)
            problems.append(f"a binary file in the public tree (not UTF-8 text, or NUL bytes), not in {ALLOWLIST.name}: {path}")
            continue
        if binary and hashlib.sha256(data).hexdigest() != allowed[path]:
            problems.append(f"{path} is listed in {ALLOWLIST.name} with another sha256")
            continue
        kind = container_kind(path, data)
        if kind:
            # r5: no containers in the public tree, except listed ones (tools/release/public_containers.txt: path, sha256, reason), each
            # fully decoded and audited, its metadata included (Sol r4: comments, extra fields, names and tar metadata leaked)
            listed = allowed.get(path)
            if listed is None:
                problems.append(f"a container or compressed file ({kind}) in the public tree, not in {ALLOWLIST.name}: {path}")
                continue
            if hashlib.sha256(data).hexdigest() != listed:
                problems.append(f"{path} is listed in {ALLOWLIST.name} with another sha256")
                continue
            problems += container_metadata_problems(path, data)
        inner = SyncAudit()
        inner.blob(path, data)
        problems += [f"inside a container: {p}" for p in inner.problems]
    for _, _, _, path in entries:
        problem = path_problem(path)
        if problem:
            problems.append(problem)
    for mode, kind, oid, path in entries:
        if kind == "commit":
            problem = check_file(path, verb="in the public tree")
            if problem:
                problems.append(problem)
    return problems


def load_known_issues(path: Path) -> list[str]:
    return [i["text"] for i in json.loads(path.read_text(encoding="utf-8"))["issues"]]


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--git-dir")
    sub = ap.add_subparsers(dest="cmd", required=True)
    v = sub.add_parser("next-version")
    v.add_argument("--remote", default="origin")
    v.add_argument("--base", default="v0.3.0")
    c = sub.add_parser("changed")
    c.add_argument("--from", dest="a", required=True)
    c.add_argument("--to", dest="b", required=True)
    n = sub.add_parser("notes")
    n.add_argument("--from", dest="a", required=True)
    n.add_argument("--to", dest="b", required=True)
    n.add_argument("--version", required=True)
    n.add_argument("--known-issues", type=Path, default=HERE / "known_issues.json")
    n.add_argument("--commit", default="")
    n.add_argument("--date", default="")
    n.add_argument("--out", type=Path, required=True)
    n.add_argument("--title-out", type=Path)
    n.add_argument("--items-out", type=Path, help="the items as Markdown (PR body)")
    n.add_argument("--plain-items-out", type=Path, help="the items as neutral plain text (the sync commit's message)")
    t = sub.add_parser("audit-tree")
    t.add_argument("--tree", required=True)
    x = sub.add_parser("audit-text", help="the public metadata files: exit 1 on any privacy hit (no redaction)")
    x.add_argument("files", nargs="+", type=Path)
    a = ap.parse_args(argv)
    if a.cmd == "audit-text":
        problems = [p for f in a.files for p in public_text_problems(f.name, f.read_text(encoding="utf-8", errors="replace"))]
        for p in problems:
            print(f"METADATA AUDIT: {p}")
        print(f"METADATA AUDIT {'FAILED' if problems else 'ok'}: {len(a.files)} files")
        return 1 if problems else 0
    if a.cmd == "audit-tree":
        problems = audit_tree(a.git_dir, a.tree)
        for p in problems:
            print(f"SYNC AUDIT: {p}")
        print(f"SYNC AUDIT {'FAILED' if problems else 'ok'}: {a.tree}")
        return 1 if problems else 0
    if a.cmd == "next-version":
        print(next_version(remote_tags(a.git_dir, a.remote, a.base), a.base))
        return 0
    if a.cmd == "changed":
        files = release_changed(a.git_dir, a.a, a.b)
        print(f"{len(files)} release files changed" + (f" (first: {files[0]})" if files else ""))
        return 0 if files else 1
    text, lines = release_notes(a.version, first_parent_commits(a.git_dir, a.a, a.b), load_known_issues(a.known_issues), a.commit, a.date)
    a.out.write_text(text, encoding="utf-8")
    if a.title_out:
        a.title_out.write_text(title_summary(lines) + "\n", encoding="utf-8")
    if a.items_out:
        a.items_out.write_text("".join(f"- {markdown(x)}\n" for x in lines), encoding="utf-8")
    if a.plain_items_out:
        a.plain_items_out.write_text("".join(f"- {x}\n" for x in lines), encoding="utf-8")
    print(f"NOTES {a.out}: {len(lines)} items")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
