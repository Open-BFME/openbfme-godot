extends RefCounted

## RETAIL'S WAR OF THE RING AUTO-RESOLVE RULES, MODELLED AS A PURE FUNCTION.
##
## WHAT THIS IS. A reading of the nine `livingworldautoresolve*.ini` documents
## retail ships, converted by `openbfme_importer.living_world_autoresolve`, plus
## the arithmetic those documents describe. Armies and modifiers go in, an
## outcome comes out. Nothing else.
##
## WHAT THIS IS EXPLICITLY NOT, AND MUST NOT BECOME WITHOUT AN OWNER DECISION:
##
## * It is NOT wired into battle resolution. Nothing in `src/wotr/`,
##   `src/retail_slice/` or `src/ui/` calls it. It is reachable from
##   `tests/wotr_autoresolve_runner.gd` and from nowhere else.
## * It holds NO state. Every function here is `static` and every input is an
##   argument. There is no session, no world, no commitment and no way to reach
##   one from this file.
## * It rolls NO dice. Retail's default weapon misses half the time
##   (`MissPercentChance = 50`) and retail states no seed and no stream, so this
##   model computes the EXPECTATION rather than sampling. It is therefore
##   deterministic, and it never claims to reproduce one particular retail
##   battle - only the arithmetic retail's tables describe.
##
## WHAT RETAIL STATES, AND WHERE. Every rule below is a quotation of a comment
## retail ships in its own documents; the function that implements it names the
## file it came from:
##
##   armor       "This controls how much damage a unit takes"; per ATTACKER type;
##               `AutoResolve_DefaultArmor` is "used as a fallback".
##   weapon      `DamagePerRound` "against a specific type of enemy";
##               `LevelBonus` "not cumulative ... next lowest level is used";
##               `ReduceAttackWhenHurt` "damage will be reduced ... to the unit's
##               current damage level"; `AutoResolve_DefaultWeapon` - "all other
##               weapons are copied from it".
##   body        `HitpointsAtLevel` "not cumulative ... next lowest level";
##               `RegenerateAtLevel` "regains <Regen> hitpoints after each
##               round"; `CanBeAttacked` - "the battle is over when all units
##               with CanBeAttacked = Yes are gone"; `LeaveInArmySummary`.
##   combatchain "The unit with a higher priority will get to select targets of
##               that type first."
##   leadership  "the unit is never affected by its own leadership";
##               `AffectsHigherLevelFirst`; "The BonusForLevel block with the
##               highest MinLevel <= unit's current level is used";
##               `MaximumUnitsAffected`; "Only one Leadership bonus can affect a
##               unit at a time. Leaderships with higher priority get to select
##               their targets first."
##   handicaps   weapon/armor/experience multipliers per GUI level.
##   resource + science purchase point bonus
##               highest threshold at or below the amount; both tables ship with
##               every non-1.0 tier COMMENTED OUT, so in RotWK they are no-ops.
##   reinforcement schedule
##               army N is available at the round listed; `EachRemaining` adds
##               that many rounds per army beyond the last listed.
##
## WHAT RETAIL DOES NOT STATE, AND WHICH THIS FILE THEREFORE LABELS AS THIS
## PROJECT'S OWN ARRANGEMENT rather than retail's:
##
##   * the READING of `LevelBonus Bonus:10%` - x0.10 or x1.10. The default here
##     is `LEVEL_BONUS_IGNORE`, which applies NOTHING, so the ambiguity cannot
##     leak into a number by default. The other two readings exist so a caller
##     can ask for one ON PURPOSE, and the answer says which it used.
##   * the ORDER and combining rule when several multipliers apply at once. This
##     file multiplies them and says so in `factors`.
##   * the within-round sequence and whether both sides strike at once. This
##     file resolves simultaneously and says so.
##
## Every result carries `factors`, a list of `{name, value, source}` records, so
## a caller can see which retail table produced each number and which numbers
## are this project's arrangement (`source` begins with `PROJECT:`).

const SCHEMA := "openbfme.living-world-autoresolve"
const SCHEMA_VERSION := 1

const BUNDLE_ENV := "OPENBFME_LIVING_WORLD_AUTORESOLVE"
const FILE_NAME := "living-world-autoresolve.json"
const MANIFEST_MAX_BYTES := 32 * 1024 * 1024

## Retail's four hard-coded fallback names, each carrying retail's own comment.
const DEFAULT_ARMOR := "AutoResolve_DefaultArmor"
const DEFAULT_WEAPON := "AutoResolve_DefaultWeapon"
const DEFAULT_BODY := "AutoResolve_DefaultBody"
const DEFAULT_COMBAT_CHAIN := "AutoResolve_DefaultCombatChain"

## How to read `LevelBonus Bonus:<percent>`. Retail does not say; see the header.
const LEVEL_BONUS_IGNORE := "ignore"
const LEVEL_BONUS_ADDITIVE := "additive"  ## 10% -> x1.10
const LEVEL_BONUS_LITERAL := "literal"  ## 10% -> x0.10

## A battle that has not ended by this round is reported UNDECIDED rather than
## given a winner. Retail states no round cap; this is a bound on the model, not
## a claim about retail, and the outcome says so.
const MAX_ROUNDS := 400
const MAX_UNITS_PER_SIDE := 512

var loaded := false
var source_path := ""
var errors: PackedStringArray = PackedStringArray()
## The whole manifest, exactly as the converter wrote it. Passed BY VALUE into
## every static function below, which is what keeps them pure.
var rules: Dictionary = {}


# --- loading (the only non-static part, and it decides nothing) ----------------


static func candidate_paths(roots: Array = []) -> Array[Dictionary]:
	var candidates: Array[Dictionary] = []
	var override := OS.get_environment(BUNDLE_ENV).strip_edges()
	if not override.is_empty():
		var direct := override if override.ends_with(FILE_NAME) else override.path_join(FILE_NAME)
		candidates.append({"path": direct, "origin": BUNDLE_ENV})
	for root in roots:
		var text := String(root).strip_edges()
		if text.is_empty():
			continue
		candidates.append({"path": text.path_join(FILE_NAME), "origin": "beside the map bundles"})
	return candidates


## Returns `{ok, path, reason}`. A failure is always a REASON naming every path
## tried and the command that makes a bundle.
func locate_and_load(roots: Array = []) -> Dictionary:
	var tried: Array[String] = []
	for candidate in candidate_paths(roots):
		var path := String(candidate["path"])
		tried.append("%s [%s]" % [path, String(candidate["origin"])])
		if not FileAccess.file_exists(path):
			continue
		if load_from(path):
			return {"ok": true, "path": path, "reason": ""}
	return {
		"ok": false,
		"path": "",
		"reason": (
			"NO AUTO-RESOLVE BUNDLE, so retail's auto-resolve tables are absent "
			+ "and nothing can be modelled from them. Looked at: %s. Produce one "
			+ "with: python -m openbfme_importer.living_world_autoresolve "
			+ "--catalog <catalog>.json --out <dir>, then point %s at <dir>."
		) % [
			"; ".join(tried) if not tried.is_empty() else "nowhere",
			BUNDLE_ENV,
		],
	}


func load_from(path: String) -> bool:
	loaded = false
	rules = {}
	errors = PackedStringArray()
	source_path = path
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return _fail("cannot open %s" % path)
	if file.get_length() > MANIFEST_MAX_BYTES:
		var oversized := file.get_length()
		file.close()
		return _fail("%s is %d bytes, over the %d limit" % [path, oversized, MANIFEST_MAX_BYTES])
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	file.close()
	if not (parsed is Dictionary):
		return _fail("%s is not a JSON object" % path)
	var document: Dictionary = parsed
	if String(document.get("schema", "")) != SCHEMA:
		return _fail("%s carries schema %s, expected %s" % [
			path, String(document.get("schema", "<none>")), SCHEMA])
	if int(document.get("schemaVersion", -1)) != SCHEMA_VERSION:
		return _fail("%s is schema version %d, expected %d" % [
			path, int(document.get("schemaVersion", -1)), SCHEMA_VERSION])
	for required in ["armors", "weapons", "bodies", "combatChains", "leaderships",
			"handicapLevels", "reinforcementSchedules", "totals", "unitTypes"]:
		if not document.has(required):
			return _fail("%s has no %s section" % [path, required])
	rules = document
	loaded = true
	return true


func _fail(reason: String) -> bool:
	errors.append(reason)
	rules = {}
	loaded = false
	return false


# --- retail's lookups, one function per retail rule ----------------------------


static func _factor(name: String, value: float, source: String) -> Dictionary:
	return {"name": name, "value": value, "source": source}


## Retail: `AutoResolve_DefaultArmor` is "used as a fallback". An armor block
## that does not list a row for this attacker type falls back to the default's
## row for it - 18 of retail's own 131 armor blocks omit exactly one row.
## Returns `{value, source, fallback, resolved}`; an armor name retail never
## declared resolves to the DEFAULT and says so, never to a lookalike.
static func armor_multiplier(
	rules_in: Dictionary, armor_name: String, attacker_type: String
) -> Dictionary:
	var armors: Dictionary = rules_in.get("armors", {})
	var fallback := ""
	var block: Dictionary = armors.get(armor_name, {})
	if block.is_empty():
		fallback = "armor block %s is not declared" % armor_name
		block = armors.get(DEFAULT_ARMOR, {})
	var rows: Dictionary = block.get("vs", {})
	if not rows.has(attacker_type):
		var default_block: Dictionary = armors.get(DEFAULT_ARMOR, {})
		var default_rows: Dictionary = default_block.get("vs", {})
		if not default_rows.has(attacker_type):
			return {
				"value": 1.0,
				"source": "MISSING:%s has no row for %s and neither has %s" % [
					armor_name, attacker_type, DEFAULT_ARMOR],
				"fallback": fallback,
				"resolved": false,
			}
		if fallback.is_empty():
			fallback = "%s declares no row for %s" % [armor_name, attacker_type]
		rows = default_rows
	var row: Dictionary = rows[attacker_type]
	if bool(row.get("unresolved", false)):
		return {
			"value": 1.0,
			"source": "UNRESOLVED MACRO %s" % String(row.get("raw", "?")),
			"fallback": fallback,
			"resolved": false,
		}
	var value := float(row.get("fraction", row.get("value", 1.0)))
	return {
		"value": value,
		"source": "livingworldautoresolvearmor.ini Armor = %s" % attacker_type,
		"fallback": fallback,
		"resolved": true,
	}


## Retail: "`DamagePerRound` gives the amount of damage a weapon does during an
## auto-resolve round against a specific type of enemy", and of the default
## weapon: "all other weapons are copied from it".
static func weapon_damage(
	rules_in: Dictionary, weapon_name: String, target_type: String
) -> Dictionary:
	var weapons: Dictionary = rules_in.get("weapons", {})
	var fallback := ""
	var block: Dictionary = weapons.get(weapon_name, {})
	if block.is_empty():
		fallback = "weapon block %s is not declared" % weapon_name
		block = weapons.get(DEFAULT_WEAPON, {})
	var rows: Dictionary = block.get("damagePerRound", {})
	if not rows.has(target_type):
		var default_rows: Dictionary = (weapons.get(DEFAULT_WEAPON, {}) as Dictionary).get(
			"damagePerRound", {})
		if not default_rows.has(target_type):
			return {
				"value": 0.0,
				"source": "MISSING:%s has no DamagePerRound against %s" % [
					weapon_name, target_type],
				"fallback": fallback,
				"resolved": false,
			}
		if fallback.is_empty():
			fallback = "%s declares no DamagePerRound against %s" % [weapon_name, target_type]
		rows = default_rows
	var row: Dictionary = rows[target_type]
	if bool(row.get("unresolved", false)):
		return {
			"value": 0.0,
			"source": "UNRESOLVED MACRO %s" % String(row.get("raw", "?")),
			"fallback": fallback,
			"resolved": false,
		}
	return {
		"value": float(row.get("value", 0.0)),
		"source": "livingworldautoresolveweapon.ini DamagePerRound Against:%s" % target_type,
		"fallback": fallback,
		"resolved": true,
	}


## Retail: "Bonuses are not cumulative. If no bonus is listed for the unit's
## current level, the next lowest level is used." Returns the AUTHORED PERCENT
## and nothing derived - see `level_bonus_multiplier` for why.
static func level_bonus_percent(
	rules_in: Dictionary, weapon_name: String, level: int
) -> Dictionary:
	var weapons: Dictionary = rules_in.get("weapons", {})
	var block: Dictionary = weapons.get(weapon_name, {})
	if block.is_empty():
		block = weapons.get(DEFAULT_WEAPON, {})
	var ladder: Array = block.get("levelBonus", [])
	var chosen: Dictionary = {}
	for entry in ladder:
		var row: Dictionary = entry
		if int(row.get("level", 0)) <= level:
			if chosen.is_empty() or int(row.get("level", 0)) >= int(chosen.get("level", 0)):
				chosen = row
	if chosen.is_empty():
		return {"percent": 0.0, "level": 0, "declared": false}
	if bool(chosen.get("unresolved", false)):
		return {
			"percent": 0.0,
			"level": int(chosen.get("level", 0)),
			"declared": false,
			"unresolvedMacro": String(chosen.get("raw", "?")),
		}
	return {
		"percent": float(chosen.get("bonusPercent", 0.0)),
		"level": int(chosen.get("level", 0)),
		"declared": true,
	}


## THE AMBIGUOUS ONE. Retail calls `LevelBonus` "a multiplier" and then authors a
## ladder rising from 10% to 50%; read as a literal multiplier, levelling a unit
## would make it WEAKER. Retail states nothing that settles it, so the default
## reading applies NOTHING and the returned `source` always names which reading
## produced the number and that the choice is this project's.
static func level_bonus_multiplier(
	rules_in: Dictionary, weapon_name: String, level: int, reading: String = LEVEL_BONUS_IGNORE
) -> Dictionary:
	var found := level_bonus_percent(rules_in, weapon_name, level)
	var percent := float(found.get("percent", 0.0))
	match reading:
		LEVEL_BONUS_IGNORE:
			return _factor("levelBonus", 1.0, (
				"PROJECT: LevelBonus ignored - retail does not state whether "
				+ "Bonus:%s%% means x%0.2f or x%0.2f") % [percent, percent / 100.0,
					1.0 + percent / 100.0])
		LEVEL_BONUS_ADDITIVE:
			return _factor("levelBonus", 1.0 + percent / 100.0,
				"PROJECT: LevelBonus read as 1 + %s%% (retail does not state this)" % percent)
		LEVEL_BONUS_LITERAL:
			return _factor("levelBonus", percent / 100.0,
				"PROJECT: LevelBonus read literally as %s%% (retail does not state this)" % percent)
	return _factor("levelBonus", 1.0, "PROJECT: unknown reading %s, applied nothing" % reading)


## Retail: "HP are not cumulative. If the current level is not listed, the next
## lowest level is used."
static func hitpoints_at_level(rules_in: Dictionary, body_name: String, level: int) -> Dictionary:
	return _ladder(rules_in, body_name, level, "hitpointsAtLevel")


## Retail: "the unit will regenerate at the end of each auto-resolve round ...
## regains <Regen> hitpoints after each round." No RotWK body has an ACTIVE
## `RegenerateAtLevel` row - every one retail wrote is commented out - so this
## returns 0 for every shipped body, which the runner asserts by name.
static func regenerate_at_level(rules_in: Dictionary, body_name: String, level: int) -> Dictionary:
	return _ladder(rules_in, body_name, level, "regenerateAtLevel")


static func _ladder(
	rules_in: Dictionary, body_name: String, level: int, key: String
) -> Dictionary:
	var bodies: Dictionary = rules_in.get("bodies", {})
	var block: Dictionary = bodies.get(body_name, {})
	var declared := not block.is_empty()
	if not declared:
		block = bodies.get(DEFAULT_BODY, {})
	var ladder: Array = block.get(key, [])
	var chosen: Dictionary = {}
	for entry in ladder:
		var row: Dictionary = entry
		if int(row.get("level", 0)) <= level:
			if chosen.is_empty() or int(row.get("level", 0)) >= int(chosen.get("level", 0)):
				chosen = row
	if chosen.is_empty():
		return {"amount": 0.0, "level": 0, "declared": false, "bodyDeclared": declared}
	if bool(chosen.get("unresolved", false)):
		return {
			"amount": 0.0,
			"level": int(chosen.get("level", 0)),
			"declared": false,
			"bodyDeclared": declared,
			"unresolvedMacro": String(chosen.get("raw", "?")),
		}
	return {
		"amount": float(chosen.get("amount", 0.0)),
		"level": int(chosen.get("level", 0)),
		"declared": true,
		"bodyDeclared": declared,
	}


## Retail: the default body "gives the default entries for the other bodies",
## and it declares `CanBeAttacked = Yes`. A body that does not restate it
## therefore inherits Yes.
static func can_be_attacked(rules_in: Dictionary, body_name: String) -> bool:
	var bodies: Dictionary = rules_in.get("bodies", {})
	var block: Dictionary = bodies.get(body_name, {})
	if block.has("canBeAttacked"):
		return bool(block["canBeAttacked"])
	var default_block: Dictionary = bodies.get(DEFAULT_BODY, {})
	return bool(default_block.get("canBeAttacked", true))


## Retail: "even if dead, the unit should still appear in the army after the
## battle is over" - "Heroes never die in B4ME2."
static func leave_in_army_summary(rules_in: Dictionary, body_name: String) -> bool:
	var bodies: Dictionary = rules_in.get("bodies", {})
	var block: Dictionary = bodies.get(body_name, {})
	return bool(block.get("leaveInArmySummary", false))


## Retail: the default weapon carries `MissPercentChance = 50`, "all weapons miss
## 50% of the time", and no other weapon restates it, so every weapon inherits.
static func miss_percent_chance(rules_in: Dictionary, weapon_name: String) -> float:
	var weapons: Dictionary = rules_in.get("weapons", {})
	var block: Dictionary = weapons.get(weapon_name, {})
	if block.has("missPercentChance"):
		return float(block["missPercentChance"])
	var default_block: Dictionary = weapons.get(DEFAULT_WEAPON, {})
	return float(default_block.get("missPercentChance", 0.0))


## Retail: "the unit's damage will be reduced by an amount [equal] to the unit's
## current damage level. E.g. if the unit has 100 max hitpoints, and is
## currently at 75 hitpoints, all the unit's attacks will be at 75% of normal."
static func reduce_attack_when_hurt(rules_in: Dictionary, weapon_name: String) -> bool:
	var weapons: Dictionary = rules_in.get("weapons", {})
	var block: Dictionary = weapons.get(weapon_name, {})
	if block.has("reduceAttackWhenHurt"):
		return bool(block["reduceAttackWhenHurt"])
	var default_block: Dictionary = weapons.get(DEFAULT_WEAPON, {})
	return bool(default_block.get("reduceAttackWhenHurt", false))


## The handicap ladder, chosen by retail's own `GUIDisplayedLevel`. An exact
## level is required: retail authors every multiple of 5 from 0 to 100 and states
## no interpolation, so a level between two rungs resolves to the HIGHEST rung at
## or below it and says so.
static func handicap_for(rules_in: Dictionary, gui_level: int) -> Dictionary:
	var levels: Array = rules_in.get("handicapLevels", [])
	var chosen: Dictionary = {}
	for entry in levels:
		var row: Dictionary = entry
		if int(row.get("guiDisplayedLevel", 0)) <= gui_level:
			if chosen.is_empty() or int(row.get("guiDisplayedLevel", 0)) >= int(
					chosen.get("guiDisplayedLevel", 0)):
				chosen = row
	if chosen.is_empty():
		return {
			"weaponMultiplier": 1.0,
			"armorMultiplier": 1.0,
			"experienceMultiplier": 1.0,
			"guiDisplayedLevel": -1,
			"exact": false,
			"declared": false,
		}
	return {
		"weaponMultiplier": float(chosen.get("weaponMultiplier", 1.0)),
		"armorMultiplier": float(chosen.get("armorMultiplier", 1.0)),
		"experienceMultiplier": float(chosen.get("experienceMultiplier", 1.0)),
		"guiDisplayedLevel": int(chosen.get("guiDisplayedLevel", 0)),
		"exact": int(chosen.get("guiDisplayedLevel", 0)) == gui_level,
		"declared": true,
	}


## The resource and science-purchase-point ladders. `section` is `resourceBonus`
## or `sciencePurchasePointBonus`; both have the same shape and both ship with
## every non-1.0 tier COMMENTED OUT in RotWK, which is a fact about retail's data
## and is asserted rather than assumed.
static func threshold_bonus(
	rules_in: Dictionary, section: String, side: String, amount: float
) -> Dictionary:
	var rules_list: Array = rules_in.get(section, [])
	for entry in rules_list:
		var rule: Dictionary = entry
		var sides: Array = rule.get("sides", [])
		if not side.is_empty() and not sides.has(side):
			continue
		var chosen: Dictionary = {}
		for bonus_entry in (rule.get("bonuses", []) as Array):
			var bonus: Dictionary = bonus_entry
			if float(bonus.get("minimum", 0.0)) <= amount:
				if chosen.is_empty() or float(bonus.get("minimum", 0.0)) >= float(
						chosen.get("minimum", 0.0)):
					chosen = bonus
		if chosen.is_empty():
			continue
		return {
			"weaponMultiplier": float(chosen.get("weaponMultiplier", 1.0)),
			"armorMultiplier": float(chosen.get("armorMultiplier", 1.0)),
			"minimum": float(chosen.get("minimum", 0.0)),
			"declared": true,
		}
	return {"weaponMultiplier": 1.0, "armorMultiplier": 1.0, "minimum": 0.0, "declared": false}


## Retail: army 1 arrives at round 0 - "VERY IMPORTANT: do not change this, or
## battles will end immediately" - 2 at 15, 3 at 30, 4 at 45, and
## "`EachRemaining = +1` ... for each army after the last listed, wait 1
## additional round before bringing on another". `army_index` is 1-based.
static func reinforcement_round(rules_in: Dictionary, side: String, army_index: int) -> int:
	var schedules: Dictionary = rules_in.get("reinforcementSchedules", {})
	var schedule: Dictionary = schedules.get(side, {})
	if schedule.is_empty():
		return -1
	var explicit: Dictionary = schedule.get("armyRound", {})
	var key := str(army_index)
	if explicit.has(key):
		return int(explicit[key])
	var last_index := 0
	var last_round := 0
	for entry in explicit.keys():
		var index := int(String(entry))
		if index > last_index:
			last_index = index
			last_round = int(explicit[entry])
	if last_index == 0 or army_index <= last_index:
		return -1
	return last_round + int(schedule.get("eachRemaining", 0)) * (army_index - last_index)


## Retail: "The unit with a higher priority will get to select targets of that
## type first ... Positive means the unit prefers attacking that type of unit;
## negative means the unit would rather attack something else. Zero is the
## default if a type is not specified."
static func target_priority(rules_in: Dictionary, chain_name: String, target_type: String) -> float:
	var chains: Dictionary = rules_in.get("combatChains", {})
	var chain: Dictionary = chains.get(chain_name, {})
	if chain.is_empty():
		chain = chains.get(DEFAULT_COMBAT_CHAIN, {})
	var targets: Dictionary = chain.get("targets", {})
	if not targets.has(target_type):
		return 0.0
	var value: Variant = targets[target_type]
	if value is Dictionary:
		return 0.0  # an unresolved priority is worth nothing, never a guess
	return float(value)


## Retail: "The BonusForLevel block with the highest MinLevel <= unit's current
## level is used."
static func leadership_bonus(
	rules_in: Dictionary, leadership_name: String, level: int
) -> Dictionary:
	var leaderships: Dictionary = rules_in.get("leaderships", {})
	var block: Dictionary = leaderships.get(leadership_name, {})
	if block.is_empty():
		return {}
	var chosen: Dictionary = {}
	for entry in (block.get("bonusForLevel", []) as Array):
		var bonus: Dictionary = entry
		if int(bonus.get("minLevel", 0)) <= level:
			if chosen.is_empty() or int(bonus.get("minLevel", 0)) >= int(chosen.get("minLevel", 0)):
				chosen = bonus
	return chosen


## Retail: "Only one Leadership bonus can affect a unit at a time. Leaderships
## with higher priority get to select their targets first"; "the unit is never
## affected by its own leadership"; `AffectsHigherLevelFirst`;
## `MaximumUnitsAffected`.
##
## `units` is an Array of unit records (see `resolve`). Returns
## `unit index -> {leadership, weaponMultiplier, armorMultiplier,
## experienceMultiplier}` for the units that got a bonus. PURE: it reads and
## returns, and mutates nothing.
static func assign_leadership(rules_in: Dictionary, units: Array) -> Dictionary:
	var sources: Array[Dictionary] = []
	for index in units.size():
		var unit: Dictionary = units[index]
		var name := String(unit.get("leadership", ""))
		if name.is_empty():
			continue
		var bonus := leadership_bonus(rules_in, name, int(unit.get("level", 1)))
		if bonus.is_empty():
			continue
		sources.append({"index": index, "leadership": name, "bonus": bonus})
	# Higher Priority selects first; ties break on the leadership NAME so the
	# result is deterministic rather than dependent on dictionary order.
	sources.sort_custom(func(a, b):
		var pa := float((a["bonus"] as Dictionary).get("priority", 0))
		var pb := float((b["bonus"] as Dictionary).get("priority", 0))
		if pa != pb:
			return pa > pb
		return String(a["leadership"]) < String(b["leadership"]))

	var leaderships: Dictionary = rules_in.get("leaderships", {})
	var assigned: Dictionary = {}
	for source in sources:
		var owner_index := int(source["index"])
		var bonus: Dictionary = source["bonus"]
		var block: Dictionary = leaderships.get(String(source["leadership"]), {})
		var affects: Array = block.get("affects", [])
		var higher_first := bool(block.get("affectsHigherLevelFirst", false))
		var eligible: Array[int] = []
		for index in units.size():
			if index == owner_index:
				continue  # "the unit is never affected by its own leadership"
			if assigned.has(index):
				continue  # "Only one Leadership bonus can affect a unit at a time"
			var unit: Dictionary = units[index]
			if not affects.has(String(unit.get("unitType", ""))):
				continue
			eligible.append(index)
		eligible.sort_custom(func(a, b):
			var la := int((units[a] as Dictionary).get("level", 1))
			var lb := int((units[b] as Dictionary).get("level", 1))
			if la != lb:
				return la > lb if higher_first else la < lb
			return a < b)
		var limit := int(bonus.get("maximumUnitsAffected", 0))
		var taken := 0
		for index in eligible:
			if limit > 0 and taken >= limit:
				break
			assigned[index] = {
				"leadership": String(source["leadership"]),
				"weaponMultiplier": float(bonus.get("weaponMultiplier", 1.0)),
				"armorMultiplier": float(bonus.get("armorMultiplier", 1.0)),
				"experienceMultiplier": float(bonus.get("experienceMultiplier", 1.0)),
				"priority": float(bonus.get("priority", 0.0)),
			}
			taken += 1
	return assigned


# --- the arithmetic -----------------------------------------------------------


## The damage `attacker` does to `defender` in one round, with every factor named
## and sourced. `modifiers` may carry `attackerLeadership`, `defenderLeadership`,
## `attackerHandicap`, `defenderHandicap`, `attackerResource`, `attackerScience`,
## `defenderResource`, `defenderScience` (as returned by the lookups above) and
## `levelBonusReading`.
##
## THE COMPOSITION IS MULTIPLICATIVE AND THAT IS THIS PROJECT'S ARRANGEMENT, not
## retail's - retail states what each factor does alone and never states how they
## combine. The `factors` list makes every one of them inspectable, and any
## factor whose `source` begins with `PROJECT:` is this project speaking.
static func strike_damage(
	rules_in: Dictionary, attacker: Dictionary, defender: Dictionary, modifiers: Dictionary = {}
) -> Dictionary:
	var factors: Array[Dictionary] = []
	var unresolved: Array[String] = []

	var weapon_name := String(attacker.get("weapon", DEFAULT_WEAPON))
	var armor_name := String(defender.get("armor", DEFAULT_ARMOR))
	var attacker_type := String(attacker.get("unitType", ""))
	var defender_type := String(defender.get("unitType", ""))

	var base := weapon_damage(rules_in, weapon_name, defender_type)
	if not bool(base["resolved"]):
		unresolved.append(String(base["source"]))
	factors.append(_factor("damagePerRound", float(base["value"]), String(base["source"])))

	var reading := String(modifiers.get("levelBonusReading", LEVEL_BONUS_IGNORE))
	factors.append(level_bonus_multiplier(
		rules_in, weapon_name, int(attacker.get("level", 1)), reading))

	# "if the unit has 100 max hitpoints, and is currently at 75 hitpoints, all
	# the unit's attacks will be at 75% of normal damage"
	var hurt := 1.0
	if reduce_attack_when_hurt(rules_in, weapon_name):
		var maximum := float(attacker.get("maxHitpoints", 0.0))
		if maximum > 0.0:
			hurt = clampf(float(attacker.get("hitpoints", maximum)) / maximum, 0.0, 1.0)
		factors.append(_factor("reduceAttackWhenHurt", hurt,
			"livingworldautoresolveweapon.ini ReduceAttackWhenHurt = Yes"))
	else:
		factors.append(_factor("reduceAttackWhenHurt", 1.0,
			"livingworldautoresolveweapon.ini ReduceAttackWhenHurt = No"))

	var armor := armor_multiplier(rules_in, armor_name, attacker_type)
	if not bool(armor["resolved"]):
		unresolved.append(String(armor["source"]))
	var armor_note := String(armor["source"])
	if not String(armor["fallback"]).is_empty():
		armor_note += " (fallback: %s)" % String(armor["fallback"])
	factors.append(_factor("armor", float(armor["value"]), armor_note))

	# Retail rolls; this model takes the expectation. See the header.
	var miss := miss_percent_chance(rules_in, weapon_name)
	factors.append(_factor("hitChance", clampf(1.0 - miss / 100.0, 0.0, 1.0),
		"PROJECT: expectation of MissPercentChance = %s (retail ROLLS; no seed is stated)" % miss))

	for pair in [
		["attackerLeadership", "weaponMultiplier", "leadershipWeapon",
			"livingworldautoresolveleadership.ini WeaponMultiplier"],
		["defenderLeadership", "armorMultiplier", "leadershipArmor",
			"livingworldautoresolveleadership.ini ArmorMultiplier"],
		["attackerHandicap", "weaponMultiplier", "handicapWeapon",
			"livingworldautoresolvehandicaps.ini WeaponMultiplier"],
		["defenderHandicap", "armorMultiplier", "handicapArmor",
			"livingworldautoresolvehandicaps.ini ArmorMultiplier"],
		["attackerResource", "weaponMultiplier", "resourceWeapon",
			"livingworldautoresolveresourcebonus.ini WeaponMultiplier"],
		["defenderResource", "armorMultiplier", "resourceArmor",
			"livingworldautoresolveresourcebonus.ini ArmorMultiplier"],
		["attackerScience", "weaponMultiplier", "scienceWeapon",
			"livingworldautoresolvesciencepurchasepointbonus.ini WeaponMultiplier"],
		["defenderScience", "armorMultiplier", "scienceArmor",
			"livingworldautoresolvesciencepurchasepointbonus.ini ArmorMultiplier"],
	]:
		var supplied: Dictionary = modifiers.get(String(pair[0]), {})
		if supplied.is_empty():
			continue
		factors.append(_factor(String(pair[2]),
			float(supplied.get(String(pair[1]), 1.0)), String(pair[3])))

	var total := 1.0
	for factor in factors:
		total *= float(factor["value"])
	return {
		"damage": maxf(total, 0.0),
		"factors": factors,
		"unresolved": unresolved,
		"composition": "PROJECT: factors multiplied; retail states no combining rule",
	}


## Retail: "The unit with a higher priority will get to select targets of that
## type first." Returns `attacker index -> defender index`, or -1 when an
## attacker has nothing it can attack. PURE.
static func choose_targets(rules_in: Dictionary, attackers: Array, defenders: Array) -> Dictionary:
	var attackable: Array[int] = []
	for index in defenders.size():
		var defender: Dictionary = defenders[index]
		if float(defender.get("hitpoints", 0.0)) <= 0.0:
			continue
		if not can_be_attacked(rules_in, String(defender.get("body", DEFAULT_BODY))):
			continue
		attackable.append(index)

	# Each attacker's own best available target type, then the attackers ordered
	# by how much they want it - retail's "higher priority selects first".
	var wants: Array[Dictionary] = []
	for index in attackers.size():
		var attacker: Dictionary = attackers[index]
		if float(attacker.get("hitpoints", 0.0)) <= 0.0:
			continue
		var chain := String(attacker.get("combatChain", DEFAULT_COMBAT_CHAIN))
		var best := -1
		var best_priority := 0.0
		for candidate in attackable:
			var target_type := String((defenders[candidate] as Dictionary).get("unitType", ""))
			var priority := target_priority(rules_in, chain, target_type)
			if best == -1 or priority > best_priority:
				best = candidate
				best_priority = priority
		if best == -1:
			continue
		wants.append({"index": index, "priority": best_priority})
	wants.sort_custom(func(a, b):
		var pa := float(a["priority"])
		var pb := float(b["priority"])
		if pa != pb:
			return pa > pb
		return int(a["index"]) < int(b["index"]))

	var chosen: Dictionary = {}
	var used: Dictionary = {}
	for want in wants:
		var attacker: Dictionary = attackers[int(want["index"])]
		var chain := String(attacker.get("combatChain", DEFAULT_COMBAT_CHAIN))
		var best := -1
		var best_key := Vector2(-1e30, 0.0)
		for candidate in attackable:
			var target_type := String((defenders[candidate] as Dictionary).get("unitType", ""))
			var priority := target_priority(rules_in, chain, target_type)
			# An unclaimed target of the same priority is preferred, so a whole
			# side does not pile onto one unit while others go untouched. That
			# spreading rule is THIS PROJECT'S; retail states only the ordering.
			var key := Vector2(priority, -float(int(used.get(candidate, 0))))
			if best == -1 or key.x > best_key.x or (key.x == best_key.x and key.y > best_key.y):
				best = candidate
				best_key = key
		chosen[int(want["index"])] = best
		if best >= 0:
			used[best] = int(used.get(best, 0)) + 1
	return chosen


## THE WHOLE BATTLE, AS A PURE FUNCTION. Armies in, outcome out.
##
## `attacker_side` and `defender_side` are
## `{armies: [[unit, ...], ...], handicap: <gui level>, resource: <float>,
##   sciencePoints: <float>, side: "PlayerMen"}`; a unit is
## `{name, unitType, level, weapon, armor, body, combatChain, leadership}`.
##
## The outcome names the winner ONLY when one side ran out of units retail's own
## rule counts - "the battle is over when all units with CanBeAttacked = Yes are
## gone". A battle that hits `MAX_ROUNDS` is reported `undecided`, never given a
## winner, because retail states no round cap and inventing one would invent a
## result.
static func resolve(
	rules_in: Dictionary,
	attacker_side: Dictionary,
	defender_side: Dictionary,
	options: Dictionary = {}
) -> Dictionary:
	if rules_in.is_empty():
		return {"ok": false, "reason": "no auto-resolve rules were supplied"}
	var reading := String(options.get("levelBonusReading", LEVEL_BONUS_IGNORE))
	var max_rounds := int(options.get("maxRounds", MAX_ROUNDS))

	var sides: Array[Dictionary] = []
	for pair in [["attacker", attacker_side, "Attacker"], ["defender", defender_side, "Defender"]]:
		var prepared := _prepare_side(rules_in, pair[1] as Dictionary, String(pair[2]))
		if not bool(prepared.get("ok", false)):
			return {"ok": false, "reason": String(prepared.get("reason", "unprepared side"))}
		prepared["role"] = String(pair[0])
		sides.append(prepared)

	var log: Array[Dictionary] = []
	var round_index := 0
	var winner := ""
	var reason := ""
	while round_index < max_rounds:
		for side in sides:
			_bring_on_reinforcements(side, round_index)
		var live_attacker := _living(rules_in, sides[0])
		var live_defender := _living(rules_in, sides[1])
		if live_attacker.is_empty() and live_defender.is_empty():
			winner = ""
			reason = "both sides have no unit with CanBeAttacked = Yes remaining"
			break
		if live_attacker.is_empty():
			winner = "defender"
			reason = "the attacker has no unit with CanBeAttacked = Yes remaining"
			break
		if live_defender.is_empty():
			winner = "attacker"
			reason = "the defender has no unit with CanBeAttacked = Yes remaining"
			break

		var leadership_a := assign_leadership(rules_in, sides[0]["field"])
		var leadership_d := assign_leadership(rules_in, sides[1]["field"])
		# BOTH SIDES STRIKE AT ONCE, and every strike is computed against the
		# hitpoints at the START of the round. Retail states no within-round
		# order; this is THIS PROJECT'S arrangement and it is the reason the
		# outcome is symmetric when the armies are.
		var pending: Array[Dictionary] = []
		pending.append_array(_plan_strikes(rules_in, sides[0], sides[1], leadership_a,
			leadership_d, reading))
		pending.append_array(_plan_strikes(rules_in, sides[1], sides[0], leadership_d,
			leadership_a, reading))
		var applied := 0.0
		for strike in pending:
			var target: Dictionary = (strike["side"] as Dictionary)["field"][int(strike["target"])]
			target["hitpoints"] = float(target["hitpoints"]) - float(strike["damage"])
			applied += float(strike["damage"])
		for side in sides:
			_regenerate(rules_in, side)
		log.append({
			"round": round_index,
			"strikes": pending.size(),
			"damage": applied,
			"attackerLiving": live_attacker.size(),
			"defenderLiving": live_defender.size(),
		})
		if pending.is_empty():
			reason = "no unit on either side could find a target"
			break
		round_index += 1

	if reason.is_empty():
		reason = "the battle reached the %d round bound without either side running out" % max_rounds
	return {
		"ok": true,
		"winner": winner,
		"undecided": winner.is_empty(),
		"reason": reason,
		"rounds": round_index,
		"log": log,
		"attacker": _summarise(rules_in, sides[0]),
		"defender": _summarise(rules_in, sides[1]),
		"levelBonusReading": reading,
		"notes": [
			"PROJECT: both sides strike simultaneously; retail states no within-round order.",
			"PROJECT: damage is the EXPECTATION of MissPercentChance, not a roll.",
			"PROJECT: factors are multiplied; retail states no combining rule.",
		],
	}


static func _prepare_side(rules_in: Dictionary, side: Dictionary, schedule: String) -> Dictionary:
	var armies: Array = side.get("armies", [])
	var prepared: Array[Dictionary] = []
	var total := 0
	for army_index in armies.size():
		var army: Array = armies[army_index]
		total += army.size()
		if total > MAX_UNITS_PER_SIDE:
			return {"ok": false, "reason": "more than %d units on one side" % MAX_UNITS_PER_SIDE}
		var arrival := reinforcement_round(rules_in, schedule, army_index + 1)
		if arrival < 0:
			return {
				"ok": false,
				"reason": "no %s reinforcement round for army %d" % [schedule, army_index + 1],
			}
		for entry in army:
			var unit: Dictionary = (entry as Dictionary).duplicate(true)
			var body := String(unit.get("body", DEFAULT_BODY))
			var maximum := float(hitpoints_at_level(
				rules_in, body, int(unit.get("level", 1)))["amount"])
			unit["maxHitpoints"] = maximum
			unit["hitpoints"] = maximum
			unit["arrivalRound"] = arrival
			unit["army"] = army_index + 1
			prepared.append(unit)
	return {
		"ok": true,
		"reserve": prepared,
		"field": [] as Array[Dictionary],
		"handicap": handicap_for(rules_in, int(side.get("handicap", 0))),
		"resource": threshold_bonus(rules_in, "resourceBonus", String(side.get("side", "")),
			float(side.get("resource", 0.0))),
		"science": threshold_bonus(rules_in, "sciencePurchasePointBonus",
			String(side.get("side", "")), float(side.get("sciencePoints", 0.0))),
		"schedule": schedule,
	}


static func _bring_on_reinforcements(side: Dictionary, round_index: int) -> void:
	var reserve: Array = side["reserve"]
	var field: Array = side["field"]
	var remaining: Array[Dictionary] = []
	for entry in reserve:
		var unit: Dictionary = entry
		if int(unit["arrivalRound"]) <= round_index:
			field.append(unit)
		else:
			remaining.append(unit)
	side["reserve"] = remaining


static func _living(rules_in: Dictionary, side: Dictionary) -> Array[int]:
	var living: Array[int] = []
	var field: Array = side["field"]
	for index in field.size():
		var unit: Dictionary = field[index]
		if float(unit.get("hitpoints", 0.0)) <= 0.0:
			continue
		if not can_be_attacked(rules_in, String(unit.get("body", DEFAULT_BODY))):
			continue
		living.append(index)
	if living.is_empty() and not (side["reserve"] as Array).is_empty():
		# Reinforcements still to come are not "gone"; retail's end condition is
		# about units, and a side with an army still scheduled has units.
		living.append(-1)
	return living


static func _plan_strikes(
	rules_in: Dictionary,
	attacking: Dictionary,
	defending: Dictionary,
	attacker_leadership: Dictionary,
	defender_leadership: Dictionary,
	reading: String
) -> Array[Dictionary]:
	var strikes: Array[Dictionary] = []
	var chosen := choose_targets(rules_in, attacking["field"], defending["field"])
	for key in chosen.keys():
		var target_index := int(chosen[key])
		if target_index < 0:
			continue
		var attacker: Dictionary = (attacking["field"] as Array)[int(key)]
		var defender: Dictionary = (defending["field"] as Array)[target_index]
		var modifiers := {
			"levelBonusReading": reading,
			"attackerHandicap": attacking["handicap"],
			"defenderHandicap": defending["handicap"],
			"attackerResource": attacking["resource"],
			"defenderResource": defending["resource"],
			"attackerScience": attacking["science"],
			"defenderScience": defending["science"],
		}
		if attacker_leadership.has(int(key)):
			modifiers["attackerLeadership"] = attacker_leadership[int(key)]
		if defender_leadership.has(target_index):
			modifiers["defenderLeadership"] = defender_leadership[target_index]
		var result := strike_damage(rules_in, attacker, defender, modifiers)
		strikes.append({
			"side": defending,
			"target": target_index,
			"damage": float(result["damage"]),
		})
	return strikes


static func _regenerate(rules_in: Dictionary, side: Dictionary) -> void:
	for entry in (side["field"] as Array):
		var unit: Dictionary = entry
		if float(unit.get("hitpoints", 0.0)) <= 0.0:
			continue
		var regen := regenerate_at_level(
			rules_in, String(unit.get("body", DEFAULT_BODY)), int(unit.get("level", 1)))
		var amount := float(regen["amount"])
		if amount <= 0.0:
			continue
		unit["hitpoints"] = minf(
			float(unit["hitpoints"]) + amount, float(unit.get("maxHitpoints", 0.0)))


static func _summarise(rules_in: Dictionary, side: Dictionary) -> Dictionary:
	var survivors: Array[Dictionary] = []
	var lost: Array[String] = []
	var kept: Array[String] = []
	for entry in (side["field"] as Array):
		var unit: Dictionary = entry
		var name := String(unit.get("name", "<unnamed>"))
		if float(unit.get("hitpoints", 0.0)) > 0.0:
			survivors.append({
				"name": name,
				"hitpoints": float(unit["hitpoints"]),
				"maxHitpoints": float(unit.get("maxHitpoints", 0.0)),
			})
			continue
		lost.append(name)
		if leave_in_army_summary(rules_in, String(unit.get("body", DEFAULT_BODY))):
			kept.append(name)
	for entry in (side["reserve"] as Array):
		var unit: Dictionary = entry
		survivors.append({
			"name": String(unit.get("name", "<unnamed>")),
			"hitpoints": float(unit.get("hitpoints", 0.0)),
			"maxHitpoints": float(unit.get("maxHitpoints", 0.0)),
			"neverArrived": true,
		})
	lost.sort()
	kept.sort()
	return {
		"survivors": survivors,
		"lost": lost,
		"keptInSummary": kept,
		"handicap": side["handicap"],
		"resource": side["resource"],
		"science": side["science"],
	}
