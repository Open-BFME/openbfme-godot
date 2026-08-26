extends RefCounted
## Setup-time configuration compiler carved out of retail_slice_sim.gd (drawer 17): faction manifest admission, armor/damage-scalar/weapon-upgrade/banner/castle-behavior compilation, playable unit runtime contracts, ranger/trebuchet contracts, structure upgrade chains and research contracts.
## State stays on the sim; the sim keeps one-line delegates under the original names.

var _sim_ref: WeakRef
var sim:
	get:
		return _sim_ref.get_ref()

func _init(owning_sim) -> void:
	_sim_ref = weakref(owning_sim)

func _configure_faction_manifest() -> bool:
	## Every faction-scoped table flows through the manifest. The 8 core
	## tables are REQUIRED (Q80): a rules dictionary without them is refused
	## by name — invented defaults were removed and never fall back silently.
	var manifest_value: Variant = sim._rules.get("faction_manifest", {})
	if typeof(manifest_value) != TYPE_DICTIONARY:
		sim.configuration_error = "Faction manifest is not a dictionary"
		return false
	var manifest := manifest_value as Dictionary
	if manifest.has("_error"):
		sim.configuration_error = "Faction manifest is invalid: %s" % String(manifest.get("_error", ""))
		return false
	var typed_expectations := {
		"unit_production_rules": TYPE_DICTIONARY,
		"ai_production_plan": TYPE_ARRAY,
		"structure_kinds": TYPE_ARRAY,
		"structure_max_health": TYPE_DICTIONARY,
		"structure_build_rules": TYPE_DICTIONARY,
		"structure_armor": TYPE_DICTIONARY,
		"unit_damage_types": TYPE_DICTIONARY,
		"spawn_roster": TYPE_ARRAY,
		"builder_unit_ids": TYPE_ARRAY,
	}
	for key in typed_expectations:
		if manifest.has(key) and typeof(manifest.get(key)) != int(typed_expectations[key]):
			sim.configuration_error = "Faction manifest field '%s' has the wrong type" % String(key)
			return false
	# Manifest keys that are now required (no fallback defaults permitted)
	var required_keys := ["unit_production_rules", "ai_production_plan", "structure_kinds", 
		"structure_max_health", "structure_build_rules", "unit_damage_types", 
		"structure_armor", "spawn_roster"]
	for req_key in required_keys:
		if not manifest.has(req_key):
			sim.configuration_error = "Faction manifest is missing required field '%s' (pack must carry it; invented defaults were removed)" % req_key
			return false
	sim._unit_production_rules = (manifest.get("unit_production_rules") as Dictionary).duplicate(true)
	var plan: Array = Array(manifest.get("ai_production_plan"))
	var kinds: Array = Array(manifest.get("structure_kinds"))
	for table in [plan, kinds]:
		for value in table as Array:
			if typeof(value) != TYPE_STRING or String(value).strip_edges() == "":
				sim.configuration_error = "Faction manifest plan or structure kinds contain a non-string entry"
				return false
	sim._production_unit_order.assign(plan)
	sim._ai_production_plan.assign(plan)
	sim._structure_kinds.assign(kinds)
	var seed_kinds: Array = Array(manifest.get("seed_structure_kinds", kinds))
	for value in seed_kinds:
		if typeof(value) != TYPE_STRING or String(value).strip_edges() == "":
			sim.configuration_error = "Faction manifest seed_structure_kinds contain a non-string entry"
			return false
		var seed_kind := String(value)
		var found_seed := false
		for kind_value in kinds:
			if String(kind_value) == seed_kind:
				found_seed = true
				break
		if not found_seed:
			sim.configuration_error = "Faction manifest seed kind '%s' is not in structure_kinds" % seed_kind
			return false
	sim._seed_structure_kinds.clear()
	for value in seed_kinds:
		sim._seed_structure_kinds.append(String(value))
	if sim._seed_structure_kinds.is_empty():
		sim._seed_structure_kinds.assign(sim._structure_kinds)
	sim._structure_max_health = (manifest.get("structure_max_health") as Dictionary).duplicate(true)
	sim._structure_bounty_values = (manifest.get("structure_bounty_values", {}) as Dictionary).duplicate(true)
	sim._structure_build_rules = (manifest.get("structure_build_rules") as Dictionary).duplicate(true)
	sim._unit_damage_types = (manifest.get("unit_damage_types") as Dictionary).duplicate(true)
	# Repopulated from the loaded documents' compiled combat blocks; no manifest
	# constant mirrors it, so it must not survive a previous configure.
	sim._unit_damage_components = {}
	# Compiled per-kind structure armor (armor.ini via each structure document).
	# Legacy manifests without it keep the FortressArmor mirror with per-line
	# armor.ini provenance; kinds outside the mirror are recorded provisionals.
	sim._structure_armor = (manifest.get("structure_armor") as Dictionary).duplicate(true)
	sim._spawn_roster = (manifest.get("spawn_roster") as Array).duplicate(true)
	for kind_value in sim._structure_kinds:
		var kind := String(kind_value)
		if int(sim._structure_max_health.get(kind, 0)) <= 0:
			sim.configuration_error = "Faction manifest structure kind '%s' has no positive maximum health" % kind
			return false
		var build_rule: Dictionary = sim._structure_build_rules.get(kind, {}) as Dictionary
		if int(build_rule.get("cost", -1)) < 0 or float(build_rule.get("seconds", 0.0)) <= 0.0:
			sim.configuration_error = "Faction manifest structure kind '%s' has no valid build rule" % kind
			return false
	return true


func _record_structure_armor_provisionals() -> void:
	## Every structure kind must have a compiled armor table. Kinds without one
	## are recorded and logged (loud failure on damage application).
	sim.structure_armor_provisional_kinds.clear()
	for kind_value in sim._structure_kinds:
		var kind := String(kind_value)
		if sim._structure_armor.has(kind):
			continue
		sim.structure_armor_provisional_kinds.append(kind)
		push_error("[RetailSliceSim] structure armor: kind '%s' has no compiled armor contract; structure damage will be refused for that kind" % kind)


func _compiled_armor_table(table_value: Variant) -> Dictionary:
	## Normalize one compiled armor.ini table to fraction scalars.
	if typeof(table_value) != TYPE_DICTIONARY:
		return {}
	var table := table_value as Dictionary
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
	var compiled := {
		"damage_scalar": float((table.get("damageScalar", {}) as Dictionary).get("percent", 100.0)) / 100.0,
		"scalars": scalars,
	}
	## SoldierArmor authors FlankedPenalty = 50% (armor.ini:762). The compiler
	## already emits table.flankedPenalty; dropping it here forced a silent
	## 1.0. Absent stays absent.
	var flanked_value: Variant = table.get("flankedPenalty", table.get("flanked_penalty", null))
	if typeof(flanked_value) == TYPE_DICTIONARY:
		var percent := float((flanked_value as Dictionary).get("percent", 0.0))
		if is_finite(percent) and percent > 0.0:
			compiled["flanked_penalty"] = percent / 100.0
	elif typeof(flanked_value) in [TYPE_FLOAT, TYPE_INT]:
		var raw := float(flanked_value)
		if is_finite(raw) and raw > 0.0:
			compiled["flanked_penalty"] = raw if raw <= 1.0 else raw / 100.0
	return compiled


func _scenario_structure_kind(document: Dictionary) -> String:
	## Lairs intentionally share one MonsterLair table. Other neutral sim.structures
	## do not: CaptureFlag, Outpost, SignalFire, ruins, and towers can carry
	## different ArmorSets despite sharing the admission role `neutral-structure`.
	return StructureArmorContract.scenario_document_kind(document)


func _scenario_structure_armor_projection(document: Dictionary) -> Dictionary:
	return StructureArmorContract.normalize_registration_armor(document)


func _configure_scenario_structure_armor_projection() -> bool:
	var registry_value: Variant = sim._rules.get("scenario_structure_runtimes", {})
	if typeof(registry_value) != TYPE_DICTIONARY:
		sim.configuration_error = "Scenario structure runtime registry is not a dictionary"
		return false
	var registry := registry_value as Dictionary
	var object_ids: Array = registry.keys()
	object_ids.sort_custom(func(a, b): return String(a).naturalnocasecmp_to(String(b)) < 0)
	var contracts_by_kind: Dictionary = {}
	var sources_by_kind: Dictionary = {}
	for object_id_value in object_ids:
		var document_value: Variant = registry.get(object_id_value)
		if typeof(document_value) != TYPE_DICTIONARY:
			continue
		var document := document_value as Dictionary
		var kind := _scenario_structure_kind(document)
		if kind == "":
			continue
		var projection := _scenario_structure_armor_projection(document)
		if projection.has("error"):
			sim.configuration_error = "Scenario structure '%s' armor is invalid: %s" % [String(document.get("objectId", object_id_value)), String(projection.get("error", ""))]
			return false
		var contract: Dictionary = (projection.get("table", {}) as Dictionary).duplicate(true) if bool(projection.get("present", false)) else {}
		if contracts_by_kind.has(kind):
			if (contracts_by_kind.get(kind, {}) as Dictionary) != contract:
				sim.configuration_error = "Scenario structure kind collision '%s': %s and %s carry unequal armor contracts" % [kind, String(sources_by_kind.get(kind, "")), String(document.get("objectId", object_id_value))]
				return false
			continue
		contracts_by_kind[kind] = contract
		sources_by_kind[kind] = String(document.get("objectId", object_id_value))
		if contract.is_empty():
			continue
		if sim._structure_armor.has(kind) and (sim._structure_armor.get(kind, {}) as Dictionary) != contract:
			sim.configuration_error = "Scenario structure kind collision '%s': faction and %s carry unequal armor contracts" % [kind, String(document.get("objectId", object_id_value))]
			return false
		sim._structure_armor[kind] = contract.duplicate(true)
	return true


func _compiled_armor_rule(document: Dictionary) -> Dictionary:
	## Normalize a playableUnit document's compiled armor block. Three honest
	## states: a compiled table, the recorded SAGE passthrough for objects with
	## no authored ArmorSet, and {} for stale packs with no armor block at all
	## (recorded as an exclusion by the caller, passthrough 1.0 at damage time).
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var simulation: Dictionary = registration.get("simulation", {}) as Dictionary
	var resolved: Dictionary = simulation.get("resolved", {}) as Dictionary
	var armor_value: Variant = resolved.get("armor", null)
	if typeof(armor_value) != TYPE_DICTIONARY:
		return {}
	var armor := armor_value as Dictionary
	if armor.get("setId") == null or String(armor.get("setId", "")) == "":
		return {"passthrough": true, "semantic": String(armor.get("semantic", ""))}
	var rule := _compiled_armor_table(armor.get("table", {}))
	if rule.is_empty():
		return {}
	rule["set_id"] = String(armor.get("setId", ""))
	rule["passthrough"] = false
	var upgrades := {}
	for upgrade_value in armor.get("upgrades", []) as Array:
		if typeof(upgrade_value) != TYPE_DICTIONARY:
			continue
		var upgrade := upgrade_value as Dictionary
		var upgrade_id := String(upgrade.get("upgradeId", ""))
		var upgrade_table := _compiled_armor_table(upgrade.get("table", {}))
		if upgrade_id == "" or upgrade_table.is_empty():
			continue
		upgrade_table["set_id"] = String(upgrade.get("setId", ""))
		upgrades[upgrade_id] = upgrade_table
	rule["upgrades"] = upgrades
	return rule


func _parse_damage_scalar(entry: Dictionary) -> Dictionary:
	## Split a compiled DamageScalar row into an evaluable filter. Retail
	## evidence: `ANY +INFANTRY -HERO` (has INFANTRY, not HERO), `ALL -STRUCTURE`
	## (lacks STRUCTURE), `NONE +MINE` (has MINE — the first token never negates
	## the +kinds; see armor_compiler.py and weapon.ini usage).
	var relation := "ANY"
	var plus: Array[String] = []
	var minus: Array[String] = []
	for token in String(entry.get("filter", "")).split(" ", false):
		if token in ["ANY", "ALL", "NONE"]:
			relation = token
		elif token.begins_with("+"):
			plus.append(token.substr(1))
		elif token.begins_with("-"):
			minus.append(token.substr(1))
	return {
		"percent": float(entry.get("percent", 100.0)) / 100.0,
		"filter": String(entry.get("filter", "")),
		"relation": relation,
		"plus": plus,
		"minus": minus,
	}


func _compiled_damage_scalars(rows: Array) -> Array:
	var scalars: Array = []
	for row_value in rows:
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		scalars.append(_parse_damage_scalar(row_value as Dictionary))
	return scalars


func _compiled_weapon_upgrade_rules(document: Dictionary) -> Dictionary:
	## Normalize a document's compiled WeaponSetUpgrade effects keyed by
	## upgrade id. {} when the document carries none.
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var simulation: Dictionary = registration.get("simulation", {}) as Dictionary
	var resolved: Dictionary = simulation.get("resolved", {}) as Dictionary
	var combat: Dictionary = resolved.get("combat", {}) as Dictionary
	var base_damage_type := String(combat.get("damageType", "")).to_lower()
	var effects := {}
	for upgrade_value in combat.get("upgrades", []) as Array:
		if typeof(upgrade_value) != TYPE_DICTIONARY:
			continue
		var upgrade := upgrade_value as Dictionary
		var upgrade_id := String(upgrade.get("upgradeId", ""))
		var kind := String(upgrade.get("kind", ""))
		if upgrade_id == "":
			continue
		var effect := {"kind": kind, "scalars": _compiled_damage_scalars(upgrade.get("damageScalars", []) as Array)}
		match kind:
			"weapon-swap":
				effect["damage"] = float((upgrade.get("damage", {}) as Dictionary).get("value", 0.0))
				effect["damage_type"] = String(upgrade.get("damageType", "")).to_lower()
				effect["weapon_id"] = String(upgrade.get("weaponId", ""))
			"nugget-upgrade":
				var total := 0.0
				var types := {}
				for nugget_value in upgrade.get("nuggets", []) as Array:
					var nugget := nugget_value as Dictionary
					total += float((nugget.get("damage", {}) as Dictionary).get("value", 0.0))
					var nugget_type := String(nugget.get("damageType", "")).to_lower()
					if nugget_type != "":
						types[nugget_type] = true
					effect["scalars"] = (effect["scalars"] as Array) + _compiled_damage_scalars(nugget.get("damageScalars", []) as Array)
				effect["damage"] = total
				effect["damage_type"] = types.keys()[0] if types.size() == 1 else ""
				effect["weapon_id"] = String(upgrade.get("weaponId", ""))
			"warhead-upgrade":
				# The primary nugget keeps the base damage type (fire arrows keep
				# their pierce); every other nugget is an authored bonus hit.
				var bonus: Array = []
				for nugget_value in upgrade.get("nuggets", []) as Array:
					var nugget := nugget_value as Dictionary
					var nugget_type := String(nugget.get("damageType", "")).to_lower()
					var row := {
						"damage": float((nugget.get("damage", {}) as Dictionary).get("value", 0.0)),
						"damage_type": nugget_type,
						"scalars": _compiled_damage_scalars(nugget.get("damageScalars", []) as Array),
					}
					if nugget_type == base_damage_type and not effect.has("damage"):
						effect["damage"] = row["damage"]
						effect["damage_type"] = nugget_type
						effect["scalars"] = row["scalars"]
					else:
						bonus.append(row)
				effect["bonus_nuggets"] = bonus
				effect["warhead_id"] = String(upgrade.get("warheadId", ""))
			"production-legality":
				# Compiler marker: WeaponSetUpgrade gates production legality
				# without a damage delta. Eligible for the purchase surface;
				# combat resolution treats it as a no-op effect.
				effect["semantic"] = String(upgrade.get("semantic", ""))
			_:
				continue
		effects[upgrade_id] = effect
	return effects


func _compiled_level_up_rules(document: Dictionary) -> Dictionary:
	## Normalize a document's compiled LevelUpUpgrade modules (Basic Training)
	## keyed by upgrade id. {} when the document carries none.
	var gameplay: Dictionary = (document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary
	var rules := {}
	for row_value in gameplay.get("levelUpgrades", []) as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := row_value as Dictionary
		var upgrade_id := String(row.get("upgradeId", ""))
		if upgrade_id == "":
			continue
		rules[upgrade_id] = {
			"levels_to_gain": int(row.get("levelsToGain", 1)),
			"level_cap": int(row.get("levelCap", 2)),
		}
	return rules


func _compiled_banner_carrier(document: Dictionary) -> Dictionary:
	## Normalize the compiler's BannerCarriersAllowed contract. A malformed
	## contract refuses registration; it never creates a placeholder flag.
	var gameplay := ((document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary)
	var source: Variant = gameplay.get("bannerCarrier")
	if typeof(source) != TYPE_DICTIONARY:
		return {}
	var contract := source as Dictionary
	var allowed_value: Variant = contract.get("allowedObjectIds", [])
	var positions_value: Variant = contract.get("positions", [])
	var min_level_value: Variant = contract.get("minLevel")
	if (
		typeof(allowed_value) != TYPE_ARRAY
		or typeof(positions_value) != TYPE_ARRAY
		or typeof(min_level_value) not in [TYPE_INT, TYPE_FLOAT]
		or int(min_level_value) < 0
		or not is_equal_approx(float(min_level_value), float(int(min_level_value)))
	):
		return {}
	var allowed: Array = allowed_value as Array
	var positions: Array = positions_value as Array
	if allowed.is_empty():
		return {}
	for value in allowed:
		if String(value) == "":
			return {}
	var banner_document: Dictionary = {}
	var db = sim._content_db_ref()
	if db != null and db.has_method("get_playable_unit_runtime"):
		banner_document = db.get_playable_unit_runtime(String(allowed[0]))
	if banner_document.is_empty():
		push_error("RetailSliceSim banner target '%s' has no converted playable-unit template" % String(allowed[0]))
		return {}
	var banner_simulation := ((banner_document.get("registration", {}) as Dictionary).get("simulation", {}) as Dictionary)
	var resolved := banner_simulation.get("resolved", {}) as Dictionary
	var member_health_value: Variant = resolved.get("memberHealth")
	var banner_max_health := 0
	if typeof(member_health_value) == TYPE_DICTIONARY:
		banner_max_health = int((member_health_value as Dictionary).get("value", 0))
	elif typeof(member_health_value) in [TYPE_INT, TYPE_FLOAT]:
		banner_max_health = int(member_health_value)
	if banner_max_health <= 0:
		push_error("RetailSliceSim banner target '%s' has no authored member health" % String(allowed[0]))
		return {}
	var respawn_ticks := _compiled_banner_respawn_ticks(banner_document)
	var offset := Vector2.ZERO
	if not positions.is_empty():
		if typeof(positions[0]) != TYPE_DICTIONARY:
			return {}
		var position: Dictionary = positions[0] as Dictionary
		if typeof(position.get("x")) not in [TYPE_INT, TYPE_FLOAT] or typeof(position.get("y")) not in [TYPE_INT, TYPE_FLOAT]:
			return {}
		offset = Vector2(float(position["x"]), float(position["y"]))
	return {
		"object_id": sim.PlayableUnitAdapter.runtime_object_id(String(allowed[0])),
		"source_banner_object_id": String(allowed[0]),
		"min_level": int(min_level_value),
		"offset_source": offset,
		"destroy_horde_on_death": bool(contract.get("destroyHordeOnBannerDeath", false)),
		"banner_max_health": banner_max_health,
		"respawn_ticks": respawn_ticks,
	}


func _compiled_banner_respawn_ticks(document: Dictionary) -> int:
	## BannerCarrierUpdate DiedRespawnTime / MeleeFreeBannerReSpawnTime (ms).
	## Returns -1 when retail authored no death respawn timer.
	var gameplay := ((document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary)
	var update: Variant = gameplay.get("bannerCarrierUpdate")
	if typeof(update) != TYPE_DICTIONARY:
		return -1
	var contract := update as Dictionary
	var died: Variant = contract.get("diedRespawnTime")
	if typeof(died) != TYPE_DICTIONARY:
		return -1
	var died_ms := int((died as Dictionary).get("milliseconds", -1))
	if died_ms < 0:
		return -1
	var melee_ms := 0
	var melee: Variant = contract.get("meleeFreeBannerRespawnTime")
	if typeof(melee) == TYPE_DICTIONARY:
		melee_ms = maxi(0, int((melee as Dictionary).get("milliseconds", 0)))
	var delay_ms := maxi(died_ms, melee_ms)
	# Match C# PackTemplateLoader: ceil ms -> ticks at sim.TICK_SECONDS.
	return maxi(0, int(ceili(float(delay_ms) / (sim.TICK_SECONDS * 1000.0))))


func _compiled_castle_behavior(document: Dictionary) -> Dictionary:
	## Fail-closed copy of pack gameplay.castleBehavior (BSE pieces).
	var gameplay := ((document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary)
	var source: Variant = gameplay.get("castleBehavior")
	if typeof(source) != TYPE_DICTIONARY:
		return {}
	var contract := source as Dictionary
	var pieces_value: Variant = contract.get("pieces")
	if typeof(pieces_value) != TYPE_ARRAY:
		return {}
	var pieces := pieces_value as Array
	if pieces.is_empty() or pieces.size() > 64:
		return {}
	var normalized: Array = []
	for index in pieces.size():
		var row_value: Variant = pieces[index]
		if typeof(row_value) != TYPE_DICTIONARY:
			return {}
		var row := row_value as Dictionary
		if int(row.get("index", -1)) != index:
			return {}
		var object_id := String(row.get("objectId", ""))
		var offset_value: Variant = row.get("offset")
		if object_id == "" or typeof(offset_value) != TYPE_ARRAY or (offset_value as Array).size() != 3:
			return {}
		var offset_arr := offset_value as Array
		if (
			typeof(offset_arr[0]) not in [TYPE_INT, TYPE_FLOAT]
			or typeof(offset_arr[1]) not in [TYPE_INT, TYPE_FLOAT]
			or typeof(offset_arr[2]) not in [TYPE_INT, TYPE_FLOAT]
		):
			return {}
		var angle := float(row.get("angleRadians", 0.0))
		if typeof(row.get("angleRadians")) not in [TYPE_INT, TYPE_FLOAT] or not is_finite(angle):
			return {}
		var target_document: Dictionary = {}
		var db = sim._content_db_ref()
		if db != null and db.has_method("get_playable_structure_runtime"):
			target_document = db.get_playable_structure_runtime(object_id)
		if target_document.is_empty():
			push_error("RetailSliceSim CastleBehavior target '%s' has no converted structure template" % object_id)
			return {}
		var target_gameplay := ((target_document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary)
		var target_health := int((((target_gameplay.get("health", {}) as Dictionary).get("primary", {}) as Dictionary).get("maxHealth", {}) as Dictionary).get("value", 0))
		if target_health <= 0:
			push_error("RetailSliceSim CastleBehavior target '%s' has no authored maximum health" % object_id)
			return {}
		normalized.append({
			"index": index,
			"source_object_id": object_id,
			"object_id": sim.PlayableUnitAdapter._runtime_id(object_id),
			"offset_source": Vector2(float(offset_arr[0]), float(offset_arr[1])),
			"elevation_source": float(offset_arr[2]),
			"angle_radians": angle,
			"maximum_health": target_health,
			"priority": int(row.get("priority", 0)),
			"phase": int(row.get("phase", 0)),
		})
	return {
		"faction": String(contract.get("faction", "")),
		"castle_template_token": String(contract.get("castleTemplateToken", "")),
		"pieces": normalized,
	}


func configure_castle_behaviors(by_source_object_id: Dictionary) -> void:
	## Vertical-slice wiring: source structure object id -> castleBehavior contract.
	sim._castle_behavior_by_source = by_source_object_id.duplicate(true)
	# setup() has already seeded legacy expansion slots on first boot. Replace
	# them with the selected pack's BSE contract before the first gameplay tick.
	for structure_id in sim.structure_ids():
		var row: Dictionary = sim.structures[structure_id]
		if sim._castle_behavior_for_structure(row).is_empty():
			continue
		if String(row.get("structure_kind", "")) == "fortress":
			sim.expansion_pads.erase(structure_id)
		sim._unpack_castle_behavior_for_structure(structure_id)
		if String(row.get("structure_kind", "")) == "fortress":
			sim._seed_expansion_pads_for(structure_id)


func _retail_source_to_sim_offset(offset_source: Vector2) -> Vector2:
	## Banner formation offsets and BSE piece XY are retail source units.
	## Prefer the map transform scale; fall back to 0.1 (common SAGE->local).
	var scale = float(sim._rules.get("source_map_transform_scale", 0.0))
	if scale <= 0.0:
		scale = float(sim._rules.get("source_unit_scale", 0.1))
	if scale <= 0.0:
		scale = 0.1
	return offset_source * scale


func _compiled_unit_upgrade_commands(document: Dictionary) -> Array:
	## Normalize the horde's compiled OBJECT_UPGRADE purchase rows. Rows with a
	## malformed identity fail the document to an empty surface; the caller
	## cross-checks every row against the compiled effect tables.
	var gameplay: Dictionary = (document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary
	var rows: Array = []
	for row_value in gameplay.get("upgradeCommands", []) as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := row_value as Dictionary
		var needed: Array = []
		for needed_value in Array(row.get("neededUpgradeIds", [])):
			needed.append(String(needed_value))
		rows.append({
			"upgrade_id": String(row.get("upgradeId", "")),
			"command_id": String(row.get("commandId", "")),
			"command_set_id": String(row.get("commandSetId", "")),
			"slot": int(row.get("slot", 0)),
			"cost": maxi(0, int(row.get("cost", 0))),
			"duration_ticks": maxi(1, roundi(float(row.get("buildTimeSeconds", 0.0)) / sim.TICK_SECONDS)),
			"cancelable": bool(row.get("cancelable", false)),
			"multi_select": bool(row.get("multiSelect", false)),
			"needed_upgrade_ids": needed,
			"needed_upgrade_any": bool(row.get("neededUpgradeAny", false)),
			"label_id": String(row.get("labelId", "")),
			"tooltip_id": String(row.get("tooltipId", "")),
			"image_id": String(row.get("buttonImageId", "")),
			"lacks_prerequisite_label_id": String(row.get("lacksPrerequisiteLabelId", "")),
		})
	return rows


func _filter_kind_matches(kind: String, target: Dictionary, target_kind: String) -> bool:
	var folded := kind.to_lower()
	if folded == "structure":
		return target_kind == "structure"
	if target_kind == "structure":
		# Object-id kinds (+EntMoot, +MordorCatapult) match sim.structures by id.
		return folded == String(target.get("source_object_id", target.get("object_id", ""))).to_lower()
	match folded:
		"infantry":
			return String(target.get("category", "")) in ["infantry", "ranged-infantry"]
		"hero":
			return String(target.get("category", "")) == "hero"
		"cavalry":
			return String(target.get("category", "")) == "cavalry"
		"monster":
			return String(target.get("category", "")) == "monster"
		"machine", "siegeengine", "siege_weapon":
			return String(target.get("category", "")) == "siege"
		"mine", "summoned":
			# The slice fields no MINE/SUMMONED objects; recorded, never matched.
			return false
	return folded == String(target.get("object_id", "")).to_lower()


func _damage_scalar_factor(scalars: Array, target: Dictionary, target_kind: String) -> float:
	## First matching authored DamageScalar wins (retail authors them mutually
	## exclusive: 200% vs infantry, 150% vs heroes on GondorSwordUpgraded).
	for scalar_value in scalars:
		var scalar: Dictionary = scalar_value
		var plus: Array = scalar.get("plus", [])
		var plus_ok := true
		if not plus.is_empty():
			if String(scalar.get("relation", "ANY")) == "ALL":
				for kind_value in plus:
					if not _filter_kind_matches(String(kind_value), target, target_kind):
						plus_ok = false
						break
			else:
				plus_ok = false
				for kind_value in plus:
					if _filter_kind_matches(String(kind_value), target, target_kind):
						plus_ok = true
						break
		if not plus_ok:
			continue
		var minus_ok := true
		for kind_value in scalar.get("minus", []) as Array:
			if _filter_kind_matches(String(kind_value), target, target_kind):
				minus_ok = false
				break
		if minus_ok:
			return float(scalar.get("percent", 1.0))
	return 1.0


func _compiled_damage_components(combat: Dictionary, source_scale: float = 1.0) -> Array:
	## Normalize the compiler's damageComponents rows. [] when the weapon has a
	## single authored damageType (the ordinary path) or authors none at all.
	var rows: Array = []
	for row_value in combat.get("damageComponents", []) as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := row_value as Dictionary
		var value := float(row.get("value", 0.0))
		if value <= 0.0:
			continue
		var normalized := {
			"damage_type": String(row.get("damageType", "")).to_lower(),
			"value": value,
		}
		if row.has("radius"):
			normalized["radius"] = maxf(0.0, float(row.get("radius", 0.0)) * source_scale)
		if row.has("damageTaperOff"):
			normalized["damage_taper_off"] = clampf(float(row.get("damageTaperOff", 0.0)), 0.0, 100.0)
		if row.has("deathType"):
			normalized["death_type"] = String(row.get("deathType", "NORMAL"))
		if row.has("damageFXType"):
			normalized["damage_fx_type"] = String(row.get("damageFXType", ""))
		rows.append(normalized)
	return rows


func _damage_components_for(attacker_id: int, damage_type_override: String) -> Array:
	## The attacker's authored multi-type damage mix. An explicit override
	## (an ability or projectile that names its own type) replaces the mix
	## outright rather than blending into it.
	if damage_type_override != "" or not sim.entities.has(attacker_id):
		return []
	return (sim.entities[attacker_id] as Dictionary).get("damage_components", []) as Array


func _weighted_armor_scalar(scalars: Dictionary, components: Array, damage_type: String) -> float:
	## Damage-weighted mean of each component's own armor column. Because the
	## armor scalar is a linear multiplier, sum(dmg_i * scalar_i) equals
	## total_damage * this mean -- so one hit reproduces retail's per-nugget
	## resolution exactly without splitting into several hits.
	##
	## For Arwen (HERO 180 + SLASH 20) into RivendellLancerArmor (HERO 200%,
	## SLASH 40%) that is (180*2.0 + 20*0.4) / 200 = 1.84, i.e. 368 of 200 raw
	## -- the retail intent, where the untyped lump previously scored 200.
	var total := 0.0
	var weighted := 0.0
	for row_value in components:
		var row := row_value as Dictionary
		var value := float(row.get("value", 0.0))
		if value <= 0.0:
			continue
		total += value
		# An untyped component keeps falling to DEFAULT: never invent a type
		# weapon.ini does not author.
		var key := String(row.get("damage_type", ""))
		weighted += value * float(scalars.get(key, scalars.get("default", 1.0)))
	if total <= 0.0:
		return float(scalars.get(damage_type.to_lower(), scalars.get("default", 1.0)))
	return weighted / total


func _member_armor_scalar(target: Dictionary, damage_type: String, components: Array = []) -> float:
	## Compiled armor.ini scalar: attacker damage type vs the victim's set
	## (its recorded applied armor upgrade swaps the set, last applied wins).
	var rule: Dictionary = sim._unit_armor.get(String(target.get("object_id", "")), {})
	if rule.is_empty() or bool(rule.get("passthrough", false)):
		return 1.0
	var table := rule
	var active := String(target.get("active_armor_upgrade", ""))
	if active != "":
		var upgrades: Dictionary = rule.get("upgrades", {})
		if (target.get("applied_upgrades", {}) as Dictionary).has(active) and upgrades.has(active):
			table = upgrades[active]
	var scalars: Dictionary = table.get("scalars", {})
	if not components.is_empty():
		return float(table.get("damage_scalar", 1.0)) * _weighted_armor_scalar(scalars, components, damage_type)
	var key := damage_type.to_lower()
	return float(table.get("damage_scalar", 1.0)) * float(scalars.get(key, scalars.get("default", 1.0)))


func _applied_weapon_effect(row: Dictionary) -> Dictionary:
	## The compiled WeaponSetUpgrade effect this horde has recorded as applied.
	var effects: Dictionary = sim._unit_weapon_upgrades.get(String(row.get("object_id", "")), {})
	if effects.is_empty():
		return {}
	var applied: Dictionary = row.get("applied_upgrades", {})
	var upgrade_ids: Array = effects.keys()
	upgrade_ids.sort()
	for upgrade_id_value in upgrade_ids:
		if applied.has(String(upgrade_id_value)):
			return effects[upgrade_id_value]
	return {}


func _configure_manifest_builders() -> void:
	## A data-driven faction names its builder via the structure documents'
	## authored construct routes; the flag rides the normalized unit rule.
	if sim.configuration_error != "":
		return
	var manifest: Dictionary = sim._rules.get("faction_manifest", {}) as Dictionary
	var configured_unit_rules: Dictionary = sim._rules.get("unit_rules", {}) as Dictionary
	for builder_value in manifest.get("builder_unit_ids", []) as Array:
		var builder_id := String(builder_value)
		if not configured_unit_rules.has(builder_id):
			sim.configuration_error = "Faction manifest builder unit '%s' has no configured unit rule" % builder_id
			return
		var builder_rule: Dictionary = configured_unit_rules[builder_id] as Dictionary
		builder_rule["is_builder"] = true
		configured_unit_rules[builder_id] = builder_rule
	sim._rules["unit_rules"] = configured_unit_rules


func _validate_faction_manifest_coherence() -> void:
	## Only an explicitly supplied manifest is validated end-to-end; legacy
	## rule dictionaries keep their historical permissiveness.
	if sim.configuration_error != "" or not sim._rules.has("faction_manifest"):
		return
	var manifest: Dictionary = sim._rules.get("faction_manifest", {}) as Dictionary
	if manifest.is_empty():
		return
	for unit_type_value in sim._ai_production_plan:
		var unit_type := String(unit_type_value)
		if not sim._unit_production_rules.has(unit_type):
			sim.configuration_error = "Faction manifest AI plan entry '%s' has no production rule" % unit_type
			return
	var configured_unit_rules: Dictionary = sim._rules.get("unit_rules", {}) as Dictionary
	for entry_value in sim._spawn_roster:
		if typeof(entry_value) != TYPE_DICTIONARY:
			sim.configuration_error = "Faction manifest spawn roster contains a non-object entry"
			return
		var entry := entry_value as Dictionary
		var object_id := String(entry.get("object_id", ""))
		if object_id == "" or int(entry.get("id", 0)) <= 0:
			sim.configuration_error = "Faction manifest spawn roster entry is missing its identity"
			return
		if not bool(entry.get("requires_unit_rule", false)) and not configured_unit_rules.has(object_id):
			sim.configuration_error = "Faction manifest spawn roster entry '%s' has no configured unit rule" % object_id
			return


func _compiled_ring_gollum_rule(registration: Dictionary) -> Dictionary:
	var objects_value: Variant = registration.get("objects", {})
	var system_value: Variant = registration.get("system", {})
	if typeof(objects_value) != TYPE_DICTIONARY or typeof(system_value) != TYPE_DICTIONARY:
		return {}
	var objects := objects_value as Dictionary
	var spawn_value: Variant = (system_value as Dictionary).get("spawn", {})
	if typeof(spawn_value) != TYPE_DICTIONARY:
		return {}
	var object_id := String((spawn_value as Dictionary).get("objectId", ""))
	var child_value: Variant = objects.get(object_id, {})
	if object_id == "" or typeof(child_value) != TYPE_DICTIONARY:
		return {}
	var child := child_value as Dictionary
	var parent_id := String(child.get("parentObjectId", ""))
	var parent_value: Variant = objects.get(parent_id, {})
	if parent_id == "" or typeof(parent_value) != TYPE_DICTIONARY:
		return {}
	var parent := parent_value as Dictionary
	var locomotors_value: Variant = parent.get("locomotors", {})
	var body_value: Variant = parent.get("body", {})
	if typeof(locomotors_value) != TYPE_DICTIONARY or typeof(body_value) != TYPE_DICTIONARY:
		return {}
	var speed_source := float((locomotors_value as Dictionary).get("normal", 0.0))
	var member_health := int((body_value as Dictionary).get("maxHealth", 0))
	if speed_source <= 0.0 or member_health <= 0:
		return {}
	# NeutralGollum binds HumanLocomotor for SET_NORMAL (neutralunits.ini:324),
	# which authors Acceleration/Braking 510 and TurnTime 500 -> 720 deg/s
	# (locomotor.ini:142). The importer now emits those on the ring descriptor
	# as `movement`; a pack cooked before that binding is a NAMED gap, not a
	# licence to reuse the walk speed as an acceleration.
	var movement_value: Variant = parent.get("movement", {})
	var movement: Dictionary = (movement_value as Dictionary) if typeof(movement_value) == TYPE_DICTIONARY else {}
	for authored_field in ["acceleration", "braking", "turnRateDegreesPerSecond"]:
		if not movement.has(authored_field):
			push_error(
				"unauthored locomotor field %s for %s (ring system descriptor predates the locomotor binding; recook the pack)"
				% [authored_field, object_id]
			)
			return {}
	var source_scale = maxf(0.000001, float(sim._rules.get("source_map_transform_scale", 1.0)))
	var speed = speed_source * source_scale
	var vision_source := float((parent.get("camouflage", {}) as Dictionary).get("detectionRange", 0.0))
	return {
		"source_object_id": object_id,
		"horde_id": object_id,
		"category": "hero" if (parent.get("kindOf", []) as Array).has("HERO") else "infantry",
		"kind_of": (parent.get("kindOf", []) as Array).duplicate(),
		"member_count": 1,
		"member_health": member_health,
		"member_damage": 0,
		"noncombatant": true,
		"speed": speed,
		"speed_source": speed_source,
		"acceleration": float(movement["acceleration"]) * source_scale,
		"acceleration_source": float(movement["acceleration"]),
		"turn_rate_degrees_per_second": float(movement["turnRateDegreesPerSecond"]),
		"braking": float(movement["braking"]) * source_scale,
		"braking_source": float(movement["braking"]),
		"attack_range": 0.0,
		"attack_range_source": 0.0,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": vision_source * source_scale,
		"vision_range_source": vision_source,
		"delay_between_shots_ms": 1000.0,
		"pre_attack_delay_ms": 0.0,
		"firing_duration_ms": 0.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 0,
		"firing_duration_ticks": 0,
		"formation_positions": [Vector3.ZERO],
		"provenance": {"source": "ring.system.registration.objects", "parentObjectId": parent_id},
	}


func _configure_playable_unit_runtime_contracts() -> void:
	var value: Variant = sim._rules.get("playable_unit_runtimes", {})
	if typeof(value) != TYPE_DICTIONARY:
		sim.configuration_error = "Playable-unit runtime registry is not a dictionary"
		return
	var configured_unit_rules: Dictionary = sim._rules.get("unit_rules", {}) as Dictionary
	if sim.ring_mechanic_enabled:
		var ring_registration_value: Variant = sim._ring_contract.get("_compiledRegistration", {})
		if typeof(ring_registration_value) == TYPE_DICTIONARY and not (ring_registration_value as Dictionary).is_empty():
			var compiled_gollum_rule := _compiled_ring_gollum_rule(ring_registration_value as Dictionary)
			if compiled_gollum_rule.is_empty():
				sim.configuration_error = "Compiled ring-system Gollum has no usable simulation rule"
				return
			configured_unit_rules[String(compiled_gollum_rule.get("source_object_id", ""))] = compiled_gollum_rule
	var producer_kinds: Dictionary = sim._rules.get("producer_kind_by_source_object", {}) as Dictionary
	sim._unit_armor.clear()
	sim._unit_weapon_upgrades.clear()
	sim._created_hero_owner_teams.clear()
	sim.missing_armor_units.clear()
	sim.missing_damage_type_units.clear()
	var object_ids: Array[String] = []
	for object_id_value in (value as Dictionary).keys():
		object_ids.append(String(object_id_value))
	object_ids.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	for object_id in object_ids:
		var document_value: Variant = (value as Dictionary).get(object_id)
		if typeof(document_value) != TYPE_DICTIONARY:
			sim.configuration_error = "Playable-unit runtime '%s' is invalid" % object_id
			return
		var ring_gollum_block: Variant = ((document_value as Dictionary).get("ringMechanic", {}) as Dictionary).get("gollum", {})
		if sim.ring_mechanic_enabled and typeof(ring_gollum_block) == TYPE_DICTIONARY \
				and not (ring_gollum_block as Dictionary).is_empty():
			sim._ring_contract.merge((ring_gollum_block as Dictionary), false)
			var gollum_simulation = sim.PlayableUnitAdapter.simulation_rule(document_value as Dictionary, false)
			var gollum_rule = sim.PlayableUnitAdapter.normalized_unit_rule(
				gollum_simulation, float(sim._rules.get("source_map_transform_scale", 0.0))
			)
			if gollum_rule.is_empty():
				sim.configuration_error = "Ring Gollum runtime '%s' has no normalized unit rule" % object_id
				return
			var source_gollum_id := String((document_value as Dictionary).get("objectId", ""))
			var member_gollum_id = sim.PlayableUnitAdapter.runtime_member_id(document_value as Dictionary)
			configured_unit_rules[source_gollum_id] = gollum_rule
			configured_unit_rules[member_gollum_id] = gollum_rule
			if not sim._ring_contract.has("gollumObjectId"):
				sim._ring_contract["gollumObjectId"] = source_gollum_id
			continue
		# Compiled armor.ini contract + forge upgrade effects ride every fresh
		# document; a stale pack without the block is a recorded exclusion with
		# the SAGE passthrough, never an invented multiplier. One canonical,
		# document-derived id space: every rule registers under BOTH ids the
		# document itself resolves (runtime_member_id = primaryMemberObjectId,
		# runtime_unit_id = containerObjectId), so entity object_id lookups,
		# unit_type-keyed purchase surfaces, and gate checks read one space —
		# no hardcoded alias tables.
		var armor_member_id = sim.PlayableUnitAdapter.runtime_member_id(document_value as Dictionary)
		var armor_unit_id = sim.PlayableUnitAdapter.runtime_unit_id(document_value as Dictionary)
		var armor_id_space: Array[String] = [armor_member_id]
		if armor_unit_id != "" and armor_unit_id != armor_member_id:
			armor_id_space.append(armor_unit_id)
		var armor_rule := _compiled_armor_rule(document_value as Dictionary)
		if armor_rule.is_empty():
			if not sim.missing_armor_units.has(armor_member_id):
				sim.missing_armor_units.append(armor_member_id)
				print("[RetailSliceSim] unit '%s' has no compiled armor block (stale pack); incoming damage uses the recorded SAGE passthrough 1.0" % armor_member_id)
		elif not bool(armor_rule.get("passthrough", false)):
			for armor_id in armor_id_space:
				sim._unit_armor[armor_id] = armor_rule
		var weapon_upgrade_rules := _compiled_weapon_upgrade_rules(document_value as Dictionary)
		if not weapon_upgrade_rules.is_empty():
			for armor_id in armor_id_space:
				sim._unit_weapon_upgrades[armor_id] = weapon_upgrade_rules
		# Authored LevelUpUpgrade effects (Basic Training) and the horde's own
		# OBJECT_UPGRADE purchase buttons ride the same document; eligibility is
		# the compiled effect tables above — never a class-name guess.
		var level_up_rules := _compiled_level_up_rules(document_value as Dictionary)
		if not level_up_rules.is_empty():
			for armor_id in armor_id_space:
				sim._unit_level_upgrades[armor_id] = level_up_rules
		var banner_rule := _compiled_banner_carrier(document_value as Dictionary)
		var document_gameplay := (((document_value as Dictionary).get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary)
		if typeof(document_gameplay.get("bannerCarrier")) == TYPE_DICTIONARY and banner_rule.is_empty():
			sim.configuration_error = "Playable-unit runtime '%s' has an unresolved banner carrier target" % object_id
			return
		if not banner_rule.is_empty():
			for armor_id in armor_id_space:
				sim._unit_banner_carriers[armor_id] = banner_rule
		# Banner unit documents carry BannerCarrierUpdate respawn timers.
		var respawn_ticks := _compiled_banner_respawn_ticks(document_value as Dictionary)
		if respawn_ticks >= 0:
			var banner_runtime_id = sim.PlayableUnitAdapter.runtime_unit_id(document_value as Dictionary)
			var banner_member_id = sim.PlayableUnitAdapter.runtime_member_id(document_value as Dictionary)
			if banner_runtime_id != "":
				sim._banner_respawn_ticks_by_object[banner_runtime_id] = respawn_ticks
			if banner_member_id != "" and banner_member_id != banner_runtime_id:
				sim._banner_respawn_ticks_by_object[banner_member_id] = respawn_ticks
			# Also index the retail source name and adapter slug used by horde contracts.
			var source_name := String((document_value as Dictionary).get("objectId", ""))
			if source_name != "":
				sim._banner_respawn_ticks_by_object[sim.PlayableUnitAdapter.runtime_object_id(source_name)] = respawn_ticks
				sim._banner_respawn_ticks_by_object[source_name] = respawn_ticks
		var purchase_rows := _compiled_unit_upgrade_commands(document_value as Dictionary)
		if purchase_rows.is_empty():
			if not sim.units_without_upgrade_commands.has(armor_member_id):
				sim.units_without_upgrade_commands.append(armor_member_id)
		else:
			# Keep the unit fieldable when a purchase row has no compiled effect
			# yet (thrall composition swaps; forged-blade WeaponSetUpgrade not
			# emitted into combat.upgrades). Fail-closed used to refuse the
			# entire playable_unit_runtimes registry — one incomplete purchase
			# killed match configure for the whole faction. Surface only the
			# rows that already have weapon/armor/level effects; record gaps.
			var armor_upgrade_ids: Dictionary = (armor_rule.get("upgrades", {}) as Dictionary)
			var weapon_upgrade_ids: Dictionary = sim._unit_weapon_upgrades.get(armor_member_id, {}) as Dictionary
			var level_upgrade_ids: Dictionary = sim._unit_level_upgrades.get(armor_member_id, {}) as Dictionary
			var accepted_purchase_rows: Array = []
			for row_value in purchase_rows:
				var purchase := row_value as Dictionary
				var purchase_id := String(purchase.get("upgrade_id", ""))
				if purchase_id == "":
					continue
				if (
					weapon_upgrade_ids.has(purchase_id)
					or armor_upgrade_ids.has(purchase_id)
					or level_upgrade_ids.has(purchase_id)
				):
					accepted_purchase_rows.append(purchase)
				else:
					print(
						"[RetailSliceSim] unit '%s' purchase '%s' has no compiled weapon/armor/level effect; deferred from purchase surface"
						% [object_id, purchase_id]
					)
			var purchase_unit_type = String(sim.PlayableUnitAdapter.simulation_rule(document_value as Dictionary).get("unit_type", ""))
			if purchase_unit_type != "" and not accepted_purchase_rows.is_empty():
				sim._unit_upgrade_commands[purchase_unit_type] = accepted_purchase_rows
			elif purchase_unit_type != "" and accepted_purchase_rows.is_empty() and not purchase_rows.is_empty():
				if not sim.units_without_upgrade_commands.has(armor_member_id):
					sim.units_without_upgrade_commands.append(armor_member_id)
		var simulation = sim.PlayableUnitAdapter.simulation_rule(document_value as Dictionary)
		if simulation.is_empty():
			# The faction builder legitimately lacks combat evidence and costs
			# zero command points; it registers through the narrower builder
			# production projection instead of failing the whole roster.
			if _register_builder_production(document_value as Dictionary, configured_unit_rules, producer_kinds):
				continue
			sim.configuration_error = "Playable-unit runtime '%s' has unresolved simulation evidence" % object_id
			return
		var unit_type := String(simulation["unit_type"])
		var member_id := String(simulation["object_id"])
		if (
			sim._unit_production_rules.has(unit_type)
			and String((sim._unit_production_rules[unit_type] as Dictionary).get("object_id", "")) != member_id
		):
			sim.configuration_error = "Playable-unit runtime '%s' collides with production '%s'" % [object_id, unit_type]
			return
		var unit_rule = sim.PlayableUnitAdapter.normalized_unit_rule(simulation, float(sim._rules.get("source_map_transform_scale", 0.0)))
		if unit_rule.is_empty():
			unit_rule = (configured_unit_rules.get(member_id, {}) as Dictionary).duplicate(true)
		if unit_rule.is_empty():
			sim.configuration_error = "Playable-unit runtime '%s' has no normalized unit rule" % object_id
			return
		# The compiled selection surface carries the unit's authored base
		# CommandSet on every command row. Record it only when the document agrees
		# on one identity; mixed surfaces remain unresolved rather than choosing a
		# convenient first row. MonitorConditionUpdate uses this exact value to
		# restore the base palette after its condition clears.
		var authored_command_sets: Array[String] = []
		for selection_value in sim.PlayableUnitAdapter.selection_commands(document_value as Dictionary):
			var command_set_id := String((selection_value as Dictionary).get("commandSetId", "")).strip_edges()
			if command_set_id != "" and not authored_command_sets.has(command_set_id): authored_command_sets.append(command_set_id)
		if authored_command_sets.size() == 1:
			unit_rule["default_command_set_id"] = authored_command_sets[0]
		var authored_formation_toggle = sim._authored_formation_toggle(document_value as Dictionary)
		if not authored_formation_toggle.is_empty():
			unit_rule["formation_toggle"] = authored_formation_toggle
		var producers: Array = simulation.get("producers", [])
		if producers.is_empty():
			sim.configuration_error = "Playable-unit runtime '%s' has no producer" % object_id
			return
		var resolved_producers: Array[Dictionary] = []
		var resolved_producer_kinds: Array[String] = []
		for producer_value in producers:
			var producer := producer_value as Dictionary
			var source_producer := String(producer.get("producer_source_object_id", ""))
			var producer_kind := String(producer_kinds.get(source_producer, ""))
			if producer_kind == "":
				sim.configuration_error = "Playable-unit runtime '%s' producer '%s' is not loaded by this faction slice" % [object_id, source_producer]
				return
			var route := producer.duplicate(true)
			route["producer_kind"] = producer_kind
			resolved_producers.append(route)
			if not resolved_producer_kinds.has(producer_kind):
				resolved_producer_kinds.append(producer_kind)
		var primary_producer := resolved_producers[0]
		unit_rule["category"] = String(simulation.get("category", unit_rule.get("category", "")))
		var horde_override: Dictionary = (sim._rules.get("horde_speed_overrides", {}) as Dictionary).get(member_id, {}) as Dictionary
		if not horde_override.is_empty() and int(unit_rule.get("member_count", 0)) > 1:
			# Retail hordes move at their horde LocomotorSet speed (the pack's
			# retail unit rules); the document speed is the unit-object
			# locomotor. Both stay recorded; the horde value drives movement.
			var scale = float(sim._rules.get("source_map_transform_scale", 0.0))
			unit_rule["speed"] = float(horde_override.get("speed", 0.0)) * scale
			unit_rule["speed_source"] = float(horde_override.get("speed", 0.0))
			var override_provenance: Dictionary = (unit_rule.get("provenance", {}) as Dictionary).duplicate(true)
			override_provenance["horde_locomotor"] = {
				"speed": float(horde_override.get("speed", 0.0)),
				"source": (horde_override.get("source", {}) as Dictionary).duplicate(true),
				"unit_object_speed": float(horde_override.get("unit_object_speed", 0.0)),
				"note": "horde movement uses the horde LocomotorSet; the document speed is the unit-object locomotor",
			}
			unit_rule["provenance"] = override_provenance
		configured_unit_rules[member_id] = unit_rule
		var doc_combat: Dictionary = simulation.get("combat", {}) as Dictionary
		var doc_damage_type := String(doc_combat.get("damageType", "")).to_lower()
		var doc_damage_components := _compiled_damage_components(
			doc_combat, float(sim._rules.get("source_map_transform_scale", 1.0))
		)
		if not doc_damage_components.is_empty():
			# A multi-nugget weapon whose nuggets author different types
			# (ArwenSword: HERO ARWEN_DAMAGE + SLASH 20) carries no single
			# authored damageType. Each component keeps its own type and is
			# weighted against its own armor column instead of collapsing the
			# whole hit onto DEFAULT.
			sim._unit_damage_components[member_id] = doc_damage_components
		if doc_damage_type != "":
			# Source-authored BFME2 damage type (SLASH/PIERCE/CAVALRY/...);
			# fortress armor consumes it through the same scalar table the
			# historical Men constants use, unknown types keep the default.
			sim._unit_damage_types[member_id] = doc_damage_type
		elif doc_damage_components.is_empty() and not sim.missing_damage_type_units.has(member_id):
			# A combat unit whose document authors no damageType is recorded at
			# configure — never only when it happens to spawn (retail Arwen
			# carries powers but no weapon); its structure damage falls to each
			# kind's DEFAULT armor scalar.
			sim.missing_damage_type_units.append(member_id)
			print("[RetailSliceSim] unit '%s' has no authored damageType; its structure damage uses each kind's DEFAULT armor scalar" % member_id)
		sim._unit_production_rules[unit_type] = {
			"category": String(simulation.get("category", "")),
			"producer_kind": String(primary_producer["producer_kind"]),
			"producer_kinds": resolved_producer_kinds,
			"producer_routes": resolved_producers,
			"producer_source_object_id": String(primary_producer["producer_source_object_id"]),
			"object_id": member_id,
			"display_name": String(simulation["display_name"]),
			"default_cost": int(simulation["default_cost"]),
			"default_build_ticks": int(simulation["default_build_ticks"]),
			"default_command_points": int(simulation["default_command_points"]),
			"command_id": String(primary_producer.get("command_id", "")),
			"command_slot": int(primary_producer.get("slot", 0)),
			"surface": String(primary_producer.get("surface", "")),
		}
		var is_ring_hero_rule := false
		for producer_route in resolved_producers:
			if String(producer_route.get("source_field", "")) == "BuildableRingHeroesMP":
				is_ring_hero_rule = true
				break
		if is_ring_hero_rule:
			(sim._unit_production_rules[unit_type] as Dictionary)["is_ring_hero"] = true
		if not sim._production_unit_order.has(unit_type):
			sim._production_unit_order.append(unit_type)
		var created_hero: Dictionary = (
			(document_value as Dictionary).get("registration", {}) as Dictionary
		).get("createAHero", {}) as Dictionary
		if created_hero.has("ownerTeam"):
			sim._created_hero_owner_teams[unit_type] = int(created_hero["ownerTeam"])
		if not created_hero.is_empty() and created_hero.has("awardDefinitions"):
			var award_contract := created_hero.duplicate(true)
			award_contract["ownerTeam"] = int(created_hero.get("ownerTeam", -1))
			sim._cah_award_contracts[unit_type] = award_contract
		var prerequisites_by_producer: Dictionary = {}
		var any_groups_by_producer: Dictionary = {}
		for route in resolved_producers:
			var producer_kind := String(route["producer_kind"])
			var candidate: Array = (route.get("prerequisites", []) as Array).duplicate()
			if String(route.get("source_field", "")) == "BuildableRingHeroesMP" and candidate.is_empty():
				candidate = ["Upgrade_RingHero", "Upgrade_FortressRingHero"]
				print("[RetailSliceSim] stale-pack-ring-prereqs-synthesized unit=%s producer=%s" % [unit_type, producer_kind])
			var candidate_any: Array = (route.get("prerequisites_any_of", []) as Array).duplicate()
			if not prerequisites_by_producer.has(producer_kind):
				prerequisites_by_producer[producer_kind] = candidate
				any_groups_by_producer[producer_kind] = candidate_any
				continue
			# One authored route per producer level can list the same button with
			# different prerequisites (base/L2/L3 command sets). The unit is
			# trainable as soon as its cheapest authored variant's prerequisites
			# hold, so the effective requirement is the minimum-cardinality set;
			# ties keep deterministic document order. An ANY-of group counts as
			# one requirement, whatever its member count.
			var existing: Array = prerequisites_by_producer[producer_kind]
			var existing_any: Array = any_groups_by_producer.get(producer_kind, []) as Array
			var candidate_size := candidate.size() + (1 if not candidate_any.is_empty() else 0)
			var existing_size := existing.size() + (1 if not existing_any.is_empty() else 0)
			if candidate_size < existing_size:
				prerequisites_by_producer[producer_kind] = candidate
				any_groups_by_producer[producer_kind] = candidate_any
		sim._unit_prerequisites[unit_type] = prerequisites_by_producer
		sim._unit_prerequisite_any_groups[unit_type] = any_groups_by_producer
		# Hero SPECIAL_POWER abilities (converted doc rows) register per unit
		# type; heroes without an abilities array simply carry none.
		var ability_rows = sim.PlayableUnitAdapter.ability_rules(document_value as Dictionary)
		if not ability_rows.is_empty():
			sim._unit_ability_rules[unit_type] = sim._scaled_ability_rules(
				ability_rows, float(sim._rules.get("source_map_transform_scale", 0.0))
			)
		sim._ensure_capture_building_ability(unit_type, document_value as Dictionary)
		# ExperienceLevel chain (converted doc rows) registers per unit type;
		# units whose chain retail never authored carry no rule and never gain
		# XP — their kill payout is the recorded default, recorded per victim.
		var experience_rule = sim.PlayableUnitAdapter.experience_rule(document_value as Dictionary)
		if not experience_rule.is_empty():
			sim._unit_experience_rules[unit_type] = experience_rule
		# Converter moduleContracts (typed + opaque deferred): index for runtime
		# consumers. Deferred rows are still attached as authored evidence; only
		# executable rows drive behavior until their subsystems land.
		var contracts = sim.PlayableUnitAdapter.module_contracts(document_value as Dictionary)
		if not contracts.is_empty():
			sim._unit_module_contracts[unit_type] = contracts
	sim._rules["unit_rules"] = configured_unit_rules


func _register_builder_production(document: Dictionary, configured_unit_rules: Dictionary, producer_kinds: Dictionary) -> bool:
	var partial = sim.PlayableUnitAdapter.builder_production_rule(document)
	if partial.is_empty():
		return false
	var member_id := String(partial["object_id"])
	var builder_rule: Dictionary = configured_unit_rules.get(member_id, {}) as Dictionary
	if builder_rule.is_empty() or not bool(builder_rule.get("is_builder", false)):
		return false
	var resolved_producers: Array[Dictionary] = []
	var resolved_producer_kinds: Array[String] = []
	for producer_value in partial["producers"] as Array:
		var producer := producer_value as Dictionary
		var producer_kind := String(producer_kinds.get(String(producer.get("producer_source_object_id", "")), ""))
		if producer_kind == "":
			return false
		var route := producer.duplicate(true)
		route["producer_kind"] = producer_kind
		resolved_producers.append(route)
		if not resolved_producer_kinds.has(producer_kind):
			resolved_producer_kinds.append(producer_kind)
	var unit_type := String(partial["unit_type"])
	var primary_producer := resolved_producers[0]
	sim._unit_production_rules[unit_type] = {
		"category": String(partial.get("category", "")),
		"producer_kind": String(primary_producer["producer_kind"]),
		"producer_kinds": resolved_producer_kinds,
		"producer_routes": resolved_producers,
		"producer_source_object_id": String(primary_producer.get("producer_source_object_id", "")),
		"object_id": member_id,
		"display_name": String(partial["display_name"]),
		"default_cost": int(partial["default_cost"]),
		"default_build_ticks": int(partial["default_build_ticks"]),
		"default_command_points": int(partial["default_command_points"]),
		"command_id": String(primary_producer.get("command_id", "")),
		"command_slot": int(primary_producer.get("slot", 0)),
		"surface": String(primary_producer.get("surface", "")),
	}
	if not sim._production_unit_order.has(unit_type):
		sim._production_unit_order.append(unit_type)
	var prerequisites_by_producer: Dictionary = {}
	var any_groups_by_producer: Dictionary = {}
	for route_value in resolved_producers:
		var route := route_value as Dictionary
		var producer_kind := String(route["producer_kind"])
		if not prerequisites_by_producer.has(producer_kind):
			prerequisites_by_producer[producer_kind] = (route.get("prerequisites", []) as Array).duplicate()
			any_groups_by_producer[producer_kind] = (route.get("prerequisites_any_of", []) as Array).duplicate()
	sim._unit_prerequisites[unit_type] = prerequisites_by_producer
	sim._unit_prerequisite_any_groups[unit_type] = any_groups_by_producer
	return true


func _configure_ranger_runtime_contract() -> void:
	var value: Variant = sim._rules.get("ranger_runtime", {})
	if typeof(value) != TYPE_DICTIONARY:
		sim.configuration_error = "Ranger runtime contract is not a dictionary"
		return
	var contract := value as Dictionary
	if contract.is_empty():
		return
	if (
		String(contract.get("schema", "")) != "openbfme.ranger-runtime-contract"
		or int(contract.get("schemaVersion", -1)) != 0
		or String(contract.get("capabilityStatus", "")) != "rules-and-prerequisite-ready"
	):
		sim.configuration_error = "Ranger runtime contract identity is invalid"
		return
	var production_value: Variant = contract.get("production")
	var prerequisite_value: Variant = contract.get("prerequisite")
	var unit_rule_value: Variant = sim._rules.get("ranger_unit_rule")
	if typeof(production_value) != TYPE_DICTIONARY or typeof(prerequisite_value) != TYPE_DICTIONARY or typeof(unit_rule_value) != TYPE_DICTIONARY:
		sim.configuration_error = "Ranger runtime contract is missing production, prerequisite, or normalized unit rule"
		return
	var production := production_value as Dictionary
	var prerequisite := prerequisite_value as Dictionary
	var unit_rule := unit_rule_value as Dictionary
	var command_sets_value: Variant = contract.get("commandSets")
	var train_options_value: Variant = prerequisite.get("trainCommandOptions")
	var upgrade_options_value: Variant = prerequisite.get("options")
	if typeof(command_sets_value) != TYPE_ARRAY or typeof(train_options_value) != TYPE_ARRAY or typeof(upgrade_options_value) != TYPE_ARRAY:
		sim.configuration_error = "Ranger runtime contract options are invalid"
		return
	if not _ranger_command_sets_are_valid(command_sets_value as Array):
		sim.configuration_error = "Ranger runtime command-set transition is invalid"
		return
	var required_unit_rule_fields: Array[String] = [
		"horde_id", "member_count", "member_health", "member_damage",
		"speed", "speed_source", "acceleration", "acceleration_source",
		"turn_rate_degrees_per_second", "braking", "braking_source",
		"attack_range", "attack_range_source", "minimum_attack_range",
		"minimum_attack_range_source", "vision_range", "vision_range_source",
		"delay_between_shots_ms", "pre_attack_delay_ms", "firing_duration_ms",
		"attack_period_ticks", "pre_attack_ticks", "firing_duration_ticks",
		"clip_size", "clip_reload_time_ms", "continuous_fire_one",
		"continuous_fire_coast_ticks", "continuous_fire_rate_multiplier",
		"formation_positions", "provenance",
	]
	for field in required_unit_rule_fields:
		if not unit_rule.has(field):
			sim.configuration_error = "Ranger normalized unit rule is missing %s" % field
			return
	var upgrade_id := String(prerequisite.get("upgradeId", ""))
	var upgrade_cost := int(prerequisite.get("cost", -1))
	var upgrade_ticks = roundi(float(prerequisite.get("buildTimeSeconds", -1.0)) / sim.TICK_SECONDS)
	var ranger_cost := int(production.get("buildCost", -1))
	var ranger_ticks = roundi(float(production.get("buildTime", -1.0)) / sim.TICK_SECONDS)
	var ranger_command_points := int(production.get("commandPoints", -1))
	if (
		String(production.get("id", "")) != "GondorRangerHorde"
		or String(prerequisite.get("trainCommandId", "")) != "Command_ConstructGondorRangerHorde"
		or not (train_options_value as Array).has("NEED_UPGRADE")
		or upgrade_id != "Upgrade_GondorArcheryRangeLevel2"
		or String(prerequisite.get("type", "")) != "OBJECT"
		or not (upgrade_options_value as Array).has("CANCELABLE")
		or int(prerequisite.get("levelsToGain", 0)) != 1
		or int(prerequisite.get("levelCap", 0)) != 3
		or String(prerequisite.get("fromCommandSet", "")) != "GondorArcheryCommandSet"
		or String(prerequisite.get("toCommandSet", "")) != "GondorArcheryCommandSetLevel2"
		or upgrade_cost < 0
		or upgrade_ticks <= 0
		or ranger_cost < 0
		or ranger_ticks <= 0
		or ranger_command_points <= 0
		or String(unit_rule.get("horde_id", "")) != sim.RANGER_HORDE_ID
		or int(unit_rule.get("member_count", 0)) != 10
	):
		sim.configuration_error = "Ranger runtime contract values are invalid"
		return
	var configured_unit_rules: Dictionary = sim._rules.get("unit_rules", {}) as Dictionary
	configured_unit_rules[sim.RANGER_OBJECT_ID] = unit_rule.duplicate(true)
	sim._rules["unit_rules"] = configured_unit_rules
	sim._unit_production_rules[sim.RANGER_HORDE_ID] = {
		"producer_kind": "archery_range",
		"object_id": sim.RANGER_OBJECT_ID,
		"display_name": "Ithilien Rangers",
		"default_cost": ranger_cost,
		"default_build_ticks": ranger_ticks,
		"default_command_points": ranger_command_points,
	}
	sim._production_unit_order.append(sim.RANGER_HORDE_ID)
	# Load-bearing only in the tiny/overlay pack: when a playableUnit ranger
	# document is fieldable, the doc-driven prerequisite map overwrites this
	# entry below; without a doc it is the only prerequisite source.
	sim._unit_prerequisites[sim.RANGER_HORDE_ID] = upgrade_id
	# The overlay's own upgrade contract rides the same generic registration
	# the doc-driven structure chains use below; a doc-driven chain for the
	# same upgrade id overwrites it with the full authored chain.
	_register_structure_upgrade_contract(sim._structure_upgrade_contracts, upgrade_id, {
		"structure_kind": "archery_range",
		"cost": upgrade_cost,
		"duration_ticks": upgrade_ticks,
		"levels_to_gain": 1,
		"level_cap": 3,
		"to_level": 2,
		"cancelable": true,
		"command_id": "",
		"from_command_set": String(prerequisite.get("fromCommandSet", "")),
		"to_command_set": String(prerequisite.get("toCommandSet", "")),
		"requires_upgrade_id": "",
		"health_add": 0,
		"production_multiplier": 1.0,
	})


func _global_upgrade_source() -> Dictionary:
	## The three raw upgrade tables the global (player-faction) path reads,
	## resolved exactly as before: an explicit top-level rules key wins, else the
	## compiled faction manifest supplies it.
	var manifest: Dictionary = sim._rules.get("faction_manifest", {}) as Dictionary
	return {
		"structure_upgrade_chains": sim._rules.get("structure_upgrade_chains", manifest.get("structure_upgrade_chains", {})),
		"structure_research": sim._rules.get("structure_research", manifest.get("structure_research", {})),
		"structure_upgrade_effects": sim._rules.get("structure_upgrade_effects", manifest.get("structure_upgrade_effects", {})),
		"structure_castle_upgrades": sim._rules.get("structure_castle_upgrades", manifest.get("structure_castle_upgrades", {})),
	}


func _manifest_upgrade_source(manifest: Dictionary) -> Dictionary:
	## The same three raw tables read straight off a per-team faction manifest
	## (cross-faction path). The manifest dicts already carry these keys, so no
	## importer change is needed.
	return {
		"structure_upgrade_chains": manifest.get("structure_upgrade_chains", {}),
		"structure_research": manifest.get("structure_research", {}),
		"structure_upgrade_effects": manifest.get("structure_upgrade_effects", {}),
		"structure_castle_upgrades": manifest.get("structure_castle_upgrades", {}),
	}


func _configure_structure_upgrade_chains() -> void:
	_compile_structure_upgrade_chains(_global_upgrade_source(), sim._structure_upgrade_contracts)
	_compile_structure_castle_upgrades(_global_upgrade_source(), sim._structure_upgrade_contracts)


func _compile_structure_castle_upgrades(source: Dictionary, contracts: Dictionary) -> void:
	## Retail's fortress improvement surface (the OBJECT_UPGRADE buttons in the
	## fortress command set's upgrades page — commandset.ini
	## MordorFortressCommandSet slots 8-13 DoomPyres/LavaMoat/FireArrows/
	## MagmaCauldrons/MorgulSorcery/GorgorothSpire, and the same shape for every
	## other faction).
	##
	## Unlike a level chain these do NOT swap the command set or raise the
	## building's level: the button buys a *Trigger* OBJECT upgrade and the
	## fortress's own CastleUpgrade module hands out the real one (see
	## `_castle_upgrade_grants`). They therefore ride their own contract branch:
	## no from/to command set, no level gain, one purchase each.
	##
	## Source rows are {kind: {"upgrades": [{upgradeId, grantsUpgradeId, cost,
	## buildTimeSeconds, slot, commandId, labelId, tooltipId, buttonImageId,
	## neededUpgradeIds?, requiresUpgradeId?}]}}. Malformed rows fail closed.
	if sim.configuration_error != "":
		return
	var value: Variant = source.get("structure_castle_upgrades", {})
	if typeof(value) != TYPE_DICTIONARY:
		sim.configuration_error = "Structure castle upgrades are not a dictionary"
		return
	var kinds: Array[String] = []
	for kind_value in (value as Dictionary).keys():
		kinds.append(String(kind_value))
	kinds.sort()
	for kind in kinds:
		var surface_value: Variant = (value as Dictionary).get(kind)
		if typeof(surface_value) != TYPE_DICTIONARY:
			sim.configuration_error = "Structure castle upgrade surface for '%s' is not a dictionary" % kind
			return
		var rows: Array = (surface_value as Dictionary).get("upgrades", []) as Array
		if rows.is_empty():
			sim.configuration_error = "Structure castle upgrade surface for '%s' is malformed" % kind
			return
		for row_value in rows:
			if typeof(row_value) != TYPE_DICTIONARY:
				sim.configuration_error = "Structure castle upgrade surface for '%s' has a malformed row" % kind
				return
			var row := row_value as Dictionary
			var upgrade_id := String(row.get("upgradeId", ""))
			var granted_id := String(row.get("grantsUpgradeId", ""))
			var cost := int(row.get("cost", -1))
			var build_seconds := float(row.get("buildTimeSeconds", 0.0))
			# Zero build time is authored evidence (see the research surface's
			# RotWK BuildTime 0 note); the duration clamps to >= 1 tick below.
			# An EMPTY grant is authored evidence, not a gap: retail's Banners /
			# Siege Kegs / Oil Casks / Mighty Catapult buttons buy an upgrade
			# that applies to the fortress itself with no CastleUpgrade pass-out
			# module behind it (commandset.ini:4107 slots 8/9/11/13).
			if upgrade_id == "" or cost < 0 or build_seconds < 0.0:
				sim.configuration_error = "Structure castle upgrade '%s' on '%s' is malformed" % [upgrade_id, kind]
				return
			var needed: Array[String] = []
			for needed_value in Array(row.get("neededUpgradeIds", [])):
				needed.append(String(needed_value))
			var contract := {
				"structure_kind": kind,
				"cost": cost,
				"duration_ticks": maxi(1, roundi(build_seconds / sim.TICK_SECONDS)),
				"level_cap": 99,
				"levels_to_gain": 0,
				"to_level": 0,
				"cancelable": bool(row.get("cancelable", false)),
				"from_command_set": "",
				"to_command_set": "",
				"requires_upgrade_id": String(row.get("requiresUpgradeId", "")),
				"health_add": 0,
				"production_multiplier": 1.0,
				"castle_upgrade": true,
				"grants_upgrade_id": granted_id,
				"command_id": String(row.get("commandId", "")),
				"slot": int(row.get("slot", 0)),
				"label_id": String(row.get("labelId", "")),
				"tooltip_id": String(row.get("tooltipId", "")),
				"image_id": String(row.get("buttonImageId", "")),
				"lacks_prerequisite_label_id": String(row.get("lacksPrerequisiteLabelId", "")),
				"needed_upgrade_ids": needed,
				"needed_upgrade_any": bool(row.get("neededUpgradeAny", false)),
			}
			if not _register_structure_upgrade_contract(contracts, upgrade_id, contract):
				return


func _compile_structure_upgrade_chains(source: Dictionary, contracts: Dictionary) -> void:
	## Generic doc-driven structure levels: every authored upgrade chain the
	## faction manifest projected from the playableStructure.* documents
	## (cost/time/level cap/command-set swap/per-level effects) registers into
	## `contracts` keyed by its authored upgrade id. Malformed chains fail closed
	## into sim.configuration_error instead of registering a partial contract.
	if sim.configuration_error != "":
		return
	var value: Variant = source.get("structure_upgrade_chains", {})
	if typeof(value) != TYPE_DICTIONARY:
		sim.configuration_error = "Structure upgrade chains are not a dictionary"
		return
	var kinds: Array[String] = []
	for kind_value in (value as Dictionary).keys():
		kinds.append(String(kind_value))
	kinds.sort()
	for kind in kinds:
		var chain_value: Variant = (value as Dictionary).get(kind)
		if typeof(chain_value) != TYPE_DICTIONARY:
			sim.configuration_error = "Structure upgrade chain for '%s' is not a dictionary" % kind
			return
		var chain := chain_value as Dictionary
		var level_cap := int(chain.get("levelCap", 0))
		var steps_value: Variant = chain.get("steps")
		if level_cap < 2 or typeof(steps_value) != TYPE_ARRAY or (steps_value as Array).is_empty():
			sim.configuration_error = "Structure upgrade chain for '%s' is malformed" % kind
			return
		var previous_to_level := 1
		for step_value in steps_value as Array:
			if typeof(step_value) != TYPE_DICTIONARY:
				sim.configuration_error = "Structure upgrade chain for '%s' has a malformed step" % kind
				return
			var step := step_value as Dictionary
			var upgrade_id := String(step.get("upgradeId", ""))
			var to_level := int(step.get("toLevel", 0))
			var cost := int(step.get("cost", -1))
			var build_seconds := float(step.get("buildTimeSeconds", 0.0))
			var from_set := String(step.get("fromCommandSet", ""))
			var to_set := String(step.get("toCommandSet", ""))
			if (
				upgrade_id == ""
				or to_level <= previous_to_level
				or to_level > level_cap
				or cost < 0
				or build_seconds <= 0.0
				or from_set == ""
				or to_set == ""
				or int(step.get("levelsToGain", 0)) < 1
				or int(step.get("levelCap", 0)) != level_cap
			):
				sim.configuration_error = "Structure upgrade chain for '%s' step '%s' is malformed" % [kind, upgrade_id]
				return
			var health_add := 0
			var production_multiplier := 1.0
			var unsupported: Array[String] = []
			for leaf_value in Array(step.get("effects", [])):
				if typeof(leaf_value) != TYPE_DICTIONARY:
					sim.configuration_error = "Structure upgrade chain for '%s' has a malformed effect" % kind
					return
				var leaf := leaf_value as Dictionary
				for modifier_value in Array(leaf.get("modifiers", [])):
					if typeof(modifier_value) != TYPE_DICTIONARY:
						sim.configuration_error = "Structure upgrade chain for '%s' has a malformed modifier" % kind
						return
					var modifier := modifier_value as Dictionary
					match String(modifier.get("kind", "")):
						"HEALTH":
							health_add += roundi(float(modifier.get("value", 0.0)))
						"PRODUCTION":
							production_multiplier *= float(modifier.get("value", 1.0))
						_:
							sim.configuration_error = "Structure upgrade chain for '%s' has an unsupported modifier" % kind
							return
				for unsupported_value in Array(leaf.get("unsupportedModifiers", [])):
					unsupported.append(String(unsupported_value))
			var contract := {
				"structure_kind": kind,
				"cost": cost,
				"duration_ticks": maxi(1, roundi(build_seconds / sim.TICK_SECONDS)),
				"levels_to_gain": int(step.get("levelsToGain", 1)),
				"level_cap": level_cap,
				"to_level": to_level,
				"cancelable": bool(step.get("cancelable", false)),
				"command_id": String(step.get("commandId", "")),
				"from_command_set": from_set,
				"to_command_set": to_set,
				"requires_upgrade_id": String(step.get("requiresUpgradeId", "")),
				"health_add": health_add,
				"production_multiplier": production_multiplier,
				# Purchase-surface + per-level model data from the doc: the HUD
				# button binds these, the structure presenter applies them.
				"slot": int(step.get("slot", 0)),
				"label_id": String(step.get("labelId", "")),
				"tooltip_id": String(step.get("tooltipId", "")),
				"image_id": String(step.get("buttonImageId", "")),
			}
			var presentation_value: Variant = step.get("presentation")
			if typeof(presentation_value) == TYPE_DICTIONARY:
				var presentation := presentation_value as Dictionary
				var visible: Array[String] = []
				var hidden: Array[String] = []
				for token_value in Array(presentation.get("visibleSubObjects", [])):
					visible.append(String(token_value))
				for token_value in Array(presentation.get("hiddenSubObjects", [])):
					hidden.append(String(token_value))
				contract["visible_sub_objects"] = visible
				contract["hidden_sub_objects"] = hidden
			if not unsupported.is_empty():
				unsupported.sort()
				contract["unsupported_modifiers"] = unsupported
			if not _register_structure_upgrade_contract(contracts, upgrade_id, contract):
				return
			previous_to_level = to_level


func _register_structure_upgrade_contract(contracts: Dictionary, upgrade_id: String, contract: Dictionary) -> bool:
	## One upgrade id binds exactly one structure kind WITHIN the target table;
	## collisions fail closed instead of picking a winner. `contracts` is the
	## global dict for the player faction and a per-team dict for a cross-faction
	## team, so the fail-close scopes per team (Men/Elf ids never collide).
	if contracts.has(upgrade_id):
		var existing: Dictionary = contracts[upgrade_id]
		if String(existing.get("structure_kind", "")) != String(contract.get("structure_kind", "")):
			sim.configuration_error = "Structure upgrade '%s' binds two structure kinds" % upgrade_id
			return false
	contracts[upgrade_id] = contract
	return true


func _configure_structure_research_contracts() -> void:
	_compile_structure_research_contracts(
		_global_upgrade_source(),
		sim._structure_upgrade_contracts,
		sim._structure_upgrade_effects,
		sim._compiled_research_kinds,
	)


func _compile_structure_research_contracts(source: Dictionary, contracts: Dictionary, upgrade_effects: Dictionary, research_kinds: Dictionary) -> void:
	## Doc-driven PLAYER technology sales: every authored research row the
	## faction manifest projected from the playableStructure.* documents
	## (cost/time/gate/button identity) registers into `contracts` keyed by its
	## authored upgrade id, with the per-kind effect bundles in `upgrade_effects`
	## and the compiled kinds recorded in `research_kinds`. Malformed surfaces
	## fail closed into sim.configuration_error.
	if sim.configuration_error != "":
		return
	var research_value: Variant = source.get("structure_research", {})
	if typeof(research_value) != TYPE_DICTIONARY:
		sim.configuration_error = "Structure research surfaces are not a dictionary"
		return
	var effects_value: Variant = source.get("structure_upgrade_effects", {})
	if typeof(effects_value) != TYPE_DICTIONARY:
		sim.configuration_error = "Structure upgrade effects are not a dictionary"
		return
	var kinds: Array[String] = []
	for kind_value in (effects_value as Dictionary).keys():
		kinds.append(String(kind_value))
	kinds.sort()
	for kind in kinds:
		var container: Dictionary = (effects_value as Dictionary).get(kind, {}) as Dictionary
		var normalized_effects: Array = []
		for effect_value in Array(container.get("effects", [])):
			if typeof(effect_value) != TYPE_DICTIONARY:
				continue
			var effect := effect_value as Dictionary
			if String(effect.get("kind", "")) == "command-set-transition":
				var normalized_command_set := _normalized_command_set_upgrade_effect(effect)
				if normalized_command_set.is_empty():
					sim.configuration_error = "Structure '%s' has a malformed CommandSetUpgrade effect" % kind
					return
				normalized_effects.append(normalized_command_set)
				continue
			normalized_effects.append({
				"upgrade_id": String(effect.get("upgradeId", "")),
				"kind": String(effect.get("kind", "")),
				"apply_to_upgrade_ids": Array(effect.get("applyToUpgradeIds", [])).duplicate(),
				"percent": float(effect.get("percent", 0.0)),
				"upgrade_discount": bool(effect.get("upgradeDiscount", false)),
				"refund_percent": float(effect.get("refundPercent", 0.0)),
				"building_required": String(effect.get("buildingRequired", "")),
				"bonus_percent": float(effect.get("bonusPercent", 0.0)),
				"upgrade_must_be_present": String(effect.get("upgradeMustBePresent", "")),
				"label_id": String(effect.get("labelId", "")),
			})
		upgrade_effects[kind] = {
			"effects": normalized_effects,
			"unsupported_effects": Array(container.get("unsupportedEffects", [])).duplicate(true),
		}
	kinds.clear()
	for kind_value in (research_value as Dictionary).keys():
		kinds.append(String(kind_value))
	kinds.sort()
	for kind in kinds:
		var surface: Dictionary = (research_value as Dictionary).get(kind, {}) as Dictionary
		var rows: Array = surface.get("upgrades", []) as Array
		if rows.is_empty():
			sim.configuration_error = "Structure research surface for '%s' is malformed" % kind
			return
		research_kinds[kind] = true
		for row_value in rows:
			if typeof(row_value) != TYPE_DICTIONARY:
				sim.configuration_error = "Structure research surface for '%s' has a malformed row" % kind
				return
			var row := row_value as Dictionary
			var upgrade_id := String(row.get("upgradeId", ""))
			var needed: Array[String] = []
			for needed_value in Array(row.get("neededUpgradeIds", [])):
				needed.append(String(needed_value))
			var contract := {
				"structure_kind": kind,
				"cost": maxi(0, int(row.get("cost", 0))),
				"duration_ticks": maxi(1, roundi(float(row.get("buildTimeSeconds", 0.0)) / sim.TICK_SECONDS)),
				"level_cap": 99,
				"levels_to_gain": 0,
				"cancelable": bool(row.get("cancelable", false)),
				"to_command_set": "",
				"team_tech": true,
				# Research rows ride every per-level command set of the building;
				# the authored slot/labels/images surface them like chain steps.
				"research": true,
				"command_id": String(row.get("commandId", "")),
				"slot": int(row.get("slot", 0)),
				"label_id": String(row.get("labelId", "")),
				"tooltip_id": String(row.get("tooltipId", "")),
				"image_id": String(row.get("buttonImageId", "")),
				"lacks_prerequisite_label_id": String(row.get("lacksPrerequisiteLabelId", "")),
				"needed_upgrade_ids": needed,
				"needed_upgrade_any": bool(row.get("neededUpgradeAny", false)),
			}
			if not _register_structure_upgrade_contract(contracts, upgrade_id, contract):
				return


func _normalized_command_set_upgrade_effect(effect: Dictionary) -> Dictionary:
	var game := String(effect.get("game", "")).to_lower()
	var active_game = String(sim._rules.get("game", "")).to_lower()
	var triggers_value: Variant = effect.get("triggerUpgradeIds")
	var provenance_value: Variant = effect.get("commandSetProvenance")
	if (
		game not in ["bfme2", "rotwk"]
		or active_game not in ["bfme2", "rotwk"]
		or game != active_game
		or String(effect.get("effectId", "")).strip_edges() == ""
		or String(effect.get("upgradeId", "")).strip_edges() == ""
		or String(effect.get("triggerSemantics", "")) not in ["any", "all"]
		or String(effect.get("commandSetId", "")).strip_edges() == ""
		or String(effect.get("module", "")) != "CommandSetUpgrade"
		or String(effect.get("runtimeStatus", "")) != "executable"
		or String(effect.get("descriptorStatus", "")) != "resolved"
		or typeof(triggers_value) != TYPE_ARRAY
		or (triggers_value as Array).is_empty()
		or typeof(provenance_value) != TYPE_DICTIONARY
	):
		return {}
	var triggers: Array[String] = []
	var seen: Dictionary = {}
	for trigger_value in triggers_value as Array:
		var trigger := String(trigger_value).strip_edges()
		var folded := trigger.to_lower()
		if trigger == "" or seen.has(folded):
			return {}
		seen[folded] = true
		triggers.append(trigger)
	if not triggers.has(String(effect.get("upgradeId", ""))):
		return {}
	var provenance := provenance_value as Dictionary
	if (
		String(provenance.get("authored", "")).strip_edges() != String(effect.get("commandSetId", ""))
		or String(provenance.get("sourceIni", "")).strip_edges() == ""
		or int(provenance.get("line", 0)) <= int(effect.get("line", 0))
	):
		return {}
	if effect.has("customAnimation"):
		var animation_value: Variant = effect.get("customAnimation")
		if typeof(animation_value) != TYPE_DICTIONARY:
			return {}
		var animation := animation_value as Dictionary
		if (
			String(animation.get("animState", "")).strip_edges() == ""
			or float(animation.get("animTimeMs", -1.0)) < 0.0
			or String(animation.get("runtimeStatus", "")) != "deferred"
			or String(animation.get("deferredReason", "")) != "presentation-runtime-not-accepted"
		):
			return {}
	return effect.duplicate(true)


func _research_gate_unsatisfied(team: int, building: Dictionary, contract: Dictionary) -> String:
	## "" when the research's NeededUpgrade row is satisfied: each needed id is
	## a team technology or a completed structure upgrade on this building.
	var needed: Array = contract.get("needed_upgrade_ids", [])
	if needed.is_empty():
		return ""
	var owned: Dictionary = sim.team_upgrades.get(team, {}) as Dictionary
	var completed: Array = building.get("completed_upgrades", [])
	var satisfied := 0
	var first_missing := ""
	for needed_value in needed:
		var needed_id := String(needed_value)
		if owned.has(needed_id) or completed.has(needed_id):
			satisfied += 1
		elif first_missing == "":
			first_missing = needed_id
	if bool(contract.get("needed_upgrade_any", false)):
		return "" if satisfied > 0 else (first_missing if first_missing != "" else String(needed[0]))
	return "" if satisfied == needed.size() else first_missing


func _configure_trebuchet_runtime_contract() -> void:
	var value: Variant = sim._rules.get("trebuchet_runtime", {})
	if typeof(value) != TYPE_DICTIONARY:
		sim.configuration_error = "Trebuchet runtime contract is not a dictionary"
		return
	var contract := value as Dictionary
	if contract.is_empty():
		return
	if (
		String(contract.get("schema", "")) != "openbfme.trebuchet-runtime-contract"
		or int(contract.get("schemaVersion", -1)) != 0
		or String(contract.get("capabilityStatus", "")) != "bounded-direct-structure-ready"
	):
		sim.configuration_error = "Trebuchet runtime contract identity is invalid"
		return
	var production: Dictionary = contract.get("production", {}) as Dictionary
	var unit: Dictionary = contract.get("unit", {}) as Dictionary
	var movement: Dictionary = unit.get("movement", {}) as Dictionary
	var combat: Dictionary = contract.get("combat", {}) as Dictionary
	var workshop: Dictionary = contract.get("workshop", {}) as Dictionary
	var workshop_stats: Dictionary = workshop.get("stats", {}) as Dictionary
	var scale = float(sim._rules.get("source_map_transform_scale", 0.0))
	if (
		scale <= 0.0
		or String(production.get("id", "")) != "GondorTrebuchet"
		or String(unit.get("objectId", "")) != "GondorTrebuchet"
		or String(workshop_stats.get("id", "")) != "GondorWorkshop"
		or String(workshop.get("trainCommandId", "")) != "Command_ConstructGondorTrebuchet"
		or String(movement.get("mode", "")) != "existing-generic-unit-path"
		or String(combat.get("scope", "")) != "direct-structure-first-slice"
		or String(combat.get("damageType", "")).to_lower() != "siege"
	):
		sim.configuration_error = "Trebuchet runtime contract values are invalid"
		return
	var speed_source := float(movement.get("speed", 0.0))
	var attack_range_source := float(combat.get("attackRange", 0.0))
	var minimum_range_source := float(combat.get("minimumAttackRange", 0.0))
	var vision_source := float(unit.get("visionRange", 0.0))
	var delay_ms := float(combat.get("delayBetweenShotsMs", 0.0))
	var pre_attack_ms := float(combat.get("preAttackDelayMs", 0.0))
	var firing_ms := float(combat.get("firingDurationMs", 0.0))
	var health := int(unit.get("maximumHealth", 0))
	var damage := int(combat.get("damage", 0))
	var build_cost := int(production.get("buildCost", -1))
	var build_ticks = roundi(float(production.get("buildTime", -1.0)) / sim.TICK_SECONDS)
	var command_points := int(production.get("commandPoints", -1))
	var workshop_cost := int(workshop_stats.get("buildCost", -1))
	var workshop_ticks = roundi(float(workshop_stats.get("buildTime", -1.0)) / sim.TICK_SECONDS)
	var workshop_health := int(workshop_stats.get("maxHealth", 0))
	if (
		speed_source <= 0.0
		or attack_range_source <= 0.0
		or minimum_range_source <= 0.0
		or vision_source <= 0.0
		or attack_range_source <= minimum_range_source
		or health <= 0
		or damage <= 0
		or build_cost <= 0
		or build_ticks <= 0
		or command_points <= 0
		or workshop_cost <= 0
		or workshop_ticks <= 0
		or workshop_health <= 0
		or delay_ms <= 0.0
		or pre_attack_ms <= 0.0
		or firing_ms <= 0.0
	):
		sim.configuration_error = "Trebuchet runtime numeric contract is invalid"
		return
	var configured_unit_rules: Dictionary = sim._rules.get("unit_rules", {}) as Dictionary
	var trebuchet_rule := {
		"horde_id": sim.TREBUCHET_OBJECT_ID,
		"member_count": 1,
		"member_health": health,
		"member_damage": damage,
		"speed": speed_source * scale,
		"speed_source": speed_source,
		"attack_range": attack_range_source * scale,
		"attack_range_source": attack_range_source,
		"minimum_attack_range": minimum_range_source * scale,
		"minimum_attack_range_source": minimum_range_source,
		"vision_range": vision_source * scale,
		"vision_range_source": vision_source,
		"delay_between_shots_ms": delay_ms,
		"pre_attack_delay_ms": pre_attack_ms,
		"firing_duration_ms": firing_ms,
		"attack_period_ticks": maxi(1, roundi(delay_ms / (sim.TICK_SECONDS * 1000.0))),
		"pre_attack_ticks": maxi(0, roundi(pre_attack_ms / (sim.TICK_SECONDS * 1000.0))),
		"firing_duration_ticks": maxi(0, roundi(firing_ms / (sim.TICK_SECONDS * 1000.0))),
		"clip_size": int(combat.get("clipSize", 0)),
		"clip_reload_time_ms": 0.0,
		"continuous_fire_one": 0,
		"continuous_fire_coast_ticks": 0,
		"continuous_fire_rate_multiplier": 1.0,
		"formation_positions": [Vector3.ZERO],
		"formation_positions_base": [Vector3.ZERO],
		"formation_mode": "Line",
		"provenance": {"contractSources": contract.get("sources", []).duplicate(true)},
	}
	# GondorTrebuchet binds CatapultLocomotor for SET_NORMAL (trebuchet.ini
	# LocomotorSet), and that template authors Acceleration = Braking = 1000
	# plus TurnTime (locomotor.ini:1683 in BFME2, TurnTime 3000 -> 120 deg/s in
	# RotWK 2.01). m3_pack_expansion now binds those three numbers onto the M3
	# contract. A pack cooked before that binding is a NAMED gap: the trebuchet
	# still builds and shoots, and _step_route refuses to move it while saying
	# exactly why. There is no invented ramp and no 360 deg/s placeholder.
	var trebuchet_movement_gaps: Array = []
	for authored_field in ["acceleration", "braking", "turnRateDegreesPerSecond"]:
		if movement.has(authored_field):
			continue
		trebuchet_movement_gaps.append(authored_field)
	if trebuchet_movement_gaps.is_empty():
		trebuchet_rule["acceleration"] = float(movement["acceleration"]) * scale
		trebuchet_rule["acceleration_source"] = float(movement["acceleration"])
		trebuchet_rule["braking"] = float(movement["braking"]) * scale
		trebuchet_rule["braking_source"] = float(movement["braking"])
		trebuchet_rule["turn_rate_degrees_per_second"] = float(movement["turnRateDegreesPerSecond"])
	else:
		# printerr, not push_warning: this is a lane-authored, expected-until-recook
		# data gap, and gate-m2-focused treats any engine WARNING as a defect.
		printerr(
			"NAMED GAP: %s carries no authored %s; the M3 trebuchet contract predates the CatapultLocomotor binding and the unit will not move until the pack is recooked"
			% [sim.TREBUCHET_OBJECT_ID, ", ".join(PackedStringArray(trebuchet_movement_gaps))]
		)
		trebuchet_rule["unauthored_locomotor_fields"] = trebuchet_movement_gaps
	configured_unit_rules[sim.TREBUCHET_OBJECT_ID] = trebuchet_rule
	sim._rules["unit_rules"] = configured_unit_rules
	sim._unit_production_rules[sim.TREBUCHET_OBJECT_ID] = {
		"producer_kind": "workshop",
		"object_id": sim.TREBUCHET_OBJECT_ID,
		"display_name": "Gondor Trebuchet",
		"default_cost": build_cost,
		"default_build_ticks": build_ticks,
		"default_command_points": command_points,
	}
	if not sim._production_unit_order.has(sim.TREBUCHET_OBJECT_ID):
		sim._production_unit_order.append(sim.TREBUCHET_OBJECT_ID)
	sim._structure_max_health["workshop"] = workshop_health
	sim._structure_build_rules["workshop"] = {"cost": workshop_cost, "seconds": float(workshop_stats.get("buildTime", 0.0))}


func _ranger_command_sets_are_valid(command_sets: Array) -> bool:
	var expected := {
		"GondorArcheryCommandSet": "Command_PurchaseUpgradeGondorArcheryRangeLevel2",
		"GondorArcheryCommandSetLevel2": "Command_PurchaseUpgradeGondorArcheryRangeLevel3",
	}
	var matched: Dictionary = {}
	for command_set_value in command_sets:
		if typeof(command_set_value) != TYPE_DICTIONARY:
			return false
		var command_set := command_set_value as Dictionary
		var command_set_id := String(command_set.get("id", ""))
		if not expected.has(command_set_id):
			continue
		var commands_value: Variant = command_set.get("commands")
		if typeof(commands_value) != TYPE_ARRAY:
			return false
		var has_ranger := false
		var has_upgrade := false
		for command_value in commands_value as Array:
			if typeof(command_value) != TYPE_DICTIONARY:
				return false
			var command := command_value as Dictionary
			var command_id := String(command.get("id", ""))
			var slot := int(command.get("slot", 0))
			has_ranger = has_ranger or (command_id == "Command_ConstructGondorRangerHorde" and slot == 2)
			has_upgrade = has_upgrade or (command_id == String(expected[command_set_id]) and slot == 4)
		if not has_ranger or not has_upgrade:
			return false
		matched[command_set_id] = true
	return matched.size() == expected.size()



func _apply_scenario_structure_faction_command_set(row: Dictionary, team: int) -> Dictionary:
	## Resolve the owning team's exact PLAYER upgrade, then let the generic typed
	## CommandSetUpgrade effect graph choose the authored set. The trained-set
	## scan remains compatibility-only for selected packs cooked before that graph
	## was accepted; a recook removes this branch without changing gameplay.
	var sets := row.get("scenario_trained_command_sets", []) as Array
	if sets.is_empty():
		return {"ok": false, "reason": "no-trained-command-sets"}
	var side_result = sim.team_retail_side(team)
	if side_result.has("reason"):
		return {"ok": false, "reason": String(side_result["reason"])}
	var side := String(side_result.get("side", ""))
	var upgrade_stem := {"Dwarves": "Dwarf", "Elves": "Elf"}.get(side, side) as String
	var faction_upgrade := "Upgrade_%sFaction" % upgrade_stem
	var completed := row.get("completed_upgrades", []) as Array
	if not completed.has(faction_upgrade):
		completed.append(faction_upgrade)
		completed.sort()
		row["completed_upgrades"] = completed
	var graph_result := _reconcile_structure_command_set_upgrades(row)
	if bool(graph_result.get("accepted_graph", false)):
		if not bool(graph_result.get("ok", false)):
			return graph_result
		return {
			"ok": true,
			"reason": "",
			"upgrade_id": faction_upgrade,
			"command_set_id": String(row.get("command_set_id", "")),
			"effect_id": String(graph_result.get("effect_id", "")),
		}
	# Compatibility for the currently selected pre-recook neutral artifacts.
	var selected: Dictionary = {}
	for set_value in sets:
		if typeof(set_value) != TYPE_DICTIONARY:
			continue
		var candidate := set_value as Dictionary
		if String(candidate.get("kind", "")) == "upgraded" and (candidate.get("triggeredBy", []) as Array).has(faction_upgrade):
			selected = candidate
			break
	if selected.is_empty():
		return {"ok": false, "reason": "faction-upgrade-not-authored", "upgrade_id": faction_upgrade}
	var prior := String(row.get("command_set_id", row.get("default_command_set_id", "")))
	var selected_id := String(selected.get("id", ""))
	if selected_id == "":
		return {"ok": false, "reason": "authored-command-set-id-empty"}
	row["command_set_id"] = selected_id
	if prior != selected_id:
		sim._emit_event("upgrade.scenario_command_set", int(row.get("id", 0)), 0, {
			"team": team,
			"upgrade_id": faction_upgrade,
			"from": prior,
			"to": selected_id,
		})
	return {"ok": true, "reason": "", "upgrade_id": faction_upgrade, "command_set_id": selected_id}


func _structure_command_set_upgrade_effects(row: Dictionary) -> Array[Dictionary]:
	var source: Array = []
	if row.has("scenario_command_set_upgrade_effects"):
		source = row.get("scenario_command_set_upgrade_effects", []) as Array
	else:
		var team := int(row.get("team", -1))
		var kind := String(row.get("structure_kind", ""))
		var bundle = sim.structure_upgrade_effects_for_team(team).get(kind, {}) as Dictionary
		source = bundle.get("effects", []) as Array
	var by_id: Dictionary = {}
	var edge_ids: Dictionary = {}
	for effect_value in source:
		if typeof(effect_value) != TYPE_DICTIONARY:
			continue
		var effect := effect_value as Dictionary
		if String(effect.get("kind", "")) != "command-set-transition":
			continue
		var effect_id := String(effect.get("effectId", ""))
		if effect_id == "":
			continue
		if by_id.has(effect_id):
			var existing := by_id[effect_id] as Dictionary
			for field in ["game", "triggerUpgradeIds", "triggerSemantics", "commandSetId", "moduleTag", "moduleOrdinal", "commandSetProvenance"]:
				if existing.get(field) != effect.get(field):
					return []
		else:
			by_id[effect_id] = effect
			edge_ids[effect_id] = {}
		var edge_key := String(effect.get("upgradeId", "")).to_lower()
		if edge_key == "" or (edge_ids[effect_id] as Dictionary).has(edge_key):
			return []
		(edge_ids[effect_id] as Dictionary)[edge_key] = true
	var result: Array[Dictionary] = []
	for effect_id_value in by_id.keys():
		var effect := by_id[effect_id_value] as Dictionary
		var expected_edges: Dictionary = {}
		for trigger_value in effect.get("triggerUpgradeIds", []) as Array:
			expected_edges[String(trigger_value).to_lower()] = true
		if (edge_ids[effect_id_value] as Dictionary).keys().size() != expected_edges.keys().size():
			return []
		for expected_key in expected_edges.keys():
			if not (edge_ids[effect_id_value] as Dictionary).has(expected_key):
				return []
		result.append(effect.duplicate(true))
	result.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		if int(a.get("moduleOrdinal", 0)) != int(b.get("moduleOrdinal", 0)):
			return int(a.get("moduleOrdinal", 0)) < int(b.get("moduleOrdinal", 0))
		if String(a.get("sourceIni", "")) != String(b.get("sourceIni", "")):
			return String(a.get("sourceIni", "")).naturalnocasecmp_to(String(b.get("sourceIni", ""))) < 0
		if int(a.get("line", 0)) != int(b.get("line", 0)):
			return int(a.get("line", 0)) < int(b.get("line", 0))
		return String(a.get("effectId", "")).naturalnocasecmp_to(String(b.get("effectId", ""))) < 0
	)
	return result


func _reconcile_structure_command_set_upgrades(row: Dictionary) -> Dictionary:
	var effects := _structure_command_set_upgrade_effects(row)
	if effects.is_empty():
		var declared_graph := false
		var declared_source: Array = row.get("scenario_command_set_upgrade_effects", []) as Array
		if not row.has("scenario_command_set_upgrade_effects"):
			var bundle = sim.structure_upgrade_effects_for_team(int(row.get("team", -1))).get(String(row.get("structure_kind", "")), {}) as Dictionary
			declared_source = bundle.get("effects", []) as Array
		for effect_value in declared_source:
			if typeof(effect_value) == TYPE_DICTIONARY and String((effect_value as Dictionary).get("kind", "")) == "command-set-transition":
				declared_graph = true
				break
		return {"ok": false, "reason": "malformed-command-set-upgrade-graph" if declared_graph else "no-accepted-command-set-upgrade", "accepted_graph": declared_graph}
	var active_game = String(sim._rules.get("game", "")).to_lower()
	var team_owned = sim.team_upgrades.get(int(row.get("team", -1)), {}) as Dictionary
	var object_owned := row.get("completed_upgrades", []) as Array
	var selected: Dictionary = {}
	for effect in effects:
		if String(effect.get("game", "")).to_lower() != active_game:
			return {"ok": false, "reason": "command-set-upgrade-edition-mismatch", "accepted_graph": true}
		var matched := 0
		var triggers := effect.get("triggerUpgradeIds", []) as Array
		for trigger_value in triggers:
			var trigger := String(trigger_value)
			if sim._dictionary_has_casefolded_key(team_owned, trigger) or sim._array_has_casefolded_string(object_owned, trigger):
				matched += 1
		var eligible := matched == triggers.size() if String(effect.get("triggerSemantics", "")) == "all" else matched > 0
		if eligible:
			selected = effect
	var command_field := "command_set_id" if row.has("scenario_source_object_id") else "command_set"
	if not row.has("command_set_upgrade_base"):
		row["command_set_upgrade_base"] = String(row.get(command_field, row.get("default_command_set_id", "")))
	var next_set := String(row.get("command_set_upgrade_base", ""))
	var next_effect := ""
	if not selected.is_empty():
		next_set = String(selected.get("commandSetId", ""))
		next_effect = String(selected.get("effectId", ""))
	var prior_set := String(row.get(command_field, ""))
	var prior_effect := String(row.get("command_set_upgrade_active_effect", ""))
	row[command_field] = next_set
	row["command_set_upgrade_active_effect"] = next_effect
	row["command_set_upgrade_receipt"] = {
		"game": active_game,
		"effectId": next_effect,
		"commandSetId": next_set,
		"triggerUpgradeIds": Array(selected.get("triggerUpgradeIds", [])).duplicate() if not selected.is_empty() else [],
		"triggerSemantics": String(selected.get("triggerSemantics", "")),
		"commandSetProvenance": (selected.get("commandSetProvenance", {}) as Dictionary).duplicate(true) if not selected.is_empty() else {},
		"customAnimationStatus": "deferred" if selected.has("customAnimation") else "absent",
	}
	if prior_set != next_set or prior_effect != next_effect:
		sim._emit_event("upgrade.command_set", int(row.get("id", 0)), 0, {
			"team": int(row.get("team", -1)), "game": active_game,
			"effect_id": next_effect, "from": prior_set, "to": next_set,
			"presentation_custom_anim": "deferred" if selected.has("customAnimation") else "absent",
		})
	return {"ok": not selected.is_empty(), "reason": "" if not selected.is_empty() else "triggers-unsatisfied", "accepted_graph": true, "effect_id": next_effect, "command_set_id": next_set}


