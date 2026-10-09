"""Pins of the RotWK binary facts the draw module port cites (engine/src/GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/).
Runs only when RW_GAME_DAT is set (the path of the RotWK game.dat); each assertion is one instruction read from the image, so a
different binary or a wrong citation fails loudly. Caveat S-001: the image is the community-modified RotWK game.dat."""
from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "rw_object_model"))
pytestmark = pytest.mark.skipif(not os.environ.get("RW_GAME_DAT"), reason="RW_GAME_DAT not set (path of the RotWK game.dat)")


@pytest.fixture(scope="module")
def img():
    from rwimage import Image

    return Image()


def body(img, va, size):
    return [f"{i.address:x} {i.mnemonic} {i.op_str}" for i in img.decode(va, size)]


def test_static_sort_level_while_fading_constructor_default_is_minus_one(img):
    # W3DModelDrawModuleData constructor RW 0x4C85E9: `or dword ptr [esi + 0x158], 0xffffffff` at RW 0x4C86DC sets
    # StaticSortLevelWhileFading (table row offset 0x158) to -1
    first = body(img, 0x4C86DC, 8)[0]
    assert first == "4c86dc or dword ptr [esi + 0x158], 0xffffffff"
