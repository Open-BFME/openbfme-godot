#!/usr/bin/env python3
"""Lane AUTOREL-1 r2: read a GitHub release back and check it against the package folder before (and after) it is published
(publish_release.sh --execute).

    python3 tools/release/release_assets.py --release-json FILE --dir <package dir> --version TAG --draft true|false
        --prerelease true|false [--repo OWNER/NAME]

FILE is `gh api repos/<repo>/releases/<id>`'s answer. The release must have the tag, the draft and pre-release flags given, and exactly the
release's assets (publish_release.sh's list: the archives the manifest names, four in format 2, two in format 3, lane ONEZIP-1; the
checksum file, manifest.json and its signature (SIGNKMS-1: manifest.json.sig and/or manifest.json.sig2, the ones in the folder); MACREL-1: with manifest-macos.json in the folder also the Mac package it names, the
sidecar and its signature), each in state "uploaded" with the local file's size and SHA-256: the archives'
digests come from the signed manifest.json (which publish_release.sh has verified against the archives and the key), the checksum file,
manifest.json and manifest.json.sig are hashed here. An asset whose answer carries no "digest" is downloaded (`$GH api -H "Accept:
application/octet-stream" repos/<repo>/releases/assets/<id>`) and hashed. Prints each problem; exit 0 when there is none.
--missing (r4: a draft is resumed, never deleted): the assets already on the release are checked the same way and the names of the
release's assets that are not there yet are printed, one per line; exit 1 when an asset that is there is wrong or foreign (only the
operator can repair that: nothing is ever deleted).
--name NAME --body-file FILE (PUBLISH-2 r3, Sol r2 P2: a resumed draft kept the name and body of the run that created it, which nobody
had audited): the release's name and body must be exactly the audited ones (the body compared with its line ends as LF, trailing white space aside); --metadata
prints `same` or `differs` for them alone (publish_release.sh then replaces them and checks again).
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
from make_manifest import PLATFORMS, asset_name, kinds_of  # noqa: E402


def expected(d: Path, version: str) -> dict[str, tuple[int, str]]:
    """{asset name: (size, sha256)} of the release's assets: the manifest's archives, the checksum file, the manifest, its signature"""
    manifest = json.loads((d / "manifest.json").read_bytes())
    out = {}
    for kind in kinds_of(manifest.get("format")):
        for platform in PLATFORMS:
            a = manifest["assets"][kind][platform]
            if a["name"] != asset_name(kind, version, platform):
                raise SystemExit(f"release_assets: the manifest names {a['name']} for {kind} {platform}")
            out[a["name"]] = (a["size"], a["sha256"])
    # SIGNKMS-1: a manifest's signature slots are the ones in the folder: X.sig (every launcher reads it) and X.sig2 (the KMS envelope
    # signature, new launchers only); publish_release.sh requires at least one and checks each
    names = [f"SHA256SUMS-{version}.txt", "manifest.json"] + sig_slots(d, "manifest.json")
    # MACREL-1: the separately signed Mac manifest and the Mac package it names (none: a release made with OPENBFME_RELEASE_SKIP_MAC=1
    # carries the no-entry manifest-macos.json); the archive's digest comes from that signed sidecar, as the others' from manifest.json
    if any((d / n).exists() for n in ("manifest-macos.json", "manifest-macos.json.sig", "manifest-macos.json.sig2")):
        mac = json.loads((d / "manifest-macos.json").read_bytes())
        for platform, a in ((mac.get("assets") or {}).get("game") or {}).items():
            if platform != "macos-arm64" or a.get("name") != f"openbfme-{version}-macos-arm64.zip":
                raise SystemExit(f"release_assets: manifest-macos.json names {a.get('name')!r} for {platform}")
            out[a["name"]] = (a["size"], a["sha256"])
        names += ["manifest-macos.json"] + sig_slots(d, "manifest-macos.json")
    for name in names:
        data = (d / name).read_bytes()
        out[name] = (len(data), hashlib.sha256(data).hexdigest())
    return out


def sig_slots(d: Path, name: str) -> list[str]:
    """the signature files of `name` in the folder: name.sig (also when missing: the release's classic slot, so a folder without any
    signature still names it and fails), then name.sig2 when present"""
    return [f"{name}.sig"] * ((d / f"{name}.sig").exists() or not (d / f"{name}.sig2").exists()) + \
        [f"{name}.sig2"] * (d / f"{name}.sig2").exists()


def download_digest(repo: str, asset_id: int) -> str:
    gh = os.environ.get("GH", "gh")
    r = subprocess.run([gh, "api", "-H", "Accept: application/octet-stream", f"repos/{repo}/releases/assets/{asset_id}"],
                       capture_output=True, timeout=1800)
    if r.returncode != 0:
        return ""
    return hashlib.sha256(r.stdout).hexdigest()


def metadata_problems(release: dict, name: str | None, body: str | None) -> list[str]:
    """the release's name and body against the audited ones (None: not checked). Nothing of either is printed: they may be private."""
    problems = []
    if name is not None and release.get("name") != name:
        problems.append("the release's name is not the audited name")
    if body is not None and (release.get("body") or "").replace("\r\n", "\n").rstrip() != body.replace("\r\n", "\n").rstrip():
        problems.append("the release's body is not the audited notes")
    return problems


def check(release: dict, want: dict[str, tuple[int, str]], version: str, draft: bool, prerelease: bool, repo: str,
          partial: bool = False, name: str | None = None, body: str | None = None) -> list[str]:
    """partial: assets missing from the release are not problems (missing() names them)"""
    problems = metadata_problems(release, name, body)
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
    ap.add_argument("--release-json", type=Path)
    ap.add_argument("--names", action="store_true", help="print the release's asset names (publish_release.sh's upload list), one per line")
    ap.add_argument("--dir", type=Path, required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("--draft", choices=("true", "false"), default="false")
    ap.add_argument("--prerelease", choices=("true", "false"), default="false")
    ap.add_argument("--repo", default="Open-BFME/openbfme-godot")
    ap.add_argument("--missing", action="store_true", help="check the assets that are there, print the names of those that are not")
    ap.add_argument("--name", help="the audited release name")
    ap.add_argument("--body-file", type=Path, help="the audited release notes")
    ap.add_argument("--metadata", action="store_true", help="print same or differs for the name and body alone")
    a = ap.parse_args(argv)
    body = a.body_file.read_text(encoding="utf-8") if a.body_file else None
    want = expected(a.dir, a.version)
    if a.names:
        print("\n".join(want))
        return 0
    try:
        release = json.loads(a.release_json.read_text(encoding="utf-8"))
    except (OSError, ValueError, AttributeError) as e:
        print(f"RELEASE CHECK: cannot read the release ({e})")
        return 1
    if a.metadata:
        print("differs" if metadata_problems(release, a.name, body) else "same")
        return 0
    problems = check(release, want, a.version, a.draft == "true", a.prerelease == "true", a.repo, partial=a.missing, name=a.name, body=body)
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
