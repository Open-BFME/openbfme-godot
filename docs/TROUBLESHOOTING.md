# Troubleshooting preview and nightly builds

Use the OpenBFME Launcher to check for updates and start the game. The game
reads Rise of the Witch-king (RotWK) 2.01 and Battle for Middle-earth II
(BFME2) 1.06 installations in any language from **two separate folders**. A
language OpenBFME has not checked yet starts with a warning box; see
[PLAYING.md](PLAYING.md).
Messages below are quoted exactly, or by their exact start. Text in `<angle
brackets>` stands for a version, file, folder or other value shown on your
computer. A message may appear inside a longer error box.

## Choosing game folders

On first run, **OpenBFME - choose your game folders** asks for the folders
containing each game's `.big` files. Choose each installation root, not its
parent folder, an individual `.big` file, or a copy of `game.dat` alone. The
game checks the archives before saving the choice; this can take a minute.
For a copied installation on Linux or macOS, keep the original subfolders
intact. If you used the launcher's **Game files** download, choose the folders
it provides.

| Message (or start) | Meaning and what to do |
|---|---|
| `The folders could not be used:` | The chosen folders failed validation. Read the specific line below it; choose again after fixing that problem. |
| `The game folders chosen before do not work any more:` | A saved folder moved or its files changed. Browse to the current complete installations and check them again. |
| `no folder chosen for <game>` | One of the two folders is blank. Choose both RotWK and BFME2. |
| `the folder does not exist: <folder>` | That location is gone. Connect its drive or choose its new location. |
| `not a folder: <path>` | A file was chosen instead of an installation directory. Choose the directory containing `.big` files. |
| `could not list <folder>:` | The folder cannot be read. Check drive access and permissions, then try again. |
| `this folder holds <other game>, not <game>:` | RotWK and BFME2 were swapped. Select the matching folder for each. |
| `no <game> archives in <folder>` | No expected `.big` files were found in that folder or its checked subfolders. Choose the installation root, not its parent or a subfolder. |
| `missing <number> of <number> <game> archives:` | Some 2.01/1.06 archives are absent. Restore a complete install at the named patch level; then choose it again. |
| `<number> archives differ from <game>:` | Archive sizes do not match a supported install. Use unmodified 2.01/1.06 files; remove mods or restore the original files. |
| `<number> archives in <folder> are not part of <game>:` | Extra `.big` files may be from a mod or patch. Restore a clean installation; do not combine installs. |
| `the same folder was picked for both games:` | Each game needs its own installation root. Choose two different folders. |
| `<game>: this folder is not a complete <patch> install:` | The files do not fit a recognized archive layout. Check the rest of the message, then restore a complete, unmodified install. If this is an official patch, include the message in a bug report. |
| `md5 mismatch for <archive>:` / `could not read <archive>` | A file is modified, damaged or unreadable. Restore the named archive from the correct installation and check the drive. |

The game refuses missing, modified and unfamiliar archives rather than
silently loading a partial installation. The one exception is a language
OpenBFME has not checked yet: its files load, and a warning box says so once
per install (see [PLAYING.md](PLAYING.md)). It reads your game files without
changing them.

## Crash or game startup error

| Message (or start) | Meaning and what to do |
|---|---|
| `OpenBFME did not close normally last time (a crash?).` | The previous run left a running marker. A crash or forced quit can do this. The box says **The log of that run is:**; attach **that** log to a report, even if the game starts now. |
| `OpenBFME cannot continue.` | A fatal error stopped this run. Read the first line of the box and attach the log named after **Log file:** with the displayed **Version:**. Use **Open log folder**. |
| `the openbfme extension is not loaded (or too old);` | The game library is missing, from another build, or incompatible with this system. Re-extract or reinstall the complete matching OpenBFME package. On Linux, check the GLIBC line for a system-version requirement. An extension-loading failure may happen before any session log can be written. |
| `mount failed:` | The selected files did not pass the final archive check. Follow the lines after it, then recheck the two installations. |
| `Windows crash diagnostics:` | The game could not set up its crash handler. Restart the game; if it repeats, send the displayed error and any session log. Do not upload memory dumps. |
| `it needs GLIBC <version> or newer` | This Linux build needs a newer system C library. Use a Linux distribution meeting the stated requirement; include both version numbers in a report if the check looks wrong. |
| `the launcher beside the game is not started:` | The game could not verify the launcher next to it, so it continues without an update handover. Re-extract the complete matching package and start the launcher directly. |
| `the launcher beside the game did not answer within <number> s;` / `the launcher's answer was refused:` | The handover timed out or its answer was invalid. This copy of the game starts while the launcher checks in the background. Try the launcher directly; report repeated failures with both logs. |

### Find the right log

The game keeps its 20 newest `openbfme-<date>-<time>.log` files. In the game,
click **Logs** next to the version; in an error box, click **Open log folder**.
The crash box names the **previous** run's log. The launcher has its own log
for download or update failures; its error box also has **Open log folder**
and **Copy details**.

| System | Game session logs | Launcher log |
|---|---|---|
| Windows | `%APPDATA%\Godot\app_userdata\OpenBFME\logs\` | `%APPDATA%\OpenBFMELauncher\launcher.log` |
| Linux | `~/.local/share/godot/app_userdata/OpenBFME/logs/` | `~/.local/share/OpenBFMELauncher/launcher.log` |
| macOS | `~/Library/Application Support/Godot/app_userdata/OpenBFME/logs/` | `~/Library/Application Support/OpenBFMELauncher/launcher.log` |

These are default locations; Linux may use another base directory if you set
`XDG_DATA_HOME`. The launcher may also keep `launcher.old.log` after rotating
its log. Logs are **not** beside the executable.

## Launcher, downloads and signed updates

The launcher checks the release list, verifies signed manifests, downloads
the matching package, then checks its files before installing or playing it.
An error headed **Not installed**, **Update rejected** or **Launcher update
rejected** is not an invitation to bypass verification. Keep the complete
error and, for launcher failures, `launcher.log`.

| Message (or start) | Meaning and what to do |
|---|---|
| `Updates skipped:` | The update check failed; Play uses the newest installed version. Read the reason after the colon and follow the matching entry below. |
| `the release list could not be fetched:` / `the manifest of <release> could not be downloaded:` / `the manifest signature of <release> could not be downloaded:` | A release request failed. Check internet access, then retry. If it repeats, attach the launcher log. |
| `GitHub's API rate limit for this network is used up` | The network has hit a temporary release-list limit. Wait and try again. |
| `GitHub refused the release list (HTTP <code>)` / `the release list request failed (HTTP <code>)` | The release service refused or failed the request. Retry later and report persistent failures with the HTTP code. |
| `the repository <name> has no releases page (HTTP 404)` | No release page was found for the configured source. Check that you have an official launcher and report it. |
| `the release list is not a JSON array` | The release service sent an unexpected response. Retry later, then report it with the launcher log. |
| `GitHub answered 304 Not Modified but no release list is cached` | The launcher received a cache answer without a usable cache. Retry the check; if it repeats, attach the launcher log. |
| `release <release> rejected:` / `Update rejected:` | The release or update failed a check. Read the reason after the colon; do not install its files by hand to bypass the check. |
| `this launcher has no release key` | This launcher cannot authenticate releases. Install a complete official launcher package; report the version shown. |
| `manifest.json is <number> bytes` / `manifest.json.sig is <number> bytes` | The manifest or signature has the wrong size. Retry from an official release; if it persists, report the release and log. |
| `the manifest's signature is not valid for the release key:` | The metadata was not signed with the expected key, or changed. Do not use this release; report the release and launcher log. |
| `manifest.json is not a JSON object` / `manifest.json:` | The signed metadata has an unsupported or invalid format. Check for a newer launcher; report the exact reason. |
| `the manifest is for <name>` / `the manifest is for <version> but came with the release <tag>` / `the manifest's channel (<channel>) does not match` | Release metadata does not match its source, tag or channel. Do not install it; report the release and exact error. |
| `the release <tag> has no signed <platform> package` | That build has no verified package for your platform. Check releases for one that does; do not substitute another platform's package. |
| `release <tag> has an incomplete macOS manifest sidecar` / `the macOS manifest of <tag> could not be downloaded:` / `the macOS manifest signature of <tag> could not be downloaded:` | A Mac release is missing metadata or it could not be retrieved. Retry later; report the release and launcher log if it persists. Do not install an unverified update. |
| `there is no payload.json (not a signed package)` / `the signature of payload.json is not valid for the release key:` | The bundled game/launcher package is unsigned or altered. Download and unpack the complete official package again; do not use the rejected copy. |
| `payload.json:` / `payload.json is not a JSON object` / `payload.json is for <name>` | The signed package inventory is malformed or for another source. Download the correct complete package; report it if a fresh copy fails. |
| `<file> does not match its signed SHA-256` / `<file> is missing` / `<file> is not a file of the signed package` | An installed file was changed, removed or added. Reinstall that version from the launcher or re-extract a fresh complete package. |
| `<asset> has <number> bytes, the signed manifest says <number>:` / `<asset> does not match the signed manifest` | The download failed its size or hash check; it was deleted and not installed. Retry, then report repeated failures. |
| `cannot create <path>` / `writing failed (is the disk full?)` | The destination cannot be written. Free space or use a writable folder, then retry. |
| `the folder could not be renamed into place` / `<version> could not be moved into place` | Files could not be finalized. Close the game and other launchers, check permissions and disk space, then retry. |
| `<version> was not installed:` / `Update not installed` | Installation stopped; the remainder of the error says why. Follow that reason, then retry. |
| `<version> is not newer than the installed <version>:` | Automatic updates will not install the same or an older version. No action is needed; choose an older version yourself only if you intend to roll back. |
| `the launcher's folder` | The launcher cannot update itself in that location. Move the complete package to a writable folder; game updates can still work. |
| `another launcher holds the versions folder` / `<version> was not started: another launcher holds the versions folder` | Another launcher is installing or starting a game. Close it, wait for any installation to finish, then retry. |
| `<version> is being played:` / `<version> is installed and could not be replaced` | That version is running or cannot be replaced. Close the game before installing it again. |
| `Another OpenBFME Launcher` / `The OpenBFME Launcher cannot start:` | Another launcher is open, or the single-instance check failed. Use the open launcher if there is one; otherwise read the details for a port or system error before retrying. |
| `<version> was changed after it was installed` / `<version> cannot be started:` | The installed game failed verification or its startup check. Use Play to download a clean copy; read the rest of the message for a Linux GLIBC or other specific cause. |
| `The game did not start` / `No game to start` | The selected version did not launch or none is installed. Use the launcher to install a supported version; include its error details if Play still fails. |
| `Paused. Resume continues where it stopped.` / `The launcher update is paused. Resume continues it.` | The download is paused, not broken. Select **Resume** to continue. |

### macOS first launch and updates

Mac previews for Apple Silicon need macOS 13 or later. Extract the complete
download so **OpenBFME Launcher.app** and **OpenBFME.app** remain together. The
apps are ad-hoc signed but not notarized. On first launch, if macOS blocks an
*unverified developer* app, dismiss the alert, then open **System Settings >
Privacy & Security > Security > Open Anyway** for that app and confirm. Repeat
for the game app if prompted. Verify the download first. Do **not** turn off
Gatekeeper or override an explicit malware/damaged-app warning; redownload
and report that warning instead.

| Message (or start) | Meaning and what to do |
|---|---|
| `Mac preview: updates are manual for now` / `A newer version may be on the releases page` | This older preview cannot update itself. Get the newer **complete Mac package** from releases and keep both apps together. |
| `macOS started the launcher from a temporary read-only copy (App Translocation)` | The launcher cannot find the game beside that temporary copy. Move both extracted apps together to a normal folder, then start the launcher from there. If macOS still blocks it, use the first-launch steps above; do not disable security checks. |
| `this verified release has no signed Mac payload;` / `this release has no macOS package` | There is no installable signed Mac update for this release. Use the complete Mac download from the releases page if one is offered. |
| `a signed whole-package update is in progress` / `The signed Mac update is waiting for the game to close.` | The two-app package cannot be swapped during play. Quit the game, then reopen the launcher to finish. |
| `The signed Mac update is staged` | A checked package is ready. Close the game and restart the launcher so both apps can update together. |
| `The Mac update to <version> did not complete` / `The previous OpenBFME Launcher was not started again:` | The update was interrupted or rolled back. Follow the error's instruction; if it cannot restore the old package, download the complete Mac package again. Include the launcher log in a report. |
| `the installed Mac package cannot be updated:` / `a previous signed Mac update transaction needs recovery` | Package verification or an earlier update is blocking a new one. Quit the game and restart the launcher; if it repeats, use a fresh complete package and report the error. |

## Online sign-in, lobby and connection

The launcher handles account linking, friends and invitations; the game's
Online screens handle guest sign-in, rooms and play. Try an update before
reporting an error about an answer the client cannot understand.

| Message (or start) | Meaning and what to do |
|---|---|
| `No online lobby is configured.` / `The lobby address is invalid.` | The online service address is missing or invalid. Use the address supplied with your build; check its spelling. |
| `Can't reach the OpenBFME online lobby` | The Online screens could not connect. Check your internet connection and try again later; the service may be down. |
| `The online lobby sent an answer this OpenBFME version does not understand` | This client and the service disagree about the protocol. Update OpenBFME; if it persists, report the version and log. |
| `You can't join this game: it was made with a different OpenBFME version or different game files.` | Players do not have matching builds or clean RotWK files. Use the same OpenBFME version and unmodified RotWK 2.01 files. |
| `This game has started already:` / `This game is gone:` | The room started or closed while you joined. Refresh Open Play and choose another room. |
| `Your online session has expired.` | The lobby no longer accepts your session. Log out and log in again. |
| `Too many requests at once:` | The lobby is throttling requests. Wait a moment and retry. |
| `The lobby's message of the day could not be read:` | A lobby notice could not be fetched. Retry later; report repeated failures with the session log. |
| `The game could not be watched:` / `The game could not be rejoined:` | The selected game is no longer available or the relay did not provide catch-up data. Return to Online, refresh the list and retry if the game is still offered. |
| `Lost the connection to the online relay server` | Other players cannot be reached. Wait briefly for it to recover; if not, use **QUIT GAME** or **Esc > Exit**. Leaving gives up the game and it cannot be rejoined. |
| `Online accounts are not available on this server yet.` | This service does not offer the launcher's account features. Use a build/service that supports them or try later. |
| `Set an HTTPS lobby URL before signing in.` / `The test lobby address is invalid.` | Account sign-in needs a valid secure service address. Check the address supplied with the build. |
| `The fixed sign-in callback port is unavailable.` / `The system browser could not be opened.` | Browser sign-in could not start or return to the launcher. Close other sign-in attempts, allow the browser to open, then retry. |
| `The e-mail sign-in timed out.` / `The browser sign-in timed out.` | The sign-in window expired. Start again and finish it in the system browser or through the e-mail link in time. |
| `The browser sign-in state did not match.` / `The lobby returned an invalid e-mail sign-in answer.` | The browser response did not match this attempt, or the service sent an invalid answer. Cancel and start a fresh sign-in; report repeated failures. |
| `This needs your player's recovery code.` / `That recovery code is not this player's.` | Account linking or unlinking needs that player's saved recovery code. If you need a new one, use **Recovery code** on the launcher's **Multiplayer** screen and follow its prompts. It is shown once and replaces the old code; do not share it in a bug report. |
| `That account is linked to another player.` / `That account type is already linked.` | The provider cannot be linked to this player as requested. Use the correct player or unlink the existing account first. |
| `Your social session expired.` / `Your social session was revoked.` | Friends or account actions lost authorization. Sign in again. |
| `Too many social requests.` | Account or friends requests are rate-limited. Wait and retry. |
| `The social lobby did not accept the request.` / `The social service is unavailable.` | The service refused the action or cannot serve it now. Retry later; attach the launcher log if it persists. |
| `The lobby returned a redirect that was not followed.` / `The lobby response was too large.` / `The lobby returned an invalid social response.` | The service sent an unexpected response. Check the service address, update OpenBFME and report persistent failures. |

For a join link, spectator link or command-line online start, these more
specific messages can also appear in the game or its session log:

| Message (or start) | Meaning and what to do |
|---|---|
| `the lobby URL must be http(s)://host[:port][/path]` | The given lobby address has an unsupported form. Check the address from the invitation. |
| `the invitation id is invalid` / `spectating needs the game's id` | The invitation or game link is incomplete. Ask the sender for a fresh complete link. |
| `accepting an invitation requires the original lobby session` / `no saved lobby session to rejoin with` | The original session is missing on this computer. Return to Online and sign in again; ask for a new invitation if needed. |
| `cannot reach the lobby <url>:` / `the lobby <url> did not answer` | The service could not be reached. Check connectivity and the address, then retry. |
| `the lobby <url> answered with a redirect: not followed` / `the lobby <url> answered with more than <number> bytes: not read` | The service gave an unexpected or oversized response. Do not follow another address from it; report the issue. |
| `the lobby answered HTTP <code> with something that is not lobby version 1` / `The lobby returned an invalid OAuth session.` | The service sent an answer this client cannot use. Check for an update and report the exact message if it repeats. |

## Report a bug

Post in the OpenBFME Discord **#bug-reports forum** or open a
[GitHub issue](https://github.com/Open-BFME/openbfme-godot/issues).
Include the build version, operating system, what you tried, the exact error
and steps to reproduce it. Attach the **game session log for the affected
run**; for launcher/update trouble also attach `launcher.log` and **Copy
details** from its error box. Check logs and copied details for private
information before posting. Do not upload game files, installation folders,
account recovery codes or Windows memory dumps.
