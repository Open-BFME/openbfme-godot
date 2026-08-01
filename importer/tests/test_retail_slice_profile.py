from __future__ import annotations

from copy import deepcopy
import fnmatch
import hashlib
import json
import tempfile
from pathlib import Path
import unittest

from openbfme_importer.catalog import CatalogEntry, InstallCatalog
from openbfme_importer.profile import ImportProfile
from openbfme_importer import retail_slice_profile as composer
from openbfme_importer.util import write_json_atomic


REPO_IMPORTER = Path(__file__).parents[1]
BASE_PROFILE_PATH = REPO_IMPORTER / "profiles" / "men-fords-v0.json"


def _canonical_sha256(value: object) -> str:
    return hashlib.sha256(
        json.dumps(
            value,
            sort_keys=True,
            ensure_ascii=False,
            separators=(",", ":"),
            allow_nan=False,
        ).encode("utf-8")
    ).hexdigest()


def _read(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    assert isinstance(value, dict)
    return value


def _road_profile(base: dict) -> dict:
    road = deepcopy(base)
    road["id"] = composer.ROAD_PROFILE_ID
    road["pack"]["id"] = "fixture-roads"
    map_resource = next(
        item for item in road["resources"] if item["id"] == composer.MAP_RESOURCE_ID
    )
    map_resource["options"]["metadata"]["roadMaterials"] = (
        composer.ROAD_MATERIALS_RELATIVE_PATH
    )
    for resource_id, (source, output) in composer.EXPECTED_ROAD_RESOURCES.items():
        road["resources"].append(
            {
                "id": resource_id,
                "kind": "texture",
                "converter": "texture",
                "patterns": [source],
                "output": output,
                "limit": 1,
                "expected_count": 1,
            }
        )
    by_source = {
        source: output for source, output in composer.EXPECTED_ROAD_RESOURCES.values()
    }
    road_sources = (
        "art/compiledtextures/tr/trdirtroad.dds",
        "art/compiledtextures/tr/trftprntdrksing.dds",
        "art/compiledtextures/tr/trftprntgrsssing.dds",
        "art/compiledtextures/tr/trfootprintdark02.dds",
        "art/compiledtextures/tr/trfootprintdarksing.dds",
    )
    road["runtime_data"][composer.ROAD_RUNTIME_PATH] = {
        "schema": "openbfme.sage-road-materials",
        "schemaVersion": 0,
        "sourceReportAggregateSha256": "a" * 64,
        "roadCount": 5,
        "roads": [
            {
                "id": road_id,
                "sourceVirtualPath": source,
                "texturePng": by_source[source],
            }
            for road_id, source in zip(
                composer.EXPECTED_ROADS, road_sources, strict=True
            )
        ],
    }
    return road


def _atlas_resource(index: int, source: str, crop_names: list[str]) -> dict:
    output_root = f"assets/ui/men/fixture-atlas-{index:03d}"
    return {
        "id": f"men-ui-fixture-{index:03d}",
        "kind": "ui",
        "converter": "texture-atlas-crops",
        "patterns": [source],
        "output": output_root,
        "limit": 1,
        "expected_count": 1,
        "required": True,
        "options": {
            "crops": [
                {
                    "crop": [crop_index, 0, 1, 1],
                    "logicalName": f"fixture-{index:03d}-{crop_index:02d}",
                    "output": crop_name,
                }
                for crop_index, crop_name in enumerate(crop_names)
            ]
        },
    }


def _faction_profile() -> dict:
    semantic_patterns: list[str] = []
    for resource_id in sorted(composer.SEMANTIC_PRUNE_IDS):
        semantic_patterns.extend(composer.FULLY_PRUNED_BASE_RESOURCES[resource_id])
    semantic_patterns.extend(composer.PARTIAL_REMOVED_PATTERNS)
    semantic_patterns = list(dict.fromkeys(semantic_patterns))
    resources: list[dict] = [
        {
            "id": "men-semantic-definitions-000",
            "kind": "data",
            "converter": "hash-only",
            "patterns": semantic_patterns,
            "limit": len(semantic_patterns),
            "expected_count": len(semantic_patterns),
        }
    ]

    ui_sources = sorted(
        {
            path
            for resource_id in composer.UI_PRUNE_IDS
            for path in composer.FULLY_PRUNED_BASE_RESOURCES[resource_id]
        }
    )
    identifiers_by_source: dict[str, list[str]] = {}
    for identifier, source in composer.EXPECTED_UI_SOURCE_PATHS.items():
        identifiers_by_source.setdefault(source, []).append(identifier)
    ui_rows: list[dict] = []
    for index, source in enumerate(ui_sources):
        identifiers = identifiers_by_source.get(source, [])
        crop_names = [f"{identifier.lower()}.png" for identifier in identifiers]
        if not crop_names:
            crop_names = [f"unused-{index:03d}.png"]
        resource = _atlas_resource(index, source, crop_names)
        resources.append(resource)
        for identifier, crop_name in zip(
            identifiers, crop_names[: len(identifiers)], strict=True
        ):
            ui_rows.append(
                {
                    "id": identifier,
                    "path": f"{resource['output']}/{crop_name}",
                    "width": 1,
                    "height": 1,
                    "crop": {"left": 0, "top": 0, "width": 1, "height": 1},
                    "sourceAtlas": {
                        "compiledVirtualPath": source,
                        "texture": Path(source).name,
                        "width": 1,
                        "height": 1,
                    },
                }
            )

    # The real leaf profile has 78 exact atlas resources.  Fill the remaining
    # slots with distinct repository-authored fixture paths.
    for index in range(len(ui_sources), 78):
        resources.append(
            _atlas_resource(
                index,
                f"art/compiledtextures/fixture/ui_{index:03d}.dds",
                [f"image-{index:03d}.png"],
            )
        )

    audio_groups = (
        [f"data/audio/sounds/gusoldg_voisel{letter}.wav" for letter in "abcdefghi"],
        [f"data/audio/sounds/gusoldg_voiseb{letter}.wav" for letter in "abcdefghij"],
        [f"data/audio/sounds/gusoldg_voiatt{letter}.wav" for letter in "abcdef"],
        [f"data/audio/sounds/gusoldg_voiatc{letter}.wav" for letter in "abcdef"],
        [f"data/audio/sounds/gusoldg_voiatb{letter}.wav" for letter in "abc"],
        ["data/audio/sounds/gugoswo_voise2a.wav"],
    )
    for index, patterns in enumerate(audio_groups):
        resources.append(
            {
                "id": f"men-audio-leaves-{index:03d}",
                "kind": "audio",
                "converter": "audio",
                "patterns": patterns,
                "output": "assets/audio/men/{stem}.wav",
                "limit": len(patterns),
                "expected_count": len(patterns),
                "required": True,
                "options": {"force_pcm": True},
            }
        )
    assert len(resources) == composer.EXPECTED_FACTION_RESOURCE_COUNT
    return {
        "format": 1,
        "id": composer.FACTION_PROFILE_ID,
        "title": "Fixture faction leaves",
        "pack": {
            "id": "fixture-faction-leaves",
            "files": {
                "audioEvents": "data/audio_events.json",
                "strings": "data/strings.json",
                "uiManifest": "data/ui_manifest.json",
            },
        },
        "resources": resources,
        "runtime_data": {
            "data/audio_events.json": {
                "schema": "openbfme.audio-events",
                "schemaVersion": 1,
                "complete": False,
                "events": [],
                "multisounds": [],
                "rootIds": [],
                "samples": [],
            },
            "data/strings.json": {
                "schema": "openbfme.localized-strings",
                "schemaVersion": 0,
                "complete": False,
                "locale": "en",
                "strings": [],
            },
            "data/ui_manifest.json": {
                "schema": "openbfme.ui-manifest",
                "schemaVersion": 0,
                "complete": False,
                "images": ui_rows,
            },
        },
    }


def _static_plan() -> dict:
    resources: list[dict] = []
    texture_ids: list[str] = []
    for index in range(19):
        if index == 0:
            source = "art/compiledtextures/pt/ptgrass05.dds"
            resource_id = "static-prop-texture-fixture-00"
        elif index == 1:
            source = "art/compiledtextures/pr/prgrey.dds"
            resource_id = "static-prop-texture-prgrey-791db9ad131c"
        elif index == 2:
            source = "art/compiledtextures/sh/shadowi.tga"
            resource_id = "static-prop-texture-shadowi-6536093a930f"
        else:
            source = f"art/compiledtextures/fixture/static_texture_{index:02d}.dds"
            resource_id = f"static-prop-texture-fixture-{index:02d}"
        texture_ids.append(resource_id)
        resources.append(
            {
                "id": resource_id,
                "kind": "texture",
                "converter": "texture",
                "patterns": [source],
                "output": f"assets/textures/props/fixture-{index:02d}.png",
                "limit": 1,
                "expected_count": 1,
                "required": True,
            }
        )
    models: list[tuple[str, str]] = []
    for index in range(34):
        source = (
            "art/w3d/pt/ptgrass15.w3d"
            if index == 0
            else f"art/w3d/fixture/static_model_{index:02d}.w3d"
        )
        output = f"assets/models/props/fixture-{index:02d}.glb"
        resources.append(
            {
                "id": f"static-prop-model-fixture-{index:02d}",
                "kind": "model",
                "converter": "w3d-static",
                "patterns": [source],
                "output": output,
                "limit": 1,
                "expected_count": 1,
                "required": True,
                "options": {
                    "model": Path(source).name,
                    "inputResourceIds": [texture_ids[index % len(texture_ids)]],
                },
            }
        )
        models.append((source, output))
    bindings = []
    for index in range(38):
        source, output = models[index % len(models)]
        bindings.append(
            {
                "typeName": "PTGrass15" if index == 0 else f"FixtureProp{index:02d}",
                "matchMethod": "exact-type-name",
                "sourceVirtualModel": source,
                "glb": output,
            }
        )
    plan = {
        "schema": composer.STATIC_PLAN_SCHEMA,
        "schemaVersion": composer.STATIC_PLAN_SCHEMA_VERSION,
        "policy": {"substitutesAllowed": False, "placementDataConsumed": False},
        "summary": {
            "profileResourceCount": 53,
            "objectBindingModelRowCount": 38,
            "eligibleTargetTypeCount": 38,
        },
        "profileFragment": {
            "resources": resources,
            "objectBindings": {"models": bindings},
        },
    }
    plan["aggregateSha256"] = _canonical_sha256(plan)
    return plan


def _hierarchical_plan() -> dict:
    texture_rows = (
        ("hier-texture-gbbarracks", "art/compiledtextures/gb/gbbarracks_n.dds"),
        ("hier-texture-gbfarm", "art/compiledtextures/gb/gbfarm.dds"),
        ("hier-texture-prgrey", "art/compiledtextures/pr/prgrey.dds"),
        ("hier-texture-unique-00", "art/compiledtextures/fixture/hier_texture_00.dds"),
        ("hier-texture-unique-01", "art/compiledtextures/fixture/hier_texture_01.dds"),
        ("hier-texture-unique-02", "art/compiledtextures/fixture/hier_texture_02.dds"),
        ("hier-texture-unique-03", "art/compiledtextures/fixture/hier_texture_03.dds"),
        ("hier-texture-unique-04", "art/compiledtextures/fixture/hier_texture_04.dds"),
        ("hier-texture-unique-05", "art/compiledtextures/fixture/hier_texture_05.dds"),
        ("hier-texture-unique-06", "art/compiledtextures/fixture/hier_texture_06.dds"),
    )
    resources: list[dict] = []
    for index, (resource_id, source) in enumerate(texture_rows):
        resources.append(
            {
                "id": resource_id,
                "kind": "texture",
                "converter": "texture",
                "patterns": [source],
                "output": f"assets/textures/props-hierarchical/fixture-{index:02d}.png",
                "limit": 1,
                "expected_count": 1,
                "required": True,
            }
        )
    models: list[tuple[str, str]] = []
    dependency_sets = (
        ["hier-texture-gbbarracks", "hier-texture-gbfarm", "hier-texture-unique-00"],
        ["hier-texture-prgrey"],
        ["hier-texture-prgrey"],
        ["hier-texture-prgrey"],
        ["hier-texture-unique-01"],
        ["hier-texture-unique-02", "hier-texture-unique-03"],
    )
    for index, dependencies in enumerate(dependency_sets):
        source = f"art/w3d/fixture/hier_model_{index:02d}.w3d"
        output = f"assets/models/props-hierarchical/fixture-{index:02d}.glb"
        resources.append(
            {
                "id": f"hier-prop-model-fixture-{index:02d}",
                "kind": "model",
                "converter": "w3d-hierarchical",
                "patterns": [source],
                "output": output,
                "limit": 1,
                "expected_count": 1,
                "required": True,
                "options": {
                    "model": Path(source).name,
                    "animations": [],
                    "required_equipment": [],
                    "inputResourceIds": dependencies,
                },
            }
        )
        models.append((source, output))
    bindings = [
        {
            "typeName": f"HierFixtureProp{index:02d}",
            "matchMethod": "exact-type-name",
            "sourceVirtualModel": source,
            "glb": output,
        }
        for index, (source, output) in enumerate(models)
    ]
    plan = {
        "schema": composer.HIERARCHICAL_PLAN_SCHEMA,
        "schemaVersion": composer.HIERARCHICAL_PLAN_SCHEMA_VERSION,
        "policy": {
            "substitutesAllowed": False,
            "profileFragmentValidatedByImportProfile": True,
        },
        "summary": {
            "profileResourceCount": 16,
            "objectBindingModelRowCount": 6,
            "eligibleTargetTypeCount": 6,
            "uniqueModelSourceCount": 6,
            "uniqueTextureSourceCount": 10,
            "cumulativePlannedTargetTypeCount": 44,
        },
        "profileFragment": {
            "resources": resources,
            "objectBindings": {"models": bindings},
        },
    }
    plan["aggregateSha256"] = _canonical_sha256(plan)
    return plan


def _animated_plan() -> dict:
    texture_rows = (
        (
            "animated-texture-banner",
            "art/compiledtextures/fixture/all_faction_banners.dds",
        ),
        ("animated-texture-bear", "art/compiledtextures/fixture/cubear.dds"),
        ("animated-texture-duck", "art/compiledtextures/fixture/cuduck.dds"),
        ("animated-texture-egret", "art/compiledtextures/fixture/cuegret.dds"),
        ("animated-texture-elkf", "art/compiledtextures/fixture/cuelkf.dds"),
        ("animated-texture-elkm", "art/compiledtextures/fixture/cuelkm.dds"),
        ("animated-texture-fish", "art/compiledtextures/fixture/cutuna.dds"),
        ("animated-texture-rabbit", "art/compiledtextures/fixture/curabbit.dds"),
        ("animated-texture-raccoon", "art/compiledtextures/fixture/curaccoon.dds"),
        ("animated-texture-wolf", "art/compiledtextures/fixture/cuwolf.dds"),
        ("animated-texture-shadow", "art/compiledtextures/sh/shadowi.tga"),
    )
    resources: list[dict] = [
        {
            "id": resource_id,
            "kind": "texture",
            "converter": "hash-only",
            "patterns": [source],
            "limit": 1,
            "expected_count": 1,
            "required": True,
        }
        for resource_id, source in texture_rows
    ]
    resources.append(
        {
            "id": composer.EXPECTED_ANIMATED_SHARED_W3D_RESOURCE_ID,
            "kind": "data",
            "converter": "hash-only",
            "patterns": list(composer.EXPECTED_ANIMATED_SHARED_W3D_PATTERNS),
            "limit": len(composer.EXPECTED_ANIMATED_SHARED_W3D_PATTERNS),
            "expected_count": len(composer.EXPECTED_ANIMATED_SHARED_W3D_PATTERNS),
            "required": True,
        }
    )
    model_rows = (
        (
            "animated-model-bear",
            "art/w3d/fixture/cubear_skn.w3d",
            [
                "art/w3d/fixture/cubear_idla.w3d",
                "art/w3d/fixture/cubear_skl.w3d",
                "art/w3d/fixture/cubear_skn.w3d",
            ],
            ["cubear_idla.w3d"],
            ["animated-texture-bear"],
            "assets/models/props-animated/bear.glb",
        ),
        (
            "animated-model-capture-flag",
            "art/w3d/fixture/capflag_skn.w3d",
            [
                "art/w3d/fixture/capflag_skl.w3d",
                "art/w3d/fixture/capflag_skn.w3d",
                "art/w3d/fixture/capflag_up.w3d",
            ],
            ["capflag_up.w3d"],
            ["animated-texture-banner"],
            "assets/models/props-animated/capture-flag.glb",
        ),
        (
            "animated-model-duck",
            "art/w3d/fixture/cuduck_skn.w3d",
            [
                "art/w3d/fixture/cuduck_idla.w3d",
                "art/w3d/fixture/cuduck_skl.w3d",
                "art/w3d/fixture/cuduck_skn.w3d",
            ],
            ["cuduck_idla.w3d"],
            ["animated-texture-duck", "animated-texture-shadow"],
            "assets/models/props-animated/duck.glb",
        ),
        (
            "animated-model-egret",
            "art/w3d/fixture/cuegret_skn.w3d",
            [
                "art/w3d/fixture/cuegret_idla.w3d",
                "art/w3d/fixture/cuegret_skl.w3d",
                "art/w3d/fixture/cuegret_skn.w3d",
                "art/w3d/fixture/cuegret_wlka.w3d",
            ],
            ["cuegret_idla.w3d", "cuegret_wlka.w3d"],
            ["animated-texture-egret", "animated-texture-shadow"],
            "assets/models/props-animated/egret.glb",
        ),
        (
            "animated-model-elk-female",
            "art/w3d/fixture/cuelkf_skn.w3d",
            ["art/w3d/fixture/cuelkf_skn.w3d"],
            [
                "nuhorse_diea.w3d",
                "nuhorse_dwna.w3d",
                "nuhorse_grza.w3d",
                "nuhorse_grzb.w3d",
                "nuhorse_runa.w3d",
                "nuhorse_upa.w3d",
                "nuhorse_wlka.w3d",
            ],
            [
                composer.EXPECTED_ANIMATED_SHARED_W3D_RESOURCE_ID,
                "animated-texture-elkf",
                "animated-texture-shadow",
            ],
            "assets/models/props-animated/elk-female.glb",
        ),
        (
            "animated-model-elk-male",
            "art/w3d/fixture/cuelk_skn.w3d",
            ["art/w3d/fixture/cuelk_skn.w3d"],
            [
                "nuhorse_diea.w3d",
                "nuhorse_dwna.w3d",
                "nuhorse_grza.w3d",
                "nuhorse_grzb.w3d",
                "nuhorse_runa.w3d",
                "nuhorse_upa.w3d",
                "nuhorse_wlka.w3d",
            ],
            [
                composer.EXPECTED_ANIMATED_SHARED_W3D_RESOURCE_ID,
                "animated-texture-elkm",
                "animated-texture-shadow",
            ],
            "assets/models/props-animated/elk-male.glb",
        ),
        (
            "animated-model-fish",
            "art/w3d/fixture/cutuna_skn.w3d",
            [
                "art/w3d/fixture/cutuna_jmpa.w3d",
                "art/w3d/fixture/cutuna_skl.w3d",
                "art/w3d/fixture/cutuna_skn.w3d",
                "art/w3d/fixture/cutuna_swma.w3d",
            ],
            ["cutuna_jmpa.w3d", "cutuna_swma.w3d"],
            ["animated-texture-fish"],
            "assets/models/props-animated/fish.glb",
        ),
        (
            "animated-model-rabbit",
            "art/w3d/fixture/curabbit_skn.w3d",
            [
                "art/w3d/fixture/curabbit_idla.w3d",
                "art/w3d/fixture/curabbit_skl.w3d",
                "art/w3d/fixture/curabbit_skn.w3d",
            ],
            ["curabbit_idla.w3d"],
            ["animated-texture-rabbit"],
            "assets/models/props-animated/rabbit.glb",
        ),
        (
            "animated-model-raccoon",
            "art/w3d/fixture/curaccoon_skn.w3d",
            [
                "art/w3d/fixture/curaccoon_idla.w3d",
                "art/w3d/fixture/curaccoon_skl.w3d",
                "art/w3d/fixture/curaccoon_skn.w3d",
            ],
            ["curaccoon_idla.w3d"],
            ["animated-texture-raccoon"],
            "assets/models/props-animated/raccoon.glb",
        ),
        (
            "animated-model-wolf",
            "art/w3d/fixture/cuwolf_skn.w3d",
            [
                "art/w3d/fixture/cuwolf_idla.w3d",
                "art/w3d/fixture/cuwolf_skl.w3d",
                "art/w3d/fixture/cuwolf_skn.w3d",
            ],
            ["cuwolf_idla.w3d"],
            ["animated-texture-wolf", "animated-texture-shadow"],
            "assets/models/props-animated/wolf.glb",
        ),
    )
    model_output: dict[str, str] = {}
    for (
        resource_id,
        model_source,
        patterns,
        animations,
        dependencies,
        output,
    ) in model_rows:
        resources.append(
            {
                "id": resource_id,
                "kind": "model",
                "converter": "w3d-bundle",
                "patterns": patterns,
                "output": output,
                "limit": len(patterns),
                "expected_count": len(patterns),
                "required": True,
                "options": {
                    "model": Path(model_source).name,
                    "animations": animations,
                    "required_equipment": [],
                    "inputResourceIds": dependencies,
                },
            }
        )
        model_output[model_source] = output
    bindings = [
        {
            "typeName": type_name,
            "matchMethod": "exact-type-name",
            "sourceVirtualModel": model_source,
            "glb": model_output[model_source],
        }
        for type_name, model_source in (
            ("Bear", "art/w3d/fixture/cubear_skn.w3d"),
            ("CaptureFlag", "art/w3d/fixture/capflag_skn.w3d"),
            ("Duck", "art/w3d/fixture/cuduck_skn.w3d"),
            ("Egret", "art/w3d/fixture/cuegret_skn.w3d"),
            ("ElkFemale", "art/w3d/fixture/cuelkf_skn.w3d"),
            ("ElkMale", "art/w3d/fixture/cuelk_skn.w3d"),
            ("Fish", "art/w3d/fixture/cutuna_skn.w3d"),
            ("Rabbit", "art/w3d/fixture/curabbit_skn.w3d"),
            ("Raccoon", "art/w3d/fixture/curaccoon_skn.w3d"),
            ("Wolf", "art/w3d/fixture/cuwolf_skn.w3d"),
        )
    ]
    plan = {
        "schema": composer.ANIMATED_PLAN_SCHEMA,
        "schemaVersion": composer.ANIMATED_PLAN_SCHEMA_VERSION,
        "policy": {
            "converter": "w3d-bundle",
            "substitutesAllowed": False,
            "profileFragmentValidatedByImportProfile": True,
            "lifecycleOrMultipleModelTargetsAllowed": False,
        },
        "summary": {
            "profileResourceCount": 22,
            "objectBindingModelRowCount": 10,
            "eligibleTargetTypeCount": 10,
            "uniqueModelSourceCount": 10,
            "uniqueTextureSourceCount": 11,
            "conversionGroupCount": 10,
            "animatedBatchPlacementCount": 26,
        },
        "profileFragment": {
            "resources": resources,
            "objectBindings": {"models": bindings},
        },
    }
    plan["aggregateSha256"] = _canonical_sha256(plan)
    return plan


def _wildcard_entries(pattern: str, expected_count: int) -> list[str]:
    known_audio = {
        "data/audio/sounds/gusoldg_voisel?.wav": [
            f"data/audio/sounds/gusoldg_voisel{letter}.wav" for letter in "abcdefghi"
        ],
        "data/audio/sounds/gusoldg_voiseb?.wav": [
            f"data/audio/sounds/gusoldg_voiseb{letter}.wav" for letter in "abcdefghij"
        ],
        "data/audio/sounds/gusoldg_voiatt?.wav": [
            f"data/audio/sounds/gusoldg_voiatt{letter}.wav" for letter in "abcdef"
        ],
        "data/audio/sounds/gusoldg_voiatc?.wav": [
            f"data/audio/sounds/gusoldg_voiatc{letter}.wav" for letter in "abcdef"
        ],
        "data/audio/sounds/gusoldg_voiatb?.wav": [
            f"data/audio/sounds/gusoldg_voiatb{letter}.wav" for letter in "abc"
        ],
    }
    known_documents = {
        # The living-world strategic rule pulls the WOTR #include closure; a
        # representative member per wildcard keeps the fixture catalog honest.
        "data/ini/campaigns/common/*.inc": [
            "data/ini/campaigns/common/livingworldregions.inc",
            "data/ini/campaigns/common/livingworldcities.inc",
            "data/ini/campaigns/common/livingworlddefaultrtssettings.inc",
        ],
        "data/ini/campaigns/scenarios/*.inc": [
            "data/ini/campaigns/scenarios/wotrscenario001.inc",
        ],
    }
    if pattern in known_documents:
        return known_documents[pattern]
    if pattern in known_audio:
        return known_audio[pattern]
    if pattern.endswith("*.*"):
        prefix = pattern[:-3]
        exact_shared = {
            "art/compiledtextures/gb/gbfarm": "art/compiledtextures/gb/gbfarm.dds",
            "art/compiledtextures/gb/gbbarracks": (
                "art/compiledtextures/gb/gbbarracks_n.dds"
            ),
        }.get(prefix)
        if exact_shared is not None:
            return [exact_shared] + [
                f"{prefix}_fixture_{index:02d}.dds"
                for index in range(expected_count - 1)
            ]
        return [f"{prefix}_fixture_{index:02d}.dds" for index in range(expected_count)]
    raise AssertionError(f"unhandled fixture wildcard: {pattern}")


def _catalog(root: Path, profiles: list[dict]) -> InstallCatalog:
    names: dict[str, str] = {}
    for profile in profiles:
        for resource in profile["resources"]:
            expected = int(resource.get("expected_count", 0))
            for pattern in resource["patterns"]:
                if any(character in pattern for character in "*?["):
                    for name in _wildcard_entries(pattern, expected):
                        assert fnmatch.fnmatchcase(name.casefold(), pattern.casefold())
                        names.setdefault(name.casefold(), name)
                else:
                    names.setdefault(pattern.casefold(), pattern)
    entries = tuple(
        CatalogEntry("fixture.big", name, index, 1, 0)
        for index, name in enumerate(
            sorted(names.values(), key=lambda value: (value.casefold(), value))
        )
    )
    return InstallCatalog(root, (), entries)


class RetailSliceProfileTests(unittest.TestCase):
    def _fixture(
        self, root: Path
    ) -> tuple[Path, Path, Path, Path, Path, Path, InstallCatalog]:
        base = _read(BASE_PROFILE_PATH)
        road = _road_profile(base)
        faction = _faction_profile()
        static = _static_plan()
        hierarchical = _hierarchical_plan()
        animated = _animated_plan()
        base_path = root / "base.json"
        road_path = root / "roads.json"
        faction_path = root / "faction.json"
        static_path = root / "static.json"
        hierarchical_path = root / "hierarchical.json"
        animated_path = root / "animated.json"
        for path, value in (
            (base_path, base),
            (road_path, road),
            (faction_path, faction),
            (static_path, static),
            (hierarchical_path, hierarchical),
            (animated_path, animated),
        ):
            write_json_atomic(path, value)
        return (
            base_path,
            road_path,
            faction_path,
            static_path,
            hierarchical_path,
            animated_path,
            _catalog(
                root,
                [
                    base,
                    road,
                    faction,
                    {"resources": static["profileFragment"]["resources"]},
                    {"resources": hierarchical["profileFragment"]["resources"]},
                    {"resources": animated["profileFragment"]["resources"]},
                ],
            ),
        )

    def test_exact_composition_is_deterministic_and_importable(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            base, roads, faction, static, hierarchical, animated, catalog = (
                self._fixture(root)
            )
            first = composer.compose_retail_slice_profile(
                base, roads, faction, static, hierarchical, animated, catalog
            )
            second = composer.compose_retail_slice_profile(
                base, roads, faction, static, hierarchical, animated, catalog
            )
            self.assertEqual(first, second)
            self.assertEqual(first.profile["id"], composer.FULL_PROFILE_ID)
            self.assertEqual(first.profile["pack"]["id"], composer.PACK_ID)
            self.assertEqual(
                len(first.profile["resources"]), composer.EXPECTED_FINAL_RESOURCE_COUNT
            )
            self.assertEqual(first.report["runtime"]["roadCount"], 5)
            self.assertEqual(first.report["runtime"]["staticBindingCount"], 38)
            self.assertEqual(first.report["runtime"]["hierarchicalBindingCount"], 6)
            self.assertEqual(first.report["runtime"]["animatedBindingCount"], 10)
            self.assertEqual(first.report["runtime"]["finalBindingCount"], 54)
            self.assertEqual(first.report["resources"]["hierarchicalReused"], 3)
            self.assertEqual(first.report["resources"]["animatedReused"], 1)
            map_resource = next(
                item
                for item in first.profile["resources"]
                if item["id"] == composer.MAP_RESOURCE_ID
            )
            self.assertEqual(
                [
                    item["typeName"]
                    for item in map_resource["options"]["objectBindings"]["models"][
                        -10:
                    ]
                ],
                list(composer.EXPECTED_ANIMATED_BINDING_TYPES),
            )
            egret = next(
                item
                for item in first.profile["resources"]
                if item["id"] == "animated-model-egret"
            )
            self.assertIn(
                "static-prop-texture-shadowi-6536093a930f",
                egret["options"]["inputResourceIds"],
            )
            self.assertNotIn(
                "animated-texture-shadow",
                {item["id"] for item in first.profile["resources"]},
            )
            self.assertEqual(
                first.report["resolution"]["duplicateCatalogEntryCount"], 0
            )
            self.assertTrue(first.report["runtime"]["exactFactionDocumentsPreserved"])
            self.assertEqual(
                {
                    item["id"]
                    for item in first.profile["resources"]
                    if item["id"] in composer.FULLY_PRUNED_BASE_RESOURCES
                },
                set(),
            )
            narrowed = next(
                item
                for item in first.profile["resources"]
                if item["id"] == composer.PARTIAL_RESOURCE_ID
            )
            self.assertEqual(
                narrowed["patterns"], list(composer.PARTIAL_RETAINED_PATTERNS)
            )
            output = root / "composed.json"
            report = root / "report.json"
            composer.write_composed_retail_slice_profile(first, output, report)
            loaded = ImportProfile.load(output)
            self.assertEqual(loaded.pack_id, composer.PACK_ID)
            self.assertEqual(
                len(loaded.resources), composer.EXPECTED_FINAL_RESOURCE_COUNT
            )
            self.assertEqual(
                hashlib.sha256(output.read_bytes()).hexdigest(),
                first.report["profileSha256"],
            )

    def test_pruning_rejects_a_changed_exact_source_contract(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            base, roads, faction, static, hierarchical, animated, catalog = (
                self._fixture(root)
            )
            changed = _read(base)
            target = next(
                item
                for item in changed["resources"]
                if item["id"] == "fords-prop-ptgrass15-model"
            )
            target["patterns"] = ["art/w3d/pt/not-the-retail-model.w3d"]
            write_json_atomic(base, changed)
            with self.assertRaisesRegex(ValueError, "source contract changed"):
                composer.compose_retail_slice_profile(
                    base, roads, faction, static, hierarchical, animated, catalog
                )

    def test_declared_output_collisions_are_rejected_case_insensitively(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            base, roads, faction, static, hierarchical, animated, catalog = (
                self._fixture(root)
            )
            changed = _read(faction)
            ui = [
                item
                for item in changed["resources"]
                if item["converter"] == "texture-atlas-crops"
            ]
            ui[-1]["output"] = ui[-2]["output"].upper()
            write_json_atomic(faction, changed)
            with self.assertRaisesRegex(ValueError, "declared output collision"):
                composer.compose_retail_slice_profile(
                    base, roads, faction, static, hierarchical, animated, catalog
                )

    def test_distinct_patterns_cannot_own_the_same_catalog_entry(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            base, roads, faction, static, hierarchical, animated, catalog = (
                self._fixture(root)
            )
            for path in (base, roads):
                changed = _read(path)
                target = next(
                    item
                    for item in changed["resources"]
                    if item["id"] == "men-structure-shared-material-textures"
                )
                # This wildcard is textually distinct from the faction's exact
                # atlas rule but resolves to that same physical CatalogEntry.
                target["patterns"] = ["art/compiledtextures/fixture/ui_077.*"]
                target["limit"] = 1
                target["expected_count"] = 1
                write_json_atomic(path, changed)
            with self.assertRaisesRegex(ValueError, "CatalogEntry collision"):
                composer.compose_retail_slice_profile(
                    base, roads, faction, static, hierarchical, animated, catalog
                )


if __name__ == "__main__":
    unittest.main()
