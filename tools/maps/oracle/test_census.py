"""The reference RefPack/EAR decoder is as strict as the C++ loader: a stream ends at its terminator command and
bytes after it are an error (Sol review of MAP-1, finding 8). Run: python -m pytest tools/maps/oracle -q
(no install needed: the streams are hand-built, the install variables only satisfy census.py's import check)."""
import os
import struct
import sys

from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# census.py refuses to import without the install variables; placeholders only for the import, restored at once (a process-wide value would make the
# later retail tests of the run fail on "unused" instead of skipping)
with mock.patch.dict(os.environ, {"ROTWK_INSTALL": os.environ.get("ROTWK_INSTALL") or "unused",
                                  "BFME2_INSTALL": os.environ.get("BFME2_INSTALL") or "unused"}):
    import census  # noqa: E402
import pytest  # noqa: E402

# header 10fb + 3-byte length 4; E0 = 4 literal bytes; FC = end, no trailing literals
STREAM = bytes([0x10, 0xFB, 0, 0, 4, 0xE0]) + b"ABCD" + bytes([0xFC])


def ear(stream, declared=4):
    return b"EAR\0" + struct.pack("<I", declared) + stream


def test_clean_stream_decodes():
    body, env = census.decode(ear(STREAM))
    assert body == b"ABCD"
    assert env == "EAR+refpack"


def test_bytes_after_the_terminator_are_refused():
    with pytest.raises(ValueError, match="4 trailing byte"):
        census.decode(ear(STREAM + b"JUNK"))


def test_declared_size_must_match():
    with pytest.raises(AssertionError):
        census.decode(ear(STREAM, declared=9))
