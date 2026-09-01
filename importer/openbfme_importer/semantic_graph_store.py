"""Bounded SQLite and canonical shard storage for the RotWK semantic graph.

This module is deliberately semantics-free.  It validates rows against the
accepted core contract, stores them one at a time, and exposes only the closed
physical projections required by the STORE owner contract.
"""

from __future__ import annotations

from collections.abc import Iterator, Mapping, Sequence
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import shutil
import sqlite3
import stat
import subprocess
import tempfile
from types import TracebackType
from typing import Any
import unicodedata

from . import semantic_graph_contract as core


_CORE_ARTIFACT_RELATIVE = Path(
    "workspace/logs/P0-CORPUS-SCHEMA-CORE-002/semantic-contract-core.json"
)
_CORE_ARTIFACT_SHA256 = (
    "83dad1c2c2baa297fefb24e77e181fdf8d9c5ff299f14dcf01c694db09caa89a"
)
_CORE_REGISTRY_SHA256 = (
    "2c6b185f7a32002ff705d2ce1859fc704a834ab8f5dfd760be7cbef1671d505b"
)
_APPLICATION_ID = 1_329_746_765
_USER_VERSION = 1
_CONSTRUCTION_TOKEN = object()

_TABLE_DDL = (
    ("meta", "CREATE TABLE meta(key TEXT COLLATE BINARY PRIMARY KEY,value BLOB NOT NULL) WITHOUT ROWID"),
    ("rows", "CREATE TABLE rows(table_ord INTEGER NOT NULL,row_id TEXT COLLATE BINARY NOT NULL,row_json BLOB NOT NULL,row_sha256 TEXT COLLATE BINARY NOT NULL,PRIMARY KEY(table_ord,row_id)) WITHOUT ROWID"),
    ("foreign_key_index", "CREATE TABLE foreign_key_index(source_table_ord INTEGER NOT NULL,source_id TEXT COLLATE BINARY NOT NULL,field_ord INTEGER NOT NULL,value_ord INTEGER NOT NULL,target_table_ord INTEGER NOT NULL,target_id TEXT COLLATE BINARY NOT NULL,PRIMARY KEY(source_table_ord,source_id,field_ord,value_ord)) WITHOUT ROWID"),
    ("lookup_index", "CREATE TABLE lookup_index(projection_ord INTEGER NOT NULL,key_json BLOB NOT NULL,sort_integer INTEGER NOT NULL DEFAULT 0,row_table_ord INTEGER NOT NULL,row_id TEXT COLLATE BINARY NOT NULL,PRIMARY KEY(projection_ord,key_json,sort_integer,row_id)) WITHOUT ROWID"),
    ("path_index", "CREATE TABLE path_index(projection_ord INTEGER NOT NULL,path_casefold TEXT COLLATE BINARY NOT NULL,path_exact TEXT COLLATE BINARY NOT NULL,row_table_ord INTEGER NOT NULL,row_id TEXT COLLATE BINARY NOT NULL,PRIMARY KEY(projection_ord,path_casefold,row_id)) WITHOUT ROWID"),
)
_INDEX_DDL = (
    ("fk_target", "CREATE INDEX fk_target ON foreign_key_index(target_table_ord,target_id,source_table_ord,source_id,field_ord,value_ord)"),
    ("lookup_row", "CREATE INDEX lookup_row ON lookup_index(row_table_ord,row_id,projection_ord)"),
    ("path_row", "CREATE INDEX path_row ON path_index(row_table_ord,row_id,projection_ord)"),
)

_LOOKUP_PROJECTIONS = (
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

_PATH_PROJECTIONS = (
    ("source-path", "source_records", "key"),
    ("document-path", "documents", "path"),
    ("asset-path", "assets", "path"),
    ("map-path", "maps", "path"),
    ("map-library-path", "map_libraries", "pathToken"),
)

_STORE_CODES = (
    "E_CORE_CONTRACT", "E_PATH_CONTAINMENT", "E_SQLITE_CONTRACT",
    "E_JSON_ENCODING", "E_JSON_DUPLICATE_KEY", "E_CANONICAL_JSON",
    "E_ROW_SHAPE", "E_ROW_IDENTITY", "E_DUPLICATE_ID", "E_ROW_ORDER",
    "E_SHARD_LAYOUT", "E_MANIFEST_SHAPE", "E_COUNT_MISMATCH",
    "E_DIGEST_MISMATCH", "E_PUBLISH_CONFLICT",
)


@dataclass(frozen=True, slots=True)
class StoreDiagnostic:
    code: str
    table: str
    observed: int | None
    limit: int | None
    row: str | None
    field: str | None


class StoreRefusal(Exception):
    __slots__ = ("diagnostic",)

    def __init__(self, diagnostic: StoreDiagnostic) -> None:
        self.diagnostic = diagnostic
        super().__init__(_diagnostic_line(diagnostic))


@dataclass(frozen=True, slots=True)
class ForeignKeyRef:
    source_table: str
    source_id: str
    field: str
    value_ordinal: int
    target_table: str
    target_id: str


@dataclass(frozen=True, slots=True)
class StoreSummary:
    core_artifact_sha256: str
    table_counts: tuple[tuple[str, int], ...]
    total_rows: int
    foreign_key_count: int
    lookup_count: int
    path_count: int


@dataclass(frozen=True, slots=True)
class ManifestSummary:
    manifest_sha256: str
    shard_set_sha256: str
    table_counts: tuple[tuple[str, int], ...]
    total_rows: int
    total_bytes: int


def normalize_casefold(value: str) -> str:
    if not isinstance(value, str):
        raise TypeError("normalize_casefold requires str")
    return unicodedata.normalize("NFC", value).casefold()


def canonical_row_bytes(row: Mapping[str, object]) -> bytes:
    if not isinstance(row, Mapping):
        raise TypeError("canonical_row_bytes requires a mapping")
    return json.dumps(
        dict(row), ensure_ascii=False, sort_keys=True, separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")


def _canonical_array_bytes(values: Sequence[object]) -> bytes:
    return json.dumps(
        list(values), ensure_ascii=False, separators=(",", ":"), allow_nan=False,
    ).encode("utf-8")


def _canonical_json_size(row: Mapping[str, object]) -> int:
    # Count fragments so an oversized row is rejected before allocating its
    # complete canonical byte representation.
    encoder = json.JSONEncoder(
        ensure_ascii=False, sort_keys=True, separators=(",", ":"),
        allow_nan=False,
    )
    return sum(len(fragment.encode("utf-8")) for fragment in encoder.iterencode(dict(row)))


def _diagnostic_line(diagnostic: StoreDiagnostic) -> str:
    def value(item: object) -> str:
        return "null" if item is None else str(item)
    return (
        "ROTWK_202_SEMANTIC_STORE REFUSE "
        f"code={diagnostic.code} table={diagnostic.table} "
        f"observed={value(diagnostic.observed)} limit={value(diagnostic.limit)} "
        f"row={value(diagnostic.row)} field={value(diagnostic.field)}"
    )


def _refuse(
    code: str, table: str = "store", *, observed: int | None = None,
    limit: int | None = None, row: str | None = None,
    field: str | None = None,
) -> None:
    raise StoreRefusal(StoreDiagnostic(code, table, observed, limit, row, field))


def _repository_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _has_reparse_point(path: Path) -> bool:
    attributes = getattr(path.lstat(), "st_file_attributes", 0)
    return path.is_symlink() or bool(
        attributes & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    )


def _git_output(root: Path, *args: str) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        ("git", "-C", str(root), *args), check=False, capture_output=True,
    )


def _assert_workspace_path(path: Path, *, absent: bool = False) -> Path:
    root = Path(os.path.abspath(_repository_root()))
    workspace = root / "workspace"
    candidate = Path(os.path.abspath(path))
    try:
        relative = candidate.relative_to(root)
        candidate.relative_to(workspace)
    except ValueError:
        _refuse("E_PATH_CONTAINMENT")
    cursor = root
    try:
        if os.path.lexists(cursor) and _has_reparse_point(cursor):
            _refuse("E_PATH_CONTAINMENT")
        for part in relative.parts:
            cursor = cursor / part
            if os.path.lexists(cursor) and _has_reparse_point(cursor):
                _refuse("E_PATH_CONTAINMENT")
    except OSError:
        _refuse("E_PATH_CONTAINMENT")
    if absent and os.path.lexists(candidate):
        _refuse("E_PATH_CONTAINMENT")
    portable = relative.as_posix()
    if _git_output(root, "ls-files", "--", portable).stdout.strip():
        _refuse("E_PATH_CONTAINMENT")
    ignored = _git_output(root, "check-ignore", "-q", "--", portable)
    if ignored.returncode != 0:
        _refuse("E_PATH_CONTAINMENT")
    return candidate


def _portable_source_bytes(path: Path) -> bytes:
    raw = path.read_bytes()
    if raw.startswith(b"\xef\xbb\xbf") or b"\x00" in raw:
        _refuse("E_CORE_CONTRACT")
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        _refuse("E_CORE_CONTRACT")
    return text.replace("\r\n", "\n").replace("\r", "\n").encode("utf-8")


def _parse_canonical_object(raw: bytes, table: str) -> dict[str, Any]:
    def object_without_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                _refuse("E_JSON_DUPLICATE_KEY", table)
            result[key] = value
        return result

    def reject_float(_value: str) -> None:
        _refuse("E_CANONICAL_JSON", table)

    if (
        raw.startswith(b"\xef\xbb\xbf") or b"\x00" in raw or b"\r" in raw
        or not raw.endswith(b"\n") or raw.endswith(b"\n\n")
    ):
        _refuse("E_JSON_ENCODING", table)
    try:
        result = json.loads(
            raw[:-1].decode("utf-8"), parse_float=reject_float,
            parse_constant=reject_float, object_pairs_hook=object_without_duplicates,
        )
    except StoreRefusal:
        raise
    except (UnicodeDecodeError, json.JSONDecodeError):
        _refuse("E_JSON_ENCODING", table)
    if not isinstance(result, dict):
        _refuse("E_ROW_SHAPE" if table != "manifest" else "E_MANIFEST_SHAPE", table)
    try:
        encoded = canonical_row_bytes(result) + b"\n"
    except (TypeError, ValueError, UnicodeError):
        _refuse("E_CANONICAL_JSON", table)
    if encoded != raw:
        _refuse("E_CANONICAL_JSON", table)
    return result


def _load_core_artifact(path: Path) -> tuple[Path, dict[str, Any]]:
    root = _repository_root()
    expected = root / _CORE_ARTIFACT_RELATIVE
    candidate = Path(os.path.abspath(path))
    if candidate != Path(os.path.abspath(expected)):
        _refuse("E_CORE_CONTRACT")
    _assert_workspace_path(candidate)
    try:
        raw = candidate.read_bytes()
    except OSError:
        _refuse("E_CORE_CONTRACT")
    if hashlib.sha256(raw).hexdigest() != _CORE_ARTIFACT_SHA256:
        _refuse("E_CORE_CONTRACT")
    document = _parse_canonical_object(raw, "manifest")
    body = dict(document)
    claimed = body.pop("contentSha256", None)
    if claimed != hashlib.sha256(canonical_row_bytes(body)).hexdigest():
        _refuse("E_CORE_CONTRACT")
    if (
        document.get("schema") != "openbfme.rotwk-202-semantic-contract-core"
        or document.get("schemaVersion") != 1
        or document.get("semanticSchema") != core.SCHEMA_NAME
        or document.get("semanticSchemaVersion") != core.SCHEMA_VERSION
        or document.get("baselineId") != core.BASELINE_ID
        or document.get("normativeDesignSha256") != core.NORMATIVE_DESIGN_SHA256
        or document.get("expected", {}).get("registryPayloadSha256") != _CORE_REGISTRY_SHA256
    ):
        _refuse("E_CORE_CONTRACT")
    closure = document.get("implementationClosure")
    if not isinstance(closure, dict) or closure.get("hashProfile") != "utf8-lf":
        _refuse("E_CORE_CONTRACT")
    rows = closure.get("files")
    if not isinstance(rows, list) or closure.get("pathOrder") != [row.get("path") for row in rows if isinstance(row, dict)]:
        _refuse("E_CORE_CONTRACT")
    closure_lines: list[str] = []
    for row in rows:
        if not isinstance(row, dict) or set(row) != {"path", "bytes", "sha256"}:
            _refuse("E_CORE_CONTRACT")
        source = _portable_source_bytes(root / row["path"])
        digest = hashlib.sha256(source).hexdigest()
        if len(source) != row["bytes"] or digest != row["sha256"]:
            _refuse("E_CORE_CONTRACT")
        closure_lines.append(f"{row['path']}|{digest}\n")
    if hashlib.sha256("".join(closure_lines).encode("utf-8")).hexdigest() != closure.get("closureSha256"):
        _refuse("E_CORE_CONTRACT")
    return candidate, document


def _bound(name: str) -> int:
    return dict(core.BOUNDS)[name]


def _first_table_row_limit_violation(table: str, observed: int) -> tuple[str, int] | None:
    for target, bound_name, code in core.ROW_LIMIT_PRECEDENCE:
        applies = (
            target == table
            or target == "NODE_TABLES" and table in core.NODE_TABLES
            or target == "ADMIN_TABLES" and table in core.ADMIN_TABLES
        )
        limit = _bound(bound_name)
        if applies and observed > limit:
            return code, limit
    return None


def _pointer(path: tuple[str, ...]) -> str:
    return "".join("/" + part.replace("~", "~0").replace("/", "~1") for part in path)


def _walk_value(
    value: Any, spec: Any, path: tuple[str, ...], depth: int, *,
    table: str, row_id: str | None,
) -> None:
    if depth > _bound("maxJsonDepth"):
        _refuse("E_JSON_DEPTH_LIMIT", table, observed=depth, limit=_bound("maxJsonDepth"), row=row_id, field=_pointer(path))
    if isinstance(value, (dict, list)) and len(value) > _bound("maxContainerItems"):
        _refuse("E_CONTAINER_ITEMS_LIMIT", table, observed=len(value), limit=_bound("maxContainerItems"), row=row_id, field=_pointer(path))
    if isinstance(value, str):
        observed = len(value.encode("utf-8"))
        if observed > _bound("maxStringUtf8Bytes"):
            _refuse("E_STRING_BYTES_LIMIT", table, observed=observed, limit=_bound("maxStringUtf8Bytes"), row=row_id, field=_pointer(path))
    if isinstance(value, int) and not isinstance(value, bool) and getattr(spec, "kind", None) in ("u16", "u32", "u64", "i64"):
        limits = {
            "u16": (0, 65_535), "u32": (0, 4_294_967_295),
            "u64": (0, 18_446_744_073_709_551_615),
            "i64": (-9_223_372_036_854_775_808, 9_223_372_036_854_775_807),
        }
        minimum, maximum = limits[spec.kind]
        if value < minimum:
            _refuse("E_INTEGER_RANGE", table, observed=value, limit=minimum, row=row_id, field=_pointer(path))
        if value > maximum:
            _refuse("E_INTEGER_RANGE", table, observed=value, limit=maximum, row=row_id, field=_pointer(path))
    if isinstance(value, dict):
        if getattr(spec, "kind", None) == "record":
            children = dict(spec.fields)
            order = tuple(children)
        elif getattr(spec, "kind", None) == "map":
            children = {key: spec.item for key in spec.values}
            order = tuple(spec.values)
        else:
            children = {key: None for key in value}
            order = tuple(sorted(value))
        for key in order:
            if key in value:
                _walk_value(value[key], children.get(key), path + (str(key),), depth + 1, table=table, row_id=row_id)
    elif isinstance(value, list):
        child = getattr(spec, "item", None)
        for index, item in enumerate(value):
            _walk_value(item, child, path + (str(index),), depth + 1, table=table, row_id=row_id)


def _validate_row(table: str, row: Mapping[str, Any]) -> bytes:
    if not isinstance(row, Mapping):
        _refuse("E_ROW_SHAPE", table)
    spec = core.table_spec(table)
    row_id = str(row.get("id")) if row.get("id") is not None else None
    if len(row) > _bound("maxContainerItems"):
        _refuse("E_CONTAINER_ITEMS_LIMIT", table, observed=len(row), limit=_bound("maxContainerItems"), row=row_id, field="")
    for field in spec.fields:
        if field.name in row:
            _walk_value(row[field.name], field.value, (field.name,), 1, table=table, row_id=row_id)
    try:
        core.validate_row_shape(table, row)
    except core.ContractError:
        _refuse("E_ROW_SHAPE", table, row=str(row.get("id")) if row.get("id") is not None else None)
    try:
        core.validate_identity(table, row)
    except core.ContractError:
        _refuse("E_ROW_IDENTITY", table, row=str(row.get("id")) if row.get("id") is not None else None)
    try:
        canonical_size = _canonical_json_size(row)
    except (TypeError, ValueError, UnicodeError):
        _refuse("E_CANONICAL_JSON", table, row=str(row.get("id")))
    line_bytes = canonical_size + 1
    if line_bytes > _bound("maxJsonlLineBytes"):
        _refuse("E_LINE_BYTES_LIMIT", table, observed=line_bytes, limit=_bound("maxJsonlLineBytes"), row=str(row.get("id")))
    try:
        return canonical_row_bytes(row)
    except (TypeError, ValueError, UnicodeError):
        _refuse("E_CANONICAL_JSON", table, row=str(row.get("id")))


def _normalize_component(value: Any, normalization: str) -> Any:
    if value is None:
        return None
    if not isinstance(value, str):
        _refuse("E_ROW_SHAPE")
    if normalization == "ci":
        return normalize_casefold(value)
    if normalization == "ascii" and value.isascii():
        return value
    _refuse("E_ROW_SHAPE")


def _key_json(values: Sequence[Any]) -> bytes:
    return json.dumps(list(values), ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")


class SemanticGraphStore:
    """Owned connection to one contained semantic graph database."""

    def __init__(self, connection: sqlite3.Connection, database_path: Path, core_artifact_path: Path, *, readonly: bool, _token: object | None = None) -> None:
        if _token is not _CONSTRUCTION_TOKEN:
            _refuse("E_SQLITE_CONTRACT")
        self._connection = connection
        self._database_path = database_path
        self._core_artifact_path = core_artifact_path
        self._readonly = readonly
        self._closed = False
        self._finalized = readonly
        self._failed = False
        self._next_table_ord = 0
        self._table_counts = [0 for _ in core.ALL_TABLES]
        self._node_rows = 0
        self._total_rows = 0

    @classmethod
    def create(cls, database_path: str | os.PathLike[str], *, core_artifact_path: str | os.PathLike[str]) -> SemanticGraphStore:
        core_path, _ = _load_core_artifact(Path(core_artifact_path))
        path = _assert_workspace_path(Path(database_path), absent=True)
        path.parent.mkdir(parents=True, exist_ok=True)
        _assert_workspace_path(path, absent=True)
        connection: sqlite3.Connection | None = None
        try:
            connection = sqlite3.connect(path)
            connection.execute("PRAGMA page_size=4096")
            connection.execute("PRAGMA encoding='UTF-8'")
            connection.execute(f"PRAGMA application_id={_APPLICATION_ID}")
            connection.execute(f"PRAGMA user_version={_USER_VERSION}")
            connection.execute("PRAGMA foreign_keys=ON")
            connection.execute("PRAGMA trusted_schema=OFF")
            connection.execute("PRAGMA journal_mode=DELETE")
            connection.execute("PRAGMA synchronous=FULL")
            connection.execute("BEGIN IMMEDIATE")
            for _, ddl in _TABLE_DDL:
                connection.execute(ddl)
            for _, ddl in _INDEX_DDL:
                connection.execute(ddl)
            return cls(connection, path, core_path, readonly=False, _token=_CONSTRUCTION_TOKEN)
        except StoreRefusal:
            if connection is not None:
                connection.close()
            if path.exists():
                path.unlink()
            raise
        except (OSError, sqlite3.Error):
            if connection is not None:
                connection.close()
            if path.exists():
                path.unlink()
            _refuse("E_SQLITE_CONTRACT")

    @classmethod
    def open_readonly(cls, database_path: str | os.PathLike[str], *, core_artifact_path: str | os.PathLike[str]) -> SemanticGraphStore:
        core_path, _ = _load_core_artifact(Path(core_artifact_path))
        path = _assert_workspace_path(Path(database_path))
        if not path.is_file():
            _refuse("E_PATH_CONTAINMENT")
        try:
            uri = path.as_uri() + "?mode=ro"
            connection = sqlite3.connect(uri, uri=True)
            result = cls(connection, path, core_path, readonly=True, _token=_CONSTRUCTION_TOKEN)
            connection.execute("PRAGMA foreign_keys=ON")
            connection.execute("PRAGMA trusted_schema=OFF")
            connection.execute("PRAGMA query_only=ON")
            result._verify_sqlite_contract()
            result._load_finalized_meta()
            return result
        except StoreRefusal:
            if "connection" in locals():
                connection.close()
            raise
        except sqlite3.Error:
            if "connection" in locals():
                connection.close()
            _refuse("E_SQLITE_CONTRACT")

    def ingest_table(self, table: str, rows: object) -> int:
        self._require_writable()
        if self._failed or self._next_table_ord >= len(core.ALL_TABLES) or table != core.ALL_TABLES[self._next_table_ord]:
            _refuse("E_ROW_ORDER", table if isinstance(table, str) else "store")
        try:
            iterator = iter(rows)  # type: ignore[arg-type]
        except TypeError:
            _refuse("E_ROW_SHAPE", table)
        table_ord = self._next_table_ord
        spec = core.table_spec(table)
        count = 0
        try:
            self._connection.execute("SAVEPOINT ingest_table")
            for supplied in iterator:
                if not isinstance(supplied, Mapping):
                    _refuse("E_ROW_SHAPE", table)
                row = dict(supplied)
                encoded = _validate_row(table, row)
                next_table_count = count + 1
                exists = self._connection.execute(
                    "SELECT 1 FROM rows WHERE table_ord=? AND row_id=?",
                    (table_ord, row["id"]),
                ).fetchone()
                if exists is not None:
                    _refuse("E_DUPLICATE_ID", table, row=row["id"])
                violation = _first_table_row_limit_violation(table, next_table_count)
                if violation is not None:
                    code, limit = violation
                    _refuse(code, table, observed=next_table_count, limit=limit)
                next_node_rows = self._node_rows + (1 if table in core.NODE_TABLES else 0)
                if next_node_rows > _bound("maxNodeRowsTotal"):
                    _refuse("E_NODE_ROWS_TOTAL_LIMIT", table, observed=next_node_rows, limit=_bound("maxNodeRowsTotal"))
                next_total = self._total_rows + 1
                if next_total > _bound("maxRowsTotal"):
                    _refuse("E_TOTAL_ROWS_LIMIT", table, observed=next_total, limit=_bound("maxRowsTotal"))
                self._connection.execute(
                    "INSERT INTO rows(table_ord,row_id,row_json,row_sha256) VALUES(?,?,?,?)",
                    (table_ord, row["id"], encoded, hashlib.sha256(encoded).hexdigest()),
                )
                self._project_foreign_keys(table_ord, spec, row)
                self._project_lookups(table_ord, table, row)
                self._project_paths(table_ord, table, row)
                count = next_table_count
                self._node_rows = next_node_rows
                self._total_rows = next_total
            self._connection.execute("RELEASE ingest_table")
        except StoreRefusal:
            self._connection.execute("ROLLBACK TO ingest_table")
            self._connection.execute("RELEASE ingest_table")
            self._node_rows -= count if table in core.NODE_TABLES else 0
            self._total_rows -= count
            self._failed = True
            self._abort_prefinalize()
            raise
        except (sqlite3.Error, TypeError, ValueError):
            self._connection.execute("ROLLBACK TO ingest_table")
            self._connection.execute("RELEASE ingest_table")
            self._node_rows -= count if table in core.NODE_TABLES else 0
            self._total_rows -= count
            self._failed = True
            self._abort_prefinalize()
            _refuse("E_SQLITE_CONTRACT", table)
        except BaseException:
            try:
                self._connection.execute("ROLLBACK TO ingest_table")
                self._connection.execute("RELEASE ingest_table")
            finally:
                self._node_rows -= count if table in core.NODE_TABLES else 0
                self._total_rows -= count
                self._failed = True
                self._abort_prefinalize()
            raise
        self._table_counts[table_ord] = count
        self._next_table_ord += 1
        return count

    def finalize(self) -> StoreSummary:
        self._require_writable()
        if self._failed or self._next_table_ord != len(core.ALL_TABLES):
            self._abort_prefinalize()
            _refuse("E_ROW_ORDER")
        try:
            summary = self._summary()
            meta = {
                "core_artifact_sha256": _CORE_ARTIFACT_SHA256,
                "finalized": "1",
                "table_counts": json.dumps(list(summary.table_counts), separators=(",", ":")),
                "total_rows": str(summary.total_rows),
                "foreign_key_count": str(summary.foreign_key_count),
                "lookup_count": str(summary.lookup_count),
                "path_count": str(summary.path_count),
            }
            self._connection.executemany(
                "INSERT INTO meta(key,value) VALUES(?,?)",
                ((key, value.encode("utf-8")) for key, value in meta.items()),
            )
            self._connection.execute("COMMIT")
            self._connection.execute("PRAGMA query_only=ON")
            self._finalized = True
            return summary
        except sqlite3.Error:
            self._failed = True
            self._abort_prefinalize()
            _refuse("E_SQLITE_CONTRACT")

    def get_row(self, table: str, row_id: str) -> dict | None:
        table_ord = self._table_ordinal(table)
        self._require_id(table, row_id)
        result = self._connection.execute(
            "SELECT row_json FROM rows WHERE table_ord=? AND row_id=?",
            (table_ord, row_id),
        ).fetchone()
        return None if result is None else json.loads(bytes(result[0]).decode("utf-8"))

    def iter_rows(self, table: str) -> Iterator[dict]:
        table_ord = self._table_ordinal(table)
        cursor = self._connection.execute(
            "SELECT row_json FROM rows WHERE table_ord=? ORDER BY row_id",
            (table_ord,),
        )
        for (raw,) in cursor:
            yield json.loads(bytes(raw).decode("utf-8"))

    def iter_foreign_keys(self, source_table: str, source_id: str, field: str | None = None) -> Iterator[ForeignKeyRef]:
        table_ord = self._table_ordinal(source_table)
        self._require_id(source_table, source_id)
        spec = core.table_spec(source_table)
        field_names = tuple(item.name for item in spec.fields)
        parameters: list[Any] = [table_ord, source_id]
        where = "source_table_ord=? AND source_id=?"
        if field is not None:
            if field not in field_names or not spec.fields[field_names.index(field)].fk_tables:
                _refuse("E_ROW_SHAPE", source_table, row=source_id, field=field)
            where += " AND field_ord=?"
            parameters.append(field_names.index(field))
        field_ranks = {name: rank for rank, name in enumerate(sorted(field_names))}
        table_ranks = {name: rank for rank, name in enumerate(sorted(core.ALL_TABLES))}
        field_case = "CASE field_ord " + " ".join(
            f"WHEN {ordinal} THEN {field_ranks[name]}"
            for ordinal, name in enumerate(field_names)
        ) + " END"
        target_case = "CASE target_table_ord " + " ".join(
            f"WHEN {ordinal} THEN {table_ranks[name]}"
            for ordinal, name in enumerate(core.ALL_TABLES)
        ) + " END"
        cursor = self._connection.execute(
            "SELECT source_table_ord,source_id,field_ord,value_ord,target_table_ord,target_id "
            f"FROM foreign_key_index WHERE {where} "
            f"ORDER BY {field_case},value_ord,{target_case},target_id",
            parameters,
        )
        for source_ord, sid, field_ord, value_ord, target_ord, target_id in cursor:
            yield ForeignKeyRef(core.ALL_TABLES[source_ord], sid, field_names[field_ord], value_ord, core.ALL_TABLES[target_ord], target_id)

    def iter_references_to(self, target_table: str, target_id: str) -> Iterator[ForeignKeyRef]:
        target_ord = self._table_ordinal(target_table)
        self._require_id(target_table, target_id)
        table_ranks = {name: rank for rank, name in enumerate(sorted(core.ALL_TABLES))}
        source_case = "CASE source_table_ord " + " ".join(
            f"WHEN {ordinal} THEN {table_ranks[name]}"
            for ordinal, name in enumerate(core.ALL_TABLES)
        ) + " END"
        field_clauses: list[str] = []
        for source_ord, table in enumerate(core.ALL_TABLES):
            field_names = tuple(field.name for field in core.table_spec(table).fields)
            field_ranks = {name: rank for rank, name in enumerate(sorted(field_names))}
            for field_ord, field_name in enumerate(field_names):
                field_clauses.append(
                    f"WHEN source_table_ord={source_ord} AND field_ord={field_ord} THEN {field_ranks[field_name]}"
                )
        field_case = "CASE " + " ".join(field_clauses) + " END"
        cursor = self._connection.execute(
            "SELECT source_table_ord,source_id,field_ord,value_ord,target_table_ord,target_id "
            "FROM foreign_key_index WHERE target_table_ord=? AND target_id=? "
            f"ORDER BY {source_case},source_id,{field_case},value_ord,target_id",
            (target_ord, target_id),
        )
        for source_ord, source_id, field_ord, value_ord, target_table_ord, tid in cursor:
            spec = core.table_spec(core.ALL_TABLES[source_ord])
            yield ForeignKeyRef(core.ALL_TABLES[source_ord], source_id, spec.fields[field_ord].name, value_ord, core.ALL_TABLES[target_table_ord], tid)

    def lookup(self, index_name: str, key: Sequence[object]) -> Iterator[dict]:
        projection_ord, projection = self._lookup_projection(index_name)
        if isinstance(key, (str, bytes, bytearray)) or not isinstance(key, Sequence) or len(key) != len(projection[2]):
            _refuse("E_ROW_SHAPE")
        table_spec = core.table_spec(projection[1])
        field_specs = {field.name: field for field in table_spec.fields}
        normalized_values = []
        for value, (field_name, normalization) in zip(key, projection[2]):
            if value is None and not field_specs[field_name].value.nullable:
                _refuse("E_ROW_SHAPE")
            normalized_values.append(_normalize_component(value, normalization))
        normalized = tuple(normalized_values)
        encoded = _key_json(normalized)
        cursor = self._connection.execute(
            "SELECT r.row_json FROM lookup_index l JOIN rows r ON r.table_ord=l.row_table_ord AND r.row_id=l.row_id "
            "WHERE l.projection_ord=? AND l.key_json=? ORDER BY l.sort_integer,l.row_id",
            (projection_ord, encoded),
        )
        for (raw,) in cursor:
            yield json.loads(bytes(raw).decode("utf-8"))

    def lookup_path(self, index_name: str, path: str) -> Iterator[dict]:
        projection_ord, _ = self._path_projection(index_name)
        try:
            exact = core.canonical_path(path)
        except core.ContractError:
            _refuse("E_ROW_SHAPE")
        folded = normalize_casefold(exact)
        cursor = self._connection.execute(
            "SELECT r.row_json FROM path_index p JOIN rows r ON r.table_ord=p.row_table_ord AND r.row_id=p.row_id "
            "WHERE p.projection_ord=? AND p.path_casefold=? ORDER BY p.row_id",
            (projection_ord, folded),
        )
        for (raw,) in cursor:
            yield json.loads(bytes(raw).decode("utf-8"))

    def iter_safe_subtree(self, index_name: str, root_path: str) -> Iterator[dict]:
        projection_ord, _ = self._path_projection(index_name)
        try:
            root_exact = core.canonical_path(root_path)
        except core.ContractError:
            _refuse("E_ROW_SHAPE")
        prefix = normalize_casefold(root_exact) + "/"
        upper = normalize_casefold(root_exact) + "0"
        root_parts = normalize_casefold(root_exact).split("/")
        cursor = self._connection.execute(
            "SELECT p.path_exact,r.row_json FROM path_index p JOIN rows r ON r.table_ord=p.row_table_ord AND r.row_id=p.row_id "
            "WHERE p.projection_ord=? AND p.path_casefold>=? AND p.path_casefold<? ORDER BY p.row_id",
            (projection_ord, prefix, upper),
        )
        for exact, raw in cursor:
            parts = normalize_casefold(exact).split("/")
            if len(parts) > len(root_parts) and parts[:len(root_parts)] == root_parts:
                yield json.loads(bytes(raw).decode("utf-8"))

    def greatest_preceding_assignment(self, document_id: str, root_kind: str, root_name: str | None, before_line: int) -> dict | None:
        self._require_id("documents", document_id)
        if not isinstance(root_kind, str) or root_name is not None and not isinstance(root_name, str) or isinstance(before_line, bool) or not isinstance(before_line, int) or before_line < 0:
            _refuse("E_ROW_SHAPE")
        projection_ord, _ = self._lookup_projection("assignment-lexical")
        key = _key_json((document_id, normalize_casefold(root_kind), None if root_name is None else normalize_casefold(root_name)))
        rows = self._connection.execute(
            "SELECT l.sort_integer,l.row_id,r.row_json FROM lookup_index l JOIN rows r ON r.table_ord=l.row_table_ord AND r.row_id=l.row_id "
            "WHERE l.projection_ord=? AND l.key_json=? AND l.sort_integer<? ORDER BY l.sort_integer DESC,l.row_id LIMIT 2",
            (projection_ord, key, before_line),
        ).fetchall()
        if not rows:
            return None
        if len(rows) > 1 and rows[0][0] == rows[1][0]:
            _refuse("E_ROW_ORDER", "assignments", observed=rows[0][0], row=rows[0][1], field="/line")
        return json.loads(bytes(rows[0][2]).decode("utf-8"))

    def close(self) -> None:
        if self._closed:
            return
        try:
            if not self._readonly and not self._finalized:
                try:
                    self._connection.execute("ROLLBACK")
                except sqlite3.Error:
                    pass
            self._connection.close()
        finally:
            self._closed = True
            if not self._readonly and not self._finalized and self._database_path.exists():
                self._database_path.unlink()

    def __enter__(self) -> SemanticGraphStore:
        self._ensure_open()
        return self

    def __exit__(self, exc_type: type[BaseException] | None, exc_value: BaseException | None, traceback: TracebackType | None) -> None:
        self.close()

    def _ensure_open(self) -> None:
        if self._closed:
            _refuse("E_SQLITE_CONTRACT")

    def _abort_prefinalize(self) -> None:
        if self._closed or self._readonly or self._finalized:
            return
        try:
            self._connection.rollback()
        except sqlite3.Error:
            pass
        self._connection.close()
        self._closed = True
        try:
            if os.path.lexists(self._database_path):
                self._database_path.unlink()
        except OSError:
            _refuse("E_SQLITE_CONTRACT")

    def _require_writable(self) -> None:
        self._ensure_open()
        if self._readonly or self._finalized:
            _refuse("E_SQLITE_CONTRACT")

    def _table_ordinal(self, table: str) -> int:
        self._ensure_open()
        if table not in core.ALL_TABLES:
            _refuse("E_ROW_SHAPE", str(table))
        return core.ALL_TABLES.index(table)

    def _require_id(self, table: str, row_id: str) -> None:
        prefix = core.table_spec(table).prefix
        if not isinstance(row_id, str) or not _valid_row_id(row_id, prefix):
            _refuse("E_ROW_SHAPE", table, row=str(row_id))

    def _project_foreign_keys(self, table_ord: int, spec: Any, row: Mapping[str, Any]) -> None:
        for field_ord, field in enumerate(spec.fields):
            if not field.fk_tables:
                continue
            value = row[field.name]
            values = () if value is None else tuple(value) if isinstance(value, list) else (value,)
            for value_ord, identity in enumerate(values):
                target = next((name for name in field.fk_tables if identity.startswith(core.table_spec(name).prefix)), None)
                if target is None:
                    _refuse("E_ROW_SHAPE", spec.name, row=row["id"], field="/" + field.name)
                self._connection.execute(
                    "INSERT INTO foreign_key_index(source_table_ord,source_id,field_ord,value_ord,target_table_ord,target_id) VALUES(?,?,?,?,?,?)",
                    (table_ord, row["id"], field_ord, value_ord, core.ALL_TABLES.index(target), identity),
                )

    def _project_lookups(self, table_ord: int, table: str, row: Mapping[str, Any]) -> None:
        for projection_ord, projection in enumerate(_LOOKUP_PROJECTIONS):
            _, projection_table, components, sort_field, sort_default, omit_null = projection
            if projection_table != table:
                continue
            if any(row[field] is None for field in omit_null):
                continue
            values = tuple(_normalize_component(row[field], normalization) for field, normalization in components)
            sort_integer = sort_default if sort_field is None else row[sort_field]
            self._connection.execute(
                "INSERT INTO lookup_index(projection_ord,key_json,sort_integer,row_table_ord,row_id) VALUES(?,?,?,?,?)",
                (projection_ord, _key_json(values), sort_integer, table_ord, row["id"]),
            )

    def _project_paths(self, table_ord: int, table: str, row: Mapping[str, Any]) -> None:
        for projection_ord, (_, projection_table, field) in enumerate(_PATH_PROJECTIONS):
            if projection_table != table:
                continue
            try:
                exact = core.canonical_path(row[field])
            except core.ContractError:
                _refuse("E_ROW_SHAPE", table, row=row["id"], field="/" + field)
            self._connection.execute(
                "INSERT INTO path_index(projection_ord,path_casefold,path_exact,row_table_ord,row_id) VALUES(?,?,?,?,?)",
                (projection_ord, normalize_casefold(exact), exact, table_ord, row["id"]),
            )

    def _lookup_projection(self, name: str) -> tuple[int, tuple[Any, ...]]:
        for ordinal, projection in enumerate(_LOOKUP_PROJECTIONS):
            if projection[0] == name:
                return ordinal, projection
        _refuse("E_ROW_SHAPE")

    def _path_projection(self, name: str) -> tuple[int, tuple[str, str, str]]:
        for ordinal, projection in enumerate(_PATH_PROJECTIONS):
            if projection[0] == name:
                return ordinal, projection
        _refuse("E_ROW_SHAPE")

    def _summary(self) -> StoreSummary:
        fk_count = self._connection.execute("SELECT count(*) FROM foreign_key_index").fetchone()[0]
        lookup_count = self._connection.execute("SELECT count(*) FROM lookup_index").fetchone()[0]
        path_count = self._connection.execute("SELECT count(*) FROM path_index").fetchone()[0]
        return StoreSummary(
            _CORE_ARTIFACT_SHA256,
            tuple(zip(core.ALL_TABLES, self._table_counts)),
            self._total_rows, fk_count, lookup_count, path_count,
        )

    def _verify_sqlite_contract(self) -> None:
        try:
            values = {
                "application_id": self._connection.execute("PRAGMA application_id").fetchone()[0],
                "user_version": self._connection.execute("PRAGMA user_version").fetchone()[0],
                "encoding": self._connection.execute("PRAGMA encoding").fetchone()[0],
                "page_size": self._connection.execute("PRAGMA page_size").fetchone()[0],
                "foreign_keys": self._connection.execute("PRAGMA foreign_keys").fetchone()[0],
                "trusted_schema": self._connection.execute("PRAGMA trusted_schema").fetchone()[0],
                "journal_mode": self._connection.execute("PRAGMA journal_mode").fetchone()[0],
                "synchronous": self._connection.execute("PRAGMA synchronous").fetchone()[0],
                "query_only": self._connection.execute("PRAGMA query_only").fetchone()[0],
            }
            expected = {
                "application_id": _APPLICATION_ID, "user_version": _USER_VERSION,
                "encoding": "UTF-8", "page_size": 4096, "foreign_keys": 1,
                "trusted_schema": 0, "journal_mode": "delete", "synchronous": 2,
                "query_only": 1,
            }
            if values != expected:
                _refuse("E_SQLITE_CONTRACT")
            schema = tuple(
                row for row in self._connection.execute(
                    "SELECT type,name,sql FROM sqlite_schema ORDER BY type,name"
                )
                if not row[1].startswith("sqlite_")
            )
            expected_rows = tuple(sorted(
                [("table", name, ddl) for name, ddl in _TABLE_DDL]
                + [("index", name, ddl) for name, ddl in _INDEX_DDL],
                key=lambda item: (item[0], item[1]),
            ))
            if schema != expected_rows:
                _refuse("E_SQLITE_CONTRACT")
        except sqlite3.Error:
            _refuse("E_SQLITE_CONTRACT")

    def _load_finalized_meta(self) -> None:
        try:
            meta_rows = tuple(self._connection.execute(
                "SELECT key,value,typeof(value) FROM meta ORDER BY key"
            ))
            expected_meta_keys = {
                "core_artifact_sha256", "finalized", "table_counts",
                "total_rows", "foreign_key_count", "lookup_count", "path_count",
            }
            if (
                len(meta_rows) != len(expected_meta_keys)
                or {item[0] for item in meta_rows} != expected_meta_keys
                or any(item[2] != "blob" for item in meta_rows)
            ):
                _refuse("E_SQLITE_CONTRACT")
            rows = {key: bytes(value).decode("utf-8") for key, value, _ in meta_rows}
            if rows["core_artifact_sha256"] != _CORE_ARTIFACT_SHA256 or rows["finalized"] != "1":
                _refuse("E_SQLITE_CONTRACT")
            counts = json.loads(rows["table_counts"])
            if (
                not isinstance(counts, list)
                or len(counts) != len(core.ALL_TABLES)
                or json.dumps(counts, ensure_ascii=False, separators=(",", ":")) != rows["table_counts"]
            ):
                _refuse("E_SQLITE_CONTRACT")
            parsed_counts: list[int] = []
            for ordinal, item in enumerate(counts):
                if (
                    not isinstance(item, list) or len(item) != 2
                    or item[0] != core.ALL_TABLES[ordinal]
                    or isinstance(item[1], bool) or not isinstance(item[1], int)
                    or item[1] < 0 or item[1] > 18_446_744_073_709_551_615
                ):
                    _refuse("E_SQLITE_CONTRACT")
                parsed_counts.append(item[1])

            def meta_u64(name: str) -> int:
                text = rows[name]
                if not text or not text.isascii() or not text.isdigit() or len(text) > 1 and text.startswith("0"):
                    _refuse("E_SQLITE_CONTRACT")
                value = int(text)
                if value > 18_446_744_073_709_551_615:
                    _refuse("E_SQLITE_CONTRACT")
                return value

            self._table_counts = parsed_counts
            self._total_rows = meta_u64("total_rows")
            self._node_rows = sum(self._table_counts[index] for index in range(len(core.NODE_TABLES)))
            self._next_table_ord = len(core.ALL_TABLES)
            actual = self._summary()
            bad_storage_types = (
                self._connection.execute(
                    "SELECT count(*) FROM rows WHERE typeof(table_ord)!='integer' OR typeof(row_id)!='text' OR typeof(row_json)!='blob' OR typeof(row_sha256)!='text'"
                ).fetchone()[0]
                + self._connection.execute(
                    "SELECT count(*) FROM foreign_key_index WHERE typeof(source_table_ord)!='integer' OR typeof(source_id)!='text' OR typeof(field_ord)!='integer' OR typeof(value_ord)!='integer' OR typeof(target_table_ord)!='integer' OR typeof(target_id)!='text'"
                ).fetchone()[0]
                + self._connection.execute(
                    "SELECT count(*) FROM lookup_index WHERE typeof(projection_ord)!='integer' OR typeof(key_json)!='blob' OR typeof(sort_integer)!='integer' OR typeof(row_table_ord)!='integer' OR typeof(row_id)!='text'"
                ).fetchone()[0]
                + self._connection.execute(
                    "SELECT count(*) FROM path_index WHERE typeof(projection_ord)!='integer' OR typeof(path_casefold)!='text' OR typeof(path_exact)!='text' OR typeof(row_table_ord)!='integer' OR typeof(row_id)!='text'"
                ).fetchone()[0]
            )
            if bad_storage_types:
                _refuse("E_SQLITE_CONTRACT")
            physical_counts = tuple(
                self._connection.execute(
                    "SELECT count(*) FROM rows WHERE table_ord=?", (ordinal,)
                ).fetchone()[0]
                for ordinal in range(len(core.ALL_TABLES))
            )
            invalid_ordinals = self._connection.execute(
                "SELECT count(*) FROM rows WHERE table_ord<0 OR table_ord>=?",
                (len(core.ALL_TABLES),),
            ).fetchone()[0]
            bad_row_hashes = 0
            expected_fk_count = 0
            expected_lookup_count = 0
            expected_path_count = 0
            bounded_table_counts = [0 for _ in core.ALL_TABLES]
            bounded_node_rows = 0
            bounded_total_rows = 0
            for table_ord, row_id, row_json, row_sha256 in self._connection.execute(
                "SELECT table_ord,row_id,row_json,row_sha256 FROM rows ORDER BY table_ord,row_id"
            ):
                if isinstance(table_ord, bool) or not isinstance(table_ord, int) or not 0 <= table_ord < len(core.ALL_TABLES):
                    bad_row_hashes += 1
                    break
                table = core.ALL_TABLES[table_ord]
                payload = bytes(row_json)
                if (
                    not isinstance(row_sha256, str) or not _valid_sha256(row_sha256)
                    or hashlib.sha256(payload).hexdigest() != row_sha256
                ):
                    bad_row_hashes += 1
                    break
                try:
                    row = json.loads(payload.decode("utf-8"))
                except (UnicodeDecodeError, json.JSONDecodeError):
                    bad_row_hashes += 1
                    break
                try:
                    validated = _validate_row(table, row)
                except StoreRefusal:
                    bad_row_hashes += 1
                    break
                if not isinstance(row_id, str) or row.get("id") != row_id or validated != payload:
                    bad_row_hashes += 1
                    break
                next_table_count = bounded_table_counts[table_ord] + 1
                violation = _first_table_row_limit_violation(table, next_table_count)
                if violation is not None:
                    code, limit = violation
                    _refuse(code, table, observed=next_table_count, limit=limit)
                next_node_rows = bounded_node_rows + (1 if table in core.NODE_TABLES else 0)
                if next_node_rows > _bound("maxNodeRowsTotal"):
                    _refuse(
                        "E_NODE_ROWS_TOTAL_LIMIT", table,
                        observed=next_node_rows, limit=_bound("maxNodeRowsTotal"),
                    )
                next_total_rows = bounded_total_rows + 1
                if next_total_rows > _bound("maxRowsTotal"):
                    _refuse(
                        "E_TOTAL_ROWS_LIMIT", table,
                        observed=next_total_rows, limit=_bound("maxRowsTotal"),
                    )
                bounded_table_counts[table_ord] = next_table_count
                bounded_node_rows = next_node_rows
                bounded_total_rows = next_total_rows
                spec = core.table_spec(table)
                for field_ord, field_spec in enumerate(spec.fields):
                    if not field_spec.fk_tables:
                        continue
                    value = row[field_spec.name]
                    values = () if value is None else tuple(value) if isinstance(value, list) else (value,)
                    for value_ord, target_id in enumerate(values):
                        target_table = next(
                            (name for name in field_spec.fk_tables if target_id.startswith(core.table_spec(name).prefix)),
                            None,
                        )
                        if target_table is None:
                            bad_row_hashes += 1
                            break
                        matches = self._connection.execute(
                            "SELECT count(*) FROM foreign_key_index WHERE source_table_ord=? AND source_id=? AND field_ord=? AND value_ord=? AND target_table_ord=? AND target_id=?",
                            (table_ord, row_id, field_ord, value_ord, core.ALL_TABLES.index(target_table), target_id),
                        ).fetchone()[0]
                        if matches != 1:
                            bad_row_hashes += 1
                            break
                        expected_fk_count += 1
                    if bad_row_hashes:
                        break
                if bad_row_hashes:
                    break
                for projection_ord, projection in enumerate(_LOOKUP_PROJECTIONS):
                    _, projection_table, components, sort_field, sort_default, omit_null = projection
                    if projection_table != table or any(row[field] is None for field in omit_null):
                        continue
                    try:
                        key_json = _key_json(tuple(
                            _normalize_component(row[field], normalization)
                            for field, normalization in components
                        ))
                    except StoreRefusal:
                        bad_row_hashes += 1
                        break
                    sort_integer = sort_default if sort_field is None else row[sort_field]
                    matches = self._connection.execute(
                        "SELECT count(*) FROM lookup_index WHERE projection_ord=? AND key_json=? AND sort_integer=? AND row_table_ord=? AND row_id=?",
                        (projection_ord, key_json, sort_integer, table_ord, row_id),
                    ).fetchone()[0]
                    if matches != 1:
                        bad_row_hashes += 1
                        break
                    expected_lookup_count += 1
                if bad_row_hashes:
                    break
                for projection_ord, (_, projection_table, field) in enumerate(_PATH_PROJECTIONS):
                    if projection_table != table:
                        continue
                    try:
                        exact = core.canonical_path(row[field])
                    except core.ContractError:
                        bad_row_hashes += 1
                        break
                    matches = self._connection.execute(
                        "SELECT count(*) FROM path_index WHERE projection_ord=? AND path_casefold=? AND path_exact=? AND row_table_ord=? AND row_id=?",
                        (projection_ord, normalize_casefold(exact), exact, table_ord, row_id),
                    ).fetchone()[0]
                    if matches != 1:
                        bad_row_hashes += 1
                        break
                    expected_path_count += 1
                if bad_row_hashes:
                    break
            orphan_fk = self._connection.execute(
                "SELECT count(*) FROM foreign_key_index f LEFT JOIN rows r ON r.table_ord=f.source_table_ord AND r.row_id=f.source_id WHERE r.row_id IS NULL"
            ).fetchone()[0]
            orphan_lookup = self._connection.execute(
                "SELECT count(*) FROM lookup_index l LEFT JOIN rows r ON r.table_ord=l.row_table_ord AND r.row_id=l.row_id WHERE r.row_id IS NULL"
            ).fetchone()[0]
            orphan_path = self._connection.execute(
                "SELECT count(*) FROM path_index p LEFT JOIN rows r ON r.table_ord=p.row_table_ord AND r.row_id=p.row_id WHERE r.row_id IS NULL"
            ).fetchone()[0]
            bad_indexes = 0
            for source_ord, source_id, field_ord, value_ord, target_ord, target_id in self._connection.execute(
                "SELECT source_table_ord,source_id,field_ord,value_ord,target_table_ord,target_id FROM foreign_key_index ORDER BY source_table_ord,source_id,field_ord,value_ord"
            ):
                if not (0 <= source_ord < len(core.ALL_TABLES) and 0 <= target_ord < len(core.ALL_TABLES) and value_ord >= 0):
                    bad_indexes += 1
                    break
                spec = core.table_spec(core.ALL_TABLES[source_ord])
                if not (0 <= field_ord < len(spec.fields)) or core.ALL_TABLES[target_ord] not in spec.fields[field_ord].fk_tables or not _valid_row_id(target_id, core.table_spec(core.ALL_TABLES[target_ord]).prefix):
                    bad_indexes += 1
                    break
            if not bad_indexes:
                for projection_ord, key_json, sort_integer, row_table_ord, _ in self._connection.execute(
                    "SELECT projection_ord,key_json,sort_integer,row_table_ord,row_id FROM lookup_index ORDER BY projection_ord,key_json,sort_integer,row_id"
                ):
                    if not (0 <= projection_ord < len(_LOOKUP_PROJECTIONS)) or row_table_ord != core.ALL_TABLES.index(_LOOKUP_PROJECTIONS[projection_ord][1]) or isinstance(sort_integer, bool) or not isinstance(sort_integer, int):
                        bad_indexes += 1
                        break
                    try:
                        decoded = json.loads(bytes(key_json).decode("utf-8"))
                    except (UnicodeDecodeError, json.JSONDecodeError):
                        bad_indexes += 1
                        break
                    if not isinstance(decoded, list) or len(decoded) != len(_LOOKUP_PROJECTIONS[projection_ord][2]) or _key_json(decoded) != bytes(key_json):
                        bad_indexes += 1
                        break
            if not bad_indexes:
                for projection_ord, folded, exact, row_table_ord, _ in self._connection.execute(
                    "SELECT projection_ord,path_casefold,path_exact,row_table_ord,row_id FROM path_index ORDER BY projection_ord,path_casefold,row_id"
                ):
                    try:
                        valid_path = core.canonical_path(exact)
                    except core.ContractError:
                        bad_indexes += 1
                        break
                    if not (0 <= projection_ord < len(_PATH_PROJECTIONS)) or row_table_ord != core.ALL_TABLES.index(_PATH_PROJECTIONS[projection_ord][1]) or folded != normalize_casefold(valid_path):
                        bad_indexes += 1
                        break
            if (
                actual.total_rows != sum(self._table_counts)
                or physical_counts != tuple(self._table_counts)
                or invalid_ordinals or bad_row_hashes or orphan_fk or orphan_lookup or orphan_path or bad_indexes
                or actual.foreign_key_count != meta_u64("foreign_key_count")
                or actual.lookup_count != meta_u64("lookup_count")
                or actual.path_count != meta_u64("path_count")
                or actual.foreign_key_count != expected_fk_count
                or actual.lookup_count != expected_lookup_count
                or actual.path_count != expected_path_count
                or self._connection.execute("SELECT count(*) FROM rows").fetchone()[0] != actual.total_rows
            ):
                _refuse("E_SQLITE_CONTRACT")
        except (ValueError, TypeError, UnicodeError, json.JSONDecodeError, sqlite3.Error):
            _refuse("E_SQLITE_CONTRACT")


def _write_manifest_stream(metadata: sqlite3.Connection, path: Path) -> None:
    total_rows, total_bytes = metadata.execute(
        "SELECT COALESCE(sum(row_count),0),COALESCE(sum(byte_count),0) FROM shards"
    ).fetchone()
    shard_set = hashlib.sha256()
    for table_ord, table in enumerate(core.ALL_TABLES):
        for relative, digest, byte_count, row_count in metadata.execute(
            "SELECT path,sha256,byte_count,row_count FROM shards WHERE table_ord=? ORDER BY shard_index",
            (table_ord,),
        ):
            shard_set.update(
                f"{table}|{relative}|{digest}|{byte_count}|{row_count}\n".encode("utf-8")
            )

    def emit(stream: Any | None) -> tuple[int, str]:
        count = 0
        digest = hashlib.sha256()

        def write(fragment: bytes) -> None:
            nonlocal count
            prospective = count + len(fragment)
            if prospective + 1 > _bound("maxJsonlLineBytes"):
                _refuse("E_LINE_BYTES_LIMIT", "manifest", observed=prospective + 1, limit=_bound("maxJsonlLineBytes"))
            if total_bytes + prospective + 1 > _bound("maxOutputBytes"):
                _refuse("E_OUTPUT_BYTES_LIMIT", "manifest", observed=total_bytes + prospective + 1, limit=_bound("maxOutputBytes"))
            count = prospective
            digest.update(fragment)
            if stream is not None:
                stream.write(fragment)

        fixed = (
            b'{"adminTables":' + _canonical_array_bytes(core.ADMIN_TABLES)
            + b',"baselineId":' + json.dumps(core.BASELINE_ID).encode("ascii")
            + b',"bounds":' + canonical_row_bytes(dict(core.BOUNDS))
            + b',"nodeTables":' + _canonical_array_bytes(core.NODE_TABLES)
            + b',"schema":' + json.dumps(core.SCHEMA_NAME).encode("ascii")
            + b',"schemaVersion":' + str(core.SCHEMA_VERSION).encode("ascii")
            + b',"shardSetSha256":' + json.dumps(shard_set.hexdigest()).encode("ascii")
            + b',"tableOrder":' + _canonical_array_bytes(core.ALL_TABLES)
            + b',"tables":{'
        )
        write(fixed)
        for table_position, table in enumerate(sorted(core.ALL_TABLES)):
            table_ord = core.ALL_TABLES.index(table)
            row_count, table_sha = metadata.execute(
                "SELECT row_count,table_sha256 FROM table_meta WHERE table_ord=?",
                (table_ord,),
            ).fetchone()
            if table_position:
                write(b",")
            write(json.dumps(table).encode("ascii") + b':{"rowCount":' + str(row_count).encode("ascii") + b',"shards":[')
            for shard_position, row in enumerate(metadata.execute(
                "SELECT path,shard_index,row_count,byte_count,first_id,last_id,sha256 FROM shards WHERE table_ord=? ORDER BY shard_index",
                (table_ord,),
            )):
                relative, shard_index, shard_rows, shard_bytes, first_id, last_id, shard_sha = row
                if shard_position:
                    write(b",")
                write(canonical_row_bytes({
                    "path": relative, "shardIndex": shard_index,
                    "rowCount": shard_rows, "bytes": shard_bytes,
                    "firstId": first_id, "lastId": last_id, "sha256": shard_sha,
                }))
            write(b'],"tableSha256":' + json.dumps(table_sha).encode("ascii") + b"}")
        write(
            b'},"totalBytes":' + str(total_bytes).encode("ascii")
            + b',"totalRows":' + str(total_rows).encode("ascii") + b"}"
        )
        return count, digest.hexdigest()

    dry_count, dry_digest = emit(None)
    with path.open("xb") as stream:
        written_count, written_digest = emit(stream)
        stream.write(b"\n")
        stream.flush()
        os.fsync(stream.fileno())
    if (dry_count, dry_digest) != (written_count, written_digest):
        _refuse("E_CANONICAL_JSON", "manifest")


def publish_canonical_shards(store: SemanticGraphStore, output_directory: str | os.PathLike[str]) -> ManifestSummary:
    if not isinstance(store, SemanticGraphStore) or not store._finalized or store._closed:
        _refuse("E_SQLITE_CONTRACT")
    destination = _assert_workspace_path(Path(output_directory))
    parent = destination.parent
    parent.mkdir(parents=True, exist_ok=True)
    _assert_workspace_path(destination)
    temporary = Path(tempfile.mkdtemp(prefix=f".{destination.name}.semantic-store.", dir=parent))
    metadata_path = temporary / ".shards.sqlite3"
    metadata: sqlite3.Connection | None = None
    active_stream = None
    try:
        metadata = sqlite3.connect(metadata_path)
        metadata.execute("CREATE TABLE shards(table_ord INTEGER,path TEXT PRIMARY KEY,shard_index INTEGER,row_count INTEGER,byte_count INTEGER,first_id TEXT,last_id TEXT,sha256 TEXT)")
        metadata.execute("CREATE TABLE table_meta(table_ord INTEGER PRIMARY KEY,row_count INTEGER,table_sha256 TEXT)")
        total_written = 0
        for table_ord, table in enumerate(core.ALL_TABLES):
            shard_index = -1
            table_hash = hashlib.sha256()
            table_row_count = 0
            stream = None
            shard_path: Path | None = None
            shard_hash = hashlib.sha256()
            shard_rows = 0
            shard_bytes = 0
            first_id: str | None = None
            last_id: str | None = None

            def close_shard() -> None:
                nonlocal stream, active_stream, shard_rows, shard_bytes, first_id, last_id
                if stream is None or shard_path is None or first_id is None or last_id is None:
                    return
                stream.flush()
                os.fsync(stream.fileno())
                stream.close()
                relative = shard_path.relative_to(temporary).as_posix()
                metadata.execute(
                    "INSERT INTO shards VALUES(?,?,?,?,?,?,?,?)",
                    (table_ord, relative, shard_index, shard_rows, shard_bytes, first_id, last_id, shard_hash.hexdigest()),
                )
                stream = None
                active_stream = None

            for row in store.iter_rows(table):
                encoded = canonical_row_bytes(row) + b"\n"
                if len(encoded) > _bound("maxJsonlLineBytes"):
                    _refuse("E_LINE_BYTES_LIMIT", table, observed=len(encoded), limit=_bound("maxJsonlLineBytes"), row=row["id"])
                if 1 > _bound("maxRowsPerShard"):
                    _refuse("E_SHARD_ROWS_LIMIT", table, observed=1, limit=_bound("maxRowsPerShard"), row=row["id"])
                if len(encoded) > _bound("maxBytesPerShard"):
                    _refuse("E_SHARD_BYTES_LIMIT", table, observed=len(encoded), limit=_bound("maxBytesPerShard"), row=row["id"])
                if stream is not None and (shard_rows + 1 > _bound("maxRowsPerShard") or shard_bytes + len(encoded) > _bound("maxBytesPerShard")):
                    close_shard()
                if stream is None:
                    shard_index += 1
                    if shard_index >= _bound("maxShardsPerTable"):
                        _refuse("E_SHARD_COUNT_LIMIT", table, observed=shard_index + 1, limit=_bound("maxShardsPerTable"))
                    if total_written + len(encoded) > _bound("maxOutputBytes"):
                        _refuse("E_OUTPUT_BYTES_LIMIT", "manifest", observed=total_written + len(encoded), limit=_bound("maxOutputBytes"))
                    shard_path = temporary / "tables" / table / f"{shard_index:06d}.jsonl"
                    shard_path.parent.mkdir(parents=True, exist_ok=True)
                    stream = shard_path.open("xb")
                    active_stream = stream
                    shard_hash = hashlib.sha256()
                    shard_rows = 0
                    shard_bytes = 0
                    first_id = row["id"]
                elif total_written + len(encoded) > _bound("maxOutputBytes"):
                    _refuse("E_OUTPUT_BYTES_LIMIT", "manifest", observed=total_written + len(encoded), limit=_bound("maxOutputBytes"))
                stream.write(encoded)
                shard_hash.update(encoded)
                table_hash.update(encoded)
                shard_rows += 1
                table_row_count += 1
                shard_bytes += len(encoded)
                total_written += len(encoded)
                last_id = row["id"]
            close_shard()
            metadata.execute(
                "INSERT INTO table_meta VALUES(?,?,?)",
                (table_ord, table_row_count, table_hash.hexdigest()),
            )
        metadata.commit()
        _write_manifest_stream(metadata, temporary / "manifest.json")
        metadata.close()
        metadata = None
        metadata_path.unlink()
        summary = validate_canonical_shards(temporary, core_artifact_path=store._core_artifact_path)
        if destination.exists():
            existing = validate_canonical_shards(destination, core_artifact_path=store._core_artifact_path)
            if existing != summary:
                _refuse("E_PUBLISH_CONFLICT", "manifest")
            shutil.rmtree(temporary)
            return existing
        os.replace(temporary, destination)
        return summary
    except BaseException:
        if active_stream is not None and not active_stream.closed:
            active_stream.close()
        if metadata is not None:
            try:
                metadata.close()
            except sqlite3.Error:
                pass
        if temporary.exists():
            try:
                shutil.rmtree(temporary)
            except OSError:
                _refuse("E_PUBLISH_CONFLICT", "manifest")
        raise


class _ManifestCursor:
    # Contract-required streaming parser: shard metadata goes directly to
    # SQLite and is never retained as a whole-manifest Python list.
    def __init__(self, raw: bytes, metadata: sqlite3.Connection) -> None:
        self.raw = raw
        self.index = 0
        self.metadata = metadata

    def expect(self, token: bytes) -> None:
        if self.raw[self.index:self.index + len(token)] != token:
            _refuse("E_MANIFEST_SHAPE", "manifest")
        self.index += len(token)

    def string(self) -> str:
        start = self.index
        self.expect(b'"')
        escaped = False
        while self.index < len(self.raw):
            byte = self.raw[self.index]
            self.index += 1
            if byte < 0x20:
                _refuse("E_JSON_ENCODING", "manifest")
            if escaped:
                escaped = False
                continue
            if byte == 0x5C:
                escaped = True
            elif byte == 0x22:
                break
        else:
            _refuse("E_JSON_ENCODING", "manifest")
        token = self.raw[start:self.index]
        try:
            value = json.loads(token.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            _refuse("E_JSON_ENCODING", "manifest")
        try:
            canonical = json.dumps(
                value, ensure_ascii=False, separators=(",", ":")
            ).encode("utf-8")
            normalized = unicodedata.normalize("NFC", value) if isinstance(value, str) else None
        except (TypeError, UnicodeError):
            _refuse("E_CANONICAL_JSON", "manifest")
        if not isinstance(value, str) or "\x00" in value or normalized != value:
            _refuse("E_CANONICAL_JSON", "manifest")
        if canonical != token:
            _refuse("E_CANONICAL_JSON", "manifest")
        return value

    def uint(self, maximum: int) -> int:
        start = self.index
        negative = self.raw[self.index:self.index + 1] == b"-"
        if negative:
            self.index += 1
        elif self.raw[self.index:self.index + 1] in (b"N", b"I"):
            _refuse("E_CANONICAL_JSON", "manifest")
        while self.index < len(self.raw) and 0x30 <= self.raw[self.index] <= 0x39:
            self.index += 1
        token = self.raw[start:self.index]
        digits = token[1:] if negative else token
        if not digits:
            _refuse("E_MANIFEST_SHAPE", "manifest")
        if len(digits) > 1 and digits.startswith(b"0"):
            _refuse("E_CANONICAL_JSON", "manifest")
        if self.raw[self.index:self.index + 1] in (b".", b"e", b"E"):
            _refuse("E_CANONICAL_JSON", "manifest")
        if len(digits) > len(str(maximum)):
            _refuse("E_INTEGER_RANGE", "manifest", observed=maximum + 1, limit=maximum)
        value = int(token)
        if value < 0:
            _refuse("E_INTEGER_RANGE", "manifest", observed=value, limit=0)
        if negative:
            _refuse("E_CANONICAL_JSON", "manifest")
        if value > maximum:
            _refuse("E_INTEGER_RANGE", "manifest", observed=value, limit=maximum)
        return value

    def string_list(self) -> list[str]:
        result: list[str] = []
        self.expect(b"[")
        if self.raw[self.index:self.index + 1] != b"]":
            while True:
                result.append(self.string())
                if self.raw[self.index:self.index + 1] != b",":
                    break
                self.index += 1
        self.expect(b"]")
        return result

    def bounds(self) -> None:
        self.expect(b"{")
        for ordinal, name in enumerate(sorted(dict(core.BOUNDS))):
            if ordinal:
                self.expect(b",")
            if self.string() != name:
                _refuse("E_MANIFEST_SHAPE", "manifest")
            self.expect(b":")
            if self.uint(18_446_744_073_709_551_615) != dict(core.BOUNDS)[name]:
                _refuse("E_MANIFEST_SHAPE", "manifest")
        self.expect(b"}")

    def shard(self, table_ord: int) -> None:
        table = core.ALL_TABLES[table_ord]
        self.expect(b'{"bytes":')
        byte_count = self.uint(18_446_744_073_709_551_615)
        self.expect(b',"firstId":')
        first_id = self.string()
        self.expect(b',"lastId":')
        last_id = self.string()
        self.expect(b',"path":')
        path = self.string()
        self.expect(b',"rowCount":')
        row_count = self.uint(4_294_967_295)
        self.expect(b',"sha256":')
        digest = self.string()
        self.expect(b',"shardIndex":')
        shard_index = self.uint(4_294_967_295)
        self.expect(b"}")
        prefix = core.table_spec(table).prefix
        if (
            not _valid_sha256(digest) or not _valid_row_id(first_id, prefix)
            or not _valid_row_id(last_id, prefix)
            or path != f"tables/{table}/{shard_index:06d}.jsonl"
        ):
            _refuse("E_MANIFEST_SHAPE", "manifest")
        count = self.metadata.execute(
            "SELECT count(*) FROM manifest_shards WHERE table_ord=?", (table_ord,)
        ).fetchone()[0]
        if count + 1 > _bound("maxShardsPerTable"):
            _refuse("E_SHARD_COUNT_LIMIT", table, observed=count + 1, limit=_bound("maxShardsPerTable"))
        try:
            self.metadata.execute(
                "INSERT INTO manifest_shards VALUES(?,?,?,?,?,?,?,?)",
                (table_ord, shard_index, path, str(row_count), str(byte_count), first_id, last_id, digest),
            )
        except sqlite3.IntegrityError:
            _refuse("E_SHARD_LAYOUT", table)

    def parse(self) -> None:
        self.expect(b'{"adminTables":')
        if self.string_list() != list(core.ADMIN_TABLES):
            _refuse("E_MANIFEST_SHAPE", "manifest")
        self.expect(b',"baselineId":')
        if self.string() != core.BASELINE_ID:
            _refuse("E_MANIFEST_SHAPE", "manifest")
        self.expect(b',"bounds":')
        self.bounds()
        self.expect(b',"nodeTables":')
        if self.string_list() != list(core.NODE_TABLES):
            _refuse("E_MANIFEST_SHAPE", "manifest")
        self.expect(b',"schema":')
        if self.string() != core.SCHEMA_NAME:
            _refuse("E_MANIFEST_SHAPE", "manifest")
        self.expect(b',"schemaVersion":')
        if self.uint(4_294_967_295) != core.SCHEMA_VERSION:
            _refuse("E_MANIFEST_SHAPE", "manifest")
        self.expect(b',"shardSetSha256":')
        shard_set = self.string()
        if not _valid_sha256(shard_set):
            _refuse("E_MANIFEST_SHAPE", "manifest")
        self.expect(b',"tableOrder":')
        if self.string_list() != list(core.ALL_TABLES):
            _refuse("E_MANIFEST_SHAPE", "manifest")
        self.expect(b',"tables":{')
        for position, table in enumerate(sorted(core.ALL_TABLES)):
            if position:
                self.expect(b",")
            if self.string() != table:
                _refuse("E_MANIFEST_SHAPE", "manifest")
            self.expect(b':{"rowCount":')
            row_count = self.uint(18_446_744_073_709_551_615)
            self.expect(b',"shards":[')
            table_ord = core.ALL_TABLES.index(table)
            if self.raw[self.index:self.index + 1] != b"]":
                while True:
                    self.shard(table_ord)
                    if self.raw[self.index:self.index + 1] != b",":
                        break
                    self.index += 1
            self.expect(b'],"tableSha256":')
            table_sha = self.string()
            self.expect(b"}")
            if not _valid_sha256(table_sha):
                _refuse("E_MANIFEST_SHAPE", "manifest")
            self.metadata.execute(
                "INSERT INTO manifest_tables VALUES(?,?,?)",
                (table_ord, str(row_count), table_sha),
            )
        self.expect(b'},"totalBytes":')
        total_bytes = self.uint(18_446_744_073_709_551_615)
        self.expect(b',"totalRows":')
        total_rows = self.uint(18_446_744_073_709_551_615)
        self.expect(b"}")
        if self.index != len(self.raw):
            _refuse("E_MANIFEST_SHAPE", "manifest")
        self.metadata.executemany(
            "INSERT INTO manifest_meta VALUES(?,?)",
            (("shardSetSha256", shard_set), ("totalBytes", str(total_bytes)), ("totalRows", str(total_rows))),
        )


def _valid_sha256(value: str) -> bool:
    return len(value) == 64 and all(character in "0123456789abcdef" for character in value)


def _valid_row_id(value: str, prefix: str) -> bool:
    return value.startswith(prefix) and len(value) == len(prefix) + 64 and _valid_sha256(value[len(prefix):])


def _safe_physical_census(output: Path, metadata: sqlite3.Connection) -> int:
    pending = [(output, "")]
    total_bytes = 0
    manifest_count = 0
    while pending:
        directory, relative_directory = pending.pop()
        try:
            if _has_reparse_point(directory) or not directory.is_dir():
                _refuse("E_PATH_CONTAINMENT")
            with os.scandir(directory) as entries:
                for entry in entries:
                    try:
                        entry_stat = entry.stat(follow_symlinks=False)
                    except OSError:
                        _refuse("E_PATH_CONTAINMENT")
                    attributes = getattr(entry_stat, "st_file_attributes", 0)
                    if entry.is_symlink() or attributes & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0):
                        _refuse("E_PATH_CONTAINMENT")
                    relative = f"{relative_directory}/{entry.name}" if relative_directory else entry.name
                    if stat.S_ISDIR(entry_stat.st_mode):
                        parts = relative.split("/")
                        if not (parts == ["tables"] or len(parts) == 2 and parts[0] == "tables" and parts[1] in core.ALL_TABLES):
                            _refuse("E_SHARD_LAYOUT", "manifest")
                        pending.append((Path(entry.path), relative))
                    elif stat.S_ISREG(entry_stat.st_mode):
                        total_bytes += entry_stat.st_size
                        if relative == "manifest.json":
                            manifest_count += 1
                            metadata.execute("INSERT INTO files VALUES(?,?)", (relative, entry_stat.st_size))
                            continue
                        parts = relative.split("/")
                        if len(parts) != 3 or parts[0] != "tables" or parts[1] not in core.ALL_TABLES or len(parts[2]) != 12 or not parts[2].endswith(".jsonl") or not parts[2][:6].isdigit():
                            _refuse("E_SHARD_LAYOUT", "manifest")
                        table_ord = core.ALL_TABLES.index(parts[1])
                        shard_index = int(parts[2][:6])
                        count = metadata.execute("SELECT count(*) FROM physical_shards WHERE table_ord=?", (table_ord,)).fetchone()[0]
                        if count + 1 > _bound("maxShardsPerTable"):
                            _refuse("E_SHARD_COUNT_LIMIT", parts[1], observed=count + 1, limit=_bound("maxShardsPerTable"))
                        metadata.execute(
                            "INSERT INTO physical_shards(table_ord,shard_index,path,size) VALUES(?,?,?,?)",
                            (table_ord, shard_index, relative, entry_stat.st_size),
                        )
                    else:
                        _refuse("E_PATH_CONTAINMENT")
            pending.sort(
                key=lambda item: (
                    -1 if item[1] == "tables"
                    else core.ALL_TABLES.index(item[1].split("/", 1)[1])
                ),
                reverse=True,
            )
        except OSError:
            _refuse("E_PATH_CONTAINMENT")
    if manifest_count != 1:
        _refuse("E_SHARD_LAYOUT", "manifest")
    if total_bytes > _bound("maxOutputBytes"):
        _refuse(
            "E_OUTPUT_BYTES_LIMIT", "manifest", observed=total_bytes,
            limit=_bound("maxOutputBytes"),
        )
    for table_ord, table in enumerate(core.ALL_TABLES):
        expected_index = 0
        for (shard_index,) in metadata.execute(
            "SELECT shard_index FROM physical_shards WHERE table_ord=? ORDER BY shard_index",
            (table_ord,),
        ):
            if shard_index != expected_index:
                _refuse("E_SHARD_LAYOUT", table)
            expected_index += 1
    return total_bytes


def _validator_metadata(path: Path) -> sqlite3.Connection:
    connection = sqlite3.connect(path)
    try:
        connection.executescript(
            "CREATE TABLE files(path TEXT PRIMARY KEY,size INTEGER);"
            "CREATE TABLE physical_shards(table_ord INTEGER,shard_index INTEGER,path TEXT,size INTEGER,row_count INTEGER,byte_count INTEGER,first_id TEXT,last_id TEXT,sha256 TEXT,PRIMARY KEY(table_ord,shard_index));"
            "CREATE TABLE physical_tables(table_ord INTEGER PRIMARY KEY,row_count INTEGER,table_sha256 TEXT);"
            "CREATE TABLE manifest_shards(table_ord INTEGER,shard_index INTEGER,path TEXT,row_count TEXT,byte_count TEXT,first_id TEXT,last_id TEXT,sha256 TEXT,PRIMARY KEY(table_ord,shard_index));"
            "CREATE TABLE manifest_tables(table_ord INTEGER PRIMARY KEY,row_count TEXT,table_sha256 TEXT);"
            "CREATE TABLE manifest_meta(key TEXT PRIMARY KEY,value TEXT);"
        )
    except BaseException:
        connection.close()
        raise
    return connection


def validate_canonical_shards(output_directory: str | os.PathLike[str], *, core_artifact_path: str | os.PathLike[str]) -> ManifestSummary:
    _load_core_artifact(Path(core_artifact_path))
    output = _assert_workspace_path(Path(output_directory))
    if not output.is_dir():
        _refuse("E_SHARD_LAYOUT", "manifest")
    handle = tempfile.NamedTemporaryFile(dir=output.parent, prefix=".semantic-store-validate.", suffix=".sqlite3", delete=False)
    metadata_path = Path(handle.name)
    handle.close()
    metadata: sqlite3.Connection | None = None
    try:
        try:
            metadata = _validator_metadata(metadata_path)
        except sqlite3.Error:
            _refuse("E_SQLITE_CONTRACT")
        _safe_physical_census(output, metadata)
        manifest_size = metadata.execute("SELECT size FROM files WHERE path='manifest.json'").fetchone()[0]
        if manifest_size > _bound("maxJsonlLineBytes"):
            _refuse("E_LINE_BYTES_LIMIT", "manifest", observed=manifest_size, limit=_bound("maxJsonlLineBytes"))
        manifest_path = output / "manifest.json"
        try:
            if _has_reparse_point(manifest_path) or manifest_path.stat().st_size != manifest_size:
                _refuse("E_PATH_CONTAINMENT")
            with manifest_path.open("rb") as stream:
                manifest_raw = stream.read(manifest_size + 1)
        except OSError:
            _refuse("E_PATH_CONTAINMENT")
        if len(manifest_raw) != manifest_size or manifest_raw.startswith(b"\xef\xbb\xbf") or b"\x00" in manifest_raw or b"\r" in manifest_raw or not manifest_raw.endswith(b"\n") or manifest_raw.endswith(b"\n\n"):
            _refuse("E_JSON_ENCODING", "manifest")
        total_rows = 0
        node_rows = 0
        shard_bytes_total = 0
        for table_ord, table in enumerate(core.ALL_TABLES):
            table_hash = hashlib.sha256()
            table_rows = 0
            previous_id = None
            previous_shard_rows: int | None = None
            previous_shard_bytes: int | None = None
            expected_index = 0
            for shard_index, relative, censused_size in metadata.execute(
                "SELECT shard_index,path,size FROM physical_shards WHERE table_ord=? ORDER BY shard_index",
                (table_ord,),
            ):
                if shard_index != expected_index:
                    _refuse("E_SHARD_LAYOUT", table)
                expected_index += 1
                path = output / Path(relative)
                try:
                    if _has_reparse_point(path) or path.stat().st_size != censused_size:
                        _refuse("E_PATH_CONTAINMENT")
                except OSError:
                    _refuse("E_PATH_CONTAINMENT")
                row_count = 0
                byte_count = 0
                first_id = None
                last_id = None
                shard_hash = hashlib.sha256()
                try:
                    stream = path.open("rb")
                    with stream:
                        while True:
                            line = stream.readline(_bound("maxJsonlLineBytes") + 1)
                            if not line:
                                break
                            if len(line) > _bound("maxJsonlLineBytes"):
                                _refuse("E_LINE_BYTES_LIMIT", table, observed=len(line), limit=_bound("maxJsonlLineBytes"))
                            row = _parse_canonical_object(line, table)
                            _validate_row(table, row)
                            identity = row["id"]
                            if previous_id is not None and identity <= previous_id:
                                _refuse("E_DUPLICATE_ID" if identity == previous_id else "E_ROW_ORDER", table, row=identity)
                            previous_id = identity
                            first_id = identity if first_id is None else first_id
                            last_id = identity
                            next_shard_rows = row_count + 1
                            if next_shard_rows > _bound("maxRowsPerShard"):
                                _refuse(
                                    "E_SHARD_ROWS_LIMIT", table,
                                    observed=next_shard_rows,
                                    limit=_bound("maxRowsPerShard"),
                                )
                            next_shard_bytes = byte_count + len(line)
                            if next_shard_bytes > _bound("maxBytesPerShard"):
                                _refuse(
                                    "E_SHARD_BYTES_LIMIT", table,
                                    observed=next_shard_bytes,
                                    limit=_bound("maxBytesPerShard"),
                                )
                            next_table = table_rows + 1
                            violation = _first_table_row_limit_violation(table, next_table)
                            if violation:
                                code, limit = violation
                                _refuse(code, table, observed=next_table, limit=limit)
                            next_node = node_rows + (1 if table in core.NODE_TABLES else 0)
                            if next_node > _bound("maxNodeRowsTotal"):
                                _refuse("E_NODE_ROWS_TOTAL_LIMIT", table, observed=next_node, limit=_bound("maxNodeRowsTotal"))
                            if total_rows + 1 > _bound("maxRowsTotal"):
                                _refuse("E_TOTAL_ROWS_LIMIT", table, observed=total_rows + 1, limit=_bound("maxRowsTotal"))
                            if (
                                row_count == 0
                                and previous_shard_rows is not None
                                and previous_shard_bytes is not None
                                and previous_shard_rows + 1 <= _bound("maxRowsPerShard")
                                and previous_shard_bytes + len(line) <= _bound("maxBytesPerShard")
                            ):
                                _refuse("E_SHARD_LAYOUT", table, row=identity)
                            row_count = next_shard_rows
                            table_rows = next_table
                            node_rows = next_node
                            total_rows += 1
                            byte_count = next_shard_bytes
                            shard_hash.update(line)
                            table_hash.update(line)
                except OSError:
                    _refuse("E_PATH_CONTAINMENT")
                if row_count == 0 or byte_count != censused_size:
                    _refuse("E_SHARD_LAYOUT", table)
                shard_bytes_total += byte_count
                metadata.execute(
                    "UPDATE physical_shards SET row_count=?,byte_count=?,first_id=?,last_id=?,sha256=? WHERE table_ord=? AND shard_index=?",
                    (row_count, byte_count, first_id, last_id, shard_hash.hexdigest(), table_ord, shard_index),
                )
                previous_shard_rows = row_count
                previous_shard_bytes = byte_count
            metadata.execute("INSERT INTO physical_tables VALUES(?,?,?)", (table_ord, table_rows, table_hash.hexdigest()))

        _ManifestCursor(manifest_raw[:-1], metadata).parse()

        missing_or_extra = metadata.execute(
            "SELECT 1 FROM physical_shards p LEFT JOIN manifest_shards m ON m.table_ord=p.table_ord AND m.shard_index=p.shard_index WHERE m.table_ord IS NULL "
            "UNION ALL SELECT 1 FROM manifest_shards m LEFT JOIN physical_shards p ON p.table_ord=m.table_ord AND p.shard_index=m.shard_index WHERE p.table_ord IS NULL LIMIT 1"
        ).fetchone()
        if missing_or_extra is not None:
            _refuse("E_SHARD_LAYOUT", "manifest")
        mismatch = metadata.execute(
            "SELECT p.table_ord,p.shard_index FROM physical_shards p JOIN manifest_shards m ON m.table_ord=p.table_ord AND m.shard_index=p.shard_index "
            "WHERE p.path!=m.path OR CAST(p.row_count AS TEXT)!=m.row_count OR CAST(p.byte_count AS TEXT)!=m.byte_count OR p.first_id!=m.first_id OR p.last_id!=m.last_id ORDER BY p.table_ord,p.shard_index LIMIT 1"
        ).fetchone()
        if mismatch is not None:
            _refuse("E_COUNT_MISMATCH", core.ALL_TABLES[mismatch[0]])
        table_mismatch = metadata.execute(
            "SELECT p.table_ord FROM physical_tables p JOIN manifest_tables m ON m.table_ord=p.table_ord WHERE CAST(p.row_count AS TEXT)!=m.row_count ORDER BY p.table_ord LIMIT 1"
        ).fetchone()
        if table_mismatch is not None or metadata.execute("SELECT count(*) FROM manifest_tables").fetchone()[0] != len(core.ALL_TABLES):
            _refuse("E_COUNT_MISMATCH", "manifest")
        manifest_meta = dict(metadata.execute("SELECT key,value FROM manifest_meta"))
        if int(manifest_meta["totalRows"]) != total_rows or int(manifest_meta["totalBytes"]) != shard_bytes_total:
            _refuse("E_COUNT_MISMATCH", "manifest")
        digest_mismatch = metadata.execute(
            "SELECT p.table_ord FROM physical_shards p JOIN manifest_shards m ON m.table_ord=p.table_ord AND m.shard_index=p.shard_index WHERE p.sha256!=m.sha256 ORDER BY p.table_ord,p.shard_index LIMIT 1"
        ).fetchone()
        if digest_mismatch is None:
            digest_mismatch = metadata.execute(
                "SELECT p.table_ord FROM physical_tables p JOIN manifest_tables m ON m.table_ord=p.table_ord WHERE p.table_sha256!=m.table_sha256 ORDER BY p.table_ord LIMIT 1"
            ).fetchone()
        if digest_mismatch is not None:
            _refuse("E_DIGEST_MISMATCH", core.ALL_TABLES[digest_mismatch[0]])
        shard_set = hashlib.sha256()
        for table_ord, table in enumerate(core.ALL_TABLES):
            for relative, digest, byte_count, row_count in metadata.execute(
                "SELECT path,sha256,byte_count,row_count FROM physical_shards WHERE table_ord=? ORDER BY shard_index",
                (table_ord,),
            ):
                shard_set.update(f"{table}|{relative}|{digest}|{byte_count}|{row_count}\n".encode("utf-8"))
        actual_set = shard_set.hexdigest()
        if manifest_meta["shardSetSha256"] != actual_set:
            _refuse("E_DIGEST_MISMATCH", "manifest")
        counts = tuple(
            (table, metadata.execute("SELECT row_count FROM physical_tables WHERE table_ord=?", (ordinal,)).fetchone()[0])
            for ordinal, table in enumerate(core.ALL_TABLES)
        )
        return ManifestSummary(hashlib.sha256(manifest_raw).hexdigest(), actual_set, counts, total_rows, shard_bytes_total)
    finally:
        cleanup_failed = False
        if metadata is not None:
            try:
                metadata.close()
            except sqlite3.Error:
                cleanup_failed = True
        for suffix in ("", "-journal", "-wal", "-shm"):
            candidate = Path(str(metadata_path) + suffix)
            try:
                if os.path.lexists(candidate):
                    candidate.unlink()
            except OSError:
                cleanup_failed = True
        if cleanup_failed:
            _refuse("E_PATH_CONTAINMENT")


__all__ = (
    "normalize_casefold", "canonical_row_bytes", "StoreDiagnostic",
    "StoreRefusal", "StoreSummary", "ManifestSummary", "ForeignKeyRef",
    "SemanticGraphStore", "publish_canonical_shards",
    "validate_canonical_shards",
)
