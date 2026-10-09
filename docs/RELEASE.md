# Test packages (closed, opt-in preview)

What a tester gets, and how to make it. Lane RELEASE-1.

## What the game does for a tester

- **Finding the game.** On the first start (no `ROTWK_INSTALL` / `BFME2_INSTALL`, no
  remembered folders) the first-run screen (`godot/scripts/release/first_run.gd`) offers
  the folders `InstallLocator` found and lets the player confirm them or choose them
  with Browse. Where it looks is in `engine/src/Common/InstallLocator.h`: the registry
  values retail reads (RotWK's gi.dat GameRegPath `InstallPath`, BFME2's
  `App Paths\lotrbfme2.exe`), the same values inside Wine / Proton / Lutris / Heroic /
  Bottles prefixes on Linux, and the installers' default folders (inference, S-1560).
  A folder passes when every 2.01 / 1.06 archive is there with its size; the mount
  then checks every md5. The choice is kept in `install-paths.cfg` in the user data
  folder; delete it to choose again. The environment variables still win (developers,
  tests).
- **Version.** `git describe --tags --match "v[0-9]*"` (the legacy `v0.2.0-playtest.*`
  tags are ignored) plus the build day, regenerated on every build
  (`engine/cmake/BuildVersion.cmake`). It is the first line of every log and stands in
  the top right corner of the menus.
- **Logs.** One file per run, `<user data>/logs/openbfme-<date>-<time>.log` (Linux
  `~/.local/share/godot/app_userdata/OpenBFME`, Windows
  `%APPDATA%\Godot\app_userdata\OpenBFME`), the 20 newest kept. Every line has the home
  folder replaced by `~` and the user name by `<user>` (`Common/LogPrivacy.h`: names end
  at any character that cannot continue a name, so `home=<home>;` is caught; the user
  name goes wherever it is a whole token, whatever its length: conservative, a one-letter
  name also replaces that word). The console (stdout / stderr, where the crash handler
  also dumps) goes through the same filter on Linux (`Common/ConsoleFilter.h`), strictly
  line-buffered: nothing reaches the console before its line is complete and scrubbed;
  a partial line is written, scrubbed, only at exit or on a crash (a name that never
  completed becomes `…`), and a line over 8 MiB is written in parts cut where no name can
  be split (`LogPrivacy::safeCut`). Godot's own file log is
  off. Windows has no console filter: the testers' README asks for the log file, not a
  copy of the console.
- **Crashes and fatal errors.** Packages use Godot's debug export template: the release
  template writes no native backtrace on a crash, the debug one does, into the session
  log. A run that ends without quitting leaves a marker; the next start names that run's
  log in a message box. An error the game cannot continue
  from shows a message box with the error, the version and the log file, then quits.
- **Network.** Only the LAN transport (`GameNetwork/Transport.cpp`, UDP) opens sockets;
  `tools/release/net_guard.py` (run by `tools/release/test_release_tools.py`) lexes every
  engine source and shipped script, comments and strings aware, and fails on any other
  reference to a socket / resolver / HTTP API, string naming one, or network header.

## Threat model

The package checks guarantee that **our pipeline never ships retail-format bytes by
accident**: a mistake in the export, the project or the tools (a retail file left in
`godot/`, a resource that embeds game data, a wrong library) is caught before a tester sees
the package. They are **not** a defence against someone who hand-edits a package after it
was built: a detector can always be fooled by an encoding it does not know (review rounds
r1 to r4 showed exactly that). Tampering is handled by **reproducibility and checksums**:

- `package.sh` builds the packages byte for byte reproducibly from a commit (below);
- `package.sh --verify <folder>` rebuilds them from the same commit in a clean folder and
  compares every archive and the checksum file byte for byte; any difference, at any layer
  (archive headers, padding, compression, a member, a byte inside the pack), is a finding;
  every package run verifies itself this way before it reports success;
- `SHA256SUMS-<version>.txt` lists every archive of the run exactly once; testers check
  the archive they downloaded against it, and the published checksum file is what the
  owner signs off.

The content checks (retail signatures, the release allowlist, strict archive formats, the
pack's resource types parsed where their format allows) stay as defence in depth against
our own mistakes.

## Reproducibility

Byte for byte the same for the same commit, the same GDExtension library and the same tools:

- every time stamp is the commit's (`SOURCE_DATE_EPOCH` = the commit time): tar, zip and
  gzip headers, `VERSION`'s date;
- `tools/release/make_archive.py` writes the archives: sorted members, the folder and
  regular files only, owner 0 without names, fixed modes (0755 for the executable and the
  folder, 0644 otherwise), gzip without a file name, zip without extra fields or comments,
  deflate level 9;
- the Godot export is deterministic for the committed tree (`git archive HEAD` of `godot/`,
  imported in a fresh folder; checked by every self-verification): scripts and scenes are
  exported as their committed text (`script_export_mode=0`,
  `editor/export/convert_text_resources_to_binary=false`; the binary scene conversion
  gives every export new random ids);
- `README_TESTERS.txt` is generated from committed files only.

Not reproduced by `package.sh`, recorded instead:

- **the GDExtension library**: built by CMake before packaging; its build record (version,
  commit, dirty flag, engine id) is checked against the commit (`lib_provenance.py`), but
  compilers do not promise identical bytes across machines, so the rebuild of `--verify`
  reuses the same library file; a swapped library makes the archives differ;
- **the tools**: Python's zlib version and the Godot binary / export templates (4.7.2,
  checked against Godot's SHA512 list when installed) are written to
  `BUILDINFO-<version>.txt` next to the archives; a different zlib can compress
  differently, so a verification runs with the same tools.

## Making a package

1. Tag the commit (owner): `git tag v0.3.0-preview.1` (any `v<digits>...` tag).
2. Build at that commit with a clean tree (`cmake --build <dir>`). The library carries one
   build record (version, commit, dirty flag, engine id = the SHA-256 of the engine
   sources); `package.sh` recomputes the engine id from `git archive HEAD engine`
   (`tools/release/lib_provenance.py`) and refuses any library that is not that build.
3. `tools/release/package.sh --out <dir>` (add `--platforms linux,windows
   --windows-dll <WIN-1's cross-built DLL of the same commit>` for Windows). It exports
   `git archive HEAD` of `godot/`, writes `README_TESTERS.txt` (known issues from
   `tools/release/known_issues.json`, each tied to its `docs/STOPS.md` rows), `LICENSE`,
   `NOTICE`, `VERSION`, the archives and `SHA256SUMS-<version>.txt`, and audits every
   file, pack entry and archive member for retail-format bytes
   (`tools/release/audit_package.py`, the commit gate's rules). A dirty tree is refused.
4. `ROTWK_INSTALL=... BFME2_INSTALL=... tools/release/test_export.sh <linux .tar.gz>`:
   start, the crash test (`tools/release/crash_test.py`: SIGSEGV leaves the native
   backtrace in the session log, no home folder or user name in the log or the console)
   and the fresh-user test (`tools/release/fresh_user_test.py`: nothing found, a Wine
   prefix found and accepted, remembered, picked, swapped and empty picks rejected) on
   the packaged executable, and the smoke test on the packaged pack and library.
   The package is checked against an ALLOWLIST (`audit_package.py --release`): exactly the
   release's files (the executable(s), the GDExtension library, `OpenBFME.pck`,
   `README_TESTERS.txt`, `LICENSE`, `NOTICE`, `VERSION`); executables with nothing appended
   after their ELF / PE image; a pack holding only this project's resource types, each in
   its format (scenes and scripts are text, `project.binary` and the UID cache are parsed to
   their end) and with no retail or container signature inside; archives whose
   every header is what `make_archive.py` writes (no link names, extra fields, comments,
   padding bytes or trailing data); a checksum file naming every archive once. Then the
   self-verification rebuilds and compares (see "Threat model"). The output folder must
   be empty; next to the archives come `SHA256SUMS-<version>.txt` and
   `BUILDINFO-<version>.txt`.
5. Keep `known_issues.json` current: a stop that is resolved and removed from
   `STOPS.md` makes the README generator fail until the item is updated.

Publishing (Discord, GitHub releases) needs the owner's OK.

## Windows test checklist (WIN-1 and the owner)

Under Wine (Proton 11.0's `wine`, headless, RELEASE-1 r5) the Windows package
`package.sh --platforms windows` built from WIN-1's `tools/release/build_windows.sh` DLL:
starts and plays a skirmish start (`OpenBFME.console.exe --headless -- --auto --check`);
finds RotWK and BFME2 through the 32-bit registry view (keys written with `reg add /reg:32`;
the first version used `RegGetValueW` + `RRF_SUBKEY_WOW6432KEY`, which found nothing there
and is not in llvm-mingw's headers: now `RegOpenKeyExW` + `KEY_WOW64_32KEY`), checks,
mounts and remembers them, replaces `install-paths.cfg` on a second pick, starts from the
remembered folders, writes the session log under `%APPDATA%\Godot\app_userdata\OpenBFME`
and removes its crash marker on a normal exit. Wine is not Windows; before a Windows
package goes to testers, check on a real Windows 10 / 11 machine (and note the result here):

1. **Registry, WOW64 view.** With RotWK and BFME2 installed by the EA installers or the
   All-in-One launcher, the first-run screen offers both folders, sourced from
   `HKLM\SOFTWARE\WOW6432Node\Electronic Arts\Electronic Arts\...\InstallPath` and
   `...\App Paths\lotrbfme2.exe` `Path` (`RegGetValueW` with `RRF_SUBKEY_WOW6432KEY` from
   the 64-bit game). Also an install recorded only under HKCU.
2. **Unicode paths.** Install folders, the user profile and the user name with non-ASCII
   characters (e.g. `C:\Spiele\Mittelerde`, a user `Zoë`): discovery, Browse, the check,
   `install-paths.cfg`, the mount and the log redaction all handle them.
3. **PID liveness.** Two copies of the game running (a LAN test on one machine): the
   second does not report the first as crashed (`ReleaseInfo.process_alive`,
   `OpenProcess` / `GetExitCodeProcess`); a killed run is reported on the next start.
4. **Config replacement.** Choosing the folders a second time replaces
   `install-paths.cfg` (`std::filesystem::rename` over an existing file), also while
   another program has it open.
5. **Log creation.** The session log appears under
   `%APPDATA%\Godot\app_userdata\OpenBFME\logs`, with `~` for the profile folder in the
   paths it prints (Windows spellings, case-insensitive, forward and back slashes).
6. **DLL export and start.** `package.sh --platforms windows --windows-dll <WIN-1 DLL>`:
   the DLL's build record passes, the export finds both names openbfme.gdextension lists,
   and `OpenBFME.exe` starts on a machine without development tools.
7. **Native dialogs and crashes.** The Browse dialog (native folder picker), the fatal
   error box and the "did not close normally" box appear; a crash writes its backtrace to
   the session log. The console filter does not exist on Windows: `OpenBFME.exe` has no
   console, but the debug export's console wrapper `OpenBFME.console.exe` prints
   unfiltered text (tell testers to attach the session log, not a console copy).
