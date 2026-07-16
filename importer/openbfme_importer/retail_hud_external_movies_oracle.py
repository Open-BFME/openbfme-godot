"""Prove the BFME2 Palantir external-movie and renderer-callback closure.

This module is deliberately an oracle, not a converter or renderer.  It reads
private retail inputs, emits hashes and typed contracts only, and fails closed
when any of the five authored movie loads or five native callbacks changes.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any, Iterable, Mapping

from .retail_hud_apt_convert import (
    _Movie,
    _decode_action_sequence,
    _movie_from_plan,
)
from .sage_apt import (
    canonical_sha256,
    parse_apt_constants,
    parse_apt_dat,
    parse_apt_geometry,
    parse_apt_movie,
    parse_tga_identity,
)


SCHEMA = "openbfme.private-hud-external-movies-oracle"

MOVIE_LOADS: tuple[dict[str, str], ...] = (
    {"movieId": "InGameSpellBook", "swf": "InGameSpellBook.swf", "target": "SpellBookUI"},
    {"movieId": "InGameSideCommandBar", "swf": "InGameSideCommandBar.swf", "target": "SideCommandBar"},
    {"movieId": "InGameHelpBox", "swf": "InGameHelpBox.swf", "target": "helpBox"},
    {"movieId": "InGameHeroSelect", "swf": "InGameHeroSelect.swf", "target": "HeroSelectUI"},
    {"movieId": "InGamePlanningMode", "swf": "InGamePlanningMode.swf", "target": "planningModeUI"},
)

ARCHIVES: dict[str, dict[str, Any]] = {
    "InGameSpellBook": {
        "archive": "apt/ingamespellbook.big",
        "archiveSha256": "eb0dcc5d79d3b88caf0870aa2bb5e965d099499c2cd67d7275586870f4d2595f",
        "directorySha256": "1487a67e82f26c32e79acfb2f5b80d2c4c217c9a989476ee2d41019657bb539f",
        "fileCount": 4,
        "payloadBytes": 30_453,
    },
    "InGameSideCommandBar": {
        "archive": "apt/ingamesidecommandbar.big",
        "archiveSha256": "a93dd36fe8c4bb9d68acb8b01823b2d1c1e18ab8aa09873a9bcab2e63a5ddbab",
        "directorySha256": "eae25ec8744379a779ad4730178abe9e3755099166a52fbd69ec49874ead752d",
        "fileCount": 3,
        "payloadBytes": 17_496,
    },
    "InGameHelpBox": {
        "archive": "apt/ingamehelpbox.big",
        "archiveSha256": "497e433e782202c9666b886d93afd089060dd38c7bbf85d4106013f562b62a7e",
        "directorySha256": "7b28b0a94a06e5ae6aa5b9c2a68df8d8ba51c9d2801c019fce29e36d18f5b07a",
        "fileCount": 13,
        "payloadBytes": 9_377,
    },
    "InGameHeroSelect": {
        "archive": "apt/ingameheroselect.big",
        "archiveSha256": "18d28e1914df15f99720835ee076a3a260aa7177f5202cf70140309339080fc7",
        "directorySha256": "3b36220bb8e2bc63c27e1592e2a7683c6bdfdeaed3f4d8114713f910d772cf61",
        "fileCount": 27,
        "payloadBytes": 2_282_905,
    },
    "InGamePlanningMode": {
        "archive": "apt/ingameplanningmode.big",
        "archiveSha256": "acb06c2dfd327bba795cd6821241ca2f55a4f95c419a53a9666d9aab0a2eaabd",
        "directorySha256": "6e826629574691246955ffde20d89762bc5b562b98f6e5132609c41f30b5ddb3",
        "fileCount": 28,
        "payloadBytes": 1_083_153,
    },
}

LIFECYCLE: dict[str, dict[str, Any]] = {
    "InGameSpellBook": {
        "programSha256": "bc9e19fd0ad7500926b31dba505f878012335ec804ec35707cfa733ecb5b943d",
        "loaded": "OnAptInGameSpellBookLoaded(GetFullName(this))",
        "unloaded": "onUnload -> OnAptInGameSpellBookUnloaded(GetFullName(this))",
    },
    "InGameSideCommandBar": {
        "programSha256": "5f819773cdf0b9105bd0f0c0978da26d2d8f8ecebf63668116c1a00041e24fb5",
        "loaded": "OnAptInGameSideCommandBarLoaded(GetFullName(this))",
        "unloaded": "onUnload -> OnAptInGameSideCommandBarUnloaded(GetFullName(this))",
    },
    "InGameHelpBox": {
        "programSha256": "71664be06717e6e52ee407d44bda48934a427601d7419d03f26ca75be7eda502",
        "loaded": "_parent.OnHelpBoxMovieLoaded(this)",
        "unloaded": "onUnload -> _parent.OnHelpBoxMovieUnloaded(this)",
    },
    "InGameHeroSelect": {
        "programSha256": "2c43ab2db3b3f9158706bc28154ed4362c9e6fd237bcc218f9e1100621190b1f",
        "loaded": "_parent.OnHeroSelectMovieLoaded(this)",
        "unloaded": "parent installs clip.onUnload -> AptPalantir::OnHeroSelectUnloaded(clip.toString())",
    },
    "InGamePlanningMode": {
        "programSha256": "6a660a1b59561308ac86226bcbf83f347e87b82e894aeb383b60be2e79d74e97",
        "loaded": "_parent.OnPlanningModeUILoaded(this)",
        "unloaded": "onUnload -> _parent.OnPlanningModeUIUnloaded(this)",
    },
}

CALLBACKS: tuple[dict[str, Any], ...] = (
    {
        "name": "AptPalantir::ClipRadar",
        "entryVa": 0x6D3125,
        "endVa": 0x6D317C,
        "sha256": "d2d9f77a95ba34319061a9e7cd55fce8a3789fad01c210a445eff917841c258e",
        "aptOwner": "sprite:93",
        "aptFrame": 14,
        "target": "Radar/RadarClip",
        "visibleResponsibility": "no pixels; rounds two float2 rectangles and stores the radar clip bounds",
        "dataSource": "APT transform rectangle only",
        "reachability": "slice-active with the shown radar",
        "typedInterface": "set_radar_clip(origin: Vector2, size: Vector2) -> void",
    },
    {
        "name": "AptPalantir::RenderRadar",
        "entryVa": 0x6D317C,
        "endVa": 0x6D32A3,
        "sha256": "01adb28b91b075856153b5b62f5d4819a7fc4ce2333f5d38d6639d1f83410127",
        "aptOwner": "sprite:93",
        "aptFrame": 14,
        "target": "Radar",
        "visibleResponsibility": "radar/minimap pixels inside the authored APT rectangle",
        "dataSource": "retail radar service; OpenSAGE observation binds Scene3D.RadarDrawUtil minimap and overlay",
        "reachability": "slice-active",
        "typedInterface": "render_radar(origin: Vector2, size: Vector2, state: RadarFrameState) -> void",
    },
    {
        "name": "AptPalantir::RenderRadarViewBox",
        "entryVa": 0x6D32A3,
        "endVa": 0x6D32AE,
        "sha256": "2927de946ed610a6f07e5f43fcddad6b9f62f0aee49a7a6c5447d2c0afef5e62",
        "aptOwner": "sprite:98",
        "aptFrame": 14,
        "target": "RadarPings/view-box",
        "visibleResponsibility": "camera view box/frustum overlay on the radar",
        "dataSource": "retail view-box service; OpenSAGE observation derives the overlay from Camera and HeightMap",
        "reachability": "slice-active with RadarPings shown",
        "typedInterface": "render_radar_view_box(state: CameraRadarFootprint) -> void",
    },
    {
        "name": "AptPalantir::RenderMovie",
        "entryVa": 0x6D32AE,
        "endVa": 0x6D32F4,
        "sha256": "8eec95b949fe7006377720ba235bc15edc8ecde9a7f75dd4b2b5f3c8b87e4785",
        "aptOwner": "root",
        "aptFrame": 0,
        "target": "MoviePlayback",
        "visibleResponsibility": "movie frame pixels fitted to the authored APT rectangle",
        "dataSource": "retail movie playback object at callback owner offset 0x78",
        "reachability": "surface-loaded but dormant in ordinary Men/Fords play until movie playback is requested",
        "typedInterface": "render_movie(origin: Vector2, size: Vector2, frame: MovieFrameHandle) -> void",
    },
    {
        "name": "AptPalantir::RenderGlobe",
        "entryVa": 0x6D32F4,
        "endVa": 0x6D3347,
        "sha256": "0bce5fee9a61db5384c44988e1e01fc5276a540e8e507d9e6ce58110cf94b129",
        "aptOwner": "sprite:55",
        "aptFrame": 25,
        "target": "GlobeSwirlRender and BigGlobeSwirlRender",
        "visibleResponsibility": "engine-owned globe render inside the authored APT rectangle",
        "dataSource": "retail globe service at callback owner offset 0x58; content identity is not statically exposed",
        "reachability": "slice-reachable when either globe timeline enters _show; not continuously visible",
        "typedInterface": "render_globe(origin: Vector2, size: Vector2, state: GlobeRenderState) -> void",
    },
)

_GAME_DAT_SHA256 = "f008b587570bad693981dc7218588c81d192a1e064b0f7f861539c51156a7640"
_TEXT_VA_FILE_DELTA = 0x400A00
_CALLBACK_APT = {
    "AptPalantir::RenderMovie": ("root", 0, 96648, 40, "MoviePlayback", 100, 374220, 376576),
    "AptPalantir::RenderGlobe": ("sprite:55", 25, 152936, 1, None, 54, 374816, 376596),
    "AptPalantir::RenderRadar": ("sprite:93", 14, 167928, 1, None, 90, 374892, 376676),
    "AptPalantir::ClipRadar": ("sprite:93", 14, 167992, 3, "RadarClip", 92, 374904, 376696),
    "AptPalantir::RenderRadarViewBox": ("sprite:98", 14, 168232, 2, None, 96, 374916, 376716),
}


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def _private_root(path: Path | str) -> Path:
    root = Path(path).resolve()
    if ".private" not in {part.casefold() for part in root.parts} or not root.is_dir():
        raise ValueError("effective-assets must be an existing private directory")
    return root


def _rows_by_archive(manifest: Mapping[str, Any], archive: str) -> list[dict[str, Any]]:
    rows = [
        dict(row)
        for row in manifest.get("files", [])
        if str(row.get("archive", "")).casefold() == archive.casefold()
    ]
    return sorted(rows, key=lambda row: (str(row["path"]).casefold(), str(row["path"])))


def _movie_summary(root: Path, name: str, rows: list[dict[str, Any]]) -> dict[str, Any]:
    by_path = {str(row["path"]).casefold(): row for row in rows}
    parsed: dict[str, Any] = {}
    for suffix in ("apt", "const", "dat"):
        key = f"{name}.{suffix}".casefold()
        if key not in by_path:
            raise ValueError(f"{name} is missing {suffix.upper()}")
    constants = parse_apt_constants((root / f"{name}.const").read_bytes(), f"{name}.const")
    apt = parse_apt_movie((root / f"{name}.apt").read_bytes(), constants, f"{name}.apt")
    dat = parse_apt_dat((root / f"{name}.dat").read_bytes(), f"{name}.dat")
    parsed["apt"] = {
        "virtualPath": apt["virtualPath"],
        "byteLength": apt["byteLength"],
        "sha256": apt["sha256"],
        "aptVersion": apt["aptVersion"],
        "root": apt["root"],
        "imports": apt["imports"],
    }
    parsed["const"] = {
        "virtualPath": constants["virtualPath"],
        "byteLength": constants["byteLength"],
        "sha256": constants["sha256"],
        "constantCount": constants["constantCount"],
    }
    parsed["dat"] = dat
    parsed["geometry"] = [
        parse_apt_geometry((root / str(row["path"])).read_bytes(), str(row["path"]))
        for row in rows
        if str(row["path"]).casefold().startswith(f"{name.casefold()}_geometry/")
    ]
    parsed["atlases"] = [
        parse_tga_identity((root / str(row["path"])).read_bytes(), str(row["path"]))
        for row in rows
        if str(row["path"]).casefold().startswith(
            f"art/textures/apt_{name.casefold()}_"
        )
    ]
    return parsed


def _walk_instructions(rows: Iterable[Mapping[str, Any]]) -> Iterable[Mapping[str, Any]]:
    for row in rows:
        yield row
        yield from _walk_instructions(row.get("body", []))


def _palantir_movie(root: Path) -> _Movie:
    name = "Palantir"
    apt_data = (root / f"{name}.apt").read_bytes()
    const_data = (root / f"{name}.const").read_bytes()
    constants = parse_apt_constants(const_data, f"{name}.const")
    apt = parse_apt_movie(apt_data, constants, f"{name}.apt")
    raw = {
        "movie": name,
        "apt": apt,
        "constants": constants,
        "geometry": [],
        "imageMap": {"mappings": []},
        "atlases": [],
    }
    return _movie_from_plan(
        raw,
        source_bytes={"palantir.apt": apt_data, "palantir.const": const_data},
    )


def _apt_callback_evidence(root: Path) -> list[dict[str, Any]]:
    movie = _palantir_movie(root)
    wanted = set(_CALLBACK_APT)
    found: dict[str, dict[str, Any]] = {}

    def scan(frames: list[list[dict[str, Any]]], owner: str) -> None:
        for frame_index, frame in enumerate(frames):
            for row in frame:
                if row.get("kind") != "place-object" or "clipActions" not in row:
                    continue
                for event in row["clipActions"]["events"]:
                    instructions, _ = _decode_action_sequence(
                        movie, int(event["instructionsOffset"])
                    )
                    values = {
                        str(item.get("operand"))
                        for item in _walk_instructions(instructions)
                        if item.get("operand") in wanted
                    }
                    for name in values:
                        found[name] = {
                            "name": name,
                            "owner": owner,
                            "frame": frame_index,
                            "placeObjectOffset": row["sourceOffset"],
                            "depth": row["depth"],
                            "instanceName": row.get("name"),
                            "characterId": row["characterId"],
                            "eventOffset": event["eventOffset"],
                            "eventNames": event["eventNames"],
                            "instructionsOffset": event["instructionsOffset"],
                        }

    scan(movie.frames, "root")
    for character in movie.characters:
        if character.get("kind") == "sprite":
            scan(character["frames"], f"sprite:{character['characterId']}")
    if set(found) != wanted:
        raise ValueError("Palantir renderer callback identity set changed")
    for name, expected in _CALLBACK_APT.items():
        row = found[name]
        actual = (
            row["owner"],
            row["frame"],
            row["placeObjectOffset"],
            row["depth"],
            row["instanceName"],
            row["characterId"],
            row["eventOffset"],
            row["instructionsOffset"],
        )
        if actual != expected or row["eventNames"] != ["unload"]:
            raise ValueError(f"Palantir callback binding changed: {name}")
    return [found[name] for name in sorted(found, key=str.casefold)]


def _game_dat_evidence(path: Path) -> list[dict[str, Any]]:
    data = path.read_bytes()
    if _sha(data) != _GAME_DAT_SHA256:
        raise ValueError("BFME2 game.dat identity changed")
    result = []
    for row in CALLBACKS:
        start = int(row["entryVa"]) - _TEXT_VA_FILE_DELTA
        end = int(row["endVa"]) - _TEXT_VA_FILE_DELTA
        payload = data[start:end]
        if _sha(payload) != row["sha256"]:
            raise ValueError(f"retail callback code changed: {row['name']}")
        result.append(
            {
                "name": row["name"],
                "entryVa": f"0x{row['entryVa']:08x}",
                "endVa": f"0x{row['endVa']:08x}",
                "byteLength": len(payload),
                "sha256": row["sha256"],
                "ret4ArgumentBytes": 16,
            }
        )
    return result


def _opensage_evidence(root: Path | None) -> list[dict[str, Any]]:
    if root is None:
        return []
    files = (
        "src/OpenSage.Mods.Bfme/Gui/AptControlBarSource.cs",
        "src/OpenSage.Mods.Bfme/Gui/AptPalantir.cs",
        "src/OpenSage.Game/RadarDrawUtil.cs",
    )
    result = []
    for relative in files:
        path = root / Path(relative)
        if not path.is_file():
            raise ValueError(f"OpenSAGE observation source missing: {relative}")
        payload = path.read_bytes()
        result.append({"path": relative, "byteLength": len(payload), "sha256": _sha(payload)})
    text = (root / files[0]).read_text(encoding="utf-8")
    if "DrawRadarMinimap" not in text or "DrawRadarOverlay" not in text:
        raise ValueError("OpenSAGE APT radar binding changed")
    radar = (root / files[2]).read_text(encoding="utf-8")
    for value in ("Radar", "HeightMap", "IGameObjectCollection", "Camera", "_miniMapTexture"):
        if value not in radar:
            raise ValueError("OpenSAGE radar observation input set changed")
    return result


def build_contract(
    effective_assets: Path | str,
    manifest: Mapping[str, Any],
    catalog: Mapping[str, Any],
    profile: Mapping[str, Any],
    game_dat: Path | str,
    *,
    opensage_root: Path | None = None,
) -> dict[str, Any]:
    root = _private_root(effective_assets)
    catalog_archives = {
        str(row["relative_path"]).casefold(): row for row in catalog.get("archives", [])
    }
    movie_rows: list[dict[str, Any]] = []
    all_delta_rows: list[dict[str, Any]] = []
    for load in MOVIE_LOADS:
        name = load["movieId"]
        expected = ARCHIVES[name]
        rows = _rows_by_archive(manifest, str(expected["archive"]))
        if len(rows) != expected["fileCount"] or sum(int(row["size"]) for row in rows) != expected["payloadBytes"]:
            raise ValueError(f"{name} archive closure changed")
        archive = catalog_archives.get(str(expected["archive"]).casefold())
        if archive is None or archive.get("directory_sha256") != expected["directorySha256"]:
            raise ValueError(f"{name} catalog archive identity changed")
        install_archive = Path(str(catalog["install_root"])) / str(expected["archive"])
        if not install_archive.is_file() or _sha(install_archive.read_bytes()) != expected["archiveSha256"]:
            raise ValueError(f"{name} retail BIG identity changed")
        for row in rows:
            source = root / str(row["path"])
            if not source.is_file() or source.stat().st_size != row["size"] or _sha(source.read_bytes()) != row["sha256"]:
                raise ValueError(f"effective source changed: {row['path']}")
        parsed = _movie_summary(root, name, rows)
        in_existing = name == "InGameSideCommandBar"
        if not in_existing:
            all_delta_rows.extend(rows)
        movie_rows.append(
            {
                **load,
                "loadOrder": len(movie_rows),
                "loadTrigger": "Palantir.InitialSetup unconditional getURL2/loadMovie sequence",
                "sliceLoadReachable": True,
                "featureVisibility": (
                    "active selection side bar"
                    if name == "InGameSideCommandBar"
                    else "loaded at initialization; feature branch may remain hidden until invoked"
                ),
                "alreadyInSealedHudSourceClosure": in_existing,
                "archive": {**expected, "catalog": dict(archive)},
                "files": [
                    {"virtualPath": row["path"], "byteLength": row["size"], "sha256": row["sha256"]}
                    for row in rows
                ],
                "parsed": parsed,
                "lifecycle": LIFECYCLE[name],
            }
        )

    resource = next(
        (row for row in profile.get("resources", []) if row.get("id") == "men-hud-apt-runtime-bundle"),
        None,
    )
    if not isinstance(resource, Mapping) or len(resource.get("patterns", [])) != 189:
        raise ValueError("current sealed HUD resource changed")
    by_path = {str(row["path"]).casefold(): row for row in manifest.get("files", [])}
    new_patterns = sorted(
        {str(row["path"]) for row in all_delta_rows}, key=lambda value: (value.casefold(), value)
    )
    prospective_patterns = sorted(
        set(map(str, resource["patterns"])) | set(new_patterns),
        key=lambda value: (value.casefold(), value),
    )
    inventory = [
        {
            "virtualPath": str(by_path[path.casefold()]["path"]),
            "byteLength": int(by_path[path.casefold()]["size"]),
            "sha256": str(by_path[path.casefold()]["sha256"]),
        }
        for path in prospective_patterns
    ]
    if len(new_patterns) != 72 or len(prospective_patterns) != 261:
        raise ValueError("external movie profile delta changed")
    prospective_aggregate = canonical_sha256(inventory)
    if prospective_aggregate != "f62347fb78065726715618ed9c73f152c678fec5646ddf7b0855825d1cb23599":
        raise ValueError("external movie prospective source aggregate changed")

    callback_apt = _apt_callback_evidence(root)
    callback_code = _game_dat_evidence(Path(game_dat))
    result: dict[str, Any] = {
        "schema": SCHEMA,
        "source": {
            "effectiveManifestAggregateSha256": manifest.get("aggregate_sha256"),
            "catalogFormat": catalog.get("format"),
            "gameDatSha256": _GAME_DAT_SHA256,
            "opensageObservationOnly": _opensage_evidence(opensage_root),
        },
        "summary": {
            "movieLoadCount": 5,
            "movieLoadsAccounted": 5,
            "rendererCallbackCount": 5,
            "rendererCallbacksAccounted": 5,
            "alreadySealedMovieCount": 1,
            "newArchiveCount": 4,
            "newSourceCount": 72,
            "newPayloadBytes": sum(int(row["size"]) for row in all_delta_rows),
            "implementationIncluded": False,
            "genericDispatchAllowed": False,
        },
        "movieLoads": movie_rows,
        "loadOrdering": {
            "exactOrder": [row["movieId"] for row in MOVIE_LOADS],
            "sameInitialSetupInvocation": True,
            "targetReplacement": "getURL2 loadMovie into the exact named empty target clip",
            "unloadRule": "dispatch only each movie's source-proven onUnload/lifecycle path; no shared guessed timing",
        },
        "rendererCallbacks": [
            {
                **row,
                "arguments": {
                    "retailStackBytes": 16,
                    "provenUse": (
                        "two float2 rectangle pointers; remaining callback arguments ignored by this function"
                        if row["name"] in {"AptPalantir::ClipRadar", "AptPalantir::RenderRadar", "AptPalantir::RenderMovie", "AptPalantir::RenderGlobe"}
                        else "all four stack arguments ignored; callback dispatches the bound view-box service"
                    ),
                },
                "genericDispatchAllowed": False,
            }
            for row in CALLBACKS
        ],
        "aptCallbackBindings": callback_apt,
        "retailCallbackCode": callback_code,
        "profileDeltaProposal": {
            "resourceId": "men-hud-apt-runtime-bundle",
            "operation": "extend the existing sealed sage-apt-runtime resource; do not create generic movie loaders",
            "currentSourceCount": 189,
            "addSourceCount": 72,
            "prospectiveSourceCount": 261,
            "addPayloadBytes": sum(int(row["size"]) for row in all_delta_rows),
            "prospectivePayloadBytes": sum(row["byteLength"] for row in inventory),
            "newPatterns": new_patterns,
            "expectedSourceAggregateSha256": prospective_aggregate,
            "converterGate": "extend the bounded converter to these four exact movie IDs before changing the profile",
        },
        "blockers": [
            "the bounded converter currently accepts only the 189-source five-bundle Palantir closure",
            "callback entry ABI is proven, but retail internal GlobeRenderState and MovieFrameHandle layouts are opaque",
            "RenderRadar needs authoritative Fords minimap/radar state and RenderRadarViewBox needs authoritative camera footprint",
            "external movie ActionScript/timelines must be converted and gated independently; source presence alone is not parity",
        ],
    }
    result["aggregateSha256"] = _sha(_canonical(result))
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--effective-assets", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--catalog", type=Path, required=True)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--game-dat", type=Path, required=True)
    parser.add_argument("--opensage-root", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    result = build_contract(
        args.effective_assets,
        json.loads(args.manifest.read_text(encoding="utf-8")),
        json.loads(args.catalog.read_text(encoding="utf-8")),
        json.loads(args.profile.read_text(encoding="utf-8")),
        args.game_dat,
        opensage_root=args.opensage_root,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(_canonical(result))
    print(json.dumps({"output": str(args.output), "aggregateSha256": result["aggregateSha256"]}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
