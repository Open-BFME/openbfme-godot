from __future__ import annotations

import json
import fnmatch
from pathlib import Path

import pytest

from openbfme_importer.retail_hud_external_movies_oracle import build_contract
from tests.retail_inputs import retail_file


ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "workspace" / "retail-work" / "cache" / "effective-assets"
MANIFEST = ASSETS / ".openbfme" / "manifest.json"
CATALOG = ROOT / "workspace" / "retail-work" / "catalog" / "bfme2.json"
PROFILE = ROOT / "workspace" / "retail-work" / "profiles" / "men-fords-v0-complete.generated.json"
GAME_DAT = retail_file("game.dat")
OPENSAGE = ROOT / "workspace" / "scratch" / "opensage-source"

pytestmark = pytest.mark.skipif(
    not all(path.is_file() for path in (MANIFEST, CATALOG, PROFILE, GAME_DAT)),
    reason="private retail HUD oracle inputs are absent",
)


def _build(profile: dict | None = None) -> dict:
    return build_contract(
        ASSETS,
        json.loads(MANIFEST.read_text(encoding="utf-8")),
        json.loads(CATALOG.read_text(encoding="utf-8")),
        profile if profile is not None else json.loads(PROFILE.read_text(encoding="utf-8")),
        GAME_DAT,
        opensage_root=OPENSAGE,
    )


def test_movie_and_callback_closure_is_exact_and_deterministic() -> None:
    first = _build()
    second = _build()
    assert first == second
    assert first["summary"] == {
        "movieLoadCount": 5,
        "movieLoadsAccounted": 5,
        "rendererCallbackCount": 5,
        "rendererCallbacksAccounted": 5,
        "alreadySealedMovieCount": 1,
        "newArchiveCount": 4,
        "newSourceCount": 72,
        "newPayloadBytes": 3_405_888,
        "implementationIncluded": False,
        "genericDispatchAllowed": False,
    }
    assert [row["movieId"] for row in first["movieLoads"]] == [
        "InGameSpellBook",
        "InGameSideCommandBar",
        "InGameHelpBox",
        "InGameHeroSelect",
        "InGamePlanningMode",
    ]
    assert all(row["sliceLoadReachable"] for row in first["movieLoads"])
    assert sum(row["alreadyInSealedHudSourceClosure"] for row in first["movieLoads"]) == 1


def test_exact_profile_delta_and_dependencies_are_accounted() -> None:
    contract = _build()
    delta = contract["profileDeltaProposal"]
    assert delta["currentSourceCount"] == 189
    assert delta["addSourceCount"] == 72
    assert delta["prospectiveSourceCount"] == 261
    assert delta["prospectivePayloadBytes"] == 10_700_284
    assert delta["expectedSourceAggregateSha256"] == (
        "f62347fb78065726715618ed9c73f152c678fec5646ddf7b0855825d1cb23599"
    )
    assert len(delta["newPatterns"]) == 72
    assert all(len(row["files"]) == row["archive"]["fileCount"] for row in contract["movieLoads"])
    assert all(row["parsed"]["apt"]["sha256"] for row in contract["movieLoads"])
    assert all(row["parsed"]["const"]["sha256"] for row in contract["movieLoads"])
    assert all(row["parsed"]["dat"]["sha256"] for row in contract["movieLoads"])


def test_renderer_callbacks_have_exact_apt_and_retail_code_evidence() -> None:
    contract = _build()
    callbacks = contract["rendererCallbacks"]
    assert len(callbacks) == len(contract["aptCallbackBindings"]) == 5
    assert len(contract["retailCallbackCode"]) == 5
    assert {row["name"] for row in callbacks} == {
        "AptPalantir::ClipRadar",
        "AptPalantir::RenderGlobe",
        "AptPalantir::RenderMovie",
        "AptPalantir::RenderRadar",
        "AptPalantir::RenderRadarViewBox",
    }
    assert all(row["typedInterface"] and row["dataSource"] for row in callbacks)
    assert all(not row["genericDispatchAllowed"] for row in callbacks)
    assert all(row["eventNames"] == ["unload"] for row in contract["aptCallbackBindings"])


def test_integrated_delta_requires_exact_path_coverage_not_only_pattern_count() -> None:
    contract = _build()
    delta_paths = contract["profileDeltaProposal"]["newPatterns"]
    profile = json.loads(PROFILE.read_text(encoding="utf-8"))
    resource = next(row for row in profile["resources"] if row["id"] == "men-hud-apt-runtime-bundle")
    integrated = [
        pattern
        for pattern in resource["patterns"]
        if any(fnmatch.fnmatchcase(path.casefold(), pattern.casefold()) for path in delta_paths)
    ]
    assert len(integrated) == 10
    resource["patterns"][resource["patterns"].index(integrated[0])] = "unrelated/replacement.pattern"
    with pytest.raises(ValueError, match="external-movie closure changed"):
        _build(profile)


def test_integrated_delta_rejects_overlapping_substitute_glob() -> None:
    profile = json.loads(PROFILE.read_text(encoding="utf-8"))
    resource = next(row for row in profile["resources"] if row["id"] == "men-hud-apt-runtime-bundle")
    exact = "art/Textures/apt_InGameHeroSelect_1.tga"
    resource["patterns"][resource["patterns"].index(exact)] = "art/Textures/apt_InGame*.tga"
    with pytest.raises(ValueError, match="external-movie closure changed"):
        _build(profile)
