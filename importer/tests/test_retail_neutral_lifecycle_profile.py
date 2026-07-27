from __future__ import annotations

import copy
import json
from pathlib import Path
import tempfile
import unittest

from openbfme_importer.profile import ImportProfile
from openbfme_importer.retail_neutral_lifecycle_profile import (
    NEUTRAL_LIFECYCLE_PLAN_SCHEMA,
    _STRUCTURES,
    _audio_paths,
    _all_model_specs,
    _all_w3d_paths,
    _canonical_sha256,
    _LEGACY_SECONDARY_WARNINGS,
    _expected_neutral_runtime_audio_events,
    _expected_w3d_paths,
    _model_texture_resource_ids,
    _texture_paths,
    build_retail_neutral_lifecycle_plan,
    generated_import_profile,
    load_retail_neutral_lifecycle_census,
    load_retail_neutral_simulation_facts,
    write_retail_neutral_lifecycle_plan,
)


def _write_fixture_source(root: Path, virtual_path: str) -> dict:
    if virtual_path == "data/ini/soundeffects.ini":
        lines: list[str] = []
        for event_id, event in _expected_neutral_runtime_audio_events().items():
            lines.append(f"AudioEvent {event_id}")
            lines.append(
                "  Sounds = "
                + " ".join(str(sound["id"]) for sound in event["sounds"])
            )
            lines.extend(
                f"  {parameter['field']} = {parameter['value']}"
                for parameter in event["parameters"]
            )
            lines.append("End")
        data = ("\n".join(lines) + "\n").encode()
    else:
        data = f"repository-authored fixture:{virtual_path}\n".encode()
    target = root.joinpath(*virtual_path.split("/"))
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    import hashlib

    return {
        "archive": "fixture.big",
        "byteLength": len(data),
        "precedence": 1,
        "roles": ["repository-authored-fixture"],
        "sha256": hashlib.sha256(data).hexdigest(),
        "virtualPath": virtual_path,
    }


def _source_index(root: Path) -> dict[str, dict]:
    paths = {
        "data/ini/soundeffects.ini",
        "data/ini/object/neutral/holes.ini",
        *(spec.object_source for spec in _STRUCTURES),
        *(
            path
            for spec in _STRUCTURES
            for path in (
                *_all_w3d_paths(spec),
                *_texture_paths(spec),
                *_audio_paths(spec),
            )
        ),
    }
    sources = {
        path: _write_fixture_source(root, path)
        for path in sorted(paths, key=str.casefold)
    }
    manifest = {
        "schema": "openbfme.effective-assets-manifest",
        "schema_version": 0,
        "aggregate_sha256": "a" * 64,
        "files": [
            {
                "path": path,
                "sha256": source["sha256"],
                "size": source["byteLength"],
                "archive": source["archive"],
                "precedence": source["precedence"],
            }
            for path, source in sorted(sources.items(), key=lambda item: item[0])
        ],
    }
    manifest_path = root / ".openbfme" / "manifest.json"
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
    return sources


def _fixture_target(spec, sources: dict[str, dict]) -> dict:
    texture_groups = {group_id: paths for group_id, paths in spec.texture_groups}
    model_texture_paths: dict[str, set[str]] = {}
    for model in spec.models:
        expected = {
            path
            for group_id in model.texture_group_ids
            for path in texture_groups[group_id]
        }
        model_texture_paths.setdefault(model.model_source, set()).update(expected)

    animation_ids: dict[str, list[str]] = {}
    for model in spec.models:
        for path, identifier in zip(
            model.animation_sources, model.authored_animation_ids, strict=True
        ):
            animation_ids.setdefault(path, []).append(identifier)

    w3d_rows = []
    for path in _expected_w3d_paths(spec):
        has_model = any(model.model_source == path for model in spec.models)
        has_hierarchy = any(model.hierarchy_source == path for model in spec.models)
        embedded = [
            {
                "status": "resolved",
                "physicalFiles": [copy.deepcopy(sources[texture_path])],
            }
            for texture_path in sorted(
                model_texture_paths.get(path, ()), key=str.casefold
            )
        ]
        w3d_rows.append(
            {
                "file": copy.deepcopy(sources[path]),
                "fileHeaders": {
                    "modelIds": ["FIXTURE_MODEL"] if has_model else [],
                    "hierarchyIds": ["FIXTURE_HIERARCHY"] if has_hierarchy else [],
                    "animationIds": animation_ids.get(path, []),
                },
                "embeddedTextures": embedded,
                # Fixtures record the legacy scanner form (explicit
                # unsupported-chunk warnings) so the acceptance of archived
                # censuses stays under test; the current scanner records no
                # warnings for decoded secondary streams, which
                # test_current_scanner_secondary_rows_without_warnings_validate
                # pins separately.
                "warnings": [
                    {**warning, "offset": index + 1}
                    for index, warning in enumerate(
                        _LEGACY_SECONDARY_WARNINGS
                        if path in spec.secondary_skin_sources
                        else []
                    )
                ],
            }
        )

    model_by_id = {model.resource_id: model for model in spec.models}
    visual_rows = []
    for phase in spec.phases:
        for conditions in phase.source_conditions:
            row = {
                "identifier": phase.source_identifier,
                "kind": "model",
                "usage": phase.visual_usage,
                "conditions": list(conditions),
                "status": (
                    "semantic" if phase.visual_mode == "no-render" else "resolved"
                ),
            }
            if phase.visual_mode == "glb":
                assert phase.model_resource_id is not None
                row["physicalFiles"] = [
                    copy.deepcopy(
                        sources[model_by_id[phase.model_resource_id].model_source]
                    )
                ]
            visual_rows.append(row)

    bib = model_by_id[spec.bib_resource_id]
    visual_rows.append(
        {
            "identifier": "FixtureBib",
            "kind": "model",
            "usage": "floor-model",
            "conditions": [],
            "status": "resolved",
            "physicalFiles": [copy.deepcopy(sources[bib.model_source])],
        }
    )
    weather_paths = {
        path
        for group_id, paths in spec.texture_groups
        if group_id.endswith("-weather")
        for path in paths
    }
    for path in sorted(weather_paths, key=str.casefold):
        visual_rows.append(
            {
                "identifier": Path(path).name,
                "kind": "texture",
                "usage": "weather-texture",
                "conditions": ["SNOW"],
                "status": "resolved",
                "physicalFiles": [copy.deepcopy(sources[path])],
            }
        )

    audio_paths = _audio_paths(spec)
    sample_ids = {
        path: f"FixtureSample{index}" for index, path in enumerate(audio_paths)
    }
    samples = [
        {"id": sample_ids[path], "file": copy.deepcopy(sources[path])}
        for path in audio_paths
    ]
    events = [
        {
            "id": event_id,
            "sampleIds": [sample_ids[path] for path in paths],
            "sourceVirtualPath": "data/ini/soundeffects.ini",
        }
        for event_id, paths in spec.audio_routes
    ]

    attachments = [
        {
            "bone": attachment.bone,
            "options": list(attachment.options),
            "particleSystemId": attachment.particle_system_id,
            "scope": [
                {
                    "kind": "ModelConditionState",
                    "headerTokens": list(attachment.source_conditions),
                }
            ],
        }
        for attachment in spec.attachments
    ]
    condition_by_phase = {
        "damaged": "DAMAGED",
        "really-damaged": "REALLYDAMAGED",
        "collapsing": "COLLAPSING",
    }
    fx_roots = [
        {
            "field": "EnteringStateFX",
            "fxListId": fx_id,
            "scope": [
                {
                    "kind": "AnimationState",
                    "headerTokens": [condition_by_phase[phase]],
                }
            ],
        }
        for phase, fx_id in spec.entering_state_fx
    ]
    stage_token = {"initial": "INITIAL", "almost-final": "ALMOST_FINAL"}
    fx_roots.extend(
        {
            "field": "FXList",
            "fxListId": fx_id,
            "value": f"{stage_token[stage]} {fx_id}",
            "scope": [{"kind": "StructureCollapseUpdate", "headerTokens": []}],
        }
        for stage, fx_id in spec.collapse_update_fx
    )

    physical_paths = {
        spec.object_source,
        "data/ini/soundeffects.ini",
        *_expected_w3d_paths(spec),
        *_texture_paths(spec),
        *_audio_paths(spec),
    }
    physical_files = [
        copy.deepcopy(sources[path])
        for path in sorted(physical_paths, key=str.casefold)
    ]
    return {
        "objectDefinition": {
            "name": spec.type_name,
            "kind": "Object",
            "parent": None,
            "ancestry": [spec.type_name],
            "inheritanceComplete": True,
            "sourceVirtualPath": spec.object_source,
            "sourceFile": copy.deepcopy(sources[spec.object_source]),
        },
        "mapPlacements": {
            "count": spec.placement_count,
            "records": [
                {
                    "recordIndex": index,
                    "uniqueId": f"{spec.type_name} fixture {index}",
                }
                for index in range(spec.placement_count)
            ],
        },
        "w3dClosure": {"fileCount": len(w3d_rows), "files": w3d_rows},
        "visualReferences": {
            "count": len(visual_rows),
            "references": visual_rows,
        },
        "audioClosure": {
            "resolved": {
                "rootIds": sorted(dict(spec.audio_routes)),
                "samples": samples,
                "events": events,
                "multisounds": [],
                "definitionDocuments": [
                    copy.deepcopy(sources["data/ini/soundeffects.ini"])
                ],
            }
        },
        "particleAndFxClosure": {
            "attachments": attachments,
            "fxRoots": fx_roots,
        },
        "physicalClosure": {
            "aggregateSha256": "0" * 64,
            "byteLength": sum(item["byteLength"] for item in physical_files),
            "fileCount": len(physical_files),
            "files": physical_files,
        },
    }


def _seal_census(census: dict) -> dict:
    census.pop("aggregateSha256", None)
    census["aggregateSha256"] = _canonical_sha256(census)
    return census


def _simulation_fixture(sources: dict[str, dict]) -> dict:
    numbers = {
        "CaveTrollLair": (2000, 1000, 500, 30),
        "Inn": (3000, 2000, 1000, 45),
        "WargLair": (2000, 1000, 500, 30),
    }
    structures = []
    for spec in _STRUCTURES:
        maximum, damaged, really_damaged, build_time = numbers[spec.type_name]
        row = {
            "typeName": spec.type_name,
            "maximumHealth": maximum,
            "initialHealth": {
                "mapAuthoredPercent": 100,
                "derivedHitPoints": maximum,
                "status": "proven-full-health-map-placement",
            },
            "damageStateRule": {
                "damagedThreshold": damaged,
                "reallyDamagedThreshold": really_damaged,
                "boundaryStatus": (
                    "direct fields; still needs BFME2 executable-oracle confirmation"
                ),
            },
            "construction": {
                "buildTimeSeconds": build_time,
                "awaitingConditions": ["AWAITING_CONSTRUCTION"],
                "activeConditions": [
                    "ACTIVELY_BEING_CONSTRUCTED",
                    "PARTIALLY_CONSTRUCTED",
                ],
                "animationMode": "MANUAL",
            },
            "bib": {
                "drawModule": "W3DFloorDraw",
                "startHiddenAuthored": False,
                "hideIfModelConditions": [],
                "constructionVisibility": "unconditional-authored-floor-draw",
            },
            "captureInitialState": {
                "mapPlacementCount": spec.placement_count,
                "initialHealthPercent": 100,
                "initialPhase": "intact",
            },
        }
        if spec.rebuild_hole is None:
            row["construction"]["rebuildTimeSeconds"] = 40
            row["collapse"] = {
                "module": None,
                "keepObjectDie": True,
                "collapsingTimeAuthored": None,
                "exactTotalTimingStatus": (
                    "no-StructureCollapseUpdate-and-bfme2-KeepObjectDie-default-not-proven"
                ),
            }
            row["postRubble"] = {
                "rebuildHoleObject": None,
                "exactPostRubbleTransitionTimingStatus": (
                    "blocked-on-bfme2-KeepObjectDie-runtime-oracle"
                ),
            }
        else:
            hole = spec.rebuild_hole
            row["collapse"] = {
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
            row["postRubble"] = {
                "rebuildHoleObject": hole.type_name,
                "rebuildHoleModelVirtualPath": hole.model.model_source,
                "rebuildHoleModelSha256": sources[hole.model.model_source]["sha256"],
                "rebuildHoleMaximumHealth": hole.maximum_health,
                "rebuildHoleFadeInSeconds": hole.fade_in_seconds,
                "rebuildHoleHealthRegenPercentPerSecond": (
                    hole.health_regen_percent_per_second
                ),
                "terminalDuration": ("unbounded-until-rebuild-or-explicit-destruction"),
            }
        structures.append(row)
    return {
        "schema": "openbfme.neutral-simulation-facts",
        "schemaVersion": 1,
        "sources": [
            {
                "kind": "retail-ini",
                "virtualPath": path,
                "sha256": sources[path]["sha256"],
            }
            for path in [
                *(spec.object_source for spec in _STRUCTURES),
                "data/ini/object/neutral/holes.ini",
            ]
        ],
        "structures": structures,
        "blockers": [
            {"code": "bfme2-StructureCollapseUpdate-runtime-not-open-source"},
            {"code": "bfme2-KeepObjectDie-default-CollapsingTime-not-proven"},
            {"code": "capture-flag-link-not-explicit-in-decoded-map-record"},
        ],
    }


def _make_fixture(root: Path) -> tuple[dict, dict, Path]:
    effective_root = root / "effective-assets"
    sources = _source_index(effective_root)
    census = {
        "schema": "openbfme.fords-unresolved-object-census",
        "schemaVersion": 1,
        "targets": [_fixture_target(spec, sources) for spec in _STRUCTURES],
    }
    return _seal_census(census), _simulation_fixture(sources), effective_root


class RetailNeutralLifecycleProfileTests(unittest.TestCase):
    def test_exact_plan_is_deterministic_bounded_and_importprofile_valid(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            census, simulation, effective_root = _make_fixture(Path(raw))
            first = build_retail_neutral_lifecycle_plan(
                census, simulation, effective_root
            )
            second = build_retail_neutral_lifecycle_plan(
                census, simulation, effective_root
            )
            profile_payload = generated_import_profile(first)
            profile_path = Path(raw) / "profile.json"
            profile_path.write_text(json.dumps(profile_payload), encoding="utf-8")
            parsed = ImportProfile.load(profile_path)

        self.assertEqual(first, second)
        self.assertEqual(first["schema"], NEUTRAL_LIFECYCLE_PLAN_SCHEMA)
        self.assertEqual(first["summary"]["modelResourceCount"], 22)
        self.assertEqual(first["summary"]["uniqueW3dSourceCount"], 24)
        self.assertEqual(first["summary"]["textureEvidenceResourceCount"], 22)
        self.assertEqual(first["summary"]["audioEvidenceResourceCount"], 40)
        self.assertEqual(first["summary"]["profileResourceCount"], 84)
        self.assertEqual(first["summary"]["lifecyclePhaseCount"], 26)
        self.assertEqual(first["summary"]["placementCount"], 8)
        self.assertEqual(first["summary"]["authoredNoRenderPhaseCount"], 6)
        self.assertEqual(len(parsed.resources), 84)
        self.assertTrue(first["policy"]["profileFragmentValidatedByImportProfile"])
        self.assertFalse(first["policy"]["substitutesAllowed"])
        self.assertFalse(first["policy"]["fakeRubbleModelsAllowed"])
        self.assertFalse(first["policy"]["parityClaimAllowed"])

        resources = first["profileFragment"]["resources"]
        evidence_resources = [
            resource
            for resource in resources
            if resource["kind"] in {"texture", "audio"}
        ]
        self.assertEqual(len(evidence_resources), 62)
        neutral_audio = [
            resource
            for resource in evidence_resources
            if resource["kind"] == "audio" and resource["converter"] == "audio"
        ]
        self.assertEqual(len(neutral_audio), 27)
        self.assertTrue(
            all(
                resource["output"].startswith("assets/audio/neutral/")
                and resource["options"] == {"force_pcm": True}
                for resource in neutral_audio
            )
        )
        self.assertTrue(
            all(
                len(resource["patterns"]) == 1
                and resource["limit"] == 1
                and resource["expected_count"] == 1
                for resource in evidence_resources
            )
        )
        self.assertEqual(
            sum(resource["converter"] == "hash-only" for resource in evidence_resources),
            35,
        )
        registry = first["runtimeAudioRegistryAddition"]
        self.assertEqual(len(registry["events"]), 4)
        self.assertEqual(len(registry["samples"]), 27)
        self.assertEqual(
            first["profileFragment"]["runtimeDataMerge"]["data/audio_events.json"],
            registry,
        )
        pattern_owners = [
            pattern for resource in resources for pattern in resource["patterns"]
        ]
        self.assertEqual(len(pattern_owners), len(set(pattern_owners)))
        model_ids = {
            resource["id"]: resource
            for resource in resources
            if resource["kind"] == "model"
        }
        self.assertEqual(len(model_ids), 22)
        for spec in _STRUCTURES:
            for model in _all_model_specs(spec):
                self.assertEqual(
                    model_ids[model.resource_id]["options"]["inputResourceIds"],
                    list(_model_texture_resource_ids(spec, model)),
                )

        bindings = first["profileFragment"]["objectBindings"]["structures"]
        self.assertEqual(
            [binding["typeName"] for binding in bindings],
            ["CaveTrollLair", "Inn", "WargLair"],
        )
        no_render = [
            (lifecycle["typeName"], phase["phase"])
            for lifecycle in first["structureLifecycles"]
            for phase in lifecycle["phases"]
            if phase["visual"]["mode"] == "no-render"
        ]
        self.assertEqual(
            no_render,
            [
                ("CaveTrollLair", "rubble"),
                ("CaveTrollLair", "post-rubble"),
                ("CaveTrollLair", "post-collapse"),
                ("WargLair", "rubble"),
                ("WargLair", "post-rubble"),
                ("WargLair", "post-collapse"),
            ],
        )
        holes = {
            lifecycle["typeName"]: lifecycle["rebuildHole"]
            for lifecycle in first["structureLifecycles"]
        }
        self.assertEqual(holes["CaveTrollLair"]["sourceTypeName"], "CaveTrollLairHole")
        self.assertEqual(holes["CaveTrollLair"]["states"][0]["fadeInSeconds"], 2.0)
        self.assertEqual(holes["CaveTrollLair"]["maximumHealth"], 500.0)
        self.assertIsNone(holes["Inn"])
        self.assertEqual(holes["WargLair"]["sourceTypeName"], "WargLairHole")
        self.assertTrue(
            all(
                lifecycle["bib"]["duringConstruction"]
                for lifecycle in first["structureLifecycles"]
            )
        )
        blocker_codes = {blocker["code"] for blocker in first["blockers"]}
        self.assertIn("exact-particle-plan-cross-selection-pending", blocker_codes)
        self.assertIn(
            "neutral-lifecycle-completion-composer-handoff-pending",
            blocker_codes,
        )
        self.assertNotIn(
            "neutral-lifecycle-runtime-contract-not-integrated", blocker_codes
        )
        self.assertTrue(
            all(
                lifecycle["effects"]["definitionTranslationStatus"]
                == "exact-particle-plan-cross-selection-pending"
                for lifecycle in first["structureLifecycles"]
            )
        )

    def test_census_source_and_no_render_tampering_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            census, simulation, effective_root = _make_fixture(Path(raw))

            bad_digest = copy.deepcopy(census)
            bad_digest["aggregateSha256"] = "f" * 64
            with self.assertRaisesRegex(ValueError, "does not match canonical"):
                build_retail_neutral_lifecycle_plan(
                    bad_digest, simulation, effective_root
                )

            missing_w3d = copy.deepcopy(census)
            rows = missing_w3d["targets"][0]["w3dClosure"]["files"]
            rows.pop()
            missing_w3d["targets"][0]["w3dClosure"]["fileCount"] = len(rows)
            _seal_census(missing_w3d)
            with self.assertRaisesRegex(ValueError, "exact W3D closure"):
                build_retail_neutral_lifecycle_plan(
                    missing_w3d, simulation, effective_root
                )

            fake_rubble = copy.deepcopy(census)
            rubble = next(
                row
                for row in fake_rubble["targets"][0]["visualReferences"]["references"]
                if row["identifier"] == "None" and row["conditions"] == ["RUBBLE"]
            )
            rubble["status"] = "resolved"
            rubble["physicalFiles"] = [
                copy.deepcopy(
                    fake_rubble["targets"][0]["w3dClosure"]["files"][0]["file"]
                )
            ]
            _seal_census(fake_rubble)
            with self.assertRaisesRegex(ValueError, "not authored no-render"):
                build_retail_neutral_lifecycle_plan(
                    fake_rubble, simulation, effective_root
                )

            changed_audio = copy.deepcopy(census)
            event = changed_audio["targets"][1]["audioClosure"]["resolved"]["events"][0]
            event["sampleIds"] = list(reversed(event["sampleIds"]))
            _seal_census(changed_audio)
            with self.assertRaisesRegex(ValueError, "audio event routing"):
                build_retail_neutral_lifecycle_plan(
                    changed_audio, simulation, effective_root
                )

            wrong_health = copy.deepcopy(simulation)
            wrong_health["structures"][0]["maximumHealth"] = 1999
            with self.assertRaisesRegex(ValueError, "maximumHealth"):
                build_retail_neutral_lifecycle_plan(
                    census, wrong_health, effective_root
                )

            hidden_bib = copy.deepcopy(simulation)
            hidden_bib["structures"][2]["bib"]["startHiddenAuthored"] = True
            with self.assertRaisesRegex(ValueError, "unconditional bib proof"):
                build_retail_neutral_lifecycle_plan(census, hidden_bib, effective_root)

            wrong_hole = copy.deepcopy(simulation)
            wrong_hole["structures"][0]["postRubble"]["rebuildHoleModelSha256"] = (
                "f" * 64
            )
            with self.assertRaisesRegex(ValueError, "manifest disagrees"):
                build_retail_neutral_lifecycle_plan(census, wrong_hole, effective_root)

            source_path = effective_root / "art/w3d/nb/nbtrolllair.w3d"
            source_path.write_bytes(source_path.read_bytes() + b"tamper")
            with self.assertRaisesRegex(ValueError, "byte length mismatch"):
                build_retail_neutral_lifecycle_plan(census, simulation, effective_root)

    def test_current_scanner_secondary_rows_without_warnings_validate(self) -> None:
        # The current metadata scanner decodes secondary skin streams instead
        # of warning about them, so a census recorded by it carries empty
        # warning lists for secondary-skin sources.  Both recorded forms must
        # validate; any other warning content must stay a hard failure.
        with tempfile.TemporaryDirectory() as raw:
            census, simulation, effective_root = _make_fixture(Path(raw))
            secondary_paths = {
                path
                for spec in _STRUCTURES
                for path in spec.secondary_skin_sources
            }
            self.assertTrue(secondary_paths)

            modern = copy.deepcopy(census)
            cleared = 0
            for target in modern["targets"]:
                for row in target["w3dClosure"]["files"]:
                    if row["file"]["virtualPath"] in secondary_paths:
                        self.assertTrue(row["warnings"])
                        row["warnings"] = []
                        cleared += 1
            self.assertGreater(cleared, 0)
            _seal_census(modern)
            plan = build_retail_neutral_lifecycle_plan(
                modern, simulation, effective_root
            )
            self.assertEqual(plan["schema"], NEUTRAL_LIFECYCLE_PLAN_SCHEMA)

            unexpected = copy.deepcopy(modern)
            for target in unexpected["targets"]:
                for row in target["w3dClosure"]["files"]:
                    if row["file"]["virtualPath"] in secondary_paths:
                        row["warnings"] = [
                            {
                                "chunkId": 88,
                                "chunkIdHex": "0x00000058",
                                "code": "unsupported-chunk",
                                "message": (
                                    "metadata scanner does not interpret deform"
                                ),
                                "offset": 1,
                            }
                        ]
            _seal_census(unexpected)
            with self.assertRaisesRegex(ValueError, "unexpected W3D scanner"):
                build_retail_neutral_lifecycle_plan(
                    unexpected, simulation, effective_root
                )

    def test_helpers_write_verified_plan_and_reject_resealed_plan_tamper(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            census, simulation, effective_root = _make_fixture(root)
            census_path = root / "census.json"
            census_path.write_text(json.dumps(census), encoding="utf-8")
            simulation_path = root / "simulation.json"
            simulation_path.write_text(json.dumps(simulation), encoding="utf-8")
            loaded = load_retail_neutral_lifecycle_census(census_path)
            loaded_simulation = load_retail_neutral_simulation_facts(simulation_path)
            plan = build_retail_neutral_lifecycle_plan(
                loaded, loaded_simulation, effective_root
            )
            output = root / "plan.json"
            write_retail_neutral_lifecycle_plan(output, plan)
            written = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(written, plan)

            tampered = copy.deepcopy(plan)
            tampered["profileFragment"]["resources"][0]["patterns"] = [
                "fixture/not-the-source.dds"
            ]
            tampered.pop("aggregateSha256")
            tampered["aggregateSha256"] = _canonical_sha256(tampered)
            with self.assertRaisesRegex(ValueError, "exact contract"):
                generated_import_profile(tampered)


if __name__ == "__main__":
    unittest.main()
