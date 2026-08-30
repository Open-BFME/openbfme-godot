"""Deterministic private manifest for every effective retail archive record."""

from __future__ import annotations

from collections import defaultdict
import hashlib
import json
from pathlib import Path
from typing import Any, Iterable, Mapping

from .catalog import CatalogEntry, InstallCatalog
from .paths import safe_relative_parts


SCHEMA = "openbfme.rotwk-202-effective-tree"
SCHEMA_VERSION = 1
CHUNK_BYTES = 1024 * 1024
ADDITIVE_SUFFIXES = (".ini", ".inc", ".str", ".csf")


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(CHUNK_BYTES), b""):
            digest.update(chunk)
    return digest.hexdigest()


def source_identity(root: Path, paths: Iterable[str]) -> dict[str, Any]:
    rows: list[dict[str, str]] = []
    for relative in sorted(set(paths)):
        parts = safe_relative_parts(relative)
        canonical = "/".join(parts)
        path = root.joinpath(*parts)
        if not path.is_file():
            raise ValueError(f"winner-rule source is missing: {canonical}")
        rows.append({"path": canonical, "sha256": _sha256_file(path)})
    encoded = "".join(f"{row['path']}|{row['sha256']}\n" for row in rows)
    return {
        "sha256": hashlib.sha256(encoded.encode("utf-8")).hexdigest(),
        "sources": rows,
    }


def _validate_catalog_paths(entries: tuple[CatalogEntry, ...]) -> None:
    keys: set[str] = set()
    for entry in entries:
        archive = "/".join(safe_relative_parts(entry.archive))
        name = "/".join(safe_relative_parts(entry.name))
        if archive != entry.archive.replace("\\", "/") or name != entry.name.replace("\\", "/"):
            raise ValueError("catalog path is not canonical")
        if entry.offset < 0 or entry.size < 0 or entry.precedence < 0:
            raise ValueError("catalog byte range or precedence is invalid")
        keys.add(entry.key)
    for key in sorted(keys):
        parts = key.split("/")
        for count in range(1, len(parts)):
            if "/".join(parts[:count]) in keys:
                raise ValueError("catalog contains a file-directory prefix collision")


def _payload_digests(catalog: InstallCatalog) -> list[str]:
    grouped: dict[str, list[tuple[int, CatalogEntry]]] = defaultdict(list)
    for index, entry in enumerate(catalog.entries):
        grouped[entry.archive.casefold()].append((index, entry))
    digests = [""] * len(catalog.entries)
    archive_spelling = {entry.archive.casefold(): entry.archive for entry in catalog.entries}
    for archive_key in sorted(grouped):
        relative = archive_spelling[archive_key]
        archive_path = catalog.install_root.joinpath(*safe_relative_parts(relative))
        archive_size = archive_path.stat().st_size
        with archive_path.open("rb") as stream:
            for index, entry in sorted(grouped[archive_key], key=lambda row: (row[1].offset, row[0])):
                if entry.offset + entry.size > archive_size:
                    raise ValueError("catalog byte range escaped its archive")
                stream.seek(entry.offset)
                remaining = entry.size
                digest = hashlib.sha256()
                while remaining:
                    chunk = stream.read(min(CHUNK_BYTES, remaining))
                    if not chunk:
                        raise ValueError("archive ended inside a catalog byte range")
                    digest.update(chunk)
                    remaining -= len(chunk)
                digests[index] = digest.hexdigest()
    if any(not value for value in digests):
        raise ValueError("one or more catalog records were not hashed")
    return digests


def build_effective_tree(
    catalog: InstallCatalog,
    *,
    baseline_id: str,
    policy_sha256: str,
    catalog_sha256: str,
    parser_sources: Mapping[str, Any],
    expected_records: int,
    expected_winners: int,
    expected_shadows: int,
) -> dict[str, Any]:
    if catalog.identity_sha256() != catalog_sha256:
        raise ValueError("catalog identity differs from the baseline")
    if catalog.source_policy_sha256 != policy_sha256:
        raise ValueError("archive policy identity differs from the baseline")
    _validate_catalog_paths(catalog.entries)
    payloads = _payload_digests(catalog)
    grouped: dict[str, list[tuple[int, CatalogEntry]]] = defaultdict(list)
    for raw_index, entry in enumerate(catalog.entries):
        grouped[entry.key].append((raw_index, entry))

    records: list[dict[str, Any]] = []
    chains: list[dict[str, Any]] = []
    shadow_count = 0
    for key in sorted(grouped):
        ordered = sorted(
            grouped[key],
            key=lambda row: (
                row[1].precedence,
                row[1].archive.casefold(),
                row[1].archive,
                row[1].name,
                row[1].offset,
                row[1].size,
                row[0],
            ),
        )
        additive = key.endswith(ADDITIVE_SUFFIXES)
        record_indexes: list[int] = []
        for chain_index, (raw_index, entry) in enumerate(ordered):
            disposition = "winner" if chain_index == 0 else "shadow"
            shadow_count += disposition == "shadow"
            record_index = len(records)
            record_indexes.append(record_index)
            records.append(
                {
                    "recordIndex": record_index,
                    "rawCatalogIndex": raw_index,
                    "key": key,
                    "archive": entry.archive,
                    "member": entry.name,
                    "offset": entry.offset,
                    "size": entry.size,
                    "precedence": entry.precedence,
                    "payloadSha256": payloads[raw_index],
                    "disposition": disposition,
                    "chainIndex": chain_index,
                    "shadowSemantics": "additive-retained" if additive else "replacement-retained",
                }
            )
        chains.append(
            {
                "key": key,
                "winnerRecordIndex": record_indexes[0],
                "recordIndexes": record_indexes,
                "shadowSemantics": "additive-retained" if additive else "replacement-retained",
            }
        )

    manifest = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "baselineId": baseline_id,
        "policySha256": policy_sha256,
        "catalogSha256": catalog_sha256,
        "parserWinnerRules": dict(parser_sources),
        "counts": {
            "archives": len(catalog.archives),
            "records": len(records),
            "winners": len(chains),
            "shadows": shadow_count,
        },
        "records": records,
        "overrideChains": chains,
    }
    validate_effective_tree(
        manifest,
        expected_records=expected_records,
        expected_winners=expected_winners,
        expected_shadows=expected_shadows,
    )
    return manifest


def validate_effective_tree(
    manifest: Mapping[str, Any], *, expected_records: int, expected_winners: int, expected_shadows: int
) -> None:
    if manifest.get("schema") != SCHEMA or manifest.get("schemaVersion") != SCHEMA_VERSION:
        raise ValueError("effective-tree schema is invalid")
    records = manifest.get("records")
    chains = manifest.get("overrideChains")
    counts = manifest.get("counts")
    if not isinstance(records, list) or not isinstance(chains, list) or not isinstance(counts, dict):
        raise ValueError("effective-tree collections are invalid")
    if (len(records), len(chains), len(records) - len(chains)) != (
        expected_records,
        expected_winners,
        expected_shadows,
    ):
        raise ValueError("effective-tree winner/shadow totals differ from the contract")
    if counts.get("records") != len(records) or counts.get("winners") != len(chains) or counts.get("shadows") != expected_shadows:
        raise ValueError("effective-tree count receipt is inconsistent")
    seen: list[int] = []
    for chain in chains:
        indexes = chain.get("recordIndexes")
        if not isinstance(indexes, list) or not indexes or chain.get("winnerRecordIndex") != indexes[0]:
            raise ValueError("effective-tree chain is invalid")
        for chain_index, record_index in enumerate(indexes):
            if not isinstance(record_index, int) or not 0 <= record_index < len(records):
                raise ValueError("effective-tree chain record index is invalid")
            record = records[record_index]
            if record.get("key") != chain.get("key") or record.get("chainIndex") != chain_index:
                raise ValueError("effective-tree chain and record differ")
            expected = "winner" if chain_index == 0 else "shadow"
            if record.get("disposition") != expected:
                raise ValueError("effective-tree disposition is invalid")
            seen.append(record_index)
    if sorted(seen) != list(range(len(records))):
        raise ValueError("effective-tree chains do not cover every record exactly once")


def render_effective_tree(manifest: Mapping[str, Any]) -> bytes:
    return (json.dumps(manifest, indent=2, sort_keys=True, ensure_ascii=False) + "\n").encode("utf-8")


def require_exact_bytes(path: Path, expected: bytes) -> None:
    if not path.is_file() or path.read_bytes() != expected:
        raise ValueError("effective-tree artifact is missing or differs from deterministic output")
