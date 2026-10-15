"""Lane HOVER-1: the inventory tool's readers on synthetic input (no game files needed)."""
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hover_inventory as inv  # noqa: E402


def test_string_table_labels_compare_case_insensitively():
    raw = b'// comment\r\nTOOLTIP:Cost\r\n// context\r\n"Cost: %d"\r\nEND\r\n\r\nCONTROLBAR:Two\r\n"a"\r\nEND\r\n'
    t = inv.string_table(raw)
    assert t == {"tooltip:cost": "Cost: %d", "controlbar:two": "a"}


def test_blocks_keep_every_field_and_skip_nested_blocks():
    text = """
CommandButton Command_A ; a comment
  Command = SPECIAL_POWER
  TextLabel = CONTROLBAR:A
  Nested
    Inner = 1
  End
  Science = SCIENCE_X SCIENCE_Y
End
CommandSet SetB
  1 = Command_A
  2 = Command_A
End
"""
    buttons = dict(inv.blocks(text, "CommandButton"))
    assert buttons == {"Command_A": {"Command": ["SPECIAL_POWER"], "TextLabel": ["CONTROLBAR:A"], "Science": ["SCIENCE_X SCIENCE_Y"]}}
    sets = dict(inv.blocks(text, "CommandSet"))
    assert sets == {"SetB": {"1": ["Command_A"], "2": ["Command_A"]}}


def test_cost_branch_follows_rw_0x807a81():
    assert inv.cost_branch({"Command": ["UNIT_BUILD"], "Object": ["GondorFighter"]})[0] == "build cost"
    assert inv.cost_branch({"Command": ["OBJECT_UPGRADE"], "Object": ["X"], "Upgrade": ["Upgrade_Y"]})[0] == "upgrade cost"
    assert inv.cost_branch({"Command": ["PURCHASE_SCIENCE"], "Science": ["A B"]})[0] == "science cost"
    assert inv.cost_branch({"Command": ["SPECIAL_POWER"]})[0] == "unit cost"
    assert inv.cost_branch({"Command": ["REVIVE"]})[0] == "revive cost"
    assert inv.cost_branch({"Command": ["CASTLE_UNPACK"]})[0] == "castle unpack cost"
    assert inv.cost_branch({"Command": ["STOP"]})[0] == "none"


def test_blocks_accept_a_block_without_a_name():
    text = "AptButtonTooltipMap\n  ButtonMap = MainMenu/A/bttn OpeningMenu/AButton\n  ;ButtonMap = X Y\nEND\n"
    assert dict(inv.blocks(text, "AptButtonTooltipMap")) == {"": {"ButtonMap": ["MainMenu/A/bttn OpeningMenu/AButton"]}}


# ---- the inventory refuses an empty or incomplete input (nothing is written); each refusal has its test -------------------------------------------------
import csv  # noqa: E402

import pytest  # noqa: E402


def write_areas(tmp_path, areas, movies, area_header=None, movie_header=None):
    a = tmp_path / "apt_hover_areas.csv"
    with open(a, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(area_header or inv.AREAS_HEADER)
        w.writerows(areas)
    with open(tmp_path / "apt_movies.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(movie_header or inv.MOVIES_HEADER)
        w.writerows(movies)
    return a


GOOD_AREAS = [["MainMenu", "MainMenu/A/bttn", "no", "0/9", "TOOLTIP:MainMenu/A/bttn", "yes"], ["Options", "Options:Exp/b", "yes", "3", "", ""]]
GOOD_MOVIES = [["MainMenu", "1"], ["Options", "1"]]


def test_areas_accepted_when_the_walk_covers_every_movie(tmp_path):
    rows = inv.load_areas(write_areas(tmp_path, GOOD_AREAS, GOOD_MOVIES), ["mainmenu.apt", "options.apt"])
    assert [r["path"] for r in rows] == ["MainMenu/A/bttn", "Options:Exp/b"]


def test_empty_areas_file_is_refused(tmp_path):
    with pytest.raises(inv.InventoryError, match="has no rows"):
        inv.load_areas(write_areas(tmp_path, [], GOOD_MOVIES), ["mainmenu.apt", "options.apt"])


def test_missing_areas_file_is_refused(tmp_path):
    with pytest.raises(inv.InventoryError, match="does not exist"):
        inv.load_areas(tmp_path / "nothing.csv", ["mainmenu.apt"])


def test_areas_file_with_the_wrong_columns_is_refused(tmp_path):
    with pytest.raises(inv.InventoryError, match="has the columns"):
        inv.load_areas(write_areas(tmp_path, [["a", "b"]], GOOD_MOVIES, area_header=["movie", "path"]), ["mainmenu.apt", "options.apt"])


def test_areas_without_the_movie_list_is_refused(tmp_path):
    a = write_areas(tmp_path, GOOD_AREAS, GOOD_MOVIES)
    (tmp_path / "apt_movies.csv").unlink()
    with pytest.raises(inv.InventoryError, match="walked movies file"):
        inv.load_areas(a, ["mainmenu.apt", "options.apt"])


def test_a_movie_of_the_install_the_walk_missed_is_refused(tmp_path):
    with pytest.raises(inv.InventoryError, match="missing"):
        inv.load_areas(write_areas(tmp_path, GOOD_AREAS, GOOD_MOVIES), ["mainmenu.apt", "options.apt", "lobby.apt"])


def test_a_movie_the_install_does_not_have_is_refused(tmp_path):
    with pytest.raises(inv.InventoryError, match="not in the install"):
        inv.load_areas(write_areas(tmp_path, GOOD_AREAS, GOOD_MOVIES), ["mainmenu.apt"])


def test_a_movie_whose_area_count_differs_is_refused(tmp_path):
    with pytest.raises(inv.InventoryError, match="says 2 areas"):
        inv.load_areas(write_areas(tmp_path, GOOD_AREAS, [["MainMenu", "2"], ["Options", "1"]]), ["mainmenu.apt", "options.apt"])


def test_areas_without_a_labelled_static_area_are_refused(tmp_path):
    rows = [["MainMenu", "MainMenu/A/bttn", "no", "0", "", ""], ["Options", "Options:Exp/b", "yes", "3", "", ""]]
    with pytest.raises(inv.InventoryError, match="no labelled static area"):
        inv.load_areas(write_areas(tmp_path, rows, GOOD_MOVIES), ["mainmenu.apt", "options.apt"])


def test_main_refuses_with_exit_2_and_writes_nothing(tmp_path, monkeypatch, capsys):
    out = tmp_path / "out"
    monkeypatch.setattr(inv.bigfs, "Mount", lambda: type("M", (), {"list": lambda self, d, e: ["a.apt"], "read": lambda self, p: b""})())
    monkeypatch.setattr(sys, "argv", ["x", "--out", str(out), "--apt-areas", str(write_areas(tmp_path, [], GOOD_MOVIES))])
    assert inv.main() == 2
    assert "REFUSED" in capsys.readouterr().err
    assert not out.exists()


WND = 'WINDOW\n NAME = "Win.wnd:Ok";\n TOOLTIPTEXT = "TOOLTIP:Ok";\n TOOLTIPCALLBACK = "[None]";\n WINDOW\n NAME = "Win.wnd:Bar";\n TOOLTIPCALLBACK = "GameWinDefaultTooltip";\n'


def test_wnd_tooltips_lists_the_text_and_the_generic_callback_only():
    rows = inv.wnd_tooltips({"window\\win.wnd": WND})
    assert [(r["gadget"], r["field"], r["value"]) for r in rows] == [("Win.wnd:Ok", "TOOLTIPTEXT", "TOOLTIP:Ok"), ("Win.wnd:Bar", "TOOLTIPCALLBACK", "GameWinDefaultTooltip")]


def test_wnd_files_with_no_gadget_are_refused():
    with pytest.raises(inv.InventoryError, match="no gadget"):
        inv.wnd_tooltips({"window\\win.wnd": "// nothing\n"})
    with pytest.raises(inv.InventoryError, match="no gadget"):
        inv.wnd_tooltips({})


def test_an_unnamed_ini_tooltip_field_is_refused():
    assert inv.ini_tooltip_fields({"a.ini": "Tooltip = X\nTooltipWidth = 3 ; c\n"}) == {"Tooltip": [("a.ini", "X")], "TooltipWidth": [("a.ini", "3")]}
    with pytest.raises(inv.InventoryError, match="TooltipSurprise"):
        inv.ini_tooltip_fields({"a.ini": "TooltipSurprise = 1\n"})
    with pytest.raises(inv.InventoryError, match="no INI file"):
        inv.ini_tooltip_fields({})


def test_code_strings_must_all_be_named_and_all_present():
    all_labels = {l for _, ls in inv.CODE_FAMILIES.values() for l in ls}
    dat = ("\0".join(sorted(all_labels)) + "\0ShowToolTip\0").encode()
    assert inv.code_tooltip_strings(dat) == all_labels  # the mechanism's own names are not sources
    assert set(inv.classify_code_strings(all_labels)) == all_labels
    with pytest.raises(inv.InventoryError, match="in no family.*TOOLTIP:BrandNew"):
        inv.classify_code_strings(all_labels | {"TOOLTIP:BrandNew"})
    with pytest.raises(inv.InventoryError, match="not in the executable"):
        inv.classify_code_strings(all_labels - {"TOOLTIP:Cost"})
    with pytest.raises(inv.InventoryError, match="empty"):
        inv.code_tooltip_strings(b"")


def test_the_lobby_start_position_tips_are_named():
    fam = {l: f for f, (_, ls) in inv.CODE_FAMILIES.items() for l in ls}
    assert fam["TOOLTIP:StartPosition"] == fam["TOOLTIP:StartPositionN"] and fam["TOOLTIP:StartPosition"].startswith("lobby")


def test_a_label_fetched_by_the_port_is_found_outside_comments(tmp_path):
    (tmp_path / "engine/src").mkdir(parents=True)
    (tmp_path / "godot/scripts").mkdir(parents=True)
    (tmp_path / "engine/src/a.cpp").write_text('// "TOOLTIP:Only"\nfetch("TOOLTIP:Cost");\n')
    assert inv.port_refs(tmp_path, "TOOLTIP:Cost") == ["engine/src/a.cpp"]
    assert inv.port_refs(tmp_path, "TOOLTIP:Only") == []


@pytest.mark.skipif(not os.environ.get("ROTWK_INSTALL") or not os.environ.get("RW_GAME_DAT"), reason="needs ROTWK_INSTALL and RW_GAME_DAT")
def test_retail_tooltip_sources_are_complete_and_named():
    mount = inv.bigfs.Mount()
    wnd = inv.wnd_tooltips({p: mount.read(p).decode("latin-1") for p in mount.list("", ".wnd")})
    ini = inv.ini_tooltip_fields({p: mount.read(p).decode("latin-1") for p in mount.list("", ".ini")})
    code = inv.classify_code_strings(inv.code_tooltip_strings(inv.game_dat()))
    strings = inv.string_table(mount.read("data\\lotr.str"))
    rows = inv.source_rows(wnd, ini, code, strings, inv.REPO)  # refuses a row that resolves to nothing without a named reason
    by_label = {r[3]: r for r in rows}
    assert by_label["TOOLTIP:StartPosition"][5] == "Starting Position" and by_label["TOOLTIP:StartPositionN"][5] == "Start Position for player %d"
    assert not [r for r in rows if r[6] == "UNRESOLVED"]
    buttons = dict(inv.blocks(mount.read("data\\ini\\commandbutton.ini").decode("latin-1"), "CommandButton"))
    readouts = inv.readout_rows(buttons, strings, inv.dat_noncommand_names(inv.game_dat()), inv.REPO)
    by_button = {r[0]: r for r in readouts}
    assert len(readouts) == 24
    assert by_button["NonCommand_Resources"][2:5] == ["Resources", "CONTROLBAR:ResourcesDescription", by_button["NonCommand_Resources"][4]] and by_button["NonCommand_Resources"][4]
    assert by_button["NonCommand_ResourceMultiplier"][4] and by_button["NonCommand_CommandPoints"][4] == "Current population/unit population cap"
    assert len(wnd) == 8 and sorted(g["value"] for g in wnd if g["field"] == "TOOLTIPTEXT") == ["APT:Null", "Apt:None", "TOOLTIP:CloseExpScreen", "TOOLTIP:ExperienceToLevel", "Tooltip:LoadProgress"]
    assert len(ini) == 19
    assert len(code) == 88
    assert "TOOLTIP:StartPosition" in code and "TOOLTIP:StartPositionN" in code


SAMPLE_STRINGS = {"tooltip:cost": "Cost: %d", "controlbar:resources": "Resources", "controlbar:resourcesdescription": "Money", "apt:none": ""}


def test_a_source_row_that_resolves_to_nothing_is_refused():
    wnd = [{"window": "w.wnd", "gadget": "w:A", "field": "TOOLTIPTEXT", "value": "NoColonNoLabel"}]
    with pytest.raises(inv.InventoryError, match="resolve to no text"):
        inv.source_rows(wnd, {}, {}, SAMPLE_STRINGS, Path("."))


def test_a_label_with_an_empty_text_is_refused_unless_it_is_a_placeholder():
    ok = [{"window": "w.wnd", "gadget": "w:A", "field": "TOOLTIPTEXT", "value": "Apt:None"}]
    with pytest.raises(inv.InventoryError, match="labels the string file lacks changed"):
        inv.source_rows(ok, {}, {}, SAMPLE_STRINGS, Path("."))  # the pinned missing-label set is not met by this sample: refused, not silently accepted
    empty = [{"window": "w.wnd", "gadget": "w:A", "field": "TOOLTIPTEXT", "value": "TOOLTIP:Blank"}]
    with pytest.raises(inv.InventoryError, match="resolve to no text"):
        inv.source_rows(empty, {}, {}, dict(SAMPLE_STRINGS, **{"tooltip:blank": " "}), Path("."))


def test_a_new_missing_label_is_refused():
    wnd = [{"window": "w.wnd", "gadget": "w:A", "field": "TOOLTIPTEXT", "value": "TOOLTIP:Brand"}]
    with pytest.raises(inv.InventoryError, match="new .*tooltip:brand"):
        inv.source_rows(wnd, {}, {}, SAMPLE_STRINGS, Path("."))


def test_a_resolved_gadget_row_carries_its_text():
    wnd = [{"window": "w.wnd", "gadget": "w:A", "field": "TOOLTIPTEXT", "value": "TOOLTIP:Cost"}]
    # the sample lacks the pinned missing labels, so only the resolution itself is checked through the helper's refusal text
    with pytest.raises(inv.InventoryError, match="gone"):
        inv.source_rows(wnd, {}, {}, SAMPLE_STRINGS, Path("."))
    assert inv.lookup(SAMPLE_STRINGS, "TOOLTIP:Cost") == ("TOOLTIP:Cost", "Cost: %d")
    assert inv.lookup(SAMPLE_STRINGS, "Cost") == ("TOOLTIP:Cost", "Cost: %d")  # an Apt path label has the TOOLTIP: prefix added
    assert inv.lookup(SAMPLE_STRINGS, "Nothing") is None


def readout_buttons():
    return {"NonCommand_Resources": {"TextLabel": ["CONTROLBAR:Resources"], "DescriptLabel": ["CONTROLBAR:ResourcesDescription"]}}


def test_readouts_list_the_noncommand_buttons_with_their_texts(tmp_path):
    rows = inv.readout_rows(readout_buttons(), SAMPLE_STRINGS, {"NonCommand_Resources", "NonCommand_"}, tmp_path)
    assert rows == [["NonCommand_Resources", "CONTROLBAR:Resources", "Resources", "CONTROLBAR:ResourcesDescription", "Money", "yes", "not hovered", "S-4612"]]


def test_readouts_refuse_a_missing_label_no_button_and_a_button_the_executable_names_but_the_data_lacks(tmp_path):
    with pytest.raises(inv.InventoryError, match="no NonCommand_ button"):
        inv.readout_rows({"Command_X": {}}, SAMPLE_STRINGS, set(), tmp_path)
    with pytest.raises(inv.InventoryError, match="resolves to nothing"):
        inv.readout_rows(readout_buttons(), {"controlbar:resources": "Resources"}, set(), tmp_path)
    with pytest.raises(inv.InventoryError, match="NonCommand_Ghost"):
        inv.readout_rows(readout_buttons(), SAMPLE_STRINGS, {"NonCommand_Ghost"}, tmp_path)
