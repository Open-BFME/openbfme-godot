#!/usr/bin/env python3
"""Lane GHPR-1: the Python tests the public pull request check runs (.github/workflows/pr.yml, CONTRIBUTING.md "What we check").

SUBSET is the test files that pass on a clean GitHub runner: Python, pytest and PyYAML (PUBLICCI-1; no capstone / pefile), no game folders, no
built engine, no Godot. It was measured by running every test file of the public tree that way (GHPR-1, 2026-10-18: 56 files of 89
passed with no failure; the rest need capstone / pefile, the game files or a build, or only skip there, and the full gate runs them all).
The operations-only tests under tools/net are left out (they leave the public tree with PUBLISH-2). Fails closed: a listed file that does not exist, a pytest failure, or a
listed file in which no test passed (it collected nothing or only skipped) is a failure.

    python3 tools/release/pr_pytest.py [pytest args]
"""
from __future__ import annotations

import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SUBSET = [
    "tools/audio/test_miles_model.py",
    "tools/audio/test_playback_bus_guard.py",
    "tools/fx/test_fx_ini_scan.py",
    "tools/horde_oracle/test_dump_tables.py",
    "tools/horde_oracle/test_rng_reference.py",
    "tools/hover/test_hover_inventory.py",
    "tools/maps/oracle/test_census.py",
    "tools/maps/oracle/test_resolve.py",
    "tools/menus/test_menu_inventory.py",
    "tools/move/test_jitter_report.py",
    "tools/net/test_cross_os_lockstep.py",
    "tools/net/test_mp3_perf_table.py",
    "tools/net/test_online_client.py",
    "tools/net/test_user_agents.py",
    "tools/object_oracle/test_object_oracle.py",
    "tools/opts/test_opts1_hotkeys.py",
    "tools/opts/test_ping_wheel_keys.py",
    "tools/path/test_no_libm.py",
    "tools/path/test_path_oracle.py",
    "tools/perf3/test_render_frame_no_wait.py",
    "tools/perf3/test_renderwait1_per_frame.py",
    "tools/platform_fake/test_platform_contract.py",
    "tools/platformicon/test_make_icons.py",
    "tools/qa/test_qa_matrix.py",
    "tools/qa/test_scenario_players.py",
    "tools/release/test_actions_build.py",
    "tools/release/test_aio.py",
    "tools/release/test_autorelease.py",
    "tools/release/test_big_entries.py",
    "tools/release/test_community_prs.py",
    "tools/release/test_public_push_guard.py",
    "tools/release/test_export_checks.py",
    "tools/release/test_launcher_tools.py",
    "tools/release/test_net_guard_files.py",
    "tools/release/test_pdb_tools.py",
    "tools/release/test_release_tools.py",
    "tools/release/test_windows_player_walk.py",
    "tools/release/test_wine_suite.py",
    "tools/render/test_d3d9_disasm.py",
    "tools/render/test_sharpness.py",
    "tools/replay/test_rotwk_replay.py",
    "tools/retail_oracle/test_helpers.py",
    "tools/rw_object_model/test_module_coverage.py",
    "tools/rw_object_model/test_rw_object_model.py",
    "tools/save/test_closed_sets.py",
    "tools/save/test_retail_save.py",
    "tools/script/test_script_tools.py",
    "tools/sim/test_float_chars.py",
    "tools/sim/test_sim_audit.py",
    "tools/skirmish_ai/test_tactic_draws.py",
    "tools/test_precommit_containers.py",
    "tools/test_precommit_tolerance.py",
    "tools/test_stops_count.py",
    "tools/weapon/test_weapon_binary_facts.py",
    "tools/weapon/test_weapon_dump_tables.py",
    "tools/weapon/test_weapon_no_libm.py",
]
# single tests of listed files that need more than a pull request's checkout has: {node id: why}
DESELECT = {
    "tools/release/test_autorelease.py::test_bridge_rules": "reads published launchers from release tags (a shallow checkout has no tags)",
}


def main(argv: list[str]) -> int:
    missing = [f for f in SUBSET if not (REPO / f).is_file()]
    if missing or not SUBSET:
        print(f"PR PYTEST FAILED: listed test files missing: {', '.join(missing) or '(the list is empty)'}")
        return 1
    with tempfile.TemporaryDirectory() as tmp:
        junit = Path(tmp, "junit.xml")
        rc = subprocess.run([sys.executable, "-m", "pytest", "-q", "-p", "no:cacheprovider", "-o", "junit_family=xunit1",
                             f"--junitxml={junit}", *(f"--deselect={d}" for d in DESELECT), *argv, *SUBSET], cwd=REPO).returncode
        try:
            root = ET.parse(junit).getroot()
        except (OSError, ET.ParseError):
            print("PR PYTEST FAILED: pytest wrote no results")
            return 1
    cases = list(root.iter("testcase"))
    passed = [c for c in cases if not any(c.find(k) is not None for k in ("failure", "error", "skipped"))]
    # every listed file ran at least one test that passed (a file that collects nothing, or only skips, is not checking anything)
    idle = [f for f in SUBSET if not any(c.get("file") == f for c in passed)]
    print(f"PR PYTEST: {len(passed)} of {len(cases)} tests passed in {len(SUBSET)} files (pytest exit {rc})")
    for f in idle:
        print(f"PR PYTEST: no test of {f} passed")
    if rc != 0 or idle:
        print("PR PYTEST FAILED")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
