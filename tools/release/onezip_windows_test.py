#!/usr/bin/env python3
"""Lane ONEZIP-1: the Windows launchers already published update to a release in the one-download layout, and the one download works
on Windows, under Wine or natively.

    Linux + Wine:  WINEPREFIX=<own prefix> python3 tools/release/onezip_windows_test.py --work DIR --wine <wine binary> \\
                       --fake-game GAME.exe --published DIR [--tags v0.4.0-alpha.3,...]
    WSL on Windows: python3 tools/release/onezip_windows_test.py --work DIR --interop --fake-game GAME.exe --published DIR \\
                       --launchers DIR --key DIR/key.secret       (no window: the swap step is skipped by name, stop S-4480)

--published: <DIR>/<tag>/openbfme-launcher-<tag>-windows-x64.zip, the launcher archives of the GitHub releases as testers downloaded
them (gh release download <tag> -R Open-BFME/openbfme-godot -p 'openbfme-launcher-*'). Not rebuilt from source: the published
executables, with two values of the build information in the embedded pack changed in place (patch_published: the release key becomes
the test key, "test_build" becomes true, so the launcher takes --api-base for the local stand-in of GitHub); every byte of code is the
published one. --fake-game: tools/release/launcher_fake_game.c built for Windows (stands in for OpenBFME.exe). The new launcher is
this tree's, exported here as a Windows test build (build_launcher.sh), or prebuilt with --launchers (a folder holding
openbfme-launcher-<NEW>-windows-x64.zip for each NEW below, and --key).

Per published launcher (RESULT <tag>: <step>: PASS | FAIL):
  install + play     it installs the combined package (the game with the new launcher in it) as its game and plays it
  staged             it stages the new launcher from the launcher archive
  swap               its next start with a window swaps itself for the new launcher, which confirms its start
  new launcher plays the new launcher plays the version the old one installed
Then the one download as a new player unpacks it (the combined zip of this run):
  ready to play      the launcher offers the game beside it, with no download, and plays it
  game start         --game-start: "continue", the next release installed in the background beside the folder, the folder unchanged
  next start         --game-start again: "start <newer>" (protocol 2), the game's side (this test) starts it and reports it
Under Wine the launcher's data folder is the prefix's Roaming\\OpenBFMELauncher, DELETED before each launcher (use a test prefix).
Exit 0 when every step passed (a skipped swap is not a pass: the run then exits 4, like an asset-free run, and says so).
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import ed25519  # noqa: E402
import make_archive  # noqa: E402
import make_manifest  # noqa: E402
from launcher_test_server import COMMIT, TEST_REPO, ReleaseServer, tiny_package  # noqa: E402
from launcher_windows_test import KILL_POINTS  # noqa: E402
import seal_package  # noqa: E402

SECRET = [b""]   # the test's release key (its secret), set in main()

EXE = "OpenBFMELauncher.exe"
# every launcher published so far and the release each one can read as newer than itself (the launchers up to v0.3.0-preview.4 know
# no -alpha tag: their next release is a preview bridge, autorelease.py next_bridge)
PUBLISHED = {"v0.3.0-preview.1": "v0.3.0-preview.9", "v0.3.0-preview.2": "v0.3.0-preview.9", "v0.3.0-preview.3": "v0.3.0-preview.9",
             "v0.3.0-preview.4": "v0.3.0-preview.9", "v0.3.0-preview.6": "v0.4.0-alpha.9", "v0.4.0-alpha.1": "v0.4.0-alpha.9",
             "v0.4.0-alpha.2": "v0.4.0-alpha.9", "v0.4.0-alpha.3": "v0.4.0-alpha.9"}
NEXT = "v0.4.0-alpha.10"   # the release after NEW, for the game-start steps
SWAP_TO = "v0.4.0-alpha.11"  # a real launcher of this tree the headless self-replacement swaps in


def patch_published(exe: bytes, test_key: bytes) -> bytes:
    """the published executable with its build information's release key replaced by the test key and test_build set (same length,
    nothing else touched); asserts that each value is there exactly once"""
    real = (REPO / "launcher/release_key.pub").read_text().strip()
    key_field = f'"release_key": "{real}"'.encode()
    flag = b'"test_build": false'
    assert exe.count(key_field) == 1, "the published launcher does not carry the release key of launcher/release_key.pub exactly once"
    assert exe.count(flag) == 1, "the published launcher's build information has no test_build: false"
    out = exe.replace(key_field, f'"release_key": "{ed25519.public_key(test_key).hex()}"'.encode()).replace(flag, b'"test_build": true ')
    assert len(out) == len(exe) and sum(a != b for a, b in zip(out, exe)) <= 64 + 5
    return out


def combined_zip(work: Path, version: str, fake_game: Path, launcher_exe: bytes) -> bytes:
    top = work / f"build-{version}-{os.urandom(3).hex()}" / f"openbfme-{version}-windows-x64"
    top.mkdir(parents=True)
    shutil.copy(fake_game, top / "OpenBFME.exe")
    (top / "OpenBFME.pck").write_bytes(b"pack of " + version.encode())
    (top / "VERSION").write_bytes(f"OpenBFME {version}\n".encode())
    (top / EXE).write_bytes(launcher_exe)
    out = top.parent / f"openbfme-{version}-windows-x64.zip"
    make_archive.zip_file(top, out, 1700000000)
    seal_package.seal_archive(out, SECRET[0], TEST_REPO)   # signed payload inventory, as a release's (seal_package.py)
    return out.read_bytes()


def release(work: Path, version: str, secret: bytes, game: bytes, launcher_zip: bytes | None, launcher_exe: bytes, fmt: int = 2) -> dict:
    d = work / f"release-{version}-{os.urandom(3).hex()}"
    d.mkdir(parents=True)
    gl, gw = make_manifest.asset_name("game", version, "linux-x64"), make_manifest.asset_name("game", version, "windows-x64")
    arch = {gl: tiny_package(gl, {"OpenBFME.x86_64": b"linux game", "OpenBFMELauncher.x86_64": b"linux launcher"}), gw: game}
    if fmt == 2:
        ll, lw = make_manifest.asset_name("launcher", version, "linux-x64"), make_manifest.asset_name("launcher", version, "windows-x64")
        arch[ll] = tiny_package(ll, {"OpenBFMELauncher.x86_64": b"linux launcher"})
        arch[lw] = launcher_zip if launcher_zip is not None else tiny_package(lw, {EXE: launcher_exe})
    sums = "".join(f"{hashlib.sha256(b).hexdigest()}  {n}\n" for n, b in arch.items())
    for n, b in arch.items():
        (d / n).write_bytes(b)
    (d / f"SHA256SUMS-{version}.txt").write_text(sums)
    manifest = make_manifest.dumps(make_manifest.build(d, version, TEST_REPO, COMMIT, time.strftime("%Y-%m-%d", time.gmtime()), fmt))
    return arch | {"manifest.json": manifest, "manifest.json.sig": ed25519.sign(secret, manifest)}


def tree(folder: Path) -> dict[str, str]:
    return {str(p.relative_to(folder)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(folder.rglob("*")) if p.is_file()}


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--work", type=Path, required=True)
    ap.add_argument("--wine")
    ap.add_argument("--interop", action="store_true", help="WSL on Windows: the .exe files run natively, without a window")
    ap.add_argument("--fake-game", type=Path, required=True)
    ap.add_argument("--published", type=Path, required=True)
    ap.add_argument("--tags", default=",".join(PUBLISHED))
    ap.add_argument("--launchers", type=Path, help="prebuilt Windows test launchers of this tree (with --key)")
    ap.add_argument("--key", type=Path)
    ap.add_argument("--godot", default=os.environ.get("GODOT", "godot"))
    ap.add_argument("--skip-swap", action="store_true", help="leave out the self-replacement steps (a quick rerun of the others)")
    a = ap.parse_args(argv)
    if bool(a.wine) == a.interop:
        ap.error("exactly one of --wine <binary> and --interop")
    tags = [] if a.tags == "none" else [t for t in a.tags.split(",") if t]   # none: this tree's launcher only
    for t in tags:
        if t not in PUBLISHED:
            ap.error(f"{t} is not a published launcher ({', '.join(PUBLISHED)})")
        if not (a.published / t / f"openbfme-launcher-{t}-windows-x64.zip").is_file():
            print(f"SKIP: openbfme-launcher-{t}-windows-x64.zip is not in {a.published}/{t} (gh release download)")
            return 1
    work = a.work.resolve()
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    wine = a.wine is not None
    news = sorted({PUBLISHED[t] for t in tags} | {"v0.4.0-alpha.9", SWAP_TO})
    if a.launchers:
        secret, builds = a.key.read_bytes(), a.launchers
    else:
        secret, builds = os.urandom(32), work / "launchers"
        builds.mkdir()
        (builds / "key.pub").write_text(ed25519.public_key(secret).hex() + "\n")
        (builds / "key.secret").write_bytes(secret)
        for v in news:
            r = subprocess.run([str(HERE / "build_launcher.sh"), "--out", str(builds), "--version", v, "--commit", COMMIT, "--repo", TEST_REPO,
                                "--key-file", str(builds / "key.pub"), "--mtime", "1700000000", "--platforms", "windows", "--worktree",
                                "--test-build", "--godot", a.godot], capture_output=True, text=True)
            if r.returncode != 0:
                print(r.stdout[-2000:], r.stderr[-2000:])
                return 1
    SECRET[0] = secret
    new_zip = {v: (builds / f"openbfme-launcher-{v}-windows-x64.zip").read_bytes() for v in news}
    new_exe = {v: zipfile.ZipFile(io.BytesIO(z)).read(f"openbfme-launcher-{v}-windows-x64/{EXE}") for v, z in new_zip.items()}

    def winpath(p: Path) -> str:
        if wine:
            return "Z:" + str(p).replace("/", "\\")
        return subprocess.run(["wslpath", "-w", str(p)], capture_output=True, text=True, check=True).stdout.strip()

    if wine:
        roaming = [r for r in sorted(Path(os.environ["WINEPREFIX"]).glob("drive_c/users/*/AppData/Roaming")) if r.parent.parent.name != "Public"]
        if len(roaming) != 1:
            print(f"cannot tell the prefix's user folder: {roaming}")
            return 1
        data = roaming[0] / "OpenBFMELauncher"
        env = dict(os.environ, WINEDEBUG=os.environ.get("WINEDEBUG", "-all"))
    else:
        appdata = work / "appdata"
        appdata.mkdir()
        data = appdata / "OpenBFMELauncher"
        env = dict(os.environ, APPDATA=winpath(appdata))
        env["WSLENV"] = ":".join(filter(None, [os.environ.get("WSLENV", ""), "APPDATA", "OPENBFME_TEST_PLAYED"]))
    played = work / "played.txt"
    env["OPENBFME_TEST_PLAYED"] = winpath(played)
    srv = ReleaseServer()
    results: dict[str, bool] = {}
    skipped: list[str] = []

    def run(inst: Path, *args: str, headless: bool = False) -> str:
        """one start of inst's launcher; returns the log lines it (and an update helper and new launcher it started) appended"""
        log = data / "launcher.log"
        before = log.stat().st_size if log.is_file() else 0
        # with its window under Wine (on Windows a staged launcher update is applied only at a start with a window); --interop has none
        cmd = ([a.wine] if wine else []) + [str(inst / EXE)] + (["--headless"] if a.interop or headless else []) + ["--", f"--api-base={srv.origin}", f"--repo={TEST_REPO}", *args]
        with open(work / "out.txt", "w") as out:  # a file, not a pipe: Wine's services would hold a pipe open
            subprocess.run(cmd, env=env, stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=900, cwd=inst)
        t0, last, size = time.time(), time.time(), -1
        while time.time() - t0 < 240:
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
        print(f"RESULT {name}: {'PASS' if ok else 'FAIL'}", flush=True)
        if not ok:
            print("\n".join(ln for ln in log.splitlines() if "LAUNCHER" in ln)[-3000:], flush=True)
            if " not true: " in log:   # the step's own account of what failed
                print("  not true: " + log.rsplit(" not true: ", 1)[1][:600], flush=True)

    def wait_played(text: str) -> bool:
        for _ in range(150):
            if played.is_file() and text.lower() in played.read_text(errors="replace").replace("/", "\\").lower():
                return True
            time.sleep(0.1)
        return False

    def fresh(name: str) -> None:
        shutil.rmtree(data, ignore_errors=True)
        played.unlink(missing_ok=True)
        srv.releases.clear()
        srv.files.clear()
        srv.requests.clear()

    try:
        for old in tags:
            new = PUBLISHED[old]
            fresh(old)
            inst_root = work / f"inst-{old}"
            with zipfile.ZipFile(a.published / old / f"openbfme-launcher-{old}-windows-x64.zip") as z:
                z.extractall(inst_root)
            inst = inst_root / f"openbfme-launcher-{old}-windows-x64"
            (inst / EXE).write_bytes(patch_published((inst / EXE).read_bytes(), secret))
            (inst / EXE).chmod(0o755)
            srv.publish(new, release(work, new, secret, combined_zip(work, new, a.fake_game, new_exe[new]), new_zip[new], new_exe[new]))
            log = run(inst, "--channel=preview", "--update", "--self-update", "--play")
            game_dir = data / "versions" / new / "game"
            step(f"{old}: install + play", f"update installed {new}" in log and wait_played(f"versions\\{new}\\game\\OpenBFME.exe")
                 and (game_dir / EXE).is_file() and (game_dir / EXE).read_bytes() == new_exe[new], log)
            step(f"{old}: staged", f"launcher update {new} staged" in log and (inst / "update" / EXE).read_bytes() == new_exe[new], log)
            if a.interop:
                print(f"SKIP {old}: swap: a start without a window does not apply a launcher update on Windows (stop S-4480)", flush=True)
                skipped.append(f"{old}: swap")
                continue
            log = run(inst, "--version")
            step(f"{old}: swap", f"{new} started after the update from {old}" in log and f"the new launcher {new} confirmed its start" in log
                 and (inst / EXE).read_bytes() == new_exe[new], log)
            played.unlink(missing_ok=True)
            log = run(inst, "--list", "--play")
            step(f"{old}: new launcher plays", f"OpenBFME Launcher {new}" in log and f"installed {new}" in log
                 and wait_played(f"versions\\{new}\\game\\OpenBFME.exe") and not (inst / "update").exists(), log)

        # the one download as a new player unpacks it
        new = "v0.4.0-alpha.9"
        fresh("one download")
        home_root = work / "unpacked"
        with zipfile.ZipFile(io.BytesIO(combined_zip(work, new, a.fake_game, new_exe[new]))) as z:
            z.extractall(home_root)
        home = home_root / f"openbfme-{new}-windows-x64"
        before = tree(home)
        log = run(home, "--list", "--play")
        step("one download: ready to play", f"installed {new} (the game beside the launcher)" in log and not (data / "versions").exists()
             and wait_played(f"openbfme-{new}-windows-x64\\OpenBFME.exe") and srv.requests == [], log)
        srv.publish(new, release(work, new, secret, combined_zip(work, new, a.fake_game, new_exe[new]), new_zip[new], new_exe[new]))
        # NEXT's game is a program that stays running (Windows' own ping.exe under the game's name), so the game's side can report the
        # real Windows process id of a live game (Sol r2: the harness reported an invented one after the stand-in had ended)
        stand_in = (Path(os.environ["WINEPREFIX"]) / "drive_c/windows/system32/ping.exe") if wine else Path("/mnt/c/Windows/System32/PING.EXE")
        srv.publish(NEXT, release(work, NEXT, secret, combined_zip(work, NEXT, stand_in, b"MZ the launcher of " + NEXT.encode()), None,
                                  b"MZ the launcher of " + NEXT.encode(), fmt=3))
        played.unlink(missing_ok=True)

        def request(args: list[str]) -> Path:
            req = work / f"request-{os.urandom(3).hex()}"
            req.mkdir()
            (req / "request.json").write_text(json.dumps({"protocol": 2, "pid": os.getpid()}))
            (req / "open").write_bytes(b"")
            return req

        def read(req: Path, name: str) -> dict:
            return json.loads((req / name).read_text()) if (req / name).is_file() else {}

        req = request([])
        log = run(home, f"--game-start={winpath(req)}", headless=True)   # as the game runs it: without a window
        after = {k: v for k, v in tree(home).items() if not k.replace("\\", "/").startswith("update/") and not k.endswith(".old")}
        step("one download: game start", read(req, "answer").get("action") == "continue" and (req / "open").exists()
             and read(req, "status") == {"text": f"{NEXT} is installed. It starts the next time you start OpenBFME.", "final": True}
             and (data / "versions" / NEXT / "game" / "OpenBFME.exe").is_file() and not played.exists()
             and {k: v for k, v in after.items() if k != EXE} == {k: v for k, v in before.items() if k != EXE}, log + json.dumps(read(req, "status")))
        req = request([])

        live: dict = {}

        def windows_pids(image: str) -> list[int]:
            """the Windows process ids of the running programs named `image` (tasklist, CSV)"""
            r = subprocess.run(([a.wine] if wine else []) + ["tasklist.exe", "/FO", "CSV", "/NH"], env=env, stdin=subprocess.DEVNULL,
                               capture_output=True, text=True, timeout=60)
            out = []
            for line in r.stdout.splitlines():
                cells = [c.strip('"') for c in line.strip().split('","')]
                if len(cells) > 1 and cells[0].lower() == image.lower() and cells[1].isdigit():
                    out.append(int(cells[1]))
            return out

        def the_game() -> None:
            """protocol 2's game side: on "start <version>", start that version's game (it keeps running), report its real Windows
            process id, then close the request"""
            for _ in range(1200):
                if (req / "answer").is_file():
                    got = read(req, "answer")
                    if got.get("action") == "start":
                        exe = data / "versions" / got["version"] / "game" / "OpenBFME.exe"
                        before = set(windows_pids("OpenBFME.exe"))
                        live["proc"] = subprocess.Popen(([a.wine] if wine else []) + [str(exe), "-n", "60", "127.0.0.1"], env=env,
                                                        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                        for _ in range(100):
                            new_pids = sorted(set(windows_pids("OpenBFME.exe")) - before)
                            if new_pids:
                                live["pid"] = new_pids[0]
                                break
                            time.sleep(0.05)
                        (req / "started.tmp").write_text(json.dumps({"protocol": 2, "pid": live.get("pid", 0), "version": got["version"]}))
                        (req / "started.tmp").rename(req / "started")
                    (req / "open").rename(req / "closed")
                    return
                time.sleep(0.05)
        import threading
        game = threading.Thread(target=the_game)
        game.start()
        log = run(home, f"--game-start={winpath(req)}", headless=True)
        game.join(timeout=200)
        checks = {"answer": read(req, "answer") == {"protocol": 2, "action": "start", "version": NEXT,
                                                   "why": f"{NEXT} is installed: it starts instead of {new}"},
                  "started": (req / "started").is_file(), "a live game with its Windows process id": live.get("pid", 0) > 0,
                  "recorded as running": f"the game started {NEXT} (pid {live.get('pid')})" in log,
                  "lock released": not (data / "versions.lock").exists()}
        if live.get("pid"):   # this test's own stand-in, by its process id
            subprocess.run(([a.wine] if wine else []) + ["taskkill.exe", "/PID", str(live["pid"]), "/F"], env=env, stdin=subprocess.DEVNULL,
                           capture_output=True, timeout=60)
        if live.get("proc"):
            live["proc"].kill()
        step("one download: next start", all(checks.values()), log + json.dumps(read(req, "answer")) + " not true: "
             + ", ".join(k for k, v in checks.items() if not v) + " pid: " + str(live.get("pid")))
        # this tree's launcher replaces itself WITHOUT a window (a game started directly runs it that way; LAUNCH-1 applied a swap on
        # Windows only at a start with a window, stop S-4480): once, then killed at each step of the swap, every later start (also
        # without a window) runs a complete launcher and the update completes
        old_exe = new_exe["v0.4.0-alpha.9"]
        for point in ([""] + KILL_POINTS) if not a.skip_swap else []:
            fresh("swap " + point)
            root = work / f"swap-{point or 'clean'}"
            with zipfile.ZipFile(io.BytesIO(combined_zip(work, "v0.4.0-alpha.9", a.fake_game, old_exe))) as z:
                z.extractall(root)
            home = root / "openbfme-v0.4.0-alpha.9-windows-x64"
            srv.publish(SWAP_TO, release(work, SWAP_TO, secret, combined_zip(work, SWAP_TO, a.fake_game, new_exe[SWAP_TO]), new_zip[SWAP_TO],
                                         new_exe[SWAP_TO]))
            log = run(home, "--self-update", headless=True)
            log += run(home, "--version", *([f"--test-kill-at={point}"] if point else []), headless=True)
            for _ in range(4):
                if (home / EXE).read_bytes() == new_exe[SWAP_TO] and not (data / "launcher-update.json").exists() and not (home / "update").exists():
                    break
                log += run(home, "--version", headless=True)
            step(f"headless self-replacement{' killed at ' + point if point else ''}",
                 (home / EXE).read_bytes() == new_exe[SWAP_TO] and not (data / "launcher-update.json").exists()
                 and not (home / "update").exists() and not list(home.glob(EXE + ".*"))
                 and (home / "launcher-manifest.json").is_file()   # the signed proof of the new launcher beside the game
                 and (not point or f"test kill at {point}" in log), log)
    finally:
        srv.stop()
    want = (4 if not a.interop else 2) * len(tags) + 3 + (0 if a.skip_swap else 1 + len(KILL_POINTS))
    how = " (Wine)" if wine else " (native, no window)"
    print(f"RESULT the one download on Windows{how}: {sum(results.values())}/{len(results)} steps passed"
          + (f", {len(skipped)} skipped by name" if skipped else ""))
    if not all(results.values()) or len(results) != want:
        return 1
    return 4 if skipped else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
