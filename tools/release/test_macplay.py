"""Lane MACPLAY-1: the Mac launcher's Play never does nothing.

The v0.4.6 Mac preview's PLAY was dimmed and said nothing when macOS ran the launcher from an App Translocation copy (the read-only copy of
one quarantined app, with no OpenBFME.app beside it). These tests drive the real exported Mac launcher (a test build trusting a throwaway
key, tools/release/test_macos_update.py's export) through its real PLAY button (--ui-play: the button's pressed signal) and assert what
happens in the world: the game process that started (a stand-in game appends its own path to OPENBFME_TEST_PLAYED), the files installed,
their quarantine flags and code seals, and the message the window shows when nothing can start. They run on macOS only (codesign,
xattr, the Darwin swap helper) and skip loudly elsewhere; the seal of the Mac package (release tooling) is tested on every system.
"""
from __future__ import annotations

import hashlib
import json
import os
import plistlib
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import ed25519  # noqa: E402
import make_archive  # noqa: E402
import make_macos_manifest  # noqa: E402
import seal_package  # noqa: E402
import test_macos_update as mu  # noqa: E402
from launcher_test_server import ReleaseServer  # noqa: E402

VERSION = "v1.3.0-preview.1"
REPO = "Test/repo"
TRANSLOCATION_WORDS = "macOS opened a temporary copy of the launcher"


# ---- the seal of the Mac package (any system) -----------------------------------------------------------------------------------------

def _mac_zip(tmp: Path, version: str = "v1.0.0") -> Path:
    top = tmp / f"openbfme-{version}-macos-arm64"
    files = {
        "OpenBFME.app/Contents/MacOS/OpenBFME": (b"\xcf\xfa\xed\xfe game", 0o755),
        "OpenBFME.app/Contents/Info.plist": (b"<plist/>\n", 0o644),
        "OpenBFME Launcher.app/Contents/MacOS/OpenBFME Launcher": (b"\xcf\xfa\xed\xfe launcher", 0o755),
        "OpenBFME Launcher.app/Contents/MacOS/openbfme-bundle-swap": (b"\xcf\xfa\xed\xfe helper", 0o755),
        "VERSION": (f"OpenBFME {version} Mac Unstable Preview (Apple Silicon)\n".encode(), 0o644),
        "NOTICE": (b"notice\n", 0o644),
    }
    for rel, (data, mode) in files.items():
        (top / rel).parent.mkdir(parents=True, exist_ok=True)
        (top / rel).write_bytes(data)
        (top / rel).chmod(mode)
    out = tmp / "pkg"
    out.mkdir()
    archive = out / f"{top.name}.zip"
    make_archive.zip_file(top, archive, 1_700_000_000)
    return archive


def test_mac_package_is_sealed_with_its_modes_and_checked_at_publish(tmp_path: Path):
    """seal_package.py seals openbfme-<v>-macos-arm64.zip (it refused the name before MACPLAY-1, so the Mac package had no signed
    inventory): payload.json names every other file with its size and SHA-256 under the release key, the executables keep their x bit,
    and make_macos_manifest.py's publish check refuses the package unsealed, sealed by another key, or with a file changed"""
    secret, other = bytes(range(32)), bytes(range(1, 33))
    archive = _mac_zip(tmp_path)
    unsealed = archive.read_bytes()
    key = ed25519.public_key(secret).hex()
    assert any("not sealed" in p for p in make_macos_manifest.seal_problems(archive, REPO, key))
    seal_package.seal_archive(archive, secret, REPO)
    assert seal_package.check(archive.parent, ed25519.public_key(secret), REPO) == []
    assert make_macos_manifest.seal_problems(archive, REPO, key) == []
    with zipfile.ZipFile(archive) as z:
        modes = {i.filename.split("/", 1)[1]: (i.external_attr >> 16) & 0o777 for i in z.infolist()}
        payload = json.loads(z.read(f"{archive.stem}/payload.json"))
        files = {i.filename.split("/", 1)[1]: z.read(i) for i in z.infolist()}
    assert modes["OpenBFME.app/Contents/MacOS/OpenBFME"] == 0o755
    assert modes["OpenBFME Launcher.app/Contents/MacOS/OpenBFME Launcher"] == 0o755
    assert modes["NOTICE"] == 0o644 and modes["payload.json"] == 0o644
    # the expected inventory computed here, from the unsealed archive's own members
    with zipfile.ZipFile(__import__("io").BytesIO(unsealed)) as z:
        want = {i.filename.split("/", 1)[1]: {"size": i.file_size, "sha256": hashlib.sha256(z.read(i)).hexdigest()} for i in z.infolist()}
    assert payload["platform"] == "macos-arm64" and payload["version"] == "v1.0.0" and payload["repo"] == REPO
    assert payload["files"] == want and set(files) == set(want) | {"payload.json", "payload.json.sig"}
    assert any("does not verify" in p for p in make_macos_manifest.seal_problems(archive, REPO, ed25519.public_key(other).hex()))
    # a changed file in a sealed package: the inventory no longer names the archive's files
    files["NOTICE"] = b"changed\n"
    top = tmp_path / "re" / archive.stem
    for rel, data in files.items():
        (top / rel).parent.mkdir(parents=True, exist_ok=True)
        (top / rel).write_bytes(data)
    make_archive.zip_file(top, archive, 1_700_000_000)
    assert any("not the inventory" in p for p in make_macos_manifest.seal_problems(archive, REPO, key))


def test_autorelease_seals_the_mac_package_before_its_manifest():
    """autorelease.sh: the Mac ZIP joins the package folder before seal_package.py runs, and manifest-macos.json is made from the sealed
    ZIP's checksum line (the build host's checksum is of the unsealed ZIP)"""
    text = (HERE / "autorelease.sh").read_text(encoding="utf-8")
    copy = text.index('cp "$MACOUT/$MACZIP" "$PKG/$MACZIP"')
    seal = text.index('keyed python3 "$T/seal_package.py" seal')
    manifest = text.index('--archive "$PKG/$MACZIP" --checksum "$WORK/$MACZIP.sealed.sha256"')
    assert text.count('cp "$MACOUT/$MACZIP" "$PKG/$MACZIP"') == 1 and copy < seal < manifest
    assert '--checksum "$MACOUT/$MACZIP.sha256" --out' not in text


def test_launcher_mac_pure_parts(tmp_path: Path):
    """launcher/tests/macplay_test.gd: the window size at Mac scale factors, App Translocation, the package folder of an app bundle in
    folders with spaces, and the macos-arm64 payload inventory (any system with Godot)"""
    godot = os.environ.get("GODOT") or shutil.which("godot")
    if not godot:
        pytest.skip("SKIPPED LOUDLY: Godot is not available for the launcher script test")
    secret = bytes(range(32))
    files = {"OpenBFME.app/Contents/MacOS/OpenBFME": b"game", "OpenBFME Launcher.app/Contents/MacOS/OpenBFME Launcher": b"launcher",
             "VERSION": f"OpenBFME {VERSION} Mac Unstable Preview (Apple Silicon)\n".encode()}
    body = seal_package.payload_bytes(files, VERSION, "macos-arm64", REPO)
    (tmp_path / "p.json").write_bytes(body)
    (tmp_path / "p.sig").write_bytes(ed25519.sign(secret, body))
    home = tmp_path / "home"
    home.mkdir()
    env = dict(os.environ, HOME=str(home), XDG_DATA_HOME=str(home / ".local/share"), XDG_CONFIG_HOME=str(home / ".config"))
    r = subprocess.run([godot, "--headless", "--path", str(HERE.parents[1] / "launcher"), "--script", "res://tests/macplay_test.gd", "--",
                        f"--payload={tmp_path / 'p.json'}", f"--sig={tmp_path / 'p.sig'}", f"--key={ed25519.public_key(secret).hex()}"],
                       env=env, capture_output=True, text=True, timeout=300)
    assert r.returncode == 0 and "MACPLAY UNIT OK" in r.stdout and "MACPLAY UNIT FAIL" not in r.stdout, r.stdout[-3000:] + r.stderr[-2000:]


# ---- the real exported Mac launcher (macOS only) --------------------------------------------------------------------------------------

@pytest.fixture(scope="module")
def mac(tmp_path_factory):
    if sys.platform != "darwin":
        pytest.skip("SKIPPED LOUDLY: the exported Mac launcher tests need macOS (codesign, xattr, the Darwin swap helper)")
    godot = Path(os.environ.get("GODOT", ""))
    if not godot.is_file():
        pytest.skip("SKIPPED LOUDLY: set GODOT to the Godot 4.7.2 editor executable")
    templates = Path(os.environ.get("OPENBFME_MAC_TEMPLATES", ""))
    if not templates.is_file():
        pytest.skip("SKIPPED LOUDLY: set OPENBFME_MAC_TEMPLATES to the official Godot 4.7.2 export templates (.tpz)")
    tmp = tmp_path_factory.mktemp("macplay")
    helper = tmp / "openbfme-bundle-swap"
    subprocess.run(["clang", "-arch", "arm64", "-mmacosx-version-min=13.0", "-Wall", "-Wextra", "-Werror", str(mu.SWAP_SOURCE),
                    "-o", str(helper)], check=True)
    game = tmp / "fakegame"
    subprocess.run(["clang", "-arch", "arm64", "-mmacosx-version-min=13.0", "-O2", str(HERE / "launcher_fake_game.c"), "-o", str(game)],
                   check=True)
    godot_home = tmp / "godot-home"
    template_dir = godot_home / "Library/Application Support/Godot/export_templates/4.7.2.stable"
    template_dir.parent.mkdir(parents=True)
    mu.package_macos_preview.install_templates(templates, template_dir, sanitize_paths=True)
    launcher = mu._export_test_launcher(tmp, godot, helper, VERSION, ed25519.public_key(mu.TEST_SECRET), godot_home)
    return {"launcher": launcher, "game": game}


def _game_app(dest: Path, game: Path) -> None:
    """a stand-in OpenBFME.app: the stand-in game (it appends its path to OPENBFME_TEST_PLAYED and exits) in a validly ad-hoc signed app"""
    (dest / "Contents/MacOS").mkdir(parents=True)
    shutil.copyfile(game, dest / "Contents/MacOS/OpenBFME")
    (dest / "Contents/MacOS/OpenBFME").chmod(0o755)
    (dest / "Contents/Info.plist").write_bytes(plistlib.dumps({
        "CFBundleExecutable": "OpenBFME", "CFBundleIdentifier": "org.openbfme.test-game", "CFBundleName": "OpenBFME",
        "CFBundlePackageType": "APPL", "CFBundleVersion": "1"}))
    subprocess.run(["codesign", "--force", "--sign", "-", str(dest)], check=True, capture_output=True)


def _package_folder(root: Path, mac: dict, seal: bool = True, version: str = VERSION) -> Path:
    root.mkdir(parents=True)
    shutil.copytree(mac["launcher"], root / "OpenBFME Launcher.app", symlinks=True)
    _game_app(root / "OpenBFME.app", mac["game"])
    for name in ("README-MAC.txt", "LICENSE", "NOTICE"):
        (root / name).write_text(f"test {name}\n", encoding="utf-8")
    (root / "VERSION").write_text(f"OpenBFME {version} Mac Unstable Preview (Apple Silicon)\ncommit {mu.COMMIT}\ncommitted {mu.DATE}\n",
                                  encoding="utf-8")
    if seal:
        seal_package.seal_folder(root, mu.TEST_SECRET, REPO, version, "macos-arm64")
    return root


def _release_zip(tmp: Path, mac: dict, version: str = VERSION) -> Path:
    folder = _package_folder(tmp / "build" / f"openbfme-{version}-macos-arm64", mac, version=version)
    archive = tmp / f"openbfme-{version}-macos-arm64.zip"
    make_archive.zip_file(folder, archive, 1_700_000_000)
    return archive


def _unzip_like_finder(archive: Path, where: Path) -> Path:
    """ditto -x -k: what Archive Utility does with a double-clicked ZIP (the folder is named after the archive)"""
    where.mkdir(parents=True, exist_ok=True)
    subprocess.run(["ditto", "-x", "-k", str(archive), str(where)], check=True)
    return where / archive.stem


def _play(app: Path, home: Path, server: ReleaseServer, *args: str) -> tuple[subprocess.CompletedProcess, str, str]:
    """the launcher's window (headless), PLAY pressed once after the start-up check: (process, launcher.log, what the game wrote)"""
    played = home / "played.txt"
    r = mu._run_exported(app, home, server, "--ui-play=1", "--screenshot=" + str(home / "unused.png"), *args,
                         env_extra={"OPENBFME_TEST_PLAYED": str(played)})
    log = (mu._app_data(home) / "launcher.log").read_text(encoding="utf-8")
    if "LAUNCHER started " in log:   # the launcher quits as soon as the game process exists: wait for the stand-in game's line
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline and not (played.exists() and played.read_text(encoding="utf-8").endswith("\n")):
            time.sleep(0.1)
    return r, log, played.read_text(encoding="utf-8") if played.exists() else ""


@pytest.fixture
def home(tmp_path: Path) -> Path:
    # a home of its own per run: the launcher's one-instance port is derived from its data folder, and on the owner's Mac another
    # program (Razer's) listened on the port of a fixed test path, so that launcher refused to start (instance_lock.gd, by design)
    h = tmp_path / f"home-{os.urandom(4).hex()}"
    h.mkdir()
    return h


def test_bundled_signed_package_in_a_folder_with_spaces_plays(mac, tmp_path: Path, home: Path):
    """the release ZIP unpacked as Finder does into folders whose names have spaces: Play starts OpenBFME.app beside the launcher"""
    server = ReleaseServer()
    try:
        root = _unzip_like_finder(_release_zip(tmp_path, mac), tmp_path / "My Games" / "Open BFME (test)")
        r, log, played = _play(root / "OpenBFME Launcher.app", home, server, "--offline")
        game = root / "OpenBFME.app/Contents/MacOS/OpenBFME"
        assert r.returncode == 0, log
        assert played.strip() == f"played game {game}", (played, log)
        assert f"started {VERSION}, the game beside the launcher (pid " in log, log
        assert "no game to start" not in log
    finally:
        server.stop()


def test_exported_launcher_closing_while_the_lobby_has_not_answered_does_not_crash(mac, tmp_path: Path, home: Path):
    """the owner's Mac, 2026-10-17 11:13 (SIGSEGV at 0x190 on the online-count thread, the 'quit unexpectedly' dialog): the real exported
    launcher, its lobby slow to answer the players-online request, Play starts the game beside it at once and the window closes: exit
    0, the game played, the request was out before the close, the close not held for the lobby"""
    from test_launcher import STATS_GOOD
    server = ReleaseServer()
    try:
        server.stats = STATS_GOOD
        server.stats_delay = 20.0
        root = _unzip_like_finder(_release_zip(tmp_path, mac), tmp_path / "Games")
        app = root / "OpenBFME Launcher.app"
        assert mu._run_exported(app, home, server, f"--lobby={server.origin}").returncode == 0
        started = time.monotonic()
        r, log, played = _play(app, home, server)
        took = time.monotonic() - started
        assert r.returncode == 0, (r.returncode, log)
        assert played.strip() == f"played game {root / 'OpenBFME.app/Contents/MacOS/OpenBFME'}", (played, log)
        assert [p for p, _ in server.requests if p == "/v1/stats"], server.requests
        assert took < 15.0, f"the close waited {took:.1f} s for the lobby"
    finally:
        server.stop()


@pytest.mark.parametrize("damage, why", [
    ("unsealed", "there is no payload.json"),
    ("changed", "NOTICE does not match its signed SHA-256"),
    ("added-in-app", "codesign verification failed for OpenBFME.app"),
    ("bad-key", "the signature of payload.json is not valid"),
])
def test_bundled_package_that_is_not_the_signed_one_is_refused_by_name(mac, tmp_path: Path, home: Path, damage: str, why: str):
    """no payload inventory, a changed signed file, a file added inside an app (its code seal) or an inventory signed by another key:
    Play does not start the game and the window says why (the status line, and the dialog)"""
    server = ReleaseServer()
    try:
        root = _package_folder(tmp_path / "Open BFME", mac, seal=damage != "unsealed")
        if damage == "changed":
            (root / "NOTICE").write_bytes((root / "NOTICE").read_bytes().upper())   # the same size: only the SHA-256 tells
        elif damage == "added-in-app":
            (root / "OpenBFME.app/Contents/MacOS/extra").write_bytes(b"extra")
        elif damage == "bad-key":
            seal_package.seal_folder(root, bytes(range(5, 37)), REPO, VERSION, "macos-arm64")
        r, log, played = _play(root / "OpenBFME Launcher.app", home, server, "--offline")
        assert played == "", played
        assert "LAUNCHER ui test: PLAY pressed" in log and why in log, log
        dialog = [line for line in log.splitlines() if "LAUNCHER ui dialog: " in line][-1]
        assert why in dialog, dialog
    finally:
        server.stop()


def test_launcher_alone_downloads_installs_and_plays_the_signed_mac_package(mac, tmp_path: Path, home: Path):
    """owner A: a launcher with no game beside it downloads the release's signed Mac package by itself, installs it into
    versions/<v>/game (exec bits kept, both apps' code seals valid, no quarantine flag), and Play starts that game"""
    server = ReleaseServer()
    try:
        archive = _release_zip(tmp_path, mac)
        server.publish(VERSION, mu._test_signed_sidecars(archive, mu.TEST_SECRET, VERSION))
        alone = tmp_path / "Only The Launcher"
        alone.mkdir()
        shutil.copytree(mac["launcher"], alone / "OpenBFME Launcher.app", symlinks=True)
        r, log, played = _play(alone / "OpenBFME Launcher.app", home, server)
        game_root = mu._app_data(home) / "versions" / VERSION / "game"
        game = game_root / "OpenBFME.app/Contents/MacOS/OpenBFME"
        assert f"LAUNCHER installed {VERSION} (openbfme-{VERSION}-macos-arm64.zip" in log, log
        assert played.strip() == f"played game {game}", (played, log)
        assert f"LAUNCHER started {VERSION} (pid " in log, log
        assert os.access(game, os.X_OK) and os.access(game_root / "OpenBFME Launcher.app/Contents/MacOS/OpenBFME Launcher", os.X_OK)
        for app in ("OpenBFME.app", "OpenBFME Launcher.app"):
            subprocess.run(["codesign", "--verify", "--deep", "--strict", str(game_root / app)], check=True)
        flags = subprocess.run(["xattr", "-r", str(game_root)], capture_output=True, text=True, check=True).stdout
        assert "com.apple.quarantine" not in flags, flags
        assert [p for p, _ in server.requests if p.endswith(".zip")] == [f"/dl/{VERSION}/{archive.name}"]
        # the signed files, exactly
        want = json.loads(mu._test_signed_sidecars(archive, mu.TEST_SECRET, VERSION)["manifest-macos.json"])
        assert mu._file_inventory(game_root) == want["assets"]["game"]["macos-arm64"]["files"]
    finally:
        server.stop()


def test_installed_mac_version_changed_after_install_is_not_started(mac, tmp_path: Path, home: Path):
    """a file of versions/<v>/game changed after the install: Play refuses it by name (and offers a fresh download)"""
    server = ReleaseServer()
    try:
        archive = _release_zip(tmp_path, mac)
        server.publish(VERSION, mu._test_signed_sidecars(archive, mu.TEST_SECRET, VERSION))
        alone = tmp_path / "Launcher"
        alone.mkdir()
        shutil.copytree(mac["launcher"], alone / "OpenBFME Launcher.app", symlinks=True)
        first = mu._run_exported(alone / "OpenBFME Launcher.app", home, server, "--update")
        assert first.returncode == 0, first.stdout + first.stderr
        notice = mu._app_data(home) / "versions" / VERSION / "game" / "NOTICE"
        notice.write_bytes(notice.read_bytes().upper())   # the same size: only the SHA-256 tells
        r, log, played = _play(alone / "OpenBFME Launcher.app", home, server)
        assert played == "" and "NOTICE does not match its signed SHA-256" in log, log
        dialog = [line for line in log.splitlines() if "LAUNCHER ui dialog: " in line][-1]
        assert "Download it again?" in dialog, dialog
    finally:
        server.stop()


def test_translocated_launcher_says_what_to_do_and_downloads_nothing(mac, tmp_path: Path, home: Path):
    """the v0.4.6 report: macOS ran the launcher from an App Translocation copy (no OpenBFME.app beside it). Play says so and what to
    do; the start-up check downloads nothing for it (a second game for a copy the player already has beside the real launcher).
    The translocation is SIMULATED: the launcher app lies under .../AppTranslocation/<UUID>/d/ (a real translocation needs Gatekeeper
    to open a quarantined app, which shows its dialog on the Mac's screen)."""
    server = ReleaseServer()
    try:
        archive = _release_zip(tmp_path, mac)
        server.publish(VERSION, mu._test_signed_sidecars(archive, mu.TEST_SECRET, VERSION))
        copy = tmp_path / "private" / "AppTranslocation" / "1A2B3C4D-0000-0000-0000-000000000000" / "d"
        copy.mkdir(parents=True)
        shutil.copytree(mac["launcher"], copy / "OpenBFME Launcher.app", symlinks=True)
        r, log, played = _play(copy / "OpenBFME Launcher.app", home, server)
        assert played == ""
        assert "runs from a macOS App Translocation copy" in log, log
        dialog = [line for line in log.splitlines() if "LAUNCHER ui dialog: " in line][-1]
        for text in (TRANSLOCATION_WORDS, "Finder", "containing both apps", "Privacy & Security > Open Anyway",
                     "Do not override malware or damaged-app warnings."):
            assert text in dialog, dialog
        assert "xattr" not in dialog and "quarantine" not in dialog, dialog
        assert not [p for p, _ in server.requests if p.endswith(".zip")], server.requests
        assert not (mu._app_data(home) / "versions" / VERSION).exists()
    finally:
        server.stop()


def test_no_game_and_no_release_says_so_on_play_and_on_the_command_line(mac, tmp_path: Path, home: Path):
    """nothing beside the launcher, nothing installed, no release to download: Play shows a dialog naming what is missing and what to
    do (not a dimmed button); --play says the same and exits 1"""
    server = ReleaseServer()
    try:
        alone = tmp_path / "Launcher Only"
        alone.mkdir()
        shutil.copytree(mac["launcher"], alone / "OpenBFME Launcher.app", symlinks=True)
        r, log, played = _play(alone / "OpenBFME Launcher.app", home, server)
        assert played == ""
        assert "PLAY pressed (no version, Play enabled)" in log, log
        dialog = [line for line in log.splitlines() if "LAUNCHER ui dialog: " in line][-1]
        assert "No game to start" in dialog and "No game is installed yet" in dialog and "Check for updates" in dialog, dialog
        cli = mu._run_exported(alone / "OpenBFME Launcher.app", home, server, "--play")
        log = (mu._app_data(home) / "launcher.log").read_text(encoding="utf-8")
        assert cli.returncode == 1 and "LAUNCHER nothing to play: No game is installed yet" in log, log
    finally:
        server.stop()


def test_apps_moved_out_of_their_package_folder_play_and_update_into_the_data_folder(mac, tmp_path: Path, home: Path):
    """the package's files moved into a folder of the player's own (not the openbfme-<v>-macos-arm64 folder the whole-package update
    replaces): Play starts the game beside the launcher; a newer release installs into versions/<v>/game (not a refused update), and
    the next Play starts that newer version"""
    newer = "v1.3.1-preview.1"
    server = ReleaseServer()
    try:
        archive = _release_zip(tmp_path / "next", mac, newer)
        server.publish(newer, mu._test_signed_sidecars(archive, mu.TEST_SECRET, newer))
        root = _package_folder(tmp_path / "My Own Games", mac)
        r, log, played = _play(root / "OpenBFME Launcher.app", home, server, "--offline")
        assert played.strip() == f"played game {root / 'OpenBFME.app/Contents/MacOS/OpenBFME'}", (played, log)
        (home / "played.txt").unlink()
        r, log, played = _play(root / "OpenBFME Launcher.app", home, server)
        game = mu._app_data(home) / "versions" / newer / "game" / "OpenBFME.app/Contents/MacOS/OpenBFME"
        assert "is not one the package update replaces" in log and f"LAUNCHER installed {newer}" in log, log
        assert "Update rejected" not in log, log
        assert played.strip() == f"played game {game}", (played, log)
        assert (root / "VERSION").read_text(encoding="utf-8").startswith(f"OpenBFME {VERSION} "), "the folder of the apps is unchanged"
    finally:
        server.stop()


# ---- the update helper's process probe with the helper's app gone (Sol r3, P2; Linux, through the libproc shim) -------------------------

@pytest.fixture(scope="module")
def probe_app(tmp_path_factory) -> Path:
    """<tmp>/Probe.app/Contents/MacOS: a copy of Godot and tools/release/macos_bundle_swap.c built for Linux with the libproc shim
    (fixtures/macos_shim), where MacOSUpdate._local_swap_helper() finds the helper beside the running Godot"""
    godot = os.environ.get("GODOT") or shutil.which("godot")
    cc = shutil.which("cc")
    if sys.platform != "linux" or not godot or not cc:
        pytest.skip("SKIPPED LOUDLY: the shimmed helper probe needs Linux, Godot and a C compiler (the Mac runs the native tests above)")
    macos = tmp_path_factory.mktemp("probe") / "Probe.app/Contents/MacOS"
    macos.mkdir(parents=True)
    real = Path(godot).resolve()
    try:
        os.link(real, macos / "godot")
    except OSError:
        shutil.copy2(real, macos / "godot")
    built = subprocess.run([cc, "-D_GNU_SOURCE", "-Wall", "-Werror", "-I", str(HERE / "fixtures/macos_shim"), "-o",
                            str(macos / "openbfme-bundle-swap"), str(HERE / "macos_bundle_swap.c")], capture_output=True, text=True)
    assert built.returncode == 0, built.stderr
    return macos


def _probe(macos: Path, home: Path, txid: str, pid: int = 0) -> tuple[Path, str]:
    env = dict(os.environ, HOME=str(home), XDG_DATA_HOME=str(home / ".local/share"), XDG_CONFIG_HOME=str(home / ".config"))
    args = [str(macos / "godot"), "--headless", "--path", str(HERE.parents[1] / "launcher"), "--script",
            "res://tests/macos_helper_probe_test.gd", "--", f"--txid={txid}"] + ([f"--pid={pid}"] if pid else [])
    r = subprocess.run(args, env=env, capture_output=True, text=True, timeout=120)
    assert r.returncode == 0, r.stdout[-2000:] + r.stderr[-2000:]
    found = dict(line.split(" ", 2)[1:] for line in r.stdout.splitlines() if line.startswith("PROBE DIR "))
    status = [line.removeprefix("PROBE ") for line in r.stdout.splitlines() if line.startswith("PROBE known=")]
    return Path(found["DIR"]), status[0] if status else ""


@pytest.mark.parametrize("case, expected", [
    # Sol r3's reproduction: removal interrupted after the whole helper app was gone; the helper process has exited (92a808ca3: unknown,
    # so the retirement kept the journal at every start)
    ("app-gone", "known=true active=false"),
    # the wider question must not miss a helper still running from the transaction directory
    ("app-gone+running-inside", "known=true active=true"),
    # an unrelated live process (the pid reused) is not the helper
    ("app-gone+running-elsewhere", "known=true active=false"),
    # controls: the app still there (the r2 partial removal), and a link where the app was (still refused as unknown)
    ("app-present", "known=true active=false"),
    ("app-is-link", "known=false active=true"),
])
def test_helper_probe_answers_after_the_helper_app_is_gone(probe_app: Path, tmp_path: Path, case: str, expected: str):
    """MACPLAY-1 (Sol r3, P2) on any Linux host: MacOSUpdate._helper_process_status asks the real helper (macos_bundle_swap.c,
    --process-under) about the helper's app, which needs that folder to exist. With the app gone it now asks about the transaction folder
    (the native Mac flow is test_damaged_package_recovery_resumes_after_an_interruption[...helper-app-gone] and
    test_helper_retirement_finishes_after_the_helper_app_is_gone)"""
    home = tmp_path / "home"
    home.mkdir()
    txid = "20261018T000000Z-1234-probe"
    helper_dir, _ = _probe(probe_app, home, txid)
    helper_dir.mkdir(parents=True)
    app = helper_dir / "OpenBFME Launcher.app"
    if case == "app-present":
        (app / "Contents/MacOS").mkdir(parents=True)
    elif case == "app-is-link":
        (tmp_path / "elsewhere").mkdir()
        os.symlink(tmp_path / "elsewhere", app)
    running = None
    try:
        if case.endswith("running-inside"):
            shutil.copy2(shutil.which("sleep"), helper_dir / "sleep")
            running = subprocess.Popen([str(helper_dir / "sleep"), "60"])
        elif case.endswith("running-elsewhere"):
            running = subprocess.Popen([shutil.which("sleep"), "60"])
        else:
            exited = subprocess.Popen([shutil.which("true")])
            exited.wait()
        pid = running.pid if running else exited.pid
        _, status = _probe(probe_app, home, txid, pid)
        assert status == expected
    finally:
        if running:
            running.kill()
            running.wait()


# ---- the macOS manifest cache (Sol r1 on DEVPORT-6, P2; any system) --------------------------------------------------------------------

def _cache_run(tmp: Path, server: ReleaseServer, mode: str) -> subprocess.CompletedProcess:
    godot = os.environ.get("GODOT") or shutil.which("godot")
    home = tmp / "cache-home"
    home.mkdir(exist_ok=True)
    env = dict(os.environ, HOME=str(home), XDG_DATA_HOME=str(home / ".local/share"), XDG_CONFIG_HOME=str(home / ".config"))
    return subprocess.run([godot, "--headless", "--path", str(HERE.parents[1] / "launcher"), "--script", "res://tests/macos_cache_test.gd",
                           "--", f"--origin={server.origin}", f"--key={ed25519.public_key(mu.TEST_SECRET).hex()}", f"--tag={VERSION}",
                           f"--mode={mode}"], env=env, capture_output=True, text=True, timeout=300)


@pytest.mark.parametrize("first", ["incomplete", "badsig", "kill"])
def test_macos_manifest_cache_never_blocks_a_retry(tmp_path: Path, first: str):
    """a cache folder holding only the manifest (a crash or full disk between the two writes), a cached pair whose signature does not
    verify, or a launcher killed between writing the pair and publishing it: the next check fetches the sidecar again and succeeds,
    and the cache then holds exactly the verified pair (DEVPORT-6 refused that release forever: 'the cached macOS manifest sidecar is
    incomplete')"""
    if not (os.environ.get("GODOT") or shutil.which("godot")):
        pytest.skip("SKIPPED LOUDLY: Godot is not available for the launcher script test")
    server = ReleaseServer()
    try:
        server.publish(VERSION, mu._test_signed_sidecars(None, mu.TEST_SECRET, VERSION))
        r = _cache_run(tmp_path, server, first)
        if first == "kill":
            assert r.returncode != 0 and "CACHE RESULT" not in r.stdout, r.stdout[-2000:]   # really killed before it could answer
            again = _cache_run(tmp_path, server, "fetch")
        else:
            again = r
        assert again.returncode == 0 and "CACHE RESULT ok=true" in again.stdout, again.stdout[-2000:] + again.stderr[-1000:]
        assert "CACHE FILES manifest-macos.json,manifest-macos.json.sig" in again.stdout, again.stdout[-2000:]
        sidecar = [p for p, _ in server.requests if p.endswith("/manifest-macos.json")]
        assert len(sidecar) == 2 if first == "kill" else len(sidecar) == 1, server.requests
        cached = _cache_run(tmp_path, server, "fetch")   # the published pair is used: nothing fetched again
        assert "CACHE RESULT ok=true" in cached.stdout and len([p for p, _ in server.requests if p.endswith("/manifest-macos.json")]) == len(sidecar)
    finally:
        server.stop()


@pytest.mark.parametrize("link", ["linkentry", "linkparent"])
def test_macos_manifest_cache_never_follows_a_link_out_of_the_cache(tmp_path: Path, link: str):
    """Sol r2 (P2): the tag's cache folder, or cache/manifests-macos, is a symbolic link to a folder outside the cache that holds an invalid
    manifest and a file of its own: the check still succeeds (the sidecar fetched and verified), the folder outside keeps its files
    untouched (f2f5fc9a5 deleted them: remove_tree opened the link it was given), and nothing is written through the link (a linked tag
    folder is replaced by the real published pair; below a linked parent nothing is cached)"""
    if not (os.environ.get("GODOT") or shutil.which("godot")):
        pytest.skip("SKIPPED LOUDLY: Godot is not available for the launcher script test")
    server = ReleaseServer()
    try:
        server.publish(VERSION, mu._test_signed_sidecars(None, mu.TEST_SECRET, VERSION))
        r = _cache_run(tmp_path, server, link)
        out = r.stdout
        assert r.returncode == 0 and "CACHE RESULT ok=true" in out, out[-2000:] + r.stderr[-1000:]
        assert "CACHE SENTINEL keep=true manifest={}" in out, out[-2000:]
        if link == "linkentry":
            assert "CACHE ENTRY link=false" in out and "CACHE FILES manifest-macos.json,manifest-macos.json.sig" in out, out[-2000:]
        else:
            assert "CACHE FILES keep.txt,manifest-macos.json" in out, out[-2000:]   # the folder outside, as it was
            assert "the macOS manifest cache is not used: " in out and "manifests-macos is a symbolic link" in out, out[-2000:]
    finally:
        server.stop()


# ---- Sol r1 on DEVPORT-6: the whole-package update's trust and recovery (macOS only, the real exported launchers) ----------------------

from test_macos_update import exported_macos_launchers  # noqa: E402,F401  (the module fixture: launchers a v1.2.2, b, wrong_version)


def _journal(home: Path) -> dict:
    return json.loads((mu._app_data(home) / "macos-update.json").read_text(encoding="utf-8"))


def _state(home: Path) -> str:
    p = mu._app_data(home) / "state.cfg"
    return p.read_text(encoding="utf-8") if p.exists() else ""


def _staged(tmp_path: Path, home: Path, server: ReleaseServer, launchers: dict) -> Path:
    """the installed sealed package A (v1.2.2) with the signed package B staged (the journal written, nothing exchanged yet)"""
    root = mu._installed_root(tmp_path / "install", launchers["a"])
    package = mu._test_package(tmp_path / f"openbfme-{mu.VERSION}-macos-arm64", launchers["b"], mu.VERSION)
    server.publish(mu.VERSION, mu._test_signed_sidecars(package, mu.TEST_SECRET, mu.VERSION))
    staged = mu._run_exported(root / "OpenBFME Launcher.app", home, server, "--install=" + mu.VERSION)
    assert staged.returncode == 0 and "staged" in staged.stdout + staged.stderr, staged.stdout + staged.stderr
    return root


def test_rollback_never_starts_a_previous_package_that_signed_data_does_not_name(exported_macos_launchers, tmp_path: Path, home: Path):
    """Sol r1 P1: the journal's old inventory is not signed. The installed package is changed locally, its app re-signed ad hoc and the
    journal edited to match: the update neither exchanges nor starts anything (DEVPORT-6 exchanged, and its rollback started the changed
    launcher again); the reason is kept for the next window"""
    server = ReleaseServer()
    try:
        root = _staged(tmp_path, home, server, exported_macos_launchers)
        plist = root / "OpenBFME.app/Contents/Info.plist"
        plist.write_bytes(plist.read_bytes().replace(b"</dict>", b"<key>Tampered</key><true/></dict>"))
        subprocess.run(["codesign", "--force", "--sign", "-", str(root / "OpenBFME.app")], check=True, capture_output=True)
        journal = _journal(home)
        journal["old_files"] = mu._file_inventory(root)
        (mu._app_data(home) / "macos-update.json").write_text(json.dumps(journal), encoding="utf-8")
        tampered = mu._file_inventory(root)
        mu._run_exported(root / "OpenBFME Launcher.app", home, server, "--version",
                         env_extra={"OPENBFME_TEST_MACOS_KILL_AT": "before-confirm", "OPENBFME_TEST_MACOS_CONFIRM_TIMEOUT_MS": "1500"})
        time.sleep(8)   # what a rollback would have needed to start A again
        log = (mu._app_data(home) / "launcher.log").read_text(encoding="utf-8")
        assert log.count("LAUNCHER OpenBFME Launcher v1.2.2") == 2, log    # the --install run and this one: nothing started again
        assert f"LAUNCHER OpenBFME Launcher {mu.VERSION}" not in log, log    # B never ran
        assert mu._file_inventory(root) == tampered and (mu._app_data(home) / "macos-update.json").exists()
        assert "neither the new nor the previous package matches its signed inventory" in _state(home), _state(home)
    finally:
        server.stop()


def _damage(tree: Path, damage: str) -> None:
    if damage == "notice":
        (tree / "NOTICE").write_bytes((tree / "NOTICE").read_bytes().upper())
    else:   # Sol r2 P1: the new package lost a whole app (f2f5fc9a5's exchange refused a root without both apps: stranded)
        shutil.rmtree(tree / "OpenBFME.app")


@pytest.mark.parametrize("damage", ["notice", "no-game-app"])
@pytest.mark.parametrize("where", ["new-at-root", "new-staged"])
def test_a_damaged_new_package_restores_the_verified_previous_one(exported_macos_launchers, tmp_path: Path, home: Path, where: str,
                                                                   damage: str):
    """Sol r1 P1: an interrupted exchange whose NEW package is damaged (at the root after the exchange, or still staged): the previous
    package verifies on its own (its signed payload inventory) and is put back at the root exactly; the damaged copy is kept aside, the
    transaction retired, and the next window shows what happened (DEVPORT-6 refused both trees together and kept the install stranded).
    Sol r2 P1: also when the damaged package at the root lacks a whole app (the restore exchange checks the package coming back)"""
    server = ReleaseServer()
    try:
        root = _staged(tmp_path, home, server, exported_macos_launchers)
        original = mu._file_inventory(root)
        journal = _journal(home)
        if where == "new-at-root":
            mu._run_exported(root / "OpenBFME Launcher.app", home, server, "--version",
                             env_extra={"OPENBFME_TEST_MACOS_KILL_AT": "after-exchange"})
            assert mu._wait_for_version(root, mu.VERSION, timeout=35), "the exchange did not happen"
            assert mu._wait_for_process_exit(exported_macos_launchers["helper"], _journal(home)["apply_pid"])
            damaged = root
        else:
            damaged = Path(journal["candidate"])
        _damage(damaged, damage)
        mu._run_exported(root / "OpenBFME Launcher.app", home, server, "--version")
        assert mu._file_inventory(root) == original, "the previous package is not back at the root exactly"
        aside = Path(journal["candidate"] + "-damaged-1")
        assert aside.is_dir() and (aside / "VERSION").read_text(encoding="utf-8").startswith(f"OpenBFME {mu.VERSION}")
        assert not (mu._app_data(home) / "macos-update.json").exists() and not Path(journal["candidate"]).exists()
        assert not (mu._app_data(home) / "macos-update/helper" / journal["txid"]).exists()
        assert "did not complete and its files are damaged. The previous version v1.2.2 was restored" in _state(home), _state(home)
        shown = mu._run_exported(root / "OpenBFME Launcher.app", home, server, "--offline", "--screenshot=" + str(tmp_path / "x.png"))
        assert shown.returncode == 0
        log = (mu._app_data(home) / "launcher.log").read_text(encoding="utf-8")
        assert "showing an update failure from while the game ran: The Mac update to" in log, log
    finally:
        server.stop()


def _launcher_runs(home: Path, version: str) -> int:
    log = mu._app_data(home) / "launcher.log"
    return log.read_text(encoding="utf-8").count(f"LAUNCHER OpenBFME Launcher {version}") if log.exists() else 0


def _wait(predicate, timeout: float = 40) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(0.2)
    return False


@pytest.mark.parametrize("damage", ["notice", "no-game-app"])
def test_live_rollback_restores_the_previous_package_when_the_new_one_is_damaged(exported_macos_launchers, tmp_path: Path, home: Path,
                                                                                   damage: str):
    """Sol r2 P2: the new launcher never confirms (killed before confirming), and while the helper is about to roll back, the new package
    at the root is damaged: the previous package is put back exactly, verified on its own, and started again; the damaged copy is kept
    aside, the next window says what happened, and the next launcher start retires the transaction (f2f5fc9a5 required the damaged
    candidate to verify after the exchange back: it recorded rollback-failed, started nothing, and startup then refused to recover)"""
    server = ReleaseServer()
    pause = tmp_path / "pause"
    try:
        root = _staged(tmp_path, home, server, exported_macos_launchers)
        original = mu._file_inventory(root)
        journal = _journal(home)
        before = _launcher_runs(home, "v1.2.2")
        mu._run_exported(root / "OpenBFME Launcher.app", home, server, "--version", env_extra={
            "OPENBFME_TEST_MACOS_KILL_AT": "before-confirm", "OPENBFME_TEST_MACOS_PAUSE_AT": "before-rollback",
            "OPENBFME_TEST_MACOS_PAUSE_FILE": str(pause)})
        assert _wait(lambda: Path(str(pause) + ".waiting").exists()), (mu._app_data(home) / "launcher.log").read_text(encoding="utf-8")
        assert (root / "VERSION").read_text(encoding="utf-8").startswith(f"OpenBFME {mu.VERSION}")   # the new package is at the root
        _damage(root, damage)
        Path(str(pause) + ".go").write_text("go", encoding="utf-8")
        assert mu._wait_for_process_exit(exported_macos_launchers["helper"], _journal(home)["apply_pid"], timeout=60)
        assert _wait(lambda: _launcher_runs(home, "v1.2.2") == before + 2), \
            (mu._app_data(home) / "launcher.log").read_text(encoding="utf-8")   # this run and the previous launcher started again
        assert mu._file_inventory(root) == original, "the previous package is not back at the root exactly"
        aside = Path(journal["candidate"] + "-damaged-1")
        assert aside.is_dir() and (aside / "VERSION").read_text(encoding="utf-8").startswith(f"OpenBFME {mu.VERSION}")
        assert "did not complete and its files are damaged. The previous version v1.2.2 was restored" in _state(home), _state(home)
        time.sleep(2)
        mu._run_exported(root / "OpenBFME Launcher.app", home, server, "--version")   # the next start retires the transaction
        assert _wait(lambda: not (mu._app_data(home) / "macos-update.json").exists(), 20), _journal(home)
        assert not (mu._app_data(home) / "macos-update/helper" / journal["txid"]).exists() and mu._file_inventory(root) == original
    finally:
        server.stop()


@pytest.mark.parametrize("kill", ["damaged-recorded", "damaged-aside", "damaged-aside+partial-helper", "damaged-aside+helper-app-gone",
                                  "damaged-aside+helper-dir-gone"])
def test_damaged_package_recovery_resumes_after_an_interruption(exported_macos_launchers, tmp_path: Path, home: Path, kill: str):
    """Sol r2 P2: the recovery of a damaged new package is killed after it recorded its phase, after it moved the damaged copy aside, or
    after that with the helper partly removed (a crash while removing it): the journal names the phase ("retiring-damaged"; f2f5fc9a5
    kept the old state, so a journal naming a missing sibling was refused as unsafe and a partial helper blocked Play), and the next
    start completes the retirement: previous package at the root exactly, damaged copy aside, helper and journal removed.
    Sol r3 P2: the removal interrupted after the helper's whole app was gone (its transaction directory left; 92a808ca3 asked the helper
    about the missing app, got "unknown" and kept the journal at every start), and after the whole
    transaction directory was gone (only the journal left)"""
    server = ReleaseServer()
    try:
        root = _staged(tmp_path, home, server, exported_macos_launchers)
        original = mu._file_inventory(root)
        journal = _journal(home)
        mu._run_exported(root / "OpenBFME Launcher.app", home, server, "--version",
                         env_extra={"OPENBFME_TEST_MACOS_KILL_AT": "after-exchange"})
        assert mu._wait_for_version(root, mu.VERSION, timeout=35), "the exchange did not happen"
        assert mu._wait_for_process_exit(exported_macos_launchers["helper"], _journal(home)["apply_pid"])
        _damage(root, "notice")
        mu._run_exported(root / "OpenBFME Launcher.app", home, server, "--version",
                         env_extra={"OPENBFME_TEST_MACOS_KILL_AT": kill.split("+")[0]})
        interrupted = _journal(home)
        assert interrupted["state"] == "retiring-damaged", interrupted["state"]
        assert mu._file_inventory(root) == original
        candidate = Path(journal["candidate"])
        aside = Path(journal["candidate"] + "-damaged-1")
        assert candidate.is_dir() != aside.is_dir() and aside.is_dir() == (kill != "damaged-recorded")
        helper_dir = mu._app_data(home) / "macos-update/helper" / journal["txid"]
        if kill.endswith("partial-helper"):
            (helper_dir / "OpenBFME Launcher.app/Contents/Info.plist").unlink()
        elif kill.endswith("helper-app-gone"):
            shutil.rmtree(helper_dir / "OpenBFME Launcher.app")
            assert helper_dir.is_dir() and not any(helper_dir.iterdir())
        elif kill.endswith("helper-dir-gone"):
            shutil.rmtree(helper_dir)
        mu._run_exported(root / "OpenBFME Launcher.app", home, server, "--version")
        assert not (mu._app_data(home) / "macos-update.json").exists(), (mu._app_data(home) / "launcher.log").read_text(encoding="utf-8")
        assert not helper_dir.exists() and not candidate.exists() and aside.is_dir()
        assert mu._file_inventory(root) == original
        assert "did not complete and its files are damaged. The previous version v1.2.2 was restored" in _state(home), _state(home)
    finally:
        server.stop()


@pytest.mark.parametrize("flow", ["confirmed", "rolled-back"])
def test_helper_retirement_finishes_after_the_helper_app_is_gone(exported_macos_launchers, tmp_path: Path, home: Path, flow: str):
    """Sol r3 P2 on the two other retirement paths (the damaged-package one is in the test above): the confirmed update's and the rolled
    back update's retirement of the helper find the helper's whole app gone (removed while the retirement waits at its start, the way an
    interrupted removal leaves it) and its transaction directory still there; the retirement still completes, on that start or the next
    (92a808ca3 asked the helper about the missing app, got "unknown" and kept the journal at every start, which blocks later updates)"""
    server = ReleaseServer()
    pause = tmp_path / "pause"
    try:
        root = _staged(tmp_path, home, server, exported_macos_launchers)
        original = mu._file_inventory(root)
        journal = _journal(home)
        env = {"OPENBFME_TEST_MACOS_PAUSE_AT": "before-helper-retirement", "OPENBFME_TEST_MACOS_PAUSE_FILE": str(pause)}
        if flow == "rolled-back":
            env.update({"OPENBFME_TEST_MACOS_KILL_AT": "before-confirm", "OPENBFME_TEST_MACOS_CONFIRM_TIMEOUT_MS": "1500"})
        mu._run_exported(root / "OpenBFME Launcher.app", home, server, "--version", env_extra=env)
        assert _wait(lambda: Path(str(pause) + ".waiting").exists(), 90), (mu._app_data(home) / "launcher.log").read_text(encoding="utf-8")
        helper_dir = mu._app_data(home) / "macos-update/helper" / journal["txid"]
        shutil.rmtree(helper_dir / "OpenBFME Launcher.app")
        assert helper_dir.is_dir() and not any(helper_dir.iterdir())
        Path(str(pause) + ".go").write_text("go", encoding="utf-8")
        assert mu._wait_for_process_exit(exported_macos_launchers["helper"], _journal(home)["apply_pid"], timeout=60)
        if not _wait(lambda: not (mu._app_data(home) / "macos-update.json").exists(), 10):
            time.sleep(2)
            mu._run_exported(root / "OpenBFME Launcher.app", home, server, "--version")   # the next start finishes it
        log = (mu._app_data(home) / "launcher.log").read_text(encoding="utf-8")
        assert _wait(lambda: not (mu._app_data(home) / "macos-update.json").exists(), 20), log
        assert not helper_dir.exists(), log
        assert "process state could not be established safely" not in log, log
        if flow == "confirmed":
            assert (root / "VERSION").read_text(encoding="utf-8").startswith(f"OpenBFME {mu.VERSION}")
        else:
            assert mu._file_inventory(root) == original
    finally:
        server.stop()


@pytest.mark.parametrize("link", ["inside-app", "signed-file"])
def test_links_inside_the_package_are_refused(exported_macos_launchers, tmp_path: Path, home: Path, link: str):
    """Sol r1: a symbolic link inside an app bundle, or a signed file replaced by a link to identical bytes: Play does not start the
    game, and the whole-package update refuses the package"""
    server = ReleaseServer()
    try:
        root = mu._installed_root(tmp_path / "install", exported_macos_launchers["a"])
        if link == "inside-app":
            os.symlink("Info.plist", root / "OpenBFME.app/Contents/Resources/link")
        else:
            copy = tmp_path / "notice-copy"
            shutil.copyfile(root / "NOTICE", copy)
            (root / "NOTICE").unlink()
            os.symlink(copy, root / "NOTICE")
        played = home / "played.txt"
        r = mu._run_exported(root / "OpenBFME Launcher.app", home, server, "--offline", "--play",
                             env_extra={"OPENBFME_TEST_PLAYED": str(played)})
        log = (mu._app_data(home) / "launcher.log").read_text(encoding="utf-8")
        assert r.returncode != 0 and not played.exists() and "is a symbolic link" in log, log
        package = mu._test_package(tmp_path / f"openbfme-{mu.VERSION}-macos-arm64", exported_macos_launchers["b"], mu.VERSION)
        server.publish(mu.VERSION, mu._test_signed_sidecars(package, mu.TEST_SECRET, mu.VERSION))
        before = {p.relative_to(root).as_posix() for p in root.rglob("*")}
        mu._run_exported(root / "OpenBFME Launcher.app", home, server, "--install=" + mu.VERSION)
        log = (mu._app_data(home) / "launcher.log").read_text(encoding="utf-8")
        assert not (mu._app_data(home) / "macos-update.json").exists(), log   # no whole-package exchange of a package with a link
        assert {p.relative_to(root).as_posix() for p in root.rglob("*")} == before
    finally:
        server.stop()


def test_an_older_release_is_installed_only_when_the_player_picks_it(exported_macos_launchers, tmp_path: Path, home: Path):
    """Sol r1: automatic vs explicit downgrade in the macOS flow: --update never stages an older release; --install=<older> does"""
    older = "v1.2.1"
    server = ReleaseServer()
    try:
        root = mu._installed_root(tmp_path / "install", exported_macos_launchers["a"])
        package = mu._test_package(tmp_path / f"openbfme-{older}-macos-arm64", exported_macos_launchers["a"], older)
        server.publish(older, mu._test_signed_sidecars(package, mu.TEST_SECRET, older), prerelease=False)
        auto = mu._run_exported(root / "OpenBFME Launcher.app", home, server, "--update", "--channel=stable")
        assert auto.returncode == 0 and not (mu._app_data(home) / "macos-update.json").exists(), auto.stdout + auto.stderr
        log = (mu._app_data(home) / "launcher.log").read_text(encoding="utf-8")
        assert "up to date: v1.2.2 installed" in log, log
        pick = mu._run_exported(root / "OpenBFME Launcher.app", home, server, f"--install={older}")
        assert pick.returncode == 0 and _journal(home)["version"] == older, pick.stdout + pick.stderr
    finally:
        server.stop()


# ---- MACFIX-1: Finder / iCloud Drive metadata is no change (issue #47); the download progress keeps moving (issue #46) -------------------

# what issue #47's Mac showed on an app unpacked into an iCloud Drive Desktop (com.apple.FinderInfo with Finder flags set: the bare
# --strict check's "detritus"), a resource fork as Finder copies one, and the Finder files of a folder Finder shows
FINDER_INFO = "0000000000000000400000000000000000000000000000000000000000000000"


def _icloud_metadata(root: Path) -> None:
    for app in ("OpenBFME.app", "OpenBFME Launcher.app"):
        for path in (root / app, root / app / "Contents/Info.plist"):
            subprocess.run(["xattr", "-wx", "com.apple.FinderInfo", FINDER_INFO, str(path)], check=True)
        subprocess.run(["xattr", "-w", "com.apple.fileprovider.fpfs#P", "1", str(root / app)], check=True)
        subprocess.run(["xattr", "-w", "com.apple.provenance", "x", str(root / app)], check=True)
    for exe in ("OpenBFME.app/Contents/MacOS/OpenBFME", "OpenBFME Launcher.app/Contents/MacOS/OpenBFME Launcher"):
        subprocess.run(["xattr", "-wx", "com.apple.FinderInfo", FINDER_INFO, str(root / exe)], check=True)
    subprocess.run(["xattr", "-w", "com.apple.ResourceFork", "fork", str(root / "OpenBFME.app/Contents/Info.plist")], check=True)
    for name in (".DS_Store", "Icon\r", "._OpenBFME.app"):
        (root / name).write_bytes(b"Finder\n")
    # the fixture is issue #47's case: the bare strict check refuses it, the seal itself holds
    strict = subprocess.run(["codesign", "--verify", "--deep", "--strict", str(root / "OpenBFME.app")], capture_output=True, text=True)
    assert strict.returncode != 0 and "detritus" in strict.stderr, strict.stderr
    subprocess.run(["codesign", "--verify", "--deep", str(root / "OpenBFME.app")], check=True, capture_output=True)


def test_icloud_metadata_on_the_unpacked_package_is_no_change_and_play_starts_it(mac, tmp_path: Path, home: Path):
    """issue #47: the release ZIP unpacked into an iCloud Drive Desktop (Finder / file provider attributes on the apps and their files,
    a resource fork, .DS_Store / Icon / AppleDouble files beside the apps): Play starts the game beside the launcher, nothing is called
    changed, and the folder still takes the whole-package update"""
    server = ReleaseServer()
    try:
        root = _unzip_like_finder(_release_zip(tmp_path, mac), tmp_path / "Desktop")
        _icloud_metadata(root)
        r, log, played = _play(root / "OpenBFME Launcher.app", home, server, "--offline")
        assert played.strip() == f"played game {root / 'OpenBFME.app/Contents/MacOS/OpenBFME'}", (played, log)
        assert "is not the signed package" not in log and "was changed after it was installed" not in log, log
        assert "is not one the package update replaces" not in log, log
    finally:
        server.stop()


@pytest.mark.parametrize("change, why", [
    ("added-in-app", "codesign verification failed for OpenBFME.app"),
    ("appledouble-in-app", "codesign verification failed for OpenBFME.app"),
    ("changed-in-app", "OpenBFME.app/Contents/Info.plist does not match its signed SHA-256"),
    ("changed", "NOTICE does not match its signed SHA-256"),
])
def test_a_real_change_under_icloud_metadata_is_still_refused_by_name(mac, tmp_path: Path, home: Path, change: str, why: str):
    """issue #47's fix keeps tamper detection: with the same metadata, a file added inside an app, an AppleDouble file inside an app
    (not ignored there: the seal refuses it), a changed sealed file or a changed signed file outside the apps is refused, Play starts
    nothing, and the dialog names what was found"""
    server = ReleaseServer()
    try:
        root = _unzip_like_finder(_release_zip(tmp_path, mac), tmp_path / "Desktop")
        _icloud_metadata(root)
        if change == "added-in-app":
            (root / "OpenBFME.app/Contents/MacOS/extra").write_bytes(b"extra")
        elif change == "appledouble-in-app":
            (root / "OpenBFME.app/Contents/MacOS/._OpenBFME").write_bytes(b"\x00\x05\x16\x07 AppleDouble")
        elif change == "changed-in-app":
            plist = root / "OpenBFME.app/Contents/Info.plist"
            plist.write_bytes(plist.read_bytes().replace(b"org.openbfme.test-game", b"org.openbfme.test-gamf"))
        else:
            (root / "NOTICE").write_bytes((root / "NOTICE").read_bytes().upper())
        r, log, played = _play(root / "OpenBFME Launcher.app", home, server, "--offline")
        assert played == "", played
        assert "LAUNCHER ui test: PLAY pressed" in log and why in log, log
        dialog = [line for line in log.splitlines() if "LAUNCHER ui dialog: " in line][-1]
        assert why in dialog, dialog
    finally:
        server.stop()


def test_icloud_metadata_on_an_installed_version_is_no_change(mac, tmp_path: Path, home: Path):
    """issue #47 for versions/<v>/game (a launcher alone downloads the package): the same metadata on the installed apps and Finder files
    in its folder; Play starts that version"""
    server = ReleaseServer()
    try:
        archive = _release_zip(tmp_path, mac)
        server.publish(VERSION, mu._test_signed_sidecars(archive, mu.TEST_SECRET, VERSION))
        alone = tmp_path / "Launcher"
        alone.mkdir()
        shutil.copytree(mac["launcher"], alone / "OpenBFME Launcher.app", symlinks=True)
        first = mu._run_exported(alone / "OpenBFME Launcher.app", home, server, "--update")
        assert first.returncode == 0, first.stdout + first.stderr
        game_root = mu._app_data(home) / "versions" / VERSION / "game"
        _icloud_metadata(game_root)
        r, log, played = _play(alone / "OpenBFME Launcher.app", home, server)
        assert played.strip() == f"played game {game_root / 'OpenBFME.app/Contents/MacOS/OpenBFME'}", (played, log)
        assert "was changed after it was installed" not in log, log
    finally:
        server.stop()


def test_icloud_metadata_on_the_installed_package_still_takes_the_whole_package_update(exported_macos_launchers, tmp_path: Path,
                                                                                         home: Path):
    """issue #47 ("updates install into the launcher's data folder: ... detritus not allowed", then "not a complete standalone OpenBFME
    package" for Finder's .DS_Store): the installed package A with the metadata stages, exchanges and confirms the signed package B; the
    root then holds exactly B's signed files"""
    server = ReleaseServer()
    try:
        root = mu._installed_root(tmp_path / "install", exported_macos_launchers["a"])
        _icloud_metadata(root)
        package = mu._test_package(tmp_path / f"openbfme-{mu.VERSION}-macos-arm64", exported_macos_launchers["b"], mu.VERSION)
        sidecars = mu._test_signed_sidecars(package, mu.TEST_SECRET, mu.VERSION)
        server.publish(mu.VERSION, sidecars)
        staged = mu._run_exported(root / "OpenBFME Launcher.app", home, server, "--update", "--channel=preview")
        out = staged.stdout + staged.stderr
        assert staged.returncode == 0 and "staged" in out and "is not one the package update replaces" not in out, out
        apply = mu._run_exported(root / "OpenBFME Launcher.app", home, server, "--version")
        assert apply.returncode == 0, apply.stdout + apply.stderr
        assert mu._wait_for_version(root, mu.VERSION, timeout=35), "the staged update was not exchanged and confirmed"
        assert mu._wait_for_receipt(home, mu.VERSION) is not None
        assert mu._file_inventory(root) == json.loads(sidecars["manifest-macos.json"])["assets"]["game"]["macos-arm64"]["files"]
    finally:
        server.stop()


@pytest.fixture(scope="module")
def mac_window_app(tmp_path_factory) -> dict:
    """issue #46's launcher on a Mac without an export (an exported launcher ignores --script): the launcher project with a Mac update
    build's build_info.json (macos_updates on, as tools/release/test_macos_update.py exports it), run by a copy of the Godot editor
    binary in <tmp>/Probe.app/Contents/MacOS beside the real bundle swap helper, built from tools/release/macos_bundle_swap.c, so the
    window takes the signed Mac package's paths (MacOSUpdate._local_swap_helper finds the helper beside the running binary)"""
    if sys.platform != "darwin":
        pytest.skip("SKIPPED LOUDLY: the Mac window's download progress test needs macOS (the Darwin swap helper)")
    godot = Path(os.environ.get("GODOT", ""))
    if not godot.is_file():
        pytest.skip("SKIPPED LOUDLY: set GODOT to the Godot 4.7.2 editor executable")
    tmp = tmp_path_factory.mktemp("macwindow")
    macos = tmp / "Probe.app/Contents/MacOS"
    macos.mkdir(parents=True)
    shutil.copy2(godot, macos / "godot")
    # the editor's own signature names its app's resources: Apple Silicon kills a copy outside it until it carries a seal of its own
    subprocess.run(["codesign", "--force", "--sign", "-", str(macos / "godot")], check=True, capture_output=True)
    subprocess.run(["clang", "-arch", "arm64", "-mmacosx-version-min=13.0", "-Wall", "-Wextra", "-Werror", str(mu.SWAP_SOURCE),
                    "-o", str(macos / "openbfme-bundle-swap")], check=True)
    project = tmp / "launcher"
    shutil.copytree(HERE.parents[1] / "launcher", project, ignore=shutil.ignore_patterns(".godot"))
    (project / "build_info.json").write_text(json.dumps({
        "version": VERSION, "display_version": VERSION, "commit": mu.COMMIT, "date": mu.DATE, "repo": REPO,
        "release_key": ed25519.public_key(mu.TEST_SECRET).hex(), "test_build": True, "local_trial": True, "macos_preview": False,
        "macos_updates": True}, sort_keys=True) + "\n", encoding="utf-8")
    for _ in range(2):
        subprocess.run([str(macos / "godot"), "--headless", "--path", str(project), "--import"], capture_output=True, timeout=300)
    return {"godot": macos / "godot", "project": project}


def test_aio_download_progress_keeps_moving_in_the_mac_window(mac_window_app, tmp_path: Path, home: Path):
    """issue #46 where it happened: the Mac launcher with signed Mac updates on (so every refresh of the Play button asked the bundle
    swap helper about the update lock: a process start per downloaded chunk) runs tools/release/test_aio.py's large download in its
    window (tests/aio_progress_test.gd: no frame gap over 1 s, both bars forward to the total, each big file shown mid-download)"""
    import test_aio
    server = ReleaseServer()
    try:
        service = test_aio.Service(server, tmp_path, test_aio.big_packages())
        for p in service.pkgs:
            for f in p["files"]:
                if len(f["data"]) == test_aio.BIG:
                    server.throttle[test_aio.file_path(f["data"])] = 0.0001
        games = tmp_path / "games"
        env = dict(os.environ, HOME=str(home), OPENBFME_GAME_USER_DIR=str(tmp_path / "game-user"))
        cmd = [str(mac_window_app["godot"]), "--headless", "--path", str(mac_window_app["project"]), "--script",
               "res://tests/aio_progress_test.gd", "--", "--offline", f"--aio-base={server.origin}", f"--aio-pins={service.pins_file}",
               f"--progress-target={games}", "--progress-big=" + ",".join(test_aio.BIG_FILES)]
        r = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=400, stdin=subprocess.DEVNULL)
        out = r.stdout + r.stderr
        print(next((ln for ln in out.splitlines() if ln.startswith("AIO PROGRESS frames")), out[-3000:]))
        log = (mu._app_data(home) / "launcher.log").read_text(encoding="utf-8")
        assert f"LAUNCHER OpenBFME Launcher {VERSION} " in log and ", macos-arm64) updates from" in log, log[-3000:]
        assert r.returncode == 0 and "AIO PROGRESS OK" in out, out[-4000:]
        assert "Message queue out of memory" not in out, out[-4000:]
        for p in service.pkgs:
            for f in p["files"]:
                path = games / p["folder"] / f["name"].replace("\\", "/")
                if p["key"] != "bfme1" and (f["language"] == "ALL" or "EN" in f["language"].split()):
                    assert path.read_bytes() == f["data"], path
                else:
                    assert not path.exists(), path
        assert log.count(" downloaded and checked (") == 6, log[-3000:]
    finally:
        server.stop()


def test_icloud_metadata_on_the_previous_package_still_lets_a_rollback_restore_it(exported_macos_launchers, tmp_path: Path, home: Path):
    """issue #47's sibling path: the installed package A carries the metadata, the update to B is exchanged and B's NOTICE is then
    damaged at the root: A (moved aside with its Finder files) verifies on its own and is put back at the root exactly"""
    server = ReleaseServer()
    try:
        root = mu._installed_root(tmp_path / "install", exported_macos_launchers["a"])
        _icloud_metadata(root)
        original = mu._file_inventory(root)
        package = mu._test_package(tmp_path / f"openbfme-{mu.VERSION}-macos-arm64", exported_macos_launchers["b"], mu.VERSION)
        server.publish(mu.VERSION, mu._test_signed_sidecars(package, mu.TEST_SECRET, mu.VERSION))
        staged = mu._run_exported(root / "OpenBFME Launcher.app", home, server, "--install=" + mu.VERSION)
        assert staged.returncode == 0 and "staged" in staged.stdout + staged.stderr, staged.stdout + staged.stderr
        mu._run_exported(root / "OpenBFME Launcher.app", home, server, "--version", env_extra={"OPENBFME_TEST_MACOS_KILL_AT": "after-exchange"})
        assert mu._wait_for_version(root, mu.VERSION, timeout=35), "the exchange did not happen"
        assert mu._wait_for_process_exit(exported_macos_launchers["helper"], _journal(home)["apply_pid"])
        _damage(root, "notice")
        mu._run_exported(root / "OpenBFME Launcher.app", home, server, "--version")
        assert mu._file_inventory(root) == original, "the previous package is not back at the root exactly"
        assert (root / ".DS_Store").exists() and "did not complete and its files are damaged. The previous version v1.2.2 was restored" \
            in _state(home), _state(home)
    finally:
        server.stop()
