from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from openbfme_importer.profile import ImportProfile
from openbfme_importer.retail_road_profile import (
    ROAD_MATERIALS_RUNTIME_PATH,
    RUNTIME_SCHEMA,
    build_road_profile,
)


FORDS_ROADS = (
    (
        "Footprints",
        "TRDirtRoad.tga",
        "art/compiledtextures/tr/trdirtroad.dds",
        "1d170b133aee2164eba79ed2f163264ec9222536c6701bb2f485a1019895b50b",
    ),
    (
        "FtPrintDrkGr02",
        "TRFtPrntDrkSing.tga",
        "art/compiledtextures/tr/trftprntdrksing.dds",
        "5eac2bcf50a9938e93f0489c7e4241b864f808834c9f4b93c5fe839074f6d9ab",
    ),
    (
        "FtPrintGrass02",
        "TRFtPrntGrssSing.tga",
        "art/compiledtextures/tr/trftprntgrsssing.dds",
        "f48bef174eaf407d75e5035a721521cb41538234f420a139fec9522a326e505d",
    ),
    (
        "FtprintsDrk",
        "TRFootPrintDark02.tga",
        "art/compiledtextures/tr/trfootprintdark02.dds",
        "c7f29c05ead25b574e7f90a42497a73be5a9123842b4036a13823309dab5acfa",
    ),
    (
        "FtprintsDrk02",
        "TRFootPrintDarkSing.tga",
        "art/compiledtextures/tr/trfootprintdarksing.dds",
        "196ce440a59a42c7f6b102e72b789c33ce4871f67ba2a83a8ee7e8134bba1e09",
    ),
)


def _canonical_sha256(value: object) -> str:
    payload = json.dumps(
        value,
        sort_keys=True,
        ensure_ascii=False,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def _rehash(report: dict[str, object]) -> dict[str, object]:
    report.pop("aggregateSha256", None)
    report["aggregateSha256"] = _canonical_sha256(report)
    return report


def _base_profile() -> dict[str, object]:
    return {
        "format": 1,
        "id": "base-profile",
        "title": "Preserve this title exactly",
        "pack": {
            "id": "base-pack",
            "version": "1.06-test",
            "schema": "openbfme.content-pack",
            "schemaVersion": 0,
            "custom": {"preserved": [1, "two", False]},
        },
        "resources": [
            {
                "id": "fords-map",
                "kind": "map",
                "converter": "sage-map",
                "patterns": ["maps/map mp fords of isen ii/map.map"],
                "output": "maps/fords-of-isen-ii",
                "limit": 1,
                "expected_count": 1,
                "options": {
                    "metadata": {
                        "id": "bfme2.map.fords-of-isen-ii",
                        "displayName": "Fords of Isen II",
                    },
                    "expected": {"objectCount": 1526},
                },
            },
            {
                "id": "base-data",
                "kind": "data",
                "converter": "hash-only",
                "patterns": ["data/base.ini"],
                "limit": 1,
                "expected_count": 1,
            },
        ],
        "runtime_data": {
            "data/base.json": {
                "schema": "openbfme.test",
                "schemaVersion": 0,
                "preserved": True,
            }
        },
        "unknownPreservedField": {"yes": "exactly"},
    }


def _road_record(
    road_id: str,
    identifier: str,
    source_path: str,
    sha256: str,
) -> dict[str, object]:
    return {
        "requestedId": road_id,
        "id": road_id,
        "status": "resolved",
        "provenance": {
            "virtualPath": "data/ini/roads.ini",
            "line": 1,
            "endLine": 5,
        },
        "fields": {
            "Texture": {
                "authoredKey": "Texture",
                "authoredValue": identifier,
                "provenance": {"virtualPath": "data/ini/roads.ini", "line": 2},
            },
            "RoadWidth": {
                "authoredKey": "RoadWidth",
                "authoredValue": "52",
                "normalizedValue": "52",
                "provenance": {"virtualPath": "data/ini/roads.ini", "line": 3},
            },
            "RoadWidthInTexture": {
                "authoredKey": "RoadWidthInTexture",
                "authoredValue": ".95",
                "normalizedValue": "0.95",
                "provenance": {"virtualPath": "data/ini/roads.ini", "line": 4},
            },
        },
        "textureLeaf": {
            "identifier": identifier,
            "status": "resolved",
            "physicalVirtualPath": source_path,
            "byteLength": 349680,
            "sha256": sha256,
            "evidence": [
                "sage-road-compiled-texture:exact-tga-stem-to-dds",
                "texture:case-insensitive-exact-basename-extensionless-stem",
            ],
            "provenance": {
                "virtualPath": "data/ini/roads.ini",
                "line": 2,
                "field": "Texture",
            },
        },
    }


def _report(rows: tuple[tuple[str, str, str, str], ...] = FORDS_ROADS) -> dict[str, object]:
    roads = [_road_record(*row) for row in rows]
    count = len(roads)
    report: dict[str, object] = {
        "schema": "openbfme.retail-road-closure",
        "schemaVersion": 1,
        "source": {
            "virtualPath": "data/ini/roads.ini",
            "byteLength": 12876,
            "sha256": "02dc8ef9b2f0a2088b4781c224606a54a3d06c3fd8bb38dd900d3d3c2d72a501",
        },
        "resolutionPolicy": {
            "roadId": "case-insensitive-exact-unique-definition",
            "texture": "exact-filename-or-stem",
            "representationBridge": "absent-authored-tga-to-unique-exact-stem-dds-only",
        },
        "catalog": {
            "assetPathCount": 40130,
            "visualPathCount": 6749,
            "roadDefinitionCount": 74,
        },
        "roads": roads,
        "diagnostics": [],
        "summary": {
            "targetCount": count,
            "resolvedRoadCount": count,
            "resolvedTextureCount": count,
            "missingDefinitionCount": 0,
            "ambiguousDefinitionCount": 0,
            "invalidDefinitionCount": 0,
            "unresolvedTextureCount": 0,
            "gapCount": 0,
            "ready": True,
        },
    }
    return _rehash(report)


def _write(path: Path, value: object) -> None:
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


class RetailRoadProfileTests(unittest.TestCase):
    def _paths(
        self,
        root: Path,
        *,
        base: dict[str, object] | None = None,
        report: dict[str, object] | None = None,
    ) -> tuple[Path, Path]:
        base_path = root / "base.json"
        report_path = root / "report.json"
        _write(base_path, _base_profile() if base is None else base)
        _write(report_path, _report() if report is None else report)
        return base_path, report_path

    def test_real_shaped_fords_profile_is_deterministic_complete_and_valid(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            base_path, report_path = self._paths(root)
            expected_ids = [row[0] for row in reversed(FORDS_ROADS)]
            first = build_road_profile(
                base_path,
                report_path,
                profile_id="base-profile-with-roads",
                pack_id="base-pack-with-roads",
                expected_road_ids=expected_ids,
            )
            second = build_road_profile(
                base_path,
                report_path,
                profile_id="base-profile-with-roads",
                pack_id="base-pack-with-roads",
                expected_road_ids=list(reversed(expected_ids)),
            )
            self.assertEqual(first, second)
            self.assertEqual(first["id"], "base-profile-with-roads")
            self.assertEqual(first["pack"]["id"], "base-pack-with-roads")
            self.assertEqual(first["title"], "Preserve this title exactly")
            self.assertEqual(
                first["pack"]["custom"], {"preserved": [1, "two", False]}
            )
            self.assertEqual(
                first["unknownPreservedField"], {"yes": "exactly"}
            )

            resources = first["resources"]
            road_resources = [
                item
                for item in resources
                if item["id"].startswith("fords-road-texture-")
            ]
            self.assertEqual(len(resources), 7)
            self.assertEqual(len(road_resources), 5)
            self.assertTrue(
                all(
                    item["kind"] == "texture"
                    and item["converter"] == "texture"
                    and item["expected_count"] == 1
                    and item["output"].startswith(
                        "maps/fords-of-isen-ii/road-materials/textures/"
                    )
                    and item["output"].endswith(".png")
                    for item in road_resources
                )
            )
            self.assertEqual(
                {item["patterns"][0] for item in road_resources},
                {row[2] for row in FORDS_ROADS},
            )

            runtime = first["runtime_data"][ROAD_MATERIALS_RUNTIME_PATH]
            self.assertEqual(runtime["schema"], RUNTIME_SCHEMA)
            self.assertEqual(runtime["schemaVersion"], 0)
            self.assertEqual(runtime["roadCount"], 5)
            self.assertEqual(
                [item["id"] for item in runtime["roads"]],
                sorted([row[0] for row in FORDS_ROADS], key=str.casefold),
            )
            self.assertTrue(
                all(
                    item["RoadWidth"] == "52"
                    and item["RoadWidthInTexture"] == "0.95"
                    and item["sourceByteLength"] == 349680
                    and item["texturePng"].endswith(".png")
                    for item in runtime["roads"]
                )
            )
            map_resource = next(
                item for item in resources if item["id"] == "fords-map"
            )
            self.assertEqual(
                map_resource["options"]["metadata"]["roadMaterials"],
                "road-materials.json",
            )

            derived_path = root / "derived.json"
            _write(derived_path, first)
            loaded = ImportProfile.load(derived_path)
            self.assertEqual(loaded.id, "base-profile-with-roads")
            self.assertEqual(loaded.pack_id, "base-pack-with-roads")
            self.assertEqual(len(loaded.resources), 7)

    def test_ids_are_preserved_unless_explicitly_changed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            one = (FORDS_ROADS[0],)
            base_path, report_path = self._paths(root, report=_report(one))
            result = build_road_profile(base_path, report_path)
            self.assertEqual(result["id"], "base-profile")
            self.assertEqual(result["pack"]["id"], "base-pack")
            self.assertEqual(result["runtime_data"][ROAD_MATERIALS_RUNTIME_PATH]["roadCount"], 1)

    def test_shared_exact_dds_gets_one_resource_without_losing_road_records(self) -> None:
        shared = (
            FORDS_ROADS[0],
            (
                "SecondRoad",
                "SecondAuthoredName.tga",
                FORDS_ROADS[0][2],
                FORDS_ROADS[0][3],
            ),
        )
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            base_path, report_path = self._paths(root, report=_report(shared))
            result = build_road_profile(base_path, report_path)
            added = result["resources"][2:]
            runtime = result["runtime_data"][ROAD_MATERIALS_RUNTIME_PATH]
            self.assertEqual(len(added), 1)
            self.assertEqual(runtime["roadCount"], 2)
            self.assertEqual(
                {item["texturePng"] for item in runtime["roads"]},
                {added[0]["output"]},
            )

    def test_schema_digest_count_and_gap_inconsistencies_fail_closed(self) -> None:
        cases: list[tuple[str, dict[str, object], str]] = []
        bad_schema = _report()
        bad_schema["schema"] = "openbfme.wrong"
        _rehash(bad_schema)
        cases.append(("schema", bad_schema, "schema"))
        bad_digest = _report()
        bad_digest["aggregateSha256"] = "0" * 64
        cases.append(("digest", bad_digest, "digest mismatch"))
        bad_count = _report()
        bad_count["summary"]["resolvedTextureCount"] = 4
        _rehash(bad_count)
        cases.append(("count", bad_count, "count mismatch"))
        bad_catalog_count = _report()
        bad_catalog_count["catalog"]["roadDefinitionCount"] = 4
        _rehash(bad_catalog_count)
        cases.append(
            (
                "catalog-count",
                bad_catalog_count,
                "definition count is inconsistent",
            )
        )
        gap = _report()
        gap["diagnostics"] = [{"code": "gap"}]
        gap["summary"]["gapCount"] = 1
        gap["summary"]["ready"] = False
        _rehash(gap)
        cases.append(("gap", gap, "conversion gaps"))

        for name, report, error in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as raw:
                base_path, report_path = self._paths(Path(raw), report=report)
                with self.assertRaisesRegex(ValueError, error):
                    build_road_profile(base_path, report_path)

    def test_ids_paths_widths_and_source_facts_fail_closed(self) -> None:
        cases: list[tuple[str, dict[str, object], str]] = []
        duplicate_id = _report()
        duplicate_id["roads"][1]["id"] = "footprints"
        duplicate_id["roads"][1]["requestedId"] = "footprints"
        _rehash(duplicate_id)
        cases.append(("duplicate-id", duplicate_id, "duplicate or case-ambiguous Road id"))

        ambiguous_path = _report()
        ambiguous_path["roads"][1]["textureLeaf"]["physicalVirtualPath"] = (
            FORDS_ROADS[0][2].upper()
        )
        _rehash(ambiguous_path)
        cases.append(("ambiguous-path", ambiguous_path, "case-ambiguous Road texture paths"))

        unsafe_path = _report()
        unsafe_path["roads"][0]["textureLeaf"]["physicalVirtualPath"] = "../escape.dds"
        _rehash(unsafe_path)
        cases.append(("unsafe-path", unsafe_path, "unsafe Road"))

        wildcard_path = _report()
        wildcard_path["roads"][0]["textureLeaf"]["physicalVirtualPath"] = "art/*.dds"
        _rehash(wildcard_path)
        cases.append(("wildcard-path", wildcard_path, "bounded relative path"))

        not_dds = _report()
        not_dds["roads"][0]["textureLeaf"]["physicalVirtualPath"] = "art/road.tga"
        _rehash(not_dds)
        cases.append(("not-dds", not_dds, "not an exact resolved DDS"))

        unsupported_width = _report()
        unsupported_width["roads"][0]["fields"]["RoadWidthInTexture"]["normalizedValue"] = "1.1"
        _rehash(unsupported_width)
        cases.append(("unsupported-width", unsupported_width, "outside the supported range"))

        inconsistent_width = _report()
        inconsistent_width["roads"][0]["fields"]["RoadWidth"]["authoredValue"] = "53"
        _rehash(inconsistent_width)
        cases.append(
            (
                "inconsistent-width",
                inconsistent_width,
                "authored and normalized values disagree",
            )
        )

        inconsistent_texture = _report()
        inconsistent_texture["roads"][0]["fields"]["Texture"]["authoredValue"] = "Other.tga"
        _rehash(inconsistent_texture)
        cases.append(("identifier", inconsistent_texture, "identifier is inconsistent"))

        inconsistent_facts = _report()
        inconsistent_facts["roads"][1]["textureLeaf"]["physicalVirtualPath"] = FORDS_ROADS[0][2]
        _rehash(inconsistent_facts)
        cases.append(("source-facts", inconsistent_facts, "texture facts disagree"))

        missing_bridge = _report()
        missing_bridge["roads"][0]["textureLeaf"]["evidence"] = [
            "texture:case-insensitive-exact-basename-extensionless-stem"
        ]
        _rehash(missing_bridge)
        cases.append(("missing-bridge", missing_bridge, "lacks the exact TGA-to-DDS"))

        for name, report, error in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as raw:
                base_path, report_path = self._paths(Path(raw), report=report)
                with self.assertRaisesRegex(ValueError, error):
                    build_road_profile(base_path, report_path)

    def test_output_resource_runtime_and_expected_id_collisions_fail_closed(self) -> None:
        same_basename_rows = (
            FORDS_ROADS[0],
            (
                "OtherRoad",
                "TRDirtRoad.tga",
                "art/other/trdirtroad.dds",
                "a" * 64,
            ),
        )
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            base_path, report_path = self._paths(
                root, report=_report(same_basename_rows)
            )
            with self.assertRaisesRegex(
                ValueError, "resource id collision|basenames collide"
            ):
                build_road_profile(base_path, report_path)

        collision_bases: list[tuple[str, dict[str, object], str]] = []
        resource_collision = _base_profile()
        resource_collision["resources"].append(
            {
                "id": "fords-road-texture-trdirtroad",
                "kind": "data",
                "converter": "hash-only",
                "patterns": ["data/collision.ini"],
                "limit": 1,
            }
        )
        collision_bases.append(("resource", resource_collision, "resource id collision"))
        output_collision = _base_profile()
        output_collision["resources"].append(
            {
                "id": "output-collision",
                "kind": "texture",
                "converter": "texture",
                "patterns": ["art/collision.dds"],
                "output": "maps/fords-of-isen-ii/road-materials/textures/trdirtroad.png",
                "limit": 1,
            }
        )
        collision_bases.append(("output", output_collision, "output collides"))
        runtime_collision = _base_profile()
        runtime_collision["runtime_data"][ROAD_MATERIALS_RUNTIME_PATH] = {}
        collision_bases.append(("runtime", runtime_collision, "runtime output collision"))

        for name, base, error in collision_bases:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as raw:
                base_path, report_path = self._paths(Path(raw), base=base)
                with self.assertRaisesRegex(ValueError, error):
                    build_road_profile(base_path, report_path)

        with tempfile.TemporaryDirectory() as raw:
            base_path, report_path = self._paths(Path(raw))
            wrong_case = [row[0] for row in FORDS_ROADS]
            wrong_case[0] = wrong_case[0].lower()
            with self.assertRaisesRegex(ValueError, "do not exactly match"):
                build_road_profile(
                    base_path, report_path, expected_road_ids=wrong_case
                )


if __name__ == "__main__":
    unittest.main()
