from __future__ import annotations

import json
from pathlib import Path, PurePosixPath
import tempfile
import unittest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.map_profile import (
    FIVE_MAP_TARGETS,
    GOLLUM_SPAWN_LIBRARY_PATH,
    _display_name,
    _gollum_spawn_library_referenced,
    _slug,
    build_five_map_profile,
    build_skirmish_map_profile,
    classify_map_directory,
    discover_registry_map_targets,
)
from openbfme_importer.profile import ImportProfile, resolve_profile

from importer.tests.test_big import make_big
from importer.tests.test_map_census import _encoded_path
from importer.tests.test_sage_map import _synthetic_map


def test_gollum_spawn_library_is_selected_only_for_authored_map_reference() -> None:
    referenced = {
        "libraryMapLists": [
            {
                "references": [
                    {
                        "normalized": "Libraries/Lib_GollumSpawn/Lib_GollumSpawn.map"
                    }
                ]
            }
        ]
    }
    inline_only = {"libraryMapLists": [{"references": []}]}

    assert _gollum_spawn_library_referenced(referenced)
    assert not _gollum_spawn_library_referenced(inline_only)
    assert GOLLUM_SPAWN_LIBRARY_PATH == (
        "libraries/lib_gollumspawn/lib_gollumspawn.map"
    )


def _catalog(root: Path, *, omit_preview: bool = False) -> InstallCatalog:
    source, _ = _synthetic_map()
    entries: dict[str, bytes] = {
        "data/ini/terrain.ini": b"Terrain TestGrass\n Texture = testgrass.tga\nEnd\n",
        "art/terrain/testgrass.tga": b"synthetic-tga",
        "libraries/multiplayer_start_teams/multiplayer_start_teams.map": source,
        "libraries/ai_initialize/ai_initialize.map": source,
        "libraries/ai_mp_inherit_management/ai_mp_inherit_management.map": source,
        "libraries/multiplayer_human/multiplayer_human.map": source,
    }
    for index, target in enumerate(FIVE_MAP_TARGETS):
        path = PurePosixPath(target.virtual_path)
        entries[target.virtual_path] = source
        entries[(path.parent / "map.ini").as_posix()] = f"map-{index}".encode()
        entries[(path.parent / f"{path.stem}_art.tga").as_posix()] = b"art"
        if not (omit_preview and index == 0):
            entries[(path.parent / f"{path.stem}_pic.tga").as_posix()] = b"preview"
    make_big(root / "maps.big", entries)
    return InstallCatalog.build(root)


class FiveMapProfileTests(unittest.TestCase):
    def test_script_composite_generated_profile_is_deterministic_and_schema_valid(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            catalog = _catalog(root)
            first = build_five_map_profile(catalog)
            second = build_five_map_profile(catalog)
            self.assertEqual(
                json.dumps(first, sort_keys=True), json.dumps(second, sort_keys=True)
            )
            profile_path = root / "profile.json"
            profile_path.write_text(
                json.dumps(first, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
            loaded = ImportProfile.load(profile_path)
            resolved = resolve_profile(loaded, catalog)

        self.assertFalse(resolved.missing_required)
        self.assertEqual(len(first["resources"]), 22)
        self.assertEqual(len(first["runtime_data"]["data/maps.json"]["maps"]), 5)
        self.assertEqual(
            [row["playerCount"] for row in first["runtime_data"]["data/maps.json"]["maps"]],
            [1, 1, 1, 1, 1],
        )
        map_resources = [
            row for row in first["resources"] if row["converter"] == "sage-map"
        ]
        self.assertEqual(len(map_resources), 5)
        script_resources = [
            row
            for row in first["resources"]
            if row["converter"] == "sage-script-composite"
        ]
        self.assertEqual(len(script_resources), 1)
        self.assertEqual(
            script_resources[0]["options"]["mapVirtualPath"],
            "maps/map mp fords of isen ii/map mp fords of isen ii.map",
        )
        self.assertTrue(
            all(
                row["patterns"][1:]
                == [
                    "libraries/multiplayer_start_teams/multiplayer_start_teams.map",
                    "libraries/ai_initialize/ai_initialize.map",
                    "libraries/ai_mp_inherit_management/ai_mp_inherit_management.map",
                    "libraries/multiplayer_human/multiplayer_human.map",
                ]
                for row in script_resources
            )
        )
        self.assertTrue(
            all(row["options"]["expected"]["terrainTextureCount"] == 1 for row in map_resources)
        )
        terrain_resources = [
            row
            for row in first["resources"]
            if row["converter"] == "sage-terrain-materials"
        ]
        self.assertEqual(len(terrain_resources), 1)
        self.assertEqual(terrain_resources[0]["patterns"], [
            "data/ini/terrain.ini",
            "art/terrain/testgrass.tga",
        ])

    def test_absent_optional_companion_is_omitted_never_substituted(self) -> None:
        # Only the map binary is required. 24 of the 68 BFME2 map directories
        # and 60 of the 179 RotWK ones ship no ``_pic.tga`` at all, so a missing
        # preview drops its resource and its catalog field instead of rejecting
        # the map or pointing at another map's art.
        with tempfile.TemporaryDirectory() as raw:
            profile = build_five_map_profile(_catalog(Path(raw), omit_preview=True))

        self.assertEqual(len(profile["resources"]), 21)
        rows = profile["runtime_data"]["data/maps.json"]["maps"]
        without_preview = [row for row in rows if "preview" not in row]
        self.assertEqual(len(without_preview), 1)
        self.assertEqual(len(rows), 5)
        previews = {
            row["output"]
            for row in profile["resources"]
            if str(row["id"]).endswith("-preview")
        }
        self.assertNotIn(
            f"assets/ui/maps/{FIVE_MAP_TARGETS[0].slug}-preview.png", previews
        )


def _mapcache_record(path: str, *, players: int, official: bool = True) -> str:
    return "\n".join(
        [
            f"MapCache {_encoded_path(path)}",
            "  isMultiplayer = Yes",
            f"  isOfficial = {'Yes' if official else 'No'}",
            "  isScenarioMP = No",
            f"  numPlayers = {players}",
            "End",
        ]
    )


def _skirmish_catalog(root: Path, *, drop_preview_for: str = "") -> InstallCatalog:
    """An install whose registry advertises four maps, one of them stale."""

    source, _ = _synthetic_map()
    directories = {
        "map mp alpha vale": 1,
        "map mp bravo ridge ii": 1,
        "map wor scenario keep": 1,
    }
    entries: dict[str, bytes] = {
        "data/ini/terrain.ini": b"Terrain TestGrass\n Texture = testgrass.tga\nEnd\n",
        "art/terrain/testgrass.tga": b"synthetic-tga",
        "libraries/multiplayer_start_teams/multiplayer_start_teams.map": source,
        "libraries/ai_initialize/ai_initialize.map": source,
        "libraries/ai_mp_inherit_management/ai_mp_inherit_management.map": source,
        "libraries/multiplayer_human/multiplayer_human.map": source,
    }
    records = [
        # Advertised but never shipped: the registry outlives its payload.
        _mapcache_record("maps/map mp ghost hollow/map mp ghost hollow.map", players=1)
    ]
    for directory, players in directories.items():
        stem = directory
        base = f"maps/{directory}/{stem}"
        entries[f"{base}.map"] = source
        entries[f"maps/{directory}/map.ini"] = b"[map]"
        entries[f"{base}_art.tga"] = b"art"
        if directory != drop_preview_for:
            entries[f"{base}_pic.tga"] = b"preview"
        records.append(_mapcache_record(f"{base}.map", players=players))
    entries["maps/mapcache.ini"] = ("\n".join(records) + "\n").encode("cp1252")
    make_big(root / "maps.big", entries)
    return InstallCatalog.build(root)


def _encoded_display_key(key: str) -> str:
    """Escape a string-table key the way retail's mapcache displayName does."""

    return "".join(f"_{byte:02X}" for byte in key.encode("utf-16-le"))


def _authored_names_catalog(root: Path) -> InstallCatalog:
    """An install whose registry authors a name the directory gets wrong."""

    source, _ = _synthetic_map()
    entries: dict[str, bytes] = {
        "data/ini/terrain.ini": b"Terrain TestGrass\n Texture = testgrass.tga\nEnd\n",
        "art/terrain/testgrass.tga": b"synthetic-tga",
        "libraries/multiplayer_start_teams/multiplayer_start_teams.map": source,
        "libraries/ai_initialize/ai_initialize.map": source,
        "libraries/ai_mp_inherit_management/ai_mp_inherit_management.map": source,
        "libraries/multiplayer_human/multiplayer_human.map": source,
        "data/lotr.str": b'Map:MAPMPFallBack4p\n "Stonewain Valley"\nEND\n',
    }
    records = []
    for directory, display_key in (
        ("map mp fall back 4p", "Map:MAPMPFallBack4p"),
        # No displayName key: the derived fallback must be recorded, not silent.
        ("map mp bravo ridge", ""),
    ):
        base = f"maps/{directory}/{directory}"
        entries[f"{base}.map"] = source
        entries[f"{base}_art.tga"] = b"art"
        entries[f"{base}_pic.tga"] = b"preview"
        record = _mapcache_record(f"{base}.map", players=1)
        if display_key:
            head, tail = record.split("\n", 1)
            record = "\n".join(
                [head, f"  displayName = {_encoded_display_key(display_key)}", tail]
            )
        records.append(record)
    entries["maps/mapcache.ini"] = ("\n".join(records) + "\n").encode("cp1252")
    make_big(root / "maps.big", entries)
    return InstallCatalog.build(root)


class FiveMapWrapperIdentityTests(unittest.TestCase):
    # This test was previously nested (dead) inside a module-level helper and
    # never collected; restored to an actual TestCase.
    def test_five_map_wrapper_keeps_its_historical_identity(self) -> None:
        # Carried over from the phase0 lane through the map-profile rewrite: the
        # legacy five-map pack's identity is depended on by existing generated
        # profiles on disk, so the wrapper is not free to drift even though the
        # builder underneath it was replaced.
        with tempfile.TemporaryDirectory() as raw:
            profile = build_five_map_profile(_catalog(Path(raw)))
        self.assertEqual("bfme2-five-maps-106-generated", profile["id"])
        self.assertEqual("bfme2-five-maps-106-private", profile["pack"]["id"])
        self.assertEqual(
            "maps/fords-of-isen-ii/map.json", profile["pack"]["files"]["entryMap"]
        )
        materials = [
            row
            for row in profile["resources"]
            if row["converter"] == "sage-terrain-materials"
        ]
        self.assertEqual("assets/terrain/five-maps", materials[0]["output"])


class SkirmishMapDiscoveryTests(unittest.TestCase):
    def test_slug_and_display_name_strip_the_retail_directory_prefix(self) -> None:
        self.assertEqual(_slug("map mp fords of isen ii"), "fords-of-isen-ii")
        self.assertEqual(_display_name("map mp fords of isen ii"), "Fords of Isen II")
        self.assertEqual(_display_name("map wor mount doom"), "Mount Doom")
        self.assertEqual(_display_name("map mp grey mountains"), "Grey Mountains")
        # Only the skirmish set keeps a bare slug, because retail reuses map
        # names across categories: BFME2 ships good/evil/wor Erebor, and RotWK
        # ships both ``map ang fornost`` and ``map wor ang fornost``.
        self.assertEqual(_slug("map wor mount doom"), "wor-mount-doom")
        self.assertEqual(_slug("map good erebor"), "good-erebor")
        self.assertEqual(_slug("map evil erebor"), "evil-erebor")
        self.assertEqual(_slug("map ang fornost"), "ang-fornost")
        self.assertEqual(_slug("map wor ang fornost"), "wor-ang-fornost")
        self.assertEqual(_slug("cin fornost - witchking fire"), "cin-fornost-witchking-fire")
        self.assertEqual(_slug("map advanced tutorial"), "advanced-tutorial")

    def test_every_retail_directory_family_classifies(self) -> None:
        self.assertEqual(classify_map_directory("map mp evendim"), "skirmish")
        self.assertEqual(classify_map_directory("map wor rivendell"), "wotr-battle")
        self.assertEqual(classify_map_directory("map wor ang fornost"), "wotr-battle")
        self.assertEqual(classify_map_directory("map good erebor"), "campaign")
        self.assertEqual(classify_map_directory("map evil shire"), "campaign")
        self.assertEqual(classify_map_directory("map ang carn dum"), "campaign")
        self.assertEqual(
            classify_map_directory("cin barrow downs - sorcerer"), "cinematic"
        )
        self.assertEqual(classify_map_directory("map beginner tutorial"), "tutorial")
        self.assertEqual(classify_map_directory("shellmap1"), "shell")
        self.assertEqual(classify_map_directory("createahero"), "system")

    def test_discovery_takes_the_map_set_from_the_registry_and_records_stale_rows(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as raw:
            catalog = _skirmish_catalog(Path(raw))
            targets, rejections = discover_registry_map_targets(catalog)

        # Only the multiplayer directories, never the War-of-the-Ring one.
        self.assertEqual([target.slug for target in targets], ["alpha-vale", "bravo-ridge-ii"])
        self.assertEqual([target.registry_player_count for target in targets], [1, 1])
        self.assertEqual(
            [(row["virtualPath"], row["status"]) for row in rejections],
            [
                (
                    "maps/map mp ghost hollow/map mp ghost hollow.map",
                    "registry-stale-missing-payload",
                )
            ],
        )

    def test_missing_preview_keeps_the_map_and_drops_only_its_preview(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            catalog = _skirmish_catalog(Path(raw), drop_preview_for="map mp alpha vale")
            targets, rejections = discover_registry_map_targets(catalog)
            profile = build_skirmish_map_profile(catalog)

        self.assertEqual(
            [target.slug for target in targets], ["alpha-vale", "bravo-ridge-ii"]
        )
        self.assertNotIn("missing-required-companion", {row["status"] for row in rejections})
        rows = {
            str(row["id"]): row
            for row in profile["runtime_data"]["data/maps.json"]["maps"]
        }
        self.assertNotIn("preview", rows["bfme2.map.alpha-vale"])
        self.assertIn("preview", rows["bfme2.map.bravo-ridge-ii"])

    def test_non_skirmish_categories_are_discoverable_and_labelled(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            catalog = _skirmish_catalog(Path(raw))
            wotr, _ = discover_registry_map_targets(
                catalog, categories=("wotr-battle",)
            )

        # The WOTR battle map is registered isMultiplayer and was previously
        # invisible only because discovery filtered on the ``map mp`` prefix.
        self.assertEqual([target.slug for target in wotr], ["wor-scenario-keep"])
        self.assertEqual([target.category for target in wotr], ["wotr-battle"])

    def test_skirmish_profile_publishes_a_catalog_with_authored_player_counts(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            catalog = _skirmish_catalog(root)
            first = build_skirmish_map_profile(catalog)
            second = build_skirmish_map_profile(catalog)
            self.assertEqual(
                json.dumps(first, sort_keys=True), json.dumps(second, sort_keys=True)
            )
            profile_path = root / "skirmish.json"
            profile_path.write_text(
                json.dumps(first, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
            resolved = resolve_profile(ImportProfile.load(profile_path), catalog)

        self.assertFalse(resolved.missing_required)
        rows = first["runtime_data"]["data/maps.json"]["maps"]
        self.assertEqual(
            [row["id"] for row in rows],
            ["bfme2.map.alpha-vale", "bfme2.map.bravo-ridge-ii"],
        )
        script_resources = [
            row
            for row in first["resources"]
            if row["converter"] == "sage-script-composite"
        ]
        self.assertEqual(len(script_resources), 2)
        self.assertEqual(
            [row["output"] for row in script_resources],
            [
                "maps/alpha-vale/scripts.json",
                "maps/bravo-ridge-ii/scripts.json",
            ],
        )
        self.assertEqual([row["displayName"] for row in rows], ["Alpha Vale", "Bravo Ridge II"])
        self.assertEqual([row["playerCount"] for row in rows], [1, 1])
        self.assertEqual([row["registryPlayerCount"] for row in rows], [1, 1])
        self.assertEqual(first["pack"]["files"]["mapCatalog"], "data/maps.json")
        self.assertEqual(
            first["pack"]["files"]["entryMap"], "maps/alpha-vale/map.json"
        )
        # Every map's still-unbound prop types are recorded, never substituted.
        evidence = first["planning_evidence"]
        self.assertEqual(
            sorted(evidence["unboundObjectTypes"]), ["alpha-vale", "bravo-ridge-ii"]
        )
        self.assertEqual(
            [row["status"] for row in evidence["rejectedMaps"]],
            ["registry-stale-missing-payload"],
        )

    def test_scripts_rotwk_skirmish_profile_uses_rotwk_map_identifiers(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            profile = build_skirmish_map_profile(
                _skirmish_catalog(Path(raw)),
                game="rotwk",
            )

        self.assertEqual(
            [
                row["id"]
                for row in profile["runtime_data"]["data/maps.json"]["maps"]
            ],
            ["rotwk.map.alpha-vale", "rotwk.map.bravo-ridge-ii"],
        )

    def test_authored_display_names_win_and_provenance_is_recorded(self) -> None:
        # Retail authors "Stonewain Valley" for the directory ``map mp fall
        # back 4p``; title-casing the directory is not merely ugly, it is
        # WRONG, and a fresh cook that reintroduces it must fail this test.
        with tempfile.TemporaryDirectory() as raw:
            catalog = _authored_names_catalog(Path(raw))
            targets, _ = discover_registry_map_targets(catalog)
            profile = build_skirmish_map_profile(catalog)

        self.assertEqual(
            [(target.slug, target.display_name, target.display_name_source) for target in targets],
            [
                ("bravo-ridge", "Bravo Ridge", "derived-from-directory"),
                ("fall-back-4p", "Stonewain Valley", "authored"),
            ],
        )
        rows = profile["runtime_data"]["data/maps.json"]["maps"]
        self.assertEqual(
            [row["displayName"] for row in rows],
            ["Bravo Ridge", "Stonewain Valley"],
        )
        # The catalog can never quietly ship a directory-derived name: the
        # planning evidence names every one, and the official corpus gate
        # asserts the count is zero.
        source = profile["planning_evidence"]["displayNameSource"]
        self.assertEqual(source["authoredCount"], 1)
        self.assertEqual(source["derivedFromDirectoryCount"], 1)
        self.assertEqual(source["derivedFromDirectorySlugs"], ["bravo-ridge"])

    def test_profile_without_string_table_records_every_derived_name(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            profile = build_skirmish_map_profile(_skirmish_catalog(Path(raw)))

        source = profile["planning_evidence"]["displayNameSource"]
        self.assertEqual(source["authoredCount"], 0)
        self.assertEqual(source["derivedFromDirectoryCount"], 2)
        self.assertEqual(
            source["derivedFromDirectorySlugs"], ["alpha-vale", "bravo-ridge-ii"]
        )

    def test_registry_player_count_disagreement_rejects_the_map(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            catalog = _skirmish_catalog(root)
            targets, _ = discover_registry_map_targets(catalog)
            lying = (
                targets[0].__class__(
                    targets[0].slug,
                    targets[0].display_name,
                    targets[0].virtual_path,
                    7,
                ),
            )
            from openbfme_importer.map_profile import build_map_profile

            with self.assertRaisesRegex(ValueError, "registry player count"):
                build_map_profile(
                    catalog,
                    lying,
                    profile_id="p",
                    title="t",
                    pack_id="pack",
                    pack_version="v0",
                    terrain_output="assets/terrain/x",
                )


if __name__ == "__main__":
    unittest.main()
