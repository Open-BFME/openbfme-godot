# First-run UX gate

Run with Godot 4.7.2, Python 3.12, pytest and pytest-xdist available:

```sh
python -m pytest launcher/tests/test_first_run.py -q
```

Set `GODOT` to the Godot executable if it is not on PATH. The gate imports the
project before testing and rejects script/parse errors as well as failed assertions.
Activate the Python 3.12 environment so shell subprocesses also use that Python.
It uses the existing release stand-in server, signed synthetic packages and isolated
home/config directories. It never needs retail files or contacts a production service.

Each of the eleven scenarios runs at 1280x720 and 2560x1440, at 100%, 125% and
150% canvas scale: 66 scenario/size/scale cases. The driver checks the actual canvas
transform, not just the requested scale. It checks key text, forward/reverse Tab
traversal, directional focus connectivity, icon tooltips and wrapped text. Focused
controls must intersect the viewport after automatic scrolling. It walks Play,
News, Multiplayer (including recovery-code entry), Mods, Game files and Settings.
Error dialogs and the safe Cancel/confirm dialog are also checked, including
Escape, initial focus, containment and focus restoration.

## What the player sees

| Scenario | Visible result and next step | Fixture boundary |
| --- | --- | --- |
| No install | Required games are not found; download action and existing-install guidance remain available. | No game folders. |
| Folders found | "Folders found", scrubbed locations, and "Version and language are checked when the game starts." | Empty synthetic directories with a text marker. |
| Unsupported version | Same folder-discovery message; no false claim of compatibility. | A synthetic marker names the case. Actual version rejection belongs to the game, not this launcher. |
| Wrong language | Same folder-discovery message and game-start validation guidance. | A synthetic marker names the case. No language validation or retail strings are exercised. |
| No network | Check connection and retry the update check. | A bound non-listening loopback socket refuses the release-list connection. The host network stays connected. |
| Lobby down | Check connection, retry Sign in; an installed build can be played offline. | Truncated health response; no authentication request. |
| Update server down | Check connection and retry Check for updates; no raw HTTP error in the status. | Manifest endpoint returns HTTP 503. |
| Disk full | Free space or check permissions, then retry; nothing new installed. | Injects the real install-completion failure callback. Does not fill the host disk or reproduce a filesystem ENOSPC. |
| Corrupt download | Download deleted; use Check for updates and retry. Copy details preserves diagnostic data. | Actual signed fixture download with one byte changed; digest verification rejects it. |
| Stable/Nightly | Stable and Nightly show their own signed release; switching back restores Stable. | Stable and preview fixture releases. |
| Sign-in unavailable | Accounts unavailable on this server; try later or play an installed build offline. | Health response has no account feature; no authentication request. |

The launcher does not inspect retail version or language. Folder-only cases are
deliberately identical and must not be described as in-game validation tests.
Directional coverage uses Godot's focus graph and UI actions; no physical gamepad
or OS accessibility API is tested.

## Screenshots

Godot headless mode cannot render screenshots. Keep assertions headless, then run
the same driver on a virtual display for captures:

```sh
DISPLAY=:99 LAUNCHER_UX_SCREENSHOTS="$(pwd)/workspace/launcher-ux/after" \
  python -m pytest launcher/tests/test_first_run.py -q
```

Screenshot mode captures each initial scenario view, skipping the exhaustive
focus walk (which runs in headless mode). Each PNG must have the requested pixel
dimensions, and the three scale captures must differ. At high scales, overflow
scrolling preserves the existing layout; Tab/directional focus brings off-screen
controls into view rather than dropping them. A compact redesign is not included.

For before captures, archive the base launcher's `launcher/` into a separate
directory, copy only the current `first_run_test.gd` driver into its `tests/`, and
set `LAUNCHER_UX_PROJECT` to that launcher directory. Set `LAUNCHER_UX_BASELINE=1`
to record known baseline UX findings rather than accept them as passing behavior.
Baseline mode still rejects script errors and missing screenshots.

## Regression command

```sh
python -m pytest tools/release/test_launcher.py \
  tools/release/test_launcher_social.py tools/release/test_launcher_oauth.py \
  tools/release/test_launcher_tools.py tools/release/test_onezip.py \
  launcher/tests/test_first_run.py -q -n 4
```

On Linux, unset `DISPLAY` for the headless regression command so native error
alerts cannot block the suite. Set it only for rendered captures.
The Windows-native export/MSVC runner and actual game startup still need their
respective environments; they are not replaced by these synthetic tests.
