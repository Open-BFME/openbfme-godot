# FACT-C: Goblins (FactionWild) and Angmar, the creeps and neutral buildings

The player's path through every item of the two factions, driven through the HUD (clicks, control-bar presses, the GUI command's target click,
right-click orders with RotWK's default alternate mouse), is pinned by `engine/tests/test_factc_sweep.cpp`,
`engine/tests/test_factc_started.cpp` and the `factc` cases in `engine/tests/test_hud_factb_units.cpp` /
`engine/tests/test_hud_factb_production.cpp`. The brief's "FactionGoblins" is
the retail PlayerTemplate `FactionWild`. Retail-dependent runs require both
`ROTWK_INSTALL` and `BFME2_INSTALL`. The numeric counts, map inventory and generated
tables below are the 2026-10-11 snapshot, not a fresh run.

## What each sweep checks

| Sweep | Items | Checked |
|---|---|---|
| `factc inventory` | every command set reachable from the PlayerTemplate (starting building and the castle it unpacks, starting units, BuildableHeroesMP, spell store) | every button's object / upgrade / special power resolves |
| `factc abilities` | 28 Goblin + 34 Angmar SPECIAL_POWER buttons on units and heroes | unlocked at the right level, the press (and its target click) fires the power: its recharge starts |
| `factc upgrades` | 37 Goblin + 44 Angmar OBJECT / PLAYER upgrade buttons (structure levels, forge technologies, horde upgrades, fortress improvements) | price (`UpgradeTemplate::calcCostToBuild`), completion in BuildTime, every module the upgrade triggers runs |
| `factc production` | 11 + 9 UNIT_BUILD buttons of the structures | price = button = calcCostToBuild, the unit after calcTimeToBuild near the producer, its command points |
| `factc heroes` | 4 + 5 heroes | recruited from the keep's REVIVE button at the record's cost, killed, revived at the revive cost |
| `factc combat` | every unit and hero with a weapon | a right click on an enemy (an orc warrior; a troll for a siege engine) makes it lose health |
| `factc neutral` | the inn, outpost, signal fire, shipwright of the multiplayer maps | a right click on the flag captures it (S-2002), the building follows; the inn trains its faction's unit, the shipwright its ship, the outpost pays |
| `factc neutral lairs` | the six creep lairs (cave troll, Moria goblin, warg, spider, fire drake, barrow wight) | the lair spawns its creeps and they attack a party that comes near |
| `factc neutral inventory` | the 72 multiplayer maps | each loads; the neutral pieces they place (below) |

The FACT-B sweep functions run for the two factions too (the `factc ...` cases of `test_hud_factb_units.cpp` / `test_hud_factb_production.cpp`),
and `test_factc_started.cpp` adds the started skirmish (NewGame::prepareNewGame on Fall Back 4p, as the lobby starts it):

| Sweep | Items | Checked |
|---|---|---|
| `factc inventory` (started) | everything reached from the started base: the keep, the expansion pads, the wall hubs, the starting builders, and from there every command set | each button resolves; the behaviour modules without a runtime are listed |
| `factc build` / `factc structures` / `factc found` (started) | the builder's build buttons, every structure's unit and upgrade buttons, every pad's expansion | price, build time, the unit at the door, every upgrade's modules run |
| `factc castle buttons` (started) | every fortress improvement of the started keep, bought through its buttons in rounds (CASTLE_UPGRADE, the House of Lamentation, included) | the upgrade on the keep at its price; a trigger's CastleUpgrade upgrade on the keep and on a wall hub inside WallUpgradeRadius; the buttons that need it (Dragon Nest, Fire Drake, Sanctum) become enabled and are bought |
| `factc units` / `factc fights` / `factc damage` | every unit, horde and hero ability and upgrade button; every unit's attack order; the first hit | the ability's effect step, the weapon mode, the attribute modifier or the OCL (not only the recharge); an upgrade on every horde member; the first hit equals a weapon hit against the INI armour |
| `factc spell book` | every spell bought in the Palantir's store (SpellStore.apt, MSG_PURCHASE_SCIENCE) and cast through the spell bar | FACT-B's EffectProbe: its modifier, OCL objects, heal, damage, weather, upgrades or bounty seen beyond a control run |
| `factc upgrade effects` | every weapon / armour upgrade of a unit or horde | two scenes, without and with the upgrade bought at the unit's button: the INI hit (Weapon.ini x Armor.ini of the weapon and armour then) changes and the health lost shows it |
| `factc fortress` (started) | walls built from a hub's Begin Wall Span, the improvements bought at the keep, the fortress power | walls built; the CastleUpgrade upgrade on the keep and every wall; the Sanctum's Frostball cast and its hits; the Goblins' passive Bat Cloud modifier |
| `factc damage` (siege) | every siege engine against a barracks | the first hit is one of the weapon's against the structure's armour |
| `factc heroes` (started) | every hero and the ring hero | recruited and revived through the keep's REVIVE buttons |

Ported in round 3: ObjectCreationUpgrade (S-2007: the spike moat, the Sanctum's readiness, the free wall catapult / giant) and ArmorUpgrade's default
ArmorSetFlag (PLAYER_UPGRADE, RW 0x6542D8: the Goblin warriors' armour). Pins in the sweeps hold only failures that say their text ("S-xxxx|text").

Ported for the castle sweep: the CASTLE_UPGRADE button (ControlBar availability RW 0x942733 case 8, press RW 0x940435 case 8: restricted when the
member's castle already has the upgrade; the press sends MSG_QUEUE_UPGRADE for the keep). Before it the House of Lamentation could not be bought.

## Registered gaps and subsequent closures

- S-084 is closed: Drogoth has `GiantBirdAIUpdate` and GIANT_BIRD movement; his old ability, fight and first-hit failure pins are gone.
- The cave troll's tree / goblin grab (GrabPassengerSpecialPower) is an unported module (S-140). The Angmar spike moat works (ObjectCreationUpgrade, S-2007).
- The capture power's (S-2002) and the special power target click's (S-2000) unported ActionManager checks; HordeDispatchSpecialPower's kept target (S-2001).
- The Troll Sling's late exit (S-2003); Karsh's first hit (S-2004); the Angmar Necromancers' attack approach (S-2005); named ability weapons without
  their hit (S-2006); the spider riders' fire arrows (S-2008).
  The Goblin first-hit sweep now allows no failure; Angmar still pins the Necromancers and Karsh.
  The castle-button sweep no longer allows a failed Fire Drake purchase (the old S-2009 finding).
  The upgrade-effect sweep now allows only that Goblin fire-arrow failure and no Angmar failures. Snow Trolls' Dark Iron Armour still has the same
  HERO coefficient; a two-hit frame can make the scene pass, so it is not evidence of a changed coefficient.
- The buy-new wall catapult / giant buttons need SlaveWatcherBehavior (S-1995). Taint and Untamed Allegiance show no effect in the spell scene (S-1997).

## Neutral pieces of the multiplayer maps (`factc neutral inventory`)

```
AngFornostSmallSpire (1 maps)
AngFornostSpire (1 maps)
ArnorBattleTower (1 maps)
BarrowWightLair (12 maps)
BarrowWight_Slaved (12 maps)
CBHobbitBldg01 (1 maps)
CBHobbitBldg02 (1 maps)
CaptureFlag (62 maps)
CaveTrollLair (19 maps)
CaveTrollLairSnow (5 maps)
CaveTroll_Slaved (24 maps)
DaleBuilding01 (1 maps)
DaleBuilding02 (2 maps)
DaleBuilding03 (2 maps)
DaleBuilding04 (2 maps)
DaleBuilding05 (2 maps)
DaleBuilding07 (2 maps)
DaleBuilding08 (2 maps)
DaleFountain (1 maps)
DwarvenBuilding01P (3 maps)
DwarvenBuilding02 (2 maps)
DwarvenBuilding02P (3 maps)
DwarvenBuilding03 (2 maps)
DwarvenBuilding03P (3 maps)
EBGarrisonableTower (1 maps)
EttenmoorsGoblinKingFortressTower (1 maps)
EttenmoorsGoblinKingWall1 (1 maps)
EttenmoorsGoblinKingWall2 (1 maps)
EttenmoorsGoblinKingWall3 (1 maps)
EttenmoorsGoblinKingWall4 (1 maps)
FireDrakeLair (11 maps)
FireDrake_Slaved (11 maps)
FornostBrokenStatue3 (1 maps)
GBWTOPColumn3 (1 maps)
GBWTOPColumn4 (1 maps)
GBWTopWall2 (1 maps)
GondorForbiddenPool (1 maps)
Inn (29 maps)
MBMMBridge (1 maps)
MBMMStatue1 (1 maps)
MMBMMRuin1 (2 maps)
MMBMMRuin2 (1 maps)
MMBMMRuin3 (2 maps)
MMBMMRuin4 (2 maps)
MMBMMRuin5 (2 maps)
MMBMMRuin6 (2 maps)
MMBMMRuin7 (2 maps)
MMBMMRuin8 (2 maps)
MinorSpider_Slaved (7 maps)
MordorGoblinArcher_Slaved (32 maps)
MordorGoblinSwordsman_Slaved (32 maps)
MoriarGoblinLair (24 maps)
MoriarGoblinLairSnow (8 maps)
NeutralWarg (14 maps)
Outpost (40 maps)
ShipWright (9 maps)
ShireGeneric02 (1 maps)
ShireGeneric03 (1 maps)
ShireGreenDragon (1 maps)
ShireMill (1 maps)
SignalFire (45 maps)
SpiderLair (7 maps)
TomBombadilHouse (1 maps)
UmbarHouse01 (1 maps)
UmbarHouse02 (1 maps)
WargLair (14 maps)
```

## Inventory (generated: `OPENBFME_FACTC_INVENTORY=<file> openbfme_tests -tc="factc inventory*"`)

## FactionWild

### Structures (10)

| Structure | BuildCost | BuildTime (s) | Command sets | Buttons |
|---|---|---|---|---|
| GoblinCave | 400 | 20 | GoblinCaveCommandSet, GoblinCaveCommandSetLevel2, GoblinCaveCommandSetLevel3 | OBJECT_UPGRADE Upgrade_WildCaveLevel2; OBJECT_UPGRADE Upgrade_WildCaveLevel3; UNIT_BUILD GoblinArcherHorde; UNIT_BUILD GoblinFighterHorde |
| GoblinFissure | 500 | 30 | GoblinFissureCommandSet, GoblinFissureCommandSetLevel2, GoblinFissureCommandSetLevel3 | OBJECT_UPGRADE Upgrade_WildFissureLevel2; OBJECT_UPGRADE Upgrade_WildFissureLevel3; UNIT_BUILD GoblinCaveTroll; UNIT_BUILD WildBabyDrakeHorde; UNIT_BUILD WildMarauderHorde; UNIT_BUILD WildMarauderSwordHorde; UNIT_BUILD WildMountainGiant |
| MordorBase | 0 | 5 | MordorBaseTestCommandSet | OBJECT_UPGRADE Upgrade_TestBuilding; OBJECT_UPGRADE Upgrade_TestBuilding_2 |
| MordorBase_DEMO | 500 | 5 |  |  |
| WildFortress | 5000 | 120 |  |  |
| WildFortressCitadel | 5000 | 120 | WildFortressCommandSet | OBJECT_UPGRADE Upgrade_HasDragonNestFireDrake; OBJECT_UPGRADE Upgrade_WildFortressBatCloud; OBJECT_UPGRADE Upgrade_WildFortressDragonNest; OBJECT_UPGRADE Upgrade_WildFortressFireArrowsTrigger; OBJECT_UPGRADE Upgrade_WildFortressRazorSpines; OBJECT_UPGRADE Upgrade_WildFortressWebCocoonTrigger; REVIVE Command_CreateAHeroReviveSlot; REVIVE Command_GenericReviveSlot1; REVIVE Command_GenericReviveSlot2; REVIVE Command_GenericReviveSlot3; REVIVE Command_GenericReviveSlot4; REVIVE Command_GenericReviveSlot5; REVIVE Command_GenericReviveSlot6; REVIVE Command_GenericReviveSlot7; REVIVE Command_RingHeroReviveSlot; SPECIAL_POWER SpecialAbilityBatCloud; UNIT_BUILD WildPorter |
| WildLumberMill | 350 | 30 | WildLumberMillCommandSetLevel1 | UNIT_BUILD MordorWorker |
| WildMineShaft | 300 | 15 | WildMineShaftCommandSet | EXIT_CONTAINER Command_ExitGarrison |
| WildSentryTower | 500 | 60 | GenericSentryTowerCommandSet | EXIT_CONTAINER Command_ExitGarrison; OBJECT_UPGRADE Upgrade_SentryTowerFireArrows |
| WildSpiderPit | 600 | 30 | WildSpiderPitCommandSet, WildSpiderPitCommandSetLevel2, WildSpiderPitCommandSetLevel3 | OBJECT_UPGRADE Upgrade_WildSpiderPitLevel2; OBJECT_UPGRADE Upgrade_WildSpiderPitLevel3; PLAYER_UPGRADE Upgrade_WildSpiderVenomSackScience; UNIT_BUILD GoblinSpiderRiderHorde; UNIT_BUILD WildSpiderlingHorde |
| WildTreasureTrove | 1000 | 45 | WildTreasureTroveCommandSet, WildTreasureTroveCommandSetLevel2, WildTreasureTroveCommandSetLevel3 | OBJECT_UPGRADE Upgrade_WildTreasureTroveLevel2; OBJECT_UPGRADE Upgrade_WildTreasureTroveLevel3; PLAYER_UPGRADE Upgrade_TechnologyWildBasicTraining; PLAYER_UPGRADE Upgrade_TechnologyWildFireArrows; PLAYER_UPGRADE Upgrade_TechnologyWildForgedBlades; PLAYER_UPGRADE Upgrade_TechnologyWildHeavyArmor |

### Units and hordes (11)

| Unit | BuildCost | BuildTime (s) | CommandPoints | Abilities and upgrades (buttons) |
|---|---|---|---|---|
| GoblinArcherHorde | 250 | 30 | 40 | FIRE_WEAPON Command_ArcherBombard; OBJECT_UPGRADE Upgrade_WildBasicTraining; OBJECT_UPGRADE Upgrade_WildFireArrows; OBJECT_UPGRADE Upgrade_WildHeavyArmor; SPECIAL_POWER SpecialAbilityCaptureBuilding; SPECIAL_POWER SpecialAbilityPoisonArrows |
| GoblinCaveTroll | 500 | 45 | 35 | SPECIAL_POWER SpecialAbilityGrabPassenger; TOGGLE_WEAPONSET Command_ToggleMountainTrollRockThrow; WAKE_AUTO_PICKUP Command_SpecialAbilityTrollFindGoblinToThrow; WAKE_AUTO_PICKUP Command_SpecialAbilityTrollFindTree |
| GoblinFighterHorde | 80 | 33 | 40 | OBJECT_UPGRADE Upgrade_WildBasicTraining; OBJECT_UPGRADE Upgrade_WildForgedBlades; OBJECT_UPGRADE Upgrade_WildHeavyArmor; SPECIAL_POWER SpecialAbilityCaptureBuilding; SPECIAL_POWER SpecialAbilityGoblinFighterPoisonedBlades |
| GoblinSpiderRiderHorde | 600 | 30 | 78 | OBJECT_UPGRADE Upgrade_WildBasicTraining; OBJECT_UPGRADE Upgrade_WildFireArrows; OBJECT_UPGRADE Upgrade_WildForgedBlades; SPECIAL_POWER SpecialAbilityCaptureBuilding; TOGGLE_WEAPONSET Command_ToggleSpiderRiderWeapon |
| MordorWorker | 20 | 5 | 5 | FOUNDATION_CONSTRUCT MordorBase; FOUNDATION_CONSTRUCT MordorBase_DEMO; SPECIAL_POWER SpecialAbilityHarvest |
| WildBabyDrakeHorde | 1500 | 30 | 72 | SPECIAL_POWER SpecialAbilityFireDrakeInferno |
| WildMarauderHorde | 500 | 30 | 72 | HORDE_TOGGLE_FORMATION Command_WildMarauderPorcupineFormation; OBJECT_UPGRADE Upgrade_WildBasicTraining; OBJECT_UPGRADE Upgrade_WildForgedBlades; OBJECT_UPGRADE Upgrade_WildHeavyArmor; SPECIAL_POWER SpecialAbilityCaptureBuilding |
| WildMarauderSwordHorde | 500 | 30 | 88 | OBJECT_UPGRADE Upgrade_WildBasicTraining; OBJECT_UPGRADE Upgrade_WildForgedBlades; OBJECT_UPGRADE Upgrade_WildHeavyArmor; SPECIAL_POWER SpecialAbilityAngmarCharge; SPECIAL_POWER SpecialAbilityCaptureBuilding |
| WildMountainGiant | 1000 | 45 | 50 | SPECIAL_POWER SpecialAbilityWildMountainGiantStomp; TOGGLE_WEAPONSET Command_ToggleMountainGiantRockThrow |
| WildPorter | 500 | 30 | 0 | DOZER_CONSTRUCT GoblinCave; DOZER_CONSTRUCT GoblinFissure; DOZER_CONSTRUCT WildFortress; DOZER_CONSTRUCT WildLumberMill; DOZER_CONSTRUCT WildMineShaft; DOZER_CONSTRUCT WildSentryTower; DOZER_CONSTRUCT WildSpiderPit; DOZER_CONSTRUCT WildTreasureTrove; SPECIAL_POWER SpecialAbilityPorterExtinguishFire |
| WildSpiderlingHorde | 250 | 30 | 50 | OBJECT_UPGRADE Upgrade_WildBasicTraining; OBJECT_UPGRADE Upgrade_WildSpiderVenomSacks |

### Heroes (4)

| Unit | BuildCost | BuildTime (s) | CommandPoints | Abilities and upgrades (buttons) |
|---|---|---|---|---|
| Drogoth | 5000 | 60 | 75 | SPECIAL_POWER SpecialAbilityDrogothFireball; SPECIAL_POWER SpecialAbilityDrogothFireflight; SPECIAL_POWER SpecialAbilityDrogothIncinerate; SPECIAL_POWER SpecialAbilityWingBlast |
| WildAzog | 1200 | 30 | 25 | SPECIAL_POWER SpecialAbilityCaptureBuilding; SPECIAL_POWER SpecialAbilityCreateAHeroBattleRage_Level3; SPECIAL_POWER SpecialAbilityFakeLeadership2; SPECIAL_POWER SpecialAbilityKingsFavor |
| WildGoblinKing | 1800 | 30 | 25 | SPECIAL_POWER SpecialAbilityCallFromTheDeep; SPECIAL_POWER SpecialAbilityCaptureBuilding; SPECIAL_POWER SpecialAbilityFakeLeadership; SPECIAL_POWER SpecialAbilityGoblinKingPoisonedStinger; SPECIAL_POWER SpecialAbilitySkullTotem; SPECIAL_POWER SpecialAbilityToggleMounted |
| WildShelob | 2000 | 45 | 50 | SPECIAL_POWER SpecialAbilityScreech; SPECIAL_POWER SpecialAbilityWildShelobPoisonedStinger; SPECIAL_POWER SpecialAbilityWildShelobTunnel |

### Upgrades (27)

| Upgrade | Type | BuildCost | BuildTime (s) |
|---|---|---|---|
| Upgrade_TechnologyWildBasicTraining | PLAYER | 1000 | 30 |
| Upgrade_TechnologyWildFireArrows | PLAYER | 1000 | 30 |
| Upgrade_TechnologyWildForgedBlades | PLAYER | 1000 | 30 |
| Upgrade_TechnologyWildHeavyArmor | PLAYER | 1000 | 30 |
| Upgrade_WildSpiderVenomSackScience | PLAYER | 1000 | 30 |
| Upgrade_HasDragonNestFireDrake | OBJECT | 2000 | 30 |
| Upgrade_SentryTowerFireArrows | OBJECT | 500 | 30 |
| Upgrade_TestBuilding | OBJECT | 0 | 10 |
| Upgrade_TestBuilding_2 | OBJECT | 0 | 10 |
| Upgrade_WildBasicTraining | OBJECT | 250 | 10 |
| Upgrade_WildCaveLevel2 | OBJECT | 100 | 30 |
| Upgrade_WildCaveLevel3 | OBJECT | 200 | 45 |
| Upgrade_WildFireArrows | OBJECT | 250 | 10 |
| Upgrade_WildFissureLevel2 | OBJECT | 500 | 30 |
| Upgrade_WildFissureLevel3 | OBJECT | 1000 | 60 |
| Upgrade_WildForgedBlades | OBJECT | 250 | 10 |
| Upgrade_WildFortressBatCloud | OBJECT | 500 | 30 |
| Upgrade_WildFortressDragonNest | OBJECT | 2500 | 30 |
| Upgrade_WildFortressFireArrowsTrigger | OBJECT | 1500 | 30 |
| Upgrade_WildFortressRazorSpines | OBJECT | 1000 | 30 |
| Upgrade_WildFortressWebCocoonTrigger | OBJECT | 2000 | 30 |
| Upgrade_WildHeavyArmor | OBJECT | 250 | 10 |
| Upgrade_WildSpiderPitLevel2 | OBJECT | 500 | 30 |
| Upgrade_WildSpiderPitLevel3 | OBJECT | 1000 | 60 |
| Upgrade_WildSpiderVenomSacks | OBJECT | 200 | 30 |
| Upgrade_WildTreasureTroveLevel2 | OBJECT | 500 | 30 |
| Upgrade_WildTreasureTroveLevel3 | OBJECT | 1000 | 60 |

### Special powers (25)

| Power | ReloadTime (frames) | Objects (modules) |
|---|---|---|
| SpecialAbilityAngmarCharge | 900 | WildMarauderSwordHorde (SpecialAbilityUpdate, SpecialPowerModule) |
| SpecialAbilityBatCloud | 600 | WildFortressCitadel () |
| SpecialAbilityCallFromTheDeep | 900 | WildGoblinKing (OCLSpecialPower) |
| SpecialAbilityCaptureBuilding | 0 | GoblinFighterHorde (SpecialAbilityUpdate, SpecialPowerModule); GoblinArcherHorde (SpecialAbilityUpdate, SpecialPowerModule); GoblinSpiderRiderHorde (SpecialAbilityUpdate, SpecialPowerModule); WildMarauderHorde (SpecialAbilityUpdate, SpecialPowerModule); WildMarauderSwordHorde (SpecialAbilityUpdate, SpecialPowerModule); WildGoblinKing (SpecialAbilityUpdate, SpecialPowerModule); WildAzog (SpecialAbilityUpdate, SpecialPowerModule) |
| SpecialAbilityCreateAHeroBattleRage_Level3 | 600 | WildAzog (SpecialAbilityUpdate, SpecialPowerModule) |
| SpecialAbilityDrogothFireball | 300 | Drogoth (SpecialPowerModule, WeaponFireSpecialAbilityUpdate) |
| SpecialAbilityDrogothFireflight | 450 | Drogoth (SpecialPowerModule, WeaponFireSpecialAbilityUpdate) |
| SpecialAbilityDrogothIncinerate | 900 | Drogoth (SpecialPowerModule, WeaponFireSpecialAbilityUpdate) |
| SpecialAbilityFakeLeadership | 1 | WildGoblinKing (SpecialPowerModule) |
| SpecialAbilityFakeLeadership2 | 1 | WildAzog (SpecialPowerModule) |
| SpecialAbilityFireDrakeInferno | 600 | WildBabyDrakeHorde (SpecialPowerModule, WeaponFireSpecialAbilityUpdate) |
| SpecialAbilityGoblinFighterPoisonedBlades | 300 | GoblinFighterHorde (HordeDispatchSpecialPower) |
| SpecialAbilityGoblinKingPoisonedStinger | 300 | WildGoblinKing (SpecialPowerModule, WeaponFireSpecialAbilityUpdate) |
| SpecialAbilityGrabPassenger | 0 | GoblinCaveTroll (SpecialAbilityUpdate) |
| SpecialAbilityHarvest | 5 | MordorWorker (SpecialPowerModule) |
| SpecialAbilityKingsFavor | 900 | WildAzog (LevelGrantSpecialPower, SpecialPowerModule) |
| SpecialAbilityPoisonArrows | 225 | GoblinArcherHorde (SpecialPowerModule, WeaponFireSpecialAbilityUpdate) |
| SpecialAbilityPorterExtinguishFire | 25 | WildPorter (SpecialPowerModule, WeaponFireSpecialAbilityUpdate) |
| SpecialAbilityScreech | 900 | WildShelob (SpecialAbilityUpdate, SpecialPowerModule) |
| SpecialAbilitySkullTotem | 600 | WildGoblinKing (ActivateModuleSpecialPower, SpecialPowerModule) |
| SpecialAbilityToggleMounted | 5 | WildGoblinKing (SpecialPowerModule, ToggleMountedSpecialAbilityUpdate) |
| SpecialAbilityWildMountainGiantStomp | 300 | WildMountainGiant (SpecialPowerModule, WeaponFireSpecialAbilityUpdate) |
| SpecialAbilityWildShelobPoisonedStinger | 300 | WildShelob (SpecialPowerModule, WeaponFireSpecialAbilityUpdate) |
| SpecialAbilityWildShelobTunnel | 450 | WildShelob (SpecialPowerModule, TeleportSpecialAbilityUpdate) |
| SpecialAbilityWingBlast | 450 | Drogoth (SpecialPowerModule, WeaponFireSpecialAbilityUpdate) |

### Spell book sciences (12)

- SCIENCE_AwakenWyrm
- SCIENCE_BalrogAlly
- SCIENCE_CaveBats
- SCIENCE_Darkness
- SCIENCE_DragonAlly
- SCIENCE_Scavenger
- SCIENCE_SpiderlingAllies
- SCIENCE_Taint
- SCIENCE_UntamedAllegiance
- SCIENCE_WarChant
- SCIENCE_WatcherAlly
- SCIENCE_WildMenAllies

## FactionAngmar

### Structures (8)

| Structure | BuildCost | BuildTime (s) | Command sets | Buttons |
|---|---|---|---|---|
| AngmarBarracks | 400 | 30 | AngmarBarracksCommandSet, AngmarBarracksCommandSetLevel2, AngmarBarracksCommandSetLevel3 | OBJECT_UPGRADE UpgradeAngmarBarracksLevel2; OBJECT_UPGRADE UpgradeAngmarBarracksLevel3; PLAYER_UPGRADE Upgrade_TechnologyAngmarBasicTraining; UNIT_BUILD AngmarDarkDunedainHorde; UNIT_BUILD AngmarDarkRangerHorde; UNIT_BUILD AngmarThrallMaster |
| AngmarDen | 450 | 45 | AngmarDenCommandSetLevel1, AngmarDenCommandSetLevel2, AngmarDenCommandSetLevel3 | OBJECT_UPGRADE UpgradeAngmarDenLevel2; OBJECT_UPGRADE UpgradeAngmarDenLevel3; PLAYER_UPGRADE Upgrade_TechnologyAngmarSpikedCollar; UNIT_BUILD AngmarDireWolfHorde; UNIT_BUILD AngmarHillTrollHorde; UNIT_BUILD AngmarSnowTrollHorde |
| AngmarForgeWorks | 600 | 30 | AngmarForgeWorksCommandSet, AngmarForgeWorksCommandSetLevel2, AngmarForgeWorksCommandSetLevel3 | OBJECT_UPGRADE Upgrade_AngmarForgeWorksLevel2; OBJECT_UPGRADE Upgrade_AngmarForgeWorksLevel3; PLAYER_UPGRADE Upgrade_TechnologyAngmarDarkIronArmor; PLAYER_UPGRADE Upgrade_TechnologyAngmarDarkIronBlades; PLAYER_UPGRADE Upgrade_TechnologyAngmarIceArrows; PLAYER_UPGRADE Upgrade_TechnologyAngmarIceShot; UNIT_BUILD AngmarTrollSling |
| AngmarFortress | 5000 | 120 |  |  |
| AngmarFortressCitadel | 0 | 120 | AngmarFortressCommandSet | CASTLE_UPGRADE Upgrade_AngmarFortressHouseOfLamentation; OBJECT_UPGRADE Upgrade_AngmarFortressBanners; OBJECT_UPGRADE Upgrade_AngmarFortressIceMunitionsTrigger; OBJECT_UPGRADE Upgrade_AngmarFortressIceWallsTrigger; OBJECT_UPGRADE Upgrade_AngmarFortressSanctum; OBJECT_UPGRADE Upgrade_AngmarFortressSpikes; REVIVE Command_CreateAHeroReviveSlot; REVIVE Command_GenericReviveSlot1; REVIVE Command_GenericReviveSlot2; REVIVE Command_GenericReviveSlot3; REVIVE Command_GenericReviveSlot4; REVIVE Command_GenericReviveSlot5; REVIVE Command_GenericReviveSlot6; REVIVE Command_GenericReviveSlot7; REVIVE Command_RingHeroReviveSlot; SPECIAL_POWER SpecialAbilityWitchkingSanctumFrostball; UNIT_BUILD AngmarPorter |
| AngmarHallofTwilight | 600 | 30 | AngmarHallofTwilightCommandSet, AngmarHallofTwilightCommandSetLevel2, AngmarHallofTwilightCommandSetLevel3 | OBJECT_UPGRADE Upgrade_AngmarHallofTwilightLevel2; OBJECT_UPGRADE Upgrade_AngmarHallofTwilightLevel3; PLAYER_UPGRADE Upgrade_TechnologyAngmarCorpseRain; PLAYER_UPGRADE Upgrade_TechnologyAngmarSoulFreeze; PLAYER_UPGRADE Upgrade_TechnologyAngmarWellOfSouls; UNIT_BUILD AngmarNecromancerHorde |
| AngmarMill | 300 | 15 | SellableCommandSet |  |
| AngmarSentryTower | 500 | 60 | AngmarBattleTowerCommandSet | EXIT_CONTAINER Command_ExitGarrison; OBJECT_UPGRADE Upgrade_AngmarTowerIceArrow |
| AngmarWallHubSmall | 200 | 30 | AngmarCastleWallHubSmallCommandSet | CANCEL_NEIGHBORHOOD Command_CancelWallSpan; DOZER_CONSTRUCT AngmarWallHubSmall |

### Units and hordes (9)

| Unit | BuildCost | BuildTime (s) | CommandPoints | Abilities and upgrades (buttons) |
|---|---|---|---|---|
| AngmarDarkDunedainHorde | 400 | 30 | 80 | OBJECT_UPGRADE Upgrade_AngmarBasicTraining; OBJECT_UPGRADE Upgrade_AngmarDarkIronArmor; OBJECT_UPGRADE Upgrade_AngmarDarkIronBlades; SPECIAL_POWER SpecialAbilityAngmarCharge; SPECIAL_POWER SpecialAbilityCaptureBuilding; SPECIAL_POWER SpecialAbilityFakeLeadership |
| AngmarDarkRangerHorde | 500 | 40 | 72 | FIRE_WEAPON Command_ArcherBombard; OBJECT_UPGRADE Upgrade_AngmarBasicTraining; OBJECT_UPGRADE Upgrade_AngmarIceArrows; SPECIAL_POWER SpecialAbilityCaptureBuilding; SPECIAL_POWER SpecialAbilityFakeLeadership; SPECIAL_POWER SpecialAbilityMenLongShot |
| AngmarDireWolfHorde | 250 | 30 | 40 | OBJECT_UPGRADE Upgrade_AngmarBasicTraining; OBJECT_UPGRADE Upgrade_AngmarSpikedCollar |
| AngmarHillTrollHorde | 600 | 40 | 80 | HORDE_TOGGLE_FORMATION Command_AngmarPorcupineFormation; OBJECT_UPGRADE Upgrade_AngmarBasicTraining; OBJECT_UPGRADE Upgrade_AngmarDarkIronArmor; OBJECT_UPGRADE Upgrade_AngmarDarkIronBlades; SPECIAL_POWER SpecialAbilityCaptureBuilding |
| AngmarNecromancerHorde | 500 | 30 | 40 | OBJECT_UPGRADE Upgrade_AngmarCorpseRain; OBJECT_UPGRADE Upgrade_AngmarSoulFreeze; OBJECT_UPGRADE Upgrade_AngmarWellOfSouls; SPECIAL_POWER SpecialAbilityCaptureBuilding; SPECIAL_POWER SpecialAbilityFakeLeadership; SPECIAL_POWER SpecialAbilityNecroBlackIce; SPECIAL_POWER SpecialAbilityNecroCorpseRain; SPECIAL_POWER SpecialAbilityNecroFellStrength; SPECIAL_POWER SpecialAbilityNecroSoulFreeze; SPECIAL_POWER SpecialAbilityNecroWellOfSouls |
| AngmarPorter | 500 | 30 | 0 | DOZER_CONSTRUCT AngmarBarracks; DOZER_CONSTRUCT AngmarDen; DOZER_CONSTRUCT AngmarForgeWorks; DOZER_CONSTRUCT AngmarFortress; DOZER_CONSTRUCT AngmarHallofTwilight; DOZER_CONSTRUCT AngmarMill; DOZER_CONSTRUCT AngmarSentryTower; DOZER_CONSTRUCT AngmarWallHubSmall; SPECIAL_POWER SpecialAbilityPorterExtinguishFire |
| AngmarSnowTrollHorde | 750 | 40 | 78 | OBJECT_UPGRADE Upgrade_AngmarBasicTraining; OBJECT_UPGRADE Upgrade_AngmarDarkIronArmor; OBJECT_UPGRADE Upgrade_AngmarDarkIronBlades; SPECIAL_POWER SpecialAbilityAngmarCharge; SPECIAL_POWER SpecialAbilityCaptureBuilding |
| AngmarThrallMaster | 200 | 20 | 40 | OBJECT_UPGRADE Upgrade_ThrallMasterOrcWarriors; OBJECT_UPGRADE Upgrade_ThrallMasterRhudaurSlingers; OBJECT_UPGRADE Upgrade_ThrallMasterRhudaurSpearmen; OBJECT_UPGRADE Upgrade_ThrallMasterWolfRiders; SPECIAL_POWER SpecialAbilityCaptureBuilding |
| AngmarTrollSling | 600 | 40 | 25 | FIRE_WEAPON Command_BombardEvil; OBJECT_UPGRADE Upgrade_AngmarIceShot; TOGGLE_NO_AUTO_ACQUIRE Command_ToggleCatAutoAcquire |

### Heroes (5)

| Unit | BuildCost | BuildTime (s) | CommandPoints | Abilities and upgrades (buttons) |
|---|---|---|---|---|
| AngmarHwaldar | 1200 | 30 | 50 | SPECIAL_POWER SpecialAbilityCaptureBuilding; SPECIAL_POWER SpecialAbilityFakeLeadership; SPECIAL_POWER SpecialAbilityFakeLeadership2; SPECIAL_POWER SpecialAbilityHwaldarSummonHillmen1; SPECIAL_POWER SpecialAbilityHwaldarSummonHillmen2 |
| AngmarKarsh | 2000 | 45 | 50 | FIRE_WEAPON Command_ChillSoul; SPECIAL_POWER SpecialAbilityCaptureBuilding; SPECIAL_POWER SpecialAbilityKarshBlink; SPECIAL_POWER SpecialAbilityWhisperofDeath |
| AngmarMorgramir | 1500 | 40 | 25 | SPECIAL_POWER SpecialAbilityAngmarMorgramirRuin; SPECIAL_POWER SpecialAbilityBlackRiderMorgulBlade; SPECIAL_POWER SpecialAbilityCaptureBuilding; SPECIAL_POWER SpecialAbilityDarkGlory; SPECIAL_POWER SpecialAbilityFakeLeadership |
| AngmarRogash | 3000 | 45 | 75 | SPECIAL_POWER SpecialAbilityCaptureBuilding; SPECIAL_POWER SpecialAbilityRageoftheNorth; SPECIAL_POWER SpecialAbilityRogashDominateTroll; SPECIAL_POWER SpecialAbilityRogashLeap |
| AngmarWitchking | 4000 | 60 | 75 | SPECIAL_POWER SpecialAbilityCaptureBuilding; SPECIAL_POWER SpecialAbilityCurseEnemy; SPECIAL_POWER SpecialAbilityFakeLeadership; SPECIAL_POWER SpecialAbilityScreech; SPECIAL_POWER SpecialAbilityToggleMounted; SPECIAL_POWER SpecialAbilityWitchkingMorgulBlade |

### Upgrades (37)

| Upgrade | Type | BuildCost | BuildTime (s) |
|---|---|---|---|
| Upgrade_TechnologyAngmarBasicTraining | PLAYER | 1000 | 60 |
| Upgrade_TechnologyAngmarCorpseRain | PLAYER | 0 | 0 |
| Upgrade_TechnologyAngmarDarkIronArmor | PLAYER | 1000 | 30 |
| Upgrade_TechnologyAngmarDarkIronBlades | PLAYER | 1000 | 30 |
| Upgrade_TechnologyAngmarIceArrows | PLAYER | 1000 | 30 |
| Upgrade_TechnologyAngmarIceShot | PLAYER | 1000 | 60 |
| Upgrade_TechnologyAngmarSoulFreeze | PLAYER | 0 | 0 |
| Upgrade_TechnologyAngmarSpikedCollar | PLAYER | 500 | 30 |
| Upgrade_TechnologyAngmarWellOfSouls | PLAYER | 0 | 0 |
| UpgradeAngmarBarracksLevel2 | OBJECT | 300 | 30 |
| UpgradeAngmarBarracksLevel3 | OBJECT | 500 | 60 |
| UpgradeAngmarDenLevel2 | OBJECT | 300 | 30 |
| UpgradeAngmarDenLevel3 | OBJECT | 500 | 60 |
| Upgrade_AngmarBasicTraining | OBJECT | 300 | 10 |
| Upgrade_AngmarCorpseRain | OBJECT | 500 | 10 |
| Upgrade_AngmarDarkIronArmor | OBJECT | 300 | 10 |
| Upgrade_AngmarDarkIronBlades | OBJECT | 300 | 10 |
| Upgrade_AngmarForgeWorksLevel2 | OBJECT | 500 | 45 |
| Upgrade_AngmarForgeWorksLevel3 | OBJECT | 500 | 60 |
| Upgrade_AngmarFortressBanners | OBJECT | 500 | 5 |
| Upgrade_AngmarFortressHouseOfLamentation | OBJECT | 1000 | 30 |
| Upgrade_AngmarFortressIceMunitionsTrigger | OBJECT | 1500 | 30 |
| Upgrade_AngmarFortressIceWallsTrigger | OBJECT | 2000 | 30 |
| Upgrade_AngmarFortressSanctum | OBJECT | 2500 | 30 |
| Upgrade_AngmarFortressSpikes | OBJECT | 1000 | 30 |
| Upgrade_AngmarHallofTwilightLevel2 | OBJECT | 500 | 30 |
| Upgrade_AngmarHallofTwilightLevel3 | OBJECT | 500 | 60 |
| Upgrade_AngmarIceArrows | OBJECT | 300 | 10 |
| Upgrade_AngmarIceShot | OBJECT | 300 | 10 |
| Upgrade_AngmarSoulFreeze | OBJECT | 300 | 10 |
| Upgrade_AngmarSpikedCollar | OBJECT | 300 | 10 |
| Upgrade_AngmarTowerIceArrow | OBJECT | 500 | 30 |
| Upgrade_AngmarWellOfSouls | OBJECT | 400 | 10 |
| Upgrade_ThrallMasterOrcWarriors | OBJECT | 0 | 1 |
| Upgrade_ThrallMasterRhudaurSlingers | OBJECT | 0 | 1 |
| Upgrade_ThrallMasterRhudaurSpearmen | OBJECT | 100 | 1 |
| Upgrade_ThrallMasterWolfRiders | OBJECT | 350 | 1 |

### Special powers (26)

| Power | ReloadTime (frames) | Objects (modules) |
|---|---|---|
| SpecialAbilityAngmarCharge | 900 | AngmarDarkDunedainHorde (SpecialAbilityUpdate, SpecialPowerModule); AngmarSnowTrollHorde (SpecialAbilityUpdate, SpecialPowerModule) |
| SpecialAbilityAngmarMorgramirRuin | 1000 | AngmarMorgramir (SpecialPowerModule, WeaponFireSpecialAbilityUpdate) |
| SpecialAbilityBlackRiderMorgulBlade | 225 | AngmarMorgramir (SpecialPowerModule, WeaponFireSpecialAbilityUpdate) |
| SpecialAbilityCaptureBuilding | 0 | AngmarThrallMaster (SpecialAbilityUpdate, SpecialPowerModule); AngmarDarkDunedainHorde (SpecialAbilityUpdate, SpecialPowerModule); AngmarDarkRangerHorde (SpecialAbilityUpdate, SpecialPowerModule); AngmarHillTrollHorde (SpecialAbilityUpdate, SpecialPowerModule); AngmarSnowTrollHorde (SpecialAbilityUpdate, SpecialPowerModule); AngmarNecromancerHorde (SpecialAbilityUpdate, SpecialPowerModule); AngmarHwaldar (SpecialAbilityUpdate, SpecialPowerModule); AngmarKarsh (SpecialAbilityUpdate, SpecialPowerModule); AngmarMorgramir (SpecialAbilityUpdate, SpecialPowerModule); AngmarRogash (SpecialAbilityUpdate, SpecialPowerModule); AngmarWitchking (SpecialAbilityUpdate, SpecialPowerModule) |
| SpecialAbilityCurseEnemy | 1500 | AngmarWitchking (CurseSpecialPower, SpecialPowerModule) |
| SpecialAbilityDarkGlory | 1200 | AngmarMorgramir (SpecialPowerModule) |
| SpecialAbilityFakeLeadership | 1 | AngmarDarkDunedainHorde (SpecialPowerModule); AngmarDarkRangerHorde (SpecialPowerModule); AngmarNecromancerHorde (); AngmarHwaldar (SpecialPowerModule); AngmarMorgramir (SpecialPowerModule); AngmarWitchking (SpecialPowerModule) |
| SpecialAbilityFakeLeadership2 | 1 | AngmarHwaldar (SpecialPowerModule) |
| SpecialAbilityHwaldarSummonHillmen1 | 900 | AngmarHwaldar (OCLSpecialPower) |
| SpecialAbilityHwaldarSummonHillmen2 | 900 | AngmarHwaldar (OCLSpecialPower) |
| SpecialAbilityKarshBlink | 900 | AngmarKarsh (SpecialPowerModule, TeleportSpecialAbilityUpdate) |
| SpecialAbilityMenLongShot | 1200 | AngmarDarkRangerHorde (OCLSpecialPower, WeaponFireSpecialAbilityUpdate) |
| SpecialAbilityNecroBlackIce | 0 | AngmarNecromancerHorde (ActivateModuleSpecialPower, SpecialPowerModule) |
| SpecialAbilityNecroCorpseRain | 1000 | AngmarNecromancerHorde (SpecialPowerModule, WeaponFireSpecialAbilityUpdate) |
| SpecialAbilityNecroFellStrength | 0 | AngmarNecromancerHorde (ActivateModuleSpecialPower, SpecialPowerModule) |
| SpecialAbilityNecroSoulFreeze | 0 | AngmarNecromancerHorde (ActivateModuleSpecialPower, SpecialPowerModule) |
| SpecialAbilityNecroWellOfSouls | 0 | AngmarNecromancerHorde (ActivateModuleSpecialPower, SpecialPowerModule) |
| SpecialAbilityPorterExtinguishFire | 25 | AngmarPorter (SpecialPowerModule, WeaponFireSpecialAbilityUpdate) |
| SpecialAbilityRageoftheNorth | 900 | AngmarRogash (HeroModeSpecialAbilityUpdate, SpecialPowerModule) |
| SpecialAbilityRogashDominateTroll | 300 | AngmarRogash (DominateEnemySpecialPower, SpecialPowerModule) |
| SpecialAbilityRogashLeap | 450 | AngmarRogash (SpecialPowerModule, WeaponFireSpecialAbilityUpdate) |
| SpecialAbilityScreech | 900 | AngmarWitchking (SpecialAbilityUpdate, SpecialPowerModule) |
| SpecialAbilityToggleMounted | 5 | AngmarWitchking (SpecialPowerModule, ToggleMountedSpecialAbilityUpdate) |
| SpecialAbilityWhisperofDeath | 225 | AngmarKarsh (SpecialPowerModule, WeaponFireSpecialAbilityUpdate) |
| SpecialAbilityWitchkingMorgulBlade | 225 | AngmarWitchking (SpecialPowerModule, WeaponFireSpecialAbilityUpdate) |
| SpecialAbilityWitchkingSanctumFrostball | 1500 | AngmarFortressCitadel (SpecialPowerModule, WeaponFireSpecialAbilityUpdate) |

### Spell book sciences (12)

- SCIENCE_Avalanche
- SCIENCE_Blight
- SCIENCE_ChillWind
- SCIENCE_FreezingRain
- SCIENCE_FrozenLand
- SCIENCE_Snowbind
- SCIENCE_SummonGiants
- SCIENCE_SummonOrcs
- SCIENCE_SummonShadeOfWolf
- SCIENCE_SummonWights
- SCIENCE_UntamedAllegiance
- SCIENCE_WarChant
