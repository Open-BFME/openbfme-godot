#!/usr/bin/env python3
"""Create an offline format-3 macOS sidecar manifest from an audited package ZIP.

    python3 tools/release/make_macos_manifest.py --version v1.2.3 --commit <40-hex> \
      --date YYYY-MM-DD --archive openbfme-v1.2.3-macos-arm64.zip \
      --checksum openbfme-v1.2.3-macos-arm64.zip.sha256
    python3 tools/release/make_macos_manifest.py --version v1.2.3 --commit <40-hex> \
      --date YYYY-MM-DD --no-entry

The sidecar is signed separately with sign_manifest.py on the release operator's
offline signing machine. This tool never opens or generates a private key.

MACREL-1 (tools/release/autorelease.sh publishes the Mac package next to the Windows / Linux ones):

    python3 tools/release/make_macos_manifest.py --verify-package <zip> --checksum <zip>.sha256 \
      --version v1.2.3 --commit <40-hex> --repo OWNER/NAME --release-key-file launcher/release_key.pub
        the package the Mac build host returned, before it is published: the strict macos-arm64 audit, the checksum
        sidecar, VERSION naming this version and commit, and the Mac launcher's build_info.json updating from this
        repository with this release key (a Mac launcher with another key or repository would refuse every update)
    python3 tools/release/make_macos_manifest.py --check <package dir> --release-key-file launcher/release_key.pub
        publish_release.sh: <dir>/manifest-macos.json is exactly this tool's manifest of <dir>'s Mac package (or the
        no-entry manifest when the folder holds none), names manifest.json's version, repository, commit and date,
        SHA256SUMS-<v>.txt lists the Mac package with its digest, and the package is sealed (MACPLAY-1: its signed
        payload.json, seal_package.py)

SIGNKMS-1: --release-key-file may be the trusted key list (launcher/release_keys.pub) or a one-key file (launcher/release_key.pub
stands for the release_keys.pub beside it, whose first entry it must be). The Mac launcher's
build_info.json must carry "release_keys" equal to the list (in file order) and "release_key" equal to its first entry (the legacy key).
--seal-key-file may be a list too: payload.json.sig is valid under any of its keys, raw or envelope (release_keys.py).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import audit_package  # noqa: E402
import make_manifest  # noqa: E402
import release_keys  # noqa: E402

DEFAULT_REPO = make_manifest.DEFAULT_REPO
MACOS_ASSET = re.compile(r"^openbfme-(v[0-9]+\.[0-9]+\.[0-9]+(?:-(?:alpha|nightly|preview)\.[0-9]+)?)-macos-arm64\.zip$")


def fail(message: str) -> None:
    raise SystemExit(f"make_macos_manifest: {message}")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def build(
    version: str,
    repo: str,
    commit: str,
    date: str,
    *,
    archive: Path | None = None,
    checksum: Path | None = None,
    no_entry: bool = False,
) -> dict:
    channel = make_manifest.channel_of(version)
    if not re.fullmatch(r"[A-Za-z0-9-]{1,39}/[A-Za-z0-9._-]{1,100}", repo):
        fail(f"{repo!r} is not <owner>/<name>")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        fail(f"commit {commit!r} is not a 40-digit hex id")
    if not re.fullmatch(r"20[0-9]{2}-(0[1-9]|1[0-2])-(0[1-9]|[12][0-9]|3[01])", date):
        fail(f"date {date!r} is not YYYY-MM-DD")
    if no_entry:
        if archive is not None or checksum is not None:
            fail("--no-entry cannot be combined with --archive or --checksum")
        game = {}
    else:
        if archive is None or checksum is None:
            fail("--archive and --checksum are both required unless --no-entry is used")
        expected_name = f"openbfme-{version}-macos-arm64.zip"
        if archive.name != expected_name or MACOS_ASSET.fullmatch(archive.name) is None:
            fail(f"archive must be named {expected_name}")
        if not archive.is_file() or not checksum.is_file():
            fail("the package archive and checksum sidecar must exist")
        digest = sha256_file(archive)
        expected_checksum = f"{digest}  {archive.name}\n".encode("ascii")
        if checksum.read_bytes() != expected_checksum:
            fail(f"{checksum.name} does not exactly match {archive.name}")
        problems = audit_package.check_release("macos-arm64", archive)
        if problems:
            fail("package ZIP failed the strict macOS arm64 audit: " + "; ".join(problems))
        top, files, problems = audit_package.read_release_zip(archive.read_bytes(), archive.name, nested=True)
        if problems or top != archive.name.removesuffix(".zip"):
            fail("cannot enumerate audited package inventory: " + "; ".join(problems or [f"unexpected top folder {top!r}"]))
        game = {
            "macos-arm64": {
                "name": archive.name,
                "size": archive.stat().st_size,
                "sha256": digest,
                "files": {
                    name: {"size": len(content), "sha256": hashlib.sha256(content).hexdigest()}
                    for name, content in sorted(files.items())
                },
            }
        }
    return {
        "format": 3,
        "product": "OpenBFME",
        "repo": repo,
        "version": version,
        "channel": channel,
        "date": date,
        "commit": commit,
        "assets": {"game": game},
    }


DISPLAY_SUFFIX = "Mac Unstable Preview (Apple Silicon)"  # package_macos.py's VERSION / display version suffix
LAUNCHER_PCK = "OpenBFME Launcher.app/Contents/Resources/OpenBFME Launcher.pck"


def dumps(manifest: dict) -> bytes:
    return (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8")


def read_key(path: Path) -> str:
    key = path.read_text(encoding="ascii").strip()
    if not re.fullmatch(r"[0-9a-f]{64}", key):
        fail(f"{path.name} does not hold a 64-digit Ed25519 public key")
    return key


def read_keys(path: Path, sibling_list: bool = False) -> list[str]:
    """SIGNKMS-1: the trusted keys (64 hex each, in file order) of a key list or a one-key file. sibling_list: a one-key file with a
    release_keys.pub beside it (launcher/release_key.pub) stands for that list, whose first entry must be it"""
    try:
        keys = release_keys.load_any(path)
        side = Path(path).with_name("release_keys.pub")
        if sibling_list and len(keys) == 1 and Path(path).name != side.name and side.is_file():
            keys = release_keys.load(side, legacy=path)
        return [k.hex() for _, k in keys]
    except (release_keys.KeyListError, OSError, ValueError) as e:
        fail(f"{path.name}: {e}")


def _hexes(keys) -> list[str]:
    """one key (64 hex) or a list of them"""
    return [keys] if isinstance(keys, str) else list(keys)


def package_problems(archive: Path, checksum: Path, version: str, commit: str, repo: str, release_key, nightly_keys=None) -> list[str]:
    """MACREL-1: what is wrong with the Mac package the build host returned (empty: nothing). release_key: the trusted keys (64 hex
    each, the list's order; one key alone is a list of one): build_info.json's release_keys must be them, its release_key the first.
    RELGH-2: nightly_keys (64 hex each, possibly none; None: not checked): build_info.json's nightly_keys must be exactly them"""
    keys = _hexes(release_key)
    expected_name = f"openbfme-{version}-macos-arm64.zip"
    if archive.name != expected_name:
        return [f"the Mac package is {archive.name}, not {expected_name}"]
    if not archive.is_file() or not checksum.is_file():
        return [f"{archive.name} or its checksum sidecar is missing"]
    if checksum.read_bytes() != f"{sha256_file(archive)}  {archive.name}\n".encode("ascii"):
        return [f"{checksum.name} does not exactly match {archive.name}"]
    problems = audit_package.check_release("macos-arm64", archive)
    if problems:
        return problems
    top, files, problems = audit_package.read_release_zip(archive.read_bytes(), archive.name, nested=True)
    if problems or top != archive.name.removesuffix(".zip"):
        return problems or [f"{archive.name}: unexpected top folder {top!r}"]
    lines = files["VERSION"].decode("utf-8", "replace").splitlines()
    if lines[:2] != [f"OpenBFME {version} {DISPLAY_SUFFIX}", f"commit {commit}"]:
        problems.append(f"{archive.name}: VERSION says {lines[:2]!r}, not {version} at {commit}")
    try:
        entries = dict(audit_package.pck_entries(files[LAUNCHER_PCK]))
        info = json.loads(entries["build_info.json"])
    except (KeyError, ValueError) as e:
        return problems + [f"{archive.name}: the Mac launcher's build_info.json cannot be read ({e})"]
    want = {"repo": repo, "release_key": keys[0], "release_keys": keys, "version": version, "macos_updates": True,
            "macos_preview": False, "test_build": False, "local_trial": False}
    if nightly_keys is not None:
        want["nightly_keys"] = list(nightly_keys)
    for key, value in want.items():
        if info.get(key) != value:
            problems.append(f"{archive.name}: the Mac launcher's build_info.json has {key} {info.get(key)!r}, not {value!r}")
    return problems


def seal_problems(archive: Path, repo: str, release_key, nightly_keys=()) -> list[str]:
    """MACPLAY-1: the published Mac package carries its signed payload inventory (seal_package.py, autorelease.sh step 6), signed by the
    release key and naming exactly the ZIP's other files: its launcher offers the game beside it only then"""
    import seal_package
    m = seal_package.NAME.match(archive.name)
    if not m:
        return [f"{archive.name} is not a package name seal_package.py knows"]
    files, _, _ = seal_package.read_archive(archive)
    body, sig = files.get(seal_package.PAYLOAD), files.get(seal_package.PAYLOAD_SIG)
    if body is None or sig is None:
        return [f"{archive.name} is not sealed (no {seal_package.PAYLOAD} / {seal_package.PAYLOAD_SIG}): its launcher would not start the game beside it"]
    trusted = [("key", bytes.fromhex(k)) for k in _hexes(release_key)]
    nightly = [("nightly", bytes.fromhex(k)) for k in nightly_keys or ()]
    if release_keys.verify_scoped(trusted, nightly, seal_package.PAYLOAD, body, sig) is None:
        return [f"{archive.name}: the signature of {seal_package.PAYLOAD} does not verify under the release key"]
    if body != seal_package.payload_bytes(files, m.group(1), m.group(2), repo):
        return [f"{archive.name}: {seal_package.PAYLOAD} is not the inventory of the archive's files"]
    return []


def check_dir(d: Path, release_key, seal_key=None, nightly_keys=None) -> list[str]:
    """MACREL-1: publish_release.sh's check of manifest-macos.json against the package folder"""
    try:
        sidecar = (d / "manifest-macos.json").read_bytes()
        mac = json.loads(sidecar)
        main_manifest = json.loads((d / "manifest.json").read_bytes())
    except (OSError, ValueError) as e:
        return [f"cannot read manifest-macos.json / manifest.json ({e})"]
    fields = ("version", "repo", "commit", "date")
    problems = [f"manifest-macos.json has {f} {mac.get(f)!r}, manifest.json {main_manifest.get(f)!r}"
                for f in fields if mac.get(f) != main_manifest.get(f)]
    if problems:
        return problems
    version = main_manifest["version"]
    name = f"openbfme-{version}-macos-arm64.zip"
    archive = d / name
    entry = (mac.get("assets") or {}).get("game")
    strays = sorted(p.name for p in d.glob("openbfme-*-macos-*") if p.name != name)
    if strays:
        return [f"{d.name} holds other Mac packages: {', '.join(strays)}"]
    sums = {}
    sums_file = d / f"SHA256SUMS-{version}.txt"
    for line in sums_file.read_text(encoding="utf-8").splitlines() if sums_file.is_file() else []:
        digest, _, listed = line.partition("  ")
        sums[listed] = digest
    if entry == {}:
        if archive.exists() or name in sums:
            return [f"manifest-macos.json names no Mac package, but {name} is in {d.name} or its checksum file"]
        want = build(version, mac.get("repo", ""), mac.get("commit", ""), mac.get("date", ""), no_entry=True)
    else:
        if not archive.is_file():
            return [f"manifest-macos.json names a Mac package, but {name} is not in {d.name}"]
        digest = sha256_file(archive)
        if sums.get(name) != digest:
            return [f"{sums_file.name} does not list {name} with its digest"]
        with tempfile.TemporaryDirectory() as t:
            checksum = Path(t) / f"{name}.sha256"
            checksum.write_text(f"{digest}  {name}\n", encoding="ascii")
            problems = package_problems(archive, checksum, version, mac["commit"], mac["repo"], release_key, nightly_keys)
            problems += seal_problems(archive, mac["repo"], seal_key or release_key, nightly_keys)
            if problems:
                return problems
            want = build(version, mac["repo"], mac["commit"], mac["date"], archive=archive, checksum=checksum)
    if dumps(want) != sidecar:
        return [f"manifest-macos.json is not the manifest of {name if entry != {} else 'a release without a Mac package'}"]
    return []


def main(argv: list[str]) -> int:
    if argv[:1] == ["--verify-package"] or argv[:1] == ["--check"]:
        ap = argparse.ArgumentParser(description=__doc__)
        ap.add_argument("--verify-package", type=Path)
        ap.add_argument("--check", type=Path)
        ap.add_argument("--checksum", type=Path)
        ap.add_argument("--version")
        ap.add_argument("--commit")
        ap.add_argument("--repo", default=DEFAULT_REPO)
        ap.add_argument("--release-key-file", type=Path, required=True)
        ap.add_argument("--seal-key-file", type=Path, help="--check: the key the package's payload.json is signed with (the manifests' "
                        "key; default --release-key-file; a dry run's throwaway key)")
        ap.add_argument("--nightly-key-file", type=Path, help="RELGH-2: the nightly-only key list (launcher/nightly_keys.pub): the Mac "
                        "launcher's build_info.json must carry exactly it, and (--check) a nightly's payload.json may be signed by it")
        a = ap.parse_args(argv)
        key = read_keys(a.release_key_file, sibling_list=True)
        nightly = None
        if a.nightly_key_file:
            try:
                nightly = [k.hex() for _, k in release_keys.load_nightly(a.nightly_key_file, None)]
            except release_keys.KeyListError as e:
                fail(f"{a.nightly_key_file.name}: {e}")
        if a.check:
            problems = check_dir(a.check, key, read_keys(a.seal_key_file) if a.seal_key_file else None, nightly)
            ok = f"manifest-macos.json ok: matches {a.check.name}"
        else:
            if not (a.checksum and a.version and a.commit):
                fail("--verify-package needs --checksum, --version and --commit")
            problems = package_problems(a.verify_package, a.checksum, a.version, a.commit, a.repo, key, nightly)
            ok = f"MAC PACKAGE ok: {a.verify_package.name} ({a.version} at {a.commit[:10]})"
        for p in problems:
            print(f"MAC PACKAGE CHECK: {p}")
        if not problems:
            print(ok)
        return 1 if problems else 0
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--repo", default=DEFAULT_REPO)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--date", required=True)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--archive", type=Path)
    group.add_argument("--no-entry", action="store_true")
    parser.add_argument("--checksum", type=Path)
    parser.add_argument("--out", type=Path, default=Path("manifest-macos.json"))
    args = parser.parse_args(argv)
    manifest = build(args.version, args.repo, args.commit, args.date, archive=args.archive,
                     checksum=args.checksum, no_entry=args.no_entry)
    args.out.write_bytes(dumps(manifest))
    print(f"MACOS MANIFEST {args.out} ({args.version}, {'no Mac asset' if args.no_entry else args.archive.name})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
