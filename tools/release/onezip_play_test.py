#!/usr/bin/env python3
"""Lane ONEZIP-1: the real game, started directly from the one download, keeps itself up to date through the launcher beside it.

    DISPLAY=:1 ROTWK_INSTALL=... BFME2_INSTALL=... python3 tools/release/onezip_play_test.py --package <openbfme-<v>-linux-x64.tar.gz> \\
        --work DIR [--shots DIR]

--package: a combined Linux package made by tools/release/package.sh (the real game and launcher) with --release-key <the public half
of --secret> --repo Test/repo, so the packaged game trusts the test key (ONEZIP-1 r2: it checks the launcher before it starts it). The game runs with its window (a
start without one asks no launcher), its user data under --work. The package's launcher trusts the real release key and talks only
to GitHub, so it is replaced by this tree's launcher exported as a test build of the same version (build_launcher.sh --test-build:
the same scripts, a throwaway key, test options on); the game starts it by itself, and the test options reach it through
OPENBFME_TEST_LAUNCHER_OPTIONS. GitHub is tools/release/launcher_test_server.py on 127.0.0.1.

Steps (RESULT <step>: PASS | FAIL; --shots: a picture of the game's window after each):
  offline          no server: the game reaches the main menu and its corner says why updates were skipped
  update           a newer release is there: the game reaches the main menu, the launcher installs the release in the background,
                   the corner says it starts next time; the unpacked folder is unchanged
  next start       the game is started from the same folder again: it closes by itself and the newer version's game is running
                   from the launcher's data folder, at the main menu
  no check         --no-update-check: the launcher is not started at all
Exit 0 when every step passed. Needs Godot 4.7.2 with its export templates, xdotool and ffmpeg for the pictures.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import os
import shutil
import signal
import subprocess
import sys
import tarfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import ed25519  # noqa: E402
import make_archive  # noqa: E402
import make_manifest  # noqa: E402
import seal_package  # noqa: E402
from launcher_test_server import COMMIT, TEST_REPO, ReleaseServer, tiny_package  # noqa: E402

GAME, LEXE = "OpenBFME.x86_64", "OpenBFMELauncher.x86_64"


def tree(folder: Path) -> dict[str, str]:
    return {str(p.relative_to(folder)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(folder.rglob("*")) if p.is_file()}


def release(work: Path, version: str, secret: bytes, folder: Path) -> dict[str, bytes]:
    """a release whose Linux package is `folder` (named openbfme-<version>-linux-x64), in the one-download layout"""
    d = work / f"release-{version}"
    d.mkdir()
    gl, gw = make_manifest.asset_name("game", version, "linux-x64"), make_manifest.asset_name("game", version, "windows-x64")
    ll, lw = make_manifest.asset_name("launcher", version, "linux-x64"), make_manifest.asset_name("launcher", version, "windows-x64")
    make_archive.tar_gz(folder, d / gl, 1700000000)
    launcher = (folder / LEXE).read_bytes()
    arch = {gl: (d / gl).read_bytes(), gw: tiny_package(gw, {"OpenBFME.exe": b"windows game", "OpenBFMELauncher.exe": b"windows launcher"}),
            ll: tiny_package(ll, {LEXE: launcher}), lw: tiny_package(lw, {"OpenBFMELauncher.exe": b"windows launcher"})}
    sums = "".join(f"{hashlib.sha256(b).hexdigest()}  {n}\n" for n, b in arch.items())
    for n, b in arch.items():
        (d / n).write_bytes(b)
    (d / f"SHA256SUMS-{version}.txt").write_text(sums)
    manifest = make_manifest.dumps(make_manifest.build(d, version, TEST_REPO, COMMIT, time.strftime("%Y-%m-%d", time.gmtime())))
    return arch | {"manifest.json": manifest, "manifest.json.sig": ed25519.sign(secret, manifest)}


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--package", type=Path, required=True)
    ap.add_argument("--work", type=Path, required=True)
    ap.add_argument("--shots", type=Path)
    ap.add_argument("--secret", type=Path, required=True,
                    help="the test release key's secret (32 bytes); the package was made with its public half (package.sh --release-key)")
    ap.add_argument("--godot", default=os.environ.get("GODOT", "godot"))
    ap.add_argument("--menu-seconds", type=int, default=420, help="how long a start may take to reach the main menu")
    a = ap.parse_args(argv)
    for var in ("ROTWK_INSTALL", "BFME2_INSTALL", "DISPLAY"):
        if not os.environ.get(var):
            print(f"SKIP the one-download play test: {var} is not set")
            return 77
    name = a.package.name.removesuffix(".tar.gz")
    version = name.removeprefix("openbfme-").removesuffix("-linux-x64")
    newer = version.rsplit(".", 1)[0] + "." + str(int(version.rsplit(".", 1)[1]) + 1)
    work = a.work.resolve()
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    with tarfile.open(a.package, "r:gz") as t:
        t.extractall(work / "unpacked", filter="data")
    home = work / "unpacked" / name
    assert (home / GAME).is_file() and (home / LEXE).is_file(), "not a combined package"
    secret = a.secret.read_bytes()
    (work / "key.pub").write_text(ed25519.public_key(secret).hex() + "\n")

    def test_launcher(v: str) -> bytes:
        r = subprocess.run([str(HERE / "build_launcher.sh"), "--out", str(work / "launchers"), "--version", v, "--commit", COMMIT, "--repo",
                            TEST_REPO, "--key-file", str(work / "key.pub"), "--mtime", "1700000000", "--platforms", "linux", "--worktree",
                            "--test-build", "--godot", a.godot], capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit(r.stdout[-2000:] + r.stderr[-2000:])
        with tarfile.open(work / "launchers" / f"openbfme-launcher-{v}-linux-x64.tar.gz", "r:gz") as t:
            return t.extractfile(f"openbfme-launcher-{v}-linux-x64/{LEXE}").read()

    (home / LEXE).write_bytes(test_launcher(version))
    (home / LEXE).chmod(0o755)
    seal_package.seal_folder(home, secret, TEST_REPO, version, "linux-x64")   # sealed again: the launcher of this test is in it
    # the newer release: the same game files under the next version, with that version's launcher
    nxt = work / "next" / f"openbfme-{newer}-linux-x64"
    shutil.copytree(home, nxt)
    (nxt / "VERSION").write_text((home / "VERSION").read_text().replace(f"OpenBFME {version}", f"OpenBFME {newer}", 1))
    (nxt / LEXE).write_bytes(test_launcher(newer))
    seal_package.seal_folder(nxt, secret, TEST_REPO, newer, "linux-x64")
    before = tree(home)

    srv = ReleaseServer()
    data = work / "data"
    env = dict(os.environ, XDG_DATA_HOME=str(data), XDG_CONFIG_HOME=str(work / "config"), HOME=str(work / "userhome"))
    (work / "userhome").mkdir()
    results: dict[str, bool] = {}
    started: list[subprocess.Popen] = []

    def game_log(out: Path) -> str:
        return out.read_text(errors="replace") if out.is_file() else ""

    def start(tag: str, origin: str, *args: str) -> tuple[subprocess.Popen, Path]:
        out = work / f"game-{tag}.out"
        e = dict(env, OPENBFME_TEST_LAUNCHER_OPTIONS=f"--api-base={origin} --repo={TEST_REPO}")
        p = subprocess.Popen([str(home / GAME), "--", *args], env=e, stdout=open(out, "w"), stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                             cwd=home)
        started.append(p)
        return p, out

    def wait_for(what, seconds: int) -> bool:
        t0 = time.time()
        while time.time() - t0 < seconds:
            if what():
                return True
            time.sleep(1)
        return False

    def stop(pid: int) -> None:
        """ends a game this test started (the test's own process ids only) and removes its "still running" marker: the next start
        is a player's normal one, not one after a crash"""
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        for _ in range(100):
            if not Path(f"/proc/{pid}").exists() or "Z" in Path(f"/proc/{pid}/stat").read_text().split(")")[-1].split()[:1]:
                break
            time.sleep(0.1)
        for marker in (data / "godot/app_userdata/OpenBFME/logs").glob("*.log.running"):
            if f'"pid":{pid},' in marker.read_text(errors="replace").replace(" ", ""):
                marker.unlink()

    def shot(pid: int, name: str) -> None:
        if not a.shots:
            return
        a.shots.mkdir(parents=True, exist_ok=True)
        wid = subprocess.run(["xdotool", "search", "--pid", str(pid)], capture_output=True, text=True).stdout.split()
        for w in wid:
            geo = subprocess.run(["xwininfo", "-id", w], capture_output=True, text=True).stdout
            if "Map State: IsViewable" in geo and "Width: 1" not in geo.split("Height")[0][-12:]:
                r = subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "x11grab", "-window_id", w, "-i", os.environ["DISPLAY"],
                                    "-frames:v", "1", str(a.shots / f"{name}.png")], capture_output=True, text=True)
                if r.returncode == 0:
                    print(f"SHOT {name}.png")
                    return
        print(f"SHOT {name}: no picture (no viewable window of pid {pid})")

    def step(name: str, checks: dict[str, bool], detail: str) -> None:
        ok = all(checks.values())
        results[name] = ok
        print(f"RESULT {name}: {'PASS' if ok else 'FAIL'}", flush=True)
        if not ok:
            print("  not true: " + "; ".join(k for k, v in checks.items() if not v), flush=True)
            print(detail[-3000:], flush=True)

    def update_lines(log: str) -> list[str]:
        return [ln for ln in log.splitlines() if ln.startswith("UPDATE ")]

    def games_from(folder: Path) -> list[int]:
        """the process ids of games whose executable is in `folder` (this test's own: everything under --work)"""
        out = []
        for p in Path("/proc").iterdir():
            if p.name.isdigit():
                try:
                    if os.readlink(p / "exe") == str(folder / GAME):
                        out.append(int(p.name))
                except OSError:
                    pass
        return out

    try:
        # 1. offline: nothing listens on the port
        p, out = start("offline", "http://127.0.0.1:9", "--res=1280x720")
        menu = wait_for(lambda: "GAME screen: MainMenu.apt" in game_log(out) or p.poll() is not None, a.menu_seconds)
        said = wait_for(lambda: any("Updates skipped: the release list could not be fetched" in ln for ln in update_lines(game_log(out))), 60)
        time.sleep(3)
        shot(p.pid, "game-offline-updates-skipped")
        log = game_log(out)
        step("offline", {"the main menu": menu and "GAME screen: MainMenu.apt" in log, "the corner says why": said,
                         "the game still runs": p.poll() is None, "the folder is unchanged": tree(home) == before,
                         "nothing installed": not (data / "OpenBFMELauncher/versions").exists()}, log)
        stop(p.pid)
        p.wait(timeout=60)

        # 2. a newer release: installed in the background while the game runs
        srv.publish(version, release(work, version, secret, home))
        srv.publish(newer, release(work, newer, secret, nxt))
        p, out = start("update", srv.origin, "--res=1280x720")
        menu = wait_for(lambda: "GAME screen: MainMenu.apt" in game_log(out) or p.poll() is not None, a.menu_seconds)
        done = wait_for(lambda: any(f"{newer} is installed. It starts the next time you start OpenBFME." in ln
                                    for ln in update_lines(game_log(out))), 300)
        time.sleep(3)
        shot(p.pid, "game-update-installed-for-next-start")
        log = game_log(out)
        # the launcher's own files (its update, the previous launcher, the signed proof of its update) are not the game's
        folder_now = {k: v for k, v in tree(home).items() if not k.startswith("update/") and not k.startswith(LEXE)
                      and not k.startswith("launcher-manifest.json")}
        step("update", {"the main menu": menu and "GAME screen: MainMenu.apt" in log, "the corner says installed for the next start": done,
                        "the game still runs": p.poll() is None,
                        "the newer version is installed beside the folder": (data / "OpenBFMELauncher/versions" / newer / "game" / GAME).is_file(),
                        "the folder's game is unchanged": folder_now == {k: v for k, v in before.items() if k != LEXE}}, log)
        stop(p.pid)
        p.wait(timeout=60)

        # 3. the next start from the same folder: the newer version runs instead
        target = data / "OpenBFMELauncher/versions" / newer / "game"
        p, out = start("next", srv.origin, "--res=1280x720")
        closed = wait_for(lambda: p.poll() is not None, 120)
        running = wait_for(lambda: bool(games_from(target)), 60)
        pids = games_from(target)
        newlog = lambda: "".join(f.read_text(errors="replace") for f in sorted((data / "godot/app_userdata/OpenBFME/logs").glob("*.log"))[-1:])  # noqa: E731
        menu = bool(pids) and wait_for(lambda: "GAME screen: MainMenu.apt" in newlog(), a.menu_seconds)
        if pids:
            time.sleep(3)
            shot(pids[0], "game-next-start-runs-the-new-version")
        log = game_log(out)
        step("next start", {"the folder's game closed by itself": closed, "with exit code 0": p.returncode == 0,
                            "the newer version's game runs": running, "it reached the main menu": menu,
                            "no game of the folder is left": games_from(home) == [],
                            "the folder's game said why": any(f"{newer} is installed: it starts instead of {version}: this game closes" in ln
                                                              for ln in update_lines(log)),
                            "the newer game asked no launcher": "the launcher is not asked for updates: started by the launcher" in newlog()},
             log[:3000] + "\n--- the newer game\n" + newlog()[-1500:])
        for pid in pids:
            stop(pid)
        wait_for(lambda: not games_from(target), 60)

        # 4. --no-update-check: no launcher is started (its log does not grow), the folder's own game runs
        llog = data / "OpenBFMELauncher/launcher.log"
        size = llog.stat().st_size
        p, out = start("nocheck", srv.origin, "--res=1280x720", "--no-update-check")
        said = wait_for(lambda: "UPDATE the launcher is not asked for updates: turned off (--no-update-check)" in game_log(out)
                        or p.poll() is not None, 120)
        menu = wait_for(lambda: "GAME screen: MainMenu.apt" in game_log(out) or p.poll() is not None, a.menu_seconds)
        step("no check", {"the game says it asks no launcher": said, "the main menu": menu and "GAME screen: MainMenu.apt" in game_log(out),
                          "the game still runs": p.poll() is None, "no launcher ran": llog.stat().st_size == size,
                          "no other version was started": games_from(target) == []}, game_log(out))
        stop(p.pid)
        p.wait(timeout=60)
    finally:
        srv.stop()
        for p in started:
            if p.poll() is None:
                p.kill()
        for pid in games_from(data / "OpenBFMELauncher/versions" / newer / "game"):
            stop(pid)
    print(f"RESULT the one download, played: {sum(results.values())}/{len(results)} steps passed")
    return 0 if all(results.values()) and len(results) == 4 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
