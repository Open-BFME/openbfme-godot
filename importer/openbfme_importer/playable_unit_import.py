"""One-command BFME2 playable-unit discovery, conversion, and publication."""

from __future__ import annotations

from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
from pathlib import PurePosixPath
import subprocess
import sys
from typing import Any, Mapping

from .catalog import InstallCatalog
from .faction_census import census_playable_faction
from .faction_policy import (
    implicit_object_roots,
    music_roots,
    source_null_command_sets,
    source_null_mapped_image_textures,
)
from .pipeline import ImportPipeline, audit_pack, bundle_digest
from .playable_unit_compiler import (
    PlayableUnitCompilerError,
    compile_playable_unit_descriptor,
)
from .playable_unit_pack_compiler import compile_playable_unit_pack_recipe
from .profile import ImportProfile, resolve_profile
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
    "data/ini/armor.ini",
    "data/ini/upgrade.ini",
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
        try:
            descriptor = compile_playable_unit_descriptor(
                object_id, documents, faction_graph=graph
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
    result: set[str] = set()
    for command in descriptor["presentation"]["ui"]["commands"]:
        fields = command.get("fields", {})
        for field in ("TextLabel", "DescriptLabel"):
            result.update(str(value) for value in fields.get(field, []) if str(value))
    for ability in descriptor.get("abilities", []):
        if not isinstance(ability, Mapping):
            continue
        button = ability.get("button", {})
        if not isinstance(button, Mapping):
            continue
        for field in ("labelIds", "tooltipIds"):
            result.update(str(value) for value in button.get(field, []) if str(value))
    return result


def _resolved_media(
    graph: Mapping[str, object],
    descriptor: Mapping[str, object],
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
    images: dict[str, Mapping[str, object]] = {}
    for identifier in sorted(_required_image_ids(descriptor), key=str.casefold):
        row = images_by_id.get(identifier.casefold())
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
    catalog: InstallCatalog, descriptor: Mapping[str, object]
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
    for identifier in sorted(_required_string_ids(descriptor), key=str.casefold):
        record = string_catalog.record(identifier)
        if record is None or not record.value:
            raise ValueError(f"required localized string is unresolved: {identifier}")
        resolved_strings[identifier] = record.value
    return resolved_strings


def compile_unit_recipe(
    catalog: InstallCatalog,
    effective_root: Path,
    object_id: str,
    *,
    faction: str = "auto",
) -> tuple[dict[str, Any], dict[str, object], dict[str, object], dict[str, object]]:
    """Compile graph, descriptor, visual closure, and pack recipe for one unit."""

    documents = _source_documents(effective_root)
    graph, draft = _select_faction_graph(catalog, documents, object_id, faction)
    images, audio = _resolved_media(graph, draft)
    strings = _resolved_strings(catalog, draft)
    descriptor = compile_playable_unit_descriptor(
        object_id,
        documents,
        faction_graph=graph,
        resolved_images=images,
        resolved_audio=audio,
        resolved_strings=strings,
    )
    composition = descriptor["composition"]
    targets = [str(composition["containerObjectId"])]
    targets.extend(str(row["objectId"]) for row in composition["members"])
    closure = build_retail_visual_closure(
        effective_root, sorted(set(targets), key=str.casefold)
    )
    recipe = compile_playable_unit_pack_recipe(descriptor, closure)
    return graph, descriptor, closure, recipe


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
        catalog, effective_root, object_id, faction=faction
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
    "FACTIONS",
    "compile_unit_recipe",
    "extend_profile_with_unit",
    "import_playable_unit",
]
