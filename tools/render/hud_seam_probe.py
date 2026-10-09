#!/usr/bin/env python3
"""Windowed pixel probe of the Palantir's PlayerMagic progress wedge and mask seam (lane HUD-3, round 2).

Two artifacts sat in the PlayerMagic.ProgressBar mask's box: a 1% wedge of the radial progress ring (the bar ran one frame past its frame-0 Stop because the
engine's call flushed new clips early) and a dark seam at the mask's right edge (the mask's canvas group had no cleared margin). At the start of a game the
bar shows nothing (retail's progress is 1, frame 0), so the box must look the same with and without the ring piece:

    python3 tools/render/hud_seam_probe.py --godot <godot> --out <dir> [--render-size 1920x1080] [hud_viewer options...]

runs the HUD viewer twice (windowed: DISPLAY must be set), the second time with OPENBFME_APT_SKIP=PlayerMagic.ProgressBar.instance2, reads the mask's box
from the first run's HUD OP lines and checks every pixel of it (plus 2 pixels around) within one byte. Exit 0 on success.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from globe_probe import read_png  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def run(godot: str, out: str, extra: list[str], skip: str) -> str:
    env = dict(os.environ)
    env["OPENBFME_APT_SKIP"] = skip
    args = [godot, "--path", os.path.join(REPO, "godot"), "res://scenes/hud_viewer.tscn", "--", "--screenshot=" + out, "--report"] + extra
    return subprocess.run(args, env=env, capture_output=True, text=True, timeout=600).stdout


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--godot", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--render-size", default="")
    opts, extra = ap.parse_known_args()
    os.makedirs(opts.out, exist_ok=True)
    if opts.render_size:
        extra = extra + ["--render-size=" + opts.render_size]
    a_png, b_png = os.path.join(opts.out, "with-ring.png"), os.path.join(opts.out, "without-ring.png")
    log = run(opts.godot, a_png, extra, "")
    run(opts.godot, b_png, extra, "PlayerMagic.ProgressBar.instance2")
    box = None
    for line in log.splitlines():
        if line.startswith("HUD OP mesh") and "PlayerMagic.ProgressBar.instance1" in line and "mask=1" in line:
            m = re.search(r"box=\((-?\d+) (-?\d+) (-?\d+) (-?\d+)\)", line)
            box = tuple(int(v) for v in m.groups())
    if not box:
        print("the ProgressBar mask was not found in the HUD OP lines")
        return 2
    a, b = read_png(a_png), read_png(b_png)
    worst = 0
    x0, y0, x1, y1 = box
    for y in range(max(0, y0 - 2), min(a[1], y1 + 3)):
        for x in range(max(0, x0 - 2), min(a[0], x1 + 3)):
            pa = a[3][y][x * a[2]:x * a[2] + 3]
            pb = b[3][y][x * b[2]:x * b[2] + 3]
            worst = max(worst, max(abs(pa[k] - pb[k]) for k in range(3)))
    print(f"ProgressBar mask box {box}: largest difference with / without the ring {worst} (of 255)")
    return 0 if worst <= 1 else 1


if __name__ == "__main__":
    sys.exit(main())
