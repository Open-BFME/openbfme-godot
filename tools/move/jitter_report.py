#!/usr/bin/env python3
"""Lane MOVE-2 (FEEDBACK-1 F3): the drawn motion's jitter, from hud_viewer's smooth2 CSV (--s2-csv).

Each CSV row is one member's drawn pose in one render frame: segment, game time t, the frame's delta dt, id, x, y, angle, the presented logic
frame, the presentation alpha, the wall clock (us) and the worker's held presentations so far. Per segment it reports:
  * frames, the render rate by the engine's deltas and by the wall clock, the worst wall gap;
  * held: presentations the worker was late for (a freeze of the whole army for one render frame);
  * reversals: render frames whose drawn step points against the previous one while the member moves (> 3 units / s);
  * hitches: render frames whose drawn speed (step / dt) is under half or over 1.5 times the mean of the two frames before while the member
    moved steadily (those two within 25 % of each other): what the eye sees as a stutter of that member;
  * the speed ripple: the mean |speed_k - speed_k-1| / speed over moving frames (0 = perfectly even).
With --hordes only the horde objects' rows (last column 1: the devlog camera's focus follows them) are measured, else only the members'.
With --wall the speeds use the wall clock's frame times instead of the engine deltas (what a real-time viewer sees; a fixed-clock movie capture
plays its frames at the engine deltas, so leave it off for movie captures).

  python3 tools/move/jitter_report.py <csv> [--wall] [--hordes]
"""
import csv
import sys
from collections import defaultdict


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    path = sys.argv[1]
    wall = "--wall" in sys.argv[2:]
    hordes_only = "--hordes" in sys.argv[2:]
    rows = defaultdict(list)  # segment -> rows
    order = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            seg = r["segment"]
            if seg not in rows:
                order.append(seg)
            rows[seg].append(r)
    total_hitch = 0
    total_moving = 0
    for seg in order:
        rs = rows[seg]
        frames = sorted({float(r["t"]) for r in rs})
        index = {t: i for i, t in enumerate(frames)}
        dts = {}
        walls = {}
        held = []
        for r in rs:
            i = index[float(r["t"])]
            dts[i] = float(r["dt"])
            if "wall_us" in r and r["wall_us"]:
                walls[i] = int(r["wall_us"])
            if "held" in r and r["held"]:
                held.append(int(r["held"]))
        n = len(frames)
        wall_dt = {}
        for i in range(1, n):
            if i in walls and i - 1 in walls:
                wall_dt[i] = (walls[i] - walls[i - 1]) / 1e6
        tracks = defaultdict(dict)
        horde_ids = set()
        for r in rs:
            if r.get("horde") == "1":
                horde_ids.add(r["id"])
                if not hordes_only:
                    continue
            elif hordes_only:
                continue
            tracks[r["id"]][index[float(r["t"])]] = (float(r["x"]), float(r["y"]))
        reversals = hitches = moving = 0
        ripple_sum = 0.0
        ripple_n = 0
        for pts in tracks.values():
            prev_step = None
            speeds = {}
            for i in range(1, n):
                if i not in pts or i - 1 not in pts:
                    prev_step = None
                    continue
                dt = wall_dt.get(i) if wall else dts.get(i)
                if not dt or dt <= 0:
                    continue
                sx = pts[i][0] - pts[i - 1][0]
                sy = pts[i][1] - pts[i - 1][1]
                d = (sx * sx + sy * sy) ** 0.5
                speeds[i] = d / dt
                if d > 3.0 * dt:
                    moving += 1
                    if prev_step and prev_step[2] > 3.0 * prev_step[3] and sx * prev_step[0] + sy * prev_step[1] < 0:
                        reversals += 1
                prev_step = (sx, sy, d, dt)
            for i in range(2, n):
                if i not in speeds or i - 1 not in speeds or i - 2 not in speeds:
                    continue
                a, c, b = speeds[i - 2], speeds[i - 1], speeds[i]
                if a < 3.0 or c < 3.0:
                    continue
                ripple_sum += abs(b - c) / max(c, 1e-6)
                ripple_n += 1
                if abs(a - c) > 0.25 * max(a, c):
                    continue
                m = 0.5 * (a + c)
                if b < 0.5 * m or b > 1.5 * m:
                    hitches += 1
        total_hitch += hitches
        total_moving += moving
        eng = [dts[i] for i in range(n) if i in dts]
        wd = list(wall_dt.values())
        fps_eng = len(eng) / sum(eng) if eng and sum(eng) > 0 else 0.0
        fps_wall = len(wd) / sum(wd) if wd and sum(wd) > 0 else 0.0
        print("JITTER %-8s frames=%d fps(engine)=%.1f fps(wall)=%.1f worst_wall_gap_ms=%.1f held=%d members=%d moving_member_frames=%d reversals=%d hitches=%d (%.4f per moving frame) ripple=%.4f"
              % (seg, n, fps_eng, fps_wall, 1000.0 * max(wd) if wd else 0.0, (max(held) - min(held)) if held else 0, len(tracks), moving, reversals,
                 hitches, hitches / max(moving, 1), ripple_sum / max(ripple_n, 1)))
    print("JITTER total moving_member_frames=%d hitches=%d (%.4f)" % (total_moving, total_hitch, total_hitch / max(total_moving, 1)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
