"""One-command BFME2 playable-unit discovery, conversion, and publication."""

from __future__ import annotations

from copy import deepcopy
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
from pathlib import PurePosixPath
import subprocess
import sys
from typing import Any, Iterable, Mapping

from .catalog import InstallCatalog
from .faction_census import (
    MUSIC_PATH,
    SOUND_EFFECTS_PATH,
    VOICE_PATH,
    _mapped_image_documents,
    _read_document,
    census_playable_faction,
)
from .mapped_image import (
    resolve_mapped_image_texture_paths_partial,
    resolve_mapped_images_partial,
)
from .sage_audio import (
    parse_sage_audio_definitions,
    resolve_audio_sample_paths_partial,
    resolve_sage_audio_closure,
)
from .faction_policy import (
    implicit_object_roots,
    music_roots,
    source_null_command_sets,
    source_null_mapped_image_textures,
)
from .pipeline import ImportPipeline, audit_pack, bundle_digest
from .playable_unit_compiler import (
    PlayableUnitCompilerInputs,
    PlayableUnitCompilerError,
    compile_playable_unit_descriptor,
    validate_playable_unit_descriptor,
)
from .playable_unit_pack_compiler import compile_playable_unit_pack_recipe
from .profile import ImportProfile, resolve_profile
from .publish_gate import enforce_playable_unit_gate
from .retail_visual_closure import build_retail_visual_closure
from .sage_string import MAX_STRING_BYTES, parse_string_catalog
from .util import read_json, write_json_atomic


FACTIONS = (
    ("men", "FactionMen", "Men"),
    ("elves", "FactionElves", "Elves"),
    ("dwarves", "FactionDwarves", "Dwarves"),
    ("isengard", "FactionIsengard", "Isengard"),
    ("mordor", "FactionMordor", "Mordor"),
    ("wild", "FactionWild", "Wild"),
)
# RotWK 2.01 expansion factions arrive through data-driven discovery
# (resolve_playable_faction), but identity admission stays closed: only the
# (short, PlayerTemplate, Side) pairs pinned here are valid downstream — the
# same shape the BFME2 table above proves for its six factions.
ROTWK_FACTIONS = (
    ("angmar", "FactionAngmar", "Angmar"),
)
_REQUIRED_DOCUMENTS = (
    "data/ini/commandset.ini",
    "data/ini/commandbutton.ini",
    "data/ini/gamedata.ini",
    "data/ini/playertemplate.ini",
    "data/ini/locomotor.ini",
    "data/ini/weapon.ini",
    # Weapon FireFX/ProjectileDetonationFX -> FXList -> Sound is the retail
    # weapon audio chain; without fxlist.ini every melee swing falls back to
    # a hardcoded class clip.
    "data/ini/fxlist.ini",
    "data/ini/armor.ini",
    "data/ini/upgrade.ini",
    # ExperienceLevel AttributeModifiers are live unit progression inputs.
    # Omitting this document made every naval combat ship fail recipe import at
    # ShipsLevel2 even though the catalog-backed descriptor compiled it.
    "data/ini/attributemodifier.ini",
    # Hero ability level gates chain through authored ExperienceLevel grants.
    "data/ini/experiencelevels.ini",
)


def _canonical_bytes(value: object) -> bytes:
    return (
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    ).encode("utf-8")


def _slug(value: str) -> str:
    result = "".join(
        character if character.isascii() and character.isalnum() else "-"
        for character in value.casefold()
    )
    result = "-".join(part for part in result.split("-") if part)
    if not result or len(result) > 80:
        raise ValueError("playable-unit object id has no safe bounded slug")
    return result


def _source_documents(effective_root: Path) -> dict[str, bytes]:
    object_root = effective_root / "data" / "ini" / "object"
    if not object_root.is_dir():
        raise FileNotFoundError("effective retail Object tree is missing")
    documents = {
        path.relative_to(effective_root).as_posix(): path.read_bytes()
        for path in object_root.rglob("*")
        if path.is_file() and path.suffix.casefold() in {".ini", ".inc"}
    }
    for relative in _REQUIRED_DOCUMENTS:
        path = effective_root.joinpath(*relative.split("/"))
        if not path.is_file():
            raise FileNotFoundError(f"effective retail source is missing: {relative}")
        documents[relative] = path.read_bytes()
    return documents


def _faction_spec(value: str) -> tuple[str, str, str] | None:
    key = value.casefold().strip()
    if key == "auto":
        return None
    for spec in FACTIONS:
        if key in {spec[0], spec[1].casefold(), spec[2].casefold()}:
            return spec
    raise ValueError(f"unsupported playable faction: {value!r}")


def _select_faction_graph(
    catalog: InstallCatalog,
    documents: Mapping[str, bytes],
    object_id: str,
    faction: str,
    *,
    game: str,
    admitted_producer_ids: tuple[str, ...] = (),
    scenario_admission_role: str | None = None,
) -> tuple[dict[str, Any], dict[str, object]]:
    requested = _faction_spec(faction)
    candidates = (requested,) if requested is not None else FACTIONS
    matches: list[tuple[dict[str, Any], dict[str, object]]] = []
    for _short, template, side in candidates:
        graph = census_playable_faction(
            catalog,
            player_template=template,
            expected_side=side,
            implicit_object_roots=implicit_object_roots(template),
            source_null_mapped_image_textures=source_null_mapped_image_textures(
                template
            ),
            source_null_command_sets=source_null_command_sets(template),
            music_roots=music_roots(template),
        )
        if admitted_producer_ids:
            definitions = graph.get("definitions")
            object_rows = (
                definitions.get("objects") if isinstance(definitions, dict) else None
            )
            if not isinstance(object_rows, list):
                raise ValueError("faction graph Object definitions are invalid")
            known = {
                str(row.get("id", "")).casefold()
                for row in object_rows
                if isinstance(row, Mapping)
            }
            for producer_id in admitted_producer_ids:
                if producer_id.casefold() not in known:
                    # This only expands the reachability filter. The descriptor
                    # compiler still resolves the producer Object, CommandSet,
                    # UNIT_BUILD button, transition and prerequisites from the
                    # authored documents and fails if any edge is invented.
                    object_rows.append({"id": producer_id, "edges": []})
                    known.add(producer_id.casefold())
        try:
            descriptor = compile_playable_unit_descriptor(
                object_id,
                documents,
                faction_graph=None if scenario_admission_role is not None else graph,
                game=game,
                scenario_admission_role=scenario_admission_role,
            )
        except PlayableUnitCompilerError:
            if requested is not None:
                raise
            continue
        matches.append((graph, descriptor))
    if not matches:
        raise ValueError(f"playable unit is not command-reachable: {object_id}")
    if len(matches) > 1:
        factions = ", ".join(
            str(item[0]["target"]["playerTemplate"]) for item in matches
        )
        raise ValueError(
            f"playable unit faction is ambiguous ({factions}); pass --faction"
        )
    return matches[0]


def _required_image_ids(descriptor: Mapping[str, object]) -> set[str]:
    presentation = descriptor["presentation"]
    ui = presentation["ui"]
    result = {str(value) for value in ui["portraitImageIds"] if str(value)}
    for command in ui["commands"]:
        for value in command["fields"].get("ButtonImage", []):
            if str(value):
                result.add(str(value))
    for ability in descriptor.get("abilities", []):
        if not isinstance(ability, Mapping):
            continue
        button = ability.get("button", {})
        if not isinstance(button, Mapping):
            continue
        for value in button.get("iconIds", []):
            if str(value):
                result.add(str(value))
    return result


def _required_audio_ids(descriptor: Mapping[str, object]) -> set[str]:
    routes = descriptor["presentation"]["audioRoutes"]
    result = {
        str(row["id"])
        for owner in routes.values()
        for rows in owner.values()
        for row in rows
    }
    for command in descriptor["presentation"]["ui"]["commands"]:
        result.update(str(row["id"]) for row in command.get("audioRoutes", []))
    return result


def _required_string_ids(descriptor: Mapping[str, object]) -> set[str]:
    from .playable_unit_compiler import command_string_ids

    result: set[str] = set()
    ui = descriptor["presentation"]["ui"]
    for command in list(ui.get("commands", [])) + list(ui.get("selectionCommands", [])):
        fields = command.get("fields", {})
        for field in ("TextLabel", "DescriptLabel"):
            result.update(command_string_ids(*fields.get(field, [])))
    for ability in descriptor.get("abilities", []):
        if not isinstance(ability, Mapping):
            continue
        button = ability.get("button", {})
        if not isinstance(button, Mapping):
            continue
        for field in ("labelIds", "tooltipIds"):
            result.update(str(value) for value in button.get(field, []) if str(value))
    return result


def _source_null_mapped_image_ids(graph: Mapping[str, object]) -> frozenset[str]:
    """Casefolded SelectPortrait ids retail authors without a MappedImage body."""

    dependencies = graph.get("dependencies")
    if not isinstance(dependencies, Mapping):
        return frozenset()
    rows = dependencies.get("sourceNullMappedImages")
    if not isinstance(rows, list):
        return frozenset()
    return frozenset(
        str(item).casefold() for item in rows if isinstance(item, str) and item
    )


def _rebind_compiled_texture_path(
    row: Mapping[str, object],
    *,
    effective_root: Path | None,
) -> Mapping[str, object] | None:
    """Fill a census leaf whose DDS path is on disk but absent from the catalog.

    Faction census resolves MappedImage textures against the install catalog
    entry list. Layered RotWK extracts can materialize
    ``art/compiledtextures/<ab>/<stem>.dds`` under the effective tree while an
    older or incomplete catalog omits that leaf, leaving
    ``compiledTextureResolution: missing``. Convert rebinds against the sealed
    effective tree with the same authored-extension-then-DDS convention used by
    ``mapped_image.resolve_mapped_image_texture_paths_partial``.
    """

    if isinstance(row.get("compiledTextureVirtualPath"), str) and row[
        "compiledTextureVirtualPath"
    ]:
        return row
    texture = row.get("texture")
    if not isinstance(texture, str) or not texture or effective_root is None:
        return None
    basename = texture.replace("\\", "/").rsplit("/", 1)[-1]
    stem = basename.rsplit(".", 1)[0]
    if len(stem) < 2:
        return None
    prefix = stem[:2].casefold()
    candidates = (
        f"art/compiledtextures/{prefix}/{basename}",
        f"art/compiledtextures/{prefix}/{stem}.dds",
    )
    for relative in candidates:
        physical = effective_root.joinpath(*relative.split("/"))
        if physical.is_file():
            rebound = deepcopy(dict(row))
            rebound["compiledTextureVirtualPath"] = relative
            rebound.pop("compiledTextureResolution", None)
            return rebound
    # Casefold directory/file search for platforms with mixed archive casing.
    texture_root = effective_root / "art" / "compiledtextures" / prefix
    if texture_root.is_dir():
        wanted = {basename.casefold(), f"{stem}.dds".casefold()}
        matches = sorted(
            (
                child
                for child in texture_root.iterdir()
                if child.is_file() and child.name.casefold() in wanted
            ),
            key=lambda item: item.name.casefold(),
        )
        if len(matches) == 1:
            rebound = deepcopy(dict(row))
            rebound["compiledTextureVirtualPath"] = (
                f"art/compiledtextures/{prefix}/{matches[0].name}"
            )
            rebound.pop("compiledTextureResolution", None)
            return rebound
    return None


def _resolved_media(
    graph: Mapping[str, object],
    descriptor: Mapping[str, object],
    *,
    effective_root: Path | None = None,
    catalog: InstallCatalog | None = None,
) -> tuple[dict[str, Mapping[str, object]], dict[str, list[str]]]:
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
    required_image_ids = _required_image_ids(descriptor)
    if catalog is not None:
        # A unit admitted outside the original faction census can introduce
        # new authored CommandButton images (the neutral ShipWright is the
        # retail example). Resolve that exact delta from the same effective
        # catalog instead of treating an intentionally narrow graph leaf set
        # as proof that retail omitted the image.
        missing_from_graph = sorted(
            (
                identifier
                for identifier in required_image_ids
                if identifier.casefold() not in images_by_id
            ),
            key=str.casefold,
        )
        if missing_from_graph:
            mapped_documents = _mapped_image_documents(catalog)
            resolution = resolve_mapped_images_partial(
                (document.source for document in mapped_documents),
                missing_from_graph,
            )
            texture_paths, _missing_textures = (
                resolve_mapped_image_texture_paths_partial(
                    resolution.records,
                    (entry.name for entry in catalog.entries),
                )
            )
            texture_paths_by_key = {
                key.casefold(): value for key, value in texture_paths.items()
            }
            for record in resolution.records:
                row = record.neutral()
                compiled = texture_paths_by_key.get(record.texture.casefold())
                if compiled is not None:
                    row["compiledTextureVirtualPath"] = compiled
                images_by_id[record.id.casefold()] = row
    source_null_images = _source_null_mapped_image_ids(graph)
    images: dict[str, Mapping[str, object]] = {}
    for identifier in sorted(required_image_ids, key=str.casefold):
        key = identifier.casefold()
        if key in source_null_images:
            # Retail SelectPortrait with no MappedImage definition — census
            # already recorded the source-null; convert must not invent art.
            continue
        row = images_by_id.get(key)
        if row is not None and not isinstance(
            row.get("compiledTextureVirtualPath"), str
        ):
            row = _rebind_compiled_texture_path(row, effective_root=effective_root)
        if row is None or not isinstance(row.get("compiledTextureVirtualPath"), str):
            raise ValueError(f"required mapped image is unresolved: {identifier}")
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
    if catalog is not None:
        missing_audio_roots = sorted(
            (
                identifier
                for identifier in _required_audio_ids(descriptor)
                if identifier.casefold() not in events
                and identifier.casefold() not in multisounds
                and identifier.casefold() not in samples
            ),
            key=str.casefold,
        )
        if missing_audio_roots:
            definitions = parse_sage_audio_definitions(
                _read_document(catalog, SOUND_EFFECTS_PATH).source
                + b"\n"
                + _read_document(catalog, VOICE_PATH).source
                + b"\n"
                + _read_document(catalog, MUSIC_PATH).source
            )
            closure = resolve_sage_audio_closure(definitions, missing_audio_roots)
            paths, _missing, _ambiguous = resolve_audio_sample_paths_partial(
                closure.sample_ids,
                (entry.name for entry in catalog.entries),
            )
            neutral = closure.neutral()
            events.update(
                {
                    str(row["id"]).casefold(): row
                    for row in neutral["events"]
                }
            )
            multisounds.update(
                {
                    str(row["id"]).casefold(): row
                    for row in neutral["multisounds"]
                }
            )
            samples.update(
                {
                    identifier.casefold(): path
                    for identifier, path in paths.items()
                }
            )
    # Census-recorded retail-absent sample files (RotWK 2.01 references nine
    # Angmar voice samples it never shipped). The engine skips a sound whose
    # sample file is missing, so a census-proven absence contributes no path;
    # anything NOT proven absent by the census still fails closed below.
    source_null_samples = _census_missing_audio_samples(graph)

    def resolve_audio(identifier: str, stack: tuple[str, ...] = ()) -> set[str]:
        key = identifier.casefold()
        if key in stack:
            raise ValueError(f"audio dependency cycle at {identifier}")
        if key in samples:
            return {samples[key]}
        if key in source_null_samples:
            return set()
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
    for identifier in sorted(_required_audio_ids(descriptor), key=str.casefold):
        paths = sorted(resolve_audio(identifier), key=str.casefold)
        resolved_audio[identifier] = paths

    return images, resolved_audio


def _census_missing_audio_samples(graph: Mapping[str, object]) -> frozenset[str]:
    """Casefolded census-recorded retail-absent audio sample identifiers."""

    unresolved = graph.get("unresolved")
    if not isinstance(unresolved, Mapping):
        return frozenset()
    rows = unresolved.get("missingAudioSamples")
    if not isinstance(rows, list):
        return frozenset()
    return frozenset(
        str(item).casefold() for item in rows if isinstance(item, str) and item
    )


def _resolved_strings(
    catalog: InstallCatalog,
    descriptor: Mapping[str, object],
    *,
    graph: Mapping[str, object] | None = None,
) -> dict[str, str]:
    string_entry = catalog.resolve_exact("data/lotr.str")
    if string_entry is None:
        raise ValueError("required string catalog is unresolved")
    string_source = catalog.open_archive_for(string_entry).read_entry(
        catalog.as_entry(string_entry), max_bytes=MAX_STRING_BYTES
    )
    # RotWK 2.01 retail ships lotr.str with a bounded lexical typo (a label
    # containing a space: "CONTROLBAR:Tooltipbuild AngmarTrollSling"); mirror
    # the census parse and record malformed rows as evidence instead of
    # failing the whole catalog. Every REQUIRED identifier below still fails
    # closed individually when it cannot be resolved.
    string_catalog = parse_string_catalog(
        string_source, duplicate_policy="first-wins", strict=False
    )
    resolved_strings: dict[str, str] = {}
    source_null_ids = {
        str(value).casefold()
        for value in (graph or {}).get("layeredSourceNullTextIds", [])
        if isinstance(value, str) and value
    }
    layered_authority = (
        (graph or {}).get("layeredDocumentAuthority")
        == "layered-effective-assets"
    )
    for identifier in sorted(_required_string_ids(descriptor), key=str.casefold):
        record = string_catalog.record(identifier)
        if record is not None and not record.value:
            # PRESENT-but-EMPTY is never a retail-null exemption. Retail
            # declining to localize an id shows up as NO ROW; an existing row
            # with no value means our read of the table lost the text. Folding
            # the two together laundered real data holes into sanctioned
            # exemptions that could never be detected again.
            raise ValueError(
                "required localized string is present but empty in "
                f"data/lotr.str: {identifier}"
            )
        if record is None:
            # TIER POLICY (must match game/tests/hud_string_completeness_runner.gd):
            # an id is "retail-absent" when the LAYERED effective string table
            # has no row for it -- not when it is missing from every mounted
            # tier. Retail's own file-level layering replaces BFME2's
            # data/lotr.str wholesale with RotWK's, so a row that exists only
            # in the BFME2 table is unreachable at runtime in a layered
            # install. Only layered authority establishes that absence, so
            # previously recorded evidence alone may not sanction a hole: it
            # may have been recorded against a different tier's table.
            if layered_authority:
                if isinstance(graph, dict):
                    missing = graph.setdefault("layeredSourceNullTextIds", [])
                    if isinstance(missing, list) and identifier not in missing:
                        missing.append(identifier)
                        missing.sort(key=str.casefold)
                continue
            if identifier.casefold() in source_null_ids:
                raise ValueError(
                    "retail-null string evidence is not tier-authoritative "
                    f"(no layered document authority): {identifier}"
                )
            raise ValueError(f"required localized string is unresolved: {identifier}")
        resolved_strings[identifier] = record.value
    return resolved_strings


def compile_unit_recipe(
    catalog: InstallCatalog,
    effective_root: Path,
    object_id: str,
    *,
    game: str,
    faction: str = "auto",
    admitted_producer_ids: tuple[str, ...] = (),
    scenario_admission_role: str | None = None,
) -> tuple[dict[str, Any], dict[str, object], dict[str, object], dict[str, object]]:
    """Compile graph, descriptor, visual closure, and pack recipe for one unit."""

    documents = _source_documents(effective_root)
    graph, draft = _select_faction_graph(
        catalog,
        documents,
        object_id,
        faction,
        game=game,
        admitted_producer_ids=admitted_producer_ids,
        scenario_admission_role=scenario_admission_role,
    )
    if effective_root.name.casefold() == "layered-effective-assets":
        graph["layeredDocumentAuthority"] = "layered-effective-assets"
    images, audio = _resolved_media(
        graph,
        draft,
        effective_root=effective_root,
        catalog=catalog,
    )
    strings = _resolved_strings(catalog, draft, graph=graph)
    descriptor = compile_playable_unit_descriptor(
        object_id,
        documents,
        faction_graph=None if scenario_admission_role is not None else graph,
        resolved_images=images,
        resolved_audio=audio,
        resolved_strings=strings,
        game=game,
        scenario_admission_role=scenario_admission_role,
    )
    composition = descriptor["composition"]
    targets = [str(composition["containerObjectId"])]
    targets.extend(str(row["objectId"]) for row in composition["members"])
    closure = build_retail_visual_closure(
        effective_root, sorted(set(targets), key=str.casefold)
    )
    recipe = compile_playable_unit_pack_recipe(descriptor, closure)
    return graph, descriptor, closure, recipe


def _compact_digest(value: object) -> str:
    return hashlib.sha256(
        json.dumps(
            value,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
        ).encode("utf-8")
    ).hexdigest()


def _standalone_mapped_image_delta(
    effective_root: Path, image_ids: Iterable[str]
) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    """Resolve retail's implicit full-texture UI images.

    A few scenario objects name a SelectPortrait whose id is not declared by a
    ``MappedImage`` block.  Retail nevertheless supplies one compiled DDS with
    the same basename.  That is an authored full-frame image, not an atlas crop;
    accept it only when the conventional two-letter directory contains exactly
    one case-insensitive basename match and the DDS header proves its bounds.
    """

    rows: list[dict[str, object]] = []
    receipts: list[dict[str, object]] = []
    for image_id in sorted(set(image_ids), key=str.casefold):
        if not image_id or any(character in image_id for character in "/\\"):
            continue
        prefix = image_id[:2].casefold()
        directory = effective_root / "art" / "compiledtextures" / prefix
        if not directory.is_dir():
            continue
        wanted = f"{image_id}.dds".casefold()
        matches = sorted(
            (
                child
                for child in directory.iterdir()
                if child.is_file() and child.name.casefold() == wanted
            ),
            key=lambda child: (child.name.casefold(), child.name),
        )
        if len(matches) != 1:
            continue
        physical = matches[0]
        with physical.open("rb") as stream:
            header = stream.read(128)
        if (
            len(header) < 128
            or header[:4] != b"DDS "
            or int.from_bytes(header[4:8], "little") != 124
        ):
            raise ValueError(
                f"standalone mapped image is not a bounded DDS: {image_id}"
            )
        height = int.from_bytes(header[12:16], "little")
        width = int.from_bytes(header[16:20], "little")
        if not 1 <= width <= 32_768 or not 1 <= height <= 32_768:
            raise ValueError(
                f"standalone mapped image DDS dimensions are invalid: {image_id}"
            )
        relative = physical.relative_to(effective_root).as_posix()
        digest = hashlib.sha256()
        with physical.open("rb") as stream:
            while block := stream.read(1024 * 1024):
                digest.update(block)
        rows.append(
            {
                "id": image_id,
                "texture": physical.name,
                "textureWidth": width,
                "textureHeight": height,
                "coords": {"left": 0, "top": 0, "right": width, "bottom": height},
                "compiledTextureVirtualPath": relative,
            }
        )
        receipts.append(
            {
                "id": image_id,
                "resolution": "exact-standalone-full-texture",
                "compiledTextureVirtualPath": relative,
                "sha256": digest.hexdigest(),
                "byteLength": physical.stat().st_size,
                "width": width,
                "height": height,
            }
        )
    return rows, receipts


def _scenario_source_resolution(
    catalog: InstallCatalog,
    descriptor: Mapping[str, object],
    *,
    effective_root: Path,
) -> tuple[
    dict[str, Mapping[str, object]],
    dict[str, list[str]],
    dict[str, str],
    dict[str, object],
]:
    """Resolve a scenario unit's exact presentation leaves without a faction graph."""

    entry_paths = [str(entry.name) for entry in catalog.entries]
    entry_identity = _compact_digest(
        sorted(entry_paths, key=lambda value: (value.casefold(), value))
    )

    required_images = sorted(_required_image_ids(descriptor), key=str.casefold)
    mapped_documents = _mapped_image_documents(catalog)
    mapped_rows: list[dict[str, object]] = []
    scenario_media_delta: list[dict[str, object]] = []
    if required_images:
        resolution = resolve_mapped_images_partial(
            (document.source for document in mapped_documents), required_images
        )
        if resolution.ambiguous_ids:
            raise ValueError(
                f"required mapped image is ambiguous: {resolution.ambiguous_ids[0]}"
            )
        missing_ids = list(resolution.missing_ids)
        delta_rows, standalone_receipts = _standalone_mapped_image_delta(
            effective_root, missing_ids
        )
        standalone_keys = {str(row["id"]).casefold() for row in delta_rows}
        missing_ids = [
            identifier
            for identifier in missing_ids
            if identifier.casefold() not in standalone_keys
        ]
        alias_requests = {
            identifier[:-3]: identifier
            for identifier in missing_ids
            if len(identifier) > 3 and identifier.casefold().endswith("new")
        }
        alias_rows: list[dict[str, object]] = []
        alias_receipts: list[dict[str, object]] = []
        if alias_requests:
            alias_resolution = resolve_mapped_images_partial(
                (document.source for document in mapped_documents),
                alias_requests,
            )
            if alias_resolution.ambiguous_ids:
                raise ValueError(
                    "required mapped image alias is ambiguous: "
                    f"{alias_resolution.ambiguous_ids[0]}"
                )
            alias_texture_paths, alias_missing_textures = (
                resolve_mapped_image_texture_paths_partial(
                    alias_resolution.records, entry_paths
                )
            )
            if alias_missing_textures:
                raise ValueError(
                    "required mapped image texture is unresolved: "
                    f"{alias_missing_textures[0]}"
                )
            alias_paths_by_key = {
                key.casefold(): value for key, value in alias_texture_paths.items()
            }
            resolved_alias_requests: set[str] = set()
            for record in alias_resolution.records:
                requested_id = alias_requests.get(record.id)
                if requested_id is None:
                    requested_id = next(
                        (
                            value
                            for key, value in alias_requests.items()
                            if key.casefold() == record.id.casefold()
                        ),
                        None,
                    )
                texture_path = alias_paths_by_key.get(record.texture.casefold())
                if requested_id is None or texture_path is None:
                    continue
                row = record.neutral()
                row["id"] = requested_id
                row["compiledTextureVirtualPath"] = texture_path
                alias_rows.append(row)
                resolved_alias_requests.add(requested_id.casefold())
                alias_receipts.append(
                    {
                        "id": requested_id,
                        "resolution": "exact-new-suffix-mapped-image-alias",
                        "sourceMappedImageId": record.id,
                        "compiledTextureVirtualPath": texture_path,
                        "textureWidth": record.texture_width,
                        "textureHeight": record.texture_height,
                        "coords": row["coords"],
                    }
                )
            missing_ids = [
                identifier
                for identifier in missing_ids
                if identifier.casefold() not in resolved_alias_requests
            ]
        # Keep both independently-authored delta forms in stable request order.
        scenario_media_delta = sorted(
            [*alias_receipts, *standalone_receipts],
            key=lambda row: str(row["id"]).casefold(),
        )
        if missing_ids:
            raise ValueError(
                f"required mapped image is unresolved: {missing_ids[0]}"
            )
        texture_paths, missing_textures = resolve_mapped_image_texture_paths_partial(
            resolution.records, entry_paths
        )
        if missing_textures:
            raise ValueError(
                "required mapped image texture is unresolved: "
                f"{missing_textures[0]}"
            )
        texture_paths_by_key = {
            key.casefold(): value for key, value in texture_paths.items()
        }
        for record in resolution.records:
            row = record.neutral()
            texture_path = texture_paths_by_key.get(record.texture.casefold())
            if texture_path is None:
                raise ValueError(
                    f"required mapped image texture is unresolved: {record.texture}"
                )
            row["compiledTextureVirtualPath"] = texture_path
            mapped_rows.append(row)
        mapped_rows.extend(alias_rows)
        mapped_rows.extend(delta_rows)

    required_audio = sorted(_required_audio_ids(descriptor), key=str.casefold)
    audio_neutral: dict[str, object] = {
        "events": [],
        "multisounds": [],
        "samplePaths": [],
    }
    audio_source_paths = (SOUND_EFFECTS_PATH, VOICE_PATH, MUSIC_PATH)
    if required_audio:
        definitions = parse_sage_audio_definitions(
            b"\n".join(_read_document(catalog, path).source for path in audio_source_paths)
        )
        audio_closure = resolve_sage_audio_closure(definitions, required_audio)
        sample_paths, missing_samples, ambiguous_samples = (
            resolve_audio_sample_paths_partial(audio_closure.sample_ids, entry_paths)
        )
        if missing_samples:
            raise ValueError(f"required audio sample is unresolved: {missing_samples[0]}")
        if ambiguous_samples:
            raise ValueError(f"required audio sample is ambiguous: {ambiguous_samples[0]}")
        audio_neutral = audio_closure.neutral()
        audio_neutral["samplePaths"] = [
            {"id": identifier, "virtualPath": sample_paths[identifier]}
            for identifier in sorted(sample_paths, key=str.casefold)
        ]

    media_graph = {
        "resolvedLeaves": {
            "mappedImages": mapped_rows,
            "audio": audio_neutral,
        }
    }
    images, audio = _resolved_media(media_graph, descriptor)
    strings = _resolved_strings(catalog, descriptor, graph={})
    required_strings = sorted(_required_string_ids(descriptor), key=str.casefold)
    receipt: dict[str, object] = {
        "schema": "openbfme.scenario-unit-source-resolution",
        "schemaVersion": 0,
        "authority": "effective-retail-corpus",
        "effectiveAssetsRootName": effective_root.name,
        "catalogEntryCount": len(entry_paths),
        "catalogEntryPathsSha256": entry_identity,
        "mappedImageDocuments": [
            {
                "virtualPath": document.virtual_path,
                "sha256": document.sha256,
                "size": document.size,
            }
            for document in mapped_documents
        ],
        "scenarioMediaDelta": scenario_media_delta,
        "audioDefinitionDocuments": list(audio_source_paths),
        "stringCatalogVirtualPath": "data/lotr.str",
        "requiredImageIds": required_images,
        "resolvedImageIds": sorted(images, key=str.casefold),
        "requiredAudioIds": required_audio,
        "resolvedAudioIds": sorted(audio, key=str.casefold),
        "requiredStringIds": required_strings,
        "resolvedStringIds": sorted(strings, key=str.casefold),
    }
    receipt["receiptSha256"] = _compact_digest(receipt)
    return images, audio, strings, receipt


def compile_scenario_unit_recipe(
    catalog: InstallCatalog,
    effective_root: Path,
    target_or_descriptor: str | Mapping[str, object],
    *,
    game: str,
    scenario_admission: Mapping[str, object] | None = None,
    prebuilt_visual_closure: Mapping[str, object] | None = None,
    prepared: PlayableUnitCompilerInputs | None = None,
    source_documents: Mapping[str, bytes] | None = None,
) -> tuple[dict[str, object], dict[str, object], dict[str, object]]:
    """Compile one non-buildable unit recipe directly from the effective corpus.

    This entry point deliberately has no faction census or synthetic producer.
    A caller must supply either a catalog descriptor that already carries a
    scenario admission contract, or an Object id plus the explicit role and
    spawn surfaces that admit it.
    """

    if game not in {"bfme2", "rotwk"}:
        raise ValueError(f"unsupported scenario unit game {game!r}")

    effective_root = Path(effective_root).expanduser().resolve(strict=True)
    if source_documents is None:
        documents = (
            prepared.documents
            if prepared is not None
            else _source_documents(effective_root)
        )
    else:
        documents = source_documents
    if prepared is not None and prepared.documents is not documents:
        raise ValueError(
            "prepared compiler inputs belong to a different document mapping"
        )
    if isinstance(target_or_descriptor, Mapping):
        if scenario_admission is not None:
            raise ValueError(
                "scenario admission must not be repeated for a descriptor input"
            )
        source_descriptor = target_or_descriptor
        validate_playable_unit_descriptor(source_descriptor)
        admitted = source_descriptor.get("scenarioAdmission")
        if not isinstance(admitted, Mapping):
            raise ValueError("scenario unit descriptor has no scenario admission")
        object_id = source_descriptor.get("objectId")
        if not isinstance(object_id, str) or not object_id:
            raise ValueError("scenario unit descriptor identity is invalid")
        if source_descriptor.get("production") != []:
            raise ValueError("scenario unit descriptor has authored production")
        scenario_admission = {
            "role": admitted.get("role"),
            "surfaces": deepcopy(admitted.get("surfaces")),
        }
    elif isinstance(target_or_descriptor, str) and target_or_descriptor:
        object_id = target_or_descriptor
        if scenario_admission is None:
            raise ValueError("scenario unit target requires explicit admission")
    else:
        raise TypeError("scenario unit target must be an Object id or descriptor")

    draft = compile_playable_unit_descriptor(
        object_id,
        documents,
        faction_graph=None,
        prepared=prepared,
        game=game,
        scenario_admission=scenario_admission,
    )
    if draft.get("objectId") != object_id or draft.get("production") != []:
        raise ValueError("scenario unit compilation changed identity or production")
    images, audio, strings, receipt = _scenario_source_resolution(
        catalog, draft, effective_root=effective_root
    )
    descriptor = compile_playable_unit_descriptor(
        object_id,
        documents,
        faction_graph=None,
        prepared=prepared,
        resolved_images=images,
        resolved_audio=audio,
        resolved_strings=strings,
        scenario_admission=scenario_admission,
        game=game,
    )
    validate_playable_unit_descriptor(descriptor)
    if descriptor.get("objectId") != object_id or descriptor.get("production") != []:
        raise ValueError("scenario unit recipe changed identity or production")
    targets = _scenario_visual_targets(descriptor)
    if prebuilt_visual_closure is None:
        closure = build_retail_visual_closure(
            effective_root,
            targets,
            catalog=catalog,
        )
    else:
        closure = deepcopy(dict(prebuilt_visual_closure))
        closure_targets = closure.get("targets")
        if not isinstance(closure_targets, list):
            raise ValueError("prebuilt scenario visual closure has no target registry")
        requested = []
        for row in closure_targets:
            if not isinstance(row, Mapping) or not isinstance(
                row.get("requestedName"), str
            ):
                raise ValueError(
                    "prebuilt scenario visual closure target registry is invalid"
                )
            requested.append(str(row["requestedName"]))
        if requested != targets:
            raise ValueError(
                "prebuilt scenario visual closure targets do not match descriptor"
            )
    unsigned_closure = dict(closure)
    unsigned_closure.pop("aggregateSha256", None)
    unsigned_closure["scenarioSourceResolution"] = receipt
    unsigned_closure["aggregateSha256"] = _compact_digest(unsigned_closure)
    closure = unsigned_closure
    recipe = compile_playable_unit_pack_recipe(descriptor, closure)
    return descriptor, closure, recipe


def _scenario_visual_targets(descriptor: Mapping[str, object]) -> list[str]:
    """Return the exact canonical visual target list used by one recipe."""

    composition = descriptor.get("composition")
    if not isinstance(composition, Mapping):
        raise ValueError("scenario unit descriptor has no composition")
    container = composition.get("containerObjectId")
    members = composition.get("members")
    if not isinstance(container, str) or not container or not isinstance(members, list):
        raise ValueError("scenario unit descriptor composition is invalid")
    targets = [container]
    for row in members:
        if not isinstance(row, Mapping):
            raise ValueError("scenario unit descriptor member is invalid")
        object_id = row.get("objectId")
        if not isinstance(object_id, str) or not object_id:
            raise ValueError("scenario unit descriptor member identity is invalid")
        targets.append(object_id)
    return sorted(set(targets), key=str.casefold)


def build_scenario_unit_visual_closure_batch(
    catalog: InstallCatalog,
    effective_root: Path,
    descriptors: Iterable[Mapping[str, object]],
    *,
    max_workers: int = 4,
) -> dict[str, dict[str, object]]:
    """Build byte-equivalent per-unit closures with one shared asset context.

    Reports remain exact single-unit reports rather than projections of a union
    graph.  The retail inventory/catalog filter is memoized by the closure
    builder, catalog identity is serialized once, and independent target sets
    are evaluated concurrently.  Duplicate target sets reuse one deterministic
    report without changing any per-unit digest semantics.
    """

    root = Path(effective_root).expanduser().resolve(strict=True)
    by_object: dict[str, tuple[str, ...]] = {}
    folded_ids: set[str] = set()
    for descriptor in descriptors:
        validate_playable_unit_descriptor(descriptor)
        object_id = descriptor.get("objectId")
        if not isinstance(object_id, str) or not object_id:
            raise ValueError("scenario unit descriptor identity is invalid")
        folded = object_id.casefold()
        if folded in folded_ids:
            raise ValueError(f"duplicate scenario unit descriptor: {object_id}")
        folded_ids.add(folded)
        if descriptor.get("production") != [] or not isinstance(
            descriptor.get("scenarioAdmission"), Mapping
        ):
            raise ValueError(f"scenario unit descriptor is not admitted: {object_id}")
        by_object[object_id] = tuple(_scenario_visual_targets(descriptor))

    if not by_object:
        return {}
    if not isinstance(max_workers, int) or isinstance(max_workers, bool) or max_workers < 1:
        raise ValueError("scenario visual closure worker count must be positive")

    identity = getattr(catalog, "identity_sha256", None)
    identity_sha256 = identity() if callable(identity) else None
    if not isinstance(identity_sha256, str) or not identity_sha256:
        identity_sha256 = None

    unique_targets = sorted(set(by_object.values()), key=lambda row: tuple(
        (item.casefold(), item) for item in row
    ))

    def _build(targets: tuple[str, ...]) -> tuple[tuple[str, ...], dict[str, object]]:
        report = build_retail_visual_closure(
            root,
            targets,
            catalog=catalog,
            catalog_identity_sha256=identity_sha256,
        )
        return targets, report

    worker_count = min(max_workers, len(unique_targets))
    with ThreadPoolExecutor(max_workers=worker_count) as executor:
        reports = dict(executor.map(_build, unique_targets))
    return {
        object_id: deepcopy(reports[targets])
        for object_id, targets in sorted(by_object.items(), key=lambda row: row[0].casefold())
    }


def extend_profile_with_unit(
    base: Mapping[str, object], recipe: Mapping[str, object]
) -> tuple[dict[str, object], dict[str, object]]:
    """Return one strict additive profile and its delta identity."""

    target = deepcopy(dict(base))
    resources = target.get("resources")
    runtime_data = target.get("runtime_data")
    pack = target.get("pack")
    if (
        not isinstance(resources, list)
        or not isinstance(runtime_data, dict)
        or not isinstance(pack, dict)
    ):
        raise ValueError("base profile is not extensible")
    files = pack.get("files")
    if not isinstance(files, dict):
        raise ValueError("base profile pack has no file registry")
    slug = _slug(str(recipe["objectId"]))
    runtime_path = f"data/playable-units/{slug}.json"
    pack_file_key = f"playableUnit.{slug}"
    object_id = str(recipe["objectId"])
    folded_runtime_path = runtime_path.casefold()
    folded_file_key = pack_file_key.casefold()
    for key, value in files.items():
        if str(key).casefold() == folded_file_key and str(key) != pack_file_key:
            raise ValueError("playable-unit pack file key collides by case")
        if str(value).casefold() == folded_runtime_path and str(key) != pack_file_key:
            raise ValueError("playable-unit runtime path has multiple pack file owners")
    old_document = runtime_data.get(runtime_path)
    old_resource_ids = set()
    if isinstance(old_document, Mapping):
        if (
            old_document.get("schema") != "openbfme.playable-unit-runtime"
            or old_document.get("schemaVersion") != 0
        ):
            raise ValueError("existing playable-unit runtime schema is invalid")
        old_object_id = old_document.get("objectId")
        if (
            not isinstance(old_object_id, str)
            or old_object_id.casefold() != object_id.casefold()
        ):
            raise ValueError("playable-unit slug belongs to a different Object id")
        if files.get(pack_file_key) != runtime_path:
            raise ValueError("existing playable-unit pack file mapping is invalid")
        raw_ids = old_document.get("resourceIds", [])
        if not isinstance(raw_ids, list) or any(
            not isinstance(value, str) for value in raw_ids
        ):
            raise ValueError("existing playable-unit resource ownership is invalid")
        if len({value.casefold() for value in raw_ids}) != len(raw_ids):
            raise ValueError("existing playable-unit resource ownership is duplicated")
        old_resource_ids = set(raw_ids)
    elif old_document is not None:
        raise ValueError("existing playable-unit runtime document is invalid")
    elif pack_file_key in files:
        raise ValueError("playable-unit pack file key is already registered")
    old_folded = {value.casefold() for value in old_resource_ids}
    for path, document in runtime_data.items():
        if str(path) == runtime_path or not isinstance(document, Mapping):
            continue
        if str(path).casefold() == folded_runtime_path:
            raise ValueError("playable-unit runtime path collides by case")
        if str(document.get("objectId", "")).casefold() == object_id.casefold():
            raise ValueError(
                "playable-unit Object id is already registered at another path"
            )
        foreign_ids = document.get("resourceIds", [])
        if isinstance(foreign_ids, list) and old_folded.intersection(
            value.casefold() for value in foreign_ids if isinstance(value, str)
        ):
            raise ValueError("playable-unit resources have shared runtime ownership")
    existing_id_rows: dict[str, list[str]] = {}
    for row in resources:
        if not isinstance(row, Mapping) or not isinstance(row.get("id"), str):
            continue
        existing_id_rows.setdefault(str(row["id"]).casefold(), []).append(
            str(row["id"])
        )
    for identifier in old_resource_ids:
        canonical_rows = existing_id_rows.get(identifier.casefold(), [])
        if len(canonical_rows) != 1:
            raise ValueError(
                "existing playable-unit resource ownership is not exclusive"
            )
        if canonical_rows[0] != identifier:
            raise ValueError(
                "existing playable-unit resource ownership spelling differs"
            )
    new_resources = deepcopy(recipe["resources"])
    new_ids = {str(row["id"]) for row in new_resources}
    if len({value.casefold() for value in new_ids}) != len(new_ids):
        raise ValueError("playable-unit recipe has duplicate resource ids")
    collisions = {
        identifier
        for identifier in new_ids
        if identifier.casefold() in existing_id_rows
        and identifier.casefold() not in old_folded
    }
    if collisions:
        raise ValueError(
            "playable-unit resource id collision: " + ", ".join(sorted(collisions))
        )
    target["resources"] = [
        row
        for row in resources
        if not isinstance(row, Mapping) or row.get("id") not in old_resource_ids
    ] + new_resources
    runtime_data[runtime_path] = {
        "schema": "openbfme.playable-unit-runtime",
        "schemaVersion": 0,
        "objectId": recipe["objectId"],
        "category": recipe["category"],
        "descriptorSha256": recipe["descriptorSha256"],
        "recipeSha256": recipe["recipeSha256"],
        "resourceIds": sorted(new_ids),
        "registration": deepcopy(recipe["runtimeRegistration"]),
    }
    files[pack_file_key] = runtime_path
    return target, {
        "runtimePath": runtime_path,
        "packFileKey": pack_file_key,
        "resourceIds": sorted(new_ids),
        "update": old_document is not None,
    }


def _profile_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _base_profile_path(state_root: Path, canonical: Path, pack_root: Path) -> Path:
    manifest = read_json(pack_root / "provenance" / "manifest.json")
    expected = manifest.get("profile_sha256") if isinstance(manifest, Mapping) else None
    candidates = [
        canonical,
        *sorted((state_root / "profiles").glob("playable-units-*.generated.json")),
    ]
    matches = [
        path
        for path in candidates
        if path.is_file() and _profile_sha256(path) == expected
    ]
    if len(matches) != 1:
        raise RuntimeError("selected base profile identity is missing or ambiguous")
    return matches[0]


def _selected_base_profile(
    state_root: Path,
    canonical: Path,
    content_root: Path,
    pack_root: Path,
    *,
    bootstrap_selection: bool,
) -> Path:
    """Bind mutations to the exact audited state pack selected by Godot."""

    state_audit = audit_pack(pack_root) if pack_root.is_dir() else {"valid": False}
    selection_path = content_root / "selection.json"
    if not state_audit.get("valid"):
        if (
            bootstrap_selection
            and not pack_root.exists()
            and not selection_path.exists()
        ):
            return canonical
        raise RuntimeError("state base pack is not canonically auditable")
    state_pack = read_json(pack_root / "pack.json")
    state_manifest = read_json(pack_root / "provenance" / "manifest.json")
    if not selection_path.is_file():
        if bootstrap_selection:
            return _base_profile_path(state_root, canonical, pack_root)
        raise RuntimeError(
            "Godot pack selection is missing; use --bootstrap-selection only for a clean install"
        )
    selection = read_json(selection_path)
    if not isinstance(selection, Mapping) or (
        selection.get("schema") != "openbfme.pack-selection"
        or selection.get("schemaVersion") != 0
    ):
        raise RuntimeError("Godot pack selection contract is invalid")
    active = selection.get("activePack")
    if not isinstance(active, str):
        raise RuntimeError("Godot pack selection has no activePack")
    relative = PurePosixPath(active)
    if (
        relative.is_absolute()
        or not relative.parts
        or any(part in {"", ".", ".."} for part in relative.parts)
    ):
        raise RuntimeError("Godot activePack is not a safe relative path")
    content_root = content_root.resolve()
    selected_root = content_root.joinpath(*relative.parts)
    candidate = selected_root
    while candidate != content_root:
        is_junction = getattr(candidate, "is_junction", lambda: False)
        if candidate.is_symlink() or is_junction():
            raise RuntimeError("Godot activePack traverses a link")
        parent = candidate.parent
        if parent == candidate:
            raise RuntimeError("Godot activePack escapes the content root")
        candidate = parent
    selected_root = selected_root.resolve()
    try:
        selected_root.relative_to(content_root)
    except ValueError as exc:
        raise RuntimeError("Godot activePack escapes the content root") from exc
    selected_audit = (
        audit_pack(selected_root) if selected_root.is_dir() else {"valid": False}
    )
    if not selected_audit.get("valid"):
        raise RuntimeError("selected Godot pack is not canonically auditable")
    selected_pack = read_json(selected_root / "pack.json")
    selected_manifest = read_json(selected_root / "provenance" / "manifest.json")
    state_id = state_pack.get("id") if isinstance(state_pack, Mapping) else None
    if not state_id or selected_pack.get("id") != state_id:
        raise RuntimeError("selected and state pack ids differ")
    state_digest = bundle_digest(pack_root)
    selected_digest = bundle_digest(selected_root)
    if selected_digest != state_digest or relative.parts[-1] != selected_digest:
        raise RuntimeError("selected and state pack bundle identities differ")
    for field in ("profile", "profile_sha256"):
        if selected_manifest.get(field) != state_manifest.get(field):
            raise RuntimeError(f"selected and state pack {field} identities differ")
    return _base_profile_path(state_root, canonical, pack_root)


def import_playable_unit(
    catalog: InstallCatalog,
    state_root: Path,
    object_id: str,
    *,
    faction: str = "auto",
    canonical_profile: Path,
    content_root: Path,
    publish: bool = True,
    bootstrap_selection: bool = False,
    conversion_jobs: int | None = None,
    allow_fewer_playable_units: bool = False,
) -> dict[str, object]:
    """Execute the complete private importer path for one playable unit."""

    pipeline = ImportPipeline(
        catalog, state_root, game="bfme2", conversion_jobs=conversion_jobs
    )
    effective_root, manifest_path, _staging, _backup = pipeline._effective_asset_paths()
    if not manifest_path.is_file():
        pipeline.extract_all_assets(force=False)
    canonical_profile = canonical_profile.expanduser().resolve()
    canonical = ImportProfile.load(canonical_profile)
    pack_root = pipeline.packs_root / canonical.pack_id
    base_path = _selected_base_profile(
        state_root,
        canonical_profile,
        content_root,
        pack_root,
        bootstrap_selection=bootstrap_selection,
    )
    if publish and base_path == canonical_profile and not pack_root.exists():
        pack_root = pipeline.build(resolve_profile(canonical, catalog), force=False)
    base_document = read_json(base_path)
    if not isinstance(base_document, Mapping):
        raise ValueError("base profile root is invalid")

    graph, descriptor, closure, recipe = compile_unit_recipe(
        catalog, effective_root, object_id, game="bfme2", faction=faction
    )
    target_document, delta = extend_profile_with_unit(base_document, recipe)
    payload = _canonical_bytes(target_document)
    profile_sha = hashlib.sha256(payload).hexdigest()
    generated_path = (
        state_root / "profiles" / f"playable-units-{profile_sha}.generated.json"
    )
    write_json_atomic(generated_path, target_document)
    target_profile = ImportProfile.load(generated_path)
    resolved = resolve_profile(target_profile, catalog)
    if resolved.missing_required:
        raise RuntimeError(
            "generated playable-unit profile is incomplete: "
            + ", ".join(resolved.missing_required)
        )

    reports = state_root / "reports" / "playable-units" / _slug(str(recipe["objectId"]))
    write_json_atomic(reports / "faction-graph.json", graph)
    write_json_atomic(reports / "descriptor.json", descriptor)
    write_json_atomic(reports / "visual-closure.json", closure)
    write_json_atomic(reports / "pack-recipe.json", recipe)

    if not publish:
        publication = {"planned": True}
    elif bool(delta["update"]):
        pack_root = pipeline.build(resolved, force=False)
        # Same fail-closed roster gate the faction publish lane runs. This is
        # the rebuild branch: it replaces the whole published bundle, so a
        # profile delta that quietly drops units would ship as a regression.
        # Raises PublishGateError, which cli maps to exit 7.
        enforce_playable_unit_gate(
            pack_root,
            content_root,
            resolved.pack_id,
            allow_fewer=allow_fewer_playable_units,
        )
        publication = pipeline.publish_to_godot(pack_root, content_root)
    else:
        command = [
            sys.executable,
            str(
                Path(__file__).resolve().parents[2]
                / "tools"
                / "merge-retail-additive-profile.py"
            ),
            "--state-root",
            str(state_root),
            "--profile",
            str(generated_path),
            "--content-root",
            str(content_root),
        ]
        for resource_id in delta["resourceIds"]:
            command.extend(("--resource-id", str(resource_id)))
        command.extend(("--runtime-path", str(delta["runtimePath"])))
        command.extend(("--pack-file-key", str(delta["packFileKey"])))
        completed = subprocess.run(
            command,
            check=True,
            text=True,
            capture_output=True,
            env={
                **os.environ,
                "PYTHONPATH": str(Path(__file__).resolve().parents[1]),
            },
        )
        publication = json.loads(completed.stdout)
    current_bundle = bundle_digest(pack_root) if pack_root.is_dir() else ""
    return {
        "ready": True,
        "object_id": recipe["objectId"],
        "category": recipe["category"],
        "faction": graph["target"]["playerTemplate"],
        "descriptor_sha256": recipe["descriptorSha256"],
        "recipe_sha256": recipe["recipeSha256"],
        "profile": str(generated_path),
        "profile_sha256": profile_sha,
        "runtime_path": delta["runtimePath"],
        "resource_count": len(delta["resourceIds"]),
        "updated": bool(delta["update"]),
        "reports": str(reports),
        "bundle_sha256": publication.get("bundle_sha256", current_bundle),
        "publication": publication,
    }


__all__ = [
    "build_scenario_unit_visual_closure_batch",
    "FACTIONS",
    "compile_scenario_unit_recipe",
    "compile_unit_recipe",
    "extend_profile_with_unit",
    "import_playable_unit",
]
