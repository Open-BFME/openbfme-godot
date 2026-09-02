"""Convert one retail queue source through an existing parser and verify its pack output."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import tempfile
from pathlib import Path, PurePosixPath
from typing import Mapping, Sequence

from .catalog import CatalogEntry, InstallCatalog
from .fleet_queues import (
    ASSET_EXTENSIONS,
    SCREEN_EXTENSIONS,
    PackProvenance,
    _canonical,
    selected_provenance,
)
from .native_backtest import validate_cooked_sage_map
from .cook.maps import convert_cooked_map, validate_map_document
from .sage_apt import parse_apt_constants, parse_apt_movie, parse_wnd_layout
from .sage_map import convert_sage_map
from .util import write_json_atomic
from .w3d_metadata import scan_w3d_metadata


class ItemVerificationError(RuntimeError):
    """One named queue item cannot pass its structural oracle."""


def _read_source(catalog: InstallCatalog, entry: CatalogEntry) -> bytes:
    try:
        return catalog.open_archive_for(entry).read_entry(
            catalog.as_entry(entry), max_bytes=max(entry.size, 1)
        )
    except (OSError, ValueError) as exc:
        raise ItemVerificationError(f"source-read-failed: {entry.name}: {exc}") from exc


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def _matches_source(record: PackProvenance, item_id: str, source_sha256: str) -> bool:
    return bool(
        (record.source_path is not None and record.source_path == item_id)
        or (record.source_sha256 is not None and record.source_sha256 == source_sha256)
    )


def _verified_record(
    content_root: Path, item_id: str, source_sha256: str, kind: str
) -> PackProvenance:
    matched = [
        record
        for record in selected_provenance(content_root)
        if _matches_source(record, item_id, source_sha256)
    ]
    if not matched:
        raise ItemVerificationError(
            f"not-converted-in-selected-pack: {item_id} has no matching source path or digest"
        )
    failures: list[str] = []
    for record in matched:
        if not record.outputs:
            failures.append(f"{record.pack}: no converted outputs")
            continue
        output_paths: list[Path] = []
        bad = False
        for output in record.outputs:
            path = record.root / Path(*PurePosixPath(output.path).parts)
            if not path.is_file():
                failures.append(f"{record.pack}: missing output {output.path}")
                bad = True
                break
            if output.size is not None and path.stat().st_size != output.size:
                failures.append(f"{record.pack}: output-size-mismatch {output.path}")
                bad = True
                break
            if output.sha256 is not None and _sha256(path) != output.sha256:
                failures.append(f"{record.pack}: output-sha256-mismatch {output.path}")
                bad = True
                break
            output_paths.append(path)
        if bad:
            continue
        if kind == "maps" and not any(path.name.casefold() == "map.json" for path in output_paths):
            failures.append(f"{record.pack}: cooked-map-document-missing")
            continue
        if kind == "screens" and not any(path.suffix.casefold() == ".json" for path in output_paths):
            failures.append(f"{record.pack}: converted-screen-document-missing")
            continue
        if record.source_sha256 is not None and record.source_sha256 != source_sha256:
            failures.append(f"{record.pack}: source-sha256-mismatch")
            continue
        return record
    raise ItemVerificationError(
        f"converted-output-corrupt: {item_id}: " + "; ".join(failures)
    )


def _json_outputs(record: PackProvenance) -> list[tuple[Path, object]]:
    values: list[tuple[Path, object]] = []
    for output in record.outputs:
        if PurePosixPath(output.path).suffix.casefold() != ".json":
            continue
        path = record.root / Path(*PurePosixPath(output.path).parts)
        try:
            values.append((path, json.loads(path.read_text(encoding="utf-8"))))
        except (OSError, UnicodeError, json.JSONDecodeError) as exc:
            raise ItemVerificationError(
                f"converted-json-load-failed: {output.path}: {exc}"
            ) from exc
    return values


def _w3d_counts(source: bytes, item_id: str) -> dict[str, int]:
    metadata = scan_w3d_metadata(source, item_id)
    if any(warning.code in {"truncated-chunk", "truncated-header"} for warning in metadata.warnings):
        raise ItemVerificationError(f"w3d-source-structurally-truncated: {item_id}")
    return {
        "meshCount": len(metadata.mesh_headers),
        "boneCount": max(
            (header.pivot_count for header in metadata.hierarchy_headers), default=0
        ),
        "animationCount": len(metadata.animation_headers),
    }


def _verify_w3d(source: bytes, item_id: str, record: PackProvenance) -> dict[str, int]:
    expected = _w3d_counts(source, item_id)
    receipts = []
    for path, value in _json_outputs(record):
        metrics = value.get("metrics") if isinstance(value, Mapping) else None
        if isinstance(metrics, Mapping) and all(key in metrics for key in expected):
            receipts.append((path, metrics))
    if not receipts:
        raise ItemVerificationError(
            f"w3d-structural-receipt-missing: {item_id} has no mesh/bone/clip count receipt"
        )
    for path, metrics in receipts:
        actual = {key: metrics.get(key) for key in expected}
        if all(
            isinstance(actual[key], int)
            and not isinstance(actual[key], bool)
            and int(actual[key]) >= expected[key]
            for key in expected
        ):
            return {
                **{f"source{key[0].upper()}{key[1:]}": value for key, value in expected.items()},
                **{f"output{key[0].upper()}{key[1:]}": int(actual[key]) for key in expected},
            }
    rendered = ", ".join(str(path) for path, _metrics in receipts)
    raise ItemVerificationError(
        f"w3d-structural-count-loss: {item_id}: source={expected}; receipts={rendered}"
    )


def _verify_map(source: bytes, item_id: str, record: PackProvenance) -> dict[str, object]:
    with tempfile.TemporaryDirectory(prefix="openbfme-verify-map-") as raw:
        root = Path(raw)
        source_path = root / "source.map"
        output = root / "cooked"
        source_path.write_bytes(source)
        try:
            convert_sage_map(source_path, output)
        except (OSError, ValueError) as exc:
            raise ItemVerificationError(f"map-cook-failed: {item_id}: {exc}") from exc
        evidence = validate_cooked_sage_map(output)
        if evidence.get("valid") is not True:
            raise ItemVerificationError(
                f"map-strict-load-failed: {item_id}: {evidence.get('errors', [])}"
            )
        engine_document_path = root / "map-v1.json"
        engine_document = convert_cooked_map(
            output,
            engine_document_path,
            source_path=item_id,
        )
        validate_map_document(engine_document)
        if not engine_document_path.is_file():
            raise ItemVerificationError(f"map-v1-write-failed: {item_id}")
    cooked = [value for path, value in _json_outputs(record) if path.name.casefold() == "map.json"]
    if not cooked or not any(
        isinstance(value, Mapping)
        and value.get("schema") == "openbfme.map"
        and value.get("source", {}).get("sha256") == hashlib.sha256(source).hexdigest()
        for value in cooked
    ):
        raise ItemVerificationError(
            f"cooked-map-source-mismatch: {item_id} map.json is absent or names another source"
        )
    return {
        "strictLoad": True,
        "engineMapV1": True,
        "checkedFiles": len(evidence.get("inventory", [])),
    }


def _verify_screen(
    catalog: InstallCatalog,
    source: bytes,
    item_id: str,
    record: PackProvenance,
) -> dict[str, object]:
    extension = PurePosixPath(item_id).suffix.casefold()
    try:
        if extension == ".wnd":
            converted = parse_wnd_layout(source, item_id)
            fact = {"windowCount": converted["windowCount"]}
        else:
            companion_id = str(PurePosixPath(item_id).with_suffix(".const"))
            companion = catalog.resolve_exact(companion_id)
            if companion is None:
                raise ItemVerificationError(
                    f"apt-constant-table-missing: {item_id}: {companion_id}"
                )
            constants = parse_apt_constants(
                _read_source(catalog, companion), companion_id
            )
            converted = parse_apt_movie(
                source,
                constants,
                item_id,
                max_bytes=4 * 1024 * 1024,
                max_exports=16 * 1024,
            )
            fact = {"frameCount": converted["root"]["frameCount"]}
    except ItemVerificationError:
        raise
    except (OSError, ValueError) as exc:
        raise ItemVerificationError(f"screen-convert-failed: {item_id}: {exc}") from exc
    with tempfile.TemporaryDirectory(prefix="openbfme-verify-screen-") as raw:
        receipt = Path(raw) / "screen.json"
        write_json_atomic(receipt, converted)
        try:
            loaded = json.loads(receipt.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as exc:
            raise ItemVerificationError(f"screen-strict-load-failed: {item_id}: {exc}") from exc
        if loaded != converted:
            raise ItemVerificationError(f"screen-strict-load-mismatch: {item_id}")
    if not any(isinstance(value, Mapping) for _path, value in _json_outputs(record)):
        raise ItemVerificationError(f"converted-screen-document-invalid: {item_id}")
    return fact


def _verified_native_screen(
    content_root: Path, item_id: str
) -> dict[str, object] | None:
    """Read the same native document/load receipt pair as queue completion."""

    selection_path = content_root / "native" / "selection.json"
    if not selection_path.is_file():
        return None
    try:
        selection = json.loads(selection_path.read_text(encoding="utf-8"))
        index_relative = selection.get("screens")
        if not isinstance(index_relative, str):
            return None
        root = content_root.resolve()
        index_path = (root / Path(*PurePosixPath(index_relative).parts)).resolve()
        index_path.relative_to(root)
        index = json.loads(index_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError, AttributeError):
        return None
    for row in index.get("screens", []):
        if not isinstance(row, Mapping) or _canonical(str(row.get("id", ""))) != item_id:
            continue
        if row.get("status") != "ok":
            failure = row.get("failure", {})
            raise ItemVerificationError(
                f"screen-convert-failed: {item_id}: {failure.get('class', 'unknown')}: "
                f"{failure.get('message', '')}"
            )
        try:
            document_path = (root / Path(*PurePosixPath(str(row["document"])).parts)).resolve()
            receipt_path = (root / Path(*PurePosixPath(str(row["receipt"])).parts)).resolve()
            document_path.relative_to(root)
            receipt_path.relative_to(root)
            payload = document_path.read_bytes()
            document = json.loads(payload)
            receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError, KeyError, ValueError) as exc:
            raise ItemVerificationError(
                f"screen-load-receipt-missing: {item_id}: {exc}"
            ) from exc
        digest = hashlib.sha256(payload).hexdigest()
        if (
            document.get("schema") != "openbfme.screen.v1"
            or digest != row.get("documentSha256")
            or receipt.get("schema") != "openbfme.screen-load-receipt"
            or receipt.get("passed") is not True
            or receipt.get("documentSha256") != digest
            or int(receipt.get("opcodesUnimplemented", -1)) != 0
        ):
            raise ItemVerificationError(
                f"screen-load-refused: {item_id}: "
                f"{receipt.get('failureClass', 'invalid-or-stale-receipt')}"
            )
        return {
            "pack": f"native/{selection.get('active', '')}",
            "converter": "screen-v1",
            "document": str(row["document"]),
            "documentSha256": digest,
            "loadReceipt": str(row["receipt"]),
            "opcodesUnimplemented": 0,
        }
    return None


def verify_item(
    *, kind: str, item_id: str, install: Path | str, content_root: Path | str
) -> dict[str, object]:
    canonical_id = _canonical(item_id)
    catalog = InstallCatalog.build(install)
    entry = catalog.resolve_exact(canonical_id)
    if entry is None:
        raise ItemVerificationError(f"source-not-found: {canonical_id}")
    extension = PurePosixPath(canonical_id).suffix.casefold()
    allowed = {
        "assets": ASSET_EXTENSIONS,
        "maps": frozenset({".map"}),
        "screens": SCREEN_EXTENSIONS,
    }[kind]
    if extension not in allowed:
        raise ItemVerificationError(
            f"kind-extension-mismatch: {kind} does not own {canonical_id}"
        )
    source = _read_source(catalog, entry)
    source_sha256 = hashlib.sha256(source).hexdigest()
    if kind == "screens":
        native = _verified_native_screen(Path(content_root), canonical_id)
        if native is not None:
            return {
                "status": "verified",
                "kind": kind,
                "id": canonical_id,
                "pack": native.pop("pack"),
                "converter": native.pop("converter"),
                "sourceSha256": source_sha256,
                "structural": native,
            }
    record = _verified_record(Path(content_root), canonical_id, source_sha256, kind)
    if extension == ".w3d":
        structural = _verify_w3d(source, canonical_id, record)
    elif kind == "maps":
        structural = _verify_map(source, canonical_id, record)
    elif kind == "screens":
        structural = _verify_screen(catalog, source, canonical_id, record)
    else:
        structural = {"sourceBytes": len(source), "outputCount": len(record.outputs)}
    return {
        "status": "verified",
        "kind": kind,
        "id": canonical_id,
        "pack": record.pack,
        "converter": record.converter,
        "sourceSha256": source_sha256,
        "structural": structural,
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kind", required=True, choices=("assets", "maps", "screens"))
    parser.add_argument("--id", required=True)
    parser.add_argument("--install", required=True, type=Path)
    parser.add_argument("--content-root", required=True, type=Path)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        result = verify_item(
            kind=args.kind,
            item_id=args.id,
            install=args.install,
            content_root=args.content_root,
        )
    except (ItemVerificationError, OSError, ValueError) as exc:
        print(f"VERIFY_ITEM_FAIL {exc}", file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
