#!/usr/bin/env python3
"""Lane SELPERF-1: the selection benchmark (play1_player.gd selperf) in the real Windows build on a Windows PC's GPU, run from the Deck over ssh.
    python3 tools/perf/selperf_win.py setup <label>=<WSL dir of an exported game> [...]
    python3 tools/perf/selperf_win.py run <label> <log file> [game arguments after --]
    python3 tools/perf/selperf_win.py wait <label> <log file> [seconds]   (a run whose poller ended early: wait for it, delete its task, fetch its log)
    python3 tools/perf/selperf_win.py cleanup
jpc-winbuild.sh <worktree> exports the game on the PC's WSL; setup copies each export to %TEMP%\\openbfme-selperf\\<label>. run starts the game
there through a one-shot scheduled task (/IT: the logged-on user's desktop session, where the GPU renders; the owner allows GPU work on the
PC this way), at below-normal priority, with a private APPDATA / LOCALAPPDATA under the temp folder (never the owner's own settings) and the
retail games at F:\\RotWK / F:\\BFME2 (WIN_PRIORITY=normal for a measurement on the idle machine); the task is deleted when the game ends and its log comes back. cleanup removes the temp folder.
ssh always gets stdin from /dev/null (the Windows ssh eats it); the PC's ssh shell is PowerShell, so cmd lines go there as a .cmd file.
"""
import os
import re
import subprocess
import sys
import time

HOST = os.environ.get("WIN_HOST", "jonathan-windows")
WSL = os.environ.get("WIN_WSL", "wsl -d Ubuntu-24.04 -e")
GAMES = {"ROTWK_INSTALL": r"F:\RotWK", "BFME2_INSTALL": r"F:\BFME2"}
EXE = "OpenBFME.console.exe"  # the console wrapper: the log goes to the redirected stdout
# the game's priority: below normal for a render beside the owner's work; a frame-time measurement runs at normal priority on an idle machine
# (briefs/jpc.md: the desktop lock, jpc-paused, no running jpc job: tools/perf/selperf_win_idle.sh)
PRIORITY = os.environ.get("WIN_PRIORITY", "belownormal")


def _run(args, check=True):
    for attempt in range(3):  # a dropped ssh / scp connection (255) is tried again
        r = subprocess.run(args, stdin=subprocess.DEVNULL, capture_output=True, text=True)
        if r.returncode != 255:
            break
        time.sleep(10)
    if check and r.returncode != 0:
        raise SystemExit(f"failed ({r.returncode}): {' '.join(args)}\n{r.stdout[-800:]}\n{r.stderr[-800:]}")
    return r


def temp_dir():
    t = os.environ.get("WIN_TEMP") or _run(["ssh", "-o", "ConnectTimeout=20", HOST, "cmd /c echo %TEMP%"]).stdout.strip()
    if not re.match(r"^[A-Za-z]:\\\S+$", t):
        raise SystemExit(f"cannot read the PC's %TEMP% ({t!r}); set WIN_TEMP")
    return t + "\\openbfme-selperf"


def wsl_path(p):
    return "/mnt/" + p[0].lower() + p[2:].replace("\\", "/")


def scp_to(local, remote):
    _run(["scp", "-q", local, f"{HOST}:{remote.replace(chr(92), '/')}"])


def scp_from(remote, local):
    _run(["scp", "-q", f"{HOST}:{remote.replace(chr(92), '/')}", local])


def cmd(T, line, check=True):
    local = "/tmp/selperf_win_ssh.cmd"
    with open(local, "w", newline="\r\n") as f:
        f.write("@echo off\n" + line + "\n")
    scp_to(local, T + "\\_ssh.cmd")
    return _run(["ssh", "-o", "ConnectTimeout=20", HOST, f"cmd /d /c {T}\\_ssh.cmd"], check=check)


def setup(T, pairs):
    w = wsl_path(T)
    copies = " && ".join(f"cp -r {src}/. {w}/{label}/" for label, src in pairs)
    mk = " ".join(f"{w}/{label}" for label, _ in pairs)
    _run(["ssh", "-o", "ConnectTimeout=20", HOST, f'{WSL} bash -c "rm -rf {w} && mkdir -p {mk} {w}/appdata {w}/localappdata {w}/out && {copies} && ls {w}/*"'])
    print("set up", ", ".join(label for label, _ in pairs), "in", T)


def finish(T, label, log, limit):
    """wait up to `limit` seconds for the run's done file; then the task is deleted and the log fetched (False: still running)"""
    out = f"{T}\\out\\{label}"
    t0 = time.time()
    while "yes" not in cmd(T, f'if exist "{out}\\done.txt" echo yes', check=False).stdout:
        if time.time() - t0 > limit:
            print(f"{label}: still running after {limit:.0f} s (the task stays; wait again)")
            return False
        time.sleep(15)
    cmd(T, f"schtasks /Delete /TN openbfme-selperf-{label} /F", check=False)
    scp_from(f"{out}\\log.txt", log)
    print(f"{label}: done, log in {log}")
    return True


def run(T, label, log, args):
    out = f"{T}\\out\\{label}"
    cmd(T, f'if exist "{out}" rmdir /s /q "{out}"\nmkdir "{out}"', check=False)
    line = f'"{T}\\{label}\\{EXE}" -- ' + " ".join(f'"{a}"' for a in args)
    bat = ["@echo off"] + [f"set {k}={v}" for k, v in GAMES.items()] + [
        f"set APPDATA={T}\\appdata", f"set LOCALAPPDATA={T}\\localappdata", f'cd /d "{T}\\{label}"',
        f'start "openbfme-selperf {label}" /{PRIORITY} /wait cmd /c "{line} > "{out}\\log.txt" 2>&1"', f'echo done> "{out}\\done.txt"']
    local = f"/tmp/selperf_win_{label}.bat"
    with open(local, "w", newline="") as f:
        f.write("\r\n".join(bat) + "\r\n")
    scp_to(local, f"{T}\\run_{label}.bat")
    task = f"openbfme-selperf-{label}"
    cmd(T, f'schtasks /Create /TN {task} /TR "{T}\\run_{label}.bat" /SC ONCE /ST 23:59 /IT /RL LIMITED /F')
    t0 = time.time()
    try:
        cmd(T, f"schtasks /Run /TN {task}")
        while "yes" not in cmd(T, f'if exist "{out}\\done.txt" echo yes', check=False).stdout:
            if time.time() - t0 > float(os.environ.get("WIN_TIMEOUT", "3600")):
                print(f"{label}: no end after the timeout")
                break
            time.sleep(15)
    finally:
        cmd(T, f"schtasks /Delete /TN {task} /F", check=False)
    scp_from(f"{out}\\log.txt", log)
    print(f"{label}: {time.time() - t0:.0f} s, log in {log}")


def cleanup(T):
    _run(["ssh", "-o", "ConnectTimeout=20", HOST, f'{WSL} bash -c "rm -rf {wsl_path(T)}"'])
    print("removed", T)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    T = temp_dir()
    if sys.argv[1] == "setup":
        setup(T, [a.split("=", 1) for a in sys.argv[2:]])
    elif sys.argv[1] == "run":
        run(T, sys.argv[2], sys.argv[3], sys.argv[4:])
    elif sys.argv[1] == "wait":
        sys.exit(0 if finish(T, sys.argv[2], sys.argv[3], float(sys.argv[4]) if len(sys.argv) > 4 else 540.0) else 3)
    elif sys.argv[1] == "cleanup":
        cleanup(T)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
