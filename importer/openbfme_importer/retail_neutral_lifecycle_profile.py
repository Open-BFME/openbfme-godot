"""Plan the exact Fords neutral-building lifecycle conversion batch.

This module is deliberately a planner, not a second building runtime.  It
accepts the sealed Fords unresolved-object census plus an effective-assets
tree, proves the selected retail bytes against the census, and emits:

* twenty state/bib GLB resources from twenty-two unique W3D inputs;
* exact one-source hash-only model/weather texture owners;
* thirteen shared one-source hash-only audio evidence owners;
* twenty-seven neutral-specific converted audio owners;
* explicit lifecycle-structure map bindings; and
* normalized presentation states, including source-authored no-render states.

The plan binds directly proven health thresholds, unconditional bib behavior,
and rebuild-hole facts. It does not promote the remaining qualified collapse
timing, Inn death timing, capture-link inference, or cross-plan handoff into
runtime truth.
"""

from __future__ import annotations

import argparse
from copy import deepcopy
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import tempfile
from typing import Any, Iterable, Mapping, Sequence

from .big import sha256_file
from .paths import safe_relative_parts
from .profile import ImportProfile
from .sage_audio import (
    parse_sage_audio_definitions,
    resolve_audio_sample_paths,
    resolve_sage_audio_closure,
)
from .util import write_json_atomic


NEUTRAL_LIFECYCLE_PLAN_SCHEMA = "openbfme.retail-neutral-lifecycle-plan"
NEUTRAL_LIFECYCLE_PLAN_SCHEMA_VERSION = 0
CENSUS_SCHEMA = "openbfme.fords-unresolved-object-census"
CENSUS_SCHEMA_VERSION = 1
SIMULATION_FACTS_SCHEMA = "openbfme.neutral-simulation-facts"
SIMULATION_FACTS_SCHEMA_VERSION = 1

_MAX_CENSUS_BYTES = 64 * 1024 * 1024
_MAX_SIMULATION_FACTS_BYTES = 4 * 1024 * 1024
_HEX_SHA256 = re.compile(r"^[0-9a-f]{64}$")
_EXPECTED_RESOURCE_COUNT = 84
_EXPECTED_MODEL_RESOURCE_COUNT = 22
_EXPECTED_TEXTURE_RESOURCE_COUNT = 22
_EXPECTED_AUDIO_RESOURCE_COUNT = 40
_EXPECTED_W3D_SOURCE_COUNT = 24
_EXPECTED_TEXTURE_SOURCE_COUNT = 22
_EXPECTED_AUDIO_SOURCE_COUNT = 40
_EXPECTED_PLACEMENT_COUNT = 8
_EXPECTED_PHASE_COUNT = 26
_EXPECTED_NO_RENDER_PHASE_COUNT = 6

_NEUTRAL_RUNTIME_AUDIO_ROOT_IDS = (
    "BuildingWargPitVoxSingles",
    "CivilianInnSelect",
    "CreepBuildingGenericSelect",
    "WargLairBuildingSelect",
)


@dataclass(frozen=True, slots=True)
class ModelSpec:
    resource_id: str
    role: str
    sources: tuple[str, ...]
    model_source: str
    hierarchy_source: str
    animation_sources: tuple[str, ...]
    authored_animation_ids: tuple[str, ...]
    converter: str
    output: str
    texture_group_ids: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class PhaseSpec:
    phase: str
    source_conditions: tuple[tuple[str, ...], ...]
    source_identifier: str
    model_resource_id: str | None
    clip: str | None
    clip_mode: str
    next_phase: str | None
    visual_mode: str = "glb"
    visual_usage: str = "model"


@dataclass(frozen=True, slots=True)
class AttachmentSpec:
    source_conditions: tuple[str, ...]
    bone: str
    particle_system_id: str
    options: tuple[str, ...] = ()


@dataclass(frozen=True, slots=True)
class RebuildHoleSpec:
    type_name: str
    object_source: str
    model: ModelSpec
    maximum_health: float
    fade_in_seconds: float
    health_regen_percent_per_second: float


@dataclass(frozen=True, slots=True)
class StructureSpec:
    type_name: str
    slug: str
    object_id: str
    object_source: str
    placement_count: int
    source_virtual_model: str
    models: tuple[ModelSpec, ...]
    phases: tuple[PhaseSpec, ...]
    bib_resource_id: str
    texture_groups: tuple[tuple[str, tuple[str, ...]], ...]
    audio_routes: tuple[tuple[str, tuple[str, ...]], ...]
    audio_semantics: tuple[tuple[str, str | None], ...]
    entering_state_fx: tuple[tuple[str, str], ...]
    collapse_update_fx: tuple[tuple[str, str], ...]
    attachments: tuple[AttachmentSpec, ...]
    rebuild_hole: RebuildHoleSpec | None
    secondary_skin_sources: tuple[str, ...] = ()


_SHARED_AUDIO_ROUTES = (
    (
        "BuildingLightDamageStone",
        (
            "data/audio/sounds/wibuild_damar1a.wav",
            "data/audio/sounds/wibuild_damar1b.wav",
            "data/audio/sounds/wibuild_damar1c.wav",
        ),
    ),
    (
        "BuildingHeavyDamageStone",
        (
            "data/audio/sounds/wibuild_damar2a.wav",
            "data/audio/sounds/wibuild_damar2b.wav",
            "data/audio/sounds/wibuild_damar2c.wav",
        ),
    ),
    (
        "BuildingSink",
        (
            "data/audio/sounds/wbgener_sink2_a.wav",
            "data/audio/sounds/wbgener_sink2_b.wav",
            "data/audio/sounds/wbgener_sink2_c.wav",
        ),
    ),
)

_WARG_AMBIENT_SUFFIXES = (
    "aa",
    "ab",
    "ac",
    "ad",
    "ae",
    "af",
    "ag",
    "ah",
    "aj",
    "ak",
    "al",
    "am",
    "an",
    "ao",
    "ap",
    "aq",
    "ar",
    "as",
    "at",
    "au",
    "av",
    "ax",
    "az",
    "ba",
)


def _model(
    structure_slug: str,
    role: str,
    sources: Sequence[str],
    *,
    model_source: str | None = None,
    hierarchy_source: str | None = None,
    animations: Sequence[str] = (),
    authored_animation_ids: Sequence[str] = (),
    converter: str,
    texture_groups: Sequence[str],
) -> ModelSpec:
    source_tuple = tuple(sources)
    if not source_tuple:
        raise AssertionError("model specification has no source")
    model_path = model_source or source_tuple[0]
    return ModelSpec(
        resource_id=f"neutral-{structure_slug}-{role}",
        role=role,
        sources=source_tuple,
        model_source=model_path,
        hierarchy_source=hierarchy_source or model_path,
        animation_sources=tuple(animations),
        authored_animation_ids=tuple(authored_animation_ids),
        converter=converter,
        output=(f"assets/models/structures/neutral-{structure_slug}/{role}.glb"),
        texture_group_ids=tuple(texture_groups),
    )


def _phase(
    phase: str,
    conditions: Sequence[Sequence[str]],
    identifier: str,
    resource_id: str | None,
    *,
    clip: str | None = None,
    clip_mode: str = "none",
    next_phase: str | None = None,
    visual_mode: str = "glb",
    usage: str = "model",
) -> PhaseSpec:
    return PhaseSpec(
        phase=phase,
        source_conditions=tuple(tuple(value) for value in conditions),
        source_identifier=identifier,
        model_resource_id=resource_id,
        clip=clip,
        clip_mode=clip_mode,
        next_phase=next_phase,
        visual_mode=visual_mode,
        visual_usage=usage,
    )


_CAVE_TEXTURE_GROUPS = (
    (
        "neutral-cave-troll-lair-textures-base",
        ("art/compiledtextures/nb/nbtrolllair.dds",),
    ),
    (
        "neutral-cave-troll-lair-textures-normal",
        ("art/compiledtextures/nb/nbtrolllair_nrm.tga",),
    ),
    (
        "neutral-cave-troll-lair-textures-damage",
        ("art/compiledtextures/nb/nbtrolllair_d2.dds",),
    ),
    (
        "neutral-cave-troll-lair-textures-bib",
        ("art/compiledtextures/nb/nbtrolllairbib.dds",),
    ),
    (
        "neutral-cave-troll-lair-textures-weather",
        (
            "art/compiledtextures/nb/nbtrolllair_snow.dds",
            "art/compiledtextures/nb/nbtrolllairbib_snow.dds",
        ),
    ),
)

_INN_TEXTURE_GROUPS = (
    (
        "neutral-inn-textures-base",
        ("art/compiledtextures/nb/nbinn.dds",),
    ),
    (
        "neutral-inn-textures-normal",
        ("art/compiledtextures/nb/nbinn_nrm.tga",),
    ),
    (
        "neutral-inn-textures-ambient-materials",
        (
            "art/compiledtextures/ex/excloudrs04.dds",
            "art/compiledtextures/rb/rbwell_waterb.dds",
        ),
    ),
    (
        "neutral-inn-textures-occupants",
        (
            "art/compiledtextures/gu/gutownsman_02.dds",
            "art/compiledtextures/gu/gutownwmn.dds",
        ),
    ),
    (
        "neutral-inn-textures-damage",
        ("art/compiledtextures/nb/nbinn_d1.dds",),
    ),
    (
        "neutral-inn-textures-bib",
        ("art/compiledtextures/nb/nbinn_bib.dds",),
    ),
    (
        "neutral-inn-textures-rubble",
        ("art/compiledtextures/gb/gbgenrubble.dds",),
    ),
    (
        "neutral-inn-textures-weather",
        ("art/compiledtextures/nb/nbinn_snow.dds",),
    ),
)

_WARG_TEXTURE_GROUPS = (
    (
        "neutral-warg-lair-textures-base",
        ("art/compiledtextures/nb/nbwarglair.dds",),
    ),
    (
        "neutral-warg-lair-textures-normal",
        ("art/compiledtextures/nb/nbwarglair_nrm.tga",),
    ),
    (
        "neutral-warg-lair-textures-damage",
        ("art/compiledtextures/nb/nbwarglair_d1.dds",),
    ),
    (
        "neutral-warg-lair-textures-bib",
        ("art/compiledtextures/nb/nbwarglairbib.dds",),
    ),
    (
        "neutral-warg-lair-textures-weather",
        (
            "art/compiledtextures/nb/nbwarglair_snow.dds",
            "art/compiledtextures/nb/nbwarglairbib_snow.dds",
        ),
    ),
)


_CAVE_MODELS = (
    _model(
        "cave-troll-lair",
        "construction",
        ("art/w3d/nb/nbtrolllair_a.w3d",),
        animations=("art/w3d/nb/nbtrolllair_a.w3d",),
        authored_animation_ids=("NBTROLLLAIR_A.NBTROLLLAIR_A",),
        converter="w3d-bundle",
        texture_groups=(
            "neutral-cave-troll-lair-textures-base",
            "neutral-cave-troll-lair-textures-normal",
        ),
    ),
    _model(
        "cave-troll-lair",
        "intact",
        ("art/w3d/nb/nbtrolllair.w3d",),
        converter="w3d-hierarchical",
        texture_groups=(
            "neutral-cave-troll-lair-textures-base",
            "neutral-cave-troll-lair-textures-normal",
        ),
    ),
    _model(
        "cave-troll-lair",
        "damaged",
        ("art/w3d/nb/nbtrolllair_d1.w3d",),
        converter="w3d-hierarchical",
        texture_groups=(
            "neutral-cave-troll-lair-textures-damage",
            "neutral-cave-troll-lair-textures-normal",
        ),
    ),
    _model(
        "cave-troll-lair",
        "really-damaged",
        ("art/w3d/nb/nbtrolllair_d2.w3d",),
        animations=("art/w3d/nb/nbtrolllair_d2.w3d",),
        authored_animation_ids=("NBTROLLLAIR_D2.NBTROLLLAIR_D2",),
        converter="w3d-bundle",
        texture_groups=(
            "neutral-cave-troll-lair-textures-damage",
            "neutral-cave-troll-lair-textures-normal",
        ),
    ),
    _model(
        "cave-troll-lair",
        "collapse",
        ("art/w3d/nb/nbtrolllair_d3.w3d",),
        animations=("art/w3d/nb/nbtrolllair_d3.w3d",),
        authored_animation_ids=("NBTROLLLAIR_D3.NBTROLLLAIR_D3",),
        converter="w3d-bundle",
        texture_groups=(
            "neutral-cave-troll-lair-textures-damage",
            "neutral-cave-troll-lair-textures-normal",
        ),
    ),
    _model(
        "cave-troll-lair",
        "bib",
        ("art/w3d/nb/nbtrolll_bib.w3d",),
        converter="w3d-hierarchical",
        texture_groups=("neutral-cave-troll-lair-textures-bib",),
    ),
)

_INN_MODELS = (
    _model(
        "inn",
        "construction",
        ("art/w3d/nb/nbinn_a.w3d",),
        animations=("art/w3d/nb/nbinn_a.w3d",),
        authored_animation_ids=("NBINN_A.NBINN_A",),
        converter="w3d-bundle",
        texture_groups=(
            "neutral-inn-textures-base",
            "neutral-inn-textures-normal",
            "neutral-inn-textures-ambient-materials",
        ),
    ),
    _model(
        "inn",
        "intact",
        (
            "art/w3d/nb/nbinn_skn.w3d",
            "art/w3d/nb/nbinn_skl.w3d",
            "art/w3d/nb/nbinn_idla.w3d",
        ),
        model_source="art/w3d/nb/nbinn_skn.w3d",
        hierarchy_source="art/w3d/nb/nbinn_skl.w3d",
        animations=("art/w3d/nb/nbinn_idla.w3d",),
        authored_animation_ids=("NBINN_SKL.NBINN_IDLA",),
        converter="w3d-bundle",
        texture_groups=(
            "neutral-inn-textures-base",
            "neutral-inn-textures-normal",
            "neutral-inn-textures-ambient-materials",
            "neutral-inn-textures-occupants",
        ),
    ),
    _model(
        "inn",
        "damaged",
        ("art/w3d/nb/nbinn_d1.w3d",),
        converter="w3d-hierarchical",
        texture_groups=(
            "neutral-inn-textures-damage",
            "neutral-inn-textures-normal",
            "neutral-inn-textures-ambient-materials",
        ),
    ),
    _model(
        "inn",
        "really-damaged",
        ("art/w3d/nb/nbinn_d2.w3d",),
        animations=("art/w3d/nb/nbinn_d2.w3d",),
        authored_animation_ids=("NBINN_D2.NBINN_D2",),
        converter="w3d-bundle",
        texture_groups=(
            "neutral-inn-textures-damage",
            "neutral-inn-textures-normal",
        ),
    ),
    _model(
        "inn",
        "collapse",
        ("art/w3d/nb/nbinn_d3.w3d",),
        animations=("art/w3d/nb/nbinn_d3.w3d",),
        authored_animation_ids=("NBINN_D3.NBINN_D3",),
        converter="w3d-bundle",
        texture_groups=(
            "neutral-inn-textures-damage",
            "neutral-inn-textures-normal",
        ),
    ),
    _model(
        "inn",
        "rubble",
        ("art/w3d/nb/nbinn_r.w3d",),
        converter="w3d-hierarchical",
        texture_groups=(
            "neutral-inn-textures-damage",
            "neutral-inn-textures-normal",
        ),
    ),
    _model(
        "inn",
        "post-rubble",
        ("art/w3d/gb/gbgenrubble.w3d",),
        converter="w3d-hierarchical",
        texture_groups=("neutral-inn-textures-rubble",),
    ),
    _model(
        "inn",
        "bib",
        ("art/w3d/nb/nbinn_bib.w3d",),
        converter="w3d-hierarchical",
        texture_groups=("neutral-inn-textures-bib",),
    ),
)

_WARG_MODELS = (
    _model(
        "warg-lair",
        "construction",
        ("art/w3d/nb/nbwarglair_a.w3d",),
        animations=("art/w3d/nb/nbwarglair_a.w3d",),
        authored_animation_ids=("NBWARGLAIR_A.NBWARGLAIR_A",),
        converter="w3d-bundle",
        texture_groups=(
            "neutral-warg-lair-textures-base",
            "neutral-warg-lair-textures-normal",
        ),
    ),
    _model(
        "warg-lair",
        "intact",
        ("art/w3d/nb/nbwarglair.w3d",),
        converter="w3d-hierarchical",
        texture_groups=(
            "neutral-warg-lair-textures-base",
            "neutral-warg-lair-textures-normal",
        ),
    ),
    _model(
        "warg-lair",
        "damaged",
        ("art/w3d/nb/nbwarglair_d1.w3d",),
        converter="w3d-hierarchical",
        texture_groups=(
            "neutral-warg-lair-textures-damage",
            "neutral-warg-lair-textures-normal",
        ),
    ),
    _model(
        "warg-lair",
        "really-damaged",
        ("art/w3d/nb/nbwarglair_d2.w3d",),
        animations=("art/w3d/nb/nbwarglair_d2.w3d",),
        authored_animation_ids=("NBWARGLAIR_D2.NBWARGLAIR_D2",),
        converter="w3d-bundle",
        texture_groups=(
            "neutral-warg-lair-textures-damage",
            "neutral-warg-lair-textures-normal",
        ),
    ),
    _model(
        "warg-lair",
        "collapse",
        ("art/w3d/nb/nbwarglair_d3.w3d",),
        animations=("art/w3d/nb/nbwarglair_d3.w3d",),
        authored_animation_ids=("NBWARGLAIR_D3.NBWARGLAIR_D3",),
        converter="w3d-bundle",
        texture_groups=(
            "neutral-warg-lair-textures-damage",
            "neutral-warg-lair-textures-normal",
        ),
    ),
    _model(
        "warg-lair",
        "bib",
        ("art/w3d/nb/nbwarglair_bib.w3d",),
        converter="w3d-hierarchical",
        texture_groups=("neutral-warg-lair-textures-bib",),
    ),
)


_CAVE_PHASES = (
    _phase(
        "construction",
        (
            ("AWAITING_CONSTRUCTION",),
            ("ACTIVELY_BEING_CONSTRUCTED", "PARTIALLY_CONSTRUCTED"),
        ),
        "NBTrollLair_A",
        "neutral-cave-troll-lair-construction",
        clip="nbtrolllair_a",
        clip_mode="manual-progress",
        next_phase="intact",
    ),
    _phase(
        "intact",
        ((),),
        "NBTrollLair",
        "neutral-cave-troll-lair-intact",
        next_phase="damaged",
    ),
    _phase(
        "damaged",
        (("DAMAGED",),),
        "NBTrollLair_D1",
        "neutral-cave-troll-lair-damaged",
        next_phase="really-damaged",
    ),
    _phase(
        "really-damaged",
        (("REALLYDAMAGED",),),
        "NBTrollLair_D2",
        "neutral-cave-troll-lair-really-damaged",
        clip="nbtrolllair_d2",
        clip_mode="once",
        next_phase="collapsing",
    ),
    _phase(
        "collapsing",
        (("COLLAPSING",),),
        "NBTrollLair_D3",
        "neutral-cave-troll-lair-collapse",
        clip="nbtrolllair_d3",
        clip_mode="once",
        next_phase="rubble",
    ),
    _phase(
        "rubble",
        (("RUBBLE",),),
        "None",
        None,
        next_phase="post-rubble",
        visual_mode="no-render",
    ),
    _phase(
        "post-rubble",
        (("POST_RUBBLE",),),
        "NONE",
        None,
        visual_mode="no-render",
    ),
    _phase(
        "post-collapse",
        (("POST_COLLAPSE",),),
        "None",
        None,
        visual_mode="no-render",
    ),
)

_INN_PHASES = (
    _phase(
        "construction",
        (
            ("AWAITING_CONSTRUCTION",),
            ("ACTIVELY_BEING_CONSTRUCTED", "PARTIALLY_CONSTRUCTED"),
        ),
        "NBInn_A",
        "neutral-inn-construction",
        clip="nbinn_a",
        clip_mode="manual-progress",
        next_phase="intact",
    ),
    _phase(
        "intact",
        ((),),
        "NBInn_SKN",
        "neutral-inn-intact",
        clip="nbinn_idla",
        clip_mode="loop",
        next_phase="damaged",
    ),
    _phase(
        "damaged",
        (("DAMAGED",),),
        "NBInn_D1",
        "neutral-inn-damaged",
        next_phase="really-damaged",
    ),
    _phase(
        "really-damaged",
        (("REALLYDAMAGED",),),
        "NBInn_D2",
        "neutral-inn-really-damaged",
        clip="nbinn_d2",
        clip_mode="once",
        next_phase="collapsing",
    ),
    _phase(
        "collapsing",
        (("COLLAPSING",),),
        "NBInn_D3",
        "neutral-inn-collapse",
        clip="nbinn_d3",
        clip_mode="once",
        next_phase="rubble",
    ),
    _phase(
        "rubble",
        (("RUBBLE",),),
        "NBInn_R",
        "neutral-inn-rubble",
        next_phase="post-rubble",
    ),
    _phase(
        "post-rubble",
        (("POST_RUBBLE",),),
        "GBGenRubble",
        "neutral-inn-post-rubble",
    ),
    _phase(
        "post-collapse",
        (("POST_COLLAPSE",),),
        "NBInn_R",
        "neutral-inn-rubble",
    ),
)

_WARG_PHASES = (
    _phase(
        "construction",
        (
            ("AWAITING_CONSTRUCTION",),
            ("ACTIVELY_BEING_CONSTRUCTED", "PARTIALLY_CONSTRUCTED"),
        ),
        "NBWargLair_A",
        "neutral-warg-lair-construction",
        clip="nbwarglair_a",
        clip_mode="manual-progress",
        next_phase="intact",
    ),
    _phase(
        "intact",
        ((),),
        "NBWargLair",
        "neutral-warg-lair-intact",
        next_phase="damaged",
    ),
    _phase(
        "damaged",
        (("DAMAGED",),),
        "NBWargLair_D1",
        "neutral-warg-lair-damaged",
        next_phase="really-damaged",
    ),
    _phase(
        "really-damaged",
        (("REALLYDAMAGED",),),
        "NBWargLair_D2",
        "neutral-warg-lair-really-damaged",
        clip="nbwarglair_d2",
        clip_mode="once",
        next_phase="collapsing",
    ),
    _phase(
        "collapsing",
        (("COLLAPSING",),),
        "NBWargLair_D3",
        "neutral-warg-lair-collapse",
        clip="nbwarglair_d3",
        clip_mode="once",
        next_phase="rubble",
    ),
    _phase(
        "rubble",
        (("RUBBLE",),),
        "None",
        None,
        next_phase="post-rubble",
        visual_mode="no-render",
    ),
    _phase(
        "post-rubble",
        (("POST_RUBBLE",),),
        "NONE",
        None,
        visual_mode="no-render",
    ),
    _phase(
        "post-collapse",
        (("POST_COLLAPSE",),),
        "None",
        None,
        visual_mode="no-render",
    ),
)


_STRUCTURES = (
    StructureSpec(
        type_name="CaveTrollLair",
        slug="cave-troll-lair",
        object_id="bfme2.object.neutral-cave-troll-lair",
        object_source="data/ini/object/neutral/cavetrolllair.ini",
        placement_count=2,
        source_virtual_model="art/w3d/nb/nbtrolllair.w3d",
        models=_CAVE_MODELS,
        phases=_CAVE_PHASES,
        bib_resource_id="neutral-cave-troll-lair-bib",
        texture_groups=_CAVE_TEXTURE_GROUPS,
        audio_routes=(
            *_SHARED_AUDIO_ROUTES,
            (
                "CreepBuildingGenericSelect",
                ("data/audio/sounds/nbcreep_selecta.wav",),
            ),
        ),
        audio_semantics=(
            ("select", "CreepBuildingGenericSelect"),
            ("constructionSelect", None),
            ("damaged", "BuildingLightDamageStone"),
            ("reallyDamaged", "BuildingHeavyDamageStone"),
            ("collapse", "BuildingSink"),
            ("ambient", None),
        ),
        entering_state_fx=(
            ("damaged", "FX_BuildingDamaged"),
            ("really-damaged", "FX_BuildingReallyDamaged"),
            ("collapsing", "FX_StructureMediumCollapse"),
        ),
        collapse_update_fx=(
            ("initial", "FX_StructureMediumCollapse"),
            ("almost-final", "FX_StructureAlmostCollapse"),
        ),
        attachments=(
            AttachmentSpec(("POST_RUBBLE",), "NONE", "SmokeBuildingMediumRubble"),
            AttachmentSpec(("POST_COLLAPSE",), "NONE", "SmokeBuildingMediumRubble"),
            AttachmentSpec(
                ("USER_2",),
                "None",
                "UntamedAllegiance",
                ("HouseColor:Yes",),
            ),
            AttachmentSpec(
                ("USER_2",),
                "None",
                "UntamedAllegiance2",
                ("HouseColor:Yes",),
            ),
        ),
        rebuild_hole=RebuildHoleSpec(
            type_name="CaveTrollLairHole",
            object_source="data/ini/object/neutral/holes.ini",
            model=_model(
                "cave-troll-lair",
                "rebuild-hole",
                ("art/w3d/nb/nbtrolllair_r.w3d",),
                converter="w3d-hierarchical",
                texture_groups=(
                    "neutral-cave-troll-lair-textures-damage",
                    "neutral-cave-troll-lair-textures-normal",
                ),
            ),
            maximum_health=500.0,
            fade_in_seconds=2.0,
            health_regen_percent_per_second=0.0,
        ),
    ),
    StructureSpec(
        type_name="Inn",
        slug="inn",
        object_id="bfme2.object.neutral-inn",
        object_source="data/ini/object/neutral/inn.ini",
        placement_count=2,
        source_virtual_model="art/w3d/nb/nbinn_skn.w3d",
        models=_INN_MODELS,
        phases=_INN_PHASES,
        bib_resource_id="neutral-inn-bib",
        texture_groups=_INN_TEXTURE_GROUPS,
        audio_routes=(
            *_SHARED_AUDIO_ROUTES,
            (
                "CivilianInnSelect",
                ("data/audio/sounds/cbinn_selecta.wav",),
            ),
            (
                "BuildingGoodVoiceSelectUnderConstruction",
                (
                    "data/audio/sounds/gubuild_consela.wav",
                    "data/audio/sounds/gubuild_conselb.wav",
                    "data/audio/sounds/gubuild_conselc.wav",
                    "data/audio/sounds/gubuild_conseld.wav",
                ),
            ),
        ),
        audio_semantics=(
            ("select", "CivilianInnSelect"),
            (
                "constructionSelect",
                "BuildingGoodVoiceSelectUnderConstruction",
            ),
            ("damaged", "BuildingLightDamageStone"),
            ("reallyDamaged", "BuildingHeavyDamageStone"),
            ("collapse", "BuildingSink"),
            ("ambient", None),
        ),
        entering_state_fx=(
            ("damaged", "FX_BuildingDamaged"),
            ("really-damaged", "FX_BuildingReallyDamaged"),
            ("collapsing", "FX_StructureMediumCollapse"),
        ),
        collapse_update_fx=(),
        attachments=(
            AttachmentSpec(
                ("ACTIVELY_BEING_CONSTRUCTED", "PARTIALLY_CONSTRUCTED"),
                "DUSTBONE",
                "BuildingContructDust",
            ),
            AttachmentSpec(("RUBBLE",), "SmokeLarge01", "SmokeBuildingLarge"),
        ),
        rebuild_hole=None,
        secondary_skin_sources=(
            "art/w3d/nb/nbinn_skn.w3d",
            "art/w3d/nb/nbinn_d3.w3d",
        ),
    ),
    StructureSpec(
        type_name="WargLair",
        slug="warg-lair",
        object_id="bfme2.object.neutral-warg-lair",
        object_source="data/ini/object/neutral/warglair.ini",
        placement_count=4,
        source_virtual_model="art/w3d/nb/nbwarglair.w3d",
        models=_WARG_MODELS,
        phases=_WARG_PHASES,
        bib_resource_id="neutral-warg-lair-bib",
        texture_groups=_WARG_TEXTURE_GROUPS,
        audio_routes=(
            *_SHARED_AUDIO_ROUTES,
            (
                "WargLairBuildingSelect",
                ("data/audio/sounds/nbwargl_selecta.wav",),
            ),
            (
                "BuildingWargPitVoxSingles",
                tuple(
                    f"data/audio/sounds/euwarg_vox2{suffix}.wav"
                    for suffix in _WARG_AMBIENT_SUFFIXES
                ),
            ),
        ),
        audio_semantics=(
            ("select", "WargLairBuildingSelect"),
            ("constructionSelect", None),
            ("damaged", "BuildingLightDamageStone"),
            ("reallyDamaged", "BuildingHeavyDamageStone"),
            ("collapse", "BuildingSink"),
            ("ambient", "BuildingWargPitVoxSingles"),
        ),
        entering_state_fx=(
            ("damaged", "FX_BuildingDamaged"),
            ("really-damaged", "FX_BuildingReallyDamaged"),
            ("collapsing", "FX_StructureMediumCollapse"),
        ),
        collapse_update_fx=(
            ("initial", "FX_StructureMediumCollapse"),
            ("almost-final", "FX_StructureAlmostCollapse"),
        ),
        attachments=(
            AttachmentSpec(("POST_RUBBLE",), "NONE", "SmokeBuildingMediumRubble"),
            AttachmentSpec(("POST_COLLAPSE",), "NONE", "SmokeBuildingMediumRubble"),
            AttachmentSpec(
                ("USER_2",),
                "None",
                "UntamedAllegiance",
                ("HouseColor:Yes",),
            ),
            AttachmentSpec(
                ("USER_2",),
                "None",
                "UntamedAllegiance2",
                ("HouseColor:Yes",),
            ),
        ),
        rebuild_hole=RebuildHoleSpec(
            type_name="WargLairHole",
            object_source="data/ini/object/neutral/holes.ini",
            model=_model(
                "warg-lair",
                "rebuild-hole",
                ("art/w3d/nb/nbwarglair_r.w3d",),
                converter="w3d-hierarchical",
                texture_groups=(
                    "neutral-warg-lair-textures-damage",
                    "neutral-warg-lair-textures-normal",
                ),
            ),
            maximum_health=500.0,
            fade_in_seconds=2.0,
            health_regen_percent_per_second=0.0,
        ),
        secondary_skin_sources=("art/w3d/nb/nbwarglair_d3.w3d",),
    ),
)


def _canonical_sha256(value: object) -> str:
    try:
        encoded = json.dumps(
            value,
            sort_keys=True,
            ensure_ascii=False,
            separators=(",", ":"),
            allow_nan=False,
        ).encode("utf-8")
    except (TypeError, ValueError) as exc:
        raise ValueError(f"document is not canonical JSON data: {exc}") from exc
    return hashlib.sha256(encoded).hexdigest()


def _mapping(value: object, label: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise ValueError(f"{label} must be an object")
    return value


def _array(value: object, label: str) -> list[Any]:
    if not isinstance(value, list):
        raise ValueError(f"{label} must be an array")
    return value


def _text(value: object, label: str) -> str:
    if not isinstance(value, str) or not value:
        raise ValueError(f"{label} must be a non-empty string")
    return value


def _integer(value: object, label: str, *, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise ValueError(f"{label} must be an integer >= {minimum}")
    return value


def _sha256(value: object, label: str) -> str:
    if not isinstance(value, str) or _HEX_SHA256.fullmatch(value) is None:
        raise ValueError(f"{label} must be a lowercase SHA-256")
    return value


def _safe_virtual_path(value: object, label: str) -> str:
    path = _text(value, label)
    if "\\" in path:
        raise ValueError(f"{label} is not a canonical POSIX path")
    try:
        canonical = "/".join(safe_relative_parts(path))
    except ValueError as exc:
        raise ValueError(f"unsafe {label}: {path!r}") from exc
    if canonical != path:
        raise ValueError(f"{label} is not canonical: {path!r}")
    return path


def _case_unique(values: Iterable[str], label: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for value in values:
        key = value.casefold()
        previous = result.get(key)
        if previous is not None:
            raise ValueError(f"duplicate {label}: {previous!r} and {value!r}")
        result[key] = value
    return result


def _validate_declared_digest(
    document: Mapping[str, Any], field: str, label: str
) -> str:
    declared = _sha256(document.get(field), f"{label} {field}")
    payload = deepcopy(dict(document))
    payload.pop(field, None)
    actual = _canonical_sha256(payload)
    if actual != declared:
        raise ValueError(f"{label} {field} does not match canonical content")
    return declared


def _source_record(value: object, label: str) -> dict[str, Any]:
    source = _mapping(value, label)
    path = _safe_virtual_path(source.get("virtualPath"), f"{label} virtualPath")
    byte_length = _integer(source.get("byteLength"), f"{label} byteLength")
    sha = _sha256(source.get("sha256"), f"{label} sha256")
    archive = _text(source.get("archive"), f"{label} archive")
    precedence = _integer(source.get("precedence"), f"{label} precedence")
    raw_roles: list[Any]
    if "role" in source:
        raw_roles = [source.get("role")]
    else:
        raw_roles = _array(source.get("roles"), f"{label} roles")
    if not raw_roles or any(
        not isinstance(role, str) or not role for role in raw_roles
    ):
        raise ValueError(f"{label} must declare one or more source roles")
    return {
        "virtualPath": path,
        "sha256": sha,
        "byteLength": byte_length,
        "archive": archive,
        "precedence": precedence,
        "roles": sorted(set(raw_roles)),
    }


def _merge_source(
    sources: dict[str, dict[str, Any]], source: dict[str, Any], label: str
) -> None:
    path = str(source["virtualPath"])
    key = path.casefold()
    previous = sources.get(key)
    if previous is not None:
        identity_fields = {
            "virtualPath",
            "sha256",
            "byteLength",
            "archive",
            "precedence",
        }
        if any(previous[field] != source[field] for field in identity_fields):
            raise ValueError(
                f"conflicting census source records for {path!r} ({label})"
            )
        previous["roles"] = sorted({*previous["roles"], *source["roles"]})
        return
    sources[key] = source


def _selected_target_rows(census: Mapping[str, Any]) -> dict[str, Mapping[str, Any]]:
    if census.get("schema") != CENSUS_SCHEMA:
        raise ValueError("unsupported Fords unresolved-object census schema")
    if census.get("schemaVersion") != CENSUS_SCHEMA_VERSION:
        raise ValueError("unsupported Fords unresolved-object census schema version")
    _validate_declared_digest(census, "aggregateSha256", "Fords census")
    selected: dict[str, Mapping[str, Any]] = {}
    expected = {spec.type_name for spec in _STRUCTURES}
    for position, raw_target in enumerate(
        _array(census.get("targets"), "census targets")
    ):
        target = _mapping(raw_target, f"census target {position}")
        object_definition = _mapping(
            target.get("objectDefinition"), f"census target {position} objectDefinition"
        )
        name = _text(
            object_definition.get("name"),
            f"census target {position} objectDefinition.name",
        )
        if name not in expected:
            continue
        if name in selected:
            raise ValueError(f"duplicate selected census target: {name}")
        selected[name] = target
    if set(selected) != expected:
        missing = sorted(expected - set(selected))
        raise ValueError(
            "census is missing selected neutral target(s): " + ", ".join(missing)
        )
    return selected


def _validate_simulation_facts(
    raw: Mapping[str, Any],
) -> tuple[str, dict[str, Mapping[str, Any]], dict[str, Mapping[str, Any]]]:
    facts = _mapping(raw, "neutral simulation facts")
    if facts.get("schema") != SIMULATION_FACTS_SCHEMA:
        raise ValueError("unsupported neutral simulation facts schema")
    if facts.get("schemaVersion") != SIMULATION_FACTS_SCHEMA_VERSION:
        raise ValueError("unsupported neutral simulation facts schema version")
    digest = _canonical_sha256(facts)

    sources: dict[str, Mapping[str, Any]] = {}
    for position, raw_source in enumerate(
        _array(facts.get("sources"), "neutral simulation facts sources")
    ):
        source = _mapping(raw_source, f"neutral simulation source {position}")
        virtual_path = source.get("virtualPath")
        if virtual_path is None:
            continue
        path = _safe_virtual_path(
            virtual_path, f"neutral simulation source {position} virtualPath"
        )
        _sha256(source.get("sha256"), f"neutral simulation source {path} sha256")
        if path.casefold() in sources:
            raise ValueError(f"duplicate neutral simulation source: {path}")
        sources[path.casefold()] = source

    rows: dict[str, Mapping[str, Any]] = {}
    for position, raw_row in enumerate(
        _array(facts.get("structures"), "neutral simulation structures")
    ):
        row = _mapping(raw_row, f"neutral simulation structure {position}")
        type_name = _text(
            row.get("typeName"), f"neutral simulation structure {position} typeName"
        )
        if type_name in rows:
            raise ValueError(f"duplicate neutral simulation structure: {type_name}")
        rows[type_name] = row
    expected_types = {spec.type_name for spec in _STRUCTURES}
    if set(rows) != expected_types:
        raise ValueError("neutral simulation structure partition does not match")

    expected_numbers = {
        "CaveTrollLair": (2000, 1000, 500, 30),
        "Inn": (3000, 2000, 1000, 45),
        "WargLair": (2000, 1000, 500, 30),
    }
    for spec in _STRUCTURES:
        row = rows[spec.type_name]
        maximum, damaged, really_damaged, build_time = expected_numbers[spec.type_name]
        if row.get("maximumHealth") != maximum:
            raise ValueError(f"{spec.type_name} maximumHealth does not match")
        initial = _mapping(row.get("initialHealth"), f"{spec.type_name} initialHealth")
        if dict(initial) != {
            "mapAuthoredPercent": 100,
            "derivedHitPoints": maximum,
            "status": "proven-full-health-map-placement",
        }:
            raise ValueError(f"{spec.type_name} initial health proof does not match")
        damage = _mapping(row.get("damageStateRule"), f"{spec.type_name} damage rule")
        if (
            damage.get("damagedThreshold") != damaged
            or damage.get("reallyDamagedThreshold") != really_damaged
        ):
            raise ValueError(f"{spec.type_name} damage thresholds do not match")
        boundary_status = _text(
            damage.get("boundaryStatus"), f"{spec.type_name} threshold boundary status"
        )
        if "BFME2 executable-oracle confirmation" not in boundary_status:
            raise ValueError(f"{spec.type_name} threshold qualification was lost")
        construction = _mapping(
            row.get("construction"), f"{spec.type_name} construction"
        )
        if (
            construction.get("buildTimeSeconds") != build_time
            or construction.get("animationMode") != "MANUAL"
            or construction.get("awaitingConditions") != ["AWAITING_CONSTRUCTION"]
            or construction.get("activeConditions")
            != ["ACTIVELY_BEING_CONSTRUCTED", "PARTIALLY_CONSTRUCTED"]
        ):
            raise ValueError(f"{spec.type_name} construction facts do not match")
        bib = _mapping(row.get("bib"), f"{spec.type_name} bib")
        if (
            bib.get("drawModule") != "W3DFloorDraw"
            or bib.get("startHiddenAuthored") is not False
            or bib.get("hideIfModelConditions") != []
            or bib.get("constructionVisibility") != "unconditional-authored-floor-draw"
        ):
            raise ValueError(f"{spec.type_name} unconditional bib proof does not match")
        capture = _mapping(
            row.get("captureInitialState"), f"{spec.type_name} capture initial state"
        )
        if (
            capture.get("mapPlacementCount") != spec.placement_count
            or capture.get("initialHealthPercent") != 100
            or capture.get("initialPhase") != "intact"
        ):
            raise ValueError(f"{spec.type_name} map initial state does not match")

        collapse = _mapping(row.get("collapse"), f"{spec.type_name} collapse")
        if spec.type_name == "Inn":
            if (
                collapse.get("module") is not None
                or collapse.get("keepObjectDie") is not True
                or collapse.get("collapsingTimeAuthored") is not None
                or collapse.get("exactTotalTimingStatus")
                != "no-StructureCollapseUpdate-and-bfme2-KeepObjectDie-default-not-proven"
            ):
                raise ValueError("Inn unproven death timing contract does not match")
            post = _mapping(row.get("postRubble"), "Inn postRubble")
            if (
                post.get("rebuildHoleObject") is not None
                or post.get("exactPostRubbleTransitionTimingStatus")
                != "blocked-on-bfme2-KeepObjectDie-runtime-oracle"
            ):
                raise ValueError("Inn post-rubble timing blocker does not match")
        else:
            exact_collapse = {
                "module": "StructureCollapseUpdate",
                "minCollapseDelayMilliseconds": 0,
                "maxCollapseDelayMilliseconds": 0,
                "collapseDamping": 0.5,
                "collapseHeight": 120.0,
                "minBurstDelayMilliseconds": 250,
                "maxBurstDelayMilliseconds": 800,
                "bigBurstFrequency": 4,
                "destroyObjectWhenDone": True,
                "animationFrameCount": 91,
                "animationFrameRate": 30,
                "exactTotalTimingStatus": "blocked-on-bfme2-runtime-oracle",
            }
            for field, expected in exact_collapse.items():
                if collapse.get(field) != expected:
                    raise ValueError(
                        f"{spec.type_name} collapse fact {field} does not match"
                    )
            assert spec.rebuild_hole is not None
            post = _mapping(row.get("postRubble"), f"{spec.type_name} postRubble")
            exact_post = {
                "rebuildHoleObject": spec.rebuild_hole.type_name,
                "rebuildHoleModelVirtualPath": spec.rebuild_hole.model.model_source,
                "rebuildHoleMaximumHealth": spec.rebuild_hole.maximum_health,
                "rebuildHoleFadeInSeconds": spec.rebuild_hole.fade_in_seconds,
                "rebuildHoleHealthRegenPercentPerSecond": (
                    spec.rebuild_hole.health_regen_percent_per_second
                ),
                "terminalDuration": "unbounded-until-rebuild-or-explicit-destruction",
            }
            for field, expected in exact_post.items():
                if post.get(field) != expected:
                    raise ValueError(
                        f"{spec.type_name} rebuild-hole fact {field} does not match"
                    )
            _sha256(
                post.get("rebuildHoleModelSha256"),
                f"{spec.type_name} rebuild-hole model SHA-256",
            )

    required_source_paths = {
        *(spec.object_source for spec in _STRUCTURES),
        "data/ini/object/neutral/holes.ini",
    }
    if any(path.casefold() not in sources for path in required_source_paths):
        raise ValueError("neutral simulation facts are missing required retail sources")
    blocker_codes = {
        _text(
            _mapping(row, "simulation blocker").get("code"), "simulation blocker code"
        )
        for row in _array(facts.get("blockers"), "neutral simulation blockers")
    }
    required_blockers = {
        "bfme2-StructureCollapseUpdate-runtime-not-open-source",
        "bfme2-KeepObjectDie-default-CollapsingTime-not-proven",
        "capture-flag-link-not-explicit-in-decoded-map-record",
    }
    if blocker_codes != required_blockers:
        raise ValueError("neutral simulation blocker set does not match")
    return digest, rows, sources


def _expected_w3d_paths(spec: StructureSpec) -> tuple[str, ...]:
    return tuple(
        sorted(
            {path for model in spec.models for path in model.sources},
            key=lambda value: (value.casefold(), value),
        )
    )


def _all_model_specs(spec: StructureSpec) -> tuple[ModelSpec, ...]:
    if spec.rebuild_hole is None:
        return spec.models
    return (*spec.models, spec.rebuild_hole.model)


def _all_w3d_paths(spec: StructureSpec) -> tuple[str, ...]:
    return tuple(
        sorted(
            {path for model in _all_model_specs(spec) for path in model.sources},
            key=lambda value: (value.casefold(), value),
        )
    )


def _texture_paths(spec: StructureSpec) -> tuple[str, ...]:
    return tuple(
        sorted(
            {path for _, paths in spec.texture_groups for path in paths},
            key=lambda value: (value.casefold(), value),
        )
    )


def _audio_paths(spec: StructureSpec) -> tuple[str, ...]:
    return tuple(
        sorted(
            {path for _, paths in spec.audio_routes for path in paths},
            key=lambda value: (value.casefold(), value),
        )
    )


def _neutral_runtime_audio_paths() -> tuple[str, ...]:
    roots = set(_NEUTRAL_RUNTIME_AUDIO_ROOT_IDS)
    return tuple(
        sorted(
            {
                path
                for spec in _STRUCTURES
                for event_id, paths in spec.audio_routes
                if event_id in roots
                for path in paths
            },
            key=lambda value: (value.casefold(), value),
        )
    )


def _neutral_runtime_audio_registry(
    effective_assets_root: Path | str,
) -> dict[str, Any]:
    root = Path(effective_assets_root).expanduser().resolve(strict=True)
    definition_path = root.joinpath("data", "ini", "soundeffects.ini")
    if not definition_path.is_file() or definition_path.is_symlink():
        raise ValueError("effective soundeffects.ini is missing or unsafe")
    definitions = parse_sage_audio_definitions(definition_path.read_bytes())
    closure = resolve_sage_audio_closure(
        definitions, _NEUTRAL_RUNTIME_AUDIO_ROOT_IDS
    )
    expected_paths = _neutral_runtime_audio_paths()
    sample_paths = resolve_audio_sample_paths(closure.sample_ids, expected_paths)
    if set(sample_paths.values()) != set(expected_paths):
        raise ValueError("neutral runtime audio sample closure changed")
    if closure.multisounds:
        raise ValueError("neutral runtime audio unexpectedly requires multisounds")

    registry = {
        "complete": True,
        "events": {
            event.id: {
                "parameters": event.neutral()["parameters"],
                "sounds": event.neutral()["sounds"],
            }
            for event in closure.events
        },
        "multisounds": {},
        "rootIds": list(closure.root_ids),
        "samples": {
            sample_id: (
                "assets/audio/neutral/"
                + PurePosixPath(source_path).name.casefold()
            )
            for sample_id, source_path in sample_paths.items()
        },
        "schema": "openbfme.audio-events",
        "schemaVersion": 1,
    }
    _validate_neutral_runtime_audio_registry(registry)
    return registry


def _expected_neutral_runtime_audio_events() -> dict[str, Any]:
    def parameters(*values: tuple[str, str]) -> list[dict[str, str]]:
        return [{"field": field, "value": value} for field, value in values]

    return {
        "BuildingWargPitVoxSingles": {
            "parameters": parameters(
                ("Priority", "lowest"),
                ("Limit", "3"),
                ("PitchShift", "-10 10"),
                ("Control", "loop"),
                ("Delay", "15000 45000"),
                ("VolumeShift", "-5"),
                ("Volume", "60"),
                ("Type", "world shrouded everyone"),
                ("SubmixSlider", "SoundFX"),
            ),
            "sounds": [
                {"id": f"EUWarg_VOX2{suffix}"}
                for suffix in _WARG_AMBIENT_SUFFIXES
            ],
        },
        "CivilianInnSelect": {
            "parameters": parameters(
                ("Volume", "95"),
                ("MinVolume", "85"),
                ("Limit", "1"),
                ("MinRange", "400"),
                ("Type", "world player"),
                ("SubmixSlider", "SoundFX"),
            ),
            "sounds": [{"id": "CBInn_selecta"}],
        },
        "CreepBuildingGenericSelect": {
            "parameters": parameters(
                ("Volume", "100"),
                ("MinVolume", "90"),
                ("MinRange", "400"),
                ("PitchShift", "10 20"),
                ("Priority", "high"),
                ("Limit", "1"),
                ("Type", "world player"),
                ("SubmixSlider", "SoundFX"),
            ),
            "sounds": [{"id": "NBCreep_selecta"}],
        },
        "WargLairBuildingSelect": {
            "parameters": parameters(
                ("Volume", "75"),
                ("MinVolume", "65"),
                ("MinRange", "400"),
                ("Priority", "high"),
                ("Limit", "1"),
                ("Type", "world player"),
                ("SubmixSlider", "SoundFX"),
            ),
            "sounds": [{"id": "NBWargL_selecta"}],
        },
    }


def _validate_neutral_runtime_audio_registry(registry: Mapping[str, Any]) -> None:
    expected_events = _expected_neutral_runtime_audio_events()
    expected_samples = {
        sound["id"]: (
            "assets/audio/neutral/" + str(sound["id"]).casefold() + ".wav"
        )
        for event in expected_events.values()
        for sound in event["sounds"]
    }
    expected = {
        "complete": True,
        "events": expected_events,
        "multisounds": {},
        "rootIds": sorted(_NEUTRAL_RUNTIME_AUDIO_ROOT_IDS, key=str.casefold),
        "samples": dict(
            sorted(expected_samples.items(), key=lambda item: item[0].casefold())
        ),
        "schema": "openbfme.audio-events",
        "schemaVersion": 1,
    }
    if dict(registry) != expected:
        raise ValueError("neutral runtime audio registry changed")


def _leaf_resource_id(prefix: str, path: str) -> str:
    """Return a readable, collision-resistant one-source resource id."""

    source = PurePosixPath(path)
    normalized = re.sub(r"[^a-z0-9]+", "-", source.stem.casefold()).strip("-")
    suffix = source.suffix.casefold().removeprefix(".")
    digest = hashlib.sha256(path.casefold().encode("utf-8")).hexdigest()[:8]
    maximum_stem = 63 - len(prefix) - len(suffix) - len(digest) - 3
    normalized = normalized[:maximum_stem].rstrip("-")
    return f"{prefix}-{normalized}-{suffix}-{digest}"


def _texture_resource_id(path: str) -> str:
    return _leaf_resource_id("neutral-texture", path)


def _audio_resource_id(path: str) -> str:
    return _leaf_resource_id("neutral-audio", path)


def _model_texture_resource_ids(
    spec: StructureSpec, model: ModelSpec
) -> tuple[str, ...]:
    groups = {group_id: paths for group_id, paths in spec.texture_groups}
    return tuple(
        _texture_resource_id(path)
        for group_id in model.texture_group_ids
        for path in groups[group_id]
    )


def _header_strings(row: Mapping[str, Any], field: str, label: str) -> list[str]:
    headers = _mapping(row.get("fileHeaders"), f"{label} fileHeaders")
    values = _array(headers.get(field), f"{label} fileHeaders.{field}")
    if any(not isinstance(value, str) or not value for value in values):
        raise ValueError(f"{label} fileHeaders.{field} contains an invalid identifier")
    if len(values) != len(set(values)):
        raise ValueError(f"{label} fileHeaders.{field} contains duplicates")
    return values


def _embedded_texture_paths(row: Mapping[str, Any], label: str) -> set[str]:
    result: set[str] = set()
    for position, raw_texture in enumerate(
        _array(row.get("embeddedTextures"), f"{label} embeddedTextures")
    ):
        texture = _mapping(raw_texture, f"{label} embedded texture {position}")
        if texture.get("status") != "resolved":
            raise ValueError(f"{label} embedded texture is not resolved")
        physical = _array(
            texture.get("physicalFiles"),
            f"{label} embedded texture {position} physicalFiles",
        )
        if len(physical) != 1:
            raise ValueError(f"{label} embedded texture must resolve to one file")
        record = _source_record(
            physical[0], f"{label} embedded texture {position} source"
        )
        result.add(str(record["virtualPath"]))
    return result


# Censuses recorded before the metadata scanner decoded the dual-bone skin
# streams carry exactly these two warnings for each secondary-skin source.
# Censuses recorded by the current scanner carry no warnings for them: the
# streams are decoded, validated, and reported as records instead.  Both
# recorded forms are accepted; anything else stays a hard failure.  The
# conversion-time secondary-skin strip proof, not this census pin, remains
# the enforcement point for stream semantics.
_LEGACY_SECONDARY_WARNINGS: list[dict[str, Any]] = [
    {
        "chunkId": 3072,
        "chunkIdHex": "0x00000C00",
        "code": "unsupported-chunk",
        "message": "metadata scanner does not interpret vertices-2",
    },
    {
        "chunkId": 3073,
        "chunkIdHex": "0x00000C01",
        "code": "unsupported-chunk",
        "message": "metadata scanner does not interpret normals-2",
    },
]


def _acceptable_warning_forms(required: bool) -> list[list[dict[str, Any]]]:
    if not required:
        return [[]]
    return [[], _LEGACY_SECONDARY_WARNINGS]


def _validate_w3d_closure(
    spec: StructureSpec,
    target: Mapping[str, Any],
    sources: dict[str, dict[str, Any]],
) -> dict[str, Mapping[str, Any]]:
    closure = _mapping(target.get("w3dClosure"), f"{spec.type_name} w3dClosure")
    rows = _array(closure.get("files"), f"{spec.type_name} w3dClosure.files")
    if _integer(closure.get("fileCount"), f"{spec.type_name} W3D fileCount") != len(
        rows
    ):
        raise ValueError(f"{spec.type_name} W3D fileCount does not match rows")
    by_path: dict[str, Mapping[str, Any]] = {}
    for position, raw_row in enumerate(rows):
        row = _mapping(raw_row, f"{spec.type_name} W3D row {position}")
        source = _source_record(
            row.get("file"), f"{spec.type_name} W3D row {position} file"
        )
        path = str(source["virtualPath"])
        key = path.casefold()
        if key in by_path:
            raise ValueError(f"duplicate {spec.type_name} W3D source: {path}")
        by_path[key] = row
        _merge_source(sources, source, f"{spec.type_name} W3D")

        warnings = _array(row.get("warnings"), f"{spec.type_name} {path} warnings")
        required_secondary = path in spec.secondary_skin_sources
        acceptable_forms = _acceptable_warning_forms(required_secondary)
        normalized_warnings: list[dict[str, Any]] = []
        for warning in warnings:
            value = _mapping(warning, f"{spec.type_name} {path} warning")
            normalized_warnings.append(
                {
                    "chunkId": value.get("chunkId"),
                    "chunkIdHex": value.get("chunkIdHex"),
                    "code": value.get("code"),
                    "message": value.get("message"),
                }
            )
        if normalized_warnings not in acceptable_forms:
            raise ValueError(
                f"{spec.type_name} {path} has unexpected W3D scanner warnings"
            )

    expected_paths = _expected_w3d_paths(spec)
    actual_paths = sorted(
        (str(_mapping(row.get("file"), "W3D file").get("virtualPath")) for row in rows),
        key=lambda value: (value.casefold(), value),
    )
    if actual_paths != list(expected_paths):
        raise ValueError(f"{spec.type_name} exact W3D closure does not match the plan")

    texture_groups = {group_id: set(paths) for group_id, paths in spec.texture_groups}
    for model in spec.models:
        model_row = by_path[model.model_source.casefold()]
        if not _header_strings(
            model_row, "modelIds", f"{spec.type_name} {model.model_source}"
        ):
            raise ValueError(f"{spec.type_name} {model.role} has no model header")
        hierarchy_row = by_path[model.hierarchy_source.casefold()]
        if not _header_strings(
            hierarchy_row,
            "hierarchyIds",
            f"{spec.type_name} {model.hierarchy_source}",
        ):
            raise ValueError(f"{spec.type_name} {model.role} has no hierarchy header")
        if model.converter == "w3d-hierarchical":
            animation_ids = _header_strings(
                model_row,
                "animationIds",
                f"{spec.type_name} {model.model_source}",
            )
            if animation_ids:
                raise ValueError(
                    f"{spec.type_name} {model.role} is not a zero-clip hierarchy"
                )
        elif model.converter == "w3d-bundle":
            if not model.animation_sources or len(model.animation_sources) != len(
                model.authored_animation_ids
            ):
                raise ValueError(
                    f"{spec.type_name} {model.role} animation contract is invalid"
                )
            for animation_path, identifier in zip(
                model.animation_sources, model.authored_animation_ids, strict=True
            ):
                animation_ids = _header_strings(
                    by_path[animation_path.casefold()],
                    "animationIds",
                    f"{spec.type_name} {animation_path}",
                )
                if identifier not in animation_ids:
                    raise ValueError(
                        f"{spec.type_name} {model.role} is missing animation {identifier!r}"
                    )
        else:
            raise AssertionError(
                f"unsupported neutral model converter: {model.converter}"
            )

        expected_textures = set().union(
            *(texture_groups[group_id] for group_id in model.texture_group_ids)
        )
        observed_textures: set[str] = set()
        for path in model.sources:
            observed_textures.update(
                _embedded_texture_paths(
                    by_path[path.casefold()], f"{spec.type_name} {path}"
                )
            )
        if observed_textures != expected_textures:
            raise ValueError(
                f"{spec.type_name} {model.role} embedded texture closure does not "
                "match its exact dependency groups"
            )
    return by_path


def _visual_rows(target: Mapping[str, Any], type_name: str) -> list[Mapping[str, Any]]:
    document = _mapping(target.get("visualReferences"), f"{type_name} visualReferences")
    rows = _array(document.get("references"), f"{type_name} visual references")
    if _integer(document.get("count"), f"{type_name} visual reference count") != len(
        rows
    ):
        raise ValueError(f"{type_name} visual reference count does not match rows")
    return [_mapping(value, f"{type_name} visual reference") for value in rows]


def _conditions(row: Mapping[str, Any], label: str) -> tuple[str, ...]:
    values = _array(row.get("conditions"), f"{label} conditions")
    if any(not isinstance(value, str) or not value for value in values):
        raise ValueError(f"{label} conditions contain an invalid token")
    return tuple(values)


def _visual_physical_paths(row: Mapping[str, Any], label: str) -> tuple[str, ...]:
    result: list[str] = []
    for position, value in enumerate(
        _array(row.get("physicalFiles", []), f"{label} physicalFiles")
    ):
        result.append(
            str(
                _source_record(value, f"{label} physical file {position}")[
                    "virtualPath"
                ]
            )
        )
    return tuple(result)


def _validate_visual_states_and_textures(
    spec: StructureSpec,
    target: Mapping[str, Any],
    w3d_rows: Mapping[str, Mapping[str, Any]],
) -> None:
    rows = _visual_rows(target, spec.type_name)
    model_by_id = {model.resource_id: model for model in spec.models}
    for phase in spec.phases:
        expected_path = (
            model_by_id[phase.model_resource_id].model_source
            if phase.model_resource_id is not None
            else None
        )
        for condition_set in phase.source_conditions:
            matches: list[Mapping[str, Any]] = []
            for row in rows:
                if (
                    row.get("identifier") == phase.source_identifier
                    and row.get("kind") == "model"
                    and row.get("usage") == phase.visual_usage
                    and _conditions(row, f"{spec.type_name} visual reference")
                    == condition_set
                ):
                    matches.append(row)
            if len(matches) != 1:
                raise ValueError(
                    f"{spec.type_name} phase {phase.phase!r} condition "
                    f"{condition_set!r} does not have one exact visual reference"
                )
            match = matches[0]
            physical = _visual_physical_paths(
                match, f"{spec.type_name} {phase.phase} visual reference"
            )
            if phase.visual_mode == "no-render":
                if match.get("status") != "semantic" or physical:
                    raise ValueError(
                        f"{spec.type_name} phase {phase.phase!r} is not authored no-render"
                    )
            else:
                if match.get("status") != "resolved" or physical != (expected_path,):
                    raise ValueError(
                        f"{spec.type_name} phase {phase.phase!r} visual source is wrong"
                    )

    bib = model_by_id[spec.bib_resource_id]
    bib_rows = [
        row
        for row in rows
        if row.get("kind") == "model"
        and row.get("usage") == "floor-model"
        and _conditions(row, f"{spec.type_name} bib reference") == ()
        and _visual_physical_paths(row, f"{spec.type_name} bib reference")
        == (bib.model_source,)
    ]
    if len(bib_rows) != 1:
        raise ValueError(
            f"{spec.type_name} does not have one exact floor/bib reference"
        )

    observed_textures: set[str] = set()
    for row in w3d_rows.values():
        observed_textures.update(
            _embedded_texture_paths(row, f"{spec.type_name} W3D texture closure")
        )
    for row in rows:
        if row.get("kind") == "texture":
            observed_textures.update(
                _visual_physical_paths(row, f"{spec.type_name} visual texture")
            )
    if observed_textures != set(_texture_paths(spec)):
        raise ValueError(
            f"{spec.type_name} exact model/weather texture closure does not match"
        )


def _validate_audio(
    spec: StructureSpec,
    target: Mapping[str, Any],
    sources: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    closure = _mapping(target.get("audioClosure"), f"{spec.type_name} audioClosure")
    resolved = _mapping(closure.get("resolved"), f"{spec.type_name} resolved audio")
    roots = _array(resolved.get("rootIds"), f"{spec.type_name} audio rootIds")
    expected_routes = dict(spec.audio_routes)
    if roots != sorted(expected_routes):
        raise ValueError(
            f"{spec.type_name} exact audio root order/closure does not match"
        )

    samples_by_id: dict[str, str] = {}
    for position, raw_sample in enumerate(
        _array(resolved.get("samples"), f"{spec.type_name} resolved samples")
    ):
        sample = _mapping(raw_sample, f"{spec.type_name} sample {position}")
        identifier = _text(sample.get("id"), f"{spec.type_name} sample id")
        if identifier in samples_by_id:
            raise ValueError(
                f"{spec.type_name} duplicate audio sample id: {identifier}"
            )
        source = _source_record(sample.get("file"), f"{spec.type_name} sample file")
        samples_by_id[identifier] = str(source["virtualPath"])
        _merge_source(sources, source, f"{spec.type_name} audio")

    actual_routes: dict[str, tuple[str, ...]] = {}
    for position, raw_event in enumerate(
        _array(resolved.get("events"), f"{spec.type_name} resolved events")
    ):
        event = _mapping(raw_event, f"{spec.type_name} audio event {position}")
        identifier = _text(event.get("id"), f"{spec.type_name} audio event id")
        sample_ids = _array(
            event.get("sampleIds"), f"{spec.type_name} {identifier} sampleIds"
        )
        try:
            paths = tuple(samples_by_id[str(sample_id)] for sample_id in sample_ids)
        except KeyError as exc:
            raise ValueError(
                f"{spec.type_name} audio event references an unresolved sample"
            ) from exc
        actual_routes[identifier] = paths
    if actual_routes != expected_routes:
        raise ValueError(f"{spec.type_name} exact audio event routing does not match")
    if set(samples_by_id.values()) != set(_audio_paths(spec)):
        raise ValueError(f"{spec.type_name} exact audio sample closure does not match")
    if _array(resolved.get("multisounds"), f"{spec.type_name} multisounds"):
        raise ValueError(f"{spec.type_name} unexpectedly requires multisound expansion")

    definition_documents = _array(
        resolved.get("definitionDocuments"),
        f"{spec.type_name} audio definitionDocuments",
    )
    if len(definition_documents) != 1:
        raise ValueError(f"{spec.type_name} must have one audio definition document")
    definition_source = _source_record(
        definition_documents[0], f"{spec.type_name} audio definition document"
    )
    if definition_source["virtualPath"] != "data/ini/soundeffects.ini":
        raise ValueError(f"{spec.type_name} audio definition document is unexpected")
    _merge_source(sources, definition_source, f"{spec.type_name} audio definition")
    return {
        "rootIds": roots,
        "routes": [
            {"eventId": event_id, "sampleVirtualPaths": list(paths)}
            for event_id, paths in spec.audio_routes
        ],
    }


def _attachment_condition_tokens(
    value: Mapping[str, Any], label: str
) -> tuple[str, ...]:
    scope = _array(value.get("scope"), f"{label} scope")
    if not scope:
        raise ValueError(f"{label} has no condition scope")
    final = _mapping(scope[-1], f"{label} final scope")
    tokens = _array(final.get("headerTokens"), f"{label} condition tokens")
    if any(not isinstance(token, str) or not token for token in tokens):
        raise ValueError(f"{label} has invalid condition tokens")
    return tuple(tokens)


def _validate_effect_ids(spec: StructureSpec, target: Mapping[str, Any]) -> None:
    closure = _mapping(
        target.get("particleAndFxClosure"), f"{spec.type_name} particleAndFxClosure"
    )
    actual_attachments: list[AttachmentSpec] = []
    for position, raw_attachment in enumerate(
        _array(closure.get("attachments"), f"{spec.type_name} attachments")
    ):
        attachment = _mapping(raw_attachment, f"{spec.type_name} attachment {position}")
        options = _array(
            attachment.get("options"), f"{spec.type_name} attachment options"
        )
        if any(not isinstance(value, str) or not value for value in options):
            raise ValueError(f"{spec.type_name} attachment has invalid options")
        actual_attachments.append(
            AttachmentSpec(
                source_conditions=_attachment_condition_tokens(
                    attachment, f"{spec.type_name} attachment {position}"
                ),
                bone=_text(attachment.get("bone"), f"{spec.type_name} attachment bone"),
                particle_system_id=_text(
                    attachment.get("particleSystemId"),
                    f"{spec.type_name} attachment particleSystemId",
                ),
                options=tuple(options),
            )
        )
    if actual_attachments != list(spec.attachments):
        raise ValueError(f"{spec.type_name} exact particle attachment IDs do not match")

    fx_rows = _array(closure.get("fxRoots"), f"{spec.type_name} fxRoots")
    entering: list[tuple[str, str]] = []
    collapse: list[tuple[str, str]] = []
    condition_to_phase = {
        "DAMAGED": "damaged",
        "REALLYDAMAGED": "really-damaged",
        "COLLAPSING": "collapsing",
    }
    value_to_stage = {"INITIAL": "initial", "ALMOST_FINAL": "almost-final"}
    for position, raw_fx in enumerate(fx_rows):
        fx = _mapping(raw_fx, f"{spec.type_name} fx root {position}")
        fx_id = _text(fx.get("fxListId"), f"{spec.type_name} fx list id")
        field = fx.get("field")
        if field == "EnteringStateFX":
            scope = _array(fx.get("scope"), f"{spec.type_name} FX scope")
            final = _mapping(scope[-1], f"{spec.type_name} FX final scope")
            tokens = _array(
                final.get("headerTokens"), f"{spec.type_name} FX condition tokens"
            )
            if len(tokens) != 1 or tokens[0] not in condition_to_phase:
                raise ValueError(
                    f"{spec.type_name} has an unexpected entering-state FX"
                )
            entering.append((condition_to_phase[str(tokens[0])], fx_id))
        elif field == "FXList":
            value = _text(fx.get("value"), f"{spec.type_name} collapse FX value")
            stage = value.split()[0]
            if stage not in value_to_stage:
                raise ValueError(
                    f"{spec.type_name} has an unexpected collapse FX stage"
                )
            collapse.append((value_to_stage[stage], fx_id))
        else:
            raise ValueError(f"{spec.type_name} has an unexpected FX root field")
    if entering != list(spec.entering_state_fx):
        raise ValueError(f"{spec.type_name} entering-state FX IDs do not match")
    if collapse != list(spec.collapse_update_fx):
        raise ValueError(f"{spec.type_name} collapse-update FX IDs do not match")


def _collect_physical_sources(
    spec: StructureSpec,
    target: Mapping[str, Any],
    sources: dict[str, dict[str, Any]],
) -> None:
    closure = _mapping(
        target.get("physicalClosure"), f"{spec.type_name} physicalClosure"
    )
    rows = _array(closure.get("files"), f"{spec.type_name} physicalClosure.files")
    if _integer(
        closure.get("fileCount"), f"{spec.type_name} physical fileCount"
    ) != len(rows):
        raise ValueError(f"{spec.type_name} physical file count does not match rows")
    local: dict[str, dict[str, Any]] = {}
    total_bytes = 0
    for position, value in enumerate(rows):
        source = _source_record(value, f"{spec.type_name} physical source {position}")
        _merge_source(local, source, f"{spec.type_name} physical closure")
        _merge_source(sources, source, f"{spec.type_name} physical closure")
        total_bytes += int(source["byteLength"])
    if (
        _integer(closure.get("byteLength"), f"{spec.type_name} physical byteLength")
        != total_bytes
    ):
        raise ValueError(f"{spec.type_name} physical byte length does not match rows")
    _sha256(
        closure.get("aggregateSha256"), f"{spec.type_name} physical aggregateSha256"
    )


def _validate_object_definition(
    spec: StructureSpec,
    target: Mapping[str, Any],
    sources: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    definition = _mapping(
        target.get("objectDefinition"), f"{spec.type_name} objectDefinition"
    )
    exact = {
        "name": spec.type_name,
        "kind": "Object",
        "parent": None,
        "ancestry": [spec.type_name],
        "inheritanceComplete": True,
        "sourceVirtualPath": spec.object_source,
    }
    for field, expected in exact.items():
        if definition.get(field) != expected:
            raise ValueError(
                f"{spec.type_name} objectDefinition.{field} does not match"
            )
    source = _source_record(
        definition.get("sourceFile"), f"{spec.type_name} object definition source"
    )
    if source["virtualPath"] != spec.object_source:
        raise ValueError(f"{spec.type_name} object definition source path is wrong")
    _merge_source(sources, source, f"{spec.type_name} object definition")
    placements = _mapping(
        target.get("mapPlacements"), f"{spec.type_name} mapPlacements"
    )
    rows = _array(placements.get("records"), f"{spec.type_name} placement records")
    count = _integer(placements.get("count"), f"{spec.type_name} placement count")
    if count != spec.placement_count or len(rows) != count:
        raise ValueError(f"{spec.type_name} exact Fords placement count does not match")
    record_indices: set[int] = set()
    unique_ids: set[str] = set()
    for position, raw_placement in enumerate(rows):
        placement = _mapping(raw_placement, f"{spec.type_name} placement {position}")
        record_index = _integer(
            placement.get("recordIndex"), f"{spec.type_name} placement recordIndex"
        )
        unique_id = _text(
            placement.get("uniqueId"), f"{spec.type_name} placement uniqueId"
        )
        if record_index in record_indices or unique_id in unique_ids:
            raise ValueError(f"{spec.type_name} placement identity is duplicated")
        record_indices.add(record_index)
        unique_ids.add(unique_id)
    return {
        "source": source,
        "placementCount": count,
        "placementRecordIndices": sorted(record_indices),
        "placementUniqueIds": sorted(unique_ids, key=lambda value: value.casefold()),
    }


def _verify_effective_sources(
    root_value: Path | str,
    selected: Sequence[dict[str, Any]],
) -> dict[str, Any]:
    root = Path(root_value).expanduser().resolve(strict=True)
    if not root.is_dir():
        raise ValueError(f"effective-assets root is not a directory: {root}")
    verified: list[dict[str, Any]] = []
    for source in sorted(
        selected, key=lambda item: str(item["virtualPath"]).casefold()
    ):
        virtual_path = str(source["virtualPath"])
        parts = safe_relative_parts(virtual_path)
        lexical = root.joinpath(*parts)
        if lexical.is_symlink():
            raise ValueError(f"effective-assets source is a symlink: {virtual_path}")
        try:
            physical = lexical.resolve(strict=True)
        except OSError as exc:
            raise ValueError(
                f"effective-assets source is missing: {virtual_path}"
            ) from exc
        if not physical.is_relative_to(root) or not physical.is_file():
            raise ValueError(
                f"effective-assets source escapes its root: {virtual_path}"
            )
        actual_size = physical.stat().st_size
        if actual_size != source["byteLength"]:
            raise ValueError(f"effective-assets byte length mismatch: {virtual_path}")
        actual_sha = sha256_file(physical)
        if actual_sha != source["sha256"]:
            raise ValueError(f"effective-assets SHA-256 mismatch: {virtual_path}")
        verified.append(
            {
                "virtualPath": virtual_path,
                "sha256": actual_sha,
                "byteLength": actual_size,
            }
        )
    return {
        "verifiedSelectedFileCount": len(verified),
        "verifiedSelectedByteLength": sum(int(item["byteLength"]) for item in verified),
        "verifiedSelectedAggregateSha256": _canonical_sha256(verified),
    }


def _effective_manifest_sources(
    root_value: Path | str,
    expected: Mapping[str, tuple[str, str]],
) -> tuple[dict[str, dict[str, Any]], str]:
    """Select exact effective-assets winners absent from the older census."""

    root = Path(root_value).expanduser().resolve(strict=True)
    manifest_path = root / ".openbfme" / "manifest.json"
    if not manifest_path.is_file() or manifest_path.stat().st_size > 64 * 1024 * 1024:
        raise ValueError("effective-assets manifest is missing or unbounded")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid effective-assets manifest: {exc}") from exc
    document = _mapping(manifest, "effective-assets manifest")
    if (
        document.get("schema") != "openbfme.effective-assets-manifest"
        or document.get("schema_version") != 0
    ):
        raise ValueError("unsupported effective-assets manifest schema")
    manifest_digest = _sha256(
        document.get("aggregate_sha256"), "effective-assets manifest aggregate"
    )
    wanted = {
        path.casefold(): (path, sha, role) for path, (sha, role) in expected.items()
    }
    selected: dict[str, dict[str, Any]] = {}
    for position, raw_row in enumerate(
        _array(document.get("files"), "effective-assets manifest files")
    ):
        row = _mapping(raw_row, f"effective-assets manifest file {position}")
        raw_path = row.get("path")
        if not isinstance(raw_path, str) or raw_path.casefold() not in wanted:
            continue
        expected_path, expected_sha, role = wanted[raw_path.casefold()]
        if raw_path != expected_path:
            raise ValueError(
                f"effective-assets manifest path case changed: {raw_path!r}"
            )
        if raw_path.casefold() in selected:
            raise ValueError(f"duplicate effective-assets manifest winner: {raw_path}")
        source = {
            "virtualPath": raw_path,
            "sha256": _sha256(
                row.get("sha256"), f"effective-assets {raw_path} SHA-256"
            ),
            "byteLength": _integer(
                row.get("size"), f"effective-assets {raw_path} byte length"
            ),
            "archive": _text(
                row.get("archive"), f"effective-assets {raw_path} archive"
            ),
            "precedence": _integer(
                row.get("precedence"), f"effective-assets {raw_path} precedence"
            ),
            "roles": [role],
        }
        if source["sha256"] != expected_sha:
            raise ValueError(
                f"effective-assets manifest disagrees with simulation proof: {raw_path}"
            )
        selected[raw_path.casefold()] = source
    if set(selected) != set(wanted):
        missing = sorted(wanted[key][0] for key in set(wanted) - set(selected))
        raise ValueError(
            "effective-assets manifest is missing simulation source(s): "
            + ", ".join(missing)
        )
    return selected, manifest_digest


def _resource_payloads() -> list[dict[str, Any]]:
    texture_resources = [
        {
            "id": _texture_resource_id(path),
            "kind": "texture",
            "patterns": [path],
            "required": True,
            "converter": "hash-only",
            "limit": 1,
            "expected_count": 1,
        }
        for spec in _STRUCTURES
        for _, paths in spec.texture_groups
        for path in paths
    ]
    audio_paths = sorted(
        {path for spec in _STRUCTURES for path in _audio_paths(spec)},
        key=lambda value: (value.casefold(), value),
    )
    neutral_runtime_paths = set(_neutral_runtime_audio_paths())
    audio_resources = []
    for path in audio_paths:
        resource: dict[str, Any] = {
            "id": _audio_resource_id(path),
            "kind": "audio",
            "patterns": [path],
            "required": True,
            "limit": 1,
            "expected_count": 1,
        }
        if path in neutral_runtime_paths:
            resource.update(
                {
                    "converter": "audio",
                    "output": (
                        "assets/audio/neutral/"
                        + PurePosixPath(path).name.casefold()
                    ),
                    "options": {"force_pcm": True},
                }
            )
        else:
            resource["converter"] = "hash-only"
        audio_resources.append(resource)
    model_resources: list[dict[str, Any]] = []
    for spec in _STRUCTURES:
        for model in _all_model_specs(spec):
            options: dict[str, Any] = {
                "model": PurePosixPath(model.model_source).name,
                "animations": [
                    PurePosixPath(path).name for path in model.animation_sources
                ],
                "required_equipment": [],
                "inputResourceIds": list(_model_texture_resource_ids(spec, model)),
            }
            model_resources.append(
                {
                    "id": model.resource_id,
                    "kind": "model",
                    "patterns": list(model.sources),
                    "required": True,
                    "converter": model.converter,
                    "output": model.output,
                    "limit": len(model.sources),
                    "expected_count": len(model.sources),
                    "options": options,
                }
            )
    resources = [*texture_resources, *audio_resources, *model_resources]
    _case_unique((str(resource["id"]) for resource in resources), "resource id")
    _case_unique(
        (
            str(resource["output"])
            for resource in resources
            if resource.get("output") is not None
        ),
        "resource output",
    )
    pattern_owners: dict[str, str] = {}
    for resource in resources:
        for pattern in resource["patterns"]:
            key = str(pattern).casefold()
            previous = pattern_owners.setdefault(key, str(resource["id"]))
            if previous != resource["id"]:
                raise AssertionError(
                    f"neutral profile source is owned twice: {pattern!r}"
                )
    return resources


def _binding_payloads() -> list[dict[str, Any]]:
    model_by_id = {
        model.resource_id: model for spec in _STRUCTURES for model in spec.models
    }
    return [
        {
            "typeName": spec.type_name,
            "sourceVirtualModel": spec.source_virtual_model,
            "glb": model_by_id[f"neutral-{spec.slug}-intact"].output,
            "objectId": spec.object_id,
            "matchMethod": "exact-type-name",
        }
        for spec in _STRUCTURES
    ]


def _phase_payload(
    phase: PhaseSpec, model_by_id: Mapping[str, ModelSpec]
) -> dict[str, Any]:
    if phase.visual_mode == "glb":
        assert phase.model_resource_id is not None
        model = model_by_id[phase.model_resource_id]
        visual: dict[str, Any] = {
            "mode": "glb",
            "modelResourceId": model.resource_id,
            "glb": model.output,
        }
    else:
        visual = {
            "mode": "no-render",
            "sourceIdentifier": phase.source_identifier,
        }
    return {
        "phase": phase.phase,
        "sourceConditionSets": [list(value) for value in phase.source_conditions],
        "visual": visual,
        "animation": {"clip": phase.clip, "mode": phase.clip_mode},
        "nextPhase": phase.next_phase,
        "transitionAuthority": "deterministic-simulation",
    }


def _lifecycle_payloads(
    simulation_rows: Mapping[str, Mapping[str, Any]],
) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for spec in _STRUCTURES:
        facts = simulation_rows[spec.type_name]
        model_by_id = {model.resource_id: model for model in _all_model_specs(spec)}
        bib = model_by_id[spec.bib_resource_id]
        rebuild_hole: dict[str, Any] | None = None
        if spec.rebuild_hole is not None:
            hole = spec.rebuild_hole
            hole_model = hole.model
            post_rubble = _mapping(
                facts.get("postRubble"), f"{spec.type_name} postRubble"
            )
            collapse = _mapping(facts.get("collapse"), f"{spec.type_name} collapse")
            rebuild_hole = {
                "sourceTypeName": hole.type_name,
                "sourceDefinitionVirtualPath": hole.object_source,
                "spawnTiming": "death-behavior-immediate",
                "states": [
                    {
                        "phase": "visible-rubble",
                        "sourceConditionSets": [[]],
                        "visual": {
                            "mode": "glb",
                            "modelResourceId": hole_model.resource_id,
                            "glb": hole_model.output,
                        },
                        "fadeInSeconds": hole.fade_in_seconds,
                        "transitionAuthority": "deterministic-simulation",
                    }
                ],
                "maximumHealth": hole.maximum_health,
                "healthRegenPercentPerSecond": (hole.health_regen_percent_per_second),
                "terminalDuration": post_rubble["terminalDuration"],
                "originalObjectDestroyWhenCollapseDone": collapse[
                    "destroyObjectWhenDone"
                ],
                "exactOriginalRemovalFrame": None,
                "exactOriginalRemovalFrameStatus": collapse["exactTotalTimingStatus"],
            }
        result.append(
            {
                "typeName": spec.type_name,
                "objectId": spec.object_id,
                "schemaVersion": 1,
                "initialPhase": "intact",
                "phases": [_phase_payload(phase, model_by_id) for phase in spec.phases],
                "bib": {
                    "sourceConditions": [],
                    "visual": {
                        "mode": "glb",
                        "modelResourceId": bib.resource_id,
                        "glb": bib.output,
                    },
                    "drawModule": "W3DFloorDraw",
                    "startHiddenAuthored": False,
                    "hideIfModelConditions": [],
                    "visibility": "unconditional-authored-floor-draw",
                    "duringConstruction": True,
                },
                "rebuildHole": rebuild_hole,
                "audioEvents": dict(spec.audio_semantics),
                "effects": {
                    "enteringStateFx": dict(spec.entering_state_fx),
                    "collapseUpdateFx": dict(spec.collapse_update_fx),
                    "particleAttachments": [
                        {
                            "sourceConditions": list(attachment.source_conditions),
                            "bone": attachment.bone,
                            "particleSystemId": attachment.particle_system_id,
                            "options": list(attachment.options),
                        }
                        for attachment in spec.attachments
                    ],
                    "definitionTranslationStatus": (
                        "exact-particle-plan-cross-selection-pending"
                    ),
                },
                "simulationFacts": {
                    "maximumHealth": facts["maximumHealth"],
                    "initialHealth": deepcopy(facts["initialHealth"]),
                    "damageStateRule": deepcopy(facts["damageStateRule"]),
                    "construction": deepcopy(facts["construction"]),
                    "collapse": deepcopy(facts["collapse"]),
                    "postRubble": deepcopy(facts["postRubble"]),
                    "captureInitialState": deepcopy(facts["captureInitialState"]),
                },
                "normalWeatherModelTextureResourceIds": [
                    _texture_resource_id(path)
                    for group_id, paths in spec.texture_groups
                    if not group_id.endswith("-weather")
                    for path in paths
                ],
                "weatherEvidenceResourceIds": [
                    _texture_resource_id(path)
                    for group_id, paths in spec.texture_groups
                    if group_id.endswith("-weather")
                    for path in paths
                ],
                "audioEvidenceResourceIds": [
                    _audio_resource_id(path) for path in _audio_paths(spec)
                ],
            }
        )
    return result


def _validate_import_profile_resources(resources: list[dict[str, Any]]) -> bool:
    payload = {
        "format": 1,
        "id": "neutral-lifecycle-fragment-validation",
        "pack": {"id": "neutral-lifecycle-fragment-validation-pack"},
        "resources": resources,
    }
    with tempfile.TemporaryDirectory(prefix="openbfme-neutral-profile-") as raw:
        path = Path(raw) / "profile.json"
        path.write_text(
            json.dumps(
                payload,
                sort_keys=True,
                ensure_ascii=False,
                separators=(",", ":"),
                allow_nan=False,
            ),
            encoding="utf-8",
        )
        parsed = ImportProfile.load(path)
    if len(parsed.resources) != len(resources):
        raise ValueError("ImportProfile parser changed neutral resource count")
    return True


def build_retail_neutral_lifecycle_plan(
    census: Mapping[str, Any],
    simulation_facts: Mapping[str, Any],
    effective_assets_root: Path | str,
) -> dict[str, Any]:
    """Build a deterministic, fail-closed neutral lifecycle conversion plan."""

    census_mapping = _mapping(census, "Fords census")
    census_digest = _validate_declared_digest(
        census_mapping, "aggregateSha256", "Fords census"
    )
    simulation_mapping = _mapping(simulation_facts, "neutral simulation facts")
    simulation_digest, simulation_rows, simulation_sources = _validate_simulation_facts(
        simulation_mapping
    )
    targets = _selected_target_rows(census_mapping)
    all_sources: dict[str, dict[str, Any]] = {}
    structure_evidence: list[dict[str, Any]] = []
    for spec in _STRUCTURES:
        target = targets[spec.type_name]
        _collect_physical_sources(spec, target, all_sources)
        object_evidence = _validate_object_definition(spec, target, all_sources)
        w3d_rows = _validate_w3d_closure(spec, target, all_sources)
        _validate_visual_states_and_textures(spec, target, w3d_rows)
        audio_evidence = _validate_audio(spec, target, all_sources)
        _validate_effect_ids(spec, target)
        structure_evidence.append(
            {
                "typeName": spec.type_name,
                "objectDefinition": object_evidence,
                "w3dSources": [
                    all_sources[path.casefold()] for path in _expected_w3d_paths(spec)
                ],
                "modelWeatherTextureSources": [
                    all_sources[path.casefold()] for path in _texture_paths(spec)
                ],
                "audio": audio_evidence,
                "secondarySkinNormalizationSources": [
                    all_sources[path.casefold()] for path in spec.secondary_skin_sources
                ],
            }
        )

    for spec in _STRUCTURES:
        simulation_source = simulation_sources[spec.object_source.casefold()]
        census_source = all_sources[spec.object_source.casefold()]
        if simulation_source.get("sha256") != census_source["sha256"]:
            raise ValueError(
                f"{spec.type_name} simulation proof disagrees with census source"
            )

    holes_source = simulation_sources["data/ini/object/neutral/holes.ini"]
    additional_expected: dict[str, tuple[str, str]] = {
        "data/ini/object/neutral/holes.ini": (
            str(holes_source["sha256"]),
            "rebuild-hole-definition-document",
        )
    }
    for spec in _STRUCTURES:
        if spec.rebuild_hole is None:
            continue
        post_rubble = _mapping(
            simulation_rows[spec.type_name].get("postRubble"),
            f"{spec.type_name} postRubble",
        )
        additional_expected[spec.rebuild_hole.model.model_source] = (
            str(post_rubble["rebuildHoleModelSha256"]),
            "rebuild-hole-model",
        )
    additional_sources, effective_manifest_digest = _effective_manifest_sources(
        effective_assets_root, additional_expected
    )
    for path, source in additional_sources.items():
        _merge_source(all_sources, source, f"simulation source {path}")
    evidence_by_type = {str(item["typeName"]): item for item in structure_evidence}
    for spec in _STRUCTURES:
        evidence = evidence_by_type[spec.type_name]
        evidence["simulationFacts"] = deepcopy(simulation_rows[spec.type_name])
        if spec.rebuild_hole is not None:
            evidence["rebuildHoleW3dSource"] = all_sources[
                spec.rebuild_hole.model.model_source.casefold()
            ]
            evidence["rebuildHoleDefinitionSource"] = all_sources[
                spec.rebuild_hole.object_source.casefold()
            ]

    planned_paths = {
        path
        for spec in _STRUCTURES
        for path in (
            *_all_w3d_paths(spec),
            *_texture_paths(spec),
            *_audio_paths(spec),
        )
    }
    definition_paths = {
        *(spec.object_source for spec in _STRUCTURES),
        "data/ini/object/neutral/holes.ini",
        "data/ini/soundeffects.ini",
    }
    selected_paths = planned_paths | definition_paths
    missing_source_records = sorted(
        path for path in selected_paths if path.casefold() not in all_sources
    )
    if missing_source_records:
        raise ValueError(
            "source evidence is missing selected source record(s): "
            + ", ".join(missing_source_records)
        )
    selected_sources = [all_sources[path.casefold()] for path in selected_paths]
    effective_evidence = _verify_effective_sources(
        effective_assets_root, selected_sources
    )

    resources = _resource_payloads()
    runtime_audio_registry = _neutral_runtime_audio_registry(effective_assets_root)
    profile_validated = _validate_import_profile_resources(resources)
    bindings = _binding_payloads()
    lifecycles = _lifecycle_payloads(simulation_rows)
    no_render_count = sum(
        phase["visual"]["mode"] == "no-render"
        for lifecycle in lifecycles
        for phase in lifecycle["phases"]
    )
    summary = {
        "structureTypeCount": len(_STRUCTURES),
        "placementCount": sum(spec.placement_count for spec in _STRUCTURES),
        "profileResourceCount": len(resources),
        "modelResourceCount": sum(
            resource["kind"] == "model" for resource in resources
        ),
        "textureEvidenceResourceCount": sum(
            resource["kind"] == "texture" for resource in resources
        ),
        "audioEvidenceResourceCount": sum(
            resource["kind"] == "audio" for resource in resources
        ),
        "uniqueW3dSourceCount": len(
            {path for spec in _STRUCTURES for path in _all_w3d_paths(spec)}
        ),
        "uniqueModelWeatherTextureSourceCount": len(
            {path for spec in _STRUCTURES for path in _texture_paths(spec)}
        ),
        "uniqueAudioSampleSourceCount": len(
            {path for spec in _STRUCTURES for path in _audio_paths(spec)}
        ),
        "lifecyclePhaseCount": sum(
            len(item["phases"])
            + (
                len(item["rebuildHole"]["states"])
                if item["rebuildHole"] is not None
                else 0
            )
            for item in lifecycles
        ),
        "authoredNoRenderPhaseCount": no_render_count,
        "structureBindingCount": len(bindings),
    }
    expected_summary = {
        "profileResourceCount": _EXPECTED_RESOURCE_COUNT,
        "modelResourceCount": _EXPECTED_MODEL_RESOURCE_COUNT,
        "textureEvidenceResourceCount": _EXPECTED_TEXTURE_RESOURCE_COUNT,
        "audioEvidenceResourceCount": _EXPECTED_AUDIO_RESOURCE_COUNT,
        "uniqueW3dSourceCount": _EXPECTED_W3D_SOURCE_COUNT,
        "uniqueModelWeatherTextureSourceCount": _EXPECTED_TEXTURE_SOURCE_COUNT,
        "uniqueAudioSampleSourceCount": _EXPECTED_AUDIO_SOURCE_COUNT,
        "placementCount": _EXPECTED_PLACEMENT_COUNT,
        "lifecyclePhaseCount": _EXPECTED_PHASE_COUNT,
        "authoredNoRenderPhaseCount": _EXPECTED_NO_RENDER_PHASE_COUNT,
        "structureBindingCount": len(_STRUCTURES),
    }
    for field, expected in expected_summary.items():
        if summary[field] != expected:
            raise AssertionError(
                f"neutral lifecycle plan invariant changed: {field}={summary[field]} "
                f"(expected {expected})"
            )

    plan: dict[str, Any] = {
        "schema": NEUTRAL_LIFECYCLE_PLAN_SCHEMA,
        "schemaVersion": NEUTRAL_LIFECYCLE_PLAN_SCHEMA_VERSION,
        "sourceEvidence": {
            "fordsUnresolvedObjectCensusAggregateSha256": census_digest,
            "neutralSimulationFacts": {
                "aggregateSha256": simulation_digest,
                "document": deepcopy(dict(simulation_mapping)),
            },
            "effectiveAssetsManifestAggregateSha256": effective_manifest_digest,
            "effectiveAssets": effective_evidence,
            "structures": structure_evidence,
            "selectedProfileInputAggregateSha256": _canonical_sha256(
                sorted(
                    (all_sources[path.casefold()] for path in planned_paths),
                    key=lambda item: str(item["virtualPath"]).casefold(),
                )
            ),
            "selectedProfileInputFileCount": len(planned_paths),
            "definitionEvidenceFileCount": len(definition_paths),
        },
        "policy": {
            "selection": "exact-cave-inn-warg-lifecycle-closure-only",
            "substitutesAllowed": False,
            "fakeRubbleModelsAllowed": False,
            "sourceAuthoredNoRenderPreserved": True,
            "visibleRebuildHoleObjectsPreserved": True,
            "modelTextureOwnership": "one-exact-source-per-hash-only-resource",
            "snowTexturesEvidenceOnlyForNormalWeatherFordsGate": True,
            "audioSampleOwnership": (
                "neutral-specific-converted-shared-men-samples-hash-only"
            ),
            "transitionAuthority": "deterministic-simulation-not-animation-completion",
            "profileFragmentValidatedByImportProfile": profile_validated,
            "parityClaimAllowed": False,
        },
        "profileFragment": {
            "resources": resources,
            "objectBindings": {"structures": bindings},
            "runtimeDataMerge": {
                "data/audio_events.json": runtime_audio_registry,
            },
        },
        "runtimeAudioRegistryAddition": runtime_audio_registry,
        "structureLifecycles": lifecycles,
        "blockers": [
            {
                "code": "bfme2-StructureCollapseUpdate-runtime-not-open-source",
                "scope": (
                    "exact Cave/Warg collapse completion frame and original-object "
                    "removal ordering"
                ),
            },
            {
                "code": "bfme2-KeepObjectDie-default-CollapsingTime-not-proven",
                "scope": (
                    "Inn D3 reachability and exact rubble/post-rubble transition timing"
                ),
            },
            {
                "code": "capture-flag-link-not-explicit-in-decoded-map-record",
                "scope": "exact Inn-to-CaptureFlag engine association rule",
            },
            {
                "code": "exact-particle-plan-cross-selection-pending",
                "scope": (
                    "completion composer must select and attach the already-converted "
                    "exact particle definitions for attachment and FX-list IDs"
                ),
            },
            {
                "code": "neutral-lifecycle-completion-composer-handoff-pending",
                "scope": (
                    "standalone resources bindings and lifecycle facts must be handed "
                    "to the implemented and tested Godot lifecycle-v1 route"
                ),
            },
            {
                "code": "converted-glb-and-original-game-oracle-not-yet-proven",
                "scope": "clips materials origins timing particles audio cadence",
            },
        ],
        "summary": summary,
    }
    plan["aggregateSha256"] = _canonical_sha256(plan)
    _validate_plan_shape(plan)
    return plan


def _validate_plan_shape(raw: Mapping[str, Any]) -> None:
    plan = _mapping(raw, "neutral lifecycle plan")
    if plan.get("schema") != NEUTRAL_LIFECYCLE_PLAN_SCHEMA:
        raise ValueError("unsupported neutral lifecycle plan schema")
    if plan.get("schemaVersion") != NEUTRAL_LIFECYCLE_PLAN_SCHEMA_VERSION:
        raise ValueError("unsupported neutral lifecycle plan schema version")
    _validate_declared_digest(plan, "aggregateSha256", "neutral lifecycle plan")
    summary = _mapping(plan.get("summary"), "neutral lifecycle plan summary")
    exact_summary = {
        "structureTypeCount": 3,
        "placementCount": _EXPECTED_PLACEMENT_COUNT,
        "profileResourceCount": _EXPECTED_RESOURCE_COUNT,
        "modelResourceCount": _EXPECTED_MODEL_RESOURCE_COUNT,
        "textureEvidenceResourceCount": _EXPECTED_TEXTURE_RESOURCE_COUNT,
        "audioEvidenceResourceCount": _EXPECTED_AUDIO_RESOURCE_COUNT,
        "uniqueW3dSourceCount": _EXPECTED_W3D_SOURCE_COUNT,
        "uniqueModelWeatherTextureSourceCount": _EXPECTED_TEXTURE_SOURCE_COUNT,
        "uniqueAudioSampleSourceCount": _EXPECTED_AUDIO_SOURCE_COUNT,
        "lifecyclePhaseCount": _EXPECTED_PHASE_COUNT,
        "authoredNoRenderPhaseCount": _EXPECTED_NO_RENDER_PHASE_COUNT,
        "structureBindingCount": 3,
    }
    if dict(summary) != exact_summary:
        raise ValueError("neutral lifecycle plan summary is not the exact contract")
    fragment = _mapping(plan.get("profileFragment"), "neutral profileFragment")
    if set(fragment) != {"resources", "objectBindings", "runtimeDataMerge"}:
        raise ValueError("neutral profileFragment has unsupported fields")
    if fragment.get("resources") != _resource_payloads():
        raise ValueError("neutral profile resources do not match the exact contract")
    bindings = _mapping(fragment.get("objectBindings"), "neutral objectBindings")
    if dict(bindings) != {"structures": _binding_payloads()}:
        raise ValueError("neutral lifecycle structure bindings do not match")
    runtime_registry = _mapping(
        plan.get("runtimeAudioRegistryAddition"),
        "neutral runtimeAudioRegistryAddition",
    )
    _validate_neutral_runtime_audio_registry(runtime_registry)
    runtime_merge = _mapping(
        fragment.get("runtimeDataMerge"), "neutral runtimeDataMerge"
    )
    if runtime_merge != {"data/audio_events.json": runtime_registry}:
        raise ValueError("neutral runtime audio registry handoff changed")
    if plan.get("runtimeAudioRegistryAddition") != runtime_registry:
        raise ValueError("neutral runtime audio registry addition changed")
    source_evidence = _mapping(plan.get("sourceEvidence"), "neutral sourceEvidence")
    simulation_evidence = _mapping(
        source_evidence.get("neutralSimulationFacts"),
        "neutral simulation sourceEvidence",
    )
    simulation_document = _mapping(
        simulation_evidence.get("document"), "neutral simulation evidence document"
    )
    simulation_digest, simulation_rows, _ = _validate_simulation_facts(
        simulation_document
    )
    if simulation_evidence.get("aggregateSha256") != simulation_digest:
        raise ValueError("neutral simulation evidence digest does not match")
    if plan.get("structureLifecycles") != _lifecycle_payloads(simulation_rows):
        raise ValueError("neutral lifecycle state metadata does not match")


def generated_import_profile(plan: Mapping[str, Any]) -> dict[str, Any]:
    """Return a standalone ImportProfile payload for the plan resources."""

    _validate_plan_shape(plan)
    resources = deepcopy(
        _mapping(plan.get("profileFragment"), "neutral profileFragment")["resources"]
    )
    payload = {
        "format": 1,
        "id": "bfme2-neutral-lifecycle-private",
        "title": "BFME2 Fords neutral lifecycle private conversion batch",
        "pack": {
            "id": "bfme2-neutral-lifecycle-private",
            "version": "0.1.0",
            "private": True,
        },
        "resources": resources,
    }
    with tempfile.TemporaryDirectory(prefix="openbfme-neutral-generated-") as raw:
        path = Path(raw) / "profile.json"
        path.write_text(
            json.dumps(
                payload,
                sort_keys=True,
                ensure_ascii=False,
                separators=(",", ":"),
                allow_nan=False,
            ),
            encoding="utf-8",
        )
        parsed = ImportProfile.load(path)
    if len(parsed.resources) != _EXPECTED_RESOURCE_COUNT:
        raise ValueError("generated neutral ImportProfile resource count changed")
    return payload


def load_retail_neutral_lifecycle_census(path: Path | str) -> dict[str, Any]:
    """Load a bounded JSON census; semantic validation occurs during build."""

    source = Path(path).expanduser().resolve()
    if not source.is_file() or source.stat().st_size > _MAX_CENSUS_BYTES:
        raise ValueError(
            f"Fords census is missing or exceeds {_MAX_CENSUS_BYTES} bytes"
        )
    try:
        value = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid Fords census: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError("Fords census root must be an object")
    return value


def load_retail_neutral_simulation_facts(path: Path | str) -> dict[str, Any]:
    """Load bounded neutral simulation facts for exact lifecycle planning."""

    source = Path(path).expanduser().resolve()
    if not source.is_file() or source.stat().st_size > _MAX_SIMULATION_FACTS_BYTES:
        raise ValueError(
            "neutral simulation facts are missing or exceed "
            f"{_MAX_SIMULATION_FACTS_BYTES} bytes"
        )
    try:
        value = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid neutral simulation facts: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError("neutral simulation facts root must be an object")
    return value


def write_retail_neutral_lifecycle_plan(
    path: Path | str, plan: Mapping[str, Any]
) -> None:
    """Write a semantically verified neutral lifecycle plan atomically."""

    _validate_plan_shape(plan)
    write_json_atomic(Path(path), dict(plan))


def _main() -> None:
    parser = argparse.ArgumentParser(
        description="Plan exact CaveTrollLair, Inn, and WargLair conversions"
    )
    parser.add_argument("census", type=Path)
    parser.add_argument("simulation_facts", type=Path)
    parser.add_argument("effective_assets_root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--profile-output", type=Path)
    args = parser.parse_args()
    census = load_retail_neutral_lifecycle_census(args.census)
    simulation_facts = load_retail_neutral_simulation_facts(args.simulation_facts)
    plan = build_retail_neutral_lifecycle_plan(
        census, simulation_facts, args.effective_assets_root
    )
    write_retail_neutral_lifecycle_plan(args.output, plan)
    if args.profile_output is not None:
        write_json_atomic(args.profile_output, generated_import_profile(plan))
    summary = plan["summary"]
    print(
        "retail neutral lifecycle plan: "
        f"{summary['modelResourceCount']} GLBs / "
        f"{summary['uniqueW3dSourceCount']} W3Ds / "
        f"{summary['placementCount']} placements / "
        f"sha256={plan['aggregateSha256']}"
    )


if __name__ == "__main__":
    _main()
