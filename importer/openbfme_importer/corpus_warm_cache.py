"""Durable, identity-keyed cache for the per-process corpus warm-up (Q58).

Twenty-four pooled convert workers each independently re-derive the same pure
functions of the 29 MB effective INI corpus: the prepared compiler inputs
(``_prepare_playable_unit_compiler_uncached``), the per-kind flat block index
(``_flat_blocks_for_kind``), and the named-definition / weapon-nugget scans.
Every one of those values is a pure function of (corpus bytes, producer code),
so they are shareable across processes and across runs — and, decisively for
the owner's scenario, they do NOT depend on the descriptor-compile code paths,
so an edit to the unit or structure *compiler* logic must not invalidate them.

Identity chain (fail-closed, §16.5 of the perf report):

* the cache key binds the corpus content identity
  (``_documents_identity(documents)`` — path + payload digests), the *producer*
  code identity described below, the Python major.minor and the pickle
  protocol;
* the producer identity is a function-granularity closure over
  ``playable_unit_compiler.py``: the exact source segments of the seed
  functions and every module-level name they transitively reference, plus the
  FULL bytes of every relative module that closure imports (``sage_cst.py``,
  ``sage_ini.py``, ...).  A trailing comment on the compiler file changes no
  segment and keeps the cache warm; an edit to any producing function, any
  referenced constant, or any parse module changes the key.  ANY uncertainty
  in the walk (dynamic import, unparsable source, missing seed) broadens to
  the full package-source digest — over-invalidation costs time, never
  correctness;
* every stored payload is pickle bytes sealed by its own sha256 in a JSON
  envelope that repeats the key and the identities; any mismatch — schema,
  version, key, identity, digest, or an unpicklable payload — is a refusal
  with a named reason and falls through to a full recompute.

Pickle (not JSON) is deliberate: the stored values are ``IniBlock`` /
``SageObject`` dataclasses carrying tuples, and a JSON round trip would
silently turn tuples into lists and CHANGE COMPILER OUTPUT, not merely miss
(§16.5 rule 3).  Pickle preserves types exactly;
``test_corpus_warm_cache.py`` proves it against the real stored objects.  The
payload is only ever unpickled after its digest verified against the sealed
envelope, and the cache root lives inside the operator's own state root.

Kill switches: ``OPENBFME_NO_CORPUS_CACHE`` (everything) plus per-part
``OPENBFME_NO_PREPARED_CACHE`` / ``OPENBFME_NO_FLATKIND_CACHE`` /
``OPENBFME_NO_NAMEDDEF_CACHE``.
"""

from __future__ import annotations

import ast
import atexit
import hashlib
import json
import os
import pickle
import sys
import threading
import time
from pathlib import Path
from typing import Any, Mapping

from .util import write_json_atomic

CACHE_SCHEMA = "openbfme.corpus-warm-cache"
# Bump on ANY change to the key material or the stored envelope/payload shape.
CACHE_VERSION = 1
_PICKLE_PROTOCOL = 5

# The producer seams whose code determines every stored value.
_PRODUCER_SEEDS = (
    "_prepare_playable_unit_compiler_uncached",
    "_flat_blocks_for_kind",
    "_named_definition_values_uncached",
    "_weapon_damage_nuggets_uncached",
    "_documents_identity",
)
_PRODUCER_MODULE = "playable_unit_compiler.py"

# The nine data fields of PlayableUnitCompilerInputs apart from ``documents``
# and the process-local lazy caches/lock.  Stored and restored by name so a
# field drift in either direction is a refusal, never a misbind.
PREPARED_TABLE_FIELDS = (
    "objects",
    "command_sets",
    "command_buttons",
    "player_templates",
    "numeric_defines",
    "numeric_define_provenance",
    "token_defines",
    "token_define_provenance",
    "object_parse_errors",
)

_STATE_LOCK = threading.Lock()
_CACHE_ROOT: Path | None = None
_WRITEBACK: list[dict[str, Any]] = []
_WRITEBACK_REGISTERED = False
_PRODUCER_MEMO: dict[str, Any] | None = None


def _truthy(name: str) -> bool:
    return os.environ.get(name, "").strip().casefold() in {"1", "true", "yes"}


def corpus_cache_part_enabled(part: str) -> bool:
    """Is one logical part (prepared / flat-kinds / named-defs) enabled?"""

    if _truthy("OPENBFME_NO_CORPUS_CACHE"):
        return False
    switch = {
        "prepared": "OPENBFME_NO_PREPARED_CACHE",
        "flat-kinds": "OPENBFME_NO_FLATKIND_CACHE",
        "named-defs": "OPENBFME_NO_NAMEDDEF_CACHE",
    }[part]
    return not _truthy(switch)


def configure_corpus_warm_cache(state_root: Path | str | None) -> None:
    """Bind the durable cache root for this process (no-op on ``None``).

    The cache stays inert until a caller that actually knows the state root —
    the faction convert entrypoints — configures it, so every other consumer of
    ``prepare_playable_unit_compiler`` behaves exactly as before.
    """

    global _CACHE_ROOT
    if state_root is None:
        return
    shared = os.environ.get("OPENBFME_SHARED_CACHE", "").strip()
    if shared:
        root = Path(shared).expanduser().resolve() / "corpus-warm"
    else:
        root = Path(state_root).expanduser().resolve() / "cache" / "corpus-warm"
    with _STATE_LOCK:
        _CACHE_ROOT = root


def corpus_cache_root() -> Path | None:
    with _STATE_LOCK:
        return _CACHE_ROOT


def _reset_for_tests() -> None:
    global _CACHE_ROOT, _PRODUCER_MEMO, _WRITEBACK
    with _STATE_LOCK:
        _CACHE_ROOT = None
        _PRODUCER_MEMO = None
        _WRITEBACK = []


def _note(message: str) -> None:
    print(f"CORPUS_CACHE {message}", file=sys.stderr, flush=True)


# ---------------------------------------------------------------------------
# Producer identity: function-granularity closure over the compiler module.
# ---------------------------------------------------------------------------


def _package_sources() -> dict[str, str]:
    package = Path(__file__).resolve().parent
    sources: dict[str, str] = {}
    for path in sorted(package.glob("*.py"), key=lambda item: item.name.casefold()):
        try:
            sources[path.name] = path.read_bytes().decode("utf-8", errors="replace")
        except OSError:
            sources[path.name] = "<unreadable>"
    return sources


def _relative_import_targets(node: ast.AST) -> tuple[list[str], bool]:
    """Package-relative module files imported by one AST node, + uncertainty."""

    targets: list[str] = []
    uncertain = False
    if isinstance(node, ast.ImportFrom) and node.level:
        if node.module:
            targets.append(node.module.split(".")[0] + ".py")
        else:
            for alias in node.names:
                if alias.name == "*":
                    uncertain = True
                else:
                    targets.append(alias.name.split(".")[0] + ".py")
    elif isinstance(node, ast.Import):
        for alias in node.names:
            if alias.name.startswith("openbfme_importer."):
                targets.append(
                    alias.name.removeprefix("openbfme_importer.").split(".")[0] + ".py"
                )
    elif isinstance(node, ast.Call):
        function = node.func
        if (isinstance(function, ast.Name) and function.id == "__import__") or (
            isinstance(function, ast.Attribute) and function.attr == "import_module"
        ):
            uncertain = True
    return targets, uncertain


def _module_import_closure(
    seeds: set[str], sources: Mapping[str, str]
) -> tuple[set[str], bool]:
    """Expand relative-import closure over package modules, like the §9 walk."""

    expanded = set(seeds)
    pending = [name for name in seeds if name in sources]
    parsed: set[str] = set()
    uncertain = False
    while pending and not uncertain:
        name = pending.pop()
        if name in parsed:
            continue
        parsed.add(name)
        try:
            tree = ast.parse(sources[name], filename=name)
        except (SyntaxError, ValueError):
            return expanded, True
        for node in ast.walk(tree):
            targets, node_uncertain = _relative_import_targets(node)
            if node_uncertain:
                uncertain = True
                break
            for target in targets:
                if target in sources and target not in expanded:
                    expanded.add(target)
                    pending.append(target)
    return expanded, uncertain


def _segment(text: str, node: ast.AST) -> str | None:
    parts: list[str] = []
    for decorator in getattr(node, "decorator_list", []) or []:
        piece = ast.get_source_segment(text, decorator)
        if piece is None:
            return None
        parts.append("@" + piece)
    piece = ast.get_source_segment(text, node)
    if piece is None:
        return None
    parts.append(piece)
    return "\n".join(parts)


def _producer_identity_payload(sources: Mapping[str, str]) -> dict[str, Any]:
    text = sources.get(_PRODUCER_MODULE)
    uncertain = text is None
    seed_segments: list[dict[str, str]] = []
    module_deps: set[str] = set()

    if not uncertain:
        try:
            tree = ast.parse(text, filename=_PRODUCER_MODULE)
        except (SyntaxError, ValueError):
            tree = None
            uncertain = True
        if tree is not None:
            top: dict[str, ast.AST] = {}
            imported: dict[str, str] = {}
            for node in tree.body:
                if isinstance(
                    node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)
                ):
                    top[node.name] = node
                elif isinstance(node, ast.Assign):
                    for target in node.targets:
                        if isinstance(target, ast.Name):
                            top[target.id] = node
                elif isinstance(node, ast.AnnAssign) and isinstance(
                    node.target, ast.Name
                ):
                    top[node.target.id] = node
                targets, node_uncertain = _relative_import_targets(node)
                uncertain = uncertain or node_uncertain
                if isinstance(node, ast.ImportFrom) and node.level:
                    for alias in node.names:
                        if alias.name == "*":
                            uncertain = True
                        else:
                            bound = alias.asname or alias.name
                            imported[bound] = (
                                (node.module.split(".")[0] + ".py")
                                if node.module
                                else alias.name.split(".")[0] + ".py"
                            )
                elif targets:
                    for alias in getattr(node, "names", []):
                        bound = (alias.asname or alias.name).split(".")[0]
                        imported.setdefault(bound, targets[0])
            # Any dynamic import anywhere in the module is doubt.
            for node in ast.walk(tree):
                _targets, node_uncertain = _relative_import_targets(node)
                if node_uncertain:
                    uncertain = True
                    break
            closure: set[str] = set()
            pending = list(_PRODUCER_SEEDS)
            if any(seed not in top for seed in _PRODUCER_SEEDS):
                uncertain = True
                pending = []
            while pending:
                name = pending.pop()
                if name in closure:
                    continue
                closure.add(name)
                for inner in ast.walk(top[name]):
                    if not isinstance(inner, ast.Name):
                        continue
                    ident = inner.id
                    if ident in top and ident not in closure:
                        pending.append(ident)
                    elif ident in imported:
                        module_deps.add(imported[ident])
            for name in sorted(closure):
                piece = _segment(text, top[name])
                if piece is None:
                    uncertain = True
                    break
                seed_segments.append(
                    {
                        "name": name,
                        "sha256": hashlib.sha256(piece.encode("utf-8")).hexdigest(),
                    }
                )

    if not uncertain:
        module_deps.discard(_PRODUCER_MODULE)
        expanded, uncertain = _module_import_closure(module_deps, sources)
        module_deps = {name for name in expanded if name in sources}

    if uncertain:
        # Broaden to the whole package: correctness before reuse.
        modules = sorted(sources)
        seed_segments = []
        mode = "full-package-fallback"
    else:
        modules = sorted(module_deps)
        mode = "function-closure"
    payload = {
        "schema": CACHE_SCHEMA,
        "version": CACHE_VERSION,
        "mode": mode,
        "seedSegments": seed_segments,
        "modules": [
            {
                "path": name,
                "sha256": hashlib.sha256(sources[name].encode("utf-8")).hexdigest(),
            }
            for name in modules
        ],
        "python": f"{sys.version_info.major}.{sys.version_info.minor}",
        "pickleProtocol": _PICKLE_PROTOCOL,
    }
    payload["sha256"] = hashlib.sha256(
        json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
    ).hexdigest()
    return payload


def producer_identity(
    module_sources: Mapping[str, str] | None = None,
) -> dict[str, Any]:
    """Code identity of everything that can change a stored value.

    ``module_sources`` is a test seam; live callers get a process-memoized
    identity over the real package sources.
    """

    global _PRODUCER_MEMO
    if module_sources is not None:
        return _producer_identity_payload(dict(module_sources))
    with _STATE_LOCK:
        if _PRODUCER_MEMO is not None:
            return _PRODUCER_MEMO
    result = _producer_identity_payload(_package_sources())
    with _STATE_LOCK:
        _PRODUCER_MEMO = result
    return result


# ---------------------------------------------------------------------------
# Envelope store / load.
# ---------------------------------------------------------------------------


def corpus_cache_key(documents_identity: str) -> str:
    payload = {
        "schema": CACHE_SCHEMA,
        "version": CACHE_VERSION,
        "documentsIdentity": documents_identity,
        "producerSha256": producer_identity()["sha256"],
        "python": f"{sys.version_info.major}.{sys.version_info.minor}",
        "pickleProtocol": _PICKLE_PROTOCOL,
    }
    return hashlib.sha256(
        json.dumps(payload, sort_keys=True, separators=(",", ":")).encode("utf-8")
    ).hexdigest()


def _entry_dir(root: Path, key: str) -> Path:
    return root / key[:2] / key


def _store_part(
    root: Path, key: str, part: str, documents_identity: str, value: object
) -> bool:
    try:
        payload = pickle.dumps(value, protocol=_PICKLE_PROTOCOL)
        entry = _entry_dir(root, key)
        entry.mkdir(parents=True, exist_ok=True)
        blob = entry / f"{part}.pkl"
        tmp = blob.with_suffix(".pkl.tmp-%d" % os.getpid())
        tmp.write_bytes(payload)
        os.replace(tmp, blob)
        envelope = {
            "schema": CACHE_SCHEMA,
            "version": CACHE_VERSION,
            "key": key,
            "part": part,
            "documentsIdentity": documents_identity,
            "producerSha256": producer_identity()["sha256"],
            "payloadSha256": hashlib.sha256(payload).hexdigest(),
            "payloadBytes": len(payload),
        }
        # The envelope is written LAST: it is the commit marker, so a torn
        # payload write can only ever look like a digest mismatch (refusal).
        write_json_atomic(entry / f"{part}.json", envelope)
        return True
    except (OSError, pickle.PicklingError):
        # A cache we cannot write is a cache we do without.
        return False


def _load_part(
    root: Path, key: str, part: str, documents_identity: str
) -> tuple[object | None, str]:
    """(value, reason). ``reason`` names the refusal; 'hit'/'miss' otherwise."""

    entry = _entry_dir(root, key)
    envelope_path = entry / f"{part}.json"
    blob_path = entry / f"{part}.pkl"
    if not envelope_path.is_file() or not blob_path.is_file():
        return None, "miss"
    try:
        envelope = json.loads(envelope_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None, "refused:unreadable-envelope"
    if not isinstance(envelope, dict):
        return None, "refused:envelope-not-an-object"
    expected = {
        "schema": CACHE_SCHEMA,
        "version": CACHE_VERSION,
        "key": key,
        "part": part,
        "documentsIdentity": documents_identity,
        "producerSha256": producer_identity()["sha256"],
    }
    for field, wanted in expected.items():
        if envelope.get(field) != wanted:
            return None, f"refused:{field}-mismatch"
    try:
        payload = blob_path.read_bytes()
    except OSError:
        return None, "refused:unreadable-payload"
    if hashlib.sha256(payload).hexdigest() != envelope.get("payloadSha256"):
        return None, "refused:payload-digest-mismatch"
    try:
        value = pickle.loads(payload)
    except Exception:  # noqa: BLE001 — any unpickle failure is a refusal
        return None, "refused:unpicklable-payload"
    return value, "hit"


# ---------------------------------------------------------------------------
# The three logical parts, as used by playable_unit_compiler.
# ---------------------------------------------------------------------------


def load_corpus_warm_state(
    documents_identity: str,
) -> tuple[dict[str, Any] | None, dict[str, Any] | None, dict[Any, Any] | None]:
    """(prepared tables | None, flat kinds | None, named defs | None)."""

    root = corpus_cache_root()
    if root is None:
        return None, None, None
    key = corpus_cache_key(documents_identity)

    prepared_tables: dict[str, Any] | None = None
    flat_kinds: dict[str, Any] | None = None
    named_defs: dict[Any, Any] | None = None

    for part in ("prepared", "flat-kinds", "named-defs"):
        if not corpus_cache_part_enabled(part):
            _note(f"part={part} status=disabled key={key[:12]}")
            continue
        started = time.perf_counter()
        value, reason = _load_part(root, key, part, documents_identity)
        elapsed = time.perf_counter() - started
        if value is None:
            _note(f"part={part} status={reason} key={key[:12]} load_s={elapsed:.3f}")
            continue
        if part == "prepared":
            if (
                isinstance(value, dict)
                and set(value) == set(PREPARED_TABLE_FIELDS)
            ):
                prepared_tables = value
                _note(f"part=prepared status=hit key={key[:12]} load_s={elapsed:.3f}")
            else:
                _note(f"part=prepared status=refused:unexpected-table-shape key={key[:12]}")
        elif part == "flat-kinds":
            if isinstance(value, dict) and all(
                isinstance(k, str) and isinstance(v, tuple) for k, v in value.items()
            ):
                flat_kinds = value
                _note(
                    f"part=flat-kinds status=hit kinds={len(value)} "
                    f"key={key[:12]} load_s={elapsed:.3f}"
                )
            else:
                _note(f"part=flat-kinds status=refused:unexpected-shape key={key[:12]}")
        else:
            if isinstance(value, dict) and all(
                isinstance(k, tuple) and len(k) == 2 for k in value
            ):
                named_defs = value
                _note(
                    f"part=named-defs status=hit entries={len(value)} "
                    f"key={key[:12]} load_s={elapsed:.3f}"
                )
            else:
                _note(f"part=named-defs status=refused:unexpected-shape key={key[:12]}")
    return prepared_tables, flat_kinds, named_defs


def register_corpus_writeback(
    documents_identity: str,
    prepared: object,
    *,
    prepared_was_hit: bool,
    loaded_flat_kinds: set[str],
    loaded_named_keys: set[Any],
) -> None:
    """Queue this process's warm state for a merge-write at interpreter exit.

    Exit-time (not per-job) because the lazy caches keep filling for the whole
    process lifetime.  The write merges over whatever is on disk at that
    moment: concurrent workers may race, the loser's union is partially lost,
    and the next run's single miss re-adds it — a cache race costs time only.
    """

    global _WRITEBACK_REGISTERED
    root = corpus_cache_root()
    if root is None:
        return
    with _STATE_LOCK:
        for item in _WRITEBACK:
            if item["identity"] == documents_identity and item["root"] == root:
                # Keep the earliest registration: its loaded-set baseline is
                # the smallest, so nothing computed since can be lost.
                return
        _WRITEBACK.append(
            {
                "identity": documents_identity,
                "root": root,
                "prepared": prepared,
                "prepared_was_hit": prepared_was_hit,
                "loaded_flat_kinds": set(loaded_flat_kinds),
                "loaded_named_keys": set(loaded_named_keys),
            }
        )
        if not _WRITEBACK_REGISTERED:
            atexit.register(flush_corpus_writeback)
            _WRITEBACK_REGISTERED = True


def flush_corpus_writeback() -> None:
    """Write every queued warm state (also callable directly, e.g. by tests)."""

    with _STATE_LOCK:
        queued, _WRITEBACK[:] = list(_WRITEBACK), []
    for item in queued:
        # The root is captured at registration so a later re-configure (tests
        # bind a fresh root per case) cannot misplace an earlier state.
        _flush_one(item["root"], item)


def _clean_cache_dict(cache: Mapping[Any, Any]) -> dict[Any, Any]:
    """Drop pending markers; only settled values are shareable."""

    return {
        key: value
        for key, value in dict(cache).items()
        if type(value).__name__ != "_CachePending"
    }


def _flush_one(root: Path, item: dict[str, Any]) -> None:
    identity = item["identity"]
    key = corpus_cache_key(identity)
    prepared = item["prepared"]
    if corpus_cache_part_enabled("prepared") and not item["prepared_was_hit"]:
        tables = {
            name: getattr(prepared, name) for name in PREPARED_TABLE_FIELDS
        }
        if _store_part(root, key, "prepared", identity, tables):
            _note(f"part=prepared status=stored key={key[:12]}")
    if corpus_cache_part_enabled("flat-kinds"):
        current = _clean_cache_dict(prepared.flat_kind_cache)
        if set(current) - item["loaded_flat_kinds"]:
            merged, _reason = _load_part(root, key, "flat-kinds", identity)
            union = dict(merged) if isinstance(merged, dict) else {}
            union.update(current)
            if _store_part(root, key, "flat-kinds", identity, union):
                _note(
                    f"part=flat-kinds status=stored kinds={len(union)} key={key[:12]}"
                )
    if corpus_cache_part_enabled("named-defs"):
        current = _clean_cache_dict(prepared.named_definition_cache)
        if set(current) - item["loaded_named_keys"]:
            merged, _reason = _load_part(root, key, "named-defs", identity)
            union = dict(merged) if isinstance(merged, dict) else {}
            union.update(current)
            if _store_part(root, key, "named-defs", identity, union):
                _note(
                    f"part=named-defs status=stored entries={len(union)} key={key[:12]}"
                )


__all__ = [
    "CACHE_SCHEMA",
    "CACHE_VERSION",
    "PREPARED_TABLE_FIELDS",
    "configure_corpus_warm_cache",
    "corpus_cache_key",
    "corpus_cache_part_enabled",
    "corpus_cache_root",
    "flush_corpus_writeback",
    "load_corpus_warm_state",
    "producer_identity",
    "register_corpus_writeback",
]
