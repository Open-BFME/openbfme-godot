"""Lane RELTEST-1 r2: windows_player_walk.sh against stand-ins (reg.exe, cmd.exe, wslpath, a launcher with the real launcher's CLI rules
for --update / --install=, a game that reaches the main menu), with the releases served from a folder through WALK_RELEASE_BASE.

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
want=""
for a in "$@"; do
  case $a in --update) want=$(echo $WALK_TEST_RELEASES | tr ' ' '\n' | sort -V | tail -1) ;; --install=*) want=${a#--install=} ;; esac
done
case " $WALK_TEST_RELEASES " in *" $want "*) ;; *) log "there is no release $want"; exit 1 ;; esac
g="$data/versions/$want/game"; mkdir -p "$g"
cat > "$g/OpenBFME.console.exe" <<GAME
#!/bin/bash
cfg="\$(printf '%s' "\$APPDATA" | tr '\\\\' /)/Godot/app_userdata/OpenBFME/install-paths.cfg"
echo "OpenBFME $want (2026-10-10)"
[ -f "\$cfg" ] && echo "RELEASE game folders: config" || { echo "RELEASE game folders: none"; exit 1; }
echo "GAME object world: ok=true, 4657 templates, 7 playable factions, 1.0 s"
echo "GAME screen: MainMenu.apt"
GAME
chmod +x "$g/OpenBFME.console.exe"
log "installed $want"
"""


def publish(rel: Path, tag: str, tamper: bool = False) -> None:
    d = rel / tag
    d.mkdir(parents=True)
    name = f"openbfme-launcher-{tag}-windows-x64.zip"
    with zipfile.ZipFile(d / name, "w") as z:
        for f, data, mode in [("OpenBFMELauncher.exe", LAUNCHER, 0o755), ("README.txt", "readme\n", 0o644)]:
            info = zipfile.ZipInfo(f"openbfme-launcher-{tag}-windows-x64/{f}")
            info.external_attr = mode << 16
            z.writestr(info, data)
    digest = hashlib.sha256((d / name).read_bytes()).hexdigest()
    (d / f"SHA256SUMS-{tag}.txt").write_text(f"{'0' * 64 if tamper else digest}  {name}\n")


def walk(tmp: Path, tag: str, tamper: bool = False) -> subprocess.CompletedProcess:
    rel, stubs, temp = tmp / "releases", tmp / "stubs", tmp / "Temp"
    for t in (OLD, NEW):
        publish(rel, t, tamper)
    stubs.mkdir()
    temp.mkdir()
    for n, text in STUBS.items():
        (stubs / n).write_text(text)
        (stubs / n).chmod(0o755)
    env = {**os.environ, "PATH": f"{stubs}:{os.environ['PATH']}", "WALK_RELEASE_BASE": f"file://{rel}", "WALK_TEST_TEMP": str(temp),
           "WALK_TEST_RELEASES": f"{OLD} {NEW}"}
    r = subprocess.run(["bash", str(HERE / "windows_player_walk.sh"), tag], env=env, capture_output=True, text=True, timeout=300)
    assert list(temp.iterdir()) == [], "the temp folder is removed"
    return r


def test_the_requested_tag_is_installed_when_a_newer_release_exists(tmp_path):
    r = walk(tmp_path, OLD)
    out = r.stdout + r.stderr
    assert r.returncode == 0 and "WALK PASS" in out, out
    assert f"LAUNCHER installed {OLD}" in out and f"installed {NEW}" not in out, out
    assert f"--install={OLD}" in out and f"OpenBFME {OLD} (" in out and "AUTOREL: Windows start PASS (exit 0)" in out, out
    assert "RELEASE game folders: config" in out


def test_the_newest_tag_too(tmp_path):
    r = walk(tmp_path, NEW)
    assert r.returncode == 0 and f"LAUNCHER installed {NEW}" in r.stdout, r.stdout + r.stderr


def test_a_launcher_zip_that_does_not_match_sha256sums_stops_the_walk(tmp_path):
    r = walk(tmp_path, OLD, tamper=True)
    out = r.stdout + r.stderr
    assert r.returncode == 1 and "does not match SHA256SUMS" in out and "LAUNCHER" not in out, out
