from __future__ import annotations

from copy import deepcopy
import json
import tempfile
from pathlib import Path
import unittest

from openbfme_importer.profile import ImportProfile, MAX_RESOURCES
from openbfme_importer.retail_fords_completion_profile import (
    DISPLAY_NAMES,
    MEN_ORDER_HINT_RESOURCES,
    MEN_ORDER_HINT_RUNTIME,
    MEN_SELECTION_RESOURCES,
    MEN_SELECTION_RUNTIME,
    _canonical_sha256,
    _merge_audio_registry,
    _merge_men_damage_effects_runtime,
    _merge_men_damage_audio_registry,
    _merge_resources,
    _neutral_runtime_objects,
    _validate_animated_runtime_contract,
    _validate_men_damage_audio_contract,
    _validate_men_damage_effects_contract,
)
from openbfme_importer.retail_men_lifecycle_profile import (
    MEN_OBJECT_IDS,
    upgrade_men_building_lifecycles,
)
from openbfme_importer.util import write_json_atomic


def _resource(
    resource_id: str,
    pattern: str,
    *,
    converter: str = "hash-only",
    output: str | None = None,
    options: dict[str, object] | None = None,
) -> dict[str, object]:
    value: dict[str, object] = {
        "id": resource_id,
        "kind": "data",
        "patterns": [pattern],
        "required": True,
        "converter": converter,
        "limit": 1,
        "expected_count": 1,
    }
    if output is not None:
        value["output"] = output
    if options is not None:
        value["options"] = options
    return value


class RetailFordsCompletionProfileTests(unittest.TestCase):
    def test_men_order_hint_is_exact_source_contract(self) -> None:
        self.assertEqual(len(MEN_ORDER_HINT_RESOURCES), 2)
        self.assertEqual(
            MEN_ORDER_HINT_RESOURCES[0]["patterns"],
            [
                "art/compiledtextures/sc/scmovehinta.dds",
                "art/compiledtextures/sc/scmovehintb.dds",
                "art/compiledtextures/sc/scmovehintc.dds",
            ],
        )
        self.assertEqual(
            MEN_ORDER_HINT_RESOURCES[1]["converter"], "w3d-hierarchical"
        )
        self.assertEqual(
            MEN_ORDER_HINT_RESOURCES[1]["patterns"],
            ["art/w3d/sc/scmovehint.w3d"],
        )
        self.assertEqual(MEN_ORDER_HINT_RUNTIME["objectName"], "MoveHint")
        self.assertEqual(MEN_ORDER_HINT_RUNTIME["gameDataName"], "SCMoveHint")
        self.assertEqual(
            MEN_ORDER_HINT_RUNTIME["model"],
            "assets/models/system/scmovehint.glb",
        )

    def test_men_selection_decal_is_exact_source_contract(self) -> None:
        self.assertEqual(len(MEN_SELECTION_RESOURCES), 2)
        self.assertEqual(
            [row["patterns"][0] for row in MEN_SELECTION_RESOURCES],
            [
                "art/compiledtextures/de/decal_g_level1.dds",
                "art/compiledtextures/de/decal_good_co.dds",
            ],
        )
        self.assertEqual(MEN_SELECTION_RUNTIME["style"], "SHADOW_MERGE_DECAL")
        self.assertEqual(MEN_SELECTION_RUNTIME["minRadiusSource"], 50.0)
        self.assertEqual(MEN_SELECTION_RUNTIME["maxRadiusSource"], 200.0)
        self.assertEqual(MEN_SELECTION_RUNTIME["maxSelectedUnits"], 40)
        self.assertEqual(
            [row["sha256"] for row in MEN_SELECTION_RUNTIME["source"]["textures"]],
            [
                "ac07d037d5bb2a792737300230455f1cc4e2452de79870784e01e5e499430ef5",
                "4c7147389e37eccd7fde6f05329c607a07fc59f08c5e80712fb3aee8389337d6",
            ],
        )

    def test_private_men_lifecycle_v1_is_exact_closed_and_reproducible(self) -> None:
        root = Path(__file__).resolve().parents[2]
        profile_path = (
            root
            / ".private"
            / "retail-work"
            / "profiles"
            / "men-fords-v0-complete.generated.json"
        )
        report_path = (
            root
            / ".private"
            / "retail-work"
            / "reports"
            / "retail-men-building-lifecycle.json"
        )
        if not profile_path.is_file() or not report_path.is_file():
            self.skipTest("private retail lifecycle evidence is not present")

        profile = json.loads(profile_path.read_text("utf-8"))
        expected_resource_count = len(profile["resources"])
        report = json.loads(report_path.read_text("utf-8"))
        input_profile = deepcopy(profile)
        input_objects = input_profile["runtime_data"]["data/objects.json"]["objects"]
        for item in input_objects:
            if item.get("id") not in MEN_OBJECT_IDS:
                continue
            lifecycle = item["presentation"]["buildingLifecycle"]
            if lifecycle.get("schemaVersion") == 0:
                continue
            phases = {value["phase"]: value for value in lifecycle["phases"]}

            def clip(phase: str) -> dict[str, object]:
                animation = phases[phase]["animation"]
                names = [] if animation["clip"] is None else [animation["clip"]]
                names.extend(animation.get("alternateClips", []))
                return {"mode": animation["mode"], "names": names}

            item["presentation"]["buildingLifecycle"] = {
                "schema": "openbfme.building-lifecycle-presentation",
                "schemaVersion": 0,
                "maxHealth": lifecycle["simulationFacts"]["maximumHealth"],
                "damagedHealth": lifecycle["simulationFacts"]["damageStateRule"][
                    "damagedThreshold"
                ],
                "reallyDamagedHealth": lifecycle["simulationFacts"]["damageStateRule"][
                    "reallyDamagedThreshold"
                ],
                "paths": {
                    "construction": phases["construction"]["visual"]["glb"],
                    "intact": phases["intact"]["visual"]["glb"],
                    "damaged": phases["damaged"]["visual"]["glb"],
                    "reallyDamaged": phases["really-damaged"]["visual"]["glb"],
                    "rubble": phases["collapsing"]["visual"]["glb"],
                    "bib": lifecycle["bib"]["visual"]["glb"],
                },
                "clips": {
                    "construction": clip("construction"),
                    "intact": clip("intact"),
                    "reallyDamaged": clip("really-damaged"),
                    "rubble": clip("collapsing"),
                },
                "bibDuringConstruction": lifecycle["bib"]["duringConstruction"],
                "components": deepcopy(lifecycle.get("components", {})),
                "unresolved": deepcopy(lifecycle["unresolved"]),
            }

        before_non_men = [
            deepcopy(item)
            for item in input_objects
            if item.get("id") not in MEN_OBJECT_IDS
        ]
        first = upgrade_men_building_lifecycles(input_profile, report)
        second = upgrade_men_building_lifecycles(input_profile, report)
        self.assertEqual(first.profile, second.profile)
        self.assertEqual(
            first.aggregate_sha256,
            "926603db27e34c47e49c60f5179d8454d0e0c4d7a4cca5d12069e1d103880a59",
        )
        self.assertEqual(
            (
                first.structure_count,
                first.phase_count,
                first.no_render_phase_count,
                first.particle_attachment_count,
                first.entering_state_fx_binding_count,
                first.audio_binding_count,
                first.blocker_count,
            ),
            (5, 40, 10, 88, 17, 9, 40),
        )

        upgraded_objects = first.profile["runtime_data"]["data/objects.json"]["objects"]
        after_non_men = [
            deepcopy(item)
            for item in upgraded_objects
            if item.get("id") not in MEN_OBJECT_IDS
        ]
        self.assertEqual(before_non_men, after_non_men)
        resources = {item["id"]: item for item in first.profile["resources"]}
        contracts = {
            item["id"]: item["presentation"]["buildingLifecycle"]
            for item in upgraded_objects
            if item.get("id") in MEN_OBJECT_IDS
        }
        self.assertEqual(set(contracts), set(MEN_OBJECT_IDS))
        for object_id, lifecycle in contracts.items():
            self.assertEqual(lifecycle["schemaVersion"], 1, object_id)
            self.assertEqual(len(lifecycle["phases"]), 8, object_id)
            self.assertEqual(
                sum(
                    phase["visual"]["mode"] == "no-render"
                    for phase in lifecycle["phases"]
                ),
                2,
                object_id,
            )
            for phase in lifecycle["phases"]:
                visual = phase["visual"]
                if visual["mode"] != "glb":
                    self.assertNotIn("modelResourceId", visual)
                    continue
                resource = resources[visual["modelResourceId"]]
                self.assertEqual(resource["output"], visual["glb"])
            bib = lifecycle["bib"]["visual"]
            self.assertEqual(resources[bib["modelResourceId"]]["output"], bib["glb"])

        with tempfile.TemporaryDirectory() as raw:
            candidate = Path(raw) / "profile.json"
            write_json_atomic(candidate, first.profile)
            self.assertEqual(
                len(ImportProfile.load(candidate).resources), expected_resource_count
            )

        tampered = deepcopy(report)
        tampered["summary"]["runtimeStructureCount"] = 4
        with self.assertRaisesRegex(ValueError, "schema or exact summary changed"):
            upgrade_men_building_lifecycles(input_profile, tampered)

    def test_ambient_registry_merge_adds_exact_roots_without_upgrading_global_complete(
        self,
    ) -> None:
        base = {
            "complete": False,
            "events": {"Existing": {"sounds": [{"id": "old"}]}},
            "multisounds": {},
            "rootIds": ["Existing"],
            "samples": {"old": "assets/audio/old.wav"},
            "schema": "openbfme.audio-events",
            "schemaVersion": 1,
        }
        events = {
            f"Ambient{index}": {"sounds": [{"id": f"leaf{index}"}]}
            for index in range(7)
        }
        addition = {
            "complete": True,
            "events": events,
            "multisounds": {},
            "rootIds": list(events),
            "samples": {
                f"leaf{index}": f"assets/audio/ambient/leaf{index}.wav"
                for index in range(7)
            },
            "schema": "openbfme.audio-events",
            "schemaVersion": 1,
        }

        self.assertEqual(_merge_audio_registry(base, addition), (7, 7))
        self.assertFalse(base["complete"])
        self.assertEqual(len(base["events"]), 8)
        self.assertEqual(len(base["samples"]), 8)
        self.assertEqual(base["rootIds"][-7:], list(events))

        with self.assertRaisesRegex(ValueError, "collision"):
            _merge_audio_registry(base, addition)

    def test_private_men_damage_audio_contract_adds_only_eight_missing_samples(
        self,
    ) -> None:
        root = Path(__file__).resolve().parents[2]
        contract_path = (
            root / ".private" / "scratch" / "men-damage-audio" / "contract-a.json"
        )
        if not contract_path.is_file():
            self.skipTest("private Men damage-audio contract is not present")
        contract = json.loads(contract_path.read_text("utf-8"))
        fragment = _validate_men_damage_audio_contract(contract)
        self.assertEqual(len(fragment["resources"]), 1)

        events: dict[str, object] = {}
        for definition in contract["definitions"]:
            sounds = []
            for sample in definition["samplesInSourceOrder"]:
                if sample["role"] != "body":
                    continue
                row = {"id": sample["id"]}
                if "weight" in sample:
                    row["weight"] = sample["weight"]
                sounds.append(row)
            events[definition["requestedId"]] = {
                "parameters": deepcopy(definition["parametersInSourceOrder"]),
                "sounds": sounds,
            }
        samples = {
            sample["id"]: sample["profileAudioRegistryOutput"]
            for sample in contract["uniqueSamples"]
            if sample["registryPresent"]
        }
        registry = {"events": events, "samples": samples}
        self.assertEqual(
            _merge_men_damage_audio_registry(registry, contract), (9, 71, 8)
        )
        self.assertEqual(len(registry["events"]), 9)
        self.assertEqual(len(registry["samples"]), 79)
        self.assertIn("CUBuild_consL1a", registry["samples"])

        tampered = deepcopy(contract)
        tampered["summary"]["profileAudioRegistryMissingLeafCount"] = 7
        with self.assertRaisesRegex(ValueError, "aggregate digest mismatch"):
            _validate_men_damage_audio_contract(tampered)

    def test_private_men_damage_effects_adds_only_sealed_delta(self) -> None:
        root = Path(__file__).resolve().parents[2]
        contract_path = (
            root / ".private" / "scratch" / "men-damage-effects" / "contract-a.json"
        )
        profile_path = (
            root
            / ".private"
            / "retail-work"
            / "profiles"
            / "men-fords-v0-complete.generated.json"
        )
        if not contract_path.is_file() or not profile_path.is_file():
            self.skipTest("private Men damage-effects evidence is not present")
        contract = json.loads(contract_path.read_text("utf-8"))
        profile = json.loads(profile_path.read_text("utf-8"))
        fragment = _validate_men_damage_effects_contract(contract)
        self.assertEqual(len(fragment["resources"]), 24)

        proposed_ids = {row["id"] for row in fragment["resources"]}
        prior = [
            deepcopy(row)
            for row in profile["resources"]
            if row["id"] not in proposed_ids
        ]
        resources = [*prior, *deepcopy(fragment["resources"])]
        runtime = deepcopy(
            profile["runtime_data"]["effects/fords-particle-bindings.json"]
        )
        patch = fragment["runtimeDataPatch"]
        append_keys = {
            (row["kind"].casefold(), row["definitionId"].casefold())
            for row in patch["definitionRegistryAppend"]
        }
        runtime["definitionRegistry"] = [
            row
            for row in runtime["definitionRegistry"]
            if (row["kind"].casefold(), row["definitionId"].casefold())
            not in append_keys
        ]
        upsert_ids = {row["fxListId"].casefold() for row in patch["fxListsUpsert"]}
        runtime["fxLists"] = [
            row
            for row in runtime["fxLists"]
            if row["fxListId"].casefold() not in upsert_ids
        ]
        new_unresolved = set(patch["unresolvedDuplicateIdentifierSystemIdsAppend"])
        family = runtime["familyResolution"]
        family["duplicateIdentifierSystemIds"] = [
            value
            for value in family["duplicateIdentifierSystemIds"]
            if value not in new_unresolved
        ]
        family["unresolvedDuplicateIdentifierSystemIds"] = [
            value
            for value in family["unresolvedDuplicateIdentifierSystemIds"]
            if value not in new_unresolved
        ]

        self.assertEqual(
            _merge_men_damage_effects_runtime(runtime, contract, resources),
            (18, 4, 4),
        )
        self.assertEqual(len(runtime["definitionRegistry"]), 35)
        self.assertEqual(len(runtime["fxLists"]), 8)
        self.assertTrue(
            set(patch["unresolvedDuplicateIdentifierSystemIdsAppend"])
            <= set(
                runtime["familyResolution"]["unresolvedDuplicateIdentifierSystemIds"]
            )
        )
        self.assertNotIn("objectBindings", patch)
        self.assertEqual(len(patch["objectBindingsAppend"]), 5)

        tampered = deepcopy(contract)
        tampered["summary"]["particleDefinitionCandidateCount"] = 30
        with self.assertRaisesRegex(ValueError, "aggregate digest mismatch"):
            _validate_men_damage_effects_contract(tampered)

    def test_exact_hash_leaf_reuse_and_particle_multi_projection_are_distinct(
        self,
    ) -> None:
        base = [
            _resource(
                "base-audio",
                "data/audio/sounds/shared.wav",
                converter="audio",
                output="assets/audio/{stem}.wav",
            )
        ]
        neutral = [_resource("neutral-shared-audio", "data/audio/sounds/shared.wav")]
        particle = [
            _resource(
                "particle-a",
                "data/ini/particlesystem.ini",
                converter="sage-particle-definition",
                output="effects/a.json",
                options={"kind": "ParticleSystem", "name": "A"},
            ),
            _resource(
                "particle-b",
                "data/ini/particlesystem.ini",
                converter="sage-particle-definition",
                output="effects/b.json",
                options={"kind": "ParticleSystem", "name": "B"},
            ),
        ]

        resources, aliases, reused = _merge_resources(
            base, [("neutral", neutral), ("particle", particle)]
        )
        self.assertEqual(
            [item["id"] for item in resources],
            ["base-audio", "particle-a", "particle-b"],
        )
        self.assertEqual(aliases, {"neutral-shared-audio": "base-audio"})
        self.assertEqual(reused[0]["sourceVirtualPath"], "data/audio/sounds/shared.wav")

        with self.assertRaisesRegex(ValueError, "source already has an owner"):
            _merge_resources(
                [
                    _resource(
                        "model-a",
                        "art/w3d/test.w3d",
                        converter="w3d-static",
                        output="a.glb",
                    )
                ],
                [
                    (
                        "unsafe",
                        [
                            _resource(
                                "model-b",
                                "art/w3d/test.w3d",
                                converter="w3d-static",
                                output="b.glb",
                            )
                        ],
                    )
                ],
            )

    def test_neutral_lifecycle_v1_preserves_no_render_and_rewrites_evidence_ids(
        self,
    ) -> None:
        lifecycles = []
        type_names = {
            "bfme2.object.neutral-cave-troll-lair": "CaveTrollLair",
            "bfme2.object.neutral-inn": "Inn",
            "bfme2.object.neutral-warg-lair": "WargLair",
        }
        for object_id, type_name in type_names.items():
            slug = object_id.rsplit(".", 1)[-1]
            lifecycles.append(
                {
                    "objectId": object_id,
                    "typeName": type_name,
                    "schemaVersion": 1,
                    "initialPhase": "intact",
                    "normalWeatherModelTextureResourceIds": ["shared-texture"],
                    "phases": [
                        {
                            "phase": "intact",
                            "visual": {
                                "mode": "glb",
                                "glb": f"assets/models/{slug}/intact.glb",
                                "modelResourceId": f"{slug}-intact",
                            },
                        },
                        {
                            "phase": "post-rubble",
                            "visual": {"mode": "no-render", "sourceIdentifier": "NONE"},
                        },
                    ],
                }
            )

        objects = _neutral_runtime_objects(
            lifecycles, {"shared-texture": "existing-texture-owner"}
        )
        self.assertEqual({item["id"] for item in objects}, set(DISPLAY_NAMES))
        for item in objects:
            lifecycle = item["presentation"]["buildingLifecycle"]
            self.assertEqual(lifecycle["objectId"], item["id"])
            self.assertEqual(lifecycle["schemaVersion"], 1)
            self.assertEqual(
                lifecycle["normalWeatherModelTextureResourceIds"],
                ["existing-texture-owner"],
            )
            self.assertEqual(lifecycle["phases"][1]["visual"]["mode"], "no-render")

    def test_animated_runtime_contract_binds_ten_types_twenty_six_placements(
        self,
    ) -> None:
        counts = {
            "Bear": 1,
            "CaptureFlag": 2,
            "Duck": 2,
            "Egret": 2,
            "ElkFemale": 1,
            "ElkMale": 1,
            "Fish": 13,
            "Rabbit": 1,
            "Raccoon": 2,
            "Wolf": 1,
        }
        targets = []
        placements = []
        action_index = 0
        for target_index, (type_name, count) in enumerate(counts.items()):
            action_count = 35 if target_index == 0 else 1
            actions = []
            for _ in range(action_count):
                actions.append({"glbAction": f"action-{action_index}"})
                action_index += 1
            targets.append(
                {
                    "targetObject": type_name,
                    "placementCount": count,
                    "glbActionMap": actions,
                }
            )
            placements.extend({"typeName": type_name} for _ in range(count))
        document = {
            "schema": "openbfme.animated-prop-runtime-contract",
            "schemaVersion": 0,
            "scope": {
                "targetTypeCount": 10,
                "placementCount": 26,
                "targetTypes": list(counts),
            },
            "targets": targets,
            "placements": placements,
        }
        digest = _canonical_sha256(document)
        document["reproduction"] = {
            "identical": True,
            "pass1Sha256": digest,
            "pass2Sha256": digest,
        }
        self.assertEqual(_validate_animated_runtime_contract(document), digest)

        document["placements"].pop()
        tampered_digest = _canonical_sha256(
            {key: value for key, value in document.items() if key != "reproduction"}
        )
        document["reproduction"]["pass1Sha256"] = tampered_digest
        document["reproduction"]["pass2Sha256"] = tampered_digest
        with self.assertRaisesRegex(ValueError, "closure changed"):
            _validate_animated_runtime_contract(document)

    def test_complete_slice_size_fits_bounded_import_profile(self) -> None:
        def document(count: int) -> dict[str, object]:
            return {
                "format": 1,
                "id": "completion-size-test",
                "title": "Completion size test",
                "pack": {"id": "completion-size-test"},
                "resources": [
                    _resource(f"resource-{index:03d}", f"data/fake/{index:03d}.bin")
                    for index in range(count)
                ],
            }

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            accepted = root / "accepted.json"
            write_json_atomic(accepted, document(380))
            self.assertEqual(len(ImportProfile.load(accepted).resources), 380)

            rejected = root / "rejected.json"
            write_json_atomic(rejected, document(MAX_RESOURCES + 1))
            with self.assertRaisesRegex(ValueError, f"1..{MAX_RESOURCES} resources"):
                ImportProfile.load(rejected)


if __name__ == "__main__":
    unittest.main()
