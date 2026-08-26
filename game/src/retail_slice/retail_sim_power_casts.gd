extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Power casting carved out of retail_sim_powers.gd (crack-2 finish): cast_power dispatch, every _cast_spellbook_* effect, weather/field-ping/summon/grove steppers, pending power effects, script-object spawns.
## State stays on the sim; the sim keeps one-line delegates under the original names.



func _cast_spellbook_scavenger(team: int, effect: Dictionary) -> Dictionary:
	var _sim = sim
	var percent := float(effect.get("bounty_percent", -1.0))
	if not _sim._is_combatant_team(team) or not is_finite(percent) or percent < 0.0:
		return {"ok": false, "reason": "invalid-scavenger-contract"}
	_sim._scavenger_bounty_percent[team] = percent
	return {"ok": true, "bounty_percent": percent}


func cast_power(team: int, power_id: String, point: Vector2) -> Dictionary:
	var _sim = sim
	var tree = _sim._team_tree(team)
	if not bool(tree.get("ready", false)):
		return {"ok": false, "reason": "spellbook-unavailable"}
	var row: Dictionary = (tree.get("powers", {}) as Dictionary).get(power_id, {}) as Dictionary
	if row.is_empty():
		return {"ok": false, "reason": "unknown-power"}
	if not _sim.has_power(team, power_id):
		return {"ok": false, "reason": "power-not-purchased"}
	if not bool(row.get("castable", false)):
		return {"ok": false, "reason": "effect-unsupported", "detail": String(row.get("locked_reason", ""))}
	if bool(row.get("nonpressable", false)) and (_sim._consumed_nonpressable_powers.get(team, {}) as Dictionary).has(power_id):
		return {"ok": false, "reason": "power-already-activated"}
	var cooldown = _sim.power_cooldown_state(team, power_id)
	if int(cooldown.get("remaining_ticks", 0)) > 0:
		return {"ok": false, "reason": "power-recharging", "remaining_ticks": int(cooldown.get("remaining_ticks", 0))}
	var effect: Dictionary = row.get("effect", {}) as Dictionary
	var result := {"ok": false, "reason": "effect-unsupported"}
	match String(effect.get("kind", "")):
		"heal":
			result = _cast_spellbook_heal(team, effect, point)
		"attribute_modifier":
			result = _cast_spellbook_attribute_modifier(team, effect, point)
		"fire_weapon":
			result = _cast_spellbook_fire_weapon(team, effect, point)
		"summon":
			result = _cast_spellbook_summon(team, effect, point)
		"structure_summon":
			result = _cast_spellbook_structure_summon(team, effect, point)
		"grove_aura":
			result = _cast_spellbook_grove(team, effect, point)
		"field_ping":
			result = _cast_spellbook_field_ping(team, power_id, effect, point)
		"cloudbreak_stun":
			result = _cast_spellbook_cloudbreak(team, effect, point)
		"weather_modifier":
			result = _cast_spellbook_weather_modifier(team, power_id, effect)
		"weather_anticategory":
			result = _cast_spellbook_weather_anticategory(team, power_id, effect)
		"creep_allegiance":
			result = _cast_spellbook_creep_allegiance(team, effect, point)
		"scavenger_bounty":
			result = _cast_spellbook_scavenger(team, effect)
	if not bool(result.get("ok", false)):
		return result
	if bool(row.get("nonpressable", false)):
		(_sim._consumed_nonpressable_powers[team] as Dictionary)[power_id] = true
	(_sim._power_cooldown_until[team] as Dictionary)[power_id] = _sim.tick_index + _sim.spell_recharge_ticks_for_team(
		team, int(row.get("reload_ticks", 1))
	)
	# A staged pick that gets cast is spent: RESET can no longer refund it.
	var staged: Array = _sim._staged_purchases[team]
	for index in range(staged.size() - 1, -1, -1):
		if String((staged[index] as Dictionary).get("power_id", "")) == power_id:
			staged.remove_at(index)
	_sim._emit_event("power.cast", 0, 0, {
		"team": team,
		"power_id": power_id,
		"science_id": String(row.get("science_id", "")),
		"sound_id": String(row.get("sound_id", "")),
		"effect_kind": String(effect.get("kind", "")),
		"radius_source": float(effect.get("radius_source", effect.get("range_source", 0.0))),
		# Map-scaled twin of radius_source so the presentation cue can cover the
		# ground the power actually affected without re-deriving the scale.
		"fx_radius": snappedf(
			float(effect.get("radius_source", effect.get("range_source", 0.0))) * _sim._spellbook_world_scale(),
			0.001
		),
		"fx_lists": row.get("fx_lists", []),
		"ocls": row.get("ocls", []),
		"battalions": int(result.get("battalions", 0)),
		"point": [snappedf(point.x, 0.001), snappedf(point.y, 0.001)],
	})
	return result


func cast_heal(team: int, point: Vector2) -> Dictionary:
	return cast_power(team, "SpellBookHeal", point)


func cast_rally(team: int, point: Vector2) -> Dictionary:
	return cast_power(team, "SpellBookRallyingCall", point)


func _spellbook_object_kinds(row: Dictionary) -> Array:
	## Maps a battalion row onto the doc's object-kind vocabulary for the
	## authored HealAffects / AttributeModifierAffects filters.
	var kinds: Array = ["ANY"]
	if String(row.get("horde_id", "")) != "":
		kinds.append("HORDE")
	var category := String(row.get("category", ""))
	match category:
		"hero":
			kinds.append("HERO")
		"cavalry":
			kinds.append("CAVALRY")
		"infantry", "ranged-infantry":
			kinds.append("INFANTRY")
	if bool(row.get("is_builder", false)):
		kinds.append("DOZER")
	return kinds


func _spellbook_affects(row: Dictionary, filter_text: String) -> bool:
	## SAGE object-filter subset: space-separated ANY/+include/-exclude terms.
	var kinds := _spellbook_object_kinds(row)
	var included := false
	for term_value in filter_text.split(" ", false):
		var term := String(term_value)
		if term == "" or term == "NONE":
			continue
		if term == "ENEMIES" or term == "ALLIES":
			continue
		if term.begins_with("-"):
			if kinds.has(term.trim_prefix("-")):
				return false
		elif term.begins_with("+"):
			if kinds.has(term.trim_prefix("+")):
				included = true
		elif term == "ANY":
			included = true
		elif term == "ALL":
			# `ALL` is the universal set ONLY when the filter authors no including
			# kind term of its own — retail's `ALL ENEMIES` (Freezing Rain) is a
			# relation-only filter and means everyone. Every OTHER `ALL` in this
			# corpus sits beside kind terms, where the filter reads conjunctively;
			# a blanket include there would silently widen it to the whole board.
			if not _spellbook_filter_has_kind_terms(filter_text):
				included = true
		elif kinds.has(term):
			included = true
	return included


func _spellbook_filter_has_kind_terms(filter_text: String) -> bool:
	## True when the filter names at least one INCLUDING object-kind term.
	## Relation words, NONE, the universal words, and `-` exclusions do not
	## count: `ALL -WildBabyDrake ENEMIES` is still a relation-only filter with
	## one carve-out, so `ALL` there is still universal.
	for term_value in filter_text.split(" ", false):
		var term := String(term_value)
		if term in ["", "NONE", "ANY", "ALL", "ALLIES", "ENEMIES"] or term.begins_with("-"):
			continue
		return true
	return false


func _cast_spellbook_heal(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	var _sim = sim
	var radius = float(effect.get("radius_source", 0.0)) * _sim._spellbook_world_scale()
	if not bool(effect.get("as_percent", true)):
		return _cast_spellbook_structure_heal(team, effect, point, radius)
	var fraction := float(effect.get("amount", 0.5))
	var healed := 0
	for id in _sim.living_ids(team):
		var row: Dictionary = _sim.entities[id]
		if Vector2(row.get("position", Vector2.ZERO)).distance_to(point) > radius:
			continue
		if not _spellbook_affects(row, String(effect.get("affects", ""))):
			continue
		var maximum_member := int(row.get("member_maximum_health", 1))
		var heal_amount := maxi(1, roundi(float(maximum_member) * fraction))
		var health_values: Array = row.get("member_health", [])
		var restored := 0
		for member_index in health_values.size():
			var current := int(health_values[member_index])
			if current > 0 and current < maximum_member:
				restored += mini(maximum_member - current, heal_amount)
				health_values[member_index] = mini(maximum_member, current + heal_amount)
		if restored > 0:
			row["member_health"] = health_values
			row["health"] = 0
			for value in health_values:
				row["health"] = int(row["health"]) + int(value)
			healed += 1
	if healed == 0:
		return {"ok": false, "reason": "no-wounded-allies-in-range"}
	return {"ok": true, "reason": "", "battalions": healed}


func _cast_spellbook_structure_heal(team: int, effect: Dictionary, point: Vector2, radius: float) -> Dictionary:
	## Rebuild-class heal: the authored flat amount restores matching
	## sim.structures in radius (HealAsPercent No + STRUCTURE affects filter).
	var _sim = sim
	var affects := String(effect.get("affects", ""))
	if not affects.contains("STRUCTURE"):
		return {"ok": false, "reason": "no-wounded-allies-in-range"}
	var amount := int(effect.get("amount", 0))
	var healed := 0
	for structure_id in _sim.structure_ids(team):
		var structure: Dictionary = _sim.structures[structure_id]
		var health := int(structure.get("health", 0))
		var maximum := int(structure.get("maximum_health", 1))
		if health <= 0 or health >= maximum:
			continue
		if Vector2(structure.get("position", Vector2.ZERO)).distance_to(point) > radius:
			continue
		structure["health"] = mini(maximum, health + amount)
		healed += 1
	if healed == 0:
		return {"ok": false, "reason": "no-wounded-allies-in-range"}
	return {"ok": true, "reason": "", "battalions": healed}


func _cast_spellbook_attribute_modifier(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	var _sim = sim
	var range_sim = float(effect.get("range_source", 0.0)) * _sim._spellbook_world_scale()
	var duration_ticks := int(effect.get("duration_ticks", 1))
	var damage_mult := float(effect.get("damage_mult", 1.0))
	# DOES THIS PAYLOAD ACTUALLY CARRY A COMBAT BUFF? `damage_mult` defaults to a
	# neutral 1.0 when the leaf authors no DAMAGE_MULT row, so a PRODUCTION-only
	# power (Industry, Dwarven Riches, Blight, Snowbind) reached the writes below
	# with 1.0 and STOMPED whatever rally the target already had.
	#
	# MEASURED, and it is a real cancel rather than a no-op: in the spellbook
	# matrix, casting mordor/SpellBookIndustry moved an ally from
	# rally_until_tick 3601 / rally_damage_mult 1.5 to 3001 / 1.0 — a live 150%
	# rally buff ended early by a power whose entire authored payload is an
	# economy multiplier (workspace/scratch/opus29-spellbook-A.out.log).
	#
	# `authored_rows` is what makes this checkable rather than guessable: it
	# distinguishes a leaf that authored DAMAGE_MULT 100% (a deliberate neutral
	# buff, which still refreshes a window) from one that authored no DAMAGE_MULT
	# row at all. Only the latter is skipped.
	var authored_rows: Dictionary = effect.get("authored_rows", {}) as Dictionary
	# Absent table = an effect compiled before authored_rows existed; fall back to
	# the old unconditional behaviour rather than silently dropping a real buff.
	var carries_rally: bool = (
		not authored_rows.has("DAMAGE_MULT") or bool(authored_rows.get("DAMAGE_MULT", false))
	)
	var rallied := 0
	for id in _sim.living_ids(team):
		var row: Dictionary = _sim.entities[id]
		if Vector2(row.get("position", Vector2.ZERO)).distance_to(point) > range_sim:
			continue
		# Aura-style filters (GENERIC_BUFF_RECIPIENT_OBJECT_FILTER) exclude the
		# HORDE container but include its members; evaluate member kinds so the
		# battalion-as-container distinction does not reject infantry hordes.
		if not _spellbook_member_affects(
			row, String(effect.get("affects", "")), true
		):
			continue
		if carries_rally:
			row["rally_until_tick"] = _sim.tick_index + duration_ticks
			row["rally_damage_mult"] = damage_mult
		# Counted either way: the filter matched, so the cast found its targets
		# and succeeds. A production-only power stays castable-but-inert, which is
		# exactly what the spellbook matrix documents it as — it simply no longer
		# cancels an unrelated buff on its way to doing nothing.
		rallied += 1
	if rallied == 0:
		return {"ok": false, "reason": "no-allies-in-range"}
	return {"ok": true, "reason": "", "battalions": rallied}


func _cast_spellbook_fire_weapon(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	## Volley/quake receptacles: the authored fire delays schedule the strikes;
	## each damage nugget detonates at cast point with its converted payload.
	var _sim = sim
	var strikes: Array = effect.get("strikes", []) as Array
	if strikes.is_empty():
		return {"ok": false, "reason": "no-strikes"}
	for strike_value in strikes:
		var strike := strike_value as Dictionary
		_sim._pending_power_effects.append({
			"kind": "strike",
			"fire_tick": _sim.tick_index + maxi(0, roundi(float(strike.get("delay_ms", 0.0)) / (_sim.TICK_SECONDS * 1000.0))),
			"team": team,
			"point": point,
			"damage": float(strike.get("damage", 0.0)),
			"radius_source": float(strike.get("radius_source", 0.0)),
			"damage_type": String(strike.get("damage_type", "")),
			"affects": String(strike.get("affects", "ENEMIES")),
		})
	return {"ok": true, "reason": "", "battalions": 0, "strikes": strikes.size()}


func _cast_spellbook_summon(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	## Summon eggs hatch after the authored destruction delay into battalions
	## whose stats and summon lifetime all come from the converted leaves. Pool
	## choices are made now, once per cast; configuration consumes no RNG bytes.
	var _sim = sim
	var targets: Array = effect.get("targets", []) as Array
	if effect.has("target_groups"):
		targets = _spellbook_resolve_summon_targets(
			Array(effect.get("target_groups", []))
		)
	if targets.is_empty():
		return {"ok": false, "reason": "no-summon-targets"}
	_sim._pending_power_effects.append({
		"kind": "summon",
		"fire_tick": _sim.tick_index + int(effect.get("hatch_delay_ticks", 0)),
		"team": team,
		"point": point,
		"targets": targets,
	})
	var queued_count := _terminal_visible_summon_count(targets)
	return {"ok": true, "reason": "", "battalions": 0, "summon_count": queued_count}


func _terminal_visible_summon_count(targets: Array) -> int:
	## The effect compiler has already followed converted egg/hatch chains, so
	## these rows are the terminal battalions/sim.entities that will materialize.
	## Never count intermediary OCL payload rows or horde members as HUD units.
	var count := 0
	for target_value in targets:
		if typeof(target_value) != TYPE_DICTIONARY:
			continue
		var target := target_value as Dictionary
		if (target.get("rule", {}) as Dictionary).is_empty():
			continue
		count += maxi(1, int(target.get("count", 1)))
	return count


func _spellbook_resolve_summon_targets(target_groups: Array) -> Array:
	var resolved: Array = []
	for group_value in target_groups:
		if typeof(group_value) != TYPE_DICTIONARY:
			continue
		var group := group_value as Dictionary
		var choices: Array = group.get("choices", []) as Array
		if choices.is_empty():
			continue
		var counts: Dictionary = {}
		for _pick in range(maxi(0, int(group.get("pick_count", 0)))):
			var choice_index := 0
			if choices.size() > 1:
				choice_index = sim.logic_random_int(0, choices.size() - 1)
			var choice := choices[choice_index] as Dictionary
			var object_id := String(choice.get("object_id", ""))
			counts[object_id] = int(counts.get(object_id, 0)) + 1
		for choice_value in choices:
			var choice := choice_value as Dictionary
			var object_id := String(choice.get("object_id", ""))
			var count := int(counts.get(object_id, 0))
			if count <= 0:
				continue
			var target := choice.duplicate(true)
			target["count"] = count
			resolved.append(target)
	return resolved


func _cast_spellbook_structure_summon(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	## Lone Tower: the structure rises at the target point over the authored
	## just-built duration, then fires its converted bow on enemies in range.
	var _sim = sim
	var structure_id = _sim._next_dynamic_structure_id
	_sim._next_dynamic_structure_id += 1
	var build_ticks := int(effect.get("build_ticks", 1))
	var health := int(effect.get("health", 1))
	var weapon: Dictionary = effect.get("weapon", {}) as Dictionary
	_sim._note_structure_table_mutation()
	_sim.structures[structure_id] = {
		"id": structure_id,
		"team": team,
		"kind": "structure",
		"structure_kind": "lone_tower",
		"name": "Lone Tower",
		"position": point,
		"rally": point,
		"health": health,
		"maximum_health": health,
		"construction_progress": 0.0,
		"construction_elapsed_ticks": 0,
		"construction_build_ticks": build_ticks,
		"level": 1,
		"completed_upgrades": [],
		"upgrade_queue": [],
		"production": [],
		"queue": [],
		"damage_remainders": {},
		"income_per_payout": 0,
		"summon_object_id": String(effect.get("object_id", "")),
		"attack": {
			"damage": float(weapon.get("damage", 0.0)),
			"range": float(weapon.get("range_source", 0.0)) * _sim._spellbook_world_scale(),
			"damage_type": String(weapon.get("damage_type", "")),
			"period_ticks": maxi(1, roundi(float(weapon.get("period_ms", 0.0)) / (_sim.TICK_SECONDS * 1000.0))),
			"pre_attack_ticks": maxi(0, roundi(float(weapon.get("pre_attack_ms", 0.0)) / (_sim.TICK_SECONDS * 1000.0))),
			"cooldown": 0,
			"affects": String(weapon.get("affects", "ENEMIES")),
		},
	}
	_sim._apply_structure_inherit_upgrades(_sim.structures[structure_id] as Dictionary)
	_sim._initialize_structure_auto_deposit(_sim.structures[structure_id] as Dictionary)
	_sim._emit_event("power.structure_summon", 0, structure_id, {"team": team, "object_id": String(effect.get("object_id", "")), "build_ticks": build_ticks, "health": health})
	return {"ok": true, "reason": "", "battalions": 0}


func _cast_spellbook_grove(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	var _sim = sim
	_sim._active_groves.append({
		"team": team,
		"point": point,
		"range_sim": float(effect.get("range_source", 0.0)) * _sim._spellbook_world_scale(),
		"armor_mult": float(effect.get("armor_mult", 1.0)),
		"damage_mult": float(effect.get("damage_mult", 1.0)),
		"buff_duration_ticks": int(effect.get("buff_duration_ticks", 1)),
		"despawn_tick": _sim.tick_index + int(effect.get("lifetime_ticks", 1)),
		"filter": String(effect.get("filter", "")),
		"terrain_condition": String(effect.get("terrain_condition", "")),
		# The AUTHORED leaf id, so the accumulator key below is per-modifier-list.
		"modifier": String(effect.get("modifier", "")),
	})
	var grove_trees: Dictionary = effect.get("trees", {}) as Dictionary
	_sim._emit_event("power.grove", 0, 0, {
		"team": team,
		"point": [snappedf(point.x, 0.001), snappedf(point.y, 0.001)],
		"lifetime_ticks": int(effect.get("lifetime_ticks", 0)),
		# Retail's terrain cell type (TAINT / ELVEN_WOOD) the ground is painted
		# with for the object's lifetime; "" when the leaf does not author one.
		"terrain_condition": String(effect.get("terrain_condition", "")),
		"terrain_object_id": String(effect.get("terrain_object_id", "")),
		"radius_source": float(effect.get("range_source", 0.0)),
		# The presenter plants the authored tree object; an empty block means
		# the chain did not convert and nothing is drawn.
		"trees": grove_trees.duplicate(true),
	})
	return {"ok": true, "reason": "", "battalions": 0}


func _cast_spellbook_field_ping(team: int, power_id: String, effect: Dictionary, point: Vector2) -> Dictionary:
	## Drop the authored ping at the cast point. It has no body the sim can shoot
	## and never moves, so it lives in its own registry rather than as an entity:
	## a bounded reveal region plus the authored auras, expiring on the leaf's
	## LifetimeUpdate. Deterministic — no RNG, no wall clock.
	var _sim = sim
	var scale = _sim._spellbook_world_scale()
	var reveal_source := float(effect.get("reveal_radius_source", 0.0))
	var ping_sequence = _sim._field_pings.size()
	_sim._field_pings.append({
		"team": team,
		"power_id": power_id,
		"object_id": String(effect.get("object_id", "")),
		"point": point,
		"reveal_radius_source": reveal_source,
		"reveal_radius_sim": reveal_source * scale,
		"expire_tick": _sim.tick_index + int(effect.get("lifetime_ticks", 1)),
		"auras": (effect.get("auras", []) as Array).duplicate(true),
		"invisibility_updates": (effect.get("invisibility_updates", []) as Array).duplicate(true),
		"invisibility_source_prefix": "field-ping:%d:%s:%d:%d" % [team, power_id, _sim.tick_index, ping_sequence],
	})
	_sim._emit_event("power.field_ping", 0, 0, {
		"team": team,
		"power_id": power_id,
		"object_id": String(effect.get("object_id", "")),
		"point": [snappedf(point.x, 0.001), snappedf(point.y, 0.001)],
		"reveal_radius_source": reveal_source,
		"reveal_radius": snappedf(reveal_source * scale, 0.001),
		"lifetime_ticks": int(effect.get("lifetime_ticks", 0)),
		"auras": (effect.get("auras", []) as Array).size(),
	})
	return {"ok": true, "reason": "", "battalions": 0}


func team_revealed_regions(team: int) -> Array:
	## Per-team shroud-reveal registry the presentation consumes. The sim has no
	## fog-of-war model of its own, so this is the whole reveal contract: every
	## live ping owned by `team` that authors a VisionRange, in cast order, with
	## the tick it lapses. Presentation work still outstanding: drawing the
	## reveal (no fog layer exists to lift) and StealthDetectorUpdate unmasking,
	## which is a converter gap named on the effect.
	var regions: Array = []
	for ping in sim._field_pings:
		if int(ping.get("team", -1)) != team:
			continue
		if float(ping.get("reveal_radius_source", 0.0)) <= 0.0:
			continue
		regions.append({
			"point": Vector2(ping.get("point", Vector2.ZERO)),
			"radius_sim": float(ping.get("reveal_radius_sim", 0.0)),
			"radius_source": float(ping.get("reveal_radius_source", 0.0)),
			"expire_tick": int(ping.get("expire_tick", -1)),
			"power_id": String(ping.get("power_id", "")),
			"object_id": String(ping.get("object_id", "")),
		})
	return regions


func field_ping_count() -> int:
	return sim._field_pings.size()


func _step_field_pings() -> void:
	## Expire lapsed pings, then refresh each live ping's authored auras on its
	## own cadence. Iteration follows cast order and the spatial gather is
	## already sorted, so the pass is lockstep-deterministic.
	var _sim = sim
	if _sim._field_pings.is_empty():
		return
	var living: Array[Dictionary] = []
	for ping in _sim._field_pings:
		if _sim.tick_index >= int(ping.get("expire_tick", -1)):
			_revoke_field_ping_invisibility(ping)
			continue
		living.append(ping)
		var team := int(ping.get("team", -1))
		var origin := Vector2(ping.get("point", Vector2.ZERO))
		for aura_value in Array(ping.get("auras", [])):
			var aura := aura_value as Dictionary
			if Array(aura.get("modifiers", [])).is_empty():
				# Authored marker aura (PalantirVision): no stat rows in retail,
				# so nothing is applied. Kept on the effect as evidence, not
				# silently invented into a buff.
				continue
			if _sim.tick_index % maxi(1, int(aura.get("refresh_ticks", 1))) != 0:
				continue
			var radius = float(aura.get("range_source", 0.0)) * _sim._spellbook_world_scale()
			for target_id in _sim._spatial_gather_sorted(origin, radius):
				if not _sim.entities.has(target_id):
					continue
				var target: Dictionary = _sim.entities[target_id]
				if int(target.get("health", 0)) <= 0:
					continue
				var same_team := int(target.get("team", -1)) == team
				if Vector2(target.get("position", Vector2.ZERO)).distance_to(origin) > radius:
					continue
				if not _summon_aura_allows_relation(aura, same_team):
					continue
				if not _spellbook_member_affects(
					target, String(aura.get("filter", "")), same_team
				):
					continue
				_sim._set_timed_modifier(
					target,
					"field-ping:%s:%s" % [String(ping.get("object_id", "")), String(aura.get("id", ""))],
					Array(aura.get("modifiers", [])),
					_sim.tick_index + int(aura.get("duration_ticks", 1))
				)
		for policy_value in ping.get("invisibility_updates", []) as Array:
			var policy := policy_value as Dictionary
			if _sim.tick_index < int(policy.get("next_update_tick", 0)): continue
			policy["next_update_tick"] = _sim.tick_index + maxi(1, int(policy.get("update_ticks", 1)))
			var source_key := "%s:%s" % [String(ping.get("invisibility_source_prefix", "field-ping")), String(policy.get("tag", ""))]
			var prior := policy.get("granted_ids", []) as Array; var desired: Array[int] = []
			var radius = float(policy.get("broadcast_range_source", 0.0)) * _sim._spellbook_world_scale(); var filter_tokens = policy.get("broadcast_filter", []) as Array
			if bool(policy.get("enabled", false)):
				for target_id in _sim.entity_ids():
					var target := _sim.entities[target_id] as Dictionary
					if int(target.get("health", 0)) <= 0 or int(target.get("team", -1)) != team: continue
					if origin.distance_to(Vector2(target.get("position", Vector2.ZERO))) > radius or not _sim._transport_filter_accepts(target, filter_tokens): continue
					desired.append(target_id); _sim._set_invisibility_source(target, source_key, policy, true, 0)
			for target_id in prior:
				if not desired.has(int(target_id)) and _sim.entities.has(int(target_id)): _sim._set_invisibility_source(_sim.entities[int(target_id)] as Dictionary, source_key, policy, false, 0)
			policy["granted_ids"] = desired
	_sim._field_pings = living


func _revoke_field_ping_invisibility(ping: Dictionary) -> void:
	var _sim = sim
	for policy_value in ping.get("invisibility_updates", []) as Array:
		var policy := policy_value as Dictionary
		var source_key := "%s:%s" % [String(ping.get("invisibility_source_prefix", "field-ping")), String(policy.get("tag", ""))]
		for target_id in policy.get("granted_ids", []) as Array:
			if _sim.entities.has(int(target_id)): _sim._set_invisibility_source(_sim.entities[int(target_id)] as Dictionary, source_key, policy, false, 0)
		policy["granted_ids"] = []


func _cast_spellbook_cloudbreak(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	## Cloud Break: enemy units matching the authored filter are disrupted for
	## the weather duration (SPELL_CLOUDBREAK_DURATION).
	var _sim = sim
	var duration_ticks := int(effect.get("duration_ticks", 1))
	var stunned := 0
	for id in _sim.living_ids(1 - team):
		var row: Dictionary = _sim.entities[id]
		if not _spellbook_affects(row, String(effect.get("affects", ""))):
			continue
		row["stun_until_tick"] = _sim.tick_index + duration_ticks
		row["route"] = []
		row["route_cells"] = []
		row["target_id"] = 0
		row["state"] = "idle"
		stunned += 1
	_revoke_opposing_weather_for_cloudbreak(team)
	_sim._emit_event("power.cloudbreak", 0, 0, {
		"team": team,
		"weather": String(effect.get("weather", "")),
		"stunned": stunned,
		"duration_ticks": duration_ticks,
		"sunbeam_object_id": String(effect.get("sunbeam_object_id", "")),
		"object_spacing_source": float(effect.get("object_spacing_source", 0.0)),
	})
	return {"ok": true, "reason": "", "battalions": stunned}


func _revoke_opposing_weather_for_cloudbreak(team: int) -> void:
	var _sim = sim
	var retained: Array[Dictionary] = []
	for entry in _sim._weather_effects:
		if int(entry.get("team", -1)) == team:
			retained.append(entry)
			continue
		match String(entry.get("kind", "")):
			"weather_modifier":
				var key := String(entry.get("source_key", ""))
				for id in _sim.entity_ids():
					(_sim.entities[id].get("timed_modifiers", {}) as Dictionary).erase(key)
			"weather_anticategory":
				for id in _sim.entity_ids():
					_erase_leadership_suppression_source(
						_sim.entities[id], String(entry.get("source_key", ""))
					)
			_:
				retained.append(entry)
	_sim._weather_effects = retained


func _cast_spellbook_weather_modifier(team: int, power_id: String, effect: Dictionary) -> Dictionary:
	## Darkness. Global, no cast point: the whole map is under the weather for
	## WeatherDuration, and every unit the authored filter accepts carries the
	## modifier leaf's rows for exactly that window.
	var _sim = sim
	var expire_tick = _sim.tick_index + int(effect.get("duration_ticks", 1))
	var source_key := "weather:%d:%s:%d" % [team, power_id, _sim.tick_index]
	var entry := {
		"kind": "weather_modifier",
		"team": team,
		"power_id": power_id,
		"source_key": source_key,
		"modifier_id": String(effect.get("modifier_id", "")),
		"modifiers": (effect.get("modifiers", []) as Array).duplicate(true),
		"affects": String(effect.get("affects", "")),
		"weather": String(effect.get("weather", "")),
		"expire_tick": expire_tick,
	}
	_sim._weather_effects.append(entry)
	var affected := _apply_weather_modifier(entry)
	_sim._emit_event("power.weather", 0, 0, {
		"team": team,
		"power_id": power_id,
		"kind": "weather_modifier",
		"weather": String(effect.get("weather", "")),
		"modifier_id": String(effect.get("modifier_id", "")),
		"duration_ticks": int(effect.get("duration_ticks", 0)),
		"expire_tick": expire_tick,
		"affected": affected,
	})
	return {"ok": true, "reason": "", "battalions": affected}


func _cast_spellbook_weather_anticategory(team: int, power_id: String, effect: Dictionary) -> Dictionary:
	## Freezing Rain. Global anti-LEADERSHIP: every enemy the authored filter
	## accepts loses its leadership grants for the weather window, reusing the
	## same suppression field the Horn of Gondor strip writes.
	var _sim = sim
	var expire_tick = _sim.tick_index + int(effect.get("duration_ticks", 1))
	var source_key := "weather:%d:%s:%d" % [team, power_id, _sim.tick_index]
	var entry := {
		"kind": "weather_anticategory",
		"team": team,
		"power_id": power_id,
		"source_key": source_key,
		"anti_category": String(effect.get("anti_category", "")),
		"affects": String(effect.get("affects", "")),
		"weather": String(effect.get("weather", "")),
		"expire_tick": expire_tick,
	}
	_sim._weather_effects.append(entry)
	var affected := _apply_weather_anticategory(entry)
	_sim._emit_event("power.weather", 0, 0, {
		"team": team,
		"power_id": power_id,
		"kind": "weather_anticategory",
		"weather": String(effect.get("weather", "")),
		"anti_category": String(effect.get("anti_category", "")),
		"duration_ticks": int(effect.get("duration_ticks", 0)),
		"expire_tick": expire_tick,
		"affected": affected,
		# Named, not dropped: the fire half of the power has no sim model.
		"unconverted_behaviors": (effect.get("unconverted_behaviors", []) as Array).duplicate(),
	})
	return {"ok": true, "reason": "", "battalions": affected}


func _apply_weather_modifier(entry: Dictionary) -> int:
	var _sim = sim
	var team := int(entry.get("team", -1))
	var affects := String(entry.get("affects", ""))
	var key := String(entry.get("source_key", ""))
	var expire_tick = int(entry.get("expire_tick", -1))
	var modifiers: Array = entry.get("modifiers", []) as Array
	var affected := 0
	for id in _sim.entity_ids():
		var row: Dictionary = _sim.entities[id]
		if int(row.get("health", 0)) <= 0:
			continue
		if not _spellbook_member_affects(row, affects, int(row.get("team", -1)) == team):
			continue
		_sim._set_timed_modifier(row, key, modifiers, expire_tick)
		affected += 1
	return affected


func _set_leadership_suppression_source(row: Dictionary, source_key: String, expire_tick: int) -> void:
	if source_key == "":
		return
	_refresh_leadership_suppression(row)
	var sources: Dictionary = row.get("leadership_suppression_sources", {}) as Dictionary
	sources[source_key] = maxi(expire_tick, int(sources.get(source_key, -1)))
	row["leadership_suppression_sources"] = sources
	_refresh_leadership_suppression(row)


func _erase_leadership_suppression_source(row: Dictionary, source_key: String) -> void:
	var sources: Dictionary = row.get("leadership_suppression_sources", {}) as Dictionary
	if not sources.has(source_key):
		_refresh_leadership_suppression(row)
		return
	sources.erase(source_key)
	if sources.is_empty():
		row.erase("leadership_suppression_sources")
		row.erase("leadership_suppressed_until_tick")
	else:
		row["leadership_suppression_sources"] = sources
	_refresh_leadership_suppression(row)


func _refresh_leadership_suppression(row: Dictionary) -> int:
	var _sim = sim
	var sources: Dictionary = row.get("leadership_suppression_sources", {}) as Dictionary
	var legacy := int(row.get("leadership_suppressed_until_tick", -1))
	if sources.is_empty() and legacy > _sim.tick_index:
		sources["legacy"] = legacy
	var effective := -1
	for source_key in sources.keys():
		var expire_tick = int(sources[source_key])
		if expire_tick <= _sim.tick_index:
			sources.erase(source_key)
		else:
			effective = maxi(effective, expire_tick)
	if sources.is_empty():
		row.erase("leadership_suppression_sources")
	else:
		row["leadership_suppression_sources"] = sources
	if effective > _sim.tick_index:
		row["leadership_suppressed_until_tick"] = effective
	else:
		row.erase("leadership_suppressed_until_tick")
	return effective


func _apply_weather_anticategory(entry: Dictionary) -> int:
	var _sim = sim
	var team := int(entry.get("team", -1))
	var affects := String(entry.get("affects", ""))
	var expire_tick = int(entry.get("expire_tick", -1))
	var source_key := String(entry.get("source_key", ""))
	if source_key == "":
		source_key = "weather:legacy:%d:%d" % [team, expire_tick]
	var affected := 0
	for id in _sim.entity_ids():
		var row: Dictionary = _sim.entities[id]
		if int(row.get("health", 0)) <= 0:
			continue
		if not _spellbook_member_affects(row, affects, int(row.get("team", -1)) == team):
			continue
		_set_leadership_suppression_source(row, source_key, expire_tick)
		affected += 1
	return affected


func _step_weather_effects() -> void:
	## Expire lapsed weather windows, then re-apply the live ones on the shared
	## aura cadence so units created or converted mid-window are covered - which
	## is what AttributeModifierWeatherBased = Yes means. Deterministic:
	## ascending entity order, fixed cadence, no RNG and no wall clock.
	var _sim = sim
	if _sim._weather_effects.is_empty():
		return
	var living: Array[Dictionary] = []
	for entry in _sim._weather_effects:
		if _sim.tick_index >= int(entry.get("expire_tick", -1)):
			continue
		living.append(entry)
		if _sim.tick_index % _sim.ABILITY_AURA_INTERVAL_TICKS != 0:
			continue
		match String(entry.get("kind", "")):
			"weather_modifier":
				_apply_weather_modifier(entry)
			"weather_anticategory":
				_apply_weather_anticategory(entry)
	_sim._weather_effects = living


func active_weather_effects() -> Array:
	## Presentation contract for the weather lane. The sim has no sky/weather
	## renderer, so this registry (and the power.weather event) is the whole
	## contract until one exists: retail's ChangeWeather cell (CLOUDY / RAINY),
	## the owning team and the tick the window lapses, in cast order.
	var rows: Array = []
	for entry in sim._weather_effects:
		rows.append({
			"team": int(entry.get("team", -1)),
			"power_id": String(entry.get("power_id", "")),
			"source_key": String(entry.get("source_key", "")),
			"kind": String(entry.get("kind", "")),
			"weather": String(entry.get("weather", "")),
			"expire_tick": int(entry.get("expire_tick", -1)),
		})
	return rows


func _migrate_restored_weather_sources() -> void:
	var _sim = sim
	if _sim._weather_effects.is_empty():
		return
	var migrated_sources := {}
	for index in range(_sim._weather_effects.size()):
		var entry: Dictionary = _sim._weather_effects[index]
		if String(entry.get("source_key", "")) == "":
			entry["source_key"] = "weather:legacy:%d:%s:%d:%d" % [
				int(entry.get("team", -1)), String(entry.get("power_id", "")),
				int(entry.get("expire_tick", -1)), index,
			]
			migrated_sources[String(entry["source_key"])] = true
	if migrated_sources.is_empty():
		return
	for id in _sim.entity_ids():
		var row: Dictionary = _sim.entities[id]
		var table: Dictionary = row.get("timed_modifiers", {}) as Dictionary
		var old_weather_payloads := {}
		for entry in _sim._weather_effects:
			var source_key := String(entry.get("source_key", ""))
			if not migrated_sources.has(source_key):
				continue
			var old_key := "weather:%s" % String(entry.get("modifier_id", ""))
			if String(entry.get("kind", "")) == "weather_modifier" and table.has(old_key):
				old_weather_payloads[old_key] = table[old_key]
		var sources: Dictionary = (
			row.get("leadership_suppression_sources", {}) as Dictionary
		).duplicate(true)
		var legacy_deadline := int(row.get("leadership_suppressed_until_tick", -1))
		var weather_deadline := -1
		for entry in _sim._weather_effects:
			var team := int(entry.get("team", -1))
			if not _spellbook_member_affects(
				row, String(entry.get("affects", "")), int(row.get("team", -1)) == team
			):
				continue
			var source_key := String(entry.get("source_key", ""))
			match String(entry.get("kind", "")):
				"weather_modifier":
					var old_key := "weather:%s" % String(entry.get("modifier_id", ""))
					if old_weather_payloads.has(old_key) and not table.has(source_key):
						table[source_key] = (old_weather_payloads[old_key] as Dictionary).duplicate(true)
				"weather_anticategory":
					var expire_tick = int(entry.get("expire_tick", -1))
					sources[source_key] = expire_tick
					weather_deadline = maxi(weather_deadline, expire_tick)
		for old_key in old_weather_payloads.keys():
			table.erase(old_key)
		row["timed_modifiers"] = table
		if legacy_deadline > _sim.tick_index and (
			weather_deadline < 0 or legacy_deadline > weather_deadline
		):
			sources["legacy"] = maxi(legacy_deadline, int(sources.get("legacy", -1)))
		row.erase("leadership_suppressed_until_tick")
		if sources.is_empty():
			row.erase("leadership_suppression_sources")
		else:
			row["leadership_suppression_sources"] = sources
		_refresh_leadership_suppression(row)


func _cast_spellbook_creep_allegiance(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	## Untamed Allegiance. Every creep lair the authored filter names, inside the
	## authored radius of the cast point, changes owner to the caster - and its
	## slaved guards go with it. Descriptor-backed lairs retain their exact
	## SpawnBehavior parent/master edge and consume their authored faction
	## CommandSetUpgrade.
	##
	## Retail authors `TargetEnemy = Yes` with an ENEMIES-relative
	## CREEP_OBJECTFILTER. Therefore both still-wild PlyrCreeps lairs and lairs
	## previously taken by an opposing roster team are eligible; friendly lairs
	## are not.
	var _sim = sim
	var radius = float(effect.get("range_source", 0.0)) * _sim._spellbook_world_scale()
	var converted_lairs: Array[int] = []
	var converted_guards: Array[int] = []
	for lair_id in _sim.structure_ids():
		var lair: Dictionary = _sim.structures[lair_id]
		if String(lair.get("structure_kind", "")) != "lair":
			continue
		if not _sim._is_hostile(team, int(lair.get("team", -1))):
			continue
		if int(lair.get("health", 0)) <= 0:
			continue
		if Vector2(lair.get("position", Vector2.ZERO)).distance_to(point) > radius:
			continue
		# The sim stores each lair's RETAIL type name (CaveTrollLair,
		# MoriarGoblinLairSnow, ...), which is exactly what the resolved
		# CREEP_OBJECTFILTER lists, so the authored filter is applied verbatim.
		var retail_type := String(lair.get("source_object_id", ""))
		if not Array(effect.get("lair_types", [])).has(retail_type):
			continue
		lair["team"] = team
		_sim._apply_scenario_structure_faction_command_set(lair, team)
		var spawn_policy := lair.get("spawn_behavior", {}) as Dictionary
		for child_value in spawn_policy.get("spawned_ids", []) as Array:
			var child_id := int(child_value)
			if not _sim.entities.has(child_id):
				continue
			var child := _sim.entities[child_id] as Dictionary
			if int(child.get("health", 0)) <= 0:
				continue
			child["team"] = team
			converted_guards.append(child_id)
		converted_lairs.append(lair_id)
	if converted_lairs.is_empty():
		return {"ok": false, "reason": "no-valid-targets"}
	_sim._emit_event("power.creep_allegiance", 0, 0, {
		"team": team,
		"point": [snappedf(point.x, 0.001), snappedf(point.y, 0.001)],
		"radius_source": float(effect.get("range_source", 0.0)),
		"lairs": converted_lairs,
		"guards": converted_guards,
		"filter_matches": Array(effect.get("lair_types", [])).size(),
	})
	return {"ok": true, "reason": "", "battalions": converted_guards.size(), "structures": converted_lairs.size()}


func _step_pending_power_effects() -> void:
	var _sim = sim
	if _sim._pending_power_effects.is_empty():
		return
	# Clear first so a fired effect that kills another death-weapon carrier can
	# append its new schedule without being overwritten by this pass.
	var processing = _sim._pending_power_effects
	# Typed replacement: a plain [] cannot be assigned to the sim's
	# Array[Dictionary] member across the subsystem boundary.
	var cleared: Array[Dictionary] = []
	_sim._pending_power_effects = cleared
	for effect in processing:
		if _sim.tick_index < int(effect.get("fire_tick", 0)):
			_sim._pending_power_effects.append(effect)
			continue
		match String(effect.get("kind", "")):
			"strike":
				_fire_power_strike(effect)
			"summon":
				_fire_power_summon(effect)
			"death_weapon":
				_fire_death_weapon(effect)


func _fire_death_weapon(effect: Dictionary) -> void:
	var _sim = sim
	var weapon_id := String(effect.get("weapon_id", ""))
	var rule: Dictionary = effect.get("weapon_rule", {}) as Dictionary
	var point := Vector2(effect.get("point", Vector2.ZERO))
	var source_team := int(effect.get("team", -1))
	if rule.is_empty():
		_sim._emit_event("module.death_weapon_unresolved", int(effect.get("source_id", 0)), 0, {
			"weapon_id": weapon_id,
			"point": [snappedf(point.x, 0.001), snappedf(point.y, 0.001)],
			"height_source": float(effect.get("height_source", 0.0)),
			"death_type": String(effect.get("death_type", "")),
		})
		return
	var radius = _sim._retail_source_to_sim_offset(
		Vector2(float(rule.get("radius_source", 0.0)), 0.0)
	).length()
	var amount := float(rule.get("damage", 0.0))
	var damage_type := String(rule.get("damage_type", ""))
	var affects := String(rule.get("affects", "ENEMIES"))
	var battalions := 0
	var hit_structures := 0
	for entity_id in _sim.entity_ids():
		var target: Dictionary = _sim.entities[entity_id]
		if int(target.get("health", 0)) <= 0:
			continue
		var allied := int(target.get("team", -1)) == source_team
		if (allied and not affects.contains("ALLIES")) or (not allied and not affects.contains("ENEMIES")):
			continue
		if not allied and not _sim._is_hostile(source_team, int(target.get("team", -1))):
			continue
		if Vector2(target.get("position", Vector2.ZERO)).distance_to(point) > radius:
			continue
		_apply_area_damage_to_battalion(entity_id, amount, damage_type)
		battalions += 1
	for structure_id in _sim.structure_ids():
		var structure: Dictionary = _sim.structures[structure_id]
		if int(structure.get("health", 0)) <= 0:
			continue
		var allied := int(structure.get("team", -1)) == source_team
		if (allied and not affects.contains("ALLIES")) or (not allied and not affects.contains("ENEMIES")):
			continue
		if not allied and not _sim._is_hostile(source_team, int(structure.get("team", -1))):
			continue
		if Vector2(structure.get("position", Vector2.ZERO)).distance_to(point) > radius:
			continue
		_apply_area_damage_to_structure(structure_id, amount, damage_type)
		hit_structures += 1
	_sim._emit_event("module.death_weapon_fired", int(effect.get("source_id", 0)), 0, {
		"weapon_id": weapon_id,
		"point": [snappedf(point.x, 0.001), snappedf(point.y, 0.001)],
		"height_source": float(effect.get("height_source", 0.0)),
		"damage": amount,
		"damage_type": damage_type,
		"battalions": battalions,
		"structures": hit_structures,
	})


func _fire_power_strike(effect: Dictionary) -> void:
	## One detonation: converted damage over converted radius against the
	## authored affects teams (ENEMIES/ALLIES; NEUTRALS has no sim team).
	var _sim = sim
	var point := Vector2(effect.get("point", Vector2.ZERO))
	var radius = float(effect.get("radius_source", 0.0)) * _sim._spellbook_world_scale()
	var amount := float(effect.get("damage", 0.0))
	var caster_team := int(effect.get("team", -1))
	var affects := String(effect.get("affects", "ENEMIES"))
	var teams: Array[int] = []
	if affects.contains("ENEMIES"):
		teams.append(1 - caster_team)
	if affects.contains("ALLIES"):
		teams.append(caster_team)
	var hit_battalions := 0
	var hit_structures := 0
	for affected_team in teams:
		for id in _sim.living_ids(affected_team):
			var row: Dictionary = _sim.entities[id]
			if Vector2(row.get("position", Vector2.ZERO)).distance_to(point) > radius:
				continue
			_apply_area_damage_to_battalion(id, amount, String(effect.get("damage_type", "")))
			hit_battalions += 1
		for structure_id in _sim.structure_ids(affected_team):
			var structure: Dictionary = _sim.structures[structure_id]
			if int(structure.get("health", 0)) <= 0:
				continue
			if Vector2(structure.get("position", Vector2.ZERO)).distance_to(point) > radius:
				continue
			_apply_area_damage_to_structure(structure_id, amount, String(effect.get("damage_type", "")))
			hit_structures += 1
	_sim._emit_event("power.strike", 0, 0, {
		"team": caster_team,
		"point": [snappedf(point.x, 0.001), snappedf(point.y, 0.001)],
		"damage": amount,
		"damage_type": String(effect.get("damage_type", "")),
		"battalions": hit_battalions,
		"structures": hit_structures,
	})


func _apply_area_damage_to_battalion(id: int, amount: float, damage_type: String) -> void:
	## AoE payload spread over the battalion's members (front-to-back, stance
	## and formation multipliers like focused combat).
	var _sim = sim
	var row: Dictionary = _sim.entities.get(id, {})
	if row.is_empty() or int(row.get("health", 0)) <= 0:
		return
	var total = maxf(0.0, amount * float(_sim._stance_state(row).get("incomingDamageMultiplier", 1.0)) * float(_sim._formation_effects(row).get("incoming_damage_multiplier", 1.0)))
	var health_values: Array = row.get("member_health", [])
	var remaining = total
	var defeated_members: Array[int] = []
	for member_index in health_values.size():
		if remaining <= 0.0:
			break
		var current := int(health_values[member_index])
		if current <= 0:
			continue
		var applied := mini(float(current), remaining)
		health_values[member_index] = maxi(0, current - int(round(applied)))
		if int(health_values[member_index]) == 0:
			defeated_members.append(member_index)
		remaining -= applied
	row["member_health"] = health_values
	var aggregate := 0
	for value in health_values:
		aggregate += int(value)
	row["health"] = aggregate
	row["last_damage_tick"] = _sim.tick_index
	_sim.record_hit_reaction(id, total)
	if aggregate <= 0:
		var death_policy = _sim._bookkeep_battalion_death(
			id, row, "NORMAL", defeated_members
		)
		_sim._emit_event("battalion.defeated", 0, id)
		if bool(death_policy.get("destroy_object", false)):
			_sim.entities.erase(id)
	else:
		_sim._apply_playable_unit_death_policy(row, "NORMAL", defeated_members)


func _apply_area_damage_to_structure(structure_id: int, amount: float, damage_type: String) -> void:
	var structure: Dictionary = sim.structures.get(structure_id, {})
	if structure.is_empty():
		return
	var health := int(structure.get("health", 0))
	if health <= 0:
		return
	var new_health := maxi(0, health - int(round(amount)))
	structure["health"] = new_health
	if new_health <= 0:
		structure["health"] = 0


func _fire_power_summon(effect: Dictionary) -> void:
	## Hatch: spawn each converted summon target with its summon lifetime.
	var team := int(effect.get("team", -1))
	var point := Vector2(effect.get("point", Vector2.ZERO))
	var spawned := _spawn_summon_targets(team, point, Array(effect.get("targets", [])))
	sim._emit_event("power.summon", 0, 0, {"team": team, "point": [snappedf(point.x, 0.001), snappedf(point.y, 0.001)], "spawned": spawned})


func _spawn_summon_targets(team: int, point: Vector2, targets: Array) -> Array:
	## Shared summon spawn: register each converted summon rule as a live unit
	## rule and place its battalions with the authored summon lifetime. Used by
	## BOTH the spellbook OCL powers and the hero egg-chain abilities so the two
	## lanes cannot drift apart.
	var _sim = sim
	var spawned: Array = []
	for target_value in targets:
		var target := target_value as Dictionary
		var rule: Dictionary = (target.get("rule", {}) as Dictionary).duplicate(true)
		var object_id := String(target.get("object_id", ""))
		if rule.is_empty():
			continue
		var count := int(target.get("count", 1))
		for index in range(count):
			var angle := TAU * float(index) / float(maxi(1, count))
			var spawn_point := point + Vector2(cos(angle), sin(angle)) * (1.0 if count <= 1 else 2.0)
			var entity_id := int(_sim._next_dynamic_id.get(team, 1000))
			_sim._next_dynamic_id[team] = entity_id + 1
			_sim._add_battalion(
				entity_id, team, spawn_point, String(rule.get("horde_id", object_id)),
				object_id, object_id, 0, rule
			)
			if not _sim.entities.has(entity_id):
				continue
			var lifetime_ticks := int(target.get("lifetime_ticks", 0))
			if lifetime_ticks > 0:
				_sim._summon_despawn_ticks[entity_id] = _sim.tick_index + lifetime_ticks
				_sim.entities[entity_id]["summon_lifetime_death_type"] = String(
					target.get("lifetime_death_type", "")
				).to_upper()
			if not Array((_sim.entities[entity_id] as Dictionary).get("summon_auras", [])).is_empty():
				_sim._summon_aura_source_ids[entity_id] = true
			spawned.append(entity_id)
	return spawned


func spawn_script_object(object_type: String, team: int, at: Vector2, ring_fallback := false, scenario_surface: String = "script-spawn") -> int:
	## Map-script object creation (campaign B4). Instantiates one battalion of
	## the retail object type `object_type` for `team` at `at` and returns its
	## entity id.
	##
	## Returns -1, and creates nothing, when this simulation cannot honestly
	## produce the type: the loaded content carries no unit rule for it, or the
	## team is not in the roster. That is the ordinary case for a campaign map
	## object whose retail type is not in the loaded faction slice, and the
	## caller (RetailMapScripts) keeps the object as registry state only rather
	## than substituting a different unit for it.
	##
	## Nothing inside the simulation calls this, so a match whose scripts never
	## create objects is byte-identical to one compiled before it existed.
	var _sim = sim
	if object_type == "":
		return -1
	if _sim.ring_mechanic_enabled and object_type == _sim._ring_gollum_object_id() and not ring_fallback:
		var existing = _sim._existing_ring_gollum_id()
		if existing != 0:
			var state = _sim._ring_state()
			state["gollum_id"] = existing
			state["gollum_spawned"] = true
			print("[RetailSliceSim] RING_GOLLUM_SCRIPT_DUPLICATE_ABSORBED existing=%d object=%s" % [existing, object_type])
			return existing
	var unit_rules_value: Variant = _sim._rules.get("unit_rules", {})
	if typeof(unit_rules_value) != TYPE_DICTIONARY:
		return -1
	var rule: Dictionary = (unit_rules_value as Dictionary).get(object_type, {}) as Dictionary
	if rule.is_empty():
		return _sim.spawn_scenario_unit(object_type, team, at, scenario_surface)
	if not _sim._next_dynamic_id.has(team):
		# Allocating outside the seeded per-team id ranges would collide with
		# another team's ids, so an unseeded team refuses rather than inventing
		# an id space.
		if team == _sim.CREEP_TEAM and _sim.ring_mechanic_enabled:
			_sim._next_dynamic_id[team] = 80001
		else:
			return -1
	var entity_id := int(_sim._next_dynamic_id[team])
	_sim._next_dynamic_id[team] = entity_id + 1
	_sim._add_battalion(
		entity_id,
		team,
		at,
		String(rule.get("horde_id", object_type)),
		object_type,
		object_type,
		0
	)
	if not _sim.entities.has(entity_id):
		return -1
	if _sim.ring_mechanic_enabled and object_type == _sim._ring_gollum_object_id() and not ring_fallback:
		var state = _sim._ring_state()
		state["gollum_id"] = entity_id
		state["gollum_spawned"] = true
		_sim._configure_ring_gollum(_sim.entities[entity_id] as Dictionary)
		_sim._emit_event("ring.gollum_spawned", entity_id, 0, {"fallback": false, "script": true})
	return entity_id


func _step_summon_despawns() -> void:
	var _sim = sim
	if _sim._summon_despawn_ticks.is_empty():
		return
	var expired: Array = []
	for entity_id_value in _sim._summon_despawn_ticks.keys():
		var entity_id := int(entity_id_value)
		if not _sim.entities.has(entity_id):
			expired.append(entity_id)
			continue
		if _sim.tick_index < int(_sim._summon_despawn_ticks[entity_id_value]):
			continue
		# The authored summon lifetime ends: the battalion fades (no kill
		# credit — there is no attacker).
		var row: Dictionary = _sim.entities[entity_id]
		var death_type := String(
			row.get("summon_lifetime_death_type", "")
		).to_upper()
		var health_values: Array = row.get("member_health", [])
		var defeated_members: Array[int] = []
		for index in health_values.size():
			if int(health_values[index]) > 0:
				defeated_members.append(index)
				health_values[index] = 0
		row["member_health"] = health_values
		row["health"] = 0
		# Remove the lifetime registry first so a combat-dead summon can never be
		# processed again as an expiry (duplicate OCL/event/CP release).
		_sim._summon_despawn_ticks.erase(entity_id)
		var death_policy = _sim._bookkeep_battalion_death(
			entity_id, row, death_type, defeated_members
		)
		_sim._emit_event("power.summon_expired", 0, entity_id, {"team": int(row.get("team", -1))})
		# Honor the converted unit death policy. The authored death type begins its
		# matching path here; an immediate-destroy verdict removes now, while a retained
		# corpse is cleaned by the ordinary corpse scheduler.
		if bool(death_policy.get("destroy_object", false)):
			_sim.entities.erase(entity_id)
		expired.append(entity_id)
	for entity_id in expired:
		_sim._summon_despawn_ticks.erase(entity_id)


func _step_summon_auras() -> void:
	## Moving scout summons (Crebain/Cave Bats) carry GenericDebuff as part of
	## their payload. Refresh the converted timed modifier on enemy battalions;
	## its authored duration naturally lets the debuff trail the moving aura by
	## up to one refresh window after separation or expiry.
	var _sim = sim
	if _sim._summon_aura_source_ids.is_empty():
		return
	var source_ids: Array = _sim._summon_aura_source_ids.keys()
	source_ids.sort()
	for source_id_value in source_ids:
		var source_id := int(source_id_value)
		if not _sim.entities.has(source_id):
			_sim._summon_aura_source_ids.erase(source_id)
			continue
		var source: Dictionary = _sim.entities[source_id]
		if int(source.get("health", 0)) <= 0:
			continue
		for aura_value in Array(source.get("summon_auras", [])):
			_refresh_one_summon_aura(source_id, source, aura_value as Dictionary)


func _refresh_one_summon_aura(source_id: int, source: Dictionary, aura: Dictionary) -> void:
	var _sim = sim
	if aura.is_empty():
		return
	var refresh_ticks := int(aura.get("refresh_ticks", 1))
	if _sim.tick_index % maxi(1, refresh_ticks) != 0:
		return
	var source_team := int(source.get("team", -1))
	var origin := Vector2(source.get("position", Vector2.ZERO))
	var radius = float(aura.get("range_source", 0.0)) * _sim._spellbook_world_scale()
	for target_id in _sim._spatial_gather_sorted(origin, radius):
		if not _sim.entities.has(target_id):
			continue
		var target: Dictionary = _sim.entities[target_id]
		var same_team := int(target.get("team", -1)) == source_team
		if int(target.get("health", 0)) <= 0:
			continue
		if Vector2(target.get("position", Vector2.ZERO)).distance_to(origin) > radius:
			continue
		if not _summon_aura_allows_relation(aura, same_team):
			continue
		if not _spellbook_member_affects(
			target, String(aura.get("filter", "")), same_team
		):
			continue
		_sim._set_timed_modifier(
			target,
			"summon-aura:%d:%s" % [source_id, String(aura.get("id", ""))],
			Array(aura.get("modifiers", [])),
			_sim.tick_index + int(aura.get("duration_ticks", 1))
		)


func _summon_aura_allows_relation(aura: Dictionary, same_team: bool) -> bool:
	var target_enemy: Variant = aura.get("target_enemy", null)
	var target_allies: Variant = aura.get("target_allies", null)
	# Selected packs predating targetEnemy/targetAllies still carry the authored
	# AttributeModifier category and relation tokens. Resolve each missing side
	# from that evidence; an explicitly authored flag overrides only its side.
	var category := String(aura.get("category", "")).to_upper()
	var filter_terms := String(aura.get("filter", "")).to_upper().replace("\t", " ").split(" ", false)
	var authored_enemy := category == "DEBUFF" or filter_terms.has("ENEMIES")
	var authored_ally := not authored_enemy
	if filter_terms.has("ALLIES"):
		authored_ally = true
	# A positive explicit side is exclusive when the opposite side is omitted.
	# An explicit false only disables its own side; the omitted side may still
	# use legacy category/filter evidence.
	var allow_enemy := bool(target_enemy) if target_enemy != null else (false if target_allies == true else authored_enemy)
	var allow_ally := bool(target_allies) if target_allies != null else (false if target_enemy == true else authored_ally)
	return allow_ally if same_team else allow_enemy


func _step_grove_auras() -> void:
	var _sim = sim
	if _sim._active_groves.is_empty():
		return
	var living: Array[Dictionary] = []
	for grove in _sim._active_groves:
		if _sim.tick_index >= int(grove.get("despawn_tick", -1)):
			continue
		living.append(grove)
		var team := int(grove.get("team", -1))
		var point := Vector2(grove.get("point", Vector2.ZERO))
		var range_sim := float(grove.get("range_sim", 0.0))
		# A grove buffs a bounded disc, so this is a neighbourhood query over the
		# owning team rather than a sweep of its whole army.
		for id in _sim._spatial_gather_sorted(point, range_sim):
			if not _sim.entities.has(id):
				continue
			var row: Dictionary = _sim.entities[id]
			if int(row.get("team", -1)) != team or int(row.get("health", 0)) <= 0:
				continue
			if Vector2(row.get("position", Vector2.ZERO)).distance_to(point) > range_sim:
				continue
			if not _spellbook_member_affects(
				row, String(grove.get("filter", "")), true
			):
				continue
			# The taint buff is an ordinary timed modifier, not a private pair of row
			# fields. Retail authors it as a ModifierList exactly like Darkness, and
			# attributemodifier.ini notes that same-kind rows from different lists ADD;
			# the private fields multiplied instead and bypassed ABILITY_ARMOR_CAP, so
			# taint + Darkness composed wrongly and could reach total immunity.
			#
			# KEYED BY THE AUTHORED LEAF ID, not a hardcoded "GenericBuff". The
			# grove/taint family binds different ModifierLists per edition and per
			# faction — RotWK ElvenGrove binds GenericBuff, BFME2 ElvenGrove binds
			# GenericArmorLeadership (grove.ini:31), TaintLand binds its own — and a
			# single hardcoded key made two different authored lists collide in one
			# accumulator slot, so the second overwrote the first instead of stacking
			# beside it. Same rule as the summon-aura and weather lanes, which are
			# already keyed by their authored id.
			_sim._set_timed_modifier(
				row,
				"taint:%s" % String(grove.get("modifier", "")),
				[
					{"kind": "ARMOR", "value": float(grove.get("armor_mult", 0.0))},
					{"kind": "DAMAGE_MULT", "value": float(grove.get("damage_mult", 1.0))},
				],
				_sim.tick_index + int(grove.get("buff_duration_ticks", 1)),
			)
	_sim._active_groves = living


func _spellbook_member_affects(
	row: Dictionary, filter_text: String, same_team: Variant = null
) -> bool:
	## Aura filters (GENERIC_BUFF_RECIPIENT_OBJECT_FILTER) exclude the HORDE
	## container kind but include its infantry/cavalry members; the sim's
	## battalion is both, so the container distinction drops out of the kind
	## list before the authored terms are evaluated.
	var kinds := _spellbook_object_kinds(row)
	kinds.erase("HORDE")
	var included := false
	for term_value in filter_text.split(" ", false):
		var term := String(term_value)
		if term == "" or term == "NONE":
			continue
		if term == "ALLIES":
			if same_team != null and not bool(same_team):
				return false
			continue
		if term == "ENEMIES":
			if same_team != null and bool(same_team):
				return false
			continue
		if term.begins_with("-"):
			if kinds.has(term.trim_prefix("-")):
				return false
		elif term.begins_with("+"):
			if kinds.has(term.trim_prefix("+")):
				included = true
		elif term == "ANY":
			included = true
		elif term == "ALL":
			# `ALL` is the universal set ONLY when the filter authors no including
			# kind term of its own — retail's `ALL ENEMIES` (Freezing Rain) is a
			# relation-only filter and means everyone. Every OTHER `ALL` in this
			# corpus sits beside kind terms, where the filter reads conjunctively;
			# a blanket include there would silently widen it to the whole board.
			if not _spellbook_filter_has_kind_terms(filter_text):
				included = true
		elif kinds.has(term):
			included = true
	return included
