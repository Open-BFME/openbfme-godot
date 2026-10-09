#!/usr/bin/env python3
"""Lane LAUNCH-1: the release manifest the launcher installs from (docs/RELEASE.md, "The launcher").

    python3 tools/release/make_manifest.py <package dir> [--version TAG] [--repo OWNER/NAME] [--allow-untagged] [--out FILE]
    python3 tools/release/make_manifest.py --check <package dir>     (manifest.json agrees with the archives next to it)

<package dir> is tools/release/package.sh's output folder: the game and launcher archives of both platforms
(openbfme-<v>-{linux-x64.tar.gz,windows-x64.zip}, openbfme-launcher-<v>-{...}) and SHA256SUMS-<v>.txt. Writes <package dir>/manifest.json:

    {"format": 2, "product": "OpenBFME", "repo": ..., "version": <tag>, "channel": "stable" | "preview", "date": <commit date, UTC>,
     "commit": <40 hex>, "assets": {"game" | "launcher": {"linux-x64" | "windows-x64": {"name", "size", "sha256",
     "files": {<path in the package>: {"size", "sha256"}}}}}}      (format 2: every file of every package, round 3)

with sorted keys, two-space indents and a final newline, so the same packages give the same bytes. The version is the folder's (the
SHA256SUMS name); it must be a release tag (v<major>.<minor>.<patch> or v<major>.<minor>.<patch>-preview.<n>) on the checked-out commit (--allow-untagged: tests);
a -preview.<n> tag is the preview channel. Every archive's size and SHA-256 are computed here and must match SHA256SUMS. Sign the result
with tools/release/sign_manifest.py on the offline machine.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import tarfile
import zipfile
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEFAULT_REPO = "Open-BFME/openbfme-godot"
PLATFORMS = ("linux-x64", "windows-x64")
KINDS = {"game": "openbfme", "launcher": "openbfme-launcher"}
_N = r"(?:0|[1-9][0-9]{0,8})"
# the launcher's release grammar (launcher/scripts/core/semver.gd): v<major>.<minor>.<patch>[-preview.<n>], matched as a whole
TAG = re.compile(rf"v({_N})\.({_N})\.({_N})(?:-preview\.({_N}))?")


def asset_name(kind: str, version: str, platform: str) -> str:
    return f"{KINDS[kind]}-{version}-{platform}.{'zip' if platform.startswith('windows') else 'tar.gz'}"


def channel_of(version: str) -> str:
    m = TAG.fullmatch(version)
    if not m or len(version) > 40:
        raise SystemExit(f"make_manifest: {version!r} is not a release version (v<major>.<minor>.<patch> or v<major>.<minor>.<patch>-preview.<n>)")
    return "preview" if m.group(4) else "stable"


def git(*args: str) -> str:
    return subprocess.run(["git", "-C", str(REPO), *args], capture_output=True, text=True, check=True).stdout.strip()


def folder_version(d: Path) -> str:
    sums = sorted(d.glob("SHA256SUMS-*.txt"))
    if len(sums) != 1:
        raise SystemExit(f"make_manifest: {d} holds {len(sums)} SHA256SUMS files (package.sh writes exactly one)")
    return sums[0].name[len("SHA256SUMS-"):-len(".txt")]


def build(d: Path, version: str, repo: str, commit: str, date: str) -> dict:
    channel = channel_of(version)
    if not re.match(r"^[A-Za-z0-9-]{1,39}/[A-Za-z0-9._-]{1,100}$", repo):
        raise SystemExit(f"make_manifest: '{repo}' is not <owner>/<name>")
    sums = {}
    sums_file = d / f"SHA256SUMS-{version}.txt"
    for line in sums_file.read_text(encoding="utf-8").splitlines():
        digest, _, name = line.partition("  ")
        sums[name] = digest
    assets: dict = {}
    for kind in KINDS:
        assets[kind] = {}
        for platform in PLATFORMS:
            name = asset_name(kind, version, platform)
            path = d / name
            if not path.is_file():
                raise SystemExit(f"make_manifest: {name} is missing in {d} (a launcher release carries the game and the launcher for both platforms)")
            data = path.read_bytes()
            digest = hashlib.sha256(data).hexdigest()
            if sums.get(name) != digest:
                raise SystemExit(f"make_manifest: {name} does not match {sums_file.name}")
            assets[kind][platform] = {"name": name, "size": len(data), "sha256": digest, "files": package_files(name, data)}
    return {"format": 2, "product": "OpenBFME", "repo": repo, "version": version, "channel": channel, "date": date, "commit": commit,
            "assets": assets}


def package_files(name: str, data: bytes) -> dict:
    """{path below the package folder: {size, sha256}} of every file in a package archive (make_archive.py's: one top folder named like
    the asset, then files). Signed with the manifest, so the launcher can check unpacked files at any later time (Sol r2: a staged
    launcher and an installed game are checked against these before they run)."""
    top = name.removesuffix(".tar.gz").removesuffix(".zip")
    out = {}
    def add(member: str, content: bytes) -> None:
        if not member.startswith(top + "/") or member.endswith("/") or ".." in member.split("/"):
            raise SystemExit(f"make_manifest: {name}: member {member!r} is not a file of the package folder {top}")
        rel = member[len(top) + 1:]
        if rel in out:
            raise SystemExit(f"make_manifest: {name}: {rel} appears twice")
        out[rel] = {"size": len(content), "sha256": hashlib.sha256(content).hexdigest()}
    try:
        if name.endswith(".zip"):
            with zipfile.ZipFile(io.BytesIO(data)) as z:
                for i in z.infolist():
                    if not i.is_dir():
                        add(i.filename, z.read(i))
        else:
            with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as t:
                for m in t.getmembers():
                    if m.isfile():
                        add(m.name, t.extractfile(m).read())
                    elif not (m.isdir() and m.name.rstrip("/") == top):
                        raise SystemExit(f"make_manifest: {name}: member {m.name!r} is neither the package folder nor a file")
    except (zipfile.BadZipFile, tarfile.TarError, OSError, EOFError) as e:
        raise SystemExit(f"make_manifest: {name} is not a package archive ({e})")
    if not out:
        raise SystemExit(f"make_manifest: {name} holds no files")
    return dict(sorted(out.items()))


def dumps(manifest: dict) -> bytes:
    return (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir", type=Path)
    ap.add_argument("--version")
    ap.add_argument("--repo", default=DEFAULT_REPO)
    ap.add_argument("--commit", help="default: HEAD (tests only)")
    ap.add_argument("--date", help="default: the commit's date, UTC (tests only)")
    ap.add_argument("--allow-untagged", action="store_true", help="tests: the version need not be a tag on the commit")
    ap.add_argument("--out", type=Path)
    ap.add_argument("--check", action="store_true", help="check <dir>/manifest.json against the archives")
    a = ap.parse_args(argv)
    version = a.version or folder_version(a.dir)
    if a.check:
        path = a.out or a.dir / "manifest.json"
        m = json.loads(path.read_bytes())
        want = build(a.dir, version, m.get("repo", ""), m.get("commit", ""), m.get("date", ""))
        if dumps(want) != path.read_bytes():
            print(f"MANIFEST MISMATCH: {path} is not the manifest of the archives in {a.dir}")
            return 1
        print(f"manifest ok: {path.name} matches {len(KINDS) * len(PLATFORMS)} archives of {version}")
        return 0
    commit = a.commit or git("rev-parse", "HEAD")
    if not re.match(r"^[0-9a-f]{40}$", commit):
        raise SystemExit(f"make_manifest: commit '{commit}' is not a 40-digit id")
    if not a.allow_untagged and version not in git("tag", "--points-at", commit).split():
        raise SystemExit(f"make_manifest: {version} is not a tag on {commit[:10]} (tag the release commit first, docs/RELEASE.md)")
    date = a.date or datetime.fromtimestamp(int(git("log", "-1", "--format=%ct", commit)), timezone.utc).strftime("%Y-%m-%d")
    out = a.out or a.dir / "manifest.json"
    out.write_bytes(dumps(build(a.dir, version, a.repo, commit, date)))
    print(f"MANIFEST {out} ({version}, {channel_of(version)})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
