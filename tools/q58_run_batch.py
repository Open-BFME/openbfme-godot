"""Q58 measurement driver: run the pooled convert batch with bracketed walls.

Usage:
    python tools/q58_run_batch.py <logfile> [extra batch args...]

Writes stdout+stderr of the batch to <logfile>, bracketed with wall-clock
timestamps so the process wall (interpreter start to exit) is derivable.

Environment:
    OPENBFME_IMPORT_ROOT     state root (default: <repo>/workspace/retail-work)
    OPENBFME_ROTWK_INSTALL   retail install with game.dat (default: F:\\RotWK)
    OPENBFME_PYTHON          interpreter (default: the state root's pinned env)
Other OPENBFME_* switches are inherited by the batch.
"""

from __future__ import annotations

import datetime as _dt
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "importer"))

from openbfme_importer.paths import default_state_root  # noqa: E402


def state_root() -> Path:
    return default_state_root()


def pinned_python(state: Path) -> str:
    configured = os.environ.get("OPENBFME_PYTHON", "").strip()
    if configured:
        return configured
    return str(state / "tools" / "python-3.12-env" / "Scripts" / "python.exe")


def rotwk_install() -> str:
    return os.environ.get("OPENBFME_ROTWK_INSTALL", "").strip() or r"F:\RotWK"


def main() -> int:
    log_path = Path(sys.argv[1])
    extra = sys.argv[2:]
    state = state_root()
    command = [
        pinned_python(state),
        str(ROOT / "tools" / "rotwk_faction_convert_batch.py"),
        "--install",
        rotwk_install(),
        "--game",
        "rotwk",
        "--state-root",
        str(state),
        "--assets-root",
        str(state / "editions" / "rotwk" / "cache" / "effective-assets"),
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
