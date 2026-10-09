"""Tests for the stale-helper guard (helpers.json, helpers.py, helper_stale.ps1).

No retail files or built helpers are needed: every case builds a throwaway manifest in tmp_path and sets file
times explicitly. Expected values are the rule itself (a helper is stale when any input is newer than its exe,
or the exe is missing) and the message contract the tests and build rely on: "stale helper: <name> (...);
rebuild with <command>".

    python -m pytest tools/retail_oracle/test_helpers.py
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import helpers  # noqa: E402

HERE = Path(__file__).resolve().parent
PS1 = HERE / "helper_stale.ps1"
needs_powershell = pytest.mark.skipif(shutil.which("powershell") is None, reason="SKIPPED LOUDLY: powershell.exe not found (build.bat's stale check cannot run)")


def stamp(path: Path, seconds: int) -> None:
    os.utime(path, ns=(seconds * 10**9, seconds * 10**9))


@pytest.fixture
def tree(tmp_path, monkeypatch):
    """exe at t=1000, one source file at t=500, a source directory with a file at t=600; returns (manifest, parts)."""
    monkeypatch.delenv("HELPERS_FORCE", raising=False)
    (tmp_path / "build").mkdir()
    (tmp_path / "src" / "dir").mkdir(parents=True)
    exe = tmp_path / "build" / "tool.exe"
    exe.write_bytes(b"MZ")
    src = tmp_path / "src" / "tool.cpp"
    src.write_text("int main(){}")
    dep = tmp_path / "src" / "dir" / "dep.h"
    dep.write_text("#pragma once")
    stamp(exe, 1000)
    stamp(src, 500)
    stamp(dep, 600)
    manifest = tmp_path / "helpers.json"
    manifest.write_text(json.dumps({"rebuild": "build-it.bat", "helpers": {"tool": {"exe": "build/tool.exe", "inputs": ["src/tool.cpp", "src/dir"]}}}))
    return manifest, {"exe": exe, "src": src, "dep": dep}


def test_fresh_helper_is_not_stale(tree):
    manifest, _ = tree
    assert helpers.stale_reason("tool", manifest) is None
    assert helpers.stale_message("tool", manifest) is None
    helpers.require_fresh("tool", manifest)  # does not fail


def test_source_newer_than_the_exe_is_stale_with_the_rebuild_command(tree):
    manifest, p = tree
    stamp(p["src"], 2000)
    assert helpers.stale_message("tool", manifest) == "stale helper: tool (tool.cpp is newer than the exe); rebuild with build-it.bat"
    with pytest.raises(pytest.fail.Exception, match=r"stale helper: tool .*rebuild with build-it\.bat"):
        helpers.require_fresh("tool", manifest)


def test_a_file_inside_an_input_directory_counts(tree):
    manifest, p = tree
    stamp(p["dep"], 2000)
    assert "dep.h is newer than the exe" in helpers.stale_message("tool", manifest)


def test_a_missing_exe_is_not_built_and_is_left_to_the_callers_skip(tree):
    manifest, p = tree
    p["exe"].unlink()
    assert helpers.stale_reason("tool", manifest) == "not built"
    helpers.require_fresh("tool", manifest)  # the callers' own loud skip reports it; require_fresh must not mask that


def test_a_wrong_input_path_is_an_error_not_fresh(tree):
    manifest, p = tree
    doc = json.loads(manifest.read_text())
    doc["helpers"]["tool"]["inputs"].append("src/deleted.cpp")
    manifest.write_text(json.dumps(doc))
    with pytest.raises(FileNotFoundError, match="deleted.cpp"):
        helpers.stale_reason("tool", manifest)
    with pytest.raises(KeyError, match="nosuch"):
        helpers.stale_reason("nosuch", manifest)


def test_a_wrong_input_path_is_an_error_even_when_not_built_or_forced(tree, monkeypatch):
    # Input validation must not depend on the exe existing or on HELPERS_FORCE: otherwise a manifest typo
    # passes on any machine where the helpers were never built.
    manifest, p = tree
    doc = json.loads(manifest.read_text())
    doc["helpers"]["tool"]["inputs"].append("src/deleted.cpp")
    manifest.write_text(json.dumps(doc))
    p["exe"].unlink()
    with pytest.raises(FileNotFoundError, match="deleted.cpp"):
        helpers.stale_reason("tool", manifest)
    monkeypatch.setenv("HELPERS_FORCE", "1")
    with pytest.raises(FileNotFoundError, match="deleted.cpp"):
        helpers.stale_reason("tool", manifest)


def test_an_empty_input_directory_is_an_error(tree):
    manifest, _ = tree
    (manifest.parent / "emptydir").mkdir()
    doc = json.loads(manifest.read_text())
    doc["helpers"]["tool"]["inputs"].append("emptydir")
    manifest.write_text(json.dumps(doc))
    with pytest.raises(FileNotFoundError, match="emptydir"):
        helpers.stale_reason("tool", manifest)


def test_helpers_force_makes_everything_stale(tree, monkeypatch):
    manifest, _ = tree
    monkeypatch.setenv("HELPERS_FORCE", "1")
    assert helpers.stale_reason("tool", manifest) == "HELPERS_FORCE is set"


def test_the_real_manifest_names_existing_inputs_for_every_helper():
    # A typo or a deleted source would make stale_reason raise; every real helper must resolve.
    doc = json.loads(helpers.MANIFEST.read_text(encoding="utf-8"))
    assert set(doc["helpers"]) == {"retail_oracle", "x87_oracle", "numeric_driver", "apt_driver", "refpack_driver", "hash_driver"}
    for name in doc["helpers"]:
        helpers.stale_reason(name)  # raises on an unknown input


def run_ps(manifest: Path, *names: str, env_force: bool = False):
    env = {k: v for k, v in os.environ.items() if k != "HELPERS_FORCE"}
    if env_force:
        env["HELPERS_FORCE"] = "1"
    cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(PS1), "-Manifest", str(manifest)]
    if names:
        cmd += ["-Name", ",".join(names)]
    return subprocess.run(cmd, capture_output=True, text=True, env=env)


@needs_powershell
def test_build_side_check_agrees_with_the_python_rule(tree):
    # helper_stale.ps1 decides what build.bat rebuilds; it must classify exactly as helpers.py does.
    manifest, p = tree
    r = run_ps(manifest)
    assert r.returncode == 0 and r.stdout.strip() == ""
    stamp(p["dep"], 2000)
    r = run_ps(manifest)
    assert r.returncode == 1
    assert r.stdout.strip() == helpers.stale_message("tool", manifest)
    stamp(p["dep"], 600)
    p["exe"].unlink()
    r = run_ps(manifest, "tool")
    assert r.returncode == 1 and "stale helper: tool (not built); rebuild with build-it.bat" in r.stdout
    p["exe"].write_bytes(b"MZ")
    stamp(p["exe"], 1000)
    assert run_ps(manifest, env_force=True).returncode == 1


@needs_powershell
def test_build_side_check_reports_a_bad_manifest_as_exit_2(tree):
    manifest, _ = tree
    doc = json.loads(manifest.read_text())
    doc["helpers"]["tool"]["inputs"].append("src/deleted.cpp")
    manifest.write_text(json.dumps(doc))
    r = run_ps(manifest)
    assert r.returncode == 2 and "deleted.cpp" in r.stdout
    assert run_ps(manifest, "nosuch").returncode == 2


@needs_powershell
def test_real_helpers_are_checked_by_the_build_side_script_without_error():
    # exit 0 (fresh) or 1 (stale/not built) are both valid answers; 2 means the manifest is broken.
    assert run_ps(helpers.MANIFEST).returncode in (0, 1)
