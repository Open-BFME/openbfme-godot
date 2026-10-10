"""Lane MP-3: tools/net/mp3_perf_table.py computes the measurement table from a census and a frame log (synthetic data with known answers)."""
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import mp3_perf_table as t  # noqa: E402


def test_lows_peaks_and_the_peak_minute(tmp_path):
    census = tmp_path / "c.csv"
    rows = ["frame,sim_us,battalions,troops,objects"]
    for f in range(1, 901):  # three game minutes; the troops peak (500) in minute 1 at frame 450, the battalions peak (40) at frame 800
        troops = 500 if f == 450 else 100
        batt = 40 if f == 800 else 10
        sim = 9000 if 300 <= f < 600 else 2000  # minute 1: 9 ms frames
        rows.append("%d,%d,%d,%d,%d" % (f, sim, batt, troops, 1000 + f))
    census.write_text("\n".join(rows) + "\n")
    frames = tmp_path / "f.log"
    lines = []
    for i in range(1000):  # 1000 rendered frames: 990 of 10 ms, 9 of 50 ms, 1 of 100 ms; logic frame i * 0.9
        us = 100000 if i == 500 else (50000 if i % 100 == 7 and i < 900 else 10000)
        lines.append("%d %d" % (int(i * 0.9), us))
    frames.write_text("\n".join(lines) + "\n")
    res = t.analyse(t.read_census(str(census)), t.read_frames(str(frames)))
    assert res["troops"]["peak"] == 500 and res["troops"]["frame"] == 450 and res["peak_minute"] == 1
    assert res["battalions"]["peak"] == 40 and abs(res["battalions"]["minute"] - 800 / 300) < 1e-9
    assert res["objects"]["peak"] == 1900
    assert abs(res["logic_peak"]["mean_ms"] - 9.0) < 1e-9 and res["logic_peak"]["max_ms"] == 9.0
    r = res["render_all"]
    assert r["frames"] == 1000
    # total 990 * 10 + 9 * 50 + 100 = 10450 ms -> 95.69 fps; 1 % = the 10 slowest (100, 9 x 50) = 550 ms -> 18.18 fps; 0.1 % = the slowest one: 10 fps
    assert abs(r["fps"] - 1000 * 1000 / 10450) < 1e-6
    assert abs(r["low1"] - 10 * 1000 / 550) < 1e-6
    assert abs(r["low01"] - 10.0) < 1e-9
    assert abs(r["p99_ms"] - 50.0) < 1e-9
    assert res["render_peak"]["frames"] > 0  # the frames showing logic frames 300 .. 599
    text = t.fmt(res, "synthetic")
    assert "peak troops 500 at game minute 1.5 (frame 450)" in text and "0.1% low 10.0" in text
