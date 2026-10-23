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
  crash on another thread writes no dump): stop S-1923, named in every Windows log. The debug
  template titles the window "OpenBFME (DEBUG)"; the `Release` autoload sets it back to the
  project's name, "OpenBFME", once the engine has started (lane UI-4). Every
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
- the order of the files in a pack does not depend on the host's file system (REPRO-1):
  Godot 4.7.2 applies a preset's `include_filter` / `exclude_filter` in the raw directory
  order, which on ext4 is a hash seeded per file system, and that order becomes the pack's.
  The launcher's presets keep both filters empty; its editor plugin
  `launcher/addons/pack_files` skips the tests, itself and `release_key.pub`, and adds the
  font licences in sorted order. The game's presets include one file
  (`tests/object_sweep_baseline.txt`) and exclude none; `test_release_tools.py` and
  `test_launcher_tools.py` hold both to that, and `test_launcher.py` builds the launcher
  package from two stagings listed in opposite orders (tmpfs) and compares the bytes;
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
   `README_TESTERS.txt`, `LICENSE`, `NOTICE`, `VERSION`, and since ONEZIP-1 the launcher's
   executable with its embedded pack and `README_LAUNCHER.txt`); executables with nothing appended
   after their ELF / PE image; a pack holding only this project's resource types, each in
   its format (scenes and scripts are text, `project.binary` and the UID cache are parsed to
   their end) and with no retail or container signature inside; archives whose
   every header is what `make_archive.py` writes (no link names, extra fields, comments,
   padding bytes or trailing data); a checksum file naming every archive once. Then the
   self-verification rebuilds and compares (see "Threat model"). The output folder must
   be empty; next to the archives come `SHA256SUMS-<version>.txt` and
   `BUILDINFO-<version>.txt`.
5. Keep `known_issues.json` current: a stop that is resolved and removed from
   `STOPS.md` makes the README generator fail until the item is updated. Every item has an `area` of the patch notes (`AREAS`).

Publishing (Discord, GitHub releases) needs the owner's OK, except the automatic releases: the owner gave a standing approval
(2026-10-09) to publish `v<major>.<minor>.<patch>-preview.<n>` releases automatically, without asking each time (see "Automatic
preview releases" below), on 2026-10-10 renamed the next builds alpha builds (`v0.4.0-alpha.<n>`, "The alpha builds" below), and on
2026-10-14 asked for plain numbers (`v0.4.5`, "OpenBFME Alpha 0.4.5", "Plain version numbers" below), which the automatic releases now
publish. A v1.0.0 or later release still needs the owner.

## The launcher (LAUNCH-1)

Testers download one archive per platform, the game and the **OpenBFME Launcher** together (see "One archive"
below; until ONEZIP-1 the launcher was its own download, `openbfme-launcher-<version>-linux-x64.tar.gz` /
`-windows-x64.zip`). The launcher checks the project's GitHub
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

**A release** carries, besides the archives (see "One archive") and `SHA256SUMS-<v>.txt`:
- `manifest.json` (`tools/release/make_manifest.py`). It holds format 2, the product, the repo, the
  version (the tag), the channel (`preview` exactly when the tag is `-alpha.<n>` or `-preview.<n>`), the date, the
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
   version. Only three tag forms are release versions: `v<major>.<minor>.<patch>`,
   `v<major>.<minor>.<patch>-alpha.<n>` and `v<major>.<minor>.<patch>-preview.<n>` (digits only, no leading zeros, matched as a
   whole; `-alpha` since RELNAME-1, launchers up to v0.3.0-preview.4 know only `-preview`). Any other
   tag is skipped with a log line: under general SemVer, Sol r1 had `v1.0.0-1` installed automatically
   over `v1.0.0--1`. A release is above its pre-releases; within one release an alpha is below a preview (SemVer's ASCII order), then
   they are ordered by number: v0.3.0-preview.4 < v0.4.0-alpha.1 < v0.4.0-alpha.2 < v0.4.0. An older one needs the user's pick (*Install this release*, with a
   confirmation, or `--install=<v>`), and Play then starts the pick. The next new build replaces the
   pick.
4. **Channels.** *Stable* offers non-prerelease releases. The pre-release channel offers all releases (non-draft,
   release-version tags); its id is `preview` (state.cfg, `--channel=preview`, the manifests). Since VERSION-1 the launcher shows
   stable as *Alpha* (the plain releases, "Alpha 0.4.5"; `--channel=alpha` is stable) and the pre-release channel as *Preview*
   (RELNAME-1 had shown it as *Alpha*). NIGHTLY-1 (owner, 2026-10-21): the two channels are shown as *Stable* and *Nightly*
   (`--channel=nightly` is the pre-release channel), the plain releases keep their "Alpha 0.4.12" name (the game is in alpha), and
   the nightly builds `v<x>.<y>.<z>-nightly.<YYYYMMDD>` (*Nightly builds* below) are on the pre-release channel. The ids never
   changed, so a player's remembered pick and every launcher's default keep working: both channels offer the plain releases. The two newest installed versions are kept, plus the user's pick.
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
   - Lane ONLINECOUNT-1: with "Show how many players are online" on (the default), the lobby's host (`https://play.openbfme.com`,
     or the one `--lobby=` remembered) is allowed for ONE path, `GET /v1/stats` (`NetPolicy.set_lobby`). It answers three counts
     (players online, open games, running games) and nothing else: no sign-in, no token, no cookie, no name. The launcher asks when it opens
     and every 5 minutes while the Play screen is in view, on a thread of its own. Off (`state.cfg`: `[launcher] show_online_count=false`,
     or `--online-count=off`) it never contacts the lobby. The game's menus read the same file (`godot/scripts/online_stats.gd`).
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
   builds the launchers and puts each into its platform's package ("One archive"). It fails without
   `launcher/release_key.pub`; `--no-launcher` gives a game-only trial.
3. `python3 tools/release/make_manifest.py <dir>`. The version must be a tag on the commit.
4. On the offline machine: `python3 tools/release/sign_manifest.py --key <key> <dir>/manifest.json`.
5. `tools/release/publish_release.sh <dir>` is a dry run. It checks the manifest against the archives,
   the signature against `launcher/release_key.pub`, the tag and the checksum file, then prints the
   commands it would run (a draft through the API, the uploads, the read-back, publishing; pre-release for preview). `--execute` runs
   them after the tag is typed again; `--confirm-tag <tag>` replaces the prompt without a terminal for a pre-release or a plain
   alpha-stage release (v0.4.5 to below v1.0.0, `autorelease.py auto-publish`) and must equal the manifest's version (see "Automatic
   preview releases").

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

### One archive (ONEZIP-1)

The owner (2026-10-13): two downloads per platform, a launcher and a game-only archive, are confusing, and the
one without the other does not update. So there is **one archive per platform**, `openbfme-<version>-windows-x64.zip`
and `openbfme-<version>-linux-x64.tar.gz`: the game's files plus `OpenBFMELauncher.exe` / `.x86_64` (one file, its pack
embedded) and `README_LAUNCHER.txt`. `package.sh` builds the launcher first and puts its executable into the game's
package; `audit_package.py --release linux|windows` is that combined set.

**The signed payload inventory (ONEZIP-1 r2).** The archive cannot hold a signature of itself (the manifest names
the archive's SHA-256), so it carries a signature of its FILES: `payload.json` lists every file of the package with
its size and SHA-256, `payload.json.sig` signs it with the release key. `tools/release/seal_package.py seal` adds both on
the machine with the key, after `package.sh --verify` and before `make_manifest.py` (autorelease.sh step 6), and
rewrites the archive with make_archive.py at its own time stamp; `package.sh --verify` compares a sealed package without
the two files and checks the seal under `launcher/release_key.pub`.

**Ready to play.** The launcher treats the game beside itself as installed (`Updater.bundled`) only when that
inventory verifies under its release key for its repository and platform, and the package's `VERSION` names the
signed version. Before Play every file is checked against it, strictly: a changed, missing or extra file, or a
launcher executable that is neither the package's nor a signed launcher update (the release manifest of that
update beside it, `launcher-manifest.json` + `.sig`, written when the new launcher confirmed its start), refuses the
start by name. No case runs unchecked (S-4481 is closed). An update check never downloads the same version again.
The folder is never written by an update: updates go to `versions/<version>/` in the data folder, as before.

**One updater.** The game has no downloader. Started directly, it first checks that the launcher beside it is a
signed one (`godot/scripts/release/launcher_check.gd`: the same inventory and proof, with the launcher's own Ed25519
code, byte for byte; the key and repository are written into the game's pack by package.sh), then runs it without a
window and detached from the game's console, `OpenBFMELauncher --headless -- --game-start=<request folder>`
(`godot/scripts/release/update_handover.gd`, `launcher/scripts/core/game_start.gd`, protocol 2):
1. The launcher answers from the disk alone which version this start plays, by the launcher's unchanged rule
   (the player's pick, else the newest installed, else the game's own folder). For another installed version it
   checks every file against the signed manifest, holds the version under the versions lock, and answers
   `start <version>`; the GAME then starts that version itself, with its own arguments after `--` (user arguments,
   never engine options), reports the process id (the launcher records it as a running game) and closes.
2. Only the game starts a game, and only while it waits: its deadline is a hard 3 seconds from the start of the
   hand-over. With no answer by then it closes the request and starts; a later answer is not read. An answer that is
   not exactly the protocol's is refused by name; every write is checked (a full disk ends the hand-over by name).
3. Then the launcher checks for an update in the background while the game runs: install into the data
   folder, stage its own update. Its state is one line the game shows in the corner of its menus. A newer version
   is used from the next start. A failure is named there and kept for the launcher's window.
4. Not asked: a game the launcher started or installed, a run without a window or with scripted options,
   `--no-update-check`, a folder without the launcher.
The update part runs under the launcher lock; an open launcher window keeps looking after updates itself, and a
window opened during a background download takes it over (the download resumes).

**The launchers published before ONEZIP-1** (v0.3.0-preview.1 .. 6, v0.4.0-alpha.1 .. 4) accept only a format 2
manifest with exactly `assets.game` and `assets.launcher`, each named `openbfme[-launcher]-<version>-<platform>`,
unpacked under a top folder of that name, with exactly the signed files. So:
- the combined archive IS the `game` asset (same name, same top folder, `OpenBFME` at its root): an old launcher
  installs it as its game; the launcher executable inside it is one more file of the version folder;
- every release still carries `openbfme-launcher-<version>-<platform>` (the launcher alone), which those launchers
  update themselves from. Nobody downloads it by hand; the notes say so in one line.
A launcher from ONEZIP-1 on takes its own update out of the combined package (`Manifest.launcher_source`: the
launcher executable is a signed file of the game package) and never fetches the launcher-only archive.
`tools/release/test_onezip_published.py` runs the published builds of every old version against such a release.

**The target page: one archive per platform.** `OPENBFME_RELEASE_LEGACY_LAUNCHERS=0 autorelease.sh` (that is
`package.sh --no-legacy-launchers`, `make_manifest.py --format 3`) leaves the launcher-only archives out: the
release is two archives, `manifest.json`, `manifest.json.sig` and `SHA256SUMS`. Format 3 is format 2 without
`assets.launcher`; the launchers from ONEZIP-1 on read both. A launcher published before ONEZIP-1 refuses such a
release by name ("format 3 (this launcher reads format 2)") and stays on what it has (stop S-4482). That makes
format 2 with the launcher-only archives a release constraint for as long as those launchers must keep updating; time
alone does not prove they have (there is no telemetry), so the switch is the owner's call.

**The launcher replaces itself also without a window** (S-4480 is closed): a game started directly runs it that way,
and on Windows LAUNCH-1 applied a staged launcher only at a start with a window. The swap is killed at each of its 8
steps natively on Windows and under Wine (`onezip_windows_test.py`, "headless self-replacement").

**Checking it.**
- `tools/release/test_onezip.py`: the launcher against the local stand-in of GitHub: the combined package installed and the launcher
  staged out of it (formats 2 and 3), the game beside the launcher, a game start (offline, rate-limited, a newer release, a tampered
  one, updating off, a launcher window open or opened meanwhile, hostile requests), and the game's side of the hand-over.
- `tools/release/test_onezip_published.py` (Linux) and `tools/release/onezip_windows_test.py` (Windows: `--wine`, or
  `onezip_windows_native.sh` from WSL on a real Windows, without a window): the PUBLISHED launcher of every old version updates to a
  one-download release. A published launcher trusts only the real release key and GitHub, so the test changes two values of the build
  information in its embedded pack (the key, `test_build`), nothing else. That is protocol and layout evidence, not a production-key
  end-to-end proof (the real key and GitHub are not in the test).
- `tools/release/onezip_play_test.py`: the packaged game with its window, started directly from the unpacked package: offline, a
  newer release installed in the background, the next start running the newer version, `--no-update-check`.
- `tools/release/windows_player_walk.sh <tag>` after a release: a new Windows player's first start from the one zip.

### Plain version numbers (VERSION-1)

The owner (2026-10-14): "Instead of like alpha 0.3.x 1 preview, can we just do alpha 0.3.x number ... Just make a major version number
when you get a huge new feature." The release after v0.4.0-alpha.4 is **v0.4.5**, shown as "OpenBFME Alpha 0.4.5"; each release takes
the next last number (v0.4.6, v0.4.7, ...), and when the owner calls a feature huge the middle number goes up (`autorelease.sh
--minor`: v0.5.0). SemVer orders every v0.4.0-alpha.<n> below v0.4.5, and v0.4.5 above every v0.3.0-preview.<n>.
- **Full releases.** A plain tag is on the launchers' stable channel (`channel_of`), and every launcher refuses a manifest whose channel
  does not match the release's pre-release flag, so a plain release is published as a full GitHub release (the "Latest release").
  Every launcher offers it: the stable channel shows full releases only, the pre-release channel shows all.
- **No bridge.** The launchers up to v0.3.0-preview.4 read plain tags, and a plain release is the newest entry of the page they read,
  so they install it and take its launcher directly (format 2, with the launcher archives, as long as ONEZIP-1's legacy archives are
  published). A `v0.3.0-preview.<n>` bridge would sort below every plain release, so none is made any more; the bridge rule below
  only guards an alpha build resumed from an intent opened before VERSION-1. An intent opened after a plain tag must be plain.
- **What players see.** The release title and the notes' first line ("OpenBFME Alpha 0.4.5 patch notes"), the launcher's version
  chip, buttons, Settings and notes titles (`Semver.display`), the game's corner text and first-run screen
  (`BuildVersion::displayLine`), README_TESTERS.txt's first line. Logs, `VERSION` and the manifests keep the tag (`v0.4.5`).
- **Evidence.** `tools/release/test_version1_published.py` runs each published Linux launcher (v0.3.0-preview.1 to .4, .6,
  v0.4.0-alpha.1 to .4; patched only to trust the test key and the local stand-in) with no channel option against today's page plus
  v0.4.5: each installs v0.4.5, swaps itself for the v0.4.5 launcher, which then installs v0.4.6 and v0.5.0 on its default channel.

### The alpha builds (RELNAME-1)

The owner (2026-10-10): "instead of preview, make it say alpha build on the next one." The builds after v0.3.0-preview.4 are
**v0.4.0-alpha.1**, alpha.2, ... It is the 0.4.0 line because SemVer orders `alpha` before `preview`: a v0.3.0-alpha.1 would be older
than v0.3.0-preview.4, and no launcher would install it. The GitHub release is named "OpenBFME v0.4.0 Alpha 1", the notes say "an
alpha build for testers", the game's corner text says "unofficial alpha build", README_TESTERS.txt "alpha build", and the launcher
shows the pre-release channel as *Alpha* (its id stays `preview`, see *Channels*). The published previews keep their names.

**The launchers already out there (Sol r1).** Two launcher codebases shipped: the archive's 42f8a8d2 (the launcher of v0.3.0-preview.1
and .2) and 2a422ce6 (.3 and .4). Both take only `v<x>.<y>.<z>[-preview.<n>]`: a `v0.4.0-alpha.<n>` release is skipped with a log
line, so it neither installs the game nor updates the launcher. Both read ONE page of the release list (`releases?per_page=30`, no
further page), install the newest tag of the channel there and stage that release's launcher, swapped in at the next start. So they
reach the alpha builds only through a **bridge**: a `v0.3.0-preview.<n>` whose launcher knows `-alpha`, on that first page.

Every automatic release decides this itself (`autorelease.py release-version`, GitHub reads, fail closed). It reads the release list
as an anonymous launcher gets it (the API's own order, drafts left out, the first 30) and takes the newest tag an old launcher parses.
That release must be a verified bridge (Sol r2: everything an old launcher relies on):
- its `manifest.json` signed under the sha's `launcher/release_key.pub`, passing the launchers' manifest schema (format 2, keys,
  this tag and repository, the tag's channel, every kind and platform with its asset name, size, SHA-256 and a file list naming the
  executable), and the release's pre-release flag matching that channel (a launcher refuses a mismatch);
- all four archives uploaded with their signed sizes, the two launcher archives also with their signed SHA-256 (GitHub's digest,
  or downloaded and hashed when GitHub gives none);
- its commit on `main`, and that commit's `launcher/scripts/core/semver.gd` **run** in Godot ($GODOT) on sample tags: it must take
  `v0.4.0-alpha.1` / `.10` and the previews, refuse `-alpha.01`, `-beta.1`, `-ALPHA.1` and the like, order v0.3.0-preview.4 <
  v0.4.0-alpha.1 < .2 < .10 < v0.4.0 and put the alpha builds on the `preview` channel (a word "alpha" in the source proves nothing). It must also lie above the
last 5 entries of the page. Then this release is the next alpha build. Otherwise this release is the next bridge, `v0.3.0-preview.<n>`
(refused when the sha's own launcher does not know `-alpha`). So:
- the first release after v0.3.0-preview.4 is the bridge v0.3.0-preview.5; the next one is v0.4.0-alpha.1. No alpha build is ever
  published while the old launchers have no bridge on their page;
- **late players**: about every 25 releases a new bridge (preview.6, .7, ...) follows, so a launcher left at preview.1 to .4 for any
  time still finds one among the newest 30;
- right before an alpha build is published, also one resumed from an open intent with its version kept, the bridge is checked again
  (still verified, still on the page with this release added); if it is gone the run stops with nothing published and the intent
  open: put the bridge release back (or publish it again by hand from its package), then run again;
- an open intent's version must be above every version of its line released or tagged on the remote, not only the last released
  one (an alpha build and a bridge are separate lines);
- a bridge sorts below every alpha build, so an alpha-aware launcher never installs it. Its notes say what it is; its game is that
  sha's build (an alpha build numbered as a preview).

`test_launcher.py` builds both shipped launchers from their commits (tree ids pinned) and checks the whole path: alpha.1 skipped, the
bridge installed and its launcher swapped in, alpha.1 then offered and its launcher staged; and the late player (bridge pushed off
the page: nothing found; the rule asks for a new bridge 5 releases earlier; the new bridge is installed). A launcher of preview.1 or
.2 whose player never picked Preview sits on Stable (their default) and saw no build before either.

### Automatic preview releases (AUTOREL-1)

The owner gave a **standing approval** (2026-10-09) to publish preview releases automatically, so testers' launchers pick up new
builds as they become available. It covers the preview channel only; a stable release still needs the owner.

**Candidate first, tag last (RELCAND-1, 2026-10-19).** A release first builds and tests a *candidate*: the exact public commit and the
exact annotated tag it will publish, both made on the Deck and kept local. Only when every build, test and check has passed are they
pushed, in one atomic push, and the artifacts that were tested are then sealed, signed and published as they are, never rebuilt. A
failure before that push leaves nothing public and no version number used: the next run releases the same number. Before RELCAND-1
the sync and the tag were pushed first and the build came after, so every build or test failure burned a number (tags cannot be
deleted): v0.4.7, v0.4.8 and v0.4.10 went that way, and v0.4.11 stopped at the Mac build after Linux, Windows and HUD6 had passed.
The order now:

| step | what | public? |
| --- | --- | --- |
| 1 | checks, the Mac host's readiness, the public metadata audited | no |
| 2 | the public tree audited; reviewed community pull requests merged on GitHub (GHPR-1); the candidate's public commit made locally | only the community merges |
| 3 | the candidate's annotated tag made locally (intent step `candidate`) | no |
| 4, 4m | the Linux / Windows / Mac builds, package.sh, test_export.sh, the Windows start + HUD6, from a checkout of the local tag | no |
| 5, 6, 7 | the Deck's checks (package.sh --verify ...), sealing, the manifests and their signatures, the notes | no |
| 7c | the pre-flight: publish_release.sh's dry run on the signed folder; `candidate.ok` records it | no |
| P | main unchanged? the tag free? the version still next? Then **one** `git push --atomic` of the commit to main and the tag | yes |
| 8 | publish the candidate's folder (`publish_release.sh --execute`), read back | yes |

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
2. **Sync.** The public `main` gets the sha's tree without `archive/` and the paths of its `tools/release/sync_exclude.txt`, with the
   README line about the legacy code and nothing else (the sync recipe, `autorelease.py sync-tree`). The public `main` has no `PRIVACY.md` since its PR #34; a file that exists only on the
   public side is not carried (the sync commit deletes it if it comes back), so a public-only file needs a pipeline change first. Before anything is pushed, that tree passes the privacy audit (`autorelease.py audit-tree`):
   - only the public top-level entries (`PUBLIC_TOP`);
   - no `workspace`, `archive` or `reference` folder anywhere;
   - no file that one of the tree's `.gitignore` rules ignores, even a tracked one;
   - every file passes the commit gate's rules (`tools/precommit.py` `check_file`: retail suffixes, retail-format bytes, developer
     paths);
   - no denylist hit in a file name or a text file (PUBLISH-2: the built-in rules and the operator's private list, below);
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

   It is then one commit on top of `main`, made on the Deck with exactly the audited tree and the message `Sync the rebuild: <date>
   (<summary>)` (the player items, the community credits, the verification line), and **not pushed yet** (RELCAND-1: there is no sync
   branch and no sync PR any more: GitHub's merge commit cannot be known before it exists, and the builds embed the public commit). If
   `main` already has the tree, there is no new commit and `main` itself is the candidate's commit.
3. **Version and tag.** The next plain number (VERSION-1, "Plain version numbers"): v0.4.5 after the alpha builds, then the highest
   plain tag on origin with its last number + 1 (`--minor`: the middle number + 1); it must be above every release tag on origin in
   the launcher's order, or the run stops. Until v0.4.0-alpha.4 it was `v0.4.0-alpha.<n>` with bridges (RELNAME-1), before that
   `v0.3.0-preview.<n>`. The release is named "OpenBFME Alpha 0.4.5" (`autorelease.py title`). An annotated tag on the candidate's public commit, made on the Deck and **pushed last** (step P).
   A commit carries one release tag at most: a second one would make `git describe`, the build's version, pick either. A run that fails
   before step P deletes its local tag: nothing of the version left the Deck. A run that fails after step P leaves its pushed tag on
   origin with no release, and the tag cannot be deleted: the repository's ruleset forbids deleting tags (GH013). It does not need to
   be: the run's release intent stays open, and the next real run (for the same sha or a later one) reconciles it and resumes that same
   version from that tag (see *The release intent*). Never delete the tag.
4. **Build and test** on the build host, from a clean checkout of the (still local) tag: `git describe` is the version, and the
   libraries, VERSION files and build records name the tag and the public commit that step P will push. Through the operator's builder (`OPENBFME_RELEASE_BUILDER`,
   kept outside this repository, PUBLISH-2; it has the interface of the tests' stand-in, `<builder> <checkout> <version> <out dir>`):
   - the Linux GDExtension (`build_linux.sh`: with Godot 4.7's own Linux SDK, buildroot `godot-2023.08.x-4`: GCC 13.2 on a glibc 2.28
     sysroot, run on the host, so it needs GLIBC 2.28 at most and loads wherever Godot 4.7 runs, Ubuntu 20.04+ and Debian 10+: lane DEP-1;
     package.sh writes the need into the Linux package's VERSION file, `requires GLIBC <x.y>`, and the launcher checks the system's GLIBC
     against it before Play: lane GLIBC-1) and the Windows DLL (`build_windows.sh --no-export`);
   - `package.sh --platforms linux,windows --windows-dll ...`: game and launchers, the provenance check of both libraries, the
     audits, the self-verification;
   - `test_export.sh` on the Linux package;
   - a native Windows start of the Windows package: `OpenBFME.console.exe --headless --quit-after 400` from WSL interop, in a
     temporary folder under the Windows `%TEMP%`, with the retail folders from the registry values `InstallLocator` reads and a
     private `APPDATA`. It must exit 0, print `GAME screen: MainMenu.apt` and load 4657 templates (`windows_start_check.sh`); the
     folder is removed.

   The packages are copied back to the signing machine. The Mac package (MACREL-1) is built from the same local tag on the Mac host;
   its checkout there is a fresh repository that fetches the tag alone over git's transport (`file://`): v0.4.11's local clone
   (`git clone --no-checkout <host>/repo.git`) hardlinks the host repository's files and died with "hardlink different from source"
   while another job wrote a pack there (`test_macos_release.py` runs the script against a local stand-in Mac with such a writer).
5. **The signing machine's checks of what came back.** Everything from here on runs the scripts of a trusted checkout: a fresh worktree
   of the tag made on the signing machine. The build host never writes to it. Its tree must be the audited sync tree of the archive sha
   (`autorelease.py sync-tree` of the sha, recomputed). Every file of its `tools/release/` and `tools/precommit.py` must also be
   byte for byte the running autorelease's own copy, so run autorelease.sh from a checkout whose tools are the released sha's. The
   checks:
   - the returned folder holds exactly the package files;
   - every archive passes the release allowlist;
   - each library (taken out of the returned archives) has one build record naming the tag, the public commit, a clean tree and the
     engine id of the tag's committed `engine/`;
   - `package.sh --verify` rebuilds every archive from the trusted checkout with those libraries and compares them byte for byte
     (zlib 1.3.1 on the signing machine and 1.3 on the build host deflate the packages to the same bytes).

   Nothing is signed before all of this has passed.
6. **Manifest and signature** on the signing machine: the trusted checkout's `make_manifest.py` and `sign_manifest.py --key <release
   key>`. The key never leaves the signing machine: no GitHub Actions, no secret, not the build host.
   GitHub only hosts the source and the assets.
7. **Public metadata** (round 6): the sync commit's message, the tag message, the release
   title and the release notes go to the public repository too. They are written before anything is pushed, and they must pass the
   privacy rules (`autorelease.py audit-text`). A hit stops the run: nothing is redacted into them. The rules, checked on the text as
   written, without Markdown escapes and NFKC-normalised:
   - no home or Users path, and not the release key's path or folder;
   - no user name (the local account's, the ssh config's users) where it names an account: as a path component (`/<user>`, `\<user>`,
     `~<user>`) or as `user@host`. An ordinary word that happens to equal an account name is fine;
   - no host name (this machine's, the ssh config's hosts);
   - no e-mail address but the noreply ones (`<id>+<login>@users.noreply.github.com`, `noreply@anthropic.com`);
   - (PUBLISH-2) no hit of the denylist below (the built-in rules and the operator's private list).

   The fixed attribution lines are not checked.
8. **Notes** (`tools/release/autorelease.py notes`) are patch notes in a studio's shape, written for players: a title line, the date and
   the build kind, the install sentence, the "Which file to download" table, then the sections **Added**, **Changed**, **Bug fixes**,
   **Removed** and **Known issues** (a section without lines is left out). Inside a section the bullets sit under a bold area line;
   the areas have a fixed list and order (`AREAS` in `autorelease.py`: Gameplay, Units and heroes, Buildings, AI, Campaign,
   Create-a-Hero, Interface, Audio, Graphics, Performance, Online, Saves, Replays and LAN, Launcher and install, Linux), a line
   without an area goes under General, printed last. "Changed" is for what is neither new nor a fix: balance and behaviour changes,
   performance work, system requirements. The known issues come from `known_issues.json`, each entry with its `area`.

   *Rules for authors.* Every commit that a player would notice carries one `Release-note:` line per change:
   `Release-note: Fixed [Interface]: A left click on empty ground deselects.` The kind is `Added`, `Changed`, `Fixed` or `Removed`,
   the area one of the list in brackets. One change per line, in player words, one or two short sentences: no lane names, stop ids,
   addresses or file names, no ";" joining two changes, under about 200 characters. Things testers reported as bugs are `Fixed`, not
   `Added`. `Release-note: none` leaves a commit out. Still accepted: `Release-note: text` (Added, or Fixed when the words say so),
   `Release-note: Fix: text`, several lines per commit. `autorelease.sh --plan` prints a `NOTES WARNING` line for a note that looks
   like several changes or names an unknown area; fix it before the gate (a warning never fails a release). Without any
   `Release-note:` line a commit that changes what a player runs gets a line made of its subject (cut down to plain words: no lane
   ids, binary addresses, stop ids or review verdicts, Added or Fixed by its words, no area): hand-written lines are better.

   *The section titles.* Launchers up to v0.4.0-alpha.3 read the Play card's lines from the sections titled exactly "What's new",
   then "What's fixed", and print every `#` line as it stands. This tree's launcher reads both title sets. While launchers that read
   only the old titles are installed, the generator writes "What's new" for Added and "What's fixed" for Bug fixes (`--titles bridge`,
   `DEFAULT_TITLES`; Changed, Removed and Known issues are the same in both), so an old launcher's Play card still has its lines.
   Once a release with the new launcher is out and the installed launchers have updated themselves, set `DEFAULT_TITLES` to
   `"studio"` (Added, Bug fixes). `fixtures/notes_view.gd` shows what each launcher reads from a page.

   Every line is neutralised, `Release-note:` text and known issues included (the kind and area words are closed sets: an unknown
   area is General, and never printed):
   - URLs are removed;
   - `@` and `#` are written full-width (GitHub links neither), and `GH-<n>` gets a non-breaking hyphen;
   - the Markdown (the notes, the PR body) escapes every special character, with `& < >` as HTML entities.
9. **The pre-flight and the push** (RELCAND-1). The trusted checkout's `publish_release.sh` runs as a dry run on the sealed, signed
   folder (the manifest against the archives, both signatures under `launcher/release_key.pub`, the local tag on the manifest's commit,
   the privacy gate on the tag's tree, the notes, the name and every asset). `$WORK/candidate.ok` then records the candidate: the sha,
   the version, the tree, the public commit, the tag object, and the SHA-256 of every file of `pkg/` and of `notes.md`. Then, and only
   then, the push:
   - `main` on the remote must still be the commit the candidate was made on (when it already had the tree: must still hold it). If it
     moved (someone pushed, a community pull request was merged by hand), the candidate is **stale**: its commit has the old `main` as
     its parent and is not what `main` would get, and every build embeds that commit, so everything would have to be redone. The run
     stops with `STALE CANDIDATE` and pushes nothing; the next run makes a new candidate on the new `main`, with the same number;
   - the tag must not be on the remote, and (outside a resumed intent) `release-version` must still give this version;
   - the pre-push hook's own check (`public_push_guard.py`) runs first, so a privacy refusal stops the run before anything is sent;
   - one `git push --atomic <remote> <public commit>:refs/heads/main refs/tags/<version>` (only the tag when `main` already had the
     tree): both refs move or neither does. A push that `main` moved under is refused as a non-fast-forward, and the tag with it. An
     uncertain answer is read back from the remote: the candidate's tag object, it went through (then `main` must hold the public
     commit); no tag and `main` moved elsewhere, it can never land (nothing public, the number free); no tag and `main` unchanged
     proves nothing (Sol r1: a server can still be applying a push whose client is gone), so the intent stays at step `pushing` with
     the local tag, and the next run decides.

   The public pull request CI (PUBLICCI-1) runs on that push to `main`. With a required status check on `main` (the owner's settings
   below) the push needs the release account's bypass on the ruleset, as the sync PR's immediate merge did.
10. **Publish**: the trusted checkout's `publish_release.sh <dir> --notes-file <notes> --execute --confirm-tag <tag>` on the candidate's
    folder, nothing rebuilt. `--confirm-tag`
   works for preview tags only; a stable release still waits for the typed tag. After every check of the dry run:
   1. The tag on GitHub must be the manifest's commit, and no release may exist for it.
   2. A draft is created through the API, the release's assets are uploaded, and the draft is read back: exactly those assets, uploaded,
      each with the size and SHA-256 of the signed manifest (the checksum file, `manifest.json` and its signature hashed locally;
      `release_assets.py`).
   3. The draft is published and read back again.

   `gh release create` is never used: when publishing fails, it deletes the release it presumes a draft, even one whose publication
   succeeded but whose answer was lost. Any failure or uncertain answer is resolved by reading the release back. **The tool never
   deletes a release** (round 4: a draft can be published between any read and a delete). A draft that cannot be finished is left as
   it is and reported. `--resume-id <id>` finishes a draft an earlier run created: it uploads the missing assets, checks every asset,
   publishes and reads back. A wrong or foreign asset already on the draft stops it for the operator. autorelease.sh then reads the
   published release back once more before it records the state.
   **The About link (RELTEST-1).** Every release is a pre-release (the launcher's channels depend on the flag: never flip it), so
   GitHub shows no "Latest release" in the sidebar, and `releases/latest` only redirects to the releases list. autorelease.sh
   therefore sets the repository's homepage (the About link) to the new release's page (`gh api --method PATCH repos/<repo> -f
   homepage=<url>`) and reads it back. A failure there is an `AUTORELEASE WARNING` line in the summary, not a failed release: the
   release is already out; set the link by hand. A release finished by reconciling an open intent does not move the link (the next
   release does). The README's Download button links to the releases list, whose top entry is the newest preview.
11. **Discord**: `bfme-community` has no release announcement command yet; the run logs that and posts nothing.
12. **Report.** A one-line summary (`AUTORELEASE OK|SKIP|FAIL ...`), also appended to `autorelease-summary.log` in
    `$OPENBFME_RELEASE_LOGS`, and the log `autorelease-<version>.log` there.
    The state records the released archive sha.

On a failure it stops at that step and publishes nothing further:
- before the push (anything up to the pre-flight, or a stale candidate): the local tag and the candidate's intent are deleted, nothing
  of the version is public (the run says `nothing was pushed: ... <version> is not used and the next run releases it`); only the
  community merges of step 2, if any, stay merged, and the next run takes them as already merged and credits them;
- after the push: no release is ever deleted: a draft stays for the next run to resume, or for the operator; a pushed tag stays when no
  release was created for it (tags cannot be deleted, GH013); the run says so, and running autorelease.sh again resumes the same
  version from that tag through the open intent, publishing the candidate's own folder when `candidate.ok` still verifies;
- nothing is retried.

**The release intent.** A run leaves a record of its candidate and of what it changed on GitHub:
- Before the candidate's local tag is made, `~/.local/state/openbfme-release/intent` records the sha, the version, the audited tree,
  the work folder, the candidate's public commit, the `main` it was made on, and then the tag object. The step is `candidate` (nothing
  public), `pushing` (the push was sent, its outcome not known yet), `tagged` (the tag is on the remote) or `publishing`. Intents of
  the order before RELCAND-1 also had `sync` (a sync branch pushed) and `tag` (the sync PR merged, the tag not pushed yet).
- `intent.rid` next to it holds the id of the draft `publish_release.sh` created (`--id-file`).
- A successful run closes the intent when it records the release in `last-released`.

Every real run first reconciles an open intent through GitHub API reads. If a read fails, it stops (fail closed).
A `candidate` or `pushing` intent is decided by the remote's tag first:
- **not on the remote, step `candidate`**: the push was never sent (the step becomes `pushing` before it is). Its local tag is
  deleted (only when it is the candidate's: on its public commit, its recorded tag object; another tag of that name stops the run), the
  intent is discarded, and the run goes on with the sha it was asked for, under the same version number. This is the run that was
  killed during its build;
- **not on the remote, step `pushing`**: discarded the same way only when `main` has moved elsewhere since (the candidate's push, a
  fast-forward of the `main` it was made on, can never land). Otherwise the very same candidate is pushed again (the same commit and
  tag object, so a push that landed meanwhile makes it a no-op), after every field is checked (`validate_intent`), its local tag is
  its tag object, its commit is one commit on its `main`, and `candidate.ok` verifies (the candidate is known to have passed); a
  mismatch stops the run with nothing pushed. Then it goes on as `tagged`;
- **on the remote, the candidate's own tag object**: the push happened and its answer was lost; the intent goes on as `tagged`. Any
  other tag object stops the run (the intent does not hold).

Then, for the other steps: The intent file
is untrusted: before it records a completion or resumes, every field is checked, and a mismatch stops the run with nothing changed.
- the sha is a commit on the archive branch, has its own gate record, contains `MIN_CONTROL`, and descends from the last released
  sha;
- the version is an alpha build `v0.4.0-alpha.<n>` or a bridge `v0.3.0-preview.<n>`, above the last released version when that is
  of the same line (`autorelease.py intent-version`);
- the tree has exactly the sha's entries (README.md aside; a PRIVACY.md of a tree released before PR #34 is tolerated);
- the public commit has that tree and is on `main`, and the remote tag is on the public commit;
- for a published release: its `manifest.json` and signature are the package folder's, the manifest names the version, the public
  commit and the repository, the signature verifies under the sha's `launcher/release_key.pub`, and the Linux library's build record
  names the version, the public commit and the engine id of the public commit's `engine/` (stand-in tests skip the library check).

The cases:
- **A published release for the intent's version**, with the assets of the intent's package folder (`release_assets.py`), is
  recorded as released and the intent closes. The run then goes on with the requested sha. This is the case where an earlier run's
  last read-back was lost, and it is what stops a later run from giving older source a newer version.
- **A draft** that is the intent's own (`intent.rid`) is resumed: the candidate's folder (or, when `candidate.ok` does not verify, a
  rebuild at the pushed tag, checked again) goes to `publish_release.sh --resume-id`, which finishes the draft. Another draft stops the
  run.
- **A tag without a release** resumes that same sha and version. The tag already on the remote is used, never a second one. When
  `$WORK/candidate.ok` still verifies (the same sha, version, tree, public commit and tag object, exactly the recorded files with their
  SHA-256), that folder is published as it is (`published as it is, not rebuilt`); otherwise the packages are built and tested again at
  the pushed tag (`candidate.ok does not verify`), as every resume did before RELCAND-1.
- An intent of the earlier order at step `tag` (its sync merged, no tag) becomes the candidate: that public commit is built and tested
  and only the tag is pushed. At step `sync` its sync branch, if still on the remote, is left for the operator (below).

While a `tagged` or `publishing` intent is open, the run resumes the intent's sha whatever sha was asked for. If it was asked for another sha, it exits 3
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
- *An earlier run's sync branch on the remote, not merged* (the order before RELCAND-1): close its PR, delete the branch, and run
  again. The run makes a candidate commit instead.
- *A local tag of the candidate's version that is not the candidate's*: delete it by hand (it never reached the remote), run again.
- *Two or more releases for one version*: remove the extra releases on GitHub, then run again.
- *A release tag on the public commit that no intent explains*: the intent of the run that pushed it was lost. Tags cannot be deleted
  (the repository's ruleset, GH013), so write the intent back by hand: `sha=<archive sha>`, `version=<the tag>`, `tree=<the public
  commit's tree>`, `public=<the public commit>`, `work=<a new work folder>`, `step=tagged`. The next run checks every field and
  resumes that version from the tag.

**The release key.** autorelease.sh signs only with the configured key, `OPENBFME_RELEASE_KEY` of the operator's release environment
(PUBLISH-2: the public tree names no private path). It is accepted only as an absolute, normalised path (no relative part, no symbolic
link: `realpath -m` of it is itself); anything else, or nothing, is refused with a message that does not repeat it. So no normalised
form of another path can appear in a diagnostic.

**The operator's release environment (PUBLISH-2).** autorelease.sh and record_gate.sh read every private value from the environment,
with no default in this repository: `OPENBFME_RELEASE_LOGS`, `OPENBFME_RELEASE_KEY`, `OPENBFME_RELEASE_DENYLIST`,
`OPENBFME_RELEASE_BUILDER` (autorelease.sh) and `OPENBFME_RELEASE_LOGS`, `OPENBFME_REMOTE_VERIFY` (record_gate.sh). A missing one stops
the run before anything is read or pushed.

**The public tree and its denylist (PUBLISH-2, after the history scrub of 2026-10-15).** The sync tree is the archive sha's tree without
`archive/` and without every path of the sha's `tools/release/sync_exclude.txt` (the CI configuration and the online service's deploy
folder; kept in step with the CI mirror's exclusion list), with the public README line (`autorelease.py sync-tree`). `audit-tree` and
`audit-text` then refuse any hit, in a file name or in the text of a file, of two rule sets:
- the built-in rules of `autorelease.py` (`BUILTIN_RULES`, generic shapes only): private LAN ranges (192.168/16, 10/8, 172.16/12;
  synthetic addresses in the game's own LAN tests are allowed), tailnet addresses and names, private path forms, AWS and Cloudflare
  account-id shapes and keys, e-mail addresses other than the noreply ones and the reserved example domains, and operational files by
  name (deploy notes, the deploy folder, logs, agent and orchestration files, CI configuration, ops / backup / monitor helpers);
- the operator's private denylist (`OPENBFME_RELEASE_DENYLIST`, kept outside this repository, the CI mirror's rule format): host,
  machine and account names, the owner's identity, the infrastructure's ids and file names. Its `warn` rules fail a release too. A
  missing, empty or malformed list is an error, never "no rules".
Hits are printed masked (only the length of a match), so the audit's output does not repeat what it found. `autorelease.sh --plan`
builds and audits the public tree too, without writing anything.

**One privacy gate (PUBLISH-2 round 2).** Every publishing path goes through `autorelease.py privacy-gate` (its halves `audit-tree` and
`audit-text`): the release sync's tree, the public metadata with the original (unneutralised) note texts, the final notes, and
`publish_release.sh`, which audits the tag's tree (what GitHub's source archives hold), the notes and the release name before anything is
uploaded, also in a dry run, and (round 3) every file the release uploads, each archive member at every depth. All of them need the private denylist; without it they refuse (`--builtin-only` is for checks that publish
nothing, and says so). Further rules of the gate:
- text is checked as written, without Markdown escapes, NFKC-normalised and with the notes' look-alikes (full-width `@` / `#`, `∼`, `∗`)
  turned back;
- metadata must be UTF-8 without NUL bytes: UTF-16 or anything undecodable is refused, never read with replacement characters;
- binary data (a file pinned in `public_binaries.txt` or not, an archive member, a library) is read whole in Latin-1, UTF-8, and UTF-16
  and UTF-32 in both byte orders at every alignment, each decoding also NFKC-normalised with the look-alikes turned back (round 4), so
  a literal of any length is seen; a member named like a text file (`.txt`, `.json`, `.gd` ...) must be UTF-8; every member of a
  container is checked by name (each part of `archive:member` on its own) and content, and a container layer by its metadata (ZIP
  headers, tar headers, every gzip member's header fields): a pinned hash never exempts a file;
- a Godot pack (round 4) holds nothing but checked values outside its entries' names and contents: version numbers below 256, no flag but
  the relative file base, zero reserved words, files right after the header, entry names UTF-8 with zero padding, each entry's MD5 its
  content's and its flags zero; anything else is a finding;
- the release's Godot executables: one that is a Godot export template pinned in `tools/release/godot_templates.txt` byte for byte, for
  the executable name it is pinned for, is upstream and not scanned (Godot's own tables match short private words by chance); the
  launcher, a release template with its pack appended, may differ from that template only in its export region (an ELF's section header
  table, a PE's headers: what Godot's export rewrites), which is scanned whole, and its pack is audited member by member; any other
  executable, or one that differs anywhere else, is scanned whole (round 4). The templates are read from `OPENBFME_GODOT_TEMPLATES` (default: Godot 4.7.2's export templates
  folder); a Godot update replaces the pins;
- speed (PRIVSPEED-1: the v0.4.9 gate ran 1-2+ hours): the same findings, line for line, from fewer regex runs. `tools/release/rulescan.py`
  reads each rule's required literals (and the characters it can never consume) off its parsed pattern and runs the regex only where
  a match can be (a rule it cannot analyse is scanned whole); each distinct binary content is scanned once (sha256), large ones split
  by decoding over worker processes (`--jobs` / `OPENBFME_PRIVACY_JOBS`, default the CPUs up to 8) that start within a memory budget
  (`OPENBFME_PRIVACY_MEMORY_MB`, default half the available memory, at most 16 GB); a failed worker fails the gate. v0.4.9's assets:
  about 75 s on a 16-core CI host (8 jobs), 100-135 s with a 2-3 GB budget; `audit-tree`: 91 s -> 15 s.
  `tools/release/test_rulescan.py` keeps the old scan verbatim as the oracle;
- file names are checked in every form too (as written, NFKC-normalised, the look-alikes turned back);
- no `export-subst` attribute in any `.gitattributes` (GitHub's source archives would not hold the audited bytes);
- no symbolic links; submodules only as listed in `tools/release/public_submodules.txt` (commit, path, url; a submodule bump updates its
  line in the same commit);
- the private list itself has a strict schema (only `id`, `kind`, `pattern`, `severity`, `allow`, `paths`, each of its type; a key
  given twice, in JSON or YAML, is an error; an unknown field's name is never printed);
- no diagnostic prints a rejected value: problem lines and file names are masked in every form of the line and in each of its words on
  its own, and (round 4) a rule with an anchor in every piece of the line between word boundaries (so a rule anchored at a path's start
  or end also masks that path inside a message, spaces in it or not), and a line that any form still matches is
  withheld whole; note warnings name the commit and the note's number, never its text; the list's own parse errors give a position only;
- a draft's name and body are the audited ones: publish_release.sh replaces a resumed draft's when they differ, and checks both before
  and after publishing.

**Redaction.** One redactor (`autorelease.py redact`) at the top of autorelease.sh and of publish_release.sh takes every line the
script and the helpers it runs print, stdout and stderr, and every line of its logs. It masks every match of the built-in and the
private rules (as the diagnostics are masked, above), and fails closed: the list is loaded before anything is printed, and a list it
cannot load stops the run. It writes the release key's path as `<release key>`, its folder as `<release key folder>` and
the home folder as `~`. It catches each as given and resolved, Markdown-escaped and URL-encoded. So no echo can leak them,
whatever it prints (intent fields included). No production script writes to the terminal or a log of its own past it.

**What the audits defend against (a stated limitation).** The sync audit, the metadata audit and the redaction defend against
*accidental* inclusion by our own lanes, tools and builds: retail bytes, private data or the key path. They do not defend against a
committer who encodes data on purpose, for example as base64 or hex text, or by steganography. Anyone with commit access could do
that, and the commit gate (`tools/precommit.py`) and review cover the committers. A deliberate leak through text is out of scope.

`--dry-run` runs everything without publishing, and since round 3 it writes nothing to GitHub:
- the community merges and the one atomic push are printed as `WOULD RUN: ...`;
- the tag stays local and is deleted at the end;
- the manifest is signed with a throwaway key;
- `publish_release.sh` runs as a dry run.

Every command a real run adds is printed as `WOULD RUN: ...`. It still reads GitHub (the remote's tags and `main`, `gh api user`).

The tests (`tools/release/test_autorelease.py`) run real mode end to end and each failure case against a stand-in:
- a throwaway archive repository holding copies of the scripts;
- a local bare repository as the public remote;
- `gh_stand_in.py` as `gh`, which can lose answers, fail publishing or corrupt an upload;
- a throwaway key;
- `OPENBFME_RELEASE_STANDIN` in place of the builder and the signing machine's package rebuild. It is refused with the real repository or a
  remote that is not a local folder.

The reference PC's gate (`verify.sh`, run by `remote-verify.sh`) also cross-builds Windows (`build_windows.sh --no-export`: the tests, the
peer and the DLL). A change that breaks the Windows build fails the gate.

### Nightly builds (NIGHTLY-1)

Owner, 2026-10-21: "build the pulled in PRs every night with patch notes and what to test for for people ... stable vs nightly ... but
the game is still in alpha". Players pick **Stable** (the numbered releases, "Alpha 0.4.12", made by hand with the owner's OK as before)
or **Nightly** (everything merged, built every night) in the launcher's Settings.

- **When.** The operator's user timer `openbfme-nightly.timer` on the coordinator's machine, 03:00 US Eastern (`America/New_York`, so
  daylight saving time is followed), runs the private wrapper, which loads the release environment and runs `tools/release/nightly.sh`.
  That takes the archive branch's head, makes a clean detached checkout of it and runs ITS `autorelease.sh <head> --nightly`. A night
  with nothing new under the release paths since the last nightly is skipped (`AUTORELEASE SKIP`, exit 0).
- **Version.** `v<next plain>-nightly.<UTC YYYYMMDD>` (`autorelease.py nightly-version`): after Alpha 0.4.11 the nightlies are
  v0.4.12-nightly.20261022, .20261023, ..., all below v0.4.12, which replaces them on both channels. One per date; a nightly is never
  below an existing release tag. Shown as "Nightly 0.4.12 (2026-10-22)". Launchers from before NIGHTLY-1 do not parse the word
  `nightly` and skip those releases (they stay on the plain releases until they update themselves).
- **On GitHub.** Each nightly is its own pre-release; after publishing only the newest 7 nightly releases are kept
  (`nightly_notes.py prune`; the tags all stay, GH013). This is required: every launcher reads one page of 30 releases, so piled-up
  nightlies would push the stable releases off it. A nightly also moves the public main (its candidate commit, as a release does) and
  lands the reviewed community pull requests whose ports it carries (GHPR-1). A public main that already carries a release tag gets a new
  commit for the next release, also with the same tree (two release tags on one commit make the build's version ambiguous).
- **Gate.** No gate record: the full gate is the merge queue's job. The nightly's candidate is checked as it is built (a failure
  anywhere publishes nothing, and the next run uses the same number): the Linux, Windows and macOS builds and packages with their audits;
  `test_export.sh` on the Linux package (a skirmish start, the smoke, crash and fresh-user runs); the launcher's unit and
  layout tests (`tools/release/test_launcher.py -k 'unit_rules or layout'`; the whole file is the merge gate's) and `godot/tests/end_test.gd` (a skirmish to its end) on the build host; the Windows start and the
  help-box run of the Windows package (under Wine on a Linux host, judged by `windows_start_check.sh` / `windows_hud6_check.sh`); the
  Deck's package checks; the metadata, tree and asset privacy audits with the private denylist. The builder is the operator's
  (`OPENBFME_RELEASE_BUILDER`, called with `AR_NIGHTLY=1`); it runs on the dedicated release CI host, never on a player's PC.
- **State.** `$OPENBFME_RELEASE_STATE/nightly/` holds the nightly's intent, `last-released` and `releases.log`, so a stable release's
  version, notes baseline and intent are untouched; the lock and the community pull request lists are shared. The first nightly's notes
  start at the last stable release.
- **Notes** (`tools/release/nightly_notes.py`): the first-parent changes since the last nightly, and for each the community pull requests
  it carries (a `(cherry picked from commit X)` line whose X GitHub links to a pull request of the repository: its number and author).
  A change with `Release-note:` lines is described by exactly those (`none` hides it), and its `Test:` lines are its test steps. For the
  rest, the operator's model command (`OPENBFME_NIGHTLY_LLM`) writes one plain sentence and 1 to 6 concrete test steps per change, and
  per community pull request. Each text is checked (the patch notes' jargon rule, no markup, mentions, references or links, the length
  limits); a draft that fails is sent back once with the problems, then the run fails. The page: the build, how to get it, the download
  table, **Thanks to our community** (each pull request: "Thanks to @login (#n): ..."), What's new / Changed / What's fixed (community
  first, credited), **What to test** (numbered, community first), how to report a problem, the known issues. The logins and numbers come
  from GitHub's record; they are the only mentions and references on the page. The whole page passes `audit-text` with the private
  denylist like every published text.
- **On a failure** the wrapper leaves `logs/nightly-<date>.done` with the exit code and the coordinator's unseen list shows
  `NIGHTLY FAILED`. A dry run (`nightly.sh --dry-run`) builds and checks everything and publishes nothing.

### The Mac package (MACREL-1)

Since MACREL-1 every automatic release also carries the macOS package for Apple Silicon: `openbfme-<v>-macos-arm64.zip`, the game and the
launcher as two ad-hoc signed apps, macOS 13 or later, an unstable preview (owner, 2026-10-15/16: no Intel Macs, no Rosetta). Macs update
themselves from it: the launcher's macOS mode (mac-release-1, `launcher/scripts/core/macos_update.gd`) reads `manifest-macos.json`, never
`manifest.json`. The other launchers never read the sidecar.

- **Before anything is pushed** (step 1), `tools/release/build_macos_release.sh --check` asks the Mac build host (`mac`, the owner's M5;
  `OPENBFME_RELEASE_MAC_HOST`) over ssh whether it has its CI environment, the build tools, Python 3.10+, Godot 4.7.2 and the pinned
  export templates. If it does not answer (asleep, lid closed, off the network) or lacks something, the run stops at `mac-check`. That
  is a named failure, and nothing has been pushed or published yet.
  `OPENBFME_RELEASE_SKIP_MAC=1`, set explicitly, releases without the Mac package. The log and the summary say so, and the release
  then carries a signed `manifest-macos.json` that names no Mac package: the Mac launchers see no update instead of guessing. Any other
  value is refused.
- **The build** (step 4m) runs after the Windows / Linux one: the tag is pushed to the host's private CI repository and cloned there at
  the tag; `tools/release/package_macos.py` makes a fresh arm64 build, the Godot export, ad-hoc signing and the strict audit in the
  host's slot 0; the package folder comes back (the zip, its `.sha256`, its inventory and `README-MAC.txt`). The clone and the tag are
  removed from the host. Nothing is signed there: the release key never leaves the Deck.
- **The Deck checks it** (step 5) with the trusted checkout's `make_macos_manifest.py --verify-package`:
  - exactly the package folder;
  - the strict `macos-arm64` allowlist;
  - its checksum;
  - `VERSION` naming the tag and the public commit;
  - the Mac launcher's `build_info.json` trusting `launcher/release_key.pub` and updating from the target repository. A Mac launcher
    with another key or repository would refuse every update.
- **The seal** (step 6, MACPLAY-1). The zip joins the package folder before `seal_package.py` runs, so it is sealed like the Windows /
  Linux packages: `payload.json` (every file of the package with its size and SHA-256, platform `macos-arm64`) and its signature under
  the release key are added inside the zip, beside the two apps (their code seals are unchanged). The Mac launcher starts the game
  beside it only when that inventory verifies, every signed file matches and both apps' code seals verify (`codesign --deep
  --strict=symlinks`: MACFIX-1, every strict check but "sideband", which refused the Finder / iCloud Drive extended attributes of a
  package unpacked into an iCloud Desktop, issue #47; Finder's `.DS_Store`, `Icon\r` and `._*` files are no part of a package either);
  a launcher with no verified package beside it installs the signed package into `versions/<v>/game` itself, like the other platforms.
  The release tooling keeps the bare `--strict` on the bytes it makes.
- **The sidecar** (step 6). The sealed zip is listed in `SHA256SUMS-<v>.txt`. `make_macos_manifest.py` writes
  `manifest-macos.json` (format 3, the "macos" schema: one `macos-arm64` game entry with every file's size and SHA-256), with the
  version, repository, commit and date of `manifest.json`. It is signed with the same key in a separate signature,
  `manifest-macos.json.sig`. `manifest.json` stays byte for byte the manifest of the Windows / Linux packages, so the released
  launchers read what they always read.
- **Publishing.** `publish_release.sh` requires both sidecar files or neither, verifies the sidecar's signature and checks it with
  `make_macos_manifest.py --check`. The check rebuilds the sidecar from the folder's Mac package, or the no-entry one when there is
  none. It also re-checks the Mac launcher's key and repository, that the checksum file lists the zip with its digest, and (MACPLAY-1)
  that the zip is sealed under the manifests' key with an inventory of exactly its files. Only then
  does it upload the zip and the two sidecar files with the other assets, and `release_assets.py` checks all of them back.
  The notes' download table gets the Mac row (`autorelease.py notes --mac`).
- **Tests.** `test_autorelease.py` covers:
  - the Mac assets and the separate signature;
  - `manifest.json` unchanged;
  - an unreachable or unready host, which stops before anything is pushed;
  - `OPENBFME_RELEASE_SKIP_MAC=1` said in the log and the summary;
  - every Mac package fault the Deck refuses;
  - every tampered sidecar `publish_release.sh` refuses;
  - the dry run signing the sidecar with the throwaway key.

  The stand-in build (`OPENBFME_RELEASE_STANDIN/build-mac`) returns a package that passes the strict audit, and
  `OPENBFME_RELEASE_MAC_SSH` (stand-in mode only) replaces ssh for the readiness check.

### Release builds on GitHub Actions (MACACT-1)

Since MACACT-1 a release can have its platforms built on GitHub's own runners instead of (macOS) or beside (Linux, Windows) the build
hosts, so a release no longer needs the owner's MacBook awake on a known address (a nightly shipped without macOS on 10-21/22 for that).
Each platform has its own switch; **the default is `host` for all three** until the owner switches:

| variable | `host` (default) | `actions` |
| --- | --- | --- |
| `OPENBFME_RELEASE_MAC_BUILDER` | the Mac build host (step 4m, `build_macos_release.sh`) | the package is built on `macos-15` (arm64) by `package_macos.py`, the Mac host is not asked |
| `OPENBFME_RELEASE_LINUX_BUILDER` | the build host's GDExtension | GitHub builds it too (`build_linux.sh`, ubuntu-24.04) and the build host's, inside the returned package, must be byte for byte GitHub's |
| `OPENBFME_RELEASE_WINDOWS_BUILDER` | the build host's GDExtension | the same with `build_windows.sh --no-export` (ubuntu-24.04, the pinned llvm-mingw) |

The Windows DLL is built on `ubuntu-24.04`, not on a `windows-2022` runner: `build_windows.sh` is a Linux-hosted cross build with a pinned
llvm-mingw (`...-ubuntu-22.04-x86_64`), and only the same toolchain binary gives bytes the release can compare with the build host's; a
Windows-hosted llvm-mingw is another compiler binary with other paths.

Switch by setting the variable in the operator's release environment (`release.env`), e.g.
`export OPENBFME_RELEASE_MAC_BUILDER=actions`; any value but `host` / `actions` stops the run before anything happens.
`OPENBFME_RELEASE_SKIP_MAC=1` stays the one way to ship without macOS (it wins over the Mac builder).
`OPENBFME_RELEASE_ACTIONS_TIMEOUT` (default 7200 s) bounds one GitHub build.

**The flow** (`autorelease.sh`, `tools/release/actions_build.py`, `.github/workflows/release-package.yml`):

1. `actions-check` (with the other step 1 checks, before anything is pushed): GitHub knows the workflow and it is active. GitHub dispatches
   only a workflow it has seen: one that was on `main` once, or registered by a push (MACACT-1 registered it once from a probe branch on
   2026-10-22; the first release after this lane carries it to `main` for good).
2. `actions-dispatch` (step 4a, after the candidate's tag is made locally and the trusted checkout is checked): the candidate's public
   commit, whose tree passed the privacy audit (step 2), the `.github` policy and the pre-push guard, is pushed to
   `refs/heads/staging/<version>` of the public repository (a leftover from a killed run is replaced; both pushes carry a lease, so a
   concurrent push is never overwritten). Only the commit goes: the tag stays on the Deck until step P. GitHub can only build what it
   has, so this push is the release's second write to GitHub before the candidate passed (after the GHPR-1 merges); it makes public a
   commit that will be public anyway, and a dry run pushes it too (and deletes it). Then one `workflow_dispatch` per platform, with the
   commit, the version and a fresh 32-hex request id, through the REST API with `return_run_details`: GitHub answers with the id of the
   run this very dispatch created, recorded in `$WORK/actions.json`. Nothing is looked up by name or time.
3. The runs build while the build host builds (step 4). `actions_build.py collect` then waits for each recorded run (step 4m for macOS,
   step 5x for Linux / Windows) and accepts it only when: it is the recorded run; event `workflow_dispatch`; the workflow file
   `release-package.yml`; on `staging/<version>` at the candidate commit (`head_sha`); actor and triggering actor the release account
   (`gh api user`); attempt 1 (a re-run could upload other files); the title naming platform, version and request; concluded
   `success`; only the platform's job ran. It must have exactly one artifact, named `<stem>-<request>`, made by that run at that commit,
   not expired; the downloaded ZIP's SHA-256 and size must be GitHub's recorded digest and size; the ZIP must hold exactly the platform's
   files, plain names, no folders. A failed, refused or timed-out run (cancelled at the timeout) **fails the release**: no fallback to a
   host (CLAUDE.md, "no fallbacks").
4. The Deck then checks the result like a host build. macOS: the exact package set, the strict audit, the checksum, `VERSION` naming the
   tag and the public commit, the launcher's key and repository (`make_macos_manifest.py --verify-package`). Linux / Windows
   (`actions-compare`): the library inside the build host's returned package must be byte for byte GitHub's, else the release fails
   ("a GitHub build can't silently differ from ours"; it also catches a build host that differs from GitHub). The packages, `test_export.sh`
   and the native Windows start stay on the build hosts: they need the game files, which never go to GitHub.
5. `actions-unstage`: the staging branch is deleted (lease on the candidate commit: a branch someone moved is left and named, and the
   release fails). On any failure the cleanup cancels unfinished runs, best effort (a dispatch whose answer was lost is found by the request id it
   recorded first) and deletes the branch too. The staging push never carries a tag (`--no-follow-tags`, whatever `push.followTags` says).
6. Only then the seal, the manifests and the signatures, all on the Deck (step 6), the pre-flight (7c) and the push of `main` and the tag
   (P). Nothing is published, and nothing is signed, before every GitHub build was verified.

**The security model.** What runs on GitHub: the public workflow on the public commit (exactly the tree that is published), on GitHub-hosted
runners, with `permissions: contents: read`, no secrets, no caches (a fresh build; no cache another workflow could have written), actions
pinned by commit sha, every download (Godot 4.7.2, its templates, ninja, the Godot Linux SDK, llvm-mingw) checked against a pinned hash, the
inputs reaching the shell only through the environment and checked first. It produces unsigned build outputs, kept 7 days as run
artifacts (public, like the code they come from; the Windows PDB, private, is never uploaded). What never leaves the Deck: the release key
and everything signed with it (`payload.json` signatures, `manifest.json`, `manifest-macos.json` and their `.sig`), the private denylist,
the release state. The Deck's `gh` token needs `repo` and `workflow` (pushing a commit that carries `.github/workflows`, dispatching,
reading runs and artifacts); the runners get only their own read-only `GITHUB_TOKEN`. Who can tamper: someone with write access to the
public repository can dispatch runs or push to `staging/*`; the Deck accepts only its own run at its own commit, so either fails the
release, never changes what is published. A compromised GitHub runner image could build a different binary: for Linux and Windows the
byte comparison with the build host catches it; for macOS the Deck's checks cover the package's structure, version, commit, key and
repository, not the compiled code (as with the Mac build host before). Suggested for the owner: a ruleset that limits creating
`refs/heads/staging/*` to the release account.

**Reproducibility across the two builds** (measured 2026-10-22: probes on one CI host, then the dry run below on the nightly builder's host).
`build_linux.sh` and `build_windows.sh` set `SOURCE_DATE_EPOCH` to the commit's time (the build date the library records was the build
day). The libraries record source paths relative to the build folder, so the workflow uses the release builder's layout (the checkout in
`src/`, the build folders beside it as `src.linux` / `src.win`), and builds the DLL through ccache with `CCACHE_BASEDIR` above both, as
the build hosts do: ccache's base_dir rewriting is what makes the DLL's `__FILE__` strings `..\..\src\engine\...` (without it the first
probe differed in 800 bytes of paths and in size). The runner's checkout drops the git-lfs filter: `.gitattributes` marks `*.png` as LFS
while the public tree holds the four PNGs as plain blobs, and a git-lfs clean filter makes such a checkout read as dirty (`git describe
--dirty`; the build hosts have no git-lfs; where a system gitconfig has it, the release scripts run with `GIT_CONFIG_NOSYSTEM=1`).

| platform | GitHub Actions vs the build host, same commit | why |
| --- | --- | --- |
| Linux `.so` | byte for byte identical (two build hosts) | the same pinned Godot SDK; prefix maps; commit-dated build record |
| Windows `.dll` | identical but for the 16-byte CodeView GUID (`pdb_tools.py same-build`) | lld derives the GUID from the PDB, which holds the build folder's absolute path (`/home/<user>/...` vs the runner's); the GUID only pairs the DLL with its private PDB for crash symbols |
| macOS zip | not byte-identical (compared by hand, not a release step): same file set; identical `VERSION`, both `.pck`s, `Info.plist`s, icons, licences, README; the two app executables identical up to their code signature (only `__LINKEDIT` / `LC_CODE_SIGNATURE` sizes and the signature blob differ); the GDExtension dylib and the swap helper differ, and so do the two `CodeResources` | the compilers differ: the Mac build host has Apple clang 21 / macOS SDK 26.4 (Command Line Tools on macOS 26.6), `macos-15` has Xcode 16.4, Apple clang 17 / SDK 15.5; the ad-hoc signatures come from different `codesign` versions (different sizes); `CodeResources` hashes the differing files |

So the comparison is a release step for Linux and Windows (`actions-compare`), and not for macOS: two Mac builds of one commit do not
match today, and the Mac host would have to be awake to compare, which is what this lane removes.

**The proof** (2026-10-22, lane head `185e35641`, a stable dry run with all three builders on `actions`, on the nightly builder's
hosts, notes since `afedb8cf9`): staging/v0.4.12 at the candidate `a3455cb23d`, runs
[macOS 39806291285](https://github.com/Open-BFME/openbfme-godot/actions/runs/39806291285),
[Linux 39806296840](https://github.com/Open-BFME/openbfme-godot/actions/runs/39806296840),
[Windows 39806302508](https://github.com/Open-BFME/openbfme-godot/actions/runs/39806302508); the Mac package passed the Deck's checks, the
Linux library was the build host's byte for byte, the DLL the build host's but for the GUID, the staging branch was deleted, `AUTORELEASE DRY RUN OK`; no tag,
no release, `main` unchanged.

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
- **The private key.** SIGNKMS-1 replaces this with two keys: the KMS key that signs on GitHub Actions and the Deck key as the
  recovery key ("Release signing" below has their threat model). Until the transition starts, the Deck key is the only one.
  It is offline, never in the repository or CI, and stored with mode 600.
  - If it is lost: no new releases can be installed by existing launchers. Testers download a new
    launcher by hand.
  - If it leaks: an attacker with the key and the GitHub account can ship code. The response is a new
    key, delivered in a launcher update signed with the old key (the self-update path), and the old key
    removed.
  - There is no revocation list.
- **Out of scope.** Anyone who can write the launcher's or the user data folder can already replace the
  launcher. Code-signing of the executables (Authenticode) does not exist yet.

### Release signing (SIGNKMS-1)

Owner, 2026-10-22: "the nightly can sign automatically, that's fine, let's switch". The release key moves from the Deck into AWS KMS,
and the release workflow on GitHub Actions signs with it. Stable releases still need the owner: his approval of the GitHub
environment `stable` comes before the KMS key signs.

**The keys.** `launcher/release_keys.pub` is the trusted list every launcher and game is built with (`package.sh`,
`build_launcher.sh` and `package_macos.py` write it into `build_info.json` as `release_keys` and into the game's
`release_trust.gd` as `RELEASE_KEYS`). Each line holds a key in 64 hex digits and a label:

| label | where the private half lives | signs |
|---|---|---|
| `deck-2026` | the Deck key: on the key host during the transition, then only as an encrypted offline backup. It is also `launcher/release_key.pub`, which every launcher before this lane trusts. | raw Ed25519 over the file (scheme v1), as since LAUNCH-1 |
| `kms-2026` | AWS KMS (`alias/openbfme-release`, `ECC_NIST_EDWARDS25519`, created in KMS and never exportable). Added by a one-line commit once the owner has created it. | the signature envelope (scheme v2) |

A signature (64 bytes) is valid when it verifies under ANY listed key, raw or as an envelope. An unknown key, a changed byte, a wrong
size or an envelope of another kind of file fails, and an empty list fails. The launcher, the game, `sign_manifest.py --verify`,
`seal_package.py check`, `make_macos_manifest.py` and `publish_release.sh` all apply this one rule (`tools/release/release_keys.py`).

**The signature envelope.** KMS signs at most 4096 bytes with pure Ed25519 (`ED25519_SHA_512`, `MessageType RAW`). Our manifests are
5 to 64 KiB, and payload.json is bigger. The prehash variant `ED25519_PH_SHA_512` is a different scheme that no launcher verifies, so
it is not used. Instead, the KMS key signs a short text that names the file:

    OpenBFME signature v2\n<kind>\n<SHA-512 of the file, 128 hex>\n        kind: manifest.json | manifest-macos.json | payload.json

The signature is still 64 bytes, so every bounded read stays the same. An envelope starts with `OpenBFME`, while a manifest is JSON
starting with `{`, so a raw signature can never pass as an envelope signature or the other way round. The kind stops a payload
signature from being replayed as a manifest signature.

**Signature slots.** A release carries `manifest.json.sig` and `manifest-macos.json.sig`, which every launcher reads, and, when
it is signed on Actions, `manifest.json.sig2` and `manifest-macos.json.sig2`, which only launchers with the list read. A new
launcher tries `.sig` first. If that is missing or does not verify, it tries `.sig2`. It keeps whichever verified under the plain
`.sig` name (its caches, `versions/<v>/`, the launcher proof). `payload.json.sig` inside a package holds one signature, made by
whoever sealed it. Only the launcher and game in that same package read it, and they were built with the list.

**The phase** (`tools/release/signing_policy.json`, committed and reviewed like code):
- `transition` (the start): the workflow writes `.sig2` (KMS) only. The Deck verifies the draft, adds `.sig` with the Deck key
  (`tools/release/cosign_release.py`) and publishes. Launchers from before this lane keep updating.
- `kms` (the end): the workflow writes the KMS signature into `.sig` and `.sig2`. The Deck verifies and publishes without any key.

**Who does what.**

| step | where | why there |
|---|---|---|
| the full gate, the nightly gate subset, the Windows start and HUD checks | our hosts | they need the retail game files, which never go to GitHub |
| the public sync: tree audit with the private denylist, community pull request merges and credit, push of main and the tag | the Deck (autorelease.sh steps 1-3, P) | the archive and the denylist are private |
| building the packages that are signed | GitHub Actions (`release.yml`: macos, linux-lib, windows-lib, package) | the signer signs only what its own run built from a commit on main |
| sealing and signing | GitHub Actions, job `sign` (environment `nightly` or `stable`; OIDC to AWS; KMS) | the private key never leaves KMS |
| the draft release with the signed assets | GitHub Actions (`actions_release.py draft`) | read back by name, size and SHA-256 |
| comparing the draft with the tested candidate, the privacy gate with the private denylist on every asset, the notes, the transition's co-signature, publishing | the Deck (`actions_release.py wait`, `cosign_release.py`, `publish_release.sh`) | the private denylist; the tested candidate; the Deck key during the transition |

The workflow never publishes: a release goes public only after the Deck has checked what Actions signed. `OPENBFME_RELEASE_SIGNER`
chooses the path (`deck`, the default until the owner switches it, or `actions`). On the `actions` path, autorelease.sh
dispatches `release.yml` after its push (step P) and waits for the run. A stable run waits for the owner's approval in the GitHub
environment, and the next autorelease run resumes the wait. Then autorelease.sh checks the draft and publishes it.

**The workflow's guards** (`.github/workflows/release.yml`, `tools/release/actions_release.py`):
- Trigger and inputs: only `workflow_dispatch`. Every input reaches the scripts through the environment, never pasted into a script.
  `plan` requires:
  - main of `Open-BFME/openbfme-godot`;
  - a kind of `nightly` with a `v<x.y.z>-nightly.<YYYYMMDD>` tag, or `stable` with a plain `v<x.y.z>`, so a nightly signing can never
    make a stable-channel manifest without the owner's approval;
  - a tag that names the commit, and a commit on main.
- Jobs: the build jobs have read-only contents, no AWS access and no caches. Only `sign` gets the OIDC token and write access to
  contents. `public_workflows.py` allows those two permissions in this file alone, in one job whose environment is `nightly` or
  `stable`.
- Supply chain: actions are pinned by commit sha, Godot and the templates are pinned by hash, and the templates are checked against
  `godot_templates.txt`.
- AWS: the session lasts 15 minutes and is named `openbfme-<run id>-<attempt>`. Its credentials live only in the environment of the
  `aws` processes.
- Signatures: every one is verified against the committed list before anything is uploaded.

**Threat model of the new model.**
- *Who can make the KMS key sign.* Only a run of `.github/workflows/release.yml` on refs/heads/main of this repository (pinned by
  repository id), in the environment `nightly` or `stable`. The role's trust policy checks GitHub's customised OIDC subject, and the key
  policy denies `kms:Sign` to every other principal.
  - In practice: anyone who can dispatch workflows (write access) can get a nightly signed, and a stable signed once the owner
    approves it. The signed bytes are always a fresh build of a commit that is already on main.
  - So signing authority equals the authority to put code on main. Protect main and the release files (step 8 of the owner's setup):
    a community pull request that touches `.github/workflows/release.yml`, `tools/release/` or `launcher/release_keys.pub` gets the
    owner's review and never takes the coordinator's automatic PR fast track ("Community pull requests").
- *Workflow injection.* No input is expanded into a script, and `run-name` is display only. The workflow never runs on pull request,
  push or `workflow_run` triggers, so a fork's code never runs with the OIDC permission.
- *Artifact substitution.* Artifacts are this run's own: no caches, no cross-run downloads. The Deck accepts only the draft whose
  body names its one-time request id, and only from the run that names that id. It re-verifies every signature and checksum, and
  compares the draft's archives with the candidate it tested before it publishes.
- *Downgrade through the old key.* The Deck key stays trusted, so whoever holds it can still sign for every launcher. That is the price
  of the recovery path below.
  - After the transition the key is kept only as an encrypted offline backup. Nothing online can use it, unlike today, where it sits
    on the key host for every nightly.
  - A later launcher release can drop it from the list if the owner prefers losing that recovery path.
- *Rollback.* Unchanged: older signed releases are never installed automatically, and a stale list gives the freeze warning.
- *A compromised AWS account.* Only the account root can rewrite the key policy and then sign. Every `kms:Sign`, every change to the
  key's policy, alias or state, and every change to the signing role sends an e-mail (CloudTrail + EventBridge + SNS).
  - Response: disable the key in KMS, then publish a launcher release signed with the Deck key that drops `kms-2026` from the list.
- *A compromised GitHub account.* It can dispatch the workflow but cannot change what main holds without the push being visible.
  The stable environment still needs the owner's approval. Publishing still goes through the Deck's checks, and every signature
  sends an e-mail.

**Recovery.**
- *The KMS key is lost* (deleted, disabled, or the AWS account is gone):
  1. Sign with the Deck key from its encrypted backup (`OPENBFME_RELEASE_SIGNER=deck`); every launcher trusts it.
  2. If a new KMS key is made, ship it in a launcher release signed with the Deck key (a new line in `release_keys.pub`).
- *The Deck key's backup is lost:* nothing changes for launchers that already have the list. The KMS key keeps signing. Only
  launchers from before this lane can no longer be updated after the cut-over, so they recover by a manual download (below).
- *A launcher that missed the transition* (one from before the trust release, started for the first time after the cut-over):
  1. It refuses the new release ("the manifest's signature is not valid") and keeps playing its installed version. It fails closed.
  2. The player downloads the current package from the releases page by hand, and the launcher in it has the list.
  3. The cut-over release's notes and the README say so.

**Rollout and the transition plan:** see "Moving releases to GitHub Actions (RELSIGN-1)" below: the four switch-over steps, the
cut-over's conditions included.

**The owner's setup (click by click; he runs it, this lane created nothing).** Nothing here needs a long-lived AWS key: GitHub uses
OIDC, and the owner uses his own console login.

1. *GitHub, repository id:* run `gh api repos/Open-BFME/openbfme-godot --jq .id`, or open https://api.github.com/repos/Open-BFME/openbfme-godot
   and read `"id"`. Note the number.
2. *GitHub, the OIDC subject:* this has no settings page, so run once, as the owner:

       gh api -X PUT repos/Open-BFME/openbfme-godot/actions/oidc/customization/sub -F use_default=false \
         -f 'include_claim_keys[]=repo' -f 'include_claim_keys[]=repository_id' -f 'include_claim_keys[]=context' \
         -f 'include_claim_keys[]=workflow_ref'

   Check it with `gh api repos/Open-BFME/openbfme-godot/actions/oidc/customization/sub`. No other workflow of the repository uses OIDC.
3. *GitHub, environments* (repository Settings > Environments > New environment):
   - `nightly`: under Deployment branches and tags choose "Selected branches and tags", then Add deployment branch rule: `main`.
     No reviewers.
   - `stable`: the same branch rule `main`, plus "Required reviewers": add yourself (leave "Prevent self-review" off: the Deck
     dispatches as your account). Save.
4. *AWS, the stack:*
   1. Sign in to the AWS console with your own login and pick region **US East (N. Virginia) us-east-1** at the top right. IAM's
      events reach EventBridge only there.
   2. Look under IAM > Identity providers. If `token.actions.githubusercontent.com` is already listed, copy its ARN.
   3. Look under CloudTrail > Trails. If a trail already logs management events here, you can reuse it (CreateTrail false). Otherwise
      the stack makes one.
   4. Find your key-admin ARN: IAM > Users > your user > copy the ARN. For an SSO login, use the role's ARN from IAM > Roles.
   5. CloudFormation > Create stack > With new resources (standard) > "Upload a template file" >
      `tools/release/aws/release-signing.yaml` > Next.
   6. Stack name `openbfme-release-signing`. Parameters:
      - GitHubRepositoryId: step 1's number;
      - KeyAdminArn: your ARN;
      - AlertEmail: your address;
      - ExistingGitHubOidcProviderArn: the ARN from step 2, or empty;
      - CreateTrail: true, or false per step 3.

      Then Next.
   7. On the review page tick "I acknowledge that AWS CloudFormation might create IAM resources with custom names", then Submit.
      Wait for CREATE_COMPLETE (a few minutes). If it fails with "invalid principals" on the key, the new role had not propagated
      yet: delete the stack and create it again.
   8. Open the e-mail "AWS Notification - Subscription Confirmation" and click "Confirm subscription".
   9. Open the stack's Outputs tab and note SigningRoleArn and Region.
   10. KMS > Customer managed keys > `alias/openbfme-release` > tab "Public key" > Download. Send the file to the coordinator. It is
       public and safe to share, and it becomes the `kms-2026` line.
5. *GitHub, the role and the region:* in Settings > Environments > `nightly`:
   - under Environment secrets, Add environment secret `OPENBFME_SIGNING_ROLE_ARN` = SigningRoleArn;
   - under Environment variables, Add environment variable `OPENBFME_SIGNING_REGION` = Region.

   Do the same for `stable`. The ARN is not a credential. It is a secret only because it names the AWS account, and GitHub masks
   secrets in the public logs, while a variable would be printed with the job's environment. No AWS credential is stored anywhere.
6. *Billing:* the stack costs about $1.05 a month (the key $1, signing under 1 cent, the trail's storage a few cents; see the
   template's header). The existing $90 budget alarm covers it.
7. *A test signature,* after the `kms-2026` commit is merged and synced: dispatch the workflow for a nightly. The coordinator does
   this; it creates a draft only. Expect one e-mail per signature (about six per release).
8. *Repository rules* (Settings > Rules > Rulesets, on main):
   - keep "Restrict deletions" and "Block force pushes";
   - only the owner and the release account may push to main and create `v*` tags.

   Community pull requests that touch the release workflow, `tools/release/` or `launcher/release_keys.pub` take the normal path with
   the owner's review, never the automatic fast track.

### Moving releases to GitHub Actions (RELSIGN-1)

Owner, 2026-10-22: "make sure the nightly builds are on github actions and compile + publish automatically for all 3 systems". MACACT-1
(the builds) and SIGNKMS-1 (the signing) are one design since RELSIGN-1:

- **One definition of the builds.** `.github/workflows/release-package.yml` holds the three build jobs (macOS package, Linux library,
  Windows library) with the build host's folder layout, ccache's path rewriting and the commit-dated build record, so the Linux library is
  the build host's byte for byte and the Windows DLL the build host's but for its CodeView GUID (the table above). It runs two ways:
  - **dispatched by the Deck** on `staging/<version>` at the candidate commit, one platform per run (`OPENBFME_RELEASE_*_BUILDER=actions`):
    the Deck collects and compares the artifacts and keeps sealing and signing itself (MACACT-1's flow above);
  - **called by `.github/workflows/release.yml`** (`workflow_call`, `OPENBFME_RELEASE_SIGNER=actions`): its jobs `macos`, `linux-lib` and
    `windows-lib` are calls of that file (its definition is the one on `main`, the caller's commit; each job checks out and builds the
    tag's commit); then `package` (`package.sh` for Linux and Windows) and `sign` (OIDC + KMS,
    the draft) as in SIGNKMS-1.
  Each build job's first step tells the two apart by `GITHUB_WORKFLOW_REF`, which the runner sets and no input can change: a dispatch must
  run release-package.yml from `staging/<version>` with the ref's head at the commit; a call must come from `release.yml` on
  `refs/heads/main` (whose `plan` job checked that the tag names the commit and the commit is on main). Anything else fails.
- **The policy** (`public_workflows.py`) allows `workflow_call` in `release-package.yml` alone (beside `workflow_dispatch`, declaring inputs
  only), a job that calls a workflow in `release.yml` alone (exactly `./.github/workflows/release-package.yml`, no `permissions` or
  `secrets` of its own: it runs with the top-level `contents: read`), and refuses a `secrets:` key everywhere (`secrets: inherit` would
  hand a called workflow every secret without one `secrets.` reference). The build jobs therefore have no secret, no write access and no
  OIDC token on either path; only `release.yml`'s `sign` job has them, in the `nightly` / `stable` environment.
- **The comparison on the signer's path.** `actions_release.py compare` holds the draft's archives to the tested candidate: byte for byte
  without the seal, except the game's Windows ZIP, which may differ in its DLL's CodeView GUID and nothing else (member by member,
  `pdb_tools.same_build`). The Mac package is compared byte for byte too, and the Mac build host's compiler is not `macos-15`'s, so a
  signer = `actions` release needs `OPENBFME_RELEASE_MAC_BUILDER=actions` (the candidate's Mac package then comes from the same runner
  image; if two `macos-15` builds of one commit still differ, the release fails closed and `OPENBFME_RELEASE_ACTIONS_COMPARE=report`
  is the owner's call).

**The switch-over, in order** (each step is the owner's call; the defaults stay `host` builders and the `deck` signer):

1. **(a) Nightly builds on Actions.** Prerequisite: the workflows are on public `main` (the next release carries them; GitHub dispatches
   only a workflow it has seen). Then, in the operator's release environment (`release.env`, or only the nightly wrapper's environment to
   keep stable releases on the hosts for a while):

       export OPENBFME_RELEASE_MAC_BUILDER=actions
       export OPENBFME_RELEASE_LINUX_BUILDER=actions
       export OPENBFME_RELEASE_WINDOWS_BUILDER=actions

   The nightly then builds macOS on `macos-15` (the Mac host is not asked), and Linux and Windows on GitHub as well as on the build host,
   whose library must match GitHub's; the packages and their tests stay on the build host (game files). The Deck still seals, signs and
   publishes automatically as now. Proof that this works: the RELSIGN-1 dry run below.
2. **(b) After the owner's AWS setup: signer on Actions.** The owner runs "The owner's setup" (SIGNKMS-1 above); the coordinator commits the
   KMS public key as `kms-2026` (`python3 tools/release/release_keys.py --spki-to-hex <the public key file>`) and dispatches one nightly
   test signature (a draft only). Then `export OPENBFME_RELEASE_SIGNER=actions` (with step (a)'s three builders), phase `transition` in
   `signing_policy.json`: Actions builds, seals and signs `.sig2` through KMS and leaves a draft; the Deck compares it with the tested
   candidate, co-signs `.sig` with the Deck key and publishes. A stable release waits for the owner's approval of the `stable`
   environment; `OPENBFME_RELEASE_SIGN_TIMEOUT` (default 14400 s) bounds that wait, then the intent stays open and the next run waits
   again (`OPENBFME_RELEASE_ACTIONS_TIMEOUT` stays MACACT-1's bound on one staging build).
3. **(c) The trust release.** The next stable release after `kms-2026` is committed (signed either way) ships launchers and games whose
   `release_keys.pub` trusts both keys. Every launcher that updates to it can follow the cut-over.
4. **(d) The cut-over to phase `kms`** (a one-line commit to `signing_policy.json`), when ALL of these hold:
   - at least 60 days since the trust release (twice the freeze-warning window; a launcher updates itself at every start);
   - at least two stable releases since the trust release;
   - no report in the community channels of a launcher older than the trust release still in use.

   Actions then writes the KMS signature into `.sig` and `.sig2`, the Deck publishes without any key, and the Deck key leaves the key host
   (deleted there, kept only as the encrypted offline backup and listed as the recovery key). A launcher that missed the transition refuses
   the new release, keeps playing its installed version and recovers by a manual download (the cut-over's notes say so).

## Community pull requests (GHPR-1)

The public repository is generated from the archive branch, so a community pull request is never merged on GitHub by hand: the next sync
would overwrite it unless the archive carries it, and a pull request closed without a merge does not show its author as merged.
`CONTRIBUTING.md` describes the flow for contributors; for the operator it is:

1. **Review** the pull request on GitHub at its current head. Public checks: `.github/workflows/pr.yml` (no secrets, read-only token,
   `pull_request` trigger; the one check to look at is **CI**, see "Hosted pull request CI" below).
2. **Port** it into the archive branch with its author kept (cherry-pick its commits, or apply them with `git am`), through the usual
   lane and gate. The port must be the pull request exactly: the release refuses a pull request whose diff is not contained in the
   public tree. If the port needed changes, ask the author to update the pull request (or push the port to its branch), review the new
   head, and list that one.
3. **List** it in the private state, `$OPENBFME_RELEASE_STATE/community-prs.tsv`, one tab-separated line per pull request:
   `<number>  <reviewed head sha>  <author login>  <archive commit of the port>` (`#` comments). Never in the repository.
4. **Release.** `autorelease.sh` handles every listed pull request whose port commit is in the released sha
   (`tools/release/community_prs.py`). After the tree passed its audits and before anything is pushed, it reads each pull request from
   GitHub and checks:
   - that it is against `main` of the target repository;
   - that its head is the reviewed sha (on GitHub and fetched from `refs/pull/<n>/head`);
   - that the author is the listed one, and that it is not a draft;
   - that it is open and mergeable, or already merged into `main`;
   - that its whole diff (from its merge base with `main`) applies in reverse, exactly, to the public tree, with the same file modes.

   Every pull request is checked before the first one is merged. A failed check stops the run with `COMMUNITY REFUSED: #<n> ...` and
   nothing written to GitHub. Then each open pull request is merged with a merge commit (`gh pr merge --merge --match-head-commit
   <reviewed sha>`; a push in between makes GitHub refuse) and read back. The sync commit goes on top of the `main` that holds those
   merges, and the sync commit's message says "Includes #37 by @login" (RELCAND-1; the sync PR's body before). These merges are the
   one write to GitHub before the candidate has passed: they publish the pull request's own, already public commits, no tag and no
   version. A candidate that fails leaves them merged; the next run takes them as already merged and credits them. After the push,
   the landed pull requests are recorded in
   `$OPENBFME_RELEASE_STATE/community-landed.tsv`: the next releases skip them (remove them from the reviewed list at leisure).
   - A pull request whose port is not in the released sha waits for a later release (printed, not merged, not credited).
   - When the public `main` already holds the release's whole tree once the pull requests are merged, there is no sync commit: the release
     tags the last merge commit, and the merged pull request is the record.
   - A run resumed after its sync was merged merges nothing more; a pull request reviewed in between waits for the next release.

`--plan` runs every check (GitHub is read, never written) and prints `COMMUNITY PLAN: #<n> by @<login> ... would be merged before the
sync` and the body's lines. `--dry-run` runs the checks and prints each merge as `WOULD RUN: gh pr merge ...`. The tests
(`tools/release/test_community_prs.py`) run the flow end to end against `gh_stand_in.py`: the stand-in's pull requests are real
branches of the local remote, merged with real merge commits. They cover each refusal by name, a push just before the merge, a lost
merge answer, a pull request merged by hand, a resumed run and the public `.github` policy.

**The `.github` policy** (`tools/release/public_workflows.py`). A sync must never carry a workflow that could reach a secret, write to
the repository or run on a private machine, also not one a community pull request added. It is checked on the sync tree before
anything is pushed (also by `--plan`) and on every pull request. The rules:
- `.github/` holds only the pull request template and `.github/workflows/<name>.yml`;
- every workflow parses as YAML (PyYAML; missing PyYAML fails the check): GitHub runs none of the jobs of a file it cannot parse;
- triggers are `pull_request`, `merge_group`, `push` and `workflow_dispatch` only (never `pull_request_target`, `workflow_run` or
  `issue_comment`);
- there is no `secrets.` reference, no write or `id-token` permission, and the top-level permissions are exactly `contents: read`;
- every action is pinned by its full commit sha;
- runners are GitHub-hosted only.

### Hosted pull request CI (PUBLICCI-1)

Until 2026-10-19 every run of `pr.yml` failed in 0 s as a "workflow file issue": line 88, a plain `run:` value, held `audio decode: MP3`,
and `: ` inside a plain YAML scalar starts a mapping, so GitHub refused the whole file (the line-based policy above passed it; it now
loads each workflow as YAML, and the `lint` job runs actionlint). The workflow (`.github/workflows/pr.yml`) now runs on GitHub-hosted
runners only, with `permissions: contents: read`, pinned action shas, no secrets and no game files:

| job | runner | what |
| --- | --- | --- |
| `lint` | ubuntu-24.04 | actionlint 1.7.12 and zizmor 1.31.1 (offline), downloads checked by SHA-256 |
| `gate` | ubuntu-24.04 | the commit gate on the pull request's changes, `pr_audit.py` (built-in privacy rules + the `.github` policy; no private denylist), `pr_pytest.py` |
| `linux` | ubuntu-24.04 | engine core + `openbfme_tests` build (ccache), the unit tests that need no game files |
| `windows` | ubuntu-24.04 | `tools/release/build_windows.sh --no-export`: the release's llvm-mingw cross build (tests, peer, GDExtension, PDB scrub, DLL audit), compile only |
| `macos` | macos-15 | the GDExtension with the Mac package's configuration (arm64, macOS 13), compile only |
| `CI` | ubuntu-24.04 | needs all of the above, runs even when one failed, and passes only when every one succeeded |

Measured on `ci/publicci1` (2026-10-19, the tree of the next sync): with warm compiler caches the whole check took 7.5 minutes
(gate 7m21 with pytest on four workers, linux 3m13, macos 55 s, windows 47 s, lint 7 s); with cold caches the builds took
11.5 (macos), 17 (windows) and 17 to 24 (linux) minutes. A pull request that touches a header most files include builds nearly cold.

Triggers: `pull_request` to `main`, `merge_group`, `push` to `main` and to `ci/**` (maintainer test branches), `workflow_dispatch`.
A newer push to a pull request cancels its older run. The compiler caches are saved only by pushes (`main`'s are the ones pull
requests restore; a `ci/**` branch's cache is visible to that branch only); pull requests restore read-only (a pull request cannot
poison what `main` builds with).

**Repository settings for the owner** (not changed by any script; GitHub, Settings):
1. Actions, General: "Fork pull request workflows from outside collaborators": *Require approval for first-time contributors* (the
   default; or stricter). Workflow permissions: *Read repository contents and packages permissions*; leave "Allow GitHub Actions to
   create and approve pull requests" off.
2. Actions, General, "Actions permissions": *Allow Open-BFME, and select non-Open-BFME, actions and reusable workflows* with only
   "Allow actions created by GitHub" ticked (the workflow uses `actions/checkout`, `actions/setup-python` and `actions/cache` only), and
   "Require actions to be pinned to a full-length commit SHA" (today: all actions allowed, pinning not required).
3. Actions, Runners: no self-hosted runner for this repository or the organisation (a pull request can edit a workflow to ask for one).
4. Rules, Rulesets, the existing `main-protection` ruleset (today: no deletion, no force push, no bypass actors): add "Require status
   checks to pass" with the one check **`CI`** (source: GitHub Actions);
   optionally "Require merge queue" (the workflow already runs on `merge_group`). Before turning the required check on,
   `autorelease.sh` must be able to push its tested sync commit to `main` (RELCAND-1: one atomic push of `main` and the tag, no sync
   PR): give the release account a bypass on that ruleset. Community pull requests then need a
   green CI before the release merges them (`community_prs.py` merges with `--match-head-commit`, unaffected otherwise).

### Pushing to the public repository (PUBLICCI-1)

Everything that writes to `Open-BFME/openbfme-godot` and the privacy scan in front of it (plan 2026-10 section 10: the private scan runs
before anything is pushed public):

| path | what goes public | the scan before it |
| --- | --- | --- |
| `autorelease.sh` step P, `git push --atomic <candidate commit>:refs/heads/main refs/tags/<version>` (RELCAND-1) | the sync commit (the public tree of the released sha) and the tag, after the candidate passed | `autorelease.py audit-tree --denylist` on that tree, `public_workflows.py`; the commit's message and the tag message: `audit-text --denylist`; `publish_release.sh`'s privacy gate in the pre-flight |
| `community_prs.py land` (from `autorelease.sh`) | merges of already public community pull requests | runs after the tree audit; each pull request's diff must be contained in the audited tree |
| `publish_release.sh --execute` | the release's name, notes and assets | `autorelease.py privacy-gate` (notes, tree, assets, `--denylist` required), every output line redacted |
| a hand-made `git push` to the public remote (`origin` in the operator's checkout) | whatever it carries (a lane branch would carry the private history) | **`tools/release/public_push_guard.py`**, the pre-push hook below |
| the GitHub web UI (merging a community pull request by hand) | the pull request's already public commits | none: don't (see "Community pull requests") |

`public_push_guard.py` is the backstop that makes the scan a property of the checkout instead of each script: as the pre-push hook it
runs for every `git push` (autorelease's included) whose URL is the public repository, needs `OPENBFME_RELEASE_DENYLIST`, and audits
every commit the push would publish (tree: the release's privacy gate and the `.github` policy; author, committer and message) and an
annotated tag's text; more than 10 new commits (a branch of the private history) is refused unread. Pushes to other remotes (the
private dev repository, the backup) pass untouched. Install it once in the operator's checkout, after the archive branch holds the file
(a checkout without it refuses every public push, which is the safe failure):

    printf '#!/bin/sh\n# public push guard (docs/RELEASE.md): never bypass\nexec python3 "$(git rev-parse --show-toplevel)/tools/release/public_push_guard.py" "$@"\n' > .git/hooks/pre-push
    chmod +x .git/hooks/pre-push

Never push with `--no-verify`. The tests (`tools/release/test_public_push_guard.py`) push through the real hook into local bare
repositories: a push elsewhere, no / a broken denylist, a clean commit, a deletion, each privacy hit (file, path, message, author,
`.github` policy, unparsable YAML), a hit in an earlier commit, a private-history branch, stale tracking refs, tags, and a crash.

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
