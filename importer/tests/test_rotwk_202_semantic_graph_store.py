from __future__ import annotations

from dataclasses import FrozenInstanceError, fields
import copy
import hashlib
import importlib.util
import inspect
import json
from pathlib import Path
import shutil
import sqlite3
import sys
import tempfile
import unittest
from unittest import mock

from openbfme_importer import semantic_graph_contract as core
from openbfme_importer import semantic_graph_store as store


ROOT = Path(__file__).resolve().parents[2]
CORE_ARTIFACT = ROOT / "workspace/logs/P0-CORPUS-SCHEMA-CORE-002/semantic-contract-core.json"
CORE_SHA256 = "83dad1c2c2baa297fefb24e77e181fdf8d9c5ff299f14dcf01c694db09caa89a"
SCHEMA_DESIGN_SHA256 = "d5f9d8fc12a631156b4c3b73737f23faae8e40c71b7577a800e06f6c2e39090c"
STORE_DESIGN_SHA256 = "67aa0fed9029dc8913a6e974576dcc850a4473048213420f7e6ccb172fede750"

EXPECTED_TABLES = tuple(
    "source_records documents definitions assignments object_occurrences "
    "module_declarations module_kinds assets opaque_tokens script_calls "
    "nested_sites directive_sites unknown_sites maps map_objects map_libraries "
    "apt_movies apt_symbols wnd_windows parser_dispositions evidence_referents "
    "mapcache_selection reference_occurrences edges selector_dispositions "
    "root_results root_memberships root_traversals root_residual_links "
    "domain_dispositions map_dispositions residuals counts".split()
)
EXPECTED_NODE_TABLES = EXPECTED_TABLES[:20]
EXPECTED_ADMIN_TABLES = EXPECTED_TABLES[20:]
EXPECTED_BOUNDS = (
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
EXPECTED_STORE_CODES = (
    "E_CORE_CONTRACT", "E_PATH_CONTAINMENT", "E_SQLITE_CONTRACT",
    "E_JSON_ENCODING", "E_JSON_DUPLICATE_KEY", "E_CANONICAL_JSON",
    "E_ROW_SHAPE", "E_ROW_IDENTITY", "E_DUPLICATE_ID", "E_ROW_ORDER",
    "E_SHARD_LAYOUT", "E_MANIFEST_SHAPE", "E_COUNT_MISMATCH",
    "E_DIGEST_MISMATCH", "E_PUBLISH_CONFLICT",
)
EXPECTED_RESOURCE_CODES = tuple(row[2] for row in EXPECTED_BOUNDS) + ("E_INTEGER_RANGE",)
EXPECTED_PRAGMAS = (
    ("application_id", 1_329_746_765), ("user_version", 1),
    ("encoding", "UTF-8"), ("page_size", 4096),
    ("foreign_keys", "ON"), ("trusted_schema", "OFF"),
    ("journal_mode", "DELETE"), ("synchronous", "FULL"),
    ("query_only", "ON"),
)
EXPECTED_DDL = (
    ("meta", "CREATE TABLE meta(key TEXT COLLATE BINARY PRIMARY KEY,value BLOB NOT NULL) WITHOUT ROWID"),
    ("rows", "CREATE TABLE rows(table_ord INTEGER NOT NULL,row_id TEXT COLLATE BINARY NOT NULL,row_json BLOB NOT NULL,row_sha256 TEXT COLLATE BINARY NOT NULL,PRIMARY KEY(table_ord,row_id)) WITHOUT ROWID"),
    ("foreign_key_index", "CREATE TABLE foreign_key_index(source_table_ord INTEGER NOT NULL,source_id TEXT COLLATE BINARY NOT NULL,field_ord INTEGER NOT NULL,value_ord INTEGER NOT NULL,target_table_ord INTEGER NOT NULL,target_id TEXT COLLATE BINARY NOT NULL,PRIMARY KEY(source_table_ord,source_id,field_ord,value_ord)) WITHOUT ROWID"),
    ("lookup_index", "CREATE TABLE lookup_index(projection_ord INTEGER NOT NULL,key_json BLOB NOT NULL,sort_integer INTEGER NOT NULL DEFAULT 0,row_table_ord INTEGER NOT NULL,row_id TEXT COLLATE BINARY NOT NULL,PRIMARY KEY(projection_ord,key_json,sort_integer,row_id)) WITHOUT ROWID"),
    ("path_index", "CREATE TABLE path_index(projection_ord INTEGER NOT NULL,path_casefold TEXT COLLATE BINARY NOT NULL,path_exact TEXT COLLATE BINARY NOT NULL,row_table_ord INTEGER NOT NULL,row_id TEXT COLLATE BINARY NOT NULL,PRIMARY KEY(projection_ord,path_casefold,row_id)) WITHOUT ROWID"),
)
EXPECTED_INDEX_DDL = (
    ("fk_target", "CREATE INDEX fk_target ON foreign_key_index(target_table_ord,target_id,source_table_ord,source_id,field_ord,value_ord)"),
    ("lookup_row", "CREATE INDEX lookup_row ON lookup_index(row_table_ord,row_id,projection_ord)"),
    ("path_row", "CREATE INDEX path_row ON path_index(row_table_ord,row_id,projection_ord)"),
)
EXPECTED_FORBIDDEN = (
    "json1", "fts", "locale-collation", "triggers", "user-functions",
    "ambient-extensions", "sqlite-nocase", "sqlite-lower", "sql-like",
)
EXPECTED_LOOKUPS = (
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
EXPECTED_PATHS = (
    ("source-path", "source_records", "key"),
    ("document-path", "documents", "path"),
    ("asset-path", "assets", "path"),
    ("map-path", "maps", "path"),
    ("map-library-path", "map_libraries", "pathToken"),
)
EXPECTED_IMPLEMENTATION_PATHS = (
    "importer/openbfme_importer/semantic_graph_store.py",
    "importer/tests/test_rotwk_202_semantic_graph_store.py",
    "tools/check-rotwk-202-semantic-store.py",
)
EXPECTED_PUBLIC_CALLABLES = (
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
EXPECTED_PUBLIC_RECORDS = (
    ("StoreDiagnostic", (("code", "str"), ("table", "str"), ("observed", "int|null"), ("limit", "int|null"), ("row", "str|null"), ("field", "str|null"))),
    ("ForeignKeyRef", (("source_table", "str"), ("source_id", "str"), ("field", "str"), ("value_ordinal", "int"), ("target_table", "str"), ("target_id", "str"))),
    ("StoreSummary", (("core_artifact_sha256", "str"), ("table_counts", "tuple[tuple[str,int],...]"), ("total_rows", "int"), ("foreign_key_count", "int"), ("lookup_count", "int"), ("path_count", "int"))),
    ("ManifestSummary", (("manifest_sha256", "str"), ("shard_set_sha256", "str"), ("table_counts", "tuple[tuple[str,int],...]"), ("total_rows", "int"), ("total_bytes", "int"))),
)


def _canonical(value) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")


def _portable(path: Path) -> bytes:
    raw = path.read_bytes()
    assert not raw.startswith(b"\xef\xbb\xbf") and b"\x00" not in raw
    return raw.decode("utf-8").replace("\r\n", "\n").replace("\r", "\n").encode("utf-8")


def validate_store_artifact(document: dict, root: Path = ROOT) -> None:
    assert set(document) == {
        "schema", "schemaVersion", "workItemId", "baselineId",
        "semanticSchema", "semanticSchemaVersion", "normativeDesign",
        "coreContract", "publicApiOrder", "publicRecordOrder", "publicApi",
        "sqliteContract", "projectionOrder", "projections",
        "pathProjectionOrder", "pathProjections", "shardContract", "bounds",
        "diagnosticContract", "implementationClosure", "expected",
        "contentSha256",
    }
    assert document["schema"] == "openbfme.rotwk-202-semantic-store-contract"
    assert document["schemaVersion"] == 1
    assert document["workItemId"] == "P0-CORPUS-SCHEMA-STORE-001"
    assert document["baselineId"] == "rotwk-202-v9.7.7-en"
    assert document["semanticSchema"] == "openbfme.rotwk-202-semantic-schema-manifest"
    assert document["semanticSchemaVersion"] == 4
    assert document["normativeDesign"] == {
        "schema": {"id": "OWNER-SEMANTIC-SCHEMA-V4-D5F9D8FC", "path": "workspace/logs/P0-CORPUS-SCHEMA-001/schema-design-v4.md", "sha256": SCHEMA_DESIGN_SHA256},
        "store": {"id": "OWNER-SEMANTIC-STORE-V2-67AA0FED", "path": "workspace/logs/P0-CORPUS-SCHEMA-STORE-001/store-design-v2.md", "sha256": STORE_DESIGN_SHA256},
    }
    assert document["coreContract"] == {
        "id": "E-SEMANTIC-CORE-002-83DAD1C2",
        "path": "workspace/logs/P0-CORPUS-SCHEMA-CORE-002/semantic-contract-core.json",
        "externalSha256": CORE_SHA256,
        "artifactSchema": "openbfme.rotwk-202-semantic-contract-core",
        "artifactSchemaVersion": 1,
        "semanticSchema": "openbfme.rotwk-202-semantic-schema-manifest",
        "semanticSchemaVersion": 4,
        "baselineId": "rotwk-202-v9.7.7-en",
        "registryPayloadSha256": "2c6b185f7a32002ff705d2ce1859fc704a834ab8f5dfd760be7cbef1671d505b",
        "contentSha256": "7360296279b0e26a5165ddca892e33325bc282db7b64f241fed4867e50389d9c",
        "closureSha256": "14ce97b008f07ebda168590cc4c74f0b01bf8179c77cbfaad9688c140130c046",
    }
    expected_api = {
        "callables": [{"name": name, "kind": kind, "signature": signature, "returnType": return_type} for name, kind, signature, return_type in EXPECTED_PUBLIC_CALLABLES],
        "records": [{"name": name, "fields": [{"name": field, "type": field_type} for field, field_type in record_fields]} for name, record_fields in EXPECTED_PUBLIC_RECORDS],
    }
    assert document["publicApiOrder"] == [row[0] for row in EXPECTED_PUBLIC_CALLABLES]
    assert document["publicApi"] == expected_api
    assert document["publicRecordOrder"] == ["StoreDiagnostic", "ForeignKeyRef", "StoreSummary", "ManifestSummary"]
    assert document["projectionOrder"] == [row[0] for row in EXPECTED_LOOKUPS]
    assert document["pathProjectionOrder"] == [row[0] for row in EXPECTED_PATHS]
    sqlite_contract = document["sqliteContract"]
    assert sqlite_contract == {
        "applicationId": 1_329_746_765, "userVersion": 1,
        "encoding": "UTF-8", "pageSize": 4096,
        "pragmas": [{"name": name, "value": value} for name, value in EXPECTED_PRAGMAS],
        "tables": [{"name": name, "ddl": ddl} for name, ddl in EXPECTED_DDL],
        "indexes": [{"name": name, "ddl": ddl} for name, ddl in EXPECTED_INDEX_DDL],
        "forbiddenFeatures": list(EXPECTED_FORBIDDEN),
    }
    expected_projections = [
        {"name": name, "table": table, "key": [{"field": field, "normalization": normalization} for field, normalization in key], "sortField": sort_field, "sortDefault": sort_default, "omitNullFields": list(omit)}
        for name, table, key, sort_field, sort_default, omit in EXPECTED_LOOKUPS
    ]
    assert document["projections"] == expected_projections
    assert document["pathProjections"] == [
        {"name": name, "table": table, "field": field}
        for name, table, field in EXPECTED_PATHS
    ]
    assert document["bounds"] == [
        {"name": name, "value": value, "code": code, "precedenceOrdinal": ordinal}
        for ordinal, (name, value, code) in enumerate(EXPECTED_BOUNDS)
    ]
    diagnostics = document["diagnosticContract"]
    assert diagnostics["marker"] == "ROTWK_202_SEMANTIC_STORE REFUSE"
    assert diagnostics["fieldOrder"] == ["code", "table", "observed", "limit", "row", "field"]
    assert diagnostics["resourceCodes"] == list(EXPECTED_RESOURCE_CODES)
    assert diagnostics["storeCodes"] == list(EXPECTED_STORE_CODES)
    assert diagnostics["precedence"] == [
        {"ordinal": index, "stage": stage}
        for index, stage in enumerate((
            "core-contract-and-containment", "raw-line-bytes", "value-walk",
            "row-shape-identity-duplicate-order", "accepted-aggregate-bounds",
            "physical-shard-and-output", "manifest-claims", "digests",
            "publication-conflict",
        ))
    ]
    assert diagnostics["resourceObservedLimitPolicy"] == "section-12-integers; section-12-null-sites-remain-null"
    assert diagnostics["storeObservedLimitPolicy"] == "null when no numeric bound exists"
    assert diagnostics["semanticValidationOwner"] == "P0-CORPUS-SCHEMA-CONFORMANCE-001"
    shard = document["shardContract"]
    assert shard["layout"] == "tables/<table>/<six-digit-shard-index>.jsonl"
    assert shard["rowEncoding"] == "canonical-json-no-lf + 0x0a"
    assert shard["manifestSchema"] == "openbfme.rotwk-202-semantic-schema-manifest"
    assert shard["manifestSchemaVersion"] == 4
    assert shard["tableOrder"] == list(EXPECTED_TABLES)
    assert shard["nodeTables"] == list(EXPECTED_NODE_TABLES)
    assert shard["adminTables"] == list(EXPECTED_ADMIN_TABLES)
    assert shard["manifestFieldOrder"] == ["schema", "schemaVersion", "baselineId", "tableOrder", "nodeTables", "adminTables", "bounds", "tables", "totalRows", "totalBytes", "shardSetSha256"]
    assert shard["shardFieldOrder"] == ["path", "shardIndex", "rowCount", "bytes", "firstId", "lastId", "sha256"]
    assert shard["tableHashRule"] == "sha256(concatenated shard bytes in shardIndex order)"
    assert shard["shardSetHashRule"] == "sha256(utf8(table|path|sha256|bytes|rowCount\\n) in table/shard order)"
    assert shard["totalBytesRule"] == "sum(shard bytes); manifest.json excluded"
    assert shard["publicationRules"] == ["ignored-workspace-contained", "no-reparse", "untracked-destination", "fresh-sibling-temp", "validate-before-publish", "stale-existing-refuse-unchanged", "identical-existing-validate-idempotent", "single-directory-rename", "remove-only-owned-temp-on-failure"]
    closure = document["implementationClosure"]
    assert set(closure) == {"hashProfile", "pathOrder", "files", "closureSha256"}
    assert closure["hashProfile"] == "utf8-lf"
    assert closure["pathOrder"] == list(EXPECTED_IMPLEMENTATION_PATHS)
    rows = []
    for relative in EXPECTED_IMPLEMENTATION_PATHS:
        payload = _portable(root / relative)
        rows.append({"path": relative, "bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest()})
    assert closure["files"] == rows
    assert closure["closureSha256"] == hashlib.sha256("".join(f"{row['path']}|{row['sha256']}\n" for row in rows).encode()).hexdigest()
    for field, component in (
        ("publicApiSha256", document["publicApi"]),
        ("sqliteContractSha256", document["sqliteContract"]),
        ("projectionsSha256", document["projections"]),
        ("pathProjectionsSha256", document["pathProjections"]),
        ("shardContractSha256", document["shardContract"]),
        ("boundsSha256", document["bounds"]),
        ("diagnosticContractSha256", document["diagnosticContract"]),
    ):
        assert document["expected"][field] == hashlib.sha256(_canonical(component)).hexdigest()
    assert document["expected"]["ownerStoreDesignSha256"] == STORE_DESIGN_SHA256
    assert document["expected"]["coreArtifactSha256"] == CORE_SHA256
    assert set(document["expected"]) == {
        "ownerStoreDesignSha256", "coreArtifactSha256", "publicApiSha256",
        "sqliteContractSha256", "projectionsSha256", "pathProjectionsSha256",
        "shardContractSha256", "boundsSha256", "diagnosticContractSha256",
    }
    body = dict(document)
    claimed = body.pop("contentSha256")
    assert claimed == hashlib.sha256(_canonical(body)).hexdigest()


def _value(spec, field_spec=None):
    if spec.nullable:
        return None
    if spec.kind == "s":
        return "x"
    if spec.kind == "path":
        return "data/x"
    if spec.kind == "sha256":
        return "a" * 64
    if spec.kind == "id":
        prefix = core.table_spec(field_spec.fk_tables[0]).prefix if field_spec and field_spec.fk_tables else "SRC-"
        return prefix + "0" * 64
    if spec.kind == "eid":
        return "EVID-" + "0" * 64
    if spec.kind in ("u16", "u32", "u64", "i64"):
        return 0
    if spec.kind == "b":
        return False
    if spec.kind in ("enum", "literal"):
        return spec.values[0]
    if spec.kind in ("list", "set"):
        return []
    if spec.kind == "record":
        return {name: _value(child) for name, child in spec.fields}
    if spec.kind == "map":
        return {name: _value(spec.item) for name in spec.values}
    raise AssertionError(spec.kind)


def valid_row(table: str, **overrides) -> dict:
    spec = core.table_spec(table)
    row = {field.name: _value(field.value, field) for field in spec.fields}
    row.update(overrides)
    # Align every discriminated union after caller overrides.
    for declared_table, discriminator, id_fields in core.DISCRIMINATED_FKS:
        if declared_table != table:
            continue
        target = row[discriminator]
        for name in id_fields:
            if target is None:
                row[name] = [] if isinstance(row[name], list) else None
            else:
                identity = core.table_spec(target).prefix + "0" * 64
                row[name] = [identity] if isinstance(row[name], list) else identity
    row.update(overrides)
    row["id"] = core.expected_id(table, row)
    core.validate_row_shape(table, row)
    core.validate_identity(table, row)
    return row


class OneShot:
    def __init__(self, values):
        self.values = tuple(values)
        self.calls = 0

    def __iter__(self):
        self.calls += 1
        if self.calls != 1:
            raise AssertionError("one-shot iterable revisited")
        yield from self.values


class ExplodingOneShot(OneShot):
    def __iter__(self):
        self.calls += 1
        if self.calls != 1:
            raise AssertionError("one-shot iterable revisited")
        yield self.values[0]
        raise RuntimeError("synthetic producer failure")


class WorkspaceCase(unittest.TestCase):
    def setUp(self):
        (ROOT / "workspace").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=ROOT / "workspace", prefix="semantic-store-test-")
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def build_store(self, rows_by_table=None, name="graph.sqlite3"):
        rows_by_table = rows_by_table or {}
        graph = store.SemanticGraphStore.create(self.root / name, core_artifact_path=CORE_ARTIFACT)
        iterables = {}
        for table in EXPECTED_TABLES:
            source = OneShot(rows_by_table.get(table, ()))
            iterables[table] = source
            graph.ingest_table(table, source)
        summary = graph.finalize()
        self.assertTrue(all(source.calls == 1 for source in iterables.values()))
        return graph, summary


class PublicSurfaceTests(unittest.TestCase):
    def test_exports_records_and_diagnostic_are_exact(self):
        self.assertEqual(store.__all__, (
            "normalize_casefold", "canonical_row_bytes", "StoreDiagnostic",
            "StoreRefusal", "StoreSummary", "ManifestSummary", "ForeignKeyRef",
            "SemanticGraphStore", "publish_canonical_shards", "validate_canonical_shards",
        ))
        expected_fields = {
            store.StoreDiagnostic: ("code", "table", "observed", "limit", "row", "field"),
            store.ForeignKeyRef: ("source_table", "source_id", "field", "value_ordinal", "target_table", "target_id"),
            store.StoreSummary: ("core_artifact_sha256", "table_counts", "total_rows", "foreign_key_count", "lookup_count", "path_count"),
            store.ManifestSummary: ("manifest_sha256", "shard_set_sha256", "table_counts", "total_rows", "total_bytes"),
        }
        for record, names in expected_fields.items():
            self.assertEqual(tuple(field.name for field in fields(record)), names)
            instance = object.__new__(record)
            with self.assertRaises((FrozenInstanceError, TypeError, AttributeError)):
                instance.extra = 1
        diagnostic = store.StoreDiagnostic("E_ROW_SHAPE", "rows", None, None, None, "/x")
        refusal = store.StoreRefusal(diagnostic)
        self.assertEqual(refusal.diagnostic, diagnostic)
        self.assertEqual(str(refusal), "ROTWK_202_SEMANTIC_STORE REFUSE code=E_ROW_SHAPE table=rows observed=null limit=null row=null field=/x")

    def test_callable_order_and_normalization(self):
        self.assertEqual(store.normalize_casefold("Stra\u00dfe"), "strasse")
        self.assertEqual(store.normalize_casefold("e\u0301"), "\u00e9")
        self.assertEqual(store.canonical_row_bytes({"b": 1, "a": "\u00c9"}), b'{"a":"\xc3\x89","b":1}')
        methods = tuple(name for name in store.SemanticGraphStore.__dict__ if not name.startswith("_") or name in ("__enter__", "__exit__"))
        self.assertEqual(methods, (
            "create", "open_readonly", "ingest_table", "finalize", "get_row",
            "iter_rows", "iter_foreign_keys", "iter_references_to", "lookup",
            "lookup_path", "iter_safe_subtree", "greatest_preceding_assignment",
            "close", "__enter__", "__exit__",
        ))
        self.assertIn("core_artifact_path", inspect.signature(store.SemanticGraphStore.create).parameters)
        with self.assertRaisesRegex(store.StoreRefusal, "E_SQLITE_CONTRACT"):
            store.SemanticGraphStore(None, Path("x"), Path("x"), readonly=False)


class SqliteAndQueryTests(WorkspaceCase):
    def test_empty_store_pragmas_schema_readonly_and_prefinalize_cleanup(self):
        unfinished = self.root / "unfinished.sqlite3"
        graph = store.SemanticGraphStore.create(unfinished, core_artifact_path=CORE_ARTIFACT)
        graph.close()
        self.assertFalse(unfinished.exists())
        graph, summary = self.build_store()
        path = graph._database_path
        self.assertEqual(summary.table_counts, tuple((name, 0) for name in EXPECTED_TABLES))
        graph.close()
        readonly = store.SemanticGraphStore.open_readonly(path, core_artifact_path=CORE_ARTIFACT)
        self.assertEqual(list(readonly.iter_rows("source_records")), [])
        for pragma, expected in (
            ("application_id", 1_329_746_765), ("user_version", 1),
            ("encoding", "UTF-8"), ("page_size", 4096),
            ("foreign_keys", 1), ("trusted_schema", 0),
            ("journal_mode", "delete"), ("synchronous", 2),
            ("query_only", 1),
        ):
            with self.subTest(pragma=pragma):
                self.assertEqual(
                    readonly._connection.execute(f"PRAGMA {pragma}").fetchone()[0],
                    expected,
                )
        readonly.close()

    def test_casefold_lookup_foreign_keys_paths_and_subtree(self):
        source_a = valid_row("source_records", rawCatalogIndex=1, key="Data/Foo.ini", archive="data/a.big", member="data/foo.ini")
        source_b = valid_row("source_records", rawCatalogIndex=2, key="data/foo.ini", archive="data/b.big", member="data/foo.ini")
        document = valid_row("documents", sourceRecordId=source_a["id"], path="Data/Foo.ini", archive="data/a.big")
        definition_a = valid_row("definitions", documentId=document["id"], line=1, name="Stra\u00dfe", sourceIni="Data/Foo.ini")
        definition_b = valid_row("definitions", documentId=document["id"], line=2, name="STRASSE", sourceIni="Data/Foo.ini")
        graph, _ = self.build_store({
            "source_records": (source_b, source_a),
            "documents": (document,), "definitions": (definition_b, definition_a),
        })
        self.assertEqual({row["id"] for row in graph.lookup("definition-name", ("strasse",))}, {definition_a["id"], definition_b["id"]})
        self.assertEqual([row["id"] for row in graph.lookup_path("source-path", "DATA/FOO.ini")], sorted((source_a["id"], source_b["id"])))
        self.assertEqual([row["id"] for row in graph.iter_safe_subtree("source-path", "data")], sorted((source_a["id"], source_b["id"])))
        forward = tuple(graph.iter_foreign_keys("documents", document["id"], "sourceRecordId"))
        self.assertEqual(forward[0].target_id, source_a["id"])
        self.assertEqual(tuple(graph.iter_references_to("source_records", source_a["id"])), forward)
        graph.close()

    def test_foreign_key_apis_sort_by_exposed_string_tuple(self):
        source = valid_row("source_records", rawCatalogIndex=61)
        document = valid_row("documents", sourceRecordId=source["id"])
        definition = valid_row(
            "definitions", documentId=document["id"],
            evidenceIds=["EVID-" + "1" * 64],
            residualIds=["RES-" + "2" * 64],
        )
        assignment = valid_row(
            "assignments", documentId=document["id"], line=1,
            occurrenceIndex=0,
        )
        graph, _ = self.build_store({
            "source_records": (source,), "documents": (document,),
            "definitions": (definition,), "assignments": (assignment,),
        })

        def exposed(reference):
            return (
                reference.source_table, reference.source_id, reference.field,
                reference.value_ordinal, reference.target_table,
                reference.target_id,
            )

        forward = [exposed(item) for item in graph.iter_foreign_keys("definitions", definition["id"])]
        reverse = [exposed(item) for item in graph.iter_references_to("documents", document["id"])]
        self.assertEqual(forward, sorted(forward))
        self.assertEqual(reverse, sorted(reverse))
        self.assertEqual([item[0] for item in reverse], ["assignments", "definitions"])
        self.assertEqual([item[2] for item in forward], ["documentId", "evidenceIds", "residualIds"])
        graph.close()

    def test_every_closed_projection_and_wrong_component_shape(self):
        rows_by_table = {}
        for _, table, _, _, _, _ in EXPECTED_LOOKUPS:
            rows_by_table.setdefault(table, [valid_row(table)])
        for _, table, _ in EXPECTED_PATHS:
            rows_by_table.setdefault(table, [valid_row(table)])
        occurrence = valid_row(
            "reference_occurrences", candidateTable="assets",
            targetId="AST-" + "0" * 64,
        )
        rows_by_table["reference_occurrences"] = [occurrence]
        graph, _ = self.build_store({name: tuple(rows) for name, rows in rows_by_table.items()})
        for name, table, components, _, _, omit_null in EXPECTED_LOOKUPS:
            row = rows_by_table[table][0]
            if name == "occurrence-target":
                row = occurrence
            if any(row[field] is None for field in omit_null):
                self.assertEqual(list(graph.lookup(name, tuple(row[field] for field, _ in components))), [])
                continue
            key = tuple(row[field] for field, _ in components)
            with self.subTest(projection=name):
                self.assertEqual([item["id"] for item in graph.lookup(name, key)], [row["id"]])
                with self.assertRaisesRegex(store.StoreRefusal, "E_ROW_SHAPE"):
                    list(graph.lookup(name, key + ("extra",)))
        for name, table, field in EXPECTED_PATHS:
            row = rows_by_table[table][0]
            with self.subTest(path_projection=name):
                self.assertEqual([item["id"] for item in graph.lookup_path(name, row[field])], [row["id"]])
                with self.assertRaisesRegex(store.StoreRefusal, "E_ROW_SHAPE"):
                    list(graph.lookup(name, (row[field],)))
        with self.assertRaisesRegex(store.StoreRefusal, "E_ROW_SHAPE"):
            list(graph.lookup("source-key", (None,)))
        graph.close()

        omitted = valid_row(
            "reference_occurrences", candidateTable="assets", targetId=None,
            occurrenceOrdinal=9,
        )
        graph, _ = self.build_store({"reference_occurrences": (omitted,)}, name="omit.sqlite3")
        self.assertEqual(list(graph.lookup("occurrence-target", ("assets", None))), [])
        graph.close()

    def test_safe_subtree_extreme_unicode_near_miss_and_row_id_order(self):
        child_a = valid_row("source_records", rawCatalogIndex=11, key="data/\U0010ffff.ini")
        child_b = valid_row("source_records", rawCatalogIndex=12, key="data/Z.ini")
        sibling = valid_row("source_records", rawCatalogIndex=13, key="database/no.ini")
        graph, _ = self.build_store({"source_records": (sibling, child_a, child_b)})
        observed = [row["id"] for row in graph.iter_safe_subtree("source-path", "data")]
        self.assertEqual(observed, sorted((child_a["id"], child_b["id"])))
        self.assertNotIn(sibling["id"], observed)
        graph.close()

    def test_nullable_projection_predecessor_and_tie_refusal(self):
        doc = valid_row("documents")
        first = valid_row("assignments", documentId=doc["id"], line=4, occurrenceIndex=0, rootKind="Stra\u00dfe", rootName=None, field="A")
        second = valid_row("assignments", documentId=doc["id"], line=7, occurrenceIndex=0, rootKind="STRASSE", rootName=None, field="B")
        graph, _ = self.build_store({"documents": (doc,), "assignments": (second, first)})
        self.assertEqual(graph.greatest_preceding_assignment(doc["id"], "strasse", None, 7)["id"], first["id"])
        self.assertEqual(graph.greatest_preceding_assignment(doc["id"], "straSSe", None, 8)["id"], second["id"])
        graph.close()

        tied = valid_row("assignments", documentId=doc["id"], line=7, occurrenceIndex=1, rootKind="Stra\u00dfe", rootName=None, field="C")
        graph, _ = self.build_store({"documents": (doc,), "assignments": (second, tied)}, name="tie.sqlite3")
        with self.assertRaisesRegex(store.StoreRefusal, "E_ROW_ORDER"):
            graph.greatest_preceding_assignment(doc["id"], "STRASSE", None, 8)
        graph.close()

    def test_open_readonly_reapplies_streaming_aggregate_precedence(self):
        row = valid_row("source_records", rawCatalogIndex=62)
        graph, _ = self.build_store({"source_records": (row,)}, name="readonly-bounds.sqlite3")
        database = graph._database_path
        graph.close()
        original = dict(core.BOUNDS)
        cases = (
            ({"maxRowsPerNodeTable": 0, "maxNodeRowsTotal": 0, "maxRowsTotal": 0}, "E_NODE_TABLE_ROWS_LIMIT"),
            ({"maxNodeRowsTotal": 0, "maxRowsTotal": 0}, "E_NODE_ROWS_TOTAL_LIMIT"),
            ({"maxRowsTotal": 0}, "E_TOTAL_ROWS_LIMIT"),
        )
        for changes, code in cases:
            with self.subTest(code=code):
                with mock.patch.object(store, "_bound", side_effect=lambda name, values=changes: values.get(name, original[name])):
                    with self.assertRaisesRegex(store.StoreRefusal, code):
                        store.SemanticGraphStore.open_readonly(database, core_artifact_path=CORE_ARTIFACT)

    def test_duplicate_cross_family_and_table_order_refuse_with_rollback(self):
        graph = store.SemanticGraphStore.create(self.root / "bad.sqlite3", core_artifact_path=CORE_ARTIFACT)
        row = valid_row("source_records")
        with self.assertRaisesRegex(store.StoreRefusal, "E_DUPLICATE_ID"):
            graph.ingest_table("source_records", OneShot((row, row)))
        graph.close()

        graph = store.SemanticGraphStore.create(self.root / "producer.sqlite3", core_artifact_path=CORE_ARTIFACT)
        with self.assertRaisesRegex(RuntimeError, "producer failure"):
            graph.ingest_table("source_records", ExplodingOneShot((valid_row("source_records"),)))
        self.assertFalse((self.root / "producer.sqlite3").exists())
        self.assertFalse((self.root / "bad.sqlite3").exists())

        graph = store.SemanticGraphStore.create(self.root / "order.sqlite3", core_artifact_path=CORE_ARTIFACT)
        with self.assertRaisesRegex(store.StoreRefusal, "E_ROW_ORDER"):
            graph.ingest_table("documents", ())
        graph.close()

        graph = store.SemanticGraphStore.create(self.root / "family.sqlite3", core_artifact_path=CORE_ARTIFACT)
        graph.ingest_table("source_records", ())
        bad = valid_row("documents")
        bad["sourceRecordId"] = "DOC-" + "0" * 64
        bad["id"] = core.expected_id("documents", bad)
        with self.assertRaisesRegex(store.StoreRefusal, "E_ROW_SHAPE"):
            graph.ingest_table("documents", (bad,))
        graph.close()

    def test_core_and_sqlite_tampering_fail_closed(self):
        wrong = self.root / "wrong-core.json"
        wrong.write_bytes(CORE_ARTIFACT.read_bytes())
        with self.assertRaisesRegex(store.StoreRefusal, "E_CORE_CONTRACT"):
            store.SemanticGraphStore.create(self.root / "wrong.sqlite3", core_artifact_path=wrong)
        graph, _ = self.build_store()
        database = graph._database_path
        graph.close()
        connection = sqlite3.connect(database)
        connection.execute("DROP INDEX path_row")
        connection.commit()
        connection.close()
        with self.assertRaisesRegex(store.StoreRefusal, "E_SQLITE_CONTRACT"):
            store.SemanticGraphStore.open_readonly(database, core_artifact_path=CORE_ARTIFACT)

    def test_containment_reparse_and_outside_refuse_before_open(self):
        outside = ROOT.parent / "semantic-store-outside.sqlite3"
        with self.assertRaisesRegex(store.StoreRefusal, "E_PATH_CONTAINMENT"):
            store.SemanticGraphStore.create(outside, core_artifact_path=CORE_ARTIFACT)
        self.assertFalse(outside.exists())
        original = store._has_reparse_point
        with mock.patch.object(store, "_has_reparse_point", side_effect=lambda path: path == ROOT / "workspace" or original(path)):
            with self.assertRaisesRegex(store.StoreRefusal, "E_PATH_CONTAINMENT"):
                store.SemanticGraphStore.create(self.root / "reparse.sqlite3", core_artifact_path=CORE_ARTIFACT)
        dangling = self.root / "dangling.sqlite3"
        real_lexists = store.os.path.lexists
        with (
            mock.patch.object(store.os.path, "lexists", side_effect=lambda path: Path(path) == dangling or real_lexists(path)),
            mock.patch.object(store, "_has_reparse_point", side_effect=lambda path: Path(path) == dangling or original(path)),
        ):
            with self.assertRaisesRegex(store.StoreRefusal, "E_PATH_CONTAINMENT"):
                store.SemanticGraphStore.create(dangling, core_artifact_path=CORE_ARTIFACT)
        with mock.patch.object(store, "_has_reparse_point", side_effect=OSError("lstat race")):
            with self.assertRaisesRegex(store.StoreRefusal, "E_PATH_CONTAINMENT"):
                store.SemanticGraphStore.create(self.root / "race.sqlite3", core_artifact_path=CORE_ARTIFACT)

    def test_extra_schema_objects_pragma_and_shifted_meta_counts_refuse(self):
        mutations = (
            ("trigger", "CREATE TRIGGER hostile AFTER INSERT ON rows BEGIN SELECT 1; END"),
            ("view", "CREATE VIEW hostile AS SELECT * FROM rows"),
            ("application", "PRAGMA application_id=1"),
            ("version", "PRAGMA user_version=2"),
            ("journal", "PRAGMA journal_mode=WAL"),
        )
        for name, statement in mutations:
            graph, _ = self.build_store(name=f"{name}.sqlite3")
            database = graph._database_path
            graph.close()
            connection = sqlite3.connect(database)
            connection.execute(statement)
            connection.commit()
            connection.close()
            with self.subTest(name=name):
                with self.assertRaisesRegex(store.StoreRefusal, "E_SQLITE_CONTRACT"):
                    store.SemanticGraphStore.open_readonly(database, core_artifact_path=CORE_ARTIFACT)

        graph, _ = self.build_store(name="meta.sqlite3")
        database = graph._database_path
        graph.close()
        connection = sqlite3.connect(database)
        counts = json.loads(bytes(connection.execute("SELECT value FROM meta WHERE key='table_counts'").fetchone()[0]).decode())
        counts[0][1] = 1
        connection.execute("UPDATE meta SET value=? WHERE key='table_counts'", (json.dumps(counts, separators=(",", ":")).encode(),))
        connection.commit()
        connection.close()
        with self.assertRaisesRegex(store.StoreRefusal, "E_SQLITE_CONTRACT"):
            store.SemanticGraphStore.open_readonly(database, core_artifact_path=CORE_ARTIFACT)

    def test_shifted_index_rows_with_unchanged_counts_refuse(self):
        first = valid_row("source_records", rawCatalogIndex=41, key="data/first.ini")
        second = valid_row("source_records", rawCatalogIndex=42, key="data/second.ini")
        document = valid_row("documents", sourceRecordId=first["id"], path="data/first.ini")
        mutations = (
            (
                "lookup", "UPDATE lookup_index SET key_json=? WHERE projection_ord=0 AND row_id=?",
                (_canonical(["shifted"]), first["id"]),
            ),
            (
                "path", "UPDATE path_index SET path_casefold=?,path_exact=? WHERE projection_ord=0 AND row_id=?",
                ("data/shifted.ini", "data/shifted.ini", first["id"]),
            ),
            (
                "foreign-key", "UPDATE foreign_key_index SET target_id=? WHERE source_table_ord=1 AND source_id=?",
                (second["id"], document["id"]),
            ),
        )
        for name, statement, parameters in mutations:
            graph, _ = self.build_store(
                {"source_records": (first, second), "documents": (document,)},
                name=f"shift-{name}.sqlite3",
            )
            database = graph._database_path
            graph.close()
            connection = sqlite3.connect(database)
            connection.execute(statement, parameters)
            connection.commit()
            connection.close()
            with self.subTest(name=name):
                with self.assertRaisesRegex(store.StoreRefusal, "E_SQLITE_CONTRACT"):
                    store.SemanticGraphStore.open_readonly(database, core_artifact_path=CORE_ARTIFACT)


class ShardTests(WorkspaceCase):
    def published(self, name="published"):
        row = valid_row("source_records", rawCatalogIndex=3, key="Data/Test.ini", archive="data/a.big", member="data/test.ini")
        graph, _ = self.build_store({"source_records": (row,)}, name=f"{name}.sqlite3")
        summary = store.publish_canonical_shards(graph, self.root / name)
        graph.close()
        return self.root / name, summary

    def test_two_byte_identical_atomic_publishes_and_idempotence(self):
        first, first_summary = self.published("one")
        second, second_summary = self.published("two")
        self.assertEqual(first_summary, second_summary)
        first_files = {path.relative_to(first).as_posix(): path.read_bytes() for path in first.rglob("*") if path.is_file()}
        second_files = {path.relative_to(second).as_posix(): path.read_bytes() for path in second.rglob("*") if path.is_file()}
        self.assertEqual(first_files, second_files)
        graph, _ = self.build_store({"source_records": (valid_row("source_records", rawCatalogIndex=3, key="Data/Test.ini", archive="data/a.big", member="data/test.ini"),)}, name="again.sqlite3")
        self.assertEqual(store.publish_canonical_shards(graph, first), first_summary)
        graph.close()

        low = valid_row("source_records", rawCatalogIndex=31)
        high = valid_row("source_records", rawCatalogIndex=32)
        left, _ = self.build_store({"source_records": (low, high)}, name="left.sqlite3")
        right, _ = self.build_store({"source_records": (high, low)}, name="right.sqlite3")
        left_summary = store.publish_canonical_shards(left, self.root / "left")
        right_summary = store.publish_canonical_shards(right, self.root / "right")
        self.assertEqual(left_summary, right_summary)
        self.assertEqual(
            {(path.relative_to(self.root / "left").as_posix(), path.read_bytes()) for path in (self.root / "left").rglob("*") if path.is_file()},
            {(path.relative_to(self.root / "right").as_posix(), path.read_bytes()) for path in (self.root / "right").rglob("*") if path.is_file()},
        )
        left.close()
        right.close()

    def test_physical_encoding_layout_counts_and_hashes_refuse(self):
        base, _ = self.published()
        shard = next((base / "tables").rglob("*.jsonl"))
        cases = {
            "bom": b"\xef\xbb\xbf" + shard.read_bytes(),
            "crlf": shard.read_bytes().replace(b"\n", b"\r\n"),
            "nul-byte": shard.read_bytes().replace(b"{", b"{\x00", 1),
            "duplicate": b'{"id":"x","id":"y"}\n',
        }
        for name, payload in cases.items():
            target = self.root / name
            shutil.copytree(base, target)
            changed = next((target / "tables").rglob("*.jsonl"))
            changed.write_bytes(payload)
            with self.subTest(name=name):
                expected = "E_JSON_DUPLICATE_KEY.*table=source_records" if name == "duplicate" else "E_JSON_ENCODING"
                with self.assertRaisesRegex(store.StoreRefusal, expected):
                    store.validate_canonical_shards(target, core_artifact_path=CORE_ARTIFACT)

    def test_physical_duplicate_and_ascii_order_precede_manifest_claims(self):
        low = valid_row("source_records", rawCatalogIndex=51)
        high = valid_row("source_records", rawCatalogIndex=52)
        graph, _ = self.build_store({"source_records": (high, low)}, name="row-order.sqlite3")
        base = self.root / "row-order"
        store.publish_canonical_shards(graph, base)
        graph.close()
        shard = next((base / "tables").rglob("*.jsonl"))
        lines = shard.read_bytes().splitlines(keepends=True)
        self.assertEqual(len(lines), 2)
        for name, payload, code in (
            ("duplicate-id", lines[0] + lines[0], "E_DUPLICATE_ID"),
            ("reverse-order", lines[1] + lines[0], "E_ROW_ORDER"),
        ):
            target = self.root / name
            shutil.copytree(base, target)
            next((target / "tables").rglob("*.jsonl")).write_bytes(payload)
            with self.subTest(name=name):
                with self.assertRaisesRegex(store.StoreRefusal, code):
                    store.validate_canonical_shards(target, core_artifact_path=CORE_ARTIFACT)

    def test_cross_shard_cut_must_be_forced_by_row_or_byte_bound(self):
        first = valid_row("source_records", rawCatalogIndex=63, key="data/a.ini")
        second = valid_row("source_records", rawCatalogIndex=64, key="data/longer-name.ini")
        original = dict(core.BOUNDS)
        cases = (
            ("row", {"maxRowsPerShard": 1}),
            (
                "byte",
                {"maxBytesPerShard": max(
                    len(store.canonical_row_bytes(first)) + 1,
                    len(store.canonical_row_bytes(second)) + 1,
                )},
            ),
        )
        for name, changes in cases:
            graph, _ = self.build_store(
                {"source_records": (second, first,)}, name=f"cut-{name}.sqlite3",
            )
            destination = self.root / f"cut-{name}"
            with mock.patch.object(
                store, "_bound",
                side_effect=lambda bound, values=changes: values.get(bound, original[bound]),
            ):
                store.publish_canonical_shards(graph, destination)
            graph.close()
            self.assertEqual(len(list((destination / "tables" / "source_records").glob("*.jsonl"))), 2)
            with self.subTest(bound=name):
                with self.assertRaisesRegex(store.StoreRefusal, "E_SHARD_LAYOUT"):
                    store.validate_canonical_shards(destination, core_artifact_path=CORE_ARTIFACT)

    def test_manifest_strict_types_claims_layout_and_set_hash_refuse(self):
        base, _ = self.published()
        mutations = {
            "bool-count": lambda doc: doc["tables"]["source_records"].__setitem__("rowCount", True),
            "false-bytes": lambda doc: doc["tables"]["source_records"]["shards"][0].__setitem__("bytes", 0),
            "false-first": lambda doc: doc["tables"]["source_records"]["shards"][0].__setitem__("firstId", "SRC-" + "f" * 64),
            "false-last": lambda doc: doc["tables"]["source_records"]["shards"][0].__setitem__("lastId", "SRC-" + "f" * 64),
            "false-index": lambda doc: doc["tables"]["source_records"]["shards"][0].__setitem__("shardIndex", 1),
            "shard-hash": lambda doc: doc["tables"]["source_records"]["shards"][0].__setitem__("sha256", "0" * 64),
            "set-hash": lambda doc: doc.__setitem__("shardSetSha256", "0" * 64),
            "u32-plus-one": lambda doc: doc["tables"]["source_records"]["shards"][0].__setitem__("shardIndex", 4_294_967_296),
            "u64-plus-one": lambda doc: doc.__setitem__("totalBytes", 18_446_744_073_709_551_616),
        }
        for name, mutate in mutations.items():
            target = self.root / name
            shutil.copytree(base, target)
            manifest = target / "manifest.json"
            document = json.loads(manifest.read_text(encoding="utf-8"))
            mutate(document)
            manifest.write_bytes(_canonical(document) + b"\n")
            with self.subTest(name=name):
                with self.assertRaises(store.StoreRefusal):
                    store.validate_canonical_shards(target, core_artifact_path=CORE_ARTIFACT)

        for name, operation in (
            ("missing", "missing"), ("extra", "extra"), ("noncontiguous", "noncontiguous"),
        ):
            target = self.root / f"layout-{name}"
            shutil.copytree(base, target)
            shard = next((target / "tables").rglob("*.jsonl"))
            if operation == "missing":
                shard.unlink()
            elif operation == "extra":
                (shard.parent / "000001.jsonl").write_bytes(shard.read_bytes())
            else:
                shard.rename(shard.parent / "000001.jsonl")
            with self.subTest(layout=name):
                with self.assertRaises(store.StoreRefusal):
                    store.validate_canonical_shards(target, core_artifact_path=CORE_ARTIFACT)

    def test_manifest_malformed_float_nan_nonnfc_escape_blank_and_extra_lf(self):
        base, _ = self.published()
        original = (base / "manifest.json").read_bytes()
        hostiles = {
            "malformed-utf8": original.replace(b'"rotwk', b'"\xffotwk', 1),
            "float": original.replace(b'"totalRows":1', b'"totalRows":1.0', 1),
            "nan": original.replace(b'"totalRows":1', b'"totalRows":NaN', 1),
            "nonnfc": original.replace(b'"rotwk-202-v9.7.7-en"', '"rotwk-202-v9.7.7-e\u0301n"'.encode("utf-8"), 1),
            "escape": original.replace(b'"rotwk-202-v9.7.7-en"', b'"rotwk-202-v9.7.7-\\u0065n"', 1),
            "blank": b"\n",
            "extra-lf": original + b"\n",
            "no-lf": original[:-1],
        }
        for name, raw in hostiles.items():
            target = self.root / f"manifest-{name}"
            shutil.copytree(base, target)
            (target / "manifest.json").write_bytes(raw)
            with self.subTest(name=name):
                with self.assertRaises(store.StoreRefusal):
                    store.validate_canonical_shards(target, core_artifact_path=CORE_ARTIFACT)

        for name, mutate in (
            ("false-count", lambda doc: doc["tables"]["source_records"].__setitem__("rowCount", 2)),
            ("wrong-hash", lambda doc: doc["tables"]["source_records"].__setitem__("tableSha256", "0" * 64)),
        ):
            target = self.root / name
            shutil.copytree(base, target)
            path = target / "manifest.json"
            document = json.loads(path.read_text(encoding="utf-8"))
            mutate(document)
            path.write_bytes(_canonical(document) + b"\n")
            with self.subTest(name=name):
                with self.assertRaises(store.StoreRefusal):
                    store.validate_canonical_shards(target, core_artifact_path=CORE_ARTIFACT)

    def test_exact_and_plus_one_prewrite_bounds(self):
        row = valid_row("source_records")
        graph = store.SemanticGraphStore.create(self.root / "bounded.sqlite3", core_artifact_path=CORE_ARTIFACT)
        with mock.patch.object(store, "_bound", side_effect=lambda name: 0 if name == "maxRowsPerNodeTable" else dict(core.BOUNDS)[name]):
            with self.assertRaisesRegex(store.StoreRefusal, "E_NODE_TABLE_ROWS_LIMIT"):
                graph.ingest_table("source_records", (row,))
        graph.close()

    def test_exact_and_plus_one_every_ingest_resource_family(self):
        original = dict(core.BOUNDS)

        def attempt(table, row, bound_name, limit, expected_code=None):
            graph = store.SemanticGraphStore.create(self.root / f"{bound_name}-{limit}-{table}.sqlite3", core_artifact_path=CORE_ARTIFACT)
            for preceding in EXPECTED_TABLES[:EXPECTED_TABLES.index(table)]:
                graph.ingest_table(preceding, ())
            with mock.patch.object(store, "_bound", side_effect=lambda name: limit if name == bound_name else original[name]):
                if expected_code:
                    with self.assertRaisesRegex(store.StoreRefusal, expected_code):
                        graph.ingest_table(table, (row,))
                else:
                    graph.ingest_table(table, (row,))
                    graph.close()

        source = valid_row("source_records")
        max_string = max(len(value.encode("utf-8")) for value in source.values() if isinstance(value, str))
        line_bytes = len(store.canonical_row_bytes(source)) + 1
        for bound_name, exact, code in (
            ("maxContainerItems", len(source), "E_CONTAINER_ITEMS_LIMIT"),
            ("maxStringUtf8Bytes", max_string, "E_STRING_BYTES_LIMIT"),
            ("maxJsonlLineBytes", line_bytes, "E_LINE_BYTES_LIMIT"),
            ("maxRowsPerNodeTable", 1, "E_NODE_TABLE_ROWS_LIMIT"),
            ("maxNodeRowsTotal", 1, "E_NODE_ROWS_TOTAL_LIMIT"),
            ("maxRowsTotal", 1, "E_TOTAL_ROWS_LIMIT"),
        ):
            attempt("source_records", source, bound_name, exact)
            attempt("source_records", source, bound_name, exact - 1, code)

        nested = valid_row("object_occurrences", moduleCounts=[{"moduleKind": "x", "count": 0}])
        attempt("object_occurrences", nested, "maxJsonDepth", 3)
        attempt("object_occurrences", nested, "maxJsonDepth", 2, "E_JSON_DEPTH_LIMIT")

        for table, bound_name, code in (
            ("reference_occurrences", "maxReferenceOccurrences", "E_OCCURRENCE_ROWS_LIMIT"),
            ("edges", "maxEdges", "E_EDGE_ROWS_LIMIT"),
            ("residuals", "maxResiduals", "E_RESIDUAL_ROWS_LIMIT"),
            ("counts", "maxRowsPerAdminTable", "E_ADMIN_TABLE_ROWS_LIMIT"),
        ):
            row = valid_row(table)
            attempt(table, row, bound_name, 1)
            attempt(table, row, bound_name, 0, code)

        graph = store.SemanticGraphStore.create(self.root / "integer.sqlite3", core_artifact_path=CORE_ARTIFACT)
        hostile = valid_row("source_records")
        hostile["precedence"] = 65_536
        hostile["id"] = "SRC-" + "0" * 64
        with self.assertRaisesRegex(store.StoreRefusal, "E_INTEGER_RANGE"):
            graph.ingest_table("source_records", (hostile,))
        graph.close()

    def test_declared_field_walk_precedence_is_single_pass(self):
        original = dict(core.BOUNDS)
        def small_string(name):
            return 100 if name == "maxStringUtf8Bytes" else original[name]

        early_integer = valid_row("source_records")
        early_integer["rawCatalogIndex"] = 4_294_967_296
        early_integer["member"] = "x" * 101
        graph = store.SemanticGraphStore.create(self.root / "field-integer.sqlite3", core_artifact_path=CORE_ARTIFACT)
        with mock.patch.object(store, "_bound", side_effect=small_string):
            with self.assertRaisesRegex(store.StoreRefusal, "E_INTEGER_RANGE"):
                graph.ingest_table("source_records", (early_integer,))

        early_string = valid_row("source_records")
        early_string["id"] = "SRC-" + "x" * 101
        early_string["rawCatalogIndex"] = 4_294_967_296
        graph = store.SemanticGraphStore.create(self.root / "field-string.sqlite3", core_artifact_path=CORE_ARTIFACT)
        with mock.patch.object(store, "_bound", side_effect=small_string):
            with self.assertRaisesRegex(store.StoreRefusal, "E_STRING_BYTES_LIMIT"):
                graph.ingest_table("source_records", (early_string,))

    def test_exact_and_plus_one_physical_limits_and_preallocation(self):
        base, _ = self.published()
        shard = next((base / "tables").rglob("*.jsonl"))
        shard_bytes = shard.stat().st_size
        manifest_line_bytes = len((base / "manifest.json").read_bytes())
        max_shard_line_bytes = max(
            len(line)
            for path in (base / "tables").rglob("*.jsonl")
            for line in path.read_bytes().splitlines(keepends=True)
        )
        max_line_bytes = max(manifest_line_bytes, max_shard_line_bytes)
        output_bytes = sum(path.stat().st_size for path in base.rglob("*") if path.is_file())
        original_bound = dict(core.BOUNDS)

        def limits(**changes):
            return lambda name: changes.get(name, original_bound[name])

        for name, exact, plus_one, code in (
            ("line", {"maxJsonlLineBytes": max_line_bytes}, {"maxJsonlLineBytes": max_line_bytes - 1}, "E_LINE_BYTES_LIMIT"),
            ("shard-bytes", {"maxBytesPerShard": shard_bytes}, {"maxBytesPerShard": shard_bytes - 1}, "E_SHARD_BYTES_LIMIT"),
            ("shard-rows", {"maxRowsPerShard": 1}, {"maxRowsPerShard": 0}, "E_SHARD_ROWS_LIMIT"),
            ("shard-count", {"maxShardsPerTable": 1}, {"maxShardsPerTable": 0}, "E_SHARD_COUNT_LIMIT"),
            ("output", {"maxOutputBytes": output_bytes}, {"maxOutputBytes": output_bytes - 1}, "E_OUTPUT_BYTES_LIMIT"),
        ):
            with self.subTest(limit=name, boundary="exact"):
                with mock.patch.object(store, "_bound", side_effect=limits(**exact)):
                    store.validate_canonical_shards(base, core_artifact_path=CORE_ARTIFACT)
            with self.subTest(limit=name, boundary="plus-one"):
                with mock.patch.object(store, "_bound", side_effect=limits(**plus_one)):
                    with self.assertRaisesRegex(store.StoreRefusal, code):
                        store.validate_canonical_shards(base, core_artifact_path=CORE_ARTIFACT)

    def test_simultaneous_row_precedes_byte_and_duplicate_precedes_aggregate(self):
        base, _ = self.published()
        original_bound = dict(core.BOUNDS)
        def tiny(name):
            return 0 if name in ("maxRowsPerShard", "maxBytesPerShard") else original_bound[name]
        with mock.patch.object(store, "_bound", side_effect=tiny):
            with self.assertRaisesRegex(store.StoreRefusal, "E_SHARD_ROWS_LIMIT"):
                store.validate_canonical_shards(base, core_artifact_path=CORE_ARTIFACT)

        graph = store.SemanticGraphStore.create(self.root / "precedence.sqlite3", core_artifact_path=CORE_ARTIFACT)
        row = valid_row("source_records")
        def zero_table(name):
            return 0 if name == "maxRowsPerNodeTable" else original_bound[name]
        with mock.patch.object(store, "_bound", side_effect=zero_table):
            graph._connection.execute(
                "INSERT INTO rows VALUES(?,?,?,?)",
                (0, row["id"], store.canonical_row_bytes(row), hashlib.sha256(store.canonical_row_bytes(row)).hexdigest()),
            )
            with self.assertRaisesRegex(store.StoreRefusal, "E_DUPLICATE_ID"):
                graph.ingest_table("source_records", (row,))

    def test_publish_conflict_is_unchanged_and_output_rollback(self):
        destination, _ = self.published()
        manifest = destination / "manifest.json"
        original = manifest.read_bytes()
        manifest.write_bytes(original + b"stale")
        graph, _ = self.build_store({"source_records": (valid_row("source_records", rawCatalogIndex=4),)}, name="conflict.sqlite3")
        before = {path.relative_to(destination).as_posix(): path.read_bytes() for path in destination.rglob("*") if path.is_file()}
        with self.assertRaises(store.StoreRefusal):
            store.publish_canonical_shards(graph, destination)
        after = {path.relative_to(destination).as_posix(): path.read_bytes() for path in destination.rglob("*") if path.is_file()}
        self.assertEqual(before, after)
        graph.close()

    def test_open_shard_stream_is_closed_before_temp_rollback(self):
        first = valid_row("source_records", rawCatalogIndex=21)
        second = valid_row("source_records", rawCatalogIndex=22)
        graph, _ = self.build_store({"source_records": (first, second)}, name="stream.sqlite3")
        destination = self.root / "stream-output"
        original = store.canonical_row_bytes
        calls = 0
        def explode(value):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise RuntimeError("after-open")
            return original(value)
        with mock.patch.object(store, "canonical_row_bytes", side_effect=explode):
            with self.assertRaisesRegex(RuntimeError, "after-open"):
                store.publish_canonical_shards(graph, destination)
        self.assertFalse(destination.exists())
        self.assertEqual(list(self.root.glob(".stream-output.semantic-store.*")), [])
        graph.close()


class ArtifactFixtureTests(unittest.TestCase):
    def test_literal_validator_rejects_component_and_content_drift(self):
        checker = _checker_module()
        document = checker.build_artifact_document(ROOT)
        validate_store_artifact(document)
        for path, value in (
            (("schema",), "wrong"),
            (("coreContract", "contentSha256"), "0" * 64),
            (("publicApiOrder",), []),
            (("publicApi", "callables"), []),
            (("publicApi", "callables", 0, "kind"), "method"),
            (("publicApi", "callables", 0, "signature"), "wrong"),
            (("publicApi", "callables", 0, "returnType"), "bytes"),
            (("publicApi", "records", 0, "fields", 0, "type"), "bytes"),
            (("sqliteContract", "applicationId"), 0),
            (("sqliteContract", "pragmas", 0, "value"), 0),
            (("sqliteContract", "tables", 0, "ddl"), "wrong"),
            (("sqliteContract", "indexes", 0, "ddl"), "wrong"),
            (("projectionOrder",), []),
            (("projections",), []),
            (("projections", 0, "key", 0, "normalization"), "ascii"),
            (("pathProjectionOrder",), []),
            (("pathProjections",), []),
            (("shardContract", "layout"), "wrong"),
            (("shardContract", "publicationRules"), []),
            (("bounds", 0, "value"), 17),
            (("diagnosticContract", "storeCodes"), []),
            (("implementationClosure", "closureSha256"), "0" * 64),
            (("implementationClosure", "files", 0, "sha256"), "0" * 64),
            (("expected", "publicApiSha256"), "0" * 64),
        ):
            hostile = copy.deepcopy(document)
            cursor = hostile
            for component in path[:-1]:
                cursor = cursor[component]
            cursor[path[-1]] = value
            body = dict(hostile)
            body.pop("contentSha256", None)
            hostile["contentSha256"] = hashlib.sha256(_canonical(body)).hexdigest()
            with self.subTest(path=path):
                with self.assertRaises(AssertionError):
                    validate_store_artifact(hostile)

        for component in (
            "coreContract", "publicApi", "sqliteContract", "shardContract",
            "diagnosticContract", "implementationClosure", "expected",
        ):
            hostile = copy.deepcopy(document)
            hostile[component]["unexpected"] = True
            body = dict(hostile)
            body.pop("contentSha256", None)
            hostile["contentSha256"] = hashlib.sha256(_canonical(body)).hexdigest()
            with self.subTest(extra_key=component):
                with self.assertRaises(AssertionError):
                    validate_store_artifact(hostile)

    def test_two_isolated_artifacts_are_byte_identical_and_canonical(self):
        checker = _checker_module()
        first = checker.isolated_artifact_bytes(ROOT)
        second = checker.isolated_artifact_bytes(ROOT)
        self.assertEqual(first, second)
        self.assertTrue(first.endswith(b"\n"))
        self.assertNotIn(b"\r", first)
        document = checker.parse_artifact(first)
        validate_store_artifact(document)


def _checker_module():
    name = "_openbfme_semantic_store_checker"
    if name in sys.modules:
        return sys.modules[name]
    path = ROOT / "tools/check-rotwk-202-semantic-store.py"
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


if __name__ == "__main__":
    unittest.main()
