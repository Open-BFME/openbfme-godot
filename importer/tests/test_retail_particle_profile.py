from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from openbfme_importer.profile import ImportProfile
from openbfme_importer import retail_particle_profile as planner
from openbfme_importer.retail_visual_profile import (
    _canonical_sha256,
    _effective_asset_aggregate,
)
from openbfme_importer.sage_particles import parse_particle_definitions


_PARTICLE_NAMES = {
    ("ParticleSystem", "BuildingContructDust"): "EXSmokePuf07.tga",
    ("ParticleSystem", "PCTMediumDust"): "EXCloud01.tga",
    ("ParticleSystem", "RDTMediumExplosion"): "EXFireEmbr.tga",
    ("ParticleSystem", "RDTMediumExplosionLight"): "EXexplo01.tga",
    ("ParticleSystem", "SmokeBuildingLarge"): "EXCloud01.tga",
    ("ParticleSystem", "SmokeBuildingMediumRubble"): "EXCloud01.tga",
    ("ParticleSystem", "WaterRipplesSmall"): "EXShockWav.tga",
    ("FXParticleSystem", "BuildingContructDust"): "EXsmokeplume.tga",
    ("FXParticleSystem", "BuildingDamaged"): "EXsmokeplume3.tga",
    ("FXParticleSystem", "PCTMediumDust"): "EXsmokeplume.tga",
    ("FXParticleSystem", "RDTMediumExplosion"): "EXFireEmbr.tga",
    ("FXParticleSystem", "RDTMediumExplosionLight"): "EXexplo01.tga",
    ("FXParticleSystem", "SmokeBuildingLarge"): "EXsmokeplume.tga",
    ("FXParticleSystem", "SmokeBuildingMediumRubble"): "EXsmokeplume.tga",
    ("FXParticleSystem", "UntamedAllegiance"): "EXEclipseBlur.tga",
    ("FXParticleSystem", "UntamedAllegiance2"): "EXGimliAxeSpecial.tga",
    ("FXParticleSystem", "WaterRipplesSmall"): "EXShockWav.tga",
}

_TEXTURE_PATHS = {
    "excloud01": "art/compiledtextures/ex/excloud01.dds",
    "execlipseblur": "art/compiledtextures/ex/execlipseblur.dds",
    "exexplo01": "art/compiledtextures/ex/exexplo01.dds",
    "exfireembr": "art/compiledtextures/ex/exfireembr.dds",
    "exgimliaxespecial": "art/compiledtextures/ex/exgimliaxespecial.dds",
    "exshockwav": "art/compiledtextures/ex/exshockwav.dds",
    "exsmokeplume": "art/compiledtextures/ex/exsmokeplume.dds",
    "exsmokeplume3": "art/compiledtextures/ex/exsmokeplume3.dds",
    "exsmokepuf07": "art/compiledtextures/ex/exsmokepuf07.dds",
}

_OBJECT_PATHS = {
    "CaveTrollLair": "data/ini/object/neutral/cavetrolllair.ini",
    "Inn": "data/ini/object/neutral/inn.ini",
    "WargLair": "data/ini/object/neutral/warglair.ini",
    "WtrRiplsSmall": "data/ini/object/civilian/civilianprop.ini",
}


def _seal(value: dict) -> dict:
    value.pop("aggregateSha256", None)
    value["aggregateSha256"] = _canonical_sha256(value)
    return value


def _write(root: Path, virtual_path: str, payload: bytes) -> None:
    path = root.joinpath(*virtual_path.split("/"))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)


def _source_row(entry: dict, role: str) -> dict:
    return {
        "archive": entry["archive"],
        "byteLength": entry["size"],
        "precedence": entry["precedence"],
        "role": role,
        "sha256": entry["sha256"],
        "virtualPath": entry["path"],
    }


def _scope(path: str, family: str, tokens: list[str]) -> list[dict]:
    outer = {
        "kind": "W3DScriptedModelDraw",
        "line": 1,
        "sourceVirtualPath": path,
        "headerTokens": ["W3DScriptedModelDraw", "ModuleTag_Draw"],
        "instanceTag": "ModuleTag_Draw",
    }
    inner: dict = {
        "kind": family,
        "line": 2,
        "sourceVirtualPath": path,
    }
    if tokens:
        inner["headerTokens"] = tokens
    return [outer, inner]


def _attachment(
    target: str,
    bone: str,
    system: str,
    family: str,
    tokens: list[str],
    options: list[str] | None = None,
) -> dict:
    path = _OBJECT_PATHS[target]
    option_values = options or []
    value = " ".join([bone, system, *option_values])
    return {
        "bone": bone,
        "definingObject": target,
        "field": "ParticleSysBone",
        "inheritanceDistance": 0,
        "line": 3,
        "options": option_values,
        "particleSystemId": system,
        "scope": _scope(path, family, tokens),
        "sourceVirtualPath": path,
        "value": value,
    }


def _fx_root(
    target: str,
    field: str,
    fx_list: str,
    family: str,
    tokens: list[str],
    stage: str | None = None,
) -> dict:
    path = _OBJECT_PATHS[target]
    value = f"{stage} {fx_list}" if stage else fx_list
    scope = _scope(path, family, tokens)
    if family == "StructureCollapseUpdate":
        scope = [
            {
                "kind": family,
                "line": 2,
                "sourceVirtualPath": path,
                "headerTokens": [family, "ModuleTag_Collapse"],
                "instanceTag": "ModuleTag_Collapse",
            }
        ]
    return {
        "definingObject": target,
        "field": field,
        "fxListId": fx_list,
        "inheritanceDistance": 0,
        "line": 4,
        "scope": scope,
        "sourceVirtualPath": path,
        "value": value,
    }


def _target_attachments(target: str) -> list[dict]:
    if target == "CaveTrollLair":
        return [
            _attachment(
                target,
                "NONE",
                "SmokeBuildingMediumRubble",
                "ModelConditionState",
                ["POST_RUBBLE"],
            ),
            _attachment(
                target,
                "NONE",
                "SmokeBuildingMediumRubble",
                "ModelConditionState",
                ["POST_COLLAPSE"],
            ),
            _attachment(
                target,
                "None",
                "UntamedAllegiance",
                "AnimationState",
                ["USER_2"],
                ["HouseColor:Yes"],
            ),
            _attachment(
                target,
                "None",
                "UntamedAllegiance2",
                "AnimationState",
                ["USER_2"],
                ["HouseColor:Yes"],
            ),
        ]
    if target == "Inn":
        return [
            _attachment(
                target,
                "DUSTBONE",
                "BuildingContructDust",
                "ModelConditionState",
                ["ACTIVELY_BEING_CONSTRUCTED", "PARTIALLY_CONSTRUCTED"],
            ),
            _attachment(
                target,
                "SmokeLarge01",
                "SmokeBuildingLarge",
                "ModelConditionState",
                ["RUBBLE"],
            ),
        ]
    if target == "WargLair":
        return [
            _attachment(
                target,
                "NONE",
                "SmokeBuildingMediumRubble",
                "ModelConditionState",
                ["POST_RUBBLE"],
            ),
            _attachment(
                target,
                "NONE",
                "SmokeBuildingMediumRubble",
                "ModelConditionState",
                ["POST_COLLAPSE"],
            ),
            _attachment(
                target,
                "None",
                "UntamedAllegiance",
                "AnimationState",
                ["USER_2"],
                ["HouseColor:Yes"],
            ),
            _attachment(
                target,
                "None",
                "UntamedAllegiance2",
                "AnimationState",
                ["USER_2"],
                ["HouseColor:Yes"],
            ),
        ]
    return [
        _attachment(
            target,
            "waterRippleBone",
            "WaterRipplesSmall",
            "IdleAnimationState",
            [],
        )
    ]


def _target_fx_roots(target: str) -> list[dict]:
    if target == "WtrRiplsSmall":
        return []
    roots = [
        _fx_root(
            target,
            "EnteringStateFX",
            "FX_BuildingDamaged",
            "AnimationState",
            ["DAMAGED"],
        ),
        _fx_root(
            target,
            "EnteringStateFX",
            "FX_BuildingReallyDamaged",
            "AnimationState",
            ["REALLYDAMAGED"],
        ),
        _fx_root(
            target,
            "EnteringStateFX",
            "FX_StructureMediumCollapse",
            "AnimationState",
            ["COLLAPSING"],
        ),
    ]
    if target != "Inn":
        roots.extend(
            [
                _fx_root(
                    target,
                    "FXList",
                    "FX_StructureMediumCollapse",
                    "StructureCollapseUpdate",
                    [],
                    "INITIAL",
                ),
                _fx_root(
                    target,
                    "FXList",
                    "FX_StructureAlmostCollapse",
                    "StructureCollapseUpdate",
                    [],
                    "ALMOST_FINAL",
                ),
            ]
        )
    return roots


def make_inputs(root: Path) -> tuple[dict, dict, dict]:
    legacy_parts: list[str] = []
    fx_parts: list[str] = []
    for (kind, name), particle_name in sorted(
        _PARTICLE_NAMES.items(), key=lambda item: (item[0][0], item[0][1])
    ):
        if kind == "ParticleSystem":
            legacy_parts.append(
                f"ParticleSystem {name}\n  Priority = AREA_EFFECT\n"
                f"  ParticleName = {particle_name}\nEnd\n"
            )
        else:
            fx_parts.append(
                f"FXParticleSystem {name}\n  System\n"
                f"    Priority = ALWAYS_RENDER\n    ParticleName = {particle_name}\n"
                "  End\nEnd\n"
            )
    payloads: dict[str, bytes] = {
        planner._PARTICLE_SOURCE_PATH: "".join(legacy_parts).encode("cp1252"),
        planner._FX_PARTICLE_SOURCE_PATH: "".join(fx_parts).encode("cp1252"),
        planner._SUBSYSTEM_LEGEND_PATH: (
            "LoadSubsystem TheFXParticleSystemManager\n"
            "  InitFile = Data\\INI\\FXParticleSystem.ini\nEnd\n"
            "LoadSubsystem TheParticleSystemManager\n"
            "  InitFile = Data\\INI\\ParticleSystem.ini\nEnd\n"
        ).encode("cp1252"),
        planner._RIPPLE_ANCHOR_PATH: b"legal-safe-ripple-anchor",
    }
    for path in _OBJECT_PATHS.values():
        payloads[path] = f"Object fixture for {path}\n".encode("cp1252")
    for stem, path in _TEXTURE_PATHS.items():
        payloads[path] = f"legal-safe-texture-{stem}".encode("ascii")

    fx_header = "; Name = {particle name in ParticleSystem.ini}\n"
    fx_parts_text = [fx_header]
    fx_spans: dict[str, tuple[int, int]] = {}
    current_line = 2
    for name, systems in planner._FX_LIST_SYSTEMS.items():
        start = current_line
        rows = [f"FXList {name}\n"]
        for system in systems:
            rows.extend(["  ParticleSystem\n", f"    Name = {system}\n", "  End\n"])
        if name == "FX_StructureMediumCollapse":
            rows.extend(
                [
                    "  Sound\n",
                    "    Name = BuildingSink\n",
                    "  End\n",
                    "  ViewShake\n",
                    "    Type = CINE_EXTREME\n",
                    "  End\n",
                ]
            )
        rows.append("End\n")
        fx_parts_text.extend(rows)
        current_line += len(rows)
        fx_spans[name] = (start, current_line - 1)
    payloads[planner._FX_LIST_SOURCE_PATH] = "".join(fx_parts_text).encode("cp1252")

    entries: list[dict] = []
    offset = 0
    for path in sorted(payloads, key=str.casefold):
        payload = payloads[path]
        _write(root, path, payload)
        entries.append(
            {
                "archive": "fixture.big",
                "offset": offset,
                "path": path,
                "precedence": 1,
                "sha256": hashlib.sha256(payload).hexdigest(),
                "size": len(payload),
            }
        )
        offset += len(payload)
    entry_by_path = {entry["path"]: entry for entry in entries}
    manifest = {
        "aggregate_sha256": _effective_asset_aggregate(entries),
        "catalog": {"identity_sha256": "a" * 64},
        "files": entries,
        "install": {"identity_sha256": "b" * 64},
        "schema": "openbfme.effective-assets-manifest",
        "schema_version": 0,
        "totals": {
            "bytes": sum(entry["size"] for entry in entries),
            "files": len(entries),
        },
    }

    parsed = {
        planner._PARTICLE_SOURCE_PATH: parse_particle_definitions(
            payloads[planner._PARTICLE_SOURCE_PATH]
        ),
        planner._FX_PARTICLE_SOURCE_PATH: parse_particle_definitions(
            payloads[planner._FX_PARTICLE_SOURCE_PATH]
        ),
    }
    definition_rows: dict[tuple[str, str], dict] = {}
    for source_path, definitions in parsed.items():
        kind = (
            "ParticleSystem"
            if source_path == planner._PARTICLE_SOURCE_PATH
            else "FXParticleSystem"
        )
        source_row = _source_row(
            entry_by_path[source_path], "particle-definition-document"
        )
        for definition in definitions:
            particle_names = [
                assignment.value
                for assignment in definition.assignments(recursive=True)
                if assignment.field.casefold() == "particlename"
            ]
            blocks = definition.blocks(recursive=True)
            texture_path = _TEXTURE_PATHS[Path(particle_names[0]).stem.casefold()]
            definition_rows[(kind, definition.name)] = {
                "assignmentCount": len(definition.assignments(recursive=True))
                + sum(block.selector is not None for block in blocks),
                "byteLength": definition.source.byte_length,
                "endLine": definition.source.end_line,
                "id": definition.name,
                "kind": kind,
                "nestedBlockCount": len(blocks),
                "particleNameIds": particle_names,
                "renderAssets": [
                    {
                        "evidence": "exact-tga-stem-to-compiled-dds",
                        "file": _source_row(
                            entry_by_path[texture_path], "particle-render-asset"
                        ),
                        "identifier": particle_names[0],
                        "status": "resolved",
                    }
                ],
                "sha256": definition.source.sha256,
                "source": source_row,
                "startLine": definition.source.start_line,
            }

    fx_payload = payloads[planner._FX_LIST_SOURCE_PATH]
    fx_lines = fx_payload.splitlines(keepends=True)
    fx_source_row = _source_row(
        entry_by_path[planner._FX_LIST_SOURCE_PATH],
        "fx-list-definition-document",
    )
    fx_rows: dict[str, dict] = {}
    for name, systems in planner._FX_LIST_SYSTEMS.items():
        start, end = fx_spans[name]
        block = b"".join(fx_lines[start - 1 : end])
        medium = name == "FX_StructureMediumCollapse"
        fx_rows[name] = {
            "assignmentCount": len(systems) + (2 if medium else 0),
            "audioEventIds": ["BuildingSink"] if medium else [],
            "byteLength": len(block),
            "endLine": end,
            "hasViewShake": medium,
            "id": name,
            "kind": "FXList",
            "nestedBlockCount": len(systems) + (2 if medium else 0),
            "particleNameIds": [],
            "particleSystemIds": list(systems),
            "sha256": hashlib.sha256(block).hexdigest(),
            "source": fx_source_row,
            "startLine": start,
        }

    targets: list[dict] = []
    for target, placement_count in planner._TARGET_PLACEMENTS.items():
        source_path = _OBJECT_PATHS[target]
        systems = list(planner._TARGET_SYSTEMS[target])
        rows = [
            definition_rows[(kind, system)]
            for system in systems
            for kind in planner._SYSTEM_FAMILIES[system]
        ]
        fx_ids = {root["fxListId"] for root in _target_fx_roots(target)}
        target_row = {
            "conversion": {
                "classification": (
                    "particle-system-runtime-work"
                    if target == "WtrRiplsSmall"
                    else "w3d-convertible-multi-state-building"
                )
            },
            "mapPlacements": {
                "count": placement_count,
                "records": [
                    {
                        "recordIndex": index,
                        "uniqueId": f"{target} {index}",
                    }
                    for index in range(placement_count)
                ],
            },
            "objectDefinition": {
                "ancestry": [target],
                "inheritanceComplete": True,
                "kind": "Object",
                "line": 1,
                "name": target,
                "parent": None,
                "sourceFile": _source_row(
                    entry_by_path[source_path], "object-definition-document"
                ),
                "sourceVirtualPath": source_path,
            },
            "particleAndFxClosure": {
                "attachments": _target_attachments(target),
                "definitions": rows,
                "fxLists": [
                    fx_rows[name] for name in planner._FX_LIST_SYSTEMS if name in fx_ids
                ],
                "fxRoots": _target_fx_roots(target),
                "particleSystemIds": systems,
            },
        }
        if target == "WtrRiplsSmall":
            target_row["visualReferences"] = {
                "count": 1,
                "references": [
                    {
                        "identifier": "P_WtrRiplsSmall",
                        "kind": "model",
                        "physicalFiles": [
                            _source_row(
                                entry_by_path[planner._RIPPLE_ANCHOR_PATH],
                                "visual-model",
                            )
                        ],
                        "status": "resolved",
                        "targetObject": target,
                    }
                ],
            }
        targets.append(target_row)

    census = {
        "schema": planner.CENSUS_SCHEMA,
        "schemaVersion": planner.CENSUS_SCHEMA_VERSION,
        "sourceEvidence": {
            "effectiveAssetsManifest": {
                "aggregateSha256": manifest["aggregate_sha256"],
                "byteLength": manifest["totals"]["bytes"],
                "catalogIdentitySha256": manifest["catalog"]["identity_sha256"],
                "fileCount": manifest["totals"]["files"],
            },
            "fxListDefinitionDocument": fx_source_row,
            "particleDefinitionDocuments": [
                _source_row(
                    entry_by_path[planner._PARTICLE_SOURCE_PATH],
                    "particle-definition-document",
                ),
                _source_row(
                    entry_by_path[planner._FX_PARTICLE_SOURCE_PATH],
                    "particle-definition-document",
                ),
            ],
        },
        "summary": {
            "fxListCount": 4,
            "particleDefinitionBlockCount": 17,
            "particleRuntimeTargetCount": 1,
            "placementCount": 15,
            "targetCount": 4,
            "uniqueParticleSystemCount": 10,
        },
        "targets": targets,
    }
    oracle_sources = []
    for path in (
        planner._PARTICLE_SOURCE_PATH,
        planner._FX_PARTICLE_SOURCE_PATH,
        planner._SUBSYSTEM_LEGEND_PATH,
        planner._FX_LIST_SOURCE_PATH,
        _OBJECT_PATHS["WtrRiplsSmall"],
    ):
        entry = entry_by_path[path]
        oracle_sources.append(
            {
                "path": f".private/retail-work/cache/effective-assets/{path}",
                "bytes": entry["size"],
                "sha256": entry["sha256"],
            }
        )
    oracle_sources.append(
        {
            "path": "F:/BFME2/game.dat",
            "bytes": 10,
            "sha256": "c" * 64,
            "file_version": "1.6.2429.30210",
            "product_version": "1.6.2429.30210",
        }
    )
    oracle = {
        "schema_version": planner.FAMILY_ORACLE_SCHEMA_VERSION,
        "scope": (
            "BFME2 1.06 particle declaration family lookup and duplicate precedence"
        ),
        "retail_sources": oracle_sources,
        "probe_name": {
            "name": "WaterRipplesSmall",
            "legacy": {
                "declaration": "ParticleSystem",
                "priority": "CRITICAL",
            },
            "fx": {
                "declaration": "FXParticleSystem",
                "priority": "VERY_LOW_OR_ABOVE",
            },
            "consumer": {
                "object": "WtrRiplsSmall",
                "reference_is_family_qualified": False,
            },
            "visible_fields_materially_equivalent": True,
            "material_discriminator": "priority/culling",
        },
        "retail_binary_evidence": {
            "manager_registration": {
                "subsystem_literal": "TheFXParticleSystemManager",
                "manager_global": "0xDFDD04",
            },
            "object_draw_consumer": {
                "manager_global_load_va": "0x7395DD",
                "name_lookup_target_va": "0x5F90DA",
            },
            "fxlist_consumer": {
                "runtime_manager_load_va": "0x5E1A34",
                "name_lookup_target_va": "0x5F90DA",
            },
            "fx_declaration_parser": {
                "manager_global_load_va": "0x5FD0DF",
                "find_target_va": "0x5F90DA",
                "duplicate_semantics": "last_definition_wins",
            },
        },
        "claims": [
            {"id": "C1", "grade": "PROVEN"},
            {"id": "C2", "grade": "PROVEN"},
            {"id": "C3", "grade": "PROVEN"},
            {"id": "C4", "grade": "UNRESOLVED"},
            {"id": "C5", "grade": "UNRESOLVED"},
            {"id": "C6", "grade": "CORROBORATION_ONLY"},
        ],
        "converter_guidance": {
            "preserve_both_source_declarations": True,
            "preserve_family_and_source_provenance": True,
            "emit_single_runtime_binding": True,
            "current_provisional_choice_for_WaterRipplesSmall": (
                "FXParticleSystem"
            ),
            "choice_is_retail_precedence_proof": False,
            "reason": (
                "Retail registers the FX manager and the two visible definitions are "
                "materially equivalent apart from priority/culling"
            ),
        },
    }
    return _seal(census), manifest, oracle


class RetailParticleProfileTests(unittest.TestCase):
    def test_exact_closure_generates_27_resource_profile_and_bindings(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            inputs = make_inputs(root)
            first = planner.build_retail_fords_particle_plan(*inputs, root)
            second = planner.build_retail_fords_particle_plan(*inputs, root)

        self.assertEqual(first, second)
        self.assertEqual(first["schema"], planner.FORDS_PARTICLE_PLAN_SCHEMA)
        self.assertEqual(first["summary"]["targetTypeCount"], 4)
        self.assertEqual(first["summary"]["placementCount"], 15)
        self.assertEqual(first["summary"]["particleSystemIdCount"], 10)
        self.assertEqual(first["summary"]["definitionResourceCount"], 17)
        self.assertEqual(first["summary"]["legacyDefinitionCount"], 7)
        self.assertEqual(first["summary"]["fxDefinitionCount"], 10)
        self.assertEqual(first["summary"]["textureResourceCount"], 9)
        self.assertEqual(first["summary"]["profileResourceCount"], 27)
        self.assertEqual(first["summary"]["directAttachmentCount"], 11)
        self.assertEqual(first["summary"]["fxRootCount"], 13)
        self.assertEqual(first["summary"]["provisionalRuntimeSelectionCount"], 1)
        self.assertEqual(first["summary"]["unresolvedFamilySelectionCount"], 6)
        self.assertTrue(first["policy"]["profileFragmentValidatedByImportProfile"])

        resources = first["profileFragment"]["resources"]
        self.assertEqual(
            sum(
                resource["converter"] == "sage-particle-definition"
                for resource in resources
            ),
            17,
        )
        self.assertEqual(
            sum(resource["converter"] == "texture" for resource in resources), 9
        )
        anchors = [
            resource for resource in resources if resource["converter"] == "hash-only"
        ]
        self.assertEqual(len(anchors), 1)
        self.assertEqual(anchors[0]["patterns"], [planner._RIPPLE_ANCHOR_PATH])

        runtime = first["profileFragment"]["runtimeData"]
        ripple = next(
            row
            for row in runtime["objectBindings"]
            if row["typeName"] == "WtrRiplsSmall"
        )
        self.assertEqual(ripple["placementCount"], 7)
        self.assertEqual(ripple["anchor"]["bone"], "waterRippleBone")
        self.assertEqual(ripple["attachments"][0]["anchorBone"], "waterRippleBone")
        self.assertEqual(
            ripple["attachments"][0]["trigger"]["stateFamily"],
            "IdleAnimationState",
        )

    def test_generated_profile_parses_and_carries_private_runtime_fragment(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            plan = planner.build_retail_fords_particle_plan(*make_inputs(root), root)
            profile_document = planner.generated_import_profile(plan)
            path = root / "profile.json"
            path.write_text(json.dumps(profile_document), encoding="utf-8")
            profile = ImportProfile.load(path)

        self.assertEqual(len(profile.resources), 27)
        self.assertIn(planner.RUNTIME_DATA_PATH, profile.runtime_data)
        self.assertEqual(
            profile.runtime_data[planner.RUNTIME_DATA_PATH]["schema"],
            planner.FORDS_PARTICLE_BINDINGS_SCHEMA,
        )

    def test_oracle_selects_only_provisional_water_runtime_family(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            plan = planner.build_retail_fords_particle_plan(*make_inputs(root), root)

        resolution = plan["familyResolution"]
        self.assertEqual(
            resolution["status"],
            "provisional-selection-with-cross-family-precedence-unresolved",
        )
        self.assertEqual(len(resolution["duplicateIdentifierSystemIds"]), 7)
        self.assertEqual(len(resolution["provisionalRuntimeSelections"]), 1)
        self.assertEqual(
            len(resolution["unresolvedDuplicateIdentifierSystemIds"]), 6
        )
        self.assertEqual(
            resolution["runtimeNamespace"]["status"],
            "proven-single-unqualified-manager-namespace",
        )
        self.assertEqual(
            resolution["duplicateSemantics"]["repeatedFxParticleSystemSyntax"],
            "proven-last-definition-wins",
        )
        self.assertEqual(
            resolution["duplicateSemantics"]["crossFamilyPrecedence"],
            "unresolved",
        )
        declarations = resolution["retailAuthoredEvidence"]["subsystemLegend"][
            "declarationsInAuthoredFileOrder"
        ]
        self.assertEqual(declarations[0]["name"], "TheFXParticleSystemManager")
        self.assertFalse(
            resolution["retailAuthoredEvidence"]["subsystemLegend"][
                "provesLoaderExecutionOrder"
            ]
        )
        water = next(
            row
            for row in plan["profileFragment"]["runtimeData"]["definitionRegistry"]
            if row["definitionId"] == "WaterRipplesSmall"
            and row["kind"] == "ParticleSystem"
        )
        self.assertEqual(water["textureResourceIds"], [water["textureResourceIds"][0]])
        ripple = next(
            row
            for row in plan["profileFragment"]["runtimeData"]["objectBindings"]
            if row["typeName"] == "WtrRiplsSmall"
        )
        candidates = ripple["systems"][0]["definitionCandidates"]
        self.assertEqual(
            [row["kind"] for row in candidates], ["ParticleSystem", "FXParticleSystem"]
        )
        water_resolution = ripple["systems"][0]["familyResolution"]
        self.assertEqual(water_resolution["selectedKind"], "FXParticleSystem")
        self.assertEqual(
            water_resolution["status"], "provisional-explicit-runtime-selection"
        )
        self.assertFalse(water_resolution["crossFamilyPrecedenceProven"])
        self.assertFalse(water_resolution["generalizesToOtherDuplicateIdentifiers"])
        cave = next(
            row
            for row in plan["profileFragment"]["runtimeData"]["objectBindings"]
            if row["typeName"] == "CaveTrollLair"
        )
        medium_dust = next(
            row
            for row in cave["systems"]
            if row["particleSystemId"] == "PCTMediumDust"
        )
        self.assertIsNone(medium_dust["familyResolution"]["selectedKind"])
        self.assertEqual(
            medium_dust["familyResolution"]["status"],
            "unresolved-cross-family-precedence",
        )

    def test_resealed_definition_span_mutation_still_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            census, manifest, oracle = make_inputs(root)
            census = copy.deepcopy(census)
            definition = census["targets"][0]["particleAndFxClosure"]["definitions"][0]
            definition["sha256"] = "f" * 64
            _seal(census)
            with self.assertRaisesRegex(ValueError, "definition span mismatch"):
                planner.build_retail_fords_particle_plan(
                    census, manifest, oracle, root
                )

    def test_missing_family_is_not_silently_collapsed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            census, manifest, oracle = make_inputs(root)
            census = copy.deepcopy(census)
            ripple = next(
                target
                for target in census["targets"]
                if target["objectDefinition"]["name"] == "WtrRiplsSmall"
            )
            ripple["particleAndFxClosure"]["definitions"] = [
                definition
                for definition in ripple["particleAndFxClosure"]["definitions"]
                if definition["kind"] != "ParticleSystem"
            ]
            census["summary"]["particleDefinitionBlockCount"] = 16
            _seal(census)
            with self.assertRaisesRegex(ValueError, "family closure mismatch"):
                planner.build_retail_fords_particle_plan(
                    census, manifest, oracle, root
                )

    def test_resealed_fx_list_count_mutation_fails_source_recount(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            census, manifest, oracle = make_inputs(root)
            census = copy.deepcopy(census)
            fx_list = census["targets"][0]["particleAndFxClosure"]["fxLists"][0]
            fx_list["assignmentCount"] += 1
            _seal(census)
            with self.assertRaisesRegex(
                ValueError, "FX list assignment count mismatch"
            ):
                planner.build_retail_fords_particle_plan(
                    census, manifest, oracle, root
                )

    def test_private_tree_byte_drift_fails_manifest_bound_read(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            census, manifest, oracle = make_inputs(root)
            path = root.joinpath(*planner._RIPPLE_ANCHOR_PATH.split("/"))
            path.write_bytes(path.read_bytes() + b"drift")
            with self.assertRaisesRegex(ValueError, "source size mismatch"):
                planner.build_retail_fords_particle_plan(
                    census, manifest, oracle, root
                )

    def test_declared_census_digest_is_authoritative(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            census, manifest, oracle = make_inputs(root)
            census["summary"]["placementCount"] = 999
            with self.assertRaisesRegex(ValueError, "census digest mismatch"):
                planner.build_retail_fords_particle_plan(
                    census, manifest, oracle, root
                )

    def test_oracle_claim_downgrade_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            census, manifest, oracle = make_inputs(root)
            oracle = copy.deepcopy(oracle)
            oracle["claims"][0]["grade"] = "CORROBORATION_ONLY"
            with self.assertRaisesRegex(ValueError, "oracle claim grades mismatch"):
                planner.build_retail_fords_particle_plan(
                    census, manifest, oracle, root
                )


if __name__ == "__main__":
    unittest.main()
