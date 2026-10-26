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
- The mod is fingerprinted when it is loaded: every archive and every loose file, by name, size and MD5. Lobbies and replays compare that
  fingerprint, so everyone in a game has the same mod. Editing a mod file while the game runs is an error; restart instead.

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
or start OpenBFME with `--asset-pack=<folder>`.

- A file named after a retail texture replaces it: `gumaarms.dds`, `gumaarms.tga`, `gumaarms.png` or `gumaarms.jpg` all replace the texture
  `GUMaArms`, wherever the file sits in the pack and whatever its case. Higher resolutions are fine.
- A file named after a retail model file replaces it: `gumaarms_skn.w3d`. A replacement model has to keep the original's skeleton (the
  same hierarchy and bone names), or its animations will not fit.
- Several packs: they load in name order, then the `--asset-pack` ones, and the later pack wins. One pack with two files for the same
  asset is an error that names both.
- **Packs are cosmetic and allowed online.** Only drawing reads them. The game logic, which uses model bones for where arrows and
  projectiles start, keeps reading the original files, so a pack never changes a game result and never stops you joining a game.
- Not replaced yet: particle, terrain, menu (Apt) and decal textures, and model formats other than W3D (glTF is planned, with automatic
  skeleton mapping and LODs).

## The modding SDK (planned)

The SDK only ever adds files that retail never reads, so it cannot change what a retail mod means (docs/ARCHITECTURE-MODS-UBER.md on the
arch1 branch, section 3). Planned in this order:

- the rest of the asset packs: the other texture kinds, glTF models with skeleton mapping and automatic LODs;
- editable menus, images and settings;
- scripting (Luau proposed) with namespaced INI extensions, hot reload and an inspector.
