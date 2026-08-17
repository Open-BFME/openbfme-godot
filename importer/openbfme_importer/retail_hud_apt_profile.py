"""Seal the exact BFME2 Men/Fords APT HUD source and conversion contract."""

from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
from typing import Any, Mapping

from .catalog import InstallCatalog
from .profile import ImportProfile, resolve_profile
from .sage_apt import (
    canonical_sha256,
    parse_apt_constants,
    parse_apt_dat,
    parse_apt_geometry,
    parse_apt_movie,
    parse_tga_identity,
    parse_wnd_layout,
)
from .util import write_json_atomic


HUD_APT_PLAN_SCHEMA = "openbfme.retail-hud-apt-plan"
HUD_APT_PLAN_SCHEMA_VERSION = 0
HUD_APT_SCENE_SCHEMA = "openbfme.hud-apt-static-scene-contract"
HUD_APT_SCENE_SCHEMA_VERSION = 0

_MAX_JSON_BYTES = 64 * 1024 * 1024
_MAX_ORACLE_BYTES = 256 * 1024
_EXPECTED_APT_FILE_COUNT = 260
_EXPECTED_APT_PAYLOAD_BYTES = 10_410_403
_EXPECTED_BUNDLE_SOURCE_COUNT = 261
_EXPECTED_BUNDLE_PAYLOAD_BYTES = 10_700_284
_EXPECTED_WND_PATH = "window/controlbar.wnd"
_EXPECTED_WND_SHA256 = "a509730457224a111af8022df6d0ef373fcaa5d91a102bc15bccf5fc1a54ced6"
_FONT_RESOURCE_ID = "men-hud-font-albertus-mt"
_FONT_VIRTUAL_PATH = "albertusmt.otf"
_FONT_ARCHIVE = "_patch103.big"
_FONT_OFFSET = 2510
_FONT_PRECEDENCE = 0
_FONT_BYTES = 24_712
_FONT_SHA256 = "6a1990e17f14ce5be199dde10f56dac3efd66aaa8e91d46119952cf55a9d9ba0"
_FONT_OUTPUT = "assets/ui/palantir/fonts/albertusmt-6a1990e17f14.otf"
_EXPECTED_PROFILE_SELECTED_COUNT = _EXPECTED_BUNDLE_SOURCE_COUNT + 1
_EXTERNAL_MOVIES_SCHEMA = "openbfme.private-hud-external-movies-oracle"
_EXTERNAL_MOVIES_AGGREGATE = (
    "df584585a406e2532e187f960b8bead6f25158f00733d1b539285a4b22090cf5"
)

_GROUPS = {
    "men-hud-apt-palantir": {
        "archive": "apt/palantir.big",
        "movie": "Palantir",
        "role": "scene",
        "count": 97,
        "bytes": 2_700_505,
        "manifest": "f37cbc3ab14cb02fcf9ff0a5a32f84901e93cc11df958dfb88ec2d76de89f5ab",
        "archiveSha256": "bfec49be38bfe2f5eb4a04de06da0addb4761a622e7f55d48440f6fa5a353010",
    },
    "men-hud-apt-palantir-export": {
        "archive": "apt/palantirexport.big",
        "movie": "PalantirExport",
        "role": "library",
        "count": 21,
        "bytes": 2_075_384,
        "manifest": "f12432c92e6ffd21af4aa309a9b8f4921808de19e6f2d48fc301b569fa1376b8",
        "archiveSha256": "fe0b422f211bdb6348c6a8f763bbff82a406883cbc263d9871fd7a5e6a930134",
    },
    "men-hud-apt-side-command-bar": {
        "archive": "apt/ingamesidecommandbar.big",
        "movie": "InGameSideCommandBar",
        "role": "scene",
        "count": 3,
        "bytes": 17_496,
        "manifest": "0c9a9557175f6ed68aae626202a326012b203919026bdc72a2e72f15de0370f9",
        "archiveSha256": "a93dd36fe8c4bb9d68acb8b01823b2d1c1e18ab8aa09873a9bcab2e63a5ddbab",
    },
    "men-hud-apt-spell-book": {
        "archive": "apt/ingamespellbook.big",
        "movie": "InGameSpellBook",
        "role": "scene",
        "count": 4,
        "bytes": 30_453,
        "manifest": "8cff6c5a7fe6919cde4d256ece54028ff32aa5d8b4d500e36647a1e7906c0358",
        "archiveSha256": "eb0dcc5d79d3b88caf0870aa2bb5e965d099499c2cd67d7275586870f4d2595f",
    },
    "men-hud-apt-help-box": {
        "archive": "apt/ingamehelpbox.big",
        "movie": "InGameHelpBox",
        "role": "scene",
        "count": 13,
        "bytes": 9_377,
        "manifest": "2a490c6da35a8762ac7926fc85c1a0a8b01e072b94b39cfe75bf13ccca292ac9",
        "archiveSha256": "497e433e782202c9666b886d93afd089060dd38c7bbf85d4106013f562b62a7e",
    },
    "men-hud-apt-hero-select": {
        "archive": "apt/ingameheroselect.big",
        "movie": "InGameHeroSelect",
        "role": "scene",
        "count": 27,
        "bytes": 2_282_905,
        "manifest": "b539dce24d3138ed847a40b13a24cef525a7da46d74e0b405f35c6dccc0e5769",
        "archiveSha256": "18d28e1914df15f99720835ee076a3a260aa7177f5202cf70140309339080fc7",
    },
    "men-hud-apt-planning-mode": {
        "archive": "apt/ingameplanningmode.big",
        "movie": "InGamePlanningMode",
        "role": "scene",
        "count": 28,
        "bytes": 1_083_153,
        "manifest": "5388c84388e69667ffd6dd99a358d098e8324247f2afc9d5dda77a1fce982ead",
        "archiveSha256": "acb06c2dfd327bba795cd6821241ca2f55a4f95c419a53a9666d9aab0a2eaabd",
    },
    "men-hud-apt-image-library": {
        "archive": "apt/libingameimagesmain.big",
        "movie": "libInGameImagesMain",
        "role": "library",
        "count": 54,
        "bytes": 2_110_754,
        "manifest": "c8298ab11c67b7fc092639bb93995d0b8a169929ae006e26398b5c8f38dc1121",
        "archiveSha256": "264bb8a1228d209d96b8a3ddfcb0ecd0ca33dc8084cf3e0a92624e60ec7926a4",
    },
    "men-hud-apt-ui-library": {
        "archive": "apt/libingameui.big",
        "movie": "libInGameUI",
        "role": "library",
        "count": 13,
        "bytes": 100_376,
        "manifest": "e5aa643dd1aea44b78cc36020dd72832fb3fcc667a04d0403e9fb78f165057d2",
        "archiveSha256": "19201789afc574296069f9069de881c40bd9c2a4810bffe5385df1f320179bde",
    },
}

_FRAME_EXPORTS = (
    "PalantirFrame_EvilDouble",
    "PalantirFrame_EvilSingle",
    "PalantirFrame_GoodDouble",
    "PalantirFrame_GoodSingle",
)
_EXTERNAL_MOVIE_LOADS = (
    ("InGameSpellBook.swf", "InGameSpellBook", "SpellBookUI"),
    ("InGameSideCommandBar.swf", "InGameSideCommandBar", "SideCommandBar"),
    ("InGameHelpBox.swf", "InGameHelpBox", "helpBox"),
    ("InGameHeroSelect.swf", "InGameHeroSelect", "HeroSelectUI"),
    ("InGamePlanningMode.swf", "InGamePlanningMode", "planningModeUI"),
)
_EXTERNAL_MOVIE_PROFILE_PATTERNS = {
    "men-hud-apt-spell-book": (
        "InGameSpellBook.*",
        "InGameSpellBook_geometry/*.ru",
    ),
    "men-hud-apt-help-box": (
        "InGameHelpBox.*",
        "InGameHelpBox_geometry/*.ru",
    ),
    "men-hud-apt-hero-select": (
        "InGameHeroSelect.*",
        "InGameHeroSelect_geometry/*.ru",
        "art/Textures/apt_InGameHeroSelect_1.tga",
    ),
    "men-hud-apt-planning-mode": (
        "InGamePlanningMode.*",
        "InGamePlanningMode_geometry/*.ru",
        "art/Textures/apt_InGamePlanningMode_1.tga",
    ),
}
_NATIVE_CALLBACK_WHITELIST = (
    "AptPalantir::ClipRadar",
    "AptPalantir::OnBttnMessenger",
    "AptPalantir::OnBttnMovie",
    "AptPalantir::OnBttnObjectives",
    "AptPalantir::OnBttnObserveNextPlayer",
    "AptPalantir::OnBttnObservePriorPlayer",
    "AptPalantir::OnBttnOptions",
    "AptPalantir::OnBttnSpellStore",
    "AptPalantir::OnClosed",
    "AptPalantir::OnHelpBoxLoaded",
    "AptPalantir::OnHelpBoxUnloaded",
    "AptPalantir::OnHeroSelectLoaded",
    "AptPalantir::OnHeroSelectUnloaded",
    "AptPalantir::OnInitialized",
    "AptPalantir::OnPlanningModeUILoaded",
    "AptPalantir::OnPlanningModeUIUnloaded",
    "AptPalantir::RenderGlobe",
    "AptPalantir::RenderMovie",
    "AptPalantir::RenderRadar",
    "AptPalantir::RenderRadarViewBox",
    "OnAptInGameSideCommandBarButtonFrameLoaded",
    "OnAptInGameSideCommandBarButtonFrameUnloaded",
    "OnAptInGameSideCommandBarFadeInComplete",
    "OnAptInGameSideCommandBarFadeOutComplete",
    "OnAptInGameSideCommandBarLoaded",
    "OnAptInGameSideCommandBarUnloaded",
    "PalantirCommandUI::OnButtonFrameLoaded",
    "PalantirCommandUI::OnButtonFrameUnloaded",
    "PalantirCommandUI::OnSubMenuLoaded",
    "PalantirCommandUI::OnSubMenuUnloaded",
    "PalantirCommandUI::OnToggleFlashLoaded",
    "PalantirCommandUI::OnToggleFlashUnloaded",
)

_COMPOSITE_OUTPUTS = (
    {
        "id": "bfme2.ui.palantir.symbols",
        "path": "data/ui/palantir/symbols.json",
        "semantics": "symbol-library-contract",
        "status": "runtime-contract-static-subset-ready-full-parity-fail-closed",
    },
    {
        "id": "bfme2.ui.palantir.timelines",
        "path": "data/ui/palantir/timelines.json",
        "semantics": "timeline-contract",
        "status": "runtime-contract-partial-timeline-fail-closed",
    },
    {
        "id": "bfme2.ui.palantir.imports",
        "path": "data/ui/palantir/imports.json",
        "semantics": "library-and-external-movie-graph",
        "status": "runtime-contract-import-graph-ready-external-edges-fail-closed",
    },
    {
        "id": "bfme2.ui.controlbar.layout.800x600",
        "path": "data/ui/palantir/controlbar-layout.json",
        "semantics": "wnd-layout-contract",
        "status": "runtime-contract-wnd-identities-ready-callbacks-fail-closed",
    },
    {
        "id": "bfme2.ui.palantir.runtime-bindings",
        "path": "data/ui/palantir/runtime-bindings.json",
        "semantics": "native-callback-whitelist",
        "status": "runtime-contract-whitelist-ready-callback-execution-fail-closed",
    },
    {
        "id": "bfme2.ui.palantir.atlases",
        "path": "assets/ui/palantir/atlases",
        "semantics": "lossless-hash-addressed-png-directory",
        "status": "runtime-bundle-atlases-ready",
    },
)


def _mapping(value: object, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"{context} must be an object")
    return dict(value)


def _load_json(path: Path | str, context: str) -> dict[str, Any]:
    source = Path(path).expanduser().resolve()
    if not source.is_file() or source.stat().st_size > _MAX_JSON_BYTES:
        raise ValueError(f"{context} is missing or too large")
    try:
        return _mapping(json.loads(source.read_text(encoding="utf-8")), context)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid {context}: {exc}") from exc


def _private_root(path: Path | str) -> Path:
    root = Path(path).expanduser().resolve()
    if "workspace" not in {part.casefold() for part in root.parts} or not root.is_dir():
        raise ValueError("effective-assets root must be an existing private directory")
    return root


def _validate_external_movies_contract(
    value: Mapping[str, Any],
) -> dict[str, dict[str, Any]]:
    contract = _mapping(value, "HUD external-movies oracle contract")
    declared = contract.get("aggregateSha256")
    basis = dict(contract)
    basis.pop("aggregateSha256", None)
    encoded = (
        json.dumps(basis, sort_keys=True, separators=(",", ":")) + "\n"
    ).encode("utf-8")
    if (
        contract.get("schema") != _EXTERNAL_MOVIES_SCHEMA
        or declared != _EXTERNAL_MOVIES_AGGREGATE
        or hashlib.sha256(encoded).hexdigest() != declared
    ):
        raise ValueError("HUD external-movies oracle contract changed")
    summary = _mapping(contract.get("summary"), "external-movies summary")
    if summary != {
        "alreadySealedMovieCount": 1,
        "genericDispatchAllowed": False,
        "implementationIncluded": False,
        "movieLoadCount": 5,
        "movieLoadsAccounted": 5,
        "newArchiveCount": 4,
        "newPayloadBytes": 3_405_888,
        "newSourceCount": 72,
        "rendererCallbackCount": 5,
        "rendererCallbacksAccounted": 5,
    }:
        raise ValueError("HUD external-movies oracle summary changed")
    proposal = _mapping(
        contract.get("profileDeltaProposal"), "external-movies profile proposal"
    )
    if (
        proposal.get("resourceId") != "men-hud-apt-runtime-bundle"
        or proposal.get("currentSourceCount") != 189
        or proposal.get("prospectiveSourceCount") != _EXPECTED_BUNDLE_SOURCE_COUNT
        or proposal.get("prospectivePayloadBytes") != _EXPECTED_BUNDLE_PAYLOAD_BYTES
        or proposal.get("addSourceCount") != 72
        or proposal.get("addPayloadBytes") != 3_405_888
        or proposal.get("expectedSourceAggregateSha256")
        != "f62347fb78065726715618ed9c73f152c678fec5646ddf7b0855825d1cb23599"
    ):
        raise ValueError("HUD external-movies profile proposal changed")
    movie_loads = contract.get("movieLoads")
    if not isinstance(movie_loads, list) or len(movie_loads) != 5:
        raise ValueError("HUD external-movies load inventory changed")
    by_movie: dict[str, dict[str, Any]] = {}
    actual_order: list[tuple[str, str, str]] = []
    for raw in movie_loads:
        movie = _mapping(raw, "external movie load")
        movie_id = movie.get("movieId")
        swf = movie.get("swf")
        target = movie.get("target")
        if not isinstance(movie_id, str) or movie_id in by_movie:
            raise ValueError("HUD external-movies identity changed")
        actual_order.append((str(swf), movie_id, str(target)))
        by_movie[movie_id] = movie
    if actual_order != list(_EXTERNAL_MOVIE_LOADS):
        raise ValueError("HUD external-movies authored load order changed")
    return by_movie


def _manifest_digest(rows: list[dict[str, Any]]) -> str:
    digest = hashlib.sha256()
    for row in sorted(rows, key=lambda value: (str(value["path"]).casefold(), str(value["path"]))):
        digest.update(str(row["path"]).encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(row["size"]).encode("ascii"))
        digest.update(b"\0")
        digest.update(str(row["sha256"]).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _validate_manifest(
    manifest: Mapping[str, Any], root: Path, catalog: InstallCatalog
) -> tuple[dict[str, list[dict[str, Any]]], dict[str, Any]]:
    rows_raw = manifest.get("files")
    if not isinstance(rows_raw, list):
        raise ValueError("effective-assets manifest files must be an array")
    by_path: dict[str, dict[str, Any]] = {}
    for raw in rows_raw:
        row = _mapping(raw, "effective-assets manifest row")
        path = row.get("path")
        if not isinstance(path, str) or not path or path.casefold() in by_path:
            raise ValueError("effective-assets manifest contains an invalid path")
        if not isinstance(row.get("size"), int) or not isinstance(row.get("sha256"), str):
            raise ValueError("effective-assets manifest contains an invalid source identity")
        by_path[path.casefold()] = row

    groups: dict[str, list[dict[str, Any]]] = {}
    selected: list[dict[str, Any]] = []
    for resource_id, expected in _GROUPS.items():
        rows = [
            row
            for row in by_path.values()
            if str(row.get("archive", "")).casefold() == str(expected["archive"]).casefold()
        ]
        rows.sort(key=lambda value: (str(value["path"]).casefold(), str(value["path"])))
        if len(rows) != expected["count"] or sum(int(row["size"]) for row in rows) != expected["bytes"]:
            raise ValueError(f"{resource_id} source closure count/bytes changed")
        if _manifest_digest(rows) != expected["manifest"]:
            raise ValueError(f"{resource_id} source closure manifest changed")
        groups[resource_id] = rows
        selected.extend(rows)
    if (
        len(selected) != _EXPECTED_APT_FILE_COUNT
        or sum(int(row["size"]) for row in selected)
        != _EXPECTED_APT_PAYLOAD_BYTES
    ):
        raise ValueError("nine-bundle HUD closure total changed")

    wnd = by_path.get(_EXPECTED_WND_PATH.casefold())
    if wnd is None or wnd["sha256"] != _EXPECTED_WND_SHA256:
        raise ValueError("controlbar.wnd source identity changed")
    font = by_path.get(_FONT_VIRTUAL_PATH.casefold())
    if font != {
        "archive": _FONT_ARCHIVE,
        "offset": _FONT_OFFSET,
        "path": _FONT_VIRTUAL_PATH,
        "precedence": _FONT_PRECEDENCE,
        "sha256": _FONT_SHA256,
        "size": _FONT_BYTES,
    }:
        raise ValueError("Albertus MT retail winner identity changed")
    for row in selected + [wnd, font]:
        path = root / Path(str(row["path"]))
        if not path.is_file() or path.stat().st_size != row["size"]:
            raise ValueError(f"private source missing or size changed: {row['path']}")
        payload = path.read_bytes()
        if hashlib.sha256(payload).hexdigest() != row["sha256"]:
            raise ValueError(f"private source digest changed: {row['path']}")
        entry = catalog.resolve_exact(str(row["path"]))
        if entry is None or entry.size != row["size"] or entry.archive.casefold() != str(row["archive"]).casefold():
            raise ValueError(f"catalog winner disagrees with manifest: {row['path']}")
    font_entry = catalog.resolve_exact(_FONT_VIRTUAL_PATH)
    if (
        font_entry is None
        or font_entry.offset != _FONT_OFFSET
        or font_entry.precedence != _FONT_PRECEDENCE
    ):
        raise ValueError("Albertus MT catalog winner offset/precedence changed")
    evidence = {
        "aggregateSha256": manifest.get("aggregate_sha256"),
        "catalog": deepcopy(manifest.get("catalog")),
        "aptBundleFileCount": len(selected),
        "aptBundlePayloadBytes": sum(int(row["size"]) for row in selected),
        "controlbarWnd": {
            "path": wnd["path"],
            "size": wnd["size"],
            "sha256": wnd["sha256"],
            "archive": wnd["archive"],
        },
        "albertusMtFont": {
            "path": font["path"],
            "size": font["size"],
            "sha256": font["sha256"],
            "archive": font["archive"],
            "offset": font["offset"],
            "precedence": font["precedence"],
        },
    }
    return groups, evidence


def _read(root: Path, row: Mapping[str, Any]) -> bytes:
    data = (root / Path(str(row["path"]))).read_bytes()
    if len(data) != row["size"] or hashlib.sha256(data).hexdigest() != row["sha256"]:
        raise ValueError(f"source changed during HUD planning: {row['path']}")
    return data


def _row_by_path(rows: list[dict[str, Any]], path: str) -> dict[str, Any]:
    matches = [row for row in rows if str(row["path"]).casefold() == path.casefold()]
    if len(matches) != 1:
        raise ValueError(f"HUD closure lacks one exact {path}")
    return matches[0]


def _validate_profile(resources: list[dict[str, Any]], catalog: InstallCatalog) -> bool:
    profile = {
        "format": 1,
        "id": "men-fords-hud-apt-fragment-validation",
        "pack": {"id": "men-fords-hud-apt-fragment-validation-pack"},
        "resources": resources,
    }
    with tempfile.TemporaryDirectory(prefix="openbfme-hud-apt-profile-") as raw:
        path = Path(raw) / "profile.json"
        path.write_text(json.dumps(profile), encoding="utf-8")
        loaded = ImportProfile.load(path)
        resolved = resolve_profile(loaded, catalog)
    if resolved.missing_required:
        raise ValueError(f"generated HUD profile has missing resources: {resolved.missing_required}")
    if len(resolved.selected_entries) != _EXPECTED_PROFILE_SELECTED_COUNT:
        raise ValueError("generated HUD profile selected-entry total changed")
    return True


def build_retail_hud_apt_plan(
    effective_assets_root: Path | str,
    effective_assets_manifest: Mapping[str, Any],
    catalog: InstallCatalog,
    oracle_report: bytes,
    external_movies_contract: Mapping[str, Any],
) -> dict[str, Any]:
    """Build the exact static Men HUD scene/library/profile contract."""

    if not oracle_report or len(oracle_report) > _MAX_ORACLE_BYTES:
        raise ValueError("HUD oracle report is missing or too large")
    oracle_text = oracle_report.decode("utf-8")
    for marker in (
        "The five exact source closures total 188 virtual files",
        "7,004,515 payload",
        "SGCommandBar",
        "window/controlbar.wnd",
    ):
        if marker not in oracle_text:
            raise ValueError(f"HUD oracle report lacks required marker: {marker}")

    root = _private_root(effective_assets_root)
    external_movies = _validate_external_movies_contract(external_movies_contract)
    groups, manifest_evidence = _validate_manifest(
        effective_assets_manifest, root, catalog
    )
    for resource_id, movie_id in (
        ("men-hud-apt-spell-book", "InGameSpellBook"),
        ("men-hud-apt-help-box", "InGameHelpBox"),
        ("men-hud-apt-hero-select", "InGameHeroSelect"),
        ("men-hud-apt-planning-mode", "InGamePlanningMode"),
    ):
        oracle_movie = external_movies[movie_id]
        oracle_archive = _mapping(
            oracle_movie.get("archive"), f"{movie_id} archive evidence"
        )
        expected = _GROUPS[resource_id]
        if (
            oracle_archive.get("archive") != expected["archive"]
            or oracle_archive.get("archiveSha256") != expected["archiveSha256"]
            or oracle_archive.get("fileCount") != expected["count"]
            or oracle_archive.get("payloadBytes") != expected["bytes"]
        ):
            raise ValueError(f"{movie_id} external-movie archive evidence changed")
        oracle_files = oracle_movie.get("files")
        if not isinstance(oracle_files, list):
            raise ValueError(f"{movie_id} external-movie file evidence changed")
        oracle_identities = [
            {
                "virtualPath": row.get("virtualPath"),
                "byteLength": row.get("byteLength"),
                "sha256": row.get("sha256"),
            }
            for row in oracle_files
            if isinstance(row, dict)
        ]
        manifest_identities = [
            {
                "virtualPath": row["path"],
                "byteLength": row["size"],
                "sha256": row["sha256"],
            }
            for row in groups[resource_id]
        ]
        if oracle_identities != manifest_identities:
            raise ValueError(f"{movie_id} external-movie file evidence changed")
    movies: list[dict[str, Any]] = []
    atlases: list[dict[str, Any]] = []
    geometry: list[dict[str, Any]] = []
    resources: list[dict[str, Any]] = []
    source_groups: list[dict[str, Any]] = []
    for source_resource_id, expected in _GROUPS.items():
        rows = groups[source_resource_id]
        movie_name = str(expected["movie"])
        apt_row = _row_by_path(rows, f"{movie_name}.apt")
        const_row = _row_by_path(rows, f"{movie_name}.const")
        dat_row = _row_by_path(rows, f"{movie_name}.dat")
        constants = parse_apt_constants(
            _read(root, const_row), str(const_row["path"])
        )
        movie = parse_apt_movie(
            _read(root, apt_row), constants, str(apt_row["path"])
        )
        dat = parse_apt_dat(_read(root, dat_row), str(dat_row["path"]))
        geometry_rows = []
        atlas_rows = []
        for row in rows:
            path = str(row["path"])
            if path.casefold().endswith(".ru"):
                parsed = parse_apt_geometry(_read(root, row), path)
                geometry_rows.append(parsed)
                geometry.append(parsed)
            elif path.casefold().endswith(".tga"):
                parsed = parse_tga_identity(_read(root, row), path)
                atlas_digest = str(parsed["sha256"])
                source_stem = Path(path).stem.casefold().replace("_", "-")
                resource_id = (
                    f"men-hud-atlas-{source_stem[:32]}-{atlas_digest[:8]}"
                )
                cooked_png = (
                    f"assets/ui/palantir/atlases/"
                    f"{source_stem}-{atlas_digest[:12]}.png"
                )
                parsed["resourceId"] = resource_id
                parsed["cookedPng"] = cooked_png
                atlas_rows.append(parsed)
                atlases.append(parsed)
        movies.append(
            {
                "movie": movie_name,
                "role": expected["role"],
                "apt": movie,
                "constants": constants,
                "imageMap": dat,
                "geometry": sorted(
                    geometry_rows, key=lambda row: str(row["virtualPath"]).casefold()
                ),
                "atlases": sorted(
                    atlas_rows, key=lambda row: str(row["virtualPath"]).casefold()
                ),
            }
        )
        source_groups.append(
            {
                "resourceId": source_resource_id,
                "archive": expected["archive"],
                "archiveSha256": expected["archiveSha256"],
                "fileCount": len(rows),
                "payloadBytes": sum(int(row["size"]) for row in rows),
                "manifestSha256": _manifest_digest(rows),
                "files": [
                    {
                        "virtualPath": row["path"],
                        "byteLength": row["size"],
                        "sha256": row["sha256"],
                    }
                    for row in rows
                ],
            }
        )

    wnd_row = _mapping(manifest_evidence["controlbarWnd"], "controlbar WND")
    wnd = parse_wnd_layout(_read(root, wnd_row), _EXPECTED_WND_PATH)
    exact_pattern_groups = set(groups) - set(_EXTERNAL_MOVIE_PROFILE_PATTERNS)
    bundle_patterns = sorted(
        {
            str(row["path"])
            for group_id in exact_pattern_groups
            for row in groups[group_id]
        }
        | {
            pattern
            for patterns in _EXTERNAL_MOVIE_PROFILE_PATTERNS.values()
            for pattern in patterns
        }
        | {_EXPECTED_WND_PATH},
        key=str.casefold,
    )
    if len(bundle_patterns) != 199:
        raise ValueError("HUD APT runtime bundle bounded pattern closure changed")
    resources.append(
        {
            "id": "men-hud-apt-runtime-bundle",
            "kind": "ui",
            "patterns": bundle_patterns,
            "required": True,
            "converter": "sage-apt-runtime",
            "output": "data/ui/palantir/scene-contract.json",
            "options": {
                "expectedSourceAggregateSha256": (
                    "f62347fb78065726715618ed9c73f152c678fec5646ddf7b0855825d1cb23599"
                ),
                "externalFonts": [
                    {
                        "fontId": "palantir:63",
                        "fontName": "Albertus MT",
                        "resourceId": _FONT_RESOURCE_ID,
                        "sourceVirtualPath": _FONT_VIRTUAL_PATH,
                        "cookedFont": _FONT_OUTPUT,
                        "sourceSha256": _FONT_SHA256,
                    }
                ],
            },
            "limit": _EXPECTED_BUNDLE_SOURCE_COUNT,
            "expected_count": _EXPECTED_BUNDLE_SOURCE_COUNT,
        }
    )
    resources.append(
        {
            "id": _FONT_RESOURCE_ID,
            "kind": "ui",
            "patterns": [_FONT_VIRTUAL_PATH],
            "required": True,
            "converter": "copy",
            "output": _FONT_OUTPUT,
            "limit": 1,
            "expected_count": 1,
        }
    )
    profile_validated = _validate_profile(resources, catalog)

    movies.sort(key=lambda row: str(row["movie"]).casefold())
    by_movie = {str(row["movie"]): row for row in movies}
    palantir_import_movies = sorted(
        {str(row["movie"]) for row in by_movie["Palantir"]["apt"]["imports"]},
        key=str.casefold,
    )
    if palantir_import_movies != [
        "libInGameImagesMain",
        "libInGameUI",
        "PalantirExport",
    ]:
        raise ValueError("Palantir exact library dependency graph changed")
    frame_exports = sorted(
        {
            str(row["symbol"])
            for row in by_movie["PalantirExport"]["apt"]["exports"]
            if str(row["symbol"]).startswith("PalantirFrame_")
        },
        key=str.casefold,
    )
    if frame_exports != sorted(_FRAME_EXPORTS, key=str.casefold):
        raise ValueError("Palantir frame export identities changed")
    constant_strings = {
        value
        for movie in movies
        for value in movie["constants"]["strings"]
        if isinstance(value, str)
    }
    external_swf_names = {load[0] for load in _EXTERNAL_MOVIE_LOADS}
    missing_external = sorted(external_swf_names - constant_strings)
    if missing_external:
        raise ValueError(f"Palantir external movie edges changed: {missing_external}")
    if set(by_movie) != {
        "InGameHelpBox",
        "InGameHeroSelect",
        "InGamePlanningMode",
        "InGameSideCommandBar",
        "InGameSpellBook",
        "Palantir",
        "PalantirExport",
        "libInGameImagesMain",
        "libInGameUI",
    }:
        raise ValueError("HUD APT exact nine-movie closure changed")

    scene_contract: dict[str, Any] = {
        "schema": HUD_APT_SCENE_SCHEMA,
        "schemaVersion": HUD_APT_SCENE_SCHEMA_VERSION,
        "sceneId": "bfme2.ui.palantir",
        "frameIds": {
            "evilDouble": "bfme2.ui.palantir.frame.evil.double",
            "evilSingle": "bfme2.ui.palantir.frame.evil.single",
            "goodDouble": "bfme2.ui.palantir.frame.good.double",
            "goodSingle": "bfme2.ui.palantir.frame.good.single",
        },
        "nodeIds": [
            "bfme2.ui.palantir.command-ui",
            "bfme2.ui.palantir.globe",
            "bfme2.ui.palantir.radar",
            "bfme2.ui.palantir.resource-bar",
            "bfme2.ui.palantir.side-command-bar",
        ],
        "movies": movies,
        "layout": wnd,
        "libraryGraph": [
            {
                "movie": movie["movie"],
                "imports": movie["apt"]["imports"],
            }
            for movie in movies
        ],
        "externalMovieEdges": [
            {
                "loadOrder": load_order,
                "movie": swf,
                "resolvesTo": movie_id,
                "target": target,
                "runtimePolicy": "required-unconditional-initial-setup-load",
                "lifecycle": deepcopy(external_movies[movie_id]["lifecycle"]),
            }
            for load_order, (swf, movie_id, target) in enumerate(
                _EXTERNAL_MOVIE_LOADS
            )
        ],
        "callbackBridge": {
            "policy": "exact-whitelist-only-unknown-fails-closed",
            "whitelist": list(_NATIVE_CALLBACK_WHITELIST),
            "wndObservedIdentities": wnd["callbacks"],
            "wndSemantics": "layout-identities-only-not-bound-by-this-foundation",
        },
        "unsupportedSemantics": {
            "actionScript": "byte-range-hash-handoffs-only-never-evaluated",
            "timelineRuntime": "structural-records-only-not-yet-renderable",
            "duplicateExports": "source-order-preserved-selection-unresolved",
            "unknownCallbacks": "data-only-fail-closed",
            "externalLoads": "network-and-filesystem-loads-forbidden",
        },
        "runtimeAssetBindings": {
            "sourceVirtualPathsAreEvidenceOnly": True,
            "externalFonts": [
                {
                    "fontId": "palantir:63",
                    "fontName": "Albertus MT",
                    "resourceId": _FONT_RESOURCE_ID,
                    "sourceVirtualPath": _FONT_VIRTUAL_PATH,
                    "cookedFont": _FONT_OUTPUT,
                    "sourceSha256": _FONT_SHA256,
                }
            ],
            "atlasTextures": [
                {
                    "resourceId": atlas["resourceId"],
                    "png": atlas["cookedPng"],
                    "sha256OfRetailSource": atlas["sha256"],
                }
                for atlas in sorted(
                    atlases, key=lambda row: str(row["cookedPng"]).casefold()
                )
            ],
            "symbolAndTimelineBinding": (
                "bounded-static-runtime-contract-ready-full-parity-fail-closed"
            ),
        },
    }
    scene_contract["aggregateSha256"] = canonical_sha256(scene_contract)
    plan: dict[str, Any] = {
        "schema": HUD_APT_PLAN_SCHEMA,
        "schemaVersion": HUD_APT_PLAN_SCHEMA_VERSION,
        "sourceEvidence": {
            "oracleReportSha256": hashlib.sha256(oracle_report).hexdigest(),
            "externalMoviesOracleAggregateSha256": _EXTERNAL_MOVIES_AGGREGATE,
            "effectiveAssets": manifest_evidence,
            "groups": source_groups,
            "privateReadBoundary": {
                "policy": "exact-manifest-sources-below-private-root",
                "aptBundleFileCount": _EXPECTED_APT_FILE_COUNT,
                "aptBundlePayloadBytes": _EXPECTED_APT_PAYLOAD_BYTES,
                "runtimeBundleSourceCount": _EXPECTED_BUNDLE_SOURCE_COUNT,
                "runtimeBundlePayloadBytes": _EXPECTED_BUNDLE_PAYLOAD_BYTES,
                "layoutFileCount": 1,
            },
        },
        "policy": {
            "scope": "men-fords-nine-apt-bundle-unconditional-load-closure-plus-controlbar-wnd",
            "universalFlashRuntime": False,
            "executesActionScript": False,
            "substitutesAllowed": False,
            "genericArtAllowed": False,
            "retailPayloadInPlan": False,
            "profileFragmentValidatedByImportProfileAndCatalog": profile_validated,
        },
        "legacyAssumptionReplacement": {
            "rejectedMappedImageId": "SGCommandBar",
            "reason": "definition-references-absent-retail-payload-and-shipped-hud-is-apt-composite",
            "requiredSceneId": "bfme2.ui.palantir",
            "requiredPackFileKey": "palantirScene",
            "atomicPrivateBinding": True,
        },
        "compositeOutputs": [deepcopy(row) for row in _COMPOSITE_OUTPUTS],
        "atlasClosure": sorted(
            atlases, key=lambda row: str(row["virtualPath"]).casefold()
        ),
        "geometryClosure": sorted(
            geometry, key=lambda row: str(row["virtualPath"]).casefold()
        ),
        "sceneContract": scene_contract,
        "profileFragment": {"resources": resources},
        "summary": {
            "aptBundleCount": 9,
            "aptClosureFileCount": _EXPECTED_APT_FILE_COUNT,
            "aptClosurePayloadBytes": _EXPECTED_APT_PAYLOAD_BYTES,
            "layoutFileCount": 1,
            "profileResourceCount": len(resources),
            "profileSelectedSourceCount": _EXPECTED_PROFILE_SELECTED_COUNT,
            "fontResourceCount": 1,
            "sourceAttestationResourceCount": 0,
            "textureConversionResourceCount": 0,
            "runtimeBundleResourceCount": 1,
            "atlasCount": len(atlases),
            "geometryFileCount": len(geometry),
            "movieCount": len(movies),
            "nativeCallbackWhitelistCount": len(_NATIVE_CALLBACK_WHITELIST),
            "wndWindowCount": wnd["windowCount"],
            "compositeOutputCount": len(_COMPOSITE_OUTPUTS),
            "unresolvedExternalMovieCount": 0,
        },
    }
    if plan["summary"]["atlasCount"] != 24 or plan["summary"]["geometryFileCount"] != 209:
        raise ValueError("HUD atlas/geometry closure counts changed")
    plan["aggregateSha256"] = canonical_sha256(plan)
    return plan


def generated_import_profile(
    plan: Mapping[str, Any],
    *,
    profile_id: str = "men-fords-v0-hud-apt-generated",
    pack_id: str = "bfme2-men-vslice-hud-apt-private",
) -> dict[str, Any]:
    """Return the catalog-resolvable source profile for the static contract."""

    document = _mapping(plan, "HUD APT plan")
    declared = document.get("aggregateSha256")
    basis = dict(document)
    basis.pop("aggregateSha256", None)
    if declared != canonical_sha256(basis):
        raise ValueError("HUD APT plan aggregate digest mismatch")
    fragment = _mapping(document.get("profileFragment"), "HUD profileFragment")
    resources = fragment.get("resources")
    if not isinstance(resources, list) or len(resources) != 2:
        raise ValueError("HUD APT profile fragment must contain bundle and font resources")
    if [resource.get("id") for resource in resources if isinstance(resource, dict)] != [
        "men-hud-apt-runtime-bundle",
        _FONT_RESOURCE_ID,
    ]:
        raise ValueError("HUD APT profile fragment resource identities changed")
    profile = {
        "format": 1,
        "id": profile_id,
        "title": "Private BFME II Men/Fords exact APT HUD source closure",
        "pack": {
            "id": pack_id,
            "version": "1.06-plan-v0",
            "dataPolicy": {
                "externalPathsAllowed": False,
                "redistributable": False,
            },
        },
        "resources": deepcopy(resources),
    }
    with tempfile.TemporaryDirectory(prefix="openbfme-hud-apt-generated-") as raw:
        path = Path(raw) / "profile.json"
        path.write_text(json.dumps(profile), encoding="utf-8")
        ImportProfile.load(path)
    return profile


def write_retail_hud_apt_plan(path: Path | str, plan: Mapping[str, Any]) -> None:
    document = _mapping(plan, "HUD APT plan")
    declared = document.get("aggregateSha256")
    basis = dict(document)
    basis.pop("aggregateSha256", None)
    if document.get("schema") != HUD_APT_PLAN_SCHEMA or declared != canonical_sha256(basis):
        raise ValueError("cannot write invalid HUD APT plan")
    write_json_atomic(Path(path), deepcopy(document))


def write_generated_import_profile(path: Path | str, profile: Mapping[str, Any]) -> None:
    document = _mapping(profile, "generated HUD ImportProfile")
    with tempfile.TemporaryDirectory(prefix="openbfme-hud-apt-write-") as raw:
        check = Path(raw) / "profile.json"
        check.write_text(json.dumps(document), encoding="utf-8")
        ImportProfile.load(check)
    write_json_atomic(Path(path), deepcopy(document))


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--effective-assets", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--catalog", required=True)
    parser.add_argument("--oracle-report", required=True)
    parser.add_argument("--external-movies-contract", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--profile", required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    oracle_path = Path(args.oracle_report).expanduser().resolve()
    if not oracle_path.is_file() or oracle_path.stat().st_size > _MAX_ORACLE_BYTES:
        raise ValueError("HUD oracle report is missing or too large")
    plan = build_retail_hud_apt_plan(
        args.effective_assets,
        _load_json(args.manifest, "effective-assets manifest"),
        InstallCatalog.load(args.catalog),
        oracle_path.read_bytes(),
        _load_json(
            args.external_movies_contract, "HUD external-movies oracle contract"
        ),
    )
    profile = generated_import_profile(plan)
    write_retail_hud_apt_plan(args.output, plan)
    write_generated_import_profile(args.profile, profile)
    print(
        json.dumps(
            {
                "aggregateSha256": plan["aggregateSha256"],
                "profileResourceCount": plan["summary"]["profileResourceCount"],
                "selectedSourceCount": plan["summary"]["profileSelectedSourceCount"],
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


__all__ = [
    "HUD_APT_PLAN_SCHEMA",
    "HUD_APT_PLAN_SCHEMA_VERSION",
    "HUD_APT_SCENE_SCHEMA",
    "HUD_APT_SCENE_SCHEMA_VERSION",
    "build_retail_hud_apt_plan",
    "generated_import_profile",
    "write_generated_import_profile",
    "write_retail_hud_apt_plan",
]
