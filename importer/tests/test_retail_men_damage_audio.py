from __future__ import annotations

import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from openbfme_importer.retail_men_damage_audio import (
    ROOT_IDS,
    _event_blocks,
    compose_men_damage_audio_contract,
)


def _digest(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


class RetailMenDamageAudioTests(unittest.TestCase):
    def _fixture(self, root: Path, *, duplicate: bool = False) -> dict[str, Path]:
        effective = root / "effective"
        ini_path = effective / "data" / "ini" / "soundeffects.ini"
        ini_path.parent.mkdir(parents=True)
        source_parts: list[str] = []
        sample_ids: list[str] = []
        event_registry: dict[str, object] = {}
        for index, event_id in enumerate(ROOT_IDS):
            body = f"FixtureBody{index}"
            sample_ids.append(body)
            assignments = [f"  Sounds = {body}"]
            parameters: list[dict[str, str]] = []
            if event_id == "BuildingBigConstructionLoop":
                assignments.extend(
                    [
                        "  Attack = FixtureAttack",
                        "  Decay = FixtureDecay",
                        "  Control = loop",
                    ]
                )
                sample_ids.extend(["FixtureAttack", "FixtureDecay"])
                parameters = [
                    {"field": "Attack", "value": "FixtureAttack"},
                    {"field": "Decay", "value": "FixtureDecay"},
                    {"field": "Control", "value": "loop"},
                ]
            source_parts.append(
                f"AudioEvent {event_id}\r\n"
                + "\r\n".join(assignments)
                + "\r\nEnd\r\n"
            )
            event_registry[event_id] = {
                "parameters": parameters,
                "sounds": [{"id": body}],
            }
        ini_payload = "\r\n".join(source_parts).encode("cp1252")
        ini_path.write_bytes(ini_payload)

        files: list[dict[str, object]] = [
            {
                "archive": "ini.big",
                "offset": 100,
                "path": "data/ini/soundeffects.ini",
                "precedence": 10,
                "sha256": _digest(ini_payload),
                "size": len(ini_payload),
            }
        ]
        if duplicate:
            duplicate_path = effective / "data" / "ini" / "voice.ini"
            duplicate_payload = (
                f"AudioEvent {ROOT_IDS[0]}\n  Sounds = FixtureBody0\nEnd\n"
            ).encode("ascii")
            duplicate_path.write_bytes(duplicate_payload)
            files.append(
                {
                    "archive": "voice.big",
                    "offset": 200,
                    "path": "data/ini/voice.ini",
                    "precedence": 11,
                    "sha256": _digest(duplicate_payload),
                    "size": len(duplicate_payload),
                }
            )

        sample_paths: dict[str, str] = {}
        for index, sample_id in enumerate(sample_ids):
            virtual = f"data/audio/sounds/{sample_id.casefold()}.wav"
            payload = f"fixture-{sample_id}".encode("ascii")
            media = effective.joinpath(*virtual.split("/"))
            media.parent.mkdir(parents=True, exist_ok=True)
            media.write_bytes(payload)
            files.append(
                {
                    "archive": "audio.big",
                    "offset": 1000 + index * 100,
                    "path": virtual,
                    "precedence": 12,
                    "sha256": _digest(payload),
                    "size": len(payload),
                }
            )
            if sample_id not in {"FixtureAttack", "FixtureDecay"}:
                sample_paths[sample_id] = f"assets/audio/men/{sample_id.casefold()}.wav"

        manifest = {
            "aggregate_sha256": "a" * 64,
            "files": files,
            "schema": "openbfme.effective-assets-manifest",
            "schema_version": 0,
            "totals": {
                "bytes": sum(int(row["size"]) for row in files),
                "files": len(files),
            },
        }
        manifest_path = effective / ".openbfme" / "manifest.json"
        manifest_path.parent.mkdir()
        manifest_path.write_text(json.dumps(manifest), "utf-8")

        catalog_entries = [
            {
                "archive": row["archive"],
                "name": row["path"],
                "offset": row["offset"],
                "precedence": row["precedence"],
                "size": row["size"],
            }
            for row in files
        ]
        catalog_path = root / "catalog.json"
        catalog_path.write_text(json.dumps({"entries": catalog_entries}), "utf-8")

        owned_patterns = [
            row["path"]
            for row in files
            if str(row["path"]).casefold().endswith(".wav")
            and Path(str(row["path"])).stem
            not in {"fixtureattack", "fixturedecay"}
        ]
        profile = {
            "id": "fixture-profile",
            "resources": [{"id": "fixture-audio", "patterns": owned_patterns}],
            "runtime_data": {
                "data/audio_events.json": {
                    "events": event_registry,
                    "samples": sample_paths,
                }
            },
        }
        profile_path = root / "profile.json"
        profile_path.write_text(json.dumps(profile), "utf-8")
        return {
            "catalog": catalog_path,
            "effective": effective,
            "manifest": manifest_path,
            "profile": profile_path,
        }

    def test_exact_byte_spans_and_duplicate_fields_are_retained(self) -> None:
        payload = (
            b"; preface\r\nAudioEvent Example\r\n"
            b"  Sounds = One\r\n  VolumeShift = -5\r\n"
            b"  VolumeShift = -10\r\nEnd\r\n"
        )
        blocks = _event_blocks(payload, "fixture.ini")
        self.assertEqual(len(blocks), 1)
        span = blocks[0]
        self.assertEqual(payload[span["byteStart"] : span["byteEndExclusive"]], payload[11:])
        self.assertEqual(span["block"].values("VolumeShift"), ("-5", "-10"))
        self.assertIsNone(span["parentSyntax"])

    def test_composes_deterministically_and_proposes_only_envelopes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            paths = self._fixture(Path(temporary))
            arguments = {
                "effective_assets_root": paths["effective"],
                "manifest_path": paths["manifest"],
                "catalog_path": paths["catalog"],
                "complete_profile_path": paths["profile"],
            }
            first = compose_men_damage_audio_contract(**arguments)
            second = compose_men_damage_audio_contract(**arguments)
        self.assertEqual(first, second)
        self.assertEqual(first["summary"]["accountedIdentifierCount"], 9)
        self.assertEqual(first["summary"]["sourceReferenceCount"], 11)
        self.assertEqual(first["summary"]["profileAudioRegistryMissingLeafCount"], 2)
        resource = first["profileFragmentProposal"]["resources"][0]
        self.assertEqual(resource["expected_count"], 2)
        self.assertEqual(
            set(
                first["profileFragmentProposal"]["runtimeDataMerge"]
                ["data/audio_events.json"]["samples"]
            ),
            {"FixtureAttack", "FixtureDecay"},
        )

    def test_duplicate_effective_definition_fails_closed_in_contract(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            paths = self._fixture(Path(temporary), duplicate=True)
            result = compose_men_damage_audio_contract(
                effective_assets_root=paths["effective"],
                manifest_path=paths["manifest"],
                catalog_path=paths["catalog"],
                complete_profile_path=paths["profile"],
            )
        self.assertEqual(result["summary"]["accountedIdentifierCount"], 8)
        self.assertIn(
            {
                "code": "ambiguous-effective-audio-definition",
                "id": ROOT_IDS[0],
                "locations": ["data/ini/soundeffects.ini", "data/ini/voice.ini"],
            },
            result["blockers"],
        )


if __name__ == "__main__":
    unittest.main()
