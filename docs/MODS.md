# Mods

OpenBFME loads RotWK 2.01 mods the way the game itself does: with the `-mod` switch. A mod that runs in retail RotWK 2.01 should run here
unmodified (PLAN rule 6). This page describes what works today (lane MODS-1) and what the modding SDK will add.

## Starting a mod

Pass the mod's folder or archive when you start OpenBFME:

```
openbfme -- -mod "<mod folder>"          (retail's spelling)
openbfme -- --mod=<mod archive>.big
OPENBFME_MOD=<mod folder> openbfme
```

- A **folder** is a mod folder. Every `.big` directly inside it is mounted, and so is every loose file below it.
- A **file** is one mod archive.
- A **relative path** is looked up in the RotWK user data folder ("My The Lord of the Rings, The Rise of the Witch-king Files"), exactly
  as retail does.
- `-mod` can be given twice: once with a folder and once with an archive. If you give two of the same kind, the last one counts, as in
  retail.
- If the path does not exist or an archive in it is damaged, OpenBFME refuses to start the mod and says why. Retail would silently start
  without it.
- Keep the mod **outside the game folders**. The game folders are checked against the clean 2.01 files, and an unknown `.big` there is
  refused. A mod folder must not contain symbolic links.

## Mods installed into the game folder (Edain)

Many mods are not `-mod` folders. Their installer, or the All In One BFME Launcher, puts their archives straight into the RotWK game
folder, and the game loads them with its own files. Edain, Edain Unchained and Ennorath work this way. Retail RotWK then simply
reads the merged folder.

OpenBFME keeps your game folder clean (it is checked against the 2.01 files) and keeps such a mod in its own folder. Point it at that
folder with:

```
openbfme -- --mod-install=<mod folder>
OPENBFME_MOD_INSTALL=<mod folder> openbfme
```

The files in that folder sit where they would sit in the game folder. For example, `<mod folder>\lang\English.big` replaces the game's
`lang\English.big`. The whole set then loads in RotWK's own order:
1. `lang\English*.big`, where the last one wins;
2. the folder's `.big` files, in case-insensitive name order, where the first one wins (so `__edain_data.big` beats `_patch201ini.big`);
3. `apt\*.big`.

A `-mod` can be added on top.

## Which file wins

The order is retail's (RotWK 2.01, Common/ModMount.h). The first source that has a file wins:

1. a loose file in the mod folder (for example `MyMod\data\ini\weapon.ini`), with any upper or lower case;
2. the mod folder's `.big` files, the one whose name sorts **last** (case-insensitive) winning. That is why mods name their archives
   `!!!...` or `zzz...` to win, and `___...` to lose;
3. the mod archive given as a file;
4. the RotWK 2.01 archives, then the BFME2 1.06 archives.

New INI files in a mod (loose or in an archive) are loaded like retail's. For example, a new `data\ini\object\...\myunit.ini` adds its
objects.

## What OpenBFME checks

- The game files must still be clean 2.01. A loose file in the RotWK install folder that would override a game archive while a mod is
  loaded is refused: move it into the mod folder.
- The mod is fingerprinted when it is loaded: every archive and every loose file, by name, size and MD5. Editing a mod file while the game
  runs is an error; restart instead.
- **What has to match to play together (profile 3, lane PROFILE-3).** Lobbies, replays and saves compare what the simulation reads: every
  file under `data\ini\`, `data\scripts\`, `bases\` (the AI's base layouts) and `libraries\` (the AI's script libraries), every
  file in a map's folder except its pictures and strings (the map, `map.ini`, `scripts.lua` ...), and every model under `art\w3d\`, as
  the game resolves them with the mod loaded. A test traces every file a retail game and a mod game read and fails on any file outside
  that list. On top of that they compare the mod's sim scripts (`sdk sim`, below) and its engine
  features. The mod's name, its folder and its other files (textures, sounds, music, videos, strings, the menus' and HUD's art: Apt
  files) don't count. Command buttons and command sets are in `data\ini\` and do count.
  - Two players with differently named copies of the same mod play together.
  - A player with a mod that changes only textures, sounds, music, videos, strings or the menus' and HUD's art plays with a player without it.
  - A mod that changes a model (`art\w3d\*.w3d`) has to match, even if it only changes how the model looks: the simulation reads models
    (a projectile's launch bone, a wall's footprint). Such a player plays only with players who have the same model files. Asset packs
    (below) are the way to change how a model looks without this.
  When the game data differ, the refusal names both players' mods and the archives that differ. The mod's name and fingerprint still travel in the LAN
  START, the replay and the checker's report.

## One-click mods in the launcher (lane MODS-3)

The launcher's **Mods** screen installs Rise of the Witch-king mods from the All In One BFME Launcher's workshop (bfmeladder.com, the
service the game files come from: docs/AIO.md), lays each one out the way `tools/mod/aio_package.py` does, and starts the game with it.

- **The list.** Nothing is asked of bfmeladder.com until you press *Load the list*. The launcher then reads the workshop's patches, mods
  and enhancements for RotWK (types 0, 1, 2) and the file list of every package and of what it depends on, and keeps the result for half
  an hour (`mods/catalog.json`; *Refresh* reads it again). Enhancements are listed under the package they extend; one that extends the
  original game is listed on its own.
- **Ready or not.** A package is **ready** when none of its layers carries an engine edit. It **needs engine features** when one does
  (`game.dat`, `game2.dat`, `file.dat`, `lotrbfme2ep1.exe`, a `.dll` or an `.asi`). The screen names each file and the layer it comes
  from, for example "Patch 2.02: lotrbfme2ep1.exe". OpenBFME never runs those files. When OpenBFME knows the file (by MD5, see *Mods that
  change the game program* below) Play passes `--mod-engine=<md5>` and the game turns on the features that stand in for it; an unknown
  one stops the start with a message naming the file.
- **Install.** The first download asks first: the files come from bfmeladder.com, are made by their authors, and have the size shown.
  The answer is kept. The layers go into `<launcher data>/mods/<guid>/install` (the game-folder files, passed with `--mod-install=`) and
  `<launcher data>/mods/<guid>/mod` (a type 1 mod's own folder, passed with `--mod=`). Each file is downloaded to `.part`, resumed with
  Range (*Pause* / *Resume*), checked against the MD5 the service lists, then renamed. Files the layout no longer has (an older version,
  an enhancement you unticked) are removed afterwards. `openbfme-aio.json` records the layout: the tool's schema `openbfme.aio-package 1`
  plus `enhancements`. Nothing is written outside that mod's folder, and the game folders are never touched.
- **Left out.** A tool's `.exe` / `.msi`, the files the AIO launcher never installs, maps (type 3, not supported yet), and the files of a
  layer for another game (a BFME2 patch belongs in the BFME2 folder, which is not mounted from a mod). The record lists each one.
- **Play with.** You pick one mod, or *The original game*, and the enhancements it was installed with (`mods/active.cfg`). Play then
  passes its folders to the game. A picked mod that is not completely installed, or whose ticked enhancements are not the installed
  ones, stops the start with a message. It never starts without the mod. BFME1 mode passes no mod.
- **Remove** deletes `mods/<guid>/` and nothing else.
- Command line: `--mods-list[=refresh]`, `--mod-get=<guid> --mod-consent [--mod-with=<guid>,...]`, `--mod-use=<guid>|none`,
  `--mod-remove=<guid>`, `--mod-args` (launcher/scripts/main.gd). Tests: `tools/release/test_mods.py`, on a made-up catalogue
  (`launcher/tests/fixtures/mods/catalogue.json`) served by a local stand-in.
- Unlike the base games, mods have no pinned table: the service's MD5 is the check. A file the service lists with Size 0 (it does so
  for some real archives) takes its size from the server's length headers.

## Mods that change the game program (lane MODS-3)

Some mods ship their own `game.dat` (the program `lotrbfme2ep1.exe` starts). OpenBFME never runs it. Each modified program the All In
One workshop carries was compared with the retail one (logs/mods-engine-edits-catalogue.md), and every change that matters to the game is
rebuilt in OpenBFME as a named feature, off unless that mod is loaded (`Common/ModEngineFeatures.h`):

| program (MD5) | what OpenBFME does |
|---|---|
| Edain Unchained 4.4.3 `game.dat` (5c31f503...) | the `edain-unchained.*` features below |
| Patch 2.02 / 2.02 Delayfix `lotrbfme2ep1.exe` | nothing to do: install checks and EA's frame pacing, no game rule |
| Edain Völker 1.7 `game.dat` | nothing to do for the game rules: frame pacing and desync reporting |
| RJ-RotWK launcher and its `file.dat` | nothing: a launcher, not the game |

The Edain Unchained features: `herobar-kindof` (KindOf HEROBAR / HEROBAR_GROUP), `herobar` (those objects in the hero bar, one
cycling slot per HEROBAR_GROUP type), `commandset-limit` (command sets of 64 slots), `commandset-button-upgrade` (CommandSetUpgrade
`CommandButtons = Name[:slot] ...`), `object-image-upgrade` (the ObjectImageUpgrade module: a new portrait / button image),
`spell-store-upgrade` (PlayerTemplate `PurchaseScienceCommandSetUpgrade = <upgrade> <command set>`), `queue-ignore-cp` (CommandButton
`QueueIgnoreCP`), `fire-at-attacker`, `large-group-bonus` (TriggeredBy / ConflictsWith / RequiresAll* / CountLooseObjects),
`science-prereqs` (a PrerequisiteSciences may name a later science), `spawn-union` (every SpawnBehavior of an object serves),
`share-experience-all`, `trigger-recharge-list`, `unique-production-id` (one production id counter for every building),
`hero-recruit-parallel` (a hero not ready does not hold the queue), `horde-exit-absorption`, `foundation-rebind` (a ReplaceSelfUpgrade
keeps its plot), `multi-execute-gate`, `description-timers` (cooldown / build / research time in the button help),
`upgrade-description` and `observer-command-range`.

Not rebuilt, because no Edain data or map uses them yet: the PRODUCTION_MONEY / _UNIT / _UPGRADE / _CONSTRUCTION modifiers and desert
weather (SAND, DESERT). The hero bar's 32 slots are inert (Edain keeps the retail 16-slot movie). Its render and crash-dump fixes are
Windows/DirectX fixes the port does not need.

The active features are part of the game's identity: two players with different engine features do not join each other, and a replay
names them.

## Not yet

- **Ordering several mods** at once, and a mod started directly from the game (without the launcher) does not get the launcher's pick.
- **Age of the Ring** and other programs OpenBFME does not know yet stop with a message naming the file.
- The mod corpus test (`OPENBFME_MOD_CORPUS`) mounts every package laid out by `tools/mod/aio_package.py`, turns on the features of its
  programs and loads its objects through the retail parsers. Most remaining messages are the mods' own data errors, which retail also
  meets (logs/mods1-report.md lists them per mod).

## Asset packs: drag-and-drop HD models and textures

An asset pack is a folder of replacement art. Drop it into the `asset-packs` folder of OpenBFME's user data folder (one subfolder per pack),
or start OpenBFME with `--asset-pack=<folder>`. The launcher's **Mods** screen lists the packs there under *Asset packs*. Each pack has an
**On** box (a pack turned off is named in `asset-packs/disabled.txt`, which the game reads too) and the screen has an *Open the folder*
button. `--asset-packs`, `--asset-pack-on=<name>` and `--asset-pack-off=<name>` do the same from the command line.

- A file named after a game texture replaces it: `gumaarms.dds`, `gumaarms.tga`, `gumaarms.png` or `gumaarms.jpg` all replace the texture
  `GUMaArms`, wherever the file sits in the pack and whatever its case. Higher resolutions are fine. Packs replace the textures of models,
  particles, building bibs and shadow decals, roads, rivers, water and the sky.
- A file named after a game model file replaces it: `gumaarms_skn.w3d`. A replacement model has to keep the original's skeleton: the same
  bones in the same order with the same parents, the same models (HLODs) on the same skeleton, and the same animations. When the game
  starts it checks each pack model against the file it replaces. A model that does not fit is left out, and the start log names it and
  says why ("RELEASE asset packs: ... does not fit ..."), so the game's own model is drawn and the rest of the pack still loads. A pack
  file that is not a W3D file at all stops the start.
- **Which file wins.** The base game, then `--mod-install`, then `-mod`, then the asset packs. A pack draws over a mod's file of the same
  name too, and is checked against the mod's model. Packs load in folder-name order, then the `--asset-pack` ones, and the later pack
  wins. One pack holding two files for the same asset is an error that names both.
- **Online is permissive (lane SDK-1c, the owner's choice, 2026-10-27).** Every game uses your packs, LAN and online too, and other
  players may use different packs or none. Packs only change what is drawn: the simulation never reads them, so players with different
  packs stay in lockstep. There is no allowlist and no review: the only files left out are the ones that would break the game itself, a
  model that does not fit the skeleton (above) and a file that cannot be read, and the start log and the launcher's card name them
  ("26 models not used ...", with the reasons behind Details; written to `asset-packs/report.json` at every start). The honest limit:
  a pack can change what one player sees and another does not. Since profile 3 the game's record carries one line per pack in use
  (`pack <name> <files> <sha256>`, in the LAN START and the replay; the replay checker reports packs that differ). These lines never
  refuse a game. They keep honest players honest; they don't catch a renderer cheat. A modified client can draw whatever it likes, and the replay checker sees what a client plays (the game
  state), not what it draws. A pack may hold only models, textures and its own text (`.txt`, `.md`, `README`, `LICENSE`); any other file
  refuses the whole pack. Only drawing reads the packs, and the game logic keeps reading the original files.
- **From a mod's HD archive.** `python3 tools/mod/asset_pack_from_big.py <archive.big> <new folder>` copies the models and textures of
  an archive into a pack and lists what it leaves out. For example, the All In One launcher's *RotWK 2.02 HD Edition* gives 503 models
  and 466 textures. Over the original 2.01 game, 477 of its models fit. The other 26 are its redesigned citadel, statues and barracks
  damage states, which need 2.02's own data, so they are left out.
- Not replaced yet: terrain tile textures (they are cut into the map's texture atlas at the original size), UI (Apt) textures, and model
  formats other than W3D (glTF is planned, with automatic skeleton mapping and LODs).

## Mod scripts in Luau (lanes SDK-2, SDK-2b)

A `-mod` folder can carry Luau scripts in files retail never reads. Retail's own Lua scripting (`Scripts.lua`, `ScriptEvents.xml`) works
as before. The scripts are loose files of the folder, never inside its archives:

```
<mod>/openbfme/sdk/mod.json                   {"sdk": 1, "name": "Income", "sim": ["income"], "presentation": ["banner"]}
<mod>/openbfme/sdk/sim/income.luau            sim scripts and the modules they require
<mod>/openbfme/sdk/presentation/banner.luau   presentation scripts and the modules they require
```

- **Roles.**
  - **Sim** scripts change game results, so they run in lockstep. Their maths uses the deterministic libm, `math.random` is the game's
    shared generator, and there is no clock. Tables can't be keyed by tables or functions (the order would differ between machines),
    there are no weak tables, and no address is ever shown.
  - **Presentation** scripts only draw, play sounds and show UI, so they may differ between players.
  - A script can only `require` modules of its own role.
- **Events of a sim script:** each runs at the end of a logic frame, in this order:
  - `onGameLoaded()` once in a game loaded from a save (see below);
  - `onGameStart()` once;
  - `onObjectCreated(id, template, player)` and `onObjectDestroyed(id, template, player)` for the objects of that frame;
  - `onFrame(frame)` every frame.

  The `game` table offers `frame()`, `playerCount()`, `playerName(i)`, `playerSide(i)`, `money(i)` and `addMoney(i, amount)`. The
  logic runs 5 frames a second.
- **Presentation scripts** get the same events after the sim scripts, a read-only `game` (no `addMoney`; plus `game.localPlayer()`, the
  player this machine plays) and `ui.banner(text)`, a message centred near the top of the screen for a few seconds. They use their own `math.random`, never
  change the game, and an error stops only them.
- **Sample:** `tools/sdk/samples/income-banner` is a complete mod folder: every player gets 250 resources every 10 seconds, a
  banner announces each payday, and the count of paydays is kept in `sdk.state` across saves (`openbfme -- -mod "<path>/tools/sdk/samples/income-banner"`).
- **Sandbox.** There is no `os`, `io`, `debug`, `getfenv` / `setfenv`, `loadstring` or `collectgarbage`, and the globals are
  read-only. `sdk.read('path')` reads a file of the mod folder and nothing outside it. There is no network.
- **Limits.** A call into a script runs at most a fixed number of VM steps (counted by the VM, so the same on every machine), and a
  script's memory is capped. A script that goes over a limit or raises an error stops that mod's scripts with a message naming the
  mod, at the same frame for every player. A broken manifest, or a script that does not load, stops the game from starting.
- **Saves (lane SDK-2b).** A game with sim scripts saves. **`sdk.state` is the only thing of a script that survives a save.** It is a
  table the sim scripts fill in place (the table itself can't be replaced). It holds booleans, numbers (not NaN), strings, and tables of
  them keyed by strings or whole numbers. Each table appears once, with no metatable, at most 16 tables deep and 64 KB in all. A loaded
  game starts the scripts afresh: their top level runs again, `sdk.state` comes back as it was saved, and `onGameLoaded()` runs before
  anything else, so a script can rebuild its other variables from `sdk.state` there. Its local and global variables start as their
  top level sets them.
  - Because a top level runs again on every load, it may not change the game. `game.addMoney`, `math.random` and a write to
    `sdk.state` there are an error that stops the game from starting, the same on every machine. Change the game from an event,
    including `onGameLoaded`; a module required from an event keeps the same rule for its own top level.
  - `sdk.state` joins the game's state hash at the end of every frame, so players whose scripts kept different state desync. A value it
    can't hold (a function, a table twice ...) stops the mod's scripts at that frame with the path named, for every player.
  - A save made with other sim scripts is refused on load, quoting both `sdk sim` lines.
- **Online, replays and saves.** The sim scripts are one line of the mod's identity, `sdk sim <sha256>`: every file below
  `openbfme/sdk/sim/` with its own SHA-256, then the manifest's sim entries and the SDK version. Players whose sim scripts differ do not start a game together, and a replay or a save made with
  other sim scripts is refused; both messages quote that line. Presentation scripts are not part of the identity at all.

## The modding SDK (planned)

The SDK only ever adds files that retail never reads, so it cannot change what a retail mod means (docs/ARCHITECTURE-MODS-UBER.md on the
arch1 branch, section 3). Planned in this order:

- the rest of the asset packs: terrain tile and UI textures, glTF models with skeleton mapping and automatic LODs;
- editable menus, images and settings;
- scripting (Luau proposed) with namespaced INI extensions, hot reload and an inspector.
