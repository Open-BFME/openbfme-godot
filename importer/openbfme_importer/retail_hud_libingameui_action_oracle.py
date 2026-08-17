"""Seal the BFME2 ``libInGameUI:37332`` MovieClipFrame program.

This is a payload-free static oracle, not an ActionScript VM.  It accepts only
the exact BFME2 1.06 HUD triplets and executable, verifies the complete local
function/lifecycle/placement chain, and emits typed semantics plus hashes.
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
)
from .sage_apt import parse_apt_constants, parse_apt_dat, parse_apt_movie


SCHEMA = "openbfme.private-hud-libingameui-action-oracle"
SCHEMA_VERSION = 0

_SOURCES: dict[str, dict[str, Any]] = {
    "libInGameUI.apt": {"byteLength": 58_462, "sha256": "305bdfabca3a815f8c373419978ca080a7f28561b2ca9d36eeeb7f35992ba392"},
    "libInGameUI.const": {"byteLength": 2_876, "sha256": "717a03669f47944f9933e829e8d5d1193e375cadbbdfc5804ee131631a7176cd"},
    "libInGameUI.dat": {"byteLength": 50, "sha256": "892429fd2c0e9dc1305897fb9bf7ab41f629f1d39b4139afa8ca4f29212d18f1"},
    "InGameSideCommandBar.apt": {"byteLength": 14_082, "sha256": "84d58c67c5cab9a3bf690125cbf1a0cbf3f4bc58ccc29ffa33b992a924eca6ef"},
    "InGameSideCommandBar.const": {"byteLength": 3_364, "sha256": "5f21b405a8121edb689365441177b38b32386f101a1bb06418336dbac815975a"},
    "InGameSideCommandBar.dat": {"byteLength": 50, "sha256": "892429fd2c0e9dc1305897fb9bf7ab41f629f1d39b4139afa8ca4f29212d18f1"},
    "Palantir.apt": {"byteLength": 378_173, "sha256": "c1f500847f0c77d4c6504edf79113b5723300165bebd42b4dafda479516f5140"},
    "Palantir.const": {"byteLength": 10_260, "sha256": "f07e24e3b70e286d491652cc827aef904a2ccabf54107d4f1bfc3030beee8fd9"},
    "Palantir.dat": {"byteLength": 586, "sha256": "d8e8964711e4061b0643dd0dd3de1876b7326cee6d60e11214793b5d483f3ae4"},
}

_GAME_DAT = {"byteLength": 10_969_600, "sha256": "f008b587570bad693981dc7218588c81d192a1e064b0f7f861539c51156a7640"}
_PE_SECTIONS = (
    (0x00401000, 0x00000600, 0x007B8CE2),
    (0x00BBA000, 0x007B9400, 0x001E9783),
    (0x00DA4000, 0x009A2C00, 0x0003A008),
)
_NATIVE_RANGES = (
    ("side-button-loaded-handler", 0x009283F8, 331, "ee1c3a918eaabf44dbbc20346d34a43ed367f1d6a5609a70be18106a1cf7f44f"),
    ("side-button-unloaded-handler", 0x00928738, 152, "f0b3fb13d41a11e47a80df9678041659b84c38ba8e7fae179770d5b21fdef0bd"),
    ("side-callback-registration", 0x009288C4, 511, "e150d50d97600b4fe9e77cb9d2bff5a74130c6160b3e32196b81dc50bb816c05"),
    ("palantir-button-loaded-handler", 0x00929698, 264, "5fb4bb4e8f44ce017acfaffb250ed1d993b0e9386bf1f3b7729c514f1182797e"),
    ("palantir-button-unloaded-handler", 0x009297A0, 61, "3c725f88f2e3a52351d5c4bacb39fbc55bd93f93f1f9bfcb28c0680caa8f2c25"),
    ("palantir-callback-registration", 0x0092A086, 396, "c7610722e103fa9e512c7f7ddecbf2cfe3230f06ba04a10f81a5b134941872d0"),
    ("movie-content-adapter-constructor", 0x009C31D5, 52, "3427055d21a04d81ffd51a519f139361e9a6c92c5e8faf19571a1cabf145254a"),
    ("movie-content-delete-dispatch", 0x009C3209, 55, "45df1c417c4f1a21a668830229403f4511f61681573e8bd3077a7d99b734569f"),
    ("movie-content-call-helper", 0x009C3240, 63, "32c76da2a59054691f00f5b6b316f77813f699d79c0b2dc48c152432f8f96629"),
    ("movie-content-create-dispatch", 0x009C329B, 311, "7435c9003e600d83eac617fcf16fd8b1854098418d25d71720102363c9b1db83"),
    ("frame-record-processing", 0x00B0F370, 559, "2b237694dddc118138533458dc28cdcc2547a1fbdd5fe9a9aebf7d20616df668"),
    ("frame-action-queue-run", 0x00AE6540, 1_191, "f51eba1f0ad75f8764a7c6a4951af20f1c20dd6ed4c4009057886894ac9bdf2e"),
)

_POOL = [
    "attachMovie", "contentClip", "_x", "placeholder", "_y", "_width",
    "_height", "extern", "_ContentName", "removeMovieClip", "initialized",
    "onUnload", "OnMovieClipFrameUnloaded", "this", "_parent",
    "OnMovieClipFrameLoaded",
]


class HudLibInGameUiActionOracleError(ValueError):
    """Raised when exact retail evidence differs from the sealed contract."""


def _sha(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def _private(path: Path | str, label: str, *, file: bool = False) -> Path:
    resolved = Path(path).resolve()
    if "workspace" not in {part.casefold() for part in resolved.parts}:
        raise HudLibInGameUiActionOracleError(f"{label} must remain under workspace")
    if file and not resolved.is_file():
        raise HudLibInGameUiActionOracleError(f"{label} is missing")
    return resolved


def _read_sources(root: Path) -> dict[str, bytes]:
    result: dict[str, bytes] = {}
    for name, expected in _SOURCES.items():
        path = (root / name).resolve()
        try:
            path.relative_to(root)
        except ValueError as exc:
            raise HudLibInGameUiActionOracleError(f"source escaped private root: {name}") from exc
        if not path.is_file() or path.is_symlink():
            raise HudLibInGameUiActionOracleError(f"private HUD source is missing or linked: {name}")
        payload = path.read_bytes()
        if len(payload) != expected["byteLength"] or _sha(payload) != expected["sha256"]:
            raise HudLibInGameUiActionOracleError(f"private HUD source identity changed: {name}")
        result[name.casefold()] = payload
    return result


def _movie(payloads: Mapping[str, bytes], name: str) -> _Movie:
    names = (f"{name}.apt", f"{name}.const", f"{name}.dat")
    for source_name in names:
        expected = _SOURCES[source_name]
        payload = payloads.get(source_name.casefold())
        if payload is None:
            raise HudLibInGameUiActionOracleError(
                f"private HUD source is missing: {source_name}"
            )
        if (
            len(payload) != expected["byteLength"]
            or _sha(payload) != expected["sha256"]
        ):
            raise HudLibInGameUiActionOracleError(
                f"private HUD source identity changed: {source_name}"
            )
    constants = parse_apt_constants(payloads[names[1].casefold()], names[1])
    apt = parse_apt_movie(payloads[names[0].casefold()], constants, names[0])
    image_map = parse_apt_dat(payloads[names[2].casefold()], names[2])
    return _movie_from_plan(
        {"movie": name, "apt": apt, "constants": constants, "imageMap": image_map, "geometry": [], "atlases": []},
        source_bytes=payloads,
    )


def _actions(movie: _Movie) -> Iterable[tuple[str, int, Mapping[str, Any]]]:
    for frame, rows in enumerate(movie.frames):
        for row in rows:
            if row.get("kind") == "action-script":
                yield "root", frame, row
    for character in movie.characters:
        frames = character.get("frames")
        if isinstance(frames, list):
            for frame, rows in enumerate(frames):
                for row in rows:
                    if row.get("kind") == "action-script":
                        yield f"sprite:{character['characterId']}", frame, row


def _program(movie: _Movie, source_offset: int, owner: str, frame: int) -> dict[str, Any]:
    rows = [(o, f, row) for o, f, row in _actions(movie) if row.get("sourceOffset") == source_offset]
    if len(rows) != 1 or rows[0][:2] != (owner, frame):
        raise HudLibInGameUiActionOracleError(f"action reachability changed: {movie.name}:{source_offset}")
    return _decode_action_program(movie, rows[0][2])


def _body_identity(movie: _Movie, row: Mapping[str, Any], expected: Mapping[str, Any]) -> dict[str, Any]:
    body = row.get("body", [])
    start = int(body[0]["offset"]) if body else int(row["nextOffset"])
    if (
        row.get("functionName") != expected["name"]
        or int(row["offset"]) != expected["definitionOffset"]
        or int(row.get("bodyByteLength", -1)) != expected["bodyByteLength"]
        or start != expected["bodyOffset"]
        or _sha(movie.data[start : start + expected["bodyByteLength"]]) != expected["bodySha256"]
    ):
        raise HudLibInGameUiActionOracleError(f"function body changed: {expected['name'] or 'onUnload'}")
    return dict(expected)


def _validate_main(movie: _Movie) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    program = _program(movie, 37_332, "sprite:6", 0)
    expected = {"instructionOffset": 53_656, "byteLength": 294, "sha256": "81298e35028262cc75249d9ee6057fc1c105369e81aeec3bb485d09a2f59cd70"}
    if any(program[key] != value for key, value in expected.items()):
        raise HudLibInGameUiActionOracleError("libInGameUI action identity changed")
    if _sha(movie.data[37_332:37_340]) != "32d5cbdb23a531eb389742291823b5884d511518f7ebbe6141e7804c091f85de":
        raise HudLibInGameUiActionOracleError("libInGameUI action record changed")
    instructions = program["instructions"]
    pool = [row.get("value") for row in instructions[0].get("constants", [])]
    if instructions[0].get("name") != "constant-pool" or pool != _POOL:
        raise HudLibInGameUiActionOracleError("libInGameUI local constant pool changed")
    definitions = [row for row in instructions if row.get("name") in {"define-function", "define-function2"}]
    identities = (
        {"name": "CreateContent", "definitionOffset": 53_668, "bodyOffset": 53_700, "bodyByteLength": 124, "bodySha256": "f499709843084c98674ded5217e58809940477af59df1c6e211cc362ce3b8c74"},
        {"name": "DeleteContent", "definitionOffset": 53_824, "bodyOffset": 53_852, "bodyByteLength": 17, "bodySha256": "1266498fbecfad0810a5c2efe2cce51cb31ca434f57ac27563459c1f93aa1eff"},
        {"name": "", "definitionOffset": 53_882, "bodyOffset": 53_912, "bodyByteLength": 26, "bodySha256": "6d939bcd2684cff9105893f26b41e5ad90dd8ce8296cd55caeda9680a663c776"},
    )
    if len(definitions) != 3:
        raise HudLibInGameUiActionOracleError("libInGameUI function count changed")
    bodies = [_body_identity(movie, row, identity) for row, identity in zip(definitions, identities, strict=True)]
    if definitions[0].get("parameters") != [{"register": 3, "name": "contentType"}, {"register": 2, "name": "contentName"}]:
        raise HudLibInGameUiActionOracleError("CreateContent signature changed")
    create_body = definitions[0]["body"]
    create_names = [row["name"] for row in create_body]
    if (
        create_names.count("set-member") != 5
        or [row.get("operand") for row in create_body if row["name"] == "get-named-member"]
        != [2, 4, 5, 6]
        or [row.get("operand") for row in create_body if row["name"] == "push-constant-byte"]
        != [1, 2, 4, 5, 6, 8]
    ):
        raise HudLibInGameUiActionOracleError(
            "CreateContent property-write order changed"
        )
    branches = [(row["offset"], row.get("targetOffset")) for row in program["instructions"] if row.get("name") == "branch-if-true"]
    branches += [(row["offset"], row.get("targetOffset")) for definition in definitions for row in definition.get("body", []) if row.get("name") == "branch-if-true"]
    if sorted(branches) != [(53_760, 53_824), (53_858, 53_869), (53_873, 53_949)]:
        raise HudLibInGameUiActionOracleError("libInGameUI branch targets changed")
    return program, bodies


def _find_function(program: Mapping[str, Any], name: str) -> Mapping[str, Any]:
    rows = [row for row in program["instructions"] if row.get("functionName") == name]
    if len(rows) != 1:
        raise HudLibInGameUiActionOracleError(f"parent lifecycle function changed: {name}")
    return rows[0]


def _parent_contract(side: _Movie, palantir: _Movie) -> list[dict[str, Any]]:
    specs = (
        (side, 6_264, "sprite:18", 0, "ingamesidecommandbar", 10_956, 996, "abcf2a697a9852b4b61c07de74f7e4151bed6cd467ebd98bb0eb74e17833fa16",
         (("OnMovieClipFrameLoaded", 11_825, 11_852, 34, "32704f6f91187f007bc729b2405509c666ba73300b807afd71c9f8062d3f1d93", "OnAptInGameSideCommandBarButtonFrameLoaded", "index=this._name.substr(6)&name=String(clip)"),
          ("OnMovieClipFrameUnloaded", 11_886, 11_912, 27, "ba0303c4a9d2cb6c3d7eb991685ab7f53ba2015bf3864986f140a19e06ca517b", "OnAptInGameSideCommandBarButtonFrameUnloaded", "index=this._name.substr(6)"))),
        (palantir, 169_224, "sprite:114", 0, "palantir", 367_624, 484, "3e6f347f6c6574a2d40e85f8f564c1f9af1c13513d0f1671298a1484d629fbfc",
         (("OnMovieClipFrameLoaded", 367_636, 367_668, 51, "2e04d77ff99a163925615cc9e6b2c7d83dbf945b428d3c9baea695a95c1e12fd", "PalantirCommandUI::OnButtonFrameLoaded", "index=clip._name&name=String(clip)"),
          ("OnMovieClipFrameUnloaded", 367_719, 367_748, 36, "4ab0920334b617d403a746a23b7634ca1c5511974f70fd6020e3e32ac7934214", "PalantirCommandUI::OnButtonFrameUnloaded", "index=clip._name"))),
    )
    output: list[dict[str, Any]] = []
    for movie, source, owner, frame, context, instruction, length, digest, functions in specs:
        program = _program(movie, source, owner, frame)
        if (program["instructionOffset"], program["byteLength"], program["sha256"]) != (instruction, length, digest):
            raise HudLibInGameUiActionOracleError(f"{context} parent program changed")
        callbacks = []
        for name, definition, body_offset, body_length, body_hash, host, argument in functions:
            row = _find_function(program, name)
            _body_identity(movie, row, {"name": name, "definitionOffset": definition, "bodyOffset": body_offset, "bodyByteLength": body_length, "bodySha256": body_hash})
            callbacks.append({"method": name, "host": host, "argument": argument, "definitionOffset": definition, "bodySha256": body_hash})
        output.append({"context": context, "parentCharacterId": 18 if context == "ingamesidecommandbar" else 114, "programId": f"{context}:{source}", "callbacks": callbacks})
    return output


def _placements(lib: _Movie, side: _Movie, palantir: _Movie) -> dict[str, Any]:
    if lib.exports.get("movieclipframe") != [6, 6]:
        raise HudLibInGameUiActionOracleError("MovieClipFrame export selection changed")
    frame = lib.characters[6].get("frames")
    if not isinstance(frame, list) or len(frame) != 1 or [row.get("sourceOffset") for row in frame[0]] != [37_332, 37_340]:
        raise HudLibInGameUiActionOracleError("MovieClipFrame frame-0 order changed")
    placeholder = frame[0][1]
    if (placeholder.get("kind"), placeholder.get("name"), placeholder.get("characterId"), placeholder.get("depth")) != ("place-object", "placeholder", 5, 1):
        raise HudLibInGameUiActionOracleError("MovieClipFrame placeholder changed")
    if _sha(lib.data[37_340:37_400]) != "97014f343299987df90e99ac5bef14b0aaf73e2c781cb3b0967b925e34c4634f":
        raise HudLibInGameUiActionOracleError("MovieClipFrame placeholder record changed")

    if side.imports.get(1) != ("libInGameUI", "MovieClipFrame"):
        raise HudLibInGameUiActionOracleError("side-command MovieClipFrame import changed")
    side_button = side.characters[18]["frames"][0]
    imported = [row for row in side_button if row.get("kind") == "place-object" and row.get("characterId") == 1]
    button_set = side.characters[21]["frames"][0]
    buttons = [row for row in button_set if row.get("kind") == "place-object" and row.get("characterId") == 18]
    root_side = [row for row in side.frames[0] if row.get("kind") == "place-object" and row.get("characterId") == 21]
    if len(imported) != 1 or (imported[0].get("name"), imported[0].get("sourceOffset")) != ("Button", 6_296):
        raise HudLibInGameUiActionOracleError("side-command imported-frame placement changed")
    if [row.get("name") for row in buttons] != [f"Button{i}" for i in range(12)] or len(root_side) != 1 or root_side[0].get("sourceOffset") != 3_400:
        raise HudLibInGameUiActionOracleError("side-command placement closure changed")

    if palantir.imports.get(108) != ("libInGameUI", "MovieClipFrame"):
        raise HudLibInGameUiActionOracleError("Palantir MovieClipFrame import changed")
    command_frames = palantir.characters[114]["frames"]
    pal_places = [row for row in command_frames[9] if row.get("kind") == "place-object" and row.get("characterId") == 108]
    root_pal = [row for row in palantir.frames[0] if row.get("kind") == "place-object" and row.get("characterId") == 114]
    expected_offsets = [169_600, 169_664, 169_728, 169_792, 169_856, 169_920]
    if [row.get("sourceOffset") for row in pal_places] != expected_offsets or [row.get("name") for row in pal_places] != ["1", "2", "3", "4", "5", "0"]:
        raise HudLibInGameUiActionOracleError("Palantir imported-frame placement order changed")
    if len(root_pal) != 1 or (root_pal[0].get("sourceOffset"), root_pal[0].get("name")) != (96_776, "CommandButtons"):
        raise HudLibInGameUiActionOracleError("Palantir CommandButtons root placement changed")
    return {
        "export": {"movie": "libInGameUI", "symbol": "MovieClipFrame", "characterId": 6, "duplicateExportRows": 2, "frameIndex": 0, "sourceOrder": [37_332, 37_340]},
        "sideCommand": {"rootFrame": 0, "rootPlacement": "ButtonSet", "buttonCount": 12, "buttonNames": [f"Button{i}" for i in range(12)], "importCharacterId": 1, "importPlacement": {"owner": "sprite:18", "frameIndex": 0, "name": "Button", "sourceOffset": 6_296}, "reachability": "immediate-from-root-frame-0"},
        "palantir": {"rootFrame": 0, "rootPlacement": "CommandButtons", "showLabelFrame": 9, "importCharacterId": 108, "instanceNamesInSourceOrder": ["1", "2", "3", "4", "5", "0"], "placementSourceOffsets": expected_offsets, "reachability": "when-CommandButtons-enters-_show-frame-9"},
    }


def _pe_range(payload: bytes, va: int, length: int) -> bytes:
    for section_va, file_offset, section_length in _PE_SECTIONS:
        relative = va - section_va
        if 0 <= relative and relative + length <= section_length:
            return payload[file_offset + relative : file_offset + relative + length]
    raise HudLibInGameUiActionOracleError(f"native range is outside sealed PE sections: 0x{va:08X}")


def _native(game_dat: bytes) -> dict[str, Any]:
    if len(game_dat) != _GAME_DAT["byteLength"] or _sha(game_dat) != _GAME_DAT["sha256"]:
        raise HudLibInGameUiActionOracleError("game.dat is not the pinned BFME2 1.06 executable")
    ranges = []
    for name, va, length, digest in _NATIVE_RANGES:
        if _sha(_pe_range(game_dat, va, length)) != digest:
            raise HudLibInGameUiActionOracleError(f"native evidence changed: {name}")
        ranges.append({"id": name, "virtualAddress": f"0x{va:08X}", "byteLength": length, "sha256": digest})
    return {
        "source": {"virtualPath": "game.dat", **_GAME_DAT},
        "ranges": ranges,
        "contentAdapterOrder": ["read _level%u.%s_ContentName from extern", "invoke CreateContent with two dynamic strings", "retain returned clip handle", "on teardown invoke DeleteContent", "clear initialized native adapter state"],
        "frameOrder": ["apply type-3 placement", "queue type-1 frame action", "run frame action after timeline traversal"],
        "hostRegistrations": {
            "OnAptInGameSideCommandBarButtonFrameLoaded": "0x009283F8",
            "OnAptInGameSideCommandBarButtonFrameUnloaded": "0x00928738",
            "PalantirCommandUI::OnButtonFrameLoaded": "0x00929698",
            "PalantirCommandUI::OnButtonFrameUnloaded": "0x009297A0",
        },
    }


def build_contract_from_payloads(payloads: Mapping[str, bytes], game_dat: bytes) -> dict[str, Any]:
    """Build the deterministic payload-free contract from exact private bytes."""
    lib = _movie(payloads, "libInGameUI")
    side = _movie(payloads, "InGameSideCommandBar")
    palantir = _movie(payloads, "Palantir")
    program, bodies = _validate_main(lib)
    parents = _parent_contract(side, palantir)
    placement = _placements(lib, side, palantir)
    native = _native(bytes(game_dat))
    contract: dict[str, Any] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "sources": [{"virtualPath": name, **identity} for name, identity in _SOURCES.items()],
        "program": {"scriptId": program["scriptId"], "owner": "sprite:6", "frameIndex": 0, "sourceOffset": 37_332, "instructionOffset": 53_656, "byteLength": 294, "sha256": program["sha256"], "recordSha256": "32d5cbdb23a531eb389742291823b5884d511518f7ebbe6141e7804c091f85de"},
        "functionBodies": [
            {**bodies[0], "parameters": ["contentType", "contentName"], "typedEffect": ["attachMovie(contentType, contentName, 0)", "contentClip=this[contentName]", "if contentClip is defined copy placeholder _x,_y,_width,_height in order", "if contentClip is defined set extern[String(this)+'_ContentName']=String(contentClip)"]},
            {**bodies[1], "parameters": [], "typedEffect": "if contentClip is defined: contentClip.removeMovieClip()"},
            {**bodies[2], "installedAs": "onUnload", "typedEffect": "_parent.OnMovieClipFrameUnloaded(this)"},
        ],
        "branchInputs": [
            {"offset": 53_760, "input": "contentClip == undefined", "trueTarget": 53_824, "trueEffect": "skip geometry and extern registration"},
            {"offset": 53_858, "input": "contentClip == undefined", "trueTarget": 53_869, "trueEffect": "skip removeMovieClip"},
            {"offset": 53_873, "input": "Boolean(initialized)", "trueTarget": 53_949, "trueEffect": "skip onUnload installation, loaded callback, and initialized assignment"},
        ],
        "firstInitializationOrder": ["install CreateContent", "install DeleteContent", "test initialized", "install onUnload", "call _parent.OnMovieClipFrameLoaded(this)", "set initialized=true"],
        "parentHostMethods": parents,
        "timelineAndPlacementReachability": placement,
        "nativeRuntimeEvidence": native,
        "implementationDecision": {
            "declarationOnly": False,
            "genericActionScriptVmRequired": False,
            "staticTypedAdapterImplementationSafe": True,
            "runtimeSupportIncluded": False,
            "profileBlockerRemovalAuthorized": False,
            "typedSlice": ["local CreateContent/DeleteContent registrations", "initialized first-entry guard", "ordered parent load/unload lifecycle calls", "placeholder geometry copy", "extern clip-path registration"],
            "dynamicContentPolicy": "resolve contentType only through the converted retail export allowlist; missing exports preserve retail undefined/no-op branch",
            "runtimeTraceRequired": False,
            "remainingGate": "the requested contentType must exist in the converted retail movie/export closure before that concrete child can render",
        },
        "summary": {
            "targetProgramCount": 1,
            "localFunctionCount": 3,
            "parentContextCount": 2,
            "nativeRangeCount": len(_NATIVE_RANGES),
            "declarationOnly": False,
            "staticTypedAdapterImplementationSafe": True,
            "runtimeSupportIncluded": False,
            "genericActionScriptVmRequired": False,
            "remainingConcreteContentGateCount": 1,
        },
    }
    contract["aggregateSha256"] = _sha(_canonical(contract))
    return contract


def build_contract(asset_root: Path | str, game_dat: Path | str) -> dict[str, Any]:
    root = _private(asset_root, "asset root")
    if not root.is_dir():
        raise HudLibInGameUiActionOracleError("asset root is missing")
    game_path = Path(game_dat).resolve()
    if not game_path.is_file():
        raise HudLibInGameUiActionOracleError("game.dat is missing")
    return build_contract_from_payloads(_read_sources(root), game_path.read_bytes())


def write_contract(contract: Mapping[str, Any], output: Path | str) -> None:
    destination = _private(output, "output")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(_canonical(contract))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("asset_root", type=Path)
    parser.add_argument("game_dat", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    contract = build_contract(args.asset_root, args.game_dat)
    write_contract(contract, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
