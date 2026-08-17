extends SceneTree
## Spellbook cast matrix: every faction spellbook-runtime power is configured,
## purchased (tree order), and cast through the live sim APIs. Unsupported
## effects fail closed with the sim's locked_reason (named gap), not a silent pass.
## Env: OPENBFME_CONTENT, OPENBFME_GOAL_SPELLBOOK_OUT

const FACTIONS: Array[String] = [
	"men", "elves", "dwarves", "isengard", "mordor", "wild", "angmar",
]
const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const SUMMON_SCOPE := {
	"elves": ["SpellBookTomBombadil", "SpellBookEagleAllies", "SpellBookEntAllies"],
	"dwarves": ["SpellBookHobbitAllies"],
	"isengard": ["SpellBookCrebain", "SpellBookWatcherAlly", "SpellBookDragonStrike"],
	"mordor": ["SpellBookAwakenWyrm"],
	"wild": ["SpellBookCaveBats", "SpellBookAwakenWyrm", "SpellBookWatcherAlly"],
	"angmar": ["SpellBookSummonOrcs", "SpellBookSummonGiants", "SpellBookSummonWights"],
}
# Literal RotWK summon expectations, pinned against the PURE retail tree
# .private/retail-work/editions/rotwk/cache/effective-assets — the tree the
# published packs are compiled from. The sibling layered-effective-assets tree
# is NOT the oracle: it rewrites magnitudes in place and pushes retail's values
# behind `;,;` / `;;,;;` markers, so a citation taken from it reads as
# authoritative while naming the override. Rows outside Angmar remain pending
# until their faction packs are rebuilt from the same oracle; keeping the
# literals here prevents a future runner from deriving its expectations from
# the effect dictionary it is meant to test.
# (The LAYERED_ / layered_effect_ names below are historical and kept so the
# emitted check names stay stable across rounds; they mean "literal-pinned".)
const LAYERED_SUMMON_EXPECTATIONS := {
	# RE-PINNED 2026-08-04 (round 13): lifetime 45000 -> 60000. PURE RETAIL
	# object/goodfaction/generic/tombombadil.ini:535-538 authors
	# `Behavior = LifetimeUpdate ModuleTag_LifetimeUpdate / MinLifetime = 60000
	# / MaxLifetime = 60000 / DeathType = FADED` on TomBombadil_Summoned.
	# 45000 was the layered tree's override.
	"elves/SpellBookTomBombadil": {"status": "assert", "objects": {"TomBombadil_Summoned": 1}, "lifetimes_ms": {"TomBombadil_Summoned": 60000}, "death_types": {"TomBombadil_Summoned": "FADED"}, "hatch_delay_ms": 2000, "cost": 10, "cooldown_tier": 2},
	# RE-PINNED 2026-08-04 (round 13): lifetime 75000 -> 120000. PURE RETAIL
	# object/goodfaction/units/elven/gwaihir.ini:447-450 authors
	# MinLifetime/MaxLifetime 120000, DeathType = FADED on GondorGwaihir.
	# 75000 was the layered tree's override.
	"elves/SpellBookEagleAllies": {"status": "assert", "objects": {"GondorGwaihir_Summoned": 2}, "lifetimes_ms": {"GondorGwaihir_Summoned": 120000}, "death_types": {"GondorGwaihir_Summoned": "FADED"}, "hatch_delay_ms": 0, "cost": 15, "cooldown_tier": 3},
	# RE-PINNED 2026-08-04 (round 13): the whole row. PURE RETAIL
	# objectcreationlist.ini:7991-8003 `ObjectCreationList OCL_SpawnEnts` has a
	# SINGLE CreateObject:
	#     ObjectNames = RohanEntFir_Summoned RohanEntBirch_Summoned   (:7993)
	#     Count       = 3                                             (:7994)
	# so the pool is TWO names and the engine picks 3 from it — there is no
	# RohanEntOak_Summoned and no separate RohanEntAshMelee_Summoned block of
	# three. The old row (3-name pool, 1 pick, plus 3 Ash melee Ents) described
	# the layered tree's rewrite of this OCL.
	# Lifetimes: object/goodfaction/units/ents/entsinfantry.ini:476-479
	# (RohanEntFir_Summoned) and :627-630 (RohanEntBirch_Summoned) both author
	# MinLifetime/MaxLifetime 120000, DeathType = FADED. 75000 was layered.
	"elves/SpellBookEntAllies": {"status": "assert", "object_pool": ["RohanEntFir_Summoned", "RohanEntBirch_Summoned"], "pool_pick_count": 3, "objects": {}, "lifetimes_ms": {"RohanEntFir_Summoned": 120000, "RohanEntBirch_Summoned": 120000}, "death_types": {"RohanEntFir_Summoned": "FADED", "RohanEntBirch_Summoned": "FADED"}, "hatch_delay_ms": 0, "cost": 15, "cooldown_tier": 3},
	# RE-PINNED 2026-08-04 (retail rebase): lifetimes 90000 -> 120000. PURE
	# RETAIL authors `MinLifetime = 120000 / MaxLifetime = 120000` on
	# RohanHobbitFriendsHorde_Summoned; the layered tree reads
	# `90000 ;;,;; 120000` - retail's value is to the right of the marker.
	"dwarves/SpellBookHobbitAllies": {"status": "assert", "objects": {"RohanHobbitFriendsHorde_Summoned": 3, "RohanSam_Summoned": 1, "RohanFrodo_Summoned": 1, "RohanMerry_Summoned": 1, "RohanPippen_Summoned": 1}, "lifetimes_ms": {"RohanHobbitFriendsHorde_Summoned": 120000, "RohanSam_Summoned": 120000, "RohanFrodo_Summoned": 120000, "RohanMerry_Summoned": 120000, "RohanPippen_Summoned": 120000}, "death_types": {"RohanHobbitFriendsHorde_Summoned": "FADED", "RohanSam_Summoned": "FADED", "RohanFrodo_Summoned": "FADED", "RohanMerry_Summoned": "FADED", "RohanPippen_Summoned": "FADED"}, "hatch_delay_ms": 2000, "cost": 10, "cooldown_tier": 2},
	"isengard/SpellBookCrebain": {"status": "assert", "objects": {"Crebain": 1}, "lifetimes_ms": {"Crebain": 60000}, "death_types": {"Crebain": ""}, "hatch_delay_ms": 0, "cost": 5, "cooldown_tier": 1},
	"isengard/SpellBookWatcherAlly": {"status": "assert", "objects": {"WatcherHead": 1, "WatcherHittingArm": 8}, "lifetimes_ms": {"WatcherHead": 30000, "WatcherHittingArm": 30000}, "death_types": {"WatcherHead": "FADED", "WatcherHittingArm": "FADED"}, "hatch_delay_ms": 2000, "cost": 15, "cooldown_tier": 3},
	# DEMOTED to pending-rebuild 2026-08-04 (retail rebase). Against the
	# pure-retail pack this power now resolves to NO terminal objects
	# (`objects={}`), where the layered pack resolved one dragon.
	#
	# NOT re-pinned to the observed empty result, because an empty summon is
	# not a plausible retail behaviour and pinning it would freeze a gap as if
	# it were a fact. The likely cause is the spawn route: retail spawns this
	# dragon through a StrafeAreaUpdate, not a plain OCL summon chain
	# (specialpower.ini:1657-1660, `RadiusCursorRadius = 180.0 ; Match the
	# radius in StrafeAreaUpdate`), and the resolver only walks OCL chains.
	# The power IS authored and IS bound in retail
	# (commandset.ini:6487 `IsengardSpellBookCommandSet` slot 12), and the
	# layered tree carries 82 references to SpellBookDragonStrikeDragon against
	# pure retail's 16 - so the fan patch reworked this power substantially.
	#
	# Left as a named, counted exclusion until the StrafeAreaUpdate spawn route
	# is resolved. EXPECTED_EXCLUDED_PENDING below is raised to 1 to match.
	"isengard/SpellBookDragonStrike": {"status": "pending-rebuild", "objects": {"SpellBookDragonStrikeDragon": 1}, "lifetimes_ms": {"SpellBookDragonStrikeDragon": 30000}, "death_types": {"SpellBookDragonStrikeDragon": "FADED"}, "hatch_delay_ms": 0, "cost": 25, "cooldown_tier": 4},
	"mordor/SpellBookAwakenWyrm": {"status": "assert", "objects": {"Wyrm": 1}, "lifetimes_ms": {"Wyrm": 60000}, "death_types": {"Wyrm": "FADED"}, "hatch_delay_ms": 1000, "cost": 15, "cooldown_tier": 3},
	# WildCaveBats is `ObjectReskin WildCaveBats Crebain`
	# (data/ini/object/neutral/neutralunits.ini:2549) and inherits Crebain's
	# LifetimeUpdate verbatim (:2483-2486): MinLifetime/MaxLifetime 60000 and NO
	# DeathType row. The earlier "FADED" pin was invented, not read: the
	# already-asserted isengard/SpellBookCrebain row above pins the same module
	# as "". Promoted after the 2026-08-04 wild rebuild reproduced the oracle.
	"wild/SpellBookCaveBats": {"status": "assert", "objects": {"WildCaveBats": 1}, "lifetimes_ms": {"WildCaveBats": 60000}, "death_types": {"WildCaveBats": ""}, "hatch_delay_ms": 0, "cost": 5, "cooldown_tier": 1},
	"wild/SpellBookAwakenWyrm": {"status": "assert", "objects": {"Wyrm": 1}, "lifetimes_ms": {"Wyrm": 60000}, "death_types": {"Wyrm": "FADED"}, "hatch_delay_ms": 1000, "cost": 15, "cooldown_tier": 3},
	# Wild and Isengard invoke the ONE SpecialPower SpellBookWatcherAlly
	# (data/ini/commandbutton.ini:11059-11061; data/ini/commandset.ini:7308 and
	# :7808 bind Command_SpellBookWatcherAlly into both faction command sets),
	# so wild must resolve to the same terminal objects the isengard row above
	# already asserts. The earlier pin froze the pre-hatch WatcherEgg midpoint;
	# the 2026-08-04 wild rebuild resolves the egg chain like isengard does.
	"wild/SpellBookWatcherAlly": {"status": "assert", "objects": {"WatcherHead": 1, "WatcherHittingArm": 8}, "lifetimes_ms": {"WatcherHead": 30000, "WatcherHittingArm": 30000}, "death_types": {"WatcherHead": "FADED", "WatcherHittingArm": "FADED"}, "hatch_delay_ms": 2000, "cost": 15, "cooldown_tier": 3},
	# RE-PINNED 2026-08-04 (retail rebase): 4 -> 3. PURE RETAIL 2.01 authors
	# `objectcreationlist.ini:7736-7739 OCL_SpawnAngmarOrcWarriors ... Count = 3`.
	# The 4 came from the fan-patched (Unofficial 2.02) tree, whose same block
	# reads `Count = 4 ;,;3` at :8229-8232 - the `;,;` is a patch merge marker,
	# and the value to its right is retail's.
	"angmar/SpellBookSummonOrcs": {"status": "assert", "objects": {"AngmarOrcWarriors_Summoned": 3}, "lifetimes_ms": {"AngmarOrcWarriors_Summoned": 90000}, "death_types": {"AngmarOrcWarriors_Summoned": "FADED"}, "hatch_delay_ms": 2000, "cost": 10, "cooldown_tier": 2},
	# RE-PINNED 2026-08-04: lifetime 60000 -> 90000. PURE RETAIL authors
	# `MinLifetime/MaxLifetime = CREATE_A_HERO_REINFORCEMENT_LIFETIME`
	# (= 90000, createaherogamedata.inc:397). The layered tree overrides it to
	# `60000 ;,; 75000 ;;,;; CREATE_A_HERO_REINFORCEMENT_LIFETIME`.
	"angmar/SpellBookSummonGiants": {"status": "assert", "objects": {"WildMountainGiant_Summoned": 2}, "lifetimes_ms": {"WildMountainGiant_Summoned": 90000}, "death_types": {"WildMountainGiant_Summoned": "FADED"}, "hatch_delay_ms": 2000, "cost": 15, "cooldown_tier": 3},
	# RE-PINNED 2026-08-04, four literals, each read from PURE RETAIL 2.01:
	#   count 4 -> 5   objectcreationlist.ini:7619-7620 `Count = 5 ; RotWK
	#                  originally 4` - EA's own 2.01 patch raised it and said so;
	#                  the layered tree reverts it (`Count = 4 ;;,;; 5`).
	#   lifetime 60000 -> 90000   CREATE_A_HERO_REINFORCEMENT_LIFETIME
	#                  (createaherogamedata.inc:397); layered overrides to 60000.
	#   cost 10 -> 15  science.ini:441 `SciencePurchasePointCostMP = 15`
	#                  (EVIL_RANK_3_COST); layered authors EVIL_RANK_2_COST.
	#   tier 2 -> 3    specialpower.ini:1515 `ReloadTime =
	#                  SPELL_RECHARGE_TIME_TIER_3`; layered reads
	#                  `SPELL_RECHARGE_TIME_TIER_2 ;;,;; ..._TIER_3`.
	"angmar/SpellBookSummonWights": {"status": "assert", "objects": {"BarrowWight_Summoned": 5}, "lifetimes_ms": {"BarrowWight_Summoned": 90000}, "death_types": {"BarrowWight_Summoned": "FADED"}, "hatch_delay_ms": 2000, "cost": 15, "cooldown_tier": 3},
}
const COOLDOWN_MS_BY_TIER := {1: 180000, 2: 360000, 3: 540000, 4: 720000, 6: 120000}
# ---------------------------------------------------------------------------
# TERRAIN-TAINT and REVEAL families. Same discipline as the summon table above:
# every number is a literal read off the PURE RETAIL 2.01 oracle at
# .private/retail-work/editions/rotwk/cache/effective-assets/data/ini, so the
# runner can never derive its expectations from the effect dictionary it tests.
# Shared defines (data/ini/gamedata.ini):
#   :185  SPELLBOOK_TAINT_RADIUS        175
#   :186  SPELLBOOK_TAINT_TIME          300000    ; ms = 5 minutes
#   :3595 FROZEN_LAND_EFFECT_DURATION   300000
#   :8131 SPELL_RECHARGE_TIME_TIER_1    180000
#   :8132 SPELL_RECHARGE_TIME_TIER_2    360000
#   :8136 SPELL_RECHARGE_TIME_TIER_6    120000
# Shared modifier leaves (data/ini/attributemodifier.ini):
#   :72-80    GenericBuff            ARMOR 50%, DAMAGE_MULT 150%, Duration 3000
#   :123-132  GenericHeroLeadership  EXPERIENCE 200%, ARMOR 50%, DAMAGE_MULT 150%, Duration 3000
#   :1139-1146 PalantirVision        Category SPELL, Duration 30000, every
#                                    Modifier row COMMENTED OUT upstream — the
#                                    aura is authored as a pure marker, so the
#                                    expectation below pins ZERO modifiers
#                                    rather than inventing a bonus.
# Costs are SciencePurchasePointCostMP (the skirmish price the sim charges).
const TERRAIN_REVEAL_EXPECTATIONS := {
	# --- TERRAIN TAINT --------------------------------------------------------
	# specialpower.ini:1060-1071 SpellBookTaint: RadiusCursorRadius
	# SPELLBOOK_TAINT_RADIUS, ReloadTime SPELL_RECHARGE_TIME_TIER_1.
	# science.ini:296-301 SCIENCE_Taint SciencePurchasePointCostMP = 5.
	# object/system/system.ini:43-53 TaintSpecialPower ModuleTag_Taint,
	#   TaintObject = TaintLand, TaintRadius = SPELLBOOK_TAINT_RADIUS.
	# object/evilfaction/structures/taintland.ini:31-38 AttributeModifierAuraUpdate
	#   BonusName GenericBuff, RefreshDelay 2000, Range SPELLBOOK_TAINT_RADIUS,
	#   RequiredConditions TAINT; :40-43 DeletionUpdate SPELLBOOK_TAINT_TIME.
	"mordor/SpellBookTaint": {
		"kind": "grove_aura", "cost": 5, "cooldown_tier": 1, "cursor_radius": 175.0,
		"terrain_object_id": "TaintLand", "terrain_condition": "TAINT",
		"modifier": "GenericBuff", "range_source": 175.0, "lifetime_ms": 300000,
		"buff_duration_ms": 3000, "armor_mult": 0.5, "damage_mult": 1.5,
	},
	# The one SpecialPower SpellBookTaint is bound into both evil command sets
	# (science.ini:297 PrerequisiteSciences ... OR SCIENCE_MORDOR OR SCIENCE_WILD),
	# so Wild must resolve to identical literals.
	"wild/SpellBookTaint": {
		"kind": "grove_aura", "cost": 5, "cooldown_tier": 1, "cursor_radius": 175.0,
		"terrain_object_id": "TaintLand", "terrain_condition": "TAINT",
		"modifier": "GenericBuff", "range_source": 175.0, "lifetime_ms": 300000,
		"buff_duration_ms": 3000, "armor_mult": 0.5, "damage_mult": 1.5,
	},
	# specialpower.ini:1330-1339 SpellBookIsengardTaint: same radius and the same
	# TaintLand payload, but ReloadTime SPELL_RECHARGE_TIME_TIER_2 and
	# science.ini:361-366 SCIENCE_IsengardTaint SciencePurchasePointCostMP = 10.
	# (science.ini:359-360 says in-line why Mordor and Isengard need two prices.)
	"isengard/SpellBookIsengardTaint": {
		"kind": "grove_aura", "cost": 10, "cooldown_tier": 2, "cursor_radius": 175.0,
		"terrain_object_id": "TaintLand", "terrain_condition": "TAINT",
		"modifier": "GenericBuff", "range_source": 175.0, "lifetime_ms": 300000,
		"buff_duration_ms": 3000, "armor_mult": 0.5, "damage_mult": 1.5,
	},
	# specialpower.ini:1183-1194 SpellBookElvenWood: RadiusCursorRadius
	# SPELLBOOK_TAINT_RADIUS, ReloadTime SPELL_RECHARGE_TIME_TIER_2.
	# science.ini:137-142 SCIENCE_ElvenWood SciencePurchasePointCostMP = 10.
	# object/system/system.ini:828-837 ElvenWoodSpecialPower, ElvenGroveObject =
	#   ElvenGrove, ElvenWoodRadius = SPELLBOOK_TAINT_RADIUS.
	# object/goodfaction/structures/elven/grove.ini:29-37 aura GenericBuff,
	#   RefreshDelay 2000, Range SPELLBOOK_TAINT_RADIUS, RequiredConditions
	#   ELVEN_WOOD; :39-42 DeletionUpdate SPELLBOOK_TAINT_TIME.
	"elves/SpellBookElvenWood": {
		"kind": "grove_aura", "cost": 10, "cooldown_tier": 2, "cursor_radius": 175.0,
		"terrain_object_id": "ElvenGrove", "terrain_condition": "ELVEN_WOOD",
		"modifier": "GenericBuff", "range_source": 175.0, "lifetime_ms": 300000,
		"buff_duration_ms": 3000, "armor_mult": 0.5, "damage_mult": 1.5,
	},
	# Men's Elven Wood is a DIFFERENT power from the Elves' one above and its
	# oracle is a different tree: the men spellbook is compiled from BFME2 1.06
	# (.private/retail-work/cache/effective-assets), not from RotWK, so every
	# literal here is read there and none of them may be copied from the Elves
	# row. 1.06 rebalanced this power and the `; ;` markers in that tree record
	# it (old value commented to the right of the live one):
	#   specialpower.ini:962-973 SpellBookElvenWoodMP: RadiusCursorRadius
	#     SPELLBOOK_TAINT_RADIUS, ReloadTime authored INLINE as 270000 (not a
	#     SPELL_RECHARGE_TIME_TIER_n define), hence "cooldown_ms" not
	#     "cooldown_tier".
	#   gamedata.ini:178 `#define SPELLBOOK_TAINT_RADIUS 250 ; ; 175` -> 250.
	#   gamedata.ini:179 SPELLBOOK_TAINT_TIME 300000.
	#   science.ini:133-138 SCIENCE_ElvenWoodMP SciencePurchasePointCostMP = 5.
	#   object/system/system.ini:1000-1010 ChildObject MenSpellBook,
	#     ElvenWoodSpecialPower, ElvenGroveObject = ElvenGrove.
	#   object/goodfaction/structures/elven/grove.ini:31
	#     `BonusName = GenericArmorLeadership ; ;GenericBuff` -> the live bonus
	#     is GenericArmorLeadership.
	#   attributemodifier.ini:159-166 ModifierList GenericArmorLeadership:
	#     Category BUFF, `Modifier = ARMOR 50%` and NOTHING ELSE, Duration 3000.
	# ARMOR-ONLY IS THE AUTHORED CONTRACT. damage_mult 1.0 below is the neutral
	# value for an ABSENT row, not a defaulted stand-in for one that failed to
	# parse — round 16 conflated those and locked this power.
	# `authored_rows` IS the pin that makes the sentence above checkable. Without
	# it, "damage_mult 1.0 is the authored-absent neutral" is a claim in a
	# comment: a future conversion that DID read a DAMAGE_MULT row and happened
	# to land on 1.0, or one that defaulted after failing to parse, would both
	# still match the numbers. ARMOR true / DAMAGE_MULT false says the leaf
	# authored exactly one row, which is what grove.ini:31 +
	# attributemodifier.ini:159-166 actually say.
	"men/SpellBookElvenWoodMP": {
		"kind": "grove_aura", "cost": 5, "cooldown_ms": 270000, "cursor_radius": 250.0,
		"terrain_object_id": "ElvenGrove", "terrain_condition": "ELVEN_WOOD",
		"modifier": "GenericArmorLeadership", "range_source": 250.0, "lifetime_ms": 300000,
		"buff_duration_ms": 3000, "armor_mult": 0.5, "damage_mult": 1.0,
		"authored_rows": {"ARMOR": true, "DAMAGE_MULT": false},
		"unsupported_modifier_rows": 0,
	},
	# --- REVEAL / FIELD PINGS -------------------------------------------------
	# specialpower.ini:1231-1243 SpellBookFrozenLand: RadiusCursorRadius
	# SPELLBOOK_TAINT_RADIUS, ReloadTime SPELL_RECHARGE_TIME_TIER_1.
	# science.ini:345-350 SCIENCE_FrozenLand SciencePurchasePointCostMP = 10.
	# object/system/system.ini:172-181 OCLSpecialPower ModuleTag_FrozenLand,
	#   OCL = OCL_SpecialPowerFrozenLand ("try three :)" — the two earlier
	#   TaintSpecialPower/SpecialPowerModule attempts at :146-170 are commented
	#   out upstream, so the OCL route is the live one).
	# objectcreationlist.ini:9381-9387 creates FrozenLandPing, Count = 1.
	# object/system/system.ini:2043-2073 FrozenLandPing: VisionRange 0.0 (NO
	#   reveal — this ping is a pure aura), LifetimeUpdate
	#   FROZEN_LAND_EFFECT_DURATION, AttributeModifierAuraUpdate BonusName
	#   GenericHeroLeadership, RefreshDelay 2000, Range SPELLBOOK_TAINT_RADIUS,
	#   TargetEnemy No.
	"angmar/SpellBookFrozenLand": {
		"kind": "field_ping", "cost": 10, "cooldown_tier": 1, "cursor_radius": 175.0,
		"object_id": "FrozenLandPing", "lifetime_ms": 300000,
		"reveal_radius_source": 0.0, "unconverted_behaviors": [],
		"auras": [{
			"id": "GenericHeroLeadership", "category": "LEADERSHIP",
			"range_source": 175.0, "refresh_ms": 2000, "duration_ms": 3000,
			"modifiers": {"EXPERIENCE": 2.0, "ARMOR": 0.5, "DAMAGE_MULT": 1.5},
			"target_enemy": false,
		}],
	},
	# specialpower.ini:1019-1026 SpellBookFarsight: RadiusCursorRadius 300.0,
	#   ReloadTime SPELL_RECHARGE_TIME_TIER_6 (note: NOT tier 1 — Farsight is the
	#   only power in this batch on the 120s tier).
	# science.ini:101-106 SCIENCE_Farsight SciencePurchasePointCostMP = 5.
	# object/system/system.ini:798-806 OCLSpecialPower ModuleTag_Farsight,
	#   OCL = OCL_SpecialPowerFarSeeing.
	# objectcreationlist.ini:8705-8711 creates FarSeeingPing, Count = 1.
	# object/system/system.ini:1951-1953 ChildObject FarSeeingPing
	#   PalantirVisionBase, VisionRange = 250 — the reveal radius, and this ping
	#   authors NO aura at all. Lifetime comes from the parent at :1905-1935
	#   (LifetimeUpdate 90000), which also carries a StealthDetectorUpdate. The
	#   2026-08-05 cook converts that module into leaf data (no residual marker),
	#   so unconverted_behaviors is now empty; runtime stealth-unmasking is
	#   tracked in the parity graph, not by this pin.
	"elves/SpellBookFarsight": {
		"kind": "field_ping", "cost": 5, "cooldown_tier": 6, "cursor_radius": 300.0,
		"object_id": "FarSeeingPing", "lifetime_ms": 90000,
		"reveal_radius_source": 250.0,
		"unconverted_behaviors": [], "auras": [],
	},
	# specialpower.ini:940-947 SpellBookEnshroudingMist: radius 150 and tier-2
	# recharge; science.ini:117-121 costs 10. OCL_SpecialPowerEnshroudingMist
	# creates EnshroudingMistPing. system.ini:1997-2025 authors a 60000ms
	# lifetime, enemy GenericDebuff aura, and the CAMOUFLAGE broadcast below.
	"elves/SpellBookEnshroudingMist": {
		"kind": "field_ping", "cost": 10, "cooldown_tier": 2, "cursor_radius": 150.0,
		"object_id": "EnshroudingMistPing", "lifetime_ms": 60000,
		"reveal_radius_source": 0.0, "unconverted_behaviors": [],
		"auras": [{
			"id": "GenericDebuff", "category": "DEBUFF",
			"range_source": 200.0, "refresh_ms": 2000, "duration_ms": 3000,
			"modifiers": {"ARMOR": -0.25, "DAMAGE_MULT": 0.75},
			"target_enemy": true,
		}],
		"invisibility_updates": [{
			"enabled": true, "update_ms": 1000, "broadcast": true,
			"broadcast_range_source": 150.0, "detection_range_source": 100.0,
			"invisibility_type": "CAMOUFLAGE",
			"broadcast_filter": "ANY +HORDE +HERO +DOZER +RohanEntFir_Summoned +RohanEntBirch_Summoned +RohanEntFir +RohanEntBirch +RohanEntAsh +MordorMountainTroll +MordorDrummerTroll +MordorAttackTroll +WildMountainGiant +GoblinCaveTroll +CaveTroll_Slaved +MordorCaveTroll_Summoned +MordorAttackTroll_Summoned +WildMountainGiant_Summoned -Drogoth -GondorGwaihir_Summoned -GondorGwaihir -MordorFellBeast -MordorWitchKingOnFellBeast -ElvenFortressEagle -SpellBookDragonStrikeDragon -KhamulFellBeast -MorgomirFellBeast ALLIES",
			"source_ini": "data/ini/object/system/system.ini", "line": 2018,
		}],
	},
	# specialpower.ini:1103-1110 SpellBookPalantirVision: RadiusCursorRadius
	#   300.0, ReloadTime SPELL_RECHARGE_TIME_TIER_1.
	# science.ini:324-329 SCIENCE_PalantirVision SciencePurchasePointCostMP = 5.
	# object/system/system.ini:118-126 OCLSpecialPower ModuleTag_PalantirVision,
	#   OCL = SpecialPowerPalantirVision.
	# objectcreationlist.ini:8673-8679 creates PalantirVisionPing, Count = 1.
	# object/system/system.ini:1937-1949 PalantirVisionPing: VisionRange 300.0,
	#   AttributeModifierAuraUpdate BonusName PalantirVision, RefreshDelay 1000,
	#   Range 200, ObjectFilter ANY +ORC +URUK +CAVALRY -STRUCTURE
	#   -BASE_FOUNDATION -HERO; lifetime 90000 from PalantirVisionBase.
	# The aura's modifier list is authored with every stat row commented out
	# (attributemodifier.ini:1139-1146), so ZERO modifiers is the retail fact.
	# StealthDetectorUpdate converts to leaf data as of the 2026-08-05 cook (see
	# the Farsight note above), so no residual marker remains on this ping.
	"isengard/SpellBookPalantirVision": {
		"kind": "field_ping", "cost": 5, "cooldown_tier": 1, "cursor_radius": 300.0,
		"object_id": "PalantirVisionPing", "lifetime_ms": 90000,
		"reveal_radius_source": 300.0,
		"unconverted_behaviors": [],
		"auras": [{
			"id": "PalantirVision", "category": "SPELL",
			"range_source": 200.0, "refresh_ms": 1000, "duration_ms": 30000,
			"modifiers": {}, "target_enemy": null,
		}],
	},
}
# Eye of Sauron already resolves through the summon lane (it spawns the live
# EyeOfSauron object, not a ping), so it is pinned as cost/cooldown only.
# science.ini:317-322 SCIENCE_EyeofSauron SciencePurchasePointCostMP = 5;
# specialpower.ini:1073-1080 ReloadTime SPELL_RECHARGE_TIME_TIER_1;
# object/system/system.ini:77-85 OCLSpecialPower OCL =
# SUPERWEAPON_SpawnEyeOfSauron; objectcreationlist.ini:4617-4624 creates
# EyeOfSauron Count = 1; object/evilfaction/units/mordor/eyeofsauron.ini:67-70
# LifetimeUpdate 60000.
const REVEAL_COST_ONLY_EXPECTATIONS := {
	"mordor/SpellBookEyeofSauron": {"cost": 5, "cooldown_tier": 1, "cursor_radius": 75.0},
}
# Named fail-closed terrain/reveal rows. Empty after the typed Enshrouding Mist
# camouflage broadcast gained an authored runtime consumer and live coverage.
const TERRAIN_REVEAL_BLOCKED := {}
## Zero as of 2026-08-04: all seven faction packs are now cooked from the same
## PURE RETAIL 2.01 oracle (editions/rotwk/cache/effective-assets), so every
## summon row is asserted against its literal. The earlier wording said "layered
## oracle"; that tree is quarantined - it carries `__patch202.big` and 530
## merge-marked INI files. A future rebuild that needs an exclusion must name it
## here with its reason.
const UNTAMED_ALLEGIANCE_LAIR_TYPES: Array = [
	"BarrowWightLair", "CaveTrollLair", "CaveTrollLairSnow", "FireDrakeLair",
	"HillTrollLair", "HillTrollLairSnow", "MoriarGoblinLair",
	"MoriarGoblinLairSnow", "SnowTrollLair", "SnowTrollLairSnow", "SpiderLair",
	"WargLair",
]
const UNTAMED_ALLEGIANCE_FILTER := "ANY +SnowTrollLair +HillTrollLair +SnowTrollLairSnow +HillTrollLairSnow +CaveTrollLair +MoriarGoblinLair +WargLair +SpiderLair +BarrowWightLair +FireDrakeLair +MoriarGoblinLairSnow +CaveTrollLairSnow +NeutralWarg +BarrowWight_Slaved +FireDrake_Slaved +MordorGoblinSwordsman_Slaved +MordorGoblinArcher_Slaved +MinorSpider_Slaved +CaveTroll_Slaved ENEMIES"
const UNTAMED_ALLEGIANCE_ROW := {
	"kind": "creep_allegiance", "cost": 10, "cooldown_ms": 360000, "cursor_radius": 60.0,
	"range_source": 60.0, "target_enemy": true,
	"lair_types": UNTAMED_ALLEGIANCE_LAIR_TYPES,
	"filter": UNTAMED_ALLEGIANCE_FILTER,
}
# ---------------------------------------------------------------------------
# WEATHER / GLOBAL and LAIR-CONVERSION families (round 15, batch 2). Same
# discipline as the tables above: every number is a literal read off the PURE
# RETAIL 2.01 oracle at
# .private/retail-work/editions/rotwk/cache/effective-assets/data/ini, so the
# runner can never derive its expectations from the effect dictionary it tests.
#
# Shared defines (data/ini/gamedata.ini):
#   :15   SPELL_DARKNESS_DURATION       180000
#   :16   SPELL_FREEZINGRAIN_DURATION   150000   ; "RotWK originally 180000"
#   :89   CREEP_OBJECTFILTER            the 19-term creep list quoted below
#   :8132 SPELL_RECHARGE_TIME_TIER_2    360000
#
# NOTE ON THE COOLDOWNS. Darkness and Freezing Rain do NOT use a tier define:
# specialpower.ini:1446-1454 and :1484-1490 both author a LITERAL
# `ReloadTime = 360000` with the comment "RotWK originally
# SPELL_RECHARGE_TIME_TIER_3". 360000 happens to equal tier 2's value, so these
# rows pin `cooldown_ms` directly rather than a tier, and a future tier-table
# edit cannot silently re-interpret them.
const WEATHER_ALLEGIANCE_EXPECTATIONS := {
	# specialpower.ini:1446-1454 SpellBookDarkness: ReloadTime 360000 literal, no
	#   RadiusCursorRadius (the power is global - it has no cast point).
	# science.ini:452-457 SCIENCE_Darkness SciencePurchasePointCostMP = 15.
	# object/system/system.ini:322-332 DarknessSpecialPower ModuleTag_Darkness:
	#   AttributeModifier = SpellBookDarkness,
	#   AttributeModifierWeatherBased = Yes, WeatherDuration =
	#   SPELL_DARKNESS_DURATION, ChangeWeather = CLOUDY, and the ALLIES-scoped
	#   affects filter quoted below. There is NO AttributeModifierRange: the
	#   effect covers the whole map for the weather window.
	# attributemodifier.ini:1053-1067 ModifierList SpellBookDarkness:
	#   Category SPELL, DAMAGE_MULT 150%, ARMOR 50%, Duration = 0 (INFINITE -
	#   the weather window is what bounds it).
	"mordor/SpellBookDarkness": {
		"kind": "weather_modifier", "cost": 15, "cooldown_ms": 360000, "cursor_radius": 0.0,
		"modifier_id": "SpellBookDarkness", "category": "SPELL",
		"modifiers": {"DAMAGE_MULT": 1.5, "ARMOR": 0.5},
		"duration_ms": 180000, "weather": "CLOUDY",
		"affects": "ANY +INFANTRY +CAVALRY +MONSTER -HERO -HORDE -MordorBlackRider -DwarvenZerker -NoldorWarrior -GondorKnightsofDol -WildBabyDrake -IsengardFanatic ALLIES",
	},
	"wild/SpellBookDarkness": {
		"kind": "weather_modifier", "cost": 15, "cooldown_ms": 360000, "cursor_radius": 0.0,
		"modifier_id": "SpellBookDarkness", "category": "SPELL",
		"modifiers": {"DAMAGE_MULT": 1.5, "ARMOR": 0.5},
		"duration_ms": 180000, "weather": "CLOUDY",
		"affects": "ANY +INFANTRY +CAVALRY +MONSTER -HERO -HORDE -MordorBlackRider -DwarvenZerker -NoldorWarrior -GondorKnightsofDol -WildBabyDrake -IsengardFanatic ALLIES",
	},
	# specialpower.ini:1484-1490 SpellBookFreezingRain: ReloadTime 360000 literal,
	#   no RadiusCursorRadius (global).
	# science.ini:501-506 SCIENCE_FreezingRain SciencePurchasePointCostMP = 15.
	# object/system/system.ini:354-367 FreezingRainSpecialPower: NO
	#   AttributeModifier at all - the payload is AntiCategory = LEADERSHIP over
	#   `AttributeModifierAffects = ALL ENEMIES`, weather-based for
	#   SPELL_FREEZINGRAIN_DURATION, ChangeWeather = RAINY, plus
	#   BurnRateModifier -100 / BurnDecayModifier 20 acting on retail's
	#   FireLogicSystem (no sim model - carried as a named residual, not dropped).
	"isengard/SpellBookFreezingRain": {
		"kind": "weather_anticategory", "cost": 15, "cooldown_ms": 360000, "cursor_radius": 0.0,
		"anti_category": "LEADERSHIP", "duration_ms": 150000, "weather": "RAINY",
		"affects": "ALL ENEMIES", "burn_rate_modifier": -100.0, "burn_decay_modifier": 20.0,
		"unconverted_behaviors": ["FireLogicSystem burn rate/decay"],
	},
	"angmar/SpellBookFreezingRain": {
		"kind": "weather_anticategory", "cost": 15, "cooldown_ms": 360000, "cursor_radius": 0.0,
		"anti_category": "LEADERSHIP", "duration_ms": 150000, "weather": "RAINY",
		"affects": "ALL ENEMIES", "burn_rate_modifier": -100.0, "burn_decay_modifier": 20.0,
		"unconverted_behaviors": ["FireLogicSystem burn rate/decay"],
	},
	# specialpower.ini:1294-1305 SpellBookUntamedAllegiance: RadiusCursorRadius
	#   60, ReloadTime SPELL_RECHARGE_TIME_TIER_2, ObjectFilter
	#   CREEP_OBJECTFILTER.
	# science.ini:396-401 SCIENCE_UntamedAllegiance
	#   SciencePurchasePointCostMP = 10.
	# object/system/system.ini:254-262 UntamedAllegianceSpecialPower:
	#   TargetEnemy = Yes, AttributeModifierAffects = CREEP_OBJECTFILTER,
	#   AttributeModifierRange = 60. There is NO AttributeModifier and NO
	#   duration on the module - the allegiance is permanent.
	# The lair list below is the `+...Lair*` subset of gamedata.ini:89, sorted.
	"mordor/SpellBookUntamedAllegiance": UNTAMED_ALLEGIANCE_ROW,
	"wild/SpellBookUntamedAllegiance": UNTAMED_ALLEGIANCE_ROW,
	"angmar/SpellBookUntamedAllegiance": UNTAMED_ALLEGIANCE_ROW,
}



## RENAMED (round 21). This was ACCEPTANCE_MIN_CASTABLE, and the name outlived
## its meaning by two rounds: round 19 made the comparison EXACT in both
## directions, so it stopped being a minimum the moment it started failing on a
## run that moved UP. A constant called MIN that rejects larger values is a trap
## for the next person to read it.
const EXPECTED_CASTABLE_POWER_COUNT := 67
## RATCHET, in the same shape as retail_slice_runner's passed-count floor, and
## added in round 18 to close this runner's biggest blind spot: every check here
## accepted `castable=false` as long as the lock carried a NAMED reason, so a
## regression that silently locked a power still produced a green
## `failed=0` run. Round 16 locked two powers exactly that way and nothing
## noticed — men/SpellBookElvenWoodMP (armor-only modifier list rejected) and
## angmar/SpellBookSnowbind (a readable PRODUCTION row taken down by an
## unreadable INVULNERABLE row beside it).
##
## MEASURED, not guessed. The true count on arrival at round 18 was 68 of 84
## powers, from .private/scratch/opus25-spellbook-BASELINE.json:
##   angmar 10, dwarves 9, elves 10, isengard 8, men 11, mordor 10, wild 10.
## The two restorations above bring it to 70 (angmar 11, men 12).
##
## ROUND 19, TWO CHANGES.
##
## 1. THE COUNT IS NOW EXACT IN BOTH DIRECTIONS. A one-sided floor still let a
##    run drift upward in silence — nothing failed, only a print asked for an
##    update, and prints are not gates. `castable != EXPECTED_CASTABLE_POWER_COUNT`
##    now FAILS whichever way it moved; the RATCHET line below still names the
##    new value so the conscious update is a copy, not an investigation.
##
## 2. THE COUNT ALONE IS NOT ENOUGH, so it is no longer the only gate. A count
##    cannot see a SWAP: lock one power, unlock another, and 70 is still 70.
##    EXPECTED_LOCKED_POWER_KEYS below pins the identities as a SET, in the same
##    shape as EXPECTED_PENDING_MISMATCH_KEYS.
##
## The value moved 70 -> 65 this round. That is NOT a regression: five powers
## that were being counted as working are now counted as INERT — see
## EXPECTED_INERT_POWER_KEYS. The later 65 -> 67 move is separately earned by
## the typed Enshrouding Mist camouflage lane and focused Scavenger bounty lane;
## both identities were removed from the locked set in the same change.
const EXPECTED_EXCLUDED_PENDING := 1
## Named, counted exclusions. One entry as of 2026-08-04; see the
## isengard/SpellBookDragonStrike row above for why it is excluded rather than
## re-pinned to its (empty, implausible) observed result.
const EXPECTED_PENDING_MISMATCH_KEYS := {"isengard/SpellBookDragonStrike": true}

## THE LOCKED SET, PINNED BY IDENTITY. Measured on this tree
## (.private/scratch/opus26-spellbook-BASELINE.json, 14 of 84). Set equality, so
## it fails in BOTH directions: a newly locked power fails, and an UNLOCKED one
## fails too and demands that the change which earned it says so here. A count
## alone could never see a swap.
const EXPECTED_LOCKED_POWER_KEYS := {
	"angmar/SpellBookAvalanche": true,
	"dwarves/SpellBookBombard": true,
	"dwarves/SpellBookCitadel": true,
	"dwarves/SpellBookUndermine": true,
	"elves/SpellBookFlood": true,
	"isengard/SpellBookDevastation": true,
	"isengard/SpellBookDragonStrike": true,
	"isengard/SpellBookFueltheFires": true,
	"isengard/SpellBookWatcherAlly": true,
	"mordor/SpellBookBarricade": true,
	"mordor/SpellBookEvilBombard": true,
	"wild/SpellBookWatcherAlly": true,
}

## CASTABLE BUT INERT — counted separately, never as working.
##
## A power that compiles, purchases and CASTS can still have a payload that
## nothing in the sim consumes, and the castable count could not tell the
## difference. Five powers are in exactly that state, and they are a FAMILY, not
## one straggler. Measured with .private/scratch/opus26-snowbind-probe.out.log,
## which dumps every compiled `attribute_modifier` effect:
##
##   angmar/SpellBookSnowbind        production_mult 0.01  ANY ENEMIES ALLIES +STRUCTURE
##   angmar/SpellBookBlight          production_mult 0.50  resource buildings, ENEMIES
##   dwarves/SpellBookDwarvenRiches  production_mult 3.00  resource buildings, ALLIES
##   isengard/SpellBookIndustry      production_mult 3.00  resource buildings, ALLIES
##   mordor/SpellBookIndustry        production_mult 3.00  resource buildings, ALLIES
##
## Every one of them carries a NEUTRAL damage_mult and armor_mult (1.0), so
## `production_mult` is the entire payload — and `_cast_spellbook_attribute_modifier`
## writes only `rally_until_tick` / `rally_damage_mult` onto ALLIED BATTALIONS.
## It never reads `production_mult` and never touches a structure. Nothing else
## in the sim reads the effect's `production_mult` either (the structure field
## `production_multiplier` that _queue_production divides by is written only by
## the upgrade lane). The cast therefore returns `no-allies-in-range` and
## changes nothing at all.
##
## THE MEMBERSHIP TEST IS DERIVED, NOT LISTED. The runner classifies a power as
## inert by looking at the compiled effect — attribute_modifier, neutral damage
## and armor, non-neutral production — and only THEN compares the resulting set
## against these keys. A consumer landing for production makes the derived set
## shrink and this pin fail, which is the point.
##
## WHY NOT IMPLEMENTED THIS ROUND (the honest version, with the evidence):
##  - The two halves need DIFFERENT consumption sites. Four of the five target
##    resource buildings (gamedata.ini:175 INDUSTRY_TYPE_SPELL_OBJECT_FILTER =
##    farms/mines/mallorn/furnace/slaughterhouse), whose retail PRODUCTION is
##    the income RATE — `income_per_payout` in _step_economy. Snowbind targets
##    `+STRUCTURE` on all sides (gamedata.ini:3609) including producers, whose
##    PRODUCTION is BUILD TIME — the `production_multiplier` divisor in
##    _queue_production. Wiring one site would leave the other four inert while
##    reporting the family as fixed.
##  - The filters are authored as SOURCE OBJECT NAMES (`+GondorFarm`,
##    `+DwarvenMineShaft`, …), and no structure-side object filter exists in the
##    sim — `_spellbook_object_kinds` maps BATTALION rows only.
##  - The durations do not resolve in the packs. Snowbind authors
##    `Duration = SNOWBIND_EFFECT_DURATION` (30000, gamedata.ini:3608) and the
##    pack ships no defines table, so the compiled effect says `permanent` —
##    which is right for Industry/Riches (no Duration row at all) and WRONG for
##    Snowbind. Shipping a permanent 1% production debuff would be worse than
##    shipping nothing.
## Named as a gap, counted as a gap. Item 2/3 in "Open, named, not fixed".
const EXPECTED_INERT_POWER_KEYS := {
	"angmar/SpellBookBlight": true,
	"angmar/SpellBookSnowbind": true,
	"dwarves/SpellBookDwarvenRiches": true,
	"isengard/SpellBookIndustry": true,
	"mordor/SpellBookIndustry": true,
}

var passed := 0
var failed := 0
var excluded_pending := 0
var total_castable := 0
var total_powers := 0
var locked_power_keys: Array[String] = []
var locked_power_key_set: Dictionary = {}
var inert_power_key_set: Dictionary = {}
var pending_mismatch_keys: Dictionary = {}
var report: Dictionary = {"spellbooks": {}, "summary": {}}


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var content_db = root.get_node_or_null("ContentDB")
	if content_db == null:
		push_error("SPELLBOOK_MATRIX FAIL no ContentDB")
		quit(1)
		return
	await process_frame
	await process_frame
	_assert_summon_policy_contracts()
	_assert_modifier_row_contracts()

	var structures: Dictionary = {}
	if content_db.has_method("get_playable_structure_runtimes"):
		structures = content_db.call("get_playable_structure_runtimes") as Dictionary
	var diagnostic_faction := OS.get_environment("OPENBFME_GOAL_DIAGNOSTIC_FACTION").strip_edges().to_lower()
	var aggregate_only := OS.get_environment("OPENBFME_GOAL_AGGREGATE_ONLY").strip_edges() == "1"
	print("SPELLBOOK_MATRIX_MODE faction=%s powers=%s aggregate_only=%s" % [
		diagnostic_faction,
		OS.get_environment("OPENBFME_GOAL_DIAGNOSTIC_POWERS").strip_edges(),
		aggregate_only,
	])
	for faction_id in FACTIONS:
		if diagnostic_faction != "" and faction_id != diagnostic_faction:
			continue
		_matrix_faction_spellbook(faction_id, structures)
	if diagnostic_faction != "" and not aggregate_only:
		print("SPELLBOOK_MATRIX_DIAGNOSTIC faction=%s totals=%s passed=%d failed=%d" % [
			diagnostic_faction,
			str((report.get("spellbooks", {}) as Dictionary).get(diagnostic_faction, {}).get("totals", {})),
			passed,
			failed,
		])
		quit(0 if failed == 0 else 1)
		return

	_check(
		"runner", "castable_power_count_is_exact",
		total_castable == EXPECTED_CASTABLE_POWER_COUNT,
		"castable=%d/%d pinned=%d — SET EXPECTED_CASTABLE_POWER_COUNT to %d in this change; locked=%s inert=%s" % [
			total_castable, total_powers, EXPECTED_CASTABLE_POWER_COUNT, total_castable,
			locked_power_keys, inert_power_key_set.keys()
		]
	)
	if total_castable != EXPECTED_CASTABLE_POWER_COUNT:
		# _check prints its detail on FAIL, but the ratchet instruction is worth its
		# own line either way: a run that moved UP is the case a one-sided floor
		# used to wave through.
		print("SPELLBOOK_MATRIX RATCHET castable=%d against EXPECTED_CASTABLE_POWER_COUNT=%d — set the constant in the change that earned it" % [
			total_castable, EXPECTED_CASTABLE_POWER_COUNT])
	# THE SET, NOT JUST THE COUNT. Swapping one lock for another leaves the count
	# untouched; comparing identities catches it, and catches an UNLOCK too — an
	# unlock is good news that still has to be recorded consciously.
	_check(
		"runner", "locked_power_keys_exact",
		locked_power_key_set == EXPECTED_LOCKED_POWER_KEYS,
		"newly_locked=%s newly_unlocked=%s (actual=%d expected=%d)" % [
			_key_difference(locked_power_key_set, EXPECTED_LOCKED_POWER_KEYS),
			_key_difference(EXPECTED_LOCKED_POWER_KEYS, locked_power_key_set),
			locked_power_key_set.size(), EXPECTED_LOCKED_POWER_KEYS.size(),
		]
	)
	_check(
		"runner", "inert_power_keys_exact",
		inert_power_key_set == EXPECTED_INERT_POWER_KEYS,
		"newly_inert=%s no_longer_inert=%s (actual=%d expected=%d)" % [
			_key_difference(inert_power_key_set, EXPECTED_INERT_POWER_KEYS),
			_key_difference(EXPECTED_INERT_POWER_KEYS, inert_power_key_set),
			inert_power_key_set.size(), EXPECTED_INERT_POWER_KEYS.size(),
		]
	)
	report["summary_castable"] = {
		"castable": total_castable, "powers": total_powers,
		"pinned": EXPECTED_CASTABLE_POWER_COUNT, "locked": locked_power_keys,
		"inert": inert_power_key_set.keys(),
	}
	_check(
		"runner", "excluded_pending_exact_known_count",
		excluded_pending == EXPECTED_EXCLUDED_PENDING,
		"excluded_pending=%d expected_literal=%d; a rebuild task must consciously update EXPECTED_EXCLUDED_PENDING" % [excluded_pending, EXPECTED_EXCLUDED_PENDING]
	)
	_check(
		"runner", "excluded_pending_exact_known_names",
		pending_mismatch_keys == EXPECTED_PENDING_MISMATCH_KEYS,
		"actual=%s expected=%s; update the named pending allowlist consciously" % [
			pending_mismatch_keys.keys(), EXPECTED_PENDING_MISMATCH_KEYS.keys()
		]
	)
	report["summary"] = {"passed": passed, "failed": failed, "excluded_pending": excluded_pending}
	var out_path := OS.get_environment("OPENBFME_GOAL_SPELLBOOK_OUT").strip_edges()
	if out_path == "":
		out_path = OS.get_environment("OPENBFME_GOAL_MATRIX_OUT").strip_edges()
	if out_path != "":
		var f := FileAccess.open(out_path, FileAccess.WRITE)
		if f != null:
			f.store_string(JSON.stringify(report, "\t"))
			f.close()
			print("SPELLBOOK_MATRIX wrote ", out_path)
	print("SPELLBOOK_MATRIX_RESULT passed=%d failed=%d excluded_pending=%d" % [passed, failed, excluded_pending])
	quit(0 if failed == 0 else 1)


func _structure_production_multipliers(sim) -> Dictionary:
	## Every structure's live production multiplier, keyed by id — the one field
	## a PRODUCTION payload would have to move to be doing anything.
	var snapshot: Dictionary = {}
	for structure_id in sim.structure_ids():
		snapshot[structure_id] = float(
			(sim.structures[structure_id] as Dictionary).get("production_multiplier", 1.0))
	return snapshot


func _battalion_rally_fields(sim) -> Dictionary:
	## The ONLY two fields _cast_spellbook_attribute_modifier writes, per entity.
	## Watching these is what makes "this cast changes nothing" a measurement of
	## the cast's own write path rather than of a field it never touches.
	var snapshot: Dictionary = {}
	for entity_id in sim.entity_ids():
		var row: Dictionary = sim.entities[entity_id]
		snapshot[entity_id] = [
			int(row.get("rally_until_tick", -1)),
			float(row.get("rally_damage_mult", 1.0)),
		]
	return snapshot


func _key_difference(left: Dictionary, right: Dictionary) -> Array:
	## Keys present in `left` and absent from `right`, so a set mismatch reports
	## WHICH powers moved rather than two long lists to diff by eye.
	var only: Array = []
	for key in left.keys():
		if not right.has(key):
			only.append(key)
	only.sort()
	return only


func _is_inert_production_power(row: Dictionary) -> bool:
	## DERIVED, not listed. A power is inert when its whole compiled payload is a
	## PRODUCTION multiplier: an attribute_modifier effect whose damage and armor
	## multipliers are the neutral 1.0 and whose production multiplier is not.
	## Nothing in the sim reads an effect's `production_mult`, so such a cast
	## changes no state at all.
	##
	## THE TRUE FORCING FUNCTION, named honestly. The paragraph above used to
	## claim that deriving the set means "the pin FAILS the day a consumer lands".
	## It does not, and it cannot: this classifier reads the COMPILED EFFECT and
	## nothing else. Wiring `production_mult` into _step_economy or
	## _queue_production would not change a single field it looks at, so the
	## derived set would not move and `inert_power_keys_exact` would stay green
	## with the power still listed as inert.
	##
	## What actually forces the update is the pair of runtime checks at the cast
	## site — inert_production_cast_changes_nothing_<power> and its seeding guard.
	## Those watch the two places a payload could land (the structures'
	## `production_multiplier` and the battalions' rally fields). The day a
	## consumer lands, the cast stops changing nothing, THAT check goes red, and
	## the change that wired it has to move the key out of
	## EXPECTED_INERT_POWER_KEYS and re-pin EXPECTED_CASTABLE_POWER_COUNT.
	##
	## What deriving the membership DOES buy is the opposite direction: a power
	## whose compiled payload newly becomes production-only is picked up
	## automatically instead of being silently missing from a hand-written list.
	var effect: Dictionary = row.get("effect", {}) as Dictionary
	if String(effect.get("kind", "")) != "attribute_modifier":
		return false
	if not is_equal_approx(float(effect.get("production_mult", 1.0)), 1.0):
		return (
			is_equal_approx(float(effect.get("damage_mult", 1.0)), 1.0)
			and is_equal_approx(float(effect.get("armor_mult", 1.0)), 1.0)
		)
	return false


func _matrix_faction_spellbook(faction_id: String, structures: Dictionary) -> void:
	var slice_script = load("res://src/retail_slice/retail_vertical_slice.gd")
	var manifest_script = load("res://src/retail_slice/retail_faction_manifest.gd")
	if slice_script == null or manifest_script == null:
		_check(faction_id, "scripts", false, "slice or manifest script failed to load")
		return
	var slice = slice_script.new()
	var map_data = load("res://src/retail_slice/retail_map_data.gd").new()
	map_data.local_transform_scale = 0.1
	slice.source_map_data = map_data
	slice.faction_manifest = {"faction": faction_id}
	slice._classify_faction_units(faction_id)
	var fieldable: Dictionary = (slice.fieldable_unit_runtimes as Dictionary).duplicate(true)
	var producible: Dictionary = (slice.producible_unit_runtimes as Dictionary).duplicate(true)
	var manifest: Dictionary = manifest_script.from_registries(faction_id, fieldable, structures)
	var builder_unit_rules: Dictionary = {}
	for builder_value in manifest.get("builder_unit_ids", []) as Array:
		var builder_id := String(builder_value)
		var builder_rule: Dictionary = slice._faction_builder_unit_rule(builder_id)
		if not builder_rule.is_empty():
			builder_unit_rules[builder_id] = builder_rule
	var doc: Dictionary = slice._faction_spellbook_document(faction_id)
	slice.free()

	var faction_report: Dictionary = {
		"document_found": not doc.is_empty(),
		"powers": {},
		"configure_ok": false,
		"configure_error": "",
	}
	if doc.is_empty():
		_check(faction_id, "document", false, "no spellbook-runtime for faction")
		report["spellbooks"][faction_id] = faction_report
		return
	_check(faction_id, "document", true, String(doc.get("_pack_file_key", "")))

	var sim = SimScript.new()
	sim._apply_gameplay_rules({
		"enable_base_loop": true,
		"faction_manifest": manifest,
		"playable_unit_runtimes": producible,
		"producer_kind_by_source_object": manifest.get("producer_kind_registry", {}),
		"unit_rules": builder_unit_rules,
		"starting_resources": 1000000,
		"source_map_transform_scale": 0.1,
		"retail_faction_sides": manifest_script.retail_faction_sides(),
	})
	if String(sim.configuration_error) != "":
		_check(faction_id, "sim_configure", false, String(sim.configuration_error))
		faction_report["configure_error"] = String(sim.configuration_error)
		report["spellbooks"][faction_id] = faction_report
		return
	sim.setup({}, sim._rules)
	sim.ai_enabled = false
	var rng_state_before: Array = sim._logic_random_state.duplicate()
	var rng_draws_before := int(sim.logic_random_draws)
	if not sim.configure_spellbook_runtime(doc):
		_check(faction_id, "spellbook_configure", false, String(sim._spellbook_error))
		faction_report["configure_error"] = String(sim._spellbook_error)
		report["spellbooks"][faction_id] = faction_report
		return
	faction_report["configure_ok"] = true
	_check(faction_id, "spellbook_configure", true, "powers=%d" % sim._spellbook_order.size())
	_check(
		faction_id,
		"spellbook_configure_rng_zero",
		sim._logic_random_state == rng_state_before and int(sim.logic_random_draws) == rng_draws_before,
		"state_words=%d->%d draws=%d->%d" % [rng_state_before.size(), sim._logic_random_state.size(), rng_draws_before, sim.logic_random_draws]
	)
	# A retail ObjectNames pool rerolls on every cast. Exercise the runtime
	# helper twice and pin one shared-logic-RNG draw per cast, never at configure.
	var pool_draws_before := int(sim.logic_random_draws)
	var synthetic_pool := [{
		"block_index": 0,
		"pick_count": 1,
		"choices": [{"object_id": "PoolA"}, {"object_id": "PoolB"}],
	}]
	sim._spellbook_resolve_summon_targets(synthetic_pool)
	sim._spellbook_resolve_summon_targets(synthetic_pool)
	_check(
		faction_id,
		"summon_pool_rng_per_cast",
		int(sim.logic_random_draws) == pool_draws_before + 2,
		"draws=%d->%d" % [pool_draws_before, sim.logic_random_draws]
	)
	# Two byte-identical CreateObject blocks remain separate groups by ordinal;
	# both contribute a spawn instead of sharing a memoized count.
	var duplicate_block_targets: Array = sim._spellbook_resolve_summon_targets([
		{"block_index": 0, "pick_count": 1, "choices": [{"object_id": "Same"}]},
		{"block_index": 1, "pick_count": 1, "choices": [{"object_id": "Same"}]},
	])
	var duplicate_block_count := 0
	for target_value in duplicate_block_targets:
		duplicate_block_count += int((target_value as Dictionary).get("count", 0))
	_check(
		faction_id,
		"summon_duplicate_create_blocks",
		duplicate_block_targets.size() == 2 and duplicate_block_count == 2,
		"targets=%d count=%d" % [duplicate_block_targets.size(), duplicate_block_count]
	)

	# Flood power points so purchase order is the only gate.
	sim.team_power_points[0] = 100000

	var order: Array = sim._spellbook_order.duplicate()
	var diagnostic_power := OS.get_environment("OPENBFME_GOAL_DIAGNOSTIC_POWER").strip_edges()
	var diagnostic_powers: Array[String] = []
	for value in OS.get_environment("OPENBFME_GOAL_DIAGNOSTIC_POWERS").split(",", false):
		var selected := String(value).strip_edges()
		if selected != "":
			diagnostic_powers.append(selected)
	var aggregate_only := OS.get_environment("OPENBFME_GOAL_AGGREGATE_ONLY").strip_edges() == "1"
	var castable_count := 0
	var unsupported_count := 0
	var cast_ok_count := 0
	var processed_count := 0
	for power_id_value in order:
		var power_id := String(power_id_value)
		if diagnostic_power != "" and power_id != diagnostic_power:
			continue
		if not diagnostic_powers.is_empty() and power_id not in diagnostic_powers:
			continue
		processed_count += 1
		var power_started_ms := Time.get_ticks_msec()
		print("SPELLBOOK_MATRIX_POWER_START faction=%s power=%s" % [faction_id, power_id])
		var row: Dictionary = sim._spellbook_powers.get(power_id, {}) as Dictionary
		var power_report: Dictionary = {
			"castable": bool(row.get("castable", false)),
			"locked_reason": String(row.get("locked_reason", "")),
			"module": String(row.get("module", "")),
			"cost": int(row.get("cost", 0)),
		}
		_assert_layered_summon_effect(faction_id, power_id, row)
		_assert_terrain_reveal_effect(faction_id, power_id, row)
		_assert_weather_allegiance_effect(faction_id, power_id, row)
		total_powers += 1
		var power_key := "%s/%s" % [faction_id, power_id]
		if not bool(row.get("castable", false)):
			unsupported_count += 1
			locked_power_keys.append(power_key)
			locked_power_key_set[power_key] = true
			# Named gap is acceptable; silent empty reason is not.
			var reason := String(row.get("locked_reason", ""))
			_check(
				faction_id,
				"unsupported_" + power_id,
				reason != "",
				"castable=false reason=" + reason
			)
			power_report["status"] = "unsupported"
			faction_report["powers"][power_id] = power_report
			print("SPELLBOOK_MATRIX_POWER_DONE faction=%s power=%s status=unsupported elapsed_ms=%d" % [
				faction_id, power_id, Time.get_ticks_msec() - power_started_ms])
			continue
		# CASTS, BUT CHANGES NOTHING. Counted under its own name and NOT as
		# castable — see EXPECTED_INERT_POWER_KEYS for the evidence and for why the
		# consumption is not wired this round. The power still runs the whole
		# purchase and cast path below, so a regression in either still shows up as
		# its own failure; it just does not get to inflate the working count.
		var inert := _is_inert_production_power(row)
		if inert:
			inert_power_key_set[power_key] = true
			power_report["production_mult"] = float(
				(row.get("effect", {}) as Dictionary).get("production_mult", 1.0))
		else:
			castable_count += 1
			total_castable += 1
		if aggregate_only:
			power_report["status"] = "aggregate-castable-inert" if inert else "aggregate-castable"
			faction_report["powers"][power_id] = power_report
			print("SPELLBOOK_MATRIX_POWER_DONE faction=%s power=%s status=%s elapsed_ms=%d" % [
				faction_id, power_id, power_report["status"], Time.get_ticks_msec() - power_started_ms])
			continue
		# Seed a friendly battalion so heal/rally-style powers have allies in range.
		_ensure_ally_for_cast(sim, 0)
		# Purchase may require prerequisites already owned — buy in tree order.
		var already_owned := sim.has_power(0, power_id)
		var purchase: Dictionary = {"ok": true, "reason": "already-owned"} if already_owned else sim.purchase_power(0, power_id)
		if not bool(purchase.get("ok", false)):
			# Try purchasing prerequisites first by scanning purchasable from UI state.
			var ui: Dictionary = sim.spellbook_ui_state(0)
			var ui_powers: Dictionary = ui.get("powers", {}) as Dictionary
			for _attempt in range(order.size() + 2):
				var progressed := false
				for candidate_value in order:
					var candidate := String(candidate_value)
					if sim.has_power(0, candidate):
						continue
					var state: Dictionary = ui_powers.get(candidate, {}) as Dictionary
					if bool(state.get("purchasable", false)):
						var buy: Dictionary = sim.purchase_power(0, candidate)
						if bool(buy.get("ok", false)):
							progressed = true
				ui = sim.spellbook_ui_state(0)
				ui_powers = ui.get("powers", {}) as Dictionary
				if sim.has_power(0, power_id):
					purchase = {"ok": true, "reason": "already-owned"}
					break
				purchase = sim.purchase_power(0, power_id)
				if bool(purchase.get("ok", false)) or not progressed:
					break
		if not bool(purchase.get("ok", false)) and String(purchase.get("reason", "")) == "already-purchased":
			purchase = {"ok": true, "reason": "already-purchased"}
		if not bool(purchase.get("ok", false)):
			_check(
				faction_id,
				"purchase_" + power_id,
				false,
				String(purchase.get("reason", ""))
			)
			power_report["status"] = "purchase-failed"
			power_report["purchase_reason"] = String(purchase.get("reason", ""))
			faction_report["powers"][power_id] = power_report
			continue
		_check(faction_id, "purchase_" + power_id, true, String(purchase.get("reason", "")))
		sim.accept_spellbook_purchases(0)
		var cast_point := Vector2(10.0, 10.0)
		# Some effect families need a live target on the map BEFORE the cast, or
		# the cast has nothing to prove: Freezing Rain needs an enemy to suppress,
		# Untamed Allegiance needs creep lairs to defect. Staged per effect kind so
		# no other power in the matrix sees a perturbed world.
		var staged: Dictionary = _stage_weather_allegiance_targets(sim, row, cast_point)
		var entity_ids_before: Array[int] = sim.entity_ids().duplicate()
		var production_before: Dictionary = _structure_production_multipliers(sim)
		var rally_before: Dictionary = _battalion_rally_fields(sim)
		var cast: Dictionary
		if bool(row.get("nonpressable", false)):
			var consumed := sim._consumed_nonpressable_powers.get(0, {}) as Dictionary
			cast = {
				"ok": consumed.has(power_id),
				"reason": "activated-on-purchase" if consumed.has(power_id) else "nonpressable-not-activated",
			}
		else:
			cast = sim.cast_power(0, power_id, cast_point)
		var cast_ok := bool(cast.get("ok", false))
		var cast_reason := String(cast.get("reason", ""))
		# Empty-target outcomes prove the effect filter ran, but they are NOT
		# successful casts (no cooldown, no power.cast event). Report them as a
		# separate effect-path bucket so the matrix cannot greenwash them.
		var empty_target := not cast_ok and cast_reason in [
			"no-wounded-allies-in-range",
			"no-allies-in-range",
			"no-enemies-in-range",
			"no-valid-targets",
		]
		if cast_ok:
			cast_ok_count += 1
			_check(faction_id, "cast_" + power_id, true, cast_reason)
			power_report["status"] = "cast-ok"
			var literal: Dictionary = LAYERED_SUMMON_EXPECTATIONS.get(
				"%s/%s" % [faction_id, power_id], {}
			) as Dictionary
			var runtime_assertable := (
				String(literal.get("status", "")) != "pending-rebuild"
				or bool(row.get("_literal_assert_active", false))
			)
			if power_id in Array(SUMMON_SCOPE.get(faction_id, [])) and String((row.get("effect", {}) as Dictionary).get("kind", "")) == "summon" and runtime_assertable:
				_assert_summon_runtime(sim, faction_id, power_id, row, entity_ids_before)
			if TERRAIN_REVEAL_EXPECTATIONS.has("%s/%s" % [faction_id, power_id]):
				_assert_terrain_reveal_runtime(sim, faction_id, power_id, row, cast_point)
			if WEATHER_ALLEGIANCE_EXPECTATIONS.has("%s/%s" % [faction_id, power_id]):
				_assert_weather_allegiance_runtime(sim, faction_id, power_id, row, cast_point, staged)
		elif empty_target:
			_check(
				faction_id,
				"effect_path_" + power_id,
				true,
				"empty-targets:" + cast_reason
			)
			power_report["status"] = "effect-path-empty-targets"
		else:
			_check(
				faction_id,
				"cast_" + power_id,
				false,
				cast_reason + " " + String(cast.get("detail", ""))
			)
			power_report["status"] = "cast-failed"
		power_report["cast_reason"] = cast_reason
		if inert:
			# DEMONSTRATE the inertness rather than asserting it — and demonstrate
			# it against the fields the cast can actually REACH, which is the part
			# this check used to get wrong.
			#
			# TWO SIDES, because a PRODUCTION-only payload could only be doing
			# something in one of two places:
			#   * the production multiplier on a structure — the only site in the
			#     sim that consumes one is _queue_production, which divides the
			#     authored build time by the PRODUCER's `production_multiplier`;
			#   * the rally fields on an allied battalion — because
			#     _cast_spellbook_attribute_modifier writes `rally_until_tick` and
			#     `rally_damage_mult` and NOTHING else. Watching only the structure
			#     side left the cast's own write path unobserved: a payload that
			#     started buffing allies would have passed this check unchanged.
			#
			# THE SEEDING GUARD is what makes the structure half mean anything. An
			# empty structure table compares equal to itself, so `{} == {}` would
			# have reported "changed nothing" on a sim that had nothing to change.
			# The fixture must actually be carrying structures for the comparison
			# to be evidence.
			var production_after: Dictionary = _structure_production_multipliers(sim)
			var rally_after: Dictionary = _battalion_rally_fields(sim)
			_check(
				faction_id,
				"inert_production_fixture_has_structures_to_change_" + power_id,
				production_before.size() > 0,
				"structures=%d — an empty table makes the comparison below vacuous" % production_before.size()
			)
			_check(
				faction_id,
				"inert_production_cast_changes_nothing_" + power_id,
				production_after == production_before and rally_after == rally_before,
				"production_mult=%.4f structures_before=%s structures_after=%s rally_before=%s rally_after=%s cast=%s" % [
					float((row.get("effect", {}) as Dictionary).get("production_mult", 1.0)),
					production_before, production_after, rally_before, rally_after, cast_reason
				]
			)
			power_report["status"] = "castable-inert-production/" + String(power_report.get("status", ""))
		faction_report["powers"][power_id] = power_report
		print("SPELLBOOK_MATRIX_POWER_DONE faction=%s power=%s status=%s elapsed_ms=%d" % [
			faction_id, power_id, power_report.get("status", "unknown"),
			Time.get_ticks_msec() - power_started_ms])

	faction_report["totals"] = {
		"powers": processed_count,
		"configured_powers": order.size(),
		"castable": castable_count,
		"unsupported_named": unsupported_count,
		"cast_ok": cast_ok_count,
	}
	_check(
		faction_id,
		"has_powers",
		order.size() > 0,
		"powers=%d" % order.size()
	)
	report["spellbooks"][faction_id] = faction_report


func _assert_summon_runtime(sim, faction_id: String, power_id: String, row: Dictionary, before_ids: Array[int]) -> void:
	## A successful summon cast is not enough: prove the authored hatch creates
	## the expected live objects for the caster, then prove LifetimeUpdate removes
	## every one at or before the longest authored target lifetime.
	var effect: Dictionary = row.get("effect", {}) as Dictionary
	var literal: Dictionary = LAYERED_SUMMON_EXPECTATIONS.get(
		"%s/%s" % [faction_id, power_id], {}
	) as Dictionary
	var targets: Array = []
	for group_value in Array(effect.get("target_groups", [])):
		for choice_value in Array((group_value as Dictionary).get("choices", [])):
			targets.append(choice_value)
	if targets.is_empty():
		targets = effect.get("targets", []) as Array
	var expected_count := 0
	var expected_by_object: Dictionary = {}
	var longest_lifetime := 0
	for target_value in targets:
		var target := target_value as Dictionary
		var count := int(target.get("count", 0))
		var object_id := String(target.get("object_id", ""))
		expected_count += count
		expected_by_object[object_id] = int(expected_by_object.get(object_id, 0)) + count
		longest_lifetime = maxi(longest_lifetime, int(target.get("lifetime_ticks", 0)))
	if bool(row.get("_literal_assert_active", false)):
		expected_by_object = (literal.get("objects", {}) as Dictionary).duplicate(true)
		expected_count = 0
		for value in expected_by_object.values():
			expected_count += int(value)
	var hatch_delay := int(effect.get("hatch_delay_ticks", 0))
	_advance_effect_lifecycle(sim, maxi(1, hatch_delay))
	var spawned_ids: Array[int] = []
	var actual_by_object: Dictionary = {}
	var pool_ids: Array = literal.get("object_pool", []) as Array
	var actual_pool_count := 0
	var all_caster_team := true
	# Retail splits summon end-of-life into two different contracts, so this
	# runner must too (see the FADED_* citations above _assert_summon_runtime's
	# expiry block). Partition the spawned rows by their authored LifetimeUpdate
	# DeathType at spawn time, because a FADED row is ERASED at expiry and its
	# death type can no longer be read afterwards.
	var faded_ids: Array[int] = []
	var corpse_ids: Array[int] = []
	# Authored despawn tick per spawned row, captured while the row is still
	# registered. A row that dies EARLY (combat) is erased from the registry by
	# _bookkeep_battalion_death, so this is the only point it can be read.
	var despawn_tick_by_id: Dictionary = {}
	for entity_id in sim.entity_ids():
		if entity_id in before_ids:
			continue
		var entity: Dictionary = sim.entity(entity_id)
		var object_id := String(entity.get("object_id", ""))
		# Other castable summon powers in the same faction matrix may have a
		# pending hatch. Ignore those unrelated object ids; this assertion owns
		# only the current power's converted target set.
		if not expected_by_object.has(object_id) and not pool_ids.has(object_id):
			continue
		spawned_ids.append(entity_id)
		if String(entity.get("summon_lifetime_death_type", "")).to_upper() == "FADED":
			faded_ids.append(entity_id)
			despawn_tick_by_id[entity_id] = int(sim._summon_despawn_ticks.get(entity_id, -1))
		else:
			corpse_ids.append(entity_id)
		all_caster_team = all_caster_team and int(entity.get("team", -1)) == 0
		if pool_ids.has(object_id):
			actual_pool_count += 1
		else:
			actual_by_object[object_id] = int(actual_by_object.get(object_id, 0)) + 1
	var expected_pool_count := int(literal.get("pool_pick_count", 0)) if bool(row.get("_literal_assert_active", false)) else 0
	_check(
		faction_id,
		"summon_entities_" + power_id,
		spawned_ids.size() == expected_count + expected_pool_count and actual_by_object == expected_by_object and actual_pool_count == expected_pool_count,
		"spawned=%s actual=%s pool=%d expected=%s pool_expected=%d" % [spawned_ids, actual_by_object, actual_pool_count, expected_by_object, expected_pool_count]
	)
	_check(
		faction_id,
		"summon_team_" + power_id,
		not spawned_ids.is_empty() and all_caster_team,
		"spawned=%s" % [spawned_ids]
	)
	var aura_registry_ok := true
	var aura_sources: Array[int] = []
	for entity_id in spawned_ids:
		var has_auras := not Array(sim.entity(entity_id).get("summon_auras", [])).is_empty()
		aura_registry_ok = aura_registry_ok and (
			sim._summon_aura_source_ids.has(entity_id) == has_auras
		)
		if has_auras:
			aura_sources.append(entity_id)
			# Prove aura liveness is not coupled to LifetimeUpdate membership.
			var saved_expiry: Variant = sim._summon_despawn_ticks.get(entity_id, null)
			sim._summon_despawn_ticks.erase(entity_id)
			sim._step_summon_auras()
			aura_registry_ok = aura_registry_ok and sim._summon_aura_source_ids.has(entity_id)
			if saved_expiry != null:
				sim._summon_despawn_ticks[entity_id] = saved_expiry
	_check(
		faction_id, "summon_aura_source_registry_" + power_id,
		aura_registry_ok, "spawned=%s aura_sources=%s registry=%s" % [
			spawned_ids, aura_sources, sim._summon_aura_source_ids.keys()
		]
	)
	## END-OF-LIFE CONTRACT (two distinct retail rules, asserted separately).
	##
	## FADED: retail's LifetimeUpdate DeathType = FADED routes the expiry into a
	## SlowDeathBehavior whose DeathTypes row is `NONE +FADED` — a fade-out, not a
	## corpse. Oracle is the PURE retail tree; paths below are relative to
	## .private/retail-work/editions/rotwk/cache/effective-assets/data/ini
	## (NOT the sibling layered-effective-assets tree, which rewrites several of
	## these magnitudes in place — re-cited 2026-08-04, round 13):
	##   object/goodfaction/generic/tombombadil.ini:535-538    MinLifetime/MaxLifetime 60000, DeathType = FADED
	##   object/goodfaction/generic/tombombadil.ini:541-548    SlowDeathBehavior ModuleTag_Fading, DeathTypes = NONE +FADED, DestructionDelay = 1000
	##   object/goodfaction/units/elven/gwaihir.ini:447-450    MinLifetime/MaxLifetime 120000, DeathType = FADED
	##   object/goodfaction/units/elven/gwaihir.ini:453-460    DeathTypes = NONE +FADED, DestructionDelay = 2500
	##   object/evilfaction/units/evilbeasts/watcher.ini:167-170  MinLifetime/MaxLifetime WATCHER_LIFETIME (gamedata.ini:7730 = 30000), DeathType = FADED
	##   object/evilfaction/units/evilbeasts/watcher.ini:179-182  DeathTypes = NONE +FADED, DestructionDelay = 3800
	##     (:173-176 is the CONTRASTING non-FADED path, DeathTypes = ALL -FADED)
	##   object/neutral/barrowwight.ini:279-283                MinLifetime/MaxLifetime CREATE_A_HERO_REINFORCEMENT_LIFETIME (createaherogamedata.inc:397 = 90000), DeathType = FADED
	##   object/neutral/barrowwight.ini:286-290                DeathTypes = NONE +FADED, DestructionDelay = 5000
	##   object/evilfaction/units/wild/mountaingiant.ini:1096-1100  MinLifetime/MaxLifetime CREATE_A_HERO_REINFORCEMENT_LIFETIME = 90000, DeathType = FADED
	##   object/evilfaction/units/wild/mountaingiant.ini:1103-1107  DeathTypes = NONE +FADED, DestructionDelay = 5000
	##   object/goodfaction/units/ents/entsinfantry.ini:476-479     MinLifetime/MaxLifetime 120000, DeathType = FADED (RohanEntFir_Summoned; Birch at :627-630 is identical)
	##   object/goodfaction/units/ents/entsinfantry.ini:387-394     DeathTypes = NONE +FADED, DestructionDelay = 10000 (longest authored FADED delay found)
	## Every authored FADED DestructionDelay is 1000..10000 ms of fade — an order of
	## magnitude under this simulation's 60000 ms corpse window
	## (SimScript.CORPSE_LIFETIME_TICKS = 600 @ TICK_SECONDS 0.1), and no FADED path
	## carries any corpse-retention row. The converter therefore models FADED as a
	## DestroyDie with included_death_types ["FADED"]
	## (game/src/retail_slice/retail_slice_sim.gd:5511-5517), so the row is erased in
	## the SAME tick its authored lifetime ends: the pinned immediate-removal window
	## is ZERO extra ticks. Granting corpse slack here would let an entity linger past
	## its authored lifetime undetected, which is exactly the regression this check owns.
	##
	## NON-FADED (e.g. Crebain / WildCaveBats — object/neutral/neutralunits.ini:2483-2486
	## authors LifetimeUpdate with NO DeathType row, and :2514-2517 is a single
	## SlowDeathBehavior DeathTypes = ALL): the ordinary corpse path applies and removal
	## legitimately happens a corpse lifetime later. That is a SEPARATE contract.
	##
	## NAMED COMPILER GAP (why this window is 0 and not the per-object fade):
	## retail's authored FADED fade would make the true window that object's own
	## SlowDeathBehavior DestructionDelay (1000..10000 ms above). That value is NOT
	## carried in the compiled pack. In the mounted elves spellbook the only
	## destruction delay present is the EGG's hatch delay
	## (/registration/leaves/objects/22/hatch/destructionDelayMs = 2000); the summoned
	## leaf TomBombadil_Summoned carries `lifetime` but no slow-death payload, and
	## SlowDeathBehavior is not even listed in its `unconvertedBehaviors`. So a
	## per-object window cannot be derived here without a compiler change, and the
	## sim erases a FADED row in the tick its lifetime ends. 0 is therefore the
	## TIGHTEST window that is true of the current engine: it can never let a
	## lingering row pass. If the compiler later emits the authored fade, this pin
	## should become that per-object delay and this comment should go away.
	const FADED_IMMEDIATE_REMOVAL_TICKS := 0
	if longest_lifetime <= 0:
		_check(faction_id, "summon_faded_expiry_" + power_id, false, "no authored lifetime")
		_check(faction_id, "summon_corpse_expiry_" + power_id, false, "no authored lifetime")
		return
	_advance_effect_lifecycle(sim, longest_lifetime + FADED_IMMEDIATE_REMOVAL_TICKS)
	# A FADED-authored row can still leave a corpse LEGITIMATELY — but only by
	# dying of something else FIRST. Retail's `NONE +FADED` SlowDeathBehavior is
	# selected by the death type actually dealt, so a summon killed in combat at
	# t < lifetime takes the ordinary corpse path (e.g. watcher.ini:179-183
	# `DeathTypes = ALL -FADED`). Distinguish the two by the tick the row died on,
	# recovered from its corpse schedule; anything that was still owed a FADED
	# expiry and is STILL PRESENT is the lingering-past-lifetime regression.
	var faded_retained: Array[int] = []
	var faded_killed_early: Array[int] = []
	for entity_id in faded_ids:
		if not (entity_id in sim.entity_ids()):
			continue
		var row_after: Dictionary = sim.entity(entity_id)
		var despawn_tick := int(despawn_tick_by_id.get(entity_id, -1))
		var death_tick := int(row_after.get("corpse_expire_tick", -1)) - SimScript.CORPSE_LIFETIME_TICKS
		if (
			int(row_after.get("health", 1)) <= 0
			and despawn_tick >= 0
			and death_tick >= 0
			and death_tick < despawn_tick
		):
			# Killed before its FADED expiry was ever due: corpse contract, and the
			# corpse assertion below now owns cleaning it up.
			faded_killed_early.append(entity_id)
			corpse_ids.append(entity_id)
			continue
		faded_retained.append(entity_id)
	_check(
		faction_id,
		"summon_faded_expiry_" + power_id,
		faded_retained.is_empty(),
		"faded=%s retained=%s killed_before_expiry=%s lifetime_ticks=%d removal_window_ticks=%d" % [
			faded_ids, faded_retained, faded_killed_early, longest_lifetime,
			FADED_IMMEDIATE_REMOVAL_TICKS
		]
	)
	var survivors: Array[int] = []
	var all_expired_to_death := true
	for entity_id in spawned_ids:
		if entity_id in sim.entity_ids():
			survivors.append(entity_id)
			all_expired_to_death = all_expired_to_death and int(sim.entity(entity_id).get("health", 1)) == 0
	_check(
		faction_id,
		"summon_lifetime_" + power_id,
		all_expired_to_death,
		"survivors=%s lifetime_ticks=%d" % [survivors, longest_lifetime]
	)
	var dead_aura_sources_erased := true
	for entity_id in aura_sources:
		dead_aura_sources_erased = dead_aura_sources_erased and not sim._summon_aura_source_ids.has(entity_id)
	_check(
		faction_id, "summon_aura_registry_erased_" + power_id,
		dead_aura_sources_erased, "aura_sources=%s registry=%s" % [
			aura_sources, sim._summon_aura_source_ids.keys()
		]
	)
	# Separate contract: non-FADED expiries keep a readable corpse and are cleaned
	# by the ordinary corpse scheduler one CORPSE_LIFETIME_TICKS later. This slack
	# is deliberately NOT extended to the FADED set asserted above.
	_advance_effect_lifecycle(sim, SimScript.CORPSE_LIFETIME_TICKS)
	var corpse_retained: Array[int] = []
	for entity_id in corpse_ids:
		if entity_id in sim.entity_ids():
			corpse_retained.append(entity_id)
	_check(
		faction_id,
		"summon_corpse_expiry_" + power_id,
		corpse_retained.is_empty(),
		"corpse=%s retained=%s corpse_ticks=%d" % [
			corpse_ids, corpse_retained, SimScript.CORPSE_LIFETIME_TICKS
		]
	)


func _assert_layered_summon_effect(faction_id: String, power_id: String, row: Dictionary) -> void:
	var key := "%s/%s" % [faction_id, power_id]
	var expected: Dictionary = LAYERED_SUMMON_EXPECTATIONS.get(key, {}) as Dictionary
	if expected.is_empty():
		return
	var effect: Dictionary = row.get("effect", {}) as Dictionary
	if key == "isengard/SpellBookCrebain":
		_assert_real_selected_crebain_aura(effect)
	var actual_objects: Dictionary = {}
	var actual_lifetimes_ms: Dictionary = {}
	var actual_death_types: Dictionary = {}
	var actual_pool: Array = []
	var actual_pool_count := 0
	var groups: Array = effect.get("target_groups", []) as Array
	if groups.is_empty():
		groups = [{"pick_count": 0, "choices": effect.get("targets", [])}]
	for group_value in groups:
		var group := group_value as Dictionary
		var choices: Array = group.get("choices", []) as Array
		var pick_count := int(group.get("pick_count", 0))
		if choices.size() > 1:
			actual_pool_count += pick_count
		for target_value in choices:
			var target := target_value as Dictionary
			var object_id := String(target.get("object_id", ""))
			if choices.size() > 1:
				actual_pool.append(object_id)
			else:
				actual_objects[object_id] = int(actual_objects.get(object_id, 0)) + pick_count
			actual_lifetimes_ms[object_id] = roundi(float(target.get("lifetime_ticks", 0)) * SimScript.TICK_SECONDS * 1000.0)
			actual_death_types[object_id] = String(target.get("lifetime_death_type", "")).to_upper()
	actual_pool.sort()
	var expected_pool: Array = (expected.get("object_pool", []) as Array).duplicate()
	expected_pool.sort()
	var actual_hatch_delay_ms := roundi(float(effect.get("hatch_delay_ticks", 0)) * SimScript.TICK_SECONDS * 1000.0)
	var tier := int(expected.get("cooldown_tier", 0))
	var matches: bool = (
		actual_objects == expected.get("objects", {})
		and actual_pool == expected_pool
		and actual_pool_count == int(expected.get("pool_pick_count", 0))
		and actual_lifetimes_ms == expected.get("lifetimes_ms", {})
		and actual_death_types == expected.get("death_types", {})
		and actual_hatch_delay_ms == int(expected.get("hatch_delay_ms", 0))
		and int(row.get("cost", -1)) == int(expected.get("cost", -2))
		and roundi(float(row.get("reload_ms", 0.0))) == int(COOLDOWN_MS_BY_TIER.get(tier, -1))
	)
	row["_literal_assert_active"] = matches or String(expected.get("status", "")) == "assert"
	if String(expected.get("status", "")) == "pending-rebuild" and not matches:
		_exclude_pending(faction_id, "pending_rebuild_mismatch_" + power_id, "objects=%s pool=%s pool_count=%d lifetimes=%s death_types=%s hatch_ms=%d expected=%s" % [actual_objects, actual_pool, actual_pool_count, actual_lifetimes_ms, actual_death_types, actual_hatch_delay_ms, expected])
		_check(faction_id, "layered_cost_cooldown_" + power_id, int(row.get("cost", -1)) == int(expected.get("cost", -2)) and roundi(float(row.get("reload_ms", 0.0))) == int(COOLDOWN_MS_BY_TIER.get(tier, -1)), "cost=%s reload_ms=%s expected=%s" % [row.get("cost"), row.get("reload_ms"), expected])
	else:
		_check(faction_id, "layered_effect_" + power_id, matches, "objects=%s pool=%s pool_count=%d lifetimes=%s death_types=%s hatch_ms=%d expected=%s" % [actual_objects, actual_pool, actual_pool_count, actual_lifetimes_ms, actual_death_types, actual_hatch_delay_ms, expected])
		_check(faction_id, "layered_cost_cooldown_" + power_id, int(row.get("cost", -1)) == int(expected.get("cost", -2)) and roundi(float(row.get("reload_ms", 0.0))) == int(COOLDOWN_MS_BY_TIER.get(tier, -1)), "cost=%s reload_ms=%s expected=%s" % [row.get("cost"), row.get("reload_ms"), expected])


func _assert_terrain_reveal_effect(faction_id: String, power_id: String, row: Dictionary) -> void:
	## Static half of the terrain/reveal contract: the compiled effect must equal
	## the literals pinned above, and a power named as blocked must still be
	## fail-closed with EXACTLY its named reason (so a future change that quietly
	## unlocks or re-locks it cannot slip through as a pass).
	var key := "%s/%s" % [faction_id, power_id]
	if TERRAIN_REVEAL_BLOCKED.has(key):
		_check(
			faction_id, "terrain_reveal_blocked_" + power_id,
			not bool(row.get("castable", false))
			and String(row.get("locked_reason", "")) == String(TERRAIN_REVEAL_BLOCKED[key]),
			"castable=%s reason=%s" % [row.get("castable"), row.get("locked_reason")]
		)
		return
	var cost_only: Dictionary = REVEAL_COST_ONLY_EXPECTATIONS.get(key, {}) as Dictionary
	if not cost_only.is_empty():
		_check(
			faction_id, "terrain_reveal_cost_cooldown_" + power_id,
			_cost_cooldown_matches(row, cost_only),
			"cost=%s reload_ms=%s cursor=%s expected=%s" % [
				row.get("cost"), row.get("reload_ms"), row.get("radius_cursor_source"), cost_only
			]
		)
		return
	var expected: Dictionary = TERRAIN_REVEAL_EXPECTATIONS.get(key, {}) as Dictionary
	if expected.is_empty():
		return
	_check(
		faction_id, "terrain_reveal_cost_cooldown_" + power_id,
		_cost_cooldown_matches(row, expected),
		"cost=%s reload_ms=%s cursor=%s expected=%s" % [
			row.get("cost"), row.get("reload_ms"), row.get("radius_cursor_source"), expected
		]
	)
	var effect: Dictionary = row.get("effect", {}) as Dictionary
	var kind := String(expected.get("kind", ""))
	if String(effect.get("kind", "")) != kind:
		_check(faction_id, "terrain_reveal_effect_" + power_id, false,
			"kind=%s expected=%s locked=%s" % [effect.get("kind"), kind, row.get("locked_reason")])
		return
	if kind == "grove_aura":
		var ok := (
			String(effect.get("terrain_object_id", "")) == String(expected.get("terrain_object_id", ""))
			and String(effect.get("terrain_condition", "")) == String(expected.get("terrain_condition", ""))
			and String(effect.get("modifier", "")) == String(expected.get("modifier", ""))
			and is_equal_approx(float(effect.get("range_source", 0.0)), float(expected.get("range_source", 0.0)))
			and is_equal_approx(float(effect.get("armor_mult", 0.0)), float(expected.get("armor_mult", 0.0)))
			and is_equal_approx(float(effect.get("damage_mult", 0.0)), float(expected.get("damage_mult", 0.0)))
			and _ticks_to_ms(int(effect.get("lifetime_ticks", 0))) == int(expected.get("lifetime_ms", 0))
			and _ticks_to_ms(int(effect.get("buff_duration_ticks", 0))) == int(expected.get("buff_duration_ms", 0))
		)
		if expected.has("authored_rows"):
			ok = ok and (effect.get("authored_rows", {}) as Dictionary) == (
				expected.get("authored_rows", {}) as Dictionary)
		if expected.has("unsupported_modifier_rows"):
			ok = ok and Array(effect.get("unsupported_modifier_rows", [])).size() == int(
				expected.get("unsupported_modifier_rows", 0))
		_check(faction_id, "terrain_reveal_effect_" + power_id, ok,
			"object=%s condition=%s modifier=%s range=%s armor=%s damage=%s lifetime_ms=%d buff_ms=%d authored=%s residuals=%s expected=%s" % [
				effect.get("terrain_object_id"), effect.get("terrain_condition"), effect.get("modifier"),
				effect.get("range_source"), effect.get("armor_mult"), effect.get("damage_mult"),
				_ticks_to_ms(int(effect.get("lifetime_ticks", 0))),
				_ticks_to_ms(int(effect.get("buff_duration_ticks", 0))),
				effect.get("authored_rows"),
				Array(effect.get("unsupported_modifier_rows", [])), expected])
		return
	# field_ping
	var actual_auras: Array = []
	for aura_value in Array(effect.get("auras", [])):
		var aura := aura_value as Dictionary
		var modifiers: Dictionary = {}
		for modifier_value in Array(aura.get("modifiers", [])):
			var modifier := modifier_value as Dictionary
			modifiers[String(modifier.get("kind", ""))] = float(modifier.get("value", 0.0))
		actual_auras.append({
			"id": String(aura.get("id", "")),
			"category": String(aura.get("category", "")),
			"range_source": float(aura.get("range_source", 0.0)),
			"refresh_ms": _ticks_to_ms(int(aura.get("refresh_ticks", 0))),
			"duration_ms": _ticks_to_ms(int(aura.get("duration_ticks", 0))),
			"modifiers": modifiers,
			"target_enemy": aura.get("target_enemy", null),
		})
	var actual_invisibility: Array = []
	for policy_value in Array(effect.get("invisibility_updates", [])):
		var policy := policy_value as Dictionary
		actual_invisibility.append({
			"enabled": bool(policy.get("enabled", false)),
			"update_ms": _ticks_to_ms(int(policy.get("update_ticks", 0))),
			"broadcast": bool(policy.get("broadcast", false)),
			"broadcast_range_source": float(policy.get("broadcast_range_source", -1.0)),
			"detection_range_source": float(policy.get("detection_range_source", -1.0)),
			"invisibility_type": String(policy.get("invisibility_type", "")),
			"broadcast_filter": " ".join(Array(policy.get("broadcast_filter", []))),
			"source_ini": String(policy.get("source_ini", "")),
			"line": int(policy.get("line", 0)),
		})
	var ping_ok := (
		String(effect.get("object_id", "")) == String(expected.get("object_id", ""))
		and _ticks_to_ms(int(effect.get("lifetime_ticks", 0))) == int(expected.get("lifetime_ms", 0))
		and is_equal_approx(float(effect.get("reveal_radius_source", -1.0)), float(expected.get("reveal_radius_source", -2.0)))
		and Array(effect.get("unconverted_behaviors", [])) == Array(expected.get("unconverted_behaviors", []))
		and actual_auras == Array(expected.get("auras", []))
		and actual_invisibility == Array(expected.get("invisibility_updates", []))
	)
	_check(faction_id, "terrain_reveal_effect_" + power_id, ping_ok,
		"object=%s lifetime_ms=%d reveal=%s unconverted=%s auras=%s invisibility=%s expected=%s" % [
			effect.get("object_id"), _ticks_to_ms(int(effect.get("lifetime_ticks", 0))),
			effect.get("reveal_radius_source"), effect.get("unconverted_behaviors"),
			actual_auras, actual_invisibility, expected])


func _assert_terrain_reveal_runtime(sim, faction_id: String, power_id: String, row: Dictionary, cast_point: Vector2) -> void:
	## Live half: the cast must have registered the field state with the authored
	## parameters, and the authored lifetime must remove it. Never asserts the
	## effect dictionary back at itself — every number comes from the literal
	## table, so a wrong compiled value fails here too.
	var expected: Dictionary = TERRAIN_REVEAL_EXPECTATIONS.get("%s/%s" % [faction_id, power_id], {}) as Dictionary
	var lifetime_ticks := int(float(expected.get("lifetime_ms", 0)) / 1000.0 / SimScript.TICK_SECONDS)
	var scale: float = sim._spellbook_world_scale()
	if String(expected.get("kind", "")) == "grove_aura":
		var matched: Dictionary = {}
		for grove_value in sim._active_groves:
			var grove := grove_value as Dictionary
			if String(grove.get("terrain_condition", "")) == String(expected.get("terrain_condition", "")):
				matched = grove
		var registered: bool = (
			not matched.is_empty()
			and int(matched.get("team", -1)) == 0
			and Vector2(matched.get("point", Vector2.ZERO)) == cast_point
			and is_equal_approx(float(matched.get("range_sim", 0.0)), float(expected.get("range_source", 0.0)) * scale)
			and is_equal_approx(float(matched.get("armor_mult", 0.0)), float(expected.get("armor_mult", 0.0)))
			and is_equal_approx(float(matched.get("damage_mult", 0.0)), float(expected.get("damage_mult", 0.0)))
			and int(matched.get("despawn_tick", -1)) == sim.tick_index + lifetime_ticks
		)
		_check(faction_id, "terrain_taint_registered_" + power_id, registered,
			"grove=%s tick=%d lifetime_ticks=%d" % [matched, sim.tick_index, lifetime_ticks])
		# The buff must actually land on a covered friendly battalion. The taint is
		# a persistent field, so standing an eligible battalion in it after the
		# cast is exactly the retail case (units walk onto tainted ground).
		var covered_id := _place_filter_eligible_ally(sim, cast_point)
		_advance_effect_lifecycle(sim, 1)
		var buffed := false
		for id in sim.living_ids(0):
			var unit: Dictionary = sim.entity(id)
			if Vector2(unit.get("position", Vector2.ZERO)).distance_to(cast_point) > float(expected.get("range_source", 0.0)) * scale:
				continue
			# The taint writes into the SHARED timed-modifier table, so this also
			# proves the routing: same key, same accumulator as Darkness and every
			# hero aura, rather than a private pair of row fields.
			# Key is "taint:" + the AUTHORED ModifierList id from the literal table,
			# not a hardcoded "GenericBuff": men's grove binds GenericArmorLeadership
			# and the RotWK groves bind GenericBuff, and a shared key would have made
			# two different authored lists collide in one accumulator slot.
			var taint: Dictionary = Dictionary(unit.get("timed_modifiers", {})).get(
				"taint:" + String(expected.get("modifier", "")), {}
			) as Dictionary
			if taint.is_empty() or int(taint.get("expires_tick", -1)) <= sim.tick_index:
				continue
			var armor := 0.0
			var damage := 0.0
			for modifier_value in Array(taint.get("modifiers", [])):
				var modifier := modifier_value as Dictionary
				if String(modifier.get("kind", "")) == "ARMOR":
					armor = float(modifier.get("value", 0.0))
				elif String(modifier.get("kind", "")) == "DAMAGE_MULT":
					damage = float(modifier.get("value", 0.0))
			if is_equal_approx(armor, float(expected.get("armor_mult", 0.0))) and is_equal_approx(damage, float(expected.get("damage_mult", 0.0))):
				buffed = true
		_check(faction_id, "terrain_taint_buffs_covered_ally_" + power_id, buffed,
			"no covered friendly battalion carried the authored ARMOR+DAMAGE_MULT window; eligible_id=%d roster=%s" % [
				covered_id, _ally_roster(sim)])
		_advance_effect_lifecycle(sim, lifetime_ticks)
		var still_live := false
		for grove_value in sim._active_groves:
			if String((grove_value as Dictionary).get("terrain_condition", "")) == String(expected.get("terrain_condition", "")):
				still_live = true
		_check(faction_id, "terrain_taint_expires_" + power_id, not still_live,
			"lifetime_ticks=%d groves=%d" % [lifetime_ticks, sim._active_groves.size()])
		return
	# field_ping
	var ping: Dictionary = {}
	for ping_value in sim._field_pings:
		if String((ping_value as Dictionary).get("object_id", "")) == String(expected.get("object_id", "")):
			ping = ping_value as Dictionary
	var reveal_source := float(expected.get("reveal_radius_source", 0.0))
	var ping_ok: bool = (
		not ping.is_empty()
		and int(ping.get("team", -1)) == 0
		and Vector2(ping.get("point", Vector2.ZERO)) == cast_point
		and is_equal_approx(float(ping.get("reveal_radius_source", -1.0)), reveal_source)
		and is_equal_approx(float(ping.get("reveal_radius_sim", -1.0)), reveal_source * scale)
		and int(ping.get("expire_tick", -1)) == sim.tick_index + lifetime_ticks
	)
	_check(faction_id, "field_ping_registered_" + power_id, ping_ok,
		"ping=%s tick=%d lifetime_ticks=%d" % [ping, sim.tick_index, lifetime_ticks])
	# The reveal registry is the presentation contract: a ping with an authored
	# VisionRange publishes exactly one region; a pure-aura ping (Frozen Land,
	# VisionRange 0.0) must publish NONE.
	var regions: Array = sim.team_revealed_regions(0)
	var mine: Array = []
	for region_value in regions:
		if String((region_value as Dictionary).get("power_id", "")) == power_id:
			mine.append(region_value)
	if reveal_source > 0.0:
		var region: Dictionary = (mine[0] as Dictionary) if not mine.is_empty() else {}
		_check(faction_id, "field_ping_reveal_region_" + power_id,
			mine.size() == 1
			and Vector2(region.get("point", Vector2.ZERO)) == cast_point
			and is_equal_approx(float(region.get("radius_source", -1.0)), reveal_source)
			and is_equal_approx(float(region.get("radius_sim", -1.0)), reveal_source * scale)
			and int(region.get("expire_tick", -1)) == sim.tick_index + lifetime_ticks,
			"regions=%s expected_radius=%s" % [mine, reveal_source])
	else:
		_check(faction_id, "field_ping_reveal_region_" + power_id, mine.is_empty(),
			"a VisionRange 0.0 ping must publish no reveal region; got %s" % [mine])
	# The registry is the presentation contract, so it has to survive the same
	# things the rest of the sim does: a save/load (or an MP resync) taken
	# WHILE a reveal is live, and a query from a team that did not cast it.
	# None of that was proved before - the reveal lane was only ever asserted
	# on the casting team, in one continuous run.
	var hash_before := String(sim.state_hash())
	var regions_before := str(sim.team_revealed_regions(0))
	var ping_count_before := int(sim.field_ping_count())
	var bytes: PackedByteArray = sim.snapshot()
	var restored := bool(sim.restore(bytes))
	_check(faction_id, "field_ping_snapshot_round_trip_" + power_id,
		restored
			and String(sim.state_hash()) == hash_before
			and str(sim.team_revealed_regions(0)) == regions_before
			and int(sim.field_ping_count()) == ping_count_before,
		"restored=%s hash_before=%s hash_after=%s regions_before=%s regions_after=%s pings=%d/%d" % [
			restored, hash_before, sim.state_hash(), regions_before,
			sim.team_revealed_regions(0), ping_count_before, sim.field_ping_count()])
	# Per-team registry: an opponent must not inherit the caster reveal.
	# CAVEAT, and the reason for the extra check below: for a VisionRange 0.0 ping
	# (Frozen Land) team 0 publishes nothing either, so this assertion is VACUOUS
	# there — it passes for the wrong reason and would keep passing if the reveal
	# lane broke entirely. The intended negative for that case is "no region for
	# ANY team", asserted explicitly rather than implied.
	_check(faction_id, "field_ping_reveal_team_isolated_" + power_id,
		sim.team_revealed_regions(1).is_empty(),
		"team 1 must see none of team 0 reveal; got %s" % [sim.team_revealed_regions(1)])
	if reveal_source <= 0.0:
		_check(faction_id, "field_ping_no_reveal_for_any_team_" + power_id,
			sim.team_revealed_regions(0).is_empty()
				and sim.team_revealed_regions(1).is_empty(),
			"a VisionRange 0.0 ping must publish no region for the caster OR the opponent; team0=%s team1=%s" % [
				sim.team_revealed_regions(0), sim.team_revealed_regions(1)])
	else:
		# The positive twin, so the pair has content in both directions: the
		# casting team DOES hold a region at this point, and only the opponent is
		# empty. Without it, "team 1 is empty" proves nothing about isolation.
		_check(faction_id, "field_ping_reveal_present_for_caster_" + power_id,
			not sim.team_revealed_regions(0).is_empty(),
			"caster must still hold its reveal when the opponent is checked; team0=%s" % [
				sim.team_revealed_regions(0)])
	# Stat-bearing auras must land on a covered unit of the authored relation.
	var camouflage_id := 0
	if not Array(expected.get("invisibility_updates", [])).is_empty():
		camouflage_id = _place_filter_eligible_ally(sim, cast_point)
	var stat_auras: Array = []
	for aura_value in Array(expected.get("auras", [])):
		if not (aura_value as Dictionary).get("modifiers", {}).is_empty():
			stat_auras.append(aura_value)
	if not stat_auras.is_empty():
		var aura: Dictionary = stat_auras[0] as Dictionary
		var refresh_ticks := maxi(1, int(float(aura.get("refresh_ms", 0)) / 1000.0 / SimScript.TICK_SECONDS))
		var target_team := 1 if bool(aura.get("target_enemy", false)) else 0
		var covered_id := _place_filter_eligible_target(sim, target_team, cast_point)
		_advance_effect_lifecycle(sim, refresh_ticks * 2)
		var applied := false
		for id in sim.living_ids(target_team):
			var unit: Dictionary = sim.entity(id)
			if Vector2(unit.get("position", Vector2.ZERO)).distance_to(cast_point) > float(aura.get("range_source", 0.0)) * scale:
				continue
			for key_value in Dictionary(unit.get("timed_modifiers", {})).keys():
				if String(key_value).begins_with("field-ping:%s:%s" % [String(expected.get("object_id", "")), String(aura.get("id", ""))]):
					applied = true
		_check(faction_id, "field_ping_aura_applies_" + power_id, applied,
			"no covered team %d unit carried the authored %s window; eligible_id=%d roster=%s" % [
				target_team, String(aura.get("id", "")), covered_id, _team_roster(sim, target_team)])
	if camouflage_id != 0:
		_check(faction_id, "field_ping_camouflage_applies_" + power_id,
			sim._stealth_active(sim.entity(camouflage_id)),
			"eligible_id=%d entity=%s" % [camouflage_id, sim.entity(camouflage_id)])
	_advance_effect_lifecycle(sim, lifetime_ticks)
	var survivor := false
	for ping_value in sim._field_pings:
		if String((ping_value as Dictionary).get("object_id", "")) == String(expected.get("object_id", "")):
			survivor = true
	_check(faction_id, "field_ping_expires_" + power_id, not survivor,
		"lifetime_ticks=%d pings=%d" % [lifetime_ticks, sim._field_pings.size()])
	# REPLAY CONTINUATION: this sim was snapshotted and restored mid-reveal
	# above, so reaching the authored expiry here proves the restored state
	# keeps ticking the lifetime down rather than freezing the reveal on.
	_check(faction_id, "field_ping_reveal_region_cleared_" + power_id,
		sim.team_revealed_regions(0).is_empty(),
		"regions=%s" % [sim.team_revealed_regions(0)])
	if camouflage_id != 0:
		_check(faction_id, "field_ping_camouflage_revoked_" + power_id,
			not sim._stealth_active(sim.entity(camouflage_id)),
			"eligible_id=%d entity=%s" % [camouflage_id, sim.entity(camouflage_id)])


func _assert_weather_allegiance_effect(faction_id: String, power_id: String, row: Dictionary) -> void:
	## Static half: cost, cooldown, cursor radius and the whole compiled effect
	## dictionary against the literal table above. A power that regresses to
	## locked fails here with its locked_reason rather than vanishing.
	var expected: Dictionary = WEATHER_ALLEGIANCE_EXPECTATIONS.get(
		"%s/%s" % [faction_id, power_id], {}
	) as Dictionary
	if expected.is_empty():
		return
	_check(
		faction_id, "weather_allegiance_cost_cooldown_" + power_id,
		_cost_cooldown_matches(row, expected),
		"cost=%s reload_ms=%s cursor=%s expected=%s" % [
			row.get("cost"), row.get("reload_ms"), row.get("radius_cursor_source"), expected
		]
	)
	var effect: Dictionary = row.get("effect", {}) as Dictionary
	var kind := String(expected.get("kind", ""))
	if String(effect.get("kind", "")) != kind:
		_check(faction_id, "weather_allegiance_effect_" + power_id, false,
			"kind=%s expected=%s locked=%s" % [effect.get("kind"), kind, row.get("locked_reason")])
		return
	match kind:
		"weather_modifier":
			var modifiers: Dictionary = {}
			for modifier_value in Array(effect.get("modifiers", [])):
				var modifier := modifier_value as Dictionary
				modifiers[String(modifier.get("kind", ""))] = float(modifier.get("value", 0.0))
			var ok := (
				String(effect.get("modifier_id", "")) == String(expected.get("modifier_id", ""))
				and String(effect.get("category", "")) == String(expected.get("category", ""))
				and modifiers == Dictionary(expected.get("modifiers", {}))
				and _ticks_to_ms(int(effect.get("duration_ticks", 0))) == int(expected.get("duration_ms", 0))
				and String(effect.get("weather", "")) == String(expected.get("weather", ""))
				and String(effect.get("affects", "")) == String(expected.get("affects", ""))
			)
			_check(faction_id, "weather_allegiance_effect_" + power_id, ok,
				"modifier=%s category=%s modifiers=%s duration_ms=%d weather=%s affects=%s expected=%s" % [
					effect.get("modifier_id"), effect.get("category"), modifiers,
					_ticks_to_ms(int(effect.get("duration_ticks", 0))), effect.get("weather"),
					effect.get("affects"), expected])
		"weather_anticategory":
			var ok_anti := (
				String(effect.get("anti_category", "")) == String(expected.get("anti_category", ""))
				and _ticks_to_ms(int(effect.get("duration_ticks", 0))) == int(expected.get("duration_ms", 0))
				and String(effect.get("weather", "")) == String(expected.get("weather", ""))
				and String(effect.get("affects", "")) == String(expected.get("affects", ""))
				and is_equal_approx(float(effect.get("burn_rate_modifier", 0.0)), float(expected.get("burn_rate_modifier", 1.0)))
				and is_equal_approx(float(effect.get("burn_decay_modifier", 0.0)), float(expected.get("burn_decay_modifier", -1.0)))
				and Array(effect.get("unconverted_behaviors", [])) == Array(expected.get("unconverted_behaviors", []))
			)
			_check(faction_id, "weather_allegiance_effect_" + power_id, ok_anti,
				"anti=%s duration_ms=%d weather=%s affects=%s burn=%s/%s unconverted=%s expected=%s" % [
					effect.get("anti_category"), _ticks_to_ms(int(effect.get("duration_ticks", 0))),
					effect.get("weather"), effect.get("affects"),
					effect.get("burn_rate_modifier"), effect.get("burn_decay_modifier"),
					effect.get("unconverted_behaviors"), expected])
		"creep_allegiance":
			var ok_creep := (
				is_equal_approx(float(effect.get("range_source", 0.0)), float(expected.get("range_source", -1.0)))
				and bool(effect.get("target_enemy", false)) == bool(expected.get("target_enemy", false))
				and Array(effect.get("lair_types", [])) == Array(expected.get("lair_types", []))
				and String(effect.get("filter", "")) == String(expected.get("filter", ""))
			)
			_check(faction_id, "weather_allegiance_effect_" + power_id, ok_creep,
				"range=%s target_enemy=%s lairs=%s filter=%s expected=%s" % [
					effect.get("range_source"), effect.get("target_enemy"),
					effect.get("lair_types"), effect.get("filter"), expected])


func _stage_weather_allegiance_targets(sim, row: Dictionary, cast_point: Vector2) -> Dictionary:
	## Stand up exactly the world state the effect under test needs, and nothing
	## else. Returns the ids the runtime assert will look for.
	var effect: Dictionary = row.get("effect", {}) as Dictionary
	match String(effect.get("kind", "")):
		"weather_anticategory":
			return {"enemy_id": _clone_battalion_onto_team(sim, 1, cast_point)}
		"creep_allegiance":
			return _seed_scenario_lairs_for_allegiance(sim, cast_point, float(effect.get("range_source", 0.0)))
	return {}


func _clone_battalion_onto_team(sim, team: int, point: Vector2) -> int:
	## The matrix fields only the caster's fortress roster. Clone one living
	## caster battalion onto another team so an ENEMIES-scoped effect has a
	## legitimate, fully-formed target row instead of a hand-built stub.
	# Prefer a row the aura-style filters actually accept: a fortress roster whose
	# first entry is a builder or a hero would be excluded by the authored
	# `-DOZER -HERO` terms, and cloning it would test nothing.
	var source_id := 0
	for entity_id in sim.living_ids(0):
		var candidate: Dictionary = sim.entity(entity_id)
		if bool(candidate.get("is_builder", false)) or String(candidate.get("category", "")) == "hero":
			continue
		source_id = entity_id
		break
	if source_id == 0:
		for entity_id in sim.living_ids(0):
			source_id = entity_id
			break
	if source_id == 0:
		return 0
	var clone: Dictionary = (sim.entity(source_id) as Dictionary).duplicate(true)
	var new_id := 900001
	while sim.entities.has(new_id):
		new_id += 1
	clone["id"] = new_id
	clone["team"] = team
	clone["position"] = point
	clone["target_id"] = 0
	sim.entities[new_id] = clone
	return new_id


func _seed_scenario_lairs_for_allegiance(sim, cast_point: Vector2, range_source: float) -> Dictionary:
	## Two lairs of authored types: one inside the power's radius (must defect,
	## with its guards) and one well outside it (must NOT). Both structures and
	## their children come from the selected RotWK scenario descriptors, so this
	## exercises the exact SpawnBehavior -> SlavedUpdate graph and faction
	## CommandSetUpgrade rather than the retired provisional creep table.
	var scale: float = sim._spellbook_world_scale()
	var radius := range_source * scale
	var far_point := cast_point + Vector2(radius * 5.0 + 10.0, 0.0)
	var content_db = root.get_node_or_null("ContentDB")
	if content_db == null:
		return {}
	# This fixture is the RotWK spellbook matrix. Install only the sealed neutral
	# scenario tables needed by this cast, after faction configuration, so the
	# neutral rows cannot enter ordinary production validation or HUD registries.
	sim._rules["game"] = "rotwk"
	for key in ["scenario_unit_runtimes", "scenario_structure_runtimes"]:
		sim._rules[key] = content_db.call("get_%s" % key, "rotwk")
	var near_id: int = sim.spawn_scenario_structure(
		"CaveTrollLair", SimScript.CREEP_TEAM, cast_point, "map-placement", 60001
	)
	var far_id: int = sim.spawn_scenario_structure(
		"SpiderLair", SimScript.CREEP_TEAM, far_point, "map-placement", 60002
	)
	# SpawnBehavior's InitialBurst is stepped by the sim, not hand-constructed.
	sim._step_spawn_behaviors()
	var guard_ids: Array = []
	if near_id != 0:
		guard_ids = (
			(sim.structure(near_id).get("spawn_behavior", {}) as Dictionary)
			.get("spawned_ids", []) as Array
		).duplicate()
	return {"near_lair_id": near_id, "far_lair_id": far_id, "near_guard_ids": guard_ids}


func _assert_weather_allegiance_runtime(sim, faction_id: String, power_id: String, row: Dictionary, cast_point: Vector2, staged: Dictionary) -> void:
	## Live half. Every number comes from the literal table, never from the
	## effect dictionary, so a wrong compiled value fails here too.
	var expected: Dictionary = WEATHER_ALLEGIANCE_EXPECTATIONS.get("%s/%s" % [faction_id, power_id], {}) as Dictionary
	var cast_tick: int = sim.tick_index
	var duration_ticks := int(float(expected.get("duration_ms", 0)) / 1000.0 / SimScript.TICK_SECONDS)
	match String(expected.get("kind", "")):
		"weather_modifier":
			_assert_weather_modifier_runtime(sim, faction_id, power_id, expected, cast_tick, duration_ticks)
		"weather_anticategory":
			_assert_weather_anticategory_runtime(sim, faction_id, power_id, expected, cast_tick, duration_ticks, staged)
		"creep_allegiance":
			_assert_creep_allegiance_runtime(sim, faction_id, power_id, expected, cast_point, staged)


func _weather_window(sim, power_id: String) -> Dictionary:
	for entry_value in sim.active_weather_effects():
		var entry := entry_value as Dictionary
		if String(entry.get("power_id", "")) == power_id:
			return entry
	return {}


func _assert_weather_modifier_runtime(sim, faction_id: String, power_id: String, expected: Dictionary, cast_tick: int, duration_ticks: int) -> void:
	var window := _weather_window(sim, power_id)
	_check(faction_id, "weather_window_registered_" + power_id,
		not window.is_empty()
		and int(window.get("team", -1)) == 0
		and String(window.get("weather", "")) == String(expected.get("weather", ""))
		and String(window.get("kind", "")) == "weather_modifier"
		and String(window.get("source_key", "")).begins_with("weather:0:%s:" % power_id)
		and int(window.get("expire_tick", -1)) == cast_tick + duration_ticks,
		"window=%s cast_tick=%d duration_ticks=%d" % [window, cast_tick, duration_ticks])
	var key := String(window.get("source_key", ""))
	# 1) the cast itself must land the authored rows on a living ally.
	var applied_at_cast := false
	for entity_id in sim.living_ids(0):
		if _timed_modifier_matches(sim.entity(entity_id), key, expected, cast_tick + duration_ticks):
			applied_at_cast = true
	_check(faction_id, "weather_modifier_applied_at_cast_" + power_id, applied_at_cast,
		"no living ally carried %s with the authored rows; roster=%s" % [key, _ally_roster(sim)])
	# 2) AttributeModifierWeatherBased = Yes means the window keeps covering the
	#    map: a battalion that appears AFTER the cast must be picked up too.
	var late_id := _clone_battalion_onto_team(sim, 0, cast_point_for_late())
	_advance_effect_lifecycle(sim, SimScript.ABILITY_AURA_INTERVAL_TICKS * 2)
	_check(faction_id, "weather_modifier_covers_late_arrival_" + power_id,
		late_id != 0 and _timed_modifier_matches(sim.entity(late_id), key, expected, cast_tick + duration_ticks),
		"late_id=%d modifiers=%s" % [late_id, sim.entity(late_id).get("timed_modifiers", {})])
	sim.entities.erase(late_id)
	# 3) the window lapses on the authored tick and takes the modifier with it.
	_advance_effect_lifecycle(sim, duration_ticks)
	var lapsed := true
	for entity_id in sim.living_ids(0):
		if Dictionary(sim.entity(entity_id).get("timed_modifiers", {})).has(key):
			lapsed = false
	_check(faction_id, "weather_modifier_expires_" + power_id,
		_weather_window(sim, power_id).is_empty() and lapsed,
		"window=%s lapsed=%s duration_ticks=%d" % [_weather_window(sim, power_id), lapsed, duration_ticks])


func cast_point_for_late() -> Vector2:
	return Vector2(10.0, 10.0)


func _timed_modifier_matches(entity: Dictionary, key: String, expected: Dictionary, expires_tick: int) -> bool:
	var table: Dictionary = entity.get("timed_modifiers", {}) as Dictionary
	if not table.has(key):
		return false
	var entry: Dictionary = table[key] as Dictionary
	if int(entry.get("expires_tick", -1)) != expires_tick:
		return false
	var rows: Dictionary = {}
	for modifier_value in Array(entry.get("modifiers", [])):
		var modifier := modifier_value as Dictionary
		rows[String(modifier.get("kind", ""))] = float(modifier.get("value", 0.0))
	return rows == Dictionary(expected.get("modifiers", {}))


func _assert_weather_anticategory_runtime(sim, faction_id: String, power_id: String, expected: Dictionary, cast_tick: int, duration_ticks: int, staged: Dictionary) -> void:
	var window := _weather_window(sim, power_id)
	_check(faction_id, "weather_window_registered_" + power_id,
		not window.is_empty()
		and int(window.get("team", -1)) == 0
		and String(window.get("weather", "")) == String(expected.get("weather", ""))
		and String(window.get("kind", "")) == "weather_anticategory"
		and int(window.get("expire_tick", -1)) == cast_tick + duration_ticks,
		"window=%s cast_tick=%d duration_ticks=%d" % [window, cast_tick, duration_ticks])
	var enemy_id := int(staged.get("enemy_id", 0))
	var enemy: Dictionary = sim.entity(enemy_id)
	_check(faction_id, "weather_anticategory_suppresses_enemy_" + power_id,
		enemy_id != 0
		and int(enemy.get("leadership_suppressed_until_tick", -1)) == cast_tick + duration_ticks,
		"enemy_id=%d suppressed_until=%s expected=%d" % [
			enemy_id, enemy.get("leadership_suppressed_until_tick"), cast_tick + duration_ticks])
	# ALLIES must be untouched: the authored filter is `ALL ENEMIES`.
	var ally_untouched := true
	for entity_id in sim.living_ids(0):
		if sim.entity(entity_id).has("leadership_suppressed_until_tick"):
			ally_untouched = false
	_check(faction_id, "weather_anticategory_spares_allies_" + power_id, ally_untouched,
		"an ALLIES row carried leadership suppression under an `ALL ENEMIES` filter; roster=%s" % [_ally_roster(sim)])
	_advance_effect_lifecycle(sim, duration_ticks)
	_check(faction_id, "weather_anticategory_expires_" + power_id,
		_weather_window(sim, power_id).is_empty()
		and not sim.entity(enemy_id).has("leadership_suppressed_until_tick"),
		"window=%s enemy=%s" % [_weather_window(sim, power_id), sim.entity(enemy_id).get("leadership_suppressed_until_tick")])
	sim.entities.erase(enemy_id)


func _assert_creep_allegiance_runtime(sim, faction_id: String, power_id: String, expected: Dictionary, cast_point: Vector2, staged: Dictionary) -> void:
	var near_id := int(staged.get("near_lair_id", 0))
	var far_id := int(staged.get("far_lair_id", 0))
	var guard_ids: Array = staged.get("near_guard_ids", []) as Array
	var near: Dictionary = sim.structure(near_id)
	var far: Dictionary = sim.structure(far_id)
	var faction_upgrade := "Upgrade_%sFaction" % String({
		"angmar": "Angmar", "dwarves": "Dwarf", "elves": "Elf",
		"isengard": "Isengard", "men": "Men", "mordor": "Mordor", "wild": "Wild",
	}.get(faction_id, ""))
	_check(faction_id, "creep_allegiance_lair_defects_" + power_id,
		near_id != 0
		and int(near.get("team", -1)) == 0
		and String(near.get("scenario_game", "")) == "rotwk"
		and String(near.get("source_object_id", "")) == "CaveTrollLair"
		and String(near.get("command_set_id", "")) == "NeutralTrollCaveCommandSet"
		and (near.get("completed_upgrades", []) as Array).has(faction_upgrade),
		"near_lair=%s" % [near])
	# The authored radius is the whole targeting rule: a lair outside it keeps
	# its creep owner. Without this the power would read as "convert every lair".
	_check(faction_id, "creep_allegiance_respects_radius_" + power_id,
		far_id != 0 and int(far.get("team", -1)) == SimScript.CREEP_TEAM
		and String(far.get("scenario_game", "")) == "rotwk"
		and String(far.get("command_set_id", "")) == "EmptyCommandSet"
		and not (far.get("completed_upgrades", []) as Array).has(faction_upgrade),
		"far_lair=%s radius_source=%s" % [far, expected.get("range_source")])
	var guards_ok := not guard_ids.is_empty()
	for guard_value in guard_ids:
		var guard: Dictionary = sim.entity(int(guard_value))
		guards_ok = (
			guards_ok
			and int(guard.get("team", -1)) == 0
			and int(guard.get("spawn_behavior_parent_id", 0)) == near_id
			and int((guard.get("slaved_update", {}) as Dictionary).get("master_id", 0)) == near_id
		)
	_check(faction_id, "creep_allegiance_guards_follow_lair_" + power_id, guards_ok,
		"guards=%s" % [guard_ids])
	# Allegiance changes ownership and the authored CommandSetUpgrade surface; it
	# does not rewrite or disable the independent SpawnBehavior contract.
	_advance_effect_lifecycle(sim, 60)
	var defected: Dictionary = sim.structure(near_id)
	var spawned_ids := (
		(defected.get("spawn_behavior", {}) as Dictionary).get("spawned_ids", []) as Array
	)
	var spawn_policy := defected.get("spawn_behavior", {}) as Dictionary
	_check(faction_id, "creep_allegiance_preserves_authored_spawn_behavior_" + power_id,
		spawned_ids == guard_ids
		and (spawn_policy.get("templates", []) as Array) == ["CaveTroll_Slaved"]
		and int(spawn_policy.get("replace_ticks", 0)) == 1200
		and int(defected.get("team", -1)) == 0,
		"defected_lair=%s" % [defected])


func _place_filter_eligible_ally(sim, point: Vector2) -> int:
	return _place_filter_eligible_target(sim, 0, point)


func _place_filter_eligible_target(sim, team: int, point: Vector2) -> int:
	## GENERIC_BUFF_RECIPIENT_OBJECT_FILTER (gamedata.ini:82) is
	## `ANY +INFANTRY +CAVALRY ... -DOZER -HERO -STRUCTURE ...`, so a fortress
	## roster whose first row is a builder or a hero can never carry the buff.
	## Stand the first row the authored filter actually accepts in the field.
	for entity_id in sim.living_ids(team):
		var entity: Dictionary = sim.entity(entity_id)
		if bool(entity.get("is_builder", false)):
			continue
		if String(entity.get("category", "")) == "hero":
			continue
		entity["position"] = point
		sim._spatial_sync(entity)
		return entity_id
	return 0


func _ally_roster(sim) -> Array:
	return _team_roster(sim, 0)


func _team_roster(sim, team: int) -> Array:
	var roster: Array = []
	for entity_id in sim.living_ids(team):
		var entity: Dictionary = sim.entity(entity_id)
		roster.append("%d:%s:%s%s" % [
			entity_id, String(entity.get("object_id", "")), String(entity.get("category", "")),
			":builder" if bool(entity.get("is_builder", false)) else "",
		])
	return roster


func _cost_cooldown_matches(row: Dictionary, expected: Dictionary) -> bool:
	# A row pins EITHER a tier (the define) OR a literal ms (retail authored the
	# number inline). Never both, and never a silent fallback between them.
	var expected_reload := -1
	if expected.has("cooldown_ms"):
		expected_reload = int(expected.get("cooldown_ms", -1))
	else:
		expected_reload = int(COOLDOWN_MS_BY_TIER.get(int(expected.get("cooldown_tier", 0)), -1))
	return (
		int(row.get("cost", -1)) == int(expected.get("cost", -2))
		and roundi(float(row.get("reload_ms", 0.0))) == expected_reload
		and is_equal_approx(float(row.get("radius_cursor_source", -1.0)), float(expected.get("cursor_radius", -2.0)))
	)


func _ticks_to_ms(ticks: int) -> int:
	return roundi(float(ticks) * SimScript.TICK_SECONDS * 1000.0)


func _assert_summon_policy_contracts() -> void:
	var sim = SimScript.new()
	_check("sim", "bombadil_default_aura_allies_only",
		sim._summon_aura_allows_relation({}, true)
		and not sim._summon_aura_allows_relation({}, false))
	var crebain := {"category": "DEBUFF", "target_enemy": true, "target_allies": null}
	_check("sim", "crebain_explicit_enemy_only",
		not sim._summon_aura_allows_relation(crebain, true)
		and sim._summon_aura_allows_relation(crebain, false))
	var faded := {
		"destroy_die": [{
			"owner_role": "object", "death_types": "NONE",
			"excluded_death_types": [], "included_death_types": ["FADED"],
		}],
	}
	_check("sim", "faded_prompt_removal_policy",
		sim._destroy_die_matches(faded, "object", "FADED")
		and not sim._destroy_die_matches(faded, "object", "NORMAL"))
	_check("sim", "corpse_retention_distinct_from_faded",
		not sim._destroy_die_matches({}, "object", "NORMAL"))
	var null_enemy_filter := {
		"category": "SPELL", "filter": "ANY ENEMIES", "target_enemy": null,
		"target_allies": false,
	}
	_check("sim", "null_target_enemy_uses_authored_enemies_filter",
		not sim._summon_aura_allows_relation(null_enemy_filter, true)
		and sim._summon_aura_allows_relation(null_enemy_filter, false))
	var gated_verdict: Dictionary = sim._spellbook_summon_aura_rules(
		{"auras": [{
			"modifier": "UpgradeGatedAura", "range": 100.0, "refreshDelayMs": 1000.0,
			"objectFilter": "ANY ALLIES", "startsActive": "No",
			"triggeredBy": ["Upgrade_ObjectLevel10"],
		}], "experienceLevelCreate": {"rank": 1}},
		{"UpgradeGatedAura": {"fields": [
			{"key": "Category", "value": "SPELL"},
			{"key": "Duration", "value": "1500"},
			{"key": "Modifier", "value": "ARMOR 50%"},
		]}}
	)
	var skipped_auras: Array = gated_verdict.get("skipped_auras", []) as Array
	_check("sim", "upgrade_gated_aura_skip_keeps_summon_convertible",
		bool(gated_verdict.get("ok", false))
		and (gated_verdict.get("auras", []) as Array).is_empty()
		and skipped_auras.size() == 1
		and String((skipped_auras[0] as Dictionary).get("reason", "")).begins_with(
			"upgrade-gated-aura-inert-at-summon-creation:"
		), str(gated_verdict))


func _assert_modifier_row_contracts() -> void:
	## Round-16 hygiene contracts. Four independent regressions live here:
	##   1. the TAB/space separator, which silently dropped authored rows;
	##   2. the terrain taint riding the SHARED modifier accumulator, so it
	##      composes additively with Darkness and respects ABILITY_ARMOR_CAP;
	##   3. leadership suppression EXTENDING rather than clobbering;
	##   4. ALL being universal only in a relation-only filter.
	var sim = SimScript.new()

	# --- 1. separator regression, at the shared parser ---------------------
	# attributemodifier.ini:74 authors ARMOR + TAB + 50% and :75 authors
	# DAMAGE_MULT + SPACE + 150% on the SAME leaf. Both spellings must land on
	# byte-identical parse results, or half the buff goes missing in silence.
	var tabbed: Dictionary = sim._parse_modifier_row("ARMOR\t50%")
	var spaced: Dictionary = sim._parse_modifier_row("ARMOR 50%")
	_check("sim", "modifier_row_tab_and_space_parse_identically",
		tabbed == spaced
			and bool(tabbed.get("ok", false))
			and String(tabbed.get("kind", "")) == "ARMOR"
			and is_equal_approx(float(tabbed.get("value", 0.0)), 0.5),
		"tabbed=%s spaced=%s" % [tabbed, spaced])
	# FAIL-CLOSED. A defaulted or skipped row applies a FRACTION of the
	# authored buff and still reads as a working ability.
	_check("sim", "modifier_row_fails_closed_on_unreadable",
		not bool(sim._parse_modifier_row("ARMOR fifty%").get("ok", false))
			and not bool(sim._parse_modifier_row("ARMOR SOME_UNRESOLVED_DEFINE").get("ok", false))
			and not bool(sim._parse_modifier_row("").get("ok", false)),
		"non-numeric=%s unresolved-define=%s empty=%s" % [
			sim._parse_modifier_row("ARMOR fifty%"),
			sim._parse_modifier_row("ARMOR SOME_UNRESOLVED_DEFINE"),
			sim._parse_modifier_row("")])

	# --- 1a. one contract per shape found in the PURE RETAIL census -----------
	# Census over .private/retail-work/editions/rotwk/cache/effective-assets
	# (all .ini/.inc, comments stripped): 894 `Modifier =` rows, KIND_PCT 386,
	# KIND_PLAIN 363, MULTI 145, BARE_FLAG **0**.
	#
	# KIND_PLAIN carries an ABSOLUTE magnitude (attributemodifier.ini:704
	# `HEALTH 400`), so it must NOT come back as a 4.0 multiplier.
	var plain_row: Dictionary = sim._parse_modifier_row("HEALTH 400")
	_check("sim", "modifier_row_plain_is_absolute_not_percent",
		bool(plain_row.get("ok", false)) and bool(plain_row.get("supported", false))
			and String(plain_row.get("shape", "")) == "plain"
			and is_equal_approx(float(plain_row.get("value", 0.0)), 400.0),
		str(plain_row))
	# A bare token is NOT a flag row: retail authors zero of them. The old
	# `INVULNERABLE` flag branch was invented from the token's appearance, and
	# nothing in the corpus produced it. One token now fails closed.
	var bare_row: Dictionary = sim._parse_modifier_row("INVULNERABLE")
	_check("sim", "modifier_row_bare_token_is_unreadable",
		not bool(bare_row.get("ok", false)), str(bare_row))
	# MULTI, shape 1: a damage-type scope list (attributemodifier.ini:1079, the
	# row that used to lock angmar/SpellBookSnowbind). READ, not supported.
	var scoped_row: Dictionary = sim._parse_modifier_row(
		"INVULNERABLE 0% SLASH PIERCE SPECIALIST CRUSH CAVALRY SIEGE FLAME MAGIC FROST HERO HERO_RANGED STRUCTURAL URUK CAVALRY_RANGED CHOP")
	_check("sim", "modifier_row_scoped_is_read_but_named_unsupported",
		bool(scoped_row.get("ok", false))
			and not bool(scoped_row.get("supported", false))
			and String(scoped_row.get("shape", "")) == "percent_scoped"
			and String(scoped_row.get("kind", "")) == "INVULNERABLE"
			and Array(scoped_row.get("scope", [])).size() == 15
			and String(scoped_row.get("reason", "")) != "",
		str(scoped_row))
	# MULTI, shape 2: `PRODUCTION <TOKEN>  %` (attributemodifier.ini:1406) — the
	# percent sign drifts off its magnitude. Reattached, then judged: the token
	# here is unresolved, so the row is unreadable, NOT a silent 0.
	var detached_unresolved: Dictionary = sim._parse_modifier_row("PRODUCTION ROHAN_FARM_LVL2_PRODUCTION  %")
	var detached_resolved: Dictionary = sim._parse_modifier_row("PRODUCTION 125  %")
	_check("sim", "modifier_row_detached_percent_reattaches",
		not bool(detached_unresolved.get("ok", false))
			and bool(detached_resolved.get("ok", false))
			and bool(detached_resolved.get("supported", false))
			and String(detached_resolved.get("shape", "")) == "percent"
			and is_equal_approx(float(detached_resolved.get("value", 0.0)), 1.25),
		"unresolved=%s resolved=%s" % [detached_unresolved, detached_resolved])
	# MULTI, shape 3: an unevaluated INI expression
	# (attributemodifier.ini:3528). READ, named, not supported.
	var expr_row: Dictionary = sim._parse_modifier_row(
		"DAMAGE_MULT #MULTIPLY( CREATE_A_HERO_ATTRIBUTE_MULTIPLIER 0.60 )")
	_check("sim", "modifier_row_expression_is_read_but_named_unsupported",
		bool(expr_row.get("ok", false))
			and not bool(expr_row.get("supported", false))
			and String(expr_row.get("shape", "")) == "expression"
			and String(expr_row.get("reason", "")) != "",
		str(expr_row))
	# MULTI, shape 4: a READABLE magnitude carrying a damage-type scope. Read,
	# named, not supported — the scope is what has no runtime here.
	var plain_scoped: Dictionary = sim._parse_modifier_row("ARMOR 25% CRUSH")
	_check("sim", "modifier_row_percent_with_single_scope_is_unsupported",
		bool(plain_scoped.get("ok", false))
			and not bool(plain_scoped.get("supported", false))
			and Array(plain_scoped.get("scope", [])) == ["CRUSH"],
		str(plain_scoped))
	# MULTI, shape 5: `ARMOR <define> CRUSH` (attributemodifier.ini:3318) — the
	# real authored form of the row above, with the magnitude left as an
	# unresolved define. The comment that used to sit here claimed "unreadable
	# magnitude wins over the scope", but the only test beside it used a
	# READABLE 25%, so the claim was never checked: a parser that saw the scope
	# first and returned a named-unsupported verdict with a silent 0 magnitude
	# would have passed. UNREADABLE must win — ok=false, fail-closed at the call
	# site — because a named-unsupported verdict invites a caller to keep going.
	var unresolved_scoped: Dictionary = sim._parse_modifier_row("ARMOR ARMOR_CRUSH_BONUS CRUSH")
	_check("sim", "modifier_row_unreadable_magnitude_beats_the_scope",
		not bool(unresolved_scoped.get("ok", true))
			and String(unresolved_scoped.get("reason", "")) != "",
		str(unresolved_scoped))
	# ... and the same row without the scope is unreadable for the same reason,
	# so the verdict comes from the magnitude and not from the row's length.
	_check("sim", "modifier_row_unreadable_magnitude_alone_is_unreadable",
		not bool(sim._parse_modifier_row("ARMOR ARMOR_CRUSH_BONUS").get("ok", true)),
		str(sim._parse_modifier_row("ARMOR ARMOR_CRUSH_BONUS")))
	# The three-way verdict is the whole point: never let "readable" and
	# "modelled" collapse back into one boolean.
	_check("sim", "modifier_row_verdict_is_three_way",
		bool(sim._parse_modifier_row("ARMOR 50%").get("supported", false))
			and not bool(sim._parse_modifier_row("ARMOR 50% CRUSH").get("supported", true))
			and bool(sim._parse_modifier_row("ARMOR 50% CRUSH").get("ok", false))
			and not bool(sim._parse_modifier_row("ARMOR nope").get("ok", true)),
		"plain=%s scoped=%s bad=%s" % [
			sim._parse_modifier_row("ARMOR 50%"),
			sim._parse_modifier_row("ARMOR 50% CRUSH"),
			sim._parse_modifier_row("ARMOR nope")])

	# --- 1b. the same regression at a real resolver -------------------------
	var grove_object := {
		"aura": {
			"modifier": "GenericBuff", "range": 100.0,
			"objectFilter": "ANY +INFANTRY ALLIES", "requiredConditions": "ELVEN_WOOD",
		},
		"deletion": {"maxMs": 60000.0},
	}
	var grove_tab: Dictionary = sim._spellbook_grove_support(
		{"ElvenGroveObject": "G"}, {}, {}, {"GenericBuff": {"fields": [
			{"key": "Modifier", "value": "ARMOR\t50%"},
			{"key": "Modifier", "value": "DAMAGE_MULT\t150%"},
			{"key": "Duration", "value": "3000"},
		]}}, {"G": grove_object})
	var grove_space: Dictionary = sim._spellbook_grove_support(
		{"ElvenGroveObject": "G"}, {}, {}, {"GenericBuff": {"fields": [
			{"key": "Modifier", "value": "ARMOR 50%"},
			{"key": "Modifier", "value": "DAMAGE_MULT 150%"},
			{"key": "Duration", "value": "3000"},
		]}}, {"G": grove_object})
	_check("sim", "grove_modifier_separator_is_irrelevant",
		bool(grove_tab.get("ok", false)) and grove_tab == grove_space
			and is_equal_approx(float((grove_tab.get("effect", {}) as Dictionary).get("armor_mult", 0.0)), 0.5)
			and is_equal_approx(float((grove_tab.get("effect", {}) as Dictionary).get("damage_mult", 0.0)), 1.5),
		"tab=%s space=%s" % [grove_tab, grove_space])
	# ROW ABSENT vs ROW UNREADABLE — the round-16 regression this pair now pins.
	# An ARMOR-only ModifierList is legitimate retail authorship, not a broken
	# conversion: BFME2 1.06 `ModifierList GenericArmorLeadership`
	# (attributemodifier.ini:159-166) is `Modifier = ARMOR 50%` and nothing else,
	# and grove.ini:31 binds exactly it. An absent DAMAGE_MULT means the neutral
	# 1.0, so the grove must PLANT and its damage multiplier must be 1.0.
	# Round 16 required both rows, so this locked men/SpellBookElvenWoodMP.
	var grove_armor_only: Dictionary = sim._spellbook_grove_support(
		{"ElvenGroveObject": "G"}, {}, {}, {"GenericArmorLeadership": {"fields": [
			{"key": "Modifier", "value": "ARMOR 50%"},
			{"key": "Duration", "value": "3000"},
		]}}, {"G": {
			"aura": {
				"modifier": "GenericArmorLeadership", "range": 250.0,
				"objectFilter": "ANY +INFANTRY ALLIES", "requiredConditions": "ELVEN_WOOD",
			},
			"deletion": {"maxMs": 300000.0},
		}})
	var armor_only_effect: Dictionary = grove_armor_only.get("effect", {}) as Dictionary
	_check("sim", "grove_accepts_armor_only_authored_leaf",
		bool(grove_armor_only.get("ok", false))
			and is_equal_approx(float(armor_only_effect.get("armor_mult", 0.0)), 0.5)
			and is_equal_approx(float(armor_only_effect.get("damage_mult", 0.0)), 1.0)
			and bool((armor_only_effect.get("authored_rows", {}) as Dictionary).get("ARMOR", false))
			and not bool((armor_only_effect.get("authored_rows", {}) as Dictionary).get("DAMAGE_MULT", true)),
		str(grove_armor_only))
	# ... but a leaf with NO readable stat row at all is still a grove with
	# nothing to apply, and still locks.
	var grove_no_stats: Dictionary = sim._spellbook_grove_support(
		{"ElvenGroveObject": "G"}, {}, {}, {"GenericBuff": {"fields": [
			{"key": "Duration", "value": "3000"},
		]}}, {"G": grove_object})
	_check("sim", "grove_with_no_stat_row_locks",
		not bool(grove_no_stats.get("ok", false)),
		str(grove_no_stats))
	# READABLE, BUT NOT A KIND THIS LANE CONSUMES. Retail authors rows like
	# `EXPERIENCE 150%` on buff leaves beside the ARMOR/DAMAGE_MULT the grove
	# does model. Round 18 fixed exactly this silent drop in the
	# SpecialPowerModule lane, but the grove lane reaches it by a different route
	# — a `match` with no default arm — so the fix did not carry. The row must
	# now land in `unsupported_modifier_rows` under its own name, in the SAME
	# shape the other lane uses, and the readable rows beside it must still apply.
	var grove_extra_kind: Dictionary = sim._spellbook_grove_support(
		{"ElvenGroveObject": "G"}, {}, {}, {"GenericBuff": {"fields": [
			{"key": "Modifier", "value": "ARMOR 50%"},
			{"key": "Modifier", "value": "EXPERIENCE 150%"},
			{"key": "Duration", "value": "3000"},
		]}}, {"G": grove_object})
	var extra_effect: Dictionary = grove_extra_kind.get("effect", {}) as Dictionary
	var extra_residual: Array = extra_effect.get("unsupported_modifier_rows", []) as Array
	_check("sim", "grove_names_a_readable_but_unmodelled_kind_as_a_residual",
		bool(grove_extra_kind.get("ok", false))
			and is_equal_approx(float(extra_effect.get("armor_mult", 0.0)), 0.5)
			and extra_residual.size() == 1
			and String((extra_residual[0] as Dictionary).get("row", "")).contains("EXPERIENCE")
			and String((extra_residual[0] as Dictionary).get("reason", "")) != "",
		str(grove_extra_kind))
	# ... and a grove whose ONLY row is an unmodelled kind still has nothing to
	# apply, so it still locks rather than planting an inert grove.
	_check("sim", "grove_with_only_an_unmodelled_kind_still_locks",
		not bool((sim._spellbook_grove_support(
			{"ElvenGroveObject": "G"}, {}, {}, {"GenericBuff": {"fields": [
				{"key": "Modifier", "value": "EXPERIENCE 150%"},
				{"key": "Duration", "value": "3000"},
			]}}, {"G": grove_object}) as Dictionary).get("ok", false)),
		str(sim._spellbook_grove_support(
			{"ElvenGroveObject": "G"}, {}, {}, {"GenericBuff": {"fields": [
				{"key": "Modifier", "value": "EXPERIENCE 150%"},
				{"key": "Duration", "value": "3000"},
			]}}, {"G": grove_object})))
	# An unreadable row must LOCK the grove; the old code continued past it.
	var grove_bad_row: Dictionary = sim._spellbook_grove_support(
		{"ElvenGroveObject": "G"}, {}, {}, {"GenericBuff": {"fields": [
			{"key": "Modifier", "value": "ARMOR 50%"},
			{"key": "Modifier", "value": "DAMAGE_MULT one-and-a-half"},
			{"key": "Duration", "value": "3000"},
		]}}, {"G": grove_object})
	_check("sim", "grove_unreadable_modifier_row_locks",
		not bool(grove_bad_row.get("ok", false)),
		str(grove_bad_row))

	# --- 1c. an unsupported row must not take the readable rows with it -------
	# The exact angmar/SpellBookSnowbind leaf (attributemodifier.ini:1076-1082):
	# one readable `PRODUCTION 1%` beside one `INVULNERABLE 0% <15 damage types>`
	# that has no runtime here. Round 16 called the second a SHAPE error and
	# locked the whole power, losing the first.
	var snowbind_support: Dictionary = sim._spellbook_effect_support(
		{"module": "SpecialPowerModule"},
		[
			{"key": "AttributeModifier", "value": "SpellBookSnowbind"},
			{"key": "AttributeModifierRange", "value": "SNOWBIND_EFFECT_RADIUS", "resolved": 200},
			{"key": "AttributeModifierAffects", "value": "F", "resolvedText": "ANY ENEMIES ALLIES +STRUCTURE"},
		],
		{}, {"SpellBookSnowbind": {"fields": [
			{"key": "Category", "value": "SPELL"},
			{"key": "Modifier", "value": "PRODUCTION 1%"},
			{"key": "Modifier", "value": "INVULNERABLE 0% SLASH PIERCE SPECIALIST CRUSH CAVALRY SIEGE FLAME MAGIC FROST HERO HERO_RANGED STRUCTURAL URUK CAVALRY_RANGED CHOP"},
			{"key": "Duration", "value": "SNOWBIND_EFFECT_DURATION"},
		]}}, {}, {}, {})
	var snowbind_effect: Dictionary = snowbind_support.get("effect", {}) as Dictionary
	var snowbind_residual: Array = snowbind_effect.get("unsupported_modifier_rows", []) as Array
	_check("sim", "unsupported_row_does_not_lock_the_readable_rows_beside_it",
		bool(snowbind_support.get("ok", false))
			and is_equal_approx(float(snowbind_effect.get("production_mult", 0.0)), 0.01)
			and snowbind_residual.size() == 1
			and String((snowbind_residual[0] as Dictionary).get("shape", "")) == "percent_scoped"
			and String((snowbind_residual[0] as Dictionary).get("reason", "")) != "",
		str(snowbind_support))
	# ... and an UNREADABLE row on the same leaf still locks it outright.
	var unreadable_leaf: Dictionary = sim._spellbook_effect_support(
		{"module": "SpecialPowerModule"},
		[
			{"key": "AttributeModifier", "value": "L"},
			{"key": "AttributeModifierRange", "value": "R", "resolved": 200},
		],
		{}, {"L": {"fields": [
			{"key": "Modifier", "value": "PRODUCTION 1%"},
			{"key": "Modifier", "value": "ARMOR SOME_UNRESOLVED_DEFINE"},
		]}}, {}, {}, {})
	_check("sim", "unreadable_row_still_locks_the_power",
		not bool(unreadable_leaf.get("ok", false)), str(unreadable_leaf))

	# --- 2. taint composes with Darkness, additively, under the cap --------
	# Retail authors the taint as a ModifierList exactly like SpellBookDarkness,
	# and same-kind rows from different lists ADD. Both author ARMOR 50%, so the
	# pair reaches -100% armor and must be clamped by ABILITY_ARMOR_CAP instead
	# of turning the battalion immune. The old private row fields MULTIPLIED and
	# bypassed the cap entirely.
	var stacked := {}
	sim._set_timed_modifier(stacked, "taint:GenericBuff", [
		{"kind": "ARMOR", "value": 0.5}, {"kind": "DAMAGE_MULT", "value": 1.5},
	], 500)
	_check("sim", "taint_alone_reduces_incoming_by_authored_armor",
		is_equal_approx(sim._ability_incoming_multiplier(stacked), 0.5)
			and is_equal_approx(sim._ability_outgoing_multiplier(stacked), 1.5),
		"incoming=%s outgoing=%s" % [
			sim._ability_incoming_multiplier(stacked), sim._ability_outgoing_multiplier(stacked)])
	sim._set_timed_modifier(stacked, "weather:SpellBookDarkness", [
		{"kind": "ARMOR", "value": 0.5}, {"kind": "DAMAGE_MULT", "value": 1.5},
	], 500)
	_check("sim", "taint_and_darkness_sum_armor_and_respect_cap",
		is_equal_approx(
			sim._ability_incoming_multiplier(stacked), 1.0 - sim.ABILITY_ARMOR_CAP
		)
			and is_equal_approx(sim._ability_outgoing_multiplier(stacked), 2.25),
		"incoming=%s cap_floor=%s outgoing=%s" % [
			sim._ability_incoming_multiplier(stacked), 1.0 - sim.ABILITY_ARMOR_CAP,
			sim._ability_outgoing_multiplier(stacked)])

	# The accumulator key is per AUTHORED ModifierList, not one hardcoded name.
	# Men's grove binds GenericArmorLeadership and the RotWK groves bind
	# GenericBuff; under a single "taint:GenericBuff" key those two different
	# authored lists share one slot and the second silently REPLACES the first
	# instead of summing beside it.
	var collide := {}
	sim._set_timed_modifier(collide, "taint:GenericBuff", [{"kind": "ARMOR", "value": 0.5}], 500)
	sim._set_timed_modifier(collide, "taint:GenericArmorLeadership", [{"kind": "ARMOR", "value": 0.5}], 500)
	_check("sim", "taint_key_is_per_authored_modifier_list",
		Dictionary(collide.get("timed_modifiers", {})).size() == 2
			and is_equal_approx(
				sim._ability_incoming_multiplier(collide), 1.0 - sim.ABILITY_ARMOR_CAP
			),
		"table=%s incoming=%s" % [collide.get("timed_modifiers"), sim._ability_incoming_multiplier(collide)])

	# --- 3. suppression windows extend, never shrink ------------------------
	var supp_sim = SimScript.new()
	supp_sim.entities = {1: {
		"team": 1, "health": 100, "position": Vector2.ZERO,
		"object_id": "Fixture", "category": "infantry",
	}}
	supp_sim._apply_weather_anticategory(
		{"team": 0, "affects": "ANY", "expire_tick": 500})
	supp_sim._apply_weather_anticategory(
		{"team": 0, "affects": "ANY", "expire_tick": 100})
	_check("sim", "leadership_suppression_extends_never_shrinks",
		int((supp_sim.entities[1] as Dictionary).get(
			"leadership_suppressed_until_tick", -1
		)) == 500,
		"row=%s" % [supp_sim.entities[1]])

	# --- 4. ALL is universal only in a relation-only filter -----------------
	# Freezing Rain authors ALL ENEMIES and means everyone. Every other ALL in
	# this corpus sits beside kind terms, where the filter reads conjunctively.
	var fixture := {"team": 0, "category": "infantry", "object_id": "Fixture"}
	_check("sim", "all_is_universal_only_without_kind_terms",
		sim._spellbook_affects(fixture, "ALL ENEMIES")
			and sim._spellbook_member_affects(fixture, "ALL ENEMIES", false)
			and not sim._spellbook_affects(fixture, "ALL +CAVALRY")
			and sim._spellbook_affects(fixture, "ALL +INFANTRY"),
		"relation_only=%s member=%s conjunctive_miss=%s conjunctive_hit=%s" % [
			sim._spellbook_affects(fixture, "ALL ENEMIES"),
			sim._spellbook_member_affects(fixture, "ALL ENEMIES", false),
			sim._spellbook_affects(fixture, "ALL +CAVALRY"),
			sim._spellbook_affects(fixture, "ALL +INFANTRY")])

func _assert_real_selected_crebain_aura(effect: Dictionary) -> void:
	## This walks the effect compiled from the mounted Isengard spellbook pack;
	## no synthetic relation flags are injected into the aura under test.
	var targets: Array = effect.get("targets", []) as Array
	for group_value in effect.get("target_groups", []) as Array:
		targets.append_array((group_value as Dictionary).get("choices", []) as Array)
	var auras_by_modifier: Dictionary = {}
	for target_value in targets:
		var target := target_value as Dictionary
		if String(target.get("object_id", "")) != "Crebain":
			continue
		for aura_value in ((target.get("rule", {}) as Dictionary).get("summon_auras", []) as Array):
			var aura := aura_value as Dictionary
			auras_by_modifier[String(aura.get("id", ""))] = aura
	var expected_modifiers := {"EyeOfSauronFear": true, "GenericDebuff": true}
	var expected_categories := {"EyeOfSauronFear": "SPELL", "GenericDebuff": "DEBUFF"}
	var relation_contracts_ok := true
	for modifier_id in expected_modifiers:
		var aura: Dictionary = auras_by_modifier.get(modifier_id, {}) as Dictionary
		relation_contracts_ok = relation_contracts_ok and not aura.is_empty()
		relation_contracts_ok = relation_contracts_ok and String(aura.get("category", "")).to_upper() == expected_categories[modifier_id]
		relation_contracts_ok = relation_contracts_ok and aura.get("target_enemy", null) == true
		relation_contracts_ok = relation_contracts_ok and aura.get("target_allies", null) == null
		relation_contracts_ok = relation_contracts_ok and not SimScript.new()._summon_aura_allows_relation(aura, true)
		relation_contracts_ok = relation_contracts_ok and SimScript.new()._summon_aura_allows_relation(aura, false)
	_check(
		"isengard", "real_selected_crebain_two_enemy_only_auras",
		auras_by_modifier.keys().size() == 2
		and auras_by_modifier.has("EyeOfSauronFear")
		and auras_by_modifier.has("GenericDebuff")
		and relation_contracts_ok,
		"relations_ok=%s auras=%s" % [relation_contracts_ok, auras_by_modifier]
	)


func _exclude_pending(bucket: String, key: String, detail: String) -> void:
	excluded_pending += 1
	var power_id := key.trim_prefix("pending_rebuild_mismatch_")
	pending_mismatch_keys["%s/%s" % [bucket, power_id]] = true
	print("SPELLBOOK_MATRIX PENDING_REBUILD_MISMATCH %s/%s | %s" % [bucket, key, detail])


func _advance_effect_lifecycle(sim, ticks: int) -> void:
	## The matrix is an effect test, not a victory test. Some minimal faction
	## manifests satisfy the normal victory detector while a long summon timer
	## is advancing; clear that terminal flag between ticks so LifetimeUpdate is
	## still exercised through the live tick path.
	for _index in range(maxi(0, ticks)):
		sim.winner = -1
		sim.tick()
	sim.winner = -1


func _ensure_ally_for_cast(sim, team: int) -> void:
	## Place a healthy player entity near the cast point when the fortress
	## already seeded one; otherwise leave empty (empty-target reasons still
	## prove the effect path for heal/rally modules).
	if sim.entity_ids().is_empty():
		return
	for entity_id in sim.entity_ids():
		var entity: Dictionary = sim.entity(entity_id)
		if int(entity.get("team", -1)) == team:
			entity["position"] = Vector2(10.0, 10.0)
			# Wound slightly so heal powers can find a wounded ally.
			if int(entity.get("health", 0)) > 1:
				entity["health"] = maxi(1, int(entity.get("health", 1)) / 2)
			return


func _check(bucket: String, key: String, ok: bool, detail: String = "") -> void:
	var full := "%s/%s" % [bucket, key]
	if ok:
		passed += 1
		print("SPELLBOOK_MATRIX PASS %s" % full)
	else:
		failed += 1
		print("SPELLBOOK_MATRIX FAIL %s | %s" % [full, detail])
	if not report["spellbooks"].has(bucket):
		report["spellbooks"][bucket] = {}
