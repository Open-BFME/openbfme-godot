"""Update transaction boundaries and failures, using the launcher's real signed-release fixtures."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_launcher as base  # noqa: E402

pytestmark = base.pytestmark
server = base.server
keys = base.keys
imported = base.imported
_remove_tmp_path = base._remove_tmp_path
launcher_builds = base.launcher_builds

OLD = "v0.1.0"
NEW = "v0.2.0"
GAME_POINTS = ["install-checked", "manifest-verified", "asset-downloading", "asset-downloaded", "asset-verified",
               "install-unpacking", "install-extracted", "install-unpacked", "install-placed", "install-committed",
               "install-selected", "install-pruned"]


@pytest.fixture(scope="module")
def full_disk_writer(tmp_path_factory):
    """Redirect only the selected regular file's descriptor, after Godot's regular-file check, to /dev/full."""
    if sys.platform != "linux" or not shutil.which("cc"):
        pytest.skip("the ENOSPC fixture needs Linux /dev/full and a C compiler")
    root = tmp_path_factory.mktemp("full-disk-writer")
    source = root / "full.c"
    source.write_text(r'''
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void full(FILE *stream, const char *phase) {
    const char *target = getenv("OPENBFME_TEST_FULL_PATH");
    const char *when = getenv("OPENBFME_TEST_FULL_PHASE");
    if (!target || !when || !stream || strcmp(when, phase)) return;
    char descriptor[64], path[4096];
    int fd = fileno(stream);
    snprintf(descriptor, sizeof descriptor, "/proc/self/fd/%d", fd);
    ssize_t n = readlink(descriptor, path, sizeof path - 1);
    if (n < 0) return;
    path[n] = 0;
    if (strcmp(path, target)) return;
    int device = open("/dev/full", O_WRONLY);
    if (device < 0 || dup2(device, fd) < 0) abort();
    close(device);
    const char message[] = "FAULT kernel ENOSPC redirected\n";
    if (write(STDERR_FILENO, message, sizeof message - 1) < 0) abort();
}

size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream) {
    size_t (*next)(const void *, size_t, size_t, FILE *) = dlsym(RTLD_NEXT, "fwrite");
    full(stream, "write");
    return next(ptr, size, count, stream);
}

int fflush(FILE *stream) {
    int (*next)(FILE *) = dlsym(RTLD_NEXT, "fflush");
    full(stream, "flush");
    return next(stream);
}
''')
    library = root / "full.so"
    subprocess.run(["cc", "-shared", "-fPIC", "-O2", "-Wall", "-Wextra", "-Werror", str(source), "-ldl", "-o", str(library)], check=True)
    return library


def full_environment(library: Path, path: Path, phase: str = "flush") -> dict[str, str]:
    return {"LD_PRELOAD": str(library), "OPENBFME_TEST_FULL_PATH": str(path), "OPENBFME_TEST_FULL_PHASE": phase}


def publish(tmp: Path, server, keys, tag: str = OLD) -> dict[str, bytes]:
    payload = hashlib.shake_256(tag.encode()).digest(300_000)
    files = base.release_files(tmp, tag, keys["release"], game=base.game_tar(tmp, tag, extra=payload))
    server.publish(tag, files)
    return files


def inventory(tmp: Path, keys, tag: str) -> dict[str, bytes]:
    root = base.data(tmp) / "versions" / tag
    raw = (root / "manifest.json").read_bytes()
    assert base.ed25519.verify(base.ed25519.public_key(keys["release"]), raw, (root / "manifest.json.sig").read_bytes())
    manifest = json.loads(raw)
    assert manifest["version"] == tag
    signed = manifest["assets"]["game"][base.PLATFORM]["files"]
    actual = {p.relative_to(root / "game").as_posix(): p.read_bytes() for p in (root / "game").rglob("*") if p.is_file()}
    assert set(actual) == set(signed)
    for name, content in actual.items():
        assert {"size": len(content), "sha256": hashlib.sha256(content).hexdigest()} == signed[name]
    return actual


@pytest.mark.skipif(base.WINDOWS, reason="new crash matrix uses Linux dead-owner recovery; Windows lease expiry has its own tests")
@pytest.mark.parametrize("point", GAME_POINTS)
@pytest.mark.parametrize("reinstall", [False, True], ids=["new-version", "reinstall"])
def test_game_update_killed_at_transaction_boundaries(tmp_path, server, keys, point, reinstall):
    publish(tmp_path, server, keys)
    assert base.launch(tmp_path, server, keys["release"], "--update").code == 0
    original = inventory(tmp_path, keys, OLD)
    tag = OLD if reinstall else NEW
    if not reinstall:
        publish(tmp_path, server, keys, tag)
    action = f"--install={tag}" if reinstall else "--update"
    r = base.launch(tmp_path, server, keys["release"], action, f"--test-kill-at={point}")
    assert r.code != 0, r
    assert f"LAUNCHER test kill at {point}" in (base.data(tmp_path) / "launcher.log").read_text()
    if point == "asset-downloading":
        part = base.data(tmp_path) / "downloads" / (base.asset_name("game", tag) + ".part")
        assert 0 < part.stat().st_size < len(server.files[f"/dl/{tag}/{base.asset_name('game', tag)}"])
    recovered = base.launch(tmp_path, None, keys["release"], "--list", "--play")
    assert recovered.code == 0 and recovered.has("play v0."), recovered
    assert inventory(tmp_path, keys, OLD) == original
    for installed in base.installed(tmp_path):
        inventory(tmp_path, keys, installed)
    assert not list((base.data(tmp_path) / "versions").glob("*.partial"))
    assert not list((base.data(tmp_path) / "versions").glob("*.old"))
    assert not (base.data(tmp_path) / "versions.lock").exists()
    r = base.launch(tmp_path, server, keys["release"], action, "--play")
    assert r.code == 0 and r.has(f"play {tag}"), r
    inventory(tmp_path, keys, tag)


@pytest.mark.parametrize("damage", ["missing", "hash", "size", "signature", "extra", "valid"])
def test_recovery_verifies_replacement_before_discarding_backup(tmp_path, server, keys, damage):
    publish(tmp_path, server, keys)
    assert base.launch(tmp_path, server, keys["release"], "--update").code == 0
    original = inventory(tmp_path, keys, OLD)
    root = base.data(tmp_path) / "versions" / OLD
    shutil.copytree(root, root.with_name(OLD + ".old"))
    root.with_name(OLD + ".partial").mkdir()
    pack = root / "game/OpenBFME.pck"
    if damage == "missing":
        shutil.rmtree(root)
    elif damage == "hash":
        content = pack.read_bytes()
        pack.write_bytes(bytes([content[0] ^ 1]) + content[1:])
    elif damage == "size":
        pack.write_bytes(b"short")
    elif damage == "signature":
        (root / "manifest.json.sig").write_bytes(bytes(64))
    elif damage == "extra":
        (root / "game/stray.txt").write_text("not signed")
    r = base.launch(tmp_path, None, keys["release"], "--play")
    assert r.code == 0 and r.has(f"play {OLD}"), r
    assert r.has("finished replacing" if damage == "valid" else "restored"), r
    assert inventory(tmp_path, keys, OLD) == original
    assert base.installed(tmp_path) == [OLD]


def test_recovery_preserves_evidence_when_neither_copy_verifies(tmp_path, server, keys):
    publish(tmp_path, server, keys)
    assert base.launch(tmp_path, server, keys["release"], "--update").code == 0
    root = base.data(tmp_path) / "versions" / OLD
    backup = root.with_name(OLD + ".old")
    shutil.copytree(root, backup)
    for copy in (root, backup):
        (copy / "manifest.json.sig").write_bytes(bytes(64))
    r = base.launch(tmp_path, None, keys["release"], "--play")
    assert r.code == 1 and r.has("neither copy verifies") and not r.has(f"play {OLD}"), r
    assert root.is_dir() and backup.is_dir()


@pytest.mark.parametrize("signature", ["missing", "empty", "truncated"])
def test_unsigned_update_never_downloads_or_replaces_the_game(tmp_path, server, keys, signature):
    publish(tmp_path, server, keys)
    assert base.launch(tmp_path, server, keys["release"], "--update").code == 0
    original = inventory(tmp_path, keys, OLD)
    files = base.release_files(tmp_path, NEW, keys["release"])
    if signature == "missing":
        del files["manifest.json.sig"]
    else:
        files["manifest.json.sig"] = files["manifest.json.sig"][:0 if signature == "empty" else 63]
    server.publish(NEW, files)
    r = base.launch(tmp_path, server, keys["release"], "--update", "--play")
    assert r.code == 3 and r.has("update rejected") and r.has(f"play {OLD}"), r
    assert not any(p == f"/dl/{NEW}/{base.asset_name('game', NEW)}" for p, _ in server.requests)
    assert base.installed(tmp_path) == [OLD] and inventory(tmp_path, keys, OLD) == original


@pytest.mark.parametrize("damage", ["size", "hash", "missing", "extra"])
def test_signed_archive_must_match_its_entire_signed_inventory(tmp_path, server, keys, damage):
    publish(tmp_path, server, keys)
    assert base.launch(tmp_path, server, keys["release"], "--update").code == 0
    files = base.release_files(tmp_path, NEW, keys["release"])
    manifest = json.loads(files["manifest.json"])
    signed = manifest["assets"]["game"][base.PLATFORM]["files"]
    if damage == "size":
        signed["OpenBFME.pck"]["size"] += 1
    elif damage == "hash":
        signed["OpenBFME.pck"]["sha256"] = "0" * 64
    elif damage == "missing":
        signed["absent.txt"] = {"size": 1, "sha256": hashlib.sha256(b"x").hexdigest()}
    else:
        del signed["OpenBFME.pck"]
    files["manifest.json"] = base.make_manifest.dumps(manifest)
    files["manifest.json.sig"] = base.ed25519.sign(keys["release"], files["manifest.json"])
    server.publish(NEW, files)
    r = base.launch(tmp_path, server, keys["release"], "--update", "--play")
    assert r.code == 3 and r.has(f"{NEW} was not installed") and r.has(f"play {OLD}"), r
    assert base.installed(tmp_path) == [OLD]
    inventory(tmp_path, keys, OLD)


@pytest.mark.parametrize("damage", ["hash", "oversize", "stale-metadata", "missing-metadata", "range-416"])
def test_corrupt_partial_is_discarded_and_retry_installs_only_verified_bytes(tmp_path, server, keys, damage):
    publish(tmp_path, server, keys)
    assert base.launch(tmp_path, server, keys["release"], "--update").code == 0
    files = publish(tmp_path, server, keys, NEW)
    name = base.asset_name("game", NEW)
    path = f"/dl/{NEW}/{name}"
    server.truncate[path] = 65536
    assert base.launch(tmp_path, server, keys["release"], "--update").code == 3
    part = base.data(tmp_path) / "downloads" / (name + ".part")
    meta = part.with_name(part.name + ".json")
    assert part.stat().st_size == 65536
    if damage == "hash":
        part.write_bytes(bytes([part.read_bytes()[0] ^ 1]) + part.read_bytes()[1:])
    elif damage == "oversize":
        part.write_bytes(files[name] + b"too long")
    elif damage == "stale-metadata":
        meta.write_text('{"sha256":"stale","size":1}')
    elif damage == "missing-metadata":
        meta.unlink()
    else:
        server.status[path] = 416
    server.requests.clear()
    r = base.launch(tmp_path, server, keys["release"], "--update", "--play")
    if damage in ("hash", "range-416"):
        assert r.code == 3 and r.has(f"play {OLD}"), r
        assert not part.exists()
        if damage == "range-416":
            del server.status[path]
        r = base.launch(tmp_path, server, keys["release"], "--update", "--play")
    else:
        assert [h.get("range") for p, h in server.requests if p == path] == [None]
    assert r.code == 0 and r.has(f"play {NEW}"), r
    inventory(tmp_path, keys, NEW)
    assert not part.exists() and not meta.exists()


@pytest.mark.parametrize("phase", ["write", "flush"])
def test_disk_full_at_download_is_reported_and_old_install_plays(tmp_path, server, keys, full_disk_writer, phase):
    publish(tmp_path, server, keys)
    assert base.launch(tmp_path, server, keys["release"], "--update").code == 0
    if phase == "write":
        publish(tmp_path, server, keys, NEW)
    else:
        server.publish(NEW, base.release_files(tmp_path, NEW, keys["release"]))
    name = base.asset_name("game", NEW)
    part = base.data(tmp_path) / "downloads" / (name + ".part")
    meta = part.with_name(part.name + ".json")
    try:
        r = base.launch(tmp_path, server, keys["release"], "--update", "--play",
                        env_extra=full_environment(full_disk_writer, part, phase))
        assert r.code == 3 and r.has("disk full") and r.has(f"play {OLD}"), r
        assert "FAULT kernel ENOSPC redirected" in r.out, r
        assert base.installed(tmp_path) == [OLD]
        inventory(tmp_path, keys, OLD)
    finally:
        part.unlink(missing_ok=True)
        meta.unlink(missing_ok=True)
    assert base.launch(tmp_path, server, keys["release"], "--update").code == 0
    inventory(tmp_path, keys, NEW)


@pytest.mark.skipif(os.name != "posix" or not Path("/dev/full").exists(), reason="requires Unix permissions and ENOSPC")
@pytest.mark.parametrize("scenario", ["unpack-permission", "archive-full", "manifest-permission", "manifest-full", "signature-full", "aside-permission",
                                     "swap-permission", "swap-missing-stage"])
def test_storage_failures_keep_or_restore_a_playable_install(tmp_path, server, keys, scenario, full_disk_writer):
    publish(tmp_path, server, keys)
    assert base.launch(tmp_path, server, keys["release"], "--update").code == 0
    original = inventory(tmp_path, keys, OLD)
    reinstall = scenario in ("aside-permission", "swap-permission", "swap-missing-stage")
    tag = OLD if reinstall else NEW
    if not reinstall:
        publish(tmp_path, server, keys, tag)
    cmd = [base.GODOT, "--headless", "--path", str(base.LAUNCHER), "--script", "res://tests/update_fault_test.gd", "--",
           f"--origin={server.origin}", f"--key={base.ed25519.public_key(keys['release']).hex()}", f"--scenario={scenario}",
           f"--tag={tag}", f"--pick={'yes' if reinstall else 'no'}"]
    target = {"archive-full": "game/OpenBFME.pck", "manifest-full": "manifest.json", "signature-full": "manifest.json.sig"}.get(scenario)
    env_extra = full_environment(full_disk_writer, base.data(tmp_path) / "versions" / (tag + ".partial") / target) if target else {}
    try:
        r = subprocess.run(cmd, env=base.isolated_environment(tmp_path, **env_extra), capture_output=True, text=True, timeout=180)
        out = r.stdout + r.stderr
        assert r.returncode == 0 and "SCRIPT ERROR" not in out, out
        result = json.loads(next(line.removeprefix("FAULT RESULT ") for line in r.stdout.splitlines() if line.startswith("FAULT RESULT ")))
        assert result["fired"] and not result["ok"] and result["error"], out
        assert any(word in result["error"] for word in ("cannot create", "writing", "could not be replaced", "could not be moved")), out
        if target:
            assert "disk full" in result["error"] and "FAULT kernel ENOSPC redirected" in out
        if scenario == "swap-permission":
            assert "previous copy is kept" in result["error"] and "restored" in out
    finally:
        for p in (base.data(tmp_path) / "versions").rglob("*"):
            if p.is_dir() and not p.is_symlink():
                p.chmod(0o755)
        (base.data(tmp_path) / "versions").chmod(0o755)
    r = base.launch(tmp_path, None, keys["release"], "--play")
    assert r.code == 0 and r.has(f"play {OLD}"), r
    assert base.installed(tmp_path) == [OLD] and inventory(tmp_path, keys, OLD) == original


def test_channel_round_trip_and_interrupted_download_never_mix_files(tmp_path, server, keys):
    stable, nightly, next_stable = OLD, "v0.2.0-nightly.20261022", NEW
    expected = {}
    for tag, marker in ((stable, "stable-only.txt"), (nightly, "nightly-only.txt"), (next_stable, "stable-new-only.txt")):
        package = base.tiny_package(base.asset_name("game", tag), {
            base.GAME_EXE: base.game_script(), "OpenBFME.pck": hashlib.shake_256(tag.encode()).digest(100_000),
            "VERSION": f"OpenBFME {tag}\n".encode(), marker: tag.encode(),
        })
        expected[tag] = base.release_files(tmp_path, tag, keys["release"], game=package)
    server.publish(stable, expected[stable])
    assert base.launch(tmp_path, server, keys["release"], "--channel=stable", "--update").code == 0
    server.publish(nightly, expected[nightly])
    name = base.asset_name("game", nightly)
    server.truncate[f"/dl/{nightly}/{name}"] = 32768
    r = base.launch(tmp_path, server, keys["release"], "--channel=nightly", "--update", "--play")
    assert r.code == 3 and r.has(f"play {stable}"), r
    server.publish(next_stable, expected[next_stable])
    r = base.launch(tmp_path, server, keys["release"], "--channel=stable", "--update", "--play")
    assert r.code == 0 and r.has(f"play {next_stable}"), r
    assert "nightly-only.txt" not in inventory(tmp_path, keys, next_stable)
    r = base.launch(tmp_path, server, keys["release"], "--channel=nightly", f"--install={nightly}", "--play")
    assert r.code == 0 and r.has(f"play {nightly}") and r.has("resumed"), r
    nightly_files = inventory(tmp_path, keys, nightly)
    assert "nightly-only.txt" in nightly_files and "stable-new-only.txt" not in nightly_files
    r = base.launch(tmp_path, server, keys["release"], "--channel=stable", "--update")
    assert r.code == 0, r
    r = base.launch(tmp_path, None, keys["release"], f"--select={next_stable}", "--play")
    assert r.code == 0 and r.has(f"play {next_stable}"), r
    inventory(tmp_path, keys, next_stable)


@pytest.mark.parametrize("point", ["self-unpacked", "self-staged"])
def test_self_update_staging_interruption_keeps_a_runnable_launcher(tmp_path, server, keys, launcher_builds, point):
    inst = base.unpack_launcher(tmp_path / "inst", launcher_builds["v0.2.0"])
    exe = inst / base.LAUNCHER_EXE
    server.publish("v0.3.0", base.release_files(tmp_path, "v0.3.0", keys["release"], launcher=launcher_builds["v0.3.0"]))
    r = base.launch(tmp_path, server, keys["release"], "--self-update", f"--test-kill-at={point}", exe=exe)
    assert f"LAUNCHER test kill at {point}" in (base.data(tmp_path) / "launcher.log").read_text(), r
    assert hashlib.sha256(exe.read_bytes()).hexdigest() == base.exe_sha(launcher_builds["v0.2.0"])
    r = base.launch(tmp_path, server, keys["release"], "--self-update", exe=exe)
    assert r.code == 0, r
    r = base.launch(tmp_path, server, keys["release"], "--version", exe=exe)
    assert r.code == 0 and "OpenBFME Launcher v0.3.0" in r.out, r
    r = base.launch(tmp_path, server, keys["release"], "--version", exe=exe)
    assert r.code == 0 and "OpenBFME Launcher v0.3.0" in r.out, r
    assert base.leftovers(inst, tmp_path) == []
