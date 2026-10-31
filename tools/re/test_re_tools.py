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


def test_equal_shape_siblings_follow_link_order():
    # three same-shape siblings that differ only in a constant (no tier A): each RotWK copy pairs
    # with the BFME2 sibling at the same link-order offset, not with the first free one
    R = [feat(0x1000, b"u1"), feat(0x1040, b"r1"), feat(0x1080, b"r2"), feat(0x10C0, b"r3"), feat(0x1200, b"u2")]
    T = [feat(0x2000, b"u1"), feat(0x2040, b"t1"), feat(0x2080, b"t2"), feat(0x20C0, b"t3"), feat(0x2200, b"u2")]
    for i in (1, 2, 3):
        R[i]["shash"] = T[i]["shash"] = b"same"
    m =match_rotwk.Matcher(R, T).run(log=lambda s: None)
    assert [m[i][0] for i in (1, 2, 3)] == [1, 2, 3]
    assert all(m[i][1] == "B" for i in (1, 2, 3))


def _tie_case(t2_shape_edit):
    # RotWK r (0x1040) calls c; BFME2 t1 sits at the predicted 0x2040 but calls d, t2 (0x2080) calls c's pair
    R = [feat(0x1000, b"u1"), feat(0x1040, b"r"), feat(0x1100, b"c"), feat(0x1140, b"d"), feat(0x1200, b"u2")]
    T = [feat(0x2000, b"u1"), feat(0x2040, b"t1"), feat(0x2080, b"t2"), feat(0x2100, b"c"), feat(0x2140, b"d"),
         feat(0x2200, b"u2")]
    for f in (R[1], T[1], T[2]):
        f["shape"] = ["op"] * 16
    if t2_shape_edit:
        T[2]["shape"] = ["op"] * 15 + ["other"]  # similarity 0.9375: still tier B, below t1's 1.0
    R[1]["callees"], T[1]["callees"], T[2]["callees"] = [0x1100], [0x2140], [0x2100]
    return match_rotwk.Matcher(R, T).run(log=lambda s: None)


def test_equal_score_tie_prefers_matched_callees_over_link_order():
    assert _tie_case(False)[1][0] == 2


def test_higher_score_beats_matched_callees():
    assert _tie_case(True)[1][0] == 1


def _ledger(tmp_path, rows):
    p = tmp_path / "ledger.csv"
    p.write_text("name,target_rva,target_size,source,lane\n"
                 + "".join(f"{n},{rva:#010x},{size},{src},{lane}\n" for n, rva, size, src, lane in rows))
    return match_rotwk.Ledger(p, 0x1000, 0x2000)


def test_ledger_reads_every_source_lane_of_the_clone(tmp_path):
    # Open-BFME-2's tools/progress.py SOURCE_LANES; an unknown lane must stay an error, not a default
    lanes = ("authored", "vendored", "generated", "library", "data", "assembly", "dump")
    assert set(lanes) == set(match_rotwk.LANE_RANK)
    led = _ledger(tmp_path,
                  [(f"f{i}", 0x1000 + 0x40 * i, 0x40, f"src{i}.x", lane) for i, lane in enumerate(lanes)])
    for i, lane in enumerate(lanes):
        st, src, cpp = led.status(0x1000 + 0x40 * i, 0x40)
        assert st == lane and src == (f"src{i}.x", f"f{i}")
        assert cpp == (1.0 if lane in match_rotwk.CPP_LANES else 0.0)


def test_assembly_is_not_counted_as_cpp_and_cpp_wins_overlaps(tmp_path):
    led = _ledger(tmp_path, [("asm", 0x1000, 0x100, "a.asm", "assembly"),
                             ("cpp", 0x1080, 0x80, "c.cpp", "authored")])
    # the assembly row starts the extent and names it; the C++ row painted over its tail is the C++ part
    st, src, cpp = led.status(0x1000, 0x100)
    assert (st, src, cpp) == ("assembly", ("a.asm", "asm"), 0.5)
    try:
        _ledger(tmp_path, [("x", 0x1000, 4, "x.bin", "mystery")])
    except KeyError:
        pass
    else:
        raise AssertionError("an unknown ledger lane must fail")


def test_attempts_may_be_empty_only_in_a_real_clone(tmp_path):
    (tmp_path / "reverse").mkdir()
    try:
        match_rotwk.load_attempts(tmp_path)
    except SystemExit:
        pass
    else:
        raise AssertionError("a folder without the decomp ledger is not a clone")
    (tmp_path / "reverse" / "functions.csv").write_text("name\n")
    assert match_rotwk.load_attempts(tmp_path) == {}
    (tmp_path / "reverse" / "attempts").mkdir()
    (tmp_path / "reverse" / "attempts" / "0x00401000.cpp").write_text("// Foo::bar\n// score=0.9731\n")
    assert match_rotwk.load_attempts(tmp_path) == {0x401000: ("reverse/attempts/0x00401000.cpp", "Foo::bar", "0.973")}


def test_dominant_and_partial_lanes_count_every_rank(tmp_path):
    led = _ledger(tmp_path, [("asm", 0x1000, 0x40, "a.asm", "assembly"),
                             ("cpp", 0x1040, 0x60, "c.cpp", "authored")])
    # starts inside the assembly row: 0x30 assembly + 0x60 authored, all covered, under 95% C++ -> dominant lane
    assert led.status(0x1010, 0x90)[0] == "authored"
    # half outside every row -> partial, still naming the top rank as dominant
    assert led.status(0x1080, 0x40)[0] == "partial-authored"
    assert led.status(0x1020, 0x10)[0] == "assembly"
