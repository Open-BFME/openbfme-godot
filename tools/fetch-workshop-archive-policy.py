#!/usr/bin/env python3
"""Pin one BFME Workshop package as an English BIG archive policy.

The workshop response is metadata only. Raw retail payloads are never written by
this tool. The expected package name/version are mandatory so a moving upstream
entry cannot silently retarget a checked-in baseline.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import tempfile
from typing import Any
from urllib.parse import urlencode
from urllib.request import Request, urlopen


WORKSHOP_ENDPOINT = "https://bfmeladder.com/api/workshop/download"


def _canonical_path(value: str) -> str:
    normalized = value.replace("\\", "/")
    path = PurePosixPath(normalized)
    if (
        not normalized
        or normalized.startswith(("/", "~"))
        or path.is_absolute()
        or any(part in {"", ".", ".."} or ":" in part for part in path.parts)
    ):
        raise ValueError(f"unsafe workshop path: {value!r}")
    return "/".join(path.parts)


def _sha256_rows(rows: list[str]) -> str:
    payload = ("\n".join(rows) + "\n").encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def _receipt_sha256(entry: dict[str, Any]) -> str:
    files = entry.get("Files")
    maps = entry.get("Maps")
    factions = entry.get("Factions")
    if not isinstance(files, list) or not isinstance(maps, list) or not isinstance(
        factions, list
    ):
        raise ValueError("workshop receipt is missing files, maps, or factions")
    rows = [
        "|".join(
            (
                str(entry.get("Guid", "")),
                str(entry.get("Name", "")),
                str(entry.get("Version", "")),
                str(entry.get("CreationTime", "")),
                str(len(files)),
                str(len(maps)),
                str(len(factions)),
            )
        )
    ]
    for item in sorted(files, key=lambda row: str(row.get("Name", "")).casefold()):
        rows.append(
            "|".join(
                (
                    _canonical_path(str(item.get("Name", ""))),
                    str(item.get("Md5", "")),
                    str(item.get("Size", "")),
                    str(item.get("Language", "")),
                    str(item.get("Url", "")),
                )
            )
        )
    return _sha256_rows(rows)


def build_policy(
    entry: dict[str, Any],
    *,
    game: str,
    patch: str,
    language: str,
    expected_name: str,
    expected_version: str,
    install_root: Path | None = None,
) -> dict[str, Any]:
    if entry.get("Name") != expected_name or entry.get("Version") != expected_version:
        raise ValueError(
            "workshop identity changed: "
            f"expected {expected_name!r} {expected_version!r}, got "
            f"{entry.get('Name')!r} {entry.get('Version')!r}"
        )
    wanted_language = language.upper()
    archives: list[dict[str, Any]] = []
    for item in entry.get("Files", []):
        if not isinstance(item, dict):
            raise ValueError("workshop file row is invalid")
        path = _canonical_path(str(item.get("Name", "")))
        size = item.get("Size")
        member_language = str(item.get("Language", ""))
        language_tokens = set(
            member_language.upper().replace(",", " ").replace(";", " ").split()
        )
        if not path.casefold().endswith(".big") or not (
            "ALL" in language_tokens or wanted_language in language_tokens
        ):
            continue
        md5 = str(item.get("Md5", "")).casefold()
        if len(md5) != 32 or any(char not in "0123456789abcdef" for char in md5):
            raise ValueError(f"workshop archive MD5 is invalid: {path}")
        if not isinstance(size, int) or isinstance(size, bool):
            raise ValueError(f"workshop archive size is invalid: {path}")
        if size < 16:
            if install_root is None:
                raise ValueError(
                    f"workshop archive has a zero/sentinel size and needs "
                    f"--install-root verification: {path}"
                )
            local_path = install_root.joinpath(*PurePosixPath(path).parts)
            if not local_path.is_file():
                raise ValueError(f"locally verified workshop archive is missing: {path}")
            digest = hashlib.md5()  # noqa: S324 - upstream contract is MD5
            with local_path.open("rb") as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(chunk)
            if digest.hexdigest().casefold() != md5:
                raise ValueError(f"local workshop archive MD5 mismatch: {path}")
            size = local_path.stat().st_size
            if size < 16:
                raise ValueError(f"local workshop archive is not a BIG payload: {path}")
        archives.append(
            {
                "path": path,
                "md5": md5,
                "size": size,
                "language": member_language.upper(),
            }
        )
    archives.sort(key=lambda row: row["path"].casefold())
    if not archives:
        raise ValueError("workshop package contains no selected BIG archives")
    policy_rows = [
        f"{row['path']}|{row['md5']}|{row['size']}|{row['language']}"
        for row in archives
    ]
    return {
        "schema": "openbfme.retail-archive-policy",
        "schemaVersion": 1,
        "game": game,
        "patch": patch,
        "package": {
            "guid": str(entry.get("Guid", "")),
            "name": str(entry.get("Name", "")),
            "version": str(entry.get("Version", "")),
            "receiptSha256": _receipt_sha256(entry),
        },
        "language": language.upper(),
        "policySha256": _sha256_rows(policy_rows),
        "archives": archives,
    }


def _fetch(guid: str) -> dict[str, Any]:
    request = Request(
        f"{WORKSHOP_ENDPOINT}?{urlencode({'guid': guid})}",
        headers={"AuthAccountUuid": "unauthenticated", "AuthAccountPassword": ""},
    )
    with urlopen(request, timeout=30) as response:  # noqa: S310 - pinned host
        value = json.load(response)
    if not isinstance(value, dict):
        raise ValueError(f"workshop entry {guid!r} was not found")
    return value


def _write_atomic(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    handle, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(handle, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--guid", required=True)
    parser.add_argument("--game", required=True)
    parser.add_argument("--patch", required=True)
    parser.add_argument("--language", default="EN")
    parser.add_argument("--expected-name", required=True)
    parser.add_argument("--expected-version", required=True)
    parser.add_argument(
        "--install-root",
        type=Path,
        help="retail root used only to verify official zero/sentinel-size rows",
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    policy = build_policy(
        _fetch(args.guid),
        game=args.game,
        patch=args.patch,
        language=args.language,
        expected_name=args.expected_name,
        expected_version=args.expected_version,
        install_root=args.install_root.resolve() if args.install_root else None,
    )
    _write_atomic(args.output.resolve(), policy)
    print(
        "WORKSHOP_ARCHIVE_POLICY PASS "
        f"guid={args.guid} version={args.expected_version} "
        f"archives={len(policy['archives'])} policy_sha256={policy['policySha256']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
