"""Lane UI-3: screenshots of every launcher screen, for the README / devlog and the layout review.

    DISPLAY=:1 python3 tools/release/launcher_screens.py --out workspace/rebuild/ui3

Runs the launcher project (a development run on the Godot 4.7.2 editor binary, $GODOT or `godot`) once per screen and size with
--ui-demo=<screen> (made-up state, no network) and --screenshot-size, under a neutral home folder (/tmp/openbfme-demo by default), so no
real user name or home folder can appear in a shot. Needs a display (Godot's headless mode renders nothing). Writes
launcher-<screen>-<W>x<H>.png and prints one line per file.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
LAUNCHER = REPO / "launcher"
SCREENS = ("main", "update", "downloading", "settings", "games", "error")
SIZES = ((1280, 720), (1280, 800))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--home", default="/tmp/openbfme-demo", help="the neutral home folder the launcher sees")
    ap.add_argument("--screens", default=",".join(SCREENS))
    args = ap.parse_args()
    if not os.environ.get("DISPLAY") and not os.environ.get("WAYLAND_DISPLAY"):
        print("launcher_screens: no display (set DISPLAY): Godot's headless mode renders nothing", file=sys.stderr)
        return 2
    godot = os.environ.get("GODOT", "godot")
    home = Path(args.home)
    shutil.rmtree(home, ignore_errors=True)
    home.mkdir(parents=True)
    args.out.mkdir(parents=True, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if k not in ("ROTWK_INSTALL", "BFME2_INSTALL", "OPENBFME_GAME_USER_DIR")}
    env.update(HOME=str(home), USER="player", LOGNAME="player", XDG_DATA_HOME=str(home / ".local/share"),
               XDG_CONFIG_HOME=str(home / ".config"), XDG_CACHE_HOME=str(home / ".cache"))
    subprocess.run([godot, "--headless", "--path", str(LAUNCHER), "--import"], env=env, capture_output=True, timeout=300)
    failed = 0
    for screen in args.screens.split(","):
        for w, h in SIZES:
            out = (args.out / f"launcher-{screen}-{w}x{h}.png").resolve()
            r = subprocess.run([godot, "--path", str(LAUNCHER), "--", f"--ui-demo={screen}", f"--screenshot-size={w}x{h}",
                                f"--screenshot={out}"], env=env, capture_output=True, text=True, timeout=120, stdin=subprocess.DEVNULL)
            ok = r.returncode == 0 and out.is_file()
            failed += not ok
            print(f"{'wrote' if ok else 'FAILED'} {out}" + ("" if ok else "\n" + r.stdout[-2000:] + r.stderr[-2000:]))
    shutil.rmtree(home, ignore_errors=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
