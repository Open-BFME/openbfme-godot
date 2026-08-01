"""Bounded parser for BFME2 Create-a-Hero saved heroes (``.cah``).

A ``.cah`` file is one *saved build order*, not a hero object.  It records the
choices a player made in the Create-a-Hero front end: a class selector, a
display name, three tint colours, two small appearance indices, the ordered
list of ability CommandButtons that were purchased, and one chosen index per
customisation group.  Everything in it is a **name** or a **small integer**;
there is not a single stat, multiplier or model path.  The engine resolves
those names against INI at load time, which is the same idiom SAGE uses for
every other persisted handle (Science, Upgrade, KindOf and CommandButton are
all stored by INI name and re-resolved through their store -- see the Zero
Hour reference implementation in ``Xfer.cpp``, ``xferScienceType`` /
``xferUpgradeMask`` / ``xferKindOf``, and ``AIStates.cpp`` for CommandButton).

Container
---------
The first eight bytes are two FourCCs written byte-reversed, ``'EALA'`` then
``'RTS2'``, so on disk they read ``ALAE2STR``.  OpenSAGE documents the BFME1
sibling of this header -- ``'EALA'`` + ``'RTS1'`` + ``uint32`` -- in a
commented-out block at ``OpenSage.Game/Data/Sav/SaveFile.cs:79-102``; that
one reads ``ALAE1STR`` on disk.  ``.cah`` is the ``RTS2`` variant and carries
version ``1`` where the save header expects ``0``.

Unlike a ``.sav``, a ``.cah`` has **no ``BLOK`` segments and no ``SG_EOF``
sentinel**.  It is a single flat record with a fixed field order, so this
parser is a straight-line reader rather than a chunk dispatcher.

String encoding
---------------
SAGE serialises strings with a one-byte length in which ``0xFF`` is an escape
introducing a full ``uint32`` length; the payload carries no NUL terminator.
Ascii strings store bytes, Unicode strings store the count of UTF-16LE code
units and occupy twice that many bytes.  This is the BFME1 ``Xfer``
convention recovered from the matched decompilation of
``Xfer::operator==(AsciiString&)`` at RVA ``0x009D6940`` and
``Xfer::operator==(UnicodeString&)`` at ``0x009D6A70``
(``Open-BFME-research/Code/GameEngine/Source/Common/System/Xfer.cpp``); Zero
Hour's ancestor lacked the escape and hard-capped at 255
(``XferSave.cpp:292-312``).  See ``ASSUMPTIONS`` below -- no observed ``.cah``
string is long enough to exercise the escape.

Payload discipline
------------------
:class:`CahHero` holds decoded values in memory for private conversion work.
:func:`cah_census` exposes only structure: counts, the group-name vocabulary
(which is format metadata, identical in every file), digests and histograms.
It never emits a hero name, a colour, an appearance index or a chosen
customisation index, all of which are authored content.

Fail-closed contract
--------------------
Every field is bounds-checked and every anomaly raises :class:`CahFormatError`
naming the file label, the byte offset and what was expected.  The final gate
is a **round-trip**: :meth:`CahHero.to_bytes` re-serialises the decoded model
and the parser rejects the input unless the result is byte-identical.  A file
this parser accepts is therefore one it can reproduce exactly, which is a much
stronger claim than "no exception was raised".  Nothing is ever defaulted or
guessed.

ASSUMPTIONS (each labelled with what would falsify it)
-----------------------------------------------------
* ``0xFF`` length escape -- inherited from BFME1 ``Xfer``; BFME2 is a later
  fork of the same engine but no ``.cah`` in the 22-file sample has a string
  of 255+ units, so the escape is unexercised.  Falsified by a ``.cah`` whose
  byte at a string-length position is ``0xFF`` and is *not* followed by a
  plausible ``uint32`` length.  :attr:`CahHero.used_length_escape` reports
  whether a given file took the path.
* ``header_selector`` (5 bytes at offset 16) -- decoded as ``uint8`` tag plus
  ``uint32``.  The tag is ``8`` in every sample; the ``uint32`` takes five
  observed values and correlates only loosely with the good/evil split, so its
  meaning is **undetermined** and it is preserved verbatim rather than
  interpreted.  The alternative split (``uint32`` at 16 plus ``uint8`` at 20)
  is byte-identical on round-trip, so nothing downstream depends on the
  choice.  Falsified by a sample where the tag is not ``8``.
* ``trailer_flag`` -- the byte before the checksum is ``1`` in all 15 heroes
  shipped inside retail ``.big`` archives and ``0`` in all 7 player-created
  heroes found in a user save directory.  The correlation is perfect across
  that 22-file sample but the sample confounds "shipped by EA" with "written
  by the retail front end", so the flag's meaning is **undetermined**.
  Falsified by a player-created hero carrying ``1`` or a shipped one carrying
  ``0``.
* ``trailer_checksum`` (last 4 bytes) -- **undecoded**.  CRC-32 and Adler-32
  over every plausible byte range, and FNV-1/FNV-1a, were all tested and none
  matches on even one file.  It is preserved verbatim and never validated.
  Falsified by finding the algorithm.
"""

from __future__ import annotations

import hashlib
import struct
from typing import Any, Iterable, Sequence


#: ``'EALA'`` + ``'RTS2'`` written byte-reversed, as they appear on disk.
CAH_MAGIC = b"ALAE2STR"

#: The only container version any observed ``.cah`` carries.
SUPPORTED_VERSION = 1

#: The header tag byte at offset 16, invariant across every observed file.
HEADER_TAG = 8

#: The ability array is a fixed-capacity array, not a terminated list: unused
#: slots are written out in full as an empty name plus two zero words.
ABILITY_SLOT_COUNT = 15

#: The trailer flag byte immediately before the undecoded checksum.  Both
#: values occur: every hero shipped inside a retail ``.big`` carries ``1`` and
#: every player-created hero found in a user save directory carries ``0``.
#: What the flag *means* is undetermined; it is preserved, not interpreted.
TRAILER_FLAGS = frozenset({0, 1})

MAX_CAH_BYTES = 1 * 1024 * 1024
MAX_STRING_UNITS = 64 * 1024
MAX_GROUP_COUNT = 4096
MAX_PALANTIR_SLOT = 255

_U8 = struct.Struct("<B")
_U32 = struct.Struct("<I")
_U32X2 = struct.Struct("<2I")
_U32X4 = struct.Struct("<4I")

#: ``0xFF`` in a length byte escapes to a following ``uint32``.
_LENGTH_ESCAPE = 0xFF


class CahFormatError(ValueError):
    """The input is not a structurally valid Create-a-Hero saved hero.

    Carries the file label and the byte offset so a failure names exactly
    where the parser stopped believing the input.
    """

    def __init__(self, label: str, offset: int, reason: str) -> None:
        super().__init__(f"{label}: at offset 0x{offset:x}: {reason}")
        self.label = label
        self.offset = offset
        self.reason = reason


class CahAbility:
    """One purchased ability: a CommandButton name and where it was placed.

    ``purchase_index`` is the order the player bought the ability in and
    ``palantir_slot`` is the command-set slot it occupies, which is why the
    same slot recurs as an ability is levelled up.
    """

    __slots__ = ("button", "purchase_index", "palantir_slot")

    def __init__(self, button: str, purchase_index: int, palantir_slot: int) -> None:
        self.button = button
        self.purchase_index = purchase_index
        self.palantir_slot = palantir_slot

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, CahAbility):
            return NotImplemented
        return (
            self.button == other.button
            and self.purchase_index == other.purchase_index
            and self.palantir_slot == other.palantir_slot
        )

    def __repr__(self) -> str:
        return (
            f"CahAbility(button={self.button!r}, "
            f"purchase_index={self.purchase_index}, "
            f"palantir_slot={self.palantir_slot})"
        )


class CahGroupChoice:
    """One customisation group and the index chosen within it.

    ``group`` is a ``CreateAHeroBlingBinder`` group name and ``value`` is the
    ``GroupOrder`` of the chosen ``Upgrade`` inside that group -- an index into
    an INI-declared table, never a stat.
    """

    __slots__ = ("group", "value")

    def __init__(self, group: str, value: int) -> None:
        self.group = group
        self.value = value

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, CahGroupChoice):
            return NotImplemented
        return self.group == other.group and self.value == other.value

    def __repr__(self) -> str:
        return f"CahGroupChoice(group={self.group!r}, value={self.value})"


class CahHero:
    """A decoded ``.cah``: the player's saved build order, nothing more."""

    __slots__ = (
        "version",
        "header_reserved",
        "header_tag",
        "header_selector",
        "name",
        "appearance",
        "colors",
        "abilities",
        "groups",
        "hero_id",
        "trailer_flag",
        "trailer_checksum",
        "used_length_escape",
        "source_sha256",
    )

    def __init__(
        self,
        *,
        version: int,
        header_reserved: int,
        header_tag: int,
        header_selector: int,
        name: str,
        appearance: tuple[int, int, int, int],
        colors: tuple[bytes, bytes, bytes],
        abilities: Sequence[CahAbility],
        groups: Sequence[CahGroupChoice],
        hero_id: str,
        trailer_flag: int,
        trailer_checksum: int,
        used_length_escape: bool,
        source_sha256: str,
    ) -> None:
        self.version = version
        self.header_reserved = header_reserved
        self.header_tag = header_tag
        self.header_selector = header_selector
        self.name = name
        self.appearance = appearance
        self.colors = colors
        self.abilities = list(abilities)
        self.groups = list(groups)
        self.hero_id = hero_id
        self.trailer_flag = trailer_flag
        self.trailer_checksum = trailer_checksum
        self.used_length_escape = used_length_escape
        self.source_sha256 = source_sha256

    def __repr__(self) -> str:  # payload-free by design
        return (
            f"CahHero(id={self.hero_id!r}, abilities={len(self.abilities)}, "
            f"groups={len(self.groups)})"
        )

    @property
    def group_values(self) -> dict[str, int]:
        """Chosen index per group name, in file order."""

        return {choice.group: choice.value for choice in self.groups}

    def to_bytes(self) -> bytes:
        """Re-serialise exactly as read.

        The parser compares this against its input, so any field the reader
        misunderstands shows up as a round-trip mismatch instead of silently
        surviving as a wrong value.
        """

        out = bytearray()
        out += CAH_MAGIC
        out += _U32.pack(self.version)
        out += _U32.pack(self.header_reserved)
        out += _U8.pack(self.header_tag)
        out += _U32.pack(self.header_selector)
        out += _write_unicode_string(self.name)
        out += _U32X4.pack(*self.appearance)
        for color in self.colors:
            out += color
        for index in range(ABILITY_SLOT_COUNT):
            if index < len(self.abilities):
                ability = self.abilities[index]
                out += _write_ascii_string(ability.button)
                out += _U32X2.pack(ability.purchase_index, ability.palantir_slot)
            else:
                out += _write_ascii_string("")
                out += _U32X2.pack(0, 0)
        out += _U32.pack(len(self.groups))
        for choice in self.groups:
            out += _write_ascii_string(choice.group)
            out += _U32.pack(choice.value)
        out += _write_ascii_string(self.hero_id)
        out += _U8.pack(self.trailer_flag)
        out += _U32.pack(self.trailer_checksum)
        return bytes(out)


def _write_length(count: int) -> bytes:
    if count < _LENGTH_ESCAPE:
        return _U8.pack(count)
    return _U8.pack(_LENGTH_ESCAPE) + _U32.pack(count)


def _write_ascii_string(text: str) -> bytes:
    payload = text.encode("ascii")
    return _write_length(len(payload)) + payload


def _write_unicode_string(text: str) -> bytes:
    payload = text.encode("utf-16-le")
    return _write_length(len(payload) // 2) + payload


class _Reader:
    """Bounds-checked cursor that reports the offset of every failure."""

    __slots__ = ("data", "label", "offset", "used_escape")

    def __init__(self, data: bytes, label: str) -> None:
        self.data = data
        self.label = label
        self.offset = 0
        self.used_escape = False

    def fail(self, reason: str, *, offset: int | None = None) -> "CahFormatError":
        return CahFormatError(
            self.label, self.offset if offset is None else offset, reason
        )

    def take(self, count: int, what: str) -> bytes:
        end = self.offset + count
        if count < 0 or end > len(self.data):
            raise self.fail(
                f"{what} needs {count} bytes but only "
                f"{len(self.data) - self.offset} remain"
            )
        chunk = self.data[self.offset : end]
        self.offset = end
        return chunk

    def u8(self, what: str) -> int:
        return _U8.unpack(self.take(1, what))[0]

    def u32(self, what: str) -> int:
        return _U32.unpack(self.take(4, what))[0]

    def length(self, what: str) -> int:
        first = self.u8(f"{what} length")
        if first != _LENGTH_ESCAPE:
            return first
        self.used_escape = True
        escaped = self.u32(f"{what} escaped length")
        if escaped < _LENGTH_ESCAPE:
            raise self.fail(
                f"{what} used the 0xFF length escape for {escaped} units, which "
                f"fits in one byte; the writer would not have escaped it"
            )
        return escaped

    def ascii_string(self, what: str) -> str:
        start = self.offset
        count = self.length(what)
        if count > MAX_STRING_UNITS:
            raise self.fail(
                f"{what} declares {count} bytes; limit is {MAX_STRING_UNITS}",
                offset=start,
            )
        raw = self.take(count, what)
        try:
            return raw.decode("ascii")
        except UnicodeDecodeError as error:
            raise self.fail(
                f"{what} is not ASCII: {error}", offset=start
            ) from error

    def unicode_string(self, what: str) -> str:
        start = self.offset
        count = self.length(what)
        if count > MAX_STRING_UNITS:
            raise self.fail(
                f"{what} declares {count} UTF-16 units; limit is "
                f"{MAX_STRING_UNITS}",
                offset=start,
            )
        raw = self.take(count * 2, what)
        try:
            text = raw.decode("utf-16-le")
        except UnicodeDecodeError as error:
            raise self.fail(
                f"{what} is not decodable UTF-16LE: {error}", offset=start
            ) from error
        if len(text) != count:
            raise self.fail(
                f"{what} declared {count} UTF-16 units but decoded to "
                f"{len(text)} characters (unpaired surrogate)",
                offset=start,
            )
        return text


def parse_cah(data: bytes, *, label: str = "<cah>") -> CahHero:
    """Decode one ``.cah``, or raise :class:`CahFormatError` saying where.

    The parser accepts nothing it cannot re-emit byte-for-byte.
    """

    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise TypeError("parse_cah expects bytes")
    data = bytes(data)
    if len(data) > MAX_CAH_BYTES:
        raise CahFormatError(
            label, 0, f"file is {len(data)} bytes; limit is {MAX_CAH_BYTES}"
        )

    reader = _Reader(data, label)

    magic = reader.take(len(CAH_MAGIC), "magic")
    if magic != CAH_MAGIC:
        raise CahFormatError(
            label,
            0,
            f"magic is {magic!r}, expected {CAH_MAGIC!r} "
            f"('EALA'+'RTS2' byte-reversed)",
        )

    version_offset = reader.offset
    version = reader.u32("version")
    if version != SUPPORTED_VERSION:
        raise CahFormatError(
            label,
            version_offset,
            f"version is {version}; this parser understands only "
            f"{SUPPORTED_VERSION} and will not guess at another layout",
        )

    reserved_offset = reader.offset
    header_reserved = reader.u32("header reserved word")
    if header_reserved != 0:
        raise CahFormatError(
            label,
            reserved_offset,
            f"header reserved word is {header_reserved}, expected 0; its "
            f"meaning when non-zero is unknown",
        )

    tag_offset = reader.offset
    header_tag = reader.u8("header tag")
    if header_tag != HEADER_TAG:
        raise CahFormatError(
            label,
            tag_offset,
            f"header tag is {header_tag}, expected {HEADER_TAG}; the field "
            f"layout at this offset is only known for that value",
        )
    header_selector = reader.u32("header selector")

    name = reader.unicode_string("hero name")
    if not name:
        raise CahFormatError(
            label, reader.offset, "hero name is empty; every observed file names its hero"
        )

    appearance = _U32X4.unpack(reader.take(16, "appearance indices"))
    colors = (
        reader.take(4, "primary colour"),
        reader.take(4, "secondary colour"),
        reader.take(4, "tertiary colour"),
    )

    abilities: list[CahAbility] = []
    seen_empty = False
    for slot in range(ABILITY_SLOT_COUNT):
        slot_offset = reader.offset
        button = reader.ascii_string(f"ability slot {slot} button")
        purchase_index, palantir_slot = _U32X2.unpack(
            reader.take(8, f"ability slot {slot} words")
        )
        if not button:
            if (purchase_index, palantir_slot) != (0, 0):
                raise CahFormatError(
                    label,
                    slot_offset,
                    f"ability slot {slot} has no button but carries "
                    f"({purchase_index}, {palantir_slot}); unused slots are "
                    f"zero-filled",
                )
            seen_empty = True
            continue
        if seen_empty:
            raise CahFormatError(
                label,
                slot_offset,
                f"ability slot {slot} is populated after an empty slot; "
                f"purchased abilities are contiguous from slot 0",
            )
        if purchase_index != len(abilities):
            raise CahFormatError(
                label,
                slot_offset,
                f"ability slot {slot} declares purchase index "
                f"{purchase_index}, expected {len(abilities)}; purchase order "
                f"is the slot ordinal in every observed file",
            )
        if not 1 <= palantir_slot <= MAX_PALANTIR_SLOT:
            raise CahFormatError(
                label,
                slot_offset,
                f"ability slot {slot} targets palantir slot {palantir_slot}, "
                f"which is outside 1..{MAX_PALANTIR_SLOT}",
            )
        abilities.append(CahAbility(button, purchase_index, palantir_slot))

    group_count_offset = reader.offset
    group_count = reader.u32("customisation group count")
    if group_count > MAX_GROUP_COUNT:
        raise CahFormatError(
            label,
            group_count_offset,
            f"declares {group_count} customisation groups; limit is "
            f"{MAX_GROUP_COUNT}",
        )
    groups: list[CahGroupChoice] = []
    seen_groups: set[str] = set()
    for index in range(group_count):
        entry_offset = reader.offset
        group = reader.ascii_string(f"group {index} name")
        if not group:
            raise CahFormatError(
                label, entry_offset, f"group {index} has an empty name"
            )
        if group in seen_groups:
            raise CahFormatError(
                label,
                entry_offset,
                f"group {group!r} appears more than once; each customisation "
                f"group is chosen exactly once",
            )
        seen_groups.add(group)
        value = reader.u32(f"group {index} chosen index")
        groups.append(CahGroupChoice(group, value))

    id_offset = reader.offset
    hero_id = reader.ascii_string("hero id")
    if not hero_id:
        raise CahFormatError(label, id_offset, "hero id is empty")

    flag_offset = reader.offset
    trailer_flag = reader.u8("trailer flag")
    if trailer_flag not in TRAILER_FLAGS:
        raise CahFormatError(
            label,
            flag_offset,
            f"trailer flag is {trailer_flag}, expected one of "
            f"{sorted(TRAILER_FLAGS)}; its meaning is undetermined and this "
            f"parser will not guess at a third value",
        )
    trailer_checksum = reader.u32("trailer checksum")

    if reader.offset != len(data):
        raise CahFormatError(
            label,
            reader.offset,
            f"{len(data) - reader.offset} trailing bytes after the record; "
            f"a .cah ends at its checksum",
        )

    hero = CahHero(
        version=version,
        header_reserved=header_reserved,
        header_tag=header_tag,
        header_selector=header_selector,
        name=name,
        appearance=appearance,
        colors=colors,
        abilities=abilities,
        groups=groups,
        hero_id=hero_id,
        trailer_flag=trailer_flag,
        trailer_checksum=trailer_checksum,
        used_length_escape=reader.used_escape,
        source_sha256=hashlib.sha256(data).hexdigest(),
    )

    rebuilt = hero.to_bytes()
    if rebuilt != data:
        offset = next(
            (i for i, (a, b) in enumerate(zip(rebuilt, data)) if a != b),
            min(len(rebuilt), len(data)),
        )
        raise CahFormatError(
            label,
            offset,
            f"round-trip mismatch: re-serialising the decoded hero produced "
            f"{len(rebuilt)} bytes that diverge from the {len(data)}-byte "
            f"input here, so the layout is not fully understood",
        )
    return hero


def cah_census(entries: Iterable[tuple[str, bytes]]) -> dict[str, Any]:
    """Structure-only census over ``(label, bytes)`` pairs.

    Deterministic and payload-free: counts, digests, the group-name vocabulary
    and histograms only.  Hero names, colours, appearance indices and chosen
    customisation indices are authored content and are deliberately absent.
    """

    files: list[dict[str, Any]] = []
    selector_histogram: dict[str, int] = {}
    ability_count_histogram: dict[str, int] = {}
    trailer_flag_histogram: dict[str, int] = {}
    group_vocabularies: set[tuple[str, ...]] = set()
    escape_users = 0

    for label, data in sorted(entries, key=lambda item: item[0]):
        hero = parse_cah(data, label=label)
        group_names = tuple(choice.group for choice in hero.groups)
        group_vocabularies.add(group_names)
        selector_key = str(hero.header_selector)
        selector_histogram[selector_key] = selector_histogram.get(selector_key, 0) + 1
        ability_key = str(len(hero.abilities))
        ability_count_histogram[ability_key] = (
            ability_count_histogram.get(ability_key, 0) + 1
        )
        flag_key = str(hero.trailer_flag)
        trailer_flag_histogram[flag_key] = trailer_flag_histogram.get(flag_key, 0) + 1
        if hero.used_length_escape:
            escape_users += 1
        files.append(
            {
                "label": label,
                "sha256": hero.source_sha256,
                "byteLength": len(data),
                "version": hero.version,
                "nameCharCount": len(hero.name),
                "abilityCount": len(hero.abilities),
                "distinctAbilityButtons": len(
                    {ability.button for ability in hero.abilities}
                ),
                "palantirSlotsUsed": sorted(
                    {ability.palantir_slot for ability in hero.abilities}
                ),
                "groupCount": len(hero.groups),
                "heroIdCharCount": len(hero.hero_id),
                "trailerFlag": hero.trailer_flag,
                "usedLengthEscape": hero.used_length_escape,
            }
        )

    vocabularies = sorted(group_vocabularies)
    return {
        "schema": "openbfme.cah-census",
        "schemaVersion": 0,
        "$comment": (
            "Structure-only census of BFME2 Create-a-Hero saved heroes. "
            "Counts, digests and the INI group vocabulary only -- no hero "
            "names, colours or chosen customisation indices. Regenerate with "
            "openbfme_importer.sage_cah.cah_census; do not hand-edit."
        ),
        "fileCount": len(files),
        "abilitySlotCapacity": ABILITY_SLOT_COUNT,
        "groupNameVocabularies": [list(names) for names in vocabularies],
        "selectorHistogram": dict(sorted(selector_histogram.items())),
        "abilityCountHistogram": dict(sorted(ability_count_histogram.items())),
        "trailerFlagHistogram": dict(sorted(trailer_flag_histogram.items())),
        "filesUsingLengthEscape": escape_users,
        "files": files,
    }
