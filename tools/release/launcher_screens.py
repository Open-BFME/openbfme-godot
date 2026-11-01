"""Lane UI-3: screenshots of every launcher screen, for the README / devlog and the layout review.

    DISPLAY=:1 python3 tools/release/launcher_screens.py --out workspace/rebuild/ui3

Runs the launcher project (a development run on the Godot 4.7.2 editor binary, $GODOT or `godot`) once per screen and size with
--ui-demo=<screen> (made-up state, no network) and --screenshot-size, under a neutral home folder (/tmp/openbfme-demo by default), so no
real user name or home folder can appear in a shot. Needs a display (Godot's headless mode renders nothing). Writes
launcher-<screen>-<W>x<H>.png and prints one line per file. --releases=<file>: a GitHub release list (the API's JSON, e.g.
`gh api repos/<owner>/<name>/releases`) put in the launcher's cache, so the notes cards show real release notes instead of demo text
(lane UI-5).
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
SCREENS = ("main", "news", "multiplayer", "mods", "games", "games-bfme1", "games-downloading", "settings", "display", "about", "update", "downloading",
           "error", "glibc", "first-run", "offline", "verifying", "paused", "update-launcher", "warning", "ask", "playing", "long", "picker")
SIZES = ((1280, 720), (1280, 800))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--home", default="/tmp/openbfme-demo", help="the neutral home folder the launcher sees")
    ap.add_argument("--screens", default=",".join(SCREENS))
    ap.add_argument("--project", type=Path, default=LAUNCHER, help="the launcher project to run (a copy, for before / after shots)")
    ap.add_argument("--sizes", default="", help="WxH,WxH: instead of 1280x720 and 1280x800")
    ap.add_argument("--window", action="store_true",
                    help="lay out as a window of that size under the project's stretch (--screenshot-window) instead of exactly WxH pixels")
    ap.add_argument("--releases", type=Path, help="a GitHub release list (JSON) for the notes cards")
    ap.add_argument("--language", default="", help="lane L10N-1: show the launcher's own text in this catalog language (tools/l10n/catalog/)")
    args = ap.parse_args()
    if not os.environ.get("DISPLAY") and not os.environ.get("WAYLAND_DISPLAY"):
        print("launcher_screens: no display (set DISPLAY): Godot's headless mode renders nothing", file=sys.stderr)
        return 2
    godot = os.environ.get("GODOT", "godot")
    home = Path(args.home)
    shutil.rmtree(home, ignore_errors=True)
    home.mkdir(parents=True)
    if args.releases:
        cache = home / ".local/share/OpenBFMELauncher/cache"
        cache.mkdir(parents=True)
        shutil.copyfile(args.releases, cache / "releases.json")
    args.out.mkdir(parents=True, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if k not in ("ROTWK_INSTALL", "BFME2_INSTALL", "OPENBFME_GAME_USER_DIR")}
    env.update(HOME=str(home), USER="player", LOGNAME="player", XDG_DATA_HOME=str(home / ".local/share"),
               XDG_CONFIG_HOME=str(home / ".config"), XDG_CACHE_HOME=str(home / ".cache"))
    subprocess.run([godot, "--headless", "--path", str(args.project), "--import"], env=env, capture_output=True, timeout=300)
    failed = 0
    sizes = [tuple(int(v) for v in x.split("x")) for x in args.sizes.split(",")] if args.sizes else SIZES
    option = "--screenshot-window" if args.window else "--screenshot-size"
    for screen in args.screens.split(","):
        for w, h in sizes:
            lang = f"-{args.language}" if args.language else ""
            out = (args.out / f"launcher-{screen}-{w}x{h}{lang}.png").resolve()
            r = subprocess.run([godot, "--path", str(args.project), "--", f"--ui-demo={screen}", f"{option}={w}x{h}",
                                f"--screenshot={out}", *([f"--language={args.language}"] if args.language else [])], env=env, capture_output=True, text=True, timeout=120, stdin=subprocess.DEVNULL)
            ok = r.returncode == 0 and out.is_file()
            failed += not ok
            print(f"{'wrote' if ok else 'FAILED'} {out}" + ("" if ok else "\n" + r.stdout[-2000:] + r.stderr[-2000:]))
    shutil.rmtree(home, ignore_errors=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
