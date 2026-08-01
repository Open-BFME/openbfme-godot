"""Tests for conversion_ledger accounting."""

from __future__ import annotations

from pathlib import Path

from openbfme_importer.conversion_ledger import ConversionLedger


def test_ledger_percentages_and_jsonl(tmp_path: Path) -> None:
    sink = tmp_path / "events.jsonl"
    ledger = ConversionLedger(
        run_id="test",
        game="rotwk",
        install_root="F:/RotWK",
        sink_path=sink,
    )
    ledger.record(kind="map", unit_id="a", status="converted", log_to_stderr=False)
    ledger.record(kind="map", unit_id="b", status="converted", log_to_stderr=False)
    ledger.record(kind="map", unit_id="c", status="failed", error="boom", log_to_stderr=False)
    ledger.record(kind="type", unit_id="t", status="gap", log_to_stderr=False)
    # Informational aggregate must not dilute converted%
    ledger.record(kind="map", unit_id="rollup", status="planned", log_to_stderr=False)
    summary = ledger.summary()
    assert summary["totalUnits"] == 5
    assert summary["eligibleUnits"] == 4
    # converted-like over eligible only: 2/4 = 50%
    assert summary["percentConvertedLike"] == 50.0
    assert summary["failedLikeCount"] == 2  # failed + gap
    assert summary["failureCount"] == 1
    assert summary["sinkErrorCount"] == 0
    assert sink.is_file()
    lines = sink.read_text(encoding="utf-8").strip().splitlines()
    assert len(lines) == 5
    # planned + converted only → converted% 100% of eligible
    ledger2 = ConversionLedger("t2", "rotwk", "x")
    ledger2.record(kind="u", unit_id="1", status="converted", log_to_stderr=False)
    ledger2.record(kind="u", unit_id="2", status="planned", log_to_stderr=False)
    s2 = ledger2.summary()
    assert s2["percentConvertedLike"] == 100.0
    out = tmp_path / "summary.json"
    written = ledger.write_summary(out)
    assert out.is_file()
    assert written["eventCount"] == 5
