from __future__ import annotations

import contextlib
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest

from openbfme_importer import cli
from openbfme_importer import sage_roads


def _write(root: Path, relative: str, payload: bytes) -> None:
    target = root.joinpath(*relative.split("/"))
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(payload)


def _roads(root: Path, text: str) -> None:
    _write(root, sage_roads.ROADS_VIRTUAL_PATH, text.encode("cp1252"))


def _valid_road(road_id: str, texture: str) -> str:
    return (
        f"Road {road_id}\n"
        f"  Texture = {texture}\n"
        "  RoadWidth = 52\n"
        "  RoadWidthInTexture = .95\n"
        "End\n"
    )


class SageRoadClosureTests(unittest.TestCase):
    def test_case_insensitive_exact_ids_and_compiled_bridge_are_deterministic(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "effective-assets"
            source = (
                "; the commented Road is not a definition\n"
                ";Road Alpha\n"
                + _valid_road("Zebra", "ZebraTrack.tga")
                + _valid_road("Alpha", "AlphaTrack.tga")
            )
            _roads(root, source)
            _write(root, "art/compiled/zebratrack.dds", b"compiled zebra")
            _write(root, "art/textures/alphatrack.tga", b"authored alpha")
            _write(root, "art/textures/zebratrack_extra.dds", b"not a match")

            first = sage_roads.build_road_closure(root, ["zebra", "ALPHA"])
            second = sage_roads.build_road_closure(root, ["ALPHA", "zebra"])

            self.assertEqual(first, second)
            self.assertTrue(first["summary"]["ready"])
            self.assertEqual(first["summary"]["resolvedRoadCount"], 2)
            self.assertEqual(first["summary"]["resolvedTextureCount"], 2)
            self.assertEqual(
                [item["id"] for item in first["roads"]], ["Alpha", "Zebra"]
            )
            alpha, zebra = first["roads"]
            self.assertEqual(
                alpha["textureLeaf"]["physicalVirtualPath"],
                "art/textures/alphatrack.tga",
            )
            self.assertEqual(
                zebra["textureLeaf"]["physicalVirtualPath"],
                "art/compiled/zebratrack.dds",
            )
            self.assertIn(
                "sage-road-compiled-texture:exact-tga-stem-to-dds",
                zebra["textureLeaf"]["evidence"],
            )
            self.assertEqual(
                alpha["fields"]["RoadWidthInTexture"]["normalizedValue"],
                "0.95",
            )
            self.assertEqual(
                first["source"]["sha256"],
                hashlib.sha256(source.encode("cp1252")).hexdigest(),
            )
            self.assertEqual(
                alpha["textureLeaf"]["sha256"],
                hashlib.sha256(b"authored alpha").hexdigest(),
            )
            serialized = json.dumps(first)
            self.assertNotIn(str(root), serialized)

            payload = dict(first)
            aggregate = payload.pop("aggregateSha256")
            canonical = json.dumps(
                payload,
                sort_keys=True,
                ensure_ascii=False,
                separators=(",", ":"),
            ).encode("utf-8")
            self.assertEqual(aggregate, hashlib.sha256(canonical).hexdigest())
            self.assertEqual(
                sage_roads.default_road_closure_report_name(["zebra", "ALPHA"]),
                sage_roads.default_road_closure_report_name(["ALPHA", "zebra"]),
            )

    def test_duplicate_and_missing_definitions_are_retained_as_gaps(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _roads(
                root,
                _valid_road("Duplicate", "First.tga")
                + _valid_road("duplicate", "Second.tga"),
            )
            _write(root, "art/first.tga", b"first")
            _write(root, "art/second.tga", b"second")

            report = sage_roads.build_road_closure(
                root, ["DUPLICATE", "Absent"]
            )

            self.assertFalse(report["summary"]["ready"])
            self.assertEqual(report["summary"]["missingDefinitionCount"], 1)
            self.assertEqual(report["summary"]["ambiguousDefinitionCount"], 1)
            by_request = {item["requestedId"]: item for item in report["roads"]}
            self.assertEqual(by_request["Absent"]["status"], "missing-definition")
            ambiguous = by_request["DUPLICATE"]
            self.assertEqual(ambiguous["status"], "ambiguous-definition")
            self.assertEqual(
                [item["id"] for item in ambiguous["candidates"]],
                ["Duplicate", "duplicate"],
            )
            self.assertEqual(
                {item["code"] for item in report["diagnostics"]},
                {"missing-road-definition", "ambiguous-road-definition"},
            )

    def test_required_fields_and_positive_decimal_widths_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _roads(
                root,
                """
Road BadNumbers
  Texture = Present.tga
  RoadWidth = many
  RoadWidthInTexture = -0.5
End
Road BadFields
  Texture = Present.tga
  Texture = Other.tga
  RoadWidth = 52
End
""",
            )
            _write(root, "art/present.tga", b"present")
            _write(root, "art/other.tga", b"other")

            report = sage_roads.build_road_closure(
                root, ["BadNumbers", "BadFields"]
            )

            self.assertFalse(report["summary"]["ready"])
            self.assertEqual(report["summary"]["invalidDefinitionCount"], 2)
            self.assertEqual(report["summary"]["resolvedTextureCount"], 1)
            self.assertEqual(
                [item["status"] for item in report["roads"]],
                ["invalid-definition", "invalid-definition"],
            )
            codes = [item["code"] for item in report["diagnostics"]]
            self.assertEqual(codes.count("malformed-road-width"), 2)
            self.assertIn("duplicate-road-field", codes)
            self.assertIn("missing-road-field", codes)
            self.assertEqual(
                report["roads"][1]["textureLeaf"]["status"], "resolved"
            )

    def test_missing_and_ambiguous_textures_do_not_use_prefixes_or_guess(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _roads(
                root,
                _valid_road("Missing", "Track.tga")
                + _valid_road("CompiledAmbiguous", "Ambiguous.tga")
                + _valid_road("AuthoredAmbiguous", "Direct.tga"),
            )
            _write(root, "art/track_extra.dds", b"prefix only")
            _write(root, "art/a/ambiguous.dds", b"a")
            _write(root, "art/b/ambiguous.dds", b"b")
            _write(root, "art/a/direct.tga", b"a")
            _write(root, "art/b/direct.tga", b"b")

            report = sage_roads.build_road_closure(
                root, ["Missing", "CompiledAmbiguous", "AuthoredAmbiguous"]
            )

            self.assertFalse(report["summary"]["ready"])
            self.assertEqual(report["summary"]["unresolvedTextureCount"], 3)
            by_id = {item["id"]: item for item in report["roads"]}
            self.assertEqual(by_id["Missing"]["textureLeaf"]["status"], "missing")
            self.assertEqual(
                by_id["Missing"]["textureLeaf"]["candidates"], []
            )
            compiled = by_id["CompiledAmbiguous"]["textureLeaf"]
            self.assertEqual(compiled["status"], "ambiguous")
            self.assertEqual(
                compiled["candidates"],
                ["art/a/ambiguous.dds", "art/b/ambiguous.dds"],
            )
            authored = by_id["AuthoredAmbiguous"]["textureLeaf"]
            self.assertEqual(authored["status"], "ambiguous")
            self.assertEqual(
                authored["candidates"],
                ["art/a/direct.tga", "art/b/direct.tga"],
            )

    def test_unsafe_and_duplicate_target_ids_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            _roads(root, _valid_road("Safe", "Safe.tga"))
            _write(root, "art/safe.tga", b"safe")

            for unsafe in ("../escape", "two words", "*", "", ".hidden"):
                with self.subTest(unsafe=unsafe):
                    with self.assertRaisesRegex(ValueError, "unsafe target Road"):
                        sage_roads.build_road_closure(root, [unsafe])
            with self.assertRaisesRegex(ValueError, "duplicate target Road"):
                sage_roads.build_road_closure(root, ["Safe", "safe"])

    def test_cli_writes_private_report_and_returns_zero_or_six(self) -> None:
        args = cli.build_parser().parse_args(
            [
                "road-closure",
                "--assets-root",
                "C:/private/effective-assets",
                "--road",
                "One",
                "--road",
                "Two",
            ]
        )
        self.assertEqual(args.command, "road-closure")
        self.assertEqual(args.roads, ["One", "Two"])

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "effective-assets"
            state = Path(raw) / "private-state"
            _roads(root, _valid_road("Ready", "Ready.tga"))
            _write(root, "art/ready.tga", b"ready")

            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                result = cli.main(
                    [
                        "--json",
                        "--state-root",
                        str(state),
                        "road-closure",
                        "--assets-root",
                        str(root),
                        "--road",
                        "ready",
                    ]
                )
            self.assertEqual(result, 0)
            output = json.loads(stdout.getvalue())
            self.assertTrue(output["ready"])
            self.assertEqual(output["gap_count"], 0)
            report_path = Path(output["report"])
            self.assertEqual(report_path.parent, state / "reports")
            report = json.loads(report_path.read_text(encoding="utf-8"))
            self.assertEqual(output["aggregate_sha256"], report["aggregateSha256"])

            gap_stdout = io.StringIO()
            with contextlib.redirect_stdout(gap_stdout):
                gap_result = cli.main(
                    [
                        "--json",
                        "--state-root",
                        str(state),
                        "road-closure",
                        "--assets-root",
                        str(root),
                        "--road",
                        "Absent",
                    ]
                )
            self.assertEqual(gap_result, 6)
            self.assertFalse(json.loads(gap_stdout.getvalue())["ready"])


if __name__ == "__main__":
    unittest.main()
