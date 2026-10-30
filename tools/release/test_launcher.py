"""Lane LAUNCH-1: the OpenBFME Launcher against a local release server.

The launcher (a development run of launcher/ on the Godot 4.7.2 editor binary, or exported test packages for the self-update) talks to
an HTTP server on 127.0.0.1 that plays GitHub: the releases API (ETag / 304, a 403 rate limit) and the release downloads (Range resumes,
a cut connection, redirects). Releases are made the way a real one is: make_archive.py archives, make_manifest.py's manifest, an Ed25519
signature from ed25519.py with a throwaway key made here (never committed). Each test runs the launcher's command line (--update,
--install=, --play, --self-update, ...) with isolated user data and reads its LAUNCHER lines.

Skipped (loudly) without Godot 4.7.2; the self-update tests also need the host's 4.7.2 export template.
"""
from __future__ import annotations

import hashlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
import zipfile
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
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
from launcher_test_support import (ARCHIVE_SUFFIX, GAME_EXE, LAUNCHER_EXE, MACOS, MACOS_SKIP, PLATFORM, WINDOWS, asset_name,  # noqa: E402
                                   dismiss_missing_pack_alert, godot_engine, isolated_environment, native_game, normalized_path,
                                   process_running, stop_process)

GODOT = godot_engine(os.environ.get("GODOT", "godot"))


def _godot_ok() -> bool:
    try:
        return subprocess.run([GODOT, "--version"], capture_output=True, text=True, encoding="utf-8", errors="replace",
                              timeout=60).stdout.startswith("4.7.2.")
    except (OSError, subprocess.TimeoutExpired):
        return False


pytestmark = [pytest.mark.skipif(MACOS, reason=MACOS_SKIP), pytest.mark.skipif(not _godot_ok(), reason="SKIP launcher tests: Godot 4.7.2 not found (set GODOT)")]


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
    """(RELGH-2: and a nightly-only key) the secrets of the throwaway release key (the old, Deck-style key), a second trusted key (SIGNKMS-1: the KMS key's stand-in,
    which signs the envelope) and another key"""
    return {"release": os.urandom(32), "kms": os.urandom(32), "other": os.urandom(32), "nightly": os.urandom(32)}


@pytest.fixture(scope="session", autouse=True)
def imported(tmp_path_factory):
    """the launcher project's import cache (a fresh clone has none; the first import may abort on a known Godot quirk)"""
    env = isolated_environment(tmp_path_factory.mktemp("launcher-import"))
    for _ in range(2):
        subprocess.run([GODOT, "--headless", "--path", str(LAUNCHER), "--import"], env=env, capture_output=True, timeout=300)


# ---- packages ---------------------------------------------------------------------------------------------------------------------------

def game_script() -> bytes:
    return native_game("played").read_bytes() if WINDOWS else b'#!/bin/sh\necho "played $(basename "$(dirname "$0")") $*" >> "$OPENBFME_TEST_PLAYED"\n'


def slow_game() -> bytes:
    return native_game("long").read_bytes() if WINDOWS else game_script() + b'''exec >/dev/null 2>&1 </dev/null
event="$OPENBFME_TEST_PLAYED.$$.exit"
trap 'rm -f "$event"' EXIT
trap 'exit' TERM INT
mkfifo "$event" || exit 1
read -r done < "$event"
'''


def game_tar(tmp: Path, version: str, extra: bytes = b"", script: bytes | None = None, version_extra: str = "") -> bytes:
    top = tmp / f"build-{version}" / f"openbfme-{version}-{PLATFORM}"
    top.mkdir(parents=True, exist_ok=True)
    (top / GAME_EXE).write_bytes(game_script() if script is None else script)
    if not WINDOWS:
        (top / GAME_EXE).chmod(0o755)
    (top / "OpenBFME.pck").write_bytes(b"pack of " + version.encode() + extra)
    (top / "VERSION").write_text(f"OpenBFME {version}\n{version_extra}")
    out = tmp / asset_name("game", version)
    if WINDOWS:
        make_archive.zip_file(top, out, 1700000000)
    else:
        make_archive.tar_gz(top, out, 1700000000)
    return out.read_bytes()


def evil_tar(version: str, name: str, kind: str = "file") -> bytes:
    """a tar.gz with one bad member (written with tarfile directly: make_archive.py writes nothing like it)"""
    top = f"openbfme-{version}-{PLATFORM}"
    raw = io.BytesIO()
    if WINDOWS:
        with zipfile.ZipFile(raw, "w") as z:
            z.writestr(top + "/", b"")
            z.writestr(top + "/" + GAME_EXE, native_game("played").read_bytes())
            raw_name = name.replace(f"openbfme-{version}-linux-x64", top)
            info = zipfile.ZipInfo(raw_name)
            info.filename = raw_name
            info.orig_filename = raw_name
            info.create_system = 3
            info.external_attr = ((0o120777 if kind == "symlink" else 0o100644) << 16)
            z.writestr(info, b"/tmp" if kind == "symlink" else b"x")
        return raw.getvalue()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.GNU_FORMAT) as t:
        d = tarfile.TarInfo(top)
        d.type = tarfile.DIRTYPE
        d.mode = 0o755
        t.addfile(d)
        exe = tarfile.TarInfo(f"{top}/OpenBFME.x86_64")
        exe.size = len(game_script())
        exe.mode = 0o755
        t.addfile(exe, io.BytesIO(game_script()))
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
    host_game = game if game is not None else game_tar(tmp, version)
    arch = {
        make_manifest.asset_name("game", version, "linux-x64"): host_game if not WINDOWS else
            tiny_package(make_manifest.asset_name("game", version, "linux-x64"),
                         {"OpenBFME.x86_64": b"linux game of " + version.encode(),
                          "OpenBFME.pck": b"linux pack of " + version.encode(), "VERSION": f"OpenBFME {version}\n".encode()}),
        make_manifest.asset_name("game", version, "windows-x64"): host_game if WINDOWS else
            tiny_package(make_manifest.asset_name("game", version, "windows-x64"),
                         {"OpenBFME.exe": b"windows game of " + version.encode(),
                          "OpenBFME.pck": b"windows pack of " + version.encode(), "VERSION": f"OpenBFME {version}\n".encode()}),
        make_manifest.asset_name("launcher", version, "linux-x64"): launcher if launcher is not None and not WINDOWS else
            tiny_package(make_manifest.asset_name("launcher", version, "linux-x64"), {"OpenBFMELauncher.x86_64": b"launcher " + version.encode()}),
        make_manifest.asset_name("launcher", version, "windows-x64"): launcher if launcher is not None and WINDOWS else
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
        good = strict(make_manifest.asset_name("game", version, PLATFORM), game_tar(tmp, version))
        make_manifest.package_files = lambda name, data: good if name == asset_name("game", version) else strict(name, data)
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


def launch(tmp: Path, server: ReleaseServer | None, key: bytes | list[bytes], *args: str, exe: Path | None = None, origin: str | None = None,
           timeout: int = 180, env_extra: dict | None = None, headless: bool = True,
           missing_pack_alert: bool = False) -> Run:
    """`key`: the secret of the key the launcher trusts, or (SIGNKMS-1) a list of secrets: --trust-key=<hex>,<hex>..."""
    env = isolated_environment(tmp, OPENBFME_TEST_PLAYED=str(tmp / "played.txt"), **(env_extra or {}))
    test = []
    if origin or server:
        test.append(f"--api-base={origin or server.origin}")
    trusted = key if isinstance(key, list) else [key]
    test += [f"--trust-key={','.join(ed25519.public_key(k).hex() for k in trusted)}", f"--repo={TEST_REPO}"]
    display = ["--headless"] if headless else []
    cmd = [str(exe), *display, "--", *test, *args] if exe else [GODOT, *display, "--path", str(LAUNCHER), "--", *test, *args]
    if not WINDOWS:
        r = subprocess.run(cmd, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace",
                           timeout=timeout, stdin=subprocess.DEVNULL)
        return Run(r.returncode, r.stdout + r.stderr)

    log = data(tmp) / "launcher.log"
    log_offset = log.stat().st_size if log.is_file() else 0
    capture = tmp / f"launcher-output-{time.monotonic_ns()}.log"
    kill_at = next((arg.partition("=")[2] for arg in args if arg.startswith("--test-kill-at=")), None)
    with capture.open("wb") as output:
        process = subprocess.Popen(cmd, env=env, stdout=output, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        deadline = time.monotonic() + timeout
        while process.poll() is None:
            if time.monotonic() >= deadline:
                stop_process(process.pid)
                raise subprocess.TimeoutExpired(cmd, timeout)
            time.sleep(0.025)
        r_code = process.wait(timeout=10)
    wait_for_update = exe is not None and (
        (exe.parent / "update").exists() or (data(tmp) / "launcher-update.json").exists()
    )
    stdout = capture.read_text(encoding="utf-8", errors="replace")
    if wait_for_update:
        helper_pid = None
        for line in stdout.splitlines():
            if "LAUNCHER started the update to " in line and "(pid " in line:
                try:
                    helper_pid = int(line.rsplit("(pid ", 1)[1].split(")", 1)[0])
                except ValueError:
                    pass
        if helper_pid is not None:
            _wait_for_native_update(tmp, log, log_offset, timeout=min(timeout, 90), helper_pid=helper_pid, kill_at=kill_at,
                                    missing_pack_alert=missing_pack_alert)
    try:
        capture.unlink()
    except PermissionError:
        pass
    return Run(r_code, _append_child_log(stdout, _new_launcher_log(log, log_offset)))


def _wait_for_native_update(tmp: Path, log: Path, log_offset: int, timeout: int, helper_pid: int | None = None,
                            kill_at: str | None = None, missing_pack_alert: bool = False) -> None:
    journal = data(tmp) / "launcher-update.json"
    deadline = time.monotonic() + timeout
    last_size = log.stat().st_size if log.is_file() else 0
    quiet_since = time.monotonic()
    while time.monotonic() < deadline:
        size = log.stat().st_size if log.is_file() else 0
        if size != last_size:
            last_size = size
            quiet_since = time.monotonic()
        journal_text = journal.read_text(encoding="utf-8", errors="replace") if journal.is_file() else ""
        terminal = not journal.is_file() or '"confirmed"' in journal_text
        helper_running = helper_pid is not None and process_running(helper_pid)
        if helper_running and missing_pack_alert:
            dismiss_missing_pack_alert(helper_pid)
        killed_at_requested_point = kill_at is not None and f"LAUNCHER test kill at {kill_at}" in _new_launcher_log(log, log_offset)
        completed = terminal or killed_at_requested_point or (
            helper_pid is not None and not helper_running and '"begin"' in journal_text
        )
        if completed and not helper_running and time.monotonic() - quiet_since >= 0.75:
            return
        time.sleep(0.1)
    raise TimeoutError(f"native launcher update did not settle within {timeout}s; journal={journal}, log={log}")


def _new_launcher_log(path: Path, offset: int) -> str:
    if not path.is_file():
        return ""
    raw = path.read_bytes()
    return raw[offset if len(raw) >= offset else 0:].decode("utf-8", "replace")


def _append_child_log(stdout: str, log_text: str) -> str:
    seen = Counter(line for line in stdout.splitlines() if "LAUNCHER " in line)
    extra = []
    for line in log_text.splitlines():
        message = line[line.index("LAUNCHER "):] if "LAUNCHER " in line else line
        if message.startswith("LAUNCHER ") and seen[message]:
            seen[message] -= 1
            continue
        extra.append(message)
    return stdout + (("\n" if stdout and extra else "") + "\n".join(extra))


def data(tmp: Path) -> Path:
    return (tmp / "appdata" if WINDOWS else tmp / "data") / "OpenBFMELauncher"


def installed(tmp: Path) -> list[str]:
    v = data(tmp) / "versions"
    return sorted(p.name for p in v.iterdir()) if v.is_dir() else []


def played(tmp: Path) -> str:
    p = tmp / "played.txt"
    for _ in range(50):  # the game is its own process
        if p.exists() and p.read_text(encoding="utf-8").strip():
            return p.read_text(encoding="utf-8")
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
    # SIGNKMS-1: the envelope and the release signature rule against release_keys.py (the Python twin)
    import release_keys
    vec["envelope"], vec["release"] = [], []
    for kind in release_keys.KINDS:
        for n in (0, 3, 200):
            m = os.urandom(n)
            vec["envelope"].append([kind, m.hex(), release_keys.envelope(kind, m).hex()])
    old, new, unknown = os.urandom(32), os.urandom(32), os.urandom(32)
    lst = [("old", ed25519.public_key(old)), ("new", ed25519.public_key(new))]
    both = ",".join(k.hex() for _, k in lst)
    for kind in release_keys.KINDS:
        data = b'{"a": "' + kind.encode() + b'"}\n'
        cases = [(both, ed25519.sign(old, data), "old key raw"),
                 (both, ed25519.sign(new, release_keys.envelope(kind, data)), "new key envelope"),
                 (both, ed25519.sign(old, release_keys.envelope(kind, data)), "old key envelope"),
                 (both, ed25519.sign(new, data), "new key raw"),
                 (both, ed25519.sign(unknown, release_keys.envelope(kind, data)), "unknown key"),
                 (both, ed25519.sign(new, release_keys.envelope(next(k for k in release_keys.KINDS if k != kind), data)), "wrong kind"),
                 (lst[0][1].hex(), ed25519.sign(new, release_keys.envelope(kind, data)), "new key, old list"),
                 (both, bytes(64), "zeros")]
        for keys_csv, sig, what in cases:
            keys = [(str(i), bytes.fromhex(h)) for i, h in enumerate(keys_csv.split(","))]
            label = release_keys.verify(keys, kind, data, sig)
            vec["release"].append([keys_csv, kind, data.hex(), sig.hex(), -1 if label is None else int(label), f"{kind}: {what}"])
    assert [v[4] for v in vec["release"][:8]] == [0, 1, 0, 1, -1, -1, -1, -1]
    (tmp_path / "vectors.json").write_text(json.dumps(vec))
    r = subprocess.run([GODOT, "--headless", "--path", str(LAUNCHER), "--script", "res://tests/crypto_test.gd", "--",
                        f"--vectors={tmp_path / 'vectors.json'}"], capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=600)
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


def signkms_fixtures(manifest: bytes, deck: bytes, kms: bytes, other: bytes, nightly: bytes, nightly_manifest: bytes, stable_manifest: bytes) -> dict[str, str]:
    """SIGNKMS-1: launcher/tests/unit_test.gd --signkms: signatures of a manifest.json, a manifest-macos.json and a payload.json under
    the old (Deck: raw) and the new (KMS: envelope, release_keys.py) throwaway keys, of the wrong kinds, and of an unknown key"""
    import release_keys
    env = release_keys.envelope
    mac = json.dumps({"format": 3, "product": "OpenBFME", "repo": TEST_REPO, "version": "v1.2.3-preview.4", "channel": "preview",
                      "date": "2026-10-16", "commit": COMMIT, "assets": {"game": {}}}, indent=2).encode() + b"\n"
    pay = json.dumps({"format": 1, "product": "OpenBFME", "repo": TEST_REPO, "version": "v1.2.3-preview.4", "platform": "linux-x64",
                      "files": {"OpenBFME.x86_64": {"size": 1, "sha256": "0" * 64}}}, indent=2).encode() + b"\n"
    sign = ed25519.sign
    fx = {
        "deck": ed25519.public_key(deck), "kms": ed25519.public_key(kms), "other": ed25519.public_key(other),
        "envelope_abc": env("manifest.json", b"abc"), "envelope_of_manifest": env("manifest.json", manifest),
        "manifest": manifest, "manifest_raw": sign(deck, manifest), "manifest_env": sign(kms, env("manifest.json", manifest)),
        "manifest_env_deck": sign(deck, env("manifest.json", manifest)),
        "manifest_env_other": sign(other, env("manifest.json", manifest)), "manifest_raw_other": sign(other, manifest),
        "manifest_env_payload_kind": sign(kms, env("payload.json", manifest)),
        "manifest_env_macos_kind": sign(kms, env("manifest-macos.json", manifest)),
        "macos": mac, "macos_raw": sign(deck, mac), "macos_env": sign(kms, env("manifest-macos.json", mac)),
        "macos_env_wrong_kind": sign(kms, env("manifest.json", mac)),
        "payload": pay, "payload_raw": sign(deck, pay), "payload_env": sign(kms, env("payload.json", pay)),
        "payload_env_wrong_kind": sign(kms, env("manifest.json", pay)),
    }
    # lane RELGH-2: documents signed by the nightly-only key: a nightly release's manifest (a real, schema-valid one), a stable release's,
    # a manifest with a nightly version but channel stable (and one with a preview channel but a non-nightly version), a nightly and a
    # stable payload.json, a nightly macOS sidecar
    nm = json.loads(nightly_manifest)
    nm_stable_channel = json.dumps(dict(nm, channel="stable"), indent=2).encode() + b"\n"
    nm_other_version = json.dumps(dict(nm, version="v1.2.3-preview.4"), indent=2).encode() + b"\n"
    nm_trailing_newline = json.dumps(dict(nm, version=nm["version"] + "\n"), indent=2).encode() + b"\n"
    nm_int_version = json.dumps(dict(nm, version=123), indent=2).encode() + b"\n"
    pay_n = json.dumps(dict(json.loads(pay), version="v1.2.3-nightly.261024100"), indent=2).encode() + b"\n"
    mac_n = json.dumps(dict(json.loads(mac), version="v1.2.3-nightly.20261024"), indent=2).encode() + b"\n"
    fx.update({
        "nightly": ed25519.public_key(nightly),
        "n_manifest": nightly_manifest, "n_manifest_raw": sign(nightly, nightly_manifest),
        "n_manifest_env": sign(nightly, env("manifest.json", nightly_manifest)),
        "n_manifest_env_wrong_kind": sign(nightly, env("payload.json", nightly_manifest)),
        "s_manifest": stable_manifest, "s_manifest_raw": sign(nightly, stable_manifest),
        "s_manifest_env": sign(nightly, env("manifest.json", stable_manifest)),
        "n_chan": nm_stable_channel, "n_chan_raw": sign(nightly, nm_stable_channel),
        "n_otherver": nm_other_version, "n_otherver_raw": sign(nightly, nm_other_version),
        "n_newline": nm_trailing_newline, "n_newline_raw": sign(nightly, nm_trailing_newline),
        "n_intver": nm_int_version, "n_intver_raw": sign(nightly, nm_int_version),
        "n_payload": pay_n, "n_payload_raw": sign(nightly, pay_n), "n_payload_env": sign(nightly, env("payload.json", pay_n)),
        "s_payload_raw": sign(nightly, pay), "s_payload_env": sign(nightly, env("payload.json", pay)),
        "n_macos": mac_n, "n_macos_env": sign(nightly, env("manifest-macos.json", mac_n)),
        "s_macos_env": sign(nightly, env("manifest-macos.json", mac)),
        "n_manifest_other": sign(other, nightly_manifest), "n_manifest_deck": sign(deck, nightly_manifest),
    })
    # lane RELGH-3: the stable Actions key is the list's SECOND stable key and signs RAW (like the Deck key): here "kms" plays it. A stable
    # release's manifest, macOS sidecar and payload, a nightly's manifest (a stable key is valid for every document)
    fx.update({
        "a_manifest_raw": sign(kms, stable_manifest), "a_macos_raw": sign(kms, mac), "a_payload_raw": sign(kms, pay),
        "a_n_manifest_raw": sign(kms, nightly_manifest),
    })
    assert release_keys.verify_detail([("deck", fx["deck"]), ("stable", fx["kms"])], "manifest.json", stable_manifest, fx["a_manifest_raw"]) \
        == ("stable", "raw")
    assert release_keys.verify_detail([("deck", fx["deck"])], "manifest.json", stable_manifest, fx["a_manifest_raw"]) is None
    # the Python twin agrees: a nightly key is valid for the nightly documents only
    nl = [("nightly", fx["nightly"])]
    assert release_keys.verify_scoped(lst0 := [("deck", fx["deck"]), ("kms", fx["kms"])], nl, "manifest.json", nightly_manifest, fx["n_manifest_raw"])
    assert release_keys.verify_scoped(lst0, nl, "manifest.json", stable_manifest, fx["s_manifest_raw"]) is None
    assert release_keys.verify_scoped(lst0, nl, "manifest.json", nm_stable_channel, fx["n_chan_raw"]) is None
    assert release_keys.verify_scoped(lst0, nl, "payload.json", pay, fx["s_payload_raw"]) is None
    assert release_keys.verify_scoped(lst0, nl, "payload.json", pay_n, fx["n_payload_raw"])
    # the envelope signature is what release_keys.verify accepts, and the raw one too (the Python twin of the rule)
    lst = [("deck", fx["deck"]), ("kms", fx["kms"])]
    assert release_keys.verify(lst, "manifest.json", manifest, fx["manifest_env"]) == "kms"
    assert release_keys.verify(lst, "manifest.json", manifest, fx["manifest_raw"]) == "deck"
    assert release_keys.verify(lst, "manifest.json", manifest, fx["manifest_env_payload_kind"]) is None
    return {k: v.hex() for k, v in fx.items()}


@pytest.mark.parametrize("project,script,summary", [
    (LAUNCHER, "log_privacy_test.gd", "PRIVACY OK"),
    (REPO / "godot", "crash_log_test.gd", "CRASH LOG OK"),
])
def test_local_diagnostic_privacy(tmp_path, project, script, summary):
    r = subprocess.run([GODOT, "--headless", "--path", str(project), "--script", f"res://tests/{script}"],
                       env=isolated_environment(tmp_path), capture_output=True, text=True, timeout=60)
    assert r.returncode == 0 and summary in r.stdout, r.stdout + r.stderr
    assert "SCRIPT ERROR" not in r.stderr and "Parse Error" not in r.stderr, r.stderr


def _unit_run(project, tmp_path, keys):
    files = release_files(tmp_path, "v1.2.3-preview.4", keys["release"])
    nightly_files = release_files(tmp_path, "v1.2.3-nightly.20261024", keys["release"])   # lane RELGH-2
    stable_files = release_files(tmp_path, "v1.2.3", keys["release"])
    (tmp_path / "m.json").write_bytes(files["manifest.json"])
    (tmp_path / "m.sig").write_bytes(files["manifest.json.sig"])
    (tmp_path / "signkms.json").write_text(json.dumps(signkms_fixtures(files["manifest.json"], keys["release"], keys["kms"], keys["other"], keys["nightly"],
                                                                       nightly_files["manifest.json"], stable_files["manifest.json"])))
    return subprocess.run([GODOT, "--headless", "--path", str(project), "--script", "res://tests/unit_test.gd", "--",
                        f"--key={ed25519.public_key(keys['release']).hex()}", f"--manifest={tmp_path / 'm.json'}",
                        f"--sig={tmp_path / 'm.sig'}", f"--signkms={tmp_path / 'signkms.json'}"], env=isolated_environment(tmp_path),
                       capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300)


def _check_unit_run(r):
    output = r.stdout + r.stderr
    assert "SCRIPT ERROR" not in output and "Parse Error" not in output, output
    assert r.returncode == 0 and "UNIT OK" in r.stdout, output[-5000:]
    print(r.stdout[-3000:])


def test_launcher_unit_rules(tmp_path, keys):
    r = _unit_run(LAUNCHER, tmp_path, keys)
    _check_unit_run(r)
    assert "UNIT signkms: the key-list checks run" in r.stdout, r.stdout[-3000:]
    assert "UNIT stable actions: the stable Actions key checks run" in r.stdout, r.stdout[-3000:]


@pytest.mark.parametrize("error", ["SCRIPT ERROR", "Parse Error"])
@pytest.mark.parametrize("stream", ["stdout", "stderr"])
def test_unit_wrapper_rejects_error_output(error, stream, capsys):
    output = {"stdout": "UNIT OK 1 checks\n", "stderr": ""}
    output[stream] += error + ": deliberately broken test\n"
    result = subprocess.CompletedProcess([], 0, **output)
    with pytest.raises(AssertionError, match=error):
        _check_unit_run(result)
    assert "UNIT OK" not in capsys.readouterr().out


@pytest.mark.parametrize("error", ["runtime", "parse"])
def test_unit_wrapper_rejects_broken_test(tmp_path, keys, error):
    project = tmp_path / "broken-launcher"
    shutil.copytree(LAUNCHER, project, ignore=shutil.ignore_patterns(".godot"))
    script = project / "tests/unit_test.gd"
    source = script.read_text(encoding="utf-8")
    if error == "runtime":
        assert 'screen.build("")' in source
        source = source.replace('screen.build("")', '# Deliberately reproduce the unbuilt-screen script error.')
    else:
        source += "\nfunc deliberately_broken( -> void:\n"
    script.write_text(source, encoding="utf-8")
    result = _unit_run(project, tmp_path, keys)
    marker = "SCRIPT ERROR" if error == "runtime" else "Parse Error"
    assert marker in result.stdout + result.stderr, result
    assert "UNIT OK" not in result.stdout + result.stderr, result
    assert result.returncode != 0, result
    with pytest.raises(AssertionError, match=marker):
        _check_unit_run(result)


@pytest.mark.skipif(WINDOWS, reason="POSIX FIFO lifetime control")
def test_slow_game_waits_for_exit_event(tmp_path):
    game = tmp_path / "game"
    game.write_bytes(slow_game())
    game.chmod(0o755)
    played = tmp_path / "played.txt"
    with subprocess.Popen([str(game)], env=isolated_environment(tmp_path, OPENBFME_TEST_PLAYED=str(played)),
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) as process:
        event = played.with_name(f"{played.name}.{process.pid}.exit")
        try:
            deadline = time.monotonic() + 10
            while not event.exists() and process.poll() is None and time.monotonic() < deadline:
                time.sleep(0.01)
            assert event.is_fifo() and process.poll() is None
            with event.open("w") as signal:
                signal.write("exit\n")
            assert process.wait(timeout=10) == 0
            assert not event.exists()
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)


@pytest.mark.skipif(sys.platform != "linux", reason="the synthetic argv executable is a Linux fixture; Windows is cross-compiled")
def test_install_paths_preserve_text_and_process_arguments():
    r = subprocess.run([sys.executable, str(HERE / "test_install_paths.py"), "--godot", GODOT],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)
    assert r.returncode == 0, r.stdout + r.stderr
    assert "PATH FIRST-RUN checks=66 failed=0 cases=11" in r.stdout
    assert "PATH LAUNCHER checks=127 failed=0 cases=11" in r.stdout


@pytest.mark.parametrize(("script", "verdict"), [
    ("oauth_test.gd", r"OAUTH PASS \d+ checks"),
    ("social_launch_test.gd", r"SOCIAL LAUNCH OK"),
    ("social_ui_test.gd", r"SOCIAL UI OK"),
    ("macos_preview_test.gd", r"MAC PREVIEW UNIT OK"),
    ("lobby_list_test.gd", r"LAUNCHER LIST: \d+ passed, 0 failed"),
])
def test_standalone_launcher_scripts(tmp_path, script, verdict):
    result = subprocess.run([GODOT, "--headless", "--path", str(LAUNCHER), "--script", f"res://tests/{script}"],
                            env=isolated_environment(tmp_path), capture_output=True, text=True, timeout=90, check=False)
    output = result.stdout + result.stderr
    assert not re.search(r"(?m)^\s*(?:SCRIPT ERROR|ERROR):", output), output
    if result.returncode == 77:
        assert script == "macos_preview_test.gd" and "native bundle checks SKIP: macOS required" in output, output
        pytest.skip("macOS preview: portable checks passed; native bundle checks require macOS")
    assert result.returncode == 0 and re.search(verdict, output), output


@pytest.mark.skipif(sys.platform != "linux", reason="short-write regression requires Linux RLIMIT_FSIZE")
def test_launcher_write_failures(tmp_path):
    # Set the limit in a single-threaded child, not preexec_fn in the threaded release-server process.
    limit = ("import os, resource, signal, sys; signal.signal(signal.SIGXFSZ, signal.SIG_IGN); "
             "resource.setrlimit(resource.RLIMIT_FSIZE, (4096, 4096)); os.execv(sys.argv[1], sys.argv[1:])")
    godot = shutil.which(GODOT) or GODOT  # os.execv takes a path, not a PATH name like the CI's "godot"
    result = subprocess.run([sys.executable, "-c", limit, godot, "--headless", "--path", str(LAUNCHER), "--script",
                             "res://tests/write_failure_test.gd", "--", "--write-limited"],
                            env=isolated_environment(tmp_path), capture_output=True, text=True, timeout=60, check=False)
    output = result.stdout + result.stderr
    assert result.returncode == 0 and "WRITE FAILURE PASS: 2 checks (settings=true, icon=true)" in output, output
    assert not re.search(r"(?m)^\s*(?:SCRIPT ERROR|ERROR):", output), output


@pytest.mark.parametrize("runner", ["test_launcher_social.py", "test_launcher_oauth.py"])
def test_launcher_social_integrations(tmp_path, runner):
    if runner == "test_launcher_oauth.py":
        from test_launcher_oauth import node_command, safe_output
        try:
            node_command()
        except RuntimeError as error:
            pytest.skip(f"SKIP LOUDLY: launcher OAuth integration needs Node.js with node:sqlite support; {error}")
    # the runners check --godot as a file (test_launcher_oauth.py: is_file): a PATH name like the CI's "godot" is resolved first, as above
    result = subprocess.run([sys.executable, str(HERE / runner), "--godot", shutil.which(GODOT) or GODOT],
                            env=isolated_environment(tmp_path), capture_output=True, text=True, timeout=240, check=False)
    output = result.stdout + result.stderr
    if runner == "test_launcher_oauth.py":
        output = safe_output(output)
    verdict = "OAUTH INTEGRATION PASS" if runner == "test_launcher_oauth.py" else "PASS: persistent identity"
    assert result.returncode == 0 and verdict in output, output


def test_oauth_without_node_skips_loudly(monkeypatch, tmp_path):
    monkeypatch.delenv("NODE", raising=False)
    monkeypatch.setenv("PATH", str(tmp_path))
    with pytest.raises(pytest.skip.Exception, match="SKIP LOUDLY: launcher OAuth integration needs Node.js.*not found"):
        test_launcher_social_integrations(tmp_path, "test_launcher_oauth.py")


def _layout_run(project: Path, tmp_path: Path, *args: str) -> subprocess.CompletedProcess:
    """launcher/tests/layout_test.gd on `project` (a neutral home folder: nothing of the user's is read or written)"""
    home = tmp_path / "layout-home"
    home.mkdir(exist_ok=True)
    env = {k: v for k, v in os.environ.items() if k not in ("ROTWK_INSTALL", "BFME2_INSTALL", "OPENBFME_GAME_USER_DIR")}
    env.update(HOME=str(home), XDG_DATA_HOME=str(home / ".local/share"), XDG_CONFIG_HOME=str(home / ".config"),
               XDG_CACHE_HOME=str(home / ".cache"))
    subprocess.run([GODOT, "--headless", "--path", str(project), "--import"], env=env, capture_output=True, timeout=300)
    return subprocess.run([GODOT, "--headless", "--path", str(project), "--script", "res://tests/layout_test.gd", "--", *args],
                          env=env, capture_output=True, text=True, timeout=900, stdin=subprocess.DEVNULL)


LAYOUT_STATES = ["main", "news", "multiplayer", "mods", "games", "games-bfme1", "games-downloading", "settings", "display", "about", "update", "downloading",
                 "error", "glibc", "first-run", "offline", "verifying", "paused", "update-launcher", "warning", "ask", "playing", "long",
                 "picker", "update-launcher+123", "update-launcher+10000000", "long+123", "long+10000000"]
LAYOUT_SIZES = ["1280x720", "1280x800", "1920x1080", "2560x1440", "3440x1440", "960x540", "960x1000", "2704x1521", "3146x1769"]
LAYOUT_LAYERS = {"error": ["main", "dialog"], "glibc": ["main", "dialog"], "ask": ["main", "dialog"], "picker": ["main", "picker"]}


def _layout_cases(stdout: str) -> dict[str, dict[str, int]]:
    """the `LAYOUT case <name>@<WxH> main=<n> ...` lines: case -> layer -> controls measured"""
    out = {}
    for line in stdout.splitlines():
        if line.startswith("LAYOUT case "):
            name, *layers = line[len("LAYOUT case "):].split()
            out[name] = {k: int(v) for k, v in (x.split("=") for x in layers)}
    return out


def test_launcher_layout_lines_up_in_every_state_and_size(tmp_path):
    """lane LAUNCHERUI-1: the window's own layout check (cells where their size flags put them, gaps equal the separation, padding is the
    theme's, shared margins, nothing overlaps or sticks out, no text is cut without its tooltip, icons are centred) over every demo state
    and the online-players chip at every window size, with no finding. The inventory is this test's own: every (state, size) pair by name,
    each with the layers it must have (window; dialog; folder picker) and more than zero controls measured in each."""
    r = _layout_run(LAUNCHER, tmp_path, "--ui-demo=main")
    findings = [x for x in r.stdout.splitlines() if x.startswith("LAYOUT finding")]
    assert r.returncode == 0 and not findings, "\n".join(findings) + r.stdout[-1500:] + r.stderr[-1500:]
    cases = _layout_cases(r.stdout)
    want = {f"{st}@{sz}": LAYOUT_LAYERS.get(st.split("+")[0], ["main"]) for st in LAYOUT_STATES for sz in LAYOUT_SIZES}
    assert sorted(cases) == sorted(want), sorted(set(cases) ^ set(want))
    for name, layers in want.items():
        assert list(cases[name]) == layers and all(n > 0 for n in cases[name].values()), (name, cases[name])
    assert f"LAYOUT OK {len(LAYOUT_STATES)} cases x {len(LAYOUT_SIZES)} sizes," in r.stdout, r.stdout[-500:]


def _sabotaged(tmp_path: Path, name: str, edits: list[tuple[str, str, str]]) -> subprocess.CompletedProcess:
    """a copy of the launcher project with `edits` (file, old, new) applied (each must apply), and the layout check run on it"""
    copy = tmp_path / name
    shutil.copytree(LAUNCHER, copy, ignore=shutil.ignore_patterns(".godot"))
    for rel, old, new in edits:
        f = copy / rel
        text = f.read_text()
        assert old in text, (rel, old)
        f.write_text(text.replace(old, new, 1))
    return _layout_run(copy, tmp_path, "--ui-demo=main")


@pytest.mark.parametrize("name, edits, expect", [
    # Sol's cases: a card 20 px (not 6) off the right margin; the bottom bar and PLAY 20 px off it; the rail's logo off its entries
    ("card-20", [("scripts/ui/play_screen.gd", "\tadd_child(_notes_card)\n", "\tvar wrap := MarginContainer.new()\n\twrap.add_theme_constant_override(\"margin_right\", 20)\n"
                  "\twrap.add_child(_notes_card)\n\tadd_child(wrap)\n")], ["screen blocks do not span the margins"]),
    ("bar-20", [("scripts/main.gd", 'bm.add_theme_constant_override("margin_right", SCREEN_MARGIN)', 'bm.add_theme_constant_override("margin_right", SCREEN_MARGIN + 20)')],
     ["margins are not the window's", "PLAY is off the right margin"]),
    ("rail", [("scripts/main.gd", "const RAIL_INK := Style.NAV_PAD", "const RAIL_INK := 9")], ["the rail does not start on one x"]),
    ("sidebar-pitch", [("scripts/ui/style.gd", "const NAV_GAP := 2", "const NAV_GAP := 10")], ["sidebar vertical pitch is not 38"]),
    ("sidebar-width", [("scripts/main.gd", 'var foot := W.trim_label(L10n.t("launcher.main.footer_version"', 'var foot := W.label(L10n.t("launcher.main.footer_version"')],
     ["sidebar button exceeds rail", "sidebar insets are not 13"]),
    # a screen's column with a gap of its own
    ("gap", [("scripts/ui/settings_screen.gd", "var v := W.vbox(Style.SCREEN_GAP)", "var v := W.vbox(20)")], ["gap is not the theme's SCREEN_GAP"]),
    # the demo states that must apply: the offline state never applies; the folder picker never opens
    ("offline-not-applied", [("scripts/main.gd", '\t\t"offline":\n', '\t\t"offline-gone":\n')], ["does not show 'Could not check for updates'"]),
    ("picker-not-opened", [("scripts/main.gd", "_games_screen.open_picker(true)", "pass")], ["the layers are", "the folder picker is not open"]),
    # nothing to measure
    ("no-sizes", [("tests/layout_test.gd", "const WINDOWS := [Vector2i(1280, 720), Vector2i(1280, 800), Vector2i(1920, 1080), Vector2i(2560, 1440), Vector2i(3440, 1440),\n\tVector2i(960, 540), Vector2i(960, 1000),\n\tVector2i(2704, 1521), Vector2i(3146, 1769)]", "const WINDOWS := []")], ["nothing to measure"]),
])
def test_launcher_layout_check_fails_closed(tmp_path, name, edits, expect):
    """the layout check is not a rubber stamp: each sabotaged copy of the project (defects of any size, a state that never applies, a
    picker that never opens, an empty size list) fails by name"""
    r = _sabotaged(tmp_path, name, edits)
    assert r.returncode != 0 and "LAYOUT FAIL" in r.stdout, r.stdout[-2500:]
    for text in expect:
        assert text in r.stdout, (text, r.stdout[-3000:])


def test_launcher_layout_check_needs_its_demo_option(tmp_path):
    r = _layout_run(LAUNCHER, tmp_path)
    assert r.returncode != 0 and "LAYOUT FAIL" in r.stdout and not _layout_cases(r.stdout), r.stdout[-1500:]


def test_online_link_injection_runs_nothing(tmp_path, server, keys):
    """lane ONLINE-2 r2 (Sol r1): Windows splices the link into "<launcher>" -- --open-url "%1", so a link with quotes adds launcher options. A link
    is only taken alone (--open-url <link>); with anything else the whole start is refused before any CLI action runs: no update, no install, no
    lobby change, no game started."""
    server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["release"]))
    link = "openbfme://join/0123456789abcdef"
    for injected in ([link + '"', "--update"], ["--open-url", link, "--update"], ["--open-url", link, "--install=v0.2.0"],
                     ["--update", "--open-url", link], ["--open-url", link, "--lobby=https://evil.example"], [link, "--update"]):
        args = injected if injected[0] == "--open-url" or injected[0] == "--update" else ["--open-url", *injected]
        r = launch(tmp_path, server, keys["release"], *args)
        assert r.code == 1 and r.has("link refused, nothing done"), (args, r)
        assert not r.has("update installed") and installed(tmp_path) == [], (args, r)
        assert not r.has("online lobby:") and not r.has("join game"), (args, r)
    assert not (tmp_path / "played.txt").exists()
    # alone, the link is taken; with no lobby set it says so and starts nothing
    r = launch(tmp_path, server, keys["release"], "--open-url", link)
    assert r.code == 1 and r.has("no online lobby is set"), r


def test_fresh_install_and_play(tmp_path, server, keys):
    server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["release"]))
    r = launch(tmp_path, server, keys["release"], "--update", "--list", "--play")
    assert r.code == 0 and r.has("update installed v0.2.0") and r.has("installed v0.2.0"), r
    assert installed(tmp_path) == ["v0.2.0"]
    game = data(tmp_path) / "versions/v0.2.0/game"
    assert (game / "OpenBFME.pck").read_bytes() == b"pack of v0.2.0"
    assert WINDOWS or os.access(game / GAME_EXE, os.X_OK), "the executable bit of the archive is kept"
    assert "played game" in played(tmp_path)
    signed = json.loads((data(tmp_path) / "versions/v0.2.0/manifest.json").read_text(encoding="utf-8"))["assets"]["game"][PLATFORM]["files"]
    assert signed["OpenBFME.pck"] == {"size": 14, "sha256": hashlib.sha256(b"pack of v0.2.0").hexdigest()}
    assert not list((data(tmp_path) / "downloads").glob("*.part")), "the verified download is removed after the install"
    agents = {h.get("user-agent") for _, h in server.requests}
    assert agents == {"OpenBFME-Launcher/v0.0.0-dev (+https://github.com/Open-BFME/openbfme-godot)"}, agents
    assert any(p.startswith(f"/repos/{TEST_REPO}/releases") for p, _ in server.requests)


def test_play_checks_the_systems_glibc(tmp_path, server, keys):
    if WINDOWS:
        pytest.skip("intrinsically Linux-only: GLIBC rejection behavior")
    """GLIBC-1: a package whose VERSION says "requires GLIBC 2.35" is not started on an older system: the message names both versions"""
    game = game_tar(tmp_path, "v0.2.0", version_extra="requires GLIBC 2.35\n")
    server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["release"], game=game))
    r = launch(tmp_path, server, keys["release"], "--update", "--play", env_extra={"OPENBFME_TEST_SYSTEM_GLIBC": "2.31"})
    assert r.code == 1 and r.has("this Linux system is too old for this build. "
                                "Update to a Linux distribution with GLIBC 2.35 or newer (this system has 2.31). "
                                "If you need help, use the OpenBFME Discord #bug-reports forum or GitHub issues."), r
    assert played(tmp_path) == ""
    r = launch(tmp_path, None, keys["release"], "--play", env_extra={"OPENBFME_TEST_SYSTEM_GLIBC": "2.35"})
    assert r.code == 0 and "played game" in played(tmp_path), r


def test_play_skips_the_glibc_check_off_linux(tmp_path, server, keys):
    """DEP-1 (Sol r1): the GLIBC check is for Linux only; platform() is "linux-x64" on macOS too, which has no GLIBC to compare"""
    game = game_tar(tmp_path, "v0.2.0", version_extra="requires GLIBC 2.28\n")
    server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["release"], game=game))
    r = launch(tmp_path, server, keys["release"], "--update", "--play",
               env_extra={"OPENBFME_TEST_SYSTEM_GLIBC": "2.17", "OPENBFME_TEST_OS_NAME": "macOS"})
    assert r.code == 0 and "GLIBC" not in r.out and "played game" in played(tmp_path), r


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


def test_preview_channel_gets_the_alpha_builds(tmp_path, server, keys):
    """RELNAME-1 (owner, 2026-10-10): the builds after v0.3.0-preview.4 are v0.4.0-alpha.<n>; a player on the preview channel gets them
    with nothing to do (the channel id stays "preview", shown as Alpha), a stable player does not"""
    server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["release"]))
    server.publish("v0.3.0-preview.4", release_files(tmp_path, "v0.3.0-preview.4", keys["release"]))
    r = launch(tmp_path, server, keys["release"], "--channel=preview", "--update")
    assert r.code == 0 and r.has("update installed v0.3.0-preview.4"), r
    server.publish("v0.4.0-alpha.1", release_files(tmp_path, "v0.4.0-alpha.1", keys["release"]))
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 0 and r.has("update installed v0.4.0-alpha.1") and not r.has("skipped release"), r
    assert installed(tmp_path) == ["v0.3.0-preview.4", "v0.4.0-alpha.1"]
    server.publish("v0.4.0-alpha.2", release_files(tmp_path, "v0.4.0-alpha.2", keys["release"]))
    r = launch(tmp_path, server, keys["release"], "--channel=preview", "--update")
    assert r.code == 0 and r.has("channel preview") and r.has("update installed v0.4.0-alpha.2"), r
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 0 and r.has("up to date: v0.4.0-alpha.2 installed"), r
    # VERSION-1: "Alpha" is now the shown name of the stable channel (the plain releases, "Alpha 0.4.5"); it works as well
    r = launch(tmp_path, server, keys["release"], "--channel=alpha", "--list")
    assert r.code == 0 and r.has("channel stable"), r
    other = tmp_path / "stable"
    other.mkdir()
    r = launch(other, server, keys["release"], "--channel=stable", "--update")
    assert r.code == 0 and r.has("update installed v0.2.0"), r


def test_nightly_channel_name(tmp_path, server, keys):
    """NIGHTLY-1 (owner, 2026-10-21): --channel=nightly is the channel id "preview"; a nightly build is offered to it and not to stable"""
    server.publish("v0.4.12", release_files(tmp_path, "v0.4.12", keys["release"]))
    server.publish("v0.4.13-nightly.20261022", release_files(tmp_path, "v0.4.13-nightly.20261022", keys["release"]))
    r = launch(tmp_path, server, keys["release"], "--channel=nightly", "--update")
    assert r.code == 0 and r.has("channel preview") and r.has("update installed v0.4.13-nightly.20261022"), r
    other = tmp_path / "stable"
    other.mkdir()
    r = launch(other, server, keys["release"], "--channel=stable", "--update")
    assert r.code == 0 and r.has("update installed v0.4.12"), r
    r = launch(other, server, keys["release"], "--channel=bogus", "--list")
    assert r.code == 1 and r.has("unknown channel 'bogus' (stable, nightly)"), r


def test_the_nightly_key_signs_nightly_releases_only(tmp_path, server, keys):
    """RELGH-2: a key trusted for nightly releases only (--trust-nightly-key, a test build's twin of build_info.json nightly_keys): a
    nightly release it signed is installed on the nightly channel; the same key on a stable release is refused by name; and without the
    option (a launcher that does not list the key) the nightly release is refused too"""
    nk = f"--trust-nightly-key={ed25519.public_key(keys['nightly']).hex()}"
    server.publish("v0.4.13-nightly.20261022", release_files(tmp_path, "v0.4.13-nightly.20261022", keys["nightly"]))
    r = launch(tmp_path, server, keys["release"], "--channel=nightly", "--update")
    assert r.code == 3 and r.has("signature is not valid") and installed(tmp_path) == [], r
    r = launch(tmp_path, server, keys["release"], "--channel=nightly", "--update", nk)
    assert r.code == 0 and r.has("update installed v0.4.13-nightly.20261022"), r
    other = tmp_path / "stable"
    other.mkdir()
    server.publish("v0.4.14", release_files(tmp_path, "v0.4.14", keys["nightly"]))
    r = launch(other, server, keys["release"], "--channel=stable", "--update", nk)
    assert r.code == 3 and r.has("signature is not valid") and installed(other) == [], r


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
    name = make_manifest.asset_name("game", "v0.2.0", PLATFORM)
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


def kms_slot(files: dict[str, bytes], kms: bytes, sig: bytes | None = b"") -> dict[str, bytes]:
    """SIGNKMS-1: a release signed the new way: manifest.json.sig2 is the KMS key's envelope signature (release_keys.py); manifest.json.sig
    is left as it is (b""), removed (None) or replaced by `sig`"""
    import release_keys
    out = dict(files)
    out["manifest.json.sig2"] = ed25519.sign(kms, release_keys.envelope("manifest.json", files["manifest.json"]))
    if sig is None:
        del out["manifest.json.sig"]
    elif sig:
        out["manifest.json.sig"] = sig
    return out


@pytest.mark.parametrize("slot", ["sig missing", "sig invalid", "sig unknown key"])
def test_a_release_signed_by_the_new_key_is_taken_from_its_second_slot(tmp_path, server, keys, slot):
    """SIGNKMS-1: a launcher trusting the list (old key, new key) installs and plays a release whose manifest.json.sig is missing or not
    valid and whose manifest.json.sig2 is the new key's envelope signature; the verified signature is kept as manifest.json.sig"""
    sig = {"sig missing": None, "sig invalid": bytes(64), "sig unknown key": ed25519.sign(keys["other"], b"x")}[slot]
    files = kms_slot(release_files(tmp_path, "v0.2.0", keys["release"]), keys["kms"], sig)
    server.publish("v0.2.0", files)
    r = launch(tmp_path, server, [keys["release"], keys["kms"]], "--update", "--list", "--play")
    assert r.code == 0 and r.has("update installed v0.2.0") and "played game" in played(tmp_path), r
    assert (data(tmp_path) / "versions/v0.2.0/manifest.json.sig").read_bytes() == files["manifest.json.sig2"]
    assert (data(tmp_path) / "cache/manifests/v0.2.0/manifest.json.sig").read_bytes() == files["manifest.json.sig2"]
    r = launch(tmp_path, None, [keys["release"], keys["kms"]], "--list", "--play")   # offline: the stored signature verifies again
    assert r.code == 0 and r.has("installed v0.2.0"), r


def test_a_release_with_both_slots_is_read_from_the_first(tmp_path, server, keys):
    """SIGNKMS-1: a valid manifest.json.sig (the old key, raw) is used as it is; manifest.json.sig2 is not even downloaded. An old
    launcher (the old key alone) reads the same release"""
    files = kms_slot(release_files(tmp_path, "v0.2.0", keys["release"]), keys["kms"])
    files["manifest.json.sig2"] = bytes(64)   # broken: would fail if it were read
    server.publish("v0.2.0", files)
    r = launch(tmp_path, server, [keys["release"], keys["kms"]], "--update")
    assert r.code == 0 and r.has("update installed v0.2.0"), r
    assert not any(p.endswith("/manifest.json.sig2") for p, _ in server.requests), "the second slot is not downloaded"
    assert (data(tmp_path) / "versions/v0.2.0/manifest.json.sig").read_bytes() == files["manifest.json.sig"]
    r = launch(tmp_path / "old", server, keys["release"], "--update")
    assert r.code == 0 and r.has("update installed v0.2.0"), r


def test_a_release_with_no_valid_slot_is_refused(tmp_path, server, keys):
    """SIGNKMS-1: neither slot valid (a broken manifest.json.sig, the second slot by an unknown key; or only the new key's signature for
    a launcher trusting the old key alone): refused, both slots named, nothing downloaded"""
    files = kms_slot(release_files(tmp_path, "v0.2.0", keys["release"]), keys["other"], bytes(64))
    server.publish("v0.2.0", files)
    r = launch(tmp_path, server, [keys["release"], keys["kms"]], "--update")
    assert r.code == 3 and r.has("signature is not valid") and r.has("manifest.json.sig: not valid; manifest.json.sig2: not valid"), r
    assert installed(tmp_path) == []
    assert not any("/dl/v0.2.0/openbfme-v0.2.0" in p for p, _ in server.requests), "no package is downloaded for a bad manifest"
    server.publish("v0.2.0", kms_slot(release_files(tmp_path, "v0.2.0", keys["release"]), keys["kms"], None))
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 3 and r.has("manifest.json.sig: not in the release; manifest.json.sig2: not valid"), r
    assert installed(tmp_path) == []


def test_manifest_for_another_repository_is_rejected(tmp_path, server, keys):
    server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["release"], repo="Other/fork"))
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 3 and r.has("the manifest is for Other/fork"), r


def test_truncated_download_resumes(tmp_path, server, keys):
    files = release_files(tmp_path, "v0.2.0", keys["release"], game=game_tar(tmp_path, "v0.2.0", extra=os.urandom(300000)))
    name = make_manifest.asset_name("game", "v0.2.0", PLATFORM)
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
    server.truncate[f"/dl/v0.3.0/{make_manifest.asset_name('game', 'v0.3.0', PLATFORM)}"] = 50000
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
    archive = evil_tar("v0.2.0", member, kind)
    if WINDOWS and "\\" in member:
        raw_name = member.replace("openbfme-v0.2.0-linux-x64", f"openbfme-v0.2.0-{PLATFORM}")
        with zipfile.ZipFile(io.BytesIO(archive)) as z:
            assert z.infolist()[-1].orig_filename == raw_name
    server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["release"], game=archive, lenient=True))
    r = launch(tmp_path, server, keys["release"], "--update")
    expected = "is a symbolic link" if WINDOWS and kind == "symlink" else why
    assert r.code == 3 and r.has(expected) and r.has("v0.2.0 was not installed"), r
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
    name = make_manifest.asset_name("game", "v0.2.0", PLATFORM)
    port = server.httpd.server_address[1]
    server.redirect[f"/dl/v0.2.0/{name}"] = f"http://localhost:{port}/dl/v0.2.0/{name}"
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 3 and r.has(f"refused URL 'http://localhost:{port}/dl/v0.2.0/{name}': not HTTPS"), r
    server.redirect[f"/dl/v0.2.0/{name}"] = f"/elsewhere/{name}"  # a relative redirect on the same origin is followed
    server.files[f"/elsewhere/{name}"] = files[name]
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 0 and r.has("update installed v0.2.0"), r


def test_test_origin_must_be_loopback(tmp_path, keys):
    r = launch(tmp_path, None, keys["release"], "--update", origin="http://example.com:80")
    assert r.code == 1 and r.has("the test API origin must be http://<address>:<port>"), r


# ---- the packaged launcher: self-update and the release build ---------------------------------------------------------------------------

def _templates_ok() -> bool:
    if WINDOWS:
        base = Path(os.environ.get("APPDATA", Path.home() / "AppData/Roaming")) / "Godot/export_templates/4.7.2.stable"
        return (base / "windows_release_x86_64.exe").is_file()
    base = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")) / "godot/export_templates/4.7.2.stable"
    return (base / "linux_release.x86_64").is_file()


def build_launcher(out: Path, version: str, key: bytes, test_build: bool = True) -> bytes:
    (out / "key.pub").parent.mkdir(parents=True, exist_ok=True)
    (out / "key.pub").write_text(ed25519.public_key(key).hex() + "\n")
    if WINDOWS:
        from launcher_native_export import export_launcher
        return export_launcher(REPO, out, version, COMMIT, TEST_REPO, ed25519.public_key(key).hex(), GODOT, 1700000000, test_build)
    cmd = [str(HERE / "build_launcher.sh"), "--out", str(out), "--version", version, "--commit", COMMIT, "--repo", TEST_REPO,
           "--key-file", str(out / "key.pub"), "--mtime", "1700000000", "--platforms", "linux", "--worktree", "--godot", GODOT]
    if test_build:
        cmd.append("--test-build")
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
    assert r.returncode == 0, r.stdout[-3000:] + r.stderr[-3000:]
    return (out / f"openbfme-launcher-{version}-{PLATFORM}{ARCHIVE_SUFFIX}").read_bytes()


@pytest.fixture(scope="module")
def launcher_builds(tmp_path_factory, keys):
    if not _templates_ok():
        pytest.skip(f"SKIP self-update tests: the Godot 4.7.2 {PLATFORM} export template is not installed")
    d = tmp_path_factory.mktemp("launchers")
    builds = {v: build_launcher(d / v, v, keys["release"]) for v in ("v0.2.0", "v0.3.0")} | \
        {"release": build_launcher(d / "release", "v0.2.0", keys["release"], test_build=False)}
    shutil.rmtree(d, ignore_errors=True)  # the archives are kept in memory
    yield builds


# RELNAME-1 r2: the launchers already published, built from their own code. Two codebases shipped: the launcher/ tree of the tags
# v0.3.0-preview.1 and .2 is the archive's 42f8a8d2, that of .3 and .4 is 2a422ce6 (the tree ids pin it: a different tree is not the
# shipped launcher). Both take only v<x>.<y>.<z>[-preview.<n>] and read one page of 30 releases.
SHIPPED = {"v0.3.0-preview.2": ("42f8a8d2c6f96f55e0fa4d597f54d97ac6a772ff", "4bc620603c6824421b967bb174f9ed42bea91b9e"),
           "v0.3.0-preview.4": ("2a422ce615dd60950a879bcd87a10da53d5b54b0", "b10f453de143a794ce9dd4e02baacc9fa9fd57ce")}


def build_shipped_launcher(out: Path, version: str, key: bytes) -> bytes:
    commit, tree = SHIPPED[version]
    r = subprocess.run(["git", "-C", str(REPO), "rev-parse", f"{commit}:launcher"], capture_output=True, text=True)
    assert r.returncode == 0 and r.stdout.strip() == tree, f"the launcher of {version} ({commit}) is not in this clone: {r.stderr}"
    (out / "key.pub").parent.mkdir(parents=True, exist_ok=True)
    (out / "key.pub").write_text(ed25519.public_key(key).hex() + "\n")
    if WINDOWS:
        from launcher_native_export import export_launcher
        return export_launcher(REPO, out, version, COMMIT, TEST_REPO, ed25519.public_key(key).hex(),
                               GODOT, 1700000000, True, source_commit=commit)
    r = subprocess.run([str(HERE / "build_launcher.sh"), "--out", str(out), "--version", version, "--commit", COMMIT, "--repo", TEST_REPO,
                        "--key-file", str(out / "key.pub"), "--mtime", "1700000000", "--platforms", "linux", "--source", commit,
                        "--godot", GODOT, "--test-build"], capture_output=True, text=True, timeout=900)
    assert r.returncode == 0, r.stdout[-3000:] + r.stderr[-3000:]
    return (out / f"openbfme-launcher-{version}-{PLATFORM}{ARCHIVE_SUFFIX}").read_bytes()


@pytest.fixture(scope="module")
def shipped_launchers(tmp_path_factory, keys):
    if not _templates_ok():
        pytest.skip(f"SKIP the published launchers' tests: the Godot 4.7.2 {PLATFORM} export templates are not installed")
    d = tmp_path_factory.mktemp("shipped")
    builds = {v: build_shipped_launcher(d / v, v, keys["release"]) for v in SHIPPED}
    builds["v0.3.0-preview.5"] = build_launcher(d / "bridge", "v0.3.0-preview.5", keys["release"])  # this tree: knows -alpha
    shutil.rmtree(d, ignore_errors=True)
    yield builds


@pytest.mark.parametrize("old", list(SHIPPED))
def test_a_published_launcher_reaches_the_alpha_builds_through_the_bridge(tmp_path, server, keys, shipped_launchers, old):
    """Sol r1: the launchers of v0.3.0-preview.1 to .4 skip -alpha tags; the bridge v0.3.0-preview.5 (a launcher that knows them) is
    installed and swapped in by the old launcher itself, which then offers v0.4.0-alpha.1"""
    inst = unpack_launcher(tmp_path / "inst", shipped_launchers[old])
    exe = inst / LAUNCHER_EXE
    server.publish("v0.3.0-preview.5", release_files(tmp_path, "v0.3.0-preview.5", keys["release"], launcher=shipped_launchers["v0.3.0-preview.5"]))
    server.publish("v0.4.0-alpha.1", release_files(tmp_path, "v0.4.0-alpha.1", keys["release"]))
    # preview.1 and .2 start on Stable until the player picks Preview (RELTEST-1 changed that in .3): a player who never did saw no
    # build at all and still does not; every other one is on the preview channel
    r = launch(tmp_path, server, keys["release"], "--channel=preview", "--update", "--self-update", exe=exe)
    assert r.code == 0 and r.has("skipped release 'v0.4.0-alpha.1'"), r
    assert r.has("update installed v0.3.0-preview.5") and r.has("launcher update v0.3.0-preview.5 staged"), r
    r = launch(tmp_path, server, keys["release"], "--version", exe=exe, headless=not WINDOWS)   # old Windows builds swap with a display
    assert r.has(f"replaced {old} by v0.3.0-preview.5") and r.has("the new launcher v0.3.0-preview.5 confirmed its start"), r
    assert hashlib.sha256(exe.read_bytes()).hexdigest() == exe_sha(shipped_launchers["v0.3.0-preview.5"])
    r = launch(tmp_path, server, keys["release"], "--update", "--self-update", exe=exe)
    assert r.code == 0 and not r.has("skipped release"), r
    assert r.has("update installed v0.4.0-alpha.1") and r.has("launcher update v0.4.0-alpha.1 staged"), r


def test_a_late_player_finds_the_newest_bridge(tmp_path, server, keys, shipped_launchers):
    """Sol r1: an old launcher reads 30 releases; a bridge that fell off that page is invisible to it. autorelease.py's bridge rule,
    run on the same list, asks for a new bridge before that happens; the new bridge is what the old launcher then installs."""
    sys.path.insert(0, str(HERE))
    import autorelease
    inst = unpack_launcher(tmp_path / "inst", shipped_launchers["v0.3.0-preview.4"])
    exe = inst / LAUNCHER_EXE
    server.publish("v0.3.0-preview.5", release_files(tmp_path, "v0.3.0-preview.5", keys["release"]))
    for n in range(1, 31):
        server.publish(f"v0.4.0-alpha.{n}", release_files(tmp_path, f"v0.4.0-alpha.{n}", keys["release"]))
    r = launch(tmp_path, server, keys["release"], "--channel=preview", "--update", "--self-update", exe=exe)
    assert r.code == 0 and not r.has("update installed") and not r.has("staged") and r.has("no preview release"), r
    assert autorelease.bridge_check(server.releases, lambda rel: "")[0].startswith("no release on the first 30")
    # the rule already asked for a new bridge 5 releases earlier
    assert autorelease.bridge_check(server.releases[5:], lambda rel: "")[0].startswith("the bridge v0.3.0-preview.5 is entry 26")
    server.publish("v0.3.0-preview.6", release_files(tmp_path, "v0.3.0-preview.6", keys["release"]))
    assert autorelease.bridge_check(server.releases, lambda rel: "") == ("", "v0.3.0-preview.6")
    r = launch(tmp_path, server, keys["release"], "--update", "--self-update", exe=exe)
    assert r.code == 0 and r.has("update installed v0.3.0-preview.6") and r.has("launcher update v0.3.0-preview.6 staged"), r


def unpack_launcher(tmp: Path, archive: bytes) -> Path:
    tmp.mkdir(parents=True, exist_ok=True)
    if WINDOWS:
        with zipfile.ZipFile(io.BytesIO(archive)) as z:
            z.extractall(tmp)
        return next(tmp.glob("openbfme-launcher-*-windows-x64"))
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as t:
        t.extractall(tmp, filter="tar")
    return next(tmp.glob(f"openbfme-launcher-*-{PLATFORM}"))


def exe_sha(archive: bytes) -> str:
    if WINDOWS:
        with zipfile.ZipFile(io.BytesIO(archive)) as z:
            name = next(n for n in z.namelist() if n.endswith("/OpenBFMELauncher.exe"))
            return hashlib.sha256(z.read(name)).hexdigest()
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as t:
        m = next(m for m in t.getmembers() if m.name.endswith("/" + LAUNCHER_EXE))
        return hashlib.sha256(t.extractfile(m).read()).hexdigest()


# ---- REPRO-1: the launcher package is the same bytes whatever order the host's file system lists the project in --------------------------

def _creation_order_dir() -> Path | None:
    """A folder on a file system that lists a directory in creation order (tmpfs: newest first), where two stagings created in opposite
    orders are listed differently; None if there is none. On ext4 (both CI hosts, the Deck) the order is a hash seeded per file system,
    the cause of v0.3.0-preview.5's unreproducible launchers, which one host alone never shows."""
    for base in (os.environ.get("OPENBFME_TMPFS", ""), "/dev/shm"):
        if not base or not os.path.isdir(base) or not os.access(base, os.W_OK):
            continue
        listed = []
        for order in (("a", "b", "c"), ("c", "b", "a")):
            d = Path(tempfile.mkdtemp(dir=base, prefix="openbfme-order."))
            for n in order:
                (d / n).touch()
            listed.append(os.listdir(d))
            shutil.rmtree(d)
        if listed[0] != listed[1]:
            return Path(base)
    return None


def test_the_launcher_package_does_not_depend_on_the_directory_order():
    """REPRO-1: the package build_launcher.sh makes for a release (the committed key, no test options) is byte for byte the same when
    every directory of the staged project lists its files in the opposite order (--stage-reverse on a creation-order file system).
    Godot applies a preset's include_filter / exclude_filter in directory order, so launcher/export_presets.cfg keeps both empty and
    addons/pack_files picks the files; with the old filters the two packages differed (build_info.json and the OFL licences moved)."""
    if not _templates_ok():
        pytest.skip(f"SKIP the reproducibility test: the Godot 4.7.2 {PLATFORM} export template is not installed")
    if WINDOWS:
        pytest.skip("SKIP the reproducibility test: it requires a Linux creation-order filesystem and the Bash exporter")
    base = _creation_order_dir()
    if base is None:
        pytest.skip("SKIP the reproducibility test: no file system here lists a directory in creation order (set OPENBFME_TMPFS to a tmpfs)")
    d = Path(tempfile.mkdtemp(dir=base, prefix="openbfme-repro."))
    try:
        built = []
        for extra in ([], ["--stage-reverse"]):
            out = d / f"out{len(built)}"
            r = subprocess.run([str(HERE / "build_launcher.sh"), "--out", str(out), "--version", "v1.2.3", "--commit", COMMIT, "--repo",
                                TEST_REPO, "--key-file", str(LAUNCHER / "release_key.pub"), "--mtime", "1700000000", "--platforms", "linux",
                                "--worktree", "--godot", GODOT, *extra], capture_output=True, text=True, timeout=900)
            assert r.returncode == 0, r.stdout[-3000:] + r.stderr[-3000:]
            built.append((out / f"openbfme-launcher-v1.2.3-{PLATFORM}{ARCHIVE_SUFFIX}").read_bytes())
        if built[0] != built[1]:
            for i, b in enumerate(built):
                (d / f"out{i}.tar.gz").write_bytes(b)
            r = subprocess.run([sys.executable, str(HERE / "make_archive.py"), "--diff", str(d / "out0.tar.gz"), str(d / "out1.tar.gz")],
                               capture_output=True, text=True)
            pytest.fail("the launcher package depends on the order the file system lists the project in:\n" + r.stdout[-3000:])
    finally:
        shutil.rmtree(d, ignore_errors=True)



def leftovers(inst: Path, tmp: Path) -> list[str]:
    out = sorted(p.name for p in inst.iterdir() if p.name not in (LAUNCHER_EXE, "README.txt", "LICENSE", "NOTICE", "VERSION"))
    if (data(tmp) / "launcher-update.json").exists():
        out.append("journal")
    return out


def staged_launcher(tmp_path, server, keys, launcher_builds, launcher=None):
    inst = unpack_launcher(tmp_path / "inst", launcher_builds["v0.2.0"])
    exe = inst / LAUNCHER_EXE
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
    assert (inst / f"{LAUNCHER_EXE}.old").is_file(), "the previous launcher is kept until the new one has started once"
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
    assert f"LAUNCHER test kill at {point}" in (data(tmp_path) / "launcher.log").read_text(encoding="utf-8"), r  # stdout of a killed process is lost
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
    b = bytearray((top / LAUNCHER_EXE).read_bytes())
    sys.path.insert(0, str(HERE))
    import audit_package  # noqa: PLC0415
    base = audit_package.embedded_pck_base(bytes(b))
    b[base + 4:base + 8] = b"\xff\xff\xff\xff"  # an unknown pack format
    (top / LAUNCHER_EXE).write_bytes(bytes(b))
    broken_archive = work / f"broken{ARCHIVE_SUFFIX}"
    (make_archive.zip_file if WINDOWS else make_archive.tar_gz)(top, broken_archive, 1700000000)
    inst, exe = staged_launcher(tmp_path, server, keys, launcher_builds, launcher=broken_archive.read_bytes())
    old = hashlib.sha256(exe.read_bytes()).hexdigest()
    for _ in range(4):
        r = launch(tmp_path, server, keys["release"], "--version", exe=exe, missing_pack_alert=True)
        assert "OpenBFME Launcher v0.2.0" in r.out, r
        assert hashlib.sha256(exe.read_bytes()).hexdigest() == old
    assert r.has("did not complete after 3 attempts: given up"), r
    assert leftovers(inst, tmp_path) == []


def test_release_build_ignores_the_test_options(tmp_path, server, keys, launcher_builds):
    inst = unpack_launcher(tmp_path / "rel", launcher_builds["release"])
    server.publish("v0.3.0", release_files(tmp_path, "v0.3.0", keys["release"]))
    # --version only: with the test options ignored, --update would ask the real api.github.com
    r = launch(tmp_path, server, keys["other"], "--version", "--ui-demo=offline", "--ui-demo=paused",
               exe=inst / LAUNCHER_EXE, timeout=120)
    assert r.has("ignored --api-base: test options are off in a release build"), r
    assert r.has("ignored --trust-key"), r
    assert r.out.count("ignored --ui-demo: test options are off in a release build") == 2, r
    assert not server.requests, "the release build never contacts the test server"
    assert "updates from Test/repo" in r.out


def test_read_only_launcher_folder(tmp_path, server, keys, launcher_builds):
    if WINDOWS:
        pytest.skip("SKIP POSIX directory-permission fixture on Windows")
    inst = unpack_launcher(tmp_path / "ro", launcher_builds["v0.2.0"])
    server.publish("v0.3.0", release_files(tmp_path, "v0.3.0", keys["release"], launcher=launcher_builds["v0.3.0"]))
    inst.chmod(0o555)
    try:
        r = launch(tmp_path, server, keys["release"], "--update", "--self-update", exe=inst / LAUNCHER_EXE)
    finally:
        inst.chmod(0o755)
    assert r.code == 3 and r.has("update installed v0.3.0") and r.has("is not writable: the launcher cannot update itself there"), r
    assert not any(p.endswith(asset_name("launcher", "v0.3.0")) for p, _ in server.requests), "nothing downloaded for it"


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
    kill_line = f"LAUNCHER test kill at {point}"
    log = (data(tmp_path) / "launcher.log").read_text(encoding="utf-8")
    assert kill_line in log and (r.code != 0 or WINDOWS and kill_line in r.out), r
    if WINDOWS:
        owner = data(tmp_path) / "versions.lock/owner"
        if point == "install-unpacked":
            assert not owner.exists(), "unpack crash point precedes versions-lock acquisition"
        else:
            assert owner.is_file(), "replacement crash point must leave its versions-lock owner"
            pid, created = map(int, owner.read_text().split())
            assert not process_running(pid), "the crash hook must actually terminate the lock owner"
            policy = (LAUNCHER / "scripts/core/updater.gd").read_text(encoding="utf-8")
            lease = int(re.search(r"const STALE_LOCK_SECONDS := (\d+)", policy).group(1))
            # Windows has no /proc owner probe: exercise its real lease expiry, without editing the lock.
            time.sleep(max(0, created + lease + 1 - time.time()))
    r = launch(tmp_path, None, keys["release"], "--list", "--play")
    assert r.code == 0 and r.has("installed v0.2.0") and r.has("play v0.2.0") and "played game" in played(tmp_path), r
    assert sorted(p.name for p in (data(tmp_path) / "versions").iterdir()) == ["v0.2.0"]


def test_a_swapped_staged_launcher_is_not_run(tmp_path, server, keys, launcher_builds):
    """Sol r2: the swap took the new executable's SHA-256 from the file in update/ itself, so a file swapped in after staging ran
    unchecked. Now every staged file is checked against the cached signed manifest before the journal is written; the journal's
    SHA-256 comes from the manifest."""
    inst, exe = staged_launcher(tmp_path, server, keys, launcher_builds)
    old = hashlib.sha256(exe.read_bytes()).hexdigest()
    other = unpack_launcher(tmp_path / "other", launcher_builds["release"]) / LAUNCHER_EXE
    shutil.copy(other, inst / "update" / LAUNCHER_EXE)
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
        if WINDOWS:
            mutated = bytearray(native_game("played").read_bytes())
            mutated[0] ^= 1
            (game / GAME_EXE).write_bytes(mutated)
        else:
            (game / GAME_EXE).write_bytes(game_script().replace(b"played", b"PWNED!"))
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


# ---- lane UI-3: the window shows no home folder and no user name ------------------------------------------------------------------------

USER_NAME = "zedtester"


def screen_texts(tmp: Path, server: ReleaseServer | None, key: bytes, *args: str) -> tuple[Run, list[str]]:
    """the launcher's window under a home folder named after USER_NAME (its data and the game's folder files inside it), every text of
    every screen dumped (--ui-dump: unscrubbed, hidden screens included)"""
    home = tmp / "home" / USER_NAME
    env = isolated_environment(tmp, OPENBFME_TEST_PLAYED=str(tmp / "played.txt"))
    env.update(HOME=str(home), USER=USER_NAME, LOGNAME=USER_NAME)
    if WINDOWS:
        env.update(USERNAME=USER_NAME, USERPROFILE=str(home), APPDATA=str(home / "AppData/Roaming"),
                   LOCALAPPDATA=str(home / "AppData/Local"))
    else:
        env.update(XDG_DATA_HOME=str(home / ".local/share"), XDG_CONFIG_HOME=str(home / ".config"),
                   XDG_CACHE_HOME=str(home / ".cache"))
    test = [f"--api-base={server.origin}"] if server else []
    test += [f"--trust-key={ed25519.public_key(key).hex()}", f"--repo={TEST_REPO}"]
    cmd = [GODOT, "--headless", "--path", str(LAUNCHER), "--", *test, *args, "--ui-dump", f"--screenshot={tmp / 'shot.png'}"]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace",
                       timeout=180, stdin=subprocess.DEVNULL)
    run = Run(r.returncode, r.stdout + r.stderr)
    return run, [ln.split("LAUNCHER ui text: ", 1)[1] for ln in run.lines if "LAUNCHER ui text: " in ln]


def game_user_dir(tmp: Path) -> Path:
    if WINDOWS:
        return tmp / "home" / USER_NAME / "AppData/Roaming/Godot/app_userdata/OpenBFME"
    return tmp / "home" / USER_NAME / ".local/share/godot/app_userdata/OpenBFME"


@pytest.mark.parametrize("screen", ["", "main", "update", "downloading", "settings", "games", "error"])
def test_no_screen_shows_the_home_folder_or_user_name(tmp_path, server, keys, screen):
    """the owner's devlog footage showed the Deck's home folder: by default no text of any screen (the folders of the games, the logs,
    an error naming a file) shows the home folder or the user's name; the folders are shown as ~/..."""
    home = tmp_path / "home" / USER_NAME
    games = home / "Games" / "BFME"
    for g in ("RotWK", "BFME2"):
        (games / g).mkdir(parents=True)
    gdir = game_user_dir(tmp_path)
    gdir.mkdir(parents=True)
    (gdir / "install-paths.cfg").write_text(
        f"# OpenBFME\nROTWK_INSTALL={normalized_path(games / 'RotWK')}\nBFME2_INSTALL={normalized_path(games / 'BFME2')}\n",
        encoding="utf-8",
    )
    display_home = "%USERPROFILE%" if WINDOWS else "~"
    sep = "\\" if WINDOWS else "/"
    display_dir = f"{display_home}{sep}Games{sep}BFME"
    if screen:
        r, texts = screen_texts(tmp_path, None, keys["release"], f"--ui-demo={screen}")
    else:
        # the real start: an update is installed and the launcher's own data folder is inside the home folder
        server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["release"]))
        r, texts = screen_texts(tmp_path, server, keys["release"])
        assert r.has("ui status: Installed v0.2.0."), r
        assert any(t == f"In {display_dir}" for t in texts), texts
    assert r.code == 0 and len(texts) > 40, r
    home_forms = {str(home), normalized_path(home), str(home).replace("/", "\\")}
    leaks = [t for t in texts if USER_NAME in t or any(p in t for p in home_forms)]
    assert not leaks, leaks
    if screen != "games":  # that screen is the one without game folders
        assert any(t.startswith(display_dir + sep) for t in texts), texts  # Settings shows the folders, shortened


def test_no_screen_shows_a_user_name_outside_the_home_folder(tmp_path, keys):
    """a game folder on a removable drive (/run/media/<name>/...) and an error naming the launcher's data folder: the name is taken out
    as a path component, and the games card shows when the folders are missing"""
    media = tmp_path / "media" / USER_NAME / "SD"
    for g in ("RotWK", "BFME2"):
        (media / g).mkdir(parents=True)
    gdir = game_user_dir(tmp_path)
    gdir.mkdir(parents=True)
    (gdir / "downloaded-installs.cfg").write_text(
        f"SOURCE=test\nROTWK_INSTALL={normalized_path(media / 'RotWK')}\nBFME2_INSTALL={normalized_path(media / 'BFME2')}\n",
        encoding="utf-8",
    )
    r, texts = screen_texts(tmp_path, None, keys["release"], "--offline")
    assert r.code == 0 and len(texts) > 40, r
    assert not [t for t in texts if USER_NAME in t], texts
    sep = "\\" if WINDOWS else "/"
    assert any(f"{sep}\u2026{sep}SD{sep}RotWK" in t for t in texts), texts
    assert r.has("ui games card: hidden"), r
    (gdir / "downloaded-installs.cfg").unlink()
    r, texts = screen_texts(tmp_path, None, keys["release"], "--offline")
    assert r.code == 0 and r.has("ui games card: shown") and not [t for t in texts if USER_NAME in t], r


# ---- lane UI-3 round 2 (Sol r1): Pause / Cancel in the window --------------------------------------------------------------------------

def window(tmp: Path, server: ReleaseServer, key: bytes, *args: str, exe: Path | None = None) -> Run:
    """the launcher's window (headless) through its start-up check, with --ui-stop / --ui-then pressing Pause, Resume or Cancel"""
    return launch(tmp, server, key, *args, f"--screenshot={tmp / 'shot.png'}", exe=exe, timeout=300)


def big_game_release(tmp: Path, server: ReleaseServer, key: bytes, version: str = "v0.2.0") -> str:
    """a release whose game package is large (incompressible) and served slowly, so a press lands mid-download; its asset name"""
    name = make_manifest.asset_name("game", version, PLATFORM)
    server.publish(version, release_files(tmp, version, key, game=game_tar(tmp, version, os.urandom(600_000))))
    server.throttle[name] = 0.02
    return name


def play_during_download(tmp: Path, server: ReleaseServer, key: bytes, name: str) -> Run:
    """Hold the download after its first chunk until Play has started the installed game and elected to wait for the update."""
    resume = threading.Event()
    path = next(p for p in server.files if p.endswith("/" + name))
    server.pause_once[path] = resume
    log = data(tmp) / "launcher.log"
    offset = log.stat().st_size if log.is_file() else 0
    ready = "LAUNCHER Playing v0.1.0. The update keeps downloading in the background;"
    with ThreadPoolExecutor(max_workers=1) as pool:
        run = pool.submit(window, tmp, server, key, f"--ui-stop=play@{name}")
        try:
            deadline = time.monotonic() + 120
            while ready not in _new_launcher_log(log, offset):
                if run.done():
                    pytest.fail(f"Play did not wait for the background update: {run.result()}")
                assert time.monotonic() < deadline, _new_launcher_log(log, offset)
                resume.wait(0.05)
        finally:
            resume.set()
        return run.result(timeout=180)


def other_partials(tmp: Path) -> list[Path]:
    """resumable downloads in downloads/ that are not the running job's: another version's game package, an unrelated file"""
    dl = data(tmp) / "downloads"
    dl.mkdir(parents=True, exist_ok=True)
    archive = asset_name("game", "v0.1.0")
    out = [dl / f"{archive}.part", dl / f"{archive}.part.json", dl / "notes.part"]
    for p in out:
        p.write_bytes(b"not this job's")
    return out


@pytest.mark.parametrize("then", ["", "cancel"])
def test_cancel_removes_only_the_cancelled_jobs_partial_download(tmp_path, server, keys, then):
    """Sol r1: Cancel removed every .part / .part.json in downloads/; only the cancelled job's own asset goes (Cancel while
    downloading, and Cancel after a Pause)"""
    name = big_game_release(tmp_path, server, keys["release"])
    others = other_partials(tmp_path)
    stop = "pause" if then else "cancel"
    r = window(tmp_path, server, keys["release"], f"--ui-stop={stop}@{name}", *([f"--ui-then={then}"] if then else []))
    assert r.has(f"ui test: {stop} pressed during Downloading {name}"), r
    if then:
        assert r.has("ui paused: game") and r.has("ui test: cancel pressed"), r
    assert r.has("ui status: The update was cancelled.") and r.has("ui paused: ") and not r.has("rejected"), r
    dl = data(tmp_path) / "downloads"
    assert not (dl / f"{name}.part").exists() and not (dl / f"{name}.part.json").exists()
    assert all(p.read_bytes() == b"not this job's" for p in others), "other downloads stay"
    assert installed(tmp_path) == []


def test_pause_and_resume_a_game_download(tmp_path, server, keys):
    name = big_game_release(tmp_path, server, keys["release"])
    r = window(tmp_path, server, keys["release"], f"--ui-stop=pause@{name}", "--ui-then=resume")
    assert r.has("ui paused: game") and r.has(f"resumed {name} at"), r
    assert r.has("ui status: Installed v0.2.0.") and installed(tmp_path) == ["v0.2.0"], r


@pytest.mark.parametrize("then", ["resume", "cancel", ""])
def test_a_stopped_launcher_download_can_be_resumed_or_cancelled(tmp_path, server, keys, launcher_builds, then):
    """Sol r1: stopping the launcher's own download was reported as "Launcher update rejected", Pause offered no Resume and Cancel left
    its partial file behind"""
    inst = unpack_launcher(tmp_path / "inst", launcher_builds["v0.2.0"])
    exe = inst / LAUNCHER_EXE
    server.publish("v0.3.0", release_files(tmp_path, "v0.3.0", keys["release"], launcher=launcher_builds["v0.3.0"]))
    name = make_manifest.asset_name("launcher", "v0.3.0", PLATFORM)
    server.throttle[name] = 0.002
    stop = "pause" if then else "cancel"
    r = window(tmp_path, server, keys["release"], f"--ui-stop={stop}@{name}", *([f"--ui-then={then}"] if then else []), exe=exe)
    assert r.has(f"ui test: {stop} pressed during Downloading {name}") and not r.has("rejected"), r
    part = data(tmp_path) / "downloads" / f"{name}.part"
    if then == "resume":
        assert r.has("ui paused: launcher") and r.has(f"resumed {name} at"), r
        assert r.has("ui status: The launcher v0.3.0 is ready and replaces this one on the next start."), r
        assert (inst / "update" / "VERSION.launcher").read_text().strip() == "v0.3.0"
    else:
        if then == "cancel":
            assert r.has("ui paused: launcher"), r
        assert r.has("ui status: The launcher update was cancelled."), r
        assert not part.exists() and not part.with_name(part.name + ".json").exists() and not (inst / "update").exists()
    assert installed(tmp_path) == ["v0.3.0"], "the game update before it was installed"


# ---- lane UI-5: updates in the background, the notes cards ------------------------------------------------------------------------------

@pytest.mark.parametrize("case", ["fresh", "cached", "offline", "network-error", "rate-limit", "manifest-error", "available",
                                  "no-release", "auto-install", "manual", "manual-available", "manual-error",
                                  "completion", "completion-error"])
def test_update_pill_and_bottom_line_share_the_completed_state(tmp_path, server, keys, case):
    current, newer = "v0.4.12-nightly.20261022", "v0.4.13-nightly.20261023"
    key = keys["release"]
    server.publish(current, release_files(tmp_path, current, key))
    r = launch(tmp_path, server, key, "--update", "--auto-update=off", "--channel=nightly", "--online-count=off")
    assert r.code == 0, r.out
    head, detail = "Up to date", "Up to date."
    diagnostic = ""
    args = []
    if case == "fresh":
        for path in (data(tmp_path) / "cache").glob("releases.*"):
            path.unlink()
    elif case in ("available", "manual-available", "auto-install"):
        server.publish(newer, release_files(tmp_path, newer, key))
        if case == "auto-install":
            r = launch(tmp_path, server, key, "--auto-update=on")
            assert r.code == 0, r.out
            detail = "Installed " + newer + "."
        else:
            head, detail = "Nightly 0.4.13 (2026-10-23) is available", newer + " is available."
    elif case in ("rate-limit", "manual-error"):
        server.rate_limited = True
        head, detail = "Could not check for updates", "Could not check for updates. Check your internet connection; retry later."
        diagnostic = "Updates skipped: GitHub's API rate limit"
    elif case == "network-error":
        server.stop()
        head, detail = "Could not check for updates", "Could not check for updates. Check your internet connection; retry later."
        diagnostic = "Updates skipped: the release list could not be fetched:"
    elif case == "manifest-error":
        server.publish(newer, release_files(tmp_path, newer, key))
        path = next(p for p in server.files if p.endswith(newer + "/manifest.json.sig"))
        server.files[path] = b"invalid signature"
        head, detail = "Could not check for updates", "Update could not be verified. Check for updates and retry."
        diagnostic = "Update rejected:"
    elif case == "no-release":
        server.releases.clear()
        head, detail = "No nightly release yet", "There is no nightly release yet."
    elif case == "completion-error":
        head, detail = "Could not check for updates", "Update could not be verified. Check for updates and retry."
    if case == "offline" or case.startswith(("manual", "completion")):
        args.append("--offline")
    if case == "offline":
        head, detail = "Offline — updates not checked", ""
    server.requests.clear()
    env = isolated_environment(tmp_path)
    result = subprocess.run([GODOT, "--headless", "--path", str(LAUNCHER), "--script", "res://tests/update_status_test.gd", "--",
                             f"--api-base={server.origin}", f"--trust-key={ed25519.public_key(key).hex()}", f"--repo={TEST_REPO}",
                             f"--status-case={case}", f"--status-head={head}", f"--status-detail={detail}",
                             f"--status-diagnostic={diagnostic}", *args],
                            env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
    out = result.stdout + result.stderr
    assert result.returncode == 0 and "UPDATE STATUS OK" in out and "SCRIPT ERROR" not in out, out
    requests = [headers for path, headers in server.requests if path.startswith("/repos/")]
    if case in ("offline", "completion", "completion-error"):
        assert requests == []
    elif case in ("fresh", "cached", "manual"):
        assert len(requests) == (2 if case == "manual" else 1)
        assert all(("if-none-match" in h) == (case != "fresh") for h in requests)
    if case == "auto-install":
        assert installed(tmp_path) == [current, newer]


def test_with_updating_by_itself_off_the_window_only_offers_the_update(tmp_path, server, keys):
    server.publish("v0.1.0", release_files(tmp_path, "v0.1.0", keys["release"]))
    r = launch(tmp_path, server, keys["release"], "--update", "--auto-update=off")
    assert r.code == 0 and r.has("updating by itself off") and installed(tmp_path) == ["v0.1.0"], r
    server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["release"]))
    r = window(tmp_path, server, keys["release"])
    assert r.has("ui status: v0.2.0 is available.") and installed(tmp_path) == ["v0.1.0"], r
    r = launch(tmp_path, server, keys["release"], "--auto-update=on")
    assert r.code == 0 and r.has("updating by itself on"), r
    r = window(tmp_path, server, keys["release"])
    assert r.has("ui status: Installed v0.2.0.") and installed(tmp_path) == ["v0.1.0", "v0.2.0"], r


def test_play_during_a_background_update_starts_the_installed_version(tmp_path, server, keys):
    """the owner: updates download in the background, also while the installed version is played, and the new version is used from the
    next Play on. Play pressed while v0.2.0 downloads starts v0.1.0; the launcher finishes the download, then closes by itself."""
    server.publish("v0.1.0", release_files(tmp_path, "v0.1.0", keys["release"]))
    assert launch(tmp_path, server, keys["release"], "--update").code == 0 and installed(tmp_path) == ["v0.1.0"]
    name = big_game_release(tmp_path, server, keys["release"], "v0.2.0")
    del server.throttle[name]
    r = play_during_download(tmp_path, server, keys["release"], name)
    assert r.has(f"ui test: play pressed during Downloading {name}") and r.has("started v0.1.0 (pid"), r
    assert r.has("installed v0.2.0") and r.has("the background update finished while the game runs: the launcher closes"), r
    assert r.code == 0 and not r.has("ui status:"), "it closed by itself, before the screenshot step"
    assert installed(tmp_path) == ["v0.1.0", "v0.2.0"]
    r = launch(tmp_path, None, keys["release"], "--play")
    assert r.code == 0 and r.has("play v0.2.0"), r


def test_an_update_never_removes_the_version_being_played(tmp_path, server, keys):
    """two versions are kept; an update installed while the player's older pick runs keeps that one until its game has ended"""
    for v in ("v0.1.0", "v0.2.0"):
        script = slow_game() if v == "v0.1.0" else game_script()
        server.publish(v, release_files(tmp_path, v, keys["release"], game=game_tar(tmp_path, v, script=script)))
        assert launch(tmp_path, server, keys["release"], "--update").code == 0
    r = launch(tmp_path, None, keys["release"], "--select=v0.1.0", "--play")
    assert r.code == 0 and r.has("play v0.1.0"), r
    pid = int(next(ln for ln in r.lines if "started v0.1.0 (pid" in ln).split("(pid ")[1].rstrip(")"))
    try:
        server.publish("v0.3.0", release_files(tmp_path, "v0.3.0", keys["release"]))
        r = launch(tmp_path, server, keys["release"], "--update")
        assert r.code == 0 and r.has("update installed v0.3.0"), r
        assert installed(tmp_path) == ["v0.1.0", "v0.2.0", "v0.3.0"], "the running v0.1.0 stays"
    finally:
        stop_process(pid)
    for _ in range(50):
        if not process_running(pid):
            break
        time.sleep(0.1)
    server.publish("v0.4.0", release_files(tmp_path, "v0.4.0", keys["release"]))
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 0 and r.has("removed the old version v0.1.0") and installed(tmp_path) == ["v0.3.0", "v0.4.0"], r


def test_the_notes_cards_show_the_releases_own_notes(tmp_path, server, keys):
    """What's New shows the "What's new" lines of the version Play starts; News every release of the channel; also offline, from the
    cached release list; markup in the notes stays text"""
    body = ("OpenBFME v0.2.0 is a test build.\n\n## What's new\n\n- Ground force\\-attack with Ctrl\\+click\\.\n- [b]not bold[/b] "
            "&lt;i&gt;\n\n## Known issues\n\n- Tribute does not work yet\\.\n")
    server.publish("v0.1.0", release_files(tmp_path, "v0.1.0", keys["release"]), notes="## What's new\n\n- The first build.\n")
    server.publish("v0.2.0", release_files(tmp_path, "v0.2.0", keys["release"]), notes=body)
    r, texts = screen_texts(tmp_path, server, keys["release"])
    assert r.has("ui status: Installed v0.2.0."), r
    assert texts.count("Ground force-attack with Ctrl+click.") == 2, "What's New and News"
    assert texts.count("[b]not bold[/b] <i>") == 2 and "Tribute does not work yet." in texts and "The first build." in texts, texts
    assert any(t.startswith("Alpha 0.2.0") for t in texts) and "What's new" in texts, texts
    r, texts = screen_texts(tmp_path, None, keys["release"], "--offline")
    assert r.code == 0 and texts.count("Ground force-attack with Ctrl+click.") == 2, texts


# ---- lane UI-5 round 2 (Sol r1): every instance protects its version, failures stay visible, launcher-only updates --------------------

def _pid_of(r: Run, version: str) -> int:
    return int(next(ln for ln in r.lines if f"started {version} (pid" in ln).split("(pid ")[1].rstrip(")"))


def _stop_game(pid: int) -> None:
    stop_process(pid)
    for _ in range(50):
        if not process_running(pid):
            return
        time.sleep(0.1)


def test_every_running_instance_keeps_its_version(tmp_path, server, keys):
    """Sol r1: one pid was kept per version, so a second instance of the same version that closed unprotected the first one, still
    running, and the next update pruned it; a running version is also never replaced by an install of itself"""
    for v in ("v0.1.0", "v0.2.0"):
        script = slow_game() if v == "v0.1.0" else game_script()
        server.publish(v, release_files(tmp_path, v, keys["release"], game=game_tar(tmp_path, v, script=script)))
        assert launch(tmp_path, server, keys["release"], "--update").code == 0
    first = _pid_of(launch(tmp_path, None, keys["release"], "--select=v0.1.0", "--play"), "v0.1.0")
    second = _pid_of(launch(tmp_path, None, keys["release"], "--play"), "v0.1.0")
    try:
        state = (data(tmp_path) / "state.cfg").read_text()
        assert str(first) in state and str(second) in state, state
        _stop_game(second)
        r = launch(tmp_path, server, keys["release"], "--install=v0.1.0")
        assert r.code == 3 and r.has("v0.1.0 is being played: close the game"), r
        server.publish("v0.3.0", release_files(tmp_path, "v0.3.0", keys["release"]))
        r = launch(tmp_path, server, keys["release"], "--update")
        assert r.code == 0 and r.has("update installed v0.3.0"), r
        assert installed(tmp_path) == ["v0.1.0", "v0.2.0", "v0.3.0"], "the first instance still runs v0.1.0"
        assert not (data(tmp_path) / "versions.lock").exists(), "the versions lock is released"
    finally:
        _stop_game(first)
    server.publish("v0.4.0", release_files(tmp_path, "v0.4.0", keys["release"]))
    r = launch(tmp_path, server, keys["release"], "--update")
    assert r.code == 0 and r.has("removed the old version v0.1.0") and installed(tmp_path) == ["v0.3.0", "v0.4.0"], r


def test_a_background_update_that_fails_while_playing_stays_visible(tmp_path, server, keys):
    """Sol r1: Play pressed during a download, then the package failed its check: the launcher closed by itself and the rejection was
    never shown. It stays open with the dialog, and a launcher closed without reading it shows it at the next start."""
    server.publish("v0.1.0", release_files(tmp_path, "v0.1.0", keys["release"]))
    assert launch(tmp_path, server, keys["release"], "--update").code == 0
    files = release_files(tmp_path, "v0.2.0", keys["release"], game=game_tar(tmp_path, "v0.2.0", os.urandom(600_000)))
    name = make_manifest.asset_name("game", "v0.2.0", PLATFORM)
    files[name] = files[name][:-1] + bytes([files[name][-1] ^ 1])  # same size, other bytes
    server.publish("v0.2.0", files)
    r = play_during_download(tmp_path, server, keys["release"], name)
    assert r.has(f"ui test: play pressed during Downloading {name}") and r.has("started v0.1.0 (pid"), r
    assert r.has("does not match the signed manifest") and r.has("the launcher stays open to show it"), r
    assert not r.has("the launcher closes") and r.has("ui dialog: Update not installed: Update rejected:"), r
    r = window(tmp_path, None, keys["release"], "--offline")
    assert r.has("showing an update failure from while the game ran") and r.has("ui dialog: An update failed while you played"), r
    assert installed(tmp_path) == ["v0.1.0"]


@pytest.mark.parametrize("result", ["success", "failure"])
def test_background_update_completion_pending_when_play_returns(tmp_path, result):
    env = isolated_environment(tmp_path)
    r = subprocess.run([GODOT, "--headless", "--path", str(LAUNCHER), "--script", "res://tests/background_play_test.gd", "--", result],
                       env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
    out = r.stdout + r.stderr
    assert r.returncode == 0 and "BACKGROUND PLAY pending completion kept" in out, out
    if result == "success":
        assert "the background update finished while the game runs: the launcher closes" in out, out
    else:
        assert "the background update failed while the game runs: the launcher stays open" in out, out
        assert "BACKGROUND PLAY failure kept for the next start" in out and "the launcher closes" not in out, out


def test_with_updating_by_itself_off_a_launcher_only_update_is_offered(tmp_path, server, keys, launcher_builds):
    """Sol r1: launcher v0.2.0 with the game already at v0.3.0 said "Up to date" and offered no Update button"""
    inst = unpack_launcher(tmp_path / "inst", launcher_builds["v0.2.0"])
    exe = inst / LAUNCHER_EXE
    server.publish("v0.3.0", release_files(tmp_path, "v0.3.0", keys["release"], launcher=launcher_builds["v0.3.0"]))
    r = launch(tmp_path, server, keys["release"], "--auto-update=off", "--install=v0.3.0", exe=exe)
    assert r.code == 0 and installed(tmp_path) == ["v0.3.0"], r
    r = window(tmp_path, server, keys["release"], exe=exe)
    assert r.has("ui status: The launcher v0.3.0 is available.") and r.has("ui update button: Update launcher to Alpha 0.3.0"), r  # VERSION-1: the button names the version as players read it
    assert not (inst / "update").exists(), "nothing staged until the player presses Update"


# ---- lane UI-5 round 3 (Sol r2): one launcher at a time ----------------------------------------------------------------------------------

def _window_process(tmp: Path, key: bytes) -> subprocess.Popen:
    """a launcher window (headless, offline) that stays open until it is stopped; its output in tmp/<pid>.log"""
    env = isolated_environment(tmp, OPENBFME_TEST_PLAYED=str(tmp / "played.txt"))
    test = [f"--trust-key={ed25519.public_key(key).hex()}", f"--repo={TEST_REPO}"]
    log = open(tmp / f"window-{time.monotonic_ns()}.log", "w")
    p = subprocess.Popen([GODOT, "--headless", "--path", str(LAUNCHER), "--", *test, "--offline"], env=env, stdout=log,
                         stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
    p.log_path = Path(log.name)  # type: ignore[attr-defined]
    return p


def _wait_started(p: subprocess.Popen) -> bool:
    """True once the window has started (its first LAUNCHER line, then two seconds for it to take the lock and build the window),
    False when it exited"""
    for _ in range(300):
        if p.poll() is not None:
            return False
        if "OpenBFME Launcher v" in p.log_path.read_text():  # type: ignore[attr-defined]
            time.sleep(2.0)
            return p.poll() is None
        time.sleep(0.1)
    return False


def test_a_second_launcher_says_so_and_exits(tmp_path, server, keys):
    """Sol r2: two launchers opened together each kept their own state.cfg, and a stale list of running games written by one let the
    other prune a version still being played. Now the second launcher (window or command line) says the first is open and exits; the
    lock (a socket on 127.0.0.1) is freed by the system when a launcher is killed."""
    server.publish("v0.1.0", release_files(tmp_path, "v0.1.0", keys["release"]))
    assert launch(tmp_path, server, keys["release"], "--update").code == 0
    first = _window_process(tmp_path, keys["release"])
    try:
        assert _wait_started(first), first.log_path.read_text()
        r = launch(tmp_path, None, keys["release"], "--offline")
        assert r.code == 1 and r.has("Another OpenBFME Launcher (v0.0.0-dev) is already open"), r
        r = launch(tmp_path, server, keys["release"], "--play")
        assert r.code == 1 and r.has("Another OpenBFME Launcher (v0.0.0-dev) is already open") and not r.has("started v0.1.0"), r
        r = launch(tmp_path, None, keys["release"], "--version")
        assert r.code == 0, "--version alone needs no lock"
        assert first.poll() is None, "the first launcher keeps running"
        # Sol r4 follow-up: the refused launchers' probes closed first, so the lock's port is left in no TIME_WAIT (Godot listens without
        # SO_REUSEADDR: the next launcher could not bind it for a minute)
        time.sleep(0.5)
        port = _lock_port(tmp_path)
        if WINDOWS:
            output = subprocess.run(["netstat", "-ano", "-p", "tcp"], capture_output=True, text=True, check=True).stdout
            waits = [ln for ln in output.splitlines() if "TIME_WAIT" in ln.upper() and ln.split()[1].endswith(f":{port}")]
        else:
            waits = [ln for ln in Path("/proc/net/tcp").read_text().splitlines()[1:]
                     if ln.split()[1].endswith(f":{port:04X}") and ln.split()[3] == "06"]
        assert waits == [], waits
    finally:
        first.kill()  # a crash: the system frees the lock
        first.wait()
    r = launch(tmp_path, None, keys["release"], "--list")
    assert r.code == 0 and r.has("installed v0.1.0"), r


def test_two_launchers_started_at_the_same_moment_get_one_lock(tmp_path, keys):
    """Sol r3: the lock file was not atomic: two launchers started together both took it, and the one that exited deleted the other's.
    Started at the same moment (five times), exactly one stays; a third is refused while it runs, also after the loser exited."""
    for round_ in range(5):
        pair = [_window_process(tmp_path, keys["release"]) for _ in range(2)]
        try:
            for p in pair:
                for _ in range(300):
                    if p.poll() is not None or "OpenBFME Launcher v" in p.log_path.read_text():
                        break
                    time.sleep(0.1)
            time.sleep(3.0)  # the loser says so and exits; the winner stays
            alive = [p for p in pair if p.poll() is None]
            gone = [p for p in pair if p.poll() is not None]
            assert len(alive) == 1 and len(gone) == 1, f"round {round_}: " + "\n".join(p.log_path.read_text()[-800:] for p in pair)
            assert gone[0].returncode == 1 and "Another OpenBFME Launcher (v0.0.0-dev) is already open" in gone[0].log_path.read_text()
            r = launch(tmp_path, None, keys["release"], "--list")
            assert r.code == 1 and r.has("Another OpenBFME Launcher (v0.0.0-dev) is already open"), "the loser's exit did not free the winner's lock"
        finally:
            for p in pair:
                if p.poll() is None:
                    p.terminate()
                    try:
                        p.wait(timeout=20)
                    except subprocess.TimeoutExpired:
                        p.kill()
                        p.wait()
    r = launch(tmp_path, None, keys["release"], "--list")
    assert r.code == 0, r


def _lock_port(tmp: Path) -> int:
    """the port of the one-launcher lock of tmp's data folder (instance_lock.gd: md5 of the data folder)"""
    d = normalized_path(data(tmp).resolve())
    return 17000 + int(hashlib.md5(d.encode()).hexdigest()[:8], 16) % 15000


def _legacy_lock_port(tmp: Path) -> int:
    """the port launchers before FLAKEFIX-2 used: 49200..65199, inside the operating systems' ephemeral port ranges"""
    d = normalized_path(data(tmp).resolve())
    return 49200 + int(hashlib.md5(d.encode()).hexdigest()[:8], 16) % 16000


# Linux 32768..60999, Windows 49152..65535 (and 1025..5000 before Vista), macOS 49152..65535
EPHEMERAL_RANGES = ((1025, 5000), (32768, 60999), (49152, 65535))


@pytest.mark.parametrize("holder", ["outgoing", "listening"])
def test_a_program_on_the_old_lock_port_does_not_stop_the_launcher(tmp_path, keys, holder):
    """FLAKEFIX-2: the lock port lay inside every OS's ephemeral range, so any program's outgoing connection could hold it and the launcher
    refused to start ("in use by another program"). The lock port now lies outside them; the old port only matters when an older launcher
    answers on it"""
    import socket
    import threading
    port = _lock_port(tmp_path)
    assert not any(first <= port <= last for first, last in EPHEMERAL_RANGES), port
    old = _legacy_lock_port(tmp_path)
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", old))
    stop = threading.Event()
    t = None
    if holder == "listening":  # a silent program listening there
        sock.listen(4)

        def serve() -> None:
            sock.settimeout(0.2)
            while not stop.is_set():
                try:
                    c, _ = sock.accept()
                except OSError:
                    continue
                c.settimeout(5)
                try:
                    c.recv(1)
                except OSError:
                    pass
                c.close()
        t = threading.Thread(target=serve, daemon=True)
        t.start()
    try:
        r = launch(tmp_path, None, keys["release"], "--list")
        assert r.code == 0 and not r.has("in use by another program") and not r.has("already open"), r
    finally:
        stop.set()
        if t is not None:
            t.join()
        sock.close()


def test_an_older_launcher_on_the_old_lock_port_is_still_seen(tmp_path, keys):
    """an older launcher (it answers its hello on the old port) still keeps a new one from starting with the same data folder"""
    import socket
    import threading
    old = _legacy_lock_port(tmp_path)
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.bind(("127.0.0.1", old))
    srv.listen(4)
    stop = threading.Event()

    def serve() -> None:
        srv.settimeout(0.2)
        while not stop.is_set():
            try:
                c, _ = srv.accept()
            except OSError:
                continue
            c.sendall(b"OPENBFME-LAUNCHER v0.4.9\n")
            c.settimeout(5)
            try:
                c.recv(1)
            except OSError:
                pass
            c.close()
    t = threading.Thread(target=serve, daemon=True)
    t.start()
    try:
        r = launch(tmp_path, None, keys["release"], "--list")
        assert r.code == 1 and r.has("Another OpenBFME Launcher (v0.4.9) is already open"), r
    finally:
        stop.set()
        t.join()
        srv.close()
    r = launch(tmp_path, None, keys["release"], "--list")
    assert r.code == 0, r


@pytest.mark.parametrize("answer", [b"", b"HTTP/1.0 200 OK\r\n\r\nnot a launcher\n"])
def test_another_program_on_the_lock_port_is_not_called_a_launcher(tmp_path, keys, answer):
    """Sol r4: any program listening on the lock's port gave "Another OpenBFME Launcher (v0.0.0-dev) is already open". The launcher reads the holder's
    hello: a silent program or one answering something else is named as such, with the port"""
    import socket
    import threading
    port = _lock_port(tmp_path)
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.bind(("127.0.0.1", port))
    srv.listen(4)
    stop = threading.Event()

    def serve() -> None:
        srv.settimeout(0.2)
        while not stop.is_set():
            try:
                c, _ = srv.accept()
            except OSError:
                continue
            if answer:
                c.sendall(answer)
            c.settimeout(5)
            try:
                c.recv(1)  # the launcher closes first (no TIME_WAIT left on the port)
            except OSError:
                pass
            c.close()
    t = threading.Thread(target=serve, daemon=True)
    t.start()
    try:
        r = launch(tmp_path, None, keys["release"], "--list")
        assert r.code == 1 and r.has(f"local port {port} is in use by another program") and not r.has("already open"), r
        r = launch(tmp_path, None, keys["release"], "--offline")
        assert r.code == 1 and r.has(f"local port {port} is in use by another program") and not r.has("already open"), r
    finally:
        stop.set()
        t.join()
        srv.close()
    r = launch(tmp_path, None, keys["release"], "--list")
    assert r.code == 0, r


# ---- lane ONLINECOUNT-1: "N players online" on the Play card, and the setting that turns it off ----------------------------------------------

STATS_GOOD = b'{"v":1,"players":7,"games_open":2,"games_running":1,"as_of":1700000000}'


def stats_requests(server: ReleaseServer) -> list[tuple[str, dict]]:
    return [(p, h) for p, h in server.requests if p.startswith("/v1/")]


def count_window(tmp: Path, server: ReleaseServer, key: bytes, *args: str) -> tuple[Run, list[str]]:
    """the launcher's window against `server`, which is also its lobby (--lobby=), headless with every text of every screen dumped"""
    launch(tmp, server, key, f"--lobby={server.origin}", *args)  # the CLI step: remembers the lobby (and the setting)
    r = subprocess.run([GODOT, "--headless", "--path", str(LAUNCHER), "--", f"--api-base={server.origin}", f"--trust-key={ed25519.public_key(key).hex()}",
                        f"--repo={TEST_REPO}", "--ui-dump", f"--screenshot={tmp / 'shot.png'}"],
                       env=dict(os.environ, XDG_DATA_HOME=str(tmp / "data")), capture_output=True, text=True, timeout=180, stdin=subprocess.DEVNULL)
    run = Run(r.returncode, r.stdout + r.stderr)
    return run, [ln.split("LAUNCHER ui text: ", 1)[1] for ln in run.lines if "LAUNCHER ui text: " in ln]


def test_the_play_card_shows_the_players_online(tmp_path, server, keys):
    server.stats = STATS_GOOD
    r, texts = count_window(tmp_path, server, keys["release"])
    assert r.has("online player count: 7 players online"), r
    assert any("7 players online" in t for t in texts), texts
    reqs = stats_requests(server)
    assert [p for p, _ in reqs] == ["/v1/stats"], reqs        # one request, to the lobby's stats path
    h = reqs[0][1]
    assert "authorization" not in h and "cookie" not in h, h  # nothing about the player
    assert h["user-agent"].startswith("OpenBFME-Launcher/"), h


@pytest.mark.parametrize("answer", [None, b"<html>no</html>", b'{"v":1,"players":-1,"games_open":0,"games_running":0}',
                                    b'{"v":1,"players":1.5,"games_open":0,"games_running":0}', b'{"v":2,"players":3,"games_open":0,"games_running":0}',
                                    b'{"v":1,"players":"3","games_open":0,"games_running":0}', b""])
def test_no_stats_answer_shows_no_number(tmp_path, server, keys, answer):
    """a lobby without stats (404), garbage or an out-of-range count: nothing is shown, never a made-up number, never a dialog"""
    server.stats = answer
    r, texts = count_window(tmp_path, server, keys["release"])
    assert r.has("online player count: not shown"), r
    assert not any("players online" in t or "player online" in t for t in texts), texts
    assert r.has("ui dialog: ") and not any(ln.rstrip().startswith("LAUNCHER ui dialog: ") and ln.strip() != "LAUNCHER ui dialog:" for ln in r.lines), r


def test_with_the_setting_off_the_lobby_is_never_asked(tmp_path, server, keys):
    server.stats = STATS_GOOD
    r, texts = count_window(tmp_path, server, keys["release"], "--online-count=off")
    assert r.has("online player count not asked") is False and not r.has("online player count: "), r
    assert stats_requests(server) == [], stats_requests(server)   # not at the start, not later: no request at all
    assert not any("players online" in t for t in texts), texts
    state = (data(tmp_path) / "state.cfg").read_text()
    assert "show_online_count=false" in state, state              # the game reads the same file (godot/scripts/online_stats.gd)
    # on again: asked again
    r, texts = count_window(tmp_path, server, keys["release"], "--online-count=on")
    assert r.has("online player count: 7 players online") and len(stats_requests(server)) == 1, r
    assert launch(tmp_path, server, keys["release"], "--online-count=maybe").code == 1


def test_an_offline_start_asks_nothing(tmp_path, server, keys):
    server.stats = STATS_GOOD
    launch(tmp_path, server, keys["release"], f"--lobby={server.origin}")
    r = subprocess.run([GODOT, "--headless", "--path", str(LAUNCHER), "--", f"--api-base={server.origin}", f"--trust-key={ed25519.public_key(keys['release']).hex()}",
                        f"--repo={TEST_REPO}", "--offline", f"--screenshot={tmp_path / 'shot.png'}"],
                       env=dict(os.environ, XDG_DATA_HOME=str(tmp_path / "data")), capture_output=True, text=True, timeout=180, stdin=subprocess.DEVNULL)
    assert r.returncode == 0, r.stdout + r.stderr
    assert stats_requests(server) == [], stats_requests(server)


def test_the_policy_allows_only_the_lobbys_stats_path():
    """NetPolicy: the lobby host, over https without a port, for GET /v1/stats alone; any other path or host stays refused"""
    text = (LAUNCHER / "scripts/net/net_policy.gd").read_text()
    assert 'const LOBBY_STATS_PATH := "/v1/stats"' in text and "u.path == LOBBY_STATS_PATH" in text


@pytest.mark.parametrize("size", ["1280x720", "1024x768", "1920x1080", "1600x900"])
@pytest.mark.parametrize("screen", ["main", "update", "downloading", "error"])
def test_the_players_chip_never_meets_whats_new(tmp_path, server, keys, screen, size):
    """lane ONLINECOUNT-1 r2 (Sol: the chip crossed What's New in the update state at 1280 wide and at 1024x768): every Play-card state with the widest
    count chip, at four layouts, reports no chip touching the notes card (the launcher measures the boxes: PlayScreen.chip_overlaps)"""
    r, texts = screen_texts(tmp_path, None, keys["release"], f"--ui-demo={screen}", f"--screenshot-size={size}")
    assert r.has("ui chip overlaps: ") and any("12345 players online" in t for t in texts), r
    assert any(ln.rstrip().endswith("ui chip overlaps:") for ln in r.lines), [ln for ln in r.lines if "chip overlaps" in ln]


def test_the_overlap_check_fails_closed(tmp_path, server, keys):
    """the checker itself: a chip placed over the card is reported (the test is not vacuous)"""
    src = (LAUNCHER / "scripts/ui/play_screen.gd").read_text()
    assert "func chip_overlaps() -> PackedStringArray:" in src and "intersects(card)" in src


@pytest.mark.parametrize("value,asks", [(None, True), ("true", True), ("false", False), ('"yes"', False), ('"false"', False), ("[1, 2]", False),
                                        ('{"a": 1}', False), ("1", False), ("0", False)])
def test_malformed_online_count_settings_never_crash(tmp_path, server, keys, value, asks):
    """lane ONLINECOUNT-1 r2 (Sol: a string / list / dictionary crashed the reader): a missing key (an older launcher's state.cfg) is on; true / false
    are what they say; anything else is off, named in the log, and never a script error"""
    server.stats = STATS_GOOD
    launch(tmp_path, server, keys["release"], f"--lobby={server.origin}")
    state = data(tmp_path) / "state.cfg"
    text = state.read_text()
    state.write_text(text + ("" if value is None else f"\n[launcher]\nshow_online_count={value}\n") if "[launcher]" not in text else
                     text.replace("[launcher]\n", "[launcher]\n" + ("" if value is None else f"show_online_count={value}\n"), 1))
    r, texts = count_window(tmp_path, server, keys["release"])
    assert not r.has("SCRIPT ERROR") and not r.has("Nonexistent"), r
    n = len(stats_requests(server))
    assert (n == 1) == asks and n <= 1, (n, r)
    if value is not None and value not in ("true", "false", "1", "0"):
        assert r.has("is not true or false"), r
    assert any("7 players online" in t for t in texts) == asks, texts
