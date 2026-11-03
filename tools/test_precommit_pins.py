"""The commit gate's check 7: a staged change to a golden hash pin (engine/tests/data/**/*.hashes) needs a `// pin: <reason>` comment among the
lines the same commit adds to a staged C++ file under engine/tests/. A comment already in the tree does not count."""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import precommit  # noqa: E402

ENV = dict(os.environ, GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1", GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@example.com",
           GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@example.com")
PIN = "engine/tests/data/perf1/battle.hashes"
CPP = "engine/tests/test_perf.cpp"
OLD_PIN = "frame 100 0123456789abcdef\n"
NEW_PIN = "frame 100 fedcba9876543210\n"
SOURCE = 'TEST_CASE("perf") {\n    CHECK(frame == 100);\n}\n'
REASON = "    // pin: the loader now keeps the second weapon slot\n"


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


def test_a_changed_pin_with_no_reason_fails(tmp_path):
    repo = make_repo(tmp_path, {PIN: OLD_PIN, CPP: SOURCE})
    result = gate(repo, {PIN: NEW_PIN})
    assert result.returncode == 1, result.stdout
    assert f"{PIN}: golden hash pin changed without a `// pin: <reason>` comment added to a staged C++ test under engine/tests/" in result.stdout
    assert "add `// pin: <reason>`" in result.stdout


def test_a_changed_pin_with_a_reason_added_passes(tmp_path):
    repo = make_repo(tmp_path, {PIN: OLD_PIN, CPP: SOURCE})
    result = gate(repo, {PIN: NEW_PIN, CPP: SOURCE.replace("}\n", REASON + "}\n")})
    assert result.returncode == 0, result.stdout
    assert result.stdout == "commit gate ok (2 changed files checked)\n"


def test_a_new_pin_file_with_a_reason_passes(tmp_path):
    repo = make_repo(tmp_path, {CPP: SOURCE})
    result = gate(repo, {PIN: NEW_PIN, CPP: SOURCE.replace("}\n", REASON + "}\n")})
    assert result.returncode == 0, result.stdout


def test_an_empty_reason_does_not_count(tmp_path):
    repo = make_repo(tmp_path, {PIN: OLD_PIN, CPP: SOURCE})
    result = gate(repo, {PIN: NEW_PIN, CPP: SOURCE.replace("}\n", "    // pin:   \n}\n")})
    assert result.returncode == 1, result.stdout
    assert f"{PIN}: golden hash pin changed without a `// pin: <reason>` comment" in result.stdout


def test_a_comment_already_in_the_tree_does_not_count(tmp_path):
    repo = make_repo(tmp_path, {PIN: OLD_PIN, CPP: SOURCE.replace("}\n", REASON + "}\n")})
    result = gate(repo, {PIN: NEW_PIN})
    assert result.returncode == 1, result.stdout
    assert f"{PIN}: golden hash pin changed without a `// pin: <reason>` comment" in result.stdout
    # touching the test file elsewhere does not make the old comment count either
    result = gate(repo, {CPP: SOURCE.replace("}\n", REASON + "    CHECK(frame == 101);\n}\n")})
    assert result.returncode == 1, result.stdout


def test_a_data_file_that_is_not_a_hashes_file_passes(tmp_path):
    repo = make_repo(tmp_path, {})
    result = gate(repo, {"engine/tests/data/perf1/battle.tsv": NEW_PIN})
    assert result.returncode == 0, result.stdout


def test_a_hashes_file_outside_engine_tests_data_passes(tmp_path):
    repo = make_repo(tmp_path, {})
    result = gate(repo, {"tools/perf/battle.hashes": NEW_PIN, "engine/tests/perf/battle.hashes": NEW_PIN})
    assert result.returncode == 0, result.stdout


def test_a_removed_pin_line_passes(tmp_path):
    repo = make_repo(tmp_path, {PIN: OLD_PIN + "frame 200 aaaaaaaaaaaaaaaa\n"})
    result = gate(repo, {PIN: OLD_PIN})
    assert result.returncode == 0, result.stdout


def test_a_merge_taking_one_parents_justified_pin_passes(tmp_path):
    repo = make_repo(tmp_path, {PIN: OLD_PIN, CPP: SOURCE})
    run(repo, "branch", "-M", "main")
    run(repo, "checkout", "-q", "-b", "side")
    (repo / PIN).write_text(NEW_PIN)
    (repo / CPP).write_text(SOURCE.replace("}\n", REASON + "}\n"))
    run(repo, "add", PIN, CPP)
    run(repo, "commit", "-q", "-m", "side re-measures the pin, with its reason")
    run(repo, "checkout", "-q", "main")
    (repo / "engine/tests/test_main.cpp").write_text('TEST_CASE("m") {\n    CHECK(b == 2);\n}\n')
    run(repo, "add", "engine/tests/test_main.cpp")
    run(repo, "commit", "-q", "-m", "main")
    run(repo, "merge", "-q", "--no-ff", "--no-commit", "side")
    result = gate(repo, {})
    assert result.returncode == 0, result.stdout
    # a pin value the merge itself writes is new against both parents and needs its own reason
    result = gate(repo, {PIN: "frame 100 1111111111111111\n"})
    assert result.returncode == 1, result.stdout
    assert f"{PIN}: golden hash pin changed without" in result.stdout


def test_a_comment_outside_a_staged_cpp_test_does_not_count(tmp_path):
    repo = make_repo(tmp_path, {PIN: OLD_PIN, CPP: SOURCE})
    # a C++ test that is not staged
    (repo / CPP).write_text(SOURCE.replace("}\n", REASON + "}\n"))
    result = gate(repo, {PIN: NEW_PIN})
    assert result.returncode == 1, result.stdout
    assert f"{PIN}: golden hash pin changed without" in result.stdout
    # staged, but not C++, and C++ outside engine/tests/
    result = gate(repo, {"engine/tests/notes.md": "// pin: the loader changed\n", "engine/src/x.cpp": "// pin: the loader changed\n",
                         "docs/pin.txt": "// pin: the loader changed\n"})
    assert result.returncode == 1, result.stdout
    assert f"{PIN}: golden hash pin changed without" in result.stdout


def test_a_pin_path_with_a_space_is_still_judged(tmp_path):
    spaced = "engine/tests/data/perf1/bat tle.hashes"
    repo = make_repo(tmp_path, {spaced: OLD_PIN, CPP: SOURCE})
    result = gate(repo, {spaced: NEW_PIN})
    assert result.returncode == 1, result.stdout
    assert f"{spaced}: golden hash pin changed without" in result.stdout


def test_a_reason_in_a_test_path_with_a_space_counts(tmp_path):
    spaced = "engine/tests/test perf.cpp"
    repo = make_repo(tmp_path, {PIN: OLD_PIN, spaced: SOURCE})
    result = gate(repo, {PIN: NEW_PIN, spaced: SOURCE.replace("}\n", REASON + "}\n")})
    assert result.returncode == 0, result.stdout


def test_a_test_path_with_a_space_keeps_its_tolerance_check(tmp_path):
    spaced = "engine/tests/test perf.cpp"
    repo = make_repo(tmp_path, {spaced: SOURCE})
    result = gate(repo, {spaced: SOURCE.replace("}\n", "    CHECK(frame > 0);\n}\n")})
    assert result.returncode == 1, result.stdout
    assert f"{spaced}:3: tolerance assertion" in result.stdout


def test_an_added_line_that_looks_like_a_diff_header_does_not_end_the_file(tmp_path):
    body = "".join(f"int line{n};\n" for n in range(20))
    repo = make_repo(tmp_path, {PIN: OLD_PIN, CPP: body})
    edited = "++ counter;\n" + body + REASON
    result = gate(repo, {PIN: NEW_PIN, CPP: edited})
    assert result.returncode == 0, result.stdout


def test_git_quoted_paths_are_decoded():
    assert precommit.diff_new_path("+++ b/engine/tests/plain.cpp") == "engine/tests/plain.cpp"
    assert precommit.diff_new_path("+++ b/engine/tests/a b.cpp\t") == "engine/tests/a b.cpp"
    assert precommit.diff_new_path('+++ "b/engine/tests/a\\tb\\"c\\\\d\\001.cpp"') == 'engine/tests/a\tb"c\\d\x01.cpp'
    assert precommit.diff_new_path('+++ "b/engine/tests/a\\tb c.cpp"\t') == "engine/tests/a\tb c.cpp"
    assert precommit.diff_new_path("+++ /dev/null") == ""


def test_git_octal_escapes_are_utf8_bytes():
    assert precommit.diff_new_path('+++ "b/engine/tests/\\303\\251.cpp"') == "engine/tests/é.cpp"
    # with core.quotepath=false a name keeps its non-ASCII text and only the other special characters are escaped
    assert precommit.diff_new_path('+++ "b/engine/tests/é\\t兵.cpp"') == "engine/tests/é\t兵.cpp"


def test_a_reason_in_a_non_ascii_test_path_counts(tmp_path):
    named = "engine/tests/tést.cpp"
    repo = make_repo(tmp_path, {PIN: OLD_PIN, named: SOURCE})
    result = gate(repo, {PIN: NEW_PIN, named: SOURCE.replace("}\n", REASON + "}\n")})
    assert result.returncode == 0, result.stdout


def test_a_pin_comment_inside_a_multi_line_raw_string_is_not_a_reason(tmp_path):
    for opening in ('R"(', 'R"x(', 'u8R"x(', 'LR"(', 'uR"(', 'UR"('):
        closing = ')x"' if "x" in opening else ')"'
        block = f"    const char* s = {opening}\n// pin: the loader changed\n{closing};\n"
        repo_root = tmp_path / str(abs(hash(opening)))
        repo_root.mkdir()
        repo = make_repo(repo_root, {PIN: OLD_PIN, CPP: SOURCE})
        result = gate(repo, {PIN: NEW_PIN, CPP: SOURCE.replace("}\n", block + "}\n")})
        assert result.returncode == 1, (opening, result.stdout)
        assert f"{PIN}: golden hash pin changed without" in result.stdout


def test_a_raw_string_opened_on_an_older_line_still_hides_an_added_pin_comment(tmp_path):
    held = 'const char* s = R"(\nfirst\n)";\n' + SOURCE
    repo = make_repo(tmp_path, {PIN: OLD_PIN, CPP: held})
    result = gate(repo, {PIN: NEW_PIN, CPP: held.replace("first\n", "first\n// pin: the loader changed\n")})
    assert result.returncode == 1, result.stdout
    assert f"{PIN}: golden hash pin changed without" in result.stdout


def test_a_reason_on_the_line_a_multi_line_raw_string_closes_counts(tmp_path):
    repo = make_repo(tmp_path, {PIN: OLD_PIN, CPP: SOURCE})
    block = '    const char* s = R"(\ntext\n)"; // pin: the loader now keeps the second slot\n'
    result = gate(repo, {PIN: NEW_PIN, CPP: SOURCE.replace("}\n", block + "}\n")})
    assert result.returncode == 0, result.stdout


def test_a_reason_after_raw_string_lookalikes_counts(tmp_path):
    for lookalike in ('// the format is R"( and so on\n', '/* R"( */\n', 'const char* s = "R\\"(";\n'):
        repo_root = tmp_path / str(abs(hash(lookalike)))
        repo_root.mkdir()
        repo = make_repo(repo_root, {PIN: OLD_PIN, CPP: SOURCE})
        # the later `)"` would close a raw string that the lookalike wrongly opened, hiding the reason
        result = gate(repo, {PIN: NEW_PIN, CPP: SOURCE.replace("}\n", lookalike + REASON + 'const char* t = ")";\n}\n')})
        assert result.returncode == 0, (lookalike, result.stdout)


def test_an_identifier_ending_in_r_does_not_open_a_raw_string(tmp_path):
    repo = make_repo(tmp_path, {PIN: OLD_PIN, CPP: SOURCE})
    block = 'const char* s = FOOR"(";\n' + REASON + 'const char* t = ")";\n'
    result = gate(repo, {PIN: NEW_PIN, CPP: SOURCE.replace("}\n", block + "}\n")})
    assert result.returncode == 0, result.stdout


def test_a_pin_comment_inside_a_block_comment_is_not_a_reason(tmp_path):
    repo = make_repo(tmp_path, {PIN: OLD_PIN, CPP: SOURCE})
    result = gate(repo, {PIN: NEW_PIN, CPP: SOURCE.replace("}\n", "/*\n// pin: the loader changed\n*/\n}\n")})
    assert result.returncode == 1, result.stdout
    assert f"{PIN}: golden hash pin changed without" in result.stdout


def test_a_reason_after_a_digit_separator_counts(tmp_path):
    repo = make_repo(tmp_path, {PIN: OLD_PIN, CPP: SOURCE})
    line = "    int n = 1'000; // pin: the loader's second slot moved\n"
    result = gate(repo, {PIN: NEW_PIN, CPP: SOURCE.replace("}\n", line + "}\n")})
    assert result.returncode == 0, result.stdout


def test_a_string_literal_is_not_a_reason(tmp_path):
    for line in ('    const char* s = "// pin: not a comment";\n',
                 '    const char* s = R"(// pin: not a comment)";\n',
                 '    const char* s = R"x(say "hi" // pin: not a comment)x";\n'):
        repo_root = tmp_path / str(abs(hash(line)))
        repo_root.mkdir()
        repo = make_repo(repo_root, {PIN: OLD_PIN, CPP: SOURCE})
        result = gate(repo, {PIN: NEW_PIN, CPP: SOURCE.replace("}\n", line + "}\n")})
        assert result.returncode == 1, (line, result.stdout)
        assert f"{PIN}: golden hash pin changed without" in result.stdout


def test_a_reason_after_code_and_a_string_counts(tmp_path):
    repo = make_repo(tmp_path, {PIN: OLD_PIN, CPP: SOURCE})
    line = '    const char* s = "http://x"; // pin: the loader now keeps the second slot\n'
    result = gate(repo, {PIN: NEW_PIN, CPP: SOURCE.replace("}\n", line + "}\n")})
    assert result.returncode == 0, result.stdout
