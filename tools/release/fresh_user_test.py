#!/usr/bin/env python3
"""Lane RELEASE-1: the fresh-user test. The game as a stranger meets it: an empty user data folder, no ROTWK_INSTALL / BFME2_INSTALL.

    ROTWK_INSTALL=<dir> BFME2_INSTALL=<dir> python3 tools/release/fresh_user_test.py [--exe <exported OpenBFME.x86_64> | --godot <godot>]

The real installs are only used to build the cases; the game itself never sees the variables. Every case runs headless in its own HOME and
XDG folders (Godot's user data is $XDG_DATA_HOME/godot/app_userdata/OpenBFME):
  none       nothing to find: the game stops with "no game folders chosen yet", nothing is remembered
  found      a Wine prefix (~/.wine: system.reg with RotWK's GameRegPath InstallPath and BFME2's App Paths entry, Z: = /) is found, the
             discovered folders are accepted (--first-run=accept), mounted, remembered, and a skirmish starts
  remembered the same user data again without any hook: the remembered folders are used ("RELEASE game folders: config"), the game starts
  picked     no prefix; the player picks the two folders (--first-run-rotwk / --first-run-bfme2): mounted, remembered, the game starts
  swapped    the two folders picked the wrong way round: rejected with "this folder holds ...", nothing remembered
  empty      an empty folder picked for RotWK: rejected naming the expected archives, nothing remembered
and for every case: one session log in user://logs starting with the version line, with neither the test HOME nor the user name in it
(found / picked: nor in the console output, review r1).
Linux only (the Windows equivalent needs APPDATA / registry fakes). Prints a RESULT line per case; exit 0 when all pass.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
GAME_ARGS = ["--auto", "--check", "--advance=1", "--seed=7"]


def win_path(p: str) -> str:
    """A host path as Wine sees it through Z: (the default dosdevices/z: -> /)."""
    return "Z:" + p.replace("/", "\\")


def reg_escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


def make_wine_prefix(home: Path, rotwk: str, bfme2: str) -> None:
    prefix = home / ".wine"
    (prefix / "drive_c").mkdir(parents=True)
    (prefix / "dosdevices").mkdir()
    os.symlink("/", prefix / "dosdevices" / "z:")
    os.symlink("../drive_c", prefix / "dosdevices" / "c:")
    (prefix / "system.reg").write_text(
        "WINE REGISTRY Version 2\n;; All keys relative to \\\\Machine\n\n#arch=win64\n\n"
        "[Software\\\\Wow6432Node\\\\Electronic Arts\\\\Electronic Arts\\\\The Lord of the Rings, The Rise of the Witch-king] 1700000000\n"
        f"\"InstallPath\"=\"{reg_escape(win_path(rotwk))}\\\\\"\n\n"
        "[Software\\\\Wow6432Node\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\App Paths\\\\lotrbfme2.exe] 1700000000\n"
        f"@=\"{reg_escape(win_path(bfme2))}\\\\lotrbfme2.exe\"\n"
        f"\"Path\"=\"{reg_escape(win_path(bfme2))}\"\n", encoding="utf-8")
    (prefix / "user.reg").write_text("WINE REGISTRY Version 2\n", encoding="utf-8")


class Case:
    def __init__(self, root: Path, name: str, home: Path | None = None):
        self.name = name
        self.home = home or (root / name / "home")
        self.home.mkdir(parents=True, exist_ok=True)

    def env(self) -> dict:
        e = {k: v for k, v in os.environ.items() if k not in ("ROTWK_INSTALL", "BFME2_INSTALL", "RW_GAME_DAT", "WINEPREFIX")}
        e["HOME"] = str(self.home)
        e["XDG_DATA_HOME"] = str(self.home / ".local/share")
        e["XDG_CONFIG_HOME"] = str(self.home / ".config")
        e["XDG_CACHE_HOME"] = str(self.home / ".cache")
        return e

    def userdata(self) -> Path:
        return self.home / ".local/share/godot/app_userdata/OpenBFME"


def run(cmd: list[str], case: Case, extra: list[str], timeout: int = 900) -> tuple[int, str]:
    p = subprocess.run(cmd + ["--"] + extra, env=case.env(), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    return p.returncode, p.stdout.decode("utf-8", "replace")


def check_log(case: Case, user: str) -> list[str]:
    problems = []
    logs = sorted((case.userdata() / "logs").glob("openbfme-*.log"))
    if not logs:
        return ["no session log in user://logs"]
    text = logs[-1].read_text(encoding="utf-8", errors="replace")
    if not text.startswith("OpenBFME "):
        problems.append("the session log does not start with the version line: " + text[:80])
    if str(case.home) in text:
        problems.append("the session log contains the home folder")
    if len(user) >= 2 and (f"/{user}/" in text or f"\\{user}\\" in text):
        problems.append(f"the session log contains the user name '{user}' as a path component")
    if list((case.userdata() / "logs").glob("*.running")):
        problems.append("a session marker was left behind after a normal exit")
    return problems


def check_console(out: str, case: Case, user: str) -> list[str]:
    """The console is filtered like the log (Common/ConsoleFilter.h): no home folder, no user name as a path component or value."""
    problems = []
    if str(case.home) + "/" in out:
        problems.append("the console shows the home folder")
    if len(user) >= 3 and any(f"{sep}{user}{end}" in out for sep in "/\\=" for end in "/\\;\n "):
        problems.append(f"the console shows the user name '{user}'")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", help="an exported build (OpenBFME.x86_64)")
    ap.add_argument("--godot", default=os.environ.get("GODOT", "godot"), help="Godot, run on the source project godot/ (default)")
    ap.add_argument("--keep", action="store_true", help="keep the temporary homes")
    a = ap.parse_args()
    rotwk, bfme2 = os.environ.get("ROTWK_INSTALL", ""), os.environ.get("BFME2_INSTALL", "")
    if not rotwk or not bfme2:
        print("SKIP fresh-user test: ROTWK_INSTALL and BFME2_INSTALL are needed to build the cases")
        return 77
    rotwk, bfme2 = str(Path(rotwk).resolve()), str(Path(bfme2).resolve())
    cmd = [a.exe, "--headless"] if a.exe else [a.godot, "--headless", "--path", str(REPO / "godot")]
    user = os.environ.get("USER", "")
    root = Path(tempfile.mkdtemp(prefix="openbfme-fresh-user-", dir=os.environ.get("OPENBFME_TEST_TMP")))
    results = []

    def record(name: str, ok: bool, why: str, out: str) -> None:
        results.append(ok)
        print(f"RESULT {name}: {'PASS' if ok else 'FAIL'} {why}")
        if not ok:
            print("\n".join("    " + l for l in out.splitlines()[-25:]))

    def remembered(case: Case) -> str:
        cfg = case.userdata() / "install-paths.cfg"
        return cfg.read_text(encoding="utf-8") if cfg.exists() else ""

    try:
        c = Case(root, "none")
        code, out = run(cmd, c, GAME_ARGS)
        ok = code != 0 and "no game folders chosen yet" in out and not remembered(c)
        problems = check_log(c, user)
        record("none", ok and not problems, f"exit {code}; {'; '.join(problems)}", out)

        c = Case(root, "found")
        make_wine_prefix(c.home, rotwk, bfme2)
        code, out = run(cmd, c, ["--first-run=accept"] + GAME_ARGS)
        cfg = remembered(c)
        ok = (code == 0 and "FIRST RUN found rotwk" in out and "FIRST RUN found bfme2" in out and "FIRST RUN remembered: true" in out
              and f"ROTWK_INSTALL={rotwk}" in cfg and f"BFME2_INSTALL={bfme2}" in cfg)
        problems = check_log(c, user) + check_console(out, c, user)
        record("found", ok and not problems, f"exit {code}; {'; '.join(problems)}", out)

        c2 = Case(root, "remembered", home=c.home)
        code, out = run(cmd, c2, GAME_ARGS)
        ok = code == 0 and "RELEASE game folders: config" in out and "FIRST RUN" not in out
        record("remembered", ok, f"exit {code}", out)

        c = Case(root, "picked")
        code, out = run(cmd, c, [f"--first-run-rotwk={rotwk}", f"--first-run-bfme2={bfme2}"] + GAME_ARGS)
        cfg = remembered(c)
        ok = code == 0 and "FIRST RUN check: ok true" in out and f"ROTWK_INSTALL={rotwk}" in cfg
        problems = check_log(c, user) + check_console(out, c, user)
        record("picked", ok and not problems, f"exit {code}; {'; '.join(problems)}", out)

        c = Case(root, "swapped")
        code, out = run(cmd, c, [f"--first-run-rotwk={bfme2}", f"--first-run-bfme2={rotwk}"] + GAME_ARGS)
        ok = (code != 0 and "FIRST RUN rejected: this folder holds The Battle for Middle-earth II, not The Rise of the Witch-king" in out
              and not remembered(c))
        record("swapped", ok, f"exit {code}", out)

        c = Case(root, "empty")
        empty = root / "empty" / "not-a-game"
        empty.mkdir(parents=True)
        code, out = run(cmd, c, [f"--first-run-rotwk={empty}", f"--first-run-bfme2={bfme2}"] + GAME_ARGS)
        ok = code != 0 and "FIRST RUN rejected: no The Rise of the Witch-king 2.01 archives in" in out and not remembered(c)
        record("empty", ok, f"exit {code}", out)
    finally:
        if not a.keep:
            shutil.rmtree(root, ignore_errors=True)
        else:
            print("kept", root)
    print(f"FRESH USER {'PASS' if all(results) else 'FAIL'} ({sum(results)}/{len(results)})")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
