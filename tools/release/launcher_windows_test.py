#!/usr/bin/env python3
"""Lane LAUNCH-1: the Windows launcher against a local release server, under Wine or natively on Windows.

    Linux + Wine:  WINEPREFIX=<own prefix> python3 tools/release/launcher_windows_test.py --work DIR --wine <wine binary> --fake-game GAME.exe
    Windows:       python tools/release/launcher_windows_test.py --work DIR --launchers DIR --fake-game GAME.exe

--fake-game: a small Windows program standing in for OpenBFME.exe (tools/release/launcher_fake_game.c, built with any Windows C compiler):
it appends "played game <path>" to the file named by OPENBFME_TEST_PLAYED. On Linux the two Windows test launchers (v0.2.0 and v0.3.0,
test builds) are exported here with build_launcher.sh; on Windows (no bash) pass --launchers, a folder holding
openbfme-launcher-v0.2.0-windows-x64.zip and openbfme-launcher-v0.3.0-windows-x64.zip made on Linux with
    tools/release/build_launcher.sh --out DIR --version v0.2.0 --commit <40 hex> --repo Test/repo --key-file DIR/key.pub --mtime 1700000000 \\
      --platforms windows --worktree --test-build          (the same with v0.3.0; DIR/key.pub from --write-key DIR first)
and --key DIR/key.secret (written by --write-key).

The launcher runs with its window (under Wine: DISPLAY must name an X display; on the Deck DISPLAY=:1), as a tester starts it: on
Windows a staged launcher update is applied only at a start with a window. Steps (each prints RESULT <step>: PASS | FAIL): fresh install + play (the fake game ran), a tampered asset rejected, an update with a
staged launcher, the self-update swap (the running .exe renamed to .old, the new one confirms its start), the .old files removed on the
next start, and the swap killed at each of its 8 steps (every later start runs a complete launcher and the update completes).
The launcher's data folder is %APPDATA%\\OpenBFMELauncher. On native Windows Godot follows the APPDATA variable (the
coordinator's JonathanPC run), so the test points APPDATA into --work and nothing of the user's is touched. Under Wine (Proton 11.0)
Godot did not follow it: the prefix's own Roaming folder is used and its OpenBFMELauncher folder is DELETED first (use a test prefix).
Exit 0 when every step passed.
Wine setup (Proton 11.0's files/bin/wine on the Deck): a fresh WINEPREFIX (`wine wineboot -i`), then copy Proton's
files/lib/vkd3d/x86_64-windows/libvkd3d-*.dll into $WINEPREFIX/drive_c/windows/system32 (Godot's Windows template imports dxgi).
"""
from __future__ import annotations

import argparse
import hashlib
import io
import os
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import ed25519  # noqa: E402
import make_archive  # noqa: E402
import make_manifest  # noqa: E402
from launcher_test_server import COMMIT, TEST_REPO, ReleaseServer, tiny_package  # noqa: E402

EXE = "OpenBFMELauncher.exe"
KILL_POINTS = ["swap-journal", "swap-helper-started", "helper-start", "helper-backup", "helper-copied", "helper-replaced",
               "helper-placed", "after-update-confirmed"]


def winpath(p: Path, wine: bool) -> str:
    return "Z:" + str(p).replace("/", "\\") if wine else str(p)


def game_zip(work: Path, version: str, fake_game: Path) -> bytes:
    top = work / f"game-{version}" / f"openbfme-{version}-windows-x64"
    top.mkdir(parents=True, exist_ok=True)
    shutil.copy(fake_game, top / "OpenBFME.exe")
    (top / "OpenBFME.pck").write_bytes(b"pack of " + version.encode())
    out = work / f"openbfme-{version}-windows-x64.zip"
    make_archive.zip_file(top, out, 1700000000)
    return out.read_bytes()


def release(work: Path, version: str, secret: bytes, game: bytes, launcher: bytes | None) -> dict[str, bytes]:
    d = work / f"release-{version}-{os.urandom(3).hex()}"
    d.mkdir(parents=True)
    lg, ll, lw = (make_manifest.asset_name("game", version, "linux-x64"), make_manifest.asset_name("launcher", version, "linux-x64"),
                  make_manifest.asset_name("launcher", version, "windows-x64"))
    arch = {lg: tiny_package(lg, {"OpenBFME.x86_64": b"linux game"}),
            make_manifest.asset_name("game", version, "windows-x64"): game,
            ll: tiny_package(ll, {"OpenBFMELauncher.x86_64": b"linux launcher"}),
            lw: launcher or tiny_package(lw, {"OpenBFMELauncher.exe": b"no launcher"})}
    sums = "".join(f"{hashlib.sha256(b).hexdigest()}  {n}\n" for n, b in arch.items())
    for n, b in arch.items():
        (d / n).write_bytes(b)
    (d / f"SHA256SUMS-{version}.txt").write_text(sums)
    manifest = make_manifest.dumps(make_manifest.build(d, version, TEST_REPO, COMMIT, "2026-10-09"))
    return arch | {"manifest.json": manifest, "manifest.json.sig": ed25519.sign(secret, manifest)}


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--work", type=Path, required=True)
    ap.add_argument("--wine")
    ap.add_argument("--fake-game", type=Path)
    ap.add_argument("--launchers", type=Path, help="prebuilt Windows test launchers (on Windows)")
    ap.add_argument("--key", type=Path, help="the test key's secret (with --launchers)")
    ap.add_argument("--write-key", type=Path, help="write a new test key (key.secret, key.pub) into this folder and exit")
    ap.add_argument("--godot", default=os.environ.get("GODOT", "godot"))
    a = ap.parse_args(argv)
    if a.write_key:
        a.write_key.mkdir(parents=True, exist_ok=True)
        secret = os.urandom(32)
        (a.write_key / "key.secret").write_bytes(secret)
        (a.write_key / "key.pub").write_text(ed25519.public_key(secret).hex() + "\n")
        print(f"test key written to {a.write_key}")
        return 0
    if not a.fake_game:
        ap.error("--fake-game is required")
    work = a.work.resolve()
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    wine = a.wine is not None
    if a.launchers:
        secret = a.key.read_bytes()
        builds = a.launchers
    else:
        secret = os.urandom(32)
        builds = work / "launchers"
        builds.mkdir()
        (builds / "key.pub").write_text(ed25519.public_key(secret).hex() + "\n")
        for v in ("v0.2.0", "v0.3.0"):
            r = subprocess.run([str(HERE / "build_launcher.sh"), "--out", str(builds), "--version", v, "--commit", COMMIT, "--repo", TEST_REPO,
                                "--key-file", str(builds / "key.pub"), "--mtime", "1700000000", "--platforms", "windows", "--worktree",
                                "--test-build", "--godot", a.godot], capture_output=True, text=True)
            if r.returncode != 0:
                print(r.stdout[-2000:], r.stderr[-2000:])
                return 1
    zips = {v: (builds / f"openbfme-launcher-{v}-windows-x64.zip").read_bytes() for v in ("v0.2.0", "v0.3.0")}
    with zipfile.ZipFile(builds / "openbfme-launcher-v0.2.0-windows-x64.zip") as z:
        z.extractall(work / "inst")
    inst = work / "inst" / "openbfme-launcher-v0.2.0-windows-x64"
    if wine:
        roaming = sorted(Path(os.environ["WINEPREFIX"]).glob("drive_c/users/*/AppData/Roaming"))
        roaming = [r for r in roaming if r.parent.parent.name != "Public"]
        if len(roaming) != 1:
            print(f"cannot tell the prefix's user folder: {roaming}")
            return 1
        data = roaming[0] / "OpenBFMELauncher"
    else:
        appdata = work / "appdata"
        appdata.mkdir()
        data = appdata / "OpenBFMELauncher"
    if data.exists():
        shutil.rmtree(data)
    played = work / "played.txt"
    env = dict(os.environ, OPENBFME_TEST_PLAYED=winpath(played, wine))
    if not wine:
        env["APPDATA"] = str(appdata)
    if wine:
        env.setdefault("WINEDEBUG", "-all")
    srv = ReleaseServer()
    results: dict[str, bool] = {}

    def run(*args: str) -> str:
        """one start; returns the log lines it (and an update helper and new launcher it started) appended"""
        log = data / "launcher.log"
        before = log.stat().st_size if log.is_file() else 0
        # with its window (DISPLAY under Wine): on Windows a staged launcher update is applied only at a start with a window
        cmd = ([a.wine] if wine else []) + [str(inst / EXE), "--", f"--api-base={srv.origin}", *args]
        with open(work / "out.txt", "w") as out:  # a file, not a pipe: Wine's services would hold a pipe open
            subprocess.run(cmd, env=env, stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=600)
        # a self-update goes on in the helper and the new launcher after this process ended: wait until the journal is settled and
        # the log is quiet
        t0, last, size = time.time(), time.time(), -1
        while time.time() - t0 < 180:
            j = data / "launcher-update.json"
            busy = j.is_file() and '"confirmed"' not in j.read_text(errors="replace") and '"rolled-back"' not in j.read_text(errors="replace")
            now = log.stat().st_size if log.is_file() else 0
            if now != size:
                size, last = now, time.time()
            if not busy and time.time() - last > 3:
                break
            time.sleep(0.5)
        if not log.is_file():
            return ""
        with open(log, "rb") as f:
            f.seek(before if log.stat().st_size >= before else 0)
            return f.read().decode("utf-8", "replace")

    def step(name: str, ok: bool, log: str) -> None:
        results[name] = ok
        print(f"RESULT {name}: {'PASS' if ok else 'FAIL'}")
        if not ok:
            print("\n".join(ln for ln in log.splitlines() if "LAUNCHER" in ln)[-3000:])

    try:
        srv.publish("v0.2.0", release(work, "v0.2.0", secret, game_zip(work, "v0.2.0", a.fake_game), None))
        log = run("--update", "--list", "--play")
        for _ in range(100):
            if played.is_file():
                break
            time.sleep(0.1)
        step("fresh install + play", "update installed v0.2.0" in log and played.is_file()
             and "played game" in played.read_text(errors="replace"), log)
        bad = release(work, "v0.3.0", secret, game_zip(work, "v0.3.0", a.fake_game), zips["v0.3.0"])
        good_game = bad[make_manifest.asset_name("game", "v0.3.0", "windows-x64")]
        bad[make_manifest.asset_name("game", "v0.3.0", "windows-x64")] = good_game[:-1] + bytes([good_game[-1] ^ 1])
        srv.publish("v0.3.0", bad)
        log = run("--update")
        step("tampered asset rejected", "does not match the signed manifest" in log and not (data / "versions" / "v0.3.0").exists(), log)
        srv.publish("v0.3.0", release(work, "v0.3.0", secret, good_game, zips["v0.3.0"]))
        log = run("--update", "--self-update")
        step("update + launcher staged", "update installed v0.3.0" in log and "launcher update v0.3.0 staged" in log, log)
        log = run("--version")
        step("self-update swap", "v0.3.0 started after the update from v0.2.0" in log and "the new launcher v0.3.0 confirmed its start" in log
             and (inst / (EXE + ".old")).is_file(), log)
        log = run("--version")
        step("previous launcher removed", "OpenBFME Launcher v0.3.0" in log and not list(inst.glob("*.old"))
             and not (inst / "update").exists() and not (data / "launcher-update.json").exists(), log)
        # the swap killed at each of its steps (--test-kill-at, test builds): every later start runs a complete launcher and the update
        # completes (Sol r1; on Windows the atomic step is MoveFileEx through cmd /c move /y)
        new_sha = hashlib.sha256(zipfile.ZipFile(io.BytesIO(zips["v0.3.0"])).read(f"openbfme-launcher-v0.3.0-windows-x64/{EXE}")).hexdigest()
        for point in KILL_POINTS:
            shutil.rmtree(work / "inst")
            with zipfile.ZipFile(builds / "openbfme-launcher-v0.2.0-windows-x64.zip") as z:
                z.extractall(work / "inst")
            (data / "launcher-update.json").unlink(missing_ok=True)
            log = run("--self-update")
            log += run("--version", f"--test-kill-at={point}")
            killed = f"test kill at {point}" in log
            ok, starts = killed, 0
            for _ in range(4):
                log += run("--version")
                starts += 1
                ok = ok and "OpenBFME Launcher v0." in log.splitlines()[-1] + log  # every start logs its version line
                if (hashlib.sha256((inst / EXE).read_bytes()).hexdigest() == new_sha and not (inst / "update").exists()
                        and not (data / "launcher-update.json").exists() and not list(inst.glob("*.old"))):
                    break
            done = hashlib.sha256((inst / EXE).read_bytes()).hexdigest() == new_sha and not (data / "launcher-update.json").exists()
            step(f"swap killed at {point}", ok and done and not list(inst.glob(EXE + ".*")), log)
    finally:
        srv.stop()
    print(f"RESULT launcher on Windows{' (Wine)' if wine else ''}: {sum(results.values())}/{len(results)} steps passed")
    return 0 if all(results.values()) and len(results) == 5 + len(KILL_POINTS) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
