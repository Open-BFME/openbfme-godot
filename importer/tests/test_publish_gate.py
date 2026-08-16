"""Fail-closed gates on the faction publish path.

Bundle ``8585890b`` shipped 20 of 24 playable units from a conversion whose
own report said ``conversionComplete: false`` with 8 converter gaps, and the
command exited 0 with ``conversion_failures: 0`` because the only completeness
signal the publish path consulted was the *asset manifest* incomplete list -
a different, narrower record that says nothing about descriptor-level
converter gaps. These tests pin both halves of the gate: the coverage report
must be clean, and a cook must not silently ship fewer playable units than
the incumbent bundle of the same pack id.
"""

from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from openbfme_importer import cli


def _write_coverage(root: Path, faction: str, *, complete: bool, gaps: int) -> Path:
    path = root / f"{faction}-coverage.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(
            {
                "aggregateSha256": "a" * 64,
                "summary": {
                    "objectCount": 24,
                    "convertedCount": 24 - gaps,
                    "excludedCount": 0,
                    "converterGapCount": gaps,
                    "unresolvedLeafCount": 0,
                    "conversionComplete": complete,
                },
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    return path


def _write_pack(
    root: Path, unit_names: list[str], *, catalog_identity: str | None = None
) -> Path:
    units = root / "data" / "playable-units"
    units.mkdir(parents=True, exist_ok=True)
    if catalog_identity is not None:
        (root / "pack.json").write_text(
            json.dumps({"sourceCatalogIdentitySha256": catalog_identity}),
            encoding="utf-8",
        )
    for name in unit_names:
        (units / f"{name}.json").write_text("{}", encoding="utf-8")
    return root


class CoverageGateTests(unittest.TestCase):
    def test_clean_coverage_has_no_blockers(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_coverage(root, "men", complete=True, gaps=0)
            self.assertEqual(cli._coverage_publish_blockers(root, ["men"]), [])

    def test_incomplete_conversion_blocks_publication(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            # The exact shape of the 8585890b cook.
            _write_coverage(root, "men", complete=False, gaps=8)
            blockers = cli._coverage_publish_blockers(root, ["men"])
        self.assertEqual(len(blockers), 1)
        self.assertIn("men", blockers[0])
        self.assertIn("8", blockers[0])

    def test_gap_count_alone_blocks_even_when_the_flag_lies(self) -> None:
        # conversionComplete is derived, not authoritative; a report that
        # claims completion while carrying gaps is still a blocker.
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_coverage(root, "elves", complete=True, gaps=3)
            blockers = cli._coverage_publish_blockers(root, ["elves"])
        self.assertEqual(len(blockers), 1)
        self.assertIn("elves", blockers[0])

    def test_every_faction_is_reported_not_just_the_first(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_coverage(root, "men", complete=False, gaps=8)
            _write_coverage(root, "elves", complete=True, gaps=0)
            _write_coverage(root, "dwarves", complete=False, gaps=1)
            blockers = cli._coverage_publish_blockers(
                root, ["men", "elves", "dwarves"]
            )
        self.assertEqual(len(blockers), 2)

    def test_unreadable_or_shapeless_report_blocks(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            (root / "men-coverage.json").write_text("{}", encoding="utf-8")
            blockers = cli._coverage_publish_blockers(root, ["men", "wild"])
        # missing summary + missing file
        self.assertEqual(len(blockers), 2)


class PlayableUnitRegressionGateTests(unittest.TestCase):
    def test_counts_playable_unit_documents_in_a_pack(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            pack = _write_pack(Path(raw), ["a", "b", "c"])
            self.assertEqual(cli._pack_playable_unit_count(pack), 3)

    def test_pack_without_a_playable_unit_directory_counts_zero(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            self.assertEqual(cli._pack_playable_unit_count(Path(raw)), 0)

    def test_incumbent_is_the_richest_published_bundle(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            content_root = Path(raw)
            pack_root = content_root / "rotwk-men-vslice"
            _write_pack(pack_root / "aaaa", [f"u{i}" for i in range(24)])
            _write_pack(pack_root / "bbbb", [f"u{i}" for i in range(20)])
            count, bundle = cli._incumbent_playable_unit_count(
                content_root, "rotwk-men-vslice"
            )
        self.assertEqual(count, 24)
        self.assertEqual(bundle, "aaaa")

    def test_unpublished_pack_id_has_no_incumbent(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            count, bundle = cli._incumbent_playable_unit_count(
                Path(raw), "rotwk-men-vslice"
            )
        self.assertIsNone(count)
        self.assertIsNone(bundle)

    def test_partial_staging_directories_are_not_incumbents(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            content_root = Path(raw)
            pack_root = content_root / "rotwk-men-vslice"
            _write_pack(pack_root / "aaaa", ["u0", "u1"])
            _write_pack(pack_root / "bbbb.building", [f"u{i}" for i in range(99)])
            count, bundle = cli._incumbent_playable_unit_count(
                content_root, "rotwk-men-vslice"
            )
        self.assertEqual(count, 2)
        self.assertEqual(bundle, "aaaa")

    def test_shipping_fewer_units_than_the_incumbent_is_a_blocker(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            content_root = Path(raw)
            pack_root = content_root / "rotwk-men-vslice"
            _write_pack(pack_root / "aaaa", [f"u{i}" for i in range(24)])
            fresh = _write_pack(Path(raw) / "cook", [f"u{i}" for i in range(20)])
            blocker = cli._playable_unit_regression_blocker(
                fresh, content_root, "rotwk-men-vslice"
            )
        self.assertIsNotNone(blocker)
        assert blocker is not None
        self.assertIn("20", blocker)
        self.assertIn("24", blocker)

    def test_shipping_the_same_or_more_units_is_allowed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            content_root = Path(raw)
            pack_root = content_root / "rotwk-men-vslice"
            _write_pack(pack_root / "aaaa", [f"u{i}" for i in range(24)])
            same = _write_pack(Path(raw) / "same", [f"u{i}" for i in range(24)])
            more = _write_pack(Path(raw) / "more", [f"u{i}" for i in range(25)])
            self.assertIsNone(
                cli._playable_unit_regression_blocker(
                    same, content_root, "rotwk-men-vslice"
                )
            )
            self.assertIsNone(
                cli._playable_unit_regression_blocker(
                    more, content_root, "rotwk-men-vslice"
                )
            )

    def test_different_catalog_identity_is_not_an_incumbent(self) -> None:
        """A RotWK-contaminated historical pack cannot set BFME2's roster."""

        with tempfile.TemporaryDirectory() as raw:
            content_root = Path(raw)
            pack_root = content_root / "bfme2-men-vslice"
            _write_pack(
                pack_root / "rotwk-mislabeled",
                ["bfme2-unit", "rotwk-only-unit"],
                catalog_identity="r" * 64,
            )
            fresh = _write_pack(
                Path(raw) / "cook",
                ["bfme2-unit"],
                catalog_identity="b" * 64,
            )
            blocker = cli._playable_unit_regression_blocker(
                fresh, content_root, "bfme2-men-vslice"
            )
        self.assertIsNone(blocker)

    def test_same_catalog_identity_still_blocks_a_roster_drop(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            content_root = Path(raw)
            pack_root = content_root / "bfme2-men-vslice"
            _write_pack(
                pack_root / "incumbent",
                ["u0", "u1"],
                catalog_identity="b" * 64,
            )
            fresh = _write_pack(
                Path(raw) / "cook", ["u0"], catalog_identity="b" * 64
            )
            blocker = cli._playable_unit_regression_blocker(
                fresh, content_root, "bfme2-men-vslice"
            )
        self.assertIsNotNone(blocker)


class GateOverrideSurfaceTests(unittest.TestCase):
    def test_both_overrides_are_documented_cli_flags(self) -> None:
        parser = cli.build_parser()
        namespace = parser.parse_args(
            [
                "publish-faction-to-slice",
                "--install",
                "C:/RotWK",
                "--faction",
                "men",
                "--allow-incomplete-coverage",
                "--allow-fewer-playable-units",
            ]
        )
        self.assertTrue(namespace.allow_incomplete_coverage)
        self.assertTrue(namespace.allow_fewer_playable_units)

    def test_overrides_default_to_off(self) -> None:
        parser = cli.build_parser()
        namespace = parser.parse_args(
            [
                "publish-faction-to-slice",
                "--install",
                "C:/RotWK",
                "--faction",
                "men",
            ]
        )
        self.assertFalse(namespace.allow_incomplete_coverage)
        self.assertFalse(namespace.allow_fewer_playable_units)


if __name__ == "__main__":  # pragma: no cover
    unittest.main()
