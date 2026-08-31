"""Pack recipe ↔ InstallCatalog identity gate."""

from __future__ import annotations

import pytest

from openbfme_importer.pack_recipe_catalog_identity import (
    PackRecipeCatalogIdentityError,
    assert_pack_recipe_catalog_identity,
    audit_pack_target_identity,
    filter_virtual_paths_to_catalog,
    missing_required_catalog_patterns,
)


def test_target_identity_requires_baseline_catalog_and_recipe() -> None:
    expected = {
        "sourceBaselineId": "rotwk-202-v9.7.7-en",
        "sourceCatalogIdentitySha256": "a" * 64,
        "sourceRecipeSha256": "b" * 64,
    }
    audit = audit_pack_target_identity(
        expected, baseline_id="rotwk-202-v9.7.7-en", catalog_sha256="a" * 64,
    )
    assert audit["matchesTarget"] is True
    assert audit["failures"] == []


def test_target_identity_names_every_missing_or_mismatched_marker() -> None:
    audit = audit_pack_target_identity(
        {"sourceBaselineId": "rotwk-201-en", "sourceCatalogIdentitySha256": "c" * 64},
        baseline_id="rotwk-202-v9.7.7-en", catalog_sha256="a" * 64,
    )
    assert audit["matchesTarget"] is False
    assert audit["failures"] == [
        "missing-or-mismatched-source-baseline",
        "missing-or-mismatched-source-catalog",
        "missing-or-invalid-source-recipe",
    ]


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
