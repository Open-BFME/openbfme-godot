"""Lane CONTRIB-1: tools/release/big_entries.py on archives built here (no game bytes)."""
from __future__ import annotations

import hashlib
import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import big_entries  # noqa: E402


def make_big(files: list[tuple[str, bytes]]) -> bytes:
    directory = sum(8 + len(n) + 1 for n, _ in files)
    offset, head, body = 16 + directory, b"", b""
    for name, data in files:
        head += struct.pack(">II", offset, len(data)) + name.encode() + b"\0"
        offset += len(data)
        body += data
    return b"BIG4" + struct.pack("<I", offset) + struct.pack(">II", len(files), 16 + directory) + head + body


def test_lists_each_distinct_version_once_in_the_policys_form():
    big = make_big([("Font.TTF", b"A"), ("data/scripts/scripts.lua", b"lua"), ("font.ttf", b"A"), ("font.ttf", b"B")])
    assert big_entries.entries(big) == [
        {"name": "font.ttf", "size": 1, "md5": hashlib.md5(b"A").hexdigest()},
        {"name": "data\\scripts\\scripts.lua", "size": 3, "md5": hashlib.md5(b"lua").hexdigest()},
        {"name": "font.ttf", "size": 1, "md5": hashlib.md5(b"B").hexdigest()},
    ]


@pytest.mark.parametrize("mutate, message", [
    (lambda b: b[:3], "not a BIG archive"),
    (lambda b: b[:8] + struct.pack(">I", 0x7F000002) + b[12:], f"declares {0x7F000002} entries, more than it can hold"),
    (lambda b: b[:16] + struct.pack(">I", 0x7F000000) + b[20:], "entry 'font.ttf' runs past the end of the archive"),
])
def test_hostile_directories_are_refused_by_name(mutate, message):
    big = make_big([("font.ttf", b"A"), ("data\\x.lua", b"x")])
    with pytest.raises(ValueError, match=message.replace("(", r"\(")):
        big_entries.entries(mutate(big))


# a directory of two empty entries at offset 0 (always in range), cut inside the second entry's header, then inside its name
EMPTY_ENTRIES = (b"BIG4" + struct.pack("<I", 54) + struct.pack(">II", 2, 54) + struct.pack(">II", 0, 0) + b"aaaaaaaaaa\0" + struct.pack(">II", 0, 0)
                 + b"bbbbbbbbbb\0")


@pytest.mark.parametrize("size, message", [(40, "the directory is cut short at entry 1"), (48, "the name of entry 1 is cut short or too long")])
def test_cut_directories_are_refused_by_name(size, message):
    assert len(EMPTY_ENTRIES) == 54
    assert big_entries.entries(EMPTY_ENTRIES) == [{"name": "aaaaaaaaaa", "size": 0, "md5": hashlib.md5(b"").hexdigest()},
                                                  {"name": "bbbbbbbbbb", "size": 0, "md5": hashlib.md5(b"").hexdigest()}]
    with pytest.raises(ValueError, match=message):
        big_entries.entries(EMPTY_ENTRIES[:size])


def test_overlapping_entries_are_refused_before_they_are_hashed_again():
    # 1000 entries all naming the same 4 KB range: 1000 x 4 KB of hashing from a ~40 KB file
    names = [f"e{i:04d}" for i in range(1000)]
    directory = sum(8 + len(n) + 1 for n in names)
    head = b"".join(struct.pack(">II", 16 + directory, 4096) + n.encode() + b"\0" for n in names)
    big = b"BIG4" + struct.pack("<I", 0) + struct.pack(">II", len(names), 16 + directory) + head + bytes(4096)
    with pytest.raises(ValueError, match="its entries overlap"):
        big_entries.entries(big)


def test_main_refuses_an_oversized_file_unread_and_names_an_unreadable_one(tmp_path, capsys):
    huge = tmp_path / "huge.big"
    with open(huge, "wb") as f:
        f.truncate(big_entries.MAX_FILE + 1)  # sparse
    assert big_entries.main(["x", str(huge)]) == 1
    assert "far larger than any known Data1.big" in capsys.readouterr().err
    assert big_entries.main(["x", str(tmp_path / "missing.big")]) == 1
    assert "cannot read it" in capsys.readouterr().err
