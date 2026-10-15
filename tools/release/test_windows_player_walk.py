"""Lane RELTEST-1 r2: windows_player_walk.sh against stand-ins (reg.exe, cmd.exe, wslpath, a launcher with the real launcher's CLI rules
for --update / --install=, a game that reaches the main menu), with the releases served from a folder through WALK_RELEASE_BASE.

Lane ONEZIP-1: the release's one zip holds the game and the launcher; the launcher must offer the game beside it without a download
(--list), and the game the walk starts is the unpacked folder's.

Sol r1: the walk ran the launcher with --update, which installs the NEWEST release of the channel, and then looked for the requested
tag: asked for preview.1 while preview.2 exists, it installed preview.2 and failed. It must install exactly the requested tag.
"""
from __future__ import annotations

import hashlib
import os
import subprocess
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
OLD, NEW = "v0.3.0-preview.1", "v0.3.0-preview.2"

STUBS = {
    "reg.exe": r"""#!/bin/bash
case "$4" in InstallPath) v='F:\RotWK\' ;; Path) v='F:\BFME2\' ;; *) exit 1 ;; esac
printf '\r\n%s\r\n    %s    REG_SZ    %s\r\n' "$2" "$4" "$v"
""",
    "cmd.exe": '#!/bin/bash\nprintf "%s\\r\\n" "$WALK_TEST_TEMP"\n',
    "wslpath": '#!/bin/bash\necho "$2"\n',
}

# the launcher's CLI (launcher/scripts/main.gd _run_cli): --update installs the newest release of the channel when it is newer than the
# installed ones, --install=<tag> installs that release; the data folder is %APPDATA%\OpenBFMELauncher
LAUNCHER = r"""#!/bin/bash
data="$(printf '%s' "$APPDATA" | tr '\\' /)/OpenBFMELauncher"; mkdir -p "$data"
log() { echo "2026-10-10T00:00:00 [1] LAUNCHER $*" >> "$data/launcher.log"; }
log "OpenBFME Launcher stand-in"
want=""; list=0
for a in "$@"; do
  case $a in --update) want=$(echo $WALK_TEST_RELEASES | tr ' ' '\n' | sort -V | tail -1) ;; --install=*) want=${a#--install=} ;; --list) list=1 ;; esac
done
here=$(dirname "$0")
if [ $list = 1 ] && [ -z "${WALK_TEST_NO_BUNDLE:-}" ]; then log "installed $(sed -n '1s/^OpenBFME //p' "$here/VERSION") (the game beside the launcher)"; fi
[ -n "$want" ] || exit 0
case " $WALK_TEST_RELEASES " in *" $want "*) ;; *) log "there is no release $want"; exit 1 ;; esac
g="$data/versions/$want/game"; mkdir -p "$g"
cp "$here/OpenBFME.console.exe" "$g/OpenBFME.console.exe"
log "installed $want"
"""

GAME = r"""#!/bin/bash
cfg="$(printf '%s' "$APPDATA" | tr '\\' /)/Godot/app_userdata/OpenBFME/install-paths.cfg"
echo "OpenBFME $(sed -n '1s/^OpenBFME //p' "$(dirname "$0")/VERSION" 2>/dev/null) (2026-10-10) from $(basename "$(cd "$(dirname "$0")" && pwd)")"
[ -f "$cfg" ] && echo "RELEASE game folders: config" || { echo "RELEASE game folders: none"; exit 1; }
echo "GAME object world: ok=true, 4657 templates, 7 playable factions, 1.0 s"
echo "GAME screen: MainMenu.apt"
"""


def publish(rel: Path, tag: str, tamper: bool = False) -> None:
    d = rel / tag
    d.mkdir(parents=True)
    name = f"openbfme-{tag}-windows-x64.zip"
    with zipfile.ZipFile(d / name, "w") as z:
        for f, data, mode in [("OpenBFMELauncher.exe", LAUNCHER, 0o755), ("OpenBFME.console.exe", GAME, 0o755),
                              ("VERSION", f"OpenBFME {tag}\n", 0o644), ("README_LAUNCHER.txt", "readme\n", 0o644)]:
            info = zipfile.ZipInfo(f"openbfme-{tag}-windows-x64/{f}")
            info.external_attr = mode << 16
            z.writestr(info, data)
    digest = hashlib.sha256((d / name).read_bytes()).hexdigest()
    (d / f"SHA256SUMS-{tag}.txt").write_text(f"{'0' * 64 if tamper else digest}  {name}\n")


def walk(tmp: Path, tag: str, tamper: bool = False, env_extra: dict | None = None) -> subprocess.CompletedProcess:
    rel, stubs, temp = tmp / "releases", tmp / "stubs", tmp / "Temp"
    for t in (OLD, NEW):
        publish(rel, t, tamper)
    stubs.mkdir()
    temp.mkdir()
    for n, text in STUBS.items():
        (stubs / n).write_text(text)
        (stubs / n).chmod(0o755)
    env = {**os.environ, "PATH": f"{stubs}:{os.environ['PATH']}", "WALK_RELEASE_BASE": f"file://{rel}", "WALK_TEST_TEMP": str(temp),
           "WALK_TEST_RELEASES": f"{OLD} {NEW}", **(env_extra or {})}
    r = subprocess.run(["bash", str(HERE / "windows_player_walk.sh"), tag], env=env, capture_output=True, text=True, timeout=300)
    assert list(temp.iterdir()) == [], "the temp folder is removed"
    return r


def test_the_requested_tag_is_installed_when_a_newer_release_exists(tmp_path):
    r = walk(tmp_path, OLD)
    out = r.stdout + r.stderr
    assert r.returncode == 0 and "WALK PASS" in out, out
    assert f"LAUNCHER installed {OLD}" in out and f"installed {NEW}" not in out, out
    assert f"--install={OLD}" in out and "AUTOREL: Windows start PASS (exit 0)" in out, out
    assert f"LAUNCHER installed {OLD} (the game beside the launcher)" in out, "the game is offered before any download"
    assert f"OpenBFME {OLD} (2026-10-10) from openbfme-{OLD}-windows-x64" in out, "the game started is the unpacked folder's"
    assert "RELEASE game folders: config" in out


def test_a_launcher_that_does_not_offer_the_game_beside_it_fails_the_walk(tmp_path):
    """lane ONEZIP-1: ready to play with no first download is checked on the published release"""
    r = walk(tmp_path, OLD, env_extra={"WALK_TEST_NO_BUNDLE": "1"})
    out = r.stdout + r.stderr
    assert r.returncode == 1 and "the launcher does not offer the game beside it" in out and "WALK PASS" not in out, out


def test_the_newest_tag_too(tmp_path):
    r = walk(tmp_path, NEW)
    assert r.returncode == 0 and f"LAUNCHER installed {NEW}" in r.stdout, r.stdout + r.stderr


def test_a_launcher_zip_that_does_not_match_sha256sums_stops_the_walk(tmp_path):
    r = walk(tmp_path, OLD, tamper=True)
    out = r.stdout + r.stderr
    assert r.returncode == 1 and "does not match SHA256SUMS" in out and "LAUNCHER" not in out, out
