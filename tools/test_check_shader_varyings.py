"""The varying budget check: Godot's own count (base index + slots), Intel's limit 29, and the commit gate running it."""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import check_shader_varyings as csv  # noqa: E402


def spatial(n: int, extra: str = "") -> str:
    lines = [f"varying vec4 v{i};" for i in range(n)]
    return "shader_type spatial;\n" + "\n".join(lines) + "\n" + extra + "\nvoid fragment() {}\n"


def check(files: dict[str, str]) -> list[str]:
    shaders = [name for name in files if name.endswith(".gdshader")]
    return csv.problems(shaders, lambda rel: files[rel])


def test_spatial_budget_is_exactly_fourteen_user_varyings():
    assert csv.shader_usage("a.gdshader", lambda _: spatial(14))[1] == 29
    assert check({"a.gdshader": spatial(14)}) == []
    found = check({"a.gdshader": spatial(15)})
    assert len(found) == 1 and "30 varyings" in found[0]


def test_matrices_and_arrays_take_their_slots():
    code = spatial(0, "varying mat3 m;\nvarying flat vec2 a[4];\nvarying highp float f, g;")
    assert csv.shader_usage("a.gdshader", lambda _: code)[1] == 15 + 3 + 4 + 2


def test_includes_count_once_and_res_paths_are_not_comments():
    files = {
        "shaders/a.gdshader": spatial(13, '#include "res://shaders/inc.gdshaderinc"\n#include "inc.gdshaderinc"'),
        "shaders/inc.gdshaderinc": "varying vec3 v_inc;\nvarying vec2 v_two;\n",
    }
    found = check(files)
    assert len(found) == 1 and "30 varyings" in found[0] and "v_inc" in found[0]


def test_declarations_sharing_a_line_all_count():
    one_line = "shader_type spatial;\n" + " ".join(f"varying vec4 v{i};" for i in range(15)) + "\nvoid fragment() {}\n"
    assert csv.shader_usage("a.gdshader", lambda _: one_line)[1] == 30
    found = check({"a.gdshader": one_line})
    assert len(found) == 1 and "30 varyings" in found[0]
    assert csv.shader_usage("a.gdshader", lambda _: spatial(0, "float my_varying_x;"))[1] == 15


def test_commented_varyings_do_not_count():
    code = spatial(14, "// varying vec4 gone;\n/* varying vec4 also_gone;\n*/")
    assert check({"a.gdshader": code}) == []


def test_canvas_item_base_is_nine():
    code = "shader_type canvas_item;\n" + "\n".join(f"varying vec2 v{i};" for i in range(21))
    found = check({"c.gdshader": code})
    assert len(found) == 1 and "30 varyings" in found[0]


def test_missing_include_is_a_problem_not_a_pass():
    found = check({"a.gdshader": spatial(1, '#include "res://nope.gdshaderinc"')})
    assert len(found) == 1 and "cannot count" in found[0]


def test_the_project_shaders_fit():
    root = HERE.parent / "godot"
    shaders = [p.relative_to(root).as_posix() for p in root.rglob("*.gdshader") if ".godot" not in p.relative_to(root).parts]
    assert shaders
    assert csv.problems(shaders, lambda rel: (root / rel).read_text(encoding="utf-8")) == []


ENV = dict(os.environ, GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1", GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.com",
           GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@example.com")


def git(repo: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True, env=ENV)


@pytest.fixture
def repo(tmp_path):
    repo = tmp_path / "repo"
    (repo / "tools").mkdir(parents=True)
    for name in ("precommit.py", "check_shader_varyings.py"):
        shutil.copy(HERE / name, repo / "tools" / name)
    git(tmp_path, "init", "-q", str(repo))
    return repo


def gate(repo: Path, files: dict[str, str]) -> subprocess.CompletedProcess:
    for name, text in files.items():
        path = repo / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        git(repo, "add", "--", name)
    return subprocess.run([sys.executable, str(repo / "tools" / "precommit.py")], cwd=repo, capture_output=True, text=True, env=ENV)


def test_gate_refuses_a_staged_include_that_pushes_a_shader_over(repo):
    ok = gate(repo, {"godot/shaders/t.gdshader": spatial(13, '#include "res://shaders/c.gdshaderinc"'),
                     "godot/shaders/c.gdshaderinc": "varying vec2 one;\n"})
    assert ok.returncode == 0, ok.stdout
    git(repo, "commit", "-q", "--no-verify", "-m", "base")
    bad = gate(repo, {"godot/shaders/c.gdshaderinc": "varying vec2 one;\nvarying vec2 two;\n"})
    assert bad.returncode == 1
    assert "godot/shaders/t.gdshader: 30 varyings" in bad.stdout


def test_gdscript_inline_shader_over_budget_is_a_problem():
    code = 'extends Node\nvar shader := Shader.new()\nvar code := """' + spatial(15) + '"""\nshader.code = code\n'
    found = csv.inline_problems({"scripts/a.gd": code}, lambda _: "")
    assert len(found) == 1
    assert "scripts/a.gd:3 (inline shader): 30 varyings" in found[0]


def test_gdscript_inline_shader_at_the_budget_passes():
    code = 'var code := """' + spatial(14) + '"""\n'
    assert csv.inline_problems({"scripts/a.gd": code}, lambda _: "") == []


def test_gdscript_comment_and_ordinary_string_mentioning_shader_type_are_not_shaders():
    code = 'extends Node\n# shader_type spatial;\nvar text := "shader_type spatial;"\n'
    assert csv.inline_problems({"scripts/a.gd": code}, lambda _: "") == []


def test_gdscript_inline_shader_includes_count():
    code = 'var code := """shader_type spatial;\n' + "\n".join(f"varying vec4 v{i};" for i in range(13)) + \
        '\n#include "res://shaders/inc.gdshaderinc"\n"""\n'
    found = csv.inline_problems({"scripts/a.gd": code}, lambda rel: "varying vec2 one;\nvarying vec2 two;\n" if rel == "shaders/inc.gdshaderinc" else "")
    assert len(found) == 1
    assert "scripts/a.gd:1 (inline shader): 30 varyings" in found[0]


def test_cli_reports_projects_without_inline_shaders_or_without_shader_files(tmp_path, capsys):
    (tmp_path / "files").mkdir()
    (tmp_path / "files" / "a.gdshader").write_text(spatial(2))
    assert csv.main(["check", str(tmp_path / "files")]) == 0
    assert "1 shaders and 0 inline shaders, the most is 17/29 (a.gdshader)" in capsys.readouterr().out
    (tmp_path / "inline").mkdir()
    (tmp_path / "inline" / "a.gd").write_text('var code := """shader_type canvas_item;\nvarying vec4 a;\n"""\n')
    assert csv.main(["check", str(tmp_path / "inline")]) == 0
    assert "0 shaders and 1 inline shaders, the most is 10/29 (a.gd:1 (inline shader))" in capsys.readouterr().out
    (tmp_path / "none").mkdir()
    assert csv.main(["check", str(tmp_path / "none")]) == 0
    assert "0 shaders and 0 inline shaders, the most is 0/29 (no shaders)" in capsys.readouterr().out


def test_gate_refuses_a_staged_gdscript_inline_shader_over_budget(repo):
    ok = gate(repo, {"godot/scripts/a.gd": 'var code := """shader_type canvas_item;\nvoid fragment() {}\n"""\n'})
    assert ok.returncode == 0, ok.stdout
    git(repo, "commit", "-q", "--no-verify", "-m", "base")
    bad = gate(repo, {"godot/scripts/a.gd": 'var code := """' + spatial(15) + '"""\n'})
    assert bad.returncode == 1
    assert "godot/scripts/a.gd" in bad.stdout and "30 varyings" in bad.stdout
