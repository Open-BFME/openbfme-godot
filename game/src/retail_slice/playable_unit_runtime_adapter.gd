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


static func selection_commands(document: Dictionary) -> Array:
	## The unit's own CommandSet (palantir when this unit is selected).
	## Distinct from ui.commands, which is the producer construct/train dump.
	var ui := _document_ui(document)
	var rows: Variant = ui.get("selectionCommands", [])
	if typeof(rows) != TYPE_ARRAY:
		return []
	var output: Array = []
	for value in rows as Array:
		if typeof(value) != TYPE_DICTIONARY:
			continue
		var row := value as Dictionary
		var command_id := String(row.get("commandId", "")).strip_edges()
		if command_id == "":
			continue
		output.append(row)
	return output


static func selection_command_ids(document: Dictionary) -> PackedStringArray:
	var ids: PackedStringArray = PackedStringArray()
	for row_value in selection_commands(document):
		ids.append(String((row_value as Dictionary).get("commandId", "")))
	return ids


static func playable_lookup_aliases(raw_id: String) -> PackedStringArray:
	## Entity rows carry `bfme2.object.gondor-fighter-horde`. Playable-unit
	## documents register as `GondorFighterHorde` / `gondorfighter`. Try every
	## spelling the two sides actually use.
	var aliases: PackedStringArray = PackedStringArray()
	var seen: Dictionary = {}
	for candidate in _alias_candidates(raw_id):
		if candidate == "" or seen.has(candidate):
			continue
		seen[candidate] = true
		aliases.append(candidate)
	return aliases


static func _alias_candidates(raw_id: String) -> PackedStringArray:
	var raw := raw_id.strip_edges()
	var out: PackedStringArray = PackedStringArray()
	if raw == "":
		return out
	out.append(raw)
	var folded := raw.to_lower()
	out.append(folded)
	var slug := folded
	if slug.begins_with("bfme2.object."):
		slug = slug.substr("bfme2.object.".length())
	var compact := slug.replace("-", "").replace("_", "")
	if compact != "":
		out.append(compact)
	var pascal := ""
	for part in slug.replace("_", "-").split("-"):
		if part.is_empty():
			continue
		pascal += part.substr(0, 1).to_upper() + part.substr(1)
	if pascal != "":
		out.append(pascal)
	return out


static func resolve_playable_document(db: Object, row: Dictionary) -> Dictionary:
	## Same recipe as RetailBattalion._member_source_geometry_radius: prefer
	## the bundle's sourceObjectId + member index, then horde/object ids.
	## An empty Dictionary is a miss, never a hit.
	if db == null:
		return {}
	var unit_type := String(row.get("unit_type", ""))
	var member_id := String(row.get("object_id", ""))
	var source_object_id := ""
	if db.has_method("get_bundle_object"):
		for candidate in playable_lookup_aliases(member_id):
			var definition: Variant = db.call("get_bundle_object", candidate)
			if typeof(definition) == TYPE_DICTIONARY and not (definition as Dictionary).is_empty():
				source_object_id = String((definition as Dictionary).get("sourceObjectId", ""))
				if source_object_id != "":
					break
		if source_object_id == "":
			for candidate in playable_lookup_aliases(unit_type):
				var horde_def: Variant = db.call("get_bundle_object", candidate)
				if typeof(horde_def) == TYPE_DICTIONARY and not (horde_def as Dictionary).is_empty():
					source_object_id = String((horde_def as Dictionary).get("sourceObjectId", ""))
					if source_object_id != "":
						break
	var raw_ids: PackedStringArray = PackedStringArray()
	if source_object_id != "":
		raw_ids.append(source_object_id)
	if unit_type != "":
		raw_ids.append(unit_type)
	if member_id != "":
		raw_ids.append(member_id)
	return resolve_playable_document_by_ids(db, raw_ids)


static func resolve_playable_document_by_ids(db: Object, raw_ids: PackedStringArray) -> Dictionary:
	if db == null:
		return {}
	var aliases: PackedStringArray = PackedStringArray()
	var seen: Dictionary = {}
	for raw in raw_ids:
		for alias in playable_lookup_aliases(String(raw)):
			if seen.has(alias):
				continue
			seen[alias] = true
			aliases.append(alias)
	if db.has_method("get_playable_unit_runtime_for_member"):
		for alias in aliases:
			var by_member: Variant = db.call("get_playable_unit_runtime_for_member", alias)
			if typeof(by_member) == TYPE_DICTIONARY and not (by_member as Dictionary).is_empty():
				return by_member as Dictionary
	if db.has_method("get_playable_unit_runtime"):
		for alias in aliases:
			var by_id: Variant = db.call("get_playable_unit_runtime", alias)
			if typeof(by_id) == TYPE_DICTIONARY and not (by_id as Dictionary).is_empty():
				return by_id as Dictionary
	return {}


static func authored_mounted_mesh(document: Dictionary) -> Dictionary:
	## Retail ModelConditionState = MOUNTED model. Missing converted GLB is a
	## named gap, never a stand-in horse.
	var visual := _document_visual(document)
	var mounted_token := ""
	for leaf_value in visual.get("authoredVisualLeaves", []) as Array:
		if typeof(leaf_value) != TYPE_DICTIONARY:
			continue
		var leaf := leaf_value as Dictionary
		var conditions: Array = leaf.get("conditions", []) as Array
		var mounted := false
		for token_value in conditions:
			if String(token_value).to_upper() == "MOUNTED":
				mounted = true
				break
		if not mounted:
			continue
		var identifier := String(leaf.get("identifier", ""))
		if identifier != "":
			mounted_token = identifier
		var output := String(leaf.get("output", ""))
		var kind := String(leaf.get("kind", ""))
		if kind == "model" or identifier.to_upper().ends_with("_SKN"):
			if output.to_lower().ends_with(".glb"):
				return {"id": identifier, "path": output, "gap": ""}
			return {
				"id": identifier,
				"path": output,
				"gap": "mounted-model-unconverted:%s" % (identifier if identifier != "" else output),
			}
	var composed: Dictionary = visual.get("presentationComposition", {}) as Dictionary
	if String(composed.get("form", "")) == "mounted-container-payload":
		return {"id": String(composed.get("visualPrimaryObjectId", "")), "path": "", "gap": ""}
	if mounted_token.to_upper().ends_with("_SKL"):
		mounted_token = mounted_token.substr(0, mounted_token.length() - 4) + "_SKN"
	if mounted_token != "":
		return {"id": mounted_token, "path": "", "gap": "mounted-model-missing:%s" % mounted_token}
	return {"id": "", "path": "", "gap": "mounted-model-missing"}


static func _document_ui(document: Dictionary) -> Dictionary:
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	if typeof(registration.get("ui")) == TYPE_DICTIONARY:
		return registration.get("ui") as Dictionary
	var presentation: Dictionary = document.get("presentation", {}) as Dictionary
	if typeof(presentation.get("ui")) == TYPE_DICTIONARY:
		return presentation.get("ui") as Dictionary
	return {}


static func _document_visual(document: Dictionary) -> Dictionary:
	if typeof(document.get("authoredVisualLeaves")) == TYPE_ARRAY:
		return document
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	if typeof(registration.get("visual")) == TYPE_DICTIONARY:
		return registration.get("visual") as Dictionary
	var presentation: Dictionary = document.get("presentation", {}) as Dictionary
	if typeof(presentation.get("visual")) == TYPE_DICTIONARY:
		return presentation.get("visual") as Dictionary
	if typeof(document.get("visual")) == TYPE_DICTIONARY:
		return document.get("visual") as Dictionary
	return {}


static func ship_particle_attachments(document: Dictionary) -> Array[Dictionary]:
	## Project importer-preserved Draw-state ParticleSysBone sites into a narrow
	## runtime binding. Particle resources are presentation-owned; malformed
	## rows fail the whole surface closed instead of moving FX to the hull root.
	if String(document.get("category", "")) != "naval":
		return []
	var visual := _document_visual(document)
	var raw: Variant = visual.get("particleAttachments", [])
	if typeof(raw) != TYPE_ARRAY:
		return []
	var output: Array[Dictionary] = []
	for value in raw as Array:
		if typeof(value) != TYPE_DICTIONARY:
			return []
		var row := value as Dictionary
		var anchor := String(row.get("anchorBone", "")).strip_edges()
		var particle := String(row.get("particleSystemId", "")).strip_edges()
		var source_ini := String(row.get("sourceIni", "")).strip_edges()
		var conditions_value: Variant = row.get("modelConditions", [])
		var options_value: Variant = row.get("options", [])
		if (
			String(row.get("field", "")) != "ParticleSysBone"
			or anchor == "" or particle == "" or source_ini == ""
			or int(row.get("line", 0)) <= 0
			or typeof(row.get("followBone")) != TYPE_BOOL
			or typeof(conditions_value) != TYPE_ARRAY
			or typeof(options_value) != TYPE_ARRAY
		):
			return []
		var conditions: Array[String] = []
		for condition_value in conditions_value as Array:
			var condition := String(condition_value).strip_edges().to_upper()
			if condition == "" or conditions.has(condition):
				return []
			conditions.append(condition)
		conditions.sort()
		var options: Array[String] = []
		for option_value in options_value as Array:
			var option := String(option_value).strip_edges()
			if option == "":
				return []
			options.append(option)
		output.append({
			"anchor_bone": anchor,
			"particle_system_id": particle,
			"follow_bone": bool(row.get("followBone", false)),
			"model_conditions": conditions,
			"options": options,
			"draw_module_kind": String(row.get("drawModuleKind", "")),
			"draw_module_tag": String(row.get("drawModuleTag", "")),
			"source_ini": source_ini,
			"line": int(row.get("line", 0)),
		})
	return output


static func _optional_resolved_string(value: Variant) -> String:
	## An absent descriptor field is null, and String(null) is not a conversion.
	var resolved: Variant = _resolved_value(value)
	if typeof(resolved) != TYPE_STRING and typeof(resolved) != TYPE_STRING_NAME:
		return ""
	return String(resolved).strip_edges()


static func ship_weapon_attack(
	document: Dictionary, source_unit_scale: float, tick_seconds: float = TICK_SECONDS
) -> Dictionary:
	## Project a naval descriptor's resolved combat block into the structure
	## weapon row the sim already fires (`structures[id]["attack"]`).
	##
	## Only four of RotWK's thirteen effective SHIP objects author a weapon the
	## runtime can execute, and the refusals matter as much as the admissions:
	## the corsair/battleship rangefinder weapons compile a weaponId with no
	## damage and no range, and ElvenFireShip authors AttackRange = 0. Naming
	## each refusal is the point — an invented range or damage would let a hull
	## fire numbers retail never wrote.
	if String(document.get("category", "")) != "naval":
		return {"ok": false, "reason": "not-a-naval-document"}
	if source_unit_scale <= 0.0 or tick_seconds <= 0.0:
		return {"ok": false, "reason": "invalid-source-scale-or-tick"}
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var simulation: Variant = registration.get("simulation", {})
	if typeof(simulation) != TYPE_DICTIONARY:
		return {"ok": false, "reason": "no-simulation-block"}
	var resolved: Variant = (simulation as Dictionary).get("resolved", {})
	if typeof(resolved) != TYPE_DICTIONARY:
		return {"ok": false, "reason": "no-resolved-simulation-block"}
	var raw_combat: Variant = (resolved as Dictionary).get("combat")
	if typeof(raw_combat) != TYPE_DICTIONARY or (raw_combat as Dictionary).is_empty():
		return {"ok": false, "reason": "no-authored-weapon"}
	var combat := raw_combat as Dictionary
	var weapon_id := _optional_resolved_string(combat.get("weaponId"))
	if weapon_id == "":
		return {"ok": false, "reason": "weapon-id-missing"}
	var damage_value: Variant = _resolved_value(combat.get("damage"))
	if typeof(damage_value) not in [TYPE_INT, TYPE_FLOAT] or float(damage_value) <= 0.0:
		return {"ok": false, "reason": "damage-not-authored:%s" % weapon_id}
	var range_value: Variant = _resolved_value(combat.get("attackRange"))
	if typeof(range_value) not in [TYPE_INT, TYPE_FLOAT] or float(range_value) <= 0.0:
		return {"ok": false, "reason": "attack-range-not-positive:%s" % weapon_id}
	var minimum_value: Variant = _resolved_value(combat.get("minimumAttackRange"))
	var minimum_source := 0.0
	if typeof(minimum_value) in [TYPE_INT, TYPE_FLOAT]:
		minimum_source = maxf(0.0, float(minimum_value))
	if minimum_source >= float(range_value):
		return {"ok": false, "reason": "minimum-range-not-below-attack-range:%s" % weapon_id}
	# DelayBetweenShots is the one field retail routinely leaves unwritten, and
	# the importer says so in the row's `semantic` instead of pretending it was
	# authored. Carry that distinction through rather than losing it here.
	var delay_row: Variant = combat.get("delayBetweenShotsMs")
	var delay_value: Variant = _resolved_value(delay_row)
	if typeof(delay_value) not in [TYPE_INT, TYPE_FLOAT] or float(delay_value) < 0.0:
		return {"ok": false, "reason": "delay-between-shots-unresolved:%s" % weapon_id}
	var delay_source := "authored"
	if typeof(delay_row) == TYPE_DICTIONARY and String((delay_row as Dictionary).get("semantic", "")) != "":
		delay_source = "sage-default"
	var pre_attack_value: Variant = _resolved_value(combat.get("preAttackDelayMs"))
	var pre_attack_ms := 0.0
	if typeof(pre_attack_value) in [TYPE_INT, TYPE_FLOAT]:
		pre_attack_ms = maxf(0.0, float(pre_attack_value))
	var attack := {
		"weapon_id": weapon_id,
		"damage": float(damage_value),
		"damage_type": _optional_resolved_string(combat.get("damageType")).to_lower(),
		"range": float(range_value) * source_unit_scale,
		"range_source": float(range_value),
		"minimum_range": minimum_source * source_unit_scale,
		"minimum_range_source": minimum_source,
		"period_ticks": maxi(1, roundi(float(delay_value) / (tick_seconds * 1000.0))),
		"pre_attack_ticks": maxi(0, roundi(pre_attack_ms / (tick_seconds * 1000.0))),
		"delay_between_shots_ms": float(delay_value),
		"delay_between_shots_source": delay_source,
		"cooldown": 0,
		"affects": "ENEMIES",
	}
	var projectile_id := _optional_resolved_string(combat.get("projectileObjectId"))
	var projectile_speed: Variant = _resolved_value(combat.get("projectileSpeed"))
	if projectile_id != "":
		# A projectile object without a speed has no flight time to compute, and
		# guessing one would move the impact tick. Refuse the pair, not half.
		if typeof(projectile_speed) not in [TYPE_INT, TYPE_FLOAT] or float(projectile_speed) <= 0.0:
			return {"ok": false, "reason": "projectile-speed-not-authored:%s" % weapon_id}
		attack["projectile_object_id"] = projectile_id
		attack["projectile_speed"] = float(projectile_speed) * source_unit_scale
		attack["projectile_speed_source"] = float(projectile_speed)
		attack["next_projectile_token"] = 1
	return {"ok": true, "reason": "", "attack": attack}


static func ship_particle_attachments_for_conditions(
	document: Dictionary, active_conditions: Array
) -> Array[Dictionary]:
	## SAGE chooses the most-specific matching ModelConditionState. Apply that
	## same deterministic mux to the attachment rows and retain every particle
	## authored in the selected state.
	var attachments := ship_particle_attachments(document)
	if attachments.is_empty():
		return []
	var active: Dictionary = {}
	for value in active_conditions:
		var token := String(value).strip_edges().to_upper()
		if token != "":
			active[token] = true
	var best_specificity := -1
	var best_condition_sets: Dictionary = {}
	for row in attachments:
		var conditions := row.get("model_conditions", []) as Array
		var matches := true
		for condition_value in conditions:
			if not active.has(String(condition_value)):
				matches = false
				break
		if matches:
			var specificity := conditions.size()
			if specificity > best_specificity:
				best_specificity = specificity
				best_condition_sets.clear()
			if specificity == best_specificity:
				best_condition_sets[",".join(conditions)] = true
	if best_specificity < 0:
		return []
	# Two distinct, equally-specific state sets matching at once have no
	# evidence-closed precedence in the descriptor. Fail closed instead of
	# combining particles from two mutually exclusive SAGE states.
	if best_condition_sets.size() != 1:
		return []
	var selected_condition_set := String(best_condition_sets.keys()[0])
	var selected: Array[Dictionary] = []
	for row in attachments:
		var conditions := row.get("model_conditions", []) as Array
		if conditions.size() != best_specificity or ",".join(conditions) != selected_condition_set:
			continue
		var matches := true
		for condition_value in conditions:
			if not active.has(String(condition_value)):
				matches = false
				break
		if matches:
			selected.append(row.duplicate(true))
	return selected


static func is_ring_hero_summon(document: Dictionary) -> bool:
	## Exact compiled provenance; command-point/category guesses misclassified
	## ordinary zero-CP heroes and hid malformed ring routes.
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	# CAH documents reuse the engine hero-roster surface but carry an explicit
	# createAHero ownership record; they are never retail One Ring summons.
	if registration.has("createAHero"):
		return false
	for route_value in registration.get("production", []) as Array:
		if typeof(route_value) == TYPE_DICTIONARY and String(
			(route_value as Dictionary).get("sourceField", "")
		) == "BuildableRingHeroesMP":
			return true
	return false


static func fieldability(document: Dictionary, allow_ring_heroes := false) -> Dictionary:
	## Fail-closed classification for roster composition: exactly why a
	## converted playable-unit document can or cannot join a faction slice.
	## Callers record the reason; nothing is silently approximated.
	var category := String(document.get("category", ""))
	if category not in ["infantry", "ranged-infantry", "cavalry", "hero", "siege", "monster"]:
		return {"ok": false, "reason": "unsupported-category:%s" % category}
	if is_ring_hero_summon(document) and not allow_ring_heroes:
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
			"source_field": String(row.get("sourceField", "")),
			"slot": slot,
			"roster_ordinal": roster_ordinal,
			"prerequisites": (row.get("prerequisites", []) as Array).duplicate(),
			# Optional ANY-of gate (converter prerequisiteAnyOf, from the
			# button's NeededUpgradeAny). Absent on every pack built before the
			# field existed, which is exactly the fail-closed ALL-of behavior.
			"prerequisites_any_of": (row.get("prerequisiteAnyOf", []) as Array).duplicate(),
			"command_set_transition": (row.get("commandSetTransition", []) as Array).duplicate(true),
		})
	return output


static func scenario_admission(document: Dictionary, surface: String) -> Dictionary:
	## Non-buildable retail Objects are addressable only from authored scenario
	## surfaces. Returning this contract is deliberately separate from
	## producer_bindings(), which remains empty and therefore cannot leak these
	## ships into an ordinary command bar or faction roster.
	if surface not in ["map-placement", "script-spawn", "tutorial-script", "object-creation-list", "lair-spawn", "horde-payload"]:
		return {}
	var registration := document.get("registration", {}) as Dictionary
	if not (registration.get("production", []) as Array).is_empty():
		return {}
	var value: Variant = registration.get("scenarioAdmission")
	if typeof(value) != TYPE_DICTIONARY:
		return {}
	var admission := value as Dictionary
	if (
		String(admission.get("kind", "")) != "authored-non-buildable"
		or String(admission.get("role", "")) not in ["inheritance-template", "scenario-only", "creature", "horde", "summoned-hero"]
		or bool(admission.get("buildCommandExposed", true))
		or not (admission.get("surfaces", []) as Array).has(surface)
		or String(admission.get("sourceIni", "")) == ""
		or int(admission.get("line", 0)) <= 0
	):
		return {}
	return admission.duplicate(true)


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
	# A created hero's TextLabel is the player's chosen name - literal display
	# text, never a retail string-table key. Route it through the HUD's authored
	# -label path (label_id "", authored_fallback) so it is shown verbatim rather
	# than looked up and failing "Required localized string ... is missing".
	var created_hero := (registration.get("createAHero", {}) as Dictionary).size() > 0
	var output: Array[Dictionary] = []
	for producer in producer_bindings(document):
		var command_row: Dictionary = commands_by_id.get(String(producer.get("command_id", "")), {})
		var fields: Dictionary = command_row.get("fields", {}) as Dictionary
		var button_image := _first_string(fields.get("ButtonImage", []))
		var label_id := _first_string(fields.get("TextLabel", []))
		var tooltip_id := _first_string(fields.get("DescriptLabel", []))
		if button_image == "" or label_id == "" or tooltip_id == "":
			return []
		var spec := {
			"unit_id": runtime_unit_id(document),
			"source_object_id": source_id,
			"producer_source_object_id": String(producer.get("producer_source_object_id", "")),
			"producer_runtime_id": String(producer.get("producer_runtime_id", "")),
			"button_name": "Train_" + _slug(source_id),
			"fallback_tooltip": String(string_bindings.get(tooltip_id, tooltip_id)),
			"image_source_size": (image_metadata.get(button_image, {}) as Dictionary).duplicate(),
			"image_id": button_image,
			"tooltip_id": tooltip_id,
			"portrait_image_id": portrait_id,
			"command_id": String(command_row.get("commandId", "")),
			"surface": String(producer.get("surface", "")),
			"slot": int(producer.get("slot", 0)),
			"roster_ordinal": int(producer.get("roster_ordinal", 0)),
		}
		if created_hero:
			spec["label_id"] = ""
			spec["fallback_label"] = label_id
			spec["authored_fallback"] = true
		else:
			spec["label_id"] = label_id
			spec["fallback_label"] = String(string_bindings.get(label_id, label_id))
		output.append(spec)
	return output


static func simulation_rule(document: Dictionary, require_producer: bool = true) -> Dictionary:
	## Importers must resolve authoritative numbers before runtime. Raw macro,
	## WeaponTemplate, Locomotor or Armor ids are evidence, not simulation values.
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var simulation: Variant = registration.get("simulation", {})
	if typeof(simulation) != TYPE_DICTIONARY:
		return {}
	var row := simulation as Dictionary
	var resolved: Dictionary = {}
	var slow_death_core_pending := false
	if row.has("status"):
		var status := String(row.get("status", ""))
		var missing: Array = row.get("missing", []) as Array
		slow_death_core_pending = (
			status == "unresolved"
			and missing == ["moduleContracts.SlowDeathBehavior"]
		)
		if (
			(status != "ready" and not slow_death_core_pending)
			or typeof(row.get("resolved")) != TYPE_DICTIONARY
		):
			return {}
		resolved = row.get("resolved", {}) as Dictionary
		row = {
			"displayName": _resolved_value(resolved.get("displayNameId")),
			"buildCost": _resolved_value(resolved.get("buildCost")),
			"buildTimeSeconds": _resolved_value(resolved.get("buildTimeSeconds")),
			"commandPoints": _resolved_value(resolved.get("commandPoints")),
			"memberCount": _resolved_value(resolved.get("memberCount")),
			"memberHealth": _resolved_value(resolved.get("memberHealth")),
			"speed": _resolved_value(resolved.get("speed")),
			"visionRange": _resolved_value(resolved.get("visionRange")),
			"combat": _flatten_combat(resolved.get("combat", {})),
			"movement": _resolved_dictionary(resolved.get("movement", {})),
			"formation": resolved.get("formation", {}),
			"fearResistant": _resolved_value(resolved.get("fearResistant")),
			"weaponModes": _resolved_weapon_modes(resolved.get("weaponModes", {})),
		}
		if resolved.has("bountyValue"):
			row["bountyValue"] = _resolved_value(resolved.get("bountyValue"))
		if resolved.has("permanentWeaponLocks"):
			row["permanentWeaponLocks"] = resolved["permanentWeaponLocks"]
		for body_field in ["innateArmorScalar", "autoHealMultiplier"]:
			if resolved.has(body_field):
				row[body_field] = _resolved_value(resolved.get(body_field))
		if resolved.has("destroyDie"):
			row["destroyDie"] = resolved["destroyDie"]
		if resolved.has("slowDeaths"):
			row["slowDeaths"] = resolved["slowDeaths"]
		if _resolved_value(resolved.get("highlanderBody")) == true:
			row["highlanderBody"] = true
		var auto_acquire := _resolved_dictionary(
			resolved.get("autoAcquireEnemiesWhenIdle", {})
		)
		if not auto_acquire.is_empty():
			row["autoAcquireEnemiesWhenIdle"] = auto_acquire
		var mood_rate := _resolved_dictionary(resolved.get("moodAttackCheckRate", {}))
		if not mood_rate.is_empty():
			row["moodAttackCheckRate"] = mood_rate
		# ShroudClearingRange, the DESHROUD radius - a range of its own, never a
		# function of VisionRange. Retail's own objects disagree constantly
		# (MenFortressCitadel is VisionRange 400 / ShroudClearingRange 800), and it
		# is compiled off the HORDE container, not the member: the member value is
		# SHROUD_CLEAR_STANDARD 25 precisely so members do not each deshroud.
		#
		# OPTIONAL, and absent must stay absent. 352 shipped objects author no
		# deshroud range and Carn Dum authors an explicit 0 for nine props, so a
		# defaulted value would be indistinguishable from an authored one - and it
		# would add a key to the hashed entity row, which the 3000-tick pin notices.
		var shroud_clearing: Variant = _resolved_value(resolved.get("shroudClearingRange"))
		if typeof(shroud_clearing) in [TYPE_INT, TYPE_FLOAT] and float(shroud_clearing) >= 0.0:
			row["shroudClearingRange"] = shroud_clearing
		var crush := _resolved_dictionary(resolved.get("crush", {}))
		if not crush.is_empty():
			row["crush"] = crush
			if crush.has("crusherLevel"):
				row["crusher_level"] = int(crush["crusherLevel"])
			if crush.has("crushableLevel"):
				row["crushable_level"] = int(crush["crushableLevel"])
			if crush.has("crushWeaponId"):
				row["crush_weapon_id"] = String(crush["crushWeaponId"])
			if crush.has("crushDamage"):
				row["crush_damage"] = int(crush["crushDamage"])
			if crush.has("minCrushVelocityPercent"):
				row["min_crush_velocity_percent"] = float(crush["minCrushVelocityPercent"])
			if crush.has("crushDecelerationPercent"):
				row["crush_deceleration_percent"] = float(crush["crushDecelerationPercent"])
			if crush.has("crushKnockback"):
				row["crush_knockback"] = float(crush["crushKnockback"])
			if crush.has("crushRevengeWeaponId"):
				row["crush_revenge_weapon_id"] = String(crush["crushRevengeWeaponId"])
			if crush.has("crushRevengeDamage"):
				row["crush_revenge_damage"] = int(crush["crushRevengeDamage"])
		if typeof(resolved.get("stances")) == TYPE_DICTIONARY:
			row["stances"] = (resolved.get("stances", {}) as Dictionary).duplicate(true)
	var scenario_only: bool = (
		not require_producer
		and _resolved_value(resolved.get("scenarioOnly")) == true
		and String((resolved.get("scenarioOnly", {}) as Dictionary).get("disposition", ""))
		== "explicit-scenario-admission"
	)
	# The importer keeps passive wildlife unresolved until this exact typed
	# runtime consumer is independently accepted. Admit only that one missing
	# seam, and only on explicit scenario-only objects; every other unresolved
	# simulation remains fail-closed.
	if slow_death_core_pending and not scenario_only:
		return {}
	var required_fields := ["displayName", "memberCount", "memberHealth", "speed", "visionRange"]
	if not scenario_only:
		required_fields.append_array(["buildCost", "buildTimeSeconds", "commandPoints"])
	for field in required_fields:
		if not row.has(field):
			return {}
	if (
		String(row.displayName).strip_edges() == ""
		or (not scenario_only and int(row.buildCost) < 0)
		or (not scenario_only and float(row.buildTimeSeconds) <= 0.0)
		or (not scenario_only and int(row.commandPoints) <= 0 and not is_ring_hero_summon(document))
		or int(row.memberCount) <= 0
		or int(row.memberHealth) <= 0
		or float(row.speed) < 0.0
		or float(row.visionRange) <= 0.0
	):
		return {}
	var producers := producer_bindings(document)
	if producers.is_empty() and require_producer:
		return {}
	var display_name := String(row.displayName)
	# The display name arrives as a source string-table id (OBJECT:ElvenElrond);
	# the pack's reviewed string bindings localize it when they cover the id.
	var string_bindings: Dictionary = registration.get("stringBindings", {}) as Dictionary
	display_name = String(string_bindings.get(display_name, display_name))
	var output := {
		"unit_type": runtime_unit_id(document),
		"object_id": runtime_member_id(document),
		"source_object_id": String(document.get("objectId", "")),
		"category": String(document.get("category", "")),
		"display_name": display_name,
		"member_count": int(row.memberCount),
		"member_health": int(row.memberHealth),
		"speed_source": float(row.speed),
		"vision_range_source": float(row.visionRange),
		"combat": (row.get("combat", {}) as Dictionary).duplicate(true),
		"weapon_modes_source": (row.get("weaponModes", {}) as Dictionary).duplicate(true),
		"movement": (row.get("movement", {}) as Dictionary).duplicate(true),
		"formation": (row.get("formation", {}) as Dictionary).duplicate(true),
		"fear_resistant": row.get("fearResistant") == true,
		"producers": producers,
		"prerequisites": (producers[0].get("prerequisites", []) as Array).duplicate() if not producers.is_empty() else [],
	}
	if scenario_only:
		output["scenario_only"] = true
	else:
		output["default_cost"] = int(row.buildCost)
		output["default_build_ticks"] = maxi(1, roundi(float(row.buildTimeSeconds) / TICK_SECONDS))
		output["default_command_points"] = int(row.commandPoints)
	if row.has("bountyValue"):
		if float(row.bountyValue) < 0.0 or float(row.bountyValue) != float(int(row.bountyValue)):
			return {}
		output["bounty_value"] = int(row.bountyValue)
	if row.has("shroudClearingRange"):
		output["shroud_clearing_range_source"] = float(row["shroudClearingRange"])
	if row.has("destroyDie"):
		var destroy_die := _resolved_destroy_die(row.get("destroyDie"))
		if destroy_die.is_empty() and not _destroy_die_is_deferred_primary_member_only(
			row.get("destroyDie")
		):
			return {}
		if not destroy_die.is_empty():
			output["destroy_die"] = destroy_die
	if row.has("slowDeaths"):
		var slow_death_fades := _resolved_slow_death_fades(row.get("slowDeaths"))
		if not slow_death_fades.is_empty():
			output["slow_death_fades"] = slow_death_fades
	if row.has("permanentWeaponLocks"):
		var permanent_locks := _normalized_permanent_weapon_locks(
			row.get("permanentWeaponLocks")
		)
		var combat_slot := String(
			(row.get("combat", {}) as Dictionary).get("weaponSlot", "")
		).to_lower()
		if permanent_locks.is_empty() or not permanent_locks.has(combat_slot):
			return {}
		output["permanent_weapon_locks"] = permanent_locks
	if row.get("highlanderBody") == true:
		output["highlander_body"] = true
	# INNATE body properties: a damage-TAKEN scalar and a regeneration-rate
	# scalar the object carries for its whole life, as against the timed
	# ability/aura modifiers the sim already tracks per entity. Absent unless the
	# document authors them, and neutral values are dropped, so nothing an
	# ordinary retail unit produces changes shape.
	var innate_armor := float(row.get("innateArmorScalar", 1.0))
	if is_finite(innate_armor) and innate_armor >= 0.0 and not is_equal_approx(innate_armor, 1.0):
		output["innate_armor_scalar"] = innate_armor
	var auto_heal := float(row.get("autoHealMultiplier", 1.0))
	if is_finite(auto_heal) and auto_heal >= 0.0 and not is_equal_approx(auto_heal, 1.0):
		output["auto_heal_multiplier"] = auto_heal
	if typeof(row.get("autoAcquireEnemiesWhenIdle")) == TYPE_DICTIONARY:
		output["auto_acquire_enemies_when_idle"] = (
			row.get("autoAcquireEnemiesWhenIdle", {}) as Dictionary
		).duplicate(true)
	if typeof(row.get("moodAttackCheckRate")) == TYPE_DICTIONARY:
		var mood_rate := row.get("moodAttackCheckRate", {}) as Dictionary
		var milliseconds := int(mood_rate.get("milliseconds", 0))
		if (
			milliseconds > 0
			and typeof(row.get("autoAcquireEnemiesWhenIdle")) == TYPE_DICTIONARY
		):
			output["mood_attack_check_rate_ms"] = milliseconds
	# Crush is flattened onto `row` from resolved.crush above. Copy it onto
	# the returned rule so normalized_unit_rule / _add_battalion can see it.
	# The previous pass wrote the keys on `row` and then dropped them.
	_copy_optional_crush_fields(output, row)
	_copy_optional_kind_of(output, document)
	if typeof(row.get("stances")) == TYPE_DICTIONARY:
		output["stances"] = (row.get("stances", {}) as Dictionary).duplicate(true)
	return output


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
	var noncombatant: bool = (
		bool(simulation.get("scenario_only", false))
		and String(combat.get("disposition", "")) == "noncombatant"
	)
	var formation: Dictionary = simulation.get("formation", {}) as Dictionary
	for field in ["acceleration", "braking", "turnRateDegreesPerSecond"]:
		if not movement.has(field):
			return {}
	if not noncombatant:
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
	# -1.0 sentinel, not 0.0: an authored ShroudClearingRange of 0 is real (Carn
	# Dum's map.ini sets nine props to exactly that) and must survive as 0 rather
	# than being read as "not authored".
	var shroud_clearing := float(simulation.get("shroud_clearing_range_source", -1.0))
	var acceleration := float(movement.get("acceleration", -1.0)) * HORDE_LOCOMOTION_RESPONSE_SCALE
	var braking := float(movement.get("braking", -1.0)) * HORDE_LOCOMOTION_RESPONSE_SCALE
	var turn_rate := float(movement.get("turnRateDegreesPerSecond", -1.0))
	var max_turn_without_reform := float(movement.get("maxTurnWithoutReformDegrees", 0.0))
	var attack_range := float(combat.get("attackRange", 0.0 if noncombatant else -1.0))
	var minimum_range := float(combat.get("minimumAttackRange", 0.0))
	var delay_ms := float(combat.get("delayBetweenShotsMs", 0.0 if noncombatant else -1.0))
	var pre_attack_ms := float(combat.get("preAttackDelayMs", 0.0 if noncombatant else -1.0))
	var firing_ms := float(combat.get("firingDurationMs", 0.0 if noncombatant else -1.0))
	var damage := int(combat.get("damage", 0))
	for numeric in [speed, vision, acceleration, braking, turn_rate, attack_range, delay_ms, pre_attack_ms, firing_ms]:
		if not is_finite(float(numeric)) or float(numeric) < 0.0:
			return {}
	if damage <= 0 and not noncombatant:
		return {}
	var period_ms := delay_ms
	var clip_reload := _resolved_clip_reload_ms(combat)
	var clip_reload_ms := float(clip_reload["reload_ms"])
	if period_ms <= 0.0 and clip_reload_ms > 0.0:
		period_ms = clip_reload_ms
	var pre_attack := _resolved_pre_attack_type(combat)
	var category := String(simulation.get("category", ""))
	# Compiled alternate weapon-mode profiles (WEAPONSET_TOGGLE_* / MOUNTED):
	# each converts into the same scaled shape the sim's weapon-mode table
	# consumes. A profile that fails validation is omitted — the runtime cast
	# then fails closed (toggle-mode-unavailable) instead of half-swapping.
	var weapon_modes: Dictionary = {}
	var modes_source: Dictionary = simulation.get("weapon_modes_source", {}) as Dictionary
	var mode_keys: Array = modes_source.keys()
	mode_keys.sort()
	for mode_key_value in mode_keys:
		var mode_key := String(mode_key_value)
		var entry := _normalized_weapon_mode(mode_key, modes_source.get(mode_key_value, {}) as Dictionary, source_scale)
		if not entry.is_empty():
			weapon_modes[mode_key] = entry
	if not weapon_modes.is_empty():
		weapon_modes["default"] = {
			"name": String(combat.get("weaponId", "default")),
			"weapon_slot": String(combat.get("weaponSlot", "")).to_lower(),
			"attack_range": attack_range * source_scale,
			"attack_range_source": attack_range,
			"minimum_attack_range": minimum_range * source_scale,
			"minimum_attack_range_source": minimum_range,
			"delay_between_shots_ms": delay_ms,
			"pre_attack_delay_ms": pre_attack_ms,
			"pre_attack_type": String(pre_attack["type"]),
			"pre_attack_random_amount_ms": float(combat.get("preAttackRandomAmountMs", 0.0)),
			"firing_duration_ms": firing_ms,
			"attack_period_ticks": maxi(1, roundi(period_ms / (TICK_SECONDS * 1000.0))),
			"pre_attack_ticks": maxi(0, roundi(pre_attack_ms / (TICK_SECONDS * 1000.0))),
			"firing_duration_ticks": maxi(0, roundi(firing_ms / (TICK_SECONDS * 1000.0))),
			"member_damage": damage,
			"clip_size": int(combat.get("clipSize", 0)),
			"clip_reload_time_ms": clip_reload_ms,
			"continuous_fire_one": int(combat.get("continuousFireOne", 0)),
			"continuous_fire_coast_ticks": maxi(0, roundi(float(combat.get("continuousFireCoastMs", 0.0)) / (TICK_SECONDS * 1000.0))),
			"continuous_fire_rate_multiplier": 1.0,
		}
	var output := {
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
		"pre_attack_type": String(pre_attack["type"]),
		"pre_attack_random_amount_ms": float(combat.get("preAttackRandomAmountMs", 0.0)),
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
		"weapon_modes": weapon_modes,
		"default_weapon_mode": "default",
		"default_weapon_slot": String(combat.get("weaponSlot", "")).to_lower(),
		"formation_positions": positions,
		"provenance": {
			"source_object_id": String(simulation.get("source_object_id", "")),
			"source_contract": "openbfme.playable-unit-runtime",
			# Loud by construction (AGENTS.md rule 5): when the clip reload had
			# to come from the ContinuousFireCoast bridge instead of the cooked
			# ClipReloadTime, the rule itself says so.
			"clip_reload_source": String(clip_reload["source"]),
			"pre_attack_type_source": String(pre_attack["source"]),
		},
	}
	if noncombatant:
		output["noncombatant"] = true
	var permanent_locks: Array = simulation.get("permanent_weapon_locks", []) as Array
	if not permanent_locks.is_empty():
		if not permanent_locks.has(String(output["default_weapon_slot"])):
			return {}
		for mode_value in weapon_modes.values():
			if String((mode_value as Dictionary).get("weapon_slot", "")) == "":
				return {}
		output["permanent_weapon_locks"] = permanent_locks.duplicate()
	if shroud_clearing >= 0.0:
		# Scaled by the map transform exactly as vision is, and kept alongside its
		# source value so the fog pass can be checked against the retail INI
		# without dividing back out.
		output["shroud_clearing_range"] = shroud_clearing * source_scale
		output["shroud_clearing_range_source"] = shroud_clearing
	if max_turn_without_reform > 0.0:
		output["max_turn_without_reform_degrees"] = max_turn_without_reform
	if turn_rate > 0.0 and movement.has("turnRateDegreesPerSecond"):
		# Provenance, not a gameplay flag: pin fixtures invent 180 deg/s with
		# no source and must keep snapping. Live locomotor TurnTime sets this.
		output["turn_rate_source"] = "locomotor"
	if movement.has("waitForFormation"):
		output["wait_for_formation"] = bool(movement.get("waitForFormation"))
	if combat.has("flankingBonus"):
		var flanking_bonus := float(combat.get("flankingBonus", 0.0))
		if is_finite(flanking_bonus) and flanking_bonus > 0.0:
			output["flanking_bonus"] = flanking_bonus
	if typeof(simulation.get("stances")) == TYPE_DICTIONARY:
		output["stances"] = (simulation.get("stances", {}) as Dictionary).duplicate(true)
	var kind_tokens: Variant = simulation.get("kind_of", [])
	if typeof(kind_tokens) == TYPE_ARRAY and not (kind_tokens as Array).is_empty():
		output["kind_of"] = (kind_tokens as Array).duplicate()
	_copy_optional_crush_fields(output, simulation)
	if simulation.get("highlander_body") == true:
		output["highlander_body"] = true
	for body_field in ["innate_armor_scalar", "auto_heal_multiplier"]:
		if simulation.has(body_field):
			output[body_field] = float(simulation[body_field])
	if simulation.has("destroy_die"):
		var destroy_die := _normalized_destroy_die(simulation.get("destroy_die"))
		if destroy_die.is_empty():
			return {}
		output["destroy_die"] = destroy_die
	var auto_acquire: Variant = simulation.get("auto_acquire_enemies_when_idle")
	if typeof(auto_acquire) == TYPE_DICTIONARY:
		var auto_row := auto_acquire as Dictionary
		if (
			typeof(auto_row.get("enabled")) == TYPE_BOOL
			and typeof(auto_row.get("attackBuildings")) == TYPE_BOOL
			and typeof(auto_row.get("whileStealthed")) == TYPE_BOOL
		):
			output["auto_acquire_enabled"] = bool(auto_row["enabled"])
			output["auto_acquire_attack_buildings"] = bool(auto_row["attackBuildings"])
			output["auto_acquire_while_stealthed"] = bool(auto_row["whileStealthed"])
	var mood_rate_ms := int(simulation.get("mood_attack_check_rate_ms", 0))
	if (
		mood_rate_ms > 0
		and output.has("auto_acquire_enabled")
		and output.has("auto_acquire_attack_buildings")
		and output.has("auto_acquire_while_stealthed")
	):
		output["mood_attack_check_rate_ticks"] = maxi(
			1, roundi(float(mood_rate_ms) / (TICK_SECONDS * 1000.0))
		)
	return output


static func _coast_expression(combat: Dictionary) -> String:
	var expression := String(combat.get("continuousFireCoastExpression", "")).strip_edges()
	if expression != "":
		return expression
	var raw: Variant = combat.get("continuousFireCoastMs")
	if typeof(raw) == TYPE_DICTIONARY:
		return String((raw as Dictionary).get("expression", "")).strip_edges()
	return ""


static func _coast_is_reloadtime_max_token(combat: Dictionary) -> bool:
	## 34 of 35 shipped clip-1 combos author ContinuousFireCoast as a
	## `*_RELOADTIME_MAX` token (GondorArcherBow weapon.ini:4241
	## GONDOR_ARCHER_BOW_RELOADTIME_MAX). The 35th and the no-coast weapons
	## are NOT a valid proxy — see `_resolved_clip_reload_ms`.
	var token := _coast_expression(combat)
	token = token.split(";", 1)[0].split("//", 1)[0].strip_edges().to_upper()
	return token.ends_with("_RELOADTIME_MAX") or token == "RELOADTIME_MAX"


static func _resolved_pre_attack_type(combat: Dictionary) -> Dictionary:
	var authored := String(combat.get("preAttackType", "")).strip_edges().to_upper()
	if authored in ["PER_POSITION", "PER_SHOT", "PER_ATTACK"]:
		return {"type": authored, "source": "preAttackType"}
	# Pre-cook bridge: current packs do not compile PreAttackType. Retail
	# clip-1 delay-0 bows whose coast is the *_RELOADTIME_MAX token are
	# PER_POSITION (GondorArcherBow weapon.ini:4233, MordorArcherBow :10402).
	# WildSpiderRiderBow is PER_SHOT and authors no coast (commented out at
	# weapon.ini:15729) — it must not take this bridge.
	if (
		int(combat.get("clipSize", 0)) == 1
		and float(combat.get("delayBetweenShotsMs", -1.0)) <= 0.0
		and _coast_is_reloadtime_max_token(combat)
	):
		return {"type": "PER_POSITION", "source": "clip-1-coast-reloadmax-proxy"}
	return {"type": "PER_SHOT", "source": "default-per-shot"}


static func _resolved_clip_reload_ms(combat: Dictionary) -> Dictionary:
	## Clip reload drives the shot cadence of ClipSize = 1 weapons: the sim
	## substitutes it for DelayBetweenShots (retail_slice_sim.gd
	## `_step_member_attacks`). Retail authors it as a RANGED value —
	## `ClipReloadTime = Min:1500 Max:2000` (weapon.ini:4239 GondorArcherBow,
	## :10409 MordorArcherBow) — and pack builds from before the importer
	## learned that form carry no `clipReloadTimeMs` at all.
	##
	## ContinuousFireCoast is NOT a universal stand-in. Five retail
	## counterexamples:
	##   * CreateAHeroBasicRangedWeapon (weapon.ini:19009) coast = 2000
	##     (FARAMIR_BOW_RELOADTIME_MAX) vs ClipReloadTime Max 1500
	##   * LegolasHawkStrike (:135) — no coast
	##   * DwarvenMenOfDaleBlackArrows (:6734) — no coast
	##   * GoblinArcherPoisonArrows (:18014) — no coast
	##   * WildSpiderRiderBow (:15729) — coast deliberately commented out
	## 34 of 35 shipped combos author coast as a *_RELOADTIME_MAX token; the
	## proxy is legal ONLY then. Otherwise use the compiled clipReloadTimeMs
	## (now that the importer parses Min/Max) or fail loud — never a silent 0.
	var reload_ms := float(combat.get("clipReloadTimeMs", 0.0))
	if reload_ms > 0.0:
		return {"reload_ms": reload_ms, "source": "clipReloadTimeMs"}
	var needs_clip_reload := (
		int(combat.get("clipSize", 0)) == 1
		and float(combat.get("delayBetweenShotsMs", -1.0)) <= 0.0
	)
	if needs_clip_reload and _coast_is_reloadtime_max_token(combat):
		var coast_ms := float(combat.get("continuousFireCoastMs", 0.0))
		if coast_ms > 0.0:
			return {"reload_ms": coast_ms, "source": "continuousFireCoastMs-proxy"}
	if needs_clip_reload:
		push_error(
			"playable_unit_runtime_adapter: clip-1 DelayBetweenShots=0 weapon has no clipReloadTimeMs and ContinuousFireCoast is not an authored *_RELOADTIME_MAX token (expression=%s). Refusing a silent 0 ms reload."
			% _coast_expression(combat)
		)
		return {"reload_ms": 0.0, "source": "unresolved"}
	return {"reload_ms": 0.0, "source": "none"}


static func _normalized_weapon_mode(mode_key: String, profile: Dictionary, source_scale: float) -> Dictionary:
	## One compiled alternate weapon profile -> the sim's weapon-mode entry.
	## Fail-closed: any unresolvable field rejects the whole mode.
	if profile.is_empty() or mode_key == "" or mode_key == "default":
		return {}
	var attack_range := float(profile.get("attackRange", -1.0))
	var minimum_range := float(profile.get("minimumAttackRange", 0.0))
	var delay_ms := float(profile.get("delayBetweenShotsMs", -1.0))
	var pre_attack_ms := float(profile.get("preAttackDelayMs", -1.0))
	var firing_ms := float(profile.get("firingDurationMs", -1.0))
	var damage := int(profile.get("damage", 0))
	for numeric in [attack_range, minimum_range, delay_ms, pre_attack_ms, firing_ms]:
		if not is_finite(float(numeric)) or float(numeric) < 0.0:
			return {}
	if damage <= 0:
		return {}
	var period_ms := delay_ms
	var clip_reload_ms := float(_resolved_clip_reload_ms(profile)["reload_ms"])
	if period_ms <= 0.0 and clip_reload_ms > 0.0:
		period_ms = clip_reload_ms
	var pre_attack := _resolved_pre_attack_type(profile)
	return {
		"name": String(profile.get("weaponId", mode_key)),
		"weapon_slot": String(profile.get("weaponSlot", "")).to_lower(),
		"attack_range": attack_range * source_scale,
		"attack_range_source": attack_range,
		"minimum_attack_range": minimum_range * source_scale,
		"minimum_attack_range_source": minimum_range,
		"delay_between_shots_ms": delay_ms,
		"pre_attack_delay_ms": pre_attack_ms,
		"pre_attack_type": String(pre_attack["type"]),
		"pre_attack_random_amount_ms": float(profile.get("preAttackRandomAmountMs", 0.0)),
		"firing_duration_ms": firing_ms,
		"attack_period_ticks": maxi(1, roundi(period_ms / (TICK_SECONDS * 1000.0))),
		"pre_attack_ticks": maxi(0, roundi(pre_attack_ms / (TICK_SECONDS * 1000.0))),
		"firing_duration_ticks": maxi(0, roundi(firing_ms / (TICK_SECONDS * 1000.0))),
		"member_damage": damage,
		"clip_size": int(profile.get("clipSize", 0)),
		"clip_reload_time_ms": clip_reload_ms,
		"continuous_fire_one": int(profile.get("continuousFireOne", 0)),
		"continuous_fire_coast_ticks": maxi(0, roundi(float(profile.get("continuousFireCoastMs", 0.0)) / (TICK_SECONDS * 1000.0))),
		"continuous_fire_rate_multiplier": 1.0,
	}


static func _normalized_permanent_weapon_locks(value: Variant) -> Array[String]:
	if typeof(value) != TYPE_ARRAY:
		return []
	var output: Array[String] = []
	for row_value in value as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			return []
		var row := row_value as Dictionary
		var slot := String(row.get("slot", "")).to_lower()
		var line: Variant = row.get("line")
		if (
			slot != "primary"
			or String(row.get("state", "")) != "LOCKED_PERMANENTLY"
			or String(row.get("module", "")) != "LockWeaponCreate"
			or String(row.get("sourceIni", "")).strip_edges() == ""
			or typeof(line) != TYPE_INT
			or int(line) <= 0
			or output.has(slot)
		):
			return []
		output.append(slot)
	return output


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


static func transport_entry_audio_event_ids(document: Dictionary, carrier_object_id: String) -> Array[String]:
	## Passenger acknowledgement candidates for one accepted ship entry.
	## Retail authors these on the PASSENGER object, not the transport. Keep the
	## specific carrier field ahead of the generic transport field and preserve
	## each descriptor row's authored order. Unknown carrier identities may use
	## only the explicitly authored generic route; no faction guess is made.
	var field_order: Array[String] = []
	match carrier_object_id.to_lower():
		"elventransportship":
			field_order.append("VoiceEnterUnitElvenTransportShip")
		"evilmentransportship":
			field_order.append("VoiceEnterUnitEvilMenTransportShip")
	field_order.append("VoiceEnterUnitTransportShip")
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var routes_value: Variant = registration.get("audioRoutes", {})
	if typeof(routes_value) != TYPE_DICTIONARY:
		return []
	var routes := routes_value as Dictionary
	var owner_order: Array[String] = ["container", "primaryMember"]
	var remaining: Array[String] = []
	for owner_value in routes.keys():
		var owner := String(owner_value)
		if not owner_order.has(owner):
			remaining.append(owner)
	remaining.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	owner_order.append_array(remaining)
	var output: Array[String] = []
	for field in field_order:
		for owner in owner_order:
			var owner_value: Variant = routes.get(owner)
			if typeof(owner_value) != TYPE_DICTIONARY:
				continue
			var owner_routes := owner_value as Dictionary
			for authored_field_value in owner_routes.keys():
				if String(authored_field_value).to_lower() != field.to_lower():
					continue
				for row_value in owner_routes[authored_field_value] as Array:
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
	return experience_rule_from_contract(experience_value)


static func module_contracts(document: Dictionary) -> Array:
	## Project converter moduleContracts into a flat runtime array. Deferred/
	## opaque rows are still indexed so the sim can attach authored evidence
	## without inventing execution. Executable rows (runtimeStatus == executable)
	## are flagged for subsystem consumers.
	## Sources (first non-empty wins):
	##   simulation.resolved.moduleContracts (units)
	##   registration.gameplay.moduleContracts (structures)
	##   registration.moduleContracts (legacy top-level)
	##   document.moduleContracts (neutral scenario props)
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var simulation: Dictionary = registration.get("simulation", {}) as Dictionary
	var resolved: Dictionary = simulation.get("resolved", {}) as Dictionary
	var gameplay: Dictionary = registration.get("gameplay", {}) as Dictionary
	var raw: Variant = resolved.get("moduleContracts", null)
	if raw == null or (typeof(raw) == TYPE_ARRAY and (raw as Array).is_empty()):
		raw = gameplay.get("moduleContracts", null)
	if raw == null or (typeof(raw) == TYPE_ARRAY and (raw as Array).is_empty()):
		raw = registration.get("moduleContracts", [])
	if raw == null or (typeof(raw) == TYPE_ARRAY and (raw as Array).is_empty()):
		raw = document.get("moduleContracts", [])
	if typeof(raw) != TYPE_ARRAY:
		return []
	var out: Array = []
	for row_value in raw as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := row_value as Dictionary
		var module_name := String(row.get("module", "")).strip_edges()
		if module_name == "":
			continue
		out.append({
			"module": module_name,
			"fields": (row.get("fields", {}) as Dictionary).duplicate(true),
			"runtime_status": String(row.get("runtimeStatus", "deferred")),
			"extraction": String(row.get("extraction", "")),
			"carrier": String(row.get("carrier", "")),
			"source_ini": String(row.get("sourceIni", "")),
			"line": int(row.get("line", 0)),
			"tag": String(row.get("tag", "")),
			"executable": String(row.get("runtimeStatus", "")) == "executable",
			"effect_graph": (row.get("effectGraph", {}) as Dictionary).duplicate(true),
		})
	return out


static func bezier_trajectory_contract(document: Dictionary) -> Dictionary:
	## Return the one evidence-closed trajectory subgraph, never the deferred
	## impact policy. Flight progress remains an explicit external launch input.
	var found: Dictionary = {}
	for row_value in module_contracts(document):
		var row := row_value as Dictionary
		if String(row.get("module", "")) != "BezierProjectileBehavior":
			continue
		if not found.is_empty():
			return {}
		var fields := row.get("fields", {}) as Dictionary
		var graph := row.get("effect_graph", {}) as Dictionary
		var trajectory := graph.get("trajectory", {}) as Dictionary
		var eligibility := graph.get("executionEligibility", {}) as Dictionary
		var runtime_status := String(row.get("runtime_status", ""))
		var eligibility_status := String(eligibility.get("runtimeStatus", ""))
		if (
			runtime_status not in ["deferred", "executable"]
			or bool(row.get("executable", false)) != (runtime_status == "executable")
			or String(row.get("extraction", "")) != "typed"
			or String(row.get("source_ini", "")).strip_edges() == ""
			or int(row.get("line", 0)) <= 0
			or String(graph.get("kind", "")) != "bezier-projectile"
			or String(trajectory.get("kind", "")) != "cubic-bezier-envelope"
			or String(trajectory.get("runtimeStatus", "")) != "executable"
			or String(trajectory.get("progressAuthority", "")) != "external-authored-projectile-flight"
			or eligibility_status != runtime_status
			or typeof(eligibility.get("blockers")) != TYPE_ARRAY
			or ((eligibility.get("blockers") as Array).is_empty()) != (runtime_status == "executable")
		):
			return {}
		var bindings := {
			"firstHeight": ["FirstHeight", "value"],
			"secondHeight": ["SecondHeight", "value"],
			"firstIndentRatio": ["FirstPercentIndent", "ratio"],
			"secondIndentRatio": ["SecondPercentIndent", "ratio"],
		}
		for graph_key_value in bindings.keys():
			var graph_key := String(graph_key_value)
			var binding := bindings[graph_key] as Array
			var field_value: Variant = fields.get(String(binding[0]))
			if typeof(field_value) != TYPE_DICTIONARY:
				return {}
			var field := field_value as Dictionary
			var scalar: Variant = field.get(String(binding[1]))
			if (
				typeof(scalar) not in [TYPE_INT, TYPE_FLOAT]
				or typeof(trajectory.get(graph_key)) not in [TYPE_INT, TYPE_FLOAT]
				or not is_equal_approx(float(trajectory.get(graph_key)), float(scalar))
				or String(field.get("authored", "")).strip_edges() == ""
				or String(field.get("sourceIni", "")).strip_edges() == ""
				or int(field.get("line", 0)) <= 0
			):
				return {}
		var arrival := graph.get("arrival", {}) as Dictionary
		if runtime_status == "executable":
			var arrival_bindings := {
				"crushStyle": ["CrushStyle", "value"],
				"dieOnImpact": ["DieOnImpact", "value"],
				"tumbleRandomly": ["TumbleRandomly", "value"],
				"bounceCount": ["BounceCount", "value"],
				"bounceDistance": ["BounceDistance", "value"],
				"bounceFirstHeight": ["BounceFirstHeight", "value"],
				"bounceSecondHeight": ["BounceSecondHeight", "value"],
				"bounceFirstIndentRatio": ["BounceFirstPercentIndent", "ratio"],
				"bounceSecondIndentRatio": ["BounceSecondPercentIndent", "ratio"],
				"groundHitFxId": ["GroundHitFX", "value"],
				"groundBounceFxId": ["GroundBounceFX", "value"],
			}
			if (
				String(arrival.get("kind", "")) != "authored-ground-impact-bounce"
				or String(arrival.get("runtimeStatus", "")) != "executable"
				or String(arrival.get("terminalPolicy", "")) not in [
					"remove-on-final-impact", "land-and-clear-projectile-state"
				]
			):
				return {}
			for graph_key_value in arrival_bindings.keys():
				var graph_key := String(graph_key_value)
				var binding := arrival_bindings[graph_key] as Array
				var field_value: Variant = fields.get(String(binding[0]))
				if typeof(field_value) != TYPE_DICTIONARY:
					return {}
				var expected: Variant = (field_value as Dictionary).get(String(binding[1]))
				if arrival.get(graph_key) != expected:
					if (
						typeof(arrival.get(graph_key)) not in [TYPE_INT, TYPE_FLOAT]
						or typeof(expected) not in [TYPE_INT, TYPE_FLOAT]
						or not is_equal_approx(float(arrival.get(graph_key)), float(expected))
					):
						return {}
		elif not arrival.is_empty():
			return {}
		found = {
			"module": "BezierProjectileBehavior",
			"trajectory": trajectory.duplicate(true),
			"deferredBlockers": (eligibility.get("blockers") as Array).duplicate(true),
			"sourceIni": String(row.get("source_ini", "")),
			"line": int(row.get("line", 0)),
			"tag": String(row.get("tag", "")),
			"carrier": String(row.get("carrier", "")),
			"activation": "conditional-explicit-runtime-request",
			"runtimeStatus": runtime_status,
			"arrival": arrival.duplicate(true),
		}
	return found


static func module_contracts_by_kind(document: Dictionary) -> Dictionary:
	## Index module_contracts() by module kind for O(1) lookup at spawn.
	var by_kind: Dictionary = {}
	for row_value in module_contracts(document):
		var row := row_value as Dictionary
		var kind := String(row.get("module", ""))
		if not by_kind.has(kind):
			by_kind[kind] = []
		(by_kind[kind] as Array).append(row)
	return by_kind


static func experience_rule_from_contract(experience_value: Variant) -> Dictionary:
	## Shared projection for playable-unit registration and spellbook summon
	## leaves. Both compiler lanes emit the same strict ExperienceLevel shape.
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
	var creation_grant: Variant = experience.get("experienceLevelCreate")
	var levels: Array[Dictionary] = []
	var previous_rank := 0
	for row_value in levels_value as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			return {}
		var row := row_value as Dictionary
		var rank := int(row.get("rank", 0))
		var required: Variant = row.get("requiredExperience")
		var award: Variant = row.get("experienceAward")
		var award_unknown := (
			award == null
			and String(row.get("experienceAwardStatus", "")) == "unauthored"
			and typeof(creation_grant) == TYPE_DICTIONARY
			and rank <= initial_rank
		)
		# Authored ranks ascend but are not always 1..N (retail summons such as
		# the ring hero enter at their authored top rank); rows keep their
		# authored ranks verbatim.
		if (
			rank <= previous_rank
			or required == null
			or (
				not award_unknown
				and award == null
			)
			or (
				award != null
				and row.has("experienceAwardStatus")
			)
		):
			return {}
		previous_rank = rank
		var health_add := 0.0
		var damage_add := 0.0
		var damage_mult := 1.0
		var spell_damage_mult := 1.0
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
					"DAMAGE_MULT":
						damage_mult *= float(modifier.get("value", 1.0))
					"SPELL_DAMAGE":
						spell_damage_mult *= float(modifier.get("value", 1.0))
					"PRODUCTION":
						production_mult *= float(modifier.get("value", 1.0))
					_:
						return {}
			for unsupported_value in Array(leaf.get("unsupportedModifiers", [])):
				unsupported.append(String(unsupported_value))
		var upgrades_value: Variant = row.get("upgrades", [])
		if typeof(upgrades_value) != TYPE_ARRAY:
			return {}
		var upgrades: Array[String] = []
		for upgrade_value in upgrades_value as Array:
			var upgrade_id := String(upgrade_value)
			if upgrade_id == "" or upgrades.has(upgrade_id):
				return {}
			upgrades.append(upgrade_id)
		var level_row: Dictionary = {
			"rank": rank,
			"required_experience": int(required),
			"experience_award": 0 if award_unknown else int(award),
			"experience_award_known": not award_unknown,
			"health_add": health_add,
			"damage_add": damage_add,
			"damage_multiplier": damage_mult,
			"spell_damage_multiplier": spell_damage_mult,
			"production_multiplier": production_mult,
			"upgrades": upgrades,
		}
		if not unsupported.is_empty():
			level_row["unsupported_modifiers"] = unsupported
		if String(row.get("selectionDecalTextureId", "")) != "":
			level_row["selection_decal_texture_id"] = String(row.get("selectionDecalTextureId", ""))
		if row.has("experienceAwardOwnGuysDie"):
			var own_guys_award: Variant = row.get("experienceAwardOwnGuysDie")
			if typeof(own_guys_award) not in [TYPE_INT, TYPE_FLOAT] or float(own_guys_award) < 0.0:
				return {}
			level_row["experience_award_own_guys_die"] = int(own_guys_award)
		for pair_value in [
			["selectionDecal", "selection_decal"],
			["levelUpPresentation", "level_up_presentation"],
		]:
			var pair: Array = pair_value
			var authored: Variant = row.get(String(pair[0]))
			if authored == null:
				continue
			if typeof(authored) != TYPE_DICTIONARY:
				return {}
			level_row[String(pair[1])] = (authored as Dictionary).duplicate(true)
		levels.append(level_row)
	var grant_rank_rows := 0
	for level_row in levels:
		if int(level_row.get("rank", 0)) == initial_rank:
			grant_rank_rows += 1
	if (
		previous_rank != max_level
		or (
			creation_grant == null
			and initial_rank != 1
		)
		or (
			creation_grant != null
			and (
				typeof(creation_grant) != TYPE_DICTIONARY
				or int((creation_grant as Dictionary).get("rank", 0)) != initial_rank
				or String((creation_grant as Dictionary).get("module", "")) != "ExperienceLevelCreate"
				or (creation_grant as Dictionary).get("mpOnly") != false
				or String((creation_grant as Dictionary).get("sourceIni", "")).strip_edges() == ""
				or typeof((creation_grant as Dictionary).get("line")) != TYPE_INT
				or int((creation_grant as Dictionary).get("line", 0)) <= 0
				or grant_rank_rows != 1
			)
		)
	):
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
		if ability_id == "" or slot < 1 or targeting not in ["self", "point", "enemy-object", "object"]:
			return []
		var button: Dictionary = row.get("button", {}) as Dictionary
		var effect: Dictionary = row.get("effect", {}) as Dictionary
		var effect_kind := String(effect.get("kind", ""))
		if effect_kind not in ["none", "weapon-blast", "heal", "summon", "attribute-modifier", "leadership-aura", "weapon-toggle", "terror", "mount-toggle", "capture-building", "experience-grant", "arrow-storm", "stealth-toggle", "teleport", "curse", "leadership-strip", "activate-module-graph", "weapon-mode-special-power", "dominate-enemy", "grab-passenger", "fling-passenger", "repair-structure", "stop-special-power", "siege-deploy", "toggle-deploy", "special-disguise", "unleash-special-power"]:
			return []
		if effect_kind == "activate-module-graph" and not _valid_activate_module_graph(effect):
			return []
		if effect_kind == "weapon-mode-special-power" and not _valid_weapon_mode_special_power(effect):
			return []
		if effect_kind == "dominate-enemy" and not _valid_dominate_enemy(effect):
			return []
		if effect_kind == "grab-passenger" and not _valid_grab_passenger(effect):
			return []
		if effect_kind == "fling-passenger" and not _valid_fling_passenger(effect):
			return []
		if effect_kind == "repair-structure" and not _valid_repair_structure(effect):
			return []
		if effect_kind == "stop-special-power" and not _valid_stop_special_power(effect):
			return []
		if effect_kind == "siege-deploy" and not _valid_siege_deploy(effect):
			return []
		if effect_kind == "toggle-deploy" and not _valid_toggle_deploy(effect):
			return []
		if effect_kind == "special-disguise":
			if not _valid_special_disguise(effect, document):
				return []
			effect = effect.duplicate(true)
			var disguise_envelope := (document.get("registration", {}) as Dictionary).get("specialDisguisePresentationPrerequisite", {}) as Dictionary
			var disguise_closure := disguise_envelope.get("closure", {}) as Dictionary
			effect["presentationPrerequisiteSha256"] = String(disguise_closure.get("aggregateSha256", ""))
		if effect_kind == "unleash-special-power" and not _valid_unleash_special_power(effect):
			return []
		var implementation: Dictionary = row.get("implementation", {}) as Dictionary
		var status := String(implementation.get("status", ""))
		if status not in ["implemented", "unimplemented", "passive"]:
			return []
		var gate: Dictionary = row.get("levelGate", {}) as Dictionary
		var special_power_contract: Dictionary = row.get("specialPowerContract", {}) as Dictionary
		for bool_key in ["publicTimer", "sharedSyncedTimer"]:
			if special_power_contract.has(bool_key) and typeof(special_power_contract[bool_key]) != TYPE_BOOL:
				return []
		for numeric_key in ["forbiddenObjectRange", "viewObjectRange", "viewObjectDurationMs", "maxCastRange", "unitCost"]:
			if special_power_contract.has(numeric_key) and (
				typeof(special_power_contract[numeric_key]) not in [TYPE_INT, TYPE_FLOAT]
				or not is_finite(float(special_power_contract[numeric_key]))
				or float(special_power_contract[numeric_key]) < 0.0
			):
				return []
		for list_key in ["flags", "objectFilter", "forbiddenObjectFilter", "preventActivationConditions", "unitCostDeathTypes"]:
			if special_power_contract.has(list_key) and typeof(special_power_contract[list_key]) != TYPE_ARRAY:
				return []
		var power_flags: Array = special_power_contract.get("flags", []) as Array
		for flag_value in power_flags:
			if String(flag_value) not in ["LIMIT_DISTANCE", "NEEDS_OBJECT_FILTER", "NO_FORBIDDEN_OBJECTS", "PATHABLE_ONLY"]:
				return []
		if power_flags.has("NEEDS_OBJECT_FILTER") and (special_power_contract.get("objectFilter", []) as Array).is_empty():
			return []
		if power_flags.has("LIMIT_DISTANCE") and float(special_power_contract.get("maxCastRange", 0.0)) <= 0.0:
			return []
		if power_flags.has("PATHABLE_ONLY") and targeting != "point":
			return []
		if power_flags.has("NO_FORBIDDEN_OBJECTS") and (
			(special_power_contract.get("forbiddenObjectFilter", []) as Array).is_empty()
			or float(special_power_contract.get("forbiddenObjectRange", 0.0)) <= 0.0
		):
			return []
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
			"special_power_contract": special_power_contract.duplicate(true),
		})
	return output


static func has_ability_surface(row: Dictionary, ability_specs: Dictionary) -> bool:
	## Converted special powers are keyed by runtime unit id, not by the broad
	## hero/siege/infantry category. The Dwarven Demolisher is the retail proof
	## that a non-hero selected unit can own an authored palantir ability row.
	var unit_id := String(row.get("unit_type", ""))
	return unit_id != "" and ability_specs.has(unit_id)


static func _valid_activate_module_graph(effect: Dictionary) -> bool:
	if String(effect.get("specialPowerTemplateId", "")) == "" or typeof(effect.get("routes")) != TYPE_ARRAY or (effect.get("routes", []) as Array).is_empty():
		return false
	for numeric_key in ["startAbilityRange", "effectRange", "unpackingVariation"]:
		if effect.has(numeric_key) and (typeof(effect[numeric_key]) not in [TYPE_INT, TYPE_FLOAT] or not is_finite(float(effect[numeric_key])) or float(effect[numeric_key]) < 0.0):
			return false
	if effect.has("mustFinishAbility") and typeof(effect["mustFinishAbility"]) != TYPE_BOOL:
		return false
	if effect.has("timingMs") and typeof(effect["timingMs"]) != TYPE_DICTIONARY:
		return false
	var timing := effect.get("timingMs", {}) as Dictionary
	for key in timing:
		if String(key) not in ["StartDelay", "PreparationTime", "PersistentPrepTime", "UnpackTime", "PackTime", "SpecialPowerDuration"] or typeof(timing[key]) not in [TYPE_INT, TYPE_FLOAT] or not is_finite(float(timing[key])) or float(timing[key]) < 0.0:
			return false
	for route_value in effect.get("routes", []) as Array:
		if typeof(route_value) != TYPE_DICTIONARY:
			return false
		var route := route_value as Dictionary
		if String(route.get("moduleTag", "")) == "" or String(route.get("targetMode", "")) not in ["SELF", "CURRENT_TARGET", "LOCATION"] or typeof(route.get("effect")) != TYPE_DICTIONARY:
			return false
		if String((route.get("effect", {}) as Dictionary).get("kind", "none")) not in ["weapon-blast", "heal", "summon", "attribute-modifier", "weapon-toggle", "terror", "mount-toggle", "experience-grant", "arrow-storm", "stealth-toggle", "teleport", "curse", "leadership-strip", "trigger-fx"]:
			return false
	return true


static func _valid_weapon_mode_special_power(effect: Dictionary) -> bool:
	if String(effect.get("specialPowerTemplateId", "")).strip_edges() == "":
		return false
	if typeof(effect.get("durationMs")) not in [TYPE_INT, TYPE_FLOAT]:
		return false
	if not is_finite(float(effect.get("durationMs", -1.0))) or float(effect.get("durationMs", -1.0)) <= 0.0:
		return false
	if typeof(effect.get("startsPaused")) != TYPE_BOOL:
		return false
	if effect.has("weaponSetFlags"):
		if typeof(effect.get("weaponSetFlags")) != TYPE_ARRAY:
			return false
		for flag_value in effect.get("weaponSetFlags", []) as Array:
			if typeof(flag_value) != TYPE_STRING or String(flag_value).strip_edges() == "":
				return false
	if effect.has("lockWeaponSlot") and String(effect.get("lockWeaponSlot", "")) != "SECONDARY":
		return false
	if effect.has("attributeModifier"):
		if typeof(effect.get("attributeModifier")) != TYPE_DICTIONARY:
			return false
		var modifier := effect.get("attributeModifier", {}) as Dictionary
		if String(modifier.get("id", "")).strip_edges() == "" or typeof(modifier.get("modifiers")) != TYPE_ARRAY:
			return false
		if (modifier.get("modifiers", []) as Array).is_empty() or typeof(modifier.get("unsupportedModifiers", [])) != TYPE_ARRAY:
			return false
		for modifier_value in modifier.get("modifiers", []) as Array:
			if typeof(modifier_value) != TYPE_DICTIONARY:
				return false
	return effect.has("attributeModifier") or effect.has("weaponSetFlags") or effect.has("lockWeaponSlot")


static func _valid_dominate_enemy(effect: Dictionary) -> bool:
	if String(effect.get("specialPowerTemplateId", "")).strip_edges() == "":
		return false
	if typeof(effect.get("startAbilityRange")) not in [TYPE_INT, TYPE_FLOAT] or not is_finite(float(effect.get("startAbilityRange", -1.0))) or float(effect.get("startAbilityRange", -1.0)) < 0.0:
		return false
	if String(effect.get("affectsFilter", "")).strip_edges() == "" or typeof(effect.get("permanentlyConvert")) != TYPE_BOOL:
		return false
	if not bool(effect.get("permanentlyConvert", false)):
		if typeof(effect.get("temporaryDefectDurationMs")) not in [TYPE_INT, TYPE_FLOAT] or not is_finite(float(effect.get("temporaryDefectDurationMs", -1.0))) or float(effect.get("temporaryDefectDurationMs", -1.0)) <= 0.0:
			return false
	if typeof(effect.get("unpackingVariation")) != TYPE_INT or int(effect.get("unpackingVariation", -1)) < 0:
		return false
	if effect.has("dominateRadius") and (typeof(effect.get("dominateRadius")) not in [TYPE_INT, TYPE_FLOAT] or not is_finite(float(effect.get("dominateRadius", -1.0))) or float(effect.get("dominateRadius", -1.0)) < 0.0):
		return false
	if effect.has("timingMs"):
		if typeof(effect.get("timingMs")) != TYPE_DICTIONARY:
			return false
		for key in (effect.get("timingMs", {}) as Dictionary).keys():
			var value: Variant = (effect.get("timingMs", {}) as Dictionary)[key]
			if String(key) not in ["UnpackTime", "PreparationTime", "FreezeAfterTriggerDuration", "TriggerModelConditionDuration"] or typeof(value) not in [TYPE_INT, TYPE_FLOAT] or not is_finite(float(value)) or float(value) < 0.0:
				return false
	if effect.has("triggerModelCondition"):
		if typeof(effect.get("triggerModelCondition")) != TYPE_DICTIONARY:
			return false
		var condition := effect.get("triggerModelCondition", {}) as Dictionary
		if String(condition.get("namespace", "")) == "" or String(condition.get("value", "")) == "":
			return false
	return true


static func _valid_fling_passenger(effect: Dictionary) -> bool:
	if String(effect.get("specialPowerTemplateId", "")).strip_edges() == "" or typeof(effect.get("mustFinishAbility")) != TYPE_BOOL:
		return false
	if typeof(effect.get("timingMs")) != TYPE_DICTIONARY:
		return false
	var timing := effect.get("timingMs", {}) as Dictionary
	if typeof(timing.get("UnpackTime")) not in [TYPE_INT, TYPE_FLOAT] or float(timing.get("UnpackTime", -1.0)) < 0.0:
		return false
	if timing.has("PackTime") and (typeof(timing.get("PackTime")) not in [TYPE_INT, TYPE_FLOAT] or float(timing.get("PackTime", -1.0)) < 0.0):
		return false
	var has_velocity := typeof(effect.get("velocity")) == TYPE_DICTIONARY
	var has_warhead := typeof(effect.get("landingWarhead")) == TYPE_DICTIONARY
	if has_velocity != has_warhead:
		return false
	if has_velocity:
		var velocity := effect.get("velocity", {}) as Dictionary
		for axis in ["x", "y", "z"]:
			if typeof(velocity.get(axis)) not in [TYPE_INT, TYPE_FLOAT] or not is_finite(float(velocity.get(axis, 0.0))):
				return false
		var warhead := effect.get("landingWarhead", {}) as Dictionary
		if String(warhead.get("id", "")) == "" or typeof(warhead.get("radius")) not in [TYPE_INT, TYPE_FLOAT] or float(warhead.get("radius", -1.0)) < 0.0 or String(warhead.get("damageType", "")) == "" or String(warhead.get("deathType", "")) == "" or String(warhead.get("forceKillObjectFilter", "")) == "":
			return false
	if effect.has("customAnimation"):
		if typeof(effect.get("customAnimation")) != TYPE_DICTIONARY:
			return false
		var animation := effect.get("customAnimation", {}) as Dictionary
		if String(animation.get("state", "")) == "" or typeof(animation.get("durationMs")) != TYPE_INT or int(animation.get("durationMs", -1)) < 0:
			return false
	return has_velocity or effect.has("customAnimation")


static func _valid_grab_passenger(effect: Dictionary) -> bool:
	if String(effect.get("specialPowerTemplateId", "")).strip_edges() == "" or typeof(effect.get("updateModuleStartsAttack")) != TYPE_BOOL or typeof(effect.get("allowTree")) != TYPE_BOOL:
		return false
	for key in ["acquire", "containment", "targetAdmission"]:
		if typeof(effect.get(key)) != TYPE_DICTIONARY:
			return false
	var acquire := effect.get("acquire", {}) as Dictionary
	if typeof(acquire.get("startAbilityRange")) not in [TYPE_INT, TYPE_FLOAT] or float(acquire.get("startAbilityRange", -1.0)) < 0.0 or typeof(acquire.get("timingMs")) != TYPE_DICTIONARY or typeof(acquire.get("animation")) != TYPE_DICTIONARY:
		return false
	var acquire_timing := acquire.get("timingMs", {}) as Dictionary
	for timing_key in ["UnpackTime", "PreparationTime", "PersistentPrepTime", "PackTime"]:
		if typeof(acquire_timing.get(timing_key)) not in [TYPE_INT, TYPE_FLOAT] or float(acquire_timing.get(timing_key, -1.0)) < 0.0:
			return false
	var acquire_animation := acquire.get("animation", {}) as Dictionary
	if String(acquire_animation.get("state", "")) == "" or typeof(acquire_animation.get("durationMs")) != TYPE_INT or int(acquire_animation.get("durationMs", -1)) < 0 or typeof(acquire_animation.get("triggerTimeMs")) != TYPE_INT or int(acquire_animation.get("triggerTimeMs", -1)) < 0:
		return false
	var containment := effect.get("containment", {}) as Dictionary
	if typeof(containment.get("slots")) != TYPE_INT or int(containment.get("slots", 0)) < 1 or String(containment.get("passengerFilter", "")) == "":
		return false
	for relation_key in ["allowEnemiesInside", "allowNeutralInside", "allowAlliesInside"]:
		if typeof(containment.get(relation_key)) != TYPE_BOOL:
			return false
	var admission := effect.get("targetAdmission", {}) as Dictionary
	if String(admission.get("passengerFilter", "")) == "" or typeof(admission.get("treeObjectIds", [])) != TYPE_ARRAY:
		return false
	if typeof(effect.get("releaseAbilities", [])) != TYPE_ARRAY:
		return false
	for release_value in effect.get("releaseAbilities", []) as Array:
		if typeof(release_value) != TYPE_DICTIONARY or not _valid_fling_passenger(release_value as Dictionary):
			return false
	return true


static func _valid_repair_structure(effect: Dictionary) -> bool:
	if String(effect.get("specialPowerTemplateId", "")) == "" or typeof(effect.get("targeting")) != TYPE_DICTIONARY or typeof(effect.get("repairRate")) != TYPE_DICTIONARY or typeof(effect.get("contactPoint")) != TYPE_DICTIONARY or typeof(effect.get("economy")) != TYPE_DICTIONARY:
		return false
	var targeting := effect.get("targeting", {}) as Dictionary
	if typeof(targeting.get("kindOf")) != TYPE_ARRAY or String(targeting.get("relation", "")) != "ALLY" or (targeting.get("kindOf", []) as Array) != ["STRUCTURE"] or typeof(targeting.get("requiresDamaged")) != TYPE_BOOL or String(targeting.get("rangeMode", "")) != "REPAIR_CONTACT_POINT":
		return false
	var rate := effect.get("repairRate", {}) as Dictionary
	if String(rate.get("status", "")) not in ["authored", "engine-default-unresolved"]:
		return false
	if String(rate.get("status", "")) == "authored" and (typeof(rate.get("maxHealthFractionPerSecond")) not in [TYPE_INT, TYPE_FLOAT] or float(rate.get("maxHealthFractionPerSecond", -1.0)) <= 0.0):
		return false
	var contact := effect.get("contactPoint", {}) as Dictionary
	return String(contact.get("name", "")) == "Repair" and typeof(contact.get("authored")) == TYPE_BOOL


static func _valid_stop_special_power(effect: Dictionary) -> bool:
	if (
		String(effect.get("specialPowerTemplateId", "")).strip_edges() == ""
		or String(effect.get("stopPowerTemplateId", "")).strip_edges() == ""
		or String(effect.get("targetMode", "")) != "SELF"
		or typeof(effect.get("interruptsCurrentOrder")) != TYPE_BOOL
		or not bool(effect.get("interruptsCurrentOrder", false))
		or typeof(effect.get("linkedModule")) != TYPE_DICTIONARY
	):
		return false
	var linked := effect.get("linkedModule", {}) as Dictionary
	return (
		String(linked.get("kind", "")).strip_edges() != ""
		and String(linked.get("sourceIni", "")).strip_edges() != ""
		and typeof(linked.get("line")) == TYPE_INT
		and int(linked.get("line", 0)) > 0
	)


static func _valid_siege_deploy(effect: Dictionary) -> bool:
	if (
		String(effect.get("specialPowerTemplateId", "")).strip_edges() == ""
		or String(effect.get("targetMode", "")) != "TARGET_STRUCTURE"
		or typeof(effect.get("lowerDelayMs")) != TYPE_INT
		or int(effect.get("lowerDelayMs", -1)) < 0
		or typeof(effect.get("raiseDelayMs")) != TYPE_INT
		or int(effect.get("raiseDelayMs", -1)) < 0
		or typeof(effect.get("evacuatePassengersOnDeploy")) != TYPE_BOOL
		or typeof(effect.get("skipAdjustPosition")) != TYPE_BOOL
		or String(effect.get("initiateSoundId", "")).strip_edges() == ""
		or typeof(effect.get("modelReceipts")) != TYPE_ARRAY
	):
		return false
	if effect.has("extraWallDistanceSource") and (
		typeof(effect.get("extraWallDistanceSource")) not in [TYPE_INT, TYPE_FLOAT]
		or not is_finite(float(effect.get("extraWallDistanceSource", -1.0)))
		or float(effect.get("extraWallDistanceSource", -1.0)) < 0.0
	):
		return false
	return true


static func _valid_toggle_deploy(effect: Dictionary) -> bool:
	## This is the DeployStyleAIUpdate-backed SELF toggle, not the wall-targeted
	## SiegeDeploySpecialPower contract. Times and modifier values remain data-
	## driven for mods, while every runtime-significant leaf is typed fail-closed.
	if (
		String(effect.get("specialPowerTemplateId", "")).strip_edges() == ""
		or String(effect.get("targetMode", "")) != "SELF"
		or typeof(effect.get("ignoreFacingCheck")) != TYPE_BOOL
		or not bool(effect.get("ignoreFacingCheck", false))
		or typeof(effect.get("autoAcquireEnabled")) != TYPE_BOOL
		or typeof(effect.get("autoAcquireModes")) != TYPE_ARRAY
		or typeof(effect.get("mustDeployToAttack")) != TYPE_BOOL
		or typeof(effect.get("moodAttackCheckRateMs")) not in [TYPE_INT, TYPE_FLOAT]
		or not is_finite(float(effect.get("moodAttackCheckRateMs", -1.0)))
		or float(effect.get("moodAttackCheckRateMs", -1.0)) < 0.0
		or typeof(effect.get("unpackTimeMs")) not in [TYPE_INT, TYPE_FLOAT]
		or not is_finite(float(effect.get("unpackTimeMs", -1.0)))
		or float(effect.get("unpackTimeMs", -1.0)) <= 0.0
		or typeof(effect.get("packTimeMs")) not in [TYPE_INT, TYPE_FLOAT]
		or not is_finite(float(effect.get("packTimeMs", -1.0)))
		or float(effect.get("packTimeMs", -1.0)) <= 0.0
		or String(effect.get("deployedAttributeModifierId", "")).strip_edges() == ""
		or String(effect.get("soundDeployId", "")).strip_edges() == ""
		or String(effect.get("soundUndeployId", "")).strip_edges() == ""
		or typeof(effect.get("autoAbility")) != TYPE_BOOL
		or typeof(effect.get("triggerWhenReady")) != TYPE_BOOL
		or typeof(effect.get("autoAbilityBlockedModelConditions")) != TYPE_ARRAY
		or typeof(effect.get("deployStyle")) != TYPE_DICTIONARY
		or typeof(effect.get("deployedAttributeModifier")) != TYPE_DICTIONARY
	):
		return false
	for mode_value in effect.get("autoAcquireModes", []) as Array:
		if typeof(mode_value) != TYPE_STRING or String(mode_value).strip_edges() == "":
			return false
	for condition_value in effect.get("autoAbilityBlockedModelConditions", []) as Array:
		if typeof(condition_value) != TYPE_STRING or String(condition_value).strip_edges() == "":
			return false
	var style := effect.get("deployStyle", {}) as Dictionary
	if (
		String(style.get("tag", "")).strip_edges() == ""
		or String(style.get("sourceIni", "")).strip_edges() == ""
		or typeof(style.get("line")) != TYPE_INT
		or int(style.get("line", 0)) <= 0
	):
		return false
	var modifier := effect.get("deployedAttributeModifier", {}) as Dictionary
	if (
		String(modifier.get("id", "")) != String(effect.get("deployedAttributeModifierId", ""))
		or typeof(modifier.get("modifiers")) != TYPE_ARRAY
		or (modifier.get("modifiers", []) as Array).is_empty()
		or typeof(modifier.get("durationMs")) not in [TYPE_INT, TYPE_FLOAT]
		or float(modifier.get("durationMs", -1.0)) != 0.0
	):
		return false
	for leaf_value in modifier.get("modifiers", []) as Array:
		if typeof(leaf_value) != TYPE_DICTIONARY:
			return false
		var leaf := leaf_value as Dictionary
		if (
			String(leaf.get("kind", "")).strip_edges() == ""
			or String(leaf.get("application", "")) not in ["additive", "multiplicative"]
			or typeof(leaf.get("value")) not in [TYPE_INT, TYPE_FLOAT]
			or not is_finite(float(leaf.get("value", 0.0)))
		):
			return false
	return true


static func _valid_special_disguise(effect: Dictionary, document: Dictionary) -> bool:
	## Runtime activation is independently binary-proven, but presentation is
	## admitted only when the immutable prerequisite envelope binds the same
	## descriptor, module fields, identities, and aggregate digest. The closure
	## remains presentation-only: it never registers a replacement simulation
	## object for either disguise template.
	if (
		String(effect.get("specialPowerTemplateId", "")) == ""
		or String(effect.get("targetMode", "")) != "SELF"
		or String(effect.get("ownerObjectId", "")) != String(document.get("objectId", ""))
		or String(effect.get("ownerDisguiseTemplateId", "")) == ""
		or String(effect.get("hostileDisguiseTemplateId", "")) == ""
		or String(effect.get("disguiseFxId", "")) == ""
		or typeof(effect.get("forceMountedWhenDisguising")) != TYPE_BOOL
		or not bool(effect.get("forceMountedWhenDisguising", false))
		or typeof(effect.get("opacityTarget")) not in [TYPE_INT, TYPE_FLOAT]
		or not is_finite(float(effect.get("opacityTarget", -1.0)))
		or float(effect.get("opacityTarget", -1.0)) < 0.0
		or float(effect.get("opacityTarget", -1.0)) > 1.0
		or typeof(effect.get("deferredBoundaries")) != TYPE_ARRAY
	):
		return false
	for key in ["unpackTimeMs", "preparationTimeMs", "persistentPrepTimeMs", "packTimeMs"]:
		if typeof(effect.get(key)) not in [TYPE_INT, TYPE_FLOAT] or not is_finite(float(effect.get(key, -1.0))) or float(effect.get(key, -1.0)) <= 0.0:
			return false
	var expected_boundaries := ["critical-hit-ordering", "death-reset-ordering", "user1-stealth-ordering", "viewer-perspective"]
	var boundaries: Array = (effect.get("deferredBoundaries", []) as Array).duplicate()
	boundaries.sort()
	if boundaries != expected_boundaries:
		return false
	var registration := document.get("registration", {}) as Dictionary
	var envelope := registration.get("specialDisguisePresentationPrerequisite", {}) as Dictionary
	var closure := envelope.get("closure", {}) as Dictionary
	var digest := String(closure.get("aggregateSha256", ""))
	if (
		String(closure.get("schema", "")) != "openbfme.special-disguise-presentation-prerequisite"
		or int(closure.get("schemaVersion", -1)) != 0
		or String(closure.get("edition", "")) not in ["bfme2", "rotwk"]
		or String(closure.get("runtimeStatus", "")) != "sealed-deferred-no-runtime-activation"
		or closure.get("presentationOnly") != true
		or closure.get("authoritativeEntityRegistration") != false
		or String(closure.get("objectId", "")) != String(document.get("objectId", ""))
		or String(closure.get("descriptorSha256", "")) != String(document.get("descriptorSha256", ""))
		or String(closure.get("aggregateSha256", "")) != digest
		or not _is_lower_sha256(digest)
	):
		return false
	var identities := closure.get("presentationIdentities", {}) as Dictionary
	if (
		String(identities.get("ownerObjectId", "")) != String(effect.get("ownerObjectId", ""))
		or String(identities.get("nonOwnerDisguiseTemplateId", "")) != String(effect.get("ownerDisguiseTemplateId", ""))
		or String(identities.get("hostilePerspectiveTemplateId", "")) != String(effect.get("hostileDisguiseTemplateId", ""))
	):
		return false
	var roles: Array[String] = []
	for leaf_value in closure.get("visualLeafBindings", []) as Array:
		if typeof(leaf_value) != TYPE_DICTIONARY:
			return false
		var role := String((leaf_value as Dictionary).get("role", ""))
		if role != "" and not roles.has(role):
			roles.append(role)
	roles.sort()
	if roles != ["non-owner-disguised-presentation", "owner-base-presentation", "owner-disguised-presentation", "owner-mounted-presentation"]:
		return false
	var receipt := closure.get("moduleReceipt", {}) as Dictionary
	if String(receipt.get("kind", receipt.get("module", ""))) != "SpecialDisguiseUpdate" or typeof(receipt.get("fields")) != TYPE_DICTIONARY:
		return false
	var fields := receipt.get("fields", {}) as Dictionary
	var exact := {
		"SpecialPowerTemplate": String(effect.get("specialPowerTemplateId", "")),
		"DisguiseAsTemplate": String(effect.get("ownerDisguiseTemplateId", "")),
		"DisguisedAsTemplate_EnemyPerspective": String(effect.get("hostileDisguiseTemplateId", "")),
		"DisguiseFX": String(effect.get("disguiseFxId", "")),
	}
	for key in exact:
		if _receipt_token(fields.get(key, {}) as Dictionary) != String(exact[key]):
			return false
	for pair_value in [["UnpackTime", "unpackTimeMs"], ["PreparationTime", "preparationTimeMs"], ["PersistentPrepTime", "persistentPrepTimeMs"], ["PackTime", "packTimeMs"]]:
		var pair: Array = pair_value
		if not is_equal_approx(_receipt_number(fields.get(String(pair[0]), {}) as Dictionary), float(effect.get(String(pair[1]), -1.0))):
			return false
	if not is_equal_approx(_receipt_number(fields.get("OpacityTarget", {}) as Dictionary), float(effect.get("opacityTarget", -1.0))):
		return false
	if _receipt_yes_no(fields.get("ForceMountedWhenDisguising", {}) as Dictionary) != true:
		return false
	var has_modifier := effect.has("triggerAttributeModifierId") or effect.has("attributeModifierDurationMs") or effect.has("triggerAttributeModifier")
	if has_modifier:
		# RotWK's generic SpecialAbilityUpdate parser stores these fields, but
		# the exact 2.01 SpecialDisguise body/callgraph contains no proven
		# application read. Unofficial-2.02 authors Rider2Tracker here; keep that
		# row unavailable until an external retail trace closes the call path.
		return false
	else:
		if fields.has("TriggerAttributeModifier") or fields.has("AttributeModifierDuration"):
			return false
	return true


static func _is_lower_sha256(value: String) -> bool:
	if value.length() != 64 or value != value.to_lower():
		return false
	for code in value.to_ascii_buffer():
		if not (code >= 48 and code <= 57) and not (code >= 97 and code <= 102):
			return false
	return true


static func _receipt_token(receipt: Dictionary) -> String:
	var authored := String(receipt.get("authored", receipt.get("value", ""))).strip_edges()
	return authored.split(" ", false)[0] if authored != "" else ""


static func _receipt_number(receipt: Dictionary) -> float:
	if receipt.has("milliseconds"):
		return float(receipt.get("milliseconds", NAN))
	if receipt.has("value") and typeof(receipt.get("value")) in [TYPE_INT, TYPE_FLOAT]:
		return float(receipt.get("value"))
	var authored := String(receipt.get("authored", "")).strip_edges()
	return authored.to_float() if authored.is_valid_float() else NAN


static func _receipt_yes_no(receipt: Dictionary) -> Variant:
	if receipt.has("value") and typeof(receipt.get("value")) == TYPE_BOOL:
		return bool(receipt.get("value"))
	var authored := String(receipt.get("authored", "")).strip_edges().to_lower()
	if authored == "yes":
		return true
	if authored == "no":
		return false
	return null


static func _valid_unleash_special_power(effect: Dictionary) -> bool:
	if (
		String(effect.get("specialPowerTemplateId", "")).strip_edges() == ""
		or String(effect.get("targetMode", "")) != "SELF_OWNED_SLAVE"
		or String(effect.get("spawnedObjectId", "")).strip_edges() == ""
		or typeof(effect.get("timingMs")) != TYPE_DICTIONARY
		or typeof(effect.get("awardXpForTriggering")) not in [TYPE_INT, TYPE_FLOAT]
		or float(effect.get("awardXpForTriggering", -1.0)) < 0.0
		or typeof(effect.get("instant")) != TYPE_BOOL
		or typeof(effect.get("creationGateUpgradeIds")) != TYPE_ARRAY
		or (effect.get("creationGateUpgradeIds", []) as Array).is_empty()
		or typeof(effect.get("slaveWatcher")) != TYPE_DICTIONARY
	):
		return false
	var timing := effect.get("timingMs", {}) as Dictionary
	if typeof(timing.get("UnpackTime")) not in [TYPE_INT, TYPE_FLOAT] or float(timing.get("UnpackTime", -1.0)) < 0.0:
		return false
	for gate_value in effect.get("creationGateUpgradeIds", []) as Array:
		if typeof(gate_value) != TYPE_STRING or String(gate_value).strip_edges() == "":
			return false
	var watcher := effect.get("slaveWatcher", {}) as Dictionary
	return (
		String(watcher.get("removeUpgradeId", "")).strip_edges() != ""
		and String(watcher.get("grantUpgradeId", "")).strip_edges() != ""
		and String(watcher.get("sourceIni", "")).strip_edges() != ""
		and typeof(watcher.get("line")) == TYPE_INT
		and int(watcher.get("line", 0)) > 0
	)


static func runtime_object_id(source_id: String) -> String:
	## Public slug for summon/OCL object lookups (mirrors runtime_member_id).
	return _runtime_id(source_id)


static func _first_production_slot(document: Dictionary) -> int:
	var rows := producer_bindings(document)
	return int(rows[0].get("slot", 0)) if not rows.is_empty() else 0


static func _resolved_value(value: Variant) -> Variant:
	return (value as Dictionary).get("value") if typeof(value) == TYPE_DICTIONARY else value


static func _resolved_weapon_modes(value: Variant) -> Dictionary:
	## Converter weaponModes: {mode: {field: {value,...}}} -> {mode: {field: value}}.
	if typeof(value) != TYPE_DICTIONARY:
		return {}
	var output: Dictionary = {}
	for mode_key in (value as Dictionary).keys():
		var profile: Variant = (value as Dictionary)[mode_key]
		if typeof(profile) != TYPE_DICTIONARY:
			return {}
		output[String(mode_key)] = _flatten_combat(profile)
	return output


static func _kind_of_tokens(document: Dictionary) -> Array:
	## Pack path: registration.kindOf. Descriptor path: top-level kindOf.
	## Absent-unless-set: empty when neither document authored tokens.
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var kind_of: Variant = registration.get("kindOf", document.get("kindOf", null))
	if typeof(kind_of) != TYPE_DICTIONARY:
		return []
	var tokens: Array = []
	var seen: Dictionary = {}
	for field in ["container", "primaryMember"]:
		for token_value in (kind_of as Dictionary).get(field, []) as Array:
			var token := String(token_value).strip_edges()
			if token == "" or seen.has(token):
				continue
			seen[token] = true
			tokens.append(token)
	return tokens


static func _copy_optional_kind_of(output: Dictionary, document: Dictionary) -> void:
	var tokens := _kind_of_tokens(document)
	if not tokens.is_empty():
		output["kind_of"] = tokens


static func _copy_optional_crush_fields(output: Dictionary, simulation: Dictionary) -> void:
	## Absent-unless-set: only copy crush keys the document actually authored.
	var crush: Dictionary = simulation.get("crush", {}) as Dictionary
	var pairs := [
		["crusher_level", "crusherLevel"],
		["crushable_level", "crushableLevel"],
		["crush_weapon_id", "crushWeaponId"],
		["crush_damage", "crushDamage"],
		["min_crush_velocity_percent", "minCrushVelocityPercent"],
		["crush_deceleration_percent", "crushDecelerationPercent"],
		["crush_knockback", "crushKnockback"],
		["crush_revenge_weapon_id", "crushRevengeWeaponId"],
		["crush_revenge_damage", "crushRevengeDamage"],
	]
	for pair_value in pairs:
		var pair: Array = pair_value
		var snake := String(pair[0])
		var camel := String(pair[1])
		var raw: Variant = null
		if simulation.has(snake):
			raw = simulation.get(snake)
		elif crush.has(camel):
			raw = crush.get(camel)
		elif crush.has(snake):
			raw = crush.get(snake)
		if raw == null:
			continue
		if snake in ["crush_weapon_id", "crush_revenge_weapon_id"]:
			var weapon_id := String(raw).strip_edges()
			if weapon_id != "":
				output[snake] = weapon_id
			continue
		if typeof(raw) in [TYPE_INT, TYPE_FLOAT] and is_finite(float(raw)):
			if snake in ["crusher_level", "crushable_level", "crush_damage", "crush_revenge_damage"]:
				output[snake] = int(raw)
			else:
				output[snake] = float(raw)


static func _resolved_dictionary(value: Variant) -> Dictionary:
	if typeof(value) != TYPE_DICTIONARY:
		return {}
	var output: Dictionary = {}
	for key in (value as Dictionary).keys():
		output[key] = _resolved_value((value as Dictionary)[key])
	return output


static func _flatten_combat(value: Variant) -> Dictionary:
	## Like `_resolved_dictionary` but keeps the ContinuousFireCoast expression
	## so the clip-reload proxy can see whether the pack authored a
	## `*_RELOADTIME_MAX` token (the only case the proxy is legal).
	var output := _resolved_dictionary(value)
	if typeof(value) != TYPE_DICTIONARY:
		return output
	var coast: Variant = (value as Dictionary).get("continuousFireCoastMs")
	if typeof(coast) == TYPE_DICTIONARY:
		output["continuousFireCoastExpression"] = String(
			(coast as Dictionary).get("expression", "")
		)
	return output


static func _resolved_destroy_die(value: Variant) -> Array[Dictionary]:
	if typeof(value) != TYPE_ARRAY or (value as Array).is_empty():
		return []
	var output: Array[Dictionary] = []
	for policy_value in value as Array:
		if typeof(policy_value) != TYPE_DICTIONARY:
			return []
		var policy := policy_value as Dictionary
		var role := String(policy.get("ownerRole", ""))
		var excluded_value: Variant = policy.get("excludedDeathTypes", [])
		if (
			role not in ["object", "container", "primaryMember"]
			or String(policy.get("module", "")) != "DestroyDie"
			or String(policy.get("deathTypes", "")) != "ALL"
			or typeof(excluded_value) != TYPE_ARRAY
		):
			return []
		var excluded: Array = (excluded_value as Array).duplicate()
		# The retail ALL -TOPPLED declarations belong to cinematic carriers
		# that this playable-unit lane does not materialize.  Keep that shape
		# compiler-visible, but refuse to advertise executable TOPPLED
		# semantics until one of those carriers has an authoritative runtime.
		if not excluded.is_empty():
			return []
		# The member object has no independent materialized corpse/presentation
		# lifecycle in RetailSliceSim. Keep this compiler-visible evidence out
		# of the executable projection until such a lifecycle exists.
		if role == "primaryMember":
			continue
		output.append({
			"owner_role": role,
			"death_types": "ALL",
			"excluded_death_types": excluded,
		})
	return output


static func _resolved_slow_death_fades(value: Variant) -> Array[Dictionary]:
	## Retail authors the fade window as `DestructionDelay` on a
	## `DeathTypes = NONE +FADED` SlowDeathBehavior. The compiler carries the
	## authored milliseconds verbatim; this projects the ones the simulation can
	## execute and drops nothing silently -- a row with no authored delay, or an
	## unresolved define expression, is simply not executable and is left out.
	##
	## Only the NONE +<type> shape is projected. `ALL` slow deaths describe a
	## multi-phase death sequence (sink, flingers, OCL spawns) this simulation
	## does not run; advertising a delay for it would claim behaviour we do not
	## have.
	if typeof(value) != TYPE_ARRAY:
		return []
	var output: Array[Dictionary] = []
	for row_value in value as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var policy := row_value as Dictionary
		if policy.get("destructionDelayAuthored") != true:
			continue
		var delay_value: Variant = policy.get("destructionDelayMs")
		if typeof(delay_value) not in [TYPE_INT, TYPE_FLOAT]:
			continue
		var delay_ms := float(delay_value)
		if not is_finite(delay_ms) or delay_ms <= 0.0:
			continue
		var tokens_value: Variant = policy.get("deathTypes", [])
		if typeof(tokens_value) != TYPE_ARRAY:
			continue
		var included: Array[String] = []
		var mode := ""
		for token_value in tokens_value as Array:
			var token := String(token_value).strip_edges().to_upper()
			if token == "NONE":
				mode = "NONE"
			elif token.begins_with("+"):
				var name := token.substr(1)
				if name != "" and not included.has(name):
					included.append(name)
			else:
				# ALL, bare types and -exclusions are not this lane's shape.
				mode = ""
				break
		if mode != "NONE" or included.is_empty():
			continue
		output.append({
			"owner_role": String(policy.get("ownerRole", "object")),
			"death_types": "NONE",
			"included_death_types": included,
			"destruction_delay_ms": delay_ms,
		})
	return output


static func _destroy_die_is_deferred_primary_member_only(value: Variant) -> bool:
	if typeof(value) != TYPE_ARRAY or (value as Array).is_empty():
		return false
	for policy_value in value as Array:
		if typeof(policy_value) != TYPE_DICTIONARY:
			return false
		var policy := policy_value as Dictionary
		var excluded_value: Variant = policy.get("excludedDeathTypes", [])
		if (
			String(policy.get("ownerRole", "")) != "primaryMember"
			or String(policy.get("module", "")) != "DestroyDie"
			or String(policy.get("deathTypes", "")) != "ALL"
			or typeof(excluded_value) != TYPE_ARRAY
			or not (excluded_value as Array).is_empty()
		):
			return false
	return true


static func _normalized_destroy_die(value: Variant) -> Array[Dictionary]:
	if typeof(value) != TYPE_ARRAY or (value as Array).is_empty():
		return []
	var output: Array[Dictionary] = []
	for policy_value in value as Array:
		if typeof(policy_value) != TYPE_DICTIONARY:
			return []
		var policy := policy_value as Dictionary
		var role := String(policy.get("owner_role", ""))
		var excluded_value: Variant = policy.get("excluded_death_types", [])
		if (
			role not in ["object", "container"]
			or String(policy.get("death_types", "")) != "ALL"
			or typeof(excluded_value) != TYPE_ARRAY
			or not (excluded_value as Array).is_empty()
		):
			return []
		output.append({
			"owner_role": role,
			"death_types": "ALL",
		})
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
