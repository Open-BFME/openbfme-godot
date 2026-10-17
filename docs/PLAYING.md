# Playing OpenBFME

The player guide: installing, the launcher, updates, logs, controls, known
issues and troubleshooting. For what OpenBFME is and what works, see the
[README](../README.md).

OpenBFME is an unofficial, fan-made rebuild of The Rise of the Witch-king
2.01. It plays on the game you have installed (or the launcher downloads it) and never changes your game
files.

## Contents

- [What you need](#what-you-need)
- [Installing](#installing)
- [The first start](#the-first-start)
- [Updates](#updates)
- [Logs](#logs)
- [The free camera](#the-free-camera)
- [Options](#options)
- [The hero bar and selection](#the-hero-bar-and-selection)
- [Skirmish rules](#skirmish-rules)
- [Skirmish games](#skirmish-games)
- [Saving and loading](#saving-and-loading)
- [Controls](#controls)
- [Online play (test feature)](#online-play-test-feature)
- [Known issues](#known-issues)
- [Troubleshooting](#troubleshooting)
- [Reporting a bug](#reporting-a-bug)

## What you need

- **The Rise of the Witch-king, patched to 2.01**, and **The Battle for
  Middle-earth II, patched to 1.06**, both in **English**. RotWK always needs
  BFME2 next to it, as in the original. All In One launcher installs work.
  A disc install patched with EA's official patch
  (`LotrBfMe2EP1-131073-english.exe`, which updates `INI.big` and `Maps.big`
  in place; the All In One package ships them unpatched with
  `_patch201ini.big` and `_patch201maps.big` next to them) is recognised, but
  **not accepted yet**: its `Data1.big` (fonts, scripts, built-in heroes) is a
  different build from the All In One one, and OpenBFME has not pinned it
  (S-4701). The game says which entry it does not know; the message asks you
  to report it, and `py -3 tools\release\big_entries.py "<game folder>\Data1.big"`
  lists the entries to post. A mix of the two layouts (a half-patched install)
  is refused with the files named.
  Games started from a disc install and from an All In One install do not
  match: players on different layouts cannot join each other's LAN games or
  play each other's replays (the lobby says whose game files differ and that
  both players need the same install), because OpenBFME has not yet shown
  that their patched files are byte-identical (S-4700).
  No games installed? The launcher's **Download the games** gets both from the
  All In One BFME Launcher service (BFME Foundation, Patch 2.22 team): about
  8.6 GB, with Pause, Resume and Cancel. Every file is checked against
  OpenBFME's list of the 2.01 / 1.06 files, and the game offers the folders on
  its first start. It is on the launcher's **Game files** page; to hide it,
  turn off *Offer to download the games* in Settings.
- **Windows 10 or 11** (64-bit) with a graphics card that supports Vulkan or
  Direct3D 12, or **64-bit Linux** with Vulkan graphics (Mesa or the
  proprietary drivers). On Linux the game folders can be inside a Wine or
  Proton prefix.

## Installing

There is **one download per system**. It holds the game and the **OpenBFME
Launcher** together, and the game in it is ready to play: nothing else is
downloaded first.

1. Download the file for your system from
   [GitHub Releases](https://github.com/Open-BFME/openbfme-godot/releases):
   `openbfme-<version>-windows-x64.zip` or
   `openbfme-<version>-linux-x64.tar.gz`. The other files of a release are
   used by OpenBFME itself; you don't need them.
   To check the download, compare its SHA-256 with the release's
   `SHA256SUMS-<version>.txt` (Linux: `sha256sum -c --ignore-missing
   SHA256SUMS-<version>.txt`; Windows PowerShell: `Get-FileHash <file>`).
   Everything OpenBFME downloads after that, it checks by itself.
2. Unpack it into a folder you can write to (not `Program Files`: the
   launcher updates itself in its own folder). On Linux use `tar -xzf`, which
   keeps the programs executable; if your archive tool lost that, run
   `chmod +x OpenBFME.x86_64 OpenBFMELauncher.x86_64`.
3. Start either program:
   - **the game**, `OpenBFME.exe` or `./OpenBFME.x86_64`, to play at once;
   - **the launcher**, `OpenBFMELauncher.exe` or
     `./OpenBFMELauncher.x86_64`, for the news, the settings, older versions
     and the games download, and **Play**.

   On Windows, SmartScreen may say *Windows protected your PC* because the
   executables are not code-signed yet: choose **More info**, then **Run
   anyway**. Windows asks this once per new file.
4. The first start asks for your game folders (next section).

Whichever one you start keeps OpenBFME up to date (see [Updates](#updates)).
Already have the launcher from an earlier build? You don't need to download
anything: it updates itself and the game.

Releases have plain numbers: Alpha 0.4.5, then 0.4.6 and so on; the middle
number goes up (0.5.0) when a big new feature lands. The launcher's channel
is **Alpha** (these releases); **Preview** also gets test builds between
releases. Both get every release, and you can switch in Settings.

The menu on the left has **Play** (the installed version, whether it is up to
date, your games and What's New from the version's release notes), **News**
(the notes of every release), **Multiplayer** (online play is coming; LAN
games are in the game), **Mods**, **Game files** (where your games are: *Show
folder*, *Choose again* makes the game ask on its next start, and the games
download) and **Settings** (General: the channel, the free camera, updating
by itself, the games download; About & logs: the versions, the log folder,
older releases).

Updates download in the background, while the launcher is open (it looks
again every hour) or while you play: **Play** keeps working during a download
and starts the installed version, and the new version is used from your next
Play on. Turn off *Update automatically in the background* to update only when
you press **Update**. Downloads show one progress bar with the speed and the
time left, and **Pause** / **Cancel**. A version that is being played is never
removed by an update.
Errors come with **Copy details** and **Open log folder**. The launcher never
shows your user name or home folder: paths read `~/...` (Linux) or
`%USERPROFILE%\...` (Windows).

The launcher keeps its files here:

| | Launcher data folder |
|---|---|
| Windows | `%APPDATA%\OpenBFMELauncher` |
| Linux | `~/.local/share/OpenBFMELauncher` |

Inside it, `versions/` holds the game versions installed by updates (the two
newest are kept, plus one you picked), and `launcher.log` records what the
launcher did. The game that came in your download stays in the folder you
unpacked and is never changed.
To remove everything, delete the folder you unpacked and this data folder.

## The first start

When the game starts for the first time it looks for your RotWK and BFME2
folders: in the registry entries the original installers and the All-in-One
launcher write, in the installers' default folders, and on Linux also inside
Wine, Proton (Steam), Lutris, Heroic and Bottles prefixes. It shows what it
found; confirm the folders or choose them with **Browse...**.

It then checks every game archive (size and MD5) against the list of the
English 2.01 / 1.06 files; `Data1.big` (fonts, scripts and the built-in
Create-a-Heroes) is checked file by file inside it, because the disc and All
In One copies of it differ. This takes about a minute the first time and is
cached afterwards. The folders are remembered in `install-paths.cfg` in the
game's data folder (see [Logs](#logs) for where that is); delete that file to
choose again.

OpenBFME only reads your game files. It uses the network only for the
launcher's update check, for LAN games you start yourself and, when you join
or host an online test game, for the lobby and relay of that game (see
[Online play](#online-play-test-feature)).

## Updates

Both programs keep OpenBFME up to date, through the same updater (the
launcher's):

- **Started the launcher?** It looks for a newer build at every start and
  every hour while it stays open.
- **Started the game directly?** It asks the launcher next to it, without
  opening its window. If a newer version is already installed, that version
  starts instead. Then the launcher looks for an update in the background
  while you play, and a line in the top right corner of the menu says what it
  found: up to date, downloading, installed for your next start, or why it
  was skipped.

A newer version is never put in place while you play: it is used **from your
next start**. Starting the game never waits for the network: offline, or when
GitHub can't be reached, it starts at once with what you have and says why.
To start the game without the check, add `--no-update-check`. A game-only
folder (no launcher next to the game) does not update.

Before it starts the launcher, the game checks that it is the signed one of
your download (or a signed update of it). The launcher checks the game's folder
the same way before **Play**: a changed file is refused, by name.

At every check the launcher asks GitHub for the releases of your channel. A
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

While the launcher stays open it checks again every hour. An update downloads
in the background: **Play** keeps working and starts the version you have
until the new one is installed, and a version that is being played is never
replaced. To update only when you press **Update**, turn off *Update
automatically in the background* in Settings > General. Only one launcher
runs at a time; starting a second one says the first is already open.

A package that fails any check is deleted and nothing is installed. The
launcher never installs an older version by itself; to go back, use
Settings > About & logs (*Play this version*, or *Install* an older release). Offline, or when GitHub's
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

The original game limits how far the camera zooms out. OpenBFME lets you zoom
out **2.5 times as far** by default: OPENBFME, Camera tab, **Max zoom-out**
(Original, 1.5x, 2x, 2.5x, 3x; `OpenBFMEMaxZoomOut` in `Options.ini`,
`-- --zoom-out=1` for the original limit). A game still starts at the
original height; the mouse wheel takes you higher. Past the map's edge the
ground fades to black, as the original game draws it.

The optional **free camera** goes further: it lets you zoom out to the whole
map. Both are OpenBFME additions, not part of the original game; the free
camera is off by default. They change only the camera; the game itself plays
the same.

- In the launcher: Settings > General, turn on **Free camera**. It
  applies the next time the game starts.
- In a game: **Ctrl+Z** turns it on and off.
- On the command line: `-- --free-camera`.
- In `Options.ini`: `OpenBFMEFreeCamera = yes`.

## Options

The original **Options** screen is the game's own, and its settings now take
effect: **Graphics** (the detail level and **Custom Settings**) sets the particle
limit, the texture quality (for models loaded after the change, so from the
next game) and whether every health bar shows; **Brightness** brightens or
darkens the picture; **Scroll Speed** sets the camera's scroll speed. The
detail rows OpenBFME cannot draw yet (model, animation, shadows, terrain,
water, shader detail) are greyed out on the Custom Settings page, and so are
**Movie**, **EAX3** and **High Audio Quality**. A texture quality change shows
from the next game you load. The **Resolution** row says where the window size
is set: OPENBFME, Display tab.

OpenBFME's own options are an addition: in **Options** (from the main menu, or
from the game menu during a game) press **OPENBFME** in the bottom left
corner, above Cancel. The OpenBFME settings take the place of the three
panels; the tabs are on the left: **Display** (also **Widescreen view**:
Original is the original game's view, which shows less from top to bottom on
screens wider than 4:3; Wider shows the 4:3 view from top to bottom and more
to the sides), **Graphics+** (anti-aliasing,
anisotropic filtering, original or enhanced look, interface scale for large
screens, soft particle edges), **Camera** (scroll speed, edge scrolling,
rotation speed, max zoom-out, the free camera, inverted drag scrolling, keeping
the mouse in the window), **Interface** (when health bars show: **Always (all
players)** shows every unit's and building's bar, yours and the enemy's; the
original health-bar and mouse options, a game clock), **Audio** (master volume, mute in
the background, output device) and **Controls** (the game's hot keys). The
defaults behave like the original game, except the max zoom-out (2.5x). Every
change shows at once;
**Accept** keeps it and **Cancel** or **Esc** puts back what you had (both
return to the Options screen), **Reset to defaults** sets the defaults. Point
at a setting to read what it does. They are kept in `Options.ini` as keys starting
with `OpenBFME`; the original game ignores them. The full list is in
`docs/OPTIONS.md`.

### Display tab

| Option | Choices | Default |
|---|---|---|
| Window mode | Windowed, Borderless window, Fullscreen | Windowed |
| Resolution | the window's size: common sizes that fit your monitor, and the monitor's own size | the size the window has |
| Vertical sync | On, Off, Adaptive | On |
| Frame rate limit | Off, 30, 60, 120, 144, 240 fps or a custom number | Off |
| Show the frame rate | frames per second and the frame time in the top-left corner | off |
| Render scale | 50% to 100% of the window's pixels for the 3D view (menus and the HUD stay sharp) | 100% |
| Upscaling | Bilinear, AMD FSR 1, AMD FSR 2 | Bilinear |

- **Fullscreen** is exclusive full screen on Windows; on Linux it is the same
  as **Borderless window**. Both cover the monitor at its own resolution, so
  *Resolution* only applies to a window; lower the *Render scale* instead.
- **FSR 1** sharpens a lower render scale; **FSR 2** also smooths edges, even
  at 100%. The game uses Godot's Forward+ renderer, which has both. (With
  Godot's Mobile renderer only FSR 1 would be offered, with Compatibility
  only Bilinear.) FSR 3 and DLSS are not available yet.
- **Keys** (OpenBFME only; no original key uses them): **F11** shows or hides
  the frame rate, **Alt+Enter** switches between a window and full screen.
- The settings are kept in `openbfme-display.cfg` next to `Options.ini` in
  the game's user folder (see [Logs](#logs) for where that is). For one run,
  `-- --res=1280x720`, `-- --vsync=off`, `-- --max-fps=60` and `-- --fps`
  override them without changing the file.

## The hero bar and selection

- **The hero bar** runs along the bottom of the screen, beside the Palantir.
  Its first button is your builders: it shows how many are idle, and a click
  selects the nearest idle one. Each of your heroes follows with their
  portrait, health and rank. Click a portrait to select that hero; click it
  again while the hero is the only one selected to move the camera to them.
  The last button selects all your heroes. The original's keys for "select
  nearest builder" and "select all heroes" (from your CommandMap.ini) do the
  same. A hero who dies leaves the bar and is revived from the fortress, as
  in the original.
- **Selection rings.** Selected units show the original's ring under them in
  your colour; a battalion gets one outline around all its soldiers, a hero
  the rune ring. Pointing at a unit without clicking shows no ring, and
  buildings get none, as in the original.
- **Build bubbles.** A selected building shows its buttons (units to train,
  upgrades, the fortress's heroes) as round bubbles around it. Right-click a
  bubble to cancel what it queued.
- **The builder's side panel.** With a builder selected, the buildings it can
  build are listed in the carved side panel.
- **Help boxes.** Hold the pointer over a bubble or a Palantir button to see
  its name, key, cost and description.
- **Autocast.** Right-click a hero's ability button on the Palantir to let the
  hero use that ability by themselves: the button glows while autocast is on,
  and the hero casts it when a target comes in range and the ability is
  ready. Right-click it again to turn it off. A left click still casts it
  once, as before.

## Skirmish rules

The skirmish lobby's **Rules** tab is the original's: the starting
**resources** (500 to 4000, 1000 by default), the **command point** limit
(from a third of the normal limit up to 100 times), and whether **custom heroes** (Create-a-Hero) and **ring heroes** are
allowed. The rules travel with a LAN game and are kept in its replay.

**Legendary Heroes** is an OpenBFME addition on the same tab, off by default
and not part of the original game: every player's heroes get 2, 3 or 4 times
their health, take less damage and hit harder. The loading screen names the
rule when it is on. A game with it on plays differently from the original;
with it off nothing changes.

### Fortress construction

Enable **Fortress construction** in the match's Rules tab (under OpenBFME
extensions in the map browser). It defaults to Off. Selecting your completed
fortress opens your faction builder's building menu on the right. Choose a
building and place/rotate it normally; construction starts immediately at the
site without a builder, using normal prices, placement checks and build times.
The fortress keeps its ordinary recruitment and upgrade commands. This applies
to every player and travels with match setup and replays. The menu comes from
the faction's starting builder template, including mod-defined command sets.

## Skirmish games

- **Your profile.** The first time you open Skirmish, the game asks for a
  profile name, as the original does. The name is kept for your next games.
- **The loading screen** shows every player's name, team, army and rank icon
  while the map loads. A campaign mission loads behind its own picture.
- **How a game is won.** As in the original, you are out when your last
  building falls: builders and armies left in the field don't keep you in
  the game. The last team standing wins.
- **Sandbox.** Put every player on one team (or play alone) and the game
  starts in sandbox mode: nobody wins, so you can build and try things for as
  long as you like. A player who loses every building is still defeated.
- **Placing a building.** The preview follows the pointer as in the original:
  in your colour and see-through, playing the building's animation, red with
  a red base where it can't go. A harbour snaps to the shore. Hold the right
  button and drag to scroll the map while you place; a quick right click
  cancels the placement.
- **The computer opponent** builds heroes, uses its spell book, and lets its
  heroes and units use their powers.

## Saving and loading

You can save a skirmish against the computer or a campaign mission and load
it later, from the same screens as the original.

- **Saving.** Press Esc in the game and choose **Save**. The save page lists
  your saved games. Type a name (or keep the suggested "Saved Game 2" and so
  on) and press Save; choosing an existing save overwrites it after asking.
  "Game Saved" confirms it.
- **Loading.** Choose **Load** in the same menu, or **Load Game** in the main
  menu. Pick a save and press Load. Over a running game you're asked first,
  because that game is lost.
- **Catching up.** A save holds the game's start and every order given since.
  Loading replays those orders at full speed behind the loading screen, which
  says "Catching up with the saved game", and you take over at the moment you
  saved. This takes about 1.5 to 2 minutes for an hour-long game on a fast PC,
  and longer on slower ones. A faster kind of save is planned.
- **Saves only load in the version that made them.** After an update, older
  saves are greyed out on the Load page. Choosing one tells you which version
  made it. Keep that version if you want to finish the game.
- **Where they are.** Saves go into the original game's Save folder in your
  user data folder (the one with your Create-a-Hero heroes), as `.obfmesave`
  files, so the original game never sees them.
- **Campaign missions** save the same way, on the Campaign page. Loading one
  plays the mission's orders again and the campaign goes on from there.
- **Not yet:** saving between campaign missions, multiplayer saves, and
  loading the original game's own saves (they show in the list greyed out,
  with a note).
- **Deleting.** Select a save and press Delete; you're asked first.
- **Uber performance mode.** A game saved with the Uber performance mode on
  loads with it off, and the reverse. The saved part is replayed as it was
  played, and the game goes on in your current mode.

## Controls

OpenBFME reads the key bindings from **your install's own CommandMap.ini**, so
the keys are the original game's. In particular:

| Keys | What they do | Compared with the original |
|---|---|---|
| Right click | give an order (move, attack, ...) | same: the original's default |
| Right drag while placing a building | scroll the map; the placement stays | same |
| *Alternate mouse setup* option | left click orders instead | same setting (`AlternateMouseSetup` in `Options.ini`); `-- --classic-mouse` forces it for one run |
| Ctrl+1 ... 9 | make a control group | same |
| 1 ... 9 | select the group; press twice quickly to look at it | same (the double press is the original's 5 game frames) |
| Shift+1 ... 9 | add the group to the selection | same |
| Ctrl+F1 ... F8, then F1 ... F8 | store a camera position, jump back to it | same |
| Command bar letters | the underlined letter of a button presses it (A is Attack Move) | same |
| Ctrl held + click | force-attack a unit, building or the ground (also your own) | **OpenBFME only**: 2.01 has no force-attack, see [below](#force-attack) |
| S | stop | same |
| D / F / G | stances | same |
| Space | jump to the last radar event | same key; it jumps to a mission script's radar ping. Other events don't feed it yet, so in a skirmish it does nothing (S-1674) |
| \` | open the spell book (powers) | same |
| F12 | screenshot | same |
| Esc | the game menu (Resume, Options, Restart; LAN: Forfeit, Exit) | same |
| Shift+Up / Shift+Down | nothing | same: 2.01 has no action for them |
| Ctrl+Z | free camera on / off | **OpenBFME only** (no original key uses Ctrl+Z) |
| F11 | frame rate counter on / off | **OpenBFME only** (no original key uses F11) |
| Alt+Enter | window / full screen | **OpenBFME only** (no original key uses Alt+Enter) |

On a German keyboard layout, Y and Z swap as in the original.

### Force-attack

Force-attack with Ctrl is an OpenBFME addition. It comes from Command &
Conquer Generals: Zero Hour, the engine BFME was built on. RotWK 2.01 still
carries the key's name and the attack's code, but nothing in the program ever
switches the mode on (this was checked in the program itself, stop S-1675).

What Ctrl does today: in OpenBFME, holding Ctrl while you click force-attacks.
The original differs: there, holding Ctrl while you press the left button
starts a selection made from the points the pointer passes, instead of the
usual box (stop S-4360). OpenBFME does not have that selection yet, and
whether it keeps Ctrl for force-attack is still to be decided.

The attack it gives is RotWK's own code: Ctrl+click on the ground makes the
selected units fire at that spot until you give another order; Ctrl+click on
a unit or building attacks it, your own included.

Whether your own units and buildings can be attacked is decided by the weapon,
as in the original: a unit only fires at your own side with a weapon that can
hurt it. Arrows and a trebuchet's rocks never hurt your own side, so archers
and trebuchets ordered onto your own building don't fire.

Alt sends the original's order-mode message, but no order reacts to it yet.

## Online play (test feature)

Online play is a **test feature**: it works, but there is no public OpenBFME
lobby yet, so it only works when someone runs a lobby for you. The whole
Online menu is a test feature.

How it works: a **lobby** holds the game list and builds the game's start; a
**relay** passes every turn between the players, so nobody sees anyone
else's address and no router needs a port opened. The relay keeps a log of
the game's orders, from which a replay of the game can be made.

**From the main menu.** Choose **Online**. The game opens RotWK's own online
screens, connected to the OpenBFME lobby instead of the old GameSpy service:

1. **Sign in.** Type a name in the **Nickname** box and press **Log In**. The
   lobby signs you in as a guest under that name (there are no accounts yet,
   so the e-mail and password boxes and the account buttons are switched
   off). The game remembers the name for next time.
2. **Home** shows the lobby's message of the day, or says that the lobby has
   none.
3. **Open Play** is the server browser: the list of open games (name, map,
   players; a lock means the game has a password). Click a column header to
   sort, **Refresh** to fetch the list again, **Join Game** to join the
   selected game, or **Create Game** to host one (a name and, if you want, a
   password).
4. The **staging room** is the same screen as a LAN game's: the slots,
   factions, colours, teams, the map and the rules, chat and **Ready**. The
   host presses **Play Game** once everyone is ready.
5. If the lobby has no relay yet, it refuses the start and the game says
   "Online games can't start yet: no relay server is available." With a
   relay, the game loads and plays through it.

**If your game closed during a match** (a crash, a lost connection), start
OpenBFME again, choose **Online** and log in with the same name. The game
asks whether to rejoin your running game; you can also pick it in Open Play,
where it is listed with **REJOIN**, and press **Join Game**. The game then
catches up with the match (a line on screen shows the progress) and puts you
back in it. Other running games are listed with **WATCH**: Join Game on them
lets you watch as a spectator.

Joining a match that has already started as a new player ("hot join") is
not possible.

Quick Match, Strategic (War of the Ring online), Profile / Stats and the
friends list are not available yet (S-4075).

**Which lobby.** Release builds use `https://play.openbfme.com`. To use
another one, start the game with `--online-lobby=<the lobby's address>`, or
set the `OPENBFME_LOBBY` environment variable, or put
`OpenBFMEOnlineLobby = <the lobby's address>` in Options.ini.

**Joining a game.** The host gives you a link such as
`openbfme://join/0123456789abcdef` (or a web page that shows it). Once the
launcher handles these links, clicking the link starts OpenBFME in that game.
To make the launcher handle them, start it once from a terminal or command
prompt with:

```
OpenBFMELauncher --headless -- --register-url-handler --lobby=<the lobby's address>
```

On Windows this adds an entry for your user only (no administrator rights).
On Linux and the Steam Deck it adds a hidden `openbfme-url.desktop` entry in
`~/.local/share/applications`.

You can also start the game straight from a terminal:

```
OpenBFME -- --online-lobby=<the lobby's address> --online-join=0123456789abcdef --online-name=YourName
```

The game signs you in as a guest under that name, joins, says you are ready
and waits for the host to start. Everyone needs the same OpenBFME version and
the same RotWK 2.01 / BFME2 1.06 files; the lobby refuses a player whose files
or build differ, and says so.

**Hosting a game** (for testers):

```
OpenBFME -- --online-lobby=<the lobby's address> --online-host --map="maps/map mp fords of isen ii/map mp fords of isen ii.map" --online-slots="player:-1:0:0;open" --online-players=2
```

The log shows `GAME ONLINE hosting game <id>: invite openbfme://join/<id>`.
Send that link to the other players. The game starts once they are in and
ready. In `--online-slots`, each slot is `state:faction:start:team`, where
state is `player` (you), `open` (for someone joining), `closed`, or an AI
level (`easy`, `medium`, `hard`, `brutal`). The faction is a number, and -1
means random.

The session log (see [Logs](#logs)) has `GAME ONLINE` lines for each step,
and the reason when the lobby refuses something.

**When someone loses their connection** (also a test feature). The others
see the usual disconnect screen with each player's timer. If the timer runs
out, the player is not removed at once as on a LAN: their army stays theirs
and stands still for five minutes, so they can come back. If they don't, their
army goes to an ally, as on a LAN. Pressing **Kick** on the disconnect screen
(all the other players) hands the army over at once.

**Rejoining after a crash or a lost connection.** Start the game again on the
same computer with:

```
OpenBFME -- --online-lobby=<the lobby's address> --online-rejoin
```

The game signs in as the same player (it keeps the lobby session in its user
data), asks the lobby for your running game and gets a new pass for it. The
screen shows "Rejoining the game: catching up" while the game replays
everything that happened since the start at full speed, then you are back.
The other players wait a moment at the end of it. Catching up grows with the
game's length: on a fast PC about 10 seconds for a ten-minute game, a minute
for half an hour, two minutes for an hour. If you are in more than one
running game, add its id: `--online-rejoin=<game id>`.

**Pausing a network game.** Press **Pause** (the Pause/Break key). The game
stops for every player at the same moment, and the screen says who paused
it. Any player presses Pause again to resume. The original game cannot pause
a multiplayer game at all; this is an OpenBFME addition.

**Watching an online game as a spectator.** Anyone who is not a player can
watch a game with its id:

```
OpenBFME -- --online-lobby=<the lobby's address> --online-spectate=<game id>
```

If the game has not started, the game waits for the host to start it; you
can also join a game that is already running (the game first replays what
happened, at full speed). You see the whole map with no fog of war and every
player's army. Press **Tab** to see one player's view, with their resources,
command points and power on screen; Tab again for the next player, then back
to everyone. **Shift+Tab** goes the other way. The host can set a spectator
delay in the lobby (in seconds): you then watch that far behind the players,
so you cannot tell them what the other side is doing. A spectator never
slows the players down.

## Known issues

These are the open gaps a player is most likely to notice. Each one is a
recorded stop in [STOPS.md](STOPS.md); the ID is given for reference.

**Not available yet**
- Save and load games; the campaign has no menu between missions and does not
  save your progress the original way (S-1360). Missions load the next one
  directly.
- War of the Ring.
- Online play is a test feature: the Online menu works against an OpenBFME
  lobby, but there is no public one yet, and Quick Match, War of the Ring
  online and the stats pages are missing (see
  [Online play](#online-play-test-feature); S-4070 .. S-4076).
- Game languages other than English: other installs are refused on the first
  start (S-1561).

**Looks or sounds different**
- Team colours on units and buildings are an educated guess at how the
  original blends them, and may look different (S-119).
- The computer opponent builds, attacks and finishes games, and uses its
  heroes, spell book and powers, but it does not yet defend or expand the way
  the original does, and it skips some powers (S-413, S-415, S-417, S-1421).
- Units and buildings cast no shadows yet, so the building preview has none
  either (S-3304).
- A destroyed enemy building out of your sight disappears at once instead of
  staying in the fog until you look again (S-561).
- Some sounds are missing: units being damaged, scripted music and parts of
  the big-battle ambience (S-1242, S-246, S-248).
- Movies have no subtitles (S-1710).
- Menus are laid out for 1024x768 and scaled; at other window sizes text and
  click targets can be slightly off (S-130, S-455).

**Menus**
- Menus work with the mouse only, not the keyboard or a gamepad (S-108).
- Skirmish's profile page (PROFILE, then the statistics entry) opens, but its
  statistics are not filled in yet and show placeholder labels (S-175).
- The graphics detail settings set the particle limit, texture quality and
  health bars, but OpenBFME doesn't draw shadows yet, and the model,
  animation, terrain, water and shader detail rows are greyed out (S-3402).
  A texture quality change shows from the next game.
- Replays are recorded for every game and can be watched from *Load Replay*,
  but can't be saved under a name, and there are no replay controls (S-1125,
  S-1126). The score screen's graphs are simplified (S-1063).
- LAN: the disconnect screen has no chat (S-1123).
- Online: when a player's game crashes, the others drop that player when the
  disconnect screen's bar runs out, but only if the game's first player still
  in it (the packet router) runs this version or later: everyone in an online
  game must update, or a crash can still freeze it (S-4810).
- Online and LAN: a player still loading the map about a minute after the
  others finished is treated as gone and dropped when the disconnect screen's
  bar runs out (S-4811).

**In-game interface**
- Hero bar: pointing at a hero's portrait shows no help box, the portrait does
  not flash when the hero is attacked, and a Create-a-Hero hero does not show
  its own picture (S-2521, S-2703).
- Help boxes: hero ability, spell, repair and revive buttons don't show their
  cost yet, and the spell book has no help box (S-2702, S-2703).
- Build bubbles don't flash and don't show how many units are queued
  (S-2701).
- Selection rings always use the highest-detail look, whatever the detail
  setting (S-2522).
- Turning a hero's autocast on or off doesn't play the hero's voice line
  (S-3460).

There are many more, smaller stops; the README gives the current count.

## Troubleshooting

**The game can't find my install, or refuses it.** Make sure both games are
installed and patched (RotWK 2.01, BFME2 1.06) and in English. Choose the
folders by hand with *Browse...*: the RotWK folder holds RotWK's archives,
the BFME2 folder BFME2's. The screen says what is wrong with a folder: which
file differs and what to do. A RotWK folder that mixes the two 2.01 layouts
(for example `INI.big` patched by EA's patch but `Maps.big` not, or EA's patch
applied and the All In One `_patch201*.big` files copied in as well) is
refused: patch a clean disc install with EA's official 2.01 patch only, or let
the launcher download the games. If the screen says your `Data1.big` holds a
version OpenBFME does not know and your install is unmodified, please report
the message on GitHub so that version can be added. To pick again later,
delete `install-paths.cfg` in the game data folder.

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

**The game says "Updates skipped" or "Update not installed".** The line in
the corner of the menu names the reason (offline, GitHub's hourly limit, a
download that failed its check). The game you have still starts; the check
runs again at your next start, and an interrupted download resumes. Open the
launcher for the details: it shows the last failure and has **Open log
folder**.

**The game still shows the old version after an update.** An update is used
from the next start: close the game and start it again. If you unpacked a
new download next to an old one, start the game from either folder: both
start the newest installed version.

**Which file do I download?** One file per system:
`openbfme-<version>-windows-x64.zip` or `openbfme-<version>-linux-x64.tar.gz`.
The `openbfme-launcher-...` files are update files for launchers from before
this layout; you don't need them.

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
