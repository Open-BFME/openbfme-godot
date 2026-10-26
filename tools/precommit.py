#!/usr/bin/env python3
"""Commit gate for the rebuild:

  1. no retail-format bytes staged (the game's files never enter git), judged by suffix AND by content: a RIFF/WAVE, ID3 or
     MPEG Layer III stream, a BIG archive or a DDS texture is refused under any file name (renaming does not bypass the gate;
     synthetic streams count too: generate them at test time instead)
  2. no developer-machine paths in staged text
  3. no debug symbols (lane WINCRASH-1, stop S-1923): a .pdb file or an MSF (PDB) file's magic anywhere in a staged file is refused; the
     Windows build's PDB is private to the builder's symbol store (tools/release/archive_symbols.sh)
  4. no containers (lane WINCRASH-1 r5, the AUTOREL-1 r5 rule): a staged file whose suffix or leading bytes make it a container or a
     compressed stream (zip, jar, gz, tgz, tar, bz2, xz, lzma, 7z, rar, cab, zstd, lz4, zlib, BIG, a Godot pck) is refused. The gate
     never unpacks: what is inside cannot be judged, so it cannot enter (Sol r4: a ZIP holding a .pdb, a ZIP holding a renamed PDB and a
     gzipped PDB passed the byte checks). The one exception is a path listed with its sha256 in tools/precommit_containers.txt (empty).
  5. no unjustified tolerance assertion in the lines a commit ADDS to a C++ test (engine/tests/) or a GDScript test (godot/tests/) (lane
     LINT-1, the testing standard's "no thresholds or at least N"): doctest CHECK_GE / CHECK_LE / CHECK_GT / CHECK_LT (and REQUIRE_ / WARN_),
     CHECK / REQUIRE / assert whose expression compares with >=, <=, > or <, or with != 0, and doctest::Approx. A `// tolerance: <reason>`
     (GDScript `# tolerance: <reason>`) comment with a non-empty reason, on the same line or the line above, justifies it. Unchanged lines
     are never flagged; in a merge (MERGE_HEAD set) a line is added only when it is new against both parents (lane PLAY-4 r3: a merge of
     the archive brought every line the archive gained as "added", those of commits made before this rule among them).
  6. no conflict-marker lines in staged text: lines starting with seven '<' or '>', or consisting of exactly seven '='.
     Binary blobs (containing NUL) are skipped by this check. No paths are exempt, including documentation and test fixtures.

Pure renames are checked too. Staged blobs are read through one cat-file process. The legacy
gate lives at archive/tools/fleet/precommit.py. Exit non-zero blocks the commit.
"""
from __future__ import annotations

import hashlib
import os
import re
import subprocess
import sys
from collections.abc import Iterator
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
RETAIL_SUFFIXES = {".big", ".w3d", ".dds", ".tga", ".map", ".apt", ".bse", ".wav", ".mp3", ".vp6", ".csf", ".str", ".wnd", ".dat"}
FIXTURE_ROOTS = ("archive/content/", "archive/importer/tests/fixtures/", "archive/docs/assets/",
                 "archive/launcher/OpenBFME.Launcher/Assets/", "archive/game/data/base/assets/ui/")
# the MSF container of a PDB (7.00) and the older program database format: debug symbols never enter the public tree or a package
MSF_MAGICS = (b"Microsoft C/C++ MSF 7.00\r\n\x1aDS\x00\x00\x00", b"Microsoft C/C++ program database 2.00\r\n\x1aJG\x00\x00")
PRIVATE_SUFFIXES = {".pdb"}


def private_symbols(data: bytes) -> bool:
    """Debug symbols (a PDB's MSF container) anywhere in `data`."""
    return any(m in data for m in MSF_MAGICS)


CONTAINER_SUFFIXES = {".zip", ".jar", ".gz", ".tgz", ".tar", ".bz2", ".tbz2", ".xz", ".txz", ".lzma", ".7z", ".rar", ".cab", ".zst", ".zstd",
                      ".lz4", ".z", ".zlib", ".big", ".pck"}
CONTAINER_ALLOWLIST = Path(__file__).resolve().parent / "precommit_containers.txt"


def _is_tar(data: bytes) -> bool:
    """A tar header by its checksum (ustar, GNU and V7 alike)."""
    if len(data) < 512 or data[0] == 0:
        return False
    field = data[148:156].replace(b"\0", b" ").strip()
    if not field or any(c not in b"01234567" for c in field):
        return False
    return int(field, 8) == sum(data[:148]) + 8 * 32 + sum(data[156:512])


def container_kind(name: str, data: bytes) -> str:
    """The container or compressed-stream kind of a file, by its suffix or its bytes (never unpacked); '' when it is neither."""
    suffix = Path(name.lower()).suffix
    if suffix in CONTAINER_SUFFIXES:
        return f"a {suffix} file"
    head = data[:8]
    kinds = ((b"PK\x03\x04", "ZIP"), (b"PK\x05\x06", "ZIP"), (b"PK\x07\x08", "ZIP"), (b"\x1f\x8b", "gzip"), (b"\xfd7zXZ\x00", "xz"),
             (b"\x5d\x00\x00", "LZMA"), (b"7z\xbc\xaf\x27\x1c", "7-Zip"), (b"Rar!\x1a\x07", "RAR"), (b"MSCF", "CAB"),
             (b"\x28\xb5\x2f\xfd", "Zstandard"), (b"\x04\x22\x4d\x18", "LZ4"), (b"BIGF", "BIG"), (b"BIG4", "BIG"), (b"GDPC", "Godot pack"))
    for magic, kind in kinds:
        if head.startswith(magic):
            return kind
    if head[:3] == b"BZh" and len(head) > 3 and 0x31 <= head[3] <= 0x39:
        return "bzip2"
    # a zlib stream: CMF 0x78, a valid FLG; "x^" also starts text, so that one counts only in binary data
    if len(head) >= 2 and head[0] == 0x78 and head[1] in (0x01, 0x5E, 0x9C, 0xDA) and (head[1] != 0x5E or b"\0" in data[:4096]):
        return "zlib"
    if len(data) >= 12 and data[-4:] == b"GDPC":
        return "Godot pack (appended)"
    if _is_tar(data):
        return "tar"
    return ""


def container_allowlist() -> set[tuple[str, str]]:
    """(path, sha256) pairs of tools/precommit_containers.txt: "<sha256>  <path>  # reason" lines; # comments and blank lines skipped."""
    pairs = set()
    if CONTAINER_ALLOWLIST.is_file():
        for line in CONTAINER_ALLOWLIST.read_text(encoding="utf-8").splitlines():
            line = line.split("#", 1)[0].strip()
            if line:
                digest, path = line.split(None, 1)
                pairs.add((path.strip(), digest.lower()))
    return pairs


MACHINE_PATH = re.compile(r"[A-Za-z]:\\Users\\|/home/[a-z]|/Users/[A-Z]")


def mp3_frame_size(data: bytes) -> int:
    if len(data) < 4 or data[0] != 0xFF or data[1] & 0xE0 != 0xE0:
        return 0
    version, layer = (data[1] >> 3) & 3, (data[1] >> 1) & 3
    bitrate, sample = data[2] >> 4, (data[2] >> 2) & 3
    if version == 1 or layer != 1 or bitrate in (0, 15) or sample == 3:
        return 0
    rates = (0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320) if version == 3 else (0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160)
    hz = (44100, 48000, 32000)[sample] // {3: 1, 2: 2, 0: 4}[version]
    return (144000 if version == 3 else 72000) * rates[bitrate] // hz + ((data[2] >> 1) & 1)


def retail_content(data: bytes) -> str:
    """Names the retail format the leading bytes announce, or ''. Independent of the file name."""
    if data[:4] == b"RIFF" and data[8:12] == b"WAVE":
        return "RIFF/WAVE audio"
    if data[:3] == b"ID3":
        return "ID3-tagged MP3 audio"
    frame = mp3_frame_size(data)
    if frame and len(data) >= frame + 4:
        following = data[frame:]
        next_frame = mp3_frame_size(following)
        # A sync word alone also occurs in random Ed25519 signatures. Require two
        # complete, consecutive frames with the same MPEG version and sample rate.
        if next_frame and len(following) >= next_frame and data[1] & 0x1E == following[1] & 0x1E and data[2] & 0x0C == following[2] & 0x0C:
            return "MPEG Layer III audio"
    if data[:4] in (b"BIGF", b"BIG4"):
        return "BIG archive"
    if data[:4] == b"DDS ":
        return "DDS texture"
    return ""


def git(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", *args], cwd=REPO, capture_output=True)


def staged() -> list[tuple[str, str]]:
    """Changed (path, staged object ID) pairs, including rename destinations; NUL framing preserves unusual file names."""
    result = git("diff", "--cached", "--raw", "--no-abbrev", "-z", "-M", "--diff-filter=ACMR")
    result.check_returncode()
    entries = iter(result.stdout.split(b"\0")[:-1])
    files = []
    for entry in entries:
        _, mode, _, oid, status = entry.split()
        path = next(entries)
        if status.startswith((b"R", b"C")):
            path = next(entries)
        if mode != b"160000":  # a gitlink names a commit, not a staged file
            files.append((os.fsdecode(path), oid.decode("ascii")))
    return files


def staged_blobs(files: list[tuple[str, str]]) -> Iterator[tuple[str, bytes]]:
    """Read one blob at a time with a single Git process, bounded by the largest staged file rather than the whole commit."""
    if not files:
        return
    with subprocess.Popen(["git", "cat-file", "--batch"], cwd=REPO, stdin=subprocess.PIPE, stdout=subprocess.PIPE) as batch:
        assert batch.stdin is not None and batch.stdout is not None
        try:
            for rel, oid in files:
                batch.stdin.write(oid.encode("ascii") + b"\n")
                batch.stdin.flush()
                header = batch.stdout.readline().split()
                if len(header) != 3 or header[0] != oid.encode("ascii") or header[1] != b"blob":
                    raise ValueError(f"cannot read staged blob: {rel}")
                size = int(header[2])
                data = batch.stdout.read(size)
                if len(data) != size or batch.stdout.read(1) != b"\n":
                    raise ValueError(f"incomplete staged blob: {rel}")
                yield rel, data
        finally:
            batch.stdin.close()
        if batch.wait():
            raise ValueError("git cat-file failed while reading staged blobs")


_CONFLICT_MARKER = re.compile(rb"^(?:<{7}|>{7}|={7}\r?$)", re.MULTILINE)


def conflict_problems(rel: str, data: bytes) -> list[str]:
    """Flag whole-blob marker lines, not diff additions. NUL-containing files are binary; other underline lengths are not markers."""
    if b"\0" in data:
        return []
    problems = []
    number, offset = 1, 0
    for match in _CONFLICT_MARKER.finditer(data):
        number += data.count(b"\n", offset, match.start())
        offset = match.start()
        problems.append(f"{rel}:{number}: conflict marker in staged text")
    return problems


def check_file(rel: str, data: bytes | None = None, verb: str = "staged") -> str:
    """The commit gate's rules for one file: a retail suffix, retail-format bytes or a developer-machine path. Returns the problem, or ''.
    data=None checks the name only. Also the public sync's audit (tools/release/autorelease.py audit-tree, lane AUTOREL-1)."""
    if Path(rel).suffix.lower() in RETAIL_SUFFIXES and not rel.startswith(FIXTURE_ROOTS):
        return f"retail-format file {verb}: {rel}"
    if data is None:
        return ""
    kind = "" if rel.startswith(FIXTURE_ROOTS) else retail_content(data)
    if kind:
        return f"retail-format bytes {verb} ({kind}): {rel}"
    if b"\x00" not in data[:4096] and MACHINE_PATH.search(data.decode("utf-8", "replace")):
        return f"developer-machine path in {rel}"
    return ""


def commit_only_checks(rel: str, data: bytes) -> str:
    """The commit gate's rules beyond check_file (lane WINCRASH-1): private debug symbols and containers. The public sync applies its own
    text-only rule (tools/release/autorelease.py, lane AUTOREL-1), so these stay out of check_file."""
    if Path(rel).suffix.lower() in PRIVATE_SUFFIXES:
        return f"debug symbols staged (the PDB is private, stop S-1923): {rel}"
    if private_symbols(data):
        return f"debug symbols (MSF / PDB bytes) staged (private, stop S-1923): {rel}"
    kind = container_kind(rel, data)
    if kind and (rel, hashlib.sha256(data).hexdigest()) not in container_allowlist():
        return (f"container or compressed stream staged ({kind}): {rel} (the gate does not unpack: generate it at test time, or list "
                f"its sha256 and path in tools/precommit_containers.txt with a reason)")
    return ""


TEST_ROOTS = ("engine/tests/", "godot/tests/")
CPP_SUFFIXES = {".cpp", ".cc", ".cxx", ".h", ".hpp", ".hh", ".inl"}
TOLERANCE_FIX = (
    "bounds and approximations can hide wrong results; assert the exact value, or justify the bound with `// tolerance: <reason>` "
    "(GDScript `# tolerance: <reason>`); example for an expected count of 3: `CHECK(count == 3);` (GDScript `assert(count == 3)`)")
_STRING = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
_TEMPLATE = re.compile(r"\b[A-Za-z_][\w:]*<[^<>();&|=!]*>")
_COMPARE = re.compile(r"[<>]=?|!=\s*0(?![\w.])|!=\s*0\.0*f?(?![\w.])")
_CPP_ASSERT = re.compile(r"\b(?:CHECK|REQUIRE|WARN)(?:_FALSE|_MESSAGE)?\s*\(")
_CPP_TOLERANCE_MACRO = re.compile(r"\b(?:CHECK|REQUIRE|WARN)_(?:GE|LE|GT|LT)\b")
_APPROX = re.compile(r"\bApprox\b")
_GD_ASSERT = re.compile(r"\bassert\s*\(")
_CPP_JUSTIFIED = re.compile(r"//\s*tolerance:\s*\S")
_GD_JUSTIFIED = re.compile(r"#\s*tolerance:\s*\S")


def _code_of(line: str, gdscript: bool) -> str:
    """The line's code with string literals, the trailing comment, arrows and shifts removed, so only real operators are left."""
    line = _STRING.sub('""', line)
    line = line.split("#" if gdscript else "//", 1)[0]
    if not gdscript:
        for _ in range(3):
            line = _TEMPLATE.sub("T", line)
    return line.replace("->", " ").replace("<<", " ").replace(">>", " ")


def tolerance_kind(line: str, gdscript: bool) -> str:
    """What makes `line` a tolerance assertion (its text, e.g. 'CHECK_GE' or 'CHECK(... >= ...)'), or ''. Block-comment lines never are."""
    if line.lstrip().startswith(("/*", "*", "//", "#")):
        return ""
    code = _code_of(line, gdscript)
    if gdscript:
        match = _GD_ASSERT.search(code)
        if match and _COMPARE.search(code[match.end():]):
            return "assert(... compared with a bound or != 0)"
        return ""
    match = _CPP_TOLERANCE_MACRO.search(code)
    if match:
        return match.group(0)
    if _APPROX.search(code):
        return "doctest::Approx"
    match = _CPP_ASSERT.search(code)
    if match and _COMPARE.search(code[match.end():]):
        return f"{match.group(0).rstrip('( ')}(... compared with a bound or != 0)"
    return ""


def tolerance_problems(rel: str, text: str, added: set[int]) -> list[str]:
    """Unjustified tolerance assertions among the `added` (1-based) lines of the test file `rel`."""
    suffix = Path(rel).suffix.lower()
    if rel.startswith("engine/tests/") and suffix in CPP_SUFFIXES:
        gdscript = False
    elif rel.startswith("godot/tests/") and suffix == ".gd":
        gdscript = True
    else:
        return []
    justified = _GD_JUSTIFIED if gdscript else _CPP_JUSTIFIED
    lines = text.splitlines()
    problems = []
    for number in sorted(added):
        if not 1 <= number <= len(lines):
            continue
        kind = tolerance_kind(lines[number - 1], gdscript)
        if not kind:
            continue
        if justified.search(lines[number - 1]) or (number > 1 and justified.search(lines[number - 2])):
            continue
        problems.append(f"{rel}:{number}: tolerance assertion {kind}: `{lines[number - 1].strip()}`: {TOLERANCE_FIX}")
    return problems


def added_lines_against(*base: str) -> dict[str, set[int]]:
    """{path: new line numbers} the staged diff against `base` (HEAD when empty) adds under the test roots (a pure rename adds none)."""
    out = git("-c", "core.quotepath=false", "diff", "--cached", "-U0", "-M", "--no-color", "--diff-filter=ACMR", *base, "--",
              *TEST_ROOTS).stdout.decode("utf-8", "replace")
    result: dict[str, set[int]] = {}
    path = ""
    for line in out.splitlines():
        if line.startswith("+++ "):
            path = line[4:].removeprefix("b/") if line != "+++ /dev/null" else ""
        elif line.startswith("@@") and path:
            match = re.match(r"@@ -\S+ \+(\d+)(?:,(\d+))? @@", line)
            if match:
                start, count = int(match.group(1)), int(match.group(2) if match.group(2) is not None else 1)
                result.setdefault(path, set()).update(range(start, start + count))
    return result


def added_lines() -> dict[str, set[int]]:
    """{path: new line numbers} the commit adds under the test roots: against HEAD, and in a merge against MERGE_HEAD as well (a line either
    parent already has was judged, or predates the rule, where that parent was committed)."""
    result = added_lines_against()
    if git("rev-parse", "-q", "--verify", "MERGE_HEAD").returncode == 0:
        other = added_lines_against("MERGE_HEAD")
        result = {path: lines & other.get(path, set()) for path, lines in result.items()}
        result = {path: lines for path, lines in result.items() if lines}
    return result


def main() -> int:
    problems = []
    try:
        files = staged()
        added = added_lines()
        for rel, data in staged_blobs(files):
            if rel in added:
                problems += tolerance_problems(rel, data.decode("utf-8", "replace"), added[rel])
            problems += conflict_problems(rel, data)
            problem = check_file(rel) or commit_only_checks(rel, data) or check_file(rel, data)
            if problem:
                problems.append(problem)
    except (subprocess.CalledProcessError, ValueError, OSError) as exc:
        problems.append(f"cannot check staged files: {exc}")
    if problems:
        print("COMMIT GATE FAILED")
        for problem in problems:
            print(" -", problem)
        return 1
    print(f"commit gate ok ({len(files)} changed files checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
