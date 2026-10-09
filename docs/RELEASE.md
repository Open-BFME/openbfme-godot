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

## The launcher (LAUNCH-1)

Testers download the **OpenBFME Launcher** first (`openbfme-launcher-<version>-linux-x64.tar.gz` /
`-windows-x64.zip`, made by `package.sh` next to the game packages). It checks the project's GitHub
Releases, installs a new game build after verifying it, and starts the game. It is a separate small Godot
project, `launcher/`: no GDExtension, no game files, its own process. A broken game build cannot break
updating: the launcher never loads game code, it only starts `OpenBFME.x86_64` / `OpenBFME.exe` of a
verified version folder, with no arguments. The game's first-run screen still finds the game files.

**Where things live.** Data folder: `~/.local/share/OpenBFMELauncher` (Linux) and
`%APPDATA%\OpenBFMELauncher` (Windows) (`launcher/project.godot`). Inside it:
- `versions/<version>/game/` holds the unpacked game package. Next to `game/` are the signed
  `manifest.json` and `manifest.json.sig` it was installed from. That manifest lists every file of the
  package with its size and SHA-256.
- `downloads/` holds resumable partial downloads.
- `cache/` holds the last releases list and its ETag, and the verified manifests.
- `state.cfg` holds the channel, the user's version pick and launcher versions that failed to start.
- `launcher-update.json` is the self-update's journal (rule 6), present only while an update is in progress.
- `launcher.log` is the launcher's log, with the home folder written as `~`. Every process appends to it
  (the launcher, its update helper, the new launcher), each line flushed and tagged with the process id;
  above 1 MiB it moves to `launcher.old.log`.

The repository is a build-time setting (`package.sh --repo`, default `Open-BFME/openbfme-godot`).

**A release** carries, besides the four archives and `SHA256SUMS-<v>.txt`:
- `manifest.json` (`tools/release/make_manifest.py`). It holds format 2, the product, the repo, the
  version (the tag), the channel (`preview` exactly when the tag is `-preview.<n>`), the date, the
  commit, and per kind (`game`, `launcher`) and platform (`linux-x64`, `windows-x64`) the asset name,
  size and SHA-256.
  - Round 3 adds `files`: every file inside the package, with its size and SHA-256. The signature
    covers it, so the launcher checks unpacked files against signed data whenever it is about to run
    them.
  - The checks happen at install (the unpacked files must be exactly the signed list) and at the
    self-update apply (rule 6).
  - Before Play every file of the version is checked: the same names, sizes and SHA-256, and no other
    file. A changed version is never started. The window offers to download it again; the command
    line says to use `--install=<v>`.
- `manifest.json.sig`: 64 raw bytes, the Ed25519 signature of the manifest's exact bytes.

**Rules the launcher enforces** (`launcher/scripts/core/updater.gd`, `manifest.gd`, `archive.gd`):
1. **Verify before use.**
   - The manifest's signature is checked against the key compiled into the launcher before the
     manifest is parsed.
   - The schema is strict: unknown or missing keys, types, version, channel, date, commit, exact asset
     names, sizes and digests are all checked.
   - The manifest must belong to its release: the same repo, the tag as its version, and a channel that
     matches the pre-release flag.
   - A package is downloaded to `downloads/<asset>.part`, resumed with `Range` and given up after 30 s
     without data. Nothing beyond the manifest's size is ever written. Then the size and SHA-256 are
     checked.
   - It is unpacked into `versions/<v>.partial` and renamed into place. Replacing an installed version
     (the user reinstalls it) first sets the installed folder aside as `<v>.old`, renames the new one in,
     and only then removes `<v>.old`. Every start first runs `Updater.recover()`: it removes unfinished
     `.partial` folders and either finishes (`<v>` verifies) or undoes (`<v>` missing) a replacement, so
     a crash at any point leaves the old or the new complete version (tested by killing each step).
   - Any mismatch deletes the download and installs nothing, with a message naming the file and both
     digests. Nothing is retried silently.
2. **Archive paths.**
   - There must be exactly one top folder named like the asset.
   - These are refused: absolute paths, `..` and `.` components, empty components, backslashes, `:`
     and the characters Windows refuses, reserved device names, control characters, names ending in a
     dot or space, and names equal ignoring case. The device names include `COM0`-`COM9`, `LPT0`-`LPT9`
     and the superscript `COM¹²³` / `LPT¹²³`.
   - Only regular files and folders are allowed. Tar links, devices and extension headers are refused,
     and so are ZIP symlinks, encrypted members and other compression methods.
   - Limits: at most 10000 members and 2 GiB.
3. **Rollback protection.** An automatic update installs only a version newer than every installed
   version. Only two tag forms are release versions: `v<major>.<minor>.<patch>` and
   `v<major>.<minor>.<patch>-preview.<n>` (digits only, no leading zeros, matched as a whole). Any other
   tag is skipped with a log line: under general SemVer, Sol r1 had `v1.0.0-1` installed automatically
   over `v1.0.0--1`. A release is above its previews, and previews are ordered by number. An older one needs the user's pick (*Install this release*, with a
   confirmation, or `--install=<v>`), and Play then starts the pick. The next new build replaces the
   pick.
4. **Channels.** *Stable* offers non-prerelease releases. *Preview* offers all releases (non-draft,
   release-version tags). The two newest installed versions are kept, plus the user's pick.
   *Freeze warning:* when the newest manifest the launcher can get is signed with a date more than 60
   days ago, it shows a plain warning (an old list may be being served). It is not a block: a quiet
   project looks the same. The warning has its own line in the window. In round 2 it was folded into
   the status line, where the automatic install's message replaced it.
5. **Offline / API errors.** The releases list is requested with `If-None-Match`; a 304 does not count
   against the 60 requests per hour. With no network, a 403/429 (the reset time is shown) or any other
   error, the launcher says why updates were skipped. Play still starts the newest verified installed
   version.
6. **Self-update** (`launcher/scripts/core/self_update.gd`). The launcher is one file: its pack is
   embedded in the executable, so the executable at its name is always a complete launcher. The
   release's `launcher` asset for the platform is verified like a game package and staged in
   `<launcher folder>/update/`. A journal (`launcher-update.json`: state, tag, both executables'
   SHA-256, attempts) records the transaction:
   1. At the next start the old launcher first re-verifies the cached manifest of the staged version
      (its signature, the repo and the tag, which must be newer than itself). Every file in `update/`
      must be exactly a signed file of that launcher package; a mismatch or an extra file removes
      `update/` and is logged.
      - Then it writes the journal (`begin`). The new executable's SHA-256 is taken **from the signed
        manifest**, never from the file. Sol r2 found the round-2 code took it from the file in
        `update/`, so a file swapped in after staging ran unchecked.
      - It starts the staged executable as the *helper* (`update/<exe> -- --apply-update=<folder>`) and
        exits. The helper runs only if its own SHA-256 is the signed one.
   2. The helper waits for the old process to end. It makes verified copies `<exe>.old` and
      `<exe>.new`, then renames `<exe>.new` over `<exe>` in one atomic rename. On Linux that is
      `rename(2)`. On Windows it is `MoveFileEx` with `MOVEFILE_REPLACE_EXISTING`, through
      `cmd /c move /y`, because Godot's own rename deletes the target first. Journal: `placed`.
   3. The helper starts the new launcher with `--after-update`. The new launcher sets the journal to
      `confirmed` before anything else. The helper polls for that from its main loop.
   4. With no confirmation within 60 s, or if the new process ends first, the helper stops it. It renames
      a verified copy of `<exe>.old` back over `<exe>` the same way, and records the version as rejected
      (never staged again automatically). Then it starts the previous launcher.
   5. The next start after `confirmed` removes `<exe>.old`, `update/` and the journal, so the previous
      launcher is kept until the new one has started once.

   Every start completes or undoes an interrupted transaction first. It uses the journal state and the
   SHA-256 of the executable at the name:
   - `begin` with the new executable in place counts as placed.
   - `begin` with the old one retries the helper, at most 3 times; then the version is rejected. A
     launcher that cannot start even as the helper ends here.
   - `placed` with the new one confirms.
   - `placed` or `rolled-back` with the old one ends as rejected.

   Sol r1 killed the old two-file swap between its renames and was left with a launcher that could not
   load its project data. The tests now kill each step of this one (`--test-kill-at`, test builds
   only): every later start runs a complete launcher, and the update completes.

   On Windows the swap happens only at a start with a window. Under Wine, a new Godot process did not
   get past its start while its parent handled no window messages (`--headless` has no message loop),
   and the helper waits for its child. A headless start logs that the update waits.

   Limit: a helper killed while it waits for a new launcher that crashes at every start leaves that
   launcher in place. Restore `<exe>.old` by hand (the README says so).
7. **Network and privacy** (`launcher/scripts/net/`).
   - Only HTTPS to `api.github.com`, `github.com` and `<label>.githubusercontent.com`, with Godot's CA
     list and host name check (`TLSOptions.client()`).
   - Every redirect is checked again (at most 5). These are refused: user info, explicit ports, IP
     literals and fragments.
   - The only header about the client is the User-Agent `OpenBFME-Launcher/<version>`. There is no
     telemetry, no account and no cookie.
   - `tools/release/net_guard.py` allows `HTTPClient` only in `http_fetch.gd`, and `OS.create_process`
     only for the game and the self-update. `test_launcher_tools.py` checks that the policy runs before
     every connection.

**Ed25519 in GDScript.** Godot's `Crypto` has no Ed25519 (mbedTLS lacks it). A GDExtension only for the
launcher would need its own cross build per platform and a second toolchain in the update path. So
`launcher/scripts/crypto/ed25519.gd` ports TweetNaCl's `crypto_sign_open` (public domain) to GDScript,
with SHA-512 written over 32-bit halves.
- Shifts are floor divisions, because Godot refuses `>>` on negative ints at run time.
- Decoding follows RFC 8032 strictly: S < L, a canonical public key, and x = 0 with the sign bit set is
  rejected. A public key A or a commitment R of small order is rejected too ([8]P is the neutral
  element). This is defence in depth: Sol r1 showed the identity key accepts a forged signature, but
  the release key has prime order. The 8 torsion points are test vectors in `crypto_test.gd` and
  `test_launcher_tools.py`.
- It is checked against the RFC 8032 7.1 vectors, against `tools/release/ed25519.py` (the RFC's
  reference code) and against OpenSSL's signatures, with bit flips, S + L, other keys and
  non-canonical keys.
- One verification takes about 60 ms. Verification handles only public data, so it is variable-time.

**Making a release** (after "Making a package"; publishing needs the owner's OK):
1. Done (96075fb6, the coordinator's key). Once, on the offline machine: run
   `python3 tools/release/sign_manifest.py --generate-key <path outside every repository>`. It prints
   the public key; commit it as `launcher/release_key.pub` (64 hex digits).
   - The private key never enters the repository, CI or GitHub. `sign_manifest.py` refuses a key
     inside this repository or any git work tree, or one other users can read.
   - The format is OpenSSL's PKCS#8, so `openssl pkeyutl -verify -rawin -pubin` checks signatures
     independently.
2. `tools/release/package.sh --out <dir> --platforms linux,windows --windows-dll <dll>`. This also
   builds the launchers. It fails without `launcher/release_key.pub`; `--no-launcher` gives a game-only
   trial.
3. `python3 tools/release/make_manifest.py <dir>`. The version must be a tag on the commit.
4. On the offline machine: `python3 tools/release/sign_manifest.py --key <key> <dir>/manifest.json`.
5. `tools/release/publish_release.sh <dir>` is a dry run. It checks the manifest against the archives,
   the signature against `launcher/release_key.pub`, the tag and the checksum file, then prints the
   `gh release create` command (`--prerelease` for preview). `--execute` runs it after the tag is typed
   again.

**Tests.**
- `tools/release/test_launcher.py` runs the launcher's command line (`--update`, `--install=`,
  `--play`, `--self-update`, ...) against a local stand-in for GitHub
  (`tools/release/launcher_test_server.py`). It uses a throwaway key and real `make_archive.py` /
  `make_manifest.py` releases. Cases:
  - fresh install and play;
  - update and keep-two;
  - channels;
  - offline;
  - a tampered asset and an oversized one;
  - a tampered manifest;
  - another key;
  - another repository;
  - a truncated download resumed, and a server ignoring `Range`;
  - five bad archive paths;
  - downgrade;
  - 403 rate limit and 304;
  - redirects;
  - the self-update swap and a broken launcher rolled back;
  - a release build ignoring the test options.
- The test options `--api-base=http://127.0.0.1:<port>`, `--trust-key`, `--repo` work only in a
  development run (the editor binary) or a `build_launcher.sh --test-build` package. A release build
  logs that it ignores them.
- `launcher/tests/crypto_test.gd` and `unit_test.gd` cover the crypto, versions, policy, schema and
  archive names. `tools/release/test_launcher_tools.py` covers the release-side tools.
- Windows: `tools/release/launcher_windows_test.py` exports the Windows test launchers and runs them
  windowed against the same server. Under Wine it needs DISPLAY and Proton's
  `files/lib/vkd3d/x86_64-windows/libvkd3d-*.dll` in the prefix's `system32`. Natively, it takes
  prebuilt launchers (`--launchers`, `--key`) and points `APPDATA` into its work folder, which Godot
  follows on native Windows. Under Wine Godot does not follow it, so the prefix's own folder is cleared.

### The updater's threat model

The launcher downloads and runs code, so the release key is its root of trust. Each threat below is
covered by the listed defence:

- **A compromised GitHub account or repository.** An attacker can replace assets, manifests and release
  notes, but cannot sign. A manifest that is not signed by the offline key is refused before parsing.
  An asset that does not match a signed manifest is deleted. A manifest of another repository is
  refused, so a valid manifest cannot be replayed into a fork.
- **Network attacker (MITM, DNS).** TLS with certificate and host name checks, an allowlist of hosts on
  every redirect, and the signature, so even a CA failure cannot introduce code.
- **Replay of an older signed release (rollback).** It is never installed automatically. Only the user
  installs an older version, with a confirmation. A *freeze* (a stale but valid list that hides newer
  releases) cannot be told apart from a quiet project. Manifests carry no expiry, so the launcher stays
  usable offline. A newest manifest signed more than 60 days ago gives a plain warning.
- **Malicious archive paths.** Even inside a correctly signed package, paths are checked as in rule 2.
  The archive is unpacked into a new folder with files written by the launcher, so nothing is followed
  outside it.
- **A partial or cut download.** It is resumed and then checked by size and SHA-256. A wrong byte
  deletes it, and the half-unpacked `.partial` folder is removed. Only a renamed, complete folder whose
  manifest verifies again counts as installed.
- **A full disk.** Write errors fail the download or unpacking with "is the disk full?". The partial
  folder is removed and the installed versions stay untouched.
- **A broken launcher update, or a crash during one.** The new launcher must confirm its start, or it
  is rolled back and not retried. One that cannot start at all is given up after 3 attempts. The
  executable at the name is replaced only by one atomic rename, and the journal lets every start
  finish or undo the transaction. The previous launcher stays as `.old` until the new one has started
  once.
- **A crash while a game version is replaced.** The old folder is kept until the new one is in place.
  `recover()` finishes or undoes the replacement at the next start.
- **The private key.** It is offline, never in the repository or CI, and stored with mode 600.
  - If it is lost: no new releases can be installed by existing launchers. Testers download a new
    launcher by hand.
  - If it leaks: an attacker with the key and the GitHub account can ship code. The response is a new
    key, delivered in a launcher update signed with the old key (the self-update path), and the old key
    removed.
  - There is no revocation list.
- **Out of scope.** Anyone who can write the launcher's or the user data folder can already replace the
  launcher. Code-signing of the executables (Authenticode) does not exist yet.

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
8. **The launcher (LAUNCH-1).** `tools/release/launcher_windows_test.py` covers fresh install and play,
   a tampered asset, an update with a staged launcher, the self-update swap through the helper and the
   removal of the previous launcher, plus the swap killed at each of its 8 steps. Results:
   - natively on JonathanPC, round 1: 5/5;
   - under Wine (Proton 11.0, windowed), round 2: 13/13.
   Natively, run the same script with prebuilt launchers (see "The launcher": Tests) from a test
   account. Then also check by hand:
   - a start from Explorer (no console) applies a staged update;
   - the launcher in a folder the user cannot write to (`Program Files`) reports that self-update needs
     a writable folder, and game updates still work;
   - SmartScreen's reaction to the unsigned executables.
