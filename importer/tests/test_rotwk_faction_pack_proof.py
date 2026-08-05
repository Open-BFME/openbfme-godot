"""The coverage waiver must never become a pack-build waiver implicitly."""

from __future__ import annotations

import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "rotwk_faction_pack_proof.py"


def _load_tool():
    spec = importlib.util.spec_from_file_location("rotwk_faction_pack_proof", TOOL)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_coverage_waiver_does_not_forward_pack_waiver() -> None:
    mod = _load_tool()
    assert mod._pack_incomplete_args(allow_incomplete_pack=False) == []


def test_explicit_pack_waiver_is_forwarded() -> None:
    mod = _load_tool()
    assert mod._pack_incomplete_args(allow_incomplete_pack=True) == [
        "--allow-incomplete"
    ]


def test_rotwk_men_pack_id_cannot_inherit_bfme2_host_identity() -> None:
    from openbfme_importer.cli import _faction_slice_pack_id

    assert _faction_slice_pack_id("rotwk", ["men"]) == "rotwk-men-vslice"
    assert _faction_slice_pack_id("bfme2", ["men"]) == "bfme2-men-vslice"
