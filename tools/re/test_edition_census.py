"""Lane BFME2MODE-0: the edition census (tools/re/edition_census.py) on synthetic inputs (no game files), and the committed
docs/EDITIONS.md section it generates."""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import edition_census as ec  # noqa: E402

REPO = Path(__file__).resolve().parents[2]


def row(rva, size, tier, how="", b2="", b2size="", name="", sub="AI / pathfinding", **kw):
    r = {"rotwk_rva": rva, "size": str(size), "tier": tier, "how": how, "bfme2_rva": b2, "bfme2_size": str(b2size or size),
         "decomp_qualname": name, "decomp_source": "", "tu": "", "rotwk_name": "", "rtti": "", "subsystem": sub}
    r.update(kw)
    return r


def test_cites_follow_chains_and_keep_game_code_addresses_only():
    text = ("// RW 0x85750D -> 0x85730B; RW 0x8AF290 .. 0x8AF2EA: x87; (RW 0x660EC6 / 0x661869) and RW 0x4C (an offset)\n"
            "// BFME2 0x66B487 is not a RotWK cite; RW 0x66B3FD, 0x66B420; RW 0x1A00000 is outside the image")
    assert ec.extract_cites(text) == [0x85750D, 0x85730B, 0x8AF290, 0x8AF2EA, 0x660EC6, 0x661869, 0x66B3FD, 0x66B420]


def test_scan_tree_reads_gamelogic_and_common_only(tmp_path):
    for rel, body in {"engine/src/GameLogic/A.cpp": "// RW 0x600000\n", "engine/src/Common/B.h": "/* RW 0x600000 and RW 0x700010 */",
                      "engine/src/GameClient/C.cpp": "// RW 0x800000\n", "engine/src/Common/notes.txt": "RW 0x900000"}.items():
        p = tmp_path / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(body)
    cites = ec.scan_tree(tmp_path)
    assert cites == {0x600000: {"engine/src/GameLogic/A.cpp", "engine/src/Common/B.h"}, 0x700010: {"engine/src/Common/B.h"}}


def test_xmap_resolves_an_address_inside_a_function():
    m = ec.XMap([row("0x200000", 0x40, "A", b2="0x200100"), row("0x200040", 0x10, "-")])
    assert m.resolve_rva(0x200000)["rotwk_rva"] == "0x200000"
    assert m.resolve_rva(0x20003F)["rotwk_rva"] == "0x200000"
    assert m.resolve_rva(0x200045)["rotwk_rva"] == "0x200040"
    assert m.resolve_rva(0x200050) is None and m.resolve_rva(0x1FFFFF) is None
    assert m.bfme2_rvas() == {0x200100}


class FakeCtx:
    """one image: strings and .rdata words by VA"""
    def __init__(self, strings=None, rdata=None, xmap=None):
        self.strings, self.rdata, self.xmap = strings or {}, rdata or {}, xmap

    def in_image(self, v):
        return 0x400000 <= v < 0x1400000

    def section(self, v):
        return ".rdata" if 0xBD0000 <= v < 0xD89000 else ".data"

    def string(self, v):
        return self.strings.get(v)

    def value(self, v, size):
        return self.rdata.get(v)

    def pointer_like(self, data):
        return data is not None and self.in_image(int.from_bytes(data[:4], "little"))


def mem(base, disp, size=4, index=None):
    return ("mem", base, index, 1, disp, size)


def kinds(a, b, ca=None, cb=None, callee=None):
    return ec.compare_pair(a, b, ca or FakeCtx(), cb or FakeCtx(), callee)[0]


def test_layout_differences_are_offset_only():
    # a field offset, a vtable slot, the frame, the stack adjustment, this + offset, a register choice
    a = [("mov", (("reg", "eax"), mem("ecx", 0x5C)), None), ("call", (mem("eax", 0x58),), None),
         ("mov", (("reg", "edx"), mem("ebp", -0x1C)), None), ("sub", (("reg", "esp"), ("imm", 0x2C)), None),
         ("add", (("reg", "edi"), ("imm", 0x5A4)), None), ("mov", (("reg", "esi"), ("reg", "eax")), None)]
    b = [("mov", (("reg", "eax"), mem("ecx", 0x54)), None), ("call", (mem("eax", 0x50),), None),
         ("mov", (("reg", "edx"), mem("ebp", -0x18)), None), ("sub", (("reg", "esp"), ("imm", 0x28)), None),
         ("add", (("reg", "edi"), ("imm", 0x554)), None), ("mov", (("reg", "esi"), ("reg", "ebx")), None)]
    assert kinds(a, b) == {"offset", "frame", "register"}
    assert not kinds(a, b) & ec.REAL_KINDS


def test_constants_are_real_divergence():
    # a constructor default written to a field (a float 1.0 vs 0.5), a compared value
    a = [("mov", (mem("esi", 0x10), ("imm", 0x3F800000)), None), ("cmp", (("reg", "eax"), ("imm", 3)), None)]
    b = [("mov", (mem("esi", 0x10), ("imm", 0x3F000000)), None), ("cmp", (("reg", "eax"), ("imm", 3)), None)]
    assert kinds(a, b) == {"constant"}


def test_a_loaded_rdata_constant_is_compared_by_value_and_pointers_are_addresses():
    a = [("fld", (mem(None, 0xBE0000),), None), ("mov", (("reg", "eax"), mem(None, 0xBE0010)), None)]
    b = [("fld", (mem(None, 0xBD1000),), None), ("mov", (("reg", "eax"), mem(None, 0xBD1010)), None)]
    same_float = (FakeCtx(rdata={0xBE0000: b"\x00\x00\x80\x3f", 0xBE0010: b"\x10\x00\x50\x00"}),
                  FakeCtx(rdata={0xBD1000: b"\x00\x00\x80\x3f", 0xBD1010: b"\x20\x00\x50\x00"}))
    assert kinds(a, b, *same_float) == {"address"}
    other_float = (FakeCtx(rdata={0xBE0000: b"\x00\x00\x80\x3f", 0xBE0010: b"\x10\x00\x50\x00"}),
                   FakeCtx(rdata={0xBD1000: b"\x00\x00\x00\x40", 0xBD1010: b"\x20\x00\x50\x00"}))
    assert kinds(a, b, *other_float) == {"data-constant", "address"}


def test_strings_and_line_numbers():
    file_a, file_b = "D:\\build\\rotwk\\Code\\GameLogic\\Object.cpp", "E:\\bfme2\\Code\\GameLogic\\Object.cpp"
    ca = FakeCtx(strings={0xC00000: file_a, 0xC00100: "Upgrade_A"})
    cb = FakeCtx(strings={0xC10000: file_b, 0xC10100: "Upgrade_B"})
    a = [("push", (("imm", 0x107C),), None), ("push", (("imm", 0xC00000),), None), ("push", (("imm", 0xC00100),), None)]
    b = [("push", (("imm", 0x1049),), None), ("push", (("imm", 0xC10000),), None), ("push", (("imm", 0xC10100),), None)]
    k, where = ec.compare_pair(a, b, ca, cb)
    assert where == [(0, "line"), (1, "address"), (2, "constant")]
    assert k == {"line", "address", "constant"}


def test_branches_and_calls():
    a = [("jne", (("imm", ("L", 0x20)),), None), ("call", (("imm", ("ext",)),), 0x600000)]
    b = [("jne", (("imm", ("L", 0x24)),), None), ("call", (("imm", ("ext",)),), 0x610000)]
    assert kinds(a, b, callee=lambda t, u: "same") == {"opcode"}
    assert kinds(a[1:], b[1:], callee=lambda t, u: "diff") == {"call"}
    assert kinds(a[1:], b[1:], callee=lambda t, u: "unpaired") == {"call-unpaired"}
    assert kinds(a[1:], b[1:]) == set()  # calls not checked without the map
    assert kinds(a, a[:1]) == {"opcode"}  # not the same shape


def test_classification_without_binaries():
    assert ec.classify_function(row("0x1000", 8, "A", b2="0x1000"))["class"] == "same"
    assert ec.classify_function(row("0x1000", 8, "B", "sim,same-shape", "0x1000"))["class"] == "same-shape (unsplit)"
    e = ec.classify_function(row("0x1000", 8, "B", "sim,edited", "0x1000"))
    assert (e["class"], e["kinds"]) == ("divergent", ["branches"])
    assert ec.classify_function(row("0x1000", 8, "D", "sim", "0x1000"))["kinds"] == ["diverged"]
    assert ec.classify_function(row("0x1000", 8, "-"))["class"] == "rotwk-only"


def test_run_summary_and_the_generated_section():
    m = ec.XMap([row("0x200000", 0x40, "A", b2="0x200100", name="AIUpdate::a"), row("0x200040", 0x10, "-", name="", sub="AI / pathfinding?"),
                 row("0x200050", 0x20, "B", "sim,edited", "0x200200", name="Player::addMoney", sub="engine core / system")])
    cites = {0x600000: {"x.cpp"}, 0x600008: {"y.cpp"}, 0x600044: {"x.cpp"}, 0x600050: {"z.cpp"}, 0x900000: {"x.cpp"}}
    b2only = [{"bfme2_rva": "0x300000", "size": 10, "qualname": "LivingWorldLogic::tick", "source": "Code/GameLogic/LivingWorld.cpp",
               "subsystem": "War of the Ring / LivingWorld"}]
    res = ec.run(cites, m, b2only=b2only)
    s = ec.summarise(res)
    assert (s["functions"], s["same"], s["identical"], s["divergent"], s["rotwk_only"], s["bfme2_only"]) == (3, 1, 1, 1, 1, 1)
    assert s["unresolved"] == {"unknown (no image)": 1}
    assert s["subsystems"]["economy"] == {"divergent": 1}  # Player::addMoney: the economy bucket
    doc = "intro\n" + ec.BEGIN + "\nold\n" + ec.END + "\ntail\n"
    out = ec.replace_section(doc, ec.render(s, res, {"text": "synthetic"}))
    assert out.startswith("intro\n" + ec.BEGIN) and out.endswith(ec.END + "\ntail\n") and "old" not in out
    assert "| real divergence | 1 | branches 1 |" in out and "`LivingWorldLogic` (1): `tick`" in out


def test_bfme2_only_lists_unpaired_gamelogic_functions():
    m = ec.XMap([row("0x1000", 8, "A", b2="0x2000")])
    inv = [{"rva": "0x2000", "size": "8"}, {"rva": "0x3000", "size": "16"}, {"rva": "0x4000", "size": "16"}]
    ledger = [{"name": "?update@HordeContain@@UAEXXZ", "target_rva": "0x3000", "source": "Code/GameEngine/Source/GameLogic/Object/Contain/HordeContain.cpp"},
              {"name": "?draw@W3DThing@@UAEXXZ", "target_rva": "0x4000", "source": "Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DThing.cpp"}]
    out = ec.bfme2_only(m, inv, ledger)
    assert [(f["bfme2_rva"], f["qualname"], f["subsystem"]) for f in out] == [("0x3000", "HordeContain::update", "horde / locomotion")]


def test_the_committed_census_section_is_consistent():
    doc = (REPO / "docs" / "EDITIONS.md").read_text(encoding="utf-8")
    assert doc.count(ec.BEGIN) == 1 and doc.count(ec.END) == 1
    sec = doc.split(ec.BEGIN, 1)[1].split(ec.END, 1)[0]
    num = lambda pat: int(re.search(pat, sec).group(1))  # noqa: E731
    same, ident, off = (int(x) for x in re.search(r"\| same behaviour \| (\d+) \| tier A identical (\d+) \+ .*? (\d+) \|", sec).groups())
    div, rw_only, b2_only = num(r"\| real divergence \| (\d+) \|"), num(r"\| RotWK-only \| (\d+) \|"), num(r"\| BFME2-only GameLogic \| (\d+) \|")
    assert same == ident + off
    total = re.search(r"\| \*\*total\*\* \| (\d+) \| (\d+) \| (\d+) \| (\d+) \| (\d+) \|", sec).groups()
    assert tuple(int(x) for x in total) == (ident, off, div, rw_only, b2_only)
    assert num(r"(\d+) RotWK functions are cited") == same + div + rw_only
