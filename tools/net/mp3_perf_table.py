#!/usr/bin/env python3
"""Lane MP-3: the owner's 8-player measurements from one rendered client's logs.

  mp3_perf_table.py --census CENSUS.csv [--frames PERF.log] [--label TEXT]

CENSUS.csv  the network game's per-logic-frame census (game.gd --net-census= / openbfme_peer --census): frame, sim_us (the frame's wall time on the
            simulation owner), battalions (living KindOf HORDE objects), troops (living INFANTRY / CAVALRY / MONSTER objects, horde members one by one),
            objects (every object).
PERF.log    the rendered frames (game.gd --perf-log=): "<logic frame shown> <frame time us>" per line.

Prints one table row group: the render frame rate over the whole game and over the peak-battle minute (average fps = frames / time, the 1 % and 0.1 % lows =
the frame rate of the slowest 1 % / 0.1 % of the frames, p99 frame time), the peaks of battalions, troops and objects with their game minute, and the logic
frame time (mean / p99 / max) over the peak-battle minute. The peak-battle minute is the game minute (300 logic frames at 5 per second) holding the peak
troop count (the most units on the field).
"""
import argparse
import csv
import sys

FRAMES_PER_MINUTE = 300  # 5 logic frames a second


def read_census(path):
    rows = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            rows.append({k: int(v) for k, v in r.items()})
    if not rows:
        raise SystemExit("%s: no census rows" % path)
    return rows


def read_frames(path):
    out = []
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) == 2:
                out.append((int(p[0]), int(p[1])))
    return out


def frame_stats(us):
    """us: frame times in microseconds -> dict(fps, low1, low01, p99_ms, frames)"""
    if not us:
        return None
    s = sorted(us)
    n = len(s)
    total = sum(s)

    def low(frac):
        k = max(1, int(n * frac))
        worst = s[n - k:]
        return 1e6 * k / sum(worst)

    return {"frames": n, "fps": 1e6 * n / total, "low1": low(0.01), "low01": low(0.001), "p99_ms": s[min(n - 1, int(0.99 * n))] / 1000.0}


def percentile(values, q):
    s = sorted(values)
    return s[min(len(s) - 1, int(q * len(s)))]


def peak(rows, key):
    best = max(rows, key=lambda r: (r[key], -r["frame"]))
    return best[key], best["frame"]


def analyse(census, frames):
    res = {}
    for key in ("battalions", "troops", "objects"):
        v, f = peak(census, key)
        res[key] = {"peak": v, "frame": f, "minute": f / FRAMES_PER_MINUTE}
    m = res["troops"]["frame"] // FRAMES_PER_MINUTE
    lo, hi = m * FRAMES_PER_MINUTE, (m + 1) * FRAMES_PER_MINUTE
    res["peak_minute"] = m
    sim = [r["sim_us"] for r in census if lo <= r["frame"] < hi and r["sim_us"] >= 0]
    if sim:
        res["logic_peak"] = {"mean_ms": sum(sim) / len(sim) / 1000.0, "p99_ms": percentile(sim, 0.99) / 1000.0, "max_ms": max(sim) / 1000.0}
    sim_all = [r["sim_us"] for r in census if r["sim_us"] >= 0]
    if sim_all:
        res["logic_all"] = {"mean_ms": sum(sim_all) / len(sim_all) / 1000.0, "p99_ms": percentile(sim_all, 0.99) / 1000.0, "max_ms": max(sim_all) / 1000.0}
    if frames:
        res["render_all"] = frame_stats([us for _, us in frames])
        res["render_peak"] = frame_stats([us for lf, us in frames if lo <= lf < hi])
    return res


def fmt(res, label):
    out = []
    out.append("measurement: %s" % label)

    def rline(name, st):
        if st:
            out.append("  render %s: avg %.1f fps, 1%% low %.1f, 0.1%% low %.1f, p99 frame %.2f ms (%d frames)" % (name, st["fps"], st["low1"], st["low01"],
                                                                                                                   st["p99_ms"], st["frames"]))

    rline("whole game", res.get("render_all"))
    rline("peak-battle minute %d" % res["peak_minute"], res.get("render_peak"))
    for key in ("battalions", "troops", "objects"):
        p = res[key]
        out.append("  peak %s %d at game minute %.1f (frame %d)" % (key, p["peak"], p["minute"], p["frame"]))
    for name, k in (("peak-battle minute %d" % res["peak_minute"], "logic_peak"), ("whole game", "logic_all")):
        if k in res:
            l = res[k]
            out.append("  logic frame time %s: mean %.2f ms, p99 %.2f ms, max %.2f ms" % (name, l["mean_ms"], l["p99_ms"], l["max_ms"]))
    return "\n".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--census", required=True)
    ap.add_argument("--frames")
    ap.add_argument("--label", default="")
    a = ap.parse_args(argv)
    census = read_census(a.census)
    frames = read_frames(a.frames) if a.frames else []
    print(fmt(analyse(census, frames), a.label or a.census))
    return 0


if __name__ == "__main__":
    sys.exit(main())
