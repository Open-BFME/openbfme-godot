# OpenBFME changelog

What changed in each release, newest first, in the shape of the release pages (Added, Changed, Bug fixes, Removed; bullets under an
area). The release pages on [GitHub](https://github.com/Open-BFME/openbfme-godot/releases) are the source for every release, with
the download table and the known issues of that build; this file keeps the history that used to sit in the README, and the first
alpha builds' notes. The rules for writing the lines are in [RELEASE.md](RELEASE.md), "Notes".

## v0.4.0-alpha.4

### Added

**Units and heroes**
- Transport ships carry hordes between shores, boarding and landing at the shore spot.
- Battle ships fight, sink and get sunk.
- Ships and hovering units sail and glide at water level.

### Changed

**Performance**
- Big battles stay smooth on screen: the picture no longer waits for the game's simulation. A 10,000-unit battle that crawled at about 7 frames a second now draws at about 30.

### Bug fixes

**Units and heroes**
- Oathbreaker hordes now walk.

**Buildings**
- Building sites clear units out of the way before construction starts.
- Castle camps and castle upgrades are paid for and limited the way the original game does it.

## v0.4.0-alpha.3

### Added

**Online (a test feature)**
- Server browser on the original online screens: an Open Play list with sortable columns, and Join Game.
- Rejoin: a player whose game closed can rejoin the running game. A game you left or lost shows a REJOIN offer.
- Spectating: watch a running game with a delay and step through the players' views. Games you can watch show a WATCH row.
- The host can pause an online game.
- The launcher can register game invite links, and the game can join a game from a lobby server that someone hosts.

**Saves**
- Skirmish games and campaign missions can be saved and loaded from the Save and Load screens. Loading a save replays the game up to the moment you saved it.

**Performance**
- Uber performance mode, a new Options entry: the next game and LAN lobby use every CPU core. It is not compatible with the original game, and Uber players only play and replay with other Uber players.

### Changed

**Performance**
- Faster path finding across long distances (the original hierarchical path search): big armies cross large maps with less stutter.

**Linux**
- The Linux build runs on older systems (GLIBC 2.18 or newer).
- Replays and online games match between Windows and Linux: the game uses its own deterministic maths library.

### Bug fixes

**Units and heroes**
- Flying units (the fell beasts, Drogoth and the other flyers) fly, swoop and fight instead of standing still.
- Heroes strike hordes in melee again.
- Unit stances (hold ground, battle, aggressive) use the original game's guard behaviour.

**Buildings**
- A building that is still being built no longer recruits units.
- Buildings show their construction stages and their damage states.

**Interface**
- A left click on empty ground deselects.
- A right click on a hero's recruit button cancels it and refunds the cost.
- An upgrade in progress shows as in progress on its bubble.
- The radar's view box follows the tilted camera.

**Performance**
- Selecting a large army no longer slows the game down.

**Gameplay**
- A game after Helm's Deep or Minas Tirith no longer starts with the wrong starting units: every game starts from freshly loaded game data.

## v0.4.0-alpha.1

The first alpha build. Launchers on v0.3.0-preview.5 or later offer it on the *Alpha* channel.

### Added

**Gameplay**
- Defeat as in the original: you're out when your last building falls; builders and armies left in the field no longer keep a player in the game.
- Sandbox mode: put every player on one team, or play alone, and nobody wins.
- The profile prompt: the first visit to Skirmish asks for your profile name, as the original does, and keeps it.

**AI**
- The computer opponent uses its spell book and hero powers. It also builds heroes, and its heroes and units use their abilities in battle.

**Buildings**
- The building preview looks like the original: it's in your colour and see-through, plays the building's animation, turns red with a red base where it can't go, and a harbour snaps to the shore.
- Scroll while placing a building: hold the right button and drag to move the map without losing the placement; a quick right click still cancels it. The pointer shows the scroll arrow.

**Interface**
- Loading screens as in the original: a skirmish shows every player's name, team, army and rank icon; a campaign mission loads behind its own picture.

### Bug fixes

**Interface**
- Menus no longer overlap: only one screen is drawn at a time, a pop-up covers the menu under it, and the pause menu covers the build bubbles.

## v0.3.0-preview.5

This preview carries the launcher that moves everyone to the alpha builds: once your launcher has updated itself to this version, it offers the alpha builds (v0.4.0-alpha.1 and later) on the same channel, now called *Alpha*.

### Added

**Launcher and install**
- A new launcher look: a menu on the left (Play, News, Multiplayer, Mods, Game files, Settings), the release notes on parchment, and updates that download in the background while you play. Play keeps starting the version you have until the new one is installed. Only one launcher runs at a time.
- The test channel is shown as *Alpha*, and the version text in the menus reads "unofficial alpha build".

**Graphics**
- The detail level and Custom Settings in Options set the particle limit, the texture quality and the health bars. Detail rows OpenBFME can't draw yet (shadows, models, terrain, water ...) are greyed out.
- Zoom out 2.5 times farther: the mouse wheel takes the camera up to 2.5 times the original height; change it in OPENBFME, Camera tab, Max zoom-out (from the original limit to 3x). Past the map's edge the ground fades to black, as in the original.

**Interface**
- OpenBFME's settings inside the Options screen: the OPENBFME button opens them as a page of the original Options screen, drawn sharp at any window size.
- Health bars for every unit you can see: the Interface tab's Always (all players) shows the bar of every unit and building in sight, yours and the enemy's.
- Brightness and Scroll Speed work.

**Units and heroes**
- Autocast hero abilities: right-click a hero's ability button and the hero uses the ability by themselves; right-click again to stop.
- Legendary Heroes, a new OpenBFME rule on the Rules tab (off by default): heroes get 2, 3 or 4 times their health and hit harder.

**Gameplay**
- The Skirmish Rules tab sets the starting resources, the command point limit, custom heroes and ring heroes, as in the original.

## v0.3.0-preview.4

### Added

**Graphics**
- Display options: the OPENBFME button in Options opens windowed, borderless window or fullscreen, the resolution, vertical sync, a frame rate limit, a frame rate counter and a render scale with AMD FSR 1 or FSR 2 upscaling. F11 shows the frame rate, Alt+Enter switches between a window and full screen.
- Selection rings: selected units stand on the original's rings in your colour; a selected battalion gets one outline around all its soldiers and a hero gets the rune ring.

**Interface**
- The hero bar at the bottom of the screen shows your heroes' portraits with their health and rank, and a builder button with the number of idle builders. Click a portrait to select the hero, click it again to look at them; a button selects all your heroes.
- Build bubbles: selecting a building shows its buttons as round bubbles around it, as in the original (the fortress's heroes and upgrades among them).
- The builder's side panel has the original's carved frame.
- Help boxes: holding the pointer over a button shows its name, key, cost and description.
- Options text is in RotWK's colours instead of green, and the credits titles are bold, as in the original.

### Bug fixes

**Audio**
- A crash on the sound thread when the game was busy is gone.

## v0.3.0-preview.3

### Added

**Buildings**
- Building placement previews: choosing a building to build shows it under the pointer, tinted red where you can't build, for every faction, fortresses included.

**Interface**
- The selection box: dragging with the left mouse button draws the original's selection box, and letting go selects your units inside it.
- Move markers on the ground: a move order shows the original's marker where your units are going.
- The credits screen: Credits in the main menu rolls the original credits.
- Custom Settings opens the advanced options in the Options screen.
- The window title reads "OpenBFME".

**Gameplay**
- Tribute: send resources to an ally from the flag above the radar, once the game's waiting time has passed.

**Launcher and install**
- A redesigned launcher that starts on the right channel: a preview launcher installs the newest preview, and release notes read as plain text.

### Bug fixes

**Units and heroes**
- Archers spread their fire: a battalion of archers shoots at the whole enemy battalion, as in the original, instead of every archer aiming at the nearest soldier.

**Interface**
- An upgrade you can't afford says "not enough money" and is not bought.

**Audio**
- The menu music stops when a campaign starts, before the opening movie.

**Campaign**
- Missing intro movies are skipped quietly, as in the original, instead of showing an error.

## v0.3.0-preview.2

### Added

**Units and heroes**
- Force-attack on the ground: hold Ctrl and click a spot on the ground and the selected units fire at it until you give another order. As in the original, a unit only fires at your own side with a weapon that can hurt it: archers and trebuchets won't shoot at your own buildings.

**Interface**
- The Options Advanced page lists the nine detail settings with their values and saves your custom choice.

### Changed

**Gameplay**
- Combat details from the original: disabled and paralysed units, temporary weapons, turrets that aim before they fire, and the original's splash damage rules.

### Bug fixes

**Online**
- Multiplayer games stay in sync when players leave.

## v0.3.0-preview.1

The first preview built for automatic updates through the launcher. It fixes what came up in the first hands-on play sessions.

### Added

**Gameplay**
- Fortress maps start in the fortress: on Helm's Deep and the other fortress maps you own the map's fortress instead of building a castle, and gates open and close with their button.
- Orders like the original: right-click gives orders by default, with the move marker on the ground; after a victory or defeat the game moves on to the score screen as the original does.

**Units and heroes**
- Force-attack on units and buildings: hold Ctrl and click a target, even one of your own.
- Health bars, levels and building progress: selected or hovered units show their health bar (Options' Health Bars adds infantry and cavalry), buildings show retail's structure bars and "Building: n%", battalions show their veterancy marks, and a selected hero shows their level and experience bar under the portrait.

**Interface**
- Control groups and hotkeys: Ctrl+number makes a group, the number selects it, a double press jumps the camera there; camera bookmarks (Ctrl+F1 to F8, then F1 to F8) and the command bar's letter hotkeys work.
- The radar looks like RotWK's: a dot for every soldier, a halo for heroes, a ring for the fortress, walls in their real shape, the gold view box, and a ping on the Palantir when your units are attacked.
- Clicks pick like the original: your own units win a click over enemies behind them, and hovering, ordering, double-clicking and box selection each follow RotWK's rules.
- Easier bug reports: Logs in the menus and Open log folder in error and crash messages.

**Create-a-Hero**
- My Heroes opens the hero builder: pick a class, powers and colours, name your hero and save. Your heroes can be fielded in skirmish and LAN games.

**Launcher and install**
- The launcher can download the games for you through the BFME Foundation's All In One service (original 2.01 / 1.06 only, every file checked).
- A Free camera option in the launcher (not retail, off by default).

### Bug fixes

**Campaign**
- Movies play: the start-up movies and the campaign movies are shown, not just heard.
- The campaign is visible: the menu backdrop no longer covers a campaign mission, and no developer text appears on screen.

**Audio**
- Stereo sound matches the original: sounds pan left and right the way RotWK's sound engine does them (before, sounds above or below the screen centre came out of one ear).

**Interface**
- The Options crash is fixed (Advanced, then Done), and the Options screen shows and saves the real settings.
- Menu fixes: button text centred as in the original, the main menu after returning from Options, sliders, drop-down lists, lobby start spots by click.
