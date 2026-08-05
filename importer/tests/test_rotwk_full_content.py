"""Fail-closed orchestration checks for the one-button RotWK path."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "rotwk_full_content.py"
FACTIONS = ("men", "elves", "dwarves", "isengard", "mordor", "wild", "angmar")


def _load_tool():
    spec = importlib.util.spec_from_file_location("rotwk_full_content", TOOL)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _write_batch_report(root: Path, *, gap: str | None = None, hard: str | None = None) -> None:
    rows = []
    for faction in FACTIONS:
        row = {
            "faction": faction,
            "gaps": 1 if faction == gap else 0,
            "conversionComplete": faction != gap,
        }
        if faction == hard:
            row["error"] = "hard failure"
        rows.append(row)
    path = root / "reports" / "rotwk-faction-convert-batch.json"
    path.parent.mkdir(parents=True)
    path.write_text(
        json.dumps({"factions": rows, "ledger": {"failureCount": 0, "sinkErrorCount": 0}}),
        encoding="utf-8",
    )


def test_convert_batch_check_reports_when_coverage_waiver_is_needed(tmp_path: Path) -> None:
    mod = _load_tool()
    _write_batch_report(tmp_path, gap="isengard")
    assert mod._assert_convert_batch_ok(tmp_path, convert_exit=3) is True


def test_convert_batch_check_rejects_hard_failure_even_on_exit_three(tmp_path: Path) -> None:
    mod = _load_tool()
    _write_batch_report(tmp_path, hard="mordor")
    with pytest.raises(RuntimeError, match="convert hard failures"):
        mod._assert_convert_batch_ok(tmp_path, convert_exit=3)


def test_clean_convert_needs_no_waiver(tmp_path: Path) -> None:
    mod = _load_tool()
    _write_batch_report(tmp_path)
    assert mod._assert_convert_batch_ok(tmp_path, convert_exit=0) is False


def test_select_existing_composes_seven_serial_receipts_and_ten_maps(
    tmp_path: Path,
) -> None:
    mod = _load_tool()
    state_root = tmp_path / "state"
    content_root = tmp_path / "content-packs"
    receipt_root = (
        state_root / "editions" / "rotwk" / "reports" / "faction-import"
    )
    receipt_root.mkdir(parents=True)
    for faction in FACTIONS:
        bundle = (faction[0] * 64) if faction != "men" else ("0" * 64)
        published = content_root / f"rotwk-{faction}-vslice" / bundle
        published.mkdir(parents=True)
        (receipt_root / f"{faction}-publication.json").write_text(
            json.dumps(
                {
                    "faction": faction,
                    "publicationReady": True,
                    "auditValid": True,
                    "conversionFailures": 0,
                    "bundleSha256": bundle,
                    "publishedPack": str(published),
                }
            ),
            encoding="utf-8",
        )
    map_bundle = "f" * 64
    published_maps = content_root / "rotwk-skirmish-maps-private" / map_bundle
    published_maps.mkdir(parents=True)
    report_path = state_root / "reports" / "rotwk-multimap-skirmish.json"
    report_path.parent.mkdir(parents=True)
    report_path.write_text(
        json.dumps(
            {
                "mapCount": 10,
                "rejectedMapCount": 0,
                "catalogProof": {"ok": True, "packMount": {"ok": True}},
                "build": {"exitCode": 0, "publishedPack": str(published_maps)},
            }
        ),
        encoding="utf-8",
    )
    selection = content_root / "selection.json"

    document = mod._select_existing_publications(
        state_root, content_root=content_root, selection=selection
    )

    assert document["activePack"] == f"rotwk-skirmish-maps-private/{map_bundle}"
    assert len(document["supplementalPacks"]) == 7
    assert json.loads(selection.read_text(encoding="utf-8")) == document


def test_select_existing_rejects_nonready_serial_receipt(tmp_path: Path) -> None:
    mod = _load_tool()
    receipt_root = (
        tmp_path / "editions" / "rotwk" / "reports" / "faction-import"
    )
    receipt_root.mkdir(parents=True)
    (receipt_root / "men-publication.json").write_text(
        json.dumps({"faction": "men", "publicationReady": False}),
        encoding="utf-8",
    )

    with pytest.raises(RuntimeError, match="not ready"):
        mod._serial_publication_rows(tmp_path)
