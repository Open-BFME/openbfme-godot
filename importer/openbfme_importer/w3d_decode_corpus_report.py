"""Serialize and regenerate the stored W3D decode-corpus evidence report.

The measurement lives in :mod:`.w3d_decode_corpus`; this module is only the
document boundary.  The absence of a CLI is how the stored report went stale
the first time -- regeneration required writing a one-off script, so nobody
did it when the decoder changed.  Regeneration (byte-identical for the same
tree and code)::

    PYTHONPATH=importer python -m openbfme_importer.w3d_decode_corpus_report \
        <effective-assets-root> <output-json>

The rendered document is exactly ``W3DDecodeCorpusReport.neutral()`` plus a
top-level ``provenance`` block (see :mod:`.measurement_provenance`) that
dates the report against the bytes of the decode-measurement import closure.
Consumers that republish this document's figures must verify that block and
refuse or loudly mark staleness; ``w3d_chunk_backlog`` does.

This module is deliberately *outside* the fingerprinted measurement closure:
serialization and provenance stamping do not change what was measured, so
editing them must not mark stored evidence stale.
"""

from __future__ import annotations

from collections.abc import Mapping
import json
from pathlib import Path

from .measurement_provenance import measurement_provenance
from .w3d_decode_corpus import W3DDecodeCorpusReport, scan_w3d_decode_corpus


DECODE_CORPUS_MEASUREMENT_MODULE = "openbfme_importer.w3d_decode_corpus"


def render_w3d_decode_corpus_report(
    report: W3DDecodeCorpusReport,
    *,
    provenance: Mapping[str, object] | None = None,
) -> str:
    """Render the report as the stored document, provenance included.

    Serialization matches the established stored report exactly (compact,
    ASCII, sorted keys, one trailing newline) so a regenerated document
    differs from its predecessor only where the underlying facts differ --
    plus the ``provenance`` block that dates it.
    """

    if not isinstance(report, W3DDecodeCorpusReport):
        raise TypeError("rendering requires a W3DDecodeCorpusReport")
    document = report.neutral()
    document["provenance"] = (
        dict(provenance)
        if provenance is not None
        else measurement_provenance(DECODE_CORPUS_MEASUREMENT_MODULE)
    )
    return (
        json.dumps(
            document,
            ensure_ascii=True,
            sort_keys=True,
            separators=(",", ":"),
        )
        + "\n"
    )


def write_w3d_decode_corpus_report(
    effective_assets_root: Path | str,
    output_path: Path | str,
) -> W3DDecodeCorpusReport:
    """Scan the effective-assets tree and write the dated report document."""

    report = scan_w3d_decode_corpus(effective_assets_root)
    rendered = render_w3d_decode_corpus_report(report)
    with open(output_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(rendered)
    return report


def _main(argv: list[str]) -> int:
    if len(argv) != 2:
        raise SystemExit(
            "usage: python -m openbfme_importer.w3d_decode_corpus_report "
            "<effective-assets-root> <output-json>"
        )
    root, output = argv
    report = write_w3d_decode_corpus_report(root, output)
    summary = {
        "selectedFileCount": report.selected_file_count,
        "streamCompleteFileCount": report.stream_complete_file_count,
        "incompleteFileCount": report.incomplete_file_count,
        "damagedFileCount": report.damaged_file_count,
        "unresolvedFileCount": report.unresolved_file_count,
        "unsupportedFileCount": report.unsupported_file_count,
    }
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":  # pragma: no cover - thin CLI shim
    import sys

    raise SystemExit(_main(sys.argv[1:]))


__all__ = [
    "DECODE_CORPUS_MEASUREMENT_MODULE",
    "render_w3d_decode_corpus_report",
    "write_w3d_decode_corpus_report",
]
