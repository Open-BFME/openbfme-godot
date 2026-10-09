"""Lane LAUNCH-1: the release-side tools of the launcher (no Godot needed): ed25519.py against OpenSSL, sign_manifest.py's key rules,
make_manifest.py, publish_release.sh's dry run and the launcher packages' allowlist."""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import audit_package  # noqa: E402
import ed25519  # noqa: E402
import make_archive  # noqa: E402
import make_manifest  # noqa: E402
from launcher_test_server import tiny_package  # noqa: E402

COMMIT = "0123456789abcdef0123456789abcdef01234567"


def run(*cmd, **kw) -> subprocess.CompletedProcess:
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, timeout=300, **kw)


def package_dir(d: Path, version: str) -> Path:
    d.mkdir(parents=True, exist_ok=True)
    sums = ""
    for kind in ("game", "launcher"):
        for platform in ("linux-x64", "windows-x64"):
            n = make_manifest.asset_name(kind, version, platform)
            data = tiny_package(n, {"OpenBFME.x86_64" if platform.startswith("linux") else "OpenBFME.exe": f"{n} bytes".encode()})
            (d / n).write_bytes(data)
            sums += f"{hashlib.sha256(data).hexdigest()}  {n}\n"
    (d / f"SHA256SUMS-{version}.txt").write_text(sums)
    return d


@pytest.mark.skipif(not shutil.which("openssl"), reason="SKIP: no openssl for the cross-check")
def test_ed25519_py_matches_openssl(tmp_path):
    for i in range(4):
        sk = os.urandom(32)
        (tmp_path / "k.pem").write_text(ed25519.private_key_pem(sk))
        msg = os.urandom(i * 100 + 1)
        (tmp_path / "m").write_bytes(msg)
        assert run("openssl", "pkeyutl", "-sign", "-rawin", "-inkey", tmp_path / "k.pem", "-in", tmp_path / "m", "-out",
                   tmp_path / "s").returncode == 0
        assert (tmp_path / "s").read_bytes() == ed25519.sign(sk, msg)
        pub = run("openssl", "pkey", "-in", tmp_path / "k.pem", "-pubout").stdout
        assert ed25519.public_from_pem(pub) == ed25519.public_key(sk)


def test_key_rules(tmp_path):
    tool = HERE / "sign_manifest.py"
    r = run(sys.executable, tool, "--generate-key", REPO / "workspace" / "release.key")
    assert r.returncode != 0 and "inside the repository" in r.stderr + r.stdout
    other = tmp_path / "otherrepo"
    other.mkdir()
    run("git", "init", "-q", other)
    r = run(sys.executable, tool, "--generate-key", other / "k.key")
    assert r.returncode != 0 and "inside a git work tree" in r.stderr + r.stdout
    key = tmp_path / "offline" / "release.key"
    key.parent.mkdir()
    r = run(sys.executable, tool, "--generate-key", key)
    assert r.returncode == 0 and len(r.stdout.strip()) == 64, r.stderr
    assert (key.stat().st_mode & 0o777) == 0o600
    public = r.stdout.strip()
    assert run(sys.executable, tool, "--generate-key", key).returncode != 0, "a key is never overwritten"
    assert run(sys.executable, tool, "--public-key", key).stdout.strip() == public
    m = tmp_path / "manifest.json"
    m.write_bytes(b'{"x": 1}\n')
    r = run(sys.executable, tool, "--key", key, m)
    assert r.returncode == 0 and (tmp_path / "manifest.json.sig").stat().st_size == 64, r.stderr
    assert run(sys.executable, tool, "--verify", public, m).returncode == 0
    m.write_bytes(b'{"x": 2}\n')
    assert run(sys.executable, tool, "--verify", public, m).returncode == 1
    key.chmod(0o640)
    r = run(sys.executable, tool, "--key", key, m)
    assert r.returncode != 0 and "other users" in r.stderr + r.stdout


def test_make_manifest(tmp_path):
    d = package_dir(tmp_path / "out", "v0.3.0-preview.2")
    tool = HERE / "make_manifest.py"
    r = run(sys.executable, tool, d, "--repo", "Test/repo", "--commit", COMMIT, "--date", "2026-10-09")
    assert r.returncode != 0 and "is not a tag" in r.stderr + r.stdout, "a release version must be a tag on the commit"
    r = run(sys.executable, tool, d, "--repo", "Test/repo", "--commit", COMMIT, "--date", "2026-10-09", "--allow-untagged")
    assert r.returncode == 0, r.stderr
    first = (d / "manifest.json").read_bytes()
    m = json.loads(first)
    assert m["channel"] == "preview" and m["version"] == "v0.3.0-preview.2" and m["commit"] == COMMIT
    assert m["assets"]["launcher"]["windows-x64"]["name"] == "openbfme-launcher-v0.3.0-preview.2-windows-x64.zip"
    assert run(sys.executable, tool, d, "--repo", "Test/repo", "--commit", COMMIT, "--date", "2026-10-09", "--allow-untagged").returncode == 0
    assert (d / "manifest.json").read_bytes() == first, "the same packages give the same manifest bytes"
    assert run(sys.executable, tool, "--check", d).returncode == 0
    (d / "openbfme-v0.3.0-preview.2-linux-x64.tar.gz").write_bytes(b"other")
    assert run(sys.executable, tool, "--check", d).returncode != 0
    r = run(sys.executable, tool, d, "--allow-untagged", "--commit", COMMIT, "--date", "2026-10-09")
    assert r.returncode != 0 and "does not match" in r.stderr + r.stdout
    with pytest.raises(SystemExit):
        make_manifest.channel_of("0.3.0")


def test_publish_dry_run(tmp_path):
    d = package_dir(tmp_path / "out", "v0.3.0")
    run(sys.executable, HERE / "make_manifest.py", d, "--repo", "Test/repo", "--commit", COMMIT, "--date", "2026-10-09", "--allow-untagged")
    sk = os.urandom(32)
    (d / "manifest.json.sig").write_bytes(ed25519.sign(sk, (d / "manifest.json").read_bytes()))
    pub = tmp_path / "release_key.pub"
    pub.write_text(ed25519.public_key(sk).hex() + "\n")
    tool = HERE / "publish_release.sh"
    r = run("bash", tool, d, "--repo", "Test/repo", "--key-file", pub, "--allow-untagged")
    assert r.returncode == 0 and "DRY RUN" in r.stdout and "gh release create v0.3.0" in r.stdout, r.stdout + r.stderr
    assert "--prerelease" not in r.stdout and "manifest.json.sig" in r.stdout
    r = run("bash", tool, d, "--repo", "Other/repo", "--key-file", pub, "--allow-untagged")
    assert r.returncode != 0 and "the manifest is for Test/repo" in r.stderr
    r = run("bash", tool, d, "--repo", "Test/repo", "--key-file", pub, "--allow-untagged", "--execute")
    assert r.returncode != 0 and "dry runs only" in r.stderr
    other = tmp_path / "other.pub"
    other.write_text(ed25519.public_key(os.urandom(32)).hex())
    r = run("bash", tool, d, "--repo", "Test/repo", "--key-file", other, "--allow-untagged")
    assert r.returncode != 0 and "does not verify" in r.stderr
    r = run("bash", tool, d, "--repo", "Test/repo", "--key-file", pub)
    assert r.returncode != 0 and "is not a tag" in r.stderr


def launcher_pack(entries=None) -> bytes:
    from test_release_tools import pck  # noqa: PLC0415 (the synthetic pack builder)
    return pck(entries or [("project.binary", b"ECFG" + b"\0" * 4), ("scripts/main.gd", b"extends Control\n"),
                           ("scenes/main.tscn", b"[gd_scene format=3]\n"), ("build_info.json", b"{}\n")])


def elf_with_pack(pack: bytes, extra: bytes = b"") -> bytes:
    """an ELF64 whose one section holds the pack and its trailer, as Godot's export embeds it (header, section table, pack, u64 size,
    "GDPC")"""
    import struct  # noqa: PLC0415
    body = pack + struct.pack("<Q", len(pack)) + b"GDPC"
    h = bytearray(64)
    h[0:4] = b"\x7fELF"
    h[4], h[5], h[6] = 2, 1, 1
    struct.pack_into("<Q", h, 40, 64)                     # e_shoff
    struct.pack_into("<HHHH", h, 52, 64, 56, 0, 64)       # e_ehsize, e_phentsize, e_phnum, e_shentsize
    struct.pack_into("<H", h, 60, 1)                      # e_shnum
    sh = bytearray(64)
    struct.pack_into("<I", sh, 4, 1)                      # SHT_PROGBITS
    struct.pack_into("<QQ", sh, 24, 128, len(body))       # sh_offset, sh_size
    return bytes(h) + bytes(sh) + extra + body


def launcher_files() -> dict[str, bytes]:
    return {"OpenBFMELauncher.x86_64": elf_with_pack(launcher_pack()), "README.txt": b"read me\n", "LICENSE": b"licence\n",
            "NOTICE": b"notice\n", "VERSION": b"v\n"}


def test_launcher_allowlist(tmp_path):
    files = launcher_files()
    top = tmp_path / "openbfme-launcher-v0.3.0-linux-x64"
    top.mkdir()
    for n, data in files.items():
        (top / n).write_bytes(data)
    assert audit_package.check_release("launcher-linux", top) == []
    make_archive.tar_gz(top, tmp_path / f"{top.name}.tar.gz", 1700000000)
    assert audit_package.check_release("launcher-linux", tmp_path / f"{top.name}.tar.gz") == []
    # the game's files are not the launcher's, and the reverse
    assert audit_package.check_release("linux", top)
    (top / "OpenBFMELauncher.pck").write_bytes(launcher_pack())
    assert any("not a file of the release" in p for p in audit_package.check_release("launcher-linux", top)), "the pack is embedded"
    (top / "OpenBFMELauncher.pck").unlink()
    exe = top / "OpenBFMELauncher.x86_64"
    # a test script or an unknown resource in the embedded pack; no pack; bytes the ELF headers do not describe
    exe.write_bytes(elf_with_pack(launcher_pack([("tests/unit_test.gd", b"extends SceneTree\n")])))
    assert any("not a resource type" in p for p in audit_package.check_release("launcher-linux", top))
    from test_release_tools import elf64  # noqa: PLC0415
    exe.write_bytes(elf64())
    assert any("no embedded Godot pack" in p for p in audit_package.check_release("launcher-linux", top))
    exe.write_bytes(elf64() + launcher_pack() + __import__("struct").pack("<Q", len(launcher_pack())) + b"GDPC")
    assert any("bytes outside what the ELF headers describe" in p for p in audit_package.check_release("launcher-linux", top))
    exe.write_bytes(files["OpenBFMELauncher.x86_64"])
    # a launcher folder named like a game package, and the reverse
    bad = tmp_path / "openbfme-v0.3.0-linux-x64"
    shutil.copytree(top, bad)
    assert any("is not openbfme-launcher-" in p for p in audit_package.check_release("launcher-linux", bad))


def test_launcher_network_rule():
    """The launcher talks to the network in one file (http_fetch.gd, HTTPClient), whose every connection is checked by NetPolicy first,
    and NetPolicy names exactly GitHub's API, its release pages and its asset hosts; it starts processes only for the game and itself."""
    import net_guard  # noqa: PLC0415
    found = net_guard.scan_launcher()
    bad = [f"{p}: {f}" for p, fs in found.items() for f in fs if not net_guard.launcher_finding_allowed(p, f)]
    assert bad == []
    assert "launcher/scripts/net/http_fetch.gd" in found
    fetch = (REPO / "launcher/scripts/net/http_fetch.gd").read_text()
    open_fn = fetch[fetch.index("func _open("):fetch.index("static func _status_text")]
    assert open_fn.index("policy.check(url)") < open_fn.index("connect_to_host("), "the policy is checked before every connection"
    assert open_fn.count("connect_to_host(") == 1 and fetch.count("connect_to_host(") == 1
    assert "TLSOptions.client()" in open_fn and "TLSOptions.client_unsafe" not in fetch
    upd = (REPO / "launcher/scripts/core/self_update.gd").read_text()
    assert upd.count("OS.execute(") == 1 and 'OS.execute("cmd.exe", ["/c", "move", "/y"' in upd
    policy = (REPO / "launcher/scripts/net/net_policy.gd").read_text()
    assert 'const GITHUB_HOSTS := ["api.github.com", "github.com"]' in policy
    assert 'const GITHUB_ASSET_SUFFIX := ".githubusercontent.com"' in policy


SMALL_ORDER = ["0000000000000000000000000000000000000000000000000000000000000000", "0000000000000000000000000000000000000000000000000000000000000080",
               "0100000000000000000000000000000000000000000000000000000000000000", "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc05",
               "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc85", "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac037a",
               "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac03fa", "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f"]


def test_small_order_points_are_refused():
    """Sol r1: the identity key with R = identity and S = 0 satisfies [S]B = R + [h]A for every message; every small-order A and R is
    refused (crypto_test.gd holds the same list)."""
    found = set()
    for i in range(64):
        y = int.from_bytes(hashlib.sha256(bytes([i])).digest(), "little") % ed25519.p
        x = ed25519._recover_x(y, 0)
        if x is not None:
            found.add(ed25519._compress(ed25519._mul(ed25519.L, (x, y, 1, x * y % ed25519.p))).hex())
    assert found == set(SMALL_ORDER), "the torsion points of the curve"
    identity = bytes.fromhex(SMALL_ORDER[2])
    forged = identity + bytes(32)
    A = ed25519._decompress(identity)
    assert ed25519._equal(ed25519._mul(0, ed25519.G), ed25519._add(ed25519._decompress(identity), ed25519._mul(5, A))), \
        "the forgery satisfies the verification equation"
    for enc in SMALL_ORDER:
        assert not ed25519.verify(bytes.fromhex(enc), b"any message", forged)
    sk = os.urandom(32)
    good = ed25519.sign(sk, b"m")
    assert ed25519.verify(ed25519.public_key(sk), b"m", good)
    assert not ed25519.verify(ed25519.public_key(sk), b"m", identity + good[32:]), "a small-order R"


def test_the_manifest_lists_every_file_of_every_package(tmp_path):
    """format 2 (round 3): the signature covers each package's files, so the launcher can check unpacked files before it runs them"""
    d = package_dir(tmp_path / "out", "v0.3.0")
    m = make_manifest.build(d, "v0.3.0", "Test/repo", COMMIT, "2026-10-09")
    assert m["format"] == 2
    f = m["assets"]["launcher"]["windows-x64"]["files"]
    data = b"openbfme-launcher-v0.3.0-windows-x64.zip bytes"
    assert f == {"OpenBFME.exe": {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()}}
    with pytest.raises(SystemExit):
        make_manifest.package_files("openbfme-v1.0.0-linux-x64.tar.gz", b"not an archive")
    with pytest.raises(SystemExit):  # a member outside the package folder
        make_manifest.package_files("openbfme-v1.0.0-linux-x64.tar.gz", tiny_package("openbfme-v9.9.9-linux-x64.tar.gz", {"x": b"1"}))
