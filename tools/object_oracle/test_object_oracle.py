"""Tests for the independent object-model oracle (tools/object_oracle/object_oracle.py).

Synthetic cases pin the oracle's own parse and inheritance rules; the retail case (ROTWK_INSTALL and
BFME2_INSTALL set) regenerates the committed golden byte for byte. The oracle shares no code with the
engine, so these tests cite the same spec sections and RotWK addresses the engine's tests do."""
from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import object_oracle as O  # noqa: E402

GOLDEN = HERE.parent.parent / "engine" / "tests" / "data" / "object_model"


def items_of(text):
    return O.lex([("t.ini", n, l) for n, l in enumerate(text.split("\n"), 1)])


def objects_of(text):
    items = items_of(text)
    priors = O.Priors()
    priors.learn(items)
    depths, openers = O.parse_structure(items, priors)
    problems = []
    defs = O.collect_objects(items, depths, openers, problems)
    assert problems == []
    return defs


def templates_of(text, errors=None):
    reg = O.Registry(O.REGISTRY_JSON)
    errors = [] if errors is None else errors
    return O.build_templates(objects_of(text), reg, errors), errors


DEFAULT = """Object DefaultThingTemplate
  Body = InactiveBody ModuleTag_DefaultBody
  End
  Behavior = DestroyDie ModuleTag_DefaultDie
  End
  InheritableModule
    Behavior = KeepObjectDie ModuleTag_DefaultKeep
    End
  End
  Draw = W3DDefaultDraw ModuleTag_DefaultDraw
  End
End
"""


def flat(tpls, name):
    return next(t for t in tpls if t.name == name).flat()


CLEAN = (
    "Object C{n}\n"
    "  Draw = W3DScriptedModelDraw ModuleTag_D\n"
    "    DefaultModelConditionState\n"
    "      Model = X\n"
    "    End\n"
    "    AnimationState = MOVING\n"
    "      Animation = Run\n"
    "        AnimationName = N\n"
    "      End\n"
    "    End\n"
    "  End\n"
    "  Behavior = DestroyDie ModuleTag_Die\n"
    "  End\n"
    "End\n"
)


def clean_corpus(n=25):
    """the priors are learned from the corpus, so a synthetic case needs enough regular text around it"""
    return "".join(CLEAN.format(n=i) for i in range(n))


def test_structure_finds_nested_ends_with_inconsistent_indentation():
    defs = objects_of(clean_corpus() +
        "Object A\n"
        "  Draw = W3DScriptedModelDraw ModuleTag_D\n"
        "    DefaultModelConditionState\n"
        "      Model = X\n"
        "    End\n"
        "\tAnimationState = MOVING\n"
        "\t\tAnimation = Run\n"
        "\t\t\tAnimationName = N\n"
        "\t\tEnd\n"
        "\t End\n"
        "  End\n"
        "  Behavior = DestroyDie ModuleTag_Die\n"
        "  End\n"
        "End\n")
    sloppy = defs[-1]
    assert sloppy.name == "A"
    assert [(d.kind, d.cls, d.tag) for d in sloppy.ops] == [("draw", "W3DScriptedModelDraw", "ModuleTag_D"), ("behavior", "DestroyDie", "ModuleTag_Die")]


def test_script_bodies_are_skipped_and_comments_stripped():
    defs = objects_of(clean_corpus() +
        "Object A\n"
        "  Draw = W3DScriptedModelDraw ModuleTag_D ; trailing comment\n"
        "    IdleAnimationState\n"
        "      BeginScript\n"
        "        End\n"
        "      EndScript\n"
        "    End\n"
        "  End\n"
        "  // Behavior = DestroyDie ModuleTag_Hidden\n"
        "End\n")
    assert [(d.cls, d.tag) for d in defs[-1].ops] == [("W3DScriptedModelDraw", "ModuleTag_D")]


def test_headers_children_and_reskins_are_found():
    defs = objects_of("Object A\nEnd\nChildObject B A\nEnd\nObjectReskin C A\nEnd\n")
    assert [(d.kind, d.name, d.parent) for d in defs] == [("Object", "A", ""), ("ChildObject", "B", "A"), ("ObjectReskin", "C", "A")]


def test_object_copies_default_and_declarations_clear_by_mask_spec_4_4():
    tpls, errors = templates_of(DEFAULT + "Object Own\n  Body = ActiveBody ModuleTag_Body\n  End\n  Behavior = SlowDeathBehavior ModuleTag_Slow\n  End\nEnd\n")
    assert errors == []
    assert flat(tpls, "Own") == [("KeepObjectDie", "ModuleTag_DefaultKeep"), ("ActiveBody", "ModuleTag_Body"), ("SlowDeathBehavior", "ModuleTag_Slow"), ("W3DDefaultDraw", "ModuleTag_DefaultDraw")]


def test_child_object_replaces_by_tag_and_clears_nothing_spec_4_6():
    tpls, errors = templates_of(
        "Object P\n  Body = ActiveBody ModuleTag_Body\n  End\n  Behavior = DestroyDie ModuleTag_Die\n  End\nEnd\n"
        "ChildObject C P\n  Behavior = SlowDeathBehavior ModuleTag_Die\n  End\n  Behavior = KeepObjectDie ModuleTag_New\n  End\nEnd\n")
    assert errors == []
    assert flat(tpls, "C") == [("ActiveBody", "ModuleTag_Body"), ("SlowDeathBehavior", "ModuleTag_Die"), ("KeepObjectDie", "ModuleTag_New")]


def test_child_ai_and_body_predicates_clear_the_parents_modules():
    tpls, errors = templates_of(
        "Object P\n  Behavior = AIUpdateInterface ModuleTag_AI\n  End\n  Body = ActiveBody ModuleTag_Body\n  End\nEnd\n"
        "ChildObject C P\n  Behavior = HordeAIUpdate ModuleTag_AI2\n  End\n  Body = StructureBody ModuleTag_B2\n  End\nEnd\n")
    assert errors == []
    assert flat(tpls, "C") == [("HordeAIUpdate", "ModuleTag_AI2"), ("StructureBody", "ModuleTag_B2")]


def test_reskin_clears_like_object():
    tpls, errors = templates_of(DEFAULT + "Object Orig\n  Draw = W3DDefaultDraw ModuleTag_Draw\n  End\nEnd\nObjectReskin S Orig\n  Draw = W3DScriptedModelDraw ModuleTag_Skin\n  End\nEnd\n")
    assert errors == []
    assert flat(tpls, "S")[-1] == ("W3DScriptedModelDraw", "ModuleTag_Skin")
    assert ("W3DDefaultDraw", "ModuleTag_Draw") not in flat(tpls, "S")


def test_remove_and_replace_module_spec_4_7():
    tpls, errors = templates_of(
        "Object P\n  Behavior = DestroyDie ModuleTag_A\n  End\n  Draw = W3DDefaultDraw ModuleTag_B\n  End\nEnd\n"
        "ChildObject R P\n  RemoveModule ModuleTag_A\nEnd\n"
        "ChildObject X P\n  ReplaceModule ModuleTag_A\n    Behavior = DestroyDie ModuleTag_A2\n    End\n  End\nEnd\n")
    assert errors == []
    assert flat(tpls, "R") == [("W3DDefaultDraw", "ModuleTag_B")]
    assert flat(tpls, "X") == [("DestroyDie", "ModuleTag_A2"), ("W3DDefaultDraw", "ModuleTag_B")]


def test_errors_are_reported_not_swallowed():
    _, errors = templates_of("Object P\n  Behavior = DestroyDie T\n  End\n  Draw = W3DDefaultDraw T\n  End\nEnd\n")
    assert any("tag T already used" in e for e in errors)
    _, errors = templates_of("Object P\n  Behavior = NoSuchBehavior T\n  End\nEnd\n")
    assert any("unknown module class" in e for e in errors)
    _, errors = templates_of("ChildObject C Missing\nEnd\n")
    assert any("before its original" in e for e in errors)
    _, errors = templates_of("Object P\n  RemoveModule Nope\nEnd\n")
    assert any("not found" in e for e in errors)


def test_golden_rendering_is_dictionary_compressed_and_deterministic():
    tpls, _ = templates_of(DEFAULT + "Object A\nEnd\nObject B\nEnd\n")
    text = O.render_golden(tpls)
    assert text == O.render_golden(tpls)
    lines = text.splitlines()
    assert lines[0].startswith("# OpenBFME object-model oracle golden v1")
    assert [l.split("\t")[0] for l in lines[2:6]] == ["M", "M", "M", "M"]
    assert lines[-1].startswith("T\tB\tObject\t")


def test_the_oracle_imports_no_engine_or_census_code():
    source = (HERE / "object_oracle.py").read_text(encoding="utf-8")
    assert "tools.census" not in source and "rotwk_census" not in source
    imports = [l for l in source.splitlines() if l.startswith(("import ", "from "))]
    allowed = {"annotations", "argparse", "collections", "json", "math", "os", "re", "struct", "sys", "Path"}
    for l in imports:
        names = set(l.replace(",", " ").split()[1:])
        assert names & {"disasm", "extract", "rwimage", "rwtables"} == set(), l
    assert "census_json" in source  # data file only


@pytest.mark.skipif(not (os.environ.get("ROTWK_INSTALL") and os.environ.get("BFME2_INSTALL")), reason="ROTWK_INSTALL / BFME2_INSTALL not set")
def test_retail_run_reproduces_the_committed_golden_with_no_problems():
    templates, objdefs, errors, unresolved = O.run(os.environ["ROTWK_INSTALL"], os.environ["BFME2_INSTALL"], True)
    assert errors == [] and unresolved == []
    assert len(templates) == 4657
    assert O.render_golden(templates) == (GOLDEN / "rotwk201_template_modules.tsv").read_text(encoding="utf-8")
    kinds = collections_counter([t.kind for t in templates])
    assert kinds == {"Object": 3320, "ChildObject": 578, "ObjectReskin": 759}


def collections_counter(values):
    out = {}
    for v in values:
        out[v] = out.get(v, 0) + 1
    return out
