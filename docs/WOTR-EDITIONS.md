# War of the Ring: BFME2 1.06 vs RotWK 2.01 (editions inventory)

Analysis only (2026-10-29, lane WOTR-2 tree `lane/wotr2` at d9c2e055b; updated the same day from the LivingWorld
editions census, section 1). No code was changed.

## 1. What this is

The engine is becoming two editions (owner decision 2026-10-29, `docs/EDITIONS.md` in lane BFME2MODE-0):
**BFME2 1.06** is the base, **RotWK 2.01** is BFME2 plus its differences. Our War of the Ring ("Living World",
`engine/src/GameLogic/LivingWorld/`, `StrategicSession`, `GodotLivingWorld.cpp`, the `AptStrategic*` screens,
`godot/scripts/wotr.gd`) is ported from RotWK 2.01 only. This file lists what differs in BFME2 1.06's War of the
Ring, so a BFME2-edition WotR can be planned.

**Method.**

1. **Code, by pairing.** The WotR port's files cite 948 distinct RotWK addresses (1749 `RW 0x...` cites). Of
   these, 812 fall inside 739 RotWK functions of `workspace/rebuild/xmap/rw_map.csv`. The other 136 are data,
   vtables or mid-table addresses. Each function's tier gives a first verdict: A = identical after masking
   addresses, B = same shape, C/D = diverged, unpaired = no BFME2 partner in the xmap.
2. **Unpaired code, by address order.** For each unpaired function I took its nearest paired neighbours on both
   sides and listed the other edition's unpaired functions between the neighbours' partners. Both images keep
   the link order, so this finds the probable counterpart. These pairings are marked "probable".
3. **Reading.** I read the BFME2 decomp
   (`workspace/reference/open-bfme-2`, `Code/GameEngine/Source/GameLogic/System/LivingWorld/...`) for the
   functions that matter. I spot-checked RotWK with Ghidra (`rotwk201_game.exe`) at RW 0x6BE6A6, 0x6E445E,
   0x6B57E7, 0x6B57B0, 0x610A21, 0x932570, 0x7F5279 and 0x7F2A76.
4. **Retail data.** I extracted the WotR INIs from the two installs into a scratch folder outside the repo, then
   diffed them. Sources: BFME2 `ini.big` (game.dat md5 34391b25..., the 1.06 binary; `_patch101.big` and
   `_patch103.big` hold no WotR file) against RotWK `INI.big` plus `_patch201ini.big`. The patch overrides only
   `wotrscenario044.inc`, adding The_Dead_Marshes to one restriction. I also compared the `apt/*.big` shell
   archives byte for byte.

**Limits.**

- The BFME2 decomp helps reading. It is byte-accurate per function but does not prove behaviour.
- Ghidra output is a decompile, not a test.
- Address-order pairings are probable, not proven.
- The xmap tier says that code changed, not what changed. Where I did not read the change, the row says
  "not verified".
- The first edition census (`edition_census.json`, "War of the Ring: identical 6, offset-only 1, real divergence
  1, RotWK-only 2, BFME2-only 111") ran on engine tree 8c9a92ff7, which has no `engine/src/GameLogic/LivingWorld/`.
  None of its 5646 cited functions is cited from a WotR file, so its WotR row covers only incidental cites.

**Two counts of the same code.** They differ because they used different inputs:

| Count | Tree and files | Map | Cited addresses | Functions | Same | Divergent | RotWK-only (unpaired) | Not a function |
|---|---|---|---|---|---|---|---|---|
| This file's first pass (method step 1) | `lane/wotr2` d9c2e055b: `GameLogic/LivingWorld/`, `StrategicSession.*`, `GodotLivingWorld.cpp`, `AptStrategic*`, `AptBannerUI*`, `StrategicHUDData*`, `LivingWorldScoreScreen*`, `wotr.gd` | `rw_map.csv` of 2026-10-28 08:43, tiers only | 948 | 739 | A 413 + B 220 | C 7 + D 14 | 85 | 136 |
| LivingWorld editions census (coordinator, 2026-10-29) | `lane/wotr2` (af1b1aca9) `engine/src/GameLogic/LivingWorld/` only, with the unchanged `tools/re/edition_census.py` (lane/bfme2mode0) and both binaries | today's map and ledger | 721 | 530 | 373 (243 identical, 130 offset-only) | 82 (36 edited, 15 D, 5 C, 26 constant/call) | 75 | 111 (.rdata 95, .data 14, none 2) |

The census compares instruction bodies, so it also flags B-tier functions with edited branches or changed
constants that the tiers alone call "same shape". Its table is kept with the project's private
review notes (not in this repository). All 157 of
its divergent and RotWK-only functions are now in section 3. The first pass named 61 of them; the other 96 are in
the "More differing functions" tables at the end of each area.

**Addresses.** `RW 0x...` is a RotWK 2.01 VA (0x400000 + rva). `B2 0x...` is a BFME2 1.06 image-relative rva:
the decomp's `RvaXXXXXXXX` names and the xmap's `bfme2_rva` column.

## 2. Headline numbers

| Measure | Count | Bytes (RotWK) |
|---|---|---|
| RotWK functions the WotR port cites | 739 | 175,033 |
| tier A (identical) | 413 | 61,811 |
| tier B (same shape) | 220 | 82,565 |
| tier C (diverged, mostly string literals) | 7 | 5,382 |
| tier D (diverged) | 14 | 5,994 |
| unpaired in the xmap | 85 | 19,281 |
| All RotWK functions paired to a BFME2 LivingWorld decomp source (cited or not) | 1863: A 1515, B 322, C 11, D 15 | |
| LivingWorld census (only `GameLogic/LivingWorld/`, body compare) | 530 functions: same 373, divergent 82, RotWK-only 75 | |

The two editions share most of the strategic engine: the turn and phase machine, the message dispatcher, the
region manager, buildings, plots, the score keeper, the eye tower and most xfers. The autoresolve engine is shared
except for the unit-type count and three helpers (3.7).
The real differences are:

1. RotWK's **treasury economy**, absent in BFME2. BFME2 bounds recruitment by world command points instead.
2. The **hero army move allowance**: two regions in RotWK, one in BFME2.
3. RotWK's **territory bonus set** (seven new bonus kinds).
4. RotWK's **victory types** as a lobby rule, against BFME2's per-scenario game types.
5. RotWK's **Siege and Support autoresolve unit types**.
6. A **reworked strategic AI**.
7. The **map and data**: Angmar, 15 new regions, Gondor split, other armies.

## 3. Differences by area

Verdicts: **same**, **differs** (how), **RotWK-only**, **BFME2-only**.

### 3.1 Strategic map, regions, territories

| Item | Verdict | Sources |
|---|---|---|
| Map model | differs: a different `art\w3d\li\livingmap.w3d` (BFME2 w3d.big 5,624,227 B, RotWK W3D.big 6,255,764 B). RotWK's map reaches north (Angmar). | archives; `livingworld.ini` |
| `LivingWorldMapInfo` | differs (data): RotWK adds `AddShadowSubObject` LM_CarnDum / LM_Fornost / LM_AmonSul, `AptZoom` 0.85 -> 0.95 and `HeroArmyIconDiameter` 40 -> 30. It also adds `AngmarBanner`, `AngmarAnts` and the Arnor/Angmar icon includes. | `data\ini\livingworld.ini` |
| Region count | differs: BFME2 38, RotWK 52 | `campaigns\common\livingworldregions.inc` |
| BFME2-only region | **Gondor** (`MAP WOR Gondor`, ArmyBonus 60, CPLimit 600; connects Enedwaith, Harad, Ithilien, Minas_Tirith). RotWK splits it into Anfalas and Belfalas. | same file |
| RotWK-only regions (15) | Amon_Sul, Anfalas, Angmar, Barrow_Downs, Belfalas, Cardolan, CarnDum, Carrock, Fornost, Forodwaith, Lostriand, MountGundabad, Rhudaur, The_Brown_Lands, Umbar | same file |
| Shared regions (37) | differs, in every region: connections rewired around the new regions; `CPLimit` mostly raised (e.g. Erebor 360 -> 600, Mordor 600 -> 720); per-region bonuses reshuffled; `DetourPoint`s dropped (318 -> 4); army/building spots moved. Renamed: Arnor shown as "Arthedain", Buckland as "North Downs" with map `MAP WOR ANG North Downs`. Osgiliath and Redhorn_Pass allow 1 instead of 2 of a building type. | same file (per-region field diff done with a script) |
| Region bonus keys | BFME2 regions use `ArmyBonus` (14 regions, value 60), `ResourceBonus`, `LegendaryBonus`, `AttackBonus`, `DefenseBonus` and `ExperienceBonus`. RotWK drops `ArmyBonus` from the regions and adds **`FertileTerritoryBonus`** (11 regions, `FERTILE_TERRITORY_BONUS` = 500 treasury per turn, `gamedata.ini`). | data; port `LivingWorldData.cpp` field table (RG rows) |
| Territories (`ConcurrentRegionBonus`) | differs. BFME2 has 6: Arnor Exp +20, Eriador Attack +15, Gondor Defense +20, Mordor Exp +20, Rhovanion Defense +20, Rhun Attack +15. RotWK has 7: the six with new members plus **Northern Waste** (Angmar, MountGundabad, CarnDum, Forodwaith). All the old bonuses are commented out and replaced by the RotWK-only bonuses below. | regions file tail |
| RotWK-only territory bonuses | `BuildingDiscountBonus` (Arnor 200), `ExtraStartResourcesBonus` (Eriador 1000), `DiscountedHeroUnitsBonus` (Gondor), `DiscountedBarracksUnitsBonus` (Mordor 100), `DiscountedSeigeUnitsBonus` (Rhovanion), `FreeBuilderBonus` (Rhun), `FreeInnUnitsBonus` (Northern Waste). None of these strings is in the BFME2 decomp or the BFME2 data. | port `LivingWorldData.cpp` CRB rows 453-459; RW bonus totals 0x6E19A1 (unpaired, not decompiled, S-1327) |
| Region help text | differs (C, strings): RotWK's region description has the territory bonus lines (`LW:TerritoryBonusHeader`, `LW:UnifiedRegionBonus`, `LW:TerritoryPartOfRegion`, ...) | RW 0x7F2A76 (664 B) vs B2 0x3F15D1 (454 B) |
| Region xfer | same version (3); 15 bytes of code differ (edited), not read | RW 0x7F5279 vs B2 0x3F3BFF |
| Move validation (`ValidateArmyRegionEntry`) | **differs in code (census: B edited, 189 B; B2 0x20EC99)**. I read both: the rules match. An unknown or disabled region gives -1. Entry needs an owned or allied region, except a hero army outside phase 4. A missing source region gives 1. Otherwise the result is the path cost. The edit is how the path search is called. RotWK calls `FindShortestPath` RW 0x7F0C1A directly with a stack metric (vtable RW 0xBFA4F0). BFME2 calls it through the region set's path finder (`m_armySet->pathFinder->FindShortestPath`). No behaviour difference found, but not verified byte for byte. | RW 0x60F00C (Ghidra) vs B2 decomp `LivingWorldRegionManagerValidateArmyRegionEntry.cpp`; S-1324 |
| Path finding (A*, `FindShortestPath`) | differs or unpaired: RW 0x7F0C1A (291 B) has no xmap pair. The BFME2 path-finder TU has unpaired code in the same window (B2 0x3EFC1C). Whether path costs match is **not verified**. | RW 0x7F0C1A; section 4 |


**More differing functions (census 2026-10-29), 8.** B2 is an rva; byte sizes are RotWK vs BFME2.

| RW | Name (census) | Census class | B2 | What differs |
|---|---|---|---|---|
| 0x60E985 | `LivingWorldRegionManager::UpdateBattleMarkers` | divergent (edited) | 0x20EBA0 | Battle markers on the map after a move phase; code edited (258 vs 249 B). Not verified. |
| 0x6103E2 | `LivingWorldRegionManager::rva00210620` | divergent (same-shape, address,constant) | 0x210620 | Region sub-block parse: allocates a region of 0x1D4 bytes, BFME2 0x1B4 (RotWK region is 32 B larger). |
| 0x610CD6 | `FUN_00610cd6` | RotWK-only | - | Region campaign helper (port cites it from LivingWorldData.h / GodotLivingWorld.cpp); no BFME2 pair. Not verified. |
| 0x7F1029 | `LivingWorldRegion::rva003EFDB3` | divergent (A, call) | 0x3EFDB3 | Region CP limit (CPLimit / AllyCPLimit) for the HUD; calls RotWK-only RW 0x71A8F9 where BFME2 calls B2 0x318FBE (army CP total). Logic same shape. |
| 0x7F4DC8 | `LivingWorldRegion::rva003F3708` | divergent (C, diverged) | 0x3F3708 | Region conquered notice (`APT:LivingWorldRegionConqueredNo...`); C, 537 vs 607 B. Not verified. |
| 0x7F5AA6 | `Rva003F44A9::rva003F44ED` | divergent (edited) | 0x3F44ED | Region army test (is a hero army present); edited, 62 vs 75 B. Not verified. |
| 0x7F8C43 | `FUN_007f8c43` | RotWK-only | - | Region attack-bonus percent multiply for the battle bonuses; no BFME2 pair (part of the bonus totals, 3.4). Not verified. |
| 0x7FAA42 | `Rva003F9258Parse` | divergent (edited) | 0x3F9258 | Region-campaign sub-block parser (port: the `Tutorial` row); edited, 124 vs 98 B. Not verified. |

### 3.2 Factions

| Item | Verdict | Sources |
|---|---|---|
| Playable factions | BFME2 6 (Men, Elves, Dwarves, Mordor, Isengard, Wild). RotWK 7 (+ **Angmar**). An Arnor template is commented out in RotWK. | `livingworldplayers.ini` |
| `LivingWorldPlayerTemplate` keys | RotWK adds `ScenarioStartResources` (3000), `ScenarioMaxResources` (default 25000, commented), `FactionDozerTemplateName` (porters) and `FactionInnUnitTemplateName` (free inn hordes). The CP values (`StartingWorldCP` 1500, `MaxWorldCP` 4500, hero CP 450) are the same. | same; port `LivingWorldData.h` +0x20 |
| Faction lists in data | RotWK appends `PlayerAngmar` to the autoresolve `Sides` lists and adds Angmar to the `LW_FORT` / `LW_BARRACKS` / `LW_ARMORY` / `LW_FARM` macros, icons (`angmaricons.ini`), build-plot icons, `playertemplate.ini` and banners | `riskcampaign.ini`, `livingworldautoresolveresourcebonus.ini`, `...sciencepurchasepointbonus.ini` |
| Banner creation | differs (C, one string) | RW 0x616AF0 `BannerUI::CreateBanner` vs B2 0x216DBC |


**More differing functions (census 2026-10-29), 1.** B2 is an rva; byte sizes are RotWK vs BFME2.

| RW | Name (census) | Census class | B2 | What differs |
|---|---|---|---|---|
| 0x5FFA44 | `Rva002000D7Config::rva00200157` | divergent (edited) | 0x200157 | RankInfo side-name test (which faction column a rank row uses); edited 222 vs 195 B: RotWK lists Angmar. Probable, not verified. |

### 3.3 Scenarios, starting armies, buildings

| Item | Verdict | Sources |
|---|---|---|
| Scenario list | BFME2 43 + tutorial: 001-011 Capital Assault, 012-022 Elimination, 023-040 World Domination (20/30/All), 041 Stronghold Struggle, 042/043 War of the North/South, `WOTRTutorial` included. RotWK has 13 + 2: 001-011, 042, 043, plus **044 Historical** and **045 Fall of Arnor**. The tutorial is commented out, and 012-041 are not included because their game types became victory types (3.6). | `campaigns\riskcampaign.ini`, `campaigns\scenarios\*.inc` |
| Shared scenarios 001-011, 042, 043 | differs: other start spots and disabled-region lists for the new map (e.g. 002 BFME2 Enedwaith / Mount_Doom / Ettenmoors / Iron_Hills / Lothlorien / Cair_Andros, RotWK Mordor / Iron_Hills / Rohan / Lothlorien / Cardolan / Arnor) and other ownership sets | scenario diffs |
| RotWK-only scenario keys | `HistoricalScenario`, `DisabledFactions`, `UseMpRulesVictoryCondition`, `StartingRestriction` / `Factions`, and `MinPlayers` in data. BFME2 code knows `MinPlayers`; the BFME2 data never uses it. | data; scenario `ParseINI` RW 0x902874 (264 B) vs B2 0x4FD94E (128 B), tier C |
| Default armies (`SpawnArmy`, act start) | BFME2: 3 heroes + 1 army per faction (24). RotWK: 4 heroes + 1 army per faction, Angmar included (35). Mordor changes most: BFME2 Witch-king, FellBeast1, FellBeast2; RotWK Witch-king, FellBeast1 (Khamul), Mouth of Sauron, Gothmog. | `livingworlddefaultarmies.inc` |
| Starting units (`LivingWorldPlayerArmy`) | BFME2 32, RotWK 63: +32 RotWK-only (Angmar, `*_Historical`, Eomer, Glorfindel, Gothmog, Azog, ...); BFME2-only `FellBeast2PlayerArmy` | `livingworldstartingunits.inc` |
| Buildable units | BFME2 59, RotWK 109: +53 RotWK-only (Angmar, Arnor-named, siege: GondorTrebuchet, DwarvenCatapult, Ents, ...); BFME2-only Mordor Black Rider 1-3 | `livingworldbuildableunits.inc` |
| Buildings | BFME2 24 (4 per faction), RotWK 28 (+4 Angmar). Every building changed: `TurnsToBuild` fortress 2 -> 1, unit `BuildTime` 1-3 turns -> all 1, `StrategicResourceCost` added (fortress 1500, barracks 500, forge 500, farm 0). | `livingworldbuildings.ini`; `gamedata.ini` `WOTR_*_COST` |
| `IncreaseTreasury` nugget | RotWK-only (14 nuggets, `TreasureAmount` = `GAIN_PER_FORTRESS` / `GAIN_PER_FARM` = 300) | same; vtable RW 0xC8C7DC, effect RW 0x9D3F70 (unpaired) |
| Other nuggets | same kinds (`SpawnArmy`, `UpgradeTroops`, `StrengthenArmy`, `IncreaseCommandPoints`) | same |
| Production phase | same shape (`runProductionPhase` RW 0x8FF849 vs B2 0x4FAA81, B edited) | xmap |


**More differing functions (census 2026-10-29), 11.** B2 is an rva; byte sizes are RotWK vs BFME2.

| RW | Name (census) | Census class | B2 | What differs |
|---|---|---|---|---|
| 0x6917BB | `FUN_006917bb` | RotWK-only | - | `Object::fillEntry` (battle result capture: experience, level, upgrades as names); no BFME2 pair (window B2 0x2910B7, 225 B). Not verified. |
| 0x6B07EF | `Player::initFromDict` | divergent (edited) | 0x2AFCDF | `Player::initFromDict`: the RTS battle player gets StartingCashRTSWithFort when the region has a fort; edited, 2832 vs 2762 B (also RTS changes). Not verified. |
| 0x6B7229 | `LivingWorldLogic::spawnArmy` | divergent (edited) | 0x2B65B7 | `spawnArmy`: hero entries take the revival entry of the ArmyToSpawn when the list holds it; edited, 420 vs 438 B. What changed not verified. |
| 0x71AAEC | `Rva003193EC::rva003191B1` | divergent (edited) | 0x3191B1 | Army member disband (MSG_LW_DISBAND_ARMY_MEMBER); edited, 264 vs 256 B. Not verified. |
| 0x80EE5A | `ArmySummaryEntry::xfer` | divergent (edited) | 0x40C4FE | `ArmySummaryEntry::xfer`: edited, 177 vs 154 B (save of an army entry; probably the upgrade set). Not verified. |
| 0x80F237 | `Rva0040C804Target::check` | divergent (edited) | 0x40C804 | `FullyUpgraded` check of an army entry (armory set); edited, 334 vs 326 B. Not verified. |
| 0x80F754 | `ArmySummary::getSpawnPositions` | divergent (edited) | 0x40CCC6 | `ArmySummary::getSpawnPositions`: where a WotR army (and its reinforcements) enters an RTS battle; edited, 717 vs 655 B. Not verified. |
| 0x812119 | `FUN_00812119` | RotWK-only | - | `ArmySummary::Load` (reinforcement placement); no BFME2 pair (B2 window 0x40F497, 846 B, probable). Not verified. |
| 0x8125FC | `ArmySummarySystem::rva0040F87E` | divergent (edited) | 0x40F87E | `ArmySummarySystem` load override (garrison units with the garrison id); edited, 633 vs 640 B. Not verified. |
| 0x901568 | `FUN_00901568` | RotWK-only | - | Scenario text helper (DisplayDefeatedText label); no BFME2 pair. Not verified. |
| 0x93385D | `LivingWorldCampaign::LivingWorldCampaign` | divergent (edited) | 0x52CF0F | `LivingWorldCampaign` ctor: same defaults (SecondsPerReinforcement 180, StartingCashRTS 7500, WithFort 2500, revival 2.0 / 1.0); RotWK has 12 more bytes of members before them (+0x38 -> +0x44) and one more bool (probably the victory types; not verified). B2 decomp `Common/LivingWorldCampaign.cpp`. |

### 3.4 Resources, command points, treasury

| Item | Verdict | Sources |
|---|---|---|
| Treasury | **RotWK-only**. `addTreasure` keeps start + accumulated (+0x318) and clips at `ScenarioMaxResources`; the income is `FertileTerritoryBonus` + `IncreaseTreasury`, paid at turn end. BFME2's `LivingWorldPlayer` has no such field (its xfer below has none). | RW 0x6E1A40 (unpaired), RW 0x6E291D income (unpaired), S-1326, S-1327 |
| Building cost | **RotWK-only** (`StrategicResourceCost` less `BuildingDiscountBonus`) | RW 0x6E0B4A, RW 0x7F31CD / 0x7F3120 (unpaired; the B2 window holds 0x3F1B7C / 0x3F1BD3, which have no cost data to read) |
| Recruit gate | differs. BFME2 `canQueueUnit` checks **world command points**: used + queued + the unit <= the player's limit. RotWK `canQueueUnit` checks **the treasury** (the army's cost <= treasury) and no command points. | B2 0x4FA83E canQueueUnit -> B2 0x2E1155 (decomp `LivingWorldPlayer.cpp`, queued CP at +0x298); RW 0x8FF50E (unpaired; port `LivingWorldLogic.cpp` canRecruit) |
| Recruit cost, refunds | **RotWK-only**: cost RW 0x80F018, dequeue refund RW 0x8FF6F8, disband refund RW 0x8108DC, queue-and-pay RW 0x8FFC4A. BFME2's counterparts (probable: B2 0x4FA9B1, 0x4FAEAA, 0x40DDD6) were not read. | address-order pairing |
| Region CP limits (`CPLimit`, `AllyCPLimit`) | same keys, other values (3.1) | data |
| Army icon CP caps (`Small/MediumArmyCommandPoints` 120/240) | same | `riskcampaign.ini` |
| Strategic HUD economy rows | **RotWK-only**: `TreasuryIncomeRowTitle/Help`, `UnitSiege`, `UnitSupport`. The HUD stats reader is unpaired: RW 0x97E280 (254 B) vs B2 window 0x574293 (165 B). | `strategichud.ini` |
| AI template | differs: RotWK adds `DesiredSiegeRatio` 10, `DesiredSupportRatio` 10 and `BonusPreferenceTreasury` 6; changes `BuildingScoreArmory` 30 -> 50, `Barracks` 50 -> 40, `Castle` 50 -> 40 and `BonusPreferenceLegendary` 1 -> 4 | `livingworldaitemplate.ini`; parser RW 0x8308FC vs B2 0x420A29 (B edited) |


**More differing functions (census 2026-10-29), 8.** B2 is an rva; byte sizes are RotWK vs BFME2.

| RW | Name (census) | Census class | B2 | What differs |
|---|---|---|---|---|
| 0x619071 | `FUN_00619071` | RotWK-only | - | Sets the player power points (+0x1F0); no BFME2 pair. Not verified. |
| 0x6B4440 | `Rva002B3A48::rva002B3A48` | divergent (same-shape, call,offset) | 0x2B3A48 | Groups' CP without heroes (attack-repelled notice); same shape, calls RW 0x71A8F9. |
| 0x6E189C | `FUN_006e189c` | RotWK-only | - | AI level from the lobby slot state; no BFME2 pair (window B2 0x2E0675, 18 B). Not verified. |
| 0x6E1BB0 | `Rva002E0906::Rva002E0906` | divergent (C, diverged) | 0x2E0906 | `LivingWorldPlayerTemplate` ctor: RotWK sets ScenarioMaxResources 25000 (C, 143 vs 100 B, string DefaultGoodArmyIcon). RotWK-only fields. |
| 0x6E1F6C | `Rva002E0C68::rva002E0C68` | divergent (same-shape, call,offset) | 0x2E0C68 | Player armies CP total; same shape, calls RotWK-only RW 0x71A8F9 (BFME2: B2 0x318FBE). |
| 0x71A8F9 | `FUN_0071a8f9` | RotWK-only | - | Army CP total incl. heroes (-> ArmySummary RW 0x80FA5D); unpaired, 8 B thunk; the BFME2 caller target differs (B2 0x318FBE). Probably the same rule; not verified. |
| 0x7F1354 | `FUN_007f1354` | RotWK-only | - | AI CP check for a unit in a region (`cpOk`); no BFME2 pair. Not verified. |
| 0x80F67F | `FUN_0080f67f` | RotWK-only | - | ArmySummary key lookup used by the AI unit tally; no BFME2 pair (window B2 0x40CC1B). Not verified. |

### 3.5 Turn, phase and battle rules

| Item | Verdict | Sources |
|---|---|---|
| Seven phases, advance conditions, `EndTurn`, `AdvanceTurnPhase`, `CheckTurnPhaseTransitions`, `IsCurrentTurnPhaseFinished` | **same** (A) | RW 0x6BDD98, 0x6BDEC7, 0x6BDF30, 0x6B5F78 = B2 0x2BD23E, 0x2BD36D, 0x2BD3D6, 0x2B5390 |
| `UpdateTurnPhase`, `CheckHeadToHeadBattles`, `OnBattleComplete`, `RemovePlayer` | same shape (B) | RW 0x6BE20A, 0x6BD97E, 0x6BBA1C, 0x6B89D4 |
| Hero army move allowance | **differs (verified)**. BFME2: every army may make a move of path cost 0..1. RotWK: a garrison 0..1, a hero army (named army) 0..2, i.e. two regions across one friendly region. | B2 0x2B27B5 (decomp `LivingWorldLogic.cpp` 1586: `result >= 0 && result <= 1`); RW 0x6B57E7 -> RW 0x6B57B0 (Ghidra: empty name < 2, else < 3) |
| Army member moves | differs (D, 118 vs 116 B), not read; probably the same allowance | RW 0x6B77AC vs B2 0x2B6B5B |
| `ARMY_MOVE_REQUEST` handler | differs, probable counterpart (RW 0x6B5847, 142 B vs B2 0x2B28B1 `armyMoveRequest`, 164 B), not read | address order |
| Retreats (`PrepareRetreats`) | differs: RW 0x6BABA8 (782 B) vs B2 0x2B9FDB (1047 B), probable counterpart, not read | address order; port `LivingWorldLogic.cpp` 1984 |
| `ArmyRetreatRounds` | differs (data): BFME2 25 autoresolve rounds for a retreating army, RotWK 8 | `riskcampaign.ini` |
| RTS reinforcements | differs (data): `SecondsPerReinforcement` 900 -> 300, `StartingCashRTSWithFort` 1000 -> 1500 (`StartingCashRTS` 6000 and the revival multipliers the same) | `livingworlddefaultrtssettings.inc` |
| Garrison units consumed by an RTS battle | differs (D, 1132 vs 1160 B), not read | RW 0x6BC7F0 vs B2 0x2BBD52 `ConsumeGarrisonUnitsFromRTS` |
| Battle start (bases, plots), battle aftermath | same shape (`PrepareBattleForLoading` B, `GameLogic_tacticalBattleComplete` A) | RW 0x6B851C, 0x6BCC5C |
| Victory evaluation | same shape (B) | RW 0x6BA994 vs B2 0x2B9DC7; conditions A (`isTeamDefeated` B2 0x4FC970) |


**More differing functions (census 2026-10-29), 9.** B2 is an rva; byte sizes are RotWK vs BFME2.

| RW | Name (census) | Census class | B2 | What differs |
|---|---|---|---|---|
| 0x6B2EFA | `Rva002B2435Get` | divergent (edited) | 0x2B2435 | Turn phase getter (xfer name "LivingWorldTurnPhase"); edited 42 vs 49 B. Not verified. |
| 0x6B45C7 | `LivingWorldLogic::AdjustArmyTargetLocations` | divergent (same-shape, constant) | 0x2B3BCF | `AdjustArmyTargetLocations` (starts army moves): same code, the player's army vector moved from +0x1B8 to +0x1E4 (RotWK LivingWorldPlayer is larger). |
| 0x6BA21C | `LivingWorldLogic::~LivingWorldLogic` | divergent (A, call,call-unpaired) | 0x2B964F | `~LivingWorldLogic`: A, differs only in a call target (RW 0x5B468A vs B2 0x5B4E74). |
| 0x6BE50E | `FUN_006be50e` | RotWK-only | - | `LivingWorldLogic::update` frame counter step; no BFME2 pair (12 B). Not verified. |
| 0x779A3D | `GameLogic::logicMessageDispatcher` | divergent (edited) | 0x377613 | `GameLogic::logicMessageDispatcher` (whole game; cited for the WotR battle-complete path): edited, 14315 vs 14202 B. Not WotR-specific; not verified. |
| 0x7F635E | `LivingWorldBattle::rva003F4D09` | divergent (edited) | 0x3F4D09 | Battle reference player (region owner else first record); edited 160 vs 165 B. Not verified. |
| 0x901AA3 | `LivingWorldScenario::PlayerDefeatCondition::QueryRegionsAndNumbers` | divergent (D, diverged) | 0x4FCDCF | `PlayerDefeatCondition::QueryRegionsAndNumbers` (capital first when LoseIfCapitalLost); D, 144 vs 114 B. Not verified. |
| 0x9026F2 | `FUN_009026f2` | RotWK-only | - | Victory: collect lost players; no BFME2 pair. Not verified. |
| 0x902768 | `FUN_00902768` | RotWK-only | - | Victory: collect beaten teams; no BFME2 pair. Not verified. |

### 3.6 Victory and game types

| Item | Verdict | Sources |
|---|---|---|
| Game type selection | differs. BFME2 bakes the game type (Capital Assault / Elimination / World Domination 20/30/All / Conquest) into each of 43 scenarios. RotWK chooses it in the lobby: **`LivingWorldVictoryType`** blocks (7) and the `RULE:VictoryType` rule. | `livingworldvictorytypes.inc` (RotWK-only file, included from the RTS settings); port `MpGameRules.cpp` kVictoryType; RW 0x932BF7 (unpaired) |
| Lobby rule table | differs. BFME2 labels (index 5) `RULE:MapRevealMode`; RotWK puts `RULE:VictoryType` at index 5. Both have `StrategicPhaseTimer`, `BattleType`, `BattleChoice` and `AutoResolveType`. | B2 `Code/GameEngine/Source/Common/RTS/MpGameRules.cpp` 149; port `Common/MpGameRules.cpp` (RW 0xDB7794) |
| `UseMpRulesVictoryCondition` | RotWK-only (044/045 force their own conditions) | data |
| Campaign xfer | differs (D, 68 vs 54 B). RotWK writes one int at +8 (version 1); BFME2's body was not compared. | RW 0x932570 (Ghidra) vs B2 0x52BAFE |

### 3.7 Autoresolve

| Item | Verdict | Sources |
|---|---|---|
| Battle engine (rounds, targets, attacks, leadership, `completeRound`, winner, `applyResults`) | **mostly the same, not throughout.** The round, attack, leadership, `completeRound`, winner and `applyResults` functions are A/B. These parts differ: **ChooseTargets** RW 0x8FE401 (B2 0x4F971E) uses INVALID type 9 vs 7 and per-type lists [10][2] vs [8][2]. The battle ctor RW 0x8FF0AC (edited) and the unit ctor RW 0x9A38A1 (constants) differ. Three helpers have no BFME2 pair: re-sort RW 0x8FEB3C, tracker copy RW 0x8FAD90, can-attack RW 0x9A3573. The weapon/armor pick RW 0x73C1E5 / 0x73C222 has no xmap pair, but BFME2 has the same selection (decomp `LivingWorldAutoResolveBattle.cpp` 549-555), probably as unpaired twins. | RW 0x8FAF8E..0x8FF0AC vs B2 0x4F6187..0x4FA3C9; RW 0x9A34B5..0x9A38A1 vs B2 0x59ADB9..0x59B1EC; census table |
| Unit types | **differs**. BFME2 has 7 types + `AutoResolveUnit_INVALID` = 7 (Soldier, Archer, Pikemen, Cavalry, Monster, Hero, Fortress). RotWK adds **Siege** and **Support** (INVALID = 9). The ThingTemplate default (+0x5D4) and every per-type table size follow. BFME2's AI keeps `int[8]` per-type counts (B2 0x59D90C). | B2 `INI_VeterancyLevelToken.cpp` `g_Va00DC85C4Names[9]`; port `LivingWorldAutoResolveData.cpp` (RW 0xDADBE4) |
| `SetUpAutoResolveBattle` | differs (D, 1059 vs 843 B). The extra RotWK code was not read; candidates are the free inn units / auto fort extra units. Not verified. | RW 0x6BD3F3 vs B2 0x2BC971 |
| Combat chains | differs (data): RotWK adds `AutoResolve_SiegeCombatChain` and `AutoResolve_SupportCombatChain`; Hero chain target priority 25 -> 35 | `livingworldautoresolvecombatchain.ini` |
| Armor / weapon / body | differs (data): +37 / +47 / +30 RotWK-only blocks (Angmar, Arnor, siege, support); all 94 armor and 103 weapon blocks changed (new vs-Siege/Support scalars); 4 bodies changed | `livingworldautoresolve{armor,weapon,body}.ini` |
| Leadership | RotWK adds `AutoResolve_AngmarWitchKingBonus` and `AutoResolve_MorgramirBonus` | `...leadership.ini` |
| Reinforcement schedule | differs (data): armies 2/3/4 join at rounds 50/100/150 (BFME2) vs 15/30/45 (RotWK) | `...reinforcementschedule.ini` |
| Handicaps | same | `...handicaps.ini` |
| Resource / science purchase point bonus | same, plus Angmar in `Sides`; the resource bonus parser differs (C, one string) | RW 0x824B8A vs B2 0x41428F |


**More differing functions (census 2026-10-29), 31.** B2 is an rva; byte sizes are RotWK vs BFME2.

| RW | Name (census) | Census class | B2 | What differs |
|---|---|---|---|---|
| 0x42E840 | `INI::parseBitString32` | divergent (edited) | 0x2EB38 | `INI::parseBitString32`: edited 277 vs 310 B (generic parser, cited for a flag field). Not verified. |
| 0x42E956 | `FUN_0042e956` | RotWK-only | - | ThingTemplate `AutoResolveUnitType` index-list row; no BFME2 pair (the name table is 9+1 vs 7+1). |
| 0x441B7C | `Rva0004224C::~Rva0004224C` | divergent (D, diverged) | 0x42262 | Science purchase point cap (multiplayer: GameLogic +0x110 is 1 or 5); D, 65 vs 62 B. Not verified. |
| 0x441BBD | `FUN_00441bbd` | RotWK-only | - | War bonus added to purchase points; no BFME2 pair. Not verified. |
| 0x66F603 | `Rva0026F28BParse` | divergent (same-shape, address,call-unpaired,constant) | 0x26F28B | Upgrade-mask parser: mask 0x90 vs 0x80 bytes (36 vs 32 words). |
| 0x6B2FD7 | `Rva002E55D8::Rva002E55D8` | divergent (A, call) | 0x2E55FF | Autoresolve tracker base ctor: A, differs only in a call target. |
| 0x6B68F3 | `FUN_006b68f3` | RotWK-only | - | Autoresolve setup helper (names); no BFME2 pair. Not verified. |
| 0x71C311 | `FUN_0071c311` | RotWK-only | - | Tracker scalar base rank set; no BFME2 pair. Not verified. |
| 0x73BE09 | `Rva0033A453::rva0033A453` | divergent (same-shape, constant) | 0x33A453 | Upgrade-mask test for the weapon/armor set: `cmp 0x24` vs `0x20` (mask 36 vs 32 words). |
| 0x73C1E5 | `FUN_0073c1e5` | RotWK-only | - | Current AutoResolveArmor by upgrades; no xmap pair, but BFME2 has the same selection (decomp `LivingWorldAutoResolveBattle.cpp` 549-555 getCurrentLivingWorldAutoResolveArmor): probably an unpaired twin. |
| 0x73C222 | `FUN_0073c222` | RotWK-only | - | Current AutoResolveWeapon by upgrades; same as above (getCurrentLivingWorldAutoResolveWeapon). |
| 0x73FB7A | `ThingTemplate::ThingTemplate` | divergent (edited) | 0x33E1B8 | `ThingTemplate` ctor (AutoResolveUnitType default 9 vs 7, among RTS fields); edited 1879 vs 1875 B. |
| 0x7F7C8D | `FUN_007f7c8d` | RotWK-only | - | `LivingWorldBattle::TestBattle` (the AI's simulated autoresolve); no BFME2 pair. Not verified. |
| 0x824722 | `FUN_00824722` | RotWK-only | - | Resource bonus: the side whose Sides list holds the player template; no BFME2 pair. Not verified. |
| 0x8287B6 | `FUN_008287b6` | RotWK-only | - | Leadership helper; no BFME2 pair. Not verified. |
| 0x8294B7 | `Rva00418BB7Less` | divergent (edited) | 0x418BB7 | Combat chain {type, priority} compare; edited 47 vs 44 B. Not verified. |
| 0x8295CB | `LivingWorldAutoResolveCombatChain::getHighestPriorityTarget` | divergent (same-shape, constant) | 0x418CC8 | `CombatChain::getHighestPriorityTarget`: type bound `cmp 10` vs `cmp 8`. |
| 0x8297EE | `Rva00418EEB::rva00418EEB` | divergent (same-shape, constant) | 0x418EEB | Combat chain next target: type bound 10 vs 8. |
| 0x829DD9 | `Rva004194D6::rva004194D6` | divergent (same-shape, address,constant,offset) | 0x4194D6 | Weapon `canDamage`: type bound 10 vs 8. |
| 0x829E64 | `Rva004194D6::rva00419561` | divergent (same-shape, address,constant,offset) | 0x419561 | Weapon `getDamageForRound`: type bound 10 vs 8. |
| 0x829F6C | `Rva00419669::Rva00419669` | divergent (same-shape, address,call-unpaired,constant,offset) | 0x419669 | Weapon copy ctor: per-type tables of 10 vs 8. |
| 0x83687D | `FUN_0083687d` | RotWK-only | - | Armor `getDamageScalar(type)`; no BFME2 pair (window B2 0x4270FA, 26 B). Probably only the bound; not verified. |
| 0x8368EC | `Rva00427157::Rva00427157` | divergent (same-shape, constant) | 0x427157 | Armor ctor (all 1.0): 10 vs 8 scalars. |
| 0x8FAD90 | `FUN_008fad90` | RotWK-only | - | SciencePurchasePointTracker copy for the battle record; no BFME2 pair. Not verified. |
| 0x8FE401 | `LivingWorldAutoResolveBattle::rva004F971E` | divergent (same-shape, address,call-unpaired,constant,frame,offset) | 0x4F971E | ChooseTargets: INVALID type 9 vs 7 (two places), a 0x14 vs 0x10 per-type stack block, list arrays [10][2] vs [8][2] (B2 decomp `LivingWorldAutoResolveBattleChooseTargets.cpp` lists[8][2]). |
| 0x8FEB3C | `FUN_008feb3c` | RotWK-only | - | Re-sort both sides after a round; no BFME2 pair (probably inlined or a twin with another type bound). Not verified. |
| 0x9188EB | `IsCRC` | RotWK-only | - | A 14-byte virtual (`IsCRC`/AIBusyState slot) the tracker header cites; no BFME2 pair. Not verified. |
| 0x9A3573 | `FUN_009a3573` | RotWK-only | - | Unit can-attack (not attacked, hp > 0, weapon canDamage); no BFME2 pair. Not verified. |
| 0x9EE028 | `FUN_009ee028` | RotWK-only | - | Autoresolve experience tracker: rank-up flag; no BFME2 pair. Not verified. |
| 0x9EE04A | `FUN_009ee04a` | RotWK-only | - | Autoresolve experience tracker: addExperiencePoints + level check; no BFME2 pair (window B2 0x5DB023). Not verified. |
| 0x9F3A3C | `FUN_009f3a3c` | RotWK-only | - | A 12-byte stub the tracker cites; no BFME2 pair. Not verified. |

### 3.8 Heroes

| Item | Verdict | Sources |
|---|---|---|
| Hero armies, recruit at a fortress `SpawnArmy` | same mechanism. Heroes take 2-3 turns in BFME2, 1 turn (and treasury) in RotWK. RotWK adds the `DiscountedHeroUnitsBonus`. | `livingworldbuildings.ini` |
| One hero queued at a time, buildable-hero list | probable counterparts differ: `FindArmyWithHero` B2 0x2E1257 (506 B) vs RW 0x6E2589 / 0x6E2625 / 0x6E267E. Not read. | address order |
| Revival | data same (`InitialRevivalCostMultiplier` 2.0); `LivingWorldPlayerCreateRevival` and `ExtractRevival` were not compared one by one | data; B2 decomp TU names |
| Ranks, experience in autoresolve | same (A) | `ExperienceTrackerAutoResolve` A |
| Create-a-Hero army | same in data (`MoW_CreateAHeroArmy` in both); the port refuses it in both editions (S-1326 inference) | data |


**More differing functions (census 2026-10-29), 5.** B2 is an rva; byte sizes are RotWK vs BFME2.

| RW | Name (census) | Census class | B2 | What differs |
|---|---|---|---|---|
| 0x61FB68 | `CreateAHeroManager::CreateAHeroManager` | divergent (edited) | 0x21FE49 | `CreateAHeroManager` ctor: edited 441 vs 421 B (RotWK Create-a-Hero changes). Not WotR-specific. |
| 0x6B16EC | `Player::initBuildableHeroes` | divergent (same-shape, address,call-unpaired,constant,offset) | 0x2B0B96 | `Player::initBuildableHeroes`: record 0xE8 vs 0xD8. |
| 0x6E281B | `LivingWorldPlayer::rva002E14E9` | divergent (edited) | 0x2E14E9 | Player persistent upgrades (completed PLAYER upgrades); edited 164 vs 159 B. Not verified. |
| 0x6E42DA | `LivingWorldPlayer::ExtractRevivalUnitDataForCurrentMap` | divergent (same-shape, address,call-unpaired,constant,frame,offset) | 0x2E2F44 | `ExtractRevivalUnitDataForCurrentMap`: revival record 0xE8 vs 0xD8 bytes (RotWK record 16 B larger). |
| 0x78100E | `UnitRevivalTracker::retrieveHeroesForArmySummary` | divergent (same-shape, address,constant,offset) | 0x37EB3C | `UnitRevivalTracker::retrieveHeroesForArmySummary`: record 0xE8 vs 0xD8. |

### 3.9 Strategic AI

| Item | Verdict | Sources |
|---|---|---|
| Dispatcher, attach, helpers | mostly A/B; the two dispatch helpers RW 0x900089 / 0x9000D4 are D | xmap |
| Garrison mover | differs a lot: RW 0x9A4205 (1910 B) vs probable B2 0x59BB50 (477 B) | address order; port `LivingWorldAIGarrisonMover.cpp` |
| Army mover | differs: `DefendHomeTerritory` RW 0x9A4C70 (590 B) vs B2 0x59C010 (331 B); bonus-strategy pass RW 0x9A53E6 (1057 B) vs B2 0x59C631 (779 B); RW 0x9A5807 vs B2 0x59C93C (D) | same |
| Builder | differs: `FillBuildingOrders` RW 0x9A63BD (2033 B) vs B2 0x59D2DA (1137 B); score helpers RW 0x9A5E68 / 0x9A5F6C / 0x9A610D vs B2 0x59CEE4 / 0x59CF34 / 0x59D089 / 0x59D1A9 / 0x59D1C3 / 0x59D1DD | same |
| Troop producer / recruit | differs: RW 0x9A6D13 / 0x9A6DE0 / 0x9A6F78 vs B2 0x59D8A5 / 0x59D90C; RW 0x9A71F7 vs B2 0x59DB48; RW 0x9A747A vs B2 0x59DD3F (D) | same |
| Inputs | RotWK spends the treasury (budget = treasury less recruits) and weighs siege/support ratios and the treasury preference. BFME2 has neither (3.4). | port `LivingWorldAI.h` |
| Test battles (`WinTestBattle`) | same shape (B edited, 1320 vs 1238 B) | RW 0x9A4EBE vs B2 0x59C15B |


**More differing functions (census 2026-10-29), 21.** B2 is an rva; byte sizes are RotWK vs BFME2.

| RW | Name (census) | Census class | B2 | What differs |
|---|---|---|---|---|
| 0x90018B | `FUN_0090018b` | RotWK-only | - | AI idle guard (every other player an ally or defeated); no BFME2 pair (window B2 0x5B0537). Not verified. |
| 0x9001FB | `FUN_009001fb` | RotWK-only | - | AI `GetBattleChoice` (the AI's battle vote); no BFME2 pair. Not verified. |
| 0x9004A6 | `Rva004FB582Owner::rva004FB582` | divergent (edited) | 0x4FB582 | First AI step: gathers the world information (only the treasury query is read); edited 141 vs 126 B. |
| 0x9006E5 | `LivingWorldAI::rva004FB7B2` | divergent (edited) | 0x4FB7B2 | End-of-phase garrison executor; edited 449 vs 370 B. Not verified. |
| 0x904F3A | `Rva004FF8DA::HasUnitOfType` | divergent (edited) | 0x4FF8DA | `HasUnitOfType`; edited 59 vs 54 B. Not verified. |
| 0x90526A | `Rva0059CFAACallee::rva004FFB00` | divergent (D, diverged) | 0x4FFB00 | Closest enemy hero; D, 269 vs 235 B. Not verified. |
| 0x9058A1 | `Rva004FF8DA::rva0050010D` | divergent (same-shape, address,constant,offset) | 0x50010D | Has unit to fight type: bound 10 vs 8, INVALID 9 vs 7 (+ line-number constants). |
| 0x905C14 | `Rva004FF8DA::rva0050024C` | divergent (same-shape, address,constant,offset) | 0x50024C | Get unit to fight type: bound 10 vs 8, INVALID 9 vs 7. |
| 0x905DA7 | `Rva005003DF::rva005003DF` | divergent (D, diverged) | 0x5003DF | Closest enemy region; D, 279 vs 289 B. Not verified. |
| 0x905FF9 | `FUN_00905ff9` | RotWK-only | - | Closest allied region with a filter; no BFME2 pair. Not verified. |
| 0x906134 | `FUN_00906134` | RotWK-only | - | Closest allied region with a filter (another); no BFME2 pair. Not verified. |
| 0x90652D | `Rva0059CFAACallee::rva00500659` | divergent (D, diverged) | 0x500659 | Closest enemy hero distance; D, 331 vs 279 B. Not verified. |
| 0x908F18 | `Rva00502FE9::rva00502FE9` | divergent (edited) | 0x502FE9 | AI building types (per faction building roles); edited 1400 vs 1387 B. Not verified. |
| 0x9095A7 | `LWAIWorldInformation::GatherWorldInformation` | divergent (edited) | 0x50366B | `LWAIWorldInformation::GatherWorldInformation` (the per-region snapshot); edited 1761 vs 1723 B (RotWK adds the treasury; probable). Not verified. |
| 0x9A3FB2 | `Rva0059BB50::rva0059B8FD` | divergent (same-shape, address,call-unpaired,constant) | 0x59B8FD | Garrison member distribution: type bound 10 vs 8. |
| 0x9A497B | `Rva0059BD2D::rva0059BD2D` | divergent (edited) | 0x59BD2D | Mover weights copy; edited 49 vs 43 B. |
| 0x9A5E10 | `UBfmeE16::_Construct<>` | divergent (D, diverged) | 0x59CE9E | Builder weights copy; D, 88 vs 70 B (RotWK weights larger). |
| 0x9A6004 | `LivingWorldAIBuilder::rva0059CFAA` | divergent (D, diverged) | 0x59CFAA | Builder score helper; D, 265 vs 223 B. Not verified. |
| 0x9A6BB5 | `Rva0059D74B::rva0059D74B` | divergent (edited) | 0x59D74B | Producer weight normalisation; edited 125 vs 121 B (probably the type count). Not verified. |
| 0x9A6C32 | `Rva0059DD3F::rva0059D7C4` | divergent (same-shape, constant,offset) | 0x59D7C4 | Player unit tally: 10 vs 8 types. |
| 0x9A70E3 | `Rva0059DA34::rva0059DA34` | divergent (same-shape, address,constant) | 0x59DA34 | Unit tally around a region: 10 vs 8 types. |

### 3.10 Save format (strategic chunk)

OpenBFME saves its own block (`obfme:LivingWorld`, S-1332). Retail `.BfME2WotR` files are read in neither edition.
These are the retail differences that matter if retail saves are ever read.

| Item | Verdict | Sources |
|---|---|---|
| `CHUNK_LivingWorldLogic` (`LivingWorldLogic::xfer`, v6), shared state (v7), delayed region victories, region manager, armies, battles, build plots, score keeper, award disputes | same (A/B) | RW 0x6BD0D5, 0x6BBD55, 0x6BA784, 0x60FFC4, 0x71BFD9, 0x7F7300, 0x902E0A, 0x8F3A89, 0x900D79 |
| `LivingWorldPlayer` xfer | **differs (verified)**: BFME2 version 5, RotWK version 6, adding the treasury accumulator (+0x318, `if version > 5`) | RW 0x6E445E (Ghidra) vs B2 0x2E30C8 (decomp `LivingWorldPlayerDtor.cpp` `Rva002E2AAE::xfer`, `Version(1,5)`) |
| Region, owning-army vector, eye tower xfers | same versions, small code edits (B edited), not read | RW 0x7F5279, 0x71C1DB, 0x7FB5E5 |
| Campaign xfer | differs (D) | RW 0x932570 vs B2 0x52BAFE |
| Enum-name xfers (`RegionBonusRuleID`, `Battle::ResolutionType`) | differs only in the debug name string (C) | RW 0x6E17E2, 0x7F563F |


**More differing functions (census 2026-10-29), 1.** B2 is an rva; byte sizes are RotWK vs BFME2.

| RW | Name (census) | Census class | B2 | What differs |
|---|---|---|---|---|
| 0x6E3C8B | `Rva002E2AAE::Rva002E2AAE` | divergent (edited) | 0x2E2903 | `LivingWorldPlayer` ctor (player id from the logic counter); edited 441 vs 427 B (the treasury fields; probable). |

### 3.11 LAN and network rules

| Item | Verdict | Sources |
|---|---|---|
| Does BFME2 1.06 have LAN WotR? | **Yes.** The LAN lobby has the `LanStrategic` mode, the online shell has `OnlineStrategic`, and the lobby rules have `StrategicPhaseTimer` / `BattleType` / `BattleChoice` / `AutoResolveType`. The strategic dispatcher has the battle vote, the exit / load / start votes and `CreateWOTRMPPlayers`. | B2 `BfmeAptScreenLanLobbyOnInitGadget.cpp` 848; `AptOnlineShellScreens.cpp` 209; `MpGameRules.cpp` 149; `LivingWorldLogic.cpp` 3810 ff. |
| Message set `MSG_LW_*` 1701-1728 | same numbering and handlers | dispatcher RW 0x6BE6A6 (C, 3264 B) vs B2 0x2BDB4C (3183 B) |
| Battle vote (0x6A7, RW 0x6BE9A5..0x6BEC30) | same: flags tally, retreat first, auto-resolve without a human, `BattleChoice` (TheGameInfo +0x80) ordering | B2 decomp case 1703; RW Ghidra |
| Every-human rule (real time only when every live human voted) | same | RW 0x6B5E3E = B2 0x2B5256 (B same shape) |
| Retreat loop inside the vote | **possible difference, not verified**: RotWK's loop leaves early after a human's retreat when GameLogic +0x114 is 0 (Ghidra, inside RW 0x6BEA94..0x6BEB9F); the BFME2 decomp loops over every player. This may explain part of the 81-byte size difference. | RW 0x6BE6A6 vs B2 0x2BDB4C |
| "Is multiplayer" test | same (GameLogic +0x114 is 1 or 2) | RW 0x610A21 (23 B, unpaired) probable twin of B2 0x210C66 (33 B) |
| Network save of a war (mode 6), Load in LAN | same feature in both per the save mode table; not compared in code | `docs/SAVES.md`, S-1340 |
| Lobby rule row 5 | differs (3.6): VictoryType vs MapRevealMode | above |
| Online WotR | exists in both retail games (GameSpy, dead); OpenBFME has none in either edition | S-1339 |

### 3.12 Shell and HUD

| Item | Verdict | Sources |
|---|---|---|
| `StrategicHUD.apt`, `LivingWorldUI.apt`, `StrategicEndTurnButton`, `StrategicDetailsRegion` / `ArmyRetinue` / `Tray`, `PalantirExport`, `InGamePlanningMode` | same (byte-identical archives) | `apt\*.big` byte compare |
| Same size, other bytes | `StrategicChecklist`, `ConflictResults`, `DetailsBuildQueue`, `DetailsTerritory`, `NextTurnInd`, `StrategicPalantir`, `RegionAward`. Content not compared. | same |
| Changed | `StrategicStats` (163,952 -> 304,376 B), `StrategicVeterancy`, `StrategicBattlePrompt` / `PlayerPage` / `ArmyPanel`, `StrategicDynamicAutoResolve`, `StrategicArmyUnitSwapper`, `StrategicDetailsArmies` / `Structures`, `StrategicPlayerStatus`, `BannerUI` | same |
| Setup screens | differ a lot. `SkirmishStrategic.big` is 7,402 B in BFME2 and 289,312 B in RotWK; `Skirmish.big` is 1,166,184 B and 43,888 B. `LanStrategic` and `OnlineStrategic` changed too. BFME2's strategic setup layout was not read. | same |
| Score screen | BFME2 has a `scorescreen.big` that RotWK lacks. Our port's WotR score screen is RotWK's TimeLine type 6. Which screen BFME2's WotR end uses is **not verified**. `LivingWorldScoreKeeper` is identical (A). | archives; S-1334 |
| Strategic HUD data rows | RotWK-only treasury and siege/support rows (3.4) | `strategichud.ini` |
| Tutorial | BFME2 includes `WOTRTutorial.inc`; RotWK comments it out. The tutorial code (`LivingWorldTutorial`) is present and identical in both (A) and is not ported. | `riskcampaign.ini`; RW 0x7F9CE2.. = B2 0x3F841D.. |


**More differing functions (census 2026-10-29), 1.** B2 is an rva; byte sizes are RotWK vs BFME2.

| RW | Name (census) | Census class | B2 | What differs |
|---|---|---|---|---|
| 0x921D5B | `AptQuitMenu::OnInitialized` | divergent (edited) | 0x51B5B5 | `AptQuitMenu::OnInitialized` (Save in a network war, S-1340); edited 866 vs 821 B. Not verified. |

## 4. BFME2-only LivingWorld code (no RotWK pair in the xmap)

Source: `edition_census.json` `bfme2_only`, subsystem "War of the Ring / LivingWorld".

**Total: 111 functions, 10,500 bytes.** They split into:

- **74 EH unwind funclets, 1,078 B** (`$L...`, `Unwind@...`, `uw_...` and two 35-byte `rva00783ac1` / `rva00793ba5`
  in the funclet range). These are compiler glue and need no port.
- **37 functions, 9,422 B.** All but one have a probable RotWK counterpart that diverged too far for the xmap to
  pair it (address order, section 1 method step 2). So they are the BFME2 versions of RotWK code our port already
  has, not BFME2 features.

| B2 rva | Bytes | Function (decomp name) | Decomp file | What it does | Probable RotWK counterpart |
|---|---|---|---|---|---|
| 0x20E205 | 75 | `~Rva0020E205` | LivingWorldRegionManager.cpp | a region-manager helper's destructor | RW 0x60DEBB..0x60E02D window (5 small functions) |
| 0x210C66 | 33 | `Rva00210C66CmpBoolField::get` | LivingWorldLogic.cpp | GameLogic "in a multiplayer game" (+0x114 in 1..2) | RW 0x610A21 (23 B) |
| 0x2B28B1 | 164 | `LivingWorldLogic::armyMoveRequest` | LivingWorldLogic.cpp | the army move order (target region and position) | RW 0x6B5847 (142 B) |
| 0x2B9FDB | 1047 | `LivingWorldLogic::PrepareRetreats` | LivingWorldLogic.cpp | phase-4 retreat preparation | RW 0x6BABA8 (782 B) |
| 0x2E07CC | 46 | `Rva002E2AAE::loadPostProcess` | LivingWorldPlayerDtor.cpp | LivingWorldPlayer post-load | RW 0x6E1A1E (34 B) |
| 0x2E1257 | 506 | `LivingWorldPlayer::FindArmyWithHero` | LivingWorldPlayer.cpp | finds the player's army holding a given hero | RW 0x6E2589 / 0x6E2625 / 0x6E267E |
| 0x2E1948 | 344 | `Rva002E1948Lookup::find` | Rva002BA8F1Lookups.cpp | player vector lookup by name | RW 0x6E2CC0..0x6E2D92 |
| 0x31996D | 165 | `AttackOrdersMember::rva0031996D` | ConsumeGarrisonUnitsFromRTS.cpp | army summary entry lookup by hero key | RW 0x71B26B / 0x71B2D0 |
| 0x3B8E2B | 94 | `LivingWorldCampaignManager::rva003B8E2B` | LivingWorldCampaignManager.cpp | campaign index by name (-1 unknown) | RW 0x7B988E / 0x7B98CF |
| 0x3EFC1C | 181 | `Rva003EFC1CHost::rva003EFC1C` | LivingWorldPathFinder.cpp | adjacent reachable regions after relaxation | RW 0x7F0E2B / 0x7F0EB3 |
| 0x3F0F48 | 91 | `LivingWorldRegion::rva003F0F48` | LivingWorldRegion.cpp | erase a pointer from a region's slot vector | RW 0x7F2121 |
| 0x3F1B7C | 87 | `LivingWorldRegion::rva003F1B7C` | LivingWorldRegionRva003F1B7C.cpp | can-build check for a plot / template | RW 0x7F3120 / 0x7F31CD (RotWK adds the cost) |
| 0x3F1BD3 | 79 | `LivingWorldRegion::rva003F1BD3` | LivingWorldRegionRva003F1BD3.cpp | sibling of the above | same |
| 0x3F1CFE | 37 | `Rva003F1093::rva003F1CFE` | LivingWorldRegion.cpp | plot lookup then the region's destroy/build member | RW 0x7F32F4 / 0x7F337F |
| 0x3F7023 | 340 | `LivingWorldBattle::DoXfer` | LivingWorldBattleDoXfer.cpp | battle save/load | not found (the window has RW 0x7F87B2, an AI helper; low confidence) |
| 0x3F7177 | 93 | `Rva003F7177Callback` ctor | LivingWorldPathFinder.cpp | path-finder callback (player, region) | same window, low confidence |
| 0x4FA83E | 247 | `LivingWorldBuildingNuggetSpawnArmy::canQueueUnit` | ...CanQueueUnit.cpp | queue limit, producible, logic check, **world CP check** (B2 0x2E1155) | RW 0x8FF50E (319 B; treasury check instead) |
| 0x4FBF8E | 206 | `RegionAwardDispute::rva004FBF8E` | RegionAwardDispute.cpp | creates the disputed region's beacon | RW 0x900F10 / 0x900F84 |
| 0x4FC33E | 158 | `Rva003F1C56Plot::rva004FC33E` | LivingWorldBuildPlot.cpp | build plot constructs a building (id) | RW 0x902B96 (209 B) |
| 0x4FD0DA | 226 | `Rva004FD1BC` ctor | Rva004FD0DACtor.cpp | `LivingWorldScenario::Scenario` constructor (0xB0 B) | RW 0x901DD5 / 0x901E84 / 0x901FA7 (RotWK's scenario object is bigger: new keys) |
| 0x4FD1BC | 451 | `~Rva004FD1BC` | LivingWorldScenarioDtor.cpp | its destructor (condition lists) | same window |
| 0x52BE96 | 90 | `Rva004E194E` copy ctor | LivingWorldCampaign.cpp | copy of a campaign record (ints + string) | RW 0x932960 |
| 0x59BB50 | 477 | `Rva0059BB50::rva0059BB50` | Rva0059BB50GarrisonMoves.cpp | AI garrison moves | RW 0x9A4205 (1910 B) |
| 0x59C010 | 331 | `LivingWorldAIArmyMover::DefendHomeTerritory` | ...DefendHomeTerritory.cpp | AI: closest hero defends home | RW 0x9A4C70 (590 B) |
| 0x59C631 | 779 | `LivingWorldAIArmyMover::rva0059C631` | LivingWorldAISupport/LivingWorldAIArmyMover.cpp | AI: two simulated battles to choose a move | RW 0x9A53E6 (1057 B) |
| 0x59CEE4, 0x59CF34 | 80, 118 | `LivingWorldAIBuilder` helpers | LivingWorldAIBuilderScores.cpp | builder scoring helpers | RW 0x9A5E68 / 0x9A5F6C |
| 0x59D089, 0x59D1A9, 0x59D1C3, 0x59D1DD | 288, 26, 26, 198 | `LivingWorldAIBuilder` helpers | same | builder scoring helpers | RW 0x9A610D (575 B) |
| 0x59D2DA | 1137 | `LivingWorldAIBuilder::FillBuildingOrders` | same | AI building orders | RW 0x9A63BD (2033 B) |
| 0x59D8A5 | 103 | `Rva0059DD3F::rva0059D8A5` | Rva0059D8A5ThreatScore.cpp | region -> territory threat score | RW 0x9A6D13 / 0x9A6DE0 / 0x9A6F78 |
| 0x59D90C | 296 | `Rva0059D90C::rva0059D90C` | Rva0059D90CUnitPicker.cpp | AI unit picker over **8** type counts | same window |
| 0x59DB48 | 503 | `Rva0059DA34::rva0059DB48` | Rva0059DA34.cpp | AI counter-recruit: rank the eight types, pick buildable | RW 0x9A71F7 (643 B) |
| 0x59E752 | 246 | `LivingWorldScenario::OwnershipSet::spawnArmiesInRegionsForPlayer` | LivingWorldScenarioOwnershipSet.cpp | spawns an ownership set's armies | RW 0x9A7B70..0x9A7C99 |
| 0x5C98C0 | 54 | `Rva005C98C0::rva005C98C0` | GameLogic/LivingWorld/LivingWorldRegionLookup.cpp | region by id, kept when a flag is set | RW 0x9D8E9D (106 B) |

**Conclusion.** No BFME2-only WotR **feature** was found in code. The BFME2-only bytes are BFME2's versions of
functions RotWK rewrote: the AI (about 5.5 KB of the 9.4 KB), recruitment, retreats, the scenario object, battle
xfer and lookups. The census also lists `LivingWorldBuildingTemplate::findNugget` under another subsystem; I did
not inspect it.

## 5. RotWK-only WotR code our port cites (what a BFME2 edition must switch off or replace)

85 cited RotWK functions are unpaired (19,281 B). Address-order pairing (section 1 method step 2) gives a probable
BFME2 counterpart for about half. The rows below have no BFME2 counterpart, or exist only because of a RotWK
feature.

| Feature | RotWK code (port cites) | BFME2 edition |
|---|---|---|
| Treasury | `addTreasure` RW 0x6E1A40; income RW 0x6E291D; `IncreaseTreasury` effect RW 0x9D3F70 (vtable RW 0xC8C7DC); the player xfer's +0x318 (v6) | off: no treasury, no income, xfer v5 |
| Building cost | RW 0x6E0B4A, RW 0x7F31CD / 0x7F3120 | off: building is free |
| Recruit cost and refunds | RW 0x80F018, 0x8FF50E (treasury gate), 0x8FF6F8, 0x8FFC4A, 0x8108DC | replace: world CP gate (B2 0x4FA83E -> 0x2E1155); refunds as BFME2 (not read) |
| Territory bonus kinds | totals RW 0x6E19A1 (not decompiled, S-1327 inference); `FertileTerritoryBonus`, discounts, free builder, free inn units | off; BFME2's Attack/Defense/Experience/Army bonuses apply instead (RotWK code still parses them; whether RotWK applies them identically is not verified) |
| Hero move allowance 2 | RW 0x6B57B0 | replace: 0..1 for all armies |
| Victory types in the lobby | RW 0x932BF7, `LivingWorldVictoryType`, `RULE:VictoryType`, scenario keys `HistoricalScenario` / `DisabledFactions` / `UseMpRulesVictoryCondition` / `StartingRestriction` | off: the scenario's own conditions; rule row 5 is MapRevealMode |
| Siege / Support autoresolve types | type names RW 0xDADBE4 (9 + INVALID) | replace: 7 + INVALID (B2 names table) |
| AI treasury and ratio logic | RW 0x9A4205, 0x9A4C70, 0x9A53E6, 0x9A5E68, 0x9A5F6C, 0x9A610D, 0x9A63BD, 0x9A6D13, 0x9A6DE0, 0x9A6F78, 0x9A71F7; helpers RW 0x9050E3, 0x9051BC, 0x9059E0, 0x905EBE, 0x906263 (no BFME2 pair found) | replace with BFME2's AI (section 4) |
| HUD treasury and siege/support stats | RW 0x97E280; `strategichud.ini` rows | hide (data-driven) |

The census's two "RotWK-only" WotR entries are RW 0x6E267E (revival entry copy, 261 B) and RW 0x808F53 (1201 B,
cited from `Player.h` / `EconomySettings.h` / `VictoryConditions.cpp`). The second is not WotR-specific and was
not read.

## 6. Plan: a BFME2-edition WotR on top of `feature/wotr`

### Shared, edition-neutral (keep one implementation)

- The turn and phase machine, the dispatcher and `MSG_LW_*`, and `StrategicSession` (LAN lockstep, phase timer,
  battle vote, every-human rule).
- The region manager, plots, build queues, upgrades and `StrengthenArmy`. `ValidateArmyRegionEntry` has the same
  rules in both (3.1), but its code is edited and the A* is unpaired, so E16 checks it.
- The autoresolve rounds, attacks, leadership, `completeRound`, winner and `applyResults` (A/B). Target choice,
  the battle and unit ctors and three helpers differ (3.7, E7).
- The battle aftermath, victory evaluation (except the condition queries, E6), the score keeper, and the OpenBFME
  save block.
- `StrategicHUD.apt` and the map renderer (data-driven), plus all INI parsers. The BFME2 WotR data uses no key
  outside RotWK's grammar: the key-set diff found 0 BFME2-only keys and 28 RotWK-only keys.

### Work items

| # | Item | Size (was) | Notes |
|---|---|---|---|
| E1 | Edition switch for WotR | S (S) | `LivingWorldLogic` / `GameSetup` take the edition from `isBfme2Data` (BFME2MODE-0); hash it and save it; refuse a cross-edition load. Depends on BFME2MODE Phase 1 (named edition line). |
| E2 | BFME2 data load test | S (S) | Doctest under `BFME2_INSTALL`: 43 scenarios + tutorial, 38 regions, 6 territories, 6 player templates, 24 buildings, 7-type autoresolve tables, zero parse errors. |
| E3 | Economy off, CP recruit gate | M (M) | No treasury, income, building cost or recruit cost in BFME2. Port BFME2 `canQueueUnit` (B2 0x4FA83E) with the world-CP test (B2 0x2E1155, queued CP +0x298). Port BFME2's dequeue/disband handling (read B2 0x4FA9B1 / 0x4FAEAA / 0x40DDD6 first). Check the template ctor RW 0x6E1BB0 (C) and the player ctor RW 0x6E3C8B (edited). The army CP total RW 0x71A8F9 is unpaired; BFME2 callers use B2 0x318FBE. HUD rows follow the data. |
| E4 | Move allowance 1 for hero armies | S (S) | B2 0x2B27B5; also read `CanMoveArmyMember_internal` (D) and `armyMoveRequest` (B2 0x2B28B1). |
| E5 | Territory and region bonuses | M (M) | Apply BFME2's ConcurrentRegionBonus attack/defense/experience and region ArmyBonus as BFME2 does. RotWK's totals function RW 0x6E19A1 is an inference today (and RW 0x7F8C43 is unpaired). Read BFME2's bonus path (`LivingWorldRegionBonusLookup.cpp`, the region description B2 0x3F15D1). |
| E6 | Victory, scenario objects and rules | **M (S)** | Scenario conditions only; no victory type row; rule row 5 = MapRevealMode (its meaning in WotR is not verified); scenario list from data. New: port BFME2 `PlayerDefeatCondition::QueryRegionsAndNumbers` (B2 0x4FCDCF, D vs RW 0x901AA3). Find the BFME2 twins of the unpaired victory collectors RW 0x9026F2 / 0x902768. Use the BFME2 `LivingWorldCampaign` ctor (RW 0x93385D vs B2 0x52CF0F: same defaults, RotWK has 12 more bytes of members) and the BFME2 scenario object (B2 0x4FD0DA / 0x4FD1BC, section 4). |
| E7 | Autoresolve differences | **L (M)** | Unit types as an edition count, not a constant. lane/wotr2 hard-codes RotWK's 10 / 9 in `LivingWorldAI.h:170-172` (`weight[10]`, `want[10]`, `foreignTally[10]`), `:211` and `:213` (`out[10]`), `LivingWorldAIGarrisonMover.cpp:40` (`type < 10`), `LivingWorldAI.cpp:212` (`< 10 ? : 9`) and `LivingWorldLogic.h:79` (default 9). BFME2 uses 8 / 7 (B2 0x4F971E ChooseTargets, B2 0x418CC8 and siblings). Low risk because the INI is parsed by name, but loops that draw randoms and the AI ratio vector depend on it. Port BFME2 ChooseTargets (B2 0x4F971E), the battle ctor (B2 0x4FA3C9) and the unit ctor (B2 0x59B1EC). Find the twins of the unpaired helpers RW 0x8FEB3C / 0x8FAD90 / 0x9A3573 / 0x73C1E5 / 0x73C222 / 0x9EE028 / 0x9EE04A / 0x7F7C8D. Plus the previous scope: `SetUpAutoResolveBattle` (B2 0x2BC971, D), `PrepareRetreats` (B2 0x2B9FDB), `ConsumeGarrisonUnitsFromRTS` (B2 0x2BBD52, D). Data values (retreat rounds 25, schedule 50/100/150) come for free. |
| E8 | BFME2 strategic AI | L (L), larger | Port the BFME2 versions of the garrison mover, army mover, builder and troop producer (section 4, about 5.5 KB) from the decomp. New: `GatherWorldInformation` (RW 0x9095A7 vs B2 0x50366B, edited, 1761 vs 1723 B) and the AI building types (RW 0x908F18 vs B2 0x502FE9, edited, 1400 vs 1387 B). Also the edited / D helpers of 3.9 (RW 0x9004A6, 0x9006E5, 0x904F3A, 0x90526A, 0x905DA7, 0x90652D, 0x9A497B, 0x9A5E10, 0x9A6004, 0x9A6BB5) and the type-bound ones (RW 0x9058A1, 0x905C14, 0x9A3FB2, 0x9A6C32, 0x9A70E3). The AI template has no siege/support/treasury keys. Could split into E8a (information + building types) and E8b (movers, builder, producer). |
| E9 | Vote retreat loop | S (S) | Confirm the RotWK early exit (inside RW 0x6BEA94..0x6BEB9F) against BFME2's bytes at B2 0x2BDB4C, then make it edition-specific if real. |
| E10 | Shell and setup | M (M) | Re-read BFME2's strategic setup (`Skirmish.apt` / `SkirmishStrategic.apt` / `LanStrategic.apt` differ a lot), `StrategicStats.apt`, and which end screen BFME2's WotR uses (`ScoreScreen.apt` is BFME2-only). Also `AptQuitMenu::OnInitialized` (RW 0x921D5B, edited). |
| E11 | Save | S (S) | Edition in the OpenBFME block; drop the treasury fields in BFME2. Retail `.BfME2WotR` reading stays out of scope (player xfer v5 vs v6, `ArmySummaryEntry::xfer` RW 0x80EE5A edited; 3.10, 3.3). |
| E12 | LAN | S (S) | Same session code; the lobby rule table per edition (E6); cross-edition joins already refused by the data identity (BFME2MODE-0). |
| E13 | Tutorial (optional) | L (L) | BFME2 lists `WOTRTutorial`; the tutorial code (identical in both) is unported. Until it is ported, the BFME2 setup must not list the tutorial scenario (a named stop, not a silent skip). |
| E14 | Pins and tests | S (S) | BFME2 data pins: scenario 002 start, one autoresolve battle, one AI turn, one RTS battle spawn (E15); per-edition state hash. |
| **E15** | Armies into and out of RTS battles | **M (new)** | `spawnArmy` RW 0x6B7229 (edited, vs B2 0x2B65B7). `ArmySummary::getSpawnPositions` RW 0x80F754 (edited, 717 vs 655 B, B2 0x40CCC6). `ArmySummarySystem` load RW 0x8125FC (edited, B2 0x40F87E) and `ArmySummary::Load` RW 0x812119 (unpaired; BFME2 window 0x40F497). `Player::initFromDict` RW 0x6B07EF (fort start cash, edited). `Object::fillEntry` RW 0x6917BB (unpaired). Read each BFME2 version and make the differences edition rules. |
| **E16** | Region manager and path check | **S (new)** | `UpdateBattleMarkers` RW 0x60E985 (edited, vs B2 0x20EBA0). Confirm `ValidateArmyRegionEntry` (RW 0x60F00C vs B2 0x20EC99) and the unpaired A* RW 0x7F0C1A give the same path costs on BFME2's map (a test over all region pairs). Region object 0x1D4 vs 0x1B4 B (RW 0x6103E2) needs nothing unless a field is a rule. |
| **E17** | Hero revival records | **S (new)** | The revival record is 0xE8 B in RotWK and 0xD8 in BFME2 (RW 0x6E42DA, 0x78100E, 0x6B16EC). Find what the 16 bytes hold and whether a BFME2 rule depends on them; `CreateAHeroManager` ctor RW 0x61FB68 is edited (Create-a-Hero lane, cross-reference). |

Sizing now, without the tutorial: 9 S (E1, E2, E4, E9, E11, E12, E14, E16, E17), 5 M (E3, E5, E6, E10,
E15), 2 L (E7, E8). E13 adds one L.

Changes from the first sizing (7 S, 5 M, 1 L):

- E6 S -> M (defeat condition queries, campaign and scenario objects).
- E7 M -> L (hard-coded type count, ChooseTargets and ctors, eight unpaired helpers).
- E8 stays L but grows (world information, building types, ten more helpers).
- New: E15 (M), E16 (S), E17 (S).

**Cross-references (not WotR work).** These BFME2 edition rules belong to BFME2MODE-0
(`SkirmishAIEditionRules.h`).

- The tactical AI team pool is 40 in BFME2 (B2 0x3A2FD4, `TeamFactory_initTeamForTacticalAI.cpp:61-68`) and 80 in
  RotWK (RW 0x7A6295). Our `AITacticalAI.cpp:989-1010` caps neither edition (brief TACPOOL-1, finding F-2 of
  `logs/decomp-discoveries-20261029.md`).
- The upgrade mask is 32 vs 36 words (RW 0x66F603 / 0x73BE09, `Common/Upgrade.h:54`). WotR's autoresolve armor and
  weapon pick use it, but it is engine-wide.

### Open questions and unverified points

- E3: BFME2's queued-CP bookkeeping (+0x298) and its refund paths were not read.
- `SetUpAutoResolveBattle`: what RotWK's extra 216 bytes do (free inn units / auto fort is a guess).
- The retreat loop difference in the vote (E9).
- Whether RotWK applies `ArmyBonus` / `AttackBonus` / `DefenseBonus` / `ExperienceBonus` exactly as BFME2 does. The
  port's bonus totals are an S-1327 inference.
- The probable pairings of section 4 (address order), in particular `LivingWorldBattle::DoXfer` B2 0x3F7023 and its
  callback, which have no clear RotWK twin.
- The BFME2 setup and score screens (archive sizes only).
- The 96 census functions added on 2026-10-29: most rows say "not verified". Only the constant diffs (type bounds,
  record sizes, offsets) and the ValidateArmyRegionEntry and LivingWorldCampaign ctor reads are checked.
- Whether the unpaired RotWK functions of 3.7 / 3.9 have BFME2 twins (address-order windows given where found).
- What `MapRevealMode` does in a BFME2 WotR lobby.
- The retail network save (mode 6) in BFME2 was not compared in code.
