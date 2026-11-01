#!/usr/bin/env python3
"""Repeat the full retail opts6 gate (or its no-retail wait units) under CPU load.

ROTWK_INSTALL=... BFME2_INSTALL=... python3 tools/opts/opts6_stress.py --godot "$GODOT" --runs 50
"""
from __future__ import annotations

import argparse
import hashlib
import multiprocessing
import os
import re
from pathlib import Path
import subprocess

REPO = Path(__file__).resolve().parents[2]


def burn_cpu() -> None:
    data = bytes(4096)
    while True:
        data = hashlib.sha256(data).digest() * 128


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--godot", default=os.environ.get("GODOT", "godot"))
    parser.add_argument("--runs", type=int, default=50)
    parser.add_argument("--workers", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--timeout", type=int, default=3600, help="wall-clock deadline per run, seconds")
    parser.add_argument("--unit", action="store_true", help="exercise only no-retail condition-wait units")
    parser.add_argument("--logs", type=Path, default=REPO / "workspace/test-side-effects/opts6-stress")
    args = parser.parse_args()
    if args.runs < 1 or args.workers < 1 or args.timeout < 1:
        parser.error("runs, workers and timeout must be positive")
    if not args.unit and any(not os.environ.get(key) for key in ("ROTWK_INSTALL", "BFME2_INSTALL")):
        parser.error("the full gate requires ROTWK_INSTALL and BFME2_INSTALL (a skip is not a stressed pass)")
    args.logs.mkdir(parents=True, exist_ok=True)
    script = "opts6_wait_test.gd" if args.unit else "opts6_test.gd"
    expected = "WAIT RESULT: 15 checks, 0 failed" if args.unit else "OPTS6 PASS"
    env = {**os.environ, "OPENBFME_TEST": "1"}
    workers = [multiprocessing.get_context("spawn").Process(target=burn_cpu) for _ in range(args.workers)]
    started = []
    try:
        for worker in workers:
            worker.start()
            started.append(worker)
        for iteration in range(1, args.runs + 1):
            with (args.logs / f"run-{iteration:03d}.log").open("w") as log:
                try:
                    result = subprocess.run([args.godot, "--headless", "--path", str(REPO / "godot"), "--script", f"res://tests/{script}"],
                                            env=env, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
                except subprocess.TimeoutExpired:
                    print(f"FAIL run {iteration}: exceeded {args.timeout}s", flush=True)
                    return 1
            output = (args.logs / f"run-{iteration:03d}.log").read_text(errors="replace")
            # a FAIL / SKIP result line, not the word inside a passing line (the retail run's sabotage self-checks report "1 FAIL lines" when they pass)
            ok = result.returncode == 0 and expected in output and not re.search(r"(?m)^\s*(FAIL|SKIP)\b|OPTS6 FAIL|SCRIPT ERROR|Parse Error", output)
            if not all(worker.is_alive() for worker in workers):
                print("FAIL: a CPU stress worker exited", flush=True)
                return 1
            print(f"{'PASS' if ok else 'FAIL'} run {iteration}/{args.runs} ({args.workers} CPU workers)", flush=True)
            if not ok:
                return 1
    finally:
        for worker in started:
            worker.terminate()
        for worker in started:
            worker.join()
    print(f"STRESS PASS: {args.runs}/{args.runs}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
