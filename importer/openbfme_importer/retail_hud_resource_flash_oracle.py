"""Recover the exact BFME2 Palantir command-point flash contract.

This oracle is deliberately narrower than an ActionScript VM.  It accepts only
the hash-pinned BFME2 1.06 Palantir sources, decoded scene contract, audio
definition/leaf, and game.dat bridge.  Its output contains identities and
typed semantics, never retail payload bytes.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path
from typing import Any, Mapping

from .sage_apt import parse_apt_constants, parse_apt_movie


SCHEMA = "openbfme.private-hud-resource-flash-oracle"
SCENE_SHA256 = "2a5ce91c1f0c6ef36124df80ac7ed5fa033bd39d1fbfe8bf8ebecf4e741aa6f6"
APT_SHA256 = "c1f500847f0c77d4c6504edf79113b5723300165bebd42b4dafda479516f5140"
CONST_SHA256 = "f07e24e3b70e286d491652cc827aef904a2ccabf54107d4f1bfc3030beee8fd9"
SOUND_INI_SHA256 = "f01d5ae3d8a0bab1fe0354d00e93dee0d8093a20e5a5441d4b6e1add130ec0b8"
WAVE_SHA256 = "f2d3aff531ecfd3616069d53551823f92aee92f009382d3bf39d4ec8e2eca350"
GAME_DAT_SHA256 = "f008b587570bad693981dc7218588c81d192a1e064b0f7f861539c51156a7640"

SCRIPT_ID = "palantir:332504"
SCRIPT_SHA256 = "0b966556e6fc10d1eaa5c129999f31e185b634425298b7bdaf21b6dd26aeb999"
TRIGGER_SCRIPT_SHA256 = (
    "7d3fe42f7872a5c1c9c34446b8707512d1ac80abf9a1459a1ad3586e035e07d9"
)
TRIGGER_BODY_SHA256 = (
    "a5b9a91b9ad21d12bced1a7d9f94c803d2abbb5fe542646356fdc90663f47788"
)
STOP_SCRIPT_SHA256 = "0a6361b3a802f55cd5ae06101c88a1e216320fe11cc0cfe1d791eed08a1200fd"
RETURN_SCRIPT_SHA256 = (
    "63f31f0d16b4dd378c7f77e841de4cd6238e01f6ea895d47b6931a888e65e663"
)
TIMELINE_SHA256 = "f2254f867b5f59070284fd2f028d5f4e4d787f09af9f59220491559053b069d6"
PLACEMENT_SHA256 = "6673eea4c330f20d073788d1f1bc36f50ba4b456a73a7ff1e40477da6b93c527"
AUDIO_BLOCK_SHA256 = (
    "8e0a306685847fca266de43f6d13329f43477f2e638f869ed5406b4045e6de90"
)

TEXT_DELTA = 0x400A00
NATIVE_HANDLER = (
    0x00812A51,
    0x00812AF3,
    "d7552e58b40a463b9f39d1cb6a3fa92dd0a6d8c0014fbc5234380865c447c6da",
)
NATIVE_REGISTRATION = (
    0x00812FD1,
    0x00813015,
    "4bc8533f87a74a685541301a6695dde67bb76fffe047b54df889a329415a4145",
)
NATIVE_APT_TRIGGER = (
    0x007FE9BB,
    0x007FE9DA,
    "0db675e029ff06307ba4b9185ffed58c6adbf316667c1b3a62232089f4acb55d",
)

ASSETS: dict[str, dict[str, Any]] = {
    "Palantir.apt": {
        "archive": "apt/palantir.big",
        "size": 378173,
        "sha256": APT_SHA256,
    },
    "Palantir.const": {
        "archive": "apt/palantir.big",
        "size": 10260,
        "sha256": CONST_SHA256,
    },
    "data/ini/soundeffects.ini": {
        "archive": "ini.big",
        "size": 677261,
        "sha256": SOUND_INI_SHA256,
    },
    "data/audio/sounds/ucommandpoints.wav": {
        "archive": "audio.big",
        "size": 96316,
        "sha256": WAVE_SHA256,
    },
}


def _canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def _sha(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _private_root(path: Path | str) -> Path:
    root = Path(path).resolve()
    if "workspace" not in {part.casefold() for part in root.parts}:
        raise ValueError("effective-assets must remain under workspace")
    return root


def _one_script(scene: Mapping[str, Any], script_id: str) -> Mapping[str, Any]:
    rows = [row for row in scene.get("actionScripts", []) if row.get("scriptId") == script_id]
    if len(rows) != 1:
        raise ValueError(f"expected exactly one script {script_id}")
    return rows[0]


def _manifest_asset(
    manifest: Mapping[str, Any], virtual_path: str, expected: Mapping[str, Any]
) -> None:
    rows = [
        row
        for row in manifest.get("files", [])
        if str(row.get("path", "")).casefold() == virtual_path.casefold()
    ]
    if len(rows) != 1:
        raise ValueError(f"manifest winner count changed: {virtual_path}")
    row = rows[0]
    for key in ("archive", "size", "sha256"):
        if row.get(key) != expected[key]:
            raise ValueError(f"manifest winner changed: {virtual_path} {key}")


def _source(root: Path, virtual_path: str, expected: Mapping[str, Any]) -> bytes:
    path = (root / Path(*virtual_path.split("/"))).resolve()
    try:
        path.relative_to(root)
    except ValueError as exc:
        raise ValueError(f"source escaped private root: {virtual_path}") from exc
    if not path.is_file() or path.is_symlink():
        raise ValueError(f"source is missing or linked: {virtual_path}")
    payload = path.read_bytes()
    if len(payload) != expected["size"] or _sha(payload) != expected["sha256"]:
        raise ValueError(f"source identity changed: {virtual_path}")
    return payload


def _cstring(payload: bytes, offset: int) -> str:
    if not 0 <= offset < len(payload):
        raise ValueError("APT string pointer is out of bounds")
    end = payload.find(b"\0", offset, min(len(payload), offset + 257))
    if end < 0:
        raise ValueError("APT string is not bounded")
    return payload[offset:end].decode("cp1252")


def _validate_placement(apt: bytes) -> dict[str, Any]:
    offset = 97352
    record = apt[offset : offset + 60]
    if len(record) != 60 or _sha(record) != PLACEMENT_SHA256:
        raise ValueError("CommandPointsFlash placement changed")
    kind, flags, depth, character_id = struct.unpack_from("<IIii", record)
    matrix = list(struct.unpack_from("<4f", record, 16))
    translation = list(struct.unpack_from("<2f", record, 32))
    ratio = struct.unpack_from("<f", record, 48)[0]
    name_pointer = struct.unpack_from("<I", record, 52)[0]
    clip_depth = struct.unpack_from("<i", record, 56)[0]
    if (
        (kind, flags, depth, character_id) != (3, 0x26, 148, 309)
        or matrix != [1.0, 0.0, 0.0, 1.0]
        or translation != [203.5, 770.2999877929688]
        or record[40:48] != b"\xff\xff\xff\xff\x00\x00\x00\x00"
        or ratio != 0.0
        or clip_depth != -1
        or _cstring(apt, name_pointer) != "CommandPointsFlash"
    ):
        raise ValueError("CommandPointsFlash placement semantics changed")
    return {
        "sourceOffset": offset,
        "byteLength": 60,
        "sha256": PLACEMENT_SHA256,
        "depth": depth,
        "characterId": character_id,
        "instanceName": "CommandPointsFlash",
        "translation": translation,
    }


def _validate_trigger(scene: Mapping[str, Any], apt: bytes) -> dict[str, Any]:
    row = _one_script(scene, "palantir:95856")
    if row.get("sha256") != TRIGGER_SCRIPT_SHA256 or row.get("byteLength") != 4052:
        raise ValueError("Palantir public API script changed")
    instructions = row.get("instructions", [])
    pools = [item for item in instructions if item.get("name") == "constant-pool"]
    functions = [
        item
        for item in instructions
        if item.get("functionName") == "PlayCommandPointEffect"
    ]
    if len(pools) != 1 or len(functions) != 1:
        raise ValueError("PlayCommandPointEffect identity changed")
    pool = [constant.get("value") for constant in pools[0].get("constants", [])]
    if len(pool) <= 39 or (pool[1], pool[38], pool[39]) != (
        "gotoAndPlay",
        "_go",
        "CommandPointsFlash",
    ):
        raise ValueError("PlayCommandPointEffect constant pool changed")
    function = functions[0]
    if (
        function.get("parameters") != []
        or function.get("bodyByteLength") != 16
        or function.get("offset") != 361120
    ):
        raise ValueError("PlayCommandPointEffect signature changed")
    body = function.get("body", [])
    if [item.get("name") for item in body] != [
        "push-constant-byte",
        "push-one",
        "push-data",
        "get-named-member",
        "call-named-method-pop",
    ]:
        raise ValueError("PlayCommandPointEffect instruction order changed")
    if [body[index].get("operand") for index in (0, 3, 4)] != [38, 39, 1]:
        raise ValueError("PlayCommandPointEffect operands changed")
    body_bytes = apt[361152:361168]
    if _sha(body_bytes) != TRIGGER_BODY_SHA256:
        raise ValueError("PlayCommandPointEffect body bytes changed")
    return {
        "receiver": "Palantir root",
        "method": "PlayCommandPointEffect",
        "arguments": [],
        "bodyOffset": 361152,
        "bodyByteLength": 16,
        "bodySha256": TRIGGER_BODY_SHA256,
        "effect": {
            "target": "CommandPointsFlash",
            "method": "gotoAndPlay",
            "arguments": ["_go"],
        },
    }


def _validate_flash_script(scene: Mapping[str, Any], apt: bytes) -> dict[str, Any]:
    row = _one_script(scene, SCRIPT_ID)
    if (
        row.get("sourceOffset") != 332504
        or row.get("instructionOffset") != 370752
        or row.get("byteLength") != 26
        or row.get("sha256") != SCRIPT_SHA256
    ):
        raise ValueError("resource-flash script identity changed")
    instructions = row.get("instructions", [])
    if [item.get("name") for item in instructions] != [
        "play",
        "push-string",
        "push-one",
        "get-string-variable",
        "push-string",
        "call-method-pop",
        "end",
    ]:
        raise ValueError("resource-flash script order changed")
    if [instructions[index].get("operand") for index in (1, 3, 4)] != [
        "Gui_PalantirResourceBarFlash",
        "_root",
        "PlaySound",
    ]:
        raise ValueError("resource-flash script operands changed")
    source_record = apt[332504:332512]
    instruction_bytes = apt[370752:370778]
    if (
        source_record != bytes.fromhex("0100000040a80500")
        or _sha(source_record)
        != "88ce32e6c4527b4c83efdd1c0f2c4311274cc4977eb19ab9a80c892e0f710c96"
        or _sha(instruction_bytes) != SCRIPT_SHA256
    ):
        raise ValueError("resource-flash APT bytes changed")
    return {
        "scriptId": SCRIPT_ID,
        "sourceOffset": 332504,
        "instructionRange": [370752, 370778],
        "byteLength": 26,
        "sha256": SCRIPT_SHA256,
        "sourceRecordRange": [332504, 332512],
        "sourceRecordSha256": (
            "88ce32e6c4527b4c83efdd1c0f2c4311274cc4977eb19ab9a80c892e0f710c96"
        ),
        "effectsInAuthoredOrder": [
            {"kind": "play-current-timeline"},
            {
                "kind": "call-root-method",
                "receiver": "_root",
                "method": "PlaySound",
                "arguments": ["Gui_PalantirResourceBarFlash"],
                "discardReturn": True,
            },
        ],
    }


def _validate_root_play_sound(scene: Mapping[str, Any], apt: bytes) -> dict[str, Any]:
    row = _one_script(scene, "palantir:95864")
    if (
        row.get("instructionOffset") != 363916
        or row.get("byteLength") != 1164
        or row.get("sha256")
        != "b411252632a770c924a57cb2009cc09fb3d93c1c8a1f7a4e077459b84cfa9381"
    ):
        raise ValueError("Palantir PlaySound owner script changed")
    instructions = row.get("instructions", [])
    pools = [item for item in instructions if item.get("name") == "constant-pool"]
    functions = [
        item for item in instructions if item.get("functionName") == "PlaySound"
    ]
    if len(pools) != 1 or len(functions) != 1:
        raise ValueError("Palantir PlaySound function identity changed")
    pool = [constant.get("value") for constant in pools[0].get("constants", [])]
    if len(pool) <= 26 or [pool[index] for index in (18, 21, 24, 25, 26)] != [
        "DoTrace",
        ")",
        "Initialized",
        "Call PlaySound(",
        "FSCommand:PlaySound",
    ]:
        raise ValueError("Palantir PlaySound constants changed")
    function = functions[0]
    if (
        function.get("offset") != 364585
        or function.get("nextOffset") != 364685
        or function.get("bodyByteLength") != 69
        or function.get("parameters") != [{"name": "name", "register": 3}]
    ):
        raise ValueError("Palantir PlaySound signature changed")
    if (
        _sha(apt[364585:364685])
        != "3f79f58d64e3385598b5dfe3866372ed12f53800adec64f62e9d87b035607dc6"
        or _sha(apt[364616:364685])
        != "d67876d2e44a19cb2b7d5a5ca8460a0fbe7c8ddef3ddfcd0b245cd42177a761c"
    ):
        raise ValueError("Palantir PlaySound bytes changed")
    return {
        "name": "PlaySound",
        "parameters": [{"name": "name", "type": "exact audio event ID"}],
        "functionRange": [364585, 364685],
        "functionSha256": (
            "3f79f58d64e3385598b5dfe3866372ed12f53800adec64f62e9d87b035607dc6"
        ),
        "bodyRange": [364616, 364685],
        "bodySha256": (
            "d67876d2e44a19cb2b7d5a5ca8460a0fbe7c8ddef3ddfcd0b245cd42177a761c"
        ),
        "precondition": "Palantir root Initialized is truthy",
        "optionalBeforeEffect": (
            "when DoTrace is truthy, trace Call PlaySound(<name>)"
        ),
        "effect": "getURL('FSCommand:PlaySound', name)",
        "usesGameCodeWrapper": False,
    }


def _validate_timeline(scene: Mapping[str, Any]) -> dict[str, Any]:
    rows = [row for row in scene.get("timelines", []) if row.get("timelineId") == "palantir:309"]
    if len(rows) != 1 or _sha(_canonical(rows[0])) != TIMELINE_SHA256:
        raise ValueError("CommandPointsFlash timeline changed")
    timeline = rows[0]
    frames = timeline.get("frames", [])
    if (
        timeline.get("characterId") != 309
        or timeline.get("frameCount") != 58
        or not timeline.get("displayListComplete")
        or len(frames) != 58
    ):
        raise ValueError("CommandPointsFlash timeline shape changed")
    stop = _one_script(scene, "palantir:332480")
    returned = _one_script(scene, "palantir:358480")
    if stop.get("sha256") != STOP_SCRIPT_SHA256 or stop.get("effects") != [{"kind": "stop"}]:
        raise ValueError("CommandPointsFlash stop frame changed")
    if returned.get("sha256") != RETURN_SCRIPT_SHA256 or returned.get("effects") != [
        {"kind": "goto", "target": "_stop", "targetType": "label"},
        {"kind": "play"},
    ]:
        raise ValueError("CommandPointsFlash return frame changed")
    if frames[0].get("labels") != [
        {"flags": 458752, "frameId": 0, "name": "_stop", "sourceOffset": 332488}
    ]:
        raise ValueError("CommandPointsFlash _stop label changed")
    if frames[8].get("labels") != [
        {"flags": 458752, "frameId": 8, "name": "_go", "sourceOffset": 332512}
    ]:
        raise ValueError("CommandPointsFlash _go label changed")
    names = sorted(
        {
            item.get("name")
            for frame in frames
            for item in frame.get("displayList", [])
            if item.get("name")
        }
    )
    character_ids = sorted(
        {
            item.get("characterId")
            for frame in frames
            for item in frame.get("displayList", [])
            if item.get("characterId") is not None
        }
    )
    if names != ["effect1", "effect2", "effect3", "effect4"] or character_ids != [
        296,
        299,
        302,
        305,
        308,
    ]:
        raise ValueError("CommandPointsFlash visual closure changed")
    return {
        "timelineId": "palantir:309",
        "characterId": 309,
        "frameCount": 58,
        "millisecondsPerFrame": 33,
        "timelineSha256": TIMELINE_SHA256,
        "stoppedFrame": {"index": 0, "label": "_stop", "script": "palantir:332480"},
        "entryFrame": {"index": 8, "label": "_go", "script": SCRIPT_ID},
        "returnFrame": {"index": 57, "script": "palantir:358480"},
        "entryToReturnIntervals": 49,
        "authoredFrameIntervalSpanMilliseconds": 1617,
        "visualCharacterIds": character_ids,
        "namedEffects": names,
    }


def _audio_event(sound_ini: bytes) -> dict[str, Any]:
    marker = b"AudioEvent Gui_PalantirResourceBarFlash\r\n"
    starts: list[int] = []
    position = 0
    while True:
        position = sound_ini.find(marker, position)
        if position < 0:
            break
        starts.append(position)
        position += len(marker)
    if len(starts) != 1:
        raise ValueError("resource-flash AudioEvent identity changed")
    start = starts[0]
    end_marker = b"End\r\n"
    end = sound_ini.find(end_marker, start + len(marker))
    if end < 0:
        raise ValueError("resource-flash AudioEvent is unterminated")
    end += len(end_marker)
    block = sound_ini[start:end]
    if start != 494767 or end != 494926 or _sha(block) != AUDIO_BLOCK_SHA256:
        raise ValueError("resource-flash AudioEvent definition changed")
    expected = (
        marker
        + b"  ReverbEffectLevel = 0\r\n"
        + b"  Sounds = UCommandPoints\r\n"
        + b"  Volume = 50\r\n"
        + b"  Type = ui player\r\n"
        + b"  SubmixSlider = SoundFX\r\n"
        + end_marker
    )
    if block != expected:
        raise ValueError("resource-flash AudioEvent fields changed")
    return {
        "eventId": "Gui_PalantirResourceBarFlash",
        "definitionRange": [start, end],
        "definitionSha256": AUDIO_BLOCK_SHA256,
        "sounds": ["UCommandPoints"],
        "volume": 50,
        "type": ["ui", "player"],
        "submixSlider": "SoundFX",
        "reverbEffectLevel": 0,
    }


def _wave_contract(wave: bytes) -> dict[str, Any]:
    if wave[:4] != b"RIFF" or wave[8:12] != b"WAVE":
        raise ValueError("UCommandPoints is not RIFF/WAVE")
    if struct.unpack_from("<I", wave, 4)[0] + 8 != len(wave):
        raise ValueError("UCommandPoints RIFF size changed")
    chunks: dict[bytes, bytes] = {}
    order: list[str] = []
    offset = 12
    while offset + 8 <= len(wave):
        chunk_id = wave[offset : offset + 4]
        size = struct.unpack_from("<I", wave, offset + 4)[0]
        end = offset + 8 + size
        if end > len(wave) or chunk_id in chunks:
            raise ValueError("UCommandPoints WAVE chunks are invalid")
        chunks[chunk_id] = wave[offset + 8 : end]
        order.append(chunk_id.decode("ascii"))
        offset = end + (size & 1)
    if offset != len(wave) or order != ["fmt ", "fact", "data"]:
        raise ValueError("UCommandPoints WAVE chunk order changed")
    fmt = chunks[b"fmt "]
    if len(fmt) != 20 or len(chunks[b"fact"]) != 4 or len(chunks[b"data"]) != 96256:
        raise ValueError("UCommandPoints WAVE chunk sizes changed")
    fields = struct.unpack_from("<HHIIHHHH", fmt)
    if fields != (17, 2, 44100, 44251, 2048, 4, 2, 2041):
        raise ValueError("UCommandPoints IMA ADPCM format changed")
    sample_frames = struct.unpack("<I", chunks[b"fact"])[0]
    if sample_frames != 93951:
        raise ValueError("UCommandPoints decoded frame count changed")
    return {
        "logicalId": "UCommandPoints",
        "virtualPath": "data/audio/sounds/ucommandpoints.wav",
        "byteLength": len(wave),
        "sha256": WAVE_SHA256,
        "codec": "IMA ADPCM WAVE format 0x0011",
        "channels": 2,
        "sampleRate": 44100,
        "sampleFrames": sample_frames,
        "durationSeconds": sample_frames / 44100,
        "bitsPerSample": 4,
        "blockAlign": 2048,
        "samplesPerBlock": 2041,
        "dataByteLength": 96256,
    }


def _native_bridge(game_dat: bytes) -> dict[str, Any]:
    if _sha(game_dat) != GAME_DAT_SHA256:
        raise ValueError("BFME2 game.dat identity changed")
    ranges = []
    for label, (start, end, digest) in (
        ("handler", NATIVE_HANDLER),
        ("registration", NATIVE_REGISTRATION),
        ("apt-trigger", NATIVE_APT_TRIGGER),
    ):
        payload = game_dat[start - TEXT_DELTA : end - TEXT_DELTA]
        if len(payload) != end - start or _sha(payload) != digest:
            raise ValueError(f"native PlaySound {label} changed")
        ranges.append(
            {
                "kind": label,
                "startVa": f"0x{start:08x}",
                "endVa": f"0x{end:08x}",
                "byteLength": end - start,
                "sha256": digest,
            }
        )
    string_offset = 0x838CE8
    if game_dat[string_offset : string_offset + 10] != b"PlaySound\0":
        raise ValueError("native PlaySound registration string changed")
    registration = game_dat[
        NATIVE_REGISTRATION[0] - TEXT_DELTA : NATIVE_REGISTRATION[1] - TEXT_DELTA
    ]
    if b"\x68\xe8\x98\xc3\x00" not in registration or b"\x51\x2a\x81\x00" not in registration:
        raise ValueError("native PlaySound registration operands changed")
    apt_trigger_name_offset = 0x837328
    if game_dat[
        apt_trigger_name_offset : apt_trigger_name_offset + 23
    ] != b"PlayCommandPointEffect\0":
        raise ValueError("native PlayCommandPointEffect string changed")
    return {
        "fsCommand": "PlaySound",
        "registrationStringVa": "0x00c398e8",
        "handlerEntryVa": "0x00812a51",
        "ranges": ranges,
        "handlerOrder": [
            "if global 0x00dfe6e8 audio service is null, return",
            "copy the exact FSCommand parameter into an engine string",
            "lookup the AudioEvent through audio-service vslot 0x12c",
            "if the AudioEvent lookup returns null, return",
            "construct a playback request with exact mode value 2",
            "submit the request through audio-service vslot 0x64",
            "destroy temporary request and event references",
        ],
        "existingVoiceSuppressionInHandler": False,
        "typedAptTrigger": {
            "entryVa": "0x007fe9bb",
            "endVa": "0x007fe9da",
            "methodStringVa": "0x00c37f28",
            "method": "PlayCommandPointEffect",
            "semanticArguments": [],
            "aptAbiArgumentSlots": [0, 0, 0, 0, 0, 0],
            "rootPointer": "0x00dc1a0c",
            "dispatcher": "0x00622a8b",
            "exactCallers": ["0x006d48e3", "0x006d4a02"],
            "callerEvidence": "each caller invokes the stub immediately after a distinct native counter reaches zero",
            "counterSemanticAliases": "unresolved-stripped-native-state",
        },
    }


def build_contract(
    scene: Mapping[str, Any],
    effective_assets: Path | str,
    manifest: Mapping[str, Any],
    game_dat: Path | str,
) -> dict[str, Any]:
    """Build one exact, payload-free resource-flash semantics contract."""

    if scene.get("aggregateSha256") != SCENE_SHA256:
        raise ValueError("HUD scene contract identity changed")
    root = _private_root(effective_assets)
    payloads: dict[str, bytes] = {}
    for virtual_path, expected in ASSETS.items():
        _manifest_asset(manifest, virtual_path, expected)
        payloads[virtual_path] = _source(root, virtual_path, expected)
    apt = payloads["Palantir.apt"]
    constants = parse_apt_constants(payloads["Palantir.const"], "Palantir.const")
    apt_contract = parse_apt_movie(apt, constants, "Palantir.apt")
    if apt_contract["root"]["millisecondsPerFrame"] != 33:
        raise ValueError("Palantir frame interval changed")

    placement = _validate_placement(apt)
    trigger = _validate_trigger(scene, apt)
    script = _validate_flash_script(scene, apt)
    root_play_sound = _validate_root_play_sound(scene, apt)
    timeline = _validate_timeline(scene)
    audio_event = _audio_event(payloads["data/ini/soundeffects.ini"])
    wave = _wave_contract(payloads["data/audio/sounds/ucommandpoints.wav"])
    native = _native_bridge(Path(game_dat).read_bytes())

    result: dict[str, Any] = {
        "schema": SCHEMA,
        "source": {
            "sceneAggregateSha256": SCENE_SHA256,
            "palantirAptSha256": APT_SHA256,
            "palantirConstSha256": CONST_SHA256,
            "soundEffectsIniSha256": SOUND_INI_SHA256,
            "gameDatSha256": GAME_DAT_SHA256,
        },
        "summary": {
            "typedInputCount": 1,
            "visualTargetCount": 1,
            "audioEventCount": 1,
            "audioLeafCount": 1,
            "nativeHandlerExact": True,
            "visualReplayExact": True,
            "mixerOverlapTraceRequired": True,
            "implementationIncluded": False,
            "genericDispatchAllowed": False,
            "fallbackAllowed": False,
        },
        "typedInput": trigger,
        "placement": placement,
        "script": script,
        "visual": timeline,
        "audio": {"event": audio_event, "leaves": [wave]},
        "hostPath": {
            "rootMethod": root_play_sound,
            "native": native,
        },
        "ordering": [
            "PlayCommandPointEffect receives zero arguments",
            "CommandPointsFlash.gotoAndPlay('_go') selects frame 8 on the existing clip",
            "frame-8 action executes play before the audio call",
            "frame-8 action calls _root.PlaySound with Gui_PalantirResourceBarFlash",
            "initialized root emits FSCommand:PlaySound with that exact event ID",
            "native handler resolves the event before submitting playback mode 2",
        ],
        "replayAndOverlap": {
            "visual": "each call rewinds the one placed CommandPointsFlash instance to frame 8; no parallel visual instance is created",
            "audioDispatch": "each frame-8 execution emits one new native playback request; the handler contains no existing-voice suppression branch",
            "audioLeafDurationSeconds": wave["durationSeconds"],
            "authoredVisualIntervalSpanSeconds": 1.617,
            "relativeDuration": "the leaf is 2.130408 seconds; the authored frame-8-to-frame-57 sequence spans 49 nominal 33-ms intervals, without asserting runtime wall-clock timing",
            "downstreamMixerOverlap": "unresolved: voice stealing, coalescing, or simultaneous mixing may occur below the hash-pinned handler",
        },
        "unresolved": [
            "downstream audio-service/mixer behavior when the same event is retriggered before its 2.130408-second leaf completes",
            "semantic names of the two distinct native counters whose zero transitions call the exact PlayCommandPointEffect APT stub",
        ],
        "tracePlan": [
            "break on 0x00812a51 and 0x00812ac5",
            "invoke PlayCommandPointEffect twice less than 2.130408 seconds apart",
            "record both vslot-0x64 request objects, returned voice handles, and audible mixer result",
            "promote an overlap policy only after two identical BFME2 1.06 observations",
            "if native counter aliases are required, break at 0x006d48e3 and 0x006d4a02 and record owner state without naming it from one observation",
        ],
        "security": {
            "allowedInput": "Palantir.PlayCommandPointEffect() only",
            "allowedVisualCall": "CommandPointsFlash.gotoAndPlay('_go') only",
            "allowedFsCommand": "PlaySound only",
            "allowedAudioEvent": "Gui_PalantirResourceBarFlash only",
            "genericActionScriptVm": False,
            "guessedAudio": False,
        },
    }
    result["aggregateSha256"] = _sha(_canonical(result))
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scene-contract", type=Path, required=True)
    parser.add_argument("--effective-assets", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--game-dat", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    contract = build_contract(
        json.loads(args.scene_contract.read_text(encoding="utf-8")),
        args.effective_assets,
        json.loads(args.manifest.read_text(encoding="utf-8")),
        args.game_dat,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(_canonical(contract))
    print(json.dumps(contract["summary"], sort_keys=True))
    print(contract["aggregateSha256"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
