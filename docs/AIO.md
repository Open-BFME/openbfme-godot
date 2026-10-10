# Installing the game files through the All In One BFME Launcher (lane AIO-1)

The owner, 2026-10-10: "hook into the all in one launcher for the game files; figure out how to
effectively bootstrap it so it downloads the launcher and headlessly downloads the files for the
user. I have permission from that mod project to use it."

Status: option (b) is approved, built in the launcher and **on by default** since round 3
(`AioInstall.DEFAULT_ENABLED` in `launcher/scripts/core/aio_install.gd`). Nothing downloads without the
player's consent and click. Research date: 2026-10-10.

## Decisions (the owner, 2026-10-10)

- **Permission.** The All In One BFME Launcher team agreed in their Discord, answering the owner
  ("thevoice"): "Hey thevoice, sure you can use our tool happy to be of help!" They also said they
  will not change their tool for us.
- **Use their API directly: option (b) is approved.** Our launcher calls
  `bfmeladder.com/api/workshop/download?guid=original-RotWK` / `original-BFME2` and downloads the
  files from `workshop-files.bfmeladder.com`.
- **Original patches only for now** (Vanilla 2.01 + 1.06). Mods will come through the same service
  later, so the client stays package-generic: a guid, its file list and the checks. The packages are
  one small table (`launcher/scripts/core/aio_pins.gd`).
- **Permanent URLs: unknown, so pin them.** The MD5-named file URL from the manifest is the pin, and
  the service's file owner is pinned per package. A changed owner or URL stops the install.
- **Bandwidth: fine.**

## Short answer

- **Headless use of the AIO launcher: no.** The download is a single-file .NET 8 WPF app, not an
  installer. It has no silent flag and no install command line. Installing a game is a GUI flow
  that asks for elevation (HKLM writes). Driving it would mean UI automation under Windows or Wine.
- **Versions: exactly ours.** The AIO launcher installs a game by syncing a workshop package. The
  base packages `original-RotWK` ("Vanilla (2.01)") and `original-BFME2` ("Vanilla (1.06)") list
  the same MD5 for every one of the 213 archives in our pinned tables
  (`engine/data/retail-archives/*-english-archives.json`): 106 of 106 RotWK archives and 107 of
  107 BFME2 archives. Patches 2.02 and 1.09 and the mods are separate packages. Its patch switcher
  can go back to Vanilla.
- **Recommended path: (b) with (c) touches.** Our launcher downloads the two Vanilla packages
  itself, from the same public service the AIO launcher and its open-source WorkshopKit use. It
  checks every archive against our own pinned MD5 and size, never against the service's word
  alone. No AIO binary, no Wine, no admin rights, no registry writes, and it is native on Windows
  and Linux. What we need from the AIO team for this is listed under "Questions for the AIO team".

## Phase 1: what the AIO launcher is (evidence)

Everything was downloaded into the ignored `workspace/aio/` and none of it is in git. Sources:

| Item | Where from | Identity |
|---|---|---|
| Installer | `https://bfmeladder.com/download` -> "All In One Launcher" -> `/download-go?app=aio` (302) -> `https://arena-files.bfmeladder.com/downloads/AllInOneLauncherSetup.exe` | 273,645,924 bytes, MD5 `06978c80c61b0a7c65e20af570e12fcd`, SHA-256 `4944579a76870c2d3d35cfd9ad5480a8481d49f8251e663adf3de3d4c8328f1a`, Last-Modified 2026-05-21 |
| Source code | `https://github.com/MarcellVokk/BfmeFoundationProject` (linked from the download page; the older `MarcellVokk/bfme-foundation-project` README says "WE'VE MOVED" there) | head `1c8d539`, 2025-12-28 |
| Older launchers | `MarcellVokk/aio-launcher` (MIT, last push 2025-05) and `Ravo92/aio-lotr-launcher` (archived; the "Patch 2.22 Launcher", Inno Setup `.iss`) | history only |
| Service answers | `GET https://bfmeladder.com/api/workshop/download?guid=original-RotWK` and `...=original-BFME2`, one request each | 127,193 and 109,757 bytes of JSON |

The download page names the authors as "the Patch 2.22 Team and the BFME Foundation Project". The
ModDB page in the brief is a mirror. The AIO binary's own strings name the bfmeladder URL as its
setup URL, so that was the copy we used.

### What it is built with

- **.NET 8 WPF, published as a self-contained single file.** Evidence: `HowToPublish.txt` in the
  source (`dotnet publish -r win-x64 /p:PublishSingleFile=true ... --self-contained true`). The
  installer's PE header is x86-64 with a `.CLR_UEF` section, and its version resource says
  `OriginalFilename AllInOneLauncher.dll`, `CompanyName Bfme Foundation Team`, `ProductVersion
  1.0.0+10a5476a...`. The bundle's string table lists the WPF and .NET runtime assemblies.
- **Not an installer framework.** No NSIS, Inno, Squirrel, WiX or MSI markers were found in the
  file. The "setup" is the launcher itself. On first start, `LauncherUpdateManager.CheckForUpdates`
  downloads the current build from `https://arena-files.bfmeladder.com/application-builds/all-in-one-launcher-main`
  into `%APPDATA%\BFME All In One Launcher\AllInOneLauncher.exe` and restarts it with
  `Verb = "runas"` (elevation). `App.xaml.cs` `EnsureAppConfig` then writes the uninstall key and
  the shortcuts.
- **Not Authenticode-signed.** The PE security directory is empty.
- **Self-updating to a moving build.** The current build is 276,767,588 bytes, Last-Modified
  2026-10-09. The service's `api/applications/versionHash?name=all-in-one-launcher&version=main`
  returns `42ab0065105aa15226e9baaed5407149`, which is not the setup file's MD5. Pinning the setup
  file's hash would therefore pin a bootstrapper that replaces itself on first run.
- The shipped binary carries the same endpoints as the source: the `bfmeladder.com/api/` base,
  `workshop/download`, `workshop/query`, `original-` and `workshop-files.bfmeladder.com`
  (`strings -e l`). Inference: the source matches the shipped build closely enough for the parts
  used here. The build's commit `10a5476a` is not in the public history we cloned.

### Command line and headless mode

- The only arguments are `--Uninstall` (`App.xaml.cs` `OnStartup`) and `--LauncherChangelog`
  (`MainWindow.xaml.cs` `ProcessCommandLineArgs`). There is no silent install, no
  install-game argument and no pipe or API for driving it. The single-instance pipe
  (`StartServer`) only brings the window to the front.
- The old Inno-based "Patch 2.22 Launcher" had Inno's `/VERYSILENT`. The current launcher deletes
  that uninstall key (`All In One Launcher_is1`) and replaces the app, so this is history.
- Installing a game is `BfmeSyncManager.InstallGame(game, language, location)`, called from the
  UI after the user picks a language and a folder.

### Where it installs, and which registry keys it writes (settles part of S-1560)

From `BfmeFoundationProject_BfmeKit/Logic/BfmeRegistryManager.cs` and `Data/BfmeDefaults.cs`. The
`nint.Size == 8` check means the 32-bit view (`WOW6432Node`) on 64-bit Windows.

- **Game folder.** `<picked location>\RotWK` and `<picked location>\BFME2`
  (`BfmeSyncManager.InstallGame`). Installing RotWK also creates BFME2's registry entry when BFME2
  is missing.
- **`HKLM\SOFTWARE\[WOW6432Node\]Electronic Arts\Electronic Arts\The Lord of the Rings, The Rise of the Witch-king`**
  (BFME2: `...\Electronic Arts\Electronic Arts\The Battle for Middle-earth II`) gets these values:
  - `InstallPath`: the folder, with a trailing backslash;
  - `Language`;
  - `MapPackVersion` = 65536 (DWORD);
  - `UseLocalUserMaps` = 0;
  - `UserDataLeafName` (`My Rise of the Witch-king Files` / `My Battle for Middle-earth II Files`);
  - `Version` = 65539 (DWORD);
  - a subkey `ergc` whose default value is **a random 20-character serial**
    (`CreateNewInstallRegistry`).
- **`HKLM\SOFTWARE\[WOW6432Node\]Microsoft\Windows\CurrentVersion\App Paths\lotrbfme2ep1.exe`**
  (BFME2: `lotrbfme2.exe`) gets these values:
  - the default value: the exe path;
  - `Game Registry` = `SOFTWARE\Electronic Arts\Electronic Arts\...`;
  - `Installed` = 1;
  - `Path` = the folder;
  - `Restart` = 0.

  `EnsureDefaults` deletes and recreates this key on every sync. For RotWK it also does BFME2's.
- **Old keys moved.** `EA GAMES\...` and `Electronic Arts\The Battle for Middle-earth II` /
  `...\The Rise of the Witch-king` (value `Install Dir`) are moved to the keys above and deleted
  (`EnsureFixedRegistry`).
- **Compatibility flags.** `HKCU\...\AppCompatFlags\Layers`: `~ WINXPSP3` for the exe and
  `game.dat`.
- **User files.** `%APPDATA%\<UserDataLeafName>\Options.ini` gets defaults. `%APPDATA%\BFME Workshop\`
  holds `Config\active_patch_<game>.json`, `active_enhancements_*.json` and `Cache\`. A
  `BFME Workshop\Cache\<md5>.pfcache` file cache sits next to the game folders (`<location>\BFME Workshop\Cache`).

For S-1560: the GameRegPath `InstallPath` values (both games) and RotWK's own `App Paths\lotrbfme2ep1.exe`
`Path` are what this launcher writes. That is a verified fact for AIO installs. For the EA
installers it stays inference. OpenBFME's discovery already reads these keys, so an AIO-installed
game is found, read-only, through the 32-bit view.

### How it downloads the game files

From `BfmeFoundationProject_WorkshopKit` (`Logic/BfmeWorkshopSyncManager.cs`,
`BfmeWorkshopDownloadManager.cs`, `Utils/HttpUtils.cs`, `Data/BfmeWorkshopEntry.cs`,
`Data/BfmeWorkshopFile.cs`) and `HttpInstruments/HttpMarshal.cs`, confirmed by direct requests:

1. **Manifest.** `GET https://bfmeladder.com/api/workshop/download?guid=original-RotWK`, with headers
   `AuthAccountUuid: unauthenticated` and `AuthAccountPassword:` (empty), as
   `BfmeWorkshopAuthInfo.Unauthenticated` sends them. No account is needed. The answer is JSON: a
   `BfmeWorkshopEntry` with `Guid`, `Name`, `Version` (`1.0.0`), `Game` (2 = RotWK, 1 = BFME2),
   `Type` (0 = patch / base), `Owner` and `Files[]`. Each file entry has `Guid`, `Name`
   (backslashes, relative to the game folder), `Url`, `Md5`, `Language` (`ALL` or a space list
   like `EN NL NO PL SV TR`) and `Size`. HttpMarshal also understands a `b64-gzip`
   Content-Encoding; we observed plain `application/json`.
2. **Files.** Every `Url` is `https://workshop-files.bfmeladder.com/<Owner>/<Md5>`. The data is
   content-addressed by MD5: `Guid == Md5` for all 681 files, and the response ETag is the MD5.
   The files sit on Cloudflare (`server: cloudflare`; R2 per the source). Range requests work:
   `accept-ranges: bytes`, and a 100-byte range returned 206. Three small files (24 B, 2.6 KB,
   3.8 MB) were downloaded and their MD5s matched the manifest.
3. **Install logic** (`Sync`):
   - It keeps the files whose language is `ALL` or the game's language.
   - For RotWK it adds BFME2's base files too (layer L4). Installing RotWK installs both games.
   - It skips a fixed list of files: intro movies `ealogo.vp6`, `cs01.vp6`, `newlinelogo.vp6`,
     `nlc_logo.vp6`, `te_logo.vp6`, plus `_zzlotr.big`, `mod.txt` and a few others.
   - It compares an existing file by size, or by MD5 for `.dat` / `.exe` and size-0 entries.
   - It downloads with `HttpClient`: 5 tries, a 5-hour timeout and no Range resume. A failed try
     starts again from zero.
   - Afterwards it **deletes every other file in the game folder** (`CleanUpFiles`) and kills
     running game processes by name.
4. **Mirrors.** None. There is one host per role: `bfmeladder.com` for the API and
   `workshop-files.bfmeladder.com` for the files.
5. **Download size.** English install: RotWK 315 files, 3.16 GB. BFME2 297 files, 5.45 GB. Total
   about 8.6 GB. Only 5.6 GB of that is the archives our tables pin; the rest is movies, executables,
   cursors and other files.

**Manifest quirks found:**

- Four entries per game have `Size: 0`. One is RotWK's `lang\EnglishAudio.big`, whose real size
  is 473,605,060: the HEAD Content-Length and our table agree on that. Our pinned table, not the
  manifest, must be the size authority.
- The Vanilla RotWK `game.dat` (MD5 `daed3668006cd90f01c34e5a7da1901f`) is byte-identical to the
  RotWK install on the dev Deck. Stop S-001 already describes that file as community-modified, so
  "Vanilla" here means vanilla data with the community's `game.dat`. OpenBFME does not run
  `game.dat`. This matters only for anyone treating an AIO install as a pristine binary oracle,
  which S-001 already covers.

### Versions it installs (the critical question)

The workshop query `api/workshop/query?game=<1|2>&type=0` lists these switchable packages:

- **BFME2:** `official-1` "Patch 1.09" 3.1.4 and "Patch 1.06v4".
- **RotWK:** `official-2` "Patch 2.02" 9.8.0, plus mods (Edain, Edain Unchained, Ennorath, Ages of
  Discord, THOA and others).
- **The base packages:** `original-BFME2` "Vanilla (1.06)" and `original-RotWK` "Vanilla (2.01)",
  both by `EA`, version `1.0.0`.

Patches layer over the base packages (`AddFiles` priority L1 to L4).

Comparison with our pinned tables (path case-insensitive, `\` as `/`):

| Package | Pinned archives | Same MD5 | Same size | Other English `.big` files in the package |
|---|---|---|---|---|
| `original-RotWK` | 106 | 106 | 105 (EnglishAudio: manifest says 0) | none |
| `original-BFME2` | 107 | 107 | 107 | none |

Our tables already record `"package": {"guid": "original-RotWK", "name": "Vanilla (2.01)", ...}`.
Inference: the archive tables were first taken from this very package. So **the AIO service is a
source of exactly the RotWK 2.01 + BFME2 1.06 archives OpenBFME accepts.** The AIO app installs
Vanilla first (`InstallGame` syncs `BaseGame`). A user, or a later patch sync, can switch the
same folder to 2.02 / 1.09, and that rewrites files and deletes ours.

### Licence, terms, account, CD key

- **Code licence.** The monorepo has no licence file. Its README: "We don't have a license.
  Everything that is in this repository is open source. Don't abuse it...". The older
  `aio-launcher` repo is MIT. We copy no code; this document only describes behaviour.
- **Terms.** `bfmeladder.com/terms` (2026-08-31) covers Arena accounts and conduct. It says the
  service "is not affiliated with Electronic Arts or with the rights holders of the games" and is
  provided "as is". Nothing there covers third-party use of the workshop API or of the game files.
  The privacy policy says files are stored at Cloudflare.
- **No account and no CD key.** The download needs no login. The launcher writes a random serial
  itself (`CreateNewInstallRegistry`). There is **no ownership check anywhere**. The game files
  are EA's, and the AIO project has no licence from EA that we could find.
- **Owner's permission.** The owner reports permission from the project. This document cannot
  verify its scope (download service use? bandwidth?). See the questions.

### What was not done, and why

- **No live run of the AIO app under Wine.** The Deck has no system Wine (Proton 11 exists), and
  the app is GUI-only with elevation. The network flow is fully visible in its source, matches the
  shipped binary's strings, and was replayed directly with single small requests. Running the GUI
  would have shown nothing new for the cost.
- **No full game download.** The brief allowed one. It was not needed: the manifest MD5s match all
  213 pinned archives, the URLs are content-addressed, and three sample files matched byte for
  byte. Pulling 8.6 GB from a volunteer service to confirm this again was not worth their
  bandwidth. The prototype's tests use a stand-in server.
- **Total traffic to their servers:**
  - one 274 MB setup download;
  - two manifest GETs and two query GETs;
  - one version-hash GET;
  - five HEADs, one 100-byte range request and three small files (about 3.8 MB).

## Phase 2: design

### (a) Download the AIO installer and drive it headlessly

Parts:

- **Windows:** download the setup (274 MB, unsigned), run it, let it self-update and elevate. Then
  automate its WPF UI to pick a language and folder and click Install, and wait for the
  `%APPDATA%\BFME Workshop\Config\active_patch_2.syncing` progress file (`ConfigUtils.SaveSyncProgress`).
- **Linux:** the same inside a Wine or Proton prefix with .NET WPF working, and UI automation there.
- **Progress:** parse the `.syncing` file.
- **Resume:** theirs, none within a file (5 retries from zero); their `.pfcache` cache survives a
  restart.
- **Verification:** afterwards, against our MD5s.
- **When their service changes:** the setup self-updates to whatever is current, and the UI can
  change at any time. A pinned setup hash breaks on every re-upload.

Verdict: **not viable.** There is no silent mode, so this means UI automation of an elevated GUI,
and that is fragile by construction. It needs admin rights and writes HKLM. It also installs a
second launcher the user did not ask for. That launcher deletes unknown files in "its" folders,
kills games by process name, and can switch the folder to 2.02 at any time, which breaks our MD5s.
Under Linux it needs a working .NET desktop runtime in Wine. It would become viable only if the
AIO team added an official command line, for example
`AllInOneLauncher.exe --install ROTWK --package original-RotWK --language EN --path <dir> --silent`
with machine-readable progress.

### (b) Use the AIO download service directly (recommended)

Parts:

- **Flow:**
  1. The player opts in (see "Consent" below).
  2. The launcher GETs the two manifests from `bfmeladder.com/api/workshop/download?guid=original-RotWK`
     and `...original-BFME2`, with a User-Agent naming OpenBFME (and the two `Auth*` headers their
     kit sends, if the AIO team wants them).
  3. It checks each manifest's shape (`Guid`, `Game`, `Type`, `Files[]`) and that every archive in
     our pinned table is listed **with the pinned MD5**. Any mismatch stops the install with a
     message naming the file. Nothing is downloaded from a manifest that disagrees with the table.
  4. File selection: our pinned archives (needed), plus the manifest's other `ALL` / chosen-language
     files (movies, `gi.dat`, `asset.dat` and so on, which keep the folder a normal install). Each
     is checked against the manifest MD5, the only authority for files we do not pin.
  5. It downloads into `<target>/RotWK` and `<target>/BFME2` through `.part` files, resuming with
     Range, and checks size and MD5 before renaming.
  6. It never deletes files it did not write, and writes no registry keys.
- **Windows:** native, no admin. Default target `%LOCALAPPDATA%\OpenBFME\retail` or a folder the
  player picks. HTTPS with Godot's CA list, as the launcher already does.
- **Linux:** the same native code; no Wine is needed for a download.
- **Progress:** per file and total bytes, through the launcher's existing progress callback.
- **Resume:** byte-level Range resume. The server supports it (206 observed), and `http_fetch.gd`
  already resumes and never writes past the expected size.
- **Verification:** pinned archives against our table (MD5 and size), the rest against the
  manifest MD5. The game's own mount check (`MountRetailArchives`) checks again at start, as it
  does for every install.
- **When their service changes:**
  - *A new manifest version, or files moved or deleted:* the pinned-table check fails loudly,
    before any byte is written ("the AIO service no longer offers RotWK 2.01's X.big with MD5 Y").
  - *Host down:* an error, with the folder-pick path still available.
  - *Changed URL scheme:* the files are content-addressed, so only the manifest step changes.
- **Handing the folders to the game:** the launcher records the two folders as a discovery
  candidate (source "downloaded by the OpenBFME launcher from the All In One BFME Launcher service").
  The game's first-run screen offers it like any found install, and the player confirms it (PLAN
  rule 10: nothing used silently). That is an engine-side discovery hint (InstallLocator), left for
  a later step. The prototype only reports the two paths.
- **Needs from the AIO team:** see the questions below. Specifically:
  - a stable endpoint, or a promise for the current one;
  - version pinning: `original-RotWK` stays Vanilla 2.01, or a versioned guid;
  - their rate limits and bandwidth stance;
  - whether to send the `Auth*` headers.

### (c) Mixes worth taking

- **Use an AIO install already on the machine.** Already done by RELEASE-1's discovery (the
  registry keys above). Recommend it in the consent screen: "you already have RotWK from the AIO
  launcher at X". Warn that switching that folder to 2.02 in the AIO launcher breaks OpenBFME's
  check.
- **Seed from AIO's cache.** `BFME Workshop\Cache\<md5>.pfcache` files are named by MD5. If the
  player already has them, the launcher could copy instead of downloading. Low value; later.
- **Mirror fallback.** If the AIO team agrees, a second origin with the same `<md5>` naming (for
  example an OpenBFME-hosted mirror) can be added without code changes beyond the allow list.

### Consent (shown before anything is downloaded)

- The setting is off by default: "Install game files with the All In One BFME Launcher service".
- The screen, before the download, states:
  - the files come from the **BFME Foundation Project / All In One BFME Launcher** servers
    (`bfmeladder.com`, files at `workshop-files.bfmeladder.com`), not from EA and not from OpenBFME;
  - which packages ("Vanilla (2.01)" and "Vanilla (1.06)"), the size (about 8.6 GB), and the
    target folder;
  - that the games are EA's, and that **you should own The Battle for Middle-earth II and The Rise
    of the Witch-king**; neither service checks this;
  - that OpenBFME sends only a User-Agent, no account and no machine data, and that their privacy
    policy applies to their servers (link).
- The player ticks "I own the games and want to download them from this service" before Download
  is enabled.
- This changes the launcher README's promise ("the launcher does not download or need any game
  files" and "Only GitHub"). The README and `docs/RELEASE.md` must say the new host is contacted
  **only** when this option is used.

### Questions for the AIO team (for the owner to paste into Discord)

Answered by the owner (see "Decisions"): permission, direct API use, pinning, bandwidth. Open:

1. Will `original-RotWK` / `original-BFME2` always be Vanilla 2.01 / 1.06 with today's files and URLs?
   If they ever change, OpenBFME stops with a message, so a heads-up would help.
2. Four manifest entries per game have `Size: 0` (for example RotWK `lang\EnglishAudio.big`, really
   473,605,060 bytes). OpenBFME uses its own sizes, but other clients might want this fixed.
3. Is there wording you want for attribution? Our consent screen names "the All In One BFME Launcher
   (the BFME Foundation Project and the Patch 2.22 team)" and links to bfmeladder.com/download.

### Recommendation

Build (b). The owner approved it on 2026-10-10 (see "Decisions"); it is on by default since round 3,
after Sol's review.

## Phase 3: the prototype (off by default)

Option (b) is built in the launcher. Turn it on with the box "Install game files with the All In One BFME Launcher
(prototype)", or with `--aio=on` on the command line. Nothing is downloaded until the player agrees to the consent
text. In the window that is a dialog after the folder pick; on the command line it is
`--aio-install=<folder> --aio-consent`.

- `launcher/scripts/core/aio_install.gd` does the flow of (b):
  - It fetches the two manifests (`original-RotWK`, `original-BFME2`).
  - It checks them against the pinned tables. Any of these refuses the install before a single file is requested:
    - a pinned archive missing, or listed with another MD5 or size;
    - a `.big` the tables do not know;
    - a file name that is not a plain relative path;
    - a URL that is not `<files host>/<Owner>/<Md5>`;
    - another package `Guid`, `Game` or `Type`;
    - a non-pinned file with no size.
  - It checks the free disk space.
  - It downloads into `<folder>/RotWK` and `<folder>/BFME2` through `.part` files, resuming with Range. Each file's
    MD5 is checked before the rename; a file that fails is deleted.
  - It never overwrites a file it did not write. A different file already in place stops the install.
  - It records the two folders in the launcher's `aio-install.json` and tells the player to pick them when the game
    asks.
- `launcher/scripts/core/aio_pins.gd` is the launcher's copy of `engine/data/retail-archives/*-english-archives.json`,
  generated by `tools/release/aio_pins.py`. The `--check` mode is part of the tests.
- `NetPolicy.extra_hosts` holds the two service hosts only while the download runs. `HttpFetch.get_small` takes extra
  headers (the service's two `Auth*` headers). The HTTPS-only, no-port and redirect rules are unchanged.
- **Tests:**
  - `tools/release/test_aio.py`: 17 tests against a local stand-in service with made-up files and made-up pins. They
    cover: off by default; the consent and the folder; a full install with every file checked; a second run that
    downloads nothing; a pinned MD5 changed; nine kinds of changed package; a corrupt file; a cut download resumed with
    Range; a player's file never overwritten; a redirect to another host refused.
  - `launcher/tests/unit_test.gd`: the network policy, the path rule, the pinned counts.
- **Not done:**
  - **No real download through the prototype.** Its tests use the stand-in only. The real service's manifests passed the
    same checks by hand in phase 1 (213 of 213 pins).
  - **No game-side discovery hint.** The game's first-run screen does not yet offer the downloaded folders by itself;
    the player browses to them.
  - **No language choice.** Only the English tables exist.

## Round 2 (2026-10-10): approved, hardened, and one real run

- **Stops on anything unrecognised, and says what to do.** Every failure names the package and the
  file, then the remedy: "Try again later. Or install the games with the All In One BFME Launcher
  yourself (https://bfmeladder.com/download) and pick their folders when OpenBFME asks." The cases
  covered:
  - a manifest that isn't the expected JSON;
  - an HTTP error (5xx and 429 are network errors that can be retried; other statuses are rejected);
  - another `Guid`, `Game`, `Type` or `Owner`;
  - a URL other than the pinned `<files host>/<owner>/<md5>`;
  - a pinned archive missing, or with another MD5 or a non-zero size that differs from ours;
  - an archive we don't know;
  - a file that is gone (HTTP 404 / 410);
  - a file longer than the pinned size;
  - a wrong MD5 (the file is deleted).

  `Size: 0` entries get the pinned size, and the download refuses a single byte more.
- **The first-run handoff.**
  - After a checked install, the launcher writes `downloaded-installs.cfg` into the game's user data
    folder (`SOURCE=`, `ROTWK_INSTALL=`, `BFME2_INSTALL=`).
  - `InstallLocator::discover` offers those folders first, as found installs with the source
    "downloaded by the OpenBFME launcher (All In One BFME Launcher service: ...)". The player still
    confirms, and the check and the mount verify every archive.
  - A file that can't be read is reported on the first-run screen (`push_error`), never ignored.
  - Tests: `aio1: the launcher's downloaded folders are offered first` in `test_release1_install.cpp`,
    and the marker contents in `tools/release/test_aio.py`.
- **UX.**
  - **Consent screen:** the source and a link to the project's page, the packages, the size and the
    folders. A box "I own The Battle for Middle-earth II and The Rise of the Witch-king..." has to be
    ticked before Download is enabled.
  - **Progress:** overall MB, per file "File i of n, RotWK/<file>: p%", and MB/s.
  - **Pause / Resume** (the partial file is kept and resumed with Range) and **Cancel** (the partial
    files are removed; checked files stay).
  - **Free space:** checked before the first byte; a folder whose free space can't be read is refused.
  - **User-Agent:** `OpenBFME-Launcher/<version> (+https://github.com/Open-BFME/openbfme-godot)`.
- **Off by default:** `AioInstall.DEFAULT_ENABLED := false`. The player's setting (`--aio=on|off`, or
  the box) overrides it.

## Round 3 (2026-10-10): Sol's fixes, turned on

- **Every file is pinned.** `tools/release/aio_package_files.json` holds the name, MD5 and size of all
  612 English files of the two packages: RotWK 315, BFME2 297. It holds hashes only, never bytes. It
  was captured with `aio_pins.py --capture` from the round-2 download, which matched the service's
  manifests. `aio_pins.py` also checks it against `engine/data/retail-archives` (213 archives) and
  generates the launcher's `aio_pins.gd`. A listed file that is not pinned, a pinned file that is not
  listed, and another MD5 or non-zero size are all refused before any download.
- **Files already on disk** must have the pinned size as well as the MD5.
- **Fractional numbers are refused.** `Game`, `Type` and `Size` must be whole JSON numbers
  (`AioInstall._whole`).
- **Cancel and pause.**
  - A stop request is checked before each file and again before the handoff, so a cancel during the
    last check never publishes `downloaded-installs.cfg`.
  - Cancel removes only this download's partial files (`<folder>/<pinned file>.part`).
- **First-run screen.** An unreadable `downloaded-installs.cfg` is shown on the screen
  (`InstallSetup.downloaded_problem`) and printed as a `FIRST RUN` line.
- **On by default:** `AioInstall.DEFAULT_ENABLED := true`. The consent screen and the ownership box
  are unchanged. The round-2 download (`workspace/aio-install/`) was deleted after the pins were
  captured.
