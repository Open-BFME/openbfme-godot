#!/usr/bin/env python3
"""Exact focused gate for the bounded semantic graph STORE contract."""

from __future__ import annotations

import argparse
from dataclasses import fields
import hashlib
import importlib.util
import inspect
import io
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
TEST_FILE = TESTS / "test_rotwk_202_semantic_graph_store.py"
ITEM_ID = "P0-CORPUS-SCHEMA-STORE-001"
ARTIFACT_RELATIVE = Path("workspace/logs/P0-CORPUS-SCHEMA-STORE-001/semantic-store-contract.json")
SCHEMA_DESIGN_RELATIVE = Path("workspace/logs/P0-CORPUS-SCHEMA-001/schema-design-v4.md")
STORE_DESIGN_RELATIVE = Path("workspace/logs/P0-CORPUS-SCHEMA-STORE-001/store-design-v2.md")
CORE_RELATIVE = Path("workspace/logs/P0-CORPUS-SCHEMA-CORE-002/semantic-contract-core.json")
SCHEMA_DESIGN_SHA256 = "d5f9d8fc12a631156b4c3b73737f23faae8e40c71b7577a800e06f6c2e39090c"
STORE_DESIGN_SHA256 = "67aa0fed9029dc8913a6e974576dcc850a4473048213420f7e6ccb172fede750"
CORE_SHA256 = "83dad1c2c2baa297fefb24e77e181fdf8d9c5ff299f14dcf01c694db09caa89a"
CORE_REGISTRY_SHA256 = "2c6b185f7a32002ff705d2ce1859fc704a834ab8f5dfd760be7cbef1671d505b"
CORE_CONTENT_SHA256 = "7360296279b0e26a5165ddca892e33325bc282db7b64f241fed4867e50389d9c"
CORE_CLOSURE_SHA256 = "14ce97b008f07ebda168590cc4c74f0b01bf8179c77cbfaad9688c140130c046"
CORE_IMPLEMENTATION_FILES = (
    ("importer/openbfme_importer/semantic_graph_contract.py", 70_231, "7fb0222805ec6a14d6fc2af687f05847c641c4565695d0fd1c04f396f82480ab"),
    ("importer/tests/test_rotwk_202_semantic_graph_contract.py", 60_365, "af7c3ae8e6ff233f822cae40a9c859345a3d1742dc39d3cce1a3eb9faa5fb51a"),
    ("tools/check-rotwk-202-semantic-contract.py", 11_777, "848ab3e6e802621405ee0151a115dc4bb21b9d774434ff181a9d58b370df0096"),
)
IMPLEMENTATION_PATHS = (
    "importer/openbfme_importer/semantic_graph_store.py",
    "importer/tests/test_rotwk_202_semantic_graph_store.py",
    "tools/check-rotwk-202-semantic-store.py",
)

TABLES = tuple(
    "source_records documents definitions assignments object_occurrences "
    "module_declarations module_kinds assets opaque_tokens script_calls "
    "nested_sites directive_sites unknown_sites maps map_objects map_libraries "
    "apt_movies apt_symbols wnd_windows parser_dispositions evidence_referents "
    "mapcache_selection reference_occurrences edges selector_dispositions "
    "root_results root_memberships root_traversals root_residual_links "
    "domain_dispositions map_dispositions residuals counts".split()
)
NODE_TABLES = TABLES[:20]
ADMIN_TABLES = TABLES[20:]
BOUNDS = (
    ("maxJsonDepth", 16, "E_JSON_DEPTH_LIMIT"),
    ("maxContainerItems", 1_000_000, "E_CONTAINER_ITEMS_LIMIT"),
    ("maxStringUtf8Bytes", 1_048_576, "E_STRING_BYTES_LIMIT"),
    ("maxJsonlLineBytes", 1_048_576, "E_LINE_BYTES_LIMIT"),
    ("maxRowsPerShard", 100_000, "E_SHARD_ROWS_LIMIT"),
    ("maxBytesPerShard", 67_108_864, "E_SHARD_BYTES_LIMIT"),
    ("maxShardsPerTable", 4_096, "E_SHARD_COUNT_LIMIT"),
    ("maxRowsPerNodeTable", 1_000_000, "E_NODE_TABLE_ROWS_LIMIT"),
    ("maxNodeRowsTotal", 5_000_000, "E_NODE_ROWS_TOTAL_LIMIT"),
    ("maxReferenceOccurrences", 5_000_000, "E_OCCURRENCE_ROWS_LIMIT"),
    ("maxEdges", 10_000_000, "E_EDGE_ROWS_LIMIT"),
    ("maxResiduals", 5_000_000, "E_RESIDUAL_ROWS_LIMIT"),
    ("maxRowsPerAdminTable", 10_000_000, "E_ADMIN_TABLE_ROWS_LIMIT"),
    ("maxRowsTotal", 20_000_000, "E_TOTAL_ROWS_LIMIT"),
    ("maxOutputBytes", 8_589_934_592, "E_OUTPUT_BYTES_LIMIT"),
)
STORE_CODES = (
    "E_CORE_CONTRACT", "E_PATH_CONTAINMENT", "E_SQLITE_CONTRACT",
    "E_JSON_ENCODING", "E_JSON_DUPLICATE_KEY", "E_CANONICAL_JSON",
    "E_ROW_SHAPE", "E_ROW_IDENTITY", "E_DUPLICATE_ID", "E_ROW_ORDER",
    "E_SHARD_LAYOUT", "E_MANIFEST_SHAPE", "E_COUNT_MISMATCH",
    "E_DIGEST_MISMATCH", "E_PUBLISH_CONFLICT",
)
PRAGMAS = (
    ("application_id", 1_329_746_765), ("user_version", 1),
    ("encoding", "UTF-8"), ("page_size", 4096),
    ("foreign_keys", "ON"), ("trusted_schema", "OFF"),
    ("journal_mode", "DELETE"), ("synchronous", "FULL"),
    ("query_only", "ON"),
)
TABLE_DDL = (
    ("meta", "CREATE TABLE meta(key TEXT COLLATE BINARY PRIMARY KEY,value BLOB NOT NULL) WITHOUT ROWID"),
    ("rows", "CREATE TABLE rows(table_ord INTEGER NOT NULL,row_id TEXT COLLATE BINARY NOT NULL,row_json BLOB NOT NULL,row_sha256 TEXT COLLATE BINARY NOT NULL,PRIMARY KEY(table_ord,row_id)) WITHOUT ROWID"),
    ("foreign_key_index", "CREATE TABLE foreign_key_index(source_table_ord INTEGER NOT NULL,source_id TEXT COLLATE BINARY NOT NULL,field_ord INTEGER NOT NULL,value_ord INTEGER NOT NULL,target_table_ord INTEGER NOT NULL,target_id TEXT COLLATE BINARY NOT NULL,PRIMARY KEY(source_table_ord,source_id,field_ord,value_ord)) WITHOUT ROWID"),
    ("lookup_index", "CREATE TABLE lookup_index(projection_ord INTEGER NOT NULL,key_json BLOB NOT NULL,sort_integer INTEGER NOT NULL DEFAULT 0,row_table_ord INTEGER NOT NULL,row_id TEXT COLLATE BINARY NOT NULL,PRIMARY KEY(projection_ord,key_json,sort_integer,row_id)) WITHOUT ROWID"),
    ("path_index", "CREATE TABLE path_index(projection_ord INTEGER NOT NULL,path_casefold TEXT COLLATE BINARY NOT NULL,path_exact TEXT COLLATE BINARY NOT NULL,row_table_ord INTEGER NOT NULL,row_id TEXT COLLATE BINARY NOT NULL,PRIMARY KEY(projection_ord,path_casefold,row_id)) WITHOUT ROWID"),
)
INDEX_DDL = (
    ("fk_target", "CREATE INDEX fk_target ON foreign_key_index(target_table_ord,target_id,source_table_ord,source_id,field_ord,value_ord)"),
    ("lookup_row", "CREATE INDEX lookup_row ON lookup_index(row_table_ord,row_id,projection_ord)"),
    ("path_row", "CREATE INDEX path_row ON path_index(row_table_ord,row_id,projection_ord)"),
)
FORBIDDEN = (
    "json1", "fts", "locale-collation", "triggers", "user-functions",
    "ambient-extensions", "sqlite-nocase", "sqlite-lower", "sql-like",
)
LOOKUPS = (
    ("source-key", "source_records", (("key", "ci"),), None, 0, ()),
    ("definition-name", "definitions", (("name", "ci"),), None, 0, ()),
    ("definition-kind-name", "definitions", (("definitionKind", "ascii"), ("name", "ci")), None, 0, ()),
    ("assignment-owner", "assignments", (("ownerTable", "ascii"), ("ownerId", "ascii"), ("field", "ci")), None, 0, ()),
    ("assignment-lexical", "assignments", (("documentId", "ascii"), ("rootKind", "ci"), ("rootName", "ci")), "line", 0, ()),
    ("object-id", "object_occurrences", (("objectId", "ci"),), None, 0, ()),
    ("module-kind", "module_kinds", (("casefoldName", "ci"),), None, 0, ()),
    ("asset-stem", "assets", (("stem", "ci"), ("suffix", "ascii")), None, 0, ()),
    ("asset-companion", "assets", (("path", "ci"), ("assetClass", "ascii")), None, 0, ()),
    ("apt-group", "apt_movies", (("directory", "ci"), ("stem", "ci")), None, 0, ()),
    ("parser-source", "parser_dispositions", (("sourceRecordId", "ascii"), ("parserFamily", "ascii")), None, 0, ()),
    ("mapcache-path", "mapcache_selection", (("virtualPath", "ci"),), None, 0, ()),
    ("edge-source", "edges", (("sourceTable", "ascii"), ("sourceId", "ascii")), None, 0, ()),
    ("edge-source-kind", "edges", (("sourceTable", "ascii"), ("sourceId", "ascii"), ("edgeKind", "ascii")), None, 0, ()),
    ("edge-target", "edges", (("targetTable", "ascii"), ("targetId", "ascii")), None, 0, ()),
    ("occurrence-site", "reference_occurrences", (("siteTable", "ascii"), ("siteId", "ascii")), None, 0, ()),
    ("occurrence-owner", "reference_occurrences", (("ownerTable", "ascii"), ("ownerId", "ascii")), None, 0, ()),
    ("occurrence-target", "reference_occurrences", (("candidateTable", "ascii"), ("targetId", "ascii")), None, 0, ("targetId",)),
    ("selector-root", "selector_dispositions", (("rootId", "ascii"),), None, 0, ()),
    ("root-member-by-root", "root_memberships", (("rootId", "ascii"),), None, 0, ()),
    ("root-member-exact", "root_memberships", (("rootId", "ascii"), ("memberTable", "ascii"), ("memberId", "ascii")), None, 0, ()),
    ("root-traversal-by-root", "root_traversals", (("rootId", "ascii"),), None, 0, ()),
    ("root-residual-by-root", "root_residual_links", (("rootId", "ascii"),), None, 0, ()),
    ("residual-subject", "residuals", (("subjectTable", "ascii"), ("subjectId", "ascii")), None, 0, ()),
    ("domain-node", "domain_dispositions", (("nodeTable", "ascii"), ("nodeId", "ascii")), None, 0, ()),
    ("map-disposition-map", "map_dispositions", (("mapId", "ascii"),), None, 0, ()),
)
PATHS = (
    ("source-path", "source_records", "key"),
    ("document-path", "documents", "path"),
    ("asset-path", "assets", "path"),
    ("map-path", "maps", "path"),
    ("map-library-path", "map_libraries", "pathToken"),
)

PUBLIC_CALLABLES = (
    ("normalize_casefold", "module-function", "normalize_casefold(value: str) -> str", "str"),
    ("canonical_row_bytes", "module-function", "canonical_row_bytes(row: Mapping[str, object]) -> bytes", "bytes"),
    ("StoreRefusal", "exception", "StoreRefusal(diagnostic:StoreDiagnostic) -> StoreRefusal", "StoreRefusal"),
    ("SemanticGraphStore", "class", "SemanticGraphStore", "<not-directly-constructible>"),
    ("SemanticGraphStore.create", "classmethod", "create(database_path, *, core_artifact_path) -> SemanticGraphStore", "SemanticGraphStore"),
    ("SemanticGraphStore.open_readonly", "classmethod", "open_readonly(database_path, *, core_artifact_path) -> SemanticGraphStore", "SemanticGraphStore"),
    ("SemanticGraphStore.ingest_table", "method", "ingest_table(table, rows) -> int", "int"),
    ("SemanticGraphStore.finalize", "method", "finalize() -> StoreSummary", "StoreSummary"),
    ("SemanticGraphStore.get_row", "method", "get_row(table, row_id) -> dict | None", "dict | None"),
    ("SemanticGraphStore.iter_rows", "method", "iter_rows(table) -> Iterator[dict]", "Iterator[dict]"),
    ("SemanticGraphStore.iter_foreign_keys", "method", "iter_foreign_keys(source_table, source_id, field=None) -> Iterator[ForeignKeyRef]", "Iterator[ForeignKeyRef]"),
    ("SemanticGraphStore.iter_references_to", "method", "iter_references_to(target_table, target_id) -> Iterator[ForeignKeyRef]", "Iterator[ForeignKeyRef]"),
    ("SemanticGraphStore.lookup", "method", "lookup(index_name, key) -> Iterator[dict]", "Iterator[dict]"),
    ("SemanticGraphStore.lookup_path", "method", "lookup_path(index_name, path) -> Iterator[dict]", "Iterator[dict]"),
    ("SemanticGraphStore.iter_safe_subtree", "method", "iter_safe_subtree(index_name, root_path) -> Iterator[dict]", "Iterator[dict]"),
    ("SemanticGraphStore.greatest_preceding_assignment", "method", "greatest_preceding_assignment(document_id, root_kind, root_name, before_line) -> dict | None", "dict | None"),
    ("SemanticGraphStore.close", "method", "close() -> None", "None"),
    ("SemanticGraphStore.__enter__", "context-method", "__enter__() -> SemanticGraphStore", "SemanticGraphStore"),
    ("SemanticGraphStore.__exit__", "context-method", "__exit__(exc_type:type[BaseException]|null, exc_value:BaseException|null, traceback:TracebackType|null) -> None", "None"),
    ("publish_canonical_shards", "module-function", "publish_canonical_shards(store, output_directory) -> ManifestSummary", "ManifestSummary"),
    ("validate_canonical_shards", "module-function", "validate_canonical_shards(output_directory, *, core_artifact_path) -> ManifestSummary", "ManifestSummary"),
)
PUBLIC_RECORDS = (
    ("StoreDiagnostic", (("code", "str"), ("table", "str"), ("observed", "int|null"), ("limit", "int|null"), ("row", "str|null"), ("field", "str|null"))),
    ("ForeignKeyRef", (("source_table", "str"), ("source_id", "str"), ("field", "str"), ("value_ordinal", "int"), ("target_table", "str"), ("target_id", "str"))),
    ("StoreSummary", (("core_artifact_sha256", "str"), ("table_counts", "tuple[tuple[str,int],...]"), ("total_rows", "int"), ("foreign_key_count", "int"), ("lookup_count", "int"), ("path_count", "int"))),
    ("ManifestSummary", (("manifest_sha256", "str"), ("shard_set_sha256", "str"), ("table_counts", "tuple[tuple[str,int],...]"), ("total_rows", "int"), ("total_bytes", "int"))),
)


class GateError(RuntimeError):
    pass


def canonical_json_bytes(value, *, trailing_lf=False):
    raw = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")
    return raw + (b"\n" if trailing_lf else b"")


def portable_bytes(path: Path) -> bytes:
    raw = path.read_bytes()
    if raw.startswith(b"\xef\xbb\xbf") or b"\x00" in raw:
        raise GateError(f"non-portable source: {path}")
    return raw.decode("utf-8").replace("\r\n", "\n").replace("\r", "\n").encode("utf-8")


def _closed_json(raw: bytes) -> dict:
    def duplicate(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise GateError("duplicate JSON key")
            result[key] = value
        return result
    def reject(_value):
        raise GateError("float or constant")
    if raw.startswith(b"\xef\xbb\xbf") or b"\r" in raw or not raw.endswith(b"\n") or raw.endswith(b"\n\n"):
        raise GateError("noncanonical JSON framing")
    try:
        document = json.loads(raw[:-1].decode("utf-8"), object_pairs_hook=duplicate, parse_float=reject, parse_constant=reject)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise GateError("JSON parse failed") from exc
    if not isinstance(document, dict) or canonical_json_bytes(document, trailing_lf=True) != raw:
        raise GateError("noncanonical JSON bytes")
    return document


def validate_inputs(root: Path) -> dict:
    for relative, expected in (
        (SCHEMA_DESIGN_RELATIVE, SCHEMA_DESIGN_SHA256),
        (STORE_DESIGN_RELATIVE, STORE_DESIGN_SHA256),
        (CORE_RELATIVE, CORE_SHA256),
    ):
        path = root / relative
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise GateError(f"bound input mismatch: {relative.as_posix()}")
    core_document = _closed_json((root / CORE_RELATIVE).read_bytes())
    body = dict(core_document)
    claimed = body.pop("contentSha256", None)
    if (
        set(core_document) != {
            "baselineId", "contentSha256", "counts", "expected",
            "implementationClosure", "normativeDesignSha256", "registries",
            "registryOrder", "schema", "schemaVersion", "semanticSchema",
            "semanticSchemaVersion",
        }
        or claimed != CORE_CONTENT_SHA256
        or claimed != hashlib.sha256(canonical_json_bytes(body)).hexdigest()
    ):
        raise GateError("core content digest mismatch")
    if (
        core_document.get("schema") != "openbfme.rotwk-202-semantic-contract-core"
        or core_document.get("schemaVersion") != 1
        or core_document.get("semanticSchema") != "openbfme.rotwk-202-semantic-schema-manifest"
        or core_document.get("semanticSchemaVersion") != 4
        or core_document.get("baselineId") != "rotwk-202-v9.7.7-en"
        or core_document.get("normativeDesignSha256") != SCHEMA_DESIGN_SHA256
        or core_document.get("expected", {}).get("registryPayloadSha256") != CORE_REGISTRY_SHA256
    ):
        raise GateError("core identity drift")
    closure = core_document.get("implementationClosure", {})
    expected_closure_rows = [
        {"path": path, "bytes": byte_count, "sha256": digest}
        for path, byte_count, digest in CORE_IMPLEMENTATION_FILES
    ]
    if closure != {
        "closureSha256": CORE_CLOSURE_SHA256,
        "files": expected_closure_rows,
        "hashProfile": "utf8-lf",
        "pathOrder": [row[0] for row in CORE_IMPLEMENTATION_FILES],
    }:
        raise GateError("core closure declaration drift")
    closure_rows = []
    for row in expected_closure_rows:
        payload = portable_bytes(root / row["path"])
        digest = hashlib.sha256(payload).hexdigest()
        if row != {"path": row["path"], "bytes": len(payload), "sha256": digest}:
            raise GateError("core closure file drift")
        closure_rows.append(f"{row['path']}|{digest}\n")
    if CORE_CLOSURE_SHA256 != hashlib.sha256("".join(closure_rows).encode()).hexdigest():
        raise GateError("core closure drift")
    return core_document


def implementation_closure(root: Path) -> dict:
    rows = []
    for relative in IMPLEMENTATION_PATHS:
        payload = portable_bytes(root / relative)
        rows.append({"path": relative, "bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest()})
    return {
        "hashProfile": "utf8-lf", "pathOrder": list(IMPLEMENTATION_PATHS),
        "files": rows,
        "closureSha256": hashlib.sha256("".join(f"{row['path']}|{row['sha256']}\n" for row in rows).encode()).hexdigest(),
    }


def public_api_document() -> dict:
    return {
        "callables": [
            {"name": name, "kind": kind, "signature": signature, "returnType": return_type}
            for name, kind, signature, return_type in PUBLIC_CALLABLES
        ],
        "records": [
            {"name": name, "fields": [{"name": field, "type": type_name} for field, type_name in record_fields]}
            for name, record_fields in PUBLIC_RECORDS
        ],
    }


def sqlite_document() -> dict:
    return {
        "applicationId": 1_329_746_765, "userVersion": 1,
        "encoding": "UTF-8", "pageSize": 4096,
        "pragmas": [{"name": name, "value": value} for name, value in PRAGMAS],
        "tables": [{"name": name, "ddl": ddl} for name, ddl in TABLE_DDL],
        "indexes": [{"name": name, "ddl": ddl} for name, ddl in INDEX_DDL],
        "forbiddenFeatures": list(FORBIDDEN),
    }


def projection_document() -> list[dict]:
    return [
        {"name": name, "table": table, "key": [{"field": field, "normalization": normalization} for field, normalization in key], "sortField": sort_field, "sortDefault": sort_default, "omitNullFields": list(omit)}
        for name, table, key, sort_field, sort_default, omit in LOOKUPS
    ]


def path_projection_document() -> list[dict]:
    return [{"name": name, "table": table, "field": field} for name, table, field in PATHS]


def shard_document() -> dict:
    return {
        "layout": "tables/<table>/<six-digit-shard-index>.jsonl",
        "rowEncoding": "canonical-json-no-lf + 0x0a",
        "manifestSchema": "openbfme.rotwk-202-semantic-schema-manifest",
        "manifestSchemaVersion": 4,
        "tableOrder": list(TABLES), "nodeTables": list(NODE_TABLES),
        "adminTables": list(ADMIN_TABLES),
        "manifestFieldOrder": ["schema", "schemaVersion", "baselineId", "tableOrder", "nodeTables", "adminTables", "bounds", "tables", "totalRows", "totalBytes", "shardSetSha256"],
        "shardFieldOrder": ["path", "shardIndex", "rowCount", "bytes", "firstId", "lastId", "sha256"],
        "tableHashRule": "sha256(concatenated shard bytes in shardIndex order)",
        "shardSetHashRule": "sha256(utf8(table|path|sha256|bytes|rowCount\\n) in table/shard order)",
        "totalBytesRule": "sum(shard bytes); manifest.json excluded",
        "publicationRules": ["ignored-workspace-contained", "no-reparse", "untracked-destination", "fresh-sibling-temp", "validate-before-publish", "stale-existing-refuse-unchanged", "identical-existing-validate-idempotent", "single-directory-rename", "remove-only-owned-temp-on-failure"],
    }


def diagnostic_document() -> dict:
    stages = (
        "core-contract-and-containment", "raw-line-bytes", "value-walk",
        "row-shape-identity-duplicate-order", "accepted-aggregate-bounds",
        "physical-shard-and-output", "manifest-claims", "digests",
        "publication-conflict",
    )
    return {
        "marker": "ROTWK_202_SEMANTIC_STORE REFUSE",
        "fieldOrder": ["code", "table", "observed", "limit", "row", "field"],
        "resourceCodes": [code for _, _, code in BOUNDS] + ["E_INTEGER_RANGE"],
        "storeCodes": list(STORE_CODES),
        "precedence": [{"ordinal": index, "stage": stage} for index, stage in enumerate(stages)],
        "resourceObservedLimitPolicy": "section-12-integers; section-12-null-sites-remain-null",
        "storeObservedLimitPolicy": "null when no numeric bound exists",
        "semanticValidationOwner": "P0-CORPUS-SCHEMA-CONFORMANCE-001",
    }


def validate_runtime_literals() -> None:
    sys.path.insert(0, str(IMPORTER))
    from openbfme_importer import semantic_graph_contract as core
    from openbfme_importer import semantic_graph_store as runtime
    if tuple(core.ALL_TABLES) != TABLES or tuple(core.NODE_TABLES) != NODE_TABLES or tuple(core.ADMIN_TABLES) != ADMIN_TABLES:
        raise GateError("accepted table registry drift")
    if tuple((item.name, item.maximum, item.refusal_code) for item in core.BOUND_SPECS) != BOUNDS:
        raise GateError("accepted bound registry drift")
    if runtime._TABLE_DDL != TABLE_DDL or runtime._INDEX_DDL != INDEX_DDL:
        raise GateError("runtime DDL drift")
    if runtime._LOOKUP_PROJECTIONS != LOOKUPS or runtime._PATH_PROJECTIONS != PATHS:
        raise GateError("runtime projection drift")
    if runtime._STORE_CODES != STORE_CODES:
        raise GateError("runtime diagnostic drift")
    if runtime.__all__ != (
        "normalize_casefold", "canonical_row_bytes", "StoreDiagnostic",
        "StoreRefusal", "StoreSummary", "ManifestSummary", "ForeignKeyRef",
        "SemanticGraphStore", "publish_canonical_shards", "validate_canonical_shards",
    ):
        raise GateError("runtime export drift")
    if (
        not inspect.isfunction(runtime.normalize_casefold)
        or not inspect.isfunction(runtime.canonical_row_bytes)
        or not issubclass(runtime.StoreRefusal, Exception)
        or runtime.StoreRefusal.__slots__ != ("diagnostic",)
        or not isinstance(runtime.SemanticGraphStore, type)
        or not isinstance(runtime.SemanticGraphStore.__dict__.get("create"), classmethod)
        or not isinstance(runtime.SemanticGraphStore.__dict__.get("open_readonly"), classmethod)
        or tuple(
            name for name in runtime.SemanticGraphStore.__dict__
            if not name.startswith("_") or name in ("__enter__", "__exit__")
        ) != tuple(row[0].split(".")[-1] for row in PUBLIC_CALLABLES[4:19])
    ):
        raise GateError("runtime callable kind/order drift")
    refusal_signature = inspect.signature(runtime.StoreRefusal)
    refusal_parameters = tuple(refusal_signature.parameters.values())
    if (
        len(refusal_parameters) != 1
        or refusal_parameters[0].name != "diagnostic"
        or refusal_parameters[0].kind is not inspect.Parameter.POSITIONAL_OR_KEYWORD
        or refusal_parameters[0].default is not inspect.Parameter.empty
        or str(refusal_parameters[0].annotation).replace(" ", "") != "StoreDiagnostic"
    ):
        raise GateError("runtime StoreRefusal signature drift")
    record_classes = (runtime.StoreDiagnostic, runtime.ForeignKeyRef, runtime.StoreSummary, runtime.ManifestSummary)
    if tuple(record.__name__ for record in record_classes) != tuple(name for name, _ in PUBLIC_RECORDS):
        raise GateError("public record order drift")
    for record, (_, expected_fields) in zip(record_classes, PUBLIC_RECORDS):
        if tuple(field.name for field in fields(record)) != tuple(name for name, _ in expected_fields):
            raise GateError("public record field drift")
        observed_types = tuple(str(record.__annotations__[name]).replace(" ", "").replace("None", "null") for name, _ in expected_fields)
        expected_types = tuple(type_name.replace(" ", "") for _, type_name in expected_fields)
        if observed_types != expected_types:
            raise GateError("public record type drift")
    expected_parameters = {
        "normalize_casefold": (("value", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "str"),),
        "canonical_row_bytes": (("row", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "Mapping[str,object]"),),
        "SemanticGraphStore.create": (("database_path", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "str|os.PathLike[str]"), ("core_artifact_path", inspect.Parameter.KEYWORD_ONLY, False, "str|os.PathLike[str]")),
        "SemanticGraphStore.open_readonly": (("database_path", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "str|os.PathLike[str]"), ("core_artifact_path", inspect.Parameter.KEYWORD_ONLY, False, "str|os.PathLike[str]")),
        "SemanticGraphStore.ingest_table": (("table", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "str"), ("rows", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "object")),
        "SemanticGraphStore.finalize": (),
        "SemanticGraphStore.get_row": (("table", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "str"), ("row_id", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "str")),
        "SemanticGraphStore.iter_rows": (("table", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "str"),),
        "SemanticGraphStore.iter_foreign_keys": (("source_table", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "str"), ("source_id", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "str"), ("field", inspect.Parameter.POSITIONAL_OR_KEYWORD, True, "str|None")),
        "SemanticGraphStore.iter_references_to": (("target_table", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "str"), ("target_id", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "str")),
        "SemanticGraphStore.lookup": (("index_name", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "str"), ("key", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "Sequence[object]")),
        "SemanticGraphStore.lookup_path": (("index_name", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "str"), ("path", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "str")),
        "SemanticGraphStore.iter_safe_subtree": (("index_name", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "str"), ("root_path", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "str")),
        "SemanticGraphStore.greatest_preceding_assignment": (("document_id", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "str"), ("root_kind", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "str"), ("root_name", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "str|None"), ("before_line", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "int")),
        "SemanticGraphStore.close": (), "SemanticGraphStore.__enter__": (),
        "SemanticGraphStore.__exit__": (("exc_type", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "type[BaseException]|None"), ("exc_value", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "BaseException|None"), ("traceback", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "TracebackType|None")),
        "publish_canonical_shards": (("store", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "SemanticGraphStore"), ("output_directory", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "str|os.PathLike[str]")),
        "validate_canonical_shards": (("output_directory", inspect.Parameter.POSITIONAL_OR_KEYWORD, False, "str|os.PathLike[str]"), ("core_artifact_path", inspect.Parameter.KEYWORD_ONLY, False, "str|os.PathLike[str]")),
    }
    runtime_objects = {
        "normalize_casefold": runtime.normalize_casefold,
        "canonical_row_bytes": runtime.canonical_row_bytes,
        "SemanticGraphStore.create": runtime.SemanticGraphStore.create,
        "SemanticGraphStore.open_readonly": runtime.SemanticGraphStore.open_readonly,
        "SemanticGraphStore.ingest_table": runtime.SemanticGraphStore.ingest_table,
        "SemanticGraphStore.finalize": runtime.SemanticGraphStore.finalize,
        "SemanticGraphStore.get_row": runtime.SemanticGraphStore.get_row,
        "SemanticGraphStore.iter_rows": runtime.SemanticGraphStore.iter_rows,
        "SemanticGraphStore.iter_foreign_keys": runtime.SemanticGraphStore.iter_foreign_keys,
        "SemanticGraphStore.iter_references_to": runtime.SemanticGraphStore.iter_references_to,
        "SemanticGraphStore.lookup": runtime.SemanticGraphStore.lookup,
        "SemanticGraphStore.lookup_path": runtime.SemanticGraphStore.lookup_path,
        "SemanticGraphStore.iter_safe_subtree": runtime.SemanticGraphStore.iter_safe_subtree,
        "SemanticGraphStore.greatest_preceding_assignment": runtime.SemanticGraphStore.greatest_preceding_assignment,
        "SemanticGraphStore.close": runtime.SemanticGraphStore.close,
        "SemanticGraphStore.__enter__": runtime.SemanticGraphStore.__enter__,
        "SemanticGraphStore.__exit__": runtime.SemanticGraphStore.__exit__,
        "publish_canonical_shards": runtime.publish_canonical_shards,
        "validate_canonical_shards": runtime.validate_canonical_shards,
    }
    for name, expected in expected_parameters.items():
        signature = inspect.signature(runtime_objects[name])
        parameters = list(signature.parameters.values())
        if parameters and parameters[0].name == "self":
            parameters.pop(0)
        observed = tuple(
            (
                parameter.name, parameter.kind,
                parameter.default is not inspect.Parameter.empty,
                str(parameter.annotation).replace(" ", ""),
            )
            for parameter in parameters
        )
        if observed != expected:
            raise GateError(f"runtime callable signature drift: {name}")
        declared_return = str(signature.return_annotation).replace(" ", "").replace("NoneType", "None")
        expected_return = next(row[3] for row in PUBLIC_CALLABLES if row[0] == name).replace(" ", "")
        if declared_return != expected_return:
            raise GateError(f"runtime callable return drift: {name}")


def build_artifact_document(root: Path) -> dict:
    core_document = validate_inputs(root)
    validate_runtime_literals()
    public_api = public_api_document()
    sqlite_contract = sqlite_document()
    projections = projection_document()
    path_projections = path_projection_document()
    shard_contract = shard_document()
    bounds = [
        {"name": name, "value": value, "code": code, "precedenceOrdinal": ordinal}
        for ordinal, (name, value, code) in enumerate(BOUNDS)
    ]
    diagnostics = diagnostic_document()
    document = {
        "schema": "openbfme.rotwk-202-semantic-store-contract",
        "schemaVersion": 1, "workItemId": ITEM_ID,
        "baselineId": "rotwk-202-v9.7.7-en",
        "semanticSchema": "openbfme.rotwk-202-semantic-schema-manifest",
        "semanticSchemaVersion": 4,
        "normativeDesign": {
            "schema": {"id": "OWNER-SEMANTIC-SCHEMA-V4-D5F9D8FC", "path": SCHEMA_DESIGN_RELATIVE.as_posix(), "sha256": SCHEMA_DESIGN_SHA256},
            "store": {"id": "OWNER-SEMANTIC-STORE-V2-67AA0FED", "path": STORE_DESIGN_RELATIVE.as_posix(), "sha256": STORE_DESIGN_SHA256},
        },
        "coreContract": {
            "id": "E-SEMANTIC-CORE-002-83DAD1C2", "path": CORE_RELATIVE.as_posix(),
            "externalSha256": CORE_SHA256,
            "artifactSchema": core_document["schema"],
            "artifactSchemaVersion": core_document["schemaVersion"],
            "semanticSchema": core_document["semanticSchema"],
            "semanticSchemaVersion": core_document["semanticSchemaVersion"],
            "baselineId": core_document["baselineId"],
            "registryPayloadSha256": core_document["expected"]["registryPayloadSha256"],
            "contentSha256": core_document["contentSha256"],
            "closureSha256": core_document["implementationClosure"]["closureSha256"],
        },
        "publicApiOrder": [row[0] for row in PUBLIC_CALLABLES],
        "publicRecordOrder": [row[0] for row in PUBLIC_RECORDS],
        "publicApi": public_api,
        "sqliteContract": sqlite_contract,
        "projectionOrder": [row[0] for row in LOOKUPS],
        "projections": projections,
        "pathProjectionOrder": [row[0] for row in PATHS],
        "pathProjections": path_projections,
        "shardContract": shard_contract,
        "bounds": bounds,
        "diagnosticContract": diagnostics,
        "implementationClosure": implementation_closure(root),
        "expected": {
            "ownerStoreDesignSha256": STORE_DESIGN_SHA256,
            "coreArtifactSha256": CORE_SHA256,
            "publicApiSha256": hashlib.sha256(canonical_json_bytes(public_api)).hexdigest(),
            "sqliteContractSha256": hashlib.sha256(canonical_json_bytes(sqlite_contract)).hexdigest(),
            "projectionsSha256": hashlib.sha256(canonical_json_bytes(projections)).hexdigest(),
            "pathProjectionsSha256": hashlib.sha256(canonical_json_bytes(path_projections)).hexdigest(),
            "shardContractSha256": hashlib.sha256(canonical_json_bytes(shard_contract)).hexdigest(),
            "boundsSha256": hashlib.sha256(canonical_json_bytes(bounds)).hexdigest(),
            "diagnosticContractSha256": hashlib.sha256(canonical_json_bytes(diagnostics)).hexdigest(),
        },
    }
    document["contentSha256"] = hashlib.sha256(canonical_json_bytes(document)).hexdigest()
    return document


def artifact_bytes(root: Path) -> bytes:
    return canonical_json_bytes(build_artifact_document(root), trailing_lf=True)


def parse_artifact(raw: bytes) -> dict:
    document = _closed_json(raw)
    body = dict(document)
    claimed = body.pop("contentSha256", None)
    if claimed != hashlib.sha256(canonical_json_bytes(body)).hexdigest():
        raise GateError("artifact content digest mismatch")
    return document


def isolated_artifact_bytes(root: Path) -> bytes:
    completed = subprocess.run(
        (sys.executable, "-I", "-B", str(root / "tools/check-rotwk-202-semantic-store.py"), "--serialize-internal"),
        cwd=root, check=False, capture_output=True,
    )
    if completed.returncode != 0 or completed.stderr:
        raise GateError("isolated serializer failed")
    return completed.stdout


def fixture_module():
    name = "_openbfme_semantic_store_fixture"
    if name in sys.modules:
        return sys.modules[name]
    spec = importlib.util.spec_from_file_location(name, TEST_FILE)
    if spec is None or spec.loader is None:
        raise GateError("cannot load independent fixture")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def validate_artifact(raw: bytes, root: Path) -> dict:
    document = parse_artifact(raw)
    if document != build_artifact_document(root):
        raise GateError("artifact is stale")
    fixture_module().validate_store_artifact(document, root)
    return document


def _has_reparse(path: Path) -> bool:
    attributes = getattr(path.lstat(), "st_file_attributes", 0)
    return path.is_symlink() or bool(attributes & getattr(__import__("stat"), "FILE_ATTRIBUTE_REPARSE_POINT", 0))


def assert_output_path(root: Path, output: Path) -> None:
    root = Path(os.path.abspath(root))
    output = Path(os.path.abspath(output))
    if output != root / ARTIFACT_RELATIVE:
        raise GateError("artifact output is outside declared lane leaf")
    cursor = output
    while True:
        if cursor.exists() and _has_reparse(cursor):
            raise GateError("artifact output crosses reparse point")
        if cursor == root:
            return
        if cursor.parent == cursor:
            raise GateError("artifact output is outside repository")
        cursor = cursor.parent


def publish_artifact(root: Path) -> Path:
    output = root / ARTIFACT_RELATIVE
    assert_output_path(root, output)
    output.parent.mkdir(parents=True, exist_ok=True)
    first = isolated_artifact_bytes(root)
    second = isolated_artifact_bytes(root)
    if first != second:
        raise GateError("isolated artifact bytes differ")
    validate_artifact(first, root)
    if output.exists() and output.read_bytes() != first:
        raise GateError("existing artifact is stale")
    if not output.exists():
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(mode="wb", dir=output.parent, prefix=".semantic-store-contract.", suffix=".tmp", delete=False) as stream:
                temporary = Path(stream.name)
                stream.write(first)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, output)
            temporary = None
        finally:
            if temporary is not None and temporary.exists():
                temporary.unlink()
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
    validate_inputs(ROOT)  # Must precede unittest discovery and any SQLite open.
    validate_runtime_literals()
    suite = unittest.defaultTestLoader.discover(str(TESTS), pattern=TEST_FILE.name)
    capture = io.StringIO()
    result = unittest.TextTestRunner(stream=capture, verbosity=2).run(suite)
    if not result.wasSuccessful():
        sys.stderr.write(capture.getvalue())
        return 1
    publish_artifact(ROOT)
    print("ROTWK_202_SEMANTIC_STORE PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except GateError as exc:
        print(f"ROTWK_202_SEMANTIC_STORE FAIL {exc}", file=sys.stderr)
        raise SystemExit(1)
