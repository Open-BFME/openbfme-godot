"""CLI-level proof that the publish gates are actually wired into ``main()``.

``test_publish_gate.py`` exercises the gate *helpers*. That is necessary but
not sufficient: deleting the enforcement blocks from
``cli.publish-faction-to-slice`` left every one of those 14 tests green,
because none of them ever called ``cli.main()``. These tests drive the real
entry point and assert on the real exit code (``7``), so removing an
enforcement block turns them red.

They also pin the three properties the round-10 review found missing:

* the ``build`` command runs the same regression gate it used to bypass,
* the coverage report is *bound* to the content being cooked, so a stale but
  internally clean report cannot authorise a publication, and
* the regression check is name-level, so swapping unit ids out is caught even
  when the count is unchanged.
"""

from __future__ import annotations

import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from openbfme_importer import cli
from openbfme_importer import publish_gate


CATALOG_IDENTITY = "c" * 64
COMPILER_TOKEN = "t" * 64


def _write_coverage(
    root: Path,
    faction: str,
    *,
    complete: bool = True,
    gaps: int = 0,
    catalog_identity: str = CATALOG_IDENTITY,
    compiler_token: str | None = COMPILER_TOKEN,
) -> Path:
    path = root / f"{faction}-coverage.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    inputs: dict[str, object] = {
        "catalogIdentitySha256": catalog_identity,
        "factionGraphInputSetSha256": "f" * 64,
    }
    if compiler_token is not None:
        inputs["compilerIdentityToken"] = compiler_token
    document = {
        "schema": "openbfme.faction-import-coverage",
        "schemaVersion": 0,
        "inputs": inputs,
        "objects": [],
        "target": {"faction": faction.title(), "playerTemplate": f"Faction{faction}"},
        "summary": {
            "objectCount": 24,
            "convertedCount": 24 - gaps,
            "excludedCount": 0,
            "converterGapCount": gaps,
            "unresolvedLeafCount": 0,
            "conversionComplete": complete,
        },
    }
    path.write_text(json.dumps(document, indent=2), encoding="utf-8")
    return path


def _write_pack(root: Path, unit_ids: list[str]) -> Path:
    units = root / "data" / "playable-units"
    units.mkdir(parents=True, exist_ok=True)
    for unit_id in unit_ids:
        (units / f"{unit_id}.json").write_text(
            json.dumps({"id": unit_id}), encoding="utf-8"
        )
    return root


class _StubCatalog:
    install_root = Path("C:/RotWK")

    def identity_sha256(self) -> str:
        return CATALOG_IDENTITY


class _GateHarness(unittest.TestCase):
    """Drive ``cli.main()`` far enough to reach a gate, and no further."""

    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.state_root = self.root / "state"
        self.content_root = self.root / "content"
        self.coverage_root = self.root / "coverage"
        for path in (self.state_root, self.content_root, self.coverage_root):
            path.mkdir(parents=True, exist_ok=True)
        self.addCleanup(self._tmp.cleanup)

    def run_publish(self, *extra: str) -> tuple[int, str, str]:
        argv = [
            # --state-root is a *global* flag: it has to precede the subcommand.
            "--state-root",
            str(self.state_root),
            "publish-faction-to-slice",
            "--install",
            "C:/RotWK",
            "--faction",
            "elves",
            "--godot-content-root",
            str(self.content_root),
            "--coverage-root",
            str(self.coverage_root),
            *extra,
        ]
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.object(
            cli, "_load_or_build_catalog", return_value=_StubCatalog()
        ), mock.patch.object(
            publish_gate, "compiler_identity_token", return_value=COMPILER_TOKEN
        ), contextlib.redirect_stdout(
            out
        ), contextlib.redirect_stderr(
            err
        ):
            code = cli.main(argv)
        return code, out.getvalue(), err.getvalue()


class CoverageGateCliTests(_GateHarness):
    def test_incomplete_coverage_exits_seven(self) -> None:
        _write_coverage(self.coverage_root, "elves", complete=False, gaps=8)
        code, _out, err = self.run_publish()
        self.assertEqual(code, publish_gate.PUBLISH_GATE_EXIT)
        self.assertIn("REFUSING TO PUBLISH", err)
        self.assertIn("converterGapCount=8", err)

    def test_override_unlocks_the_coverage_gate(self) -> None:
        _write_coverage(self.coverage_root, "elves", complete=False, gaps=8)
        code, _out, err = self.run_publish("--allow-incomplete-coverage")
        # The override must not be a no-op: the command has to get *past* the
        # gate. It then dies on the missing base profile, which is exit 1, not
        # the gate exit.
        self.assertNotEqual(code, publish_gate.PUBLISH_GATE_EXIT)
        self.assertIn("--allow-incomplete-coverage", err)

    def test_missing_coverage_report_is_not_a_silent_pass(self) -> None:
        code, _out, err = self.run_publish()
        self.assertNotEqual(code, 0)
        self.assertIn("coverage", err.casefold())


class CoverageBindingCliTests(_GateHarness):
    """A stale-but-clean report must not authorise a publication."""

    def test_foreign_catalog_identity_exits_seven(self) -> None:
        _write_coverage(self.coverage_root, "elves", catalog_identity="d" * 64)
        code, _out, err = self.run_publish()
        self.assertEqual(code, publish_gate.PUBLISH_GATE_EXIT)
        self.assertIn("catalog", err.casefold())

    def test_stale_compiler_token_exits_seven(self) -> None:
        _write_coverage(self.coverage_root, "elves", compiler_token="0" * 64)
        code, _out, err = self.run_publish()
        self.assertEqual(code, publish_gate.PUBLISH_GATE_EXIT)
        self.assertIn("compiler", err.casefold())

    def test_report_without_a_compiler_token_exits_seven(self) -> None:
        # Every coverage report cooked before this gate existed. Absence is a
        # refusal, not a pass - that is what "fail closed" means here.
        _write_coverage(self.coverage_root, "elves", compiler_token=None)
        code, _out, err = self.run_publish()
        self.assertEqual(code, publish_gate.PUBLISH_GATE_EXIT)
        self.assertIn("compiler", err.casefold())

    def test_stale_binding_override_unlocks_it(self) -> None:
        _write_coverage(self.coverage_root, "elves", compiler_token="0" * 64)
        code, _out, err = self.run_publish("--allow-stale-coverage")
        self.assertNotEqual(code, publish_gate.PUBLISH_GATE_EXIT)
        self.assertIn("--allow-stale-coverage", err)

    def test_fresh_binding_passes_the_gate(self) -> None:
        _write_coverage(self.coverage_root, "elves")
        code, _out, err = self.run_publish()
        self.assertNotEqual(code, publish_gate.PUBLISH_GATE_EXIT)


class NameLevelRegressionTests(unittest.TestCase):
    """Counting units is not enough - the *names* must not change."""

    def test_swapping_a_unit_id_at_the_same_count_is_a_blocker(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            content_root = Path(raw) / "content"
            _write_pack(
                content_root / "rotwk-elves-vslice" / "aaaa",
                ["elven_archer", "elven_warrior", "legolas"],
            )
            fresh = _write_pack(
                Path(raw) / "cook",
                ["elven_archer", "elven_warrior", "glorfindel"],
            )
            blocker = publish_gate.playable_unit_regression_blocker(
                fresh, content_root, "rotwk-elves-vslice"
            )
        self.assertIsNotNone(blocker)
        assert blocker is not None
        self.assertIn("legolas", blocker)
        self.assertNotIn("glorfindel", blocker.split("missing")[0])

    def test_adding_units_is_never_a_blocker(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            content_root = Path(raw) / "content"
            _write_pack(content_root / "rotwk-elves-vslice" / "aaaa", ["a", "b"])
            fresh = _write_pack(Path(raw) / "cook", ["a", "b", "c"])
            self.assertIsNone(
                publish_gate.playable_unit_regression_blocker(
                    fresh, content_root, "rotwk-elves-vslice"
                )
            )

    def test_incumbent_names_come_from_the_richest_bundle(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            content_root = Path(raw) / "content"
            pack = content_root / "rotwk-elves-vslice"
            _write_pack(pack / "aaaa", ["a", "b", "c"])
            _write_pack(pack / "bbbb", ["a"])
            _write_pack(pack / "cccc.building", [f"u{i}" for i in range(99)])
            ids, bundle = publish_gate.incumbent_playable_unit_ids(
                content_root, "rotwk-elves-vslice"
            )
        self.assertEqual(ids, {"a", "b", "c"})
        self.assertEqual(bundle, "aaaa")


class BuildCommandGateTests(unittest.TestCase):
    """``build`` publishes *and* selects; it must not bypass the gate."""

    def test_build_runs_the_playable_unit_regression_gate(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            content_root = root / "content"
            cooked = _write_pack(root / "pack", ["a"])
            _write_pack(content_root / "rotwk-elves-vslice" / "aaaa", ["a", "b"])

            pipeline = mock.MagicMock()
            pipeline.build.return_value = cooked
            pipeline.conversion_cache_stats = {}
            pipeline.plan_report.return_value = {"ready": True}
            pipeline.reports_root = root / "reports"
            published: list[object] = []
            pipeline.publish_to_godot.side_effect = lambda *a, **k: published.append(a)

            resolved = mock.MagicMock()
            resolved.pack_id = "rotwk-elves-vslice"
            resolved.profile.id = "gate-test-profile"

            out, err = io.StringIO(), io.StringIO()
            with mock.patch.object(
                cli, "_load_or_build_catalog", return_value=_StubCatalog()
            ), mock.patch.object(
                cli, "ImportPipeline", return_value=pipeline
            ), mock.patch.object(
                cli, "_resolved", return_value=resolved
            ), mock.patch.object(
                cli, "audit_pack", return_value={"valid": True}
            ), mock.patch.object(
                cli, "bundle_digest", return_value="d" * 64
            ), contextlib.redirect_stdout(
                out
            ), contextlib.redirect_stderr(
                err
            ):
                code = cli.main(
                    [
                        "--state-root",
                        str(root / "state"),
                        "build",
                        "--install",
                        "C:/RotWK",
                        "--godot-content-root",
                        str(content_root),
                    ]
                )
        stderr = err.getvalue()
        self.assertEqual(code, publish_gate.PUBLISH_GATE_EXIT)
        self.assertIn("REFUSING TO PUBLISH", stderr)
        self.assertIn("b", stderr)  # the dropped unit id is named
        self.assertEqual(published, [], "build published despite the gate refusing")

    def test_no_publish_build_is_never_gated(self) -> None:
        """A proof build ships nothing, so there is nothing to refuse.

        ``tools/gate-retail.ps1`` runs two ``build --no-publish`` passes to
        prove byte-reproducibility. Gating those would fail the gate on a
        publication that is not happening.
        """

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            content_root = root / "content"
            cooked = _write_pack(root / "pack", ["a"])
            _write_pack(content_root / "rotwk-elves-vslice" / "aaaa", ["a", "b"])

            pipeline = mock.MagicMock()
            pipeline.build.return_value = cooked
            pipeline.conversion_cache_stats = {}
            pipeline.plan_report.return_value = {"ready": True}
            pipeline.reports_root = root / "reports"
            resolved = mock.MagicMock()
            resolved.pack_id = "rotwk-elves-vslice"
            resolved.profile.id = "gate-test-profile"

            out, err = io.StringIO(), io.StringIO()
            with mock.patch.object(
                cli, "_load_or_build_catalog", return_value=_StubCatalog()
            ), mock.patch.object(
                cli, "ImportPipeline", return_value=pipeline
            ), mock.patch.object(
                cli, "_resolved", return_value=resolved
            ), mock.patch.object(
                cli, "audit_pack", return_value={"valid": True}
            ), mock.patch.object(
                cli, "bundle_digest", return_value="d" * 64
            ), contextlib.redirect_stdout(
                out
            ), contextlib.redirect_stderr(
                err
            ):
                code = cli.main(
                    [
                        "--state-root",
                        str(root / "state"),
                        "build",
                        "--install",
                        "C:/RotWK",
                        "--godot-content-root",
                        str(content_root),
                        "--no-publish",
                    ]
                )
        self.assertEqual(code, 0)
        self.assertNotIn("REFUSING TO PUBLISH", err.getvalue())
        pipeline.publish_to_godot.assert_not_called()


if __name__ == "__main__":  # pragma: no cover
    unittest.main()
