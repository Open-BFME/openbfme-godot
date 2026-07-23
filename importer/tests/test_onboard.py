"""Unit tests for the pure logic in tools/onboard.py (no real installs)."""

from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import onboard  # noqa: E402


class TestArgumentParsing(unittest.TestCase):
    def test_non_interactive_flags(self) -> None:
        args = onboard.parse_args(
            ["--install", r"F:\BFME2", "--godot", r"C:\godot\godot.exe", "--yes"]
        )
        self.assertEqual(args.install, Path(r"F:\BFME2"))
        self.assertEqual(args.godot, Path(r"C:\godot\godot.exe"))
        self.assertTrue(args.yes)
        self.assertFalse(args.skip_gates)
        self.assertFalse(args.force_convert)

    def test_defaults(self) -> None:
        args = onboard.parse_args([])
        self.assertIsNone(args.install)
        self.assertIsNone(args.rotwk)
        self.assertIsNone(args.godot)
        self.assertFalse(args.yes)
        self.assertEqual(args.config, onboard.DEFAULT_CONFIG_PATH)
        self.assertIsNone(args.state_root)
        self.assertIsNone(args.content_root)

    def test_optional_rotwk_and_overrides(self) -> None:
        args = onboard.parse_args(
            [
                "--rotwk",
                r"F:\RotWK",
                "--state-root",
                r"D:\state",
                "--content-root",
                r"D:\packs",
                "--skip-gates",
                "--force-convert",
            ]
        )
        self.assertEqual(args.rotwk, Path(r"F:\RotWK"))
        self.assertEqual(args.state_root, Path(r"D:\state"))
        self.assertEqual(args.content_root, Path(r"D:\packs"))
        self.assertTrue(args.skip_gates)
        self.assertTrue(args.force_convert)


class TestConfigRoundTrip(unittest.TestCase):
    def test_missing_config_is_empty(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(onboard.load_config(Path(tmp) / "missing.json"), {})

    def test_round_trip_preserves_values_and_stamps_schema(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "onboard.config.json"
            onboard.save_config(
                path,
                {"godotExe": r"C:\godot\godot.exe", "bfme2Install": r"F:\BFME2"},
            )
            loaded = onboard.load_config(path)
            self.assertEqual(loaded["godotExe"], r"C:\godot\godot.exe")
            self.assertEqual(loaded["bfme2Install"], r"F:\BFME2")
            self.assertEqual(loaded["schema"], onboard.CONFIG_SCHEMA)
            self.assertEqual(loaded["schemaVersion"], onboard.CONFIG_VERSION)
            # No stray staging file left behind.
            self.assertEqual(
                [p.name for p in Path(tmp).iterdir()], ["onboard.config.json"]
            )

    def test_corrupt_config_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "onboard.config.json"
            path.write_text("{not json", encoding="utf-8")
            with self.assertRaises(onboard.OnboardError):
                onboard.load_config(path)

    def test_non_object_config_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "onboard.config.json"
            path.write_text(json.dumps(["not", "an", "object"]), encoding="utf-8")
            with self.assertRaises(onboard.OnboardError):
                onboard.load_config(path)


class TestInstallClassification(unittest.TestCase):
    def test_bfme2_install(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "lotrbfme2.exe").write_bytes(b"MZ")
            self.assertEqual(onboard.classify_install(Path(tmp)), "bfme2")

    def test_rotwk_install_wins_when_both_markers_present(self) -> None:
        # A RotWK install directory also carries base-game files; the
        # expansion executable is the distinguishing marker.
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "lotrbfme2.exe").write_bytes(b"MZ")
            (Path(tmp) / "lotrbfme2ep1.exe").write_bytes(b"MZ")
            self.assertEqual(onboard.classify_install(Path(tmp)), "rotwk")

    def test_unrecognized_directory(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            self.assertIsNone(onboard.classify_install(Path(tmp)))

    def test_missing_directory(self) -> None:
        self.assertIsNone(
            onboard.classify_install(Path(r"C:\does\not\exist\anywhere"))
        )


class TestPrerequisites(unittest.TestCase):
    def _fake_env(self, tmp: Path) -> tuple[Path, Path]:
        python_env = tmp / "tools" / "python-3.12-env" / "Scripts" / "python.exe"
        python_env.parent.mkdir(parents=True)
        python_env.write_bytes(b"MZ")
        godot = tmp / "Godot_v4.7-stable_win64.exe"
        godot.write_bytes(b"MZ")
        return python_env, godot

    def test_all_present(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            tmp = Path(raw)
            python_env, godot = self._fake_env(tmp)
            results = onboard.evaluate_prerequisites(
                python_env=python_env,
                godot_exe=godot,
                git_path=r"C:\Program Files\Git\cmd\git.exe",
            )
            self.assertTrue(onboard.prerequisites_ready(results))

    def test_missing_python_env_blocks(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            tmp = Path(raw)
            _, godot = self._fake_env(tmp)
            results = onboard.evaluate_prerequisites(
                python_env=tmp / "nope" / "python.exe",
                godot_exe=godot,
                git_path="git",
            )
            self.assertFalse(onboard.prerequisites_ready(results))
            failing = [r for r in results if not r.ok]
            self.assertEqual([r.name for r in failing], ["importer Python env"])
            self.assertTrue(failing[0].fix)

    def test_missing_godot_and_git_block(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            tmp = Path(raw)
            python_env, _ = self._fake_env(tmp)
            results = onboard.evaluate_prerequisites(
                python_env=python_env,
                godot_exe=None,
                git_path=None,
            )
            failing = {r.name for r in results if not r.ok}
            self.assertEqual(failing, {"Godot 4.7 executable", "git"})

    def test_importer_python_path_shape(self) -> None:
        root = Path(r"D:\state")
        self.assertEqual(
            onboard.importer_python(root),
            root / "tools" / "python-3.12-env" / "Scripts" / "python.exe",
        )


class TestGodotConsoleResolution(unittest.TestCase):
    def test_prefers_console_sibling(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            tmp = Path(raw)
            gui = tmp / "Godot_v4.7-stable_win64.exe"
            console = tmp / "Godot_v4.7-stable_win64_console.exe"
            gui.write_bytes(b"MZ")
            console.write_bytes(b"MZ")
            self.assertEqual(onboard.resolve_godot_console(gui), console)

    def test_keeps_console_exe(self) -> None:
        path = Path(r"C:\godot\Godot_v4.7-stable_win64_console.exe")
        self.assertEqual(onboard.resolve_godot_console(path), path)

    def test_no_sibling_keeps_original(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            gui = Path(raw) / "Godot_v4.7-stable_win64.exe"
            gui.write_bytes(b"MZ")
            self.assertEqual(onboard.resolve_godot_console(gui), gui)


class TestPackPlanning(unittest.TestCase):
    def _selection(self, active: str) -> dict:
        return {
            "activePack": active,
            "schema": "openbfme.pack-selection",
            "schemaVersion": 0,
        }

    def test_verify_path_when_bundle_present(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            content = Path(raw)
            bundle = "a" * 64
            pack_root = content / onboard.MEN_PACK_ID / bundle
            pack_root.mkdir(parents=True)
            (pack_root / "pack.json").write_text("{}", encoding="utf-8")
            plan = onboard.plan_pack_step(
                self._selection(f"{onboard.MEN_PACK_ID}/{bundle}"), content
            )
            self.assertEqual(plan.action, "verify")
            self.assertEqual(plan.pack_root, pack_root)

    def test_convert_when_no_selection(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            plan = onboard.plan_pack_step(None, Path(raw))
            self.assertEqual(plan.action, "convert")
            self.assertIn("selection.json", plan.reason)

    def test_convert_when_bundle_missing_on_disk(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            plan = onboard.plan_pack_step(
                self._selection(f"{onboard.MEN_PACK_ID}/{'b' * 64}"), Path(raw)
            )
            self.assertEqual(plan.action, "convert")
            self.assertIn("missing on disk", plan.reason)

    def test_convert_when_active_pack_malformed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            plan = onboard.plan_pack_step({"activePack": 42}, Path(raw))
            self.assertEqual(plan.action, "convert")

    def test_convert_when_wrong_host_pack(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            plan = onboard.plan_pack_step(
                self._selection(f"some-other-pack/{'c' * 64}"), Path(raw)
            )
            self.assertEqual(plan.action, "convert")
            self.assertIn("some-other-pack", plan.reason)

    def test_force_convert_overrides_verify(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            content = Path(raw)
            bundle = "d" * 64
            pack_root = content / onboard.MEN_PACK_ID / bundle
            pack_root.mkdir(parents=True)
            (pack_root / "pack.json").write_text("{}", encoding="utf-8")
            plan = onboard.plan_pack_step(
                self._selection(f"{onboard.MEN_PACK_ID}/{bundle}"),
                content,
                force_convert=True,
            )
            self.assertEqual(plan.action, "convert")


class TestGateParsing(unittest.TestCase):
    SLICE_PATTERN = onboard.GATES[0]["result_pattern"]
    MENU_PATTERN = onboard.GATES[1]["result_pattern"]

    def test_parses_slice_result_and_signature(self) -> None:
        output = (
            "loading...\n"
            "RETAIL_SLICE_SIGNATURE 3CB9CA98\n"
            "RETAIL_SLICE_RESULT passed=341 failed=0\n"
        )
        outcome = onboard.parse_gate_output(
            "retail_slice_runner", output, self.SLICE_PATTERN
        )
        self.assertTrue(outcome.ran)
        self.assertTrue(outcome.ok)
        self.assertEqual(outcome.passed, 341)
        self.assertEqual(outcome.failed, 0)
        self.assertEqual(outcome.signature, "3CB9CA98")

    def test_parses_menu_result(self) -> None:
        output = "MENU_SKIRMISH_RESULT passed=74 failed=0\n"
        outcome = onboard.parse_gate_output(
            "menu_skirmish_runner", output, self.MENU_PATTERN
        )
        self.assertTrue(outcome.ok)
        self.assertEqual(outcome.passed, 74)
        self.assertEqual(outcome.signature, "")

    def test_failed_checks_fail_the_gate(self) -> None:
        output = "RETAIL_SLICE_RESULT passed=300 failed=2\n"
        outcome = onboard.parse_gate_output(
            "retail_slice_runner", output, self.SLICE_PATTERN
        )
        self.assertTrue(outcome.ran)
        self.assertFalse(outcome.ok)

    def test_missing_result_line_fails_closed(self) -> None:
        outcome = onboard.parse_gate_output(
            "retail_slice_runner", "SCRIPT ERROR: kaboom\n", self.SLICE_PATTERN
        )
        self.assertFalse(outcome.ran)
        self.assertFalse(outcome.ok)
        self.assertIn("no recognizable result line", outcome.detail)

    def test_zero_passed_is_not_a_pass(self) -> None:
        outcome = onboard.parse_gate_output(
            "menu_skirmish_runner",
            "MENU_SKIRMISH_RESULT passed=0 failed=0\n",
            self.MENU_PATTERN,
        )
        self.assertTrue(outcome.ran)
        self.assertFalse(outcome.ok)

    def test_summary_aggregation(self) -> None:
        good = onboard.GateOutcome(
            name="retail_slice_runner", ran=True, passed=341, failed=0,
            signature="3CB9CA98",
        )
        bad = onboard.GateOutcome(name="menu_skirmish_runner", ran=False,
                                  detail="crashed")
        ok, lines = onboard.summarize_gates([good, bad])
        self.assertFalse(ok)
        self.assertEqual(len(lines), 2)
        self.assertIn("PASS retail_slice_runner", lines[0])
        self.assertIn("signature=3CB9CA98", lines[0])
        self.assertIn("FAIL menu_skirmish_runner", lines[1])

    def test_summary_all_green(self) -> None:
        outcomes = [
            onboard.GateOutcome(name="a", ran=True, passed=10, failed=0),
            onboard.GateOutcome(name="b", ran=True, passed=5, failed=0),
        ]
        ok, lines = onboard.summarize_gates(outcomes)
        self.assertTrue(ok)
        self.assertTrue(all(line.strip().startswith("PASS") for line in lines))

    def test_empty_outcomes_are_not_a_pass(self) -> None:
        ok, lines = onboard.summarize_gates([])
        self.assertFalse(ok)
        self.assertEqual(lines, [])


if __name__ == "__main__":
    unittest.main()
