OpenBFME Launcher @VERSION@
==========================

The launcher installs, updates and starts OpenBFME, a rebuild of The Battle for Middle-earth II: The Rise of the
Witch-king 2.01. OpenBFME runs on YOUR OWN copies of The Rise of the Witch-king 2.01 and The Battle for Middle-earth II
1.06; the launcher does not need any game files and downloads none unless you press "Download the games" (see below). When OpenBFME starts
the first time, it asks where they are.

Start it: @START@
This download holds the launcher and the game together: the game is ready to play, with no first download. Start either
one; both keep OpenBFME up to date. The game, started directly, asks the launcher to look for an update in the background
(it says what it found in the top right corner of its menu) and uses a newer version from your next start.
Press Play to start your selected installed version, or the newest usable version if none is selected (including the bundled game).
At every start (and every hour while it stays open) the launcher looks
for a newer version of your channel (Stable: the tested numbered releases, Alpha 0.4.5, 0.4.6, ...; Nightly: also a new test build twice a day,
which may break; both get every release), downloads it in the background, checks it and installs it; Play keeps working meanwhile and the new
version starts from your next Play on (turn off "Update automatically in the background" in Settings to update only when you press Update). Windows
SmartScreen may warn that the launcher is unrecognised: it is not code-signed yet; choose "More info", then "Run anyway".
The menu on the left: Play, News (release notes), Multiplayer, Mods, Game files (your game folders, the games download) and
Settings (the channel, the free camera, updating by itself; About & logs: the installed versions and older releases, the log
folder). The launcher's fonts (Cinzel, Cormorant Garamond, Inter) are under the SIL Open Font License 1.1.

Replay and save files
---------------------
Use Play > Open replay..., drop one .replay file on the launcher, or double-click a downloaded replay. The launcher starts
the installed build with the exact recorded version and commit, not the newest build. If it is missing, the message names
the required version and build date; install it first. Open save..., a dropped .obfmesave, or double-clicking a save uses
the same routing with --load. The game still needs your retail files and checks save/replay/profile compatibility.
Formats 5-8 did not record a release version; use the matching game's --replay option for those files. New recordings use
format 10, and new saves use header version 3; both record the build date separately from the date of the match/save.
Older replay format 9 and save headers 1-2 route by version and commit, with build date unavailable rather than guessed.
The game accepts --replay "path to match.replay" (also --replay=FILE); the launcher accepts
OpenBFMELauncher -- --replay "path to match.replay" or -- --load "path to game.obfmesave".
On first interactive launch Windows registers per-user OpenBFME.Replay and OpenBFME.Save ProgIDs (never overrides another app's default or
Windows UserChoice); Linux registers a per-user .desktop/MIME handler using xdg-utils, shared-mime-info and desktop-file-utils.
No administrator access is required. Disable Settings > General > Open .replay and .obfmesave files with OpenBFME, or run
OpenBFMELauncher -- --replay-association=off before first use. Use =on to register again. Explicit opening still works.
Turn it off before removing the launcher to clean up its association. Details: docs/REPLAY-OPEN.md in the source repository.

What it contacts
----------------
For release updates, GitHub over HTTPS with certificate checks:
  * api.github.com - the list of releases of @REPO@ (about one request per start;
    unchanged answers are cached and do not count against GitHub's limit of 60 requests per hour);
  * github.com and *.githubusercontent.com - the release files (the manifest, its signature, the packages).
Update requests send a User-Agent naming the launcher, its version and the project's GitHub page; they do not send account or game data.
The online player count also asks the configured lobby for anonymous totals at start and every five minutes while the Play screen is visible
and the window is focused. It is on by default; turn off "Show how many players are online" in Settings > General to stop those requests.
Multiplayer contacts that lobby for sign-in, friends, invitations and game sessions. Optional linked-account sign-in opens its provider in your
browser. Online games use the lobby and relay. These are separate from release updates; the launcher does not send telemetry.
On your own computer it holds a socket on 127.0.0.1 (never used to send anything) so that only one launcher runs at a time.
Only while a "Download the games" you started runs (the button shows when no game folders are found):
bfmeladder.com and workshop-files.bfmeladder.com, the servers of the All In One BFME Launcher (the BFME Foundation
Project and the Patch 2.22 team), for The Rise of the Witch-king 2.01 and The Battle for Middle-earth II 1.06. Every
file is checked against OpenBFME's own list before it is kept. Untick "Offer to download the games" in Settings to
hide the button.
Only when you press "Load the list" or install a mod (Mods): bfmeladder.com and workshop-files.bfmeladder.com again,
for the list of Rise of the Witch-king mods and their files. Mods are made by their own authors; every file is checked
against the MD5 the service lists, and they go into the launcher's own mods folder, never into your game folders.
Offline, or when GitHub refuses (for example its hourly limit), it says so and plays the newest installed version.

How it checks a download
------------------------
Every release carries manifest.json, signed with the OpenBFME release key (Ed25519). The launcher accepts only a
manifest whose signature matches the key built into it, then checks every downloaded package's size and SHA-256
against the manifest before unpacking it. A package that fails a check is deleted and nothing is installed.
It never installs an older version by itself; to go back, use Settings > About & logs (play an installed version, or
install an older release).

Where it stores files
---------------------
  Linux:   ~/.local/share/OpenBFMELauncher
  Windows: %APPDATA%\OpenBFMELauncher
    versions\<version>\   the installed versions (the two newest are kept, and the one you picked); the game that
                         came in this download stays in its own folder and is never changed by an update
    downloads\            downloads in progress (resumed next time)
    cache\                the last release list and the checked manifests
    launcher.log          what the launcher did (attach it to a bug report; your home folder is written as ~)
OpenBFME itself keeps its settings and logs in its own folder (see the README_TESTERS.txt of an installed version).
Updates of the launcher itself are placed next to it (in update) and replace it at the next start; the previous
launcher is kept as OpenBFMELauncher.exe.old (Windows) or OpenBFMELauncher.x86_64.old (Linux) until the new one has started once. If the launcher ever does not
start after an update, rename that .old file back to the launcher's name. Keep the launcher in a folder you can
write to.

Remove everything: delete this folder and the data folder above.
