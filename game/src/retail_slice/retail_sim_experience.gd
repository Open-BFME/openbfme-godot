extends RefCounted
## Experience/veterancy subsystem extracted from retail_slice_sim.gd (Q81
## strangler-fig extraction #3). Pure code move: XP state lives on entity
## rows and _unit_experience_rules stays on the sim; this class is stateless
## logic through the sim back-reference, and the sim keeps one-line
## delegates under the original names.
##
## Scope: spawn-time XP attachment, kill/own-guys-die awards, the award
## pipeline with EXPERIENCE modifiers, level-up effects, the King's Favor
## experience grant, and the read accessors. NOT here: CAH award tallies
## (_record_cah_member_kill stays a sim callback), banner carriers, hero
## rank attainment (sim callbacks).

# Weak back-reference: a strong ref would form a RefCounted cycle with the
# sim (which holds this subsystem), leaking freed sims as zombies — the
# script_wiring orphan-refusal contracts catch exactly that. The getter
# keeps plain `sim.` syntax working everywhere below.
var _sim_ref: WeakRef
var sim:
	get:
		return _sim_ref.get_ref()


func _init(owning_sim) -> void:
	_sim_ref = weakref(owning_sim)


func attach_experience_state(row: Dictionary) -> void:
	## Spawn-time XP pool for one entity. Fielded units enter at rank 1 with no
	## experience; retail summons whose chain starts at the top rank (ring
	## hero, Treebeard) enter at their authored rank. Produced (revived)
	## heroes restart at the chain's entry rank, matching retail.
	var rule: Dictionary = sim._unit_experience_rules.get(String(row.get("unit_type", "")), {})
	if rule.is_empty():
		return
	row["level"] = int(rule.get("initial_rank", 1))
	row["experience_xp"] = 0
	row["experience_max_level"] = int(rule.get("max_level", 1))
	for level_value in Array(rule.get("levels", [])):
		var level_row := level_value as Dictionary
		if int(level_row.get("rank", 0)) == int(row["level"]):
			apply_experience_level_effects(row, level_row)
			var receipts: Array[String] = []
			if not (level_row.get("selection_decal", {}) as Dictionary).is_empty():
				receipts.append("presentation_binding:SelectionDecal")
			if not (level_row.get("level_up_presentation", {}) as Dictionary).is_empty():
				receipts.append("presentation_binding:LevelUpPresentation")
			if not receipts.is_empty():
				row["experience_presentation_receipts"] = receipts


func experience_level_row(rule: Dictionary, rank: int) -> Dictionary:
	for row_value in Array(rule.get("levels", [])):
		var row := row_value as Dictionary
		if int(row.get("rank", 0)) == rank:
			return row
	return {}


func experience_state(entity_id: int) -> Dictionary:
	if not sim.entities.has(entity_id):
		return {}
	var row: Dictionary = sim.entities[entity_id]
	var rule: Dictionary = sim._unit_experience_rules.get(String(row.get("unit_type", "")), {})
	if rule.is_empty():
		return {}
	var level := int(row.get("level", 1))
	var state: Dictionary = {
		"level": level,
		"xp": int(row.get("experience_xp", 0)),
		"max_level": int(rule.get("max_level", 1)),
	}
	for row_value in Array(rule.get("levels", [])):
		var level_row := row_value as Dictionary
		if int(level_row.get("rank", 0)) > level:
			state["next_threshold"] = int(level_row.get("required_experience", 0))
			break
	return state


func experience_unauthored_victims() -> Array[String]:
	## Victim unit types whose kills paid the recorded default because retail
	## authors no ExperienceLevel chain for them (never an invented award).
	var output: Array[String] = []
	for value in sim._experience_unauthored_victims.keys():
		output.append(String(value))
	output.sort()
	return output


func award_member_kill_experience(attacker_id: int, target: Dictionary) -> void:
	# Award stats count at the lethal hook before XP's authored-evidence early
	# returns. A victim with no ExperienceLevel still counts as a retail kill.
	sim._record_cah_member_kill(attacker_id, target)
	if not sim.entities.has(attacker_id):
		return
	var attacker: Dictionary = sim.entities[attacker_id]
	if int(attacker.get("health", 0)) <= 0:
		return
	var victim_rule: Dictionary = sim._unit_experience_rules.get(String(target.get("unit_type", "")), {})
	if victim_rule.is_empty():
		sim._experience_unauthored_victims[String(target.get("unit_type", ""))] = true
		return
	var victim_level := clampi(
		int(target.get("level", 1)),
		int(victim_rule.get("initial_rank", 1)),
		int(victim_rule.get("max_level", 1))
	)
	var victim_level_row := experience_level_row(victim_rule, victim_level)
	if victim_level_row.get("experience_award_known") == false:
		sim._experience_unauthored_victims[String(target.get("unit_type", ""))] = true
		return
	var award := int(victim_level_row.get("experience_award", 0))
	if award <= 0:
		return
	award_experience(attacker, award)


func award_experience(row: Dictionary, amount: int) -> void:
	var rule: Dictionary = sim._unit_experience_rules.get(String(row.get("unit_type", "")), {})
	if rule.is_empty() or amount <= 0 or int(row.get("health", 0)) <= 0:
		return
	# Leadership EXPERIENCE modifiers (and any timed EXPERIENCE buff) scale
	# the recipient's XP intake — retail's 200% leadership experience rule.
	var experience_factor: float = sim._timed_modifier_product(row, "EXPERIENCE")
	if experience_factor != 1.0:
		amount = maxi(1, roundi(float(amount) * experience_factor))
	var max_level := int(rule.get("max_level", 1))
	var level := int(row.get("level", 1))
	# The pool keeps counting at the authored cap (retail's tracker never
	# stops); only the level-ups stop at the last authored rank.
	var xp := int(row.get("experience_xp", 0)) + amount
	row["experience_xp"] = xp
	# Authored thresholds are cumulative; the next rank is the smallest
	# authored rank above the live one (chains are not always 1..N).
	while level < max_level:
		var next_row: Dictionary = {}
		for row_value in Array(rule.get("levels", [])):
			var candidate := row_value as Dictionary
			if int(candidate.get("rank", 0)) > level:
				next_row = candidate
				break
		if next_row.is_empty() or xp < int(next_row.get("required_experience", 0)):
			break
		level = int(next_row.get("rank", level + 1))
		apply_experience_level_effects(row, next_row)
	if level != int(row.get("level", 1)):
		row["level"] = level
		sim._refresh_banner_carrier_state(row)
		sim._record_hero_rank_attainment(row)
		sim._emit_event("battalion.level_up", int(row.get("id", 0)), 0, {
			"team": int(row.get("team", -1)),
			"unit_type": String(row.get("unit_type", "")),
			"level": level,
		})


func apply_experience_level_effects(row: Dictionary, level_row: Dictionary) -> void:
	## SAGE level modifiers are permanent: additive kinds fold into the
	## per-member base stat (living members' current health rises by the same
	## authored amount), exactly the authored magnitudes, never scaled.
	var health_add := roundi(float(level_row.get("health_add", 0.0)))
	var damage_add := roundi(float(level_row.get("damage_add", 0.0)))
	var damage_multiplier := float(level_row.get("damage_multiplier", 1.0))
	var spell_damage_multiplier := float(level_row.get("spell_damage_multiplier", 1.0))
	if health_add != 0:
		row["member_maximum_health"] = int(row.get("member_maximum_health", 0)) + health_add
		row["maximum_health"] = int(row["member_maximum_health"]) * int(row.get("member_count", 1))
		var health_values: Array = row.get("member_health", [])
		for index in range(health_values.size()):
			if int(health_values[index]) > 0:
				health_values[index] = mini(int(row["member_maximum_health"]), int(health_values[index]) + health_add)
		row["member_health"] = health_values
		var aggregate := 0
		for value in health_values:
			aggregate += int(value)
		row["health"] = aggregate
	if damage_add != 0 or damage_multiplier != 1.0:
		row["member_damage"] = roundi(
			float(int(row.get("member_damage", 0)) + damage_add)
			* damage_multiplier
		)
		row["damage"] = int(row["member_damage"]) * int(row.get("member_count", 1))
	if spell_damage_multiplier != 1.0:
		row["spell_damage_multiplier"] = (
			float(row.get("spell_damage_multiplier", 1.0))
			* spell_damage_multiplier
		)
	var applied: Dictionary = row.get("applied_upgrades", {}) as Dictionary
	for upgrade_value in Array(level_row.get("upgrades", [])):
		var upgrade_id := String(upgrade_value)
		if upgrade_id != "":
			applied[upgrade_id] = sim.tick_index
	row["applied_upgrades"] = applied


func award_own_guys_die_experience(row: Dictionary, defeated_count: int) -> void:
	## ExperienceAwardOwnGuysDie belongs to the victim horde's current level:
	## each of its own member deaths feeds its surviving tracker. A completely
	## defeated horde has no living tracker to receive the award.
	if defeated_count <= 0 or int(row.get("health", 0)) <= 0:
		return
	var rule := sim._unit_experience_rules.get(String(row.get("unit_type", "")), {}) as Dictionary
	if rule.is_empty():
		return
	var level_row := experience_level_row(rule, int(row.get("level", 1)))
	if not level_row.has("experience_award_own_guys_die"):
		return
	var per_member := maxi(0, int(level_row.get("experience_award_own_guys_die", 0)))
	if per_member <= 0:
		return
	var amount := per_member * defeated_count
	award_experience(row, amount)
	sim._emit_event("experience.own_guys_die", int(row.get("id", 0)), 0, {"members": defeated_count, "amount": amount})


func apply_ability_experience_grant(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	## LevelGrantSpecialPower (King's Favor / Train Allies): every allied
	## battalion inside the authored RadiusEffect of the target point passing
	## the authored AcceptanceFilter gains exactly the authored Experience
	## through the normal experience pipeline (EXPERIENCE modifiers included).
	## Only recipients with a converted ExperienceLevel chain count — a unit
	## retail never authored a chain for is never granted an invented award.
	var team := int(hero_row.get("team", -1))
	var amount := int(effect.get("experience", 0))
	var radius := float(effect.get("radius_scaled", 0.0))
	if amount <= 0 or radius <= 0.0:
		return {"ok": false, "reason": "experience-grant-fields-missing"}
	var filter_text := String(effect.get("affects", ""))
	var granted := 0
	for id in sim.living_ids(team):
		var ally: Dictionary = sim.entities[id]
		if Vector2(ally.get("position", Vector2.ZERO)).distance_to(point) > radius:
			continue
		if not sim._ability_filter_accepts(ally, filter_text):
			continue
		if (sim._unit_experience_rules.get(String(ally.get("unit_type", "")), {}) as Dictionary).is_empty():
			continue
		award_experience(ally, amount)
		granted += 1
	if granted == 0:
		return {"ok": false, "reason": "no-eligible-allies-in-radius"}
	return {"ok": true, "reason": "", "effect": "experience-grant", "affected": granted}
