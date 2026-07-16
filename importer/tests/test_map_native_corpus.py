from __future__ import annotations

from contextlib import redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import openbfme_importer.map_native_corpus as map_native_module
from openbfme_importer.map_native_corpus import (
    MAP_NATIVE_CORPUS_MANIFEST,
    MapNativeCorpusBuildError,
    MapNativeCorpusError,
    MapNativeCorpusLimitError,
    MapNativeCorpusReuseError,
    build_map_native_corpus,
)
from openbfme_importer.sage_map import SageMapError

from importer.tests.test_map_corpus import _synthetic_script_container, _write_corpus
from importer.tests.test_sage_map import _synthetic_map


def _tree_snapshot(root: Path) -> dict[str, str]:
    return {
        path.relative_to(root).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(item for item in root.rglob("*") if item.is_file())
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


class MapNativeCorpusTests(unittest.TestCase):
    def test_script_containers_are_no_output_handoffs_and_reuse_is_verified(
        self,
    ) -> None:
        scripts = {
            "data/maps/packed.bse": _synthetic_script_container(compressed=True),
            "data/maps/misleading.map": _synthetic_script_container(),
        }
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            effective = root / "effective"
            output = root / "output"
            _write_corpus(effective, scripts)
            with mock.patch.object(
                map_native_module, "convert_sage_map"
            ) as converter:
                report = build_map_native_corpus(effective, output)
                reused = build_map_native_corpus(effective, output)

            converter.assert_not_called()
            self.assertTrue(report.complete)
            self.assertTrue(reused.reused)
            self.assertEqual(report.neutral(), reused.neutral())
            self.assertEqual(report.entries, ())
            self.assertEqual(report.outputs, ())
            self.assertEqual(len(report.handoffs), 2)
            self.assertEqual(
                [item.source_path for item in report.handoffs],
                ["data/maps/misleading.map", "data/maps/packed.bse"],
            )
            self.assertTrue(all(item.handed_off for item in report.handoffs))
            self.assertEqual(
                {
                    path.relative_to(output).as_posix()
                    for path in output.rglob("*")
                    if path.is_file()
                },
                {MAP_NATIVE_CORPUS_MANIFEST},
            )
            summary = report.neutral()["summary"]
            self.assertEqual(summary["candidateArtifactCount"], 2)
            self.assertEqual(summary["selectedMapCount"], 0)
            self.assertEqual(summary["scriptContainerCount"], 2)
            self.assertEqual(summary["scriptContainerHandoffCount"], 2)
            self.assertTrue(summary["structuralConversionComplete"])
            self.assertTrue(summary["sourceAccountingComplete"])
            self.assertEqual(
                summary["candidateArtifactBytes"],
                sum(len(item) for item in scripts.values()),
            )
            self.assertFalse(_contains_bytes(report.neutral()))

    def test_handoff_source_tamper_and_manifest_omission_fail_closed(self) -> None:
        scripts = {
            "data/maps/a.scb": _synthetic_script_container(),
            "data/maps/b.scb": _synthetic_script_container(compressed=True),
        }
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            effective = root / "effective"
            output = root / "output"
            _write_corpus(effective, scripts)
            build_map_native_corpus(effective, output)
            manifest_path = output / MAP_NATIVE_CORPUS_MANIFEST
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["handoffs"] = manifest["handoffs"][:1]
            parsed = map_native_module._parse_handoff(manifest["handoffs"][0])
            manifest["summary"] = map_native_module._summary(
                (), (parsed,), (), published=True
            )
            basis = {
                key: value
                for key, value in manifest.items()
                if key != "identitySha256"
            }
            manifest["identitySha256"] = map_native_module._canonical_sha256(
                basis
            )
            manifest_path.write_bytes(
                map_native_module._canonical_json_bytes(manifest, pretty=True)
            )
            with self.assertRaisesRegex(
                MapNativeCorpusReuseError, "handoff inventory"
            ):
                build_map_native_corpus(effective, output)

            second_output = root / "second-output"
            _write_corpus(root / "second-effective", scripts)
            second_effective = root / "second-effective"
            build_map_native_corpus(second_effective, second_output)
            source = second_effective / "data" / "maps" / "a.scb"
            source.write_bytes(source.read_bytes() + b"tamper")
            with self.assertRaisesRegex(MapNativeCorpusError, "size|SHA-256"):
                build_map_native_corpus(second_effective, second_output, force=True)

    def test_signature_discovered_sources_are_carried_reused_and_deduplicated(
        self,
    ) -> None:
        ear_map, _ = _synthetic_map()
        ckmp_map, _ = _synthetic_map(compressed=False)
        files = {
            "data/maps/packed.bse": ear_map,
            "data/maps/direct.scb": ckmp_map,
            "data/maps/disguised.tga": ear_map,
            "data/maps/ordinary.tga": b"TGA\0ordinary-image-data",
            "maps/combo.map": ear_map,
        }
        profiles = {"data/maps/direct.scb": "scenario"}
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            effective = root / "effective"
            output = root / "output"
            _write_corpus(effective, files)
            with mock.patch.object(
                map_native_module,
                "convert_sage_map",
                wraps=map_native_module.convert_sage_map,
            ) as converter:
                report = build_map_native_corpus(
                    effective,
                    output,
                    profiles=profiles,
                )
                reused = build_map_native_corpus(
                    effective,
                    output,
                    profiles=profiles,
                )

            self.assertEqual(converter.call_count, 2)
            self.assertTrue(report.complete)
            self.assertTrue(reused.reused)
            self.assertEqual(report.neutral(), reused.neutral())
            self.assertEqual(len(report.entries), 4)
            self.assertEqual(len(report.outputs), 2)
            self.assertEqual(
                [item.source_path for item in report.entries],
                [
                    "data/maps/direct.scb",
                    "data/maps/disguised.tga",
                    "data/maps/packed.bse",
                    "maps/combo.map",
                ],
            )
            by_path = {item.source_path: item for item in report.entries}
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
                by_path["data/maps/packed.bse"].output_path,
                by_path["data/maps/disguised.tga"].output_path,
            )
            self.assertEqual(
                by_path["data/maps/packed.bse"].output_path,
                by_path["maps/combo.map"].output_path,
            )
            summary = report.neutral()["summary"]
            self.assertEqual(summary["mapSuffixDiscoveryCount"], 1)
            self.assertEqual(summary["earSignatureDiscoveryCount"], 3)
            self.assertEqual(summary["ckmpSignatureDiscoveryCount"], 1)
            self.assertEqual(summary["signatureDiscoveredMapCount"], 4)
            self.assertNotIn(
                "data/maps/ordinary.tga",
                {item.source_path for item in report.entries},
            )
            for output_entry in report.outputs:
                cooked = json.loads(
                    (output / output_entry.path / "map.json").read_text("utf-8")
                )
                self.assertIn(output_entry.source_sha256, cooked["id"])
                self.assertNotIn("packed", cooked["id"].casefold())
                self.assertNotIn("direct", cooked["id"].casefold())
                self.assertIn(
                    f"/profile/{output_entry.profile}-v{output_entry.profile_version}",
                    output_entry.path,
                )

    def test_build_reuse_force_and_copied_root_are_deterministic(self) -> None:
        source_bytes, _ = _synthetic_map()
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            effective = root / "effective"
            first_output = root / "private-packs" / "first"
            second_output = root / "private-packs" / "second"
            _write_corpus(
                effective,
                {
                    "maps/Alpha/Alpha.map": source_bytes,
                    "data/ignored.bin": b"NOT_SELECTED",
                },
            )

            stdout = io.StringIO()
            with redirect_stdout(stdout):
                first = build_map_native_corpus(effective, first_output)
                reused = build_map_native_corpus(effective, first_output)
                copied = build_map_native_corpus(effective, second_output)
                forced = build_map_native_corpus(effective, first_output, force=True)

            self.assertEqual(stdout.getvalue(), "")
            self.assertTrue(first.complete)
            self.assertFalse(first.reused)
            self.assertTrue(reused.reused)
            self.assertFalse(copied.reused)
            self.assertFalse(forced.reused)
            self.assertEqual(first.neutral(), reused.neutral())
            self.assertEqual(first.neutral(), copied.neutral())
            self.assertEqual(first.neutral(), forced.neutral())
            self.assertEqual(
                (first_output / MAP_NATIVE_CORPUS_MANIFEST).read_bytes(),
                (second_output / MAP_NATIVE_CORPUS_MANIFEST).read_bytes(),
            )
            report = first.neutral()
            self.assertEqual(report["summary"]["selectedMapCount"], 1)
            self.assertEqual(report["summary"]["outputFileCount"], 18)
            self.assertTrue(report["summary"]["structuralConversionComplete"])
            self.assertEqual(report["summary"]["runnableMapCount"], 1)
            self.assertEqual(report["summary"]["nonRunnableMapCount"], 0)
            self.assertFalse(report["summary"]["objectBindingSemanticsComplete"])
            self.assertFalse(report["summary"]["gameplayFidelityClaimed"])
            self.assertFalse(report["summary"]["gameplaySemanticFidelityClaimed"])
            self.assertEqual(
                report["outputs"][0]["objectResolution"]["resolutionStatus"],
                "partial",
            )
            self.assertGreater(
                report["outputs"][0]["objectResolution"]["unresolvedTypeCount"],
                0,
            )
            source_sha256 = hashlib.sha256(source_bytes).hexdigest()
            cooked_map = json.loads(
                (first_output / first.outputs[0].path / "map.json").read_text(
                    encoding="utf-8"
                )
            )
            self.assertEqual(
                cooked_map["id"],
                f"openbfme.retail-map.{source_sha256}",
            )
            self.assertEqual(
                cooked_map["displayName"],
                f"Retail map {source_sha256[:12]}",
            )
            self.assertNotEqual(cooked_map["displayName"], "Fords of Isen II")
            self.assertEqual(report["entries"][0]["mapKind"], "multiplayer")
            self.assertEqual(report["entries"][0]["profileVersion"], 1)
            self.assertTrue(report["entries"][0]["runnable"])
            self.assertFalse(_contains_bytes(report))
            encoded = json.dumps(report, allow_nan=False, sort_keys=True)
            self.assertNotIn(str(root), encoded)
            self.assertNotIn("NOT_SELECTED", encoded)
            self.assertFalse(
                any(
                    path.suffix.casefold() == ".map" for path in first_output.rglob("*")
                )
            )

    def test_mixed_profiles_build_and_dedupe_only_within_exact_profile(self) -> None:
        multiplayer, _ = _synthetic_map(object_type_names=("MultiplayerTree",))
        scenario, _ = _synthetic_map(
            start_name="Scenario_Entry",
            object_type_names=("ScenarioTree",),
        )
        structural, _ = _synthetic_map(height_dimensions=(1, 1))
        files = {
            "maps/a/multiplayer.map": multiplayer,
            "maps/b/scenario.map": scenario,
            "maps/c/library.map": structural,
            "maps/d/placeholder.map": structural,
            "maps/e/placeholder-copy.map": structural,
        }
        profiles = {
            "maps/b/scenario.map": "scenario",
            "maps/c/library.map": "library",
            "maps/d/placeholder.map": "placeholder",
            "maps/e/placeholder-copy.map": "placeholder",
        }
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            effective = root / "effective"
            output = root / "output"
            _write_corpus(effective, files)
            with mock.patch.object(
                map_native_module,
                "convert_sage_map",
                wraps=map_native_module.convert_sage_map,
            ) as converter:
                report = build_map_native_corpus(
                    effective,
                    output,
                    profiles=profiles,
                )

            self.assertEqual(converter.call_count, 4)
            self.assertEqual(
                [call.kwargs["profile"] for call in converter.call_args_list],
                ["multiplayer", "scenario", "library", "placeholder"],
            )
            self.assertEqual(len(report.entries), 5)
            self.assertEqual(len(report.outputs), 4)
            summary = report.neutral()["summary"]
            self.assertTrue(summary["structuralConversionComplete"])
            self.assertEqual(summary["runnableMapCount"], 2)
            self.assertEqual(summary["nonRunnableMapCount"], 3)
            self.assertFalse(summary["gameplayFidelityClaimed"])

            by_path = {item.source_path: item for item in report.entries}
            self.assertNotEqual(
                by_path["maps/c/library.map"].output_path,
                by_path["maps/d/placeholder.map"].output_path,
            )
            self.assertEqual(
                by_path["maps/d/placeholder.map"].output_path,
                by_path["maps/e/placeholder-copy.map"].output_path,
            )
            for output_entry in report.outputs:
                self.assertIn(
                    f"/profile/{output_entry.profile}-v{output_entry.profile_version}",
                    output_entry.path,
                )
                cooked = json.loads(
                    (output / output_entry.path / "map.json").read_text("utf-8")
                )
                self.assertEqual(cooked["mapKind"], output_entry.profile)
                self.assertEqual(cooked["profileVersion"], output_entry.profile_version)
                self.assertEqual(cooked["runnable"], output_entry.runnable)
                if output_entry.profile != "multiplayer":
                    self.assertEqual(
                        cooked["id"],
                        (
                            f"openbfme.content-map.{output_entry.source_sha256}."
                            f"{output_entry.profile}.v{output_entry.profile_version}"
                        ),
                    )
                    self.assertIn(
                        output_entry.source_sha256[:12], cooked["displayName"]
                    )
                    self.assertNotIn("Fords", cooked["displayName"])
                    self.assertNotIn("Retail", cooked["displayName"])

    def test_profile_selection_binds_default_reuse_request_and_identity(self) -> None:
        source_bytes, _ = _synthetic_map()
        source_path = "maps/a/a.map"
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            effective = root / "effective"
            default_output = root / "default"
            explicit_output = root / "explicit"
            scenario_output = root / "scenario"
            _write_corpus(effective, {source_path: source_bytes})

            default = build_map_native_corpus(effective, default_output)
            explicit = build_map_native_corpus(
                effective,
                explicit_output,
                profiles={source_path: "multiplayer"},
            )
            reused = build_map_native_corpus(
                effective,
                default_output,
                profiles={source_path: "multiplayer"},
            )
            scenario = build_map_native_corpus(
                effective,
                scenario_output,
                profiles={source_path: "scenario"},
            )

            self.assertEqual(default.neutral(), explicit.neutral())
            self.assertTrue(reused.reused)
            self.assertEqual(default.request_sha256, explicit.request_sha256)
            self.assertNotEqual(default.request_sha256, scenario.request_sha256)
            self.assertNotEqual(default.identity_sha256, scenario.identity_sha256)
            self.assertNotEqual(default.outputs[0].path, scenario.outputs[0].path)
            with self.assertRaises(MapNativeCorpusReuseError):
                build_map_native_corpus(
                    effective,
                    default_output,
                    profiles={source_path: "scenario"},
                )
            with self.assertRaisesRegex(MapNativeCorpusError, "unknown manifest path"):
                build_map_native_corpus(
                    effective,
                    root / "invalid",
                    profiles={"maps/missing.map": "scenario"},
                )

    def test_every_declared_map_is_recorded_and_duplicate_content_is_deduplicated(
        self,
    ) -> None:
        alpha, _ = _synthetic_map(object_type_names=("AlphaTree",))
        beta, _ = _synthetic_map(object_type_names=("BetaTree",))
        gamma, _ = _synthetic_map(compressed=False)
        files = {
            "maps/a/a.map": alpha,
            "maps/b/b.map": beta,
            "maps/c/c.map": alpha,
            "maps/d/d.MAP": gamma,
            "maps/d/readme.txt": b"ignored",
        }
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            effective = root / "effective"
            output = root / "output"
            _write_corpus(effective, files)
            with mock.patch.object(
                map_native_module,
                "convert_sage_map",
                wraps=map_native_module.convert_sage_map,
            ) as converter:
                report = build_map_native_corpus(effective, output)

        self.assertEqual(converter.call_count, 3)
        self.assertEqual(len(report.entries), 4)
        self.assertEqual(len(report.outputs), 3)
        self.assertEqual(
            [item.source_path for item in report.entries],
            ["maps/a/a.map", "maps/b/b.map", "maps/c/c.map", "maps/d/d.MAP"],
        )
        self.assertTrue(all(item.accepted for item in report.entries))
        by_path = {item.source_path: item for item in report.entries}
        self.assertEqual(
            by_path["maps/a/a.map"].output_path,
            by_path["maps/c/c.map"].output_path,
        )
        self.assertEqual(
            {item.source_sha256 for item in report.outputs},
            {
                hashlib.sha256(alpha).hexdigest(),
                hashlib.sha256(beta).hexdigest(),
                hashlib.sha256(gamma).hexdigest(),
            },
        )

    def test_conversion_failure_rolls_back_and_redacts_exception_text(self) -> None:
        alpha, _ = _synthetic_map(object_type_names=("AlphaTree",))
        beta, _ = _synthetic_map(object_type_names=("BetaTree",))
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            effective = root / "effective"
            output = root / "corpus"
            _write_corpus(
                effective,
                {"maps/a/a.map": alpha, "maps/b/b.map": beta},
            )
            build_map_native_corpus(effective, output)
            before = _tree_snapshot(output)
            real_convert = map_native_module.convert_sage_map
            calls = 0

            def failing_convert(*args: object, **kwargs: object) -> object:
                nonlocal calls
                calls += 1
                if calls == 2:
                    raise SageMapError(
                        "DO_NOT_EXPORT_PARSE_DETAIL object Secret at (12, 34)"
                    )
                return real_convert(*args, **kwargs)

            with (
                mock.patch.object(
                    map_native_module,
                    "convert_sage_map",
                    side_effect=failing_convert,
                ),
                self.assertRaises(MapNativeCorpusBuildError) as caught,
            ):
                build_map_native_corpus(effective, output, force=True)

            self.assertEqual(calls, 2)
            self.assertEqual(_tree_snapshot(output), before)
            failure = caught.exception.report
            self.assertFalse(failure.published)
            self.assertFalse(failure.complete)
            self.assertEqual(failure.neutral()["summary"]["acceptedMapCount"], 1)
            self.assertEqual(failure.neutral()["summary"]["rejectedMapCount"], 1)
            self.assertEqual(
                [item.rejection_code for item in failure.entries],
                [None, "sage-map-conversion-rejected"],
            )
            self.assertNotIn(
                "DO_NOT_EXPORT_PARSE_DETAIL",
                json.dumps(failure.neutral(), sort_keys=True),
            )
            self.assertFalse(any(root.glob(".corpus.staging-*")))
            self.assertFalse(any(root.glob(".corpus.backup-*")))

    def test_publish_failure_restores_prior_output_atomically(self) -> None:
        source_bytes, _ = _synthetic_map()
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            effective = root / "effective"
            output = root / "corpus"
            _write_corpus(effective, {"maps/a/a.map": source_bytes})
            build_map_native_corpus(effective, output)
            before = _tree_snapshot(output)
            real_replace = map_native_module.os.replace

            def flaky_replace(source: object, target: object) -> object:
                source_path = Path(source)  # type: ignore[arg-type]
                target_path = Path(target)  # type: ignore[arg-type]
                if (
                    source_path.name.startswith(".corpus.staging-")
                    and target_path == output
                ):
                    raise OSError("synthetic publish failure")
                return real_replace(source, target)

            with (
                mock.patch.object(
                    map_native_module.os,
                    "replace",
                    side_effect=flaky_replace,
                ),
                self.assertRaisesRegex(
                    MapNativeCorpusError, "prior output was preserved"
                ),
            ):
                build_map_native_corpus(effective, output, force=True)

            self.assertEqual(_tree_snapshot(output), before)
            self.assertFalse(any(root.glob(".corpus.staging-*")))
            self.assertFalse(any(root.glob(".corpus.backup-*")))

    def test_map_parse_and_native_backtest_rejections_abort_publish(self) -> None:
        valid, _ = _synthetic_map()
        with self.subTest("map parse rejection"):
            with tempfile.TemporaryDirectory() as raw:
                root = Path(raw)
                effective = root / "effective"
                output = root / "output"
                _write_corpus(
                    effective,
                    {
                        "maps/a/broken.map": b"BROKEN_MAP_PAYLOAD",
                        "maps/b/valid.map": valid,
                    },
                )
                with self.assertRaises(MapNativeCorpusBuildError) as caught:
                    build_map_native_corpus(effective, output)
                report = caught.exception.report
                self.assertEqual(len(report.entries), 2)
                self.assertEqual(
                    [item.rejection_code for item in report.entries],
                    ["map-corpus:census-parse-rejected", None],
                )
                self.assertFalse(output.exists())
                self.assertNotIn(
                    "BROKEN_MAP_PAYLOAD", json.dumps(report.neutral(), sort_keys=True)
                )

        with self.subTest("native backtest rejection"):
            with tempfile.TemporaryDirectory() as raw:
                root = Path(raw)
                effective = root / "effective"
                output = root / "output"
                _write_corpus(effective, {"maps/a/a.map": valid})
                rejected_evidence = {
                    "schema": "openbfme.native-output-backtest",
                    "schemaVersion": 0,
                    "family": "cooked-sage-map",
                    "valid": False,
                    "errors": ["DO_NOT_EXPORT_BACKTEST_DETAIL"],
                    "errorCount": 1,
                }
                with (
                    mock.patch.object(
                        map_native_module,
                        "validate_cooked_sage_map",
                        return_value=rejected_evidence,
                    ),
                    self.assertRaises(MapNativeCorpusBuildError) as caught,
                ):
                    build_map_native_corpus(effective, output)
                report = caught.exception.report
                self.assertEqual(
                    report.entries[0].rejection_code, "native-backtest-rejected"
                )
                self.assertFalse(output.exists())
                self.assertNotIn(
                    "DO_NOT_EXPORT_BACKTEST_DETAIL",
                    json.dumps(report.neutral(), sort_keys=True),
                )

    def test_limits_never_truncate_or_create_partial_output(self) -> None:
        alpha, _ = _synthetic_map(object_type_names=("AlphaTree",))
        beta, _ = _synthetic_map(object_type_names=("BetaTree",))
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            effective = root / "effective"
            _write_corpus(
                effective,
                {"maps/a/a.map": alpha, "maps/b/b.map": beta},
            )
            with self.assertRaisesRegex(MapNativeCorpusLimitError, "file count"):
                build_map_native_corpus(effective, root / "files", max_files=1)
            with self.assertRaisesRegex(MapNativeCorpusLimitError, "total bytes"):
                build_map_native_corpus(
                    effective,
                    root / "bytes",
                    max_total_bytes=len(alpha) + len(beta) - 1,
                )
            with self.assertRaises(TypeError):
                build_map_native_corpus(effective, root / "bad", max_files=True)
            with self.assertRaises(ValueError):
                build_map_native_corpus(effective, root / "bad", max_files=0)
            self.assertFalse((root / "files").exists())
            self.assertFalse((root / "bytes").exists())

    def test_unsafe_manifest_paths_and_overlapping_output_are_rejected(self) -> None:
        source_bytes, _ = _synthetic_map()
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            effective = root / "effective"
            output = root / "output"
            _write_corpus(effective, {"maps/a/a.map": source_bytes})
            manifest_path = effective / ".openbfme" / "manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["files"][0]["path"] = "../escape.map"
            manifest["aggregate_sha256"] = _aggregate(manifest["files"])
            manifest_path.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(MapNativeCorpusError, "unsafe path"):
                build_map_native_corpus(effective, output)
            self.assertFalse(output.exists())

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            effective = root / "effective"
            _write_corpus(effective, {"maps/a/a.map": source_bytes})
            with self.assertRaisesRegex(MapNativeCorpusError, "must not overlap"):
                build_map_native_corpus(effective, effective / "private-output")
            self.assertFalse((effective / "private-output").exists())

    def test_source_and_output_tamper_are_detected_without_replacement(self) -> None:
        source_bytes, _ = _synthetic_map()
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            effective = root / "effective"
            output = root / "corpus"
            _write_corpus(effective, {"maps/a/a.map": source_bytes})
            report = build_map_native_corpus(effective, output)
            output_file = output.joinpath(
                *report.outputs[0].path.split("/"),
                report.outputs[0].inventory[0].path,
            )
            output_file.write_bytes(output_file.read_bytes() + b"tamper")
            with self.assertRaises(MapNativeCorpusReuseError):
                build_map_native_corpus(effective, output)

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            effective = root / "effective"
            output = root / "corpus"
            _write_corpus(effective, {"maps/a/a.map": source_bytes})
            build_map_native_corpus(effective, output)
            before = _tree_snapshot(output)
            source_path = effective / "maps" / "a" / "a.map"
            tampered = bytearray(source_path.read_bytes())
            tampered[-1] ^= 1
            source_path.write_bytes(tampered)
            with self.assertRaisesRegex(MapNativeCorpusError, "SHA-256"):
                build_map_native_corpus(effective, output, force=True)
            self.assertEqual(_tree_snapshot(output), before)


if __name__ == "__main__":
    unittest.main()
