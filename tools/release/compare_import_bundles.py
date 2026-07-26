#!/usr/bin/env python3
"""Compare two private OpenBFME import outputs without exporting payload bytes."""

from __future__ import annotations

import argparse
import binascii
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
from typing import Iterable
import zlib

RECEIPT_SCHEMA = "openbfme.import-repro-receipt"
IGNORED_NAMES = {".DS_Store", "Thumbs.db"}


def glb_capabilities(path: Path) -> tuple[bool, bool, bool]:
    """Return whether a GLB is valid and declares animations and skins."""

    try:
        with path.open("rb") as stream:
            header = stream.read(12)
            if len(header) != 12:
                return False, False, False
            magic, version, declared_size = struct.unpack("<4sII", header)
            if magic != b"glTF" or version != 2 or declared_size != path.stat().st_size:
                return False, False, False
            chunk_header = stream.read(8)
            if len(chunk_header) != 8:
                return False, False, False
            chunk_size, chunk_kind = struct.unpack("<II", chunk_header)
            if chunk_kind != 0x4E4F534A or chunk_size > 64 * 1024 * 1024:
                return False, False, False
            payload = stream.read(chunk_size)
            if len(payload) != chunk_size:
                return False, False, False
        document = json.loads(payload.rstrip(b" \t\r\n\x00").decode("utf-8"))
        asset = document.get("asset")
        if not isinstance(asset, dict) or asset.get("version") != "2.0":
            return False, False, False
        return True, bool(document.get("animations")), bool(document.get("skins"))
    except (OSError, UnicodeDecodeError, ValueError, TypeError, AttributeError):
        return False, False, False


def valid_png(path: Path) -> bool:
    """Validate PNG framing, chunk CRCs, and the compressed image stream."""

    try:
        payload = path.read_bytes()
        if not payload.startswith(b"\x89PNG\r\n\x1a\n"):
            return False
        offset = 8
        saw_ihdr = saw_idat = saw_iend = False
        compressed = bytearray()
        while offset + 12 <= len(payload):
            length = struct.unpack(">I", payload[offset : offset + 4])[0]
            kind = payload[offset + 4 : offset + 8]
            end = offset + 12 + length
            if end > len(payload):
                return False
            data = payload[offset + 8 : offset + 8 + length]
            expected_crc = struct.unpack(">I", payload[offset + 8 + length : end])[0]
            if binascii.crc32(kind + data) & 0xFFFFFFFF != expected_crc:
                return False
            if kind == b"IHDR":
                if saw_ihdr or length != 13 or offset != 8:
                    return False
                width, height, depth, color, compression, filtering, interlace = struct.unpack(
                    ">IIBBBBB", data
                )
                if width == 0 or height == 0 or depth not in {1, 2, 4, 8, 16}:
                    return False
                if color not in {0, 2, 3, 4, 6} or compression != 0 or filtering != 0 or interlace not in {0, 1}:
                    return False
                saw_ihdr = True
            elif kind == b"IDAT":
                if not saw_ihdr:
                    return False
                saw_idat = True
                compressed.extend(data)
            elif kind == b"IEND":
                if length != 0 or end != len(payload):
                    return False
                saw_iend = True
                break
            offset = end
        if not (saw_ihdr and saw_idat and saw_iend):
            return False
        return len(zlib.decompress(bytes(compressed))) > 0
    except (OSError, ValueError, zlib.error):
        return False


def families(relative: str, path: Path) -> set[str]:
    value = relative.casefold().replace("\\", "/")
    suffix = Path(value).suffix
    result: set[str] = set()
    if "/maps/" in f"/{value}" or value.startswith("maps/"):
        result.add("maps")
    if any(token in value for token in ("/ui/", "/hud/", "portrait", "button")):
        result.add("ui")
    if suffix == ".png":
        if not valid_png(path):
            raise ValueError(f"invalid PNG texture: {relative}")
        result.add("textures")
    if suffix == ".glb":
        valid, has_animations, has_skins = glb_capabilities(path)
        if not valid:
            raise ValueError(f"invalid GLB model: {relative}")
        result.add("models")
        if has_animations:
            result.add("animations")
        if has_skins:
            result.add("skeletons")
    if suffix in {".wav", ".ogg", ".mp3"}:
        result.add("audio")
    if suffix == ".json":
        result.add("rules")
    return result or {"other"}


def contained_files(root: Path) -> list[Path]:
    result: list[Path] = []

    def visit(directory: Path) -> None:
        for entry in os.scandir(directory):
            path = Path(entry.path)
            stat = entry.stat(follow_symlinks=False)
            if entry.is_symlink() or getattr(stat, "st_file_attributes", 0) & 0x400:
                raise ValueError("bundle contains a link or reparse point")
            if entry.is_dir(follow_symlinks=False):
                visit(path)
            elif entry.is_file(follow_symlinks=False):
                result.append(path)

    root_stat = root.lstat()
    if root.is_symlink() or getattr(root_stat, "st_file_attributes", 0) & 0x400:
        raise ValueError("bundle root is a link or reparse point")
    visit(root)
    return result


def inventory(root: Path) -> tuple[dict[str, tuple[int, str]], dict[str, int]]:
    if not root.is_dir():
        raise ValueError("bundle root is not a directory")
    rows: dict[str, tuple[int, str]] = {}
    canonical_paths: set[str] = set()
    counts: dict[str, int] = {}
    for item in sorted(contained_files(root), key=lambda path: path.as_posix().casefold()):
        if item.name in IGNORED_NAMES:
            continue
        relative = item.relative_to(root).as_posix()
        canonical = relative.casefold()
        if canonical in canonical_paths:
            raise ValueError("duplicate canonical bundle path")
        canonical_paths.add(canonical)
        hasher = hashlib.sha256()
        with item.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                hasher.update(chunk)
        digest = hasher.hexdigest()
        rows[relative] = (item.stat().st_size, digest)
        for key in families(relative, item):
            counts[key] = counts.get(key, 0) + 1
    if not rows:
        raise ValueError("bundle is empty")
    return rows, dict(sorted(counts.items()))


def canonical_digest(rows: dict[str, tuple[int, str]]) -> str:
    digest = hashlib.sha256()
    for path, (size, sha256) in sorted(rows.items()):
        digest.update(path.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(size).encode("ascii"))
        digest.update(b"\0")
        digest.update(sha256.encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def safe_identity(value: str, label: str) -> str:
    if not re.fullmatch(r"[0-9A-Za-z][0-9A-Za-z._-]{0,127}", value):
        raise ValueError(f"unsafe {label}")
    return value


def compare(
    first: Path,
    second: Path,
    game: str,
    profile: str,
    release_commit: str,
    minimums: dict[str, int],
) -> dict[str, object]:
    left, left_counts = inventory(first)
    right, right_counts = inventory(second)
    missing = sorted(set(left) - set(right))
    extra = sorted(set(right) - set(left))
    changed = sorted(path for path in set(left) & set(right) if left[path] != right[path])
    if missing or extra or changed:
        raise ValueError(
            f"bundle mismatch missing={len(missing)} extra={len(extra)} changed={len(changed)}"
        )
    if left_counts != right_counts:
        raise ValueError("asset family counts differ")
    for key, minimum in minimums.items():
        if left_counts.get(key, 0) < minimum:
            raise ValueError(f"asset family {key} has {left_counts.get(key, 0)} files; expected {minimum}")
    digest = canonical_digest(left)
    return {
        "schema": RECEIPT_SCHEMA,
        "schemaVersion": 1,
        "game": safe_identity(game, "game"),
        "profile": safe_identity(profile, "profile"),
        "releaseCommit": release_commit,
        "identical": True,
        "bundleDigestA": digest,
        "bundleDigestB": digest,
        "fileCount": len(left),
        "byteCount": sum(size for size, _ in left.values()),
        "families": left_counts,
    }


def parse_minimums(values: Iterable[str]) -> dict[str, int]:
    result: dict[str, int] = {}
    for value in values:
        name, separator, count = value.partition("=")
        if not separator or not name or not count.isdigit():
            raise ValueError(f"invalid --require-family value: {value}")
        result[name] = int(count)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("first", type=Path)
    parser.add_argument("second", type=Path)
    parser.add_argument("--game", required=True, choices=("bfme2", "rotwk"))
    parser.add_argument("--profile", required=True)
    parser.add_argument("--release-commit", required=True)
    parser.add_argument("--require-family", action="append", default=[])
    parser.add_argument("--receipt", type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9a-f]{40}", args.release_commit):
        parser.error("--release-commit must be a full lowercase SHA-1")
    try:
        receipt = compare(
            args.first.resolve(),
            args.second.resolve(),
            args.game,
            args.profile,
            args.release_commit,
            parse_minimums(args.require_family),
        )
        args.receipt.parent.mkdir(parents=True, exist_ok=True)
        args.receipt.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    except (OSError, ValueError) as error:
        print(f"IMPORT_REPRO FAIL {error}", file=sys.stderr)
        return 1
    print(
        f"IMPORT_REPRO_PASS files={receipt['fileCount']} bytes={receipt['byteCount']} "
        f"digest={receipt['bundleDigestA']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
