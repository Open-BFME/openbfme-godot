#!/usr/bin/env python3
"""Lane RELEASE-1: is a built GDExtension library exactly the committed sources?

    python3 tools/release/lib_provenance.py <library> --version <v> --commit <sha> --engine-id <64 hex>
    python3 tools/release/lib_provenance.py --engine-id-of <engine dir of an export> [--x86-32 ON|OFF] [--id-options STR]
    python3 tools/release/lib_provenance.py --record <library>       (prints the record's fields)

A library carries one build record (engine/cmake/BuildVersion.cmake, BuildVersion::kBuildRecord):
    OPENBFME-BUILD-INFO 1 version=<v> commit=<sha> dirty=<0|1> engine-id=<64 hex> x86-32=<ON|OFF> id-options=<rest>
The check needs exactly one record whose version and commit are the package's, dirty=0, and whose engine id (the configure-time SHA-256 of
the engine sources, cmake/EngineId.cmake) equals the id of the committed export, which package.sh computes from `git archive HEAD engine`
with --engine-id-of (this script re-implements EngineId.cmake's manifest, and the test pins the two against each other). Review r1: a grep
for the commit hash accepted a deliberately mismatched library; the commit string alone proves nothing.
Exit 0 = the library is the committed build; 1 with one line per problem.
"""
from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path

PREFIX = b"OPENBFME-BUILD-INFO "
FIELDS = ("version", "commit", "dirty", "engine-id", "x86-32", "id-options")


def records(data: bytes) -> list[dict]:
    """Every build record in a binary (NUL-terminated strings starting with the prefix)."""
    out = []
    pos = 0
    while (pos := data.find(PREFIX, pos)) >= 0:
        end = data.find(b"\0", pos)
        line = data[pos:end if end >= 0 else len(data)].decode("utf-8", "replace")
        pos += len(PREFIX)
        m = re.match(r"OPENBFME-BUILD-INFO 1 version=(\S+) commit=(\S+) dirty=(\S+) engine-id=(\S+) x86-32=(\S*) id-options=(.*)$", line)
        out.append(dict(zip(FIELDS, m.groups())) if m else {"malformed": line[:120]})
    return out


def check(data: bytes, version: str, commit: str, engine_id: str) -> list[str]:
    recs = records(data)
    if len(recs) != 1:
        return [f"the library carries {len(recs)} build records (exactly one is required: built by engine/cmake/BuildVersion.cmake?)"]
    r = recs[0]
    if "malformed" in r:
        return [f"malformed build record: {r['malformed']}"]
    problems = []
    if r["version"] != version:
        problems.append(f"the library is version {r['version']}, the package is {version}")
    if r["commit"] != commit:
        problems.append(f"the library was built from commit {r['commit']}, the package is {commit}")
    if r["dirty"] != "0":
        problems.append("the library was built from a tree with local changes (dirty=1)")
    if r["engine-id"] != engine_id:
        problems.append(f"the library's engine sources (engine id {r['engine-id'][:16]}...) are not the committed ones ({engine_id[:16]}...)")
    return problems


def engine_id_of(root: Path, x86_32: str = "OFF", options: str = "") -> str:
    """cmake/EngineId.cmake's openbfme_compute_engine_id: SHA-256 of "openbfme-engine-id 1", the config line and one
    "file <path> <sha256 of the LF text>" line per file under src/, cmake/, data/, thirdparty/lua-4.0.1/ and CMakeLists.txt, sorted."""
    files = set()
    for d in ("src", "cmake", "data", "thirdparty/lua-4.0.1"):
        base = root / d
        if base.is_dir():
            files.update(p.relative_to(root).as_posix() for p in base.rglob("*") if p.is_file())
    if (root / "CMakeLists.txt").is_file():
        files.add("CMakeLists.txt")
    manifest = f"openbfme-engine-id 1\nconfig x86_32={x86_32} options={options}\n"
    if not any(f.startswith("src/") for f in files):
        raise ValueError(f"no engine sources under {root}/src")
    # CMake's list(SORT) is a byte-wise string sort, like Python's sorted() on ASCII paths
    for rel in sorted(files):
        text = (root / rel).read_bytes().replace(b"\r\n", b"\n")
        manifest += f"file {rel} {hashlib.sha256(text).hexdigest()}\n"
    return hashlib.sha256(manifest.encode("utf-8")).hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("library", nargs="?")
    ap.add_argument("--version")
    ap.add_argument("--commit")
    ap.add_argument("--engine-id")
    ap.add_argument("--engine-id-of")
    ap.add_argument("--x86-32", default="OFF")
    ap.add_argument("--id-options", default="")
    ap.add_argument("--record", action="store_true")
    a = ap.parse_args()
    if a.engine_id_of:
        print(engine_id_of(Path(a.engine_id_of), a.x86_32, a.id_options))
        return 0
    if not a.library:
        ap.error("a library is required")
    data = Path(a.library).read_bytes()
    if a.record:
        recs = records(data)
        if len(recs) != 1 or "malformed" in recs[0]:
            print(f"{len(recs)} build records: {recs}", file=sys.stderr)
            return 1
        for k in FIELDS:
            print(f"{k}={recs[0][k]}")
        return 0
    if not (a.version and a.commit and a.engine_id):
        ap.error("--version, --commit and --engine-id are required")
    problems = check(data, a.version, a.commit, a.engine_id)
    for p in problems:
        print(f"LIBRARY PROVENANCE: {a.library}: {p}", file=sys.stderr)
    if not problems:
        print(f"library provenance ok: {Path(a.library).name} is {a.version} ({a.commit[:10]}, engine id {a.engine_id[:16]}...)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
