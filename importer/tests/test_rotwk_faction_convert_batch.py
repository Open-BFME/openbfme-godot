"""Fail-closed asset-root selection for the RotWK faction batch."""

from __future__ import annotations

import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "rotwk_faction_convert_batch.py"


def _load_tool():
    spec = importlib.util.spec_from_file_location("rotwk_faction_convert_batch", TOOL)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_effective_assets_uses_the_owner_selected_pure_retail_oracle(tmp_path: Path) -> None:
    """RE-PINNED 2026-08-04 (retail rebase, owner decision).

    These two tests used to pin `layered-effective-assets` as the canonical
    RotWK oracle. That tree is NOT pure retail and is now quarantined:
    `verify-effective-assets` reports `layer-expansion/__patch202.big` among its
    patch archives (the Unofficial 2.02 fan patch), and 530 of its INI files
    carry three-way merge markers (`;,;` / `;;,;;`) that the pure tree does not
    have at all - e.g. `gamedata.ini:24`
    `#define SPECIALPOWER_DEVASTATION_ENT_DAMAGE 300 ;;,;; 800 ; balance`.
    The canonical oracle is `cache/effective-assets`, which
    `extract-all-assets --game rotwk` produces and which verifies with only the
    retail `_patch201*` archives.

    The PROPERTY under test is unchanged and still the important one: the
    canonical tree is selected exactly, and there is no silent fallback to a
    different tree when it is absent.
    """

    mod = _load_tool()
    canonical = tmp_path / "editions" / "rotwk" / "cache" / "effective-assets"
    quarantined = tmp_path / "editions" / "rotwk" / "cache" / "layered-effective-assets"
    canonical.mkdir(parents=True)
    quarantined.mkdir()

    assert mod._effective_assets(tmp_path, "rotwk") == canonical


def test_effective_assets_does_not_fall_back_to_the_quarantined_layered_tree(
    tmp_path: Path,
) -> None:
    mod = _load_tool()
    quarantined = tmp_path / "editions" / "rotwk" / "cache" / "layered-effective-assets"
    quarantined.mkdir(parents=True)

    try:
        mod._effective_assets(tmp_path, "rotwk")
    except SystemExit as exc:
        assert "canonical rotwk effective-assets tree missing" in str(exc)
    else:
        raise AssertionError("the quarantined layered RotWK cache was accepted")
