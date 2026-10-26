"""Lane ONEZIP-1: one download with the launcher and the game together; whichever is started keeps the install up to date.

The launcher (a development run of launcher/ on the Godot 4.7.2 editor binary, its own folder named with the test option
--launcher-dir) against test_launcher.py's local release server:
  * the combined package: a release whose "game" package also holds the launcher's executable. The launcher installs it, and takes its
    own update out of it (from the installed copy, or from a download when that version is not installed); the launcher-only archive
    of a format 2 release is never fetched by this launcher; a format 3 release (no launcher archives at all) works the same;
  * the game beside the launcher (the package as unpacked): played with no download, counted as installed, checked against the signed
    file list once an update check has cached it;
  * a game started directly (--game-start=<request folder>, the protocol of launcher/scripts/core/game_start.gd): which version plays,
    the update in the background, every refusal named, the game beside the launcher never written;
  * hostile requests and releases: refused by name, the installed versions unchanged and still startable.
The published launchers updating to such a release: test_onezip_published.py (the real builds).
"""
from __future__ import annotations

import hashlib
import json
import os
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import ed25519  # noqa: E402
import make_archive
import make_manifest
import seal_package
from launcher_test_server import COMMIT, TEST_REPO, tiny_package
import test_launcher
from test_launcher import GODOT, LAUNCHER, _godot_ok, _remove_tmp_path, data, imported, installed, keys, played, server  # noqa: F401
from launcher_test_support import (ARCHIVE_SUFFIX, GAME_EXE, LAUNCHER_EXE, MACOS, MACOS_SKIP, PLATFORM, WINDOWS,
                                   asset_name, deny_write, isolated_environment, native_game, normalized_path)

pytestmark = [pytest.mark.skipif(MACOS, reason=MACOS_SKIP), pytest.mark.skipif(not _godot_ok(), reason="SKIP the one-download launcher tests: Godot 4.7.2 not found (set GODOT)")]

LEXE = LAUNCHER_EXE
VERSION_OPT = "--launcher-version=v0.4.0"   # a development run has no release version, and only a release version updates itself


def launch(tmp, server, key, *args, **kw):
    return test_launcher.launch(tmp, server, key, VERSION_OPT, *args, **kw)


SEAL_KEY = b""


@pytest.fixture(autouse=True)
def _seal_key(keys):
    """every combined package of these tests is sealed with the test's release key (seal_package.py), as a release's is"""
    global SEAL_KEY
    SEAL_KEY = keys["release"]


def game_script(version: str) -> bytes:
    if WINDOWS:
        return native_game("version").read_bytes()
    return linux_game_script(version)


def linux_game_script(version: str) -> bytes:
    return (f'#!/bin/sh\necho "played {version} from $(dirname "$0") args: $*" >> "$OPENBFME_TEST_PLAYED"\n').encode()


def combined_tar(tmp: Path, version: str, launcher: bytes, extra: bytes = b"", seal: bool = True) -> bytes:
    top = tmp / f"build-{version}-{os.urandom(3).hex()}" / f"openbfme-{version}-{PLATFORM}"
    top.mkdir(parents=True)
    if WINDOWS:
        (top / GAME_EXE).write_bytes(native_game("version").read_bytes())
    else:
        (top / GAME_EXE).write_bytes(game_script(version))
        (top / GAME_EXE).chmod(0o755)
    (top / "OpenBFME.pck").write_bytes(b"pack of " + version.encode() + extra)
    (top / "VERSION").write_text(f"OpenBFME {version}\n")
    (top / LEXE).write_bytes(launcher)
    if not WINDOWS:
        (top / LEXE).chmod(0o755)
    out = top.parent / f"openbfme-{version}-{PLATFORM}{ARCHIVE_SUFFIX}"
    (make_archive.zip_file if WINDOWS else make_archive.tar_gz)(top, out, 1700000000)
    if seal:
        seal_package.seal_archive(out, SEAL_KEY, TEST_REPO)
    return out.read_bytes()


def combined_release(tmp: Path, version: str, secret: bytes, fmt: int = 2, launcher: bytes | None = None, game: bytes | None = None,
                     signed_game: bytes | None = None) -> dict[str, bytes]:
    """a release in the one-download layout. `signed_game`: the manifest is made for this game archive while `game` is served (a
    tampered download)"""
    launcher = launcher if launcher is not None else b"the launcher of " + version.encode()
    d = tmp / f"release-{version}-{os.urandom(3).hex()}"
    d.mkdir(parents=True)
    gl, gw = make_manifest.asset_name("game", version, "linux-x64"), make_manifest.asset_name("game", version, "windows-x64")
    ll, lw = make_manifest.asset_name("launcher", version, "linux-x64"), make_manifest.asset_name("launcher", version, "windows-x64")
    served = game if game is not None else combined_tar(tmp, version, launcher)
    linux_files = {"OpenBFME.x86_64": linux_game_script(version), "OpenBFME.pck": b"linux pck",
                   "VERSION": f"OpenBFME {version}\n".encode()}
    if fmt == 3:
        linux_files[make_manifest.LAUNCHER_EXE["linux-x64"]] = launcher
    arch = {gl: tiny_package(gl, linux_files) if WINDOWS else
            (signed_game if signed_game is not None else served),
            gw: (signed_game if signed_game is not None else served) if WINDOWS else
            tiny_package(gw, {"OpenBFME.exe": b"windows game of " + version.encode(), "OpenBFME.pck": b"windows pck",
                              "VERSION": f"OpenBFME {version}\n".encode(), "OpenBFMELauncher.exe": launcher})}
    if fmt == 2:
        arch[ll] = tiny_package(ll, {"OpenBFMELauncher.x86_64": launcher, "README.txt": b"the launcher\n"})
        arch[lw] = tiny_package(lw, {"OpenBFMELauncher.exe": launcher})
    sums = "".join(f"{hashlib.sha256(b).hexdigest()}  {n}\n" for n, b in arch.items())
    for n, b in arch.items():
        (d / n).write_bytes(b)
    (d / f"SHA256SUMS-{version}.txt").write_text(sums)
    manifest = make_manifest.dumps(make_manifest.build(d, version, TEST_REPO, COMMIT, time.strftime("%Y-%m-%d", time.gmtime()), fmt))
    arch[make_manifest.asset_name("game", version, PLATFORM)] = served
    return arch | {f"SHA256SUMS-{version}.txt": sums.encode(), "manifest.json": manifest, "manifest.json.sig": ed25519.sign(secret, manifest)}


@pytest.fixture(scope="module")
def native_update_launcher(tmp_path_factory, keys):
    if not WINDOWS:
        return None
    out = tmp_path_factory.mktemp("onezip-update-launcher")
    archive = test_launcher.build_launcher(out, "v0.6.0", keys["release"])
    return (test_launcher.unpack_launcher(out / "unpacked", archive) / LEXE).read_bytes()


def unpack_beside(tmp: Path, version: str, launcher: bytes = b"this launcher") -> Path:
    """the combined package of `version` as a player unpacked it: the launcher's own folder with the game beside it"""
    import io
    import tarfile
    if WINDOWS:
        import zipfile
        home = tmp / "unpacked"
        with zipfile.ZipFile(io.BytesIO(combined_tar(tmp, version, launcher))) as z:
            z.extractall(home)
        return home / f"openbfme-{version}-windows-x64"
    home = tmp / "unpacked"
    with tarfile.open(fileobj=io.BytesIO(combined_tar(tmp, version, launcher)), mode="r:gz") as t:
        t.extractall(home, filter="data")
    return home / f"openbfme-{version}-{PLATFORM}"


def tampered(archive: bytes) -> bytes:
    """the archive with one byte changed (the same size: only the SHA-256 tells)"""
    b = bytearray(archive)
    b[len(b) // 2] ^= 0xFF
    return bytes(b)


def tree(folder: Path) -> dict[str, str]:
    return {p.relative_to(folder).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(folder.rglob("*")) if p.is_file()}


def asked(server, part: str) -> list[str]:
    return [p for p, _ in server.requests if part in p]


def request(tmp: Path, body: bytes | None = None, state: str = "open") -> Path:
    req = tmp / f"request-{os.urandom(3).hex()}"
    req.mkdir()
    (req / "request.json").write_bytes(body if body is not None else json.dumps({"protocol": 2, "pid": os.getpid()}).encode())
    (req / state).write_bytes(b"")
    return req


def answer(req: Path, name: str) -> dict:
    return json.loads((req / name).read_text(encoding="utf-8")) if (req / name).exists() else {}


# ---- the combined package as a release ---------------------------------------------------------------------------------------------------

@pytest.mark.parametrize("fmt", [2, 3])
def test_the_launcher_updates_the_game_and_itself_from_the_one_package(tmp_path, server, keys, fmt):
    """the install is the combined package, and the launcher's update is that package's launcher executable (checked against the
    signed manifest); the launcher-only archive of a format 2 release is never asked for, a format 3 release has none"""
    new_launcher = b"the launcher of v0.5.0 " + os.urandom(8)
    files = combined_release(tmp_path, "v0.5.0", keys["release"], fmt=fmt, launcher=new_launcher)
    assert (make_manifest.asset_name("launcher", "v0.5.0", PLATFORM) in files) == (fmt == 2)
    server.publish("v0.5.0", files)
    home = tmp_path / "launcher-home"
    home.mkdir()
    r = launch(tmp_path, server, keys["release"], "--update", "--self-update", "--play", f"--launcher-dir={home}")
    assert r.code == 0 and r.has("update installed v0.5.0") and r.has("launcher update v0.5.0 staged"), r
    assert installed(tmp_path) == ["v0.5.0"]
    assert "played v0.5.0 from" in played(tmp_path) and "/versions/v0.5.0/game" in played(tmp_path)
    assert tree(home) == {"update/" + LEXE: hashlib.sha256(new_launcher).hexdigest(),
                          "update/VERSION.launcher": hashlib.sha256(b"v0.5.0\n").hexdigest()}
    assert os.access(home / "update" / LEXE, os.X_OK)
    assert asked(server, "openbfme-launcher-") == [], "this launcher needs no launcher-only archive"
    assert len(asked(server, asset_name("game", "v0.5.0"))) == 1, "one download serves the game and the launcher"


def test_the_launcher_alone_is_staged_from_a_download_of_the_package(tmp_path, server, keys):
    """updating by itself off, or no game installed yet: the launcher's update still comes from the one package (downloaded, unpacked
    beside the downloads, checked file by file against the signed list, then removed again)"""
    new_launcher = b"the launcher of v0.5.0 " + os.urandom(8)
    server.publish("v0.5.0", combined_release(tmp_path, "v0.5.0", keys["release"], launcher=new_launcher))
    home = tmp_path / "launcher-home"
    home.mkdir()
    r = launch(tmp_path, server, keys["release"], "--self-update", f"--launcher-dir={home}")
    assert r.code == 0 and r.has("launcher update v0.5.0 staged"), r
    assert (home / "update" / LEXE).read_bytes() == new_launcher
    assert installed(tmp_path) == [] and list((data(tmp_path) / "downloads").iterdir()) == [], "nothing is left beside the downloads"


def test_a_package_whose_launcher_is_not_the_signed_one_is_not_staged(tmp_path, server, keys):
    """a served package with another launcher executable than the manifest signs: refused by name, nothing staged, nothing installed"""
    good = combined_tar(tmp_path, "v0.5.0", b"the signed launcher")
    evil = tampered(good)
    server.publish("v0.5.0", combined_release(tmp_path, "v0.5.0", keys["release"], launcher=b"the signed launcher", game=evil, signed_game=good))
    home = tmp_path / "launcher-home"
    home.mkdir()
    r = launch(tmp_path, server, keys["release"], "--self-update", "--update", f"--launcher-dir={home}")
    assert r.code == 3 and r.has("update rejected") and r.has("launcher update rejected"), r
    assert r.has(f"{asset_name('game', 'v0.5.0')} does not match the signed manifest (SHA-256 "), r
    assert tree(home) == {} and installed(tmp_path) == []


def test_a_format_3_release_without_a_launcher_in_its_package_is_refused_by_name(tmp_path, server, keys):
    """format 3 has no launcher archive: a package without the launcher's executable has no launcher at all. make_manifest.py refuses
    to write such a manifest; the launcher, given one signed anyway, installs the game and says the release has no launcher"""
    d = tmp_path / "pkg"
    d.mkdir()
    gl, gw = make_manifest.asset_name("game", "v0.5.0", "linux-x64"), make_manifest.asset_name("game", "v0.5.0", "windows-x64")
    arch = {gl: tiny_package(gl, {"OpenBFME.x86_64": linux_game_script("v0.5.0"), "OpenBFME.pck": b"linux pck",
                                  "VERSION": b"OpenBFME v0.5.0\n"}),
            gw: tiny_package(gw, {"OpenBFME.exe": native_game("version").read_bytes() if WINDOWS else b"windows game",
                                  "OpenBFME.pck": b"windows pck", "VERSION": b"OpenBFME v0.5.0\n"})}
    sums = "".join(f"{hashlib.sha256(b).hexdigest()}  {n}\n" for n, b in arch.items())
    for n, b in arch.items():
        (d / n).write_bytes(b)
    (d / "SHA256SUMS-v0.5.0.txt").write_text(sums)
    with pytest.raises(SystemExit, match="holds no OpenBFMELauncher.x86_64: a format 3 release has no other launcher"):
        make_manifest.build(d, "v0.5.0", TEST_REPO, COMMIT, "2026-10-13", 3)
    # written by hand, as a broken release tool would
    from unittest import mock
    with mock.patch.object(make_manifest, "LAUNCHER_EXE", {"linux-x64": "OpenBFME.x86_64", "windows-x64": "OpenBFME.exe"}):
        m = make_manifest.build(d, "v0.5.0", TEST_REPO, COMMIT, time.strftime("%Y-%m-%d", time.gmtime()), 3)
    manifest = make_manifest.dumps(m)
    server.publish("v0.5.0", arch | {"manifest.json": manifest, "manifest.json.sig": ed25519.sign(keys["release"], manifest)})
    home = tmp_path / "launcher-home"
    home.mkdir()
    r = launch(tmp_path, server, keys["release"], "--update", "--self-update", f"--launcher-dir={home}")
    assert r.code == 3 and r.has("update installed v0.5.0"), r
    assert r.has(f"launcher update rejected: the release v0.5.0 has no launcher: {LEXE} is not in its package"), r
    assert tree(home) == {}


@pytest.mark.parametrize("change, said", [
    ({"format": 4}, "format 4 (this launcher reads formats 2 and 3)"),
    ({"format": 3, "assets+": {"launcher": {}}}, "assets: keys"),
    ({"format": 2, "assets-": "launcher"}, "assets: keys"),
])
def test_a_manifest_outside_the_two_formats_is_refused_by_name(tmp_path, server, keys, change, said):
    files = combined_release(tmp_path, "v0.5.0", keys["release"])
    m = json.loads(files["manifest.json"])
    m["format"] = change["format"]
    if "assets+" in change:
        m["assets"].update(change["assets+"])
    if "assets-" in change:
        del m["assets"][change["assets-"]]
    files["manifest.json"] = make_manifest.dumps(m)
    files["manifest.json.sig"] = ed25519.sign(keys["release"], files["manifest.json"])
    server.publish("v0.5.0", files)
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 3 and r.has("update rejected") and r.has(said), r
    assert installed(tmp_path) == [] and asked(server, ARCHIVE_SUFFIX) == []


# ---- the game beside the launcher ---------------------------------------------------------------------------------------------------------

def test_the_unpacked_package_plays_without_any_download(tmp_path, keys):
    """no server, no network, nothing installed: Play starts the game beside the launcher, with the launcher's mark in its environment"""
    home = unpack_beside(tmp_path, "v0.5.0")
    before = tree(home)
    r = launch(tmp_path, None, keys["release"], "--list", "--play", f"--launcher-dir={home}")
    assert r.code == 0 and r.has("installed v0.5.0 (the game beside the launcher)") and r.has("play v0.5.0"), r
    assert r.has("started v0.5.0, the game beside the launcher") and not r.has("without the file check"), r
    suffix = "" if WINDOWS else " "
    assert played(tmp_path) == f"played v0.5.0 from {normalized_path(home)} args:{suffix}\n", played(tmp_path)
    assert installed(tmp_path) == [] and tree(home) == before


def test_its_own_version_is_never_downloaded_and_a_newer_one_is(tmp_path, server, keys):
    home = unpack_beside(tmp_path, "v0.5.0")
    server.publish("v0.5.0", combined_release(tmp_path, "v0.5.0", keys["release"], launcher=b"this launcher"))
    r = launch(tmp_path, server, keys["release"], "--update", f"--launcher-dir={home}")
    assert r.code == 0 and r.has("up to date: v0.5.0 installed, newest preview release v0.5.0") or r.has("up to date: v0.5.0 installed"), r
    assert installed(tmp_path) == [] and asked(server, ARCHIVE_SUFFIX) == [], "the bundled version needs no first download"
    server.publish("v0.6.0", combined_release(tmp_path, "v0.6.0", keys["release"]))
    before = tree(home)
    r = launch(tmp_path, server, keys["release"], "--update", "--play", f"--launcher-dir={home}")
    assert r.code == 0 and r.has("update installed v0.6.0"), r
    assert installed(tmp_path) == ["v0.6.0"] and "played v0.6.0 from" in played(tmp_path)
    assert tree(home) == before, "an update never writes the game beside the launcher"


def proof_for(tmp: Path, home: Path, version: str, launcher: bytes, secret: bytes) -> None:
    """the signed manifest of `version`'s release beside the game, as a confirmed launcher update writes it (Updater.write_launcher_proof)"""
    files = combined_release(tmp, version, secret, launcher=launcher)
    (home / "launcher-manifest.json").write_bytes(files["manifest.json"])
    (home / "launcher-manifest.json.sig").write_bytes(files["manifest.json.sig"])


@pytest.mark.parametrize("change, said", [
    (lambda h, t, k: (h / "OpenBFME.pck").write_bytes(b"something else"), "OpenBFME.pck does not match its signed SHA-256"),
    (lambda h, t, k: (h / "OpenBFME.pck").unlink(), "OpenBFME.pck is missing"),
    (lambda h, t, k: (h / "libinjected.so").write_bytes(b"an unsigned library"), "libinjected.so is not a file of the signed package"),
    (lambda h, t, k: (h / "update").mkdir() or (h / "update" / "other.so").write_bytes(b"x"), "update/other.so is not a file of the signed package"),
    (lambda h, t, k: (h / LEXE).write_bytes(b"another launcher"),
      f"{LEXE} is neither the package's nor a signed launcher update"),
    (lambda h, t, k: proof_for(t, h, "v0.6.0", b"the launcher of v0.6.0", k) or (h / LEXE).write_bytes(b"not that launcher"),
      f"{LEXE} is neither the package's nor a signed launcher update (its SHA-256 is not the signed one)"),
    (lambda h, t, k: proof_for(t, h, "v0.6.0", b"the launcher of v0.6.0", os.urandom(32)) or (h / LEXE).write_bytes(b"the launcher of v0.6.0"),
      f"{LEXE} is neither the package's nor a signed launcher update (the manifest's signature is not valid for the release key"),
])
def test_the_game_beside_the_launcher_is_checked_against_its_signed_inventory(tmp_path, keys, change, said):
    """from the first start, offline: every file of the unpacked package against its signed payload inventory, nothing else in the
    folder but the launcher's own files, the launcher the package's or a signed update of it. A changed folder is refused by name and
    nothing is started (Sol r1 of ONEZIP-1: an unsigned library was accepted, a changed game ran unchecked)"""
    home = unpack_beside(tmp_path, "v0.5.0", b"this launcher")
    change(home, tmp_path, keys["release"])
    r = launch(tmp_path, None, keys["release"], "--play", f"--launcher-dir={home}")
    assert r.code == 1 and r.has("v0.5.0 (the game beside the launcher) is not the signed package (" + said), r
    time.sleep(0.5)
    assert not (tmp_path / "played.txt").exists(), "a changed folder is never started"


def test_a_launcher_update_beside_the_game_is_accepted_by_its_signed_proof(tmp_path, keys):
    home = unpack_beside(tmp_path, "v0.5.0", b"this launcher")
    proof_for(tmp_path, home, "v0.6.0", b"the launcher of v0.6.0", keys["release"])
    (home / LEXE).write_bytes(b"the launcher of v0.6.0")
    (home / (LEXE + ".old")).write_bytes(b"this launcher")   # the previous launcher, kept until the new one started once
    r = launch(tmp_path, None, keys["release"], "--play", f"--launcher-dir={home}")
    assert r.code == 0 and "played v0.5.0" in played(tmp_path), r


@pytest.mark.parametrize("change, said", [
    (lambda h: (h / "payload.json.sig").write_bytes(bytes(64)), "the signature of payload.json is not valid for the release key"),
    (lambda h: (h / "payload.json").write_bytes((h / "payload.json").read_bytes().replace(b"v0.5.0", b"v0.9.0")),
     "the signature of payload.json is not valid for the release key"),
    (lambda h: (h / "payload.json").unlink(), "there is no payload.json (not a signed package)"),
    (lambda h: (h / "payload.json.sig").write_bytes(b"short"), "payload.json.sig is 5 bytes"),
])
def test_a_package_whose_inventory_does_not_verify_is_not_offered(tmp_path, keys, change, said):
    """a missing, corrupt or foreign signature is no permission to run unchecked: the game beside the launcher is not offered at all"""
    home = unpack_beside(tmp_path, "v0.5.0")
    change(home)
    r = launch(tmp_path, None, keys["release"], "--list", "--play", f"--launcher-dir={home}")
    assert r.code == 1 and r.has("the game beside the launcher is not offered: " + said), r
    assert r.has("nothing to play: The game beside the launcher is not the signed package (") and not (tmp_path / "played.txt").exists()


def test_a_folder_without_a_readable_version_is_not_offered(tmp_path, keys):
    home = unpack_beside(tmp_path, "v0.5.0")
    (home / "VERSION").write_text("OpenBFME Launcher v0.5.0\n")
    r = launch(tmp_path, None, keys["release"], "--play", f"--launcher-dir={home}")
    assert r.code == 1 and r.has("the game beside the launcher is not offered: its VERSION file (OpenBFME Launcher v0.5.0) is not the signed version v0.5.0"), r
    assert r.has("nothing to play: The game beside the launcher is not the signed package (") and not (tmp_path / "played.txt").exists()


# ---- a game started directly ----------------------------------------------------------------------------------------------------------------

def game_start(tmp: Path, server, key: bytes, home: Path, req: Path, **kw):
    return launch(tmp, server, key, f"--game-start={req}", f"--launcher-dir={home}", **kw)


def as_the_game(tmp: Path, server, key: bytes, home: Path, req: Path, act: bool = True, env_extra: dict | None = None):
    """the launcher's game start, with this test playing the game's side of protocol 2: on "start <version>" it starts that version
    (a process whose command line names the version's folder, standing for its game), reports the process id in `started` and closes
    the request. (test_launcher.Run of the launcher, the answer, the stand-in game's process or None)"""
    env = test_launcher.isolated_environment(tmp, OPENBFME_TEST_PLAYED=str(tmp / "played.txt"), **(env_extra or {}))
    cmd = [GODOT, "--headless", "--path", str(LAUNCHER), "--", f"--api-base={server.origin if server else 'http://127.0.0.1:9'}",
           f"--trust-key={ed25519.public_key(key).hex()}", f"--repo={TEST_REPO}", VERSION_OPT, f"--game-start={req}", f"--launcher-dir={home}"]
    capture_path = tmp / f"game-start-output-{time.monotonic_ns()}.log" if WINDOWS else None
    capture = capture_path.open("wb") if capture_path else None
    if WINDOWS:
        p = subprocess.Popen(cmd, env=env, stdout=capture, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
    else:
        p = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                             encoding="utf-8", errors="replace", stdin=subprocess.DEVNULL)
    got, game = {}, None
    for _ in range(600):
        if (req / "answer").exists():
            got = json.loads((req / "answer").read_text(encoding="utf-8"))
            break
        if p.poll() is not None:
            break
        time.sleep(0.05)
    if got.get("action") == "start" and act:
        exe = data(tmp) / "versions" / got["version"] / "game" / GAME_EXE
        if WINDOWS:
            env["OPENBFME_TEST_VERSION"] = got["version"]
            env["OPENBFME_TEST_SLEEP"] = "1"
            game = subprocess.Popen([str(exe), "-n", "60", "127.0.0.1"], env=env)
        else:
            game = subprocess.Popen(["/bin/sh", "-c", "sleep 30; :", str(exe)], env=env)
        (req / "started.tmp").write_text(json.dumps({"protocol": 2, "pid": game.pid, "version": got["version"]}),
                                         encoding="utf-8")
        (req / "started.tmp").rename(req / "started")
    if act and (req / "open").exists():
        (req / "open").rename(req / "closed")
    out, _ = p.communicate(timeout=300)
    if capture:
        capture.close()
        out = capture_path.read_text(encoding="utf-8", errors="replace")
    return test_launcher.Run(p.returncode, out), got, game


def test_offline_the_game_goes_on_at_once_and_is_told_why(tmp_path, keys):
    """no network: the answer is "continue" (the game's own folder plays), and the status names why updates were skipped"""
    home = unpack_beside(tmp_path, "v0.5.0")
    req = request(tmp_path)
    t0 = time.time()
    r = game_start(tmp_path, None, keys["release"], home, req, origin="http://127.0.0.1:9")
    assert r.code == 2, r
    assert answer(req, "answer") == {"protocol": 2, "action": "continue", "why": "", "version": ""}
    s = answer(req, "status")
    assert s["final"] is True and s["text"].startswith("Updates skipped: the release list could not be fetched: ") and s["text"].endswith("v0.5.0 starts."), s
    assert (req / "open").exists() and not (req / "claimed").exists() and not (tmp_path / "played.txt").exists()
    assert time.time() - t0 < 30


def test_a_rate_limited_check_is_named(tmp_path, server, keys):
    home = unpack_beside(tmp_path, "v0.5.0")
    server.rate_limited = True
    req = request(tmp_path)
    r = game_start(tmp_path, server, keys["release"], home, req)
    assert r.code == 2 and answer(req, "answer")["action"] == "continue", r
    assert "GitHub's API rate limit for this network is used up" in answer(req, "status")["text"]


def test_a_newer_release_installs_in_the_background_and_starts_from_the_next_start(tmp_path, server, keys, native_update_launcher):
    """the first direct start plays the game's own folder and installs v0.6.0 beside it (never into it); the launcher's update is
    staged; the next direct start is answered "start v0.6.0": the game starts it, reports its process id, and the launcher records it
    as a running game (prune never removes it) under the versions lock it held from the check to the record"""
    home = unpack_beside(tmp_path, "v0.5.0")
    before = tree(home)
    server.publish("v0.5.0", combined_release(tmp_path, "v0.5.0", keys["release"], launcher=b"this launcher"))
    new_launcher = native_update_launcher or b"the launcher of v0.6.0"
    server.publish("v0.6.0", combined_release(tmp_path, "v0.6.0", keys["release"], launcher=new_launcher))
    req = request(tmp_path)
    r = game_start(tmp_path, server, keys["release"], home, req)
    assert r.code == 0, r
    assert answer(req, "answer") == {"protocol": 2, "action": "continue", "why": "", "version": ""} and (req / "open").exists()
    assert answer(req, "status") == {"text": "v0.6.0 is installed. It starts the next time you start OpenBFME.", "final": True}
    assert installed(tmp_path) == ["v0.6.0"] and not (tmp_path / "played.txt").exists()
    after = tree(home)
    assert {k: v for k, v in after.items() if not k.startswith("update/")} == before, "the running game's folder is never written"
    assert after["update/" + LEXE] == hashlib.sha256(new_launcher).hexdigest()
    req = request(tmp_path)
    r, got, game = as_the_game(tmp_path, server, keys["release"], home, req)
    try:
        assert r.code == 0, r
        assert got == {"protocol": 2, "action": "start", "why": "v0.6.0 is installed: it starts instead of v0.5.0", "version": "v0.6.0"}
        assert r.has(f"the game started v0.6.0 (pid {game.pid})"), r
        assert f"{game.pid}" in (data(tmp_path) / "state.cfg").read_text(), "recorded as a running game"
        assert not (data(tmp_path) / "versions.lock").exists(), "the hold ended"
        assert answer(req, "status") == {"text": "Up to date (v0.6.0).", "final": True}
    finally:
        game.kill()
        game.wait(timeout=10)


def test_on_windows_the_launcher_replaces_itself_without_a_window_too(tmp_path, server, keys, native_update_launcher):
    """S-4480 is closed: a game-start run has no window, and on Windows too it begins the swap of a staged launcher (LAUNCH-1 applied it
    only at a start with a window; tools/release/onezip_windows_test.py kills that swap at every step natively and under Wine)"""
    home = unpack_beside(tmp_path, "v0.5.0")
    server.publish("v0.6.0", combined_release(tmp_path, "v0.6.0", keys["release"], launcher=native_update_launcher))
    req = request(tmp_path)
    r = game_start(tmp_path, server, keys["release"], home, req, env_extra={"OPENBFME_TEST_OS_NAME": "Windows"})
    assert r.code == 0 and installed(tmp_path) == ["v0.6.0"], r
    assert r.has("started the update to v0.6.0") and not r.has("applied at the next start with a window"), r


def test_a_game_that_went_on_by_itself_never_gets_a_second_one(tmp_path, server, keys):
    """the game stopped waiting (its deadline passed: the request is closed) and starts nothing: the launcher's late "start" is not
    acted on, nothing is recorded, the versions lock is let go, and the background update goes on"""
    home = unpack_beside(tmp_path, "v0.5.0")
    server.publish("v0.6.0", combined_release(tmp_path, "v0.6.0", keys["release"]))
    assert launch(tmp_path, server, keys["release"], "--update", f"--launcher-dir={home}").code == 0
    req = request(tmp_path, state="closed")
    t0 = time.time()
    r, got, game = as_the_game(tmp_path, server, keys["release"], home, req, act=False)
    assert game is None and got["action"] == "start" and r.code == 0, r
    assert r.has("the game did not report starting v0.6.0") and time.time() - t0 < 30, r
    assert "running" not in (data(tmp_path) / "state.cfg").read_text() and not (data(tmp_path) / "versions.lock").exists()
    assert not (tmp_path / "played.txt").exists()


def test_a_reply_that_is_not_the_protocols_is_refused_by_name(tmp_path, server, keys):
    """the game's `started` reply is checked: another version, a pid that is not that version's game, or not JSON: refused by name,
    nothing recorded, the lock let go"""
    home = unpack_beside(tmp_path, "v0.5.0")
    server.publish("v0.6.0", combined_release(tmp_path, "v0.6.0", keys["release"]))
    assert launch(tmp_path, server, keys["release"], "--update", f"--launcher-dir={home}").code == 0
    for reply, said in (({"protocol": 2, "pid": os.getpid(), "version": "v0.6.0"}, f"process {os.getpid()} is not a running game of v0.6.0"),
                        ({"protocol": 2, "pid": 1, "version": "v0.5.0"}, "the game's `started` reply is refused"),
                        ({"protocol": "2", "pid": "1", "version": 5}, "the game's `started` reply is refused"),
                        # Sol r2: a valid reply of 4096 bytes followed by more is refused as a whole
                        (json.dumps({"protocol": 2, "pid": os.getpid(), "version": "v0.6.0"}).ljust(4096) + "not json",
                         "the game's `started` reply is refused (4104 bytes, at most 4096)"),
                        ("not json", "the game's `started` reply is refused")):
        req = request(tmp_path)
        (req / "started").write_text(json.dumps(reply) if isinstance(reply, dict) else reply)
        r, got, game = as_the_game(tmp_path, server, keys["release"], home, req, act=False)
        assert got["action"] == "start" and r.has(said), r
        assert "running" not in (data(tmp_path) / "state.cfg").read_text() and not (data(tmp_path) / "versions.lock").exists()


def test_an_installed_version_that_was_changed_is_not_started_and_the_game_goes_on(tmp_path, server, keys):
    home = unpack_beside(tmp_path, "v0.5.0")
    server.publish("v0.6.0", combined_release(tmp_path, "v0.6.0", keys["release"]))
    assert launch(tmp_path, server, keys["release"], "--update", f"--launcher-dir={home}").code == 0
    (data(tmp_path) / "versions/v0.6.0/game/OpenBFME.pck").write_bytes(b"changed")
    req = request(tmp_path)
    r = game_start(tmp_path, server, keys["release"], home, req)
    res = answer(req, "answer")
    assert res["action"] == "continue" and res["why"].startswith("v0.6.0 is installed but cannot be started (") and "OpenBFME.pck does not" in res["why"], res
    assert res["why"].endswith("v0.5.0 starts instead") and (req / "open").exists() and not (data(tmp_path) / "versions.lock").exists(), r


def test_the_players_pick_decides_a_direct_start_too(tmp_path, server, keys):
    """the launcher's rule for which version starts is unchanged: the player's pick, else the newest. A pick of the game's own folder
    keeps a direct start in that folder although a newer version is installed"""
    home = unpack_beside(tmp_path, "v0.5.0")
    server.publish("v0.6.0", combined_release(tmp_path, "v0.6.0", keys["release"]))
    assert launch(tmp_path, server, keys["release"], "--update", f"--launcher-dir={home}").code == 0
    r = launch(tmp_path, None, keys["release"], "--select=v0.5.0", f"--launcher-dir={home}")
    assert r.code == 0 and r.has("selected v0.5.0") and installed(tmp_path) == ["v0.6.0"], r
    req = request(tmp_path)
    r = game_start(tmp_path, server, keys["release"], home, req)
    assert answer(req, "answer")["action"] == "continue" and not (tmp_path / "played.txt").exists(), r


def test_a_tampered_update_is_refused_by_name_and_the_game_stays_startable(tmp_path, server, keys):
    """the served package is not the signed one: nothing is installed, the status and the launcher's next window say why, and the next
    direct start still plays the game's own folder"""
    home = unpack_beside(tmp_path, "v0.5.0")
    before = tree(home)
    good = combined_tar(tmp_path, "v0.6.0", b"L")
    evil = tampered(good)
    server.publish("v0.6.0", combined_release(tmp_path, "v0.6.0", keys["release"], launcher=b"L", game=evil, signed_game=good))
    req = request(tmp_path)
    r = game_start(tmp_path, server, keys["release"], home, req)
    s = answer(req, "status")
    assert r.code == 3 and s["final"] and s["text"].startswith(f"Update not installed: {asset_name('game', 'v0.6.0')}"), s
    assert "does not match the signed manifest (SHA-256 " in s["text"] and "nothing was installed. v0.5.0 stays." in s["text"], s
    assert installed(tmp_path) == [] and tree(home) == before
    assert "Update not installed" in (data(tmp_path) / "state.cfg").read_text(), "the launcher's window shows it at its next start"
    req = request(tmp_path)
    game_start(tmp_path, server, keys["release"], home, req)
    assert answer(req, "answer")["action"] == "continue"
    r = launch(tmp_path, None, keys["release"], "--play", f"--launcher-dir={home}")
    assert r.code == 0 and "played v0.5.0" in played(tmp_path), r


def test_updating_by_itself_off_only_says_that_a_newer_version_is_there(tmp_path, server, keys):
    home = unpack_beside(tmp_path, "v0.5.0")
    server.publish("v0.6.0", combined_release(tmp_path, "v0.6.0", keys["release"]))
    assert launch(tmp_path, None, keys["release"], "--auto-update=off").code == 0
    req = request(tmp_path)
    r = game_start(tmp_path, server, keys["release"], home, req)
    assert r.code == 0 and answer(req, "status") == {
        "text": "v0.6.0 is available. Open the OpenBFME Launcher to update (updating by itself is off).", "final": True}, r
    assert installed(tmp_path) == [] and asked(server, ARCHIVE_SUFFIX) == []


def test_a_full_disk_while_answering_ends_the_hand_over_by_name(tmp_path, keys):
    """a real ENOSPC (the answer's temporary file is /dev/full): the launcher says so by name, leaves no answer, and the game, which reads
    none, starts its own version at its deadline"""
    home = unpack_beside(tmp_path, "v0.5.0")
    req = request(tmp_path)
    if WINDOWS:
        (req / "answer.tmp").write_bytes(b"")
        with deny_write(req / "answer.tmp"):
            r = game_start(tmp_path, None, keys["release"], home, req, origin="http://127.0.0.1:9")
    else:
        (req / "answer.tmp").symlink_to("/dev/full")
        r = game_start(tmp_path, None, keys["release"], home, req, origin="http://127.0.0.1:9")
    assert r.code == 1 and r.has("game start: answer could not be written into the game's request folder (is the disk full?)"), r
    assert not (req / "answer").exists() and not (req / "answer.tmp").exists()


def test_a_full_disk_while_downloading_an_update_is_named_and_the_game_stays_playable(tmp_path, server, keys):
    """a real ENOSPC during the background download (the partial file is /dev/full): nothing is installed, the status names the
    failure, and the game beside the launcher still starts"""
    home = unpack_beside(tmp_path, "v0.5.0")
    files = combined_release(tmp_path, "v0.6.0", keys["release"])
    server.publish("v0.6.0", files)
    name = asset_name("game", "v0.6.0")
    dl = data(tmp_path) / "downloads"
    dl.mkdir(parents=True)
    if WINDOWS:
        (dl / (name + ".part")).write_bytes(b"")
        (dl / (name + ".part.json")).write_text(json.dumps({"sha256": hashlib.sha256(files[name]).hexdigest(), "size": len(files[name])},
                                                            separators=(",", ":")))
        with deny_write(dl / (name + ".part")):
            req = request(tmp_path)
            r = game_start(tmp_path, server, keys["release"], home, req)
    else:
        (dl / (name + ".part")).symlink_to("/dev/full")
        (dl / (name + ".part.json")).write_text(json.dumps({"sha256": hashlib.sha256(files[name]).hexdigest(), "size": len(files[name])},
                                                           separators=(",", ":")))
        req = request(tmp_path)
        r = game_start(tmp_path, server, keys["release"], home, req)
    s = answer(req, "status")
    assert r.code == 3 and s["final"] and s["text"].startswith(f"Update not installed: {name}: ") and "v0.5.0 stays." in s["text"], s
    assert installed(tmp_path) == []
    r = launch(tmp_path, None, keys["release"], "--play", f"--launcher-dir={home}")
    assert r.code == 0 and "played v0.5.0" in played(tmp_path), r


def lock_port(tmp: Path) -> int:
    return test_launcher._lock_port(tmp)


def test_with_the_launcher_window_open_the_game_is_answered_and_the_window_updates(tmp_path, server, keys):
    """the launcher lock is held (a window is open): the direct start is answered from the disk, and this run starts no second update"""
    home = unpack_beside(tmp_path, "v0.5.0")
    server.publish("v0.6.0", combined_release(tmp_path, "v0.6.0", keys["release"]))
    data(tmp_path).mkdir(parents=True)
    held = socket.socket()
    held.bind(("127.0.0.1", lock_port(tmp_path)))
    held.listen()

    def hello() -> None:
        try:
            while True:
                c, _ = held.accept()
                c.sendall(b"OPENBFME-LAUNCHER v0.5.0\n")
                c.recv(16)
                c.close()
        except OSError:
            pass
    threading.Thread(target=hello, daemon=True).start()
    try:
        req = request(tmp_path)
        r = game_start(tmp_path, server, keys["release"], home, req)
    finally:
        held.close()
    assert r.code == 0 and answer(req, "answer")["action"] == "continue", r
    assert answer(req, "status") == {"text": "The OpenBFME Launcher is open: it looks after updates.", "final": True}
    assert installed(tmp_path) == [] and asked(server, "/repos/") == []


def test_the_launcher_inside_an_installed_version_never_writes_into_it(tmp_path, server, keys):
    """the combined package is installed with its launcher in it. Started from there, that launcher has no folder of its own: it
    stages no update into the signed version folder (which Play would then refuse as changed) and plays the installed versions"""
    server.publish("v0.5.0", combined_release(tmp_path, "v0.5.0", keys["release"]))
    home = tmp_path / "launcher-home"
    home.mkdir()
    assert launch(tmp_path, server, keys["release"], "--update", f"--launcher-dir={home}").code == 0
    inside = data(tmp_path) / "versions/v0.5.0/game"
    before = tree(inside)
    server.publish("v0.6.0", combined_release(tmp_path, "v0.6.0", keys["release"]))
    r = launch(tmp_path, server, keys["release"], "--update", "--self-update", "--play", f"--launcher-dir={inside}")
    assert r.code == 0 and r.has("this launcher runs out of an installed version's folder") and r.has("update installed v0.6.0"), r
    assert r.has("launcher update: this launcher has no folder of its own (a development run, or one out of an installed version)") and tree(inside) == before, r
    assert "played v0.6.0 from" in played(tmp_path)
    r = launch(tmp_path, None, keys["release"], "--select=v0.5.0", "--play", f"--launcher-dir={inside}")
    assert r.code == 0 and r.has("play v0.5.0"), "the version folder still verifies: " + repr(r)


def test_a_launcher_opened_during_the_background_update_takes_it_over(tmp_path, server, keys):
    """the background run holds the launcher lock while it downloads; a launcher started meanwhile asks it to stop, takes the lock and
    goes on with the same partial download (resumed, not started over)"""
    home = unpack_beside(tmp_path, "v0.5.0")
    big = combined_tar(tmp_path, "v0.6.0", b"L", extra=os.urandom(600000))
    server.publish("v0.6.0", combined_release(tmp_path, "v0.6.0", keys["release"], launcher=b"L", game=big))
    resume = threading.Event()
    server.pause_once[f"/dl/v0.6.0/{asset_name('game', 'v0.6.0')}"] = resume
    req = request(tmp_path)
    env = isolated_environment(tmp_path, OPENBFME_TEST_PLAYED=str(tmp_path / "played.txt"))
    command = [GODOT, "--headless", "--path", str(LAUNCHER), "--", f"--api-base={server.origin}",
               f"--trust-key={ed25519.public_key(keys['release']).hex()}", f"--repo={TEST_REPO}", VERSION_OPT,
               f"--game-start={req}", f"--launcher-dir={home}"]
    log_path = tmp_path / "background-update.log"
    log_file = log_path.open("wb") if WINDOWS else None
    bg = subprocess.Popen(command, env=env, stdout=log_file if WINDOWS else subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=not WINDOWS, encoding="utf-8" if not WINDOWS else None,
                          errors="replace" if not WINDOWS else None, stdin=subprocess.DEVNULL)
    stop_thread = None

    def background_output() -> str:
        if WINDOWS:
            return log_path.read_text(encoding="utf-8", errors="replace")
        return ""

    try:
        part = data(tmp_path) / "downloads" / (asset_name("game", "v0.6.0") + ".part")
        for _ in range(600):
            if part.exists() and part.stat().st_size > 0:
                break
            time.sleep(0.05)
        assert part.exists(), "the background download did not start\n" + background_output()
        stop_file = data(tmp_path) / "game-start.stop"

        def resume_after_takeover_request() -> None:
            while bg.poll() is None and not resume.is_set():
                if stop_file.exists():
                    resume.set()
                    return
                time.sleep(0.01)

        stop_thread = threading.Thread(target=resume_after_takeover_request, daemon=True)
        stop_thread.start()
        r = launch(tmp_path, server, keys["release"], "--update", f"--launcher-dir={home}")
        if WINDOWS:
            bg.wait(timeout=120)
            out = background_output()
        else:
            out, _ = bg.communicate(timeout=120)
    finally:
        resume.set()
        if stop_thread is not None:
            stop_thread.join(timeout=1)
        if bg.poll() is None:
            bg.kill()
            bg.wait(timeout=15)
        if log_file:
            log_file.close()
    assert r.code == 0 and r.has("a background update (started with the game) is asked to stop") and r.has("update installed v0.6.0"), r
    assert r.has(f"resumed {asset_name('game', 'v0.6.0')} at "), r
    assert answer(req, "status") == {"text": "The update goes on in the OpenBFME Launcher.", "final": True}, out
    assert installed(tmp_path) == ["v0.6.0"]


@pytest.mark.parametrize("make, said", [
    (lambda t: "relative/folder", "--game-start does not name an existing folder by its full path"),
    (lambda t: str(t / "missing"), "--game-start does not name an existing folder by its full path"),
    (lambda t: str(request(t)) + "/../" + request(t).name, "--game-start does not name an existing folder by its full path"),
    (lambda t: str((t / "empty").mkdir() or t / "empty"), "the request folder has no request.json"),
    (lambda t: str(request(t, body=b"[" + b"1," * 40000 + b"1]")), "the request folder has no request.json (or one above 65536 bytes)"),
    (lambda t: str(request(t, body=b"[1, 2]")), "request.json is not a JSON object"),
    (lambda t: str(request(t, body=b'{"protocol": 2, "pid": 1, "args": ["--script", "x.gd"]}')), "request.json has the keys"),
    (lambda t: str(request(t, body=b'{"protocol": 1, "pid": 1}')), "request.json speaks protocol 1"),
    (lambda t: str(request(t, body=b'{"protocol": 2.5, "pid": 1}')), "request.json speaks protocol 2.5"),
    (lambda t: str(request(t, body=b'{"protocol": "2", "pid": 1}')), "request.json speaks protocol 2"),
    (lambda t: str(request(t, body=b'{"protocol": 2, "pid": -4}')), "request.json's pid is not a process id"),
    (lambda t: str(request(t, body=b'{"protocol": 2, "pid": 1.5}')), "request.json's pid is not a process id"),
    (lambda t: str(request(t, body=b'{"protocol": 2, "pid": 1e30}')), "request.json's pid is not a process id"),
    (lambda t: str(request(t, state="other")), "the request folder holds neither open nor closed"),
])
def test_a_hostile_request_is_refused_by_name_and_nothing_runs(tmp_path, server, keys, make, said):
    """the request folder comes from the command line: it is checked before the launcher writes into it, starts anything or asks the
    network; the installed version is neither started nor changed"""
    home = unpack_beside(tmp_path, "v0.5.0")
    server.publish("v0.6.0", combined_release(tmp_path, "v0.6.0", keys["release"]))
    assert launch(tmp_path, server, keys["release"], "--update", f"--launcher-dir={home}").code == 0
    server.requests.clear()
    before = tree(data(tmp_path) / "versions")
    target = make(tmp_path)
    folder = Path(target)
    listing = sorted(p.name for p in folder.iterdir()) if folder.is_dir() else None
    r = launch(tmp_path, server, keys["release"], f"--game-start={target}", f"--launcher-dir={home}")
    assert r.code == 1 and r.has("game start refused, nothing done: " + said), r
    time.sleep(0.3)
    assert not (tmp_path / "played.txt").exists() and server.requests == []
    assert tree(data(tmp_path) / "versions") == before
    assert listing is None or sorted(p.name for p in folder.iterdir()) == listing, "nothing is written into a refused folder"


def test_a_link_never_reaches_the_game_start_mode(tmp_path, server, keys):
    """ONLINE-2's rule holds for the new option: an openbfme:// link with anything else beside it refuses the whole start"""
    home = unpack_beside(tmp_path, "v0.5.0")
    req = request(tmp_path)
    r = launch(tmp_path, server, keys["release"], "--open-url", "openbfme://join/abc", f"--game-start={req}", f"--launcher-dir={home}")
    assert r.code == 1 and r.has("link refused, nothing done"), r
    assert not (req / "result").exists() and not (tmp_path / "played.txt").exists()


# ---- the game's side ------------------------------------------------------------------------------------------------------------------------

def test_the_games_side_of_the_hand_over(tmp_path, keys):
    """godot/scripts/release/update_handover.gd and launcher_check.gd (godot/tests/update_handover_test.gd): the launcher beside the game
    authenticated before it is started (Sol r1 of ONEZIP-1: a replaced executable or a link ran unchecked), the protocol's answers, the
    deadline, malformed answers refused by name, and the argument boundary: the installed version gets this game's arguments after "--",
    so a player's (or a crafted) --script stays a user argument and never runs a script"""
    import shutil
    k = keys["release"]
    base = unpack_beside(tmp_path / "base", "v0.5.0", b"this launcher")
    folders = {}

    def variant(name, change):
        d = tmp_path / "folders" / name
        shutil.copytree(base, d, symlinks=True)
        change(d)
        folders[name] = d
        return str(d)

    def relink(d):
        (d / LEXE).unlink()
        target = tmp_path / "native-symlink-target.exe"
        if WINDOWS:
            target.write_bytes(native_game("marker").read_bytes())
            (d / LEXE).symlink_to(target)
        else:
            (d / LEXE).symlink_to("/bin/true")

    def foreign_payload(d):
        body = seal_package.payload_bytes({}, "v0.5.0", PLATFORM, "Other/repo")
        (d / "payload.json").write_bytes(body)
        (d / "payload.json.sig").write_bytes(ed25519.sign(k, body))

    import release_keys

    def kms_sealed(d, kind="payload.json"):
        """SIGNKMS-1: the inventory signed the new way (the second trusted key over the envelope of `kind`)"""
        (d / "payload.json.sig").write_bytes(ed25519.sign(keys["kms"], release_keys.envelope(kind, (d / "payload.json").read_bytes())))

    def kms_proof(d):
        proof_for(tmp_path, d, "v0.6.0", b"the launcher of v0.6.0", k)
        body = (d / "launcher-manifest.json").read_bytes()
        (d / "launcher-manifest.json.sig").write_bytes(ed25519.sign(keys["kms"], release_keys.envelope("manifest.json", body)))
        (d / LEXE).write_bytes(b"the launcher of v0.6.0")

    checks = [
        {"name": "the package's launcher", "dir": variant("ok", lambda d: None), "want": ""},
        {"name": "a replaced launcher", "dir": variant("replaced", lambda d: (d / LEXE).write_bytes(
            b"#!/bin/sh\necho evil\n" if not WINDOWS else native_game("marker").read_bytes())),
         "want": f"{LEXE} is not the package's launcher, and there is no signed proof of an update (launcher-manifest.json: missing)"},
    ]
    try:
        link = {"name": "a link", "dir": variant("link", relink), "want": f"{LEXE} is a link, not the signed launcher"}
    except OSError as error:
        if not WINDOWS or getattr(error, "winerror", None) != 1314:
            raise
        print(f"SKIP symlink subcase 'a link': Windows ERROR_PRIVILEGE_NOT_HELD (winerror 1314): {error}")
    else:
        checks.append(link)
    checks.extend([
        {"name": "a signed update", "dir": variant("proof", lambda d: proof_for(tmp_path, d, "v0.6.0", b"the launcher of v0.6.0", k)
                                                   or (d / LEXE).write_bytes(b"the launcher of v0.6.0")), "want": ""},
        {"name": "a foreign proof", "dir": variant("foreign-proof", lambda d: proof_for(tmp_path, d, "v0.6.0", b"the launcher of v0.6.0",
                                                                                         os.urandom(32)) or (d / LEXE).write_bytes(b"the launcher of v0.6.0")),
         "want": "launcher-manifest.json: the signature is not the OpenBFME release's"},
        {"name": "a proof of another launcher", "dir": variant("other-proof", lambda d: proof_for(tmp_path, d, "v0.6.0", b"the launcher of v0.6.0", k)
                                                                  or (d / LEXE).write_bytes(b"something else")),
         "want": "is neither the package's launcher nor the launcher of the signed release in launcher-manifest.json"},
        {"name": "no inventory", "dir": variant("no-payload", lambda d: (d / "payload.json").unlink()), "want": "the package's payload.json: missing"},
        {"name": "a broken signature", "dir": variant("bad-sig", lambda d: (d / "payload.json.sig").write_bytes(bytes(64))),
         "want": "the package's payload.json: the signature is not the OpenBFME release's"},
        {"name": "another repository's inventory", "dir": variant("other-repo", foreign_payload), "want": "is not this platform's package of Test/repo"},
        {"name": "a 32 MiB signature", "dir": variant("huge-sig", lambda d: os.truncate(d / "payload.json.sig", 32 << 20)),
         "want": "the package's payload.json: no signature (payload.json.sig is not 64 bytes)"},
        {"name": "a 4 MiB inventory", "dir": variant("huge-payload", lambda d: os.truncate(d / "payload.json", 4 << 20)),
         "want": "the package's payload.json: larger than 65536 bytes"},
        # SIGNKMS-1: the trusted list (old key, new key); the new key signs the envelope of the file's kind
        {"name": "an inventory sealed by the new key", "dir": variant("kms-payload", kms_sealed), "want": ""},
        {"name": "an update proven by the new key", "dir": variant("kms-proof", kms_proof), "want": ""},
        {"name": "an inventory signature of the wrong kind", "dir": variant("kms-wrong-kind", lambda d: kms_sealed(d, "manifest.json")),
         "want": "the package's payload.json: the signature is not the OpenBFME release's"},
    ])
    # the launcher's data folder with an installed version whose game writes the arguments it got
    ldir = tmp_path / "launcher-data"
    game = ldir / "versions/v0.6.0/game" / GAME_EXE
    game.parent.mkdir(parents=True)
    argv = tmp_path / "argv.txt"
    if WINDOWS:
        game.write_bytes(native_game("args").read_bytes())
    else:
        game.write_text(f'#!/bin/sh\nprintf "%s\\n" "$@" > "{argv}"\n')
        game.chmod(0o755)
    marker = tmp_path / "unsigned-launcher-ran"
    if WINDOWS:
        (folders["replaced"] / LEXE).write_bytes(native_game("marker").read_bytes())
    else:
        (folders["replaced"] / LEXE).write_text(f'#!/bin/sh\ntouch "{marker}"\n')
        (folders["replaced"] / LEXE).chmod(0o755)
    enospc = tmp_path / "enospc"
    enospc.mkdir()
    if WINDOWS:
        (enospc / "request.json.tmp").write_bytes(b"")
    else:
        (enospc / "request.json.tmp").symlink_to("/dev/full")
    cases = {"enospc_dir": str(enospc), "key": f"{ed25519.public_key(k).hex()},{ed25519.public_key(keys['kms']).hex()}", "repo": TEST_REPO, "check": checks, "launcher_dir": str(ldir), "version": "v0.6.0",
             "requests": str(tmp_path / "requests"), "unsigned_dir": str(folders["replaced"])}
    (tmp_path / "cases.json").write_text(json.dumps(cases))
    env = isolated_environment(tmp_path, OPENBFME_TEST_ARGS=str(argv), OPENBFME_TEST_MARKER=str(marker))
    if WINDOWS:
        with deny_write(enospc / "request.json.tmp"):
            r = subprocess.run([GODOT, "--headless", "--path", str(LAUNCHER.parent / "godot"), "--script",
                                "res://tests/update_handover_test.gd", "--", f"--cases={tmp_path / 'cases.json'}"],
                               env=env, capture_output=True, text=True, encoding="utf-8", errors="replace",
                               timeout=300, stdin=subprocess.DEVNULL)
    else:
        r = subprocess.run([GODOT, "--headless", "--path", str(LAUNCHER.parent / "godot"), "--script",
                            "res://tests/update_handover_test.gd", "--", f"--cases={tmp_path / 'cases.json'}"],
                           env=env, capture_output=True, text=True, encoding="utf-8", errors="replace",
                           timeout=300, stdin=subprocess.DEVNULL)
    out = r.stdout + r.stderr
    assert r.returncode == 0 and "HANDOVER OK" in r.stdout and "HANDOVER FAIL" not in out and "SCRIPT ERROR" not in out, out[-4000:]
    for _ in range(50):
        if argv.exists():
            break
        time.sleep(0.1)
    assert argv.read_text().splitlines() == ["--", "--script", "res://evil.gd", "--fps"], "the game's arguments come after --"
    time.sleep(0.5)
    assert not marker.exists(), "an unsigned launcher is never started"


def test_the_game_shares_the_launchers_signature_code_byte_for_byte():
    """one implementation: the game's copy of the Ed25519 / SHA-512 code and of the release signature rule (SIGNKMS-1) is the launcher's"""
    for f in ("ed25519.gd", "sha512.gd", "release_signature.gd"):
        assert (LAUNCHER.parent / "godot/scripts/crypto" / f).read_bytes() == (LAUNCHER / "scripts/crypto" / f).read_bytes(), f


def test_a_signed_file_larger_than_its_limit_is_refused_before_it_is_read(tmp_path):
    """Sol r2 of ONEZIP-1: a 4 MiB payload.json (limit 64 KiB) and a 32 MiB payload.json.sig are refused by their size, and the
    launcher's peak memory does not grow by the file (launcher/tests/bounded_read_test.gd)"""
    d = tmp_path / "pkg"
    d.mkdir()
    r = subprocess.run([GODOT, "--headless", "--path", str(LAUNCHER), "--script", "res://tests/bounded_read_test.gd", "--", f"--dir={d}"],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300,
                       stdin=subprocess.DEVNULL, env=isolated_environment(tmp_path))
    out = r.stdout + r.stderr
    assert r.returncode == 0 and "BOUNDED OK" in r.stdout and "SCRIPT ERROR" not in out, out[-3000:]


def test_closing_while_the_lobby_has_not_answered_does_not_crash(tmp_path, server, keys):
    """MACPLAY-1 (the owner's Mac, 2026-10-17 11:13: SIGSEGV at 0x190 on the online-count thread): Play starts the game beside the launcher
    at once and the window closes while the players-online request is still out. The window waits for (and cancels) that request: the
    launcher exits 0, the game was played, and the request had really been sent before the close (else the race was not played)"""
    from test_launcher import STATS_GOOD
    server.stats = STATS_GOOD
    server.stats_delay = 20.0
    home = unpack_beside(tmp_path, "v0.5.0")
    assert launch(tmp_path, server, keys["release"], f"--lobby={server.origin}").code == 0
    started = time.monotonic()
    r = launch(tmp_path, server, keys["release"], f"--launcher-dir={home}", "--ui-play=1", f"--screenshot={tmp_path / 'unused.png'}")
    took = time.monotonic() - started
    assert r.code == 0, r
    assert r.has("ui test: PLAY pressed (v0.5.0") and r.has("started v0.5.0, the game beside the launcher"), r
    assert "played v0.5.0" in played(tmp_path), r
    assert [p for p, _ in server.requests if p == "/v1/stats"], server.requests
    assert took < 15.0, f"the close waited {took:.1f} s for the lobby: the request was not cancelled"
