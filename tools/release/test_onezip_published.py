"""Lane ONEZIP-1: the launchers already in testers' hands update to a release in the one-download layout, with no action of the player.

Not rebuilt from their source: the PUBLISHED builds, the launcher archives of the GitHub releases as testers downloaded them
(OPENBFME_PUBLISHED_LAUNCHERS=<folder>/<tag>/openbfme-launcher-<tag>-linux-x64.tar.gz; `gh release download <tag> -R
Open-BFME/openbfme-godot -p 'openbfme-launcher-*'`; default workspace/onezip1/published, git-ignored). A published launcher trusts
only the real release key and talks only to GitHub, so two values of the build information in its embedded pack are changed for the
test, in place and of the same length (patch_published): the release key becomes the test's throwaway key, and "test_build" becomes
true, which lets the launcher take --api-base for the local stand-in of GitHub. Every byte of program code and scripts is the
published one. The release it updates to is made by this tree: the combined package (the game with this tree's launcher, exported
here) as the "game" asset, the launcher archive beside it, a format 2 manifest (make_manifest.py).

Each published launcher: installs the combined package as its game and plays it; stages the new launcher from the launcher archive;
at its next start swaps itself for the new launcher (its own helper protocol, with the new launcher as the helper); the new launcher
then plays the installed version and, from the next release on, takes its own update out of the combined package.
SKIPS LOUDLY without the published archives, Godot 4.7.2 or the Linux export templates.
"""
from __future__ import annotations

import hashlib
import io
import os
import sys
import tarfile
import time
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import ed25519  # noqa: E402
import make_archive  # noqa: E402
import make_manifest  # noqa: E402
import test_launcher  # noqa: E402, F401
from onezip_windows_test import patch_published  # noqa: E402  (the two in-place changes of a published build's build information)
from launcher_test_server import COMMIT, TEST_REPO, tiny_package  # noqa: E402
from test_launcher import _godot_ok, _remove_tmp_path, _templates_ok, build_launcher, data, imported, installed, keys, launch, played, server  # noqa: E402, F401

PUBLISHED_DIR = Path(os.environ.get("OPENBFME_PUBLISHED_LAUNCHERS", REPO / "workspace/onezip1/published"))
# every launcher published so far (v0.3.0-preview.5 was never published); the release each one can read as newer than itself:
# the launchers up to v0.3.0-preview.4 know no -alpha tag, so their next release is a preview bridge (autorelease.py next_bridge)
PUBLISHED = {"v0.3.0-preview.1": "v0.3.0-preview.9", "v0.3.0-preview.2": "v0.3.0-preview.9", "v0.3.0-preview.3": "v0.3.0-preview.9",
             "v0.3.0-preview.4": "v0.3.0-preview.9", "v0.3.0-preview.6": "v0.4.0-alpha.9", "v0.4.0-alpha.1": "v0.4.0-alpha.9",
             "v0.4.0-alpha.2": "v0.4.0-alpha.9", "v0.4.0-alpha.3": "v0.4.0-alpha.9"}
LEXE = "OpenBFMELauncher.x86_64"

pytestmark = pytest.mark.skipif(not _godot_ok(), reason="SKIP the published launchers' update test: Godot 4.7.2 not found (set GODOT)")


def published_launcher(tag: str, into: Path, test_key: bytes) -> Path:
    archive = PUBLISHED_DIR / tag / f"openbfme-launcher-{tag}-linux-x64.tar.gz"
    if not archive.is_file():
        pytest.skip(f"SKIP the published launcher {tag}: {archive.name} is not in the published-launchers folder "
                    "(OPENBFME_PUBLISHED_LAUNCHERS; gh release download)")
    with tarfile.open(archive, "r:gz") as t:
        t.extractall(into, filter="data")
    top = into / f"openbfme-launcher-{tag}-linux-x64"
    exe = top / LEXE
    exe.write_bytes(patch_published(exe.read_bytes(), test_key))
    exe.chmod(0o755)
    return top


@pytest.fixture(scope="module")
def new_launchers(tmp_path_factory, keys):
    """this tree's launcher, exported as a test build, under each version the published launchers update to: the launcher archive
    (bytes) and its executable"""
    if not _templates_ok():
        pytest.skip("SKIP the published launchers' update test: the Godot 4.7.2 Linux export templates are not installed")
    out = {}
    for version in sorted(set(PUBLISHED.values())):
        d = tmp_path_factory.mktemp("new-" + version)
        archive = build_launcher(d, version, keys["release"])
        with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as t:
            exe = t.extractfile(f"openbfme-launcher-{version}-linux-x64/{LEXE}").read()
        out[version] = (archive, exe)
    return out


def game_script(version: str) -> bytes:
    return (f'#!/bin/sh\necho "played {version} from $(dirname "$0") args: $*" >> "$OPENBFME_TEST_PLAYED"\n').encode()


def one_download_release(tmp: Path, version: str, secret: bytes, launcher_archive: bytes | None, launcher_exe: bytes, fmt: int = 2) -> dict:
    """the release's assets in the one-download layout, made with the release tools (make_archive.py, make_manifest.py)"""
    top = tmp / f"build-{version}" / f"openbfme-{version}-linux-x64"
    top.mkdir(parents=True)
    for name, content in (("OpenBFME.x86_64", game_script(version)), ("OpenBFME.pck", b"pack of " + version.encode()),
                          ("VERSION", f"OpenBFME {version}\n".encode()), (LEXE, launcher_exe)):
        (top / name).write_bytes(content)
    (top / "OpenBFME.x86_64").chmod(0o755)
    (top / LEXE).chmod(0o755)
    d = tmp / f"release-{version}"
    d.mkdir()
    gl, gw = make_manifest.asset_name("game", version, "linux-x64"), make_manifest.asset_name("game", version, "windows-x64")
    make_archive.tar_gz(top, d / gl, 1700000000)
    arch = {gl: (d / gl).read_bytes(), gw: tiny_package(gw, {"OpenBFME.exe": b"windows game", "OpenBFMELauncher.exe": b"windows launcher"})}
    if fmt == 2:
        lw = make_manifest.asset_name("launcher", version, "windows-x64")
        arch[make_manifest.asset_name("launcher", version, "linux-x64")] = launcher_archive
        arch[lw] = tiny_package(lw, {"OpenBFMELauncher.exe": b"windows launcher"})
    sums = "".join(f"{hashlib.sha256(b).hexdigest()}  {n}\n" for n, b in arch.items())
    for n, b in arch.items():
        (d / n).write_bytes(b)
    (d / f"SHA256SUMS-{version}.txt").write_text(sums)
    manifest = make_manifest.dumps(make_manifest.build(d, version, TEST_REPO, COMMIT, time.strftime("%Y-%m-%d", time.gmtime()), fmt))
    return arch | {f"SHA256SUMS-{version}.txt": sums.encode(), "manifest.json": manifest, "manifest.json.sig": ed25519.sign(secret, manifest)}


@pytest.mark.parametrize("old", list(PUBLISHED))
def test_a_published_launcher_updates_to_the_one_download_release(tmp_path, server, keys, new_launchers, old):
    new = PUBLISHED[old]
    archive, new_exe = new_launchers[new]
    top = published_launcher(old, tmp_path / "inst", keys["release"])
    exe = top / LEXE
    r = launch(tmp_path, None, keys["release"], "--version", exe=exe)
    assert r.code == 0 and f"OpenBFME Launcher {old}" in r.out, r      # the published build runs, patched only in its build information
    server.publish(new, one_download_release(tmp_path, new, keys["release"], archive, new_exe))
    # 1. the published launcher installs the combined package as its game, plays it, and stages the new launcher
    r = launch(tmp_path, server, keys["release"], "--channel=preview", "--update", "--self-update", "--play", exe=exe)
    assert r.code == 0 and r.has(f"update installed {new}") and r.has(f"launcher update {new} staged") and r.has(f"play {new}"), r
    assert installed(tmp_path) == [new]
    assert played(tmp_path) == f"played {new} from {data(tmp_path)}/versions/{new}/game args: \n"
    assert (data(tmp_path) / "versions" / new / "game" / LEXE).read_bytes() == new_exe, "the package's launcher is installed with the game"
    # 2. its next start swaps itself for the new launcher (the old launcher's own helper protocol; the new launcher is the helper)
    r = launch(tmp_path, server, keys["release"], "--version", exe=exe)
    assert r.has(f"replaced {old} by {new}") and r.has(f"the new launcher {new} confirmed its start"), r
    assert exe.read_bytes() == new_exe and f"OpenBFME Launcher {new}" in r.out
    # 3. the new launcher goes on with what the old one installed
    (tmp_path / "played.txt").unlink()
    r = launch(tmp_path, server, keys["release"], "--list", "--play", exe=exe)
    assert r.code == 0 and r.has(f"installed {new}") and r.has(f"play {new}"), r
    assert played(tmp_path) == f"played {new} from {data(tmp_path)}/versions/{new}/game args: \n"
    assert not (top / "update").exists() and not (top / (LEXE + ".old")).exists(), "the swap was confirmed and cleaned up"


@pytest.mark.parametrize("old", ["v0.3.0-preview.4", "v0.4.0-alpha.3"])
def test_a_published_launcher_refuses_a_format_3_release_by_name_and_keeps_playing(tmp_path, server, keys, new_launchers, old):
    """stop S-4482: a release without the launcher archives (format 3, the target page) is not readable by a launcher published
    before ONEZIP-1. It says so, installs nothing of it, and still plays what it has: the reason every release carries the
    launcher archives until the owner retires those launchers."""
    new = PUBLISHED[old]
    later = new.rsplit(".", 1)[0] + ".10"
    archive, new_exe = new_launchers[new]
    top = published_launcher(old, tmp_path / "inst", keys["release"])
    exe = top / LEXE
    server.publish(new, one_download_release(tmp_path, new, keys["release"], archive, new_exe))
    assert launch(tmp_path, server, keys["release"], "--channel=preview", "--update", exe=exe).code == 0
    server.publish(later, one_download_release(tmp_path / "later", later, keys["release"], None, b"a later launcher", fmt=3))
    r = launch(tmp_path, server, keys["release"], "--update", "--self-update", "--play", exe=exe)
    assert r.code == 3 and r.has(f"update rejected: release {later} rejected: manifest.json: format 3") and r.has("(this launcher reads format 2)"), r
    assert installed(tmp_path) == [new] and not (top / "update").exists()
    assert played(tmp_path) == f"played {new} from {data(tmp_path)}/versions/{new}/game args: \n"


def test_the_updated_launcher_takes_the_next_release_from_the_one_package_alone(tmp_path, server, keys, new_launchers):
    """after the swap a tester's launcher is this tree's: the release after that needs no launcher archive (format 3 here: there is
    none at all). The game and the launcher's own update both come out of the one package."""
    old, new = "v0.4.0-alpha.3", "v0.4.0-alpha.9"
    archive, new_exe = new_launchers[new]
    top = published_launcher(old, tmp_path / "inst", keys["release"])
    exe = top / LEXE
    server.publish(new, one_download_release(tmp_path, new, keys["release"], archive, new_exe))
    assert launch(tmp_path, server, keys["release"], "--update", "--self-update", exe=exe).code == 0
    r = launch(tmp_path, server, keys["release"], "--version", exe=exe)
    assert exe.read_bytes() == new_exe, r
    later = b"the launcher of v0.4.0-alpha.10 (test bytes)"
    server.publish("v0.4.0-alpha.10", one_download_release(tmp_path, "v0.4.0-alpha.10", keys["release"], None, later, fmt=3))
    r = launch(tmp_path, server, keys["release"], "--update", "--self-update", "--play", exe=exe)
    assert r.code == 0 and r.has("update installed v0.4.0-alpha.10") and r.has("launcher update v0.4.0-alpha.10 staged"), r
    assert (top / "update" / LEXE).read_bytes() == later
    assert [p for p, _ in server.requests if "openbfme-launcher-v0.4.0-alpha.10" in p] == []
    assert "played v0.4.0-alpha.10 from" in played(tmp_path)
