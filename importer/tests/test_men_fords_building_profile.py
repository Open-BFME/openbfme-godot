from __future__ import annotations

import json
from pathlib import Path
import unittest

from openbfme_importer.pipeline import MEN_FORDS_SOURCE_ENTRY_COUNT
from openbfme_importer.profile import ImportProfile


PROFILE_PATH = Path(__file__).parents[1] / "profiles" / "men-fords-v0.json"
SCHEMA = "openbfme.building-lifecycle-presentation"

STRUCTURES = {
    "bfme2.object.men-fortress": {
        "slug": "men-fortress",
        "health": (7500, 2500, 1250),
        "clips": {
            "construction": ["gbfortress_abl"],
            "intact": [],
            "reallyDamaged": ["gbfortress_d2an"],
            "rubble": ["gbfortress_d3an"],
        },
    },
    "bfme2.object.men-farm": {
        "slug": "men-farm",
        "health": (2000, 1333, 667),
        "clips": {
            "construction": ["gbfarm_abld"],
            "intact": ["gbfarm_idla"],
            "reallyDamaged": ["gbfarm_d2an"],
            "rubble": ["gbfarm_d3an"],
        },
    },
    "bfme2.object.men-barracks": {
        "slug": "men-barracks",
        "health": (3000, 2000, 1000),
        "clips": {
            "construction": ["gbbarracks_abld"],
            "intact": ["gbbarracks_2ida", "gbbarracks_2idb"],
            "reallyDamaged": ["gbbarracks_d2an"],
            "rubble": ["gbbarracks_d3an"],
        },
    },
    "bfme2.object.men-archery-range": {
        "slug": "men-archery-range",
        "health": (3000, 2000, 1000),
        "clips": {
            "construction": ["gbarcheryn_abld"],
            "intact": ["gbarcheryn_idla"],
            "reallyDamaged": ["gbarcheryn_d2an"],
            "rubble": ["gbarcheryn_d3an"],
        },
    },
    "bfme2.object.men-stable": {
        "slug": "men-stable",
        "health": (3000, 2000, 1000),
        "clips": {
            "construction": ["gbstable_abld"],
            "intact": ["gbstable_idla"],
            "reallyDamaged": ["gbstable_d2an"],
            "rubble": ["gbstable_d3an"],
        },
    },
}

SOURCE_GROUPS = {
    "men-fortress-construction-model": (
        "w3d-bundle",
        "gbfortress_a.w3d",
        ["gbfortress_a.w3d", "gbfortress_ask.w3d", "gbfortress_abl.w3d"],
        ["gbfortress_abl.w3d"],
    ),
    "men-fortress-intact-model": (
        "w3d-hierarchical",
        "gbfortress.w3d",
        ["gbfortress.w3d"],
        [],
    ),
    "men-fortress-damaged-model": (
        "w3d-hierarchical",
        "gbfortress.w3d",
        ["gbfortress.w3d"],
        [],
    ),
    "men-fortress-really-damaged-model": (
        "w3d-bundle",
        "gbfortress_d2.w3d",
        ["gbfortress_d2.w3d", "gbfortress_d2sk.w3d", "gbfortress_d2an.w3d"],
        ["gbfortress_d2an.w3d"],
    ),
    "men-fortress-rubble-model": (
        "w3d-bundle",
        "gbfortress_d3.w3d",
        ["gbfortress_d3.w3d", "gbfortress_d3sk.w3d", "gbfortress_d3an.w3d"],
        ["gbfortress_d3an.w3d"],
    ),
    "men-fortress-bib-model": (
        "w3d-hierarchical",
        "gbfortress_bib.w3d",
        ["gbfortress_bib.w3d"],
        [],
    ),
    "men-farm-construction-model": (
        "w3d-bundle",
        "gbfarm_a.w3d",
        ["gbfarm_a.w3d", "gbfarm_askl.w3d", "gbfarm_abld.w3d"],
        ["gbfarm_abld.w3d"],
    ),
    "men-farm-intact-model": (
        "w3d-bundle",
        "gbfarm_skn.w3d",
        ["gbfarm_skn.w3d", "gbfarm_skl.w3d", "gbfarm_idla.w3d"],
        ["gbfarm_idla.w3d"],
    ),
    "men-farm-damaged-model": (
        "w3d-hierarchical",
        "gbfarm_d1.w3d",
        ["gbfarm_d1.w3d"],
        [],
    ),
    "men-farm-really-damaged-model": (
        "w3d-bundle",
        "gbfarm_d2.w3d",
        ["gbfarm_d2.w3d", "gbfarm_d2sk.w3d", "gbfarm_d2an.w3d"],
        ["gbfarm_d2an.w3d"],
    ),
    "men-farm-rubble-model": (
        "w3d-bundle",
        "gbfarm_d3.w3d",
        ["gbfarm_d3.w3d", "gbfarm_d3sk.w3d", "gbfarm_d3an.w3d"],
        ["gbfarm_d3an.w3d"],
    ),
    "men-farm-bib-model": (
        "w3d-hierarchical",
        "gbfarm_bib.w3d",
        ["gbfarm_bib.w3d"],
        [],
    ),
    "men-barracks-construction-model": (
        "w3d-bundle",
        "gbbarracks_a.w3d",
        ["gbbarracks_a.w3d", "gbbarracks_askl.w3d", "gbbarracks_abld.w3d"],
        ["gbbarracks_abld.w3d"],
    ),
    "men-barracks-intact-model": (
        "w3d-bundle",
        "gbbarracks_skn.w3d",
        [
            "gbbarracks_skn.w3d",
            "gbbarracks_2skl.w3d",
            "gbbarracks_2ida.w3d",
            "gbbarracks_2idb.w3d",
        ],
        ["gbbarracks_2ida.w3d", "gbbarracks_2idb.w3d"],
    ),
    "men-barracks-damaged-model": (
        "w3d-hierarchical",
        "gbbarracks_d1.w3d",
        ["gbbarracks_d1.w3d"],
        [],
    ),
    "men-barracks-really-damaged-model": (
        "w3d-bundle",
        "gbbarracks_d2.w3d",
        ["gbbarracks_d2.w3d", "gbbarracks_d2sk.w3d", "gbbarracks_d2an.w3d"],
        ["gbbarracks_d2an.w3d"],
    ),
    "men-barracks-rubble-model": (
        "w3d-bundle",
        "gbbarracks_d3.w3d",
        ["gbbarracks_d3.w3d", "gbbarracks_d3sk.w3d", "gbbarracks_d3an.w3d"],
        ["gbbarracks_d3an.w3d"],
    ),
    "men-barracks-bib-model": (
        "w3d-hierarchical",
        "gbbarracks_bib.w3d",
        ["gbbarracks_bib.w3d"],
        [],
    ),
    "men-archery-range-construction-model": (
        "w3d-bundle",
        "gbarcheryn_a.w3d",
        ["gbarcheryn_a.w3d", "gbarcheryn_askl.w3d", "gbarcheryn_abld.w3d"],
        ["gbarcheryn_abld.w3d"],
    ),
    "men-archery-range-intact-model": (
        "w3d-bundle",
        "gbarcheryn_skn.w3d",
        ["gbarcheryn_skn.w3d", "gbarcheryn_skl.w3d", "gbarcheryn_idla.w3d"],
        ["gbarcheryn_idla.w3d"],
    ),
    "men-archery-range-damaged-model": (
        "w3d-hierarchical",
        "gbarcheryn_d1.w3d",
        ["gbarcheryn_d1.w3d"],
        [],
    ),
    "men-archery-range-really-damaged-model": (
        "w3d-bundle",
        "gbarcheryn_d2.w3d",
        ["gbarcheryn_d2.w3d", "gbarcheryn_d2sk.w3d", "gbarcheryn_d2an.w3d"],
        ["gbarcheryn_d2an.w3d"],
    ),
    "men-archery-range-rubble-model": (
        "w3d-bundle",
        "gbarcheryn_d3.w3d",
        ["gbarcheryn_d3.w3d", "gbarcheryn_d3sk.w3d", "gbarcheryn_d3an.w3d"],
        ["gbarcheryn_d3an.w3d"],
    ),
    "men-archery-range-bib-model": (
        "w3d-hierarchical",
        "gbarcheryn_bib.w3d",
        ["gbarcheryn_bib.w3d"],
        [],
    ),
    "men-stable-construction-model": (
        "w3d-bundle",
        "gbstable_a.w3d",
        ["gbstable_a.w3d", "gbstable_askl.w3d", "gbstable_abld.w3d"],
        ["gbstable_abld.w3d"],
    ),
    "men-stable-intact-model": (
        "w3d-bundle",
        "gbstable_skn.w3d",
        ["gbstable_skn.w3d", "gbstable_skl.w3d", "gbstable_idla.w3d"],
        ["gbstable_idla.w3d"],
    ),
    "men-stable-damaged-model": (
        "w3d-hierarchical",
        "gbstable_d1.w3d",
        ["gbstable_d1.w3d"],
        [],
    ),
    "men-stable-really-damaged-model": (
        "w3d-bundle",
        "gbstable_d2.w3d",
        ["gbstable_d2.w3d", "gbstable_d2sk.w3d", "gbstable_d2an.w3d"],
        ["gbstable_d2an.w3d"],
    ),
    "men-stable-rubble-model": (
        "w3d-bundle",
        "gbstable_d3.w3d",
        ["gbstable_d3.w3d", "gbstable_d3sk.w3d", "gbstable_d3an.w3d"],
        ["gbstable_d3an.w3d"],
    ),
    "men-stable-bib-model": (
        "w3d-hierarchical",
        "gbstable_bib.w3d",
        ["gbstable_bib.w3d"],
        [],
    ),
}

MODEL_TEXTURES = {
    "gbfortress_a.w3d": {"gb/gbfortress1.dds", "gb/gbfortress1_nrm.tga"},
    "gbfortress.w3d": {
        "ex/exfiretorchseq.dds",
        "ex/exgflagseq.dds",
        "gb/gbfortress1.dds",
        "gb/gbfortress1_nrm.tga",
        "pg/pg02.dds",
    },
    "gbfortress_d2.w3d": {
        "ex/exfiretorchseq.dds",
        "ex/exgflagseq.dds",
        "gb/gbfortress1_nrm.tga",
        "gb/gbfortress1d.dds",
        "pg/pg02.dds",
    },
    "gbfortress_d3.w3d": {"gb/gbfortress1_nrm.tga", "gb/gbfortress1d.dds"},
    "gbfortress_bib.w3d": {"gb/gbwall_bib.dds"},
    "gbfarm_a.w3d": {"gb/gbfarm.dds", "gb/gbfarm_nrm.tga", "pm/pmrailtie.dds"},
    "gbfarm_skn.w3d": {
        "gb/gbfarm.dds",
        "gb/gbfarm_nrm.tga",
        "gb/gbnight2.dds",
        "pt/ptgrass04.dds",
        "rh/rhfarmtools.dds",
        "ru/rupeasant03_alt.dds",
    },
    "gbfarm_d1.w3d": {
        "gb/gbbarracks_n.dds",
        "gb/gbfarm_d.dds",
        "gb/gbfarm_nrm.tga",
    },
    "gbfarm_d2.w3d": {"gb/gbfarm_d.dds", "gb/gbfarm_nrm.tga"},
    "gbfarm_d3.w3d": {"gb/gbfarm_d.dds", "gb/gbfarm_nrm.tga"},
    "gbfarm_bib.w3d": {"gb/gbfarmt.dds"},
    "gbbarracks_a.w3d": {
        "gb/gbbarracks_new.dds",
        "gb/gbbarracks_new_nrm.tga",
        "pm/pmrailtie.dds",
    },
    "gbbarracks_skn.w3d": {
        "gb/gbbarracks_new.dds",
        "gb/gbbarracks_new_nrm.tga",
        "gb/gbnightwindows.dds",
        "gb/gbvet.dds",
        "gb/gbvet_nrm.tga",
        "gu/gubanner.dds",
        "gu/gumanatarms.dds",
    },
    "gbbarracks_d1.w3d": {
        "gb/gbbarracks_new_nrm.tga",
        "gb/gbbarracks_newd.dds",
        "gb/gbvet_nrm.tga",
        "gb/gbvetd.dds",
    },
    "gbbarracks_d2.w3d": {
        "gb/gbbarracks_new_nrm.tga",
        "gb/gbbarracks_newd.dds",
        "gb/gbvet_nrm.tga",
        "gb/gbvetd.dds",
    },
    "gbbarracks_d3.w3d": {
        "gb/gbbarracks_new_nrm.tga",
        "gb/gbbarracks_newd.dds",
        "gb/gbvet_nrm.tga",
        "gb/gbvetd.dds",
    },
    "gbbarracks_bib.w3d": {"gb/gbbarracks_bib.dds"},
    "gbarcheryn_a.w3d": {"gb/gbarcheryn_l.dds", "gb/gbvet.dds", "gu/guarcher.dds"},
    "gbarcheryn_skn.w3d": {
        "g_/g_arrow.dds",
        "gb/gbarcheryn_a.dds",
        "gb/gbarcheryn_a_nrm.tga",
        "gb/gbarcheryn_l.dds",
        "gb/gbarcheryn_l_nrm.tga",
        "gb/gbnightwindows.dds",
        "gb/gbvet.dds",
        "gb/gbvet_nrm.tga",
        "gu/guarcher.dds",
        "gu/gumanatarms.dds",
    },
    "gbarcheryn_d1.w3d": {
        "gb/gbarcheryn_a_nrm.tga",
        "gb/gbarcheryn_ad.dds",
        "gb/gbarcheryn_l_nrm.tga",
        "gb/gbarcheryn_ld.dds",
        "gb/gbvet_nrm.tga",
        "gb/gbvetd.dds",
        "gu/guarcher.dds",
    },
    "gbarcheryn_d2.w3d": {
        "gb/gbarcheryn_l_nrm.tga",
        "gb/gbarcheryn_ld.dds",
        "gb/gbvet_nrm.tga",
        "gb/gbvetd.dds",
    },
    "gbarcheryn_d3.w3d": {
        "gb/gbarcheryn_l_nrm.tga",
        "gb/gbarcheryn_ld.dds",
        "gb/gbvet_nrm.tga",
        "gb/gbvetd.dds",
    },
    "gbarcheryn_bib.w3d": {"gb/gbarcheryn_bib.dds"},
    "gbstable_a.w3d": {
        "gb/gbstable.dds",
        "gb/gbstable_nrm.tga",
        "gb/gbstablehorses.dds",
        "pm/pmrailtie.dds",
        "ru/rufrmhors04.dds",
    },
    "gbstable_skn.w3d": {
        "ex/exgflagseq.dds",
        "gb/gbnight.dds",
        "gb/gbstable.dds",
        "gb/gbstable_nrm.tga",
        "gb/gbstablehorses.dds",
        "gb/gbvet.dds",
        "gb/gbvet_nrm.tga",
        "ru/rufrmhors04.dds",
    },
    "gbstable_d1.w3d": {
        "ex/exgflagseq.dds",
        "gb/gbstable_d.dds",
        "gb/gbstable_nrm.tga",
        "gb/gbvet_nrm.tga",
        "gb/gbvetd.dds",
    },
    "gbstable_d2.w3d": {
        "ex/exgflagseq.dds",
        "gb/gbnight.dds",
        "gb/gbstable_d.dds",
        "gb/gbstable_nrm.tga",
        "gb/gbvet_nrm.tga",
        "gb/gbvetd.dds",
    },
    "gbstable_d3.w3d": {
        "gb/gbstable_d.dds",
        "gb/gbstable_nrm.tga",
        "gb/gbvet_nrm.tga",
        "gb/gbvetd.dds",
    },
    "gbstable_bib.w3d": {"gb/gbstablet.dds"},
}


def load_payload() -> dict:
    return json.loads(PROFILE_PATH.read_text(encoding="utf-8"))


class MenFordsBuildingProfileTests(unittest.TestCase):
    def test_import_profile_loads_and_lifecycle_contract_is_exact(self) -> None:
        profile = ImportProfile.load(PROFILE_PATH)
        self.assertEqual(profile.id, "men-fords-v0")
        # 83 asset/data rules, the living-world strategic document, and the
        # exact map plus two-library script composite.
        self.assertEqual(len(profile.resources), 85)
        self.assertEqual(MEN_FORDS_SOURCE_ENTRY_COUNT, 394)

        payload = load_payload()
        objects = {
            item["id"]: item
            for item in payload["runtime_data"]["data/objects.json"]["objects"]
        }
        for object_id, expected in STRUCTURES.items():
            lifecycle = objects[object_id]["presentation"]["buildingLifecycle"]
            self.assertEqual(lifecycle["schema"], SCHEMA)
            self.assertEqual(lifecycle["schemaVersion"], 0)
            self.assertEqual(
                (
                    lifecycle["maxHealth"],
                    lifecycle["damagedHealth"],
                    lifecycle["reallyDamagedHealth"],
                ),
                expected["health"],
            )
            root = f"assets/models/structures/{expected['slug']}"
            expected_paths = {
                "construction": f"{root}/construction.glb",
                "intact": f"{root}/intact.glb",
                "damaged": f"{root}/damaged.glb",
                "reallyDamaged": f"{root}/really-damaged.glb",
                "rubble": f"{root}/rubble.glb",
                "bib": f"{root}/bib.glb",
            }
            self.assertEqual(lifecycle["paths"], expected_paths)
            self.assertEqual(
                objects[object_id]["presentation"]["model"],
                lifecycle["paths"]["intact"],
            )
            self.assertFalse(lifecycle["bibDuringConstruction"])
            self.assertEqual(
                {key: value["names"] for key, value in lifecycle["clips"].items()},
                expected["clips"],
            )
            self.assertTrue(lifecycle["unresolved"])

    def test_exact_model_hierarchy_and_clip_groups(self) -> None:
        profile = ImportProfile.load(PROFILE_PATH)
        resources = {resource.id: resource for resource in profile.resources}
        self.assertEqual(len(SOURCE_GROUPS), 30)
        for resource_id, expected in SOURCE_GROUPS.items():
            converter, model, basenames, animations = expected
            resource = resources[resource_id]
            self.assertEqual(resource.converter, converter)
            self.assertEqual(resource.options["model"], model)
            self.assertEqual(
                [Path(pattern).name for pattern in resource.patterns], basenames
            )
            self.assertEqual(resource.options.get("animations", []), animations)
            self.assertEqual(resource.expected_count, len(basenames))
            self.assertEqual(resource.limit, len(basenames))

        damaged = resources["men-fortress-damaged-model"]
        self.assertEqual(
            damaged.options["sourceVariantOf"],
            "men-fortress-intact-model",
        )
        self.assertEqual(
            damaged.options["textureOverrides"],
            [
                {
                    "authored": "gbfortress1.tga",
                    "target": "gbfortress1.dds",
                    "source": "gbfortress1d.dds",
                }
            ],
        )

        root_rigid_bibs = {
            resource_id
            for resource_id in SOURCE_GROUPS
            if resources[resource_id].options.get("provenRootRigidBake") is True
        }
        self.assertEqual(
            root_rigid_bibs,
            {"men-farm-bib-model", "men-barracks-bib-model"},
        )

    def test_no_construction_model_backs_an_intact_output(self) -> None:
        profile = ImportProfile.load(PROFILE_PATH)
        intact_resources = [
            resource
            for resource in profile.resources
            if resource.output
            and resource.output.startswith("assets/models/structures/men-")
            and resource.output.endswith("/intact.glb")
        ]
        self.assertEqual(len(intact_resources), 5)
        for resource in intact_resources:
            self.assertFalse(resource.options["model"].casefold().endswith("_a.w3d"))

    def test_each_model_stages_its_complete_exact_texture_closure(self) -> None:
        profile = ImportProfile.load(PROFILE_PATH)
        resources = {resource.id: resource for resource in profile.resources}
        ownership: dict[str, list[str]] = {}
        for resource in profile.resources:
            for pattern in resource.patterns:
                ownership.setdefault(pattern.casefold(), []).append(resource.id)

        for required in MODEL_TEXTURES.values():
            for relative in required:
                virtual_path = f"art/compiledtextures/{relative}"
                self.assertEqual(
                    len(ownership.get(virtual_path.casefold(), [])),
                    1,
                    virtual_path,
                )

        for resource_id in SOURCE_GROUPS:
            resource = resources[resource_id]
            model = resource.options["model"]
            dependencies = [
                resources[dependency_id]
                for dependency_id in resource.options["inputResourceIds"]
            ]
            staged = {
                pattern.casefold()
                for dependency in dependencies
                for pattern in dependency.patterns
            }
            required = {
                f"art/compiledtextures/{relative}".casefold()
                for relative in MODEL_TEXTURES[model]
            }
            self.assertTrue(required <= staged, f"{resource_id}: {required - staged}")

    def test_door_sources_use_proven_import_once_conversions(self) -> None:
        profile = ImportProfile.load(PROFILE_PATH)
        resources = {resource.id: resource for resource in profile.resources}
        payload = load_payload()
        fortress = next(
            item
            for item in payload["runtime_data"]["data/objects.json"]["objects"]
            if item["id"] == "bfme2.object.men-fortress"
        )
        door = fortress["presentation"]["buildingLifecycle"]["components"]["door"]
        expected = {
            "closed": (
                "men-fortress-door-closed-source",
                "art/w3d/gb/gbfdoor_drc.w3d",
                "assets/models/structures/men-fortress/door-closed.glb",
            ),
            "construction": (
                "men-fortress-door-construction-source",
                "art/w3d/gb/gbfdoor_a.w3d",
                "assets/models/structures/men-fortress/door-construction.glb",
            ),
            "rubble": (
                "men-fortress-door-rubble-source",
                "art/w3d/gb/gbfdoor_d3.w3d",
                "assets/models/structures/men-fortress/door-rubble.glb",
            ),
        }
        for state, (resource_id, source, output) in expected.items():
            record = door[state]
            self.assertEqual(record["resourceId"], resource_id)
            self.assertEqual(record["source"], source)
            self.assertEqual(record["path"], output)
            self.assertEqual(record["status"], "ready")
            self.assertNotIn("unresolvedCode", record)
            resource = resources[resource_id]
            self.assertEqual(resource.converter, "w3d-bundle")
            self.assertEqual(resource.output, output)
            self.assertEqual(resource.patterns, (source,))
            self.assertEqual(resource.options["model"], Path(source).name)
            self.assertEqual(resource.options["animations"], [Path(source).name])
            self.assertEqual(
                resource.options["inputResourceIds"],
                ["men-fortress-material-textures"],
            )

        building_patterns = {
            pattern.casefold()
            for resource in profile.resources
            if resource.id.startswith(
                (
                    "men-fortress-",
                    "men-farm-",
                    "men-barracks-",
                    "men-archery-range-",
                    "men-stable-",
                    "men-structure-",
                )
            )
            for pattern in resource.patterns
        }
        self.assertFalse(any("snow" in pattern for pattern in building_patterns))
        self.assertFalse(any("gbfdoor_dro" in pattern for pattern in building_patterns))


if __name__ == "__main__":
    unittest.main()
