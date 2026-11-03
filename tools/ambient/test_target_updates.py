"""Compare target ambient updates to the matching BFME2 donor (S-001 applies).

No binary contents are stored. RW_GAME_DAT and BFME2_INSTALL select local images.
This checks instruction structure/operands, not the identity of external callees.
"""
import os
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "re"))


@pytest.mark.skipif(not (os.getenv("RW_GAME_DAT") and os.getenv("BFME2_INSTALL")), reason="local target and donor images required")
@pytest.mark.parametrize("target,donor,size,count", [(0x882058, 0x87EDC9, 2916, 870), (0x8ACAFA, 0x8A9424, 499, 159)])
def test_target_update_instruction_structure_matches_donor(target, donor, size, count):
    from relib import Image
    from pair_diff import norm_listing

    rw = Image(os.environ["RW_GAME_DAT"])
    bf = Image(Path(os.environ["BFME2_INSTALL"]) / "game.dat")
    target_ops = norm_listing(rw, target - rw.base, size)
    donor_ops = norm_listing(bf, donor - bf.base, size)
    assert len(target_ops) == count
    assert len(donor_ops) == count

    def normalize(op):
        # Source __FILE__ spelling differs between the two builds.
        op = re.sub(r"'[^']*'", "SOURCE", op)
        # Animal runtime layout grew by 0xC4; data layout did not change.
        for new, old in [(0x49C, 0x3D8), (0x4A0, 0x3DC), (0x4A4, 0x3E0), (0x4A8, 0x3E4), (0x4AC, 0x3E8), (0x4AD, 0x3E9)]:
            op = op.replace(hex(new), hex(old))
        return op

    assert [normalize(op) for op in target_ops] == [normalize(op) for op in donor_ops]
