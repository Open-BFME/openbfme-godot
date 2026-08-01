"""Single-flight pack-filtered visual inventory under concurrent workers."""

from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from typing import Any

from openbfme_importer.retail_visual_closure import (
    _build_pack_filtered_context,
    _pack_filtered_asset_context,
    clear_visual_closure_asset_cache,
)


class _CountingCatalog:
    def __init__(self, present: set[str]) -> None:
        self.present = {p.casefold() for p in present}
        self.builds = 0
        self.entries = [type("E", (), {"name": p})() for p in present]

    def identity_sha256(self) -> str:
        return "a" * 64

    def resolve_exact(self, virtual_path: str) -> object | None:
        key = virtual_path.replace("\\", "/").casefold()
        return object() if key in self.present else None


def test_pack_filtered_context_single_flight(monkeypatch, tmp_path: Path) -> None:
    """Many concurrent callers must construct the filtered inventory once."""

    clear_visual_closure_asset_cache()
    builds = {"n": 0}
    fake_assets = tmp_path / "assets"
    fake_assets.mkdir()

    def _fake_build(root: Path | str, catalog: object) -> tuple[Any, ...]:
        builds["n"] += 1
        # Distinct non-empty path tuple so callers share the memoized result.
        return (
            {},
            {},
            {},
            ("art/w3d/a.w3d",),
            ("art/w3d/a.w3d",),
            {},
            object(),
        )

    monkeypatch.setattr(
        "openbfme_importer.retail_visual_closure._build_pack_filtered_context",
        _fake_build,
    )
    monkeypatch.setattr(
        "openbfme_importer.retail_visual_closure._assets_root_fingerprint",
        lambda root: "assets-key",
    )
    # Bypass real inventory: _build is fully faked, but resolve still needs a path.
    catalog = _CountingCatalog({"art/w3d/a.w3d"})

    def _call() -> tuple[Any, ...]:
        return _pack_filtered_asset_context(fake_assets, catalog)

    with ThreadPoolExecutor(max_workers=16) as pool:
        futures = [pool.submit(_call) for _ in range(16)]
        results = [f.result(timeout=30) for f in as_completed(futures)]

    assert builds["n"] == 1
    assert all(r is results[0] or r[3] == results[0][3] for r in results)
    clear_visual_closure_asset_cache()
