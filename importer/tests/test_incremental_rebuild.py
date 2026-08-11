"""Synthetic contracts for bounded incremental faction rebuilds."""

from __future__ import annotations

import hashlib
from pathlib import Path

from openbfme_importer.incremental_rebuild import (
    compiler_dependency_identity,
    document_closure_identity,
    rebuild_execution_provenance,
    rebuild_status_for_coverage,
    reconvert_requested,
    w3d_adapter_cache_identity,
)
from openbfme_importer.cli import build_parser


def _sha(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def test_document_closure_edit_invalidates_only_dependent_document() -> None:
    documents = {
        "data/ini/object/a.ini": b"Object A\nEnd\n",
        "data/ini/object/b.ini": b"Object B\nEnd\n",
        "data/ini/commandset.ini": b"CommandSet Shared\nEnd\n",
    }
    a = document_closure_identity(documents, ("data/ini/object/a.ini",))
    b = document_closure_identity(documents, ("data/ini/object/b.ini",))

    edited = {**documents, "data/ini/object/a.ini": b"Object A\n  KindOf = HERO\nEnd\n"}
    assert document_closure_identity(edited, ("data/ini/object/a.ini",))["sha256"] != a["sha256"]
    assert document_closure_identity(edited, ("data/ini/object/b.ini",))["sha256"] == b["sha256"]


def test_synthetic_hundred_document_savings_measurement() -> None:
    documents = {
        f"data/ini/object/object-{index:03d}.ini": f"Object O{index}\nEnd\n".encode()
        for index in range(100)
    }
    before = {
        path: document_closure_identity(documents, (path,))["sha256"]
        for path in documents
    }
    changed_path = "data/ini/object/object-042.ini"
    documents[changed_path] += b"; compiler-relevant edit\n"
    rebuilt = sum(
        document_closure_identity(documents, (path,))["sha256"] != identity
        for path, identity in before.items()
    )
    assert rebuilt == 1  # 1% rebuild, versus the former 100% whole-corpus salt.


def test_unknown_document_dependencies_fail_closed_to_full_corpus() -> None:
    documents = {"a.ini": b"a", "b.ini": b"b"}
    before = document_closure_identity(documents, None)
    after = document_closure_identity({**documents, "b.ini": b"changed"}, None)
    assert before["mode"] == "full-corpus-fallback"
    assert before["sha256"] != after["sha256"]


def test_compiler_identity_is_family_scoped_and_unknown_fails_closed() -> None:
    modules = {
        "playable_unit_compiler.py": b"def compile_unit(): return 1\n",
        "playable_unit_pack_compiler.py": b"def pack_unit(): return 1\n",
        "playable_structure_compiler.py": b"def compile_structure(): return 1\n",
        "playable_structure_pack_compiler.py": b"def pack_structure(): return 1\n",
        "spellbook_compiler.py": b"def compile_spellbook(): return 1\n",
        "spellbook_pack_compiler.py": b"def pack_spellbook(): return 1\n",
        "faction_object_cache.py": b"CACHE_VERSION = 4\n",
    }
    unit_before = compiler_dependency_identity("unit", module_sources=modules)
    structure_before = compiler_dependency_identity("structure", module_sources=modules)
    unknown_before = compiler_dependency_identity("new-family", module_sources=modules)
    edited = {
        **modules,
        "playable_structure_compiler.py": b"def compile_structure(): return 2\n",
    }
    assert (
        compiler_dependency_identity("unit", module_sources=edited)["sha256"]
        == unit_before["sha256"]
    )
    assert (
        compiler_dependency_identity("structure", module_sources=edited)["sha256"]
        != structure_before["sha256"]
    )
    unknown_after = compiler_dependency_identity("new-family", module_sources=edited)
    assert unknown_before["mode"] == "full-compiler-fallback"
    assert unknown_before["sha256"] != unknown_after["sha256"]


def test_dynamic_compiler_dependency_discovery_fails_closed() -> None:
    modules = {
        "playable_unit_compiler.py": b"__import__(dependency_name)\n",
        "playable_unit_pack_compiler.py": b"pass\n",
        "faction_object_cache.py": b"pass\n",
        "unrelated.py": b"VALUE = 1\n",
    }
    identity = compiler_dependency_identity("unit", module_sources=modules)
    assert identity["mode"] == "full-compiler-fallback"
    assert identity["dependencyDiscoveryUncertain"] is True
    assert {row["path"] for row in identity["modules"]} == set(modules)


def test_reconversion_scope_and_provenance_are_honest() -> None:
    assert reconvert_requested("IsengardUrukHai", ("*uruk*",))
    assert not reconvert_requested("GondorFighter", ("*uruk*",))
    partial = rebuild_execution_provenance(
        reconvert_only=("*uruk*",), single_build=False
    )
    assert partial["reconversion"]["scope"] == "partial"
    assert partial["reconversion"]["patterns"] == ["*uruk*"]
    assert partial["reproducibility"]["attested"] is False
    assert partial["reconversion"]["fullCorpusReconverted"] is False
    single = rebuild_execution_provenance(reconvert_only=(), single_build=True)
    assert single["reproducibility"] == {
        "mode": "single-build",
        "attested": False,
        "reason": "cold A/B byte comparison was explicitly skipped",
    }


def test_scoped_adapter_edit_only_changes_selected_converter_key() -> None:
    patterns = ("*uruk*",)
    before = "a" * 64
    after = "b" * 64
    assert w3d_adapter_cache_identity(before, "GondorFighter", patterns) == (
        w3d_adapter_cache_identity(after, "GondorFighter", patterns)
    )
    assert w3d_adapter_cache_identity(before, "IsengardUrukHai", patterns) != (
        w3d_adapter_cache_identity(after, "IsengardUrukHai", patterns)
    )


def test_rebuild_status_reports_changed_source_and_compiler_reason(tmp_path: Path) -> None:
    assets = tmp_path / "assets"
    source = assets / "data" / "ini" / "object" / "a.ini"
    source.parent.mkdir(parents=True)
    source.write_bytes(b"changed")
    compiler = compiler_dependency_identity("unit")
    coverage = {
        "faction": "men",
        "objects": [
            {
                "id": "A",
                "family": "unit",
                "incremental": {
                    "sourceDocuments": [
                        {
                            "path": "data/ini/object/a.ini",
                            "sha256": _sha(b"before"),
                        }
                    ],
                    "compilerIdentity": "0" * 64,
                    "dependencyMode": "explicit-source-closure",
                    "cacheKey": "1" * 64,
                },
            }
        ],
    }
    status = rebuild_status_for_coverage(coverage, assets)
    row = status["objects"][0]
    assert row["wouldRebuild"] is True
    assert row["reasons"] == [
        "source-changed:data/ini/object/a.ini",
        f"compiler-identity-changed:{compiler['sha256']}",
    ]
    assert status["summary"] == {"objects": 1, "rebuild": 1, "reuse": 0}


def test_cli_exposes_scoped_reconvert_single_build_and_status() -> None:
    parser = build_parser()
    build = parser.parse_args(
        [
            "build",
            "--install",
            "synthetic-install",
            "--reconvert-only",
            "*uruk*",
            "--single-build",
        ]
    )
    assert build.reconvert_only == ["*uruk*"]
    assert build.single_build is True
    status = parser.parse_args(
        [
            "rebuild-status",
            "--faction",
            "men",
            "--assets-root",
            "synthetic-assets",
        ]
    )
    assert status.command == "rebuild-status"
    assert status.faction == ["men"]
