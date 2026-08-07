extends RefCounted

## THE STRATEGIC AUTO-RESOLVE. Two armies and a battle commitment go in; an
## attrition list, a winner (or an honest "undecided") and THE COMPLETE WORKING
## come out.
##
## This is the wired half of the auto-resolve lane. `wotr_autoresolve.gd` reads
## retail's nine documents and answers "what does retail's table say"; this file
## is the only thing in the game that turns those answers into a result, and it
## is the only thing that rolls a die.
##
## ============================================================================
## THE RULE, IN ONE PARAGRAPH, AS A PLAYER WOULD READ IT.
##
## Each round, every unit still standing picks a target using retail's own
## combat-chain priorities, and works out how much damage it would do using
## retail's own numbers: retail's damage-per-round against that kind of target,
## multiplied by that target's armour row against this kind of attacker,
## multiplied by anything retail says applies - a leadership bonus, a handicap
## rung, a resource bonus, the target territory's standing-building
## StrengthenArmy armour bonus, and the "hurt units hit softer" rule. Then the DICE
## decide how much of that actually lands. The striking unit rolls three dice,
## the unit being struck rolls two. Both sort highest first, the top two are
## paired off, and the striker has to BEAT the defender on a pair - a tie goes
## to the defender, exactly as in Risk. Win both pairs and the full damage
## lands; win one and half of it lands; win neither and nothing does. Both sides
## strike at the same time, against the hitpoints everyone had at the start of
## the round. Then retail's regeneration applies, retail's reinforcement
## schedule brings on the next army if it is due, and the round ends. The battle
## is over when one side has no unit left that retail's own rule counts.
##
## RETAIL'S PART: the damage tables, the armour tables, the hitpoints per level,
## the combat chain's target priorities, the leadership bonuses, the handicap
## ladder, the reinforcement schedule, the regeneration rule, the "hurt units
## hit softer" rule, the THIS_TERRIORITY StrengthenArmy armour tiers, and the
## end-of-battle condition.
##
## THIS PROJECT'S PART: the dice, the order the multipliers are applied in, the
## fact that both sides strike at once, the reading of `LevelBonus` (which is to
## apply nothing, because retail's own wording contradicts its own ladder), the
## round cap, and the rule that spreads targets rather than piling on. Every one
## of those lives in `wotr_autoresolve_rules.gd` as an editable row, and every
## one is labelled on the battle result screen.
##
## RETAIL ROLLED TOO, and this file does not pretend to reproduce it. Retail's
## default weapon carries `MissPercentChance = 50` and retail states no seed and
## no stream, so what retail's own dice did is not recoverable from the shipped
## files. The dice here are a DIFFERENT, STATED rule that happens to leave
## retail's damage numbers scaled by an average of 0.5396 against retail's own
## 0.5000 - close enough that retail's tables still mean what they meant.
##
## ============================================================================
## DETERMINISM, WHICH IS THE HARD CONSTRAINT.
##
## Every peer MUST roll identically or the campaign desyncs. This project has
## already fixed that class of defect twice (867447e, and the battlefield map),
## and a battle whose outcome depended on an unseeded roll would be the third.
## So:
##
##  * THE SEED IS THE COMMITMENT. `seed_for()` is the canonical SHA-256 digest
##    of the battle commitment - the same canonicaliser `state_hash()` uses,
##    over a record that is itself inside the strategic hash. Region, turn, both
##    seats, both factions, both handicaps, the battlefield, every committed and
##    defending army id and the brief digest are all in it. Two peers whose
##    strategic hashes agree cannot have different seeds, because the seed is a
##    pure function of hashed state.
##
##  * THERE IS NO OTHER SOURCE OF RANDOMNESS IN THIS FILE. No `randomize()`, no
##    `randi()`, no `RandomNumberGenerator`, no `Time.`, no `OS.`. The runner
##    greps this file for every one of those tokens and reddens if one appears.
##
##  * EVERY DIE IS A HASH, not a stream. A die is SHA-256 over
##    `seed|round|role|attacker|target|slot|attempt`, so it does not matter in
##    what order the dice are asked for, how many are asked for, or whether a
##    caller resolves two battles at once. There is no cursor to get out of step.
##
##  * THE ARITHMETIC IS INTEGER. Every quantity is thousandths of a hitpoint,
##    held in an `int`, exactly as `wotr_state.gd` requires of anything that
##    enters the strategic hash. Retail's authored decimals are converted once,
##    at the lookup, by `roundi(value * 1000.0)`; nothing downstream is a float.
##    A float in the hash would be a platform-dependent hash.
##
## PURE. Every function is `static`, every input is an argument, and the only
## mutation is of dictionaries this file created itself. It holds no session, no
## world and no state, and there is no way to reach one from here.

const AutoResolve = preload("res://src/wotr/wotr_autoresolve.gd")
const Rules = preload("res://src/wotr/wotr_autoresolve_rules.gd")
const StateScript = preload("res://src/wotr/wotr_state.gd")

const MILLI := Rules.MILLI

## Retail's four documented fallback blocks.
const DEFAULT_ARMOR := AutoResolve.DEFAULT_ARMOR
const DEFAULT_WEAPON := AutoResolve.DEFAULT_WEAPON
const DEFAULT_BODY := AutoResolve.DEFAULT_BODY
const DEFAULT_COMBAT_CHAIN := AutoResolve.DEFAULT_COMBAT_CHAIN

## How many rounds of strike-by-strike working the result carries. A 400-round
## battle would otherwise produce a log nobody can read and a dictionary nobody
## wants to hash. Beyond this the per-round SUMMARY is still complete; only the
## individual dice are dropped, and the result says how many rounds were
## abbreviated. Presentation only - it never reaches strategic state.
const DETAILED_ROUNDS := 12

## THE HONEST REGISTER for this resolver, in the same spirit as
## `wotr_handoff.UNSUPPORTED_BY_TACTICAL_SIM`: things retail's own data shows
## retail MODELS in auto-resolve that this resolver does not yet, each with the
## evidence and the reason nothing was invented in its place. The runner
## asserts on this register so it can never quietly rot, and an entry may only
## leave it by being implemented from retail data.
##
## NOT in this register, because they are NOT gaps:
##   * resource and science purchase-point tiers - consumed, and retail ships
##     every non-1.0 tier commented out, so they look up to 1.0 (retail's data
##     saying nothing, asserted in the runner rather than assumed).
##   * hero contribution - a WOTR hero IS a unit here: it fights on its own
##     retail body/weapon/armor blocks (unitType Hero) and projects its
##     leadership through `assign_leadership()`, which is retail's own
##     leadership table.
##   * army composition counters - retail's counters ARE the per-type
##     DamagePerRound rows, the per-attacker-type armor rows and the combat
##     chain priorities, all of which every strike consumes.
##   * the setup screen's "Auto-Resolve Display" (Dynamic / Quick) - retail's
##     rule decides the PRESENTATION of a resolution, not its arithmetic; the
##     row is documented as locked in `wotr_setup_bindings.gd` because this
##     project draws a written working where retail animates one.
const UNMODELLED_RETAIL_BEHAVIOUR := {
	"fortress_combatant": (
		"Retail auto-resolves a standing fortress as a UNIT: the tables type "
		+ "AutoResolveUnit_Fortress and ship per-faction fortress blocks "
		+ "(AutoResolve_MenFortressArmor/Body/Weapon and seven more factions), "
		+ "and the object bindings bind MenFortress, MordorFortress and their "
		+ "kin onto them. This resolver fields only ARMIES, because neither the "
		+ "living-world document nor the strategic state names WHICH fortress "
		+ "object stands in a region - the region's `fortress` block carries a "
		+ "display name and a portrait, not a template - and deriving MenFortress "
		+ "from FactionMen would be a guessed mapping wearing a lookup's costume. "
		+ "Closes when the strategic layer (prebuilt_fortress, Stream E) carries "
		+ "a fortress TEMPLATE the bindings can answer for."),
	"region_bonus_modifiers": (
		"Living-world regions author attack/defense/experience/resource bonuses "
		+ "and territories add more, but retail's nine auto-resolve documents "
		+ "state NO mechanism by which any of them reaches auto-resolve "
		+ "arithmetic - within those nine documents the battle multipliers are "
		+ "armor, weapon, level, leadership, handicap, resource-treasury and "
		+ "science tiers. That statement excludes livingworldbuildings.ini's now-"
		+ "applied THIS_TERRIORITY StrengthenArmy bonus. The region bonuses now RIDE "
		+ "the tactical rules overlay "
		+ "(wotr_battle.gd `region_bonuses`); no auto-resolve multiplier is "
		+ "invented for them until retail's own mechanism is identified."),
}


# --- the seed -----------------------------------------------------------------


## THE ONLY SEED. A pure function of the battle commitment, which is inside the
## strategic hash. Refuses an empty commitment rather than returning a digest of
## nothing, because a digest of nothing is the same on every battle and would
## make every battle roll the same dice.
static func seed_for(commitment: Dictionary) -> String:
	if commitment.is_empty():
		return ""
	return StateScript.canonical_digest(commitment)


## One die, as a hash. `stream` is whatever names this die uniquely within the
## battle; callers below build it from round, role, attacker index, target index
## and slot. Returns `{face, attempts, fellBackToModulo}`.
##
## REJECTION SAMPLING, because 2^32 is not a multiple of six and a plain modulo
## would make low faces very slightly likelier. The bias is tiny and it would
## never be noticed - which is exactly why it is removed rather than tolerated.
static func roll_die(seed_hex: String, stream: String, faces: int, attempts: int) -> Dictionary:
	if faces < 2:
		return {"face": 1, "attempts": 0, "fellBackToModulo": false}
	var bound := (4294967296 / faces) * faces
	var value := 0
	for attempt in range(maxi(attempts, 1)):
		var context := HashingContext.new()
		context.start(HashingContext.HASH_SHA256)
		context.update(("%s|%s|%d" % [seed_hex, stream, attempt]).to_utf8_buffer())
		var digest := context.finish()
		value = 0
		for index in range(4):
			value = (value << 8) | int(digest[index])
		if value < bound:
			return {"face": value % faces + 1, "attempts": attempt + 1, "fellBackToModulo": false}
	return {"face": value % faces + 1, "attempts": maxi(attempts, 1), "fellBackToModulo": true}


## THE DICE CONTEST. Returns `{attackerDice, defenderDice, pairs, pairsWon,
## fractionMilli, fallbacks}` - everything the battle screen prints, including
## the dice themselves, so a player can check the arithmetic by eye.
static func contest(seed_hex: String, stream: String) -> Dictionary:
	var faces := Rules.int_value("die_faces", 6)
	var attempts := Rules.int_value("die_reroll_attempts", 8)
	var pairs := Rules.int_value("pairs_compared", 2)
	var ties_to_defender := Rules.bool_value("ties_to_defender", true)
	var fallbacks := 0

	var attacker_dice: Array[int] = []
	for slot in range(Rules.int_value("attacker_dice", 3)):
		var rolled := roll_die(seed_hex, "%s|a%d" % [stream, slot], faces, attempts)
		attacker_dice.append(int(rolled["face"]))
		if bool(rolled["fellBackToModulo"]):
			fallbacks += 1
	var defender_dice: Array[int] = []
	for slot in range(Rules.int_value("defender_dice", 2)):
		var rolled := roll_die(seed_hex, "%s|d%d" % [stream, slot], faces, attempts)
		defender_dice.append(int(rolled["face"]))
		if bool(rolled["fellBackToModulo"]):
			fallbacks += 1

	# Highest first. `sort()` then reverse rather than a custom comparator, so
	# the ordering is the engine's own total order over ints and cannot depend
	# on a lambda's tie behaviour.
	attacker_dice.sort()
	attacker_dice.reverse()
	defender_dice.sort()
	defender_dice.reverse()

	var compared: Array[Dictionary] = []
	var won := 0
	for index in range(pairs):
		if index >= attacker_dice.size() or index >= defender_dice.size():
			break
		var a := attacker_dice[index]
		var d := defender_dice[index]
		var attacker_wins := a > d if ties_to_defender else a >= d
		if attacker_wins:
			won += 1
		compared.append({"attacker": a, "defender": d, "attackerWins": attacker_wins})

	var fractions := Rules.array_value("strike_fraction_milli_by_pairs_won")
	var fraction := 0
	if won < fractions.size():
		fraction = int(fractions[won])
	return {
		"attackerDice": attacker_dice,
		"defenderDice": defender_dice,
		"pairs": compared,
		"pairsWon": won,
		"fractionMilli": fraction,
		"fallbacks": fallbacks,
		"tiesToDefender": ties_to_defender,
	}


# --- building a side from strategic armies -------------------------------------


## Turn one strategic army's roster entries into auto-resolve units, using the
## object-to-block binding table the importer produced.
##
## `bindings` is the `objects` section of `living-world-autoresolve-bindings`.
## A template with NO binding is returned in `unbound` BY NAME and contributes
## no unit - it is never given retail's default blocks and quietly fought with,
## because a unit fighting on default numbers looks exactly like a unit fighting
## on its own and there would be no way to tell.
##
## UPGRADES. Retail gates each armour and weapon row on `RequiredUpgrades` /
## `ExcludedUpgrades`. The strategic layer owns no upgrades at all, so the row
## that applies is the FIRST one in retail's own file order that requires
## nothing. That is not a guess: a row that requires an upgrade cannot apply to
## a force that has none. When every row requires something, no row applies, and
## the unit falls back to retail's documented default block WITH THE FALLBACK
## NAMED in the unit record.
static func units_for_roster(
	bindings: Dictionary, entries: Array, level: int = 1
) -> Dictionary:
	var units: Array[Dictionary] = []
	var unbound: Array[String] = []
	for row in entries:
		var entry := row as Dictionary
		var template := String(entry.get("thing_template", entry.get("thingTemplate", "")))
		var quantity := int(entry.get("quantity", 0))
		if template.is_empty() or quantity <= 0:
			continue
		if not bindings.has(template):
			unbound.append(template)
			continue
		var binding: Dictionary = bindings[template]
		var armor := _first_ungated(binding.get("armorSet", []), "block")
		var weapon := _first_ungated(binding.get("weaponSet", []), "block")
		for copy in range(quantity):
			units.append({
				"template": template,
				"unit_type": String(binding.get("unitType", "")),
				"body": String(binding.get("body", DEFAULT_BODY)),
				"combat_chain": String(binding.get("combatChain", DEFAULT_COMBAT_CHAIN)),
				"leadership": String(binding.get("leadership", "")),
				"armor": String(armor.get("name", DEFAULT_ARMOR)),
				"armor_fallback": String(armor.get("fallback", "")),
				"weapon": String(weapon.get("name", DEFAULT_WEAPON)),
				"weapon_fallback": String(weapon.get("fallback", "")),
				"level": level,
			})
	unbound.sort()
	return {"units": units, "unbound": PackedStringArray(unbound)}


static func _first_ungated(rows: Array, key: String) -> Dictionary:
	for row in rows:
		var entry := row as Dictionary
		if (entry.get("requiredUpgrades", []) as Array).is_empty():
			return {"name": String(entry.get(key, "")), "fallback": ""}
	if rows.is_empty():
		return {"name": "", "fallback": "the object declares no block of this kind"}
	return {
		"name": "",
		"fallback": "every one of retail's %d rows requires an upgrade the strategic layer cannot own" % rows.size(),
	}


## Give every unit its starting hitpoints from retail's `HitpointsAtLevel`
## ladder, in thousandths. Returns the units with `hitpoints_milli` and
## `max_hitpoints_milli` added, plus `unresolved` naming any body whose ladder
## did not answer - those units get ZERO maximum and are reported, never a
## substituted number.
static func with_hitpoints(rules_in: Dictionary, units: Array) -> Dictionary:
	var out: Array[Dictionary] = []
	var unresolved: Array[String] = []
	for row in units:
		var unit: Dictionary = (row as Dictionary).duplicate(true)
		var body := String(unit.get("body", DEFAULT_BODY))
		var found := AutoResolve.hitpoints_at_level(
			rules_in, body, int(unit.get("level", 1)))
		var amount := 0
		if bool(found.get("declared", false)):
			amount = _to_milli(float(found.get("amount", 0.0)))
		else:
			unresolved.append("%s (body %s, level %d)" % [
				String(unit.get("template", "?")), body, int(unit.get("level", 1))])
		unit["max_hitpoints_milli"] = amount
		unit["hitpoints_milli"] = amount
		out.append(unit)
	unresolved.sort()
	return {"units": out, "unresolved": PackedStringArray(unresolved)}


static func _to_milli(value: float) -> int:
	return roundi(value * float(MILLI))


# --- the arithmetic of one strike ---------------------------------------------


## Every factor of one strike, in thousandths, in the order
## `wotr_autoresolve_rules.factor_order` states - and WHOSE NUMBER EACH ONE IS.
##
## Returns `{damageMilli, factors, unresolved, contest}`. `factors` is a list of
## `{name, milli, owner, source}`; `owner` is "retail" or "project" and the
## battle screen prints them differently. Any factor this project chose is
## "project" and says why in `source`.
static func strike(
	rules_in: Dictionary,
	attacker: Dictionary,
	defender: Dictionary,
	modifiers: Dictionary,
	seed_hex: String,
	stream: String
) -> Dictionary:
	var factors: Array[Dictionary] = []
	var unresolved: Array[String] = []
	var weapon_name := String(attacker.get("weapon", DEFAULT_WEAPON))
	var armor_name := String(defender.get("armor", DEFAULT_ARMOR))
	var attacker_type := String(attacker.get("unit_type", ""))
	var defender_type := String(defender.get("unit_type", ""))

	var base := AutoResolve.weapon_damage(rules_in, weapon_name, defender_type)
	if not bool(base["resolved"]):
		unresolved.append(String(base["source"]))
	var damage := _to_milli(float(base["value"]))
	factors.append(_factor("damagePerRound", damage, "retail", String(base["source"])))

	var applied: Dictionary = {"damagePerRound": true}
	for name in Rules.array_value("factor_order"):
		var key := String(name)
		if applied.has(key):
			continue
		applied[key] = true
		match key:
			"levelBonus":
				var reading := Rules.string_value("level_bonus_reading", Rules.LEVEL_BONUS_IGNORE)
				var bonus := AutoResolve.level_bonus_multiplier(
					rules_in, weapon_name, int(attacker.get("level", 1)), reading)
				var milli := _to_milli(float(bonus["value"]))
				damage = damage * milli / MILLI
				factors.append(_factor("levelBonus", milli, "project", String(bonus["source"])))
			"reduceAttackWhenHurt":
				var hurt := MILLI
				var source := "livingworldautoresolveweapon.ini ReduceAttackWhenHurt = No"
				if AutoResolve.reduce_attack_when_hurt(rules_in, weapon_name):
					var maximum := int(attacker.get("max_hitpoints_milli", 0))
					if maximum > 0:
						hurt = clampi(
							int(attacker.get("hitpoints_milli", maximum)) * MILLI / maximum,
							0, MILLI)
					source = "livingworldautoresolveweapon.ini ReduceAttackWhenHurt = Yes"
				damage = damage * hurt / MILLI
				factors.append(_factor("reduceAttackWhenHurt", hurt, "retail", source))
			"armor":
				var armor := AutoResolve.armor_multiplier(rules_in, armor_name, attacker_type)
				if not bool(armor["resolved"]):
					unresolved.append(String(armor["source"]))
				var note := String(armor["source"])
				if not String(armor["fallback"]).is_empty():
					note += " (fallback: %s)" % String(armor["fallback"])
				var milli := _to_milli(float(armor["value"]))
				damage = damage * milli / MILLI
				factors.append(_factor("armor", milli, "retail", note))
			"dice":
				continue  # applied last, below, so it scales a finished retail number
			_:
				var supplied := _modifier_for(key, modifiers)
				if supplied.is_empty():
					continue
				var milli := _to_milli(float(supplied["value"]))
				damage = damage * milli / MILLI
				factors.append(_factor(key, milli, "retail", String(supplied["source"])))

	var rolled: Dictionary = {}
	var rule := Rules.string_value("resolution_rule", Rules.RULE_RISK_DICE)
	if rule == Rules.RULE_RISK_DICE:
		rolled = contest(seed_hex, stream)
		var fraction := int(rolled["fractionMilli"])
		damage = damage * fraction / MILLI
		factors.append(_factor("dice", fraction, "project", (
			"PROJECT: Risk dice - attacker %s, defender %s, %d of %d pairs won%s. "
			+ "REPLACES retail's MissPercentChance = %s, whose seed retail never states."
			) % [
				str(rolled["attackerDice"]), str(rolled["defenderDice"]),
				int(rolled["pairsWon"]), (rolled["pairs"] as Array).size(),
				" (ties to the defender)" if bool(rolled["tiesToDefender"]) else "",
				AutoResolve.miss_percent_chance(rules_in, weapon_name),
			]))
	else:
		var miss := AutoResolve.miss_percent_chance(rules_in, weapon_name)
		var fraction := clampi(MILLI - _to_milli(miss / 100.0), 0, MILLI)
		if not Rules.bool_value("retail_miss_percent_is_replaced_by_the_dice", true):
			fraction = MILLI
		damage = damage * fraction / MILLI
		factors.append(_factor("dice", fraction, "project", (
			"PROJECT: no dice - the EXPECTATION of retail's MissPercentChance = %s. "
			+ "Deterministic, and never a claim about a particular retail battle.") % miss))

	return {
		"damageMilli": maxi(damage, 0),
		"factors": factors,
		"unresolved": unresolved,
		"contest": rolled,
	}


static func _factor(name: String, milli: int, owner: String, source: String) -> Dictionary:
	return {"name": name, "milli": milli, "owner": owner, "source": source}


## Which supplied modifier a `factor_order` entry names, or `{}`. A name this
## table does not know is SKIPPED rather than silently applied as 1.0, and the
## rules table's own refusal check is what catches a typo.
static func _modifier_for(key: String, modifiers: Dictionary) -> Dictionary:
	const MAP := {
		"leadershipWeapon": ["attackerLeadership", "weaponMultiplier",
			"livingworldautoresolveleadership.ini WeaponMultiplier"],
		"leadershipArmor": ["defenderLeadership", "armorMultiplier",
			"livingworldautoresolveleadership.ini ArmorMultiplier"],
		"handicapWeapon": ["attackerHandicap", "weaponMultiplier",
			"livingworldautoresolvehandicaps.ini WeaponMultiplier"],
		"handicapArmor": ["defenderHandicap", "armorMultiplier",
			"livingworldautoresolvehandicaps.ini ArmorMultiplier"],
		"resourceWeapon": ["attackerResource", "weaponMultiplier",
			"livingworldautoresolveresourcebonus.ini WeaponMultiplier"],
		"resourceArmor": ["defenderResource", "armorMultiplier",
			"livingworldautoresolveresourcebonus.ini ArmorMultiplier"],
		"scienceWeapon": ["attackerScience", "weaponMultiplier",
			"livingworldautoresolvesciencepurchasepointbonus.ini WeaponMultiplier"],
		"scienceArmor": ["defenderScience", "armorMultiplier",
			"livingworldautoresolvesciencepurchasepointbonus.ini ArmorMultiplier"],
		"strengthenArmor": ["targetStrengthening", "value",
			"livingworldbuildings.ini StrengthenArmy Bonus (THIS_TERRIORITY)"],
	}
	if not MAP.has(key):
		return {}
	var row: Array = MAP[key]
	var supplied: Dictionary = modifiers.get(String(row[0]), {})
	if supplied.is_empty():
		return {}
	return {"value": float(supplied.get(String(row[1]), 1.0)), "source": String(row[2])}


# --- the whole battle ----------------------------------------------------------


## RESOLVE. `attacker_side` and `defender_side` are
## `{units: [...], handicap: <gui level>, resource: <float>, sciencePoints:
## <float>, side: "PlayerMen"}` where each unit is a record from
## `units_for_roster()` carrying `hitpoints_milli`.
##
## Returns the full result, including `working` - the strike-by-strike record
## the battle screen draws. PURE: `attacker_side` and `defender_side` are
## duplicated before anything is written.
static func resolve(
	rules_in: Dictionary,
	attacker_side: Dictionary,
	defender_side: Dictionary,
	seed_hex: String,
	options: Dictionary = {}
) -> Dictionary:
	var table_refusal := Rules.refusal()
	if not table_refusal.is_empty():
		return _refused("the auto-resolve rules table is not usable: %s" % table_refusal)
	if rules_in.is_empty():
		return _refused("no auto-resolve rules were supplied")
	if seed_hex.length() != 64 or not seed_hex.is_valid_hex_number():
		return _refused(
			"the auto-resolve seed is not a commitment digest, so the dice would not "
			+ "be reproducible on another peer")

	var max_units := Rules.int_value("max_units_per_side", 512)
	var sides: Array[Dictionary] = []
	for pair in [[attacker_side, "Attacker", "attacker"], [defender_side, "Defender", "defender"]]:
		var prepared := _prepare(rules_in, pair[0] as Dictionary, String(pair[1]), max_units)
		if not bool(prepared.get("ok", false)):
			return _refused(String(prepared.get("reason", "a side could not be prepared")))
		prepared["role"] = String(pair[2])
		sides.append(prepared)

	var max_rounds := int(options.get("maxRounds", Rules.int_value("max_rounds", 400)))
	var log: Array[Dictionary] = []
	var abbreviated := 0
	var fallbacks := 0
	var unresolved: Array[String] = []
	var round_index := 0
	var winner := ""
	var reason := ""

	while round_index < max_rounds:
		for side in sides:
			_bring_on(side, round_index)
		var live_attacker := _living(rules_in, sides[0])
		var live_defender := _living(rules_in, sides[1])
		if live_attacker == 0 and live_defender == 0:
			reason = "both sides ran out of units retail's own rule counts in the same round"
			break
		if live_attacker == 0:
			winner = "defender"
			reason = "the attacker has no unit with CanBeAttacked = Yes remaining"
			break
		if live_defender == 0:
			winner = "attacker"
			reason = "the defender has no unit with CanBeAttacked = Yes remaining"
			break

		var leadership_a := AutoResolve.assign_leadership(rules_in, _as_model_units(sides[0]["field"]))
		var leadership_d := AutoResolve.assign_leadership(rules_in, _as_model_units(sides[1]["field"]))
		var detailed := round_index < DETAILED_ROUNDS
		if not detailed:
			abbreviated += 1

		var pending: Array[Dictionary] = []
		pending.append_array(_plan(rules_in, sides[0], sides[1], leadership_a, leadership_d,
			seed_hex, round_index, "a", detailed))
		pending.append_array(_plan(rules_in, sides[1], sides[0], leadership_d, leadership_a,
			seed_hex, round_index, "d", detailed))

		# BOTH SIDES STRIKE AT ONCE. Every strike above was computed against the
		# hitpoints at the START of the round; only now is any of it applied.
		var dealt := 0
		var strikes: Array[Dictionary] = []
		for entry in pending:
			var strike_row: Dictionary = entry
			var target: Dictionary = (strike_row["side"] as Dictionary)["field"][int(strike_row["target"])]
			target["hitpoints_milli"] = int(target["hitpoints_milli"]) - int(strike_row["damageMilli"])
			dealt += int(strike_row["damageMilli"])
			fallbacks += int(strike_row.get("fallbacks", 0))
			for note in strike_row.get("unresolved", []) as Array:
				if not unresolved.has(String(note)):
					unresolved.append(String(note))
			if detailed:
				strikes.append(strike_row["working"])

		for side in sides:
			_regenerate(rules_in, side)

		log.append({
			"round": round_index,
			"detailed": detailed,
			"strikeCount": pending.size(),
			"damageMilli": dealt,
			"attackerLiving": live_attacker,
			"defenderLiving": live_defender,
			"strikes": strikes,
		})
		if pending.is_empty():
			reason = "no unit on either side could find a target"
			break
		round_index += 1

	if reason.is_empty():
		reason = ("the battle reached this project's %d round bound without either side "
			+ "running out; retail states no round cap, so no winner is invented") % max_rounds

	unresolved.sort()
	return {
		"ok": true,
		"refusals": PackedStringArray(),
		"winner": winner,
		"undecided": winner.is_empty(),
		"reason": reason,
		"rounds": round_index,
		"seed": seed_hex,
		"log": log,
		"abbreviatedRounds": abbreviated,
		"dieFallbacks": fallbacks,
		"unresolved": PackedStringArray(unresolved),
		"attacker": _summarise(rules_in, sides[0]),
		"defender": _summarise(rules_in, sides[1]),
		"rules": Rules.explain(),
	}


static func _refused(reason: String) -> Dictionary:
	return {
		"ok": false,
		"refusals": PackedStringArray([reason]),
		"winner": "",
		"undecided": true,
		"reason": reason,
		"rounds": 0,
		"seed": "",
		"log": [],
		"abbreviatedRounds": 0,
		"dieFallbacks": 0,
		"unresolved": PackedStringArray(),
		"attacker": {},
		"defender": {},
		"rules": Rules.explain(),
	}


static func _prepare(
	rules_in: Dictionary, side: Dictionary, schedule: String, max_units: int
) -> Dictionary:
	var strengthening: Dictionary = side.get("strengthening", {})
	if not strengthening.is_empty() and not bool(strengthening.get("ok", false)):
		return {"ok": false, "reason": "%s StrengthenArmy report is refused: %s" % [
			schedule, String(strengthening.get("refusal", "unknown refusal"))]}
	var strengthen_multiplier := float(strengthening.get("armor_multiplier", 1.0))
	if not is_finite(strengthen_multiplier) or strengthen_multiplier < 0.0:
		return {"ok": false, "reason": "%s StrengthenArmy armor multiplier is malformed" % schedule}
	var armies: Array = side.get("armies", [])
	if armies.is_empty() and side.has("units"):
		armies = [side.get("units", [])]
	var reserve: Array[Dictionary] = []
	var total := 0
	for army_index in armies.size():
		var army: Array = armies[army_index]
		total += army.size()
		if total > max_units:
			return {"ok": false, "reason": "more than %d units on one side" % max_units}
		var arrival := AutoResolve.reinforcement_round(rules_in, schedule, army_index + 1)
		if arrival < 0:
			return {"ok": false,
				"reason": "retail's %s schedule states no arrival round for army %d" % [
					schedule, army_index + 1]}
		for row in army:
			var unit: Dictionary = (row as Dictionary).duplicate(true)
			unit["arrival_round"] = arrival
			unit["army"] = army_index + 1
			if not unit.has("max_hitpoints_milli"):
				return {"ok": false,
					"reason": "unit %s carries no hitpoints; call with_hitpoints() first" % String(
						unit.get("template", "?"))}
			reserve.append(unit)
	# AN EMPTY SIDE IS ALLOWED, and it is not a hole. A region its owner has left
	# undefended is a legal thing to attack, and retail's own end condition -
	# "the battle is over when all units with CanBeAttacked = Yes are gone" - is
	# already true of that side before round one. The resolver therefore reports
	# a winner in zero rounds with no attrition, which is what happened, rather
	# than refusing a legal move or fabricating a fight.
	return {
		"ok": true,
		"reserve": reserve,
		"field": [] as Array[Dictionary],
		"handicap": AutoResolve.handicap_for(rules_in, int(side.get("handicap", 0))),
		"resource": AutoResolve.threshold_bonus(rules_in, "resourceBonus",
			String(side.get("side", "")), float(side.get("resource", 0.0))),
		"science": AutoResolve.threshold_bonus(rules_in, "sciencePurchasePointBonus",
			String(side.get("side", "")), float(side.get("sciencePoints", 0.0))),
		"strengthening": {"value": strengthen_multiplier,
			"source": "livingworldbuildings.ini StrengthenArmy Bonus (THIS_TERRIORITY)"},
		"schedule": schedule,
	}


static func _bring_on(side: Dictionary, round_index: int) -> void:
	var remaining: Array[Dictionary] = []
	for row in side["reserve"] as Array:
		var unit: Dictionary = row
		if int(unit["arrival_round"]) <= round_index:
			(side["field"] as Array).append(unit)
		else:
			remaining.append(unit)
	side["reserve"] = remaining


## How many units retail's own end condition still counts. A side with an army
## still scheduled is not "gone", so a pending reserve counts as one.
static func _living(rules_in: Dictionary, side: Dictionary) -> int:
	var count := 0
	for row in side["field"] as Array:
		var unit: Dictionary = row
		if int(unit.get("hitpoints_milli", 0)) <= 0:
			continue
		if not AutoResolve.can_be_attacked(rules_in, String(unit.get("body", DEFAULT_BODY))):
			continue
		count += 1
	if count == 0 and not (side["reserve"] as Array).is_empty():
		return 1
	return count


## The shape `wotr_autoresolve.gd`'s leadership and targeting rules read. It
## wants `unitType`/`hitpoints`; the strategic records carry `unit_type`/
## `hitpoints_milli`. Translated here rather than renaming either, because the
## pure model is retail's reading and the strategic record is hashed state, and
## a shared field name would couple two things that change for different reasons.
static func _as_model_units(field: Array) -> Array:
	var out: Array = []
	for row in field:
		var unit: Dictionary = row
		out.append({
			"unitType": String(unit.get("unit_type", "")),
			"level": int(unit.get("level", 1)),
			"leadership": String(unit.get("leadership", "")),
			"body": String(unit.get("body", DEFAULT_BODY)),
			"combatChain": String(unit.get("combat_chain", DEFAULT_COMBAT_CHAIN)),
			"hitpoints": float(int(unit.get("hitpoints_milli", 0))),
		})
	return out


static func _plan(
	rules_in: Dictionary,
	attacking: Dictionary,
	defending: Dictionary,
	attacker_leadership: Dictionary,
	defender_leadership: Dictionary,
	seed_hex: String,
	round_index: int,
	role: String,
	detailed: bool
) -> Array[Dictionary]:
	var strikes: Array[Dictionary] = []
	var chosen := AutoResolve.choose_targets(rules_in,
		_as_model_units(attacking["field"]), _as_model_units(defending["field"]))
	# Sorted so the order strikes are planned in - and therefore the dice stream
	# names - never depends on Dictionary iteration order.
	var keys: Array[int] = []
	for key in chosen.keys():
		keys.append(int(key))
	keys.sort()
	for key in keys:
		var target_index := int(chosen[key])
		if target_index < 0:
			continue
		var attacker: Dictionary = (attacking["field"] as Array)[key]
		var defender: Dictionary = (defending["field"] as Array)[target_index]
		var modifiers := {
			"attackerHandicap": attacking["handicap"],
			"defenderHandicap": defending["handicap"],
			"attackerResource": attacking["resource"],
			"defenderResource": defending["resource"],
			"attackerScience": attacking["science"],
			"defenderScience": defending["science"],
		}
		var strengthening := defending.get("strengthening", {}) as Dictionary
		if float(strengthening.get("value", 1.0)) != 1.0:
			modifiers["targetStrengthening"] = strengthening
		if attacker_leadership.has(key):
			modifiers["attackerLeadership"] = attacker_leadership[key]
		if defender_leadership.has(target_index):
			modifiers["defenderLeadership"] = defender_leadership[target_index]
		# THE DICE STREAM. Every component is a value both peers already agree
		# on, and no component is a clock, a counter or an iteration order.
		var stream := "r%d|%s|u%d|t%d" % [round_index, role, key, target_index]
		var result := strike(rules_in, attacker, defender, modifiers, seed_hex, stream)
		var row := {
			"side": defending,
			"target": target_index,
			"damageMilli": int(result["damageMilli"]),
			"unresolved": result["unresolved"],
			"fallbacks": int((result["contest"] as Dictionary).get("fallbacks", 0)),
		}
		if detailed:
			row["working"] = {
				"round": round_index,
				"role": role,
				"attacker": String(attacker.get("template", "?")),
				"attackerType": String(attacker.get("unit_type", "")),
				"defender": String(defender.get("template", "?")),
				"defenderType": String(defender.get("unit_type", "")),
				"damageMilli": int(result["damageMilli"]),
				"factors": result["factors"],
				"contest": result["contest"],
			}
		strikes.append(row)
	return strikes


static func _regenerate(rules_in: Dictionary, side: Dictionary) -> void:
	for row in side["field"] as Array:
		var unit: Dictionary = row
		if int(unit.get("hitpoints_milli", 0)) <= 0:
			continue
		var found := AutoResolve.regenerate_at_level(
			rules_in, String(unit.get("body", DEFAULT_BODY)), int(unit.get("level", 1)))
		var amount := _to_milli(float(found["amount"]))
		if amount <= 0:
			continue
		unit["hitpoints_milli"] = mini(
			int(unit["hitpoints_milli"]) + amount, int(unit.get("max_hitpoints_milli", 0)))


## THE ATTRITION, which is what the strategic layer actually applies. Survivors
## carry their remaining hitpoints forward - that is what makes army strength a
## continuous quantity rather than a command-point integer - and the dead are
## named. A body retail marks `LeaveInArmySummary` stays in the roster at zero
## hitpoints, because retail says so ("even if dead, the unit should still
## appear in the army after the battle is over" - "Heroes never die in B4ME2").
static func _summarise(rules_in: Dictionary, side: Dictionary) -> Dictionary:
	var survivors: Array[Dictionary] = []
	var lost: Array[String] = []
	var kept: Array[String] = []
	for row in side["field"] as Array:
		var unit: Dictionary = row
		var name := String(unit.get("template", "<unnamed>"))
		if int(unit.get("hitpoints_milli", 0)) > 0:
			survivors.append(_survivor(unit, int(unit["hitpoints_milli"])))
			continue
		lost.append(name)
		if AutoResolve.leave_in_army_summary(rules_in, String(unit.get("body", DEFAULT_BODY))):
			kept.append(name)
			survivors.append(_survivor(unit, 0))
	for row in side["reserve"] as Array:
		var unit: Dictionary = row
		survivors.append(_survivor(unit, int(unit.get("hitpoints_milli", 0))))
	lost.sort()
	kept.sort()
	survivors.sort_custom(func(a, b): return String(a["template"]) < String(b["template"]))
	return {
		"survivors": survivors,
		"lost": PackedStringArray(lost),
		"keptInSummary": PackedStringArray(kept),
		"handicap": side["handicap"],
		"resource": side["resource"],
		"science": side["science"],
	}


static func _survivor(unit: Dictionary, hitpoints_milli: int) -> Dictionary:
	return {
		# WHICH STRATEGIC ARMY THIS UNIT BELONGS TO, carried through the whole
		# battle so the attrition can be written back to the right army record.
		# Without it the strategic layer would know only that six units died and
		# not whose they were.
		"army_id": int(unit.get("army_id", 0)),
		"template": String(unit.get("template", "<unnamed>")),
		"unit_type": String(unit.get("unit_type", "")),
		"body": String(unit.get("body", DEFAULT_BODY)),
		"combat_chain": String(unit.get("combat_chain", DEFAULT_COMBAT_CHAIN)),
		"leadership": String(unit.get("leadership", "")),
		"armor": String(unit.get("armor", DEFAULT_ARMOR)),
		"weapon": String(unit.get("weapon", DEFAULT_WEAPON)),
		"level": int(unit.get("level", 1)),
		"hitpoints_milli": hitpoints_milli,
		"max_hitpoints_milli": int(unit.get("max_hitpoints_milli", 0)),
	}
