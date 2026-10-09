"""Lane MOVE-2: tools/move/jitter_report.py on synthetic smooth2 CSVs."""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = "segment,t,dt,id,x,y,angle,frame,alpha,wall_us,held,horde\n"


def run(tmp_path, rows, *args):
    p = tmp_path / "m.csv"
    p.write_text(HEADER + "".join(rows))
    out = subprocess.run([sys.executable, os.path.join(HERE, "jitter_report.py"), str(p), *args], capture_output=True, text=True, check=True)
    return out.stdout


def rows_for(xs, dt=1.0 / 60.0, horde=0):
    return ["march,%.5f,%.5f,7,%.4f,0,0,1,0,%d,0,%d\n" % ((i + 1) * dt, dt, x, int((i + 1) * dt * 1e6), horde) for i, x in enumerate(xs)]


def test_even_motion_has_no_hitch(tmp_path):
    out = run(tmp_path, rows_for([i * 0.5 for i in range(60)]))
    assert "hitches=0 " in out and "reversals=0 " in out


def test_a_stall_is_a_hitch_and_a_step_back_a_reversal(tmp_path):
    xs = [i * 0.5 for i in range(30)]
    xs[15] = xs[14]                      # one frame without motion between steady frames
    xs += [xs[-1] - 0.5, xs[-1] - 1.0]   # then it walks back
    out = run(tmp_path, rows_for(xs))
    assert "hitches=0 " not in out
    assert "reversals=1 " in out


def test_horde_rows_are_measured_apart(tmp_path):
    rows = rows_for([i * 0.5 for i in range(30)]) + rows_for([i * 0.5 for i in range(30)], horde=1)
    assert "members=1 " in run(tmp_path, rows)
    assert "members=1 " in run(tmp_path, rows, "--hordes")
