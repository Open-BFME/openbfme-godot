from __future__ import annotations

from contextlib import redirect_stdout
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

import openbfme_importer.map_corpus as map_corpus_module

from openbfme_importer.map_corpus import (
    MapCorpusError,
    MapCorpusLimitError,
    MapCorpusStrictError,
    scan_map_corpus,
)
from openbfme_importer.sage_map import SageMapError
from importer.tests.test_sage_map import (
    _dotnet_string,
    _record,
    _refpack_literals,
    _synthetic_map,
)


def _aggregate(files: list[dict[str, object]]) -> str:
    digest = hashlib.sha256()
    for item in files:
        digest.update(str(item["path"]).encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(item["size"]).encode("ascii"))
        digest.update(b"\0")
        digest.update(str(item["sha256"]).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _manifest(files: dict[str, bytes]) -> dict[str, object]:
    inventory: list[dict[str, object]] = []
    offset = 32
    for precedence, (path, payload) in enumerate(
        sorted(files.items(), key=lambda item: item[0].casefold())
    ):
        inventory.append(
            {
                "archive": "synthetic-fixture.big",
                "offset": offset,
                "path": path,
                "precedence": precedence,
                "sha256": hashlib.sha256(payload).hexdigest(),
                "size": len(payload),
            }
        )
        offset += len(payload)
    return {
        "schema": "openbfme.effective-assets-manifest",
        "schema_version": 0,
        "catalog": {
            "archive_count": 1,
            "entry_count": len(inventory),
            "format": 4,
            "identity_sha256": "a" * 64,
        },
        "install": {
            "identity_sha256": "b" * 64,
            "root": "synthetic-install",
        },
        "totals": {
            "bytes": sum(len(payload) for payload in files.values()),
            "files": len(files),
        },
        "aggregate_sha256": _aggregate(inventory),
        "files": inventory,
    }


def _write_manifest(root: Path, manifest: dict[str, object]) -> None:
    metadata = root / ".openbfme"
    metadata.mkdir(parents=True, exist_ok=True)
    (metadata / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def _write_corpus(root: Path, files: dict[str, bytes]) -> dict[str, object]:
    for relative, payload in files.items():
        target = root.joinpath(*relative.split("/"))
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
    manifest = _manifest(files)
    _write_manifest(root, manifest)
    return manifest


def _facts(
    source: bytes, *, accepted: bool = True, profile: str = "multiplayer"
) -> dict[str, object]:
    runnable = profile in {"multiplayer", "scenario"}
    return {
        "sourceBytes": len(source),
        "sourceSha256": hashlib.sha256(source).hexdigest(),
        "bodySha256": hashlib.sha256(b"body\0" + source).hexdigest(),
        "envelopeKind": "uncompressed",
        "chunks": [
            {
                "name": "HeightMapData",
                "version": 5,
                "occurrences": 1,
                "payloadBytes": len(source),
                "probeStatus": "parsed",
                "probeRejections": [],
            },
            {
                "name": "BlendTileData",
                "version": 18,
                "occurrences": 1,
                "payloadBytes": len(source),
                "probeStatus": "parsed",
                "probeRejections": [],
            },
            {
                "name": "PlayerScriptsList",
                "version": 1,
                "occurrences": 2,
                "payloadBytes": len(source),
                "probeStatus": "parsed" if accepted else "semantic-rejected",
                "probeRejections": [
                    {
                        "reason": "DO_NOT_EXPORT_SCRIPT_BODY",
                        "occurrences": 1,
                    }
                ],
            }
        ],
        "features": {
            "coordinates": [123.25, 456.5],
            "scripts": ["DO_NOT_EXPORT_SCRIPT_BODY"],
            "scriptListCount": 1,
        },
        "strictCook": {
            "mapKind": profile,
            "profileVersion": 1,
            "runnable": runnable,
            "structuralStatus": (
                "runnable-structure" if runnable else "non-runnable-structural-map"
            ),
            "accepted": accepted,
            "reason": None if accepted else "DO_NOT_EXPORT_REJECTION_TEXT",
        },
    }


def _contains_bytes(value: object) -> bool:
    if isinstance(value, (bytes, bytearray, memoryview)):
        return True
    if isinstance(value, dict):
        return any(
            _contains_bytes(key) or _contains_bytes(item) for key, item in value.items()
        )
    if isinstance(value, (list, tuple)):
        return any(_contains_bytes(item) for item in value)
    return False


def _fixture_census(
    source: bytes, *, profile: str = "multiplayer"
) -> dict[str, object]:
    return _facts(
        source,
        accepted=source != b"REJECTED_MAP_PAYLOAD",
        profile=profile,
    )


def _script_container_facts(
    source: bytes, *, profile: str = "multiplayer"
) -> dict[str, object]:
    facts = _facts(source, accepted=False, profile=profile)
    facts["chunks"] = [
        {
            "name": name,
            "version": 1,
            "occurrences": 1,
            "payloadBytes": 0,
            "probeStatus": "unclassified",
            "probeRejections": [],
        }
        for name in (
            "ScriptImportSize",
            "PlayerScriptsList",
            "NamedCameras",
            "CameraAnimationList",
            "ScriptsPlayers",
            "ScriptTeams",
            "ObjectsList",
            "TriggerAreas",
            "WaypointsList",
        )
    ]
    return facts


def _synthetic_script_container(*, compressed: bool = False) -> bytes:
    names = [
        "ScriptImportSize",
        "PlayerScriptsList",
        "NamedCameras",
        "CameraAnimationList",
        "ScriptsPlayers",
        "ScriptTeams",
        "ObjectsList",
        "TriggerAreas",
        "WaypointsList",
    ]
    indexes = {name: index + 1 for index, name in enumerate(names)}
    name_table = struct.pack("<I", len(names))
    for index in range(len(names), 0, -1):
        name_table += _dotnet_string(names[index - 1]) + struct.pack("<I", index)
    body = b"CkMp" + name_table + b"".join(
        _record(name, 3 if name == "ObjectsList" else 1, b"", indexes)
        for name in names
    )
    return _refpack_literals(body) if compressed else body


class MapCorpusTests(unittest.TestCase):
    def test_discovers_map_payloads_by_suffix_and_signature_without_omission(
        self,
    ) -> None:
        ear_map, _ = _synthetic_map()
        ckmp_map, _ = _synthetic_map(compressed=False)
        malformed = b"BROKEN_SUFFIX_SELECTED"
        files = {
            "data/maps/packed.bse": ear_map,
            "data/maps/direct.scb": ckmp_map,
            "data/maps/disguised.tga": ear_map,
            "data/maps/ordinary.tga": b"TGA\0ordinary-image-data",
            "maps/combo.map": ear_map,
            "maps/malformed.map": malformed,
        }
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, files)
            real_probe = map_corpus_module._read_signature_prefix
            real_census = map_corpus_module.census_sage_map_bytes
            with (
                mock.patch.object(
                    map_corpus_module,
                    "_read_signature_prefix",
                    wraps=real_probe,
                ) as prefix_probe,
                mock.patch.object(
                    map_corpus_module,
                    "census_sage_map_bytes",
                    wraps=real_census,
                ) as census,
            ):
                report = scan_map_corpus(root)

        self.assertEqual(prefix_probe.call_count, len(files))
        self.assertEqual(census.call_count, 5)
        self.assertEqual(
            [item.virtual_path for item in report.maps],
            [
                "data/maps/direct.scb",
                "data/maps/disguised.tga",
                "data/maps/packed.bse",
                "maps/combo.map",
                "maps/malformed.map",
            ],
        )
        by_path = {item.virtual_path: item for item in report.maps}
        self.assertEqual(
            by_path["data/maps/packed.bse"].discovery_reasons,
            ("ear-signature",),
        )
        self.assertEqual(
            by_path["data/maps/direct.scb"].discovery_reasons,
            ("ckmp-signature",),
        )
        self.assertEqual(
            by_path["data/maps/disguised.tga"].discovery_reasons,
            ("ear-signature",),
        )
        self.assertEqual(
            by_path["maps/combo.map"].discovery_reasons,
            ("map-suffix", "ear-signature"),
        )
        self.assertEqual(
            by_path["maps/malformed.map"].discovery_reasons,
            ("map-suffix",),
        )
        self.assertTrue(by_path["data/maps/packed.bse"].accepted)
        self.assertTrue(by_path["data/maps/direct.scb"].accepted)
        self.assertTrue(by_path["data/maps/disguised.tga"].accepted)
        self.assertFalse(by_path["maps/malformed.map"].accepted)
        self.assertEqual(
            by_path["maps/malformed.map"].rejection_code,
            "census-parse-rejected",
        )
        summary = report.neutral()["summary"]
        self.assertEqual(summary["selectedMapCount"], 5)
        self.assertEqual(summary["mapSuffixDiscoveryCount"], 2)
        self.assertEqual(summary["earSignatureDiscoveryCount"], 3)
        self.assertEqual(summary["ckmpSignatureDiscoveryCount"], 1)
        self.assertEqual(summary["signatureDiscoveredMapCount"], 4)
        self.assertEqual(summary["suffixOnlyDiscoveryCount"], 1)
        self.assertEqual(summary["multiReasonDiscoveryCount"], 1)
        self.assertNotIn(
            "data/maps/ordinary.tga",
            {item.virtual_path for item in report.maps},
        )
        encoded = json.dumps(report.neutral(), allow_nan=False, sort_keys=True)
        self.assertNotIn("BROKEN_SUFFIX_SELECTED", encoded)
        self.assertFalse(_contains_bytes(report.neutral()))

    def test_structural_partition_hands_off_scb_and_keeps_terrain_maps(self) -> None:
        terrain, _ = _synthetic_map(compressed=False)
        files = {
            "data/maps/packed.bse": _synthetic_script_container(compressed=True),
            "data/maps/misleading.map": _synthetic_script_container(),
            "data/maps/terrain.scb": terrain,
            "data/maps/malformed.map": b"BROKEN_MAP",
        }

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, files)
            with mock.patch.object(
                map_corpus_module,
                "census_sage_map_bytes",
                wraps=map_corpus_module.census_sage_map_bytes,
            ) as called:
                report = scan_map_corpus(root)

        self.assertEqual(called.call_count, len(files))
        self.assertEqual(
            [item.virtual_path for item in report.script_containers],
            ["data/maps/misleading.map", "data/maps/packed.bse"],
        )
        self.assertEqual(
            [item.virtual_path for item in report.maps],
            ["data/maps/malformed.map", "data/maps/terrain.scb"],
        )
        terrain = {item.virtual_path: item for item in report.maps}
        self.assertFalse(terrain["data/maps/malformed.map"].accepted)
        self.assertEqual(
            terrain["data/maps/malformed.map"].rejection_code,
            "census-parse-rejected",
        )
        self.assertTrue(terrain["data/maps/terrain.scb"].accepted)
        for item in report.script_containers:
            self.assertEqual(item.status, "handed-off")
            self.assertIsNone(item.profile)
            self.assertTrue(item.script_container_core_present)
            self.assertFalse(item.terrain_core_present)
            self.assertEqual(len(item.classification_evidence_sha256), 64)
        summary = report.neutral()["summary"]
        self.assertEqual(summary["candidateArtifactCount"], 4)
        self.assertEqual(summary["scriptContainerCount"], 2)
        self.assertEqual(summary["selectedMapCount"], 2)
        self.assertEqual(summary["acceptedMapCount"], 1)
        self.assertEqual(summary["rejectedMapCount"], 1)
        self.assertFalse(summary["complete"])
        self.assertNotIn("missing CkMp magic", json.dumps(report.neutral(), sort_keys=True))

    def test_profile_mapping_rejects_structurally_proven_script_container(self) -> None:
        source = _synthetic_script_container()
        path = "data/maps/direct.scb"
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {path: source})
            with (
                mock.patch.object(
                    map_corpus_module,
                    "census_sage_map_bytes",
                    wraps=map_corpus_module.census_sage_map_bytes,
                ) as called,
                self.assertRaisesRegex(MapCorpusError, "terrain maps only"),
            ):
                scan_map_corpus(root, profiles={path: "scenario"})

        called.assert_called_once_with(source, profile="scenario")

    def test_signature_candidate_is_fully_size_and_sha_verified(self) -> None:
        source, _ = _synthetic_map()
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"data/maps/packed.bse": source})
            target = root / "data" / "maps" / "packed.bse"
            tampered = bytearray(target.read_bytes())
            tampered[-1] ^= 1
            target.write_bytes(tampered)
            with (
                mock.patch.object(map_corpus_module, "census_sage_map_bytes") as census,
                self.assertRaisesRegex(MapCorpusError, "SHA-256"),
            ):
                scan_map_corpus(root)
            census.assert_not_called()

    def test_profiles_accept_exact_signature_candidates_only(self) -> None:
        direct, _ = _synthetic_map(compressed=False)
        files = {
            "data/maps/direct.scb": direct,
            "data/images/ordinary.tga": b"TGA\0ordinary-image-data",
        }
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, files)
            report = scan_map_corpus(
                root,
                profiles={"data/maps/direct.scb": "scenario"},
            )
            with self.assertRaisesRegex(MapCorpusError, "nonselected"):
                scan_map_corpus(
                    root,
                    profiles={"data/images/ordinary.tga": "scenario"},
                )
            with self.assertRaisesRegex(MapCorpusError, "unknown manifest path"):
                scan_map_corpus(
                    root,
                    profiles={"data/maps/missing.scb": "scenario"},
                )

        self.assertEqual(len(report.maps), 1)
        self.assertEqual(report.maps[0].profile, "scenario")
        self.assertEqual(report.maps[0].discovery_reasons, ("ckmp-signature",))
        self.assertTrue(report.maps[0].accepted)

    def test_explicit_profiles_propagate_and_bind_all_corpus_hashes(self) -> None:
        files = {
            "maps/a/a.map": b"A",
            "maps/b/b.map": b"B",
            "maps/c/c.map": b"C",
            "maps/d/d.map": b"D",
        }
        assignments = {
            "maps/b/b.map": "scenario",
            "maps/c/c.map": "library",
            "maps/d/d.map": "placeholder",
        }

        def census(source: bytes, *, profile: str) -> dict[str, object]:
            return _facts(source, profile=profile)

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, files)
            with mock.patch.object(
                map_corpus_module,
                "census_sage_map_bytes",
                side_effect=census,
            ) as called:
                default = scan_map_corpus(root)
                explicit_default = scan_map_corpus(
                    root,
                    profiles={path: "multiplayer" for path in files},
                )
                mixed = scan_map_corpus(root, profiles=assignments)
                reordered = scan_map_corpus(
                    root, profiles=dict(reversed(tuple(assignments.items())))
                )

        self.assertEqual(default.neutral(), explicit_default.neutral())
        self.assertEqual(mixed.neutral(), reordered.neutral())
        self.assertEqual(
            [item.profile for item in mixed.maps],
            ["multiplayer", "scenario", "library", "placeholder"],
        )
        self.assertEqual([item.profile_version for item in mixed.maps], [1] * 4)
        self.assertEqual(
            [item.runnable for item in mixed.maps], [True, True, False, False]
        )
        self.assertEqual(
            mixed.neutral()["summary"]["runnableMapCount"],
            2,
        )
        self.assertEqual(
            mixed.neutral()["summary"]["nonRunnableMapCount"],
            2,
        )
        self.assertEqual(
            mixed.neutral()["diagnostics"]["profileCounts"],
            {
                "library": 1,
                "multiplayer": 1,
                "placeholder": 1,
                "scenario": 1,
            },
        )
        self.assertNotEqual(
            default.profile_selection_sha256, mixed.profile_selection_sha256
        )
        self.assertNotEqual(default.map_inventory_sha256, mixed.map_inventory_sha256)
        self.assertNotEqual(default.map_evidence_sha256, mixed.map_evidence_sha256)
        self.assertNotEqual(default.corpus_sha256, mixed.corpus_sha256)
        self.assertEqual(called.call_count, 16)
        self.assertEqual(
            [call.kwargs["profile"] for call in called.call_args_list[-4:]],
            ["multiplayer", "scenario", "library", "placeholder"],
        )

    def test_invalid_profile_mappings_fail_closed_before_census(self) -> None:
        files = {
            "maps/a/a.map": b"A",
            "data/not-a-map.bin": b"B",
        }
        cases: tuple[tuple[object, str], ...] = (
            ({"maps/missing.map": "scenario"}, "unknown manifest path"),
            ({"data/not-a-map.bin": "scenario"}, "non-map path"),
            ({"../escape.map": "scenario"}, "unsafe path"),
            ({"maps/a/a.map": "campaign"}, "unsupported profile"),
            ({"Maps/a/a.map": "scenario"}, "exactly match manifest casing"),
            (
                {
                    "maps/a/a.map": "scenario",
                    "MAPS/A/A.MAP": "library",
                },
                "case-colliding paths",
            ),
            ({1: "scenario"}, "key must be a string"),
            (["not", "a", "mapping"], "path-to-profile mapping"),
        )
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, files)
            for profiles, expected in cases:
                with (
                    self.subTest(profiles=profiles),
                    mock.patch.object(
                        map_corpus_module, "census_sage_map_bytes"
                    ) as census,
                    self.assertRaisesRegex((MapCorpusError, TypeError), expected),
                ):
                    scan_map_corpus(root, profiles=profiles)  # type: ignore[arg-type]
                census.assert_not_called()

    def test_hundreds_of_maps_are_all_processed_without_omission(self) -> None:
        files = {
            f"maps/map-{index:03d}/map-{index:03d}.map": f"MAP-{index:03d}".encode(
                "ascii"
            )
            for index in range(300)
        }
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, files)
            with mock.patch.object(
                map_corpus_module,
                "census_sage_map_bytes",
                side_effect=lambda source, *, profile: _facts(source, profile=profile),
            ) as census:
                report = scan_map_corpus(root)

        self.assertEqual(census.call_count, 300)
        self.assertEqual(len(report.maps), 300)
        self.assertEqual(report.neutral()["summary"]["selectedMapCount"], 300)
        self.assertEqual(report.neutral()["summary"]["acceptedMapCount"], 300)
        self.assertTrue(report.complete)

    def test_scans_every_declared_map_deterministically_and_redacts_map_details(
        self,
    ) -> None:
        files = {
            "maps/Alpha/Alpha.map": b"ACCEPTED_MAP_PAYLOAD",
            "Maps/Zeta/Zeta.MAP": b"REJECTED_MAP_PAYLOAD",
            "data/ignored.bin": b"NON_MAP_PAYLOAD_MUST_NOT_BE_CENSUSED",
        }
        with (
            tempfile.TemporaryDirectory() as first_raw,
            tempfile.TemporaryDirectory() as second_raw,
        ):
            first_root = Path(first_raw)
            second_root = Path(second_raw)
            _write_corpus(first_root, files)
            _write_corpus(second_root, files)

            stdout = io.StringIO()
            with (
                mock.patch.object(
                    map_corpus_module,
                    "census_sage_map_bytes",
                    side_effect=_fixture_census,
                ) as census,
                redirect_stdout(stdout),
            ):
                first = scan_map_corpus(first_root)
                repeated = scan_map_corpus(first_root)
                copied = scan_map_corpus(second_root)

        self.assertEqual(stdout.getvalue(), "")
        self.assertEqual(census.call_count, 6)
        self.assertEqual(first.neutral(), repeated.neutral())
        self.assertEqual(first.neutral(), copied.neutral())
        self.assertFalse(first.complete)
        self.assertEqual(
            [item.virtual_path for item in first.maps],
            ["maps/Alpha/Alpha.map", "Maps/Zeta/Zeta.MAP"],
        )
        report = first.json_ready()
        self.assertEqual(report["schema"], "openbfme.map-corpus")
        self.assertEqual(report["summary"]["selectedMapCount"], 2)
        self.assertEqual(report["summary"]["acceptedMapCount"], 1)
        self.assertEqual(report["summary"]["rejectedMapCount"], 1)
        self.assertEqual(
            report["diagnostics"]["rejectionCodes"],
            {"strict-cook-rejected": 1},
        )
        self.assertEqual(
            [item["status"] for item in report["maps"]],
            ["accepted", "rejected"],
        )
        encoded = json.dumps(report, sort_keys=True)
        self.assertNotIn("ACCEPTED_MAP_PAYLOAD", encoded)
        self.assertNotIn("REJECTED_MAP_PAYLOAD", encoded)
        self.assertNotIn("NON_MAP_PAYLOAD_MUST_NOT_BE_CENSUSED", encoded)
        self.assertNotIn("DO_NOT_EXPORT_SCRIPT_BODY", encoded)
        self.assertNotIn("DO_NOT_EXPORT_REJECTION_TEXT", encoded)
        self.assertNotIn("123.25", encoded)
        self.assertFalse(_contains_bytes(report))

    def test_strict_mode_finishes_all_maps_and_preserves_report(self) -> None:
        files = {
            "maps/a/a.map": b"ACCEPTED_MAP_PAYLOAD",
            "maps/b/b.map": b"REJECTED_MAP_PAYLOAD",
        }
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, files)
            with mock.patch.object(
                map_corpus_module,
                "census_sage_map_bytes",
                side_effect=_fixture_census,
            ) as census:
                normal = scan_map_corpus(root)
                with self.assertRaises(MapCorpusStrictError) as caught:
                    scan_map_corpus(root, strict=True)

        self.assertEqual(census.call_count, 4)
        self.assertEqual(caught.exception.report.neutral(), normal.neutral())
        self.assertEqual(len(caught.exception.report.maps), 2)

    def test_parser_exception_is_compact_rejection_and_does_not_stop_later_maps(
        self,
    ) -> None:
        files = {
            "maps/a/a.map": b"BROKEN_MAP",
            "maps/b/b.map": b"GOOD_MAP",
        }

        def census(source: bytes, *, profile: str = "multiplayer") -> dict[str, object]:
            if source == b"BROKEN_MAP":
                raise SageMapError(
                    "DO_NOT_EXPORT_EXCEPTION_TEXT script Secret at (12, 34)"
                )
            return _facts(source, profile=profile)

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, files)
            with mock.patch.object(
                map_corpus_module, "census_sage_map_bytes", side_effect=census
            ) as called:
                report = scan_map_corpus(root)

        self.assertEqual(called.call_count, 2)
        self.assertEqual(
            [item.status for item in report.maps], ["rejected", "accepted"]
        )
        self.assertEqual(
            report.neutral()["diagnostics"]["rejectionCodes"],
            {"census-parse-rejected": 1},
        )
        self.assertNotIn("DO_NOT_EXPORT_EXCEPTION_TEXT", json.dumps(report.neutral()))

    def test_real_bounded_census_rejection_is_recorded_without_payload(self) -> None:
        source = b"SYNTHETIC_NOT_A_SAGE_MAP"
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"maps/invalid.map": source})
            report = scan_map_corpus(root)

        self.assertFalse(report.complete)
        self.assertEqual(report.maps[0].rejection_code, "census-parse-rejected")
        self.assertNotIn(source.decode("ascii"), json.dumps(report.neutral()))

    def test_invalid_census_contract_is_rejected_without_leaking_returned_details(
        self,
    ) -> None:
        source = b"MAP"
        invalid = _facts(source)
        invalid["sourceSha256"] = "0" * 64
        invalid["leak"] = "DO_NOT_EXPORT_INVALID_CENSUS_DETAILS"
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"maps/a.map": source})
            with mock.patch.object(
                map_corpus_module, "census_sage_map_bytes", return_value=invalid
            ):
                report = scan_map_corpus(root)

        self.assertEqual(report.maps[0].rejection_code, "census-contract-rejected")
        self.assertNotIn(
            "DO_NOT_EXPORT_INVALID_CENSUS_DETAILS", json.dumps(report.neutral())
        )

    def test_manifest_change_during_census_fails_closed(self) -> None:
        source = b"MAP"
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            manifest = _write_corpus(root, {"maps/a.map": source})

            def census(
                payload: bytes, *, profile: str = "multiplayer"
            ) -> dict[str, object]:
                manifest["install"]["root"] = "changed-during-census"  # type: ignore[index]
                _write_manifest(root, manifest)
                return _facts(payload, profile=profile)

            with (
                mock.patch.object(
                    map_corpus_module, "census_sage_map_bytes", side_effect=census
                ),
                self.assertRaisesRegex(MapCorpusError, "changed during map census"),
            ):
                scan_map_corpus(root)

    def test_rejects_selected_map_size_and_sha256_mismatches_before_census(
        self,
    ) -> None:
        source = b"TRUSTED_MAP"
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"maps/trusted.map": source})
            (root / "maps" / "trusted.map").write_bytes(source + b"x")
            with (
                mock.patch.object(map_corpus_module, "census_sage_map_bytes") as census,
                self.assertRaisesRegex(MapCorpusError, "file size"),
            ):
                scan_map_corpus(root)
            census.assert_not_called()

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"maps/trusted.map": source})
            changed = bytearray(source)
            changed[-1] ^= 1
            (root / "maps" / "trusted.map").write_bytes(changed)
            with (
                mock.patch.object(map_corpus_module, "census_sage_map_bytes") as census,
                self.assertRaisesRegex(MapCorpusError, "SHA-256"),
            ):
                scan_map_corpus(root)
            census.assert_not_called()

    def test_rejects_manifest_schema_totals_aggregate_paths_order_and_collisions(
        self,
    ) -> None:
        source = b"MAP"

        def assert_manifest_rejected(mutate: object, expected: str) -> None:
            with tempfile.TemporaryDirectory() as raw:
                root = Path(raw)
                manifest = _write_corpus(
                    root,
                    {"data/a.bin": b"A", "maps/b.map": source},
                )
                mutate(manifest)  # type: ignore[operator]
                _write_manifest(root, manifest)
                with self.assertRaisesRegex(MapCorpusError, expected):
                    scan_map_corpus(root)

        assert_manifest_rejected(
            lambda value: value.__setitem__("schema", "wrong"),
            "schema is unsupported",
        )
        assert_manifest_rejected(
            lambda value: value["totals"].__setitem__("bytes", 1),  # type: ignore[index,union-attr]
            "totals do not match",
        )
        assert_manifest_rejected(
            lambda value: value.__setitem__("aggregate_sha256", "0" * 64),
            "aggregate SHA-256",
        )
        assert_manifest_rejected(
            lambda value: value["files"].reverse(),  # type: ignore[index,union-attr]
            "inventory is not canonical",
        )

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            manifest = _manifest({"maps/a.map": source})
            entry = manifest["files"][0]  # type: ignore[index]
            entry["path"] = "../escape.map"  # type: ignore[index]
            manifest["aggregate_sha256"] = _aggregate(manifest["files"])  # type: ignore[arg-type,index]
            _write_manifest(root, manifest)
            with self.assertRaisesRegex(MapCorpusError, "unsafe path"):
                scan_map_corpus(root)

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            manifest = _manifest({"Maps/A.map": source, "maps/a.MAP": source})
            _write_manifest(root, manifest)
            with self.assertRaisesRegex(MapCorpusError, "case-colliding paths"):
                scan_map_corpus(root)

    def test_rejects_missing_extra_and_linked_tree_entries_before_census(self) -> None:
        source = b"MAP"
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"maps/a.map": source})
            (root / "maps" / "a.map").unlink()
            with (
                mock.patch.object(map_corpus_module, "census_sage_map_bytes") as census,
                self.assertRaisesRegex(MapCorpusError, "missing declared files"),
            ):
                scan_map_corpus(root)
            census.assert_not_called()

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"maps/a.map": source})
            (root / "extra.bin").write_bytes(b"extra")
            with self.assertRaisesRegex(MapCorpusError, "undeclared files"):
                scan_map_corpus(root)

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"maps/a.map": source})
            (root / "empty-extra").mkdir()
            with self.assertRaisesRegex(MapCorpusError, "undeclared directories"):
                scan_map_corpus(root)

        with (
            tempfile.TemporaryDirectory() as raw,
            tempfile.TemporaryDirectory() as external_raw,
        ):
            root = Path(raw)
            external = Path(external_raw) / "target.map"
            external.write_bytes(source)
            _write_corpus(root, {"maps/a.map": source})
            declared = root / "maps" / "a.map"
            declared.unlink()
            try:
                os.symlink(external, declared)
            except OSError:
                declared.write_bytes(source)
                original = map_corpus_module._is_link_like
                with mock.patch.object(
                    map_corpus_module,
                    "_is_link_like",
                    side_effect=lambda path: path == declared or original(path),
                ):
                    with self.assertRaisesRegex(MapCorpusError, "contains a link"):
                        scan_map_corpus(root)
            else:
                with self.assertRaisesRegex(MapCorpusError, "contains a link"):
                    scan_map_corpus(root)

    def test_empty_selection_and_all_selected_bounds_fail_without_truncation(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, {"data/readme.txt": b"no maps"})
            with self.assertRaisesRegex(MapCorpusError, "declares no map files"):
                scan_map_corpus(root)

        files = {"maps/a.map": b"AAAA", "maps/b.map": b"BBBB"}
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _write_corpus(root, files)
            with mock.patch.object(
                map_corpus_module, "census_sage_map_bytes"
            ) as census:
                with self.assertRaisesRegex(MapCorpusLimitError, "file count"):
                    scan_map_corpus(root, max_files=1)
                with self.assertRaisesRegex(MapCorpusLimitError, "total bytes"):
                    scan_map_corpus(root, max_total_bytes=4)
                with mock.patch.object(map_corpus_module, "MAX_SOURCE_BYTES", 3):
                    with self.assertRaisesRegex(MapCorpusLimitError, "per-file"):
                        scan_map_corpus(root)
            census.assert_not_called()

    def test_option_types_and_hard_bounds_fail_closed(self) -> None:
        with self.assertRaises(TypeError):
            scan_map_corpus("unused", strict=1)  # type: ignore[arg-type]
        with self.assertRaises(TypeError):
            scan_map_corpus("unused", profiles=[])  # type: ignore[arg-type]
        with self.assertRaises(TypeError):
            scan_map_corpus("unused", max_files=False)  # type: ignore[arg-type]
        with self.assertRaises(ValueError):
            scan_map_corpus("unused", max_files=0)
        with self.assertRaises(ValueError):
            scan_map_corpus(
                "unused",
                max_total_bytes=map_corpus_module.MAX_MAP_CORPUS_TOTAL_BYTES + 1,
            )


if __name__ == "__main__":
    unittest.main()
