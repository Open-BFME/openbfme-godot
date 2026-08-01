extends RefCounted

## ============================================================================
## THE ONE FILE A MODDER EDITS TO CHANGE HOW A WAR OF THE RING BATTLE IS
## AUTO-RESOLVED.
##
## Every coefficient, every die, every toggle and every ordering decision that
## is NOT in a retail file is in the single table below. Nothing else in
## `src/wotr/` carries a combat constant. If you want different dice, different
## modifiers, or a completely different resolution rule, you change values HERE
## and you do not touch a line of logic anywhere else.
##
## ----------------------------------------------------------------------------
## WHOSE NUMBER IS IT? Every row carries `owner`, and the answer is one of two
## things and never anything in between:
##
##   "retail"   The number is in a file EA shipped, `source` names the file and
##              the field, and this table only says WHICH retail value is used -
##              never what it is. Change one of these and you are no longer
##              running retail's model; the battle screen will still label the
##              number as retail's, which would then be a lie. Prefer to leave
##              them alone.
##
##   "project"  Retail is SILENT here and this project had to decide something.
##              `note` says what retail does and does not state, and what was
##              chosen instead. These are the rows meant to be edited. Every one
##              of them is shown to the player on the battle result screen,
##              marked as this project's, so a mod that changes them is visible
##              rather than secret.
##
## `evidence` is the same three-way vocabulary `wotr_setup_bindings.gd` uses:
##   "data"        transcribed from a shipped retail file, named in `source`
##   "screenshot"  read off a retail screenshot
##   "authored"    a judgement call by this project, reasoning in `note`
##
## ----------------------------------------------------------------------------
## WHY DICE AT ALL, WHEN THE MODEL USED TO TAKE AN EXPECTATION.
##
## Retail's default auto-resolve weapon carries `MissPercentChance = 50` with
## the comment "default = all weapons miss 50% of the time". That is a RANDOM
## ROLL, and retail states no seed, no stream and no ordering for it. A previous
## reading of this model took the arithmetic expectation instead - multiply the
## damage by 0.5 - which is deterministic but is also not a battle: the same two
## armies always produce the same casualty list to the last hitpoint, and a
## defender who is one hitpoint short always dies.
##
## So this project rolls. It rolls RISK DICE, because Risk's contest is a rule
## players already know, it can be read off the screen in one line, and nobody
## will mistake it for EA's. It is NOT a reconstruction of what retail did -
## retail's stream is unknown and unknowable from the shipped files - and the
## battle screen says exactly that, next to the dice it rolled.
##
## THE DICE ARE SEEDED FROM THE COMMITMENT, never from the clock. See
## `wotr_autoresolve_battle.gd`: the seed is the canonical digest of the battle
## commitment, which is inside the strategic hash, so two peers who agree on the
## strategic state roll the same numbers in the same order. `randomize()`,
## `Time.*` and the engine's global RNG are absent from that file by
## construction, and its runner asserts they are.
## ============================================================================


## Where a resolution rule may be swapped wholesale.
const RULE_RISK_DICE := "risk_dice"
const RULE_EXPECTATION := "expectation"

## How to read `LevelBonus Bonus:<percent>`; retail does not say which.
const LEVEL_BONUS_IGNORE := "ignore"
const LEVEL_BONUS_ADDITIVE := "additive"
const LEVEL_BONUS_LITERAL := "literal"

## Fixed-point scale. Every combat quantity in the strategic layer is an INTEGER
## in thousandths, for the reason `wotr_state.gd` states at the top of itself:
## the strategic hash carries no floats. A retail decimal is converted once, at
## the lookup, by `roundi(value * 1000.0)`, and every multiplication afterwards
## is integer.
const MILLI := 1000


## ----------------------------------------------------------------------------
## THE TABLE. One row per decision. `value` is what the game uses.
## ----------------------------------------------------------------------------
const RULES: Array[Dictionary] = [
	# --- the resolution rule itself ------------------------------------------
	{
		"id": "resolution_rule",
		"value": RULE_RISK_DICE,
		"owner": "project",
		"evidence": "authored",
		"source": "",
		"note": (
			"WHICH RULE DECIDES WHETHER A STRIKE LANDS. `risk_dice` runs the "
			+ "contest described by the rows below. `expectation` restores the "
			+ "older behaviour: multiply damage by (1 - MissPercentChance/100) "
			+ "and roll nothing, which is deterministic but never surprising. "
			+ "A mod that wants some third rule adds a branch in "
			+ "`wotr_autoresolve_battle.gd::_strike_fraction_milli` and a value "
			+ "here; nothing else in the file knows which rule is running."),
	},

	# --- the dice ------------------------------------------------------------
	{
		"id": "attacker_dice",
		"value": 3,
		"owner": "project",
		"evidence": "authored",
		"source": "Risk, the board game",
		"note": "How many dice the STRIKING unit rolls. Risk's attacker rolls three.",
	},
	{
		"id": "defender_dice",
		"value": 2,
		"owner": "project",
		"evidence": "authored",
		"source": "Risk, the board game",
		"note": "How many dice the unit BEING STRUCK rolls. Risk's defender rolls two.",
	},
	{
		"id": "die_faces",
		"value": 6,
		"owner": "project",
		"evidence": "authored",
		"source": "Risk, the board game",
		"note": (
			"Faces per die. Any size works: the roller rejection-samples, so a "
			+ "face count that does not divide 2^32 is still unbiased."),
	},
	{
		"id": "pairs_compared",
		"value": 2,
		"owner": "project",
		"evidence": "authored",
		"source": "Risk, the board game",
		"note": (
			"Both sides sort their dice highest-first and the top N are paired "
			+ "off against each other. N is min(attacker_dice, defender_dice) in "
			+ "Risk; it is stated separately here so a mod can pair fewer."),
	},
	{
		"id": "ties_to_defender",
		"value": true,
		"owner": "project",
		"evidence": "authored",
		"source": "Risk, the board game",
		"note": (
			"On an equal pair the DEFENDER wins, which is Risk's own rule. Set "
			+ "false to hand ties to the attacker."),
	},
	{
		"id": "strike_fraction_milli_by_pairs_won",
		"value": [0, 500, 1000],
		"owner": "project",
		"evidence": "authored",
		"source": "",
		"note": (
			"The fraction of retail's computed damage a strike delivers, in "
			+ "thousandths, indexed by how many pairs the attacker won. Two "
			+ "won = all of it, one = half, none = nothing. The list must have "
			+ "pairs_compared + 1 entries. "
			+ "WHY THESE THREE NUMBERS: with 3 dice against 2 the attacker wins "
			+ "both pairs 2890 times in 7776, splits 2611 and loses both 2275, "
			+ "so the average fraction is (2*2890 + 1*2611) / (2*7776) = "
			+ "8391/15552 = 0.5396. Retail's own default weapon misses half the "
			+ "time, an average of 0.5000. The dice therefore leave retail's "
			+ "damage tables sitting almost exactly where retail's own miss "
			+ "chance left them - within four points - instead of rescaling the "
			+ "whole game. That is a property of THIS table, checked in the "
			+ "runner, and not a claim about what retail did."),
	},

	# --- what the dice replace ------------------------------------------------
	{
		"id": "retail_miss_percent_is_replaced_by_the_dice",
		"value": true,
		"owner": "project",
		"evidence": "authored",
		"source": "livingworldautoresolveweapon.ini MissPercentChance",
		"note": (
			"Retail's `MissPercentChance` is a roll with no stated seed. When "
			+ "the dice are running they REPLACE it rather than stacking on top "
			+ "of it, so a strike is not discounted twice. The battle screen "
			+ "names the retail value that was replaced. Set false to apply both."),
	},

	# --- the four places retail is silent -------------------------------------
	{
		"id": "level_bonus_reading",
		"value": LEVEL_BONUS_IGNORE,
		"owner": "project",
		"evidence": "authored",
		"source": "livingworldautoresolveweapon.ini LevelBonus Bonus:<percent>",
		"note": (
			"Retail calls LevelBonus 'a multiplier' and then authors a ladder "
			+ "rising from 10% at level 2 to 50% at level 10. Read literally, "
			+ "levelling a unit makes it WEAKER; read as 1+x it makes it "
			+ "stronger. Retail states nothing that settles it, so the default "
			+ "applies NOTHING and the ambiguity cannot leak into a number. "
			+ "`additive` reads 10% as x1.10, `literal` reads it as x0.10; both "
			+ "are labelled as this project's choice wherever they show up."),
	},
	{
		"id": "factor_composition",
		"value": "multiplicative",
		"owner": "project",
		"evidence": "authored",
		"source": "",
		"note": (
			"Retail states what each multiplier does ALONE and never states how "
			+ "several combine. This project multiplies them, in the fixed order "
			+ "given by `factor_order` below, dividing by 1000 after each step "
			+ "so the whole computation is integer."),
	},
	{
		"id": "factor_order",
		"value": [
			"damagePerRound", "levelBonus", "reduceAttackWhenHurt", "armor",
			"leadershipWeapon", "leadershipArmor", "handicapWeapon", "handicapArmor",
			"resourceWeapon", "resourceArmor", "scienceWeapon", "scienceArmor",
			"dice",
		],
		"owner": "project",
		"evidence": "authored",
		"source": "",
		"note": (
			"THE ORDER MATTERS because each step truncates to a whole "
			+ "thousandth. Retail states no order at all. This one puts retail's "
			+ "own per-unit numbers first, then the bonuses that scale them, then "
			+ "the dice last, so the dice scale a finished retail number rather "
			+ "than an intermediate one. Reorder the list to change it; the "
			+ "battle screen prints the factors in exactly this order."),
	},
	{
		"id": "round_order",
		"value": "simultaneous",
		"owner": "project",
		"evidence": "authored",
		"source": "",
		"note": (
			"Retail names the PARTS of a round - damage per round, regeneration "
			+ "'at the end of each auto-resolve round', reinforcements 'at the "
			+ "beginning of round N' - and never states the sequence, nor "
			+ "whether both sides strike at once. Here both sides strike "
			+ "simultaneously against the hitpoints they had at the START of the "
			+ "round, which is why two identical armies produce no winner."),
	},
	{
		"id": "target_spread",
		"value": true,
		"owner": "project",
		"evidence": "authored",
		"source": "livingworldautoresolvecombatchain.ini",
		"note": (
			"Retail states only the ORDER in which units pick targets ('the "
			+ "unit with a higher priority will get to select targets of that "
			+ "type first'), not what happens when two units want the same "
			+ "target. This prefers an unclaimed target of equal priority, so a "
			+ "whole army does not pile onto one unit. Set false to let every "
			+ "unit take its single best target regardless."),
	},

	# --- bounds ---------------------------------------------------------------
	{
		"id": "max_rounds",
		"value": 400,
		"owner": "project",
		"evidence": "authored",
		"source": "",
		"note": (
			"Retail states NO round cap. A battle that reaches this bound is "
			+ "reported UNDECIDED and no winner is invented; the strategic layer "
			+ "then leaves the region and both armies exactly where they were."),
	},
	{
		"id": "max_units_per_side",
		"value": 512,
		"owner": "project",
		"evidence": "authored",
		"source": "",
		"note": "A refusal bound, not a game rule. A larger side is refused by name.",
	},
	{
		"id": "die_reroll_attempts",
		"value": 8,
		"owner": "project",
		"evidence": "authored",
		"source": "",
		"note": (
			"Rejection-sampling attempts before a die falls back to a plain "
			+ "modulo. 2^32 is not a multiple of 6, so a plain modulo is very "
			+ "slightly biased toward low faces; rejection removes that. Eight "
			+ "attempts leaves a residual chance near (4/2^32)^8. The battle "
			+ "record counts any fallback that did happen rather than hiding it."),
	},

	# --- retail's own values this file only POINTS AT -------------------------
	{
		"id": "damage_per_round_source",
		"value": "livingworldautoresolveweapon.ini DamagePerRound Against:<type>",
		"owner": "retail",
		"evidence": "data",
		"source": "livingworldautoresolveweapon.ini",
		"note": "1,500 authored damage rows. Read, never edited here.",
	},
	{
		"id": "armor_multiplier_source",
		"value": "livingworldautoresolvearmor.ini Armor = <attacker type>",
		"owner": "retail",
		"evidence": "data",
		"source": "livingworldautoresolvearmor.ini",
		"note": "1,161 authored armour rows across 131 blocks. Read, never edited here.",
	},
	{
		"id": "hitpoints_source",
		"value": "livingworldautoresolvebody.ini HitpointsAtLevel",
		"owner": "retail",
		"evidence": "data",
		"source": "livingworldautoresolvebody.ini",
		"note": "653 authored hitpoint rows across 102 bodies. Read, never edited here.",
	},
	{
		"id": "handicap_source",
		"value": "livingworldautoresolvehandicaps.ini AutoResolveHandicapLevel",
		"owner": "retail",
		"evidence": "data",
		"source": "livingworldautoresolvehandicaps.ini",
		"note": (
			"21 rungs, GUIDisplayedLevel 0..100 in fives. Every rung's "
			+ "ArmorMultiplier is exactly the reciprocal of its "
			+ "WeaponMultiplier - retail's own algebra, asserted exactly."),
	},
	{
		"id": "leadership_source",
		"value": "livingworldautoresolveleadership.ini BonusForLevel",
		"owner": "retail",
		"evidence": "data",
		"source": "livingworldautoresolveleadership.ini",
		"note": "13 blocks. Only one may affect a unit; never its own owner.",
	},
	{
		"id": "combat_chain_source",
		"value": "livingworldautoresolvecombatchain.ini TargetPriority",
		"owner": "retail",
		"evidence": "data",
		"source": "livingworldautoresolvecombatchain.ini",
		"note": "9 chains, 27 authored priority rows.",
	},
	{
		"id": "reinforcement_source",
		"value": "livingworldautoresolvereinforcementschedule.ini",
		"owner": "retail",
		"evidence": "data",
		"source": "livingworldautoresolvereinforcementschedule.ini",
		"note": "Army 1 at round 0, 2 at 15, 3 at 30, 4 at 45, +1 each after.",
	},
]


## The rows a mod is EXPECTED to touch, and the rows it should not. Used by the
## battle screen to colour the working, and asserted by the runner so a row
## cannot quietly change sides.
static func project_rule_ids() -> PackedStringArray:
	return _ids_owned_by("project")


static func retail_rule_ids() -> PackedStringArray:
	return _ids_owned_by("retail")


static func _ids_owned_by(owner: String) -> PackedStringArray:
	var found: Array[String] = []
	for row in RULES:
		if String(row.get("owner", "")) == owner:
			found.append(String(row.get("id", "")))
	found.sort()
	return PackedStringArray(found)


## The whole row, or `{}` when no such rule exists. NEVER a default: a caller
## asking for a rule this table does not carry is a bug in the caller, and
## answering it with a plausible number is how a silent fallback starts.
static func row(id: String) -> Dictionary:
	for entry in RULES:
		if String(entry.get("id", "")) == id:
			return entry
	return {}


static func value(id: String) -> Variant:
	var found := row(id)
	return found.get("value", null) if not found.is_empty() else null


static func int_value(id: String, fallback: int) -> int:
	var found: Variant = value(id)
	return int(found) if typeof(found) == TYPE_INT else fallback


static func bool_value(id: String, fallback: bool) -> bool:
	var found: Variant = value(id)
	return bool(found) if typeof(found) == TYPE_BOOL else fallback


static func string_value(id: String, fallback: String) -> String:
	var found: Variant = value(id)
	return String(found) if typeof(found) == TYPE_STRING else fallback


static func array_value(id: String) -> Array:
	var found: Variant = value(id)
	return (found as Array) if typeof(found) == TYPE_ARRAY else []


## Why this table cannot be used as it stands, or "" when it can. Checked once
## at the top of every resolution, so a mod that mistypes a value is refused BY
## NAME instead of silently resolving battles with a broken rule.
static func refusal() -> String:
	var ids: Dictionary = {}
	for entry in RULES:
		var id := String(entry.get("id", ""))
		if id.is_empty():
			return "a rule row carries no id"
		if ids.has(id):
			return "rule '%s' is declared twice" % id
		ids[id] = true
		var owner := String(entry.get("owner", ""))
		if owner != "retail" and owner != "project":
			return "rule '%s' carries owner '%s', which is neither retail nor project" % [id, owner]
		if not ["data", "screenshot", "authored"].has(String(entry.get("evidence", ""))):
			return "rule '%s' carries no recognised evidence" % id
	var rule := string_value("resolution_rule", "")
	if rule != RULE_RISK_DICE and rule != RULE_EXPECTATION:
		return "resolution_rule '%s' is not a rule this build implements" % rule
	var pairs := int_value("pairs_compared", 0)
	if pairs <= 0:
		return "pairs_compared must be at least 1"
	if int_value("attacker_dice", 0) < pairs or int_value("defender_dice", 0) < pairs:
		return "pairs_compared (%d) exceeds the dice one side rolls" % pairs
	if int_value("die_faces", 0) < 2:
		return "die_faces must be at least 2"
	var fractions := array_value("strike_fraction_milli_by_pairs_won")
	if fractions.size() != pairs + 1:
		return "strike_fraction_milli_by_pairs_won has %d entries; pairs_compared %d needs %d" % [
			fractions.size(), pairs, pairs + 1]
	for entry in fractions:
		if typeof(entry) != TYPE_INT or int(entry) < 0:
			return "strike_fraction_milli_by_pairs_won carries a non-integer or negative entry"
	if int_value("max_rounds", 0) <= 0:
		return "max_rounds must be positive"
	if int_value("die_reroll_attempts", 0) <= 0:
		return "die_reroll_attempts must be positive"
	var reading := string_value("level_bonus_reading", "")
	if not [LEVEL_BONUS_IGNORE, LEVEL_BONUS_ADDITIVE, LEVEL_BONUS_LITERAL].has(reading):
		return "level_bonus_reading '%s' is not a reading this build implements" % reading
	return ""


## One line per rule, for the battle result screen and for the log. Retail's
## rows and this project's are visibly different lines, because a player has to
## be able to tell which numbers came out of EA's files and which ones this
## project decided.
static func explain() -> Array[Dictionary]:
	var lines: Array[Dictionary] = []
	for entry in RULES:
		lines.append({
			"id": String(entry.get("id", "")),
			"owner": String(entry.get("owner", "")),
			"evidence": String(entry.get("evidence", "")),
			"source": String(entry.get("source", "")),
			"note": String(entry.get("note", "")),
			"text": "%s = %s" % [String(entry.get("id", "")), _render(entry.get("value", null))],
		})
	return lines


static func _render(value_in: Variant) -> String:
	if typeof(value_in) == TYPE_ARRAY:
		var parts: Array[String] = []
		for item in value_in as Array:
			parts.append(str(item))
		return "[%s]" % ", ".join(parts)
	return str(value_in)
