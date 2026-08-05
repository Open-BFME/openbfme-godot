from __future__ import annotations

import hashlib
import json

import pytest

from openbfme_importer.catalog import ArchiveInfo, CatalogEntry, InstallCatalog
from openbfme_importer.effective_assets_catalog import EffectiveAssetsCatalog


def _sealed_tree(tmp_path):
    root = tmp_path / "effective"
    asset = root / "art" / "w3d" / "iu" / "patched.w3d"
    asset.parent.mkdir(parents=True)
    asset.write_bytes(b"patched-w3d")
    metadata = root / ".openbfme"
    metadata.mkdir()
    manifest = {
        "aggregate_sha256": hashlib.sha256(b"manifest").hexdigest(),
        "catalog": {"identity_sha256": hashlib.sha256(b"catalog").hexdigest()},
        "files": [
            {
                "path": "art/w3d/iu/patched.w3d",
                "size": len(b"patched-w3d"),
                "sha256": hashlib.sha256(b"patched-w3d").hexdigest(),
            }
        ],
    }
    (metadata / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
    return root, manifest


def test_sealed_manifest_is_the_exact_catalog_and_byte_source(tmp_path) -> None:
    root, manifest = _sealed_tree(tmp_path)
    catalog = EffectiveAssetsCatalog(root)

    entry = catalog.resolve_exact("ART/W3D/IU/PATCHED.W3D")
    assert entry is not None
    assert catalog.resolve_exact("art/w3d/iu/live-install-only.w3d") is None
    assert catalog.identity_sha256() == manifest["catalog"]["identity_sha256"]
    assert catalog.archive_sha256(entry.archive) == manifest["aggregate_sha256"]
    assert catalog.search("art/w3d/iu/*.w3d") == [entry]
    reader = catalog.open_archive_for(entry)
    assert reader.read_entry(catalog.as_entry(entry), max_bytes=64) == b"patched-w3d"


def test_sealed_catalog_reader_fails_on_post_manifest_size_drift(tmp_path) -> None:
    root, _manifest = _sealed_tree(tmp_path)
    catalog = EffectiveAssetsCatalog(root)
    entry = catalog.resolve_exact("art/w3d/iu/patched.w3d")
    assert entry is not None
    (root / "art" / "w3d" / "iu" / "patched.w3d").write_bytes(b"drift")

    with pytest.raises(ValueError, match="size drifted"):
        catalog.open_archive_for(entry).read_entry(entry, max_bytes=64)


def test_overlay_preserves_layered_semantics_and_adds_missing_assets(tmp_path) -> None:
    root, _manifest = _sealed_tree(tmp_path)
    semantic = root / "data" / "ini" / "object" / "patch.ini"
    semantic.parent.mkdir(parents=True)
    semantic.write_text("ObjectReskin PatchedOnly\nEnd\n", encoding="utf-8")
    manifest_path = root / ".openbfme" / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    semantic_bytes = semantic.read_bytes()
    manifest["files"].append(
        {
            "path": "data/ini/object/patch.ini",
            "size": len(semantic_bytes),
            "sha256": hashlib.sha256(semantic_bytes).hexdigest(),
        }
    )
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

    install = tmp_path / "install"
    install.mkdir()
    archive_path = install / "layer.big"
    archive_path.write_bytes(b"x" * 64)
    archive = ArchiveInfo("layer.big", 64, 1, "BIGF", 16, 1, "0" * 64)
    layered_entry = CatalogEntry("layer.big", "data/ini/object/base.ini", 16, 8, 0)
    base = InstallCatalog(install, (archive,), (layered_entry,))

    catalog = EffectiveAssetsCatalog(root, base_catalog=base)

    assert catalog.resolve_exact("data/ini/object/base.ini") == layered_entry
    assert catalog.resolve_exact("data/ini/object/patch.ini") is None
    patched = catalog.resolve_exact("art/w3d/iu/patched.w3d")
    assert patched is not None
    assert patched.archive == ".openbfme/sealed-effective-assets.bin"
