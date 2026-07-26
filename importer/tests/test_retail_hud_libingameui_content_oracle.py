from __future__ import annotations

import json
from pathlib import Path

import pytest

from openbfme_importer.retail_hud_libingameui_content_oracle import (
    HudLibInGameUiContentOracleError,
    build_contract,
    build_contract_from_payloads,
    write_contract,
)
from tests.retail_inputs import retail_file


ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / ".private" / "retail-work" / "cache" / "effective-assets"
GAME_DAT = retail_file("game.dat")
OUTPUT = ROOT / ".private" / "scratch" / "hud-libingameui-content-oracle"
MOVIES = (
    "InGameHelpBox",
    "InGameHeroSelect",
    "InGamePlanningMode",
    "InGameSideCommandBar",
    "InGameSpellBook",
    "libInGameImagesMain",
    "libInGameUI",
    "Palantir",
    "PalantirExport",
    "StrategicHUD",
)
SOURCE_NAMES = tuple(
    f"{movie}.{extension}"
    for movie in MOVIES
    for extension in ("apt", "const", "dat")
)

pytestmark = pytest.mark.skipif(
    not all((ASSETS / name).is_file() for name in SOURCE_NAMES)
    or not GAME_DAT.is_file(),
    reason="private retail HUD sources or BFME2 game.dat are absent",
)


def _payloads() -> dict[str, bytes]:
    return {name.casefold(): (ASSETS / name).read_bytes() for name in SOURCE_NAMES}


def test_contract_is_deterministic_and_closes_the_current_scope() -> None:
    first = build_contract(ASSETS, GAME_DAT)
    second = build_contract_from_payloads(_payloads(), GAME_DAT.read_bytes())
    assert first == second
    assert first["scope"]["currentMovies"] == list(MOVIES[:-1])
    assert first["scope"]["excludedComparisonMovies"] == ["StrategicHUD"]
    assert first["summary"] == {
        "currentMovieCount": 9,
        "excludedComparisonMovieCount": 1,
        "authoredProducerCount": 1,
        "nativeProducerCount": 3,
        "reachableNativeProducerCount": 1,
        "currentConsumerContextCount": 2,
        "allowedExportCount": 1,
        "rejectedContentTypeCount": 2,
        "runtimeTraceRequired": False,
        "runtimeSupportIncluded": False,
        "remainingIntegrationGateCount": 1,
    }


def test_exact_export_allowlist_is_one_retail_command_button() -> None:
    contract = build_contract(ASSETS, GAME_DAT)
    assert contract["allowedExports"] == [
        {
            "movie": "libInGameUI",
            "contentType": "CommandButton",
            "sourceIndex": 647,
            "characterId": 49,
            "kind": "sprite",
            "exportTableOffset": 736,
            "exportRecordOffset": 5912,
            "exportRecordSha256": "1781bccfd68f6ab4a96a49b97b5ae596568ec44c9eeced147e8212b87619700e",
            "characterSourceOffset": 18020,
            "characterHeaderSha256": "f2bf1b6b03e4fb18fd13bdffdc6184aaec0b285772f73bca1eb359b44c0dbfbb",
        }
    ]
    assert contract["rejectedContentTypes"] == [
        {
            "contentType": "StrategicCommandButton",
            "producerCallSite": "0x009E1402",
            "export": "StrategicHUD::StrategicCommandButton#12",
            "classification": "outside-nine-movie-men-fords-closure",
        },
        {
            "contentType": "icon",
            "producerCallSite": "0x009FCBBB",
            "export": None,
            "classification": "no-exact-export-in-current-or-strategic-comparison-closure",
        },
    ]


def test_authored_and_native_producers_are_classified_exactly() -> None:
    contract = build_contract(ASSETS, GAME_DAT)
    authored = contract["authoredProducer"]
    assert authored["scriptId"] == "palantir:95872"
    assert authored["call"] == (
        "_root.CommandButtons['0'].CreateContent('CommandButton','Bttn')"
    )
    assert authored["guard"] == "Boolean(_global.InGame)"
    assert authored["guardBranchTarget"] == 365314
    assert authored["reachableInMenFords"] is False
    native = contract["nativeRuntimeEvidence"]
    assert native["genericCreateDispatch"] == "0x009C329B"
    assert native["currentContentType"] == "CommandButton"
    assert native["currentContentName"] == "Button"
    assert [(row["contentType"], row["callSite"], row["reachableInMenFords"]) for row in native["producers"]] == [
        ("CommandButton", "0x009C7DDC", True),
        ("StrategicCommandButton", "0x009E1402", False),
        ("icon", "0x009FCBBB", False),
    ]
    assert native["directCallGraph"]["0x009C329B"] == [
        "0x009C7DDC",
        "0x009E1402",
        "0x009FCBBB",
    ]


def test_side_and_palantir_are_the_only_current_consumers() -> None:
    consumers = build_contract(ASSETS, GAME_DAT)["contentConsumers"]
    assert [row["movie"] for row in consumers] == [
        "InGameSideCommandBar",
        "Palantir",
    ]
    assert consumers[0]["instanceCount"] == 12
    assert consumers[0]["contentHostName"] == "Button"
    assert consumers[0]["instanceNames"] == [f"Button{index}" for index in range(12)]
    assert consumers[1]["instanceCount"] == 6
    assert consumers[1]["contentHostNames"] == ["1", "2", "3", "4", "5", "0"]


def test_undefined_is_noop_and_only_runtime_export_binding_remains() -> None:
    contract = build_contract(ASSETS, GAME_DAT)
    assert contract["undefinedExportBehavior"] == {
        "requestedCall": "attachMovie(contentType, contentName, 0)",
        "whenExportUndefined": [
            "this[contentName] remains undefined",
            "skip placeholder _x,_y,_width,_height copies",
            "skip extern[String(this)+'_ContentName'] registration",
            "render no concrete child",
        ],
        "policy": "preserve-retail-undefined-no-op; never-substitute-generic-art",
    }
    decision = contract["implementationDecision"]
    assert decision["staticAllowlistComplete"] is True
    assert decision["runtimeTraceRequired"] is False
    assert decision["runtimeSupportIncluded"] is False
    assert decision["allowedContentTypes"] == ["CommandButton"]
    assert decision["remainingGate"] == (
        "bind the converted libInGameUI export registry to instantiate character 49 "
        "and its converted timeline/visual closure at runtime"
    )


def test_output_is_byte_deterministic_payload_free_and_private() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    first = OUTPUT / "pytest-a.json"
    second = OUTPUT / "pytest-b.json"
    write_contract(build_contract(ASSETS, GAME_DAT), first)
    write_contract(
        build_contract_from_payloads(_payloads(), GAME_DAT.read_bytes()), second
    )
    assert first.read_bytes() == second.read_bytes()
    decoded = json.loads(first.read_text(encoding="utf-8"))
    assert decoded["schema"] == "openbfme.private-hud-libingameui-content-oracle"
    assert all(not Path(row["virtualPath"]).is_absolute() for row in decoded["sources"])
    assert "instructions" not in decoded["authoredProducer"]


def test_changed_retail_source_and_game_dat_fail_closed(tmp_path: Path) -> None:
    payloads = _payloads()
    changed_apt = bytearray(payloads["libingameui.apt"])
    changed_apt[5912] ^= 1
    payloads["libingameui.apt"] = bytes(changed_apt)
    with pytest.raises(
        HudLibInGameUiContentOracleError, match="source identity changed"
    ):
        build_contract_from_payloads(payloads, GAME_DAT.read_bytes())

    changed_game = bytearray(GAME_DAT.read_bytes())
    changed_game[0x5C2943] ^= 1
    with pytest.raises(
        HudLibInGameUiContentOracleError, match="pinned BFME2 1.06 executable"
    ):
        build_contract_from_payloads(_payloads(), bytes(changed_game))

    with pytest.raises(HudLibInGameUiContentOracleError, match="under .private"):
        write_contract(build_contract(ASSETS, GAME_DAT), tmp_path / "contract.json")
