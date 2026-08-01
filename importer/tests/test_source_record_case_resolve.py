"""Case-insensitive effective-assets path resolve for layered RotWK extracts."""

from __future__ import annotations

import pytest

from openbfme_importer.retail_visual_profile import _source_record


def _src(**overrides):
    row = {
        "sha256": "a" * 64,
        "size": 12,
        "archive": "base.big",
        "offset": 0,
        "precedence": 1,
    }
    row.update(overrides)
    return row


def test_exact_path_match() -> None:
    path = "art/compiledtextures/gu/x.dds"
    rec = _source_record(path, {path: _src()})
    assert rec["virtualPath"] == path
    assert rec["byteLength"] == 12


def test_casefold_resolves_to_manifest_canonical_path() -> None:
    manifest = "Art/CompiledTextures/gu/gutownsman_02.dds"
    rec = _source_record(
        "art/compiledtextures/gu/gutownsman_02.dds",
        {manifest: _src(size=99)},
    )
    assert rec["virtualPath"] == manifest
    assert rec["byteLength"] == 99


def test_absent_path_fails_closed() -> None:
    with pytest.raises(ValueError, match="absent from effective-assets"):
        _source_record("art/missing.dds", {"art/other.dds": _src()})
