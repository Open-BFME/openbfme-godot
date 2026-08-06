extends RefCounted

## THE STRATEGIC LAYER'S OWN GAP REGISTER, measured against the RotWK
## living-world document (`openbfme.living-world`, 52 regions) and the gamedata
## `#define` table the macros bundle carries.
##
## Same discipline as `wotr_handoff.gd`'s `UNSUPPORTED_BY_TACTICAL_SIM`: every
## part of retail's War of the Ring the strategic layer does NOT model is named
## here, with the reason it is not modelled, so the absence is a stated fact
## rather than something a reader has to discover by playing. The rule each
## entry obeys is the repository's: where retail recorded the data, the layer
## models it; where retail did not, the layer FAILS CLOSED and says so here,
## because behavior invented to fill a hole is indistinguishable on screen from
## parity and therefore worse than the hole.
##
## A test asserts on this register (`wotr_strategic_runner.gd`), for the same
## reason one asserts on the handoff's: a gap list nothing checks is a gap list
## that rots into flattery.

## name -> why the gap is a gap, and where the hole in the data is. Keys are
## sorted by `names()` so every consumer reports them in one order.
const OPEN_GAPS := {
	# `strategic_treasury_income` AND `strategic_building_construction` USED TO BE
	# HERE, and what they said is worth quoting because the reason they were
	# written is the reason they were wrong - the same reason `strategic_ai_turns`
	# and `neutral_region_capture` were wrong before them:
	#
	#   "the document records income INGREDIENTS ... but no rule combining them
	#   into a per-turn treasury"
	#   "building prices are recorded (WOTR_*_COST) but the LW_* building macros
	#   the scenarios place are unexpanded importer gaps, so what a built
	#   structure IS or GRANTS is unrecorded"
	#
	# BOTH REASONED FROM ONE ARTEFACT - the importer's JSON living-world document
	# - as though that artefact were the whole of what retail ships. It is not. It
	# transcribes `livingworldregions.inc` and the scenario files, and it does not
	# read `livingworldbuildings.ini`, `riskcampaign.ini`, `gamedata.ini` or
	# `data/lotr.str`. Every one of those four holds part of the rule:
	#
	#   `livingworldbuildings.ini` - 28 `LivingWorldBuilding` blocks carrying
	#   `Type`, `AvailableTo`, `StrategicResourceCost`, `TurnsToBuild`,
	#   `CanDefendTerritory`, `BattleThingTemplate`, their `ArmyToSpawn` recruit
	#   lists AND `BuildingNugget IncreaseTreasury / TreasureAmount =
	#   GAIN_PER_FORTRESS|GAIN_PER_FARM`. This project has been CONVERTING that
	#   file all along - it is the `buildings` array in `living-world-ui.json`,
	#   which the strategic screen already loads for its portraits.
	#
	#   `riskcampaign.ini` lines 1-7 - the four `LW_*` `#define`s, above retail's
	#   own comment stating how to read them: "Allows scenarios to say that a fort
	#   should be spawned in a region, and THE APPROPRIATE ONE FOR THE CONTROLLING
	#   FACTION will be created."
	#
	#   `gamedata.ini` 8453-8460 - one `;//---WOTR---` block holding all seven
	#   numbers: the four prices and `GAIN_PER_FORTRESS`, `GAIN_PER_FARM`,
	#   `FERTILE_TERRITORY_BONUS`. `macros.json` already converts it.
	#
	#   `data/lotr.str` - the cadence and the sum, in prose:
	#   `STRATEGICHUD:StatsCTreasuryIncomeHelp` ("Treasury increases by this
	#   amount at the start of each turn"), `APT:NewWOTRFeature4Desc` ("Accumulate
	#   Treasury FROM FARMS AND FORTRESSES to recruit units and construct
	#   buildings on the strategic map"), `LW:InstructionText07` ("You can
	#   construct new buildings on any open build plot in a territory you
	#   control"), `CONTROLBAR:LW_FortRestricted`, and a dozen more.
	#
	# So construction and the treasury are IMPLEMENTED now - `wotr_buildings.gd`
	# is the catalogue, `wotr_state.gd` holds the rules and the purse,
	# `wotr_session.gd` is the door, `wotr_ai.gd` spends retail's `BuildingScore*`
	# weights on it - and the last mile that retail genuinely did not write down
	# is in `PROJECT_AUTHORED_RULES` below rather than left to look like parity.

	# WHAT THE CONVERTER STILL DROPS. `living-world-ui.json` carries fifteen of a
	# `LivingWorldBuilding` block's fields and NONE of its `BuildingNugget`
	# sub-blocks. The treasury nugget is reconstructible by type (retail's table
	# is exactly type-uniform) and IS reconstructed; the other four are not, and
	# are the reason a Barracks and an Armory currently cost real treasure and
	# grant nothing beyond standing on the map.
	"strategic_building_nuggets":
		"the living-world UI converter drops every BuildingNugget, so a farm's "
		+ "one-off IncreaseCommandPoints grant (Type=WORLD Amount=+30), a "
		+ "fortress's StrengthenArmy armour table (Bonus = <n> Armor:<pct>), an "
		+ "armory's UpgradeTroops list (NumUpgradesPerTurn/UpgradeableUnits) and "
		+ "every SpawnArmy QueueSize are unrecorded here; only IncreaseTreasury "
		+ "is reconstructible, because all 7 Fortress blocks name "
		+ "GAIN_PER_FORTRESS and all 7 Resource blocks name GAIN_PER_FARM",


	# The document's army rosters (`LivingWorldPlayerArmy`) carry entries and
	# quantities but no purchase cost, and no per-object CommandPoints value
	# reaches the document either - the region CP caps are compared against
	# roster QUANTITIES today, which is the honest approximation the state file
	# documents, not retail's arithmetic.
	"army_recruitment_and_cp_costs":
		"rosters carry no purchase cost and no per-object CommandPoints value, "
		+ "so recruitment and the world/hero CP economy cannot be enforced with "
		+ "retail's numbers",

	# Retail merges and splits WOTR armies on the strategic map. The document
	# records nothing about the operation - no cap, no cost, no rule for what
	# happens to the merged force's identity - so the layer offers neither.
	"army_merging_and_splitting":
		"no merge/split rule appears anywhere in the document",

	# `neutral_region_capture` USED TO BE HERE, and it is worth saying what it said
	# and why it is gone, because the reason it was written is the reason it was
	# wrong: it concluded from the IMPORTER'S JSON DOCUMENT that "the document
	# records no non-battle claim rule", and stopped there. The document is a
	# transcription of `livingworldregions.inc` and the scenario files; it is not
	# the whole of what retail ships. The rule is in `data/lotr.str`, stated in
	# prose by the War of the Ring tutorial's own narration, and it is quoted in
	# full at `CLAIM_RECORD_SCHEMA` in `wotr_state.gd`:
	#
	#   a HERO army takes neutral ground by marching into it
	#   (`WOTRSCRIPT:WOTR_Tutorial071subtitle`);
	#   a GARRISON army cannot (`WOTRSCRIPT:WOTR_Tutorial066subtitle`);
	#   the notice raised is `APT:LivingWorldRegionTakenNotice` ("%s taken!"),
	#   which every region names as its `ConqueredNotice` and which is a different
	#   string from `APT:LivingWorldRegionConqueredNotice` ("%s conquered!").
	#
	# So neutral ground is CLAIMED, never fought for, and `wotr_battle.gd` was
	# right all along to refuse a battle with no defending faction. The strategic
	# layer implements the claim (`can_claim` / `build_claim` / `begin_claim` /
	# `apply_claim`), `can_attack()` no longer says yes to unowned ground, and
	# `wotr_session.commit_attack()` routes a neutral target down the claim path.
	# What retail did not write down about it is named below, in
	# `PROJECT_AUTHORED_RULES`, rather than left to look like parity.

	# No spellbook, powers, or heroic strategic actions appear anywhere in the
	# living-world document - which matches retail: War of the Ring mode has no
	# living-world power tree. Listed so nobody "restores" one that never
	# existed.
	"strategic_powers":
		"absent from the document AND from retail's WOTR mode; nothing to model",

	# THIS ENTRY REPLACED A FALSE ONE, and the correction is the point of it.
	#
	# It used to read `strategic_ai_turns`: "the document carries no AI decision
	# data; an invented AI would masquerade as parity". The first clause is true
	# ONLY of the importer's JSON document, and it was written as if it were true
	# of retail. It is not. Retail ships `data/ini/livingworldaitemplate.ini` -
	# 781 bytes in the RotWK layer - carrying one `LivingWorldAITemplate
	# DefaultAITemplate` block with twenty authored weights. Seven of them are
	# `BonusPreference*`, a preference ordering over the SAME per-region bonus
	# fields the document records for all 52 regions. Retail's AI has recorded
	# taste in which region it wants; the importer simply never read the file.
	#
	# So the AI turn is no longer a gap: `wotr_ai.gd` scores targets with retail's
	# own weights over the document's own bonuses, and everything it decides that
	# retail did NOT record (when to attack, how to reach a front line, how to
	# break a tie) is named project-authored in `PROJECT_AUTHORED_RULES` and
	# reported to the screen through `decision_provenance()`.
	#
	# WHAT IS STILL A GAP is the other thirteen weights. `DesiredSoldierRatio` and
	# its eight siblings govern RECRUITMENT and `BuildingScoreArmory` and its
	# three siblings govern STRATEGIC CONSTRUCTION - two things this layer refuses
	# to do at all, for the reasons recorded under
	# `army_recruitment_and_cp_costs` and `strategic_building_construction`. The
	# opponent parses those weights, carries them, and reports them as unspendable
	# rather than dropping them or spending them on something retail did not mean.
	# `BonusPreferenceTreasury`, retail's HIGHEST weight, is also unspent: no
	# region-bonus field in the document is called treasury, and mapping it onto
	# `extraStartResources` or `fertileTerritory` would be this project deciding
	# what retail meant.
	# NARROWED, not deleted, and the narrowing is the record of what closed. The
	# `BuildingScore*` half of this gap is GONE: `wotr_ai.gd` now spends
	# `BuildingScoreArmory`, `BuildingScoreBarracks`, `BuildingScoreCastle` and
	# `BuildingScoreFarm` on retail's own four `Type` values (Castle->Fortress and
	# Farm->Resource are argued from retail's own vocabulary in
	# `wotr_buildings.gd`). What is still unspent is the nine `Desired*Ratio`
	# weights, which govern RECRUITMENT.
	#
	# `BonusPreferenceTreasury` is ALSO no longer unbindable, and saying why is
	# the point: it used to be listed here as naming "no region-bonus field the
	# document records". It names `fertileTerritory` - retail's own display string
	# for that field is `LW:RegionTreasuryBonus`, "+%d Treasure", and the region
	# panel tooltip over it is `STRATEGICHUD:RegionTreasuryIncomeBonusTooltip`,
	# "Treasury Income Bonus". Retail's highest-weighted preference is now spent.
	"strategic_ai_recruitment":
		"retail's AI template is loaded, its BonusPreference weights choose the "
		+ "opponent's targets and its BuildingScore* weights choose what it "
		+ "builds, but its nine Desired*Ratio weights govern RECRUITMENT, which "
		+ "this layer does not model (see army_recruitment_and_cp_costs)",
	"pre_battle_retreat_losses": "pre-battle retreat loss arithmetic is not proven by the retail strings",
	"phase_moves_apply_immediately": "moves apply immediately; no retail command-buffer representation is proven",
	"single_battle_per_phase": "the v1 contract admits one battle; no retail multi-battle queue representation is proven",
	"retreat_distance_rule": "automatic no-option relocation uses project-authored graph-distance and region-id tie rules",
	"phase_timer": "no authoritative retail timer value is proven for this contract",
}


## RULES THIS PROJECT AUTHORED, named so a player is never shown a strategic
## behaviour and left guessing whether retail chose it or this repository did.
##
## The distinction this table exists to keep sharp: an OPEN GAP above is
## something the layer REFUSES TO DO because retail's data does not say how. An
## entry here is something the layer DOES, in a place where retail's data says
## THAT it happens but not exactly HOW - a stated substitute, not silent parity.
## `wotr_ai.gd` keeps its own such table for the opponent's decisions; this one
## covers the strategic rules themselves.
const PROJECT_AUTHORED_RULES := {
	# Retail states the claim exists and who may make it (see the strings quoted
	# where `neutral_region_capture` used to be). It does not state WHICH of a
	# seat's hero armies marches, because in retail the player drags one. This
	# layer has no drag: `claiming_army()` picks the lowest-sorted adjacent owned
	# region's lowest army id - the same reproducible rule `wotr_handoff.gd`
	# already uses to choose a staging region - because the choice enters the hash
	# and therefore cannot be left to iteration order.
	"neutral_claim_army_selection":
		"retail lets the player drag the army that takes neutral ground; this "
		+ "layer has no drag, so the claiming army is the lowest army id in the "
		+ "lowest-sorted adjacent region the seat owns, chosen deterministically "
		+ "because it enters the strategic hash",

	# NOTHING anywhere in the living-world ini, the gamedata `#define` table or
	# the string table prices a claim: no cost field on a region, no per-move
	# treasury deduction, no command-point charge. Retail's own tutorial claims a
	# region with no mention of paying for one. Free is therefore the reading, and
	# it is recorded here as a reading rather than presented as a fact.
	"neutral_claim_costs_nothing":
		"no region field, gamedata #define or string prices taking neutral "
		+ "ground, so a claim costs nothing; that is this project reading an "
		+ "absence, not a value retail wrote down",

	# Retail's War of the Ring turn is PHASED - a planning phase in which every
	# army may move once, then a battle phase resolving every collision at once
	# (`wotrtutorial.inc` names the phases: Planning, ResolveBattles,
	# PlanRetreats). This layer's turn is ONE ACTION: one battle or one claim, and
	# then the turn passes. Two seats moving into the same neutral region in one
	# phase - the collision retail's tutorial manufactures in The Dead Marshes -
	# therefore cannot happen here; it reduces to the first seat claiming the
	# region and the second attacking it, which is the same battle one turn later.
	# ------------------------------------------------------------------------
	# CONSTRUCTION AND THE TREASURY. Retail records far more of this than the
	# register used to claim (see the block that replaced the two closed gaps
	# above), but three things are genuinely this project's and are named here so
	# a player is never shown a number and left guessing whose it is.
	# ------------------------------------------------------------------------

	# Retail authors the three addends and states in prose that they accumulate
	# per turn from farms and fortresses, and that the region term is a SUM
	# ("format string to show treasury income total (sum of all region Treasury
	# Income Bonuses)"). It does NOT write the whole expression down anywhere:
	# nothing says there is no base income, nothing says the building term and the
	# region term add rather than multiply, and nothing says whether a region's
	# PERMANENT authored stronghold (the `Fortress` block on a region, which no
	# seat built and which sits on no build plot) counts as a fortress for
	# `GAIN_PER_FORTRESS`. This layer reads it as a plain sum of structures
	# STANDING ON PLOTS plus fertile regions held, with no base term.
	"retreat_ai_order_tie_break":
		"AI-owned pending retreats are ordered by army id and choose the lowest-sorted admissible adjacent region; retail authors no tie rule",
	"retreat_cap_overflow_capital":
		"an ordered or closest-allied retreat must fit the destination command-point cap; only the authored-capital last resort ignores it",
	"retreat_capital_unevidenced_destroys":
		"when no admissible allied region and no resolvable authored capital exist, the defeated hero army is destroyed so Retreat cannot lock",

	"strategic_treasury_income_arithmetic":
		"income = GAIN_PER_FORTRESS per standing Fortress + GAIN_PER_FARM per "
		+ "standing Resource building + FERTILE_TERRITORY_BONUS per fertile "
		+ "region held, summed with no base term; retail authors all three "
		+ "amounts, the per-turn cadence and the fact that farms and fortresses "
		+ "accumulate treasury, but not the expression, and not whether a "
		+ "region's permanent authored stronghold counts (this layer says no, "
		+ "because it stands on no build plot)",

	# Retail's player clicks a specific foundation - `LW:InstructionText09`, "To
	# build a new structure, left click on a build plot". A caller that does not
	# name one (the opponent; a scenario's `SpawnBuildings` row, which names a
	# region and never a plot) still needs an answer, and the answer enters the
	# strategic hash, so it cannot be left to iteration order.
	"lowest_free_build_plot":
		"a build that names no plot lands on the lowest-numbered free plot in "
		+ "the region; retail's player clicks a specific foundation and its "
		+ "scenarios name only a region, so nothing retail wrote decides this",

	# Retail states this rule, in its own tutorial's prose and nowhere else:
	# "All structures cost a number of turns to build and only one structure per
	# territory can be under construction at a time" (`LW:InstructionText10`,
	# `data/lotr.str` 21729). No ini authors it. What is this project's is the
	# READING - RotWK compressed every `TurnsToBuild` to 1, so there is no
	# observable construction WINDOW for the rule to govern, and it is implemented
	# as its observable effect instead: a territory gains at most one structure
	# per turn. Without it, `WOTR_FARM_COST = 0` lets a seat fill every plot it
	# owns on turn one for nothing.
	"one_structure_per_territory_per_turn":
		"retail's tutorial states 'only one structure per territory can be under "
		+ "construction at a time' and no ini authors it; every shipped building "
		+ "is TurnsToBuild = 1, so there is no construction window to govern and "
		+ "the rule is implemented as its observable effect - a territory admits "
		+ "one new structure per turn",

	# Retail prices a refund where it means one - `LW:UnitRefundHelpText`,
	# "Treasury Refund if Disbanded +%d", for a DISBANDED UNIT - and prices none
	# anywhere for a demolished structure. The contrast is the evidence, and it is
	# recorded as a reading of an absence rather than as a value retail wrote.
	"demolition_refunds_nothing":
		"demolishing a structure frees its plot and returns no treasure; retail "
		+ "authors a refund for a disbanded UNIT and none for a structure, so "
		+ "this is this project reading an absence",

	# Fable v1.0 deliberately applies tactical moves immediately; retail evidence
	# does not expose a deferred command representation.
	"phase_moves_apply_immediately":
		"movement is applied when ordered during Tactical; no deferred move queue is invented",
	"single_battle_per_phase":
		"the current strategic transaction supports one committed battle in Battle; a multi-battle queue is not invented",
	"retreat_distance_rule":
		"when retail requires automatic relocation and no adjacent friendly retreat exists, closest allied ground is chosen by graph distance, then region id; that tie is project-authored",
	"phase_timer":
		"retail presents phase timing, but this contract authors no timer value, so phases end only through END PHASE",
	"pre_battle_retreat_losses":
		"retail names pre-battle retreat consequences but no loss arithmetic is proven here; the command is not implemented",

}


## The project-authored rule names, sorted, for the same reason `names()` sorts
## the gaps: every consumer reports them in one order.
static func authored_rule_names() -> PackedStringArray:
	var sorted_names: Array[String] = []
	for key in PROJECT_AUTHORED_RULES.keys():
		sorted_names.append(String(key))
	sorted_names.sort()
	return PackedStringArray(sorted_names)


static func authored_rule_reason(name: String) -> String:
	return String(PROJECT_AUTHORED_RULES.get(name, ""))


static func names() -> PackedStringArray:
	var sorted_names: Array[String] = []
	for key in OPEN_GAPS.keys():
		sorted_names.append(String(key))
	sorted_names.sort()
	return PackedStringArray(sorted_names)


static func reason(name: String) -> String:
	return String(OPEN_GAPS.get(name, ""))
