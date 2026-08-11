"""Bounded SAGE audio-definition parsing and dependency resolution.

The retail runtime names logical ``AudioEvent``, ``Multisound``, and
``MusicTrack`` records; those records, rather than filename prefixes, are the
authoritative bridge to sample leaves.  This module keeps that graph
deterministic and source-neutral.  It never reads a retail install itself and
never returns source bytes or host filesystem paths.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import PurePosixPath
import re
from typing import Iterable

from .paths import safe_relative_parts
from .sage_ini import MAX_INI_BYTES, IniBlock, parse_flat_named_blocks


MAX_AUDIO_DEFINITIONS = 32_768
MAX_AUDIO_ROOTS = 16_384
MAX_AUDIO_REFERENCES_PER_DEFINITION = 4_096
MAX_AUDIO_PARAMETERS_PER_EVENT = 4_096
MAX_AUDIO_REFERENCE_WEIGHT = 1_000_000
MAX_AUDIO_SAMPLE_PATHS = 250_000

_APPROVED_FACTION_VOICE_EQUIVALENCES = {
    ("mordorbatteringram", "sound", "urukvoicedie"): (
        "OrcVoiceDie",
        "mordor-battering-ram-inherits-isengard-slow-death-sound",
    ),
}


def normalize_faction_voice_event(object_id: str, field: str, event_id: str) -> str:
    """Apply only source-backed, object-specific faction voice equivalences.

    RotWK authors ``MordorBatteringRam`` as an Isengard child and replaces all
    seventeen command voices with ``OrcBatteringRamVoice*`` but accidentally
    inherits ``SlowDeathBehavior/Sound = INITIAL UrukVoiceDie``.  Retail places
    ``OrcVoiceDie`` immediately beside ``UrukVoiceDie`` with identical semantics,
    and Mordor unit/siege death behaviors repeatedly select the Orc event.  That
    makes this one binding an approved equivalence rather than an invented
    substitute.  Every other event, including stranded/modded goblin bindings,
    remains verbatim and cannot abort an edition build here.
    """

    approved = _APPROVED_FACTION_VOICE_EQUIVALENCES.get(
        (object_id.casefold(), field.casefold(), event_id.casefold())
    )
    return approved[0] if approved is not None else event_id


def faction_voice_equivalence_provenance(
    object_id: str, field: str, event_id: str
) -> dict[str, str] | None:
    """Describe an approved rewrite so compiled routes retain source intent."""

    approved = _APPROVED_FACTION_VOICE_EQUIVALENCES.get(
        (object_id.casefold(), field.casefold(), event_id.casefold())
    )
    if approved is None:
        return None
    return {"authoredId": event_id, "reason": approved[1]}

_IDENTIFIER = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_+.-]{0,255}$")
_REFERENCE = re.compile(
    r"^(?P<id>[A-Za-z0-9_][A-Za-z0-9_+.-]{0,255})(?::(?P<weight>[0-9]+))?$"
)
_MEDIA_EXTENSIONS = {".wav", ".mp3"}
_TRACK_FILENAME = re.compile(
    r"^[A-Za-z0-9_][A-Za-z0-9_+.-]{0,255}\.(?:wav|mp3)$", re.IGNORECASE
)


@dataclass(frozen=True, slots=True)
class WeightedAudioReference:
    """One logical sample/event reference and its optional SAGE weight."""

    id: str
    weight: int | None = None

    def neutral(self) -> dict[str, object]:
        value: dict[str, object] = {"id": self.id}
        if self.weight is not None:
            value["weight"] = self.weight
        return value


@dataclass(frozen=True, slots=True)
class AudioEventDefinition:
    id: str
    sounds: tuple[WeightedAudioReference, ...]
    parameters: tuple[tuple[str, str], ...]

    def neutral(self) -> dict[str, object]:
        return {
            "id": self.id,
            "sounds": [item.neutral() for item in self.sounds],
            "parameters": [
                {"field": field, "value": value}
                for field, value in self.parameters
            ],
        }


@dataclass(frozen=True, slots=True)
class MultisoundDefinition:
    id: str
    subsounds: tuple[WeightedAudioReference, ...]
    parameters: tuple[tuple[str, str], ...]

    def neutral(self) -> dict[str, object]:
        return {
            "id": self.id,
            "subsounds": [item.neutral() for item in self.subsounds],
            "parameters": [
                {"field": field, "value": value}
                for field, value in self.parameters
            ],
        }


@dataclass(frozen=True, slots=True)
class MusicTrackDefinition:
    """One ``MusicTrack`` record; ``filename`` is ``None`` for FAKE silence."""

    id: str
    filename: str | None
    parameters: tuple[tuple[str, str], ...]

    def neutral(self) -> dict[str, object]:
        return {
            "id": self.id,
            "filename": self.filename,
            "parameters": [
                {"field": field, "value": value}
                for field, value in self.parameters
            ],
        }


@dataclass(frozen=True, slots=True)
class SageAudioDefinitions:
    events: tuple[AudioEventDefinition, ...]
    multisounds: tuple[MultisoundDefinition, ...]
    tracks: tuple[MusicTrackDefinition, ...] = ()


@dataclass(frozen=True, slots=True)
class SageAudioClosure:
    root_ids: tuple[str, ...]
    events: tuple[AudioEventDefinition, ...]
    multisounds: tuple[MultisoundDefinition, ...]
    sample_ids: tuple[str, ...]
    tracks: tuple[MusicTrackDefinition, ...] = ()

    def neutral(self) -> dict[str, object]:
        return {
            "rootIds": list(self.root_ids),
            "events": [item.neutral() for item in self.events],
            "multisounds": [item.neutral() for item in self.multisounds],
            "sampleIds": list(self.sample_ids),
            "tracks": [item.neutral() for item in self.tracks],
        }


def _identifier(value: str, context: str) -> str:
    if not _IDENTIFIER.fullmatch(value):
        raise ValueError(f"unsafe {context} identifier: {value!r}")
    return value


def _references(values: Iterable[str], context: str) -> tuple[WeightedAudioReference, ...]:
    result: list[WeightedAudioReference] = []
    for value in values:
        for token in value.split():
            match = _REFERENCE.fullmatch(token)
            if match is None:
                raise ValueError(f"unsafe {context} reference: {token!r}")
            raw_weight = match.group("weight")
            weight = int(raw_weight) if raw_weight is not None else None
            if weight is not None and weight > MAX_AUDIO_REFERENCE_WEIGHT:
                raise ValueError(f"{context} reference weight exceeds limit")
            result.append(WeightedAudioReference(match.group("id"), weight))
            if len(result) > MAX_AUDIO_REFERENCES_PER_DEFINITION:
                raise ValueError(f"{context} reference count exceeds limit")
    return tuple(result)


def _unique_blocks(blocks: tuple[IniBlock, ...], kind: str) -> tuple[IniBlock, ...]:
    if len(blocks) > MAX_AUDIO_DEFINITIONS:
        raise ValueError(f"{kind} definition count exceeds limit")
    seen: dict[str, int] = {}
    unique: list[IniBlock] = []
    for block in blocks:
        _identifier(block.name, kind)
        key = block.name.casefold()
        previous_index = seen.get(key)
        if previous_index is None:
            seen[key] = len(unique)
            unique.append(block)
        else:
            # SAGE's ordered definition namespace is last-wins. Layered
            # voice.ini uses this intentionally (Merry's second Retreat row
            # removes PlayPercent). Replace atomically; never merge fields from
            # two definitions into a shape retail did not author.
            unique[previous_index] = block
    return tuple(unique)


def parse_sage_audio_definitions(source: bytes) -> SageAudioDefinitions:
    """Parse the two definition families used by object audio references."""

    if len(source) > MAX_INI_BYTES:
        raise ValueError(f"audio definition source exceeds {MAX_INI_BYTES} byte limit")
    event_blocks = _unique_blocks(
        parse_flat_named_blocks(source, "AudioEvent"), "AudioEvent"
    )
    multisound_blocks = _unique_blocks(
        parse_flat_named_blocks(source, "Multisound"), "Multisound"
    )
    track_blocks = _unique_blocks(
        parse_flat_named_blocks(source, "MusicTrack"), "MusicTrack"
    )
    if len(event_blocks) + len(multisound_blocks) + len(track_blocks) > MAX_AUDIO_DEFINITIONS:
        raise ValueError("audio definition count exceeds limit")

    events: list[AudioEventDefinition] = []
    for block in event_blocks:
        # AudioEvent scalar assignments are ordered/last-wins. The layered
        # GoblinBuilder row contains an earlier malformed Sounds assignment
        # followed by its valid effective value; do not concatenate both into
        # a sample list the engine never authors.
        authored_sound_values = block.values("Sounds")
        sound_values = authored_sound_values[-1:]
        sounds = _references(sound_values, f"AudioEvent {block.name} Sounds")
        parameters = tuple(
            (field, value)
            for field, value in block.assignments
            if field.casefold() != "sounds"
        )
        if len(parameters) > MAX_AUDIO_PARAMETERS_PER_EVENT:
            raise ValueError(f"AudioEvent {block.name!r} parameter count exceeds limit")
        events.append(AudioEventDefinition(block.name, sounds, parameters))

    multisounds: list[MultisoundDefinition] = []
    for block in multisound_blocks:
        subsounds = _references(
            block.values("Subsounds"), f"Multisound {block.name} Subsounds"
        )
        parameters = tuple(
            (field, value)
            for field, value in block.assignments
            if field.casefold() != "subsounds"
        )
        if len(parameters) > MAX_AUDIO_PARAMETERS_PER_EVENT:
            raise ValueError(f"Multisound {block.name!r} parameter count exceeds limit")
        multisounds.append(MultisoundDefinition(block.name, subsounds, parameters))

    tracks: list[MusicTrackDefinition] = []
    for block in track_blocks:
        filenames = block.values("Filename")
        if len(filenames) > 1:
            raise ValueError(f"MusicTrack {block.name!r} has multiple Filename values")
        filename = filenames[0].strip() if filenames else ""
        if filename and _TRACK_FILENAME.fullmatch(filename) is None:
            raise ValueError(f"unsafe MusicTrack {block.name!r} filename: {filename!r}")
        parameters = tuple(
            (field, value)
            for field, value in block.assignments
            if field.casefold() != "filename"
        )
        if len(parameters) > MAX_AUDIO_PARAMETERS_PER_EVENT:
            raise ValueError(f"MusicTrack {block.name!r} parameter count exceeds limit")
        tracks.append(MusicTrackDefinition(block.name, filename or None, parameters))

    events.sort(key=lambda item: (item.id.casefold(), item.id))
    multisounds.sort(key=lambda item: (item.id.casefold(), item.id))
    tracks.sort(key=lambda item: (item.id.casefold(), item.id))
    event_keys = {item.id.casefold() for item in events}
    overlap = sorted(
        (item.id for item in multisounds if item.id.casefold() in event_keys),
        key=str.casefold,
    )
    if overlap:
        raise ValueError(f"ambiguous audio definition kind: {overlap[0]!r}")
    track_keys = {item.id.casefold() for item in tracks}
    track_overlap = sorted(
        (
            item.id
            for item in (*events, *multisounds)
            if item.id.casefold() in track_keys
        ),
        key=str.casefold,
    )
    if track_overlap:
        raise ValueError(f"ambiguous audio definition kind: {track_overlap[0]!r}")
    return SageAudioDefinitions(tuple(events), tuple(multisounds), tuple(tracks))


def resolve_sage_audio_closure(
    definitions: SageAudioDefinitions, root_ids: Iterable[str]
) -> SageAudioClosure:
    """Resolve exact object roots through multisounds to sample identifiers.

    Multisound subsounds may name ``AudioEvent``, nested ``Multisound``, or
    ``MusicTrack`` records; tracks contribute their ``Filename`` stem as a
    sample identifier (FAKE silence tracks contribute none).
    """

    roots = list(root_ids)
    if len(roots) > MAX_AUDIO_ROOTS:
        raise ValueError(f"audio roots must contain at most {MAX_AUDIO_ROOTS} identifiers")
    requested: dict[str, str] = {}
    for root in roots:
        _identifier(root, "audio root")
        key = root.casefold()
        if key in requested:
            raise ValueError(f"duplicate audio root identifier: {root!r}")
        requested[key] = root

    events = {item.id.casefold(): item for item in definitions.events}
    multisounds = {item.id.casefold(): item for item in definitions.multisounds}
    tracks = {item.id.casefold(): item for item in definitions.tracks}
    selected_events: dict[str, AudioEventDefinition] = {}
    selected_multisounds: dict[str, MultisoundDefinition] = {}
    selected_tracks: dict[str, MusicTrackDefinition] = {}
    visiting: set[str] = set()

    def visit(identifier: str) -> None:
        key = identifier.casefold()
        event = events.get(key)
        if event is not None:
            selected_events[key] = event
            return
        track = tracks.get(key)
        if track is not None:
            selected_tracks[key] = track
            return
        multisound = multisounds.get(key)
        if multisound is None:
            raise ValueError(f"unresolved audio definition: {identifier!r}")
        if key in selected_multisounds:
            return
        if key in visiting:
            raise ValueError(f"Multisound dependency cycle at {identifier!r}")
        visiting.add(key)
        for reference in multisound.subsounds:
            visit(reference.id)
        visiting.remove(key)
        selected_multisounds[key] = multisound

    for key in sorted(requested):
        visit(requested[key])

    def canonical(key: str) -> str:
        if key in events:
            return events[key].id
        if key in tracks:
            return tracks[key].id
        return multisounds[key].id

    canonical_roots = [canonical(key) for key in requested]

    selected_event_list = sorted(
        selected_events.values(), key=lambda item: (item.id.casefold(), item.id)
    )
    selected_multisound_list = sorted(
        selected_multisounds.values(), key=lambda item: (item.id.casefold(), item.id)
    )
    selected_track_list = sorted(
        selected_tracks.values(), key=lambda item: (item.id.casefold(), item.id)
    )
    samples_by_key: dict[str, str] = {}
    for item in selected_event_list:
        for reference in item.sounds:
            # SAGE identifiers and the Windows retail filesystem are
            # case-insensitive.  Retail 1.06 contains authored case variants
            # of the same Uruk sample across different AudioEvents.
            samples_by_key.setdefault(reference.id.casefold(), reference.id)
    for item in selected_track_list:
        if item.filename is None:
            continue  # FAKE silence tracks intentionally reference no media leaf.
        stem = PurePosixPath(item.filename).stem
        samples_by_key.setdefault(stem.casefold(), stem)
    samples = sorted(
        samples_by_key.values(), key=lambda item: (item.casefold(), item)
    )
    return SageAudioClosure(
        tuple(sorted(canonical_roots, key=str.casefold)),
        tuple(selected_event_list),
        tuple(selected_multisound_list),
        tuple(samples),
        tuple(selected_track_list),
    )


def resolve_audio_sample_paths(
    sample_ids: Iterable[str], virtual_paths: Iterable[str]
) -> dict[str, str]:
    """Resolve sample stems to unique normalized virtual media paths."""

    requested: dict[str, str] = {}
    for sample_id in sample_ids:
        _identifier(sample_id, "audio sample")
        key = sample_id.casefold()
        if key in requested:
            raise ValueError(f"duplicate audio sample identifier: {sample_id!r}")
        requested[key] = sample_id
    if len(requested) > MAX_AUDIO_SAMPLE_PATHS:
        raise ValueError("audio sample request count exceeds limit")

    selected_paths = list(virtual_paths)
    if len(selected_paths) > MAX_AUDIO_SAMPLE_PATHS:
        raise ValueError("audio sample path count exceeds limit")
    candidates: dict[str, set[str]] = {}
    for value in selected_paths:
        parts = safe_relative_parts(value)
        normalized = "/".join(parts)
        folded = normalized.casefold()
        if not folded.startswith("data/audio/"):
            continue
        suffix = PurePosixPath(normalized).suffix.casefold()
        if suffix not in _MEDIA_EXTENSIONS:
            continue
        stem = PurePosixPath(normalized).stem.casefold()
        if stem in requested:
            candidates.setdefault(stem, set()).add(normalized)

    result: dict[str, str] = {}
    for key in sorted(requested):
        matches = sorted(candidates.get(key, set()), key=lambda item: item.casefold())
        if not matches:
            raise ValueError(f"unresolved audio sample: {requested[key]!r}")
        if len(matches) != 1:
            raise ValueError(f"ambiguous audio sample: {requested[key]!r}")
        result[requested[key]] = matches[0]
    return result


def resolve_audio_sample_paths_partial(
    sample_ids: Iterable[str], virtual_paths: Iterable[str]
) -> tuple[dict[str, str], tuple[str, ...], tuple[str, ...]]:
    """Resolve samples once while preserving missing/ambiguous rows as facts."""

    requested: dict[str, str] = {}
    for sample_id in sample_ids:
        _identifier(sample_id, "audio sample")
        key = sample_id.casefold()
        if key in requested:
            raise ValueError(f"duplicate audio sample identifier: {sample_id!r}")
        requested[key] = sample_id
    if len(requested) > MAX_AUDIO_SAMPLE_PATHS:
        raise ValueError("audio sample request count exceeds limit")

    selected_paths = list(virtual_paths)
    if len(selected_paths) > MAX_AUDIO_SAMPLE_PATHS:
        raise ValueError("audio sample path count exceeds limit")
    candidates: dict[str, set[str]] = {}
    for value in selected_paths:
        parts = safe_relative_parts(value)
        normalized = "/".join(parts)
        folded = normalized.casefold()
        if not folded.startswith("data/audio/"):
            continue
        suffix = PurePosixPath(normalized).suffix.casefold()
        if suffix not in _MEDIA_EXTENSIONS:
            continue
        stem = PurePosixPath(normalized).stem.casefold()
        if stem in requested:
            candidates.setdefault(stem, set()).add(normalized)

    resolved: dict[str, str] = {}
    missing: list[str] = []
    ambiguous: list[str] = []
    for key in sorted(requested):
        matches = sorted(candidates.get(key, set()), key=lambda item: item.casefold())
        if not matches:
            missing.append(requested[key])
        elif len(matches) != 1:
            ambiguous.append(requested[key])
        else:
            resolved[requested[key]] = matches[0]
    return resolved, tuple(missing), tuple(ambiguous)
