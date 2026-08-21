"""Q58 measurement driver: run the pooled convert batch with bracketed walls.

Usage:
    python tools/q58_run_batch.py <logfile> [extra batch args...]

Writes stdout+stderr of the batch to <logfile>, bracketed with wall-clock
timestamps so the process wall (interpreter start to exit) is derivable.
Environment (PYTHONPATH, OPENBFME_* switches) is inherited from the caller.
"""

from __future__ import annotations

import datetime as _dt
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STATE = Path(r"C:\Users\Jonathan\Desktop\open-bfme\workspace\retail-work")
PYTHON = STATE / "tools" / "python-3.12-env" / "Scripts" / "python.exe"


def main() -> int:
    log_path = Path(sys.argv[1])
    extra = sys.argv[2:]
    command = [
        str(PYTHON),
        str(ROOT / "tools" / "rotwk_faction_convert_batch.py"),
        "--install",
        r"F:\RotWK",
        "--game",
        "rotwk",
        "--state-root",
        str(STATE),
        "--assets-root",
        str(STATE / "editions" / "rotwk" / "cache" / "effective-assets"),
        *extra,
    ]
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w", encoding="utf-8", errors="replace") as sink:
        sink.write(f"COMMAND {' '.join(command)}\n")
        started = time.perf_counter()
        sink.write(f"START epoch={time.time():.3f} {_dt.datetime.now().isoformat()}\n")
        sink.flush()
        code = subprocess.call(command, stdout=sink, stderr=subprocess.STDOUT)
        wall = time.perf_counter() - started
        sink.write(
            f"EXIT code={code} process_wall_s={wall:.1f} "
            f"epoch={time.time():.3f} {_dt.datetime.now().isoformat()}\n"
        )
    print(f"done code={code} wall={wall:.1f}s log={log_path}")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
