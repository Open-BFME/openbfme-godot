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

const DEFAULT_FACTION := "men"
## Historical single-faction host pack id. This is a FAST-PATH hint, never an
## identity requirement: a composed pack is id'd `bfme2-<a>-<b>-…-vslice` and
## hosts Men just as fully. Every consumer that resolves a host pack falls back
## to pack.json's `factionImportCoverage` (ModLoader.pack_provides_faction /
## RetailVerticalSlice._pack_root_hosting_faction / MainMenu.
## _mounted_pack_root_hosting_faction) when this literal does not match.
const DEFAULT_PACK_ID := "bfme2-men-vslice"
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


static func default_manifest() -> Dictionary:
	return {
		"faction": DEFAULT_FACTION,
		"pack_id": DEFAULT_PACK_ID,
		"structure_kinds": SimScript.STRUCTURE_KINDS.duplicate(),
		# Tiny pack seeds the full five-building starter base (historical slice).
		"seed_structure_kinds": SimScript.STRUCTURE_KINDS.duplicate(),
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
	var structure_armor: Dictionary = {}
	var structure_upgrade_chains: Dictionary = {}
	var structure_research: Dictionary = {}
	var structure_upgrade_effects: Dictionary = {}
	var producer_kind_registry: Dictionary = {}
	var builder_sources: Dictionary = {}
	var pack_roots: Dictionary = {}
	var fortress_kind := ""
	var excluded_structures: Dictionary = {}
	var structure_construct_icons: Dictionary = {}
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
		structure_build_rules[kind] = {"cost": int(cost), "seconds": seconds}
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
		var upgrade_effects: Variant = (registration.get("gameplay", {}) as Dictionary).get("upgradeEffects")
		if upgrade_effects != null:
			var effects_error := _validate_structure_upgrade_effects(object_id, upgrade_effects)
			if effects_error != "":
				return {"_error": effects_error}
			structure_upgrade_effects[kind] = (upgrade_effects as Dictionary).duplicate(true)
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
		if PlayableUnitAdapter.is_ring_hero_summon(unit_document):
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
				return {"_error": "unit '%s' has unresolved simulation evidence and cannot join the spawn roster" % unit_id, "excluded_units": production_exclusions.duplicate(true)}
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
	return {
		"faction": slug,
		# The host slice pack (map, HUD dock, shared surfaces) stays asserted;
		# faction gameplay content arrives from the packs recorded below.
		"pack_id": DEFAULT_PACK_ID,
		# Full constructable list for the builder UI / production routing.
		"structure_kinds": structure_kinds,
		# Retail start: only fortresses are pre-placed; everything else is built.
		"seed_structure_kinds": ["fortress"],
		"structure_object_ids": structure_object_ids,
		"structure_max_health": structure_max_health,
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
		# Authored per-structure effect bindings keyed by technology id
		# (discounts, refunds, income bonuses); kinds without one are absent.
		"structure_upgrade_effects": structure_upgrade_effects,
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
	for object_id in _matching_ids(structure_runtimes, prefixes):
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
	## the fraction table the simulation consumes. {} when the document has no
	## armor block (stale pack) or records the no-authored-set SAGE passthrough
	## (the simulation's recorded provisional path covers both, loudly).
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var gameplay: Dictionary = registration.get("gameplay", {}) as Dictionary
	var armor_value: Variant = gameplay.get("armor", null)
	if typeof(armor_value) != TYPE_DICTIONARY:
		return {}
	var armor := armor_value as Dictionary
	if armor.get("setId") == null or String(armor.get("setId", "")) == "":
		return {}
	var table: Dictionary = armor.get("table", {}) as Dictionary
	var scalars := {}
	var default_percent := 100.0
	var default_value: Variant = table.get("default", {})
	if typeof(default_value) == TYPE_DICTIONARY:
		default_percent = float((default_value as Dictionary).get("percent", 100.0))
	scalars["default"] = default_percent / 100.0
	for key_value in (table.get("scalars", {}) as Dictionary).keys():
		var row: Variant = (table.get("scalars", {}) as Dictionary).get(key_value)
		if typeof(row) != TYPE_DICTIONARY:
			continue
		scalars[String(key_value)] = float((row as Dictionary).get("percent", default_percent)) / 100.0
	return {
		"set_id": String(armor.get("setId", "")),
		"damage_scalar": float((table.get("damageScalar", {}) as Dictionary).get("percent", 100.0)) / 100.0,
		"scalars": scalars,
	}


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
