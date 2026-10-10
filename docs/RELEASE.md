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
  copy of the console. The log folder is one click away (lane WINCRASH-1): a "Logs" link
  under the version in the menus, an "Open log folder" button in the crash and error
  messages (the file manager opens with that log selected), and `--open-logs` on the
  command line (opens the folder, starts no game). The logs are not written next to the
  executable: the launcher accepts a version folder only with exactly its signed files,
  and Program Files is not writable.
- **Crashes and fatal errors.** Packages use Godot's debug export template: the release
  template writes no native backtrace on a crash, the debug one does, into the session
  log. Godot's Windows handler symbolizes its own executable only (an extension frame is
  "openbfme.windows.template_debug.x86_64.dll+<offset>") and covers the main thread only (a
  crash on another thread writes no dump): stop S-1923, named in every Windows log. Every
  Windows build writes the DLL's PDB (CodeView line tables, build paths remapped, the home
  folder prefixes lld records blanked: `tools/release/pdb_tools.py`). The PDB is PRIVATE:
  `tools/release/archive_symbols.sh` keeps it (Linux: the `.so` and, built with
  `-DOPENBFME_LINUX_DEBUG_FILE=ON`, its `.so.debug`) only in the builder's local symbol
  store, never in a git tree or a release folder, and the package audit and the commit
  gate refuse a `.pdb` or MSF bytes anywhere (the commit gate also refuses any container or
  compressed stream, which it never unpacks, unless `tools/precommit_containers.txt` lists
  its path and sha256); a report's offsets are mapped there
  (`llvm-symbolizer --obj=<dll> --relative-address 0x<offset>`).
  An in-game symbolized report was tried and removed (WINCRASH-1 round 3): code that runs
  inside the fault could not be shown safe (faults while it unwinds, raised versus real
  signals, bounded writes), and a reporter that can swallow or invent a crash is worse
  than none. A run that ends without quitting leaves a marker; the next start names
  that run's log in a message box with "Open log folder" and OK. An error the game cannot
  continue from shows a message box with the error, the version and the log file (and
  "Open log folder"), then quits when it is closed. `--crash-test` (debug builds) crashes
  on purpose two seconds after the start, in native code, for the release checks.
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

Publishing (Discord, GitHub releases) needs the owner's OK, except preview releases: the owner gave a standing approval
(2026-10-09) to publish `v<major>.<minor>.<patch>-preview.<n>` releases automatically, without asking each time (see "Automatic
preview releases" below). A stable (non-preview) release still needs the owner.

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
   - Lane AIO-1 (`docs/AIO.md`): only while the player's opt-in download of
     the game files runs, `bfmeladder.com` and `workshop-files.bfmeladder.com` (HTTPS, no port) are
     allowed as well (`NetPolicy.extra_hosts`). The requests to them are the file lists of the two
     Vanilla packages, sent with the two headers the AIO launcher's own client sends, and the listed files.

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

**Making a release** (after "Making a package"; publishing needs the owner's OK, preview releases excepted: "Automatic preview
releases"):
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
   commands it would run (a draft through the API, the uploads, the read-back, publishing; pre-release for preview). `--execute` runs
   them after the tag is typed again; `--confirm-tag <tag>` replaces the prompt for a preview release without a terminal and must
   equal the manifest's version (see "Automatic preview releases").

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

### Automatic preview releases (AUTOREL-1)

The owner gave a **standing approval** (2026-10-09) to publish preview releases automatically, so testers' launchers pick up new
builds as they become available. It covers the preview channel only; a stable release still needs the owner.

`tools/release/autorelease.sh <archive sha>` runs on the Deck and needs no prompt. The coordinator starts it with an
`archive-legacy-codebase` commit that passed the full gate, after recording that gate:
`tools/release/record_gate.sh <sha> <remote-verify name>`. That script reads the run's RESULT lines (`remote-verify.sh --result`) and
appends `<sha>\t<gate name>\t<date>\tALL PASS` to `~/.local/state/openbfme-release/gated.tsv` only when the run was RESULT ALL PASS
and DONE 0. The run must also be of exactly that commit: verify.sh's `RESULT sha` line, or, in a log from before that line, an
abbreviation that is the prefix of exactly one object of the repository, that commit. It is resolved as an object prefix, never as a
ref, so a tag or branch named like the abbreviation cannot stand in for it.

1. **Checks.**
   - One release at a time (a lock in `~/.local/state/openbfme-release/`).
   - The sha must have a gate record of its own. The record of an ancestor never counts, and neither does ancestry.
   - The sha must be on `archive-legacy-codebase`.
   - Releases only move forward: the sha must descend from the last released archive sha
     (`~/.local/state/openbfme-release/last-released`) and differ from it, whatever `--since` says.
   - If nothing under `engine/`, `godot/`, `launcher/` or `tools/release/` changed since the last released sha, there is no release:
     it prints `AUTORELEASE SKIP` and stops.
   - `--since <archive sha>` only sets where the notes start. The first release has no state and needs it.
   - The release key must be the one `launcher/release_key.pub` names.
   - The sha must contain `MIN_CONTROL`, the commit of the round-3 release scripts. An older sha carries an older
     `publish_release.sh` and older package checks, so it is never released automatically.
   - An open release intent (below) is reconciled first, and while one is open no other sha is accepted.
2. **Sync.** The public `main` gets the sha's tree without `archive/`, with main's `PRIVACY.md` and the README line about the legacy
   code (the sync recipe). Before anything is pushed, that tree passes the privacy audit (`autorelease.py audit-tree`):
   - only the public top-level entries (`PUBLIC_TOP`);
   - no `workspace`, `archive` or `reference` folder anywhere;
   - no file that one of the tree's `.gitignore` rules ignores, even a tracked one;
   - every file passes the commit gate's rules (`tools/precommit.py` `check_file`: retail suffixes, retail-format bytes, developer
     paths);
   - **text only** (round 6): every file synced to the public repository must be UTF-8 text with no NUL byte. Any binary file stops
     the sync: an image, a font, prebuilt data, or a container or compressed file of any kind, wherever its bytes start. That also
     covers a gzip after a prefix, a self-extracting shell stub and a PNG with an archive appended. The only exceptions are listed in
     `tools/release/public_binaries.txt`, each with its path, sha256 and a reason; the list is empty, and today's public tree has no
     binary file. A listed container must also decode completely and pass the audit with its metadata (no ZIP comments, extra fields
     or encrypted members; no gzip file name, comment or extra field; tar members only files and folders, without links, PAX headers
     or owner names; member names through the path rule). Text files with a container suffix (`.zip`, `.gz`, `.tar` ...) are refused
     too. Test fixtures are generated at test time;
   - **file names**: no tracked path (or member name) holds a private path (an absolute home or Windows user folder path, a `home/` or `Users/`
     component) or the user's name as a component.

   It is then one commit on top of `main`, pushed as `sync/<date>-<sha>`, a PR titled `Sync the rebuild: <date> (<summary>)`, merged
   with a merge commit; then the branch is deleted. A merge whose answer was lost is read back. The merge commit must have exactly the
   audited tree. If `main` already has it, there is no PR and `main` is released as it is.
3. **Version and tag.** `v0.3.0-preview.<n>`, n = 1 + the highest `v0.3.0-preview.<n>` tag on origin (the launcher's grammar; other
   tags are ignored). An annotated tag on the merged public commit, pushed (a push whose answer was lost is checked on the remote).
   A commit carries one release tag at most: a second one would make `git describe`, the build's version, pick either. If a failed run
   left a tag on the commit with no release published, the run stops and says to delete that tag by hand first.
4. **Build and test** on JonathanPC's WSL through the jpc scripts (their machine-wide slots, niced; JonathanPC lane `autorel-src`),
   from a clean checkout of the tag:
   - the Linux GDExtension and the Windows DLL (`build_windows.sh --no-export`);
   - `package.sh --platforms linux,windows --windows-dll ...`: game and launchers, the provenance check of both libraries, the
     audits, the self-verification;
   - `test_export.sh` on the Linux package;
   - a native Windows start of the Windows package: `OpenBFME.console.exe --headless --quit-after 400` from WSL interop, in a
     temporary folder under the Windows `%TEMP%`, with the retail folders from the registry values `InstallLocator` reads and a
     private `APPDATA`. It must exit 0, print `GAME screen: MainMenu.apt` and load 4657 templates (`windows_start_check.sh`); the
     folder is removed.

   The packages are copied back to the Deck.
5. **The Deck's checks of what came back.** Everything from here on runs the scripts of a trusted checkout: a fresh worktree of the
   tag made on the Deck. JonathanPC never writes to it. Its tree must be the audited sync tree of the archive sha, so every entry but
   README.md / PRIVACY.md is the archive sha's own object. Every file of its `tools/release/` and `tools/precommit.py` must also be
   byte for byte the running autorelease's own copy, so run autorelease.sh from a checkout whose tools are the released sha's. The
   checks:
   - the returned folder holds exactly the package files;
   - every archive passes the release allowlist;
   - each library (taken out of the returned archives) has one build record naming the tag, the public commit, a clean tree and the
     engine id of the tag's committed `engine/`;
   - `package.sh --verify` rebuilds every archive from the trusted checkout with those libraries and compares them byte for byte
     (the Deck's zlib 1.3.1 and JonathanPC's 1.3 deflate the packages to the same bytes).

   Nothing is signed before all of this has passed.
6. **Manifest and signature** on the Deck: the trusted checkout's `make_manifest.py` and `sign_manifest.py --key
   ~/.config/openbfme-release/release-ed25519.pem`. The key never leaves the Deck: no GitHub Actions, no secret, no JonathanPC.
   GitHub only hosts the source and the assets.
7. **Public metadata** (round 6): the sync commit's message, the PR title and body, the merge subject, the tag message, the release
   title and the release notes go to the public repository too. They are written before anything is pushed, and they must pass the
   privacy rules (`autorelease.py audit-text`). A hit stops the run: nothing is redacted into them. The rules, checked on the text as
   written, without Markdown escapes and NFKC-normalised:
   - no home or Users path, and not the release key's path or folder;
   - no user name (the local account's, the ssh config's users) where it names an account: as a path component (`/deck`, `\deck`,
     `~deck`) or as `user@host`. An ordinary word is fine: "Steam Deck controls now work" passes;
   - no host name (this machine's, the ssh config's hosts);
   - no e-mail address but the noreply ones (`<id>+<login>@users.noreply.github.com`, `noreply@anthropic.com`).

   The fixed attribution lines are not checked.
8. **Notes** (`tools/release/autorelease.py notes`) come from the first-parent commit subjects since the notes baseline, written for
   players: "What's new", "What's fixed" and the known issues of `known_issues.json`. A subject is cut down to plain words (no lane
   ids, binary addresses, stop ids or review verdicts). Commits that change nothing a player runs are left out. A
   `Release-note: <text>` line in a commit message is used as written (`Release-note: Fix: ...` for a fix, `Release-note: none` to
   leave a commit out). A merge whose subject is not plain enough should carry one. Every line is neutralised, `Release-note:`
   text and known issues included:
   - URLs are removed;
   - `@` and `#` are written full-width (GitHub links neither), and `GH-<n>` gets a non-breaking hyphen;
   - the Markdown (the notes, the PR body) escapes every special character, with `& < >` as HTML entities.
9. **Publish**: the trusted checkout's `publish_release.sh <dir> --notes-file <notes> --execute --confirm-tag <tag>`. `--confirm-tag`
   works for preview tags only; a stable release still waits for the typed tag. After every check of the dry run:
   1. The tag on GitHub must be the manifest's commit, and no release may exist for it.
   2. A draft is created through the API, the seven assets are uploaded, and the draft is read back: exactly those assets, uploaded,
      each with the size and SHA-256 of the signed manifest (the checksum file, `manifest.json` and its signature hashed locally;
      `release_assets.py`).
   3. The draft is published and read back again.

   `gh release create` is never used: when publishing fails, it deletes the release it presumes a draft, even one whose publication
   succeeded but whose answer was lost. Any failure or uncertain answer is resolved by reading the release back. **The tool never
   deletes a release** (round 4: a draft can be published between any read and a delete). A draft that cannot be finished is left as
   it is and reported. `--resume-id <id>` finishes a draft an earlier run created: it uploads the missing assets, checks every asset,
   publishes and reads back. A wrong or foreign asset already on the draft stops it for the operator. autorelease.sh then reads the
   published release back once more before it records the state.
10. **Discord**: `bfme-community` has no release announcement command yet; the run logs that and posts nothing.
11. **Report.** A one-line summary (`AUTORELEASE OK|SKIP|FAIL ...`), also appended to
    `~/.cache/openbfme-recover/logs/autorelease-summary.log`, and the log `~/.cache/openbfme-recover/logs/autorelease-<version>.log`.
    The state records the released archive sha.

On a failure it stops at that step and publishes nothing further:
- no release is ever deleted: a draft stays for the next run to resume, or for the operator;
- a pushed tag stays when no release was created for it;
- nothing is retried.

**The release intent.** A run that has started to change GitHub leaves a record of it:
- Before `main` moves or a tag is pushed, `~/.local/state/openbfme-release/intent` records the sha, the version, the audited tree,
  the work folder, the public commit once it is known, and the step (`sync`, `tag`, `tagged`, `publishing`).
- `intent.rid` next to it holds the id of the draft `publish_release.sh` created (`--id-file`).
- A successful run closes the intent when it records the release in `last-released`.

Every real run first reconciles an open intent through GitHub API reads. If a read fails, it stops (fail closed). The intent file
is untrusted: before it records a completion or resumes, every field is checked, and a mismatch stops the run with nothing changed.
- the sha is a commit on the archive branch, has its own gate record, contains `MIN_CONTROL`, and descends from the last released
  sha;
- the version is a `v0.3.0-preview.<n>` above the last released one;
- the tree has exactly the sha's entries (README.md and PRIVACY.md aside);
- the public commit has that tree and is on `main`, and the remote tag is on the public commit;
- for a published release: its `manifest.json` and signature are the package folder's, the manifest names the version, the public
  commit and the repository, the signature verifies under the sha's `launcher/release_key.pub`, and the Linux library's build record
  names the version, the public commit and the engine id of the public commit's `engine/` (stand-in tests skip the library check).

The cases:
- **A published release for the intent's version**, with the assets of the intent's package folder (`release_assets.py`), is
  recorded as released and the intent closes. The run then goes on with the requested sha. This is the case where an earlier run's
  last read-back was lost, and it is what stops a later run from giving older source a newer version.
- **A draft** that is the intent's own (`intent.rid`) is resumed: the packages are rebuilt and checked again, then
  `publish_release.sh --resume-id` finishes the draft. Another draft stops the run.
- **A tag without a release**, or no tag yet, resumes that same sha and version. The tag already on the remote is used, never a
  second one.

While an intent is open, the run resumes the intent's sha whatever sha was asked for. If it was asked for another sha, it exits 3
(`AUTORELEASE RESUMED ONLY`), and the next run takes the requested one. `--dry-run` and `--plan` refuse to start while an intent is
open.

**Operator steps** for what a run cannot repair by itself. In each case it stops with a message naming the case:
- *A published release that cannot be verified* (its package folder was deleted, or the assets differ): compare it with
  `SHA256SUMS-<v>.txt` by hand. If it is right, write `<archive sha> <version> <public commit> <date>` to `last-released` and delete
  `intent` and `intent.rid`. If it is wrong, mark it as a draft or delete it on GitHub, then delete the intent files.
- *A draft the intent did not create* (the answer to its creation was lost), or *a draft with a wrong asset*: check it on GitHub.
  Either delete the draft by hand and run again (the run resumes the release), or finish it by hand.
- *An intent that does not hold* (a field does not match): find out how it changed. Correct the file, or remove `intent` and
  `intent.rid` once the release's real state is known (and record a published release in `last-released` by hand).
- *An earlier run's sync branch on the remote, not merged*: close its PR, delete the branch, and run again. The run makes a new one.
- *Two or more releases for one version*, or *a release tag on the public commit that no intent explains*: remove the extra ones on
  GitHub (`git push origin --delete refs/tags/<tag>` for a tag with no release), then run again.

**The release key.** autorelease.sh signs only with the configured key, `~/.config/openbfme-release/release-ed25519.pem`.
`OPENBFME_RELEASE_KEY` is accepted only when it resolves to exactly that path; any other value is refused with a message that does not
repeat it. So no normalised form of another path can appear in a diagnostic.

**Redaction.** One redactor at the top of autorelease.sh takes every line the script and the helpers it runs print, stdout and
stderr, and every line of its logs. It writes the release key's path as `<release key>`, its folder as `<release key folder>` and
the home folder as `~`. It catches each as given and resolved, Markdown-escaped and URL-encoded. So no echo can leak them,
whatever it prints (intent fields included). No production script writes to the terminal or a log of its own past it.

**What the audits defend against (a stated limitation).** The sync audit, the metadata audit and the redaction defend against
*accidental* inclusion by our own lanes, tools and builds: retail bytes, private data or the key path. They do not defend against a
committer who encodes data on purpose, for example as base64 or hex text, or by steganography. Anyone with commit access could do
that, and the commit gate (`tools/precommit.py`) and review cover the committers. A deliberate leak through text is out of scope.

`--dry-run` runs everything without publishing, and since round 3 it writes nothing to GitHub:
- the sync branch push, the PR, the merge and the tag push are printed as `WOULD RUN: ...`;
- the tag stays local and is deleted at the end;
- the manifest is signed with a throwaway key;
- `publish_release.sh` runs as a dry run.

Every command a real run adds is printed as `WOULD RUN: ...`. It still reads GitHub (the remote's tags and `main`, `gh api user`).

The tests (`tools/release/test_autorelease.py`) run real mode end to end and each failure case against a stand-in:
- a throwaway archive repository holding copies of the scripts;
- a local bare repository as the public remote;
- `gh_stand_in.py` as `gh`, which can lose answers, fail publishing or corrupt an upload;
- a throwaway key;
- `OPENBFME_RELEASE_STANDIN` in place of JonathanPC and the Deck's package rebuild. It is refused with the real repository or a
  remote that is not a local folder.

JonathanPC's gate (`verify.sh`, run by `remote-verify.sh`) also cross-builds Windows (`build_windows.sh --no-export`: the tests, the
peer and the DLL). A change that breaks the Windows build fails the gate.

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
   - natively on a Windows machine, round 1: 5/5, and round 3 (the merged launcher): 13/13;
   - under Wine (Proton 11.0, windowed), round 2: 13/13.
   Natively, run the same script with prebuilt launchers (see "The launcher": Tests) from a test
   account. Then also check by hand:
   - a start from Explorer (no console) applies a staged update;
   - the launcher in a folder the user cannot write to (`Program Files`) reports that self-update needs
     a writable folder, and game updates still work;
   - SmartScreen's reaction to the unsigned executables.
