"""Lane LAUNCH-1: the OpenBFME Launcher against a local release server.

The launcher (a development run of launcher/ on the Godot 4.7.2 editor binary, or exported test packages for the self-update) talks to
an HTTP server on 127.0.0.1 that plays GitHub: the releases API (ETag / 304, a 403 rate limit) and the release downloads (Range resumes,
a cut connection, redirects). Releases are made the way a real one is: make_archive.py archives, make_manifest.py's manifest, an Ed25519
signature from ed25519.py with a throwaway key made here (never committed). Each test runs the launcher's command line (--update,
--install=, --play, --self-update, ...) with its own data folder (XDG_DATA_HOME) and reads its LAUNCHER lines.

Skipped (loudly) without Godot 4.7.2; the self-update tests also need the Linux export templates.
"""
from __future__ import annotations

import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import time
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
LAUNCHER = REPO / "launcher"
sys.path.insert(0, str(HERE))

import ed25519  # noqa: E402
import make_archive  # noqa: E402
import make_manifest  # noqa: E402
from launcher_test_server import COMMIT, TEST_REPO, ReleaseServer, tiny_package  # noqa: E402

GODOT = os.environ.get("GODOT", "godot")


def _godot_ok() -> bool:
    try:
        return subprocess.run([GODOT, "--version"], capture_output=True, text=True, timeout=60).stdout.startswith("4.7.2.")
    except (OSError, subprocess.TimeoutExpired):
        return False


pytestmark = pytest.mark.skipif(not _godot_ok(), reason="SKIP launcher tests: Godot 4.7.2 not found (set GODOT)")


@pytest.fixture(autouse=True)
def _remove_tmp_path(request, tmp_path):
    """each launcher test unpacks packages and launchers (the self-update tests copy 75 MB executables): its folder is removed when it
    ends, so a run does not fill /tmp (a RAM disk on the Deck)"""
    yield
    shutil.rmtree(tmp_path, ignore_errors=True)


@pytest.fixture
def server():
    s = ReleaseServer()
    yield s
    s.stop()


@pytest.fixture(scope="session")
def keys(tmp_path_factory):
    """(secret, public) of the throwaway release key, and another key"""
    return {"release": os.urandom(32), "other": os.urandom(32)}


@pytest.fixture(scope="session", autouse=True)
def imported():
    """the launcher project's import cache (a fresh clone has none; the first import may abort on a known Godot quirk)"""
    for _ in range(2):
        subprocess.run([GODOT, "--headless", "--path", str(LAUNCHER), "--import"], capture_output=True, timeout=300)


# ---- packages ---------------------------------------------------------------------------------------------------------------------------

GAME_SCRIPT = b'#!/bin/sh\necho "played $(basename "$(dirname "$0")") $*" >> "$OPENBFME_TEST_PLAYED"\n'


def game_tar(tmp: Path, version: str, extra: bytes = b"") -> bytes:
    top = tmp / f"build-{version}" / f"openbfme-{version}-linux-x64"
    top.mkdir(parents=True, exist_ok=True)
    (top / "OpenBFME.x86_64").write_bytes(GAME_SCRIPT)
    (top / "OpenBFME.x86_64").chmod(0o755)
    (top / "OpenBFME.pck").write_bytes(b"pack of " + version.encode() + extra)
    (top / "VERSION").write_text(f"OpenBFME {version}\n")
    out = tmp / f"openbfme-{version}-linux-x64.tar.gz"
    make_archive.tar_gz(top, out, 1700000000)
    return out.read_bytes()


def evil_tar(version: str, name: str, kind: str = "file") -> bytes:
    """a tar.gz with one bad member (written with tarfile directly: make_archive.py writes nothing like it)"""
    top = f"openbfme-{version}-linux-x64"
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.GNU_FORMAT) as t:
        d = tarfile.TarInfo(top)
        d.type = tarfile.DIRTYPE
        d.mode = 0o755
        t.addfile(d)
        exe = tarfile.TarInfo(f"{top}/OpenBFME.x86_64")
        exe.size = len(GAME_SCRIPT)
        exe.mode = 0o755
        t.addfile(exe, io.BytesIO(GAME_SCRIPT))
        bad = tarfile.TarInfo(name)
        if kind == "symlink":
            bad.type = tarfile.SYMTYPE
            bad.linkname = "/tmp"
            t.addfile(bad)
        else:
            payload = b"escaped\n"
            bad.size = len(payload)
            t.addfile(bad, io.BytesIO(payload))
    import gzip
    return gzip.compress(raw.getvalue(), mtime=0)


def release_files(tmp: Path, version: str, secret: bytes, game: bytes | None = None, launcher: bytes | None = None,
                  repo: str = TEST_REPO, date: str | None = None, lenient: bool = False) -> dict[str, bytes]:
    """the assets of a release: four archives, SHA256SUMS, manifest.json and its signature. `lenient`: the game archive is deliberately
    malformed (bad member paths): its signed file list is taken from a good package, so the launcher's own path checks are tested"""
    d = tmp / f"release-{version}-{hashlib.sha256(os.urandom(8)).hexdigest()[:6]}"
    d.mkdir(parents=True)
    arch = {
        make_manifest.asset_name("game", version, "linux-x64"): game if game is not None else game_tar(tmp, version),
        make_manifest.asset_name("game", version, "windows-x64"):
            tiny_package(make_manifest.asset_name("game", version, "windows-x64"), {"OpenBFME.exe": b"windows game of " + version.encode()}),
        make_manifest.asset_name("launcher", version, "linux-x64"): launcher if launcher is not None else
            tiny_package(make_manifest.asset_name("launcher", version, "linux-x64"), {"OpenBFMELauncher.x86_64": b"launcher " + version.encode()}),
        make_manifest.asset_name("launcher", version, "windows-x64"):
            tiny_package(make_manifest.asset_name("launcher", version, "windows-x64"), {"OpenBFMELauncher.exe": b"launcher " + version.encode()}),
    }
    sums = ""
    for n, data in arch.items():
        (d / n).write_bytes(data)
        sums += f"{hashlib.sha256(data).hexdigest()}  {n}\n"
    (d / f"SHA256SUMS-{version}.txt").write_text(sums)
    date = date or time.strftime("%Y-%m-%d", time.gmtime())
    strict = make_manifest.package_files
    if lenient:
        good = strict(make_manifest.asset_name("game", version, "linux-x64"), game_tar(tmp, version))
        make_manifest.package_files = lambda name, data: good if name.startswith("openbfme-v") and "linux" in name else strict(name, data)
    try:
        manifest = make_manifest.dumps(make_manifest.build(d, version, repo, COMMIT, date))
    finally:
        make_manifest.package_files = strict
    files = dict(arch)
    files[f"SHA256SUMS-{version}.txt"] = sums.encode()
    files["manifest.json"] = manifest
    files["manifest.json.sig"] = ed25519.sign(secret, manifest)
    return files


# ---- running the launcher ---------------------------------------------------------------------------------------------------------------

class Run:
    def __init__(self, code: int, out: str) -> None:
        self.code, self.out = code, out
        self.lines = [ln for ln in out.splitlines() if ln.startswith("LAUNCHER")]

    def has(self, text: str) -> bool:
        return any(text in ln for ln in self.lines)

    def __repr__(self) -> str:
        return f"exit {self.code}\n" + "\n".join(self.lines)


def launch(tmp: Path, server: ReleaseServer | None, key: bytes, *args: str, exe: Path | None = None, origin: str | None = None,
           timeout: int = 180) -> Run:
    env = dict(os.environ, XDG_DATA_HOME=str(tmp / "data"), OPENBFME_TEST_PLAYED=str(tmp / "played.txt"))
    test = []
    if origin or server:
        test.append(f"--api-base={origin or server.origin}")
    test += [f"--trust-key={ed25519.public_key(key).hex()}", f"--repo={TEST_REPO}"]
    cmd = [str(exe), "--headless", "--", *test, *args] if exe else [GODOT, "--headless", "--path", str(LAUNCHER), "--", *test, *args]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=timeout, stdin=subprocess.DEVNULL)
    return Run(r.returncode, r.stdout + r.stderr)


def data(tmp: Path) -> Path:
    return tmp / "data" / "OpenBFMELauncher"


def installed(tmp: Path) -> list[str]:
    v = data(tmp) / "versions"
    return sorted(p.name for p in v.iterdir()) if v.is_dir() else []


def played(tmp: Path) -> str:
    p = tmp / "played.txt"
    for _ in range(50):  # the game is its own process
        if p.exists() and p.read_text().strip():
            return p.read_text()
        time.sleep(0.1)
    return ""


# ---- the tests --------------------------------------------------------------------------------------------------------------------------

def test_gdscript_crypto_matches_the_references(tmp_path, keys):
    """SHA-512 and Ed25519 in GDScript against hashlib, ed25519.py (RFC 8032) and OpenSSL; invalid signatures rejected"""
    vec = {"sha512": [], "ed25519": []}
    for n in (0, 1, 111, 112, 127, 128, 129, 239, 240, 1000):
        m = os.urandom(n)
        vec["sha512"].append([m.hex(), hashlib.sha512(m).hexdigest()])
    for i in range(6):
        sk = os.urandom(32)
        pk = ed25519.public_key(sk)
        msg = os.urandom(i * 37)
        sig = ed25519.sign(sk, msg)
        vec["ed25519"].append([pk.hex(), msg.hex(), sig.hex(), True, "python"])
        vec["ed25519"].append([pk.hex(), (msg + b"x").hex(), sig.hex(), False, "changed message"])
        vec["ed25519"].append([ed25519.public_key(os.urandom(32)).hex(), msg.hex(), sig.hex(), False, "another key"])
    if shutil.which("openssl"):
        sk = os.urandom(32)
        (tmp_path / "k.pem").write_text(ed25519.private_key_pem(sk))
        msg = b'{"a manifest": true}\n'
        (tmp_path / "m").write_bytes(msg)
        subprocess.run(["openssl", "pkeyutl", "-sign", "-rawin", "-inkey", str(tmp_path / "k.pem"), "-in", str(tmp_path / "m"),
                        "-out", str(tmp_path / "s")], check=True)
        osig = (tmp_path / "s").read_bytes()
        assert osig == ed25519.sign(sk, msg), "ed25519.py and OpenSSL disagree"
        vec["ed25519"].append([ed25519.public_key(sk).hex(), msg.hex(), osig.hex(), True, "openssl"])
    else:
        print("SKIP the OpenSSL cross-check: no openssl")
    (tmp_path / "vectors.json").write_text(json.dumps(vec))
    r = subprocess.run([GODOT, "--headless", "--path", str(LAUNCHER), "--script", "res://tests/crypto_test.gd", "--",
                        f"--vectors={tmp_path / 'vectors.json'}"], capture_output=True, text=True, timeout=600)
    print(r.stdout[-3000:])
    assert r.returncode == 0 and "CRYPTO OK" in r.stdout, r.stdout[-3000:] + r.stderr[-2000:]


def test_rfc8032_vectors_in_python():
    sk = bytes.fromhex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb")
    assert ed25519.public_key(sk).hex() == "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c"
    sig = ed25519.sign(sk, b"\x72")
    assert sig.hex() == ("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
                         "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00")
    assert ed25519.verify(ed25519.public_key(sk), b"\x72", sig)
    assert not ed25519.verify(ed25519.public_key(sk), b"\x73", sig)
    s_plus_l = sig[:32] + (int.from_bytes(sig[32:], "little") + ed25519.L).to_bytes(32, "little")
    assert not ed25519.verify(ed25519.public_key(sk), b"\x72", s_plus_l), "S >= L is rejected"


def test_launcher_unit_rules(tmp_path, keys):
    files = release_files(tmp_path, "v1.2.3-preview.4", keys["release"])
    (tmp_path / "m.json").write_bytes(files["manifest.json"])
    (tmp_path / "m.sig").write_bytes(files["manifest.json.sig"])
    r = subprocess.run([GODOT, "--headless", "--path", str(LAUNCHER), "--script", "res://tests/unit_test.gd", "--",
                        f"--key={ed25519.public_key(keys['release']).hex()}", f"--manifest={tmp_path / 'm.json'}",
                        f"--sig={tmp_path / 'm.sig'}"], capture_output=True, text=True, timeout=300)
    print(r.stdout[-3000:])
    assert r.returncode == 0 and "UNIT OK" in r.stdout, r.stdout[-3000:] + r.stderr[-2000:]


def test_fresh_install_and_play(tmp_path, server, keys):
    server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["release"]))
    r = launch(tmp_path, server, keys["release"], "--update", "--list", "--play")
    assert r.code == 0 and r.has("update installed v0.2.0") and r.has("installed v0.2.0"), r
    assert installed(tmp_path) == ["v0.2.0"]
    game = data(tmp_path) / "versions/v0.2.0/game"
    assert (game / "OpenBFME.pck").read_bytes() == b"pack of v0.2.0"
    assert os.access(game / "OpenBFME.x86_64", os.X_OK), "the executable bit of the archive is kept"
    assert "played game" in played(tmp_path)
    signed = json.loads((data(tmp_path) / "versions/v0.2.0/manifest.json").read_text())["assets"]["game"]["linux-x64"]["files"]
    assert signed["OpenBFME.pck"] == {"size": 14, "sha256": hashlib.sha256(b"pack of v0.2.0").hexdigest()}
    assert not list((data(tmp_path) / "downloads").glob("*.part")), "the verified download is removed after the install"
    agents = {h.get("user-agent") for _, h in server.requests}
    assert agents == {"OpenBFME-Launcher/v0.0.0-dev (+https://github.com/Open-BFME/openbfme-godot)"}, agents
    assert any(p.startswith(f"/repos/{TEST_REPO}/releases") for p, _ in server.requests)


def test_update_keeps_the_two_newest(tmp_path, server, keys):
    for v in ("v0.2.0", "v0.3.0", "v0.4.0"):
        server.publish(v, release_files(tmp_path, v, keys["release"]))
        r = launch(tmp_path, server, keys["release"], "--update")
        assert r.code == 0 and r.has(f"update installed {v}"), r
    assert installed(tmp_path) == ["v0.3.0", "v0.4.0"]
    assert r.has("removed the old version v0.2.0")
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 0 and r.has("up to date: v0.4.0 installed"), r


def test_channels(tmp_path, server, keys):
    server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["release"]))
    server.publish("v0.3.0-preview.1", release_files(tmp_path, "v0.3.0-preview.1", keys["release"]))
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.has("update installed v0.2.0"), r  # stable: the pre-release is not offered
    r = launch(tmp_path, server, keys["release"], "--channel=preview", "--update")
    assert r.has("update installed v0.3.0-preview.1"), r
    # a release whose pre-release flag disagrees with its signed channel is refused
    server.publish("v0.4.0", release_files(tmp_path, "v0.4.0", keys["release"]), prerelease=True)
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 3 and r.has("does not match the release's pre-release flag"), r


def test_offline_launch(tmp_path, server, keys):
    server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["release"]))
    assert launch(tmp_path, server, keys["release"], "--update").code == 0
    server.stop()
    t0 = time.time()
    r = launch(tmp_path, server, keys["release"], "--update", "--play")
    assert r.code == 2 and r.has("updates skipped: the release list could not be fetched"), r
    assert r.has("play v0.2.0") and "played game" in played(tmp_path), r
    assert time.time() - t0 < 60


def test_no_network_and_nothing_installed(tmp_path, keys):
    r = launch(tmp_path, None, keys["release"], "--update", "--play", origin="http://127.0.0.1:9")
    assert r.code == 1 and r.has("updates skipped") and r.has("nothing to play"), r


def test_tampered_asset_is_rejected(tmp_path, server, keys):
    files = release_files(tmp_path, "v0.2.0", keys["release"])
    name = make_manifest.asset_name("game", "v0.2.0", "linux-x64")
    good = files[name]
    files[name] = good[:-1] + bytes([good[-1] ^ 1])  # same size, other bytes
    server.publish("v0.2.0", files)
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 3 and r.has("does not match the signed manifest") and r.has("nothing was installed"), r
    assert installed(tmp_path) == []
    assert not list((data(tmp_path) / "downloads").glob("*.part")), "a failed check deletes the download (no half state)"
    files[name] = good + b"x"  # a longer file: the size check (the server's extra byte is never written)
    server.publish("v0.2.0", files)
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 3 and r.has("more than the manifest's"), r
    assert installed(tmp_path) == []


def test_tampered_manifest_is_rejected(tmp_path, server, keys):
    files = release_files(tmp_path, "v0.2.0", keys["release"])
    files["manifest.json"] = files["manifest.json"].replace(b'"commit": "0', b'"commit": "1')
    server.publish("v0.2.0", files)
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 3 and r.has("signature is not valid"), r
    assert installed(tmp_path) == []
    assert not any("/dl/v0.2.0/openbfme-v0.2.0" in p for p, _ in server.requests), "no package is downloaded for a bad manifest"


def test_manifest_signed_by_another_key_is_rejected(tmp_path, server, keys):
    server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["other"]))
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 3 and r.has("signature is not valid"), r
    assert installed(tmp_path) == []


def test_manifest_for_another_repository_is_rejected(tmp_path, server, keys):
    server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["release"], repo="Other/fork"))
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 3 and r.has("the manifest is for Other/fork"), r


def test_truncated_download_resumes(tmp_path, server, keys):
    files = release_files(tmp_path, "v0.2.0", keys["release"], game=game_tar(tmp_path, "v0.2.0", extra=os.urandom(300000)))
    name = make_manifest.asset_name("game", "v0.2.0", "linux-x64")
    server.publish("v0.2.0", files)
    path = f"/dl/v0.2.0/{name}"
    server.truncate[path] = 100000
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 3 and r.has("resumes from here next time"), r
    part = data(tmp_path) / "downloads" / (name + ".part")
    assert part.stat().st_size == 100000
    assert installed(tmp_path) == []
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 0 and r.has("resumed %s at 100000 bytes" % name) and r.has("update installed v0.2.0"), r
    ranges = [h.get("range") for p, h in server.requests if p == path]
    assert ranges[-1] == "bytes=100000-", ranges
    # a server that ignores Range: the download starts over and still verifies
    files2 = release_files(tmp_path, "v0.3.0", keys["release"], game=game_tar(tmp_path, "v0.3.0", extra=os.urandom(300000)))
    server.publish("v0.3.0", files2)
    server.truncate[f"/dl/v0.3.0/{make_manifest.asset_name('game', 'v0.3.0', 'linux-x64')}"] = 50000
    assert launch(tmp_path, server, keys["release"], "--update").code == 3
    server.ignore_range = True
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 0 and r.has("update installed v0.3.0"), r


@pytest.mark.parametrize("member,kind,why", [
    ("openbfme-v0.2.0-linux-x64/../escaped.txt", "file", "a '..' component"),
    ("/tmp/openbfme-launcher-escaped.txt", "file", "an absolute path"),
    ("openbfme-v0.2.0-linux-x64/link", "symlink", "type '2'"),
    ("openbfme-v0.2.0-linux-x64/a\\..\\..\\x", "file", "a backslash"),
    ("elsewhere/x", "file", "not inside the package folder"),
])
def test_archive_paths_are_checked(tmp_path, server, keys, member, kind, why):
    server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["release"], game=evil_tar("v0.2.0", member, kind), lenient=True))
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 3 and r.has(why) and r.has("v0.2.0 was not installed"), r
    assert installed(tmp_path) == [], "the partial folder is removed"
    assert not (data(tmp_path) / "versions" / "escaped.txt").exists()
    assert not Path("/tmp/openbfme-launcher-escaped.txt").exists()


def test_downgrade_needs_the_users_pick(tmp_path, server, keys):
    old = release_files(tmp_path, "v0.2.0", keys["release"])
    server.publish("v0.2.0", old)
    server.publish("v0.3.0", release_files(tmp_path, "v0.3.0", keys["release"]))
    assert launch(tmp_path, server, keys["release"], "--update").has("update installed v0.3.0")
    # an attacker (or a mistake) serves only the older, validly signed release: never installed automatically
    server.releases = [r for r in server.releases if r["tag_name"] == "v0.2.0"]
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 0 and r.has("up to date: v0.3.0 installed, newest stable release v0.2.0"), r
    assert installed(tmp_path) == ["v0.3.0"]
    # the user picks it: installed and played from now on (a rollback); a new release replaces the pick
    r = launch(tmp_path, server, keys["release"], "--install=v0.2.0", "--play")
    assert r.code == 0 and r.has("installing the older v0.2.0 by request") and r.has("play v0.2.0"), r
    assert "played game" in played(tmp_path)
    assert installed(tmp_path) == ["v0.2.0", "v0.3.0"]
    r = launch(tmp_path, server, keys["release"], "--select=v0.3.0", "--list")
    assert r.has("installed v0.3.0 (selected)"), r
    r = launch(tmp_path, server, keys["release"], "--select=v9.0.0")
    assert r.code == 1 and r.has("it is not installed"), r


def test_rate_limit_and_not_modified(tmp_path, server, keys):
    server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["release"]))
    assert launch(tmp_path, server, keys["release"], "--update").code == 0
    r = launch(tmp_path, server, keys["release"], "--update")
    api = [h for p, h in server.requests if p.startswith("/repos/")]
    assert api[-1].get("if-none-match"), "the cached list's ETag is sent"
    assert r.code == 0 and r.has("unchanged: 304 Not Modified") and r.has("up to date"), r
    server.rate_limited = True
    r = launch(tmp_path, server, keys["release"], "--update", "--play")
    assert r.code == 2 and r.has("rate limit for this network is used up (it resets at"), r
    assert r.has("play v0.2.0"), r


def test_redirects_only_to_allowed_hosts(tmp_path, server, keys):
    files = release_files(tmp_path, "v0.2.0", keys["release"])
    server.publish("v0.2.0", files)
    name = make_manifest.asset_name("game", "v0.2.0", "linux-x64")
    port = server.httpd.server_address[1]
    server.redirect[f"/dl/v0.2.0/{name}"] = f"http://localhost:{port}/dl/v0.2.0/{name}"
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 3 and r.has("refused URL 'http://localhost"), r
    server.redirect[f"/dl/v0.2.0/{name}"] = f"/elsewhere/{name}"  # a relative redirect on the same origin is followed
    server.files[f"/elsewhere/{name}"] = files[name]
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 0 and r.has("update installed v0.2.0"), r


def test_test_origin_must_be_loopback(tmp_path, keys):
    r = launch(tmp_path, None, keys["release"], "--update", origin="http://example.com:80")
    assert r.code == 1 and r.has("the test API origin must be http://127.0.0.1"), r


# ---- the packaged launcher: self-update and the release build ---------------------------------------------------------------------------

def _templates_ok() -> bool:
    base = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")) / "godot/export_templates/4.7.2.stable"
    return (base / "linux_release.x86_64").is_file()


def build_launcher(out: Path, version: str, key: bytes, test_build: bool = True) -> bytes:
    (out / "key.pub").parent.mkdir(parents=True, exist_ok=True)
    (out / "key.pub").write_text(ed25519.public_key(key).hex() + "\n")
    cmd = [str(HERE / "build_launcher.sh"), "--out", str(out), "--version", version, "--commit", COMMIT, "--repo", TEST_REPO,
           "--key-file", str(out / "key.pub"), "--mtime", "1700000000", "--platforms", "linux", "--worktree", "--godot", GODOT]
    if test_build:
        cmd.append("--test-build")
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
    assert r.returncode == 0, r.stdout[-3000:] + r.stderr[-3000:]
    return (out / f"openbfme-launcher-{version}-linux-x64.tar.gz").read_bytes()


@pytest.fixture(scope="module")
def launcher_builds(tmp_path_factory, keys):
    if not _templates_ok():
        pytest.skip("SKIP self-update tests: the Godot 4.7.2 Linux export templates are not installed")
    d = tmp_path_factory.mktemp("launchers")
    builds = {v: build_launcher(d / v, v, keys["release"]) for v in ("v0.2.0", "v0.3.0")} | \
        {"release": build_launcher(d / "release", "v0.2.0", keys["release"], test_build=False)}
    shutil.rmtree(d, ignore_errors=True)  # the archives are kept in memory
    yield builds


def unpack_launcher(tmp: Path, archive: bytes) -> Path:
    tmp.mkdir(parents=True, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as t:
        t.extractall(tmp, filter="tar")
    return next(tmp.glob("openbfme-launcher-*-linux-x64"))


def exe_sha(archive: bytes) -> str:
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as t:
        m = next(m for m in t.getmembers() if m.name.endswith("/OpenBFMELauncher.x86_64"))
        return hashlib.sha256(t.extractfile(m).read()).hexdigest()


def leftovers(inst: Path, tmp: Path) -> list[str]:
    out = sorted(p.name for p in inst.iterdir() if p.name not in ("OpenBFMELauncher.x86_64", "README.txt", "LICENSE", "NOTICE", "VERSION"))
    if (data(tmp) / "launcher-update.json").exists():
        out.append("journal")
    return out


def staged_launcher(tmp_path, server, keys, launcher_builds, launcher=None):
    inst = unpack_launcher(tmp_path / "inst", launcher_builds["v0.2.0"])
    exe = inst / "OpenBFMELauncher.x86_64"
    server.publish("v0.3.0", release_files(tmp_path, "v0.3.0", keys["release"], launcher=launcher or launcher_builds["v0.3.0"]))
    r = launch(tmp_path, server, keys["release"], "--self-update", exe=exe)
    assert r.code == 0 and r.has("launcher update v0.3.0 staged"), r
    assert (inst / "update" / "VERSION.launcher").read_text().strip() == "v0.3.0"
    return inst, exe


def test_launcher_self_update_swap(tmp_path, server, keys, launcher_builds):
    inst, exe = staged_launcher(tmp_path, server, keys, launcher_builds)
    r = launch(tmp_path, server, keys["release"], "--version", exe=exe)  # returns when the old launcher, the helper and the new one ended
    assert r.has("started the update to v0.3.0") and r.has("replaced v0.2.0 by v0.3.0"), r
    assert r.has("v0.3.0 started after the update from v0.2.0") and r.has("the new launcher v0.3.0 confirmed its start"), r
    assert "OpenBFME Launcher v0.3.0" in r.out
    assert hashlib.sha256(exe.read_bytes()).hexdigest() == exe_sha(launcher_builds["v0.3.0"])
    assert (inst / "OpenBFMELauncher.x86_64.old").is_file(), "the previous launcher is kept until the new one has started once"
    r = launch(tmp_path, server, keys["release"], "--version", exe=exe)
    assert "OpenBFME Launcher v0.3.0" in r.out and r.has("removed the previous launcher (v0.2.0)"), r
    assert leftovers(inst, tmp_path) == []
    r = launch(tmp_path, server, keys["release"], "--self-update", exe=exe)
    assert r.has("the launcher is up to date (v0.3.0)"), r


KILL_POINTS = ["swap-journal", "swap-helper-started", "helper-start", "helper-backup", "helper-copied", "helper-replaced",
               "helper-placed", "after-update-confirmed"]


@pytest.mark.parametrize("point", KILL_POINTS)
def test_a_self_update_killed_at_every_step(tmp_path, server, keys, launcher_builds, point):
    """Sol r1: a kill between the old two-file renames left a launcher that could not load its project data. Killed at each step of
    the journaled one-file swap, every later start runs a complete launcher, and the update completes."""
    inst, exe = staged_launcher(tmp_path, server, keys, launcher_builds)
    r = launch(tmp_path, server, keys["release"], "--version", f"--test-kill-at={point}", exe=exe)
    assert f"LAUNCHER test kill at {point}" in (data(tmp_path) / "launcher.log").read_text(), r  # stdout of a killed process is lost
    versions = []
    for _ in range(4):
        r = launch(tmp_path, server, keys["release"], "--version", exe=exe)
        assert "OpenBFME Launcher v0." in r.out, ("the launcher at the name did not start", r.out[-3000:])
        versions.append(r.out)
        if leftovers(inst, tmp_path) == [] and hashlib.sha256(exe.read_bytes()).hexdigest() == exe_sha(launcher_builds["v0.3.0"]):
            break
    assert hashlib.sha256(exe.read_bytes()).hexdigest() == exe_sha(launcher_builds["v0.3.0"]), versions[-1][-3000:]
    assert leftovers(inst, tmp_path) == [], (leftovers(inst, tmp_path), versions[-1][-3000:])


def test_a_launcher_that_does_not_confirm_is_rolled_back(tmp_path, server, keys, launcher_builds):
    inst, exe = staged_launcher(tmp_path, server, keys, launcher_builds)
    old = hashlib.sha256(exe.read_bytes()).hexdigest()
    r = launch(tmp_path, server, keys["release"], "--version", "--test-kill-at=no-confirm", exe=exe)
    assert r.has("quits without confirming its start") and r.has("update to v0.3.0 rolled back"), r
    # the helper starts the previous launcher again, which ends the transaction
    assert r.has("was rolled back; staying on v0.2.0"), r
    assert hashlib.sha256(exe.read_bytes()).hexdigest() == old
    r = launch(tmp_path, server, keys["release"], "--version", exe=exe)
    assert "OpenBFME Launcher v0.2.0" in r.out, r
    assert leftovers(inst, tmp_path) == []
    r = launch(tmp_path, server, keys["release"], "--self-update", exe=exe)
    assert r.has("failed to start before and is not installed again automatically"), r


def test_a_launcher_that_cannot_start_is_given_up(tmp_path, server, keys, launcher_builds):
    """a validly signed launcher whose embedded pack is broken cannot even run as the helper: the old launcher keeps running, retries
    at most three times, then rejects the version"""
    work = tmp_path / "broken"
    top = unpack_launcher(work, launcher_builds["v0.3.0"])
    b = bytearray((top / "OpenBFMELauncher.x86_64").read_bytes())
    sys.path.insert(0, str(HERE))
    import audit_package  # noqa: PLC0415
    base = audit_package.embedded_pck_base(bytes(b))
    b[base + 4:base + 8] = b"\xff\xff\xff\xff"  # an unknown pack format
    (top / "OpenBFMELauncher.x86_64").write_bytes(bytes(b))
    make_archive.tar_gz(top, work / "broken.tar.gz", 1700000000)
    inst, exe = staged_launcher(tmp_path, server, keys, launcher_builds, launcher=(work / "broken.tar.gz").read_bytes())
    old = hashlib.sha256(exe.read_bytes()).hexdigest()
    for _ in range(4):
        r = launch(tmp_path, server, keys["release"], "--version", exe=exe)
        assert "OpenBFME Launcher v0.2.0" in r.out, r
        assert hashlib.sha256(exe.read_bytes()).hexdigest() == old
    assert r.has("did not complete after 3 attempts: given up"), r
    assert leftovers(inst, tmp_path) == []


def test_release_build_ignores_the_test_options(tmp_path, server, keys, launcher_builds):
    inst = unpack_launcher(tmp_path / "rel", launcher_builds["release"])
    server.publish("v0.3.0", release_files(tmp_path, "v0.3.0", keys["release"]))
    # --version only: with the test options ignored, --update would ask the real api.github.com
    r = launch(tmp_path, server, keys["other"], "--version", exe=inst / "OpenBFMELauncher.x86_64", timeout=120)
    assert r.has("ignored --api-base: test options are off in a release build"), r
    assert r.has("ignored --trust-key"), r
    assert not server.requests, "the release build never contacts the test server"
    assert "updates from Test/repo" in r.out


def test_read_only_launcher_folder(tmp_path, server, keys, launcher_builds):
    inst = unpack_launcher(tmp_path / "ro", launcher_builds["v0.2.0"])
    server.publish("v0.3.0", release_files(tmp_path, "v0.3.0", keys["release"], launcher=launcher_builds["v0.3.0"]))
    inst.chmod(0o555)
    try:
        r = launch(tmp_path, server, keys["release"], "--update", "--self-update", exe=inst / "OpenBFMELauncher.x86_64")
    finally:
        inst.chmod(0o755)
    assert r.code == 3 and r.has("update installed v0.3.0") and r.has("is not writable: the launcher cannot update itself there"), r
    assert not any(p.endswith("openbfme-launcher-v0.3.0-linux-x64.tar.gz") for p, _ in server.requests), "nothing downloaded for it"


def test_freeze_warning(tmp_path, server, keys):
    """Sol r1: an old but validly signed release list (a freeze) is not detectable, so a manifest signed more than 60 days ago gives a
    plain warning; the update still works"""
    server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["release"], date="2020-01-01"))
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 0 and r.has("update installed v0.2.0"), r
    assert r.has("warning: the newest stable release the launcher can see (v0.2.0) is") and r.has("signed 2020-01-01"), r
    server.publish("v0.3.0", release_files(tmp_path, "v0.3.0", keys["release"]))
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 0 and not r.has("warning:"), r


def test_versions_outside_the_release_grammar_are_skipped(tmp_path, server, keys):
    """Sol r1: with the general SemVer grammar, v1.0.0-1 (signed, older) was installed over v1.0.0--1; neither is a release version now,
    and a tag with a trailing newline is not one either"""
    server.publish("v1.0.0-preview.1", release_files(tmp_path, "v1.0.0-preview.1", keys["release"]))
    for tag in ("v1.0.0-1", "v1.0.0--1", "v1.0.0-rc.1"):
        server.releases.insert(0, dict(server.releases[-1], tag_name=tag, prerelease=True))
    server.releases.insert(0, dict(server.releases[-1], tag_name="v9.0.0\n", prerelease=False))
    r = launch(tmp_path, server, keys["release"], "--channel=preview", "--update")
    assert r.code == 0 and r.has("update installed v1.0.0-preview.1"), r
    for tag in ("v1.0.0-1", "v1.0.0--1", "v1.0.0-rc.1", "v9.0.0\\n"):
        assert r.has(f"skipped release '{tag}'"), (tag, r)
    with pytest.raises(SystemExit):
        make_manifest.channel_of("v1.0.0\n")


def test_an_interrupted_reinstall_is_recovered(tmp_path, server, keys):
    """Sol r1: replacing an installed version deleted it before the replacement was in place. The state a crash left behind (the
    installed folder set aside, nothing in its place) is restored at the next start."""
    server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["release"]))
    assert launch(tmp_path, server, keys["release"], "--update").code == 0
    versions = data(tmp_path) / "versions"
    (versions / "v0.2.0").rename(versions / "v0.2.0.old")
    (versions / "v0.2.0.partial").mkdir()
    r = launch(tmp_path, None, keys["release"], "--list", "--play")
    assert r.has("restored v0.2.0: replacing it was interrupted") and r.has("installed v0.2.0") and r.has("play v0.2.0"), r
    assert sorted(p.name for p in versions.iterdir()) == ["v0.2.0"]


@pytest.mark.parametrize("point", ["install-unpacked", "install-aside", "install-placed"])
def test_a_reinstall_killed_at_every_step(tmp_path, server, keys, point):
    """The reinstall of an installed version (the user's pick), killed at each step: the next start has a complete, verified
    v0.2.0 (the old or the new copy) and no leftovers."""
    server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["release"]))
    assert launch(tmp_path, server, keys["release"], "--update").code == 0
    r = launch(tmp_path, server, keys["release"], "--install=v0.2.0", f"--test-kill-at={point}")
    assert r.code != 0 and f"LAUNCHER test kill at {point}" in (data(tmp_path) / "launcher.log").read_text(), r
    r = launch(tmp_path, None, keys["release"], "--list", "--play")
    assert r.code == 0 and r.has("installed v0.2.0") and r.has("play v0.2.0") and "played game" in played(tmp_path), r
    assert sorted(p.name for p in (data(tmp_path) / "versions").iterdir()) == ["v0.2.0"]


def test_a_swapped_staged_launcher_is_not_run(tmp_path, server, keys, launcher_builds):
    """Sol r2: the swap took the new executable's SHA-256 from the file in update/ itself, so a file swapped in after staging ran
    unchecked. Now every staged file is checked against the cached signed manifest before the journal is written; the journal's
    SHA-256 comes from the manifest."""
    inst, exe = staged_launcher(tmp_path, server, keys, launcher_builds)
    old = hashlib.sha256(exe.read_bytes()).hexdigest()
    other = unpack_launcher(tmp_path / "other", launcher_builds["release"]) / "OpenBFMELauncher.x86_64"  # runnable, but not v0.3.0
    shutil.copy(other, inst / "update" / "OpenBFMELauncher.x86_64")
    r = launch(tmp_path, server, keys["release"], "--version", exe=exe)
    assert r.has("removed the staged launcher update: the staged launcher v0.3.0 is not the signed one"), r
    assert not r.has("started the update to") and not r.has("replaced v0.2.0"), r
    assert hashlib.sha256(exe.read_bytes()).hexdigest() == old and not (inst / "update").exists()
    assert leftovers(inst, tmp_path) == []
    # an extra file in update/ is refused the same way
    inst2, exe2 = staged_launcher(tmp_path / "b", server, keys, launcher_builds)
    (inst2 / "update" / "extra.dll").write_bytes(b"MZ")
    r = launch(tmp_path / "b", server, keys["release"], "--version", exe=exe2)
    assert r.has("extra.dll is not a file of the signed package"), r


@pytest.mark.parametrize("change", ["pck", "exe", "extra"])
def test_a_modified_installed_game_is_not_started(tmp_path, server, keys, change):
    """round 3: before Play every file of the version is checked against the signed per-file SHA-256; a changed one is not started,
    and downloading it again (the user's pick of the same version) repairs it"""
    server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["release"]))
    assert launch(tmp_path, server, keys["release"], "--update").code == 0
    game = data(tmp_path) / "versions/v0.2.0/game"
    if change == "pck":
        b = bytearray((game / "OpenBFME.pck").read_bytes())
        b[0] ^= 1  # the same size: only the hash shows it
        (game / "OpenBFME.pck").write_bytes(bytes(b))
    elif change == "exe":
        (game / "OpenBFME.x86_64").write_bytes(GAME_SCRIPT.replace(b"played", b"PWNED!"))
    else:
        (game / "evil.so").write_bytes(b"\x7fELF")
    r = launch(tmp_path, None, keys["release"], "--play")
    assert r.code == 1 and r.has("v0.2.0 was changed after it was installed") and r.has("it is not started"), r
    assert played(tmp_path) == ""
    r = launch(tmp_path, server, keys["release"], "--install=v0.2.0", "--play")
    assert r.code == 0 and r.has("install installed v0.2.0") and r.has("play v0.2.0") and "played game" in played(tmp_path), r


def test_the_freeze_warning_stays_visible_in_the_window(tmp_path, server, keys):
    """Sol r2: the warning was folded into the status line and replaced by the automatic install's message"""
    server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["release"], date="2020-01-01"))
    r = launch(tmp_path, server, keys["release"], f"--screenshot={tmp_path / 'shot.png'}")
    assert r.has("ui status: Installed v0.2.0."), r
    assert r.has("ui warning: Warning: the newest stable release the launcher can see (v0.2.0) is"), r
    server.publish("v0.3.0", release_files(tmp_path, "v0.3.0", keys["release"]))
    r = launch(tmp_path, server, keys["release"], f"--screenshot={tmp_path / 'shot.png'}")
    assert r.has("ui status: Installed v0.3.0.") and any(ln.rstrip().endswith("ui warning:") for ln in r.lines), r
