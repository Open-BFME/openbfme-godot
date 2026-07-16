"""Seal the exact BFME2 InGameSideCommandBar frame-state actions.

This payload-free oracle validates the BFME2 1.06 APT/CONST/DAT triplet and
emits only identities, typed effects, and reachability evidence. The two
native scheduling questions are sealed against the BFME2 1.06 ``game.dat``.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any, Iterable, Mapping

from .retail_hud_apt_convert import (
    _Movie,
    _decode_action_program,
    _movie_from_plan,
    _timeline_labels,
)
from .sage_apt import parse_apt_constants, parse_apt_dat, parse_apt_movie


SCHEMA = "openbfme.private-hud-side-command-action-oracle"
SCHEMA_VERSION = 0

_SOURCES = {
    "InGameSideCommandBar.apt": {
        "byteLength": 14_082,
        "sha256": "84d58c67c5cab9a3bf690125cbf1a0cbf3f4bc58ccc29ffa33b992a924eca6ef",
    },
    "InGameSideCommandBar.const": {
        "byteLength": 3_364,
        "sha256": "5f21b405a8121edb689365441177b38b32386f101a1bb06418336dbac815975a",
    },
    "InGameSideCommandBar.dat": {
        "byteLength": 50,
        "sha256": "892429fd2c0e9dc1305897fb9bf7ab41f629f1d39b4139afa8ca4f29212d18f1",
    },
}

_GAME_DAT = {
    "byteLength": 10_969_600,
    "sha256": "f008b587570bad693981dc7218588c81d192a1e064b0f7f861539c51156a7640",
}

_NATIVE_RANGES = (
    {
        "id": "opcode-b2-table-entry",
        "virtualAddress": "0x00DDCF10",
        "byteLength": 8,
        "sha256": "d434c66e37e90fa01d089643f456fc33ad762bf20daf4c4659f94df85c363fe7",
    },
    {
        "id": "opcode-b2-handler",
        "virtualAddress": "0x00B09200",
        "byteLength": 138,
        "sha256": "070419c060fde22906e9efa1676119187a009348a0d0f225e3a26721e58383ab",
    },
    {
        "id": "named-call-undefined-check",
        "virtualAddress": "0x00B081AC",
        "byteLength": 15,
        "sha256": "55fc688d0cb5d7d52d8660715bd83d991f9fdc42116ab482b8f8c3226f3b0843",
    },
    {
        "id": "named-call-undefined-branch",
        "virtualAddress": "0x00B087A5",
        "byteLength": 24,
        "sha256": "d61c03683d0f72e5e54d6f3f85f1981b1cf0ad5b874694aff994a17fb8136cb7",
    },
    {
        "id": "frame-record-processing",
        "virtualAddress": "0x00B0F370",
        "byteLength": 559,
        "sha256": "2b237694dddc118138533458dc28cdcc2547a1fbdd5fe9a9aebf7d20616df668",
    },
    {
        "id": "frame-action-selection",
        "virtualAddress": "0x00B0F680",
        "byteLength": 96,
        "sha256": "9b3fcafb2a7c528adddb329a6fc2573af98583072959722d1f80205c75b9332c",
    },
    {
        "id": "frame-action-queue-add",
        "virtualAddress": "0x00AE4B80",
        "byteLength": 225,
        "sha256": "9b56f28560a3b4bb58b0c661d6bf571085c6f3ca1ab96e0a72d6902c06ed0b44",
    },
    {
        "id": "seek-frame-order",
        "virtualAddress": "0x00AE2C10",
        "byteLength": 335,
        "sha256": "94fcfd39d7a4b34021f4b058b3c46fb9c3f1d689287b6537bccadfdf0898dfa6",
    },
    {
        "id": "advance-frame-order",
        "virtualAddress": "0x00AE2DF1",
        "byteLength": 56,
        "sha256": "1ea0c9a8232fc995bd3508063b9ae3713e38d7498f0061a80a7fae65cb443f58",
    },
    {
        "id": "tick-order",
        "virtualAddress": "0x00ACD84D",
        "byteLength": 43,
        "sha256": "40be6444cb920e479940cc80a3e8828a48a93a591c8f953716da93c88f06b8bb",
    },
    {
        "id": "frame-action-queue-run",
        "virtualAddress": "0x00AE6540",
        "byteLength": 1_191,
        "sha256": "f51eba1f0ad75f8764a7c6a4951af20f1c20dd6ed4c4009057886894ac9bdf2e",
    },
)

_PE_SECTIONS = (
    (0x00401000, 0x00000600, 0x007B8CE2),
    (0x00BBA000, 0x007B9400, 0x001E9783),
    (0x00DA4000, 0x009A2C00, 0x0003A008),
)

_PROGRAMS: dict[int, dict[str, Any]] = {
    6264: {
        "instructionOffset": 10956,
        "byteLength": 996,
        "sha256": "abcf2a697a9852b4b61c07de74f7e4151bed6cd467ebd98bb0eb74e17833fa16",
        "recordSha256": "2b639c5f0f24b8962f224a9a73467d58185cdbfac6b106b28e3b7a2fab0a720f",
    },
    6272: {
        "instructionOffset": 11952,
        "byteLength": 10,
        "sha256": "ee3f7f3c582961473ffbbebe851f0086820fd9fa57c62f3573a021d2c5917557",
        "recordSha256": "1b4853890cedbfc0f76c82c5782d45acbc4f80dd21ccb4b7cdd77284e62ccb74",
        "instructionNames": (
            "push-zero",
            "push-string",
            "call-function-pop",
            "end",
        ),
    },
    6368: {
        "instructionOffset": 11992,
        "byteLength": 18,
        "sha256": "268aab1f60a086e5bf869d83da0aabdfe2f383d688bf98ce6a6a59fab274f040",
        "recordSha256": "131110e0b82ef65c23d01b5fe57e23a82d704f532e09f94096d7d9016afe6dfe",
        "instructionNames": (
            "push-zero",
            "push-string",
            "call-function-pop",
            "push-zero",
            "push-string",
            "call-function-pop",
            "end",
        ),
    },
    7296: {
        "instructionOffset": 12148,
        "byteLength": 73,
        "sha256": "56466dca85c04dd52fd50a5cb02ea625cdad4b776c7ef14a6854f6412caca675",
        "recordSha256": "ab1c7196a7356eaab49bd4a1deabf34027c7c7d9917ca56d924e10828ba51e94",
        "instructionNames": (
            "constant-pool",
            "push-global-variable",
            "get-named-member",
            "not",
            "not",
            "branch-if-true",
            "push-constant-byte",
            "var",
            "push-constant-byte",
            "zero-variable",
            "push-value-of-variable",
            "push-byte",
            "less-than2",
            "not",
            "branch-if-true",
            "push-constant-byte",
            "push-one",
            "push-this-variable",
            "push-constant-byte",
            "push-value-of-variable",
            "push-one",
            "add2",
            "to-string",
            "add2",
            "get-member",
            "call-named-method-pop",
            "push-constant-byte",
            "push-value-of-variable",
            "increment",
            "set-variable",
            "branch-always",
            "end",
        ),
    },
}

_FUNCTIONS = {
    "GetNextButton": {
        "definitionOffset": 10968,
        "bodyOffset": 10996,
        "bodyByteLength": 85,
        "bodySha256": "a3df6709576a4a211c773964f23a50e74cb2148c23fa5f3e8f1224f5b8b57a13",
    },
    "IsNextButtonFrameVisible": {
        "definitionOffset": 11081,
        "bodyOffset": 11108,
        "bodyByteLength": 101,
        "bodySha256": "daa6c68215c032f6ed0819f914138faa968815a25111b996d42e5725b4cf49b5",
    },
    "GetPriorButton": {
        "definitionOffset": 11209,
        "bodyOffset": 11236,
        "bodyByteLength": 85,
        "bodySha256": "20a3b9b362d10448c370a7d0f4d466eb64fb422df1759c42b4c81f7af2bce314",
    },
    "IsPriorButtonFrameVisible": {
        "definitionOffset": 11321,
        "bodyOffset": 11348,
        "bodyByteLength": 101,
        "bodySha256": "2d80ff5e0dcf689e0af206ce6ffcf3a029f41a419924a6d68710a9e45b717378",
    },
    "UpdateFrameState": {
        "definitionOffset": 11449,
        "bodyOffset": 11476,
        "bodyByteLength": 185,
        "bodySha256": "4e3ebf348802940609232fefcd3c8a9693d482524af5bdfc23d00ef55806af53",
    },
    "UpdateNeighborFrameStates": {
        "definitionOffset": 11661,
        "bodyOffset": 11688,
        "bodyByteLength": 137,
        "bodySha256": "2ffad24c431995b73b00765b2ad1f8dce45deeed25387e048ea53ffd4baf4d24",
    },
}


class HudSideCommandOracleError(ValueError):
    """Raised when source identity or typed semantics differ from the oracle."""


def _sha(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _pe_range(payload: bytes, virtual_address: int, byte_length: int) -> bytes:
    for section_va, file_offset, section_length in _PE_SECTIONS:
        relative = virtual_address - section_va
        if 0 <= relative and relative + byte_length <= section_length:
            return payload[file_offset + relative : file_offset + relative + byte_length]
    raise HudSideCommandOracleError(
        f"native evidence range is outside sealed PE sections: 0x{virtual_address:08X}"
    )


def _native_contract(game_dat: bytes) -> dict[str, Any]:
    if (
        len(game_dat) != _GAME_DAT["byteLength"]
        or _sha(game_dat) != _GAME_DAT["sha256"]
    ):
        raise HudSideCommandOracleError("BFME2 1.06 game.dat identity changed")

    ranges: list[dict[str, Any]] = []
    for expected in _NATIVE_RANGES:
        virtual_address = int(expected["virtualAddress"], 16)
        payload = _pe_range(game_dat, virtual_address, expected["byteLength"])
        if _sha(payload) != expected["sha256"]:
            raise HudSideCommandOracleError(
                f"native evidence range changed: {expected['id']}"
            )
        ranges.append(dict(expected))

    return {
        "source": {"virtualPath": "game.dat", **_GAME_DAT},
        "ranges": ranges,
        "callNamedMethodPop": {
            "opcode": "0xB2",
            "dispatchTableVirtualAddress": "0x00DDCF10",
            "handlerVirtualAddress": "0x00B09200",
            "callCoreVirtualAddress": "0x00B08070",
            "undefinedReceiverCheckVirtualAddress": "0x00B081AC",
            "undefinedReceiverBranchVirtualAddress": "0x00B087A5",
            "undefinedReceiverEffect": (
                "consume argument-count, receiver, method, and arguments; "
                "push undefined; pop-form handler discards undefined"
            ),
            "raisesActionScriptError": False,
        },
        "frameScheduling": {
            "frameRecordProcessorVirtualAddress": "0x00B0F370",
            "frameActionSelectorVirtualAddress": "0x00B0F680",
            "actionQueueAddVirtualAddress": "0x00AE4B80",
            "actionQueueRunVirtualAddress": "0x00AE6540",
            "seekFrameOrder": [
                "process frame records",
                "select and queue raw type-1 frame actions",
            ],
            "tickOrder": [
                "advance movie timelines",
                "run queued frame actions",
            ],
            "rawType1EffectDuringFrameRecordPass": "deferred",
            "rawType3EffectDuringFrameRecordPass": "place object immediately",
            "sameFramePlacementVisibleToAction": True,
        },
    }


def frame_label_for_neighbors(
    *, frame_exists: bool, prior_frame_visible: bool, next_frame_visible: bool
) -> str | None:
    """Return the exact label selected by retail ``UpdateFrameState``."""

    if not frame_exists:
        return None
    if next_frame_visible:
        return "_middle" if prior_frame_visible else "_top"
    return "_bottom" if prior_frame_visible else "_topbottom"


def show_target_names() -> list[str]:
    """Return the exact ascending target names authored by source offset 7296."""

    return [f"Button{index}" for index in range(1, 16)]


def _movie(payloads: Mapping[str, bytes]) -> _Movie:
    normalized = {str(key).casefold(): bytes(value) for key, value in payloads.items()}
    for name, expected in _SOURCES.items():
        payload = normalized.get(name.casefold())
        if payload is None:
            raise HudSideCommandOracleError(f"private HUD source is missing: {name}")
        if len(payload) != expected["byteLength"] or _sha(payload) != expected["sha256"]:
            raise HudSideCommandOracleError(f"private HUD source identity changed: {name}")

    apt_data = normalized["ingamesidecommandbar.apt"]
    const_data = normalized["ingamesidecommandbar.const"]
    dat_data = normalized["ingamesidecommandbar.dat"]
    constants = parse_apt_constants(const_data, "InGameSideCommandBar.const")
    apt = parse_apt_movie(apt_data, constants, "InGameSideCommandBar.apt")
    image_map = parse_apt_dat(dat_data, "InGameSideCommandBar.dat")
    raw = {
        "movie": "InGameSideCommandBar",
        "apt": apt,
        "constants": constants,
        "imageMap": image_map,
        "geometry": [],
        "atlases": [],
    }
    return _movie_from_plan(raw, source_bytes=normalized)


def _all_action_rows(movie: _Movie) -> Iterable[tuple[str, int, Mapping[str, Any]]]:
    for frame_index, rows in enumerate(movie.frames):
        for row in rows:
            if row.get("kind") == "action-script":
                yield "root", frame_index, row
    for character in movie.characters:
        frames = character.get("frames")
        if not isinstance(frames, list):
            continue
        owner = f"sprite:{int(character['characterId'])}"
        for frame_index, rows in enumerate(frames):
            for row in rows:
                if row.get("kind") == "action-script":
                    yield owner, frame_index, row


def _validated_programs(movie: _Movie) -> dict[int, dict[str, Any]]:
    action_rows = {
        int(row["sourceOffset"]): (owner, frame_index, row)
        for owner, frame_index, row in _all_action_rows(movie)
    }
    output: dict[int, dict[str, Any]] = {}
    for source_offset, expected in _PROGRAMS.items():
        found = action_rows.get(source_offset)
        if found is None:
            raise HudSideCommandOracleError(
                f"action-script source offset is missing: {source_offset}"
            )
        owner, frame_index, row = found
        program = _decode_action_program(movie, row)
        for key in ("instructionOffset", "byteLength", "sha256"):
            if program[key] != expected[key]:
                raise HudSideCommandOracleError(
                    f"action-script identity changed: {source_offset}"
                )
        record = movie.data[source_offset : source_offset + 8]
        if len(record) != 8 or _sha(record) != expected["recordSha256"]:
            raise HudSideCommandOracleError(
                f"action-script record changed: {source_offset}"
            )
        instruction_names = tuple(row["name"] for row in program["instructions"])
        if "instructionNames" in expected and instruction_names != expected["instructionNames"]:
            raise HudSideCommandOracleError(
                f"action-script opcode sequence changed: {source_offset}"
            )
        output[source_offset] = {
            "scriptId": f"ingamesidecommandbar:{source_offset}",
            "sourceOffset": source_offset,
            "instructionOffset": program["instructionOffset"],
            "byteLength": program["byteLength"],
            "sha256": program["sha256"],
            "recordSha256": expected["recordSha256"],
            "owner": owner,
            "frameIndex": frame_index,
            "instructions": program["instructions"],
        }
    return output


def _validate_target_semantics(programs: Mapping[int, Mapping[str, Any]]) -> None:
    first = programs[6272]["instructions"]
    if first[1].get("operand") != "UpdateNeighborFrameStates":
        raise HudSideCommandOracleError("6272 call target changed")

    second = programs[6368]["instructions"]
    if [second[index].get("operand") for index in (1, 4)] != [
        "UpdateFrameState",
        "UpdateNeighborFrameStates",
    ]:
        raise HudSideCommandOracleError("6368 authored call order changed")

    show = programs[7296]["instructions"]
    constants = [row.get("value") for row in show[0].get("constants", [])]
    if constants != ["_global", "InGame", "i", "_show", "this", "Button", "gotoAndPlay"]:
        raise HudSideCommandOracleError("7296 constant pool changed")
    if (
        show[11].get("operand") != 15
        or show[5].get("targetOffset") != 12220
        or show[14].get("targetOffset") != 12220
        or show[25].get("operand") != 6
        or show[30].get("targetOffset") != 12178
    ):
        raise HudSideCommandOracleError("7296 loop or dispatch shape changed")


def _function_contract(movie: _Movie, library: Mapping[str, Any]) -> list[dict[str, Any]]:
    definitions = {
        str(row.get("functionName")): row
        for row in library["instructions"]
        if row.get("name") == "define-function"
    }
    result: list[dict[str, Any]] = []
    for name, expected in _FUNCTIONS.items():
        row = definitions.get(name)
        if row is None:
            raise HudSideCommandOracleError(f"retail function is missing: {name}")
        body = row.get("body", [])
        body_offset = int(body[0]["offset"]) if body else int(row["nextOffset"])
        body_end = int(row["nextOffset"])
        payload = movie.data[body_offset:body_end]
        if (
            int(row["offset"]) != expected["definitionOffset"]
            or body_offset != expected["bodyOffset"]
            or len(payload) != expected["bodyByteLength"]
            or _sha(payload) != expected["bodySha256"]
        ):
            raise HudSideCommandOracleError(f"retail function body changed: {name}")
        result.append({"name": name, **expected})
    return result


def _reachability(movie: _Movie) -> dict[str, Any]:
    root_places = [
        row
        for row in movie.frames[0]
        if row.get("kind") == "place-object" and row.get("name") == "ButtonSet"
    ]
    if len(root_places) != 1:
        raise HudSideCommandOracleError("ButtonSet root placement changed")
    root = root_places[0]
    if (
        root.get("characterId") != 21
        or root.get("depth") != 1
        or root.get("translation") != [1048.300048828125, 361.29998779296875]
    ):
        raise HudSideCommandOracleError("ButtonSet root placement identity changed")

    button_set = movie.characters[21].get("frames")
    button_frame = movie.characters[18].get("frames")
    if not isinstance(button_set, list) or len(button_set) != 1:
        raise HudSideCommandOracleError("ButtonSet timeline changed")
    if not isinstance(button_frame, list) or len(button_frame) != 21:
        raise HudSideCommandOracleError("button-frame timeline changed")
    if _timeline_labels(button_frame) != {"_hide": 0, "_show": 10}:
        raise HudSideCommandOracleError("button-frame labels changed")

    placements = [row for row in button_set[0] if row.get("kind") == "place-object"]
    button_placements = [row for row in placements if str(row.get("name", "")).startswith("Button")]
    expected_names = [f"Button{index}" for index in range(12)]
    if (
        [row.get("name") for row in button_placements] != expected_names
        or [row.get("characterId") for row in button_placements] != [18] * 12
        or [row.get("depth") for row in button_placements] != list(range(1, 112, 10))
    ):
        raise HudSideCommandOracleError("ButtonSet child placement closure changed")

    frame_zero_actions = [
        int(row["sourceOffset"])
        for row in button_frame[0]
        if row.get("kind") == "action-script"
    ]
    frame_ten_actions = [
        int(row["sourceOffset"])
        for row in button_frame[10]
        if row.get("kind") == "action-script"
    ]
    frame_ten_places = [
        row for row in button_frame[10] if row.get("kind") == "place-object"
    ]
    if frame_zero_actions != [6264, 6272] or frame_ten_actions != [6368]:
        raise HudSideCommandOracleError("button-frame action reachability changed")
    if [row.get("name") for row in frame_ten_places] != ["Frame", "ButtonGlass"]:
        raise HudSideCommandOracleError("button-frame show placements changed")
    if int.from_bytes(movie.data[6368:6372], "little") != 1 or [
        int.from_bytes(
            movie.data[int(row["sourceOffset"]) : int(row["sourceOffset"]) + 4],
            "little",
        )
        for row in frame_ten_places
    ] != [3, 3]:
        raise HudSideCommandOracleError("button-frame raw record types changed")

    return {
        "root": {
            "frameIndex": 0,
            "placementSourceOffset": int(root["sourceOffset"]),
            "name": "ButtonSet",
            "characterId": 21,
            "depth": 1,
            "translation": list(root["translation"]),
        },
        "buttonSet": {
            "characterId": 21,
            "frameCount": 1,
            "sourceOrder": [
                {"kind": str(row["kind"]), "sourceOffset": int(row["sourceOffset"])}
                for row in button_set[0]
            ],
            "localButtonNames": expected_names,
            "localButtonDepths": [int(row["depth"]) for row in button_placements],
            "showScriptTargets": show_target_names(),
            "staticallyAbsentShowTargets": [
                name for name in show_target_names() if name not in expected_names
            ],
        },
        "buttonFrame": {
            "characterId": 18,
            "frameCount": 21,
            "labels": {"_hide": 0, "_show": 10},
            "hideFrameActionOrder": frame_zero_actions,
            "showFrameActionOrder": frame_ten_actions,
            "showFrameSourceOrder": [
                {
                    "kind": str(row["kind"]),
                    "sourceOffset": int(row["sourceOffset"]),
                    "rawRecordType": int.from_bytes(
                        movie.data[
                            int(row["sourceOffset"]) : int(row["sourceOffset"]) + 4
                        ],
                        "little",
                    ),
                }
                for row in button_frame[10]
            ],
            "showFramePlacements": [
                {
                    "name": str(row["name"]),
                    "characterId": int(row["characterId"]),
                    "depth": int(row["depth"]),
                    "sourceOffset": int(row["sourceOffset"]),
                    "rawRecordType": 3,
                }
                for row in frame_ten_places
            ],
        },
    }


def build_contract_from_payloads(
    payloads: Mapping[str, bytes], game_dat: bytes
) -> dict[str, Any]:
    """Build a deterministic oracle from in-memory private source payloads."""

    movie = _movie(payloads)
    native = _native_contract(bytes(game_dat))
    programs = _validated_programs(movie)
    _validate_target_semantics(programs)
    functions = _function_contract(movie, programs[6264])
    reachability = _reachability(movie)

    scripts = [
        {
            key: programs[offset][key]
            for key in (
                "scriptId",
                "sourceOffset",
                "instructionOffset",
                "byteLength",
                "sha256",
                "recordSha256",
                "owner",
                "frameIndex",
            )
        }
        for offset in (6272, 6368, 7296)
    ]
    scripts[0]["typedEffect"] = ["UpdateNeighborFrameStates()"]
    scripts[1]["typedEffect"] = [
        "UpdateFrameState()",
        "UpdateNeighborFrameStates()",
    ]
    scripts[2]["typedEffect"] = {
        "guard": "Boolean(_global.InGame)",
        "order": show_target_names(),
        "call": "target.gotoAndPlay('_show')",
    }

    truth_table = [
        {
            "priorFrameVisible": prior,
            "nextFrameVisible": next_,
            "label": frame_label_for_neighbors(
                frame_exists=True,
                prior_frame_visible=prior,
                next_frame_visible=next_,
            ),
        }
        for prior, next_ in ((False, False), (False, True), (True, False), (True, True))
    ]
    contract: dict[str, Any] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "sources": [
            {"virtualPath": name, **identity}
            for name, identity in _SOURCES.items()
        ],
        "scripts": scripts,
        "functionLibrary": {
            "scriptId": "ingamesidecommandbar:6264",
            "instructionOffset": programs[6264]["instructionOffset"],
            "byteLength": programs[6264]["byteLength"],
            "sha256": programs[6264]["sha256"],
            "functions": functions,
            "namePrefix": "Button",
            "namePrefixLength": 6,
            "neighborLookup": {
                "prior": "_parent['Button' + (Number(this._name.substr(6)) - 1)]",
                "next": "_parent['Button' + (Number(this._name.substr(6)) + 1)]",
            },
            "frameVisiblePredicate": "neighbor != undefined && neighbor.Frame != undefined",
            "updateFrameState": {
                "frameMissingEffect": "no-op",
                "truthTable": truth_table,
                "dispatch": "Frame.gotoAndPlay(label)",
            },
            "updateNeighborFrameStates": {
                "order": ["next", "prior"],
                "effect": "if neighbor != undefined: neighbor.UpdateFrameState()",
            },
        },
        "timelineReachability": reachability,
        "nativeRuntimeEvidence": native,
        "resolvedNativeQuestions": {
            "staticallyAbsentShowTargets": {
                "targets": reachability["buttonSet"]["staticallyAbsentShowTargets"],
                "effect": "ordered call is consumed as a no-op",
                "evidenceRangeIds": [
                    "opcode-b2-table-entry",
                    "opcode-b2-handler",
                    "named-call-undefined-check",
                    "named-call-undefined-branch",
                ],
            },
            "showFrameScheduling": {
                "frameIndex": 10,
                "actionSourceOffset": 6368,
                "placementsVisibleAtAction": ["Frame", "ButtonGlass"],
                "frameLabelResult": "use sealed UpdateFrameState truth table",
                "evidenceRangeIds": [
                    "frame-record-processing",
                    "frame-action-selection",
                    "frame-action-queue-add",
                    "seek-frame-order",
                    "advance-frame-order",
                    "tick-order",
                    "frame-action-queue-run",
                ],
            },
        },
        "unresolvedRuntimeTraces": [],
        "implementationOpportunity": {
            "genericActionScriptVmRequired": False,
            "exactTypedProgramCount": 3,
            "fullyStaticProgramIds": [
                "ingamesidecommandbar:6272",
                "ingamesidecommandbar:6368",
                "ingamesidecommandbar:7296",
            ],
            "traceGatedProgramIds": [],
            "runtimePrimitive": "typed button-frame topology adapter with exact ordered label dispatch",
            "doNotGuess": [],
        },
        "summary": {
            "targetScriptCount": 3,
            "exactFunctionBodyCount": 6,
            "localButtonCount": 12,
            "authoredShowTargetCount": 15,
            "staticallyAbsentShowTargetCount": 4,
            "unresolvedRuntimeTraceCount": 0,
            "genericActionScriptVmRequired": False,
            "implementationIncluded": False,
        },
    }
    canonical = json.dumps(contract, sort_keys=True, separators=(",", ":")).encode()
    contract["aggregateSha256"] = _sha(canonical)
    return contract


def build_contract(asset_root: Path | str, game_dat: Path | str) -> dict[str, Any]:
    """Build the oracle from the contained effective-assets directory."""

    root = Path(asset_root).expanduser().resolve()
    if ".private" not in {part.casefold() for part in root.parts} or not root.is_dir():
        raise HudSideCommandOracleError(
            "effective-assets must be an existing private directory"
        )
    payloads = {name: (root / name).read_bytes() for name in _SOURCES}
    game_dat_path = Path(game_dat).expanduser().resolve()
    if not game_dat_path.is_file():
        raise HudSideCommandOracleError("BFME2 1.06 game.dat is missing")
    return build_contract_from_payloads(payloads, game_dat_path.read_bytes())


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("asset_root", type=Path)
    parser.add_argument("game_dat", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    contract = build_contract(args.asset_root, args.game_dat)
    rendered = json.dumps(contract, indent=2, sort_keys=True) + "\n"
    if args.output is None:
        print(rendered, end="")
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
