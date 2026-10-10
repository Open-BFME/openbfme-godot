# Playing OpenBFME

The player guide: installing, the launcher, updates, logs, controls, known
issues and troubleshooting. For what OpenBFME is and what works, see the
[README](../README.md).

OpenBFME is an unofficial, fan-made rebuild of The Rise of the Witch-king
2.01. It plays on **your own copy** of the game and never changes your game
files.

## Contents

- [What you need](#what-you-need)
- [Installing with the launcher](#installing-with-the-launcher)
- [The first start](#the-first-start)
- [Updates](#updates)
- [Logs](#logs)
- [The free camera](#the-free-camera)
- [Controls](#controls)
- [Known issues](#known-issues)
- [Troubleshooting](#troubleshooting)
- [Reporting a bug](#reporting-a-bug)

## What you need

- **The Rise of the Witch-king, patched to 2.01**, and **The Battle for
  Middle-earth II, patched to 1.06**, both in **English**. RotWK always needs
  BFME2 next to it, as in the original. Disc installs and All In One launcher
  installs both work.
  No games installed? The launcher's **Download the games** gets both from the
  All In One BFME Launcher service (BFME Foundation, Patch 2.22 team): about
  8.6 GB, with Pause, Resume and Cancel. Every file is checked against
  OpenBFME's list of the 2.01 / 1.06 files, and the game offers the folders on
  its first start. To hide the button, untick *Offer to download the games* in
  Settings.
- **Windows 10 or 11** (64-bit) with a graphics card that supports Vulkan or
  Direct3D 12, or **64-bit Linux** with Vulkan graphics (Mesa or the
  proprietary drivers). On Linux the game folders can be inside a Wine or
  Proton prefix.

## Installing with the launcher

The **OpenBFME Launcher** installs the game, keeps it up to date and starts
it. It is a separate small program: a broken game build can never break the
launcher.

1. Download the launcher for your system from
   [GitHub Releases](https://github.com/Open-BFME/openbfme-godot/releases):
   `openbfme-launcher-<version>-windows-x64.zip` or
   `openbfme-launcher-<version>-linux-x64.tar.gz`.
   To check the download, compare its SHA-256 with the release's
   `SHA256SUMS-<version>.txt` (Linux: `sha256sum -c --ignore-missing
   SHA256SUMS-<version>.txt`; Windows PowerShell: `Get-FileHash <file>`).
   The launcher checks everything it downloads after that by itself.
2. Unpack it into a folder you can write to (not `Program Files`: the
   launcher updates itself in its own folder). On Linux use `tar -xzf`, which
   keeps the launcher executable; if your archive tool lost that, run
   `chmod +x OpenBFMELauncher.x86_64`.
3. Start it: `OpenBFMELauncher.exe`, or `./OpenBFMELauncher.x86_64`. On
   Windows, SmartScreen may say *Windows protected your PC* because the
   executables are not code-signed yet: choose **More info**, then **Run
   anyway**. Windows asks this once per new launcher file.
4. The launcher starts on the channel of its own version: a preview launcher
   such as v0.3.0-preview.1 on **Preview** (test builds), a release on
   **Stable**. It downloads the newest build of that channel, checks it and
   installs it by itself. You can switch the channel at the top right at any
   time (there is no Stable release yet).
5. Press **Play**. The first start asks for your game folders (next
   section).

The main screen shows the installed version and whether it is up to date,
with one button when an update is waiting. Downloads show one progress bar
with the speed and the time left, and **Pause** / **Cancel**. The gear opens
**Settings**: the channel, the game folders (*Show folder* opens them; *Choose
again* makes the game ask on its next start), the free camera, the installed
versions (play an older one, or install an older release) and the log folder.
Errors come with **Copy details** and **Open log folder**. The launcher never
shows your user name or home folder: paths read `~/...` (Linux) or
`%USERPROFILE%\...` (Windows).

The launcher keeps its files here:

| | Launcher data folder |
|---|---|
| Windows | `%APPDATA%\OpenBFMELauncher` |
| Linux | `~/.local/share/OpenBFMELauncher` |

Inside it, `versions/` holds the installed game versions (the two newest are
kept, plus one you picked), and `launcher.log` records what the launcher did.
To remove everything, delete the launcher's folder and this data folder.

You can also download a game package (`openbfme-<version>-windows-x64.zip` /
`-linux-x64.tar.gz`) directly, unpack it and run `OpenBFME.exe` /
`./OpenBFME.x86_64`. The launcher is the recommended way: it checks every file
for you.

## The first start

When the game starts for the first time it looks for your RotWK and BFME2
folders: in the registry entries the original installers and the All-in-One
launcher write, in the installers' default folders, and on Linux also inside
Wine, Proton (Steam), Lutris, Heroic and Bottles prefixes. It shows what it
found; confirm the folders or choose them with **Browse...**.

It then checks every game archive (size and MD5) against the list of the
English 2.01 / 1.06 files. This takes about a minute the first time and is
cached afterwards. The folders are remembered in `install-paths.cfg` in the
game's data folder (see [Logs](#logs) for where that is); delete that file to
choose again.

OpenBFME only reads your game files. It uses the network only for the
launcher's update check and for LAN games you start yourself.

## Updates

At every start the launcher asks GitHub for the releases of your channel. A
new build is downloaded (an interrupted download resumes next time) and
**verified before anything runs**:

- every release carries a manifest signed with the OpenBFME release key
  (Ed25519); the launcher accepts only a manifest whose signature matches the
  key built into it;
- the downloaded package's size and SHA-256 must match the manifest, and
  every unpacked file must match the manifest's list;
- before **Play**, every file of the installed version is checked again; a
  changed version is never started, and the launcher offers to download it
  again.

A package that fails any check is deleted and nothing is installed. The
launcher never installs an older version by itself; to go back, use
Settings > Versions (*Play this version*, or *Install* an older release). Offline, or when GitHub's
hourly limit is reached, it says so and plays the newest installed version.

The launcher only talks to `api.github.com`, `github.com` and
`*.githubusercontent.com` over HTTPS (and to `bfmeladder.com` only while a
*Download the games* you started runs). It sends no telemetry and needs no
account.

The full design is in [RELEASE.md](RELEASE.md#the-launcher-launch-1).

## Logs

Every run of the game writes its own log file, `openbfme-<date>-<time>.log`.
The 20 newest are kept. Your user name and home folder are removed from every
line.

| | Game data folder | Logs |
|---|---|---|
| Windows | `%APPDATA%\Godot\app_userdata\OpenBFME` | `...\logs\` |
| Linux | `~/.local/share/godot/app_userdata/OpenBFME` | `.../logs/` |

The same data folder holds `install-paths.cfg`, `Options.ini` and your
replays.

You don't have to find the folder by hand:

- **Logs**, next to the version number in the top right corner of the menus,
  opens the folder;
- every error and crash message has an **Open log folder** button that opens
  the folder with that log selected;
- after a crash, the next start tells you which log belongs to the crashed
  run;
- from the command line, `OpenBFME.exe --open-logs` (or
  `./OpenBFME.x86_64 --open-logs`) opens the folder without starting the game.

On Windows, `OpenBFME.console.exe` shows a console window. Its text is **not**
filtered and can show your Windows user name: always send the log file, not a
copy of the console.

## The free camera

The original game limits how far the camera zooms out. OpenBFME has an
optional **free camera** that lets you zoom out to the whole map. It is an
OpenBFME addition, not part of the original game, and it is off by default.
It changes only the camera; the game itself plays the same.

- In the launcher: Settings (the gear), tick **Free camera**. It
  applies the next time the game starts.
- In a game: **Ctrl+Z** turns it on and off.
- On the command line: `-- --free-camera`.
- In `Options.ini`: `OpenBFMEFreeCamera = yes`.

## Controls

OpenBFME reads the key bindings from **your install's own CommandMap.ini**, so
the keys are the original game's. In particular:

| Keys | What they do | Compared with the original |
|---|---|---|
| Right click | give an order (move, attack, ...) | same: the original's default |
| *Alternate mouse setup* option | left click orders instead | same setting (`AlternateMouseSetup` in `Options.ini`); `-- --classic-mouse` forces it for one run |
| Ctrl+1 ... 9 | make a control group | same |
| 1 ... 9 | select the group; press twice quickly to look at it | same (the double press is the original's 5 game frames) |
| Shift+1 ... 9 | add the group to the selection | same |
| Ctrl+F1 ... F8, then F1 ... F8 | store a camera position, jump back to it | same |
| Command bar letters | the underlined letter of a button presses it (A is Attack Move) | same |
| Ctrl held + click | force-attack a unit, building or the ground (also your own) | see [below](#force-attack) |
| S | stop | same |
| D / F / G | stances | same |
| Space | jump to the last radar event | same key, but radar events are not created yet, so it does nothing for now (S-1674) |
| \` | open the spell book (powers) | same |
| F12 | screenshot | same |
| Esc | the game menu (Resume, Options, Restart; LAN: Forfeit, Exit) | same |
| Shift+Up / Shift+Down | nothing | same: 2.01 has no action for them |
| Ctrl+Z | free camera on / off | **OpenBFME only** (no original key uses Ctrl+Z) |

On a German keyboard layout, Y and Z swap as in the original.

### Force-attack

Holding Ctrl for force-attack comes from Command & Conquer Generals: Zero
Hour, the engine BFME was built on. How RotWK 2.01 itself turns force-attack
on could not be found in the program, so this one is an informed guess
(recorded as stop S-1675). The attack it gives once it is on is RotWK's own:
Ctrl+click on the ground makes the selected units fire at that spot until you
give another order; Ctrl+click on a unit or building attacks it, your own
included.

Whether your own units and buildings can be attacked is decided by the weapon,
as in the original: a unit only fires at your own side with a weapon that can
hurt it. Arrows and a trebuchet's rocks never hurt your own side, so archers
and trebuchets ordered onto your own building don't fire.

Alt sends the original's order-mode message, but no order reacts to it yet.

## Known issues

These are the open gaps a player is most likely to notice. Each one is a
recorded stop in [STOPS.md](STOPS.md); the ID is given for reference.

**Not available yet**
- Save and load games; the campaign has no menu between missions and does not
  save your progress the original way (S-1360). Missions load the next one
  directly.
- War of the Ring.
- Online play (LAN only for now).
- Game languages other than English: other installs are refused on the first
  start (S-1561).

**Looks or sounds different**
- Team colours on units and buildings are an educated guess at how the
  original blends them, and may look different (S-119).
- The computer opponent builds, attacks and finishes games, but does not yet
  defend, expand or use heroes the way the original does (S-413, S-415,
  S-417).
- A destroyed enemy building out of your sight disappears at once instead of
  staying in the fog until you look again (S-561).
- Some sounds are missing: units being damaged, scripted music and parts of
  the big-battle ambience (S-1242, S-246, S-248).
- Movies have no subtitles (S-1710). Loading screens don't show the
  mission picture (S-1364).
- Menus are laid out for 1024x768 and scaled; at other window sizes text and
  click targets can be slightly off, and widescreen camera framing may differ
  (S-130, S-455).

**Menus**
- Menus work with the mouse only, not the keyboard or a gamepad (S-108).
- The Options screen's advanced page saves your detail choices, but they don't
  change the graphics yet (S-2481).
- Some lists and boxes of a menu show through pop-ups placed over them
  (S-1914).
- Replays are recorded for every game and can be watched from *Load Replay*,
  but can't be saved under a name, and there are no replay controls (S-1125,
  S-1126). The score screen's graphs are simplified (S-1063).
- LAN: the disconnect screen has no chat (S-1123).

There are many more, smaller stops; the README gives the current count.

## Troubleshooting

**The game can't find my install, or refuses it.** Make sure both games are
installed and patched (RotWK 2.01, BFME2 1.06) and in English. Choose the
folders by hand with *Browse...*: the RotWK folder holds RotWK's archives,
the BFME2 folder BFME2's. The screen says what is wrong with a folder. To
pick again later, delete `install-paths.cfg` in the game data
folder.

**A movie doesn't play.** If your install lacks a movie file (some copies
have the logo movies removed), the game skips that movie, as the original
does, and the log has a `MOVIE ERROR` line naming the file and the folders
searched (`Lang\<language>\Data\Movies`, then `Data\Movies`). If a movie
starts but can't be decoded, the game shows "The movie ... could not be
played" with the reason for a few seconds, then carries on. Either way,
send us the log.

**The game crashed.** Start it again: it tells you which log belongs to the
crashed run and offers **Open log folder**. Send that log (see below). Crash
logs contain a backtrace that tells us where it happened.

**An error message, then the game closes.** That is a problem the game
cannot continue from (OpenBFME stops rather than guessing). The message
names the error and the log; please send both.

**The launcher doesn't start after an update.** It keeps the previous
version as `OpenBFMELauncher.old` (Windows: `OpenBFMELauncher.exe.old`) until
the new one has started once. Rename that file back to the launcher's name.

**The launcher can't update.** It shows why (offline, GitHub's hourly
limit, a failed check). Play still starts the newest installed version.
Keep the launcher in a folder you can write to.

## Reporting a bug

- Open an issue on
  [GitHub](https://github.com/Open-BFME/openbfme-godot/issues), or post in the
  OpenBFME Discord's `#feedback` channel (join from the
  [YouTube channel](https://www.youtube.com/@OpenBFMEGodot) page).
- **Attach the session log** of that run (see [Logs](#logs)). For launcher
  problems, attach `launcher.log` from the launcher data folder.
- Say which version you played (top right corner of the menus, and the first
  line of the log) and what you did when it happened.
- Tell us what looks, sounds or plays differently from the original game:
  that is the most useful feedback of all.
