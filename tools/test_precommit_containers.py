"""Lane WINCRASH-1 r5: the commit gate refuses containers and compressed streams (tools/precommit.py rule 4), never unpacking them; the one
exception is a path + sha256 in tools/precommit_containers.txt. Sol r4's three cases are regressions: a ZIP holding a .pdb, a ZIP holding a
PDB renamed readme.bin, a gzipped PDB; each passed the byte checks of round 4."""
from __future__ import annotations

import gzip
import hashlib
import io
import os
import shutil
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
sys.path.insert(0, str(HERE))

import precommit  # noqa: E402

# a PDB's leading bytes (the MSF 7.00 superblock) and some content: enough for the MSF check the containers hide it from
PDB = precommit.MSF_MAGICS[0] + struct.pack("<6I", 512, 1, 8, 0, 0, 3) + b"\0" * 400 + b"symbols" * 300


def zipped(name: str, data: bytes) -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(name, data)
    return buf.getvalue()


def gate(tmp_path: Path, files: dict[str, bytes], allow: str = "") -> subprocess.CompletedProcess:
    """The real tools/precommit.py, run in a fresh repository with `files` staged."""
    repo = tmp_path / "repo"
    (repo / "tools").mkdir(parents=True)
    shutil.copy(HERE / "precommit.py", repo / "tools" / "precommit.py")
    (repo / "tools" / "precommit_containers.txt").write_text(allow)
    env = dict(os.environ, GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1")
    subprocess.run(["git", "init", "-q", str(repo)], check=True, env=env)
    for name, data in files.items():
        (repo / name).parent.mkdir(parents=True, exist_ok=True)
        (repo / name).write_bytes(data)
        subprocess.run(["git", "-C", str(repo), "add", name], check=True, env=env)
    return subprocess.run([sys.executable, str(repo / "tools" / "precommit.py")], capture_output=True, text=True, env=env)


@pytest.mark.parametrize("name,data", [
    ("assets/secret.zip", zipped("secret.pdb", b"debug symbols, deflated")),        # Sol r4 case 1
    ("assets/data.bin", zipped("readme.bin", PDB)),                                  # Sol r4 case 2: a renamed PDB in a renamed ZIP
    ("docs/notes.txt", gzip.compress(PDB)),                                          # Sol r4 case 3: a gzipped PDB under a text name
])
def test_sol_r4_containers_are_refused(tmp_path, name, data):
    result = gate(tmp_path, {name: data})
    assert result.returncode == 1, result.stdout
    assert "container or compressed stream staged" in result.stdout and name in result.stdout


@pytest.mark.parametrize("name,data", [
    ("a.tar", b"x"), ("a.jar", b"x"), ("a.tgz", b"x"), ("a.7z", b"x"), ("a.pck", b"x"), ("a.zst", b"x"),
    ("blob1", b"\x1f\x8b\x08\x00" + b"\0" * 20), ("blob2", b"BZh91AY&SY" + b"\0" * 20), ("blob3", b"\xfd7zXZ\x00" + b"\0" * 20),
    ("blob4", b"7z\xbc\xaf\x27\x1c" + b"\0" * 20), ("blob5", b"Rar!\x1a\x07\x00" + b"\0" * 20), ("blob6", b"MSCF\0\0\0\0" + b"\0" * 20),
    ("blob7", b"\x28\xb5\x2f\xfd" + b"\0" * 20), ("blob8", b"\x04\x22\x4d\x18" + b"\0" * 20), ("blob9", b"x\x9c" + b"\0" * 20),
    ("blob10", b"\x5d\x00\x00\x80\x00" + b"\0" * 20), ("blob11", b"GDPC" + b"\0" * 20), ("blob12", b"\0" * 20 + b"GDPC"),
])
def test_every_container_kind_is_recognised(name, data):
    assert precommit.container_kind(name, data), name


def test_text_and_ordinary_binaries_pass(tmp_path):
    assert precommit.container_kind("notes.md", b"x^2 is a square\n") == ""
    assert precommit.container_kind("a.png", b"\x89PNG\r\n\x1a\n" + b"\0" * 64) == ""
    result = gate(tmp_path, {"docs/notes.md": b"x^2 is a square\nplain text\n", "engine/a.cpp": b"int main() { return 0; }\n"})
    assert result.returncode == 0, result.stdout


def test_an_allowlisted_container_passes_with_exactly_its_bytes(tmp_path):
    data = zipped("fixture.txt", b"a reviewed fixture")
    allow = f"# reviewed\n{hashlib.sha256(data).hexdigest()}  tests/fixture.zip  # the fixture tool needs a real ZIP\n"
    assert gate(tmp_path / "a", {"tests/fixture.zip": data}, allow).returncode == 0
    # other bytes at the path, or the same bytes at another path: refused
    other = zipped("fixture.txt", b"something else")
    assert gate(tmp_path / "b", {"tests/fixture.zip": other}, allow).returncode == 1
    assert gate(tmp_path / "c", {"tests/elsewhere.zip": data}, allow).returncode == 1


def test_the_allowlist_starts_empty():
    assert precommit.container_allowlist() == set()


def test_no_tracked_file_is_a_container():
    """Today's tree (every tracked blob, sparse checkouts included) holds no container: the rule needs no allowlist entry."""
    if not (REPO / ".git").exists():
        pytest.skip("not a git checkout")
    env = dict(os.environ, GIT_NO_LAZY_FETCH="1")
    names = [n for n in subprocess.run(["git", "-C", str(REPO), "ls-files", "-z", "-s"], capture_output=True, env=env).stdout.split(b"\0") if n]
    blobs = [(line.split(b"\t", 1)[1].decode(), line.split()[1].decode()) for line in names if line.startswith(b"100")]
    batch = subprocess.Popen(["git", "-C", str(REPO), "cat-file", "--batch"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, env=env)
    hits, checked = [], 0
    for path, sha in blobs:
        batch.stdin.write(sha.encode() + b"\n")
        batch.stdin.flush()
        header = batch.stdout.readline().split()
        if not header or header[-1] == b"missing":
            continue
        data = batch.stdout.read(int(header[2]))
        batch.stdout.read(1)
        checked += 1
        if precommit.container_kind(path, data):
            hits.append(path)
    batch.stdin.close()
    batch.wait()
    assert checked > 1000 and hits == [], hits
