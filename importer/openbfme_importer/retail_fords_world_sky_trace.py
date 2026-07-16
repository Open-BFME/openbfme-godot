"""Trace the unresolved Fords world-sky selection without guessing a face set.

This is a deliberately bounded static oracle.  It joins the existing retail
skybox and water-reflection contracts to exact ``game.dat`` constructor,
parser-table, and registry-reference evidence.  Static evidence is allowed to
reject a candidate selection; it is not allowed to promote one.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
from typing import Any, Mapping

from .catalog import InstallCatalog
from .paths import safe_relative_parts
from .retail_fords_environment_profile import (
    _canonical_sha256,
    _validate_effective_manifest,
)
from .util import write_json_atomic


SCHEMA = "openbfme.retail-fords-world-sky-trace"
SCHEMA_VERSION = 0

GAMEDATA_INI_PATH = "data/ini/gamedata.ini"
SKYBOX_ORACLE_SCHEMA = "openbfme.retail-fords-skybox-oracle"
WATER_ORACLE_SCHEMA = "openbfme.retail-fords-water-reflection-oracle"

_MAX_JSON_BYTES = 32 * 1024 * 1024
_MAX_GAME_DAT_BYTES = 64 * 1024 * 1024
_MAX_TEXT_BYTES = 8 * 1024 * 1024
_EXPECTED_GAME_DAT_SHA256 = (
    "f008b587570bad693981dc7218588c81d192a1e064b0f7f861539c51156a7640"
)
_EXPECTED_IMAGE_BASE = 0x00400000
_EXPECTED_CONSTRUCTOR_VA = 0x0060D4E3
_EXPECTED_CONSTRUCTOR_CALL_VA = 0x0060D755
_EXPECTED_PARSE_CALLBACK_VA = 0x0060D6F5
_EXPECTED_REGISTRY_VA = 0x00DFF494
_EXPECTED_REGISTRY_REFS = {
    0x0060D720: "parser-create-or-find-record",
    0x00BAE6E6: "static-registry-initialization",
    0x00BB7AE1: "static-registry-teardown-thunk",
}
_FACE_FIELDS = (
    ("N", "TSMorningN.tga", 0x04),
    ("E", "TSMorningE.tga", 0x08),
    ("S", "TSMorningS.tga", 0x0C),
    ("W", "TSMorningW.tga", 0x10),
    ("T", "TSMorningT.tga", 0x14),
)


def _canonical_bytes(value: object) -> bytes:
    return json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")


def _load_json(path: Path | str, label: str) -> dict[str, Any]:
    source = Path(path).expanduser().resolve()
    if not source.is_file():
        raise ValueError(f"missing {label}: {source}")
    if source.stat().st_size > _MAX_JSON_BYTES:
        raise ValueError(f"{label} exceeds bounded size")
    value = json.loads(source.read_text("utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be an object")
    declared = value.get("aggregateSha256")
    payload = dict(value)
    payload.pop("aggregateSha256", None)
    if declared != _canonical_sha256(payload):
        raise ValueError(f"{label} aggregateSha256 is invalid")
    return value


def _load_manifest(path: Path | str) -> dict[str, Any]:
    source = Path(path).expanduser().resolve()
    if not source.is_file() or source.stat().st_size > _MAX_JSON_BYTES:
        raise ValueError("effective-assets manifest is missing or too large")
    value = json.loads(source.read_text("utf-8"))
    if not isinstance(value, dict):
        raise ValueError("effective-assets manifest must be an object")
    return value


def _verified_read(
    root: Path, virtual_path: str, files: Mapping[str, Mapping[str, Any]]
) -> bytes:
    record = files.get(virtual_path)
    if record is None:
        raise ValueError(f"effective asset is missing: {virtual_path}")
    source = root.joinpath(*safe_relative_parts(virtual_path))
    payload = source.read_bytes()
    if len(payload) != record.get("size"):
        raise ValueError(f"effective asset size changed: {virtual_path}")
    if hashlib.sha256(payload).hexdigest() != record.get("sha256"):
        raise ValueError(f"effective asset digest changed: {virtual_path}")
    return payload


def _source_row(
    virtual_path: str,
    files: Mapping[str, Mapping[str, Any]],
    catalog: InstallCatalog,
) -> dict[str, Any]:
    record = files[virtual_path]
    winner = catalog.resolve_exact(virtual_path)
    if winner is None or winner.name != virtual_path:
        raise ValueError(f"catalog has no exact-case winner: {virtual_path}")
    if winner.size != record["size"]:
        raise ValueError(f"catalog/effective size mismatch: {virtual_path}")
    return {
        "archive": winner.archive,
        "byteCount": record["size"],
        "precedence": winner.precedence,
        "sha256": record["sha256"],
        "virtualPath": virtual_path,
    }


def _occurrences(payload: bytes, needle: bytes) -> list[int]:
    result: list[int] = []
    start = 0
    while True:
        offset = payload.find(needle, start)
        if offset < 0:
            return result
        result.append(offset)
        start = offset + 1


def _pe_layout(payload: bytes) -> tuple[int, list[dict[str, int | str]]]:
    if len(payload) < 0x100 or payload[:2] != b"MZ":
        raise ValueError("game.dat is not a PE image")
    pe_offset = struct.unpack_from("<I", payload, 0x3C)[0]
    if payload[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ValueError("game.dat has no PE signature")
    coff = pe_offset + 4
    section_count = struct.unpack_from("<H", payload, coff + 2)[0]
    optional_size = struct.unpack_from("<H", payload, coff + 16)[0]
    optional = coff + 20
    if struct.unpack_from("<H", payload, optional)[0] != 0x10B:
        raise ValueError("game.dat is not PE32")
    image_base = struct.unpack_from("<I", payload, optional + 28)[0]
    section_table = optional + optional_size
    sections: list[dict[str, int | str]] = []
    for index in range(section_count):
        offset = section_table + index * 40
        name = payload[offset : offset + 8].rstrip(b"\0").decode("ascii")
        virtual_size, rva, raw_size, raw_offset = struct.unpack_from(
            "<IIII", payload, offset + 8
        )
        sections.append(
            {
                "name": name,
                "rawOffset": raw_offset,
                "rawSize": raw_size,
                "rva": rva,
                "virtualSize": virtual_size,
            }
        )
    return image_base, sections


def _raw_to_va(
    raw_offset: int, image_base: int, sections: list[dict[str, int | str]]
) -> int | None:
    for section in sections:
        start = int(section["rawOffset"])
        size = int(section["rawSize"])
        if start <= raw_offset < start + size:
            return image_base + int(section["rva"]) + raw_offset - start
    return None


def _va_to_raw(
    va: int, image_base: int, sections: list[dict[str, int | str]]
) -> int:
    rva = va - image_base
    for section in sections:
        start = int(section["rva"])
        size = max(int(section["rawSize"]), int(section["virtualSize"]))
        if start <= rva < start + size:
            return int(section["rawOffset"]) + rva - start
    raise ValueError(f"VA is outside the PE sections: 0x{va:08X}")


def _section(payload: bytes, sections: list[dict[str, int | str]], name: str) -> tuple[bytes, int, int]:
    matches = [value for value in sections if value["name"] == name]
    if len(matches) != 1:
        raise ValueError(f"expected one PE section {name}")
    section = matches[0]
    raw = int(section["rawOffset"])
    size = int(section["rawSize"])
    return payload[raw : raw + size], raw, int(section["rva"])


def _relative_call_sites(code: bytes, code_va: int, target: int) -> list[int]:
    result: list[int] = []
    for offset in range(len(code) - 4):
        if code[offset] != 0xE8:
            continue
        relative = struct.unpack_from("<i", code, offset + 1)[0]
        site = code_va + offset
        if site + 5 + relative == target:
            result.append(site)
    return result


def _static_executable_trace(game_dat: bytes) -> dict[str, Any]:
    digest = hashlib.sha256(game_dat).hexdigest()
    if digest != _EXPECTED_GAME_DAT_SHA256:
        raise ValueError("game.dat identity drifted from the BFME2 1.06 oracle")
    image_base, sections = _pe_layout(game_dat)
    if image_base != _EXPECTED_IMAGE_BASE:
        raise ValueError("game.dat image base drifted")
    text, text_raw, text_rva = _section(game_dat, sections, ".text")
    text_va = image_base + text_rva

    literal_rows = []
    for face, literal, field_offset in _FACE_FIELDS:
        offsets = _occurrences(game_dat, literal.encode("ascii") + b"\0")
        if len(offsets) != 1:
            raise ValueError(f"expected one executable literal {literal}")
        literal_va = _raw_to_va(offsets[0], image_base, sections)
        if literal_va is None:
            raise ValueError(f"literal is outside mapped PE sections: {literal}")
        immediate_offsets = _occurrences(text, struct.pack("<I", literal_va))
        if len(immediate_offsets) != 1 or text[immediate_offsets[0] - 1] != 0x68:
            raise ValueError(f"constructor xref drifted for {literal}")
        literal_rows.append(
            {
                "face": face,
                "fieldOffset": field_offset,
                "literal": literal,
                "literalFileOffset": offsets[0],
                "literalVa": literal_va,
                "pushInstructionVa": text_va + immediate_offsets[0] - 1,
            }
        )

    constructor_raw = _va_to_raw(_EXPECTED_CONSTRUCTOR_VA, image_base, sections)
    if game_dat[constructor_raw : constructor_raw + 6] != b"\xb8\x19\xd3\xb6\x00\xe8":
        raise ValueError("SkyboxTextureSet constructor signature drifted")
    constructor_calls = _relative_call_sites(
        text, text_va, _EXPECTED_CONSTRUCTOR_VA
    )
    if constructor_calls != [_EXPECTED_CONSTRUCTOR_CALL_VA]:
        raise ValueError("SkyboxTextureSet constructor call graph drifted")

    type_offsets = _occurrences(game_dat, b"SkyboxTextureSet\0")
    if len(type_offsets) != 1:
        raise ValueError("SkyboxTextureSet type literal count drifted")
    type_va = _raw_to_va(type_offsets[0], image_base, sections)
    if type_va is None:
        raise ValueError("SkyboxTextureSet type literal is unmapped")
    descriptors = _occurrences(game_dat, struct.pack("<I", type_va))
    if len(descriptors) != 1:
        raise ValueError("SkyboxTextureSet parser descriptor count drifted")
    descriptor_raw = descriptors[0]
    callback_va, terminator = struct.unpack_from("<II", game_dat, descriptor_raw + 4)
    if callback_va != _EXPECTED_PARSE_CALLBACK_VA or terminator != 0:
        raise ValueError("SkyboxTextureSet parser descriptor drifted")
    descriptor_va = _raw_to_va(descriptor_raw, image_base, sections)
    if descriptor_va is None:
        raise ValueError("SkyboxTextureSet parser descriptor is unmapped")

    registry_needle = struct.pack("<I", _EXPECTED_REGISTRY_VA)
    registry_immediates = _occurrences(text, registry_needle)
    registry_refs = []
    for immediate in registry_immediates:
        instruction_offset = immediate - 1
        instruction_va = text_va + instruction_offset
        if text[instruction_offset] not in (0xBE, 0xB9):
            raise ValueError("unexpected absolute registry xref instruction")
        classification = _EXPECTED_REGISTRY_REFS.get(instruction_va)
        if classification is None:
            raise ValueError(
                f"unclassified absolute registry xref: 0x{instruction_va:08X}"
            )
        registry_refs.append(
            {"classification": classification, "instructionVa": instruction_va}
        )
    if {value["instructionVa"] for value in registry_refs} != set(
        _EXPECTED_REGISTRY_REFS
    ):
        raise ValueError("SkyboxTextureSet registry xrefs drifted")

    executable_name_tokens = {}
    for value in ("Morning", "DefaultSky", "new_skybox"):
        executable_name_tokens[value] = _occurrences(
            game_dat, value.encode("ascii") + b"\0"
        )
    if any(executable_name_tokens.values()):
        raise ValueError("unexpected named world-sky selection token in game.dat")

    draw_offsets = _occurrences(game_dat, b"DrawSkyBox\0")
    if len(draw_offsets) != 1:
        raise ValueError("DrawSkyBox executable literal count drifted")
    begin_offsets = _occurrences(game_dat, b"DRAW_SKYBOX_BEGIN\0")
    end_offsets = _occurrences(game_dat, b"DRAW_SKYBOX_END\0")
    if len(begin_offsets) != 1 or len(end_offsets) != 1:
        raise ValueError("skybox render-mode telemetry literals drifted")

    return {
        "byteCount": len(game_dat),
        "constructor": {
            "callSites": constructor_calls,
            "fieldDefaults": literal_rows,
            "meaning": (
                "per-record initialization defaults before INI fields are parsed; "
                "not a selected named texture set"
            ),
            "va": _EXPECTED_CONSTRUCTOR_VA,
        },
        "imageBase": image_base,
        "namedSelectionTokenOffsets": executable_name_tokens,
        "parserDescriptor": {
            "callbackVa": callback_va,
            "descriptorVa": descriptor_va,
            "typeLiteralFileOffset": type_offsets[0],
            "typeLiteralVa": type_va,
        },
        "registry": {
            "absoluteTextReferences": registry_refs,
            "address": _EXPECTED_REGISTRY_VA,
            "rendererConsumerReferenceCount": 0,
            "scope": (
                "only direct absolute references are classified; aliased or dynamic "
                "consumers cannot be excluded by this static scan"
            ),
        },
        "renderModeEvidence": {
            "drawSkyBoxLiteralFileOffset": draw_offsets[0],
            "telemetryLiteralFileOffsets": {
                "begin": begin_offsets[0],
                "end": end_offsets[0],
            },
            "meaning": (
                "the executable has a sky-background draw mode; these literals do "
                "not name or choose a texture set"
            ),
        },
        "sha256": digest,
    }


def _opensage_trace(root: Path | str) -> dict[str, Any]:
    source = Path(root).expanduser().resolve()
    if not source.is_dir():
        raise ValueError(f"missing OpenSAGE source: {source}")
    result = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    )
    commit = result.stdout.strip()
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("OpenSAGE commit identity is invalid")
    tokens = ("SkyboxTextureSet", "SkyboxTextureSets", "SkyboxSettings")
    references: list[dict[str, Any]] = []
    production = source / "src"
    for path in sorted(production.rglob("*.cs")):
        relative = path.relative_to(source).as_posix()
        if ".Tests/" in relative or relative.endswith("Tests.cs"):
            continue
        if path.stat().st_size > _MAX_TEXT_BYTES:
            raise ValueError(f"OpenSAGE source exceeds bounded size: {relative}")
        for number, line in enumerate(path.read_text("utf-8").splitlines(), 1):
            matched = sorted({token for token in tokens if token in line})
            if matched:
                references.append(
                    {"line": number, "path": relative, "tokens": matched}
                )
    renderer_refs = [
        value
        for value in references
        if "/Rendering/" in value["path"] or "/Graphics/" in value["path"]
    ]
    if renderer_refs:
        raise ValueError("OpenSAGE gained a skybox renderer reference; review required")
    return {
        "commit": commit,
        "productionReferenceCount": len(references),
        "productionReferences": references,
        "rendererReferenceCount": 0,
        "scope": "secondary implementation evidence; not retail behavior proof",
    }


def _draw_skybox_contract(gamedata: bytes) -> dict[str, Any]:
    text = gamedata.decode("latin-1")
    values = re.findall(
        r"(?im)^\s*DrawSkyBox\s*=\s*([A-Za-z]+)\s*(?:;.*)?$", text
    )
    if values != ["Yes"]:
        raise ValueError("expected one global DrawSkyBox = Yes declaration")
    line_number = next(
        index
        for index, line in enumerate(text.splitlines(), 1)
        if re.match(r"(?i)^\s*DrawSkyBox\s*=", line)
    )
    return {
        "authoredValue": True,
        "line": line_number,
        "meaning": (
            "global permission to draw the sky background; not a named texture-set "
            "selection rule"
        ),
    }


def compose_fords_world_sky_trace_contract(
    *,
    effective_assets_root: Path | str,
    manifest_path: Path | str,
    catalog_path: Path | str,
    game_dat_path: Path | str,
    skybox_oracle_path: Path | str,
    water_reflection_contract_path: Path | str,
    opensage_root: Path | str,
) -> dict[str, Any]:
    root = Path(effective_assets_root).expanduser().resolve()
    manifest = _load_manifest(manifest_path)
    files, manifest_evidence = _validate_effective_manifest(manifest)
    catalog = InstallCatalog.load(Path(catalog_path).expanduser().resolve())
    gamedata = _verified_read(root, GAMEDATA_INI_PATH, files)
    gamedata_row = _source_row(GAMEDATA_INI_PATH, files, catalog)

    skybox = _load_json(skybox_oracle_path, "Fords skybox oracle")
    if skybox.get("schema") != SKYBOX_ORACLE_SCHEMA:
        raise ValueError("unexpected Fords skybox oracle schema")
    if skybox["worldSky"]["selection"]["proven"]:
        raise ValueError("skybox oracle selection changed; static trace needs review")
    if skybox["summary"]["mapSkyboxOverridePresent"]:
        raise ValueError("Fords gained a map-authored skybox override")

    water = _load_json(
        water_reflection_contract_path, "Fords water-reflection oracle"
    )
    if water.get("schema") != WATER_ORACLE_SCHEMA:
        raise ValueError("unexpected Fords water-reflection oracle schema")
    if water["reflectionSkydome"]["classification"] != (
        "water-reflection-skydome-not-world-sky"
    ):
        raise ValueError("water-reflection skydome classification drifted")

    game_dat_source = Path(game_dat_path).expanduser().resolve()
    if not game_dat_source.is_file() or game_dat_source.stat().st_size > _MAX_GAME_DAT_BYTES:
        raise ValueError("game.dat is missing or exceeds the bounded size")
    executable = _static_executable_trace(game_dat_source.read_bytes())
    opensage = _opensage_trace(opensage_root)
    draw_skybox = _draw_skybox_contract(gamedata)

    blockers = [
        {
            "id": "no-map-authored-world-sky-selection",
            "evidence": "existing oracle proves no SkyboxSettings chunk and no map.ini override",
        },
        {
            "id": "parser-defaults-stop-before-renderer",
            "evidence": (
                "TSMorning literals initialize each parsed record; the only direct "
                "registry references are parser creation, static init, and teardown"
            ),
        },
        {
            "id": "no-static-no-override-selection-rule",
            "evidence": (
                "game.dat contains no exact Morning, DefaultSky, or new_skybox named "
                "selection token and the bounded scan found no direct registry renderer consumer"
            ),
        },
        {
            "id": "static-absence-cannot-exclude-aliased-consumer",
            "evidence": (
                "an indirect or copied record could evade direct absolute-xref analysis"
            ),
        },
    ]
    contract: dict[str, Any] = {
        "evidence": {
            "effectiveManifest": manifest_evidence,
            "executable": executable,
            "gamedata": gamedata_row,
            "opensage": opensage,
            "skyboxOracleAggregateSha256": skybox["aggregateSha256"],
            "waterReflectionOracleAggregateSha256": water["aggregateSha256"],
        },
        "globalDrawSkybox": draw_skybox,
        "mapSelection": {
            "mapAuthoredOverridePresent": False,
            "source": "validated existing Fords skybox oracle",
        },
        "runtimeTraceRequired": {
            "debugger": "x86 cdb/windbg attached to the retail BFME2 1.06 game.dat",
            "minimumRuns": 2,
            "steps": [
                {
                    "step": 1,
                    "action": (
                        "break at 0x0060D76F after each SkyboxTextureSet insertion; "
                        "identify the map key Morning and retain its record pointer"
                    ),
                },
                {
                    "step": 2,
                    "action": (
                        "before loading Fords, set read watchpoints on record fields "
                        "+0x04,+0x08,+0x0C,+0x10; repeat with +0x14 because x86 has "
                        "only four hardware watchpoint slots"
                    ),
                },
                {
                    "step": 3,
                    "action": (
                        "run through the first rendered Fords frame and record every "
                        "reader instruction, copied destination, resolved texture name, "
                        "and bound GPU texture resource"
                    ),
                },
                {
                    "step": 4,
                    "action": (
                        "hash the five effective retail winners only if the same named "
                        "record is proven to reach the main-camera sky draw"
                    ),
                },
            ],
            "successCriterion": (
                "one named record and all five resolved retail textures are observed "
                "on the active main-camera sky draw for Fords"
            ),
        },
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "summary": {
            "blockerCount": len(blockers),
            "globalDrawSkyboxEnabled": True,
            "morningConstructorDefaultFieldCount": len(_FACE_FIELDS),
            "selectedFaceClosureCount": 0,
            "waterReflectionSkydomeSeparated": True,
            "worldSkySelectionProven": False,
        },
        "waterReflectionBoundary": {
            "classification": "water-reflection-skydome-not-world-sky",
            "maySatisfyWorldSkySelection": False,
        },
        "worldSkySelection": {
            "blockers": blockers,
            "candidatePromotionRejected": ["Morning", "DefaultSky"],
            "proven": False,
            "selectedFaceClosure": [],
            "selectedTextureSet": None,
            "status": "fail-closed-static-trace-stops-before-renderer",
        },
    }
    contract["aggregateSha256"] = _canonical_sha256(contract)
    return contract


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--effective-assets-root", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--catalog", required=True, type=Path)
    parser.add_argument("--game-dat", required=True, type=Path)
    parser.add_argument("--skybox-oracle", required=True, type=Path)
    parser.add_argument("--water-reflection-contract", required=True, type=Path)
    parser.add_argument("--opensage-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    contract = compose_fords_world_sky_trace_contract(
        effective_assets_root=args.effective_assets_root,
        manifest_path=args.manifest,
        catalog_path=args.catalog,
        game_dat_path=args.game_dat,
        skybox_oracle_path=args.skybox_oracle,
        water_reflection_contract_path=args.water_reflection_contract,
        opensage_root=args.opensage_root,
    )
    write_json_atomic(args.output, contract)
    print(
        json.dumps(
            {
                "aggregateSha256": contract["aggregateSha256"],
                "output": str(args.output.resolve()),
                "summary": contract["summary"],
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
