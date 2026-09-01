#!/usr/bin/env python3
"""Focused public-fixture gate for the storage-free semantic contract."""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
IMPORTER = ROOT / "importer"
TESTS = IMPORTER / "tests"
TEST_FILE = TESTS / "test_rotwk_202_semantic_graph_contract.py"
ITEM_ID = "P0-CORPUS-SCHEMA-CORE-002"
ARTIFACT_RELATIVE = Path("workspace") / "logs" / ITEM_ID / "semantic-contract-core.json"
IMPLEMENTATION_PATHS = (
    "importer/openbfme_importer/semantic_graph_contract.py",
    "importer/tests/test_rotwk_202_semantic_graph_contract.py",
    "tools/check-rotwk-202-semantic-contract.py",
)
REGISTRY_ORDER = (
    "INPUT_ARTIFACT_DIGESTS", "BOUND_INPUT_IDS", "SOURCE_SCHEMA_INPUT_SHAPE",
    "NODE_TABLES", "ADMIN_TABLES", "ALL_TABLES", "DOMAIN_KEYS", "ROOT_IDS",
    "RESIDUAL_KINDS", "PARSER_FAMILIES", "COVERAGE_DISPOSITIONS",
    "OCCURRENCE_KINDS", "CANDIDATE_FAMILIES", "ASSET_SUFFIXES",
    "SKIRMISH_KINDS", "WOTR_KINDS", "CAH_KINDS", "CAMPAIGN_KINDS",
    "SHELL_KINDS", "IDENTITY_REGISTRY", "EDGE_DECLARATIONS", "EDGE_SPECS",
    "EDGE_KINDS", "OCCURRENCE_SOURCES", "CANDIDATE_TABLE_BY_FAMILY",
    "OCCURRENCE_ALLOWED_STATES", "OCCURRENCE_OWNER_LIFT",
    "OCCURRENCE_RESIDUALS", "OCCURRENCE_EDGE_DIRECTIONS",
    "DISCRIMINATED_FKS", "RESOLUTION_CARDINALITY", "EVIDENCE_VARIANTS",
    "TRAVERSAL_COMMON", "TRAVERSAL_EFFECTIVE", "ROOT_SPECS",
    "SELECTOR_STATE_RULES", "DOMAIN_ROOTS", "MAP_MODE_RULES",
    "PARSER_DISPATCH", "BOUND_SPECS", "BOUNDS", "ROW_LIMIT_PRECEDENCE",
    "LIVE_INVARIANTS", "TABLE_SPECS",
)

EXPECTED_CANONICAL_ATOM_VECTOR_SHA256 = (
    "d0a281275c9f25331320fc7fed10046cc8f07a5be443c4df897d65f96831387a"
)
EXPECTED_FIELD_CONTRACT_SHA256 = (
    "c52402837d60d07817ea7a2da6c194fc5e351ba8dd5ed808ad5fbeb8eb3ce0d2"
)


class ArtifactError(RuntimeError):
    pass


def _json_value(value):
    if dataclasses.is_dataclass(value):
        return {
            field.name: _json_value(getattr(value, field.name))
            for field in dataclasses.fields(value)
        }
    if isinstance(value, (tuple, list)):
        return [_json_value(item) for item in value]
    if isinstance(value, dict):
        if not all(isinstance(key, str) for key in value):
            raise ArtifactError("artifact maps require string keys")
        return {key: _json_value(item) for key, item in value.items()}
    if value is None or type(value) in (str, int, bool):
        return value
    raise ArtifactError(f"artifact value is not closed JSON: {type(value).__name__}")


def canonical_json_bytes(value, *, trailing_lf: bool = True) -> bytes:
    encoded = json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")
    return encoded + (b"\n" if trailing_lf else b"")


def _portable_source_bytes(path: Path) -> bytes:
    raw = path.read_bytes()
    if raw.startswith(b"\xef\xbb\xbf") or b"\x00" in raw:
        raise ArtifactError(f"non-portable implementation source: {path.name}")
    text = raw.decode("utf-8").replace("\r\n", "\n").replace("\r", "\n")
    return text.encode("utf-8")


def implementation_closure(root: Path) -> dict:
    rows = []
    for relative in IMPLEMENTATION_PATHS:
        source = _portable_source_bytes(root / Path(relative))
        rows.append({
            "path": relative,
            "sha256": hashlib.sha256(source).hexdigest(),
            "bytes": len(source),
        })
    closure_bytes = "".join(
        f"{row['path']}|{row['sha256']}\n" for row in rows
    ).encode("utf-8")
    return {
        "hashProfile": "utf8-lf",
        "pathOrder": list(IMPLEMENTATION_PATHS),
        "files": rows,
        "closureSha256": hashlib.sha256(closure_bytes).hexdigest(),
    }


def registry_entries(contract_module) -> list[dict]:
    return [
        {"name": name, "value": _json_value(getattr(contract_module, name))}
        for name in REGISTRY_ORDER
    ]


def build_artifact_document(root: Path) -> dict:
    sys.path.insert(0, str(root / "importer"))
    from openbfme_importer import semantic_graph_contract as contract

    entries = registry_entries(contract)
    registry_sha256 = hashlib.sha256(
        canonical_json_bytes(entries, trailing_lf=False)
    ).hexdigest()
    document = {
        "schema": "openbfme.rotwk-202-semantic-contract-core",
        "schemaVersion": 1,
        "semanticSchema": contract.SCHEMA_NAME,
        "semanticSchemaVersion": contract.SCHEMA_VERSION,
        "baselineId": contract.BASELINE_ID,
        "normativeDesignSha256": contract.NORMATIVE_DESIGN_SHA256,
        "registryOrder": list(REGISTRY_ORDER),
        "registries": entries,
        "counts": {
            "registryCount": len(entries),
            "tableCount": len(contract.ALL_TABLES),
            "nodeTableCount": len(contract.NODE_TABLES),
            "adminTableCount": len(contract.ADMIN_TABLES),
            "edgeKindCount": len(contract.EDGE_KINDS),
            "occurrenceSourceCount": len(contract.OCCURRENCE_SOURCES),
            "rootCount": len(contract.ROOT_IDS),
            "residualKindCount": len(contract.RESIDUAL_KINDS),
            "boundCount": len(contract.BOUND_SPECS),
        },
        "expected": {
            "canonicalAtomVectorSha256": EXPECTED_CANONICAL_ATOM_VECTOR_SHA256,
            "fieldContractSha256": EXPECTED_FIELD_CONTRACT_SHA256,
            "registryPayloadSha256": registry_sha256,
        },
        "implementationClosure": implementation_closure(root),
    }
    document["contentSha256"] = hashlib.sha256(
        canonical_json_bytes(document, trailing_lf=False)
    ).hexdigest()
    return document


def artifact_bytes(root: Path) -> bytes:
    return canonical_json_bytes(build_artifact_document(root))


def isolated_artifact_bytes(root: Path) -> bytes:
    command = (
        sys.executable, "-I", "-B", str(root / "tools" / Path(__file__).name),
        "--serialize-internal",
    )
    completed = subprocess.run(
        command, cwd=root, check=False, capture_output=True,
    )
    if completed.returncode != 0 or completed.stderr:
        raise ArtifactError("isolated artifact serializer failed")
    return completed.stdout


def _reject_float(_value):
    raise ArtifactError("artifact floats are forbidden")


def _object_without_duplicates(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ArtifactError(f"duplicate artifact key: {key}")
        result[key] = value
    return result


def parse_artifact(raw: bytes) -> dict:
    if raw.startswith(b"\xef\xbb\xbf") or not raw.endswith(b"\n") or raw.endswith(b"\n\n") or b"\r" in raw:
        raise ArtifactError("artifact is not canonical UTF-8 LF JSON")
    try:
        document = json.loads(
            raw[:-1].decode("utf-8"), parse_float=_reject_float,
            parse_constant=_reject_float, object_pairs_hook=_object_without_duplicates,
        )
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ArtifactError("artifact JSON parse failed") from exc
    if not isinstance(document, dict) or canonical_json_bytes(document) != raw:
        raise ArtifactError("artifact bytes are not canonical")
    claimed = document.get("contentSha256")
    body = dict(document)
    body.pop("contentSha256", None)
    if claimed != hashlib.sha256(canonical_json_bytes(body, trailing_lf=False)).hexdigest():
        raise ArtifactError("artifact content digest mismatch")
    return document


def _fixture_module():
    name = "_openbfme_semantic_contract_fixture"
    module = sys.modules.get(name)
    if module is not None:
        return module
    spec = importlib.util.spec_from_file_location(name, TEST_FILE)
    if spec is None or spec.loader is None:
        raise ArtifactError("cannot load independent artifact fixture")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def validate_artifact(raw: bytes, root: Path) -> dict:
    document = parse_artifact(raw)
    expected = build_artifact_document(root)
    if document != expected:
        raise ArtifactError("artifact is stale or not reconstructable from implementation")
    _fixture_module().validate_contract_artifact(document, root)
    return document


def _has_reparse_point(path: Path) -> bool:
    attributes = getattr(path.lstat(), "st_file_attributes", 0)
    return bool(attributes & getattr(__import__("stat"), "FILE_ATTRIBUTE_REPARSE_POINT", 0))


def _assert_output_path(root: Path, output: Path) -> None:
    root = Path(os.path.abspath(root))
    output = Path(os.path.abspath(output))
    expected = Path(os.path.abspath(root / ARTIFACT_RELATIVE))
    if output != expected:
        raise ArtifactError("artifact output is outside declared lane log root")
    cursor = output
    while True:
        if cursor.is_symlink() or cursor.exists() and _has_reparse_point(cursor):
            raise ArtifactError("artifact output crosses a reparse point")
        if cursor == root:
            return
        if cursor.parent == cursor:
            break
        cursor = cursor.parent
    raise ArtifactError("artifact output is outside repository root")


def _atomic_write(output: Path, raw: bytes) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", dir=output.parent, prefix=".semantic-contract-core.",
            suffix=".tmp", delete=False,
        ) as stream:
            temporary = Path(stream.name)
            stream.write(raw)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, output)
        temporary = None
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def _publish_bytes(output: Path, raw: bytes) -> None:
    if output.exists():
        if not output.is_file() or output.read_bytes() != raw:
            raise ArtifactError("existing artifact is stale")
    else:
        _atomic_write(output, raw)
    if not output.is_file() or output.read_bytes() != raw:
        raise ArtifactError("published artifact is missing or changed")


def publish_artifact(root: Path) -> Path:
    output = root / ARTIFACT_RELATIVE
    _assert_output_path(root, output)
    output.parent.mkdir(parents=True, exist_ok=True)
    _assert_output_path(root, output)
    first = isolated_artifact_bytes(root)
    second = isolated_artifact_bytes(root)
    if first != second:
        raise ArtifactError("isolated artifact serializations differ")
    validate_artifact(first, root)
    _publish_bytes(output, first)
    _assert_output_path(root, output)
    validate_artifact(output.read_bytes(), root)
    return output


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--serialize-internal", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()

    if args.serialize_internal:
        sys.stdout.buffer.write(artifact_bytes(ROOT))
        return 0

    sys.path.insert(0, str(IMPORTER))
    suite = unittest.defaultTestLoader.discover(
        str(TESTS), pattern="test_rotwk_202_semantic_graph_contract.py"
    )
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if not result.wasSuccessful():
        return 1
    publish_artifact(ROOT)
    print("ROTWK_202_SEMANTIC_CONTRACT PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
