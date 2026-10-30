# BFME1 support

BFME1 is an experimental, separate game mode. It uses a BFME1 installation for
both menus and gameplay, without mixing in BFME2 or RotWK data.

The main menu loads BFME1's mapped textures from `art/textures`, including
JPEG colour with a companion PNG alpha mask. The backdrop and native
`RenderImage` logo use the same texture loader as the menu gadgets.
`Video FrontEnd` in BFME1's `Video.ini` identifies `big_movie_with_alpha.vp6`
as the movie for front-end menus. It loops over `ShellMapLowLOD`, preserves
its alpha, and stops when the shell backdrop is hidden for gameplay.
The movie fills the aspect-fitted backdrop beneath the APT menu and logo;
this placement and immediate start/stop are presentation inferences, not a
verified port of retail's display transitions.

Submenus retain their authored 3D front-end background. When the BFME1 main
menu becomes active again, it hides that background through its existing
APT instant-hide function so it no longer covers the continuing movie. This
main-menu activation rule is also a presentation inference; BFME2 and RotWK
keep their existing background behaviour.

`godot/tests/bfme1_menu_backdrop_test.gd` requires `BFME1_INSTALL` and a GPU.
It compares independently decoded JPEG/PNG and VP6 assets against the menu
at four resolutions, checks repeated Skirmish/Options returns, looping and
gameplay/menu transitions, and writes screenshots under `BFME1_MENU_TEST_DIR`
when that variable is set.

From the checkout root on **Linux or macOS (POSIX shell)**, with a matching
Godot 4.7 editor binary on `PATH`, the GDExtension already built into
`godot/bin/`, and BFME1 data in `games/BFME1`, launch:

```sh
BFME1_INSTALL="$PWD/games/BFME1" godot --path godot -- --game=bfme1
```

Alternatively pass `--bfme1-install="$PWD/games/BFME1"` alongside `--game=bfme1`.
Replace the folder relative to the checkout with your installation; `$PWD`
resolves it before Godot changes its working directory. On **Windows cmd.exe**, use
`build.bat` with MSVC/Ninja first, then run the Godot 4.7 console executable
with `--path godot -- --game=bfme1 --bfme1-install="%CD%/games/BFME1"`; the POSIX
environment-variable prefix above is not Windows command syntax.
The installation must contain its BIG archives, including `apt` and `lang`.
Installed patch archives are included in BFME1 archive order; launcher-generated
`!!!` graphics overrides are excluded and named in the mount report (`excluded_archives`). This mode fingerprints the installed
archives for content identity, but does not certify them against a retail 1.03
manifest. The existing RotWK verification policy is unchanged.

The launcher offers **Battle for Middle-earth I** in the Play screen's game selector
and passes the asset folder when starting BFME1 (`BFME1_INSTALL`, a remembered
`BFME1_INSTALL=` in the game's `install-paths.cfg`, `downloaded-installs.cfg`
or `downloaded-bfme1.cfg`, the default games folder's `BFME1`, or an existing
macOS AIO installation). The command-line equivalent is `--game=bfme1 --play` on
the launcher; the selection persists. With **Battle for Middle-earth I** chosen,
**Game files** offers an OPTIONAL download of BFME1 1.03 (the `original-BFME1`
package, from BFME Arena / bfmeladder.com, a third party: OpenBFME does not host
it); it goes into `<folder>/BFME1` and is found from then on
([AIO.md](AIO.md#bfme1-optional-community-pr-36-restored-2026-10-18), "BFME1 (optional)").

BFME1 has separate `bfme1-Options.ini` and `bfme1-Skirmish.ini` settings.
`--game=bfme1` and `--bfme1-install=...` are normal player options, so profiles persist between launches.
Automation flags such as `--auto` still use an isolated temporary profile file.
Its archive fingerprint cache is `bfme1-archive-cache.cfg` in the user data folder.

## Source and implementation

The BFME1 donor was inspected at `77b050212e`. Keep a local reference checkout
under `workspace/reference/open-bfme-1`; paths below are relative to that
donor, not this checkout. Relevant sources:

- `game/GameEngineDevice/Source/Win32Device/Common/Win32BIGFileSystem_loadBigFiles.cpp`:
  descending archive order, first archive wins.
- `game/GameEngine/Source/GameClient/GameText.cpp`, `parseCSF` at `0x00437570`:
  complemented UTF-16 strings, first string per label, optional voice records.
- `game/GameEngine/Source/GameClient/GUI/AptMainMenuConstructor.cpp` and
  `BfmeAptScreenMainMenuProvider.cpp`: BFME1 menu commands and providers.
- `game/GameEngine/Source/Common/Thing/ThingTemplate.cpp` and
  `GameClient/Drawable/Behavior/RandomSoundSelectorFieldParse.cpp`:
  legacy object and voice fields.
- `game/GameEngine/Source/Common/INI/INIAudioSettings.cpp` and
  `GameClient/Eva.cpp`: BFME1 audio settings and predefined EVA names.

The shared engine reads BFME1's subsystem legend and omits expansion-only
Stances/Create-a-Hero initialization. Language files under `lang/english` are
exposed at the root, as expected by existing menu readers. Both the shell and
in-game text consumers understand CSF.

## Isolation

BFME1 support never changes RotWK / BFME2 behaviour (community PR #36 review). `Common/BfmeGame.h`:

- **Which game:** `isBfme1Data(fs)` (BFME1's `SubsystemLegend.ini` without
  `SubsystemLegendExpansion1.ini` or BFME2's `AwardSystem.ini`), cached per INI
  environment (`INIEnvironment::bfme1Data`).
- **INI rows:** BFME1's extra rows are registered per table (`RegisterBfme1FieldRows`) and found only in BFME1 data; RotWK's tables stay
  the binary's, so RotWK data still fails on these fields as retail does. Value-level differences (AudioEvent `Control` names, special power
  `Enum` names, particle priorities, OCL dispositions, command points) ask `INI::bfme1Data()`.
- **Simulation:** differences ask the template (`ThingTemplate::bfme1Data()`, set by a ThingFactory built for BFME1 data): the vision default,
  build plots under their occupant, the castle flags. A RotWK game's state hash and saves are unchanged.
- **Files read through a file system ask that file system:** map chunks (`MapReadOptions::bfme1`, set by every map / base / library reader
  from its file system), the map cache, and the W3D model and texture layout (`W3DFileSource::FlatAssetLayout`, `Resolve_W3D_Texture`'s
  `flatLayout`), which the simulation's asset manager (launch bones) also reads.
- **Client paths with nothing to ask** (the texture decoder's PNG / JPEG sniffing, the Apt VM's type-8 constants, floor picking) ask
  `bfme1Mounted()`, set by the BFME1 mount once its archives pass `isBfme1Data` and cleared otherwise. It never decides a game rule.
- What BFME1 mode infers rather than ports is stop **S-5017** ([STOPS.md](STOPS.md)), in the BFME1 mount's report.

## How the BFME1 data differs

These are the places where BFME1's data or menus differ from RotWK's. Each one is handled when the BFME1 archives are mounted
(`isBfme1Data`, `Common/BfmeGame.h`: `SubsystemLegend.ini` without
`SubsystemLegendExpansion1.ini` or `AwardSystem.ini`):

- **Models and textures** are flat: `art\w3d\<name>.w3d` and `art\textures\<name>.dds`, with no two-letter folders
  (`W3D_Set_Flat_Asset_Layout`, set by the mount).
- **Command points:** GameData has `GoodCommandPointsMP56` / `MP78` (5–6 and 7–8 players), not one row per count
  (Open-BFME-1 `Player_newMap.cpp`).
- **AIData has no `DeepWaterDepth`, and GameData has no `MaxCells*` pathfinder limits.** BFME1 compiles its limits in:
  5000 cells per frame, 400 adjust tries, 2000 patch path cells, 2500 attack path cells, and 30000 cell infos
  (Open-BFME-1 `AIPathfind.cpp`). INFERENCE: the horde-melee, melee-engagement, possible-destination and towards-goal
  budgets have no located constant, so they keep RotWK 2.01's values.
- **Script libraries** use `MPPositionInfo` version 0: the two flags and the team, with no LoadAIScript flag and no
  side restrictions.
- **Victory:** GameData has no `VictoryCondition*ObjectFilter` rows. INFERENCE: a base is a `STRUCTURE` that is not
  `IGNORE_FOR_VICTORY`, and an army is any unit that is not a `DOZER` (the compiled rule isn't in Open-BFME-1).
- **Vision defaults:** an omitted `ShroudClearingRange` defaults to `VisionRange`, as in Open-BFME-1 `Object.cpp`.
  Gondor's keep relies on this; treating it as zero hid the keep and castle floor behind shroud.
- **Last map:** selecting a skirmish map immediately saves the current profile's lobby. The preferred map is restored
  even when the remaining saved lobby settings cannot be reused.
- **Start castles:** a match starts with each player's castle standing. INFERENCE: a castle flag that uses
  `UseTheNewCastleSystemInsteadOfTheClunkyBuildList` (no RotWK castle has that row), and whose owner's faction has a
  `CastleToUnpackForFaction` entry, unpacks that castle at once and for free on its first update. Neutral flags keep
  the normal unpack price. The retail trigger (the caller of `CastleBehavior::initiateUnpack`) isn't in Open-BFME-1.
- **Castle layouts:** BFME1's new castle system places its authored layout without per-piece overlap checks;
  adjacent wall and gate geometry deliberately overlaps. Applying the RotWK placement check drops those pieces.
- **Occupied plots:** starting construction hides and deselects a regular build plot, closing its commands. The plot returns when its occupant is removed.
- **Ground texture alpha:** JPEG ground textures take priority over paired PNG alpha masks and load their alpha through the same packed-texture merge used by the UI.
- **Build-plot picking:** plot surfaces use `W3DFloorDraw`, so mouse picking tests their visible floor triangles
  as well as scripted model draws. Hidden floors remain unpickable.
- **Skirmish maps:** the lobby lists the mapcache's multiplayer maps, as for RotWK (Sol review r1: no file-name filter; in BFME1 1.03 every
  multiplayer entry is a `map mp` map with two or more players).
- **Skirmish presentation:** gadget atlases use BFME1's flat `art/textures` packed textures, including TGA data stored under `.dds` names. The map list uses the retail two-column
  badge/count-bearing-name layout and BFME1 difficulty badges; the profile and map-type dropdowns bind their actual choices. Profile text,
  faction presentation and rank read the selected profile's `SkirmishStats.ini`; unavailable historical values are not invented.
  New personas start with fresh statistics even when reusing a deleted name; map badges refresh with profile changes and recorded results.
  Faction presentation uses the loaded `PlayerTemplate` index and `Side` (`APT:<Side>Caps`), not a retail-faction table. Rank artwork follows
  BFME1 `Rva0046F910RankDisplay.cpp`: use the mounted `AptRankIcon<Side><Rank>` image when present, otherwise the native generic `AptRankIcon<Rank>`.
- **Texture replacements:** PNG/JPEG files are accepted by BFME1 texture lookup (including `GBCampFloor`), and image signatures are recognized even when replacements retain `.dds` filenames.
- **Palantir commands:** BFME1 uses six fixed `CommandButtons.Command1` … `Command6` slots and `SetCommandButtonState`, rather than BFME2's dynamic content frames. Slot state updates only run when content changes, so show animations can finish.
- **Command tooltips:** BFME1's legacy cursor blocks are skipped to their `End` when loading help fonts. A native help panel displays the existing command-help text because BFME1 has no BFME2 `OnHelpBoxLoaded` content-frame callback.
- **Pause menu:** BFME1 uses type-8 `PushData` constants to reference the active constant pool. Resolving these initializes each button's own text and callback, including the Restart/Exit confirmations.
- **Keys and voices:** only `Data\INI\CommandMap.ini` exists (there's no root `CommandMap.ini`), and there's no
  `CrowdResponse.ini`, because BFME1 has no crowd responses.
- **Siege initiation sounds:** the update-based siege deploy powers retain BFME1's `InitiateSound2` alongside
  `InitiateSound` in their own base parser. BFME2/RotWK still reject the extra row. This fixes siege-ladder object
  loading; initiation-sound playback remains unimplemented under S-4942.
- **Legacy behavior fields:** `DynamicPortalBehaviour` reads `AllowKindOf` and `RejectKindOf` only for BFME1 data.
  A unit must match any allowed kind and no rejected kind; an explicitly empty allow mask admits nothing.
  These become the shared waypoint filter with an ANY rule, not ordinary Object fields. Sources: Open-BFME-1
  `DynamicPortalBehaviourRva001F96E0.cpp` and `Map/Waypoint_method001ABBB0.cpp`.
  The default allows all defined kinds and rejects none. Rejecting `CAVALRY` also rejects units with the
  `MOUNTED` model condition, even without the cavalry kind. This does not establish full BFME1 waypoint parity.
  `BloodthirstyUpdate` also reads BFME1's `NumToSacrifice` (default 1) and `InitiateVoice2` without widening
  BFME2/RotWK's field tables. The second initiation voice plays alongside the first, independently of crowd responses
  (`MessageStream/CommandXlatVoice.cpp`, case `0x7E9` and its two-slot playback tail). Voice voting groups the ordered
  pair of initiation sounds case-insensitively, including choices with only a second sound.
  The recovered BFME1 `Update/Rva00287290Update.cpp` awards experience on every accepted sacrifice, as the shared
  runtime does; it does not gate experience on `NumToSacrifice`. That field is retained, but any further purpose is
  unverified, so no quota-based veterancy rule is invented.
- **Main menu:** `MainMenu.apt` shows its menu bar only when `ShowMainMenu()` is called. INFERENCE: BFME1's
  `OnInitialized` makes that call (Open-BFME-1 declares `_bfme_onInitialized` without its body). Its button instance
  names don't match the buttons: the clip named `Skirmish` is the Campaign button, and `Tutorial` is Skirmish. Scripts
  find buttons by their `buttonName` label (`$SoloPlay`, `$Skirmish`), as a player does.
- **Skirmish lobby:** `Skirmish.apt` opens with `OpenDefault`, its persona popup opens with `PopUpPersona` and closes
  with `PopUpControl("close")`, and it sends `Skirmish::PersonaAccept` / `PersonaCancel` / `PersonaRemove` /
  `PersonaOk`. The Start button is `bottomNav.StartGame`.

## Limits

This is not yet a complete or retail-compatible BFME1 implementation. The shared
simulation still has unported modules, and BFME1-specific castle, horde, audio,
and combat behavior needs further comparison with the original game. Legacy
fields that lack runtime consumers are retained rather than discarded.
BFME1 particle priorities currently map to the shared renderer's quality tiers;
its original particle budget is not reproduced. One shipped unresolved mounted
Faramir voice reference (`UnitSpecificSounds` `VoiceGarrison = FaramirKnightVoiceGarrison`, which no AudioEvent defines) loads as BFME1 loads
it: BFME1's `UnitSpecificSounds` lines store the event name without looking it up (Open-BFME-1 `ThingTemplate_parseArbitrarySoundsIntoMap.cpp`).

The Living World Good/Evil campaigns and their saves are not implemented. Their
menu requests display the existing unavailable-screen message instead of trying
to start a BFME2 linear campaign. BFME1 currently uses the English language view.

## Checks

Synthetic CSF tests are included in `openbfme_tests --test-case="GameText:*"`.
Legacy portal filtering, both bloodthirsty voice slots, per-sacrifice experience, and BFME2/RotWK rejection are
covered by `openbfme_tests --test-case="bfme1 isolation:*"`. A core-only installed-data check can run without Godot:

```sh
BFME1_INSTALL="$PWD/games/BFME1" engine/build/openbfme_tests \
  --test-case="bfme1 isolation retail: installed BFME1 object world*"
```

It mounts BIG archives in BFME1 order (excluding launcher-generated `!!!` overrides), checks every subsystem parse
error, and verifies the castle-upgrade parent and child templates load. Without `BFME1_INSTALL` it prints SKIP;
that is not a passed installed-data check. It does not launch gameplay or write player settings.

Run `engine/build/openbfme_tests --test-case="GameText:*"` from the checkout
root on Linux after building the core tests (Windows: `engine\build\openbfme_tests.exe`
with the same argument). No retail data is needed for these synthetic tests.
The following **Linux/macOS POSIX-shell** checks use the editor binary, not an
export template, and require the built extension and BFME1 data described above.
The installed-data smoke test is:

```sh
BFME1_INSTALL="$PWD/games/BFME1" godot --headless --path godot \
  --script res://tests/bfme1_test.gd
```

It checks object loading, the real main menu and Skirmish shell, and a
Gondor-versus-Mordor Fords of Isen game. It prints `BFME1 SKIP` and exits 0
when `BFME1_INSTALL` is unset; that exit is not a passed installed-data test.

The whole launch, scripted with real mouse events (main menu → Solo Play → Skirmish → persona → Start → 10 s of play):

```sh
BFME1_INSTALL="$PWD/games/BFME1" godot --headless --path godot -- --game=bfme1 --auto --check --advance=10 \
  "--map=maps/map mp fords of isen/map mp fords of isen.map" --faction=FactionGondor
```

`godot/tests/bfme1_quit_test.gd` checks the camp-floor material and all six pause-menu buttons with real movie clicks, including confirmations.
