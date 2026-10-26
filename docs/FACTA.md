# FACT-A inventory: Men, Elves and Dwarves

Generated from the output of `FACT-A inventory` ([engine/tests/test_hud_facta_inventory.cpp](../engine/tests/test_hud_facta_inventory.cpp)) on the retail data. Each faction is walked from a real
skirmish start (the castle unpacked, the builders, the PlayerTemplate heroes); every button's product is walked in turn, and so are the command sets an
upgrade switches to (CommandSetUpgrade: the building levels) and the objects the modules make or lay (ObjectCreationUpgrade, ReplaceSelfUpgrade: the
gates and towers, WallHubBehavior: the wall pieces). For each object: its cost, build time (s), command points, command set and the modules still
unported (S-140); for each button: the slot, the command, what it should do, its target and the state the HUD shows. The costs, times, command points,
command sets, targets, HUD states and result counts below are the recorded 2026-10-11 review round 4 snapshot, not a new test run. The unported-module
lists and the status notes have been checked against source at `c4ee740e5b8cf1f060aec0fad9ad4c9c5bb1ae64`. Reproducing the retail-data inventory needs
both ROTWK_INSTALL and BFME2_INSTALL; without them the test prints SKIP. The `should` column describes command intent, not verified implementation;
upgrade-set rows are read from the data without buying the upgrade in the inventory test.

A button that is NOT-SHOWN was not found on the control bar in that inventory: spell-book entries are in the power tree, CaptureBuilding is a context
command, and the Ivory Tower and Mighty Catapult powers hide until their upgrade (HIDE_WHILE_DISABLED). REVIVE slots use hero purchase or dead-hero
records, not just dead heroes: skirmish initialization creates purchase records for BuildableHeroesMP. A missing record hides its slot; prerequisites
can hide a restricted slot, while money and command-point shortages show can't-afford. The historical NOT-SHOWN rows do not describe current hero
availability (ControlBar::reviveButton, HeroSystem::initPlayer).

## Results (FACT-A tests, 2026-10-11, review round 4)

| faction | objects / buttons (+ upgrade sets) | units built | upgrades bought | abilities pressed / ran | sold | expansions | wall pieces replaced | castle upgrades (CASTLE_UPGRADE) | builder constructions | heroes recruited / revived | battalion upgrades (weapon / armour / level) | spells inventoried / cast / effect-verified (stopped) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Men | 56 / 368 (+52) | 10 | 24 | 39 / 39 | 22 | 7 | 5 | 1 / 1 | 12 | 8 / 7 | 21 (7 / 6 / 7) | 12 / 12 / 12 |
| Elves | 47 / 315 (+31) | 10 | 18 | 32 / 31 | 17 | 4 | 2 | 0 / 0 | 10 | 7 / 6 | 17 (4 / 5 / 6) | 12 / 12 / 11 (Flood: S-1987) |
| Dwarves | 46 / 295 (+61) | 9 | 19 | 30 / 29 | 18 | 8 | 5 | 0 / 0 | 9 | 5 / 4 | 19 (5 / 5 / 4) | 12 / 12 / 12 |

What the tests check besides the press: the price and the build time of every unit and upgrade; each ability's effect against a table read from
the data (damage, heal, buff, debuff, fear, summon, levels, a timer refresh, a capture, the object's state); the citadel's castle upgrades on the
castle, the keep, the pads, the members and the walls within WallUpgradeRadius; a sold expansion freeing its plot; each wall segment upgrade replaced
by its piece at the segment's place with the upgrade's cost as its paid cost; the ring hero taking the Ring (the player loses Upgrade_RingHero) and
dropping it at its death (no revive slot); the citadel's CASTLE_UPGRADE (the House of Healing) charged, completed, shown owned through the castle and
healing a wounded battalion within its radius that did not heal before; each battalion upgrade in real hits (the idle mood scan off, both sides
toughened): the health an enemy battalion loses a hit to a fresh twin of the unit and then to the unit after its purchase (a weapon set must take
more), the health the unit loses a hit to enemy fighters and archers before and after (an armour must lose less), Basic Training raising the
battalion's rank; every power of the book's command set (by name) bought, cast and showing its world effect against the same scene without it (a
summon by the names of the objects its OCL or egg makes, a heal, a buff or debuff, damage; the Flood was counted as stopped by S-1987 in the results above,
but the pinned test now requires its damage; Rebuild healing a wounded building; Cloud Break's sunny weather and sunbeams; the Riches' modifier on the economy building; the
Arrow Volley's and the Bombard's hits on the enemy battalion within their weapon's size).

Source status since those results:
- the Flood's wave runs through FloodUpdate (SpellEffectModules); S-1987 is closed and the spell test expects damage, not a stopped cast;
- Basic Training's LevelUpUpgrade calls HordeContain::gainMemberLevel for each gained level; both contained and entering members are leveled, so the
  old S-635 member-level item is no longer open;
- DoCommandUpgrade is bound and runs SPECIAL_POWER, SPELL_BOOK and TOGGLE_WEAPONSET; other command types remain stopped by S-5145;
- HordeDispatchSpecialPower is bound and dispatches to the members; its target substitution remains S-2001;
- TransitionDamageFX is bound and emits damage-state FX, OCL and particle events; its rubble-neighbour and other remaining differences are S-686.
  These three bound classes have been removed from the unported lists below; that does not mean their remaining stops are closed.

Known open items at the pinned source:
- the Noldor Warrior horde's ArcherBombard: the horde's position attack (PLAY-2's groupAttackPosition) ends at once;
- the Dwarven Demolisher's deploy (S-1984);
- the eagle's SlaveWatcherBehavior link in ObjectCreationUpgrade (now S-2007, including the former S-1985 item);
- the wall links of a replaced segment (S-1986);
- the Dwarven Riches' PRODUCTION income bonus (S-253);
- seen while measuring: a GondorKnightHorde or DwarvenGuardianHorde ordered onto a second victim right after a fight stays in AI_ATTACK_OBJECT and
  lands nothing (the order after that works); the test measures the before hits on a fresh twin;
- the context click on a burning building (S-286), the porters' extinguisher (FireLogicNugget, S-321); capture context on a capturable structure now
  issues the special-power message through CommandTranslator::evaluateCapture, but capture powers with ObjectFilter remain stopped by S-2002;
- the unported modules listed per object below (GeometryUpgrade, PickupStuffUpdate, the CAH modules, ...).


## Men (Gondor)

### MenSpellBook
cost 0, time 1.0, cp 0, set `MenSpellBookCommandSet` (not selected by a click); unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_SpellBookHeal | SPELL_BOOK | spell book (power tree) | SpellBookHeal | NOT-SHOWN |
| 2 | Command_SpellBookRallyingCall | SPELL_BOOK | spell book (power tree) | SpellBookRallyingCall | NOT-SHOWN |
| 3 | Command_SpellBookRebuild | SPELL_BOOK | spell book (power tree) | SpellBookRebuild | NOT-SHOWN |
| 4 | Command_SpellBookTomBombadil | SPELL_BOOK | spell book (power tree) | SpellBookTomBombadil | NOT-SHOWN |
| 5 | Command_SpellBookHobbitAllies | SPELL_BOOK | spell book (power tree) | SpellBookHobbitAllies | NOT-SHOWN |
| 6 | Command_SpellBookLoneTower | SPELL_BOOK | spell book (power tree) | SpellBookSpawnLoneTower | NOT-SHOWN |
| 7 | Command_SpellBookArrowVolleyGood | SPELL_BOOK | spell book (power tree) | SpellBookArrowVolleyGood | NOT-SHOWN |
| 8 | Command_SpellBookCloudBreak | SPELL_BOOK | spell book (power tree) | SpellBookCloudBreak | NOT-SHOWN |
| 9 | Command_SpellBookRohanAllies | SPELL_BOOK | spell book (power tree) | SpellBookRohanAllies | NOT-SHOWN |
| 10 | Command_SpellBookDunedainAllies | SPELL_BOOK | spell book (power tree) | SpellBookDunedainAllies | NOT-SHOWN |
| 11 | Command_SpellBookArmyoftheDead | SPELL_BOOK | spell book (power tree) | SpellBookArmyoftheDead | NOT-SHOWN |
| 12 | Command_SpellBookEarthquake | SPELL_BOOK | spell book (power tree) | SpellBookEarthquake | NOT-SHOWN |

### MenFortress
cost 5000, time 120.0, cp 0, set `-` (not selected by a click); unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|

### MenPorter
cost 500, time 30.0, cp 0, set `MenPorterCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_Stop | STOP | stops |  | enabled |
| 2 | Command_PorterExtinguishFire | SPECIAL_POWER | casts the power | SpecialAbilityPorterExtinguishFire | enabled |
| 3 | Command_PorterConstructMenFarm | DOZER_CONSTRUCT | builder places the structure | GondorFarm | enabled |
| 4 | Command_PorterConstructMenBarracks | DOZER_CONSTRUCT | builder places the structure | GondorBarracks | enabled |
| 5 | Command_PorterConstructMenArcheryRange | DOZER_CONSTRUCT | builder places the structure | GondorArcherRange | enabled |
| 6 | Command_PorterConstructMenStable | DOZER_CONSTRUCT | builder places the structure | GondorStable | enabled |
| 7 | Command_PorterConstructMenWorkshop | DOZER_CONSTRUCT | builder places the structure | GondorWorkshop | enabled |
| 8 | Command_PorterConstructMenSentryTower | DOZER_CONSTRUCT | builder places the structure | GondorBattleTower | enabled |
| 9 | Command_PorterConstructMenWell | DOZER_CONSTRUCT | builder places the structure | GondorWell | enabled |
| 10 | Command_PorterConstructMenStatue | DOZER_CONSTRUCT | builder places the structure | GondorStatue | enabled |
| 11 | Command_PorterConstructMenBlacksmith | DOZER_CONSTRUCT | builder places the structure | GondorForge | enabled |
| 12 | Command_PorterConstructMenMarketPlace | DOZER_CONSTRUCT | builder places the structure | GondorMarketPlace | enabled |
| 13 | Command_PorterConstructMenWallHub | DOZER_CONSTRUCT | builder places the structure | MenWallHubSmall | enabled |
| 14 | Command_PorterConstructMenFortress | DOZER_CONSTRUCT | builder places the structure | MenFortress | enabled |

### MenFortressExpansionPadSide
cost 1, time 5.0, cp 0, set `MenFortressExpansionPadSideCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ConstructMenTrebuchetSideExpansion | FOUNDATION_CONSTRUCT | builds on the plot | MenTrebuchetSideExpansion | enabled |
| 2 | Command_ConstructMenArrowTowerExpansion | FOUNDATION_CONSTRUCT | builds on the plot | MenArrowTowerExpansion | enabled |
| 3 | Command_ConstructMenGarrisonDormitory | FOUNDATION_CONSTRUCT | builds on the plot | MenGarrisonTowerExpansion | enabled |

### MenFortressExpansionPadCorner
cost 1, time 5.0, cp 0, set `MenFortressExpansionPadCornerCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ConstructMenTrebuchetExpansion | FOUNDATION_CONSTRUCT | builds on the plot | MenTrebuchetExpansion | enabled |
| 2 | Command_ConstructMenArrowTowerExpansion | FOUNDATION_CONSTRUCT | builds on the plot | MenArrowTowerExpansion | enabled |
| 3 | Command_ConstructMenGarrisonDormitory | FOUNDATION_CONSTRUCT | builds on the plot | MenGarrisonTowerExpansion | enabled |
| 4 | Command_ConstructMenWallHubExpansion | FOUNDATION_CONSTRUCT | builds on the plot | MenWallHubSmallExpansion | enabled |

### MenFortressCitadel
cost 5000, time 120.0, cp 0, set `MenFortressCommandSet`; unported: AudioLoopUpgrade, CitadelSlaughterHordeContain, DefectionHelper, FiringTrackerHelper, GeometryUpgrade, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ConstructMenPorter | UNIT_BUILD | builds the unit / horde | MenPorter | enabled |
| 2 | Command_SelectRevivablesMenFortress | PUSH_VISIBLE_COMMAND_RANGE | opens a page |  | enabled |
| 3 | Command_FireWeaponMenFortressBoilingOil | SPECIAL_POWER | casts the power | SpecialAbilityMenFortressBoilingOil | restricted |
| 4 | Command_SpecialAbilityIvoryTowerVision | SPECIAL_POWER | casts the power | SpecialAbilityMenFortressIvoryTowerVision | NOT-SHOWN |
| 5 | Command_SelectUpgradesMenFortress | PUSH_VISIBLE_COMMAND_RANGE | opens a page |  | enabled |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |
| 8 | Command_PurchaseUpgradeMenFortressBanners | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_MenFortressBanners | enabled |
| 9 | Command_PurchaseUpgradeMenFortressHouseOfHealing | CASTLE_UPGRADE | castle upgrade | Upgrade_MenFortressHouseOfHealing | restricted |
| 10 | Command_PurchaseUpgradeMenFortressFlamingMunitions | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GoodFortressFlamingMunitionsTrigger | enabled |
| 11 | Command_PurchaseUpgradeMenFortressBoilingOil | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_MenFortressBoilingOil | enabled |
| 12 | Command_PurchaseUpgradeMenFortressNumenorStonework | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_MenFortressNumenorStoneworkTrigger | enabled |
| 13 | Command_PurchaseUpgradeMenFortressIvoryTower | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_MenFortressIvoryTower | restricted |
| 14 | Command_RadialBack | POP_VISIBLE_COMMAND_RANGE | closes a page |  | enabled |
| 15 | Command_RingHeroReviveSlot | REVIVE | recruits / revives a hero |  | NOT-SHOWN |
| 16 | Command_CreateAHeroReviveSlot | REVIVE | recruits / revives a hero |  | NOT-SHOWN |
| 17 | Command_GenericReviveSlot1 | REVIVE | recruits / revives a hero |  | enabled |
| 18 | Command_GenericReviveSlot2 | REVIVE | recruits / revives a hero |  | enabled |
| 19 | Command_GenericReviveSlot3 | REVIVE | recruits / revives a hero |  | enabled |
| 20 | Command_GenericReviveSlot4 | REVIVE | recruits / revives a hero |  | enabled |
| 21 | Command_GenericReviveSlot5 | REVIVE | recruits / revives a hero |  | enabled |
| 22 | Command_GenericReviveSlot6 | REVIVE | recruits / revives a hero |  | enabled |
| 23 | Command_GenericReviveSlot7 | REVIVE | recruits / revives a hero |  | enabled |
| 24 | Command_RadialBack | POP_VISIBLE_COMMAND_RANGE | closes a page |  | enabled |

### CreateAHero
cost 500, time 30.0, cp 50, set `CreateAHeroCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, LocomotorSetUpgrade, RecoveryHelper, ReplaceObjectUpdate, StoreObjectsSpecialPower, TeleportToCasterSpecialPower

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 2 | Command_Stop | STOP | stops |  | enabled |

### RohanEowyn
cost 1200, time 30.0, cp 25, set `RohanEowynCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_EowynSmite | SPECIAL_POWER | casts the power | SpecialAbilitySmite | enabled |
| 3 | Command_MountHorseEowyn | SPECIAL_POWER | casts the power | SpecialAbilityToggleMounted | restricted |
| 4 | Command_Disguise | SPECIAL_POWER | casts the power | SpecialAbilityDisguise | restricted |
| 5 | Command_EowynShieldMaiden | SPECIAL_POWER | casts the power | SpecialAbilityEowynShieldMaiden | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### RohanEomer
cost 1000, time 30.0, cp 25, set `RohanEomerCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_MountHorseEomer | SPECIAL_POWER | casts the power | SpecialAbilityToggleMounted | enabled |
| 3 | Command_RohanEomerHorseLordLeadership | SPECIAL_POWER | casts the power | SpecialAbilityFakeLeadership | enabled |
| 4 | Command_RohanEomerOutlawLeadership | SPECIAL_POWER | casts the power | SpecialAbilityFakeLeadership2 | restricted |
| 5 | Command_RohanEomerSpear | SPECIAL_POWER | casts the power | SpecialAbilitySpearThrow | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### GondorBoromir
cost 1250, time 30.0, cp 50, set `BoromirCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_SpecialAbilityHornOfGondor | SPECIAL_POWER | casts the power | SpecialAbilityHornOfGondor | restricted |
| 3 | Command_SpecialAbilityBoromirForGondor | SPECIAL_POWER | casts the power | SpecialAbilityFakeLeadership | restricted |
| 4 | Command_SpecialAbilityCaptainOfGondorBoromir | SPECIAL_POWER | casts the power | SpecialAbilityKingsFavor | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### RohanTheoden
cost 1200, time 30.0, cp 50, set `RohanTheodenCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_TheodenToggleMounted | SPECIAL_POWER | casts the power | SpecialAbilityToggleMounted | enabled |
| 3 | Command_SpecialAbilityTheodenLeadership | SPECIAL_POWER | casts the power | SpecialAbilityFakeLeadership | enabled |
| 4 | Command_SpecialAbilityKingsFavor | SPECIAL_POWER | casts the power | SpecialAbilityKingsFavor | restricted |
| 5 | Command_SpecialAbilityTheodenGloriousCharge | SPECIAL_POWER | casts the power | SpecialAbilityTheodenGloriousCharge | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### GondorFaramir
cost 1200, time 30.0, cp 50, set `GondorFaramirCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_ToggleFaramirWeapon | TOGGLE_WEAPONSET | swaps the weapon set |  | enabled |
| 3 | Command_SpecialAbilityWoundArrow | SPECIAL_POWER | casts the power | SpecialAbilityWoundArrow | restricted |
| 4 | Command_ToggleMounted | SPECIAL_POWER | casts the power | SpecialAbilityToggleMounted | restricted |
| 5 | Command_FaramirFakeLeadershipButton | SPECIAL_POWER | casts the power | SpecialAbilityFakeLeadership | restricted |
| 6 | Command_SpecialAbilityCaptainOfGondor | SPECIAL_POWER | casts the power | SpecialAbilityKingsFavor | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### GondorAragornMP
cost 2000, time 30.0, cp 50, set `GondorAragornCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_SpecialAbilityAthelas | SPECIAL_POWER | casts the power | SpecialAbilityAragornAthelas | enabled |
| 3 | Command_SpecialAbilityBladeMaster | SPECIAL_POWER | casts the power | SpecialAbilityAragornBladeMaster | restricted |
| 4 | Command_AragornFakeLeadershipButton | SPECIAL_POWER | casts the power | SpecialAbilityFakeLeadership | restricted |
| 5 | Command_SpecialAbilityElendil | SPECIAL_POWER | casts the power | SpecialAbilityAragornElendil | restricted |
| 6 | Command_SpawnOathbreakers | SPECIAL_POWER | casts the power | SuperweaponSpawnOathbreakers | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### GondorGandalf
cost 3000, time 45.0, cp 75, set `GondorGandalfCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_SpecialAbilityWizardBlast | SPECIAL_POWER | casts the power | SpecialAbilityWizardBlast | enabled |
| 3 | Command_GondorGandalfLightningSword | SPECIAL_POWER | casts the power | SpecialAbilityLightningSword | restricted |
| 4 | Command_GandalfShadowfax | SPECIAL_POWER | casts the power | SpecialAbilityToggleMounted | restricted |
| 5 | Command_GondorGandalfIstariLight | SPECIAL_POWER | casts the power | SpecialAbilityIstariLight | restricted |
| 6 | Command_SpecialAbilityWordOfPower | SPECIAL_POWER | casts the power | SpecialAbilityWordOfPower | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### ElvenGaladriel_RingHero
cost 10000, time 300.0, cp 0, set `GaladrielCommandSet`; unported: AnnounceBirthAndDeathBehavior, DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_SpecialPowerSpawnTornado | SPECIAL_POWER | casts the power | SpecialPowerSpawnTornado | enabled |
| 3 | Command_SpecialAbilityTerribleFury | SPECIAL_POWER | casts the power | SpecialAbilityScreech | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### GondorFarm
cost 300, time 15.0, cp 0, set `SellableCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GeometryUpgrade, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### GondorBarracks
cost 400, time 30.0, cp 0, set `GondorBarracksCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GeometryUpgrade, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ConstructGondorFighterHorde | UNIT_BUILD | builds the unit / horde | GondorFighterHorde | enabled |
| 2 | Command_ConstructRohanSpearmenHorde | UNIT_BUILD | builds the unit / horde | RohanSpearmenHorde | enabled |
| 3 | Command_ConstructGondorTowerShieldGuardHorde | UNIT_BUILD | builds the unit / horde | GondorTowerShieldGuardHorde | restricted |
| 4 | Command_PurchaseUpgradeGondorBarracksLevel2 | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorBarracksLevel2 | enabled |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

after its upgrade, set `GondorBarracksCommandSetLevel2`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_ConstructGondorFighterHorde | UNIT_BUILD | builds the unit / horde | GondorFighterHorde |
| 2 | Command_ConstructRohanSpearmenHorde | UNIT_BUILD | builds the unit / horde | RohanSpearmenHorde |
| 3 | Command_ConstructGondorTowerShieldGuardHorde | UNIT_BUILD | builds the unit / horde | GondorTowerShieldGuardHorde |
| 4 | Command_PurchaseUpgradeGondorBarracksLevel3 | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorBarracksLevel3 |
| 6 | Command_Sell | SELL | sells the structure |  |

after its upgrade, set `GondorBarracksCommandSetLevel3`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_ConstructGondorFighterHorde | UNIT_BUILD | builds the unit / horde | GondorFighterHorde |
| 2 | Command_ConstructRohanSpearmenHorde | UNIT_BUILD | builds the unit / horde | RohanSpearmenHorde |
| 3 | Command_ConstructGondorTowerShieldGuardHorde | UNIT_BUILD | builds the unit / horde | GondorTowerShieldGuardHorde |
| 6 | Command_Sell | SELL | sells the structure |  |

### GondorArcherRange
cost 300, time 30.0, cp 0, set `GondorArcheryCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GeometryUpgrade, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ConstructGondorArcherHorde | UNIT_BUILD | builds the unit / horde | GondorArcherHorde | enabled |
| 2 | Command_ConstructGondorRangerHorde | UNIT_BUILD | builds the unit / horde | GondorRangerHorde | restricted |
| 3 | Command_PurchaseTechnologyGondorFireArrows | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyGondorFireArrows | restricted |
| 4 | Command_PurchaseUpgradeGondorArcheryRangeLevel2 | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorArcheryRangeLevel2 | enabled |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

after its upgrade, set `GondorArcheryCommandSetLevel2`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_ConstructGondorArcherHorde | UNIT_BUILD | builds the unit / horde | GondorArcherHorde |
| 2 | Command_ConstructGondorRangerHorde | UNIT_BUILD | builds the unit / horde | GondorRangerHorde |
| 3 | Command_PurchaseTechnologyGondorFireArrows | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyGondorFireArrows |
| 4 | Command_PurchaseUpgradeGondorArcheryRangeLevel3 | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorArcheryRangeLevel3 |
| 6 | Command_Sell | SELL | sells the structure |  |

after its upgrade, set `GondorArcheryCommandSetLevel3`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_ConstructGondorArcherHorde | UNIT_BUILD | builds the unit / horde | GondorArcherHorde |
| 2 | Command_ConstructGondorRangerHorde | UNIT_BUILD | builds the unit / horde | GondorRangerHorde |
| 3 | Command_PurchaseTechnologyGondorFireArrows | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyGondorFireArrows |
| 6 | Command_Sell | SELL | sells the structure |  |

### GondorStable
cost 600, time 30.0, cp 0, set `GondorStablesCommandSet`; unported: DefectionHelper, GeometryUpgrade, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ConstructGondorKnightHorde | UNIT_BUILD | builds the unit / horde | GondorKnightHorde | enabled |
| 2 | Command_ConstructRohanRohirrimHorde | UNIT_BUILD | builds the unit / horde | RohanRohirrimHorde | restricted |
| 3 | Command_ConstructGondorKnightsofDolHorde | UNIT_BUILD | builds the unit / horde | GondorKnightsofDolHorde | restricted |
| 4 | Command_PurchaseUpgradeGondorStableLevel2 | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorStableLevel2 | enabled |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

after its upgrade, set `GondorStablesCommandSetLevel2`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_ConstructGondorKnightHorde | UNIT_BUILD | builds the unit / horde | GondorKnightHorde |
| 2 | Command_ConstructRohanRohirrimHorde | UNIT_BUILD | builds the unit / horde | RohanRohirrimHorde |
| 3 | Command_ConstructGondorKnightsofDolHorde | UNIT_BUILD | builds the unit / horde | GondorKnightsofDolHorde |
| 4 | Command_PurchaseUpgradeGondorStableLevel3 | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorStableLevel3 |
| 6 | Command_Sell | SELL | sells the structure |  |

after its upgrade, set `GondorStablesCommandSetLevel3`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_ConstructGondorKnightHorde | UNIT_BUILD | builds the unit / horde | GondorKnightHorde |
| 2 | Command_ConstructRohanRohirrimHorde | UNIT_BUILD | builds the unit / horde | RohanRohirrimHorde |
| 3 | Command_ConstructGondorKnightsofDolHorde | UNIT_BUILD | builds the unit / horde | GondorKnightsofDolHorde |
| 6 | Command_Sell | SELL | sells the structure |  |

### GondorWorkshop
cost 700, time 30.0, cp 0, set `GondorWorkshopCommandSet`; unported: DefectionHelper, GeometryUpgrade, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ConstructGondorTrebuchet | UNIT_BUILD | builds the unit / horde | GondorTrebuchet | enabled |
| 2 | Command_PurchaseTechnologyGondorFireStones | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyGondorFireStones | restricted |
| 3 | Command_PurchaseUpgradeGondorWorkshopLevel2 | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorWorkshopLevel2 | enabled |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

after its upgrade, set `GondorWorkshopCommandSetLevel2`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_ConstructGondorTrebuchet | UNIT_BUILD | builds the unit / horde | GondorTrebuchet |
| 2 | Command_PurchaseTechnologyGondorFireStones | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyGondorFireStones |
| 3 | Command_PurchaseUpgradeGondorWorkshopLevel3 | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorWorkshopLevel3 |
| 6 | Command_Sell | SELL | sells the structure |  |

after its upgrade, set `GondorWorkshopCommandSetLevel3`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_ConstructGondorTrebuchet | UNIT_BUILD | builds the unit / horde | GondorTrebuchet |
| 2 | Command_PurchaseTechnologyGondorFireStones | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyGondorFireStones |
| 6 | Command_Sell | SELL | sells the structure |  |

### GondorBattleTower
cost 500, time 60.0, cp 0, set `GenericSentryTowerCommandSet`; unported: DefectionHelper, EvacuateDamage, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ExitGarrison | EXIT_CONTAINER | lets a rider out |  | restricted |
| 2 | Command_ExitGarrison | EXIT_CONTAINER | lets a rider out |  | restricted |
| 3 | Command_PurchaseSentryTowerFireArrowsUpgrade | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_SentryTowerFireArrows | enabled |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### GondorWell
cost 300, time 30.0, cp 0, set `SellableCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### GondorStatue
cost 300, time 30.0, cp 0, set `SellableCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### GondorForge
cost 1000, time 60.0, cp 0, set `GondorForgeCommandSet`; unported: DefectionHelper, GeometryUpgrade, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_PurchaseTechnologyGondorBasicTraining | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyGondorBasicTraining | enabled |
| 2 | Command_PurchaseTechnologyGondorForgedBlades | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyGondorForgedBlades | restricted |
| 3 | Command_PurchaseTechnologyGondorHeavyArmor | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyGondorHeavyArmor | restricted |
| 4 | Command_PurchaseUpgradeGondorForgeLevel2 | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorForgeLevel2 | enabled |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

after its upgrade, set `GondorForgeCommandSetLevel2`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_PurchaseTechnologyGondorBasicTraining | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyGondorBasicTraining |
| 2 | Command_PurchaseTechnologyGondorForgedBlades | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyGondorForgedBlades |
| 3 | Command_PurchaseTechnologyGondorHeavyArmor | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyGondorHeavyArmor |
| 4 | Command_PurchaseUpgradeGondorForgeLevel3 | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorForgeLevel3 |
| 6 | Command_Sell | SELL | sells the structure |  |

after its upgrade, set `GondorForgeCommandSetLevel3`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_PurchaseTechnologyGondorBasicTraining | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyGondorBasicTraining |
| 2 | Command_PurchaseTechnologyGondorForgedBlades | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyGondorForgedBlades |
| 3 | Command_PurchaseTechnologyGondorHeavyArmor | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyGondorHeavyArmor |
| 6 | Command_Sell | SELL | sells the structure |  |

### GondorMarketPlace
cost 1500, time 45.0, cp 0, set `MarketPlaceCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_PurchaseUpgradeIronOre | PLAYER_UPGRADE | researches the player upgrade | Upgrade_MarketplaceUpgradeIronOre | enabled |
| 2 | Command_PurchaseUpgradeGrandHarvest | PLAYER_UPGRADE | researches the player upgrade | Upgrade_MarketplaceUpgradeGrandHarvest | enabled |
| 3 | Command_PurchaseUpgradeDefiance | PLAYER_UPGRADE | researches the player upgrade | Upgrade_MarketplaceUpgradeDefiance | enabled |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### MenWallHubSmall
cost 200, time 25.0, cp 0, set `GondorCastleWallHubSmallCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_GondorCastleWallHub_BeginWallSpan | DOZER_CONSTRUCT | builder places the structure | MenWallHubSmall | enabled |
| 4 | Command_CancelWallSpan | CANCEL_NEIGHBORHOOD | cancels the plot placement |  | restricted |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |
- makes or lays MenWallSegmentSmall
- makes or lays MenWallHubSmall
- makes or lays MenWallCliffCap

### MenTrebuchetSideExpansion
cost 1200, time 30.0, cp 0, set `SellableCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper, SlaveWatcherBehavior

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

after its upgrade, set `GondorCastleWallUpgradeCommandSetBuyNewTreb`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_PurchaseNewWallTreb | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_HasWallTrebuchet |
| 6 | Command_Sell | SELL | sells the structure |  |

after its upgrade, set `SellableCommandSet`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  |
- makes or lays MenTrebuchetFortress

### MenArrowTowerExpansion
cost 500, time 30.0, cp 0, set `SellableCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### MenGarrisonTowerExpansion
cost 500, time 10.0, cp 0, set `DormitoryCommandSet`; unported: AllowBannerSpawnUpgrade, DefectionHelper, EvacuateDamage, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ExitGarrison | EXIT_CONTAINER | lets a rider out |  | restricted |
| 2 | Command_ExitGarrison | EXIT_CONTAINER | lets a rider out |  | restricted |
| 3 | Command_ExitGarrison | EXIT_CONTAINER | lets a rider out |  | restricted |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### MenTrebuchetExpansion
cost 1200, time 30.0, cp 0, set `SellableCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper, SlaveWatcherBehavior

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

after its upgrade, set `GondorCastleWallUpgradeCommandSetBuyNewTreb`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_PurchaseNewWallTreb | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_HasWallTrebuchet |
| 6 | Command_Sell | SELL | sells the structure |  |

after its upgrade, set `SellableCommandSet`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  |
- makes or lays MenTrebuchetFortress

### MenWallHubSmallExpansion
cost 200, time 25.0, cp 0, set `GondorCastleWallHubSmallCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_GondorCastleWallHub_BeginWallSpan | DOZER_CONSTRUCT | builder places the structure | MenWallHubSmall | enabled |
| 4 | Command_CancelWallSpan | CANCEL_NEIGHBORHOOD | cancels the plot placement |  | restricted |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |
- makes or lays MenWallSegmentSmall
- makes or lays MenWallHubSmall
- makes or lays MenWallCliffCap

### GondorFighterHorde
cost 250, time 20.0, cp 60, set `GondorFighterHordeCommandSet`; unported: CommandButtonHuntUpdate, DefectionHelper, FiringTrackerHelper, GuardingHelper, PickupStuffUpdate, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_ToggleFormationGondorFighter | HORDE_TOGGLE_FORMATION | toggles the horde formation |  | enabled |
| 3 | Command_PurchaseUpgradeGondorForgedBlades | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorForgedBlades | restricted |
| 4 | Command_PurchaseUpgradeGondorHeavyArmor | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorHeavyArmor | restricted |
| 5 | Command_PurchaseUpgradeGondorBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorBasicTraining | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### RohanSpearmenHorde
cost 300, time 20.0, cp 60, set `RohanPikeCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, PickupStuffUpdate, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_RohanPorcupineFormation | HORDE_TOGGLE_FORMATION | toggles the horde formation |  | enabled |
| 3 | Command_PurchaseUpgradeGondorForgedBlades | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorForgedBlades | restricted |
| 4 | Command_PurchaseUpgradeGondorHeavyArmor | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorHeavyArmor | restricted |
| 5 | Command_PurchaseUpgradeGondorBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorBasicTraining | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### GondorTowerShieldGuardHorde
cost 450, time 20.0, cp 75, set `GondorTowerShieldGuardCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, PickupStuffUpdate, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_TowerGuardPorcupineFormation | HORDE_TOGGLE_FORMATION | toggles the horde formation |  | enabled |
| 3 | Command_PurchaseUpgradeGondorForgedBlades | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorForgedBlades | restricted |
| 4 | Command_PurchaseUpgradeGondorHeavyArmor | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorHeavyArmor | restricted |
| 5 | Command_PurchaseUpgradeGondorBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorBasicTraining | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### GondorArcherHorde
cost 300, time 20.0, cp 60, set `GondorArcherHordeCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, PickupStuffUpdate, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_ArcherBombard | FIRE_WEAPON | fires the weapon slot |  | enabled |
| 3 | Command_PurchaseUpgradeGondorFireArrows | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorFireArrows | restricted |
| 4 | Command_PurchaseUpgradeGondorHeavyArmor | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorHeavyArmor | restricted |
| 5 | Command_PurchaseUpgradeGondorBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorBasicTraining | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### GondorRangerHorde
cost 500, time 30.0, cp 72, set `GondorRangerHordeCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, PickupStuffUpdate, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_ArcherBombard | FIRE_WEAPON | fires the weapon slot |  | enabled |
| 3 | Command_MenLongShotAbility | SPECIAL_POWER | casts the power | SpecialAbilityMenLongShot | restricted |
| 4 | Command_PurchaseUpgradeGondorFireArrows | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorFireArrows | restricted |
| 5 | Command_PurchaseUpgradeGondorBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorBasicTraining | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### GondorKnightHorde
cost 500, time 25.0, cp 60, set `GondorKnightHordeCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, PickupStuffUpdate, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_PurchaseUpgradeGondorForgedBlades | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorForgedBlades | restricted |
| 3 | Command_PurchaseUpgradeGondorHeavyArmor | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorHeavyArmor | restricted |
| 4 | Command_PurchaseUpgradeGondorBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorBasicTraining | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### RohanRohirrimHorde
cost 650, time 45.0, cp 80, set `RohirrimHordeCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, PickupStuffUpdate, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_ToggleRohirrimWeapon | TOGGLE_WEAPONSET | swaps the weapon set |  | enabled |
| 3 | Command_PurchaseUpgradeRohanForgedBladesForRohirrim | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_RohanForgedBladesForRohirrim | restricted |
| 4 | Command_PurchaseUpgradeRohanHeavyArmorForRohirrim | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_RohanHeavyArmorForRohirrim | restricted |
| 5 | Command_PurchaseUpgradeRohanBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_RohanBasicTraining | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### GondorKnightsofDolHorde
cost 1500, time 45.0, cp 100, set `GondorKnightsofDolHordeCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, PickupStuffUpdate, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_KnightsofDolInspiration | SPECIAL_POWER | casts the power | SpecialAbilityFakeLeadership | restricted |
| 3 | Command_ChargeofGlory | SPECIAL_POWER | casts the power | SpecialAbilityTheodenGloriousCharge | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### GondorTrebuchet
cost 1000, time 30.0, cp 25, set `GondorTrebuchetCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_ToggleTrebAutoAcquire | TOGGLE_NO_AUTO_ACQUIRE | hold fire |  | enabled |
| 3 | Command_PurchaseUpgradeGondorFireStones | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorFireStones | restricted |
| 4 | Command_Bombard | FIRE_WEAPON | fires the weapon slot |  | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### MenWallSegmentSmall
cost 50, time 20.0, cp 0, set `MenWallSegmentCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_MenWallUpgradeToHub | OBJECT_UPGRADE | researches the upgrade on this object | GondorCastleWallHub | enabled |
| 2 | Command_MenWallUpgradeToRegularGate | OBJECT_UPGRADE | researches the upgrade on this object | MenWallGate | enabled |
| 3 | Command_MenWallUpgradeToPosternGate | OBJECT_UPGRADE | researches the upgrade on this object | MenWallPosternGate | enabled |
| 4 | Command_MenWallUpgradeToTower | OBJECT_UPGRADE | researches the upgrade on this object | GondorCastleWallTower | enabled |
| 5 | Command_MenWallUpgradeToTrebuchet | OBJECT_UPGRADE | researches the upgrade on this object | GondorCastleWallTrebuchet | enabled |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |
| 7 | Command_StartSelfRepair | START_SELF_REPAIR |  |  | restricted |
- makes or lays MenWallHubSmall
- makes or lays MenWallGateSmall
- makes or lays MenWallPosternGateSmall
- makes or lays MenWallTowerSmall
- makes or lays MenWallTrebuchetSmall

### MenWallCliffCap
cost 50, time 20.0, cp 0, set `MenWallCliffCapCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### MenTrebuchetFortress
cost 1000, time 30.0, cp 0, set `GondorTrebuchetFortressWallCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleTrebAutoAcquire | TOGGLE_NO_AUTO_ACQUIRE | hold fire |  | enabled |
| 2 | Command_Bombard | FIRE_WEAPON | fires the weapon slot |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |

### GondorFighter
cost 0, time 1.0, cp 4, set `GondorFighterCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### RohanSpearmen
cost 0, time 1.0, cp 4, set `RohanPikeCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_RohanPorcupineFormation | HORDE_TOGGLE_FORMATION | toggles the horde formation |  | enabled |
| 3 | Command_PurchaseUpgradeGondorForgedBlades | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorForgedBlades | restricted |
| 4 | Command_PurchaseUpgradeGondorHeavyArmor | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorHeavyArmor | restricted |
| 5 | Command_PurchaseUpgradeGondorBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorBasicTraining | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | NOT-SHOWN |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### GondorTowerShieldGuard
cost 0, time 1.0, cp 5, set `GondorTowerShieldGuardCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_TowerGuardPorcupineFormation | HORDE_TOGGLE_FORMATION | toggles the horde formation |  | enabled |
| 3 | Command_PurchaseUpgradeGondorForgedBlades | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorForgedBlades | restricted |
| 4 | Command_PurchaseUpgradeGondorHeavyArmor | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorHeavyArmor | restricted |
| 5 | Command_PurchaseUpgradeGondorBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GondorBasicTraining | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | NOT-SHOWN |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### GondorArcher
cost 0, time 1.0, cp 4, set `GondorArcherCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### GondorRanger
cost 0, time 1.0, cp 6, set `GondorRangerCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### GondorCavalry
cost 0, time 1.0, cp 6, set `GondorCavalryCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### RohanRohirrim
cost 0, time 1.0, cp 8, set `RohanRohirrimCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_ToggleRohirrimWeapon | TOGGLE_WEAPONSET | swaps the weapon set |  | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### GondorKnightsofDol
cost 0, time 1.0, cp 20, set `GondorKnightsofDolCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_KnightsofDolInspiration | SPECIAL_POWER | casts the power | SpecialAbilityFakeLeadership | restricted |
| 3 | Command_ChargeofGlory | SPECIAL_POWER | casts the power | SpecialAbilityTheodenGloriousCharge | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### MenWallGateSmall
cost 500, time 25.0, cp 0, set `CastleGateCommandSet`; unported: DefectionHelper, FakePathfindPortalBehaviour, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleGate | TOGGLE_GATE | opens / closes the gate |  | active |
| 2 | Command_StartSelfRepair | START_SELF_REPAIR |  |  | restricted |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### MenWallPosternGateSmall
cost 30, time 5.0, cp 0, set `SellableCommandSet`; unported: DefectionHelper, DynamicPortalBehaviour, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### MenWallTowerSmall
cost 500, time 20.0, cp 0, set `SellableCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### MenWallTrebuchetSmall
cost 1200, time 30.0, cp 0, set `-` (not selected by a click); unported: DefectionHelper, GuardingHelper, RecoveryHelper, SlaveWatcherBehavior

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|

after its upgrade, set `GondorCastleWallUpgradeCommandSetBuyNewTreb`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_PurchaseNewWallTreb | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_HasWallTrebuchet |
| 6 | Command_Sell | SELL | sells the structure |  |

after its upgrade, set `SellableCommandSet`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  |
- makes or lays GondorTrebuchetWall

### GondorTrebuchetWall
cost 1000, time 30.0, cp 0, set `GondorTrebuchetFortressWallCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleTrebAutoAcquire | TOGGLE_NO_AUTO_ACQUIRE | hold fire |  | enabled |
| 2 | Command_Bombard | FIRE_WEAPON | fires the weapon slot |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |

Total: 56 objects, 368 buttons, 52 buttons of the command sets an upgrade switches to.

## Elves

### ElvesSpellBook
cost 0, time 1.0, cp 0, set `ElvesSpellBookCommandSet` (not selected by a click); unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_SpellBookRallyingCall | SPELL_BOOK | spell book (power tree) | SpellBookRallyingCall | NOT-SHOWN |
| 2 | Command_SpellBookHeal | SPELL_BOOK | spell book (power tree) | SpellBookHeal | NOT-SHOWN |
| 3 | Command_SpellBookFarsight | SPELL_BOOK | spell book (power tree) | SpellBookFarsight | NOT-SHOWN |
| 4 | Command_SpellBookTomBombadil | SPELL_BOOK | spell book (power tree) | SpellBookTomBombadil | NOT-SHOWN |
| 5 | Command_SpellBookEnshroudingMist | SPELL_BOOK | spell book (power tree) | SpellBookEnshroudingMist | NOT-SHOWN |
| 6 | Command_SpellBookArrowVolleyGood | SPELL_BOOK | spell book (power tree) | SpellBookArrowVolleyGood | NOT-SHOWN |
| 7 | Command_SpellBookElvenWood | SPELL_BOOK | spell book (power tree) | SpellBookElvenWood | NOT-SHOWN |
| 8 | Command_SpellBookCloudBreak | SPELL_BOOK | spell book (power tree) | SpellBookCloudBreak | NOT-SHOWN |
| 9 | Command_SpellBookEntAllies | SPELL_BOOK | spell book (power tree) | SpellBookEntAllies | NOT-SHOWN |
| 10 | Command_SpellBookEagleAllies | SPELL_BOOK | spell book (power tree) | SpellBookEagleAllies | NOT-SHOWN |
| 11 | Command_SpellBookFlood | SPELL_BOOK | spell book (power tree) | SpellBookFlood | NOT-SHOWN |
| 12 | Command_SpellBookSunflare | SPELL_BOOK | spell book (power tree) | SpellBookSunflare | NOT-SHOWN |

### ElvenFortress
cost 5000, time 120.0, cp 0, set `-` (not selected by a click); unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|

### ElvenPorter
cost 500, time 30.0, cp 0, set `ElvenPorterCommandSet`; unported: DefectionHelper, GiveUpgradeUpdate, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_Stop | STOP | stops |  | enabled |
| 2 | Command_PorterExtinguishFire | SPECIAL_POWER | casts the power | SpecialAbilityPorterExtinguishFire | enabled |
| 3 | Command_ConstructElvenMallornTree | DOZER_CONSTRUCT | builder places the structure | ElvenMallornTree | enabled |
| 4 | Command_ConstructElvenBarracks | DOZER_CONSTRUCT | builder places the structure | ElvenBarracks | enabled |
| 5 | Command_ConstructElvenGreenPasture | DOZER_CONSTRUCT | builder places the structure | ElvenGreenPasture | enabled |
| 6 | Command_ConstructElvenEntMoot | DOZER_CONSTRUCT | builder places the structure | ElvenEntMoot | enabled |
| 7 | Command_ConstructElvenStatue | DOZER_CONSTRUCT | builder places the structure | ElvenStatue | enabled |
| 8 | Command_ConstructElvenWatchTower | DOZER_CONSTRUCT | builder places the structure | ElvenBattleTower | enabled |
| 9 | Command_ConstructElvenMirrorOfGaladriel | DOZER_CONSTRUCT | builder places the structure | ElvenMirrorOfGaladriel | enabled |
| 10 | Command_ConstructEregionForge | DOZER_CONSTRUCT | builder places the structure | EregionForge | enabled |
| 11 | Command_ConstructElvenWallHub | DOZER_CONSTRUCT | builder places the structure | ElvenCastleWallHub | enabled |
| 12 | Command_ConstructElvenFortress | DOZER_CONSTRUCT | builder places the structure | ElvenFortress | enabled |

### ElvenFortressExpansionPadCorner
cost 1, time 5.0, cp 0, set `ElvenFortressExpansionPadCornerCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ConstructElvenWatchtowerExpansion | FOUNDATION_CONSTRUCT | builds on the plot | ElvenWatchtowerExpansion | enabled |
| 2 | Command_ConstructElvenVigilantEntExpansion | FOUNDATION_CONSTRUCT | builds on the plot | ElvenVigilantEntExpansion | enabled |
| 3 | Command_ConstructElvenFloodgateExpansion | FOUNDATION_CONSTRUCT | builds on the plot | ElvenFloodgateExpansion | enabled |
| 4 | Command_ConstructElvenCastleWallHubExpansion | FOUNDATION_CONSTRUCT | builds on the plot | ElvenCastleWallHubExpansion | enabled |

### ElvenCitadel
cost 5000, time 120.0, cp 0, set `ElvenFortressCommandSet`; unported: AudioLoopUpgrade, CitadelSlaughterHordeContain, DefectionHelper, FiringTrackerHelper, GeometryUpgrade, GuardingHelper, RecoveryHelper, SlaveWatcherBehavior, SpawnUnitBehavior

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ConstructElvenPorter | UNIT_BUILD | builds the unit / horde | ElvenPorter | enabled |
| 2 | Command_SelectRevivablesElvenFortress | PUSH_VISIBLE_COMMAND_RANGE | opens a page |  | enabled |
| 3 | Command_SelectUpgradesElvenFortress | PUSH_VISIBLE_COMMAND_RANGE | opens a page |  | enabled |
| 4 | Command_PurchaseNewEagle | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_HasEaglesNestEagle | restricted |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |
| 7 | Command_PurchaseUpgradeElvenFortressEnchantedAnvil | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenFortressEnchantedAnvil | enabled |
| 8 | Command_PurchaseUpgradeElvenFortressBlessedMist | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenFortressBlessedMist | enabled |
| 9 | Command_PurchaseUpgradeElvenFortressCrystalMoat | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenFortressCrystalMoat | enabled |
| 10 | Command_PurchaseUpgradeElvenFortressMysticFountains | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenFortressMysticFountains | enabled |
| 11 | Command_PurchaseUpgradeElvenFortressEncasingVines | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenFortressEncasingVinesTrigger | enabled |
| 12 | Command_PurchaseUpgradeElvenFortressEaglesNest | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenFortressEaglesNest | restricted |
| 13 | Command_RadialBack | POP_VISIBLE_COMMAND_RANGE | closes a page |  | enabled |
| 14 | Command_RingHeroReviveSlot | REVIVE | recruits / revives a hero |  | NOT-SHOWN |
| 15 | Command_CreateAHeroReviveSlot | REVIVE | recruits / revives a hero |  | NOT-SHOWN |
| 16 | Command_GenericReviveSlot1 | REVIVE | recruits / revives a hero |  | enabled |
| 17 | Command_GenericReviveSlot2 | REVIVE | recruits / revives a hero |  | enabled |
| 18 | Command_GenericReviveSlot3 | REVIVE | recruits / revives a hero |  | enabled |
| 19 | Command_GenericReviveSlot4 | REVIVE | recruits / revives a hero |  | enabled |
| 20 | Command_GenericReviveSlot5 | REVIVE | recruits / revives a hero |  | enabled |
| 21 | Command_GenericReviveSlot6 | REVIVE | recruits / revives a hero |  | enabled |
| 22 | Command_GenericReviveSlot7 | REVIVE | recruits / revives a hero |  | NOT-SHOWN |
| 23 | Command_RadialBack | POP_VISIBLE_COMMAND_RANGE | closes a page |  | enabled |
- makes or lays ElvenFortressEagle
- makes or lays ElvenFortressCrystalMoat

### CreateAHero
cost 500, time 30.0, cp 50, set `CreateAHeroCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, LocomotorSetUpgrade, RecoveryHelper, ReplaceObjectUpdate, StoreObjectsSpecialPower, TeleportToCasterSpecialPower

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 2 | Command_Stop | STOP | stops |  | enabled |

### ElvenHaldir
cost 1200, time 30.0, cp 50, set `ElvenHaldirCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_ToggleHaldirWeapon | TOGGLE_WEAPONSET | swaps the weapon set |  | enabled |
| 3 | Command_HaldirFakeLeadershipButton | SPECIAL_POWER | casts the power | SpecialAbilityFakeLeadership | restricted |
| 4 | Command_HaldirGoldenArrow | SPECIAL_POWER | casts the power | SpecialAbilityHaldirGoldenArrow | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### ElvenGlorfindel
cost 1500, time 30.0, cp 50, set `ElvenGlorfindelCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_MountHorseGlorfindel | SPECIAL_POWER | casts the power | SpecialAbilityToggleMounted | enabled |
| 3 | Command_SpecialAbilityGlorfindelBladeOfPurity | SPECIAL_POWER | casts the power | SpecialAbilityGlorfindelBladeOfPurity | restricted |
| 4 | Command_SpecialAbilityGlorfindelWindRider | SPECIAL_POWER | casts the power | SpecialAbilityGlorfindelWindRider | restricted |
| 5 | Command_GlorfindelStarlight | SPECIAL_POWER | casts the power | SpecialAbilityGlorfindelStarlight | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### ElvenArwen
cost 800, time 30.0, cp 25, set `ElvenArwenCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_MountHorseArwen | SPECIAL_POWER | casts the power | SpecialAbilityToggleMounted | enabled |
| 3 | Command_ArwenElvenGrace | SPECIAL_POWER | casts the power | SpecialAbilityElvenGrace | restricted |
| 4 | Command_ArwenFlood | SPECIAL_POWER | casts the power | SpecialAbilityPersonalFlood | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### ElvenLegolas
cost 3000, time 30.0, cp 75, set `RohanLegolasCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_LegolasHawkStrike | SPECIAL_POWER | casts the power | SpecialAbilityHawkStrike | enabled |
| 3 | Command_LegolasKnifeFightingMode | SPECIAL_POWER | casts the power | SpecialAbilityKnifeFighter | notready |
| 4 | Command_SpecialAbilityTrainArchers | SPECIAL_POWER | casts the power | SpecialAbilityTrainArchers | restricted |
| 5 | Command_SpecialAbilityArrowStorm | SPECIAL_POWER | casts the power | SpecialAbilityArrowStorm | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### ElvenThranduil
cost 2500, time 30.0, cp 75, set `ElvenThranduilCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_ThranduilWildWalkButton | SPECIAL_POWER | casts the power | SpecialAbilityFakeLeadership | enabled |
| 3 | Command_SpecialAbilityMoveUnseen | SPECIAL_POWER | casts the power | SpecialAbilityMoveUnseen | restricted |
| 4 | Command_SpecialAbilityElfCloakThranduil | SPECIAL_POWER | casts the power | SpecialAbilityElfCloak | restricted |
| 5 | Command_SpecialAbilityDeadEye | SPECIAL_POWER | casts the power | SpecialAbilityThranduilDeadeye | restricted |
| 6 | Command_SpecialAbilityThornofVengeance | SPECIAL_POWER | casts the power | SpecialAbilityThornVengeance | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### ElvenElrond
cost 2500, time 30.0, cp 50, set `ElrondCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_SpecialAbilityElrondElvenGrace | SPECIAL_POWER | casts the power | SpecialAbilityElrondElvenGrace | enabled |
| 3 | Command_ElrondFakeLeadership | SPECIAL_POWER | casts the power | SpecialAbilityFakeLeadership | restricted |
| 4 | Command_SpecialAbilityElrondFarsight | SPECIAL_POWER | casts the power | SpecialAbilityElrondFarsight | restricted |
| 5 | Command_SpecialPowerSpawnWhirlwind | SPECIAL_POWER | casts the power | SpecialPowerSpawnTornado | restricted |
| 6 | Command_SpecialAbilityElrondRestoration | SPECIAL_POWER | casts the power | SpecialAbilityElrondRestoration | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### ElvenGaladriel_RingHero
cost 10000, time 300.0, cp 0, set `GaladrielCommandSet`; unported: AnnounceBirthAndDeathBehavior, DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_SpecialPowerSpawnTornado | SPECIAL_POWER | casts the power | SpecialPowerSpawnTornado | enabled |
| 3 | Command_SpecialAbilityTerribleFury | SPECIAL_POWER | casts the power | SpecialAbilityScreech | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### ElvenMallornTree
cost 300, time 15.0, cp 0, set `SellableCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### ElvenBarracks
cost 300, time 30.0, cp 0, set `ERElvenBarracksCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GeometryUpgrade, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ConstructElvenLorienWarriorHorde | UNIT_BUILD | builds the unit / horde | ElvenLorienWarriorHorde | enabled |
| 2 | Command_ConstructElvenLorienArcherHorde | UNIT_BUILD | builds the unit / horde | ElvenLorienArcherHorde | enabled |
| 3 | Command_ConstructElvenMithlondSentryHorde | UNIT_BUILD | builds the unit / horde | ElvenMithlondSentryHorde | enabled |
| 4 | Command_ConstructElvenMirkwoodArcherHorde | UNIT_BUILD | builds the unit / horde | ElvenMirkwoodArcherHorde | restricted |
| 5 | Command_ConstructNoldorWarriorHorde | UNIT_BUILD | builds the unit / horde | NoldorWarriorHorde | restricted |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |
| 7 | Command_PurchaseUpgradeElvenBarracksLevel2 | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenBarracksLevel2 | enabled |

after its upgrade, set `ElvenBarracksCommandSetLevel2`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_ConstructElvenLorienWarriorHorde | UNIT_BUILD | builds the unit / horde | ElvenLorienWarriorHorde |
| 2 | Command_ConstructElvenLorienArcherHorde | UNIT_BUILD | builds the unit / horde | ElvenLorienArcherHorde |
| 3 | Command_ConstructElvenMithlondSentryHorde | UNIT_BUILD | builds the unit / horde | ElvenMithlondSentryHorde |
| 4 | Command_ConstructElvenMirkwoodArcherHorde | UNIT_BUILD | builds the unit / horde | ElvenMirkwoodArcherHorde |
| 5 | Command_ConstructNoldorWarriorHorde | UNIT_BUILD | builds the unit / horde | NoldorWarriorHorde |
| 6 | Command_Sell | SELL | sells the structure |  |
| 7 | Command_PurchaseUpgradeElvenBarracksLevel3 | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenBarracksLevel3 |

after its upgrade, set `ElvenBarracksCommandSetLevel3`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_ConstructElvenLorienWarriorHorde | UNIT_BUILD | builds the unit / horde | ElvenLorienWarriorHorde |
| 2 | Command_ConstructElvenLorienArcherHorde | UNIT_BUILD | builds the unit / horde | ElvenLorienArcherHorde |
| 3 | Command_ConstructElvenMithlondSentryHorde | UNIT_BUILD | builds the unit / horde | ElvenMithlondSentryHorde |
| 4 | Command_ConstructElvenMirkwoodArcherHorde | UNIT_BUILD | builds the unit / horde | ElvenMirkwoodArcherHorde |
| 5 | Command_ConstructNoldorWarriorHorde | UNIT_BUILD | builds the unit / horde | NoldorWarriorHorde |
| 6 | Command_Sell | SELL | sells the structure |  |

### ElvenGreenPasture
cost 600, time 30.0, cp 0, set `ElvenGreenPastureCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GeometryUpgrade, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ConstructElvenRivendellLancers | UNIT_BUILD | builds the unit / horde | ElvenRivendellLancerHorde | enabled |
| 2 | Command_ConstructElvenRivendellArchers | UNIT_BUILD | builds the unit / horde | ElvenRivendellArcherHorde | restricted |
| 3 | Command_ConstructElvenGreenPastureLevel2 | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenGreenPastureLevel2 | enabled |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

after its upgrade, set `ElvenGreenPastureCommandSetLevel2`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_ConstructElvenRivendellLancers | UNIT_BUILD | builds the unit / horde | ElvenRivendellLancerHorde |
| 2 | Command_ConstructElvenRivendellArchers | UNIT_BUILD | builds the unit / horde | ElvenRivendellArcherHorde |
| 3 | Command_ConstructElvenGreenPastureLevel3 | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenGreenPastureLevel3 |
| 6 | Command_Sell | SELL | sells the structure |  |

after its upgrade, set `ElvenGreenPastureCommandSetLevel3`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_ConstructElvenRivendellLancers | UNIT_BUILD | builds the unit / horde | ElvenRivendellLancerHorde |
| 2 | Command_ConstructElvenRivendellArchers | UNIT_BUILD | builds the unit / horde | ElvenRivendellArcherHorde |
| 6 | Command_Sell | SELL | sells the structure |  |

### ElvenEntMoot
cost 750, time 60.0, cp 0, set `EntsMootCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ConstructEntGeneric | UNIT_BUILD | builds the unit / horde | RohanGenericEnt | enabled |
| 2 | Command_ConstructEntTreeBeard | UNIT_BUILD | builds the unit / horde | RohanTreeBerd | enabled |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### ElvenStatue
cost 300, time 45.0, cp 0, set `SellableCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### ElvenBattleTower
cost 500, time 60.0, cp 0, set `ElvenWatchTowerCommandSet`; unported: DefectionHelper, EvacuateDamage, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_PurchaseUpgradeElvenSilverthornTowerArrows | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenSilverthornArrowsTower | enabled |
| 2 | Command_ExitGarrison | EXIT_CONTAINER | lets a rider out |  | restricted |
| 3 | Command_ExitGarrison | EXIT_CONTAINER | lets a rider out |  | restricted |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### ElvenMirrorOfGaladriel
cost 300, time 45.0, cp 0, set `SellableCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### EregionForge
cost 1000, time 30.0, cp 0, set `EregionForgeCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GeometryUpgrade, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_PurchaseTechnologyElvenBasicTraining | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyElvenBasicTraining | enabled |
| 2 | Command_PurchaseTechnologyElvenHeavyArmor | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyElvenHeavyArmor | enabled |
| 3 | Command_PurchaseTechnologyElvenForgedBlades | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyElvenForgedBlades | restricted |
| 4 | Command_PurchaseTechnologyElvenSilverthornArrows | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyElvenSilverthornArrows | restricted |
| 5 | Command_PurchaseUpgradeEregionForgeLevel2 | OBJECT_UPGRADE | researches the upgrade on this object | EregionForge | enabled |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

after its upgrade, set `EregionForgeCommandSetLevel2`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_PurchaseTechnologyElvenBasicTraining | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyElvenBasicTraining |
| 2 | Command_PurchaseTechnologyElvenHeavyArmor | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyElvenHeavyArmor |
| 3 | Command_PurchaseTechnologyElvenForgedBlades | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyElvenForgedBlades |
| 4 | Command_PurchaseTechnologyElvenSilverthornArrows | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyElvenSilverthornArrows |
| 5 | Command_PurchaseUpgradeEregionForgeLevel3 | OBJECT_UPGRADE | researches the upgrade on this object | EregionForge |
| 6 | Command_Sell | SELL | sells the structure |  |

after its upgrade, set `EregionForgeCommandSetLevel3`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_PurchaseTechnologyElvenBasicTraining | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyElvenBasicTraining |
| 2 | Command_PurchaseTechnologyElvenHeavyArmor | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyElvenHeavyArmor |
| 3 | Command_PurchaseTechnologyElvenForgedBlades | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyElvenForgedBlades |
| 4 | Command_PurchaseTechnologyElvenSilverthornArrows | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyElvenSilverthornArrows |
| 6 | Command_Sell | SELL | sells the structure |  |

### ElvenCastleWallHub
cost 150, time 20.0, cp 0, set `ElvenCastleWallHubCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ElvenCastleWallHub_BeginWallSpan | DOZER_CONSTRUCT | builder places the structure | ElvenCastleWallHub | enabled |
| 4 | Command_CancelWallSpan | CANCEL_NEIGHBORHOOD | cancels the plot placement |  | restricted |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |
- makes or lays ElvenCastleWallSegment
- makes or lays ElvenCastleWallHub
- makes or lays ElvenWallCliffCap

### ElvenWatchtowerExpansion
cost 500, time 30.0, cp 0, set `SellableCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### ElvenVigilantEntExpansion
cost 1200, time 30.0, cp 0, set `ElvenVigilantEntCommandSet`; unported: AimWeaponBehavior, DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleTrebAutoAcquire | TOGGLE_NO_AUTO_ACQUIRE | hold fire |  | enabled |
| 2 | Command_Bombard | FIRE_WEAPON | fires the weapon slot |  | notready |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |

### ElvenFloodgateExpansion
cost 1500, time 30.0, cp 0, set `SellableCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### ElvenCastleWallHubExpansion
cost 150, time 20.0, cp 0, set `ElvenCastleWallHubCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ElvenCastleWallHub_BeginWallSpan | DOZER_CONSTRUCT | builder places the structure | ElvenCastleWallHub | enabled |
| 4 | Command_CancelWallSpan | CANCEL_NEIGHBORHOOD | cancels the plot placement |  | restricted |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |
- makes or lays ElvenCastleWallSegment
- makes or lays ElvenCastleWallHub
- makes or lays ElvenWallCliffCap

### ElvenFortressEagle
cost 0, time 1.0, cp 0, set `GwaihirCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### ElvenFortressCrystalMoat
cost 0, time 1.0, cp 0, set `-` (not selected by a click); unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|

### ElvenLorienWarriorHorde
cost 300, time 30.0, cp 60, set `ElvenLorienWarriorHordeCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, PickupStuffUpdate, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_PurchaseUpgradeElvenForgedBlades | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenForgedBlades | restricted |
| 3 | Command_PurchaseUpgradeElvenHeavyArmor | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenHeavyArmor | restricted |
| 4 | Command_PurchaseUpgradeElvenBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenBasicTraining | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### ElvenLorienArcherHorde
cost 300, time 30.0, cp 60, set `ElvenLorienArcherHordeCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, PickupStuffUpdate, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_ArcherBombard | FIRE_WEAPON | fires the weapon slot |  | enabled |
| 3 | Command_PurchaseUpgradeElvenHeavyArmor | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenHeavyArmor | restricted |
| 4 | Command_PurchaseUpgradeElvenSilverthornArrows | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenSilverthornArrows | restricted |
| 5 | Command_PurchaseUpgradeElvenBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenBasicTraining | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### ElvenMithlondSentryHorde
cost 300, time 35.0, cp 60, set `ElvenMithlondSentryHordeCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, PickupStuffUpdate, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_MithlondSentryPorcupineFormation | HORDE_TOGGLE_FORMATION | toggles the horde formation |  | enabled |
| 3 | Command_PurchaseUpgradeElvenForgedBlades | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenForgedBlades | restricted |
| 4 | Command_PurchaseUpgradeElvenHeavyArmor | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenHeavyArmor | restricted |
| 5 | Command_PurchaseUpgradeElvenBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenBasicTraining | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### ElvenMirkwoodArcherHorde
cost 800, time 45.0, cp 80, set `ElvenMirkwoodArcherHordeCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, PickupStuffUpdate, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_ArcherBombard | FIRE_WEAPON | fires the weapon slot |  | enabled |
| 3 | Command_SpecialAbilityElfCloak | SPECIAL_POWER | casts the power | SpecialAbilityElfCloak | enabled |
| 4 | Command_PurchaseUpgradeElvenSilverthornArrows | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenSilverthornArrows | restricted |
| 5 | Command_PurchaseUpgradeElvenBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenBasicTraining | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### NoldorWarriorHorde
cost 1500, time 45.0, cp 96, set `NoldorWarriorHordeCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_ToggleElvenWarriorWeapon | TOGGLE_WEAPONSET | swaps the weapon set |  | enabled |
| 3 | Command_ArcherBombard | FIRE_WEAPON | fires the weapon slot |  | enabled |
| 4 | Command_SpecialAbilityNoldorWeaponSong | SPECIAL_POWER | casts the power | SpecialAbilityGlorfindelBladeOfPurity | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### ElvenRivendellLancerHorde
cost 450, time 30.0, cp 60, set `ElvenRivendellLancerHordeCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, PickupStuffUpdate, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_PurchaseUpgradeElvenHeavyArmor | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenHeavyArmor | restricted |
| 3 | Command_PurchaseUpgradeElvenForgedBlades | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenForgedBlades | restricted |
| 4 | Command_PurchaseUpgradeElvenBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenBasicTraining | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### ElvenRivendellArcherHorde
cost 450, time 30.0, cp 80, set `ElvenRivendellArcherHordeCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, PickupStuffUpdate, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_PurchaseUpgradeElvenHeavyArmor | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenHeavyArmor | restricted |
| 3 | Command_PurchaseUpgradeElvenSilverthornArrows | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenSilverthornArrows | restricted |
| 4 | Command_PurchaseUpgradeElvenBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenBasicTraining | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### RohanGenericEnt
cost 1000, time 45.0, cp 50, set `RohanEntCommandSet`; unported: AimWeaponBehavior, DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_ToggleEntRockThrow | TOGGLE_WEAPONSET | swaps the weapon set |  | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### RohanTreeBerd
cost 2000, time 60.0, cp 50, set `RohanTreeBeardCommandSet`; unported: AimWeaponBehavior, DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_ToggleTreebeardRockThrow | TOGGLE_WEAPONSET | swaps the weapon set |  | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### ElvenCastleWallSegment
cost 40, time 20.0, cp 0, set `ElvenWallUpgradeNodeCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ElvenWallUpgradeToHub | OBJECT_UPGRADE | researches the upgrade on this object | ElvenCastleWallHub | enabled |
| 2 | Command_ElvenWallUpgradeToRegularGate | OBJECT_UPGRADE | researches the upgrade on this object | ElvenCastleWallGate | enabled |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |
| 7 | Command_StartSelfRepair | START_SELF_REPAIR |  |  | restricted |
- makes or lays ElvenCastleWallHub
- makes or lays ElvenCastleWallGate

### ElvenWallCliffCap
cost 50, time 20.0, cp 0, set `ElvenWallCliffCapCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### ElvenLorienWarrior
cost 0, time 1.0, cp 5, set `-` (not selected by a click); unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|

### ElvenLorienArcher
cost 0, time 1.0, cp 5, set `ElvenLorienArcherHordeCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_ArcherBombard | FIRE_WEAPON | fires the weapon slot |  | notready |
| 3 | Command_PurchaseUpgradeElvenHeavyArmor | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenHeavyArmor | restricted |
| 4 | Command_PurchaseUpgradeElvenSilverthornArrows | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenSilverthornArrows | restricted |
| 5 | Command_PurchaseUpgradeElvenBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_ElvenBasicTraining | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | NOT-SHOWN |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### ElvenMithlondSentry
cost 0, time 1.0, cp 4, set `ElvenMithlondSentryCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | NOT-SHOWN |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### ElvenMirkwoodArcher
cost 0, time 1.0, cp 8, set `ElvenMirkwoodArcherCommandSet`; unported: AimWeaponBehavior, DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | NOT-SHOWN |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### NoldorWarrior
cost 0, time 1.0, cp 16, set `NoldorWarriorCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_ToggleElvenWarriorWeapon | TOGGLE_WEAPONSET | swaps the weapon set |  | enabled |
| 3 | Command_ArcherBombard | FIRE_WEAPON | fires the weapon slot |  | enabled |
| 4 | Command_SpecialAbilityNoldorWeaponSong | SPECIAL_POWER | casts the power | SpecialAbilityGlorfindelBladeOfPurity | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | NOT-SHOWN |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### ElvenRivendellLancer
cost 0, time 1.0, cp 6, set `ElvenRivendellLancerCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### ElvenRivendellArcher
cost 0, time 1.0, cp 8, set `ElvenRivendellArcherCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### ElvenCastleWallGate
cost 500, time 20.0, cp 0, set `CastleGateCommandSet`; unported: DefectionHelper, FakePathfindPortalBehaviour, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleGate | TOGGLE_GATE | opens / closes the gate |  | active |
| 2 | Command_StartSelfRepair | START_SELF_REPAIR |  |  | restricted |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

Total: 47 objects, 315 buttons, 31 buttons of the command sets an upgrade switches to.

## Dwarves

### DwarvesSpellBook
cost 0, time 1.0, cp 0, set `DwarvesSpellBookCommandSet` (not selected by a click); unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_SpellBookHeal | SPELL_BOOK | spell book (power tree) | SpellBookHeal | NOT-SHOWN |
| 2 | Command_SpellBookRebuild | SPELL_BOOK | spell book (power tree) | SpellBookRebuild | NOT-SHOWN |
| 3 | Command_SpellBookRallyingCall | SPELL_BOOK | spell book (power tree) | SpellBookRallyingCall | NOT-SHOWN |
| 4 | Command_SpellBookHobbitAllies | SPELL_BOOK | spell book (power tree) | SpellBookHobbitAllies | NOT-SHOWN |
| 5 | Command_SpellBookLoneTowerDwarf | SPELL_BOOK | spell book (power tree) | SpellBookSpawnLoneTowerDwarf | NOT-SHOWN |
| 6 | Command_SpellBookDwarvenRiches | SPELL_BOOK | spell book (power tree) | SpellBookDwarvenRiches | NOT-SHOWN |
| 7 | Command_SpellBookMenOfDaleAllies | SPELL_BOOK | spell book (power tree) | SpellBookMenOfDaleAllies | NOT-SHOWN |
| 8 | Command_SpellBookCloudBreak | SPELL_BOOK | spell book (power tree) | SpellBookCloudBreak | NOT-SHOWN |
| 9 | Command_SpellBookUndermine | SPELL_BOOK | spell book (power tree) | SpellBookUndermine | NOT-SHOWN |
| 10 | Command_SpellBookBombard | SPELL_BOOK | spell book (power tree) | SpellBookBombard | NOT-SHOWN |
| 11 | Command_SpellBookEarthquake | SPELL_BOOK | spell book (power tree) | SpellBookEarthquake | NOT-SHOWN |
| 12 | Command_SpellBookCitadel | SPELL_BOOK | spell book (power tree) | SpellBookCitadel | NOT-SHOWN |

### DwarvenFortress
cost 5000, time 120.0, cp 0, set `-` (not selected by a click); unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|

### DwarvenPorter
cost 500, time 30.0, cp 0, set `DwarvenPorterCommandSet`; unported: DefectionHelper, GiveUpgradeUpdate, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_Stop | STOP | stops |  | enabled |
| 2 | Command_PorterExtinguishFire | SPECIAL_POWER | casts the power | SpecialAbilityPorterExtinguishFire | enabled |
| 3 | Command_ConstructDwarvenMineShaft | DOZER_CONSTRUCT | builder places the structure | DwarvenMineShaft | enabled |
| 4 | Command_ConstructDwarvenBarracks | DOZER_CONSTRUCT | builder places the structure | DwarfBarracks | enabled |
| 5 | Command_ConstructDwarvenArcheryRange | DOZER_CONSTRUCT | builder places the structure | DwarvenArcheryRange | enabled |
| 6 | Command_ConstructDwarvenForgeWorks | DOZER_CONSTRUCT | builder places the structure | DwarvenSiegeWorks | enabled |
| 7 | Command_ConstructDwarvenSentryTower | DOZER_CONSTRUCT | builder places the structure | DwarvenSentryTower | enabled |
| 8 | Command_ConstructDwarvenHearth | DOZER_CONSTRUCT | builder places the structure | DwarvenHearth | enabled |
| 9 | Command_ConstructDwarvenStatue | DOZER_CONSTRUCT | builder places the structure | DwarvenStatue | enabled |
| 10 | Command_ConstructDwarvenWallHub | DOZER_CONSTRUCT | builder places the structure | DwarvenWallHubSmall | enabled |
| 11 | Command_PorterConstructDwarvenFortress | DOZER_CONSTRUCT | builder places the structure | DwarvenFortress | enabled |

### DwarvenFortressExpansionPadSide
cost 1, time 5.0, cp 0, set `DwarvenFortressExpansionPadSideCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ConstructDwarvenCatapultExpansion | FOUNDATION_CONSTRUCT | builds on the plot | DwarvenCatapultExpansion | enabled |
| 2 | Command_ConstructDwarvenEreborTowerTowerExpansion | FOUNDATION_CONSTRUCT | builds on the plot | DwarvenEreborTowerTowerExpansion | enabled |
| 3 | Command_ConstructDwarvenHallExpansion | FOUNDATION_CONSTRUCT | builds on the plot | DwarvenHallExpansion | enabled |
| 4 | Command_ConstructDwarvenWallHubExpansion | FOUNDATION_CONSTRUCT | builds on the plot | DwarvenWallHubSmallExpansion | enabled |

### DwarvenFortressExpansionPadCorner
cost 1, time 5.0, cp 0, set `DwarvenFortressExpansionPadCornerCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ConstructDwarvenCatapultExpansion | FOUNDATION_CONSTRUCT | builds on the plot | DwarvenCatapultExpansion | enabled |
| 2 | Command_ConstructDwarvenEreborTowerTowerExpansion | FOUNDATION_CONSTRUCT | builds on the plot | DwarvenEreborTowerTowerExpansion | enabled |
| 3 | Command_ConstructDwarvenHallExpansion | FOUNDATION_CONSTRUCT | builds on the plot | DwarvenHallExpansion | enabled |
| 4 | Command_ConstructDwarvenWallHubExpansion | FOUNDATION_CONSTRUCT | builds on the plot | DwarvenWallHubSmallExpansion | enabled |

### DwarvenFortressCitadel
cost 0, time 120.0, cp 0, set `DwarvenFortressCommandSet`; unported: AudioLoopUpgrade, CitadelSlaughterHordeContain, DefectionHelper, FiringTrackerHelper, GeometryUpgrade, GuardingHelper, RecoveryHelper, SlaveWatcherBehavior

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ConstructDwarvenPorter | UNIT_BUILD | builds the unit / horde | DwarvenPorter | enabled |
| 2 | Command_SelectRevivablesDwarvenFortress | PUSH_VISIBLE_COMMAND_RANGE | opens a page |  | enabled |
| 3 | Command_FireWeaponDwarvenFortressOilCasks | SPECIAL_POWER | casts the power | SpecialAbilityDwarvenFortressOilCasks | restricted |
| 4 | Command_FireWeaponMightyCatapult | SPECIAL_POWER | casts the power | SpecialAbilityMightyCatapultLauncher | NOT-SHOWN |
| 5 | Command_SelectUpgradesDwarvenFortress | PUSH_VISIBLE_COMMAND_RANGE | opens a page |  | enabled |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |
| 8 | Command_PurchaseUpgradeDwarvenFortressBanners | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenFortressBanners | enabled |
| 9 | Command_PurchaseUpgradeDwarvenFortressSiegeKegs | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenFortressSiegeKegs | enabled |
| 10 | Command_PurchaseUpgradeDwarvenFortressFlamingMunitions | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_GoodFortressFlamingMunitionsTrigger | enabled |
| 11 | Command_PurchaseUpgradeDwarvenFortressOilCasks | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenFortressOilCasks | enabled |
| 12 | Command_PurchaseUpgradeDwarvenFortressDwarvenStonework | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenFortressDwarvenStoneworkTrigger | enabled |
| 13 | Command_PurchaseUpgradeDwarvenFortressMightyCatapult | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenFortressMightyCatapult | restricted |
| 14 | Command_RadialBack | POP_VISIBLE_COMMAND_RANGE | closes a page |  | enabled |
| 15 | Command_RingHeroReviveSlot | REVIVE | recruits / revives a hero |  | NOT-SHOWN |
| 16 | Command_CreateAHeroReviveSlot | REVIVE | recruits / revives a hero |  | NOT-SHOWN |
| 17 | Command_GenericReviveSlot1 | REVIVE | recruits / revives a hero |  | enabled |
| 18 | Command_GenericReviveSlot2 | REVIVE | recruits / revives a hero |  | enabled |
| 19 | Command_GenericReviveSlot3 | REVIVE | recruits / revives a hero |  | enabled |
| 20 | Command_GenericReviveSlot4 | REVIVE | recruits / revives a hero |  | enabled |
| 21 | Command_GenericReviveSlot5 | REVIVE | recruits / revives a hero |  | NOT-SHOWN |
| 22 | Command_GenericReviveSlot6 | REVIVE | recruits / revives a hero |  | NOT-SHOWN |
| 23 | Command_GenericReviveSlot7 | REVIVE | recruits / revives a hero |  | NOT-SHOWN |
| 24 | Command_RadialBack | POP_VISIBLE_COMMAND_RANGE | closes a page |  | enabled |
- makes or lays DwarvenFortressMightyCatapult

### CreateAHero
cost 500, time 30.0, cp 50, set `CreateAHeroCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, LocomotorSetUpgrade, RecoveryHelper, ReplaceObjectUpdate, StoreObjectsSpecialPower, TeleportToCasterSpecialPower

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 2 | Command_Stop | STOP | stops |  | enabled |

### DwarvenCaptainofDale
cost 1200, time 30.0, cp 50, set `CaptainofDaleCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_SpecialAbilityDaleSlamArrow | SPECIAL_POWER | casts the power | SpecialAbilityDaleSlamArrow | restricted |
| 3 | Command_SpecialAbilityTrainArchers | SPECIAL_POWER | casts the power | SpecialAbilityTrainArchers | restricted |
| 4 | Command_SpecialAbilityDaleBeastSlayerArrow | SPECIAL_POWER | casts the power | SpecialAbilityDaleBeastSlayerArrow | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### DwarvenGloin
cost 2000, time 30.0, cp 25, set `GloinCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_SpecialAbilityDwarvenGloinSlam | SPECIAL_POWER | casts the power | SpecialAbilityDwarvenGloinSlam | enabled |
| 3 | Command_SpecialAbilityDwarvenGloinShakeFoundation | SPECIAL_POWER | casts the power | SpecialAbilityDwarvenGloinShakeFoundation | restricted |
| 4 | Command_SpecialAbilityGloinSmash | SPECIAL_POWER | casts the power | SpecialAbilityGloinSmash | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### DwarvenGimli
cost 4000, time 30.0, cp 75, set `RohanGimliCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_GimliAxeThrow | FIRE_WEAPON | fires the weapon slot |  | enabled |
| 3 | Command_RohanGimliLeap | SPECIAL_POWER | casts the power | SpecialAbilityGimliLeap | restricted |
| 4 | Command_SpecialAbilitySlayer | SPECIAL_POWER | casts the power | SpecialAbilityGimliHeroMode | notready |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### DwarvenDain
cost 2500, time 30.0, cp 50, set `DainCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_SpecialAbilityDainLeadership | SPECIAL_POWER | casts the power | SpecialAbilityFakeLeadership | enabled |
| 3 | Command_SpecialAbilityDainMightyRage | SPECIAL_POWER | casts the power | SpecialAbilityDainMightyRage | restricted |
| 4 | Command_SpecialAbilityStubbornPride | SPECIAL_POWER | casts the power | SpecialAbilityDainStubbornPride | restricted |
| 5 | Command_SpecialAbilityDainSummonRoyalGuard | SPECIAL_POWER | casts the power | SpecialAbilityDainSummonRoyalGuard | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### ElvenGaladriel_RingHero
cost 10000, time 300.0, cp 0, set `GaladrielCommandSet`; unported: AnnounceBirthAndDeathBehavior, DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_SpecialPowerSpawnTornado | SPECIAL_POWER | casts the power | SpecialPowerSpawnTornado | enabled |
| 3 | Command_SpecialAbilityTerribleFury | SPECIAL_POWER | casts the power | SpecialAbilityScreech | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### DwarvenMineShaft
cost 300, time 15.0, cp 0, set `DwarvenMineShaftCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GeometryUpgrade, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ExitGarrison | EXIT_CONTAINER | lets a rider out |  | restricted |
| 2 | Command_ExitGarrison | EXIT_CONTAINER | lets a rider out |  | restricted |
| 3 | Command_ExitGarrison | EXIT_CONTAINER | lets a rider out |  | restricted |
| 4 | Command_ExitGarrison | EXIT_CONTAINER | lets a rider out |  | restricted |
| 5 | Command_ExitGarrison | EXIT_CONTAINER | lets a rider out |  | restricted |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### DwarfBarracks
cost 400, time 20.0, cp 0, set `DwarvenBarracksCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GeometryUpgrade, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ConstructDwarvenGuardianHorde | UNIT_BUILD | builds the unit / horde | DwarvenGuardianHorde | enabled |
| 2 | Command_ConstructDwarvenPhalanxHorde | UNIT_BUILD | builds the unit / horde | DwarvenPhalanxHorde | enabled |
| 3 | Command_PurchaseTechnologyDwarvenBasicTraining | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyDwarvenBasicTraining | enabled |
| 4 | Command_ConstructDwarvenZerkerHorde | UNIT_BUILD | builds the unit / horde | DwarvenZerkerHorde | restricted |
| 5 | Command_UpgradeDwarvenBarracksLevel2 | OBJECT_UPGRADE | researches the upgrade on this object | DwarfBarracks | enabled |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

after its upgrade, set `DwarvenBarracksCommandSetLevel2`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_ConstructDwarvenGuardianHorde | UNIT_BUILD | builds the unit / horde | DwarvenGuardianHorde |
| 2 | Command_ConstructDwarvenPhalanxHorde | UNIT_BUILD | builds the unit / horde | DwarvenPhalanxHorde |
| 3 | Command_PurchaseTechnologyDwarvenBasicTraining | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyDwarvenBasicTraining |
| 4 | Command_ConstructDwarvenZerkerHorde | UNIT_BUILD | builds the unit / horde | DwarvenZerkerHorde |
| 5 | Command_UpgradeDwarvenBarracksLevel3 | OBJECT_UPGRADE | researches the upgrade on this object | DwarfBarracks |
| 6 | Command_Sell | SELL | sells the structure |  |

after its upgrade, set `DwarvenBarracksCommandSetLevel3`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_ConstructDwarvenGuardianHorde | UNIT_BUILD | builds the unit / horde | DwarvenGuardianHorde |
| 2 | Command_ConstructDwarvenPhalanxHorde | UNIT_BUILD | builds the unit / horde | DwarvenPhalanxHorde |
| 3 | Command_PurchaseTechnologyDwarvenBasicTraining | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyDwarvenBasicTraining |
| 4 | Command_ConstructDwarvenZerkerHorde | UNIT_BUILD | builds the unit / horde | DwarvenZerkerHorde |
| 6 | Command_Sell | SELL | sells the structure |  |

### DwarvenArcheryRange
cost 500, time 30.0, cp 0, set `DwarvenArcheryRangeCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GeometryUpgrade, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ConstructDwarvenAxeThrowerHorde | UNIT_BUILD | builds the unit / horde | DwarvenAxeThrowerHorde | enabled |
| 2 | Command_ConstructDwarvenMenOfDaleHorde | UNIT_BUILD | builds the unit / horde | DwarvenMenOfDaleHorde | restricted |
| 3 | Command_PurchaseTechnologyDwarvenFireArrows | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyDwarvenFireArrows | restricted |
| 4 | Command_PurchaseUpgradeDwarvenArcheryRangeLevel2 | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenArcheryRangeLevel2 | enabled |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

after its upgrade, set `DwarvenArcheryRangeCommandSetLevel2`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_ConstructDwarvenAxeThrowerHorde | UNIT_BUILD | builds the unit / horde | DwarvenAxeThrowerHorde |
| 2 | Command_ConstructDwarvenMenOfDaleHorde | UNIT_BUILD | builds the unit / horde | DwarvenMenOfDaleHorde |
| 3 | Command_PurchaseTechnologyDwarvenFireArrows | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyDwarvenFireArrows |
| 4 | Command_PurchaseUpgradeDwarvenArcheryRangeLevel3 | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenArcheryRangeLevel3 |
| 6 | Command_Sell | SELL | sells the structure |  |

after its upgrade, set `DwarvenArcheryRangeCommandSetLevel3`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_ConstructDwarvenAxeThrowerHorde | UNIT_BUILD | builds the unit / horde | DwarvenAxeThrowerHorde |
| 2 | Command_ConstructDwarvenMenOfDaleHorde | UNIT_BUILD | builds the unit / horde | DwarvenMenOfDaleHorde |
| 3 | Command_PurchaseTechnologyDwarvenFireArrows | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyDwarvenFireArrows |
| 6 | Command_Sell | SELL | sells the structure |  |

### DwarvenSiegeWorks
cost 500, time 30.0, cp 0, set `DwarvenForgeWorksCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GeometryUpgrade, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ConstructDwarvenBattleWagon | UNIT_BUILD | builds the unit / horde | DwarvenBattleWagon | enabled |
| 2 | Command_ConstructDwarvenDemolisher | UNIT_BUILD | builds the unit / horde | DwarvenDemolisher | restricted |
| 3 | Command_ConstructDwarvenCatapult | UNIT_BUILD | builds the unit / horde | DwarvenCatapult | restricted |
| 4 | Command_PurchaseTechnologyDwarvenFlamingShot | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyDwarvenFlamingShot | restricted |
| 5 | Command_UpgradeDwarvenForgeWorksLevel2 | OBJECT_UPGRADE | researches the upgrade on this object | DwarvenSiegeWorks | enabled |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |
| 7 | Command_PurchaseTechnologyDwarvenForgedBlades | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyDwarvenForgedBlades | restricted |
| 8 | Command_PurchaseTechnologyDwarvenMithrilMail | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyDwarvenMithrilMail | restricted |
| 9 | Command_PurchaseTechnologyDwarvenSiegeHammer | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyDwarvenSiegeHammer | restricted |

after its upgrade, set `DwarvenForgeWorksCommandSetLevel2`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_ConstructDwarvenBattleWagon | UNIT_BUILD | builds the unit / horde | DwarvenBattleWagon |
| 2 | Command_ConstructDwarvenDemolisher | UNIT_BUILD | builds the unit / horde | DwarvenDemolisher |
| 3 | Command_ConstructDwarvenCatapult | UNIT_BUILD | builds the unit / horde | DwarvenCatapult |
| 4 | Command_PurchaseTechnologyDwarvenFlamingShot | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyDwarvenFlamingShot |
| 5 | Command_UpgradeDwarvenForgeWorksLevel3 | OBJECT_UPGRADE | researches the upgrade on this object | DwarvenSiegeWorks |
| 6 | Command_Sell | SELL | sells the structure |  |
| 7 | Command_PurchaseTechnologyDwarvenForgedBlades | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyDwarvenForgedBlades |
| 8 | Command_PurchaseTechnologyDwarvenMithrilMail | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyDwarvenMithrilMail |
| 9 | Command_PurchaseTechnologyDwarvenSiegeHammer | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyDwarvenSiegeHammer |

after its upgrade, set `DwarvenForgeWorksCommandSetLevel3`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_ConstructDwarvenBattleWagon | UNIT_BUILD | builds the unit / horde | DwarvenBattleWagon |
| 2 | Command_ConstructDwarvenDemolisher | UNIT_BUILD | builds the unit / horde | DwarvenDemolisher |
| 3 | Command_ConstructDwarvenCatapult | UNIT_BUILD | builds the unit / horde | DwarvenCatapult |
| 4 | Command_PurchaseTechnologyDwarvenFlamingShot | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyDwarvenFlamingShot |
| 6 | Command_Sell | SELL | sells the structure |  |
| 7 | Command_PurchaseTechnologyDwarvenForgedBlades | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyDwarvenForgedBlades |
| 8 | Command_PurchaseTechnologyDwarvenMithrilMail | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyDwarvenMithrilMail |
| 9 | Command_PurchaseTechnologyDwarvenSiegeHammer | PLAYER_UPGRADE | researches the player upgrade | Upgrade_TechnologyDwarvenSiegeHammer |

### DwarvenSentryTower
cost 500, time 60.0, cp 0, set `AxeSentryTowerCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ExitGarrison | EXIT_CONTAINER | lets a rider out |  | restricted |
| 2 | Command_ExitGarrison | EXIT_CONTAINER | lets a rider out |  | restricted |
| 3 | Command_PurchaseSentryTowerForgedBladesUpgrade | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenTowerForgedBlades | enabled |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### DwarvenHearth
cost 300, time 45.0, cp 0, set `SellableCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### DwarvenStatue
cost 300, time 45.0, cp 0, set `SellableCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### DwarvenWallHubSmall
cost 200, time 30.0, cp 0, set `DwarvenCastleWallHubSmallCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_DwarvenCastleWallHub_BeginWallSpan | DOZER_CONSTRUCT | builder places the structure | DwarvenWallHubSmall | enabled |
| 4 | Command_CancelWallSpan | CANCEL_NEIGHBORHOOD | cancels the plot placement |  | restricted |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |
- makes or lays DwarvenWallSegmentSmall
- makes or lays DwarvenWallHubSmall
- makes or lays DwarvenWallCliffCap

### DwarvenCatapultExpansion
cost 1200, time 30.0, cp 0, set `SellableCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper, SlaveWatcherBehavior

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

after its upgrade, set `DwarvenUpgradeCommandSetBuyNewCatapult`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_DwarvenPurchaseNewWallCatapult | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_HasWallTrebuchet |
| 6 | Command_Sell | SELL | sells the structure |  |

after its upgrade, set `SellableCommandSet`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  |
- makes or lays DwarvenCatapultFortress

### DwarvenEreborTowerTowerExpansion
cost 500, time 30.0, cp 0, set `SellableCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### DwarvenHallExpansion
cost 400, time 10.0, cp 0, set `DwarvenHallCommandSet`; unported: DefectionHelper, EvacuateDamage, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ExitGarrison | EXIT_CONTAINER | lets a rider out |  | restricted |
| 2 | Command_ExitGarrison | EXIT_CONTAINER | lets a rider out |  | restricted |
| 3 | Command_ExitGarrison | EXIT_CONTAINER | lets a rider out |  | restricted |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### DwarvenWallHubSmallExpansion
cost 200, time 30.0, cp 0, set `DwarvenCastleWallHubSmallCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_DwarvenCastleWallHub_BeginWallSpan | DOZER_CONSTRUCT | builder places the structure | DwarvenWallHubSmall | enabled |
| 4 | Command_CancelWallSpan | CANCEL_NEIGHBORHOOD | cancels the plot placement |  | restricted |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |
- makes or lays DwarvenWallSegmentSmall
- makes or lays DwarvenWallHubSmall
- makes or lays DwarvenWallCliffCap

### DwarvenFortressMightyCatapult
cost 800, time 40.0, cp 0, set `EmptyCommandSet` (not selected by a click); unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|

### DwarvenGuardianHorde
cost 300, time 30.0, cp 70, set `DwarvenGuardianHordeCommandSet`; unported: CommandButtonHuntUpdate, DefectionHelper, FiringTrackerHelper, GuardingHelper, PickupStuffUpdate, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_DwarvenGuardianChargeAttack | SPECIAL_POWER | casts the power | SpecialAbilityDwarvenCharge | restricted |
| 3 | Command_PurchaseUpgradeDwarvenMithrilMail | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenMithrilMail | restricted |
| 4 | Command_PurchaseUpgradeDwarvenForgedBlades | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenForgedBlades | restricted |
| 5 | Command_PurchaseUpgradeDwarvenBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenBasicTraining | restricted |
| 6 | Command_PurchaseUpgradeDwarvenSiegeHammer | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenSiegeHammer | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### DwarvenPhalanxHorde
cost 350, time 30.0, cp 60, set `DwarvenPhalanxHordeCommandSet`; unported: CommandButtonHuntUpdate, DefectionHelper, FiringTrackerHelper, GuardingHelper, PickupStuffUpdate, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_DwarvenPhalanxPorcupineFormation | HORDE_TOGGLE_FORMATION | toggles the horde formation |  | enabled |
| 3 | Command_PurchaseUpgradeDwarvenMithrilMail | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenMithrilMail | restricted |
| 4 | Command_PurchaseUpgradeDwarvenForgedBlades | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenForgedBlades | restricted |
| 5 | Command_PurchaseUpgradeDwarvenBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenBasicTraining | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### DwarvenZerkerHorde
cost 1500, time 40.0, cp 120, set `DwarvenZerkerHordeCommandSet`; unported: CommandButtonHuntUpdate, DefectionHelper, FiringTrackerHelper, GuardingHelper, PickupStuffUpdate, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_ZealotAxeThrow | SPECIAL_POWER | casts the power | SpecialAbilityZealotBoomAxe | enabled |
| 3 | Command_SpecialAbilityZerkergang | SPECIAL_POWER | casts the power | SpecialAbilityGimliHeroMode | notready |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### DwarvenAxeThrowerHorde
cost 250, time 20.0, cp 60, set `DwarvenAxeThrowerHordeCommandSet`; unported: CommandButtonHuntUpdate, DefectionHelper, FiringTrackerHelper, GuardingHelper, PickupStuffUpdate, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_ArcherBombard | FIRE_WEAPON | fires the weapon slot |  | enabled |
| 3 | Command_PurchaseUpgradeDwarvenMithrilMail | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenMithrilMail | restricted |
| 4 | Command_PurchaseUpgradeDwarvenForgedBlades | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenForgedBlades | restricted |
| 5 | Command_PurchaseUpgradeDwarvenBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenBasicTraining | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### DwarvenMenOfDaleHorde
cost 500, time 30.0, cp 72, set `DwarvenMenOfDaleHordeCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, PickupStuffUpdate, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_ArcherBombard | FIRE_WEAPON | fires the weapon slot |  | enabled |
| 3 | Command_BlackArrows | SPECIAL_POWER | casts the power | SpecialAbilityBlackArrows | enabled |
| 4 | Command_PurchaseUpgradeDwarvenMithrilMail | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenMithrilMail | restricted |
| 5 | Command_PurchaseUpgradeDwarvenFireArrows | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenFireArrows | restricted |
| 6 | Command_PurchaseUpgradeDwarvenBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenBasicTraining | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### DwarvenBattleWagon
cost 400, time 30.0, cp 40, set `DwarvenBattleWagonCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_SpecialAbilityOilBarrels | SPECIAL_POWER | casts the power | SpecialAbilityDwarvenBattleWagonOilBarrels | enabled |
| 3 | Command_UpgradeBattleWagonAxeThrowers | OBJECT_UPGRADE | researches the upgrade on this object | DwarvenBattleWagon | enabled |
| 4 | Command_UpgradeBattleWagonBannerCarrier | OBJECT_UPGRADE | researches the upgrade on this object | DwarvenBattleWagon | enabled |
| 5 | Command_UpgradeBattleWagonMenOfDale | OBJECT_UPGRADE | researches the upgrade on this object | DwarvenBattleWagon | enabled |
| 6 | Command_UpgradeBattleWagonHearth | OBJECT_UPGRADE | researches the upgrade on this object | DwarvenBattleWagon | enabled |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

after its upgrade, set `DwarvenBattleWagonCommandSet_Upgraded`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  |
| 2 | Command_SpecialAbilityOilBarrels | SPECIAL_POWER | casts the power | SpecialAbilityDwarvenBattleWagonOilBarrels |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  |
| 14 | Command_Stop | STOP | stops |  |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  |

### DwarvenDemolisher
cost 600, time 30.0, cp 25, set `DwarvenDemolisherCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper, ToggleDeploySpecialAbilityUpdate

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_SpecialAbilityDwarvenDemolisherDeploy | SPECIAL_POWER | casts the power | SpecialAbilityDwarvenDemolisherDeploy | enabled |
| 3 | Command_PurchaseUpgradeDwarvenMithrilMail | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenMithrilMail | restricted |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### DwarvenCatapult
cost 800, time 40.0, cp 25, set `DwarvenCatapultCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_SpecialAbilityDwarvenIronHillsCatapultOilBarrels | SPECIAL_POWER | casts the power | SpecialAbilityDwarvenIronHillsCatapultOilBarrels | enabled |
| 3 | Command_Bombard | FIRE_WEAPON | fires the weapon slot |  | enabled |
| 4 | Command_ToggleTrebAutoAcquire | TOGGLE_NO_AUTO_ACQUIRE | hold fire |  | enabled |
| 5 | Command_PurchaseUpgradeDwarvenFlamingShot | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenFlamingShot | restricted |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### DwarvenWallSegmentSmall
cost 50, time 15.0, cp 0, set `DwarvenWallSegmentCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_DwarvenWallUpgradeToHub | OBJECT_UPGRADE | researches the upgrade on this object | DwarvenCastleWallHub | enabled |
| 2 | Command_DwarvenWallUpgradeToRegularGate | OBJECT_UPGRADE | researches the upgrade on this object | DwarvenCastleWallGate | enabled |
| 3 | Command_DwarvenWallUpgradeToPosternGate | OBJECT_UPGRADE | researches the upgrade on this object | DwarvenWallPosternGate | enabled |
| 4 | Command_DwarvenWallUpgradeToTower | OBJECT_UPGRADE | researches the upgrade on this object | DwarvenCastleWallTower | enabled |
| 5 | Command_DwarvenWallUpgradeToCatapult | OBJECT_UPGRADE | researches the upgrade on this object | DwarvenCastleWallCatapult | enabled |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |
| 7 | Command_StartSelfRepair | START_SELF_REPAIR |  |  | restricted |
- makes or lays DwarvenWallHubSmall
- makes or lays DwarvenWallGateSmall
- makes or lays DwarvenWallPosternGateSmall
- makes or lays DwarvenWallTowerSmall
- makes or lays DwarvenWallCatapultSmall

### DwarvenWallCliffCap
cost 50, time 20.0, cp 0, set `DwarvenWallCliffCapCommandSet`; unported: DefectionHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### DwarvenCatapultFortress
cost 800, time 40.0, cp 0, set `DwarvenCatapultFortressCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleTrebAutoAcquire | TOGGLE_NO_AUTO_ACQUIRE | hold fire |  | enabled |
| 3 | Command_Bombard | FIRE_WEAPON | fires the weapon slot |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |

### DwarvenGuardian
cost 0, time 1.0, cp 7, set `DwarvenGuardianCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_DwarvenGuardianChargeAttack | SPECIAL_POWER | casts the power | SpecialAbilityDwarvenCharge | enabled |
| 3 | Command_PurchaseUpgradeDwarvenMithrilMail | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenMithrilMail | restricted |
| 4 | Command_PurchaseUpgradeDwarvenForgedBlades | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenForgedBlades | restricted |
| 5 | Command_PurchaseUpgradeDwarvenBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenBasicTraining | restricted |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | NOT-SHOWN |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

after its upgrade, set `DwarvenGuardianHordeToggleCommandSet`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  |
| 2 | Command_DwarvenGuardianChargeAttack | SPECIAL_POWER | casts the power | SpecialAbilityDwarvenCharge |
| 3 | Command_PurchaseUpgradeDwarvenMithrilMail | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenMithrilMail |
| 5 | Command_PurchaseUpgradeDwarvenBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenBasicTraining |
| 6 | Command_ToggleGuardianWeapon | TOGGLE_WEAPONSET | swaps the weapon set |  |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  |
| 14 | Command_Stop | STOP | stops |  |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  |

### DwarvenPhalanx
cost 0, time 1.0, cp 4, set `DwarvenPhalanxCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### DwarvenZerker
cost 0, time 1.0, cp 30, set `DwarvenZerkerCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 2 | Command_ZealotAxeThrow | SPECIAL_POWER | casts the power | SpecialAbilityZealotBoomAxe | enabled |
| 3 | Command_SpecialAbilityZerkergang | SPECIAL_POWER | casts the power | SpecialAbilityGimliHeroMode | notready |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | NOT-SHOWN |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### DwarvenAxeThrower
cost 0, time 1.0, cp 5, set `DwarvenAxeThrowerCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### DwarvenMenOfDale
cost 0, time 1.0, cp 6, set `DwarvenMenOfDaleCommandSet`; unported: AimWeaponBehavior, DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleStance | TOGGLE_STANCE | toggles the stance |  | restricted |
| 3 | Command_PurchaseUpgradeDwarvenMithrilMail | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenMithrilMail | restricted |
| 4 | Command_PurchaseUpgradeDwarvenFireArrows | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenFireArrows | restricted |
| 5 | Command_PurchaseUpgradeDwarvenBasicTraining | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_DwarvenBasicTraining | restricted |
| 6 | Command_ArcherBombard | FIRE_WEAPON | fires the weapon slot |  | notready |
| 12 | Command_CaptureBuilding | SPECIAL_POWER | casts the power | SpecialAbilityCaptureBuilding | NOT-SHOWN |
| 13 | Command_AttackMove | ATTACK_MOVE | attack-move |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |
| 16 | Command_SetStanceBattle | SET_STANCE | sets the stance |  | restricted |
| 17 | Command_SetStanceAggressive | SET_STANCE | sets the stance |  | restricted |
| 18 | Command_SetStanceHoldGround | SET_STANCE | sets the stance |  | restricted |

### DwarvenWallGateSmall
cost 500, time 30.0, cp 0, set `CastleGateCommandSet`; unported: DefectionHelper, FakePathfindPortalBehaviour, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleGate | TOGGLE_GATE | opens / closes the gate |  | active |
| 2 | Command_StartSelfRepair | START_SELF_REPAIR |  |  | restricted |
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### DwarvenWallPosternGateSmall
cost 300, time 15.0, cp 0, set `SellableCommandSet`; unported: DefectionHelper, DynamicPortalBehaviour, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### DwarvenWallTowerSmall
cost 500, time 15.0, cp 0, set `SellableCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  | enabled |

### DwarvenWallCatapultSmall
cost 1200, time 30.0, cp 0, set `-` (not selected by a click); unported: DefectionHelper, GuardingHelper, RecoveryHelper, SlaveWatcherBehavior

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|

after its upgrade, set `DwarvenUpgradeCommandSetBuyNewCatapult`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 1 | Command_DwarvenPurchaseNewWallCatapult | OBJECT_UPGRADE | researches the upgrade on this object | Upgrade_HasWallTrebuchet |
| 6 | Command_Sell | SELL | sells the structure |  |

after its upgrade, set `SellableCommandSet`:

| slot | button | command | should | target |
|---|---|---|---|---|
| 6 | Command_Sell | SELL | sells the structure |  |
- makes or lays DwarvenWallCatapult

### DwarvenWallCatapult
cost 800, time 40.0, cp 0, set `DwarvenCatapultFortressCommandSet`; unported: DefectionHelper, FiringTrackerHelper, GuardingHelper, RecoveryHelper

| slot | button | command | should | target | HUD |
|---|---|---|---|---|---|
| 1 | Command_ToggleTrebAutoAcquire | TOGGLE_NO_AUTO_ACQUIRE | hold fire |  | enabled |
| 3 | Command_Bombard | FIRE_WEAPON | fires the weapon slot |  | enabled |
| 14 | Command_Stop | STOP | stops |  | enabled |

Total: 46 objects, 295 buttons, 61 buttons of the command sets an upgrade switches to.
