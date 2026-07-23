"""Tests for the SAGE ``.csf`` binary string-table decoder."""

from __future__ import annotations

import struct
from pathlib import Path

import pytest

try:
    from openbfme_importer.big import BigArchive
    from openbfme_importer.sage_csf import (
        CsfCatalog,
        CsfFormatError,
        SUPPORTED_MAX_VERSION,
        parse_csf,
    )
except ModuleNotFoundError:  # Supports the repository-root acceptance command.
    from importer.openbfme_importer.big import BigArchive
    from importer.openbfme_importer.sage_csf import (
        CsfCatalog,
        CsfFormatError,
        SUPPORTED_MAX_VERSION,
        parse_csf,
    )


REAL_INSTALLS = {
    "bfme2": Path("F:/BFME2/lang/english.big"),
    "rotwk": Path("F:/RotWK/lang/english.big"),
}
MAX_REAL_CSF_BYTES = 32 * 1024 * 1024


def _not_utf16(text: str) -> bytes:
    return bytes(byte ^ 0xFF for byte in text.encode("utf-16-le"))


def _header(
    *,
    magic: bytes = b" FSC",
    version: int = 3,
    labels: int,
    strings: int,
    language: int = 0,
) -> bytes:
    return struct.pack("<4sIIIII", magic, version, labels, strings, 0, language)


def _label_block(label: str, pairs: list[bytes], *, magic: bytes = b" LBL") -> bytes:
    encoded = label.encode("ascii")
    return (
        struct.pack("<4sII", magic, len(pairs), len(encoded))
        + encoded
        + b"".join(pairs)
    )


def _plain_string(text: str, *, magic: bytes = b" RTS") -> bytes:
    return struct.pack("<4sI", magic, len(text)) + _not_utf16(text)


def _wide_string(text: str, extra: str) -> bytes:
    encoded_extra = extra.encode("ascii")
    return (
        struct.pack("<4sI", b"WRTS", len(text))
        + _not_utf16(text)
        + struct.pack("<I", len(encoded_extra))
        + encoded_extra
    )


# ---------------------------------------------------------------------------
# Synthetic round-trip fixtures
# ---------------------------------------------------------------------------


def test_parses_single_plain_string() -> None:
    data = _header(labels=1, strings=1) + _label_block(
        "UI:Alpha", [_plain_string("Grey Havens")]
    )
    catalog = parse_csf(data)
    assert isinstance(catalog, CsfCatalog)
    assert catalog.version == 3
    assert catalog.language_code == 0
    assert list(catalog.records.items()) == [("UI:Alpha", "Grey Havens")]
    assert catalog.resolve("ui:alpha") == "Grey Havens"
    assert catalog.record("UI:Alpha").extra is None
    report = catalog.to_report()
    assert report["recordCount"] == 1
    assert report["wideRecordCount"] == 0
    assert report["version"] == 3
    assert len(report["sourceSha256"]) == 64


def test_parses_wrts_row_with_extra_ascii_string() -> None:
    data = _header(labels=2, strings=2, version=2, language=9) + _label_block(
        "UI:Wide", [_wide_string("Voice line", "eowyn_taunt.wav")]
    ) + _label_block("UI:Plain", [_plain_string("Plain")])
    catalog = parse_csf(data)
    assert catalog.version == 2
    assert catalog.language_code == 9
    assert list(catalog.records) == ["UI:Wide", "UI:Plain"]
    assert catalog.records["UI:Wide"] == "Voice line"
    assert catalog.record("ui:wide").extra == "eowyn_taunt.wav"
    assert catalog.record("UI:Plain").extra is None
    report = catalog.to_report()
    assert report["recordCount"] == 2
    assert report["wideRecordCount"] == 1
    assert report["languageCode"] == 9


def test_parses_empty_catalog() -> None:
    catalog = parse_csf(_header(labels=0, strings=0, version=0))
    assert len(catalog) == 0
    assert catalog.records == {}
    assert catalog.resolve("anything") is None
    report = catalog.to_report()
    assert report["recordCount"] == 0
    assert report["maxLabelChars"] == 0
    assert report["maxValueChars"] == 0


def test_roundtrip_non_ascii_value_survives_not_encoding() -> None:
    text = "Théoden — König’s hall"
    data = _header(labels=1, strings=1) + _label_block(
        "UI:Umlaut", [_plain_string(text)]
    )
    assert parse_csf(data).records["UI:Umlaut"] == text


# ---------------------------------------------------------------------------
# Fail-closed behaviour
# ---------------------------------------------------------------------------


def test_rejects_bad_file_magic() -> None:
    data = _header(magic=b"CSF ", labels=0, strings=0)
    with pytest.raises(CsfFormatError, match="magic"):
        parse_csf(data)


def test_rejects_unsupported_version() -> None:
    data = _header(labels=0, strings=0, version=SUPPORTED_MAX_VERSION + 1)
    with pytest.raises(CsfFormatError, match="version"):
        parse_csf(data)


def test_rejects_truncated_header() -> None:
    with pytest.raises(CsfFormatError, match="truncated"):
        parse_csf(b" FSC\x03\x00\x00\x00")


def test_rejects_truncated_label_block() -> None:
    data = _header(labels=1, strings=1) + struct.pack("<4sII", b" LBL", 1, 64)
    with pytest.raises(CsfFormatError, match="truncated"):
        parse_csf(data)


def test_rejects_truncated_string_payload() -> None:
    block = _label_block("UI:Cut", [_plain_string("Full value")])
    data = _header(labels=1, strings=1) + block[:-4]
    with pytest.raises(CsfFormatError, match="truncated"):
        parse_csf(data)


def test_rejects_missing_label_block_for_declared_count() -> None:
    data = _header(labels=2, strings=2) + _label_block(
        "UI:Only", [_plain_string("One")]
    )
    with pytest.raises(CsfFormatError, match="truncated"):
        parse_csf(data)


def test_rejects_header_string_count_mismatch() -> None:
    data = _header(labels=1, strings=2) + _label_block(
        "UI:One", [_plain_string("One")]
    )
    with pytest.raises(CsfFormatError, match="declares 2 strings"):
        parse_csf(data)


def test_rejects_non_lbl_block_magic() -> None:
    data = _header(labels=1, strings=1) + _label_block(
        "UI:Bad", [_plain_string("Value")], magic=b" XBL"
    )
    with pytest.raises(CsfFormatError, match="expected b' LBL'"):
        parse_csf(data)


def test_rejects_unknown_string_magic() -> None:
    data = _header(labels=1, strings=1) + _label_block(
        "UI:Bad", [_plain_string("Value", magic=b"XRTS")]
    )
    with pytest.raises(CsfFormatError, match="XRTS"):
        parse_csf(data)


def test_rejects_trailing_bytes() -> None:
    data = (
        _header(labels=1, strings=1)
        + _label_block("UI:One", [_plain_string("One")])
        + b"\x00"
    )
    with pytest.raises(CsfFormatError, match="trailing"):
        parse_csf(data)


def test_rejects_duplicate_case_insensitive_labels() -> None:
    data = _header(labels=2, strings=2) + _label_block(
        "UI:Dup", [_plain_string("First")]
    ) + _label_block("ui:dup", [_plain_string("Second")])
    with pytest.raises(CsfFormatError, match="duplicate"):
        parse_csf(data)


def test_rejects_empty_label_name() -> None:
    data = _header(labels=1, strings=1) + _label_block(
        "", [_plain_string("Value")]
    )
    with pytest.raises(CsfFormatError, match="empty"):
        parse_csf(data)


# ---------------------------------------------------------------------------
# Real retail launcher catalogs (skipped when the installs are absent)
# ---------------------------------------------------------------------------


def _real_csf_payloads(archive_path: Path) -> list[tuple[str, bytes]]:
    archive = BigArchive.open(archive_path)
    payloads = []
    for entry in archive.entries:
        if entry.name.casefold().endswith(".csf"):
            payloads.append(
                (entry.name, archive.read_entry(entry, max_bytes=MAX_REAL_CSF_BYTES))
            )
    return payloads


@pytest.mark.parametrize("install", sorted(REAL_INSTALLS))
def test_real_launcher_csf_parses(install: str) -> None:
    archive_path = REAL_INSTALLS[install]
    if not archive_path.is_file():
        pytest.skip(f"retail archive not present: {archive_path}")
    payloads = _real_csf_payloads(archive_path)
    assert payloads, f"no .csf entries found in {archive_path}"
    for name, data in payloads:
        catalog = parse_csf(data)
        assert len(catalog) > 0, name
        labels = [row.label for row in catalog.rows]
        assert all(labels), name
        folded = [label.casefold() for label in labels]
        assert len(set(folded)) == len(folded), name
        assert catalog.version <= SUPPORTED_MAX_VERSION
        assert catalog.declared_string_count >= len(catalog)
        report = catalog.to_report()
        assert report["recordCount"] == len(catalog)
        assert report["maxValueChars"] > 0
