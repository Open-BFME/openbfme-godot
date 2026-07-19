class_name RetailFactionManifest
extends RefCounted
## Faction manifest for the retail vertical slice.
##
## The manifest is the single faction-scoped table the slice and simulation
## read: pack id to assert, structure kinds with object ids, maximum health and
## build rules, the producer registry, the initial spawn roster, the enemy AI
## production plan, and unit damage types. `default_manifest()` reproduces
## today's private Men/Gondor slice exactly (every value equals the historical
## constant), while `from_registries()` builds the same shape for another
## faction purely from loaded `playableUnit.*` / `playableStructure.*` runtime
## documents and fails closed with a specific error when content is missing.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const PlayableUnitAdapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")

const DEFAULT_FACTION := "men"
const DEFAULT_PACK_ID := "bfme2-men-vslice"
const FACTION_OBJECT_PREFIXES := {
	"men": ["men", "gondor"],
	"elves": ["elven", "eregion"],
	"dwarves": ["dwarven", "dwarf"],
	"isengard": ["isengard"],
	"mordor": ["mordor"],
	"wild": ["wild", "goblin"],
}
# Moved verbatim from RetailVerticalSlice.BUILDING_OBJECT_IDS.
const DEFAULT_STRUCTURE_OBJECT_IDS := {
	"fortress": "bfme2.object.men-fortress",
	"farm": "bfme2.object.men-farm",
	"barracks": "bfme2.object.men-barracks",
	"archery_range": "bfme2.object.men-archery-range",
	"stable": "bfme2.object.men-stable",
	"workshop": "bfme2.object.gondor-workshop",
}
# Moved verbatim from RetailVerticalSlice._producer_kind_registry(): the
# structures actually instantiated by the hosted faction slice. A descriptor
# whose retail producer is not present fails closed instead of being attached
# to an unrelated building.
const DEFAULT_PRODUCER_KIND_REGISTRY := {
	"GondorFortress": "fortress",
	"MenFortress": "fortress",
	"GondorBarracks": "barracks",
	"GondorArcherRange": "archery_range",
	"GondorStable": "stable",
	"GondorWorkshop": "workshop",
}


static func default_manifest() -> Dictionary:
	return {
		"faction": DEFAULT_FACTION,
		"pack_id": DEFAULT_PACK_ID,
		"structure_kinds": SimScript.STRUCTURE_KINDS.duplicate(),
		"structure_object_ids": DEFAULT_STRUCTURE_OBJECT_IDS.duplicate(true),
		"structure_max_health": SimScript.STRUCTURE_MAX_HEALTH.duplicate(true),
		"structure_build_rules": SimScript.STRUCTURE_BUILD_RULES.duplicate(true),
		"producer_kind_registry": DEFAULT_PRODUCER_KIND_REGISTRY.duplicate(true),
		"unit_production_rules": SimScript.UNIT_PRODUCTION_RULES.duplicate(true),
		"ai_production_plan": SimScript.AI_PRODUCTION_PLAN.duplicate(),
		"unit_damage_types": SimScript.UNIT_DAMAGE_TYPES.duplicate(true),
		"spawn_roster": SimScript.DEFAULT_SPAWN_ROSTER.duplicate(true),
		"builder_unit_ids": [],
		"faction_pack_roots": [],
	}


static func from_registries(faction: String, unit_runtimes: Dictionary, structure_runtimes: Dictionary) -> Dictionary:
	## Builds a manifest for a non-Men faction from imported pack registries.
	## `faction` is a lowercase source object-id prefix (for example "rohan"
	## matching RohanBarracks / RohanPorter). Missing content is a specific
	## error; nothing ever falls back to Men/Gondor tables.
	var slug := faction.strip_edges().to_lower()
	if slug == "" or slug == DEFAULT_FACTION:
		return default_manifest()

	var prefixes: Array = FACTION_OBJECT_PREFIXES.get(slug, [slug]) as Array
	var structure_ids := _matching_ids(structure_runtimes, prefixes)
	if structure_ids.is_empty():
		return {"_error": "faction '%s' has no loaded playableStructure.* runtime documents" % slug}
	var unit_ids := _matching_ids(unit_runtimes, prefixes)
	if unit_ids.is_empty():
		return {"_error": "faction '%s' has no loaded playableUnit.* runtime documents" % slug}

	var structure_kinds: Array = []
	var structure_object_ids: Dictionary = {}
	var structure_source_by_kind: Dictionary = {}
	var structure_max_health: Dictionary = {}
	var structure_build_rules: Dictionary = {}
	var producer_kind_registry: Dictionary = {}
	var builder_sources: Dictionary = {}
	var pack_roots: Dictionary = {}
	var fortress_kind := ""
	for object_id in structure_ids:
		var document: Dictionary = structure_runtimes[object_id] as Dictionary
		var registration: Dictionary = document.get("registration", {}) as Dictionary
		var production: Dictionary = registration.get("production", {}) as Dictionary
		# Citadels, expansion pads, and wall templates are lifecycle resources
		# owned by their authored parent/construct route. They are loaded by the
		# presenter but must not become independent base structures or producers.
		if String(production.get("evidence", "")) != "authored-construct-command":
			continue
		var kind := _structure_kind_for(String(document.get("slug", "")), prefixes)
		if kind.contains("fortress"):
			if fortress_kind != "":
				return {"_error": "faction '%s' declares more than one fortress structure (%s and %s)" % [slug, structure_source_by_kind.get(fortress_kind, ""), object_id]}
			kind = "fortress"
			fortress_kind = kind
		if structure_kinds.has(kind):
			return {"_error": "faction '%s' structures '%s' and '%s' collapse to the same kind '%s'" % [slug, structure_source_by_kind.get(kind, ""), object_id, kind]}
		var lifecycle: Dictionary = ((registration.get("presentation", {}) as Dictionary).get("buildingLifecycle", {}) as Dictionary)
		var maximum_health := int((lifecycle.get("simulationFacts", {}) as Dictionary).get("maximumHealth", 0))
		if maximum_health <= 0:
			return {"_error": "structure '%s' has no proven simulationFacts.maximumHealth" % object_id}
		var scalar_fields: Dictionary = (registration.get("gameplay", {}) as Dictionary).get("scalarFields", {}) as Dictionary
		var cost := _scalar_number(scalar_fields, "BuildCost")
		var seconds := _scalar_number(scalar_fields, "BuildTime")
		if cost < 0.0:
			return {"_error": "structure '%s' has no numeric BuildCost scalar field" % object_id}
		if seconds <= 0.0:
			return {"_error": "structure '%s' has no positive numeric BuildTime scalar field" % object_id}
		structure_kinds.append(kind)
		structure_source_by_kind[kind] = object_id
		structure_object_ids[kind] = PlayableUnitAdapter._runtime_id(object_id)
		structure_max_health[kind] = maximum_health
		structure_build_rules[kind] = {"cost": int(cost), "seconds": seconds}
		producer_kind_registry[object_id] = kind
		pack_roots[String(document.get("_pack_root", ""))] = true
		for route_value in production.get("routes", []) as Array:
			builder_sources[String((route_value as Dictionary).get("builderObjectId", ""))] = true
	if fortress_kind == "":
		return {"_error": "faction '%s' has no fortress structure runtime; the starting base cannot be seeded" % slug}
	# Deterministic base order: the fortress leads, remaining kinds keep the
	# natural-nocase document order they were derived from.
	structure_kinds.erase("fortress")
	structure_kinds.push_front("fortress")

	var builder_names: Array = builder_sources.keys()
	builder_names.sort_custom(func(a, b) -> bool: return String(a).naturalnocasecmp_to(String(b)) < 0)
	if builder_names.is_empty():
		return {"_error": "faction '%s' structures declare no authored construct routes, so no builder unit is provable" % slug}
	var builder_source := ""
	var builder_document: Dictionary = {}
	for candidate_value in builder_names:
		var candidate := String(candidate_value)
		var candidate_document := _unit_document_for(unit_runtimes, candidate)
		if not candidate_document.is_empty():
			builder_source = candidate
			builder_document = candidate_document
			break
	if builder_document.is_empty():
		return {"_error": "faction '%s' builder candidates [%s] have no playableUnit.* runtime document (convert the faction porter)" % [slug, ", ".join(builder_names)]}
	var builder_member_id := PlayableUnitAdapter.runtime_member_id(builder_document)

	# Every matching unit must resolve against this faction's producers.
	var producer_kinds_folded: Dictionary = {}
	for producer_id_value in producer_kind_registry.keys():
		producer_kinds_folded[String(producer_id_value).to_lower()] = String(producer_id_value)
	for unit_id in unit_ids:
		var unit_document: Dictionary = unit_runtimes[unit_id] as Dictionary
		for producer in PlayableUnitAdapter.producer_bindings(unit_document):
			var producer_source := String(producer.get("producer_source_object_id", ""))
			if not producer_kinds_folded.has(producer_source.to_lower()):
				return {"_error": "unit '%s' is produced by '%s', which has no playableStructure.* runtime for faction '%s'" % [unit_id, producer_source, slug]}
		pack_roots[String(unit_document.get("_pack_root", ""))] = true

	# One trainable unit type per producer, in fortress-first structure order,
	# each producer's first unit in natural-nocase unit order.
	var roster_units: Array = []
	var seen_unit_types: Dictionary = {}
	for kind_value in structure_kinds:
		var kind := String(kind_value)
		var producer_source := String(structure_source_by_kind.get(kind, ""))
		for unit_id in unit_ids:
			var unit_document: Dictionary = unit_runtimes[unit_id] as Dictionary
			if String(unit_document.get("objectId", "")).to_lower() == builder_source.to_lower():
				continue
			var produced_here := false
			for producer in PlayableUnitAdapter.producer_bindings(unit_document):
				if String(producer.get("producer_source_object_id", "")).to_lower() == producer_source.to_lower():
					produced_here = true
					break
			if not produced_here:
				continue
			var simulation := PlayableUnitAdapter.simulation_rule(unit_document)
			if simulation.is_empty():
				return {"_error": "unit '%s' has unresolved simulation evidence and cannot join the spawn roster" % unit_id}
			var unit_type := String(simulation.get("unit_type", ""))
			if not seen_unit_types.has(unit_type):
				seen_unit_types[unit_type] = true
				roster_units.append({
					"unit_type": unit_type,
					"object_id": String(simulation.get("object_id", "")),
					"display_name": String(simulation.get("display_name", "")),
				})
			break
	if roster_units.is_empty():
		return {"_error": "faction '%s' has no trainable playable unit for any of its producers" % slug}

	var spawn_roster: Array = []
	var player_slots: Array = [[1, "player_spawn_primary"], [2, "player_spawn_secondary"]]
	var enemy_slots: Array = [[101, "enemy_spawn_primary"], [102, "enemy_spawn_secondary"], [103, "enemy_reserve"]]
	for slot_index in player_slots.size():
		var unit: Dictionary = roster_units[mini(slot_index, roster_units.size() - 1)]
		spawn_roster.append({
			"id": int(player_slots[slot_index][0]),
			"team": SimScript.PLAYER_TEAM,
			"anchor": String(player_slots[slot_index][1]),
			"name": String(unit["display_name"]),
			"object_id": String(unit["object_id"]),
			"unit_type": String(unit["unit_type"]),
			"requires_unit_rule": true,
		})
	for slot_index in enemy_slots.size():
		var unit: Dictionary = roster_units[mini(slot_index, roster_units.size() - 1)]
		spawn_roster.append({
			"id": int(enemy_slots[slot_index][0]),
			"team": SimScript.ENEMY_TEAM,
			"anchor": String(enemy_slots[slot_index][1]),
			"name": "Enemy %s" % String(unit["display_name"]),
			"object_id": String(unit["object_id"]),
			"unit_type": String(unit["unit_type"]),
			"requires_unit_rule": true,
		})
	spawn_roster.append({
		"id": 3, "team": SimScript.PLAYER_TEAM, "anchor": "player_builder",
		"name": "Builder", "object_id": builder_member_id, "unit_type": builder_member_id,
		"command_points": 0, "requires_unit_rule": true,
	})
	spawn_roster.append({
		"id": 104, "team": SimScript.ENEMY_TEAM, "anchor": "enemy_builder",
		"name": "Enemy Builder", "object_id": builder_member_id, "unit_type": builder_member_id,
		"command_points": 0, "requires_unit_rule": true,
	})

	var ai_production_plan: Array = []
	for unit_value in roster_units:
		ai_production_plan.append(String((unit_value as Dictionary)["unit_type"]))

	var sorted_pack_roots: Array = pack_roots.keys()
	sorted_pack_roots.sort()
	return {
		"faction": slug,
		# The host slice pack (map, HUD dock, shared surfaces) stays asserted;
		# faction gameplay content arrives from the packs recorded below.
		"pack_id": DEFAULT_PACK_ID,
		"structure_kinds": structure_kinds,
		"structure_object_ids": structure_object_ids,
		"structure_max_health": structure_max_health,
		"structure_build_rules": structure_build_rules,
		"producer_kind_registry": producer_kind_registry,
		"unit_production_rules": {},
		"ai_production_plan": ai_production_plan,
		"unit_damage_types": {},
		"spawn_roster": spawn_roster,
		"builder_unit_ids": [builder_member_id],
		"faction_pack_roots": sorted_pack_roots,
	}


static func _matching_ids(registry: Dictionary, prefixes: Array) -> Array:
	var result: Array = []
	for object_id_value in registry.keys():
		var object_id := String(object_id_value)
		if typeof(registry[object_id_value]) != TYPE_DICTIONARY:
			continue
		for prefix_value in prefixes:
			if object_id.to_lower().begins_with(String(prefix_value)):
				result.append(object_id)
				break
	result.sort_custom(func(a, b) -> bool: return String(a).naturalnocasecmp_to(String(b)) < 0)
	return result


static func _unit_document_for(unit_runtimes: Dictionary, source_object_id: String) -> Dictionary:
	for object_id_value in unit_runtimes.keys():
		if String(object_id_value).to_lower() == source_object_id.to_lower():
			return unit_runtimes[object_id_value] as Dictionary
	return {}


static func _structure_kind_for(document_slug: String, prefixes: Array) -> String:
	var kind := document_slug
	for prefix_value in prefixes:
		var prefix := String(prefix_value)
		if kind.to_lower().begins_with(prefix):
			kind = kind.substr(prefix.length())
			break
	kind = kind.trim_prefix("-")
	if kind == "":
		kind = document_slug
	return kind.replace("-", "_")


static func _scalar_number(scalar_fields: Dictionary, field: String) -> float:
	var row_value: Variant = scalar_fields.get(field)
	if typeof(row_value) != TYPE_DICTIONARY:
		return -1.0
	var row := row_value as Dictionary
	var resolved: Variant = row.get("value")
	if typeof(resolved) in [TYPE_INT, TYPE_FLOAT] and is_finite(float(resolved)):
		return float(resolved)
	var expression := String(row.get("expression", "")).strip_edges()
	if expression == "" or not expression.is_valid_float():
		return -1.0
	var value := expression.to_float()
	return value if is_finite(value) else -1.0
