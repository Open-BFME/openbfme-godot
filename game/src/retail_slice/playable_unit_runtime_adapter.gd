class_name PlayableUnitRuntimeAdapter
extends RefCounted
## Converts an imported playable-unit document into the narrow data contracts
## consumed by the vertical slice. This file is deliberately source-object
## agnostic: categories declare capabilities; object ids are data, never code.

const TICK_SECONDS := 0.1


static func _has_authored_command_socket_evidence(route: Dictionary) -> bool:
	## Retail summons some heroes through a producer's authored construct
	## command instead of a fortress roster slot (Treebeard is trained by the
	## Ent Moot's Command_ConstructEntTreeBeard socket). Such a route is only
	## valid for a hero when it carries the authored INI provenance the
	## importer records for command sockets; anything else fails closed. This
	## mirrors the ContentDB registry gate verbatim.
	var source_value: Variant = route.get("source")
	if typeof(source_value) != TYPE_DICTIONARY:
		return false
	var source := source_value as Dictionary
	for field in ["producerIni", "commandSetIni", "commandButtonIni"]:
		if String(source.get(field, "")).strip_edges() == "":
			return false
	return true


static func is_ring_hero_summon(document: Dictionary) -> bool:
	## Retail ring heroes (Galadriel) are summoned by the One Ring mechanic,
	## never trained from a producer roster: their roster entry is the summon
	## slot and their command-point cost is zero. Such a document is not a
	## trained production candidate for any faction.
	if String(document.get("category", "")) != "hero":
		return false
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var simulation_row: Dictionary = registration.get("simulation", {}) as Dictionary
	var resolved: Dictionary = simulation_row.get("resolved", {}) as Dictionary
	var command_points: Variant = _resolved_value(resolved.get("commandPoints"))
	return command_points != null and int(command_points) == 0


static func fieldability(document: Dictionary) -> Dictionary:
	## Fail-closed classification for roster composition: exactly why a
	## converted playable-unit document can or cannot join a faction slice.
	## Callers record the reason; nothing is silently approximated.
	var category := String(document.get("category", ""))
	if category not in ["infantry", "ranged-infantry", "cavalry", "hero", "siege", "monster"]:
		return {"ok": false, "reason": "unsupported-category:%s" % category}
	if is_ring_hero_summon(document):
		return {"ok": false, "reason": "ring-hero-summon-not-trained"}
	var simulation := simulation_rule(document)
	if not simulation.is_empty():
		# Heroes and combat units without proven damage stay out of the roster
		# rather than crashing later when the normalized unit rule is empty.
		var combat: Dictionary = simulation.get("combat", {}) as Dictionary
		var damage_value: Variant = combat.get("damage")
		if typeof(damage_value) not in [TYPE_INT, TYPE_FLOAT] or int(damage_value) <= 0:
			return {"ok": false, "reason": "unresolved-combat-damage"}
		return {"ok": true, "reason": ""}
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var simulation_row: Dictionary = registration.get("simulation", {}) as Dictionary
	var missing: Array = (simulation_row.get("missing", []) as Array).duplicate()
	if not missing.is_empty():
		var fields: Array[String] = []
		for value in missing:
			fields.append(String(value))
		return {"ok": false, "reason": "unresolved-simulation-evidence:%s" % ",".join(fields)}
	var resolved: Dictionary = simulation_row.get("resolved", {}) as Dictionary
	var command_points: Variant = _resolved_value(resolved.get("commandPoints"))
	if command_points != null and int(command_points) <= 0:
		return {"ok": false, "reason": "command-points-unresolved-or-zero"}
	var combat_resolved: Dictionary = _resolved_dictionary(resolved.get("combat", {}))
	if int(combat_resolved.get("damage", 0)) <= 0:
		return {"ok": false, "reason": "unresolved-combat-damage"}
	return {"ok": false, "reason": "unresolved-simulation-evidence"}


static func runtime_unit_id(document: Dictionary) -> String:
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var composition: Dictionary = registration.get("composition", {}) as Dictionary
	return _runtime_id(String(composition.get("containerObjectId", document.get("objectId", ""))))


static func runtime_member_id(document: Dictionary) -> String:
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var composition: Dictionary = registration.get("composition", {}) as Dictionary
	return _runtime_id(String(composition.get("primaryMemberObjectId", document.get("objectId", ""))))


static func producer_bindings(document: Dictionary) -> Array[Dictionary]:
	var output: Array[Dictionary] = []
	var category := String(document.get("category", ""))
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var rows: Variant = registration.get("production", [])
	if typeof(rows) != TYPE_ARRAY:
		return output
	for value in rows as Array:
		if typeof(value) != TYPE_DICTIONARY:
			continue
		var row := value as Dictionary
		var surface := String(row.get("surface", ""))
		var slot := int(row.get("slot", 0))
		var roster_ordinal := int(row.get("rosterOrdinal", 0))
		if (
			surface not in ["command-socket", "hero-roster"]
			or (surface == "command-socket" and (slot < 1 or roster_ordinal != 0))
			or (surface == "hero-roster" and (roster_ordinal < 1 or slot != 0))
			or (category == "hero" and surface != "hero-roster" and not _has_authored_command_socket_evidence(row))
			or (category != "hero" and surface == "hero-roster")
		):
			return []
		output.append({
			"producer_source_object_id": String(row.get("producerObjectId", "")),
			"producer_runtime_id": _runtime_id(String(row.get("producerObjectId", ""))),
			"command_set_id": String(row.get("commandSetId", "")),
			"command_id": String(row.get("commandId", "")),
			"surface": surface,
			"slot": slot,
			"roster_ordinal": roster_ordinal,
			"prerequisites": (row.get("prerequisites", []) as Array).duplicate(),
			"command_set_transition": (row.get("commandSetTransition", []) as Array).duplicate(true),
		})
	return output


static func _select_portrait_id(portraits: Variant) -> String:
	## Prefer the authored SelectPortrait over the button icon when the
	## document declares both: hero portraits are "HP*" (192px), unit
	## portraits are "UP*" (191px), button icons are "HI*"/"BG*" (63-64px).
	for prefix in ["HP", "UP"]:
		for value in portraits as Array:
			if typeof(value) == TYPE_STRING and String(value).begins_with(prefix):
				return String(value)
	return _first_string(portraits)


static func hud_spec(document: Dictionary) -> Dictionary:
	var specs := hud_specs(document)
	return specs[0] if not specs.is_empty() else {}


static func hud_specs(document: Dictionary) -> Array[Dictionary]:
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var ui: Dictionary = registration.get("ui", {}) as Dictionary
	var commands: Variant = ui.get("commands", [])
	var portraits: Variant = ui.get("portraitImageIds", [])
	if typeof(commands) != TYPE_ARRAY or (commands as Array).is_empty() or typeof(portraits) != TYPE_ARRAY or (portraits as Array).is_empty():
		return []
	var portrait_id := _select_portrait_id(portraits)
	if portrait_id == "":
		return []
	var source_id := String(document.get("objectId", ""))
	var image_metadata: Dictionary = registration.get("imageBindingMetadata", {}) as Dictionary
	var string_bindings: Dictionary = registration.get("stringBindings", {}) as Dictionary
	var commands_by_id: Dictionary = {}
	for command_value in commands as Array:
		if typeof(command_value) == TYPE_DICTIONARY:
			commands_by_id[String((command_value as Dictionary).get("commandId", ""))] = command_value
	var output: Array[Dictionary] = []
	for producer in producer_bindings(document):
		var command_row: Dictionary = commands_by_id.get(String(producer.get("command_id", "")), {})
		var fields: Dictionary = command_row.get("fields", {}) as Dictionary
		var button_image := _first_string(fields.get("ButtonImage", []))
		var label_id := _first_string(fields.get("TextLabel", []))
		var tooltip_id := _first_string(fields.get("DescriptLabel", []))
		if button_image == "" or label_id == "" or tooltip_id == "":
			return []
		output.append({
			"unit_id": runtime_unit_id(document),
			"source_object_id": source_id,
			"producer_source_object_id": String(producer.get("producer_source_object_id", "")),
			"producer_runtime_id": String(producer.get("producer_runtime_id", "")),
			"button_name": "Train_" + _slug(source_id),
			"fallback_label": String(string_bindings.get(label_id, label_id)),
			"fallback_tooltip": String(string_bindings.get(tooltip_id, tooltip_id)),
			"image_source_size": (image_metadata.get(button_image, {}) as Dictionary).duplicate(),
			"image_id": button_image,
			"label_id": label_id,
			"tooltip_id": tooltip_id,
			"portrait_image_id": portrait_id,
			"command_id": String(command_row.get("commandId", "")),
			"surface": String(producer.get("surface", "")),
			"slot": int(producer.get("slot", 0)),
			"roster_ordinal": int(producer.get("roster_ordinal", 0)),
		})
	return output


static func simulation_rule(document: Dictionary) -> Dictionary:
	## Importers must resolve authoritative numbers before runtime. Raw macro,
	## WeaponTemplate, Locomotor or Armor ids are evidence, not simulation values.
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var simulation: Variant = registration.get("simulation", {})
	if typeof(simulation) != TYPE_DICTIONARY:
		return {}
	var row := simulation as Dictionary
	if row.has("status"):
		if String(row.get("status", "")) != "ready" or typeof(row.get("resolved")) != TYPE_DICTIONARY:
			return {}
		var resolved := row.get("resolved", {}) as Dictionary
		row = {
			"displayName": _resolved_value(resolved.get("displayNameId")),
			"buildCost": _resolved_value(resolved.get("buildCost")),
			"buildTimeSeconds": _resolved_value(resolved.get("buildTimeSeconds")),
			"commandPoints": _resolved_value(resolved.get("commandPoints")),
			"memberCount": _resolved_value(resolved.get("memberCount")),
			"memberHealth": _resolved_value(resolved.get("memberHealth")),
			"speed": _resolved_value(resolved.get("speed")),
			"visionRange": _resolved_value(resolved.get("visionRange")),
			"combat": _resolved_dictionary(resolved.get("combat", {})),
			"movement": _resolved_dictionary(resolved.get("movement", {})),
			"formation": resolved.get("formation", {}),
			"fearResistant": _resolved_value(resolved.get("fearResistant")),
		}
	for field in ["displayName", "buildCost", "buildTimeSeconds", "commandPoints", "memberCount", "memberHealth", "speed", "visionRange"]:
		if not row.has(field):
			return {}
	if (
		String(row.displayName).strip_edges() == ""
		or int(row.buildCost) < 0
		or float(row.buildTimeSeconds) <= 0.0
		or int(row.commandPoints) <= 0
		or int(row.memberCount) <= 0
		or int(row.memberHealth) <= 0
		or float(row.speed) < 0.0
		or float(row.visionRange) <= 0.0
	):
		return {}
	var producers := producer_bindings(document)
	if producers.is_empty():
		return {}
	var display_name := String(row.displayName)
	# The display name arrives as a source string-table id (OBJECT:ElvenElrond);
	# the pack's reviewed string bindings localize it when they cover the id.
	var string_bindings: Dictionary = registration.get("stringBindings", {}) as Dictionary
	display_name = String(string_bindings.get(display_name, display_name))
	return {
		"unit_type": runtime_unit_id(document),
		"object_id": runtime_member_id(document),
		"source_object_id": String(document.get("objectId", "")),
		"category": String(document.get("category", "")),
		"display_name": display_name,
		"default_cost": int(row.buildCost),
		"default_build_ticks": maxi(1, roundi(float(row.buildTimeSeconds) / TICK_SECONDS)),
		"default_command_points": int(row.commandPoints),
		"member_count": int(row.memberCount),
		"member_health": int(row.memberHealth),
		"speed_source": float(row.speed),
		"vision_range_source": float(row.visionRange),
		"combat": (row.get("combat", {}) as Dictionary).duplicate(true),
		"movement": (row.get("movement", {}) as Dictionary).duplicate(true),
		"formation": (row.get("formation", {}) as Dictionary).duplicate(true),
		"fear_resistant": row.get("fearResistant") == true,
		"producers": producers,
		"prerequisites": (producers[0].get("prerequisites", []) as Array).duplicate(),
	}


static func builder_production_rule(document: Dictionary) -> Dictionary:
	## Builders never resolve combat evidence (porters do not fight) and cost
	## zero command points, both of which the full simulation_rule contract
	## rejects. This narrower projection carries only the production facts
	## needed to train the faction builder from its authored producer.
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var simulation: Variant = registration.get("simulation", {})
	if typeof(simulation) != TYPE_DICTIONARY:
		return {}
	var resolved: Dictionary = (simulation as Dictionary).get("resolved", {}) as Dictionary
	var display := String(_resolved_value(resolved.get("displayNameId")))
	var cost := int(_resolved_value(resolved.get("buildCost")))
	var seconds := float(_resolved_value(resolved.get("buildTimeSeconds")))
	var command_points := int(_resolved_value(resolved.get("commandPoints")))
	if display.strip_edges() == "" or cost <= 0 or seconds <= 0.0 or command_points < 0:
		return {}
	var producers := producer_bindings(document)
	if producers.is_empty():
		return {}
	var string_bindings: Dictionary = registration.get("stringBindings", {}) as Dictionary
	return {
		"unit_type": runtime_unit_id(document),
		"object_id": runtime_member_id(document),
		"category": String(document.get("category", "")),
		"display_name": String(string_bindings.get(display, display)),
		"default_cost": cost,
		"default_build_ticks": maxi(1, roundi(seconds / TICK_SECONDS)),
		"default_command_points": command_points,
		"producers": producers,
	}


## Multiplies proven locomotor acceleration/braking for snappier horde response.
## Not a retail claim — scales source-authored numbers only (see REPORT).
const HORDE_LOCOMOTION_RESPONSE_SCALE := 1.5


static func normalized_unit_rule(simulation: Dictionary, source_scale: float) -> Dictionary:
	if source_scale <= 0.0:
		return {}
	var movement: Dictionary = simulation.get("movement", {}) as Dictionary
	var combat: Dictionary = simulation.get("combat", {}) as Dictionary
	var formation: Dictionary = simulation.get("formation", {}) as Dictionary
	for field in ["acceleration", "braking", "turnRateDegreesPerSecond"]:
		if not movement.has(field):
			return {}
	for field in ["attackRange", "delayBetweenShotsMs", "preAttackDelayMs", "firingDurationMs", "damage"]:
		if not combat.has(field):
			return {}
	var positions_value: Variant = formation.get("positions", [])
	if typeof(positions_value) != TYPE_ARRAY or (positions_value as Array).size() != int(simulation.get("member_count", 0)):
		return {}
	var source_positions: Array[Vector2] = []
	for value in positions_value as Array:
		if typeof(value) != TYPE_DICTIONARY:
			return {}
		var row := value as Dictionary
		source_positions.append(Vector2(float(row.get("x", NAN)), float(row.get("y", NAN))))
		if not is_finite(source_positions[-1].x) or not is_finite(source_positions[-1].y):
			return {}
	var center := Vector2.ZERO
	for position in source_positions:
		center += position
	center /= float(source_positions.size())
	var positions: Array[Vector3] = []
	for position in source_positions:
		positions.append(Vector3((position.y - center.y) * source_scale, 0.0, (position.x - center.x) * source_scale))
	var speed := float(simulation.get("speed_source", -1.0))
	var vision := float(simulation.get("vision_range_source", -1.0))
	var acceleration := float(movement.get("acceleration", -1.0)) * HORDE_LOCOMOTION_RESPONSE_SCALE
	var braking := float(movement.get("braking", -1.0)) * HORDE_LOCOMOTION_RESPONSE_SCALE
	var turn_rate := float(movement.get("turnRateDegreesPerSecond", -1.0))
	var attack_range := float(combat.get("attackRange", -1.0))
	var minimum_range := float(combat.get("minimumAttackRange", 0.0))
	var delay_ms := float(combat.get("delayBetweenShotsMs", -1.0))
	var pre_attack_ms := float(combat.get("preAttackDelayMs", -1.0))
	var firing_ms := float(combat.get("firingDurationMs", -1.0))
	var damage := int(combat.get("damage", 0))
	for numeric in [speed, vision, acceleration, braking, turn_rate, attack_range, delay_ms, pre_attack_ms, firing_ms]:
		if not is_finite(float(numeric)) or float(numeric) < 0.0:
			return {}
	if damage <= 0:
		return {}
	var period_ms := delay_ms
	var clip_reload_ms := float(combat.get("clipReloadTimeMs", 0.0))
	if period_ms <= 0.0 and clip_reload_ms > 0.0:
		period_ms = clip_reload_ms
	var category := String(simulation.get("category", ""))
	return {
		"horde_id": String(simulation.get("unit_type", "")),
		"member_count": int(simulation.get("member_count", 0)),
		"member_health": int(simulation.get("member_health", 0)),
		"member_damage": damage,
		"category": category,
		"speed": speed * source_scale,
		"speed_source": speed,
		"acceleration": acceleration * source_scale,
		"acceleration_source": acceleration,
		"turn_rate_degrees_per_second": turn_rate,
		"braking": braking * source_scale,
		"braking_source": braking,
		"attack_range": attack_range * source_scale,
		"attack_range_source": attack_range,
		"minimum_attack_range": minimum_range * source_scale,
		"minimum_attack_range_source": minimum_range,
		"vision_range": vision * source_scale,
		"vision_range_source": vision,
		"delay_between_shots_ms": delay_ms,
		"pre_attack_delay_ms": pre_attack_ms,
		"firing_duration_ms": firing_ms,
		"attack_period_ticks": maxi(1, roundi(period_ms / (TICK_SECONDS * 1000.0))),
		"pre_attack_ticks": maxi(0, roundi(pre_attack_ms / (TICK_SECONDS * 1000.0))),
		"firing_duration_ticks": maxi(0, roundi(firing_ms / (TICK_SECONDS * 1000.0))),
		"clip_size": int(combat.get("clipSize", 0)),
		"clip_reload_time_ms": clip_reload_ms,
		"continuous_fire_one": int(combat.get("continuousFireOne", 0)),
		"continuous_fire_coast_ticks": maxi(0, roundi(float(combat.get("continuousFireCoastMs", 0.0)) / (TICK_SECONDS * 1000.0))),
		"continuous_fire_rate_multiplier": 1.0,
		"fear_resistant": bool(simulation.get("fear_resistant", false)),
		"formation_positions": positions,
		"provenance": {"source_object_id": String(simulation.get("source_object_id", "")), "source_contract": "openbfme.playable-unit-runtime"},
	}


static func audio_event_ids(document: Dictionary, kind: String) -> Array[String]:
	var aliases: Dictionary = {
		"select": ["voiceselect", "voiceselectbattle"],
		"move": ["voicemove"],
		"attack": ["voiceattack", "voiceattackmachine", "voiceattackstructure"],
		"attack_structure": ["voiceattackstructure", "voiceattackmachine"],
		"build": ["voicebuildresponse"],
		"death": ["voicedie", "sound", "sounddeath", "sounddeath1", "sounddeath2"],
	}
	if not aliases.has(kind):
		return []
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var routes: Variant = registration.get("audioRoutes", {})
	if typeof(routes) != TYPE_DICTIONARY:
		return []
	var output: Array[String] = []
	for owner_value in (routes as Dictionary).values():
		if typeof(owner_value) != TYPE_DICTIONARY:
			continue
		var owner := owner_value as Dictionary
		for field_value in owner.keys():
			var field := String(field_value).to_lower()
			if not (aliases[kind] as Array).has(field):
				continue
			for row_value in Array(owner[field_value]):
				if typeof(row_value) == TYPE_DICTIONARY:
					_append_unique_string(output, String((row_value as Dictionary).get("id", "")))
	return output


static func production_audio_event_ids(document: Dictionary) -> Dictionary:
	var output := {"purchase": [], "purchase_by_command": {}, "created": []}
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var routes: Dictionary = registration.get("audioRoutes", {}) as Dictionary
	for owner_value in routes.values():
		if typeof(owner_value) != TYPE_DICTIONARY:
			continue
		for field_value in (owner_value as Dictionary).keys():
			if String(field_value).to_lower() not in ["voicecreated", "voicefullycreated", "soundcreated"]:
				continue
			for route_value in Array((owner_value as Dictionary)[field_value]):
				if typeof(route_value) == TYPE_DICTIONARY:
					_append_unique_string(output.created, String((route_value as Dictionary).get("id", "")))
	var ui: Dictionary = registration.get("ui", {}) as Dictionary
	for command_value in Array(ui.get("commands", [])):
		if typeof(command_value) != TYPE_DICTIONARY:
			continue
		var command := command_value as Dictionary
		var command_id := String(command.get("commandId", ""))
		var command_events: Array = []
		for route_value in Array(command.get("audioRoutes", [])):
			if typeof(route_value) != TYPE_DICTIONARY:
				continue
			var route := route_value as Dictionary
			_append_unique_string(command_events, String(route.get("id", "")))
			_append_unique_string(output.purchase, String(route.get("id", "")))
		(output.purchase_by_command as Dictionary)[command_id] = command_events
	return output


static func experience_rule(document: Dictionary) -> Dictionary:
	## Project the converter-emitted ExperienceLevel chain of one unit document
	## into the narrow runtime contract: cumulative per-level thresholds, the XP
	## a killer collects per member kill at each of this unit's levels, and the
	## per-level stat effects the sim can apply faithfully. Fail-closed like the
	## other adapter surfaces: a malformed row rejects the whole contract. Older
	## packs without the key and units whose chain retail never authored
	## ("unauthored"/"unavailable") project an empty rule.
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var experience_value: Variant = registration.get("experience")
	if typeof(experience_value) != TYPE_DICTIONARY:
		return {}
	var experience := experience_value as Dictionary
	if String(experience.get("status", "")) != "compiled":
		return {}
	var max_level := int(experience.get("maxLevel", 0))
	var initial_rank := int(experience.get("initialRank", 1))
	var levels_value: Variant = experience.get("levels")
	if (
		max_level < 1
		or initial_rank < 1
		or initial_rank > max_level
		or typeof(levels_value) != TYPE_ARRAY
		or (levels_value as Array).is_empty()
	):
		return {}
	var levels: Array[Dictionary] = []
	var previous_rank := 0
	for row_value in levels_value as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			return {}
		var row := row_value as Dictionary
		var rank := int(row.get("rank", 0))
		var required: Variant = row.get("requiredExperience")
		var award: Variant = row.get("experienceAward")
		# Authored ranks ascend but are not always 1..N (retail summons such as
		# the ring hero enter at their authored top rank); rows keep their
		# authored ranks verbatim.
		if rank <= previous_rank or required == null or award == null:
			return {}
		previous_rank = rank
		var health_add := 0.0
		var damage_add := 0.0
		var production_mult := 1.0
		var unsupported: Array[String] = []
		for leaf_value in Array(row.get("attributeModifiers", [])):
			if typeof(leaf_value) != TYPE_DICTIONARY:
				return {}
			var leaf := leaf_value as Dictionary
			for modifier_value in Array(leaf.get("modifiers", [])):
				if typeof(modifier_value) != TYPE_DICTIONARY:
					return {}
				var modifier := modifier_value as Dictionary
				match String(modifier.get("kind", "")):
					"HEALTH":
						health_add += float(modifier.get("value", 0.0))
					"DAMAGE_ADD":
						damage_add += float(modifier.get("value", 0.0))
					"PRODUCTION":
						production_mult *= float(modifier.get("value", 1.0))
					_:
						return {}
			for unsupported_value in Array(leaf.get("unsupportedModifiers", [])):
				unsupported.append(String(unsupported_value))
		var level_row: Dictionary = {
			"rank": rank,
			"required_experience": int(required),
			"experience_award": int(award),
			"health_add": health_add,
			"damage_add": damage_add,
			"production_multiplier": production_mult,
		}
		if not unsupported.is_empty():
			level_row["unsupported_modifiers"] = unsupported
		if String(row.get("selectionDecalTextureId", "")) != "":
			level_row["selection_decal_texture_id"] = String(row.get("selectionDecalTextureId", ""))
		levels.append(level_row)
	if previous_rank != max_level or int((levels[0] as Dictionary).get("rank", 0)) != initial_rank:
		return {}
	return {
		"max_level": max_level,
		"initial_rank": initial_rank,
		"levels": levels,
		"source_ini": String(experience.get("sourceIni", "")),
	}


static func ability_rules(document: Dictionary) -> Array[Dictionary]:
	## Project the converter-emitted SPECIAL_POWER ability rows of one hero
	## document into the narrow runtime contract. Fail-closed like the other
	## adapter surfaces: any malformed row rejects the whole array instead of
	## silently dropping an authored ability.
	var output: Array[Dictionary] = []
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var rows: Variant = registration.get("abilities", [])
	if typeof(rows) != TYPE_ARRAY:
		return []
	var string_bindings: Dictionary = registration.get("stringBindings", {}) as Dictionary
	for value in rows as Array:
		if typeof(value) != TYPE_DICTIONARY:
			return []
		var row := value as Dictionary
		var ability_id := String(row.get("id", ""))
		var slot := int(row.get("slot", 0))
		var targeting := String(row.get("targeting", ""))
		if ability_id == "" or slot < 1 or targeting not in ["self", "point", "enemy-object"]:
			return []
		var button: Dictionary = row.get("button", {}) as Dictionary
		var effect: Dictionary = row.get("effect", {}) as Dictionary
		var effect_kind := String(effect.get("kind", ""))
		if effect_kind not in ["none", "weapon-blast", "heal", "summon", "attribute-modifier", "leadership-aura", "weapon-toggle", "terror"]:
			return []
		var implementation: Dictionary = row.get("implementation", {}) as Dictionary
		var status := String(implementation.get("status", ""))
		if status not in ["implemented", "unimplemented", "passive"]:
			return []
		var gate: Dictionary = row.get("levelGate", {}) as Dictionary
		var raw_level: Variant = gate.get("requiredLevel")
		var label_id := _first_string(button.get("iconIds", []))
		var text_id := _first_string(button.get("labelIds", []))
		var tooltip_id := _first_string(button.get("tooltipIds", []))
		output.append({
			"ability_id": ability_id,
			"slot": slot,
			"special_power_id": String(row.get("specialPowerId", "")),
			"targeting": targeting,
			"cooldown_ticks": maxi(0, roundi(float(row.get("cooldownMs", 0.0)) / (TICK_SECONDS * 1000.0))),
			"required_level": int(raw_level) if raw_level != null else 1,
			"level_gate_resolved": gate.is_empty() or raw_level != null,
			"castable": status == "implemented",
			"availability_reason": String(implementation.get("reason", "")),
			"limitations": (implementation.get("limitations", []) as Array).duplicate(),
			"effect": effect.duplicate(true),
			"icon_id": _first_string(button.get("iconIds", [])),
			"label_id": text_id,
			"tooltip_id": tooltip_id,
			"fallback_label": String(string_bindings.get(text_id, text_id)),
			"fallback_tooltip": String(string_bindings.get(tooltip_id, tooltip_id)),
			"initiate_sound_id": String(row.get("initiateSoundId", "")),
			"unit_specific_sound_id": String(row.get("unitSpecificSoundId", "")),
			"radius_cursor_type": String(button.get("radiusCursorType", "")),
			"options": (button.get("options", []) as Array).duplicate(),
		})
	return output


static func runtime_object_id(source_id: String) -> String:
	## Public slug for summon/OCL object lookups (mirrors runtime_member_id).
	return _runtime_id(source_id)


static func _first_production_slot(document: Dictionary) -> int:
	var rows := producer_bindings(document)
	return int(rows[0].get("slot", 0)) if not rows.is_empty() else 0


static func _resolved_value(value: Variant) -> Variant:
	return (value as Dictionary).get("value") if typeof(value) == TYPE_DICTIONARY else value


static func _resolved_dictionary(value: Variant) -> Dictionary:
	if typeof(value) != TYPE_DICTIONARY:
		return {}
	var output: Dictionary = {}
	for key in (value as Dictionary).keys():
		output[key] = _resolved_value((value as Dictionary)[key])
	return output


static func _first_string(value: Variant) -> String:
	if typeof(value) == TYPE_STRING:
		return String(value)
	if typeof(value) != TYPE_ARRAY:
		return ""
	for item in value as Array:
		if typeof(item) == TYPE_STRING and String(item).strip_edges() != "":
			return String(item)
	return ""


static func _append_unique_string(output: Array, value: String) -> void:
	if value != "" and not output.has(value):
		output.append(value)


static func _runtime_id(source_id: String) -> String:
	return "bfme2.object." + _slug(source_id)


static func _slug(value: String) -> String:
	var output := ""
	var previous_dash := false
	for index in value.length():
		var code := value.unicode_at(index)
		var is_upper := code >= 65 and code <= 90
		var is_lower := code >= 97 and code <= 122
		var is_digit := code >= 48 and code <= 57
		if is_upper and index > 0 and not previous_dash:
			var previous := value.unicode_at(index - 1)
			if (previous >= 97 and previous <= 122) or (previous >= 48 and previous <= 57):
				output += "-"
		if is_upper or is_lower or is_digit:
			output += String.chr(code).to_lower()
			previous_dash = false
		elif not previous_dash and output != "":
			output += "-"
			previous_dash = true
	return output.trim_suffix("-")
