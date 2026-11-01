#!/usr/bin/env python3
"""Lane RELEASE-1: a crash of the packaged game is reported (review r1: the release export template wrote no native backtrace).

    ROTWK_INSTALL=<dir> BFME2_INSTALL=<dir> python3 tools/release/crash_test.py <exported OpenBFME.x86_64>

Starts the game headless in a fresh HOME / user data folder, waits for the main menu, sends SIGSEGV and checks:
  * the session log holds Godot's crash dump with its native (C++) backtrace,
  * neither the session log nor the console output contains the home folder or the user name (the console filter),
  * the crash marker stays behind, and the next start names the crashed run's log.
Lane NIGHTFIX-3 (the 2026-10-29 nightly: the crashed game wrote no dump and was killed after 60 s): Godot's crash handler is not
async-signal-safe; when the SIGSEGV interrupts the main thread inside malloc, the handler's first backtrace() (dlopen of libgcc_s ->
calloc) waits for the malloc lock that thread holds, forever (seen with gdb on a loaded host, the 10-28 package too). So the signal goes
when the main thread sleeps between frames (it then holds no lock), and a crashed game that still does not end within 60 s is shown
(every thread's state and stack, gdb when present; the end of its console and session log) and the crash runs once more in a fresh
folder: the result is the rerun's (said in the output). Any other failure is final.
Linux only. Prints RESULT lines; exit 0 when all hold.
"""
from __future__ import annotations

import ctypes
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

EXIT_SECONDS = int(os.environ.get("OPENBFME_CRASH_TEST_EXIT_SECONDS", "60"))  # tests of this script shorten the wait
PR_SET_PTRACER, PR_SET_PTRACER_ANY = 0x59616D61, 0xFFFFFFFFFFFFFFFF


def _allow_debugger() -> None:
    """In the game's process before exec: a debugger that is not its parent may attach (Yama ptrace_scope 1), for the hang's stacks."""
    try:
        ctypes.CDLL(None).prctl(PR_SET_PTRACER, ctypes.c_ulong(PR_SET_PTRACER_ANY), 0, 0, 0)
    except (OSError, AttributeError):
        pass


def wait_until_idle(pid: int, seconds: float = 10.0) -> bool:
    """Waits until the game's main thread sleeps between frames (nanosleep: it holds no malloc or other lock then). -> whether it did."""
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        try:
            if Path(f"/proc/{pid}/task/{pid}/wchan").read_text().strip() == "hrtimer_nanosleep":
                return True
        except OSError:
            return False
        time.sleep(0.01)
    print(f"CRASH the main thread did not sleep within {seconds:.0f} s: the SIGSEGV goes anyway")
    return False


def _tail(path: Path, lines: int) -> list[str]:
    try:
        return path.read_text(encoding="utf-8", errors="replace").splitlines()[-lines:]
    except OSError:
        return ["(not readable)"]


def hang_report(pid: int, console: Path, logs: Path, scrub) -> None:
    """What a crashed game that does not end is doing: its threads (name, kernel wait, state), their stacks, the end of its output."""
    print(f"CRASH HANG the game (pid {pid}) did not end within {EXIT_SECONDS} s of the SIGSEGV:")
    tasks = sorted(Path(f"/proc/{pid}/task").glob("*"), key=lambda t: int(t.name))
    for t in tasks:
        def read(name: str) -> str:
            try:
                return (t / name).read_text().strip()
            except OSError:
                return "?"
        stat = read("stat")
        state = stat.rsplit(")", 1)[1].split()[0] if ")" in stat else "?"
        print(f"CRASH HANG thread {t.name} {read('comm')!r} state {state} wchan {read('wchan')} syscall {read('syscall').split(' ')[0]}")
    if shutil.which("gdb"):
        try:
            out = subprocess.run(["gdb", "-p", str(pid), "-batch", "-ex", "set pagination off", "-ex", "thread apply all bt 30"],
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180).stdout.decode("utf-8", "replace")
        except subprocess.TimeoutExpired:
            out = "(gdb did not finish within 180 s)"
        for line in out.splitlines():
            if line.startswith(("Thread ", "#")):
                print("CRASH HANG gdb " + scrub(line))
    else:
        print("CRASH HANG no gdb on this host: no stacks")
    print("CRASH HANG the last console lines:")
    for line in _tail(console, 25):
        print("CRASH HANG | " + scrub(line))
    sessions = sorted(logs.glob("openbfme-*.log"))
    print(f"CRASH HANG the last session log lines ({sessions[-1].name if sessions else 'no session log'}):")
    for line in _tail(sessions[-1], 25) if sessions else []:
        print("CRASH HANG | " + scrub(line))


def run_once(exe: Path, user: str, real_home: str, scrub) -> tuple[bool, bool]:
    """One crash in a fresh HOME. -> (every check held, the crashed game hung)"""
    root = Path(tempfile.mkdtemp(prefix="openbfme-crash-", dir=os.environ.get("OPENBFME_TEST_TMP")))
    home = root / "home"
    home.mkdir()
    env = dict(os.environ, HOME=str(home), XDG_DATA_HOME=str(home / ".local/share"), XDG_CACHE_HOME=str(home / ".cache"),
               XDG_CONFIG_HOME=str(home / ".config"))
    logs = home / ".local/share/godot/app_userdata/OpenBFME/logs"
    ok = True
    hung = False

    def result(name: str, good: bool, why: str = "") -> None:
        nonlocal ok
        ok = ok and good
        print(f"RESULT crash {name}: {'PASS' if good else 'FAIL'} {why}")

    try:
        console = root / "console.txt"
        with console.open("wb") as out:
            p = subprocess.Popen([str(exe), "--headless"], cwd=exe.parent, env=env, stdout=out, stderr=subprocess.STDOUT,
                                 preexec_fn=_allow_debugger)
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
                wait_until_idle(p.pid)
                p.send_signal(signal.SIGSEGV)
            try:
                code = p.wait(timeout=EXIT_SECONDS)
            except subprocess.TimeoutExpired:
                hung = started
                if hung:
                    hang_report(p.pid, console, logs, scrub)
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
    return ok, hung


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

    def scrub(line: str) -> str:  # the hang report goes into CI logs: no home folder or user name of the host
        if len(real_home) > 1:
            line = line.replace(real_home, "~")
        return line.replace(user, "<user>") if len(user) >= 3 else line

    ok, hung = run_once(exe, user, real_home, scrub)
    if not ok and hung:
        print("CRASH RERUN the crashed game hung (report above): the crash runs once more in a fresh folder")
        ok, hung = run_once(exe, user, real_home, scrub)
        print(f"CRASH RERUN {'PASS: the first crash hung, the rerun passed' if ok else 'FAIL'}")
    print(f"CRASH TEST {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
