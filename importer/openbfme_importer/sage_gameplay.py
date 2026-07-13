"""Typed, payload-free closure for flat SAGE gameplay definitions."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
from typing import Iterable, Mapping

from .sage_ini import IniBlock, parse_flat_named_blocks


MAX_GAMEPLAY_DEFINITIONS = 32_768
MAX_GAMEPLAY_ROOTS = 16_384


@dataclass(frozen=True, slots=True)
class GameplayClosure:
    upgrades: tuple[dict[str, object], ...]
    sciences: tuple[dict[str, object], ...]
    special_powers: tuple[dict[str, object], ...]
    mapped_images: tuple[str, ...]
    text_ids: tuple[str, ...]
    audio_roots: tuple[str, ...]
    fx_lists: tuple[str, ...]
    missing_upgrades: tuple[str, ...]
    ambiguous_upgrades: tuple[str, ...]
    missing_sciences: tuple[str, ...]
    ambiguous_sciences: tuple[str, ...]
    missing_special_powers: tuple[str, ...]
    ambiguous_special_powers: tuple[str, ...]


def _tokens(values: Iterable[str]) -> tuple[str, ...]:
    return tuple(
        token
        for value in values
        for token in value.split()
        if token.casefold() not in {"none", "null", "0"} and not token.startswith("$")
    )


def _digest(block: IniBlock) -> str:
    digest = hashlib.sha256(b"openbfme.sage-gameplay-definition\0")
    digest.update(block.kind.encode("utf-8") + b"\0")
    digest.update(block.name.encode("utf-8") + b"\0")
    for field, value in block.assignments:
        digest.update(field.encode("utf-8") + b"\0")
        digest.update(value.encode("utf-8") + b"\n")
    return digest.hexdigest()


def _candidates(source: bytes, kind: str) -> dict[str, list[IniBlock]]:
    blocks = parse_flat_named_blocks(source, kind)
    if len(blocks) > MAX_GAMEPLAY_DEFINITIONS:
        raise ValueError(f"{kind} definition count exceeds limit")
    result: dict[str, list[IniBlock]] = {}
    for block in blocks:
        result.setdefault(block.name.casefold(), []).append(block)
    return result


def _canonical_roots(values: Iterable[str], context: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for value in values:
        if not isinstance(value, str) or not value or "\0" in value:
            raise ValueError(f"{context} contains an invalid identifier")
        result.setdefault(value.casefold(), value)
        if len(result) > MAX_GAMEPLAY_ROOTS:
            raise ValueError(f"{context} count exceeds limit")
    return result


def _resolved_row(block: IniBlock, references: Mapping[str, Iterable[str]]) -> dict[str, object]:
    normalized = {
        key: sorted(set(values), key=lambda item: (item.casefold(), item))
        for key, values in references.items()
        if values
    }
    return {
        "id": block.name,
        "assignmentCount": len(block.assignments),
        "definitionSha256": _digest(block),
        "references": dict(sorted(normalized.items())),
    }


def resolve_gameplay_definition_closure(
    *,
    upgrade_source: bytes,
    science_source: bytes,
    special_power_source: bytes,
    upgrade_roots: Iterable[str],
    science_roots: Iterable[str],
    special_power_roots: Iterable[str],
    string_identifiers: Mapping[str, str],
    audio_identifiers: Mapping[str, str],
) -> GameplayClosure:
    """Resolve command roots and their typed definition-to-definition edges."""

    upgrade_candidates = _candidates(upgrade_source, "Upgrade")
    science_candidates = _candidates(science_source, "Science")
    power_candidates = _candidates(special_power_source, "SpecialPower")
    requested_upgrades = _canonical_roots(upgrade_roots, "upgrade roots")
    requested_sciences = _canonical_roots(science_roots, "science roots")
    requested_powers = _canonical_roots(special_power_roots, "special-power roots")
    mapped_images: set[str] = set()
    text_ids: set[str] = set()
    audio_roots: set[str] = set()
    fx_lists: set[str] = set()
    upgrade_rows: dict[str, dict[str, object]] = {}
    science_rows: dict[str, dict[str, object]] = {}
    power_rows: dict[str, dict[str, object]] = {}
    missing_upgrades: set[str] = set()
    ambiguous_upgrades: set[str] = set()
    missing_sciences: set[str] = set()
    ambiguous_sciences: set[str] = set()
    missing_powers: set[str] = set()
    ambiguous_powers: set[str] = set()

    def collect_shared(block: IniBlock) -> tuple[set[str], set[str]]:
        block_text: set[str] = set()
        block_audio: set[str] = set()
        for _, value in block.assignments:
            for token in _tokens((value,)):
                string_id = string_identifiers.get(token.casefold())
                if string_id is not None:
                    text_ids.add(string_id)
                    block_text.add(string_id)
                audio_id = audio_identifiers.get(token.casefold())
                if audio_id is not None:
                    audio_roots.add(audio_id)
                    block_audio.add(audio_id)
        return block_text, block_audio

    processed_upgrades: set[str] = set()
    while True:
        pending = [key for key in sorted(requested_upgrades) if key not in processed_upgrades]
        if not pending:
            break
        for key in pending:
            processed_upgrades.add(key)
            requested = requested_upgrades[key]
            matches = upgrade_candidates.get(key, [])
            if not matches:
                missing_upgrades.add(requested)
                continue
            if len(matches) != 1:
                ambiguous_upgrades.add(requested)
                continue
            block = matches[0]
            sub_upgrades = _tokens(block.values("SubUpgradeTemplateNames"))
            for identifier in sub_upgrades:
                requested_upgrades.setdefault(identifier.casefold(), identifier)
            block_images = {
                token
                for field in ("ButtonImage", "StrategicIcon")
                for token in _tokens(block.values(field))
            }
            mapped_images.update(block_images)
            block_fx = set(_tokens(block.values("UpgradeFX")))
            fx_lists.update(block_fx)
            block_text, block_audio = collect_shared(block)
            upgrade_rows[key] = _resolved_row(block, {
                "upgrades": sub_upgrades,
                "mappedImages": block_images,
                "localizedStrings": block_text,
                "audioDefinitions": block_audio,
                "fxLists": block_fx,
            })

    processed_sciences: set[str] = set()

    def resolve_sciences() -> None:
        while True:
            pending = [key for key in sorted(requested_sciences) if key not in processed_sciences]
            if not pending:
                return
            for key in pending:
                processed_sciences.add(key)
                requested = requested_sciences[key]
                matches = science_candidates.get(key, [])
                if not matches:
                    missing_sciences.add(requested)
                    continue
                if len(matches) != 1:
                    ambiguous_sciences.add(requested)
                    continue
                block = matches[0]
                prerequisites = tuple(
                    item for item in _tokens(block.values("PrerequisiteSciences"))
                    if item.startswith("SCIENCE_")
                )
                for identifier in prerequisites:
                    requested_sciences.setdefault(identifier.casefold(), identifier)
                block_text, block_audio = collect_shared(block)
                science_rows[key] = _resolved_row(block, {
                    "sciences": prerequisites,
                    "localizedStrings": block_text,
                    "audioDefinitions": block_audio,
                })

    resolve_sciences()
    for key in sorted(requested_powers):
        requested = requested_powers[key]
        matches = power_candidates.get(key, [])
        if not matches:
            missing_powers.add(requested)
            continue
        if len(matches) != 1:
            ambiguous_powers.add(requested)
            continue
        block = matches[0]
        required_sciences = tuple(
            item for item in _tokens(block.values("RequiredSciences"))
            if item.startswith("SCIENCE_")
        )
        for identifier in required_sciences:
            requested_sciences.setdefault(identifier.casefold(), identifier)
        block_text, block_audio = collect_shared(block)
        power_rows[key] = _resolved_row(block, {
            "sciences": required_sciences,
            "localizedStrings": block_text,
            "audioDefinitions": block_audio,
        })
    resolve_sciences()

    ordered = lambda rows: tuple(rows[key] for key in sorted(rows))
    sorted_ids = lambda values: tuple(sorted(values, key=lambda item: (item.casefold(), item)))
    return GameplayClosure(
        ordered(upgrade_rows), ordered(science_rows), ordered(power_rows),
        sorted_ids(mapped_images), sorted_ids(text_ids), sorted_ids(audio_roots),
        sorted_ids(fx_lists), sorted_ids(missing_upgrades),
        sorted_ids(ambiguous_upgrades), sorted_ids(missing_sciences),
        sorted_ids(ambiguous_sciences), sorted_ids(missing_powers),
        sorted_ids(ambiguous_powers),
    )
