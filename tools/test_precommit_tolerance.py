"""Lane LINT-1: the commit gate flags tolerance assertions among the lines a commit adds to C++ tests (engine/tests/) and GDScript tests
(godot/tests/) unless a `// tolerance: <reason>` (`# tolerance: <reason>`) comment on the line or the line above justifies them."""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import precommit  # noqa: E402

ENV = dict(os.environ, GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1", GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.com",
           GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@example.com")
CPP = "engine/tests/test_x.cpp"
GD = "godot/tests/x_test.gd"


def run(repo: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True, env=ENV)


def make_repo(tmp_path: Path, committed: dict[str, str]) -> Path:
    repo = tmp_path / "repo"
    (repo / "tools").mkdir(parents=True)
    shutil.copy(HERE / "precommit.py", repo / "tools" / "precommit.py")
    (repo / "tools" / "precommit_containers.txt").write_text("")
    run(tmp_path, "init", "-q", str(repo))
    for name, text in committed.items():
        (repo / name).parent.mkdir(parents=True, exist_ok=True)
        (repo / name).write_text(text)
        run(repo, "add", name)
    if committed:
        run(repo, "commit", "-q", "-m", "base")
    return repo


def gate(repo: Path, staged: dict[str, str]) -> subprocess.CompletedProcess:
    for name, text in staged.items():
        (repo / name).parent.mkdir(parents=True, exist_ok=True)
        (repo / name).write_text(text)
        run(repo, "add", name)
    return subprocess.run([sys.executable, str(repo / "tools" / "precommit.py")], capture_output=True, text=True, env=ENV)


def new_file(tmp_path: Path, name: str, text: str) -> subprocess.CompletedProcess:
    return gate(make_repo(tmp_path, {}), {name: text})


@pytest.mark.parametrize("line,kind", [
    ("    CHECK_GE(count, 3);", "CHECK_GE"),
    ("    CHECK_LE(count, 3);", "CHECK_LE"),
    ("    CHECK_GT(count, 3);", "CHECK_GT"),
    ("    CHECK_LT(count, 3);", "CHECK_LT"),
    ("    REQUIRE_GE(count, 3);", "REQUIRE_GE"),
    ("    CHECK(count >= 3);", "CHECK"),
    ("    CHECK(unfired <= 1);", "CHECK"),
    ("    CHECK(count > 2);", "CHECK"),
    ("    CHECK(a < b);", "CHECK"),
    ("    REQUIRE(sold != 0);", "REQUIRE"),
    ("    CHECK(health == doctest::Approx(10.0f));", "doctest::Approx"),
])
def test_each_cpp_tolerance_form_is_flagged(tmp_path, line, kind):
    result = new_file(tmp_path, CPP, f'TEST_CASE("t") {{\n{line}\n}}\n')
    assert result.returncode == 1, result.stdout
    assert f"{CPP}:2:" in result.stdout and kind in result.stdout
    assert "assert the exact value, or justify the bound with `// tolerance: <reason>`" in result.stdout


@pytest.mark.parametrize("line", [
    "    CHECK(count == 3);",
    "    CHECK_EQ(count, 3);",
    "    CHECK(owner->team == team);",
    "    CHECK(std::vector<int>{1, 2} == v);",
    '    CHECK(name == "a<=b");',
    "    CHECK(sold == 0); // was >= 1 once",
    "    REQUIRE(!list.empty());",
])
def test_exact_checks_pass(tmp_path, line):
    result = new_file(tmp_path, CPP, f'TEST_CASE("t") {{\n{line}\n}}\n')
    assert result.returncode == 0, result.stdout


def test_a_justification_on_the_same_line_or_above_passes(tmp_path):
    text = ('TEST_CASE("t") {\n'
            "    CHECK_LE(frames, 40); // tolerance: retail's ramp finishes within 40 logic frames (RW 0x5D1A40)\n"
            "    // tolerance: measured frame time, noisy\n"
            "    CHECK(ms < 16);\n"
            "}\n")
    result = new_file(tmp_path, CPP, text)
    assert result.returncode == 0, result.stdout


def test_an_empty_reason_does_not_justify(tmp_path):
    result = new_file(tmp_path, CPP, 'TEST_CASE("t") {\n    CHECK_LE(frames, 40); // tolerance:   \n}\n')
    assert result.returncode == 1 and f"{CPP}:2:" in result.stdout


def test_an_existing_line_is_not_flagged_but_a_new_one_beside_it_is(tmp_path):
    old = 'TEST_CASE("t") {\n    CHECK_GE(count, 3);\n}\n'
    repo = make_repo(tmp_path, {CPP: old})
    assert gate(repo, {CPP: old.replace("}\n", "    CHECK(count == 4);\n}\n")}).returncode == 0
    # the committed line is untouched; only the added one is judged
    result = gate(repo, {CPP: old.replace("}\n", "    CHECK(count == 4);\n    CHECK_LT(count, 9);\n}\n")})
    assert result.returncode == 1
    assert f"{CPP}:4:" in result.stdout and f"{CPP}:2:" not in result.stdout


def test_a_merge_judges_only_the_lines_new_to_both_parents(tmp_path):
    # lane PLAY-4 r3: the merged branch's tolerance line was committed there (before the rule, or justified); the merge brings it unflagged, but a
    # line the merge resolution itself adds is judged
    repo = make_repo(tmp_path, {CPP: 'TEST_CASE("t") {\n    CHECK(a == 1);\n}\n'})
    run(repo, "branch", "-M", "main")
    run(repo, "checkout", "-q", "-b", "other")
    (repo / "engine/tests/test_other.cpp").write_text('TEST_CASE("o") {\n    CHECK_GE(count, 3);\n}\n')
    run(repo, "add", "engine/tests/test_other.cpp")
    run(repo, "commit", "-q", "-m", "other")
    run(repo, "checkout", "-q", "main")
    (repo / "engine/tests/test_main.cpp").write_text('TEST_CASE("m") {\n    CHECK(b == 2);\n}\n')
    run(repo, "add", "engine/tests/test_main.cpp")
    run(repo, "commit", "-q", "-m", "main")
    run(repo, "merge", "-q", "--no-ff", "--no-commit", "other")
    assert gate(repo, {}).returncode == 0
    result = gate(repo, {CPP: 'TEST_CASE("t") {\n    CHECK(a == 1);\n    CHECK_LT(a, 9);\n}\n'})
    assert result.returncode == 1 and f"{CPP}:3:" in result.stdout and "test_other.cpp" not in result.stdout


def test_a_removed_tolerance_line_passes(tmp_path):
    repo = make_repo(tmp_path, {CPP: 'TEST_CASE("t") {\n    CHECK_GE(count, 3);\n    CHECK(a == 1);\n}\n'})
    result = gate(repo, {CPP: 'TEST_CASE("t") {\n    CHECK(a == 1);\n}\n'})
    assert result.returncode == 0, result.stdout


def test_a_pure_rename_of_a_tolerance_line_passes(tmp_path):
    repo = make_repo(tmp_path, {CPP: 'TEST_CASE("t") {\n    CHECK_GE(count, 3);\n    CHECK(a == 1);\n}\n'})
    run(repo, "mv", CPP, "engine/tests/test_y.cpp")
    result = subprocess.run([sys.executable, str(repo / "tools" / "precommit.py")], capture_output=True, text=True, env=ENV)
    assert result.returncode == 0, result.stdout


def test_files_outside_the_test_roots_are_not_judged(tmp_path):
    assert new_file(tmp_path, "engine/src/x.cpp", "bool f(int a) { return a >= 3; }\nvoid g() { CHECK_GE(a, 1); }\n").returncode == 0


@pytest.mark.parametrize("line", ["assert(count >= 3)", "assert(count <= 3, \"msg\")", "assert(sold != 0)", "assert(a > b)", "assert(a < b)"])
def test_gdscript_tolerance_forms_are_flagged(tmp_path, line):
    result = new_file(tmp_path, GD, f"extends SceneTree\nfunc _init():\n\t{line}\n")
    assert result.returncode == 1, result.stdout
    assert f"{GD}:3:" in result.stdout and "# tolerance: <reason>" in result.stdout


def test_gdscript_exact_and_justified_pass(tmp_path):
    text = ("extends SceneTree\nfunc _init():\n\tassert(count == 3)\n\tassert(name == \"a>=b\")\n"
            "\tassert(ms < 16) # tolerance: wall-clock measurement\n"
            "\t# tolerance: retail bound\n\tassert(frames <= 40)\n")
    result = new_file(tmp_path, GD, text)
    assert result.returncode == 0, result.stdout


def test_the_unit_function_reports_line_numbers():
    text = "a\nCHECK_GE(x, 1);\nCHECK(y == 2);\nCHECK(z > 0);\n"
    problems = precommit.tolerance_problems(CPP, text, {2, 3, 4})
    assert [p.split(": ")[0] for p in problems] == [f"{CPP}:2", f"{CPP}:4"]


def test_a_merge_answers_only_for_the_lines_neither_parent_has(tmp_path):
    # lane ONLINE-3: merging the archive into a lane brings the archive's lines (other lanes', judged when they were committed there); only a line the
    # merge itself adds (a conflict resolution) is the merge commit's
    repo = make_repo(tmp_path, {CPP: 'TEST_CASE("t") {\n    CHECK(a == 1);\n}\n'})
    run(repo, "checkout", "-q", "-b", "other")
    (repo / CPP).write_text('TEST_CASE("t") {\n    CHECK(count >= 3);\n    CHECK(a == 1);\n}\n')
    run(repo, "commit", "-q", "-am", "theirs (before the gate)")
    run(repo, "checkout", "-q", "-")
    (repo / "engine/tests/test_y.cpp").write_text("// mine\n")
    run(repo, "add", "engine/tests/test_y.cpp")
    run(repo, "commit", "-q", "-m", "mine")
    run(repo, "merge", "-q", "--no-commit", "other")
    result = gate(repo, {})
    assert result.returncode == 0, result.stdout
    # a line the merge adds itself is judged
    result = gate(repo, {CPP: 'TEST_CASE("t") {\n    CHECK(count >= 3);\n    CHECK(a == 1);\n    CHECK(b > 0);\n}\n'})
    assert result.returncode == 1, result.stdout
    assert f"{CPP}:4:" in result.stdout and f"{CPP}:2:" not in result.stdout


def test_a_merge_judges_only_the_lines_new_against_both_parents(tmp_path):
    base = 'TEST_CASE("t") {\n    CHECK(a == 1);\n}\n'
    repo = make_repo(tmp_path, {CPP: base})
    run(repo, "checkout", "-q", "-b", "side")
    # the merged branch's tolerance line was that branch's commit (the gate judged it there, or it predates the rule)
    (repo / CPP).write_text(base.replace("}\n", "    CHECK_GE(count, 3);\n}\n"))
    (repo / "engine/tests/test_side.cpp").write_text('TEST_CASE("s") {\n    CHECK_LT(n, 9);\n}\n')
    run(repo, "add", CPP, "engine/tests/test_side.cpp")
    run(repo, "commit", "-q", "-m", "side")
    run(repo, "checkout", "-q", "-")
    (repo / "engine/tests/test_main.cpp").write_text('TEST_CASE("m") {\n    CHECK(b == 2);\n}\n')
    run(repo, "add", "engine/tests/test_main.cpp")
    run(repo, "commit", "-q", "-m", "main")
    run(repo, "merge", "-q", "--no-ff", "--no-commit", "side")
    assert gate(repo, {}).returncode == 0
    # a line the merge itself writes (a conflict resolution) is new against both parents and is judged
    result = gate(repo, {CPP: base.replace("}\n", "    CHECK_GE(count, 3);\n    CHECK_GT(m, 1);\n}\n")})
    assert result.returncode == 1
    assert f"{CPP}:4:" in result.stdout and f"{CPP}:3:" not in result.stdout and "test_side.cpp" not in result.stdout
