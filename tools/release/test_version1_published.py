"""Lane VERSION-1 (owner, 2026-10-14): the releases are plain numbers, "Alpha 0.4.5" = v0.4.5, then v0.4.6, ... and v0.5.0 for a feature
the owner calls huge. Every launcher in testers' hands must update to them with no action of the player.

Not rebuilt from their source: the PUBLISHED Linux launchers, the launcher archives of the GitHub releases as testers downloaded them
(OPENBFME_PUBLISHED_LAUNCHERS=<folder>/<tag>/openbfme-launcher-<tag>-linux-x64.tar.gz; `gh release download <tag> -R
Open-BFME/openbfme-godot -p 'openbfme-launcher-*'`; default workspace/version1/published, git-ignored), patched only in the two values
of their build information that point them at the local stand-in of GitHub (onezip_windows_test.patch_published: the test's release
key, test_build true). Each one starts with NO channel option and an empty data folder, as a tester's launcher on its default channel:
v0.3.0-preview.1 and .2 default to stable, every later one to the pre-release channel (RELTEST-1).

The release page the stand-in serves: what is on GitHub today (the previews and alpha builds as pre-releases, their assets left out:
they are never downloaded, being older than what is offered) and v0.4.5 as autorelease.py publishes it: a full release (prerelease
false, manifest channel stable, format 2 with the launcher archives), its launcher this tree's launcher exported as v0.4.5.

Per published launcher: it installs v0.4.5 and plays it, stages the v0.4.5 launcher and swaps itself for it at its next start. Then
the launcher it became (this tree's, still on its default channel: the old one never stored a pick) takes v0.4.6 and v0.5.0 (format 3:
one package, no launcher archive) by itself. SKIPS LOUDLY without the published archives, Godot 4.7.2 or the export templates.
"""
from __future__ import annotations

import io
import os
import sys
import tarfile
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import test_launcher  # noqa: E402, F401
from onezip_windows_test import patch_published  # noqa: E402
from test_launcher import _godot_ok, _remove_tmp_path, _templates_ok, build_launcher, data, installed, keys, launch, played, server  # noqa: E402, F401
from test_onezip_published import LEXE, one_download_release  # noqa: E402

PUBLISHED_DIR = Path(os.environ.get("OPENBFME_PUBLISHED_LAUNCHERS", REPO / "workspace/version1/published"))
# every launcher published so far (v0.3.0-preview.5 was never published), with the channel it starts on when the player never picked one
PUBLISHED = {"v0.3.0-preview.1": "stable", "v0.3.0-preview.2": "stable", "v0.3.0-preview.3": "preview", "v0.3.0-preview.4": "preview",
             "v0.3.0-preview.6": "preview", "v0.4.0-alpha.1": "preview", "v0.4.0-alpha.2": "preview", "v0.4.0-alpha.3": "preview",
             "v0.4.0-alpha.4": "preview"}
FIRST = "v0.4.5"

pytestmark = pytest.mark.skipif(not _godot_ok(), reason="SKIP the published launchers' plain-version test: Godot 4.7.2 not found (set GODOT)")


def published_launcher(tag: str, into: Path, test_key: bytes) -> Path:
    archive = PUBLISHED_DIR / tag / f"openbfme-launcher-{tag}-linux-x64.tar.gz"
    if not archive.is_file():
        pytest.skip(f"SKIP the published launcher {tag}: {archive.name} is not in the published-launchers folder "
                    "(OPENBFME_PUBLISHED_LAUNCHERS; gh release download)")
    with tarfile.open(archive, "r:gz") as t:
        t.extractall(into, filter="data")
    top = into / f"openbfme-launcher-{tag}-linux-x64"
    exe = top / LEXE
    exe.write_bytes(patch_published(exe.read_bytes(), test_key))
    exe.chmod(0o755)
    return top


@pytest.fixture(scope="module")
def first_launcher(tmp_path_factory, keys):
    """this tree's launcher exported as a test build of v0.4.5: (launcher archive, executable)"""
    if not _templates_ok():
        pytest.skip("SKIP the published launchers' plain-version test: the Godot 4.7.2 Linux export templates are not installed")
    archive = build_launcher(tmp_path_factory.mktemp("new-" + FIRST), FIRST, keys["release"])
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as t:
        exe = t.extractfile(f"openbfme-launcher-{FIRST}-linux-x64/{LEXE}").read()
    return archive, exe


def todays_page(server) -> None:
    """the releases on GitHub today, oldest first, as pre-releases (assets left out: an older release is never downloaded)"""
    for tag in PUBLISHED:
        server.releases.insert(0, {"tag_name": tag, "draft": False, "prerelease": True, "body": f"Notes of {tag}",
                                   "published_at": "2026-10-13T00:00:00Z", "assets": []})


@pytest.mark.parametrize("old", list(PUBLISHED))
def test_a_published_launcher_on_its_default_channel_updates_to_the_plain_releases(tmp_path, server, keys, first_launcher, old):
    archive, new_exe = first_launcher
    top = published_launcher(old, tmp_path / "inst", keys["release"])
    exe = top / LEXE
    r = launch(tmp_path, None, keys["release"], "--version", exe=exe)
    assert r.code == 0 and f"OpenBFME Launcher {old}" in r.out, r      # the published build runs, patched only in its build information
    todays_page(server)
    server.publish(FIRST, one_download_release(tmp_path, FIRST, keys["release"], archive, new_exe))
    assert server.releases[0]["tag_name"] == FIRST and server.releases[0]["prerelease"] is False, "v0.4.5 is a full release"
    # 1. no --channel: the launcher's own default. It installs v0.4.5, plays it and stages the v0.4.5 launcher
    r = launch(tmp_path, server, keys["release"], "--update", "--self-update", "--play", exe=exe)
    assert r.code == 0 and r.has(f"update installed {FIRST}") and r.has(f"launcher update {FIRST} staged") and r.has(f"play {FIRST}"), r
    assert installed(tmp_path) == [FIRST]
    assert played(tmp_path) == f"played {FIRST} from {data(tmp_path)}/versions/{FIRST}/game args: \n"
    # 2. its next start swaps itself for the v0.4.5 launcher (the old launcher's own helper protocol)
    r = launch(tmp_path, server, keys["release"], "--version", exe=exe)
    assert r.has(f"replaced {old} by {FIRST}") and r.has(f"the new launcher {FIRST} confirmed its start"), r
    assert exe.read_bytes() == new_exe and f"OpenBFME Launcher {FIRST}" in r.out
    # 3. the v0.4.5 launcher, on its default channel (stable), takes the next plain releases by itself: v0.4.6, then the huge-feature
    #    v0.5.0, both one-package releases without a launcher archive (format 3)
    nxt = b"the launcher of v0.4.6 (test bytes)"
    server.publish("v0.4.6", one_download_release(tmp_path / "n1", "v0.4.6", keys["release"], None, nxt, fmt=3))
    assert server.releases[0]["prerelease"] is False
    (tmp_path / "played.txt").unlink()
    r = launch(tmp_path, server, keys["release"], "--update", "--play", exe=exe)
    assert r.code == 0 and r.has("update installed v0.4.6") and r.has("play v0.4.6"), r
    assert "played v0.4.6 from" in played(tmp_path)
    # the channel it does this on: stable, the v0.4.5 launcher's default (the old launcher stored no pick)
    r = launch(tmp_path, server, keys["release"], "--update", exe=exe)
    assert r.code == 0 and r.has("up to date: v0.4.6 installed, newest stable release v0.4.6"), r
    server.publish("v0.5.0", one_download_release(tmp_path / "n2", "v0.5.0", keys["release"], None, b"the launcher of v0.5.0", fmt=3))
    r = launch(tmp_path, server, keys["release"], "--update", "--self-update", "--list", exe=exe)
    assert r.code == 0 and r.has("update installed v0.5.0") and r.has("launcher update v0.5.0 staged"), r
    assert set(installed(tmp_path)) >= {"v0.4.6", "v0.5.0"}
    assert (top / "update" / LEXE).read_bytes() == b"the launcher of v0.5.0"
