#!/usr/bin/env python3
"""Commit gate for the rebuild. Two checks that keep a public repo legal:

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

Pure renames are skipped: their content already passed this gate. The legacy
gate lives at archive/tools/fleet/precommit.py. Exit non-zero blocks the commit.
"""
from __future__ import annotations

import hashlib
import re
import subprocess
import sys
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


def retail_content(data: bytes) -> str:
    """Names the retail format the leading bytes announce, or ''. Independent of the file name."""
    if data[:4] == b"RIFF" and data[8:12] == b"WAVE":
        return "RIFF/WAVE audio"
    if data[:3] == b"ID3":
        return "ID3-tagged MP3 audio"
    if len(data) >= 4 and data[0] == 0xFF and (data[1] & 0xE0) == 0xE0:
        version, layer = (data[1] >> 3) & 3, (data[1] >> 1) & 3
        if version != 1 and layer == 1 and (data[2] >> 4) != 15 and ((data[2] >> 2) & 3) != 3:
            return "MPEG Layer III audio"
    if data[:4] in (b"BIGF", b"BIG4"):
        return "BIG archive"
    if data[:4] == b"DDS ":
        return "DDS texture"
    return ""


def git(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", *args], cwd=REPO, capture_output=True)


def staged() -> list[str]:
    out = git("diff", "--cached", "--name-status", "-M", "--diff-filter=ACMR").stdout.decode("utf-8", "replace")
    files = []
    for line in out.splitlines():
        parts = line.split("\t")
        # every destination is checked, pure renames (R100) included: a rename can move bytes out of an exemption or behind another suffix
        files.append(parts[-1].replace("\\", "/"))
    return files


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


def main() -> int:
    problems = []
    files = staged()
    for rel in files:
        data = git("show", f":{rel}").stdout
        problem = check_file(rel) or commit_only_checks(rel, data) or check_file(rel, data)
        if problem:
            problems.append(problem)
    if problems:
        print("COMMIT GATE FAILED")
        for problem in problems:
            print(" -", problem)
        return 1
    print(f"commit gate ok ({len(files)} changed files checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
