from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path
from types import SimpleNamespace

import pytest

from openbfme_importer.retail_hud_apt_convert import (
    PRODUCTION_ATLAS_COUNT,
    PRODUCTION_BLOCKER_COUNT,
    PRODUCTION_DRAW_COUNT,
    PRODUCTION_EXTERNAL_FONT_BINDINGS,
    PRODUCTION_MEN_FORDS_RETAIL_INI_SHA256,
    PRODUCTION_OUTPUT_COUNT,
    PRODUCTION_SOURCE_COUNT,
    OUTPUT_SCHEMA,
    HudAptConvertError,
    _Reader,
    _decode_action_sequence,
    _evaluate_timeline_action_subset,
    _parse_geometry_text,
    _parse_button_character,
    _parse_clip_actions,
    _parse_font_character,
    _parse_place_object,
    _parse_text_character,
    _reconstruct_timeline,
    convert_hud_apt,
    convert_hud_apt_bundle,
)


def _button_fixture() -> bytearray:
    data = bytearray(256)
    struct.pack_into("<II", data, 0, 4, 0x09876543)
    struct.pack_into(
        "<I4f8I", data, 8, 0, -50.0, -50.0, 50.0, 50.0, 2, 4, 64, 96, 1, 108, 0, 176
    )
    for index, vertex in enumerate(
        ((-50.0, 50.0), (-50.0, -50.0), (50.0, -50.0), (50.0, 50.0))
    ):
        struct.pack_into("<2f", data, 64 + index * 8, *vertex)
    struct.pack_into("<6H", data, 96, 0, 1, 2, 2, 3, 0)
    data[108] = 8
    struct.pack_into(
        "<Ii4f2f4f4f",
        data,
        112,
        128,
        1,
        1.0,
        0.0,
        0.0,
        1.0,
        0.0,
        0.0,
        1.0,
        1.0,
        1.0,
        1.0,
        0.0,
        0.0,
        0.0,
        0.0,
    )
    return data


def test_place_object_decodes_exact_fixed_record() -> None:
    data = bytearray(96)
    struct.pack_into("<IIII", data, 0, 3, 0x2E, 17, 42)
    struct.pack_into("<ffffff", data, 16, 1.0, 0.0, 0.0, 1.0, 12.5, -4.0)
    data[40:44] = bytes((255, 128, 64, 255))
    data[44:48] = bytes((0, 1, 2, 3))
    struct.pack_into("<fIi", data, 48, 0.25, 72, 0)
    data[72:77] = b"Frame\0"

    row = _parse_place_object(_Reader(bytes(data), "fixture.apt"), 0)

    assert row["depth"] == 17
    assert row["characterId"] == 42
    assert row["translation"] == [12.5, -4.0]
    assert row["name"] == "Frame"
    assert row["tint"] == [1.0, 128 / 255, 64 / 255, 1.0]


def test_place_object_rejects_reserved_flag_bytes() -> None:
    data = bytearray(64)
    struct.pack_into("<II", data, 0, 3, 0x100)
    with pytest.raises(HudAptConvertError, match="reserved flag"):
        _parse_place_object(_Reader(bytes(data), "fixture.apt"), 0)


def test_place_object_preserves_only_the_exact_retail_flagged_null_pointer() -> None:
    data = bytearray(166_820)
    struct.pack_into("<II", data, 166_756, 3, 0xB6)
    row = _parse_place_object(_Reader(bytes(data), "InGameHeroSelect.apt"), 166_756)
    assert row["clipActionsOffset"] == 0
    assert row["clipActionsPointerState"] == "source-flagged-null"
    assert row["clipActionsRecordSha256"] == (
        "9f4483465264d873ef0052102e909eee3e6a4afd485d1b4f663894868012a898"
    )

    unexpected = bytearray(64)
    struct.pack_into("<II", unexpected, 0, 3, 0x80)
    with pytest.raises(HudAptConvertError, match="pointer is null"):
        _parse_place_object(_Reader(bytes(unexpected), "fixture.apt"), 0)


def test_font_and_text_characters_preserve_exact_typed_fields() -> None:
    font_data = bytearray(96)
    struct.pack_into("<5I", font_data, 0, 3, 0x09876543, 32, 1, 48)
    font_data[32:43] = b"Albertus MT\0"
    struct.pack_into("<I", font_data, 48, 77)
    font = _parse_font_character(_Reader(bytes(font_data), "fixture.apt"), 0)
    assert font["name"] == "Albertus MT"
    assert font["glyphCharacterIds"] == [77]

    text_data = bytearray(160)
    struct.pack_into(
        "<II4fII", text_data, 0, 2, 0x09876543, -2.0, -2.0, 50.2, 21.15, 63, 2
    )
    text_data[32:36] = bytes((0, 204, 255, 255))
    struct.pack_into("<f5I", text_data, 36, 14.0, 1, 0, 0, 96, 112)
    text_data[96:103] = b"999999\0"
    text_data[112:123] = b"stringName\0"
    text = _parse_text_character(_Reader(bytes(text_data), "fixture.apt"), 0)
    assert text["bounds"] == pytest.approx([-2.0, -2.0, 50.2, 21.15])
    assert text["fontCharacterId"] == 63
    assert text["alignmentCode"] == 2
    assert text["color"] == [0.0, 0.8, 1.0, 1.0]
    assert text["fontHeight"] == 14.0
    assert text["readOnly"] is True
    assert text["multiline"] is False
    assert text["wordWrap"] is False
    assert text["placeholder"] == "999999"
    assert text["variableName"] == "stringName"


def test_font_rejects_missing_nonempty_glyph_table() -> None:
    data = bytearray(32)
    struct.pack_into("<5I", data, 0, 3, 0x09876543, 24, 1, 0)
    data[24:29] = b"Font\0"
    with pytest.raises(HudAptConvertError, match="glyph table is null"):
        _parse_font_character(_Reader(bytes(data), "fixture.apt"), 0)


def test_text_rejects_unknown_alignment_and_malformed_boolean() -> None:
    data = bytearray(96)
    struct.pack_into("<II4fII", data, 0, 2, 0x09876543, 0.0, 0.0, 10.0, 10.0, 3, 9)
    struct.pack_into("<f5I", data, 36, 12.0, 2, 0, 0, 80, 88)
    data[80:81] = b"\0"
    data[88:89] = b"\0"
    with pytest.raises(HudAptConvertError, match="alignment"):
        _parse_text_character(_Reader(bytes(data), "fixture.apt"), 0)
    struct.pack_into("<I", data, 28, 0)
    with pytest.raises(HudAptConvertError, match="not a boolean"):
        _parse_text_character(_Reader(bytes(data), "fixture.apt"), 0)


def test_button_character_preserves_exact_hit_state_and_no_actions() -> None:
    button = _parse_button_character(
        _Reader(bytes(_button_fixture()), "fixture.apt"), 0
    )
    assert button["isMenu"] is False
    assert button["bounds"] == [-50.0, -50.0, 50.0, 50.0]
    assert button["triangles"] == [[0, 1, 2], [2, 3, 0]]
    assert button["records"][0]["states"] == ["hit"]
    assert button["records"][0]["characterId"] == 128
    assert button["actions"] == []


def test_button_rejects_missing_vertex_and_invalid_state_flags() -> None:
    missing_vertex = _button_fixture()
    struct.pack_into("<H", missing_vertex, 96, 9)
    with pytest.raises(HudAptConvertError, match="missing vertex"):
        _parse_button_character(_Reader(bytes(missing_vertex), "fixture.apt"), 0)
    invalid_state = _button_fixture()
    invalid_state[108] = 0x10
    with pytest.raises(HudAptConvertError, match="record flags"):
        _parse_button_character(_Reader(bytes(invalid_state), "fixture.apt"), 0)


def test_clip_action_parser_preserves_exact_event_order_and_masks() -> None:
    data = bytearray(80)
    struct.pack_into("<iI", data, 0, 2, 16)
    data[16:19] = (0x040000).to_bytes(3, "little")
    struct.pack_into("<II", data, 20, 0, 48)
    data[28:31] = (0x000001).to_bytes(3, "little")
    struct.pack_into("<II", data, 32, 0, 64)

    result = _parse_clip_actions(_Reader(bytes(data), "fixture.apt"), 0)

    assert result["eventCount"] == 2
    assert result["headerEndOffset"] == 8
    assert [event["eventIndex"] for event in result["events"]] == [0, 1]
    assert [event["eventNames"] for event in result["events"]] == [
        ["unload"],
        ["initialize"],
    ]
    assert [event["instructionsOffset"] for event in result["events"]] == [48, 64]
    assert [event["eventEndOffset"] for event in result["events"]] == [28, 40]
    assert all(event["nextEventOffset"] == 0 for event in result["events"])


def test_clip_action_parser_rejects_unknown_event_mask() -> None:
    data = bytearray(32)
    struct.pack_into("<iI", data, 0, 1, 8)
    data[8:11] = (0x000008).to_bytes(3, "little")
    with pytest.raises(HudAptConvertError, match="event mask"):
        _parse_clip_actions(_Reader(bytes(data), "fixture.apt"), 0)


def test_clip_action_parser_rejects_key_code_without_key_press() -> None:
    data = bytearray(32)
    struct.pack_into("<iI", data, 0, 1, 8)
    data[8:11] = (0x000001).to_bytes(3, "little")
    data[11] = 13
    with pytest.raises(HudAptConvertError, match="lacks key-press"):
        _parse_clip_actions(_Reader(bytes(data), "fixture.apt"), 0)


def test_geometry_parser_preserves_real_textured_uv_transform() -> None:
    groups = _parse_geometry_text(
        "\n".join(
            (
                "c",
                "s tc:255:128:64:255:310:0.5:0:0:0.25:824:216",
                "t 0:0:10:0:0:20",
            )
        ),
        "fixture.ru",
    )

    assert groups == [
        {
            "style": "tc",
            "values": [
                255.0,
                128.0,
                64.0,
                255.0,
                310.0,
                0.5,
                0.0,
                0.0,
                0.25,
                824.0,
                216.0,
            ],
            "primitives": [[[0.0, 0.0], [10.0, 0.0], [0.0, 20.0]]],
        }
    ]


def test_timeline_reconstruction_preserves_cumulative_display_state() -> None:
    placement = {
        "kind": "place-object",
        "sourceOffset": 100,
        "flags": 0x0E,
        "depth": 7,
        "characterId": 42,
        "matrix": [1.0, 0.0, 0.0, 1.0],
        "translation": [12.0, 24.0],
        "tint": [1.0, 0.5, 0.25, 1.0],
        "additive": [0.0, 0.0, 0.0, 0.0],
        "ratio": 0.0,
        "clipDepth": 0,
    }
    move = {
        **placement,
        "sourceOffset": 200,
        "flags": 0x05,
        "characterId": 999,
        "translation": [48.0, 96.0],
    }
    remove = {"kind": "remove-object", "sourceOffset": 300, "depth": 7}

    timeline, failures = _reconstruct_timeline(
        "Fixture", 9, [[placement], [move], [remove]]
    )

    assert failures == []
    assert timeline["displayListComplete"] is True
    assert timeline["frameCount"] == 3
    first = timeline["frames"][0]["displayList"][0]
    second = timeline["frames"][1]["displayList"][0]
    assert first["characterId"] == second["characterId"] == 42
    assert second["translation"] == [48.0, 96.0]
    assert second["tint"] == [1.0, 0.5, 0.25, 1.0]
    assert second["sourceOffsets"] == [100, 200]
    assert timeline["frames"][2]["displayList"] == []


def test_timeline_reconstruction_fails_closed_on_orphan_move() -> None:
    timeline, failures = _reconstruct_timeline(
        "Fixture",
        10,
        [
            [
                {
                    "kind": "place-object",
                    "sourceOffset": 400,
                    "flags": 0x01,
                    "depth": 3,
                    "characterId": 0,
                    "matrix": [1.0, 0.0, 0.0, 1.0],
                    "translation": [0.0, 0.0],
                    "tint": [1.0, 1.0, 1.0, 1.0],
                    "additive": [0.0, 0.0, 0.0, 0.0],
                    "ratio": 0.0,
                    "clipDepth": 0,
                }
            ]
        ],
    )

    assert timeline["displayListComplete"] is False
    assert failures == [
        {
            "frameIndex": 0,
            "sourceOffset": 400,
            "reason": "move-without-existing-depth",
        }
    ]


def test_typed_action_subset_executes_exact_timeline_effects() -> None:
    instructions = [
        {"offset": 4, "opcode": 0xA1, "name": "push-string", "operand": "FadeIn"},
        {"offset": 12, "opcode": 0x9F, "name": "goto-frame2", "operand": 1},
        {"offset": 20, "opcode": 0x00, "name": "end"},
    ]

    effects, maximum, terminal, unsupported = _evaluate_timeline_action_subset(
        instructions
    )

    assert effects == [
        {"kind": "goto", "targetType": "string", "target": "FadeIn"},
        {"kind": "play"},
    ]
    assert (maximum, terminal, unsupported) == (1, 0, [])


def test_typed_action_subset_rejects_stack_underflow() -> None:
    instructions = [
        {"offset": 4, "opcode": 0x9F, "name": "goto-frame2", "operand": 0},
        {"offset": 12, "opcode": 0x00, "name": "end"},
    ]
    with pytest.raises(HudAptConvertError, match="stack underflow"):
        _evaluate_timeline_action_subset(instructions)


def test_action_decoder_rejects_non_instruction_branch_target() -> None:
    data = bytearray(12)
    data[0] = 0x99
    struct.pack_into("<i", data, 4, -1)
    data[8] = 0x00
    movie = SimpleNamespace(
        reader=_Reader(bytes(data), "fixture.apt"), constants={"entries": []}
    )
    with pytest.raises(HudAptConvertError, match="targets non-instruction"):
        _decode_action_sequence(movie, 0)


def test_action_decoder_rejects_unknown_opcode() -> None:
    movie = SimpleNamespace(
        reader=_Reader(b"\xfe\x00", "fixture.apt"), constants={"entries": []}
    )
    with pytest.raises(HudAptConvertError, match="opcode 0xfe"):
        _decode_action_sequence(movie, 0)


def test_private_retail_contract_is_deterministic_and_fail_closed(
    tmp_path: Path,
) -> None:
    repo = Path(__file__).resolve().parents[2]
    plan = repo / ".private/scratch/hud-261-source-conversion/plan.json"
    assets = repo / ".private/retail-work/cache/effective-assets"
    if not plan.is_file() or not assets.is_dir():
        pytest.skip("private BFME2 retail HUD closure is unavailable")
    first = tmp_path / "first.json"
    second = tmp_path / "second.json"

    contract_a = convert_hud_apt(plan, assets, first)
    contract_b = convert_hud_apt(plan, assets, second)

    assert first.read_bytes() == second.read_bytes()
    assert contract_a == contract_b
    assert contract_a["schema"] == OUTPUT_SCHEMA
    assert contract_a["sceneId"] == "bfme2.ui.palantir"
    assert contract_a["summary"]["staticSubsetReady"] is True
    assert contract_a["summary"]["parityReady"] is False
    assert contract_a["summary"]["drawCount"] == 28
    assert contract_a["summary"]["texturedTriangleCount"] == 2
    # Plan conversion has no exact WND companion and therefore none of the
    # production bundle path's three precise WND blockers.
    assert contract_a["summary"]["blockerCount"] == PRODUCTION_BLOCKER_COUNT - 3
    assert contract_a["wndCompanion"] is None
    assert contract_a["renderPolicy"]["exactWndCompanionBound"] is False
    assert contract_a["summary"]["timelineCount"] == 22
    assert contract_a["summary"]["timelineFrameCount"] == 640
    assert contract_a["summary"]["timelineInstanceCount"] == 51
    assert contract_a["renderPolicy"]["exactTimelineDisplayLists"] is True
    assert contract_a["renderPolicy"]["timelinePlaybackBound"] is False
    assert all(timeline["displayListComplete"] for timeline in contract_a["timelines"])
    selection = contract_a["frameSelection"]
    assert selection["policy"] == (
        "bounded-retail-initial-setup-plus-men-fords-side-fade"
    )
    assert selection["palantir"]["selectedVariant"] == "good-double"
    assert selection["palantir"]["selectedFrameIndex"] == 19
    assert selection["palantir"]["importSymbol"] == "PalantirFrame_GoodDouble"
    assert selection["inGameSideCommandBar"]["initialState"] == "hidden-offscreen"
    assert selection["inGameSideCommandBar"]["fadeInApplied"] is False
    assert selection["inGameSideCommandBar"]["selectionDrivenFadeInBound"] is True
    assert selection["inGameSideCommandBar"]["fadeRuntimeContract"] == (
        "sideCommandFadeRuntime"
    )
    codes = {row["code"] for row in contract_a["unsupportedSemantics"]}
    assert "action-script-not-executed" not in codes
    assert "action-script-unsupported-opcodes" in codes
    assert "additional-timeline-frames-not-converted" not in codes
    assert "timeline-playback-not-bound" in codes
    assert "palantir-nondefault-frame-selection-not-bound" in codes
    assert "side-command-bar-fade-runtime-not-bound" not in codes
    assert contract_a["renderPolicy"][
        "exactMenFordsSideCommandFadeRuntimeBound"
    ] is True
    fade = contract_a["sideCommandFadeRuntime"]
    assert fade["schema"] == "openbfme.retail-hud-men-fords-side-fade"
    assert fade["typedInput"]["selectionKindsMutuallyExclusive"] is True
    assert fade["typedInput"]["selectedIdsSortedUnique"] is True
    assert len(fade["eligibility"]["roster"]) == 9
    assert all(
        row["eligibleCommandCount"] > 0 for row in fade["eligibility"]["roster"]
    )
    assert fade["eligibility"]["multiBattalionCommands"] == [
        "Command_ToggleStance",
        "Command_AttackMove",
        "Command_Stop",
    ]
    assert fade["timeline"]["millisecondsPerFrame"] == 33
    assert fade["timeline"]["targetExamples"] == {
        "31": 12,
        "32": 22,
        "37": 17,
        "41": 13,
        "42": 12,
    }
    assert fade["timeline"]["completionStateTransition"] == [2, 3]
    assert fade["timeline"]["settledStopFrameOneBased"] == 31
    assert fade["nativeStateMachine"]["fadeOutBound"] is False
    assert fade["remainingTraceGates"] == [
        {
            "id": "side-command-native-row-alias-trace",
            "blocksTypedGodotImplementation": False,
            "blocksExactNativeAliasParityClaim": True,
        }
    ]
    assert contract_a["summary"]["actionScriptCount"] == 74
    assert contract_a["summary"]["supportedActionScriptCount"] == 66
    assert contract_a["summary"]["unsupportedActionScriptCount"] == 8
    assert contract_a["summary"]["typedSideCommandActionScriptCount"] == 3
    assert contract_a["summary"]["typedMenFordsSideCommandFadeRuntimeCount"] == 1
    assert contract_a["summary"]["typedPalantirCommandActionScriptCount"] == 2
    side_programs = {
        program["scriptId"]: program
        for program in contract_a["actionScripts"]
        if program["scriptId"]
        in {
            "ingamesidecommandbar:6272",
            "ingamesidecommandbar:6368",
            "ingamesidecommandbar:7296",
        }
    }
    assert set(side_programs) == {
        "ingamesidecommandbar:6272",
        "ingamesidecommandbar:6368",
        "ingamesidecommandbar:7296",
    }
    assert all(
        program["supported"]
        and program["unsupportedInstructions"] == []
        and program["terminalStackDepth"] == 0
        for program in side_programs.values()
    )
    assert [
        effect["kind"]
        for effect in side_programs["ingamesidecommandbar:6368"]["effects"]
    ] == [
        "side-command-update-frame-state",
        "side-command-update-neighbor-frame-states",
    ]
    topology = contract_a["sideCommandTopology"]
    assert topology["unresolvedRuntimeTraceCount"] == 0
    assert topology["scheduling"] == {
        "sameFramePlacementsBeforeQueuedActions": True,
        "genericActionScriptVmUsed": False,
    }
    assert [row["name"] for row in topology["buttonSet"]["localButtons"]] == [
        f"Button{index}" for index in range(12)
    ]
    assert topology["buttonSet"]["staticallyAbsentShowTargets"] == [
        "Button12",
        "Button13",
        "Button14",
        "Button15",
    ]
    palantir_command_programs = {
        program["scriptId"]: program
        for program in contract_a["actionScripts"]
        if program["scriptId"] in {"palantir:169224", "palantir:169256"}
    }
    assert set(palantir_command_programs) == {"palantir:169224", "palantir:169256"}
    assert all(
        program["supported"]
        and program["unsupportedInstructions"] == []
        and program["terminalStackDepth"] == 0
        and program["maximumStackDepth"] == 6
        for program in palantir_command_programs.values()
    )
    assert palantir_command_programs["palantir:169224"]["effects"][0]["kind"] == (
        "palantir-command-register-lifecycle-functions"
    )
    assert [
        row["name"]
        for row in palantir_command_programs["palantir:169224"]["effects"][0][
            "functions"
        ]
    ] == [
        "OnMovieClipFrameLoaded",
        "OnMovieClipFrameUnloaded",
        "OnCommandButtonSubMenuLoaded",
        "OnCommandButtonSubMenuUnloaded",
        "OnCommandButtonToggleFlashLoaded",
        "OnCommandButtonToggleFlashUnloaded",
    ]
    method_effect = palantir_command_programs["palantir:169256"]["effects"][0]
    assert method_effect["kind"] == "palantir-command-register-button-methods"
    assert method_effect["buttonOrder"] == ["0", "1", "2", "3", "4", "5"]
    assert [row["name"] for row in method_effect["methods"]] == [
        "SetAutoAbilityOverlayState",
        "SetFlashEffectState",
        "SetGlassState",
    ]
    assert all(
        effect["invocationDuringRegistration"] is False
        for effect in (
            palantir_command_programs["palantir:169224"]["effects"][0],
            method_effect,
        )
    )
    command_topology = contract_a["palantirCommandTopology"]
    assert command_topology["typedScriptIds"] == [
        "palantir:169224",
        "palantir:169256",
    ]
    assert command_topology["commandButtons"]["numericButtonFrames"] == [
        "1",
        "2",
        "3",
        "4",
        "5",
        "0",
    ]
    assert command_topology["scheduling"] == {
        "rawActionRecordEffect": "deferred",
        "rawPlacementRecordEffect": "immediate",
        "sameFramePlacementsBeforeQueuedActions": True,
        "genericActionScriptVmUsed": False,
    }
    assert command_topology["remainingTraceGates"][0] == {
        "id": "skill-upgrade-root-method-effects",
        "programId": "palantir:167296",
        "scenario": "enter CommandUI _show once while InGame is true",
    }
    blocked_skill_upgrade = next(
        program
        for program in contract_a["actionScripts"]
        if program["scriptId"] == "palantir:167296"
    )
    assert blocked_skill_upgrade["supported"] is False
    assert blocked_skill_upgrade["effects"] == []
    minlod_programs = {
        program["scriptId"]: program
        for program in contract_a["actionScripts"]
        if program["scriptId"]
        in {"palantir:152912", "palantir:333872", "palantir:334840"}
    }
    assert set(minlod_programs) == {
        "palantir:152912",
        "palantir:333872",
        "palantir:334840",
    }
    assert all(
        program["supported"]
        and program["unsupportedInstructions"] == []
        and program["terminalStackDepth"] == 0
        and len(program["effects"]) == 1
        and program["effects"][0]["kind"] == "conditional-min-lod"
        and program["effects"][0]["condition"]
        == {"kind": "required-boolean-input", "name": "MinLOD", "equals": True}
        and program["effects"][0]["whenFalse"] == []
        and program["effects"][0]["sourceEvidence"]["programId"] == script_id
        for script_id, program in minlod_programs.items()
    )
    assert minlod_programs["palantir:152912"]["effects"][0]["whenTrue"] == [
        {
            "kind": "stop-timeline-if-property-equals",
            "target": "this",
            "propertyIndex": 13,
            "propertyName": "_name",
            "equals": "GlobeSwirlRender",
        }
    ]
    assert {
        script_id: [effect["target"] for effect in program["effects"][0]["whenTrue"]]
        for script_id, program in minlod_programs.items()
        if script_id != "palantir:152912"
    } == {
        "palantir:333872": ["effect1", "effect4"],
        "palantir:334840": ["effect2", "effect3"],
    }
    assert (
        sum(
            blocker["code"] == "action-script-unsupported-opcodes"
            for blocker in contract_a["unsupportedSemantics"]
        )
        == 8
    )
    resource_program = next(
        program
        for program in contract_a["actionScripts"]
        if program["scriptId"] == "palantir:332504"
    )
    assert resource_program["supported"] is True
    assert resource_program["maximumStackDepth"] == 4
    assert resource_program["sha256"] == (
        "0b966556e6fc10d1eaa5c129999f31e185b634425298b7bdaf21b6dd26aeb999"
    )
    assert [effect["kind"] for effect in resource_program["effects"]] == [
        "play-current-timeline",
        "emit-retail-audio-event-intent",
    ]
    assert resource_program["effects"][1]["arguments"] == [
        "Gui_PalantirResourceBarFlash"
    ]
    resource_flash = contract_a["resourceFlash"]
    assert resource_flash["typedInput"]["effect"] == {
        "target": "CommandPointsFlash",
        "method": "gotoAndPlay",
        "arguments": ["_go"],
    }
    assert resource_flash["visual"]["timelineSha256"] == (
        "f2254f867b5f59070284fd2f028d5f4e4d787f09af9f59220491559053b069d6"
    )
    assert resource_flash["visual"]["entryFrame"]["index"] == 8
    assert resource_flash["visual"]["returnFrame"]["index"] == 57
    assert resource_flash["audioEventIntent"]["eventId"] == (
        "Gui_PalantirResourceBarFlash"
    )
    assert resource_flash["runtimePolicy"] == {
        "nativeCounterAutoTriggerBound": False,
        "mixerOverlapPolicyBound": False,
        "genericDispatchAllowed": False,
        "fallbackAllowed": False,
    }
    assert {
        blocker["code"]
        for blocker in contract_a["unsupportedSemantics"]
        if blocker["code"].startswith("resource-flash-")
    } == {
        "resource-flash-native-trigger-capture-not-passed",
        "resource-flash-mixer-overlap-capture-not-passed",
    }
    assert contract_a["summary"]["clipActionProgramCount"] == 6
    assert contract_a["summary"]["supportedClipActionProgramCount"] == 5
    assert contract_a["summary"]["clipActionCount"] == 28
    assert contract_a["summary"]["clipActionEventCount"] == 28
    assert contract_a["summary"]["executableClipActionEventCount"] == 27
    assert contract_a["summary"]["displayItemCount"] == 31
    assert contract_a["summary"]["fontCount"] == 1
    assert contract_a["summary"]["embeddedFontGlyphCount"] == 0
    assert contract_a["summary"]["textCount"] == 3
    assert contract_a["summary"]["textInstanceCount"] == 3
    assert contract_a["summary"]["buttonCount"] == 1
    assert contract_a["summary"]["buttonInstanceCount"] == 3
    assert contract_a["summary"]["buttonActionCount"] == 0
    assert len(contract_a["fonts"]) == 1
    font = contract_a["fonts"][0]
    assert {
        key: font[key]
        for key in (
            "fontId",
            "movie",
            "characterId",
            "sourceOffset",
            "definitionByteLength",
            "definitionSha256",
            "name",
            "glyphCount",
            "glyphCharacterIds",
            "fontPayloadContained",
        )
    } == {
        "fontId": "palantir:63",
        "movie": "Palantir",
        "characterId": 63,
        "sourceOffset": 3736,
        "definitionByteLength": 20,
        "definitionSha256": "b49b2cdcdee84450c6870316757b553b5d2a1630823ab7bf934e315c870a115d",
        "name": "Albertus MT",
        "glyphCount": 0,
        "glyphCharacterIds": [],
        "fontPayloadContained": False,
    }
    assert font["externalFont"] == {
        **dict(PRODUCTION_EXTERNAL_FONT_BINDINGS[0]),
        "byteLength": 24_712,
        "family": "Albertus MT",
        "subfamily": "Regular",
        "postScriptName": "AlbertusMT",
        "outlineFormat": "CFF",
        "unitsPerEm": 1000,
        "glyphCount": 298,
        "embeddedBitmapStrikeCount": 0,
        "runtimeLoading": "sha256-verified-fontfile",
        "fallbackAllowed": False,
    }
    assert {
        text["characterId"]: (
            text["placeholder"],
            text["variableName"],
            text["alignmentCode"],
            text["fontHeight"],
        )
        for text in contract_a["texts"]
    } == {
        130: ("999999", "stringName", 0, 14.0),
        132: ("x99", "stringName", 2, 14.0),
        134: ("999/999", "stringName", 1, 14.0),
    }
    assert {
        instance["runtimeSource"]["initialValue"]
        for instance in contract_a["textInstances"]
    } == {
        "$PalantirResources",
        "$PalantirResourceMultiplier",
        "$PalantirCommandPoints",
    }
    assert all(
        instance["runtimeSource"]["localizationOrLiveValueBound"]
        and not instance["runtimeSource"]["fallbackAllowed"]
        for instance in contract_a["textInstances"]
    )
    display_rows = [
        *(row["displayOrder"] for row in contract_a["draws"]),
        *(row["displayOrder"] for row in contract_a["textInstances"]),
    ]
    assert sorted(display_rows) == list(range(31))
    assert sorted(row["displayOrder"] for row in contract_a["textInstances"]) == [
        28,
        29,
        30,
    ]
    assert contract_a["buttons"][0]["characterId"] == 129
    assert contract_a["buttons"][0]["triangles"] == [[0, 1, 2], [2, 3, 0]]
    assert [record["states"] for record in contract_a["buttons"][0]["records"]] == [
        ["hit"]
    ]
    assert contract_a["buttons"][0]["actions"] == []
    assert all(
        instance["eventBindings"] == [] and len(instance["hitVertices"]) == 4
        for instance in contract_a["buttonInstances"]
    )
    assert all(binding["targetClipId"] for binding in contract_a["clipActions"])
    assert all(not binding["targetTimelineId"] for binding in contract_a["clipActions"])
    assert {
        name: sum(
            event["eventNames"] == [name]
            for binding in contract_a["clipActions"]
            for event in binding["events"]
        )
        for name in ("initialize", "unload")
    } == {"initialize": 27, "unload": 1}
    assert all(
        event["keyCode"] == 0 and event["nextEventOffset"] == 0
        for binding in contract_a["clipActions"]
        for event in binding["events"]
    )
    clip_blockers = [
        blocker
        for blocker in contract_a["unsupportedSemantics"]
        if blocker["code"].startswith("clip-action-")
    ]
    assert len(clip_blockers) == 1
    assert {
        code: sum(blocker["code"] == code for blocker in clip_blockers)
        for code in {
            "clip-action-host-callback-not-bound",
            "clip-action-external-state-not-bound",
            "clip-action-lifecycle-dispatch-not-bound",
        }
    } == {
        "clip-action-host-callback-not-bound": 0,
        "clip-action-external-state-not-bound": 0,
        "clip-action-lifecycle-dispatch-not-bound": 1,
    }
    typed_programs = {
        program["programId"]: program
        for program in contract_a["clipActionPrograms"]
        if program["supported"]
    }
    assert set(typed_programs) == {
        "ingamesidecommandbar:clip-event:13680",
        "libingameui:clip-event:56252",
        "palantir:clip-event:375628",
        "palantir:clip-event:375640",
        "palantir:clip-event:375652",
    }
    local_method = typed_programs["ingamesidecommandbar:clip-event:13680"]["effects"][0]
    assert local_method["kind"] == "define-local-method"
    assert local_method["methodName"] == "SetFlashEffectState"
    assert local_method["parameters"] == ["state"]
    assert local_method["body"] == {
        "kind": "call-indexed-ancestor-timeline-method",
        "receiverAncestorHops": 2,
        "collection": "flashEffects",
        "index": {"ancestorHops": 1, "property": "_name"},
        "methodName": "gotoAndPlay",
        "arguments": [{"kind": "parameter", "name": "state"}],
    }
    property_write = typed_programs["libingameui:clip-event:56252"]["effects"][0]
    assert {
        key: property_write[key]
        for key in ("kind", "target", "propertyIndex", "propertyName", "value")
    } == {
        "kind": "set-clip-property",
        "target": "",
        "propertyIndex": 7,
        "propertyName": "_visible",
        "value": False,
    }
    assert all(
        effect["sourceEvidence"]["sha256"] == program["sha256"]
        and effect["sourceEvidence"]["byteLength"] == program["byteLength"]
        for program in typed_programs.values()
        for effect in program["effects"]
    )
    assert {
        program_id: program["effects"][0]["aptVariable"]
        for program_id, program in typed_programs.items()
        if program_id.startswith("palantir:clip-event")
    } == {
        "palantir:clip-event:375628": "$PalantirResources",
        "palantir:clip-event:375640": "$PalantirResourceMultiplier",
        "palantir:clip-event:375652": "$PalantirCommandPoints",
    }
    assert "clip-actions-not-executed" not in codes
    assert "text-character-not-converted" not in codes
    assert "button-character-not-converted" not in codes
    capture_blockers = [
        blocker
        for blocker in contract_a["unsupportedSemantics"]
        if blocker["code"] == "text-rendered-parity-capture-not-passed"
    ]
    assert len(capture_blockers) == 1
    assert capture_blockers[0]["gateCount"] == 7
    assert len(capture_blockers[0]["gates"]) == 7
    assert contract_a["runtimeAssetBindings"] == {
        "externalFonts": [dict(PRODUCTION_EXTERNAL_FONT_BINDINGS[0])]
    }
    assert contract_a["summary"]["externalMovieLoadCount"] == 5
    assert contract_a["summary"]["externalMovieAttachmentBlockerCount"] == 0
    assert contract_a["summary"]["externalMovieAttachmentCount"] == 4
    assert contract_a["summary"]["externalMovieLifecycleCaptureBlockerCount"] == 1
    assert contract_a["renderPolicy"]["exactExternalMovieChildSlotsBound"] is True
    assert contract_a["renderPolicy"]["externalMovieLifecycleCapturePassed"] is False
    assert contract_a["summary"]["flaggedNullClipActionPointerCount"] == 1
    assert contract_a["summary"]["externalFontBindingCount"] == 1
    assert [row["movie"] for row in contract_a["externalMovieLoads"]] == [
        "InGameSpellBook",
        "InGameSideCommandBar",
        "InGameHelpBox",
        "InGameHeroSelect",
        "InGamePlanningMode",
    ]
    assert (
        sum(
            row["runtimeAttachment"] == "exact-palantir-child-slot-bound"
            for row in contract_a["externalMovieLoads"]
        )
        == 4
    )
    assert [row["targetPath"] for row in contract_a["externalMovieAttachments"]] == [
        "Palantir.root.frame0/SpellBookUI",
        "Palantir.root.frame0/helpBox",
        "Palantir.root.frame0/HeroSelectUI",
        "Palantir.root.frame0/planningModeUI",
    ]
    assert [
        row["placeholder"]["depth"] for row in contract_a["externalMovieAttachments"]
    ] == [
        3,
        176,
        174,
        180,
    ]
    assert [
        row["sourceRoot"]["initialStopFrame"]
        for row in contract_a["externalMovieAttachments"]
    ] == [
        8,
        0,
        8,
        8,
    ]
    assert all(
        not row["genericVmRequired"]
        and not row["independentRootAllowed"]
        and not row["lifecycle"]["dispatchBound"]
        for row in contract_a["externalMovieAttachments"]
    )
    external_capture = [
        blocker
        for blocker in contract_a["unsupportedSemantics"]
        if blocker["code"] == "external-movie-lifecycle-capture-not-passed"
    ]
    assert len(external_capture) == 1
    assert external_capture[0]["gateCount"] == 4
    assert [gate["id"] for gate in external_capture[0]["gates"]] == [
        "apt-load-completion-order",
        "hero-select-initial-visibility",
        "palantir-target-removal-order",
        "help-box-alt-anchor-runtime-value",
    ]
    assert not any(
        blocker["code"] == "external-movie-target-attachment-not-bound"
        for blocker in contract_a["unsupportedSemantics"]
    )
    assert contract_a["externalMovieLifecycle"]["nativeRetainedSlots"] == {
        "HeroSelectUI": "+0xc4",
        "helpBox": "+0xc8",
        "planningModeUI": "+0xcc",
    }
    assert contract_a["externalMovieLifecycle"]["nativeResetClearOrder"] == [
        "HeroSelectUI",
        "helpBox",
        "planningModeUI",
    ]
    assert contract_a["sourceDiagnostics"]["flaggedNullClipActionPointers"] == [
        {
            "clipActionsOffset": 0,
            "flags": 182,
            "movie": "InGameHeroSelect",
            "recordSha256": (
                "7cf6432cbd91629acd5252c69aa957a08cadffd61214ae49ed0e078dec99a135"
            ),
            "sourceOffset": 166756,
            "sourceVirtualPath": "InGameHeroSelect.apt",
        }
    ]
    assert not any(
        blocker["code"]
        in {
            "text-external-font-runtime-loading-not-bound",
            "text-runtime-string-source-not-bound",
        }
        for blocker in contract_a["unsupportedSemantics"]
    )
    assert all(
        program["terminalStackDepth"] == 0
        for program in contract_a["actionScripts"]
        if program["supported"]
    )
    assert {
        tuple(instruction["name"] for instruction in program["instructions"])
        for program in contract_a["actionScripts"]
        if program["supported"]
        and program["scriptId"]
        not in {
            *minlod_programs,
            *side_programs,
            *palantir_command_programs,
            "palantir:332504",
        }
    } == {("stop", "end"), ("goto-label", "play", "end")}
    assert (
        json.loads(first.read_text(encoding="utf-8"))["aggregateSha256"]
        == contract_a["aggregateSha256"]
    )


def test_private_261_source_bundle_needs_no_planner_at_conversion_time(
    tmp_path: Path,
) -> None:
    repo = Path(__file__).resolve().parents[2]
    plan_path = repo / ".private/scratch/hud-261-source-conversion/plan.json"
    assets = repo / ".private/retail-work/cache/effective-assets"
    if not plan_path.is_file() or not assets.is_dir():
        pytest.skip("private BFME2 retail HUD closure is unavailable")
    plan = json.loads(plan_path.read_text(encoding="utf-8"))
    virtual_paths = [
        row["virtualPath"]
        for group in plan["sourceEvidence"]["groups"]
        for row in group["files"]
    ]
    virtual_paths.append("window/controlbar.wnd")
    sources = {path: assets / Path(*path.split("/")) for path in virtual_paths}
    retail_ini_sources = {
        path: assets / Path(*path.split("/"))
        for path in PRODUCTION_MEN_FORDS_RETAIL_INI_SHA256
    }

    first = convert_hud_apt_bundle(
        sources,
        tmp_path / "first",
        external_fonts=PRODUCTION_EXTERNAL_FONT_BINDINGS,
        external_font_sources={"albertusmt.otf": assets / "albertusmt.otf"},
        retail_ini_sources=retail_ini_sources,
    )
    second = convert_hud_apt_bundle(
        sources,
        tmp_path / "second",
        expected_source_aggregate_sha256=first["source"]["sourceAggregateSha256"],
        external_fonts=PRODUCTION_EXTERNAL_FONT_BINDINGS,
        external_font_sources={"albertusmt.otf": assets / "albertusmt.otf"},
        retail_ini_sources=retail_ini_sources,
    )

    assert first == second
    assert first["source"]["sourceCount"] == PRODUCTION_SOURCE_COUNT
    assert first["source"]["sourcePayloadBytes"] == 10_700_284
    assert first["summary"]["atlasCount"] == PRODUCTION_ATLAS_COUNT
    assert first["summary"]["drawCount"] == PRODUCTION_DRAW_COUNT
    assert first["summary"]["texturedTriangleCount"] == 2
    assert first["summary"]["blockerCount"] == PRODUCTION_BLOCKER_COUNT
    companion = first["wndCompanion"]
    assert companion["schema"] == "openbfme.retail-hud-wnd-companion"
    assert companion["schemaVersion"] == 0
    assert companion["source"] == {
        "virtualPath": "window/controlbar.wnd",
        "sha256": "a509730457224a111af8022df6d0ef373fcaa5d91a102bc15bccf5fc1a54ced6",
        "windowCount": 87,
        "callbackCount": 21,
        "activationAuthority": "active-companion-not-candidate-dead",
    }
    assert companion["oracleAggregates"] == {
        "callback": "ad97b6c02ed6a46eec745adda4434264b84dcc969b7c46115f6a8a6458d33662",
        "message": "238e9de43c8ebae4a22de1f7b04c4ced3933dbe3328c83ffa44d805b5336274c",
        "draw": "748ad63a218497f9ff9565b1b8078a165c90fd75dc7d39335d46a6edd4f3c484",
    }
    assert companion["runtimeInventory"]["implementedCallbackCount"] == 15
    assert companion["runtimeInventory"]["requiredMessageCallbackCount"] == 5
    assert companion["runtimeInventory"]["requiredMessageUnimplemented"] == []
    assert companion["runtimeInventory"]["outsideSlice"] == {
        "BeaconWindowInput": "event-dormant",
        "ControlBarObserverSystem": "outside declared player-v-player slice",
    }
    assert companion["runtimeInventory"]["unresolvedBuiltins"] == [
        "GameWinDefaultInput",
        "GameWinDefaultSystem",
        "GameWinDefaultTooltip",
        "W3DGameWinDefaultDraw",
    ]
    assert companion["liveBinding"] == {
        "callbackDispatchBound": False,
        "renderServicesBound": False,
        "genericDispatchAllowed": False,
        "fallbackVisualsAllowed": False,
    }
    assert first["renderPolicy"]["exactWndCompanionBound"] is True
    assert first["renderPolicy"]["wndLiveDispatchBound"] is False
    assert first["renderPolicy"]["wndRenderServicesBound"] is False
    assert first["summary"]["wndCompanionBound"] is True
    assert first["summary"]["wndTypedCallbackCount"] == 15
    assert first["summary"]["wndRequiredMessageCallbackCount"] == 5
    assert first["summary"]["wndRequiredMessageUnimplementedCount"] == 0
    assert first["summary"]["wndUnresolvedBuiltinCount"] == 4
    blockers = {row["code"]: row for row in first["unsupportedSemantics"]}
    assert "wnd-layout-callbacks-not-bound" not in blockers
    assert blockers["wnd-unresolved-runtime-builtins-not-bound"]["callbackCount"] == 4
    assert blockers["wnd-dynamic-draw-service-capture-not-passed"]["gateCount"] == 7
    assert blockers["wnd-live-dispatch-render-services-not-bound"]["messageAliasGateCount"] == 7
    assert first["summary"]["clipActionProgramCount"] == 6
    assert first["summary"]["supportedClipActionProgramCount"] == 5
    assert first["summary"]["clipActionCount"] == 28
    assert first["summary"]["clipActionEventCount"] == 28
    assert first["summary"]["executableClipActionEventCount"] == 27
    first_files = sorted(
        path.relative_to(tmp_path / "first").as_posix()
        for path in (tmp_path / "first").rglob("*")
        if path.is_file()
    )
    second_files = sorted(
        path.relative_to(tmp_path / "second").as_posix()
        for path in (tmp_path / "second").rglob("*")
        if path.is_file()
    )
    assert first_files == second_files
    assert len(first_files) == PRODUCTION_OUTPUT_COUNT
    assert "data/ui/palantir/scene-contract.json" in first_files
    font_relative = "assets/ui/palantir/fonts/albertusmt-6a1990e17f14.otf"
    assert font_relative in first_files
    assert hashlib.sha256(
        (tmp_path / "first" / font_relative).read_bytes()
    ).hexdigest() == (
        "6a1990e17f14ce5be199dde10f56dac3efd66aaa8e91d46119952cf55a9d9ba0"
    )
    assert all(
        (tmp_path / "first" / path).read_bytes()
        == (tmp_path / "second" / path).read_bytes()
        for path in first_files
    )

    changed_font = [dict(PRODUCTION_EXTERNAL_FONT_BINDINGS[0])]
    changed_font[0]["sourceSha256"] = "0" * 64
    with pytest.raises(HudAptConvertError, match="external font binding contract"):
        convert_hud_apt_bundle(
            sources,
            tmp_path / "changed-font",
            expected_source_aggregate_sha256=first["source"]["sourceAggregateSha256"],
            external_fonts=changed_font,
        )
