#!/usr/bin/env python3
"""Lane RELEASE-1: a crash of the packaged game is reported (review r1: the release export template wrote no native backtrace).

    ROTWK_INSTALL=<dir> BFME2_INSTALL=<dir> python3 tools/release/crash_test.py <exported OpenBFME.x86_64>

Starts the game headless in a fresh HOME / user data folder, waits for the main menu, sends SIGSEGV and checks:
  * the session log holds Godot's crash dump with its native (C++) backtrace,
  * neither the session log nor the console output contains the home folder or the user name (the console filter),
  * the crash marker stays behind, and the next start names the crashed run's log.
Linux only. Prints RESULT lines; exit 0 when all hold.
"""
from __future__ import annotations

import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    exe = Path(sys.argv[1]).resolve()
    if not os.environ.get("ROTWK_INSTALL") or not os.environ.get("BFME2_INSTALL"):
        print("SKIP crash test: ROTWK_INSTALL and BFME2_INSTALL are needed")
        return 77
    user = os.environ.get("USER", "")
    real_home = os.environ.get("HOME", "")
    root = Path(tempfile.mkdtemp(prefix="openbfme-crash-", dir=os.environ.get("OPENBFME_TEST_TMP")))
    home = root / "home"
    home.mkdir()
    env = dict(os.environ, HOME=str(home), XDG_DATA_HOME=str(home / ".local/share"), XDG_CACHE_HOME=str(home / ".cache"),
               XDG_CONFIG_HOME=str(home / ".config"))
    logs = home / ".local/share/godot/app_userdata/OpenBFME/logs"
    ok = True

    def result(name: str, good: bool, why: str = "") -> None:
        nonlocal ok
        ok = ok and good
        print(f"RESULT crash {name}: {'PASS' if good else 'FAIL'} {why}")

    try:
        console = root / "console.txt"
        with console.open("wb") as out:
            p = subprocess.Popen([str(exe), "--headless"], cwd=exe.parent, env=env, stdout=out, stderr=subprocess.STDOUT)
            started = False
            for _ in range(1200):
                if p.poll() is not None:
                    break
                if b"GAME screen: MainMenu.apt" in console.read_bytes():
                    started = True
                    break
                time.sleep(0.1)
            if started:
                time.sleep(0.5)
                p.send_signal(signal.SIGSEGV)
            try:
                code = p.wait(timeout=60)
            except subprocess.TimeoutExpired:
                p.kill()
                code = p.wait()
        text = console.read_bytes().decode("utf-8", "replace")
        result("reached the main menu", started, f"exit {code}")
        sessions = sorted(logs.glob("openbfme-*.log"))
        log = sessions[-1].read_text(encoding="utf-8", errors="replace") if sessions else ""
        result("native backtrace in the session log", "Program crashed with signal 11" in log and "C++ BACKTRACE" in log,
               "" if log else "(no session log)")
        for sink, content in (("session log", log), ("console", text)):
            leaks = [w for w in (str(home), real_home if len(real_home) > 1 else None) if w and w + "/" in content]
            if len(user) >= 3:
                leaks += [f"user name '{user}'" for sep in "/\\=" if f"{sep}{user}" in content][:1]
            result(f"no home folder or user name in the {sink}", not leaks, ", ".join(leaks))
        result("crash marker left behind", bool(list(logs.glob("*.log.running"))))
        nxt = subprocess.run([str(exe), "--headless", "--quit"], cwd=exe.parent, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             timeout=300).stdout.decode("utf-8", "replace")
        named = sessions[-1].name if sessions else "?"
        result("the next start names the crashed log", "previous run did not end normally" in nxt and named in nxt)
    finally:
        shutil.rmtree(root, ignore_errors=True)
    print(f"CRASH TEST {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
