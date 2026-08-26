extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Map seeding carved out of retail_slice_sim.gd (drawer 19): scenario map placements, capturable neutrals, capture-flag linking, castle fixtures and their garrisons.
## State stays on the sim; the sim keeps one-line delegates under the original names.



func _seed_scenario_map_placements() -> void:
	## Resolve every authored map type against the selected registries. Unknown
	## decoration remains visual-only; exact-one-domain admission is enforced by
	## sim.scenario_spawn_contract(). Source-index order fixes identity allocation.
	if sim._scenario_map_placements.is_empty() or not sim._scenario_runtime_tables_present():
		return
	var placements = sim._scenario_map_placements.duplicate(true)
	placements.sort_custom(
		func(a, b):
			var ai := int((a as Dictionary).get("source_index", -1))
			var bi := int((b as Dictionary).get("source_index", -1))
			if ai != bi:
				return ai < bi
			return String((a as Dictionary).get("type_name", "")) < String((b as Dictionary).get("type_name", ""))
	)
	# Capturable rows carry richer gameplay than the generic scenario structure
	# contract (neutral ownership, capturable/link fields, paired transfer). When
	# that lane is enabled it owns its authored source indices; generic registry
	# admission must not steal them merely because CaptureFlag/Outpost now also
	# have descriptor-backed scenario documents.
	var capturable_source_indices: Dictionary = {}
	if sim.capturable_neutrals_enabled:
		for capturable_value in sim._capturable_placements:
			if typeof(capturable_value) != TYPE_DICTIONARY:
				continue
			var capturable := capturable_value as Dictionary
			var capturable_index := int(capturable.get("source_index", -1))
			if capturable_index >= 0:
				capturable_source_indices[capturable_index] = true
	var seen_source_indices: Dictionary = {}
	for placement_value in placements:
		var placement := placement_value as Dictionary
		var source_index := int(placement.get("source_index", -1))
		if source_index < 0 or seen_source_indices.has(source_index):
			continue
		seen_source_indices[source_index] = true
		var object_id := String(placement.get("type_name", ""))
		var contract = sim.scenario_spawn_contract(object_id, "map-placement")
		var kind := String(contract.get("kind", ""))
		if kind == "":
			continue
		# Unit/prop admission still outranks a malformed cross-domain capturable
		# row at the same source index. Only the generic STRUCTURE path yields to
		# the richer capturable structure contract.
		if kind == "structure" and capturable_source_indices.has(source_index):
			continue
		var properties := placement.get("properties", {}) as Dictionary
		var team := _castle_fixture_team(String(properties.get("originalOwner", "")))
		var at := Vector2(placement.get("position", Vector2.ZERO))
		var spawned_id := -1
		match kind:
			"unit":
				spawned_id = sim.spawn_scenario_unit(object_id, team, at, "map-placement", sim._next_scenario_unit_id)
				if spawned_id > 0:
					sim._next_scenario_unit_id += 1
			"structure":
				spawned_id = sim.spawn_scenario_structure(object_id, team, at, "map-placement", sim._next_scenario_structure_id)
				if spawned_id > 0:
					sim._next_scenario_structure_id += 1
			"prop":
				spawned_id = sim.spawn_scenario_prop(object_id, at, "map-placement")
		if spawned_id <= 0:
			sim._emit_event("scenario.map_placement_refused", 0, 0, {
				"object_id": object_id, "kind": kind, "source_index": source_index,
			})
			continue
		var row: Dictionary = {}
		match kind:
			"unit": row = sim.entities[spawned_id] as Dictionary
			"structure": row = sim.structures[spawned_id] as Dictionary
			"prop": row = sim.scenario_props[spawned_id] as Dictionary
		row["yaw"] = float(placement.get("yaw", 0.0))
		row["scenario_source_index"] = source_index
		row["scenario_source_position"] = Vector3(placement.get("source_position", Vector3.ZERO))
		row["scenario_source_properties"] = properties.duplicate(true)
		sim._scenario_map_seeded_source_indices[source_index] = true
		sim._emit_event("scenario.map_placement_seeded", spawned_id if kind == "unit" else 0, spawned_id if kind == "structure" else 0, {
			"object_id": String((contract.get("document", {}) as Dictionary).get("objectId", object_id)),
			"kind": kind, "source_index": source_index, "team": team,
			"yaw": float(placement.get("yaw", 0.0)),
		})
	# SpawnBehavior children consume this normal allocator; continue immediately
	# after map-assigned CREEP unit IDs without collision.
	sim._next_dynamic_id[sim.CREEP_TEAM] = sim._next_scenario_unit_id

func _castle_fixture_team(owner: String) -> int:
	## Map a fixture's authored originalOwner ("Player_N/teamPlayer_N",
	## "PlyrCivilian/teamPlyrCivilian", …) onto a sim team. A player owner maps
	## to the roster team seated at that start index (team_start_indices:
	## team -> zero-based start index); PlyrCreeps maps to the creep team;
	## everything else — civilian/neutral owners, retail's malformed "/team",
	## and players with no roster seat on this match — maps to the
	## non-combatant civilian team, never silently to team 0.
	if owner.begins_with("PlyrCreeps"):
		return sim.CREEP_TEAM
	if owner.begins_with("Player_"):
		var digits := owner.trim_prefix("Player_").split("/", true, 1)[0]
		if digits.is_valid_int():
			var seat := int(digits) - 1
			for team_value in sim._team_descriptors.keys():
				var descriptor: Dictionary = sim._team_descriptors[team_value]
				if int(descriptor.get("start_index", -1)) == seat:
					return int(team_value)
			# Roster rows without a start_index (any lobby launch where no one
			# picked a start position): the team still spawns at the map's
			# configured seat (_ai_start_waypoint_name falls back to
			# sim._configured_team_start_indices), so the castle's authored
			# Player_N owner must resolve through the same table instead of
			# dropping to the civilian team (review 2026-08-19: the player's
			# own open gate blocked him on every injected-roster launch).
			var seat_teams: Array = sim._configured_team_start_indices.keys()
			seat_teams.sort()
			for team_value in seat_teams:
				var descriptor: Dictionary = sim._team_descriptors.get(int(team_value), {}) as Dictionary
				if descriptor.has("start_index"):
					continue
				if int(sim._configured_team_start_indices[team_value]) == seat:
					return int(team_value)
	return sim.CASTLE_CIVILIAN_TEAM


func _seed_capturable_neutrals() -> void:
	## Map-authored Inn / Outpost / SignalFire / CaptureFlag become live
	## NEUTRAL sim.structures. The flag is CAPTURABLE; LINKED_TO_FLAG buildings
	## follow the nearest flag. Visuals stay on the bound map props.
	if sim._capturable_placements.is_empty():
		return
	## Inn has no selected neutral descriptor yet. Keep that one gap explicit;
	## descriptor-backed CaptureFlag / Outpost / SignalFire must never fall back.
	if not sim._structure_armor.has("inn"):
		sim._structure_armor["inn"] = {"set_id": "NeutralInn-provisional", "damage_scalar": 1.0, "scalars": {"default": 1.0}}
	var placements = sim._capturable_placements.duplicate(true)
	placements.sort_custom(
		func(a, b): return int((a as Dictionary).get("source_index", 0)) < int((b as Dictionary).get("source_index", 0))
	)
	var seeded: Array[int] = []
	for placement_value in placements:
		var placement: Dictionary = placement_value
		if sim._scenario_map_seeded_source_indices.has(int(placement.get("source_index", -1))):
			continue
		var kind := StructureArmorContract.scenario_runtime_kind(String(placement.get("type_name", placement.get("structure_kind", ""))))
		var authored_kind := StructureArmorContract.scenario_runtime_kind(String(placement.get("structure_kind", "")))
		if kind == "" or authored_kind != kind:
			sim.configuration_error = "Capturable placement '%s' has inconsistent runtime kind '%s'" % [String(placement.get("type_name", "")), String(placement.get("structure_kind", ""))]
			return
		if not sim._structure_armor.has(kind):
			sim.configuration_error = "Capturable placement '%s' has no compiled armor contract" % String(placement.get("type_name", ""))
			return
		var structure_id = sim._next_capturable_structure_id
		sim._next_capturable_structure_id += 1
		var max_health := maxi(1, int(placement.get("maximum_health", 1)))
		sim._note_structure_table_mutation()
		var row := {
			"id": structure_id,
			"team": sim.NEUTRAL_TEAM,
			"structure_kind": kind,
			"name": String(placement.get("type_name", kind)),
			"type_name": String(placement.get("type_name", "")),
			"source_index": int(placement.get("source_index", -1)),
			"position": Vector2(placement.get("position", Vector2.ZERO)),
			"yaw": float(placement.get("yaw", 0.0)),
			"rally": Vector2(placement.get("position", Vector2.ZERO)),
			"health": max_health,
			"maximum_health": max_health,
			"construction_progress": 1.0,
			"level": 1,
			"completed_upgrades": [],
			"damage_remainders": {},
			"queue": [],
			"upgrade_queue": [],
			"capturable": bool(placement.get("capturable", false)),
			"linked_to_flag": bool(placement.get("linked_to_flag", false)),
			"unattackable": bool(placement.get("unattackable", false)),
			"presentation": "bound-map-prop",
			"linked_structure_id": 0,
		}
		if kind == "outpost":
			row["auto_deposit_amount"] = sim.OUTPOST_DEPOSIT_AMOUNT
			row["auto_deposit_interval_ms"] = sim.OUTPOST_DEPOSIT_MS
			row["auto_deposit_capture_bonus"] = sim.OUTPOST_CAPTURE_BONUS
			sim._initialize_structure_auto_deposit(row)
		sim.structures[structure_id] = row
		seeded.append(structure_id)
		sim._emit_event("structure.neutral_seeded", 0, structure_id, {
			"type_name": String(placement.get("type_name", "")),
			"kind": kind,
			"source_index": int(placement.get("source_index", -1)),
		})
	_link_capture_flags(seeded)


func _link_capture_flags(seeded_ids: Array[int]) -> void:
	## Pair each CAPTURABLE flag with the nearest LINKED_TO_FLAG building.
	## The map does not encode the link; retail places the pair together.
	var flags: Array[int] = []
	var buildings: Array[int] = []
	for structure_id in seeded_ids:
		var row: Dictionary = sim.structures.get(structure_id, {})
		if row.is_empty():
			continue
		if bool(row.get("capturable", false)):
			flags.append(structure_id)
		elif bool(row.get("linked_to_flag", false)):
			buildings.append(structure_id)
	for flag_id in flags:
		var flag: Dictionary = sim.structures[flag_id]
		var origin := Vector2(flag.get("position", Vector2.ZERO))
		var best_id := 0
		var best_distance := INF
		for building_id in buildings:
			var building: Dictionary = sim.structures[building_id]
			if int(building.get("linked_structure_id", 0)) != 0:
				continue
			var distance := origin.distance_to(Vector2(building.get("position", Vector2.ZERO)))
			if distance < best_distance:
				best_distance = distance
				best_id = building_id
		if best_id == 0:
			continue
		flag["linked_structure_id"] = best_id
		(sim.structures[best_id] as Dictionary)["linked_structure_id"] = flag_id


func _transfer_linked_capture(flag_row: Dictionary, team: int) -> void:
	var linked_id := int(flag_row.get("linked_structure_id", 0))
	if linked_id == 0 or not sim.structures.has(linked_id):
		return
	var linked: Dictionary = sim.structures[linked_id]
	linked["team"] = team
	sim._award_auto_deposit_capture(linked, team)
	sim._emit_event("structure.linked_captured", 0, linked_id, {
		"team": team,
		"flag_id": int(flag_row.get("id", 0)),
		"structure_kind": String(linked.get("structure_kind", "")),
	})


func _seed_castle_fixtures() -> void:
	## Lane L2b: map-authored castle sim.structures become live sim sim.structures,
	## following the creep-lair seeding precedent (deterministic source_index
	## order, authored health/owner, recorded provisional armor). Only the
	## sim-seeded subset arrives here — creep lairs, INERT scenery and
	## capturable flags were deferred upstream with named reasons.
	if sim._castle_fixture_placements.is_empty():
		return
	if not sim._structure_armor.has("castle_fixture"):
		# No castle map-fixture armor table is compiled into the packs yet, so
		# register a recorded neutral 1.0 provisional (the creep-lair
		# precedent) instead of falling to the unrelated 0.25 default. The
		# authored set name rides each row ("castle_fixture_armor") for the
		# lane that compiles those tables.
		sim._structure_armor["castle_fixture"] = {"set_id": "MapFixture-provisional", "damage_scalar": 1.0, "scalars": {"default": 1.0}}
	var placements = sim._castle_fixture_placements.duplicate(true)
	placements.sort_custom(
		func(a, b): return int((a as Dictionary).get("source_index", 0)) < int((b as Dictionary).get("source_index", 0))
	)
	for placement_value in placements:
		var placement: Dictionary = placement_value
		if sim._scenario_map_seeded_source_indices.has(int(placement.get("source_index", -1))):
			continue
		var structure_id = sim._next_castle_fixture_id
		sim._next_castle_fixture_id += 1
		var position := Vector2(placement.get("position", Vector2.ZERO))
		var maximum_health := maxi(1, roundi(float(placement.get("maximum_health", 1.0))))
		var health := maximum_health
		if placement.has("initial_health"):
			# objectInitialHealth is an authored PERCENT of MaxHealth: Carn Dum
			# authors 99/75/50 on seven rows, everything measured else is 100.
			health = clampi(
				maxi(1, roundi(float(maximum_health) * float(placement.get("initial_health", 100.0)) / 100.0)),
				1,
				maximum_health
			)
		sim._note_structure_table_mutation()
		var row := {
			"id": structure_id,
			"team": _castle_fixture_team(String(placement.get("owner", ""))),
			"kind": "structure",
			"structure_kind": "castle_fixture",
			"name": String(placement.get("type_name", "")),
			"castle_fixture_type": String(placement.get("type_name", "")),
			"castle_fixture_role": String(placement.get("role", "")),
			"kind_of": (placement.get("kind_of", []) as Array).duplicate(),
			"castle_fixture_owner": String(placement.get("owner", "")),
			"castle_fixture_armor": String(placement.get("armor", "")),
			"castle_fixture_kind_of": Array(placement.get("kind_of", [])).duplicate(),
			"castle_fixture_enabled": bool(placement.get("enabled", true)),
			"castle_fixture_targetable": bool(placement.get("targetable", true)),
			"source_index": int(placement.get("source_index", -1)),
			"position": position,
			"elevation": float(placement.get("elevation", 0.0)),
			"facing_radians": float(placement.get("yaw", 0.0)),
			"rally": position,
			"health": health,
			"maximum_health": maximum_health,
			# SAGE objectIndestructible, carried verbatim (Erebor authors it on
			# 501 of 609 fixture rows); _apply_structure_damage refuses it.
			"indestructible": bool(placement.get("indestructible", false)),
			"construction_progress": 1.0,
			"level": 1,
			"completed_upgrades": [],
			"upgrade_queue": [],
			"production": [],
			"queue": [],
			"damage_remainders": {},
			"income_per_payout": 0,
			# The battlefield already draws the map's bound prop for this
			# placement; the exact playable-structure document is optional for
			# presentation but mandatory for wall-defense behavior.
			"presentation": "bound-map-prop",
		}
		var type_name := String(placement.get("type_name", ""))
		var document = sim._playable_structure_runtime_document(type_name)
		if not document.is_empty():
			row["source_object_id"] = type_name
			row["object_id"] = type_name
			var gameplay := ((document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary)
			var attack = sim._structure_attack_from_combat(gameplay.get("combat", {}) as Dictionary)
			if not attack.is_empty():
				row["attack"] = attack
				row["wall_defense_status"] = "compiled"
			elif String(placement.get("role", "")) == "wall-mounted":
				if sim._document_is_wall_upgrade_slot(document):
					# Retail authors these WITHOUT a weapon: the slot gains one
					# through the player's Trebuchet/Postern/Garrison purchase
					# (GondorCastleWallCommandSet, WeaponSet PLAYER_UPGRADE) or,
					# for catapult mounts, a slaved trebuchet spawned on
					# creation. Neither purchase flow exists in this runtime
					# yet, so the slot is inert BY NAME - not a stale pack.
					row["wall_defense_status"] = "upgrade-slot-purchase-unimplemented"
					if not sim._named_wall_slot_types.has(type_name):
						sim._named_wall_slot_types[type_name] = true
						print("[RetailSliceSim] CASTLE_WALL_UPGRADE_SLOT type=%s reason=map-placed slot; Trebuchet/Postern/Garrison purchase on fixtures not implemented (named gap)" % type_name)
				else:
					# Document present but no compiled combat: the exact shape a
					# half-recooked pack takes. Loud, never inert-and-silent.
					row["wall_defense_status"] = "stale-pack-document-without-combat"
					print("[RetailSliceSim] CASTLE_WALL_DEFENSE_STALE type=%s source_index=%d reason=document-without-compiled-combat" % [type_name, int(placement.get("source_index", -1))])
			sim._attach_structure_module_contracts(row)
		elif String(placement.get("role", "")) == "wall-mounted":
			row["wall_defense_status"] = "stale-pack-missing-structure-document"
			print("[RetailSliceSim] CASTLE_WALL_DEFENSE_STALE type=%s source_index=%d reason=missing-playable-structure-document" % [type_name, int(placement.get("source_index", -1))])
		sim.structures[structure_id] = row
		var garrison := placement.get("garrison", {}) as Dictionary
		if not garrison.is_empty():
			_attach_castle_fixture_garrison(sim.structures[structure_id] as Dictionary, garrison)
		var stale_status := String(row.get("wall_defense_status", ""))
		if stale_status.begins_with("stale-pack-"):
			sim._emit_event("castle.fixture_wall_defense_stale", 0, structure_id, {
				"type_name": type_name,
				"source_index": int(placement.get("source_index", -1)),
				"reason": "missing-playable-structure-document" if stale_status == "stale-pack-missing-structure-document" else "document-without-compiled-combat",
			})
		var fixture_row: Dictionary = sim.structures[structure_id]
		var gate_block_value: Variant = placement.get("gate", null)
		if String(placement.get("role", "")) == "gate" and typeof(gate_block_value) == TYPE_DICTIONARY:
			var gate_block := gate_block_value as Dictionary
			var opened := bool(gate_block.get("openByDefault", false))
			fixture_row["gate_behavior"] = {
				"open": opened,
				"pathing_open": opened,
				"open_fraction": 1.0 if opened else 0.0,
				"reset_ticks": sim._ship_contract_delay_ticks(float(gate_block.get("resetMilliseconds", 0.0))),
				"pathing_threshold": float(gate_block.get("percentOpenForPathing", 100.0)) / 100.0,
				"repel_colliding": false,
				"close_tick": -1,
				"unsupported_semantics": [],
			}
			fixture_row["gate_geometries"] = (gate_block.get("geometries", {}) as Dictionary).duplicate(true)
			if gate_block.has("commandSet"):
				fixture_row["gate_command_set"] = String(gate_block.get("commandSet", ""))
			if gate_block.has("commandSetRows"):
				fixture_row["gate_command_rows"] = (gate_block.get("commandSetRows", []) as Array).duplicate(true)
			var ai_gate_value: Variant = gate_block.get("aiGateUpdate", null)
			if typeof(ai_gate_value) == TYPE_DICTIONARY:
				var ai_gate := ai_gate_value as Dictionary
				fixture_row["ai_gate_update"] = {"trigger_width_source": Vector2(float(ai_gate.get("triggerWidthX", 0.0)), float(ai_gate.get("triggerWidthY", 0.0)))}
			var portal_value: Variant = gate_block.get("fakePathfindPortal", null)
			if typeof(portal_value) == TYPE_DICTIONARY:
				var portal := portal_value as Dictionary
				fixture_row["fake_pathfind_portal"] = {"allow_enemies": bool(portal.get("allowEnemies", false)), "allow_non_skirmish_ai": bool(portal.get("allowNonSkirmishAIUnits", false))}
			sim.structures[structure_id] = fixture_row
			sim._sync_gate_passage(structure_id)
		sim._emit_event("castle.fixture_seeded", 0, structure_id, {
			"type_name": String(placement.get("type_name", "")),
			"role": String(placement.get("role", "")),
			"team": int(sim.structures[structure_id].get("team", -1)),
			"source_index": int(placement.get("source_index", -1)),
		})


func _attach_castle_fixture_garrison(row: Dictionary, garrison: Dictionary) -> void:
	var capacity := int(garrison.get("containMax", 0))
	if capacity <= 0:
		return
	var statuses: Array = (garrison.get("objectStatusOfContained", []) as Array).duplicate()
	var filter: Array = (garrison.get("passengerFilter", []) as Array).duplicate()
	# Compatibility for the selected v0.2.6 maps pack: its fixture schema
	# predates L4 and carries capacity/ownership/death but not these two fields.
	# Exact tower identity keeps this authored stopgap narrow and loud; the next
	# maps recook emits the same values from INI and retires this branch.
	if statuses.is_empty() or filter.is_empty():
		var tower_type := String(row.get("castle_fixture_type", ""))
		if tower_type in ["EBGarrisonableTower", "GHGarrisonableTower"]:
			statuses = ["UNSELECTABLE", "CAN_ATTACK", "ENCLOSED"]
			filter = ["ANY", "+INFANTRY", "+BANNER", "-CAVALRY", "-SUMMONED", "-WildSpiderling", "-WildSpiderlingHorde", "-COMBO_HORDE", "-IsengardSharku", "-AngmarThrallMaster"]
			row["garrison_contract_gap"] = "selected-map-pack-predates-L4-fields"
			push_warning("Castle garrison %s uses the explicit pre-L4 fixture compatibility contract; maps recook owed" % tower_type)
	var exit_values: Array = garrison.get("exitOffset", []) as Array
	var exit_offset := Vector2.ZERO
	if exit_values.size() == 3:
		exit_offset = Vector2(float(exit_values[0]), float(exit_values[1]))
	elif String(row.get("castle_fixture_type", "")) == "EBGarrisonableTower":
		# ereborbuildings.ini:5279 HordeGarrisonContain ExitOffset X:50 (pre-L4 pack)
		exit_offset = Vector2(50.0, 0.0)
		row["garrison_contract_gap"] = "selected-map-pack-predates-L4-fields"
		push_warning("Castle garrison EBGarrisonableTower exitOffset/visionRange from the explicit pre-L4 compatibility contract; maps recook owed")
	elif String(row.get("castle_fixture_type", "")) == "GHGarrisonableTower":
		# greyhavenbuildings.ini:2171 ExitOffset X:0 Y:-45 (pre-L4 pack)
		exit_offset = Vector2(0.0, -45.0)
		row["garrison_contract_gap"] = "selected-map-pack-predates-L4-fields"
		push_warning("Castle garrison GHGarrisonableTower exitOffset/visionRange from the explicit pre-L4 compatibility contract; maps recook owed")
	elif not garrison.has("exitOffset"):
		push_warning("Castle garrison %s has no authored exitOffset in the fixture row and no compatibility contract; occupants exit at the tower centre" % String(row.get("castle_fixture_type", "")))
	var kill_on_death := bool(garrison.get("killPassengersOnDeath", false))
	row["transport_capacity"] = capacity
	row["horde_transport"] = {
		"module": "HordeGarrisonContain",
		"contained_statuses": statuses,
		"passenger_filter": filter,
		"damage_ratio": float(garrison.get("damagePercentToUnits", 0.0)),
		"allow_own": bool(garrison.get("allowOwnPlayerInsideOverride", false)),
		"allow_allies": bool(garrison.get("allowAlliesInside", false)),
		"allow_enemies": bool(garrison.get("allowEnemiesInside", false)),
		"allow_neutral": bool(garrison.get("allowNeutralInside", false)),
		"exit_delay_ticks": 0,
		"kill_passengers_on_death": kill_on_death,
		"eject_passengers_on_death": not kill_on_death,
		"exit_offset_source": exit_offset,
		"tower_vision_range_source": float(garrison.get("visionRange", 600.0 if String(row.get("castle_fixture_type", "")) == "EBGarrisonableTower" else 160.0 if String(row.get("castle_fixture_type", "")) == "GHGarrisonableTower" else 0.0)),
		"unsupported_semantics": [],
		"source": "map-fixtures.garrison",
	}

