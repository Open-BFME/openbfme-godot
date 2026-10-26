"""Lane LAUNCH-1: the release-side tools of the launcher (no Godot needed): ed25519.py against OpenSSL, sign_manifest.py's key rules,
make_manifest.py, publish_release.sh's dry run and the launcher packages' allowlist."""
from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path, PurePosixPath

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import audit_package  # noqa: E402
import ed25519  # noqa: E402
import make_archive  # noqa: E402
import make_manifest  # noqa: E402
import onezip_windows_test  # noqa: E402
from launcher_test_server import tiny_package  # noqa: E402

COMMIT = "0123456789abcdef0123456789abcdef01234567"


def test_onezip_mode_options_and_paths(tmp_path, monkeypatch):
    validate = onezip_windows_test.validate_mode_options
    validate("wine", False, False, None, None, False)
    validate(None, True, False, None, None, False)
    validate(None, False, True, None, None, True)
    with pytest.raises(ValueError, match="exactly one"):
        validate(None, False, False, None, None, True)
    with pytest.raises(ValueError, match="--native requires Windows"):
        validate(None, False, True, None, None, False)
    with pytest.raises(ValueError, match="--launchers requires --key"):
        validate(None, False, True, Path("launchers"), None, True)
    with pytest.raises(ValueError, match="--key requires --launchers"):
        validate(None, False, True, None, Path("key.secret"), True)

    spaced = tmp_path / "path with spaces"
    assert onezip_windows_test.resolve_windows_path(spaced, "native") == str(spaced.resolve())
    assert onezip_windows_test.resolve_windows_path(PurePosixPath("/mnt/c/path with spaces"), "wine") == r"Z:\mnt\c\path with spaces"
    monkeypatch.setattr(onezip_windows_test.subprocess, "run",
                        lambda *args, **kwargs: subprocess.CompletedProcess(args[0], 0, stdout=r"C:\path with spaces"))
    assert onezip_windows_test.resolve_windows_path(spaced, "interop") == r"C:\path with spaces"


@pytest.mark.parametrize("official", [True, False, None, "longer"])
def test_custom_key_labels_trial_without_enabling_test_overrides(tmp_path, official):
    """build_launcher.sh's build_info.json: local_trial unless the trusted list is exactly the staged launcher's own (SIGNKMS-1: the
    staged release_keys.pub, or for a source without one its release_key.pub), never test_build; release_keys begins with release_key"""
    key = "12" * 32
    keys = f"{key},{'56' * 32}"
    official_arg = {True: keys, False: "34" * 32, None: "", "longer": keys + "," + "78" * 32}[official]
    script = (HERE / "build_launcher.sh").read_text()
    build_info = script.split('"$TEST_BUILD" "$OFFICIAL" <<\'EOF\'\n', 1)[1].split("\nEOF", 1)[0]
    info_path = tmp_path / "build_info.json"
    result = run(sys.executable, "-", info_path, "v0.0.0-alpha.9", COMMIT, "2026-10-15", "Test/repo", key, keys, "false", official_arg,
                 input=build_info)
    assert result.returncode == 0, result.stderr
    info = json.loads(info_path.read_text())
    assert info["local_trial"] is (official is not True)
    assert info["test_build"] is False
    assert info["release_key"] == key
    assert info["release_keys"] == keys.split(",")
    bad = run(sys.executable, "-", info_path, "v0.0.0-alpha.9", COMMIT, "2026-10-15", "Test/repo", "56" * 32, keys, "false", keys,
              input=build_info)
    assert bad.returncode != 0 and "release_keys does not begin with release_key" in bad.stderr, bad.stderr


def test_build_launcher_checks_the_key_list_before_anything_is_built(tmp_path):
    """SIGNKMS-1: build_launcher.sh --keys-file must begin with --key-file's key and parse (release_keys.py); without --keys-file the
    committed launcher/release_keys.pub is used when --key-file is its first key, else the --key-file key alone (a test key). The key
    checks come before the Godot check, so a fake Godot that fails shows how far a run got."""
    deck = ed25519.public_key(b"\x01" * 32).hex()
    kms = ed25519.public_key(b"\x02" * 32).hex()
    key = tmp_path / "key.pub"
    key.write_text(deck + "\n")
    fake = tmp_path / "godot"
    fake.write_text("#!/bin/sh\necho 4.6.0.fake\n")
    fake.chmod(0o755)

    def build(*extra):
        return run("bash", HERE / "build_launcher.sh", "--out", tmp_path / "out", "--version", "v0.0.1", "--commit", COMMIT, "--repo",
                   "Test/repo", "--key-file", key, "--mtime", "1700000000", "--godot", fake, *extra)

    good = tmp_path / "keys.pub"
    good.write_text(f"# test\n{deck}  deck-test\n{kms}  kms-test\n")
    r = build("--keys-file", good)
    assert r.returncode != 0 and "Godot 4.7.2 is required" in r.stderr, r.stderr   # the list passed
    swapped = tmp_path / "swapped.pub"
    swapped.write_text(f"{kms}  kms-test\n{deck}  deck-test\n")
    r = build("--keys-file", swapped)
    assert r.returncode != 0 and f"the first key of {swapped} is not the key of {key}" in r.stderr, r.stderr
    broken = tmp_path / "broken.pub"
    broken.write_text(f"{deck}  deck-test\n{deck}  again\n")
    r = build("--keys-file", broken)
    assert r.returncode != 0 and "is not a valid release key list" in r.stderr and "listed twice" in r.stderr, r.stderr
    r = build("--keys-file", tmp_path / "missing.pub")
    assert r.returncode != 0 and "does not exist" in r.stderr, r.stderr
    r = build()   # a test key without --keys-file: a one-key list
    assert r.returncode != 0 and "Godot 4.7.2 is required" in r.stderr, r.stderr


def test_the_committed_key_list_begins_with_the_legacy_key():
    """SIGNKMS-1: launcher/release_keys.pub parses and its first entry is launcher/release_key.pub (deck-2026)"""
    import release_keys
    keys = release_keys.load(HERE.parents[1] / "launcher/release_keys.pub")
    assert keys[0] == ("deck-2026", bytes.fromhex((HERE.parents[1] / "launcher/release_key.pub").read_text().strip()))


def run(*cmd, **kw) -> subprocess.CompletedProcess:
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, encoding="utf-8", errors="replace",
                          timeout=300, **kw)


@pytest.fixture(autouse=True)
def _private_denylist(tmp_path, monkeypatch):
    """PUBLISH-2 r2: publish_release.sh's privacy gate needs the operator's private denylist; a fictional one here"""
    f = tmp_path / "denylist.json"
    f.write_text(json.dumps({"rules": [{"id": "hosts", "kind": "word", "pattern": ["pubhost-q"]}]}))
    monkeypatch.setenv("OPENBFME_RELEASE_DENYLIST", str(f))


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
    if os.name != "nt":
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
    if os.name != "nt":
        key.chmod(0o640)
        r = run(sys.executable, tool, "--key", key, m)
        assert r.returncode != 0 and "other users" in r.stderr + r.stdout


def test_make_manifest(tmp_path):
    d = package_dir(tmp_path / "out", "v0.3.0-preview.2")
    tool = HERE / "make_manifest.py"
    tree = run("git", "-C", REPO, "rev-parse", "HEAD^{tree}").stdout.strip()
    commit = run("git", "-C", REPO, "commit-tree", tree, "-m", "untagged manifest test",
                 env=dict(os.environ, GIT_AUTHOR_NAME="Fixture", GIT_AUTHOR_EMAIL="fixture@example.invalid",
                          GIT_COMMITTER_NAME="Fixture", GIT_COMMITTER_EMAIL="fixture@example.invalid")).stdout.strip()
    assert re.fullmatch(r"[0-9a-f]{40}", commit), "create a real untagged commit for the tag-policy test"
    r = run(sys.executable, tool, d, "--repo", "Test/repo", "--commit", commit, "--date", "2026-10-09")
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


def test_nightly_release_version():
    """NIGHTLY-1 (owner, 2026-10-21): v<x>.<y>.<z>-nightly.<YYYYMMDD> is a pre-release (channel id "preview"), ordered between alpha and
    preview of its release and below the release (the launcher's semver.gd order)"""
    import make_macos_manifest
    order = ["v0.4.12", "v0.4.13-alpha.1", "v0.4.13-nightly.1", "v0.4.13-nightly.20261021", "v0.4.13-nightly.20261022",
             "v0.4.13-preview.1", "v0.4.13"]
    assert sorted(reversed(order), key=make_manifest.version_key) == order
    assert make_manifest.channel_of("v0.4.13-nightly.20261022") == "preview" and make_manifest.channel_of("v0.4.13") == "stable"
    for bad in ["v0.4.13-nightly", "v0.4.13-nightly.", "v0.4.13-nightly.020261022", "v0.4.13-nightly.1234567890", "v0.4.13-nightly.1x"]:
        with pytest.raises(SystemExit):
            make_manifest.channel_of(bad)
    assert make_macos_manifest.MACOS_ASSET.match("openbfme-v0.4.13-nightly.20261022-macos-arm64.zip")
    assert not make_macos_manifest.MACOS_ASSET.match("openbfme-v0.4.13-nightly.-macos-arm64.zip")


def test_publish_dry_run(tmp_path):
    if os.name == "nt":
        pytest.skip("intrinsically POSIX: production Bash publish dry-run")
    d = package_dir(tmp_path / "out", "v0.3.0")
    run(sys.executable, HERE / "make_manifest.py", d, "--repo", "Test/repo", "--commit", COMMIT, "--date", "2026-10-09", "--allow-untagged")
    sk = os.urandom(32)
    (d / "manifest.json.sig").write_bytes(ed25519.sign(sk, (d / "manifest.json").read_bytes()))
    pub = tmp_path / "release_key.pub"
    pub.write_text(ed25519.public_key(sk).hex() + "\n")
    tool = HERE / "publish_release.sh"
    r = run("bash", tool, d, "--repo", "Test/repo", "--key-file", pub, "--allow-untagged")
    assert r.returncode == 0 and "DRY RUN" in r.stdout and "repos/Test/repo/releases" in r.stdout, r.stdout + r.stderr
    assert "tag_name=v0.3.0 " in r.stdout and "-F draft=true" in r.stdout and "prerelease=false" in r.stdout and "manifest.json.sig" in r.stdout
    assert "release create" not in r.stdout, "never gh release create (its failure path deletes the release, Sol r1)"
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


from test_release_tools import elf_with_pack, launcher_pack  # noqa: E402  (the synthetic launcher executable)


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


def test_the_launcher_presets_have_no_include_or_exclude_filter():
    """REPRO-1: Godot 4.7.2 applies these filters in the file system's directory order (_edit_files_with_filter), which decides the order
    of the pack: a file the pack needs or must not hold goes to addons/pack_files/export_plugin.gd (ADD / SKIP), never to a filter."""
    text = (REPO / "launcher" / "export_presets.cfg").read_text(encoding="utf-8")
    presets = len(re.findall(r"^\[preset\.\d+\]$", text, re.M))
    assert presets == 3, "the launcher has Linux, Windows and macOS presets"
    for key in ("include_filter", "exclude_filter"):
        assert text.count(f'\n{key}=""\n') == presets, f"every preset's {key} must be empty"
    project = (REPO / "launcher" / "project.godot").read_text(encoding="utf-8")
    assert 'enabled=PackedStringArray("res://addons/pack_files/plugin.cfg")' in project, "the plugin that picks the pack's files is on"


# ---- lane ONEZIP-1: one package per platform -------------------------------------------------------------------------------------------

def one_download_dir(d: Path, version: str, legacy: bool = True, launcher_in_game: bytes | None = b"the launcher") -> Path:
    """package.sh's output in the one-download layout: the game's package with the launcher's executable in it, and with `legacy`
    the launcher-only archives holding the launcher"""
    d.mkdir(parents=True, exist_ok=True)
    sums = ""
    for kind in ("game", "launcher") if legacy else ("game",):
        for platform in ("linux-x64", "windows-x64"):
            n = make_manifest.asset_name(kind, version, platform)
            ext = ".x86_64" if platform.startswith("linux") else ".exe"
            files = {"OpenBFMELauncher" + ext: b"the launcher"}
            if kind == "game":
                files = {"OpenBFME" + ext: b"the game"} | ({"OpenBFMELauncher" + ext: launcher_in_game} if launcher_in_game else {})
            data = tiny_package(n, files)
            (d / n).write_bytes(data)
            sums += f"{hashlib.sha256(data).hexdigest()}  {n}\n"
    (d / f"SHA256SUMS-{version}.txt").write_text(sums)
    return d


def test_the_manifest_of_the_one_download_layout(tmp_path):
    """format 2 names the package and the launcher archive of each platform and insists on ONE launcher per release; format 3 names
    the packages alone, each with its launcher inside, and refuses a folder that still holds launcher archives"""
    tool = HERE / "make_manifest.py"
    common = ("--repo", "Test/repo", "--commit", COMMIT, "--date", "2026-10-13", "--allow-untagged")
    d = one_download_dir(tmp_path / "two", "v0.5.0")
    assert run(sys.executable, tool, d, *common).returncode == 0
    m = json.loads((d / "manifest.json").read_bytes())
    assert m["format"] == 2 and sorted(m["assets"]) == ["game", "launcher"]
    assert m["assets"]["game"]["linux-x64"]["files"]["OpenBFMELauncher.x86_64"] == m["assets"]["launcher"]["linux-x64"]["files"]["OpenBFMELauncher.x86_64"]
    assert run(sys.executable, tool, "--check", d).returncode == 0
    r = run(sys.executable, tool, d, *common, "--format", "3")
    assert r.returncode != 0 and "format 3 names no launcher archive, but" in r.stderr + r.stdout
    other = one_download_dir(tmp_path / "other", "v0.5.0", launcher_in_game=b"another launcher")
    r = run(sys.executable, tool, other, *common)
    assert r.returncode != 0 and "OpenBFMELauncher.x86_64 in openbfme-v0.5.0-linux-x64.tar.gz is not the one in openbfme-launcher-v0.5.0-linux-x64.tar.gz" in r.stderr + r.stdout

    d3 = one_download_dir(tmp_path / "three", "v0.5.0", legacy=False)
    r = run(sys.executable, tool, d3, *common)
    assert r.returncode != 0 and "openbfme-launcher-v0.5.0-linux-x64.tar.gz is missing" in r.stderr + r.stdout, "format 2 needs the launcher archives"
    assert run(sys.executable, tool, d3, *common, "--format", "3").returncode == 0
    m3 = json.loads((d3 / "manifest.json").read_bytes())
    assert m3["format"] == 3 and sorted(m3["assets"]) == ["game"] and "OpenBFMELauncher.exe" in m3["assets"]["game"]["windows-x64"]["files"]
    r = run(sys.executable, tool, "--check", d3)
    assert r.returncode == 0 and "matches 2 archives" in r.stdout, r.stdout + r.stderr
    bare = one_download_dir(tmp_path / "bare", "v0.5.0", legacy=False, launcher_in_game=None)
    r = run(sys.executable, tool, bare, *common, "--format", "3")
    assert r.returncode != 0 and "holds no OpenBFMELauncher.x86_64: a format 3 release has no other launcher" in r.stderr + r.stdout
    (d3 / "manifest.json").write_bytes((d3 / "manifest.json").read_bytes().replace(b'"format": 3', b'"format": 4'))
    r = run(sys.executable, tool, "--check", d3)
    assert r.returncode != 0 and "has the format 4 (2 or 3)" in r.stdout


@pytest.mark.parametrize("legacy, archives", [(True, 4), (False, 2)])
def test_the_release_assets_follow_the_manifest(tmp_path, legacy, archives):
    """release_assets.py --names (publish_release.sh's upload list) and its read-back check: the archives the manifest names, the
    checksum file, the manifest and its signature; a launcher archive on a format 3 release is foreign"""
    import release_assets
    v = "v0.5.0"
    d = one_download_dir(tmp_path / "pkg", v, legacy=legacy)
    assert run(sys.executable, HERE / "make_manifest.py", d, "--repo", "Test/repo", "--commit", COMMIT, "--date", "2026-10-13",
               "--allow-untagged", "--format", "2" if legacy else "3").returncode == 0
    (d / "manifest.json.sig").write_bytes(b"s" * 64)
    r = run(sys.executable, HERE / "release_assets.py", "--names", "--dir", d, "--version", v)
    names = r.stdout.split()
    assert r.returncode == 0 and len(names) == archives + 3 and names[-3:] == [f"SHA256SUMS-{v}.txt", "manifest.json", "manifest.json.sig"], r.stdout
    assert [n for n in names if n.startswith("openbfme-launcher-")] == ([f"openbfme-launcher-{v}-linux-x64.tar.gz",
                                                                       f"openbfme-launcher-{v}-windows-x64.zip"] if legacy else [])
    want = release_assets.expected(d, v)
    release = {"tag_name": v, "draft": False, "prerelease": False, "assets": [
        {"name": n, "state": "uploaded", "size": s, "digest": f"sha256:{h}", "id": i} for i, (n, (s, h)) in enumerate(want.items())]}
    assert release_assets.check(release, want, v, False, False, "Test/repo") == []
    stray = f"openbfme-launcher-{v}-windows-x64.zip" if not legacy else "openbfme-extra.zip"
    release["assets"].append({"name": stray, "state": "uploaded", "size": 1, "digest": "sha256:" + "0" * 64, "id": 99})
    assert release_assets.check(release, want, v, False, False, "Test/repo") == [f"an asset that is not the release's: {stray}"]


def test_publish_dry_run_uploads_the_manifests_archives(tmp_path):
    """the dry run of a format 3 release names two archives and no launcher archive; a checksum file that lists another set stops it"""
    if os.name == "nt":
        pytest.skip("intrinsically POSIX: production Bash publish dry-run")
    d = one_download_dir(tmp_path / "pkg", "v0.5.0", legacy=False)
    secret = os.urandom(32)
    pub = tmp_path / "key.pub"
    pub.write_text(ed25519.public_key(secret).hex() + "\n")
    assert run(sys.executable, HERE / "make_manifest.py", d, "--repo", "Test/repo", "--commit", COMMIT, "--date", "2026-10-13",
               "--allow-untagged", "--format", "3").returncode == 0
    (d / "manifest.json.sig").write_bytes(ed25519.sign(secret, (d / "manifest.json").read_bytes()))
    r = run("bash", HERE / "publish_release.sh", d, "--repo", "Test/repo", "--key-file", pub, "--allow-untagged")
    assert r.returncode == 0 and "DRY RUN" in r.stdout, r.stdout + r.stderr
    upload = next(ln for ln in r.stdout.splitlines() if " release upload " in ln)
    assert upload.count("openbfme-v0.5.0-") == 2 and "openbfme-launcher-" not in upload and upload.rstrip().endswith("manifest.json.sig"), upload
    sums = d / "SHA256SUMS-v0.5.0.txt"
    sums.write_text(sums.read_text().splitlines()[0] + "\n")
    r = run("bash", HERE / "publish_release.sh", d, "--repo", "Test/repo", "--key-file", pub, "--allow-untagged")
    assert r.returncode != 0 and "PUBLISH FAIL" in r.stderr, r.stdout + r.stderr


def test_the_notes_say_which_file_to_download():
    """one linked archive per platform, the remaining files in one line; the links are built from the checked version and repository
    alone, and anything else is refused (a version or repository with Markdown or a URL in it never reaches the page)"""
    import autorelease
    lines = autorelease.download_section("v0.4.0-alpha.5", "Open-BFME/openbfme-godot")
    text = "\n".join(lines)
    base = "https://github.com/Open-BFME/openbfme-godot/releases/download/v0.4.0-alpha.5"
    assert lines[0] == "## Which file to download"
    assert f"| **[openbfme-v0.4.0-alpha.5-windows-x64.zip]({base}/openbfme-v0.4.0-alpha.5-windows-x64.zip)** | **OpenBFME for Windows.** " in text
    assert f"| **[openbfme-v0.4.0-alpha.5-linux-x64.tar.gz]({base}/openbfme-v0.4.0-alpha.5-linux-x64.tar.gz)** | **OpenBFME for Linux.** " in text
    assert text.count("https://") == 2 and "(link removed)" not in text
    rest = next(ln for ln in lines if ln.startswith("| openbfme-launcher-"))
    assert rest == ("| openbfme-launcher-v0.4.0-alpha.5-windows-x64.zip, openbfme-launcher-v0.4.0-alpha.5-linux-x64.tar.gz, "
                    "SHA256SUMS-v0.4.0-alpha.5.txt, manifest.json, manifest.json.sig | Used by OpenBFME to update itself and for checking "
                    "downloads. You don't need these. |")
    single = "\n".join(autorelease.download_section("v0.4.0-alpha.5", "Open-BFME/openbfme-godot", legacy_launchers=False))
    assert "openbfme-launcher-" not in single and "| SHA256SUMS-v0.4.0-alpha.5.txt, manifest.json, manifest.json.sig | Used by" in single
    for version, repo in (("v0.4.0-alpha.5](https://evil.example)", "Open-BFME/openbfme-godot"), ("v0.4.0", "Open-BFME/x](https://evil.example)"),
                          ("v0.4.0", "evil.example/a/b"), ("0.4.0", "Open-BFME/openbfme-godot")):
        with pytest.raises(SystemExit):
            autorelease.download_section(version, repo)
    # the notes without a repository argument use the default repository (NOTES-1): the table and the sentence are both there
    notes, _ = autorelease.release_notes("v0.4.0-alpha.5", [], [])
    assert "Which file to download" in notes and "Install or update it with the OpenBFME Launcher." in notes
    full, _ = autorelease.release_notes("v0.4.0-alpha.5", [{"subject": "x", "body": "Release-note: See https://evil.example now", "files": []}], [],
                                        repo="Open-BFME/openbfme-godot")
    assert full.count("https://") == 2 and "evil.example" not in full and "(link removed)" in full, "commit text still loses its links"
    assert autorelease.public_text_problems("notes.md", full) == []


def test_make_archive_extracts_one_file(tmp_path):
    """package.sh takes the launcher's executable out of the launcher archive for the combined package: exactly that file's bytes"""
    package_script = (HERE / "package.sh").read_text()
    build_script = (HERE / "build_launcher.sh").read_text()
    assert 'if [ "$p" = macos ]; then launcher_extract_out="$dir"; else launcher_extract_out="$dir/$lexe"; fi' in package_script
    assert 'chmod 0755 "$dir/$lexe" "$dir/$lexe/Contents/MacOS/OpenBFME Launcher"' in package_script
    assert 'chmod 0755 "$dir/$lexe"' in package_script
    assert 'chmod 0755 "$dir" "$dir/$exe" "$LAUNCHER_BINARY"' in build_script
    assert 'chmod 0755 "$dir" "$dir/$exe"' in build_script
    assert all(
        "|| true" not in line and "2>/dev/null" not in line
        for script in (package_script, build_script)
        for line in script.splitlines()
        if "chmod " in line
    )
    for name, lexe in (
        ("openbfme-launcher-v1-linux-x64.tar.gz", "OpenBFMELauncher.x86_64"),
        ("openbfme-launcher-v1-windows-x64.zip", "OpenBFMELauncher.exe"),
    ):
        (tmp_path / name).write_bytes(tiny_package(name, {lexe: b"the launcher \x00\xff", "README.txt": b"read me\n"}))
        out = tmp_path / name.replace(".", "-")
        out.mkdir()
        executable = out / lexe
        assert make_archive.extract(tmp_path / name, lexe, executable) == 0
        assert executable.read_bytes() == b"the launcher \x00\xff"
        assert make_archive.extract(tmp_path / name, "missing.txt", tmp_path / "none.bin") == 1 and not (tmp_path / "none.bin").exists()
        assert make_archive.extract(tmp_path / name, "../" + name, tmp_path / "none.bin") == 1 and not (tmp_path / "none.bin").exists()
        # --has: how package.sh --verify tells a combined package from the game alone
        assert make_archive.has(tmp_path / name, lexe) and not make_archive.has(tmp_path / name, "OpenBFME.x86_64")
        assert run(sys.executable, HERE / "make_archive.py", "--has", tmp_path / name, "README.txt").returncode == 0
        assert run(sys.executable, HERE / "make_archive.py", "--has", tmp_path / name, "OpenBFME.pck").returncode == 1


def test_make_archive_extracts_macos_launcher_subtree(tmp_path):
    folder = tmp_path / "openbfme-v1-macos-universal"
    executable = folder / "OpenBFMELauncher.app/Contents/MacOS/OpenBFME Launcher"
    executable.parent.mkdir(parents=True)
    executable.write_bytes(b"launcher app executable")
    executable.chmod(0o755)
    archive = tmp_path / "openbfme-v1-macos-universal.zip"
    make_archive.zip_file(folder, archive, 1_700_000_000)

    unpacked = tmp_path / "unpacked"
    assert make_archive.extract(archive, "OpenBFMELauncher.app", unpacked) == 0
    assert (unpacked / "OpenBFMELauncher.app/Contents/MacOS/OpenBFME Launcher").read_bytes() == b"launcher app executable"


# ---- lane ONEZIP-1 r3 (Sol r2): the seal tool reads only plain archives --------------------------------------------------------------

def _hostile_tar(path: Path, top: str, members: list) -> None:
    import io
    import tarfile
    with tarfile.open(path, "w:gz", format=tarfile.GNU_FORMAT) as t:
        d = tarfile.TarInfo(top)
        d.type = tarfile.DIRTYPE
        t.addfile(d)
        for name, kind, data in members:
            info = tarfile.TarInfo(name)
            if kind == "file":
                info.size = len(data)
                t.addfile(info, io.BytesIO(data))
            else:
                info.type = {"symlink": tarfile.SYMTYPE, "hardlink": tarfile.LNKTYPE, "device": tarfile.CHRTYPE,
                             "fifo": tarfile.FIFOTYPE, "dir": tarfile.DIRTYPE}[kind]
                info.linkname = data.decode() if kind in ("symlink", "hardlink") else ""
                t.addfile(info)


def _hostile_zip(path: Path, members: list) -> None:
    import zipfile
    with zipfile.ZipFile(path, "w") as z:
        for name, kind, data in members:
            info = zipfile.ZipInfo(name)
            info.filename = name
            info.orig_filename = name
            info.create_system = 3
            info.external_attr = ((0o120777 if kind == "symlink" else 0o100644) << 16)
            z.writestr(info, data)


@pytest.mark.parametrize("fmt", ["tar.gz", "zip"])
@pytest.mark.parametrize("extra, said", [
    (lambda d, top, out: [(f"{top}//{out}/escaped.txt", "file", b"x")], "an absolute path"),
    (lambda d, top, out: [(f"{top}/../escaped.txt", "file", b"x")], "a '..' path component"),
    (lambda d, top, out: [(f"{top}/a/../../escaped.txt", "file", b"x")], "a '..' path component"),
    (lambda d, top, out: [(f"{top}/./x", "file", b"x")], "a '.' path component"),
    (lambda d, top, out: [(f"{top}/C:x", "file", b"x")], "an absolute path"),   # a drive letter
    (lambda d, top, out: [(f"{top}/ab:c", "file", b"x")], "a backslash, a drive letter or a character Windows refuses"),
    (lambda d, top, out: [(f"{top}/a\\b", "file", b"x")], "a backslash, a drive letter or a character Windows refuses"),
    (lambda d, top, out: [(f"{top}/VERSION", "file", b"again")], "appears twice"),
    (lambda d, top, out: [(f"{top}/version", "file", b"again")], "appears twice"),
    (lambda d, top, out: [(f"{top}/link", "symlink", str(out).encode())], "a link"),
    (lambda d, top, out: [(f"elsewhere/x", "file", b"x")], "not inside the package folder"),
    (lambda d, top, out: [(f"{top}/VERSION/x", "file", b"x")], "lies below a member that is a file"),
])
def test_the_seal_tool_refuses_a_hostile_archive_before_writing_anything(tmp_path, fmt, extra, said):
    """seal_package.py unseal / seal read archives before any signature check (package.sh --verify unseals first): every member is
    checked first, and a member that is not a plain file below the package folder refuses the whole archive by name; nothing is
    written, inside or outside the staging folder"""
    import seal_package
    out = tmp_path / "outside"
    out.mkdir()
    plat = "linux-x64" if fmt == "tar.gz" else "windows-x64"
    top = f"openbfme-v1.0.0-{plat}"
    members = [(f"{top}/VERSION", "file", b"OpenBFME v1.0.0\n"), (f"{top}/game", "file", b"game")] + extra(tmp_path, top, out)
    arc = tmp_path / f"{top}.{fmt}"
    if fmt == "tar.gz":
        _hostile_tar(arc, top, members)
    else:
        _hostile_zip(arc, members)
        if any("\\" in name for name, _, _ in members):
            with zipfile.ZipFile(arc) as z:
                assert z.infolist()[-1].orig_filename == members[-1][0]
    with pytest.raises(SystemExit, match=said.replace("(", r"\(").replace(")", r"\)")):
        seal_package.unseal(arc, tmp_path / "unsealed.out")
    with pytest.raises(SystemExit, match=said.replace("(", r"\(").replace(")", r"\)")):
        seal_package.seal_archive(arc, os.urandom(32), "Test/repo")
    assert list(out.iterdir()) == [] and not (tmp_path / "escaped.txt").exists() and not (tmp_path / "unsealed.out").exists()


@pytest.mark.parametrize("kind", ["hardlink", "device", "fifo", "dir"])
def test_the_seal_tool_refuses_special_tar_members(tmp_path, kind):
    import seal_package
    top = "openbfme-v1.0.0-linux-x64"
    arc = tmp_path / f"{top}.tar.gz"
    _hostile_tar(arc, top, [(f"{top}/VERSION", "file", b"OpenBFME v1.0.0\n"), (f"{top}/special", kind, b"VERSION")])
    with pytest.raises(SystemExit, match="a link, folder, device or other special member"):
        seal_package.unseal(arc, tmp_path / "x.tar.gz")


def test_the_seal_tool_counts_tar_members_before_keeping_them_and_checks_the_root_entry(tmp_path):
    """Sol r3 of ONEZIP-1: a tar with no root header and 10,001 files was accepted against the 10,000 cap, duplicate root entries passed,
    and the count guards ran after all the metadata was allocated. The members are streamed and counted first: the 10,001st file
    refuses the archive having kept at most the cap's worth of headers (measured), and a second root entry refuses by name."""
    import io
    import tarfile
    import tracemalloc

    import seal_package
    top = "openbfme-v1.0.0-linux-x64"

    def make(path: Path, files: int, roots: int) -> None:
        with tarfile.open(path, "w:gz") as t:
            for _ in range(roots):
                d = tarfile.TarInfo(top)
                d.type = tarfile.DIRTYPE
                t.addfile(d)
            for i in range(files):
                f = tarfile.TarInfo(f"{top}/f{i}")
                t.addfile(f, io.BytesIO(b""))

    a = tmp_path / f"{top}.tar.gz"
    make(a, 10001, 0)
    tracemalloc.start()
    with pytest.raises(SystemExit, match="more than 10000 files"):
        seal_package.read_archive(a)
    peak = tracemalloc.get_traced_memory()[1]
    tracemalloc.stop()
    assert peak < 16 << 20, f"{peak} bytes held (the cap's worth of headers is about 6 MB)"   # tolerance: 10,000 kept TarInfos
    make(a, 3, 2)
    with pytest.raises(SystemExit, match="the package folder appears twice"):
        seal_package.read_archive(a)
    make(a, 10000, 1)   # exactly the cap, with its root entry: accepted
    assert len(seal_package.read_archive(a)[0]) == 10000
    make(a, 10001, 1)
    with pytest.raises(SystemExit, match="more than 10000 (members|files)"):
        seal_package.read_archive(a)


def test_the_seal_tool_refuses_a_huge_zip_directory_before_allocating_for_it(tmp_path):
    """Sol r4 of ONEZIP-1: a zip of 100,000 empty files (14 MB) allocated 63 MB before the member cap refused it. The end-of-central-
    directory record is read first: the entry count and the directory's size refuse it by name with bounded memory; a malformed
    record refuses by name; exactly 10,000 files are still accepted."""
    import tracemalloc
    import zipfile

    import seal_package
    top = "openbfme-v1.0.0-windows-x64"

    def make(path: Path, files: int) -> None:
        with zipfile.ZipFile(path, "w") as z:
            for i in range(files):
                info = zipfile.ZipInfo(f"{top}/f{i}")
                info.create_system = 3
                info.external_attr = 0o100644 << 16
                z.writestr(info, b"")

    a = tmp_path / f"{top}.zip"
    make(a, 100000)
    tracemalloc.start()
    with pytest.raises(SystemExit, match=r"more than 10000 members \(100000 entries in the central directory\)"):
        seal_package.read_archive(a)
    peak = tracemalloc.get_traced_memory()[1]
    tracemalloc.stop()
    assert peak < 1 << 20, f"{peak} bytes allocated before the refusal"   # tolerance: the 64 KiB tail read and bookkeeping; was 63 MB
    make(a, 10000)
    assert len(seal_package.read_archive(a)[0]) == 10000
    make(a, 3)
    raw = a.read_bytes()
    for name, data, said in (("no record", b"PK\x03\x04" + b"x" * 40, "no zip end-of-central-directory record"),
                             ("comment", raw[:-2] + b"\x09\x00", "comment length disagrees"),
                             ("counts", raw[:-14] + b"\x09" + raw[-13:], "entry counts disagree"),
                             ("offset", raw[:-6] + b"\xff\xff\xff\x7f" + raw[-2:], "beyond the end of the file")):
        b = tmp_path / f"{top}-{name.replace(' ', '')}.zip"
        b.write_bytes(data)
        with pytest.raises(SystemExit, match=said):
            seal_package.read_archive(b.replace(tmp_path / f"{top}.zip"))
    big = tmp_path / f"{top}.zip"   # a directory of 3 MB: 5,000 entries with 600-byte comments each
    with zipfile.ZipFile(big, "w") as z:
        for i in range(5000):
            info = zipfile.ZipInfo(f"{top}/f{i}")
            info.comment = b"c" * 600
            z.writestr(info, b"")
    with pytest.raises(SystemExit, match="the central directory is [0-9]+ bytes \\(at most 2097152\\)"):
        seal_package.read_archive(big)


def test_onlinemenu1_the_launcher_account_page_talks_only_to_the_named_lobby():
    """lane ONLINEMENU-1 (SOCIAL-2b, PR #8): the launcher's account files are allowed their findings by name (net_guard.ALLOWED_LAUNCHER); what
    makes that safe is pinned here: every lobby request goes to the lobby NetPolicy accepted, with no redirects; the browser opens only an
    allow-listed Discord authorize URL or that lobby's own e-mail page; the OAuth callback listens on 127.0.0.1; social_identity.gd's MachineGuid read is pinned in net_guard"""
    import net_guard  # noqa: PLC0415
    oauth = (REPO / "launcher/scripts/core/oauth_account.gd").read_text()
    assert oauth.count("_http.request(") == 1 and oauth.count("_http.max_redirects = 0") == 1 and ".max_redirects = " not in oauth.replace("_http.max_redirects = 0", "")
    assert "_policy.set_lobby(url) != \"\"" in oauth and 'func _url(path: String) -> String:\n\treturn lobby_url.trim_suffix("/") + path' in oauth
    assert re.findall(r"(?<!func )(?<![\w.])_request\((.*?),", oauth) == ['_url("/v1/auth/oauth/accounts")', '_url("/v1/auth/email/poll")', "_url(path)"]
    assert oauth.count(".listen(") == 1 and '_server.listen(CALLBACK_PORT, "127.0.0.1")' in oauth
    assert "OS.shell_open(" not in oauth and oauth.count('.open_url(') == 2
    first, second = oauth.index('.open_url(value.authorization_url)'), oauth.index('.open_url(value.url)')
    # each open follows its URL check in the same response branch
    start, email = oauth.rindex('elif _request_kind == "start":', 0, first), oauth.rindex('elif _request_kind == "email_start":', 0, second)
    assert "if not _allowed_authorization(value.authorization_url):" in oauth[start:first]
    assert 'value.get("url") != lobby_url + "/email/" + str(ticket)' in oauth[email:second]
    assert 'var prefix := "https://discord.com/oauth2/authorize?"' in oauth
    social = (REPO / "launcher/scripts/net/social_client.gd").read_text()
    assert social.count(".request(") == 1 and social.count("request.request(url,") == 1 and "request.max_redirects = 0" in social
    send = social[social.index("func _send_request("):social.index("request.request(url,")]
    assert "var url := lobby_url + path" in send and "parsed.origin != lobby_url" in send and "TLSOptions.client()" in send
    # social_identity.gd's one `reg query` is the exact MachineGuid read (net_guard.MACHINE_GUID_QUERY), not an allowance of the file
    assert "launcher/scripts/core/social_identity.gd" not in net_guard.ALLOWED_LAUNCHER
    assert "launcher/scripts/core/social_identity.gd" not in net_guard.scan_launcher()
    assert not net_guard.launcher_finding_allowed("launcher/scripts/core/oauth_account.gd", "line 1: WebSocketPeer")
    assert not net_guard.launcher_finding_allowed("launcher/scripts/net/social_client.gd", "line 1: TCPServer")
