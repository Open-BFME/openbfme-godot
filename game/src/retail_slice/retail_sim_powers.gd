extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Spellbook/powers subsystem extracted from retail_slice_sim.gd (Q81
## strangler-fig extraction #11). Verbatim move, compiler-guided sim.
## prefixes, pin-verified byte-identical.





func set_spellbook_orb_open(open: bool) -> void:
	sim.clock_paused = open


func configure_spellbook_runtime(document: Dictionary) -> bool:
	var _sim = sim
	_sim._spellbook_ready = false
	_sim._spellbook_error = ""
	_sim._spellbook_document = document.duplicate(true)
	_sim._spellbook_powers.clear()
	_sim._spellbook_order.clear()
	_sim._spellbook_sciences.clear()
	_sim._spellbook_intrinsic.clear()
	_sim._science_to_power.clear()
	_sim._spellbook_command_points_upgrade.clear()
	if typeof(document) != TYPE_DICTIONARY or String(document.get("schema", "")) != _sim.SPELLBOOK_SCHEMA:
		_sim._spellbook_error = "spellbook document is missing or not an %s" % _sim.SPELLBOOK_SCHEMA
		return false
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var power_tree: Dictionary = registration.get("powerTree", {}) as Dictionary
	var spell_book_object: Dictionary = registration.get("spellBook", {}) as Dictionary
	var command_points_upgrade_value: Variant = spell_book_object.get("commandPointsUpgrade")
	if command_points_upgrade_value != null:
		if typeof(command_points_upgrade_value) != TYPE_DICTIONARY:
			_sim._spellbook_error = "spellbook CommandPointsUpgrade is malformed"
			return false
		var command_points_upgrade := command_points_upgrade_value as Dictionary
		# Retail authors one CommandPointsUpgrade on the shared spellbook system
		# object (Marketplace Grand Harvest → +100 CP). Every faction's compiled
		# spellbook-runtime carries that same evidence. JSON may deliver the
		# numeric fields as int or float — accept exact retail identity with
		# integral-float tolerance only (not arbitrary amounts/objects).
		var command_points_upgrade_keys := [
			"triggeredBy", "commandPoints", "requiredObject",
			"module", "sourceIni", "line",
		]
		if command_points_upgrade.size() != command_points_upgrade_keys.size():
			_sim._spellbook_error = "spellbook CommandPointsUpgrade is unsupported"
			return false
		for key in command_points_upgrade_keys:
			if not command_points_upgrade.has(key):
				_sim._spellbook_error = "spellbook CommandPointsUpgrade is unsupported"
				return false
		var command_points_value: Variant = command_points_upgrade.get("commandPoints")
		var line_value: Variant = command_points_upgrade.get("line")
		var command_points_ok := (
			typeof(command_points_value) in [TYPE_INT, TYPE_FLOAT]
			and is_equal_approx(float(command_points_value), 100.0)
		)
		var line_ok := (
			typeof(line_value) in [TYPE_INT, TYPE_FLOAT]
			and is_equal_approx(float(line_value), float(int(line_value)))
			and int(line_value) > 0
		)
		if (
			String(command_points_upgrade.get("triggeredBy", "")) != "Upgrade_MarketplaceUpgradeGrandHarvest"
			or not command_points_ok
			or String(command_points_upgrade.get("requiredObject", "")) != "NONE +GondorMarketPlace"
			or String(command_points_upgrade.get("module", "")) != "CommandPointsUpgrade"
			or typeof(command_points_upgrade.get("sourceIni")) != TYPE_STRING
			or String(command_points_upgrade.get("sourceIni", "")).strip_edges() == ""
			or not line_ok
		):
			_sim._spellbook_error = "spellbook CommandPointsUpgrade is unsupported"
			return false
		_sim._spellbook_command_points_upgrade = command_points_upgrade.duplicate(true)
	for intrinsic_value in Array(spell_book_object.get("intrinsicSciences", [])):
		if typeof(intrinsic_value) != TYPE_STRING or String(intrinsic_value).strip_edges() == "":
			_sim._spellbook_error = "spellbook intrinsic sciences are malformed"
			return false
		_sim._spellbook_intrinsic.append(String(intrinsic_value))
	# Sciences with an authored purchase block make up the palantir tree. Their
	# prerequisiteGroups are preserved OR groups: a science is purchasable when
	# ANY group is fully owned; an empty group list means no prerequisites.
	for science_value in Array(power_tree.get("sciences", [])):
		if typeof(science_value) != TYPE_DICTIONARY:
			_sim._spellbook_error = "spellbook science entry is malformed"
			return false
		var science := science_value as Dictionary
		var science_id := String(science.get("id", ""))
		var purchase: Dictionary = science.get("purchase", {}) as Dictionary
		if science_id == "" or purchase.is_empty():
			continue
		var cost := int((science.get("pointCostMP", {}) as Dictionary).get("value", -1))
		var slot := int(purchase.get("slot", -1))
		if cost <= 0 or slot <= 0:
			_sim._spellbook_error = "spellbook science '%s' has no resolved MP cost or purchase slot" % science_id
			return false
		var groups: Array = []
		var groups_well_formed := true
		for group_value in Array(science.get("prerequisiteGroups", [])):
			if typeof(group_value) != TYPE_ARRAY:
				groups_well_formed = false
				break
			var group: Array = []
			for member_value in group_value:
				if typeof(member_value) != TYPE_STRING or String(member_value).strip_edges() == "":
					groups_well_formed = false
					break
				group.append(String(member_value))
			if not groups_well_formed:
				break
			groups.append(group)
		if not groups_well_formed:
			_sim._spellbook_error = "spellbook science '%s' prerequisite groups are malformed" % science_id
			return false
		_sim._spellbook_sciences[science_id] = {"cost": cost, "slot": slot, "groups": groups}
	var leaves: Dictionary = registration.get("leaves", {}) as Dictionary
	var modifier_leaves: Dictionary = {}
	for modifier_value in Array(leaves.get("attributeModifiers", [])):
		if typeof(modifier_value) == TYPE_DICTIONARY:
			modifier_leaves[String((modifier_value as Dictionary).get("id", ""))] = modifier_value
	var object_leaves: Dictionary = {}
	for object_value in Array(leaves.get("objects", [])):
		if typeof(object_value) == TYPE_DICTIONARY:
			object_leaves[String((object_value as Dictionary).get("id", ""))] = object_value
	var ocl_leaves: Dictionary = {}
	for ocl_value in Array(leaves.get("objectCreationLists", [])):
		if typeof(ocl_value) == TYPE_DICTIONARY:
			ocl_leaves[String((ocl_value as Dictionary).get("id", ""))] = ocl_value
	# Production path: register converted OCL leaves for CreateObjectDie hatch.
	_sim.ingest_ocl_leaves_from_document({"leaves": leaves})
	var weapon_leaves: Dictionary = {}
	for weapon_value in Array(leaves.get("weapons", [])):
		if typeof(weapon_value) == TYPE_DICTIONARY:
			weapon_leaves[String((weapon_value as Dictionary).get("id", ""))] = weapon_value
	for power_value in Array(power_tree.get("powers", [])):
		if typeof(power_value) != TYPE_DICTIONARY:
			_sim._spellbook_error = "spellbook power entry is malformed"
			return false
		var power := power_value as Dictionary
		var power_id := String(power.get("id", ""))
		if power_id == "":
			_sim._spellbook_error = "spellbook power is missing its id"
			return false
		var cast: Dictionary = power.get("cast", {}) as Dictionary
		var effect_definition: Dictionary = power.get("effect", {}) as Dictionary
		var fields: Array = effect_definition.get("fields", []) as Array
		var references: Dictionary = effect_definition.get("references", {}) as Dictionary
		# The tree science is the required science carrying the purchase slot
		# (e.g. Rallying Call's requiredSciences pair collapses to the MP one).
		var science_id := ""
		for required_value in Array(power.get("requiredSciences", [])):
			var candidate := String(required_value)
			if _sim._spellbook_sciences.has(candidate):
				science_id = candidate
				break
		var science_row: Dictionary = _sim._spellbook_sciences.get(science_id, {}) as Dictionary
		var reload_row: Dictionary = power.get("reloadTimeMs", {}) as Dictionary
		var reload_ms := float(reload_row.get("value", 0.0))
		# Retail authors `ReloadTime = 0` DELIBERATELY on the one-shot passive
		# spells (specialpower.ini:1341-1346 SpellBookScavenger,
		# :1492-1498 SpellBookFueltheFires): they have no recharge at all. A
		# resolved-zero reload is therefore authored truth, not a conversion
		# gap, and must not be reported as one — the effect resolver owns the
		# real verdict for those powers. An ABSENT reloadTimeMs stays locked.
		#
		# TODO (one-shot gate, unreachable today): retail keeps these from being
		# recast by marking the BUTTON, not the power - commandbutton.ini:9364-9371
		# Command_SpellBookScavenger and :9457-9465 Command_SpellBookFueltheFires
		# both author `Options = NONPRESSABLE`. This sim reads reload time only, so
		# a resolved-zero reload would let a once-per-match spell fire every frame.
		# No gate is implemented because both powers are BLOCKED at the effect
		# resolver (no supply-dock economy, no kill-bounty economy), so nothing can
		# cast them. Any future power that resolves with an authored-zero reload
		# MUST carry a once-per-match gate keyed off NONPRESSABLE before it ships.
		var reload_authored_zero := (
			reload_ms == 0.0
			and String(reload_row.get("expression", "")).strip_edges() == "0"
		)
		var row := {
			"id": power_id,
			"module": String(effect_definition.get("module", "")),
			"science_id": science_id,
			"cost": int(science_row.get("cost", 0)),
			"purchase_slot": int(science_row.get("slot", 0)),
			"cast_slot": int(cast.get("slot", 0)),
			"icon_ids": Array(cast.get("iconIds", [])),
			"needs_target_pos": Array(cast.get("options", [])).has("NEED_TARGET_POS"),
			"radius_cursor_source": float((power.get("radiusCursorRadius", {}) as Dictionary).get("value", 0.0)),
			"reload_ms": reload_ms,
			"reload_ticks": 0 if reload_authored_zero else maxi(1, roundi(reload_ms / 1000.0 / _sim.TICK_SECONDS)),
			"sound_id": String(power.get("initiateSoundId", "")),
			"fx_lists": Array(references.get("fxLists", [])),
			"ocls": Array(references.get("objectCreationLists", [])),
		}


		# Empty-is-absent: ordinary pressable powers keep their prior serialized
		# row/hash bytes; only the two retail one-shot passive buttons carry this.
		if Array(cast.get("options", [])).has("NONPRESSABLE"):
			row["nonpressable"] = true
		if science_id == "":
			row["castable"] = false
			row["locked_reason"] = "power has no purchasable tree science in the document"
		elif reload_ms <= 0.0 and not reload_authored_zero:
			row["castable"] = false
			row["locked_reason"] = "reloadTimeMs did not resolve to a positive value"
		else:
			var support = _sim._spellbook_effect_support(row, fields, references, modifier_leaves, object_leaves, ocl_leaves, weapon_leaves)
			row["castable"] = bool(support.get("ok", false))
			row["locked_reason"] = String(support.get("reason", ""))
			row["effect"] = support.get("effect", {})
		_sim._spellbook_powers[power_id] = row
	if _sim._spellbook_powers.is_empty():
		_sim._spellbook_error = "spellbook document carries no powers"
		return false
	# Science → tree power index so the palantir can draw prerequisite forks
	# without re-deriving tree logic in the presentation layer.
	_sim._science_to_power.clear()
	for tree_power_id in _sim._spellbook_powers.keys():
		var tree_science := String((_sim._spellbook_powers[tree_power_id] as Dictionary).get("science_id", ""))
		if tree_science != "":
			_sim._science_to_power[tree_science] = String(tree_power_id)
	var ordered: Array[String] = []
	for power_id_value in _sim._spellbook_powers.keys():
		ordered.append(String(power_id_value))
	ordered.sort_custom(func(a: String, b: String) -> bool:
		var row_a: Dictionary = _sim._spellbook_powers[a]
		var row_b: Dictionary = _sim._spellbook_powers[b]
		if int(row_a.get("purchase_slot", 0)) != int(row_b.get("purchase_slot", 0)):
			return int(row_a.get("purchase_slot", 0)) < int(row_b.get("purchase_slot", 0))
		return a.naturalnocasecmp_to(b) < 0
	)
	var seen_slots: Dictionary = {}
	for power_id_value in ordered:
		var slot := int((_sim._spellbook_powers[power_id_value] as Dictionary).get("purchase_slot", 0))
		if slot <= 0 or seen_slots.has(slot):
			_sim._spellbook_error = "spellbook purchase slots are missing or duplicated"
			return false
		seen_slots[slot] = true
	_sim._spellbook_order = ordered
	# The compiled Rank ladder rides with the spellbook document. A pack cooked
	# before that contract existed carries no rankScienceGrants at all: the
	# ladder then stays unconfigured and every rank call refuses with that
	# reason instead of inventing a spell-point grant.
	if power_tree.has("rankScienceGrants"):
		if not configure_player_rank_science_grants(power_tree.get("rankScienceGrants", []) as Array):
			_sim._spellbook_error = "spellbook rank ladder is malformed: %s" % _sim._player_rank_ladder_error
			return false
	else:
		_sim._player_rank_ladder.clear()
		_sim._player_rank_ladder_error = "the compiled spellbook document carries no rankScienceGrants"
	_reset_spellbook_match_state()
	_sim._spellbook_ready = true
	_sim._state_hash_static_digest.clear()
	return true


## Cross-faction seam: give ONE team its own faction spellbook tree while other
## teams keep the global (player-faction) tree. Reuses the exact parser above by
## parsing into the globals and lifting the result into the per-team store, then
## restoring the globals and match-state untouched — so configuring team B never
## perturbs team A's already-configured tree or the default signature. Fails
## closed: a missing/incomplete doc leaves the team without an override (it will
## fall back to the global tree only if one exists) and returns false with the
## parse error surfaced through team_spellbook_error().


func configure_team_spellbook_runtime(team: int, document: Dictionary) -> bool:
	# Deep copies: configure_spellbook_runtime() clears the global tree dicts in
	# place, so a reference-only capture would be wiped mid-parse.
	var _sim = sim
	var saved := _spellbook_global_bundle_copy()
	var saved_error = _sim._spellbook_error
	var saved_document = _sim._spellbook_document
	var saved_sciences = _sim._team_sciences.duplicate(true)
	var saved_cooldowns = _sim._power_cooldown_until.duplicate(true)
	var saved_staged = _sim._staged_purchases.duplicate(true)
	var saved_nonpressable = _sim._consumed_nonpressable_powers.duplicate(true)
	var saved_scavenger = _sim._scavenger_bounty_percent.duplicate(true)
	var ok := configure_spellbook_runtime(document)
	var parsed := _spellbook_global_bundle_copy() if ok else {}
	var parse_error = _sim._spellbook_error
	# Restore the globals + match state exactly as they were before this call.
	_apply_spellbook_bundle(saved)
	_sim._spellbook_document = saved_document
	_sim._spellbook_error = saved_error
	_sim._team_sciences = saved_sciences
	_sim._power_cooldown_until = saved_cooldowns
	_sim._staged_purchases = saved_staged
	_sim._consumed_nonpressable_powers = saved_nonpressable
	_sim._scavenger_bounty_percent = saved_scavenger
	if not ok:
		_sim._team_spellbooks.erase(team)
		_sim._team_spellbook_errors[team] = parse_error
		return false
	parsed["document"] = document.duplicate(true)
	_sim._team_spellbooks[team] = parsed
	_sim._team_spellbook_errors[team] = ""
	# Seed this team's ownership overlays from ITS OWN intrinsic sciences.
	_sim._team_sciences[team] = (parsed.get("intrinsic", []) as Array).duplicate(true)
	_sim._power_cooldown_until[team] = {}
	_sim._staged_purchases[team] = []
	_sim._consumed_nonpressable_powers[team] = {}
	_sim._scavenger_bounty_percent[team] = 0.0
	_sim.purchased_powers[team] = []
	return true


func team_spellbook_error(team: int) -> String:
	return String(sim._team_spellbook_errors.get(team, ""))


func team_has_spellbook_override(team: int) -> bool:
	return sim._team_spellbooks.has(team)


func _spellbook_global_bundle() -> Dictionary:
	## A shallow view of the current global tree fields. Used to lift a freshly
	## parsed tree into the per-team store and to save/restore around that parse.
	var _sim = sim
	return {
		"ready": _sim._spellbook_ready,
		"powers": _sim._spellbook_powers,
		"order": _sim._spellbook_order,
		"sciences": _sim._spellbook_sciences,
		"science_to_power": _sim._science_to_power,
		"intrinsic": _sim._spellbook_intrinsic,
		"command_points_upgrade": _sim._spellbook_command_points_upgrade,
	}


func _spellbook_global_bundle_copy() -> Dictionary:
	## A DEEP copy of the global tree, detached from the live global dicts so a
	## subsequent in-place clear/refill of those dicts cannot mutate it.
	var _sim = sim
	var order_copy: Array[String] = []
	for power_id in _sim._spellbook_order:
		order_copy.append(String(power_id))
	return {
		"ready": _sim._spellbook_ready,
		"powers": _sim._spellbook_powers.duplicate(true),
		"order": order_copy,
		"sciences": _sim._spellbook_sciences.duplicate(true),
		"science_to_power": _sim._science_to_power.duplicate(true),
		"intrinsic": (_sim._spellbook_intrinsic as Array).duplicate(true),
		"command_points_upgrade": _sim._spellbook_command_points_upgrade.duplicate(true),
	}


func _apply_spellbook_bundle(bundle: Dictionary) -> void:
	var _sim = sim
	_sim._spellbook_ready = bool(bundle.get("ready", false))
	_sim._spellbook_powers = bundle.get("powers", {}) as Dictionary
	_sim._spellbook_order = bundle.get("order", []) as Array[String]
	_sim._spellbook_sciences = bundle.get("sciences", {}) as Dictionary
	_sim._science_to_power = bundle.get("science_to_power", {}) as Dictionary
	_sim._spellbook_intrinsic = bundle.get("intrinsic", []) as Array
	_sim._spellbook_command_points_upgrade = bundle.get("command_points_upgrade", {}) as Dictionary


func _team_tree(team: int) -> Dictionary:
	## The tree a team resolves powers against: its own override when present,
	## otherwise a view of the global (default same-faction) tree. Read-only —
	## team ownership overlays live in the per-team maps, not in the tree.
	var _sim = sim
	if _sim._team_spellbooks.has(team):
		return _sim._team_spellbooks[team]
	return _spellbook_global_bundle()


func _reset_spellbook_match_state() -> void:
	var _sim = sim
	_sim._team_sciences = _sim._seed_team_map(_sim._spellbook_intrinsic)
	# A team with its own faction tree starts from ITS intrinsic sciences, not
	# the global player-faction ones.
	for team_value in _sim._team_spellbooks.keys():
		var override_tree: Dictionary = _sim._team_spellbooks[team_value]
		_sim._team_sciences[team_value] = (override_tree.get("intrinsic", []) as Array).duplicate(true)
	_sim._power_cooldown_until = _sim._seed_team_map({})
	_sim._consumed_nonpressable_powers = _sim._seed_team_map({})
	_sim._scavenger_bounty_percent = _sim._seed_team_map(0.0)
	_sim._staged_purchases = _sim._seed_team_map([])
	_sim._pending_power_effects.clear()
	_sim._active_groves.clear()
	_sim._field_pings.clear()
	_sim._weather_effects.clear()
	_sim._summon_despawn_ticks.clear()
	_sim._summon_aura_source_ids.clear()


## Timed spellbook effect state (volley strikes, summon hatches, groves).


func spellbook_available() -> bool:
	return sim._spellbook_ready


func spellbook_error() -> String:
	return sim._spellbook_error


func spellbook_power_ids() -> Array[String]:
	return sim._spellbook_order.duplicate()


func spellbook_power(power_id: String) -> Dictionary:
	return (sim._spellbook_powers.get(power_id, {}) as Dictionary).duplicate(true)


func _spellbook_world_scale() -> float:
	## Doc radii are source-map units; sim space is source * local transform.
	return maxf(0.000001, float(sim._rules.get("source_map_transform_scale", 1.0)))


func power_points(team: int) -> int:
	return int(sim.team_power_points.get(team, 0))


func owned_sciences(team: int) -> Array:
	return (sim._team_sciences.get(team, []) as Array).duplicate()


func has_power(team: int, power_id: String) -> bool:
	return (sim.purchased_powers.get(team, []) as Array).has(power_id)


func _science_owned(team: int, science_id: String) -> bool:
	return (sim._team_sciences.get(team, []) as Array).has(science_id)


func _power_prerequisites_met(team: int, science_id: String) -> bool:
	var sciences: Dictionary = _team_tree(team).get("sciences", {}) as Dictionary
	var science_row: Dictionary = sciences.get(science_id, {}) as Dictionary
	if science_row.is_empty():
		return false
	var groups: Array = science_row.get("groups", []) as Array
	if groups.is_empty():
		return true
	for group_value in groups:
		var satisfied := true
		for member in group_value as Array:
			if not _science_owned(team, String(member)):
				satisfied = false
				break
		if satisfied:
			return true
	return false


func can_purchase_power(team: int, power_id: String) -> Dictionary:
	var tree := _team_tree(team)
	if not bool(tree.get("ready", false)):
		return {"ok": false, "reason": "spellbook-unavailable"}
	var row: Dictionary = (tree.get("powers", {}) as Dictionary).get(power_id, {}) as Dictionary
	if row.is_empty():
		return {"ok": false, "reason": "unknown-power"}
	if has_power(team, power_id):
		return {"ok": false, "reason": "already-purchased"}
	if not _power_prerequisites_met(team, String(row.get("science_id", ""))):
		return {"ok": false, "reason": "prerequisites-unmet"}
	if power_points(team) < int(row.get("cost", 0)):
		return {"ok": false, "reason": "insufficient-power-points"}
	return {"ok": true, "reason": ""}


func purchase_power(team: int, power_id: String, cost: int = -1) -> Dictionary:
	var _sim = sim
	var verdict := can_purchase_power(team, power_id)
	if not bool(verdict.get("ok", false)):
		return verdict
	var row: Dictionary = (_team_tree(team).get("powers", {}) as Dictionary)[power_id]
	var doc_cost := int(row.get("cost", 0))
	if cost >= 0 and cost != doc_cost:
		return {"ok": false, "reason": "cost-mismatch"}
	# Scavenger's purchase button CommandTriggers its NONPRESSABLE spell-book
	# command. Preflight the complete effect before spending points or mutating
	# ownership so a malformed contract rolls back byte-for-byte.
	var passive := _nonpressable_purchase_activation(team, power_id, row)
	if not bool(passive.get("ok", false)):
		return passive
	_sim.team_power_points[team] = power_points(team) - doc_cost
	(_sim.purchased_powers[team] as Array).append(power_id)
	(_sim._team_sciences[team] as Array).append(String(row.get("science_id", "")))
	(_sim._staged_purchases[team] as Array).append({"power_id": power_id, "science_id": String(row.get("science_id", "")), "cost": doc_cost})
	if bool(passive.get("activate", false)):
		_sim._scavenger_bounty_percent[team] = float(passive.get("bounty_percent", 0.0))
		(_sim._consumed_nonpressable_powers[team] as Dictionary)[power_id] = true
	_sim._emit_event("power.purchased", 0, 0, {
		"team": team,
		"power_id": power_id,
		"science_id": String(row.get("science_id", "")),
		"cost": doc_cost,
		"purchase_slot": int(row.get("purchase_slot", 0)),
	})
	return {"ok": true, "reason": "", "cost": doc_cost, "passive_activated": bool(passive.get("activate", false))}


func _nonpressable_purchase_activation(team: int, power_id: String, row: Dictionary) -> Dictionary:
	if not bool(row.get("nonpressable", false)):
		return {"ok": true, "activate": false}
	var effect := row.get("effect", {}) as Dictionary
	# Fuel the Fires is also NONPRESSABLE but remains locked until its own
	# consumer exists. Do not invent a generic passive dispatcher here.
	if String(effect.get("kind", "")) != "scavenger_bounty":
		return {"ok": true, "activate": false}
	var percent := float(effect.get("bounty_percent", -1.0))
	if not sim._is_combatant_team(team) or not is_finite(percent) or percent < 0.0:
		return {"ok": false, "reason": "invalid-scavenger-contract", "power_id": power_id}
	return {"ok": true, "activate": true, "bounty_percent": percent}


func reset_spellbook_purchases(team: int) -> Dictionary:
	## Retail RESET: refund this session's unspent picks so the player re-picks.
	var _sim = sim
	if not bool(_team_tree(team).get("ready", false)):
		return {"ok": false, "reason": "spellbook-unavailable"}
	var refunded := 0
	var restored: Array = []
	for entry_value in Array(_sim._staged_purchases.get(team, [])):
		if typeof(entry_value) != TYPE_DICTIONARY:
			continue
		var entry := entry_value as Dictionary
		var power_id := String(entry.get("power_id", ""))
		refunded += int(entry.get("cost", 0))
		(_sim.purchased_powers[team] as Array).erase(power_id)
		(_sim._team_sciences[team] as Array).erase(String(entry.get("science_id", "")))
		var row: Dictionary = (_team_tree(team).get("powers", {}) as Dictionary).get(power_id, {}) as Dictionary
		if bool(row.get("nonpressable", false)) and (_sim._consumed_nonpressable_powers.get(team, {}) as Dictionary).has(power_id):
			(_sim._consumed_nonpressable_powers[team] as Dictionary).erase(power_id)
			if String((row.get("effect", {}) as Dictionary).get("kind", "")) == "scavenger_bounty":
				_sim._scavenger_bounty_percent[team] = 0.0
		restored.append(power_id)
	_sim._staged_purchases[team] = []
	if refunded > 0:
		_sim.team_power_points[team] = power_points(team) + refunded
	_sim._emit_event("power.reset", 0, 0, {"team": team, "refunded": refunded, "powers": restored})
	return {"ok": true, "reason": "", "refunded": refunded, "powers": restored}


func accept_spellbook_purchases(team: int) -> Dictionary:
	## Retail ACCEPT (including closing the orb): the session's picks commit.
	if not bool(_team_tree(team).get("ready", false)):
		return {"ok": false, "reason": "spellbook-unavailable"}
	sim._staged_purchases[team] = []
	return {"ok": true, "reason": ""}


func power_cooldown_state(team: int, power_id: String) -> Dictionary:
	var _sim = sim
	var row: Dictionary = (_team_tree(team).get("powers", {}) as Dictionary).get(power_id, {}) as Dictionary
	if row.is_empty():
		return {}
	var total = _sim.spell_recharge_ticks_for_team(team, int(row.get("reload_ticks", 1)))
	var ready_tick := int((_sim._power_cooldown_until.get(team, {}) as Dictionary).get(power_id, -1))
	var remaining = maxi(0, ready_tick - _sim.tick_index)
	return {
		"total_ticks": total,
		"remaining_ticks": remaining,
		"progress": 1.0 - (float(remaining) / float(maxi(1, total))),
	}


func spellbook_power_radius_sim(power_id: String) -> float:
	## The doc's resolved targeting-cursor radius mapped into sim units (the
	## targeting ring's size; 0 when the power/doc carries none).
	var row: Dictionary = sim._spellbook_powers.get(power_id, {}) as Dictionary
	if row.is_empty():
		return 0.0
	return float(row.get("radius_cursor_source", 0.0)) * _spellbook_world_scale()


func spellbook_ui_state(team: int) -> Dictionary:
	## Per-power orb state: the presentation layer never derives tree logic.
	var tree := _team_tree(team)
	var tree_powers: Dictionary = tree.get("powers", {}) as Dictionary
	var tree_sciences: Dictionary = tree.get("sciences", {}) as Dictionary
	var tree_intrinsic: Array = tree.get("intrinsic", []) as Array
	var tree_science_to_power: Dictionary = tree.get("science_to_power", {}) as Dictionary
	var powers: Dictionary = {}
	for power_id in (tree.get("order", []) as Array):
		var row: Dictionary = tree_powers[power_id]
		var owned := has_power(team, power_id)
		var prerequisites_met := _power_prerequisites_met(team, String(row.get("science_id", "")))
		var cooldown := power_cooldown_state(team, power_id)
		var staged := false
		for entry_value in Array(sim._staged_purchases.get(team, [])):
			if String((entry_value as Dictionary).get("power_id", "")) == power_id:
				staged = true
				break
		var locked_reason := ""
		if not owned:
			if not prerequisites_met:
				locked_reason = "prerequisites-unmet"
			elif power_points(team) < int(row.get("cost", 0)):
				locked_reason = "insufficient-power-points"
			elif not bool(row.get("castable", false)):
				locked_reason = String(row.get("locked_reason", "effect-unsupported"))
		var prereq_power_ids: Array = []
		var science_row: Dictionary = tree_sciences.get(String(row.get("science_id", "")), {}) as Dictionary
		for group_value in Array(science_row.get("groups", [])):
			# Only groups this faction can ever complete draw a palantir fork:
			# every member must be intrinsic-owned or a tree science (groups
			# naming another faction's root, e.g. SCIENCE_DWARVES, are dead
			# paths the retail orb does not draw).
			var achievable := true
			for member in group_value as Array:
				var member_id := String(member)
				if not tree_intrinsic.has(member_id) and not tree_science_to_power.has(member_id):
					achievable = false
					break
			if not achievable:
				continue
			for member in group_value as Array:
				var prereq_power_id := String(tree_science_to_power.get(String(member), ""))
				if prereq_power_id != "" and not prereq_power_ids.has(prereq_power_id):
					prereq_power_ids.append(prereq_power_id)
		powers[power_id] = {
			"id": power_id,
			"cost": int(row.get("cost", 0)),
			"purchase_slot": int(row.get("purchase_slot", 0)),
			"cast_slot": int(row.get("cast_slot", 0)),
			"owned": owned,
			"staged": staged,
			"prerequisites_met": prerequisites_met,
			"prereq_power_ids": prereq_power_ids,
			"purchasable": not owned and prerequisites_met and power_points(team) >= int(row.get("cost", 0)),
			"castable": bool(row.get("castable", false)),
			"nonpressable": bool(row.get("nonpressable", false)),
			"locked_reason": locked_reason,
			"effect_locked_reason": String(row.get("locked_reason", "")),
			"needs_target_pos": bool(row.get("needs_target_pos", false)),
			"radius_cursor_source": float(row.get("radius_cursor_source", 0.0)),
			"sound_id": String(row.get("sound_id", "")),
			"cooldown": cooldown,
		}
	return {"points": power_points(team), "powers": powers}


func award_power_kill(team: int) -> void:
	# Creeps are excluded from the spellbook economy: a creep kill never banks
	# power points for the creep owner. Rostered killers of creeps still earn.
	var _sim = sim
	if not _sim._is_combatant_team(team):
		return
	var kills_per_point := maxi(1, int(_sim._rules.get("power_point_kills", _sim.POWER_POINT_KILLS)))
	_sim._kills_toward_power_point[team] = int(_sim._kills_toward_power_point.get(team, 0)) + 1
	if int(_sim._kills_toward_power_point[team]) >= kills_per_point:
		_sim._kills_toward_power_point[team] = 0
		_sim.team_power_points[team] = power_points(team) + 1
		_sim._emit_event("power.point_earned", 0, 0, {"team": team, "points": power_points(team)})


## Rank.ini player ladder: SkillPointsNeededDefault is the threshold that
## promotes the player, SciencePurchasePointsGranted is the spell-point award
## that promotion pays. Configuration arrives with the compiled spellbook
## document (registration.powerTree.rankScienceGrants) or directly through
## configure_player_rank_science_grants.
##
## DEFERRED, stated rather than hidden: these ledgers are NOT part of
## _authoritative_state(). Nothing in live play awards player skill points yet
## — the earn rate (skill points per kill/damage) is unresolved from retail
## data, exactly like the PROVISIONAL power-point rate above — so the ladder
## cannot advance during a match and has no live state to snapshot. When that
## earn rate lands, these three ledgers must join the authoritative state and
## the state pin has to be re-minted deliberately by the owner.


func configure_player_rank_science_grants(rows: Array) -> bool:
	## Bind the compiled Rank ladder. Fails closed on a malformed row, a
	## non-ascending rank, a non-ascending threshold, or a missing grant: a
	## ladder that cannot say which rank a crossing belongs to must not run.
	var _sim = sim
	var ladder: Array[Dictionary] = []
	_sim._player_rank_ladder_error = ""
	var previous_rank := 0
	var previous_threshold := -1
	for row_value in rows:
		if typeof(row_value) != TYPE_DICTIONARY:
			return _reject_player_rank_ladder("rank ladder row is malformed")
		var row := row_value as Dictionary
		var rank := int(row.get("rank", 0))
		if rank <= previous_rank:
			return _reject_player_rank_ladder(
				"rank ladder ranks do not ascend: %d follows %d" % [rank, previous_rank]
			)
		var granted := _rank_ladder_integer(row, "sciencePurchasePointsGranted")
		if granted < 0:
			return _reject_player_rank_ladder(
				"Rank %d has no resolved %s" % [rank, _sim.RANK_SCIENCE_PURCHASE_POINTS_GRANTED_FIELD]
			)
		var threshold := _rank_ladder_integer(row, "skillPointsNeededDefault")
		if threshold < 0:
			return _reject_player_rank_ladder(
				"Rank %d has no resolved %s" % [rank, _sim.RANK_SKILL_POINTS_NEEDED_FIELD]
			)
		if threshold <= previous_threshold:
			return _reject_player_rank_ladder(
				"Rank %d %s does not ascend: %d follows %d"
				% [rank, _sim.RANK_SKILL_POINTS_NEEDED_FIELD, threshold, previous_threshold]
			)
		previous_rank = rank
		previous_threshold = threshold
		ladder.append({
			"rank": rank,
			"granted": granted,
			"skill_points_needed": threshold,
			"granted_receipt": (row.get("sciencePurchasePointsGranted", {}) as Dictionary).duplicate(true),
			"threshold_receipt": (row.get("skillPointsNeededDefault", {}) as Dictionary).duplicate(true),
		})
	if ladder.is_empty():
		return _reject_player_rank_ladder("rank ladder carries no ranks")
	if ladder == _sim._player_rank_ladder:
		# Rank.ini is a system file, so a cross-faction team document carries
		# the same ladder. Re-binding an identical ladder must not wipe the
		# per-team rank ledgers that are already standing.
		return true
	_sim._player_rank_ladder = ladder
	_sim._team_player_rank.clear()
	_sim._team_player_skill_points.clear()
	_sim._team_player_rank_granted.clear()
	return true


func _reject_player_rank_ladder(reason: String) -> bool:
	var _sim = sim
	_sim._player_rank_ladder.clear()
	_sim._player_rank_ladder_error = reason
	return false


func _rank_ladder_integer(row: Dictionary, key: String) -> int:
	var contract: Dictionary = row.get(key, {}) as Dictionary
	var value: Variant = contract.get("value")
	if typeof(value) not in [TYPE_INT, TYPE_FLOAT]:
		return -1
	var number := float(value)
	if not is_finite(number) or number < 0.0 or number != floor(number):
		return -1
	return int(number)


func player_rank_ladder_error() -> String:
	return sim._player_rank_ladder_error


func player_rank_ladder_size() -> int:
	return sim._player_rank_ladder.size()


func player_rank(team: int) -> int:
	return int(sim._team_player_rank.get(team, 0))


func player_skill_points(team: int) -> int:
	return int(sim._team_player_skill_points.get(team, 0))


func advance_player_rank(team: int, rank: int) -> Dictionary:
	## Promote a player to `rank`, paying every authored
	## SciencePurchasePointsGranted the promotion crosses. Idempotent: a rank
	## already reached grants nothing and says so with granted = 0.
	var _sim = sim
	if _sim._player_rank_ladder.is_empty():
		return {"ok": false, "reason": "rank-ladder-unavailable", "detail": _sim._player_rank_ladder_error}
	if not _sim._is_combatant_team(team):
		return {"ok": false, "reason": "not-a-combatant-team"}
	var known := false
	for entry in _sim._player_rank_ladder:
		if int(entry.get("rank", 0)) == rank:
			known = true
			break
	if not known:
		return {"ok": false, "reason": "unknown-rank", "rank": rank}
	var granted_total := 0
	var crossed: Array[int] = []
	var ledger: Dictionary = _sim._team_player_rank_granted.get(team, {}) as Dictionary
	for entry in _sim._player_rank_ladder:
		var entry_rank := int(entry.get("rank", 0))
		if entry_rank > rank or ledger.has(entry_rank):
			continue
		ledger[entry_rank] = int(entry.get("granted", 0))
		granted_total += int(entry.get("granted", 0))
		crossed.append(entry_rank)
	_sim._team_player_rank_granted[team] = ledger
	_sim._team_player_rank[team] = maxi(player_rank(team), rank)
	if granted_total > 0:
		_sim.team_power_points[team] = power_points(team) + granted_total
		_sim._emit_event("power.rank_granted", 0, 0, {
			"team": team,
			"rank": player_rank(team),
			"granted": granted_total,
			"points": power_points(team),
		})
	return {
		"ok": true,
		"reason": "",
		"rank": player_rank(team),
		"granted": granted_total,
		"crossed_ranks": crossed,
		"points": power_points(team),
	}


func award_player_skill_points(team: int, amount: int) -> Dictionary:
	## Bank skill points and promote across every threshold they reach. This is
	## the authored trigger for the spell-point grant; the amount per kill is
	## the caller's contract, not this function's invention.
	var _sim = sim
	if _sim._player_rank_ladder.is_empty():
		return {"ok": false, "reason": "rank-ladder-unavailable", "detail": _sim._player_rank_ladder_error}
	if not _sim._is_combatant_team(team):
		return {"ok": false, "reason": "not-a-combatant-team"}
	if amount < 0:
		return {"ok": false, "reason": "negative-skill-points"}
	_sim._team_player_skill_points[team] = player_skill_points(team) + amount
	var reached := player_rank(team)
	for entry in _sim._player_rank_ladder:
		if int(entry.get("skill_points_needed", 0)) <= player_skill_points(team):
			reached = maxi(reached, int(entry.get("rank", 0)))
	var verdict := advance_player_rank(team, reached) if reached > 0 else {"ok": true, "reason": "", "rank": 0, "granted": 0}
	verdict["skill_points"] = player_skill_points(team)
	return verdict


## Retail field identities this file consumes from the compiled spellbook
## document. Packs cooked before the field-contract receipts landed carry the
## resolved values without them; the accessors below say so instead of
## inventing a receipt.


func _spellbook_science_document_rows(team: int) -> Array:
	## The Science rows of the document this team plays: its cross-faction
	## override when it has one, otherwise the global document.
	var _sim = sim
	var document = _sim._spellbook_document
	if _sim._team_spellbooks.has(team):
		document = (_sim._team_spellbooks[team] as Dictionary).get("document", {}) as Dictionary
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var power_tree: Dictionary = registration.get("powerTree", {}) as Dictionary
	return power_tree.get("sciences", []) as Array


func _spellbook_science_document_row(team: int, science_id: String) -> Dictionary:
	if science_id == "":
		return {}
	for row_value in _spellbook_science_document_rows(team):
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := row_value as Dictionary
		if String(row.get("id", "")) == science_id:
			return row
	return {}


func _science_field_receipt(row: Dictionary, field: String) -> Dictionary:
	return ((row.get("fieldContracts", {}) as Dictionary).get(field, {}) as Dictionary).duplicate(true)


func science_purchase_cost(science_id: String, multiplayer: bool) -> int:
	## Retail authors two purchase costs per Science: SciencePurchasePointCost
	## for skirmish/campaign and SciencePurchasePointCostMP for multiplayer.
	## The palantir spends the one matching the match kind. A science the
	## document does not carry, or one whose cost did not resolve, returns -1 —
	## never 0, which would read as "free".
	var row := _spellbook_science_document_row(sim.PLAYER_TEAM, science_id)
	if row.is_empty():
		return -1
	var cost_key := "pointCostMP" if multiplayer else "pointCost"
	var cost_value: Variant = (row.get(cost_key, {}) as Dictionary).get("value")
	if typeof(cost_value) not in [TYPE_INT, TYPE_FLOAT]:
		return -1
	var cost := float(cost_value)
	if not is_finite(cost) or cost < 0.0 or cost != floor(cost):
		return -1
	return int(cost)


func science_purchase_cost_receipt(science_id: String, multiplayer: bool) -> Dictionary:
	## The authored source receipt behind science_purchase_cost, when the pack
	## carries field contracts. Empty for packs cooked before they existed.
	var _sim = sim
	var row := _spellbook_science_document_row(_sim.PLAYER_TEAM, science_id)
	if row.is_empty():
		return {}
	return _science_field_receipt(
		row,
		_sim.SCIENCE_PURCHASE_POINT_COST_MP_FIELD if multiplayer else _sim.SCIENCE_PURCHASE_POINT_COST_FIELD,
	)


func science_is_grantable(science_id: String) -> bool:
	## Science IsGrantable = Yes marks a science that may be handed to a player
	## outside the purchase flow (rank rewards, scripts). A science the document
	## does not carry is not grantable — fail closed.
	var row := _spellbook_science_document_row(sim.PLAYER_TEAM, science_id)
	if row.is_empty():
		return false
	return bool(row.get("isGrantable", false))


func grant_science(team: int, science_id: String) -> Dictionary:
	## Grant one science without spending purchase points, the way a rank
	## reward or a map script does. Gated exactly by the authored contract:
	## IsGrantable = Yes, PrerequisiteSciences satisfied, not already owned.
	var _sim = sim
	if not _sim._is_combatant_team(team):
		return {"ok": false, "reason": "not-a-combatant-team"}
	var row := _spellbook_science_document_row(team, science_id)
	if row.is_empty():
		return {"ok": false, "reason": "unknown-science"}
	if not bool(row.get("isGrantable", false)):
		return {
			"ok": false,
			"reason": "science-not-grantable",
			"receipt": _science_field_receipt(row, _sim.SCIENCE_IS_GRANTABLE_FIELD),
		}
	if _science_owned(team, science_id):
		return {"ok": false, "reason": "already-owned"}
	if not _science_prerequisites_met(team, row):
		return {
			"ok": false,
			"reason": "prerequisites-unmet",
			"receipt": _science_field_receipt(row, _sim.SCIENCE_PREREQUISITE_SCIENCES_FIELD),
		}
	(_sim._team_sciences[team] as Array).append(science_id)
	_sim._emit_event("science.granted", 0, 0, {"team": team, "science_id": science_id})
	return {
		"ok": true,
		"reason": "",
		"science_id": science_id,
		"receipt": _science_field_receipt(row, _sim.SCIENCE_IS_GRANTABLE_FIELD),
	}


func _science_prerequisites_met(team: int, row: Dictionary) -> bool:
	## PrerequisiteSciences is an OR of AND groups: any fully owned group
	## admits the science, and an empty group list has no prerequisite at all.
	var groups: Array = row.get("prerequisiteGroups", []) as Array
	if groups.is_empty():
		return true
	for group_value in groups:
		if typeof(group_value) != TYPE_ARRAY:
			return false
		var satisfied := true
		for member_value in group_value as Array:
			if not _science_owned(team, String(member_value)):
				satisfied = false
				break
		if satisfied:
			return true
	return false


