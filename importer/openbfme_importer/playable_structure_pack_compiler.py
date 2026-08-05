"""Closure-driven playable-structure conversion recipe compiler.

Structures convert from the same retail visual closure as playable units, but
their pack recipes are keyed by building lifecycle phase instead of unit core
animation states.  This module owns the source-backed visual recipe and the
descriptor+evidence-bound runtime composition.

Version note: the runtime envelope (``openbfme.playable-structure-runtime``)
stays at schemaVersion 0 — its identity fields and registration wrapper are
unchanged, and the publication lane pins that version.  The embedded
``openbfme.building-lifecycle-presentation`` document is composed at
schemaVersion 1 in the exact presenter-grade shape RetailStructure validates,
carrying ``evidenceProfile: "composed-structure-runtime"`` so validators can
distinguish it from the sealed Men and neutral evidence lanes.
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from copy import deepcopy
import hashlib
from pathlib import PurePosixPath
import re

from .playable_unit_pack_compiler import (
    _digest,
    _paths,
    _resource_id,
    _rows,
    _slug,
    _validate_dependency_closure,
)
from .profile import MAX_PATTERNS_PER_RESOURCE


SCHEMA = "openbfme.playable-structure-pack-recipe"
SCHEMA_VERSION = 1
LIFECYCLE_PHASE_ORDER = (
    "construction",
    "intact",
    "damaged",
    "really-damaged",
    "rubble",
    "post-rubble",
)


class PlayableStructurePackCompilerError(ValueError):
    """A source-backed structure cannot produce one bounded pack recipe."""


def _closure_identity(visual_closure: Mapping[str, object]) -> str:
    if (
        visual_closure.get("schema") != "openbfme.retail-visual-closure"
        or visual_closure.get("schemaVersion") != 1
    ):
        raise PlayableStructurePackCompilerError("visual closure identity is invalid")
    unsigned = dict(visual_closure)
    digest = unsigned.pop("aggregateSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise PlayableStructurePackCompilerError("visual closure digest is invalid")
    return digest


def _scanned_index(
    visual_closure: Mapping[str, object],
) -> dict[str, Mapping[str, object]]:
    scanned = _rows(visual_closure.get("scannedW3d"), "scanned W3D")
    _validate_dependency_closure(visual_closure, scanned)
    result: dict[str, Mapping[str, object]] = {}
    for row in scanned:
        path = str(row.get("virtualPath", ""))
        if not path or path.casefold() in result:
            raise PlayableStructurePackCompilerError("scanned W3D index is invalid")
        result[path.casefold()] = row
    return result


def _header_ids(row: Mapping[str, object], field: str) -> tuple[str, ...]:
    headers = row.get("headerIds")
    if not isinstance(headers, Mapping):
        raise PlayableStructurePackCompilerError("scanned W3D header ids are missing")
    values = headers.get(field, [])
    if not isinstance(values, list) or any(
        not isinstance(value, str) for value in values
    ):
        raise PlayableStructurePackCompilerError(f"scanned W3D {field} are invalid")
    return tuple(values)


def _animation_hierarchies(row: Mapping[str, object], path: str) -> frozenset[str]:
    prefixes: set[str] = set()
    for identifier in _header_ids(row, "animationIds"):
        if "." not in identifier:
            continue
        prefixes.add(identifier.split(".", 1)[0].casefold())
    if not prefixes:
        raise PlayableStructurePackCompilerError(
            f"animation W3D declares no hierarchy header: {path}"
        )
    return frozenset(prefixes)


def _animation_clip_ids(row: Mapping[str, object]) -> tuple[str, ...]:
    clips: set[str] = set()
    for identifier in _header_ids(row, "animationIds"):
        if "." not in identifier:
            continue
        clips.add(identifier.split(".", 1)[1].casefold())
    return tuple(sorted(clips))


def _phase_rows(
    visual_closure: Mapping[str, object], target_object_id: str
) -> tuple[
    dict[str, set[str]],
    dict[str, set[tuple[str, ...]]],
    dict[str, set[str]],
    list[str],
    dict[str, set[str]],
    dict[str, set[tuple[str, ...]]],
    dict[str, set[str]],
    list[dict[str, object]],
]:
    target_key = target_object_id.casefold()
    model_phases: dict[str, set[str]] = {}
    model_conditions: dict[str, set[tuple[str, ...]]] = {}
    model_draw_modules: dict[str, set[str]] = {}
    module_positions: dict[str, tuple[str, int, str]] = {}
    animation_phases: dict[str, set[str]] = {}
    bib_conditions: dict[str, set[tuple[str, ...]]] = {}
    bib_identifiers: dict[str, set[str]] = {}
    exclusions: list[dict[str, object]] = []
    deferred_editor_models: list[
        tuple[
            tuple[str, ...],
            tuple[str, ...],
            tuple[str, ...],
            object,
            str,
        ]
    ] = []
    for row in _rows(visual_closure.get("exactLeaves"), "exact visual leaves"):
        if str(row.get("targetObject", "")).casefold() != target_key:
            continue
        kind = str(row.get("kind", "")).casefold()
        if kind not in {"model", "animation"}:
            continue
        conditions = row.get("conditions", [])
        if not isinstance(conditions, list) or any(
            not isinstance(value, str) for value in conditions
        ):
            raise PlayableStructurePackCompilerError(
                f"visual leaf conditions are invalid: {row.get('identifier')}"
            )
        condition_keys = {str(value).casefold() for value in conditions}
        raw_phases = row.get("lifecyclePhases", [])
        if not isinstance(raw_phases, list) or any(
            not isinstance(value, str) for value in raw_phases
        ):
            raise PlayableStructurePackCompilerError(
                f"visual leaf lifecycle phases are invalid: {row.get('identifier')}"
            )
        unknown = [
            value for value in raw_phases if value not in LIFECYCLE_PHASE_ORDER
        ]
        if unknown:
            raise PlayableStructurePackCompilerError(
                "visual leaf declares an unknown lifecycle phase: "
                + ", ".join(sorted(unknown))
            )
        paths = _paths(row, f"structure {kind} leaf")
        provenance = row.get("provenance", {})
        scope_path = (
            provenance.get("scopePath", [])
            if isinstance(provenance, Mapping)
            else []
        )
        draw_module = (
            str(scope_path[0])
            if isinstance(scope_path, list) and scope_path
            else ""
        )
        if "world_builder" in condition_keys:
            # Retail fortresses author ``Model = None`` as their default and
            # gate the real body behind WORLD_BUILDER because retail fortresses
            # are always map-placed.  Defer the exclusion decision: when the
            # object authors no other intact model, the world-builder model is
            # its only source-backed intact visual and must stay available.
            if kind == "model" and str(row.get("usage", "")) != "floor-model":
                deferred_editor_models.append(
                    (
                        paths,
                        tuple(str(value) for value in conditions),
                        tuple(str(value) for value in raw_phases),
                        row.get("provenance"),
                        draw_module,
                    )
                )
                continue
            for path in paths:
                exclusions.append(
                    {
                        "kind": kind,
                        "sourceW3d": path,
                        "reason": "editor-only-model",
                    }
                )
            continue
        condition_tuple = tuple(str(value) for value in conditions)
        if kind == "model" and str(row.get("usage", "")) == "floor-model":
            for path in paths:
                bib_conditions.setdefault(path, set()).add(condition_tuple)
                bib_identifiers.setdefault(path, set()).add(
                    str(row.get("identifier", ""))
                )
            continue
        destination = model_phases if kind == "model" else animation_phases
        for path in paths:
            destination.setdefault(path, set()).update(raw_phases)
            if kind == "model":
                model_conditions.setdefault(path, set()).add(condition_tuple)
                model_draw_modules.setdefault(path, set()).add(draw_module)
                if isinstance(provenance, Mapping):
                    position = (
                        str(provenance.get("virtualPath", "")).casefold(),
                        int(provenance.get("line", 0)),
                        str(provenance.get("virtualPath", "")),
                    )
                    current = module_positions.get(draw_module)
                    if current is None or position < current:
                        module_positions[draw_module] = position
    if any("intact" in phases for phases in model_phases.values()) or not (
        model_phases
    ):
        for paths, _conditions, _phases, _provenance, _module in (
            deferred_editor_models
        ):
            for path in paths:
                exclusions.append(
                    {
                        "kind": "model",
                        "sourceW3d": path,
                        "reason": "editor-only-model",
                    }
                )
    else:
        for paths, condition_tuple, editor_phases, editor_provenance, editor_module in (
            deferred_editor_models
        ):
            for path in paths:
                model_phases.setdefault(path, set()).update(editor_phases)
                model_conditions.setdefault(path, set()).add(condition_tuple)
                model_draw_modules.setdefault(path, set()).add(editor_module)
                if isinstance(editor_provenance, Mapping):
                    position = (
                        str(editor_provenance.get("virtualPath", "")).casefold(),
                        int(editor_provenance.get("line", 0)),
                        str(editor_provenance.get("virtualPath", "")),
                    )
                    current = module_positions.get(editor_module)
                    if current is None or position < current:
                        module_positions[editor_module] = position
    module_order = sorted(
        module_positions, key=lambda module: module_positions[module]
    )
    return (
        model_phases,
        model_conditions,
        model_draw_modules,
        module_order,
        animation_phases,
        bib_conditions,
        bib_identifiers,
        exclusions,
    )


def _hierarchy_providers(
    prefixes: frozenset[str], scanned: Mapping[str, Mapping[str, object]]
) -> dict[str, tuple[str, ...]]:
    providers: dict[str, list[str]] = {prefix: [] for prefix in prefixes}
    for row in scanned.values():
        authored = {
            value.casefold() for value in _header_ids(row, "hierarchyIds")
        }
        for prefix in prefixes & authored:
            providers[prefix].append(str(row["virtualPath"]))
    return {
        prefix: tuple(sorted(paths, key=lambda item: (item.casefold(), item)))
        for prefix, paths in providers.items()
    }


def _phase_slug(phases: tuple[str, ...]) -> str:
    if not phases:
        return "unphased"
    return "-".join(_slug(value) for value in phases)


def _condition_sets_list(sets: set[tuple[str, ...]]) -> list[list[str]]:
    return [
        list(value)
        for value in sorted(sets, key=lambda item: (len(item), item))
    ]


def _ui_image_resources(
    slug: str, resolved_images: Mapping[str, Mapping[str, object]]
) -> tuple[list[dict[str, object]], dict[str, str], dict[str, dict[str, int]]]:
    """Mirror the playable-unit UI lane: group resolved MappedImage rows by
    retail atlas and emit one texture-atlas-crops resource per atlas plus the
    id -> converted PNG bindings and their crop metadata."""

    mapped_by_atlas: dict[str, list[tuple[str, Mapping[str, object]]]] = {}
    for identifier, image in resolved_images.items():
        if not isinstance(identifier, str) or not isinstance(image, Mapping):
            raise PlayableStructurePackCompilerError("mapped UI image is invalid")
        source = image.get("compiledTextureVirtualPath")
        if not isinstance(source, str) or not source:
            raise PlayableStructurePackCompilerError(
                f"UI image atlas is unresolved: {identifier}"
            )
        mapped_by_atlas.setdefault(source, []).append((identifier, image))
    resources: list[dict[str, object]] = []
    image_bindings: dict[str, str] = {}
    image_binding_metadata: dict[str, dict[str, int]] = {}
    for atlas_index, (source, records) in enumerate(
        sorted(mapped_by_atlas.items(), key=lambda item: item[0].casefold())
    ):
        atlas_digest = hashlib.sha256(source.casefold().encode()).hexdigest()[:12]
        resource_id = _resource_id(
            "structure", slug, "ui-atlas", str(atlas_index), atlas_digest
        )
        output_directory = f"assets/ui/structures/{slug}/{atlas_digest}"
        crops: list[dict[str, object]] = []
        for identifier, image in sorted(records, key=lambda item: item[0].casefold()):
            coords = image.get("coords")
            if not isinstance(coords, Mapping):
                raise PlayableStructurePackCompilerError(
                    f"UI image crop is invalid: {identifier}"
                )
            values = [coords.get(name) for name in ("left", "top", "right", "bottom")]
            if any(
                not isinstance(value, int) or isinstance(value, bool)
                for value in values
            ):
                raise PlayableStructurePackCompilerError(
                    f"UI image crop is invalid: {identifier}"
                )
            output_name = (
                f"{_slug(identifier)}-"
                f"{hashlib.sha256(identifier.casefold().encode()).hexdigest()[:8]}.png"
            )
            crops.append(
                {
                    "logicalName": _resource_id("image", identifier),
                    "output": output_name,
                    "crop": [
                        values[0],
                        values[1],
                        values[2] - values[0],
                        values[3] - values[1],
                    ],
                }
            )
            image_bindings[identifier] = f"{output_directory}/{output_name}"
            image_binding_metadata[identifier] = {
                "width": values[2] - values[0],
                "height": values[3] - values[1],
            }
        resources.append(
            {
                "id": resource_id,
                "kind": "ui",
                "converter": "texture-atlas-crops",
                "patterns": [source],
                "output": output_directory,
                "options": {"crops": crops},
                "required": True,
                "limit": 1,
                "expected_count": 1,
            }
        )
    return resources, image_bindings, image_binding_metadata


def compile_structure_visual_recipe(
    target_object_id: str,
    visual_closure: Mapping[str, object],
    *,
    resolved_images: Mapping[str, Mapping[str, object]] | None = None,
    image_binding_gaps: Sequence[Mapping[str, object]] | None = None,
) -> dict[str, object]:
    """Compile one structure's phase-keyed visual pack recipe or fail closed.

    ``resolved_images`` carries the structure's construct-button / selection
    portrait MappedImage rows (census-resolved, with atlas coords) and
    ``image_binding_gaps`` the explicit rows for images that could not be
    resolved. When both are ``None`` (standalone tooling and legacy tests)
    the recipe stays byte-identical to the pre-UI-binding shape."""

    if not target_object_id or len(target_object_id) > 256:
        raise PlayableStructurePackCompilerError("target Object id is invalid")
    closure_digest = _closure_identity(visual_closure)
    scanned = _scanned_index(visual_closure)
    (
        model_phases,
        model_conditions,
        model_draw_modules,
        draw_module_order,
        animation_phases,
        bib_conditions,
        bib_identifiers,
        exclusions,
    ) = _phase_rows(visual_closure, target_object_id)
    if not model_phases:
        raise PlayableStructurePackCompilerError(
            f"structure has no resolved lifecycle model: {target_object_id}"
        )
    slug = _slug(target_object_id)

    model_hierarchy_ids: dict[str, frozenset[str]] = {}
    model_animation_ids: dict[str, frozenset[str]] = {}
    model_external_hierarchies: dict[str, frozenset[str]] = {}
    model_skinned_meshes: dict[str, int] = {}
    model_mesh_counts: dict[str, int] = {}
    for model_path in (*model_phases, *bib_conditions):
        row = scanned.get(model_path.casefold())
        if row is None:
            raise PlayableStructurePackCompilerError(
                f"structure model is absent from scannedW3d: {model_path}"
            )
        skinned_mesh_count = row.get("skinnedMeshCount", 0)
        if (
            isinstance(skinned_mesh_count, bool)
            or not isinstance(skinned_mesh_count, int)
            or skinned_mesh_count < 0
        ):
            raise PlayableStructurePackCompilerError(
                f"scanned W3D skinned mesh count is invalid: {model_path}"
            )
        model_skinned_meshes[model_path] = skinned_mesh_count
        mesh_count = row.get("meshCount")
        if mesh_count is not None and (
            isinstance(mesh_count, bool)
            or not isinstance(mesh_count, int)
            or mesh_count < 0
        ):
            raise PlayableStructurePackCompilerError(
                f"scanned W3D mesh count is invalid: {model_path}"
            )
        model_mesh_counts[model_path] = mesh_count
        own_ids = frozenset(
            value.casefold() for value in _header_ids(row, "hierarchyIds")
        )
        model_hierarchy_ids[model_path] = own_ids
        model_animation_ids[model_path] = frozenset(
            value.casefold() for value in _header_ids(row, "animationIds")
        )
        referenced = row.get("modelHierarchyIdentifiers", [])
        if not isinstance(referenced, list) or any(
            not isinstance(value, str) for value in referenced
        ):
            raise PlayableStructurePackCompilerError(
                f"scanned W3D model hierarchy identifiers are invalid: {model_path}"
            )
        model_external_hierarchies[model_path] = frozenset(
            value.casefold()
            for value in referenced
            if value.casefold() not in own_ids
        )
    target_model_keys = {path.casefold() for path in model_phases}

    dependency = visual_closure.get("w3dDependencyClosure")
    if not isinstance(dependency, Mapping):
        raise PlayableStructurePackCompilerError("visual texture closure is invalid")
    row_channel_counts: dict[str, int] = {}
    for row in scanned.values():
        channel_count = row.get("embeddedAnimationChannelCount", 0)
        if (
            isinstance(channel_count, bool)
            or not isinstance(channel_count, int)
            or channel_count < 0
        ):
            raise PlayableStructurePackCompilerError(
                "scanned W3D embedded animation channel count is invalid: "
                + str(row.get("virtualPath", ""))
            )
        row_channel_counts[str(row["virtualPath"]).casefold()] = channel_count
    retail_absent_textures: dict[str, set[str]] = {}
    for row in _rows(dependency.get("embeddedTextures"), "embedded textures"):
        if row.get("status") != "missing":
            continue
        source = row.get("sourceW3dVirtualPath")
        identifier = row.get("identifier")
        if not isinstance(source, str) or not isinstance(identifier, str):
            raise PlayableStructurePackCompilerError("embedded texture row is invalid")
        if not identifier:
            raise PlayableStructurePackCompilerError(
                "retail-absent texture identifier is empty"
            )
        retail_absent_textures.setdefault(source.casefold(), set()).add(identifier)

    animation_bindings: dict[str, dict[str, object]] = {}
    for animation_path in sorted(
        animation_phases, key=lambda item: (item.casefold(), item)
    ):
        row = scanned.get(animation_path.casefold())
        if row is None:
            raise PlayableStructurePackCompilerError(
                f"structure animation is absent from scannedW3d: {animation_path}"
            )
        raw_mesh_count = row.get("meshCount")
        header_ids = row.get("headerIds")
        header_models = (
            header_ids.get("modelIds", []) if isinstance(header_ids, Mapping) else []
        )
        if (
            isinstance(raw_mesh_count, int)
            and not isinstance(raw_mesh_count, bool)
            and raw_mesh_count > 0
        ) or header_models:
            # RotWK 2.01 authors door "animation" variants that are full
            # models (kbhalldoors_cla.w3d ships two door meshes, its own
            # hierarchy, and the embedded clip). Importing such a file as a
            # bare clip source injects its meshes into the host bundle, so
            # it can never bind as an attached animation; the door motion
            # stays a recorded exclusion instead of silently corrupting the
            # host model. Files that are themselves lifecycle models still
            # convert through their own model states.
            exclusions.append(
                {
                    "kind": "animation",
                    "sourceW3d": animation_path,
                    "reason": "animation-file-carries-render-model",
                }
            )
            continue
        raw_channel_count = row.get("embeddedAnimationChannelCount")
        if raw_channel_count == 0 and not isinstance(raw_channel_count, bool):
            # RotWK 2.01 binds header-only pose "clips" with zero animation
            # channels (kbforgd_cls.w3d / kbangwgn_cls.w3d hold the doors in
            # their modeled pose). There is no motion to convert — importing
            # them creates no action — so the binding is recorded as an
            # explicit exclusion and the model presents its base pose,
            # exactly what the retail engine renders.
            exclusions.append(
                {
                    "kind": "animation",
                    "sourceW3d": animation_path,
                    "reason": "animation-authors-no-motion-channels",
                }
            )
            continue
        prefixes = _animation_hierarchies(row, animation_path)
        providers = _hierarchy_providers(prefixes, scanned)
        unprovided = sorted(
            prefix for prefix, paths in providers.items() if not paths
        )
        if unprovided:
            exclusions.append(
                {
                    "kind": "animation",
                    "sourceW3d": animation_path,
                    "reason": "animation-hierarchy-unresolved",
                    "hierarchyIds": unprovided,
                }
            )
            continue
        animation_bindings[animation_path] = {
            "prefixes": prefixes,
            "providers": providers,
            "clipIds": _animation_clip_ids(row),
        }

    resources: list[dict[str, object]] = []
    states: list[dict[str, object]] = []
    bib_states: list[dict[str, object]] = []
    attached_animations: set[str] = set()
    selected_w3d: set[str] = set()

    def _compile_model(
        model_path: str,
        *,
        output: str,
        bind_animations: bool,
    ) -> tuple[str, list[str], list[str]]:
        own_hierarchies = model_hierarchy_ids[model_path]
        animations: list[str] = []
        clip_ids: set[str] = set()
        hierarchy_patterns: set[str] = set()
        if bind_animations:
            phases = model_phases[model_path]
            for animation_path, binding in animation_bindings.items():
                if not animation_phases[animation_path] & phases:
                    continue
                prefixes = binding["prefixes"]
                assert isinstance(prefixes, frozenset)
                providers = binding["providers"]
                assert isinstance(providers, Mapping)
                compatible = True
                required_hierarchy_files: set[str] = set()
                for prefix in prefixes:
                    if prefix in own_hierarchies:
                        continue
                    if own_hierarchies:
                        compatible = False
                        break
                    dedicated = [
                        path
                        for path in providers[prefix]
                        if path.casefold() not in target_model_keys
                    ]
                    if not dedicated:
                        compatible = False
                        break
                    required_hierarchy_files.update(dedicated)
                if not compatible:
                    continue
                animations.append(animation_path)
                binding_clips = binding["clipIds"]
                assert isinstance(binding_clips, tuple)
                clip_ids.update(binding_clips)
                hierarchy_patterns.update(required_hierarchy_files)
                attached_animations.add(animation_path)
        animations.sort(key=lambda item: (item.casefold(), item))
        external_hierarchies = model_external_hierarchies[model_path]
        if external_hierarchies:
            providers = _hierarchy_providers(external_hierarchies, scanned)
            unresolved = sorted(
                prefix for prefix, paths in providers.items() if len(paths) != 1
            )
            if unresolved:
                raise PlayableStructurePackCompilerError(
                    "structure model hierarchy provider is not unique in scannedW3d: "
                    + ", ".join(unresolved)
                )
            hierarchy_patterns.update(paths[0] for paths in providers.values())
        embedded_animation_ids = model_animation_ids[model_path]
        channel_count = row_channel_counts[model_path.casefold()]
        # A header-only embedded animation chunk keys nothing; it is vacuous
        # evidence, not the adapter's embedded-animation shape.
        embedded_animation = bool(embedded_animation_ids) and channel_count > 0
        if embedded_animation and not bind_animations:
            raise PlayableStructurePackCompilerError(
                "structure bib model embeds an animation clip: " + model_path
            )
        # RotWK 2.01 models embed a redundant one-channel pose clip beside
        # externally bound state clips (kbangwgn_a.w3d embeds
        # KBANGWGN_ASKL.KBANGWGN_A while retail binds the _ABLD buildup
        # clip; the citadel kbfdoor models are the same shape). The adapter
        # discards the embedded pose actions with recorded report evidence
        # when external clips are declared, so a model that mixes both is a
        # regular bundle of its attached clips — not a failure.
        #
        # With no external clips attached, the reverse is retail's second way
        # of authoring a build-up: the motion lives inside the model's own
        # W3D as compressed animation channels and the state names the model
        # as its own clip (``AnimationName = GBWell_A.GBWell_A``, backed by
        # eight keyed channels in gbwell_a.w3d). ``options["animations"]``
        # below declares the model as its own animation source, which is the
        # adapter's embedded-model-animation shape: it imports the file once
        # and captures the clip it carries. Those captured clips are bundled
        # in the GLB exactly like attached ones, so they must be declared as
        # bundled clip ids or the phase animation cannot resolve the clip the
        # conversion actually produced.
        if embedded_animation and not animations:
            clip_ids.update(
                identifier.split(".", 1)[1]
                for identifier in embedded_animation_ids
                if "." in identifier
            )
        pivot_only = (
            model_mesh_counts[model_path] is not None
            and model_mesh_counts[model_path] == 0
        )
        if pivot_only:
            if not (own_hierarchies or external_hierarchies):
                raise PlayableStructurePackCompilerError(
                    "structure model has no meshes and no hierarchy: " + model_path
                )
            if animations or embedded_animation:
                raise PlayableStructurePackCompilerError(
                    "structure pivot-only model carries animations: " + model_path
                )
        patterns = sorted(
            {model_path, *animations, *hierarchy_patterns},
            key=lambda item: (item.casefold(), item),
        )
        # A lifecycle model with no animation binding is not automatically a
        # skinned hierarchy. A model-authored hierarchy is a rigid carrier and
        # must use the adapter's explicit, validated root-rigid bake; a model
        # without one is a static mesh. Calling both shapes merely
        # ``w3d-hierarchical`` deferred the distinction until Blender and made
        # real bib models fail at skin validation. A model whose HLod headers
        # reference a hierarchy its own file does not author is the same rigid
        # carrier with its pivots in a sibling file; the provider is staged so
        # the pinned importer resolves it exactly. A model that embeds its own
        # animation clip is the adapter's exact embedded-animation shape. The
        # bake stays rigid-only: any mesh with a vertex-influences stream is
        # real skeletal content that baking would destroy, so such models keep
        # their hierarchy instead. A model with no meshes at all is retail's
        # authored attachment-pivot carrier (fire and smoke emitters): the
        # hierarchy IS the content and must survive to the GLB.
        converter = (
            "w3d-bundle"
            if animations or embedded_animation
            else "w3d-hierarchical"
            if own_hierarchies or external_hierarchies
            else "w3d-static"
        )
        # Resource ids must distinguish every emitted GLB, not just the W3D
        # stem. Bib and lifecycle bodies often share a retail stem (trees and
        # props that reuse the same model file name for multiple roles); using
        # only the stem collides their ids and rejects an otherwise exact
        # default-visual recipe. The output path already carries the role
        # (phase slug or ``bib-``) so its stem is unique within one recipe.
        resource_id = _resource_id("structure", slug, PurePosixPath(output).stem)
        options: dict[str, object] = {"model": PurePosixPath(model_path).name}
        if animations:
            options["animations"] = [
                PurePosixPath(path).name for path in animations
            ]
        elif embedded_animation:
            options["animations"] = [PurePosixPath(model_path).name]
        elif pivot_only:
            options["provenPivotOnlyModel"] = True
        elif (own_hierarchies or external_hierarchies) and (
            model_skinned_meshes[model_path] == 0
        ):
            options["provenRootRigidBake"] = True
        absent = retail_absent_textures.get(model_path.casefold())
        if absent:
            options["retailAbsentTextures"] = sorted(absent, key=str.casefold)
        selected_w3d.update(patterns)
        resources.append(
            {
                "id": resource_id,
                "kind": "model",
                "converter": converter,
                "patterns": patterns,
                "output": output,
                "options": options,
                "required": True,
                "limit": len(patterns),
                "expected_count": len(patterns),
            }
        )
        return resource_id, animations, sorted(clip_ids)

    for model_path in sorted(model_phases, key=lambda item: (item.casefold(), item)):
        if (
            model_mesh_counts[model_path] == 0
            and not model_hierarchy_ids[model_path]
            and not model_external_hierarchies[model_path]
            and model_animation_ids[model_path]
        ):
            # RotWK 2.01 authors an animation-only W3D in a Model slot
            # (AngmarFortressCitadel's House of Lamentation RUBBLE state
            # names KBFHoLa_D3, a file carrying only the destruction clip).
            # The engine finds no render object in it and draws nothing, so
            # the authored truth is an explicit exclusion, never a body.
            exclusions.append(
                {
                    "kind": "model",
                    "sourceW3d": model_path,
                    "reason": "model-slot-authors-animation-only-w3d",
                }
            )
            continue
        phases = tuple(
            sorted(
                model_phases[model_path],
                key=lambda value: (
                    LIFECYCLE_PHASE_ORDER.index(value),
                    value,
                ),
            )
        )
        stem = PurePosixPath(model_path).stem
        output = (
            f"assets/models/structures/{slug}/"
            f"{_phase_slug(phases)}-{_slug(stem)}.glb"
        )
        resource_id, animations, clip_ids = _compile_model(
            model_path, output=output, bind_animations=True
        )
        # A model's own animation chunk only keys motion when it carries
        # channels; a header-only chunk is vacuous evidence (see the embedded
        # animation rule in _compile_model). Recording the keyed embedded clip
        # ids lets a failure say whether the model carries motion at all, so
        # "this model keys nothing" never reads the same as "this model keys
        # motion that the conversion did not bundle".
        embedded_clip_ids = (
            sorted(
                {
                    identifier.split(".", 1)[1]
                    for identifier in model_animation_ids[model_path]
                    if "." in identifier
                }
            )
            if row_channel_counts[model_path.casefold()] > 0
            else []
        )
        states.append(
            {
                "phases": list(phases),
                "sourceW3d": model_path,
                "sourceConditionSets": _condition_sets_list(
                    model_conditions.get(model_path, set())
                ),
                "drawModules": sorted(model_draw_modules.get(model_path, set())),
                "animations": animations,
                "animationClipIds": clip_ids,
                "embeddedAnimationClipIds": embedded_clip_ids,
                "resourceId": resource_id,
                "output": output,
            }
        )

    for bib_path in sorted(bib_conditions, key=lambda item: (item.casefold(), item)):
        stem = PurePosixPath(bib_path).stem
        output = f"assets/models/structures/{slug}/bib-{_slug(stem)}.glb"
        resource_id, _animations, _clips = _compile_model(
            bib_path, output=output, bind_animations=False
        )
        bib_states.append(
            {
                "sourceW3d": bib_path,
                "sourceConditionSets": _condition_sets_list(
                    bib_conditions[bib_path]
                ),
                "identifiers": sorted(bib_identifiers.get(bib_path, set())),
                "resourceId": resource_id,
                "output": output,
            }
        )

    for animation_path in sorted(
        set(animation_bindings) - attached_animations,
        key=lambda item: (item.casefold(), item),
    ):
        exclusions.append(
            {
                "kind": "animation",
                "sourceW3d": animation_path,
                "reason": "animation-unattached",
            }
        )

    selected_keys = {path.casefold() for path in selected_w3d}
    textures: set[str] = set()
    for row in _rows(dependency.get("embeddedTextures"), "embedded textures"):
        source = row.get("sourceW3dVirtualPath")
        if not isinstance(source, str) or source.casefold() not in selected_keys:
            continue
        if row.get("status") == "missing":
            # The texture is absent from the retail install (no exact
            # candidate); retail renders the model without that map.  Record
            # the retail-absent reference explicitly instead of guessing a
            # substitute.  Ambiguous or invalid references stay fail-closed.
            exclusions.append(
                {
                    "kind": "texture",
                    "identifier": str(row.get("identifier", "")),
                    "sourceW3d": source,
                    "reason": "retail-absent-texture",
                }
            )
            continue
        if row.get("status") != "resolved":
            raise PlayableStructurePackCompilerError(
                f"selected structure W3D has unresolved texture: {source}"
            )
        textures.update(_paths(row, f"embedded texture {source}"))
    texture_paths = tuple(sorted(textures, key=lambda item: (item.casefold(), item)))
    texture_ids: list[str] = []
    texture_resources: list[dict[str, object]] = []
    for offset in range(0, len(texture_paths), MAX_PATTERNS_PER_RESOURCE):
        batch = texture_paths[offset : offset + MAX_PATTERNS_PER_RESOURCE]
        identifier = _resource_id(
            "structure", slug, f"material-textures-{offset // MAX_PATTERNS_PER_RESOURCE:03d}"
        )
        texture_ids.append(identifier)
        texture_resources.append(
            {
                "id": identifier,
                "kind": "texture",
                "converter": "hash-only",
                "patterns": list(batch),
                "required": True,
                "limit": len(batch),
                "expected_count": len(batch),
            }
        )
    for resource in resources:
        options = resource["options"]
        assert isinstance(options, dict)
        options["inputResourceIds"] = list(texture_ids)

    covered_phases = sorted(
        {phase for state in states for phase in state["phases"]},
        key=LIFECYCLE_PHASE_ORDER.index,
    )
    exclusions.sort(
        key=lambda row: (
            str(row["reason"]),
            str(row["sourceW3d"]).casefold(),
            str(row["sourceW3d"]),
        )
    )
    ui_resources: list[dict[str, object]] = []
    image_bindings: dict[str, str] = {}
    image_binding_metadata: dict[str, dict[str, int]] = {}
    bind_ui_images = resolved_images is not None or image_binding_gaps is not None
    if bind_ui_images:
        ui_resources, image_bindings, image_binding_metadata = _ui_image_resources(
            slug, resolved_images or {}
        )
    gap_rows = sorted(
        (
            {
                "usage": str(row.get("usage", "")),
                "imageId": str(row.get("imageId", "")),
                "reason": str(row.get("reason", "")),
            }
            for row in (image_binding_gaps or ())
        ),
        key=lambda row: (row["usage"], row["imageId"].casefold(), row["reason"]),
    )
    identifiers = [
        str(row["id"]) for row in (*texture_resources, *resources, *ui_resources)
    ]
    if len({value.casefold() for value in identifiers}) != len(identifiers):
        raise PlayableStructurePackCompilerError(
            "structure recipe produced colliding resource ids"
        )

    document: dict[str, object] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "objectId": target_object_id,
        "slug": slug,
        "visualClosureSha256": closure_digest,
        "resources": [*texture_resources, *resources, *ui_resources],
        **(
            {
                "imageBindings": image_bindings,
                "imageBindingMetadata": image_binding_metadata,
                "imageBindingGaps": gap_rows,
            }
            if bind_ui_images
            else {}
        ),
        "lifecycleStates": states,
        "bibStates": bib_states,
        "drawModuleOrder": draw_module_order,
        "phaseCoverage": {
            "covered": covered_phases,
            "missing": [
                phase
                for phase in LIFECYCLE_PHASE_ORDER
                if phase not in covered_phases
            ],
        },
        "exclusions": exclusions,
    }
    document["recipeSha256"] = _digest(document)
    return document


def validate_structure_visual_recipe(value: Mapping[str, object]) -> None:
    """Reject any structure visual recipe that drifted from its evidence."""

    if value.get("schema") != SCHEMA or value.get("schemaVersion") != SCHEMA_VERSION:
        raise PlayableStructurePackCompilerError(
            "structure recipe identity is invalid"
        )
    unsigned = dict(value)
    digest = unsigned.pop("recipeSha256", None)
    if not isinstance(digest, str) or digest != _digest(unsigned):
        raise PlayableStructurePackCompilerError("structure recipe digest is invalid")
    for field in ("objectId", "slug", "visualClosureSha256"):
        if not isinstance(value.get(field), str) or not value[field]:
            raise PlayableStructurePackCompilerError(
                f"structure recipe {field} is invalid"
            )
    if value["slug"] != _slug(str(value["objectId"])):
        raise PlayableStructurePackCompilerError(
            "structure recipe slug does not match its object id"
        )
    resources = _rows(value.get("resources"), "structure recipe resources")
    identifiers = [str(row.get("id", "")) for row in resources]
    if not identifiers or len(
        {item.casefold() for item in identifiers}
    ) != len(identifiers):
        raise PlayableStructurePackCompilerError(
            "structure recipe resource ids are invalid"
        )
    if "imageBindings" in value:
        bindings = value.get("imageBindings")
        metadata = value.get("imageBindingMetadata")
        gaps = value.get("imageBindingGaps")
        if (
            not isinstance(bindings, Mapping)
            or any(
                not isinstance(key, str)
                or not key
                or not isinstance(path, str)
                or not path
                for key, path in bindings.items()
            )
            or not isinstance(metadata, Mapping)
            or not isinstance(gaps, list)
            or any(
                not isinstance(row, Mapping)
                or not isinstance(row.get("usage"), str)
                or not row.get("usage")
                or not isinstance(row.get("reason"), str)
                or not row.get("reason")
                for row in gaps
            )
        ):
            raise PlayableStructurePackCompilerError(
                "structure recipe UI image bindings are invalid"
            )
    resource_ids = {identifier.casefold() for identifier in identifiers}
    states = _rows(value.get("lifecycleStates"), "structure lifecycle states")
    if not states:
        raise PlayableStructurePackCompilerError(
            "structure recipe has no lifecycle states"
        )
    bib_states = value.get("bibStates")
    if not isinstance(bib_states, list):
        raise PlayableStructurePackCompilerError(
            "structure recipe bib states are invalid"
        )
    module_order = value.get("drawModuleOrder")
    if not isinstance(module_order, list) or any(
        not isinstance(module, str) for module in module_order
    ):
        raise PlayableStructurePackCompilerError(
            "structure recipe draw module order is invalid"
        )
    for state in (*states, *bib_states):
        if not isinstance(state, Mapping):
            raise PlayableStructurePackCompilerError(
                "structure lifecycle state is invalid"
            )
        reference = str(state.get("resourceId", ""))
        if reference.casefold() not in resource_ids:
            raise PlayableStructurePackCompilerError(
                "structure lifecycle state references an unknown resource"
            )
        condition_sets = state.get("sourceConditionSets")
        if not isinstance(condition_sets, list) or any(
            not isinstance(conditions, list)
            or any(not isinstance(token, str) for token in conditions)
            for conditions in condition_sets
        ):
            raise PlayableStructurePackCompilerError(
                "structure lifecycle state condition sets are invalid"
            )
    for state in states:
        phases = state.get("phases")
        if not isinstance(phases, list) or any(
            phase not in LIFECYCLE_PHASE_ORDER for phase in phases
        ):
            raise PlayableStructurePackCompilerError(
                "structure lifecycle state phases are invalid"
            )
        clip_ids = state.get("animationClipIds")
        if not isinstance(clip_ids, list) or any(
            not isinstance(clip, str) or not clip for clip in clip_ids
        ):
            raise PlayableStructurePackCompilerError(
                "structure lifecycle state clip ids are invalid"
            )
        draw_modules = state.get("drawModules")
        if not isinstance(draw_modules, list) or any(
            not isinstance(module, str) for module in draw_modules
        ):
            raise PlayableStructurePackCompilerError(
                "structure lifecycle state draw modules are invalid"
            )


RUNTIME_SCHEMA = "openbfme.playable-structure-runtime"
RUNTIME_SCHEMA_VERSION = 0
LIFECYCLE_PRESENTATION_SCHEMA = "openbfme.building-lifecycle-presentation"
LIFECYCLE_PRESENTATION_SCHEMA_VERSION = 1
COMPOSED_EVIDENCE_PROFILE = "composed-structure-runtime"
PRESENTED_PHASE_ORDER = (
    "construction",
    "intact",
    "damaged",
    "really-damaged",
    "collapsing",
    "rubble",
    "post-rubble",
    "post-collapse",
)

_CONSTRUCTION_CONDITIONS = frozenset(
    {
        "AWAITING_CONSTRUCTION",
        "ACTIVELY_BEING_CONSTRUCTED",
        "PARTIALLY_CONSTRUCTED",
        "CONSTRUCTION_COMPLETE",
    }
)
_POST_RUBBLE_CONDITIONS = frozenset({"POST_RUBBLE", "POST_COLLAPSE"})
_ANIMATION_MODE_MAP = {
    "MANUAL": "manual-progress",
    "LOOP": "loop",
    "ONCE": "once",
}
_CANONICAL_PHASE_LABELS = {
    "intact": [[]],
    "damaged": [["DAMAGED"]],
    "really-damaged": [["REALLYDAMAGED"]],
    "collapsing": [["COLLAPSING"]],
    "rubble": [["RUBBLE"]],
    "post-rubble": [["POST_RUBBLE"]],
    "post-collapse": [["POST_COLLAPSE"]],
}


def _runtime_object_id(source_id: str) -> str:
    """Mirror the runtime's camel-splitting bundle id rule exactly."""

    output: list[str] = []
    previous_dash = False
    for index, character in enumerate(source_id):
        code = ord(character)
        is_upper = 65 <= code <= 90
        is_lower = 97 <= code <= 122
        is_digit = 48 <= code <= 57
        if is_upper and index > 0 and not previous_dash:
            previous = ord(source_id[index - 1])
            if 97 <= previous <= 122 or 48 <= previous <= 57:
                output.append("-")
        if is_upper or is_lower or is_digit:
            output.append(character.lower())
            previous_dash = False
        elif not previous_dash and output:
            output.append("-")
            previous_dash = True
    slug = "".join(output).rstrip("-")
    if not slug:
        raise PlayableStructurePackCompilerError(
            f"structure object id has no safe runtime id: {source_id!r}"
        )
    return "bfme2.object." + slug


def _exact_condition_set(conditions: Sequence[str]) -> tuple[str, ...]:
    """Uppercase, order-free authored condition set with no token stripping.

    Canonical phase selection is exact: a state gated by SNOW, UPGRADE_*,
    DOOR_*, USER_*, or any other extra token is a variant/gated visual and
    never competes for a base lifecycle phase slot.
    """

    return tuple(sorted(str(value).upper() for value in conditions))


def _canonical_match(phase: str, condition_set: tuple[str, ...]) -> bool:
    ## Random build variations are engine-picked cosmetic bodies. The composed
    ## presentation deterministically presents variation ONE (retail authors
    ## its models as the default state too); other variations stay packed as
    ## recorded secondary visuals.
    values = set(condition_set) - {"BUILD_VARIATION_ONE"}
    if phase == "construction":
        return bool(values) and values <= _CONSTRUCTION_CONDITIONS
    if phase == "intact":
        return not values
    if phase == "damaged":
        return values == {"DAMAGED"}
    if phase == "really-damaged":
        return values == {"REALLYDAMAGED"}
    if phase == "rubble":
        return values == {"RUBBLE"}
    if phase == "post-rubble":
        return bool(values) and values <= _POST_RUBBLE_CONDITIONS
    return False


def _state_canonical_phases(state: Mapping[str, object]) -> set[str]:
    condition_sets = state.get("sourceConditionSets", [])
    assert isinstance(condition_sets, list)
    result: set[str] = set()
    for phase in LIFECYCLE_PHASE_ORDER:
        if phase not in state["phases"]:
            continue
        if any(
            _canonical_match(phase, _exact_condition_set(conditions))
            for conditions in condition_sets
        ):
            result.add(phase)
    return result


def _primary_draw_module(
    states: Sequence[Mapping[str, object]],
    module_order: Sequence[str],
    notes: list[dict[str, object]],
) -> tuple[Mapping[str, object], ...]:
    """Restrict phase selection to the primary lifecycle draw module.

    Retail structures render several draw modules at once (body, house-color
    banner, doors, upgrade sub-draws, floor bib).  The presenter contract
    carries exactly one body per phase, so the module authoring the widest
    exact-canonical lifecycle coverage is the body; a coverage tie resolves
    to the first-authored module (retail authors the body draw first) and is
    recorded.  Every other module's models stay packed but are recorded as
    unpresented secondary visuals instead of silently competing for phases.
    """

    module_phases: dict[str, set[str]] = {}
    for state in states:
        modules = state.get("drawModules", [])
        assert isinstance(modules, list)
        canonical = _state_canonical_phases(state)
        for module in modules or [""]:
            module_phases.setdefault(str(module), set()).update(canonical)
    if not module_phases:
        raise PlayableStructurePackCompilerError(
            "structure has no lifecycle draw module evidence"
        )
    best_coverage = max(len(phases) for phases in module_phases.values())
    winners = sorted(
        module
        for module, phases in module_phases.items()
        if len(phases) == best_coverage
    )
    if len(winners) > 1:
        ordered = [module for module in module_order if module in winners]
        if len(ordered) != len(winners) or not ordered:
            raise PlayableStructurePackCompilerError(
                "structure primary lifecycle draw module is ambiguous: "
                + ", ".join(winners)
            )
        notes.append(
            {
                "kind": "phase-visual",
                "reason": "draw-module-coverage-tie-first-authored",
                "primaryDrawModule": ordered[0],
                "tiedDrawModules": winners,
            }
        )
        primary = ordered[0]
    else:
        primary = winners[0]
    result: list[Mapping[str, object]] = []
    for state in states:
        modules = state.get("drawModules", [])
        assert isinstance(modules, list)
        if primary in {str(module) for module in modules or [""]}:
            result.append(state)
        else:
            notes.append(
                {
                    "kind": "phase-visual",
                    "reason": "secondary-draw-module-visual",
                    "sourceW3d": str(state.get("sourceW3d", "")),
                    "drawModules": [str(module) for module in modules],
                }
            )
    return tuple(result)


def _select_phase_states(
    states: Sequence[Mapping[str, object]],
    module_order: Sequence[str],
    notes: list[dict[str, object]],
) -> dict[str, Mapping[str, object]]:
    """Pick the canonical recipe state per authored phase or fail closed."""

    primary_states = _primary_draw_module(states, module_order, notes)
    selected: dict[str, Mapping[str, object]] = {}
    for phase in LIFECYCLE_PHASE_ORDER:
        candidates: list[Mapping[str, object]] = []
        for state in primary_states:
            if phase in _state_canonical_phases(state):
                candidates.append(state)
        outputs = {str(state["output"]) for state in candidates}
        if len(outputs) > 1 and phase == "construction":
            # RotWK 2.01 authors distinct pre-placement and buildup bodies
            # (AngmarWallTowerSmall: KBArwWal_A for AWAITING_CONSTRUCTION
            # beside KBArrwWal_A for ACTIVELY_BEING_CONSTRUCTED). The buildup
            # body is what retail presents while the structure is built, so
            # it is the canonical construction visual; the awaiting ghost
            # stays packed as a recorded secondary. Any other multiplicity
            # remains the hard failure below.
            active = [
                state
                for state in candidates
                if any(
                    "ACTIVELY_BEING_CONSTRUCTED" in _exact_condition_set(conditions)
                    for conditions in state.get("sourceConditionSets", [])
                )
            ]
            active_outputs = {str(state["output"]) for state in active}
            if len(active_outputs) == 1:
                notes.append(
                    {
                        "kind": "phase-visual",
                        "phase": "construction",
                        "reason": "actively-being-constructed-canonical",
                        "sourceW3d": str(active[0].get("sourceW3d", "")),
                        "unpresentedOutputs": sorted(
                            outputs - active_outputs
                        ),
                    }
                )
                candidates = active
                outputs = active_outputs
        if len(outputs) > 1:
            raise PlayableStructurePackCompilerError(
                f"structure phase visual is ambiguous among exact canonical "
                f"states: {phase}: " + ", ".join(sorted(outputs))
            )
        if candidates:
            selected[phase] = candidates[0]
    return selected


def _world_builder_intact_fallback(
    states: Sequence[Mapping[str, object]],
    module_order: Sequence[str],
    notes: list[dict[str, object]],
) -> Mapping[str, object] | None:
    """Return the exclusively world-builder-gated intact state, when unique.

    Retail fortresses author ``Model = None`` as their default state and gate
    the real body behind WORLD_BUILDER because retail fortresses are always
    map-placed.  When no exact-canonical intact state exists, one unambiguous
    intact model with a world-builder-gated occurrence is the authored default
    presentation; snow/upgrade-only variants or competing models stay
    fail-closed.
    """

    primary_states = _primary_draw_module(states, module_order, notes)
    candidates = [
        state
        for state in primary_states
        if "intact" in state["phases"]
        and any(
            conditions
            and {str(token).casefold() for token in conditions}
            <= {"world_builder"}
            for conditions in state.get("sourceConditionSets", [])
        )
    ]
    outputs = {str(state["output"]) for state in candidates}
    if len(outputs) != 1:
        return None
    selected = candidates[0]
    notes.append(
        {
            "kind": "phase-visual",
            "phase": "intact",
            "reason": "world-builder-gated-default-visual",
            "sourceW3d": str(selected.get("sourceW3d", "")),
            "drawModules": list(selected.get("drawModules", [])),
        }
    )
    return selected


def _evidence_state_clips(
    state: Mapping[str, object],
) -> list[dict[str, object]]:
    """Group one evidence state's Animation sub-blocks into clip records."""

    groups: dict[tuple[str, ...], dict[str, object]] = {}
    order: list[tuple[str, ...]] = []
    for assignment in state.get("assignments", []):
        if not isinstance(assignment, Mapping):
            continue
        key = str(assignment.get("key", ""))
        if key not in {"AnimationName", "AnimationMode"}:
            continue
        provenance = assignment.get("provenance", {})
        scope = tuple(
            str(part)
            for part in (
                provenance.get("scopePath", [])
                if isinstance(provenance, Mapping)
                else []
            )
        )
        group = groups.get(scope)
        if group is None:
            group = {"names": [], "mode": None}
            groups[scope] = group
            order.append(scope)
        raw = str(assignment.get("rawValue", "")).strip()
        if not raw:
            continue
        if key == "AnimationMode":
            group["mode"] = raw.upper()
        else:
            names = group["names"]
            assert isinstance(names, list)
            names.append(raw.split(".")[-1].lower())
    clips: list[dict[str, object]] = []
    for scope in order:
        group = groups[scope]
        mode = group["mode"]
        names = group["names"]
        assert isinstance(names, list)
        for name in names:
            clips.append(
                {
                    "clip": name,
                    "drawModule": scope[0] if scope else "",
                    "rawMode": mode if isinstance(mode, str) else "ONCE",
                    "modeSource": "authored" if isinstance(mode, str) else (
                        "engine-default-once"
                    ),
                }
            )
    return clips


def _phase_evidence_clips(
    evidence_states: Sequence[Mapping[str, object]],
    phase: str,
) -> tuple[list[dict[str, object]], bool]:
    """Return (clips, from_idle_family) for one lifecycle phase."""

    clips: list[dict[str, object]] = []
    seen: set[tuple[str, str]] = set()
    idle_family = False
    for state in evidence_states:
        conditions = state.get("conditions", [])
        if not isinstance(conditions, list):
            continue
        exact = _exact_condition_set([str(value) for value in conditions])
        if not _canonical_match(phase, exact):
            continue
        family = str(state.get("family", "")).casefold()
        state_clips = _evidence_state_clips(state)
        if state_clips and family == "idleanimationstate":
            idle_family = True
        for clip in state_clips:
            key = (str(clip["clip"]), str(clip["rawMode"]))
            if key in seen:
                continue
            seen.add(key)
            clips.append(clip)
    return clips, idle_family


def _assert_self_referential_construction_clip(
    unbundled: Sequence[Mapping[str, str]],
    *,
    phase_model_source: str,
    embedded_clip_ids: frozenset[str],
) -> None:
    """Fail closed, precisely, on an unresolved self-referential build-up.

    Retail authors a structure's construction animation two ways. The split
    shape names a separate asset (``GBBarracks_ASKL.GBBarracks_ABLD``, motion
    in gbbarracks_abld.w3d). The embedded shape names the construction model
    as its own clip (``GBWell_A.GBWell_A``) and keys the motion inside that
    model's own W3D as compressed animation channels; the conversion declares
    the model as its own animation source and bundles the captured clip.

    Reaching this function means an embedded-shape name did not resolve, so
    the clip is genuinely unusable rather than merely unbound. Say which of
    the two ways it failed instead of reporting a bare clip count.
    """

    stem = PurePosixPath(phase_model_source).stem.casefold()
    if not stem:
        return
    manual_unbundled = sorted(
        {
            str(row["clip"])
            for row in unbundled
            if str(row.get("rawMode", "")).upper() == "MANUAL"
        }
    )
    if manual_unbundled != [stem]:
        return
    if stem in embedded_clip_ids:
        # The model keys motion, but externally attached clips displaced it in
        # the conversion, so which clip drives the build is ambiguous.
        raise PlayableStructurePackCompilerError(
            "structure construction phase names a self-referential MANUAL clip "
            "whose keyed embedded animation was displaced by externally bound "
            "clips: " + stem
        )
    raise PlayableStructurePackCompilerError(
        "structure construction phase names a self-referential MANUAL clip "
        "whose model embeds no keyed animation channels: " + stem
    )


def _phase_animation(
    evidence_states: Sequence[Mapping[str, object]],
    phase: str,
    bundled_clip_ids: set[str],
    notes: list[dict[str, object]],
    state_draw_modules: frozenset[str] = frozenset(),
    phase_model_source: str = "",
    embedded_clip_ids: frozenset[str] = frozenset(),
) -> dict[str, object]:
    """Derive one phase's declared animation from state evidence, fail closed."""

    source_phase = "rubble" if phase == "collapsing" else phase
    clips, idle_family = _phase_evidence_clips(evidence_states, source_phase)
    if phase == "rubble":
        # The rubble-entry clip is presented on the collapsing phase (the Men
        # contract); retained rubble is static.
        return {"clip": None, "mode": "none"}
    available: list[dict[str, object]] = []
    unbundled: list[dict[str, str]] = []
    for clip in clips:
        name = str(clip["clip"])
        if name not in bundled_clip_ids:
            unbundled.append({"clip": name, "rawMode": str(clip["rawMode"])})
            notes.append(
                {
                    "kind": "animation-clip",
                    "phase": phase,
                    "clip": name,
                    "reason": "clip-not-bundled-for-phase-model",
                }
            )
            continue
        raw_mode = str(clip["rawMode"])
        mode = _ANIMATION_MODE_MAP.get(raw_mode)
        if mode is None:
            notes.append(
                {
                    "kind": "animation-clip",
                    "phase": phase,
                    "clip": name,
                    "reason": f"unsupported-animation-mode-{raw_mode.lower()}",
                }
            )
            continue
        available.append(
            {"clip": name, "mode": mode, "drawModule": clip.get("drawModule", "")}
        )
    if phase == "construction":
        manual_clips = [
            clip for clip in available if clip["mode"] == "manual-progress"
        ]
        manual = sorted({str(clip["clip"]) for clip in manual_clips})
        if len(manual) > 1 and state_draw_modules:
            # Retail structures animate construction in every visible draw
            # module (the body and, for example, its door or ent).  The
            # build-progress driver is the clip authored in the selected
            # construction model's own module; secondary-module clips stay
            # bundled but do not compete for the presented phase animation.
            preferred = sorted(
                {
                    str(clip["clip"])
                    for clip in manual_clips
                    if str(clip.get("drawModule", "")).casefold()
                    in state_draw_modules
                }
            )
            if len(preferred) == 1:
                notes.append(
                    {
                        "kind": "animation-clip",
                        "phase": phase,
                        "clip": preferred[0],
                        "reason": "manual-construction-clip-primary-draw-module",
                        "competingClips": manual,
                    }
                )
                manual = preferred
        if not manual:
            _assert_self_referential_construction_clip(
                unbundled,
                phase_model_source=phase_model_source,
                embedded_clip_ids=embedded_clip_ids,
            )
        if len(manual) != 1:
            # `found 0` used to be the symptom of a real converter defect:
            # embedded-shape build-up clips (``GBWell_A.GBWell_A``, motion in
            # the model's own compressed channels) were never registered as
            # bundled, blocking 33 structures across all 7 factions. That was
            # fixed at the source in `clip_ids`; the IsengardTavern case this
            # branch once tolerated now converts with
            # `mbtavern_abld / manual-progress`, and no cooked faction pack
            # records an unbundled MANUAL construction clip. A silent
            # none-mode phase here would only re-hide that class of defect:
            # a structure that never animates its build-up, shipped as exact.
            raise PlayableStructurePackCompilerError(
                "structure construction phase requires exactly one bundled "
                f"MANUAL animation clip, found {len(manual)}"
            )
        return {"clip": manual[0], "mode": "manual-progress"}
    if not available:
        return {"clip": None, "mode": "none"}
    if idle_family:
        names = list(dict.fromkeys(str(clip["clip"]) for clip in available))
        result: dict[str, object] = {"clip": names[0], "mode": "loop-random"}
        if len(names) > 1:
            result["alternateClips"] = names[1:]
        return result
    names = list(dict.fromkeys(str(clip["clip"]) for clip in available))
    if len(names) > 1:
        notes.append(
            {
                "kind": "animation-clip",
                "phase": phase,
                "reason": "ambiguous-phase-clips",
                "clips": names,
            }
        )
        return {"clip": None, "mode": "none"}
    mode = next(
        str(clip["mode"]) for clip in available if str(clip["clip"]) == names[0]
    )
    return {"clip": names[0], "mode": mode}


def _floor_draw_bib(
    recipe_bib_states: Sequence[Mapping[str, object]],
    floor_draws: Sequence[Mapping[str, object]],
    notes: list[dict[str, object]],
) -> dict[str, object] | None:
    if not recipe_bib_states and not floor_draws:
        return None
    if bool(recipe_bib_states) != bool(floor_draws):
        # Some RotWK wild structures author floor draws without a matching
        # bib model closure (or the reverse) for mine/fissure/trove shapes.
        # Record the mismatch and omit floor-draw binding rather than invent
        # a bib model or fail the whole structure convert.
        notes.append(
            {
                "kind": "floor-draw-bib",
                "reason": "floor-draw-and-bib-presence-disagree",
                "bibStateCount": len(recipe_bib_states),
                "floorDrawCount": len(floor_draws),
            }
        )
        return None

    def _correlated_draws(state: Mapping[str, object]) -> list[Mapping[str, object]]:
        # The authored link between a floor draw and its bib model is the
        # draw's own ModelName assignment; module tags are not part of it.
        identifiers = {
            str(identifier).casefold()
            for identifier in state.get("identifiers", [])
        }
        if not identifiers:
            notes.append(
                {
                    "kind": "floor-draw-bib",
                    "reason": "bib-state-missing-identifiers",
                }
            )
            return []
        correlated = [
            draw
            for draw in floor_draws
            if any(
                str(assignment.get("key", "")) == "ModelName"
                and str(assignment.get("rawValue", "")).strip().casefold()
                in identifiers
                for assignment in draw.get("assignments", [])
                if isinstance(assignment, Mapping)
            )
        ]
        if not correlated:
            notes.append(
                {
                    "kind": "floor-draw-bib",
                    "reason": "bib-state-uncorrelated-to-floor-draw",
                    "identifiers": sorted(identifiers),
                }
            )
        return correlated

    def _draw_facts(draw: Mapping[str, object]) -> tuple[bool, set[str]]:
        start_hidden = False
        hide_conditions: set[str] = set()
        for assignment in draw.get("assignments", []):
            if not isinstance(assignment, Mapping):
                continue
            key = str(assignment.get("key", ""))
            raw = str(assignment.get("rawValue", "")).strip()
            if key == "HideIfModelConditions":
                hide_conditions.update(token.upper() for token in raw.split())
            elif key == "StartHidden":
                if raw not in {"Yes", "No"}:
                    raise PlayableStructurePackCompilerError(
                        "structure floor draw StartHidden value is invalid"
                    )
                start_hidden = raw == "Yes"
        return start_hidden, hide_conditions

    candidates = [
        state
        for state in recipe_bib_states
        if any(
            not _exact_condition_set([str(v) for v in conditions])
            for conditions in state.get("sourceConditionSets", [])
        )
    ]
    visible: list[Mapping[str, object]] = []
    for state in candidates:
        if any(not _draw_facts(draw)[0] for draw in _correlated_draws(state)):
            visible.append(state)
        else:
            # Retail floor draws author hidden variant bibs (StartHidden = Yes)
            # beside exactly one visible bib; hidden variants stay packed and
            # are recorded, never presented or silently dropped.
            notes.append(
                {
                    "kind": "bib-visual",
                    "reason": "start-hidden-authored-bib-not-presented",
                    "sourceW3d": str(state.get("sourceW3d", "")),
                    "identifiers": [
                        str(identifier)
                        for identifier in state.get("identifiers", [])
                    ],
                }
            )
    outputs = {str(state["output"]) for state in visible}
    if len(outputs) != 1:
        # Reaching here means the structure authors floor draws AND bib models
        # (the presence-disagreement shapes returned above) yet leaves no
        # single visible bib to present. Retail does not author that: across
        # the pure RotWK 2.01 tree, 0 of 182 objects carrying W3DFloorDraw
        # models have every model StartHidden = Yes, and this branch fires on
        # 0 converted structures in every cooked faction pack. Fail closed —
        # a note here would hand the presenter a bib-less floor draw and call
        # the convert exact.
        evidence_outputs = outputs or {
            str(state["output"]) for state in candidates
        }
        raise PlayableStructurePackCompilerError(
            "structure floor draw bib visual is absent or ambiguous: "
            + ", ".join(sorted(evidence_outputs))
        )
    selected = visible[0]
    hide_conditions: set[str] = set()
    start_hidden = True
    draw_modules: set[str] = set()
    for draw in _correlated_draws(selected):
        draw_modules.add(str(draw.get("moduleKind", "")))
        draw_hidden, draw_hide_conditions = _draw_facts(draw)
        start_hidden = start_hidden and draw_hidden
        hide_conditions.update(draw_hide_conditions)
    during_construction = not (
        {"AWAITING_CONSTRUCTION", "PARTIALLY_CONSTRUCTED"} & hide_conditions
    )
    return {
        "drawModule": "/".join(sorted(draw_modules)),
        "duringConstruction": during_construction,
        "hideIfModelConditions": sorted(hide_conditions),
        "sourceConditions": [],
        "startHiddenAuthored": start_hidden,
        "visibility": "condition-driven-authored-floor-draw",
        "visual": {
            "mode": "glb",
            "glb": str(selected["output"]),
            "modelResourceId": str(selected["resourceId"]),
        },
    }


def _scalar_number(
    descriptor: Mapping[str, object], field: str
) -> float:
    gameplay = descriptor.get("gameplay")
    assert isinstance(gameplay, Mapping)
    scalar_fields = gameplay.get("scalarFields")
    row = (
        scalar_fields.get(field) if isinstance(scalar_fields, Mapping) else None
    )
    if not isinstance(row, Mapping):
        raise PlayableStructurePackCompilerError(
            f"structure descriptor lacks a {field} scalar field"
        )
    raw = row.get("value", row.get("expression"))
    try:
        value = float(str(raw).strip())
    except ValueError:
        raise PlayableStructurePackCompilerError(
            f"structure {field} is not a resolved number: {raw!r}"
        ) from None
    if not value > 0.0:
        raise PlayableStructurePackCompilerError(
            f"structure {field} is not positive: {raw!r}"
        )
    return value


_COLLAPSE_INTEGER_FIELDS = {
    "MinCollapseDelay": "minCollapseDelayMilliseconds",
    "MaxCollapseDelay": "maxCollapseDelayMilliseconds",
    "MinBurstDelay": "minBurstDelayMilliseconds",
    "MaxBurstDelay": "maxBurstDelayMilliseconds",
    "BigBurstFrequency": "bigBurstFrequency",
    "CollapseHeight": "collapseHeight",
}
_COLLAPSE_FLOAT_FIELDS = {
    "CollapseDamping": "collapseDamping",
    "MaxShudder": "maxShudder",
}


_ANIMATION_SOUND_RE = re.compile(r"^Sound:\s*(?P<event>\S+)\s+(?P<groups>Animation:.+)$")
_ANIMATION_SOUND_GROUP_RE = re.compile(
    r"Animation:\s*(?P<animation>\S+)\s+Frames:\s*(?P<frames>[0-9]+(?:\s+[0-9]+)*)"
)
_MODEL_CONDITION_SOUND_RE = re.compile(
    r"^(?P<condition>.+?)\s+Sound:\s*(?P<event>\S+)$"
)


def _generic_audio_bindings(
    modules: Sequence[Mapping[str, object]],
) -> tuple[dict[str, object], list[dict[str, object]]]:
    """Mirror the sealed Men audio-behavior reading with retail-wide spacing."""

    summary: dict[str, object] = {"collapse": None, "construction": None}
    bindings: list[dict[str, object]] = []
    for module in modules:
        kind = module.get("moduleKind")
        source_object = str(module.get("sourceObject", ""))
        for assignment in module.get("assignments", []):
            if not isinstance(assignment, Mapping):
                continue
            key = assignment.get("key")
            raw = str(assignment.get("rawValue", "")).strip()
            if not raw:
                continue
            if (
                kind == "ModelConditionAudioLoopClientBehavior"
                and key == "ModelCondition"
            ):
                match = _MODEL_CONDITION_SOUND_RE.fullmatch(raw)
                if match is None:
                    raise PlayableStructurePackCompilerError(
                        f"invalid ModelCondition audio value: {raw!r}"
                    )
                event = match.group("event")
                bindings.append(
                    {
                        "eventId": event,
                        "kind": "model-condition-loop",
                        "sourceConditionExpression": match.group("condition"),
                        "sourceObject": source_object,
                    }
                )
                if "RUBBLE" in match.group("condition").upper():
                    summary["collapse"] = event
            elif kind == "CastleMemberBehavior" and key == "BeingBuiltSound":
                bindings.append(
                    {
                        "eventId": raw,
                        "kind": "construction-loop",
                        "sourceObject": source_object,
                    }
                )
                summary["construction"] = raw
            elif kind == "AnimationSoundClientBehavior" and key == "AnimationSound":
                match = _ANIMATION_SOUND_RE.fullmatch(raw)
                if match is None:
                    raise PlayableStructurePackCompilerError(
                        f"invalid AnimationSound value: {raw!r}"
                    )
                # One authored value may bind the same sound to several
                # animation/frame groups on a single line.
                groups = list(
                    _ANIMATION_SOUND_GROUP_RE.finditer(match.group("groups"))
                )
                remainder = _ANIMATION_SOUND_GROUP_RE.sub(
                    "", match.group("groups")
                ).strip()
                if not groups or remainder:
                    raise PlayableStructurePackCompilerError(
                        f"invalid AnimationSound value: {raw!r}"
                    )
                for group in groups:
                    bindings.append(
                        {
                            "animation": group.group("animation"),
                            "eventId": match.group("event"),
                            "frames": [
                                int(value)
                                for value in group.group("frames").split()
                            ],
                            "kind": "animation-frame",
                            "sourceObject": source_object,
                        }
                    )
    return summary, bindings


def _generic_collapse_contract(
    modules: Sequence[Mapping[str, object]],
) -> dict[str, object] | None:
    """Read authored StructureCollapseUpdate facts without requiring the full
    Men field set; unauthored fields stay absent instead of being invented."""

    candidates: list[dict[str, object]] = []
    for module in modules:
        if module.get("moduleKind") != "StructureCollapseUpdate":
            continue
        contract: dict[str, object] = {
            "module": "StructureCollapseUpdate",
            "sourceObject": str(module.get("sourceObject", "")),
            "fxLists": {},
            "exactTotalTimingStatus": "blocked-on-bfme2-runtime-oracle",
        }
        for assignment in module.get("assignments", []):
            if not isinstance(assignment, Mapping):
                continue
            key = str(assignment.get("key", ""))
            raw = str(assignment.get("rawValue", "")).strip()
            if key == "FXList":
                parts = raw.split()
                if len(parts) != 2:
                    raise PlayableStructurePackCompilerError(
                        f"invalid collapse FXList: {raw!r}"
                    )
                fx = contract["fxLists"]
                assert isinstance(fx, dict)
                fx[parts[0].casefold().replace("_", "-")] = parts[1]
            elif key in _COLLAPSE_INTEGER_FIELDS:
                try:
                    contract[_COLLAPSE_INTEGER_FIELDS[key]] = int(raw)
                except ValueError:
                    raise PlayableStructurePackCompilerError(
                        f"invalid collapse {key} value: {raw!r}"
                    ) from None
            elif key in _COLLAPSE_FLOAT_FIELDS:
                try:
                    contract[_COLLAPSE_FLOAT_FIELDS[key]] = float(raw)
                except ValueError:
                    raise PlayableStructurePackCompilerError(
                        f"invalid collapse {key} value: {raw!r}"
                    ) from None
            elif key == "DestroyObjectWhenDone":
                if raw not in {"Yes", "No"}:
                    raise PlayableStructurePackCompilerError(
                        "invalid DestroyObjectWhenDone value"
                    )
                contract["destroyObjectWhenDone"] = raw == "Yes"
        candidates.append(contract)
    if not candidates:
        return None
    payloads = {
        _digest({k: v for k, v in value.items() if k != "sourceObject"})
        for value in candidates
    }
    if len(payloads) != 1:
        raise PlayableStructurePackCompilerError(
            "StructureCollapseUpdate evidence is contradictory"
        )
    return candidates[-1]


def _unique_notes(notes: list[dict[str, object]]) -> list[dict[str, object]]:
    ordered = sorted(
        notes,
        key=lambda row: (
            str(row.get("kind", "")),
            str(row.get("phase", "")),
            str(row.get("reason", "")),
            str(row.get("clip", "")),
        ),
    )
    result: list[dict[str, object]] = []
    for note in ordered:
        if note not in result:
            result.append(note)
    return result


def _phase_row(
    *,
    phase: str,
    condition_sets: list[list[str]],
    visual: Mapping[str, object],
    animation: Mapping[str, object],
    next_phase: str | None,
) -> dict[str, object]:
    return {
        "phase": phase,
        "sourceConditionSets": [list(value) for value in condition_sets],
        "transitionAuthority": "deterministic-simulation",
        "visual": deepcopy(dict(visual)),
        "animation": deepcopy(dict(animation)),
        "nextPhase": next_phase,
    }


def compose_structure_runtime_document(
    descriptor: Mapping[str, object],
    visual_recipe: Mapping[str, object],
    lifecycle_evidence: Mapping[str, object],
) -> dict[str, object]:
    """Join one structure descriptor, visual recipe, and lifecycle evidence
    into a runtime document carrying the presenter-grade version-1
    building-lifecycle presentation."""

    from .playable_structure_compiler import (
        validate_playable_structure_descriptor,
    )
    from .playable_structure_lifecycle_evidence import (
        validate_structure_lifecycle_evidence,
    )
    from .retail_men_lifecycle_profile import (
        _entering_state_fx,
        _particle_attachments,
    )

    validate_playable_structure_descriptor(descriptor)
    validate_structure_visual_recipe(visual_recipe)
    validate_structure_lifecycle_evidence(lifecycle_evidence)
    identities = {
        str(descriptor["objectId"]).casefold(),
        str(visual_recipe["objectId"]).casefold(),
        str(lifecycle_evidence["objectId"]).casefold(),
    }
    if len(identities) != 1:
        raise PlayableStructurePackCompilerError(
            "structure descriptor, visual recipe, and lifecycle evidence "
            "identities differ"
        )

    health_contract = descriptor["gameplay"]["health"]
    if health_contract is None:
        raise PlayableStructurePackCompilerError(
            "foundation-only structures have no runtime lifecycle document"
        )
    health = health_contract["primary"]
    if not isinstance(health, Mapping):
        raise PlayableStructurePackCompilerError(
            "structure descriptor health contract is invalid"
        )
    max_health = health.get("maxHealth")
    damaged = health.get("maxHealthDamaged")
    really_damaged = health.get("maxHealthReallyDamaged")
    if not isinstance(max_health, Mapping):
        raise PlayableStructurePackCompilerError(
            "structure descriptor lacks a resolved MaxHealth"
        )
    has_damage_rule = isinstance(damaged, Mapping) and isinstance(
        really_damaged, Mapping
    )
    if not has_damage_rule and (
        isinstance(damaged, Mapping) or isinstance(really_damaged, Mapping)
    ):
        raise PlayableStructurePackCompilerError(
            "structure descriptor authors only one damage threshold; the "
            "damage state rule cannot be half-proven"
        )

    runtime_id = _runtime_object_id(str(descriptor["objectId"]))
    states = visual_recipe["lifecycleStates"]
    assert isinstance(states, list)
    module_order = visual_recipe.get("drawModuleOrder", [])
    assert isinstance(module_order, list)
    notes: list[dict[str, object]] = []
    if any(
        "BUILD_VARIATION_ONE" in _exact_condition_set(
            [str(token) for token in conditions]
        )
        for state in states
        for conditions in state.get("sourceConditionSets", [])
    ):
        notes.append(
            {
                "kind": "phase-visual",
                "reason": "build-variation-one-presented",
            }
        )
    selected = _select_phase_states(
        states, [str(module) for module in module_order], notes
    )
    intact_state = selected.get("intact")
    if intact_state is None:
        intact_state = _world_builder_intact_fallback(
            states, [str(module) for module in module_order], notes
        )
    if intact_state is None:
        raise PlayableStructurePackCompilerError(
            "structure has no canonical default-state intact visual"
        )
    construction_state = selected.get("construction")
    construction_status: str | None = None
    if construction_state is None:
        # Retail structures that never construct (engine-spawned composites,
        # wall templates, and objects authoring no construction states at
        # all) present a chain starting at intact; the omission is recorded
        # evidence, never a fabricated construction phase.
        production = descriptor["production"]
        assert isinstance(production, Mapping)
        evidence_kind = str(production.get("evidence", ""))
        authored_construction_states = any(
            "construction" in state["phases"] for state in states
        )
        if evidence_kind in {"engine-spawned-composite", "wall-template"}:
            construction_status = f"never-constructed-{evidence_kind}"
        elif not authored_construction_states:
            construction_status = "no-authored-construction-states"
        else:
            raise PlayableStructurePackCompilerError(
                "structure has no dedicated construction visual"
            )
        notes.append(
            {
                "kind": "phase-chain",
                "phase": "construction",
                "reason": construction_status,
            }
        )
    if not has_damage_rule:
        notes.append(
            {
                "kind": "phase-chain",
                "phase": "damaged/really-damaged",
                "reason": "no-authored-damage-thresholds",
            }
        )
    presented_phases = [
        phase
        for phase in PRESENTED_PHASE_ORDER
        if not (phase == "construction" and construction_status is not None)
        and not (
            phase in {"damaged", "really-damaged"} and not has_damage_rule
        )
    ]

    def _visual_for(phase: str) -> tuple[Mapping[str, object], dict[str, object]]:
        state = selected.get(phase)
        if state is not None:
            return state, {
                "mode": "glb",
                "glb": str(state["output"]),
                "modelResourceId": str(state["resourceId"]),
            }
        # SAGE model-condition fallback: with no dedicated state authored, the
        # default (intact) model keeps rendering for this condition.
        notes.append(
            {
                "kind": "phase-visual",
                "phase": phase,
                "reason": "default-model-condition-state-fallback",
            }
        )
        return intact_state, {
            "mode": "glb",
            "glb": str(intact_state["output"]),
            "modelResourceId": str(intact_state["resourceId"]),
            "visualFallback": "default-model-condition-state",
        }

    no_render = {"mode": "no-render", "sourceIdentifier": "None"}
    evidence_states = lifecycle_evidence["visualStates"]
    assert isinstance(evidence_states, list)
    evidence_modules = lifecycle_evidence["runtimeModules"]
    assert isinstance(evidence_modules, list)
    floor_draws = lifecycle_evidence["floorDraws"]
    assert isinstance(floor_draws, list)

    def _bundled_clips(state: Mapping[str, object]) -> set[str]:
        clip_ids = state.get("animationClipIds", [])
        assert isinstance(clip_ids, list)
        return {str(value) for value in clip_ids}

    construction_condition_sets: list[list[str]] = []
    if construction_state is not None:
        construction_condition_sets = [
            [str(token) for token in conditions]
            for conditions in construction_state.get("sourceConditionSets", [])
            if _canonical_match(
                "construction",
                _exact_condition_set([str(token) for token in conditions]),
            )
        ]
        if not construction_condition_sets:
            raise PlayableStructurePackCompilerError(
                "structure lacks exact construction conditions"
            )

    phase_rows: list[dict[str, object]] = []
    for index, phase in enumerate(presented_phases):
        next_phase = (
            presented_phases[index + 1]
            if phase not in {"post-rubble", "post-collapse"}
            else None
        )
        if phase in {"post-rubble", "post-collapse"}:
            state = selected.get("post-rubble") if phase == "post-rubble" else None
            if state is not None:
                visual: dict[str, object] = {
                    "mode": "glb",
                    "glb": str(state["output"]),
                    "modelResourceId": str(state["resourceId"]),
                }
            else:
                visual = dict(no_render)
            phase_rows.append(
                _phase_row(
                    phase=phase,
                    condition_sets=_CANONICAL_PHASE_LABELS[phase],
                    visual=visual,
                    animation={"clip": None, "mode": "none"},
                    next_phase=None,
                )
            )
            continue
        if phase == "collapsing":
            state, visual = _visual_for("rubble")
        else:
            state, visual = _visual_for(phase)
        animation = _phase_animation(
            evidence_states,
            phase,
            _bundled_clips(state),
            notes,
            state_draw_modules=frozenset(
                str(module).casefold()
                for module in state.get("drawModules", [])
            ),
            phase_model_source=str(state.get("sourceW3d", "")),
            embedded_clip_ids=frozenset(
                str(clip).casefold()
                for clip in state.get("embeddedAnimationClipIds", [])
            ),
        )
        condition_sets = (
            construction_condition_sets
            if phase == "construction"
            else _CANONICAL_PHASE_LABELS[phase]
        )
        phase_rows.append(
            _phase_row(
                phase=phase,
                condition_sets=condition_sets,
                visual=visual,
                animation=animation,
                next_phase=next_phase,
            )
        )

    construction_animation: Mapping[str, object] | None = next(
        (
            row["animation"]
            for row in phase_rows
            if row["phase"] == "construction"
            and isinstance(row["animation"], Mapping)
        ),
        None,
    )

    collapse = _generic_collapse_contract(
        [module for module in evidence_modules if isinstance(module, Mapping)]
    )
    if collapse is not None:
        collapse_facts: dict[str, object] = deepcopy(collapse)
        collapse_update_fx = deepcopy(collapse["fxLists"])
        terminal = (
            "destroy-object-when-collapse-done"
            if collapse.get("destroyObjectWhenDone") is True
            else "retained-until-explicit-destruction"
        )
    else:
        collapse_facts = {
            "module": None,
            "status": "no-authored-structure-collapse-update",
        }
        collapse_update_fx = {}
        terminal = "retained-until-explicit-destruction"

    entering_fx, entering_records = _entering_state_fx(
        [state for state in evidence_states if isinstance(state, Mapping)]
    )
    particles = _particle_attachments(
        [state for state in evidence_states if isinstance(state, Mapping)]
    )
    audio_events, audio_bindings = _generic_audio_bindings(
        [module for module in evidence_modules if isinstance(module, Mapping)]
    )

    bib_states = visual_recipe.get("bibStates", [])
    assert isinstance(bib_states, list)
    bib = _floor_draw_bib(bib_states, floor_draws, notes)

    simulation_facts: dict[str, object] = {
        "maximumHealth": max_health["value"],
        "collapse": collapse_facts,
        "postRubble": {"terminalDuration": terminal},
    }
    if has_damage_rule:
        assert isinstance(damaged, Mapping)
        assert isinstance(really_damaged, Mapping)
        simulation_facts["damageStateRule"] = {
            "damagedThreshold": damaged["value"],
            "reallyDamagedThreshold": really_damaged["value"],
        }
    else:
        simulation_facts["damageStateRuleStatus"] = (
            "no-authored-damage-thresholds"
        )
    if construction_status is None:
        assert construction_animation is not None
        simulation_facts["construction"] = {
            "buildTimeSeconds": _scalar_number(descriptor, "BuildTime"),
            "animationMode": "MANUAL",
            "animation": construction_animation["clip"],
        }
    else:
        simulation_facts["construction"] = {"status": construction_status}

    lifecycle: dict[str, object] = {
        "schema": LIFECYCLE_PRESENTATION_SCHEMA,
        "schemaVersion": LIFECYCLE_PRESENTATION_SCHEMA_VERSION,
        "evidenceProfile": COMPOSED_EVIDENCE_PROFILE,
        "objectId": runtime_id,
        "initialPhase": "intact",
        "phases": phase_rows,
        "phaseCoverage": deepcopy(visual_recipe["phaseCoverage"]),
        "bib": bib,
        "audioEvents": {
            "collapse": audio_events.get("collapse"),
            "construction": audio_events.get("construction"),
        },
        "audioBindings": audio_bindings,
        "effects": {
            "collapseUpdateFx": collapse_update_fx,
            "definitionTranslationStatus": (
                "requires-exact-definition-runtime-binding"
            ),
            "enteringStateFx": entering_fx,
            "enteringStateBindings": entering_records,
            "particleAttachments": particles,
        },
        "simulationFacts": simulation_facts,
        "rebuildHole": None,
        "compositionExclusions": _unique_notes(notes),
    }

    document: dict[str, object] = {
        "schema": RUNTIME_SCHEMA,
        "schemaVersion": RUNTIME_SCHEMA_VERSION,
        "objectId": descriptor["objectId"],
        "slug": visual_recipe["slug"],
        "descriptorSha256": descriptor["descriptorSha256"],
        "recipeSha256": visual_recipe["recipeSha256"],
        "lifecycleEvidenceSha256": lifecycle_evidence["evidenceSha256"],
        "registration": {
            "production": deepcopy(descriptor["production"]),
            "gameplay": deepcopy(descriptor["gameplay"]),
            "presentation": {
                "buildingLifecycle": lifecycle,
                "ui": deepcopy(descriptor["presentation"]["ui"]),
                "audioRoutes": deepcopy(descriptor["presentation"]["audioRoutes"]),
                # Converted construct-button / selection-portrait crops (and
                # the explicit rows for images the census could not resolve),
                # carried from the visual recipe so HUD consumers bind this
                # faction's own art or keep an honest text-only socket.
                **(
                    {
                        "imageBindings": deepcopy(visual_recipe["imageBindings"]),
                        "imageBindingMetadata": deepcopy(
                            visual_recipe["imageBindingMetadata"]
                        ),
                        "imageBindingGaps": deepcopy(
                            visual_recipe["imageBindingGaps"]
                        ),
                    }
                    if "imageBindings" in visual_recipe
                    else {}
                ),
            },
            "unsupportedVisualReferences": deepcopy(visual_recipe["exclusions"]),
        },
    }
    if "compositeRole" in descriptor:
        document["compositeRole"] = descriptor["compositeRole"]
    document["runtimeSha256"] = _digest(document)
    return document


__all__ = [
    "COMPOSED_EVIDENCE_PROFILE",
    "LIFECYCLE_PHASE_ORDER",
    "LIFECYCLE_PRESENTATION_SCHEMA",
    "PRESENTED_PHASE_ORDER",
    "PlayableStructurePackCompilerError",
    "RUNTIME_SCHEMA",
    "RUNTIME_SCHEMA_VERSION",
    "SCHEMA",
    "SCHEMA_VERSION",
    "compile_structure_visual_recipe",
    "compose_structure_runtime_document",
    "validate_structure_visual_recipe",
]
