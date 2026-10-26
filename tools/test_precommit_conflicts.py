"""Conflict-marker checks use whole staged blobs, with no documentation or fixture exemptions."""
from __future__ import annotations

import os
import shlex
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
CONFLICT = b"\n".join((b"<" * 7 + b" ours", b"ours", b"=" * 7, b"theirs", b">" * 7 + b" theirs")) + b"\n"


def git(repo: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True, env=ENV)


@pytest.fixture
def repo(tmp_path):
    repo = tmp_path / "repo"
    (repo / "tools").mkdir(parents=True)
    shutil.copy(HERE / "precommit.py", repo / "tools" / "precommit.py")
    git(tmp_path, "init", "-q", str(repo))
    return repo


def stage(repo: Path, name: str, data: bytes):
    path = repo / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    git(repo, "add", "--", name)


def gate(repo: Path) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(repo / "tools/precommit.py")], capture_output=True, text=True, env=ENV)


def test_staged_conflict_reports_every_file_and_line(repo):
    stage(repo, "launcher/scripts/main.gd", b"extends Control\n" + CONFLICT)
    result = gate(repo)
    assert result.returncode == 1, result.stdout
    assert result.stdout.splitlines()[1:] == [
        f" - launcher/scripts/main.gd:{number}: conflict marker in staged text" for number in (2, 4, 6)
    ]


def test_installed_hook_refuses_commit_of_staged_conflict(repo):
    hook = repo / ".git/hooks/pre-commit"
    python = shlex.quote(sys.executable.replace("\\", "/"))
    hook.write_text(f"#!/bin/sh\nexec {python} tools/precommit.py\n")
    hook.chmod(0o755)
    stage(repo, "launcher/scripts/main.gd", CONFLICT)
    result = subprocess.run(["git", "-C", str(repo), "commit", "-m", "change"], capture_output=True, text=True, env=ENV)
    assert result.returncode == 1
    assert "launcher/scripts/main.gd:1: conflict marker" in result.stdout + result.stderr
    assert subprocess.run(["git", "-C", str(repo), "rev-parse", "--verify", "HEAD"], capture_output=True, env=ENV).returncode != 0


@pytest.mark.parametrize("marker", [b"<" * 7 + b" ours", b">" * 7 + b" theirs", b"<" * 8, b">" * 8, b"=" * 7])
@pytest.mark.parametrize("ending", [b"\n", b"\r\n", b""])
def test_each_marker_is_refused_independently(repo, marker, ending):
    stage(repo, "notes.txt", b"first\nsecond\n" + marker + ending)
    result = gate(repo)
    assert result.returncode == 1, result.stdout
    assert "notes.txt:3: conflict marker" in result.stdout


@pytest.mark.parametrize("suffix", ["md", "adoc", "asciidoc"])
@pytest.mark.parametrize("length", [5, 6, 8, 20])
def test_documentation_underlines_of_other_lengths_pass(repo, suffix, length):
    stage(repo, f"docs/title.{suffix}", b"Title\n" + b"=" * length + b"\n")
    result = gate(repo)
    assert result.returncode == 0, result.stdout


@pytest.mark.parametrize("name", ["docs/title.md", "docs/title.adoc", "tools/fixtures/conflict.txt", "archive/content/fixture.txt"])
def test_documentation_and_fixtures_have_no_path_exemption(repo, name):
    stage(repo, name, b"Title\n" + b"=" * 7 + b"\n")
    result = gate(repo)
    assert result.returncode == 1, result.stdout
    assert f"{name}:2: conflict marker" in result.stdout


@pytest.mark.parametrize("text", [b"prefix " + b"<" * 7, b" " + b">" * 7, b"<" * 6, b">" * 6,
                                  b"=" * 7 + b" text", b"=" * 8])
def test_only_specified_line_starts_match(repo, text):
    stage(repo, "notes.txt", text + b"\n")
    result = gate(repo)
    assert result.returncode == 0, result.stdout


@pytest.mark.parametrize("staged_conflict", [False, True])
def test_staged_blob_not_working_tree_is_checked(repo, staged_conflict):
    stage(repo, "launcher/scripts/main.gd", CONFLICT if staged_conflict else b"extends Control\n")
    (repo / "launcher/scripts/main.gd").write_bytes(b"extends Control\n" if staged_conflict else CONFLICT)
    result = gate(repo)
    assert result.returncode == int(staged_conflict), result.stdout


def test_unchanged_marker_lines_in_a_modified_file_are_refused(repo):
    stage(repo, "notes.txt", CONFLICT)
    git(repo, "commit", "-q", "-m", "base")
    stage(repo, "notes.txt", CONFLICT + b"unrelated addition\n")
    result = gate(repo)
    assert result.returncode == 1, result.stdout
    assert "notes.txt:1: conflict marker" in result.stdout


@pytest.mark.parametrize("prefix", [b"\0", b"x" * 8192 + b"\0"])
def test_binary_blobs_are_skipped_even_with_a_late_nul(repo, prefix):
    stage(repo, "fixture.bin", prefix + b"\n" + CONFLICT)
    result = gate(repo)
    assert result.returncode == 0, result.stdout


def test_binary_blobs_still_get_byte_format_checks(repo):
    stage(repo, "fixture.bin", precommit.MSF_MAGICS[0] + b"\n" + CONFLICT)
    result = gate(repo)
    assert result.returncode == 1, result.stdout
    assert "debug symbols" in result.stdout
    assert "conflict marker" not in result.stdout


def test_pure_rename_checks_destination_blob(repo):
    stage(repo, "old.txt", CONFLICT)
    git(repo, "commit", "-q", "-m", "base")
    git(repo, "mv", "old.txt", "new.txt")
    result = gate(repo)
    assert result.returncode == 1, result.stdout
    assert "new.txt:1: conflict marker" in result.stdout


def test_deleted_conflict_file_is_not_read(repo):
    stage(repo, "old.txt", CONFLICT)
    git(repo, "commit", "-q", "-m", "base")
    git(repo, "rm", "old.txt")
    result = gate(repo)
    assert result.returncode == 0, result.stdout
    assert "0 changed files checked" in result.stdout


@pytest.mark.parametrize("name", ["space name.txt", "caf\N{LATIN SMALL LETTER E WITH ACUTE}.txt", "tab\tname.txt", "line\nname.txt"])
def test_unusual_paths_are_checked_without_git_quoting(repo, name):
    if os.name == "nt" and any(c in name for c in "\t\n"):
        pytest.skip("Windows file names cannot contain tabs or newlines")
    stage(repo, name, CONFLICT)
    result = gate(repo)
    assert result.returncode == 1, result.stdout
    assert f"{name}:1: conflict marker" in result.stdout


def test_large_blob_is_checked_to_the_end(repo):
    stage(repo, "large.txt", b"ordinary text\n" * 100000 + CONFLICT)
    result = gate(repo)
    assert result.returncode == 1, result.stdout
    assert "large.txt:100001: conflict marker" in result.stdout


def test_many_files_use_one_blob_reader_and_a_constant_git_process_count(repo, monkeypatch, capsys):
    monkeypatch.setattr(precommit, "REPO", repo)
    stage(repo, "notes.txt", b"ordinary text\n")
    commands = []
    popen = subprocess.Popen

    def traced_popen(args, *pos, **kwargs):
        commands.append(args)
        return popen(args, *pos, **kwargs)

    with monkeypatch.context() as trace:
        trace.setattr(subprocess, "Popen", traced_popen)
        assert precommit.main() == 0
    small = commands.copy()
    for number in range(1000):
        (repo / f"file-{number}.txt").write_bytes(b"ordinary text\n")
    git(repo, "add", "--", "*.txt")
    commands.clear()
    with monkeypatch.context() as trace:
        trace.setattr(subprocess, "Popen", traced_popen)
        assert precommit.main() == 0
    assert commands == small
    assert len(commands) == 4
    assert sum(command[1:] == ["cat-file", "--batch"] for command in commands) == 1
    assert "1001 changed files checked" in capsys.readouterr().out


def test_blob_reader_fails_closed_on_an_unavailable_object(repo, monkeypatch, capsys):
    monkeypatch.setattr(precommit, "REPO", repo)
    monkeypatch.setattr(precommit, "staged", lambda: [("missing.txt", "0" * 40)])
    assert precommit.main() == 1
    assert "cannot read staged blob: missing.txt" in capsys.readouterr().out
