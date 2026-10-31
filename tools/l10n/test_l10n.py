"""Lane L10N-1: the OpenBFME text catalog (docs/L10N.md, tools/l10n/l10n.py). No game files; the runtime test needs Godot 4.7.2."""
from __future__ import annotations

import importlib.util
import json
import os
import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
_spec = importlib.util.spec_from_file_location("l10n_tool", HERE / "l10n.py")
l10n = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(l10n)


@pytest.fixture(scope="module")
def langs():
    return l10n.languages()


def test_catalog_is_valid(langs):
    assert l10n.validate(langs) == []


def test_generated_files_are_current(langs):
    assert l10n.stale_generated(langs) == []


def test_every_id_the_code_uses_exists_with_its_arguments(langs):
    used, problems = l10n.usage(langs)
    assert problems == []
    assert used, "no code uses the catalog"


def test_every_id_is_used(langs):
    used, _ = l10n.usage(langs)
    assert l10n.unused(langs, used) == []


def test_no_new_hard_coded_english_in_the_launcher():
    assert l10n.lint() == []


def test_coverage_lists_what_a_translation_misses(langs):
    report = l10n.coverage(langs)
    assert set(report) == set(langs) - {"en"}
    for code, r in report.items():
        assert r["translated"] + len(r["missing"]) == r["total"]
        assert all(k in l10n.messages(langs["en"]) for k in r["missing"])
        assert langs[code]["@@status"] != "complete" or not r["missing"], f"{code} says complete but misses {r['missing'][:5]}"


def _catalog(tmp_path, files):
    for code, data in files.items():
        (tmp_path / f"{code}.json").write_text(json.dumps(data), encoding="utf-8")
    return l10n.languages(tmp_path)


EN = {"@@locale": "en", "@@name": "English", "@@status": "source"}
DE = {"@@locale": "de", "@@name": "Deutsch", "@@status": "sample"}


@pytest.mark.parametrize("en, de, expect", [
    ({"a.b": "{x} of {y}"}, {"a.b": "{x} von {z}"}, "differ from en"),
    ({"a.b": "{n:d} files"}, {"a.b": "{n} Dateien"}, "differ from en"),
    ({"a.b": "50% {x"}, {}, "stray '{'"),
    ({"a.b": "x } y"}, {}, "stray '}'"),
    ({"a.b": "{x} and {x:d}"}, {}, "two types"),
    ({"a.b": "x"}, {"a.c": "y"}, "not an id of en.json"),
    ({"A.b": "x"}, {}, "bad id"),
    ({"nodot": "x"}, {}, "bad id"),
    ({"a.b": {"one": "1 file", "other": "{count:d} files"}}, {"a.b": {"one": "1 Datei"}}, "exactly the forms"),
    ({"a.b": {"one": "1 file", "few": "x", "other": "{count:d} files"}}, {}, "exactly the forms"),
    ({"a.b": {"one": "1 file", "other": "{count} files"}}, {}, "{count:d}"),
    ({"a.b": "x"}, {"a.b": {"one": "x", "other": "y"}}, "plural here but not"),
    ({"a.b": 3}, {}, "a string or a plural object"),
])
def test_the_validator_refuses(tmp_path, en, de, expect):
    problems = l10n.validate(_catalog(tmp_path, {"en": {**EN, **en}, "de": {**DE, **de}}))
    assert any(expect in p for p in problems), problems


@pytest.mark.parametrize("files, expect", [
    ({"de": DE}, "en.json is missing"),
    ({"en": {**EN, "@@status": "complete"}}, "@@status"),
    ({"en": EN, "de": {**DE, "@@locale": "fr"}}, "@@locale"),
    ({"en": EN, "de": {**DE, "@@name": ""}}, "@@name"),
    ({"en": EN, "xx": {"@@locale": "xx", "@@name": "X", "@@status": "sample"}}, "no plural rule"),
    ({"en": {**EN, "@a.b": {"description": "x"}}}, "notes are"),
])
def test_the_validator_checks_the_metadata(tmp_path, files, expect):
    problems = l10n.validate(_catalog(tmp_path, files))
    assert any(expect in p for p in problems), problems


def test_a_plural_translation_may_leave_out_the_count(tmp_path):
    en = {**EN, "a.b": {"one": "one file", "other": "{count:d} files"}}
    pl = {"@@locale": "pl", "@@name": "Polski", "@@status": "sample",
          "a.b": {"one": "jeden plik", "few": "{count:d} pliki", "many": "{count:d} plików"}}
    assert l10n.validate(_catalog(tmp_path, {"en": en, "pl": pl})) == []


def test_duplicate_ids_are_refused(tmp_path):
    (tmp_path / "en.json").write_text('{"@@locale": "en", "a.b": "x", "a.b": "y"}', encoding="utf-8")
    with pytest.raises(ValueError, match="duplicate"):
        l10n.languages(tmp_path)


def test_usage_checks_ids_and_arguments(tmp_path):
    langs = {"en": {**EN, "a.plain": "x", "a.args": "{x} of {y:d}", "a.count": {"one": "one", "other": "{count:d}"}}}
    (tmp_path / "s.gd").write_text("\n".join([
        'var a := L10n.t("a.plain")',
        'var b := L10n.t("a.args", {"x": f(1, 2), "y": {"k": 1}.size()})',
        'var c := L10n.t("a.args", {"x": 1})',
        'var d := L10n.t("a.count", {"count": n})',
        'var e := L10n.t("a.missing")',
        'var f := L10n.t("a.args")',
        'var g := L10n.t("a.args", args)',
    ]), encoding="utf-8")
    (tmp_path / "c.cpp").write_text('auto a = OpenBFMEText::text("en", "a.plain");\nauto b = OpenBFMEText::text(lang, "a.nope", {});\n',
                                    encoding="utf-8")
    cpp_used, cpp_problems = l10n.usage(langs, gd_roots=(), cpp_roots=(tmp_path,))
    assert sorted(cpp_used) == ["a.nope", "a.plain"] and len(cpp_problems) == 1 and "c.cpp:2: unknown text id 'a.nope'" in cpp_problems[0]
    (tmp_path / "c.cpp").unlink()
    used, problems = l10n.usage(langs, gd_roots=(tmp_path,), cpp_roots=())
    assert sorted(used) == ["a.args", "a.count", "a.missing", "a.plain"]
    assert len(problems) == 3, problems
    assert "s.gd:3: a.args needs the arguments ['x', 'y'], the call passes ['x']" in problems[0]
    assert "s.gd:5: unknown text id 'a.missing'" in problems[1]
    assert "s.gd:6: a.args needs the arguments ['x', 'y'], the call passes none" in problems[2]
    assert l10n.unused({"en": {**EN, "a.plain": "x", "a.other": "y"}}, used) == [
        "tools/l10n/catalog/en.json: a.other is not used by any code (remove it)"]


def test_the_lint_finds_player_text_but_not_logs_keys_or_paths(tmp_path):
    src = tmp_path / "x.gd"
    src.write_text("\n".join([
        'b.text = "Install the game"',                        # 1: flagged
        'Log.line("LAUNCHER could not open %s" % p)',         # logs are English
        'var d := {"Name": 1, "Version": "Not text"}',        # 3: the key is not, the value is
        'if out.begins_with("Error while loading"):',         # matching program output
        'var p := "OpenBFME Launcher.app/Contents/MacOS"',    # a path
        'var f := "apt/Background.big"',
        'label.text = "Settings"',                            # 7: one word, capitalised
        'x = L10n.t("launcher.settings.title")',
        'var s := "%.1f MB/s" % rate',                        # 9: a unit
        '# a comment with "Some words"',
        'push_error("Something went wrong")',
        'if msg == "Some reply":',
        'var k := row["Display Name"]',
        'var e := """Multi',
        'line text"""',                                        # 14: flagged where it starts
        'label.text = row.get("Title", "Install the game")',  # 16: the key is not, the default is
    ]), encoding="utf-8")
    assert [line for line, _ in l10n.lint_file(src)] == [1, 3, 7, 9, 14, 16]


def test_the_allow_list_needs_reasons_and_stays_current(tmp_path, monkeypatch):
    allow = json.loads(l10n.ALLOW.read_text(encoding="utf-8"))
    for rel, entries in allow.items():
        assert rel == "*" or (REPO / rel).is_file(), rel
        assert all(str(reason).strip() for reason in entries.values()), rel


def _godot():
    binary = shutil.which(os.environ.get("GODOT", "godot"))
    if not binary:
        pytest.skip("SKIP LOUDLY: Godot 4.7.2 is required")
    version = subprocess.run([binary, "--version"], capture_output=True, text=True, timeout=15).stdout
    if not version.startswith("4.7.2."):
        pytest.skip(f"SKIP LOUDLY: Godot 4.7.2 required; found {version.strip()}")
    return binary


def _env(tmp_path):
    env = dict(os.environ, HOME=str(tmp_path), USERPROFILE=str(tmp_path), XDG_DATA_HOME=str(tmp_path / "data"),
               XDG_CONFIG_HOME=str(tmp_path / "config"), APPDATA=str(tmp_path / "appdata"))
    for name in ("ROTWK_INSTALL", "BFME2_INSTALL", "DISPLAY", "WAYLAND_DISPLAY"):
        env.pop(name, None)
    return env


def test_the_runtime(tmp_path):
    """launcher/tests/l10n_test.gd: placeholders, plurals, decimal separators, the install's language"""
    godot = _godot()
    work = tmp_path / "installs"
    work.mkdir()
    r = subprocess.run([godot, "--headless", "--path", str(REPO / "launcher"), "--script", "res://tests/l10n_test.gd", "--",
                        f"--dir={work}"], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300,
                       stdin=subprocess.DEVNULL, env=_env(tmp_path))
    out = r.stdout + r.stderr
    assert r.returncode == 0 and "L10N OK" in r.stdout and "ERROR" not in out, out[-4000:]


def test_the_runtime_reports_every_misuse(tmp_path):
    """no silent default: each misuse is a push_error and stays visible in the text"""
    godot = _godot()
    r = subprocess.run([godot, "--headless", "--path", str(REPO / "launcher"), "--script", "res://tests/l10n_test.gd", "--",
                        "--errors"], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=300,
                       stdin=subprocess.DEVNULL, env=_env(tmp_path))
    out = r.stdout + r.stderr
    assert r.returncode == 0, out[-4000:]
    for error, shown in [("unknown text id 'launcher.no_such_id'", "ERRCASE unknown=launcher.no_such_id"),
                         ("test.missing needs the argument 'name'", "ERRCASE missing=a {name} b"),
                         ("the argument 'n' is a number ({n:d}), got \"many\"", "ERRCASE type=many files"),
                         ("no text catalog for the language 'xx'", "ERRCASE language=false en")]:
        assert error in out and shown in r.stdout, out[-4000:]
