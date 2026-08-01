"""Emit a bounded, zero-guess Fords of Isen II world-sky oracle contract.

The retail map does not contain a ``SkyboxSettings`` record.  This module
records that absence, the exact global texture-set declarations, and bounded
``game.dat`` string evidence without promoting any of them into a renderer
selection rule.  The separately placed water-reflection skydome is classified
and closed independently so it cannot be mistaken for the world sky.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
from typing import Any, Mapping

from .catalog import InstallCatalog
from .paths import safe_relative_parts
from .retail_fords_environment_profile import (
    _canonical_sha256,
    _skybox_sets,
    _validate_effective_manifest,
)
from .sage_map import census_sage_map_bytes, parse_sage_map_bytes
from .util import write_json_atomic
from .w3d_metadata import scan_w3d_metadata


SCHEMA = "openbfme.retail-fords-skybox-oracle"
SCHEMA_VERSION = 0

MAP_PATH = "maps/map mp fords of isen ii/map mp fords of isen ii.map"
MAP_INI_PATH = "maps/map mp fords of isen ii/map.ini"
ENVIRONMENT_INI_PATH = "data/ini/environment.ini"
WORLD_SKY_MODEL_PATH = "art/w3d/ne/new_skybox.w3d"
NATURE_PROP_INI_PATH = "data/ini/object/nature/natureprop.ini"
REFLECTION_MODEL_PATH = "art/w3d/wt/wtrsky_grohan.w3d"
REFLECTION_TEXTURE_PATH = "art/compiledtextures/wt/wtrskydome_gapofrohan.dds"

_MAX_DOCUMENT_BYTES = 32 * 1024 * 1024
_MAX_GAME_DAT_BYTES = 64 * 1024 * 1024
_DEFAULT_SKY_COMMENT = (
    "; This DefaultSky texture scheme must be present and texture names must match\n"
    "; the assigned textures in new_skybox.w3d"
)
_EXECUTABLE_STRINGS = (
    "SkyboxTextureSet",
    "SkyboxTextureN",
    "SkyboxTextureE",
    "SkyboxTextureS",
    "SkyboxTextureW",
    "SkyboxTextureT",
    "TSMorningN.tga",
    "TSMorningE.tga",
    "TSMorningS.tga",
    "TSMorningW.tga",
    "TSMorningT.tga",
)


def _load_manifest(path: Path | str) -> tuple[dict[str, Any], Path]:
    source = Path(path).expanduser().resolve()
    if source.stat().st_size > _MAX_DOCUMENT_BYTES:
        raise ValueError("effective-assets manifest exceeds the bounded size")
    value = json.loads(source.read_text("utf-8"))
    if not isinstance(value, dict):
        raise ValueError("effective-assets manifest must be an object")
    return value, source


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
    digest = hashlib.sha256(payload).hexdigest()
    if digest != record.get("sha256"):
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


def _ascii_offsets(payload: bytes, values: tuple[str, ...]) -> dict[str, list[int]]:
    """Return every exact NUL-terminated ASCII occurrence for bounded literals."""

    result: dict[str, list[int]] = {}
    for value in values:
        needle = value.encode("ascii") + b"\0"
        offsets: list[int] = []
        start = 0
        while True:
            offset = payload.find(needle, start)
            if offset < 0:
                break
            offsets.append(offset)
            start = offset + 1
        result[value] = offsets
    return result


def _named_set(
    sets: list[dict[str, Any]], name: str
) -> dict[str, Any]:
    matches = [value for value in sets if value["name"].casefold() == name.casefold()]
    if len(matches) != 1:
        raise ValueError(f"expected one SkyboxTextureSet {name!r}")
    return matches[0]


def _selection_contract(
    *,
    map_has_settings: bool,
    map_ini_has_override: bool,
    default_set: Mapping[str, Any],
    executable_offsets: Mapping[str, list[int]],
) -> dict[str, Any]:
    """Resolve only directly authored selection evidence; otherwise fail closed."""

    default_missing = [
        face["requestedName"]
        for face in default_set["faces"]
        if face["selectedVirtualPath"] is None
    ]
    executable_has_morning_literals = all(
        executable_offsets.get(f"TSMorning{face}.tga")
        for face in ("N", "E", "S", "W", "T")
    )
    blockers = []
    if not map_has_settings and not map_ini_has_override:
        blockers.append(
            {
                "id": "no-map-authored-world-sky-selection",
                "evidence": "no SkyboxSettings top-level map chunk and no skybox token in map.ini",
                "requiredProof": "retail renderer trace identifying the active texture scheme when both map override mechanisms are absent",
            }
        )
    blockers.append(
        {
            "id": "environment-default-semantics-do-not-state-selection",
            "evidence": "environment.ini requires DefaultSky to exist and match new_skybox.w3d, but does not state that maps without overrides select it",
            "requiredProof": "direct retail executable control-flow or original runtime trace proving the no-override selection rule",
        }
    )
    if default_missing:
        blockers.append(
            {
                "id": "default-sky-leaves-absent",
                "missingRequestedNames": default_missing,
                "requiredProof": "retail payloads for the five DefaultSky leaves or proof that another complete named set is selected",
            }
        )
    if executable_has_morning_literals:
        blockers.append(
            {
                "id": "executable-morning-literals-are-not-active-selection-proof",
                "evidence": "game.dat contains all five TSMorning face literals beside the SkyboxTextureSet parser vocabulary",
                "requiredProof": "retail control-flow or runtime trace showing those parser-record defaults reach the active Fords renderer",
            }
        )
    return {
        "blockers": blockers,
        "candidateAssetClosure": [],
        "proven": False,
        "selectedTextureSet": None,
        "status": "fail-closed-no-direct-retail-selection-rule",
    }


def _reflection_contract(
    *,
    map_payload: bytes,
    nature_ini: bytes,
    model_payload: bytes,
    files: Mapping[str, Mapping[str, Any]],
    catalog: InstallCatalog,
) -> dict[str, Any]:
    parsed = parse_sage_map_bytes(map_payload)
    placements = [
        value
        for value in parsed.objects
        if value["typeName"] == "WaterReflectionSkydome_GapOfRohan"
    ]
    if len(placements) != 1:
        raise ValueError("Fords reflection-skydome placement count drifted")
    text = nature_ini.decode("latin-1")
    match = re.search(
        r"(?ims)^Object\s+WaterReflectionSkydome_GapOfRohan\s*$"
        r"(?P<body>.*?)^End\s*$",
        text,
    )
    if match is None or not re.search(
        r"(?im)^\s*Model\s*=\s*WtrSky_GRohan\s*$", match.group("body")
    ):
        raise ValueError("reflection-skydome INI model binding drifted")
    metadata = scan_w3d_metadata(model_payload, REFLECTION_MODEL_PATH)
    texture_values = [
        str(value.value)
        for value in metadata.shader_material_properties
        if value.name == "Texture_0" and isinstance(value.value, str)
    ]
    if texture_values != ["WtrSkydome_GapOfRohan.tga"]:
        raise ValueError("reflection-skydome W3D texture binding drifted")
    if PurePosixPath(REFLECTION_TEXTURE_PATH).stem.casefold() != PurePosixPath(
        texture_values[0]
    ).stem.casefold():
        raise ValueError("reflection-skydome compiled texture counterpart drifted")
    closure_paths = [
        NATURE_PROP_INI_PATH,
        REFLECTION_MODEL_PATH,
        REFLECTION_TEXTURE_PATH,
        MAP_PATH,
    ]
    closure = [_source_row(path, files, catalog) for path in closure_paths]
    return {
        "assetClosure": closure,
        "assetClosureSha256": _canonical_sha256(closure),
        "classification": "placed-water-reflection-skydome-not-world-sky",
        "modelBinding": "WtrSky_GRohan",
        "placementCount": 1,
        "proven": True,
        "textureBinding": texture_values[0],
        "textureVirtualPath": REFLECTION_TEXTURE_PATH,
    }


def compose_fords_skybox_oracle_contract(
    *,
    effective_assets_root: Path | str,
    manifest_path: Path | str,
    catalog_path: Path | str,
    game_dat_path: Path | str,
) -> dict[str, Any]:
    root = Path(effective_assets_root).expanduser().resolve()
    manifest, manifest_source = _load_manifest(manifest_path)
    files, manifest_evidence = _validate_effective_manifest(manifest)
    catalog_source = Path(catalog_path).expanduser().resolve()
    catalog = InstallCatalog.load(catalog_source)

    source_paths = (
        MAP_PATH,
        MAP_INI_PATH,
        ENVIRONMENT_INI_PATH,
        WORLD_SKY_MODEL_PATH,
        NATURE_PROP_INI_PATH,
        REFLECTION_MODEL_PATH,
        REFLECTION_TEXTURE_PATH,
    )
    payloads = {path: _verified_read(root, path, files) for path in source_paths}
    source_rows = [_source_row(path, files, catalog) for path in source_paths]

    environment_text = payloads[ENVIRONMENT_INI_PATH].decode("latin-1").replace(
        "\r\n", "\n"
    )
    if _DEFAULT_SKY_COMMENT not in environment_text:
        raise ValueError("environment.ini DefaultSky/new_skybox semantics drifted")
    sets = _skybox_sets(payloads[ENVIRONMENT_INI_PATH], files)
    default_set = _named_set(sets, "DefaultSky")
    morning_set = _named_set(sets, "Morning")

    census = census_sage_map_bytes(payloads[MAP_PATH])
    chunk_names = [value["name"] for value in census["chunks"]]
    map_has_settings = "SkyboxSettings" in chunk_names
    map_ini_tokens = re.findall(
        r"(?i)Skybox(?:TextureSet|Texture[NEWST]|Settings)?",
        payloads[MAP_INI_PATH].decode("latin-1"),
    )

    game_dat_source = Path(game_dat_path).expanduser().resolve()
    game_dat_size = game_dat_source.stat().st_size
    if game_dat_size > _MAX_GAME_DAT_BYTES:
        raise ValueError("game.dat exceeds the bounded executable size")
    game_dat = game_dat_source.read_bytes()
    executable_offsets = _ascii_offsets(game_dat, _EXECUTABLE_STRINGS)
    missing_executable_literals = [
        value for value, offsets in executable_offsets.items() if not offsets
    ]
    if missing_executable_literals:
        raise ValueError(
            "game.dat lacks expected sky parser literals: "
            + ", ".join(missing_executable_literals)
        )

    world_model = scan_w3d_metadata(
        payloads[WORLD_SKY_MODEL_PATH], WORLD_SKY_MODEL_PATH
    )
    world_texture_refs = sorted(
        {value.identifier for value in world_model.texture_references}, key=str.casefold
    )
    if world_texture_refs != [
        "SkyBox_01.tga",
        "SkyBox_02.tga",
        "SkyBox_03.tga",
        "SkyBox_04.tga",
    ]:
        raise ValueError("new_skybox.w3d texture references drifted")

    selection = _selection_contract(
        map_has_settings=map_has_settings,
        map_ini_has_override=bool(map_ini_tokens),
        default_set=default_set,
        executable_offsets=executable_offsets,
    )
    reflection = _reflection_contract(
        map_payload=payloads[MAP_PATH],
        nature_ini=payloads[NATURE_PROP_INI_PATH],
        model_payload=payloads[REFLECTION_MODEL_PATH],
        files=files,
        catalog=catalog,
    )
    contract: dict[str, Any] = {
        "evidence": {
            "catalogPath": str(catalog_source),
            "executable": {
                "asciiLiteralOffsets": executable_offsets,
                "byteCount": game_dat_size,
                "path": str(game_dat_source),
                "sha256": hashlib.sha256(game_dat).hexdigest(),
                "scope": "presence-only; literals do not prove renderer control flow",
            },
            "manifest": {
                **manifest_evidence,
                "path": str(manifest_source),
            },
            "retailFiles": source_rows,
            "retailFilesAggregateSha256": _canonical_sha256(source_rows),
        },
        "map": {
            "mapIniSkyboxTokens": map_ini_tokens,
            "skyboxSettingsChunkPresent": map_has_settings,
            "topLevelChunkCount": len(chunk_names),
            "topLevelChunkNames": chunk_names,
        },
        "reflectionSkydome": reflection,
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "summary": {
            "blockerCount": len(selection["blockers"]),
            "defaultSkyResolvedFaceCount": sum(
                face["selectedVirtualPath"] is not None
                for face in default_set["faces"]
            ),
            "mapSkyboxOverridePresent": map_has_settings or bool(map_ini_tokens),
            "morningResolvedFaceCount": sum(
                face["selectedVirtualPath"] is not None
                for face in morning_set["faces"]
            ),
            "reflectionClosureFileCount": len(reflection["assetClosure"]),
            "worldSkySelectionProven": selection["proven"],
        },
        "worldSky": {
            "defaultSemantics": {
                "comment": _DEFAULT_SKY_COMMENT.removeprefix("; ").replace(
                    "\n; ", " "
                ),
                "defaultSet": default_set,
                "meaning": "model texture-name compatibility requirement; not a no-override selection statement",
            },
            "morningCandidate": morning_set,
            "model": {
                "source": _source_row(WORLD_SKY_MODEL_PATH, files, catalog),
                "textureReferences": world_texture_refs,
            },
            "selection": selection,
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
    parser.add_argument("--output", required=True, type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    contract = compose_fords_skybox_oracle_contract(
        effective_assets_root=args.effective_assets_root,
        manifest_path=args.manifest,
        catalog_path=args.catalog,
        game_dat_path=args.game_dat,
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
