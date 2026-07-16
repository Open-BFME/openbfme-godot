from __future__ import annotations

import copy
import hashlib
import json

import pytest

import openbfme_importer.map_script_closure as closure_module
from openbfme_importer.map_script_closure import (
    MAP_SCRIPT_CLOSURE_SCHEMA,
    MapScriptClosureError,
    build_map_script_closure,
)


def _sha256(seed: str) -> str:
    return hashlib.sha256(seed.encode("utf-8")).hexdigest()


def _sources(count: int = 5) -> list[tuple[str, int, str]]:
    result = [
        (f"data/maps/script-{index}.scb", 100 + index, _sha256(f"source-{index}"))
        for index in range(count)
    ]
    if count == 5:
        # Exercise the SCB corpus's legal source-content deduplication while
        # retaining five exact source-path joins.
        result[-1] = (result[-1][0], result[0][1], result[0][2])
    return result


def _handoff(path: str, size: int, digest: str) -> dict[str, object]:
    classification = _sha256(f"classification:{path}")
    chunk_names = _sha256(f"chunks:{path}")
    census = _sha256(f"census:{path}")
    body = _sha256(f"body:{path}")
    basis = {
        "schema": "openbfme.map-native-handoff-evidence",
        "schemaVersion": 0,
        "sourcePath": path,
        "sourceBytes": size,
        "sourceSha256": digest,
        "discoveryReasons": ["ckmp-signature"],
        "artifactKind": "script-container",
        "classificationEvidenceSha256": classification,
        "chunkNameSetSha256": chunk_names,
        "censusEvidenceSha256": census,
        "status": "handed-off",
        "rejectionCode": None,
        "rejectionEvidenceSha256": None,
        "conversionDisposition": "no-native-output",
    }
    return {
        "sourcePath": path,
        "sourceBytes": size,
        "sourceSha256": digest,
        "discoveryReasons": ["ckmp-signature"],
        "artifactKind": "script-container",
        "classificationEvidenceSha256": classification,
        "chunkNameSetSha256": chunk_names,
        "censusEvidenceSha256": census,
        "bodySha256": body,
        "status": "handed-off",
        "rejectionCode": None,
        "rejectionEvidenceSha256": None,
        "handoffEvidenceSha256": closure_module._map_sha256(basis),
        "outputPath": None,
    }


def _terrain_entry_and_output() -> tuple[dict[str, object], dict[str, object]]:
    source_path = "data/maps/terrain.map"
    source_sha256 = _sha256("terrain-source")
    output_path = closure_module._map_output_relative(source_sha256, "multiplayer", 1)
    inventory = [
        {
            "path": "map.json",
            "bytes": 91,
            "sha256": _sha256("terrain-map-json"),
        }
    ]
    tree_sha256 = closure_module._map_sha256(
        {
            "schema": "openbfme.map-native-output-tree",
            "schemaVersion": 0,
            "files": inventory,
        }
    )
    resolution = {
        "resolutionStatus": "complete",
        "typeCount": 0,
        "placementCount": 0,
        "resolvedTypeCount": 0,
        "resolvedPlacementCount": 0,
        "boundTypeCount": 0,
        "boundPlacementCount": 0,
        "logicalTypeCount": 0,
        "logicalPlacementCount": 0,
        "unresolvedTypeCount": 0,
        "unresolvedPlacementCount": 0,
    }
    output = {
        "path": output_path,
        "sourceSha256": source_sha256,
        "mapKind": "multiplayer",
        "profileVersion": 1,
        "runnable": True,
        "structuralStatus": "runnable-structure",
        "fileCount": 1,
        "bytes": 91,
        "treeSha256": tree_sha256,
        "inventory": inventory,
        "backtestEvidence": {
            "valid": True,
            "runnable": True,
            "gameplayFidelityClaimed": False,
            "facts": {
                "mapKind": "multiplayer",
                "profileVersion": 1,
                "runnable": True,
            },
        },
        "objectResolution": resolution,
    }
    entry = {
        "sourcePath": source_path,
        "sourceBytes": 499,
        "sourceSha256": source_sha256,
        "discoveryReasons": ["map-suffix", "ear-signature"],
        "mapKind": "multiplayer",
        "profileVersion": 1,
        "runnable": True,
        "structuralStatus": "runnable-structure",
        "censusEvidenceSha256": _sha256("terrain-census"),
        "bodySha256": _sha256("terrain-body"),
        "status": "accepted",
        "rejectionCode": None,
        "rejectionEvidenceSha256": None,
        "outputPath": output_path,
    }
    return entry, output


def _map_manifest(
    sources: list[tuple[str, int, str]],
    *,
    source_manifest_sha256: str | None = None,
    source_aggregate_sha256: str | None = None,
) -> dict[str, object]:
    handoffs = [_handoff(*item) for item in sources]
    if sources:
        entries: list[dict[str, object]] = []
        outputs: list[dict[str, object]] = []
    else:
        entry, output = _terrain_entry_and_output()
        entries = [entry]
        outputs = [output]
    parsed_outputs = tuple(closure_module._parse_map_output(item) for item in outputs)
    output_lookup = {item.path.casefold(): item for item in parsed_outputs}
    parsed_entries = tuple(
        closure_module._parse_map_entry(item, output_lookup) for item in entries
    )
    parsed_handoffs = tuple(
        closure_module._parse_map_handoff(item) for item in handoffs
    )
    selection = {
        "sourceManifestSha256": source_manifest_sha256
        or _sha256("map-source-manifest"),
        "sourceManifestAggregateSha256": source_aggregate_sha256
        or _sha256("map-source-aggregate"),
        "sourceMapCorpusSha256": _sha256("map-corpus"),
        "sourceProfileSelectionSha256": _sha256("map-profiles"),
        "sourceMapInventorySha256": _sha256("map-inventory"),
        "sourceMapEvidenceSha256": _sha256("map-evidence"),
        "requestSha256": _sha256("map-request"),
    }
    basis: dict[str, object] = {
        "schema": "openbfme.map-native-corpus",
        "schemaVersion": 0,
        "selection": selection,
        "summary": closure_module._map_summary(
            parsed_entries, parsed_handoffs, parsed_outputs
        ),
        "entries": entries,
        "handoffs": handoffs,
        "outputs": outputs,
    }
    return {**basis, "identitySha256": closure_module._map_sha256(basis)}


def _backtest(size: int, digest: str, semantic: str) -> dict[str, object]:
    body_sha256 = _sha256(f"decoded:{digest}")
    basis = {
        "schema": "openbfme.sage-scb-backtest",
        "schemaVersion": 0,
        "sourceBytes": size,
        "decodedBodyBytes": max(size - 8, 0),
        "chunkCount": 3,
        "sourceSha256": digest,
        "decodedBodySha256": body_sha256,
        "reconstructedBodySha256": body_sha256,
        "semanticSha256": semantic,
        "exactWireMatch": True,
    }
    return {
        "schema": "openbfme.sage-scb-backtest",
        "schemaVersion": 0,
        "accepted": True,
        "exactWireMatch": True,
        "sourceBytes": size,
        "decodedBodyBytes": max(size - 8, 0),
        "chunkCount": 3,
        "sourceSha256": digest,
        "decodedBodySha256": body_sha256,
        "reconstructedBodySha256": body_sha256,
        "semanticSha256": semantic,
        "evidenceSha256": closure_module._scb_sha256(basis),
    }


def _scb_manifest(
    sources: list[tuple[str, int, str]],
    *,
    source_manifest_sha256: str | None = None,
    source_aggregate_sha256: str | None = None,
) -> dict[str, object]:
    outputs_by_source: dict[str, dict[str, object]] = {}
    entries: list[dict[str, object]] = []
    for path, size, digest in sorted(
        sources, key=lambda item: (item[0].casefold(), item[0])
    ):
        output = outputs_by_source.get(digest)
        if output is None:
            native_sha256 = _sha256(f"native:{digest}")
            semantic_sha256 = _sha256(f"semantic:{digest}")
            output = {
                "path": closure_module._scb_output_relative(digest, native_sha256),
                "sourceSha256": digest,
                "nativeBytes": 500 + len(outputs_by_source),
                "nativeSha256": native_sha256,
                "semanticSha256": semantic_sha256,
                "backtestEvidence": _backtest(size, digest, semantic_sha256),
            }
            outputs_by_source[digest] = output
        entries.append(
            {
                "sourcePath": path,
                "sourceBytes": size,
                "sourceSha256": digest,
                "outputPath": output["path"],
            }
        )
    outputs = sorted(
        outputs_by_source.values(),
        key=lambda item: (str(item["path"]).casefold(), str(item["path"])),
    )
    source = {
        "manifestAggregateSha256": source_aggregate_sha256
        or _sha256("scb-source-aggregate"),
        "manifestFileCount": max(len(entries), 1),
        "manifestSha256": source_manifest_sha256 or _sha256("scb-source-manifest"),
        "manifestTotalBytes": max(sum(item[1] for item in sources), 1),
    }
    selected_rows = [
        {
            "path": item["sourcePath"],
            "size": item["sourceBytes"],
            "sha256": item["sourceSha256"],
        }
        for item in entries
    ]
    selection = {
        "caseInsensitiveSuffix": ".scb",
        "files": len(entries),
        "bytes": sum(item["sourceBytes"] for item in entries),
        "inventorySha256": closure_module._scb_inventory_sha256(
            "openbfme.scb-native-corpus-selection-v0", selected_rows
        ),
    }
    limits = {
        "hardMaxFiles": 1_024,
        "hardMaxTotalBytes": 1024 * 1024 * 1024,
        "maxFiles": 1_024,
        "maxTotalBytes": 1024 * 1024 * 1024,
    }
    parsed_entries = tuple(closure_module._parse_scb_entry(item) for item in entries)
    parsed_outputs = tuple(closure_module._parse_scb_output(item) for item in outputs)
    output_rows = [
        {
            "path": item.path,
            "size": item.native_bytes,
            "sha256": item.native_sha256,
        }
        for item in parsed_outputs
    ]
    request_basis = {
        "schema": "openbfme.scb-native-corpus-request",
        "schemaVersion": 0,
        "source": source,
        "selection": selection,
        "limits": limits,
    }
    basis: dict[str, object] = {
        "schema": "openbfme.scb-native-corpus",
        "schemaVersion": 0,
        "source": source,
        "selection": selection,
        "limits": limits,
        "summary": closure_module._scb_summary(parsed_entries, parsed_outputs),
        "entries": entries,
        "outputs": outputs,
        "outputTreeSha256": closure_module._scb_inventory_sha256(
            "openbfme.scb-native-corpus-output-tree-v0", output_rows
        ),
        "requestSha256": closure_module._scb_sha256(request_basis),
    }
    return {**basis, "identitySha256": closure_module._scb_sha256(basis)}


def _reseal_map(document: dict[str, object]) -> None:
    basis = {key: value for key, value in document.items() if key != "identitySha256"}
    document["identitySha256"] = closure_module._map_sha256(basis)


def _reseal_scb(document: dict[str, object]) -> None:
    basis = {key: value for key, value in document.items() if key != "identitySha256"}
    document["identitySha256"] = closure_module._scb_sha256(basis)


def test_complete_five_source_join_is_path_free_and_exact() -> None:
    sources = _sources()
    result = build_map_script_closure(_map_manifest(sources), _scb_manifest(sources))

    assert result.complete
    assert result.cross_manifest_exact_source_join
    assert len(result.entries) == 5
    assert len(result.outputs) == 4
    assert result.source_bytes == sum(item[1] for item in sources)
    assert result.native_bytes == sum(item.native_bytes for item in result.outputs)
    neutral = result.neutral()
    assert neutral["schema"] == MAP_SCRIPT_CLOSURE_SCHEMA
    assert neutral["summary"]["exactWireBacktestCount"] == 4
    assert neutral["summary"]["gameplayFidelityClaimed"] is False
    assert neutral["summary"]["glbConversionClaimed"] is False
    encoded = json.dumps(neutral, sort_keys=True)
    assert "data/maps" not in encoded
    assert "objects/" not in encoded
    assert closure_module._closure_sha256(result.evidence_hash_basis()) == (
        result.evidence_sha256
    )


def test_zero_scb_bfme_case_is_vacuously_complete() -> None:
    source_manifest = _sha256("same-source-manifest")
    aggregate = _sha256("same-source-aggregate")
    result = build_map_script_closure(
        _map_manifest(
            [],
            source_manifest_sha256=source_manifest,
            source_aggregate_sha256=aggregate,
        ),
        _scb_manifest(
            [],
            source_manifest_sha256=source_manifest,
            source_aggregate_sha256=aggregate,
        ),
    )

    assert result.complete
    assert result.entries == ()
    assert result.outputs == ()
    assert result.cross_manifest_exact_source_join
    assert result.neutral()["summary"]["complete"] is True


@pytest.mark.parametrize("mismatch", ["missing", "extra", "case", "hash"])
def test_source_join_mismatches_fail_closed(mismatch: str) -> None:
    map_sources = _sources()
    scb_sources = list(map_sources)
    if mismatch == "missing":
        scb_sources.pop()
    elif mismatch == "extra":
        scb_sources.append(("data/maps/unmatched.scb", 9, _sha256("unmatched")))
    elif mismatch == "case":
        path, size, digest = scb_sources[0]
        scb_sources[0] = (path.upper(), size, digest)
    else:
        path, size, _digest = scb_sources[0]
        scb_sources[0] = (path, size, _sha256("different-source"))

    with pytest.raises(MapScriptClosureError, match="counts|casing|identity"):
        build_map_script_closure(_map_manifest(map_sources), _scb_manifest(scb_sources))


def test_duplicate_case_colliding_scb_source_fails_closed() -> None:
    sources = _sources(2)
    scb = _scb_manifest(sources)
    duplicate = copy.deepcopy(scb["entries"][0])
    duplicate["sourcePath"] = str(duplicate["sourcePath"]).upper()
    scb["entries"].append(duplicate)

    with pytest.raises(MapScriptClosureError, match="canonical|case-collides"):
        build_map_script_closure(_map_manifest(sources), scb)


def test_tampered_handoff_evidence_fails_even_with_resealed_map_identity() -> None:
    sources = _sources(1)
    manifest = _map_manifest(sources)
    manifest["handoffs"][0]["handoffEvidenceSha256"] = _sha256("tampered")
    _reseal_map(manifest)

    with pytest.raises(MapScriptClosureError, match="handoff evidence seal"):
        build_map_script_closure(manifest, _scb_manifest(sources))


def test_tampered_map_or_scb_identity_fails_closed() -> None:
    sources = _sources(1)
    map_manifest = _map_manifest(sources)
    map_manifest["identitySha256"] = _sha256("wrong-map-identity")
    with pytest.raises(MapScriptClosureError, match="map-native identity"):
        build_map_script_closure(map_manifest, _scb_manifest(sources))

    scb_manifest = _scb_manifest(sources)
    scb_manifest["identitySha256"] = _sha256("wrong-scb-identity")
    with pytest.raises(MapScriptClosureError, match="SCB identity"):
        build_map_script_closure(_map_manifest(sources), scb_manifest)


def test_tampered_backtest_fails_even_with_resealed_scb_identity() -> None:
    sources = _sources(1)
    manifest = _scb_manifest(sources)
    manifest["outputs"][0]["backtestEvidence"]["accepted"] = False
    _reseal_scb(manifest)

    with pytest.raises(MapScriptClosureError, match="exact-wire backtest"):
        build_map_script_closure(_map_manifest(sources), manifest)


def test_tampered_output_and_unreferenced_output_fail_closed() -> None:
    sources = _sources(1)
    output_tamper = _scb_manifest(sources)
    output_tamper["outputs"][0]["nativeBytes"] += 1
    with pytest.raises(MapScriptClosureError, match="summary|output-tree|identity"):
        build_map_script_closure(_map_manifest(sources), output_tamper)

    unreferenced = _scb_manifest(sources)
    unreferenced["entries"] = []
    with pytest.raises(MapScriptClosureError, match="unreferenced output"):
        build_map_script_closure(_map_manifest(sources), unreferenced)


def test_mapping_key_reorder_and_repeat_are_deterministic() -> None:
    sources = _sources(3)
    map_manifest = _map_manifest(sources)
    scb_manifest = _scb_manifest(sources)
    reordered_map = dict(reversed(list(map_manifest.items())))
    reordered_scb = dict(reversed(list(scb_manifest.items())))

    first = build_map_script_closure(reordered_map, reordered_scb)
    second = build_map_script_closure(map_manifest, scb_manifest)
    third = build_map_script_closure(map_manifest, scb_manifest)

    assert first == second == third
    assert first.neutral() == second.neutral() == third.neutral()


def test_canonical_manifest_bytes_are_accepted_noncanonical_bytes_are_not() -> None:
    sources = _sources(1)
    map_manifest = _map_manifest(sources)
    scb_manifest = _scb_manifest(sources)
    map_raw = closure_module._pretty_json_bytes(map_manifest, ensure_ascii=True)
    scb_raw = closure_module._pretty_json_bytes(scb_manifest, ensure_ascii=False)

    assert build_map_script_closure(map_raw, scb_raw).complete
    with pytest.raises(MapScriptClosureError, match="encoding is not canonical"):
        build_map_script_closure(json.dumps(map_manifest).encode("utf-8"), scb_raw)


@pytest.mark.parametrize(
    "bad_map",
    [
        b'{"schema":1,"schema":2}\n',
        b'{"value":NaN}\n',
        b"[]\n",
        b"\xff",
    ],
)
def test_bad_json_duplicate_keys_nan_and_nonobjects_fail(bad_map: bytes) -> None:
    with pytest.raises(MapScriptClosureError, match="invalid JSON|root"):
        build_map_script_closure(bad_map, _scb_manifest(_sources(1)))


def test_unsafe_and_noncanonical_paths_fail_closed() -> None:
    sources = _sources(1)
    manifest = _map_manifest(sources)
    manifest["handoffs"][0]["sourcePath"] = "../escape.scb"
    with pytest.raises(MapScriptClosureError, match="path is unsafe"):
        build_map_script_closure(manifest, _scb_manifest(sources))

    scb_manifest = _scb_manifest(sources)
    scb_manifest["entries"][0]["sourcePath"] = "data\\maps\\script.scb"
    with pytest.raises(MapScriptClosureError, match="not canonical"):
        build_map_script_closure(_map_manifest(sources), scb_manifest)


def test_nonfinite_mapping_and_different_empty_source_manifests_fail_closed() -> None:
    sources = _sources(1)
    nonfinite = _scb_manifest(sources)
    nonfinite["source"]["manifestTotalBytes"] = float("nan")
    with pytest.raises(MapScriptClosureError, match="canonical JSON"):
        build_map_script_closure(_map_manifest(sources), nonfinite)

    with pytest.raises(MapScriptClosureError, match="no exact source join"):
        build_map_script_closure(_map_manifest([]), _scb_manifest([]))
