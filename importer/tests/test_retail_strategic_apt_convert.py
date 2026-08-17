"""Gate the RotWK War of the Ring strategic APT ingress lane."""

from __future__ import annotations

import json
import struct
from pathlib import Path

import pytest

from openbfme_importer import retail_strategic_apt_convert as convert
from openbfme_importer import retail_strategic_apt_profile as profile
from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.retail_hud_apt_convert import (
    HudAptConvertError,
    register_expected_flagged_null_clip_actions,
)
from openbfme_importer.sage_apt import canonical_sha256


PRIVATE_ROOT = Path(__file__).resolve().parents[2] / "workspace" / "retail-work"
#: THE ROTWK EDITION'S OWN VIEW.  This lane cooks Rise of the Witch King's
#: strategic screens, so it reads the RotWK overlay (RotWK's archives over
#: BFME2's) -- NOT ``cache/effective-assets``, which is the plain BFME2
#: install view.  Pointing this at the BFME2 view is exactly the defect this
#: lane shipped: seventeen strategic and shell sheets differ between the
#: editions, and the whole bundle was cooked from the wrong one.
#: ``test_bfme2_view_is_refused_by_the_planner`` keeps that door shut.
EFFECTIVE_ASSETS = PRIVATE_ROOT / "editions" / "rotwk" / "cache" / "effective-assets"
MANIFEST = EFFECTIVE_ASSETS / ".openbfme" / "manifest.json"
CATALOG = PRIVATE_ROOT / "catalog" / "rotwk.json"
BFME2_EFFECTIVE_ASSETS = PRIVATE_ROOT / "cache" / "effective-assets"
BFME2_MANIFEST = BFME2_EFFECTIVE_ASSETS / ".openbfme" / "manifest.json"

private_sources = pytest.mark.skipif(
    not (EFFECTIVE_ASSETS.is_dir() and MANIFEST.is_file() and CATALOG.is_file()),
    reason="private retail effective-assets view is not present",
)


# --------------------------------------------------------------------------
# Closure identity
# --------------------------------------------------------------------------


def test_screen_closure_is_the_complete_war_of_the_ring_set() -> None:
    # The 24 strategic screens plus the six libraries they import from.
    assert len(convert.SCREEN_MOVIES) == 24
    assert convert.SCREEN_MOVIES[0] == "LivingWorldUI"
    for required in (
        "StrategicHUD",
        "StrategicPalantir",
        "StrategicDetailsTray",
        "StrategicEndTurnButton",
        "StrategicPlayerStatus",
        "StrategicBattlePrompt",
        "StrategicConflictResults",
        "StrategicDynamicAutoResolve",
        "TimeLine",
    ):
        assert required in convert.SCREEN_MOVIES
    assert convert.LIBRARY_MOVIES == (
        "libInGameUI",
        "libInGameImagesMain",
        "PalantirExport",
        "GameWindowGadgets",
        "MenuExport",
        "MenuFrameAndBg",
    )
    assert len(convert.MOVIE_CLOSURE) == 30
    assert convert.OUTPUT_SCHEMA == "openbfme.strategic-ui"
    assert convert.SCREEN_SCHEMA == "openbfme.strategic-ui-screen"
    assert convert.MANIFEST_FILE_NAME == "strategic-ui.json"
    assert convert.SCENE_ID == "rotwk.ui.strategic"


def test_movie_of_maps_every_closure_shape() -> None:
    assert convert._movie_of("StrategicPalantir.apt") == "StrategicPalantir"
    assert convert._movie_of("TimeLine_geometry/16.ru") == "TimeLine"
    assert (
        convert._movie_of("art/Textures/apt_StrategicEndTurnButton_1.tga")
        == "StrategicEndTurnButton"
    )
    # The in-game palantir root movie is NOT part of the strategic closure.
    assert convert._movie_of("Palantir.apt") is None
    assert convert._movie_of("window/controlbar.wnd") is None


def test_out_of_closure_source_is_rejected() -> None:
    with pytest.raises(convert.StrategicAptConvertError):
        convert._required_strategic_contract(
            {"palantir.apt": b"x"}, {"palantir.apt": "Palantir.apt"}
        )


def test_named_gaps_stay_honest_and_named() -> None:
    gaps = {row["gap"] for row in convert.NAMED_GAPS}
    assert gaps == {
        "nine-slice-metrics-not-authored",
        "timeline-playback-not-bound",
        "action-scripts-not-executed",
        "dynamic-content-slots-are-empty",
        "strategic-text-values-are-live",
        "sachawynter-font-binding-unproven",
    }
    for row in convert.NAMED_GAPS:
        assert len(str(row["detail"])) > 40


def test_flagged_null_clip_action_registration_is_fail_closed() -> None:
    # The eight measured TimeLine identities registered at import are exact.
    assert len(convert.EXPECTED_FLAGGED_NULL_CLIP_ACTIONS) == 8
    assert all(
        path == "timeline.apt" and flags == 0xA6
        for path, _offset, flags in convert.EXPECTED_FLAGGED_NULL_CLIP_ACTIONS
    )
    # RotWK's TimeLine.apt offsets, re-measured when the lane moved off the
    # BFME2 asset layer. BFME2's were 120,344..120,792; these must not be.
    offsets = [offset for _path, offset, _flags in convert.EXPECTED_FLAGGED_NULL_CLIP_ACTIONS]
    assert offsets == list(range(72_228, 72_228 + 8 * 64, 64))
    # Registration validates identity shape; hostile rows are refused.
    for hostile in (
        ("", 1, 1),
        ("TimeLine.apt", 1, 1),  # not casefolded
        ("timeline.apt", -1, 1),
        ("timeline.apt", 1, 0x1FF),
    ):
        with pytest.raises(HudAptConvertError):
            register_expected_flagged_null_clip_actions([hostile])


# --------------------------------------------------------------------------
# Font identity
# --------------------------------------------------------------------------


def _sfnt_with_family(family: str) -> bytes:
    encoded = family.encode("utf-16-be")
    name_table = struct.pack(">HHH", 0, 1, 18) + struct.pack(
        ">6H", 3, 1, 0x0409, 1, len(encoded), 0
    ) + encoded
    header = b"\x00\x01\x00\x00" + struct.pack(">HHHH", 1, 16, 0, 0)
    directory = struct.pack(">4sIII", b"name", 0, 28, len(name_table))
    return header + directory + name_table


def test_embedded_font_family_reads_the_authored_name_table() -> None:
    assert (
        convert._embedded_font_family(_sfnt_with_family("SachaWynterTight"), "t")
        == "SachaWynterTight"
    )
    with pytest.raises(convert.StrategicAptConvertError):
        convert._embedded_font_family(b"not a font at all", "t")
    with pytest.raises(convert.StrategicAptConvertError):
        convert._embedded_font_family(b"\x00\x01\x00\x00" + b"\x00" * 8, "t")


def test_only_albertus_has_a_proven_apt_binding() -> None:
    proven = [row for row in convert.FONT_SOURCES if row["bindingProven"]]
    assert [row["aptFontName"] for row in proven] == ["Albertus MT"]
    unproven = [row for row in convert.FONT_SOURCES if not row["bindingProven"]]
    # SachaWynter deliberately binds to NOTHING: no shipped family matches.
    assert all(row["aptFontName"] is None for row in unproven)


def test_font_rows_fail_closed_on_digest_or_family_drift() -> None:
    payloads = {"albertusmt.otf": _sfnt_with_family("Albertus MT")}
    with pytest.raises(convert.StrategicAptConvertError):
        convert._font_rows(payloads)  # wrong digest
    with pytest.raises(convert.StrategicAptConvertError):
        convert._font_rows({})  # missing winner


# --------------------------------------------------------------------------
# Display-list re-encoding
# --------------------------------------------------------------------------


def test_synthetic_place_rows_preserve_the_exact_cumulative_state() -> None:
    entry = {
        "depth": 7,
        "characterId": 12,
        "matrix": [2.0, 0.0, 0.0, 2.0],
        "translation": [10.0, 20.0],
        "tint": [1.0, 0.5, 0.25, 1.0],
        "additive": [0.0, 0.0, 0.0, 0.0],
        "ratio": 0.5,
        "name": "endTurnButton",
        "clipDepth": 0,
        "sourceOffsets": [100, 200],
    }
    (row,) = convert._synthetic_place_rows([entry])
    assert row["kind"] == "place-object"
    assert row["flags"] & 0x02 and row["flags"] & 0x04 and row["flags"] & 0x08
    assert row["flags"] & 0x20 and not row["flags"] & 0x40
    assert row["name"] == "endTurnButton"
    assert row["sourceOffset"] == 200
    assert row["matrix"] == [2.0, 0.0, 0.0, 2.0]
    masked = dict(entry, clipDepth=3, name="")
    (mask_row,) = convert._synthetic_place_rows([masked])
    assert mask_row["flags"] & 0x40 and "name" not in mask_row


def test_compact_timeline_summarises_offsets_without_losing_identity() -> None:
    timeline = {
        "frames": [
            {
                "displayList": [
                    {"depth": 1, "sourceOffsets": [5, 9, 13]},
                    {"depth": 2, "sourceOffsets": []},
                ]
            }
        ]
    }
    convert._compact_timeline(timeline)
    first, second = timeline["frames"][0]["displayList"]
    assert (
        first["sourceOffsetFirst"],
        first["sourceOffsetLast"],
        first["sourceOffsetCount"],
    ) == (5, 13, 3)
    assert second["sourceOffsetCount"] == 0 and second["sourceOffsetFirst"] == -1
    assert "sourceOffsets" not in first


# --------------------------------------------------------------------------
# Sealed source closure and plan (private view required)
# --------------------------------------------------------------------------


@private_sources
def test_strategic_source_closure_matches_the_sealed_identity() -> None:
    paths = convert.strategic_source_virtual_paths(EFFECTIVE_ASSETS)
    assert len(paths) == profile.EXPECTED_SOURCE_COUNT
    assert paths == sorted(paths, key=str.casefold)
    total = sum(
        (EFFECTIVE_ASSETS / Path(*path.split("/"))).stat().st_size for path in paths
    )
    assert total == profile.EXPECTED_PAYLOAD_BYTES


@private_sources
def test_plan_seals_the_closure_and_validates_against_the_catalog(
    tmp_path: Path,
) -> None:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    catalog = InstallCatalog.load(CATALOG)
    plan = profile.build_retail_strategic_apt_plan(
        EFFECTIVE_ASSETS, manifest, catalog
    )
    assert plan["schema"] == profile.STRATEGIC_APT_PLAN_SCHEMA
    assert plan["sceneId"] == convert.SCENE_ID
    assert plan["summary"]["sourceCount"] == profile.EXPECTED_SOURCE_COUNT
    assert plan["summary"]["archiveCount"] == len(profile.EXPECTED_ARCHIVES)
    assert (
        plan["sourceEvidence"]["sourceAggregateSha256"]
        == profile.EXPECTED_SOURCE_AGGREGATE_SHA256
    )
    assert plan["policy"]["executesActionScript"] is False
    assert plan["policy"]["substitutesAllowed"] is False
    # THE EDITION IS PROVED, NOT NAMED. The plan must show it bound the view
    # to a RotWK catalog whose expansion layer shadows the BFME2 layer.
    edition = plan["sourceEvidence"]["edition"]
    assert edition["game"] == "rotwk" == profile.EXPECTED_GAME
    assert edition["layerPrecedence"] == ["layer-0-rotwk", "layer-1-bfme2"]
    assert edition["catalogIdentitySha256"] == catalog.identity_sha256()
    # RotWK's own archives must supply the strategic screens; the BFME2 layer
    # only ever contributes geometry/texture pages RotWK never replaced.
    assert all(
        archive.startswith("layer-0-rotwk/") or archive.startswith("layer-1-bfme2/")
        for archive in plan["sourceEvidence"]["archives"]
    )
    for movie_archive in (
        "layer-0-rotwk/apt/strategicpalantir.big",
        "layer-0-rotwk/apt/strategichud.big",
        "layer-0-rotwk/apt/livingworldui.big",
        "layer-0-rotwk/apt/menuexport.big",
    ):
        assert plan["sourceEvidence"]["archives"][movie_archive] > 0
    basis = dict(plan)
    basis.pop("aggregateSha256")
    assert plan["aggregateSha256"] == canonical_sha256(basis)

    plan_path = tmp_path / "plan.json"
    profile.write_retail_strategic_apt_plan(plan_path, plan)
    assert json.loads(plan_path.read_text(encoding="utf-8")) == plan
    tampered = dict(plan)
    tampered["aggregateSha256"] = "0" * 64
    with pytest.raises(ValueError):
        profile.write_retail_strategic_apt_plan(tmp_path / "bad.json", tampered)


@pytest.mark.skipif(
    not (BFME2_EFFECTIVE_ASSETS.is_dir() and BFME2_MANIFEST.is_file() and CATALOG.is_file()),
    reason="private BFME2 effective-assets view is not present",
)
def test_bfme2_view_is_refused_by_the_planner() -> None:
    """THE REGRESSION THIS LANE ALREADY SHIPPED, nailed shut.

    The BFME2 view used to validate cleanly against ``catalog/rotwk.json``
    because the planner stripped the install-layer prefix before comparing
    archives, and BFME2's ``apt/menuexport.big`` is byte-identical to the
    ``layer-1-bfme2/apt/menuexport.big`` the RotWK catalog indexes. The view
    is now bound to the catalog it was extracted from, so it cannot pass.
    """

    manifest = json.loads(BFME2_MANIFEST.read_text(encoding="utf-8"))
    catalog = InstallCatalog.load(CATALOG)
    with pytest.raises(ValueError, match="not extracted from this catalog"):
        profile.build_retail_strategic_apt_plan(
            BFME2_EFFECTIVE_ASSETS, manifest, catalog
        )


@pytest.mark.skipif(
    not (PRIVATE_ROOT / "catalog" / "bfme2.json").is_file(),
    reason="private BFME2 catalog is not present",
)
def test_a_bfme2_catalog_cannot_serve_the_rotwk_lane() -> None:
    catalog = InstallCatalog.load(PRIVATE_ROOT / "catalog" / "bfme2.json")
    manifest = json.loads(BFME2_MANIFEST.read_text(encoding="utf-8"))
    with pytest.raises(ValueError, match="is not a rotwk catalog"):
        profile.build_retail_strategic_apt_plan(
            BFME2_EFFECTIVE_ASSETS, manifest, catalog
        )


# --------------------------------------------------------------------------
# Conversion (private view required)
# --------------------------------------------------------------------------


def _sources() -> dict[str, Path]:
    return {
        path: EFFECTIVE_ASSETS.joinpath(*path.split("/"))
        for path in convert.strategic_source_virtual_paths(EFFECTIVE_ASSETS)
    }


@private_sources
def test_conversion_produces_the_bounded_strategic_bundle(tmp_path: Path) -> None:
    output = tmp_path / "out"
    manifest = convert.convert_strategic_apt_bundle(
        _sources(),
        output,
        expected_source_aggregate_sha256=(
            profile.EXPECTED_SOURCE_AGGREGATE_SHA256
        ),
    )
    assert manifest["schema"] == "openbfme.strategic-ui"
    assert manifest["schemaVersion"] == 0
    assert manifest["sceneId"] == "rotwk.ui.strategic"
    policy = manifest["renderPolicy"]
    assert policy["actionScriptExecuted"] is False
    assert policy["timelinePlaybackBound"] is False
    assert policy["syntheticFallbackAllowed"] is False

    summary = manifest["summary"]
    assert summary["screenCount"] == 24
    assert summary["atlasCount"] == 79
    assert summary["fontCount"] == 3
    # DRAW BUDGET, RESTATED FOR ROTWK -- and note WHY the total moved rather
    # than only that it did. Cooking from BFME2 gave 10,529 draws of which
    # only 5,554 were textured; RotWK gives 9,571 of which 6,062 are. The
    # difference is one BFME2 sprite on TimeLine's `_strategicEndOpen` frames
    # that flattened to 620 SOLID vector triangles per frame, which RotWK
    # replaces with its textured blue-steel plate. Fewer draws, MORE retail
    # art: the textured floor below is therefore raised, not lowered.
    assert summary["drawCount"] > 9_500
    assert summary["texturedTriangleCount"] > 6_000
    assert summary["namedInstanceCount"] > 530
    gaps = {row["gap"] for row in manifest["namedGaps"]}
    assert "nine-slice-metrics-not-authored" in gaps
    assert "sachawynter-font-binding-unproven" in gaps

    # PER-TEXTURE EDITION PROOF. Every cooked atlas states the retail sheet
    # it came from, and the sheets that differ between editions must carry
    # ROTWK's digest. These four are measured cross-edition differences: the
    # palantir ring, the stats plaque, and the shell plate MenuExport draws
    # (BFME2's is green, RotWK's is blue-steel).
    atlas_sources = {
        str(row["sourceVirtualPath"]).casefold(): row
        for row in manifest["atlasSources"]
    }
    assert len(atlas_sources) == summary["atlasCount"]
    for virtual_path, rotwk_sha256, bfme2_sha256 in (
        (
            "art/textures/apt_strategicpalantir_1.tga",
            "cd13947c49ba",
            "cc4ce1696fed",
        ),
        ("art/textures/apt_strategicstats_1.tga", "305a827291be", "88cd052ba75a"),
        ("art/textures/apt_menuexport_1.tga", "68d4eadab9cc", "83db043843ce"),
        ("art/textures/apt_menuexport_2.tga", "3b51973b8e25", "268de9714542"),
    ):
        row = atlas_sources[virtual_path]
        assert str(row["sha256"]).startswith(rotwk_sha256), virtual_path
        assert not str(row["sha256"]).startswith(bfme2_sha256), virtual_path
        assert row["cookedPng"] in manifest["atlases"]
    # RotWK ships THREE MenuExport sheets; BFME2 ships two. A bundle carrying
    # only two came from the wrong edition.
    assert "art/textures/apt_menuexport_3.tga" in atlas_sources

    # Every declared artefact actually landed.
    cooked = {
        path.relative_to(output).as_posix() for path in output.rglob("*") if path.is_file()
    }
    assert "strategic-ui.json" in cooked
    for atlas in manifest["atlases"]:
        assert atlas.startswith("assets/ui/strategic/atlases/") and atlas in cooked
    for row in manifest["fonts"]:
        assert row["cookedFont"] in cooked
    for row in manifest["supportingData"]:
        assert row["virtualPath"] in cooked
    for row in manifest["screens"]:
        assert row["file"] in cooked

    # THE POINT OF THE LANE: the ornament screens carry real textured retail
    # draws, and the palantir's authored stop frame carries the palantir art.
    by_movie = {row["movie"]: row for row in manifest["screens"]}
    palantir = json.loads(
        (output / by_movie["StrategicPalantir"]["file"]).read_text(encoding="utf-8")
    )
    stop_frames = [
        frame for frame in palantir["flattenedFrames"] if frame["authoredStop"]
    ]
    assert stop_frames and stop_frames[0]["drawCount"] > 1_000
    for movie in (
        "StrategicDetailsTray",
        "StrategicDetailsTerritory",
        "StrategicEndTurnButton",
        "StrategicDetailsBuildQueue",
        "TimeLine",
    ):
        assert by_movie[movie]["summary"]["texturedTriangleCount"] > 0, movie
    # The END TURN button ships every authored interaction state by label.
    end_turn = by_movie["StrategicEndTurnButton"]["labels"]
    assert {"_up", "_over", "_down", "_disabled"} <= set(end_turn)
    # LivingWorldUI's art lives in its exported symbols, not its root frame.
    lwui = json.loads(
        (output / by_movie["LivingWorldUI"]["file"]).read_text(encoding="utf-8")
    )
    exported = {row["symbol"] for row in lwui["exports"]}
    assert exported == {
        "RegionConqueredNoticeEvil",
        "RegionConqueredNoticeGood",
        "RegionPopup",
    }
    assert all(
        any(frame["drawCount"] > 0 for frame in row["flattenedFrames"])
        for row in lwui["exports"]
    )

    # Textured draws only ever reference cooked, hash-addressed atlases.
    for frame in palantir["flattenedFrames"]:
        for draw in frame["draws"]:
            if draw["kind"] == "textured-triangle":
                assert draw["atlas"] in manifest["atlases"]
            assert len(draw["points"]) == 3

    basis = dict(manifest)
    basis.pop("aggregateSha256")
    assert manifest["aggregateSha256"] == canonical_sha256(basis)


@private_sources
def test_conversion_is_deterministic(tmp_path: Path) -> None:
    first = convert.convert_strategic_apt_bundle(_sources(), tmp_path / "a")
    second = convert.convert_strategic_apt_bundle(_sources(), tmp_path / "b")
    assert first["aggregateSha256"] == second["aggregateSha256"]


@private_sources
def test_changed_source_aggregate_fails_closed(tmp_path: Path) -> None:
    with pytest.raises(convert.StrategicAptConvertError):
        convert.convert_strategic_apt_bundle(
            _sources(), tmp_path / "out", expected_source_aggregate_sha256="0" * 64
        )


@private_sources
def test_non_empty_output_directory_fails_closed(tmp_path: Path) -> None:
    output = tmp_path / "out"
    output.mkdir()
    (output / "stale.txt").write_text("stale", encoding="utf-8")
    with pytest.raises(convert.StrategicAptConvertError):
        convert.convert_strategic_apt_bundle(_sources(), output)


@private_sources
def test_incomplete_closure_fails_closed(tmp_path: Path) -> None:
    sources = {
        path: source
        for path, source in _sources().items()
        if not path.casefold().startswith("strategicpalantir.")
    }
    with pytest.raises(convert.StrategicAptConvertError):
        convert.convert_strategic_apt_bundle(sources, tmp_path / "out")
