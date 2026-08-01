"""Seal the three small BFME2 Palantir command-interface ActionScripts.

The oracle is payload-free.  It verifies the exact retail APT closure and
BFME2 1.06 native ranges, then emits typed registrations, calls, reachability,
and the smallest remaining observation gates.  It is not an ActionScript VM.
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


SCHEMA = "openbfme.private-hud-palantir-command-action-oracle"
SCHEMA_VERSION = 0

_SOURCES: dict[str, dict[str, Any]] = {
    "Palantir.apt": {
        "byteLength": 378_173,
        "sha256": "c1f500847f0c77d4c6504edf79113b5723300165bebd42b4dafda479516f5140",
    },
    "Palantir.const": {
        "byteLength": 10_260,
        "sha256": "f07e24e3b70e286d491652cc827aef904a2ccabf54107d4f1bfc3030beee8fd9",
    },
    "Palantir.dat": {
        "byteLength": 586,
        "sha256": "d8e8964711e4061b0643dd0dd3de1876b7326cee6d60e11214793b5d483f3ae4",
    },
    "libInGameUI.apt": {
        "byteLength": 58_462,
        "sha256": "305bdfabca3a815f8c373419978ca080a7f28561b2ca9d36eeeb7f35992ba392",
    },
    "libInGameUI.const": {
        "byteLength": 2_876,
        "sha256": "717a03669f47944f9933e829e8d5d1193e375cadbbdfc5804ee131631a7176cd",
    },
    "libInGameUI.dat": {
        "byteLength": 50,
        "sha256": "892429fd2c0e9dc1305897fb9bf7ab41f629f1d39b4139afa8ca4f29212d18f1",
    },
}

_GAME_DAT = {
    "byteLength": 10_969_600,
    "sha256": "f008b587570bad693981dc7218588c81d192a1e064b0f7f861539c51156a7640",
}

_PE_SECTIONS = (
    (0x00401000, 0x00000600, 0x007B8CE2),
    (0x00BBA000, 0x007B9400, 0x001E9783),
    (0x00DA4000, 0x009A2C00, 0x0003A008),
)

_NATIVE_RANGES = (
    (
        "command-ui-index-parser",
        0x00929628,
        112,
        "d43d56fcfede1ac5fde015cc4d50bb0c5fe8cec7062fa76414d6b7e79e6c47db",
    ),
    (
        "button-frame-loaded-handler",
        0x00929698,
        264,
        "5fb4bb4e8f44ce017acfaffb250ed1d993b0e9386bf1f3b7729c514f1182797e",
    ),
    (
        "button-frame-unloaded-handler",
        0x009297A0,
        61,
        "3c725f88f2e3a52351d5c4bacb39fbc55bd93f93f1f9bfcb28c0680caa8f2c25",
    ),
    (
        "submenu-loaded-handler",
        0x009297DD,
        259,
        "9689cd100268179c74a014dca3f42dda521ce0f13e857885bdcfeb9ca1f945c8",
    ),
    (
        "submenu-unloaded-handler",
        0x009298E0,
        62,
        "a677634a6d3b46d71a3126474c4cdb125b931fc24ef80c3092d6e5a0fb8ccc1e",
    ),
    (
        "toggle-flash-loaded-handler",
        0x0092991E,
        259,
        "25d49e54a52fc72fd51366d69a5a59ff98730e0355bbbce56d2efcd7e3a3c3f6",
    ),
    (
        "toggle-flash-unloaded-handler",
        0x00929A21,
        62,
        "61716810804c26f2165b75a652dc1f69cc2f372d7f4dd5b4330cdfea16cbff00",
    ),
    (
        "command-ui-handler-registration",
        0x0092A086,
        396,
        "c7610722e103fa9e512c7f7ddecbf2cfe3230f06ba04a10f81a5b134941872d0",
    ),
    (
        "frame-record-processing",
        0x00B0F370,
        559,
        "2b237694dddc118138533458dc28cdcc2547a1fbdd5fe9a9aebf7d20616df668",
    ),
    (
        "frame-action-selection",
        0x00B0F680,
        96,
        "9b3fcafb2a7c528adddb329a6fc2573af98583072959722d1f80205c75b9332c",
    ),
    (
        "frame-action-queue-add",
        0x00AE4B80,
        225,
        "9b56f28560a3b4bb58b0c661d6bf571085c6f3ca1ab96e0a72d6902c06ed0b44",
    ),
    (
        "frame-action-queue-run",
        0x00AE6540,
        1_191,
        "f51eba1f0ad75f8764a7c6a4951af20f1c20dd6ed4c4009057886894ac9bdf2e",
    ),
    (
        "seek-frame-order",
        0x00AE2C10,
        335,
        "94fcfd39d7a4b34021f4b058b3c46fb9c3f1d689287b6537bccadfdf0898dfa6",
    ),
    (
        "advance-frame-order",
        0x00AE2DF1,
        56,
        "1ea0c9a8232fc995bd3508063b9ae3713e38d7498f0061a80a7fae65cb443f58",
    ),
    (
        "tick-order",
        0x00ACD84D,
        43,
        "40be6444cb920e479940cc80a3e8828a48a93a591c8f953716da93c88f06b8bb",
    ),
)

_PROGRAMS: dict[int, dict[str, Any]] = {
    167_296: {
        "movie": "Palantir",
        "owner": "sprite:86",
        "frameIndex": 9,
        "instructionOffset": 367_200,
        "byteLength": 68,
        "sha256": "2ddccd66a9c1b67fde9f41dfd3cd471bc0061724827ee1bfa426d8dac2447567",
        "recordSha256": "e5002ea2f5b84632eb90d5129d015a40a0bc5abf5e8a44645cc070179d983f87",
    },
    169_224: {
        "movie": "Palantir",
        "owner": "sprite:114",
        "frameIndex": 0,
        "instructionOffset": 367_624,
        "byteLength": 484,
        "sha256": "3e6f347f6c6574a2d40e85f8f564c1f9af1c13513d0f1671298a1484d629fbfc",
        "recordSha256": "e5e6ba9b166bdf8196e7e95b27590d412ab5deab0aff0d13d97658fcfe7dcc3b",
    },
    169_256: {
        "movie": "Palantir",
        "owner": "sprite:114",
        "frameIndex": 9,
        "instructionOffset": 368_120,
        "byteLength": 293,
        "sha256": "c29cecb1997de0b9de26b4c5ec01761c81d45bc21cadf45dfd1e268ac2cefe3b",
        "recordSha256": "43a4f49b55819d464db12accd6e9ca2774a5bfd34bcb0bee1567ee4aae9d3fe3",
    },
}

_LIFECYCLE_FUNCTIONS: tuple[dict[str, Any], ...] = (
    {
        "name": "OnMovieClipFrameLoaded",
        "definitionOffset": 367636,
        "bodyOffset": 367668,
        "bodyByteLength": 51,
        "bodySha256": "2e04d77ff99a163925615cc9e6b2c7d83dbf945b428d3c9baea695a95c1e12fd",
        "host": "PalantirCommandUI::OnButtonFrameLoaded",
        "argument": "index=clip._name&name=String(clip)",
    },
    {
        "name": "OnMovieClipFrameUnloaded",
        "definitionOffset": 367719,
        "bodyOffset": 367748,
        "bodyByteLength": 36,
        "bodySha256": "4ab0920334b617d403a746a23b7634ca1c5511974f70fd6020e3e32ac7934214",
        "host": "PalantirCommandUI::OnButtonFrameUnloaded",
        "argument": "index=clip._name",
    },
    {
        "name": "OnCommandButtonSubMenuLoaded",
        "definitionOffset": 367784,
        "bodyOffset": 367816,
        "bodyByteLength": 59,
        "bodySha256": "bd603f86f55a96977d0c8d6f001952441af5fca3c4f96d1c59e84ab46eb713bc",
        "host": "PalantirCommandUI::OnSubMenuLoaded",
        "argument": "index=clip._name.substr(7)&name=String(clip)",
    },
    {
        "name": "OnCommandButtonSubMenuUnloaded",
        "definitionOffset": 367875,
        "bodyOffset": 367904,
        "bodyByteLength": 43,
        "bodySha256": "c7032d22743076388774d66857f2d788d3facea1433ff415ff287b999a0087f2",
        "host": "PalantirCommandUI::OnSubMenuUnloaded",
        "argument": "index=clip._name.substr(7)",
    },
    {
        "name": "OnCommandButtonToggleFlashLoaded",
        "definitionOffset": 367947,
        "bodyOffset": 367976,
        "bodyByteLength": 59,
        "bodySha256": "86c91c219bada277fcccbe6a103b33ce9c17b870d05c0b806728e0051664a02e",
        "host": "PalantirCommandUI::OnToggleFlashLoaded",
        "argument": "index=clip._name.substr(11)&name=String(clip)",
    },
    {
        "name": "OnCommandButtonToggleFlashUnloaded",
        "definitionOffset": 368035,
        "bodyOffset": 368064,
        "bodyByteLength": 43,
        "bodySha256": "251de13f80d51b7fcb72b137bd7817901c86b66c37c3dd9d51d24cd525ac3241",
        "host": "PalantirCommandUI::OnToggleFlashUnloaded",
        "argument": "index=clip._name.substr(11)",
    },
)

_BUTTON_METHODS: tuple[dict[str, Any], ...] = (
    {
        "name": "SetAutoAbilityOverlayState",
        "definitionOffset": 368160,
        "bodyOffset": 368192,
        "bodyByteLength": 45,
        "bodySha256": "abf82bf818bb3423a889db7f20fb3b9483d5e9e7fda65710988bc50f7343a482",
        "target": "this._parent._parent.AutoAbilityOverlays[this._name]",
    },
    {
        "name": "SetFlashEffectState",
        "definitionOffset": 368242,
        "bodyOffset": 368272,
        "bodyByteLength": 45,
        "bodySha256": "348936c664694b5d48c022b09f90552b509cc052dd8a00390a411d980ef46196",
        "target": "this._parent.FlashEffects[this._name]",
    },
    {
        "name": "SetGlassState",
        "definitionOffset": 368322,
        "bodyOffset": 368352,
        "bodyByteLength": 46,
        "bodySha256": "c3546a83b7f1c52d876e993edea2d3f6e9c8054621f8ea3b4e94d821ba84ddb7",
        "target": "this._parent['glass' + this._name]",
    },
)

_LIFECYCLE_CALLERS: tuple[dict[str, Any], ...] = (
    {
        "symbol": "CommandButtonToggleFlash",
        "characterId": 3,
        "sourceOffset": 36844,
        "instructionOffset": 53500,
        "byteLength": 130,
        "sha256": "a8473ef69565d4b3582ce94d7035c8fad606f1b05afde194dca1cbf62d20b7f5",
        "recordSha256": "10545c9f2841d72706a723bef326069b9c130dbecd2e7ffc343b9b68c311e247",
        "loaded": "OnCommandButtonToggleFlashLoaded",
        "unloaded": "OnCommandButtonToggleFlashUnloaded",
    },
    {
        "symbol": "MovieClipFrame",
        "characterId": 6,
        "sourceOffset": 37332,
        "instructionOffset": 53656,
        "byteLength": 294,
        "sha256": "81298e35028262cc75249d9ee6057fc1c105369e81aeec3bb485d09a2f59cd70",
        "recordSha256": "32d5cbdb23a531eb389742291823b5884d511518f7ebbe6141e7804c091f85de",
        "loaded": "OnMovieClipFrameLoaded",
        "unloaded": "OnMovieClipFrameUnloaded",
    },
    {
        "symbol": "CommandButtonSubMenu",
        "characterId": 26,
        "sourceOffset": 50092,
        "instructionOffset": 54736,
        "byteLength": 238,
        "sha256": "7e501c21dd2bc7d5efea9ef61aad47e22303c14a80f7e2017b2b45b3da00f9cb",
        "recordSha256": "db9318f37646f82f1825647252ce66a57399800dfc07277e4130d6fdfa0bd89e",
        "loaded": "OnCommandButtonSubMenuLoaded",
        "unloaded": "OnCommandButtonSubMenuUnloaded",
    },
)

_HOST_REGISTRY = (
    ("PalantirCommandUI::OnButtonFrameLoaded", "0x00929698", "button-frame-slot"),
    ("PalantirCommandUI::OnButtonFrameUnloaded", "0x009297A0", "button-frame-slot"),
    ("PalantirCommandUI::OnSubMenuLoaded", "0x009297DD", "submenu-slot"),
    ("PalantirCommandUI::OnSubMenuUnloaded", "0x009298E0", "submenu-slot"),
    ("PalantirCommandUI::OnToggleFlashLoaded", "0x0092991E", "toggle-flash-slot"),
    ("PalantirCommandUI::OnToggleFlashUnloaded", "0x00929A21", "toggle-flash-slot"),
)


class HudPalantirCommandOracleError(ValueError):
    """Raised when the sealed retail command-interface evidence changes."""


def _sha(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _canonical(value: object) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")


def _pe_range(payload: bytes, virtual_address: int, byte_length: int) -> bytes:
    for section_va, file_offset, section_length in _PE_SECTIONS:
        relative = virtual_address - section_va
        if 0 <= relative and relative + byte_length <= section_length:
            return payload[
                file_offset + relative : file_offset + relative + byte_length
            ]
    raise HudPalantirCommandOracleError(
        f"native evidence range is outside sealed PE sections: 0x{virtual_address:08X}"
    )


def _movie(payloads: Mapping[str, bytes], name: str) -> _Movie:
    normalized = {str(key).casefold(): bytes(value) for key, value in payloads.items()}
    names = (f"{name}.apt", f"{name}.const", f"{name}.dat")
    for source_name in names:
        expected = _SOURCES[source_name]
        payload = normalized.get(source_name.casefold())
        if payload is None:
            raise HudPalantirCommandOracleError(
                f"private HUD source is missing: {source_name}"
            )
        if (
            len(payload) != expected["byteLength"]
            or _sha(payload) != expected["sha256"]
        ):
            raise HudPalantirCommandOracleError(
                f"private HUD source identity changed: {source_name}"
            )
    constants = parse_apt_constants(normalized[names[1].casefold()], names[1])
    apt = parse_apt_movie(normalized[names[0].casefold()], constants, names[0])
    image_map = parse_apt_dat(normalized[names[2].casefold()], names[2])
    return _movie_from_plan(
        {
            "movie": name,
            "apt": apt,
            "constants": constants,
            "imageMap": image_map,
            "geometry": [],
            "atlases": [],
        },
        source_bytes=normalized,
    )


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


def _programs(
    movie: _Movie, expected_rows: Mapping[int, Mapping[str, Any]]
) -> dict[int, dict[str, Any]]:
    rows = {
        int(row["sourceOffset"]): (owner, frame_index, row)
        for owner, frame_index, row in _all_action_rows(movie)
    }
    result: dict[int, dict[str, Any]] = {}
    for source_offset, expected in expected_rows.items():
        found = rows.get(source_offset)
        if found is None:
            raise HudPalantirCommandOracleError(
                f"action-script source offset is missing: {source_offset}"
            )
        owner, frame_index, row = found
        program = _decode_action_program(movie, row)
        if owner != expected.get("owner") or frame_index != expected.get("frameIndex"):
            raise HudPalantirCommandOracleError(
                f"action-script reachability changed: {source_offset}"
            )
        for key in ("instructionOffset", "byteLength", "sha256"):
            if program[key] != expected[key]:
                raise HudPalantirCommandOracleError(
                    f"action-script identity changed: {source_offset}"
                )
        if (
            _sha(movie.data[source_offset : source_offset + 8])
            != expected["recordSha256"]
        ):
            raise HudPalantirCommandOracleError(
                f"action-script record changed: {source_offset}"
            )
        result[source_offset] = {
            **program,
            "owner": owner,
            "frameIndex": frame_index,
            "recordSha256": expected["recordSha256"],
        }
    return result


def _validate_function_bodies(
    movie: _Movie,
    program: Mapping[str, Any],
    expected_rows: Iterable[Mapping[str, Any]],
) -> list[dict[str, Any]]:
    definitions = [
        row
        for row in program["instructions"]
        if row.get("name") in {"define-function", "define-function2"}
    ]
    expected = list(expected_rows)
    if len(definitions) != len(expected):
        raise HudPalantirCommandOracleError("typed function count changed")
    output: list[dict[str, Any]] = []
    for row, identity in zip(definitions, expected, strict=True):
        body = row.get("body", [])
        body_offset = int(body[0]["offset"]) if body else int(row["nextOffset"])
        payload = movie.data[body_offset : int(row["nextOffset"])]
        if (
            int(row["offset"]) != identity["definitionOffset"]
            or body_offset != identity["bodyOffset"]
            or len(payload) != identity["bodyByteLength"]
            or _sha(payload) != identity["bodySha256"]
            or [str(item["name"]) for item in row.get("parameters", [])]
            != ["clip" if "host" in identity else "state"]
        ):
            raise HudPalantirCommandOracleError(
                f"typed function body changed: {identity['name']}"
            )
        output.append(dict(identity))
    return output


def _validate_semantics(
    palantir: _Movie, programs: Mapping[int, Mapping[str, Any]]
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    skill = programs[167_296]["instructions"]
    if [row.get("value") for row in skill[0].get("constants", [])] != [
        "_root",
        "UpdateSkillUpgradeButton",
        "_global",
        "InGame",
        "_up",
        "SetCommandButtonState",
        "_disabled",
    ] or [
        skill[index].get("operand")
        for index in (2, 3, 5, 8, 9, 12, 13, 17, 18, 22, 23, 27, 28)
    ] != [
        0,
        1,
        3,
        39,
        4,
        0,
        5,
        0,
        5,
        0,
        5,
        0,
        5,
    ]:
        raise HudPalantirCommandOracleError("167296 call or branch semantics changed")

    lifecycle = programs[169_224]
    if [str(row.get("functionName", "")) for row in lifecycle["instructions"][1:7]] != [
        row["name"] for row in _LIFECYCLE_FUNCTIONS
    ]:
        raise HudPalantirCommandOracleError("169224 registration order changed")
    lifecycle_functions = _validate_function_bodies(
        palantir, lifecycle, _LIFECYCLE_FUNCTIONS
    )

    setters = programs[169_256]
    if [
        row.get("value") for row in setters["instructions"][0].get("constants", [])
    ] != [
        "i",
        "buttonFrame",
        "this",
        "SetAutoAbilityOverlayState",
        "_parent",
        "AutoAbilityOverlays",
        "_name",
        "gotoAndPlay",
        "SetFlashEffectState",
        "FlashEffects",
        "SetGlassState",
        "glass",
    ]:
        raise HudPalantirCommandOracleError("169256 constant pool changed")
    if (
        setters["instructions"][5].get("operand") != 6
        or setters["instructions"][8].get("targetOffset") != 368412
        or setters["instructions"][31].get("targetOffset") != 368136
        or [setters["instructions"][index].get("operand") for index in (16, 20, 24)]
        != [3, 8, 10]
    ):
        raise HudPalantirCommandOracleError("169256 loop or setter order changed")
    method_functions = _validate_function_bodies(palantir, setters, _BUTTON_METHODS)
    return lifecycle_functions, method_functions


def _validate_lifecycle_callers(lib: _Movie) -> list[dict[str, Any]]:
    expected = {
        int(row["sourceOffset"]): {
            **row,
            "owner": f"sprite:{int(row['characterId'])}",
            "frameIndex": 0,
        }
        for row in _LIFECYCLE_CALLERS
    }
    programs = _programs(lib, expected)
    output: list[dict[str, Any]] = []
    for identity in _LIFECYCLE_CALLERS:
        program = programs[int(identity["sourceOffset"])]
        strings = {
            str(constant.get("value"))
            for instruction in _walk(program["instructions"])
            for constant in instruction.get("constants", [])
        }
        if identity["loaded"] not in strings or identity["unloaded"] not in strings:
            raise HudPalantirCommandOracleError(
                f"lifecycle caller changed: {identity['symbol']}"
            )
        if lib.exports.get(str(identity["symbol"]).casefold()) not in (
            [int(identity["characterId"])],
            [int(identity["characterId"]), int(identity["characterId"])],
        ):
            raise HudPalantirCommandOracleError(
                f"lifecycle export changed: {identity['symbol']}"
            )
        output.append(dict(identity))
    return output


def _walk(instructions: Iterable[Mapping[str, Any]]) -> Iterable[Mapping[str, Any]]:
    for instruction in instructions:
        yield instruction
        yield from _walk(instruction.get("body", []))


def _reachability(palantir: _Movie) -> dict[str, Any]:
    if (
        palantir.imports.get(106) != ("libInGameUI", "CommandButtonSubMenu")
        or palantir.imports.get(108) != ("libInGameUI", "MovieClipFrame")
        or palantir.imports.get(110) != ("libInGameUI", "ButtonGlass")
        or palantir.imports.get(111) != ("libInGameUI", "CommandButtonToggleFlash")
    ):
        raise HudPalantirCommandOracleError("Palantir command imports changed")
    root_places = {
        str(row.get("name", "")): row
        for row in palantir.frames[0]
        if row.get("kind") == "place-object"
    }
    command_ui = root_places.get("CommandUI")
    command_buttons = root_places.get("CommandButtons")
    overlays = root_places.get("AutoAbilityOverlays")
    if (
        command_ui is None
        or command_buttons is None
        or overlays is None
        or (command_ui.get("characterId"), command_ui.get("depth")) != (86, 17)
        or (command_buttons.get("characterId"), command_buttons.get("depth"))
        != (114, 44)
        or (overlays.get("characterId"), overlays.get("depth")) != (122, 90)
    ):
        raise HudPalantirCommandOracleError("Palantir root command placements changed")
    skill_frames = palantir.characters[86].get("frames", [])
    command_frames = palantir.characters[114].get("frames", [])
    if (
        len(skill_frames) != 19
        or _timeline_labels(skill_frames) != {"_hide": 0, "_show": 9}
        or len(command_frames) != 19
        or _timeline_labels(command_frames) != {"_hide": 0, "_show": 9}
    ):
        raise HudPalantirCommandOracleError("Palantir command timeline labels changed")
    show_rows = command_frames[9]
    if [
        int(row["sourceOffset"])
        for row in show_rows
        if row.get("kind") == "action-script"
    ] != [169256]:
        raise HudPalantirCommandOracleError("CommandButtons show action changed")
    placements = [row for row in show_rows if row.get("kind") == "place-object"]
    numeric = [row for row in placements if str(row.get("name", "")) in set("012345")]
    glass = [row for row in placements if str(row.get("name", "")).startswith("glass")]
    toggles = [
        row for row in placements if str(row.get("name", "")).startswith("toggleFlash")
    ]
    submenus = [
        row for row in placements if str(row.get("name", "")).startswith("subMenu")
    ]
    if (
        [row.get("name") for row in numeric] != ["1", "2", "3", "4", "5", "0"]
        or [row.get("characterId") for row in numeric] != [108] * 6
        or sorted(str(row.get("name")) for row in glass)
        != [f"glass{i}" for i in range(6)]
        or [row.get("name") for row in toggles] != [f"toggleFlash{i}" for i in range(4)]
        or [row.get("name") for row in submenus] != [f"subMenu{i}" for i in range(4)]
        or not all(
            int.from_bytes(
                palantir.data[int(row["sourceOffset"]) : int(row["sourceOffset"]) + 4],
                "little",
            )
            == 3
            for row in placements
        )
    ):
        raise HudPalantirCommandOracleError("CommandButtons show placements changed")
    flash_frames = palantir.characters[113].get("frames", [])
    overlay_frames = palantir.characters[122].get("frames", [])
    flash_names = [
        row.get("name") for row in flash_frames[0] if row.get("kind") == "place-object"
    ]
    overlay_names = [
        row.get("name")
        for row in overlay_frames[0]
        if row.get("kind") == "place-object"
    ]
    if flash_names != ["0", "1", "2", "3", "4", "5"] or sorted(overlay_names) != [
        "0",
        "1",
        "2",
        "3",
        "4",
        "5",
    ]:
        raise HudPalantirCommandOracleError("command overlay collections changed")
    return {
        "root": [
            {
                "name": name,
                "characterId": int(row["characterId"]),
                "depth": int(row["depth"]),
                "sourceOffset": int(row["sourceOffset"]),
                "translation": list(row["translation"]),
            }
            for name, row in (
                ("CommandUI", command_ui),
                ("CommandButtons", command_buttons),
                ("AutoAbilityOverlays", overlays),
            )
        ],
        "skillUpgrade": {
            "characterId": 86,
            "labels": {"_hide": 0, "_show": 9},
            "showAction": 167296,
        },
        "commandButtons": {
            "characterId": 114,
            "labels": {"_hide": 0, "_show": 9},
            "declarationAction": 169224,
            "showAction": 169256,
            "showSourceOrder": [
                {
                    "kind": str(row["kind"]),
                    "sourceOffset": int(row["sourceOffset"]),
                    "rawRecordType": int.from_bytes(
                        palantir.data[
                            int(row["sourceOffset"]) : int(row["sourceOffset"]) + 4
                        ],
                        "little",
                    ),
                }
                for row in show_rows
            ],
            "numericButtonFrames": [str(row["name"]) for row in numeric],
            "glassTargets": sorted(str(row["name"]) for row in glass),
            "toggleFlashTargets": [str(row["name"]) for row in toggles],
            "subMenuTargets": [str(row["name"]) for row in submenus],
            "flashEffectTargets": flash_names,
            "autoAbilityOverlayTargets": sorted(str(name) for name in overlay_names),
        },
        "imports": [
            {"localCharacterId": local_id, "movie": movie, "symbol": symbol}
            for local_id, (movie, symbol) in sorted(palantir.imports.items())
            if local_id in {106, 108, 110, 111}
        ],
    }


def _native_contract(game_dat: bytes) -> dict[str, Any]:
    if (
        len(game_dat) != _GAME_DAT["byteLength"]
        or _sha(game_dat) != _GAME_DAT["sha256"]
    ):
        raise HudPalantirCommandOracleError("BFME2 1.06 game.dat identity changed")
    ranges: list[dict[str, Any]] = []
    for range_id, virtual_address, byte_length, digest in _NATIVE_RANGES:
        payload = _pe_range(game_dat, virtual_address, byte_length)
        if _sha(payload) != digest:
            raise HudPalantirCommandOracleError(
                f"native evidence range changed: {range_id}"
            )
        ranges.append(
            {
                "id": range_id,
                "virtualAddress": f"0x{virtual_address:08X}",
                "byteLength": byte_length,
                "sha256": digest,
            }
        )
    registry: list[dict[str, Any]] = []
    string_vas = [
        0x00C683F0,
        0x00C683C4,
        0x00C683A0,
        0x00C68378,
        0x00C68350,
        0x00C68324,
    ]
    for (name, handler, slot), string_va in zip(
        _HOST_REGISTRY, string_vas, strict=True
    ):
        payload = _pe_range(game_dat, string_va, len(name.encode("ascii")) + 1)
        if payload != name.encode("ascii") + b"\0":
            raise HudPalantirCommandOracleError(f"native host name changed: {name}")
        registry.append(
            {
                "name": name,
                "stringVirtualAddress": f"0x{string_va:08X}",
                "stringSha256": _sha(payload),
                "handlerVirtualAddress": handler,
                "retainedSlot": slot,
            }
        )
    return {
        "source": {"virtualPath": "game.dat", **_GAME_DAT},
        "ranges": ranges,
        "hostRegistry": registry,
        "indexPolicy": "parse index and accept only 0 through 5",
        "retainedSlotsPerIndex": ["button-frame", "submenu", "toggle-flash"],
        "frameScheduling": {
            "rawType1EffectDuringFrameRecordPass": "deferred",
            "rawType3EffectDuringFrameRecordPass": "place object immediately",
            "sameFramePlacementsVisibleToAction": True,
            "actionQueueRunsAfterTimelineTraversal": True,
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
    }


def _root_method_closure_evidence(
    palantir: _Movie,
    lib: _Movie,
    payloads: Mapping[str, bytes],
    game_dat: bytes,
) -> dict[str, Any]:
    names = ("UpdateSkillUpgradeButton", "SetCommandButtonState")
    definitions: set[str] = set()
    for movie in (palantir, lib):
        for _, _, row in _all_action_rows(movie):
            program = _decode_action_program(movie, row)
            definitions.update(
                str(instruction.get("functionName", ""))
                for instruction in _walk(program["instructions"])
                if instruction.get("name") in {"define-function", "define-function2"}
            )
    if any(name in definitions for name in names):
        raise HudPalantirCommandOracleError(
            "skill-upgrade root method unexpectedly entered the APT closure"
        )
    normalized = {str(key).casefold(): bytes(value) for key, value in payloads.items()}
    rows: list[dict[str, Any]] = []
    for name in names:
        encoded = name.encode("ascii")
        const_count = normalized["palantir.const"].count(encoded)
        other_source_count = sum(
            payload.count(encoded)
            for key, payload in normalized.items()
            if key != "palantir.const"
        )
        native_count = game_dat.count(encoded)
        if const_count != 1 or other_source_count != 0 or native_count != 0:
            raise HudPalantirCommandOracleError(
                f"skill-upgrade root method closure changed: {name}"
            )
        rows.append(
            {
                "name": name,
                "aptDefinitionCount": 0,
                "palantirConstReferenceCount": const_count,
                "otherAptClosureReferenceCount": other_source_count,
                "gameDatAsciiReferenceCount": native_count,
            }
        )
    return {
        "methods": rows,
        "conclusion": "typed call signatures are static; root method effects are outside the converted closure",
    }


def build_contract_from_payloads(
    payloads: Mapping[str, bytes], game_dat: bytes
) -> dict[str, Any]:
    """Build the deterministic payload-free command-interface oracle."""

    palantir = _movie(payloads, "Palantir")
    lib = _movie(payloads, "libInGameUI")
    programs = _programs(palantir, _PROGRAMS)
    lifecycle_functions, button_methods = _validate_semantics(palantir, programs)
    lifecycle_callers = _validate_lifecycle_callers(lib)
    reachability = _reachability(palantir)
    native = _native_contract(bytes(game_dat))
    root_method_closure = _root_method_closure_evidence(
        palantir, lib, payloads, bytes(game_dat)
    )

    scripts = [
        {
            key: programs[source_offset][key]
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
        for source_offset in (167_296, 169_224, 169_256)
    ]
    scripts[0]["typedEffect"] = {
        "authoredOrder": [
            {"call": "_root.UpdateSkillUpgradeButton", "arguments": []},
            {
                "when": "Boolean(_global.InGame)",
                "call": "_root.SetCommandButtonState",
                "argumentsInOrder": [
                    [1, "_up"],
                    [2, "_disabled"],
                    [4, "_up"],
                    [5, "_disabled"],
                ],
            },
        ],
        "classification": "typed-host-call-intent",
    }
    scripts[1]["typedEffect"] = {
        "classification": "declaration-only-typed-registration",
        "functions": [row["name"] for row in lifecycle_functions],
        "invocationDuringDeclaration": False,
    }
    scripts[2]["typedEffect"] = {
        "classification": "typed-per-button-method-registration",
        "buttonOrder": [str(index) for index in range(6)],
        "methodOrder": [row["name"] for row in button_methods],
        "invocationDuringRegistration": False,
    }

    contract: dict[str, Any] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "sources": [
            {"virtualPath": name, **identity} for name, identity in _SOURCES.items()
        ],
        "scripts": scripts,
        "lifecycleRegistrations": lifecycle_functions,
        "buttonMethodRegistrations": [
            {**row, "dispatch": "target.gotoAndPlay(state)"} for row in button_methods
        ],
        "lifecycleCallers": lifecycle_callers,
        "timelineReachability": reachability,
        "nativeRuntimeEvidence": native,
        "rootMethodClosureEvidence": root_method_closure,
        "implementationOpportunity": {
            "genericActionScriptVmRequired": False,
            "implementationSafeProgramIds": ["palantir:169224", "palantir:169256"],
            "hostIntentOnlyProgramIds": ["palantir:167296"],
            "declarationOnlyProgramIds": ["palantir:169224"],
            "reason": "registration programs create sealed callbacks or local methods but execute no callback bodies during registration",
        },
        "remainingTraceGates": [
            {
                "id": "skill-upgrade-root-method-effects",
                "programId": "palantir:167296",
                "scenario": "enter CommandUI _show once while InGame is true",
                "capture": [
                    "UpdateSkillUpgradeButton return/error and mutations",
                    "four SetCommandButtonState return/error and mutations",
                ],
                "reason": "the two _root methods are not defined in the converted APT closure or named in game.dat",
            },
            {
                "id": "command-child-lifecycle-host-result",
                "programId": "palantir:169224",
                "scenario": "one CommandButtons show-hide cycle",
                "capture": [
                    "six callback kinds in order",
                    "formatted arguments",
                    "native retained-slot create/clear results",
                ],
                "reason": "native registry and slot code are static, but converted clip-handle ownership must be bound",
            },
        ],
        "summary": {
            "targetScriptCount": 3,
            "declarationOnlyProgramCount": 1,
            "implementationSafeProgramCount": 2,
            "hostIntentOnlyProgramCount": 1,
            "lifecycleFunctionCount": 6,
            "buttonMethodCount": 3,
            "numericButtonFrameCount": 6,
            "nativeHostHandlerCount": 6,
            "remainingTraceGateCount": 2,
            "genericActionScriptVmRequired": False,
            "implementationIncluded": False,
        },
    }
    contract["aggregateSha256"] = _sha(_canonical(contract))
    return contract


def build_contract(asset_root: Path | str, game_dat: Path | str) -> dict[str, Any]:
    root = Path(asset_root)
    game_path = Path(game_dat)
    payloads = {name: (root / name).read_bytes() for name in _SOURCES}
    return build_contract_from_payloads(payloads, game_path.read_bytes())


def write_contract(contract: Mapping[str, Any], output: Path | str) -> None:
    destination = Path(output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(_canonical(contract))


def _main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("asset_root", type=Path)
    parser.add_argument("game_dat", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    contract = build_contract(args.asset_root, args.game_dat)
    write_contract(contract, args.output)
    print(f"PALANTIR_COMMAND_ORACLE {contract['aggregateSha256']}")
    return 0


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(_main())
