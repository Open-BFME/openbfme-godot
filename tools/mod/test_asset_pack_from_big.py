"""Lane SDK-1: tools/mod/asset_pack_from_big.py on a made-up archive (no mod or game bytes)."""
from __future__ import annotations

import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOL = HERE / "asset_pack_from_big.py"


def make_big(path: Path, files: list[tuple[str, bytes]]) -> None:
    index = b"".join(struct.pack(">II", 0, 0) + n.encode("latin-1") + b"\0" for n, _ in files)
    at = 16 + len(index)
    entries, body = b"", b""
    for name, data in files:
        entries += struct.pack(">II", at + len(body), len(data)) + name.encode("latin-1") + b"\0"
        body += data
    path.write_bytes(b"BIGF" + struct.pack("<I", 16 + len(entries) + len(body)) + struct.pack(">II", len(files), 16 + len(entries)) + entries + body)


def test_models_and_textures_become_a_pack_the_rest_is_listed(tmp_path):
    big = tmp_path / "hd.big"
    make_big(big, [
        ("art\\w3d\\gu\\gumaarms_skn.w3d", b"model"),
        ("art\\compiledtextures\\gu\\gumaarms.dds", b"DDS hd"),
        ("art\\textures\\gumaarms.tga", b"tga loses"),          # the lookup takes the compiled .dds first
        ("art\\compiledtextures\\hc\\hc_warg.jpg", b"jpg loses"),
        ("art\\compiledtextures\\hc\\hc_warg.tga", b"tga wins"),  # the models name .tga files
        ("data\\ini\\weapon.ini", b"Weapon X"),
        ("asset.dat", b"dat"),
    ])
    pack = tmp_path / "pack"
    r = subprocess.run([sys.executable, str(TOOL), str(big), str(pack)], capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stdout + r.stderr
    files = sorted(str(p.relative_to(pack)).replace("\\", "/") for p in pack.rglob("*") if p.is_file())
    assert files == ["art/compiledtextures/gu/gumaarms.dds", "art/compiledtextures/hc/hc_warg.tga", "art/w3d/gu/gumaarms_skn.w3d"], files
    assert (pack / "art" / "compiledtextures" / "gu" / "gumaarms.dds").read_bytes() == b"DDS hd"
    out = r.stdout.splitlines()
    assert "left out: art\\textures\\gumaarms.tga: the game's texture lookup takes art\\compiledtextures\\gu\\gumaarms.dds first" in out, out
    assert "left out: art\\compiledtextures\\hc\\hc_warg.jpg: the game's texture lookup takes art\\compiledtextures\\hc\\hc_warg.tga first" in out, out
    assert "left out: data\\ini\\weapon.ini: not a model or texture (game data stays in the mod)" in out, out
    assert out[-1] == f"asset pack {pack}: 1 models, 2 textures; 4 files left out", out
    # a folder that is not empty is refused (nothing is overwritten)
    r = subprocess.run([sys.executable, str(TOOL), str(big), str(pack)], capture_output=True, text=True, timeout=60)
    assert r.returncode == 2 and "is not empty" in r.stdout


def test_a_loose_tga_beats_a_compiled_jpg(tmp_path):
    """a model names x.tga: the lookup reaches art\\textures\\x.tga, never a compiled .jpg (Sol r1)"""
    big = tmp_path / "hd.big"
    make_big(big, [("art\\compiledtextures\\tr\\tree.jpg", b"jpg"), ("art\\textures\\tree.tga", b"tga")])
    r = subprocess.run([sys.executable, str(TOOL), str(big), str(tmp_path / "pack")], capture_output=True, text=True, timeout=60)
    assert r.returncode == 0, r.stdout + r.stderr
    assert (tmp_path / "pack" / "art" / "textures" / "tree.tga").read_bytes() == b"tga"
    assert not (tmp_path / "pack" / "art" / "compiledtextures").exists()


def test_an_archive_path_leaving_the_pack_is_refused_before_anything_is_written(tmp_path):
    """Sol r1: an entry name with '..', a root or a drive must not write outside the pack folder"""
    victim = tmp_path / "victim.dds"
    victim.write_bytes(b"mine")
    for bad in ["..\\victim.dds", "art\\..\\..\\victim.dds", "/tmp/x.dds", "C:\\x.dds", "art\\\\x.dds"]:
        big = tmp_path / "evil.big"
        make_big(big, [("art\\w3d\\gu\\ok.w3d", b"model"), (bad, b"evil")])
        pack = tmp_path / "pack"
        r = subprocess.run([sys.executable, str(TOOL), str(big), str(pack)], capture_output=True, text=True, timeout=60)
        assert r.returncode == 1 and "would leave the pack folder: nothing written" in r.stdout, (bad, r.stdout + r.stderr)
        assert victim.read_bytes() == b"mine"
        assert not pack.exists() or not any(pack.iterdir()), bad
