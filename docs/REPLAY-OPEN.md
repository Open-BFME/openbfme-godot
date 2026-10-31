# Open a downloaded replay or save

Download the `.replay` file from the server admin console (or receive it from
another player). In the launcher, use **Open replay...**, drop one `.replay`
onto its window, or double-click it after enabling the file association.
The launcher needs an installed or bundled game; it does not download or run
anything named by the replay. Offline playback works with an installed build.
**Open save...**, dropped `.obfmesave` files, and save file associations use the
same parser, exact-build selection and launch handoff, with `--load` instead of
`--replay`. No saves are converted.

```
OpenBFME-Launcher -- --replay "downloaded match.replay"
OpenBFME-Launcher -- "downloaded match.replay"
OpenBFME-Launcher -- --load "saved game.obfmesave"
OpenBFME -- --replay "downloaded match.replay"
```

The game also accepts `--replay=path` and `--replay path` without the Godot `--`
separator. Use the separator for launcher options. A bare `.replay` filename
before the separator is accepted too (Finder and dropping onto an executable).
Paths containing spaces or non-ASCII characters remain a single argument.
Multiple files, mixed launcher actions and a missing replay path are refused.

## Which build plays it?

New recordings use OpenBFME replay **format 10**: format 9's recording
`BuildVersion::kVersion` and full Git commit after the existing profile string,
then the UTC `BuildVersion::kBuildDate` (`YYYY-MM-DD`). Saves use **header version
3**, appending the same build date after version 2's flags. This is not the date
the player saved the game. The launcher reads only bounded routing
metadata (1 MiB profile, 128-byte version, 40-byte commit; 256 MiB file limit).
It selects an exact **version and commit**, not the newest build or the user's
normal Play selection. Installed manifests supply the commit; bundled packages
supply their signed inventory's `VERSION`. Package inventory/signature checks
still run before starting the game. No match: the refusal names the needed
version and date, not a bare commit or engine hash. Development builds without
known provenance are not guessed. The in-game Load and replay screens and direct
load/playback refusals say, for example, **Made with Nightly 0.4.12 (2026-10-22).
Open it with that version from the launcher.**
Save routing reads and SHA-256-checks only the bounded HEAD part (1 MiB); the game
remains responsible for validating the entire container and its LOG/SNAP parts.

The game validates the **entire** replay through PR #35's bounded, transactional
reader before mounting anything. It then uses the normal retail mount and setup,
`GameWorld.prepare_replay` and `start_replay` path used by SaveLoad's replay menu.
The existing engine/profile/archive compatibility checks are authoritative;
recording provenance is routing metadata, not permission to bypass those checks.
A direct replay never hands over to a newer installed version on startup.
Invalid input and incompatible profiles produce a message (nonzero exit when
headless), not a native crash. Normal skirmish/campaign/multiplayer startup flags
cannot be combined with replay startup.

Formats **5, 6 and 7** remain readable by the game. They did not record a release
version/commit, so the launcher refuses to guess and points to the build that
made the file in the launcher, without dumping its engine identity. Format **9**
and save headers **1–2** remain readable and routable by version/commit; their
build date is explicitly unavailable, never copied from the save date. Older
games cannot read format 10 / save header 3. Formats 5-8 (8 added the view record) carry no build provenance. Replays are OpenBFME's format, not retail replays.
Relay/interrupted recordings may end at an intact record boundary, as before.
Server recorders using `ReplayWriter` record their build automatically.

## Per-user file associations / opt-out

On the first interactive start of an exported launcher (the unpacked install),
Windows/Linux register the current executable without administrator rights.
Moving/replacing the launcher requires running it once in the new location.
Source/editor runs do not automatically modify associations.

* **Windows:** `HKCU\Software\Classes\OpenBFME.Replay` and
  `.replay\OpenWithProgids`; the open command quotes both the executable and
  `%1`, passing `-- --replay`. When no extension default exists, it is set to
  OpenBFME. An existing default and Windows `UserChoice` are never overwritten;
  use **Open with > OpenBFME** / Windows Default Apps if necessary.
  Saves also register `OpenBFME.Save`, `.obfmesave\OpenWithProgids`, and `-- --load`.
* **Linux / Steam Deck desktop mode:** per-user shared MIME XML
  (`application/x-openbfme-replay`, `.replay` and `OBFMEREP` magic), a dedicated
  `openbfme-replay.desktop` and `xdg-mime` default. Requires `xdg-utils`,
  `shared-mime-info`, and `desktop-file-utils`. Errors name the missing command.
  `$XDG_DATA_HOME` / `$XDG_CONFIG_HOME` are respected. Turning it off restores the
  previous handler when known and removes only OpenBFME's entries and files.
  Saves use `application/x-openbfme-save`, `.obfmesave`, `OBFMESAV` magic and
  `openbfme-save.desktop`; both types keep separate previous-handler state.
* **macOS (Apple Silicon, macOS 13+):** the exported launcher's signed
  `Info.plist` declares the Viewer document type and `org.openbfme.replay` UTI.
  Launch Services discovers it when the app is installed/opened. The app does
  not force itself over another default: Finder **Get Info > Open with >
  OpenBFME Launcher > Change All** selects it. Godot passes Finder documents
  as arguments (opening another instance if already running); drag/drop uses
  the window's file-drop signal.
  The same signed plist also declares `org.openbfme.save` for `.obfmesave`.

Disable **Settings > General > Open .replay and .obfmesave files with OpenBFME**, or before
the first interactive launch run:

```
OpenBFME-Launcher -- --replay-association=off
```

`--replay-association=on` registers it again. The setting persists. Explicit
Open replay / CLI replay / window drag-drop remain available when disabled.
On macOS, off refuses Finder argument opens; choose another default in Finder
to remove the OS-level default. The signed app's plist is never rewritten.

## Architecture and headless coverage

The existing replay menu is SaveLoad.apt in replay mode; its file list uses
`GameWorld.list_replays`. `game.gd::_begin_replay` calls `prepare_replay`, shows
its refusal text, then follows the load-screen/start path. `Recorder` owns the
wire format and playback driver. `CommandLogLoad` replays the embedded log when
loading saves; it is not a second external-file startup path. The `Release`
autoload owns first-run retail mounting, fatal messages/session logging and
normal launcher handover. Direct replay skips only that update handover.
The existing `--auto` / `--advance` test path remains separate.

No test fixture contains retail bytes; fixtures are generated at runtime.

```
cmake --build engine/build --target openbfme_tests
engine/build/openbfme_tests --test-case="replay open:*"
godot --headless --path launcher --script res://tests/replay_open_test.gd
godot --headless --path godot --script res://tests/replay_args_test.gd
GODOT=godot python3 -m pytest tools/release/test_replay_open.py
```

The native tests cover actual playback batches and hashes, formats 5–9,
truncation/mutation/size refusal, unchanged output on failure, and provenance.
The game entrypoint test checks the Load-screen listing and direct save check
against a synthetic old-build save without mounting retail files.
Set `OPENBFME_REPLAY_FIXTURE` or `OPENBFME_SAVE_FIXTURE` to an output path to
retain the synthetic file for the game/launcher integration tests. Launcher tests use a recording process adapter:
production version-selection and argument handoff run, but no OS process or
association is created. Packaging tests inspect the macOS plist declaration.
On Linux, the Python suite also runs 26 real replay/save registration/removal checks in an
isolated XDG sandbox when `xdg-mime`, `update-mime-database` and
`update-desktop-database` are installed. It checks MIME recognition and restores
the sandbox's previous handler; it never changes the developer's file defaults.
Full rendering/simulation still requires the player's retail installations.
