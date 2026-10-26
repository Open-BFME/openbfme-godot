"""An isolated sync word in a signature is not an MP3 stream."""
import importlib.util
from pathlib import Path

import pytest

spec = importlib.util.spec_from_file_location("audio_gate", Path(__file__).with_name("precommit.py"))
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


@pytest.mark.parametrize("header", [bytes([255, 251, 144, 0]), bytes([255, 243, 64, 0]), bytes([255, 227, 24, 0])])
def test_mp3_detection_requires_two_complete_frames(header):
    size = gate.mp3_frame_size(header)
    frame = header + bytes(size - 4)
    assert gate.retail_content(frame * 2) == "MPEG Layer III audio"
    assert gate.retail_content(frame * 2) == gate.retail_content(frame * 3)
    assert gate.retail_content(header + bytes(60)) == ""
    assert gate.retail_content(frame) == ""
    assert gate.retail_content(frame + header) == ""
    assert gate.retail_content(frame + bytes(size)) == ""


def test_tagged_audio_still_fails_closed():
    assert gate.retail_content(b"ID3" + bytes(61)) == "ID3-tagged MP3 audio"
    assert gate.retail_content(b"RIFF" + bytes(4) + b"WAVE" + bytes(52)) == "RIFF/WAVE audio"
