"""Generate a bounded private import profile from the Men leaf census.

The census remains the source-neutral dependency graph.  This module turns
that graph into exact converter rules and private runtime manifests without
writing a profile or exposing an install path.  Localized values are the one
intentional retail payload in the returned value: they are selected privately
from ``lotr.str`` for the non-redistributable pack.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import PurePosixPath
import re
from typing import Any, Iterable

from .catalog import InstallCatalog
from .faction_census import (
    EVA_PATH,
    SCIENCE_PATH,
    SOUND_EFFECTS_PATH,
    SPECIAL_POWER_PATH,
    STRING_CATALOG_PATH,
    UPGRADE_PATH,
    VOICE_PATH,
    _effective_entries,
    _first_identifier,
    _read_document,
    census_men_faction,
)
from .paths import safe_relative_parts
from .profile import (
    MAX_PATTERNS_PER_RESOURCE,
    MAX_PROFILE_BYTES,
    MAX_RESOURCES,
    MAX_TEXTURE_ATLAS_CROPS,
)
from .sage_audio import (
    parse_sage_audio_definitions,
    resolve_audio_sample_paths,
    resolve_sage_audio_closure,
)
from .sage_ini import (
    _lines as _ini_lines,
)
from .sage_string import MAX_STRING_BYTES, parse_string_catalog


PROFILE_ID = "bfme2-men-106-leaf-closure"
PACK_ID = "bfme2-men-106-private-leaves"
_REPORT_SCHEMA = "openbfme.faction-command-leaf-census"
_REPORT_CLOSURE = "command-ui-localization-audio-gameplay-definition-leaves"
_SHA256 = re.compile(r"^[0-9a-f]{64}$")
_SAFE_OUTPUT_STEM = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")
_FIXED_SEMANTIC_DOCUMENTS = {
    "data/ini/playertemplate.ini",
    "data/ini/commandset.ini",
    "data/ini/commandbutton.ini",
    "data/ini/soundeffects.ini",
    VOICE_PATH,
    STRING_CATALOG_PATH,
    UPGRADE_PATH,
    SCIENCE_PATH,
    SPECIAL_POWER_PATH,
}
_MAPPED_IMAGE_DOCUMENT_PREFIX = "data/ini/mappedimages/"


def _object(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"{context} must be an object")
    return value


def _array(value: Any, context: str) -> list[Any]:
    if not isinstance(value, list):
        raise ValueError(f"{context} must be an array")
    return value


def _text(value: Any, context: str, *, maximum: int = 512) -> str:
    if not isinstance(value, str) or not value or len(value) > maximum or "\0" in value:
        raise ValueError(f"{context} must be a bounded nonempty string")
    return value


def _integer(value: Any, context: str, *, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise ValueError(f"{context} must be an integer >= {minimum}")
    return value


def _digest(value: Any, context: str) -> str:
    result = _text(value, context, maximum=64).casefold()
    if _SHA256.fullmatch(result) is None:
        raise ValueError(f"{context} must be a SHA-256 digest")
    return result


def _virtual_path(value: Any, context: str) -> str:
    result = _text(value, context)
    if any(character in result for character in "*?["):
        raise ValueError(f"{context} must be an exact virtual path")
    try:
        parts = safe_relative_parts(result)
    except ValueError as exc:
        raise ValueError(f"{context} must be a safe relative virtual path") from exc
    return "/".join(parts)


def _unique_strings(value: Any, context: str) -> list[str]:
    items = [_text(item, f"{context} item") for item in _array(value, context)]
    folded: set[str] = set()
    for item in items:
        key = item.casefold()
        if key in folded:
            raise ValueError(f"{context} contains duplicate identifiers")
        folded.add(key)
    return sorted(items, key=lambda item: (item.casefold(), item))


def _catalog_entry(catalog: InstallCatalog, virtual_path: str, context: str) -> Any:
    entry = catalog.resolve_exact(virtual_path)
    if entry is None:
        raise ValueError(f"{context} is missing from the install catalog: {virtual_path}")
    return entry


def _chunks(items: list[Any], maximum: int) -> Iterable[list[Any]]:
    for offset in range(0, len(items), maximum):
        yield items[offset : offset + maximum]


def _source_document_paths(
    catalog: InstallCatalog, report: dict[str, Any]
) -> list[str]:
    rows = _array(report.get("sourceDocuments"), "faction census sourceDocuments")
    if not rows:
        raise ValueError("faction census sourceDocuments must not be empty")
    paths: dict[str, str] = {}
    for index, raw_row in enumerate(rows):
        row = _object(raw_row, f"faction census sourceDocuments[{index}]")
        path = _virtual_path(
            row.get("virtualPath"),
            f"faction census sourceDocuments[{index}].virtualPath",
        )
        archive = _virtual_path(
            row.get("archive"), f"faction census sourceDocuments[{index}].archive"
        )
        size = _integer(
            row.get("size"), f"faction census sourceDocuments[{index}].size"
        )
        _digest(row.get("sha256"), f"faction census sourceDocuments[{index}].sha256")
        key = path.casefold()
        if key in paths:
            raise ValueError("faction census sourceDocuments contains duplicate paths")
        entry = _catalog_entry(catalog, path, "faction census source document")
        if entry.archive.casefold() != archive.casefold() or entry.size != size:
            raise ValueError(
                f"faction census source document no longer matches the catalog: {path}"
            )
        paths[key] = entry.name
    missing = sorted(
        path for path in _FIXED_SEMANTIC_DOCUMENTS if path.casefold() not in paths
    )
    if missing:
        raise ValueError(
            "faction census is missing required semantic document(s): "
            + ", ".join(missing)
        )
    selected_keys = {path.casefold() for path in _FIXED_SEMANTIC_DOCUMENTS}
    selected_keys.update(
        key for key in paths if key.startswith(_MAPPED_IMAGE_DOCUMENT_PREFIX)
    )
    definitions = _object(report.get("definitions"), "faction census definitions")
    object_rows = _array(
        definitions.get("objects"), "faction census definitions.objects"
    )
    for index, raw_object in enumerate(object_rows):
        object_row = _object(raw_object, f"faction census definitions.objects[{index}]")
        source_rows = [
            _object(
                object_row.get("source"),
                f"faction census definitions.objects[{index}].source",
            ),
            *[
                _object(
                    item,
                    f"faction census definitions.objects[{index}].inheritanceSources",
                )
                for item in _array(
                    object_row.get("inheritanceSources"),
                    f"faction census definitions.objects[{index}].inheritanceSources",
                )
            ],
        ]
        for source_index, source_row in enumerate(source_rows):
            path = _virtual_path(
                source_row.get("virtualPath"),
                f"faction census object source {index}:{source_index}",
            )
            key = path.casefold()
            if key not in paths:
                raise ValueError(
                    f"faction census object source is absent from sourceDocuments: {path}"
                )
            selected_keys.add(key)
    return sorted(
        (paths[key] for key in selected_keys),
        key=lambda item: (item.casefold(), item),
    )


def _source_leaf_roles(
    catalog: InstallCatalog, report: dict[str, Any]
) -> dict[str, set[str]]:
    rows = _array(report.get("sourceLeaves"), "faction census sourceLeaves")
    leaves: dict[str, set[str]] = {}
    for index, raw_row in enumerate(rows):
        row = _object(raw_row, f"faction census sourceLeaves[{index}]")
        path = _virtual_path(
            row.get("virtualPath"), f"faction census sourceLeaves[{index}].virtualPath"
        )
        archive = _virtual_path(
            row.get("archive"), f"faction census sourceLeaves[{index}].archive"
        )
        size = _integer(row.get("size"), f"faction census sourceLeaves[{index}].size")
        roles = set(
            _unique_strings(row.get("roles"), f"faction census sourceLeaves[{index}].roles")
        )
        if not roles:
            raise ValueError("faction census source leaf must declare at least one role")
        key = path.casefold()
        if key in leaves:
            raise ValueError("faction census sourceLeaves contains duplicate paths")
        entry = _catalog_entry(catalog, path, "faction census source leaf")
        if entry.archive.casefold() != archive.casefold() or entry.size != size:
            raise ValueError(f"faction census source leaf no longer matches the catalog: {path}")
        leaves[key] = roles
    return leaves


def _require_leaf_role(
    leaves: dict[str, set[str]], virtual_path: str, role: str, context: str
) -> None:
    if role not in leaves.get(virtual_path.casefold(), set()):
        raise ValueError(f"{context} is not declared as a {role} source leaf")


def _semantic_resources(paths: list[str]) -> list[dict[str, Any]]:
    resources: list[dict[str, Any]] = []
    for batch_index, batch in enumerate(_chunks(paths, MAX_PATTERNS_PER_RESOURCE)):
        resources.append(
            {
                "id": f"men-semantic-definitions-{batch_index:03d}",
                "kind": "data",
                "converter": "hash-only",
                "patterns": batch,
                "required": True,
                "limit": len(batch),
                "expected_count": len(batch),
            }
        )
    return resources


def _ui_resources_and_manifest(
    catalog: InstallCatalog,
    report: dict[str, Any],
    dependencies: dict[str, Any],
    leaves: dict[str, set[str]],
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    requested = _unique_strings(
        dependencies.get("mappedImages"), "faction census dependencies.mappedImages"
    )
    raw_rows = _array(
        _object(report.get("resolvedLeaves"), "faction census resolvedLeaves").get(
            "mappedImages"
        ),
        "faction census resolvedLeaves.mappedImages",
    )
    rows_by_id: dict[str, dict[str, Any]] = {}
    groups: dict[str, tuple[str, list[dict[str, Any]]]] = {}
    for index, raw_row in enumerate(raw_rows):
        row = _object(raw_row, f"mapped image row {index}")
        identifier = _text(row.get("id"), f"mapped image row {index}.id", maximum=128)
        key = identifier.casefold()
        if key in rows_by_id:
            raise ValueError("faction census mapped images contains duplicate identifiers")
        texture = _virtual_path(row.get("texture"), f"mapped image {identifier}.texture")
        atlas_width = _integer(
            row.get("textureWidth"), f"mapped image {identifier}.textureWidth", minimum=1
        )
        atlas_height = _integer(
            row.get("textureHeight"), f"mapped image {identifier}.textureHeight", minimum=1
        )
        coords = _object(row.get("coords"), f"mapped image {identifier}.coords")
        left = _integer(coords.get("left"), f"mapped image {identifier}.coords.left")
        top = _integer(coords.get("top"), f"mapped image {identifier}.coords.top")
        right = _integer(
            coords.get("right"), f"mapped image {identifier}.coords.right", minimum=1
        )
        bottom = _integer(
            coords.get("bottom"), f"mapped image {identifier}.coords.bottom", minimum=1
        )
        if right <= left or bottom <= top or right > atlas_width or bottom > atlas_height:
            raise ValueError(f"mapped image {identifier!r} has an invalid atlas crop")
        atlas_path = _virtual_path(
            row.get("compiledTextureVirtualPath"),
            f"mapped image {identifier}.compiledTextureVirtualPath",
        )
        entry = _catalog_entry(catalog, atlas_path, "mapped image compiled atlas")
        atlas_path = entry.name
        _require_leaf_role(leaves, atlas_path, "mapped-image-texture", "mapped image atlas")
        normalized = {
            "id": identifier,
            "texture": texture,
            "atlasWidth": atlas_width,
            "atlasHeight": atlas_height,
            "left": left,
            "top": top,
            "width": right - left,
            "height": bottom - top,
            "atlasPath": atlas_path,
        }
        rows_by_id[key] = normalized
        atlas_key = atlas_path.casefold()
        if atlas_key in groups and groups[atlas_key][0] != atlas_path:
            raise ValueError("mapped image atlas paths are case-ambiguous")
        groups.setdefault(atlas_key, (atlas_path, []))[1].append(normalized)

    if set(rows_by_id) != {item.casefold() for item in requested}:
        raise ValueError("resolved mapped images do not match the requested identifier set")

    resources: list[dict[str, Any]] = []
    images: list[dict[str, Any]] = []
    output_paths: set[str] = set()
    resource_ids: set[str] = set()
    for atlas_key in sorted(groups):
        atlas_path, atlas_rows = groups[atlas_key]
        atlas_rows.sort(key=lambda item: (str(item["id"]).casefold(), str(item["id"])))
        atlas_digest = hashlib.sha256(
            b"openbfme.men-ui-atlas\0" + atlas_path.casefold().encode("utf-8")
        ).hexdigest()
        output_directory = f"assets/ui/men/{atlas_digest}"
        for part_index, part in enumerate(_chunks(atlas_rows, MAX_TEXTURE_ATLAS_CROPS)):
            resource_id = f"men-ui-{atlas_digest[:32]}-{part_index:03d}"
            if resource_id in resource_ids:
                raise ValueError("generated UI resource id collision")
            resource_ids.add(resource_id)
            crops: list[dict[str, Any]] = []
            for item in part:
                identifier = str(item["id"])
                image_digest = hashlib.sha256(
                    b"openbfme.men-ui-image\0" + identifier.encode("utf-8")
                ).hexdigest()
                filename = f"image-{image_digest}.png"
                pack_path = f"{output_directory}/{filename}"
                output_key = pack_path.casefold()
                if output_key in output_paths:
                    raise ValueError("generated UI output path collision")
                output_paths.add(output_key)
                crops.append(
                    {
                        "logicalName": f"mi-{image_digest}",
                        "output": filename,
                        "crop": [
                            int(item["left"]),
                            int(item["top"]),
                            int(item["width"]),
                            int(item["height"]),
                        ],
                    }
                )
                images.append(
                    {
                        "id": identifier,
                        "path": pack_path,
                        "width": int(item["width"]),
                        "height": int(item["height"]),
                        "crop": {
                            "left": int(item["left"]),
                            "top": int(item["top"]),
                            "width": int(item["width"]),
                            "height": int(item["height"]),
                        },
                        "sourceAtlas": {
                            "texture": str(item["texture"]),
                            "compiledVirtualPath": atlas_path,
                            "width": int(item["atlasWidth"]),
                            "height": int(item["atlasHeight"]),
                        },
                    }
                )
            resources.append(
                {
                    "id": resource_id,
                    "kind": "ui",
                    "converter": "texture-atlas-crops",
                    "patterns": [atlas_path],
                    "output": output_directory,
                    "required": True,
                    "limit": 1,
                    "expected_count": 1,
                    "options": {"crops": crops},
                }
            )
    images.sort(key=lambda item: (str(item["id"]).casefold(), str(item["id"])))
    return resources, {
        "schema": "openbfme.ui-manifest",
        "schemaVersion": 0,
        "complete": False,
        "images": images,
    }


def _localized_strings(
    catalog: InstallCatalog,
    report: dict[str, Any],
    dependencies: dict[str, Any],
) -> dict[str, Any]:
    requested = _unique_strings(
        dependencies.get("textIds"), "faction census dependencies.textIds"
    )
    resolved = _object(report.get("resolvedLeaves"), "faction census resolvedLeaves")
    localization = _object(
        resolved.get("localization"), "faction census resolvedLeaves.localization"
    )
    report_rows = _array(
        localization.get("records"), "faction census localization records"
    )
    report_by_id: dict[str, dict[str, Any]] = {}
    for index, raw_row in enumerate(report_rows):
        row = _object(raw_row, f"faction census localization records[{index}]")
        identifier = _text(
            row.get("id"), f"faction census localization records[{index}].id", maximum=4096
        )
        key = identifier.casefold()
        if key in report_by_id:
            raise ValueError("faction census localization records contains duplicates")
        report_by_id[key] = {
            "id": identifier,
            "charCount": _integer(
                row.get("charCount"),
                f"faction census localization records[{index}].charCount",
            ),
            "utf8Sha256": _digest(
                row.get("utf8Sha256"),
                f"faction census localization records[{index}].utf8Sha256",
            ),
        }
    if set(report_by_id) != {item.casefold() for item in requested}:
        raise ValueError("resolved localization records do not match the requested identifier set")

    entry = _catalog_entry(catalog, STRING_CATALOG_PATH, "localization catalog")
    archive = catalog.open_archive_for(entry)
    source = archive.read_entry(catalog.as_entry(entry), max_bytes=MAX_STRING_BYTES)
    string_catalog = parse_string_catalog(source, duplicate_policy="first-wins")
    selected: dict[str, str] = {}
    for requested_id in requested:
        record = string_catalog.record(requested_id)
        if record is None:
            raise ValueError(f"localization identifier disappeared from lotr.str: {requested_id}")
        report_row = report_by_id[requested_id.casefold()]
        value_digest = hashlib.sha256(record.value.encode("utf-8")).hexdigest()
        if (
            report_row["id"] != record.identifier
            or report_row["charCount"] != len(record.value)
            or report_row["utf8Sha256"] != value_digest
        ):
            raise ValueError(
                f"localization census record no longer matches lotr.str: {requested_id}"
            )
        selected[record.identifier] = record.value
    return {
        "schema": "openbfme.localized-strings",
        "schemaVersion": 0,
        "locale": "en",
        "complete": False,
        "strings": dict(
            sorted(selected.items(), key=lambda item: (item[0].casefold(), item[0]))
        ),
    }


def _weighted_references(value: Any, context: str) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for index, raw_reference in enumerate(_array(value, context)):
        reference = _object(raw_reference, f"{context}[{index}]")
        if set(reference) not in ({"id"}, {"id", "weight"}):
            raise ValueError(f"{context}[{index}] has unsupported fields")
        item: dict[str, Any] = {
            "id": _text(reference.get("id"), f"{context}[{index}].id", maximum=256)
        }
        if "weight" in reference:
            item["weight"] = _integer(
                reference["weight"], f"{context}[{index}].weight"
            )
        result.append(item)
    return result


def _parameters(value: Any, context: str) -> list[dict[str, str]]:
    result: list[dict[str, str]] = []
    for index, raw_parameter in enumerate(_array(value, context)):
        parameter = _object(raw_parameter, f"{context}[{index}]")
        if set(parameter) != {"field", "value"}:
            raise ValueError(f"{context}[{index}] has unsupported fields")
        result.append(
            {
                "field": _text(
                    parameter.get("field"), f"{context}[{index}].field", maximum=256
                ),
                "value": _text(
                    parameter.get("value"), f"{context}[{index}].value", maximum=4096
                ),
            }
        )
    return result


def _definition_map(value: Any, context: str, reference_field: str) -> dict[str, Any]:
    result: dict[str, Any] = {}
    folded: set[str] = set()
    for index, raw_row in enumerate(_array(value, context)):
        row = _object(raw_row, f"{context}[{index}]")
        if set(row) != {"id", reference_field, "parameters"}:
            raise ValueError(f"{context}[{index}] has unsupported fields")
        identifier = _text(row.get("id"), f"{context}[{index}].id", maximum=256)
        key = identifier.casefold()
        if key in folded:
            raise ValueError(f"{context} contains duplicate identifiers")
        folded.add(key)
        result[identifier] = {
            reference_field: _weighted_references(
                row.get(reference_field), f"{context}[{index}].{reference_field}"
            ),
            "parameters": _parameters(
                row.get("parameters"), f"{context}[{index}].parameters"
            ),
        }
    return dict(sorted(result.items(), key=lambda item: (item[0].casefold(), item[0])))


def _audio_resources_and_manifest(
    catalog: InstallCatalog,
    report: dict[str, Any],
    dependencies: dict[str, Any],
    leaves: dict[str, set[str]],
    audio_slug: str,
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    if not audio_slug or any(
        character not in "abcdefghijklmnopqrstuvwxyz0123456789"
        for character in audio_slug
    ):
        raise ValueError(f"faction audio slug is unsafe: {audio_slug!r}")
    resolved = _object(report.get("resolvedLeaves"), "faction census resolvedLeaves")
    audio = _object(resolved.get("audio"), "faction census resolvedLeaves.audio")
    roots = _unique_strings(audio.get("rootIds"), "faction census audio.rootIds")
    dependency_roots = _unique_strings(
        dependencies.get("audioRootIds"), "faction census dependencies.audioRootIds"
    )
    if roots != dependency_roots:
        raise ValueError("audio roots do not match the dependency root set")
    sample_ids = _unique_strings(audio.get("sampleIds"), "faction census audio.sampleIds")
    events = _definition_map(audio.get("events"), "faction census audio.events", "sounds")
    multisounds = _definition_map(
        audio.get("multisounds"), "faction census audio.multisounds", "subsounds"
    )
    if {item.casefold() for item in events} & {item.casefold() for item in multisounds}:
        raise ValueError("audio event and multisound identifiers overlap")
    definitions = {
        **{item.casefold(): item for item in events},
        **{item.casefold(): item for item in multisounds},
    }
    if any(item.casefold() not in definitions for item in roots):
        raise ValueError("audio root does not resolve to an event or multisound")
    sample_keys = {item.casefold() for item in sample_ids}
    for event_id, event in events.items():
        if any(item["id"].casefold() not in sample_keys for item in event["sounds"]):
            raise ValueError(f"audio event {event_id!r} references a missing sample")
    for multisound_id, multisound in multisounds.items():
        if any(
            item["id"].casefold() not in definitions
            for item in multisound["subsounds"]
        ):
            raise ValueError(f"multisound {multisound_id!r} references a missing definition")

    raw_paths = _array(audio.get("samplePaths"), "faction census audio.samplePaths")
    paths_by_id: dict[str, tuple[str, str]] = {}
    generated_paths: set[str] = set()
    for index, raw_row in enumerate(raw_paths):
        row = _object(raw_row, f"faction census audio.samplePaths[{index}]")
        identifier = _text(
            row.get("id"), f"faction census audio.samplePaths[{index}].id", maximum=256
        )
        key = identifier.casefold()
        if key in paths_by_id:
            raise ValueError("faction census audio samplePaths contains duplicate identifiers")
        path = _virtual_path(
            row.get("virtualPath"),
            f"faction census audio.samplePaths[{index}].virtualPath",
        )
        entry = _catalog_entry(catalog, path, "audio sample")
        path = entry.name
        _require_leaf_role(leaves, path, "audio-sample", "audio sample")
        source_stem = PurePosixPath(path).stem
        if source_stem.casefold() != key or _SAFE_OUTPUT_STEM.fullmatch(source_stem) is None:
            raise ValueError(f"audio sample {identifier!r} has an unsafe or mismatched source stem")
        output_path = f"assets/audio/{audio_slug}/{source_stem.casefold()}.wav"
        output_key = output_path.casefold()
        if output_key in generated_paths:
            raise ValueError("generated audio output path collision")
        generated_paths.add(output_key)
        paths_by_id[key] = (path, output_path)
    if set(paths_by_id) != sample_keys:
        raise ValueError("audio sample paths do not match the requested sample identifier set")

    ordered_samples = [
        (identifier, *paths_by_id[identifier.casefold()]) for identifier in sample_ids
    ]
    resources: list[dict[str, Any]] = []
    for batch_index, batch in enumerate(
        _chunks(ordered_samples, MAX_PATTERNS_PER_RESOURCE)
    ):
        resources.append(
            {
                "id": f"{audio_slug}-audio-leaves-{batch_index:03d}",
                "kind": "audio",
                "converter": "audio",
                "patterns": [item[1] for item in batch],
                "output": f"assets/audio/{audio_slug}/{{stem}}.wav",
                "required": True,
                "limit": len(batch),
                "expected_count": len(batch),
                "options": {"force_pcm": True},
            }
        )
    samples = {item[0]: item[2] for item in ordered_samples}
    return resources, {
        "schema": "openbfme.audio-events",
        "schemaVersion": 1,
        "complete": False,
        "rootIds": roots,
        "events": events,
        "multisounds": multisounds,
        "samples": samples,
    }


def _validate_report(
    report: Any, expected_faction: str = "Men", expected_template: str = "FactionMen"
) -> dict[str, Any]:
    result = _object(report, "faction census report")
    if result.get("format") != 1:
        raise ValueError("faction census report has an unsupported format")
    if result.get("schema") != _REPORT_SCHEMA or result.get("schemaVersion") != 1:
        raise ValueError("faction census report has an unsupported schema")
    if result.get("closureStatus") != _REPORT_CLOSURE:
        raise ValueError("faction census report does not contain the required leaf closure")
    target = _object(result.get("target"), "faction census target")
    if (
        target.get("game") != "BFME2"
        or target.get("patch") != "1.06"
        or target.get("faction") != expected_faction
        or target.get("playerTemplate") != expected_template
    ):
        raise ValueError(f"faction census target is not BFME2 1.06 {expected_faction}")
    _digest(result.get("inputSetSha256"), "faction census inputSetSha256")
    unresolved = _object(result.get("unresolved"), "faction census unresolved")
    if not unresolved:
        raise ValueError("faction census unresolved diagnostics are missing")
    for name, value in unresolved.items():
        if not isinstance(name, str) or not isinstance(value, list):
            raise ValueError("faction census unresolved diagnostics are malformed")
        if value:
            raise ValueError("faction census contains unresolved dependencies")
    summary = _object(result.get("summary"), "faction census summary")
    if _integer(summary.get("unresolvedCount"), "faction census summary.unresolvedCount") != 0:
        raise ValueError("faction census contains unresolved dependencies")
    _object(result.get("dependencies"), "faction census dependencies")
    _object(result.get("resolvedLeaves"), "faction census resolvedLeaves")
    return result


def _validate_relevant_summary_counts(
    report: dict[str, Any],
    dependencies: dict[str, Any],
    source_leaves: dict[str, set[str]],
    ui_manifest: dict[str, Any],
    strings_manifest: dict[str, Any],
    audio_manifest: dict[str, Any],
) -> None:
    summary = _object(report.get("summary"), "faction census summary")
    expected = {
        "mappedImageCount": len(
            _array(dependencies.get("mappedImages"), "faction census dependencies.mappedImages")
        ),
        "mappedImageResolvedCount": len(ui_manifest["images"]),
        "textIdCount": len(
            _array(dependencies.get("textIds"), "faction census dependencies.textIds")
        ),
        "textResolvedCount": len(strings_manifest["strings"]),
        "audioRootCount": len(audio_manifest["rootIds"]),
        "audioEventCount": len(audio_manifest["events"]),
        "audioMultisoundCount": len(audio_manifest["multisounds"]),
        "audioSampleCount": len(audio_manifest["samples"]),
        "sourceLeafCount": len(source_leaves),
    }
    for field, count in expected.items():
        if _integer(summary.get(field), f"faction census summary.{field}") != count:
            raise ValueError(f"faction census summary.{field} does not match its records")


def build_men_leaf_profile_from_report(
    catalog: InstallCatalog, report: dict[str, Any] | None
) -> dict[str, Any]:
    """Build a JSON-ready profile from a freshly produced neutral census."""

    if report is None:
        raise ValueError("faction census report is required")
    report = _validate_report(report)
    dependencies = _object(report["dependencies"], "faction census dependencies")
    source_documents = _source_document_paths(catalog, report)
    source_leaves = _source_leaf_roles(catalog, report)
    semantic_resources = _semantic_resources(source_documents)
    ui_resources, ui_manifest = _ui_resources_and_manifest(
        catalog, report, dependencies, source_leaves
    )
    strings_manifest = _localized_strings(catalog, report, dependencies)
    audio_slug = _text(
        _object(report["target"], "faction census target").get("faction"),
        "faction census target.faction",
        maximum=64,
    ).casefold()
    audio_resources, audio_manifest = _audio_resources_and_manifest(
        catalog, report, dependencies, source_leaves, audio_slug
    )
    _validate_relevant_summary_counts(
        report,
        dependencies,
        source_leaves,
        ui_manifest,
        strings_manifest,
        audio_manifest,
    )
    resources = [*semantic_resources, *ui_resources, *audio_resources]
    if not 1 <= len(resources) <= MAX_RESOURCES:
        raise ValueError(
            f"generated Men leaf profile requires {len(resources)} resources; "
            f"ImportProfile supports at most {MAX_RESOURCES}"
        )

    profile: dict[str, Any] = {
        "format": 1,
        "id": PROFILE_ID,
        "title": "BFME II 1.06 Men private command/UI/audio/gameplay-definition leaf closure",
        "pack": {
            "id": PACK_ID,
            "version": "1.06-leaves-v0",
            "schema": "openbfme.content-pack",
            "schemaVersion": 0,
            "priority": 905,
            "vertical_slice_complete": False,
            "full_faction_complete": False,
            "asset_conversion_complete": False,
            "oracle_parity_complete": False,
            "capability_maturity": "command-ui-localization-audio-gameplay-definition-leaves-only",
            "censusInputSha256": report["inputSetSha256"],
            "dataPolicy": {
                "externalPathsAllowed": False,
                "redistributable": False,
            },
            "files": {
                "uiManifest": "data/ui_manifest.json",
                "strings": "data/strings.json",
                "audioEvents": "data/audio_events.json",
            },
        },
        "resources": resources,
        "runtime_data": {
            "data/audio_events.json": audio_manifest,
            "data/strings.json": strings_manifest,
            "data/ui_manifest.json": ui_manifest,
        },
    }
    encoded = (
        json.dumps(profile, indent=2, sort_keys=True, ensure_ascii=False, allow_nan=False)
        + "\n"
    ).encode("utf-8")
    if len(encoded) > MAX_PROFILE_BYTES:
        raise ValueError(
            f"generated Men leaf profile exceeds the {MAX_PROFILE_BYTES} byte limit"
        )
    return profile


def build_men_leaf_profile(catalog: InstallCatalog) -> dict[str, Any]:
    """Census the install and return its deterministic private leaf profile."""

    return build_men_leaf_profile_from_report(catalog, census_men_faction(catalog))


# ---------------------------------------------------------------------------
# Faction pack audio extension (elves/dwarves/isengard/mordor/wild + overlays)
#
# The Men leaf profile above is the reference emission: a schemaVersion-1
# openbfme.audio-events registry plus the converter resources that cook every
# resolved sample.  The faction slice packs ship the same surface, generalized
# per faction, and additionally carry the retail eva.ini announcer coverage
# (the Camp* side sets) that the object-graph census never requests as roots.
# ---------------------------------------------------------------------------

_FACTION_PLAYER_TEMPLATES: dict[str, str] = {
    "Men": "FactionMen",
    "Elves": "FactionElves",
    "Dwarves": "FactionDwarves",
    "Isengard": "FactionIsengard",
    "Mordor": "FactionMordor",
    "Wild": "FactionWild",
}
_EVA_BLOCK_HEADER = re.compile(
    r"^(?:NewEvaEvent|PredefinedEvaEvent)\s+([A-Za-z0-9_+.-]+)\s*$", re.IGNORECASE
)
_EVA_SOUND_SENTINELS = frozenset({"none", "null", "nosound", "0"})
MAX_EVA_EVENTS = 1_024


def _eva_event_side_sounds(source: bytes) -> dict[str, tuple[str, tuple[tuple[str, str], ...]]]:
    """Index every eva.ini announcer block's ``(side, sound)`` pairs per event.

    Mirrors the census parser for ``NewEvaEvent`` and adds the
    ``PredefinedEvaEvent`` family (AllyDefeated, EnemyDefeated, ...) whose
    blocks author the same nested ``SideSound`` sections.
    """

    events: dict[str, list[tuple[str, str]]] = {}
    authored_names: dict[str, str] = {}
    current: str | None = None
    in_side_sound = False
    side: str | None = None
    for line in _ini_lines(source):
        header = _EVA_BLOCK_HEADER.fullmatch(line)
        if header is not None and current is None:
            current = header.group(1)
            authored_names.setdefault(current.casefold(), current)
            events.setdefault(current.casefold(), [])
            if len(events) > MAX_EVA_EVENTS:
                raise ValueError("eva event document count exceeds limit")
            continue
        if current is None:
            continue
        if line.casefold() == "end":
            if in_side_sound:
                in_side_sound = False
                side = None
            else:
                current = None
            continue
        if "=" in line:
            key, _, value = line.partition("=")
            key = key.strip().casefold()
            if in_side_sound and key == "side":
                side = _first_identifier(value.strip())
            elif in_side_sound and key == "sound" and side:
                sound = _first_identifier(value.strip())
                if sound and sound.casefold() not in _EVA_SOUND_SENTINELS:
                    events[current.casefold()].append((side, sound))
            continue
        if line.split()[0].casefold() == "sidesound":
            in_side_sound = True
    return {key: (authored_names[key], tuple(pairs)) for key, pairs in events.items()}


def _eva_side_map_document(
    events: dict[str, tuple[str, tuple[tuple[str, str], ...]]]
) -> dict[str, Any]:
    """Project the parsed eva.ini blocks into the runtime side-map document.

    The map is global retail data (identical for every faction): each event id
    binds one announcer sound per side.  Later-authored duplicate sides win,
    matching SAGE override order.
    """

    mapped: dict[str, dict[str, str]] = {}
    for key in sorted(events, key=str.casefold):
        authored, pairs = events[key]
        sides: dict[str, str] = {}
        for side, sound in pairs:
            sides[side] = sound
        if sides:
            mapped[authored] = dict(sorted(sides.items(), key=lambda item: item[0].casefold()))
    return {"schema": "openbfme.eva-events", "schemaVersion": 0, "events": mapped}


def _eva_audio_extension(
    catalog: InstallCatalog,
    side: str,
    audio_slug: str,
    existing_samples: dict[str, str],
    existing_definitions: dict[str, Any],
) -> tuple[list[dict[str, Any]], dict[str, Any], dict[str, Any], dict[str, Any]]:
    """Resolve the faction side's eva.ini Camp* announcer sets.

    Returns (resources, events, multisounds, samples) in exactly the manifest
    row shapes ``_audio_resources_and_manifest`` emits, so callers merge them
    field-by-field.  Every root must resolve through the same voice.ini /
    soundeffects.ini definition corpus the census uses; anything unresolved
    raises (fail closed, never a substitute).
    """

    eva_document = _read_document(catalog, EVA_PATH)
    sound_effects_document = _read_document(catalog, SOUND_EFFECTS_PATH)
    voice_document = _read_document(catalog, VOICE_PATH)
    eva_events = _eva_event_side_sounds(eva_document.source)
    side_keys = {side.casefold(), f"player{side.casefold()}"}
    roots: dict[str, str] = {}
    for _key in sorted(eva_events, key=str.casefold):
        _authored, pairs = eva_events[_key]
        for sound_side, sound in pairs:
            if sound_side.casefold() in side_keys:
                roots.setdefault(sound.casefold(), sound)
    definitions = parse_sage_audio_definitions(
        sound_effects_document.source + b"\n" + voice_document.source
    )
    closure = resolve_sage_audio_closure(definitions, sorted(roots.values(), key=str.casefold))
    virtual_paths = [entry.name for entry in _effective_entries(catalog).values()]
    sample_paths = resolve_audio_sample_paths(closure.sample_ids, virtual_paths)

    events: dict[str, Any] = {}
    multisounds: dict[str, Any] = {}
    for row in closure.events:
        if row.id.casefold() in {item.casefold() for item in existing_definitions}:
            continue
        neutral = row.neutral()
        neutral.pop("id")
        events[row.id] = neutral
    for row in closure.multisounds:
        if row.id.casefold() in {item.casefold() for item in existing_definitions}:
            continue
        neutral = row.neutral()
        neutral.pop("id")
        multisounds[row.id] = neutral

    ordered_new_samples: list[tuple[str, str]] = []
    known_sample_keys = {item.casefold() for item in existing_samples}
    for sample_id in closure.sample_ids:
        if sample_id.casefold() in known_sample_keys:
            continue
        virtual_path = sample_paths[sample_id]
        source_stem = PurePosixPath(virtual_path).stem
        if source_stem.casefold() != sample_id.casefold() or _SAFE_OUTPUT_STEM.fullmatch(source_stem) is None:
            raise ValueError(f"eva audio sample {sample_id!r} has an unsafe or mismatched source stem")
        _catalog_entry(catalog, virtual_path, "eva audio sample")
        ordered_new_samples.append((sample_id, virtual_path))

    resources: list[dict[str, Any]] = []
    for batch_index, batch in enumerate(_chunks(ordered_new_samples, MAX_PATTERNS_PER_RESOURCE)):
        resources.append(
            {
                "id": f"{audio_slug}-eva-audio-leaves-{batch_index:03d}",
                "kind": "audio",
                "converter": "audio",
                "patterns": [item[1] for item in batch],
                "output": f"assets/audio/{audio_slug}/{{stem}}.wav",
                "required": True,
                "limit": len(batch),
                "expected_count": len(batch),
                "options": {"force_pcm": True},
            }
        )
    samples = {
        item[0]: f"assets/audio/{audio_slug}/{PurePosixPath(item[1]).stem.casefold()}.wav"
        for item in ordered_new_samples
    }
    return resources, events, multisounds, samples


def _validate_faction_audio_report(report: Any, faction: str, template: str) -> dict[str, Any]:
    """Audio-scoped census validation for the faction pack audio emission.

    The full-report validator (``_validate_report``) gates on every census
    lane being unresolved-free; isengard/mordor/wild carry pre-existing
    diagnostics owned by other lanes (the ``+SOUND:`` tokenization and
    mapped-image texture gaps recorded in AUDIO-COVERAGE.md) that do not
    touch the audio closure.  This validator pins identity/schema and the
    audio subtree's presence; ``_audio_resources_and_manifest`` then enforces
    every audio invariant fail-closed (roots resolve, samples bind unique
    catalog leaves).  Any unresolved diagnostics stay visible in the returned
    report rather than being silently waived.
    """

    result = _object(report, "faction census report")
    if result.get("format") != 1:
        raise ValueError("faction census report has an unsupported format")
    if result.get("schema") != _REPORT_SCHEMA or result.get("schemaVersion") != 1:
        raise ValueError("faction census report has an unsupported schema")
    if result.get("closureStatus") != _REPORT_CLOSURE:
        raise ValueError("faction census report does not contain the required leaf closure")
    target = _object(result.get("target"), "faction census target")
    if (
        target.get("game") != "BFME2"
        or target.get("patch") != "1.06"
        or target.get("faction") != faction
        or target.get("playerTemplate") != template
    ):
        raise ValueError(f"faction census target is not BFME2 1.06 {faction}")
    _digest(result.get("inputSetSha256"), "faction census inputSetSha256")
    resolved = _object(result.get("resolvedLeaves"), "faction census resolvedLeaves")
    _object(resolved.get("audio"), "faction census resolvedLeaves.audio")
    return result


def build_faction_audio_extension(
    catalog: InstallCatalog,
    report: dict[str, Any],
    faction: str,
    *,
    include_census_registry: bool = True,
) -> dict[str, Any]:
    """Build one faction pack's audio surface from its leaf census report.

    Generalizes the Men leaf audio emission to any census-covered faction and
    adds the faction side's eva.ini announcer coverage.  Returns the profile
    fragments the caller merges into the composed faction profile:
    ``resources`` (converter rows), ``runtime_data`` (the schemaVersion-1
    audio registry and the eva side map), and ``files`` (pack registry keys).
    With ``include_census_registry=False`` only the EVA surface is emitted
    (overlay packs mounted next to a full host pack).
    """

    template = _FACTION_PLAYER_TEMPLATES.get(faction)
    if template is None:
        raise ValueError(f"faction audio extension has no player template for {faction!r}")
    report = _validate_faction_audio_report(report, faction, template)
    dependencies = _object(report["dependencies"], "faction census dependencies")
    audio_slug = faction.casefold()

    resources: list[dict[str, Any]] = []
    events: dict[str, Any] = {}
    multisounds: dict[str, Any] = {}
    samples: dict[str, str] = {}
    roots: list[str] = []
    if include_census_registry:
        source_leaves = _source_leaf_roles(catalog, report)
        census_resources, census_manifest = _audio_resources_and_manifest(
            catalog, report, dependencies, source_leaves, audio_slug
        )
        summary = _object(report.get("summary"), "faction census summary")
        for field, count in {
            "audioRootCount": len(census_manifest["rootIds"]),
            "audioEventCount": len(census_manifest["events"]),
            "audioMultisoundCount": len(census_manifest["multisounds"]),
            "audioSampleCount": len(census_manifest["samples"]),
        }.items():
            if _integer(summary.get(field), f"faction census summary.{field}") != count:
                raise ValueError(f"faction census summary.{field} does not match its records")
        resources.extend(census_resources)
        events.update(census_manifest["events"])
        multisounds.update(census_manifest["multisounds"])
        samples.update(census_manifest["samples"])
        roots.extend(census_manifest["rootIds"])

    existing_definitions: dict[str, Any] = {**events, **multisounds}
    eva_resources, eva_events, eva_multisounds, eva_samples = _eva_audio_extension(
        catalog, faction, audio_slug, samples, existing_definitions
    )
    resources.extend(eva_resources)
    events.update(eva_events)
    multisounds.update(eva_multisounds)
    samples.update(eva_samples)
    roots.extend(
        sorted(
            (
                identifier
                for identifier in (*eva_events.keys(), *eva_multisounds.keys())
                if identifier.casefold() not in {item.casefold() for item in roots}
            ),
            key=str.casefold,
        )
    )

    audio_manifest = {
        "schema": "openbfme.audio-events",
        "schemaVersion": 1,
        "complete": False,
        "rootIds": roots,
        "events": dict(sorted(events.items(), key=lambda item: item[0].casefold())),
        "multisounds": dict(sorted(multisounds.items(), key=lambda item: item[0].casefold())),
        "samples": dict(sorted(samples.items(), key=lambda item: item[0].casefold())),
    }
    eva_document = _read_document(catalog, EVA_PATH)
    eva_side_map = _eva_side_map_document(_eva_event_side_sounds(eva_document.source))
    unresolved = _object(report.get("unresolved"), "faction census unresolved")
    return {
        "resources": resources,
        "runtime_data": {
            "data/audio_events.json": audio_manifest,
            "data/eva_events.json": eva_side_map,
        },
        "files": {
            "audioEvents": "data/audio_events.json",
            "evaEvents": "data/eva_events.json",
        },
        # Pre-existing non-audio census diagnostics, surfaced (never hidden):
        # owned by other lanes and irrelevant to this audio closure.
        "unresolvedDiagnostics": {
            name: value for name, value in sorted(unresolved.items()) if value
        },
    }
