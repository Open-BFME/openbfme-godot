"""Seal exact BFME2 Palantir external-movie attachment semantics.

This is a payload-free evidence oracle.  It does not convert a movie, execute
ActionScript, or add a generic movie loader.  It validates the already sealed
261-source HUD plan against retail APT bytecode, the external-movie and WND
oracles, and selected BFME2 1.06 ``game.dat`` x86 ranges.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any, Iterable, Mapping

from .retail_hud_apt_convert import _Movie, _decode_action_sequence, _movie_from_plan


SCHEMA = "openbfme.private-hud-external-attachment-oracle"
_PLAN_AGGREGATE = "d8850c6033b8ae3041e044246ab216550b6eabb1d8cce0397006f936066c36c4"
_EXTERNAL_AGGREGATE = "df584585a406e2532e187f960b8bead6f25158f00733d1b539285a4b22090cf5"
_WND_AGGREGATE = "ce561e9f625ad9879885bdde96def3b46f953eb0b5d5dfcb7be819199edec2e4"
_GAME_DAT_SHA256 = "f008b587570bad693981dc7218588c81d192a1e064b0f7f861539c51156a7640"
_TEXT_VA_FILE_DELTA = 0x400A00

_INITIAL_SETUP = {
    "programOffset": 363916,
    "programSha256": "b411252632a770c924a57cb2009cc09fb3d93c1c8a1f7a4e077459b84cfa9381",
    "functionOffset": 364685,
    "bodyOffset": 364716,
    "bodyByteLength": 317,
    "bodySha256": "55735eb6de14ebf8e03267e14bb52feea4ff51b6dca64d3bba731a450a8e74d6",
}

_TARGETS: tuple[dict[str, Any], ...] = (
    {
        "movieId": "InGameSpellBook",
        "swf": "InGameSpellBook.swf",
        "target": "SpellBookUI",
        "depth": 3,
        "translation": [0.0, 0.0],
        "placeOffset": 95944,
        "loadInstructionOffset": 364906,
        "sourceProgramOffset": 22016,
        "sourceProgramSha256": "bc9e19fd0ad7500926b31dba505f878012335ec804ec35707cfa733ecb5b943d",
        "labels": {"_hide": 0, "_show": 9},
        "initialStopFrame": 8,
        "visibility": {
            "sourceDefault": "hidden",
            "normalMenVsMen": "load-active but visually dormant until the spell-book host path invokes the authored show timeline",
            "proof": "root frames 0..8 place no display characters and stop at frame 8; the InGame guard skips the frame-0 test-only _show branch",
        },
        "loaded": "FSCommand:OnAptInGameSpellBookLoaded(GetFullName(this))",
        "unloaded": "source onUnload -> FSCommand:OnAptInGameSpellBookUnloaded(GetFullName(this))",
        "godotInterface": {
            "attach": "attach_spell_book(slot: RetailHudMovieSlot, movie: RetailHudMovie) -> void",
            "visibility": "set_spell_book_open(open: bool) -> void",
            "lifecycle": "spell_book_loaded(full_name: String); spell_book_unloaded(full_name: String)",
            "timelineCalls": [
                "SetState(state)",
                "SetButtonState(index, state)",
                "SetButtonFlashEffectState(index, state)",
            ],
        },
    },
    {
        "movieId": "InGameHelpBox",
        "swf": "InGameHelpBox.swf",
        "target": "helpBox",
        "depth": 176,
        "translation": [585.0, 607.0],
        "placeOffset": 97480,
        "loadInstructionOffset": 364938,
        "sourceProgramOffset": 3636,
        "sourceProgramSha256": "71664be06717e6e52ee407d44bda48934a427601d7419d03f26ca75be7eda502",
        "labels": {},
        "initialStopFrame": 0,
        "visibility": {
            "sourceDefault": "hidden",
            "normalMenVsMen": "load-active but visually dormant until help content calls Show(height)",
            "proof": "the one-frame root places box, whose sprite frame 0 is label _hide and executes stop",
        },
        "loaded": "_parent.OnHelpBoxMovieLoaded(this) -> GameCode(OnHelpBoxLoaded, clip.toString())",
        "unloaded": "source onUnload -> _parent.OnHelpBoxMovieUnloaded(this) -> GameCode(OnHelpBoxUnloaded, clip.toString())",
        "godotInterface": {
            "attach": "attach_help_box(slot: RetailHudMovieSlot, movie: RetailHudMovie, alternate_anchor: Vector2) -> void",
            "visibility": "show_help(height: float); hide_help()",
            "placement": "move_help(use_alternate_location: bool) -> void",
            "lifecycle": "help_box_loaded(full_name: String); help_box_unloaded(full_name: String)",
        },
    },
    {
        "movieId": "InGameHeroSelect",
        "swf": "InGameHeroSelect.swf",
        "target": "HeroSelectUI",
        "depth": 174,
        "translation": [375.0, 700.0],
        "placeOffset": 97416,
        "loadInstructionOffset": 364954,
        "sourceProgramOffset": 167740,
        "sourceProgramSha256": "2c43ab2db3b3f9158706bc28154ed4362c9e6fd237bcc218f9e1100621190b1f",
        "labels": {"_hide": 0, "_fadein": 9, "_show": 19},
        "initialStopFrame": 8,
        "visibility": {
            "sourceDefault": "hidden",
            "normalMenVsMen": "runtime trace required: InitialSetup unconditionally calls ShowHeroSelectInterface after issuing all five loads, but no body for that method exists in the sealed APT/CONST closure or game.dat string table",
            "proof": "root frames 0..8 are empty and stop at frame 8; Show() enters _fadein at frame 9 and the shown timeline stops at frame 28",
        },
        "loaded": "_parent.OnHeroSelectMovieLoaded(this) -> install clip.onUnload -> GameCode(OnHeroSelectLoaded, clip.toString())",
        "unloaded": "parent-installed clip.onUnload -> GameCode(OnHeroSelectUnloaded, clip.toString())",
        "godotInterface": {
            "attach": "attach_hero_select(slot: RetailHudMovieSlot, movie: RetailHudMovie) -> void",
            "visibility": "set_hero_select_open(open: bool) -> void",
            "lifecycle": "hero_select_loaded(full_name: String); hero_select_unloaded(full_name: String)",
            "timelineCalls": [
                "SetButtonState(index, state)",
                "SetButtonHealthBar(index, value)",
                "SetButtonRankProgress(index, value)",
            ],
        },
    },
    {
        "movieId": "InGamePlanningMode",
        "swf": "InGamePlanningMode.swf",
        "target": "planningModeUI",
        "depth": 180,
        "translation": [512.0, 30.0],
        "placeOffset": 97608,
        "loadInstructionOffset": 364970,
        "sourceProgramOffset": 28244,
        "sourceProgramSha256": "6a660a1b59561308ac86226bcbf83f347e87b82e894aeb383b60be2e79d74e97",
        "labels": {"_init": 0, "_open": 9, "_close": 19},
        "initialStopFrame": 8,
        "visibility": {
            "sourceDefault": "closed",
            "normalMenVsMen": "load-active but visually dormant until planning mode calls Open()",
            "proof": "root frames 0..8 place no display characters and stop at frame 8; the InGame guard skips the test-only Open call",
        },
        "loaded": "_parent.OnPlanningModeUILoaded(this) -> GameCode(OnPlanningModeUILoaded, clip.toString())",
        "unloaded": "source onUnload -> _parent.OnPlanningModeUIUnloaded(this) -> GameCode(OnPlanningModeUIUnloaded, clip.toString())",
        "godotInterface": {
            "attach": "attach_planning_mode(slot: RetailHudMovieSlot, movie: RetailHudMovie) -> void",
            "visibility": "set_planning_mode_open(open: bool) -> void",
            "lifecycle": "planning_mode_ui_loaded(full_name: String); planning_mode_ui_unloaded(full_name: String)",
            "timelineSignals": ["planning_mode_opened", "planning_mode_closed"],
        },
    },
)

_NATIVE_RANGES: tuple[dict[str, Any], ...] = (
    {
        "name": "spell-book-loaded-wrapper",
        "entryVa": 0x92AC3C,
        "endVa": 0x92AD30,
        "sha256": "2c1ee9fea4d06a3ccbdc96248019b7868034d1e087418215d2e840193a919c71",
        "effect": "registered OnAptInGameSpellBookLoaded handler",
    },
    {
        "name": "spell-book-unloaded-wrapper",
        "entryVa": 0x92A7FC,
        "endVa": 0x92A804,
        "sha256": "94c11a7ac8650215af07d2f18f9efba10fa25dd1525f790772efaad7481a9618",
        "effect": "registered OnAptInGameSpellBookUnloaded handler; delegates to 0x0092a66a",
    },
    {
        "name": "spell-book-unloaded-body",
        "entryVa": 0x92A66A,
        "endVa": 0x92A786,
        "sha256": "15fbe84ba3ef6c37e2cfd1c8f96353bb525fe8aa4ec387dfeeb95934fc54d665",
        "effect": "delegated spell-book unload bookkeeping",
    },
    {
        "name": "help-box-loaded",
        "entryVa": 0x6D3EEA,
        "endVa": 0x6D3F7C,
        "sha256": "933b8177be9f4c2b29cd233cf6df73b291efeade7a58db24153dcbba87af9a73",
        "effect": "construct and retain the loaded help-box handle in Palantir owner slot +0xc8",
    },
    {
        "name": "help-box-unloaded",
        "entryVa": 0x6D3F7C,
        "endVa": 0x6D3F8A,
        "sha256": "1a93a44122bfd62d57e403d9b8989810f12ecd627b12b13586272d6a75d3b876",
        "effect": "clear Palantir owner slot +0xc8",
    },
    {
        "name": "hero-select-loaded",
        "entryVa": 0x6D3F8A,
        "endVa": 0x6D402A,
        "sha256": "b83a6871c22c61ec4b1a0dab12f10d86ef5129b3c25097b545ea64deaf6d3a8d",
        "effect": "construct and retain the loaded hero-select handle in Palantir owner slot +0xc4",
    },
    {
        "name": "hero-select-unloaded",
        "entryVa": 0x6D402A,
        "endVa": 0x6D4038,
        "sha256": "c543da81ac0e5de4433aee1e6255c6c40eeb033f296d61894ed8d0b98201fee1",
        "effect": "clear Palantir owner slot +0xc4",
    },
    {
        "name": "planning-mode-loaded",
        "entryVa": 0x6D4038,
        "endVa": 0x6D40CA,
        "sha256": "1f490a0dc4ddc56f0c9227f45fdb5ad9bb88adf0ac3679d66b07458ded615a46",
        "effect": "construct and retain the loaded planning-mode handle in Palantir owner slot +0xcc",
    },
    {
        "name": "planning-mode-unloaded",
        "entryVa": 0x6D40CA,
        "endVa": 0x6D40D8,
        "sha256": "048d6a15b0ed6a73fe1acc321247e5e15ac78afa87751f5bcb3f8a640531e180",
        "effect": "clear Palantir owner slot +0xcc",
    },
    {
        "name": "palantir-native-reset",
        "entryVa": 0x6D40F4,
        "endVa": 0x6D4240,
        "sha256": "caa92439a63eac781e297a16ace1e3f48e79abe8750f6b3b1e8a5637d6a61587",
        "effect": "clear retained handles in exact HeroSelect(+0xc4), HelpBox(+0xc8), PlanningMode(+0xcc) order, then reset other Palantir state",
    },
)


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _canonical(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def _require_contract(raw: Mapping[str, Any], schema: str, aggregate: str) -> None:
    if raw.get("schema") != schema or raw.get("aggregateSha256") != aggregate:
        raise ValueError(f"sealed contract changed: {schema}")


def _program(
    movie: _Movie, offset: int, expected_sha256: str
) -> tuple[list[dict[str, Any]], int]:
    instructions, end = _decode_action_sequence(movie, offset)
    if _sha(movie.data[offset:end]) != expected_sha256:
        raise ValueError(f"{movie.name} ActionScript changed at {offset}")
    return instructions, end


def _walk(instructions: Iterable[Mapping[str, Any]]) -> Iterable[Mapping[str, Any]]:
    for row in instructions:
        yield row
        body = row.get("body")
        if isinstance(body, list):
            yield from _walk(body)


def _function(
    instructions: Iterable[Mapping[str, Any]], name: str
) -> Mapping[str, Any]:
    rows = [row for row in _walk(instructions) if row.get("functionName") == name]
    if len(rows) != 1:
        raise ValueError(f"expected one ActionScript function: {name}")
    return rows[0]


def _movie_map(plan: Mapping[str, Any], asset_root: Path) -> dict[str, _Movie]:
    scene = plan.get("sceneContract")
    if not isinstance(scene, Mapping) or not isinstance(scene.get("movies"), list):
        raise ValueError("261-source scene contract changed")
    result: dict[str, _Movie] = {}
    for raw in scene["movies"]:
        if not isinstance(raw, Mapping):
            raise ValueError("movie contract changed")
        name = str(raw.get("movie"))
        result[name] = _movie_from_plan(raw, asset_root)
    return result


def _label_map(movie: _Movie) -> dict[str, int]:
    result: dict[str, int] = {}
    for frame_index, frame in enumerate(movie.frames):
        for item in frame:
            if item.get("kind") == "frame-label":
                result[str(item["name"])] = frame_index
    return result


def _find_place(movie: _Movie, name: str) -> Mapping[str, Any]:
    rows = [
        row
        for row in movie.frames[0]
        if row.get("kind") == "place-object" and row.get("name") == name
    ]
    if len(rows) != 1:
        raise ValueError(f"Palantir target placement changed: {name}")
    return rows[0]


def _validate_hidden_entry(movie: _Movie, initial_stop_frame: int) -> None:
    if movie.name == "InGameHelpBox":
        root_places = [
            row for row in movie.frames[0] if row.get("kind") == "place-object"
        ]
        if len(root_places) != 1 or root_places[0].get("name") != "box":
            raise ValueError("HelpBox root entry changed")
        box = movie.characters[int(root_places[0]["characterId"])]
        if box.get("kind") != "sprite":
            raise ValueError("HelpBox box character changed")
        labels = {
            str(row["name"]): frame_id
            for frame_id, frame in enumerate(box["frames"])
            for row in frame
            if row.get("kind") == "frame-label"
        }
        actions = [
            row for row in box["frames"][0] if row.get("kind") == "action-script"
        ]
        if labels.get("_hide") != 0 or len(actions) != 1:
            raise ValueError("HelpBox hidden entry changed")
        instructions, end = _decode_action_sequence(
            movie, int(actions[0]["instructionsOffset"])
        )
        if [row["name"] for row in instructions] != ["stop", "end"] or _sha(
            movie.data[int(actions[0]["instructionsOffset"]) : end]
        ) != "0a6361b3a802f55cd5ae06101c88a1e216320fe11cc0cfe1d791eed08a1200fd":
            raise ValueError("HelpBox hidden stop changed")
        return

    for frame in movie.frames[: initial_stop_frame + 1]:
        if any(row.get("kind") == "place-object" for row in frame):
            raise ValueError(f"{movie.name} hidden entry acquired display content")
    actions = [
        row
        for row in movie.frames[initial_stop_frame]
        if row.get("kind") == "action-script"
    ]
    if len(actions) != 1:
        raise ValueError(f"{movie.name} hidden stop changed")
    instructions, end = _decode_action_sequence(
        movie, int(actions[0]["instructionsOffset"])
    )
    if [row["name"] for row in instructions] != ["stop", "end"] or _sha(
        movie.data[int(actions[0]["instructionsOffset"]) : end]
    ) != "0a6361b3a802f55cd5ae06101c88a1e216320fe11cc0cfe1d791eed08a1200fd":
        raise ValueError(f"{movie.name} hidden stop program changed")


def _native_evidence(game_dat: Path) -> list[dict[str, Any]]:
    data = game_dat.read_bytes()
    if _sha(data) != _GAME_DAT_SHA256:
        raise ValueError("BFME2 game.dat identity changed")
    result: list[dict[str, Any]] = []
    for expected in _NATIVE_RANGES:
        start = int(expected["entryVa"]) - _TEXT_VA_FILE_DELTA
        end = int(expected["endVa"]) - _TEXT_VA_FILE_DELTA
        payload = data[start:end]
        if _sha(payload) != expected["sha256"]:
            raise ValueError(f"retail x86 changed: {expected['name']}")
        result.append(
            {
                **expected,
                "entryVa": f"0x{int(expected['entryVa']):08x}",
                "endVa": f"0x{int(expected['endVa']):08x}",
                "byteLength": len(payload),
            }
        )
    return result


def _hero_flagged_null(movie: _Movie) -> dict[str, Any]:
    rows: list[tuple[int, int, Mapping[str, Any]]] = []
    for character_id, character in enumerate(movie.characters):
        if character.get("kind") != "sprite":
            continue
        for frame_id, frame in enumerate(character["frames"]):
            for item in frame:
                if item.get("clipActionsPointerState") == "source-flagged-null":
                    rows.append((character_id, frame_id, item))
    if len(rows) != 1:
        raise ValueError("HeroSelect flagged-null record count changed")
    character_id, frame_id, row = rows[0]
    expected = (
        98,
        7,
        166756,
        0xB6,
        "SelectedHighlight",
        "7cf6432cbd91629acd5252c69aa957a08cadffd61214ae49ed0e078dec99a135",
    )
    actual = (
        character_id,
        frame_id,
        row.get("sourceOffset"),
        row.get("flags"),
        row.get("name"),
        row.get("clipActionsRecordSha256"),
    )
    if actual != expected:
        raise ValueError("HeroSelect flagged-null identity changed")
    return {
        "movieId": movie.name,
        "owner": "sprite:98",
        "frameId": 7,
        "sourceOffset": 166756,
        "flags": "0xb6",
        "name": "SelectedHighlight",
        "pointerState": "source-flagged-null",
        "recordSha256": expected[-1],
        "rule": "preserve the null pointer as authored; do not synthesize initialize, unload, or generic clip-action dispatch",
    }


def build_contract(
    asset_root: Path | str,
    plan: Mapping[str, Any],
    external_movies: Mapping[str, Any],
    wnd_activation: Mapping[str, Any],
    game_dat: Path | str,
) -> dict[str, Any]:
    if (
        plan.get("schema") != "openbfme.retail-hud-apt-plan"
        or plan.get("aggregateSha256") != _PLAN_AGGREGATE
    ):
        raise ValueError("sealed 261-source HUD plan changed")
    _require_contract(
        external_movies,
        "openbfme.private-hud-external-movies-oracle",
        _EXTERNAL_AGGREGATE,
    )
    _require_contract(
        wnd_activation, "openbfme.private-hud-wnd-activation-oracle", _WND_AGGREGATE
    )
    if (
        wnd_activation.get("summary", {}).get("decision")
        != "active-companion-not-candidate-dead"
    ):
        raise ValueError("WND companion activation decision changed")

    root = Path(asset_root)
    movies = _movie_map(plan, root)
    required = {"Palantir", *(str(row["movieId"]) for row in _TARGETS)}
    if not required.issubset(movies):
        raise ValueError("attachment movie closure changed")
    palantir = movies["Palantir"]

    initial, _ = _program(
        palantir, _INITIAL_SETUP["programOffset"], _INITIAL_SETUP["programSha256"]
    )
    initial_setup = _function(initial, "InitialSetup")
    if (
        initial_setup.get("offset") != _INITIAL_SETUP["functionOffset"]
        or initial_setup.get("bodyByteLength") != _INITIAL_SETUP["bodyByteLength"]
    ):
        raise ValueError("Palantir InitialSetup function changed")
    body_offset = int(initial_setup["body"][0]["offset"])
    body_end = body_offset + int(initial_setup["bodyByteLength"])
    if (
        body_offset != _INITIAL_SETUP["bodyOffset"]
        or _sha(palantir.data[body_offset:body_end]) != _INITIAL_SETUP["bodySha256"]
    ):
        raise ValueError("Palantir InitialSetup body changed")
    get_url_offsets = [
        int(row["offset"])
        for row in initial_setup["body"]
        if row.get("name") == "get-url2"
    ]
    expected_load_offsets = [364906, 364922, 364938, 364954, 364970]
    if get_url_offsets != expected_load_offsets:
        raise ValueError("Palantir external load order changed")

    external_loads = external_movies.get("movieLoads")
    if not isinstance(external_loads, list):
        raise ValueError("external movie load contract changed")
    external_order = [str(row.get("movieId")) for row in external_loads]
    if external_order != [
        "InGameSpellBook",
        "InGameSideCommandBar",
        "InGameHelpBox",
        "InGameHeroSelect",
        "InGamePlanningMode",
    ]:
        raise ValueError("external movie oracle load order changed")
    external_targets = {
        str(row.get("movieId")): str(row.get("target")) for row in external_loads
    }
    if any(
        external_targets.get(str(row["movieId"])) != row["target"] for row in _TARGETS
    ):
        raise ValueError("external movie oracle target mapping changed")

    target_rows: list[dict[str, Any]] = []
    for order, expected in enumerate(_TARGETS):
        movie = movies[str(expected["movieId"])]
        place = _find_place(palantir, str(expected["target"]))
        actual_place = (
            place.get("sourceOffset"),
            place.get("depth"),
            place.get("characterId"),
            place.get("flags"),
            place.get("translation"),
        )
        expected_place = (
            expected["placeOffset"],
            expected["depth"],
            41,
            0x26,
            expected["translation"],
        )
        if actual_place != expected_place:
            raise ValueError(f"Palantir target changed: {expected['target']}")
        _program(
            movie,
            int(expected["sourceProgramOffset"]),
            str(expected["sourceProgramSha256"]),
        )
        if _label_map(movie) != expected["labels"]:
            raise ValueError(f"source root labels changed: {movie.name}")
        _validate_hidden_entry(movie, int(expected["initialStopFrame"]))
        target_rows.append(
            {
                "loadOrderAmongBlockedTargets": order,
                "loadInstructionOffset": expected["loadInstructionOffset"],
                "movieId": expected["movieId"],
                "swf": expected["swf"],
                "targetScope": f"Palantir.root.frame0/{expected['target']}",
                "attachmentKind": "replace-content-of-authored-empty-child-clip",
                "placeholder": {
                    "sourceOffset": place["sourceOffset"],
                    "recordSha256": _sha(
                        palantir.data[
                            int(place["sourceOffset"]) : int(place["sourceOffset"]) + 60
                        ]
                    ),
                    "characterId": 41,
                    "characterProof": "one-frame empty sprite with frames=[[]]",
                    "depth": place["depth"],
                    "matrix": place["matrix"],
                    "translation": place["translation"],
                    "tint": place["tint"],
                    "additive": place["additive"],
                },
                "sourceRoot": {
                    "frameCount": len(movie.frames),
                    "entryFrame": 0,
                    "labels": expected["labels"],
                    "initialStopFrame": expected["initialStopFrame"],
                    "programOffset": expected["sourceProgramOffset"],
                    "programSha256": expected["sourceProgramSha256"],
                },
                "visibility": expected["visibility"],
                "lifecycle": {
                    "loaded": expected["loaded"],
                    "unloaded": expected["unloaded"],
                },
                "godotInterfaceProposal": expected["godotInterface"],
                "genericMovieRootAllowed": False,
                "genericVmRequired": False,
            }
        )

    empty_character = palantir.characters[41]
    if empty_character.get("kind") != "sprite" or empty_character.get("frames") != [[]]:
        raise ValueError("Palantir empty attachment character changed")

    method_name = b"ShowHeroSelectInterface"
    if any(method_name in movie.data for movie in movies.values()):
        raise ValueError("ShowHeroSelectInterface unexpectedly acquired an APT body")
    game_dat_data = Path(game_dat).read_bytes()
    if method_name in game_dat_data:
        raise ValueError(
            "ShowHeroSelectInterface unexpectedly acquired a game.dat name"
        )

    wnd_path = root / "window" / "controlbar.wnd"
    wnd_data = wnd_path.read_bytes()
    if (
        _sha(wnd_data)
        != "a509730457224a111af8022df6d0ef373fcaa5d91a102bc15bccf5fc1a54ced6"
    ):
        raise ValueError("ControlBar.wnd identity changed")
    target_names = [str(row["target"]) for row in _TARGETS]
    if any(name.encode() in wnd_data for name in target_names):
        raise ValueError("WND unexpectedly acquired an APT attachment target")

    result: dict[str, Any] = {
        "schema": SCHEMA,
        "source": {
            "planAggregateSha256": _PLAN_AGGREGATE,
            "externalMoviesAggregateSha256": _EXTERNAL_AGGREGATE,
            "wndActivationAggregateSha256": _WND_AGGREGATE,
            "gameDatSha256": _GAME_DAT_SHA256,
            "wndSha256": _sha(wnd_data),
        },
        "summary": {
            "blockedTargetCount": 4,
            "exactAttachmentCount": 4,
            "genericVmRequiredCount": 0,
            "independentRootAllowedCount": 0,
            "sourceDefaultHiddenOrClosedCount": 4,
            "normalMenVsMenStaticallyDormantCount": 3,
            "normalMenVsMenVisibilityTraceCount": 1,
            "implementationIncluded": False,
        },
        "attachmentOrder": {
            "fullInitialSetupOrder": external_order,
            "blockedTargetOrder": [str(row["movieId"]) for row in _TARGETS],
            "sameInitialSetupInvocation": True,
            "completionOrder": "unresolved: getURL2 issues all loads without an authored wait; callback completion order requires an APT engine trace",
            "postLoadInstructions": [
                "PalantirFrame.gotoAndPlay(_good)",
                "ShowHeroSelectInterface()",
                "InitGlobeUIs()",
                "Initialized=true",
                "GameCode(OnInitialized)",
            ],
        },
        "targets": target_rows,
        "heroSelectFlaggedNull": _hero_flagged_null(movies["InGameHeroSelect"]),
        "wndCompanion": {
            "decision": "active semantic/control companion beside Apt/Palantir",
            "attachmentAuthority": "none for these four target names; exact target strings are absent from ControlBar.wnd",
            "targetNamesAbsent": target_names,
            "retainWndCallbacks": True,
        },
        "nativeLifecycle": {
            "code": _native_evidence(Path(game_dat)),
            "retainedSlots": {
                "HeroSelectUI": "+0xc4",
                "helpBox": "+0xc8",
                "planningModeUI": "+0xcc",
            },
            "resetClearOrder": ["HeroSelectUI", "helpBox", "planningModeUI"],
            "spellBookPath": "separate OnAptInGameSpellBookLoaded/Unloaded FSCommand registration; no proven relative reset order with the three AptPalantir retained slots",
        },
        "unresolvedRuntimeTraces": [
            {
                "id": "apt-load-completion-order",
                "smallestTrace": "break on the four source loaded callbacks during Palantir InitialSetup and record callback order plus target full names",
                "blocks": "callback-completion ordering only; attachment scope and issue order are already exact",
            },
            {
                "id": "hero-select-initial-visibility",
                "smallestTrace": "break immediately before and after ShowHeroSelectInterface during one normal Men-v-Men start and record HeroSelectUI frame/visibility",
                "blocks": "whether HeroSelectUI remains dormant after its hidden source entry",
            },
            {
                "id": "palantir-target-removal-order",
                "smallestTrace": "record the four target unload callbacks and child removal events during one normal match teardown",
                "blocks": "SpellBook relative teardown order and whether APT child removal precedes or follows native handle clearing",
            },
            {
                "id": "help-box-alt-anchor-runtime-value",
                "smallestTrace": "record clip._x/_y and altHelpBoxLocation._x/_y in OnHelpBoxMovieLoaded",
                "blocks": "runtime numeric altLocationX/altLocationY only; the subtraction formula is statically exact",
            },
        ],
        "implementationRule": "bind four typed RetailHudMovieSlot children at the authored Palantir paths; do not create independent roots, a generic ActionScript VM, guessed frames, or synthetic lifecycle events",
    }
    result["aggregateSha256"] = _sha(_canonical(result))
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--asset-root", type=Path, required=True)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--external-movies", type=Path, required=True)
    parser.add_argument("--wnd-activation", type=Path, required=True)
    parser.add_argument("--game-dat", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    result = build_contract(
        args.asset_root,
        json.loads(args.plan.read_text(encoding="utf-8")),
        json.loads(args.external_movies.read_text(encoding="utf-8")),
        json.loads(args.wnd_activation.read_text(encoding="utf-8")),
        args.game_dat,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(_canonical(result))
    print(
        json.dumps(
            {"aggregateSha256": result["aggregateSha256"], "output": str(args.output)},
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
