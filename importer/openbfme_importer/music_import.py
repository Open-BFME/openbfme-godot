"""Per-faction music: extract the authored RotWK music binding into a pack.

WHY THIS MODULE EXISTS
======================
Retail BFME2/RotWK does not put per-faction music in ``playertemplate.ini`` -
every playable template there declares the same ``LoadScreenMusic``. The
authored binding lives in two places, and both are read here:

1. ``data/ini/music.ini`` declares ``MusicTrack`` leaves (id -> mp3 filename +
   volume + control flags) and ``Multisound`` playlists that group them
   (``BaseBuildingMannishMusic``, ``ExploreMordorMusic``, ``ActionGoodMusic``,
   ...).  The file's own comment says it plainly: "These are the actual
   'tracks' played by the music scripting system."

2. ``data/ini/audiosettings.ini`` names the global music state machine -
   ``MusicScriptLibraryName = "Libraries\\Music_MusicScripts_Single\\
   Music_MusicScripts_Single.map"`` - and THAT map holds the scripts that pick
   which multisound a player hears.  ``___MusicScript_Test<Faction>`` compares
   ``SKIRMISH_PLAYER_FACTION`` for the ``<Local Player>`` against a
   PlayerTemplate ``Side`` token and stashes the answer in the counter
   ``___MusicScript_PlayerFaction``; the ``___MusicScript_Do*`` scripts gate on
   that counter plus an ``InPhase*`` flag and play one multisound.

So the faction -> playlist table is *derived from the retail scripts*, never
guessed from names.  Adding a faction (Angmar, in RotWK) or re-pointing one is
a data change in the retail library, and this extractor follows it: RotWK's own
``___MusicScript_TestAngmar`` sets the same counter value as Mordor (5), which
is why Angmar authentically shares Mordor's in-game music.

WHAT IS *NOT* SCRIPT-BOUND
==========================
The victory/defeat screen leaves (``VictoryScreenGood`` / ``VictoryScreenEvil``
/ ``VictoryScreenAngmar`` / ``LoseScreenGood`` / ``LoseScreenEvil``) are plain
``MusicTrack`` declarations that the score screen selects in engine code, not
in the music-script map.  They are bound here by their authored
alignment/faction suffix and every such row is stamped
``"derivation": "name-bound"`` so a consumer can tell the difference from the
``"script-bound"`` rows.  Shell music is read from ``miscaudio.ini``'s
``MiscAudio`` block, which *is* an explicit authored declaration.
"""

from __future__ import annotations

import re
from collections.abc import Mapping
from typing import Any

from .sage_audio import parse_sage_audio_definitions

SCHEMA = "openbfme.music"
SCHEMA_VERSION = 0

MUSIC_INI_PATH = "data/ini/music.ini"
MISC_AUDIO_INI_PATH = "data/ini/miscaudio.ini"
AUDIO_SETTINGS_INI_PATH = "data/ini/audiosettings.ini"
MUSIC_SCRIPT_LIBRARY_PATH = (
    "libraries/music_musicscripts_single/music_musicscripts_single.map"
)

#: Retail track files live here; the pack mirrors them under ``assets/``.
RETAIL_TRACK_DIR = "data/audio/tracks"
PACK_TRACK_DIR = "assets/audio/music/tracks"

#: ``___MusicScript_InPhase<X>`` flag suffix -> the phase name this document
#: uses.  The three phases are the retail level-0 ("ambient") ladder.
PHASE_BY_FLAG = {
    "___MusicScript_InPhaseBaseBuilding": "basebuilding",
    "___MusicScript_InPhaseExplore": "explore",
    "___MusicScript_InPhaseExplore2": "explore2",
}

#: The counter every faction test writes, and the flag that records alignment.
FACTION_COUNTER = "___MusicScript_PlayerFaction"
GOOD_FACTION_FLAG = "___MusicScript_IsGoodFaction"

#: MiscAudio keys this document carries forward.  Shell music is what the menu
#: keeps playing, so it is part of the same contract.
MISC_AUDIO_MUSIC_KEYS = {
    "LowLODShellMusic": "shellLowLod",
    "HighLODShellMusic": "shellHighLod",
    "FullScreenSubMenuMusic": "fullScreenSubMenu",
    "ScoreScreenMusic": "scoreScreen",
    "CreditsMusic": "credits",
}

#: Screen leaves, keyed by the side token that should hear them.  ``None`` is
#: the alignment fallback used when a side has no dedicated leaf.
VICTORY_TRACK_BY_SIDE = {"Angmar": "VictoryScreenAngmar"}
VICTORY_TRACK_BY_ALIGNMENT = {"good": "VictoryScreenGood", "evil": "VictoryScreenEvil"}
DEFEAT_TRACK_BY_ALIGNMENT = {"good": "LoseScreenGood", "evil": "LoseScreenEvil"}

_DEFINE_RE = re.compile(
    rb"^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(-?\d+)", re.MULTILINE
)
_ADD_RE = re.compile(r"^#ADD\(\s*(.+?)\s*\)$", re.IGNORECASE)
_SUBTRACT_RE = re.compile(r"^#SUBTRACT\(\s*(.+?)\s*\)$", re.IGNORECASE)
_MISC_AUDIO_BLOCK_RE = re.compile(
    rb"^\s*MiscAudio\b(.*?)^\s*End\b", re.MULTILINE | re.DOTALL | re.IGNORECASE
)


class MusicImportError(ValueError):
    """The retail music binding could not be recovered as authored."""


def parse_music_defines(source: bytes) -> dict[str, int]:
    """Collect ``#define NAME <int>`` volume constants from an INI."""

    return {
        match.group(1).decode("ascii"): int(match.group(2))
        for match in _DEFINE_RE.finditer(source)
    }


def resolve_volume(expression: str | None, defines: Mapping[str, int]) -> int | None:
    """Resolve a ``Volume =`` right-hand side to an integer, or ``None``.

    Retail writes these three forms and no others in music.ini: a bare integer,
    a bare ``#define`` name, and ``#ADD( NAME <int> )`` / ``#SUBTRACT( ... )``.
    An unrecognised form returns ``None`` rather than a guessed number - a
    missing volume is honest, an invented one is not.
    """

    if expression is None:
        return None
    text = expression.split(";", 1)[0].strip()
    if not text:
        return None
    for pattern, sign in ((_ADD_RE, 1), (_SUBTRACT_RE, -1)):
        match = pattern.match(text)
        if not match:
            continue
        terms = match.group(1).split()
        if len(terms) != 2:
            return None
        left = _volume_term(terms[0], defines)
        right = _volume_term(terms[1], defines)
        if left is None or right is None:
            return None
        return left + sign * right
    return _volume_term(text, defines)


def _volume_term(term: str, defines: Mapping[str, int]) -> int | None:
    term = term.strip()
    try:
        return int(term)
    except ValueError:
        pass
    return defines.get(term)


def _control_flags(parameters: tuple[tuple[str, str], ...]) -> list[str]:
    flags: list[str] = []
    for key, value in parameters:
        if key.casefold() != "control":
            continue
        for token in value.split(";", 1)[0].split():
            folded = token.strip().casefold()
            if folded and folded not in flags:
                flags.append(folded)
    return flags


def _parameter(parameters: tuple[tuple[str, str], ...], name: str) -> str | None:
    folded = name.casefold()
    for key, value in parameters:
        if key.casefold() == folded:
            return value
    return None


def _script_index(document: Mapping[str, Any]) -> dict[str, Mapping[str, Any]]:
    scripts = document.get("scripts")
    if not isinstance(scripts, list):
        raise MusicImportError("music script document has no 'scripts' array")
    index: dict[str, Mapping[str, Any]] = {}
    for script in scripts:
        if isinstance(script, Mapping) and isinstance(script.get("name"), str):
            index[script["name"]] = script
    if not index:
        raise MusicImportError("music script document declares no named scripts")
    return index


def _statements(script: Mapping[str, Any]) -> list[tuple[str, str, list[Any]]]:
    """Flatten a parsed script into ``(kind, opcode, arguments)`` rows."""

    payload = script.get("payload")
    if not isinstance(payload, Mapping):
        return []
    rows: list[tuple[str, str, list[Any]]] = []

    def walk(records: Any) -> None:
        if not isinstance(records, list):
            return
        for record in records:
            if not isinstance(record, Mapping):
                continue
            name = record.get("name")
            value = record.get("value")
            if not isinstance(value, Mapping):
                continue
            if name == "OrCondition":
                walk(value.get("records"))
                continue
            if name not in {"Condition", "ScriptAction", "ScriptActionFalse"}:
                continue
            internal = value.get("internalName")
            if not isinstance(internal, Mapping):
                continue
            opcode = internal.get("name")
            if not isinstance(opcode, str):
                continue
            arguments: list[Any] = []
            for argument in value.get("arguments") or []:
                if not isinstance(argument, Mapping):
                    continue
                text = argument.get("text")
                arguments.append(text if text else argument.get("integer"))
            rows.append((str(name), opcode, arguments))

    walk(payload.get("records"))
    return rows


def music_script_faction_bindings(document: Mapping[str, Any]) -> dict[str, Any]:
    """Recover faction -> multisound bindings from the music-script library.

    Returns a mapping with ``sides`` (side token -> selector/alignment),
    ``phases`` (selector -> phase -> multisound id) and ``level1`` (alignment ->
    action/triumphal multisound id).  Everything is read out of the parsed
    script records; nothing is hard-coded per faction.
    """

    scripts = _script_index(document)

    sides: dict[str, dict[str, Any]] = {}
    for name, script in scripts.items():
        if not name.startswith("___MusicScript_Test"):
            continue
        side: str | None = None
        selector: int | None = None
        alignment: str | None = None
        for kind, opcode, arguments in _statements(script):
            if kind == "Condition" and opcode == "SKIRMISH_PLAYER_FACTION":
                if len(arguments) >= 2 and isinstance(arguments[1], str):
                    side = arguments[1]
            elif opcode == "SET_COUNTER" and arguments[:1] == [FACTION_COUNTER]:
                if len(arguments) >= 2 and isinstance(arguments[1], int):
                    selector = arguments[1]
            elif opcode == "SET_FLAG" and arguments[:1] == [GOOD_FACTION_FLAG]:
                if len(arguments) >= 2:
                    alignment = "good" if arguments[1] else "evil"
        if side is None or selector is None or alignment is None:
            raise MusicImportError(
                f"{name} does not declare a side, selector and alignment"
            )
        if side in sides:
            raise MusicImportError(f"side {side!r} is bound by more than one test script")
        sides[side] = {
            "selector": selector,
            "alignment": alignment,
            "scriptName": name,
        }
    if not sides:
        raise MusicImportError("no ___MusicScript_Test* faction tests were found")

    phases: dict[int, dict[str, str]] = {}
    for name, script in scripts.items():
        selector: int | None = None
        phase: str | None = None
        playlist: str | None = None
        for kind, opcode, arguments in _statements(script):
            if kind == "Condition" and opcode == "COUNTER":
                if arguments[:1] == [FACTION_COUNTER] and len(arguments) >= 3:
                    # (counter, comparison, value); comparison 2 is "equals".
                    if arguments[1] == 2 and isinstance(arguments[2], int):
                        selector = arguments[2]
            elif kind == "Condition" and opcode == "FLAG":
                if arguments and arguments[0] in PHASE_BY_FLAG and arguments[1:2] == [1]:
                    phase = PHASE_BY_FLAG[str(arguments[0])]
            elif opcode.startswith("MUSIC_SCRIPT_") and arguments:
                if isinstance(arguments[0], str):
                    playlist = arguments[0]
        if selector is None or phase is None or playlist is None:
            continue
        bucket = phases.setdefault(selector, {})
        if bucket.get(phase) not in (None, playlist):
            raise MusicImportError(
                f"selector {selector} binds phase {phase} to two playlists"
            )
        bucket[phase] = playlist
    if not phases:
        raise MusicImportError("no phase playlists were bound to a faction selector")

    level1: dict[str, dict[str, str]] = {}
    for name, script in scripts.items():
        for role, marker in (("action", "DoAction"), ("triumphal", "DoTriumphal")):
            if marker not in name:
                continue
            alignment = "good" if "Good" in name else "evil" if "Evil" in name else None
            if alignment is None:
                continue
            for _kind, opcode, arguments in _statements(script):
                if opcode.startswith("MUSIC_SCRIPT_") and arguments:
                    if isinstance(arguments[0], str):
                        level1.setdefault(alignment, {})[role] = arguments[0]

    return {
        "sides": sides,
        "phases": {str(key): value for key, value in sorted(phases.items())},
        "level1": level1,
    }


def parse_misc_audio_music(source: bytes) -> dict[str, str]:
    """Read the music declarations out of ``miscaudio.ini``'s MiscAudio block."""

    match = _MISC_AUDIO_BLOCK_RE.search(source)
    if match is None:
        raise MusicImportError("miscaudio.ini has no MiscAudio block")
    body = match.group(1).decode("latin-1")
    result: dict[str, str] = {}
    for line in body.splitlines():
        stripped = line.split(";", 1)[0].strip()
        if "=" not in stripped:
            continue
        key, _, value = stripped.partition("=")
        target = MISC_AUDIO_MUSIC_KEYS.get(key.strip())
        if target is None:
            continue
        cleaned = value.strip()
        if cleaned and cleaned.casefold() != "nosound":
            result[target] = cleaned
    return result


def _track_row(
    track_id: str,
    definition: Any,
    defines: Mapping[str, int],
) -> dict[str, Any] | None:
    filename = getattr(definition, "filename", None)
    if not filename:
        # ``Type = FAKE`` leaves (e.g. Silence) carry no file; a playlist that
        # references one keeps the id but ships no bytes.
        return None
    leaf = filename.strip().replace("\\", "/").rsplit("/", 1)[-1].casefold()
    row: dict[str, Any] = {
        "id": track_id,
        "file": f"audio/music/tracks/{leaf}",
        "source": f"{RETAIL_TRACK_DIR}/{leaf}",
    }
    volume = resolve_volume(
        _parameter(getattr(definition, "parameters", ()), "Volume"), defines
    )
    if volume is not None:
        row["volume"] = volume
    control = _control_flags(getattr(definition, "parameters", ()))
    if control:
        row["control"] = control
    return row


def build_music_document(
    *,
    music_ini: bytes,
    misc_audio_ini: bytes,
    script_document: Mapping[str, Any],
    provenance: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    """Compose ``data/music.json`` from the authored retail music binding."""

    defines = parse_music_defines(music_ini)
    definitions = parse_sage_audio_definitions(music_ini)
    tracks_by_id = {track.id: track for track in definitions.tracks}
    multisounds_by_id = {sound.id: sound for sound in definitions.multisounds}
    bindings = music_script_faction_bindings(script_document)
    misc = parse_misc_audio_music(misc_audio_ini)

    referenced_playlists: set[str] = set()
    for phase_map in bindings["phases"].values():
        referenced_playlists.update(phase_map.values())
    for roles in bindings["level1"].values():
        referenced_playlists.update(roles.values())
    referenced_playlists.update(
        value
        for value in misc.values()
        if value in multisounds_by_id or value in tracks_by_id
    )

    factions: dict[str, Any] = {}
    for side, row in sorted(bindings["sides"].items()):
        selector = str(row["selector"])
        alignment = str(row["alignment"])
        phase_map = bindings["phases"].get(selector)
        if not phase_map:
            raise MusicImportError(
                f"side {side} selects music slot {selector}, which binds no playlist"
            )
        entry: dict[str, Any] = {
            "side": side,
            "alignment": alignment,
            "selector": int(selector),
            "phases": dict(sorted(phase_map.items())),
            "derivation": "script-bound",
            "scriptName": row["scriptName"],
        }
        level1 = bindings["level1"].get(alignment, {})
        if level1:
            entry["level1"] = dict(sorted(level1.items()))
        screens: dict[str, Any] = {}
        victory = VICTORY_TRACK_BY_SIDE.get(side) or VICTORY_TRACK_BY_ALIGNMENT.get(
            alignment
        )
        defeat = DEFEAT_TRACK_BY_ALIGNMENT.get(alignment)
        if victory in tracks_by_id:
            screens["victory"] = victory
        if defeat in tracks_by_id:
            screens["defeat"] = defeat
        if screens:
            # Name-bound, not script-bound: the score screen picks these in
            # engine code, so the derivation is stamped and stays visible.
            entry["screens"] = dict(sorted(screens.items()))
            entry["screensDerivation"] = "name-bound"
        factions[side] = entry

    playlists: dict[str, Any] = {}
    referenced_tracks: set[str] = set()
    for playlist_id in sorted(referenced_playlists):
        multisound = multisounds_by_id.get(playlist_id)
        if multisound is None:
            # A MiscAudio slot may name a bare MusicTrack (Shell2Music); wrap it
            # as a one-leaf playlist rather than dropping the declaration.
            if playlist_id in tracks_by_id:
                playlists[playlist_id] = {
                    "id": playlist_id,
                    "control": ["play_one"],
                    "tracks": [playlist_id],
                    "kind": "track",
                }
                referenced_tracks.add(playlist_id)
                continue
            raise MusicImportError(
                f"music binding references {playlist_id!r}, which music.ini "
                "declares neither as a Multisound nor as a MusicTrack"
            )
        members = [reference.id for reference in multisound.subsounds]
        if not members:
            raise MusicImportError(f"multisound {playlist_id} declares no subsounds")
        playlists[playlist_id] = {
            "id": playlist_id,
            "control": _control_flags(multisound.parameters) or ["play_one"],
            "tracks": members,
            "kind": "multisound",
        }
        referenced_tracks.update(members)

    for entry in factions.values():
        for track_id in (entry.get("screens") or {}).values():
            referenced_tracks.add(str(track_id))

    tracks: dict[str, Any] = {}
    silent: list[str] = []
    for track_id in sorted(referenced_tracks):
        definition = tracks_by_id.get(track_id)
        if definition is None:
            raise MusicImportError(f"music.ini declares no MusicTrack {track_id!r}")
        row = _track_row(track_id, definition, defines)
        if row is None:
            silent.append(track_id)
            continue
        tracks[track_id] = row

    document: dict[str, Any] = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "source": dict(provenance or {}),
        "factions": factions,
        "playlists": dict(sorted(playlists.items())),
        "tracks": tracks,
        "shell": dict(sorted(misc.items())),
        "counts": {
            "factions": len(factions),
            "playlists": len(playlists),
            "tracks": len(tracks),
        },
    }
    if silent:
        document["silentTracks"] = sorted(silent)
    return document


def music_profile_resources(document: Mapping[str, Any]) -> list[dict[str, Any]]:
    """One ``copy`` resource per referenced track.

    ``copy`` and not ``audio``: the retail leaves are already MP3, which Godot
    decodes natively, so re-encoding through the pinned ffmpeg lane would cost
    a lossy generation and ~300 MB of CPU for no runtime benefit.  The existing
    ``music`` resources in ``men-fords-v0`` use ``copy`` for the same reason;
    the ffmpeg lane stays where it earns its keep, on the WAV voice closure.
    Only tracks a faction playlist actually references get a resource.
    """

    tracks = document.get("tracks")
    if not isinstance(tracks, Mapping):
        raise MusicImportError("music document has no 'tracks' mapping")
    resources: list[dict[str, Any]] = []
    seen: set[str] = set()
    for track_id, row in sorted(tracks.items()):
        if not isinstance(row, Mapping):
            raise MusicImportError(f"track {track_id!r} is not an object")
        source = str(row["source"])
        leaf = source.rsplit("/", 1)[-1]
        if leaf in seen:
            # Retail aliases some ids onto the same mp3 (AcGood01 and AcGood02
            # both point into the AcGood0N_* file set with an off-by-one).
            # One file, one resource; the document keeps both ids.
            continue
        seen.add(leaf)
        resources.append(
            {
                # Resource ids are bounded lowercase slugs, and retail track
                # filenames carry underscores, so the slug is derived from the
                # file leaf (unique by construction) with '_' folded to '-'.
                "id": "music-track-%s" % leaf.rsplit(".", 1)[0].casefold().replace("_", "-"),
                "kind": "music",
                "converter": "copy",
                "patterns": [source],
                "output": f"{PACK_TRACK_DIR}/{leaf}",
                "limit": 1,
            }
        )
    return resources


def compose_music_profile(
    document: Mapping[str, Any],
    *,
    pack_id: str,
    game: str,
    catalog_identity: str,
) -> dict[str, Any]:
    """Build a standalone import profile for the music-only pack.

    A separate pack (not a faction pack edit) is the right shape here: the
    music closure is identical for every faction - the *selection* differs, not
    the bytes - so folding ~290 MB of shared tracks into each faction pack
    would multiply them by the faction count.  It also keeps this deliverable
    off the contested faction-pack oracle entirely.
    """

    return {
        "format": 1,
        "id": f"{pack_id}-profile",
        "title": f"{game.upper()} per-faction music",
        "pack": {
            "id": pack_id,
            "version": "0.1",
            "schema": "openbfme.content-pack",
            "schemaVersion": 0,
            # Below the faction slices (900) so a faction pack always wins any
            # id collision; music declares its own file and collides with none.
            "priority": 400,
            "vertical_slice_complete": False,
            "capability_maturity": "faction-bound-ambient-music",
            "dataPolicy": {"externalPathsAllowed": False, "redistributable": False},
            "files": {"music": "data/music.json"},
            "sourceCatalogIdentitySha256": catalog_identity,
        },
        "resources": music_profile_resources(document),
        "runtime_data": {"data/music.json": dict(document)},
    }
