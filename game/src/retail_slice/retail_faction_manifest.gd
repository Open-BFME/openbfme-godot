class_name RetailFactionManifest
extends RefCounted
## Faction manifest for the retail vertical slice.
##
## The manifest is the single faction-scoped table the slice and simulation
## read: pack id to assert, structure kinds with object ids, maximum health and
## build rules, the producer registry, the initial spawn roster, the enemy AI
## production plan, and unit damage types. `default_manifest()` reproduces
## the legacy private Men/Gondor tiny slice (historical constants). 
## `from_registries()` builds the same shape from loaded `playableUnit.*` /
## `playableStructure.*` runtime documents for any faction including Men when
## those registries are non-empty, and fails closed with a specific error when
## content is missing.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const PlayableUnitAdapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const StructureArmorContract = preload("res://src/retail_slice/structure_armor_contract.gd")

const DEFAULT_FACTION := "men"
## Historical single-faction host pack id. This is a FAST-PATH hint, never an
## identity requirement: a composed pack is id'd `bfme2-<a>-<b>-…-vslice` and
## hosts Men just as fully. Every consumer that resolves a host pack falls back
## to pack.json's `factionImportCoverage` (ModLoader.pack_provides_faction /
## RetailVerticalSlice._pack_root_hosting_faction / MainMenu.
## _mounted_pack_root_hosting_faction) when this literal does not match.
const DEFAULT_PACK_ID := "bfme2-men-vslice"
## Pack faction id -> retail side token (playertemplate.ini `Side =`), the
## vocabulary retail scripts compare with SKIRMISH_PLAYER_FACTION. Versioned
## repo data with its evidence inline; see the file's $comment. Loaded once.
const RETAIL_FACTION_SIDES_PATH := "res://data/retail_faction_sides.json"
static var _retail_faction_sides_cache: Dictionary = {}
static var _retail_faction_sides_loaded := false
const FACTION_OBJECT_PREFIXES := {
	"men": ["men", "gondor"],
	"elves": ["elven", "eregion"],
	"dwarves": ["dwarven", "dwarf"],
	"isengard": ["isengard"],
	"mordor": ["mordor"],
	"wild": ["wild", "goblin"],
	"angmar": ["angmar"],
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


static func retail_faction_sides() -> Dictionary:
	## The pack-faction-id -> retail-side table, loaded once from versioned repo
	## data. A missing or malformed file returns {} LOUDLY (push_error): every
	## faction gate then refuses with "no retail side mapping" instead of
	## answering false as though the faction simply did not match.
	if _retail_faction_sides_loaded:
		return _retail_faction_sides_cache.duplicate(true)
	_retail_faction_sides_loaded = true
	_retail_faction_sides_cache = {}
	if not FileAccess.file_exists(RETAIL_FACTION_SIDES_PATH):
		push_error("retail_faction_sides: %s is missing; every retail faction gate will refuse" % RETAIL_FACTION_SIDES_PATH)
		return {}
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(RETAIL_FACTION_SIDES_PATH))
	if typeof(parsed) != TYPE_DICTIONARY:
		push_error("retail_faction_sides: %s did not parse as a JSON object; every retail faction gate will refuse" % RETAIL_FACTION_SIDES_PATH)
		return {}
	var document := parsed as Dictionary
	if String(document.get("schema", "")) != "openbfme.retail-faction-sides" or int(document.get("schemaVersion", -1)) != 0:
		push_error("retail_faction_sides: %s has schema '%s' v%s, expected openbfme.retail-faction-sides v0; every retail faction gate will refuse" % [
			RETAIL_FACTION_SIDES_PATH, String(document.get("schema", "")), str(document.get("schemaVersion", "?")),
		])
		return {}
	var sides: Dictionary = document.get("sides", {}) as Dictionary
	var validated: Dictionary = {}
	var keys := sides.keys()
	keys.sort()
	for key in keys:
		var faction_id := String(key)
		var side := String(sides[key])
		if faction_id == "" or side == "" or faction_id != faction_id.to_lower():
			push_error("retail_faction_sides: invalid row '%s' -> '%s' (faction ids are non-empty lowercase, sides non-empty); dropping the WHOLE table so nothing resolves from a half-valid file" % [faction_id, side])
			return {}
		validated[faction_id] = side
	_retail_faction_sides_cache = validated
	return _retail_faction_sides_cache.duplicate(true)


static func default_manifest() -> Dictionary:
	return {
		"faction": DEFAULT_FACTION,
		"pack_id": DEFAULT_PACK_ID,
		"structure_kinds": SimScript.STRUCTURE_KINDS.duplicate(),
		# Tiny pack seeds the full five-building starter base (historical slice).
		"seed_structure_kinds": SimScript.STRUCTURE_KINDS.duplicate(),
		"structure_object_ids": DEFAULT_STRUCTURE_OBJECT_IDS.duplicate(true),
		"structure_source_object_ids": {},
		"structure_max_health": SimScript.STRUCTURE_MAX_HEALTH.duplicate(true),
		"structure_build_rules": SimScript.STRUCTURE_BUILD_RULES.duplicate(true),
		"structure_inherit_upgrades": {},
		"deferred_structure_inherit_upgrades": {},
		"structure_production_exit_updates": {},
		"deferred_structure_production_exit_updates": {},
		"structure_auto_deposit_updates": {},
		"deferred_structure_auto_deposit_updates": {},
		"producer_kind_registry": DEFAULT_PRODUCER_KIND_REGISTRY.duplicate(true),
		"unit_production_rules": SimScript.UNIT_PRODUCTION_RULES.duplicate(true),
		"ai_production_plan": SimScript.AI_PRODUCTION_PLAN.duplicate(),
		"unit_damage_types": SimScript.UNIT_DAMAGE_TYPES.duplicate(true),
		"structure_armor": SimScript.DEFAULT_STRUCTURE_ARMOR.duplicate(true),
		"spawn_roster": SimScript.DEFAULT_SPAWN_ROSTER.duplicate(true),
		"builder_unit_ids": [],
		"faction_pack_roots": [],
	}


static func from_registries(faction: String, unit_runtimes: Dictionary, structure_runtimes: Dictionary, allow_ring_heroes := false) -> Dictionary:
	## Builds a manifest from imported pack registries. `faction` is a lowercase
	## source object-id prefix (for example "rohan" matching RohanBarracks /
	## RohanPorter). Men keeps `default_manifest()` when both registries are
	## empty (legacy tiny pack). When Men playableUnit.* / playableStructure.*
	## documents are loaded, Men takes the same data-driven path as other
	## factions. Missing content is a specific error; non-Men factions never
	## fall back to Men/Gondor tables.
	var slug := faction.strip_edges().to_lower()
	if slug == "":
		return default_manifest()
	if slug == DEFAULT_FACTION and (unit_runtimes.is_empty() or structure_runtimes.is_empty()):
		# Legacy tiny Men pack: no converted playable runtimes → hardcoded tables.
		# NEVER silent (Q80): callers in parity contexts must know they are on
		# synthetic constants, not pack data.
		push_warning("faction manifest: men with empty registries falls to default_manifest() — SYNTHETIC constants, not pack data (legacy tiny-pack path)")
		return default_manifest()

	var prefixes: Array = (FACTION_OBJECT_PREFIXES.get(slug, [slug]) as Array).duplicate()
	if slug == DEFAULT_FACTION:
		# Rohan allies may ship inside a Men pack; include their docs when present.
		if not _matching_ids(unit_runtimes, ["rohan"]).is_empty() or not _matching_ids(structure_runtimes, ["rohan"]).is_empty():
			if not prefixes.has("rohan"):
				prefixes.append("rohan")
	var structure_ids := _matching_ids(structure_runtimes, prefixes)
	if structure_ids.is_empty():
		return {"_error": "faction '%s' has no loaded playableStructure.* runtime documents" % slug}
	# Shared retail units ship one playableUnit document per faction pack, each
	# bound to that faction's own producer (retail authors MordorWorker at the
	# isengard, mordor, and wild lumber mills). The flat registry keeps only
	# the last-loaded pack's copy, so scope every in-scope unit to this
	# faction's own pack copy before any producer validation runs.
	unit_runtimes = faction_scoped_unit_runtimes(prefixes, unit_runtimes, structure_runtimes, _content_db_pack_index())
	var unit_ids := _matching_ids(unit_runtimes, prefixes)
	if unit_ids.is_empty():
		return {"_error": "faction '%s' has no loaded playableUnit.* runtime documents" % slug}

	var structure_kinds: Array = []
	var structure_object_ids: Dictionary = {}
	var structure_source_by_kind: Dictionary = {}
	var structure_max_health: Dictionary = {}
	var structure_build_rules: Dictionary = {}
	var structure_bounty_values: Dictionary = {}
	var structure_armor: Dictionary = {}
	var structure_upgrade_chains: Dictionary = {}
	var structure_research: Dictionary = {}
	var structure_castle_upgrades: Dictionary = {}
	var structure_upgrade_effects: Dictionary = {}
	var structure_create_grants: Dictionary = {}
	var structure_inherit_upgrades: Dictionary = {}
	var deferred_structure_inherit_upgrades: Dictionary = {}
	var structure_production_exit_updates: Dictionary = {}
	var deferred_structure_production_exit_updates: Dictionary = {}
	var structure_auto_deposit_updates: Dictionary = {}
	var deferred_structure_auto_deposit_updates: Dictionary = {}
	var producer_kind_registry: Dictionary = {}
	var builder_sources: Dictionary = {}
	var pack_roots: Dictionary = {}
	var fortress_kind := ""
	var excluded_structures: Dictionary = {}
	var fortress_composite_object_ids: Dictionary = {}
	var structure_construct_icons: Dictionary = {}
	## The citadel's ShroudClearingRange, harvested off the excluded composite
	## and filed under the fortress kind once the loop has seen both documents.
	var fortress_composite_deshroud_source := 0.0
	for object_id in structure_ids:
		var document: Dictionary = structure_runtimes[object_id] as Dictionary
		var registration: Dictionary = document.get("registration", {}) as Dictionary
		var production: Dictionary = registration.get("production", {}) as Dictionary
		var evidence := String(production.get("evidence", ""))
		# Citadels, expansion pads, and wall templates are lifecycle resources
		# owned by their authored parent/construct route. They are loaded by the
		# presenter but must not become independent base structures. An
		# engine-spawned fortress composite (the citadel) can still register as
		# a producer component of the fortress below, when a unit's authored
		# producer binding cross-checks against its recorded command sets.
		if evidence != "authored-construct-command":
			var composite_role := String(document.get("compositeRole", ""))
			if composite_role != "":
				if evidence != "engine-spawned-composite" or not composite_role.begins_with("fortress-composite-"):
					return {"_error": "structure '%s' has invalid composite role '%s' for evidence '%s'" % [object_id, composite_role, evidence]}
				if fortress_composite_object_ids.has(composite_role):
					return {"_error": "faction '%s' declares duplicate fortress composite role '%s'" % [slug, composite_role]}
				fortress_composite_object_ids[composite_role] = object_id
			var deferred_auto_deposit: Variant = (
				registration.get("gameplay", {}) as Dictionary
			).get("autoDepositUpdates")
			if deferred_auto_deposit != null:
				var auto_deposit_error := _validate_structure_auto_deposit_updates(
					object_id, deferred_auto_deposit
				)
				if auto_deposit_error != "":
					return {"_error": auto_deposit_error}
				deferred_structure_auto_deposit_updates[object_id] = (
					deferred_auto_deposit as Array
				).duplicate(true)
			var composite_gameplay := registration.get("gameplay", {}) as Dictionary
			var deferred_exit: Variant = composite_gameplay.get("productionExitUpdates")
			if deferred_exit != null:
				var deferred_exit_error := _validate_structure_production_exit_updates(
					object_id, deferred_exit, composite_gameplay.get("moduleContracts")
				)
				if deferred_exit_error != "":
					return {"_error": deferred_exit_error}
				var executable_exit_rows: Array = []
				var deferred_exit_rows: Array = []
				for exit_value in deferred_exit as Array:
					var exit_row := exit_value as Dictionary
					if String(exit_row.get("runtimeStatus", "")) == "executable":
						executable_exit_rows.append(exit_row.duplicate(true))
					else:
						deferred_exit_rows.append(exit_row.duplicate(true))
				if not executable_exit_rows.is_empty():
					structure_production_exit_updates[object_id] = executable_exit_rows
				if not deferred_exit_rows.is_empty():
					deferred_structure_production_exit_updates[object_id] = deferred_exit_rows
			var deferred_inherit: Variant = (
				registration.get("gameplay", {}) as Dictionary
			).get("inheritUpgradesOnCreate")
			if deferred_inherit != null:
				var deferred_error := _validate_structure_inherit_upgrades(
					object_id, deferred_inherit
				)
				if deferred_error != "":
					return {"_error": deferred_error}
				deferred_structure_inherit_upgrades[object_id] = (
					deferred_inherit as Array
				).duplicate(true)
			# Retail authors the fortress improvement buttons (and the
			# CastleUpgrade modules that make them do anything) on the CITADEL
			# composite, not on the constructable fortress object. The citadel
			# is excluded from the base-structure roster, so harvest its castle
			# upgrade surface here and file it under the fortress kind the
			# runtime actually selects.
			var composite_castle: Variant = (
				registration.get("gameplay", {}) as Dictionary
			).get("castleUpgrades")
			if composite_castle != null and composite_role == "fortress-composite-citadel":
				var composite_castle_error := _validate_structure_castle_upgrades(
					object_id, composite_castle
				)
				if composite_castle_error != "":
					return {"_error": composite_castle_error}
				structure_castle_upgrades["fortress"] = (
					composite_castle as Dictionary
				).duplicate(true)
			if composite_role == "fortress-composite-citadel":
				# THE FORTRESS SEES THROUGH ITS CITADEL, for exactly the reason
				# the castle upgrades above are harvested here. MenFortress - the
				# constructable object the "fortress" kind is built from -
				# authors no VisionRange and no ShroudClearingRange at all;
				# MenFortressCitadel authors 400 and 800. Reading the deshroud
				# range off the constructable object alone leaves a player's own
				# fortress standing in the dark, which is what the first fog-on
				# playtest reported.
				var citadel_fields: Dictionary = (
					(document.get("registration", {}) as Dictionary).get("gameplay", {})
					as Dictionary
				).get("scalarFields", {}) as Dictionary
				var citadel_deshroud := _scalar_number(citadel_fields, "ShroudClearingRange")
				if citadel_deshroud < 0.0:
					citadel_deshroud = _scalar_number(citadel_fields, "VisionRange")
				if citadel_deshroud > 0.0:
					fortress_composite_deshroud_source = citadel_deshroud
			excluded_structures[object_id.to_lower()] = {
				"object_id": object_id,
				"evidence": evidence,
				"document": document,
			}
			continue
		var kind := _structure_kind_for(String(document.get("slug", "")), prefixes)
		# Fortress expansions (barricades, pads, watchtowers) are authored
		# constructs of the fortress, never the fortress itself — a kind that
		# carries "expansion" must not claim the unique fortress slot.
		if kind.contains("fortress") and not kind.contains("expansion"):
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
		var bounty_value := _scalar_number(scalar_fields, "BountyValue")
		if bounty_value >= 0.0:
			if bounty_value != float(int(bounty_value)):
				return {"_error": "structure '%s' has non-integral BountyValue" % object_id}
			structure_bounty_values[kind] = int(bounty_value)
		var health_contract := (
			(registration.get("gameplay", {}) as Dictionary).get("health", {})
			as Dictionary
		)
		var build_rule := {"cost": int(cost), "seconds": seconds}
		# The building's own deshroud range, in SOURCE units, carried so the
		# simulation's look pass can give a structure vision. Retail authors
		# ShroudClearingRange and VisionRange independently and both ship in the
		# compiled scalar fields - GondorBarracks is 160/160, MenFortressCitadel
		# 400/800, GondorBattleTower 600/500 - so the deshroud value is preferred
		# and vision is only the fallback for the objects that author no
		# ShroudClearingRange. A structure that authors 0 on purpose (the
		# fortress expansion PADS do) keeps 0 and stays a non-looker.
		var deshroud_source := _scalar_number(scalar_fields, "ShroudClearingRange")
		if deshroud_source < 0.0:
			deshroud_source = _scalar_number(scalar_fields, "VisionRange")
		if deshroud_source > 0.0:
			build_rule["shroud_clearing_range_source"] = deshroud_source
		if bool(
			(health_contract.get("highlanderBody", {}) as Dictionary)
			.get("value", false)
		):
			build_rule["highlander_body"] = true
		structure_build_rules[kind] = build_rule
		var armor_rule := _compiled_structure_armor(document)
		if not armor_rule.is_empty():
			structure_armor[kind] = armor_rule
		var upgrade_chain: Variant = (registration.get("gameplay", {}) as Dictionary).get("upgradeChain")
		if upgrade_chain != null:
			# Doc-driven purchased levels (L2/L3 cost/time/command-set swap/
			# per-level effects); malformed chains fail the manifest closed
			# rather than arriving partial in the simulation.
			var chain_error := _validate_structure_upgrade_chain(object_id, upgrade_chain)
			if chain_error != "":
				return {"_error": chain_error}
			structure_upgrade_chains[kind] = (upgrade_chain as Dictionary).duplicate(true)
		var research: Variant = (registration.get("gameplay", {}) as Dictionary).get("research")
		if research != null:
			# Doc-driven PLAYER technology sales (marketplace/forge/barracks/
			# archery range research buttons); malformed surfaces fail closed.
			var research_error := _validate_structure_research(object_id, research)
			if research_error != "":
				return {"_error": research_error}
			structure_research[kind] = (research as Dictionary).duplicate(true)
		var castle_upgrades: Variant = (registration.get("gameplay", {}) as Dictionary).get("castleUpgrades")
		if castle_upgrades != null:
			# Doc-driven fortress improvement sales (the OBJECT_UPGRADE buttons
			# whose real upgrade a CastleUpgrade module hands out).
			var castle_error := _validate_structure_castle_upgrades(object_id, castle_upgrades)
			if castle_error != "":
				return {"_error": castle_error}
			structure_castle_upgrades[kind] = (castle_upgrades as Dictionary).duplicate(true)
		var upgrade_effects: Variant = (registration.get("gameplay", {}) as Dictionary).get("upgradeEffects")
		if upgrade_effects != null:
			var effects_error := _validate_structure_upgrade_effects(object_id, upgrade_effects)
			if effects_error != "":
				return {"_error": effects_error}
			structure_upgrade_effects[kind] = (upgrade_effects as Dictionary).duplicate(true)
		var create_grants: Variant = (registration.get("gameplay", {}) as Dictionary).get("createGrants")
		if create_grants != null:
			var grants_error := _validate_structure_create_grants(object_id, create_grants)
			if grants_error != "":
				return {"_error": grants_error}
			structure_create_grants[kind] = (create_grants as Array).duplicate(true)
		var inherit_upgrades: Variant = (registration.get("gameplay", {}) as Dictionary).get("inheritUpgradesOnCreate")
		if inherit_upgrades != null:
			var inherit_error := _validate_structure_inherit_upgrades(object_id, inherit_upgrades)
			if inherit_error != "":
				return {"_error": inherit_error}
			structure_inherit_upgrades[kind] = (inherit_upgrades as Array).duplicate(true)
		var structure_gameplay := registration.get("gameplay", {}) as Dictionary
		var production_exit_updates: Variant = structure_gameplay.get("productionExitUpdates")
		if production_exit_updates != null:
			var production_exit_error := _validate_structure_production_exit_updates(
				object_id, production_exit_updates, structure_gameplay.get("moduleContracts")
			)
			if production_exit_error != "":
				return {"_error": production_exit_error}
			var executable_exit_rows: Array = []
			var deferred_exit_rows: Array = []
			for exit_value in production_exit_updates as Array:
				var exit_row := exit_value as Dictionary
				if String(exit_row.get("runtimeStatus", "")) == "executable":
					executable_exit_rows.append(exit_row.duplicate(true))
				else:
					deferred_exit_rows.append(exit_row.duplicate(true))
			if not executable_exit_rows.is_empty():
				structure_production_exit_updates[kind] = executable_exit_rows
			if not deferred_exit_rows.is_empty():
				deferred_structure_production_exit_updates[object_id] = deferred_exit_rows
		var auto_deposit_updates: Variant = (
			registration.get("gameplay", {}) as Dictionary
		).get("autoDepositUpdates")
		if auto_deposit_updates != null:
			var auto_deposit_error := _validate_structure_auto_deposit_updates(
				object_id, auto_deposit_updates
			)
			if auto_deposit_error != "":
				return {"_error": auto_deposit_error}
			var executable_rows: Array = []
			var deferred_rows: Array = []
			for auto_deposit_value in auto_deposit_updates as Array:
				var auto_deposit_row := auto_deposit_value as Dictionary
				if String(auto_deposit_row.get("runtimeStatus", "")) == "executable":
					executable_rows.append(auto_deposit_row.duplicate(true))
				else:
					deferred_rows.append(auto_deposit_row.duplicate(true))
			if not executable_rows.is_empty():
				structure_auto_deposit_updates[kind] = executable_rows
			if not deferred_rows.is_empty():
				deferred_structure_auto_deposit_updates[object_id] = deferred_rows
		producer_kind_registry[object_id] = kind
		pack_roots[String(document.get("_pack_root", ""))] = true
		# Doc-driven construct icon: the structure's own converted construct
		# commandbutton crop. Kinds whose doc records a binding gap are simply
		# absent here and keep the HUD's honest text-only socket.
		var construct_icon := _structure_construct_icon(document)
		if not construct_icon.is_empty():
			structure_construct_icons[kind] = construct_icon
		for route_value in production.get("routes", []) as Array:
			builder_sources[String((route_value as Dictionary).get("builderObjectId", ""))] = true
	if fortress_kind == "":
		return {"_error": "faction '%s' has no fortress structure runtime; the starting base cannot be seeded" % slug}
	# Deterministic base order: the fortress leads, remaining kinds keep the
	# natural-nocase document order they were derived from.
	structure_kinds.erase("fortress")
	structure_kinds.push_front("fortress")
	# Filed after the loop because the constructable fortress and its citadel are
	# two documents and the order they arrive in is not guaranteed. The
	# constructable object wins when it authors a range of its own; every men-,
	# elf- and dwarf-shaped fortress in the shipped packs authors none, so in
	# practice this is where a fortress gets its eyes.
	if fortress_composite_deshroud_source > 0.0:
		var fortress_rule: Dictionary = structure_build_rules.get("fortress", {}) as Dictionary
		if float(fortress_rule.get("shroud_clearing_range_source", 0.0)) <= 0.0:
			fortress_rule["shroud_clearing_range_source"] = fortress_composite_deshroud_source
			structure_build_rules["fortress"] = fortress_rule

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

	# Every matching unit must resolve against this faction's producers. A
	# producer the structure pass excluded can still resolve as a producer
	# component of the fortress: retail fortresses spawn their citadel as an
	# engine-spawned composite which carries the fortress command set, so the
	# porter's authored producer is that citadel, never the fortress base
	# object (which authors no command set at all). The fold is recorded in
	# the producer registry and stays fail-closed: the composite must record
	# the exact command set, slot, and command the binding cites.
	var producer_kinds_folded: Dictionary = {}
	for producer_id_value in producer_kind_registry.keys():
		producer_kinds_folded[String(producer_id_value).to_lower()] = String(producer_id_value)
	# Rohan-namespace allies only join a Men roster when their producers are
	# Men/Gondor structures. A Rohan object produced at another faction's
	# structure (Treebeard at the Ent Moot) is that faction's content and is
	# excluded with a recorded reason instead of failing the Men manifest.
	var ally_excluded_units: Dictionary = {}
	var production_exclusions: Array = []
	for unit_id in unit_ids:
		var unit_document: Dictionary = unit_runtimes[unit_id] as Dictionary
		if PlayableUnitAdapter.is_ring_hero_summon(unit_document) and not allow_ring_heroes:
			# A ring-hero roster entry is retail's One Ring summon slot, not a
			# trained production route: it must never be validated as producer
			# content (its recorded producer belongs to the summon mechanic, and
			# a cohabiting pack can bind it to the wrong faction's fortress).
			ally_excluded_units[unit_id] = true
			production_exclusions.append({
				"object_id": unit_id,
				"category": String(unit_document.get("category", "")),
				"reason": "ring-hero-summon-not-trained",
			})
			continue
		# Per-route resolution: a dead binding (missing producer, out-of-scope
		# ally producer) drops that route with a recorded reason; the unit is
		# excluded only when ZERO of its authored routes resolve. Corrupt
		# composite evidence stays a hard manifest error regardless of other
		# routes — that is data corruption, not a missing structure.
		var resolved_route_count := 0
		var roster_dropped_routes: Array = []
		for producer in PlayableUnitAdapter.producer_bindings(unit_document):
			var producer_source := String(producer.get("producer_source_object_id", ""))
			if allow_ring_heroes and PlayableUnitAdapter.is_ring_hero_summon(unit_document) \
					and String(producer.get("source_field", "")) == "BuildableRingHeroesMP":
				# This is an engine PlayerTemplate roster, not a literal building
				# command set. Bind its recorded source identity to this faction's
				# fortress so the rule-on manifest exposes the retail ring slot.
				producer_kind_registry[producer_source] = "fortress"
				producer_kinds_folded[producer_source.to_lower()] = producer_source
				resolved_route_count += 1
				continue
			if producer_kinds_folded.has(producer_source.to_lower()):
				resolved_route_count += 1
				continue
			if (
				slug == DEFAULT_FACTION
				and unit_id.to_lower().begins_with("rohan")
				and not (producer_source.to_lower().begins_with("men") or producer_source.to_lower().begins_with("gondor"))
			):
				roster_dropped_routes.append({
					"producer_source_object_id": producer_source,
					"reason": "producer-outside-faction-scope:%s" % producer_source,
				})
				continue
			var composite: Dictionary = excluded_structures.get(producer_source.to_lower(), {}) as Dictionary
			if composite.is_empty():
				# A route whose producer structure never converted (retail once
				# authored IsengardBallista at the not-yet-converted
				# IsengardSiegeWorks) is that structure's content, not a
				# faction-wide defect: drop the route with a recorded reason —
				# the same contract the production-rules pass applies below.
				roster_dropped_routes.append({
					"producer_source_object_id": producer_source,
					"reason": "producer-not-loaded:%s" % producer_source,
				})
				continue
			var composite_evidence := String(composite.get("evidence", ""))
			if composite_evidence != "engine-spawned-composite":
				return {"_error": "unit '%s' is produced by '%s', whose recorded production evidence '%s' cannot produce units for faction '%s'" % [unit_id, producer_source, composite_evidence, slug]}
			var composite_document: Dictionary = composite.get("document", {}) as Dictionary
			if not _composite_authors_producer(composite_document, producer):
				return {"_error": "unit '%s' is produced by '%s', which does not author command '%s' in command set '%s' slot %d" % [unit_id, producer_source, String(producer.get("command_id", "")), String(producer.get("command_set_id", "")), int(producer.get("slot", 0))]}
			var composite_id := String(composite.get("object_id", ""))
			producer_kind_registry[composite_id] = "fortress"
			producer_kinds_folded[producer_source.to_lower()] = composite_id
			pack_roots[String(composite_document.get("_pack_root", ""))] = true
			resolved_route_count += 1
		if resolved_route_count == 0 and not roster_dropped_routes.is_empty():
			ally_excluded_units[unit_id] = true
			var roster_exclusion := {
				"object_id": unit_id,
				"category": String(unit_document.get("category", "")),
				"reason": String((roster_dropped_routes[0] as Dictionary).get("reason", "")),
			}
			# Single-route exclusions keep their historical shape; the per-route
			# record only appears when more than one authored route was dropped.
			if roster_dropped_routes.size() > 1:
				roster_exclusion["dropped_routes"] = roster_dropped_routes.duplicate(true)
			production_exclusions.append(roster_exclusion)
			continue
		pack_roots[String(unit_document.get("_pack_root", ""))] = true

	# One trainable unit type per producer, fortress-first. Prefer line troops
	# (infantry/ranged/cavalry/siege) over heroes so the starting skirmish
	# roster plays like BFME, not a hero duel. Heroes still train from fortress.
	var roster_units: Array = []
	var seen_unit_types: Dictionary = {}
	var ordered_unit_ids: Array = unit_ids.duplicate()
	ordered_unit_ids.sort_custom(func(a, b) -> bool:
		var da: Dictionary = unit_runtimes[a] as Dictionary
		var db: Dictionary = unit_runtimes[b] as Dictionary
		var pa := _spawn_category_priority(String(da.get("category", "")))
		var pb := _spawn_category_priority(String(db.get("category", "")))
		if pa != pb:
			return pa < pb
		return String(a).naturalnocasecmp_to(String(b)) < 0
	)
	for kind_value in structure_kinds:
		var kind := String(kind_value)
		for unit_id_value in ordered_unit_ids:
			var unit_id := String(unit_id_value)
			if ally_excluded_units.has(unit_id):
				continue
			var unit_document: Dictionary = unit_runtimes[unit_id] as Dictionary
			if String(unit_document.get("objectId", "")).to_lower() == builder_source.to_lower():
				continue
			var produced_here := false
			for producer in PlayableUnitAdapter.producer_bindings(unit_document):
				var binding_source := String(producer.get("producer_source_object_id", ""))
				var binding_producer_id := String(producer_kinds_folded.get(binding_source.to_lower(), ""))
				if binding_producer_id != "" and String(producer_kind_registry.get(binding_producer_id, "")) == kind:
					produced_here = true
					break
			if not produced_here:
				continue
			var simulation := PlayableUnitAdapter.simulation_rule(unit_document)
			if simulation.is_empty():
				# Skip this producer slot's candidate; try next unit for the same
				# producer rather than failing the whole faction. Units with
				# unresolved combat/formation evidence stay train-blocked via
				# exclusions while other fieldable troops still spawn.
				production_exclusions.append({
					"object_id": unit_id,
					"reason": "unresolved simulation evidence (missing combat/formation contract)",
				})
				continue
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
		return {"_error": "faction '%s' has no trainable playable unit for any of its producers" % slug, "excluded_units": production_exclusions.duplicate(true)}

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

	# Every trainable unit gets a production rule built from its own document:
	# producer binding (citadel-fold aware), cost, build time, command points,
	# and the HUD identity (label/tooltip/button/portrait). Units whose
	# evidence cannot honestly drive training stay out with a recorded
	# exclusion — never a silently approximated rule.
	var unit_production_rules: Dictionary = {}
	var unit_damage_types: Dictionary = {}
	# Doc-derived "Trains <units>" per producer kind for the construct surface's
	# honest fallback tooltips: another faction's buildings name their OWN
	# trained units from production evidence, never borrowed Men strings.
	var structure_training_summaries: Dictionary = {}
	var excluded_units: Array = production_exclusions.duplicate()
	for unit_id in unit_ids:
		var unit_document: Dictionary = unit_runtimes[unit_id] as Dictionary
		if String(unit_document.get("objectId", "")).to_lower() == builder_source.to_lower():
			continue
		if ally_excluded_units.has(unit_id):
			continue
		var category := String(unit_document.get("category", ""))
		var bindings := PlayableUnitAdapter.producer_bindings(unit_document)
		var simulation := PlayableUnitAdapter.simulation_rule(unit_document)
		if simulation.is_empty():
			excluded_units.append({
				"object_id": unit_id,
				"category": category,
				"reason": "unresolved-simulation-evidence",
			})
			continue
		var surfaces: Dictionary = {}
		for binding in bindings:
			surfaces[String(binding.get("surface", ""))] = true
		if category == "hero" and not surfaces.has("hero-roster"):
			# Command-socket summons (Treebeard at the Ent Moot) are not
			# trained production: retail summons them through its own mechanic,
			# so no producer ever declares a train rule for them.
			excluded_units.append({
				"object_id": unit_id,
				"category": category,
				"reason": "command-socket-summon-not-trained",
			})
			continue
		var resolved_producers: Array = []
		var resolved_producer_kinds: Array = []
		var rule_dropped_routes: Array = []
		for binding in bindings:
			var binding_source := String(binding.get("producer_source_object_id", ""))
			var binding_producer_id := String(producer_kinds_folded.get(binding_source.to_lower(), ""))
			if binding_producer_id == "":
				# Dead route: this producer never loaded. The unit keeps its
				# other resolved routes (mirroring the roster pass above); only
				# a unit with zero resolved routes is excluded.
				rule_dropped_routes.append({
					"producer_source_object_id": binding_source,
					"reason": "producer-not-loaded:%s" % binding_source,
				})
				continue
			var route: Dictionary = (binding as Dictionary).duplicate(true)
			route["producer_kind"] = String(producer_kind_registry.get(binding_producer_id, ""))
			resolved_producers.append(route)
			if not resolved_producer_kinds.has(String(route["producer_kind"])):
				resolved_producer_kinds.append(String(route["producer_kind"]))
		if resolved_producers.is_empty():
			var rule_exclusion := {
				"object_id": unit_id,
				"category": category,
				"reason": String((rule_dropped_routes[0] as Dictionary).get("reason", "")) if not rule_dropped_routes.is_empty() else "no-producer-route",
			}
			if rule_dropped_routes.size() > 1:
				rule_exclusion["dropped_routes"] = rule_dropped_routes.duplicate(true)
			excluded_units.append(rule_exclusion)
			continue
		var primary_producer: Dictionary = resolved_producers[0]
		var unit_type := String(simulation.get("unit_type", ""))
		var hud_spec := PlayableUnitAdapter.hud_spec(unit_document)
		var rule := {
			"category": String(simulation.get("category", "")),
			"producer_kind": String(primary_producer.get("producer_kind", "")),
			"producer_kinds": resolved_producer_kinds.duplicate(),
			"producer_routes": resolved_producers.duplicate(true),
			"producer_source_object_id": String(primary_producer.get("producer_source_object_id", "")),
			"surface": String(primary_producer.get("surface", "")),
			"object_id": String(simulation.get("object_id", "")),
			"display_name": String(simulation.get("display_name", "")),
			"default_cost": int(simulation.get("default_cost", 0)),
			"default_build_ticks": int(simulation.get("default_build_ticks", 1)),
			"default_command_points": int(simulation.get("default_command_points", 0)),
			"command_id": String(primary_producer.get("command_id", "")),
			"command_slot": int(primary_producer.get("slot", 0)),
			"roster_ordinal": int(primary_producer.get("roster_ordinal", 0)),
			"label_id": String(hud_spec.get("label_id", "")),
			"tooltip_id": String(hud_spec.get("tooltip_id", "")),
			"image_id": String(hud_spec.get("image_id", "")),
			"portrait_image_id": String(hud_spec.get("portrait_image_id", "")),
		}
		# Recorded dead routes for a kept unit: present ONLY when at least one
		# authored route was actually dropped, so fully-resolving factions keep
		# a byte-identical rule shape.
		if not rule_dropped_routes.is_empty():
			rule["dropped_routes"] = rule_dropped_routes.duplicate(true)
		if unit_production_rules.has(unit_type) and String((unit_production_rules[unit_type] as Dictionary).get("object_id", "")) != String(rule["object_id"]):
			return {"_error": "units '%s' and '%s' collide on runtime unit type '%s'" % [String((unit_production_rules[unit_type] as Dictionary).get("object_id", "")), String(rule["object_id"]), unit_type]}
		unit_production_rules[unit_type] = rule
		# Honest trained-unit name for the construct fallback tooltips: the
		# doc's own train label ("Train Lorien &Warriors" -> "Lorien Warriors");
		# unresolved OBJECT: ids and raw label ids never reach the summary.
		var trained_name := String(hud_spec.get("fallback_label", "")).strip_edges()
		trained_name = trained_name.replace("&&", "\u0001").replace("&", "").replace("\u0001", "&").strip_edges()
		if trained_name.to_lower().begins_with("train "):
			trained_name = trained_name.substr(6).strip_edges()
		if trained_name.contains(":"):
			trained_name = String(simulation.get("display_name", "")).strip_edges()
		if trained_name.contains(":"):
			trained_name = ""
		if trained_name != "":
			for trained_kind_value in resolved_producer_kinds:
				var trained_kind := String(trained_kind_value)
				if not structure_training_summaries.has(trained_kind):
					structure_training_summaries[trained_kind] = []
				if not (structure_training_summaries[trained_kind] as Array).has(trained_name):
					(structure_training_summaries[trained_kind] as Array).append(trained_name)
		var damage_type := String((simulation.get("combat", {}) as Dictionary).get("damageType", "")).to_lower()
		if damage_type != "":
			unit_damage_types[String(rule["object_id"])] = damage_type

	var ai_production_plan: Array = []
	for unit_value in roster_units:
		ai_production_plan.append(String((unit_value as Dictionary)["unit_type"]))

	# ContentDB skips invalid playableUnit documents with only a warning;
	# surface those skips in this faction's recorded exclusions so the roster
	# never silently narrows.
	var tree := Engine.get_main_loop() as SceneTree
	var content_db := tree.root.get_node_or_null("ContentDB") if tree != null else null
	if content_db != null and content_db.has_method("get_skipped_playable_unit_documents"):
		var seen_exclusions: Dictionary = {}
		for existing_value in excluded_units:
			seen_exclusions[String((existing_value as Dictionary).get("object_id", ""))] = true
		for skip_value in content_db.get_skipped_playable_unit_documents():
			var skip_reason := String(skip_value)
			var object_hint := skip_reason.get_slice(":", 0).trim_prefix("playableUnit.")
			var in_scope := false
			for prefix_value in prefixes:
				if object_hint.to_lower().begins_with(String(prefix_value)):
					in_scope = true
					break
			if not in_scope or seen_exclusions.has(object_hint):
				continue
			seen_exclusions[object_hint] = true
			excluded_units.append({
				"object_id": object_hint,
				"category": "",
				"reason": "invalid-runtime-document:%s" % skip_reason.get_slice(":", 1),
			})

	var sorted_pack_roots: Array = pack_roots.keys()
	sorted_pack_roots.sort()
	# Exact retail object identities carried by each live structure kind.
	# Besides the independently constructed root, this includes only composite
	# aliases proven by the producer binding above (for example
	# MenFortressCitadel -> fortress). InheritUpgradeCreate matches this table;
	# it never guesses from runtime slugs or KindOf.
	var structure_source_object_ids: Dictionary = {}
	for kind_value in structure_kinds:
		var kind := String(kind_value)
		structure_source_object_ids[kind] = [String(structure_source_by_kind[kind])]
	for source_id_value in producer_kind_registry.keys():
		var source_id := String(source_id_value)
		var kind := String(producer_kind_registry[source_id_value])
		if not structure_source_object_ids.has(kind):
			continue
		var aliases: Array = structure_source_object_ids[kind]
		if not aliases.has(source_id):
			aliases.append(source_id)
			aliases.sort_custom(func(a, b) -> bool:
				return String(a).naturalnocasecmp_to(String(b)) < 0
			)
	for carrier_kind_value in structure_inherit_upgrades.keys():
		var carrier_kind := String(carrier_kind_value)
		for rule_value in structure_inherit_upgrades[carrier_kind] as Array:
			var rule := rule_value as Dictionary
			var source_id := String(rule.get("sourceObjectId", ""))
			var matches: Array[String] = []
			for donor_kind_value in structure_source_object_ids.keys():
				var donor_kind := String(donor_kind_value)
				for alias_value in structure_source_object_ids[donor_kind] as Array:
					if String(alias_value).nocasecmp_to(source_id) == 0:
						matches.append(donor_kind)
						break
			if matches.size() != 1:
				return {
					"_error": (
						"structure '%s' InheritUpgradeCreate source '%s' "
						+ "resolves to %d live structure kinds"
					) % [
						String(structure_source_by_kind.get(carrier_kind, carrier_kind)),
						source_id,
						matches.size(),
					]
				}
			rule["sourceKind"] = matches[0]
	# Prefer the real pack.json id from faction content roots. Composed alpha
	# packs (e.g. bfme2-men-elves-...) replace the historical men-vslice host
	# id; asserting DEFAULT_PACK_ID when that pack is not mounted makes the
	# slice fail closed on host resolution / HUD image pack-root checks.
	var resolved_pack_id := _host_pack_id_from_roots(sorted_pack_roots)
	if resolved_pack_id == "":
		resolved_pack_id = DEFAULT_PACK_ID
	return {
		"faction": slug,
		# The host slice pack (map, HUD dock, shared surfaces) stays asserted;
		# faction gameplay content arrives from the packs recorded below.
		"pack_id": resolved_pack_id,
		# Full constructable list for the builder UI / production routing.
		"structure_kinds": structure_kinds,
		# Retail start: only fortresses are pre-placed; everything else is built.
		"seed_structure_kinds": ["fortress"],
		"structure_object_ids": structure_object_ids,
		"structure_source_object_ids": structure_source_object_ids,
		# Exact policy roles survive conversion; presenters resolve citadel and
		# pad art without guessing faction identity from object-name strings.
		"fortress_composite_object_ids": fortress_composite_object_ids,
		"structure_max_health": structure_max_health,
		# Absent stays absent. Scavenger may only pay for a structure whose own
		# effective object descriptor authors BountyValue.
		"structure_bounty_values": structure_bounty_values,
		"structure_build_rules": structure_build_rules,
		# Compiled armor.ini table per kind (fractions); kinds whose structure
		# document carries no armor block are absent here and become recorded
		# provisionals in the simulation.
		"structure_armor": structure_armor,
		# Authored purchased-level chains per kind (from the structure docs'
		# compiled upgradeChain); kinds without one are simply absent.
		"structure_upgrade_chains": structure_upgrade_chains,
		# Authored PLAYER research sales per kind (the structure docs' compiled
		# research surface); kinds without one are simply absent.
		"structure_research": structure_research,
		# Authored fortress improvement sales per kind (the OBJECT_UPGRADE
		# buttons whose real upgrade retail's CastleUpgrade module hands out);
		# kinds without one are simply absent.
		"structure_castle_upgrades": structure_castle_upgrades,
		# Authored per-structure effect bindings keyed by technology id
		# (discounts, refunds, income bonuses); kinds without one are absent.
		"structure_upgrade_effects": structure_upgrade_effects,
		# Source-backed GrantUpgradeCreate rows, applied by the simulation at
		# the exact create/build-complete lifecycle edge.
		"structure_create_grants": structure_create_grants,
		# Retail InheritUpgradeCreate contracts, evaluated once on the
		# carrier's create edge against the exact source identities above.
		"structure_inherit_upgrades": structure_inherit_upgrades,
		# Parsed but not executable until their owning wall/composite lifecycle
		# is materialized. Keeping these separate prevents importer coverage
		# from masquerading as live runtime coverage.
		"deferred_structure_inherit_upgrades": deferred_structure_inherit_upgrades,
		"structure_production_exit_updates": structure_production_exit_updates,
		"deferred_structure_production_exit_updates": deferred_structure_production_exit_updates,
		"structure_auto_deposit_updates": structure_auto_deposit_updates,
		"deferred_structure_auto_deposit_updates": deferred_structure_auto_deposit_updates,
		"producer_kind_registry": producer_kind_registry,
		"unit_production_rules": unit_production_rules,
		# Doc-derived "Trains <units>" per producer kind for honest construct
		# fallback tooltips (factions without localized construct strings).
		"structure_training_summaries": structure_training_summaries,
		# Doc-driven construct-button icons per kind: each structure doc's own
		# converted construct commandbutton crop (imageBindings). Kinds with a
		# recorded binding gap are absent and stay honest text-only sockets.
		"structure_construct_icons": structure_construct_icons,
		"ai_production_plan": ai_production_plan,
		"unit_damage_types": unit_damage_types,
		"excluded_units": excluded_units,
		"spawn_roster": spawn_roster,
		"builder_unit_ids": [builder_member_id],
		"faction_pack_roots": sorted_pack_roots,
	}


static func _host_pack_id_from_roots(pack_root_list: Array) -> String:
	## Returns the pack.json id for the first readable faction pack root, or "".
	for root_value in pack_root_list:
		var root := String(root_value).strip_edges()
		if root == "":
			continue
		var pack_path := root.path_join("pack.json")
		if not FileAccess.file_exists(pack_path):
			continue
		var file := FileAccess.open(pack_path, FileAccess.READ)
		if file == null:
			continue
		var text := file.get_as_text()
		file.close()
		var json := JSON.new()
		if json.parse(text) != OK or typeof(json.data) != TYPE_DICTIONARY:
			continue
		var pack_id := String((json.data as Dictionary).get("id", "")).strip_edges()
		if pack_id != "":
			return pack_id
	return ""


static func faction_scoped_unit_runtimes(prefixes: Array, unit_runtimes: Dictionary, structure_runtimes: Dictionary, pack_index: Dictionary) -> Dictionary:
	## Resolves cross-pack same-name unit documents to the faction's own pack
	## copy. Retail authors some units at several factions' structures (the
	## evil-faction MordorWorker is built at the isengard, mordor, and wild
	## lumber mills), so each faction pack legitimately ships its own document
	## for the same Object id, bound to that faction's own producer. The flat
	## ContentDB registry keeps only the last-loaded pack's copy; here every
	## in-scope unit id is restored to the copy from the faction's own pack(s),
	## proven by the pack roots of the faction's structure documents. A
	## foreign-pack copy never wins; with no faction-owned copy the registry
	## document stands and producer validation still fails closed on genuine
	## mismatches. `pack_index` maps casefolded object ids to the load-ordered
	## per-pack documents (ContentDB.get_playable_unit_runtime_pack_index()).
	if pack_index.is_empty():
		return unit_runtimes
	var faction_roots: Dictionary = {}
	var faction_structure_ids: Dictionary = {}
	for object_id in _matching_ids(structure_runtimes, prefixes):
		faction_structure_ids[String(object_id).to_lower()] = true
		var root := String((structure_runtimes[object_id] as Dictionary).get("_pack_root", ""))
		if root != "":
			faction_roots[root] = true
	if faction_roots.is_empty():
		return unit_runtimes
	var scoped := unit_runtimes.duplicate(true)
	for object_id in _matching_ids(scoped, prefixes):
		var variants: Array = pack_index.get(String(object_id).to_lower(), []) as Array
		if variants.size() < 2:
			continue
		# Prefer the copy whose pack owns the exact producer structure named by
		# its production route. This matters when an active RotWK faction and its
		# BFME2 supplemental dependency both contribute same-prefix structures:
		# both roots are broadly faction-owned, but their command-set layouts can
		# differ (for example Gondor Barracks Tower Guard slot 2 vs 3). Pack load
		# order must not select the supplemental unit against the active
		# producer's command set.
		var producer_owned_variant: Dictionary = {}
		for variant_value in variants:
			var candidate := variant_value as Dictionary
			var candidate_root := String(candidate.get("_pack_root", ""))
			var registration := candidate.get("registration", {}) as Dictionary
			for route_value in registration.get("production", []) as Array:
				var route := route_value as Dictionary
				var producer_id := String(route.get("producerObjectId", ""))
				if (
					producer_id == ""
					or not faction_structure_ids.has(producer_id.to_lower())
					or not structure_runtimes.has(producer_id)
				):
					continue
				var producer := structure_runtimes[producer_id] as Dictionary
				if candidate_root != "" and String(producer.get("_pack_root", "")) == candidate_root:
					producer_owned_variant = candidate
					break
			if not producer_owned_variant.is_empty():
				break
		if not producer_owned_variant.is_empty():
			scoped[object_id] = producer_owned_variant
			continue
		for variant_value in variants:
			var variant := variant_value as Dictionary
			if faction_roots.has(String(variant.get("_pack_root", ""))):
				# Deterministic: the first faction-owned copy in pack load
				# order wins; producer validation below still cross-checks it.
				scoped[object_id] = variant
				break
	return scoped


static func _validate_structure_upgrade_chain(object_id: String, chain_value: Variant) -> String:
	## Fail-closed shape check for one structure document's compiled upgrade
	## chain; "" when valid. Deeper semantics (per-kind collisions, per-step
	## effects) stay with the simulation's own registration gate.
	if typeof(chain_value) != TYPE_DICTIONARY:
		return "structure '%s' upgrade chain is not a dictionary" % object_id
	var chain := chain_value as Dictionary
	var level_cap := int(chain.get("levelCap", 0))
	var steps_value: Variant = chain.get("steps")
	if level_cap < 2 or typeof(steps_value) != TYPE_ARRAY or (steps_value as Array).is_empty():
		return "structure '%s' upgrade chain is malformed" % object_id
	var previous_to_level := 1
	var seen_upgrades: Dictionary = {}
	for step_value in steps_value as Array:
		if typeof(step_value) != TYPE_DICTIONARY:
			return "structure '%s' upgrade chain has a malformed step" % object_id
		var step := step_value as Dictionary
		var upgrade_id := String(step.get("upgradeId", ""))
		var to_level := int(step.get("toLevel", 0))
		if (
			upgrade_id == ""
			or seen_upgrades.has(upgrade_id.to_lower())
			or to_level <= previous_to_level
			or to_level > level_cap
			or int(step.get("cost", -1)) < 0
			or float(step.get("buildTimeSeconds", 0.0)) <= 0.0
			or String(step.get("commandId", "")) == ""
			or String(step.get("fromCommandSet", "")) == ""
			or String(step.get("toCommandSet", "")) == ""
		):
			return "structure '%s' upgrade chain step '%s' is malformed" % [object_id, upgrade_id]
		seen_upgrades[upgrade_id.to_lower()] = true
		previous_to_level = to_level
	return ""


static func _validate_structure_research(object_id: String, research_value: Variant) -> String:
	## Fail-closed shape check for one structure document's compiled PLAYER
	## research surface; "" when valid. Deeper semantics (per-kind collisions,
	## gate resolution) stay with the simulation's own registration gate.
	if typeof(research_value) != TYPE_DICTIONARY:
		return "structure '%s' research surface is not a dictionary" % object_id
	var research := research_value as Dictionary
	var upgrades_value: Variant = research.get("upgrades")
	if typeof(upgrades_value) != TYPE_ARRAY or (upgrades_value as Array).is_empty():
		return "structure '%s' research surface is malformed" % object_id
	var seen_upgrades: Dictionary = {}
	for row_value in upgrades_value as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			return "structure '%s' research surface has a malformed row" % object_id
		var row := row_value as Dictionary
		var upgrade_id := String(row.get("upgradeId", ""))
		if (
			upgrade_id == ""
			or seen_upgrades.has(upgrade_id.to_lower())
			or int(row.get("cost", -1)) < 0
			# Zero is authored evidence, not corruption: RotWK 2.01 sells the
			# Hall of Twilight necromancy technologies at BuildCost 0 /
			# BuildTime 0 (_patch201ini.big: #define
			# ANGMAR_TECH_SOUL_FREEZE_BUILDTIME 0, likewise WELL_OF_SOULS and
			# CORPSE_RAIN). The simulation clamps research duration to >= 1
			# tick, so only a negative time is malformed.
			or float(row.get("buildTimeSeconds", -1.0)) < 0.0
			or String(row.get("commandId", "")) == ""
			or int(row.get("slot", 0)) < 1
		):
			return "structure '%s' research row '%s' is malformed" % [object_id, upgrade_id]
		seen_upgrades[upgrade_id.to_lower()] = true
	return ""


static func _validate_structure_castle_upgrades(object_id: String, castle_value: Variant) -> String:
	## Fail-closed shape check for one structure document's compiled fortress
	## improvement surface; "" when valid.
	##
	## A row is one button on the fortress's upgrades page, in any of the THREE
	## shapes retail authors. The TRIGGER shape buys a `*Trigger` upgrade whose
	## real upgrade a CastleUpgrade module hands to the castle. The PLAIN shape
	## (Banners, Siege Kegs, Oil Casks, Mighty Catapult — commandset.ini:4107
	## slots 8/9/11/13, four of the six buttons retail puts on that page) applies
	## to the fortress itself and hands out nothing, which is an EMPTY
	## `grantsUpgradeId`. Requiring a grant made those four unsellable.
	##
	## The third is the SELF-GRANTING PASS-OUT: a CastleUpgrade module whose
	## `TriggeredBy` and `Upgrade` are the SAME id. Angmar's House of Lamentation
	## is authored exactly that way (angmarfortress.ini:1235-1238,
	## `ModuleTag_PassOutHouseOfHealingUpgrade`), and Men's House of Healing rides
	## the same `Command = CASTLE_UPGRADE` button. This was previously rejected on
	## the theory that a row naming the same id twice "would silently buy
	## nothing", which is wrong: the module exists to PROPAGATE the purchased
	## upgrade from the fortress to every castle piece, so in and out are
	## legitimately equal. Rejecting it failed the whole Angmar manifest closed
	## and the slice would not boot.
	if typeof(castle_value) != TYPE_DICTIONARY:
		return "structure '%s' castle upgrade surface is not a dictionary" % object_id
	var surface := castle_value as Dictionary
	var upgrades_value: Variant = surface.get("upgrades")
	if typeof(upgrades_value) != TYPE_ARRAY or (upgrades_value as Array).is_empty():
		return "structure '%s' castle upgrade surface is malformed" % object_id
	var seen_upgrades: Dictionary = {}
	for row_value in upgrades_value as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			return "structure '%s' castle upgrade surface has a malformed row" % object_id
		var row := row_value as Dictionary
		var upgrade_id := String(row.get("upgradeId", ""))
		if (
			upgrade_id == ""
			or seen_upgrades.has(upgrade_id.to_lower())
			or int(row.get("cost", -1)) < 0
			or float(row.get("buildTimeSeconds", -1.0)) < 0.0
			or String(row.get("commandId", "")) == ""
			or int(row.get("slot", 0)) < 1
		):
			return "structure '%s' castle upgrade row '%s' is malformed" % [object_id, upgrade_id]
		seen_upgrades[upgrade_id.to_lower()] = true
	return ""


static func _validate_structure_upgrade_effects(object_id: String, effects_value: Variant) -> String:
	## Fail-closed shape check for one structure document's compiled upgrade
	## effect bindings; "" when valid. Rows the runtime cannot apply ride the
	## converter's own unsupportedEffects record — never dropped silently.
	if typeof(effects_value) != TYPE_DICTIONARY:
		return "structure '%s' upgrade effects are not a dictionary" % object_id
	var container := effects_value as Dictionary
	var effects_value_inner: Variant = container.get("effects", [])
	var unsupported_value: Variant = container.get("unsupportedEffects", [])
	if typeof(effects_value_inner) != TYPE_ARRAY or typeof(unsupported_value) != TYPE_ARRAY:
		return "structure '%s' upgrade effects are malformed" % object_id
	for row_value in effects_value_inner as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			return "structure '%s' upgrade effect row is malformed" % object_id
		var row := row_value as Dictionary
		var upgrade_id := String(row.get("upgradeId", ""))
		var kind := String(row.get("kind", ""))
		if upgrade_id == "" or kind == "":
			return "structure '%s' upgrade effect row is malformed" % object_id
	return ""


static func _validate_structure_create_grants(object_id: String, grants_value: Variant) -> String:
	if typeof(grants_value) != TYPE_ARRAY or (grants_value as Array).is_empty():
		return "structure '%s' createGrants is not a non-empty array" % object_id
	var seen: Dictionary = {}
	for row_value in grants_value as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			return "structure '%s' createGrants contains a non-dictionary row" % object_id
		var row := row_value as Dictionary
		var upgrade_id := String(row.get("upgradeId", ""))
		var upgrade_type := String(row.get("upgradeType", ""))
		var on_create: Variant = row.get("onCreateWhenComplete")
		var on_complete: Variant = row.get("onBuildComplete")
		if (
			upgrade_id == ""
			or upgrade_type not in ["OBJECT", "PLAYER"]
			or typeof(on_create) != TYPE_BOOL
			or typeof(on_complete) != TYPE_BOOL
			or (not bool(on_create) and not bool(on_complete))
			or String(row.get("module", "")) != "GrantUpgradeCreate"
			or String(row.get("sourceIni", "")) == ""
			or int(row.get("line", 0)) <= 0
		):
			return "structure '%s' has an invalid GrantUpgradeCreate row" % object_id
		var identity := "%s|%s|%s" % [
			upgrade_id.to_lower(), str(bool(on_create)), str(bool(on_complete))
		]
		if seen.has(identity):
			return "structure '%s' has duplicate GrantUpgradeCreate rows" % object_id
		seen[identity] = true
	return ""


static func _validate_structure_inherit_upgrades(object_id: String, rules_value: Variant) -> String:
	if typeof(rules_value) != TYPE_ARRAY or (rules_value as Array).is_empty():
		return "structure '%s' inheritUpgradesOnCreate is not a non-empty array" % object_id
	var seen: Dictionary = {}
	for row_value in rules_value as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			return "structure '%s' inheritUpgradesOnCreate contains a non-dictionary row" % object_id
		var row := row_value as Dictionary
		var radius_value: Variant = row.get("radius")
		if typeof(radius_value) != TYPE_DICTIONARY:
			return "structure '%s' has an invalid InheritUpgradeCreate radius" % object_id
		var radius := radius_value as Dictionary
		var upgrade_id := String(row.get("upgradeId", ""))
		var source_id := String(row.get("sourceObjectId", ""))
		if (
			upgrade_id == ""
			or String(row.get("upgradeType", "")) != "OBJECT"
			or source_id == ""
			or String(row.get("objectFilter", "")) != "ANY +%s" % source_id
			or String(row.get("module", "")) != "InheritUpgradeCreate"
			or String(radius.get("authored", "")) == ""
			or float(radius.get("value", 0.0)) <= 0.0
			or String(row.get("sourceIni", "")) == ""
			or int(row.get("line", 0)) <= 0
		):
			return "structure '%s' has an invalid InheritUpgradeCreate row" % object_id
		var identity := "%s|%s|%s" % [
			upgrade_id.to_lower(), source_id.to_lower(), str(float(radius["value"]))
		]
		if seen.has(identity):
			return "structure '%s' has duplicate InheritUpgradeCreate rows" % object_id
		seen[identity] = true
	return ""


static func _validate_structure_production_exit_updates(
	object_id: String, rules_value: Variant, module_contracts_value: Variant = null
) -> String:
	if typeof(rules_value) != TYPE_ARRAY or (rules_value as Array).is_empty():
		return "structure '%s' productionExitUpdates is not a non-empty array" % object_id
	var first_value: Variant = (rules_value as Array)[0]
	if typeof(first_value) != TYPE_DICTIONARY:
		return "structure '%s' productionExitUpdates contains a non-dictionary row" % object_id
	# Fresh descriptors expose the compiler-authoritative module-contract rows
	# verbatim. Older BFME2 packs carry the pre-promotion compatibility shape;
	# keep that strict parser below until those immutable packs are replaced.
	if (first_value as Dictionary).has("fields"):
		return _validate_canonical_production_exit_updates(
			object_id, rules_value as Array, module_contracts_value
		)
	return _validate_legacy_production_exit_updates(object_id, rules_value)


static func _validate_canonical_production_exit_updates(
	object_id: String, rules: Array, module_contracts_value: Variant
) -> String:
	if typeof(module_contracts_value) != TYPE_ARRAY:
		return "structure '%s' canonical QueueProductionExitUpdate lacks moduleContracts authority" % object_id
	var authoritative_rows: Array = []
	for contract_value in module_contracts_value as Array:
		if typeof(contract_value) != TYPE_DICTIONARY:
			return "structure '%s' moduleContracts contains a non-dictionary row" % object_id
		var contract := contract_value as Dictionary
		if String(contract.get("module", "")) == "QueueProductionExitUpdate":
			authoritative_rows.append(contract)
	if rules != authoritative_rows:
		return "structure '%s' QueueProductionExitUpdate compatibility projection drifted from moduleContracts" % object_id
	var executable_fields := {
		"UnitCreatePoint": true,
		"NaturalRallyPoint": true,
		"ExitDelay": true,
		"PlacementViewAngle": true,
		"NoExitPath": true,
	}
	for row_value in rules:
		if typeof(row_value) != TYPE_DICTIONARY:
			return "structure '%s' productionExitUpdates contains a non-dictionary row" % object_id
		var row := row_value as Dictionary
		if (
			not _has_exact_dictionary_keys(
				row,
				["carrier", "extraction", "fields", "line", "module", "runtimeStatus", "sourceIni", "tag"]
			)
			or String(row.get("carrier", "")) != "Behavior"
			or String(row.get("extraction", "")) != "typed"
			or String(row.get("module", "")) != "QueueProductionExitUpdate"
			or not String(row.get("runtimeStatus", "")) in ["executable", "deferred"]
			or typeof(row.get("fields")) != TYPE_DICTIONARY
			or typeof(row.get("sourceIni")) != TYPE_STRING
			or String(row.get("sourceIni", "")) == ""
			or typeof(row.get("tag")) != TYPE_STRING
			or String(row.get("tag", "")) == ""
			or not _is_json_integral(row.get("line"))
			or int(row.get("line", 0)) <= 0
		):
			return "structure '%s' has an invalid canonical QueueProductionExitUpdate row" % object_id
		if String(row.get("runtimeStatus", "")) == "deferred":
			continue
		var fields := row.get("fields", {}) as Dictionary
		for field_value in fields.keys():
			if not executable_fields.has(String(field_value)):
				return "structure '%s' executable QueueProductionExitUpdate has unsupported fields" % object_id
		var create_points: Variant = fields.get("UnitCreatePoint")
		if typeof(create_points) != TYPE_ARRAY or (create_points as Array).is_empty():
			return "structure '%s' executable QueueProductionExitUpdate lacks UnitCreatePoint" % object_id
		for coordinate_name in ["UnitCreatePoint", "NaturalRallyPoint"]:
			var coordinate_rows: Variant = fields.get(coordinate_name, [])
			if typeof(coordinate_rows) != TYPE_ARRAY:
				return "structure '%s' executable QueueProductionExitUpdate has malformed coordinates" % object_id
			for coordinate_value in coordinate_rows as Array:
				if typeof(coordinate_value) != TYPE_DICTIONARY:
					return "structure '%s' executable QueueProductionExitUpdate has malformed coordinates" % object_id
				var coordinate := coordinate_value as Dictionary
				var value: Variant = coordinate.get("value")
				if coordinate.get("validNumeric") != true or typeof(value) != TYPE_DICTIONARY:
					return "structure '%s' executable QueueProductionExitUpdate has invalid numeric coordinates" % object_id
				for axis in ["x", "y", "z"]:
					var axis_value: Variant = (value as Dictionary).get(axis)
					if typeof(axis_value) not in [TYPE_INT, TYPE_FLOAT]:
						return "structure '%s' executable QueueProductionExitUpdate has invalid numeric coordinates" % object_id
	return ""


static func _validate_legacy_production_exit_updates(
	object_id: String, rules_value: Variant
) -> String:
	# The composed runtime envelope intentionally carries only the
	# compiler-validated gameplay projection, not descriptor sourceDocuments.
	# compose_structure_runtime_document() validates the descriptor self-digest
	# (integrity, not retail authenticity), its closed schema, and its internally
	# consistent SHA-bearing source-path table before creating this envelope.
	# Source-byte authenticity belongs to the importer build and pack-receipt
	# boundary. Keep this runtime boundary shape-only; it cannot re-attest source
	# bytes from a projection which deliberately omits that provenance table.
	if typeof(rules_value) != TYPE_ARRAY or (rules_value as Array).is_empty():
		return "structure '%s' productionExitUpdates is not a non-empty array" % object_id
	for row_value in rules_value as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			return "structure '%s' productionExitUpdates contains a non-dictionary row" % object_id
		var row := row_value as Dictionary
		if (
			not _has_exact_dictionary_keys(
				row,
				[
					"module",
					"unitCreatePoint",
					"naturalRallyPoint",
					"exitDelay",
					"allowAirborneCreation",
					"initialBurst",
					"deferredFields",
					"runtimeStatus",
					"sourceIni",
					"line",
				]
			)
			or
			String(row.get("module", "")) != "QueueProductionExitUpdate"
			or String(row.get("runtimeStatus", "")) != "deferred"
			or typeof(row.get("sourceIni")) != TYPE_STRING
			or String(row.get("sourceIni", "")) == ""
			or not _is_json_integral(row.get("line"))
			or int(row.get("line", 0)) <= 0
		):
			return "structure '%s' has an invalid deferred QueueProductionExitUpdate row" % object_id
		for field_name in ["unitCreatePoint", "naturalRallyPoint"]:
			var coord_field_value: Variant = row.get(field_name)
			if typeof(coord_field_value) != TYPE_DICTIONARY:
				return "structure '%s' has an invalid QueueProductionExitUpdate coordinate" % object_id
			var coord_field := coord_field_value as Dictionary
			var coord_defaulted_value: Variant = coord_field.get("defaulted")
			var coord_defaulted := coord_field.has("defaulted")
			var expected_coord_keys := (
				["authored", "value", "defaulted"]
				if coord_defaulted
				else ["authored", "value", "sourceIni", "line"]
			)
			if (
				not _has_exact_dictionary_keys(coord_field, expected_coord_keys)
				or (
					coord_defaulted
					and (
						typeof(coord_defaulted_value) != TYPE_BOOL
						or coord_defaulted_value != true
					)
				)
			):
				return "structure '%s' has an invalid QueueProductionExitUpdate coordinate" % object_id
			var coordinate_value: Variant = coord_field.get("value")
			if typeof(coordinate_value) != TYPE_DICTIONARY:
				return "structure '%s' has an invalid QueueProductionExitUpdate coordinate" % object_id
			var coordinate := coordinate_value as Dictionary
			var authored_coordinate := _queue_exit_authored_coordinate(
				String(coord_field.get("authored", ""))
			)
			if (
				coordinate.size() != 3
				or not coordinate.has("x")
				or not coordinate.has("y")
				or not coordinate.has("z")
				or typeof(coordinate["x"]) != TYPE_FLOAT
				or typeof(coordinate["y"]) != TYPE_FLOAT
				or typeof(coordinate["z"]) != TYPE_FLOAT
			):
				return "structure '%s' has an invalid QueueProductionExitUpdate coordinate" % object_id
			if (
				typeof(coord_field.get("authored")) != TYPE_STRING
				or (
					coord_defaulted
					and (
						String(coord_field["authored"]) != ""
						or float(coordinate["x"]) != 0.0
						or float(coordinate["y"]) != 0.0
						or float(coordinate["z"]) != 0.0
					)
				)
				or (
					not coord_defaulted
					and (
						String(coord_field["authored"]) == ""
						or authored_coordinate.is_empty()
						or float(authored_coordinate.get("x", NAN))
						!= float(coordinate["x"])
						or float(authored_coordinate.get("y", NAN))
						!= float(coordinate["y"])
						or float(authored_coordinate.get("z", NAN))
						!= float(coordinate["z"])
						or typeof(coord_field.get("sourceIni")) != TYPE_STRING
						or String(coord_field.get("sourceIni", "")) == ""
						or not _is_json_integral(coord_field.get("line"))
						or int(coord_field.get("line", 0)) <= 0
					)
				)
			):
				return "structure '%s' has an invalid QueueProductionExitUpdate coordinate" % object_id
		for field_name in ["exitDelay", "initialBurst"]:
			var integer_field_value: Variant = row.get(field_name)
			if typeof(integer_field_value) != TYPE_DICTIONARY:
				return "structure '%s' has an invalid QueueProductionExitUpdate integer" % object_id
			var integer_field := integer_field_value as Dictionary
			var integer_defaulted_value: Variant = integer_field.get("defaulted")
			var integer_defaulted := integer_field.has("defaulted")
			var expected_integer_keys: Array = []
			if integer_defaulted:
				expected_integer_keys = (
					["authored", "value", "defaulted", "unit"]
					if field_name == "exitDelay"
					else ["authored", "value", "defaulted"]
				)
			else:
				expected_integer_keys = ["authored", "value", "sourceIni", "line"]
				if field_name == "exitDelay":
					expected_integer_keys.append("unit")
				if integer_field.has("resolvedDefine"):
					expected_integer_keys.append("resolvedDefine")
			if (
				not _has_exact_dictionary_keys(integer_field, expected_integer_keys)
				or (
					integer_defaulted
					and (
						typeof(integer_defaulted_value) != TYPE_BOOL
						or integer_defaulted_value != true
					)
				)
				or typeof(integer_field.get("authored")) != TYPE_STRING
				or not _is_json_integral(integer_field.get("value"))
				or not _queue_exit_authored_integer_matches(integer_field)
				or int(integer_field["value"]) < 0
				or int(integer_field["value"]) > 4294967295
				or (
					field_name == "exitDelay"
					and String(integer_field.get("unit", "")) != "milliseconds"
				)
				or (
					field_name != "exitDelay"
					and integer_field.has("unit")
				)
				or (
					integer_defaulted
					and (
						String(integer_field["authored"]) != "0"
						or int(integer_field["value"]) != 0
					)
				)
				or (
					not integer_defaulted
					and (
						String(integer_field["authored"]) == ""
						or typeof(integer_field.get("sourceIni")) != TYPE_STRING
						or String(integer_field.get("sourceIni", "")) == ""
						or not _is_json_integral(integer_field.get("line"))
						or int(integer_field.get("line", 0)) <= 0
					)
				)
			):
				return "structure '%s' has an invalid QueueProductionExitUpdate integer" % object_id
		var airborne_value: Variant = row.get("allowAirborneCreation")
		if typeof(airborne_value) != TYPE_DICTIONARY:
			return "structure '%s' has an invalid QueueProductionExitUpdate boolean" % object_id
		var airborne := airborne_value as Dictionary
		var airborne_defaulted_value: Variant = airborne.get("defaulted")
		var airborne_defaulted := airborne.has("defaulted")
		var expected_airborne_keys := (
			["authored", "value", "defaulted"]
			if airborne_defaulted
			else ["authored", "value", "sourceIni", "line"]
		)
		if (
			not _has_exact_dictionary_keys(airborne, expected_airborne_keys)
			or (
				airborne_defaulted
				and (
					typeof(airborne_defaulted_value) != TYPE_BOOL
					or airborne_defaulted_value != true
				)
			)
			or typeof(airborne.get("authored")) != TYPE_STRING
			or typeof(airborne.get("value")) != TYPE_BOOL
			or (
				airborne_defaulted
				and (
					String(airborne["authored"]) != "No"
					or bool(airborne["value"])
				)
			)
			or (
				not airborne_defaulted
				and (
					String(airborne["authored"]) == ""
					or not ["yes", "no"].has(
						String(airborne["authored"]).strip_edges().to_lower()
					)
					or bool(airborne["value"])
					!= (
						String(airborne["authored"]).strip_edges().to_lower()
						== "yes"
					)
					or typeof(airborne.get("sourceIni")) != TYPE_STRING
					or String(airborne.get("sourceIni", "")) == ""
					or not _is_json_integral(airborne.get("line"))
					or int(airborne.get("line", 0)) <= 0
				)
			)
		):
			return "structure '%s' has an invalid QueueProductionExitUpdate boolean" % object_id
		var deferred_value: Variant = row.get("deferredFields")
		if typeof(deferred_value) != TYPE_ARRAY:
			return "structure '%s' has invalid QueueProductionExitUpdate deferred fields" % object_id
		var seen: Dictionary = {}
		for deferred_row_value in deferred_value as Array:
			if typeof(deferred_row_value) != TYPE_DICTIONARY:
				return "structure '%s' has invalid QueueProductionExitUpdate deferred fields" % object_id
			var deferred_row := deferred_row_value as Dictionary
			var deferred_name := String(deferred_row.get("name", "")).to_lower()
			if (
				not _has_exact_dictionary_keys(
					deferred_row,
					["name", "authored", "sourceIni", "line", "reason"]
				)
				or typeof(deferred_row.get("name")) != TYPE_STRING
				or not [
					"placementviewangle",
					"usereturntoformation",
					"noexitpath",
				].has(deferred_name)
				or seen.has(deferred_name)
				or typeof(deferred_row.get("authored")) != TYPE_STRING
				or String(deferred_row.get("authored", "")) == ""
				or typeof(deferred_row.get("sourceIni")) != TYPE_STRING
				or String(deferred_row.get("sourceIni", "")) == ""
				or not _is_json_integral(deferred_row.get("line"))
				or int(deferred_row.get("line", 0)) <= 0
				or typeof(deferred_row.get("reason")) != TYPE_STRING
				or String(deferred_row.get("reason", ""))
				!= "bfme-field-without-local-runtime-oracle"
			):
				return "structure '%s' has invalid QueueProductionExitUpdate deferred fields" % object_id
			seen[deferred_name] = true
	return ""


static func _validate_structure_auto_deposit_updates(
	object_id: String, rules_value: Variant
) -> String:
	if typeof(rules_value) != TYPE_ARRAY or (rules_value as Array).is_empty():
		return "structure '%s' autoDepositUpdates is not a non-empty array" % object_id
	for row_value in rules_value as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			return "structure '%s' autoDepositUpdates contains a non-dictionary row" % object_id
		var row := row_value as Dictionary
		if (
			not _has_exact_dictionary_keys(
				row,
				[
					"module",
					"depositTiming",
					"depositAmount",
					"initialCaptureBonus",
					"actualMoney",
					"upgradedBoosts",
					"deferredFields",
					"runtimeStatus",
					"sourceIni",
					"line",
				]
			)
			or String(row.get("module", "")) != "AutoDepositUpdate"
			or not ["executable", "deferred"].has(
				String(row.get("runtimeStatus", ""))
			)
			or typeof(row.get("sourceIni")) != TYPE_STRING
			or String(row.get("sourceIni", "")) == ""
			or not _is_json_integral(row.get("line"))
			or int(row.get("line", 0)) <= 0
		):
			return "structure '%s' has an invalid AutoDepositUpdate row" % object_id
		var timing_value: Variant = row.get("depositTiming")
		if typeof(timing_value) != TYPE_DICTIONARY:
			return "structure '%s' has invalid AutoDepositUpdate timing" % object_id
		var timing := timing_value as Dictionary
		var timing_copy := timing.duplicate()
		if (
			String(timing_copy.get("unit", "")) != "milliseconds"
			or not _is_json_integral(timing_copy.get("simulationTicks"))
		):
			return "structure '%s' has invalid AutoDepositUpdate timing" % object_id
		timing_copy.erase("unit")
		var simulation_ticks := int(timing_copy.get("simulationTicks", -1))
		timing_copy.erase("simulationTicks")
		if (
			not _valid_auto_deposit_integer(timing_copy, true)
			or simulation_ticks
			!= (
				(int(timing.get("value", 0)) + 99) / 100
				if int(timing.get("value", 0)) > 0
				else 0
			)
		):
			return "structure '%s' has invalid AutoDepositUpdate timing" % object_id
		if (
			not _valid_auto_deposit_integer(row.get("depositAmount"), false)
			or not _valid_auto_deposit_integer(
				row.get("initialCaptureBonus"), false
			)
			or not _valid_auto_deposit_bool(row.get("actualMoney"))
		):
			return "structure '%s' has invalid AutoDepositUpdate scalar" % object_id
		var boosts_value: Variant = row.get("upgradedBoosts")
		if typeof(boosts_value) != TYPE_ARRAY:
			return "structure '%s' has invalid AutoDepositUpdate boosts" % object_id
		for boost_value in boosts_value as Array:
			if typeof(boost_value) != TYPE_DICTIONARY:
				return "structure '%s' has invalid AutoDepositUpdate boost" % object_id
			var boost := boost_value as Dictionary
			if (
				not _has_exact_dictionary_keys(
					boost,
					[
						"upgradeId",
						"upgradeType",
						"upgradeAttestation",
						"boost",
						"authored",
						"sourceIni",
						"line",
					]
				)
				or String(boost.get("upgradeId", "")) == ""
				or String(boost.get("upgradeType", "")) != "PLAYER"
				or typeof(boost.get("upgradeAttestation")) != TYPE_DICTIONARY
				or not _is_json_integral(boost.get("boost"))
				or String(boost.get("authored", "")) == ""
				or String(boost.get("sourceIni", "")) == ""
				or int(boost.get("line", 0)) <= 0
			):
				return "structure '%s' has invalid AutoDepositUpdate boost" % object_id
			var attestation := boost["upgradeAttestation"] as Dictionary
			if (
				not _has_exact_dictionary_keys(
					attestation,
					[
						"upgradeId",
						"upgradeType",
						"sourceIni",
						"sourceSha256",
					]
				)
				or String(attestation.get("upgradeId", ""))
				!= String(boost["upgradeId"])
				or String(attestation.get("upgradeType", "")) != "PLAYER"
				or String(attestation.get("sourceIni", "")).to_lower()
				!= "data/ini/upgrade.ini"
				or String(attestation.get("sourceSha256", "")).length() != 64
				or not String(attestation.get("sourceSha256", ""))
				.is_valid_hex_number(false)
			):
				return "structure '%s' has invalid AutoDepositUpdate upgrade attestation" % object_id
			var authored_tokens := (
				String(boost["authored"])
				.replace(":", " ")
				.split(" ", false)
			)
			if (
				authored_tokens.size() != 4
				or String(authored_tokens[0]).to_lower() != "upgradetype"
				or String(authored_tokens[1]) != String(boost["upgradeId"])
				or String(authored_tokens[2]).to_lower() != "boost"
				or not String(authored_tokens[3]).is_valid_int()
				or int(authored_tokens[3]) != int(boost["boost"])
			):
				return "structure '%s' has invalid AutoDepositUpdate boost projection" % object_id
		var deferred_value: Variant = row.get("deferredFields")
		if typeof(deferred_value) != TYPE_ARRAY:
			return "structure '%s' has invalid AutoDepositUpdate deferred fields" % object_id
		var seen_deferred: Dictionary = {}
		for deferred_value_row in deferred_value as Array:
			if typeof(deferred_value_row) != TYPE_DICTIONARY:
				return "structure '%s' has invalid AutoDepositUpdate deferred field" % object_id
			var deferred := deferred_value_row as Dictionary
			var deferred_name := String(deferred.get("name", "")).to_lower()
			if (
				not _has_exact_dictionary_keys(
					deferred,
					["name", "authored", "sourceIni", "line", "reason"]
				)
				or not ["givenoxp", "onlywhengarrisoned"].has(deferred_name)
				or seen_deferred.has(deferred_name)
				or String(deferred.get("authored", "")) == ""
				or String(deferred.get("sourceIni", "")) == ""
				or int(deferred.get("line", 0)) <= 0
				or String(deferred.get("reason", ""))
				!= "bfme-field-without-local-runtime-oracle"
			):
				return "structure '%s' has invalid AutoDepositUpdate deferred field" % object_id
			seen_deferred[deferred_name] = true
		if (
			(String(row.get("runtimeStatus", "")) == "executable")
			!= (deferred_value as Array).is_empty()
		):
			return "structure '%s' has invalid AutoDepositUpdate runtime status" % object_id
		for default_pair in [
			[timing, 0],
			[row.get("depositAmount"), 0],
			[row.get("initialCaptureBonus"), 0],
		]:
			var default_field := default_pair[0] as Dictionary
			if (
				default_field.has("defaulted")
				and (
					bool(default_field.get("defaulted", false)) != true
					or int(default_field.get("value", -1)) != int(default_pair[1])
					or String(default_field.get("authored", ""))
					!= str(int(default_pair[1]))
				)
			):
				return "structure '%s' has invalid AutoDepositUpdate default" % object_id
	return ""


static func _valid_auto_deposit_integer(
	field_value: Variant, unsigned: bool
) -> bool:
	if typeof(field_value) != TYPE_DICTIONARY:
		return false
	var field := field_value as Dictionary
	var defaulted := field.has("defaulted")
	var expected_keys := (
		["authored", "value", "defaulted"]
		if defaulted
		else ["authored", "value", "sourceIni", "line"]
	)
	if not defaulted and field.has("resolvedDefine"):
		expected_keys.append("resolvedDefine")
	if (
		not _has_exact_dictionary_keys(field, expected_keys)
		or typeof(field.get("authored")) != TYPE_STRING
		or not _is_json_integral(field.get("value"))
	):
		return false
	var value := int(field["value"])
	if (
		(unsigned and (value < 0 or value > 4294967295))
		or (
			not unsigned
			and (value < -2147483648 or value > 2147483647)
		)
	):
		return false
	if defaulted:
		return (
			typeof(field.get("defaulted")) == TYPE_BOOL
			and bool(field["defaulted"])
			and String(field["authored"]) == str(value)
		)
	return (
		String(field["authored"]) != ""
		and typeof(field.get("sourceIni")) == TYPE_STRING
		and String(field["sourceIni"]) != ""
		and _is_json_integral(field.get("line"))
		and int(field["line"]) > 0
	)


static func _valid_auto_deposit_bool(field_value: Variant) -> bool:
	if typeof(field_value) != TYPE_DICTIONARY:
		return false
	var field := field_value as Dictionary
	var defaulted := field.has("defaulted")
	var expected_keys := (
		["authored", "value", "defaulted"]
		if defaulted
		else ["authored", "value", "sourceIni", "line"]
	)
	var authored := String(field.get("authored", "")).strip_edges().to_lower()
	return (
		_has_exact_dictionary_keys(field, expected_keys)
		and ["yes", "no"].has(authored)
		and typeof(field.get("value")) == TYPE_BOOL
		and bool(field["value"]) == (authored == "yes")
		and (
			not defaulted
			or (
				typeof(field.get("defaulted")) == TYPE_BOOL
				and bool(field["defaulted"])
				and authored == "yes"
			)
		)
		and (
			defaulted
			or (
				String(field.get("sourceIni", "")) != ""
				and int(field.get("line", 0)) > 0
			)
		)
	)



static func _is_json_integral(value: Variant) -> bool:
	## Godot JSON.parse_string yields TYPE_FLOAT for whole numbers. Structure
	## contracts authored as integers must still validate after load.
	if typeof(value) == TYPE_INT:
		return true
	if typeof(value) != TYPE_FLOAT:
		return false
	var number := float(value)
	return is_finite(number) and number == floor(number)


static func _json_integral(value: Variant, default_value: int = 0) -> int:
	if typeof(value) == TYPE_INT:
		return int(value)
	if typeof(value) == TYPE_FLOAT:
		var number := float(value)
		if is_finite(number) and number == floor(number):
			return int(number)
	return default_value

static func _has_exact_dictionary_keys(value: Dictionary, expected: Array) -> bool:
	if value.size() != expected.size():
		return false
	for key_value in expected:
		if not value.has(key_value):
			return false
	return true


static func _queue_exit_authored_integer_matches(field: Dictionary) -> bool:
	var authored := String(field.get("authored", ""))
	var value_value: Variant = field.get("value")
	if not _is_json_integral(value_value):
		return false
	var decimal_expression := RegEx.new()
	if decimal_expression.compile("^[0-9]+$") != OK:
		return false
	if decimal_expression.search(authored) != null:
		return (
			not field.has("resolvedDefine")
			and authored.is_valid_int()
			and authored.to_int() == int(value_value)
		)
	var define_expression := RegEx.new()
	if define_expression.compile("^[A-Za-z_][A-Za-z0-9_]*$") != OK:
		return false
	if define_expression.search(authored) == null:
		return false
	var resolved_value: Variant = field.get("resolvedDefine")
	if typeof(resolved_value) != TYPE_DICTIONARY:
		return false
	var resolved := resolved_value as Dictionary
	return (
		resolved.size() == 2
		and resolved.has("name")
		and resolved.has("value")
		and typeof(resolved.get("name")) == TYPE_STRING
		and String(resolved["name"]) == authored
		and _is_json_integral(resolved.get("value"))
		and int(resolved["value"]) == int(value_value)
	)


static func _queue_exit_authored_coordinate(authored: String) -> Dictionary:
	## Parse retail Coord3D authored forms. Mirrors the importer's known-typo
	## repair for RotWK AngmarKennelExpansion (`X:70.0.0` → `X:70.0`).
	var expression := RegEx.new()
	var compile_error := expression.compile(
		"(?i)^\\s*X\\s*:\\s*([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][+-]?\\d+)?)\\s+"
		+ "Y\\s*:\\s*([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][+-]?\\d+)?)\\s+"
		+ "Z\\s*:\\s*([+-]?(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][+-]?\\d+)?)\\s*$"
	)
	if compile_error != OK:
		return {}
	var matched := expression.search(authored)
	if matched == null:
		var repair := RegEx.new()
		if repair.compile("(?i)(X|Y|Z)\\s*:\\s*([+-]?(?:\\d+\\.\\d+|\\d+|\\.\\d+))\\.0(?=\\s|$)") != OK:
			return {}
		var repaired := repair.sub(authored.strip_edges(), "$1:$2", true)
		matched = expression.search(repaired)
		if matched == null:
			return {}
	return {
		"x": float(matched.get_string(1)),
		"y": float(matched.get_string(2)),
		"z": float(matched.get_string(3)),
	}


static func _content_db_pack_index() -> Dictionary:
	## The cross-pack unit variant index, when a ContentDB autoload provides
	## one. Fixtures which call from_registries with bare dictionaries get no
	## index and keep the registry documents exactly as passed.
	var tree := Engine.get_main_loop() as SceneTree
	if tree == null:
		return {}
	var content_db := tree.root.get_node_or_null("ContentDB")
	if content_db == null or not content_db.has_method("get_playable_unit_runtime_pack_index"):
		return {}
	return content_db.get_playable_unit_runtime_pack_index() as Dictionary


static func _compiled_structure_armor(document: Dictionary) -> Dictionary:
	## Normalize a playableStructure document's compiled armor.ini contract to
	## the fraction table the simulation consumes. A missing armor block remains
	## a stale-pack gap. An explicit null setId is different: the importer proved
	## the object ancestry authors no ArmorSet, so SAGE applies unmodified damage.
	var projection := StructureArmorContract.normalize_registration_armor(document)
	if projection.has("error") or not bool(projection.get("present", false)):
		return {}
	return (projection.get("table", {}) as Dictionary).duplicate(true)


static func _structure_construct_icon(document: Dictionary) -> Dictionary:
	## The structure doc's own construct-button icon: the authored construct
	## commandbutton's ButtonImage (buttonImageId on the construct route)
	## resolved through the doc's registration.presentation.imageBindings.
	## {} when the doc records a gap instead — the HUD then keeps the honest
	## text-only socket rather than borrowing another faction's art.
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var bindings: Dictionary = (registration.get("presentation", {}) as Dictionary).get("imageBindings", {}) as Dictionary
	if bindings.is_empty():
		return {}
	for route_value in (registration.get("production", {}) as Dictionary).get("routes", []) as Array:
		var route := route_value as Dictionary
		if String(route.get("surface", "")) != "construct":
			continue
		var image_id := String(route.get("buttonImageId", ""))
		if image_id != "" and bindings.has(image_id):
			return {
				"image_id": image_id,
				"structure_object_id": String(document.get("objectId", "")),
			}
	return {}


static func _composite_authors_producer(document: Dictionary, producer: Dictionary) -> bool:
	## Fail-closed cross-check for the fortress-composite producer fold: the
	## engine-spawned composite's recorded command sets must author the exact
	## command set, slot, and command the unit's producer binding cites.
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var gameplay: Dictionary = registration.get("gameplay", {}) as Dictionary
	var command_set_id := String(producer.get("command_set_id", "")).to_lower()
	var command_id := String(producer.get("command_id", "")).to_lower()
	var slot := int(producer.get("slot", 0))
	for set_value in gameplay.get("trainedCommandSets", []) as Array:
		var set_row := set_value as Dictionary
		if String(set_row.get("id", "")).to_lower() != command_set_id:
			continue
		for slot_value in set_row.get("slots", []) as Array:
			var slot_row := slot_value as Dictionary
			if int(slot_row.get("slot", -1)) == slot and String(slot_row.get("commandId", "")).to_lower() == command_id:
				return true
	return false


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


static func _spawn_category_priority(category: String) -> int:
	## Lower is preferred for the opening spawn roster / AI production seed.
	match category:
		"infantry":
			return 0
		"ranged-infantry":
			return 1
		"cavalry":
			return 2
		"siege":
			return 3
		"monster":
			return 4
		"naval":
			return 5
		"hero":
			return 6
		_:
			return 9


## Maps stripped retail slugs onto the sim/HUD kind vocabulary used by the
## historical Men tables (archery_range, battle_tower, …).
const STRUCTURE_KIND_ALIASES := {
	"archerrange": "archery_range",
	"archeryrange": "archery_range",
	"barracks": "barracks",
	"farm": "farm",
	"stable": "stable",
	"workshop": "workshop",
	"fortress": "fortress",
	"marketplace": "marketplace",
	"forge": "forge",
	"well": "well",
	"statue": "statue",
	"battletower": "battle_tower",
	"castlewallhub": "castle_wall_hub",
	"castlewallsegment": "castle_wall_segment",
	"castlewalltower": "castle_wall_tower",
	"castlewalltrebuchet": "castle_wall_trebuchet",
	"arrowtowerexpansion": "arrow_tower_expansion",
	"garrisontowerexpansion": "garrison_tower_expansion",
	"trebuchetexpansion": "trebuchet_expansion",
	"trebuchetsideexpansion": "trebuchet_side_expansion",
	"wallcliffcap": "wall_cliff_cap",
	"wallgate": "wall_gate",
	"wallhubsmall": "wall_hub_small",
	"wallhubsmallexpansion": "wall_hub_small_expansion",
	"wallhubsmallouter": "wall_hub_small_outer",
	"wallposterngate": "wall_postern_gate",
	"wallsegmentsmall": "wall_segment_small",
	"fortresscitadel": "fortress_citadel",
	"fortressexpansionpadcorner": "fortress_expansion_pad_corner",
	"fortressexpansionpadside": "fortress_expansion_pad_side",
}


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
	kind = kind.replace("-", "_").to_lower()
	# Collapse undashed retail slugs (gondorarcherrange → archerrange).
	var compact := kind.replace("_", "")
	if STRUCTURE_KIND_ALIASES.has(compact):
		return String(STRUCTURE_KIND_ALIASES[compact])
	if STRUCTURE_KIND_ALIASES.has(kind):
		return String(STRUCTURE_KIND_ALIASES[kind])
	return kind


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
