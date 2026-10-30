"""Lane NIGHTFIX-3: tools/release/crash_test.py on a stand-in game.

The 2026-10-29 nightly's crash test failed once: the game reached the main menu, took the SIGSEGV and neither wrote its crash dump nor
ended within 60 s (Godot's crash handler waited for the malloc lock the interrupted main thread held). crash_test.py now sends the signal
while the main thread sleeps, reports a hang that still happens (threads, stacks, the end of the output) and runs the crash once more;
any other failure stays final. The stand-in game writes a session log and marker like the real one, sleeps as the game does between
frames and, per FAKE_HANG, ignores the SIGSEGV (a hang) always, the first time only, or never.
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

FAKE = r'''#!/usr/bin/env python3
import os, signal, sys, time
from pathlib import Path
logs = Path(os.environ["XDG_DATA_HOME"]) / "godot/app_userdata/OpenBFME/logs"
logs.mkdir(parents=True, exist_ok=True)
if sys.argv[2:3] == ["--quit"]:
    for m in logs.glob("*.log.running"):
        print("RELEASE the previous run did not end normally: " + m.name[:-len(".running")])
    sys.exit(0)
log = logs / "openbfme-20261029-000000.log"
log.write_text("OpenBFME stand-in\n")
(logs / (log.name + ".running")).write_text('{"pid": 1}')
runs = Path(os.environ["FAKE_STATE"]) / "runs"
n = int(runs.read_text()) + 1 if runs.exists() else 1
runs.write_text(str(n))
hang = os.environ["FAKE_HANG"] == "always" or (os.environ["FAKE_HANG"] == "once" and n == 1)
def crashed(*_):
    if hang:
        return
    with log.open("a") as f:
        f.write("handle_crash: Program crashed with signal 11\n[1] frame\n-- END OF C++ BACKTRACE --\n")
    os._exit(134)
signal.signal(signal.SIGSEGV, crashed)
print("GAME screen: MainMenu.apt", flush=True)
time.sleep(30)  # the main thread sleeps (hrtimer_nanosleep) as the game's does between frames
'''


def run(tmp: Path, hang: str) -> subprocess.CompletedProcess:
    exe = tmp / "pkg" / "OpenBFME.x86_64"
    exe.parent.mkdir()
    exe.write_text(FAKE)
    exe.chmod(0o755)
    state = tmp / "state"
    state.mkdir()
    env = dict(os.environ, ROTWK_INSTALL=str(tmp), BFME2_INSTALL=str(tmp), FAKE_HANG=hang, FAKE_STATE=str(state),
               OPENBFME_CRASH_TEST_EXIT_SECONDS="3", OPENBFME_TEST_TMP=str(tmp))
    return subprocess.run([sys.executable, str(HERE / "crash_test.py"), str(exe)], env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, timeout=300)


def test_a_crash_that_ends_passes_without_a_rerun(tmp_path: Path) -> None:
    r = run(tmp_path, "never")
    assert r.returncode == 0, r.stdout
    assert "CRASH TEST PASS" in r.stdout
    assert "CRASH HANG" not in r.stdout and "CRASH RERUN" not in r.stdout
    assert "did not sleep" not in r.stdout  # the signal went while the main thread slept


def test_a_hang_is_reported_and_the_rerun_decides(tmp_path: Path) -> None:
    r = run(tmp_path, "once")
    assert r.returncode == 0, r.stdout
    assert "CRASH HANG the game (pid " in r.stdout
    assert "CRASH HANG thread " in r.stdout
    assert "CRASH HANG | GAME screen: MainMenu.apt" in r.stdout
    assert "CRASH RERUN PASS: the first crash hung, the rerun passed" in r.stdout
    assert r.stdout.rstrip().endswith("CRASH TEST PASS")


def test_a_hang_twice_fails(tmp_path: Path) -> None:
    r = run(tmp_path, "always")
    assert r.returncode == 1, r.stdout
    assert r.stdout.count("CRASH HANG the game (pid ") == 2
    assert "CRASH RERUN FAIL" in r.stdout
    assert r.stdout.rstrip().endswith("CRASH TEST FAIL")
