"""Lane WEAPON-1 binary-fact tests: the research scripts' assertions about the RotWK image (SKIPPED LOUDLY without RW_GAME_DAT) and the
reference models' golden vectors (no image needed)."""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent


def _run(script: str, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(HERE / script), *args], capture_output=True, text=True)


def _need_image():
    if not os.environ.get("RW_GAME_DAT"):
        pytest.skip("SKIPPED LOUDLY: RW_GAME_DAT is not set (the binary facts cannot be re-checked)")


def test_timing_facts_hold_in_the_binary():
    _need_image()
    r = _run("dump_timing_facts.py")
    assert r.returncode == 0, r.stdout[-2000:] + r.stderr[-2000:]
    assert "28/28 facts hold" in r.stdout


def test_timing_reference_model_golden_sequences():
    r = _run("dump_timing_facts.py", "--golden")
    assert r.returncode == 0, r.stdout[-2000:] + r.stderr[-2000:]


def test_damage_reference_model_golden_vectors():
    r = _run("damage_golden.py")
    assert r.returncode == 0, r.stdout[-3000:]
    assert "FAIL" not in r.stdout
    assert r.stdout.count("ok   ") >= 30


def test_damage_tables_dump_runs_against_the_binary(tmp_path):
    _need_image()
    r = _run("dump_damage_tables.py", str(tmp_path))
    assert r.returncode == 0, r.stdout[-2000:] + r.stderr[-2000:]
    assert (tmp_path / "table_armor_block.tsv").exists()
