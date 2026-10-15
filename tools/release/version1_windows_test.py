#!/usr/bin/env python3
"""Lane VERSION-1: the Windows launchers already published update to the plain releases (v0.4.5, "OpenBFME Alpha 0.4.5") on their
default channel, under Wine (the Windows twin of test_version1_published.py).

    WINEPREFIX=<own prefix> DISPLAY=:1 python3 tools/release/version1_windows_test.py --work DIR --wine <wine binary> \\
        --fake-game GAME.exe --published DIR [--tags v0.4.0-alpha.4,...]

--published: <DIR>/<tag>/openbfme-launcher-<tag>-windows-x64.zip as testers downloaded them (gh release download); each executable is
patched only in its build information (onezip_windows_test.patch_published: the test key, test_build). --fake-game:
tools/release/launcher_fake_game.c built for Windows. The v0.4.5 launcher is this tree's, exported here as a Windows test build.

Per published launcher, started with NO channel option and an empty data folder (RESULT <tag>: <step>: PASS | FAIL):
  install + play   on today's page (the previews and alpha builds as pre-releases) plus v0.4.5 as a full release, it installs v0.4.5
                   (the combined package) and plays it
  staged           it stages the v0.4.5 launcher
  swap             its next start (with a window) swaps itself for the v0.4.5 launcher, which confirms its start
  next release     the v0.4.5 launcher installs v0.4.6 (format 3, one package) and says it is on the stable channel
Exit 0 when every step passed. Under Wine the launcher's data folder is the prefix's Roaming\\OpenBFMELauncher, DELETED before each
launcher (use a test prefix).
"""
from __future__ import annotations

import argparse
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
import onezip_windows_test as oz  # noqa: E402  (patch_published, combined_zip, release: ONEZIP-1's Windows release stand-ins)
from launcher_test_server import COMMIT, TEST_REPO, ReleaseServer  # noqa: E402

EXE = oz.EXE
PUBLISHED = ["v0.3.0-preview.1", "v0.3.0-preview.2", "v0.3.0-preview.3", "v0.3.0-preview.4", "v0.3.0-preview.6", "v0.4.0-alpha.1",
             "v0.4.0-alpha.2", "v0.4.0-alpha.3", "v0.4.0-alpha.4"]
FIRST, NEXT = "v0.4.5", "v0.4.6"


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--work", type=Path, required=True)
    ap.add_argument("--wine", required=True)
    ap.add_argument("--fake-game", type=Path, required=True)
    ap.add_argument("--published", type=Path, required=True)
    ap.add_argument("--tags", default=",".join(PUBLISHED))
    ap.add_argument("--godot", default=os.environ.get("GODOT", "godot"))
    a = ap.parse_args(argv)
    tags = [t for t in a.tags.split(",") if t]
    for t in tags:
        if t not in PUBLISHED:
            ap.error(f"{t} is not a published launcher ({', '.join(PUBLISHED)})")
        if not (a.published / t / f"openbfme-launcher-{t}-windows-x64.zip").is_file():
            print(f"SKIP: openbfme-launcher-{t}-windows-x64.zip is not in {a.published}/{t} (gh release download)")
            return 1
    if not tags:
        ap.error("no launcher to test")
    work = a.work.resolve()
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    secret = os.urandom(32)
    oz.SECRET[0] = secret
    builds = work / "launchers"
    builds.mkdir()
    (builds / "key.pub").write_text(ed25519.public_key(secret).hex() + "\n")
    r = subprocess.run([str(HERE / "build_launcher.sh"), "--out", str(builds), "--version", FIRST, "--commit", COMMIT, "--repo", TEST_REPO,
                        "--key-file", str(builds / "key.pub"), "--mtime", "1700000000", "--platforms", "windows", "--worktree",
                        "--test-build", "--godot", a.godot], capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-2000:], r.stderr[-2000:])
        return 1
    new_zip = (builds / f"openbfme-launcher-{FIRST}-windows-x64.zip").read_bytes()
    new_exe = zipfile.ZipFile(io.BytesIO(new_zip)).read(f"openbfme-launcher-{FIRST}-windows-x64/{EXE}")

    roaming = [p for p in sorted(Path(os.environ["WINEPREFIX"]).glob("drive_c/users/*/AppData/Roaming")) if p.parent.parent.name != "Public"]
    if len(roaming) != 1:
        print(f"cannot tell the prefix's user folder: {roaming}")
        return 1
    data = roaming[0] / "OpenBFMELauncher"
    env = dict(os.environ, WINEDEBUG=os.environ.get("WINEDEBUG", "-all"))
    played = work / "played.txt"
    env["OPENBFME_TEST_PLAYED"] = "Z:" + str(played).replace("/", "\\")
    srv = ReleaseServer()
    results: dict[str, bool] = {}

    def run(inst: Path, *args: str) -> str:
        """one start of inst's launcher, with its window (a staged launcher update is applied only at a start with a window); the log
        lines it, an update helper and the new launcher appended"""
        log = data / "launcher.log"
        before = log.stat().st_size if log.is_file() else 0
        with open(work / "out.txt", "w") as out:  # a file, not a pipe: Wine's services would hold a pipe open
            subprocess.run([a.wine, str(inst / EXE), "--", f"--api-base={srv.origin}", f"--repo={TEST_REPO}", *args], env=env, stdout=out,
                           stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=900, cwd=inst)
        t0, last, size = time.time(), time.time(), -1
        while time.time() - t0 < 240:   # the update helper and the new launcher run on after the old one exits
            j = data / "launcher-update.json"
            text = j.read_text(errors="replace") if j.is_file() else ""
            busy = j.is_file() and '"confirmed"' not in text and '"rolled-back"' not in text
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

    def wait_played(text: str) -> bool:
        for _ in range(150):
            if played.is_file() and text.lower() in played.read_text(errors="replace").replace("/", "\\").lower():
                return True
            time.sleep(0.1)
        return False

    try:
        for old in tags:
            shutil.rmtree(data, ignore_errors=True)
            played.unlink(missing_ok=True)
            srv.releases.clear()
            srv.files.clear()
            srv.requests.clear()
            for tag in PUBLISHED:   # today's page: pre-releases, no assets (older than what is offered: never downloaded)
                srv.releases.insert(0, {"tag_name": tag, "draft": False, "prerelease": True, "body": f"Notes of {tag}",
                                        "published_at": "2026-10-13T00:00:00Z", "assets": []})
            inst_root = work / f"inst-{old}"
            with zipfile.ZipFile(a.published / old / f"openbfme-launcher-{old}-windows-x64.zip") as z:
                z.extractall(inst_root)
            inst = inst_root / f"openbfme-launcher-{old}-windows-x64"
            (inst / EXE).write_bytes(oz.patch_published((inst / EXE).read_bytes(), secret))
            (inst / EXE).chmod(0o755)
            srv.publish(FIRST, oz.release(work, FIRST, secret, oz.combined_zip(work, FIRST, a.fake_game, new_exe), new_zip, new_exe))
            assert srv.releases[0]["tag_name"] == FIRST and srv.releases[0]["prerelease"] is False
            log = run(inst, "--update", "--self-update", "--play")
            game_dir = data / "versions" / FIRST / "game"
            step(f"{old}: install + play", f"update installed {FIRST}" in log and wait_played(f"versions\\{FIRST}\\game\\OpenBFME.exe")
                 and (game_dir / EXE).is_file() and (game_dir / EXE).read_bytes() == new_exe, log)
            step(f"{old}: staged", f"launcher update {FIRST} staged" in log and (inst / "update" / EXE).is_file()
                 and (inst / "update" / EXE).read_bytes() == new_exe, log)
            log = run(inst, "--version")
            step(f"{old}: swap", f"{FIRST} started after the update from {old}" in log and f"the new launcher {FIRST} confirmed its start" in log
                 and (inst / EXE).read_bytes() == new_exe, log)
            srv.publish(NEXT, oz.release(work, NEXT, secret, oz.combined_zip(work, NEXT, a.fake_game, b"MZ the launcher of " + NEXT.encode()),
                                         None, b"MZ the launcher of " + NEXT.encode(), fmt=3))
            log = run(inst, "--update")
            log2 = run(inst, "--update")
            step(f"{old}: next release", f"OpenBFME Launcher {FIRST}" in log and f"update installed {NEXT}" in log
                 and f"up to date: {NEXT} installed, newest stable release {NEXT}" in log2, log + log2)
    finally:
        srv.stop()
    failed = [k for k, ok in results.items() if not ok]
    print(f"RESULT {'ALL PASS' if not failed else 'FAIL: ' + ', '.join(failed)} ({len(results)} steps, {len(tags)} launchers)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
