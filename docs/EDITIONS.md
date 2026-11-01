# Game editions: BFME2 1.06 and RotWK 2.01

The engine is growing two editions with their own rules (owner decision,
2026-10-29; rules for contributors in `AGENTS.md`):

- **BFME2 1.06** is the base. Its rules come from the clean BFME2 1.06
  `game.dat` (md5 `34391b253b0ee12d6cec1178f46d3bf9`); the Open-BFME-2
  decompilation is the reading aid.
- **RotWK 2.01** is BFME2 plus its differences (`RW 0x...` cites).

Today every game runs RotWK's rules. BFME1 keeps its own mode (RotWK's rules
on BFME1 data, stop S-5017).

## Phase 0 (lane BFME2MODE-0)

Phase 0 measures the work and proves the first slice. It changes no version,
wire format or save format, and RotWK games are unchanged.

### The developer switch

`OPENBFME_EDITION=bfme2` (the game) or `openbfme_peer --edition=bfme2` mounts
BFME2 1.06's archives **alone** (no RotWK archive), for gameplay. There is no
player-visible option yet. The data decides the edition: `isBfme2Data`
(`engine/src/Common/GameEdition.h`) recognises BFME2 1.06's archives by their
legend (`SubsystemLegend.ini` and `AwardSystem.ini` without RotWK's
`SubsystemLegendExpansion1.ini`). The INI load then reads BFME2's legend; the
rules are still RotWK's. Any other value of the switch is an error.

Because the data identity already covers every mounted archive, a BFME2 game
and a RotWK game can never join, replay or load each other's saves by mistake.
Phase 1 adds the named edition line (identity, START, replay, saves).

### What Phase 0 proves

The tests are `engine/tests/test_bfme2mode0.cpp` (BFME2_INSTALL) and
`tools/re/test_edition_census.py`.

1. **BFME2 1.06 alone loads.** Every INI, every template and every BFME2
   map's object list load with one named error. The named list in the test
   (`namedIniErrors`) holds it; its history is below.
2. **A BFME2-map AI-vs-AI skirmish is deterministic.** Two Medium computers
   play on BFME2's own Evendim for 3000 frames. They play twice, and the two
   runs agree on every frame hash. The same digest comes out on two CI hosts.
3. **The census below** measures the work.
4. **One constant branch is checked against the BFME2 1.06 `game.dat`**
   through `tools/retail_oracle` (section "Oracle" below).

### The named load errors (BFME2 1.06 alone)

BFME2 1.06 alone loads 4,238 templates with **no INI error**. Its 120 maps
place 123,461 objects, and every one of them resolves (no unresolved template,
no `map.ini` or read error). Since merge queue q34 the named list in the test
has one line: the War of the Ring branch's Living World parser reads
`Data\INI\Campaigns\RiskCampaign.ini`, and its `AddPlayer` row is one of
S-1321's unported Living World rows (RW 0x6E309A's table), so the file stops
there as retail stops a file on a row it cannot parse.

The first run with RotWK's field tables had two errors, both from one cause.
`BezierProjectileBehavior`'s row `PreLandingEmotionAffectsAllies` is in BFME2
1.06's table (BFME2 `0xC41B58`, parseBool, +0xB4) but not in RotWK's
(RW `0xC58368`). So `evilfactionsubobjects.ini` stopped at line 1590, as
retail stops a file on an unknown field, and its 53 later templates were not
defined: `MumakilHowda`, the Mordor catapult rocks, `RainOfFireProjectile`, ...
`CreateAHeroSystem.ini`'s `MUMAKILS_KILLED` stat then named a missing template.

The fix is a BFME2 row, registered per table like BFME1's rows. It is found
only in BFME2 1.06 data (`INIEnvironment::bfme2Data`); RotWK data still fails
on it as retail does.

Two names the scope review expected were never errors. `FadeAndDieOrnamentUpdate`
is a RotWK module too (RW `0x7A76CF`). `IFarmTemplate` is in no BFME2 1.06 INI.

### The edition variants so far

| System | Variant | RotWK 2.01 | BFME2 1.06 | Checked by |
|---|---|---|---|---|
| skirmish AI | FarmKillSquad team priority (`SkirmishAIEditionRules.h`) | 500 (RW `0x9BB49D`) | 30 (`0x9AD00B`) | `tools/retail_oracle/test_edition_oracle.py` (both game.dat files) |
| projectiles | pre-landing scare relationship mask (`BezierProjectileBehaviorModuleData::preLandingRelationshipMask`) | 7, every object (RW `0x85F31A`) | 3 (enemies, neutral), or 7 with `PreLandingEmotionAffectsAllies` (`0x85C2B8 .. 0x85C2D4`) | `test_bfme2mode0.cpp`; the one BFME2 user, `MordorCatapultHumanHeadsProjectile`, says `No` |
| client shell | absent GameData `ShellMapOn` | an error (RotWK's data sets it) | GlobalData's constructor default 1 (`0x6369FD`) | `test_bfme2mode0.cpp` |

What still plays by RotWK's rules in a BFME2 game is everything else,
including the 573 divergent functions below.

Under the switch the real client runs a BFME2 skirmish end to end on BFME2
1.06's archives alone: BFME2's menus, its skirmish lobby, the load screen and
the game with the HUD. No load error and no client error.

### The oracle check (deliverable d)

`AIFarmKillSquad::initializeTeamTemplate` writes the team template's priority:
500 in RotWK (RW `0x9BB49D`) and 30 in BFME2 (`0x9AD00B`). This is the first
edition variant: `engine/src/GameLogic/SkirmishAI/SkirmishAIEditionRules.h`,
read through `GameLogic::edition()`.

`tools/retail_oracle/test_edition_oracle.py` runs both retail instruction
sequences on a zeroed template and compares the stored value with the
engine's constant. It passes, an exact match on both images. The BFME2 image
is the clean 1.06 one (md5 checked); the RotWK image carries the S-001
caveat.

The same function's forbidden KindOf mask differs too: RotWK adds the bit the
port reads as `ARCHER`. That difference is counted but not ported, because
BFME2's KindOf numbering is unverified.

The oracle helper is MSVC-built on Windows (`build.bat`). It also builds with
llvm-mingw (`tools/retail_oracle/build_mingw.sh`) and runs under Wine, which
`oracle.py` uses off Windows with RotWK's / BFME2's own `msvcr71` / `msvcp71`
forced native. That is how this check ran (Wine 11.19). Under Wine the rest of
the oracle suite gave 29 passed and 10 skipped (the drivers that need MSVC).
The 5 failures came from the host environment: the venv has no `numpy`, and
`tasklist` is Windows-only. No Windows runner is needed for this kind of
check, and no game file goes to a hosted runner.

### Phase 1, sized from the census

1. **Plumbing** (small to medium):
   - an edition line in the profile identity, START, the replay header and
     saves, with named refusals on a mismatch, copied from SimulationProfile's
     path and tests;
   - one wire / version claim (coordinate with profile 3's bump; no "missing
     line = RotWK" default);
   - a player-visible choice in the launcher and skirmish setup;
   - BFME2 campaigns moved onto the BFME2 edition.
2. **BFME2 field tables** (small): BFME2 1.06 data needs only one BFME2 row
   today (`PreLandingEmotionAffectsAllies`, done in Phase 0). Mods for BFME2
   need the rest: a table diff between the binaries, one BFME2 row per field
   RotWK dropped.
3. **Real divergences: 573 functions**, ported per subsystem:
   - AI 148
   - objects / modules 124
   - engine core 107
   - combat 45
   - script engine 35
   - horde 27
   - INI 20
   - castles 15
   - economy 8
   - ...

   They break down into 319 edited (branches), 90 constants, 67 calls and 86
   diverged. Each is a variant row citing both binaries, with oracle checks
   where a leaf can run.
4. **RotWK-only off in BFME2: 590 functions.** Each needs an explicit "not in
   BFME2" path (a named stop, not a silent RotWK run).
5. **BFME2-only GameLogic: 777 functions, never ported.** The biggest groups
   are objects / modules 271, AI 135, War of the Ring 111 and combat 85.
6. **Unresolved data cites: 1,187** (941 `.rdata` tables and constants, 245
   globals). They need a data-table diff between the binaries, which the
   census does not do yet.

A fixed small BFME2 pin set (this AI-vs-AI twin plus a load census) keeps the
gate budget flat.

## Census

`tools/re/edition_census.py` resolves every `RW 0x...` cite in
`engine/src/GameLogic` and `engine/src/Common` to its RotWK function through
the xmap (`docs/XMAP.md`, `tools/re/README.md`). It then compares each
function with its BFME2 1.06 counterpart instruction by instruction:

- **same behaviour**: tier A (identical after masking addresses), or a tier B
  same-shape pair whose differences are layout only. Layout means struct field
  offsets and vtable slots, stack frames, `this` + offset arithmetic and array
  strides, allocation sizes, `__LINE__` numbers, register choice, and the
  addresses of the same strings and globals.
- **real divergence**, any of these:
  - a same-shape pair with a changed immediate constant (a constructor
    default, a compared value, an enum count);
  - a changed float or other constant that the code loads from `.rdata`;
  - a call to a different function (both callees are paired by the map, and
    the pairs disagree);
  - tier B edited (instructions added or removed: branches);
  - tier C / D (diverged).
- **RotWK-only**: no BFME2 counterpart. This is the expansion's code, which is
  off in the BFME2 edition.
- **BFME2-only GameLogic**: BFME2 1.06 functions with no RotWK counterpart
  whose decomp source is under `GameLogic`. They were never ported, because
  every port starts from RotWK.

These are heuristics with known limits. A "constant" includes enum counts that
RotWK grew (`cmp reg, 0x21` against `0x20`), which may not change behaviour.
`this` + offset arithmetic counts as layout, so a counter that changed by a
multiple of 4 is missed. A call whose callee the map cannot pair is counted as
layout (`call-unpaired`), not as divergence. Cites that do not land inside a
mapped function are listed as unresolved, by section.

Regenerate this section on a machine with the xmap and both binaries:

```sh
python3 tools/re/edition_census.py --rotwk "$ROTWK_INSTALL/game.dat" --bfme2 "$BFME2_INSTALL/game.dat" \
    --json workspace/rebuild/xmap/edition_census.json --doc docs/EDITIONS.md
```

<!-- edition_census:begin (generated by tools/re/edition_census.py; do not edit by hand) -->

Inputs: engine source f73af1558; xmap rw_map.csv (68433 RotWK functions); BFME2 inventory 66336 functions, decomp ledger 98943 rows; binaries RotWK md5 daed3668006cd90f01c34e5a7da1901f, BFME2 md5 34391b253b0ee12d6cec1178f46d3bf9

| Class | Functions | Meaning |
|---|---|---|
| same behaviour | 4485 | tier A identical 3043 + tier B same-shape with layout-only differences 1442 |
| real divergence | 573 | branches 319, call 67, constant 90, data-constant 1, diverged 86, opcode 15 |
| RotWK-only | 590 | no BFME2 counterpart: off in the BFME2 edition |
| BFME2-only GameLogic | 777 | BFME2 functions with no RotWK counterpart, never ported |

5648 RotWK functions are cited from 12062 distinct addresses in engine/src/GameLogic and engine/src/Common. A function counts once, in the first class that applies (divergent if any difference is real).

Unresolved cites (the address is not inside a mapped function):

- data (.rdata: constants, field / lookup tables, vtables): 941
- globals (.data): 245
- no section: 1

Per subsystem (the xmap's subsystem of the RotWK function; "economy" = money, resources, supply, command points, taxes):

| Subsystem | Identical | Offset-only | Real divergence | RotWK-only | BFME2-only GameLogic |
|---|---|---|---|---|---|
| objects / modules | 522 | 430 | 124 | 117 | 271 |
| AI / pathfinding | 603 | 384 | 148 | 119 | 135 |
| engine core / system | 630 | 223 | 107 | 62 | 63 |
| combat / weapons | 244 | 80 | 45 | 54 | 85 |
| script engine | 231 | 113 | 35 | 9 | 26 |
| horde / locomotion | 134 | 96 | 27 | 17 | 19 |
| GameClient / draw | 149 | 18 | 10 | 9 | 13 |
| INI / templates | 132 | 17 | 20 | 5 | 0 |
| unclassified | 0 | 0 | 0 | 163 | 0 |
| castles / structures | 76 | 40 | 15 | 4 | 23 |
| audio | 85 | 6 | 7 | 12 | 12 |
| War of the Ring / LivingWorld | 6 | 1 | 1 | 2 | 111 |
| economy | 57 | 19 | 8 | 0 | 16 |
| APT / UI / shell | 39 | 10 | 20 | 5 | 0 |
| math / support libs | 44 | 4 | 2 | 1 | 0 |
| library | 39 | 1 | 0 | 1 | 0 |
| map / terrain logic | 30 | 0 | 1 | 3 | 3 |
| network / online | 9 | 0 | 1 | 6 | 0 |
| EH/compiler glue | 8 | 0 | 0 | 0 | 0 |
| other | 5 | 0 | 2 | 0 | 0 |
| Create-a-Hero | 0 | 0 | 0 | 1 | 0 |
| **total** | 3043 | 1442 | 573 | 590 | 777 |

<details><summary>The 777 BFME2-only GameLogic functions, by class</summary>

- `(free functions)` (243): `$L10047`, `$L10540`, `$L10541`, `$L10542`, `$L10543`, `$L11455`, `$L1188`, `$L12291`, `$L12299`, `$L12301`, `$L12303`, `$L12304`, `$L12305`, `$L12307`, `$L12308`, `$L1399`, `$L14321`, `$L1440`, `$L1630`, `$L17338`, `$L1808`, `$L1810`, `$L1816`, `$L1990`, `$L2065`, `$L21793`, `$L21821`, `$L21823`, `$L21825`, `$L21827`, `$L21828`, `$L21829`, `$L23734`, `$L25154`, `$L26885`, `$L2798`, `$L29113`, `$L29149`, `$L29152`, `$L29264` ...
- `AIWall` (7): `buildGate`, `calcGatePosition`, `informInstanceOnTriggerEntered`, `init`, `rva004EADBA`, `rva004EBB58`, `updateState`
- `LivingWorldAIBuilder` (7): `FillBuildingOrders`, `rva0059CEE4`, `rva0059CF34`, `rva0059D089`, `rva0059D1A9`, `rva0059D1C3`, `rva0059D1DD`
- `AIStructureCreepTactic` (6): `canRun`, `findBestInterestZone`, `moveDozerAway`, `rva005AC294`, `update`, `validateTemplateName`
- `AIUpdateInterface` (5): ``scalar deleting destructor'`, `friend_newModuleInstance`, `rva00262D40`, `rva00265667`, `setLastCommandSource`
- `Object` (5): `defect`, `findModule`, `makeDirty`, `onCapture`, `rva0028F518`
- `GhostObject` (4): `vslot00`, `vslot04`, `vslot08`, `vslot0C`
- `AIGroup` (3): `cohereMoveSpeed`, `containsAnyObjectsNotOwnedByPlayer`, `rva0036E071`
- `AutoHealBehavior` (3): `pulseHealObject`, `rva00045235E`, `xfer`
- `CastleBehavior` (3): `friend_newModuleInstance`, `rva003977F6`, `rva0039A7D6`
- `CaveContain` (3): `iterateContained`, `removeAllContained`, `xfer`
- `GameLogic` (3): `getFirstObject`, `isInMultiplayerGame`, `rva0023D17D`
- `LivingWorldRegion` (3): `rva003F0F48`, `rva003F1B7C`, `rva003F1BD3`
- `ProductionUpdate` (3): `GetEntryToProcess`, `ProductionUpdate`, `getModuleNameKey`
- `ScriptEngine` (3): `notifyOfAcquiredScience`, `rva00357A97`, `rva00357AF8`
- `WorkerAIUpdate` (3): `getModuleNameKey`, `isSupplyTruckBrainActiveAndBusy`, `xfer`
- `AIFarmKillSquad` (2): `findFarmTarget`, `findProductionTarget`
- `AIGateUpdate` (2): `friend_newModuleInstance`, `~AIGateUpdate`
- `AITargetChooser` (2): `isAchievableTarget`, `rva00505408`
- `AITeamBuilder` (2): `checkIdleTeams`, `recruitUnDefined`
- `AimWeaponBehavior` (2): `getModuleNameKey`, `update`
- `ArmorTemplate` (2): `ArmorTemplate`, `parseArmorCoefficients`
- `AudioLoopUpgrade` (2): `loadPostProcess`, `update`
- `CitadelSlaughterHordeContain` (2): `rva00480E9A`, `xfer`
- `DozerAIUpdate` (2): `onDelete`, `privateRepair`
- `EvacuateGarrisonSpecialPowerModuleData` (2): `EvacuateGarrisonSpecialPowerModuleData`, `friend_newModuleData`
- `ExperienceLevelCreate` (2): `onBuildComplete`, `rva004B9201`
- `FoundationAIUpdate` (2): `friend_newModuleInstance`, `xfer`
- `GateOpenAndCloseBehavior` (2): `friend_newModuleInstance`, `xfer`
- `GeometryUpgrade` (2): `xfer`, `~GeometryUpgrade`
- `GettingBuiltBehavior` (2): `loadPostProcess`, `onObjectCreated`
- `GettingBuiltBehaviorModuleData` (2): ``scalar deleting destructor'`, `~GettingBuiltBehaviorModuleData`
- `HordeAIUpdate` (2): `aiDoCommand`, `update`
- `HordeContain` (2): `checkSpecialUnitDeath`, `rva004690D0`
- `HordeSiegeEngineContain` (2): `onDelete`, `removeFromContainList`
- `InvisibilityUpdateModuleData` (2): `friend_newModuleData`, `~InvisibilityUpdateModuleData`
- `LargeGroupAudioUpdate` (2): `friend_newModuleInstance`, `rva004AB7C8`
- `LivingWorldAIArmyMover` (2): `DefendHomeTerritory`, `rva0059C631`
- `LivingWorldLogic` (2): `PrepareRetreats`, `armyMoveRequest`
- `Locomotor` (2): `rva001E7ECA`, `setFlag`
- `MaxHealthUpgradeModuleData` (2): `MaxHealthUpgradeModuleData`, `friend_newModuleData`
- `MissileUpdateModuleData` (2): `friend_newModuleData`, `~MissileUpdateModuleData`
- `Pathfinder` (2): `iterateCellsAlongLine`, `rva002EE96B`
- `QueueProductionExitUpdate` (2): `getModuleNameKey`, `~QueueProductionExitUpdate`
- `RampageBehavior` (2): `getModuleNameKey`, `update`
- `RebuildHoleExposeDieModuleData` (2): `RebuildHoleExposeDieModuleData`, `friend_newModuleData`
- `ReplenishUnitsBehavior` (2): `update`, `~ReplenishUnitsBehavior`
- `RousingSpeechUpdate` (2): `rva004ACF1D`, `update`
- `Rva00204136` (2): `rva00204136`, `rva00204984`
- `Rva002A8F24` (2): `Rva002A8F24`, `update`
- `Rva003ECA69Element` (2): `rva003ECB52`, `rva003ECB94`
- `Rva004FD1BC` (2): `Rva004FD1BC`, `~Rva004FD1BC`
- `ScaleWallSpecialAbilityUpdateModuleData` (2): `ScaleWallSpecialAbilityUpdateModuleData`, `friend_newModuleData`
- `ScriptAction` (2): `getParameter`, `getUiStrings`
- `ScriptActions` (2): `Rva003BD884Set`, `doModifyBuildableStatus`
- `SpecialAbilityUpdate` (2): `rva0044EE80`, `update`
- `StateMachine` (2): `isGoalObjectDestroyed`, `setGoalPosition`
- `SupplyCenterDockUpdate` (2): `update`, `xfer`
- `SymbioticStructuresBody` (2): `getModuleNameKey`, `rva004C123C`
- `TeleportToCasterSpecialPowerModuleData` (2): `TeleportToCasterSpecialPowerModuleData`, `friend_newModuleData`
- `ToggleHiddenSpecialAbilityUpdateModuleData` (2): `ToggleHiddenSpecialAbilityUpdateModuleData`, `friend_newModuleData`
- `ToggleMountedSpecialAbilityUpdate` (2): `createMountedTemplate`, `getModuleNameKey`
- `TransportAIUpdate` (2): `friend_newModuleInstance`, `xfer`
- `TunnelContain` (2): `scatterToNearbyPosition`, `xfer`
- `UnpauseSpecialPowerUpgradeModuleData` (2): `UnpauseSpecialPowerUpgradeModuleData`, `friend_newModuleData`
- `VictoryConditions` (2): `testDefeat`, `update`
- `WeaponFireSpecialAbilityUpdateModuleData` (2): `WeaponFireSpecialAbilityUpdateModuleData`, `~WeaponFireSpecialAbilityUpdateModuleData`
- `WeaponSetSpecialAbilityUpdateModuleData` (2): `WeaponSetSpecialAbilityUpdateModuleData`, `friend_newModuleData`
- `WeaponStore` (2): `allocateNewWeapon`, `rva002CE964`
- `$$CBW4LocomotorSetType::pair<>` (1): `~pair<>`
- `AI` (1): `parseScience`
- `AIAttackPositionFireWeaponState` (1): `update`
- `AIBuilder` (1): `moneySaverUpdate`
- `AIChargeTargetState` (1): `rva0034BFFF`
- `AIDockState` (1): `onExit`
- `AIGiantBirdAttack` (1): `xfer`
- `AIGiantBirdSwoopState` (1): `xfer`
- `AIGuardRetaliateMachine` (1): `xfer`
- `AIMoveToPositionAndEnterState` (1): `onExit`
- `AIPickUpCrateState` (1): `update`
- `AISpecialPowerCaptureBuilding` (1): `shouldActivate`
- `AISpecialPowerSelfBuff` (1): `shouldActivate`
- `AISpecialPowerUpdate` (1): `~AISpecialPowerUpdate`
- `AISpellBookHeal` (1): `shouldActivate`
- `AIStatCollector` (1): `rva004DFB7E`
- `AIStateMachine` (1): `setGoalWaypoint`
- `AITactic` (1): `end`
- `AITacticsGenerator` (1): `rva00506020`
- `AITargetHeuristicTechBuilding` (1): `findBestTarget`
- `AIUnitBuilder` (1): `rva00598016`
- `AIUpgrade` (1): `rva005975C0`
- `AIUpgradeHeuristicFortress` (1): `getUpgradeHeuristicResult`
- `AIUpgradeScienceBuilder` (1): `buildExpansionUpgrades`
- `AODCrushCollideModuleData` (1): `AODCrushCollideModuleData`
- `AllowBannerSpawnUpgrade` (1): `~AllowBannerSpawnUpgrade`
- `AmoebaAttackEntry` (1): `rva005872CF`
- `AnimalAIUpdate` (1): `getModuleNameKey`
- `ArmorStore` (1): `ArmorStore`
- `ArmorUpgrade` (1): `~ArmorUpgrade`
- `ArmySummary` (1): `Load`
- `ArmySummarySystem` (1): `GetArmyIDOfFirstWalkOnArmy`
- `ArrowStormUpdate` (1): `startPreparation`
- `AttachUpdate` (1): `getModuleNameKey`
- `AttackNugget` (1): `~AttackNugget`
- `AttackObjectGroupOrder` (1): `xfer`
- `AttackOrdersMember` (1): `rva0031996D`
- `AttributeModifierPoolUpdate` (1): `friend_newModuleInstance`
- `AudioLoopUpgradeModuleData` (1): `~AudioLoopUpgradeModuleData`
- `AutoDepositUpdateModuleData` (1): `~AutoDepositUpdateModuleData`
- `AutoFindHealingUpdate` (1): `getModuleNameKey`
- `BaseUpgradeModuleData` (1): `BaseUpgradeModuleData`
- `BehaviorModule` (1): `xfer`
- `BezierProjectileBehavior` (1): `friend_newModuleData`
- `BfmeNodeZ` (1): `~BfmeNodeZ`
- `BfmeSelectionState` (1): `isSelectionLocked`
- `BloodthirstyUpdate` (1): `getModuleNameKey`
- `BloodthirstyUpdateModuleData` (1): `friend_newModuleData`
- `BodyModule` (1): `xfer`
- `BoneFXDamage` (1): `rva004B9757`
- `BoneFXUpdate` (1): `killRunningParticleSystems`
- `BoredUpdateModuleData` (1): `BoredUpdateModuleData`
- `BridgeScaffoldBehavior` (1): `rva0004583BD`
- `BridgeTowerBehavior` (1): `~BridgeTowerBehavior`
- `BroadcastStealthUpdate` (1): `xfer`
- `BuffManager` (1): ``scalar deleting destructor'`
- `BuildListInfo` (1): `getDesiredGatherers`
- `CallHelpOnDamageModuleData` (1): `CallHelpOnDamageModuleData`
- `CastleMemberBehavior` (1): ``scalar deleting destructor'`
- `CastleMemberBehaviorModuleData` (1): `~CastleMemberBehaviorModuleData`
- `CastleUpgrade` (1): `upgradeImplementation`
- `CastleUpgradeModuleData` (1): `CastleUpgradeModuleData`
- `CaveSystem` (1): `DoXfer`
- `ChangeStanceGroupOrder` (1): `ChangeStanceGroupOrder`
- `CivilianSpawnCollideModuleData` (1): `CivilianSpawnCollideModuleData`
- `CivilianSpawnUpdate` (1): `getModuleNameKey`
- `ClickReactionBehavior` (1): `getModuleNameKey`
- `CloudBreakSpecialPower` (1): `doSpecialPowerAtLocation`
- `CommandSetUpgradeModuleData` (1): `friend_newModuleData`
- `Condition` (1): `Condition`
- `CrateCollideModuleData` (1): `buildFieldParse`
- `CreateAHeroManager` (1): `FindBlingByUpgradeName`
- `CreateCrateDie` (1): `getModuleNameKey`
- `CreateModule` (1): `xfer`
- `CreateObjectDieIfEldestKindof` (1): `getModuleNameKey`
- `CritterEmitterUpdate` (1): `~CritterEmitterUpdate`
- `CritterEmitterUpdateModuleData` (1): `friend_newModuleData`
- `CurseSpecialPower` (1): `triggerAbilityEffect`
- `CurseSpecialPowerModuleData` (1): `CurseSpecialPowerModuleData`
- `CursorParticleSystemFXNugget` (1): `~CursorParticleSystemFXNugget`
- `DOTManager` (1): `DoXfer`
- `DamageFilteredCreateObjectDie` (1): `~DamageFilteredCreateObjectDie`
- `DamageFilteredCreateObjectDieModuleData` (1): `DamageFilteredCreateObjectDieModuleData`
- `DarknessSpecialPowerModuleData` (1): `DarknessSpecialPowerModuleData`
- `DefectorSpecialPowerModuleData` (1): `DefectorSpecialPowerModuleData`
- `DelayedExperienceLevelGrantSystem` (1): ``scalar deleting destructor'`
- `DelayedWeaponSetUpgradeUpdate` (1): `~DelayedWeaponSetUpgradeUpdate`
- `DemoTrapUpdateModuleData` (1): `DemoTrapUpdateModuleData`
- `DetachableRiderBodyModuleData` (1): `DetachableRiderBodyModuleData`
- `DevastateSpecialPowerModuleData` (1): `~DevastateSpecialPowerModuleData`
- `DllHandle` (1): `DllHandle`
- `DoCommandUpgrade` (1): `~DoCommandUpgrade`
- `DockUpdateModuleData` (1): `DockUpdateModuleData`
- `DrawableModule` (1): `DrawableModule`
- `DualWeaponBehavior` (1): `~DualWeaponBehavior`
- `Emotion` (1): `rva004DCEC4`
- `EvacuateGarrisonSpecialPower` (1): `finishAbility`
- `ExperienceScalarUpgradeModuleData` (1): `ExperienceScalarUpgradeModuleData`
- `FakePathfindPortalBehaviour` (1): `rva0046190A`
- `FakePathfindPortalBehaviourModuleData` (1): `friend_newModuleData`
- `FireWeaponUpdateModuleData` (1): ``scalar deleting destructor'`
- `FireWeaponWhenDamagedBehavior` (1): `friend_newModuleInstance`
- `FireWeaponWhenDamagedBehaviorModuleData` (1): `FireWeaponWhenDamagedBehaviorModuleData`
- `FlightDeckBehavior::PAUFlightDeckInfo` (1): `__uninitialized_fill_n<>`
- `FormationSquad` (1): `rva00549345`
- `FreezingRainSpecialPowerModuleData` (1): `FreezingRainSpecialPowerModuleData`
- `GarrisonObjectGroupOrder` (1): `xfer`
- `GenericObjectCreationNugget::AnimSet` (1): `~AnimSet`
- `GeometryShape` (1): `operator=`
- `GiantBirdFollowPathState` (1): `update`
- `GiantBirdFollowWaypointPathState` (1): `update`
- `GiantBirdGuardOuterState` (1): `xfer`
- `GiveOrRestoreUpgradeSpecialPowerModuleData` (1): `~GiveOrRestoreUpgradeSpecialPowerModuleData`
- `GloriousChargeUpdate` (1): `rva004AD613`
- `GloriousChargeUpdateModuleData` (1): `friend_newModuleData`
- `GrabPassengerSpecialPowerModuleData` (1): `GrabPassengerSpecialPowerModuleData`
- `GrantUpgradeCreate` (1): `onBuildComplete`
- `GrantUpgradeCreateModuleData` (1): `~GrantUpgradeCreateModuleData`
- `GroupOrder` (1): `GroupOrder`
- `HealCrateCollide` (1): `executeCrateBehavior`
- `HeightDieUpdate` (1): `getModuleNameKey`
- `HeroModeSpecialAbilityUpdateModuleData` (1): `~HeroModeSpecialAbilityUpdateModuleData`
- `HighlanderBody` (1): `attemptDamage`
- `HijackerUpdate` (1): `rva0004A4010`
- `HitReactionBehavior` (1): `getModuleNameKey`
- `HordeContainModuleData` (1): `friend_newModuleData`
- `HordeGarrisonContainModuleData` (1): `~HordeGarrisonContainModuleData`
- `HordeSiegeEngineContainModuleData` (1): `friend_newModuleData`
- `ImmortalBody` (1): `internalChangeHealth`
- `InheritUpgradeCreateModuleData` (1): `friend_newModuleData`
- `InstantDeathBehavior` (1): `rva00045CF10`
- `LargeGroupAudio` (1): ``scalar deleting destructor'`
- `LargeGroupBonusUpdate` (1): `xfer`
- `LaserUpdate` (1): `rva000363102`
- `LevelGrantSpecialPower` (1): `triggerAbilityEffect`
- `LevelGrantSpecialPowerModuleData` (1): `LevelGrantSpecialPowerModuleData`
- `LifetimeUpdate` (1): `getModuleNameKey`
- `LivingWorldBattle` (1): `DoXfer`
- `LivingWorldBuildingNuggetSpawnArmy` (1): `canQueueUnit`
- `LivingWorldBuildingTemplate` (1): `findNugget`
- `LivingWorldCampaignManager` (1): `rva003B8E2B`
- `LivingWorldPlayer` (1): `FindArmyWithHero`
- `LivingWorldScenario::OwnershipSet` (1): `spawnArmiesInRegionsForPlayer`
- `LockWeaponCreateModuleData` (1): `friend_newModuleData`
- `LocomotorSetUpgrade` (1): `~LocomotorSetUpgrade`
- `Made002CC711` (1): ``scalar deleting destructor'`
- `Made002CC8A4` (1): `rva0050A4A2`
- `Made002CCA37` (1): `Made002CCA37`
- `Matrix3D` (1): `Pre_Rotate_Z`
- `MineshaftPortalBehaviourModuleData` (1): `friend_newModuleData`
- `MissileLauncherBuildingUpdate` (1): ``scalar deleting destructor'`
- `ModelConditionAudioLoopClientBehaviorModuleData` (1): `friend_newModuleData`
- `ModelConditionSpecialAbilityUpdateModuleData` (1): `friend_newModuleData`
- `MoneyCrateCollideModuleData` (1): `MoneyCrateCollideModuleData`
- `MonitorConditionUpdateModuleData` (1): `MonitorConditionUpdateModuleData`
- `MoveToGroupOrder` (1): `rva00547368`
- `NotifyTargetsOfImminentProbableCrushingUpdate` (1): `xfer`
- `OCLSpecialPowerModuleData` (1): `friend_newModuleData`
- `OCLUpdate` (1): `getRemainingFrames`
- `OathbreakerBody` (1): ``scalar deleting destructor'`
- `ObjectCreationUpgrade` (1): ``scalar deleting destructor'`
- `ObjectTypes` (1): `isInSet`
- `OilSpillUpdate` (1): `xfer`
- `OneRingPenaltyUpdate` (1): `getModuleNameKey`
- `OneRingPenaltyUpdateModuleData` (1): `~OneRingPenaltyUpdateModuleData`
- `OpenContain` (1): `getModuleNameKey`
- `Overridable` (1): `markAsOverride`
- `PAUAngleFXInfo` (1): `_M_allocate_and_copy<>`
- `PAUDynamicPortalLink` (1): `_Destroy<>`
- `PAURva00395D77` (1): `_Destroy<>`
- `PAURva0047BAF7Element::list<>` (1): `remove`
- `PartTheHeavensUpdate` (1): `~PartTheHeavensUpdate`
- `PartitionFilterFlammable` (1): `allow`
- `PartitionManager` (1): `rva00625300`
- `Path` (1): `SetLastNodePortal`
- `PathfinderPosGoalManager` (1): `rva004DE2ED`
- `PickupStuffUpdate` (1): `update`
- `PickupStuffUpdateModuleData` (1): `PickupStuffUpdateModuleData`
- `PillageModule` (1): `rva00484FE2`
- `PoisonedBehavior` (1): `calcSleepTime`
- `PoisonedBehaviorModuleData` (1): `friend_newModuleData`
- `PolygonTrigger` (1): `rva0007E03A`
- `ProductionQueueHordeContain` (1): `xfer`
- `ProductionQueueHordeContainModuleData` (1): `iniParseTemplateContainer`
- `ProjectileStreamUpdate` (1): `update`
- `PropagandaTowerBehavior` (1): `getModuleNameKey`
- `RadarMarkerClientUpdateModuleData` (1): `friend_newModuleData`
- `RadarUpdate` (1): `extendRadar`
- `RadarUpgradeModuleData` (1): `RadarUpgradeModuleData`
- `RadiusDecalUpdate` (1): `xfer`
- `RebuildHoleBehavior` (1): `getRebuildHoleBehaviorInterfaceFromObject`
- `RefundDieModuleData` (1): `RefundDieModuleData`
- `RegionAwardDispute` (1): `rva004FBF8E`
- `RemoveUpgradeUpgrade` (1): `upgradeRemovalImplementation`
- `RemoveUpgradeUpgradeModuleData` (1): `friend_newModuleData`
- `RepairDockUpdate` (1): `action`
- `ReplaceSelfUpgrade` (1): `xfer`
- `ReplenishUnitsBehaviorModuleData` (1): `ReplenishUnitsBehaviorModuleData`
- `RespawnBodyModuleData` (1): `~RespawnBodyModuleData`
- `RespawnUpdate` (1): `getModuleNameKey`
- `RiderChangeContain` (1): `friend_newModuleInstance`
- `RunOffMapBehaviorModuleData` (1): `~RunOffMapBehaviorModuleData`
- `Rva000635AB` (1): `rva000635AB`
- `Rva00087A93` (1): ``scalar deleting destructor'`
- `Rva00199FCC` (1): `rva00199FCC`
- `Rva001DFEAABase` (1): ``scalar deleting destructor'`
- `Rva001EAF7B` (1): `rva001EAF7B`
- `Rva001EB0B1Holder` (1): `rva001EB0B1`
- `Rva001EB0FB` (1): `rva001EB0FB`
- `Rva001F9B52` (1): `Rva001F9B52`
- `Rva001FB8A9` (1): `Rva001FB8A9`
- `Rva001FBBE4` (1): `Rva001FBBE4`
- `Rva0020E100Owner` (1): `forwardAndFinish`
- `Rva0020E205` (1): `~Rva0020E205`
- `Rva00210C66CmpBoolField` (1): `get`
- `Rva0023D377` (1): ``scalar deleting destructor'`
- `Rva0024076D` (1): ``scalar deleting destructor'`
- `Rva00242D1DLoadView` (1): `loadPostProcess`
- `Rva00243B48` (1): `~Rva00243B48`
- `Rva00246FD7` (1): `Rva00246FD7`
- `Rva00248CDD` (1): `rva00248CDD`
- `Rva002542F3Member` (1): `Rva002542F3Member`
- `Rva00261409Filter` (1): `getPlayerMask`
- `Rva0026AF86` (1): `~Rva0026AF86`
- `Rva0027C230` (1): `Rva0027C230`
- `Rva0027D30D` (1): `rva0027D30D`
- `Rva0027D347` (1): `rva0027D347`
- `Rva002A8D49` (1): `Rva002A8D49`
- `Rva002C5FD9` (1): `rva002C5FD9`
- `Rva002E1948Lookup` (1): `find`
- `Rva002E2AAE` (1): `loadPostProcess`
- `Rva002E3766Holder` (1): `notifyObjectExited`
- `Rva003058F9` (1): `Rva003058F9`
- `Rva00332E60` (1): `rva00332E60`
- `Rva00368344` (1): `rva0036BB2F`
- `Rva00376A62` (1): `rva0059E872`
- `Rva0037F77C` (1): `rva0037F77C`
- `Rva0039627D` (1): `Rva0039627D`
- `Rva003B7C1E` (1): `rva003B7C1E`
- `Rva003ECD60Object` (1): `xfer`
- `Rva003EE083` (1): `rva003EE083`
- `Rva003EFC1CHost` (1): `rva003EFC1C`
- `Rva003F1093` (1): `rva003F1CFE`
- `Rva003F1C56Plot` (1): `rva004FC33E`
- `Rva003F7177Callback` (1): `Rva003F7177Callback`
- `Rva003FD4DA` (1): ``scalar deleting destructor'`
- `Rva00406F9C` (1): `rva004CE4D4`
- `Rva0041E6BE` (1): `Rva0041E6BE`
- `Rva00439E0C` (1): `rva00438D58`
- `Rva0044EECA` (1): `rva0044EECA`
- `Rva00458BE1` (1): `rva00458BE1`
- `Rva0045EF55List` (1): `forEach`
- `Rva0046262D` (1): `Rva0046262D`
- `Rva00466E23` (1): `~Rva00466E23`
- `Rva004697E1Contain` (1): `rva004783D7`
- `Rva00472620` (1): `rva00472620`
- `Rva004783F4` (1): `rva004783F4`
- `Rva0047BDED` (1): `rva0047BE23`
- `Rva0048130E` (1): `~Rva0048130E`
- `Rva00483C9FFilter` (1): `allow`
- `Rva004851FE` (1): `~Rva004851FE`
- `Rva00485983` (1): `~Rva00485983`
- `Rva00485ADCFilter` (1): `Rva00485ADCFilter`
- `Rva004864AA` (1): `~Rva004864AA`
- `Rva00486579` (1): `~Rva00486579`
- `Rva00486B56` (1): `~Rva00486B56`
- `Rva00492179` (1): `startUnpacking`
- `Rva0049520B` (1): `update`
- `Rva00496A63` (1): `~Rva00496A63`
- `Rva004B2A9D` (1): `rva004B2A9D`
- `Rva004B2F7B` (1): `rva004B2F7B`
- `Rva004B39FC` (1): `~Rva004B39FC`
- `Rva004B3C0A` (1): `~Rva004B3C0A`
- `Rva004B3D3B` (1): `~Rva004B3D3B`
- `Rva004B4130` (1): `rva004B4130`
- `Rva004B48D3` (1): `~Rva004B48D3`
- `Rva004B4CDF` (1): `loadPostProcess`
- `Rva004B55C8` (1): `~Rva004B55C8`
- `Rva004B568B` (1): `~Rva004B568B`
- `Rva004B57CE` (1): `~Rva004B57CE`
- `Rva004B5982` (1): `~Rva004B5982`
- `Rva004B5D0B` (1): `~Rva004B5D0B`
- `Rva004B66B3` (1): `~Rva004B66B3`
- `Rva004B67DC` (1): `~Rva004B67DC`
- `Rva004B79AD` (1): `~Rva004B79AD`
- `Rva004B82A5` (1): `~Rva004B82A5`
- `Rva004B8732` (1): `xfer`
- `Rva004B9301` (1): `rva004B9301`
- `Rva004C227A` (1): ``scalar deleting destructor'`
- `Rva004C447D` (1): `xfer`
- `Rva004CBE31` (1): `rva004CBE31`
- `Rva004CBEB0` (1): `Rva004CBEB0`
- `Rva004DC1BF` (1): `rva004DC096`
- `Rva004E194E` (1): `Rva004E194E`
- `Rva004EA3B8` (1): `~Rva004EA3B8`
- `Rva004EB5E9` (1): `~Rva004EB5E9`
- `Rva004FAC6B` (1): `Rva004FAC6B`
- `Rva00501E3FElement` (1): `operator=`
- `Rva00583794` (1): `rva00583794`
- `Rva0059BB50` (1): `rva0059BB50`
- `Rva0059D90C` (1): `rva0059D90C`
- `Rva0059DA34` (1): `rva0059DB48`
- `Rva0059DD3F` (1): `rva0059D8A5`
- `Rva005C98C0` (1): `rva005C98C0`
- `Rva005D8223` (1): `shouldActivate`
- `Rva005D84F6` (1): `rva005D8619`
- `Rva0061EEE0` (1): ``scalar deleting destructor'`
- `Rva008291D` (1): ``scalar deleting destructor'`
- `Rva2225E0Filter` (1): `accepts`
- `SalvageCrateCollide` (1): `rva004BD2D0`
- `ScriptList` (1): `WriteScriptsDataChunk`
- `ShareExperienceBehaviorModuleData` (1): `ShareExperienceBehaviorModuleData`
- `SiegeAIUpdate` (1): `friend_newModuleInstance`
- `SiegeAIUpdateModuleData` (1): `SiegeAIUpdateModuleData`
- `SlaughterHordeContain` (1): `rva004804DD`
- `SlaveWatcherBehavior` (1): `~SlaveWatcherBehavior`
- `SpawnBehavior` (1): `getModuleNameKey`
- `SpecialDisguiseUpdateModuleData` (1): `SpecialDisguiseUpdateModuleData`
- `SpecialPowerCompletionDie` (1): `rva000486A2F`
- `SpecialPowerCompletionDieModuleData` (1): `SpecialPowerCompletionDieModuleData`
- `SpecialPowerCreate` (1): `onBuildComplete`
- `SplitHordeSpecialPower` (1): `doSpecialPower`
- `StatusBitsUpgrade` (1): `upgradeRemovalImplementation`
- `StatusBitsUpgradeModuleData` (1): `StatusBitsUpgradeModuleData`
- `StealthDetectorUpdate` (1): ``scalar deleting destructor'`
- `StealthUpdate` (1): `rva00373D5E`
- `StealthUpgrade` (1): `~StealthUpgrade`
- `StoreObjectsSpecialPowerModuleData` (1): `friend_newModuleData`
- `StructureBodyModuleData` (1): `~StructureBodyModuleData`
- `SubObjectsUpgradeModuleData` (1): ``scalar deleting destructor'`
- `SupplyWarehouseCripplingBehavior` (1): `getModuleNameKey`
- `SupplyWarehouseDockUpdate` (1): `rva004A7DA5`
- `SymbioticStructuresBodyModuleData` (1): `friend_newModuleData`
- `SynchronizeGroupOrder` (1): `~SynchronizeGroupOrder`
- `TaintSpecialPower` (1): `doSpecialPowerAtLocation`
- `TeleportSpecialAbilityUpdateModuleData` (1): `friend_newModuleData`
- `TerrainResourceBehavior` (1): `getModuleNameKey`
- `TerrainResourceBehaviorModuleData` (1): `friend_newModuleData`
- `ToggleDeploySpecialAbilityUpdateModuleData` (1): `friend_newModuleData`
- `ToggleMountedSpecialAbilityUpdateModuleData` (1): `friend_newModuleData`
- `ToppleUpdateModuleData` (1): `ToppleUpdateModuleData`
- `TransportContain` (1): `xfer`
- `UBfmeE12` (1): `?8U?$_Deque_iterator<>`
- `UBfmePod128::$$CBHV?$vector::HU?$pair::_Rb_tree<>` (1): `_Rb_tree<>`
- `UBridgeBehaviorListValue12::list<>` (1): `~list<>`
- `UCoord3D::list<>` (1): `list<>`
- `UntamedAllegianceSpecialPower` (1): `ApplyPowerToObject`
- `UntamedAllegianceSpecialPowerModuleData` (1): `UntamedAllegianceSpecialPowerModuleData`
- `UpgradeModule` (1): ``scalar deleting destructor'`
- `UpgradeSoundSelectorClientBehaviorModuleData` (1): `friend_newModuleData`
- `VAsciiString` (1): `?0U?$pair<>`
- `VAsciiString::U?$pair::_Rb_tree<>` (1): `insert_unique`
- `VAsciiString::pair<>` (1): `pair<>`
- `VLocomotorTemplate::OVERRIDE<>` (1): `operator->`
- `VRva00285BEC` (1): `_Construct<>`
- `W3DRenderObjectSnapshot` (1): `~W3DRenderObjectSnapshot`
- `W4NameKeyType::hash_map<>` (1): `erase`
- `W4ObjectID::U?$_List_iterator` (1): `__find<>`
- `W4ParticleSystemID::vector<>` (1): `erase`
- `WallUpgradeUpdate` (1): `friend_newModuleInstance`
- `WeaponChangeSpecialPowerModuleData` (1): `~WeaponChangeSpecialPowerModuleData`
- `WeaponFireSpecialAbilityUpdate` (1): `update`
- `WorkerAIUpdateModuleData` (1): `friend_newModuleData`
- `WoundArrowUpdateModuleData` (1): `WoundArrowUpdateModuleData`
- `_STL::$$CBGE::U?$pair` (1): `_Construct<>`
- `_STL::H::HU?$_Nonconst_traits::U?$_List_iterator` (1): `find<>`
- `_STL::U_Bit_reference::_Bit_iter<>` (1): `operator--`

</details>

<!-- edition_census:end -->
