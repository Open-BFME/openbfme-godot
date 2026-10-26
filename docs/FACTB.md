# FACT-B: Isengard and Mordor, item by item

Lane FACT-B checks every structure, unit, horde, hero, upgrade and ability of Isengard and Mordor through the in-game HUD, the way a
player reaches them: clicks on objects and on the ground, and presses of the control bar's own buttons (the button pages opened with
their own buttons). The tests are `engine/tests/test_hud_factb_*.cpp` (they SKIP without the games). The inventory below is generated
from the retail data by `factb inventory` (`FACTB_INVENTORY_OUT`); it is a reachability inventory, not proof that every listed module works.
These tests require both `ROTWK_INSTALL` and `BFME2_INSTALL` for retail-dependent runs.
The numeric results and generated tables below are the 2026-10-11 snapshot, not a fresh run.

## Results (2026-10-11)

| sweep | Isengard | Mordor | what is checked |
|---|---|---|---|
| inventory (`factb inventory`) | 19 structures, 21 units / hordes, 5 heroes, 29 upgrades, 40 powers | 16, 23, 6, 28, 33 | walked from the PlayerTemplate and the objects a started skirmish's base holds (citadel, pads, walls) |
| build (`factb build`) | 11 / 11 | 9 / 9 | the builder's button, a legal site clicked, BuildCost, calcTimeToBuild frames (RW 0x73C39E), the CommandSet |
| expansions (`factb found`) | 4 / 4 | 3 / 3 | every pad button (towers, siege expansions, the wall hub expansion): price, construction frames |
| structures (`factb structures`) | 36 / 37 | 36 / 39 | every UNIT_BUILD and upgrade button of every structure (the citadels' improvements too), levelled through its own buttons: price, frames, exit point, command points, a click moves the unit; upgrades land and run their modules |
| fortress (`factb fortress`) | 5 / 5 | 4 / 4 | in a started skirmish's castle, through the HUD: Isengard builds a wall span from a wall hub's Begin Wall Span button (8 pieces), then the keep's buttons buy Iron Plating / Morgul Sorcery (the tower / spire button is not enabled before), CastleUpgrade (RW 0x8BAAC0) hands the upgrade to the keep and to every wall built, the Wizard's Tower / Gorgoroth Spire is bought and its power cast at an enemy horde: the weapon's hits |
| owner (`factb owner`) | pass | pass | a building given to the player (Object::setTeam, RW 0x699658) takes its faction command set; a science reset does not give back a removed InitialUpgrade |
| units (`factb units`) | 41 / 67 | 38 / 74 | every ability, upgrade and toggle button at level 10, cast at a target its filters take (Dominate Troll at a troll, Tame the Beast at a warg, a curse at a hero): the result in the world, per module class: an owner change (dominate, capture), a hit of the special weapon's size by the caster (Weapon.ini x Armor.ini, above its hits before the cast), the ModifierList in a victim's pool, experience or a rank, a terrified enemy (screech), a restarted enemy recharge (curse), objects made, the weapon set flags or mode; a class with none of these fails; horde upgrades on every member they affect |
| heroes (`factb heroes`) | 5 / 5 | 6 / 6 | recruited through the citadel's REVIVE buttons, killed, revived; the ring hero through its slot (the ring upgrades taken, the ring dropped, recruited again) |
| fights (`factb fights`) | 16 / 16 | 17 / 20 | a click on an enemy: it loses health |
| damage (`factb damage`) | 13 / 14 | 14 / 17 | the first hit on an enemy hero equals the weapon's Damage x the hero's armour coefficient x the nugget's DamageScalar (k members' hits for a horde) |
| spell book (`factb spell book`) | 7 / 12 | 9 / 12 | each power cast through the HUD's spell bar, its effect beyond a control run of the same scene |

The units' failures include the stance button (S-289, on units with that button) and the stops below; every failing item is pinned in the tests
with its stop (an item that starts to work fails its test until the pin moves). Round 3 replaced the units' generic "effect step ran"
counter with the results above: it had passed abilities whose effect never happened. Sauron's Fires of Doom (its special weapon holds
only a WeaponOCLNugget) and Sharku's Man-Eater (a GrabNugget) now fail under S-180, those nuggets' delivery is not ported; Saruman's
Lightning Bolt deals its damage, but its weapon's WeaponOCLNugget makes nothing (S-180 too); the Haradrim archers' Barbed Arrow joined S-1997. Round 4: the units and fortress sweeps run with the idle mood scan off (nothing attacks unless ordered) and their victims at twenty times their health, so a damage result is the caster's first hit after the cast, of the special weapon's size, with none before it; a spawn result is an object of the templates the power's OCL or SpecialObject names. With that the Black Riders' Morgul Blade and the Witch-king's curse work.

Fixed in this lane: the upgrade buttons' availability (RW 0x942733) and press-time affordability (RW 0x940435), CONTEXTMODE special powers
that sent a move order, the battering ram a click could not select (Object::isSelectable RW 0x68DE58), the Berserker's Light Torch
(WeaponChangeSpecialPowerModule RW 0x8C8420), the fortress improvements (CastleUpgrade RW 0x8BAAC0), the players' InitialUpgrades
(RW 0x6AFC92: the ring hero's upgrades are taken back when it is built), a building's upgrade modules run again for its new owner (Object::setTeam RW 0x699658, FACT-C's finding). At the time of this snapshot the stops were: S-1990 (FIRE_WEAPON: Bombard, mine Deploy,
Mumakil Charge), S-1991 (Bloodthirsty sacrifice), S-1992 (auto-acquire toggle), S-1993 (the lumber mills earn nothing), S-1994 (the fell
beasts), S-1995 (the unported module classes listed below, among them the Lava Moat's ObjectCreationUpgrade), S-1996 (a Berserker against a
hero), S-1997 (ability and spell effects not seen: Extinguish Fire, Taint, Industry, Fuel the Fires, Dragon Strike, Untamed
Allegiance), S-921 (Devastation, SPELL-3), S-289 (the stance button's radial menu). The catapult made at the siege works barely moves away
from the door when ordered (MOVE-3's exit, reported).

Since that snapshot, `FIRE_WEAPON` dispatch exists; the Isengard Ballista's
`Command_BombardEvil` remains pinned at S-1990. Bloodthirsty (S-1991), the
auto-acquire toggle (S-1992), lumber harvesting (S-1993), the fell beasts
(S-1994), and the Berserker's first hit on a hero (S-1996) have implementations
and their old failure pins are gone. The button sweep does not assert a
harvesting effect: `engine/tests/test_lumber1.cpp` covers it separately.
Both factions' first-hit damage sweeps now allow no failing item.
`WeaponOCLNugget` and `GrabNugget` delivery also exist now; S-180 still pins
Saruman's missing OCL result and Sharku's Man-Eater result in the button sweep,
not the absence of all delivery for either nugget class.
The historical unported-module tables below still record the snapshot;
`BloodthirstyUpdate`, `ObjectCreationUpgrade`, `SupplyCenterCreate`,
`SupplyCenterDockUpdate`, `DoCommandUpgrade` and `TransitionDamageFX` now have runtimes.

## FactionIsengard

19 structures, 15 units, 6 hordes, 5 heroes, 29 upgrades, 40 powers, 12 sciences, 29 other commands; 50 command sets.

### structure

| name | data | reached from |
|---|---|---|
| IsengardArmory | cost 1000, time 60s, CP 0; upgrade modules: CommandSetUpgrade<-Upgrade_IsengardArmoryLevel2 CommandSetUpgrade<-Upgrade_IsengardArmoryLevel3 LevelUpUpgrade<-Upgrade_IsengardArmoryLevel2 LevelUpUpgrade<-Upgrade_IsengardArmoryLevel3 SubObjectsUpgrade<-Upgrade_IsengardArmoryLevel2 SubObjectsUpgrade<-Upgrade_IsengardArmoryLevel3 SubObjectsUpgrade<-Upgrade_StructureLevel1 WeaponSetUpgrade<-Upgrade_IsengardArmoryLevel3 | IsengardPorterCommandSet:11:Command_PorterConstructIsengardArmory |
| IsengardBallistaExpansion | cost 1200, time 30s, CP 0; upgrade modules: AttributeModifierUpgrade<-Upgrade_IsengardFortressIronPlating CommandSetUpgrade<-Upgrade_HasWallTrebuchet|Upgrade_TrebuchetTurret CommandSetUpgrade<-Upgrade_TrebuchetTurret ModelConditionUpgrade<-Upgrade_IsengardFortressIronPlating WeaponSetUpgrade<-Upgrade_EvilFortressFlamingMunitions | IsengardFortressExpansionPadCornerCommandSet:2:Command_ConstructIsengardBallistaExpansion |
| IsengardBattleTower | cost 500, time 60s, CP 0; upgrade modules: WeaponSetUpgrade<-Upgrade_SentryTowerFireArrows | IsengardPorterCommandSet:9:Command_PorterConstructIsengardBattleTower |
| IsengardCastleWallHub | cost 150, time 20s, CP 0; upgrade modules: AttributeModifierUpgrade<-Upgrade_IsengardFortressIronPlating ModelConditionUpgrade<-Upgrade_IsengardFortressIronPlating | IsengardCastleWallHubCommandSet:1:Command_IsengardCastleWallHub_BeginWallSpan<br>IsengardPorterCommandSet:12:Command_PorterConstructIsengardWallHub |
| IsengardCastleWallHubExpansion | cost 150, time 20s, CP 0; upgrade modules: AttributeModifierUpgrade<-Upgrade_IsengardFortressIronPlating ModelConditionUpgrade<-Upgrade_IsengardFortressIronPlating | IsengardFortressExpansionPadCornerCommandSet:4:Command_ConstructIsengardWallHubExpansion |
| IsengardFortress | cost 5000, time 120s, CP 0 | FactionIsengard:StartingBuilding<br>IsengardPorterCommandSet:13:Command_PorterConstructIsengardFortress<br>the started skirmish's base |
| IsengardFortressCitadel | cost 5000, time 120s, CP 0; powers: SpecialPowerModule(SpecialAbilityLightningStrike); upgrade modules: AttributeModifierUpgrade<-Upgrade_IsengardFortressIronPlating AttributeModifierUpgrade<-Upgrade_IsengardFortressMurderOfCrows CastleUpgrade<-Upgrade_IsengardFortressIronPlatingTrigger CastleUpgrade<-Upgrade_IsengardFortressOrcfireMunitionsTrigger CostModifierUpgrade<-Upgrade_IsengardFortressBurningForges CostModifierUpgrade<-Upgrade_IsengardFortressExcavations ModelConditionUpgrade<-Upgrade_EvilFortressFlamingMunitions ModelConditionUpgrade<-Upgrade_IsengardFortressBurningForges ModelConditionUpgrade<-Upgrade_IsengardFortressExcavations ModelConditionUpgrade<-Upgrade_IsengardFortressIronPlating ModelConditionUpgrade<-Upgrade_IsengardFortressMurderOfCrows ModelConditionUpgrade<-Upgrade_IsengardFortressWizardsTower ModelConditionUpgrade<-Upgrade_RingHero|Upgrade_FortressRingHero StatusBitsUpgrade<-Upgrade_EvilFortressFlamingMunitions UnpauseSpecialPowerUpgrade<-Upgrade_IsengardFortressMurderOfCrows UnpauseSpecialPowerUpgrade<-Upgrade_IsengardFortressWizardsTower | the started skirmish's base |
| IsengardFortressExpansionPadCorner | cost 1, time 5s, CP 0 | the started skirmish's base |
| IsengardFurnace | cost 300, time 15s, CP 0; upgrade modules: SubObjectsUpgrade<-Upgrade_StructureLevel1 SubObjectsUpgrade<-Upgrade_StructureLevel2 SubObjectsUpgrade<-Upgrade_StructureLevel3 WeaponSetUpgrade<-Upgrade_StructureLevel3 | IsengardPorterCommandSet:3:Command_PorterConstructIsengardFurnace |
| IsengardLumberMill | cost 350, time 30s, CP 0; upgrade modules: SpawnBehavior<- SubObjectsUpgrade<-Upgrade_StructureLevel1 SubObjectsUpgrade<-Upgrade_StructureLevel2 SubObjectsUpgrade<-Upgrade_StructureLevel3 WeaponSetUpgrade<-Upgrade_StructureLevel3 | IsengardPorterCommandSet:4:Command_PorterConstructIsengardLumberMill |
| IsengardMineLauncherExpansion | cost 1500, time 30s, CP 0; upgrade modules: AttributeModifierUpgrade<-Upgrade_IsengardFortressIronPlating ModelConditionUpgrade<-Upgrade_IsengardFortressIronPlating | IsengardFortressExpansionPadCornerCommandSet:3:Command_ConstructIsengardMineLauncherExpansion |
| IsengardSiegeWorks | cost 600, time 30s, CP 0; upgrade modules: CommandSetUpgrade<-Upgrade_StructureLevel2 CommandSetUpgrade<-Upgrade_StructureLevel3 LevelUpUpgrade<-Upgrade_IsengardSiegeWorksLevel2 LevelUpUpgrade<-Upgrade_IsengardSiegeWorksLevel3 SubObjectsUpgrade<-Upgrade_StructureLevel1 SubObjectsUpgrade<-Upgrade_StructureLevel2 SubObjectsUpgrade<-Upgrade_StructureLevel3 WeaponSetUpgrade<-Upgrade_StructureLevel3 | IsengardPorterCommandSet:8:Command_PorterConstructIsengardSiegeWorks |
| IsengardTavern | cost 600, time 45s, CP 0; upgrade modules: CommandSetUpgrade<-Upgrade_IsengardTavernLevel2 CommandSetUpgrade<-Upgrade_IsengardTavernLevel3 LevelUpUpgrade<-Upgrade_IsengardTavernLevel2 LevelUpUpgrade<-Upgrade_IsengardTavernLevel3 SubObjectsUpgrade<-Upgrade_IsengardTavernLevel2 SubObjectsUpgrade<-Upgrade_IsengardTavernLevel3 SubObjectsUpgrade<-Upgrade_StructureLevel1 WeaponSetUpgrade<-Upgrade_IsengardTavernLevel2 | IsengardPorterCommandSet:5:Command_ConstructIsengardTavern |
| IsengardTowerExpansion | cost 600, time 30s, CP 0; upgrade modules: AttributeModifierUpgrade<-Upgrade_IsengardFortressIronPlating ModelConditionUpgrade<-Upgrade_IsengardFortressIronPlating WeaponSetUpgrade<-Upgrade_EvilFortressFlamingMunitions | IsengardFortressExpansionPadCornerCommandSet:1:Command_ConstructIsengardTowerExpansion |
| IsengardUrukPit | cost 350, time 25s, CP 0; upgrade modules: CommandSetUpgrade<-Upgrade_IsengardUrukPitLevel2 CommandSetUpgrade<-Upgrade_IsengardUrukPitLevel3 LevelUpUpgrade<-Upgrade_IsengardUrukPitLevel2 LevelUpUpgrade<-Upgrade_IsengardUrukPitLevel3 SubObjectsUpgrade<-Upgrade_IsengardUrukPitLevel2 SubObjectsUpgrade<-Upgrade_IsengardUrukPitLevel3 SubObjectsUpgrade<-Upgrade_StructureLevel1 | IsengardPorterCommandSet:6:Command_PorterConstructIsengardUrukPit |
| IsengardWargPit | cost 500, time 30s, CP 0; upgrade modules: CommandSetUpgrade<-Upgrade_IsengardWargPitLevel2 CommandSetUpgrade<-Upgrade_IsengardWargPitLevel3 LevelUpUpgrade<-Upgrade_IsengardWargPitLevel2 LevelUpUpgrade<-Upgrade_IsengardWargPitLevel3 SubObjectsUpgrade<-Upgrade_IsengardWargPitLevel2 SubObjectsUpgrade<-Upgrade_IsengardWargPitLevel3 SubObjectsUpgrade<-Upgrade_StructureLevel1 | IsengardPorterCommandSet:7:Command_PorterConstructIsengardWargPit |
| IsengardWargSentry | cost 400, time 15s, CP 0; upgrade modules: SpawnBehavior<- | IsengardPorterCommandSet:10:Command_PorterConstructIsengardWargSentry |
| MordorBase | cost 0, time 5s, CP 0 | MordorWorkerCommandSet:3:Command_ConstructMordorBaseTest |
| MordorBase_DEMO | cost 500, time 5s, CP 0 | MordorWorkerCommandSet:2:Command_ConstructMordorBase |

### horde

| name | data | reached from |
|---|---|---|
| IsengardBerserkerHorde | cost 1000, time 45s, CP 75; powers: SpecialPowerModule(SpecialAbilityCaptureBuilding); upgrade modules: AutoHealBehavior<-Upgrade_MiniHordeLvl3 UnpauseSpecialPowerUpgrade<-Upgrade_MiniHordeLvl2 UnpauseSpecialPowerUpgrade<-Upgrade_MiniHordeLvl7 | IsengardUrukPitCommandSetLevel1:5:Command_ConstructIsengardBerserkerHorde<br>IsengardUrukPitCommandSetLevel2:5:Command_ConstructIsengardBerserkerHorde<br>IsengardUrukPitCommandSetLevel3:5:Command_ConstructIsengardBerserkerHorde |
| IsengardFighterHorde | cost 400, time 30s, CP 75; powers: SpecialPowerModule(SpecialAbilityCaptureBuilding); upgrade modules: LevelUpUpgrade<-Upgrade_IsengardBasicTraining StatusBitsUpgrade<-Upgrade_IsengardForgedBlades StatusBitsUpgrade<-Upgrade_IsengardHeavyArmor | IsengardUrukPitCommandSetLevel1:1:Command_ConstructIsengardFighterHorde<br>IsengardUrukPitCommandSetLevel2:1:Command_ConstructIsengardFighterHorde<br>IsengardUrukPitCommandSetLevel3:1:Command_ConstructIsengardFighterHorde |
| IsengardPikemanHorde | cost 400, time 30s, CP 60; powers: SpecialPowerModule(SpecialAbilityCaptureBuilding); upgrade modules: LevelUpUpgrade<-Upgrade_IsengardBasicTraining StatusBitsUpgrade<-Upgrade_IsengardForgedBlades StatusBitsUpgrade<-Upgrade_IsengardHeavyArmor | IsengardUrukPitCommandSetLevel1:3:Command_ConstructIsengardPikemanHorde<br>IsengardUrukPitCommandSetLevel2:3:Command_ConstructIsengardPikemanHorde<br>IsengardUrukPitCommandSetLevel3:3:Command_ConstructIsengardPikemanHorde |
| IsengardUrukCrossbowHorde | cost 300, time 30s, CP 60; powers: SpecialPowerModule(SpecialAbilityCaptureBuilding); upgrade modules: LevelUpUpgrade<-Upgrade_IsengardBasicTraining StatusBitsUpgrade<-Upgrade_IsengardFireArrows StatusBitsUpgrade<-Upgrade_IsengardHeavyArmor | IsengardUrukPitCommandSetLevel1:2:Command_ConstructIsengardUrukCrossbowHorde<br>IsengardUrukPitCommandSetLevel2:2:Command_ConstructIsengardUrukCrossbowHorde<br>IsengardUrukPitCommandSetLevel3:2:Command_ConstructIsengardUrukCrossbowHorde |
| IsengardWildmanAxeHorde | cost 250, time 20s, CP 48; powers: SpecialPowerModule(SpecialAbilityCaptureBuilding); upgrade modules: LevelUpUpgrade<-Upgrade_IsengardBasicTraining StatusBitsUpgrade<-Upgrade_DwarvenFireArrows StatusBitsUpgrade<-Upgrade_IsengardHeavyArmor | IsengardTavernCommandSetLevel1:2:Command_ConstructIsengardWildmanAxeHorde<br>IsengardTavernCommandSetLevel2:2:Command_ConstructIsengardWildmanAxeHorde<br>IsengardTavernCommandSetLevel3:2:Command_ConstructIsengardWildmanAxeHorde |
| IsengardWildmanHorde | cost 150, time 20s, CP 48; powers: SpecialPowerModule(SpecialAbilityCaptureBuilding) SpecialPowerModule(SpecialAbilityWildmanPillage); upgrade modules: LevelUpUpgrade<-Upgrade_IsengardBasicTraining StatusBitsUpgrade<-Upgrade_IsengardHeavyArmor StatusBitsUpgrade<-Upgrade_IsengardTorches | IsengardTavernCommandSetLevel1:1:Command_ConstructIsengardWildmanHorde<br>IsengardTavernCommandSetLevel2:1:Command_ConstructIsengardWildmanHorde<br>IsengardTavernCommandSetLevel3:1:Command_ConstructIsengardWildmanHorde |

### unit

| name | data | reached from |
|---|---|---|
| IsengardBallista | cost 300, time 40s, CP 25 | IsengardSiegeWorksCommandSetLevel1:2:Command_ConstructIsengardBallista<br>IsengardSiegeWorksCommandSetLevel2:2:Command_ConstructIsengardBallista<br>IsengardSiegeWorksCommandSetLevel3:2:Command_ConstructIsengardBallista |
| IsengardBatteringRam | cost 400, time 30s, CP 25 | IsengardSiegeWorksCommandSetLevel1:1:Command_ConstructIsengardBatteringRam<br>IsengardSiegeWorksCommandSetLevel2:1:Command_ConstructIsengardBatteringRam<br>IsengardSiegeWorksCommandSetLevel3:1:Command_ConstructIsengardBatteringRam |
| IsengardBeserker | cost 300, time 25s, CP 25; powers: SpecialPowerModule(SpecialAbilityCaptureBuilding) WeaponChangeSpecialPowerModule(SpecialAbilityBerserkerLightTorch) | IsengardUrukPitCommandSetLevel1:4:Command_ConstructIsengardBerserker<br>IsengardUrukPitCommandSetLevel2:4:Command_ConstructIsengardBerserker<br>IsengardUrukPitCommandSetLevel3:4:Command_ConstructIsengardBerserker |
| IsengardExplosiveMine | cost 500, time 45s, CP 10 | IsengardSiegeWorksCommandSetLevel1:3:Command_ConstructIsengardExplosiveMine<br>IsengardSiegeWorksCommandSetLevel2:3:Command_ConstructIsengardExplosiveMine<br>IsengardSiegeWorksCommandSetLevel3:3:Command_ConstructIsengardExplosiveMine |
| IsengardFanatic | cost 0, time 1s, CP 25; upgrade modules: AttributeModifierUpgrade<-Upgrade_MiniHordeLvl2 UnpauseSpecialPowerUpgrade<-Upgrade_MiniHordeLvl2 UnpauseSpecialPowerUpgrade<-Upgrade_MiniHordeLvl7 | IsengardBerserkerHorde:InitialPayload |
| IsengardFighter | cost 0, time 1s, CP 5; upgrade modules: ArmorUpgrade<-Upgrade_IsengardHeavyArmor SubObjectsUpgrade<-Upgrade_IsengardForgedBlades SubObjectsUpgrade<-Upgrade_IsengardHeavyArmor WeaponSetUpgrade<-Upgrade_IsengardForgedBlades | IsengardFighterHorde:InitialPayload |
| IsengardPikeman | cost 0, time 1s, CP 4; upgrade modules: ArmorUpgrade<-Upgrade_IsengardHeavyArmor StatusBitsUpgrade<-Upgrade_IsengardForgedBlades SubObjectsUpgrade<-Upgrade_IsengardForgedBlades SubObjectsUpgrade<-Upgrade_IsengardHeavyArmor | IsengardPikemanHorde:InitialPayload |
| IsengardPorter | cost 500, time 30s, CP 0; powers: SpecialPowerModule(SpecialAbilityPorterExtinguishFire) | FactionIsengard:StartingUnit0<br>FactionIsengard:StartingUnit1<br>IsengardFortressCommandSet:1:Command_ConstructIsengardPorter<br>(+1) |
| IsengardSpellBook | cost 0, time 1s, CP 0; powers: DarknessSpecialPower(SpellBookDarkness) DevastateSpecialPower(SpellBookDevastation) FreezingRainSpecialPower(SpellBookFreezingRain) OCLSpecialPower(SpellBookArrowVolleyEvil) OCLSpecialPower(SpellBookAvalanche) OCLSpecialPower(SpellBookAwakenWyrm) OCLSpecialPower(SpellBookBalrogAlly) OCLSpecialPower(SpellBookBarricade) OCLSpecialPower(SpellBookCaveBats) OCLSpecialPower(SpellBookChillWind) OCLSpecialPower(SpellBookCrebain) OCLSpecialPower(SpellBookDragonAlly) OCLSpecialPower(SpellBookDragonStrike) OCLSpecialPower(SpellBookEvilBombard) OCLSpecialPower(SpellBookEvilEyeofSauron) OCLSpecialPower(SpellBookEyeofSauron) OCLSpecialPower(SpellBookFrozenLand) OCLSpecialPower(SpellBookPalantirVision) OCLSpecialPower(SpellBookRainOfFire) OCLSpecialPower(SpellBookSpiderlingAllies) OCLSpecialPower(SpellBookSummonGiants) OCLSpecialPower(SpellBookSummonOrcs) OCLSpecialPower(SpellBookSummonShadeOfWolf) OCLSpecialPower(SpellBookSummonWights) OCLSpecialPower(SpellBookWatcherAlly) OCLSpecialPower(SpellBookWildMenAllies) ProductionSpeedBonus(SpellBookCalltheHorde) ProductionSpeedBonus(SpellBookMordorCalltheHorde) ScavengerSpecialPower(SpellBookScavenger) SpecialPowerModule(SpellBookBlight) SpecialPowerModule(SpellBookFueltheFires) SpecialPowerModule(SpellBookIndustry) SpecialPowerModule(SpellBookRebuild) SpecialPowerModule(SpellBookSnowbind) SpecialPowerModule(SpellBookWarChant) TaintSpecialPower(SpellBookIsengardTaint) TaintSpecialPower(SpellBookTaint) UntamedAllegianceSpecialPower(SpellBookUntamedAllegiance); upgrade modules: BuildableHeroListUpgrade<-Upgrade_RingHero | FactionIsengard:SpellBookMp<br>the started skirmish's base |
| IsengardUrukCrossbow | cost 0, time 1s, CP 4; upgrade modules: ArmorUpgrade<-Upgrade_IsengardHeavyArmor SubObjectsUpgrade<-Upgrade_IsengardFireArrows SubObjectsUpgrade<-Upgrade_IsengardHeavyArmor WeaponSetUpgrade<-Upgrade_IsengardFireArrows | IsengardUrukCrossbowHorde:InitialPayload |
| IsengardWargPackHorde | cost 250, time 30s, CP 60; powers: SpecialPowerModule(SpecialAbilityWargHowl); upgrade modules: LevelUpUpgrade<-Upgrade_IsengardBasicTraining | IsengardWargPitCommandSetLevel1:1:Command_ConstructIsengardWargPackHorde<br>IsengardWargPitCommandSetLevel2:1:Command_ConstructIsengardWargPackHorde<br>IsengardWargPitCommandSetLevel3:1:Command_ConstructIsengardWargPackHorde |
| IsengardWargRiderHorde | cost 500, time 30s, CP 60; powers: SpecialPowerModule(SpecialAbilityCaptureBuilding) SpecialPowerModule(SpecialAbilityWargHowl); upgrade modules: LevelUpUpgrade<-Upgrade_IsengardBasicTraining StatusBitsUpgrade<-Upgrade_IsengardForgedBlades StatusBitsUpgrade<-Upgrade_IsengardHeavyArmor | IsengardWargPitCommandSetLevel1:2:Command_ConstructIsengardWargRiderHorde<br>IsengardWargPitCommandSetLevel2:2:Command_ConstructIsengardWargRiderHorde<br>IsengardWargPitCommandSetLevel3:2:Command_ConstructIsengardWargRiderHorde |
| IsengardWildman | cost 0, time 1s, CP 4; upgrade modules: ArmorUpgrade<-Upgrade_IsengardHeavyArmor SubObjectsUpgrade<-Upgrade_IsengardHeavyArmor SubObjectsUpgrade<-Upgrade_IsengardTorches WeaponSetUpgrade<-Upgrade_IsengardTorches | IsengardWildmanHorde:InitialPayload |
| IsengardWildmanAxe | cost 0, time 1s, CP 4 | IsengardWildmanAxeHorde:InitialPayload |
| MordorWorker | cost 20, time 5s, CP 5; powers: SpecialPowerModule(SpecialAbilityHarvest) | IsengardLumberMillCommandSetLevel1:1:Command_BuildMordorWorker |

### hero

| name | data | reached from |
|---|---|---|
| IsengardLurtz | cost 1200, time 30s, CP 50; powers: SpecialPowerModule(SpecialAbilityCaptureBuilding) SpecialPowerModule(SpecialAbilityFakeLeadership) SpecialPowerModule(SpecialAbilityFakeLeadership2) SpecialPowerModule(SpecialAbilityLurtzCarnage) SpecialPowerModule(SpecialAbilityLurtzCripple); upgrade modules: AttributeModifierAuraUpdate<-Upgrade_LurtzLeadership AttributeModifierAuraUpdate<-Upgrade_LurtzPillage AutoHealBehavior<- UnpauseSpecialPowerUpgrade<-Upgrade_LurtzCarnage UnpauseSpecialPowerUpgrade<-Upgrade_LurtzCrippleTrigger UnpauseSpecialPowerUpgrade<-Upgrade_LurtzLeadership UnpauseSpecialPowerUpgrade<-Upgrade_LurtzPillage | FactionIsengard:BuildableHeroesMP |
| IsengardSaruman | cost 3000, time 45s, CP 75; powers: SpecialPowerModule(SpecialAbilityCaptureBuilding) SpecialPowerModule(SpecialAbilityDominateEnemy) SpecialPowerModule(SpecialAbilityKingsFavor) SpecialPowerModule(SpecialAbilityLightingBolt) SpecialPowerModule(SpecialAbilitySarumanFireball) SpecialPowerModule(SpecialPowerTelekeneticPush); upgrade modules: AutoHealBehavior<- UnpauseSpecialPowerUpgrade<-Upgrade_SarumanDominate UnpauseSpecialPowerUpgrade<-Upgrade_SarumanFireBall UnpauseSpecialPowerUpgrade<-Upgrade_SarumanSpeechCraft UnpauseSpecialPowerUpgrade<-Upgrade_SarumanThunderBolt | FactionIsengard:BuildableHeroesMP |
| IsengardSharku | cost 1200, time 30s, CP 25; powers: PlayerHealSpecialPower(SpecialAbilityIsengardSharkuManEaterDrop) SpecialPowerModule(SpecialAbilityCaptureBuilding) SpecialPowerModule(SpecialAbilityIsengardSharkuManEaterGrab) SpecialPowerModule(SpecialAbilitySharkuBloodHunt) SpecialPowerModule(SpecialAbilityTameTheBeast); upgrade modules: AttributeModifierAuraUpdate<-Upgrade_SharkuBloodHunt AutoHealBehavior<- ModelConditionUpgrade<-Upgrade_SharkuBloodHunt UnpauseSpecialPowerUpgrade<-Upgrade_SharkuBloodHunt UnpauseSpecialPowerUpgrade<-Upgrade_SharkuManEater UnpauseSpecialPowerUpgrade<-Upgrade_SharkuTameTheBeast | FactionIsengard:BuildableHeroesMP |
| IsengardWormTongue | cost 800, time 45s, CP 50; powers: SpecialPowerModule(SpecialAbilityActivateeDummy) SpecialPowerModule(SpecialAbilityCaptureBuilding) SpecialPowerModule(SpecialAbilityKingsFavor) SpecialPowerModule(SpecialAbilitySarumanFireball) SpecialPowerModule(SpecialAbilityWormtongueBackstab) SpecialPowerModule(SpecialAbilityWormtongueCorrodeAllegiance) SpecialPowerModule(SpecialAbilityWormtongueEscape) SpecialPowerModule(SpecialAbilityWormtongueVenomousWords) SpecialPowerModule(SpecialPowerTelekeneticPush); upgrade modules: AutoHealBehavior<- UnpauseSpecialPowerUpgrade<-Upgrade_SarumanFireBall UnpauseSpecialPowerUpgrade<-Upgrade_SarumanSpeechCraft UnpauseSpecialPowerUpgrade<-Upgrade_WormtongueBackstab UnpauseSpecialPowerUpgrade<-Upgrade_WormtongueCorrodeAllegiance UnpauseSpecialPowerUpgrade<-Upgrade_WormtongueVenomousWords | FactionIsengard:BuildableHeroesMP |
| MordorSauron_RingHero | cost 10000, time 300s, CP 0; powers: SpecialPowerModule(FiresOfDoomSpecialPower) SpecialPowerModule(SpecialAbilityScreech); upgrade modules: AttributeModifierAuraUpdate<- AutoHealBehavior<- RemoveUpgradeUpgrade<-Upgrade_WildFaction|Upgrade_IsengardFaction|Upgrade_MordorFaction|Upgrade_GoblinFaction|Upgrade_AngmarFaction | FactionIsengard:BuildableRingHeroesMP |

### upgrade

| name | data | reached from |
|---|---|---|
| Upgrade_HasWallTrebuchet | OBJECT, cost 600, time 30s | IsengardUpgradeCommandSetBuyNewBallista:1:Command_IsengardPurchaseNewWallBallista |
| Upgrade_IsengardArmoryLevel2 | OBJECT, cost 750, time 30s | IsengardArmoryCommandSetLevel1:5:Command_PurchaseUpgradeIsengardArmoryLevel2 |
| Upgrade_IsengardArmoryLevel3 | OBJECT, cost 750, time 60s | IsengardArmoryCommandSetLevel2:5:Command_PurchaseUpgradeIsengardArmoryLevel3 |
| Upgrade_IsengardBasicTraining | OBJECT, cost 300, time 10s | IsengardFighterHordeCommandSet:5:Command_PurchaseUpgradeIsengardBasicTraining<br>IsengardPikemanHordeCommandSet:5:Command_PurchaseUpgradeIsengardBasicTraining<br>IsengardUrukCrossbowHordeCommandSet:5:Command_PurchaseUpgradeIsengardBasicTraining<br>(+4) |
| Upgrade_IsengardFireArrows | OBJECT, cost 300, time 10s | IsengardUrukCrossbowHordeCommandSet:3:Command_PurchaseUpgradeIsengardFireArrows |
| Upgrade_IsengardForgedBlades | OBJECT, cost 300, time 10s | IsengardFighterHordeCommandSet:3:Command_PurchaseUpgradeIsengardForgedBlades<br>IsengardPikemanHordeCommandSet:3:Command_PurchaseUpgradeIsengardForgedBlades<br>WargRiderHordeCommandSet:3:Command_PurchaseUpgradeIsengardForgedBlades |
| Upgrade_IsengardFortressBurningForges | OBJECT, cost 500, time 30s | IsengardFortressCommandSet:8:Command_PurchaseUpgradeIsengardFortressBurningForges |
| Upgrade_IsengardFortressExcavations | OBJECT, cost 1000, time 30s | IsengardFortressCommandSet:9:Command_PurchaseUpgradeIsengardFortressExcavations |
| Upgrade_IsengardFortressIronPlatingTrigger | OBJECT, cost 2000, time 30s | IsengardFortressCommandSet:11:Command_PurchaseUpgradeIsengardFortressIronPlating |
| Upgrade_IsengardFortressMurderOfCrows | OBJECT, cost 500, time 30s | IsengardFortressCommandSet:7:Command_PurchaseUpgradeIsengardFortressMurderOfCrows |
| Upgrade_IsengardFortressOrcfireMunitionsTrigger | OBJECT, cost 1500, time 30s | IsengardFortressCommandSet:10:Command_PurchaseUpgradeIsengardFortressOrcfireMunitions |
| Upgrade_IsengardFortressWizardsTower | OBJECT, cost 2500, time 30s | IsengardFortressCommandSet:12:Command_PurchaseUpgradeIsengardFortressWizardsTower |
| Upgrade_IsengardHeavyArmor | OBJECT, cost 300, time 10s | IsengardFighterHordeCommandSet:4:Command_PurchaseUpgradeIsengardHeavyArmor<br>IsengardPikemanHordeCommandSet:4:Command_PurchaseUpgradeIsengardHeavyArmor<br>IsengardUrukCrossbowHordeCommandSet:4:Command_PurchaseUpgradeIsengardHeavyArmor<br>(+2) |
| Upgrade_IsengardSiegeWorksLevel2 | OBJECT, cost 500, time 30s | IsengardSiegeWorksCommandSetLevel1:4:Command_UpgradeIsengardSiegeWorksLevel2 |
| Upgrade_IsengardSiegeWorksLevel3 | OBJECT, cost 1000, time 45s | IsengardSiegeWorksCommandSetLevel2:4:Command_UpgradeIsengardSiegeWorksLevel3 |
| Upgrade_IsengardTavernLevel2 | OBJECT, cost 400, time 30s | IsengardTavernCommandSetLevel1:5:Command_UpgradeIsengardTavernLevel2 |
| Upgrade_IsengardTavernLevel3 | OBJECT, cost 1000, time 60s | IsengardTavernCommandSetLevel2:5:Command_UpgradeIsengardTavernLevel3 |
| Upgrade_IsengardTorches | OBJECT, cost 300, time 10s | IsengardWildmanHordeCommandSet:3:Command_PurchaseUpgradeIsengardTorches |
| Upgrade_IsengardUrukPitLevel2 | OBJECT, cost 500, time 30s | IsengardUrukPitCommandSetLevel1:7:Command_UpgradeIsengardUrukPitLevel2 |
| Upgrade_IsengardUrukPitLevel3 | OBJECT, cost 1000, time 45s | IsengardUrukPitCommandSetLevel2:7:Command_UpgradeIsengardUrukPitLevel3 |
| Upgrade_IsengardWargPitLevel2 | OBJECT, cost 200, time 30s | IsengardWargPitCommandSetLevel1:3:Command_UpgradeIsengardWargPitLevel2 |
| Upgrade_IsengardWargPitLevel3 | OBJECT, cost 300, time 45s | IsengardWargPitCommandSetLevel2:3:Command_UpgradeIsengardWargPitLevel3 |
| Upgrade_SentryTowerFireArrows | OBJECT, cost 500, time 30s | GenericSentryTowerCommandSet:3:Command_PurchaseSentryTowerFireArrowsUpgrade |
| Upgrade_TechnologyIsengardBasicTraining | PLAYER, cost 1000, time 30s | IsengardArmoryCommandSetLevel1:1:Command_PurchaseTechnologyIsengardBasicTraining<br>IsengardArmoryCommandSetLevel2:1:Command_PurchaseTechnologyIsengardBasicTraining<br>IsengardArmoryCommandSetLevel3:1:Command_PurchaseTechnologyIsengardBasicTraining |
| Upgrade_TechnologyIsengardFireArrows | PLAYER, cost 1000, time 30s | IsengardArmoryCommandSetLevel1:4:Command_PurchaseTechnologyIsengardFireArrows<br>IsengardArmoryCommandSetLevel2:4:Command_PurchaseTechnologyIsengardFireArrows<br>IsengardArmoryCommandSetLevel3:4:Command_PurchaseTechnologyIsengardFireArrows |
| Upgrade_TechnologyIsengardForgedBlades | PLAYER, cost 1000, time 30s | IsengardArmoryCommandSetLevel1:3:Command_PurchaseTechnologyIsengardForgedBlades<br>IsengardArmoryCommandSetLevel2:3:Command_PurchaseTechnologyIsengardForgedBlades<br>IsengardArmoryCommandSetLevel3:3:Command_PurchaseTechnologyIsengardForgedBlades |
| Upgrade_TechnologyIsengardHeavyArmor | PLAYER, cost 1000, time 15s | IsengardArmoryCommandSetLevel1:2:Command_PurchaseTechnologyIsengardHeavyArmor<br>IsengardArmoryCommandSetLevel2:2:Command_PurchaseTechnologyIsengardHeavyArmor<br>IsengardArmoryCommandSetLevel3:2:Command_PurchaseTechnologyIsengardHeavyArmor |
| Upgrade_TestBuilding | OBJECT, cost 0, time 10s | MordorBaseTestCommandSet:1:Command_UpgradeTest |
| Upgrade_TestBuilding_2 | OBJECT, cost 0, time 10s | MordorBaseTestCommandSet:2:Command_UpgradeTest_2 |

### power

| name | data | reached from |
|---|---|---|
| FiresOfDoomSpecialPower | SPECIAL_POWER, type 125, reload 1200 frames, options 0x20 | SauronCommandSet:2:Command_FiresOfDoom |
| SpecialAbilityBerserkerLightTorch | SPECIAL_POWER, type 53, reload 0 frames, options 0x1100100 | IsengardBeserkerCommandSet:2:Command_SpecialAbilityLightTorch |
| SpecialAbilityCaptureBuilding | SPECIAL_POWER, type 29, reload 0 frames, options 0x100103 | IsengardBeserkerCommandSet:12:Command_CaptureBuilding<br>IsengardFanaticCommandSet:12:Command_CaptureBuilding<br>IsengardFanaticHordeCommandSet:12:Command_CaptureBuilding<br>(+11) |
| SpecialAbilityDominateEnemy | SPECIAL_POWER, type 77, reload 1500 frames, options 0x220 | SarumanCommandSet:4:Command_SpecialAbilityDominateEnemy |
| SpecialAbilityFakeLeadership | SPECIAL_POWER, type 135, reload 1 frames, options 0x10000000 | LurtzCommandSet:5:Command_LurtzLeadership |
| SpecialAbilityFakeLeadership2 | SPECIAL_POWER, type 135, reload 1 frames, options 0x10000000 | LurtzCommandSet:6:Command_LurtzPillage |
| SpecialAbilityFanaticHeroMode | SPECIAL_POWER, type 60, reload 1200 frames, options 0x100100 | IsengardFanaticCommandSet:3:Command_SpecialAbilityDeathWish<br>IsengardFanaticHordeCommandSet:3:Command_SpecialAbilityDeathWish |
| SpecialAbilityHarvest | SPECIAL_POWER, type 141, reload 5 frames, options 0x100100 | MordorWorkerCommandSet:1:Command_Harvest |
| SpecialAbilityIsengardSharkuManEaterGrab | SPECIAL_POWER, type 129, reload 450 frames, options 0x4041 | SharkuCommandSet:4:Command_SpecialAbilitySharkuManEater |
| SpecialAbilityKingsFavor | SPECIAL_POWER, type 134, reload 900 frames, options 0x220 | SarumanCommandSet:5:Command_SpecialAbilitySpeechCraft |
| SpecialAbilityLightingBolt | SPECIAL_POWER, type 47, reload 300 frames, options 0x20 | SarumanCommandSet:6:Command_SarumanThunderBolt |
| SpecialAbilityLightningStrike | SPECIAL_POWER, type 47, reload 1500 frames, options 0x400020 | IsengardFortressCommandSet:4:Command_IsengardWizardsTowerLightningStrike |
| SpecialAbilityLurtzCarnage | SPECIAL_POWER, type 60, reload 600 frames, options 0x0 | LurtzCommandSet:4:Command_SpecialAbilityLurtzCarnage |
| SpecialAbilityLurtzCripple | SPECIAL_POWER, type 129, reload 300 frames, options 0x1 | LurtzCommandSet:3:Command_LurtzCripple |
| SpecialAbilityMurderOfCrows | SPECIAL_POWER, type 110, reload 300 frames, options 0x220 | IsengardFortressCommandSet:13:Command_SpecialAbilityMurderOfCrows |
| SpecialAbilityPorterExtinguishFire | SPECIAL_POWER, type 75, reload 25 frames, options 0x220 | IsengardPorterCommandSet:2:Command_PorterExtinguishFire |
| SpecialAbilitySarumanFireball | SPECIAL_POWER, type 130, reload 300 frames, options 0x7 | SarumanCommandSet:3:Command_SarumanFireball |
| SpecialAbilityScreech | SPECIAL_POWER, type 137, reload 900 frames, options 0x100 | SauronCommandSet:3:Command_SpecialAbilityTerribleFury |
| SpecialAbilitySharkuBloodHunt | SPECIAL_POWER, type 76, reload 450 frames, options 0x10000000 | SharkuCommandSet:3:Command_SpecialAbilitySharkuBloodHunt |
| SpecialAbilityStoneWall | SPECIAL_POWER, type 60, reload 900 frames, options 0x100100 | IsengardFanaticCommandSet:2:Command_SpecialAbilityStoneWall<br>IsengardFanaticHordeCommandSet:2:Command_SpecialAbilityStoneWall |
| SpecialAbilityTameTheBeast | SPECIAL_POWER, type 77, reload 600 frames, options 0x220 | SharkuCommandSet:2:Command_SpecialAbilityTameTheBeast |
| SpecialAbilityWargHowl | SPECIAL_POWER, type 76, reload 600 frames, options 0x100100 | WargPackHordeCommandSet:2:Command_SpecialAbilityWargHowl<br>WargRiderHordeCommandSet:2:Command_SpecialAbilityWargHowl |
| SpecialAbilityWildmanPillage | SPECIAL_POWER, type 53, reload 600 frames, options 0x10000000 | IsengardWildmanHordeCommandSet:2:Command_SpecialAbilityWildmanPillage |
| SpecialAbilityWormtongueBackstab | SPECIAL_POWER, type 129, reload 300 frames, options 0x1 | WormtongueCommandSet:4:Command_SpecialAbilityWormtongueBackstab |
| SpecialAbilityWormtongueCorrodeAllegiance | SPECIAL_POWER, type 129, reload 900 frames, options 0x3 | WormtongueCommandSet:5:Command_SpecialAbilityWormtongueCorrodeAllegiance |
| SpecialAbilityWormtongueEscape | SPECIAL_POWER, type 54, reload 450 frames, options 0x0 | WormtongueCommandSet:2:Command_SpecialAbilityWormtongueEscape |
| SpecialAbilityWormtongueVenomousWords | SPECIAL_POWER, type 53, reload 450 frames, options 0x20 | WormtongueCommandSet:3:Command_SpecialAbilityWormtongueVenomousWords |
| SpecialPowerTelekeneticPush | SPECIAL_POWER, type 68, reload 300 frames, options 0x1 | SarumanCommandSet:2:Command_SpecialAbilityIsengardWizardBlast |
| SpellBookCrebain | SPELL_BOOK, type 109, reload 900 frames, options 0x20 | IsengardSpellBookCommandSet:3:Command_SpellBookCrebain |
| SpellBookDevastation | SPELL_BOOK, type 112, reload 1800 frames, options 0x20 | IsengardSpellBookCommandSet:6:Command_SpellBookDevastation |
| SpellBookDragonAlly | SPELL_BOOK, type 126, reload 3600 frames, options 0x20 | IsengardSpellBookCommandSet:11:Command_SpellBookDragonAlly |
| SpellBookDragonStrike | SPELL_BOOK, type 127, reload 3600 frames, options 0x20 | IsengardSpellBookCommandSet:12:Command_SpellBookDragonStrike |
| SpellBookFreezingRain | SPELL_BOOK, type 121, reload 1800 frames, options 0x0 | IsengardSpellBookCommandSet:8:Command_SpellBookFreezingRain |
| SpellBookFueltheFires | SPELL_BOOK, type 122, reload 0 frames, options 0x10000000 | IsengardSpellBookCommandSet:9:Command_SpellBookFueltheFires |
| SpellBookIndustry | SPELL_BOOK, type 111, reload 1800 frames, options 0x20 | IsengardSpellBookCommandSet:5:Command_SpellBookIndustry |
| SpellBookIsengardTaint | SPELL_BOOK, type 104, reload 1800 frames, options 0x20 | IsengardSpellBookCommandSet:4:Command_SpellBookIsengardTaint |
| SpellBookPalantirVision | SPELL_BOOK, type 108, reload 900 frames, options 0x20 | IsengardSpellBookCommandSet:2:Command_SpellBookPalantirVision |
| SpellBookWarChant | SPELL_BOOK, type 107, reload 900 frames, options 0x20 | IsengardSpellBookCommandSet:1:Command_SpellBookWarChant |
| SpellBookWatcherAlly | SPELL_BOOK, type 123, reload 2700 frames, options 0x20 | IsengardSpellBookCommandSet:10:Command_SpellBookWatcherAlly |
| SpellBookWildMenAllies | SPELL_BOOK, type 115, reload 1800 frames, options 0x20 | IsengardSpellBookCommandSet:7:Command_SpellBookWildMenAllies |

### science

| name | data | reached from |
|---|---|---|
| SCIENCE_Crebain | PURCHASE_SCIENCE | IsengardSpellStoreCommandSet:3:Command_PurchaseSpellCrebain |
| SCIENCE_Devastation | PURCHASE_SCIENCE | IsengardSpellStoreCommandSet:6:Command_PurchaseSpellDevastation |
| SCIENCE_DragonAlly | PURCHASE_SCIENCE | IsengardSpellStoreCommandSet:11:Command_PurchaseSpellDragonAlly |
| SCIENCE_DragonStrike | PURCHASE_SCIENCE | IsengardSpellStoreCommandSet:12:Command_PurchaseSpellDragonStrike |
| SCIENCE_FreezingRain | PURCHASE_SCIENCE | IsengardSpellStoreCommandSet:9:Command_PurchaseSpellFreezingRain |
| SCIENCE_FueltheFires | PURCHASE_SCIENCE | IsengardSpellStoreCommandSet:8:Command_PurchaseSpellFueltheFires |
| SCIENCE_Industry | PURCHASE_SCIENCE | IsengardSpellStoreCommandSet:7:Command_PurchaseSpellIndustry |
| SCIENCE_IsengardTaint | PURCHASE_SCIENCE | IsengardSpellStoreCommandSet:5:Command_PurchaseSpellIsengardTaint |
| SCIENCE_PalantirVision | PURCHASE_SCIENCE | IsengardSpellStoreCommandSet:1:Command_PurchaseSpellPalantirVision |
| SCIENCE_WarChant | PURCHASE_SCIENCE | IsengardSpellStoreCommandSet:2:Command_PurchaseSpellWarChant |
| SCIENCE_WatcherAlly | PURCHASE_SCIENCE | IsengardSpellStoreCommandSet:10:Command_PurchaseSpellWatcherAlly |
| SCIENCE_WildMenAllies | PURCHASE_SCIENCE | IsengardSpellStoreCommandSet:4:Command_PurchaseSpellWildMenAllies |

### command

| name | data | reached from |
|---|---|---|
| Command_ArcherBombard | FIRE_WEAPON | IsengardUrukCrossbowHordeCommandSet:2:Command_ArcherBombard |
| Command_AttackMove | ATTACK_MOVE | GenericCommandSet:13:Command_AttackMove<br>IsengardBallistaCommandSet:13:Command_AttackMove<br>IsengardBeserkerCommandSet:13:Command_AttackMove<br>(+21) |
| Command_BombardEvil | FIRE_WEAPON | IsengardBallistaCommandSet:2:Command_BombardEvil |
| Command_CancelWallSpan | CANCEL_NEIGHBORHOOD | IsengardCastleWallHubCommandSet:4:Command_CancelWallSpan |
| Command_CreateAHeroReviveSlot | REVIVE | IsengardFortressCommandSet:17:Command_CreateAHeroReviveSlot |
| Command_DeployMine | FIRE_WEAPON | IsengardExplosiveMineCommandSet:1:Command_DeployMine |
| Command_ExitGarrison | EXIT_CONTAINER | GenericSentryTowerCommandSet:1:Command_ExitGarrison<br>GenericSentryTowerCommandSet:2:Command_ExitGarrison |
| Command_GenericReviveSlot1 | REVIVE | IsengardFortressCommandSet:18:Command_GenericReviveSlot1 |
| Command_GenericReviveSlot2 | REVIVE | IsengardFortressCommandSet:19:Command_GenericReviveSlot2 |
| Command_GenericReviveSlot3 | REVIVE | IsengardFortressCommandSet:20:Command_GenericReviveSlot3 |
| Command_GenericReviveSlot4 | REVIVE | IsengardFortressCommandSet:21:Command_GenericReviveSlot4 |
| Command_GenericReviveSlot5 | REVIVE | IsengardFortressCommandSet:22:Command_GenericReviveSlot5 |
| Command_GenericReviveSlot6 | REVIVE | IsengardFortressCommandSet:23:Command_GenericReviveSlot6 |
| Command_GenericReviveSlot7 | REVIVE | IsengardFortressCommandSet:24:Command_GenericReviveSlot7 |
| Command_IsengardBloodThirstySacrifice | BLOODTHIRSTY | IsengardFighterCommandSet:2:Command_IsengardBloodThirstySacrifice<br>IsengardFighterHordeCommandSet:6:Command_IsengardBloodThirstySacrifice |
| Command_IsengardPikemanPorcupineFormation | HORDE_TOGGLE_FORMATION | IsengardPikemanHordeCommandSet:2:Command_IsengardPikemanPorcupineFormation |
| Command_RadialBack | POP_VISIBLE_COMMAND_RANGE | IsengardFortressCommandSet:14:Command_RadialBack<br>IsengardFortressCommandSet:25:Command_RadialBack |
| Command_RingHeroReviveSlot | REVIVE | IsengardFortressCommandSet:16:Command_RingHeroReviveSlot |
| Command_SelectRevivablesIsengardFortress | PUSH_VISIBLE_COMMAND_RANGE | IsengardFortressCommandSet:2:Command_SelectRevivablesIsengardFortress |
| Command_SelectUpgradesIsengardFortress | PUSH_VISIBLE_COMMAND_RANGE | IsengardFortressCommandSet:3:Command_SelectUpgradesIsengardFortress |
| Command_Sell | SELL | GenericSentryTowerCommandSet:6:Command_Sell<br>IsengardArmoryCommandSetLevel1:6:Command_Sell<br>IsengardArmoryCommandSetLevel2:6:Command_Sell<br>(+18) |
| Command_SetStanceAggressive | SET_STANCE | GenericCommandSet:17:Command_SetStanceAggressive<br>IsengardBallistaCommandSet:17:Command_SetStanceAggressive<br>IsengardBeserkerCommandSet:17:Command_SetStanceAggressive<br>(+19) |
| Command_SetStanceBattle | SET_STANCE | GenericCommandSet:16:Command_SetStanceBattle<br>IsengardBallistaCommandSet:16:Command_SetStanceBattle<br>IsengardBeserkerCommandSet:16:Command_SetStanceBattle<br>(+19) |
| Command_SetStanceHoldGround | SET_STANCE | GenericCommandSet:18:Command_SetStanceHoldGround<br>IsengardBallistaCommandSet:18:Command_SetStanceHoldGround<br>IsengardBeserkerCommandSet:18:Command_SetStanceHoldGround<br>(+19) |
| Command_Stop | STOP | GenericCommandSet:14:Command_Stop<br>IsengardBallistaCommandSet:14:Command_Stop<br>IsengardBeserkerCommandSet:14:Command_Stop<br>(+22) |
| Command_ToggleFormationIsengardFighter | HORDE_TOGGLE_FORMATION | IsengardFighterHordeCommandSet:2:Command_ToggleFormationIsengardFighter |
| Command_ToggleLurtzWeapon | TOGGLE_WEAPONSET | LurtzCommandSet:2:Command_ToggleLurtzWeapon |
| Command_ToggleNoAutoAcquire | TOGGLE_NO_AUTO_ACQUIRE | IsengardBallistaCommandSet:3:Command_ToggleNoAutoAcquire |
| Command_ToggleStance | TOGGLE_STANCE | GenericCommandSet:1:Command_ToggleStance<br>IsengardBallistaCommandSet:1:Command_ToggleStance<br>IsengardBeserkerCommandSet:1:Command_ToggleStance<br>(+19) |

### behaviour modules without a runtime

| module class | templates |
|---|---|
| AnnounceBirthAndDeathBehavior | MordorSauron_RingHero |
| AudioLoopUpgrade | IsengardFortressCitadel |
| BaseUpgrade | MordorBase |
| BloodthirstyUpdate | IsengardFighterHorde |
| CitadelSlaughterHordeContain | IsengardFortressCitadel |
| EvacuateDamage | IsengardBattleTower |
| FlingPassengerSpecialAbilityUpdate | IsengardSharku |
| GeometryUpgrade | IsengardArmory, IsengardFortressCitadel, IsengardFurnace, IsengardLumberMill, IsengardSiegeWorks |
| ObjectCreationUpgrade | IsengardBallistaExpansion |
| PickupStuffUpdate | IsengardBerserkerHorde, IsengardFighterHorde, IsengardPikemanHorde, IsengardUrukCrossbowHorde, IsengardWargPackHorde, IsengardWargRiderHorde, IsengardWildmanAxeHorde, IsengardWildmanHorde |
| SiegeDeployHordeSpecialPower | IsengardBerserkerHorde, IsengardFighterHorde |
| SlaveWatcherBehavior | IsengardBallistaExpansion |
| SupplyCenterCreate | IsengardLumberMill |
| SupplyCenterDockUpdate | IsengardLumberMill |
| TransitionDamageFX | IsengardBallista, IsengardFurnace, IsengardWargPit |

## FactionMordor

16 structures, 17 units, 6 hordes, 6 heroes, 28 upgrades, 33 powers, 12 sciences, 31 other commands; 49 command sets.

### structure

| name | data | reached from |
|---|---|---|
| MordorBase | cost 0, time 5s, CP 0 | MordorWorkerCommandSet:3:Command_ConstructMordorBaseTest |
| MordorBase_DEMO | cost 500, time 5s, CP 0 | MordorWorkerCommandSet:2:Command_ConstructMordorBase |
| MordorBattleTower | cost 500, time 60s, CP 0; upgrade modules: WeaponSetUpgrade<-Upgrade_SentryTowerFireArrows | MordorPorterCommandSet:11:Command_PorterconstructMordorBattleTower |
| MordorFortress | cost 5000, time 120s, CP 0 | FactionMordor:StartingBuilding<br>MordorPorterCommandSet:12:Command_PorterConstructMordorFortress<br>the started skirmish's base |
| MordorFortressBarricadeExpansion | cost 600, time 30s, CP 0; upgrade modules: AttributeModifierUpgrade<-Upgrade_MordorFortressMorgulSorcery ModelConditionUpgrade<-Upgrade_MordorFortressMorgulSorcery SpawnBehavior<- WeaponSetUpgrade<-Upgrade_EvilFortressFlamingMunitions | MordorFortressExpansionPadCornerCommandSet:2:Command_ConstructMordorBarricadeExpansion |
| MordorFortressCitadel | cost 5000, time 120s, CP 0; powers: SpecialPowerModule(SpecialAbilityGorgorothSpireFireball) SpecialPowerModule(SpecialAbilityMordorFortressMagmaCauldrons); upgrade modules: AttributeModifierAuraUpdate<-Upgrade_MordorFortressDoomPyres AttributeModifierUpgrade<-Upgrade_MordorFortressMorgulSorcery CastleUpgrade<-Upgrade_MordorFortressFireArrowsTrigger CastleUpgrade<-Upgrade_MordorFortressMorgulSorceryTrigger ModelConditionUpgrade<-Upgrade_EvilFortressFlamingMunitions ModelConditionUpgrade<-Upgrade_MordorFortressGorgorothSpire ModelConditionUpgrade<-Upgrade_MordorFortressMagmaCauldrons ModelConditionUpgrade<-Upgrade_MordorFortressMorgulSorcery ModelConditionUpgrade<-Upgrade_RingHero|Upgrade_FortressRingHero StatusBitsUpgrade<-Upgrade_EvilFortressFlamingMunitions SubObjectsUpgrade<-Upgrade_MordorFortressDoomPyres SubObjectsUpgrade<-Upgrade_StructureLevel1 UnpauseSpecialPowerUpgrade<-Upgrade_MordorFortressGorgorothSpire UnpauseSpecialPowerUpgrade<-Upgrade_MordorFortressMagmaCauldrons | the started skirmish's base |
| MordorFortressExpansionPadCorner | cost 1, time 5s, CP 0 | the started skirmish's base |
| MordorGateWatchersExpansion | cost 600, time 30s, CP 0; upgrade modules: AttributeModifierAuraUpdate<- AttributeModifierUpgrade<-Upgrade_MordorFortressMorgulSorcery ModelConditionUpgrade<-Upgrade_MordorFortressMorgulSorcery | MordorFortressExpansionPadCornerCommandSet:3:Command_ConstructMordorGateWatchersExpansion |
| MordorHaradrimPalace | cost 300, time 30s, CP 0; upgrade modules: CommandSetUpgrade<-Upgrade_MordorHaradrimPalaceLevel2 CommandSetUpgrade<-Upgrade_MordorHaradrimPalaceLevel3 LevelUpUpgrade<-Upgrade_MordorHaradrimPalaceLevel2 LevelUpUpgrade<-Upgrade_MordorHaradrimPalaceLevel3 SubObjectsUpgrade<-Upgrade_MordorHaradrimPalaceLevel2 SubObjectsUpgrade<-Upgrade_MordorHaradrimPalaceLevel3 SubObjectsUpgrade<-Upgrade_StructureLevel1 WeaponSetUpgrade<-Upgrade_MordorHaradrimPalaceLevel3 | MordorPorterCommandSet:6:Command_PorterConstructMordorHaradrimPalace |
| MordorLumberMill | cost 350, time 30s, CP 0; upgrade modules: SpawnBehavior<- SubObjectsUpgrade<-Upgrade_StructureLevel1 SubObjectsUpgrade<-Upgrade_StructureLevel2 SubObjectsUpgrade<-Upgrade_StructureLevel3 WeaponSetUpgrade<-Upgrade_StructureLevel3 | MordorPorterCommandSet:4:Command_PorterConstructMordorLumberMill |
| MordorMumakilPen | cost 1000, time 45s, CP 0; upgrade modules: CommandSetUpgrade<-Upgrade_MordorMumakilPenLevel2 CommandSetUpgrade<-Upgrade_MordorMumakilPenLevel3 LevelUpUpgrade<-Upgrade_MordorMumakilPenLevel2 LevelUpUpgrade<-Upgrade_MordorMumakilPenLevel3 SubObjectsUpgrade<-Upgrade_MordorMumakilPenLevel2 SubObjectsUpgrade<-Upgrade_MordorMumakilPenLevel3 SubObjectsUpgrade<-Upgrade_StructureLevel1 | MordorPorterCommandSet:9:Command_PorterConstructMumakilPen |
| MordorOrcPit | cost 350, time 35s, CP 0; upgrade modules: CommandSetUpgrade<-Upgrade_MordorOrcPitLevel2 CommandSetUpgrade<-Upgrade_MordorOrcPitLevel3 LevelUpUpgrade<-Upgrade_MordorOrcPitLevel2 LevelUpUpgrade<-Upgrade_MordorOrcPitLevel3 SubObjectsUpgrade<-Upgrade_MordorOrcPitLevel2 SubObjectsUpgrade<-Upgrade_MordorOrcPitLevel3 SubObjectsUpgrade<-Upgrade_StructureLevel1 | MordorPorterCommandSet:5:Command_PorterConstructMordorOrcPit |
| MordorSiegeWorks | cost 600, time 30s, CP 0; upgrade modules: CommandSetUpgrade<-Upgrade_MordorSiegeWorksLevel2 CommandSetUpgrade<-Upgrade_MordorSiegeWorksLevel3 LevelUpUpgrade<-Upgrade_MordorSiegeWorksLevel2 LevelUpUpgrade<-Upgrade_MordorSiegeWorksLevel3 SubObjectsUpgrade<-Upgrade_MordorSiegeWorksLevel2 SubObjectsUpgrade<-Upgrade_MordorSiegeWorksLevel3 SubObjectsUpgrade<-Upgrade_StructureLevel1 | MordorPorterCommandSet:8:Command_PorterConstructMordorSiegeWorks |
| MordorSlaughterHouse | cost 300, time 15s, CP 0; upgrade modules: SubObjectsUpgrade<-Upgrade_StructureLevel1 SubObjectsUpgrade<-Upgrade_StructureLevel2 SubObjectsUpgrade<-Upgrade_StructureLevel3 WeaponSetUpgrade<-Upgrade_StructureLevel3 | MordorPorterCommandSet:3:Command_PorterConstructMordorSlaughterHouse |
| MordorTrollCage | cost 750, time 45s, CP 0; upgrade modules: CommandSetUpgrade<-Upgrade_MordorTrollCageLevel2 CommandSetUpgrade<-Upgrade_MordorTrollCageLevel3 LevelUpUpgrade<-Upgrade_MordorTrollCageLevel2 LevelUpUpgrade<-Upgrade_MordorTrollCageLevel3 SubObjectsUpgrade<-Upgrade_MordorTrollCageLevel2 SubObjectsUpgrade<-Upgrade_MordorTrollCageLevel3 SubObjectsUpgrade<-Upgrade_StructureLevel1 | MordorPorterCommandSet:7:Command_PorterConstructMordorTrollCage |
| MordorWallCatapultExpansion | cost 1200, time 30s, CP 0; upgrade modules: AttributeModifierUpgrade<-Upgrade_MordorFortressMorgulSorcery CommandSetUpgrade<-Upgrade_HasWallTrebuchet|Upgrade_TrebuchetTurret CommandSetUpgrade<-Upgrade_TrebuchetTurret ModelConditionUpgrade<-Upgrade_MordorFortressMorgulSorcery | MordorFortressExpansionPadCornerCommandSet:1:Command_ConstructMordorWallCatapultExpansion |

### horde

| name | data | reached from |
|---|---|---|
| MordorArcherHorde | cost 250, time 30s, CP 40; powers: SpecialPowerModule(SpecialAbilityCaptureBuilding); upgrade modules: LevelUpUpgrade<-Upgrade_MordorBasicTraining StatusBitsUpgrade<-Upgrade_MordorFireArrows StatusBitsUpgrade<-Upgrade_MordorHeavyArmor | MordorOrcPitCommandSetLevel1:2:Command_ConstructMordorOrcArcherHorde<br>MordorOrcPitCommandSetLevel2:2:Command_ConstructMordorOrcArcherHorde<br>MordorOrcPitCommandSetLevel3:2:Command_ConstructMordorOrcArcherHorde |
| MordorBlackOrcHorde | cost 400, time 25s, CP 60; powers: SpecialPowerModule(SpecialAbilityCaptureBuilding); upgrade modules: LevelUpUpgrade<-Upgrade_MordorBasicTraining StatusBitsUpgrade<-Upgrade_MordorForgedBlades StatusBitsUpgrade<-Upgrade_MordorHeavyArmor | MordorOrcPitCommandSetLevel1:3:Command_ConstructMordorBlackOrcFighterHorde<br>MordorOrcPitCommandSetLevel2:3:Command_ConstructMordorBlackOrcFighterHorde<br>MordorOrcPitCommandSetLevel3:3:Command_ConstructMordorBlackOrcFighterHorde |
| MordorCorsairsOfUmbarHorde | cost 400, time 30s, CP 60; powers: SpecialPowerModule(SpecialAbilityCaptureBuilding); upgrade modules: LevelUpUpgrade<-Upgrade_MordorBasicTraining StatusBitsUpgrade<-Upgrade_MordorForgedBlades | MordorHaradrimPalaceCommandSetLevel1:3:Command_ConstructMordorCorsairsOfUmbarHorde<br>MordorHaradrimPalaceCommandSetLevel2:3:Command_ConstructMordorCorsairsOfUmbarHorde<br>MordorHaradrimPalaceCommandSetLevel3:3:Command_ConstructMordorCorsairsOfUmbarHorde |
| MordorEasterlingHorde | cost 350, time 25s, CP 75; powers: SpecialPowerModule(SpecialAbilityCaptureBuilding); upgrade modules: LevelUpUpgrade<-Upgrade_MordorBasicTraining StatusBitsUpgrade<-Upgrade_MordorHeavyArmor | MordorHaradrimPalaceCommandSetLevel1:1:Command_ConstructEasterlingHorde<br>MordorHaradrimPalaceCommandSetLevel2:1:Command_ConstructEasterlingHorde<br>MordorHaradrimPalaceCommandSetLevel3:1:Command_ConstructEasterlingHorde |
| MordorFighterHorde | cost 100, time 25s, CP 40; powers: SpecialPowerModule(SpecialAbilityCaptureBuilding); upgrade modules: LevelUpUpgrade<-Upgrade_MordorBasicTraining StatusBitsUpgrade<-Upgrade_MordorForgedBlades StatusBitsUpgrade<-Upgrade_MordorHeavyArmor | MordorOrcPitCommandSetLevel1:1:Command_ConstructMordorOrcFighterHorde<br>MordorOrcPitCommandSetLevel2:1:Command_ConstructMordorOrcFighterHorde<br>MordorOrcPitCommandSetLevel3:1:Command_ConstructMordorOrcFighterHorde |
| MordorHaradrimArcherHorde | cost 425, time 45s, CP 72; powers: SpecialPowerModule(SpecialAbilityCaptureBuilding) SpecialPowerModule(SpecialAbilityHaradrimArcherBarbedArrow); upgrade modules: LevelUpUpgrade<-Upgrade_MordorBasicTraining StatusBitsUpgrade<-Upgrade_MordorFireArrows StatusBitsUpgrade<-Upgrade_MordorHeavyArmor | MordorHaradrimPalaceCommandSetLevel1:4:Command_ConstructMordorHaradrimArcherHorde<br>MordorHaradrimPalaceCommandSetLevel2:4:Command_ConstructMordorHaradrimArcherHorde<br>MordorHaradrimPalaceCommandSetLevel3:4:Command_ConstructMordorHaradrimArcherHorde |

### unit

| name | data | reached from |
|---|---|---|
| MordorArcher | cost 0, time 1s, CP 2; upgrade modules: ArmorUpgrade<-Upgrade_MordorHeavyArmor SubObjectsUpgrade<-Upgrade_MordorFireArrows SubObjectsUpgrade<-Upgrade_MordorHeavyArmor WeaponSetUpgrade<-Upgrade_MordorFireArrows | MordorArcherHorde:InitialPayload |
| MordorAttackTroll | cost 900, time 45s, CP 45; powers: SpecialPowerModule(SpecialAbilityDominateTroll); upgrade modules: ModelConditionUpgrade<-Upgrade_AllFactionUpgrade RadiateFearUpdate<- | MordorTrollCageCommandSetLevel1:3:Command_ConstructMordorAttackTroll<br>MordorTrollCageCommandSetLevel2:3:Command_ConstructMordorAttackTroll<br>MordorTrollCageCommandSetLevel3:3:Command_ConstructMordorAttackTroll |
| MordorBatteringRam | cost 400, time 30s, CP 25 | MordorSiegeWorksCommandSetLevel1:1:Command_ConstructMordorBatteringRam<br>MordorSiegeWorksCommandSetLevel2:1:Command_ConstructMordorBatteringRam<br>MordorSiegeWorksCommandSetLevel3:1:Command_ConstructMordorBatteringRam |
| MordorBlackOrc | cost 0, time 1s, CP 5; upgrade modules: ArmorUpgrade<-Upgrade_MordorHeavyArmor AttributeModifierUpgrade<-Upgrade_MordorFighterFearless SubObjectsUpgrade<-Upgrade_MordorForgedBlades SubObjectsUpgrade<-Upgrade_MordorHeavyArmor | MordorBlackOrcHorde:InitialPayload |
| MordorBlackRiderHorde | cost 2000, time 40s, CP 150; powers: SpecialPowerModule(SpecialAbilityBlackRiderMorgulBlade) SpecialPowerModule(SpecialAbilityCaptureBuilding) SpecialPowerModule(SpecialAbilityFakeLeadership) SpecialPowerModule(SpecialAbilityScreech) SpecialPowerModule(SpecialAbilityToggleMounted); upgrade modules: AttributeModifierAuraUpdate<-Upgrade_MiniHordeLvl2 AutoHealBehavior<-Upgrade_MiniHordeLvl3 ModelConditionUpgrade<-Upgrade_MiniHordeLvl2 RadiateFearUpdate<-Upgrade_BlackRiderDreadVisage UnpauseSpecialPowerUpgrade<-Upgrade_MiniHordeLvl10 UnpauseSpecialPowerUpgrade<-Upgrade_MiniHordeLvl2 UnpauseSpecialPowerUpgrade<-Upgrade_MiniHordeLvl4 | MordorSiegeWorksCommandSetLevel1:5:Command_ConstructMordorBlackRiderHorde<br>MordorSiegeWorksCommandSetLevel2:5:Command_ConstructMordorBlackRiderHorde<br>MordorSiegeWorksCommandSetLevel3:5:Command_ConstructMordorBlackRiderHorde |
| MordorCatapult | cost 300, time 30s, CP 25 | MordorSiegeWorksCommandSetLevel1:2:Command_ConstructMordorCatapult<br>MordorSiegeWorksCommandSetLevel2:2:Command_ConstructMordorCatapult<br>MordorSiegeWorksCommandSetLevel3:2:Command_ConstructMordorCatapult |
| MordorCorsairsOfUmbar | cost 0, time 1s, CP 4; upgrade modules: AttributeModifierUpgrade<-Upgrade_GondorFighterFearless SubObjectsUpgrade<-Upgrade_MordorForgedBlades|Upgrade_WildForgedBlades | MordorCorsairsOfUmbarHorde:InitialPayload |
| MordorDrummerTroll | cost 500, time 45s, CP 35; powers: SpecialPowerModule(SpecialAbilityRoar); upgrade modules: AttributeModifierAuraUpdate<- AutoHealBehavior<-Upgrade_MountainTrollVeterancy | MordorTrollCageCommandSetLevel1:2:Command_ConstructMordorDrummerTroll<br>MordorTrollCageCommandSetLevel2:2:Command_ConstructMordorDrummerTroll<br>MordorTrollCageCommandSetLevel3:2:Command_ConstructMordorDrummerTroll |
| MordorEasterling | cost 0, time 1s, CP 5; upgrade modules: ArmorUpgrade<-Upgrade_MordorHeavyArmor StatusBitsUpgrade<-Upgrade_MordorForgedBlades SubObjectsUpgrade<-Upgrade_MordorForgedBlades SubObjectsUpgrade<-Upgrade_MordorHeavyArmor | MordorEasterlingHorde:InitialPayload |
| MordorFighter | cost 0, time 1s, CP 2; upgrade modules: ArmorUpgrade<-Upgrade_MordorHeavyArmor AttributeModifierUpgrade<-Upgrade_MordorFighterFearless SubObjectsUpgrade<-Upgrade_MordorForgedBlades SubObjectsUpgrade<-Upgrade_MordorHeavyArmor | MordorFighterHorde:InitialPayload |
| MordorHaradrimArcher | cost 0, time 1s, CP 6; upgrade modules: ArmorUpgrade<-Upgrade_MordorHeavyArmor SubObjectsUpgrade<-Upgrade_MordorFireArrows SubObjectsUpgrade<-Upgrade_MordorHeavyArmor WeaponSetUpgrade<-Upgrade_MordorFireArrows | MordorHaradrimArcherHorde:InitialPayload |
| MordorHaradrimRiderHorde | cost 500, time 25s, CP 50; powers: SpecialPowerModule(SpecialAbilityCaptureBuilding); upgrade modules: LevelUpUpgrade<-Upgrade_MordorBasicTraining StatusBitsUpgrade<-Upgrade_MordorForgedBlades StatusBitsUpgrade<-Upgrade_MordorHeavyArmor | MordorHaradrimPalaceCommandSetLevel1:2:Command_ConstructMordorHaradrimRiderHorde<br>MordorHaradrimPalaceCommandSetLevel2:2:Command_ConstructMordorHaradrimRiderHorde<br>MordorHaradrimPalaceCommandSetLevel3:2:Command_ConstructMordorHaradrimRiderHorde |
| MordorMountainTroll | cost 500, time 45s, CP 45; powers: SpecialPowerModule(SpecialAbilityMountainTrollBored) SpecialPowerModule(SpecialAbilityTrollDropTree) | MordorTrollCageCommandSetLevel1:1:Command_ConstructMordorMountainTroll<br>MordorTrollCageCommandSetLevel2:1:Command_ConstructMordorMountainTroll<br>MordorTrollCageCommandSetLevel3:1:Command_ConstructMordorMountainTroll |
| MordorMumakil | cost 1000, time 60s, CP 60; upgrade modules: FireWeaponWhenDeadBehavior<- LevelUpUpgrade<-Upgrade_MordorBasicTraining ModelConditionUpgrade<-Upgrade_AllFactionUpgrade ModelConditionUpgrade<-Upgrade_MumakilLevel2 SubObjectsUpgrade<-Upgrade_MumakilLevel1 | MordorMumakilPenCommandSetLevel1:1:Command_ConstructMordorMumakil<br>MordorMumakilPenCommandSetLevel2:1:Command_ConstructMordorMumakil<br>MordorMumakilPenCommandSetLevel3:1:Command_ConstructMordorMumakil |
| MordorPorter | cost 500, time 30s, CP 0; powers: SpecialPowerModule(SpecialAbilityPorterExtinguishFire) | FactionMordor:StartingUnit0<br>FactionMordor:StartingUnit1<br>MordorFortressCommandSet:1:Command_ConstructMordorPorter<br>(+1) |
| MordorSpellBook | cost 0, time 1s, CP 0; powers: DarknessSpecialPower(SpellBookDarkness) DevastateSpecialPower(SpellBookDevastation) FreezingRainSpecialPower(SpellBookFreezingRain) OCLSpecialPower(SpellBookArrowVolleyEvil) OCLSpecialPower(SpellBookAvalanche) OCLSpecialPower(SpellBookAwakenWyrm) OCLSpecialPower(SpellBookBalrogAlly) OCLSpecialPower(SpellBookBarricade) OCLSpecialPower(SpellBookCaveBats) OCLSpecialPower(SpellBookChillWind) OCLSpecialPower(SpellBookCrebain) OCLSpecialPower(SpellBookDragonAlly) OCLSpecialPower(SpellBookDragonStrike) OCLSpecialPower(SpellBookEvilBombard) OCLSpecialPower(SpellBookEvilEyeofSauron) OCLSpecialPower(SpellBookEyeofSauron) OCLSpecialPower(SpellBookFrozenLand) OCLSpecialPower(SpellBookPalantirVision) OCLSpecialPower(SpellBookRainOfFire) OCLSpecialPower(SpellBookSpiderlingAllies) OCLSpecialPower(SpellBookSummonGiants) OCLSpecialPower(SpellBookSummonOrcs) OCLSpecialPower(SpellBookSummonShadeOfWolf) OCLSpecialPower(SpellBookSummonWights) OCLSpecialPower(SpellBookWatcherAlly) OCLSpecialPower(SpellBookWildMenAllies) ProductionSpeedBonus(SpellBookCalltheHorde) ProductionSpeedBonus(SpellBookMordorCalltheHorde) ScavengerSpecialPower(SpellBookScavenger) SpecialPowerModule(SpellBookBlight) SpecialPowerModule(SpellBookFueltheFires) SpecialPowerModule(SpellBookIndustry) SpecialPowerModule(SpellBookRebuild) SpecialPowerModule(SpellBookSnowbind) SpecialPowerModule(SpellBookWarChant) TaintSpecialPower(SpellBookIsengardTaint) TaintSpecialPower(SpellBookTaint) UntamedAllegianceSpecialPower(SpellBookUntamedAllegiance); upgrade modules: BuildableHeroListUpgrade<-Upgrade_RingHero | FactionMordor:SpellBookMp<br>the started skirmish's base |
| MordorWorker | cost 20, time 5s, CP 5; powers: SpecialPowerModule(SpecialAbilityHarvest) | MordorLumberMillCommandSetLevel1:1:Command_BuildMordorWorker |

### hero

| name | data | reached from |
|---|---|---|
| KhamulFellBeast | cost 3000, time 60s, CP 50; powers: SpecialPowerModule(SpecialAbilityLevelAttack) SpecialPowerModule(SpecialAbilityScreech) SpecialPowerModule(SpecialAbilityToggleMountedFromFellBeast); upgrade modules: AutoHealBehavior<- | FactionMordor:BuildableHeroesMP |
| MordorGothmog | cost 1200, time 30s, CP 25; powers: SpecialPowerModule(SpecialAbilityCaptureBuilding) SpecialPowerModule(SpecialAbilityFakeLeadership) SpecialPowerModule(SpecialAbilityFakeLeadership2) SpecialPowerModule(SpecialAbilityLurtzCarnage); upgrade modules: AttributeModifierAuraUpdate<-Upgrade_GothmogDayoftheOrc AttributeModifierAuraUpdate<-Upgrade_GothmogIronHand AutoHealBehavior<- UnpauseSpecialPowerUpgrade<-Upgrade_GothmogCarnage UnpauseSpecialPowerUpgrade<-Upgrade_GothmogDayoftheOrc UnpauseSpecialPowerUpgrade<-Upgrade_GothmogIronHand | FactionMordor:BuildableHeroesMP |
| MordorMouthOfSauron | cost 1500, time 45s, CP 50; powers: SpecialPowerModule(SpecialAbilityCaptureBuilding) SpecialPowerModule(SpecialAbilityMouthOfSauronDissent) SpecialPowerModule(SpecialAbilityMouthOfSauronDoubt) SpecialPowerModule(SpecialAbilitySarumanFireball) SpecialPowerModule(SpecialAbilityToggleMounted); upgrade modules: AttributeModifierUpgrade<-Upgrade_ElvenGift AutoHealBehavior<- UnpauseSpecialPowerUpgrade<-Upgrade_MouthOfSauronDissent UnpauseSpecialPowerUpgrade<-Upgrade_MouthOfSauronDoubt UnpauseSpecialPowerUpgrade<-Upgrade_MouthOfSauronEvilEye | FactionMordor:BuildableHeroesMP |
| MordorSauron_RingHero | cost 10000, time 300s, CP 0; powers: SpecialPowerModule(FiresOfDoomSpecialPower) SpecialPowerModule(SpecialAbilityScreech); upgrade modules: AttributeModifierAuraUpdate<- AutoHealBehavior<- RemoveUpgradeUpgrade<-Upgrade_WildFaction|Upgrade_IsengardFaction|Upgrade_MordorFaction|Upgrade_GoblinFaction|Upgrade_AngmarFaction | FactionMordor:BuildableRingHeroesMP |
| MordorWitchKingOnFellBeast | cost 5000, time 60s, CP 75; powers: SpecialPowerModule(SpecialAbilityCurseEnemy) SpecialPowerModule(SpecialAbilityFakeLeadership) SpecialPowerModule(SpecialAbilityLevelAttack) SpecialPowerModule(SpecialAbilityScreech) SpecialPowerModule(SpecialAbilityToggleMountedFromFellBeast); upgrade modules: AttributeModifierAuraUpdate<-Upgrade_DreadVisage AutoHealBehavior<- ModelConditionUpgrade<-Upgrade_MordorFaction RadiateFearUpdate<-Upgrade_DreadVisage UnpauseSpecialPowerUpgrade<-Upgrade_DreadVisage UnpauseSpecialPowerUpgrade<-Upgrade_HourOfTheWitchKing UnpauseSpecialPowerUpgrade<-Upgrade_Screech | FactionMordor:BuildableHeroesMP |
| MorgomirFellBeast | cost 3000, time 60s, CP 50; powers: SpecialPowerModule(SpecialAbilityLevelAttack) SpecialPowerModule(SpecialAbilityScreech) SpecialPowerModule(SpecialAbilityToggleMountedFromFellBeast); upgrade modules: AutoHealBehavior<- | FactionMordor:BuildableHeroesMP |

### upgrade

| name | data | reached from |
|---|---|---|
| Upgrade_HasWallTrebuchet | OBJECT, cost 600, time 30s | MordorUpgradeCommandSetBuyNewCatapult:1:Command_MordorPurchaseNewWallCatapult |
| Upgrade_MordorBasicTraining | OBJECT, cost 250, time 10s | MordorArcherHordeCommandSet:4:Command_PurchaseUpgradeMordorBasicTraining<br>MordorBlackOrcHordeCommandSet:5:Command_PurchaseUpgradeMordorBasicTraining<br>MordorCorsairsOfUmbarHordeCommandSet:3:Command_PurchaseUpgradeMordorBasicTraining<br>(+6) |
| Upgrade_MordorFireArrows | OBJECT, cost 250, time 10s | MordorArcherHordeCommandSet:3:Command_PurchaseUpgradeMordorFireArrows<br>MordorHaradrimArcherCommandSet:5:Command_PurchaseUpgradeMordorFireArrows<br>MordorHaradrimArcherHordeCommandSet:5:Command_PurchaseUpgradeMordorFireArrows<br>(+1) |
| Upgrade_MordorForgedBlades | OBJECT, cost 250, time 10s | MordorBlackOrcHordeCommandSet:3:Command_PurchaseUpgradeMordorForgedBlades<br>MordorCorsairsOfUmbarHordeCommandSet:2:Command_PurchaseUpgradeMordorForgedBlades<br>MordorEasterlingHordeCommandSet:5:Command_PurchaseUpgradeMordorForgedBlades<br>(+1) |
| Upgrade_MordorFortressDoomPyres | OBJECT, cost 500, time 5s | MordorFortressCommandSet:8:Command_PurchaseUpgradeMordorFortressDoomPyres |
| Upgrade_MordorFortressFireArrowsTrigger | OBJECT, cost 1500, time 30s | MordorFortressCommandSet:10:Command_PurchaseUpgradeMordorFortressFireArrows |
| Upgrade_MordorFortressGorgorothSpire | OBJECT, cost 2500, time 30s | MordorFortressCommandSet:13:Command_PurchaseUpgradeMordorFortressGorgorothSpire |
| Upgrade_MordorFortressLavaMoat | OBJECT, cost 1000, time 30s | MordorFortressCommandSet:9:Command_PurchaseUpgradeMordorFortressLavaMoat |
| Upgrade_MordorFortressMagmaCauldrons | OBJECT, cost 1500, time 30s | MordorFortressCommandSet:11:Command_PurchaseUpgradeMordorFortressMagmaCauldrons |
| Upgrade_MordorFortressMorgulSorceryTrigger | OBJECT, cost 2000, time 30s | MordorFortressCommandSet:12:Command_PurchaseUpgradeMordorFortressMorgulSorcery |
| Upgrade_MordorHaradrimPalaceLevel2 | OBJECT, cost 500, time 30s | MordorHaradrimPalaceCommandSetLevel1:7:Command_UpgradeMordorHaradrimPalaceLevel2 |
| Upgrade_MordorHaradrimPalaceLevel3 | OBJECT, cost 750, time 45s | MordorHaradrimPalaceCommandSetLevel2:7:Command_UpgradeMordorHaradrimPalaceLevel3 |
| Upgrade_MordorHeavyArmor | OBJECT, cost 250, time 10s | MordorBlackOrcHordeCommandSet:4:Command_PurchaseUpgradeMordorHeavyArmor<br>MordorEasterlingHordeCommandSet:4:Command_PurchaseUpgradeMordorHeavyArmor<br>MordorHaradrimArcherCommandSet:4:Command_PurchaseUpgradeMordorHeavyArmor<br>(+2) |
| Upgrade_MordorMumakilPenLevel2 | OBJECT, cost 250, time 30s | MordorMumakilPenCommandSetLevel1:2:Command_UpgradeMordorMumakilPenLevel2 |
| Upgrade_MordorMumakilPenLevel3 | OBJECT, cost 500, time 45s | MordorMumakilPenCommandSetLevel2:2:Command_UpgradeMordorMumakilPenLevel3 |
| Upgrade_MordorOrcPitLevel2 | OBJECT, cost 350, time 30s | MordorOrcPitCommandSetLevel1:5:Command_UpgradeMordorOrcPitLevel2 |
| Upgrade_MordorOrcPitLevel3 | OBJECT, cost 500, time 45s | MordorOrcPitCommandSetLevel2:5:Command_UpgradeMordorOrcPitLevel3 |
| Upgrade_MordorSiegeWorksLevel2 | OBJECT, cost 500, time 30s | MordorSiegeWorksCommandSetLevel1:7:Command_UpgradeMordorSiegeWorksLevel2 |
| Upgrade_MordorSiegeWorksLevel3 | OBJECT, cost 1000, time 45s | MordorSiegeWorksCommandSetLevel2:7:Command_UpgradeMordorSiegeWorksLevel3 |
| Upgrade_MordorTrollCageLevel2 | OBJECT, cost 500, time 30s | MordorTrollCageCommandSetLevel1:4:Command_UpgradeMordorTrollCageLevel2 |
| Upgrade_MordorTrollCageLevel3 | OBJECT, cost 1000, time 60s | MordorTrollCageCommandSetLevel2:4:Command_UpgradeMordorTrollCageLevel3 |
| Upgrade_SentryTowerFireArrows | OBJECT, cost 500, time 30s | MordorBattleTowerCommandSet:1:Command_PurchaseSentryTowerFireArrowsUpgrade |
| Upgrade_TechnologyMordorBasicTraining | PLAYER, cost 1000, time 30s | MordorOrcPitCommandSetLevel1:4:Command_PurchaseTechnologyMordorBasicTraining<br>MordorOrcPitCommandSetLevel2:4:Command_PurchaseTechnologyMordorBasicTraining<br>MordorOrcPitCommandSetLevel3:4:Command_PurchaseTechnologyMordorBasicTraining |
| Upgrade_TechnologyMordorFireArrows | PLAYER, cost 1000, time 30s | MordorHaradrimPalaceCommandSetLevel1:5:Command_PurchaseTechnologyMordorFireArrows<br>MordorHaradrimPalaceCommandSetLevel2:5:Command_PurchaseTechnologyMordorFireArrows<br>MordorHaradrimPalaceCommandSetLevel3:5:Command_PurchaseTechnologyMordorFireArrows |
| Upgrade_TechnologyMordorForgedBlades | PLAYER, cost 1000, time 30s | MordorSiegeWorksCommandSetLevel1:3:Command_PurchaseTechnologyMordorForgedBlades<br>MordorSiegeWorksCommandSetLevel2:3:Command_PurchaseTechnologyMordorForgedBlades<br>MordorSiegeWorksCommandSetLevel3:3:Command_PurchaseTechnologyMordorForgedBlades |
| Upgrade_TechnologyMordorHeavyArmor | PLAYER, cost 1000, time 30s | MordorSiegeWorksCommandSetLevel1:4:Command_PurchaseTechnologyMordorHeavyArmor<br>MordorSiegeWorksCommandSetLevel2:4:Command_PurchaseTechnologyMordorHeavyArmor<br>MordorSiegeWorksCommandSetLevel3:4:Command_PurchaseTechnologyMordorHeavyArmor |
| Upgrade_TestBuilding | OBJECT, cost 0, time 10s | MordorBaseTestCommandSet:1:Command_UpgradeTest |
| Upgrade_TestBuilding_2 | OBJECT, cost 0, time 10s | MordorBaseTestCommandSet:2:Command_UpgradeTest_2 |

### power

| name | data | reached from |
|---|---|---|
| FiresOfDoomSpecialPower | SPECIAL_POWER, type 125, reload 1200 frames, options 0x20 | SauronCommandSet:2:Command_FiresOfDoom |
| SpecialAbilityBlackRiderMorgulBlade | SPECIAL_POWER, type 129, reload 225 frames, options 0x1 | EvilMenBlackRiderCommandSet:3:Command_BlackRiderMorgulBlade |
| SpecialAbilityCaptureBuilding | SPECIAL_POWER, type 29, reload 0 frames, options 0x100103 | EvilMenBlackRiderCommandSet:12:Command_CaptureBuilding<br>GothmogCommandSet:12:Command_CaptureBuilding<br>MordorArcherHordeCommandSet:12:Command_CaptureBuilding<br>(+8) |
| SpecialAbilityCurseEnemy | SPECIAL_POWER, type 147, reload 1500 frames, options 0x1 | MordorWitchKingRidingFellBeastCommandSet:5:Command_HourOfTheWitchKing |
| SpecialAbilityDominateTroll | SPECIAL_POWER, type 144, reload 300 frames, options 0x3 | MordorAttackTrollCommandSet:2:Command_DominateTroll |
| SpecialAbilityFakeLeadership | SPECIAL_POWER, type 135, reload 1 frames, options 0x10000000 | EvilMenBlackRiderCommandSet:2:Command_BlackRiderDreadVisage<br>GothmogCommandSet:2:Command_GothmogIronHand<br>MordorWitchKingRidingFellBeastCommandSet:3:Command_WitchKingDreadVisage |
| SpecialAbilityFakeLeadership2 | SPECIAL_POWER, type 135, reload 1 frames, options 0x10000000 | GothmogCommandSet:4:Command_GothmogDayoftheOrc |
| SpecialAbilityGorgorothSpireFireball | SPECIAL_POWER, type 98, reload 1500 frames, options 0x400020 | MordorFortressCommandSet:4:Command_FireWeaponMordorFortressSpireFireball |
| SpecialAbilityGrabPassenger | SPECIAL_POWER, type 39, reload 0 frames, options 0x202017 | MordorMountainTrollCommandSet:16:Command_SpecialAbilityTrollGrabTree |
| SpecialAbilityHaradrimArcherBarbedArrow | SPECIAL_POWER, type 129, reload 300 frames, options 0x3 | MordorHaradrimArcherHordeCommandSet:2:Command_HaradrimArcherBarbedArrowAbility |
| SpecialAbilityHarvest | SPECIAL_POWER, type 141, reload 5 frames, options 0x100100 | MordorWorkerCommandSet:1:Command_Harvest |
| SpecialAbilityLurtzCarnage | SPECIAL_POWER, type 60, reload 600 frames, options 0x0 | GothmogCommandSet:3:Command_SpecialAbilityGothmogCarnage |
| SpecialAbilityMordorFortressMagmaCauldrons | SPECIAL_POWER, type 53, reload 150 frames, options 0x0 | MordorFortressCommandSet:3:Command_FireWeaponMordorFortressMagmaCauldrons |
| SpecialAbilityMouthOfSauronDissent | SPECIAL_POWER, type 47, reload 600 frames, options 0x220 | MordorMouthOfSauronCommandSet:4:Command_MouthOfSauronDissent |
| SpecialAbilityMouthOfSauronDoubt | SPECIAL_POWER, type 47, reload 300 frames, options 0x220 | MordorMouthOfSauronCommandSet:3:Command_MouthOfSauronDoubt |
| SpecialAbilityPorterExtinguishFire | SPECIAL_POWER, type 75, reload 25 frames, options 0x220 | MordorPorterCommandSet:2:Command_PorterExtinguishFire |
| SpecialAbilityRoar | SPECIAL_POWER, type 137, reload 900 frames, options 0x0 | MordorDrummerTrollCommandSet:2:Command_SpecialAbilityRoar |
| SpecialAbilitySarumanFireball | SPECIAL_POWER, type 130, reload 300 frames, options 0x7 | MordorMouthOfSauronCommandSet:5:Command_MouthOfSauronEvilEye |
| SpecialAbilityScreech | SPECIAL_POWER, type 137, reload 900 frames, options 0x0 | EvilMenBlackRiderCommandSet:4:Command_SpecialAbilityScreechBlackRider<br>KhamulFellBeastCommandSet:3:Command_SpecialAbilityScreech<br>MordorWitchKingRidingFellBeastCommandSet:4:Command_SpecialAbilityScreechWitchKing<br>(+1) |
| SpecialAbilityToggleMounted | SPECIAL_POWER, type 56, reload 5 frames, options 0x41000000 | MordorMouthOfSauronCommandSet:2:Command_MountHorseMouthOfSauron |
| SpecialAbilityToggleMountedFromFellBeast | SPECIAL_POWER, type 56, reload 5 frames, options 0x41000020 | KhamulFellBeastCommandSet:2:Command_KhamulDismount<br>MordorWitchKingRidingFellBeastCommandSet:2:Command_WitchKingDismount |
| SpellBookArrowVolleyEvil | SPELL_BOOK, type 114, reload 1800 frames, options 0x20 | MordorSpellBookCommandSet:7:Command_SpellBookArrowVolleyEvil |
| SpellBookAwakenWyrm | SPELL_BOOK, type 120, reload 2700 frames, options 0x20 | MordorSpellBookCommandSet:10:Command_SpellBookAwakenWyrm |
| SpellBookBalrogAlly | SPELL_BOOK, type 124, reload 3600 frames, options 0x20 | MordorSpellBookCommandSet:11:Command_SpellBookBalrogAlly |
| SpellBookBarricade | SPELL_BOOK, type 106, reload 900 frames, options 0x20 | MordorSpellBookCommandSet:5:Command_SpellBookBarricade |
| SpellBookDarkness | SPELL_BOOK, type 119, reload 1800 frames, options 0x0 | MordorSpellBookCommandSet:9:Command_SpellBookDarkness |
| SpellBookEvilBombard | SPELL_BOOK, type 98, reload 2700 frames, options 0x20 | MordorSpellBookCommandSet:8:Command_SpellBookEvilBombard |
| SpellBookEyeofSauron | SPELL_BOOK, type 105, reload 900 frames, options 0x20 | MordorSpellBookCommandSet:2:Command_SpellBookEyeofSauron |
| SpellBookIndustry | SPELL_BOOK, type 111, reload 1800 frames, options 0x20 | MordorSpellBookCommandSet:4:Command_SpellBookIndustry |
| SpellBookRainOfFire | SPELL_BOOK, type 125, reload 3600 frames, options 0x20 | MordorSpellBookCommandSet:12:Command_SpellBookRainOfFire |
| SpellBookTaint | SPELL_BOOK, type 104, reload 900 frames, options 0x20 | MordorSpellBookCommandSet:1:Command_SpellBookTaint |
| SpellBookUntamedAllegiance | SPELL_BOOK, type 113, reload 1800 frames, options 0x20 | MordorSpellBookCommandSet:6:Command_SpellBookUntamedAllegiance |
| SpellBookWarChant | SPELL_BOOK, type 107, reload 900 frames, options 0x20 | MordorSpellBookCommandSet:3:Command_SpellBookWarChant |

### science

| name | data | reached from |
|---|---|---|
| SCIENCE_ArrowVolleyEvil | PURCHASE_SCIENCE | MordorSpellStoreCommandSet:7:Command_PurchaseSpellArrowVolleyEvil |
| SCIENCE_AwakenWyrm | PURCHASE_SCIENCE | MordorSpellStoreCommandSet:10:Command_PurchaseSpellAwakenWyrm |
| SCIENCE_BalrogAlly | PURCHASE_SCIENCE | MordorSpellStoreCommandSet:11:Command_PurchaseSpellBalrogAlly |
| SCIENCE_Barricade | PURCHASE_SCIENCE | MordorSpellStoreCommandSet:6:Command_PurchaseSpellBarricade |
| SCIENCE_Darkness | PURCHASE_SCIENCE | MordorSpellStoreCommandSet:9:Command_PurchaseSpellDarkness |
| SCIENCE_EvilBombard | PURCHASE_SCIENCE | MordorSpellStoreCommandSet:8:Command_PurchaseSpellEvilBombard |
| SCIENCE_EyeofSauron | PURCHASE_SCIENCE | MordorSpellStoreCommandSet:2:Command_PurchaseSpellEyeofSauron |
| SCIENCE_Industry | PURCHASE_SCIENCE | MordorSpellStoreCommandSet:4:Command_PurchaseSpellIndustry |
| SCIENCE_RainOfFire | PURCHASE_SCIENCE | MordorSpellStoreCommandSet:12:Command_PurchaseSpellRainOfFire |
| SCIENCE_Taint | PURCHASE_SCIENCE | MordorSpellStoreCommandSet:1:Command_PurchaseSpellTaint |
| SCIENCE_UntamedAllegiance | PURCHASE_SCIENCE | MordorSpellStoreCommandSet:5:Command_PurchaseSpellUntamedAllegiance |
| SCIENCE_WarChant | PURCHASE_SCIENCE | MordorSpellStoreCommandSet:3:Command_PurchaseSpellWarChant |

### command

| name | data | reached from |
|---|---|---|
| Command_ArcherBombard | FIRE_WEAPON | MordorArcherHordeCommandSet:2:Command_ArcherBombard<br>MordorHaradrimArcherCommandSet:3:Command_ArcherBombard<br>MordorHaradrimArcherHordeCommandSet:3:Command_ArcherBombard |
| Command_AttackMove | ATTACK_MOVE | EvilMenBlackRiderCommandSet:13:Command_AttackMove<br>GenericCommandSet:13:Command_AttackMove<br>GothmogCommandSet:13:Command_AttackMove<br>(+21) |
| Command_BloodThirstySacrifice | BLOODTHIRSTY | MordorBlackOrcHordeCommandSet:2:Command_BloodThirstySacrifice<br>MordorFighterHordeCommandSet:2:Command_BloodThirstySacrifice |
| Command_BombardEvil | FIRE_WEAPON | MordorCatapultCommandSet:2:Command_BombardEvil |
| Command_ChargeAttack | FIRE_WEAPON | MordorMumakilCommandSet:2:Command_ChargeAttack |
| Command_CreateAHeroReviveSlot | REVIVE | MordorFortressCommandSet:16:Command_CreateAHeroReviveSlot |
| Command_DrummerTrollLeadership | NONE | MordorDrummerTrollCommandSet:3:Command_DrummerTrollLeadership |
| Command_GenericReviveSlot1 | REVIVE | MordorFortressCommandSet:17:Command_GenericReviveSlot1 |
| Command_GenericReviveSlot2 | REVIVE | MordorFortressCommandSet:18:Command_GenericReviveSlot2 |
| Command_GenericReviveSlot3 | REVIVE | MordorFortressCommandSet:19:Command_GenericReviveSlot3 |
| Command_GenericReviveSlot4 | REVIVE | MordorFortressCommandSet:20:Command_GenericReviveSlot4 |
| Command_GenericReviveSlot5 | REVIVE | MordorFortressCommandSet:21:Command_GenericReviveSlot5 |
| Command_GenericReviveSlot6 | REVIVE | MordorFortressCommandSet:22:Command_GenericReviveSlot6 |
| Command_GenericReviveSlot7 | REVIVE | MordorFortressCommandSet:23:Command_GenericReviveSlot7 |
| Command_MordorEasterlingPorcupineFormation | HORDE_TOGGLE_FORMATION | MordorEasterlingHordeCommandSet:2:Command_MordorEasterlingPorcupineFormation |
| Command_RadialBack | POP_VISIBLE_COMMAND_RANGE | MordorFortressCommandSet:14:Command_RadialBack<br>MordorFortressCommandSet:24:Command_RadialBack |
| Command_RingHeroReviveSlot | REVIVE | MordorFortressCommandSet:15:Command_RingHeroReviveSlot |
| Command_SelectRevivablesMordorFortress | PUSH_VISIBLE_COMMAND_RANGE | MordorFortressCommandSet:2:Command_SelectRevivablesMordorFortress |
| Command_SelectUpgradesMordorFortress | PUSH_VISIBLE_COMMAND_RANGE | MordorFortressCommandSet:5:Command_SelectUpgradesMordorFortress |
| Command_Sell | SELL | MordorBattleTowerCommandSet:6:Command_Sell<br>MordorFortressCommandSet:6:Command_Sell<br>MordorHaradrimPalaceCommandSetLevel1:6:Command_Sell<br>(+17) |
| Command_SetStanceAggressive | SET_STANCE | EvilMenBlackRiderCommandSet:17:Command_SetStanceAggressive<br>GenericCommandSet:17:Command_SetStanceAggressive<br>GothmogCommandSet:17:Command_SetStanceAggressive<br>(+20) |
| Command_SetStanceBattle | SET_STANCE | EvilMenBlackRiderCommandSet:16:Command_SetStanceBattle<br>GenericCommandSet:16:Command_SetStanceBattle<br>GothmogCommandSet:16:Command_SetStanceBattle<br>(+20) |
| Command_SetStanceHoldGround | SET_STANCE | EvilMenBlackRiderCommandSet:18:Command_SetStanceHoldGround<br>GenericCommandSet:18:Command_SetStanceHoldGround<br>GothmogCommandSet:18:Command_SetStanceHoldGround<br>(+20) |
| Command_SpecialAbilityTrollFindTastyOrc | WAKE_AUTO_PICKUP | MordorMountainTrollCommandSet:4:Command_SpecialAbilityTrollFindTastyOrc |
| Command_SpecialAbilityTrollFindTree | WAKE_AUTO_PICKUP | MordorMountainTrollCommandSet:3:Command_SpecialAbilityTrollFindTree |
| Command_Stop | STOP | EvilMenBlackRiderCommandSet:14:Command_Stop<br>GenericCommandSet:14:Command_Stop<br>GothmogCommandSet:14:Command_Stop<br>(+22) |
| Command_ToggleCatAutoAcquire | TOGGLE_NO_AUTO_ACQUIRE | MordorCatapultCommandSet:4:Command_ToggleCatAutoAcquire |
| Command_ToggleCatapultWeapon | TOGGLE_WEAPON | MordorCatapultCommandSet:3:Command_ToggleCatapultWeapon |
| Command_ToggleCorsairWeapon | TOGGLE_WEAPONSET | MordorCorsairsOfUmbarHordeCommandSet:4:Command_ToggleCorsairWeapon |
| Command_ToggleMountainTrollRockThrow | TOGGLE_WEAPONSET | MordorMountainTrollCommandSet:2:Command_ToggleMountainTrollRockThrow |
| Command_ToggleStance | TOGGLE_STANCE | EvilMenBlackRiderCommandSet:1:Command_ToggleStance<br>GenericCommandSet:1:Command_ToggleStance<br>GothmogCommandSet:1:Command_ToggleStance<br>(+20) |

### behaviour modules without a runtime

| module class | templates |
|---|---|
| AimWeaponBehavior | MordorHaradrimArcher |
| AnnounceBirthAndDeathBehavior | MordorSauron_RingHero |
| AudioLoopUpgrade | MordorFortressCitadel |
| AutoPickUpUpdate | MordorMountainTroll |
| BaseUpgrade | MordorBase |
| BloodthirstyUpdate | MordorArcherHorde, MordorBlackOrcHorde, MordorFighterHorde |
| BoredUpdate | MordorMountainTroll |
| CitadelSlaughterHordeContain | MordorFortressCitadel |
| ClickReactionBehavior | MordorAttackTroll, MordorMountainTroll |
| DoCommandUpgrade | MordorMountainTroll |
| EvacuateDamage | MordorBattleTower |
| FlingPassengerSpecialAbilityUpdate | MordorMountainTroll |
| GeometryUpgrade | MordorFortressCitadel, MordorHaradrimPalace, MordorLumberMill, MordorMumakilPen, MordorSlaughterHouse |
| GrabPassengerSpecialPower | MordorMountainTroll |
| HordeTransportContainDamage | MordorMumakil |
| ObjectCreationUpgrade | MordorFortressCitadel, MordorWallCatapultExpansion |
| PickupStuffUpdate | MordorArcherHorde, MordorBlackOrcHorde, MordorBlackRiderHorde, MordorCorsairsOfUmbarHorde, MordorEasterlingHorde, MordorFighterHorde, MordorHaradrimArcherHorde, MordorHaradrimRiderHorde |
| RunOffMapBehavior | MordorMumakil |
| SiegeDeployHordeSpecialPower | MordorBlackOrcHorde, MordorFighterHorde |
| SlaughterHordeContain | MordorSlaughterHouse |
| SlaveWatcherBehavior | MordorWallCatapultExpansion |
| SupplyCenterCreate | MordorLumberMill |
| SupplyCenterDockUpdate | MordorLumberMill |
| TransitionDamageFX | MordorCatapult |
