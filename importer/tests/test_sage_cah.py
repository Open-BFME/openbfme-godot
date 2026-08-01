"""Tests for the BFME2 Create-a-Hero saved-hero (``.cah``) decoder.

The synthetic builders below are written independently of the parser's own
serialiser so the tests are a real oracle rather than a mirror of the code
under test.
"""

from __future__ import annotations

import json
import struct

import pytest
from tests.retail_inputs import retail_file

try:
    from openbfme_importer.big import BigArchive
    from openbfme_importer.sage_cah import (
        ABILITY_SLOT_COUNT,
        CAH_MAGIC,
        HEADER_TAG,
        MAX_CAH_BYTES,
        MAX_GROUP_COUNT,
        SUPPORTED_VERSION,
        TRAILER_FLAGS,
        CahFormatError,
        cah_census,
        parse_cah,
    )
except ModuleNotFoundError:  # Supports the repository-root acceptance command.
    from importer.openbfme_importer.big import BigArchive
    from importer.openbfme_importer.sage_cah import (
        ABILITY_SLOT_COUNT,
        CAH_MAGIC,
        HEADER_TAG,
        MAX_CAH_BYTES,
        MAX_GROUP_COUNT,
        SUPPORTED_VERSION,
        TRAILER_FLAGS,
        CahFormatError,
        cah_census,
        parse_cah,
    )


RETAIL_BIG = retail_file("data1.big")
RETAIL_PREFIX = "data/systemheroes/"
MAX_REAL_CAH_BYTES = 1 * 1024 * 1024

#: The twelve customisation groups every observed retail hero writes, in file
#: order: the five ``ATTRIBUTE`` binders and the seven ``APPEARANCE`` ones.
EXPECTED_GROUPS = [
    "CreateAHero_Weapon",
    "CreateAHero_ArmorAttribute",
    "CreateAHero_DamageMultAttribute",
    "CreateAHero_VisionAttribute",
    "CreateAHero_AutoHealAttribute",
    "CreateAHero_HealthMultAttribute",
    "CreateAHero_ShoulderPlates",
    "CreateAHero_Helmet",
    "CreateAHero_Gauntlets",
    "CreateAHero_Shield",
    "CreateAHero_Boots",
    "CreateAHero_Body",
]


# --- independent builders ---------------------------------------------------


def _length(count: int, *, force_escape: bool = False) -> bytes:
    if count < 0xFF and not force_escape:
        return struct.pack("<B", count)
    return struct.pack("<BI", 0xFF, count)


def _ascii(text: str, *, force_escape: bool = False) -> bytes:
    raw = text.encode("ascii")
    return _length(len(raw), force_escape=force_escape) + raw


def _unicode(text: str, *, force_escape: bool = False) -> bytes:
    raw = text.encode("utf-16-le")
    return _length(len(raw) // 2, force_escape=force_escape) + raw


def _build(
    *,
    magic: bytes = CAH_MAGIC,
    version: int = SUPPORTED_VERSION,
    reserved: int = 0,
    tag: int = HEADER_TAG,
    selector: int = 19,
    name: str = "Hadhod",
    appearance: tuple[int, int, int, int] = (3, 1, 0, 0),
    colors: tuple[bytes, bytes, bytes] = (b"\xab\x7eN\xff", b"\xfe\x931\xff", b"\xffp3\xff"),
    abilities: list[tuple[str, int, int]] | None = None,
    slot_count: int = ABILITY_SLOT_COUNT,
    pad_words: tuple[int, int] = (0, 0),
    groups: list[tuple[str, int]] | None = None,
    group_count: int | None = None,
    hero_id: str = "E06BD0F22E8D478DB6484E29",
    trailer_flag: int = 1,
    checksum: int = 0xFDB768EE,
    tail: bytes = b"",
    force_escape: bool = False,
) -> bytes:
    if abilities is None:
        abilities = [
            ("Command_CreateAHero_Charge_Level1", 0, 1),
            ("Command_CreateAHero_BattleRage_Level1", 1, 2),
            ("Command_CreateAHero_AxeThrow_Level1", 2, 3),
        ]
    if groups is None:
        groups = [(name, index) for index, name in enumerate(EXPECTED_GROUPS)]

    out = bytearray()
    out += magic
    out += struct.pack("<II", version, reserved)
    out += struct.pack("<BI", tag, selector)
    out += _unicode(name, force_escape=force_escape)
    out += struct.pack("<4I", *appearance)
    for color in colors:
        out += color
    for index in range(slot_count):
        if index < len(abilities):
            button, purchase, palantir = abilities[index]
            out += _ascii(button, force_escape=force_escape)
            out += struct.pack("<2I", purchase, palantir)
        else:
            out += _ascii("")
            out += struct.pack("<2I", *pad_words)
    out += struct.pack("<I", len(groups) if group_count is None else group_count)
    for group, value in groups:
        out += _ascii(group, force_escape=force_escape)
        out += struct.pack("<I", value)
    out += _ascii(hero_id)
    out += struct.pack("<BI", trailer_flag, checksum)
    out += tail
    return bytes(out)


# --- happy path -------------------------------------------------------------


def test_parses_every_field_of_a_well_formed_hero() -> None:
    hero = parse_cah(_build(), label="synthetic")

    assert hero.version == SUPPORTED_VERSION
    assert hero.header_tag == HEADER_TAG
    assert hero.header_selector == 19
    assert hero.name == "Hadhod"
    assert hero.appearance == (3, 1, 0, 0)
    assert hero.colors[0] == b"\xab\x7eN\xff"
    assert [ability.button for ability in hero.abilities] == [
        "Command_CreateAHero_Charge_Level1",
        "Command_CreateAHero_BattleRage_Level1",
        "Command_CreateAHero_AxeThrow_Level1",
    ]
    assert [ability.palantir_slot for ability in hero.abilities] == [1, 2, 3]
    assert [ability.purchase_index for ability in hero.abilities] == [0, 1, 2]
    assert [choice.group for choice in hero.groups] == EXPECTED_GROUPS
    assert hero.group_values["CreateAHero_Weapon"] == 0
    assert hero.hero_id == "E06BD0F22E8D478DB6484E29"
    assert hero.trailer_flag == 1
    assert hero.trailer_checksum == 0xFDB768EE
    assert hero.used_length_escape is False


def test_round_trip_is_byte_identical() -> None:
    raw = _build()
    assert parse_cah(raw, label="synthetic").to_bytes() == raw


def test_unused_ability_slots_are_not_reported_as_abilities() -> None:
    hero = parse_cah(_build(), label="synthetic")
    # Three purchased out of a fifteen-slot fixed array.
    assert len(hero.abilities) == 3
    assert ABILITY_SLOT_COUNT == 15


@pytest.mark.parametrize("flag", sorted(TRAILER_FLAGS))
def test_both_observed_trailer_flags_are_accepted(flag: int) -> None:
    hero = parse_cah(_build(trailer_flag=flag), label="synthetic")
    assert hero.trailer_flag == flag


def test_a_full_fifteen_slot_hero_round_trips() -> None:
    abilities = [(f"Command_CreateAHero_Ability{i}", i, (i % 5) + 1) for i in range(15)]
    raw = _build(abilities=abilities)
    hero = parse_cah(raw, label="synthetic")
    assert len(hero.abilities) == ABILITY_SLOT_COUNT
    assert hero.to_bytes() == raw


# --- the 0xFF length escape -------------------------------------------------


def test_long_strings_use_the_0xff_length_escape_and_round_trip() -> None:
    long_button = "Command_CreateAHero_" + "X" * 300
    raw = _build(abilities=[(long_button, 0, 1)])
    hero = parse_cah(raw, label="synthetic")

    assert hero.used_length_escape is True
    assert hero.abilities[0].button == long_button
    assert hero.to_bytes() == raw


def test_long_unicode_name_uses_the_escape_and_round_trips() -> None:
    raw = _build(name="H" * 400)
    hero = parse_cah(raw, label="synthetic")
    assert hero.used_length_escape is True
    assert len(hero.name) == 400
    assert hero.to_bytes() == raw


def test_escape_used_for_a_short_length_is_rejected() -> None:
    # A writer would never escape a length that fits in one byte; accepting it
    # would let the same string have two encodings and break round-tripping.
    raw = _build(force_escape=True)
    with pytest.raises(CahFormatError) as caught:
        parse_cah(raw, label="synthetic")
    assert "length escape" in caught.value.reason


# --- fail-closed: header ----------------------------------------------------


def test_rejects_wrong_magic() -> None:
    with pytest.raises(CahFormatError) as caught:
        parse_cah(_build(magic=b"ALAE1STR"), label="wrong-magic")
    assert caught.value.label == "wrong-magic"
    assert caught.value.offset == 0
    assert "magic" in caught.value.reason


def test_rejects_unsupported_version_without_guessing_a_layout() -> None:
    with pytest.raises(CahFormatError) as caught:
        parse_cah(_build(version=2), label="v2")
    assert caught.value.offset == 8
    assert "will not guess" in caught.value.reason


def test_rejects_non_zero_reserved_word() -> None:
    with pytest.raises(CahFormatError) as caught:
        parse_cah(_build(reserved=1), label="reserved")
    assert caught.value.offset == 12
    assert "reserved" in caught.value.reason


def test_rejects_unknown_header_tag() -> None:
    with pytest.raises(CahFormatError) as caught:
        parse_cah(_build(tag=9), label="tag")
    assert caught.value.offset == 16
    assert "header tag" in caught.value.reason


def test_rejects_empty_hero_name() -> None:
    with pytest.raises(CahFormatError) as caught:
        parse_cah(_build(name=""), label="noname")
    assert "name is empty" in caught.value.reason


def test_rejects_unpaired_surrogate_in_name() -> None:
    body = bytearray(_build(name="AB"))
    # Overwrite the two name code units with a lone high surrogate.
    start = body.index(b"A\x00B\x00")
    body[start : start + 4] = b"\x00\xd8\x00\xd8"
    with pytest.raises(CahFormatError) as caught:
        parse_cah(bytes(body), label="surrogate")
    assert "UTF-16LE" in caught.value.reason or "surrogate" in caught.value.reason


# --- fail-closed: ability array ---------------------------------------------


def test_rejects_non_zero_words_in_an_unused_ability_slot() -> None:
    with pytest.raises(CahFormatError) as caught:
        parse_cah(_build(pad_words=(0, 7)), label="pad")
    assert "zero-filled" in caught.value.reason


def test_rejects_a_populated_slot_after_an_empty_one() -> None:
    # Build the header by hand so the ability array can carry a hole.
    raw = bytearray()
    raw += CAH_MAGIC
    raw += struct.pack("<II", SUPPORTED_VERSION, 0)
    raw += struct.pack("<BI", HEADER_TAG, 19)
    raw += _unicode("Hadhod")
    raw += struct.pack("<4I", 3, 1, 0, 0)
    raw += b"\xab\x7eN\xff" + b"\xfe\x931\xff" + b"\xffp3\xff"
    raw += _ascii("") + struct.pack("<2I", 0, 0)
    raw += _ascii("Command_CreateAHero_Charge_Level1") + struct.pack("<2I", 0, 1)
    for _ in range(ABILITY_SLOT_COUNT - 2):
        raw += _ascii("") + struct.pack("<2I", 0, 0)
    raw += struct.pack("<I", 0)
    raw += _ascii("ID")
    raw += struct.pack("<BI", 1, 0)
    with pytest.raises(CahFormatError) as caught:
        parse_cah(bytes(raw), label="hole")
    assert "after an empty slot" in caught.value.reason


def test_rejects_out_of_order_purchase_index() -> None:
    with pytest.raises(CahFormatError) as caught:
        parse_cah(
            _build(abilities=[("Command_CreateAHero_Charge_Level1", 5, 1)]),
            label="order",
        )
    assert "purchase index" in caught.value.reason


def test_rejects_zero_palantir_slot() -> None:
    with pytest.raises(CahFormatError) as caught:
        parse_cah(
            _build(abilities=[("Command_CreateAHero_Charge_Level1", 0, 0)]),
            label="slot0",
        )
    assert "palantir slot" in caught.value.reason


def test_rejects_non_ascii_button_name() -> None:
    body = bytearray(_build())
    start = body.index(b"Command_CreateAHero_Charge_Level1")
    body[start] = 0x80
    with pytest.raises(CahFormatError) as caught:
        parse_cah(bytes(body), label="nonascii")
    assert "ASCII" in caught.value.reason


# --- fail-closed: groups ----------------------------------------------------


def test_rejects_duplicate_group() -> None:
    groups = [(name, 0) for name in EXPECTED_GROUPS]
    groups[3] = ("CreateAHero_Weapon", 2)
    with pytest.raises(CahFormatError) as caught:
        parse_cah(_build(groups=groups), label="dupe")
    assert "more than once" in caught.value.reason


def test_rejects_empty_group_name() -> None:
    groups = [(name, 0) for name in EXPECTED_GROUPS]
    groups[0] = ("", 0)
    with pytest.raises(CahFormatError) as caught:
        parse_cah(_build(groups=groups), label="emptygroup")
    assert "empty name" in caught.value.reason


def test_rejects_absurd_group_count_before_allocating() -> None:
    with pytest.raises(CahFormatError) as caught:
        parse_cah(_build(group_count=MAX_GROUP_COUNT + 1), label="huge")
    assert "limit" in caught.value.reason


def test_rejects_group_count_larger_than_the_payload() -> None:
    with pytest.raises(CahFormatError) as caught:
        parse_cah(_build(group_count=40), label="overcount")
    assert isinstance(caught.value, CahFormatError)


# --- fail-closed: trailer and framing ---------------------------------------


def test_rejects_unknown_trailer_flag() -> None:
    with pytest.raises(CahFormatError) as caught:
        parse_cah(_build(trailer_flag=2), label="flag2")
    assert "trailer flag" in caught.value.reason


def test_rejects_empty_hero_id() -> None:
    with pytest.raises(CahFormatError) as caught:
        parse_cah(_build(hero_id=""), label="noid")
    assert "hero id is empty" in caught.value.reason


def test_rejects_trailing_bytes_after_the_checksum() -> None:
    with pytest.raises(CahFormatError) as caught:
        parse_cah(_build(tail=b"\x00"), label="tail")
    assert "trailing bytes" in caught.value.reason


def test_rejects_a_file_over_the_size_limit() -> None:
    with pytest.raises(CahFormatError) as caught:
        parse_cah(b"\x00" * (MAX_CAH_BYTES + 1), label="huge")
    assert "limit" in caught.value.reason


def test_rejects_non_bytes_input() -> None:
    with pytest.raises(TypeError):
        parse_cah("not bytes")  # type: ignore[arg-type]


# --- fail-closed: the round-trip backstop -----------------------------------


def test_round_trip_mismatch_is_reported_rather_than_accepted(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """The backstop must fire even when every field check passes.

    Nothing reachable through the field checks can produce a mismatch, which is
    the point of a backstop, so the serialiser is perturbed to prove the guard
    is wired rather than decorative.
    """

    import openbfme_importer.sage_cah as module

    original = module.CahHero.to_bytes
    monkeypatch.setattr(
        module.CahHero, "to_bytes", lambda self: original(self) + b"\x00"
    )
    with pytest.raises(CahFormatError) as caught:
        parse_cah(_build(), label="roundtrip")
    assert "round-trip mismatch" in caught.value.reason


# --- structural robustness --------------------------------------------------


def test_every_truncation_fails_closed_with_a_structured_error() -> None:
    raw = _build()
    for cut in range(len(raw)):
        with pytest.raises(CahFormatError):
            parse_cah(raw[:cut], label=f"truncated-{cut}")


def test_byte_flips_never_escape_as_an_unstructured_exception() -> None:
    raw = _build()
    for index in range(0, len(raw), 7):
        for mask in (0x01, 0x80, 0xFF):
            mutated = bytearray(raw)
            mutated[index] ^= mask
            try:
                hero = parse_cah(bytes(mutated), label=f"flip-{index}")
            except CahFormatError:
                continue
            # If it parsed, it must still reproduce its own input exactly.
            assert hero.to_bytes() == bytes(mutated)


# --- census -----------------------------------------------------------------


def _census_entries() -> list[tuple[str, bytes]]:
    return [
        ("b.cah", _build(name="Berethor", selector=19, trailer_flag=1)),
        ("a.cah", _build(name="Krashnak", selector=57, trailer_flag=0)),
    ]


def test_census_is_deterministic_and_order_independent() -> None:
    entries = _census_entries()
    first = json.dumps(cah_census(entries), sort_keys=False)
    second = json.dumps(cah_census(list(reversed(entries))), sort_keys=False)
    assert first == second


def test_census_reports_structure_and_histograms() -> None:
    report = cah_census(_census_entries())
    assert report["schema"] == "openbfme.cah-census"
    assert report["fileCount"] == 2
    assert report["abilitySlotCapacity"] == ABILITY_SLOT_COUNT
    assert report["selectorHistogram"] == {"19": 1, "57": 1}
    assert report["trailerFlagHistogram"] == {"0": 1, "1": 1}
    assert report["abilityCountHistogram"] == {"3": 2}
    assert report["groupNameVocabularies"] == [EXPECTED_GROUPS]
    assert [entry["label"] for entry in report["files"]] == ["a.cah", "b.cah"]


def test_census_never_emits_authored_payload() -> None:
    report = cah_census(_census_entries())
    serialized = json.dumps(report)
    # Hero names, colours and chosen customisation indices are authored
    # content and must not reach a committable report.
    assert "Berethor" not in serialized
    assert "Krashnak" not in serialized
    assert "Command_CreateAHero" not in serialized


# --- retail oracle ----------------------------------------------------------


@pytest.mark.skipif(
    not RETAIL_BIG.is_file(), reason="no retail BFME2 install configured"
)
def test_every_shipped_retail_hero_decodes_and_round_trips() -> None:
    archive = BigArchive.open(RETAIL_BIG)
    entries = [
        entry
        for entry in archive.entries
        if entry.key.startswith(RETAIL_PREFIX) and entry.key.endswith(".cah")
    ]
    assert entries, "data1.big should ship data/systemheroes/*.cah"

    payloads = [
        (entry.name, archive.read_entry(entry, max_bytes=MAX_REAL_CAH_BYTES))
        for entry in entries
    ]

    for label, data in payloads:
        hero = parse_cah(data, label=label)
        assert hero.to_bytes() == data, label
        # Shipped heroes are level-10 builds against the twelve INI groups.
        assert len(hero.abilities) == 10, label
        assert [choice.group for choice in hero.groups] == EXPECTED_GROUPS, label
        assert hero.hero_id.lower() == label.rsplit("/", 1)[-1][7:-4].lower(), label
        assert hero.trailer_flag == 1, label

    report = cah_census(payloads)
    assert report["fileCount"] == len(payloads)
    assert report["abilityCountHistogram"] == {"10": len(payloads)}
    assert report["groupNameVocabularies"] == [EXPECTED_GROUPS]
    assert report["filesUsingLengthEscape"] == 0
