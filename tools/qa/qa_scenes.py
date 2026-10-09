#!/usr/bin/env python3
"""Lane QA-2: plays the feedback scenes (godot/scripts/qa_scene.gd) in real skirmishes, windowed, and makes a clip of each recording.

Each scene is one process of the game scene (game.gd --auto --qa --qa-scene=<name> --screens=<dir>): main menu -> Skirmish lobby -> load -> the scene
(a logged test hook places the units, the orders go through the HUD's input) -> Esc, Exit -> score screen -> lobby. qa_scene.gd saves every rendered
frame of a recording as <dir>/qa2-<tag>-<label>/fNNNNN.jpg with the game time of each frame (times.txt); this script turns each recording into
<dir>/qa2-<tag>-<label>.mp4 at real game speed (ffmpeg) and keeps the run's log beside it.

  python3 tools/qa/qa_scenes.py --out DIR [--scenes melee archers ...] [--faction FactionMen] [--map KEY] [--keep-frames]

Needs ROTWK_INSTALL / BFME2_INSTALL, a display (DISPLAY) and GODOT (default godot). Nothing retail is written into the repository.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
PROJECT = Path(os.environ["QA_PROJECT"]) if os.environ.get("QA_PROJECT") else ROOT / "godot"
SCENES = ["melee", "archers", "troll", "rohirrim", "charge", "garrison", "grond", "wall", "produce"]


def clip_fps(times: list[tuple[int, float]]) -> float:
    """The frame rate that plays the recorded frames at real game speed (frames / game seconds)."""
    if len(times) < 2 or times[-1][1] <= times[0][1]:
        return 30.0
    return (len(times) - 1) / ((times[-1][1] - times[0][1]) / 1000.0)


def read_times(path: Path) -> list[tuple[int, float]]:
    out = []
    for line in path.read_text().splitlines():
        parts = line.split()
        if len(parts) >= 2:
            out.append((int(parts[0]), float(parts[1])))
    return out


def encode(rec: Path, keep_frames: bool) -> Path | None:
    times = read_times(rec / "times.txt") if (rec / "times.txt").exists() else []
    if not times:
        return None
    fps = clip_fps(times)
    mp4 = rec.with_suffix(".mp4")
    cmd = ["ffmpeg", "-y", "-loglevel", "error", "-framerate", f"{fps:.3f}", "-i", str(rec / "f%05d.jpg"), "-vf", "fps=30,scale=1280:-2",
           "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "23", str(mp4)]
    subprocess.run(cmd, check=True)
    if not keep_frames:
        shutil.rmtree(rec)
    return mp4


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--scenes", nargs="*", default=SCENES)
    ap.add_argument("--faction", default="FactionMen")
    ap.add_argument("--map", default="maps/map mp tournament mp1/map mp tournament mp1.map")
    ap.add_argument("--seed", type=int, default=5150)
    ap.add_argument("--timeout", type=float, default=1200.0)
    ap.add_argument("--keep-frames", action="store_true")
    a = ap.parse_args(argv)
    if not os.environ.get("ROTWK_INSTALL") or not os.environ.get("BFME2_INSTALL"):
        print("QA SKIP: ROTWK_INSTALL / BFME2_INSTALL are not set")
        return 77
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    godot = os.environ.get("GODOT", "godot")
    bad = 0
    for name in a.scenes:
        log = out / f"scene-{name}.log"
        cmd = [godot, "--path", str(PROJECT), "--", "--auto", "--qa", f"--qa-scene={name}", f"--faction={a.faction}", f"--map={a.map}", "--ai=2",
               "--opponents=1", f"--seed={a.seed}", "--qa-minutes=10", "--qa-speed=1", f"--qa-tag={name}", "--res=1280x720", f"--screens={out}",
               "--no-record"]
        env = dict(os.environ)
        env.setdefault("MALLOC_ARENA_MAX", "2")
        with log.open("w", encoding="utf-8", errors="replace") as fh:
            fh.write("# " + " ".join(cmd) + "\n")
            fh.flush()
            try:
                code = subprocess.run(cmd, stdout=fh, stderr=subprocess.STDOUT, env=env, timeout=a.timeout).returncode
            except subprocess.TimeoutExpired:
                code = "timeout"
        clips = []
        for rec in sorted(out.glob(f"qa2-{name}-*")):
            if rec.is_dir():
                mp4 = encode(rec, a.keep_frames)
                if mp4:
                    clips.append(mp4.name)
        issues = [l.strip() for l in log.read_text(errors="replace").splitlines() if l.startswith("QA ISSUE")]
        print(f"QA scene {name}: exit {code}, clips {clips}, {len(issues)} issues", flush=True)
        for i in issues:
            print("  " + i[:300], flush=True)
        bad += 0 if code == 0 else 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
