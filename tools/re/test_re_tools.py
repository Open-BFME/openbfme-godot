"""Unit tests for the RotWK -> BFME2 -> decomp mapping tools (pure parts: no game files needed)."""
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import inventory  # noqa: E402
import match_rotwk  # noqa: E402
import relib  # noqa: E402
import stops_xmap  # noqa: E402


def lcs_dp(a, b):
    prev = [0] * (len(b) + 1)
    for x in a:
        cur = [0]
        for j, y in enumerate(b):
            cur.append(prev[j] + 1 if x == y else max(prev[j + 1], cur[j]))
        prev = cur
    return prev[-1]


def test_bit_parallel_lcs_matches_dynamic_programming():
    rnd = random.Random(5)
    for _ in range(300):
        a = [rnd.choice("abcde") for _ in range(rnd.randint(0, 70))]
        b = [rnd.choice("abcde") for _ in range(rnd.randint(0, 70))]
        assert match_rotwk.lcs_len(a, b) == lcs_dp(a, b)


def test_indel_similarity():
    assert match_rotwk.similarity([], []) == 1.0
    assert match_rotwk.similarity(["mov r4,m4"], ["mov r4,m4"]) == 1.0
    assert match_rotwk.similarity(list("abcd"), list("abxd")) == 0.75  # 2*3/8


def test_qualname_reads_msvc_names():
    q = match_rotwk.qualname
    assert q("?privateMoveToPosition@AIUpdateInterface@@QAEXPBUCoord3D@@MW4CommandSourceType@@@Z") == \
        "AIUpdateInterface::privateMoveToPosition"
    assert q("??0GeometryInfo@@QAE@XZ") == "GeometryInfo::GeometryInfo"
    assert q("??1DisplayString@@UAE@XZ") == "DisplayString::~DisplayString"
    assert q("??_GFoo@Bar@@UAEPAXI@Z") == "Bar::Foo::`scalar deleting destructor'"
    assert q("_compress2") == "_compress2"


def test_file_strings_hash_by_basename():
    a = relib.salt_string("E:\\Builds\\BFME2X\\Code\\production\\Code\\GameEngine\\Source\\X\\Foo.cpp")
    b = relib.salt_string("C:\\projects\\bfme2patch10\\Code\\GameEngine\\Source\\X\\foo.CPP")
    assert a == b == "__FILE__:foo.cpp"
    assert relib.salt_string("Weapon %s not found") == "Weapon %s not found"


class FakeImage:
    def __init__(self, mem, text):
        self.mem, self.text = bytearray(mem), text


def test_inventory_extent_runs_to_next_entry_without_int3_padding():
    mem = bytes(16) + b"\x55\x8b\xec\xc3\xcc\xcc\xcc\xcc" + b"\xc3\x90\xcc\xcc" + b"\x90\x90"
    img = FakeImage(mem, (16, 30))
    inv = inventory.build(img, [(16, "FUN_a"), (24, "b"), (28, "c"), (24, "FUN_dup"), (40, "outside")])
    assert inv == [(16, 4, 4, "FUN_a"), (24, 2, 2, "b"), (28, 2, 2, "c")]


def feat(rva, mhash, size=32):
    return {"rva": rva, "size": size, "blk": size, "name": "", "mhash": mhash, "shash": b"s%d" % rva,
            "shape": ["op"] * 8, "callees": [], "strs": [], "ext": []}


def test_ambiguous_identical_bodies_follow_link_order():
    # unique anchors at 0x1000 / 0x5000 shift by +0x100; two identical getters must not cross
    R = [feat(0x1000, b"u1"), feat(0x2000, b"dup"), feat(0x4000, b"dup"), feat(0x5000, b"u2")]
    T = [feat(0x1100, b"u1"), feat(0x2100, b"dup"), feat(0x4100, b"dup"), feat(0x5100, b"u2"),
         feat(0x9000, b"dup")]
    m = match_rotwk.Matcher(R, T).run(log=lambda s: None)
    assert m[1][0] == 1 and m[2][0] == 2
    assert m[1][1] == "A" and m[1][3].startswith("amb2x3") and not m[1][3].endswith("far")


def test_stop_addresses_take_the_game_named_before_them():
    text = "| S-9 | code | AI | aiIdle RW 0x5E821A, setter BFME2 0x00ADB36E; getter 0x00B0B3F4 (BFME1 0x00401000) |"
    assert stops_xmap.cited(text) == [(0x5E821A, "RW"), (0xADB36E, "BFME2"), (0xB0B3F4, "RW"), (0x401000, "BFME1")]


def test_stop_impact_uses_area_keywords():
    assert stops_xmap.impact("Horde melee approach", "") == 3
    assert stops_xmap.impact("W3D animation", "") == 2
    assert stops_xmap.impact("Disconnect path (MP-2)", "") == 1
    assert stops_xmap.impact("INI block bodies", "") == 2
