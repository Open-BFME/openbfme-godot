from __future__ import annotations

from collections import Counter
from types import SimpleNamespace

import pytest

from openbfme_importer.requirement_graph import build_feature_graph, render_feature_graph
from openbfme_importer.retail_ini_coverage import classify_assignment_value


def test_feature_graph_closes_every_assignment_and_unknown() -> None:
    assignment_sites = [
        {"sourceIni": "a.ini", "line": 2, "rootKind": "Object", "rootName": "A", "field": "Health", "valueSha256": "1" * 64, "valueTokenCount": 1, "moduleKind": None, "categories": []},
        {"sourceIni": "a.ini", "line": 3, "rootKind": "Object", "rootName": "A", "field": "Model", "valueSha256": "2" * 64, "valueTokenCount": 1, "moduleKind": None, "categories": ["assets"]},
        {"sourceIni": "a.ini", "line": 4, "rootKind": "Object", "rootName": "A", "field": "Mystery", "valueSha256": "3" * 64, "valueTokenCount": 2, "moduleKind": None, "categories": []},
    ]
    scan = SimpleNamespace(
        documents=[{"path": "a.ini", "archive": "a.big"}],
        definitions=[{"sourceIni": "a.ini", "line": 1, "kind": "Object", "name": "A"}],
        assignment_sites=assignment_sites,
        object_rows=[{"objectId": "A", "sourceIni": "a.ini", "modules": {"X": 1}}],
        asset_references=[{"sourceIni": "a.ini", "line": 3, "field": "Model", "token": "a.w3d"}],
        module_sites=Counter({"behavior:x": 1}),
        script_call_sites=[{"sourceIni": "a.ini", "line": 5, "command": "Do"}],
        nested_sites=[], directive_sites=[],
        unknown_lines=[{"sourceIni": "a.ini", "line": 6, "textSha256": "4" * 64}],
    )
    classifications = {
        ("a.ini", 2, "health"): "scalar",
        ("a.ini", 3, "model"): "reference",
        ("a.ini", 4, "mystery"): "opaque-unresolved",
    }
    counts = {"documents": 1, "definitions": 1, "assignments": 3, "objects": 1, "assetReferences": 1, "objectModuleSites": 1, "scriptCallSites": 1}
    first = build_feature_graph(
        scan, baseline_id="fixture", policy_sha256="a" * 64, catalog_sha256="b" * 64,
        effective_tree_sha256="c" * 64, source_identity={"sha256": "d" * 64},
        root_queries=[], assignment_classifications=classifications, expected_counts=counts,
    )
    second = build_feature_graph(
        scan, baseline_id="fixture", policy_sha256="a" * 64, catalog_sha256="b" * 64,
        effective_tree_sha256="c" * 64, source_identity={"sha256": "d" * 64},
        root_queries=[], assignment_classifications=classifications, expected_counts=counts,
    )
    assert render_feature_graph(first) == render_feature_graph(second)
    assert [row["classification"] for row in first["assignments"]] == ["scalar", "reference", "opaque-unresolved"]
    assert first["counts"]["unknownSites"] == 1
    assert classify_assignment_value("1.5", module_kind=None, asset_reference_count=0) == "scalar"
    with pytest.raises(ValueError, match="classification"):
        build_feature_graph(
            scan, baseline_id="fixture", policy_sha256="a" * 64, catalog_sha256="b" * 64,
            effective_tree_sha256="c" * 64, source_identity={}, root_queries=[],
            assignment_classifications={}, expected_counts=counts,
        )
