from __future__ import annotations

from dataclasses import replace
import hashlib
import json
import struct
import unittest
from unittest.mock import patch

import openbfme_importer.w3d_texture_closure as texture_closure_module
from openbfme_importer.w3d_catalog import scan_w3d_catalog
from openbfme_importer.w3d_job_planner import plan_w3d_batches
from openbfme_importer.w3d_texture_closure import (
    W3DModelTextureClosure,
    W3DTextureClosureError,
    plan_w3d_texture_closure,
    texture_closure_forced_terminals,
)


_TEXTURE_EXTENSIONS = {".bmp", ".dds", ".jpeg", ".jpg", ".pcx", ".png", ".tga"}


def _canonical(
    value: object, *, pretty: bool = False, ensure_ascii: bool = True
) -> bytes:
    options: dict[str, object] = {
        "ensure_ascii": ensure_ascii,
        "allow_nan": False,
        "sort_keys": True,
    }
    if pretty:
        options["indent"] = 2
    else:
        options["separators"] = (",", ":")
    return (json.dumps(value, **options) + "\n").encode()


def _seal(value: object) -> str:
    return hashlib.sha256(_canonical(value)).hexdigest()


def _document_seal(value: object, *, ensure_ascii: bool = True) -> str:
    return hashlib.sha256(
        _canonical(value, pretty=True, ensure_ascii=ensure_ascii)
    ).hexdigest()


def _fixed(value: str, size: int) -> bytes:
    encoded = value.encode("ascii")
    if len(encoded) >= size:
        raise ValueError("fixture identifier is too long")
    return encoded + b"\0" * (size - len(encoded))


def _chunk(chunk_id: int, payload: bytes, *, children: bool = False) -> bytes:
    return (
        struct.pack("<II", chunk_id, len(payload) | (0x80000000 if children else 0))
        + payload
    )


def _model(name: str, *textures: str, extra_chunks: bytes = b"") -> bytes:
    model_header = struct.pack(
        "<II16s16s",
        0x00040001,
        1,
        _fixed(name, 16),
        _fixed("", 16),
    )
    mesh_header = struct.pack(
        "<II16s16s9I10f",
        0x00040002,
        0x0002A000,
        _fixed("BODY", 16),
        _fixed(name, 16),
        1,
        3,
        1,
        1,
        0,
        0,
        0,
        0,
        0,
        *[float(value) for value in range(10)],
    )
    texture_chunks = b"".join(
        _chunk(
            0x30,
            _chunk(0x31, _chunk(0x32, value.encode("ascii") + b"\0"), children=True),
            children=True,
        )
        for value in textures
    )
    return (
        _chunk(0x700, _chunk(0x701, model_header), children=True)
        + _chunk(0x1F, mesh_header)
        + _chunk(0x02, b"\0" * 36)
        + _chunk(0x20, b"\0" * 32)
        + texture_chunks
        + extra_chunks
    )


def _shader_string_property(name: str, value: str) -> bytes:
    raw_name = name.encode("cp1252") + b"\0"
    raw_value = value.encode("cp1252") + b"\0"
    return (
        struct.pack("<ii", 1, len(raw_name))
        + raw_name
        + struct.pack("<i", len(raw_value))
        + raw_value
    )


def _shader_material(*properties: tuple[str, str]) -> bytes:
    payload = _chunk(
        0x52,
        struct.pack("<B32si", 1, _fixed("Fixture.fx", 32), 0),
    )
    payload += b"".join(
        _chunk(0x53, _shader_string_property(name, value)) for name, value in properties
    )
    return _chunk(0x51, payload, children=True)


def _dazzle() -> bytes:
    return _chunk(
        0x900,
        _chunk(0x901, b"FixtureDazzle\0") + _chunk(0x902, b"FixtureDazzleType\0"),
        children=True,
    )


def _aggregate(files: list[dict[str, object]]) -> str:
    digest = hashlib.sha256()
    for item in files:
        digest.update(str(item["path"]).encode())
        digest.update(b"\0")
        digest.update(str(item["size"]).encode())
        digest.update(b"\0")
        digest.update(str(item["sha256"]).encode())
        digest.update(b"\n")
    return digest.hexdigest()


def _effective(files: dict[str, bytes]) -> dict[str, object]:
    inventory: list[dict[str, object]] = []
    offset = 64
    for precedence, (path, payload) in enumerate(
        sorted(files.items(), key=lambda item: (item[0].casefold(), item[0]))
    ):
        inventory.append(
            {
                "archive": "retail/assets.big",
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
        "install": {"identity_sha256": "b" * 64, "root": "retail-install"},
        "totals": {
            "bytes": sum(len(payload) for payload in files.values()),
            "files": len(files),
        },
        "aggregate_sha256": _aggregate(inventory),
        "files": inventory,
    }


def _native_request(document: dict[str, object]) -> str:
    selection = document["selection"]
    assert isinstance(selection, dict)
    entries = document["entries"]
    reclassified = document["reclassified"]
    assert isinstance(entries, list)
    assert isinstance(reclassified, list)
    sources = [
        {
            "path": item["sourcePath"],
            "bytes": item["sourceBytes"],
            "sha256": item["sourceSha256"],
            "family": "texture",
            "extension": item["sourceExtension"],
            "disposition": "media-conversion",
        }
        for item in entries
    ]
    sources.extend(
        {
            "path": item["sourcePath"],
            "bytes": item["sourceBytes"],
            "sha256": item["sourceSha256"],
            "family": "texture",
            "extension": item["originalExtension"],
            "disposition": "map-payload",
            "detectedKind": item["detectedKind"],
            "evidenceSha256": item["evidenceSha256"],
        }
        for item in reclassified
    )
    sources.sort(key=lambda item: (str(item["path"]).casefold(), str(item["path"])))
    return _seal(
        {
            "schema": "openbfme.native-corpus-request",
            "schemaVersion": 0,
            "families": ["texture"],
            "sourceManifestSha256": selection["sourceManifestSha256"],
            "sourceManifestAggregateSha256": selection["sourceManifestAggregateSha256"],
            "conversion": {},
            "sources": sources,
        }
    )


def _reseal_native(document: dict[str, object]) -> None:
    selection = document["selection"]
    assert isinstance(selection, dict)
    selection["requestSha256"] = _native_request(document)
    basis = {key: value for key, value in document.items() if key != "identitySha256"}
    document["identitySha256"] = _seal(basis)


def _native(
    effective: dict[str, object],
    *,
    converted: list[str] | None = None,
    reclassified: list[str] | None = None,
    output_sha256s: dict[str, str] | None = None,
) -> dict[str, object]:
    files = effective["files"]
    assert isinstance(files, list)
    by_path = {str(item["path"]): item for item in files}
    all_textures = [
        path
        for path in by_path
        if "." + path.rsplit(".", 1)[-1].casefold() in _TEXTURE_EXTENSIONS
    ]
    selected = all_textures if converted is None else converted
    reclassified = reclassified or []
    outputs_by_path: dict[str, dict[str, object]] = {}
    entries: list[dict[str, object]] = []
    for path in sorted(selected, key=lambda item: (item.casefold(), item)):
        source = by_path[path]
        digest = (
            output_sha256s[path]
            if output_sha256s and path in output_sha256s
            else hashlib.sha256(("png:" + path).encode()).hexdigest()
        )
        output_path = f"objects/sha256/{digest[:2]}/{digest}.png"
        evidence = {"family": "png", "sha256": digest, "size": 19, "valid": True}
        outputs_by_path[output_path] = {
            "path": output_path,
            "bytes": 19,
            "sha256": digest,
            "nativeFamily": "png",
            "evidence": evidence,
        }
        entries.append(
            {
                "sourcePath": path,
                "sourceArchive": source["archive"],
                "sourceExtension": "." + path.rsplit(".", 1)[-1].casefold(),
                "sourceBytes": source["size"],
                "sourceSha256": source["sha256"],
                "family": "texture",
                "outputPath": output_path,
                "outputBytes": 19,
                "outputSha256": digest,
                "nativeFamily": "png",
                "evidence": evidence,
            }
        )
    reclassified_rows = []
    for path in sorted(reclassified, key=lambda item: (item.casefold(), item)):
        source = by_path[path]
        reclassified_rows.append(
            {
                "sourcePath": path,
                "sourceBytes": source["size"],
                "sourceSha256": source["sha256"],
                "originalFamily": "texture",
                "originalExtension": "." + path.rsplit(".", 1)[-1].casefold(),
                "classification": "map-payload",
                "detectedKind": "uncompressed",
                "evidenceSha256": "c" * 64,
            }
        )
    outputs = sorted(
        outputs_by_path.values(), key=lambda item: str(item["path"]).casefold()
    )
    converted_bytes = sum(int(item["sourceBytes"]) for item in entries)
    reclassified_bytes = sum(int(item["sourceBytes"]) for item in reclassified_rows)
    document: dict[str, object] = {
        "schema": "openbfme.native-corpus",
        "schemaVersion": 0,
        "selection": {
            "families": ["texture"],
            "sourceManifestSha256": _document_seal(effective, ensure_ascii=False),
            "sourceManifestAggregateSha256": effective["aggregate_sha256"],
            "requestSha256": "",
            "conversion": {},
        },
        "totals": {
            "candidateFileCount": len(entries) + len(reclassified_rows),
            "candidateBytes": converted_bytes + reclassified_bytes,
            "convertedFileCount": len(entries),
            "convertedBytes": converted_bytes,
            "reclassifiedFileCount": len(reclassified_rows),
            "reclassifiedBytes": reclassified_bytes,
            "outputFileCount": len(outputs),
            "outputBytes": sum(int(item["bytes"]) for item in outputs),
        },
        "entries": entries,
        "reclassified": reclassified_rows,
        "outputs": outputs,
        "identitySha256": "",
    }
    _reseal_native(document)
    return document


def _inputs(
    values: dict[str, bytes], *, same_directory: bool = False
) -> dict[str, str]:
    result = {}
    for index, path in enumerate(
        sorted(values, key=lambda item: (item.casefold(), item))
    ):
        directory = "stage" if same_directory else f"stage/{index:04d}"
        result[path] = f"{directory}/{path.rsplit('/', 1)[-1]}"
    return result


def _plan(
    values: dict[str, bytes],
    textures: dict[str, bytes],
    *,
    converted: list[str] | None = None,
    reclassified: list[str] | None = None,
    staged: dict[str, str] | None = None,
    output_sha256s: dict[str, str] | None = None,
):
    report = scan_w3d_catalog(values)
    effective = _effective({**values, **textures})
    native = _native(
        effective,
        converted=converted,
        reclassified=reclassified,
        output_sha256s=output_sha256s,
    )
    return plan_w3d_texture_closure(
        report,
        staged or _inputs(values),
        effective,
        native,
    )


class W3DTextureClosureTests(unittest.TestCase):
    def test_texture_resolution_indexes_the_manifest_once(self) -> None:
        references = tuple(f"Tex{index:04d}.dds" for index in range(16))
        textures = {
            f"Art/Textures/Tex{index:04d}.dds": str(index).encode()
            for index in range(512)
        }
        with patch.object(
            texture_closure_module,
            "_without_extension",
            wraps=texture_closure_module._without_extension,
        ) as without_extension:
            plan = _plan(
                {"Models/Indexed.w3d": _model("Indexed", *references)},
                textures,
            )

        self.assertTrue(plan.complete)
        self.assertEqual(len(plan.copy_instructions), len(references))
        self.assertLessEqual(
            without_extension.call_count,
            len(textures) + len(references),
        )

    def test_exact_path_and_private_destination_preserve_referenced_layout(
        self,
    ) -> None:
        values = {"Models/Hero.w3d": _model("Hero", "Art/Textures/Hero_D.dds")}
        plan = _plan(values, {"Art/Textures/Hero_D.dds": b"dds"})

        self.assertTrue(plan.complete)
        self.assertEqual(len(plan.copy_instructions), 1)
        copy = plan.copy_instructions[0]
        self.assertEqual(
            copy.destination_path,
            "stage/0000/Art/Textures/Hero_D.png",
        )
        self.assertTrue(copy.source_output_path.endswith(".png"))
        self.assertEqual(plan.models[0].resolved_reference_count, 1)
        self.assertEqual(
            plan.private_plan_sha256,
            "d610e37e3e99229bc794fb7cd19edd1343422fd343e677c38bd527491ef1d154",
        )
        self.assertEqual(
            plan.evidence_sha256,
            "842364a927eb711c2ddff7638ef26348583895215eec9feb91055ef5dddb9ce5",
        )
        neutral = plan.neutral()
        self.assertEqual(neutral["schemaVersion"], 1)
        self.assertEqual(
            neutral["hashes"]["textureDependencyPolicy"],
            texture_closure_module.TEXTURE_DEPENDENCY_POLICY,
        )

    def test_pinned_shader_texture_properties_are_closed(self) -> None:
        properties = (
            ("DiffuseTexture", "Diffuse.dds"),
            ("NormalMap", "Normal.dds"),
            ("SpecMap", "Spec.dds"),
            ("Texture_0", "Primary.dds"),
            ("Texture_1", "Secondary.dds"),
            ("DamagedTexture", "Damaged.dds"),
        )
        textures = {
            f"Art/Textures/{identifier}": identifier.encode("ascii")
            for _name, identifier in properties
        }
        plan = _plan(
            {
                "Models/Shader.w3d": _model(
                    "Shader",
                    extra_chunks=_shader_material(*properties),
                )
            },
            textures,
        )

        self.assertTrue(plan.complete)
        self.assertEqual(plan.models[0].texture_reference_count, 6)
        self.assertEqual(plan.models[0].resolved_reference_count, 6)
        self.assertEqual(len(plan.copy_instructions), 6)
        self.assertEqual(
            sorted(item.destination_path for item in plan.copy_instructions),
            sorted(
                f"stage/0000/{identifier.removesuffix('.dds')}.png"
                for _name, identifier in properties
            ),
        )

    def test_shader_texture_consumer_conditions_match_pinned_importer(self) -> None:
        conditional_empty = _plan(
            {
                "Models/Conditional.w3d": _model(
                    "Conditional",
                    extra_chunks=_shader_material(("DiffuseTexture", "")),
                )
            },
            {},
            converted=[],
        )
        unconditional_empty = _plan(
            {
                "Models/Unconditional.w3d": _model(
                    "Unconditional",
                    extra_chunks=_shader_material(("Texture_0", "")),
                )
            },
            {},
            converted=[],
        )
        unrelated = _plan(
            {
                "Models/Unrelated.w3d": _model(
                    "Unrelated",
                    extra_chunks=_shader_material(
                        ("Arbitrary", "NotATexture.dds"),
                    ),
                )
            },
            {},
            converted=[],
        )

        self.assertTrue(conditional_empty.complete)
        self.assertTrue(conditional_empty.models[0].texture_free)
        self.assertEqual(
            unconditional_empty.models[0].terminal_reasons,
            ("unsafe-texture-identifier",),
        )
        self.assertTrue(unrelated.complete)
        self.assertTrue(unrelated.models[0].texture_free)

    def test_unbound_shader_texture_property_is_terminal(self) -> None:
        identifier = "Unbound.dds"
        plan = _plan(
            {
                "Models/Unbound.w3d": _model(
                    "Unbound",
                    extra_chunks=_chunk(
                        0x53,
                        _shader_string_property("DiffuseTexture", identifier),
                    ),
                )
            },
            {f"Art/Textures/{identifier}": b"dds"},
        )

        self.assertFalse(plan.complete)
        self.assertEqual(
            plan.models[0].terminal_reasons,
            ("unbound-shader-texture-property",),
        )
        self.assertFalse(plan.copy_instructions)

    def test_dazzle_implicit_texture_dependency_is_closed(self) -> None:
        plan = _plan(
            {
                "Models/Dazzle.w3d": _model(
                    "Dazzle",
                    extra_chunks=_dazzle(),
                )
            },
            {"Art/Textures/SunDazzle.tga": b"tga"},
        )

        self.assertTrue(plan.complete)
        self.assertEqual(plan.models[0].texture_reference_count, 1)
        self.assertEqual(
            plan.copy_instructions[0].destination_path,
            "stage/0000/SunDazzle.png",
        )

    def test_missing_dazzle_implicit_texture_is_terminal(self) -> None:
        plan = _plan(
            {
                "Models/Dazzle.w3d": _model(
                    "Dazzle",
                    extra_chunks=_dazzle(),
                )
            },
            {},
            converted=[],
        )

        self.assertFalse(plan.complete)
        self.assertEqual(
            plan.models[0].terminal_reasons,
            ("missing-texture-source",),
        )

    def test_unicode_source_ids_match_planner_and_terminals_bridge_exactly(
        self,
    ) -> None:
        values = {
            "Models/\u00dcber/\u6a21\u578b.w3d": _model(
                "Unicode",
                "Missing.dds",
                "../Unsafe.dds",
            )
        }
        report = scan_w3d_catalog(values)
        staged = _inputs(values)
        effective = _effective(values)
        texture_plan = plan_w3d_texture_closure(
            report,
            staged,
            effective,
            _native(effective, converted=[]),
        )
        baseline = plan_w3d_batches(report, staged, index=report.index)
        self.assertEqual(len(baseline.jobs), 1)
        self.assertEqual(
            texture_plan.models[0].model_source_id,
            baseline.jobs[0].model_source_id,
        )

        forced, evidence_sha256 = texture_closure_forced_terminals(texture_plan)
        self.assertEqual(
            forced,
            {
                texture_plan.models[0].model_source_id: (
                    "texture-closure-missing-texture-source",
                    "texture-closure-unsafe-texture-identifier",
                )
            },
        )
        self.assertRegex(evidence_sha256, r"^[0-9a-f]{64}$")
        self.assertEqual(
            texture_closure_forced_terminals(texture_plan),
            (forced, evidence_sha256),
        )

        job_plan = plan_w3d_batches(
            report,
            staged,
            index=report.index,
            forced_terminal_reasons=forced,
            forced_terminal_evidence_sha256=evidence_sha256,
        )
        self.assertFalse(job_plan.jobs)
        self.assertEqual(
            job_plan.terminals[0].reason_codes,
            forced[texture_plan.models[0].model_source_id],
        )
        self.assertEqual(
            job_plan.forced_terminal_evidence_sha256,
            evidence_sha256,
        )

    def test_forced_terminal_bridge_attests_complete_empty_mapping(self) -> None:
        plan = _plan({"Models/A.w3d": _model("A")}, {}, converted=[])

        forced, evidence_sha256 = texture_closure_forced_terminals(plan)

        self.assertEqual(forced, {})
        self.assertRegex(evidence_sha256, r"^[0-9a-f]{64}$")

    def test_forced_terminal_bridge_rejects_malformed_model_evidence(self) -> None:
        plan = _plan(
            {"Models/A.w3d": _model("A", "Missing.dds")},
            {},
            converted=[],
        )
        model = plan.models[0]

        with self.assertRaisesRegex(W3DTextureClosureError, "repeats model"):
            texture_closure_forced_terminals(replace(plan, models=(model, model)))
        with self.assertRaisesRegex(W3DTextureClosureError, "not canonical"):
            texture_closure_forced_terminals(
                replace(
                    plan,
                    models=(replace(model, terminal_reasons=("Not-Canonical",)),),
                )
            )
        with self.assertRaisesRegex(W3DTextureClosureError, "evidence SHA-256"):
            texture_closure_forced_terminals(replace(plan, evidence_sha256="f" * 64))
        with self.assertRaisesRegex(TypeError, "requires a texture plan"):
            texture_closure_forced_terminals(object())  # type: ignore[arg-type]

        malformed = W3DModelTextureClosure(
            model_source_id=model.model_source_id.upper(),
            model_source_sha256=model.model_source_sha256,
            staged_model_path=model.staged_model_path,
            texture_reference_count=model.texture_reference_count,
            resolved_reference_count=model.resolved_reference_count,
            instruction_ids=model.instruction_ids,
            reference_evidence_sha256s=model.reference_evidence_sha256s,
            terminal_reasons=model.terminal_reasons,
        )
        with self.assertRaisesRegex(W3DTextureClosureError, "source ID"):
            texture_closure_forced_terminals(replace(plan, models=(malformed,)))

    def test_unique_basename_and_extension_fallback_are_deterministic(self) -> None:
        basename = _plan(
            {"Models/A.w3d": _model("A", "Unique.dds")},
            {"Art/Textures/Unique.dds": b"dds"},
        )
        extension = _plan(
            {"Models/A.w3d": _model("A", "Art/Textures/Unique.tga")},
            {"Art/Textures/Unique.dds": b"dds"},
        )

        self.assertTrue(basename.complete)
        self.assertTrue(extension.complete)
        self.assertTrue(
            basename.copy_instructions[0].destination_path.endswith("/Unique.png")
        )
        self.assertEqual(
            extension.copy_instructions[0].destination_path,
            "stage/0000/Art/Textures/Unique.png",
        )
        again = _plan(
            {"Models/A.w3d": _model("A", "Art/Textures/Unique.tga")},
            {"Art/Textures/Unique.dds": b"dds"},
        )
        self.assertEqual(extension.private_plan_sha256, again.private_plan_sha256)
        self.assertEqual(extension.evidence_sha256, again.evidence_sha256)

    def test_reference_subdirectories_prevent_same_stem_destination_collision(
        self,
    ) -> None:
        plan = _plan(
            {
                "Models/A.w3d": _model(
                    "A",
                    "Art/A/Shared.dds",
                    "Art/B/Shared.dds",
                )
            },
            {
                "Art/A/Shared.dds": b"a",
                "Art/B/Shared.dds": b"b",
            },
        )

        self.assertTrue(plan.complete)
        self.assertEqual(
            sorted(item.destination_path for item in plan.copy_instructions),
            [
                "stage/0000/Art/A/Shared.png",
                "stage/0000/Art/B/Shared.png",
            ],
        )

    def test_reference_destination_over_path_bound_is_terminal(self) -> None:
        model_path = "Models/A.w3d"
        reference = f"Textures/{'t' * 250}.dds"
        staged_model = f"stage/{'m' * 250}/A.w3d"
        plan = _plan(
            {model_path: _model("A", reference)},
            {reference: b"dds"},
            staged={model_path: staged_model},
        )

        self.assertFalse(plan.complete)
        self.assertEqual(
            plan.models[0].terminal_reasons,
            ("unsafe-texture-destination",),
        )
        self.assertFalse(plan.copy_instructions)

    def test_ambiguous_missing_and_unsafe_references_are_terminals(self) -> None:
        ambiguous = _plan(
            {"Models/A.w3d": _model("A", "Shared.tga")},
            {"Art/A/Shared.dds": b"a", "Art/B/Shared.tga": b"b"},
        )
        missing = _plan(
            {"Models/A.w3d": _model("A", "Absent.dds")},
            {},
            converted=[],
        )
        unsafe = _plan(
            {"Models/A.w3d": _model("A", "../Secret.dds")},
            {},
            converted=[],
        )

        self.assertEqual(
            ambiguous.models[0].terminal_reasons, ("ambiguous-texture-source",)
        )
        self.assertEqual(
            missing.models[0].terminal_reasons, ("missing-texture-source",)
        )
        self.assertEqual(
            unsafe.models[0].terminal_reasons, ("unsafe-texture-identifier",)
        )
        self.assertFalse(ambiguous.copy_instructions)
        self.assertFalse(missing.copy_instructions)
        self.assertFalse(unsafe.copy_instructions)

    def test_unconverted_and_reclassified_sources_never_enter_copy_plan(self) -> None:
        values = {"Models/A.w3d": _model("A", "Art/Texture.dds")}
        textures = {"Art/Texture.dds": b"dds"}
        unconverted = _plan(values, textures, converted=[])
        reclassified = _plan(
            values,
            textures,
            converted=[],
            reclassified=["Art/Texture.dds"],
        )

        self.assertEqual(
            unconverted.models[0].terminal_reasons, ("texture-conversion-missing",)
        )
        self.assertEqual(
            reclassified.models[0].terminal_reasons,
            ("texture-source-reclassified",),
        )
        self.assertFalse(unconverted.copy_instructions)
        self.assertFalse(reclassified.copy_instructions)

    def test_destination_conflicts_terminal_every_affected_model(self) -> None:
        values = {
            "Models/A.w3d": _model("A", "Art/Shared.dds"),
            "Models/B.w3d": _model("B", "Art/Shared.tga"),
        }
        plan = _plan(
            values,
            {"Art/Shared.dds": b"a", "Art/Shared.tga": b"b"},
            staged=_inputs(values, same_directory=True),
        )

        self.assertEqual(plan.terminal_model_count, 2)
        self.assertFalse(plan.copy_instructions)
        for model in plan.models:
            self.assertEqual(model.terminal_reasons, ("texture-destination-collision",))

    def test_identical_same_directory_requirements_deduplicate(self) -> None:
        values = {
            "Models/A.w3d": _model("A", "Art/Shared.dds"),
            "Models/B.w3d": _model("B", "Art/Shared.dds"),
        }
        plan = _plan(
            values,
            {"Art/Shared.dds": b"same"},
            staged=_inputs(values, same_directory=True),
        )

        self.assertTrue(plan.complete)
        self.assertEqual(len(plan.copy_instructions), 1)
        self.assertEqual(len(plan.copy_instructions[0].model_source_ids), 2)
        self.assertEqual(
            {model.instruction_ids for model in plan.models},
            {(plan.copy_instructions[0].instruction_id,)},
        )

    def test_texture_free_models_are_explicit_and_require_no_copy(self) -> None:
        plan = _plan({"Models/A.w3d": _model("A")}, {}, converted=[])

        self.assertTrue(plan.complete)
        self.assertEqual(plan.texture_free_model_count, 1)
        self.assertTrue(plan.models[0].texture_free)
        self.assertEqual(plan.models[0].texture_reference_count, 0)
        self.assertFalse(plan.copy_instructions)

    def test_catalog_native_source_and_stage_mismatches_fail_closed(self) -> None:
        values = {"Models/A.w3d": _model("A", "Art/A.dds")}
        report = scan_w3d_catalog(values)
        effective = _effective({**values, "Art/A.dds": b"dds"})
        native = _native(effective)

        with self.assertRaisesRegex(W3DTextureClosureError, "catalog metadata"):
            plan_w3d_texture_closure(
                replace(report, metadata_sha256="0" * 64),
                _inputs(values),
                effective,
                native,
            )
        with self.assertRaisesRegex(
            W3DTextureClosureError, "staged-input paths mismatch"
        ):
            plan_w3d_texture_closure(report, {}, effective, native)
        entries = native["entries"]
        assert isinstance(entries, list)
        entries[0]["sourceSha256"] = "f" * 64
        _reseal_native(native)
        with self.assertRaisesRegex(
            W3DTextureClosureError, "source SHA-256 mismatches"
        ):
            plan_w3d_texture_closure(
                report,
                _inputs(values),
                effective,
                native,
            )

    def test_manifest_seals_totals_and_identities_are_verified(self) -> None:
        values = {"Models/A.w3d": _model("A", "Art/A.dds")}
        report = scan_w3d_catalog(values)
        effective = _effective({**values, "Art/A.dds": b"dds"})
        native = _native(effective)

        with self.assertRaisesRegex(W3DTextureClosureError, "aggregate SHA-256"):
            damaged = {**effective, "aggregate_sha256": "0" * 64}
            plan_w3d_texture_closure(report, _inputs(values), damaged, native)
        with self.assertRaisesRegex(W3DTextureClosureError, "identity SHA-256"):
            damaged_native = {**native, "identitySha256": "0" * 64}
            plan_w3d_texture_closure(report, _inputs(values), effective, damaged_native)
        with self.assertRaisesRegex(W3DTextureClosureError, "manifest seal mismatches"):
            plan_w3d_texture_closure(
                report,
                _inputs(values),
                effective,
                native,
                effective_manifest_sha256="0" * 64,
            )

    def test_case_colliding_and_unsafe_manifest_paths_are_rejected(self) -> None:
        values = {"Models/A.w3d": _model("A")}
        report = scan_w3d_catalog(values)
        collision = _effective(
            {**values, "Art/Texture.dds": b"a", "art/texture.DDS": b"b"}
        )
        with self.assertRaisesRegex(W3DTextureClosureError, "case-collides"):
            plan_w3d_texture_closure(
                report,
                _inputs(values),
                collision,
                _native(collision),
            )

        unsafe = _effective({**values, "../Texture.dds": b"a"})
        with self.assertRaisesRegex(W3DTextureClosureError, "path is unsafe"):
            plan_w3d_texture_closure(
                report,
                _inputs(values),
                unsafe,
                _native(unsafe),
            )

    def test_optional_seals_are_bound_and_neutral_evidence_is_path_free(self) -> None:
        values = {"Models/Secret.w3d": _model("Secret", "Art/Secret.dds")}
        report = scan_w3d_catalog(values)
        effective = _effective({**values, "Art/Secret.dds": b"dds"})
        native = _native(effective)
        plan = plan_w3d_texture_closure(
            report,
            _inputs(values),
            _canonical(effective, pretty=True),
            _canonical(native, pretty=True),
            effective_manifest_sha256=_document_seal(effective, ensure_ascii=False),
            native_manifest_sha256=_document_seal(native),
            edition_seal="d" * 64,
            source_seal="e" * 64,
        )

        neutral = json.dumps(plan.neutral(), sort_keys=True)
        self.assertNotIn("Art/Secret.dds", neutral)
        self.assertNotIn("stage/", neutral)
        self.assertNotIn("sourceOutputPath", neutral)
        self.assertNotIn("destinationPath", neutral)
        self.assertIn("d" * 64, neutral)
        self.assertIn("e" * 64, neutral)
        self.assertFalse(plan.neutral()["summary"]["filesCopied"])
        self.assertFalse(plan.neutral()["summary"]["renderParityProven"])


if __name__ == "__main__":
    unittest.main()
