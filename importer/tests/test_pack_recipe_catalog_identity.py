"""Pack recipe ↔ InstallCatalog identity gate."""

from __future__ import annotations

import pytest

from openbfme_importer.pack_recipe_catalog_identity import (
    PackRecipeCatalogIdentityError,
    assert_pack_recipe_catalog_identity,
    filter_virtual_paths_to_catalog,
    missing_required_catalog_patterns,
)


class _FakeCatalog:
    def __init__(self, present: set[str]) -> None:
        self._present = {path.casefold() for path in present}

    def resolve_exact(self, virtual_path: str):
        key = virtual_path.replace("\\", "/").casefold()
        return object() if key in self._present else None


def test_missing_required_patterns_reports_orphan_rows() -> None:
    recipe = {
        "resources": [
            {
                "id": "unit-a-model",
                "required": True,
                "patterns": ["art/w3d/ok.w3d"],
            },
            {
                "id": "unit-a-orphan",
                "required": True,
                "patterns": ["art/w3d/missing.w3d"],
            },
            {
                "id": "unit-a-optional",
                "required": False,
                "patterns": ["art/w3d/also-missing.w3d"],
            },
        ]
    }
    catalog = _FakeCatalog({"art/w3d/ok.w3d"})
    missing = missing_required_catalog_patterns(recipe, catalog)
    assert missing == [("unit-a-orphan", "art/w3d/missing.w3d")]


def test_assert_pack_recipe_catalog_identity_raises_with_sample() -> None:
    recipe = {
        "resources": [
            {
                "id": "structure-x",
                "required": True,
                "patterns": ["art/w3d/ghost.w3d"],
            }
        ]
    }
    catalog = _FakeCatalog(set())
    with pytest.raises(PackRecipeCatalogIdentityError, match="ghost.w3d"):
        assert_pack_recipe_catalog_identity(recipe, catalog, object_id="X")


def test_assert_skips_without_catalog() -> None:
    recipe = {
        "resources": [
            {
                "id": "unit-a",
                "required": True,
                "patterns": ["art/w3d/missing.w3d"],
            }
        ]
    }
    assert_pack_recipe_catalog_identity(recipe, None)


def test_filter_virtual_paths_to_catalog() -> None:
    catalog = _FakeCatalog({"art/w3d/a.w3d"})
    kept = filter_virtual_paths_to_catalog(
        ["art/w3d/a.w3d", "art/w3d/b.w3d"], catalog
    )
    assert kept == ("art/w3d/a.w3d",)
