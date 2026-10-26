"""MP3 fixture contracts, tool invocation and failure handling (no audio tools needed)."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys

import pytest

SPEC = importlib.util.spec_from_file_location("gen_mp3_fixtures", Path(__file__).with_name("gen_mp3_fixtures.py"))
gen = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gen)


def test_reference_contracts_cover_exactly_the_generated_fixtures():
    names = [name for name, *_ in gen.FIXTURES]
    rows = [line.split("\t") for line in gen.data_lines(gen.MANIFEST)]
    assert [row[0] for row in rows] == names
    assert [line.split("\t")[0] for line in gen.data_lines(gen.GOLDEN)] == names
    assert len(names) == len(set(names)) == 17
    assert sum(int(row[1]) for row in rows) == 51522
    for _, size, digest in rows:
        assert 0 < int(size) < 12 * 1024
        assert len(bytes.fromhex(digest)) == 32


@pytest.fixture
def fixtures(tmp_path, monkeypatch):
    out = tmp_path / "fixtures"
    out.mkdir()
    rows = []
    for name, *_ in gen.FIXTURES:
        data = ("synthetic unit-test payload " + name).encode()
        (out / (name + ".mp3.bin")).write_bytes(data)
        rows.append(f"{name}\t{len(data)}\t{hashlib.sha256(data).hexdigest()}")
    manifest = tmp_path / "manifest.tsv"
    manifest.write_text("# test contract\n" + "\n".join(rows) + "\n")
    monkeypatch.setattr(gen, "MANIFEST", str(manifest))
    return out


def test_check_only_needs_no_encoder_decoder_or_git(fixtures, monkeypatch, capsys):
    monkeypatch.setattr(sys, "argv", ["gen", "--out", str(fixtures), "--check-only"])

    def unexpected(*args, **kwargs):
        pytest.fail("check-only must not discover or execute external tools")

    monkeypatch.setattr(gen.shutil, "which", unexpected)
    monkeypatch.setattr(gen.subprocess, "run", unexpected)
    gen.main()
    assert "Verified 17 MP3 fixtures (sizes and SHA-256)" in capsys.readouterr().out


def test_valid_existing_fixtures_are_reused_without_any_tools(fixtures, monkeypatch):
    before = {p.name: p.read_bytes() for p in fixtures.iterdir()}
    monkeypatch.setattr(sys, "argv", ["gen", "--out", str(fixtures), "--lame", "lame-4.0"])

    def unexpected(*args, **kwargs):
        pytest.fail("valid fixtures must not invoke or discover tools")

    monkeypatch.setattr(gen.shutil, "which", unexpected)
    monkeypatch.setattr(gen.subprocess, "run", unexpected)
    gen.main()
    assert {p.name: p.read_bytes() for p in fixtures.iterdir()} == before


@pytest.mark.parametrize("version", ["3.100", "4.0", "3.99.5", "3.100.1"])
def test_encoder_version_is_explicit(monkeypatch, version):
    monkeypatch.setattr(gen, "run", lambda *args, **kwargs: subprocess.CompletedProcess(
        args[0], 0, stdout=f"LAME 64bits version {version} (reference)\n", stderr=""))
    if version == "3.100":
        assert gen.lame_version("lame").startswith("LAME 64bits version 3.100")
    else:
        with pytest.raises(SystemExit, match=f"lame {version} found, fixtures need 3.100"):
            gen.lame_version("lame")


def test_lame_4_does_not_publish_or_rebaseline(fixtures, monkeypatch):
    before = {p.name: p.read_bytes() for p in fixtures.iterdir()}
    # Explicit rebaselining must not replace even valid fixtures with a different encoder.
    monkeypatch.setattr(sys, "argv", ["gen", "--out", str(fixtures), "--write-golden"])
    monkeypatch.setattr(gen.shutil, "which", lambda tool: tool)
    monkeypatch.setattr(gen, "run", lambda *args, **kwargs: subprocess.CompletedProcess(
        args[0], 0, stdout="LAME version 4.0\n", stderr=""))
    with pytest.raises(SystemExit, match="lame 4.0 found, fixtures need 3.100"):
        gen.main()
    assert {p.name: p.read_bytes() for p in fixtures.iterdir()} == before


@pytest.mark.parametrize("name", [name for name, *_ in gen.FIXTURES])
def test_each_required_fixture_must_exist(fixtures, name):
    (fixtures / (name + ".mp3.bin")).unlink()
    with pytest.raises(SystemExit, match="MP3 fixture missing or unreadable:.*" + name):
        gen.check_fixtures(str(fixtures))


@pytest.mark.parametrize("change", ["same-size corruption", "truncated"])
def test_size_and_hash_are_both_checked(fixtures, change):
    path = fixtures / "joint_44k.mp3.bin"
    data = path.read_bytes()
    path.write_bytes(data[:-1] if change == "truncated" else bytes([data[0] ^ 1]) + data[1:])
    with pytest.raises(SystemExit, match="size/SHA-256 mismatch for joint_44k"):
        gen.check_fixtures(str(fixtures))


@pytest.mark.parametrize("contract", ["MANIFEST", "GOLDEN"])
def test_fixture_name_drift_is_rejected(fixtures, tmp_path, monkeypatch, contract):
    changed = tmp_path / "changed.tsv"
    changed.write_text("unexpected\t1\t0\n")
    monkeypatch.setattr(gen, contract, str(changed))
    with pytest.raises(SystemExit, match="fixture names differ"):
        gen.check_fixtures(str(fixtures))


@pytest.mark.parametrize("tool", ["lame", "mpg123"])
def test_missing_tool_is_named(tmp_path, monkeypatch, tool):
    monkeypatch.setattr(sys, "argv", ["gen", "--out", str(tmp_path / "out")])
    monkeypatch.setattr(gen.shutil, "which", lambda name: None if name == tool else name)
    monkeypatch.setattr(gen, "lame_version", lambda tool: "LAME version 3.100")
    with pytest.raises(SystemExit, match="MP3 fixture tool not found: " + tool):
        gen.main()


@pytest.mark.parametrize("mismatch", [None, "golden", "hash"])
def test_resolved_tools_and_publish_only_after_validation(tmp_path, monkeypatch, mismatch):
    out = tmp_path / "published"
    out.mkdir()
    (out / "tiny.mp3.bin").write_bytes(b"previous valid output")
    payload = b"synthetic unit-test stream"
    golden = tmp_path / "golden.tsv"
    golden.write_text("tiny\t44100\t1\t4\t3\t1.871\t0\n")
    manifest = tmp_path / "manifest.tsv"
    manifest.write_text(f"tiny\t{len(payload)}\t{hashlib.sha256(payload).hexdigest()}\n")
    if mismatch:
        (golden if mismatch == "golden" else manifest).write_text("tiny\twrong contract\n")
    monkeypatch.setattr(gen, "GOLDEN", str(golden))
    monkeypatch.setattr(gen, "MANIFEST", str(manifest))
    monkeypatch.setattr(gen, "FIXTURES", [("tiny", 44100, 1, [], lambda rate: [[0.0] * 4])])
    monkeypatch.setattr(gen, "check_rate", lambda *args: None)
    monkeypatch.setattr(gen.shutil, "which", lambda tool: None if tool == "ffmpeg" else tool)
    lame, mpg123 = str(tmp_path / "host tools" / "lame"), str(tmp_path / "host tools" / "mpg123")
    commands = []

    def run(cmd, **kwargs):
        commands.append(cmd)
        if "--version" in cmd:
            return subprocess.CompletedProcess(cmd, 0, stdout="LAME version 3.100\n", stderr="")
        if cmd[0] == lame:
            Path(cmd[-1]).write_bytes(payload)
            return subprocess.CompletedProcess(cmd, 0)
        assert cmd[0] == mpg123
        return subprocess.CompletedProcess(cmd, 0, stdout=struct.pack("<4h", 0, 1, 2, 3))

    monkeypatch.setattr(gen, "run", run)
    monkeypatch.setattr(sys, "argv", ["gen", "--out", str(out), "--lame", lame, "--mpg123", mpg123])
    if mismatch:
        with pytest.raises(SystemExit, match="differ from mp3_golden|size/SHA-256 mismatch"):
            gen.main()
        assert (out / "tiny.mp3.bin").read_bytes() == b"previous valid output"
    else:
        gen.main()
        assert (out / "tiny.mp3.bin").read_bytes() == payload
    assert [cmd[0] for cmd in commands] == [lame, mpg123, lame, mpg123]
