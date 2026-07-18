"""Bounded SAGE audio-definition parsing and dependency resolution.

The retail runtime names logical ``AudioEvent`` and ``Multisound`` records;
those records, rather than filename prefixes, are the authoritative bridge to
sample leaves.  This module keeps that graph deterministic and source-neutral.
It never reads a retail install itself and never returns source bytes or host
filesystem paths.
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

_IDENTIFIER = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_+.-]{0,255}$")
_REFERENCE = re.compile(
    r"^(?P<id>[A-Za-z0-9_][A-Za-z0-9_+.-]{0,255})(?::(?P<weight>[0-9]+))?$"
)
_MEDIA_EXTENSIONS = {".wav", ".mp3"}


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
class SageAudioDefinitions:
    events: tuple[AudioEventDefinition, ...]
    multisounds: tuple[MultisoundDefinition, ...]


@dataclass(frozen=True, slots=True)
class SageAudioClosure:
    root_ids: tuple[str, ...]
    events: tuple[AudioEventDefinition, ...]
    multisounds: tuple[MultisoundDefinition, ...]
    sample_ids: tuple[str, ...]

    def neutral(self) -> dict[str, object]:
        return {
            "rootIds": list(self.root_ids),
            "events": [item.neutral() for item in self.events],
            "multisounds": [item.neutral() for item in self.multisounds],
            "sampleIds": list(self.sample_ids),
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
    seen: set[str] = set()
    for block in blocks:
        _identifier(block.name, kind)
        key = block.name.casefold()
        if key in seen:
            raise ValueError(f"duplicate {kind} definition: {block.name!r}")
        seen.add(key)
    return blocks


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
    if len(event_blocks) + len(multisound_blocks) > MAX_AUDIO_DEFINITIONS:
        raise ValueError("audio definition count exceeds limit")

    events: list[AudioEventDefinition] = []
    for block in event_blocks:
        sound_values = block.values("Sounds")
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

    events.sort(key=lambda item: (item.id.casefold(), item.id))
    multisounds.sort(key=lambda item: (item.id.casefold(), item.id))
    event_keys = {item.id.casefold() for item in events}
    overlap = sorted(
        (item.id for item in multisounds if item.id.casefold() in event_keys),
        key=str.casefold,
    )
    if overlap:
        raise ValueError(f"ambiguous audio definition kind: {overlap[0]!r}")
    return SageAudioDefinitions(tuple(events), tuple(multisounds))


def resolve_sage_audio_closure(
    definitions: SageAudioDefinitions, root_ids: Iterable[str]
) -> SageAudioClosure:
    """Resolve exact object roots through multisounds to sample identifiers."""

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
    selected_events: dict[str, AudioEventDefinition] = {}
    selected_multisounds: dict[str, MultisoundDefinition] = {}
    visiting: set[str] = set()

    def visit(identifier: str) -> None:
        key = identifier.casefold()
        event = events.get(key)
        if event is not None:
            selected_events[key] = event
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

    canonical_roots = [
        events[key].id if key in events else multisounds[key].id
        for key in requested
    ]

    selected_event_list = sorted(
        selected_events.values(), key=lambda item: (item.id.casefold(), item.id)
    )
    selected_multisound_list = sorted(
        selected_multisounds.values(), key=lambda item: (item.id.casefold(), item.id)
    )
    samples_by_key: dict[str, str] = {}
    for item in selected_event_list:
        for reference in item.sounds:
            # SAGE identifiers and the Windows retail filesystem are
            # case-insensitive.  Retail 1.06 contains authored case variants
            # of the same Uruk sample across different AudioEvents.
            samples_by_key.setdefault(reference.id.casefold(), reference.id)
    samples = sorted(
        samples_by_key.values(), key=lambda item: (item.casefold(), item)
    )
    return SageAudioClosure(
        tuple(sorted(canonical_roots, key=str.casefold)),
        tuple(selected_event_list),
        tuple(selected_multisound_list),
        tuple(samples),
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
