"""Q58 byte-identity legs: serial oracle vs pooled, corpus cache on/off.

Runs the requested legs sequentially, copying each leg's coverage + object
artifacts aside, then diffs them with q58_compare_coverage.py.

Usage: python tools/q58_identity_legs.py <workdir> <faction> <leg> [...]
Legs: pooled-cold, pooled-warm, pooled-nocache, serial-oracle
Env for every leg: OPENBFME_NO_COVERAGE_SHORTCIRCUIT=1, OPENBFME_NO_OBJECT_CACHE=1
(so every object genuinely compiles through the corpus-cache-fed tables).
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from q58_run_batch import pinned_python, rotwk_install, state_root  # noqa: E402

STATE = state_root()
PYTHON = Path(pinned_python(STATE))
COVERAGE_ROOT = STATE / "editions" / "rotwk" / "reports" / "faction-import"

LEG_ENV = {
    "pooled-cold": {"OPENBFME_NO_CORPUS_CACHE": ""},
    "pooled-warm": {"OPENBFME_NO_CORPUS_CACHE": ""},
    "pooled-nocache": {"OPENBFME_NO_CORPUS_CACHE": "1"},
    "serial-oracle": {"OPENBFME_NO_CORPUS_CACHE": "1"},
}


def run_leg(workdir: Path, faction: str, leg: str) -> None:
    env = dict(os.environ)
    env["PYTHONPATH"] = str(ROOT / "importer")
    env["OPENBFME_NO_COVERAGE_SHORTCIRCUIT"] = "1"
    env["OPENBFME_NO_OBJECT_CACHE"] = "1"
    for key, value in LEG_ENV[leg].items():
        if value:
            env[key] = value
        else:
            env.pop(key, None)
    command = [
        str(PYTHON),
        str(ROOT / "tools" / "rotwk_faction_convert_batch.py"),
        "--install",
        rotwk_install(),
        "--game",
        "rotwk",
        "--state-root",
        str(STATE),
        "--assets-root",
        str(STATE / "editions" / "rotwk" / "cache" / "effective-assets"),
        "--faction",
        faction,
    ]
    if leg.startswith("pooled"):
        command += ["--produce-procs", "12", "--convert-jobs", "1"]
    log = workdir / f"{faction}-{leg}.log"
    workdir.mkdir(parents=True, exist_ok=True)
    started = time.perf_counter()
    with log.open("w", encoding="utf-8", errors="replace") as sink:
        code = subprocess.call(command, stdout=sink, stderr=subprocess.STDOUT, env=env)
    wall = time.perf_counter() - started
    print(f"LEG {faction} {leg} exit={code} wall={wall:.1f}s", flush=True)
    if code != 0:
        raise SystemExit(f"leg failed: {faction} {leg}")
    # Copy the outputs aside before the next leg overwrites them.
    dest = workdir / f"{faction}-{leg}"
    if dest.exists():
        shutil.rmtree(dest)
    dest.mkdir(parents=True)
    shutil.copy2(COVERAGE_ROOT / f"{faction}-coverage.json", dest / "coverage.json")
    shutil.copytree(COVERAGE_ROOT / faction / "objects", dest / "objects")


def main() -> int:
    workdir = Path(sys.argv[1])
    faction = sys.argv[2]
    for leg in sys.argv[3:]:
        run_leg(workdir, faction, leg)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
