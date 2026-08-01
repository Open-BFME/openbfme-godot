"""Gate the retail main-menu shell APT ingress lane."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from openbfme_importer import profile as profile_module
from openbfme_importer import retail_shell_apt_convert as convert
from openbfme_importer import retail_shell_apt_profile as shell_profile
from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.sage_apt import (
    MAX_IMPORTS,
    canonical_sha256,
    parse_apt_constants,
    parse_apt_movie,
)


PRIVATE_ROOT = Path(__file__).resolve().parents[2] / ".private" / "retail-work"
EFFECTIVE_ASSETS = PRIVATE_ROOT / "cache" / "effective-assets"
MANIFEST = EFFECTIVE_ASSETS / ".openbfme" / "manifest.json"
CATALOG = PRIVATE_ROOT / "catalog" / "bfme2.json"

private_sources = pytest.mark.skipif(
    not (EFFECTIVE_ASSETS.is_dir() and MANIFEST.is_file() and CATALOG.is_file()),
    reason="private retail effective-assets view is not present",
)


# --------------------------------------------------------------------------
# Parser bounds that the shell closure needs and the palantir closure did not.
# --------------------------------------------------------------------------


def test_import_bound_admits_the_shell_movie() -> None:
    # MainMenu.apt declares 27 imports; the old bound of 16 rejected it.
    assert MAX_IMPORTS >= 27


@private_sources
def test_shell_symbol_with_trailing_space_parses() -> None:
    constants = parse_apt_constants(
        (EFFECTIVE_ASSETS / "MainMenu.const").read_bytes(), "MainMenu.const"
    )
    movie = parse_apt_movie(
        (EFFECTIVE_ASSETS / "MainMenu.apt").read_bytes(), constants, "MainMenu.apt"
    )
    symbols = {str(row["symbol"]) for row in movie["imports"]}
    # Retail authors this symbol with a trailing space.
    assert "ButtonMed_Down " in symbols
    assert {str(row["movie"]) for row in movie["imports"]} == {
        "GameWindowGadgets",
        "MenuExport",
    }


@private_sources
def test_movie_name_stays_strict_even_though_symbols_relax() -> None:
    from openbfme_importer.sage_apt import _SAFE_MOVIE, _SAFE_SYMBOL

    assert _SAFE_SYMBOL.fullmatch("ButtonMed_Down ")
    assert not _SAFE_MOVIE.fullmatch("ButtonMed_Down ")
    for hostile in ("../MenuExport", " MenuExport", "Menu/Export", "Menu\\Export"):
        assert not _SAFE_SYMBOL.fullmatch(hostile)
        assert not _SAFE_MOVIE.fullmatch(hostile)


# --------------------------------------------------------------------------
# Closure identity
# --------------------------------------------------------------------------


def test_closure_and_layer_identities_are_sealed() -> None:
    assert [name for name, _role in convert.MOVIE_CLOSURE] == [
        "AptLevel0",
        "Background",
        "MainMenu",
        "GameWindowGadgets",
        "MenuExport",
        "MenuFrameAndBg",
    ]
    assert convert.MOVIE_ORDER == ("Background", "MainMenu")
    assert convert.SCENE_ID == "bfme2.ui.shell.mainmenu"
    assert convert.RUNTIME_OUTPUT_PATH == "data/ui/shell/scene-contract.json"
    assert convert.OUTPUT_SCHEMA == "openbfme.retail-shell-apt-runtime"


def test_converter_is_registered_for_profiles_and_pipeline() -> None:
    from openbfme_importer.pipeline import RESOURCE_BUNDLE_CONVERTERS

    assert shell_profile.SHELL_APT_CONVERTER in profile_module.ALLOWED_CONVERTERS
    assert shell_profile.SHELL_APT_CONVERTER in RESOURCE_BUNDLE_CONVERTERS


def test_out_of_closure_source_is_rejected() -> None:
    with pytest.raises(convert.ShellAptConvertError):
        convert._required_shell_contract(
            {"palantir.apt": b"x"}, {"palantir.apt": "Palantir.apt"}
        )


def test_movie_of_maps_every_closure_shape() -> None:
    assert convert._movie_of("MainMenu.apt") == "MainMenu"
    assert convert._movie_of("MainMenu_geometry/16.ru") == "MainMenu"
    assert convert._movie_of("art/Textures/apt_MenuExport_2.tga") == "MenuExport"
    assert convert._movie_of("art/Textures/apt_Palantir_1.tga") is None
    assert convert._movie_of("window/controlbar.wnd") is None


# --------------------------------------------------------------------------
# Profile fragment
# --------------------------------------------------------------------------


def test_profile_resource_shape_is_the_bundle_contract() -> None:
    resource = shell_profile.shell_profile_resource(patterns=["MainMenu.apt"])
    assert resource["id"] == "shell-apt-runtime-bundle"
    assert resource["converter"] == "sage-apt-shell-runtime"
    assert resource["output"] == "data/ui/shell/scene-contract.json"
    assert resource["required"] is True
    assert resource["limit"] == 1 and resource["expected_count"] == 1
    assert (
        resource["options"]["expectedSourceAggregateSha256"]
        == shell_profile.EXPECTED_SOURCE_AGGREGATE_SHA256
    )


@private_sources
def test_shell_source_closure_matches_the_sealed_identity() -> None:
    paths = convert.shell_source_virtual_paths(EFFECTIVE_ASSETS)
    assert len(paths) == shell_profile.EXPECTED_SOURCE_COUNT
    assert paths == sorted(paths, key=str.casefold)
    total = sum(
        (EFFECTIVE_ASSETS / Path(*path.split("/"))).stat().st_size for path in paths
    )
    assert total == shell_profile.EXPECTED_PAYLOAD_BYTES


@private_sources
def test_plan_seals_the_closure_and_validates_against_the_catalog(
    tmp_path: Path,
) -> None:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    catalog = InstallCatalog.load(CATALOG)
    plan = shell_profile.build_retail_shell_apt_plan(
        EFFECTIVE_ASSETS, manifest, catalog
    )
    assert plan["schema"] == shell_profile.SHELL_APT_PLAN_SCHEMA
    assert plan["sceneId"] == convert.SCENE_ID
    assert plan["summary"]["sourceCount"] == shell_profile.EXPECTED_SOURCE_COUNT
    assert plan["summary"]["archiveCount"] == 6
    assert (
        plan["sourceEvidence"]["sourceAggregateSha256"]
        == shell_profile.EXPECTED_SOURCE_AGGREGATE_SHA256
    )
    assert plan["policy"]["executesActionScript"] is False
    assert plan["policy"]["substitutesAllowed"] is False
    assert plan["policy"]["profileFragmentValidatedByImportProfileAndCatalog"] is True
    assert plan["packBinding"] == {
        "packFileKey": "shellScene",
        "runtimePath": "data/ui/shell/scene-contract.json",
    }
    basis = dict(plan)
    basis.pop("aggregateSha256")
    assert plan["aggregateSha256"] == canonical_sha256(basis)

    generated = shell_profile.generated_import_profile(plan)
    assert generated["pack"]["dataPolicy"]["redistributable"] is False
    assert [row["id"] for row in generated["resources"]] == [
        "shell-apt-runtime-bundle"
    ]

    plan_path = tmp_path / "plan.json"
    profile_path = tmp_path / "profile.json"
    shell_profile.write_retail_shell_apt_plan(plan_path, plan)
    shell_profile.write_generated_import_profile(profile_path, generated)
    assert json.loads(plan_path.read_text(encoding="utf-8")) == plan

    tampered = dict(plan)
    tampered["aggregateSha256"] = "0" * 64
    with pytest.raises(ValueError):
        shell_profile.write_retail_shell_apt_plan(tmp_path / "bad.json", tampered)
    with pytest.raises(ValueError):
        shell_profile.generated_import_profile(tampered)


# --------------------------------------------------------------------------
# Conversion
# --------------------------------------------------------------------------


@private_sources
def test_conversion_produces_the_bounded_retail_draw_contract(tmp_path: Path) -> None:
    paths = convert.shell_source_virtual_paths(EFFECTIVE_ASSETS)
    sources = {
        path: EFFECTIVE_ASSETS.joinpath(*path.split("/")) for path in paths
    }
    output = tmp_path / "out"
    contract = convert.convert_shell_apt_bundle(
        sources,
        output,
        expected_source_aggregate_sha256=(
            shell_profile.EXPECTED_SOURCE_AGGREGATE_SHA256
        ),
    )
    assert contract["schema"] == "openbfme.retail-shell-apt-runtime"
    assert contract["schemaVersion"] == 0
    assert contract["sceneId"] == "bfme2.ui.shell.mainmenu"
    assert contract["authoredResolution"] == [1024, 768]
    assert contract["layers"] == ["Background", "MainMenu"]

    policy = contract["renderPolicy"]
    assert policy["actionScriptExecuted"] is False
    assert policy["syntheticFallbackAllowed"] is False
    assert policy["staticSubsetRequiresExplicitOptIn"] is True
    assert policy["timelinePlaybackBound"] is False
    assert policy["nativeBackdropBound"] is False

    summary = contract["summary"]
    assert summary["staticSubsetReady"] is True
    # Parity is never claimed: the shell backdrop is a native 3D shellmap and
    # timeline playback is unbound.
    assert summary["parityReady"] is False
    assert summary["drawCount"] > 0
    assert summary["atlasCount"] == 6
    assert summary["movieCount"] == 6

    codes = {row["code"] for row in contract["unsupportedSemantics"]}
    assert "shell-backdrop-requires-native-view3d" in codes
    assert "timeline-playback-not-bound" in codes

    # The backdrop gadget is recorded, never substituted with art.
    view3d = [
        row for row in contract["nativeGadgets"] if row["symbol"] == "View3D"
    ]
    assert view3d and all(
        row["runtimeBinding"] == "native-host-required-no-apt-payload"
        for row in view3d
    )

    # Every textured draw resolves to a cooked, hash-addressed atlas.
    cooked = {
        path.relative_to(output).as_posix()
        for path in output.rglob("*")
        if path.is_file()
    }
    assert "data/ui/shell/scene-contract.json" in cooked
    assert len(cooked) == summary["atlasCount"] + 1
    for atlas in contract["atlases"]:
        assert atlas.startswith("assets/ui/shell/atlases/")
        assert atlas.endswith(".png")
        assert atlas in cooked
    for draw in contract["draws"]:
        if draw["kind"] == "textured-triangle":
            assert draw["atlas"] in contract["atlases"]
        assert len(draw["points"]) == 3

    basis = dict(contract)
    basis.pop("aggregateSha256")
    assert contract["aggregateSha256"] == canonical_sha256(basis)


@private_sources
def test_conversion_is_deterministic(tmp_path: Path) -> None:
    paths = convert.shell_source_virtual_paths(EFFECTIVE_ASSETS)
    sources = {
        path: EFFECTIVE_ASSETS.joinpath(*path.split("/")) for path in paths
    }
    first = convert.convert_shell_apt_bundle(sources, tmp_path / "a")
    second = convert.convert_shell_apt_bundle(sources, tmp_path / "b")
    assert first["aggregateSha256"] == second["aggregateSha256"]


@private_sources
def test_changed_source_aggregate_fails_closed(tmp_path: Path) -> None:
    paths = convert.shell_source_virtual_paths(EFFECTIVE_ASSETS)
    sources = {
        path: EFFECTIVE_ASSETS.joinpath(*path.split("/")) for path in paths
    }
    with pytest.raises(convert.ShellAptConvertError):
        convert.convert_shell_apt_bundle(
            sources, tmp_path / "out", expected_source_aggregate_sha256="0" * 64
        )


@private_sources
def test_non_empty_output_directory_fails_closed(tmp_path: Path) -> None:
    paths = convert.shell_source_virtual_paths(EFFECTIVE_ASSETS)
    sources = {
        path: EFFECTIVE_ASSETS.joinpath(*path.split("/")) for path in paths
    }
    output = tmp_path / "out"
    output.mkdir()
    (output / "stale.txt").write_text("stale", encoding="utf-8")
    with pytest.raises(convert.ShellAptConvertError):
        convert.convert_shell_apt_bundle(sources, output)


@private_sources
def test_incomplete_closure_fails_closed(tmp_path: Path) -> None:
    paths = [
        path
        for path in convert.shell_source_virtual_paths(EFFECTIVE_ASSETS)
        if not path.casefold().startswith("menuexport.")
    ]
    sources = {
        path: EFFECTIVE_ASSETS.joinpath(*path.split("/")) for path in paths
    }
    with pytest.raises(convert.ShellAptConvertError):
        convert.convert_shell_apt_bundle(sources, tmp_path / "out")
