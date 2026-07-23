from __future__ import annotations

import json
from pathlib import Path, PurePosixPath
import tempfile
import unittest

from importer.tests.test_sage_scb import _fixture

from openbfme_importer.pipeline import ImportPipeline
from openbfme_importer.sage_scb import SageScbError
from openbfme_importer.sage_scripts import (
    MAP_SCRIPTS_SCHEMA,
    MAP_SCRIPTS_SCHEMA_VERSION,
    map_scripts_document,
)


ROOT = Path(__file__).resolve().parents[2]
CATALOG = ROOT / ".private/retail-work/catalog/rotwk.json"
CONTRACT = (
    ROOT
    / ".private/retail-work/reports/skirmish-script-contract"
    / "skirmish_script_contract.json"
)

# One real skirmish map and one real libraries.big SCB from the tier-1
# skirmish contract scope (both present in the RotWK 2.01 catalog).
REAL_MAP = "maps/map mp adorn river/map mp adorn river.map"
REAL_SCB = "libraries/power restrictions.scb"


def _pipeline() -> ImportPipeline:
    return object.__new__(ImportPipeline)


class SageScriptsConverterTests(unittest.TestCase):
    def test_synthetic_scb_round_trips_through_the_converter(self) -> None:
        source_bytes = _fixture()
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source = root / "fixture.scb"
            source.write_bytes(source_bytes)
            target = root / "fixture.scripts.json"

            outputs = _pipeline()._convert_sage_scripts(source, target, {})
            self.assertEqual(outputs, [target])

            document = json.loads(target.read_text(encoding="utf-8"))
            self.assertEqual(document["schema"], MAP_SCRIPTS_SCHEMA)
            self.assertEqual(
                document["schemaVersion"], MAP_SCRIPTS_SCHEMA_VERSION
            )
            self.assertEqual(document["source"]["container"], "scb")
            self.assertEqual(
                document["source"]["sourceBytes"], len(source_bytes)
            )
            self.assertEqual(len(document["source"]["sourceSha256"]), 64)

            # The fixture carries one grouped and one top-level copy of the
            # same script: 1 condition + 2 actions (true/false lanes) each.
            self.assertEqual(document["counts"]["scripts"], 2)
            self.assertEqual(document["counts"]["actionSlots"], 4)
            self.assertEqual(document["counts"]["conditionSlots"], 2)
            self.assertEqual(document["actionOpcodes"], {"internalKey": 4})
            self.assertEqual(document["conditionOpcodes"], {"internalKey": 2})

            rows = document["scripts"]
            self.assertEqual(
                [row["groupPath"] for row in rows], ["fixture-group", ""]
            )
            self.assertEqual(
                {row["name"] for row in rows}, {"duplicate-script"}
            )
            for row in rows:
                self.assertEqual(row["actionSlots"], 2)
                self.assertEqual(row["conditionSlots"], 1)
                payload = row["payload"]
                self.assertEqual(payload["name"], "duplicate-script")
                self.assertIn("records", payload)

    def test_synthetic_map_container_extracts_only_player_scripts(self) -> None:
        # The synthetic SCB body is also a valid CkMp map blob for the map
        # lane, which parses PlayerScriptsList and skips every other chunk.
        document = map_scripts_document(_fixture(), container="map")
        self.assertEqual(document["source"]["container"], "map")
        self.assertEqual(document["counts"]["scripts"], 2)
        self.assertEqual(document["counts"]["actionSlots"], 4)
        self.assertEqual(document["counts"]["conditionSlots"], 2)

    def test_two_runs_are_byte_identical(self) -> None:
        source_bytes = _fixture()
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source = root / "fixture.scb"
            source.write_bytes(source_bytes)
            first = root / "first.scripts.json"
            second = root / "second.scripts.json"

            _pipeline()._convert_sage_scripts(source, first, {})
            _pipeline()._convert_sage_scripts(source, second, {})
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertNotIn(
                raw.encode("utf-8", "ignore"), first.read_bytes()
            )

    def test_corrupt_input_fails_closed(self) -> None:
        source_bytes = _fixture()
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            target = root / "out.scripts.json"

            truncated = root / "truncated.scb"
            truncated.write_bytes(source_bytes[: len(source_bytes) // 2])
            with self.assertRaises(SageScbError):
                _pipeline()._convert_sage_scripts(truncated, target, {})
            self.assertFalse(target.exists())

            garbage = root / "garbage.map"
            garbage.write_bytes(b"\x00" * 64)
            with self.assertRaises((SageScbError, ValueError)):
                _pipeline()._convert_sage_scripts(garbage, target, {})
            self.assertFalse(target.exists())

            flipped = bytearray(source_bytes)
            flipped[-1] ^= 0xFF
            corrupted = root / "corrupted.scb"
            corrupted.write_bytes(bytes(flipped))
            with self.assertRaises((SageScbError, ValueError)):
                _pipeline()._convert_sage_scripts(corrupted, target, {})
            self.assertFalse(target.exists())

    def test_converter_contract_is_strict(self) -> None:
        source_bytes = _fixture()
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source = root / "fixture.scb"
            source.write_bytes(source_bytes)

            with self.assertRaisesRegex(ValueError, "scripts.json"):
                _pipeline()._convert_sage_scripts(
                    source, root / "fixture.json", {}
                )
            with self.assertRaisesRegex(ValueError, "no options"):
                _pipeline()._convert_sage_scripts(
                    source, root / "fixture.scripts.json", {"extra": True}
                )
            wrong_suffix = root / "fixture.ini"
            wrong_suffix.write_bytes(source_bytes)
            with self.assertRaisesRegex(ValueError, ".map or .scb"):
                _pipeline()._convert_sage_scripts(
                    wrong_suffix, root / "fixture.scripts.json", {}
                )

    def test_unsupported_container_label_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "unsupported"):
            map_scripts_document(_fixture(), container="ini")


@unittest.skipUnless(
    CATALOG.is_file() and CONTRACT.is_file(),
    "RotWK catalog or skirmish script contract is not present",
)
class SageScriptsRealCorpusTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        from openbfme_importer.catalog import InstallCatalog

        cls.catalog = InstallCatalog.load(CATALOG)
        stale = cls.catalog.stale_reasons()
        if stale:
            raise unittest.SkipTest(f"stale RotWK catalog: {stale}")
        cls.winners = {
            rows[0].name.casefold(): rows[0]
            for rows in cls.catalog._by_key.values()
        }
        cls.contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
        cls.contract_sources = {
            row["path"].casefold(): row for row in cls.contract["sources"]
        }

    def _convert_real_entry(self, virtual_path: str) -> dict:
        entry = self.winners[virtual_path.casefold()]
        archive = self.catalog.open_archive_for(entry)
        payload = archive.read_entry(
            self.catalog.as_entry(entry), max_bytes=max(entry.size, 1)
        )
        suffix = PurePosixPath(virtual_path).suffix
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            source = root / f"entry{suffix}"
            source.write_bytes(payload)
            target = root / "entry.scripts.json"
            _pipeline()._convert_sage_scripts(source, target, {})
            return json.loads(target.read_text(encoding="utf-8"))

    def _assert_matches_contract(self, virtual_path: str) -> None:
        expected = self.contract_sources[virtual_path.casefold()]
        document = self._convert_real_entry(virtual_path)

        self.assertEqual(
            document["counts"]["scripts"], expected["scriptCount"]
        )
        self.assertEqual(document["actionOpcodes"], expected["actionOpcodes"])
        self.assertEqual(
            document["conditionOpcodes"], expected["conditionOpcodes"]
        )
        self.assertEqual(
            document["counts"]["actionSlots"],
            sum(row["actionSlots"] for row in expected["scripts"]),
        )
        self.assertEqual(
            document["counts"]["conditionSlots"],
            sum(row["conditionSlots"] for row in expected["scripts"]),
        )
        self.assertEqual(
            [row["name"] for row in document["scripts"]],
            [row["name"] for row in expected["scripts"]],
        )

    def test_real_skirmish_map_matches_the_contract(self) -> None:
        self._assert_matches_contract(REAL_MAP)

    def test_real_library_scb_matches_the_contract(self) -> None:
        self._assert_matches_contract(REAL_SCB)


if __name__ == "__main__":
    unittest.main()
