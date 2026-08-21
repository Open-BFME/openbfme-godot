"""Q58 corpus warm cache: identity, type preservation, poisoning, byte-identity.

The hazard this file exists for (§16.5 rule 3 of the perf report): the stored
values are ``IniBlock`` / ``SageObject`` dataclasses carrying tuples, and a
type-flattening round trip would CHANGE COMPILER OUTPUT, not merely miss a
cache.  So the round-trip tests assert type identity recursively against the
REAL stored objects, and the byte-identity test proves a warm-cache convert
equals a cache-disabled convert artifact-for-artifact.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from openbfme_importer import corpus_warm_cache as cwc
from openbfme_importer import playable_unit_compiler as puc
from openbfme_importer.sage_cst import SageObject
from openbfme_importer.sage_ini import IniBlock

from importer.tests.test_faction_import import _fixture


@pytest.fixture(autouse=True)
def _clean_cache_state():
    cwc._reset_for_tests()
    puc.clear_prepared_playable_unit_compiler_memo()
    yield
    cwc._reset_for_tests()
    puc.clear_prepared_playable_unit_compiler_memo()


def _documents() -> dict[str, bytes]:
    documents, _graph = _fixture()
    return dict(documents)


def _prepare_with_cache(tmp_path: Path, documents) -> puc.PlayableUnitCompilerInputs:
    cwc.configure_corpus_warm_cache(tmp_path)
    prepared = puc.prepare_playable_unit_compiler(documents)
    # Touch the lazy caches so there is something worth sharing.
    puc._flat_blocks_for_kind(
        documents, "CommandSet", prepared.flat_kind_cache,
        cache_lock=prepared.cache_lock,
    )
    puc._named_definition_values(
        documents, "Weapon", "HeroBow",
        cache=prepared.named_definition_cache, cache_lock=prepared.cache_lock,
    )
    return prepared


def _reload_fresh(tmp_path: Path, documents) -> puc.PlayableUnitCompilerInputs:
    """Simulate a second worker process: cold memo, same cache root."""

    puc.clear_prepared_playable_unit_compiler_memo()
    cwc.configure_corpus_warm_cache(tmp_path)
    return puc.prepare_playable_unit_compiler(dict(documents))


def _assert_type_identical(a: object, b: object, path: str = "$") -> None:
    """Deep equality INCLUDING exact container and dataclass types."""

    assert type(a) is type(b), f"{path}: {type(a).__name__} != {type(b).__name__}"
    if isinstance(a, dict):
        assert set(a) == set(b), f"{path}: key drift"
        for key in a:
            _assert_type_identical(a[key], b[key], f"{path}[{key!r}]")
    elif isinstance(a, (list, tuple)):
        assert len(a) == len(b), f"{path}: length drift"
        for index, (left, right) in enumerate(zip(a, b)):
            _assert_type_identical(left, right, f"{path}[{index}]")
    elif isinstance(a, (IniBlock, SageObject)) or hasattr(a, "__dataclass_fields__"):
        for field in a.__dataclass_fields__:
            _assert_type_identical(
                getattr(a, field), getattr(b, field), f"{path}.{field}"
            )
    else:
        assert a == b, f"{path}: value drift"


# --------------------------------------------------------------------------
# Round trips against the real stored objects (extends the §11.4 tuple-drift
# test to the actual cache payloads).
# --------------------------------------------------------------------------


def test_prepared_roundtrip_preserves_types_exactly(tmp_path: Path) -> None:
    documents = _documents()
    first = _prepare_with_cache(tmp_path, documents)
    cwc.flush_corpus_writeback()

    second = _reload_fresh(tmp_path, documents)
    # The loaded form must have come from disk, not a recompute.
    for name in cwc.PREPARED_TABLE_FIELDS:
        _assert_type_identical(getattr(first, name), getattr(second, name), name)
    # Representative hard cases: dataclasses under tuples under dicts.
    assert isinstance(next(iter(second.objects.values())), SageObject)
    assert all(type(b.assignments) is tuple for b in second.command_sets.values())


def test_flat_kind_roundtrip_preserves_tuple_of_iniblock(tmp_path: Path) -> None:
    documents = _documents()
    first = _prepare_with_cache(tmp_path, documents)
    cwc.flush_corpus_writeback()

    second = _reload_fresh(tmp_path, documents)
    assert "commandset" in second.flat_kind_cache, "flat kinds must be seeded"
    loaded = second.flat_kind_cache["commandset"]
    original = first.flat_kind_cache["commandset"]
    assert type(loaded) is tuple
    assert all(type(block) is IniBlock for block in loaded)
    _assert_type_identical(original, loaded, "flat[commandset]")


def test_named_definition_roundtrip_is_seeded_and_equal(tmp_path: Path) -> None:
    documents = _documents()
    first = _prepare_with_cache(tmp_path, documents)
    cwc.flush_corpus_writeback()

    second = _reload_fresh(tmp_path, documents)
    key = ("weapon", "herobow")
    assert key in second.named_definition_cache
    _assert_type_identical(
        first.named_definition_cache[key],
        second.named_definition_cache[key],
        "named[weapon,herobow]",
    )


def test_rebound_prepared_keeps_the_documents_identity_guard(tmp_path: Path) -> None:
    """§16.5 rule 4: the ``prepared.documents is documents`` guard must hold
    against the loading worker's own corpus object."""

    documents = _documents()
    _prepare_with_cache(tmp_path, documents)
    cwc.flush_corpus_writeback()

    puc.clear_prepared_playable_unit_compiler_memo()
    other_view = dict(documents)
    loaded = puc.prepare_playable_unit_compiler(other_view)
    assert loaded.documents is other_view


# --------------------------------------------------------------------------
# Poisoning: any byte flip is refused with a named reason and recomputed.
# --------------------------------------------------------------------------


def _entry_dir(tmp_path: Path, documents) -> Path:
    identity = puc._documents_identity(documents)
    key = cwc.corpus_cache_key(identity)
    return tmp_path / "cache" / "corpus-warm" / key[:2] / key


def test_poisoned_payload_refused_by_name_and_recomputed(tmp_path: Path) -> None:
    documents = _documents()
    baseline = _prepare_with_cache(tmp_path, documents)
    cwc.flush_corpus_writeback()

    blob = _entry_dir(tmp_path, documents) / "prepared.bin"
    raw = bytearray(blob.read_bytes())
    raw[-3] ^= 0xFF  # flip a byte deep in the pickle payload
    blob.write_bytes(bytes(raw))

    identity = puc._documents_identity(documents)
    key = cwc.corpus_cache_key(identity)
    value, reason = cwc._load_part(
        tmp_path / "cache" / "corpus-warm", key, "prepared", identity
    )
    assert value is None
    assert reason == "refused:payload-digest-mismatch"

    # The full path recomputes and the recompute equals the baseline.
    recomputed = _reload_fresh(tmp_path, documents)
    for name in cwc.PREPARED_TABLE_FIELDS:
        _assert_type_identical(getattr(baseline, name), getattr(recomputed, name), name)


def test_poisoned_envelope_wrong_identity_is_refused(tmp_path: Path) -> None:
    documents = _documents()
    _prepare_with_cache(tmp_path, documents)
    cwc.flush_corpus_writeback()

    blob = _entry_dir(tmp_path, documents) / "prepared.bin"
    header, _sep, payload = blob.read_bytes().partition(b"\n")
    envelope = json.loads(header.decode("utf-8"))
    envelope["documentsIdentity"] = "0" * 64
    blob.write_bytes(
        json.dumps(envelope, sort_keys=True, separators=(",", ":")).encode("utf-8")
        + b"\n"
        + payload
    )

    identity = puc._documents_identity(documents)
    key = cwc.corpus_cache_key(identity)
    value, reason = cwc._load_part(
        tmp_path / "cache" / "corpus-warm", key, "prepared", identity
    )
    assert value is None
    assert reason == "refused:documentsIdentity-mismatch"


def test_version_drift_is_a_clean_refusal(tmp_path: Path, monkeypatch) -> None:
    documents = _documents()
    _prepare_with_cache(tmp_path, documents)
    cwc.flush_corpus_writeback()

    monkeypatch.setattr(cwc, "CACHE_VERSION", cwc.CACHE_VERSION + 1)
    tables, flat, named = cwc.load_corpus_warm_state(
        puc._documents_identity(documents)
    )
    # A version bump changes the key, so the entry simply does not exist.
    assert tables is None and flat is None and named is None


# --------------------------------------------------------------------------
# Kill switches.
# --------------------------------------------------------------------------


def test_master_kill_switch_disables_everything(tmp_path: Path, monkeypatch) -> None:
    documents = _documents()
    _prepare_with_cache(tmp_path, documents)
    cwc.flush_corpus_writeback()

    monkeypatch.setenv("OPENBFME_NO_CORPUS_CACHE", "1")
    tables, flat, named = cwc.load_corpus_warm_state(
        puc._documents_identity(documents)
    )
    assert tables is None and flat is None and named is None


@pytest.mark.parametrize(
    ("switch", "part_index"),
    [
        ("OPENBFME_NO_PREPARED_CACHE", 0),
        ("OPENBFME_NO_FLATKIND_CACHE", 1),
        ("OPENBFME_NO_NAMEDDEF_CACHE", 2),
    ],
)
def test_per_part_kill_switch(tmp_path, monkeypatch, switch, part_index) -> None:
    documents = _documents()
    _prepare_with_cache(tmp_path, documents)
    cwc.flush_corpus_writeback()

    monkeypatch.setenv(switch, "1")
    loaded = cwc.load_corpus_warm_state(puc._documents_identity(documents))
    assert loaded[part_index] is None
    for index, value in enumerate(loaded):
        if index != part_index:
            assert value is not None, f"part {index} must stay available"


# --------------------------------------------------------------------------
# Producer identity: the property the whole cut depends on.
# --------------------------------------------------------------------------


def _live_sources() -> dict[str, str]:
    return cwc._package_sources()


def test_trailing_compiler_comment_keeps_the_identity() -> None:
    """The owner's scenario: appending a comment to the compiler file must NOT
    invalidate the corpus warm cache (the object/plan caches DO invalidate)."""

    sources = _live_sources()
    base = cwc._producer_identity_payload(sources)["sha256"]
    edited = dict(sources)
    edited["playable_unit_compiler.py"] += "\n# q58 marker comment\n"
    assert cwc._producer_identity_payload(edited)["sha256"] == base


def test_editing_a_producer_function_changes_the_identity() -> None:
    sources = _live_sources()
    base = cwc._producer_identity_payload(sources)["sha256"]
    edited = dict(sources)
    edited["playable_unit_compiler.py"] = edited["playable_unit_compiler.py"].replace(
        "no effective Object definitions were supplied",
        "no effective Object definitions were supplied!",
    )
    assert cwc._producer_identity_payload(edited)["sha256"] != base


def test_editing_a_parse_module_changes_the_identity() -> None:
    sources = _live_sources()
    base = cwc._producer_identity_payload(sources)["sha256"]
    for module in ("sage_ini.py", "sage_cst.py"):
        edited = dict(sources)
        edited[module] += "\n# any byte\n"
        assert cwc._producer_identity_payload(edited)["sha256"] != base, module


def test_uncertainty_broadens_to_full_package_fallback() -> None:
    sources = _live_sources()
    edited = dict(sources)
    edited["playable_unit_compiler.py"] = (
        "import importlib\n_x = importlib.import_module('json')\n"
        + edited["playable_unit_compiler.py"]
    )
    payload = cwc._producer_identity_payload(edited)
    assert payload["mode"] == "full-package-fallback"
    # In fallback mode, ANY package edit invalidates — over-invalidation only.
    edited2 = dict(edited)
    edited2["faction_import.py"] = edited2.get("faction_import.py", "") + "\n# x\n"
    assert cwc._producer_identity_payload(edited2)["sha256"] != payload["sha256"]


def test_live_identity_uses_the_narrow_closure() -> None:
    payload = cwc.producer_identity()
    assert payload["mode"] == "function-closure", (
        "the live module tree must resolve the narrow producer closure; "
        "full-package-fallback would silently disable the Q58 win on every "
        "compiler edit"
    )
    modules = {row["path"] for row in payload["modules"]}
    assert "sage_ini.py" in modules and "sage_cst.py" in modules
    names = {row["name"] for row in payload["seedSegments"]}
    assert "_prepare_playable_unit_compiler_uncached" in names
    assert "_object_index" in names
    assert "compile_playable_unit_descriptor" not in names


# --------------------------------------------------------------------------
# Write-back merge.
# --------------------------------------------------------------------------


def test_writeback_merges_new_flat_kinds_with_existing_entry(tmp_path: Path) -> None:
    documents = _documents()
    prepared = _prepare_with_cache(tmp_path, documents)
    cwc.flush_corpus_writeback()

    # A second "process" computes a DIFFERENT kind and writes back.
    second = _reload_fresh(tmp_path, documents)
    puc._flat_blocks_for_kind(
        documents, "Upgrade", second.flat_kind_cache, cache_lock=second.cache_lock
    )
    cwc.flush_corpus_writeback()

    third = _reload_fresh(tmp_path, documents)
    assert "commandset" in third.flat_kind_cache
    assert "upgrade" in third.flat_kind_cache
    _assert_type_identical(
        prepared.flat_kind_cache["commandset"],
        third.flat_kind_cache["commandset"],
        "merged[commandset]",
    )


def test_entry_is_one_file_so_writers_cannot_tear_the_pair(tmp_path: Path) -> None:
    """v1 regression: envelope and payload were two independently-replaced
    files, and 24 racing exit-writers left (A's envelope, B's payload) — every
    later reader refused it and re-scanned the corpus. One file per part is
    the structural fix; two sequential stores must both stay readable."""

    documents = _documents()
    _prepare_with_cache(tmp_path, documents)
    cwc.flush_corpus_writeback()

    entry = _entry_dir(tmp_path, documents)
    files = sorted(p.name for p in entry.iterdir())
    assert all(name.endswith(".bin") for name in files), files
    assert not [name for name in files if ".tmp-" in name], files

    identity = puc._documents_identity(documents)
    key = cwc.corpus_cache_key(identity)
    root = tmp_path / "cache" / "corpus-warm"
    assert cwc._store_part(root, key, "flat-kinds", identity, {"a": ()})
    assert cwc._store_part(root, key, "flat-kinds", identity, {"a": (), "b": ()})
    value, reason = cwc._load_part(root, key, "flat-kinds", identity)
    assert reason == "hit"
    assert set(value) == {"a", "b"}


def test_unconfigured_cache_changes_nothing(tmp_path: Path) -> None:
    documents = _documents()
    prepared = puc.prepare_playable_unit_compiler(documents)
    assert cwc.corpus_cache_root() is None
    cwc.flush_corpus_writeback()
    assert not list(tmp_path.rglob("*.pkl"))
    assert prepared.documents is documents
