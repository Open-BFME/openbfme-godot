extends RefCounted
## Order API carved out of retail_slice_sim.gd (drawer 21): move/attack/stop/stance/formation orders, group speed caps, formation toggles and modifiers.
## State stays on the sim; the sim keeps one-line delegates under the original names.

var _sim_ref: WeakRef
var sim:
	get:
		return _sim_ref.get_ref()

func _init(owning_sim) -> void:
	_sim_ref = weakref(owning_sim)

func issue_move(ids: Array[int], destination: Vector2, ack_kind: String = "order.move", team: int = sim.PLAYER_TEAM) -> int:
	var accepted_ids: Array[int] = []
	sim.last_route_rejection = ""
	for id in ids:
		if accepted_ids.has(id) or not sim._is_commandable_for_team(id, team):
			continue
		var row: Dictionary = sim.entities[id]
		if not sim._assign_route(row, destination):
			continue
		row["target_id"] = 0
		row["target_kind"] = "battalion"
		row["attack_windup"] = 0
		row["attack_move"] = false
		sim._clear_member_targets(row)
		row["state"] = "run"
		row["order_kind"] = "move"
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		_apply_group_speed_cap(accepted_ids)
		sim._stamp_order_sequence(accepted_ids)
		sim.last_route_rejection = ""
		sim._emit_event(ack_kind, accepted_ids[0], 0, sim._voice_event_identity(accepted_ids[0]))
	return accepted_ids.size()


func _apply_group_speed_cap(accepted_ids: Array[int]) -> void:
	## WaitForFormation (locomotor.ini:713, on the melee / charge-melee / ranged
	## horde locomotors): "When moving into formations, these guys stop & wait for
	## others." Retail's observable consequence is that a mixed selection advances
	## at the pace of its slowest member and arrives roughly together, rather than
	## the cavalry landing a lap ahead of the pikes.
	##
	## The cap is the minimum AUTHORED speed across the group, so per-row stance,
	## formation, and ability multipliers still apply on top of it in _step_route.
	## A single-battalion order caps at its own speed, i.e. no change.
	##
	## Deterministic: min over the accepted set is order-independent, and the
	## accepted set is already built in caller-supplied id order.
	## Per-row WaitForFormation, not the global sim.retail_formation_movement flag.
	## Pin fixtures do not author the field, so they stay absent-unless-set.
	## A mixed group that includes at least one waiter coheres at the slowest
	## authored speed; only waiters receive the key.
	var waiters: Array[int] = []
	if sim.retail_formation_movement:
		waiters = accepted_ids.duplicate()
	else:
		for id in accepted_ids:
			if bool((sim.entities[id] as Dictionary).get("wait_for_formation", false)):
				waiters.append(id)
	if waiters.is_empty():
		return
	var slowest := INF
	for id in accepted_ids:
		var row: Dictionary = sim.entities[id]
		slowest = minf(slowest, maxf(0.0, float(row.get("speed", 0.0))))
	if slowest == INF:
		return
	for id in waiters:
		(sim.entities[id] as Dictionary)["group_speed_cap"] = slowest


func issue_attack(ids: Array[int], target_id: int, team: int = sim.PLAYER_TEAM) -> int:
	var target_kind = "battalion" if sim.entities.has(target_id) else ("structure" if sim.structures.has(target_id) else "")
	if target_kind == "":
		return 0
	var target: Dictionary = sim.entities[target_id] if target_kind == "battalion" else sim.structures[target_id]
	if int(target["health"]) <= 0:
		return 0
	var accepted_ids: Array[int] = []
	sim.last_route_rejection = ""
	for id in ids:
		if accepted_ids.has(id) or not sim._is_commandable_for_team(id, team):
			continue
		var row: Dictionary = sim.entities[id]
		if bool(row.get("noncombatant", false)):
			continue
		if int(row["team"]) == int(target["team"]):
			continue
		if target_kind == "battalion" and not sim._can_engage_battalion(row, target):
			continue
		if not sim._assign_target_route(row, Vector2(target["position"])):
			continue
		row["target_id"] = target_id
		row["target_kind"] = target_kind
		row["attack_windup"] = 0
		row["attack_move"] = false
		sim._clear_member_targets(row)
		row["state"] = "run"
		row["order_kind"] = "attack"
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		# A group attack coheres exactly like a group move; refreshing here also
		# stops a cap left over from an earlier escort order throttling the
		# charge.
		_apply_group_speed_cap(accepted_ids)
		sim._stamp_order_sequence(accepted_ids)
		sim.last_route_rejection = ""
		var ack = sim._voice_event_identity(accepted_ids[0])
		ack["target_kind"] = target_kind
		sim._emit_event("voice.attack", accepted_ids[0], target_id, ack)
		sim._emit_music("battle")
	return accepted_ids.size()


func issue_attack_move(ids: Array[int], destination: Vector2, team: int = sim.PLAYER_TEAM) -> int:
	# Retail answers an attack-move with an attack-class acknowledgement, not
	# the plain move line; the sim keeps the order kind distinct so the audio
	# layer picks the attack ack without guessing.
	var accepted := issue_move(ids, destination, "voice.attack", team)
	if accepted <= 0:
		return 0
	for id in ids:
		if not sim._is_commandable_for_team(id, team):
			continue
		var row: Dictionary = sim.entities[id]
		if Vector2(row.get("destination", row["position"])).is_equal_approx(destination):
			row["attack_move"] = true
			row["attack_move_destination"] = destination
			row["order_kind"] = "attack_move"
	return accepted


func issue_stop(ids: Array[int], team: int = sim.PLAYER_TEAM) -> int:
	var accepted_ids: Array[int] = []
	for id in ids:
		if accepted_ids.has(id) or not sim._is_commandable_for_team(id, team):
			continue
		var row: Dictionary = sim.entities[id]
		row["target_id"] = 0
		row["target_kind"] = "battalion"
		row["attack_windup"] = 0
		row["attack_move"] = false
		sim._clear_member_attack_schedule(row)
		sim._clear_member_targets(row)
		sim._clear_pending_route(row, true)
		row["state"] = "idle"
		row["order_kind"] = ""
		sim._rearm_mood_idle_cadence(row)
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		sim._stamp_order_sequence(accepted_ids)
		sim._emit_event("order.stop", accepted_ids[0], 0)
	return accepted_ids.size()


func issue_toggle_stance(ids: Array[int], team: int = sim.PLAYER_TEAM) -> int:
	var accepted_ids: Array[int] = []
	for id in ids:
		if accepted_ids.has(id) or not sim._is_commandable_for_team(id, team):
			continue
		var row: Dictionary = sim.entities[id]
		var index = sim.STANCE_ORDER.find(String(row.get("stance", "Battle")))
		row["stance"] = sim.STANCE_ORDER[posmod(index + 1, sim.STANCE_ORDER.size())]
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		sim._stamp_order_sequence(accepted_ids)
		sim._emit_event("order.stance", accepted_ids[0], 0, {"stance": String((sim.entities[accepted_ids[0]] as Dictionary)["stance"])})
	return accepted_ids.size()


func issue_set_stance(ids: Array[int], stance: String, team: int = sim.PLAYER_TEAM) -> int:
	if not sim.STANCE_ORDER.has(stance):
		return 0
	var accepted_ids: Array[int] = []
	for id in ids:
		if accepted_ids.has(id) or not sim._is_commandable_for_team(id, team):
			continue
		(sim.entities[id] as Dictionary)["stance"] = stance
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		sim._stamp_order_sequence(accepted_ids)
		sim._emit_event("order.stance", accepted_ids[0], 0, {"stance": stance})
	return accepted_ids.size()


func _authored_formation_toggle(document: Dictionary) -> Dictionary:
	## The unit's own HORDE_TOGGLE_FORMATION button, read off the compiled
	## selection surface so the sim gate and the palantir gate answer from the
	## SAME authored data (commandbutton.ini / commandset.ini).
	##
	var compiled_toggle = sim.PlayableUnitAdapter.formation_toggle_contract(document)
	var modifier_lists: Array = compiled_toggle.get("modifierLists", []) as Array
	var effects: Array = []
	var modifier_ids: Array[String] = []
	var unsupported_receipts: Array[String] = []
	for list_value in modifier_lists:
		var modifier_list := list_value as Dictionary
		var modifier_id := String(modifier_list.get("id", ""))
		if modifier_id != "":
			modifier_ids.append(modifier_id)
		for modifier_value in modifier_list.get("modifiers", []) as Array:
			var modifier := modifier_value as Dictionary
			if String(modifier.get("runtimeSupport", "receipt-only")) == "supported":
				effects.append(modifier.duplicate(true))
			else:
				unsupported_receipts.append(
					"formation_modifier_unsupported:%s:%s" % [
						modifier_id, String(modifier.get("kind", "UNKNOWN"))
					]
				)
	for selection_value in sim.PlayableUnitAdapter.selection_commands(document):
		var selection := selection_value as Dictionary
		for kind_value in selection.get("commandKinds", []) as Array:
			if String(kind_value).strip_edges().to_upper() != "HORDE_TOGGLE_FORMATION":
				continue
			return {
				"command_id": String(selection.get("commandId", "")),
				"command_set_id": String(selection.get("commandSetId", "")),
				"source_ini": String(selection.get("sourceIni", "")),
				"modifier": {
					"id": modifier_ids[0] if modifier_ids.size() == 1 else "+".join(modifier_ids),
					"modifier_ids": modifier_ids,
					"category": "FORMATION",
					"modifiers": effects,
					"unsupported_receipts": unsupported_receipts,
				},
			}
	return {}


func horde_formation_toggle(row: Dictionary) -> Dictionary:
	## The unit's authored HORDE_TOGGLE_FORMATION button, or {} when its command
	## set carries none.
	##
	## RETAIL ORACLE: a formation toggle is authored per command set, not given
	## to every unit. data/ini/commandbutton.ini declares 18 live
	## HORDE_TOGGLE_FORMATION buttons (Command_TowerGuardPorcupineFormation
	## :664, Command_ToggleFormationGondorFighter :196, Rohan :676, Isengard
	## pikeman :690, Mithlond :702, Dwarven :714, Wild :726, Mordor Easterling
	## :738, Angmar :9678, ...), each with
	## `Options = TOGGLE_IMAGE_ON_FORMATION OK_FOR_MULTI_SELECT` and a TWO-image
	## ButtonImage; only 13 command sets reference one. A Gondor archer horde
	## has no such button and cannot be put into a formation at all.
	return row.get("formation_toggle", {}) as Dictionary


func _formation_order_admitted(row: Dictionary) -> bool:
	if not horde_formation_toggle(row).is_empty():
		return true
	# LEGACY FIXTURE CARVE-OUT, deliberate and narrow: synthetic runner rows
	# that predate the compiled command surface carry no module contracts and
	# no authored toggle, and the determinism pins script a formation order on
	# exactly such a row. Descriptor-backed rows -- everything a real pack
	# produces -- are held to the authored data.
	return not row.has("module_contracts") and not row.has("command_surface")


func issue_toggle_formation(ids: Array[int], team: int = sim.PLAYER_TEAM) -> int:
	var accepted_ids: Array[int] = []
	for id in ids:
		if accepted_ids.has(id) or not sim._is_commandable_for_team(id, team):
			continue
		var row: Dictionary = sim.entities[id]
		if bool(row.get("is_builder", false)):
			continue
		if not _formation_order_admitted(row):
			continue
		var index = sim.FORMATION_ORDER.find(String(row.get("formation_mode", "Line")))
		var next_mode = sim.FORMATION_ORDER[posmod(index + 1, sim.FORMATION_ORDER.size())]
		row["formation_mode"] = next_mode
		_apply_formation_mode(row)
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		sim._stamp_order_sequence(accepted_ids)
		sim._emit_event(
			"order.formation",
			accepted_ids[0],
			0,
			{"formation": String((sim.entities[accepted_ids[0]] as Dictionary)["formation_mode"])}
		)
	return accepted_ids.size()


func issue_set_formation(ids: Array[int], formation: String, team: int = sim.PLAYER_TEAM) -> int:
	if not sim.FORMATION_ORDER.has(formation):
		return 0
	var accepted_ids: Array[int] = []
	for id in ids:
		if accepted_ids.has(id) or not sim._is_commandable_for_team(id, team):
			continue
		var row: Dictionary = sim.entities[id]
		if bool(row.get("is_builder", false)):
			continue
		if not _formation_order_admitted(row):
			continue
		row["formation_mode"] = formation
		_apply_formation_mode(row)
		accepted_ids.append(id)
	if not accepted_ids.is_empty():
		sim._stamp_order_sequence(accepted_ids)
		sim._emit_event("order.formation", accepted_ids[0], 0, {"formation": formation})
	return accepted_ids.size()




func _apply_formation_attribute_modifier(row: Dictionary) -> void:
	## RETAIL ORACLE: the toggle swaps the horde to its authored
	## `AlternateFormation` ChildObject, whose HordeContain carries
	## `AttributeModifiers = <ModifierList>` and `IsPorcupineFormation = Yes`
	## (object/evilfaction/hordes/isengard/isengardhordes.ini:531-548, and the
	## file's own note: "for alternate formations, all info outside of the
	## Contain Behavior module is ignored. Any modifications need to be done via
	## the Attribute Modifiers in the contain module").
	##
	## That ModifierList is attributemodifier.ini:756-764
	## `ModifierList GondorTowerShieldGuardHordePorcupine / Category = FORMATION
	## / Modifier = CRUSHED_DECELERATE 1000% / Duration = 0` -- and the same
	## shape at :766-806 for Isengard, Mithlond, Dwarven, Wild and Mordor.
	## The SPEED / ARMOR / DAMAGE_ADD / CRUSHABLE_LEVEL rows in those blocks are
	## COMMENTED OUT in retail and are deliberately NOT applied here.
	##
	## Duration = 0 is "forever" (the file says so at :764), so the entry never
	## expires; it is erased when the horde leaves the formation.
	var toggle := horde_formation_toggle(row)
	var table: Dictionary = row.get("timed_modifiers", {}) as Dictionary
	var was_active: bool = table.has(sim.FORMATION_MODIFIER_KEY)
	var modifier := toggle.get("modifier", {}) as Dictionary
	var effects: Array = modifier.get("modifiers", []) as Array
	var active: bool = (
		String(row.get("formation_mode", "Line")) != "Line" and not effects.is_empty()
	)
	if not active and not was_active:
		# Nothing authored and nothing to drop: leave the row byte-identical.
		# Rows carry an empty `timed_modifiers` dict by construction, so an
		# unconditional erase-if-empty here would change every hashed row.
		return
	table.erase(sim.FORMATION_MODIFIER_KEY)
	if active:
		table[sim.FORMATION_MODIFIER_KEY] = {
			"modifiers": effects.duplicate(true),
			"expires_tick": -1,
			"persistent": true,
			"category": String(modifier.get("category", "FORMATION")),
			"modifier_id": String(modifier.get("id", "")),
			"modifier_ids": Array(modifier.get("modifier_ids", [])).duplicate(),
			"unsupported_modifier_receipts": Array(
				modifier.get("unsupported_receipts", [])
			).duplicate(),
			"source_id": int(row.get("id", 0)),
		}
	row["timed_modifiers"] = table


func _apply_formation_mode(row: Dictionary) -> void:
	_apply_formation_attribute_modifier(row)
	var base: Array = row.get("formation_positions_base", row.get("formation_positions", [])) as Array
	if base.is_empty():
		return
	var mode := String(row.get("formation_mode", "Line"))
	var isotropic = float(sim.FORMATION_SPACING.get(mode, 1.0))
	# x = lateral, z = depth (see sim.FORMATION_SPACING_RETAIL).
	var lateral_scale = isotropic
	var depth_scale = isotropic
	if sim.retail_formation_movement:
		var authored: Dictionary = sim.FORMATION_SPACING_RETAIL.get(mode, {}) as Dictionary
		lateral_scale = float(authored.get("lateral", 1.0))
		depth_scale = float(authored.get("depth", 1.0))
	var scaled: Array = []
	for slot_value in base:
		if typeof(slot_value) != TYPE_VECTOR3:
			scaled.append(Vector3.ZERO)
			continue
		var slot: Vector3 = slot_value
		scaled.append(Vector3(slot.x * lateral_scale, slot.y, slot.z * depth_scale))
	row["formation_positions"] = scaled


