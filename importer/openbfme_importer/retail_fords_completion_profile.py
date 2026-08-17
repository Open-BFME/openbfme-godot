"""Compose the exact late Fords asset batches onto the sealed full slice profile.

The earlier slice composer remains the proof boundary for terrain, roads, Men,
buildings, static props, hierarchical props, and animated props.  This module
adds the bounded late batches that were deliberately left outside that
boundary: the proven header-only Orc meat rack, neutral structure lifecycles,
exact particle/FX definitions, Fords ambient audio, and the Gondor Archer
projectile/impact presentation, the exact Fords environment evidence, and the
exact APT/TGA Men HUD closure.  It also consumes the sealed Men lifecycle-audio
contract to add only the eight previously absent construction-loop envelope
leaves. It also consumes the sealed Men damage-effects contract to add exactly
18 missing particle-definition projections, six missing textures, and four
missing FX-list registry records without resolving cross-family precedence. It
reuses already-owned retail leaves by exact virtual path and rejects every
other accidental overlap.
"""

from __future__ import annotations

import argparse
from copy import deepcopy
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import tempfile
from typing import Any, Mapping, Sequence

from .catalog import InstallCatalog
from .paths import safe_relative_parts
from .profile import ImportProfile, MAX_PROFILE_BYTES, MAX_RESOURCES, resolve_profile
from .retail_men_damage_audio import (
    CONTRACT_SCHEMA as MEN_DAMAGE_AUDIO_SCHEMA,
    ROOT_IDS as MEN_DAMAGE_AUDIO_ROOT_IDS,
)
from .retail_men_damage_effects import (
    PARTICLE_BINDINGS_PATH as MEN_DAMAGE_EFFECTS_RUNTIME_PATH,
    SCHEMA as MEN_DAMAGE_EFFECTS_SCHEMA,
)
from .retail_men_lifecycle_profile import upgrade_men_building_lifecycles
from .util import write_json_atomic


COMPLETION_REPORT_SCHEMA = "openbfme.retail-fords-completion-profile"
COMPLETION_REPORT_SCHEMA_VERSION = 0
BASE_PROFILE_ID = "men-fords-v0-full-generated"
COMPLETE_PROFILE_ID = "men-fords-v0-complete-generated"
PACK_ID = "bfme2-men-vslice"
BASE_RESOURCE_COUNT = 240
MEN_DAMAGE_AUDIO_CONTRACT_AGGREGATE_SHA256 = (
    "148e8089f3754899bb4a933fff61a4bdd5693320a8e6cfb15b6258dceebf206e"
)
MEN_DAMAGE_AUDIO_MISSING_SAMPLE_IDS = (
    "CUBuild_consL1a",
    "CUBuild_consL1b",
    "CUBuild_consL1c",
    "CUBuild_consL1d",
    "CUBuild_consL2w",
    "CUBuild_consL3a",
    "CUBuild_consL3b",
    "CUBuild_consL3c",
)
MEN_DAMAGE_AUDIO_EXPECTED_SUMMARY = {
    "accountedIdentifierCount": 9,
    "definitionBlockerCount": 0,
    "identifierCount": 9,
    "missingDefinitionCount": 0,
    "missingLeafCount": 0,
    "profileAudioRegistryMissingByteCount": 426762,
    "profileAudioRegistryMissingLeafCount": 8,
    "profileResourceCount": 348,
    "profileResourceMissingLeafCount": 8,
    "proposedResourceCount": 1,
    "sourceReferenceCount": 79,
    "uniqueSampleCount": 79,
}
MEN_DAMAGE_EFFECTS_CONTRACT_AGGREGATE_SHA256 = (
    "2659886f2a53c82a1f76f88399e68866dfa4911fe43fc6f08fdd1d3d354dcd3d"
)
MEN_DAMAGE_EFFECTS_EXPECTED_SUMMARY = {
    "allBindingsAccounted": True,
    "directParticleSystemIdCount": 14,
    "duplicateFamilySystemCount": 10,
    "enteringStateFxBindingCount": 17,
    "fxListParticleSystemIdCount": 7,
    "menObjectCount": 5,
    "particleAttachmentCount": 88,
    "particleBearingStateBlockCount": 29,
    "particleDefinitionCandidateCount": 31,
    "textureLeafCount": 13,
    "uniqueFxListCount": 7,
    "uniqueParticleSystemIdCount": 21,
    "w3dLeafCount": 0,
}
MEN_DAMAGE_EFFECTS_DUPLICATE_IDS = (
    "BuildingContructDust",
    "FireBuildingLarge",
    "FireBuildingMedium",
    "FireBuildingSmall",
    "PCTMediumDust",
    "RDTMediumExplosion",
    "RDTMediumExplosionLight",
    "SmokeBuildingLarge",
    "SmokeBuildingMedium",
    "SmokeBuildingMediumRubble",
)
MEN_DAMAGE_EFFECTS_MISSING_FX_LIST_IDS = (
    "FX_BoilingOilAttack",
    "FX_FortressCollapse",
    "FX_FortressDamaged",
    "FX_FortressReallyDamaged",
)
MEN_DAMAGE_EFFECTS_NEW_UNRESOLVED_IDS = (
    "FireBuildingLarge",
    "FireBuildingMedium",
    "FireBuildingSmall",
    "SmokeBuildingMedium",
)
MEN_DAMAGE_EFFECTS_MISSING_TEXTURE_PATHS = (
    "art/compiledtextures/ex/excloud06hires.dds",
    "art/compiledtextures/ex/exexplo03.dds",
    "art/compiledtextures/ex/exfire01.tga",
    "art/compiledtextures/ex/exfirescroll3.dds",
    "art/compiledtextures/ex/exwater01.dds",
    "art/compiledtextures/ex/exwater04.dds",
)

NO_MOTION_SCHEMA = "openbfme.retail-no-motion-prop-plan"
NEUTRAL_SCHEMA = "openbfme.retail-neutral-lifecycle-plan"
PARTICLE_SCHEMA = "openbfme.retail-fords-particle-plan"
AMBIENT_AUDIO_SCHEMA = "openbfme.retail-fords-ambient-audio-plan"
ARCHER_PROJECTILE_SCHEMA = "openbfme.retail-archer-projectile-plan"
FORDS_ENVIRONMENT_SCHEMA = "openbfme.retail-fords-environment-plan"
HUD_APT_SCHEMA = "openbfme.retail-hud-apt-plan"
ANIMATED_RUNTIME_SCHEMA = "openbfme.animated-prop-runtime-contract"
ANIMATED_RUNTIME_PATH = "maps/fords-of-isen-ii/animated-props.json"
ARCHER_PROJECTILE_PACK_KEY = "gondorArcherProjectile"
HUD_APT_RUNTIME_PATH = "data/ui/palantir/scene-contract.json"
HUD_APT_PACK_KEY = "palantirScene"
SHELL_APT_SCHEMA = "openbfme.retail-shell-apt-plan"
SHELL_APT_RESOURCE_ID = "shell-apt-runtime-bundle"
SHELL_APT_RUNTIME_PATH = "data/ui/shell/scene-contract.json"
SHELL_APT_PACK_KEY = "shellScene"
HUD_UI_AUDIO_RESOURCE = {
    "converter": "audio",
    "expected_count": 4,
    "id": "men-hud-ui-audio-leaves",
    "kind": "audio",
    "limit": 4,
    "options": {"force_pcm": True},
    "output": "assets/audio/hud/{stem}.wav",
    "patterns": [
        "data/audio/sounds/uclick.wav",
        "data/audio/sounds/uclick_jewel.wav",
        "data/audio/sounds/uclicknofun.wav",
        "data/audio/sounds/upowerselect.wav",
    ],
    "required": True,
}
HUD_UI_AUDIO_EVENTS = {
    "Gui_PalantirButtonClick": ("UClick_jewel", "50"),
    "Gui_PalantirCommandButtonClick": ("UClick_jewel", "50"),
    "Gui_PalantirCommandButtonDisabledClick": ("UClickNoFun", "50"),
    "Gui_PalantirChoosePowerClick": ("UPowerSelect", "80"),
    "Gui_CloseSpellStoreClick": ("UClick_jewel", "50"),
    "Gui_UpgradeButtonGlow": ("UClick", "50"),
}
FORDS_ENVIRONMENT_RUNTIME_PATH = "maps/fords-of-isen-ii/environment.json"
FORDS_ENVIRONMENT_PACK_KEY = "fordsEnvironment"
MEN_SELECTION_RUNTIME_PATH = "effects/men-selection-decal.json"
MEN_SELECTION_PACK_KEY = "menSelectionDecal"
MEN_ORDER_HINT_RUNTIME_PATH = "effects/men-order-hint.json"
MEN_ORDER_HINT_PACK_KEY = "menOrderHint"
MEN_SELECTION_RESOURCES = [
    {
        "converter": "texture",
        "expected_count": 1,
        "id": "men-selection-decal-level1",
        "kind": "texture",
        "limit": 1,
        "output": "assets/textures/selection/decal_g_level1.png",
        "patterns": ["art/compiledtextures/de/decal_g_level1.dds"],
        "required": True,
    },
    {
        "converter": "texture",
        "expected_count": 1,
        "id": "men-selection-decal-good-co",
        "kind": "texture",
        "limit": 1,
        "output": "assets/textures/selection/decal_good_co.png",
        "patterns": ["art/compiledtextures/de/decal_good_co.dds"],
        "required": True,
    },
]
MEN_SELECTION_RUNTIME = {
    "schema": "openbfme.men-selection-decal",
    "schemaVersion": 0,
    "experienceLevel": "MenGenericLevel1",
    "style": "SHADOW_MERGE_DECAL",
    "textures": [
        "assets/textures/selection/decal_g_level1.png",
        "assets/textures/selection/decal_good_co.png",
    ],
    "opacityMin": 0.8,
    "opacityMax": 1.0,
    "minRadiusSource": 50.0,
    "maxRadiusSource": 200.0,
    "maxSelectedUnits": 40,
    "source": {
        "ini": "data/ini/experiencelevels.ini",
        "textures": [
            {
                "role": "Texture",
                "name": "decal_G_level1",
                "virtualPath": "art/compiledtextures/de/decal_g_level1.dds",
                "sha256": "ac07d037d5bb2a792737300230455f1cc4e2452de79870784e01e5e499430ef5",
            },
            {
                "role": "Texture2",
                "name": "decal_good_CO",
                "virtualPath": "art/compiledtextures/de/decal_good_co.dds",
                "sha256": "4c7147389e37eccd7fde6f05329c607a07fc59f08c5e80712fb3aee8389337d6",
            },
        ],
    },
}
MEN_ORDER_HINT_RESOURCES = [
    {
        "converter": "hash-only",
        "expected_count": 3,
        "id": "men-order-hint-textures",
        "kind": "texture",
        "limit": 3,
        "patterns": [
            "art/compiledtextures/sc/scmovehinta.dds",
            "art/compiledtextures/sc/scmovehintb.dds",
            "art/compiledtextures/sc/scmovehintc.dds",
        ],
        "required": True,
    },
    {
        "converter": "w3d-hierarchical",
        "expected_count": 1,
        "id": "men-order-hint-model",
        "kind": "model",
        "limit": 1,
        "options": {
            "inputResourceIds": ["men-order-hint-textures"],
            "model": "scmovehint.w3d",
        },
        "output": "assets/models/system/scmovehint.glb",
        "patterns": ["art/w3d/sc/scmovehint.w3d"],
        "required": True,
    },
]
MEN_ORDER_HINT_RUNTIME = {
    "schema": "openbfme.men-order-hint",
    "schemaVersion": 0,
    "objectName": "MoveHint",
    "gameDataName": "SCMoveHint",
    "model": "assets/models/system/scmovehint.glb",
    "modelScalePolicy": "source-map-local-transform-scale",
    "source": {
        "gameData": "data/ini/gamedata.ini",
        "objectIni": "data/ini/object/system/system.ini",
        "model": {
            "virtualPath": "art/w3d/sc/scmovehint.w3d",
            "sha256": "ea8198bc58d077a84313cca4c45ddd8b2ab710e57c55d26c5ed2e226b116f37c",
        },
        "textures": [
            {
                "virtualPath": "art/compiledtextures/sc/scmovehinta.dds",
                "sha256": "c69f0d0d2792a32b99064d0d7c9958aa4edf29e23e71fd2f2b25d248034b7ae5",
            },
            {
                "virtualPath": "art/compiledtextures/sc/scmovehintb.dds",
                "sha256": "a3f6534b84afcff1c807a941f12fbeef722d8f453031c19887f662091bc002e4",
            },
            {
                "virtualPath": "art/compiledtextures/sc/scmovehintc.dds",
                "sha256": "ffbda7ef3d02b7df9a140e968d920996a69a6c1ec5818f6fddfcec468f81ac6b",
            },
        ],
    },
}
DEFAULT_MEN_LIFECYCLE_REPORT_PATH = Path(
    "workspace/retail-work/reports/retail-men-building-lifecycle.json"
)

EXPECTED_SUMMARIES: dict[str, dict[str, int]] = {
    NO_MOTION_SCHEMA: {
        "animationClipCount": 0,
        "conversionGroupCount": 1,
        "eligibleTargetTypeCount": 1,
        "modelResourceCount": 1,
        "objectBindingModelRowCount": 1,
        "placementCount": 1,
        "profileResourceCount": 2,
        "removedHeaderOnlyAnimationCount": 1,
        "sourcePatternCount": 2,
        "textureResourceCount": 1,
    },
    NEUTRAL_SCHEMA: {
        "audioEvidenceResourceCount": 40,
        "authoredNoRenderPhaseCount": 6,
        "lifecyclePhaseCount": 26,
        "modelResourceCount": 22,
        "placementCount": 8,
        "profileResourceCount": 84,
        "structureBindingCount": 3,
        "structureTypeCount": 3,
        "textureEvidenceResourceCount": 22,
        "uniqueAudioSampleSourceCount": 40,
        "uniqueModelWeatherTextureSourceCount": 22,
        "uniqueW3dSourceCount": 24,
    },
    PARTICLE_SCHEMA: {
        "anchorResourceCount": 1,
        "definitionResourceCount": 17,
        "directAttachmentCount": 11,
        "duplicateFamilySystemCount": 7,
        "fxDefinitionCount": 10,
        "fxListCount": 4,
        "fxRootCount": 13,
        "legacyDefinitionCount": 7,
        "particleSystemIdCount": 10,
        "placementCount": 15,
        "profileResourceCount": 27,
        "runtimeSelectionCount": 7,
        "targetTypeCount": 4,
        "textureResourceCount": 9,
        "unresolvedFamilySelectionCount": 0,
    },
    AMBIENT_AUDIO_SCHEMA: {
        "ambientStreamCount": 1,
        "audioEventCount": 6,
        "catalogSelectedEntryCount": 57,
        "mapPlacementCount": 50,
        "mp3SampleCount": 1,
        "multisoundCount": 0,
        "profileResourceCount": 2,
        "reusedSampleCount": 0,
        "rootCount": 7,
        "sampleCount": 57,
        "wavSampleCount": 56,
    },
    ARCHER_PROJECTILE_SCHEMA: {
        "audioLeafCount": 100,
        "damageFxSetMappingCount": 4,
        "definitionEvidenceFileCount": 3,
        "exactClosureSourceFileCount": 109,
        "fireAudioLeafCount": 32,
        "impactAudioLeafCount": 68,
        "impactModelCount": 1,
        "newSelectedSourceFileCount": 106,
        "profileFragmentResourceCount": 4,
        "reusedExactLeafCount": 3,
        "streakTextureCount": 2,
        "unresolvedEngineSemanticCount": 5,
    },
    FORDS_ENVIRONMENT_SCHEMA: {
        "blockerCount": 4,
        "catalogSelectedEntryCount": 3,
        "completeSkyboxTextureSetCount": 8,
        "profileResourceCount": 3,
        "resolvedSkyboxFaceAssignmentCount": 40,
        "skyboxTextureSetCount": 11,
        "uniqueResolvedSkyboxFaceFileCount": 37,
        "worldSkySelectionResolved": False,
    },
    HUD_APT_SCHEMA: {
        "aptBundleCount": 9,
        "aptClosureFileCount": 260,
        "aptClosurePayloadBytes": 10410403,
        "atlasCount": 24,
        "compositeOutputCount": 6,
        "geometryFileCount": 209,
        "fontResourceCount": 1,
        "layoutFileCount": 1,
        "movieCount": 9,
        "nativeCallbackWhitelistCount": 32,
        "profileResourceCount": 2,
        "profileSelectedSourceCount": 262,
        "runtimeBundleResourceCount": 1,
        "sourceAttestationResourceCount": 0,
        "textureConversionResourceCount": 0,
        "unresolvedExternalMovieCount": 0,
        "wndWindowCount": 87,
    },
}

DISPLAY_NAMES = {
    "bfme2.object.neutral-cave-troll-lair": "Cave Troll Lair",
    "bfme2.object.neutral-inn": "Inn",
    "bfme2.object.neutral-warg-lair": "Warg Lair",
}


@dataclass(frozen=True, slots=True)
class ComposedRetailFordsCompletion:
    profile: dict[str, Any]
    report: dict[str, Any]


def _canonical_bytes(value: Any) -> bytes:
    return json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")


def _canonical_sha256(value: Any) -> str:
    return hashlib.sha256(_canonical_bytes(value)).hexdigest()


def _pretty_json_bytes(value: Any) -> bytes:
    return (
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    ).encode("utf-8")


def _file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _mapping(value: object, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be an object")
    return value


def _array(value: object, label: str) -> list[Any]:
    if not isinstance(value, list):
        raise ValueError(f"{label} must be an array")
    return value


def _text(value: object, label: str) -> str:
    if not isinstance(value, str) or not value:
        raise ValueError(f"{label} must be a nonempty string")
    return value


def _load_document(path: Path | str, label: str) -> tuple[dict[str, Any], Path]:
    source = Path(path).expanduser().resolve()
    if source.stat().st_size > MAX_PROFILE_BYTES:
        raise ValueError(f"{label} exceeds the {MAX_PROFILE_BYTES}-byte safety bound")
    try:
        value = json.loads(source.read_text("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid {label}: {exc}") from exc
    return _mapping(value, label), source


def _validate_declared_digest(document: Mapping[str, Any], label: str) -> str:
    declared = _text(document.get("aggregateSha256"), f"{label} aggregateSha256")
    payload = {
        key: value for key, value in document.items() if key != "aggregateSha256"
    }
    calculated = _canonical_sha256(payload)
    if declared != calculated:
        raise ValueError(
            f"{label} aggregate digest mismatch: declared {declared}, calculated {calculated}"
        )
    return declared


def _validate_plan(
    document: dict[str, Any],
    *,
    schema: str,
    fragment_keys: set[str],
    resource_count_key: str = "profileResourceCount",
) -> dict[str, Any]:
    if document.get("schema") != schema or document.get("schemaVersion") != 0:
        raise ValueError(f"unsupported {schema} document")
    _validate_declared_digest(document, schema)
    summary = _mapping(document.get("summary"), f"{schema} summary")
    if summary != EXPECTED_SUMMARIES[schema]:
        raise ValueError(f"{schema} exact summary changed")
    fragment = _mapping(document.get("profileFragment"), f"{schema} profileFragment")
    if set(fragment) != fragment_keys:
        raise ValueError(f"{schema} profileFragment fields changed")
    resources = _array(fragment.get("resources"), f"{schema} resources")
    if len(resources) != summary[resource_count_key]:
        raise ValueError(f"{schema} profile resource count changed")
    return fragment


def _validate_men_damage_audio_contract(
    document: dict[str, Any],
) -> dict[str, Any]:
    if (
        document.get("schema") != MEN_DAMAGE_AUDIO_SCHEMA
        or document.get("schemaVersion") != 0
    ):
        raise ValueError("unsupported Men damage-audio contract")
    digest = _validate_declared_digest(document, "Men damage-audio contract")
    if digest != MEN_DAMAGE_AUDIO_CONTRACT_AGGREGATE_SHA256:
        raise ValueError("Men damage-audio contract aggregate changed")
    if document.get("summary") != MEN_DAMAGE_AUDIO_EXPECTED_SUMMARY:
        raise ValueError("Men damage-audio exact summary changed")
    scope = _mapping(document.get("scope"), "Men damage-audio scope")
    if scope.get("rootIds") != list(MEN_DAMAGE_AUDIO_ROOT_IDS):
        raise ValueError("Men damage-audio root identity closure changed")

    definitions = _array(document.get("definitions"), "Men damage-audio definitions")
    if len(definitions) != len(MEN_DAMAGE_AUDIO_ROOT_IDS):
        raise ValueError("Men damage-audio definition closure changed")
    seen_definitions: set[str] = set()
    for raw in definitions:
        definition = _mapping(raw, "Men damage-audio definition")
        requested = _text(definition.get("requestedId"), "damage-audio requestedId")
        if (
            requested not in MEN_DAMAGE_AUDIO_ROOT_IDS
            or requested in seen_definitions
            or definition.get("declaredId") != requested
            or definition.get("caseExact") is not True
            or definition.get("profileEventRegistryExact") is not True
        ):
            raise ValueError("Men damage-audio exact definition identity changed")
        seen_definitions.add(requested)
        precedence = _mapping(
            definition.get("definitionPrecedence"),
            f"Men damage-audio {requested} precedence",
        )
        source_file = _mapping(
            precedence.get("sourceFile"), f"Men damage-audio {requested} sourceFile"
        )
        if (
            precedence.get("effectiveDefinitionCount") != 1
            or precedence.get("inheritanceParent") is not None
            or precedence.get("policy") != "unique-effective-definition-no-inheritance"
            or source_file.get("candidateCount") != 1
            or source_file.get("ignoredLowerPrecedenceEntries") != []
        ):
            raise ValueError("Men damage-audio definition precedence changed")

    samples = _array(document.get("uniqueSamples"), "Men damage-audio samples")
    if len(samples) != 79:
        raise ValueError("Men damage-audio sample closure changed")
    sample_ids: set[str] = set()
    missing_ids: list[str] = []
    for raw in samples:
        sample = _mapping(raw, "Men damage-audio sample")
        sample_id = _text(sample.get("id"), "Men damage-audio sample id")
        if sample_id.casefold() in sample_ids:
            raise ValueError("Men damage-audio sample IDs are not case-unique")
        sample_ids.add(sample_id.casefold())
        path = _text(sample.get("path"), f"Men damage-audio sample {sample_id} path")
        safe_relative_parts(path)
        if sample.get("registryPresent") is False:
            missing_ids.append(sample_id)
            if sample.get("profileResourceIds") != []:
                raise ValueError("missing Men damage-audio leaf gained an owner")
        elif sample.get("registryPresent") is not True or not _array(
            sample.get("profileResourceIds"),
            f"Men damage-audio sample {sample_id} owners",
        ):
            raise ValueError("owned Men damage-audio leaf evidence changed")
    if tuple(missing_ids) != MEN_DAMAGE_AUDIO_MISSING_SAMPLE_IDS:
        raise ValueError("Men damage-audio missing leaf closure changed")

    blockers = _array(document.get("blockers"), "Men damage-audio blockers")
    if len(blockers) != 8 or {
        _text(item.get("sampleId"), "Men damage-audio blocker sampleId")
        for item in blockers
        if isinstance(item, dict)
        and item.get("code") == "profile-audio-registry-leaf-missing"
    } != set(MEN_DAMAGE_AUDIO_MISSING_SAMPLE_IDS):
        raise ValueError("Men damage-audio exact profile gap changed")

    fragment = _mapping(
        document.get("profileFragmentProposal"),
        "Men damage-audio profileFragmentProposal",
    )
    if (
        set(fragment) != {"integrationStatus", "resources", "runtimeDataMerge"}
        or fragment.get("integrationStatus") != "proposal-only-not-integrated"
    ):
        raise ValueError("Men damage-audio profile fragment shape changed")
    resources = _array(fragment.get("resources"), "Men damage-audio resources")
    if len(resources) != 1:
        raise ValueError("Men damage-audio resource delta changed")
    resource = _mapping(resources[0], "Men damage-audio resource")
    expected_paths = [
        f"data/audio/sounds/{sample_id.casefold()}.wav"
        for sample_id in MEN_DAMAGE_AUDIO_MISSING_SAMPLE_IDS
    ]
    if resource != {
        "converter": "audio",
        "expected_count": 8,
        "id": "men-building-lifecycle-envelope-audio-leaves",
        "kind": "audio",
        "limit": 8,
        "options": {"force_pcm": True},
        "output": "assets/audio/men-building-lifecycle/{stem}.wav",
        "patterns": expected_paths,
    }:
        raise ValueError("Men damage-audio exact resource changed")
    runtime_merge = _mapping(
        fragment.get("runtimeDataMerge"), "Men damage-audio runtimeDataMerge"
    )
    if set(runtime_merge) != {"data/audio_events.json"}:
        raise ValueError("Men damage-audio runtime merge target changed")
    merge_document = _mapping(
        runtime_merge["data/audio_events.json"],
        "Men damage-audio registry merge",
    )
    expected_samples = {
        sample_id: (f"assets/audio/men-building-lifecycle/{sample_id.casefold()}.wav")
        for sample_id in MEN_DAMAGE_AUDIO_MISSING_SAMPLE_IDS
    }
    if merge_document != {"samples": expected_samples}:
        raise ValueError("Men damage-audio exact registry delta changed")
    return fragment


def _validate_men_damage_effects_contract(
    document: dict[str, Any],
) -> dict[str, Any]:
    if (
        document.get("schema") != MEN_DAMAGE_EFFECTS_SCHEMA
        or document.get("schemaVersion") != 0
    ):
        raise ValueError("unsupported Men damage-effects contract")
    digest = _validate_declared_digest(document, "Men damage-effects contract")
    if digest != MEN_DAMAGE_EFFECTS_CONTRACT_AGGREGATE_SHA256:
        raise ValueError("Men damage-effects contract aggregate changed")
    if document.get("summary") != MEN_DAMAGE_EFFECTS_EXPECTED_SUMMARY:
        raise ValueError("Men damage-effects exact summary changed")

    bindings = _mapping(document.get("bindings"), "Men damage-effects bindings")
    fx_bindings = _array(
        bindings.get("enteringStateFx"), "Men damage-effects enteringStateFx"
    )
    attachments = _array(
        bindings.get("particleAttachments"), "Men damage-effects attachments"
    )
    state_blocks = _array(
        bindings.get("particleStateBlocks"), "Men damage-effects state blocks"
    )
    if len(fx_bindings) != 17 or len(attachments) != 88 or len(state_blocks) != 29:
        raise ValueError("Men damage-effects binding closure changed")
    indexes = sorted(
        index
        for raw in state_blocks
        for index in _array(
            _mapping(raw, "Men damage-effects state block").get("attachmentIndexes"),
            "Men damage-effects state attachment indexes",
        )
    )
    if indexes != list(range(88)):
        raise ValueError("Men damage-effects state-block ownership changed")

    definitions = _array(
        document.get("particleDefinitions"), "Men damage-effects definitions"
    )
    if len(definitions) != 21:
        raise ValueError("Men damage-effects particle-system closure changed")
    duplicate_ids: list[str] = []
    candidate_count = 0
    missing_candidate_count = 0
    for raw in definitions:
        definition = _mapping(raw, "Men damage-effects definition")
        system_id = _text(
            definition.get("particleSystemId"), "Men damage-effects system id"
        )
        candidates = _array(
            definition.get("definitionCandidates"),
            f"Men damage-effects {system_id} candidates",
        )
        resolution = _mapping(
            definition.get("familyResolution"),
            f"Men damage-effects {system_id} familyResolution",
        )
        candidate_count += len(candidates)
        kinds: list[str] = []
        for raw_candidate in candidates:
            candidate = _mapping(
                raw_candidate, f"Men damage-effects {system_id} candidate"
            )
            kind = _text(candidate.get("kind"), f"{system_id} candidate kind")
            if kind not in {"ParticleSystem", "FXParticleSystem"}:
                raise ValueError("Men damage-effects definition family changed")
            if candidate.get("definitionId") != system_id:
                raise ValueError("Men damage-effects definition identity changed")
            source = _mapping(candidate.get("source"), f"{system_id} source")
            expected_path = (
                "data/ini/particlesystem.ini"
                if kind == "ParticleSystem"
                else "data/ini/fxparticlesystem.ini"
            )
            if source.get("virtualPath") != expected_path:
                raise ValueError("Men damage-effects definition source changed")
            span = _mapping(candidate.get("sourceSpan"), f"{system_id} sourceSpan")
            if not isinstance(span.get("byteLength"), int) or not span.get("sha256"):
                raise ValueError("Men damage-effects definition provenance changed")
            coverage = _mapping(
                candidate.get("profileCoverage"), f"{system_id} profileCoverage"
            )
            resource_ids = _array(
                coverage.get("definitionResourceIds"),
                f"{system_id} definition resource owners",
            )
            present = coverage.get("fordsParticleBindingRegistryPresent")
            if present is True:
                if len(resource_ids) != 1 or not coverage.get(
                    "fordsParticleBindingRegistryResourceId"
                ):
                    raise ValueError("Men damage-effects owned definition changed")
            elif present is False:
                if (
                    resource_ids
                    or coverage.get("fordsParticleBindingRegistryResourceId")
                    is not None
                ):
                    raise ValueError(
                        "Men damage-effects missing definition gained owner"
                    )
                missing_candidate_count += 1
            else:
                raise ValueError("Men damage-effects definition coverage changed")
            kinds.append(kind)
        if len(candidates) == 2:
            duplicate_ids.append(system_id)
            if (
                set(kinds) != {"ParticleSystem", "FXParticleSystem"}
                or resolution.get("status") != "unresolved-cross-family-precedence"
                or resolution.get("selectedKind") is not None
                or resolution.get("crossFamilyPrecedenceProven") is not False
            ):
                raise ValueError("Men damage-effects duplicate-family policy changed")
        elif (
            len(candidates) != 1
            or resolution.get("status") != "exact-single-authored-family"
            or resolution.get("selectedKind") != kinds[0]
        ):
            raise ValueError("Men damage-effects single-family selection changed")
    if candidate_count != 31 or missing_candidate_count != 18:
        raise ValueError("Men damage-effects definition resource delta changed")
    if tuple(duplicate_ids) != MEN_DAMAGE_EFFECTS_DUPLICATE_IDS:
        raise ValueError("Men damage-effects duplicate identifier set changed")

    leaves = _array(document.get("renderLeaves"), "Men damage-effects renderLeaves")
    if len(leaves) != 13:
        raise ValueError("Men damage-effects render-leaf closure changed")
    missing_textures: list[str] = []
    for raw in leaves:
        leaf = _mapping(raw, "Men damage-effects render leaf")
        path = _text(leaf.get("virtualPath"), "Men damage-effects render path")
        safe_relative_parts(path)
        if leaf.get("kind") != "texture":
            raise ValueError("Men damage-effects gained a W3D render leaf")
        source = _mapping(leaf.get("source"), f"Men damage-effects {path} source")
        if source.get("virtualPath") != path or not source.get("sha256"):
            raise ValueError("Men damage-effects render-leaf provenance changed")
        owners = _array(
            leaf.get("profileResourceIds"), f"Men damage-effects {path} owners"
        )
        if not owners:
            missing_textures.append(path)
            if not leaf.get("proposedResourceId"):
                raise ValueError("Men damage-effects missing texture proposal changed")
    if tuple(missing_textures) != MEN_DAMAGE_EFFECTS_MISSING_TEXTURE_PATHS:
        raise ValueError("Men damage-effects texture resource delta changed")

    coverage = _mapping(
        document.get("profileCoverage"), "Men damage-effects profileCoverage"
    )
    if coverage != {
        "existingFxListIds": [
            "FX_BuildingDamaged",
            "FX_BuildingReallyDamaged",
            "FX_StructureMediumCollapse",
        ],
        "missingDefinitionResourceCount": 18,
        "missingFxListIds": list(MEN_DAMAGE_EFFECTS_MISSING_FX_LIST_IDS),
        "missingTextureResourceCount": 6,
        "missingW3dResourceCount": 0,
        "resourceDeltaCount": 24,
    }:
        raise ValueError("Men damage-effects exact profile coverage changed")

    fragment = _mapping(
        document.get("profileFragmentProposal"),
        "Men damage-effects profileFragmentProposal",
    )
    if (
        set(fragment) != {"integrationStatus", "resources", "runtimeDataPatch"}
        or fragment.get("integrationStatus") != "proposal-only-not-integrated"
    ):
        raise ValueError("Men damage-effects profile fragment shape changed")
    resources = _array(fragment.get("resources"), "Men damage-effects resources")
    if len(resources) != 24:
        raise ValueError("Men damage-effects resource proposal changed")
    converters = [
        _mapping(row, "Men damage-effects resource").get("converter")
        for row in resources
    ]
    if (
        converters.count("sage-particle-definition") != 18
        or converters.count("texture") != 6
    ):
        raise ValueError("Men damage-effects resource converter delta changed")
    patch = _mapping(
        fragment.get("runtimeDataPatch"), "Men damage-effects runtimeDataPatch"
    )
    if (
        set(patch)
        != {
            "targetRuntimeDataPath",
            "definitionRegistryAppend",
            "fxListsUpsert",
            "objectBindingsAppend",
            "unresolvedDuplicateIdentifierSystemIdsAppend",
        }
        or patch.get("targetRuntimeDataPath") != MEN_DAMAGE_EFFECTS_RUNTIME_PATH
        or len(
            _array(
                patch.get("definitionRegistryAppend"),
                "Men damage-effects definition registry append",
            )
        )
        != 18
        or len(
            _array(
                patch.get("objectBindingsAppend"),
                "Men damage-effects object binding evidence",
            )
        )
        != 5
        or patch.get("unresolvedDuplicateIdentifierSystemIdsAppend")
        != list(MEN_DAMAGE_EFFECTS_NEW_UNRESOLVED_IDS)
    ):
        raise ValueError("Men damage-effects runtime patch changed")
    upserts = _array(patch.get("fxListsUpsert"), "Men damage-effects FX-list upserts")
    if (
        tuple(
            _text(
                _mapping(row, "Men damage-effects FX-list upsert").get("fxListId"),
                "Men damage-effects FX-list id",
            )
            for row in upserts
        )
        != MEN_DAMAGE_EFFECTS_MISSING_FX_LIST_IDS
    ):
        raise ValueError("Men damage-effects FX-list delta changed")

    blockers = _array(document.get("blockers"), "Men damage-effects blockers")
    precedence = next(
        (
            _mapping(row, "Men damage-effects precedence blocker")
            for row in blockers
            if isinstance(row, dict)
            and row.get("code") == "cross-family-precedence-unresolved"
        ),
        None,
    )
    if (
        precedence is None
        or precedence.get("particleSystemIds") != list(MEN_DAMAGE_EFFECTS_DUPLICATE_IDS)
        or precedence.get("status") != "preserve-both-candidates-fail-closed"
    ):
        raise ValueError("Men damage-effects precedence blocker changed")
    return fragment


def _merge_men_damage_effects_runtime(
    runtime: dict[str, Any],
    contract: Mapping[str, Any],
    resources: Sequence[Mapping[str, Any]],
) -> tuple[int, int, int]:
    """Validate the old owner boundary, then attach only the sealed delta."""

    fragment = _mapping(
        contract.get("profileFragmentProposal"), "Men damage-effects fragment"
    )
    patch = _mapping(fragment.get("runtimeDataPatch"), "Men damage-effects patch")
    proposed_resources = _array(fragment.get("resources"), "Men damage resources")
    proposed_ids = {
        _resource_id(_mapping(row, "Men damage resource"), "Men damage resource")
        for row in proposed_resources
    }
    resource_by_id = {
        _resource_id(resource, "completed resource"): resource for resource in resources
    }
    if not proposed_ids <= set(resource_by_id):
        raise ValueError("Men damage-effects proposed resources are not all attached")
    prior_resources = {
        resource_id: resource
        for resource_id, resource in resource_by_id.items()
        if resource_id not in proposed_ids
    }

    registry = _array(runtime.get("definitionRegistry"), "particle definitionRegistry")
    registry_by_key = {
        (
            str(
                _mapping(row, "particle definition registry row").get("kind")
            ).casefold(),
            str(
                _mapping(row, "particle definition registry row").get("definitionId")
            ).casefold(),
        ): _mapping(row, "particle definition registry row")
        for row in registry
    }
    leaves = _array(contract.get("renderLeaves"), "Men damage renderLeaves")
    for raw_leaf in leaves:
        leaf = _mapping(raw_leaf, "Men damage render leaf")
        expected_owners = set(
            _array(leaf.get("profileResourceIds"), "Men damage leaf owners")
        )
        path = _text(leaf.get("virtualPath"), "Men damage leaf path")
        actual_owners = {
            resource_id
            for resource_id, resource in prior_resources.items()
            if path in _patterns(resource, f"Men damage prior resource {resource_id}")
        }
        if actual_owners != expected_owners:
            raise ValueError(
                f"Men damage-effects current texture owner delta changed: {path}"
            )

    definitions = _array(contract.get("particleDefinitions"), "Men damage definitions")
    for raw_definition in definitions:
        definition = _mapping(raw_definition, "Men damage definition")
        for raw_candidate in _array(
            definition.get("definitionCandidates"), "Men damage candidates"
        ):
            candidate = _mapping(raw_candidate, "Men damage candidate")
            coverage = _mapping(candidate.get("profileCoverage"), "candidate coverage")
            key = (
                str(candidate.get("kind")).casefold(),
                str(candidate.get("definitionId")).casefold(),
            )
            registry_row = registry_by_key.get(key)
            expected_present = coverage.get("fordsParticleBindingRegistryPresent")
            if (registry_row is not None) is not expected_present:
                raise ValueError(
                    "Men damage-effects current definition registry delta changed"
                )
            expected_ids = set(
                _array(
                    coverage.get("definitionResourceIds"),
                    "Men damage definition resource owners",
                )
            )
            actual_ids = {
                resource_id
                for resource_id, resource in prior_resources.items()
                if resource.get("converter") == "sage-particle-definition"
                and isinstance(resource.get("options"), dict)
                and str(resource["options"].get("kind", "")).casefold() == key[0]
                and str(resource["options"].get("name", "")).casefold() == key[1]
            }
            if actual_ids != expected_ids:
                raise ValueError(
                    "Men damage-effects current definition owner delta changed"
                )

    registry_append = _array(
        patch.get("definitionRegistryAppend"), "Men damage definitionRegistryAppend"
    )
    for raw in registry_append:
        row = deepcopy(_mapping(raw, "Men damage definition registry append"))
        key = (str(row.get("kind")).casefold(), str(row.get("definitionId")).casefold())
        if key in registry_by_key:
            raise ValueError("Men damage-effects definition registry append collides")
        definition_resource_id = _text(
            row.get("definitionResourceId"),
            "Men damage definition registry resource id",
        )
        definition_resource = resource_by_id.get(definition_resource_id)
        if (
            definition_resource_id not in proposed_ids
            or definition_resource is None
            or definition_resource.get("converter") != "sage-particle-definition"
            or definition_resource.get("output") != row.get("definitionOutputJson")
        ):
            raise ValueError("Men damage-effects definition registry owner changed")
        texture_ids = _array(
            row.get("textureResourceIds"),
            "Men damage definition registry texture owners",
        )
        if any(texture_id not in resource_by_id for texture_id in texture_ids):
            raise ValueError("Men damage-effects texture registry owner is missing")
        registry.append(row)
        registry_by_key[key] = row

    fx_lists = _array(runtime.get("fxLists"), "particle FX lists")
    fx_by_id = {
        str(_mapping(row, "particle FX list").get("fxListId")).casefold()
        for row in fx_lists
    }
    upserts = _array(patch.get("fxListsUpsert"), "Men damage FX-list upserts")
    for raw in upserts:
        row = deepcopy(_mapping(raw, "Men damage FX-list upsert"))
        folded = str(row.get("fxListId")).casefold()
        if folded in fx_by_id:
            raise ValueError("Men damage-effects FX-list upsert collides")
        fx_lists.append(row)
        fx_by_id.add(folded)

    family = _mapping(runtime.get("familyResolution"), "particle familyResolution")
    duplicate_ids = _array(
        family.get("duplicateIdentifierSystemIds"), "particle duplicate IDs"
    )
    unresolved_ids = _array(
        family.get("unresolvedDuplicateIdentifierSystemIds"),
        "particle unresolved duplicate IDs",
    )
    new_unresolved = _array(
        patch.get("unresolvedDuplicateIdentifierSystemIdsAppend"),
        "Men damage unresolved duplicate append",
    )
    new_proven_fx = _array(
        patch.get("provenFxDuplicateIdentifierSystemIdsAppend", []),
        "Men damage proven FX duplicate append",
    )
    for system_id in new_unresolved:
        if system_id in duplicate_ids or system_id in unresolved_ids:
            raise ValueError("Men damage-effects unresolved duplicate append collides")
        duplicate_ids.append(system_id)
        unresolved_ids.append(system_id)
    selections = _array(family.get("runtimeSelections", []), "particle runtime selections")
    selection_ids = {
        str(_mapping(row, "particle runtime selection").get("particleSystemId"))
        for row in selections
    }
    for system_id in new_proven_fx:
        if system_id in duplicate_ids or system_id in unresolved_ids or system_id in selection_ids:
            raise ValueError("Men damage-effects proven FX duplicate append collides")
        duplicate_ids.append(system_id)
        selections.append(
            {
                "particleSystemId": system_id,
                "status": "proven-effective-fx-manager-family",
                "selectedKind": "FXParticleSystem",
                "crossFamilyPrecedenceProven": True,
                "generalizesToOtherDuplicateIdentifiers": True,
            }
        )
        selection_ids.add(system_id)
    if family.get("status") == "proven-effective-fx-manager-family":
        if set(duplicate_ids) != selection_ids or unresolved_ids:
            raise ValueError("Men damage-effects proven FX family closure is incomplete")
        family["proof"] = (
            "Retail registers only TheFXParticleSystemManager and the supported "
            "game.dat binaries have no legacy ParticleSystem.ini load literal."
        )
    else:
        if not set(MEN_DAMAGE_EFFECTS_DUPLICATE_IDS) <= set(unresolved_ids):
            raise ValueError("Men damage-effects cross-family blocker is incomplete")
        family["blocker"] = (
            "Cross-family precedence is not proven by this input contract; preserve "
            "both authored candidates and fail closed."
        )
    return len(registry_append), len(upserts), len(new_unresolved) + len(new_proven_fx)


def _merge_men_damage_audio_registry(
    base: dict[str, Any], contract: Mapping[str, Any]
) -> tuple[int, int, int]:
    events = _mapping(base.get("events"), "base Men damage-audio events")
    samples = _mapping(base.get("samples"), "base Men damage-audio samples")
    event_index = {
        str(key).casefold(): (str(key), value) for key, value in events.items()
    }
    sample_index = {
        str(key).casefold(): (str(key), value) for key, value in samples.items()
    }
    definitions = _array(contract.get("definitions"), "Men damage-audio definitions")
    for raw in definitions:
        definition = _mapping(raw, "Men damage-audio definition")
        event_id = _text(definition.get("requestedId"), "Men damage-audio event id")
        body = []
        for raw_sample in _array(
            definition.get("samplesInSourceOrder"),
            f"Men damage-audio {event_id} samples",
        ):
            sample = _mapping(raw_sample, f"Men damage-audio {event_id} sample")
            if sample.get("role") != "body":
                continue
            row = {"id": _text(sample.get("id"), "Men damage-audio body sample")}
            if "weight" in sample:
                row["weight"] = sample["weight"]
            body.append(row)
        expected = {
            "parameters": deepcopy(
                _array(
                    definition.get("parametersInSourceOrder"),
                    f"Men damage-audio {event_id} parameters",
                )
            ),
            "sounds": body,
        }
        existing = event_index.get(event_id.casefold())
        if existing is None or existing != (event_id, expected):
            raise ValueError(f"base Men damage-audio event changed: {event_id!r}")

    existing_leaf_count = 0
    missing_rows: list[dict[str, Any]] = []
    for raw in _array(contract.get("uniqueSamples"), "Men damage-audio samples"):
        sample = _mapping(raw, "Men damage-audio sample")
        sample_id = _text(sample.get("id"), "Men damage-audio sample id")
        existing = sample_index.get(sample_id.casefold())
        if sample.get("registryPresent") is True:
            expected_output = _text(
                sample.get("profileAudioRegistryOutput"),
                f"Men damage-audio {sample_id} registry output",
            )
            if existing != (sample_id, expected_output):
                raise ValueError(
                    f"owned Men damage-audio registry leaf changed: {sample_id!r}"
                )
            existing_leaf_count += 1
        else:
            if existing is not None:
                raise ValueError(
                    f"Men damage-audio proposed leaf already exists: {sample_id!r}"
                )
            missing_rows.append(sample)

    fragment = _mapping(
        contract.get("profileFragmentProposal"), "Men damage-audio fragment"
    )
    runtime_merge = _mapping(
        fragment.get("runtimeDataMerge"), "Men damage-audio runtimeDataMerge"
    )
    addition = _mapping(
        _mapping(
            runtime_merge.get("data/audio_events.json"),
            "Men damage-audio registry merge",
        ).get("samples"),
        "Men damage-audio sample additions",
    )
    if len(missing_rows) != 8 or len(addition) != 8:
        raise ValueError("Men damage-audio registry delta changed")
    for sample_id in MEN_DAMAGE_AUDIO_MISSING_SAMPLE_IDS:
        output = _text(addition.get(sample_id), f"Men damage-audio {sample_id} output")
        if sample_id.casefold() in sample_index:
            raise ValueError(f"Men damage-audio sample collision: {sample_id!r}")
        samples[sample_id] = output
        sample_index[sample_id.casefold()] = (sample_id, output)
    return len(definitions), existing_leaf_count, len(addition)


def _merge_audio_registry(
    base: dict[str, Any],
    addition: Mapping[str, Any],
    *,
    expected_root_count: int = 7,
    label: str = "ambient",
) -> tuple[int, int]:
    expected_keys = {
        "complete",
        "events",
        "multisounds",
        "rootIds",
        "samples",
        "schema",
        "schemaVersion",
    }
    if set(base) != expected_keys or set(addition) != expected_keys:
        raise ValueError("audio registry merge document shape changed")
    if (
        base.get("schema") != "openbfme.audio-events"
        or addition.get("schema") != "openbfme.audio-events"
        or base.get("schemaVersion") != 1
        or addition.get("schemaVersion") != 1
        or addition.get("complete") is not True
    ):
        raise ValueError("audio registry merge schema changed")

    added_events = _mapping(addition.get("events"), f"{label} audio events")
    added_multisounds = _mapping(
        addition.get("multisounds"), f"{label} audio multisounds"
    )
    added_samples = _mapping(addition.get("samples"), f"{label} audio samples")
    base_events = _mapping(base.get("events"), "base audio events")
    base_multisounds = _mapping(base.get("multisounds"), "base audio multisounds")
    base_samples = _mapping(base.get("samples"), "base audio samples")

    existing_logical = {
        str(value).casefold() for value in [*base_events, *base_multisounds]
    }
    for collection, values, kind_label in (
        (base_events, added_events, "audio event"),
        (base_multisounds, added_multisounds, "audio multisound"),
    ):
        for raw_name, value in values.items():
            name = _text(raw_name, kind_label)
            if name.casefold() in existing_logical:
                raise ValueError(f"{label} {kind_label} collision: {name!r}")
            existing_logical.add(name.casefold())
            collection[name] = deepcopy(value)

    existing_samples = {str(value).casefold() for value in base_samples}
    for raw_name, raw_path in added_samples.items():
        name = _text(raw_name, f"{label} audio sample id")
        path = _text(raw_path, f"{label} audio sample {name!r} path")
        safe_relative_parts(path)
        if name.casefold() in existing_samples:
            raise ValueError(f"{label} audio sample collision: {name!r}")
        existing_samples.add(name.casefold())
        base_samples[name] = path

    roots = _array(base.get("rootIds"), "base audio rootIds")
    existing_roots = {str(value).casefold() for value in roots}
    added_roots = _array(addition.get("rootIds"), f"{label} audio rootIds")
    if len(added_roots) != expected_root_count:
        raise ValueError(f"{label} audio root closure changed")
    for raw_root in added_roots:
        root = _text(raw_root, "ambient audio root id")
        if root.casefold() in existing_roots or root not in added_events:
            raise ValueError(
                f"{label} audio root collision or missing event: {root!r}"
            )
        existing_roots.add(root.casefold())
        roots.append(root)

    # The addition is complete for its roots, not for the broader base
    # registry. Never upgrade a deliberately incomplete global closure.
    base["complete"] = bool(base.get("complete")) and bool(addition.get("complete"))
    return len(added_events) + len(added_multisounds), len(added_samples)


def _merge_hud_ui_audio_registry(base: dict[str, Any]) -> tuple[int, int]:
    events = _mapping(base.get("events"), "base HUD UI audio events")
    samples = _mapping(base.get("samples"), "base HUD UI audio samples")
    for sample_id in ("UClick", "UClick_jewel", "UClickNoFun", "UPowerSelect"):
        if sample_id in samples:
            raise ValueError(f"HUD UI audio sample collision: {sample_id}")
        samples[sample_id] = f"assets/audio/hud/{sample_id.casefold()}.wav"
    for event_id, (sample_id, volume) in HUD_UI_AUDIO_EVENTS.items():
        if event_id in events:
            raise ValueError(f"HUD UI audio event collision: {event_id}")
        events[event_id] = {
            "parameters": [
                {"field": "ReverbEffectLevel", "value": "0"},
                {"field": "Volume", "value": volume},
                {"field": "Type", "value": "ui player"},
                {"field": "SubmixSlider", "value": "SoundFX"},
            ],
            "sounds": [{"id": sample_id}],
        }
    return len(HUD_UI_AUDIO_EVENTS), 4


def _validate_archer_reuse_requirements(
    profile: Mapping[str, Any], archer_plan: Mapping[str, Any]
) -> list[dict[str, str]]:
    context = _mapping(archer_plan.get("dedupeContext"), "archer dedupeContext")
    if context.get("baseProfileId") != COMPLETE_PROFILE_ID:
        raise ValueError("archer reuse base profile changed")
    resources = {
        _resource_id(item, "completed resource"): item
        for item in _array(profile.get("resources"), "completed resources")
        if isinstance(item, dict)
    }
    validated: list[dict[str, str]] = []
    for raw in _array(context.get("reuseRequirements"), "archer reuse requirements"):
        requirement = _mapping(raw, "archer reuse requirement")
        resource_id = _text(
            requirement.get("existingResourceId"), "archer existing resource id"
        )
        source = _text(
            requirement.get("sourceVirtualPath"), "archer reused source path"
        )
        safe_relative_parts(source)
        resource = resources.get(resource_id)
        if (
            requirement.get("selection") != "reuse-existing-resource-owner"
            or resource is None
            or resource.get("converter") != requirement.get("existingConverter")
            or source.casefold()
            not in {
                value.casefold()
                for value in _patterns(resource, f"archer owner {resource_id!r}")
            }
        ):
            raise ValueError(f"archer exact reuse requirement changed: {source!r}")
        validated.append(
            {"existingResourceId": resource_id, "sourceVirtualPath": source}
        )
    if len(validated) != 3:
        raise ValueError("archer exact reuse closure changed")
    return validated


def _validate_animated_runtime_contract(document: dict[str, Any]) -> str:
    if (
        document.get("schema") != ANIMATED_RUNTIME_SCHEMA
        or document.get("schemaVersion") != 0
    ):
        raise ValueError("unsupported animated-prop runtime contract")
    reproduction = _mapping(
        document.get("reproduction"), "animated runtime reproduction"
    )
    first = _text(reproduction.get("pass1Sha256"), "animated runtime pass1Sha256")
    second = _text(reproduction.get("pass2Sha256"), "animated runtime pass2Sha256")
    calculated = _canonical_sha256(
        {key: value for key, value in document.items() if key != "reproduction"}
    )
    if (
        first != second
        or first != calculated
        or reproduction.get("identical") is not True
    ):
        raise ValueError("animated-prop runtime contract is not reproducible")
    scope = _mapping(document.get("scope"), "animated runtime scope")
    if scope.get("targetTypeCount") != 10 or scope.get("placementCount") != 26:
        raise ValueError("animated-prop runtime scope changed")
    targets = _array(document.get("targets"), "animated runtime targets")
    placements = _array(document.get("placements"), "animated runtime placements")
    if len(targets) != 10 or len(placements) != 26:
        raise ValueError("animated-prop runtime closure changed")
    target_types = [
        _text(item.get("targetObject"), "animated targetObject") for item in targets
    ]
    if len({value.casefold() for value in target_types}) != 10:
        raise ValueError("animated-prop runtime target types are not unique")
    if (
        sum(
            len(_array(item.get("glbActionMap"), "animated target glbActionMap"))
            for item in targets
        )
        != 44
    ):
        raise ValueError("animated-prop runtime action mapping count changed")
    if sorted(target_types, key=str.casefold) != sorted(
        [
            _text(value, "animated scope target type")
            for value in scope.get("targetTypes", [])
        ],
        key=str.casefold,
    ):
        raise ValueError("animated-prop runtime scope target identities changed")
    placement_counts: dict[str, int] = {}
    for placement in placements:
        type_name = _text(placement.get("typeName"), "animated placement typeName")
        placement_counts[type_name] = placement_counts.get(type_name, 0) + 1
    for target in targets:
        type_name = str(target["targetObject"])
        if placement_counts.get(type_name) != target.get("placementCount"):
            raise ValueError(f"animated-prop placement count changed for {type_name!r}")
    return first


def _resource_id(resource: Mapping[str, Any], label: str) -> str:
    return _text(resource.get("id"), f"{label} id")


def _patterns(resource: Mapping[str, Any], label: str) -> list[str]:
    values = [
        _text(value, f"{label} pattern")
        for value in _array(resource.get("patterns"), f"{label} patterns")
    ]
    if len({value.casefold() for value in values}) != len(values):
        raise ValueError(f"{label} has duplicate patterns")
    for value in values:
        safe_relative_parts(value)
    return values


def _rewrite_resource_dependencies(
    resource: dict[str, Any], aliases: Mapping[str, str]
) -> None:
    options = resource.get("options")
    if not isinstance(options, dict) or "inputResourceIds" not in options:
        return
    dependencies = _array(options["inputResourceIds"], "inputResourceIds")
    rewritten = [
        aliases.get(_text(value, "input resource id"), str(value))
        for value in dependencies
    ]
    if len(set(rewritten)) != len(rewritten):
        rewritten = list(dict.fromkeys(rewritten))
    options["inputResourceIds"] = rewritten


def _can_share_source(
    resource: Mapping[str, Any], owners: Sequence[tuple[str, str]]
) -> bool:
    return resource.get("converter") == "sage-particle-definition" and all(
        converter == "sage-particle-definition" for _, converter in owners
    )


def _merge_resources(
    base_resources: Sequence[Mapping[str, Any]],
    additions: Sequence[tuple[str, Sequence[Mapping[str, Any]]]],
) -> tuple[list[dict[str, Any]], dict[str, str], list[dict[str, str]]]:
    resources = [deepcopy(dict(value)) for value in base_resources]
    ids: dict[str, str] = {}
    source_owners: dict[str, list[tuple[str, str]]] = {}
    outputs: dict[str, str] = {}
    for resource in resources:
        resource_id = _resource_id(resource, "base resource")
        folded_id = resource_id.casefold()
        if folded_id in ids:
            raise ValueError(f"duplicate base resource id: {resource_id!r}")
        ids[folded_id] = resource_id
        converter = str(resource.get("converter", "copy"))
        for pattern in _patterns(resource, f"base resource {resource_id!r}"):
            source_owners.setdefault(pattern.casefold(), []).append(
                (resource_id, converter)
            )
        output = resource.get("output")
        if isinstance(output, str):
            outputs.setdefault(output.casefold(), resource_id)

    aliases: dict[str, str] = {}
    reused: list[dict[str, str]] = []
    for batch, raw_resources in additions:
        for raw in raw_resources:
            resource = deepcopy(dict(raw))
            resource_id = _resource_id(resource, f"{batch} resource")
            if resource_id.casefold() in ids:
                raise ValueError(f"{batch} resource id collision: {resource_id!r}")
            patterns = _patterns(resource, f"{batch} resource {resource_id!r}")
            converter = str(resource.get("converter", "copy"))
            owners = [
                owner
                for pattern in patterns
                for owner in source_owners.get(pattern.casefold(), [])
            ]
            if (
                converter == "hash-only"
                and resource.get("output") is None
                and len(patterns) == 1
                and owners
            ):
                owner_ids = {owner_id for owner_id, _ in owners}
                if len(owner_ids) != 1:
                    raise ValueError(
                        f"{batch} evidence leaf has ambiguous owners: {patterns[0]!r}"
                    )
                owner_id = next(iter(owner_ids))
                aliases[resource_id] = owner_id
                reused.append(
                    {
                        "batch": batch,
                        "discardedResourceId": resource_id,
                        "reusedResourceId": owner_id,
                        "sourceVirtualPath": patterns[0],
                    }
                )
                continue
            if owners and not _can_share_source(resource, owners):
                raise ValueError(
                    f"{batch} source already has an owner: {patterns!r} -> {owners!r}"
                )
            output = resource.get("output")
            if isinstance(output, str):
                owner = outputs.get(output.casefold())
                if owner is not None:
                    raise ValueError(
                        f"{batch} output collision: {output!r} already owned by {owner!r}"
                    )
                outputs[output.casefold()] = resource_id
            ids[resource_id.casefold()] = resource_id
            resources.append(resource)
            for pattern in patterns:
                source_owners.setdefault(pattern.casefold(), []).append(
                    (resource_id, converter)
                )

    for resource in resources:
        _rewrite_resource_dependencies(resource, aliases)
    if len(resources) > MAX_RESOURCES:
        raise ValueError(
            f"completed profile exceeds the bounded {MAX_RESOURCES}-resource limit"
        )
    return resources, aliases, reused


def _rewrite_resource_ids(value: Any, aliases: Mapping[str, str]) -> Any:
    if isinstance(value, list):
        return [_rewrite_resource_ids(item, aliases) for item in value]
    if not isinstance(value, dict):
        return value
    rewritten: dict[str, Any] = {}
    for key, item in value.items():
        if key == "modelResourceId" and isinstance(item, str):
            rewritten[key] = aliases.get(item, item)
        elif key.endswith("ResourceIds") and isinstance(item, list):
            rewritten[key] = list(
                dict.fromkeys(
                    aliases.get(str(resource_id), str(resource_id))
                    for resource_id in item
                )
            )
        else:
            rewritten[key] = _rewrite_resource_ids(item, aliases)
    return rewritten


def _neutral_runtime_objects(
    lifecycles: Sequence[Mapping[str, Any]], aliases: Mapping[str, str]
) -> list[dict[str, Any]]:
    objects: list[dict[str, Any]] = []
    seen: set[str] = set()
    for index, raw in enumerate(lifecycles):
        lifecycle = _rewrite_resource_ids(deepcopy(dict(raw)), aliases)
        object_id = _text(
            lifecycle.get("objectId"), f"neutral lifecycle {index} objectId"
        )
        if object_id not in DISPLAY_NAMES or object_id in seen:
            raise ValueError(
                f"unexpected or duplicate neutral lifecycle object: {object_id!r}"
            )
        seen.add(object_id)
        if lifecycle.get("schemaVersion") != 1:
            raise ValueError(f"neutral lifecycle {object_id!r} changed schema version")
        phases = _array(
            lifecycle.get("phases"), f"neutral lifecycle {object_id!r} phases"
        )
        intact = [
            phase
            for phase in phases
            if isinstance(phase, dict) and phase.get("phase") == "intact"
        ]
        if len(intact) != 1:
            raise ValueError(f"neutral lifecycle {object_id!r} lacks one intact phase")
        visual = _mapping(intact[0].get("visual"), "neutral intact visual")
        model = _text(visual.get("glb"), "neutral intact GLB")
        presentation_lifecycle = {
            key: value for key, value in lifecycle.items() if key != "typeName"
        }
        presentation_lifecycle["schema"] = "openbfme.building-lifecycle-presentation"
        objects.append(
            {
                "displayName": DISPLAY_NAMES[object_id],
                "id": object_id,
                "kind": "structure",
                "presentation": {
                    "buildingLifecycle": presentation_lifecycle,
                    "model": model,
                },
                "sourceTypeName": _text(
                    lifecycle.get("typeName"),
                    f"neutral lifecycle {object_id!r} typeName",
                ),
            }
        )
    if seen != set(DISPLAY_NAMES):
        raise ValueError("neutral lifecycle object closure changed")
    return objects


def _map_resource(profile: dict[str, Any]) -> dict[str, Any]:
    matches = [
        resource
        for resource in _array(profile.get("resources"), "profile resources")
        if isinstance(resource, dict) and resource.get("converter") == "sage-map"
    ]
    if len(matches) != 1:
        raise ValueError("base profile must contain one exact sage-map resource")
    return matches[0]


def _append_bindings(
    profile: dict[str, Any],
    no_motion_fragment: Mapping[str, Any],
    neutral_fragment: Mapping[str, Any],
) -> tuple[int, int]:
    map_resource = _map_resource(profile)
    options = _mapping(map_resource.get("options"), "sage-map options")
    bindings = _mapping(options.get("objectBindings"), "sage-map objectBindings")
    models = _array(bindings.get("models"), "sage-map model bindings")
    structures = bindings.setdefault("structures", [])
    if not isinstance(structures, list):
        raise ValueError("sage-map structure bindings must be an array")
    no_motion_bindings = _array(
        _mapping(
            no_motion_fragment.get("objectBindings"), "no-motion objectBindings"
        ).get("models"),
        "no-motion model bindings",
    )
    neutral_bindings = _array(
        _mapping(neutral_fragment.get("objectBindings"), "neutral objectBindings").get(
            "structures"
        ),
        "neutral structure bindings",
    )
    existing_types = {
        str(item.get("typeName")).casefold()
        for item in [*models, *structures]
        if isinstance(item, dict)
    }
    for raw in [*no_motion_bindings, *neutral_bindings]:
        binding = _mapping(raw, "completion object binding")
        type_name = _text(binding.get("typeName"), "completion binding typeName")
        if type_name.casefold() in existing_types:
            raise ValueError(f"completion binding type collision: {type_name!r}")
        existing_types.add(type_name.casefold())
    models.extend(deepcopy(no_motion_bindings))
    structures.extend(deepcopy(neutral_bindings))
    return len(no_motion_bindings), len(neutral_bindings)


def _validate_animated_runtime_handoff(
    profile: dict[str, Any], contract: Mapping[str, Any]
) -> None:
    map_resource = _map_resource(profile)
    bindings = _mapping(
        _mapping(map_resource.get("options"), "sage-map options").get("objectBindings"),
        "sage-map objectBindings",
    )
    model_bindings = {
        _text(item.get("typeName"), "animated map binding typeName"): item
        for item in _array(bindings.get("models"), "sage-map model bindings")
        if isinstance(item, dict)
    }
    resources = {
        _resource_id(item, "completed resource"): item
        for item in _array(profile.get("resources"), "completed resources")
        if isinstance(item, dict)
    }
    for raw in _array(contract.get("targets"), "animated runtime targets"):
        target = _mapping(raw, "animated runtime target")
        type_name = _text(target.get("targetObject"), "animated targetObject")
        binding = model_bindings.get(type_name)
        if binding is None:
            raise ValueError(
                f"animated runtime target lacks an exact map binding: {type_name!r}"
            )
        source_model = _text(
            target.get("sourceVirtualModel"), "animated target sourceVirtualModel"
        )
        output_glb = _text(target.get("outputGlb"), "animated target outputGlb")
        safe_relative_parts(source_model)
        safe_relative_parts(output_glb)
        if (
            binding.get("sourceVirtualModel") != source_model
            or binding.get("glb") != output_glb
            or binding.get("matchMethod") != "exact-type-name"
        ):
            raise ValueError(
                f"animated runtime target disagrees with map binding: {type_name!r}"
            )
        resource_id = _text(target.get("resourceId"), "animated target resourceId")
        resource = resources.get(resource_id)
        if resource is None or resource.get("output") != output_glb:
            raise ValueError(
                f"animated runtime target disagrees with profile resource: {type_name!r}"
            )


def _validate_base_report(
    report: Mapping[str, Any], base_profile_path: Path, base_profile: Mapping[str, Any]
) -> str:
    if (
        report.get("schema") != "openbfme.retail-slice-profile-composition"
        or report.get("schemaVersion") != 0
    ):
        raise ValueError("unsupported base composition report")
    digest = _validate_declared_digest(report, "base composition report")
    profile_sha = _file_sha256(base_profile_path)
    if report.get("profileSha256") != profile_sha:
        raise ValueError("base composition report does not bind the base profile bytes")
    resources = _mapping(report.get("resources"), "base report resources")
    if resources.get("final") != BASE_RESOURCE_COUNT:
        raise ValueError("base composition resource count changed")
    if base_profile.get("id") != BASE_PROFILE_ID:
        raise ValueError("unexpected base profile id")
    return digest


def _validate_profile_document(profile: Mapping[str, Any]) -> ImportProfile:
    # The persisted profile uses the pretty, deterministic writer below.  Test
    # the bytes that ImportProfile.load will actually receive, not a smaller
    # canonical surrogate that can pass while the written file exceeds the
    # hard profile bound.
    encoded = _pretty_json_bytes(profile)
    if len(encoded) > MAX_PROFILE_BYTES:
        raise ValueError("completed profile exceeds the ImportProfile byte limit")
    with tempfile.TemporaryDirectory(prefix="openbfme-fords-completion-") as raw:
        path = Path(raw) / "profile.json"
        path.write_bytes(encoded)
        return ImportProfile.load(path)


def compose_retail_fords_completion_profile(
    base_profile_path: Path | str,
    base_report_path: Path | str,
    no_motion_plan_path: Path | str,
    neutral_plan_path: Path | str,
    particle_plan_path: Path | str,
    ambient_audio_plan_path: Path | str,
    archer_projectile_plan_path: Path | str,
    fords_environment_plan_path: Path | str,
    hud_apt_plan_path: Path | str,
    animated_runtime_contract_path: Path | str,
    men_damage_audio_contract_path: Path | str,
    men_damage_effects_contract_path: Path | str,
    catalog_path: Path | str,
    men_lifecycle_report_path: Path | str = DEFAULT_MEN_LIFECYCLE_REPORT_PATH,
    shell_apt_plan_path: Path | str | None = None,
) -> ComposedRetailFordsCompletion:
    base, base_path = _load_document(base_profile_path, "base full profile")
    base_report, base_report_source = _load_document(
        base_report_path, "base composition report"
    )
    base_report_digest = _validate_base_report(base_report, base_path, base)
    if len(_array(base.get("resources"), "base resources")) != BASE_RESOURCE_COUNT:
        raise ValueError("base profile resource count changed")
    pack = _mapping(base.get("pack"), "base pack")
    if pack.get("id") != PACK_ID:
        raise ValueError("base pack id changed")

    no_motion, no_motion_source = _load_document(no_motion_plan_path, "no-motion plan")
    neutral, neutral_source = _load_document(neutral_plan_path, "neutral plan")
    particle, particle_source = _load_document(particle_plan_path, "particle plan")
    ambient_audio, ambient_audio_source = _load_document(
        ambient_audio_plan_path, "ambient-audio plan"
    )
    archer_projectile, archer_projectile_source = _load_document(
        archer_projectile_plan_path, "archer-projectile plan"
    )
    fords_environment, fords_environment_source = _load_document(
        fords_environment_plan_path, "Fords environment plan"
    )
    hud_apt, hud_apt_source = _load_document(hud_apt_plan_path, "HUD APT plan")
    # The main-menu shell APT closure is an optional, additive ingress lane: it
    # rides the same transactional pack build as the palantir HUD bundle, and a
    # composition that does not pass a shell plan produces exactly the profile
    # it produced before the lane existed.
    shell_apt: dict[str, Any] | None = None
    shell_apt_source: Path | None = None
    shell_apt_resources: list[Any] = []
    if shell_apt_plan_path is not None:
        shell_apt, shell_apt_source = _load_document(
            shell_apt_plan_path, "shell APT plan"
        )
        if shell_apt.get("schema") != SHELL_APT_SCHEMA:
            raise ValueError("shell APT plan schema changed")
        _validate_declared_digest(shell_apt, "shell APT plan")
        shell_fragment = _mapping(
            shell_apt.get("profileFragment"), "shell APT profileFragment"
        )
        shell_apt_resources = _array(
            shell_fragment.get("resources"), "shell APT resources"
        )
        if [
            _resource_id(resource, "shell APT resource")
            for resource in shell_apt_resources
        ] != [SHELL_APT_RESOURCE_ID]:
            raise ValueError("shell APT profile fragment resource identities changed")
        shell_binding = _mapping(shell_apt.get("packBinding"), "shell APT packBinding")
        if (
            shell_binding.get("packFileKey") != SHELL_APT_PACK_KEY
            or shell_binding.get("runtimePath") != SHELL_APT_RUNTIME_PATH
        ):
            raise ValueError("shell APT pack binding changed")
    animated_runtime, animated_runtime_source = _load_document(
        animated_runtime_contract_path, "animated-prop runtime contract"
    )
    men_damage_audio, men_damage_audio_source = _load_document(
        men_damage_audio_contract_path, "Men damage-audio contract"
    )
    men_damage_effects, men_damage_effects_source = _load_document(
        men_damage_effects_contract_path, "Men damage-effects contract"
    )
    men_lifecycle_report, men_lifecycle_report_source = _load_document(
        men_lifecycle_report_path, "Men building lifecycle report"
    )
    animated_runtime_digest = _validate_animated_runtime_contract(animated_runtime)
    men_damage_audio_fragment = _validate_men_damage_audio_contract(men_damage_audio)
    men_damage_effects_fragment = _validate_men_damage_effects_contract(
        men_damage_effects
    )
    no_motion_fragment = _validate_plan(
        no_motion,
        schema=NO_MOTION_SCHEMA,
        fragment_keys={"resources", "objectBindings"},
    )
    neutral_fragment = _validate_plan(
        neutral,
        schema=NEUTRAL_SCHEMA,
        fragment_keys={"resources", "objectBindings", "runtimeDataMerge"},
    )
    particle_fragment = _validate_plan(
        particle,
        schema=PARTICLE_SCHEMA,
        fragment_keys={"resources", "runtimeData", "runtimeDataPath"},
    )
    ambient_audio_fragment = _validate_plan(
        ambient_audio,
        schema=AMBIENT_AUDIO_SCHEMA,
        fragment_keys={"resources", "runtimeDataMerge"},
    )
    archer_projectile_fragment = _validate_plan(
        archer_projectile,
        schema=ARCHER_PROJECTILE_SCHEMA,
        fragment_keys={"resources", "runtimeData", "runtimeDataPath"},
        resource_count_key="profileFragmentResourceCount",
    )
    fords_environment_fragment = _validate_plan(
        fords_environment,
        schema=FORDS_ENVIRONMENT_SCHEMA,
        fragment_keys={"resources", "runtimeDataMerge"},
    )
    hud_apt_fragment = _validate_plan(
        hud_apt,
        schema=HUD_APT_SCHEMA,
        fragment_keys={"resources"},
    )
    hud_resources = _array(hud_apt_fragment.get("resources"), "HUD APT resources")
    if len(hud_resources) != 2:
        raise ValueError("HUD APT resource count changed")
    hud_bundle = _mapping(hud_resources[0], "HUD APT runtime bundle")
    hud_bundle_options = _mapping(
        hud_bundle.get("options"), "HUD APT runtime bundle options"
    )
    if (
        hud_bundle.get("converter") != "sage-apt-runtime"
        or hud_bundle.get("output") != HUD_APT_RUNTIME_PATH
        or hud_bundle.get("expected_count") != 261
        or hud_bundle.get("limit") != 261
        or hud_bundle_options
        != {
            "expectedSourceAggregateSha256": (
                "f62347fb78065726715618ed9c73f152c678fec5646ddf7b0855825d1cb23599"
            )
        }
    ):
        raise ValueError("HUD APT runtime bundle contract changed")
    hud_font = _mapping(hud_resources[1], "HUD Albertus MT font")
    if hud_font != {
        "converter": "copy",
        "expected_count": 1,
        "id": "men-hud-font-albertus-mt",
        "kind": "ui",
        "limit": 1,
        "output": "assets/ui/palantir/fonts/albertusmt-6a1990e17f14.otf",
        "patterns": ["albertusmt.otf"],
        "required": True,
    }:
        raise ValueError("HUD Albertus MT font resource contract changed")
    hud_scene_contract = _mapping(hud_apt.get("sceneContract"), "HUD APT sceneContract")
    hud_scene_digest = _validate_declared_digest(
        hud_scene_contract, "HUD APT sceneContract"
    )
    legacy_hud_replacement = _mapping(
        hud_apt.get("legacyAssumptionReplacement"),
        "HUD APT legacyAssumptionReplacement",
    )
    if (
        legacy_hud_replacement.get("rejectedMappedImageId") != "SGCommandBar"
        or legacy_hud_replacement.get("requiredPackFileKey") != HUD_APT_PACK_KEY
        or legacy_hud_replacement.get("requiredSceneId") != "bfme2.ui.palantir"
        or legacy_hud_replacement.get("atomicPrivateBinding") is not True
    ):
        raise ValueError("HUD APT replacement contract changed")
    hud_runtime_bindings = _mapping(
        hud_scene_contract.get("runtimeAssetBindings"),
        "HUD APT runtimeAssetBindings",
    )
    if hud_runtime_bindings.get("externalFonts") != [
        {
            "cookedFont": "assets/ui/palantir/fonts/albertusmt-6a1990e17f14.otf",
            "fontId": "palantir:63",
            "fontName": "Albertus MT",
            "resourceId": "men-hud-font-albertus-mt",
            "sourceSha256": (
                "6a1990e17f14ce5be199dde10f56dac3efd66aaa8e91d46119952cf55a9d9ba0"
            ),
            "sourceVirtualPath": "albertusmt.otf",
        }
    ]:
        raise ValueError("HUD Albertus MT runtime binding changed")
    # The planner seals the font in the scene contract and as its own copy
    # resource. The production converter option is composed here so the pinned
    # APT converter receives the same exact external-font binding atomically.
    hud_bundle_options["externalFonts"] = deepcopy(
        hud_runtime_bindings["externalFonts"]
    )

    completed = deepcopy(base)
    completed["id"] = COMPLETE_PROFILE_ID
    completed["title"] = "BFME2 Men/Fords exact retail completion"
    resources, aliases, reused = _merge_resources(
        _array(base.get("resources"), "base resources"),
        [
            (
                "no-motion",
                _array(no_motion_fragment["resources"], "no-motion resources"),
            ),
            ("neutral", _array(neutral_fragment["resources"], "neutral resources")),
            ("particle", _array(particle_fragment["resources"], "particle resources")),
            (
                "ambient-audio",
                _array(ambient_audio_fragment["resources"], "ambient-audio resources"),
            ),
            (
                "men-damage-audio",
                _array(
                    men_damage_audio_fragment["resources"],
                    "Men damage-audio resources",
                ),
            ),
            (
                "men-damage-effects",
                _array(
                    men_damage_effects_fragment["resources"],
                    "Men damage-effects resources",
                ),
            ),
            (
                "archer-projectile",
                _array(
                    archer_projectile_fragment["resources"],
                    "archer-projectile resources",
                ),
            ),
            (
                "fords-environment",
                _array(
                    fords_environment_fragment["resources"],
                    "Fords environment resources",
                ),
            ),
            ("hud-apt", _array(hud_apt_fragment["resources"], "HUD APT resources")),
            ("shell-apt", deepcopy(shell_apt_resources)),
            ("hud-ui-audio", [deepcopy(HUD_UI_AUDIO_RESOURCE)]),
            ("men-selection", deepcopy(MEN_SELECTION_RESOURCES)),
            ("men-order-hint", deepcopy(MEN_ORDER_HINT_RESOURCES)),
        ],
    )
    completed["resources"] = resources
    archer_reuse = _validate_archer_reuse_requirements(completed, archer_projectile)
    no_motion_binding_count, neutral_binding_count = _append_bindings(
        completed, no_motion_fragment, neutral_fragment
    )
    _validate_animated_runtime_handoff(completed, animated_runtime)
    men_lifecycle_upgrade = upgrade_men_building_lifecycles(
        completed, men_lifecycle_report
    )
    completed = men_lifecycle_upgrade.profile

    runtime_data = _mapping(completed.get("runtime_data"), "base runtime_data")
    ambient_runtime_merge = _mapping(
        ambient_audio_fragment.get("runtimeDataMerge"),
        "ambient runtimeDataMerge",
    )
    if set(ambient_runtime_merge) != {"data/audio_events.json"}:
        raise ValueError("ambient runtime merge target changed")
    ambient_registry = _mapping(
        ambient_audio.get("runtimeAudioRegistryAddition"),
        "ambient runtimeAudioRegistryAddition",
    )
    if ambient_runtime_merge["data/audio_events.json"] != ambient_registry:
        raise ValueError("ambient runtime registry handoffs disagree")
    base_audio_registry = _mapping(
        runtime_data.get("data/audio_events.json"), "base audio registry"
    )
    neutral_runtime_merge = _mapping(
        neutral_fragment.get("runtimeDataMerge"),
        "neutral runtimeDataMerge",
    )
    if set(neutral_runtime_merge) != {"data/audio_events.json"}:
        raise ValueError("neutral runtime merge target changed")
    neutral_registry = _mapping(
        neutral.get("runtimeAudioRegistryAddition"),
        "neutral runtimeAudioRegistryAddition",
    )
    if neutral_runtime_merge["data/audio_events.json"] != neutral_registry:
        raise ValueError("neutral runtime registry handoffs disagree")
    neutral_audio_event_count, neutral_audio_sample_count = _merge_audio_registry(
        base_audio_registry,
        neutral_registry,
        expected_root_count=4,
        label="neutral",
    )
    ambient_event_count, ambient_sample_count = _merge_audio_registry(
        base_audio_registry, ambient_registry
    )
    (
        men_damage_audio_event_count,
        men_damage_audio_existing_sample_count,
        men_damage_audio_added_sample_count,
    ) = _merge_men_damage_audio_registry(base_audio_registry, men_damage_audio)
    hud_ui_audio_event_count, hud_ui_audio_sample_count = (
        _merge_hud_ui_audio_registry(base_audio_registry)
    )
    objects_document = _mapping(
        runtime_data.get("data/objects.json"), "base objects runtime document"
    )
    objects = _array(objects_document.get("objects"), "base runtime objects")
    object_ids = {
        str(item.get("id")).casefold() for item in objects if isinstance(item, dict)
    }
    neutral_objects = _neutral_runtime_objects(
        _array(neutral.get("structureLifecycles"), "neutral structureLifecycles"),
        aliases,
    )
    for item in neutral_objects:
        if item["id"].casefold() in object_ids:
            raise ValueError(f"neutral runtime object id collision: {item['id']!r}")
        object_ids.add(item["id"].casefold())
        objects.append(item)

    particle_runtime_path = _text(
        particle_fragment.get("runtimeDataPath"), "particle runtimeDataPath"
    )
    safe_relative_parts(particle_runtime_path)
    if particle_runtime_path in runtime_data:
        raise ValueError("particle runtime path collides with existing runtime data")
    particle_runtime = _rewrite_resource_ids(
        deepcopy(
            _mapping(particle_fragment.get("runtimeData"), "particle runtimeData")
        ),
        aliases,
    )
    (
        men_damage_effect_definition_count,
        men_damage_effect_fx_list_count,
        men_damage_effect_new_unresolved_count,
    ) = _merge_men_damage_effects_runtime(
        particle_runtime, men_damage_effects, resources
    )
    runtime_data[particle_runtime_path] = particle_runtime
    files = _mapping(
        _mapping(completed.get("pack"), "completed pack").get("files"), "pack files"
    )
    if "fordsParticleBindings" in files:
        raise ValueError("pack already declares fordsParticleBindings")
    files["fordsParticleBindings"] = particle_runtime_path
    if ANIMATED_RUNTIME_PATH in runtime_data or "fordsAnimatedProps" in files:
        raise ValueError(
            "animated-prop runtime path collides with existing runtime data"
        )
    runtime_data[ANIMATED_RUNTIME_PATH] = deepcopy(animated_runtime)
    files["fordsAnimatedProps"] = ANIMATED_RUNTIME_PATH
    archer_runtime_path = _text(
        archer_projectile_fragment.get("runtimeDataPath"),
        "archer runtimeDataPath",
    )
    safe_relative_parts(archer_runtime_path)
    if archer_runtime_path in runtime_data or ARCHER_PROJECTILE_PACK_KEY in files:
        raise ValueError("archer projectile runtime path collides with existing data")
    archer_runtime = deepcopy(
        _mapping(
            archer_projectile_fragment.get("runtimeData"),
            "archer runtimeData",
        )
    )
    unresolved_archer = _array(
        archer_runtime.get("unresolvedEngineSemantics"),
        "archer unresolvedEngineSemantics",
    )
    if len(unresolved_archer) != 5:
        raise ValueError("archer unresolved engine semantic count changed")
    runtime_data[archer_runtime_path] = archer_runtime
    files[ARCHER_PROJECTILE_PACK_KEY] = archer_runtime_path
    environment_runtime_merge = _mapping(
        fords_environment_fragment.get("runtimeDataMerge"),
        "Fords environment runtimeDataMerge",
    )
    if set(environment_runtime_merge) != {FORDS_ENVIRONMENT_RUNTIME_PATH}:
        raise ValueError("Fords environment runtime merge target changed")
    environment_runtime = _mapping(
        environment_runtime_merge[FORDS_ENVIRONMENT_RUNTIME_PATH],
        "Fords environment runtime document",
    )
    if (
        environment_runtime.get("schema") != "openbfme.fords-environment"
        or environment_runtime.get("schemaVersion") != 0
        or environment_runtime.get("complete") is not False
        or len(_array(environment_runtime.get("blockers"), "environment blockers")) != 4
    ):
        raise ValueError("Fords environment runtime contract changed")
    if (
        FORDS_ENVIRONMENT_RUNTIME_PATH in runtime_data
        or FORDS_ENVIRONMENT_PACK_KEY in files
    ):
        raise ValueError("Fords environment runtime path collides with existing data")
    runtime_data[FORDS_ENVIRONMENT_RUNTIME_PATH] = deepcopy(environment_runtime)
    files[FORDS_ENVIRONMENT_PACK_KEY] = FORDS_ENVIRONMENT_RUNTIME_PATH
    if HUD_APT_RUNTIME_PATH in runtime_data or HUD_APT_PACK_KEY in files:
        raise ValueError("HUD APT runtime path collides with existing data")
    # The structural planner scene is source evidence, not the cooked runtime
    # contract. The single production bundle resource creates this path from
    # the exact 261-source closure during the transactional pack build.
    files[HUD_APT_PACK_KEY] = HUD_APT_RUNTIME_PATH
    if shell_apt is not None:
        if SHELL_APT_RUNTIME_PATH in runtime_data or SHELL_APT_PACK_KEY in files:
            raise ValueError("shell APT runtime path collides with existing data")
        # As with the palantir bundle, the shell path is created by the single
        # production bundle resource during the transactional pack build.
        files[SHELL_APT_PACK_KEY] = SHELL_APT_RUNTIME_PATH
    if MEN_SELECTION_RUNTIME_PATH in runtime_data or MEN_SELECTION_PACK_KEY in files:
        raise ValueError("Men selection decal runtime path collides with existing data")
    runtime_data[MEN_SELECTION_RUNTIME_PATH] = deepcopy(MEN_SELECTION_RUNTIME)
    files[MEN_SELECTION_PACK_KEY] = MEN_SELECTION_RUNTIME_PATH
    if MEN_ORDER_HINT_RUNTIME_PATH in runtime_data or MEN_ORDER_HINT_PACK_KEY in files:
        raise ValueError("Men order-hint runtime path collides with existing data")
    runtime_data[MEN_ORDER_HINT_RUNTIME_PATH] = deepcopy(MEN_ORDER_HINT_RUNTIME)
    files[MEN_ORDER_HINT_PACK_KEY] = MEN_ORDER_HINT_RUNTIME_PATH
    completed["pack"]["vertical_slice_complete"] = False

    authoritative = _validate_profile_document(completed)
    catalog = InstallCatalog.load(Path(catalog_path).expanduser().resolve())
    stale = catalog.stale_reasons()
    if stale:
        raise ValueError("retail catalog is stale: " + "; ".join(stale))
    resolved = resolve_profile(authoritative, catalog)
    if resolved.missing_required:
        raise ValueError(
            "completed profile has missing required resources: "
            + ", ".join(resolved.missing_required)
        )
    selected_with_repetition = sum(
        len(resource.entries) for resource in resolved.resources
    )
    planned_resource_count = sum(
        len(_array(fragment["resources"], "completion fragment resources"))
        for fragment in [
            no_motion_fragment,
            neutral_fragment,
            particle_fragment,
            ambient_audio_fragment,
            men_damage_audio_fragment,
            men_damage_effects_fragment,
            archer_projectile_fragment,
            fords_environment_fragment,
            hud_apt_fragment,
            {"resources": shell_apt_resources},
            {"resources": MEN_SELECTION_RESOURCES},
            {"resources": MEN_ORDER_HINT_RESOURCES},
        ]
    )
    neutral_blockers: list[dict[str, Any]] = []
    for raw_blocker in _array(neutral.get("blockers"), "neutral blockers"):
        if not isinstance(raw_blocker, dict):
            continue
        code = raw_blocker.get("code")
        if code == "neutral-lifecycle-completion-composer-handoff-pending":
            continue
        blocker = deepcopy(raw_blocker)
        if code == "exact-particle-plan-cross-selection-pending":
            blocker = {
                "code": "exact-particle-cross-family-selection-unresolved",
                "scope": (
                    "all exact definitions are attached, but ten Men damage duplicate "
                    "identifiers "
                    "cannot be selected until the BFME2 cross-family rule is proven"
                ),
            }
        neutral_blockers.append(blocker)
    profile_sha = hashlib.sha256(_pretty_json_bytes(completed)).hexdigest()
    report: dict[str, Any] = {
        "schema": COMPLETION_REPORT_SCHEMA,
        "schemaVersion": COMPLETION_REPORT_SCHEMA_VERSION,
        "profileId": COMPLETE_PROFILE_ID,
        "packId": PACK_ID,
        "profileSha256": profile_sha,
        "inputs": {
            "baseProfileSha256": _file_sha256(base_path),
            "baseCompositionReportSha256": _file_sha256(base_report_source),
            "baseCompositionReportAggregateSha256": base_report_digest,
            "noMotionPlanSha256": _file_sha256(no_motion_source),
            "noMotionPlanAggregateSha256": no_motion["aggregateSha256"],
            "neutralPlanSha256": _file_sha256(neutral_source),
            "neutralPlanAggregateSha256": neutral["aggregateSha256"],
            "particlePlanSha256": _file_sha256(particle_source),
            "particlePlanAggregateSha256": particle["aggregateSha256"],
            "ambientAudioPlanSha256": _file_sha256(ambient_audio_source),
            "ambientAudioPlanAggregateSha256": ambient_audio["aggregateSha256"],
            "archerProjectilePlanSha256": _file_sha256(archer_projectile_source),
            "archerProjectilePlanAggregateSha256": archer_projectile["aggregateSha256"],
            "fordsEnvironmentPlanSha256": _file_sha256(fords_environment_source),
            "fordsEnvironmentPlanAggregateSha256": fords_environment["aggregateSha256"],
            "hudAptPlanSha256": _file_sha256(hud_apt_source),
            "hudAptPlanAggregateSha256": hud_apt["aggregateSha256"],
            "hudAptSceneAggregateSha256": hud_scene_digest,
            **(
                {}
                if shell_apt is None or shell_apt_source is None
                else {
                    "shellAptPlanSha256": _file_sha256(shell_apt_source),
                    "shellAptPlanAggregateSha256": shell_apt["aggregateSha256"],
                }
            ),
            "animatedRuntimeContractSha256": _file_sha256(animated_runtime_source),
            "animatedRuntimeContractAggregateSha256": animated_runtime_digest,
            "menDamageAudioContractSha256": _file_sha256(men_damage_audio_source),
            "menDamageAudioContractAggregateSha256": men_damage_audio[
                "aggregateSha256"
            ],
            "menDamageEffectsContractSha256": _file_sha256(men_damage_effects_source),
            "menDamageEffectsContractAggregateSha256": men_damage_effects[
                "aggregateSha256"
            ],
            "menLifecycleReportSha256": _file_sha256(men_lifecycle_report_source),
            "menLifecycleReportAggregateSha256": (
                men_lifecycle_upgrade.report_aggregate_sha256
            ),
        },
        "resources": {
            "base": BASE_RESOURCE_COUNT,
            "planned": planned_resource_count,
            "reused": len(reused),
            "added": len(resources) - BASE_RESOURCE_COUNT,
            "final": len(resources),
            "maximum": MAX_RESOURCES,
            "reuseAliases": reused,
        },
        "resolution": {
            "resourceCount": len(resolved.resources),
            "selectedEntryReferenceCount": selected_with_repetition,
            "selectedUniqueFileCount": len(resolved.selected_entries),
            "intentionalRepeatedProjectionCount": (
                selected_with_repetition - len(resolved.selected_entries)
            ),
            "missingRequiredCount": 0,
        },
        "runtime": {
            "noMotionBindingCount": no_motion_binding_count,
            "neutralStructureBindingCount": neutral_binding_count,
            "neutralLifecycleObjectCount": len(neutral_objects),
            "neutralAudioEventCount": neutral_audio_event_count,
            "neutralAudioSampleCount": neutral_audio_sample_count,
            "particleRuntimePath": particle_runtime_path,
            "animatedPropRuntimePath": ANIMATED_RUNTIME_PATH,
            "animatedPropTargetCount": 10,
            "animatedPropPlacementCount": 26,
            "animatedPropActionMappingCount": 44,
            "animatedPropMapBindingHandoffValidated": True,
            "ambientAudioEventCount": ambient_event_count,
            "ambientAudioSampleCount": ambient_sample_count,
            "ambientAudioMapPlacementCount": 50,
            "menDamageAudioEventCount": men_damage_audio_event_count,
            "menDamageAudioExistingSampleCount": (
                men_damage_audio_existing_sample_count
            ),
            "menDamageAudioAddedSampleCount": men_damage_audio_added_sample_count,
            "hudUiAudioEventCount": hud_ui_audio_event_count,
            "hudUiAudioSampleCount": hud_ui_audio_sample_count,
            "menDamageAudioSourceReferenceCount": 79,
            "menDamageEffectDefinitionRegistryAddedCount": (
                men_damage_effect_definition_count
            ),
            "menDamageEffectFxListAddedCount": men_damage_effect_fx_list_count,
            "menDamageEffectNewUnresolvedFamilyCount": (
                men_damage_effect_new_unresolved_count
            ),
            "menDamageEffectParticleSystemCount": 21,
            "menDamageEffectDefinitionCandidateCount": 31,
            "menDamageEffectTextureLeafCount": 13,
            "menDamageEffectW3dLeafCount": 0,
            "archerProjectileRuntimePath": archer_runtime_path,
            "archerProjectileExactReuse": archer_reuse,
            "fordsEnvironmentRuntimePath": FORDS_ENVIRONMENT_RUNTIME_PATH,
            "fordsEnvironmentBlockerCount": 4,
            "fordsWorldSkySelectionResolved": False,
            "hudAptRuntimePath": HUD_APT_RUNTIME_PATH,
            "hudAptSceneId": hud_scene_contract.get("sceneId"),
            "hudAptAtlasCount": 24,
            "hudAptGeometryFileCount": 209,
            "hudAptWndWindowCount": 87,
            "hudAptSourceCount": 261,
            "hudAptProfileSelectedSourceCount": 262,
            "hudAptFontResourceCount": 1,
            "hudAptExpectedRuntimeDrawCount": 26,
            "hudAptExpectedRuntimeBlockerCount": 131,
            "hudAptParityReady": False,
            "menSelectionRuntimePath": MEN_SELECTION_RUNTIME_PATH,
            "menSelectionTextureCount": 2,
            "menSelectionStyle": "SHADOW_MERGE_DECAL",
            "menLifecycleSchemaVersion": 1,
            "menLifecycleStructureCount": men_lifecycle_upgrade.structure_count,
            "menLifecyclePhaseCount": men_lifecycle_upgrade.phase_count,
            "menLifecycleNoRenderPhaseCount": (
                men_lifecycle_upgrade.no_render_phase_count
            ),
            "menLifecycleParticleAttachmentCount": (
                men_lifecycle_upgrade.particle_attachment_count
            ),
            "menLifecycleEnteringStateFxBindingCount": (
                men_lifecycle_upgrade.entering_state_fx_binding_count
            ),
            "menLifecycleAudioBindingCount": (
                men_lifecycle_upgrade.audio_binding_count
            ),
            "menLifecycleBlockerCount": men_lifecycle_upgrade.blocker_count,
            "menLifecycleAggregateSha256": (men_lifecycle_upgrade.aggregate_sha256),
            "particleFamilySelectionStatus": _mapping(
                particle_runtime.get("familyResolution"), "particle familyResolution"
            ).get("status"),
        },
        "remainingBlockers": {
            "menSelectionDecal": [
                {
                    "code": "selection-decal-color-throb-and-pixel-merge-oracle-pending",
                    "scope": (
                        "both source textures, merge style, opacity bounds, radii, and "
                        "selection limit are bound; exact renderer color, opacity timing, "
                        "and pixel merge equivalence require original-render approval"
                    ),
                }
            ],
            "neutral": neutral_blockers,
            "particleFamilyResolution": deepcopy(
                _mapping(
                    particle_runtime.get("familyResolution"),
                    "particle familyResolution",
                )
            ),
            "archerProjectile": deepcopy(unresolved_archer),
            "ambientAudio": [
                {
                    "code": "sage-ambient-runtime-semantics-unresolved",
                    "scope": (
                        "attack/decay and delayed loop scheduling, attenuation, "
                        "priority/limit, pitch/volume variance, and ambient-stream "
                        "mix semantics"
                    ),
                }
            ],
            "menBuildingDamageAudio": {
                "code": "sage-men-building-damage-audio-runtime-semantics-unresolved",
                "sourceClosureComplete": True,
                "eventCount": men_damage_audio_event_count,
                "sampleCount": (
                    men_damage_audio_existing_sample_count
                    + men_damage_audio_added_sample_count
                ),
                "unresolved": deepcopy(
                    _array(
                        _mapping(
                            men_damage_audio.get("mixerSemanticBoundary"),
                            "Men damage-audio mixerSemanticBoundary",
                        ).get("unresolved"),
                        "Men damage-audio unresolved mixer semantics",
                    )
                ),
            },
            "menBuildingDamageEffects": deepcopy(
                _array(
                    men_damage_effects.get("blockers"),
                    "Men damage-effects blockers",
                )
            ),
            "fordsEnvironment": deepcopy(
                _array(environment_runtime.get("blockers"), "environment blockers")
            ),
            "hudApt": {
                "code": "hud-apt-runtime-parity-pending",
                "runtimeOutput": HUD_APT_RUNTIME_PATH,
                "expectedDrawCount": 26,
                "expectedBlockerCount": 131,
                "parityReady": False,
                "plannerUnsupportedSemantics": deepcopy(
                    _mapping(
                        hud_scene_contract.get("unsupportedSemantics"),
                        "HUD APT unsupportedSemantics",
                    )
                ),
            },
            "menBuildingLifecycle": {
                "code": "men-building-lifecycle-runtime-and-render-proof-pending",
                "schemaVersion": 1,
                "structureCount": men_lifecycle_upgrade.structure_count,
                "phaseCount": men_lifecycle_upgrade.phase_count,
                "blockerCount": men_lifecycle_upgrade.blocker_count,
                "aggregateSha256": men_lifecycle_upgrade.aggregate_sha256,
            },
        },
        "resolvedHandoffs": [
            {
                "code": "neutral-lifecycle-completion-composer-handoff-complete",
                "evidence": {
                    "runtimeObjectCount": len(neutral_objects),
                    "structureBindingCount": neutral_binding_count,
                },
            },
            {
                "code": "exact-men-building-lifecycle-v1-attached",
                "evidence": {
                    "structureCount": men_lifecycle_upgrade.structure_count,
                    "phaseCount": men_lifecycle_upgrade.phase_count,
                    "noRenderPhaseCount": (men_lifecycle_upgrade.no_render_phase_count),
                    "aggregateSha256": men_lifecycle_upgrade.aggregate_sha256,
                },
            },
            {
                "code": "exact-particle-definitions-attached",
                "evidence": {
                    "runtimePath": particle_runtime_path,
                    "provisionalSelectionCount": 1,
                    "unresolvedCrossFamilySelectionCount": 10,
                },
            },
            {
                "code": "exact-men-building-damage-effect-definitions-attached",
                "evidence": {
                    "contractAggregateSha256": men_damage_effects["aggregateSha256"],
                    "definitionRegistryAddedCount": (
                        men_damage_effect_definition_count
                    ),
                    "fxListAddedCount": men_damage_effect_fx_list_count,
                    "resourceAddedCount": 24,
                    "textureLeafCount": 13,
                    "w3dLeafCount": 0,
                    "unresolvedCrossFamilySelectionCount": 10,
                },
            },
            {
                "code": "exact-fords-ambient-audio-attached",
                "evidence": {
                    "eventCount": ambient_event_count,
                    "sampleCount": ambient_sample_count,
                    "mapPlacementCount": 50,
                },
            },
            {
                "code": "exact-men-building-damage-audio-leaves-attached",
                "evidence": {
                    "eventCount": men_damage_audio_event_count,
                    "existingSampleCount": men_damage_audio_existing_sample_count,
                    "addedSampleCount": men_damage_audio_added_sample_count,
                    "sourceReferenceCount": 79,
                    "contractAggregateSha256": men_damage_audio["aggregateSha256"],
                },
            },
            {
                "code": "exact-gondor-archer-projectile-attached",
                "evidence": {
                    "runtimePath": archer_runtime_path,
                    "exactReuseCount": len(archer_reuse),
                    "unresolvedEngineSemanticCount": len(unresolved_archer),
                },
            },
            {
                "code": "exact-fords-environment-contract-attached",
                "evidence": {
                    "runtimePath": FORDS_ENVIRONMENT_RUNTIME_PATH,
                    "sourceResourceCount": 3,
                    "worldSkySelectionResolved": False,
                    "blockerCount": 4,
                },
            },
            {
                "code": "exact-men-hud-apt-runtime-bundle-attached",
                "evidence": {
                    "runtimePath": HUD_APT_RUNTIME_PATH,
                    "sourceCount": 261,
                    "runtimeOutputCount": 25,
                    "atlasCount": 24,
                    "geometryFileCount": 209,
                    "wndWindowCount": 87,
                    "externalMovieLoadCount": 5,
                    "externalMovieTargetAttachmentBlockerCount": 4,
                },
            },
            {
                "code": "exact-men-hud-albertus-mt-font-attached",
                "evidence": {
                    "resourceId": "men-hud-font-albertus-mt",
                    "sourceVirtualPath": "albertusmt.otf",
                    "sourceSha256": (
                        "6a1990e17f14ce5be199dde10f56dac3efd66aaa8e91d46119952cf55a9d9ba0"
                    ),
                    "output": (
                        "assets/ui/palantir/fonts/albertusmt-6a1990e17f14.otf"
                    ),
                },
            },
            {
                "code": "exact-men-selection-decal-source-contract-attached",
                "evidence": {
                    "runtimePath": MEN_SELECTION_RUNTIME_PATH,
                    "experienceLevel": "MenGenericLevel1",
                    "style": "SHADOW_MERGE_DECAL",
                    "textureCount": 2,
                    "minRadiusSource": 50,
                    "maxRadiusSource": 200,
                    "maxSelectedUnits": 40,
                },
            },
        ],
    }
    report["aggregateSha256"] = _canonical_sha256(report)
    return ComposedRetailFordsCompletion(completed, report)


def write_composed_retail_fords_completion(
    composed: ComposedRetailFordsCompletion,
    profile_path: Path | str,
    report_path: Path | str,
) -> None:
    profile = Path(profile_path)
    report = Path(report_path)
    write_json_atomic(profile, composed.profile)
    if _file_sha256(profile) != composed.report["profileSha256"]:
        raise RuntimeError("written completion profile digest changed")
    write_json_atomic(report, composed.report)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-profile", required=True, type=Path)
    parser.add_argument("--base-report", required=True, type=Path)
    parser.add_argument("--no-motion-plan", required=True, type=Path)
    parser.add_argument("--neutral-plan", required=True, type=Path)
    parser.add_argument("--particle-plan", required=True, type=Path)
    parser.add_argument("--ambient-audio-plan", required=True, type=Path)
    parser.add_argument("--archer-projectile-plan", required=True, type=Path)
    parser.add_argument("--fords-environment-plan", required=True, type=Path)
    parser.add_argument("--hud-apt-plan", required=True, type=Path)
    parser.add_argument("--animated-runtime-contract", required=True, type=Path)
    parser.add_argument("--men-damage-audio-contract", required=True, type=Path)
    parser.add_argument("--men-damage-effects-contract", required=True, type=Path)
    parser.add_argument(
        "--men-lifecycle-report",
        type=Path,
        default=DEFAULT_MEN_LIFECYCLE_REPORT_PATH,
    )
    parser.add_argument(
        "--shell-apt-plan",
        type=Path,
        default=None,
        help=(
            "optional retail main-menu shell APT plan; when supplied the "
            "shell bundle resource and the shellScene pack key are attached"
        ),
    )
    parser.add_argument("--catalog", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    composed = compose_retail_fords_completion_profile(
        args.base_profile,
        args.base_report,
        args.no_motion_plan,
        args.neutral_plan,
        args.particle_plan,
        args.ambient_audio_plan,
        args.archer_projectile_plan,
        args.fords_environment_plan,
        args.hud_apt_plan,
        args.animated_runtime_contract,
        args.men_damage_audio_contract,
        args.men_damage_effects_contract,
        args.catalog,
        args.men_lifecycle_report,
        args.shell_apt_plan,
    )
    write_composed_retail_fords_completion(composed, args.output, args.report)
    print(
        json.dumps(
            {
                "profileId": composed.profile["id"],
                "profileSha256": composed.report["profileSha256"],
                "resourceCount": composed.report["resources"]["final"],
                "selectedUniqueFileCount": composed.report["resolution"][
                    "selectedUniqueFileCount"
                ],
                "remainingBlockerGroups": len(composed.report["remainingBlockers"]),
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


__all__ = [
    "ComposedRetailFordsCompletion",
    "compose_retail_fords_completion_profile",
    "write_composed_retail_fords_completion",
]
