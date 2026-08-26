extends RefCounted
## Spellbook/powers subsystem extracted from retail_slice_sim.gd (Q81
## strangler-fig extraction #11). Verbatim move, compiler-guided sim.
## prefixes, pin-verified byte-identical.

var _sim_ref: WeakRef
var sim:
	get:
		return _sim_ref.get_ref()


func _init(owning_sim) -> void:
	_sim_ref = weakref(owning_sim)


func set_spellbook_orb_open(open: bool) -> void:
	sim.clock_paused = open


func configure_spellbook_runtime(document: Dictionary) -> bool:
	sim._spellbook_ready = false
	sim._spellbook_error = ""
	sim._spellbook_document = document.duplicate(true)
	sim._spellbook_powers.clear()
	sim._spellbook_order.clear()
	sim._spellbook_sciences.clear()
	sim._spellbook_intrinsic.clear()
	sim._science_to_power.clear()
	sim._spellbook_command_points_upgrade.clear()
	if typeof(document) != TYPE_DICTIONARY or String(document.get("schema", "")) != sim.SPELLBOOK_SCHEMA:
		sim._spellbook_error = "spellbook document is missing or not an %s" % sim.SPELLBOOK_SCHEMA
		return false
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var power_tree: Dictionary = registration.get("powerTree", {}) as Dictionary
	var spell_book_object: Dictionary = registration.get("spellBook", {}) as Dictionary
	var command_points_upgrade_value: Variant = spell_book_object.get("commandPointsUpgrade")
	if command_points_upgrade_value != null:
		if typeof(command_points_upgrade_value) != TYPE_DICTIONARY:
			sim._spellbook_error = "spellbook CommandPointsUpgrade is malformed"
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
			sim._spellbook_error = "spellbook CommandPointsUpgrade is unsupported"
			return false
		for key in command_points_upgrade_keys:
			if not command_points_upgrade.has(key):
				sim._spellbook_error = "spellbook CommandPointsUpgrade is unsupported"
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
			sim._spellbook_error = "spellbook CommandPointsUpgrade is unsupported"
			return false
		sim._spellbook_command_points_upgrade = command_points_upgrade.duplicate(true)
	for intrinsic_value in Array(spell_book_object.get("intrinsicSciences", [])):
		if typeof(intrinsic_value) != TYPE_STRING or String(intrinsic_value).strip_edges() == "":
			sim._spellbook_error = "spellbook intrinsic sciences are malformed"
			return false
		sim._spellbook_intrinsic.append(String(intrinsic_value))
	# Sciences with an authored purchase block make up the palantir tree. Their
	# prerequisiteGroups are preserved OR groups: a science is purchasable when
	# ANY group is fully owned; an empty group list means no prerequisites.
	for science_value in Array(power_tree.get("sciences", [])):
		if typeof(science_value) != TYPE_DICTIONARY:
			sim._spellbook_error = "spellbook science entry is malformed"
			return false
		var science := science_value as Dictionary
		var science_id := String(science.get("id", ""))
		var purchase: Dictionary = science.get("purchase", {}) as Dictionary
		if science_id == "" or purchase.is_empty():
			continue
		var cost := int((science.get("pointCostMP", {}) as Dictionary).get("value", -1))
		var slot := int(purchase.get("slot", -1))
		if cost <= 0 or slot <= 0:
			sim._spellbook_error = "spellbook science '%s' has no resolved MP cost or purchase slot" % science_id
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
			sim._spellbook_error = "spellbook science '%s' prerequisite groups are malformed" % science_id
			return false
		sim._spellbook_sciences[science_id] = {"cost": cost, "slot": slot, "groups": groups}
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
	sim.ingest_ocl_leaves_from_document({"leaves": leaves})
	var weapon_leaves: Dictionary = {}
	for weapon_value in Array(leaves.get("weapons", [])):
		if typeof(weapon_value) == TYPE_DICTIONARY:
			weapon_leaves[String((weapon_value as Dictionary).get("id", ""))] = weapon_value
	for power_value in Array(power_tree.get("powers", [])):
		if typeof(power_value) != TYPE_DICTIONARY:
			sim._spellbook_error = "spellbook power entry is malformed"
			return false
		var power := power_value as Dictionary
		var power_id := String(power.get("id", ""))
		if power_id == "":
			sim._spellbook_error = "spellbook power is missing its id"
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
			if sim._spellbook_sciences.has(candidate):
				science_id = candidate
				break
		var science_row: Dictionary = sim._spellbook_sciences.get(science_id, {}) as Dictionary
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
			"reload_ticks": 0 if reload_authored_zero else maxi(1, roundi(reload_ms / 1000.0 / sim.TICK_SECONDS)),
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
			var support := _spellbook_effect_support(row, fields, references, modifier_leaves, object_leaves, ocl_leaves, weapon_leaves)
			row["castable"] = bool(support.get("ok", false))
			row["locked_reason"] = String(support.get("reason", ""))
			row["effect"] = support.get("effect", {})
		sim._spellbook_powers[power_id] = row
	if sim._spellbook_powers.is_empty():
		sim._spellbook_error = "spellbook document carries no powers"
		return false
	# Science → tree power index so the palantir can draw prerequisite forks
	# without re-deriving tree logic in the presentation layer.
	sim._science_to_power.clear()
	for tree_power_id in sim._spellbook_powers.keys():
		var tree_science := String((sim._spellbook_powers[tree_power_id] as Dictionary).get("science_id", ""))
		if tree_science != "":
			sim._science_to_power[tree_science] = String(tree_power_id)
	var ordered: Array[String] = []
	for power_id_value in sim._spellbook_powers.keys():
		ordered.append(String(power_id_value))
	ordered.sort_custom(func(a: String, b: String) -> bool:
		var row_a: Dictionary = sim._spellbook_powers[a]
		var row_b: Dictionary = sim._spellbook_powers[b]
		if int(row_a.get("purchase_slot", 0)) != int(row_b.get("purchase_slot", 0)):
			return int(row_a.get("purchase_slot", 0)) < int(row_b.get("purchase_slot", 0))
		return a.naturalnocasecmp_to(b) < 0
	)
	var seen_slots: Dictionary = {}
	for power_id_value in ordered:
		var slot := int((sim._spellbook_powers[power_id_value] as Dictionary).get("purchase_slot", 0))
		if slot <= 0 or seen_slots.has(slot):
			sim._spellbook_error = "spellbook purchase slots are missing or duplicated"
			return false
		seen_slots[slot] = true
	sim._spellbook_order = ordered
	# The compiled Rank ladder rides with the spellbook document. A pack cooked
	# before that contract existed carries no rankScienceGrants at all: the
	# ladder then stays unconfigured and every rank call refuses with that
	# reason instead of inventing a spell-point grant.
	if power_tree.has("rankScienceGrants"):
		if not configure_player_rank_science_grants(power_tree.get("rankScienceGrants", []) as Array):
			sim._spellbook_error = "spellbook rank ladder is malformed: %s" % sim._player_rank_ladder_error
			return false
	else:
		sim._player_rank_ladder.clear()
		sim._player_rank_ladder_error = "the compiled spellbook document carries no rankScienceGrants"
	_reset_spellbook_match_state()
	sim._spellbook_ready = true
	sim._state_hash_static_digest.clear()
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
	var saved := _spellbook_global_bundle_copy()
	var saved_error = sim._spellbook_error
	var saved_document = sim._spellbook_document
	var saved_sciences = sim._team_sciences.duplicate(true)
	var saved_cooldowns = sim._power_cooldown_until.duplicate(true)
	var saved_staged = sim._staged_purchases.duplicate(true)
	var saved_nonpressable = sim._consumed_nonpressable_powers.duplicate(true)
	var saved_scavenger = sim._scavenger_bounty_percent.duplicate(true)
	var ok := configure_spellbook_runtime(document)
	var parsed := _spellbook_global_bundle_copy() if ok else {}
	var parse_error = sim._spellbook_error
	# Restore the globals + match state exactly as they were before this call.
	_apply_spellbook_bundle(saved)
	sim._spellbook_document = saved_document
	sim._spellbook_error = saved_error
	sim._team_sciences = saved_sciences
	sim._power_cooldown_until = saved_cooldowns
	sim._staged_purchases = saved_staged
	sim._consumed_nonpressable_powers = saved_nonpressable
	sim._scavenger_bounty_percent = saved_scavenger
	if not ok:
		sim._team_spellbooks.erase(team)
		sim._team_spellbook_errors[team] = parse_error
		return false
	parsed["document"] = document.duplicate(true)
	sim._team_spellbooks[team] = parsed
	sim._team_spellbook_errors[team] = ""
	# Seed this team's ownership overlays from ITS OWN intrinsic sciences.
	sim._team_sciences[team] = (parsed.get("intrinsic", []) as Array).duplicate(true)
	sim._power_cooldown_until[team] = {}
	sim._staged_purchases[team] = []
	sim._consumed_nonpressable_powers[team] = {}
	sim._scavenger_bounty_percent[team] = 0.0
	sim.purchased_powers[team] = []
	return true


func team_spellbook_error(team: int) -> String:
	return String(sim._team_spellbook_errors.get(team, ""))


func team_has_spellbook_override(team: int) -> bool:
	return sim._team_spellbooks.has(team)


func _spellbook_global_bundle() -> Dictionary:
	## A shallow view of the current global tree fields. Used to lift a freshly
	## parsed tree into the per-team store and to save/restore around that parse.
	return {
		"ready": sim._spellbook_ready,
		"powers": sim._spellbook_powers,
		"order": sim._spellbook_order,
		"sciences": sim._spellbook_sciences,
		"science_to_power": sim._science_to_power,
		"intrinsic": sim._spellbook_intrinsic,
		"command_points_upgrade": sim._spellbook_command_points_upgrade,
	}


func _spellbook_global_bundle_copy() -> Dictionary:
	## A DEEP copy of the global tree, detached from the live global dicts so a
	## subsequent in-place clear/refill of those dicts cannot mutate it.
	var order_copy: Array[String] = []
	for power_id in sim._spellbook_order:
		order_copy.append(String(power_id))
	return {
		"ready": sim._spellbook_ready,
		"powers": sim._spellbook_powers.duplicate(true),
		"order": order_copy,
		"sciences": sim._spellbook_sciences.duplicate(true),
		"science_to_power": sim._science_to_power.duplicate(true),
		"intrinsic": (sim._spellbook_intrinsic as Array).duplicate(true),
		"command_points_upgrade": sim._spellbook_command_points_upgrade.duplicate(true),
	}


func _apply_spellbook_bundle(bundle: Dictionary) -> void:
	sim._spellbook_ready = bool(bundle.get("ready", false))
	sim._spellbook_powers = bundle.get("powers", {}) as Dictionary
	sim._spellbook_order = bundle.get("order", []) as Array[String]
	sim._spellbook_sciences = bundle.get("sciences", {}) as Dictionary
	sim._science_to_power = bundle.get("science_to_power", {}) as Dictionary
	sim._spellbook_intrinsic = bundle.get("intrinsic", []) as Array
	sim._spellbook_command_points_upgrade = bundle.get("command_points_upgrade", {}) as Dictionary


func _team_tree(team: int) -> Dictionary:
	## The tree a team resolves powers against: its own override when present,
	## otherwise a view of the global (default same-faction) tree. Read-only —
	## team ownership overlays live in the per-team maps, not in the tree.
	if sim._team_spellbooks.has(team):
		return sim._team_spellbooks[team]
	return _spellbook_global_bundle()


func _reset_spellbook_match_state() -> void:
	sim._team_sciences = sim._seed_team_map(sim._spellbook_intrinsic)
	# A team with its own faction tree starts from ITS intrinsic sciences, not
	# the global player-faction ones.
	for team_value in sim._team_spellbooks.keys():
		var override_tree: Dictionary = sim._team_spellbooks[team_value]
		sim._team_sciences[team_value] = (override_tree.get("intrinsic", []) as Array).duplicate(true)
	sim._power_cooldown_until = sim._seed_team_map({})
	sim._consumed_nonpressable_powers = sim._seed_team_map({})
	sim._scavenger_bounty_percent = sim._seed_team_map(0.0)
	sim._staged_purchases = sim._seed_team_map([])
	sim._pending_power_effects.clear()
	sim._active_groves.clear()
	sim._field_pings.clear()
	sim._weather_effects.clear()
	sim._summon_despawn_ticks.clear()
	sim._summon_aura_source_ids.clear()


## Timed spellbook effect state (volley strikes, summon hatches, groves).


func _spellbook_effect_support(power_row: Dictionary, fields: Array, references: Dictionary, modifier_leaves: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	## Evidence gate: a power becomes castable only when its converted leaves
	## fully determine the runtime effect. Anything unresolved stays locked
	## with the gap recorded (never an invented effect).
	var module := String(power_row.get("module", ""))
	var field_values: Dictionary = {}
	var field_resolved: Dictionary = {}
	for field_value in fields:
		if typeof(field_value) == TYPE_DICTIONARY:
			var field_row := field_value as Dictionary
			var field_key := String(field_row.get("key", ""))
			if field_row.has("resolvedText"):
				field_values[field_key] = String(field_row.get("resolvedText", ""))
			else:
				field_values[field_key] = String(field_row.get("value", ""))
			if field_row.has("resolved"):
				field_resolved[field_key] = float(field_row.get("resolved", 0.0))
			elif field_row.has("resolvedMax"):
				field_resolved[field_key] = float(field_row.get("resolvedMax", 0.0))
	match module:
		"PlayerHealSpecialPower":
			# HealAmount is the resolved fraction of member health restored; the
			# authored HealRadius define now resolves in the doc (cursor radius
			# remains the fallback when it does not). Rebuild-class powers are
			# the flat structure-heal shape: HealAsPercent No + STRUCTURE affects
			# + a flat authored amount.
			var amount := _spellbook_field_float(field_values, "HealAmount", 0.0)
			var radius = float(field_resolved.get("HealRadius", float(power_row.get("radius_cursor_source", 0.0))))
			var as_percent := String(field_values.get("HealAsPercent", "Yes")) != "No"
			if radius <= 0.0:
				return {"ok": false, "reason": "heal radius did not resolve in the document"}
			if as_percent and (amount <= 0.0 or amount > 1.0):
				return {"ok": false, "reason": "heal amount did not resolve in the document"}
			if not as_percent and amount <= 0.0:
				return {"ok": false, "reason": "flat structure-heal amount did not resolve in the document"}
			return {"ok": true, "effect": {"kind": "heal", "amount": amount, "as_percent": as_percent, "radius_source": radius, "affects": String(field_values.get("HealAffects", ""))}}
		"SpecialPowerModule":
			var modifier_id := String(field_values.get("AttributeModifier", ""))
			if not field_values.has("AttributeModifier"):
				# Retail sometimes ships a SpecialPowerModule whose whole payload is
				# COMMENTED OUT upstream and re-homed on another object. Fuel the Fires
				# is the case in this corpus: object/system/system.ini:369-378 comments
				# out AttributeModifier/Range/Affects with the note "Done in science
				# check on the lumber mill", and the live bonus is
				# SupplyCenterDockUpdate BonusScience = SCIENCE_FueltheFires /
				# BonusScienceMultiplier = 200% on Object LumberMill
				# (object/civilian/civilianbuildings.ini:18831-18836). The sim has no
				# worker/supply-dock economy for that multiplier to act on.
				return {"ok": false, "reason": "SpecialPowerModule authors no AttributeModifier: the payload is commented out on the module and re-homed on a SupplyCenterDockUpdate BonusScience, which the sim does not model"}
			if modifier_id == "" or not modifier_leaves.has(modifier_id):
				return {"ok": false, "reason": "attribute modifier '%s' is not a converted leaf" % modifier_id}
			# Retail AttributeModifier leaves author repeated Modifier lines
			# (DAMAGE_MULT, ARMOR, PRODUCTION, …). Collapsing by key kept only
			# the last row and falsely rejected powers that still carried a
			# resolved DAMAGE_MULT earlier in the list.
			var modifier_fields: Dictionary = {}
			var modifier_rows: Array = []
			for field_value in Array((modifier_leaves[modifier_id] as Dictionary).get("fields", [])):
				if typeof(field_value) != TYPE_DICTIONARY:
					continue
				var field_row := field_value as Dictionary
				var field_key := String(field_row.get("key", ""))
				var field_text := String(field_row.get("value", ""))
				if field_key == "Modifier":
					modifier_rows.append(field_text)
				elif field_key != "":
					modifier_fields[field_key] = field_text
			var damage_mult := 0.0
			var armor_mult := 0.0
			var production_mult := 0.0
			var invulnerable := false
			var unsupported_rows: Array = []
			# WHICH ROWS THE LEAF ACTUALLY AUTHORED. The three multipliers below
			# all default to the neutral 1.0 when their row is absent, so a
			# consumer reading only the numbers cannot tell an AUTHORED 100% from
			# a row that was never written. That distinction is not academic: the
			# spellbook matrix classifies a power as "castable but inert" when its
			# damage and armor multipliers are neutral and its production
			# multiplier is not, and "neutral" meant "absent OR authored-100%".
			# The grove-aura lane already publishes exactly this table for the
			# same reason (see the `authored_rows` key on the grove_aura effect).
			var authored_rows: Dictionary = {
				"DAMAGE_MULT": false, "ARMOR": false, "PRODUCTION": false
			}
			for modifier_text_value in modifier_rows:
				var parsed := _parse_modifier_row(String(modifier_text_value))
				# Fail-closed on an UNREADABLE row (see _parse_modifier_row). An
				# unconsumed KIND still falls through: this probe only asks whether the
				# leaf carries an effect the sim can apply, and `has_effect` below is
				# the verdict for that.
				if not parsed.get("ok", false):
					return {"ok": false, "reason": "attribute modifier '%s': %s" % [
						modifier_id, String(parsed.get("reason", "")),
					]}
				if not bool(parsed.get("supported", false)):
					# READ but not modelled — named and counted, never a silent drop and
					# never a shape error that takes the readable rows beside it down
					# with it. angmar/SpellBookSnowbind is exactly this case: its
					# `INVULNERABLE 0% SLASH PIERCE …` damage-type scope list has no
					# runtime here, while the `PRODUCTION 1%` row on the same leaf is
					# perfectly readable and used to be lost with it.
					unsupported_rows.append({
						"row": String(modifier_text_value),
						"shape": String(parsed.get("shape", "")),
						"reason": String(parsed.get("reason", "")),
					})
					continue
				var kind := String(parsed.get("kind", ""))
				# Only the percent shape is a multiplier; KIND_PLAIN is an absolute
				# magnitude (HEALTH 400) and must not be read as one.
				if String(parsed.get("shape", "")) != "percent":
					unsupported_rows.append({
						"row": String(modifier_text_value),
						"shape": String(parsed.get("shape", "")),
						"reason": "absolute-magnitude '%s' row has no multiplier runtime here" % kind,
					})
					continue
				var percent := float(parsed.get("value", 0.0))
				if kind == "DAMAGE_MULT":
					damage_mult = percent
					authored_rows["DAMAGE_MULT"] = true
				elif kind == "ARMOR":
					armor_mult = percent
					authored_rows["ARMOR"] = true
				elif kind == "PRODUCTION":
					production_mult = percent
					authored_rows["PRODUCTION"] = true
			var duration_ms := _spellbook_field_float(modifier_fields, "Duration", 0.0)
			# Duration may be an unresolved define name on the leaf; power-level
			# field_resolved already carries numeric AttributeModifierRange.
			if duration_ms <= 0.0 and modifier_fields.has("Duration"):
				var duration_text := String(modifier_fields.get("Duration", ""))
				# Common retail define pattern: NAME_EFFECT_DURATION — resolve via
				# power field table when the leaf only stores the define token.
				if duration_text != "" and field_resolved.has(duration_text):
					duration_ms = float(field_resolved.get(duration_text, 0.0))
			var range_source := _spellbook_field_float(field_values, "AttributeModifierRange", 0.0)
			if range_source <= 0.0 and field_resolved.has("AttributeModifierRange"):
				range_source = float(field_resolved.get("AttributeModifierRange", 0.0))
			# `invulnerable` can no longer be set from a row: retail authors ZERO bare
			# `INVULNERABLE` flags (census in _parse_modifier_row) — every authored
			# INVULNERABLE carries `0%` plus a damage-type scope list, which is
			# read-but-not-supported above. The field is kept (always false) so the
			# effect shape and its consumers do not move; blanket invulnerability was
			# never actually authored in this corpus.
			var has_effect := damage_mult > 0.0 or armor_mult > 0.0 or production_mult > 0.0 or invulnerable
			# Economy PRODUCTION auras (Industry / Dwarven Riches) are permanent
			# while the model condition holds — no Duration row. Combat auras
			# require a positive duration.
			var duration_ok := duration_ms > 0.0 or production_mult > 0.0
			if not has_effect or not duration_ok or range_source <= 0.0:
				return {"ok": false, "reason": "attribute modifier leaf lacks a resolved damage mult, duration, or range"}
			var duration_ticks := 0
			if duration_ms > 0.0:
				duration_ticks = maxi(1, roundi(duration_ms / 1000.0 / sim.TICK_SECONDS))
			return {"ok": true, "effect": {
				"kind": "attribute_modifier",
				"damage_mult": damage_mult if damage_mult > 0.0 else 1.0,
				"armor_mult": armor_mult if armor_mult > 0.0 else 1.0,
				"production_mult": production_mult if production_mult > 0.0 else 1.0,
				"invulnerable": invulnerable,
				"duration_ticks": duration_ticks,
				"permanent": duration_ticks == 0 and production_mult > 0.0,
				"range_source": range_source,
				"affects": String(field_values.get("AttributeModifierAffects", "")),
				# Which of the three multiplier rows the leaf actually authored, so
				# an authored-neutral 100% is distinguishable from an absent row.
				"authored_rows": authored_rows,
				# Named residual rows carried onto the effect so the runner and the
				# report can COUNT them instead of losing them.
				"unsupported_modifier_rows": unsupported_rows,
			}}
		"OCLSpecialPower":
			return _spellbook_ocl_support(
				power_row, references, modifier_leaves, object_leaves, ocl_leaves, weapon_leaves,
				String(field_values.get("CreateLocation", "")),
				String(field_values.get("NearestSecondaryObjectFilter", ""))
			)
		"DarknessSpecialPower":
			return _spellbook_weather_modifier_support(field_values, field_resolved, modifier_leaves)
		"FreezingRainSpecialPower":
			return _spellbook_weather_anticategory_support(field_values, field_resolved)
		"UntamedAllegianceSpecialPower":
			return _spellbook_untamed_allegiance_support(field_values, field_resolved)
		"DevastateSpecialPower":
			# Retail's Devastation squeezes resources out of the map's TREE objects
			# (TreeValueMultiplier 50%, TreeValueTotalCap 1500 -
			# object/system/system.ini:241-252) and fires DevastationEntWeapon, whose
			# only damage nugget is filtered to `NONE +RohanGenericEnt +RohanTreeBerd
			# ENEMIES` (weapon.ini DevastationEntWeapon). The sim models neither trees
			# as harvestable objects nor Ent units, so BOTH halves have no target.
			return {"ok": false, "reason": "DevastateSpecialPower has no target in the sim: its resource half converts TREE objects (TreeValueMultiplier %s, TreeValueTotalCap %d), which the sim does not model, and its damage half (FireWeapon '%s') is filtered to Ents only" % [
				String(field_values.get("TreeValueMultiplier", "")),
				int(field_resolved.get("TreeValueTotalCap", 0)),
				String(field_values.get("FireWeapon", "")),
			]}
		"ScavengerSpecialPower":
			var bounty_percent := _spellbook_field_float(field_values, "BountyPercent", -1.0)
			if bounty_percent < 0.0:
				return {"ok": false, "reason": "ScavengerSpecialPower BountyPercent did not resolve in the document"}
			return {"ok": true, "effect": {"kind": "scavenger_bounty", "bounty_percent": bounty_percent}}
		"ElvenWoodSpecialPower":
			return _spellbook_grove_support(field_values, field_resolved, references, modifier_leaves, object_leaves, ocl_leaves, "ElvenGroveObject")
		"TaintSpecialPower":
			# Retail's TaintSpecialPower and ElvenWoodSpecialPower are the SAME
			# module shape with a different planted object: `TaintObject/TaintRadius/
			# TaintFX/TaintOCL` against `ElvenGroveObject/ElvenWoodRadius/
			# ElvenWoodFX/ElvenWoodOCL` (data/ini/object/system/system.ini:31-38 vs
			# :829-837). TaintLand and ElvenGrove are byte-identical objects apart
			# from Side and the RequiredConditions cell type
			# (object/evilfaction/sim.structures/taintland.ini:3-46 vs
			# object/goodfaction/sim.structures/elven/grove.ini:3-47), so one resolver
			# serves both.
			return _spellbook_grove_support(field_values, field_resolved, references, modifier_leaves, object_leaves, ocl_leaves, "TaintObject")
		"CloudBreakSpecialPower":
			return _spellbook_cloudbreak_support(field_values, field_resolved)
		_:
			return {"ok": false, "reason": "unsupported effect module '%s'" % module}


func _spellbook_field_float(fields: Dictionary, key: String, fallback: float) -> float:
	var raw := String(fields.get(key, ""))
	if raw == "" or not raw.is_valid_float():
		return fallback
	return float(raw)


func _parse_modifier_row(value: String) -> Dictionary:
	## THE one reader of an AttributeModifier leaf's `Modifier =` row. Every
	## resolver that used to split the text itself is routed through here.
	##
	## Retail authors these rows TAB-separated as often as space-separated
	## (attributemodifier.ini:74 `Modifier = ARMOR<TAB>50%` against :75
	## `Modifier = DAMAGE_MULT 150%`) and the converter preserves the tab
	## verbatim, so a space-only split silently dropped every tabbed row. That
	## is exactly what kept Elven Wood reading as "modifier is not converted",
	## and duplicating the normalization at four call sites is how one of them
	## kept missing it.
	##
	## SHAPES ARE A CENSUS, NOT A GUESS. Round 18 counted every `Modifier =` row
	## in the PURE RETAIL oracle tree
	## (workspace/retail-work/editions/rotwk/cache/effective-assets, all .ini/.inc,
	## comments stripped): 894 rows total, and they fall into exactly three
	## authored shapes —
	##
	##   KIND_PCT   386  `ARMOR<TAB>50%`, `DAMAGE_MULT 150%`
	##                     -> shape "percent", value = n / 100.0
	##   KIND_PLAIN 363  `HEALTH 400`, `CRUSHABLE_LEVEL 3`, and the far commoner
	##                   `ARMOR <DEFINE_TOKEN>` / `HEALTH <DEFINE_TOKEN>` form
	##                     -> shape "plain", value = n (NOT divided); an
	##                        unresolved define token is unreadable, ok=false
	##   MULTI      145  `INVULNERABLE 0% SLASH PIERCE …` (a damage-type scope
	##                   list), `ARMOR <TOKEN> CRUSH`, `DAMAGE_MULT
	##                   #MULTIPLY( X 0.60 )`, `PRODUCTION <TOKEN> %` (the
	##                   percent sign detached by whitespace)
	##                     -> shape "percent_scoped"/"plain_scoped"/"expression",
	##                        ok=true, supported=false, with a NAMED reason
	##
	##   BARE_FLAG    0  ZERO rows in the corpus are a single bare token. The old
	##                   `parts.size() == 1 -> flag row` branch was dead code
	##                   invented from the *appearance* of `INVULNERABLE`, whose
	##                   real authored form always carries `0%` plus a scope list
	##                   (attributemodifier.ini:909, :1079). It is deleted: one
	##                   token is now unreadable and fails closed.
	##
	## THREE-WAY VERDICT, because two different mistakes are possible:
	##   ok=false                -> UNREADABLE. Fail closed at the call site: a
	##                              row nobody can read is a buff that would go
	##                              missing in silence.
	##   ok=true, supported=false -> READ, but its runtime is not modelled. The
	##                              caller may explicitly not-support it by name
	##                              (`reason`) and carry on with the rows it can
	##                              apply. A shape error here used to lock whole
	##                              powers (angmar/SpellBookSnowbind lost its
	##                              readable `PRODUCTION 1%` because the
	##                              INVULNERABLE row beside it was called a shape
	##                              error).
	##   ok=true, supported=true  -> a plain scalar the generic consumers apply.
	##
	## `flag` is retained on every result (always false) so callers written
	## against the old contract keep compiling; nothing sets it any more.
	var parts_raw := value.replace("\t", " ").split(" ", false)
	var parts: Array[String] = []
	for part_value in parts_raw:
		parts.append(String(part_value))
	if parts.is_empty():
		return {"ok": false, "reason": "modifier row is empty"}
	var kind: String = parts[0]
	var rest: Array[String] = parts.slice(1)
	if rest.size() >= 2 and rest[rest.size() - 1] == "%":
		# `PRODUCTION ROHAN_FARM_LVL2_PRODUCTION  %` (attributemodifier.ini:1406):
		# retail lets the percent sign drift off its magnitude. Reattach it before
		# anything else so the row is judged on its real shape.
		rest = rest.slice(0, rest.size() - 1)
		rest[rest.size() - 1] = rest[rest.size() - 1] + "%"
	if rest.is_empty():
		return {"ok": false, "reason": "modifier row '%s' is a single token with no magnitude; retail authors none (census: 0 of 894)" % value}
	var head: String = rest[0]
	var scope: Array = rest.slice(1)
	if head.begins_with("#"):
		# `DAMAGE_MULT #MULTIPLY( CREATE_A_HERO_ATTRIBUTE_MULTIPLIER 0.60 )`.
		# The pack ships the expression verbatim; the sim has no INI expression
		# evaluator, so this is read-but-not-supported rather than a shape error.
		return {
			"ok": true, "supported": false, "flag": false, "shape": "expression",
			"kind": kind, "value": 0.0, "scope": rest,
			"reason": "modifier row '%s' is an authored INI expression; the pack ships it unevaluated and the sim has no expression evaluator" % value,
		}
	var magnitude := 0.0
	var shape := ""
	if head.ends_with("%"):
		var percent_text: String = head.trim_suffix("%")
		if not percent_text.is_valid_float():
			return {"ok": false, "reason": "modifier row '%s' has a non-numeric percent" % value}
		magnitude = float(percent_text) / 100.0
		shape = "percent"
	elif head.is_valid_float():
		# KIND_PLAIN carries an ABSOLUTE magnitude (HEALTH 400), not a percent.
		# Callers must read `shape` before treating `value` as a multiplier.
		magnitude = float(head)
		shape = "plain"
	else:
		return {"ok": false, "reason": "modifier row '%s' magnitude '%s' is an unresolved define token" % [value, head]}
	if scope.is_empty():
		return {
			"ok": true, "supported": true, "flag": false, "shape": shape,
			"kind": kind, "value": magnitude, "scope": [],
		}
	return {
		"ok": true, "supported": false, "flag": false, "shape": shape + "_scoped",
		"kind": kind, "value": magnitude, "scope": scope,
		"reason": "modifier row '%s' scopes %s to the damage-type list %s; the sim applies modifiers globally and does not model per-damage-type scoping" % [
			value, kind, " ".join(scope),
		],
	}


func _spellbook_ocl_support(power_row: Dictionary, references: Dictionary, modifier_leaves: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary, weapon_leaves: Dictionary, create_location: String = "", secondary_object_filter: String = "") -> Dictionary:
	## OCL powers dispatch on the spawned objects' converted evidence:
	## fire-weapon receptacles (volley/quake), summon eggs, or sim.structures.
	var ocl_ids: Array = references.get("objectCreationLists", []) as Array
	if ocl_ids.is_empty():
		return {"ok": false, "reason": "power references no object-creation list"}
	var ocl_id := String(ocl_ids[0])
	var ocl: Dictionary = ocl_leaves.get(ocl_id, {}) as Dictionary
	if ocl.is_empty():
		return {"ok": false, "reason": "object-creation list '%s' is not a converted leaf" % ocl_id}
	var spawns: Array = []
	var missing: Array = []
	var creates: Array = ocl.get("createObjects", []) as Array
	for create_index in range(creates.size()):
		var create_value: Variant = creates[create_index]
		if typeof(create_value) != TYPE_DICTIONARY:
			continue
		var create := create_value as Dictionary
		for object_name_value in Array(create.get("objects", [])):
			var object_name := String(object_name_value)
			var leaf: Dictionary = object_leaves.get(object_name, {}) as Dictionary
			if leaf.is_empty():
				missing.append(object_name)
			else:
				spawns.append({"create": create, "create_index": create_index, "leaf": leaf})
	if not missing.is_empty():
		return {"ok": false, "reason": "spawned object(s) %s are not converted leaves" % ", ".join(missing)}
	if spawns.is_empty():
		return {"ok": false, "reason": "object-creation list '%s' creates no objects" % ocl_id}
	var first_leaf: Dictionary = (spawns[0] as Dictionary)["leaf"]
	if not Array(first_leaf.get("fireWeapons", [])).is_empty():
		return _spellbook_fire_weapon_support(spawns, weapon_leaves)
	# Reveal/field "ping" objects are not units and must not be routed through the
	# summon resolver, which rejected them for the irrelevant reason that an
	# IMMOBILE object authors no locomotor. Returns {} for anything else.
	var ping_verdict := _spellbook_field_ping_support(spawns, modifier_leaves)
	if not ping_verdict.is_empty():
		return ping_verdict
	# Shape detectors that own a MORE PRECISE reason than the generic summon or
	# structure paths would produce. Each names the single authored module that
	# carries the whole power, so a future converter change can be aimed at it.
	var shaped := _spellbook_ocl_named_gap(spawns, object_leaves, ocl_leaves, create_location, secondary_object_filter)
	if not shaped.is_empty():
		return shaped
	for spawn_value in spawns:
		var hatch_leaf: Dictionary = (spawn_value as Dictionary).get("leaf", {}) as Dictionary
		if typeof(hatch_leaf.get("hatch", null)) == TYPE_DICTIONARY:
			var summon_verdict := _spellbook_summon_support(spawns, modifier_leaves, object_leaves, ocl_leaves, weapon_leaves)
			if bool(summon_verdict.get("ok", false)):
				return summon_verdict
			var preview := _spellbook_summon_literal_preview(spawns, object_leaves, ocl_leaves)
			if bool(preview.get("ok", false)):
				summon_verdict["effect"] = preview.get("effect", {})
			return summon_verdict
	var body_kinds: Array = first_leaf.get("bodyKinds", []) as Array
	if body_kinds.has("StructureBody"):
		return _spellbook_structure_summon_support(spawns[0], weapon_leaves)
	# Direct summon OCLs (Ents, Eagles, Crebain/Cave Bats) create their live
	# units without an egg. Dragon Strike is identified by the spawned leaf's
	# authored-but-unconverted StrafeAreaUpdate, never by an unrelated OCL flag.
	for spawn_value in spawns:
		var leaf: Dictionary = (spawn_value as Dictionary).get("leaf", {}) as Dictionary
		if Array(leaf.get("unconvertedBehaviors", [])).has("StrafeAreaUpdate"):
			var preview := _spellbook_direct_summon_support(spawns, modifier_leaves, object_leaves, weapon_leaves)
			var verdict := {"ok": false, "reason": "spawned object '%s' requires StrafeAreaUpdate attack-run runtime" % String(leaf.get("id", ""))}
			if bool(preview.get("ok", false)):
				verdict["effect"] = preview.get("effect", {})
			return verdict
	# A direct summoned unit may author a SlowDeath corpse-effect OCL. Only an
	# actual egg is blocked here: its unconverted SlowDeath OCL creates the live
	# summon payload. Scan every leaf so a later egg cannot evade the gate.
	for spawn_value in spawns:
		var leaf: Dictionary = (spawn_value as Dictionary).get("leaf", {}) as Dictionary
		if _spellbook_has_unconverted_hatch_payload(leaf, object_leaves, ocl_leaves):
			return {"ok": false, "reason": "spawned object '%s' has an unconverted SlowDeathBehavior hatch OCL" % String(leaf.get("id", ""))}
	return _spellbook_direct_summon_support(spawns, modifier_leaves, object_leaves, weapon_leaves)


func _spellbook_has_unconverted_hatch_payload(leaf: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary) -> bool:
	if not Array(leaf.get("unconvertedBehaviors", [])).has("SlowDeathBehavior"):
		return false
	for ocl_id_value in Array(leaf.get("unconvertedSlowDeathOcls", [])):
		var ocl: Dictionary = ocl_leaves.get(String(ocl_id_value), {}) as Dictionary
		for create_value in Array(ocl.get("createObjects", [])):
			if typeof(create_value) != TYPE_DICTIONARY:
				continue
			for object_id_value in Array((create_value as Dictionary).get("objects", [])):
				var payload: Dictionary = object_leaves.get(String(object_id_value), {}) as Dictionary
				if payload.is_empty():
					continue
				if payload.has("horde") or payload.has("weaponId") or int(payload.get("maxHealth", 0)) > 1:
					return true
	return false


func _spellbook_hatch_payload_leaves(leaf: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary) -> Array:
	## One hatch level down: the objects an egg's CreateObjectDie OCL creates.
	## Several powers park their entire runtime behind the egg (Avalanche's
	## FloodUpdate wave, the Citadel's CastleBehavior), so a named-gap scan that
	## only looked at the power OCL's direct spawns would miss them.
	var payload: Array = []
	var hatch_value: Variant = leaf.get("hatch", null)
	if typeof(hatch_value) != TYPE_DICTIONARY:
		return payload
	var ocl: Dictionary = ocl_leaves.get(String((hatch_value as Dictionary).get("ocl", "")), {}) as Dictionary
	for create_value in Array(ocl.get("createObjects", [])):
		if typeof(create_value) != TYPE_DICTIONARY:
			continue
		for object_id_value in Array((create_value as Dictionary).get("objects", [])):
			var child: Dictionary = object_leaves.get(String(object_id_value), {}) as Dictionary
			if not child.is_empty():
				payload.append(child)
	return payload


func _spellbook_ocl_named_gap(spawns: Array, object_leaves: Dictionary, ocl_leaves: Dictionary, create_location: String, secondary_object_filter: String) -> Dictionary:
	## Precise fail-closed verdicts for OCL powers whose blocker is a single
	## authored module, reported BEFORE the generic summon/structure resolvers
	## produce an incidental (and misleading) reason such as "locomotion is not
	## converted" for an object that is deliberately IMMOBILE.
	## Returns {} when no named shape matches.
	var chain: Array = []
	for spawn_value in spawns:
		var spawn_leaf: Dictionary = (spawn_value as Dictionary).get("leaf", {}) as Dictionary
		if spawn_leaf.is_empty():
			continue
		chain.append(spawn_leaf)
		chain.append_array(_spellbook_hatch_payload_leaves(spawn_leaf, object_leaves, ocl_leaves))
	for leaf_value in chain:
		var leaf: Dictionary = leaf_value as Dictionary
		var unconverted: Array = leaf.get("unconvertedBehaviors", []) as Array
		if unconverted.has("FloodUpdate"):
			# Avalanche / Flood: the spawned object is an IMMOBILE INERT NO_COLLIDE
			# marker with an ImmortalBody, maxHealth 1 and a DeletionUpdate. Every
			# gameplay value of the power - the sweep path, its damage, the units it
			# throws - lives in FloodUpdate, which the compiler does not emit.
			return {"ok": false, "reason": "spawned object '%s' carries the whole power in FloodUpdate (wave path, damage and throw), which is not converted; the converted leaf is an immortal maxHealth-1 marker with only a %d ms DeletionUpdate" % [
				String(leaf.get("id", "")),
				int((leaf.get("deletion", {}) as Dictionary).get("maxMs", 0)),
			]}
		if unconverted.has("CastleBehavior"):
			# Citadel: the summoned object is a castle FOUNDATION. Its value is the
			# plot layout CastleBehavior declares (which expansion sites exist, what
			# may be built on them); without it the leaf is an UNATTACKABLE,
			# maxHealth-1 immortal marker with nothing to build on.
			return {"ok": false, "reason": "summoned object '%s' is a castle foundation whose CastleBehavior (plot layout and expansion sites) is not converted; the leaf converts to an UNATTACKABLE immortal maxHealth-%d marker with no plots" % [
				String(leaf.get("id", "")), int(leaf.get("maxHealth", 0)),
			]}
	if create_location == "USE_SECONDARY_OBJECT_LOCATION":
		# Bombard / Evil Bombard. The payload IS fully converted (20 seeds,
		# SpreadFormation, each seed hatching a projectile that fires
		# BombardProjectileWeapon at +600 ms for 400 SIEGE over radius 100), but the
		# barrage FOOTPRINT is the power, and where the seeds are laid out is not
		# determinable: CreateLocation names the secondary object while the same
		# CreateObject block also authors OrientInSecondaryDirection, so "at the
		# keep" and "at the cast point, oriented away from the keep" both read
		# consistently. The OpenSAGE oracle does not settle it either -
		# OCLSpecialPower.cs:35 throws NotImplementedException for this enum.
		# Guessing would move every impact, so the power stays locked.
		# TWO further runtime blockers ride the same payload, both verified against
		# the converted dwarves pack's BombardPhaseInitialWeapon /
		# BombardProjectileWeapon leaves:
		#   - the fear half is a LuaEventNugget (`LuaEvent = BeUncontrollablyAfraid`,
		#     Radius 200, SendToEnemies/SendToNeutral Yes) - retail routes it through
		#     the Lua event bus, which this sim does not have at all;
		#   - the impact half carries a MetaImpactNugget (ShockWaveAmount 75.0,
		#     ShockWaveTaperOff 1.0, ShockWaveRadius 50) - physics knockback with no
		#     model here.
		# So even a settled spread origin would leave the power partly unmodelled;
		# recording only the footprint ambiguity understated the gap.
		return {"ok": false, "reason": "OCLSpecialPower CreateLocation = USE_SECONDARY_OBJECT_LOCATION (NearestSecondaryObjectFilter '%s') is not modelled: the spread origin of the seed formation - caster keep or cast point - is not determinable from the converted pack, and the barrage footprint is the whole power. Two further halves have no runtime here either: the LuaEventNugget fear pulse (LuaEvent BeUncontrollablyAfraid, radius 200) needs retail's Lua event bus, and the MetaImpactNugget shockwave (amount 75.0, taper 1.0, radius 50) needs physics knockback" % secondary_object_filter}
	return {}


## --- Weather-based global spells (Darkness, Freezing Rain) -------------------
## Retail models these as a WEATHER change plus a global, range-less effect that
## holds for WeatherDuration. Neither power authors an AttributeModifierRange or
## a NEED_TARGET_POS cast option: the scope is the whole map. The sim therefore
## keeps a live window rather than doing a one-shot sweep, and re-applies it on
## the shared aura cadence so units that appear during the window are covered
## exactly as they are in retail (AttributeModifierWeatherBased = Yes).
## EMPTY-IS-ABSENT in the serialized state (see _serialize_state).


func _spellbook_weather_modifier_support(field_values: Dictionary, field_resolved: Dictionary, modifier_leaves: Dictionary) -> Dictionary:
	## RECORDED, redundant-but-unconsumed: SpellBookDarkness also authors a
	## SpecialPower-LEVEL filter (specialpower.ini:1446-1455 `ObjectFilter = ANY
	## -STRUCTURE -DwarvenZerker -NoldorWarrior -GondorKnightsofDol
	## -WildBabyDrake -IsengardFanatic -MordorBlackRider`). The converter does not
	## carry it onto the power row, and it changes nothing: the MODULE filter read
	## below (AttributeModifierAffects) is strictly narrower - it is ALLIES-only,
	## admits only +INFANTRY +CAVALRY +MONSTER (so -STRUCTURE is already implied),
	## and repeats every one of those unit exclusions. Noted here so the absence
	## reads as verified-inert rather than overlooked.
	if String(field_values.get("AttributeModifierWeatherBased", "")) != "Yes":
		return {"ok": false, "reason": "weather power does not author AttributeModifierWeatherBased = Yes"}
	var weather_ms := float(field_resolved.get("WeatherDuration", 0.0))
	if weather_ms <= 0.0:
		return {"ok": false, "reason": "WeatherDuration did not resolve in the document"}
	var modifier_id := String(field_values.get("AttributeModifier", ""))
	var modifier: Dictionary = modifier_leaves.get(modifier_id, {}) as Dictionary
	if modifier.is_empty():
		return {"ok": false, "reason": "weather attribute modifier '%s' is not a converted leaf" % modifier_id}
	var modifiers: Array = []
	var category := ""
	var leaf_duration_ms := 0.0
	for field_value in Array(modifier.get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field := field_value as Dictionary
		var key := String(field.get("key", ""))
		var value := String(field.get("value", ""))
		if key == "Category":
			category = value
		elif key == "Duration" and value.is_valid_float():
			leaf_duration_ms = float(value)
		elif key == "Modifier":
			var parsed := _parse_modifier_row(value)
			if not parsed.get("ok", false):
				return {"ok": false, "reason": "weather modifier '%s' has an unreadable modifier row: %s" % [modifier_id, String(parsed.get("reason", ""))]}
			var kind := String(parsed.get("kind", ""))
			# STRICT lane: a weather modifier is a whole-army buff and every authored
			# row of it has to land, so a read-but-not-modelled row still locks the
			# power — under its own name, not as a shape error.
			if not bool(parsed.get("supported", false)):
				return {"ok": false, "reason": "weather modifier '%s' has a row with no runtime: %s" % [modifier_id, String(parsed.get("reason", ""))]}
			if String(parsed.get("shape", "")) != "percent" or kind not in ["ARMOR", "DAMAGE_MULT", "EXPERIENCE"]:
				return {"ok": false, "reason": "weather modifier '%s' requires unsupported '%s' runtime" % [modifier_id, kind]}
			modifiers.append({"kind": kind, "value": float(parsed.get("value", 0.0))})
	if modifiers.is_empty():
		return {"ok": false, "reason": "weather modifier '%s' carries no converted stat rows" % modifier_id}
	# The authored leaf is INFINITE (Duration = 0): the weather window is what
	# bounds it. A positive leaf duration would be a different, shorter contract
	# than the one implemented here, so it fails closed rather than being
	# silently widened to the weather window.
	if leaf_duration_ms > 0.0:
		return {"ok": false, "reason": "weather modifier '%s' authors its own Duration %.0f ms; only the infinite (Duration = 0) weather-bounded form is modelled" % [modifier_id, leaf_duration_ms]}
	return {"ok": true, "effect": {
		"kind": "weather_modifier",
		"modifier_id": modifier_id,
		"category": category,
		"modifiers": modifiers,
		"duration_ticks": maxi(1, roundi(weather_ms / 1000.0 / sim.TICK_SECONDS)),
		"weather": String(field_values.get("ChangeWeather", "")),
		"affects": String(field_values.get("AttributeModifierAffects", "")),
	}}


func _spellbook_weather_anticategory_support(field_values: Dictionary, field_resolved: Dictionary) -> Dictionary:
	if String(field_values.get("AttributeModifierWeatherBased", "")) != "Yes":
		return {"ok": false, "reason": "weather power does not author AttributeModifierWeatherBased = Yes"}
	var weather_ms := float(field_resolved.get("WeatherDuration", 0.0))
	if weather_ms <= 0.0:
		return {"ok": false, "reason": "WeatherDuration did not resolve in the document"}
	var anti_category := String(field_values.get("AntiCategory", ""))
	if anti_category != "LEADERSHIP":
		# LEADERSHIP is the one modifier category the sim can suppress
		# (leadership_suppressed_until_tick, shared with Horn of Gondor).
		return {"ok": false, "reason": "AntiCategory '%s' has no suppression runtime in the sim" % anti_category}
	var affects := String(field_values.get("AttributeModifierAffects", ""))
	if affects == "":
		return {"ok": false, "reason": "AttributeModifierAffects is absent from the document"}
	# The burn-rate half of Freezing Rain (BurnRateModifier / BurnDecayModifier)
	# acts on retail's FireLogicSystem, which the sim does not model. It is
	# carried as evidence on the effect and named, never silently dropped.
	var unconverted: Array = []
	if field_resolved.has("BurnRateModifier") or field_resolved.has("BurnDecayModifier"):
		unconverted.append("FireLogicSystem burn rate/decay")
	return {"ok": true, "effect": {
		"kind": "weather_anticategory",
		"anti_category": anti_category,
		"duration_ticks": maxi(1, roundi(weather_ms / 1000.0 / sim.TICK_SECONDS)),
		"weather": String(field_values.get("ChangeWeather", "")),
		"affects": affects,
		"burn_rate_modifier": float(field_resolved.get("BurnRateModifier", 0.0)),
		"burn_decay_modifier": float(field_resolved.get("BurnDecayModifier", 0.0)),
		"unconverted_behaviors": unconverted,
	}}


func _spellbook_untamed_allegiance_support(field_values: Dictionary, field_resolved: Dictionary) -> Dictionary:
	## Lair conversion. The authored payload is exactly three things: TargetEnemy,
	## an object filter naming every creep lair and slaved creep, and a radius.
	## There is no AttributeModifier and no duration - the allegiance is
	## permanent, which is why the module carries neither.
	var range_source := float(field_resolved.get("AttributeModifierRange", 0.0))
	if range_source <= 0.0:
		return {"ok": false, "reason": "AttributeModifierRange did not resolve in the document"}
	var filter := String(field_values.get("AttributeModifierAffects", ""))
	if filter == "" or filter.ends_with("_OBJECTFILTER") or filter.ends_with("_OBJECT_FILTER"):
		return {"ok": false, "reason": "creep object filter '%s' did not resolve to its member list in the document" % filter}
	var lair_types: Array = []
	for term_value in filter.split(" ", false):
		var term := String(term_value)
		if term.begins_with("+") and term.contains("Lair"):
			lair_types.append(term.trim_prefix("+"))
	if lair_types.is_empty():
		return {"ok": false, "reason": "resolved creep filter names no lair objects"}
	lair_types.sort()
	return {"ok": true, "effect": {
		"kind": "creep_allegiance",
		"range_source": range_source,
		"filter": filter,
		"lair_types": lair_types,
		"target_enemy": String(field_values.get("TargetEnemy", "")) == "Yes",
	}}


func _spellbook_fire_weapon_support(spawns: Array, weapon_leaves: Dictionary) -> Dictionary:
	var strikes: Array = []
	var seen_weapons: Array = []
	for spawn_value in spawns:
		var spawn := spawn_value as Dictionary
		for fw_value in Array((spawn["leaf"] as Dictionary).get("fireWeapons", [])):
			var fw := fw_value as Dictionary
			var weapon_id := String(fw.get("weapon", ""))
			var weapon: Dictionary = weapon_leaves.get(weapon_id, {}) as Dictionary
			if weapon.is_empty():
				return {"ok": false, "reason": "fire-weapon '%s' is not a converted leaf" % weapon_id}
			if not seen_weapons.has(weapon_id):
				seen_weapons.append(weapon_id)
			var nuggets := _spellbook_weapon_damage_nuggets(weapon, weapon_leaves)
			if nuggets.is_empty():
				# Warning-shot phase: an authored fire entry with no damage.
				continue
			var delay_ms := float(fw.get("fireDelayMs", 0.0))
			for nugget_value in nuggets:
				var nugget := nugget_value as Dictionary
				strikes.append({
					"delay_ms": delay_ms + float(nugget.get("delaytime", 0.0)),
					"damage": float(nugget.get("damage", 0.0)),
					"radius_source": float(nugget.get("radius", 0.0)),
					"damage_type": String(nugget.get("damagetype", "")).to_lower(),
					"affects": String(weapon.get("radiusDamageAffects", "ENEMIES")),
				})
	if strikes.is_empty():
		# Undermine is this case: DwarvenUndermineSpawnWeapon has no DamageNugget
		# at all, only a MetaImpactNugget whose payload is an instant-death filter
		# plus a shock wave, and whose ShockWaveRadius is still the unresolved
		# define SPELL_UNDERMINE_SPAWN_DAMAGE_RADIUS in the pack. Naming the nugget
		# kinds points the fix at the compiler rather than at "no damage".
		var nugget_kinds: Array = []
		for weapon_id_value in seen_weapons:
			var seen_weapon: Dictionary = weapon_leaves.get(String(weapon_id_value), {}) as Dictionary
			for nugget_value in Array(seen_weapon.get("nuggets", [])):
				var nugget_kind := String((nugget_value as Dictionary).get("kind", ""))
				if nugget_kind != "" and not nugget_kinds.has(nugget_kind):
					nugget_kinds.append(nugget_kind)
		if nugget_kinds.is_empty():
			return {"ok": false, "reason": "fire-weapon chain (%s) carries no resolved damage nuggets" % ", ".join(seen_weapons)}
		return {"ok": false, "reason": "fire-weapon chain (%s) authors no DamageNugget; its only payload is %s, which has no sim runtime" % [", ".join(seen_weapons), ", ".join(nugget_kinds)]}
	return {"ok": true, "effect": {"kind": "fire_weapon", "strikes": strikes}}


func _spellbook_weapon_damage_nuggets(weapon: Dictionary, weapon_leaves: Dictionary) -> Array:
	## Direct damage nuggets, else the first projectile nugget's warhead chain.
	var direct: Array = weapon.get("damageNuggets", []) as Array
	if not direct.is_empty():
		return direct
	for nugget_value in Array(weapon.get("nuggets", [])):
		var nugget := nugget_value as Dictionary
		if String(nugget.get("kind", "")).to_lower() != "projectilenugget":
			continue
		var warhead: Dictionary = weapon_leaves.get(String(nugget.get("warheadId", "")), {}) as Dictionary
		if not warhead.is_empty():
			return warhead.get("damageNuggets", []) as Array
	return []


func _spellbook_weapon_field(weapon: Dictionary, key: String) -> float:
	for field_value in Array(weapon.get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field_row := field_value as Dictionary
		if String(field_row.get("key", "")) != key:
			continue
		if field_row.has("resolvedMax"):
			return float(field_row.get("resolvedMax", 0.0))
		if field_row.has("resolved"):
			return float(field_row.get("resolved", 0.0))
		return 0.0
	return 0.0


func _spellbook_structure_summon_support(spawn: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	var leaf: Dictionary = spawn["leaf"]
	var health := int(leaf.get("maxHealth", 0))
	var weapon_id := String(leaf.get("weaponId", ""))
	var weapon: Dictionary = weapon_leaves.get(weapon_id, {}) as Dictionary
	var nuggets := _spellbook_weapon_damage_nuggets(weapon, weapon_leaves)
	var attack_range := _spellbook_weapon_field(weapon, "AttackRange")
	if health <= 0:
		return {"ok": false, "reason": "summoned structure health is not converted"}
	if weapon_id == "":
		# The Barricade is this case. It is SPAWNS_ARE_THE_WEAPONS: the structure
		# itself never shoots, its garrison does, and that garrison is the
		# unconverted SpawnBehavior (barricade.ini:175-184 - SpawnNumber 4,
		# InitialBurst 4, SpawnTemplateName MordorArcherBarricade_Slaved,
		# SpawnedRequireSpawner Yes). Summoning a silent 3000 HP wall would ship
		# half a power as if it were whole.
		var kind_of: Array = leaf.get("kindOf", []) as Array
		var unconverted: Array = leaf.get("unconvertedBehaviors", []) as Array
		if kind_of.has("SPAWNS_ARE_THE_WEAPONS") and unconverted.has("SpawnBehavior"):
			return {"ok": false, "reason": "summoned structure '%s' is SPAWNS_ARE_THE_WEAPONS: its entire combat payload is the unconverted SpawnBehavior garrison and the leaf authors no weapon of its own" % String(leaf.get("id", ""))}
		return {"ok": false, "reason": "summoned structure '%s' authors no weapon" % String(leaf.get("id", ""))}
	if weapon.is_empty() or nuggets.is_empty() or attack_range <= 0.0:
		return {"ok": false, "reason": "summoned structure weapon '%s' is not fully converted" % weapon_id}
	var build_ms := 0.0
	for field_value in Array((spawn["create"] as Dictionary).get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field_row := field_value as Dictionary
		var field_key := String(field_row.get("key", ""))
		if field_key == "JustBuiltDuration" or field_key == "StartingBusyTime":
			build_ms = maxf(build_ms, float(field_row.get("resolved", 0.0)))
	return {"ok": true, "effect": {
		"kind": "structure_summon",
		"object_id": String(leaf.get("id", "")),
		"health": health,
		"build_ticks": maxi(1, roundi(build_ms / 1000.0 / sim.TICK_SECONDS)),
		"weapon": {
			"damage": float((nuggets[0] as Dictionary).get("damage", 0.0)),
			"range_source": attack_range,
			"damage_type": String((nuggets[0] as Dictionary).get("damagetype", "")).to_lower(),
			"period_ms": _spellbook_weapon_field(weapon, "DelayBetweenShots"),
			"pre_attack_ms": _spellbook_weapon_field(weapon, "PreAttackDelay"),
			"firing_ms": _spellbook_weapon_field(weapon, "FiringDuration"),
			"affects": String(weapon.get("radiusDamageAffects", "ENEMIES")),
		},
	}}


func _spellbook_direct_summon_support(spawns: Array, modifier_leaves: Dictionary, object_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	## Direct OCL summon: validate every authored object and apply the same
	## Count/ObjectNames choice-pool semantics used by egg hatch OCLs. Choice
	## groups remain declarative here; the shared logic RNG is touched per cast.
	var groups_by_index: Dictionary = {}
	for spawn_value in spawns:
		var spawn := spawn_value as Dictionary
		var create: Dictionary = spawn.get("create", {}) as Dictionary
		var leaf: Dictionary = spawn.get("leaf", {}) as Dictionary
		var create_index := int(spawn.get("create_index", -1))
		var object_id := String(leaf.get("id", ""))
		if not Array(create.get("objects", [])).has(object_id):
			return {"ok": false, "reason": "direct summon object '%s' is absent from its CreateObject choice list" % String(leaf.get("id", ""))}
		var verdict := _spellbook_summon_rule(leaf, modifier_leaves, object_leaves, weapon_leaves)
		if not bool(verdict.get("ok", false)):
			return {"ok": false, "reason": String(verdict.get("reason", "direct summon stats unresolved"))}
		if not groups_by_index.has(create_index):
			groups_by_index[create_index] = {
				"block_index": create_index,
				"pick_count": _spellbook_create_pick_count(create),
				"choices": [],
			}
		var group: Dictionary = groups_by_index[create_index]
		(group["choices"] as Array).append({
				"object_id": object_id,
				"rule": verdict["rule"],
				"lifetime_ticks": int(verdict.get("lifetime_ticks", 0)),
				"lifetime_death_type": String(verdict.get("lifetime_death_type", "")),
			})
	var target_groups: Array = []
	var group_indices: Array = groups_by_index.keys()
	group_indices.sort()
	for group_index in group_indices:
		target_groups.append(groups_by_index[group_index])
	if target_groups.is_empty():
		return {"ok": false, "reason": "direct summon OCL resolves to no live targets"}
	return {"ok": true, "effect": {
		"kind": "summon",
		"hatch_delay_ticks": 0,
		"target_groups": target_groups,
	}}


func _spellbook_create_pick_count(create: Dictionary, multiplier: int = 1) -> int:
	var count := 1
	for field_value in Array(create.get("fields", [])):
		if (
			typeof(field_value) == TYPE_DICTIONARY
			and String((field_value as Dictionary).get("key", "")) == "Count"
		):
			count = maxi(1, int((field_value as Dictionary).get("resolved", 1)))
	return count * maxi(1, multiplier)


func _spellbook_create_enabled(create: Dictionary, owned_upgrades: Dictionary = {}) -> bool:
	## CreateObject rows can be mutually exclusive presentation variants. The
	## default spellbook cast owns no CE graphics upgrades, so a RequiredUpgrades
	## row is not an additional summon while its ForbiddenUpgrades sibling is.
	for field_value in Array(create.get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field := field_value as Dictionary
		var key := String(field.get("key", ""))
		var upgrades := String(field.get("value", "")).split(" ", false)
		if key == "RequiredUpgrades":
			for upgrade in upgrades:
				if not owned_upgrades.has(String(upgrade)):
					return false
		elif key == "ForbiddenUpgrades":
			for upgrade in upgrades:
				if owned_upgrades.has(String(upgrade)):
					return false
	return true


func _spellbook_summon_support(spawns: Array, modifier_leaves: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	## Egg powers: the power OCL creates eggs; each egg hatches an OCL of
	## summoned battalions. The full chain must convert: egg hatch, hatch OCL,
	## target horde/member stats, member weapon, locomotion, summon lifetime.
	var hatch_spawn: Dictionary = {}
	for spawn_value in spawns:
		var candidate := spawn_value as Dictionary
		if typeof((candidate.get("leaf", {}) as Dictionary).get("hatch", null)) == TYPE_DICTIONARY:
			hatch_spawn = candidate
			break
	if hatch_spawn.is_empty():
		return {"ok": false, "reason": "summon OCL has no converted hatch leaf"}
	var hatch: Dictionary = (hatch_spawn.get("leaf", {}) as Dictionary).get("hatch", {}) as Dictionary
	var hatch_ocl_id := String(hatch.get("ocl", ""))
	var hatch_ocl: Dictionary = ocl_leaves.get(hatch_ocl_id, {}) as Dictionary
	if hatch_ocl.is_empty():
		return {"ok": false, "reason": "hatch OCL '%s' is not a converted leaf" % hatch_ocl_id}
	var egg_count := 1
	for field_value in Array((hatch_spawn.get("create", {}) as Dictionary).get("fields", [])):
		if typeof(field_value) == TYPE_DICTIONARY and String((field_value as Dictionary).get("key", "")) == "Count":
			egg_count = maxi(1, int((field_value as Dictionary).get("resolved", 1)))
	var hatch_delay_ms := float(hatch.get("destructionDelayMs", 0.0))
	var target_groups: Array = []
	var hatch_creates: Array = hatch_ocl.get("createObjects", []) as Array
	for create_index in range(hatch_creates.size()):
		var create_value: Variant = hatch_creates[create_index]
		if typeof(create_value) != TYPE_DICTIONARY:
			continue
		var create := create_value as Dictionary
		if not _spellbook_create_enabled(create):
			continue
		if not _spellbook_create_enabled(create):
			continue
		var choices: Array = []
		for object_name_value in Array(create.get("objects", [])):
			var object_name := String(object_name_value)
			var target_leaf: Dictionary = object_leaves.get(object_name, {}) as Dictionary
			if target_leaf.is_empty():
				return {"ok": false, "reason": "summon target '%s' is not a converted leaf" % object_name}
			var verdict := _spellbook_summon_rule(target_leaf, modifier_leaves, object_leaves, weapon_leaves)
			if not bool(verdict.get("ok", false)):
				return {"ok": false, "reason": String(verdict.get("reason", "summon stats unresolved"))}
			choices.append({
				"object_id": object_name,
				"rule": verdict["rule"],
				"lifetime_ticks": int(verdict.get("lifetime_ticks", 0)),
				"lifetime_death_type": String(verdict.get("lifetime_death_type", "")),
			})
		if not choices.is_empty():
			target_groups.append({
				"block_index": create_index,
				"pick_count": _spellbook_create_pick_count(create, egg_count),
				"choices": choices,
			})
	if target_groups.is_empty():
		return {"ok": false, "reason": "hatch OCL '%s' spawns no converted targets" % hatch_ocl_id}
	return {"ok": true, "effect": {
		"kind": "summon",
		"hatch_delay_ticks": maxi(0, roundi(hatch_delay_ms / 1000.0 / sim.TICK_SECONDS)),
		"target_groups": target_groups,
	}}


func _spellbook_summon_literal_preview(spawns: Array, object_leaves: Dictionary, ocl_leaves: Dictionary) -> Dictionary:
	## Preserve the exact hatch payload and LifetimeUpdate literals when a live
	## summon remains unsupported for an orthogonal reason (for example, the
	## Watcher's unresolved weapon runtime). This is inspection data only: the
	## original failed verdict remains authoritative and cannot be cast.
	var hatch_spawn: Dictionary = {}
	for spawn_value in spawns:
		var candidate := spawn_value as Dictionary
		if typeof((candidate.get("leaf", {}) as Dictionary).get("hatch", null)) == TYPE_DICTIONARY:
			hatch_spawn = candidate
			break
	if hatch_spawn.is_empty():
		return {"ok": false}
	var hatch: Dictionary = (hatch_spawn.get("leaf", {}) as Dictionary).get("hatch", {}) as Dictionary
	var hatch_ocl: Dictionary = ocl_leaves.get(String(hatch.get("ocl", "")), {}) as Dictionary
	if hatch_ocl.is_empty():
		return {"ok": false}
	var egg_count := _spellbook_create_pick_count(hatch_spawn.get("create", {}) as Dictionary)
	var target_groups: Array = []
	var hatch_creates: Array = hatch_ocl.get("createObjects", []) as Array
	for create_index in range(hatch_creates.size()):
		var create: Dictionary = hatch_creates[create_index] as Dictionary
		if not _spellbook_create_enabled(create):
			continue
		var choices: Array = []
		for object_name_value in Array(create.get("objects", [])):
			var object_name := String(object_name_value)
			var target_leaf: Dictionary = object_leaves.get(object_name, {}) as Dictionary
			if target_leaf.is_empty():
				return {"ok": false}
			var lifetime: Dictionary = target_leaf.get("lifetime", {}) as Dictionary
			if lifetime.is_empty():
				var horde: Dictionary = target_leaf.get("horde", {}) as Dictionary
				var member: Dictionary = object_leaves.get(String(horde.get("memberObject", "")), {}) as Dictionary
				lifetime = member.get("lifetime", {}) as Dictionary
			var lifetime_ms := float(lifetime.get("maxMs", 0.0))
			choices.append({
				"object_id": object_name,
				"rule": {},
				"lifetime_ticks": maxi(0, roundi(lifetime_ms / 1000.0 / sim.TICK_SECONDS)),
				"lifetime_death_type": String(lifetime.get("deathType", "")).to_upper(),
			})
		if not choices.is_empty():
			target_groups.append({
				"block_index": create_index,
				"pick_count": _spellbook_create_pick_count(create, egg_count),
				"choices": choices,
			})
	if target_groups.is_empty():
		return {"ok": false}
	return {"ok": true, "effect": {
		"kind": "summon",
		"hatch_delay_ticks": maxi(0, roundi(float(hatch.get("destructionDelayMs", 0.0)) / 1000.0 / sim.TICK_SECONDS)),
		"target_groups": target_groups,
	}}


func _spellbook_summon_rule(target_leaf: Dictionary, modifier_leaves: Dictionary, object_leaves: Dictionary, weapon_leaves: Dictionary) -> Dictionary:
	## Project one summon target into the sim's unit-rule shape; every value
	## traces to the converted object/weapon/locomotor leaves.
	var horde: Dictionary = target_leaf.get("horde", {}) as Dictionary
	var member_id := String(target_leaf.get("id", "")) if horde.is_empty() else String(horde.get("memberObject", ""))
	var member_count := 1 if horde.is_empty() else int(horde.get("memberCount", 1))
	var member: Dictionary = object_leaves.get(member_id, {}) as Dictionary
	if member.is_empty():
		return {"ok": false, "reason": "summoned member '%s' is not a converted leaf" % member_id}
	if not member.has("maxHealth") and member.has("buildVariations"):
		for variation_value in Array(member.get("buildVariations", [])):
			var candidate: Dictionary = object_leaves.get(String(variation_value), {}) as Dictionary
			if candidate.has("maxHealth"):
				member = candidate
				break
	var member_health := int(member.get("maxHealth", 0))
	if member_health <= 0:
		return {"ok": false, "reason": "summoned member '%s' health is not converted" % member_id}
	var locomotor: Dictionary = member.get("locomotor", {}) as Dictionary
	var speed := float(locomotor.get("speed", 0.0))
	# Wyrm is intentionally stationary (WyrmLocomotor Speed = 0) but still has
	# a fully authored locomotor and ranged fire-breath runtime.
	if locomotor.is_empty() or speed < 0.0:
		return {"ok": false, "reason": "summoned member '%s' locomotion is not converted" % member_id}
	for authored_field in ["acceleration", "braking", "turnRateDegreesPerSecond"]:
		if not locomotor.has(authored_field):
			push_error(
				"unauthored locomotor field %s for spellbook member %s"
				% [authored_field, member_id]
			)
			return {
				"ok": false,
				"reason": "summoned member '%s' locomotor field %s is unauthored" % [member_id, authored_field],
			}
	var weapon_id := String(member.get("weaponId", ""))
	var weapon: Dictionary = weapon_leaves.get(weapon_id, {}) as Dictionary
	var kind_of: Array = member.get("kindOf", []) as Array
	var move_only := kind_of.has("MOVE_ONLY")
	if weapon_id == "" and not move_only:
		# Distinct from "the weapon leaf did not convert": the object authors NO
		# WeaponSet at all. The Watcher is this case - its attack runtime is
		# GrabPassengerSpecialPower + SpecialAbilityUpdate + TransportContain
		# (grab-and-devour), none of which the compiler emits, so reporting a
		# missing weapon leaf would aim a fix at the wrong module.
		var attack_behaviors: Array = []
		for behavior_value in Array(member.get("unconvertedBehaviors", [])):
			var behavior := String(behavior_value)
			if behavior in ["GrabPassengerSpecialPower", "SpecialAbilityUpdate", "TransportContain", "AutoPickUpUpdate"]:
				attack_behaviors.append(behavior)
		if attack_behaviors.is_empty():
			return {"ok": false, "reason": "summoned member '%s' authors no weapon and no unconverted attack runtime" % member_id}
		return {"ok": false, "reason": "summoned member '%s' authors no weapon: its attack runtime is %s, which is not converted" % [member_id, ", ".join(attack_behaviors)]}
	if weapon.is_empty() and not move_only:
		return {"ok": false, "reason": "summoned member '%s' weapon '%s' is not a converted leaf" % [member_id, weapon_id]}
	var nuggets := _spellbook_weapon_damage_nuggets(weapon, weapon_leaves) if not weapon.is_empty() else []
	if nuggets.is_empty() and not move_only:
		return {"ok": false, "reason": "summoned member weapon '%s' has no resolved damage" % weapon_id}
	var attack_range := _spellbook_weapon_field(weapon, "AttackRange") if not weapon.is_empty() else 0.0
	if attack_range <= 0.0 and not move_only:
		return {"ok": false, "reason": "summoned member weapon '%s' range is not converted" % weapon_id}
	var lifetime_ms := 0.0
	var lifetime_death_type := ""
	var lifetime_row: Dictionary = target_leaf.get("lifetime", {}) as Dictionary
	if not lifetime_row.is_empty():
		lifetime_ms = float(lifetime_row.get("maxMs", 0.0))
		lifetime_death_type = String(lifetime_row.get("deathType", ""))
	if lifetime_ms <= 0.0:
		var member_lifetime: Dictionary = member.get("lifetime", {}) as Dictionary
		lifetime_ms = float(member_lifetime.get("maxMs", 0.0))
		lifetime_death_type = String(member_lifetime.get("deathType", ""))
	if lifetime_ms <= 0.0:
		return {"ok": false, "reason": "summon target '%s' is missing converted LifetimeUpdate" % String(target_leaf.get("id", ""))}
	var scale := _spellbook_world_scale()
	var source_positions: Array[Vector2] = []
	for rank_value in Array(horde.get("ranks", [])):
		for position_value in Array((rank_value as Dictionary).get("positions", [])):
			var pair: Array = position_value as Array
			if pair.size() >= 2:
				source_positions.append(Vector2(float(pair[0]), float(pair[1])))
	if source_positions.size() != member_count:
		source_positions.clear()
		for index in range(member_count):
			source_positions.append(Vector2(10.0 + float(index % 4) * 15.0, float(index / 4) * 15.0))
	var center := Vector2.ZERO
	for position in source_positions:
		center += position
	center /= float(maxi(1, source_positions.size()))
	var positions: Array[Vector3] = []
	for position in source_positions:
		positions.append(Vector3((position.y - center.y) * scale, 0.0, (position.x - center.x) * scale))
	var damage_nugget: Dictionary = nuggets[0] if not nuggets.is_empty() else {}
	var delay_ms := _spellbook_weapon_field(weapon, "DelayBetweenShots")
	var clip_reload_ms := _spellbook_weapon_field(weapon, "ClipReloadTime")
	var period_ms := delay_ms if delay_ms > 0.0 else clip_reload_ms
	var pre_attack_ms := _spellbook_weapon_field(weapon, "PreAttackDelay")
	var firing_ms := _spellbook_weapon_field(weapon, "FiringDuration")
	var category := "infantry"
	if kind_of.has("HERO"):
		category = "hero"
	elif kind_of.has("CAVALRY"):
		category = "cavalry"
	var vision := float(member.get("visionRange", 0.0))
	if vision <= 0.0:
		vision = attack_range
	var rule := {
		"horde_id": String(target_leaf.get("id", "")),
		"member_count": member_count,
		"member_health": member_health,
		"member_damage": 0 if move_only else maxi(1, int(damage_nugget.get("damage", 0))),
		"category": category,
		"speed": speed * scale,
		"speed_source": speed,
		# Authored only. All 126 shipped spellbook locomotor rows carry the three
		# fields; the guard above refuses the summon outright if one is missing.
		"acceleration": float(locomotor["acceleration"]) * scale,
		"acceleration_source": float(locomotor["acceleration"]),
		"turn_rate_degrees_per_second": float(locomotor["turnRateDegreesPerSecond"]),
		"braking": float(locomotor["braking"]) * scale,
		"braking_source": float(locomotor["braking"]),
		"attack_range": attack_range * scale,
		"attack_range_source": attack_range,
		"minimum_attack_range": _spellbook_weapon_field(weapon, "MinimumAttackRange") * scale,
		"minimum_attack_range_source": _spellbook_weapon_field(weapon, "MinimumAttackRange"),
		"vision_range": vision * scale,
		"vision_range_source": vision,
		"delay_between_shots_ms": delay_ms,
		"pre_attack_delay_ms": pre_attack_ms,
		"firing_duration_ms": firing_ms,
		"attack_period_ticks": maxi(1, roundi(period_ms / (sim.TICK_SECONDS * 1000.0))),
		"pre_attack_ticks": maxi(0, roundi(pre_attack_ms / (sim.TICK_SECONDS * 1000.0))),
		"firing_duration_ticks": maxi(0, roundi(firing_ms / (sim.TICK_SECONDS * 1000.0))),
		"clip_size": int(_spellbook_weapon_field(weapon, "ClipSize")),
		"clip_reload_time_ms": clip_reload_ms,
		"formation_positions": positions,
		"default_weapon_mode": "default",
		"default_weapon_slot": String(member.get("weaponSlot", "")).to_lower(),
		"damage_type": String(damage_nugget.get("damagetype", "")).to_lower(),
		"provenance": {"source": "spellbook-summon", "object_id": String(target_leaf.get("id", ""))},
	}
	var aura_verdict := _spellbook_summon_aura_rules(member, modifier_leaves)
	if not bool(aura_verdict.get("ok", false)):
		return {"ok": false, "reason": String(aura_verdict.get("reason", "summon aura is not converted"))}
	var summon_auras: Array = aura_verdict.get("auras", []) as Array
	var summon_aura_skips: Array = aura_verdict.get("skipped_auras", []) as Array
	if move_only and summon_auras.is_empty() and summon_aura_skips.is_empty():
		return {"ok": false, "reason": "move-only summoned member '%s' has no converted aura payload" % member_id}
	if not summon_auras.is_empty():
		rule["summon_auras"] = summon_auras
	if not summon_aura_skips.is_empty():
		rule["summon_aura_skips"] = summon_aura_skips
	# Carry authored lifecycle policy into the synthesized unit rule. FADED is a
	# lifetime-only removal: combat death still follows the ordinary corpse rule.
	var destroy_die: Array = []
	for policy_value in Array(member.get("destroyDie", [])):
		var policy := policy_value as Dictionary
		destroy_die.append({
			"owner_role": "object",
			"death_types": String(policy.get("deathTypes", "ALL")).to_upper(),
			"excluded_death_types": Array(policy.get("excludedDeathTypes", [])).duplicate(),
			"included_death_types": Array(policy.get("includedDeathTypes", [])).duplicate(),
		})
	if lifetime_death_type.to_upper() == "FADED":
		destroy_die.append({
			"owner_role": "object",
			"death_types": "NONE",
			"excluded_death_types": [],
			"included_death_types": ["FADED"],
		})
	if not destroy_die.is_empty():
		rule["destroy_die"] = destroy_die
	var keep_rows: Array = member.get("keepObjectDie", []) as Array
	if not keep_rows.is_empty():
		var keep := keep_rows[0] as Dictionary
		rule["keep_object_die"] = true
		rule["keep_object_die_policy"] = {
			"death_types": String(keep.get("deathTypes", "ALL")).to_upper(),
			"excluded_death_types": Array(keep.get("excludedDeathTypes", [])).duplicate(),
			"included_death_types": Array(keep.get("includedDeathTypes", [])).duplicate(),
		}
	var permanent_locks: Array[String] = []
	for lock_value in Array(member.get("permanentWeaponLocks", [])):
		if typeof(lock_value) != TYPE_DICTIONARY:
			return {"ok": false, "reason": "summoned member '%s' has a malformed permanent weapon lock" % member_id}
		var lock_row := lock_value as Dictionary
		var slot := String(lock_row.get("slot", "")).to_lower()
		var lock_line: Variant = lock_row.get("line")
		if (
			slot != "primary"
			or String(lock_row.get("state", "")) != "LOCKED_PERMANENTLY"
			or String(lock_row.get("module", "")) != "LockWeaponCreate"
			or String(lock_row.get("sourceIni", "")).strip_edges() == ""
			or typeof(lock_line) not in [TYPE_INT, TYPE_FLOAT]
			or not is_equal_approx(float(lock_line), float(int(lock_line)))
			or int(lock_line) <= 0
			or permanent_locks.has(slot)
			or slot != String(rule.get("default_weapon_slot", ""))
		):
			return {"ok": false, "reason": "summoned member '%s' permanent weapon lock is unsupported" % member_id}
		permanent_locks.append(slot)
	if not permanent_locks.is_empty():
		rule["permanent_weapon_locks"] = permanent_locks
	var creation_leaf: Dictionary = (
		target_leaf
		if target_leaf.has("experienceLevelCreate")
		else member
	)
	var creation_grant: Variant = creation_leaf.get("experienceLevelCreate")
	if creation_grant != null:
		if typeof(creation_grant) != TYPE_DICTIONARY:
			return {"ok": false, "reason": "summoned member '%s' has malformed ExperienceLevelCreate evidence" % member_id}
		var grant := creation_grant as Dictionary
		var grant_rank: Variant = grant.get("rank")
		var grant_line: Variant = grant.get("line")
		if (
			String(grant.get("module", "")) != "ExperienceLevelCreate"
			or grant.get("mpOnly") != false
			or typeof(grant_rank) not in [TYPE_INT, TYPE_FLOAT]
			or not is_equal_approx(float(grant_rank), float(int(grant_rank)))
			or int(grant_rank) < 1
			or String(grant.get("sourceIni", "")).strip_edges() == ""
			or typeof(grant_line) not in [TYPE_INT, TYPE_FLOAT]
			or not is_equal_approx(float(grant_line), float(int(grant_line)))
			or int(grant_line) <= 0
		):
			return {"ok": false, "reason": "summoned member '%s' ExperienceLevelCreate evidence is unsupported" % member_id}
		# JSON numbers enter Godot as floats; the shared adapter deliberately
		# expects source line metadata as an integer. Normalize only the proven
		# integral creation line at this boundary.
		var experience_contract: Dictionary = (creation_leaf.get("experience", {}) as Dictionary).duplicate(true)
		var normalized_grant: Dictionary = (experience_contract.get("experienceLevelCreate", {}) as Dictionary).duplicate(true)
		if not normalized_grant.is_empty():
			normalized_grant["line"] = int(normalized_grant.get("line", 0))
			experience_contract["experienceLevelCreate"] = normalized_grant
		var creation_experience_rule = sim.PlayableUnitAdapter.experience_rule_from_contract(
			experience_contract
		)
		if (
			creation_experience_rule.is_empty()
			or int(creation_experience_rule.get("initial_rank", 0)) != int(grant_rank)
		):
			return {"ok": false, "reason": "summoned member '%s' creation experience chain is unsupported" % member_id}
		var creation_effects: Dictionary = {}
		for level_value in Array(creation_experience_rule.get("levels", [])):
			var level_row := level_value as Dictionary
			if int(level_row.get("rank", 0)) == int(grant_rank):
				creation_effects = level_row
				break
		if (
			creation_effects.is_empty()
			or not Array(creation_effects.get("unsupported_modifiers", [])).is_empty()
			or float(creation_effects.get("production_multiplier", 1.0)) != 1.0
		):
			return {"ok": false, "reason": "summoned member '%s' creation experience effects are unsupported" % member_id}
		rule["creation_experience_rank"] = int(grant_rank)
		rule["creation_experience_effects"] = creation_effects.duplicate(true)
	return {
		"ok": true,
		"rule": rule,
		"lifetime_ticks": maxi(1, roundi(lifetime_ms / 1000.0 / sim.TICK_SECONDS)),
		"lifetime_death_type": lifetime_death_type.to_upper(),
	}


func _spellbook_summon_aura_rules(member: Dictionary, modifier_leaves: Dictionary) -> Dictionary:
	var source_auras: Array = member.get("auras", []) as Array
	if source_auras.is_empty() and typeof(member.get("aura", null)) == TYPE_DICTIONARY:
		source_auras.append(member.get("aura", {}))
	var compiled: Array = []
	var skipped: Array = []
	for aura_value in source_auras:
		var verdict := _spellbook_one_summon_aura_rule(
			member, aura_value as Dictionary, modifier_leaves
		)
		if not bool(verdict.get("ok", false)):
			return verdict
		if bool(verdict.get("skip", false)):
			skipped.append({
				"modifier": String((aura_value as Dictionary).get("modifier", "")),
				"reason": String(verdict.get("reason", "summon-aura-skipped")),
			})
			continue
		compiled.append(verdict.get("aura", {}))
	return {"ok": true, "auras": compiled, "skipped_auras": skipped}


func _spellbook_one_summon_aura_rule(member: Dictionary, aura: Dictionary, modifier_leaves: Dictionary, allow_marker_modifiers: bool = false) -> Dictionary:
	## allow_marker_modifiers: retail authors at least one ModifierList whose stat
	## rows are all commented out and only its Duration survives — PalantirVision
	## (attributemodifier.ini:1139-1146). On a summon that is evidence of an
	## unconverted payload and stays fail-closed; on a reveal ping it is the
	## authored truth (the aura genuinely changes no stat), so the ping lane opts
	## in and the row is kept as a zero-modifier marker.
	var modifier_id := String(aura.get("modifier", ""))
	var modifier: Dictionary = modifier_leaves.get(modifier_id, {}) as Dictionary
	if modifier.is_empty():
		return {"ok": false, "reason": "summon aura modifier '%s' is not a converted leaf" % modifier_id}
	var modifiers: Array = []
	var duration_ms := 0.0
	var category := ""
	for field_value in Array(modifier.get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field := field_value as Dictionary
		var key := String(field.get("key", ""))
		var value := String(field.get("value", ""))
		if key == "Category":
			category = value
		elif key == "Duration" and value.is_valid_float():
			duration_ms = float(value)
		elif key == "Modifier":
			var parsed := _parse_modifier_row(value)
			if not parsed.get("ok", false):
				return {"ok": false, "reason": "summon aura modifier '%s' has an unreadable modifier row: %s" % [modifier_id, String(parsed.get("reason", ""))]}
			var kind := String(parsed.get("kind", ""))
			# STRICT lane, same reasoning as the weather modifier above: a summon's
			# aura is the whole reason the summon is worth casting.
			if not bool(parsed.get("supported", false)):
				return {"ok": false, "reason": "summon aura modifier '%s' has a row with no runtime: %s" % [modifier_id, String(parsed.get("reason", ""))]}
			if String(parsed.get("shape", "")) != "percent" or kind not in ["ARMOR", "DAMAGE_MULT", "EXPERIENCE"]:
				return {"ok": false, "reason": "summon aura modifier '%s' requires unsupported '%s' runtime" % [modifier_id, kind]}
			modifiers.append({"kind": kind, "value": float(parsed.get("value", 0.0))})
		elif key == "ModelCondition" and value.strip_edges() != "":
			modifiers.append({"kind": "MODEL_CONDITION", "value": value.strip_edges()})
	var range_source := float(aura.get("range", 0.0))
	var refresh_ms := float(aura.get("refreshDelayMs", 0.0))
	var filter := String(aura.get("objectFilter", ""))
	var modifiers_missing := modifiers.is_empty() and not allow_marker_modifiers
	if category not in ["LEADERSHIP", "SPELL", "DEBUFF"] or modifiers_missing or duration_ms <= 0.0 or range_source <= 0.0 or refresh_ms <= 0.0 or filter == "":
		return {"ok": false, "reason": "summon aura '%s' is incomplete (category=%s modifiers=%d duration_ms=%.1f range=%.1f refresh_ms=%.1f filter=%s)" % [modifier_id, category, modifiers.size(), duration_ms, range_source, refresh_ms, "present" if filter != "" else "missing"]}
	var starts_active := String(aura.get("startsActive", "Yes")).to_lower() != "no"
	var enabled_on_create := starts_active
	if not enabled_on_create:
		var creation_rank := int((member.get("experienceLevelCreate", {}) as Dictionary).get("rank", 0))
		for trigger_value in Array(aura.get("triggeredBy", [])):
			var trigger := String(trigger_value)
			if trigger.begins_with("Upgrade_ObjectLevel"):
				var level_text := trigger.trim_prefix("Upgrade_ObjectLevel")
				if level_text.is_valid_int() and creation_rank >= int(level_text):
					enabled_on_create = true
					break
	if not enabled_on_create:
		return {
			"ok": true,
			"skip": true,
			"reason": "upgrade-gated-aura-inert-at-summon-creation:%s" % modifier_id,
		}
	return {"ok": true, "aura": {
		"id": modifier_id,
		"category": category,
		"modifiers": modifiers,
		"duration_ticks": maxi(1, roundi(duration_ms / (sim.TICK_SECONDS * 1000.0))),
		"refresh_ticks": maxi(1, roundi(refresh_ms / (sim.TICK_SECONDS * 1000.0))),
		"range_source": range_source,
		"filter": filter,
		"target_enemy": String(aura.get("targetEnemy", "")).to_lower() == "yes" if aura.has("targetEnemy") else null,
		"target_allies": String(aura.get("targetAllies", "")).to_lower() == "yes" if aura.has("targetAllies") else null,
		"starts_active": starts_active,
		"triggered_by": Array(aura.get("triggeredBy", [])).duplicate(),
	}}


func _spellbook_grove_chain(references: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary) -> Dictionary:
	## Resolve Elven Wood's authored geometry chain, or return {} for "none".
	##
	## Retail plants the grove through an ObjectCreationList, not through the
	## ElvenGrove object (which authors `Model = None` and is pure particle):
	## OCL_ElvenWoodSeed creates N ElvenWoodTreeSeed at an authored spread
	## radius, each seed's SlowDeath hatches OCL_ElvenWoodTree -> ElvenWoodTree,
	## which itself hatches OCL_ElvenWoodTreeSpawn -> ElvenWoodTreeOpt (the tree
	## that stays). Every number below is read off that chain; nothing is
	## assumed, and an unresolvable link simply yields no trees rather than an
	## invented grove.
	var ocl_ids: Array = references.get("objectCreationLists", []) as Array
	if ocl_ids.is_empty():
		return {}
	var ocl: Dictionary = ocl_leaves.get(String(ocl_ids[0]), {}) as Dictionary
	if ocl.is_empty():
		return {}
	var creates: Array = ocl.get("createObjects", []) as Array
	if creates.is_empty() or typeof(creates[0]) != TYPE_DICTIONARY:
		return {}
	var create := creates[0] as Dictionary
	var names: Array = create.get("objects", []) as Array
	if names.is_empty():
		return {}
	var count := 1
	var min_radius := 0.0
	var max_radius := 0.0
	for field_value in Array(create.get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field_row := field_value as Dictionary
		match String(field_row.get("key", "")):
			"Count":
				count = maxi(1, int(field_row.get("resolved", 1)))
			"MinDistanceAFormation":
				min_radius = float(field_row.get("resolved", 0.0))
			"MaxDistanceFormation":
				max_radius = float(field_row.get("resolved", 0.0))
	# Follow every authored hatch to the object that actually remains standing.
	var object_id := String(names[0])
	var guard := 0
	while guard < 4:
		guard += 1
		var leaf: Dictionary = object_leaves.get(object_id, {}) as Dictionary
		if leaf.is_empty():
			return {}
		var hatch: Dictionary = leaf.get("hatch", {}) as Dictionary
		var hatch_ocl_id := String(hatch.get("ocl", ""))
		if hatch_ocl_id == "":
			break
		var hatch_ocl: Dictionary = ocl_leaves.get(hatch_ocl_id, {}) as Dictionary
		var hatch_creates: Array = hatch_ocl.get("createObjects", []) as Array
		if hatch_creates.is_empty() or typeof(hatch_creates[0]) != TYPE_DICTIONARY:
			break
		var hatch_names: Array = (hatch_creates[0] as Dictionary).get("objects", []) as Array
		if hatch_names.is_empty():
			break
		object_id = String(hatch_names[0])
	if object_id == "" or not object_leaves.has(object_id):
		return {}
	if max_radius <= 0.0:
		max_radius = min_radius
	return {
		"object_id": object_id,
		"count": count,
		"min_radius_source": min_radius,
		"max_radius_source": max_radius,
	}


func _spellbook_grove_support(field_values: Dictionary, field_resolved: Dictionary, references: Dictionary, modifier_leaves: Dictionary, object_leaves: Dictionary, ocl_leaves: Dictionary = {}, object_field: String = "ElvenGroveObject") -> Dictionary:
	## Terrain-taint family (Elven Wood, Taint, Isengard Taint): the converted
	## grove/taint-land leaf carries the whole effect — modifier leaf, refresh,
	## range, filter, the authored terrain cell type, and the object lifetime.
	var grove_id := String(field_values.get(object_field, ""))
	var grove: Dictionary = object_leaves.get(grove_id, {}) as Dictionary
	if grove.is_empty():
		return {"ok": false, "reason": "terrain-taint object '%s' is not a converted leaf" % grove_id}
	var aura: Dictionary = grove.get("aura", {}) as Dictionary
	var deletion: Dictionary = grove.get("deletion", {}) as Dictionary
	if aura.is_empty() or deletion.is_empty():
		return {"ok": false, "reason": "terrain-taint object '%s' aura or lifetime is not converted" % grove_id}
	var modifier_id := String(aura.get("modifier", ""))
	var modifier: Dictionary = modifier_leaves.get(modifier_id, {}) as Dictionary
	if modifier.is_empty():
		return {"ok": false, "reason": "grove aura modifier '%s' is not a converted leaf" % modifier_id}
	# ROW ABSENT is not ROW UNREADABLE. A modifier list that never authors a
	# DAMAGE_MULT row means the neutral 1.0 (BFME2 `ModifierList
	# GenericArmorLeadership` is `ARMOR 50%` and nothing else,
	# attributemodifier.ini:159-166, and it is what the BFME2 ElvenGrove actually
	# binds: grove.ini:31 `BonusName = GenericArmorLeadership`). A row that IS
	# authored and cannot be read is still fail-closed, because that is the case
	# where half an authored buff goes missing in silence.
	var armor_mult := 1.0
	var damage_mult := 1.0
	var saw_armor := false
	var saw_damage := false
	var buff_duration_ms := 0.0
	# READABLE BUT UNMATCHED KIND. A row this resolver parses cleanly and whose
	# shape is a plain percent, but whose KIND is neither ARMOR nor DAMAGE_MULT
	# (retail authors e.g. `EXPERIENCE 150%` on buff leaves), used to fall
	# straight through the `match` below and vanish. That is the same silent drop
	# the SpecialPowerModule lane was fixed for in round 18 — it just took a
	# different route to it, so the fix did not reach here. Named and counted on
	# the effect instead, in the SAME shape that lane uses.
	var unsupported_rows: Array = []
	for field_value in Array(modifier.get("fields", [])):
		if typeof(field_value) != TYPE_DICTIONARY:
			continue
		var field_row := field_value as Dictionary
		if String(field_row.get("key", "")) == "Modifier":
			# FAIL-CLOSED, not `continue`: a row this resolver cannot read is a row
			# whose buff would silently go missing, and the grove would still plant.
			var parsed := _parse_modifier_row(String(field_row.get("value", "")))
			if (
				not parsed.get("ok", false)
				or not bool(parsed.get("supported", false))
				or String(parsed.get("shape", "")) != "percent"
			):
				return {"ok": false, "reason": "grove aura modifier '%s' has an unsupported modifier row '%s' (%s)" % [
					modifier_id, String(field_row.get("value", "")),
					String(parsed.get("reason", "shape is not a plain percent")),
				]}
			var percent := float(parsed.get("value", 0.0))
			match String(parsed.get("kind", "")):
				"ARMOR":
					armor_mult = percent
					saw_armor = true
				"DAMAGE_MULT":
					damage_mult = percent
					saw_damage = true
				_:
					unsupported_rows.append({
						"row": String(field_row.get("value", "")),
						"shape": String(parsed.get("shape", "")),
						"reason": "kind '%s' has no grove-aura runtime here" % String(parsed.get("kind", "")),
					})
		elif String(field_row.get("key", "")) == "Duration":
			buff_duration_ms = float(field_row.get("resolved", field_row.get("value", 0.0)))
	var aura_range := float(aura.get("range", 0.0))
	var lifetime_ms := float(deletion.get("maxMs", 0.0))
	var filter := String(aura.get("objectFilter", ""))
	# AT LEAST ONE stat row is required, not both. Round 16 required both and it
	# was wrong twice over: it locked men/SpellBookElvenWoodMP, whose authored
	# leaf (GenericArmorLeadership) legitimately carries only ARMOR 50%, and it
	# conflated "absent" with "unreadable" — the case the round-16 note was
	# actually written about (a row that IS authored and cannot be parsed) is
	# still fail-closed, in the loop above. A leaf with NO readable stat row at
	# all still fails here: that is a grove with nothing to apply.
	if not (saw_armor or saw_damage) or armor_mult <= 0.0 or damage_mult <= 0.0 or buff_duration_ms <= 0.0 or aura_range <= 0.0 or lifetime_ms <= 0.0:
		return {"ok": false, "reason": "grove aura modifier '%s' carries no readable stat row (ARMOR seen=%s %.3f, DAMAGE_MULT seen=%s %.3f), range, or lifetime is not converted" % [
			modifier_id, saw_armor, armor_mult, saw_damage, damage_mult,
		]}
	if filter == "" or (not filter.contains("ANY") and not filter.contains("+")):
		return {"ok": false, "reason": "grove aura object filter is not converted"}
	return {"ok": true, "effect": {
		"kind": "grove_aura",
		"armor_mult": armor_mult,
		# Usually authored alongside ARMOR on the same modifier leaf (RotWK
		# GenericBuff: ARMOR 50% + DAMAGE_MULT 150%). When the leaf authors no
		# DAMAGE_MULT row at all (BFME2 GenericArmorLeadership) this stays at the
		# neutral 1.0, which is what "absent" means — a row that is present and
		# unreadable never reaches here.
		"damage_mult": damage_mult,
		# Which rows the leaf actually authored, so a consumer can tell an
		# authored-neutral 1.0 from a defaulted one.
		"authored_rows": {"ARMOR": saw_armor, "DAMAGE_MULT": saw_damage},
		# Named residual rows carried onto the effect so the runner and the report
		# can COUNT them instead of losing them — same key and same shape as the
		# SpecialPowerModule lane's.
		"unsupported_modifier_rows": unsupported_rows,
		"buff_duration_ticks": maxi(1, roundi(buff_duration_ms / 1000.0 / sim.TICK_SECONDS)),
		"range_source": aura_range,
		"lifetime_ticks": maxi(1, roundi(lifetime_ms / 1000.0 / sim.TICK_SECONDS)),
		"filter": filter,
		"modifier": modifier_id,
		"terrain_object_id": grove_id,
		# Retail's RequiredConditions cell type (TAINT / ELVEN_WOOD). Presentation
		# reads this to pick the ground decal; "" when the leaf does not author it.
		"terrain_condition": String(aura.get("requiredConditions", "")),
		# Presentation-only: the authored tree chain behind the grove. Absent
		# when the chain does not fully convert, and then no geometry is placed.
		"trees": _spellbook_grove_chain(references, object_leaves, ocl_leaves),
	}}


func _spellbook_field_ping_support(spawns: Array, modifier_leaves: Dictionary) -> Dictionary:
	## Reveal/field family (Farsight, Palantir Vision, Frozen Land, and the
	## Enshrouding Mist gap). Retail spawns a "ping": an object that is not a unit
	## at all — IMMOBILE, UNATTACKABLE, no weapon, no hatch — whose entire runtime
	## is a bounded VisionRange reveal plus optional AttributeModifierAuraUpdate
	## rows, ended by its authored LifetimeUpdate
	## (data/ini/object/system/system.ini:1905-1955, :1997-2062;
	## FrozenLandPing lifetime FROZEN_LAND_EFFECT_DURATION = gamedata.ini:3595).
	##
	## Returns {} when the spawn is not this shape, so the caller falls through to
	## the summon/structure resolvers unchanged.
	if spawns.is_empty():
		return {}
	var leaf: Dictionary = (spawns[0] as Dictionary).get("leaf", {}) as Dictionary
	if leaf.is_empty():
		return {}
	var kind_of: Array = leaf.get("kindOf", []) as Array
	var lifetime: Dictionary = leaf.get("lifetime", {}) as Dictionary
	var auras: Array = leaf.get("auras", []) as Array
	if auras.is_empty() and typeof(leaf.get("aura", null)) == TYPE_DICTIONARY:
		auras = [leaf.get("aura", {})]
	var vision_range := float(leaf.get("visionRange", 0.0))
	var is_ping := (
		kind_of.has("IMMOBILE")
		and kind_of.has("UNATTACKABLE")
		and (leaf.get("locomotor", {}) as Dictionary).is_empty()
		and Array(leaf.get("fireWeapons", [])).is_empty()
		and String(leaf.get("weaponId", "")) == ""
		and typeof(leaf.get("hatch", null)) != TYPE_DICTIONARY
		and (leaf.get("horde", {}) as Dictionary).is_empty()
		and float(lifetime.get("maxMs", 0.0)) > 0.0
		and (vision_range > 0.0 or not auras.is_empty())
	)
	if not is_ping:
		return {}
	var object_id := String(leaf.get("id", ""))
	var unconverted: Array = Array(leaf.get("unconvertedBehaviors", [])).duplicate()
	var invisibility_result := _spellbook_ping_invisibility_rules(leaf)
	if not bool(invisibility_result.get("ok", false)):
		return {"ok": false, "reason": "ping '%s' invisibility update is not executable: %s" % [object_id, String(invisibility_result.get("reason", ""))]}
	var invisibility_updates := invisibility_result.get("rules", []) as Array
	if unconverted.has("InvisibilityUpdate") and invisibility_updates.is_empty():
		return {"ok": false, "reason": "ping '%s' retains InvisibilityUpdate only as an unconverted marker" % object_id}
	if not invisibility_updates.is_empty():
		unconverted.erase("InvisibilityUpdate")
	var compiled_auras: Array = []
	var effect_auras := 0
	for aura_value in auras:
		var verdict := _spellbook_one_summon_aura_rule(
			leaf, aura_value as Dictionary, modifier_leaves, true
		)
		if not bool(verdict.get("ok", false)):
			return {"ok": false, "reason": "ping '%s' aura is not converted: %s" % [object_id, String(verdict.get("reason", ""))]}
		if bool(verdict.get("skip", false)):
			continue
		var aura: Dictionary = verdict.get("aura", {}) as Dictionary
		compiled_auras.append(aura)
		if not Array(aura.get("modifiers", [])).is_empty():
			effect_auras += 1
	var reveal_source := vision_range
	if reveal_source <= 0.0 and effect_auras == 0 and invisibility_updates.is_empty():
		return {"ok": false, "reason": "ping '%s' has neither a converted VisionRange reveal nor a stat-bearing aura" % object_id}
	var radius_source := reveal_source
	for aura_value in compiled_auras:
		radius_source = maxf(radius_source, float((aura_value as Dictionary).get("range_source", 0.0)))
	for invisibility_value in invisibility_updates:
		radius_source = maxf(radius_source, float((invisibility_value as Dictionary).get("broadcast_range_source", 0.0)))
	return {"ok": true, "effect": {
		"kind": "field_ping",
		"object_id": object_id,
		"lifetime_ticks": maxi(1, roundi(float(lifetime.get("maxMs", 0.0)) / 1000.0 / sim.TICK_SECONDS)),
		"reveal_radius_source": reveal_source,
		"radius_source": radius_source,
		"auras": compiled_auras,
		"invisibility_updates": invisibility_updates,
		# Named residual gaps carried onto the effect so the runner and the report
		# can count them instead of losing them (StealthDetectorUpdate on the
		# Palantir/Farsight base object: this reveal does NOT unmask stealth).
		"unconverted_behaviors": unconverted,
	}}


func _spellbook_ping_invisibility_rules(leaf: Dictionary) -> Dictionary:
	var rows := leaf.get("invisibilityUpdates", []) as Array
	var rules: Array[Dictionary] = []
	for row_value in rows:
		if typeof(row_value) != TYPE_DICTIONARY: return {"ok": false, "reason": "row is not a dictionary"}
		var row := row_value as Dictionary
		var type := String(row.get("invisibilityType", "")).to_upper()
		var update_ms := float(row.get("updatePeriodMs", 0.0))
		var broadcast_value: Variant = row.get("broadcast", false)
		var broadcast := false
		if typeof(broadcast_value) == TYPE_BOOL: broadcast = broadcast_value
		elif String(broadcast_value).to_upper() == "YES": broadcast = true
		if type not in ["CAMOUFLAGE", "STEALTH"] or update_ms <= 0.0: return {"ok": false, "reason": "type/update cadence missing"}
		if not broadcast: return {"ok": false, "reason": "field ping row does not author Broadcast Yes"}
		if typeof(row.get("broadcastRange")) not in [TYPE_INT, TYPE_FLOAT] or float(row.get("broadcastRange", -1.0)) < 0.0: return {"ok": false, "reason": "broadcast range unresolved"}
		if typeof(row.get("detectionRange")) not in [TYPE_INT, TYPE_FLOAT] or float(row.get("detectionRange", -1.0)) < 0.0: return {"ok": false, "reason": "detection range unresolved"}
		var filter_text := String(row.get("broadcastObjectFilter", "")).strip_edges()
		if filter_text == "" or (not filter_text.contains("ANY") and not filter_text.contains("ALL") and not filter_text.contains("+")): return {"ok": false, "reason": "broadcast object filter unresolved"}
		var starts_value: Variant = row.get("startsActive", false)
		var enabled := false
		if typeof(starts_value) == TYPE_BOOL: enabled = starts_value
		elif String(starts_value).to_upper() == "YES": enabled = true
		rules.append({"enabled":enabled, "update_ticks":maxi(1,sim._ship_contract_delay_ticks(update_ms)), "next_update_tick":sim.tick_index, "broadcast":true, "broadcast_range_source":float(row.get("broadcastRange")), "broadcast_filter":Array(filter_text.split(" ", false)), "invisibility_type":type, "forbidden_conditions":[], "forbidden_weapon_conditions":[], "hint_detectable_conditions":[], "options":[], "detection_range_source":float(row.get("detectionRange")), "become_fx_id":"", "exit_fx_id":"", "granted_ids":[], "tag":"field-ping", "source_ini":String(row.get("sourceIni", "")), "line":int(row.get("line", 0)), "unsupported_semantics":[]})
	return {"ok": true, "rules": rules}


func _spellbook_cloudbreak_support(field_values: Dictionary, field_resolved: Dictionary) -> Dictionary:
	## Cloud Break: WeatherDuration resolves in the doc; the enemy disruption
	## is the module's authored affects filter over the weather duration.
	var duration_ms := float(field_resolved.get("WeatherDuration", 0.0))
	var affects := String(field_values.get("AttributeModifierAffects", ""))
	var weather := String(field_values.get("ChangeWeather", ""))
	if duration_ms <= 0.0 or affects == "" or weather == "":
		return {"ok": false, "reason": "cloud break duration, affects filter, or weather is not converted"}
	var required := {
		"ReEnableAntiCategory": "Yes",
		"AttributeModifierWeatherBased": "Yes",
		"SunbeamObject": "CloudBreakSunbeam",
	}
	for authored in required:
		if String(field_values.get(authored, "")) != String(required[authored]):
			return {"ok": false, "reason": "cloud break %s is not the supported authored value" % authored}
	if int(field_resolved.get("ObjectSpacing", -1)) != 300:
		return {"ok": false, "reason": "cloud break ObjectSpacing is not the supported authored value"}
	return {"ok": true, "effect": {
		"kind": "cloudbreak_stun",
		"duration_ticks": maxi(1, roundi(duration_ms / 1000.0 / sim.TICK_SECONDS)),
		"affects": affects,
		"weather": weather,
		"sunbeam_object_id": String(field_values.get("SunbeamObject", "")),
		"object_spacing_source": float(field_resolved.get("ObjectSpacing", 0.0)),
	}}


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
	sim.team_power_points[team] = power_points(team) - doc_cost
	(sim.purchased_powers[team] as Array).append(power_id)
	(sim._team_sciences[team] as Array).append(String(row.get("science_id", "")))
	(sim._staged_purchases[team] as Array).append({"power_id": power_id, "science_id": String(row.get("science_id", "")), "cost": doc_cost})
	if bool(passive.get("activate", false)):
		sim._scavenger_bounty_percent[team] = float(passive.get("bounty_percent", 0.0))
		(sim._consumed_nonpressable_powers[team] as Dictionary)[power_id] = true
	sim._emit_event("power.purchased", 0, 0, {
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
	if not bool(_team_tree(team).get("ready", false)):
		return {"ok": false, "reason": "spellbook-unavailable"}
	var refunded := 0
	var restored: Array = []
	for entry_value in Array(sim._staged_purchases.get(team, [])):
		if typeof(entry_value) != TYPE_DICTIONARY:
			continue
		var entry := entry_value as Dictionary
		var power_id := String(entry.get("power_id", ""))
		refunded += int(entry.get("cost", 0))
		(sim.purchased_powers[team] as Array).erase(power_id)
		(sim._team_sciences[team] as Array).erase(String(entry.get("science_id", "")))
		var row: Dictionary = (_team_tree(team).get("powers", {}) as Dictionary).get(power_id, {}) as Dictionary
		if bool(row.get("nonpressable", false)) and (sim._consumed_nonpressable_powers.get(team, {}) as Dictionary).has(power_id):
			(sim._consumed_nonpressable_powers[team] as Dictionary).erase(power_id)
			if String((row.get("effect", {}) as Dictionary).get("kind", "")) == "scavenger_bounty":
				sim._scavenger_bounty_percent[team] = 0.0
		restored.append(power_id)
	sim._staged_purchases[team] = []
	if refunded > 0:
		sim.team_power_points[team] = power_points(team) + refunded
	sim._emit_event("power.reset", 0, 0, {"team": team, "refunded": refunded, "powers": restored})
	return {"ok": true, "reason": "", "refunded": refunded, "powers": restored}


func accept_spellbook_purchases(team: int) -> Dictionary:
	## Retail ACCEPT (including closing the orb): the session's picks commit.
	if not bool(_team_tree(team).get("ready", false)):
		return {"ok": false, "reason": "spellbook-unavailable"}
	sim._staged_purchases[team] = []
	return {"ok": true, "reason": ""}


func power_cooldown_state(team: int, power_id: String) -> Dictionary:
	var row: Dictionary = (_team_tree(team).get("powers", {}) as Dictionary).get(power_id, {}) as Dictionary
	if row.is_empty():
		return {}
	var total = sim.spell_recharge_ticks_for_team(team, int(row.get("reload_ticks", 1)))
	var ready_tick := int((sim._power_cooldown_until.get(team, {}) as Dictionary).get(power_id, -1))
	var remaining = maxi(0, ready_tick - sim.tick_index)
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
	if not sim._is_combatant_team(team):
		return
	var kills_per_point := maxi(1, int(sim._rules.get("power_point_kills", sim.POWER_POINT_KILLS)))
	sim._kills_toward_power_point[team] = int(sim._kills_toward_power_point.get(team, 0)) + 1
	if int(sim._kills_toward_power_point[team]) >= kills_per_point:
		sim._kills_toward_power_point[team] = 0
		sim.team_power_points[team] = power_points(team) + 1
		sim._emit_event("power.point_earned", 0, 0, {"team": team, "points": power_points(team)})


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
	var ladder: Array[Dictionary] = []
	sim._player_rank_ladder_error = ""
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
				"Rank %d has no resolved %s" % [rank, sim.RANK_SCIENCE_PURCHASE_POINTS_GRANTED_FIELD]
			)
		var threshold := _rank_ladder_integer(row, "skillPointsNeededDefault")
		if threshold < 0:
			return _reject_player_rank_ladder(
				"Rank %d has no resolved %s" % [rank, sim.RANK_SKILL_POINTS_NEEDED_FIELD]
			)
		if threshold <= previous_threshold:
			return _reject_player_rank_ladder(
				"Rank %d %s does not ascend: %d follows %d"
				% [rank, sim.RANK_SKILL_POINTS_NEEDED_FIELD, threshold, previous_threshold]
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
	if ladder == sim._player_rank_ladder:
		# Rank.ini is a system file, so a cross-faction team document carries
		# the same ladder. Re-binding an identical ladder must not wipe the
		# per-team rank ledgers that are already standing.
		return true
	sim._player_rank_ladder = ladder
	sim._team_player_rank.clear()
	sim._team_player_skill_points.clear()
	sim._team_player_rank_granted.clear()
	return true


func _reject_player_rank_ladder(reason: String) -> bool:
	sim._player_rank_ladder.clear()
	sim._player_rank_ladder_error = reason
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
	if sim._player_rank_ladder.is_empty():
		return {"ok": false, "reason": "rank-ladder-unavailable", "detail": sim._player_rank_ladder_error}
	if not sim._is_combatant_team(team):
		return {"ok": false, "reason": "not-a-combatant-team"}
	var known := false
	for entry in sim._player_rank_ladder:
		if int(entry.get("rank", 0)) == rank:
			known = true
			break
	if not known:
		return {"ok": false, "reason": "unknown-rank", "rank": rank}
	var granted_total := 0
	var crossed: Array[int] = []
	var ledger: Dictionary = sim._team_player_rank_granted.get(team, {}) as Dictionary
	for entry in sim._player_rank_ladder:
		var entry_rank := int(entry.get("rank", 0))
		if entry_rank > rank or ledger.has(entry_rank):
			continue
		ledger[entry_rank] = int(entry.get("granted", 0))
		granted_total += int(entry.get("granted", 0))
		crossed.append(entry_rank)
	sim._team_player_rank_granted[team] = ledger
	sim._team_player_rank[team] = maxi(player_rank(team), rank)
	if granted_total > 0:
		sim.team_power_points[team] = power_points(team) + granted_total
		sim._emit_event("power.rank_granted", 0, 0, {
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
	if sim._player_rank_ladder.is_empty():
		return {"ok": false, "reason": "rank-ladder-unavailable", "detail": sim._player_rank_ladder_error}
	if not sim._is_combatant_team(team):
		return {"ok": false, "reason": "not-a-combatant-team"}
	if amount < 0:
		return {"ok": false, "reason": "negative-skill-points"}
	sim._team_player_skill_points[team] = player_skill_points(team) + amount
	var reached := player_rank(team)
	for entry in sim._player_rank_ladder:
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
	var document = sim._spellbook_document
	if sim._team_spellbooks.has(team):
		document = (sim._team_spellbooks[team] as Dictionary).get("document", {}) as Dictionary
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
	var row := _spellbook_science_document_row(sim.PLAYER_TEAM, science_id)
	if row.is_empty():
		return {}
	return _science_field_receipt(
		row,
		sim.SCIENCE_PURCHASE_POINT_COST_MP_FIELD if multiplayer else sim.SCIENCE_PURCHASE_POINT_COST_FIELD,
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
	if not sim._is_combatant_team(team):
		return {"ok": false, "reason": "not-a-combatant-team"}
	var row := _spellbook_science_document_row(team, science_id)
	if row.is_empty():
		return {"ok": false, "reason": "unknown-science"}
	if not bool(row.get("isGrantable", false)):
		return {
			"ok": false,
			"reason": "science-not-grantable",
			"receipt": _science_field_receipt(row, sim.SCIENCE_IS_GRANTABLE_FIELD),
		}
	if _science_owned(team, science_id):
		return {"ok": false, "reason": "already-owned"}
	if not _science_prerequisites_met(team, row):
		return {
			"ok": false,
			"reason": "prerequisites-unmet",
			"receipt": _science_field_receipt(row, sim.SCIENCE_PREREQUISITE_SCIENCES_FIELD),
		}
	(sim._team_sciences[team] as Array).append(science_id)
	sim._emit_event("science.granted", 0, 0, {"team": team, "science_id": science_id})
	return {
		"ok": true,
		"reason": "",
		"science_id": science_id,
		"receipt": _science_field_receipt(row, sim.SCIENCE_IS_GRANTABLE_FIELD),
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


func _cast_spellbook_scavenger(team: int, effect: Dictionary) -> Dictionary:
	var percent := float(effect.get("bounty_percent", -1.0))
	if not sim._is_combatant_team(team) or not is_finite(percent) or percent < 0.0:
		return {"ok": false, "reason": "invalid-scavenger-contract"}
	sim._scavenger_bounty_percent[team] = percent
	return {"ok": true, "bounty_percent": percent}


func cast_power(team: int, power_id: String, point: Vector2) -> Dictionary:
	var tree := _team_tree(team)
	if not bool(tree.get("ready", false)):
		return {"ok": false, "reason": "spellbook-unavailable"}
	var row: Dictionary = (tree.get("powers", {}) as Dictionary).get(power_id, {}) as Dictionary
	if row.is_empty():
		return {"ok": false, "reason": "unknown-power"}
	if not has_power(team, power_id):
		return {"ok": false, "reason": "power-not-purchased"}
	if not bool(row.get("castable", false)):
		return {"ok": false, "reason": "effect-unsupported", "detail": String(row.get("locked_reason", ""))}
	if bool(row.get("nonpressable", false)) and (sim._consumed_nonpressable_powers.get(team, {}) as Dictionary).has(power_id):
		return {"ok": false, "reason": "power-already-activated"}
	var cooldown := power_cooldown_state(team, power_id)
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
		(sim._consumed_nonpressable_powers[team] as Dictionary)[power_id] = true
	(sim._power_cooldown_until[team] as Dictionary)[power_id] = sim.tick_index + sim.spell_recharge_ticks_for_team(
		team, int(row.get("reload_ticks", 1))
	)
	# A staged pick that gets cast is spent: RESET can no longer refund it.
	var staged: Array = sim._staged_purchases[team]
	for index in range(staged.size() - 1, -1, -1):
		if String((staged[index] as Dictionary).get("power_id", "")) == power_id:
			staged.remove_at(index)
	sim._emit_event("power.cast", 0, 0, {
		"team": team,
		"power_id": power_id,
		"science_id": String(row.get("science_id", "")),
		"sound_id": String(row.get("sound_id", "")),
		"effect_kind": String(effect.get("kind", "")),
		"radius_source": float(effect.get("radius_source", effect.get("range_source", 0.0))),
		# Map-scaled twin of radius_source so the presentation cue can cover the
		# ground the power actually affected without re-deriving the scale.
		"fx_radius": snappedf(
			float(effect.get("radius_source", effect.get("range_source", 0.0))) * _spellbook_world_scale(),
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
	var radius = float(effect.get("radius_source", 0.0)) * _spellbook_world_scale()
	if not bool(effect.get("as_percent", true)):
		return _cast_spellbook_structure_heal(team, effect, point, radius)
	var fraction := float(effect.get("amount", 0.5))
	var healed := 0
	for id in sim.living_ids(team):
		var row: Dictionary = sim.entities[id]
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
	var affects := String(effect.get("affects", ""))
	if not affects.contains("STRUCTURE"):
		return {"ok": false, "reason": "no-wounded-allies-in-range"}
	var amount := int(effect.get("amount", 0))
	var healed := 0
	for structure_id in sim.structure_ids(team):
		var structure: Dictionary = sim.structures[structure_id]
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
	var range_sim := float(effect.get("range_source", 0.0)) * _spellbook_world_scale()
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
	for id in sim.living_ids(team):
		var row: Dictionary = sim.entities[id]
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
			row["rally_until_tick"] = sim.tick_index + duration_ticks
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
	var strikes: Array = effect.get("strikes", []) as Array
	if strikes.is_empty():
		return {"ok": false, "reason": "no-strikes"}
	for strike_value in strikes:
		var strike := strike_value as Dictionary
		sim._pending_power_effects.append({
			"kind": "strike",
			"fire_tick": sim.tick_index + maxi(0, roundi(float(strike.get("delay_ms", 0.0)) / (sim.TICK_SECONDS * 1000.0))),
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
	var targets: Array = effect.get("targets", []) as Array
	if effect.has("target_groups"):
		targets = _spellbook_resolve_summon_targets(
			Array(effect.get("target_groups", []))
		)
	if targets.is_empty():
		return {"ok": false, "reason": "no-summon-targets"}
	sim._pending_power_effects.append({
		"kind": "summon",
		"fire_tick": sim.tick_index + int(effect.get("hatch_delay_ticks", 0)),
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
	var structure_id = sim._next_dynamic_structure_id
	sim._next_dynamic_structure_id += 1
	var build_ticks := int(effect.get("build_ticks", 1))
	var health := int(effect.get("health", 1))
	var weapon: Dictionary = effect.get("weapon", {}) as Dictionary
	sim._note_structure_table_mutation()
	sim.structures[structure_id] = {
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
			"range": float(weapon.get("range_source", 0.0)) * _spellbook_world_scale(),
			"damage_type": String(weapon.get("damage_type", "")),
			"period_ticks": maxi(1, roundi(float(weapon.get("period_ms", 0.0)) / (sim.TICK_SECONDS * 1000.0))),
			"pre_attack_ticks": maxi(0, roundi(float(weapon.get("pre_attack_ms", 0.0)) / (sim.TICK_SECONDS * 1000.0))),
			"cooldown": 0,
			"affects": String(weapon.get("affects", "ENEMIES")),
		},
	}
	sim._apply_structure_inherit_upgrades(sim.structures[structure_id] as Dictionary)
	sim._initialize_structure_auto_deposit(sim.structures[structure_id] as Dictionary)
	sim._emit_event("power.structure_summon", 0, structure_id, {"team": team, "object_id": String(effect.get("object_id", "")), "build_ticks": build_ticks, "health": health})
	return {"ok": true, "reason": "", "battalions": 0}


func _cast_spellbook_grove(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	sim._active_groves.append({
		"team": team,
		"point": point,
		"range_sim": float(effect.get("range_source", 0.0)) * _spellbook_world_scale(),
		"armor_mult": float(effect.get("armor_mult", 1.0)),
		"damage_mult": float(effect.get("damage_mult", 1.0)),
		"buff_duration_ticks": int(effect.get("buff_duration_ticks", 1)),
		"despawn_tick": sim.tick_index + int(effect.get("lifetime_ticks", 1)),
		"filter": String(effect.get("filter", "")),
		"terrain_condition": String(effect.get("terrain_condition", "")),
		# The AUTHORED leaf id, so the accumulator key below is per-modifier-list.
		"modifier": String(effect.get("modifier", "")),
	})
	var grove_trees: Dictionary = effect.get("trees", {}) as Dictionary
	sim._emit_event("power.grove", 0, 0, {
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
	var scale := _spellbook_world_scale()
	var reveal_source := float(effect.get("reveal_radius_source", 0.0))
	var ping_sequence = sim._field_pings.size()
	sim._field_pings.append({
		"team": team,
		"power_id": power_id,
		"object_id": String(effect.get("object_id", "")),
		"point": point,
		"reveal_radius_source": reveal_source,
		"reveal_radius_sim": reveal_source * scale,
		"expire_tick": sim.tick_index + int(effect.get("lifetime_ticks", 1)),
		"auras": (effect.get("auras", []) as Array).duplicate(true),
		"invisibility_updates": (effect.get("invisibility_updates", []) as Array).duplicate(true),
		"invisibility_source_prefix": "field-ping:%d:%s:%d:%d" % [team, power_id, sim.tick_index, ping_sequence],
	})
	sim._emit_event("power.field_ping", 0, 0, {
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
	if sim._field_pings.is_empty():
		return
	var living: Array[Dictionary] = []
	for ping in sim._field_pings:
		if sim.tick_index >= int(ping.get("expire_tick", -1)):
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
			if sim.tick_index % maxi(1, int(aura.get("refresh_ticks", 1))) != 0:
				continue
			var radius = float(aura.get("range_source", 0.0)) * _spellbook_world_scale()
			for target_id in sim._spatial_gather_sorted(origin, radius):
				if not sim.entities.has(target_id):
					continue
				var target: Dictionary = sim.entities[target_id]
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
				sim._set_timed_modifier(
					target,
					"field-ping:%s:%s" % [String(ping.get("object_id", "")), String(aura.get("id", ""))],
					Array(aura.get("modifiers", [])),
					sim.tick_index + int(aura.get("duration_ticks", 1))
				)
		for policy_value in ping.get("invisibility_updates", []) as Array:
			var policy := policy_value as Dictionary
			if sim.tick_index < int(policy.get("next_update_tick", 0)): continue
			policy["next_update_tick"] = sim.tick_index + maxi(1, int(policy.get("update_ticks", 1)))
			var source_key := "%s:%s" % [String(ping.get("invisibility_source_prefix", "field-ping")), String(policy.get("tag", ""))]
			var prior := policy.get("granted_ids", []) as Array; var desired: Array[int] = []
			var radius = float(policy.get("broadcast_range_source", 0.0)) * _spellbook_world_scale(); var filter_tokens := policy.get("broadcast_filter", []) as Array
			if bool(policy.get("enabled", false)):
				for target_id in sim.entity_ids():
					var target := sim.entities[target_id] as Dictionary
					if int(target.get("health", 0)) <= 0 or int(target.get("team", -1)) != team: continue
					if origin.distance_to(Vector2(target.get("position", Vector2.ZERO))) > radius or not sim._transport_filter_accepts(target, filter_tokens): continue
					desired.append(target_id); sim._set_invisibility_source(target, source_key, policy, true, 0)
			for target_id in prior:
				if not desired.has(int(target_id)) and sim.entities.has(int(target_id)): sim._set_invisibility_source(sim.entities[int(target_id)] as Dictionary, source_key, policy, false, 0)
			policy["granted_ids"] = desired
	sim._field_pings = living


func _revoke_field_ping_invisibility(ping: Dictionary) -> void:
	for policy_value in ping.get("invisibility_updates", []) as Array:
		var policy := policy_value as Dictionary
		var source_key := "%s:%s" % [String(ping.get("invisibility_source_prefix", "field-ping")), String(policy.get("tag", ""))]
		for target_id in policy.get("granted_ids", []) as Array:
			if sim.entities.has(int(target_id)): sim._set_invisibility_source(sim.entities[int(target_id)] as Dictionary, source_key, policy, false, 0)
		policy["granted_ids"] = []


func _cast_spellbook_cloudbreak(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	## Cloud Break: enemy units matching the authored filter are disrupted for
	## the weather duration (SPELL_CLOUDBREAK_DURATION).
	var duration_ticks := int(effect.get("duration_ticks", 1))
	var stunned := 0
	for id in sim.living_ids(1 - team):
		var row: Dictionary = sim.entities[id]
		if not _spellbook_affects(row, String(effect.get("affects", ""))):
			continue
		row["stun_until_tick"] = sim.tick_index + duration_ticks
		row["route"] = []
		row["route_cells"] = []
		row["target_id"] = 0
		row["state"] = "idle"
		stunned += 1
	_revoke_opposing_weather_for_cloudbreak(team)
	sim._emit_event("power.cloudbreak", 0, 0, {
		"team": team,
		"weather": String(effect.get("weather", "")),
		"stunned": stunned,
		"duration_ticks": duration_ticks,
		"sunbeam_object_id": String(effect.get("sunbeam_object_id", "")),
		"object_spacing_source": float(effect.get("object_spacing_source", 0.0)),
	})
	return {"ok": true, "reason": "", "battalions": stunned}


func _revoke_opposing_weather_for_cloudbreak(team: int) -> void:
	var retained: Array[Dictionary] = []
	for entry in sim._weather_effects:
		if int(entry.get("team", -1)) == team:
			retained.append(entry)
			continue
		match String(entry.get("kind", "")):
			"weather_modifier":
				var key := String(entry.get("source_key", ""))
				for id in sim.entity_ids():
					(sim.entities[id].get("timed_modifiers", {}) as Dictionary).erase(key)
			"weather_anticategory":
				for id in sim.entity_ids():
					_erase_leadership_suppression_source(
						sim.entities[id], String(entry.get("source_key", ""))
					)
			_:
				retained.append(entry)
	sim._weather_effects = retained


func _cast_spellbook_weather_modifier(team: int, power_id: String, effect: Dictionary) -> Dictionary:
	## Darkness. Global, no cast point: the whole map is under the weather for
	## WeatherDuration, and every unit the authored filter accepts carries the
	## modifier leaf's rows for exactly that window.
	var expire_tick = sim.tick_index + int(effect.get("duration_ticks", 1))
	var source_key := "weather:%d:%s:%d" % [team, power_id, sim.tick_index]
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
	sim._weather_effects.append(entry)
	var affected := _apply_weather_modifier(entry)
	sim._emit_event("power.weather", 0, 0, {
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
	var expire_tick = sim.tick_index + int(effect.get("duration_ticks", 1))
	var source_key := "weather:%d:%s:%d" % [team, power_id, sim.tick_index]
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
	sim._weather_effects.append(entry)
	var affected := _apply_weather_anticategory(entry)
	sim._emit_event("power.weather", 0, 0, {
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
	var team := int(entry.get("team", -1))
	var affects := String(entry.get("affects", ""))
	var key := String(entry.get("source_key", ""))
	var expire_tick = int(entry.get("expire_tick", -1))
	var modifiers: Array = entry.get("modifiers", []) as Array
	var affected := 0
	for id in sim.entity_ids():
		var row: Dictionary = sim.entities[id]
		if int(row.get("health", 0)) <= 0:
			continue
		if not _spellbook_member_affects(row, affects, int(row.get("team", -1)) == team):
			continue
		sim._set_timed_modifier(row, key, modifiers, expire_tick)
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
	var sources: Dictionary = row.get("leadership_suppression_sources", {}) as Dictionary
	var legacy := int(row.get("leadership_suppressed_until_tick", -1))
	if sources.is_empty() and legacy > sim.tick_index:
		sources["legacy"] = legacy
	var effective := -1
	for source_key in sources.keys():
		var expire_tick = int(sources[source_key])
		if expire_tick <= sim.tick_index:
			sources.erase(source_key)
		else:
			effective = maxi(effective, expire_tick)
	if sources.is_empty():
		row.erase("leadership_suppression_sources")
	else:
		row["leadership_suppression_sources"] = sources
	if effective > sim.tick_index:
		row["leadership_suppressed_until_tick"] = effective
	else:
		row.erase("leadership_suppressed_until_tick")
	return effective


func _apply_weather_anticategory(entry: Dictionary) -> int:
	var team := int(entry.get("team", -1))
	var affects := String(entry.get("affects", ""))
	var expire_tick = int(entry.get("expire_tick", -1))
	var source_key := String(entry.get("source_key", ""))
	if source_key == "":
		source_key = "weather:legacy:%d:%d" % [team, expire_tick]
	var affected := 0
	for id in sim.entity_ids():
		var row: Dictionary = sim.entities[id]
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
	if sim._weather_effects.is_empty():
		return
	var living: Array[Dictionary] = []
	for entry in sim._weather_effects:
		if sim.tick_index >= int(entry.get("expire_tick", -1)):
			continue
		living.append(entry)
		if sim.tick_index % sim.ABILITY_AURA_INTERVAL_TICKS != 0:
			continue
		match String(entry.get("kind", "")):
			"weather_modifier":
				_apply_weather_modifier(entry)
			"weather_anticategory":
				_apply_weather_anticategory(entry)
	sim._weather_effects = living


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
	if sim._weather_effects.is_empty():
		return
	var migrated_sources := {}
	for index in range(sim._weather_effects.size()):
		var entry: Dictionary = sim._weather_effects[index]
		if String(entry.get("source_key", "")) == "":
			entry["source_key"] = "weather:legacy:%d:%s:%d:%d" % [
				int(entry.get("team", -1)), String(entry.get("power_id", "")),
				int(entry.get("expire_tick", -1)), index,
			]
			migrated_sources[String(entry["source_key"])] = true
	if migrated_sources.is_empty():
		return
	for id in sim.entity_ids():
		var row: Dictionary = sim.entities[id]
		var table: Dictionary = row.get("timed_modifiers", {}) as Dictionary
		var old_weather_payloads := {}
		for entry in sim._weather_effects:
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
		for entry in sim._weather_effects:
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
		if legacy_deadline > sim.tick_index and (
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
	var radius = float(effect.get("range_source", 0.0)) * _spellbook_world_scale()
	var converted_lairs: Array[int] = []
	var converted_guards: Array[int] = []
	for lair_id in sim.structure_ids():
		var lair: Dictionary = sim.structures[lair_id]
		if String(lair.get("structure_kind", "")) != "lair":
			continue
		if not sim._is_hostile(team, int(lair.get("team", -1))):
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
		sim._apply_scenario_structure_faction_command_set(lair, team)
		var spawn_policy := lair.get("spawn_behavior", {}) as Dictionary
		for child_value in spawn_policy.get("spawned_ids", []) as Array:
			var child_id := int(child_value)
			if not sim.entities.has(child_id):
				continue
			var child := sim.entities[child_id] as Dictionary
			if int(child.get("health", 0)) <= 0:
				continue
			child["team"] = team
			converted_guards.append(child_id)
		converted_lairs.append(lair_id)
	if converted_lairs.is_empty():
		return {"ok": false, "reason": "no-valid-targets"}
	sim._emit_event("power.creep_allegiance", 0, 0, {
		"team": team,
		"point": [snappedf(point.x, 0.001), snappedf(point.y, 0.001)],
		"radius_source": float(effect.get("range_source", 0.0)),
		"lairs": converted_lairs,
		"guards": converted_guards,
		"filter_matches": Array(effect.get("lair_types", [])).size(),
	})
	return {"ok": true, "reason": "", "battalions": converted_guards.size(), "structures": converted_lairs.size()}


func _step_pending_power_effects() -> void:
	if sim._pending_power_effects.is_empty():
		return
	# Clear first so a fired effect that kills another death-weapon carrier can
	# append its new schedule without being overwritten by this pass.
	var processing = sim._pending_power_effects
	# Typed replacement: a plain [] cannot be assigned to the sim's
	# Array[Dictionary] member across the subsystem boundary.
	var cleared: Array[Dictionary] = []
	sim._pending_power_effects = cleared
	for effect in processing:
		if sim.tick_index < int(effect.get("fire_tick", 0)):
			sim._pending_power_effects.append(effect)
			continue
		match String(effect.get("kind", "")):
			"strike":
				_fire_power_strike(effect)
			"summon":
				_fire_power_summon(effect)
			"death_weapon":
				_fire_death_weapon(effect)


func _fire_death_weapon(effect: Dictionary) -> void:
	var weapon_id := String(effect.get("weapon_id", ""))
	var rule: Dictionary = effect.get("weapon_rule", {}) as Dictionary
	var point := Vector2(effect.get("point", Vector2.ZERO))
	var source_team := int(effect.get("team", -1))
	if rule.is_empty():
		sim._emit_event("module.death_weapon_unresolved", int(effect.get("source_id", 0)), 0, {
			"weapon_id": weapon_id,
			"point": [snappedf(point.x, 0.001), snappedf(point.y, 0.001)],
			"height_source": float(effect.get("height_source", 0.0)),
			"death_type": String(effect.get("death_type", "")),
		})
		return
	var radius = sim._retail_source_to_sim_offset(
		Vector2(float(rule.get("radius_source", 0.0)), 0.0)
	).length()
	var amount := float(rule.get("damage", 0.0))
	var damage_type := String(rule.get("damage_type", ""))
	var affects := String(rule.get("affects", "ENEMIES"))
	var battalions := 0
	var hit_structures := 0
	for entity_id in sim.entity_ids():
		var target: Dictionary = sim.entities[entity_id]
		if int(target.get("health", 0)) <= 0:
			continue
		var allied := int(target.get("team", -1)) == source_team
		if (allied and not affects.contains("ALLIES")) or (not allied and not affects.contains("ENEMIES")):
			continue
		if not allied and not sim._is_hostile(source_team, int(target.get("team", -1))):
			continue
		if Vector2(target.get("position", Vector2.ZERO)).distance_to(point) > radius:
			continue
		_apply_area_damage_to_battalion(entity_id, amount, damage_type)
		battalions += 1
	for structure_id in sim.structure_ids():
		var structure: Dictionary = sim.structures[structure_id]
		if int(structure.get("health", 0)) <= 0:
			continue
		var allied := int(structure.get("team", -1)) == source_team
		if (allied and not affects.contains("ALLIES")) or (not allied and not affects.contains("ENEMIES")):
			continue
		if not allied and not sim._is_hostile(source_team, int(structure.get("team", -1))):
			continue
		if Vector2(structure.get("position", Vector2.ZERO)).distance_to(point) > radius:
			continue
		_apply_area_damage_to_structure(structure_id, amount, damage_type)
		hit_structures += 1
	sim._emit_event("module.death_weapon_fired", int(effect.get("source_id", 0)), 0, {
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
	var point := Vector2(effect.get("point", Vector2.ZERO))
	var radius = float(effect.get("radius_source", 0.0)) * _spellbook_world_scale()
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
		for id in sim.living_ids(affected_team):
			var row: Dictionary = sim.entities[id]
			if Vector2(row.get("position", Vector2.ZERO)).distance_to(point) > radius:
				continue
			_apply_area_damage_to_battalion(id, amount, String(effect.get("damage_type", "")))
			hit_battalions += 1
		for structure_id in sim.structure_ids(affected_team):
			var structure: Dictionary = sim.structures[structure_id]
			if int(structure.get("health", 0)) <= 0:
				continue
			if Vector2(structure.get("position", Vector2.ZERO)).distance_to(point) > radius:
				continue
			_apply_area_damage_to_structure(structure_id, amount, String(effect.get("damage_type", "")))
			hit_structures += 1
	sim._emit_event("power.strike", 0, 0, {
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
	var row: Dictionary = sim.entities.get(id, {})
	if row.is_empty() or int(row.get("health", 0)) <= 0:
		return
	var total = maxf(0.0, amount * float(sim._stance_state(row).get("incomingDamageMultiplier", 1.0)) * float(sim._formation_effects(row).get("incoming_damage_multiplier", 1.0)))
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
	row["last_damage_tick"] = sim.tick_index
	sim.record_hit_reaction(id, total)
	if aggregate <= 0:
		var death_policy = sim._bookkeep_battalion_death(
			id, row, "NORMAL", defeated_members
		)
		sim._emit_event("battalion.defeated", 0, id)
		if bool(death_policy.get("destroy_object", false)):
			sim.entities.erase(id)
	else:
		sim._apply_playable_unit_death_policy(row, "NORMAL", defeated_members)


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
			var entity_id := int(sim._next_dynamic_id.get(team, 1000))
			sim._next_dynamic_id[team] = entity_id + 1
			sim._add_battalion(
				entity_id, team, spawn_point, String(rule.get("horde_id", object_id)),
				object_id, object_id, 0, rule
			)
			if not sim.entities.has(entity_id):
				continue
			var lifetime_ticks := int(target.get("lifetime_ticks", 0))
			if lifetime_ticks > 0:
				sim._summon_despawn_ticks[entity_id] = sim.tick_index + lifetime_ticks
				sim.entities[entity_id]["summon_lifetime_death_type"] = String(
					target.get("lifetime_death_type", "")
				).to_upper()
			if not Array((sim.entities[entity_id] as Dictionary).get("summon_auras", [])).is_empty():
				sim._summon_aura_source_ids[entity_id] = true
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
	if object_type == "":
		return -1
	if sim.ring_mechanic_enabled and object_type == sim._ring_gollum_object_id() and not ring_fallback:
		var existing = sim._existing_ring_gollum_id()
		if existing != 0:
			var state = sim._ring_state()
			state["gollum_id"] = existing
			state["gollum_spawned"] = true
			print("[RetailSliceSim] RING_GOLLUM_SCRIPT_DUPLICATE_ABSORBED existing=%d object=%s" % [existing, object_type])
			return existing
	var unit_rules_value: Variant = sim._rules.get("unit_rules", {})
	if typeof(unit_rules_value) != TYPE_DICTIONARY:
		return -1
	var rule: Dictionary = (unit_rules_value as Dictionary).get(object_type, {}) as Dictionary
	if rule.is_empty():
		return sim.spawn_scenario_unit(object_type, team, at, scenario_surface)
	if not sim._next_dynamic_id.has(team):
		# Allocating outside the seeded per-team id ranges would collide with
		# another team's ids, so an unseeded team refuses rather than inventing
		# an id space.
		if team == sim.CREEP_TEAM and sim.ring_mechanic_enabled:
			sim._next_dynamic_id[team] = 80001
		else:
			return -1
	var entity_id := int(sim._next_dynamic_id[team])
	sim._next_dynamic_id[team] = entity_id + 1
	sim._add_battalion(
		entity_id,
		team,
		at,
		String(rule.get("horde_id", object_type)),
		object_type,
		object_type,
		0
	)
	if not sim.entities.has(entity_id):
		return -1
	if sim.ring_mechanic_enabled and object_type == sim._ring_gollum_object_id() and not ring_fallback:
		var state = sim._ring_state()
		state["gollum_id"] = entity_id
		state["gollum_spawned"] = true
		sim._configure_ring_gollum(sim.entities[entity_id] as Dictionary)
		sim._emit_event("ring.gollum_spawned", entity_id, 0, {"fallback": false, "script": true})
	return entity_id


func _step_summon_despawns() -> void:
	if sim._summon_despawn_ticks.is_empty():
		return
	var expired: Array = []
	for entity_id_value in sim._summon_despawn_ticks.keys():
		var entity_id := int(entity_id_value)
		if not sim.entities.has(entity_id):
			expired.append(entity_id)
			continue
		if sim.tick_index < int(sim._summon_despawn_ticks[entity_id_value]):
			continue
		# The authored summon lifetime ends: the battalion fades (no kill
		# credit — there is no attacker).
		var row: Dictionary = sim.entities[entity_id]
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
		sim._summon_despawn_ticks.erase(entity_id)
		var death_policy = sim._bookkeep_battalion_death(
			entity_id, row, death_type, defeated_members
		)
		sim._emit_event("power.summon_expired", 0, entity_id, {"team": int(row.get("team", -1))})
		# Honor the converted unit death policy. The authored death type begins its
		# matching path here; an immediate-destroy verdict removes now, while a retained
		# corpse is cleaned by the ordinary corpse scheduler.
		if bool(death_policy.get("destroy_object", false)):
			sim.entities.erase(entity_id)
		expired.append(entity_id)
	for entity_id in expired:
		sim._summon_despawn_ticks.erase(entity_id)


func _step_summon_auras() -> void:
	## Moving scout summons (Crebain/Cave Bats) carry GenericDebuff as part of
	## their payload. Refresh the converted timed modifier on enemy battalions;
	## its authored duration naturally lets the debuff trail the moving aura by
	## up to one refresh window after separation or expiry.
	if sim._summon_aura_source_ids.is_empty():
		return
	var source_ids: Array = sim._summon_aura_source_ids.keys()
	source_ids.sort()
	for source_id_value in source_ids:
		var source_id := int(source_id_value)
		if not sim.entities.has(source_id):
			sim._summon_aura_source_ids.erase(source_id)
			continue
		var source: Dictionary = sim.entities[source_id]
		if int(source.get("health", 0)) <= 0:
			continue
		for aura_value in Array(source.get("summon_auras", [])):
			_refresh_one_summon_aura(source_id, source, aura_value as Dictionary)


func _refresh_one_summon_aura(source_id: int, source: Dictionary, aura: Dictionary) -> void:
	if aura.is_empty():
		return
	var refresh_ticks := int(aura.get("refresh_ticks", 1))
	if sim.tick_index % maxi(1, refresh_ticks) != 0:
		return
	var source_team := int(source.get("team", -1))
	var origin := Vector2(source.get("position", Vector2.ZERO))
	var radius = float(aura.get("range_source", 0.0)) * _spellbook_world_scale()
	for target_id in sim._spatial_gather_sorted(origin, radius):
		if not sim.entities.has(target_id):
			continue
		var target: Dictionary = sim.entities[target_id]
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
		sim._set_timed_modifier(
			target,
			"summon-aura:%d:%s" % [source_id, String(aura.get("id", ""))],
			Array(aura.get("modifiers", [])),
			sim.tick_index + int(aura.get("duration_ticks", 1))
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
	if sim._active_groves.is_empty():
		return
	var living: Array[Dictionary] = []
	for grove in sim._active_groves:
		if sim.tick_index >= int(grove.get("despawn_tick", -1)):
			continue
		living.append(grove)
		var team := int(grove.get("team", -1))
		var point := Vector2(grove.get("point", Vector2.ZERO))
		var range_sim := float(grove.get("range_sim", 0.0))
		# A grove buffs a bounded disc, so this is a neighbourhood query over the
		# owning team rather than a sweep of its whole army.
		for id in sim._spatial_gather_sorted(point, range_sim):
			if not sim.entities.has(id):
				continue
			var row: Dictionary = sim.entities[id]
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
			sim._set_timed_modifier(
				row,
				"taint:%s" % String(grove.get("modifier", "")),
				[
					{"kind": "ARMOR", "value": float(grove.get("armor_mult", 0.0))},
					{"kind": "DAMAGE_MULT", "value": float(grove.get("damage_mult", 1.0))},
				],
				sim.tick_index + int(grove.get("buff_duration_ticks", 1)),
			)
	sim._active_groves = living


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
