"""Bug-report-grade diagnostics: layout, schema, redaction, retention, failure.

These tests are the contract. The Godot sim and the WPF launcher write into the
same tree, so anything asserted here is a cross-component promise, not an
importer implementation detail.
"""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import tempfile
from typing import Any
import unittest
from unittest.mock import patch

from openbfme_importer import diagnostics
from openbfme_importer.diagnostics import (
    COMPONENTS,
    DIAGNOSTICS_ENV,
    LOG_ROOT_ENV,
    MAX_RUNS,
    RUN_DIR_PATTERN,
    DiagnosticsRun,
    default_log_root,
    diagnostics_enabled,
    redact,
    redact_text,
    start_run,
)


RUN_DIR_RE = re.compile(RUN_DIR_PATTERN)


class _LogRootCase(unittest.TestCase):
    """Every test gets a private log root; none may touch the real one."""

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name) / "logs"
        self._env = patch.dict(
            os.environ,
            {LOG_ROOT_ENV: str(self.root), DIAGNOSTICS_ENV: "1"},
            clear=False,
        )
        self._env.start()
        self.addCleanup(self._env.stop)
        self.addCleanup(self._tmp.cleanup)

    def _run_dirs(self) -> list[Path]:
        if not self.root.is_dir():
            return []
        return sorted(
            (item for item in self.root.iterdir() if item.is_dir()),
            key=lambda item: item.name,
        )


class RunDirectoryLayoutTests(_LogRootCase):
    def test_run_directory_name_and_files_match_the_shared_contract(self) -> None:
        run = start_run("importer", argv=["openbfme-import", "doctor"])
        self.addCleanup(run.close)
        self.assertTrue(run.enabled)
        directory = run.directory
        assert directory is not None
        self.assertEqual(directory.parent, self.root.resolve())

        match = RUN_DIR_RE.fullmatch(directory.name)
        self.assertIsNotNone(match, f"run dir name off-contract: {directory.name}")
        assert match is not None
        self.assertEqual(match.group("component"), "importer")
        self.assertEqual(int(match.group("pid")), os.getpid())

        run.event("smoke", detail="hello")
        run.close()

        for name in ("run.log", "run.jsonl", "env.json", "identity.json"):
            self.assertTrue((directory / name).is_file(), f"missing {name}")
        # No failure happened, so there must be no error.txt.
        self.assertFalse((directory / "error.txt").exists())

    def test_component_name_is_restricted_to_the_shared_vocabulary(self) -> None:
        self.assertEqual(COMPONENTS, ("importer", "converter", "sim", "launcher"))
        with self.assertRaises(ValueError):
            start_run("nonsense")

    def test_default_log_root_follows_localappdata_and_the_override(self) -> None:
        self.assertEqual(default_log_root(), self.root.resolve())
        with patch.dict(os.environ, {LOG_ROOT_ENV: ""}, clear=False):
            with patch.dict(
                os.environ, {"LOCALAPPDATA": str(self.root / "lad")}, clear=False
            ):
                self.assertEqual(
                    default_log_root(),
                    (self.root / "lad" / "OpenBFME" / "logs").resolve(),
                )


class JsonlSchemaTests(_LogRootCase):
    def test_every_event_carries_exactly_the_contract_keys(self) -> None:
        run = start_run("converter", argv=["openbfme-import", "build"])
        run.event("plain")
        run.event("with-fields", level="warning", count=3, chosen="ffmpeg")
        with run.phase("extract", profile="men-fords-v0") as phase:
            phase.count(files=7)
        run.decision(
            "tool",
            chosen="C:/pinned/ffmpeg.exe",
            reason="pinned under state root",
            candidates=["C:/pinned/ffmpeg.exe", "PATH:ffmpeg"],
        )
        run.close()
        directory = run.directory
        assert directory is not None

        lines = (directory / "run.jsonl").read_text(encoding="utf-8").splitlines()
        self.assertGreaterEqual(len(lines), 6)
        events: list[str] = []
        for line in lines:
            event = json.loads(line)
            self.assertEqual(
                set(event), {"ts", "level", "component", "event", "fields"}
            )
            self.assertEqual(event["component"], "converter")
            self.assertIn(
                event["level"], {"debug", "info", "warning", "error", "critical"}
            )
            self.assertRegex(
                event["ts"], r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$"
            )
            self.assertIsInstance(event["fields"], dict)
            self.assertIn("seq", event["fields"])
            events.append(event["event"])

        self.assertIn("run.begin", events)
        self.assertIn("phase.begin", events)
        self.assertIn("phase.end", events)
        self.assertIn("decision", events)
        self.assertIn("run.end", events)
        self.assertEqual(events[0], "run.begin")
        self.assertEqual(events[-1], "run.end")

        payloads = [json.loads(line) for line in lines]
        seqs = [item["fields"]["seq"] for item in payloads]
        self.assertEqual(seqs, sorted(seqs))

        phase_end = next(
            item for item in payloads if item["event"] == "phase.end"
        )
        self.assertEqual(phase_end["fields"]["phase"], "extract")
        self.assertIn("duration_s", phase_end["fields"])
        self.assertEqual(phase_end["fields"]["counts"], {"files": 7})
        self.assertEqual(phase_end["fields"]["outcome"], "ok")

        decision = next(item for item in payloads if item["event"] == "decision")
        self.assertEqual(decision["fields"]["kind"], "tool")
        self.assertIn("candidates", decision["fields"])

    def test_human_log_lines_are_timestamped_and_name_the_event(self) -> None:
        run = start_run("importer")
        run.event("smoke", detail="hello")
        run.close()
        directory = run.directory
        assert directory is not None
        text = (directory / "run.log").read_text(encoding="utf-8")
        self.assertRegex(
            text,
            r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z\s+INFO\s+importer\s+run\.begin",
        )
        self.assertIn("smoke", text)


class RedactionTests(_LogRootCase):
    # Assembled at runtime on purpose. A literal credential-shaped string in a
    # tracked file fails tools/export-scan.ps1's "credential-shaped token" rule,
    # which is exactly the rule this test exists to keep honest.
    FAKE_TOKEN = "ghp_" + "A" * 36

    def test_secrets_and_home_paths_never_reach_disk(self) -> None:
        home = str(Path.home())
        with patch.dict(
            os.environ,
            {
                "OPENBFME_FAKE_TOKEN": self.FAKE_TOKEN,
                "OPENBFME_SIGNING_KEY": "s3cret-signing-material",
                "OPENBFME_HOMEY_PATH": os.path.join(home, "Desktop", "open-bfme"),
            },
            clear=False,
        ):
            run = start_run("importer", argv=["openbfme-import", "--home", home])
            run.event(
                "leaky",
                token=self.FAKE_TOKEN,
                path=os.path.join(home, "secret-place"),
                nested={"deeper": [self.FAKE_TOKEN, home]},
            )
            run.close()

        directory = run.directory
        assert directory is not None
        for path in sorted(directory.iterdir()):
            text = path.read_text(encoding="utf-8", errors="replace")
            self.assertNotIn(self.FAKE_TOKEN, text, f"token leaked into {path.name}")
            self.assertNotIn(home, text, f"home path leaked into {path.name}")
            self.assertNotIn(
                "s3cret-signing-material", text, f"signing key leaked into {path.name}"
            )

    def test_redact_is_pure_and_recursive(self) -> None:
        home = str(Path.home())
        payload = {"a": self.FAKE_TOKEN, "b": [home, {"c": home + "\\x"}], "d": 5}
        cleaned = redact(payload)
        rendered = json.dumps(cleaned)
        self.assertNotIn(self.FAKE_TOKEN, rendered)
        self.assertNotIn(home, rendered)
        self.assertEqual(cleaned["d"], 5)
        # Original untouched.
        self.assertEqual(payload["a"], self.FAKE_TOKEN)

    def test_content_digests_survive_redaction(self) -> None:
        digest = "d44afb09a370ada6cacbc6e02b4933fb9e244b1094768bce7ae20df143e42328"
        self.assertEqual(redact(digest), digest)
        self.assertEqual(redact(f"rotwk-men-vslice/{digest}"), f"rotwk-men-vslice/{digest}")

    def test_paths_this_process_never_configured_are_still_masked(self) -> None:
        """A path can arrive from a catalog, not from this machine's env.

        `doctor` surfaced exactly this: the install root recorded inside a
        catalog reached the log as a raw retail drive path, which is one of the
        shapes tools/export-scan.ps1 fails a build over.
        """

        forbidden = {
            "retail drive": "F:\\BFME2\\game.dat",
            "other account home": "D:\\Users\\SomebodyElse\\Desktop\\thing",
            "posix home": "/home/somebodyelse/work",
            "unc share": "\\\\BUILDBOX\\packs\\rotwk",
        }
        ci_patterns = {
            "home directory": re.compile(
                r"(?i)[A-Z]:[\\/]+Users[\\/]+(?!Example\b|Public\b|Default\b|%|\$|<)"
                r"[A-Za-z0-9._-]+"
            ),
            "private retail drive": re.compile(r"(?i)\bF:[\\/]+(BFME2|ROTWK)\b"),
        }
        for label, raw in forbidden.items():
            cleaned = redact_text(raw)
            self.assertNotIn(raw, cleaned, f"{label} survived redaction")
            for name, pattern in ci_patterns.items():
                self.assertIsNone(
                    pattern.search(cleaned),
                    f"{label} still matches the CI {name} rule: {cleaned}",
                )

    def test_secret_named_environment_values_are_masked_in_env_json(self) -> None:
        with patch.dict(
            os.environ, {"OPENBFME_FAKE_TOKEN": self.FAKE_TOKEN}, clear=False
        ):
            run = start_run("importer")
            run.close()
        directory = run.directory
        assert directory is not None
        env = json.loads((directory / "env.json").read_text(encoding="utf-8"))
        self.assertIn("OPENBFME_FAKE_TOKEN", env["environment"])
        self.assertEqual(env["environment"]["OPENBFME_FAKE_TOKEN"], "<REDACTED>")


class RetentionTests(_LogRootCase):
    def _seed(self, index: int, *, error: bool = False) -> Path:
        directory = self.root / f"2020010{0}T{index:06d}Z-importer-{1000 + index}"
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "run.log").write_text("seeded\n", encoding="utf-8")
        if error:
            (directory / "error.txt").write_text("boom\n", encoding="utf-8")
        return directory

    def test_start_prunes_down_to_the_newest_runs(self) -> None:
        seeded = [self._seed(index) for index in range(1, 31)]
        run = start_run("importer")
        run.close()
        remaining = {item.name for item in self._run_dirs()}
        # The new run plus the newest MAX_RUNS - 1 seeded ones.
        self.assertEqual(len(remaining), MAX_RUNS)
        assert run.directory is not None
        self.assertIn(run.directory.name, remaining)
        for stale in seeded[:5]:
            self.assertNotIn(stale.name, remaining)
        for fresh in seeded[-5:]:
            self.assertIn(fresh.name, remaining)

    def test_failure_runs_are_kept_past_the_normal_window(self) -> None:
        keeper = self._seed(1, error=True)
        for index in range(2, 31):
            self._seed(index)
        run = start_run("importer")
        run.close()
        remaining = {item.name for item in self._run_dirs()}
        self.assertIn(keeper.name, remaining, "error.txt run was pruned")

    def test_failure_runs_are_dropped_beyond_the_hard_ceiling(self) -> None:
        keeper = self._seed(1, error=True)
        for index in range(2, 130):
            self._seed(index)
        run = start_run("importer")
        run.close()
        remaining = {item.name for item in self._run_dirs()}
        self.assertNotIn(keeper.name, remaining)

    def test_collision_suffixed_runs_are_prunable(self) -> None:
        """Two runs in one second share a name; the suffixed one must age out."""

        first = start_run("importer")
        second = start_run("importer")
        first.close()
        second.close()
        assert first.directory is not None and second.directory is not None
        self.assertNotEqual(first.directory, second.directory)
        self.assertTrue(RUN_DIR_RE.fullmatch(second.directory.name))
        for index in range(1, 31):
            self._seed(index)
        third = start_run("importer")
        third.close()
        remaining = {item.name for item in self._run_dirs()}
        self.assertEqual(len(remaining), MAX_RUNS)

    def test_unrelated_directories_are_never_pruned(self) -> None:
        bystander = self.root / "not-a-run-dir"
        bystander.mkdir(parents=True)
        for index in range(1, 31):
            self._seed(index)
        run = start_run("importer")
        run.close()
        self.assertTrue(bystander.is_dir())


class FailureCaptureTests(_LogRootCase):
    def test_error_txt_records_traceback_and_recent_lines(self) -> None:
        run = start_run("converter")
        run.event("breadcrumb-one")
        run.event("breadcrumb-two")
        try:
            raise RuntimeError("forced diagnostic failure")
        except RuntimeError as exc:
            run.failure(exc, context={"phase": "cook"})
        run.close()
        directory = run.directory
        assert directory is not None
        text = (directory / "error.txt").read_text(encoding="utf-8")
        self.assertIn("RuntimeError: forced diagnostic failure", text)
        self.assertIn("Traceback (most recent call last)", text)
        self.assertIn("breadcrumb-two", text)
        self.assertIn("cook", text)

        events = [
            json.loads(line)
            for line in (directory / "run.jsonl").read_text(encoding="utf-8").splitlines()
        ]
        failure = next(item for item in events if item["event"] == "failure")
        self.assertEqual(failure["level"], "error")
        self.assertEqual(failure["fields"]["exception_type"], "RuntimeError")
        run_end = events[-1]
        self.assertEqual(run_end["event"], "run.end")
        self.assertEqual(run_end["fields"]["outcome"], "failed")

    def test_phase_context_manager_records_the_exception_and_reraises(self) -> None:
        run = start_run("converter")
        with self.assertRaises(ValueError):
            with run.phase("convert-assets"):
                raise ValueError("phase blew up")
        run.close()
        directory = run.directory
        assert directory is not None
        events = [
            json.loads(line)
            for line in (directory / "run.jsonl").read_text(encoding="utf-8").splitlines()
        ]
        phase_end = next(item for item in events if item["event"] == "phase.end")
        self.assertEqual(phase_end["fields"]["outcome"], "failed")
        self.assertEqual(phase_end["level"], "error")
        self.assertTrue((directory / "error.txt").is_file())


class DisabledByDefaultTests(_LogRootCase):
    def test_diagnostics_off_creates_nothing_and_no_ops(self) -> None:
        with patch.dict(os.environ, {DIAGNOSTICS_ENV: "0"}, clear=False):
            self.assertFalse(diagnostics_enabled())
            run = start_run("importer", enabled=None)
            self.assertFalse(run.enabled)
            self.assertIsNone(run.directory)
            run.event("ignored", detail="nothing")
            with run.phase("extract"):
                pass
            run.decision("tool", chosen="x", reason="y")
            run.failure(RuntimeError("also ignored"))
            run.close()
        self.assertFalse(self.root.exists(), "disabled diagnostics touched the disk")

    def test_environment_flag_and_explicit_flag_both_enable(self) -> None:
        with patch.dict(os.environ, {DIAGNOSTICS_ENV: "1"}, clear=False):
            self.assertTrue(diagnostics_enabled())
        for value in ("true", "yes", "on", "1"):
            with patch.dict(os.environ, {DIAGNOSTICS_ENV: value}, clear=False):
                self.assertTrue(diagnostics_enabled())
        for value in ("", "0", "false", "no", "off"):
            with patch.dict(os.environ, {DIAGNOSTICS_ENV: value}, clear=False):
                self.assertFalse(diagnostics_enabled())
        with patch.dict(os.environ, {DIAGNOSTICS_ENV: "0"}, clear=False):
            run = start_run("importer", enabled=True)
            self.addCleanup(run.close)
            self.assertTrue(run.enabled)

    def test_active_run_is_a_disabled_singleton_until_one_starts(self) -> None:
        diagnostics.set_active_run(None)
        idle = diagnostics.active_run()
        self.assertFalse(idle.enabled)
        idle.event("safe-no-op")
        run = start_run("importer")
        self.addCleanup(run.close)
        diagnostics.set_active_run(run)
        self.assertIs(diagnostics.active_run(), run)
        diagnostics.set_active_run(None)


class FailClosedTests(_LogRootCase):
    def test_unusable_log_root_warns_loudly_and_never_raises(self) -> None:
        blocker = self.root
        blocker.parent.mkdir(parents=True, exist_ok=True)
        blocker.write_text("not a directory\n", encoding="utf-8")
        with patch("sys.stderr") as stderr:
            run = start_run("importer")
        self.assertFalse(run.enabled)
        self.assertIsNone(run.directory)
        written = "".join(
            str(call.args[0]) for call in stderr.write.call_args_list if call.args
        )
        self.assertIn("[diagnostics]", written)
        # Work continues regardless.
        run.event("still-fine")
        run.close()

    def test_write_failures_mid_run_do_not_propagate(self) -> None:
        run = start_run("importer")
        self.addCleanup(run.close)
        with patch.object(
            DiagnosticsRun, "_append", side_effect=OSError("disk full")
        ):
            run.event("swallowed")
        run.event("recovered")


class IdentityTests(_LogRootCase):
    def test_identity_reports_the_selection_it_actually_read(self) -> None:
        content_root = Path(self._tmp.name) / "content-packs"
        content_root.mkdir(parents=True)
        selection = {
            "activePack": "rotwk-men-vslice/" + "a" * 64,
            "schema": "openbfme.pack-selection",
            "schemaVersion": 0,
            "supplementalPacks": ["rotwk-elves-vslice/" + "b" * 64],
        }
        (content_root / "selection.json").write_text(
            json.dumps(selection), encoding="utf-8"
        )
        (content_root / "rotwk-men-vslice" / ("a" * 64)).mkdir(parents=True)

        run = start_run("importer", content_root=content_root)
        run.close()
        directory = run.directory
        assert directory is not None
        payload = json.loads((directory / "identity.json").read_text(encoding="utf-8"))
        self.assertTrue(payload["resolved"])
        self.assertEqual(len(payload["selection_sha256"]), 64)
        self.assertEqual(payload["active_pack"]["id"], "rotwk-men-vslice")
        self.assertEqual(payload["active_pack"]["digest"], "a" * 64)
        self.assertTrue(payload["active_pack"]["present"])
        self.assertEqual(len(payload["supplemental_packs"]), 1)
        self.assertFalse(payload["supplemental_packs"][0]["present"])

    def test_a_disabled_run_never_reads_the_selection(self) -> None:
        """Identity collection is real I/O; the default path must not pay it."""

        with patch.dict(os.environ, {DIAGNOSTICS_ENV: "0"}, clear=False):
            run = start_run("importer", enabled=None)
        with patch(
            "openbfme_importer.diagnostics.collect_identity"
        ) as collect:
            self.assertEqual(run.record_identity(None), {})
        collect.assert_not_called()

    def test_missing_selection_is_reported_not_guessed(self) -> None:
        content_root = Path(self._tmp.name) / "empty-content"
        content_root.mkdir(parents=True)
        run = start_run("importer", content_root=content_root)
        run.close()
        directory = run.directory
        assert directory is not None
        payload = json.loads((directory / "identity.json").read_text(encoding="utf-8"))
        self.assertFalse(payload["resolved"])
        self.assertIn("reason", payload)
        self.assertIsNone(payload["active_pack"])


class CliWiringTests(_LogRootCase):
    def test_cli_exposes_a_global_diagnostics_flag(self) -> None:
        from openbfme_importer.cli import build_parser

        parser = build_parser()
        args = parser.parse_args(["--diagnostics", "doctor", "--install", "X"])
        self.assertTrue(args.diagnostics)
        args = parser.parse_args(["doctor", "--install", "X"])
        self.assertFalse(args.diagnostics)

    def test_component_follows_the_command(self) -> None:
        from openbfme_importer.diagnostics import component_for_command

        self.assertEqual(component_for_command("build"), "converter")
        self.assertEqual(component_for_command("import-faction"), "converter")
        self.assertEqual(component_for_command("doctor"), "importer")
        self.assertEqual(component_for_command("audit"), "importer")


class ToolDiscoveryLoggingTests(_LogRootCase):
    """The recorded failure class: a PATH binary standing in for a pinned one."""

    def _events(self, run: DiagnosticsRun) -> list[dict[str, Any]]:
        directory = run.directory
        assert directory is not None
        return [
            json.loads(line)
            for line in (directory / "run.jsonl").read_text(encoding="utf-8").splitlines()
        ]

    def test_missing_tool_is_recorded_rather_than_returned_silently(self) -> None:
        from openbfme_importer.tools import discover_executable

        run = start_run("converter")
        self.addCleanup(run.close)
        with patch("shutil.which", return_value=None):
            self.assertIsNone(
                discover_executable("definitely-not-a-real-tool", "OPENBFME_NOPE")
            )
        decision = next(
            item
            for item in self._events(run)
            if item["event"] == "decision"
            and item["fields"].get("tool") == "definitely-not-a-real-tool"
        )
        self.assertEqual(decision["fields"]["source"], "missing")
        self.assertIsNone(decision["fields"]["chosen"])

    def test_path_fallback_for_a_pinned_tool_is_logged_as_a_warning(self) -> None:
        from openbfme_importer import tools as tools_module

        run = start_run("converter")
        self.addCleanup(run.close)
        fake = Path(self._tmp.name) / "path-ffmpeg" / "ffmpeg.exe"
        fake.parent.mkdir(parents=True, exist_ok=True)
        fake.write_bytes(b"not really ffmpeg")
        empty_state = Path(self._tmp.name) / "no-tools"
        with patch.dict(
            os.environ, {"OPENBFME_IMPORT_ROOT": str(empty_state)}, clear=False
        ):
            with patch.object(tools_module.shutil, "which", return_value=str(fake)):
                found = tools_module.discover_executable("ffmpeg", "OPENBFME_FFMPEG")
        self.assertIsNotNone(found)
        decision = next(
            item
            for item in self._events(run)
            if item["event"] == "decision" and item["fields"].get("tool") == "ffmpeg"
        )
        self.assertEqual(decision["fields"]["source"], "path")
        self.assertEqual(decision["level"], "warning")
        self.assertTrue(decision["fields"]["pinned_expected"])

    def test_environment_override_is_named_as_such(self) -> None:
        from openbfme_importer.tools import discover_executable

        run = start_run("converter")
        self.addCleanup(run.close)
        fake = Path(self._tmp.name) / "override" / "ffmpeg.exe"
        fake.parent.mkdir(parents=True, exist_ok=True)
        fake.write_bytes(b"override")
        with patch.dict(os.environ, {"OPENBFME_FFMPEG": str(fake)}, clear=False):
            discover_executable("ffmpeg", "OPENBFME_FFMPEG")
        decision = next(
            item
            for item in self._events(run)
            if item["event"] == "decision" and item["fields"].get("tool") == "ffmpeg"
        )
        self.assertEqual(decision["fields"]["source"], "environment")


class CliEndToEndTests(_LogRootCase):
    def test_a_failing_command_leaves_a_complete_run_directory(self) -> None:
        from openbfme_importer.cli import main

        missing = Path(self._tmp.name) / "no-such-install"
        code = main(["--diagnostics", "--json", "index", "--install", str(missing)])
        self.assertEqual(code, 1)

        runs = self._run_dirs()
        self.assertEqual(len(runs), 1)
        directory = runs[0]
        self.assertIn("-importer-", directory.name)
        for name in ("run.log", "run.jsonl", "env.json", "identity.json", "error.txt"):
            self.assertTrue((directory / name).is_file(), f"missing {name}")

        events = [
            json.loads(line)
            for line in (directory / "run.jsonl").read_text(encoding="utf-8").splitlines()
        ]
        self.assertEqual(events[0]["event"], "run.begin")
        self.assertEqual(events[0]["fields"]["command"], "index")
        self.assertEqual(events[-1]["event"], "run.end")
        self.assertEqual(events[-1]["fields"]["outcome"], "failed")
        self.assertEqual(events[-1]["fields"]["exit_code"], 1)
        self.assertTrue(any(item["event"] == "failure" for item in events))

    def test_without_the_flag_no_run_directory_appears(self) -> None:
        from openbfme_importer.cli import main

        missing = Path(self._tmp.name) / "no-such-install"
        with patch.dict(os.environ, {DIAGNOSTICS_ENV: "0"}, clear=False):
            code = main(["--json", "index", "--install", str(missing)])
        self.assertEqual(code, 1)
        self.assertFalse(self.root.exists(), "a default run wrote diagnostics")


if __name__ == "__main__":
    unittest.main()
