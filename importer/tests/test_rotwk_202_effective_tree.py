from __future__ import annotations

import hashlib
from pathlib import Path
import struct
import tempfile

import pytest

from openbfme_importer.catalog import CatalogEntry, InstallCatalog
from openbfme_importer.effective_tree import (
    build_effective_tree,
    render_effective_tree,
    require_exact_bytes,
)


def _big(path: Path, files: list[tuple[str, bytes]]) -> None:
    encoded = [(name.encode("latin-1") + b"\0", payload) for name, payload in files]
    header_size = 16 + sum(8 + len(name) for name, _ in encoded)
    offset = header_size
    records = bytearray()
    bodies = bytearray()
    for name, payload in encoded:
        records.extend(struct.pack(">II", offset, len(payload)))
        records.extend(name)
        bodies.extend(payload)
        offset += len(payload)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"BIG4" + struct.pack("<I", offset) + struct.pack(">II", len(files), header_size) + records + bodies)


def _catalog(root: Path) -> InstallCatalog:
    _big(root / "layer-0-patch/a.big", [("Data/Foo.INI", b"patch"), ("lotr.str", b"new"), ("unique.bin", b"only")])
    _big(root / "layer-1-base/b.big", [("data/foo.ini", b"base"), ("LOTR.STR", b"old")])
    catalog = InstallCatalog.build(root)
    catalog.source_policy = type(
        "Policy", (), {
            "policy_sha256": "a" * 64,
            "identity_record": lambda self: {"policySha256": self.policy_sha256},
        },
    )()
    return catalog


def test_effective_tree_is_deterministic_complete_and_fail_closed() -> None:
    with tempfile.TemporaryDirectory(prefix="openbfme-effective-tree-") as raw:
        root = Path(raw)
        catalog = _catalog(root)
        kwargs = {
            "baseline_id": "fixture",
            "policy_sha256": "a" * 64,
            "catalog_sha256": catalog.identity_sha256(),
            "parser_sources": {"sha256": "b" * 64, "sources": []},
            "expected_records": 5,
            "expected_winners": 3,
            "expected_shadows": 2,
        }
        first = build_effective_tree(catalog, **kwargs)
        second = build_effective_tree(catalog, **kwargs)
        assert render_effective_tree(first) == render_effective_tree(second)
        assert first["counts"] == {"archives": 2, "records": 5, "winners": 3, "shadows": 2}
        foo = next(chain for chain in first["overrideChains"] if chain["key"] == "data/foo.ini")
        rows = [first["records"][index] for index in foo["recordIndexes"]]
        assert [row["archive"] for row in rows] == ["layer-0-patch/a.big", "layer-1-base/b.big"]
        assert [row["payloadSha256"] for row in rows] == [
            hashlib.sha256(b"patch").hexdigest(), hashlib.sha256(b"base").hexdigest()
        ]
        assert {row["shadowSemantics"] for row in rows} == {"additive-retained"}

        artifact = root / "tree.json"
        artifact.write_bytes(render_effective_tree(first))
        require_exact_bytes(artifact, render_effective_tree(second))
        artifact.write_text("{}\n", encoding="utf-8")
        with pytest.raises(ValueError, match="differs"):
            require_exact_bytes(artifact, render_effective_tree(second))

        bad = InstallCatalog(
            root,
            (),
            (CatalogEntry("x.big", "a", 0, 0, 0), CatalogEntry("x.big", "a/b", 0, 0, 0)),
        )
        bad.source_policy = type(
            "Policy", (), {
                "policy_sha256": "a" * 64,
                "identity_record": lambda self: {"policySha256": self.policy_sha256},
            },
        )()
        with pytest.raises(ValueError, match="prefix collision"):
            build_effective_tree(bad, **{**kwargs, "catalog_sha256": bad.identity_sha256(), "expected_records": 2})
