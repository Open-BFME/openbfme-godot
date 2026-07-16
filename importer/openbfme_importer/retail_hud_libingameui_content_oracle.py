"""Seal the concrete content closure requested by ``libingameui:37332``.

This payload-free static oracle pins the BFME2 1.06 Men-versus-Men Fords HUD
movie closure and executable.  It proves the authored and native contentType
producers, resolves them against exact APT exports, and emits a one-symbol
allowlist.  It neither converts assets nor adds runtime support.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path
from typing import Any, Iterable, Mapping

from .retail_hud_apt_convert import _Movie, _decode_action_program, _movie_from_plan
from .sage_apt import parse_apt_constants, parse_apt_dat, parse_apt_movie


SCHEMA = "openbfme.private-hud-libingameui-content-oracle"
SCHEMA_VERSION = 0

_MOVIES: dict[str, tuple[tuple[int, str], tuple[int, str], tuple[int, str]]] = {
    "InGameHelpBox": (
        (5_512, "520e5a1ff4aac288d7957a8c76818a3ceaff72b395167ccb660fa301447178e7"),
        (2_176, "2e6e635242e77d2bdd392001f7136c8dea61fa7a13e554850be7c702a97c71de"),
        (50, "892429fd2c0e9dc1305897fb9bf7ab41f629f1d39b4139afa8ca4f29212d18f1"),
    ),
    "InGameHeroSelect": (
        (174_324, "dc155d39f7b8dde5c2ca7ec09407918b3e914d61a5adc59a194e0a33268e3cbd"),
        (3_146, "46343633da353aa7fdcde60e6e2b61304ef2084324083dccf4c5e3dcbc433f93"),
        (178, "29e57bc7bf05b9b21970b10834c9493a0d9ddaee4a184538f50cfdf614c8a70b"),
    ),
    "InGamePlanningMode": (
        (29_998, "20003cc09ef9b209bdb4c25a0ec3da9842abbc41a7cb5229e69a7b3f4b01330e"),
        (1_633, "e180509285f59f53484bf28a5351d2b26047a4b4b3501660415318e74b766731"),
        (195, "d51ddc3707f8a47eb91d66dd1025b014515775a97701e7c24fceb8e043cf515d"),
    ),
    "InGameSideCommandBar": (
        (14_082, "84d58c67c5cab9a3bf690125cbf1a0cbf3f4bc58ccc29ffa33b992a924eca6ef"),
        (3_364, "5f21b405a8121edb689365441177b38b32386f101a1bb06418336dbac815975a"),
        (50, "892429fd2c0e9dc1305897fb9bf7ab41f629f1d39b4139afa8ca4f29212d18f1"),
    ),
    "InGameSpellBook": (
        (27_966, "24f82808dadd151ffed47284ee92800af18db22894cac4f2479e32b90913f1f4"),
        (2_406, "40fa111c2cc8bbd05e979ea9b8b5c7fce34654c9c8c8d2b1deaf0399368b1639"),
        (50, "892429fd2c0e9dc1305897fb9bf7ab41f629f1d39b4139afa8ca4f29212d18f1"),
    ),
    "libInGameImagesMain": (
        (9_068, "ad5bb65d3ae84a85934c931764c4ed2a24cefce4db1a996fdd73add388897d24"),
        (32, "b1fb2aca40af93325888ee9077825df275627f55cb1e1d29938e103290228703"),
        (414, "fbcb53e6acc3be69461fa8066743dcd179abde8cfee22899f30aa1ce9258da0f"),
    ),
    "libInGameUI": (
        (58_462, "305bdfabca3a815f8c373419978ca080a7f28561b2ca9d36eeeb7f35992ba392"),
        (2_876, "717a03669f47944f9933e829e8d5d1193e375cadbbdfc5804ee131631a7176cd"),
        (50, "892429fd2c0e9dc1305897fb9bf7ab41f629f1d39b4139afa8ca4f29212d18f1"),
    ),
    "Palantir": (
        (378_173, "c1f500847f0c77d4c6504edf79113b5723300165bebd42b4dafda479516f5140"),
        (10_260, "f07e24e3b70e286d491652cc827aef904a2ccabf54107d4f1bfc3030beee8fd9"),
        (586, "d8e8964711e4061b0643dd0dd3de1876b7326cee6d60e11214793b5d483f3ae4"),
    ),
    "PalantirExport": (
        (1_716, "2c35dc2671e316d6d2101b3d8790bea7f9f7b06a597abe6937862396f188391c"),
        (32, "708c329be95e34edd70c1a13a82ccc58f8bad534f86ecf2c268b51467dcb21bf"),
        (224, "6a45a2b1445b034f369fed28d2f29791abb82e026b0a32b45718788636433b4a"),
    ),
}
_EXCLUDED_MOVIES = {
    "StrategicHUD": (
        (19_115, "9b1bf4f832db1925ff3a4ee1eff49a2f11db87bf12d6394211f19c4f6570221a"),
        (2_465, "d575e2b1e8e542b620ee7bb58d20d6edfb7b1123b079245823579c101977e8c5"),
        (50, "892429fd2c0e9dc1305897fb9bf7ab41f629f1d39b4139afa8ca4f29212d18f1"),
    )
}
_ALL_MOVIES = {**_MOVIES, **_EXCLUDED_MOVIES}
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
    ("side-command-button-build", 0x009285EF, 329, "841ee50a38086d2a0818cffe65fa4baa7600b47d25a76986a6cde1c82c118f23"),
    ("palantir-command-button-build", 0x009294FC, 217, "ea4b9d28e5010a7b29abddb9dd0807085ec598dee2a85e8a8a0b8d79101b0bf8"),
    ("shared-command-consumer", 0x009C3697, 92, "46226d15d1b8c8e2aedfcc542431f9b80d718a81781d60305452bd5f7367a190"),
    ("command-content-constructor", 0x009C35A6, 119, "e1a030c84636de55e2c23b88e9097e97e82d031cf635118b5e944f427156a686"),
    ("command-content-factory", 0x009C802B, 74, "96ab43dc729a62a777f7e4dca26d0c59e3866785aed039680cd0f8fcb1533672"),
    ("command-content-adapter", 0x009C7D4A, 737, "0a7ee100c4d16c10be3562c5879c96cd7da6aa05089ef0995ba678d8f7aa097a"),
    ("generic-create-content-dispatch", 0x009C329B, 311, "7435c9003e600d83eac617fcf16fd8b1854098418d25d71720102363c9b1db83"),
)
_NATIVE_STRINGS = (
    ("CommandButton", 0x00BDA630, "4c98b6d7a45bed7413fdb64d426a86d1c8f21e0ebac6d92e61e24730bca8ed44"),
    ("Button", 0x00C6CF64, "038938c87b702cb0ab54ec8f2e47da93e131859faee153dc2787c22748f5826c"),
    ("StrategicCommandButton", 0x00C779D4, "573be0a3a4ae0942ab85391281ee1c7baa4fddbecaa7a8d5faca48d54b3843a2"),
    ("icon", 0x00C7A1B4, "a3458a02fe2c47ab7dbd0468240da989b3a8291829e530ce04e04feb3182ad0e"),
)


class HudLibInGameUiContentOracleError(ValueError):
    """Raised when retail content evidence differs from the sealed contract."""


def _sha(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def _private(path: Path | str, label: str) -> Path:
    resolved = Path(path).resolve()
    if ".private" not in {part.casefold() for part in resolved.parts}:
        raise HudLibInGameUiContentOracleError(f"{label} must remain under .private")
    return resolved


def _sources() -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for movie, identities in _ALL_MOVIES.items():
        for extension, (length, digest) in zip(("apt", "const", "dat"), identities, strict=True):
            result[f"{movie}.{extension}"] = {"byteLength": length, "sha256": digest}
    return result


_SOURCES = _sources()


def _read_sources(root: Path) -> dict[str, bytes]:
    output: dict[str, bytes] = {}
    for name, identity in _SOURCES.items():
        path = (root / name).resolve()
        try:
            path.relative_to(root)
        except ValueError as exc:
            raise HudLibInGameUiContentOracleError(f"source escaped private root: {name}") from exc
        if not path.is_file() or path.is_symlink():
            raise HudLibInGameUiContentOracleError(f"private HUD source is missing or linked: {name}")
        payload = path.read_bytes()
        if len(payload) != identity["byteLength"] or _sha(payload) != identity["sha256"]:
            raise HudLibInGameUiContentOracleError(f"private HUD source identity changed: {name}")
        output[name.casefold()] = payload
    return output


def _parse_movie(
    payloads: Mapping[str, bytes], name: str
) -> tuple[_Movie, dict[str, Any]]:
    names = tuple(f"{name}.{extension}" for extension in ("apt", "const", "dat"))
    data = []
    for source_name in names:
        payload = payloads.get(source_name.casefold())
        identity = _SOURCES[source_name]
        if payload is None or len(payload) != identity["byteLength"] or _sha(payload) != identity["sha256"]:
            raise HudLibInGameUiContentOracleError(f"private HUD source identity changed: {source_name}")
        data.append(payload)
    constants = parse_apt_constants(data[1], names[1])
    apt = parse_apt_movie(data[0], constants, names[0])
    image_map = parse_apt_dat(data[2], names[2])
    movie = _movie_from_plan(
        {"movie": name, "apt": apt, "constants": constants, "imageMap": image_map, "geometry": [], "atlases": []},
        source_bytes={names[index].casefold(): payload for index, payload in enumerate(data)},
    )
    return movie, apt


def _action_rows(movie: _Movie) -> Iterable[tuple[str, int, Mapping[str, Any]]]:
    for frame_index, rows in enumerate(movie.frames):
        for row in rows:
            if row.get("kind") == "action-script":
                yield "root", frame_index, row
    for character in movie.characters:
        for frame_index, rows in enumerate(character.get("frames", [])):
            for row in rows:
                if row.get("kind") == "action-script":
                    yield f"sprite:{character['characterId']}", frame_index, row


def _authored_producer(palantir: _Movie) -> dict[str, Any]:
    rows = [row for owner, frame, row in _action_rows(palantir) if (owner, frame, row.get("sourceOffset")) == ("root", 0, 95_872)]
    if len(rows) != 1:
        raise HudLibInGameUiContentOracleError("Palantir authored producer reachability changed")
    program = _decode_action_program(palantir, rows[0])
    if (
        program["instructionOffset"],
        program["byteLength"],
        program["sha256"],
        _sha(palantir.data[95_872:95_880]),
    ) != (
        365_080,
        235,
        "e8c094804c2834692b8ecc2914ca10029c691bbbca7c30ac88fdd02afd61cca7",
        "945960a9c9338943d35f5233689c5ccb4e545b7735eba2ccc429b988bc65d102",
    ):
        raise HudLibInGameUiContentOracleError("Palantir authored producer identity changed")
    instructions = {int(row["offset"]): row for row in program["instructions"]}
    pool = [row.get("value") for row in instructions[365_080].get("constants", [])]
    required = {0: "_global", 1: "InGame", 18: "CommandButtons", 19: "Bttn", 20: "CommandButton", 21: "0", 22: "CreateContent"}
    if any(pool[index] != value for index, value in required.items()):
        raise HudLibInGameUiContentOracleError("Palantir authored CreateContent constants changed")
    exact = (
        (365_092, "push-global-variable", None, None),
        (365_093, "get-named-member", 1, None),
        (365_095, "not", None, None),
        (365_096, "not", None, None),
        (365_097, "branch-if-true", 210, 365_314),
        (365_174, "push-constant-byte", 19, None),
        (365_176, "push-constant-byte", 20, None),
        (365_178, "push-byte", 2, None),
        (365_180, "push-value-of-variable", 3, None),
        (365_182, "get-named-member", 18, None),
        (365_184, "get-named-member", 21, None),
        (365_186, "call-named-method-pop", 22, None),
    )
    for offset, name, operand, target in exact:
        row = instructions.get(offset, {})
        if row.get("name") != name or row.get("operand") != operand or row.get("targetOffset") != target:
            raise HudLibInGameUiContentOracleError("Palantir authored CreateContent flow changed")
    return {
        "scriptId": "palantir:95872",
        "sourceOffset": 95_872,
        "instructionOffset": 365_080,
        "byteLength": 235,
        "sha256": program["sha256"],
        "recordSha256": "945960a9c9338943d35f5233689c5ccb4e545b7735eba2ccc429b988bc65d102",
        "call": "_root.CommandButtons['0'].CreateContent('CommandButton','Bttn')",
        "guard": "Boolean(_global.InGame)",
        "guardBranchTarget": 365_314,
        "classification": "test-only-authored-fallback-skipped-in-ingame-scene",
        "reachableInMenFords": False,
    }


def _export_contract(
    movies: Mapping[str, tuple[_Movie, dict[str, Any]]]
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    candidate_names = {"commandbutton", "strategiccommandbutton", "icon"}
    current = []
    for movie_name in _MOVIES:
        for row in movies[movie_name][1]["exports"]:
            if str(row["symbol"]).casefold() in candidate_names:
                current.append((movie_name, row))
    if current != [("libInGameUI", {"sourceIndex": 647, "symbol": "CommandButton", "characterId": 49})]:
        raise HudLibInGameUiContentOracleError("current content export allowlist changed")

    lib_movie, lib_apt = movies["libInGameUI"]
    lib_character = lib_apt["characters"][49]
    if (
        lib_apt["root"]["exportCount"] != 656
        or lib_character != {"characterId": 49, "kind": "sprite", "sourceOffset": 18_020, "frameCount": 76, "frameTableOffset": 33_928}
        or _sha(lib_movie.data[5_912:5_920]) != "1781bccfd68f6ab4a96a49b97b5ae596568ec44c9eeced147e8212b87619700e"
        or _sha(lib_movie.data[18_020:18_036]) != "f2bf1b6b03e4fb18fd13bdffdc6184aaec0b285772f73bca1eb359b44c0dbfbb"
    ):
        raise HudLibInGameUiContentOracleError("CommandButton export record changed")

    strategic_movie, strategic_apt = movies["StrategicHUD"]
    rows = [row for row in strategic_apt["exports"] if str(row["symbol"]).casefold() in candidate_names]
    if rows != [{"sourceIndex": 0, "symbol": "StrategicCommandButton", "characterId": 12}]:
        raise HudLibInGameUiContentOracleError("StrategicHUD comparison export changed")
    if (
        strategic_apt["root"]["exportCount"] != 1
        or strategic_apt["characters"][12].get("sourceOffset") != 612
        or strategic_apt["characters"][12].get("kind") != "sprite"
        or _sha(strategic_movie.data[104:112]) != "1ae299bd6af481514e72ba6316b7771f2ca2393e204a366920f6be889808163b"
        or _sha(strategic_movie.data[612:628]) != "abd528826aaf19cf1f3c0597a5fdfd26db4e4716ef1f1afdfcd959dde7533865"
    ):
        raise HudLibInGameUiContentOracleError("StrategicCommandButton export record changed")

    allowlist = [{
        "movie": "libInGameUI",
        "contentType": "CommandButton",
        "sourceIndex": 647,
        "characterId": 49,
        "kind": "sprite",
        "exportTableOffset": 736,
        "exportRecordOffset": 5_912,
        "exportRecordSha256": "1781bccfd68f6ab4a96a49b97b5ae596568ec44c9eeced147e8212b87619700e",
        "characterSourceOffset": 18_020,
        "characterHeaderSha256": "f2bf1b6b03e4fb18fd13bdffdc6184aaec0b285772f73bca1eb359b44c0dbfbb",
    }]
    rejected = [
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
    return allowlist, rejected


def _consumer_contract(movies: Mapping[str, tuple[_Movie, dict[str, Any]]]) -> list[dict[str, Any]]:
    imports = []
    for name in _MOVIES:
        for row in movies[name][1]["imports"]:
            if str(row["symbol"]).casefold() == "movieclipframe":
                imports.append((name, row["movie"], row["symbol"], row["characterId"]))
    if imports != [
        ("InGameSideCommandBar", "libInGameUI", "MovieClipFrame", 1),
        ("Palantir", "libInGameUI", "MovieClipFrame", 108),
    ]:
        raise HudLibInGameUiContentOracleError("MovieClipFrame consumer closure changed")

    side = movies["InGameSideCommandBar"][0]
    side_import = [row for row in side.characters[18]["frames"][0] if row.get("kind") == "place-object" and row.get("characterId") == 1]
    side_buttons = [row for row in side.characters[21]["frames"][0] if row.get("kind") == "place-object" and row.get("characterId") == 18]
    if (
        [(row.get("name"), row.get("sourceOffset")) for row in side_import] != [("Button", 6_296)]
        or [row.get("name") for row in side_buttons] != [f"Button{index}" for index in range(12)]
    ):
        raise HudLibInGameUiContentOracleError("side command consumer placement changed")

    palantir = movies["Palantir"][0]
    pal_places = [row for row in palantir.characters[114]["frames"][9] if row.get("kind") == "place-object" and row.get("characterId") == 108]
    if (
        [row.get("name") for row in pal_places] != ["1", "2", "3", "4", "5", "0"]
        or [row.get("sourceOffset") for row in pal_places] != [169_600, 169_664, 169_728, 169_792, 169_856, 169_920]
    ):
        raise HudLibInGameUiContentOracleError("Palantir command consumer placement changed")
    return [
        {
            "movie": "InGameSideCommandBar",
            "owner": "sprite:18",
            "importCharacterId": 1,
            "contentHostName": "Button",
            "instanceCount": 12,
            "instanceNames": [f"Button{index}" for index in range(12)],
            "reachability": "immediate-from-root-frame-0",
            "nativeCreateCallSite": "0x009286D2",
        },
        {
            "movie": "Palantir",
            "owner": "sprite:114",
            "importCharacterId": 108,
            "contentHostNames": ["1", "2", "3", "4", "5", "0"],
            "instanceCount": 6,
            "reachability": "when-CommandButtons-enters-_show-frame-9",
            "nativeCreateCallSite": "0x009295AE",
        },
    ]


def _pe_range(payload: bytes, virtual_address: int, length: int) -> bytes:
    for section_va, file_offset, section_length in _PE_SECTIONS:
        relative = virtual_address - section_va
        if 0 <= relative and relative + length <= section_length:
            return payload[file_offset + relative : file_offset + relative + length]
    raise HudLibInGameUiContentOracleError(f"native range is outside sealed PE sections: 0x{virtual_address:08X}")


def _direct_callers(payload: bytes, target: int) -> list[int]:
    callers: list[int] = []
    for section_va, file_offset, section_length in _PE_SECTIONS:
        section = payload[file_offset : file_offset + section_length]
        for index in range(len(section) - 4):
            if section[index] != 0xE8:
                continue
            caller = section_va + index
            destination = caller + 5 + struct.unpack_from("<i", section, index + 1)[0]
            if destination == target:
                callers.append(caller)
    return callers


def _native_contract(game_dat: bytes) -> dict[str, Any]:
    if len(game_dat) != _GAME_DAT["byteLength"] or _sha(game_dat) != _GAME_DAT["sha256"]:
        raise HudLibInGameUiContentOracleError("game.dat is not the pinned BFME2 1.06 executable")
    ranges = []
    for name, address, length, digest in _NATIVE_RANGES:
        if _sha(_pe_range(game_dat, address, length)) != digest:
            raise HudLibInGameUiContentOracleError(f"native evidence changed: {name}")
        ranges.append({"id": name, "virtualAddress": f"0x{address:08X}", "byteLength": length, "sha256": digest})
    strings = []
    for value, address, digest in _NATIVE_STRINGS:
        payload = _pe_range(game_dat, address, len(value) + 1)
        if payload != value.encode("ascii") + b"\0" or _sha(payload) != digest:
            raise HudLibInGameUiContentOracleError(f"native content string changed: {value}")
        strings.append({"value": value, "virtualAddress": f"0x{address:08X}", "sha256": digest})
    expected_callers = {
        0x009C3697: [0x009286D2, 0x009295AE, 0x00968544, 0x009685A6],
        0x009C35A6: [0x009C365E, 0x009C36D6],
        0x009C802B: [0x00967D0B, 0x009C35D2],
        0x009C7D4A: [0x009C805C],
        0x009C329B: [0x009C7DDC, 0x009E1402, 0x009FCBBB],
    }
    for target, callers in expected_callers.items():
        if _direct_callers(game_dat, target) != callers:
            raise HudLibInGameUiContentOracleError(f"native direct-call graph changed at 0x{target:08X}")
    producers = [
        {"contentType": "CommandButton", "callSite": "0x009C7DDC", "stringAddress": "0x00BDA630", "classification": "reachable-men-fords", "reachableInMenFords": True},
        {"contentType": "StrategicCommandButton", "callSite": "0x009E1402", "stringAddress": "0x00C779D4", "classification": "strategic-hud-only", "reachableInMenFords": False},
        {"contentType": "icon", "callSite": "0x009FCBBB", "stringAddress": "0x00C7A1B4", "classification": "unit-icon-path-not-imported-by-current-closure", "reachableInMenFords": False},
    ]
    return {
        "source": {"virtualPath": "game.dat", **_GAME_DAT},
        "ranges": ranges,
        "strings": strings,
        "genericCreateDispatch": "0x009C329B",
        "directCallGraph": {f"0x{target:08X}": [f"0x{caller:08X}" for caller in callers] for target, callers in expected_callers.items()},
        "producers": producers,
        "currentConsumerPath": ["0x009286D2 or 0x009295AE", "0x009C3697", "0x009C36D6", "0x009C35A6", "0x009C35D2", "0x009C802B", "0x009C805C", "0x009C7D4A", "0x009C7DDC", "0x009C329B"],
        "currentContentType": "CommandButton",
        "currentContentName": "Button",
    }


def build_contract_from_payloads(payloads: Mapping[str, bytes], game_dat: bytes) -> dict[str, Any]:
    """Build the deterministic payload-free content closure contract."""
    movies = {name: _parse_movie(payloads, name) for name in _ALL_MOVIES}
    authored = _authored_producer(movies["Palantir"][0])
    allowlist, rejected = _export_contract(movies)
    consumers = _consumer_contract(movies)
    native = _native_contract(bytes(game_dat))
    contract: dict[str, Any] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "sources": [{"virtualPath": name, **identity} for name, identity in _SOURCES.items()],
        "scope": {
            "scenario": "BFME2 1.06 Men-versus-Men Fords of Isen II in-game HUD",
            "currentMovies": list(_MOVIES),
            "excludedComparisonMovies": list(_EXCLUDED_MOVIES),
        },
        "authoredProducer": authored,
        "nativeRuntimeEvidence": native,
        "contentConsumers": consumers,
        "allowedExports": allowlist,
        "rejectedContentTypes": rejected,
        "undefinedExportBehavior": {
            "requestedCall": "attachMovie(contentType, contentName, 0)",
            "whenExportUndefined": [
                "this[contentName] remains undefined",
                "skip placeholder _x,_y,_width,_height copies",
                "skip extern[String(this)+'_ContentName'] registration",
                "render no concrete child",
            ],
            "policy": "preserve-retail-undefined-no-op; never-substitute-generic-art",
        },
        "implementationDecision": {
            "staticAllowlistComplete": True,
            "runtimeTraceRequired": False,
            "genericActionScriptVmRequired": False,
            "runtimeSupportIncluded": False,
            "profileBlockerRemovalAuthorized": False,
            "allowedContentTypes": ["CommandButton"],
            "integrationRule": "resolve only libInGameUI::CommandButton sourceIndex 647 character 49 for current Men/Fords CreateContent; every other exact string follows retail undefined/no-op",
            "remainingGate": "bind the converted libInGameUI export registry to instantiate character 49 and its converted timeline/visual closure at runtime",
            "remainingGateNeedsTrace": False,
        },
        "summary": {
            "currentMovieCount": len(_MOVIES),
            "excludedComparisonMovieCount": len(_EXCLUDED_MOVIES),
            "authoredProducerCount": 1,
            "nativeProducerCount": 3,
            "reachableNativeProducerCount": 1,
            "currentConsumerContextCount": 2,
            "allowedExportCount": 1,
            "rejectedContentTypeCount": 2,
            "runtimeTraceRequired": False,
            "runtimeSupportIncluded": False,
            "remainingIntegrationGateCount": 1,
        },
    }
    contract["aggregateSha256"] = _sha(_canonical(contract))
    return contract


def build_contract(asset_root: Path | str, game_dat: Path | str) -> dict[str, Any]:
    root = _private(asset_root, "asset root")
    if not root.is_dir():
        raise HudLibInGameUiContentOracleError("asset root is missing")
    game_path = Path(game_dat).resolve()
    if not game_path.is_file():
        raise HudLibInGameUiContentOracleError("game.dat is missing")
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
    write_contract(build_contract(args.asset_root, args.game_dat), args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
