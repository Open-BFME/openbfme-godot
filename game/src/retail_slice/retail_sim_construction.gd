extends RefCounted
## Construction and sell carved out of retail_slice_sim.gd (drawer 20): structure sell/rally, command slots, construct validation and placement, per-tick construction stepping.
## State stays on the sim; the sim keeps one-line delegates under the original names.

var _sim_ref: WeakRef
var sim:
	get:
		return _sim_ref.get_ref()

func _init(owning_sim) -> void:
	_sim_ref = weakref(owning_sim)

func structure_sell_command(structure_id: int) -> Dictionary:
	## Compiled Command_Sell row for this structure, or {} if the mounted docs
	## do not author a sell slot. Command_Sell is slot 6 of every Men production
	## set and the whole of SellableCommandSet (commandset.ini:5771).
	if not sim.structures.has(structure_id):
		return {}
	var building: Dictionary = sim.structures[structure_id]
	if int(building.get("health", 0)) <= 0:
		return {}
	# farm.ini:34 authors CommandSet = SellableCommandSet on the object with
	# no under-construction override. SAGE exposes SELL for the building's
	# whole life; refund is still SellPercentage of the already-paid cost.
	var slot := _compiled_sell_slot_for(building)
	if slot.is_empty():
		return {}
	var kind := String(building.get("structure_kind", ""))
	var team := int(building.get("team", -1))
	var cost = int((sim.structure_build_rules_for_team(team).get(kind, {}) as Dictionary).get("cost", 0))
	if cost <= 0:
		cost = int((sim._expansion_build_rules.get(kind, {}) as Dictionary).get("cost", 0))
	# gamedata.ini:8973 SellPercentage = 50%.
	var refund := int(cost / 2)
	return {
		"command_id": String(slot.get("commandId", "Command_Sell")),
		"slot": int(slot.get("slot", 6)),
		"refund": refund,
	}


func sell_structure(team: int, structure_id: int) -> Dictionary:
	## Retail SELL (commandbutton.ini:3554): raze the building and refund
	## SellPercentage of its authored build cost. Presentation follows the
	## ordinary health=0 / structure.destroyed path.
	if not sim.base_loop_enabled or sim.winner != -1:
		return {"ok": false, "reason": "match-unavailable"}
	if not sim.structures.has(structure_id):
		return {"ok": false, "reason": "unknown-structure"}
	var building: Dictionary = sim.structures[structure_id]
	if int(building.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(building.get("health", 0)) <= 0:
		return {"ok": false, "reason": "structure-unavailable"}
	var sell: Dictionary = structure_sell_command(structure_id)
	if sell.is_empty():
		return {"ok": false, "reason": "no-sell-command"}
	var refund := int(sell.get("refund", 0))
	sim.team_resources[team] = sim.resources_for_team(team) + refund
	building["health"] = 0
	building["queue"] = []
	building["upgrade_queue"] = []
	# Detach the porter so a later construct does not treat this husk as a
	# cancellable site (that path refunds the full build cost).
	var builder_id := int(building.get("builder_id", 0))
	if builder_id != 0 and sim.entities.has(builder_id):
		var builder: Dictionary = sim.entities[builder_id]
		if int(builder.get("construction_id", 0)) == structure_id:
			builder["construction_id"] = 0
			if String(builder.get("order_kind", "")) == "construct":
				builder["order_kind"] = ""
			if String(builder.get("state", "")) == "construct":
				builder["state"] = "idle"
	building["builder_id"] = 0
	_clear_expansion_pad_occupant(structure_id)
	var structure_kind := String(building.get("structure_kind", ""))
	sim._emit_event("structure.sold", 0, structure_id, {
		"team": team,
		"refund": refund,
		"structure_kind": structure_kind,
	})
	sim._emit_event("structure.destroyed", 0, structure_id, {
		"reason": "sold",
		"structure_kind": structure_kind,
		"team": team,
	})
	return {"ok": true, "refund": refund, "structure_id": structure_id}


func structure_command_slot(structure_id: int, command_id: String) -> int:
	## Authored palantir slot for a command on this building's current compiled
	## command set, or 0 when the docs do not place it.
	if command_id == "" or not sim.structures.has(structure_id):
		return 0
	for slot_value in _compiled_command_slots_for(sim.structures[structure_id]):
		if typeof(slot_value) != TYPE_DICTIONARY:
			continue
		var slot: Dictionary = slot_value
		if String(slot.get("commandId", "")) == command_id:
			return int(slot.get("slot", 0))
	return 0


func _compiled_sell_slot_for(building: Dictionary) -> Dictionary:
	for slot_value in _compiled_command_slots_for(building):
		if typeof(slot_value) != TYPE_DICTIONARY:
			continue
		var slot: Dictionary = slot_value
		if String(slot.get("commandId", "")) == "Command_Sell":
			return slot
	return {}


func _compiled_command_slots_for(building: Dictionary) -> Array:
	# Scenario sim.structures carry the exact selected-pack command-set projection on
	# the instance. It stays outside the faction structure registry, but ownership
	# transitions still expose the authored defected-lair command surface.
	var scenario_sets := building.get("scenario_trained_command_sets", []) as Array
	if not scenario_sets.is_empty():
		var active_id := String(building.get("command_set_id", building.get("default_command_set_id", "")))
		for set_value in scenario_sets:
			if typeof(set_value) != TYPE_DICTIONARY:
				continue
			var command_set := set_value as Dictionary
			if String(command_set.get("id", "")) == active_id:
				return command_set.get("slots", []) as Array
		return []
	var candidates: Array[String] = []
	var stamped := String(building.get("source_object_id", ""))
	if stamped != "":
		candidates.append(stamped)
	var kind := String(building.get("structure_kind", ""))
	var aliases: Variant = sim.structure_source_object_ids_for_team(int(building.get("team", -1))).get(kind, [])
	if typeof(aliases) == TYPE_ARRAY:
		for alias_value in aliases as Array:
			var alias_id := String(alias_value)
			if alias_id != "" and not candidates.has(alias_id):
				candidates.append(alias_id)
	elif typeof(aliases) in [TYPE_STRING, TYPE_STRING_NAME]:
		var alias_id := String(aliases)
		if alias_id != "" and not candidates.has(alias_id):
			candidates.append(alias_id)
	var db = sim._content_db_ref()
	if db == null or not db.has_method("get_playable_structure_runtime"):
		return []
	for object_id in candidates:
		var document: Dictionary = db.get_playable_structure_runtime(object_id)
		if document.is_empty():
			continue
		var sets: Array = (
			((document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary)
			.get("trainedCommandSets", [])
		) as Array
		var slots := _direct_or_first_command_set_slots(sets)
		if not slots.is_empty():
			return slots
	return []


func _direct_or_first_command_set_slots(sets: Array) -> Array:
	for set_value in sets:
		if typeof(set_value) != TYPE_DICTIONARY:
			continue
		var row: Dictionary = set_value
		if String(row.get("kind", "")) == "direct":
			return row.get("slots", []) as Array
	if sets.is_empty() or typeof(sets[0]) != TYPE_DICTIONARY:
		return []
	return (sets[0] as Dictionary).get("slots", []) as Array


func _clear_expansion_pad_occupant(structure_id: int) -> void:
	for fortress_id_value in sim.expansion_pads.keys():
		var pads: Array = sim.expansion_pads[fortress_id_value] as Array
		for pad_value in pads:
			if typeof(pad_value) != TYPE_DICTIONARY:
				continue
			var pad: Dictionary = pad_value
			if int(pad.get("expansion_structure_id", 0)) == structure_id:
				pad["expansion_structure_id"] = 0


func issue_construct(ids: Array[int], structure_kind: String, position: Vector2, dry_run: bool = false, team: int = sim.PLAYER_TEAM) -> Dictionary:
	return _issue_construct_for_team(team, ids, structure_kind, position, dry_run)


# Approximate footprint radii in local units (world scale ~0.0265/source unit).
# Placement is legal when the two footprints plus a small working margin do
# not overlap — the previous flat 7.0-unit exclusion wasted most of the base.
const STRUCTURE_PLACEMENT_RADII := {
	"fortress": 4.0,
	"stable": 2.6,
	"barracks": 2.4,
	"archery_range": 2.4,
	"workshop": 2.4,
	"farm": 2.2,
}
const PLACEMENT_CLEARANCE_MARGIN := 0.4
const MAX_STRUCTURE_PLACEMENT_RADIUS := 4.0


func _structure_placement_radius(structure_kind: String) -> float:
	return float(STRUCTURE_PLACEMENT_RADII.get(structure_kind, 2.4))


func _authored_structure_placement_radius(team: int, structure_kind: String) -> float:
	var sources: Variant = sim.structure_source_object_ids_for_team(team).get(structure_kind, [])
	var source_object_id := ""
	if typeof(sources) == TYPE_ARRAY and not (sources as Array).is_empty():
		source_object_id = String((sources as Array)[0])
	elif typeof(sources) in [TYPE_STRING, TYPE_STRING_NAME]:
		source_object_id = String(sources)
	if source_object_id != "":
		var authored_radius = sim._structure_footprint_radius({
			"source_object_id": source_object_id,
			"structure_kind": structure_kind,
		})
		if authored_radius > 0.0:
			return authored_radius
	return _structure_placement_radius(structure_kind)


func _authored_site_foundation_fixture_contains(fixture: Dictionary, site: Vector2) -> bool:
	# An authored site may coincide with its own foundation/build-plot marker,
	# but never gets a blanket exemption from the surrounding keep. Both the
	# fixture identity and its actual compiled footprint must prove occupancy.
	var role := String(fixture.get("castle_fixture_role", "")).to_lower()
	var fixture_type := String(fixture.get("castle_fixture_type", "")).to_lower()
	var is_foundation := (
		role.contains("foundation")
		or role.contains("build-plot")
		or role.contains("build_plot")
		or fixture_type.contains("foundation")
		or fixture_type.contains("buildplot")
		or fixture_type.contains("build_plot")
	)
	if not is_foundation:
		return false
	var footprint = sim._structure_footprint_radius(fixture)
	return footprint > 0.0 and Vector2(fixture.get("position", Vector2.ZERO)).distance_to(site) <= footprint


func _issue_construct_for_team(
	team: int,
	ids: Array[int],
	structure_kind: String,
	position: Vector2,
	dry_run: bool = false,
	authored_castle_site: bool = false
) -> Dictionary:
	if not sim.base_loop_enabled or sim.winner != -1:
		return {"ok": false, "reason": "match-unavailable"}
	if team != sim.PLAYER_TEAM and team != sim.ENEMY_TEAM:
		return {"ok": false, "reason": "invalid-team"}
	# The constructing team's OWN faction tables (identical to the globals in
	# the default single-manifest match; a cross-faction guest builds its own
	# faction's sim.structures at its own costs).
	var team_structure_build_rules = sim.structure_build_rules_for_team(team)
	var team_structure_max_health = sim.structure_max_health_for_team(team)
	if not team_structure_build_rules.has(structure_kind):
		return {"ok": false, "reason": "unsupported-structure"}
	var permission = sim.building_permission_for_kind(team, structure_kind)
	if not bool(permission.get("known", false)):
		return {
			"ok": false,
			"reason": "building-permission-identity-unresolved",
			"detail": String(permission.get("reason", "")),
		}
	if not bool(permission.get("allowed", false)):
		return {
			"ok": false,
			"reason": "building-disallowed",
			"object_type": String(permission.get("object_type", "")),
		}
	# BFME1 build-plots-only: construction is restricted to designated empty
	# plots. The click must land on a free plot; the build then snaps to the
	# plot's center and skips the freeform geometry checks (plot positions are
	# predetermined and valid). Occupancy is claimed after the site is created.
	var build_plot_index := -1
	if sim.build_plots_only:
		build_plot_index = sim._free_build_plot_index_near(team, position)
		if build_plot_index < 0:
			return {"ok": false, "reason": "build-plots-only: pick an empty plot"}
		position = Vector2((sim.build_plots[team] as Array)[build_plot_index].get("position", position))
	else:
		if sim.playable_outline.size() >= 3 and not Geometry2D.is_point_in_polygon(position, sim.playable_outline):
			return {"ok": false, "reason": "outside-playable-area"}
		var new_radius := (
			_authored_structure_placement_radius(team, structure_kind)
			if authored_castle_site
			else _structure_placement_radius(structure_kind)
		)
		# The spatial query is exact: no existing footprint farther away than the
		# two maximum authored radii plus the authored clearance can overlap this
		# site. This matters on castle maps, whose hundreds of wall fixtures made
		# every fallback candidate repeat a full structure-table scan.
		var gather_radius := new_radius + MAX_STRUCTURE_PLACEMENT_RADIUS + PLACEMENT_CLEARANCE_MARGIN
		var exempted_foundation_id := 0
		for existing_id in sim._structure_ids_within_gather_radius(position, gather_radius):
			var existing_row: Dictionary = sim.structures[existing_id] as Dictionary
			# Exempt at most the one foundation marker whose own footprint contains
			# the authored point. Walls, gates, towers, other foundations, and every
			# live/dynamic structure retain normal clearance.
			if (
				authored_castle_site
				and exempted_foundation_id == 0
				and existing_id >= sim.CASTLE_FIXTURE_FIRST_ID
				and _authored_site_foundation_fixture_contains(existing_row, position)
			):
				exempted_foundation_id = existing_id
				continue
			var existing_position := Vector2(existing_row.get("position", Vector2.ZERO))
			var existing_radius := _structure_placement_radius(String(existing_row.get("structure_kind", "")))
			var clearance_margin := PLACEMENT_CLEARANCE_MARGIN
			if existing_id >= sim.CASTLE_FIXTURE_FIRST_ID:
				var fixture_radius = sim._structure_footprint_radius(existing_row)
				if fixture_radius > 0.0:
					existing_radius = fixture_radius
				# Imported fixtures have exact authored footprints and no builder
				# working apron. The margin remains for live/dynamic sim.structures.
				clearance_margin = 0.0
			var clearance := new_radius + existing_radius + clearance_margin
			if existing_position.distance_to(position) < clearance:
				return {
					"ok": false,
					"reason": "site-obstructed",
					"obstruction_id": existing_id,
					"obstruction_type": String(existing_row.get("castle_fixture_type", existing_row.get("structure_kind", ""))),
					"obstruction_role": String(existing_row.get("castle_fixture_role", "")),
					"obstruction_distance": existing_position.distance_to(position),
					"required_clearance": clearance,
				}
	var builder_id := 0
	for value in ids:
		var id := int(value)
		if not sim.entities.has(id):
			continue
		var row: Dictionary = sim.entities[id]
		if int(row.get("team", -1)) != team or int(row.get("health", 0)) <= 0 or not bool(row.get("is_builder", false)):
			continue
		builder_id = id
		break
	if builder_id == 0:
		return {"ok": false, "reason": "builder-required"}
	var build_rule: Dictionary = team_structure_build_rules[structure_kind]
	var cost := int(build_rule["cost"])
	if sim.resources_for_team(team) < cost:
		return {"ok": false, "reason": "insufficient-resources", "cost": cost}
	if dry_run:
		return {"ok": true, "reason": "", "dry_run": true, "cost": cost}
	var structure_id = sim._next_dynamic_structure_id
	sim._next_dynamic_structure_id += 1
	var maximum_health := int(team_structure_max_health[structure_kind])
	var production: Array[String] = []
	var construct_production_order = sim.production_unit_order_for_team(team)
	var construct_production_rules = sim.unit_production_rules_for_team(team)
	var construct_scope = sim.production_scope_for_team(team)
	for unit_type in construct_production_order:
		if not construct_scope.is_empty() and not construct_scope.has(String(unit_type)):
			continue
		var production_rule: Dictionary = construct_production_rules.get(unit_type, {}) as Dictionary
		var producer_kinds_for_rule: Array = production_rule.get("producer_kinds", [String(production_rule.get("producer_kind", ""))])
		if producer_kinds_for_rule.has(structure_kind):
			production.append(unit_type)
	var build_ticks = maxi(1, roundi(float(build_rule["seconds"]) / sim.TICK_SECONDS))
	sim._note_structure_table_mutation()
	sim.structures[structure_id] = {
		"id": structure_id,
		"team": team,
		"kind": "structure",
		"structure_kind": structure_kind,
		"name": structure_kind.replace("_", " ").capitalize(),
		"position": position,
		"rally": position + Vector2(4.0, 0.0),
		"health": maximum_health,
		"maximum_health": maximum_health,
		"construction_progress": 0.0,
		"level": 1,
		"completed_upgrades": [],
		"upgrade_queue": [],
		"construction_build_ticks": build_ticks,
		"construction_elapsed_ticks": 0,
		"builder_id": builder_id,
		"production": production,
		"queue": [],
		"damage_remainders": {},
		"income_per_payout": int(sim._rules.get("farm_income", 25)) if structure_kind == "farm" else 0,
	}
	# A building the player RAISES is the same retail object as the one the map
	# seeds (_seed_structures) or a flag unpacks (unpack_base): both of those
	# stamp the faction's authored source id and this path did not, so a
	# constructed structure came up with no retail identity at all -- which is
	# what left a built fortress unable to unpack its castle and therefore
	# showing an empty command wheel.
	#
	# Snapshot-inert (state_signature carries no source id), so the
	# cross-platform pin is untouched. It is NOT inert in general -- the id also
	# feeds sim._structure_footprint_radius and object-id script queries -- but the
	# footprint half is measured to be a no-op on the mounted packs; see the
	# note in _seed_structures.
	var constructed_sources: Variant = sim.structure_source_object_ids_for_team(team).get(structure_kind, [])
	if typeof(constructed_sources) == TYPE_ARRAY and not (constructed_sources as Array).is_empty():
		sim.structures[structure_id]["source_object_id"] = String((constructed_sources as Array)[0])
	elif typeof(constructed_sources) in [TYPE_STRING, TYPE_STRING_NAME]:
		sim.structures[structure_id]["source_object_id"] = String(constructed_sources)
	sim._stamp_refund_die_creation_cost(sim.structures[structure_id] as Dictionary, cost)
	sim._mark_ring_delivery_structure(sim.structures[structure_id] as Dictionary)
	if bool(build_rule.get("highlander_body", false)):
		sim.structures[structure_id]["highlander_body"] = true
	sim.team_resources[team] = sim.resources_for_team(team) - cost
	var builder: Dictionary = sim.entities[builder_id]
	var previous_site_id := int(builder.get("construction_id", 0))
	if previous_site_id != 0 and sim.structures.has(previous_site_id):
		# Redirecting a busy builder cancels its unfinished site with a full
		# refund; otherwise the site would linger as unfinishable scaffolding.
		var previous_site: Dictionary = sim.structures[previous_site_id]
		if float(previous_site.get("construction_progress", 1.0)) < 1.0:
			var previous_team := int(previous_site.get("team", team))
			sim.team_resources[previous_team] = sim.resources_for_team(previous_team) + int(sim.structure_build_rules_for_team(previous_team).get(String(previous_site.get("structure_kind", "")), {}).get("cost", 0))
			sim.structures.erase(previous_site_id)
			sim._note_structure_table_mutation()
			sim._emit_event("construction.cancelled", builder_id, previous_site_id, {"team": previous_team})
	builder["construction_id"] = structure_id
	builder["order_kind"] = "construct"
	builder["target_id"] = 0
	sim._clear_member_targets(builder)
	if not sim._assign_route(builder, position):
		sim.structures.erase(structure_id)
		sim._note_structure_table_mutation()
		sim.team_resources[team] = sim.resources_for_team(team) + cost
		builder["construction_id"] = 0
		return {"ok": false, "reason": sim.last_route_rejection if sim.last_route_rejection != "" else "route-rejected"}
	sim._apply_structure_inherit_upgrades(sim.structures[structure_id] as Dictionary)
	sim._initialize_structure_auto_deposit(sim.structures[structure_id] as Dictionary)
	sim._unpack_castle_behavior_for_structure(structure_id)
	if sim.build_plots_only and build_plot_index >= 0:
		(sim.build_plots[team] as Array)[build_plot_index]["occupant_structure_id"] = structure_id
	sim._emit_event("construction.started", builder_id, structure_id, {"team": team, "structure_kind": structure_kind, "cost": cost, "build_ticks": build_ticks, "object_id": String(builder.get("object_id", ""))})
	return {"ok": true, "builder_id": builder_id, "structure_id": structure_id, "cost": cost, "build_ticks": build_ticks}


# --- Dev playtest cheats -----------------------------------------------------
# Direct state mutation for the dev HUD (OPENBFME_DEV_HUD). Never routed
# through the lockstep command codec — the presentation layer blocks these in
# multiplayer, where a one-sided mutation would desync the peers.


func debug_finish_team_work(team: int) -> Dictionary:
	## Fast-forwards every in-progress job the team owns: construction sites,
	## production queues, structure/battalion upgrade queues, spellbook power
	## cooldowns, and hero ability cooldowns. Jobs are only rescheduled to
	## complete now — the REAL step paths still run, so every authored side
	## effect (pad seeding, events, upgrade effects) fires normally.
	var constructions := 0
	var jobs := 0
	for structure_id in sim.structure_ids():
		var building: Dictionary = sim.structures[structure_id]
		if int(building.get("team", -1)) != team:
			continue
		if float(building.get("construction_progress", 1.0)) < 1.0:
			building["construction_elapsed_ticks"] = maxi(0, int(building.get("construction_build_ticks", 1)) - 1)
			constructions += 1
		for item_value in building.get("queue", []) as Array:
			(item_value as Dictionary)["complete_tick"] = sim.tick_index
			jobs += 1
		for item_value in building.get("upgrade_queue", []) as Array:
			(item_value as Dictionary)["complete_tick"] = sim.tick_index
			jobs += 1
	sim._power_cooldown_until[team] = {}
	for id in sim.entity_ids():
		var row: Dictionary = sim.entities[id]
		if int(row.get("team", -1)) != team:
			continue
		for item_value in row.get("upgrade_queue", []) as Array:
			(item_value as Dictionary)["complete_tick"] = sim.tick_index
			jobs += 1
		var states: Dictionary = row.get("ability_states", {}) as Dictionary
		for ability_id in states:
			(states[ability_id] as Dictionary)["cooldown_ready_tick"] = 0
	return {"constructions": constructions, "jobs": jobs}


func debug_level_up_battalions(ids: Array) -> Dictionary:
	## +1 authored rank per selected battalion/hero: awards exactly the XP
	## delta to the next authored threshold through the real experience
	## pipeline, so every authored level effect applies at true magnitudes.
	var leveled := 0
	var capped := 0
	var unauthored := 0
	for id_value in ids:
		var id := int(id_value)
		if not sim.entities.has(id):
			continue
		var row: Dictionary = sim.entities[id]
		if int(row.get("health", 0)) <= 0:
			continue
		var rule: Dictionary = sim._unit_experience_rules.get(String(row.get("unit_type", "")), {})
		if rule.is_empty():
			unauthored += 1
			continue
		var level := int(row.get("level", 1))
		var next_row: Dictionary = {}
		for row_value in Array(rule.get("levels", [])):
			var candidate := row_value as Dictionary
			if int(candidate.get("rank", 0)) > level:
				next_row = candidate
				break
		if level >= int(rule.get("max_level", 1)) or next_row.is_empty():
			capped += 1
			continue
		var needed := int(next_row.get("required_experience", 0)) - int(row.get("experience_xp", 0))
		sim._award_experience(row, maxi(1, needed))
		leveled += 1
	return {"leveled": leveled, "capped": capped, "unauthored": unauthored}


func _step_construction() -> void:
	for structure_id in sim.structure_ids():
		var site: Dictionary = sim.structures[structure_id]
		if float(site.get("construction_progress", 1.0)) >= 1.0:
			continue
		if bool(site.get("builder_free", false)):
			# Foundation behavior: plot-built expansions rise without a porter.
			var elapsed := int(site.get("construction_elapsed_ticks", 0)) + 1
			var build_ticks := maxi(1, int(site.get("construction_build_ticks", 1)))
			site["construction_elapsed_ticks"] = elapsed
			site["construction_progress"] = minf(1.0, float(elapsed) / float(build_ticks))
			if elapsed >= build_ticks:
				var foundation_team := int(site.get("team", -1))
				if sim._team_ai_state.has(foundation_team):
					(sim._team_ai_state[foundation_team] as Dictionary)["construction_resolved"] = true
				sim._apply_structure_create_grants(site, false, true)
				sim._emit_event("construction.completed", 0, structure_id, {"team": int(site.get("team", -1)), "structure_kind": String(site.get("structure_kind", ""))})
			continue
		var builder_id := int(site.get("builder_id", 0))
		if builder_id != 0 and (not sim.entities.has(builder_id) or int((sim.entities[builder_id] as Dictionary).get("health", 0)) <= 0):
			# A dead builder can never finish its site. The husk is destroyed
			# instead of stalling forever; the builder's assignment clears too.
			if sim.entities.has(builder_id):
				(sim.entities[builder_id] as Dictionary)["construction_id"] = 0
			site["builder_id"] = 0
			if int(site.get("health", 0)) > 0:
				site["health"] = 0
				sim._emit_event("structure.destroyed", 0, structure_id, {"reason": "construction-builder-unavailable"})
			continue
		if not sim.entities.has(builder_id):
			continue
		var builder: Dictionary = sim.entities[builder_id]
		if int(builder.get("health", 0)) <= 0 or String(builder.get("state", "")) != "construct":
			continue
		# The builder only advances the site it is currently assigned to. An
		# abandoned site sharing the same builder id must not leech progress
		# from (and then hijack completion of) the active build.
		if int(builder.get("construction_id", 0)) != structure_id:
			continue
		var elapsed := int(site.get("construction_elapsed_ticks", 0)) + 1
		var build_ticks := maxi(1, int(site.get("construction_build_ticks", 1)))
		site["construction_elapsed_ticks"] = elapsed
		site["construction_progress"] = minf(1.0, float(elapsed) / float(build_ticks))
		if elapsed >= build_ticks:
			builder["construction_id"] = 0
			builder["order_kind"] = ""
			builder["state"] = "idle"
			var site_team := int(site.get("team", -1))
			if sim._team_ai_state.has(site_team):
				(sim._team_ai_state[site_team] as Dictionary)["construction_resolved"] = true
			if String(site.get("structure_kind", "")) == "fortress":
				sim._seed_expansion_pads_for(structure_id)
			sim._apply_structure_create_grants(site, false, true)
			sim._emit_event("construction.completed", builder_id, structure_id, {"team": int(site.get("team", -1)), "structure_kind": String(site.get("structure_kind", ""))})
