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

The settings menu uses BFME1's own `Options.apt`. Its first frame stops after
`OnInitialized`; the host then calls `assignOpen("normal")` to create the
controls and footer before opening the OpenBFME settings page. This host call
is a presentation inference from the movie's script, not a verified retail
engine port. BFME1's panel and footer paths are a separate options layout;
BFME2 and RotWK retain their existing layout.
BFME1 also uses its own detail and audio help labels. Its custom graphics
controls remain unverified under S-5017: the page says "Not available yet"
instead of offering BFME2/RotWK's nine custom graphics settings. Detail
presets and OpenBFME's own graphics settings remain available.

`godot/tests/options_open_test.gd` checks opening, every settings tab, hidden
retail controls, footer clicks, Accept/Cancel persistence and repeated returns.
It uses `BFME1_INSTALL` for BFME1 and `BFME2_INSTALL`/`ROTWK_INSTALL` for the
other two menu presentations, with an isolated temporary profile. Set
`OPTIONS_OPEN_SHOTS` to an existing folder for screenshots in a windowed run.

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

## Skirmish profile rank thresholds

The profile table `{0,5,10,30,50,150,300,500,800,1500}` comes from BFME1
1.03's engine constants. The reading aid is
[Open-BFME-1 at `f633d0e97a953289405438821be0c09b28527bbb`](https://github.com/Open-BFME/Open-BFME-1/tree/f633d0e97a953289405438821be0c09b28527bbb/game/GameEngine/Source/Common),
with the following source files relative to `game/GameEngine/Source/Common/`.
All addresses here are **RVAs**; the inspected image base is `0x00400000`.

- `Rva004D8EE0Ctor.cpp`, RVA `0x004D8EE0` (VA `0x008D8EE0`), writes those ten
  integers in order at offsets `+0x00` through `+0x24`.
- `SkirmishBattleHonorsConstructor.cpp`, RVA `0x0009E130`, passes `this+0x14`
  to that initializer through the jump thunk at RVA `0x00010807`. This ties
  the table to the skirmish honors object, beyond the address-based name.
- `SkirmishBattleHonors_rankHelpers.cpp`, `getRank` at RVA `0x0009D2F0`,
  scans the nine promotion thresholds starting at `this+0x18` (the value 5),
  advances while `points >= threshold`, and returns a rank from 1 to 10.
- `SkirmishBattleHonors_getPointsToNextRank.cpp`, RVA `0x0009D390`, subtracts
  points from the next threshold; `SkirmishBattleHonors_getRankWidth.cpp`,
  RVA `0x0009D4E0`, subtracts the current threshold from the next. Both
  return zero for rank 10.

The instructions were inspected in the donor's `bfme1.retail-1.03-unpacked`
baseline, `inputs/baselines/bfme1/retail-1.03-unpacked/files/lotrbfme.exe`,
SHA-256 `1fbdc0ced8f283732c094c4f0804ce8dc1e3e3675b720bcab066c94fed964e75`.
Its manifest describes a SafeDisc-unpacked image with crack patches reverted,
and records remaining repairs/unknown regions. None of its `reverted_patches`
or `no_ground_truth` ranges overlaps the constructors, thunk or rank helpers
cited above. This is static evidence from that identified baseline, rather
than independent certification of a pristine retail image or a new byte-exact
recovery claim.

The profile display preserves its existing boundaries: reaching a threshold
promotes immediately. Below rank 10, progress resets to zero at the start of
the new interval; rank 10 keeps zero points remaining and 100 percent progress.
These constants and this documentation apply only to the BFME1 profile display.

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
- **Automatic pickup:** BFME1 selects its own typed `AutoPickUpUpdate` data and runtime variant. Its
  `PickUpKindOf` mask admits any matching kind, retains `+`/`-` edits and clears with `NONE`.
  `Bored` and `BoredFilter` run the integrated allied-target boredom scan after eating and before ordinary
  pickup, using the same scan/button gate. Sources: BFME1 field table VA `0x010BB9D0` and Open-BFME-1
  `GameLogic/Object/Update/AutoPickUpUpdateUpdate.cpp`, RVA `0x00281F40`. The port maps the boredom power's
  name to the shared enum ordinal; BFME1's binary ordinal differs. BFME2/RotWK retain `PickUpFilter`
  (RW `0xC65B60`) and their separate `BoredUpdate`; they reject the BFME1-only fields. `PickUpKindOf2`
  is absent from BFME1's verified table and is rejected. The shared tree-point/automatic-throw limitations
  remain reported by S-7160; BFME1's broader simulation compatibility remains S-5017.
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

The Living World Good/Evil campaigns and their saves are not implemented.
`LivingWorldStores` selects the BFME1 loading mode once from the mounted data.
Its campaign blocks retain recording handlers instead of using BFME2/RotWK
War of the Ring field tables. The object-world report explicitly names this
under S-5017: campaign field validation and exact nested block boundaries are
unverified. This preserves menu/skirmish startup without claiming campaign support.
BFME2/RotWK keep their typed parsers and S-1321 errors for unported fields. Their
schemas are not widened to accept BFME1 campaign rows. BFME1 campaign
menu requests display the existing unavailable-screen message instead of trying
to start a BFME2 linear campaign. BFME1 currently uses the English language view.

## Checks

When `BFME1_INSTALL` is set, the release gate (`tools/release/test_export.sh`)
checks the packaged executable reaches `MainMenu.apt`, then runs the packaged
`bfme1_test.gd` against that package's library and resources. An invalid install,
a skipped test, a missing success marker, a script error or a nonzero exit fails
the gate. When `BFME1_INSTALL` is unset or empty, both BFME1 checks print `SKIP`
without failing publication; BFME1 packaged startup is unverified on that builder.
Both nightly and stable candidates use these conditional checks.

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

## Skirmish AI

BFME1 uses its own `AIPlayer` variant, selected once when BFME1 data is
mounted. Its `PlayerAITypes.ini` faction libraries supply the opening build
orders, economy decisions, team compositions and attack scripts. It does not
require RotWK's `SkirmishAIData` or `ArmyDefinition` tables. The change affects
BFME1 only; BFME2 and RotWK retain their current AI and library ordering.

The port preserves the castle flag's script name (`BASE_FLAG_n`), imports a
BFME1 library before its dependencies, resolves named script/group overrides
before difficulty filtering, and implements tactical-marker plot
selection. Production evaluates the library's team conditions, chooses the
highest eligible priority with the logic RNG, trains required and optional
members through real production queues, and activates finished teams for their
scripted orders. Lost or cancelled production is retried; expired builds use
the original minimum-size rule. Threat queries sum BFME1 `ThreatLevel` values
and retain the original 25-frame cache.

The BFME1 variant also recruits eligible existing units before paying for
production, observes explicit team and per-unit recruitment overrides, and
replenishes automatically reinforced teams one missing unit at a time. A
reinforcement joins its existing team; it does not create another instance.
Ready teams wait for idle members or strictly more than 60 seconds from the
original request. Building sets both native flags (`0x000EC960`); activation
uses the separate `0x000F4E20` readiness gate, including the nonempty-horde
check. The shared horde runtime's moving indexed-member flag (`Object +0x114`,
native slot `0x0023C4D0`) is still a reported S-5017 dependency. `RECRUIT_TEAM` / `RECRUIT_TEAM_AT_TEAM` use the same native
ready queue. Hero requests select the first matching revival-list record and
its REVIVE button index; an unavailable first record is not skipped for a
later record. The BFME1 producer gate scans 20 command slots, and preserves
its mixed auto-build queue rule and `TOGGLE_AUTO_BUILD` override. BFME1
team references resolve before prototype lookup and copy the referenced
team ID, so recruitment/attack scripts retain their original target.
Empty active non-singleton teams are retired after their prototype update,
freeing the original instance limit after a merge. The BFME1 player pass runs
AI before that player's generic team scripts. Spell readiness uses the native
per-condition retry cache, allowing faction scripts such as Rohan's Draft to
cast after purchasing the science.

BFME1 `HordeWorkerAIUpdate` retains its inherited `HordeAIUpdate` combat
runtime rather than using the generic unit AI. The original constructor
(`0x002C4A70`) calls the horde constructor (`0x002C43A0`) and schedules its
first wake five frames later. Its command dispatcher (`0x002C5240`) delegates
non-worker orders to the horde dispatcher (`0x002C45E0`). Rohan peasants use
this class: routing them to generic AI left their soldiers armed but without
the horde combat handoff. The same error affected the Rohan opponent in the
Mordor retail check. Worker-specific repair/task overrides are still explicitly
reported under S-5017; this fix does not claim those operations are ported.

The source is the BFME1 1.03 unpacked baseline identified above, with
Open-BFME-1 as a reading aid. All addresses below are RVAs:

- Library merge order: `GameLogic/Map/Rva0019F890FillHelper.cpp`,
  `0x0019F890`, with list transfer at `0x0035E510` / `0x0035E5A0`.
  Named version stacks: `0x0035CE60`; automatic traversal accepts only
  the current version at `0x003412E0` / `0x00343370`, checked by
  `0x00355950` / `0x003558C0`. Shadowed groups retain named subroutines
  without running their children automatically.
- Castle names: unpack at `0x00376590`; unlike RotWK `RW 0x79BE6A` /
  `RW 0x79C0FD` and BFME2 `CastleBehaviorRegisterOwnedObject.cpp`, BFME1
  retains the flag name. Plot selection: `Rva002FE590CastleBuild.cpp`,
  `0x002FE590`, and castle selectors `0x0036FE10` / `0x00373090`.
- Team selection and production: `GameLogic/AI/AIPlayer.cpp`,
  `AIPlayerSelectTeamToBuild.cpp` (`0x00163220`),
  `AIPlayerBuildSpecificAITeam.cpp` (`0x001650F0`),
  `AIPlayerQueueUnits.cpp`, `AIPlayerOnUnitProduced.cpp`,
  `AIPlayerCheckQueuedTeams.cpp` and `AIPlayerCheckReadyTeams.cpp`.
  Production-condition evaluation: `Common/RTS/TeamPrototypeScripts.cpp`,
  `0x000ED850`; build timeout: `0x00160FC0`.
- Threat: object query `0x001D2580` / scan `0x001CF710`, team query
  `0x000F72D0` / scan `0x000F67B0`. Command-point team estimate:
  `ScriptConditionsHasCommandPointsToBuildTeam.cpp`, `0x003262C0`.
- Opposition probe creation: BFME1 action 444 at `0x00304127` calls
  `0x003006B0`. Plain unit-position rows use bounding sphere radius
  (`Object + 0xBC`), X spacing `radius * index * 2.25`, Y spacing
  `radius * 2`, and sampled ground height (`0x0030094F`–`0x00300A51`).
  The constant at VA `0x010D0370` is double 2.25. The waypoint form,
  alternate origin and structure-origin pathfinder adjustment use the same
  native helper, including transport loading and delivery described below.
- Script recruitment: `ScriptActionsTeamRecruit.cpp`, `0x002F5BE0` /
  `0x000F2FB0`; queue-time recruitment is `0x000F2A00`, with its
  candidate predicate at `0x000F10F0`. Specific recruitment is
  `AISkirmishPlayerRecruitSpecificAITeam.cpp`, `0x001699C0`.
  Reinforcement selection: `AIPlayerSelectTeamToReinforce.cpp`,
  `0x00167080`; join: `AIUpdateInterface_joinTeam.cpp`, `0x0027D4F0`.
  Surviving-member position estimate: `TeamMemberQueries.cpp`, `0x000F2420`.
  Nearest-kind move: `0x002FC800`.
- Team references: `ScriptActionsReferences.cpp`, `0x002F5B00`;
  assignment `0x00345470`, copy `0x003455E0`, and lookup before the
  prototype fallback in `ScriptEngine::getTeamNamed`, `0x0034F160`.
- Player update order: `Player::update`, `0x000D9A80`; active empty-team
  cleanup: `TeamPrototype::updateState`, `0x000F71C0`; merge handler:
  `0x002FD790`. Spell readiness: condition 85, `0x0032B8D0`, with a
  50-frame retry cap, the module's earlier ready frame, and a permanent -1
  cache for an unknown power. Successful checks leave the cache unchanged.
- Factory and hero selection: `AIPlayer_findFactory.cpp`, `0x001643B0`;
  `BuildAssistant_isPossibleToMakeUnit.cpp`, `0x000FDBF0`; first-name
  revival index `0x000FA8B0`, availability `0x000F9700`. BFME1 has a
  legacy 0x60-byte revival record at Player +0x684, unlike RotWK's 0xE8-byte
  record. This ports the AI selection/gate, not the shared revival engine's
  full BFME1 arithmetic. Recruitment-aware team feasibility: `0x00164750`;
  factory/cost feasibility: `AIPlayerIsPossibleToBuildTeam.cpp`, `0x001645B0`.

The native AI save block is version 8 for BFME1. Pending work orders,
production identifiers, selection timers, threat caches, ready/reinforcement
queues, team references, per-condition power readiness and explicit
recruitment/auto-build overrides are saved and hashed.
Older BFME1 AI blocks, including version 7 which lacks the new queue and flag
state, are refused rather than silently discarding this state. BFME2 / RotWK save layouts are
unchanged. This is OpenBFME's snapshot format, not retail save compatibility.

Combined-horde recruitment now follows `0x000F2A00`: one ordered scan of
existing combinations and the first two SplitResult types, including repeated
component types, with the native nearest-candidate and tie rules. Combination
uses HordeContain's slots 0x6C/0x70 (`0x0023CAA0` / `0x00245BF0`): the same-owner,
disabled/status/JUST_BUILT gates, exact ComboHorde target, cancellation of queued
production, transfer of the existing soldiers, union of upgrades, maximum horde
rank and selection of the higher-ranked banner (ties keep the receiver's).
The result takes the other horde's transform; both source hordes retire without
spawning replacement payloads. The named-team handler `0x00303AC0` ->
`0x000F5E40` scans source prototype instances newest first and releases the two
components to the destination owner's default team before testing the merge.
It resets movement when their squared sphere-edge distance exceeds 22,500.
It does not use RotWK's plain recruitment helper (`RW 0x7A449F`).

Scripted transports follow reinforcement helper `0x003006B0`. Starts-full teams
partition passengers and carriers by descending slot sizes using the native
STLport sort and first-fit order (`0x00095FB0` / `0x00096570`). An explicit
teamTransport loads valid passengers and creates overflow carriers with the
native destination-relative placement. BFME1-only states 28/29/30 are registered
as in `0x00185A20`: move and evacuate, optionally followed by return to the spawn
origin and deletion. The moves lock against interruption; any terminal update
result evacuates and activates the team, or deletes the returning carrier
(`0x0017A280`, `0x00188520`, `0x0017A340`, `0x0017A430`, `0x0017A530`). State goals,
origin, pending path append, lock and internal movement survive OpenBFME snapshots.
BFME2/RotWK do not register these BFME1 states.

Full retail fidelity remains **S-5018**, with shared horde/transport, hero revival,
combat, movement and script gaps under **S-5017**, **S-1186** and **S-1190**.
The combination callback is now implemented: Object +0x370 is the indicator-color
index, copied from the receiver and broadcast to every behavior module by
`0x001BF2E0` / `0x001BF2A0`. The horde/transport implementations of that virtual
are native no-ops; no tint or ownership effect is invented. Object snapshot
version 3 saves this BFME1-only field; BFME2/RotWK still write version 2.
Member adoption (`0x0023FDB0`) releases old pathfinder goals, raises soldiers to
the maximum horde rank, applies the forced locomotor set and refreshes formation.
Scoring is disabled temporarily during the native transfer.

The container-system variant also implements BFME1's entry/relationship rules,
transparent wrappers and virtual capacity, payload creation at build completion,
indestructible payload inheritance, GrabWeapon's temporary passenger protection,
exit permission/layer checks, unload idling/mood reset, and exact PC24 healing.
An off-map return regression observes travel past the map edge before deletion.
The client now follows current animated passenger bones and blends released
attachments; BFME1 entry/exit fades and their zero-frame behavior are implemented.
The native scripted rampage state arms falling riders at the original eject
deadline. Its RunOffMap completion raises a named S-5017 error; selection and
script-entry guards remain unverified. SlaughterHordeContain has a BFME1-only
wrapper/member/refund path, exact-team upgrade replacement (including permanent
module removal), experience rewards and saved deferred ownership score correction.
Passenger client aiming follows the native goal/range gate and 0.1-radian step.
BFME1 AOD has its own wave/path/scatter and contact-damage runtime; mounted large
and small Oathbreaker templates are exercised. HorseHordeContain has its native
member order, speed clamp and paired reform assignment. Ordinary, horse and AOD hordes now share native polar formation, frozen member
phases, pair reform, budgeted melee destination search and ground member
acquisition. Ordinary walking paths no longer suppress the readiness callback.
A BFME1-only persistent three-axis contact manager supplies cached tests, native
bucket dispatch, and removal checks between callbacks. Native wall/deployed-tower
melee views, reciprocal recipient guards, crush-speed/facing checks, temporary
machine preservation and scalar reservations now run in the BFME1 variant.
Native mood/target search, emotion/quarrel, collision-ignore guards and plain
Bridge-end queries run. Plain retail Bridge creation/classification, the general
quick-reachability and cell-passability differences, and charge attack paths now
have BFME1 variants. General pathfinder equivalence, contested-building and
AssaultTransport mood callbacks, mesh-backed Bridge heights/damage,
contact-coordinate storage, combat and physics boundaries still limit parity.
See [the native evidence and scope](re/bfme1_containers.md).
Ordinary skirmish has no strategic walk-on army;
that assimilation action is therefore a no-op, as in the original handler.

`test_bfme1_skirmish_ai.cpp` checks real faction libraries, construction,
trained hordes and actual damage to an opponent for Gondor, Rohan, Mordor and
Isengard on Fords of Isen. It requires `BFME1_INSTALL`; without game files it
prints SKIP. Synthetic tests cover optional production, cancellation recovery,
inactive production-condition scripts, threat-cache timing, hero build-index
selection (including a busy first record), recruitment overrides, automatic
replacement, mixed busy-factory production, the 60-second ready boundary and
transactional AI save/load, independent power-condition caches and their
exact retry boundaries. Additional checks cover soldier-preserving combinations,
native merge refusals, starts-full packing, overflow transports, activation and
return/deletion, with AI-state round trips. A mounted test uses Rohan, Gondor and
Elven double combinations plus a MordorMumakil carrying a Haradrim horde.
These checks establish those outcomes, not replay parity with
the original executable.

The BFME2 standalone startup check retains its existing reported **S-1321**
`AddPlayer` parser gap in `WOTRTutorial.inc`; the AI regression test allows
that specifically named campaign error and checks ordinary skirmish startup.
Other object-data parse errors fail the regression check.

Validation for the skirmish AI and recruitment/transport changes (2026-11-08):

- The normal Core Debug target passed. The Godot extension and interactive
  client were not rebuilt or played.
- The focused regression runner passed 2,359 assertions in 126 cases without
  retail mounts; tests requiring installed game data printed SKIP. Coverage
  includes the earlier AI paths plus component/direct-combination tie rules,
  repeated split types, named-team source ordering and merge refusals,
  soldier preservation, starts-full packing, overflow carriers, delivery and
  return/deletion, original indicator-color callbacks, maximum horde/member ranks
  without duplicate scores, payload timing and protection, native entry/exit/mood
  rules, recursive slot costs, grab damage/protection, PC24 healing, BFME1-only
  state registration, and delivery/departure AI-state
  round trips (including the departure locomotor flag).
  The optional RotWK binary-table oracle was not run (`RW_GAME_DAT` unset).
- The existing SAVE-4b suite passed 1,596 assertions in 16 cases, including
  round trips for all 181 ported behavior classes and links between hordes,
  transports and tunnel networks.
- With all three installs mounted, the three retail cases passed all 316
  assertions. All four Medium BFME1 factions completed buildings, trained
  hordes and damaged the opponent on Fords of Isen (seed 9001, starting cash
  2000), within the unchanged 4,500-frame limit. Four AI snapshot round trips
  and BFME2/RotWK startup isolation passed, with the specifically named BFME2
  S-1321 campaign parse gap above reported. A separate retail case preserved
  all ten original soldiers in Rohan, Gondor and Elven double combinations
  and delivered a Haradrim horde from a scripted MordorMumakil. The combinations
  also retained the native receiver color index and synchronized all soldiers
  to rank 3 without awarding extra level-up scores.
- The earlier Rohan/Mordor zero-damage failures were reproduced before the
  horde-worker binding fix. The Rohan peasants had been routed to generic AI;
  their soldiers had weapons (and Draft modifiers) but no horde attack
  handoff. The Mordor test's Rohan opponent used that same incorrect runtime.
  Correcting the native horde inheritance and initial wake resolved both;
  the retail tests' targets, damage assertions and frame limit were unchanged.
- The temporary regression/retail runner compiled the changed simulation
  sources with optimization and the configured strict floating-point flags,
  to avoid Debug-mode simulation/checksum delays. These are outcome checks,
  not a replay comparison against the original executable.
- Compiler flags (1,476 commands), link flags (15 commands) and simulation
  source classification passed. The full libclang AST audit was not run.
- The commit gate passed all 30 changed/new files using a temporary index;
  the workspace index was left untouched. No game data was committed.
