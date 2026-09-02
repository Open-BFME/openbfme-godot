"""Fast tests for tools/fleet. They run in the pre-commit gate."""
from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
FLEET = ROOT / "tools" / "fleet"
sys.path.insert(0, str(FLEET))

import fleetlib as fl  # noqa: E402


def _run(script: str, *args: str, env: dict | None = None) -> subprocess.CompletedProcess:
    merged = dict(os.environ)
    if env:
        merged.update(env)
    return subprocess.run([sys.executable, str(FLEET / script), *args], cwd=ROOT, capture_output=True, text=True, env=merged, timeout=120)


def test_queue_order_covers_every_derived_queue() -> None:
    assert set(fl.DERIVED) <= set(fl.QUEUE_ORDER)


def test_manual_queue_units_are_well_formed() -> None:
    for folder in (FLEET / "queues").iterdir():
        if not folder.is_dir():
            continue
        for path in folder.glob("*.json"):
            doc = json.loads(path.read_text(encoding="utf-8"))
            assert {"title", "rank", "detail", "oracle", "status"} <= set(doc), path
            assert doc["status"] in {"open", "done"}, path


def test_core_modules_queue_reads_census_and_excludes_existing_files(tmp_path, monkeypatch) -> None:
    census = tmp_path / "census.json"
    census.write_text(json.dumps({"members": [
        {"name": "AutoHealBehavior", "objectCount": {"rotwk-retail": 5}, "classification": "C"},
        {"name": "HordeContain", "objectCount": {"rotwk-retail": 300}, "classification": "C"},
    ]}), encoding="utf-8")
    modules = tmp_path / "Modules"
    modules.mkdir()
    (modules / "AutoHealBehavior.cs").write_text("// done", encoding="utf-8")
    (modules / "HordeContain.cs").write_text("=> world.RecordTechGap(TypeName);", encoding="utf-8")
    monkeypatch.setattr(fl, "CENSUS", census)
    monkeypatch.setattr(fl, "MODULES_DIR", modules)
    units, total = fl.core_modules_units()
    assert total == 2
    # A gap-recording stub is not an implementation; it stays open.
    assert [u["id"] for u in units] == ["core-modules/HordeContain"]


def test_red_queue_parses_fail_lines(tmp_path, monkeypatch) -> None:
    logs = tmp_path / "logs"
    logs.mkdir()
    (logs / "x-retail_slice_runner.txt").write_text(
        "RETAIL_SLICE PASS alpha\nRETAIL_SLICE FAIL beta (why)\nRETAIL_SLICE FAIL beta (again)\nRETAIL_SLICE_RESULT passed=1 failed=1\n",
        encoding="utf-8",
    )
    monkeypatch.setattr(fl, "LOGS", logs)
    units, total = fl.red_units()
    assert total == 3
    assert [u["id"] for u in units] == ["red/beta"]


def test_queue_next_prints_a_unit_or_no_work() -> None:
    result = _run("work.py", "next", "cook")
    assert result.returncode in (0, 1)
    assert "UNIT" in result.stdout or "NO_WORK" in result.stdout


def test_lock_acquire_release_roundtrip(tmp_path, monkeypatch) -> None:
    env = {"FLEET_AGENT": "tester-a"}
    lock_dir = tmp_path / "locks"
    monkeypatch.setenv("FLEET_AGENT", "tester-a")
    # Point the lock directory at tmp by running with a patched workspace.
    script = FLEET / "lock.py"
    code = (
        "import sys, fleetlib as fl, pathlib; fl.WORKSPACE = pathlib.Path(sys.argv[1]);"
        "import lock; lock.LOCKS = fl.WORKSPACE / 'locks'; sys.exit(lock.main(sys.argv[2:]))"
    )
    def run(*args, agent="tester-a"):
        return subprocess.run([sys.executable, "-c", code, str(tmp_path), *args], cwd=FLEET, capture_output=True, text=True, env=dict(os.environ, FLEET_AGENT=agent))
    hub = "game/src/retail_slice/retail_slice_sim.gd"
    assert run("acquire", hub).returncode == 0
    assert run("acquire", hub, agent="tester-b").returncode == 1
    assert run("check", hub, agent="tester-b").returncode == 1
    assert run("check", hub).returncode == 0
    assert run("release", hub).returncode == 0
    assert run("check", hub).returncode == 1  # unlocked hub while FLEET_AGENT set
    assert script.is_file() and lock_dir.parent.exists()


def test_audit_deps_finds_referrers() -> None:
    ghost = "tools/fleet/" + "zz-nonexistent-" + "unit.py"  # split so this file does not name it
    result = _run("audit_deps.py", "tools/fleet/hubs.txt", ghost)
    assert result.returncode == 0
    assert "REFERENCED tools/fleet/hubs.txt" in result.stdout
    assert f"FREE       {ghost}" in result.stdout


def test_contract_fixtures_match_required_keys() -> None:
    for name in ("snapshot-v1", "match-launch-v1", "bundle-v1"):
        schema = json.loads((ROOT / "contracts" / f"{name}.schema.json").read_text(encoding="utf-8"))
        fixture = json.loads((ROOT / "contracts" / "fixtures" / f"{name}.json").read_text(encoding="utf-8"))
        assert set(schema["required"]) <= set(fixture), name
        assert fixture["schema"] == schema["properties"]["schema"]["const"]
    snap = json.loads((ROOT / "contracts" / "fixtures" / "snapshot-v1.json").read_text(encoding="utf-8"))
    for key, values in snap["objects"].items():
        assert len(values) == snap["object_count"], key


@pytest.mark.skipif(not (ROOT / ".git").exists(), reason="needs a git checkout")
def test_precommit_passes_on_clean_index() -> None:
    result = _run("precommit.py", env={"FLEET_GATE_NO_PYTEST": "1"})
    assert result.returncode == 0, result.stdout + result.stderr
