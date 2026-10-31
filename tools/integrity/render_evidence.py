#!/usr/bin/env python3
# OpenBFME integrity evidence view (lane ANTICHEAT-1): renders the frames openbfme_peer --fog-check-dump wrote around detector D1's events into a
# picture and a short clip a reviewer can read at a glance. Two panels per frame, both from the re-simulated game:
#   left  "what PLAYER could see": PLAYER's own shroud (clear ground bright, fogged ground dark), PLAYER's units green, allies cyan, neutral players' grey,
#         the enemy objects PLAYER saw red (remembered buildings too);
#   right "what the game held": the same, plus the enemy objects hidden from PLAYER (orange).
# The target of each flagged order gets a yellow cross on both panels from the frame the order ran on. Python 3 standard library + ffmpeg.
#
#   render_evidence.py --dump DIR --report report.json --player P --out-dir OUT --name NAME [--scale 5]
# writes OUT/NAME.png (the frame of the first flagged order) and OUT/NAME.mp4 (every dumped frame, 5 frames a game second shown at 10 fps).
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

GREEN, CYAN, RED, ORANGE, YELLOW, GREY = (60, 220, 90), (80, 220, 230), (235, 60, 50), (255, 150, 30), (255, 235, 40), (205, 205, 215)


def safe_text(text: str) -> str:
    """Letters, digits, spaces and plain punctuation only (a caption, never markup)."""
    return "".join(c if c.isalnum() or c in " _-.,:()=+/" else "_" for c in str(text))[:160]


def read_terrain(path: Path):
    lines = path.read_text().splitlines()
    head = lines[0].split()
    w, h, cell, ox, oy = int(head[1]), int(head[2]), float(head[4]), float(head[6]), float(head[7])
    heights = [[int(v) for v in line.split()] for line in lines[1:1 + h]]
    return w, h, cell, ox, oy, heights


def read_frame(path: Path, h: int):
    lines = path.read_text().splitlines()
    frame = int(lines[0].split()[1])
    cells = lines[1:1 + h]
    objs = []
    for line in lines[1 + h:]:
        p = line.split()
        if p and p[0] == "o":
            objs.append({"id": int(p[1]), "rel": p[2], "x": float(p[3]), "y": float(p[4]), "seen": p[5] == "1", "kind": p[6]})
    return frame, cells, objs


def render(terrain, frame_data, targets_now, scale: int) -> tuple[int, int, bytearray]:
    w, h, cell, ox, oy, heights = terrain
    _, cells, objs = frame_data
    lo = min(min(r) for r in heights)
    hi = max(max(r) for r in heights)
    span = max(1, hi - lo)
    gap = 4 * scale
    pw, ph = w * scale, h * scale
    W, H = 2 * pw + gap, ph
    img = bytearray(W * H * 3)

    def put(px, py, c):
        if 0 <= px < W and 0 <= py < H:
            i = (py * W + px) * 3
            img[i:i + 3] = bytes(c)

    sep = bytes((16, 20, 24)) * gap
    for cy in range(h):
        lrow, rrow = bytearray(), bytearray()
        for cx in range(w):
            g = 70 + int(120 * (heights[cy][cx] - lo) / span)
            st = cells[cy][cx] if cy < len(cells) and cx < len(cells[cy]) else "s"
            left = (g, g, int(g * 0.9)) if st == "c" else (g * 2 // 5, g * 2 // 5, g // 2) if st == "f" else (0, 0, 0)
            lrow += bytes(left) * scale
            rrow += bytes((g, g, int(g * 0.9))) * scale
        line = bytes(lrow) + sep + bytes(rrow)
        for dy in range(scale):
            py = (h - 1 - cy) * scale + dy  # north up
            img[py * W * 3:(py + 1) * W * 3] = line

    def dot(panel, x, y, c, r, hollow=False):
        px0 = int((x - ox) / cell * scale) + panel * (pw + gap)
        py0 = int((h - (y - oy) / cell) * scale)
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                d = dx * dx + dy * dy
                if d <= r * r and (not hollow or d >= (r - 1) * (r - 1)):
                    put(px0 + dx, py0 + dy, c)

    def cross(panel, x, y, c, r):
        px0 = int((x - ox) / cell * scale) + panel * (pw + gap)
        py0 = int((h - (y - oy) / cell) * scale)
        for k in range(-r, r + 1):
            for t in (-1, 0, 1):
                put(px0 + k, py0 + k + t, c)
                put(px0 + k, py0 - k + t, c)

    for o in objs:
        r = scale if o["kind"] == "s" else max(2, scale // 2 + 1)
        if o["rel"] == "own":
            c = GREEN
        elif o["rel"] == "ally":
            c = CYAN
        elif o["rel"] == "neutral":
            c = GREY
        else:
            c = RED if o["seen"] else ORANGE
        if o["rel"] != "enemy" or o["seen"]:
            dot(0, o["x"], o["y"], c, r)
        dot(1, o["x"], o["y"], c, r, hollow=(o["rel"] == "enemy" and not o["seen"] and o["kind"] == "s"))
    for (x, y) in targets_now:
        cross(0, x, y, YELLOW, 3 * scale)
        cross(1, x, y, YELLOW, 3 * scale)
    return W, H, img


def ffmpeg_major(ffmpeg: str) -> int:
    """The major version `ffmpeg -version` names ("ffmpeg version 6.1.1-3ubuntu5", "ffmpeg version n7.1"); an answer it does not parse (a git
    build "N-...", a failed run) is an error: the filter-script option depends on it."""
    r = subprocess.run([ffmpeg, "-version"], capture_output=True, text=True)
    m = re.match(r"ffmpeg version n?(\d+)\.", r.stdout) if r.returncode == 0 else None
    if not m:
        raise RuntimeError(f"cannot read the ffmpeg version (exit {r.returncode}): {(r.stdout or r.stderr).splitlines()[:1]}")
    return int(m.group(1))


def filter_script_args(major: int, script: Path) -> list[str]:
    """ffmpeg 7 deprecated -filter_script (9 removes it: "Option not found"); its replacement is the file form of the option, -/filter:v FILE"""
    return ["-/filter:v", str(script)] if major >= 7 else ["-filter_script:v", str(script)]


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", required=True)
    ap.add_argument("--report", required=True)
    ap.add_argument("--player", type=int, required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--scale", type=int, default=5)
    ap.add_argument("--hold", type=int, default=12, help="frames a cross stays after its order")
    a = ap.parse_args(argv)
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        print("render_evidence: ffmpeg not found", file=sys.stderr)
        return 1
    dump = Path(a.dump)
    terrain = read_terrain(dump / "terrain.txt")
    report = json.loads(Path(a.report).read_text())
    player = next(p for p in report["players"] if p["player_index"] == a.player)
    events = player["events"]
    frames = sorted(dump.glob("frame_*.txt"))
    if not frames or not events:
        print("render_evidence: nothing to render", file=sys.stderr)
        return 1
    out = Path(a.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    dumped = {int(f.stem.split("_")[1]) for f in frames}
    first_event = next((e["frame"] for e in events if e["frame"] in dumped), None)
    with tempfile.TemporaryDirectory(prefix="evidence-") as tmp:
        still = None
        texts = []
        for i, f in enumerate(frames):
            fd = read_frame(f, terrain[1])
            fr = fd[0]
            now = [e for e in events if e["frame"] <= fr <= e["frame"] + a.hold]
            W, H, img = render(terrain, fd, [(e["x"], e["y"]) for e in now], a.scale)
            p = Path(tmp) / f"f{i:05d}.ppm"
            p.write_bytes(b"P6\n%d %d\n255\n" % (W, H) + bytes(img))
            label = f"frame {fr}  ({fr / 5:.1f} s)"
            if now:
                e = now[-1]
                label += f"   ORDER: {e['command'].replace('MSG_DO_', '').lower()} on {e['target_template']} - {e['reason'].replace('_', ' ')}"
            texts.append(label)
            if fr == first_event:
                still = p
        # captions: one text file per frame through a concat list of single-frame clips is heavy; drawtext with per-frame enable windows instead
        # every text from the report (names, templates) goes through a file read with expansion=none: never into the filter string itself
        font = "fontfile=/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
        who = safe_text(player.get("name") or f"player {a.player}")
        panel = terrain[0] * a.scale + 4 * a.scale

        def label(text: str, name: str) -> str:
            f = Path(tmp) / name
            f.write_text(safe_text(text), encoding="utf-8")
            return f"textfile={f}:expansion=none"

        filters = ["pad=ceil(iw/2)*2:ceil((ih+96)/2)*2:0:96:color=0x101418",
                   f"drawtext={font}:fontsize=18:fontcolor=white:x=10:y=8:"
                   + label("Integrity check D1 (orders on objects the player could not see) - re-simulated replay", "t_head.txt"),
                   f"drawtext={font}:fontsize=17:fontcolor=0x9be59b:x=10:y=70:" + label(f"What {who} could see (own fog of war)", "t_left.txt"),
                   f"drawtext={font}:fontsize=17:fontcolor=0xffb060:x={panel + 10}:y=70:"
                   + label(f"What the game held (orange = hidden from {who})", "t_right.txt")]
        for i, t in enumerate(texts):
            filters.append(f"drawtext={font}:fontsize=18:fontcolor=0xffeb28:x=10:y=38:" + label(t, f"t_{i:05d}.txt") + f":enable='eq(n\\,{i})'")
        script = Path(tmp) / "filters.txt"
        script.write_text(",".join(filters))
        common = [ffmpeg, "-hide_banner", "-loglevel", "error", "-y"]
        try:
            major = ffmpeg_major(ffmpeg)
        except RuntimeError as e:
            print(f"render_evidence: {e}", file=sys.stderr)
            return 1
        rc = subprocess.call(common + ["-framerate", "10", "-i", str(Path(tmp) / "f%05d.ppm"), *filter_script_args(major, script),
                                       "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "20", str(out / f"{a.name}.mp4")])
        if rc != 0:
            return rc
        idx = int(still.stem[1:]) if still else 0
        rc = subprocess.call(common + ["-i", str(out / f"{a.name}.mp4"), "-vf", f"select=eq(n\\,{idx})", "-frames:v", "1", str(out / f"{a.name}.png")])
        return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
