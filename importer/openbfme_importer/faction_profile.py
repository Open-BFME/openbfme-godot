"""Generate a bounded private import profile from the Men leaf census.

The census remains the source-neutral dependency graph.  This module turns
that graph into exact converter rules and private runtime manifests without
writing a profile or exposing an install path.  Localized values are the one
intentional retail payload in the returned value: they are selected privately
from ``lotr.str`` for the non-redistributable pack.
"""

from __future__ import annotations

from dataclasses import replace
import hashlib
import json
from pathlib import PurePosixPath
import re
from typing import Any, Iterable

from .catalog import InstallCatalog
from .faction_census import (
    EVA_PATH,
    FX_LIST_PATH,
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
from .playable_unit_compiler import (
    _numeric_defines,
    _resolved_multiplicative_expression,
)
from .sage_audio import (
    parse_sage_audio_definitions,
    resolve_audio_sample_paths,
    resolve_audio_sample_paths_partial,
    resolve_sage_audio_closure,
)
from .sage_cst import parse_sage_document
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
    # RotWK 2.01's expansion side. eva.ini authors a full Angmar announcer set
    # (93 side-sound rows), so an Angmar overlay pack is composable exactly the
    # way the six BFME2 sides are.
    "Angmar": "FactionAngmar",
}
# The exact (game, patch) identity labels faction_census stamps onto a leaf
# census target, one per supported retail edition. Kept as pairs so a census
# cannot claim one edition's game name with the other's patch level.
CENSUS_EDITIONS: frozenset[tuple[str, str]] = frozenset(
    {("BFME2", "1.06"), ("RotWK", "2.01")}
)
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
    events: dict[str, tuple[str, tuple[tuple[str, str], ...]]],
    source: bytes,
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
    semantics = _eva_event_semantics(source)
    return {
        "schema": "openbfme.eva-events",
        "schemaVersion": 1,
        "events": mapped,
        "semantics": semantics,
    }


def _eva_event_semantics(source: bytes) -> dict[str, dict[str, Any]]:
    """Compile retail EVA arbitration fields into runtime-consumable values.

    Three field shapes share the block walk:

    - integers (``priority``, ``cooldownMs``, ``quietTimeMs``, ``expirationMs``,
      ``delayMs``): resolved through eva.ini's ``#define`` constants and held
      to the same non-negative-integer rule as before;
    - yes/no flags (``playFromHomeBase``, ``jumpToLocation``): retail authors
      only ``Yes``/``No``; anything else fails closed;
    - event-id lists (``blockEvents`` from ``OtherEvaEventsToBlock``): the
      whitespace-separated event names, verbatim and in authored order. A
      name may reference an event defined LATER in the file (retail ships
      ``EvaEventForwardReference`` for exactly that), so names are compiled
      unresolved, as authored.
    """

    constants = _numeric_defines({EVA_PATH: source})
    output: dict[str, dict[str, Any]] = {}
    current = ""
    in_side_sound = False
    for line in _ini_lines(source):
        header = _EVA_BLOCK_HEADER.fullmatch(line)
        if header is not None and current == "":
            current = header.group(1)
            output.setdefault(current, {})
            continue
        if current == "":
            continue
        if line.casefold() == "end":
            if in_side_sound:
                in_side_sound = False
            else:
                current = ""
            continue
        if line.split()[0].casefold() == "sidesound":
            in_side_sound = True
            continue
        if in_side_sound or "=" not in line:
            continue
        key, _, raw = line.partition("=")
        folded = key.strip().casefold()
        destination = _EVA_COMPILED_SEMANTIC_FIELDS.get(folded)
        if destination is None:
            continue
        if destination == "blockEvents":
            tokens = raw.strip().split()
            if not tokens or any(_EVA_BLOCK_REFERENCE.fullmatch(token) is None for token in tokens):
                raise ValueError(
                    f"eva-event-semantics-unresolved:{current}:{key.strip()}:{raw.strip()}"
                )
            output[current][destination] = tokens
            continue
        if destination in ("playFromHomeBase", "jumpToLocation"):
            token = raw.strip().split()[0].casefold() if raw.strip() else ""
            if token not in ("yes", "no"):
                raise ValueError(
                    f"eva-event-semantics-unresolved:{current}:{key.strip()}:{raw.strip()}"
                )
            output[current][destination] = token == "yes"
            continue
        value = _resolved_multiplicative_expression(raw.strip(), constants)
        if value is None or float(value) < 0 or not float(value).is_integer():
            raise ValueError(
                f"eva-event-semantics-unresolved:{current}:{key.strip()}:{raw.strip()}"
            )
        output[current][destination] = int(value)
    return {
        event_id: fields
        for event_id, fields in sorted(output.items(), key=lambda item: item[0].casefold())
        if fields
    }


def _resolve_audio_roots_extension(
    catalog: InstallCatalog,
    label: str,
    audio_slug: str,
    roots: list[str],
    existing_samples: dict[str, str],
    existing_definitions: dict[str, Any],
) -> tuple[
    list[dict[str, Any]],
    dict[str, Any],
    dict[str, Any],
    dict[str, Any],
    list[str],
    list[str],
    Any,
]:
    """Resolve ``roots`` through the voice.ini / soundeffects.ini corpus.

    The one resolver behind every extra-root audio surface (EVA announcers,
    Create-a-Hero class voices).  Returns (resources, events, multisounds,
    samples, missing_samples, dropped_definitions) in the manifest row shapes
    ``_audio_resources_and_manifest`` emits.  Definitions the pack already
    carries are not re-emitted; a definition left with no playable leaf is
    DROPPED and named, never shipped as a silent stub.
    """

    sound_effects_document = _read_document(catalog, SOUND_EFFECTS_PATH)
    voice_document = _read_document(catalog, VOICE_PATH)
    definitions = parse_sage_audio_definitions(
        sound_effects_document.source + b"\n" + voice_document.source
    )
    closure = resolve_sage_audio_closure(definitions, sorted(roots, key=str.casefold))
    virtual_paths = [entry.name for entry in _effective_entries(catalog).values()]
    sample_paths, missing_samples, ambiguous_samples = resolve_audio_sample_paths_partial(
        closure.sample_ids, virtual_paths
    )
    if ambiguous_samples:
        # Two catalog leaves answering one stem is an INSTALL problem, not a
        # retail authoring fact: refusing keeps the coin-flip out of the pack.
        raise ValueError(
            "ambiguous %s audio samples: %s" % (label, ", ".join(sorted(ambiguous_samples)))
        )

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
    dropped, pruned = _prune_unplayable_definitions(events, multisounds, missing_samples)

    ordered_new_samples: list[tuple[str, str]] = []
    known_sample_keys = {item.casefold() for item in existing_samples}
    for sample_id in closure.sample_ids:
        if sample_id.casefold() in known_sample_keys:
            continue
        if sample_id not in sample_paths:
            continue
        virtual_path = sample_paths[sample_id]
        source_stem = PurePosixPath(virtual_path).stem
        if source_stem.casefold() != sample_id.casefold() or _SAFE_OUTPUT_STEM.fullmatch(source_stem) is None:
            raise ValueError(f"{label} audio sample {sample_id!r} has an unsafe or mismatched source stem")
        _catalog_entry(catalog, virtual_path, f"{label} audio sample")
        ordered_new_samples.append((sample_id, virtual_path))

    resources: list[dict[str, Any]] = []
    for batch_index, batch in enumerate(_chunks(ordered_new_samples, MAX_PATTERNS_PER_RESOURCE)):
        resources.append(
            {
                "id": f"{audio_slug}-{label}-audio-leaves-{batch_index:03d}",
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
    return resources, events, multisounds, samples, sorted(missing_samples, key=str.casefold), dropped, pruned


def _cah_voice_audio_extension(
    catalog: InstallCatalog,
    audio_slug: str,
    existing_samples: dict[str, str],
    existing_definitions: dict[str, Any],
) -> tuple[list[dict[str, Any]], dict[str, Any], dict[str, Any], dict[str, Any], dict[str, Any]]:
    """Cook the Create-a-Hero class voice sets into the faction's registry.

    A created hero is synthesized at runtime from the ``cah.system`` table,
    which names its voice events BY REFERENCE (``HeroWestMaleVoiceAttack``)
    into the audio registry - it ships no samples of its own, so every event
    the 16 subclass SoundUpgrade blocks in ``createaheroaudio.inc`` name must
    be a root of the faction pack's registry or the hero is mute.  Before this
    the registry only carried events some CONVERTED unit referenced, and
    ``retail_slice_audio`` reported every created hero as
    ``unvoiced_created_hero`` (owner's v0.2.8 run.log).  Same resolver as the
    EVA announcers; the roots are the union of the unconditional subclass
    routes the CAH compiler projects.
    """

    from .cah_system_compiler import AUDIO_PATH as CAH_AUDIO_PATH
    from .cah_system_compiler import _cah_voice_bindings

    audio_document = _read_document(catalog, CAH_AUDIO_PATH)
    bindings = _cah_voice_bindings({CAH_AUDIO_PATH: audio_document.source})
    roots: dict[str, str] = {}
    for fields in bindings.values():
        for event_ids in fields.values():
            for event_id in event_ids:
                roots.setdefault(event_id.casefold(), event_id)
    (
        resources,
        events,
        multisounds,
        samples,
        missing_samples,
        dropped,
        pruned,
    ) = _resolve_audio_roots_extension(
        catalog, "cah", audio_slug, sorted(roots.values(), key=str.casefold),
        existing_samples, existing_definitions,
    )
    diagnostics = {
        "rootCount": len(roots),
        "missingSamples": missing_samples,
        "droppedDefinitions": sorted(dropped, key=str.casefold),
        "prunedDefinitions": pruned,
    }
    return resources, events, multisounds, samples, diagnostics


def _eva_audio_extension(
    catalog: InstallCatalog,
    side: str,
    audio_slug: str,
    existing_samples: dict[str, str],
    existing_definitions: dict[str, Any],
) -> tuple[
    list[dict[str, Any]],
    dict[str, Any],
    dict[str, Any],
    dict[str, Any],
    dict[str, list[str]],
]:
    """Resolve the faction side's eva.ini Camp* announcer sets.

    Returns (resources, events, multisounds, samples, diagnostics) in exactly
    the manifest row shapes ``_audio_resources_and_manifest`` emits, so callers
    merge them field-by-field.  Every root must resolve through the same
    voice.ini / soundeffects.ini definition corpus the census uses.

    RETAIL'S OWN BROKEN REFERENCES ARE A FACT, NOT A FAILURE. RotWK 2.01 names
    four Angmar announcer samples that ship under no name at all (EA prefix
    typos: ``KUAngUpg_Sanct2`` for the shipped ``kuupg_sanct2``, and so on).
    A missing sample costs its own leaf and nothing else; a definition left
    with zero playable leaves is DROPPED rather than shipped as a silent stub
    that would read as a routing bug at runtime.  Both are reported by name in
    ``diagnostics`` and never repaired by guessing which file EA meant - that
    guess is the substitution this pipeline exists to refuse.
    """

    eva_document = _read_document(catalog, EVA_PATH)
    eva_events = _eva_event_side_sounds(eva_document.source)
    side_keys = {side.casefold(), f"player{side.casefold()}"}
    roots: dict[str, str] = {}
    for _key in sorted(eva_events, key=str.casefold):
        _authored, pairs = eva_events[_key]
        for sound_side, sound in pairs:
            if sound_side.casefold() in side_keys:
                roots.setdefault(sound.casefold(), sound)
    (
        resources,
        events,
        multisounds,
        samples,
        missing_samples,
        dropped,
        pruned,
    ) = _resolve_audio_roots_extension(
        catalog, "eva", audio_slug, sorted(roots.values(), key=str.casefold),
        existing_samples, existing_definitions,
    )
    dropped_keys = {item.casefold() for item in dropped}
    orphans: list[dict[str, str]] = []
    for _key in sorted(eva_events, key=str.casefold):
        authored, pairs = eva_events[_key]
        for sound_side, sound in pairs:
            if sound.casefold() in dropped_keys:
                orphans.append({"event": authored, "side": sound_side, "sound": sound})
    installed = {entry.name.casefold(): entry.name for entry in _effective_entries(catalog).values()}
    counterparts: dict[str, str] = {}
    for sample_id in missing_samples:
        candidate = _MISSING_SAMPLE_COUNTERPARTS.get(sample_id.casefold())
        if candidate is not None and candidate.casefold() in installed:
            counterparts[sample_id] = installed[candidate.casefold()]
    diagnostics = {
        "missingSamples": sorted(missing_samples, key=str.casefold),
        # NAMED, NEVER APPLIED. Which file EA meant is the owner's call, not the
        # compiler's; recording the candidate keeps the decision available
        # without smuggling it into a pack.
        "missingSampleCounterparts": dict(sorted(counterparts.items(), key=lambda item: item[0].casefold())),
        "droppedDefinitions": sorted(dropped, key=str.casefold),
        "prunedDefinitions": pruned,
        "orphanedSideMapLeaves": orphans,
        "orphanPolicy": (
            "recorded-not-pruned: the side map stays the faithful projection of "
            "retail's authored eva.ini, so a leaf naming a dropped definition is "
            "reported here rather than edited out. At runtime it fails closed on "
            "the registry lookup (missing_event), never on a substitute."
        ),
    }
    return resources, events, multisounds, samples, diagnostics


def _prune_unplayable_definitions(
    events: dict[str, Any],
    multisounds: dict[str, Any],
    missing_samples: tuple[str, ...],
) -> tuple[list[str], list[dict[str, Any]]]:
    """Drop leaves whose sample does not exist, and definitions left empty.

    Runs to a fixpoint: a multisound whose every subsound was dropped is itself
    unplayable, and any parent referencing it loses that leaf in turn.

    Returns (dropped, pruned).  ``dropped`` names definitions removed outright;
    ``pruned`` names definitions that SURVIVED IN EDITED FORM, with the leaves
    they lost.  Reporting only the first set hides the more misleading case: a
    multisound retail authored as "Angmar line + region cheer" that now plays
    only the cheer still resolves at runtime, so nothing downstream would ever
    surface it.  Both sets are facts about retail's own broken references and
    both belong in the pack's disclosure.
    """

    if not missing_samples:
        return [], []
    unplayable = {item.casefold() for item in missing_samples}
    dropped: list[str] = []
    pruned: dict[str, dict[str, Any]] = {}
    changed = True
    while changed:
        changed = False
        for table, field, kind in (
            (events, "sounds", "event"),
            (multisounds, "subsounds", "multisound"),
        ):
            for definition_id in list(table):
                rows = table[definition_id][field]
                kept = [row for row in rows if str(row["id"]).casefold() not in unplayable]
                if len(kept) != len(rows):
                    record = pruned.setdefault(
                        definition_id, {"id": definition_id, "kind": kind, "removed": []}
                    )
                    for row in rows:
                        leaf = str(row["id"])
                        if leaf.casefold() in unplayable and leaf not in record["removed"]:
                            record["removed"].append(leaf)
                    table[definition_id][field] = kept
                    changed = True
                if not kept:
                    del table[definition_id]
                    dropped.append(definition_id)
                    unplayable.add(definition_id.casefold())
                    changed = True
    # A definition that was pruned and then dropped is reported as dropped only:
    # naming it twice would overstate how much survived in edited form.
    survivors = [
        dict(record, removed=sorted(record["removed"], key=str.casefold))
        for definition_id, record in pruned.items()
        if definition_id not in dropped
    ]
    return dropped, sorted(survivors, key=lambda row: str(row["id"]).casefold())


# RotWK 2.01 names four announcer samples that ship under no name, and for each
# one the install DOES ship a file whose stem differs only by an EA prefix typo
# (`KUAngUpg_` for `kuupg_`, `Rova` for `Rhoa`). Recorded so the disclosure can
# name the counterpart; every entry is re-checked against the install at compose
# time, so this table can name a file but never invent one. NOTHING IS MAPPED:
# rebinding a broken id onto a lookalike is the substitution this pipeline
# refuses, and it stays the owner's call.
_MISSING_SAMPLE_COUNTERPARTS = {
    "kuangupg_sanct2": "data/audio/sounds/kuupg_sanct2.wav",
    "kuangupg_sanctum": "data/audio/sounds/kuupg_sanctum.wav",
    "kuwar_losrova": "data/audio/sounds/kuwar_losrhoa.wav",
    "kuwar_unirova": "data/audio/sounds/kuwar_unirhoa.wav",
}
# Retail authors these EVA block fields and the compiler emits all of them.
# Counted from the bytes at compose time rather than listed here, so the
# declaration cannot drift away from the file.
_EVA_COMPILED_SEMANTIC_FIELDS = {
    "priority": "priority",
    "timebetweeneventsms": "cooldownMs",
    "quiettimems": "quietTimeMs",
    "expirationtimems": "expirationMs",
    "millisecondstowaitbeforeplaying": "delayMs",
    "otherevaeventstoblock": "blockEvents",
    "alwaysplayfromhomebase": "playFromHomeBase",
    "countasjumptolocation": "jumpToLocation",
}
# An OtherEvaEventsToBlock token names another eva event block.
_EVA_BLOCK_REFERENCE = re.compile(r"^[A-Za-z0-9_+.-]+$")
# What retail_slice_audio._play_eva_announcement actually reads today.
_EVA_RUNTIME_CONSUMED_FIELDS = ("blockEvents", "cooldownMs", "delayMs", "priority")
# Structural, not semantics: these build the side map itself.
_EVA_STRUCTURAL_FIELDS = frozenset({"side", "sound"})


def _eva_semantic_field_coverage(source: bytes) -> dict[str, Any]:
    """Declare which eva.ini block fields this pack's semantics carry.

    An announcer document that silently omits a field implies a completeness
    it does not have. Naming the omissions - with the count retail authors -
    keeps a reader from mistaking "not implemented" for "not used". The
    ``MiscEvaData`` global block is counted too: its fields are authored in
    eva.ini and stay uncompiled (they belong to the camp-destroyed and
    jump-to-camera lanes, not announcer arbitration).
    """

    authored: dict[str, int] = {}
    depth = 0
    inside = False
    for line in _ini_lines(source):
        header = _EVA_BLOCK_HEADER.fullmatch(line)
        if (header is not None or line.strip().casefold() == "miscevadata") and not inside:
            inside = True
            depth = 0
            continue
        if not inside:
            continue
        stripped = line.strip()
        if stripped.casefold() == "end":
            if depth == 0:
                inside = False
            else:
                depth -= 1
            continue
        if "=" not in stripped:
            if stripped:
                depth += 1
            continue
        name = stripped.split("=", 1)[0].strip()
        folded = name.casefold()
        if folded in _EVA_COMPILED_SEMANTIC_FIELDS or folded in _EVA_STRUCTURAL_FIELDS:
            continue
        authored[name] = authored.get(name, 0) + 1
    compiled_counts: dict[str, int] = {}
    for folded, emitted in _EVA_COMPILED_SEMANTIC_FIELDS.items():
        if emitted in _EVA_RUNTIME_CONSUMED_FIELDS:
            continue
        compiled_counts[emitted] = sum(
            1
            for line in _ini_lines(source)
            if line.strip().split("=", 1)[0].strip().casefold() == folded
            and "=" in line
        )
    return {
        "compiledAndConsumedByRuntime": sorted(_EVA_RUNTIME_CONSUMED_FIELDS),
        "compiledButUnconsumed": dict(sorted(compiled_counts.items())),
        "authoredButNotCompiled": dict(sorted(authored.items())),
    }


# An Object block's creation voice can name an EVA announcer event instead of
# a positional sound: ``VoiceCreated = EVA:NazgulCreated`` (pure RotWK 2.01
# mordorblackrider.ini:686-687). ``VoiceFullyCreated`` carries the same
# binding for objects retail announces at construction completion; the two
# never disagree on one object (verified against the oracle corpus at compose
# time - a disagreement raises rather than picking one silently).
#
# Two more retail create hooks feed the same map:
# - SAGE ``ChildObject`` / ``ObjectReskin`` inherit the parent's authored
#   fields (``ChildObject MordorSauron_RingHero MordorSauron`` keeps
#   ``VoiceCreated = EVA:SauronCreated``).
# - Fortress heroes comment ``VoiceCreated`` out and rehook the announcement
#   to spawn FX (lurtz.ini:670-671; fxlist.ini:12021-12027
#   ``EvaEventOwner = LurtzCreated``). ``InitialSpawnFX`` is that hook.
#   ``RespawnAsTemplate`` is the last-resort follow for the foot Witch-King,
#   which authors no spawn FX and converts into the fellbeast object that
#   does (witchking.ini:371).
_EVA_CREATED_VOICE = re.compile(r"^eva:([A-Za-z0-9_+.-]+)\s*$", re.IGNORECASE)
_EVA_CREATED_OBJECT_PREFIX = "data/ini/object/"
_EVA_CREATED_FX_HEADER = re.compile(r"^FXList\s+([A-Za-z0-9_+.-]+)\s*$", re.IGNORECASE)
MAX_EVA_CREATED_BINDINGS = 4_096
MAX_EVA_CREATED_FX_LISTS = 8_192
MAX_EVA_CREATED_ANCESTRY = 64


def _fx_list_eva_event_owners(source: bytes) -> dict[str, tuple[str, ...]]:
    """Index ``EvaEvent / EvaEventOwner`` tokens per FXList.

    ``InitialSpawnFX`` names one of these lists; the nested ``EvaEventOwner``
    is the eva.ini block the announcer fires when that spawn FX plays.
    """

    owners: dict[str, list[str]] = {}
    current_list: str | None = None
    section_stack: list[str] = []
    for line in _ini_lines(source):
        header = _EVA_CREATED_FX_HEADER.fullmatch(line)
        if header is not None and current_list is None:
            current_list = header.group(1)
            owners.setdefault(current_list.casefold(), [])
            if len(owners) > MAX_EVA_CREATED_FX_LISTS:
                raise ValueError("eva created-event FXList count exceeds limit")
            continue
        if current_list is None:
            continue
        if line.casefold() == "end":
            if section_stack:
                section_stack.pop()
            else:
                current_list = None
            continue
        if "=" in line:
            key, _, value = line.partition("=")
            if (
                key.strip().casefold() == "evaeventowner"
                and section_stack
                and section_stack[-1] == "evaevent"
            ):
                token = value.strip()
                if token:
                    owners[current_list.casefold()].append(token)
            continue
        section_stack.append(line.split()[0].casefold())
    return {key: tuple(values) for key, values in owners.items()}


def _eva_created_ancestry(
    index: dict[str, Any], target: Any
) -> tuple[Any, ...]:
    """Root-to-child ancestry. Annotation parents on plain Object stop the walk."""

    current = target
    if (
        target.kind.casefold() == "object"
        and target.parent is not None
        and target.parent.casefold() not in index
    ):
        current = replace(target, parent=None)
    chain = [current]
    seen = {current.name.casefold()}
    while current.parent:
        parent = index.get(current.parent.casefold())
        if parent is None:
            if current.kind.casefold() == "object":
                break
            raise ValueError(
                f"eva-created-binding-unresolved-parent:{target.name}:{current.parent}"
            )
        key = parent.name.casefold()
        if key in seen or len(chain) >= MAX_EVA_CREATED_ANCESTRY:
            raise ValueError(f"eva-created-binding-cycle:{target.name}")
        seen.add(key)
        chain.append(parent)
        current = parent
    return tuple(reversed(chain))


def _eva_effective_field(
    ancestry: tuple[Any, ...], key: str, *, recursive: bool = False
) -> tuple[str, ...]:
    selected: tuple[str, ...] = ()
    for item in ancestry:
        values = item.values(key, recursive=recursive)
        if values:
            selected = values
    return selected


def _eva_event_from_voice_values(obj_name: str, key: str, values: tuple[str, ...]) -> str | None:
    event: str | None = None
    for value in values:
        match = _EVA_CREATED_VOICE.fullmatch(value.strip())
        if match is None:
            if value.strip().casefold().startswith("eva:"):
                raise ValueError(
                    f"eva-created-binding-unresolved:{obj_name}:{key}:{value.strip()}"
                )
            continue
        candidate = match.group(1)
        if event is not None and event.casefold() != candidate.casefold():
            raise ValueError(
                f"eva-created-binding-conflict:{obj_name}:{key}:{event}:{candidate}"
            )
        event = candidate
    return event


def _eva_created_event_bindings(catalog: InstallCatalog) -> dict[str, str]:
    """Project every Object's authored create-EVA hook into one map.

    Retail keys creation announcements per OBJECT. Three authored sources,
    in precedence order:

    1. ``VoiceCreated = EVA:<event>`` (else ``VoiceFullyCreated = EVA:<event>``),
       with ``ChildObject`` / ``ObjectReskin`` inheriting the parent when the
       child does not override the field.
    2. ``InitialSpawnFX`` -> fxlist.ini ``EvaEvent.EvaEventOwner`` when that
       owner names an authored eva.ini block. This is the fortress-hero create
       hook (VoiceCreated commented out as "rehooked to spawn FX").
    3. ``RespawnAsTemplate`` follow, when the object itself authors neither
       hook but converts into one that does (foot Witch-King -> fellbeast).

    ``VoiceCreated`` wins when both a voice field and a spawn-FX owner name
    an event; they never disagree in pure RotWK 2.01 and a disagreement
    raises rather than guessing.
    """

    index: dict[str, Any] = {}
    paths = sorted(
        (
            entry.name
            for entry in _effective_entries(catalog).values()
            if entry.name.casefold().startswith(_EVA_CREATED_OBJECT_PREFIX)
            and entry.name.casefold().endswith(".ini")
        ),
        key=str.casefold,
    )
    for path in paths:
        document = parse_sage_document(_read_document(catalog, path).source, path)
        for obj in document.objects:
            index[obj.name.casefold()] = obj

    authored_events = {
        names[0].casefold(): names[0]
        for names in _eva_event_side_sounds(_read_document(catalog, EVA_PATH).source).values()
    }
    fx_owners = _fx_list_eva_event_owners(_read_document(catalog, FX_LIST_PATH).source)

    voice_bindings: dict[str, str] = {}
    spawn_bindings: dict[str, str] = {}
    respawn_as: dict[str, str] = {}
    for obj in index.values():
        ancestry = _eva_created_ancestry(index, obj)
        created_event = _eva_event_from_voice_values(
            obj.name, "VoiceCreated", _eva_effective_field(ancestry, "VoiceCreated")
        )
        fully_event = _eva_event_from_voice_values(
            obj.name,
            "VoiceFullyCreated",
            _eva_effective_field(ancestry, "VoiceFullyCreated"),
        )
        if (
            created_event is not None
            and fully_event is not None
            and created_event.casefold() != fully_event.casefold()
        ):
            raise ValueError(
                f"eva-created-binding-conflict:{obj.name}:{created_event}:{fully_event}"
            )
        if created_event is not None or fully_event is not None:
            voice_bindings[obj.name] = (
                created_event if created_event is not None else fully_event
            )  # type: ignore[assignment]

        spawn_values = _eva_effective_field(ancestry, "InitialSpawnFX", recursive=True)
        if spawn_values:
            owners = fx_owners.get(spawn_values[-1].strip().casefold(), ())
            chosen: str | None = None
            for owner in owners:
                event = authored_events.get(owner.casefold())
                if event is None:
                    raise ValueError(
                        f"eva-created-binding-unresolved:{obj.name}:InitialSpawnFX:{owner}"
                    )
                if chosen is not None and chosen.casefold() != event.casefold():
                    raise ValueError(
                        f"eva-created-binding-conflict:{obj.name}:InitialSpawnFX:{chosen}:{event}"
                    )
                chosen = event
            if chosen is not None:
                spawn_bindings[obj.name] = chosen

        templates = _eva_effective_field(ancestry, "RespawnAsTemplate", recursive=True)
        if templates:
            token = templates[-1].strip()
            if token:
                respawn_as[obj.name] = token

    bindings: dict[str, str] = dict(voice_bindings)
    for name, event in spawn_bindings.items():
        existing = bindings.get(name)
        if existing is None:
            bindings[name] = event
            continue
        if existing.casefold() != event.casefold():
            raise ValueError(
                f"eva-created-binding-conflict:{name}:{existing}:{event}"
            )
    for name, template in respawn_as.items():
        if name in bindings:
            continue
        event = bindings.get(template)
        if event is None:
            folded = template.casefold()
            event = next(
                (value for key, value in bindings.items() if key.casefold() == folded),
                None,
            )
        if event is not None:
            bindings[name] = event

    if len(bindings) > MAX_EVA_CREATED_BINDINGS:
        raise ValueError("eva created-event binding count exceeds limit")
    return dict(sorted(bindings.items(), key=lambda item: item[0].casefold()))


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
    # RotWK 2.01 is the shipped content baseline (owner decision 2026-07-27), so
    # a 2.01 census is as valid an audio source as a 1.06 one. The (game, patch)
    # pair is still checked as a PAIR: a relabelled census that claims one
    # edition's identity over the other's bytes stays refused.
    edition = (target.get("game"), target.get("patch"))
    if edition not in CENSUS_EDITIONS:
        raise ValueError(
            "faction census target names no supported retail edition: %r" % (edition,)
        )
    if target.get("faction") != faction or target.get("playerTemplate") != template:
        raise ValueError(
            f"faction census target is not {edition[0]} {edition[1]} {faction}"
        )
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
    (
        eva_resources,
        eva_events,
        eva_multisounds,
        eva_samples,
        eva_diagnostics,
    ) = _eva_audio_extension(catalog, faction, audio_slug, samples, existing_definitions)
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
    # Created heroes are fieldable by every faction and a created hero resolves
    # its voice through the mounted registries, so EVERY registry surface -
    # host pack and per-faction EVA overlay alike - carries the class voice
    # sets. The RotWK selection mounts no host registry for its factions (unit
    # voices ride the unit documents; only the overlays ship a registry), so
    # gating this on include_census_registry left every created hero mute.
    cah_diagnostics: dict[str, Any] = {}
    if True:
        (
            cah_resources,
            cah_events,
            cah_multisounds,
            cah_samples,
            cah_diagnostics,
        ) = _cah_voice_audio_extension(
            catalog, audio_slug, samples, {**events, **multisounds}
        )
        resources.extend(cah_resources)
        events.update(cah_events)
        multisounds.update(cah_multisounds)
        samples.update(cah_samples)
        roots.extend(
            sorted(
                (
                    identifier
                    for identifier in (*cah_events.keys(), *cah_multisounds.keys())
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
    eva_side_map = _eva_side_map_document(
        _eva_event_side_sounds(eva_document.source), eva_document.source
    )
    # The side map stays the authored retail projection - it is the oracle, not
    # a derived view - but the pack carries WHY a named sound will never play,
    # so a fail-closed announcement at runtime can be told apart from a bug.
    eva_side_map["unplayableRetailReferences"] = eva_diagnostics
    # ... and WHICH PART of the retail schema these semantics carry, so the
    # document cannot be read as a complete compilation of eva.ini.
    eva_side_map["semanticFieldCoverage"] = _eva_semantic_field_coverage(
        eva_document.source
    )
    # Retail announces unit/hero creation PER OBJECT, not via one generic id:
    # VoiceCreated = EVA:<event> (with ChildObject inherit) plus spawn-FX
    # EvaEventOwner on InitialSpawnFX for fortress heroes whose VoiceCreated
    # line was rehooked. The runtime's hero-created signal carries the created
    # object id and resolves it through this map. An object retail gives no
    # create hook stays unmapped and fails closed at runtime.
    eva_side_map["createdEvents"] = _eva_created_event_bindings(catalog)
    unresolved = _object(report.get("unresolved"), "faction census unresolved")
    return {
        "resources": resources,
        "runtime_data": {
            "data/audio_events.json": audio_manifest,
            "data/eva_events.json": eva_side_map,
        },
        "evaDiagnostics": eva_diagnostics,
        "cahVoiceDiagnostics": cah_diagnostics,
        "evaSemanticFieldCoverage": eva_side_map["semanticFieldCoverage"],
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
