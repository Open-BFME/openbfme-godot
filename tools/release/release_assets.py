#!/usr/bin/env python3
"""Lane AUTOREL-1 r2: read a GitHub release back and check it against the package folder before (and after) it is published
(publish_release.sh --execute).

    python3 tools/release/release_assets.py --release-json FILE --dir <package dir> --version TAG --draft true|false
        --prerelease true|false [--repo OWNER/NAME]

FILE is `gh api repos/<repo>/releases/<id>`'s answer. The release must have the tag, the draft and pre-release flags given, and exactly the
release's seven assets (publish_release.sh's list), each in state "uploaded" with the local file's size and SHA-256: the four archives'
digests come from the signed manifest.json (which publish_release.sh has verified against the archives and the key), the checksum file,
manifest.json and manifest.json.sig are hashed here. An asset whose answer carries no "digest" is downloaded (`$GH api -H "Accept:
application/octet-stream" repos/<repo>/releases/assets/<id>`) and hashed. Prints each problem; exit 0 when there is none.
--missing (r4: a draft is resumed, never deleted): the assets already on the release are checked the same way and the names of the
release's assets that are not there yet are printed, one per line; exit 1 when an asset that is there is wrong or foreign (only the
operator can repair that: nothing is ever deleted).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from make_manifest import KINDS, PLATFORMS, asset_name  # noqa: E402


def expected(d: Path, version: str) -> dict[str, tuple[int, str]]:
    """{asset name: (size, sha256)} of the seven assets"""
    manifest = json.loads((d / "manifest.json").read_bytes())
    out = {}
    for kind in KINDS:
        for platform in PLATFORMS:
            a = manifest["assets"][kind][platform]
            if a["name"] != asset_name(kind, version, platform):
                raise SystemExit(f"release_assets: the manifest names {a['name']} for {kind} {platform}")
            out[a["name"]] = (a["size"], a["sha256"])
    for name in (f"SHA256SUMS-{version}.txt", "manifest.json", "manifest.json.sig"):
        data = (d / name).read_bytes()
        out[name] = (len(data), hashlib.sha256(data).hexdigest())
    return out


def download_digest(repo: str, asset_id: int) -> str:
    gh = os.environ.get("GH", "gh")
    r = subprocess.run([gh, "api", "-H", "Accept: application/octet-stream", f"repos/{repo}/releases/assets/{asset_id}"],
                       capture_output=True, timeout=1800)
    if r.returncode != 0:
        return ""
    return hashlib.sha256(r.stdout).hexdigest()


def check(release: dict, want: dict[str, tuple[int, str]], version: str, draft: bool, prerelease: bool, repo: str,
          partial: bool = False) -> list[str]:
    """partial: assets missing from the release are not problems (missing() names them)"""
    problems = []
    if release.get("tag_name") != version:
        problems.append(f"the release's tag is {release.get('tag_name')!r}, not {version}")
    if release.get("draft") is not draft:
        problems.append(f"the release is {'a draft' if release.get('draft') else 'published'} (expected {'a draft' if draft else 'published'})")
    if release.get("prerelease") is not prerelease:
        problems.append(f"the release's pre-release flag is {release.get('prerelease')!r}, expected {prerelease}")
    assets = release.get("assets") or []
    names = [a.get("name") for a in assets]
    for n in sorted(set(names) - set(want)):
        problems.append(f"an asset that is not the release's: {n}")
    if not partial:
        for n in sorted(set(want) - set(names)):
            problems.append(f"asset missing: {n}")
    if len(names) != len(set(names)):
        problems.append("an asset name appears twice")
    for a in assets:
        n = a.get("name")
        if n not in want:
            continue
        size, digest = want[n]
        if a.get("state") != "uploaded":
            problems.append(f"{n}: state {a.get('state')!r}, not uploaded")
        if a.get("size") != size:
            problems.append(f"{n}: {a.get('size')} bytes on GitHub, {size} here")
        got = a.get("digest") or ""
        if got:
            if got != f"sha256:{digest}":
                problems.append(f"{n}: digest {got} on GitHub, sha256:{digest} here")
        elif download_digest(repo, a.get("id")) != digest:
            problems.append(f"{n}: the downloaded asset's SHA-256 is not {digest}")
    return problems


def missing(release: dict, want: dict[str, tuple[int, str]]) -> list[str]:
    have = {a.get("name") for a in release.get("assets") or []}
    return [n for n in want if n not in have]


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--release-json", type=Path, required=True)
    ap.add_argument("--dir", type=Path, required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("--draft", choices=("true", "false"), required=True)
    ap.add_argument("--prerelease", choices=("true", "false"), required=True)
    ap.add_argument("--repo", default="Open-BFME/openbfme-godot")
    ap.add_argument("--missing", action="store_true", help="check the assets that are there, print the names of those that are not")
    a = ap.parse_args(argv)
    try:
        release = json.loads(a.release_json.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        print(f"RELEASE CHECK: cannot read the release ({e})")
        return 1
    want = expected(a.dir, a.version)
    problems = check(release, want, a.version, a.draft == "true", a.prerelease == "true", a.repo, partial=a.missing)
    if a.missing:
        for p in problems:
            print(f"RELEASE CHECK: {p}", file=sys.stderr)
        for n in missing(release, want):
            print(n)
        return 1 if problems else 0
    for p in problems:
        print(f"RELEASE CHECK: {p}")
    if not problems:
        print(f"release ok: {a.version} id {release.get('id')}, {len(release.get('assets') or [])} assets match")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
