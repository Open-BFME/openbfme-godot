"""One-command BFME2 faction spellbook discovery, conversion, and reporting.

This lane converts a faction's PlayerTemplate-bound spellbook (spell store
sciences, spell book powers, and their effect leaves) into the descriptor,
pack recipe, and runtime document artifacts shared with the other conversion
lanes.  It composes the faction census, the effective INI view, and the
spellbook compilers; every resolution step fails closed.
"""

from __future__ import annotations

from copy import deepcopy
from pathlib import Path
from typing import Any, Mapping

from .catalog import InstallCatalog
from .faction_census import census_playable_faction
from .faction_policy import (
    implicit_object_roots,
    music_roots,
    source_null_command_sets,
    source_null_mapped_image_textures,
)
from .playable_unit_compiler import prepare_playable_unit_compiler
from .playable_unit_import import FACTIONS, _source_documents
from .sage_string import MAX_STRING_BYTES, parse_string_catalog
from .spellbook_compiler import compile_spellbook_descriptor
from .spellbook_pack_compiler import (
    compile_spellbook_pack_recipe,
    compose_spellbook_runtime_document,
)
from .util import write_json_atomic


_REQUIRED_EXTRA_DOCUMENTS = (
    "data/ini/science.ini",
    "data/ini/specialpower.ini",
    "data/ini/objectcreationlist.ini",
    "data/ini/fxlist.ini",
    "data/ini/attributemodifier.ini",
    "data/ini/fxparticlesystem.ini",
)


def spellbook_source_documents(effective_root: Path) -> dict[str, bytes]:
    """Return the effective INI view required by the spellbook compilers."""

    documents = _source_documents(effective_root)
    for relative in _REQUIRED_EXTRA_DOCUMENTS:
        path = effective_root.joinpath(*relative.split("/"))
        if not path.is_file():
            raise FileNotFoundError(f"effective retail source is missing: {relative}")
        documents[relative] = path.read_bytes()
    return documents


def _requirement_ids(descriptor: Mapping[str, object], family: str) -> list[str]:
    requirements = descriptor.get("requirements")
    if not isinstance(requirements, Mapping):
        raise ValueError("spellbook descriptor requirements are invalid")
    values = requirements.get(family)
    if not isinstance(values, list) or any(
        not isinstance(item, str) or not item for item in values
    ):
        raise ValueError(f"spellbook descriptor {family} requirements are invalid")
    return list(values)


def _resolved_spellbook_media(
    graph: Mapping[str, object], descriptor: Mapping[str, object]
) -> tuple[dict[str, Mapping[str, object]], dict[str, list[str]]]:
    """Resolve spellbook button art and audio ids through the census leaves."""

    leaves = graph.get("resolvedLeaves")
    if not isinstance(leaves, Mapping):
        raise ValueError("faction graph has no resolved media leaves")
    mapped_rows = leaves.get("mappedImages")
    audio = leaves.get("audio")
    if not isinstance(mapped_rows, list) or not isinstance(audio, Mapping):
        raise ValueError("faction graph media closure is invalid")
    images_by_id = {
        str(row["id"]).casefold(): row
        for row in mapped_rows
        if isinstance(row, Mapping) and isinstance(row.get("id"), str)
    }
    images: dict[str, Mapping[str, object]] = {}
    for identifier in sorted(
        _requirement_ids(descriptor, "mappedImages"), key=str.casefold
    ):
        row = images_by_id.get(identifier.casefold())
        if row is None or not isinstance(row.get("compiledTextureVirtualPath"), str):
            raise ValueError(f"required spellbook mapped image is unresolved: {identifier}")
        images[identifier] = deepcopy(row)

    events = {
        str(row["id"]).casefold(): row
        for row in audio.get("events", [])
        if isinstance(row, Mapping) and isinstance(row.get("id"), str)
    }
    multisounds = {
        str(row["id"]).casefold(): row
        for row in audio.get("multisounds", [])
        if isinstance(row, Mapping) and isinstance(row.get("id"), str)
    }
    samples = {
        str(row["id"]).casefold(): str(row["virtualPath"])
        for row in audio.get("samplePaths", [])
        if isinstance(row, Mapping)
        and isinstance(row.get("id"), str)
        and isinstance(row.get("virtualPath"), str)
    }

    def resolve_audio(identifier: str, stack: tuple[str, ...] = ()) -> set[str]:
        key = identifier.casefold()
        if key in stack:
            raise ValueError(f"audio dependency cycle at {identifier}")
        if key in samples:
            return {samples[key]}
        row = events.get(key)
        child_field = "sounds"
        if row is None:
            row = multisounds.get(key)
            child_field = "subsounds"
        if row is None:
            raise ValueError(f"audio dependency is unresolved: {identifier}")
        result: set[str] = set()
        for child in row.get(child_field, []):
            if not isinstance(child, Mapping) or not isinstance(child.get("id"), str):
                raise ValueError(f"audio definition is invalid: {identifier}")
            result.update(resolve_audio(str(child["id"]), (*stack, key)))
        return result

    resolved_audio: dict[str, list[str]] = {}
    for identifier in sorted(_requirement_ids(descriptor, "audio"), key=str.casefold):
        resolved_audio[identifier] = sorted(resolve_audio(identifier), key=str.casefold)
    return images, resolved_audio


def _resolved_spellbook_strings(
    catalog: InstallCatalog, descriptor: Mapping[str, object]
) -> dict[str, str]:
    """Resolve spellbook label ids through the install string catalog."""

    string_entry = catalog.resolve_exact("data/lotr.str")
    if string_entry is None:
        raise ValueError("required string catalog is unresolved")
    string_source = catalog.open_archive_for(string_entry).read_entry(
        catalog.as_entry(string_entry), max_bytes=MAX_STRING_BYTES
    )
    string_catalog = parse_string_catalog(string_source, duplicate_policy="first-wins")
    resolved: dict[str, str] = {}
    for identifier in sorted(_requirement_ids(descriptor, "strings"), key=str.casefold):
        record = string_catalog.record(identifier)
        if record is None or not record.value:
            raise ValueError(f"required localized string is unresolved: {identifier}")
        resolved[identifier] = record.value
    return resolved


def _faction_spec(faction: str) -> tuple[str, str, str]:
    key = faction.casefold().strip()
    spec = next(
        (
            item
            for item in FACTIONS
            if key in {item[0], item[1].casefold(), item[2].casefold()}
        ),
        None,
    )
    if spec is None:
        raise ValueError(f"unsupported playable faction: {faction!r}")
    return spec


def compile_spellbook_lane(
    catalog: InstallCatalog, effective_root: Path, faction: str
) -> dict[str, Any]:
    """Compile graph, descriptor, pack recipe, and runtime for one spellbook."""

    spec = _faction_spec(faction)
    graph = census_playable_faction(
        catalog,
        player_template=spec[1],
        expected_side=spec[2],
        implicit_object_roots=implicit_object_roots(spec[1]),
        source_null_mapped_image_textures=source_null_mapped_image_textures(spec[1]),
        source_null_command_sets=source_null_command_sets(spec[1]),
        music_roots=music_roots(spec[1]),
    )
    documents = spellbook_source_documents(effective_root)
    prepared = prepare_playable_unit_compiler(documents)
    draft = compile_spellbook_descriptor(graph, documents, prepared=prepared)
    images, audio = _resolved_spellbook_media(graph, draft)
    strings = _resolved_spellbook_strings(catalog, draft)
    descriptor = compile_spellbook_descriptor(
        graph,
        documents,
        resolved_images=images,
        resolved_audio=audio,
        resolved_strings=strings,
        prepared=prepared,
    )
    recipe = compile_spellbook_pack_recipe(descriptor)
    runtime = compose_spellbook_runtime_document(descriptor, recipe)
    return {
        "graph": graph,
        "descriptor": descriptor,
        "recipe": recipe,
        "runtime": runtime,
    }


def summarize_spellbook_lane(result: Mapping[str, Any]) -> dict[str, Any]:
    """Return bounded counts describing one compiled spellbook lane result."""

    descriptor = result["descriptor"]
    recipe = result["recipe"]
    runtime = result["runtime"]
    leaves = descriptor["leaves"]
    sciences = descriptor["sciences"]
    return {
        "target": dict(descriptor["target"]),
        "spellBookObjectId": descriptor["spellBook"]["objectId"],
        "descriptorSha256": descriptor["descriptorSha256"],
        "recipeSha256": recipe["recipeSha256"],
        "runtimeSha256": runtime["runtimeSha256"],
        "scienceCount": len(sciences),
        "purchasableScienceCount": sum(1 for row in sciences if "purchase" in row),
        "powerCount": len(descriptor["powers"]),
        "leafCounts": {
            family: len(leaves[family])
            for family in (
                "objectCreationLists",
                "fxLists",
                "weapons",
                "attributeModifiers",
                "upgrades",
                "objects",
                "particles",
            )
        },
        "audioBindingCount": len(recipe["runtimeRegistration"]["audioBindings"]),
        "imageBindingCount": len(recipe["runtimeRegistration"]["imageBindings"]),
        "stringBindingCount": len(recipe["runtimeRegistration"]["stringBindings"]),
        "resourceCount": len(recipe["resources"]),
        "limitations": list(descriptor["limitations"]),
    }


def import_spellbook(
    catalog: InstallCatalog,
    effective_root: Path,
    faction: str,
    *,
    report_root: Path,
) -> dict[str, Any]:
    """Convert one faction spellbook and persist the artifacts as JSON reports."""

    result = compile_spellbook_lane(catalog, effective_root, faction)
    write_json_atomic(report_root / "descriptor.json", result["descriptor"])
    write_json_atomic(report_root / "pack-recipe.json", result["recipe"])
    write_json_atomic(report_root / "runtime.json", result["runtime"])
    summary = summarize_spellbook_lane(result)
    write_json_atomic(report_root / "summary.json", summary)
    return summary


__all__ = [
    "compile_spellbook_lane",
    "import_spellbook",
    "spellbook_source_documents",
    "summarize_spellbook_lane",
]
