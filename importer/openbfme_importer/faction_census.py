"""Payload-free command-reachable census for supported SAGE factions."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
from pathlib import Path
import re
from typing import Any, Iterable

from .big import sha256_file
from .catalog import CatalogEntry, InstallCatalog
from .game import retail_game
from .mapped_image import (
    resolve_mapped_image_texture_paths_partial,
    resolve_mapped_images_partial,
)
from .sage_audio import (
    normalize_faction_voice_event,
    parse_sage_audio_definitions,
    resolve_audio_sample_paths,
    resolve_audio_sample_paths_partial,
    resolve_sage_audio_closure,
)
from .sage_cst import SageCstError, parse_sage_document
from .sage_gameplay import resolve_gameplay_definition_closure
from .sage_ini import (
    IniBlock,
    MAX_INI_BYTES,
    _lines as _ini_lines,
    parse_flat_named_blocks,
    parse_object_definitions,
)
from .sage_string import parse_string_catalog


PLAYER_TEMPLATE_PATH = "data/ini/playertemplate.ini"
COMMAND_SET_PATH = "data/ini/commandset.ini"
COMMAND_BUTTON_PATH = "data/ini/commandbutton.ini"
SOUND_EFFECTS_PATH = "data/ini/soundeffects.ini"
VOICE_PATH = "data/ini/voice.ini"
STRING_CATALOG_PATH = "data/lotr.str"
UPGRADE_PATH = "data/ini/upgrade.ini"
SCIENCE_PATH = "data/ini/science.ini"
SPECIAL_POWER_PATH = "data/ini/specialpower.ini"
# Create-a-Hero powers live in a sibling document (both BFME2 and RotWK). Wild
# (and other sides) still reach CAH abilities through shared command surfaces.
CREATE_A_HERO_SPECIAL_POWER_PATH = "data/ini/createaherospecialpowers.ini"
FX_LIST_PATH = "data/ini/fxlist.ini"
# Weapon FireFX/ProjectileDetonationFX -> FXList -> Sound is where retail
# authors every melee swing and projectile impact sound; routing it here is
# what lets the playable-unit lane bind those AudioEvents per unit.
WEAPON_PATH = "data/ini/weapon.ini"
# Living-world autoresolve armies carry `Weapon = AutoResolve_*` slots whose
# definitions live in a sibling document, not weapon.ini. Indexing it keeps
# those references resolved facts instead of false "missing" claims.
AUTORESOLVE_WEAPON_PATH = "data/ini/livingworldautoresolveweapon.ini"
EVA_PATH = "data/ini/eva.ini"
MUSIC_PATH = "data/ini/music.ini"
MAPPED_IMAGE_PREFIX = "data/ini/mappedimages/"
MAX_OBJECT_DOCUMENTS = 4_096
MAX_TOTAL_OBJECT_INI_BYTES = 128 * 1024 * 1024
MAX_MAPPED_IMAGE_DOCUMENTS = 4_096
MAX_TOTAL_MAPPED_IMAGE_BYTES = 128 * 1024 * 1024
MAX_FX_LISTS = 4_096
MAX_EVA_EVENTS = 4_096
MAX_WEAPONS = 8_192

_IMPLICIT_MEN_ROOTS = (
    ("MenFortressCenterGeneric", "fortress-composite-center"),
    ("MenFortressCitadel", "fortress-composite-citadel"),
    ("MenFortressExpansionPadCorner", "fortress-composite-corner-pad"),
    ("MenFortressExpansionPadSide", "fortress-composite-side-pad"),
)

_OBJECT_EDGE_FIELDS = {
    "initialpayload": "horde-member",
    "bannercarriersallowed": "horde-banner",
    "segmenttemplatename": "wall-segment-template",
    "cliffcaptemplatename": "wall-cliff-cap-template",
    "gatetemplatename": "wall-gate-template",
    "posternfronttemplatename": "wall-postern-template",
    "posternbacktemplatename": "wall-postern-template",
    "towertemplatename": "wall-tower-template",
    "trebuchettemplatename": "wall-trebuchet-template",
}


@dataclass(frozen=True, slots=True)
class _SourceDocument:
    virtual_path: str
    archive: str
    size: int
    sha256: str
    source: bytes

    def public(self) -> dict[str, Any]:
        return {
            "virtualPath": self.virtual_path,
            "archive": self.archive,
            "size": self.size,
            "sha256": self.sha256,
        }


@dataclass(frozen=True, slots=True)
class _ObjectDefinition:
    block: IniBlock
    source: _SourceDocument


@dataclass(frozen=True, slots=True)
class PlayableFaction:
    """One effective playable PlayerTemplate and its side-owned Object count."""

    name: str
    side: str
    object_count: int

    @property
    def short_name(self) -> str:
        """Return the established faction alias derived from the template name."""

        value = self.name[7:] if self.name.casefold().startswith("faction") else self.side
        return value.casefold()


def _read_document(catalog: InstallCatalog, virtual_path: str) -> _SourceDocument:
    entry = catalog.resolve_exact(virtual_path)
    if entry is None:
        raise ValueError(
            f"catalog is missing required faction census input: {virtual_path}"
        )
    archive = catalog.open_archive_for(entry)
    source = archive.read_entry(catalog.as_entry(entry), max_bytes=MAX_INI_BYTES)
    return _SourceDocument(
        virtual_path=entry.name,
        archive=entry.archive,
        size=len(source),
        sha256=hashlib.sha256(source).hexdigest(),
        source=source,
    )


def _object_documents(catalog: InstallCatalog) -> list[_SourceDocument]:
    winners = _effective_entries(catalog)
    selected = [
        entry
        for entry in winners.values()
        if entry.name.casefold().startswith("data/ini/object/")
        and entry.name.casefold().endswith((".ini", ".inc"))
    ]
    selected.sort(key=lambda item: (item.name.casefold(), item.name))
    if len(selected) > MAX_OBJECT_DOCUMENTS:
        raise ValueError("faction census object document count exceeds limit")
    if sum(entry.size for entry in selected) > MAX_TOTAL_OBJECT_INI_BYTES:
        raise ValueError("faction census object document bytes exceed limit")
    return [_read_document(catalog, entry.name) for entry in selected]


def _effective_ini_documents(catalog: InstallCatalog) -> list[_SourceDocument]:
    """Return every effective ``.ini`` document for source-wide discovery."""

    selected = [
        entry
        for entry in _effective_entries(catalog).values()
        if entry.name.casefold().endswith(".ini")
    ]
    selected.sort(key=lambda item: (item.name.casefold(), item.name))
    if len(selected) > MAX_OBJECT_DOCUMENTS:
        raise ValueError("faction discovery INI document count exceeds limit")
    if sum(entry.size for entry in selected) > MAX_TOTAL_OBJECT_INI_BYTES:
        raise ValueError("faction discovery INI document bytes exceed limit")
    return [_read_document(catalog, entry.name) for entry in selected]


def _effective_entries(catalog: InstallCatalog) -> dict[str, CatalogEntry]:
    winners: dict[str, CatalogEntry] = {}
    for entry in sorted(
        catalog.entries,
        key=lambda item: (
            item.precedence,
            item.archive.casefold(),
            item.name.casefold(),
        ),
    ):
        winners.setdefault(entry.key, entry)
    return winners


def _mapped_image_documents(catalog: InstallCatalog) -> list[_SourceDocument]:
    selected = [
        entry
        for entry in _effective_entries(catalog).values()
        if entry.name.casefold().startswith(MAPPED_IMAGE_PREFIX)
        and entry.name.casefold().endswith(".ini")
    ]
    selected.sort(key=lambda item: (item.name.casefold(), item.name))
    if not selected:
        raise ValueError("catalog is missing mapped-image definition documents")
    if len(selected) > MAX_MAPPED_IMAGE_DOCUMENTS:
        raise ValueError("mapped-image document count exceeds limit")
    if sum(entry.size for entry in selected) > MAX_TOTAL_MAPPED_IMAGE_BYTES:
        raise ValueError("mapped-image document bytes exceed limit")
    return [_read_document(catalog, entry.name) for entry in selected]


def _definition_tokens(value: str) -> tuple[str, ...]:
    return tuple(re.findall(r"[A-Za-z0-9_][A-Za-z0-9_+.-]*", value))


_ADDITIVE_SOUND_PREFIX = re.compile(r"^\+\s*sound:(?P<identifier>.+)$", re.IGNORECASE)

# Behavior-module fields which reference FX lists on a spell book Object.
# Mirrors the effect-leaf taxonomy proven by the spellbook compiler lane.
_SPELLBOOK_FX_FIELDS = frozenset(
    {"triggerfx", "healfx", "elvenwoodfx", "taintfx", "fx"}
)


def _audio_reference_tokens(value: str) -> tuple[str, ...]:
    """Tokenize one audio field, honoring the additive SOUND namespace prefix.

    Retail voice fields author additive routes as ``+SOUND:EventId``; the
    prefix selects the sound-effects namespace and is not part of the
    definition identifier.  Every other value keeps its plain token stream.
    """

    match = _ADDITIVE_SOUND_PREFIX.match(value.strip())
    if match:
        return (match.group("identifier").strip(),)
    return _definition_tokens(value)


_FX_LIST_HEADER = re.compile(r"^FXList\s+([A-Za-z0-9_+.-]+)\s*$", re.IGNORECASE)
_EVA_EVENT_HEADER = re.compile(r"^NewEvaEvent\s+([A-Za-z0-9_+.-]+)\s*$", re.IGNORECASE)


def _eva_event_side_sounds(source: bytes) -> dict[str, tuple[str, tuple[tuple[str, str], ...]]]:
    """Index NewEvaEvent announcer sounds as ``(side, sound)`` pairs per event.

    Object ``EVA:<Event>`` voice values reference these blocks; each nested
    ``SideSound`` section binds one faction side to one audio definition.
    """

    events: dict[str, list[tuple[str, str]]] = {}
    authored_names: dict[str, str] = {}
    current: str | None = None
    in_side_sound = False
    side: str | None = None
    for line in _ini_lines(source):
        header = _EVA_EVENT_HEADER.fullmatch(line)
        if header is not None and current is None:
            current = header.group(1)
            authored_names.setdefault(current.casefold(), current)
            events.setdefault(current.casefold(), [])
            if len(events) > MAX_EVA_EVENTS:
                raise ValueError("EvaEvent document count exceeds limit")
            continue
        if current is None:
            continue
        if line.casefold() == "end":
            if in_side_sound:
                in_side_sound = False
                side = None
            else:
                current = None
            continue
        if "=" in line:
            key, _, value = line.partition("=")
            key = key.strip().casefold()
            if in_side_sound and key == "side":
                side = _first_identifier(value.strip())
            elif in_side_sound and key == "sound" and side:
                sound = _first_identifier(value.strip())
                if sound:
                    events[current.casefold()].append((side, sound))
            continue
        if line.split()[0].casefold() == "sidesound":
            in_side_sound = True
    return {
        key: (authored_names[key], tuple(pairs))
        for key, pairs in events.items()
    }


def _fx_list_sound_names(source: bytes) -> dict[str, tuple[str, tuple[str, ...]]]:
    """Index Sound nugget names per FXList without resolving FX semantics.

    fxlist.ini nests flat ``Sound`` sections inside each ``FXList`` body; a
    census only needs the authored ``Name`` references inside those sections
    to route their audio definitions.  The file is line-oriented SAGE INI:
    bare words open sections, ``End`` closes them.
    """

    sounds: dict[str, list[str]] = {}
    authored_names: dict[str, str] = {}
    current_list: str | None = None
    section_stack: list[str] = []
    for line in _ini_lines(source):
        header = _FX_LIST_HEADER.fullmatch(line)
        if header is not None and current_list is None:
            current_list = header.group(1)
            authored_names.setdefault(current_list.casefold(), current_list)
            sounds.setdefault(current_list.casefold(), [])
            if len(sounds) > MAX_FX_LISTS:
                raise ValueError("FXList document count exceeds limit")
            continue
        if current_list is None:
            continue
        if line.casefold() == "end":
            if section_stack:
                section_stack.pop()
            else:
                current_list = None
            continue
        if "=" in line:
            key, _, value = line.partition("=")
            if (
                key.strip().casefold() == "name"
                and "sound" in section_stack
                and (identifier := _first_identifier(value.strip()))
            ):
                sounds[current_list.casefold()].append(identifier)
            continue
        section_stack.append(line.split()[0].casefold())
    result: dict[str, tuple[str, tuple[str, ...]]] = {}
    for key, values in sounds.items():
        deduped = tuple(
            dict.fromkeys(sorted(values, key=lambda item: (item.casefold(), item)))
        )
        result[key] = (authored_names[key], deduped)
    return result


# Both retail weapon families: weapon.ini `Weapon <name>` and the
# living-world `AutoResolveWeapon <name>` blocks (abstract autoresolve damage
# tables; they author no FireFX, but indexing them keeps `Weapon =
# AutoResolve_*` object slots resolved facts instead of false missing claims).
_WEAPON_HEADER = re.compile(
    r"^(?:AutoResolve)?Weapon\s+([A-Za-z0-9_+.-]+)\s*$", re.IGNORECASE
)
_WEAPON_AUDIO_FX_FIELDS = {
    "firefx": "FireFX",
    "projectiledetonationfx": "ProjectileDetonationFX",
}
_WEAPON_VALUE_SENTINELS = frozenset({"none", "null"})


def _weapon_fire_fx_names(
    source: bytes,
) -> dict[str, tuple[str, tuple[tuple[str, str], ...]]]:
    """Index authored FireFX/ProjectileDetonationFX references per Weapon.

    Retail authors weapon audio OFF the object, on the weapon: ``Weapon
    BoromirSword / FireFX = FX_GondorSwordHit`` (pure retail
    weapon.ini:5616-5624).  The census only needs the FX-list names so the
    weapon -> FXList -> Sound chain can route its audio definitions; weapon
    semantics stay unresolved here.
    """

    weapons: dict[str, list[tuple[str, str]]] = {}
    authored_names: dict[str, str] = {}
    current: str | None = None
    section_stack: list[str] = []
    for line in _ini_lines(source):
        header = _WEAPON_HEADER.fullmatch(line)
        if header is not None and current is None:
            current = header.group(1)
            authored_names.setdefault(current.casefold(), current)
            weapons.setdefault(current.casefold(), [])
            if len(weapons) > MAX_WEAPONS:
                raise ValueError("Weapon document count exceeds limit")
            continue
        if current is None:
            continue
        if line.casefold() == "end":
            if section_stack:
                section_stack.pop()
            else:
                current = None
            continue
        if "=" in line:
            key, _, value = line.partition("=")
            field = _WEAPON_AUDIO_FX_FIELDS.get(key.strip().casefold())
            if field is not None:
                identifier = _first_identifier(value.strip())
                if (
                    identifier
                    and identifier.casefold() not in _WEAPON_VALUE_SENTINELS
                ):
                    weapons[current.casefold()].append((field, identifier))
            continue
        section_stack.append(line.split()[0].casefold())
    result: dict[str, tuple[str, tuple[tuple[str, str], ...]]] = {}
    for key, values in weapons.items():
        result[key] = (authored_names[key], tuple(dict.fromkeys(values)))
    return result


def _block_candidates(blocks: Iterable[IniBlock]) -> dict[str, list[IniBlock]]:
    result: dict[str, list[IniBlock]] = {}
    for block in blocks:
        candidates = result.setdefault(block.name.casefold(), [])
        # Retail 1.06 contains at least one byte-for-byte semantic duplicate
        # CommandButton.  It is one effective definition, not an ambiguity;
        # conflicting duplicates must still fail closed below.
        if block not in candidates:
            candidates.append(block)
    return result


def _first_identifier(value: str) -> str | None:
    token = value.split()[0] if value.split() else ""
    if not token or token.casefold() in {"none", "null", "0"} or token.startswith("$"):
        return None
    return token


def _identifiers(value: str) -> list[str]:
    return [
        token
        for token in value.split()
        if token.casefold() not in {"none", "null", "0"} and not token.startswith("$")
    ]


def _set_hash(domain: str, values: Iterable[str]) -> str:
    digest = hashlib.sha256(domain.encode("ascii") + b"\0")
    for value in sorted(set(values), key=lambda item: (item.casefold(), item)):
        encoded = value.encode("utf-8")
        digest.update(len(encoded).to_bytes(4, "little"))
        digest.update(encoded)
    return digest.hexdigest()


def _casefold_unique(values: Iterable[str]) -> tuple[str, ...]:
    """Return one deterministic authored spelling per SAGE identifier."""

    result: dict[str, str] = {}
    for value in sorted(set(values), key=lambda item: (item.casefold(), item)):
        result.setdefault(value.casefold(), value)
    return tuple(result.values())


def _block_values(block: IniBlock, key: str) -> list[str]:
    return list(block.values(key))


# Object UI image fields whose override must be scope-aware: a module-scoped
# value (the RespawnUpdate respawn portrait) is a real reference but never
# shadows an inherited top-level authored value.
_SCOPE_AWARE_OVERRIDE_FIELDS = frozenset({"buttonimage", "selectportrait"})


def _object_ui_top_level_fields(
    object_docs: Iterable[_SourceDocument],
) -> dict[tuple[str, str], frozenset[str]]:
    """Index strict-parse top-level UI field keys per (document, object).

    The flat IniBlock reader cannot distinguish a module-scoped assignment
    from the Object's own top-level one; the strict sage_cst reader can.
    Documents outside the strict grammar contribute no entries and their
    objects keep the legacy flat override behavior.
    """

    scope: dict[tuple[str, str], frozenset[str]] = {}
    for document in object_docs:
        try:
            parsed = parse_sage_document(document.source, document.virtual_path)
        except SageCstError:
            continue
        for item in parsed.objects:
            scope[(document.virtual_path.casefold(), item.name.casefold())] = (
                frozenset(
                    assignment.key.casefold()
                    for assignment in item.assignments
                    if assignment.key.casefold() in _SCOPE_AWARE_OVERRIDE_FIELDS
                )
            )
    return scope


_OBJECT_AUDIO_VALUE_FIELDS = frozenset(
    value.casefold()
    for value in (
        "ActiveLoopSound",
        "BeingBuiltSound",
        "EnterSound",
        "ExitSound",
        "InitiateSound",
        "SelfBuildingLoop",
        "SelfRepairFromDamageLoop",
        "SelfRepairFromRubbleLoop",
        "SoundAmbient",
        "SoundAmbientDamaged",
        "SoundAmbientReallyDamaged",
        "SoundClosingGateLoop",
        "SoundCreated",
        "SoundCrushing",
        "SoundDeploy",
        "SoundFinishedClosingGate",
        "SoundFinishedOpeningGate",
        "SoundImpact",
        "SoundMoveLoop",
        "SoundMoveStart",
        "SoundOnDamaged",
        "SoundOnReallyDamaged",
        "SoundOpeningGateLoop",
        "SoundStealthOff",
        "SoundStealthOn",
        "SoundToPlay",
        "SoundUndeploy",
        "SpeedBonusAudioLoop",
        "TriggerSound",
    )
)
_COMMAND_AUDIO_VALUE_FIELDS = frozenset(
    {"setautoabilityunitsound", "unitspecificsound"}
)
_AUDIO_SENTINELS = frozenset({"none", "null", "nosound", "0"})


def _object_audio_reference_ordinals(field: str) -> tuple[int, ...]:
    """Return token positions defined by the BFME2 Object audio field schema."""

    folded = field.casefold()
    if folded == "initiatevoice":
        return (0,)
    if folded.startswith("voice") and folded != "voicepriority":
        return (0,)
    if folded == "animationsound":
        return (1,)  # ``Sound: Event Animation: ...``
    if folded == "sound":
        return (1,)  # ``Sound = INITIAL Event``
    if folded in _OBJECT_AUDIO_VALUE_FIELDS:
        return (0,)
    return ()


def _command_audio_reference_ordinals(
    field: str, tokens: tuple[str, ...]
) -> tuple[int, ...]:
    folded = field.casefold()
    if folded == "unitspecificsound":
        return tuple(range(len(tokens)))
    return (0,) if folded in _COMMAND_AUDIO_VALUE_FIELDS else ()


def _effective_object_assignments(
    definition: _ObjectDefinition,
    candidates: dict[str, list[_ObjectDefinition]],
    ui_top_level_fields: dict[tuple[str, str], frozenset[str]] | None = None,
) -> tuple[
    list[tuple[str, str, _ObjectDefinition]],
    list[_ObjectDefinition],
    tuple[str, str] | None,
]:
    """Resolve all inherited assignment families with child override semantics.

    UI image fields (``SelectPortrait``/``ButtonImage``) are scope-aware: a
    module-scoped value (for example the RespawnUpdate respawn portrait) is a
    genuine reference but never overrides an inherited top-level authored
    value.  ``ui_top_level_fields`` carries the strict-parse top-level keys
    per (document, object); objects without an entry keep the legacy flat
    behavior where any presence selects the field.
    """

    current = definition
    ancestry: list[_ObjectDefinition] = []
    seen = {current.block.name.casefold()}
    selected_fields: set[str] = set()
    effective: list[tuple[str, str, _ObjectDefinition]] = []
    supplemental: list[tuple[str, str, _ObjectDefinition]] = []
    while True:
        assignments_by_field: dict[str, list[tuple[str, str]]] = {}
        for field, value in current.block.assignments:
            assignments_by_field.setdefault(field.casefold(), []).append((field, value))
        top_level = (
            ui_top_level_fields.get(
                (
                    current.source.virtual_path.casefold(),
                    current.block.name.casefold(),
                )
            )
            if ui_top_level_fields is not None
            else None
        )
        for folded, assignments in assignments_by_field.items():
            if folded in selected_fields:
                continue
            if (
                folded in _SCOPE_AWARE_OVERRIDE_FIELDS
                and top_level is not None
                and folded not in top_level
            ):
                supplemental.extend(
                    (field, value, current) for field, value in assignments
                )
                continue
            selected_fields.add(folded)
            effective.extend((field, value, current) for field, value in assignments)
        parent = current.block.parent
        if not parent:
            return [*effective, *supplemental], ancestry, None
        key = parent.casefold()
        if key in seen:
            raise ValueError(
                f"Object inheritance cycle while resolving {definition.block.name}"
            )
        seen.add(key)
        matches = candidates.get(key, [])
        if not matches:
            return [*effective, *supplemental], ancestry, ("missing", parent)
        if len(matches) != 1:
            return [*effective, *supplemental], ancestry, ("ambiguous", parent)
        current = matches[0]
        ancestry.append(current)


def _is_playable_template(block: IniBlock) -> bool:
    """Apply the retail-authored playable predicate without truthy coercion."""

    values = _block_values(block, "PlayableSide")
    if not values:
        return False
    identifiers = [_first_identifier(value) for value in values]
    if len(identifiers) != 1 or identifiers[0] is None:
        raise ValueError(
            f"PlayerTemplate {block.name} has ambiguous PlayableSide assignments"
        )
    value = identifiers[0].casefold()
    if value == "yes":
        return True
    if value == "no":
        return False
    raise ValueError(
        f"PlayerTemplate {block.name} has unsupported PlayableSide value: "
        f"{identifiers[0]!r}"
    )


def discover_playable_factions(
    catalog: InstallCatalog,
) -> tuple[PlayableFaction, ...]:
    """Discover playable factions from the effective PlayerTemplate document.

    Retail BFME2/RotWK explicitly distinguish skirmish factions from observer,
    civilian, tutorial, and incomplete template rows with ``PlayableSide =
    Yes``.  Admission therefore requires that exact authored predicate plus a
    single valid ``Side`` assignment; names and sides are never inferred from a
    built-in faction list.  Object counts resolve each effective Object's
    inherited ``Side`` with the same child-override semantics as the census.
    """

    player_doc = _read_document(catalog, PLAYER_TEMPLATE_PATH)
    candidates = _block_candidates(
        parse_flat_named_blocks(player_doc.source, "PlayerTemplate")
    )
    playable: list[tuple[str, str]] = []
    for key in sorted(candidates):
        blocks = candidates[key]
        if len(blocks) != 1:
            raise ValueError(
                "effective PlayerTemplate input has ambiguous "
                f"definition: {blocks[0].name}"
            )
        block = blocks[0]
        if not _is_playable_template(block):
            continue
        side_values = [
            identifier
            for value in _block_values(block, "Side")
            if (identifier := _first_identifier(value)) is not None
        ]
        if len(side_values) != 1:
            raise ValueError(
                f"playable PlayerTemplate {block.name} must have exactly one valid Side"
            )
        playable.append((block.name, side_values[0]))

    object_candidates: dict[str, list[_ObjectDefinition]] = {}
    for document in _effective_ini_documents(catalog):
        for block in parse_object_definitions(document.source):
            object_candidates.setdefault(block.name.casefold(), []).append(
                _ObjectDefinition(block, document)
            )
    resolved_sides: dict[str, str | None] = {}

    def _object_side(
        definition: _ObjectDefinition, trail: tuple[str, ...] = ()
    ) -> str | None:
        key = definition.block.name.casefold()
        if key in resolved_sides:
            return resolved_sides[key]
        if key in trail:
            resolved_sides[key] = None
            return None
        own = [
            identifier
            for field, value in definition.block.assignments
            if field.casefold() == "side"
            if (identifier := _first_identifier(value)) is not None
        ]
        if own:
            resolved_sides[key] = own[-1]
            return own[-1]
        parent = definition.block.parent
        matches = object_candidates.get(parent.casefold(), []) if parent else []
        if parent and len(matches) == 1:
            resolved_sides[key] = _object_side(matches[0], (*trail, key))
        else:
            resolved_sides[key] = None
        return resolved_sides[key]

    objects_by_side: dict[str, set[str]] = {}
    for definitions in object_candidates.values():
        for definition in definitions:
            side = _object_side(definition)
            if side is None:
                continue
            objects_by_side.setdefault(side.casefold(), set()).add(
                definition.block.name.casefold()
            )

    return tuple(
        PlayableFaction(
            name=name,
            side=side,
            object_count=len(objects_by_side.get(side.casefold(), set())),
        )
        for name, side in sorted(
            playable, key=lambda item: (item[0].casefold(), item[0])
        )
    )


def resolve_playable_faction(
    catalog: InstallCatalog, value: str
) -> PlayableFaction:
    """Resolve a side/template/short alias against discovered playable data."""

    key = value.casefold().strip()
    if not key:
        raise ValueError("playable faction selector is empty")
    matches = [
        faction
        for faction in discover_playable_factions(catalog)
        if key
        in {
            faction.name.casefold(),
            faction.side.casefold(),
            faction.short_name,
        }
    ]
    if len(matches) != 1:
        raise ValueError(f"unsupported playable faction: {value!r}")
    return matches[0]


def _census_playable_faction(
    catalog: InstallCatalog,
    *,
    player_template: str,
    game: str = "bfme2",
    expected_side: str | None = None,
    implicit_object_roots: Iterable[tuple[str, str]] = (),
    source_null_mapped_image_textures: Iterable[tuple[str, str]] = (),
    source_null_command_sets: Iterable[tuple[str, str]] = (),
    music_roots: Iterable[tuple[str, str]] = (),
    _legacy_men_identity: bool = False,
) -> dict[str, Any]:
    """Return neutral command/UI dependency facts without retail INI bodies.

    ``implicit_object_roots`` is deliberately caller-owned policy.  These are
    engine-created composite objects which cannot be discovered from command
    buttons; guessing them from a faction name would silently hide graph gaps.

    ``source_null_mapped_image_textures`` and ``source_null_command_sets``
    are equally caller-owned: retail 1.06 authors references to a UI atlas or
    CommandSet it never ships (placeholder button art, the Isengard side-pad
    CommandSet).  Each policy entry must name the exact authored identifier
    and is only consumed when the reference is genuinely absent from the
    effective catalog; a policy entry which suddenly resolves fails closed.

    ``music_roots`` declares the engine-level skirmish music loops (shell and
    load-screen).  Each declared root must resolve through the merged
    MusicTrack/Multisound namespace; anything else fails closed.
    """

    if not re.fullmatch(r"[A-Za-z0-9_+.-]+", player_template):
        raise ValueError(f"invalid PlayerTemplate identifier: {player_template!r}")
    game_definition = retail_game(game)

    def _policy_entries(
        entries: Iterable[tuple[str, str]], label: str
    ) -> dict[str, tuple[str, str]]:
        by_key: dict[str, tuple[str, str]] = {}
        for raw_identifier, raw_reason in entries:
            identifier, reason = str(raw_identifier), str(raw_reason)
            if not re.fullmatch(r"[A-Za-z0-9_+.-]+", identifier):
                raise ValueError(f"invalid {label} identifier: {identifier!r}")
            if not reason or any(character in reason for character in "\r\n"):
                raise ValueError(f"invalid {label} reason: {reason!r}")
            key = identifier.casefold()
            candidate = (identifier, reason)
            previous = by_key.get(key)
            if previous is not None and previous != candidate:
                raise ValueError(
                    f"case-colliding {label} policy entries: "
                    f"{previous[0]!r} and {identifier!r}"
                )
            by_key[key] = candidate
        return by_key

    implicit_roots_by_key: dict[str, tuple[str, str]] = {}
    for raw_identifier, raw_reason in implicit_object_roots:
        identifier, reason = str(raw_identifier), str(raw_reason)
        if not re.fullmatch(r"[A-Za-z0-9_+.-]+", identifier):
            raise ValueError(f"invalid implicit object root: {identifier!r}")
        if not reason or any(character in reason for character in "\r\n"):
            raise ValueError(f"invalid implicit object root reason: {reason!r}")
        key = identifier.casefold()
        candidate = (identifier, reason)
        previous = implicit_roots_by_key.get(key)
        if previous is not None and previous != candidate:
            raise ValueError(
                "case-colliding implicit object roots: "
                f"{previous[0]!r} and {identifier!r}"
            )
        implicit_roots_by_key[key] = candidate
    normalized_implicit_roots = tuple(
        sorted(
            implicit_roots_by_key.values(),
            key=lambda item: (item[0].casefold(), item[0], item[1]),
        )
    )
    source_null_texture_policy = _policy_entries(
        source_null_mapped_image_textures, "source-null MappedImage texture"
    )
    source_null_command_set_policy = _policy_entries(
        source_null_command_sets, "source-null CommandSet"
    )
    music_root_policy = _policy_entries(music_roots, "music root")

    player_doc = _read_document(catalog, PLAYER_TEMPLATE_PATH)
    command_set_doc = _read_document(catalog, COMMAND_SET_PATH)
    command_button_doc = _read_document(catalog, COMMAND_BUTTON_PATH)
    sound_effects_doc = _read_document(catalog, SOUND_EFFECTS_PATH)
    voice_doc = _read_document(catalog, VOICE_PATH)
    string_catalog_doc = _read_document(catalog, STRING_CATALOG_PATH)
    upgrade_doc = _read_document(catalog, UPGRADE_PATH)
    science_doc = _read_document(catalog, SCIENCE_PATH)
    special_power_doc = _read_document(catalog, SPECIAL_POWER_PATH)
    create_a_hero_power_entry = catalog.resolve_exact(CREATE_A_HERO_SPECIAL_POWER_PATH)
    if create_a_hero_power_entry is not None:
        create_a_hero_power_doc = _read_document(
            catalog, CREATE_A_HERO_SPECIAL_POWER_PATH
        )
        special_power_source = (
            special_power_doc.source + b"\n" + create_a_hero_power_doc.source
        )
    else:
        special_power_source = special_power_doc.source
    fx_list_doc = _read_document(catalog, FX_LIST_PATH)
    # weapon.ini is optional at census level (mirrors the Create-a-Hero power
    # document): absent means no weapon audio chain is claimed, present means
    # every command-reachable object's WeaponSet weapons route their
    # FireFX/ProjectileDetonationFX sounds.
    weapon_doc = (
        _read_document(catalog, WEAPON_PATH)
        if catalog.resolve_exact(WEAPON_PATH) is not None
        else None
    )
    autoresolve_weapon_doc = (
        _read_document(catalog, AUTORESOLVE_WEAPON_PATH)
        if weapon_doc is not None
        and catalog.resolve_exact(AUTORESOLVE_WEAPON_PATH) is not None
        else None
    )
    eva_doc = _read_document(catalog, EVA_PATH)
    music_doc = _read_document(catalog, MUSIC_PATH)
    mapped_image_docs = _mapped_image_documents(catalog)
    # Object Voice* fields resolve through voice.ini while impacts, footsteps,
    # construction sounds, and other world SFX resolve through
    # soundeffects.ini. Treat the two retail definition documents as one
    # namespace so cross-document Multisound edges stay exact and duplicate
    # identifiers fail closed in the shared parser.  music.ini adds the
    # MusicTrack records and shell Multisound loops to the same namespace.
    audio_definitions = parse_sage_audio_definitions(
        sound_effects_doc.source + b"\n" + voice_doc.source + b"\n" + music_doc.source
    )
    fx_list_sounds = _fx_list_sound_names(fx_list_doc.source)
    weapon_fire_fx = (
        _weapon_fire_fx_names(
            weapon_doc.source
            + (
                b"\n" + autoresolve_weapon_doc.source
                if autoresolve_weapon_doc is not None
                else b""
            )
        )
        if weapon_doc is not None
        else {}
    )
    eva_events = _eva_event_side_sounds(eva_doc.source)
    string_catalog = parse_string_catalog(
        string_catalog_doc.source, duplicate_policy="first-wins", strict=False
    )
    string_identifier_names = {
        record.identifier.casefold(): record.identifier
        for record in string_catalog.records
    }
    audio_definition_names = {
        item.id.casefold(): item.id
        for item in (
            *audio_definitions.events,
            *audio_definitions.multisounds,
            *audio_definitions.tracks,
        )
    }
    player_templates = _block_candidates(
        parse_flat_named_blocks(player_doc.source, "PlayerTemplate")
    )
    command_sets = _block_candidates(
        parse_flat_named_blocks(command_set_doc.source, "CommandSet")
    )
    command_buttons = _block_candidates(
        parse_flat_named_blocks(command_button_doc.source, "CommandButton")
    )
    template_candidates = player_templates.get(player_template.casefold(), [])
    if not template_candidates:
        raise ValueError(f"effective PlayerTemplate input has no {player_template}")
    if len(template_candidates) != 1:
        raise ValueError(
            f"effective PlayerTemplate input has ambiguous {player_template} definitions"
        )
    template = template_candidates[0]
    side = (
        _first_identifier(_block_values(template, "Side")[0])
        if _block_values(template, "Side")
        else None
    )
    if side is None:
        raise ValueError(f"{player_template} has no valid Side")
    if expected_side is not None and side != expected_side:
        raise ValueError(
            f"{player_template} Side must be {expected_side}, got {side!r}"
        )

    object_docs = _object_documents(catalog)
    object_candidates: dict[str, list[_ObjectDefinition]] = {}
    for document in object_docs:
        for block in parse_object_definitions(document.source):
            object_candidates.setdefault(block.name.casefold(), []).append(
                _ObjectDefinition(block, document)
            )
    ui_top_level_fields = _object_ui_top_level_fields(object_docs)

    roots: list[dict[str, str]] = []
    roster_entries: list[dict[str, Any]] = []
    roster_ordinals: dict[str, int] = {}
    object_ids: set[str] = set()
    command_set_ids: set[str] = set()
    missing_audio_definitions: set[str] = set()
    missing_eva_events: set[str] = set()
    missing_fx_lists: set[str] = set()
    missing_weapon_definitions: set[str] = set()
    spellbook_object_keys: set[str] = set()
    spellbook_fx_lists: set[str] = set()
    sciences: set[str] = set()
    intrinsic_sciences: set[str] = set()
    for assignment_ordinal, (field, value) in enumerate(template.assignments):
        folded = field.casefold()
        starting_unit_suffix = folded.removeprefix("startingunit")
        if folded == "startingbuilding" or (
            folded.startswith("startingunit") and starting_unit_suffix.isdigit()
        ):
            identifier = _first_identifier(value)
            if identifier:
                object_ids.add(identifier)
                roots.append(
                    {"sourceField": field, "id": identifier, "edgeKind": "object"}
                )
        elif folded in {
            "buildableheroesmp",
            "buildableringheroesmp",
            "spellbookmp",
            "ringhero",
        }:
            for token_ordinal, identifier in enumerate(_identifiers(value)):
                object_ids.add(identifier)
                roots.append(
                    {"sourceField": field, "id": identifier, "edgeKind": "object"}
                )
                if folded == "spellbookmp":
                    spellbook_object_keys.add(identifier.casefold())
                if folded in {
                    "buildableheroesmp",
                    "buildableringheroesmp",
                    "ringhero",
                }:
                    roster_ordinal = roster_ordinals.get(folded, 0)
                    roster_entries.append(
                        {
                            "sourceField": field,
                            "assignmentOrdinal": assignment_ordinal,
                            "tokenOrdinal": token_ordinal,
                            "rosterOrdinal": roster_ordinal,
                            "id": identifier,
                        }
                    )
                    roster_ordinals[folded] = roster_ordinal + 1
        elif folded == "purchasesciencecommandsetmp":
            identifier = _first_identifier(value)
            if identifier:
                command_set_ids.add(identifier)
                roots.append(
                    {"sourceField": field, "id": identifier, "edgeKind": "command-set"}
                )
        elif folded == "intrinsicsciencesmp":
            for identifier in _identifiers(value):
                if identifier.startswith("SCIENCE_"):
                    sciences.add(identifier)
                    intrinsic_sciences.add(identifier)
                    roots.append(
                        {"sourceField": field, "id": identifier, "edgeKind": "science"}
                    )
    for identifier, reason in normalized_implicit_roots:
        object_ids.add(identifier)
        roots.append(
            {
                "sourceField": reason,
                "id": identifier,
                "edgeKind": "engine-implicit-object",
            }
        )
    declared_music_roots: list[dict[str, str]] = []
    music_audio_ids: set[str] = set()
    for key in sorted(music_root_policy):
        identifier, reason = music_root_policy[key]
        audio_id = audio_definition_names.get(key)
        if audio_id is None:
            raise ValueError(
                f"declared music root does not resolve in the effective "
                f"catalog: {identifier}"
            )
        music_audio_ids.add(audio_id)
        roots.append(
            {
                "sourceField": reason,
                "id": audio_id,
                "edgeKind": "engine-music-root",
            }
        )
        declared_music_roots.append(
            {"id": audio_id, "reason": reason}
        )

    processed_objects: set[str] = set()
    processed_command_sets: set[str] = set()
    processed_buttons: set[str] = set()
    ambiguous_objects: set[str] = set()
    missing_objects: set[str] = set()
    missing_command_sets: set[str] = set()
    missing_buttons: set[str] = set()
    ambiguous_command_sets: set[str] = set()
    ambiguous_buttons: set[str] = set()
    missing_inheritance_objects: set[str] = set()
    ambiguous_inheritance_objects: set[str] = set()
    consumed_source_null_command_sets: set[str] = set()
    upgrades: set[str] = set()
    special_powers: set[str] = set()
    mapped_images: set[str] = set()
    nullable_portrait_images: set[str] = set()
    required_mapped_images: set[str] = set()
    text_ids: set[str] = set()
    audio_roots: set[str] = set()
    for _audio_id in music_audio_ids:
        audio_roots.add(_audio_id)
    object_rows: dict[str, dict[str, Any]] = {}
    command_set_rows: dict[str, dict[str, Any]] = {}
    command_button_rows: dict[str, dict[str, Any]] = {}

    while True:
        changed = False
        for identifier in sorted(object_ids, key=lambda item: (item.casefold(), item)):
            key = identifier.casefold()
            if key in processed_objects:
                continue
            processed_objects.add(key)
            changed = True
            candidates = object_candidates.get(key, [])
            if not candidates:
                missing_objects.add(identifier)
                continue
            if len(candidates) != 1:
                ambiguous_objects.add(identifier)
                continue
            definition = candidates[0]
            block = definition.block
            edge_rows: list[dict[str, str]] = []
            weapon_edge_keys: set[tuple[str, str]] = set()
            if block.parent:
                edge_rows.append(
                    {
                        "field": "parent",
                        "targetKind": "object",
                        "targetId": block.parent,
                    }
                )
            effective_assignments, ancestry, inheritance_problem = (
                _effective_object_assignments(
                    definition, object_candidates, ui_top_level_fields
                )
            )
            if inheritance_problem:
                problem, target = inheritance_problem
                if problem == "missing":
                    missing_inheritance_objects.add(target)
                else:
                    ambiguous_inheritance_objects.add(target)
            for field, value, supplier in effective_assignments:
                folded = field.casefold()
                inherited = supplier.block.name.casefold() != block.name.casefold()
                if folded == "commandset":
                    target = _first_identifier(value)
                    if target:
                        command_set_ids.add(target)
                        edge = {
                            "field": field,
                            "targetKind": "command-set",
                            "targetId": target,
                        }
                        if inherited:
                            edge["sourceObjectId"] = supplier.block.name
                        edge_rows.append(edge)
                if folded in _OBJECT_EDGE_FIELDS:
                    target = _first_identifier(value)
                    if target:
                        object_ids.add(target)
                        edge = {
                            "field": field,
                            "targetKind": _OBJECT_EDGE_FIELDS[folded],
                            "targetId": target,
                        }
                        if inherited:
                            edge["sourceObjectId"] = supplier.block.name
                        edge_rows.append(edge)
                if folded in {"selectportrait", "buttonimage"}:
                    target = _first_identifier(value)
                    if target:
                        mapped_images.add(target)
                        if folded == "selectportrait":
                            nullable_portrait_images.add(target)
                        else:
                            required_mapped_images.add(target)
                        edge = {
                            "field": field,
                            "targetKind": "mapped-image",
                            "targetId": target,
                        }
                        if inherited:
                            edge["sourceObjectId"] = supplier.block.name
                        edge_rows.append(edge)
                target = _first_identifier(value)
                if target:
                    text_id = string_identifier_names.get(target.casefold())
                    if text_id is not None:
                        text_ids.add(text_id)
                        edge = {
                            "field": field,
                            "targetKind": "localized-string",
                            "targetId": text_id,
                        }
                        if inherited:
                            edge["sourceObjectId"] = supplier.block.name
                        edge_rows.append(edge)
                tokens = _audio_reference_tokens(value)
                for ordinal in _object_audio_reference_ordinals(field):
                    stripped_value = value.lstrip()
                    if stripped_value.casefold().startswith("eva:"):
                        if ordinal != 0:
                            continue
                        eva_token = _first_identifier(stripped_value[4:])
                        if not eva_token:
                            continue
                        eva_record = eva_events.get(eva_token.casefold())
                        eva_edge = {
                            "field": field,
                            "targetKind": "eva-event",
                            "targetId": (
                                eva_record[0] if eva_record is not None else eva_token
                            ),
                            "resolution": (
                                "resolved" if eva_record is not None else "unresolved"
                            ),
                        }
                        if inherited:
                            eva_edge["sourceObjectId"] = supplier.block.name
                        edge_rows.append(eva_edge)
                        if eva_record is None:
                            missing_eva_events.add(eva_token)
                            continue
                        side_keys = {
                            side.casefold(),
                            f"player{side.casefold()}",
                        }
                        for sound_side, sound_token in eva_record[1]:
                            if sound_side.casefold() not in side_keys:
                                continue
                            audio_id = audio_definition_names.get(
                                sound_token.casefold()
                            )
                            if audio_id is not None:
                                audio_roots.add(audio_id)
                                target_id = audio_id
                                resolution = "resolved"
                            else:
                                target_id = sound_token
                                resolution = "unresolved"
                                missing_audio_definitions.add(sound_token)
                            sound_edge = {
                                "field": f"EvaEvent:{eva_record[0]}",
                                "targetKind": "audio-definition",
                                "targetId": target_id,
                                "resolution": resolution,
                            }
                            if inherited:
                                sound_edge["sourceObjectId"] = supplier.block.name
                            edge_rows.append(sound_edge)
                        continue
                    if ordinal >= len(tokens):
                        continue
                    token = normalize_faction_voice_event(
                        block.name, field, tokens[ordinal]
                    )
                    if token.casefold() in _AUDIO_SENTINELS:
                        continue
                    audio_id = audio_definition_names.get(token.casefold())
                    if audio_id is not None:
                        audio_roots.add(audio_id)
                        target_id = audio_id
                        resolution = "resolved"
                    else:
                        target_id = token
                        resolution = "unresolved"
                        missing_audio_definitions.add(token)
                    edge = {
                        "field": field,
                        "targetKind": "audio-definition",
                        "targetId": target_id,
                        "resolution": resolution,
                    }
                    if inherited:
                        edge["sourceObjectId"] = supplier.block.name
                    edge_rows.append(edge)
                if key in spellbook_object_keys and folded in _SPELLBOOK_FX_FIELDS:
                    fx_target = _first_identifier(value)
                    if fx_target:
                        spellbook_fx_lists.add(fx_target)
                        edge = {
                            "field": field,
                            "targetKind": "fx-list",
                            "targetId": fx_target,
                        }
                        if inherited:
                            edge["sourceObjectId"] = supplier.block.name
                        edge_rows.append(edge)
                        fx_record = fx_list_sounds.get(fx_target.casefold())
                        if fx_record is None:
                            missing_fx_lists.add(fx_target)
                        else:
                            for sound_token in fx_record[1]:
                                audio_id = audio_definition_names.get(
                                    sound_token.casefold()
                                )
                                if audio_id is not None:
                                    audio_roots.add(audio_id)
                                    target_id = audio_id
                                    resolution = "resolved"
                                else:
                                    target_id = sound_token
                                    resolution = "unresolved"
                                    missing_audio_definitions.add(sound_token)
                                sound_edge = {
                                    "field": f"FXList:{fx_record[0]}",
                                    "targetKind": "audio-definition",
                                    "targetId": target_id,
                                    "resolution": resolution,
                                }
                                if inherited:
                                    sound_edge["sourceObjectId"] = (
                                        supplier.block.name
                                    )
                                edge_rows.append(sound_edge)
                if folded == "weapon" and weapon_doc is not None:
                    # WeaponSet slot rows ("Weapon = PRIMARY BoromirSword"):
                    # the weapon name is the LAST identifier token. Route its
                    # authored FireFX/ProjectileDetonationFX FXList sounds so
                    # weapon audio events become resolvable pack leaves.
                    weapon_tokens = re.findall(r"[A-Za-z0-9_+.-]+", value)
                    weapon_token = weapon_tokens[-1] if weapon_tokens else None
                    if (
                        weapon_token
                        and weapon_token.casefold()
                        not in _WEAPON_VALUE_SENTINELS
                    ):
                        weapon_record = weapon_fire_fx.get(
                            weapon_token.casefold()
                        )
                        if weapon_record is None:
                            missing_weapon_definitions.add(weapon_token)
                        else:
                            for fx_field, fx_target in weapon_record[1]:
                                fx_record = fx_list_sounds.get(
                                    fx_target.casefold()
                                )
                                if fx_record is None:
                                    missing_fx_lists.add(fx_target)
                                    continue
                                for sound_token in fx_record[1]:
                                    audio_id = audio_definition_names.get(
                                        sound_token.casefold()
                                    )
                                    if audio_id is not None:
                                        audio_roots.add(audio_id)
                                        target_id = audio_id
                                        resolution = "resolved"
                                    else:
                                        target_id = sound_token
                                        resolution = "unresolved"
                                        missing_audio_definitions.add(
                                            sound_token
                                        )
                                    edge_field = (
                                        f"Weapon:{weapon_record[0]}:{fx_field}"
                                    )
                                    if (edge_field, target_id.casefold()) in (
                                        weapon_edge_keys
                                    ):
                                        continue
                                    weapon_edge_keys.add(
                                        (edge_field, target_id.casefold())
                                    )
                                    sound_edge = {
                                        "field": edge_field,
                                        "targetKind": "audio-definition",
                                        "targetId": target_id,
                                        "resolution": resolution,
                                        "fxListId": fx_record[0],
                                    }
                                    if inherited:
                                        sound_edge["sourceObjectId"] = (
                                            supplier.block.name
                                        )
                                    edge_rows.append(sound_edge)
            edge_rows.sort(
                key=lambda item: (
                    item["field"].casefold(),
                    item["targetId"].casefold(),
                    item.get("sourceObjectId", "").casefold(),
                )
            )
            object_rows[key] = {
                "id": block.name,
                "definitionKind": block.kind,
                "parentId": block.parent,
                "source": {
                    "archive": definition.source.archive,
                    "virtualPath": definition.source.virtual_path,
                    "sha256": definition.source.sha256,
                },
                "inheritanceSources": [
                    {
                        "id": ancestor.block.name,
                        "archive": ancestor.source.archive,
                        "virtualPath": ancestor.source.virtual_path,
                        "sha256": ancestor.source.sha256,
                    }
                    for ancestor in ancestry
                ],
                "edges": edge_rows,
            }

        for identifier in sorted(
            command_set_ids, key=lambda item: (item.casefold(), item)
        ):
            key = identifier.casefold()
            if key in processed_command_sets:
                continue
            processed_command_sets.add(key)
            changed = True
            candidates = command_sets.get(key, [])
            if not candidates:
                if key in source_null_command_set_policy:
                    consumed_source_null_command_sets.add(key)
                    continue
                missing_command_sets.add(identifier)
                continue
            if len(candidates) != 1:
                ambiguous_command_sets.add(identifier)
                continue
            block = candidates[0]
            buttons: list[str] = []
            for _, value in block.assignments:
                target = _first_identifier(value)
                if target and target.startswith("Command_"):
                    buttons.append(target)
            command_set_rows[key] = {
                "id": block.name,
                "buttons": sorted(set(buttons), key=str.casefold),
            }

        all_buttons = {
            button for row in command_set_rows.values() for button in row["buttons"]
        }
        for identifier in sorted(all_buttons, key=lambda item: (item.casefold(), item)):
            key = identifier.casefold()
            if key in processed_buttons:
                continue
            processed_buttons.add(key)
            changed = True
            candidates = command_buttons.get(key, [])
            if not candidates:
                missing_buttons.add(identifier)
                continue
            if len(candidates) != 1:
                ambiguous_buttons.add(identifier)
                continue
            block = candidates[0]
            selected: dict[str, list[str]] = {}
            for field in (
                "Command",
                "Object",
                "Upgrade",
                "SpecialPower",
                "Science",
                "ButtonImage",
                "TextLabel",
                "DescriptLabel",
            ):
                values = _block_values(block, field)
                if values:
                    selected[field] = values
            button_audio_references: set[str] = set()
            button_audio_routes: list[dict[str, object]] = []
            for field, value in block.assignments:
                tokens = _audio_reference_tokens(value)
                for ordinal in _command_audio_reference_ordinals(field, tokens):
                    if ordinal >= len(tokens):
                        continue
                    token = tokens[ordinal]
                    if token.casefold() in _AUDIO_SENTINELS:
                        continue
                    audio_id = audio_definition_names.get(token.casefold())
                    if audio_id is not None:
                        audio_roots.add(audio_id)
                        button_audio_references.add(audio_id)
                        target_id = audio_id
                        resolution = "resolved"
                    else:
                        target_id = token
                        resolution = "unresolved"
                        missing_audio_definitions.add(token)
                    button_audio_routes.append(
                        {
                            "field": field,
                            "targetId": target_id,
                            "tokenOrdinal": ordinal,
                            "resolution": resolution,
                        }
                    )
            for value in selected.get("Object", []):
                target = _first_identifier(value)
                if target:
                    object_ids.add(target)
            for value in selected.get("Upgrade", []):
                upgrades.update(_identifiers(value))
            for value in selected.get("SpecialPower", []):
                special_powers.update(_identifiers(value))
            for value in selected.get("Science", []):
                sciences.update(
                    identifier
                    for identifier in _identifiers(value)
                    if identifier.startswith("SCIENCE_")
                )
            for value in selected.get("ButtonImage", []):
                target = _first_identifier(value)
                if target:
                    mapped_images.add(target)
                    required_mapped_images.add(target)
            for field in ("TextLabel", "DescriptLabel"):
                for value in selected.get(field, []):
                    target = _first_identifier(value)
                    if target:
                        text_ids.add(target)
            command_button_rows[key] = {
                "id": block.name,
                "fields": selected,
                "audioReferences": sorted(button_audio_references, key=str.casefold),
                "audioRoutes": button_audio_routes,
                "source": command_button_doc.public(),
            }
        if not changed:
            break

    directly_reachable_sciences = set(sciences)
    gameplay_closure = resolve_gameplay_definition_closure(
        upgrade_source=upgrade_doc.source,
        science_source=science_doc.source,
        special_power_source=special_power_source,
        upgrade_roots=_casefold_unique(upgrades),
        science_roots=_casefold_unique(sciences),
        special_power_roots=_casefold_unique(special_powers),
        string_identifiers=string_identifier_names,
        audio_identifiers=audio_definition_names,
    )
    upgrades.update(str(item["id"]) for item in gameplay_closure.upgrades)
    sciences.update(str(item["id"]) for item in gameplay_closure.sciences)
    mapped_images.update(gameplay_closure.mapped_images)
    required_mapped_images.update(gameplay_closure.mapped_images)
    text_ids.update(gameplay_closure.text_ids)
    audio_roots.update(gameplay_closure.audio_roots)

    upgrades = set(_casefold_unique(upgrades))
    sciences = set(_casefold_unique(sciences))
    special_powers = set(_casefold_unique(special_powers))
    mapped_images = set(_casefold_unique(mapped_images))
    text_ids = set(_casefold_unique(text_ids))
    audio_roots = set(_casefold_unique(audio_roots))
    mapped_image_resolution = resolve_mapped_images_partial(
        (document.source for document in mapped_image_docs),
        _casefold_unique(mapped_images),
    )
    mapped_image_records = mapped_image_resolution.records
    missing_mapped_images = set(mapped_image_resolution.missing_ids)
    nullable_portrait_keys = {item.casefold() for item in nullable_portrait_images}
    required_mapped_image_keys = {item.casefold() for item in required_mapped_images}
    source_null_mapped_images = sorted(
        (
            item
            for item in missing_mapped_images
            if item.casefold() in nullable_portrait_keys
            and item.casefold() not in required_mapped_image_keys
        ),
        key=str.casefold,
    )
    source_null_keys = {item.casefold() for item in source_null_mapped_images}
    unresolved_mapped_images = {
        item
        for item in missing_mapped_images
        if item.casefold() not in source_null_keys
    }
    effective_entries = _effective_entries(catalog)
    effective_virtual_paths = [entry.name for entry in effective_entries.values()]
    mapped_texture_paths, missing_mapped_image_textures = (
        resolve_mapped_image_texture_paths_partial(
            mapped_image_records, effective_virtual_paths
        )
    )
    source_null_texture_keys = {
        texture.casefold()
        for texture in missing_mapped_image_textures
        if texture.casefold() in source_null_texture_policy
    }
    unresolved_mapped_image_textures = tuple(
        texture
        for texture in missing_mapped_image_textures
        if texture.casefold() not in source_null_texture_keys
    )
    source_null_texture_images: dict[str, list[str]] = {
        key: [] for key in source_null_texture_keys
    }
    for record in mapped_image_records:
        key = record.texture.casefold()
        if key in source_null_texture_images:
            source_null_texture_images[key].append(record.id)
    mapped_texture_paths_by_key = {
        texture.casefold(): path for texture, path in mapped_texture_paths.items()
    }
    mapped_image_rows = []
    for record in mapped_image_records:
        row = record.neutral()
        compiled_texture = mapped_texture_paths_by_key.get(record.texture.casefold())
        if compiled_texture is not None:
            row["compiledTextureVirtualPath"] = compiled_texture
        elif record.texture.casefold() in source_null_texture_keys:
            row["compiledTextureResolution"] = "source-null"
        else:
            row["compiledTextureResolution"] = "missing"
        mapped_image_rows.append(row)
    source_null_mapped_image_texture_rows = [
        {
            "texture": source_null_texture_policy[key][0],
            "reason": source_null_texture_policy[key][1],
            "mappedImages": sorted(
                source_null_texture_images[key], key=str.casefold
            ),
        }
        for key in sorted(source_null_texture_keys)
    ]
    resolved_but_declared_null = sorted(
        source_null_command_set_policy[key][0]
        for key in command_set_rows
        if key in source_null_command_set_policy
    )
    if resolved_but_declared_null:
        raise ValueError(
            "source-null CommandSet policy entries resolved in the effective "
            f"catalog: {resolved_but_declared_null}"
        )
    resolved_null_textures = sorted(
        source_null_texture_policy[key][0]
        for key in mapped_texture_paths_by_key
        if key in source_null_texture_policy
    )
    if resolved_null_textures:
        raise ValueError(
            "source-null MappedImage texture policy entries resolved in the "
            f"effective catalog: {resolved_null_textures}"
        )
    source_null_command_set_rows = [
        {
            "id": source_null_command_set_policy[key][0],
            "reason": source_null_command_set_policy[key][1],
        }
        for key in sorted(consumed_source_null_command_sets)
    ]

    resolved_text_rows: list[dict[str, Any]] = []
    missing_text_ids: set[str] = set()
    for identifier in sorted(text_ids, key=str.casefold):
        record = string_catalog.record(identifier)
        if record is None:
            missing_text_ids.add(identifier)
            continue
        encoded_value = record.value.encode("utf-8")
        resolved_text_rows.append(
            {
                "id": record.identifier,
                "charCount": len(record.value),
                "utf8Sha256": hashlib.sha256(encoded_value).hexdigest(),
            }
        )
    duplicate_text_keys = {
        identifier.casefold()
        for identifier in string_catalog.diagnostics.duplicate_identifiers
    }
    conflicting_text_keys = {
        identifier.casefold()
        for identifier in string_catalog.diagnostics.conflicting_identifiers
    }
    requested_duplicate_text_ids = sorted(
        (
            identifier
            for identifier in text_ids
            if identifier.casefold() in duplicate_text_keys
        ),
        key=str.casefold,
    )
    requested_conflicting_text_ids = sorted(
        (
            identifier
            for identifier in text_ids
            if identifier.casefold() in conflicting_text_keys
        ),
        key=str.casefold,
    )

    audio_closure = resolve_sage_audio_closure(
        audio_definitions, _casefold_unique(audio_roots)
    )
    missing_audio_samples: set[str] = set()
    ambiguous_audio_samples: set[str] = set()
    try:
        audio_sample_paths = resolve_audio_sample_paths(
            audio_closure.sample_ids, effective_virtual_paths
        )
    except ValueError as exc:
        if not str(exc).startswith(("unresolved audio sample:", "ambiguous audio sample:")):
            raise
        (
            audio_sample_paths,
            missing_rows,
            ambiguous_rows,
        ) = resolve_audio_sample_paths_partial(
            audio_closure.sample_ids, effective_virtual_paths
        )
        missing_audio_samples.update(missing_rows)
        ambiguous_audio_samples.update(ambiguous_rows)
    audio_row = audio_closure.neutral()
    audio_row["samplePaths"] = [
        {"id": identifier, "virtualPath": audio_sample_paths[identifier]}
        for identifier in audio_closure.sample_ids
        if identifier in audio_sample_paths
    ]

    source_leaf_roles: dict[str, set[str]] = {}
    for path in mapped_texture_paths.values():
        source_leaf_roles.setdefault(path, set()).add("mapped-image-texture")
    for path in audio_sample_paths.values():
        source_leaf_roles.setdefault(path, set()).add("audio-sample")
    source_leaves = []
    for path in sorted(source_leaf_roles, key=str.casefold):
        entry = catalog.resolve_exact(path)
        if entry is None:
            raise ValueError(f"resolved faction leaf disappeared from catalog: {path}")
        source_leaves.append(
            {
                "virtualPath": entry.name,
                "archive": entry.archive,
                "size": entry.size,
                "roles": sorted(source_leaf_roles[path]),
            }
        )

    scanned_documents = [
        player_doc,
        command_set_doc,
        command_button_doc,
        sound_effects_doc,
        voice_doc,
        string_catalog_doc,
        upgrade_doc,
        science_doc,
        special_power_doc,
        fx_list_doc,
        *((weapon_doc,) if weapon_doc is not None else ()),
        *(
            (autoresolve_weapon_doc,)
            if autoresolve_weapon_doc is not None
            else ()
        ),
        eva_doc,
        music_doc,
        *mapped_image_docs,
        *object_docs,
    ]
    used_archives = sorted(
        {
            *(document.archive for document in scanned_documents),
            *(str(item["archive"]) for item in source_leaves),
        },
        key=str.casefold,
    )
    archive_by_name = {item.relative_path.casefold(): item for item in catalog.archives}
    archive_rows = []
    for relative in used_archives:
        info = archive_by_name[relative.casefold()]
        archive_sha256 = getattr(catalog, "archive_sha256", None)
        archive_rows.append(
            {
                "relativePath": info.relative_path,
                "sha256": (
                    archive_sha256(info.relative_path)
                    if callable(archive_sha256)
                    else sha256_file(catalog.install_root / Path(info.relative_path))
                ),
                "directorySha256": info.directory_sha256,
            }
        )
    source_facts = [document.public() for document in scanned_documents]
    source_facts.sort(key=lambda item: str(item["virtualPath"]).casefold())
    input_hash = hashlib.sha256()
    if _legacy_men_identity:
        identity_namespace = "openbfme.men"
        input_hash.update(b"openbfme.men-command-leaf-census-inputs\0")
    else:
        identity_namespace = f"openbfme.faction.{player_template.casefold()}"
        input_hash.update(b"openbfme.faction-command-leaf-census-inputs\0")
        input_hash.update(player_template.encode("utf-8") + b"\0")
        input_hash.update(side.encode("utf-8") + b"\0")
        for identifier, reason in sorted(
            normalized_implicit_roots,
            key=lambda item: (item[0].casefold(), item[0], item[1]),
        ):
            input_hash.update(identifier.encode("utf-8") + b"\0")
            input_hash.update(reason.encode("utf-8") + b"\n")
        for policy_domain, policy in (
            ("source-null-texture", source_null_texture_policy),
            ("source-null-command-set", source_null_command_set_policy),
            ("music-root", music_root_policy),
        ):
            for key in sorted(policy):
                identifier, reason = policy[key]
                input_hash.update(policy_domain.encode("ascii") + b"\0")
                input_hash.update(identifier.encode("utf-8") + b"\0")
                input_hash.update(reason.encode("utf-8") + b"\n")
    for item in source_facts:
        input_hash.update(str(item["virtualPath"]).encode("utf-8") + b"\0")
        input_hash.update(str(item["sha256"]).encode("ascii") + b"\n")
    for item in source_leaves:
        input_hash.update(str(item["virtualPath"]).encode("utf-8") + b"\0")
        input_hash.update(str(item["archive"]).encode("utf-8") + b"\0")
        input_hash.update(str(item["size"]).encode("ascii") + b"\n")

    object_list = sorted(
        object_rows.values(), key=lambda item: str(item["id"]).casefold()
    )
    command_set_list = sorted(
        command_set_rows.values(), key=lambda item: str(item["id"]).casefold()
    )
    command_button_list = sorted(
        command_button_rows.values(), key=lambda item: str(item["id"]).casefold()
    )
    spellbook_powers = sorted(
        (
            identifier
            for identifier in special_powers
            if identifier.startswith("SpellBook")
        ),
        key=str.casefold,
    )
    spellbook_sciences = sorted(
        (
            identifier
            for identifier in directly_reachable_sciences
            if identifier.casefold()
            not in {item.casefold() for item in intrinsic_sciences}
        ),
        key=str.casefold,
    )
    unresolved = {
        "missingObjects": sorted(missing_objects, key=str.casefold),
        "ambiguousObjects": sorted(ambiguous_objects, key=str.casefold),
        "missingCommandSets": sorted(missing_command_sets, key=str.casefold),
        "ambiguousCommandSets": sorted(ambiguous_command_sets, key=str.casefold),
        "missingCommandButtons": sorted(missing_buttons, key=str.casefold),
        "ambiguousCommandButtons": sorted(ambiguous_buttons, key=str.casefold),
        "missingInheritanceObjects": sorted(
            missing_inheritance_objects, key=str.casefold
        ),
        "ambiguousInheritanceObjects": sorted(
            ambiguous_inheritance_objects, key=str.casefold
        ),
        "missingTextIds": sorted(missing_text_ids, key=str.casefold),
        "missingMappedImages": sorted(unresolved_mapped_images, key=str.casefold),
        "missingAudioDefinitions": sorted(missing_audio_definitions, key=str.casefold),
        "missingEvaEvents": sorted(missing_eva_events, key=str.casefold),
        "ambiguousMappedImages": list(mapped_image_resolution.ambiguous_ids),
        "missingUpgrades": list(gameplay_closure.missing_upgrades),
        "ambiguousUpgrades": list(gameplay_closure.ambiguous_upgrades),
        "missingSciences": list(gameplay_closure.missing_sciences),
        "ambiguousSciences": list(gameplay_closure.ambiguous_sciences),
        "missingSpecialPowers": list(gameplay_closure.missing_special_powers),
        "ambiguousSpecialPowers": list(gameplay_closure.ambiguous_special_powers),
        "missingFxLists": sorted(missing_fx_lists, key=str.casefold),
        "missingWeaponDefinitions": sorted(
            missing_weapon_definitions, key=str.casefold
        ),
    }
    if unresolved_mapped_image_textures:
        unresolved["missingMappedImageTextures"] = sorted(
            unresolved_mapped_image_textures, key=str.casefold
        )
    if missing_audio_samples:
        unresolved["missingAudioSamples"] = sorted(
            missing_audio_samples, key=str.casefold
        )
    if ambiguous_audio_samples:
        unresolved["ambiguousAudioSamples"] = sorted(
            ambiguous_audio_samples, key=str.casefold
        )
    report = {
        "format": 1,
        "schema": "openbfme.faction-command-leaf-census",
        "schemaVersion": 1,
        "target": {
            "game": "BFME2" if game_definition.id == "bfme2" else "RotWK",
            "patch": "1.06" if game_definition.id == "bfme2" else "2.01",
            "faction": side,
            "playerTemplate": player_template,
            "mode": "normal-skirmish-command-reachable",
        },
        "closureStatus": "command-ui-localization-audio-gameplay-definition-leaves",
        "sourceArchives": archive_rows,
        "sourceDocuments": source_facts,
        "sourceLeaves": source_leaves,
        "inputSetSha256": input_hash.hexdigest(),
        "roots": sorted(
            roots,
            key=lambda item: (
                item["edgeKind"],
                item["id"].casefold(),
                item["id"],
                item["sourceField"].casefold(),
                item["sourceField"],
            ),
        ),
        "definitions": {
            "objects": object_list,
            "commandSets": command_set_list,
            "commandButtons": command_button_list,
            "upgrades": list(gameplay_closure.upgrades),
            "sciences": list(gameplay_closure.sciences),
            "specialPowers": list(gameplay_closure.special_powers),
        },
        "dependencies": {
            "upgrades": sorted(upgrades, key=str.casefold),
            "specialPowers": sorted(special_powers, key=str.casefold),
            "spellbookSpecialPowers": spellbook_powers,
            "sciences": sorted(sciences, key=str.casefold),
            "spellbookSciences": spellbook_sciences,
            "mappedImages": [record.id for record in mapped_image_records],
            "sourceNullMappedImages": source_null_mapped_images,
            "sourceNullMappedImageTextures": source_null_mapped_image_texture_rows,
            "sourceNullCommandSets": source_null_command_set_rows,
            "textIds": sorted(text_ids, key=str.casefold),
            "audioRootIds": list(audio_closure.root_ids),
            "musicRootIds": [row["id"] for row in declared_music_roots],
            "fxLists": sorted(
                {str(item) for item in gameplay_closure.fx_lists}
                | spellbook_fx_lists,
                key=str.casefold,
            ),
        },
        "resolvedLeaves": {
            "mappedImages": mapped_image_rows,
            "localization": {
                "duplicatePolicy": "source-order-first-wins",
                "catalogSummary": string_catalog.neutral_summary(),
                "records": resolved_text_rows,
                "requestedDuplicateIds": requested_duplicate_text_ids,
                "requestedConflictingDuplicateIds": requested_conflicting_text_ids,
                "oracleStatus": (
                    "first-wins-source-compatible-conflicts-require-visual-review"
                    if requested_conflicting_text_ids
                    else "no-requested-conflicts"
                ),
            },
            "audio": audio_row,
        },
        "unresolved": unresolved,
        "summary": {
            "rootCount": len(roots),
            "rootIdCount": len({item["id"] for item in roots}),
            "objectCount": len(object_list),
            "commandSetCount": len(command_set_list),
            "commandButtonCount": len(command_button_list),
            "upgradeCount": len(upgrades),
            "specialPowerCount": len(special_powers),
            "spellbookSpecialPowerCount": len(spellbook_powers),
            "scienceCount": len(sciences),
            "spellbookScienceCount": len(spellbook_sciences),
            "mappedImageReferenceCount": len(mapped_images),
            "mappedImageCount": len(mapped_image_records),
            "mappedImageResolvedCount": len(mapped_image_records),
            "mappedImageSourceNullCount": len(source_null_mapped_images),
            "mappedImageTextureCount": len(mapped_texture_paths),
            "mappedImageTextureSourceNullCount": len(
                source_null_mapped_image_texture_rows
            ),
            "commandSetSourceNullCount": len(source_null_command_set_rows),
            "textIdCount": len(text_ids),
            "textResolvedCount": len(resolved_text_rows),
            "requestedTextConflictCount": len(requested_conflicting_text_ids),
            "audioRootCount": len(audio_closure.root_ids),
            "musicRootCount": len(declared_music_roots),
            "audioEventCount": len(audio_closure.events),
            "audioMultisoundCount": len(audio_closure.multisounds),
            "audioSampleCount": len(audio_closure.sample_ids),
            "sourceLeafCount": len(source_leaves),
            "unresolvedCount": sum(len(items) for items in unresolved.values()),
            "objectSetSha256": _set_hash(
                f"{identity_namespace}-object-set", (item["id"] for item in object_list)
            ),
            "upgradeSetSha256": _set_hash(
                f"{identity_namespace}-upgrade-set", upgrades
            ),
            "specialPowerSetSha256": _set_hash(
                f"{identity_namespace}-special-power-set", special_powers
            ),
            "resolvedUpgradeDefinitionCount": len(gameplay_closure.upgrades),
            "resolvedScienceDefinitionCount": len(gameplay_closure.sciences),
            "resolvedSpecialPowerDefinitionCount": len(gameplay_closure.special_powers),
            "fxListReferenceCount": len(
                {str(item) for item in gameplay_closure.fx_lists}
                | spellbook_fx_lists
            ),
        },
        "limitations": [
            "Command-reachable upgrade, science, and special-power definitions are resolved as typed identifier edges plus payload-free assignment digests.",
            "This census does not yet resolve W3D, animation, material, FX-list bodies, weapon/projectile, construction, damage, or destruction leaves; the sole weapon exception is the Weapon FireFX/ProjectileDetonationFX -> FXList -> Sound audio chain, routed as typed audio-definition edges.",
            "Mapped-image, localization, and audio leaves cover the current command-reachable object/button graph, not every future runtime state.",
            "Authored SelectPortrait references with no MappedImage definition are preserved as explicit source-null images; required ButtonImage gaps remain unresolved.",
            "Caller-declared source-null policy covers only retail-authored references absent from every effective archive (placeholder button atlas textures, the Isengard side-pad CommandSet); every other missing leaf remains unresolved.",
            "Caller-declared music roots cover the engine-level skirmish shell and load-screen loops resolved through the merged MusicTrack/Multisound namespace.",
            "Localized duplicate conflicts use BFME2 source order and remain explicit oracle-review evidence.",
            "Runtime support and oracle parity are not implied by definition reachability.",
            (
                "ROTWK 2.01 data is a separate future overlay and is not merged into this BFME2 1.06 report."
                if game_definition.id == "bfme2"
                else "RotWK policy contains no curated BFME2 source-null or implicit-root allowances; unresolved leaves remain explicit."
            ),
        ],
    }
    if not _legacy_men_identity:
        report["playerTemplateRosters"] = {
            "entries": roster_entries,
        }
    return report


def census_playable_faction(
    catalog: InstallCatalog,
    *,
    player_template: str,
    game: str = "bfme2",
    expected_side: str | None = None,
    implicit_object_roots: Iterable[tuple[str, str]] = (),
    source_null_mapped_image_textures: Iterable[tuple[str, str]] = (),
    source_null_command_sets: Iterable[tuple[str, str]] = (),
    music_roots: Iterable[tuple[str, str]] = (),
) -> dict[str, Any]:
    """Return one generic playable-faction census with identity-bound policy."""

    return _census_playable_faction(
        catalog,
        player_template=player_template,
        game=game,
        expected_side=expected_side,
        implicit_object_roots=implicit_object_roots,
        source_null_mapped_image_textures=source_null_mapped_image_textures,
        source_null_command_sets=source_null_command_sets,
        music_roots=music_roots,
        _legacy_men_identity=False,
    )


def census_men_faction(catalog: InstallCatalog) -> dict[str, Any]:
    """Compatibility entry point for the established Men census identity."""

    return _census_playable_faction(
        catalog,
        player_template="FactionMen",
        expected_side="Men",
        implicit_object_roots=_IMPLICIT_MEN_ROOTS,
        _legacy_men_identity=True,
    )
