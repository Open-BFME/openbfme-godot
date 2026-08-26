extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Behavior-module contract subsystem extracted from retail_slice_sim.gd
## (Q81 strangler-fig extraction #13): every _attach_* contract binder
## plus its step/request/apply logic. Verbatim move, pin-verified.





func _attach_auto_heal_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("auto_heal_behavior"):
		return
	var executable := bool(contract.get("executable", false))
	if not executable and String(contract.get("runtime_status", "")) == "executable":
		executable = true
	if not executable:
		return
	var fields := contract.get("fields", {}) as Dictionary
	if not bool(_module_contract_value(fields, "StartsActive", false)):
		return
	var amount := int(_module_contract_value(fields, "HealingAmount", 0))
	var delay_milliseconds := float(_module_contract_value(fields, "HealingDelay", 0.0))
	if amount <= 0 or delay_milliseconds <= 0.0:
		return
	var tick_milliseconds = _sim.TICK_SECONDS * 1000.0
	var exact_ticks = delay_milliseconds / tick_milliseconds
	# A cadence the tick rate cannot express would be rounded, i.e. healed at a
	# rate nobody authored. Refuse and say so instead.
	if exact_ticks < 1.0 or not is_equal_approx(exact_ticks, float(roundi(exact_ticks))):
		push_error(
			"[RetailSliceSim] AutoHealBehavior %s authors HealingDelay %.0fms, which is not a whole %.0fms sim tick; healing refused rather than run at a rounded rate"
			% [String(contract.get("tag", "")), delay_milliseconds, tick_milliseconds]
		)
		return
	var delay_ticks := roundi(exact_ticks)
	row["auto_heal_behavior"] = {
		"healing_amount": amount,
		"healing_delay_ticks": delay_ticks,
		"start_delay_ticks": _ship_contract_delay_ticks(
			float(_module_contract_value(fields, "StartHealingDelay", 0.0))
		),
		"heal_only_if_not_in_combat": bool(
			_module_contract_value(fields, "HealOnlyIfNotInCombat", false)
		),
		"heal_only_if_not_under_attack": bool(
			_module_contract_value(fields, "HealOnlyIfNotUnderAttack", false)
		),
		"armed_tick": _sim.tick_index,
		"next_heal_tick": -1,
		"tag": String(contract.get("tag", "")),
		"source_ini": String(contract.get("sourceIni", contract.get("source_ini", ""))),
		"line": int(contract.get("line", 0)),
	}


func _step_auto_heal_updates() -> void:
	var _sim = sim
	for id in _sim.entity_ids():
		if not _sim.entities.has(id):
			continue
		var row := _sim.entities[id] as Dictionary
		if not row.has("auto_heal_behavior") and not row.has("module_contracts"):
			_attach_module_contracts(row)
		if row.has("auto_heal_behavior"):
			_apply_auto_heal_pulse(row, true)
	for id in _sim.structure_ids():
		if not _sim.structures.has(id):
			continue
		var row := _sim.structures[id] as Dictionary
		if (
			not row.has("auto_heal_behavior")
			and not bool(row.get("structure_module_contracts_attached", false))
		):
			_attach_structure_module_contracts(row)
		if row.has("auto_heal_behavior"):
			_apply_auto_heal_pulse(row, false)


func _apply_auto_heal_pulse(row: Dictionary, battalion: bool) -> void:
	var _sim = sim
	var policy := row.get("auto_heal_behavior", {}) as Dictionary
	if policy.is_empty():
		return
	var health := int(row.get("health", 0))
	var maximum := int(row.get("maximum_health", 0))
	# Dead stays dead: AutoHealBehavior never resurrects.
	if health <= 0 or maximum <= 0:
		return
	if health >= maximum:
		# Undamaged objects bank nothing; the restart delay is measured from the
		# next damage, not from the last pulse.
		policy["next_heal_tick"] = -1
		return
	var start_delay := int(policy.get("start_delay_ticks", 0))
	var anchor := int(policy.get("armed_tick", 0)) + start_delay
	# Either authored gate hangs the pulse on the last damage the object took:
	# HealOnlyIfNotInCombat while the fight is on, HealOnlyIfNotUnderAttack while
	# incoming damage is still landing. The window is the authored
	# StartHealingDelay in both cases; retail authors no separate magnitude.
	if (
		bool(policy.get("heal_only_if_not_in_combat", false))
		or bool(policy.get("heal_only_if_not_under_attack", false))
	):
		anchor = int(row.get("last_damage_tick", -1000000)) + start_delay
	var next_heal := int(policy.get("next_heal_tick", -1))
	if next_heal < 0 or next_heal < anchor:
		next_heal = maxi(anchor, _sim.tick_index)
	if _sim.tick_index < next_heal:
		policy["next_heal_tick"] = next_heal
		return
	policy["next_heal_tick"] = next_heal + maxi(1, int(policy.get("healing_delay_ticks", 1)))
	var amount := int(policy.get("healing_amount", 0))
	if amount <= 0:
		return
	if not battalion:
		row["health"] = mini(maximum, health + amount)
		return
	var health_values: Array = row.get("member_health", [])
	var member_maximum := int(row.get("member_maximum_health", 0))
	if health_values.is_empty() or member_maximum <= 0:
		row["health"] = mini(maximum, health + amount)
		return
	var remaining := amount
	for index in range(health_values.size()):
		if remaining <= 0:
			break
		var current := int(health_values[index])
		# A dead member is not healed back into the battalion.
		if current <= 0 or current >= member_maximum:
			continue
		var healed := mini(remaining, member_maximum - current)
		health_values[index] = current + healed
		remaining -= healed
	row["member_health"] = health_values
	var aggregate := 0
	for value in health_values:
		aggregate += int(value)
	row["health"] = aggregate


# --- Hero ability surface (converted SPECIAL_POWER rows) ---
# Per-hero cast surface for converter-emitted abilities: cooldowns, authored
# level gates, and the effect kinds the converted leaves actually provide
# (weapon blasts, heals, OCL summons, attribute modifiers with duration).
# Abilities needing unimplemented systems stay unavailable with their
# converted reason; nothing is faked.


func _scaled_ability_rules(rules: Array[Dictionary], source_scale: float) -> Array[Dictionary]:
	## Bind converted ability rows to this map's source scale. Retail ranges
	## arrive in source units and scale exactly like combat attack ranges.
	var _sim = sim
	var output: Array[Dictionary] = []
	for rule in rules:
		var scaled := (rule as Dictionary).duplicate(true)
		var effect: Dictionary = (scaled.get("effect", {}) as Dictionary).duplicate(true)
		var scale := source_scale if source_scale > 0.0 else 1.0
		var power_contract := (scaled.get("special_power_contract", {}) as Dictionary).duplicate(true)
		for range_key in ["forbiddenObjectRange", "viewObjectRange", "maxCastRange"]:
			if power_contract.has(range_key):
				power_contract[range_key + "Scaled"] = float(power_contract[range_key]) * scale
		var receipts: Array[String] = []
		if bool(power_contract.get("publicTimer", false)):
			receipts.append("hud_binding:PublicTimer")
		if power_contract.has("viewObjectRange") or power_contract.has("viewObjectDurationMs"):
			receipts.append("view_object_model_binding")
		for audio_key in ["initiateIntentSoundId", "enterStateIntentSoundId", "successEvaEventId"]:
			if String(power_contract.get(audio_key, "")) != "":
				receipts.append("presentation_binding:%s" % audio_key)
		power_contract["unsupported_semantics"] = receipts
		scaled["special_power_contract"] = power_contract
		match String(effect.get("kind", "")):
			"weapon-blast":
				effect["damage_radius"] = float(effect.get("damageRadius", 0.0)) * scale
				var range_source := float(effect.get("attackRange", effect.get("startAbilityRange", 0.0)))
				effect["range"] = range_source * scale
				# Converter-emitted knockback magnitudes (source units) bind to
				# map scale like every other range. Gandalf's Wizard Blast is
				# the compiled Men ability that carries them today
				# (knockbackRadius 110, knockbackStrength 70); abilities whose
				# MetaImpactNugget extraction is still importer follow-up keep
				# these keys at 0 and deal damage without a shockwave —
				# fail-closed, nothing invented.
				effect["knockback_radius"] = float(effect.get("knockbackRadius", 0.0)) * scale
				effect["knockback_strength"] = float(effect.get("knockbackStrength", 0.0)) * scale
			"heal":
				effect["radius_scaled"] = float(effect.get("radius", 0.0)) * scale
			"attribute-modifier":
				effect["duration_ticks"] = maxi(1, roundi(float(effect.get("durationMs", 0.0)) / (_sim.TICK_SECONDS * 1000.0)))
				effect["range_scaled"] = float(effect.get("range", 0.0)) * scale
			"leadership-aura":
				# AttributeModifierAuraUpdate: Range binds to map scale like every
				# other authored range; the modifier list itself is scale-free.
				effect["range_scaled"] = float(effect.get("range", 0.0)) * scale
			"terror":
				# FearNugget/TerrorSpecialPower: radius and the optional scatter
				# displacement are source units; FearDuration is milliseconds.
				effect["radius_scaled"] = float(effect.get("radius", 0.0)) * scale
				effect["duration_ticks"] = maxi(1, roundi(float(effect.get("durationMs", 0.0)) / (_sim.TICK_SECONDS * 1000.0)))
				effect["scatter_strength_scaled"] = float(effect.get("scatterStrength", 0.0)) * scale
			"mount-toggle":
				# Mounted LocomotorSet speed is source units, like every speed.
				effect["mounted_speed_scaled"] = float(effect.get("mountedSpeed", 0.0)) * scale
			"capture-building":
				# StartAbilityRange gates the cast like an attack range; the
				# channel is the authored unpack + preparation + pack envelope.
				effect["range"] = float(effect.get("startAbilityRange", 0.0)) * scale
				var channel_ms := float(effect.get("unpackMs", 0.0)) + float(effect.get("preparationMs", 0.0)) + float(effect.get("packMs", 0.0))
				effect["channel_ticks"] = maxi(1, roundi(channel_ms / (_sim.TICK_SECONDS * 1000.0)))
			"experience-grant":
				# LevelGrantSpecialPower: StartAbilityRange gates the cast like
				# an attack range; RadiusEffect is the grant circle; the
				# authored Experience amount is scale-free.
				effect["range"] = float(effect.get("startAbilityRange", 0.0)) * scale
				effect["radius_scaled"] = float(effect.get("radiusEffect", 0.0)) * scale
			"arrow-storm":
				# ArrowStormUpdate barrage: ranges/radii are source units; the
				# burst cadence is the authored PersistentPrepTime; shot counts
				# and per-shot weapon damage are scale-free.
				effect["range"] = float(effect.get("startAbilityRange", 0.0)) * scale
				effect["target_radius_scaled"] = float(effect.get("targetRadius", 0.0)) * scale
				effect["shot_interval_ticks"] = maxi(1, roundi(float(effect.get("persistentPrepMs", 0.0)) / (_sim.TICK_SECONDS * 1000.0)))
			"stealth-toggle":
				# ToggleHiddenSpecialAbilityUpdate / InvisibilitySpecialPower:
				# EffectDuration is milliseconds; an authored BroadcastRadius
				# (ally cloak) binds to map scale. Zero stays zero: a duration
				# the converter could not resolve keeps the cast fail-closed,
				# while a leaf retail authors no duration for arrives flagged
				# `untimed` and cloaks until recast instead.
				var stealth_ms := float(effect.get("effectDurationMs", 0.0))
				effect["duration_ticks"] = maxi(1, roundi(stealth_ms / (_sim.TICK_SECONDS * 1000.0))) if stealth_ms > 0.0 else 0
				effect["broadcast_radius_scaled"] = float(effect.get("broadcastRadius", 0.0)) * scale
			"teleport":
				# TeleportSpecialAbilityUpdate: an authored MaxDistance gates the
				# cast like a range; omission is unlimited. BusyForDuration holds
				# the hero after arrival. DestinationWeaponName fires at arrival.
				effect["range"] = float(effect.get("maxDistance", 0.0)) * scale
				effect["busy_ticks"] = maxi(0, roundi(float(effect.get("busyForDurationMs", 0.0)) / (_sim.TICK_SECONDS * 1000.0)))
				if effect.has("destinationWeapon"):
					var destination_weapon := (effect.get("destinationWeapon", {}) as Dictionary).duplicate(true)
					destination_weapon["knockback_radius"] = float(destination_weapon.get("knockbackRadius", 0.0)) * scale
					destination_weapon["knockback_strength"] = float(destination_weapon.get("knockbackStrength", 0.0)) * scale
					effect["destinationWeapon"] = destination_weapon
			"curse":
				# CurseSpecialPower: StartAbilityRange gates the cast; the
				# radius cursor bounds target selection; CursePercentage is
				# scale-free.
				effect["range"] = float(effect.get("startAbilityRange", 0.0)) * scale
				effect["radius_scaled"] = float(effect.get("radiusCursorRadius", 0.0)) * scale
			"leadership-strip":
				# SpecialPowerModule AntiCategory=LEADERSHIP: the authored
				# AttributeModifierRange binds to map scale; the paired
				# ModifierList authors only the suppression duration.
				effect["radius_scaled"] = float(effect.get("attributeModifierRange", 0.0)) * scale
				var strip_ms := float(effect.get("antiCategoryDurationMs", 0.0))
				effect["duration_ticks"] = maxi(1, roundi(strip_ms / (_sim.TICK_SECONDS * 1000.0))) if strip_ms > 0.0 else 0
			"activate-module-graph":
				effect["range"] = float(effect.get("startAbilityRange", 0.0)) * scale
				effect["effect_range_scaled"] = float(effect.get("effectRange", 0.0)) * scale
				var timing := effect.get("timingMs", {}) as Dictionary
				var timing_ticks: Dictionary = {}
				for timing_key in ["StartDelay", "PreparationTime", "PersistentPrepTime", "UnpackTime", "PackTime", "SpecialPowerDuration"]:
					if timing.has(timing_key):
						timing_ticks[timing_key] = maxi(0, roundi(float(timing[timing_key]) / (_sim.TICK_SECONDS * 1000.0)))
				effect["timing_ticks"] = timing_ticks
				var scaled_routes: Array = []
				for route_value in effect.get("routes", []) as Array:
					var route := (route_value as Dictionary).duplicate(true)
					var nested_rule := {"effect": (route.get("effect", {}) as Dictionary).duplicate(true)}
					var nested_scaled := _scaled_ability_rules([nested_rule], source_scale)
					if not nested_scaled.is_empty():
						route["effect"] = (nested_scaled[0] as Dictionary).get("effect", {})
					scaled_routes.append(route)
				effect["routes"] = scaled_routes
			"weapon-mode-special-power":
				effect["duration_ticks"] = _ship_contract_delay_ticks(float(effect.get("durationMs", 0.0)))
			"toggle-deploy":
				effect["unpack_ticks"] = _ship_contract_delay_ticks(float(effect.get("unpackTimeMs", 0.0)))
				effect["pack_ticks"] = _ship_contract_delay_ticks(float(effect.get("packTimeMs", 0.0)))
			"dominate-enemy":
				effect["range"] = float(effect.get("startAbilityRange", 0.0)) * scale
				effect["dominate_radius_scaled"] = float(effect.get("dominateRadius", 0.0)) * scale
				var timing := effect.get("timingMs", {}) as Dictionary
				var timing_ticks: Dictionary = {}
				for timing_key in ["UnpackTime", "PreparationTime", "FreezeAfterTriggerDuration", "TriggerModelConditionDuration"]:
					if timing.has(timing_key):
						timing_ticks[timing_key] = maxi(0, roundi(float(timing[timing_key]) / (_sim.TICK_SECONDS * 1000.0)))
				effect["timing_ticks"] = timing_ticks
				if not bool(effect.get("permanentlyConvert", false)):
					var temporary_ms := float(effect.get("temporaryDefectDurationMs", 0.0))
					effect["temporary_defect_duration_ticks"] = maxi(1, roundi(temporary_ms / (_sim.TICK_SECONDS * 1000.0))) if temporary_ms > 0.0 else 0
			"grab-passenger":
				var acquire := (effect.get("acquire", {}) as Dictionary).duplicate(true)
				effect["range"] = float(acquire.get("startAbilityRange", 0.0)) * scale
				var acquire_timing := acquire.get("timingMs", {}) as Dictionary
				var acquire_ticks: Dictionary = {}
				for timing_key in ["UnpackTime", "PreparationTime", "PersistentPrepTime", "PackTime"]:
					acquire_ticks[timing_key] = maxi(0, roundi(float(acquire_timing.get(timing_key, 0.0)) / (_sim.TICK_SECONDS * 1000.0)))
				acquire["timing_ticks"] = acquire_ticks
				var animation := (acquire.get("animation", {}) as Dictionary).duplicate(true)
				animation["duration_ticks"] = maxi(0, roundi(float(animation.get("durationMs", 0.0)) / (_sim.TICK_SECONDS * 1000.0)))
				animation["trigger_ticks"] = maxi(0, roundi(float(animation.get("triggerTimeMs", 0.0)) / (_sim.TICK_SECONDS * 1000.0)))
				acquire["animation"] = animation
				effect["acquire"] = acquire
				var scaled_release: Array = []
				for release_value in effect.get("releaseAbilities", []) as Array:
					var nested := _scaled_ability_rules([{"effect": (release_value as Dictionary).duplicate(true)}], source_scale)
					if not nested.is_empty():
						scaled_release.append((nested[0] as Dictionary).get("effect", {}))
				effect["releaseAbilities"] = scaled_release
			"fling-passenger":
				var fling_timing := effect.get("timingMs", {}) as Dictionary
				effect["timing_ticks"] = {
					"UnpackTime": maxi(0, roundi(float(fling_timing.get("UnpackTime", 0.0)) / (_sim.TICK_SECONDS * 1000.0))),
					"PackTime": maxi(0, roundi(float(fling_timing.get("PackTime", 0.0)) / (_sim.TICK_SECONDS * 1000.0))),
				}
				var fling_velocity := effect.get("velocity", {}) as Dictionary
				if not fling_velocity.is_empty():
					effect["horizontal_velocity_scaled"] = Vector2(float(fling_velocity.get("x", 0.0)), float(fling_velocity.get("y", 0.0))) * scale
					effect["vertical_velocity_source"] = float(fling_velocity.get("z", 0.0))
				var landing := (effect.get("landingWarhead", {}) as Dictionary).duplicate(true)
				if not landing.is_empty():
					landing["radius_scaled"] = float(landing.get("radius", 0.0)) * scale
					effect["landingWarhead"] = landing
		scaled["effect"] = effect
		output.append(scaled)
	return output


func _attach_hero_ability_state(row: Dictionary) -> void:
	## Spawn-time ability state for one hero entity. Retail heroes enter at
	## rank 1; the authored level gates read this row's live level, which the
	## experience pipeline raises as the hero gains ranks.
	var states: Dictionary = {}
	for rule_value in sim._unit_ability_rules.get(String(row.get("unit_type", "")), []) as Array:
		var rule := rule_value as Dictionary
		states[String(rule.get("ability_id", ""))] = {
			"cooldown_ready_tick": 0,
			"cooldown_ticks": int(rule.get("cooldown_ticks", 0)),
		}
	row["level"] = 1
	row["ability_states"] = states


# --- Experience / veterancy ---
# Per-entity XP pool driven by the converted ExperienceLevel chains. A member
# kill pays the victim's authored ExperienceAward at the victim's current
# level; levels unlock at the authored cumulative thresholds; each level's
# authored permanent modifiers (HEALTH / DAMAGE_ADD) fold into the per-member
# base stats. Revived heroes re-enter through production at rank 1 with an
# empty pool, matching retail. Units retail never authored a chain for carry
# no rule: they pay the recorded default (zero) and the fallback is recorded
# per victim unit type instead of inventing an award.


func _attach_module_contracts(row: Dictionary) -> void:
	## Attach converter moduleContracts to a live entity for runtime consumers.
	## Deferred/opaque rows stay as authored evidence. Executable KeepObjectDie
	## contracts fold into death policy (destroyOnDeath=false keeps the object).
	# Lazy consumers call this from damage, targeting, sim.containment, and periodic
	# updates. Attachment is materialization, not an accumulating effect: replaying
	# it duplicated repeated contract arrays and made live state diverge from a
	# restored snapshot after one tick.
	var _sim = sim
	if row.has("module_contracts"):
		return
	var unit_type := String(row.get("unit_type", ""))
	var contracts: Array = _sim._unit_module_contracts.get(unit_type, []) as Array
	if contracts.is_empty():
		return
	row["module_contracts"] = contracts.duplicate(true)
	for contract_value in contracts:
		if typeof(contract_value) != TYPE_DICTIONARY:
			continue
		var contract := contract_value as Dictionary
		var module_name := String(contract.get("module", ""))
		var fields: Dictionary = contract.get("fields", {}) as Dictionary
		var folded := module_name.to_lower()
		var executable := bool(contract.get("executable", false))
		if folded.contains("keepobjectdie") or folded.contains("createobjectdie"):
			if not fields.is_empty():
				var die_rows: Array = row.get("module_die_contracts", []) as Array
				die_rows.append({
					"module": module_name,
					"fields": fields.duplicate(true),
					"executable": executable,
				})
				row["module_die_contracts"] = die_rows
		# KeepObjectDie: forbids erase when death_type matches policy.
		if executable and folded.contains("keepobjectdie"):
			var destroy_on_death := true
			if fields.has("destroyOnDeath"):
				destroy_on_death = bool(fields.get("destroyOnDeath", true))
			if not destroy_on_death:
				var death_types := String(fields.get("deathTypes", "ALL"))
				var excluded: Array = []
				var excluded_raw: Variant = fields.get("excludedDeathTypes", [])
				if typeof(excluded_raw) == TYPE_ARRAY:
					for item in excluded_raw as Array:
						excluded.append(String(item).to_upper())
				# Only ALL is proven (same honesty bar as DestroyDie). Other
				# death-type subsets stay as module_die_contracts evidence.
				if death_types == "ALL":
					row["keep_object_die"] = true
					row["keep_object_die_module"] = module_name
					row["keep_object_die_policy"] = {
						"death_types": death_types,
						"excluded_death_types": excluded,
					}
		# CreateObjectDie: queue CreationList on matching death (second consumer).
		if executable and folded.contains("createobjectdie"):
			var creation_list := ""
			var cl_raw: Variant = fields.get("CreationList", fields.get("creationList", {}))
			if typeof(cl_raw) == TYPE_DICTIONARY:
				creation_list = String((cl_raw as Dictionary).get("value", ""))
			elif typeof(cl_raw) == TYPE_STRING:
				creation_list = String(cl_raw)
			var death_types2 := String(fields.get("deathTypes", "ALL"))
			var excluded2: Array = []
			var excluded_raw2: Variant = fields.get("excludedDeathTypes", [])
			if typeof(excluded_raw2) == TYPE_ARRAY:
				for item2 in excluded_raw2 as Array:
					excluded2.append(String(item2).to_upper())
			var included2: Array = []
			var included_raw2: Variant = fields.get("includedDeathTypes", [])
			if typeof(included_raw2) == TYPE_ARRAY:
				for item3 in included_raw2 as Array:
					included2.append(String(item3).to_upper())
			# ALL [-excluded] and NONE [+included] are both executable filters.
			if creation_list != "" and death_types2 in ["ALL", "NONE"]:
				row["create_object_die"] = true
				row["create_object_die_policy"] = {
					"creation_list": creation_list,
					"death_types": death_types2,
					"excluded_death_types": excluded2,
					"included_death_types": included2,
				}
		# AttributeModifierUpgrade / GeometryUpgrade ledgers for upgrade grants.
		if executable and (
			folded.contains("attributemodifierupgrade")
			or folded.contains("geometryupgrade")
		):
			var upgrade_rows: Array = row.get("module_upgrade_contracts", []) as Array
			upgrade_rows.append({
				"module": module_name,
				"fields": fields.duplicate(true),
				"executable": true,
			})
			row["module_upgrade_contracts"] = upgrade_rows
		if folded == "fireweaponwhendeadbehavior":
			_sim._attach_fire_weapon_when_dead_contract(row, contract)
		elif folded == "hordetransportcontain":
			_sim._attach_horde_transport_contract(row, contract)
		elif folded in ["transportcontain", "tunnelcontain", "garrisoncontain", "hordegarrisoncontain"]:
			_sim._attach_container_family_contract(row, contract)
		elif folded == "productionqueuehordecontain":
			_sim._attach_container_family_contract(row, contract)
		elif folded == "siegeenginecontain":
			_sim._attach_siege_engine_contain_contract(row, contract)
		elif folded == "largegroupbonusupdate":
			_sim._attach_large_group_bonus_contract(row, contract)
		elif folded == "hitreactionbehavior":
			_sim._attach_hit_reaction_contract(row, contract)
		elif folded == "animalaiupdate":
			_sim._attach_animal_ai_contract(row, contract)
		elif folded == "threatfinderupdate":
			_sim._attach_threat_finder_contract(row, contract)
		elif folded == "radiatefearupdate":
			_sim._attach_radiate_fear_contract(row, contract)
		elif folded == "poisonedbehavior":
			_sim._attach_poisoned_contract(row, contract)
		elif folded == "damagefieldupdate":
			_sim._attach_damage_field_contract(row, contract)
		elif folded == "spawnunitbehavior":
			_sim._attach_spawn_unit_contract(row, contract)
		elif folded == "modelconditionsoundselectorclientbehavior":
			_sim._attach_model_condition_sound_selector(row, contract)
		elif folded == "randomsoundselectorclientbehavior":
			_sim._attach_random_sound_selector(row, contract)
		elif folded == "upgradesoundselectorclientbehavior":
			_sim._attach_upgrade_sound_selector(row, contract)
		elif folded == "largegroupaudioupdate":
			_sim._attach_large_group_audio_contract(row, contract)
		elif folded == "firespreadupdate":
			_sim._attach_fire_spread_contract(row, contract)
		elif folded == "shipslowdeathbehavior":
			_sim._attach_ship_slow_death_contract(row, contract)
		elif folded == "slowdeathbehavior":
			_sim._attach_slow_death_core_contract(row, contract)
		elif folded == "attributemodifierauraupdate":
			_sim._attach_attribute_modifier_aura_contract(row, contract)
		elif folded == "autohealbehavior":
			_attach_auto_heal_contract(row, contract)
		elif folded == "lifetimeupdate":
			_sim._attach_lifetime_update_contract(row, contract)
		elif folded == "stancesbehavior":
			_sim._attach_stances_contract(row, contract)
		elif folded == "aiupdateinterface":
			_attach_ai_update_contract(row, contract)
		elif folded == "hordeaiupdate":
			_attach_horde_ai_update_contract(row, contract)
		elif folded == "pickupstuffupdate":
			_sim._attach_pickup_stuff_update_contract(row, contract)
		elif folded == "autoabilitybehavior":
			_sim._attach_auto_ability_contract(row, contract)
		elif folded == "aispecialpowerupdate":
			_sim._attach_ai_special_power_contract(row, contract)
		elif folded == "weaponmodespecialpowerupdate":
			_sim._attach_weapon_mode_special_power_contract(row, contract)
		elif folded == "respawnupdate":
			_sim._attach_respawn_update_contract(row, contract)
		elif folded == "fireweaponupdate":
			_sim._attach_fire_weapon_update_contract(row, contract)
		elif folded == "deletionupdate":
			_sim._attach_deletion_update_contract(row, contract)
		elif folded == "productionupdate":
			_sim._attach_production_update_contract(row, contract)
		elif folded == "gettingbuiltbehavior":
			_sim._attach_getting_built_contract(row, contract)
		elif folded == "buildingbehavior":
			_sim._attach_building_behavior_contract(row, contract)
		elif folded == "queueproductionexitupdate":
			_sim._attach_queue_production_exit_contract(row, contract)
		elif folded == "rebuildholeexposeddie" or folded == "rebuildholeexposedie":
			_sim._attach_rebuild_hole_expose_contract(row, contract)
		elif folded == "rebuildholebehavior":
			_sim._attach_rebuild_hole_behavior_contract(row, contract)
		elif folded == "bannercarrierupdate":
			_sim._attach_banner_carrier_update_contract(row, contract)
		elif folded == "respawnbody":
			_sim._attach_respawn_body_contract(row, contract)
		elif folded == "giveupgradeupdate":
			_sim._attach_give_upgrade_contract(row, contract)
		elif folded == "gateopenandclosebehavior":
			_sim._attach_gate_open_close_contract(row, contract)
		elif folded == "aigateupdate":
			_sim._attach_ai_gate_contract(row, contract)
		elif folded == "fakepathfindportalbehaviour":
			_sim._attach_fake_pathfind_portal_contract(row, contract)
		elif folded == "stealthdetectorupdate":
			_sim._attach_stealth_detector_contract(row, contract)
		elif folded == "invisibilityupdate":
			_sim._attach_invisibility_update_contract(row, contract)
		elif folded == "slavedupdate":
			_sim._attach_slaved_update_contract(row, contract)
		elif folded == "castleupgrade":
			_sim._attach_castle_upgrade_contract(row, contract)
		elif folded == "spawnbehavior":
			_sim._attach_spawn_behavior_contract(row, contract)
		elif folded == "stealthupdate":
			_sim._attach_stealth_update_contract(row, contract)
		elif folded == "objectcreationupgrade":
			_sim._attach_object_creation_upgrade_contract(row, contract)
		elif folded == "attributemodifierupgrade":
			_sim._attach_attribute_modifier_upgrade_contract(row, contract)
		elif folded == "geometryupgrade":
			_sim._attach_geometry_upgrade_contract(row, contract)
		elif folded == "emotiontrackerupdate":
			_sim._attach_emotion_tracker_contract(row, contract)
		elif folded == "castlememberbehavior":
			_sim._attach_castle_member_contract(row, contract)
		elif folded == "inactivebody":
			_sim._attach_inactive_body_contract(row, contract)
		elif folded == "squishcollide":
			_sim._attach_squish_collide_contract(row, contract)
		elif folded == "hordemembercollide":
			_sim._attach_horde_member_collide_contract(row, contract)
		elif folded == "notifytargetsofimminentprobablecrushingupdate":
			_sim._attach_notify_crushing_contract(row, contract)
		elif folded == "flammableupdate":
			_sim._attach_flammable_update_contract(row, contract)
		elif folded == "dynamicportalbehaviour":
			_sim._attach_dynamic_portal_contract(row, contract)
		elif folded == "foundationaiupdate":
			_sim._attach_foundation_ai_contract(row, contract)
		elif folded == "monitorconditionupdate":
			_sim._attach_monitor_condition_contract(row, contract)
		elif folded == "refunddie":
			_sim._attach_refund_die_contract(row, contract)
		elif folded == "dualweaponbehavior":
			_sim._attach_dual_weapon_contract(row, contract)
		elif folded == "attachupdate":
			_sim._attach_attach_update_contract(row, contract)
		elif folded == "replaceselfupgrade":
			_sim._attach_replace_self_contract(row, contract)
		elif folded == "citadelslaughterhordecontain":
			_sim._attach_citadel_slaughter_contract(row, contract)
		elif folded == "oclupdate":
			_sim._attach_ocl_update_contract(row, contract)
		elif folded == "hordecontain":
			_attach_horde_contain_contract(row, contract)
		elif folded == "stopspecialpower":
			_sim._attach_stop_special_power_contract(row, contract)
		elif folded == "unleashspecialpower":
			_sim._attach_unleash_special_power_contract(row, contract)
		elif folded == "specialenemysenseupdate":
			_sim._attach_special_enemy_sense_contract(row, contract)


func module_contracts_for_unit_type(unit_type: String) -> Array:
	return (sim._unit_module_contracts.get(unit_type, []) as Array).duplicate(true)


func register_unit_module_contracts(unit_type: String, contracts: Array) -> void:
	## Index unit-carried moduleContracts by unit type, the same way
	## register_structure_module_contracts indexes the structure table. Headless
	## fixtures and scenario documents both reach the attach path through here.
	if unit_type.strip_edges() == "" or contracts.is_empty():
		return
	sim._unit_module_contracts[unit_type] = contracts.duplicate(true)


func register_structure_module_contracts(key: String, contracts: Array) -> void:
	## Index structure-carried moduleContracts (KeepObjectDie/CreateObjectDie).
	if key.strip_edges() == "" or contracts.is_empty():
		return
	sim._structure_module_contracts[key] = contracts.duplicate(true)


# De-staticed on extraction (instance sim access).
func _contracts_have_executable_refund_die(contracts: Array) -> bool:
	for contract_value in contracts:
		if typeof(contract_value) != TYPE_DICTIONARY:
			continue
		var contract := contract_value as Dictionary
		if String(contract.get("module", "")).to_lower() != "refunddie":
			continue
		if bool(contract.get("executable", false)) or String(
			contract.get("runtimeStatus", contract.get("runtime_status", ""))
		) == "executable":
			return true
	return false


func _stamp_refund_die_creation_cost(row: Dictionary, cost: int) -> void:
	## Preserve the creation-time/charged price only for objects that actually
	## carry an executable RefundDie row. Unrelated objects retain their exact
	## legacy state bytes and state pins.
	if cost < 0 or row.has("cached_build_cost"):
		return
	for key_value in [
		row.get("source_object_id", ""), row.get("object_id", ""),
		row.get("structure_kind", ""), row.get("kind", ""),
	]:
		var key := String(key_value)
		if key != "" and _contracts_have_executable_refund_die(
			sim._structure_module_contracts.get(key, []) as Array
		):
			row["cached_build_cost"] = cost
			return


func _configure_playable_structure_module_contracts() -> void:
	## Prefer the sealed registry already projected into simulation rules. This
	## keeps fixture/replacement behavior available to headless runners while
	## retaining ContentDB as the live-scene source.
	var _sim = sim
	var runtimes_value: Variant = _sim._rules.get("playable_structure_runtimes", {})
	if typeof(runtimes_value) != TYPE_DICTIONARY or (runtimes_value as Dictionary).is_empty():
		var db = _content_db_ref()
		if db == null:
			return
		runtimes_value = db.get("playable_structure_runtimes")
	if typeof(runtimes_value) != TYPE_DICTIONARY:
		return
	var runtimes: Dictionary = runtimes_value
	for object_id_value in runtimes.keys():
		var document_value: Variant = runtimes[object_id_value]
		if typeof(document_value) != TYPE_DICTIONARY:
			continue
		var document: Dictionary = document_value
		var contracts := _structure_contracts_with_passive_area_resolution(
			document, _sim.PlayableUnitAdapter.module_contracts(document)
		)
		if contracts.is_empty():
			continue
		register_structure_module_contracts(String(object_id_value), contracts)
		register_castle_upgrade_grants(String(document.get("objectId", object_id_value)), contracts)
		var slug := String(document.get("slug", ""))
		if slug != "":
			register_structure_module_contracts(slug, contracts)


# De-staticed on extraction (instance sim access).
func _structure_contracts_with_passive_area_resolution(
	document: Dictionary, contracts: Array
) -> Array:
	## Opaque moduleContracts preserve EffectRadius's authored define token;
	## playable_structure_compiler also emits the resolved numeric radius in its
	## dedicated passiveAreaEffect contract. Merge those two receipts before the
	## runtime indexes the module, without changing the underlying document.
	var output := contracts.duplicate(true)
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var gameplay: Dictionary = registration.get("gameplay", {}) as Dictionary
	var passive_value: Variant = gameplay.get("passiveAreaEffect")
	if typeof(passive_value) != TYPE_DICTIONARY:
		return output
	var passive := passive_value as Dictionary
	var index := -1
	for contract_index in output.size():
		if String((output[contract_index] as Dictionary).get("module", "")) == "PassiveAreaEffectBehavior":
			index = contract_index
			break
	if index < 0:
		output.append({
			"module": "PassiveAreaEffectBehavior",
			"fields": {},
			"runtime_status": "deferred",
			"source_ini": String(passive.get("sourceIni", "")),
			"line": int(passive.get("line", 0)),
			"tag": "",
			"executable": false,
		})
		index = output.size() - 1
	var contract := (output[index] as Dictionary).duplicate(true)
	var fields := (contract.get("fields", {}) as Dictionary).duplicate(true)
	var radius = float(passive.get("radius", 0.0))
	if radius > 0.0:
		fields["EffectRadius"] = {
			"authored": String(passive.get("radiusAuthored", radius)),
			"value": radius,
		}
	if (
		not fields.has("HealPercentPerSecond")
		and String(passive.get("healPercentPerSecondAuthored", "")) != ""
	):
		var heal_text := String(passive.get("healPercentPerSecondAuthored", ""))
		# The dedicated structure contract stores the numeric percent token
		# without its trailing sign in current packs ("2" for authored "2%").
		if heal_text.is_valid_float():
			heal_text += "%"
		fields["HealPercentPerSecond"] = {
			"authored": heal_text,
		}
	if String(passive.get("upgradeRequired", "")) != "":
		fields["UpgradeRequired"] = {"authored": String(passive.get("upgradeRequired", ""))}
	if typeof(passive.get("modifier")) == TYPE_DICTIONARY:
		fields["ResolvedModifier"] = (passive.get("modifier") as Dictionary).duplicate(true)
	contract["fields"] = fields
	output[index] = contract
	return output


func register_castle_upgrade_grants(source_object_id: String, contracts: Array) -> void:
	## Index every CastleUpgrade row (retail's trigger -> real upgrade hop) from
	## one structure's projected moduleContracts. Rows missing either half are
	## skipped: a half-recorded contract must never invent an upgrade id.
	var _sim = sim
	for contract_value in contracts:
		if typeof(contract_value) != TYPE_DICTIONARY:
			continue
		var contract: Dictionary = contract_value
		if String(contract.get("module", "")).to_lower() != "castleupgrade":
			continue
		var fields: Dictionary = contract.get("fields", {}) as Dictionary
		var trigger := _castle_upgrade_field(fields, "TriggeredBy")
		var granted := _castle_upgrade_field(fields, "Upgrade")
		if trigger == "" or granted == "":
			continue
		var radius_text := _castle_upgrade_field(fields, "WallUpgradeRadius")
		var rows: Array = _sim._castle_upgrade_grants.get(trigger.to_lower(), []) as Array
		var already_recorded := false
		for existing_value in rows:
			if String((existing_value as Dictionary).get("upgrade_id", "")) == granted:
				already_recorded = true
				break
		if already_recorded:
			continue
		rows.append({
			"upgrade_id": granted,
			"wall_upgrade_radius": radius_text,
			"source_object_id": source_object_id,
			"tag": String(contract.get("tag", "")),
		})
		_sim._castle_upgrade_grants[trigger.to_lower()] = rows


# De-staticed on extraction (instance sim access).
func _castle_upgrade_field(fields: Dictionary, key: String) -> String:
	## Opaque-authored moduleContracts record {authored, sourceIni, line} per
	## field; typed rows add "value". Either shape resolves to the authored id.
	var raw: Variant = fields.get(key, fields.get(key.to_lower(), null))
	if typeof(raw) == TYPE_DICTIONARY:
		var row := raw as Dictionary
		var authored := String(row.get("authored", ""))
		if authored != "":
			return authored.strip_edges()
		return String(row.get("value", "")).strip_edges()
	if typeof(raw) in [TYPE_STRING, TYPE_STRING_NAME]:
		return String(raw).strip_edges()
	return ""


func castle_upgrade_grants_for(trigger_upgrade_id: String) -> Array:
	## The real upgrade(s) a bought trigger hands out. Empty when the selected
	## packs record no CastleUpgrade module for that trigger.
	return (sim._castle_upgrade_grants.get(trigger_upgrade_id.to_lower(), []) as Array).duplicate(true)


func _apply_castle_upgrade_grants(building: Dictionary, trigger_upgrade_id: String) -> void:
	## Retail's CastleUpgrade pass-out: the fortress takes the real upgrade, and
	## so does every castle piece it owns (retail scopes wall improvements by
	## WallUpgradeRadius; the castle pieces ARE the fortress's own walls, so the
	## owning-fortress set is that radius exactly and needs no distance guess).
	var _sim = sim
	if bool(_sim.apply_castle_upgrade_trigger(int(building.get("id",0)),trigger_upgrade_id).get("ok",false)):
		return
	var grants := castle_upgrade_grants_for(trigger_upgrade_id)
	if grants.is_empty():
		return
	var structure_id = int(building.get("id", 0))
	var recipients: Array[int] = [structure_id]
	for piece_id_value in building.get("castle_piece_structure_ids", []) as Array:
		recipients.append(int(piece_id_value))
	for grant_value in grants:
		var granted := String((grant_value as Dictionary).get("upgrade_id", ""))
		if granted == "":
			continue
		for recipient_id in recipients:
			if not _sim.structures.has(recipient_id):
				continue
			var recipient: Dictionary = _sim.structures[recipient_id]
			var owned: Array = recipient.get("completed_upgrades", [])
			if owned.has(granted):
				continue
			owned.append(granted)
			recipient["completed_upgrades"] = owned
		_sim._emit_event("upgrade.castle_granted", structure_id, 0, {
			"team": int(building.get("team", -1)),
			"trigger_upgrade_id": trigger_upgrade_id,
			"upgrade_id": granted,
			"recipient_count": recipients.size(),
		})


func _content_db_ref():
	# ContentDB is a project autoload Node (not Engine.has_singleton).
	var tree := Engine.get_main_loop() as SceneTree
	if tree == null or tree.root == null:
		return null
	return tree.root.get_node_or_null("/root/ContentDB")


func _snapshot_scenario_runtime_tables() -> void:
	## Scenario registries are match inputs, not global producer tables. Snapshot
	## the validated ContentDB registries only when the caller did not inject an
	## explicit fixture. Empty registries add no rule keys, preserving legacy
	## hashes until a selected neutral pack actually supplies documents.
	var _sim = sim
	var explicit_tables := false
	for key in ["scenario_unit_runtimes", "scenario_structure_runtimes", "scenario_prop_runtimes", "scenario_pickup_runtimes"]:
		if _sim._rules.has(key):
			explicit_tables = true
			break
	# Merely installing/selecting a neutral pack is not match state. Direct sims
	# (including the owner pin) only inherit global registries when an active
	# scenario-map lane requests them; injected test/script tables remain valid.
	if not explicit_tables and not bool(_sim._rules.get("enable_scenario_map_placements", false)):
		return
	var game := String(_sim._rules.get("game", "")).to_lower()
	if game not in ["bfme2", "rotwk"]:
		_sim._rules["_scenario_registry_error"] = "scenario runtime selection requires game=bfme2 or game=rotwk"
		return
	var db = _content_db_ref()
	if db == null and not explicit_tables:
		_sim._rules["_scenario_registry_error"] = "scenario runtime selection has no ContentDB"
		return
	for key in ["scenario_unit_runtimes", "scenario_structure_runtimes", "scenario_prop_runtimes", "scenario_pickup_runtimes"]:
		if _sim._rules.has(key):
			continue
		var getter := "get_%s" % key
		if not db.has_method(getter):
			_sim._rules["_scenario_registry_error"] = "ContentDB missing edition-scoped %s" % getter
			return
		var value: Variant = db.call(getter, game)
		if typeof(value) == TYPE_DICTIONARY and not (value as Dictionary).is_empty():
			_sim._rules[key] = (value as Dictionary).duplicate(true)


func scenario_spawn_contract(object_id: String, surface: String) -> Dictionary:
	## One fail-closed lookup for map placement, scripts, OCL leaves and lair
	## payloads. The three registries stay disjoint from faction production and
	## HUD tables; an identity admitted by more than one kind is ambiguous and is
	## therefore refused instead of selected by registry order.
	var _sim = sim
	var matches: Array[Dictionary] = []
	for kind in ["unit", "structure", "prop", "pickup"]:
		var document := _scenario_document_for_kind(kind, object_id, surface)
		if not document.is_empty():
			matches.append({"kind": kind, "document": document})
	if matches.size() != 1:
		return {}
	var result = matches[0].duplicate(true)
	var document := result["document"] as Dictionary
	var registration := document.get("registration", {}) as Dictionary
	var presentation: Dictionary = {}
	if typeof(document.get("presentation")) == TYPE_DICTIONARY:
		presentation = (document.get("presentation") as Dictionary).duplicate(true)
	elif typeof(registration.get("presentation")) == TYPE_DICTIONARY:
		presentation = (registration.get("presentation") as Dictionary).duplicate(true)
	elif typeof(registration.get("visual")) == TYPE_DICTIONARY:
		presentation = (registration.get("visual") as Dictionary).duplicate(true)
	result["presentation"] = presentation
	if String(result["kind"]) == "unit":
		var source_rule = _sim.PlayableUnitAdapter.simulation_rule(document, false)
		if not source_rule.is_empty():
			result["unit_rule"] = _sim.PlayableUnitAdapter.normalized_unit_rule(
				source_rule, float(_sim._rules.get("source_map_transform_scale", 0.0))
			)
	return result


func scenario_unit_rule(object_id: String, surface: String) -> Dictionary:
	var contract := scenario_spawn_contract(object_id, surface)
	if String(contract.get("kind", "")) != "unit":
		return {}
	return (contract.get("unit_rule", {}) as Dictionary).duplicate(true)


func spawn_scenario_unit(
	object_id: String, team: int, at: Vector2, surface: String, requested_id: int = -1
) -> int:
	## Scenario units consume their descriptor-derived rule directly. The rule is
	## never inserted into unit_rules or production tables, so spawning one cannot
	## expose a construct button, roster row, or HUD command.
	var _sim = sim
	var contract := scenario_spawn_contract(object_id, surface)
	if String(contract.get("kind", "")) != "unit":
		return -1
	var rule := contract.get("unit_rule", {}) as Dictionary
	var document := contract.get("document", {}) as Dictionary
	if rule.is_empty() or (requested_id <= 0 and not _sim._next_dynamic_id.has(team)):
		return -1
	var entity_id := requested_id if requested_id > 0 else int(_sim._next_dynamic_id[team])
	if _sim.entities.has(entity_id) or _sim.structures.has(entity_id):
		return -1
	if requested_id <= 0:
		_sim._next_dynamic_id[team] = entity_id + 1
	_sim._add_battalion(
		entity_id, team, at, String(rule.get("display_name", object_id)),
		String(rule.get("object_id", object_id)), String(rule.get("horde_id", object_id)),
		0, rule
	)
	if not _sim.entities.has(entity_id):
		return -1
	var row := _sim.entities[entity_id] as Dictionary
	# Scenario-only units never enter the faction unit registry, but their exact
	# module contracts still have to reach the instance. Registering after the
	# body is constructed avoids exposing the object as production while letting
	# SlavedUpdate bind the authored lair master on the same spawn tick.
	var scenario_contracts = _sim.PlayableUnitAdapter.module_contracts(document)
	if not scenario_contracts.is_empty():
		var unit_type := String(row.get("unit_type", object_id))
		var registered := _sim._unit_module_contracts.get(unit_type, []) as Array
		if registered.is_empty():
			_sim._unit_module_contracts[unit_type] = scenario_contracts.duplicate(true)
		elif registered != scenario_contracts:
			_sim.entities.erase(entity_id)
			return -1
		_attach_module_contracts(row)
	row["scenario_source_object_id"] = String(document.get("objectId", object_id))
	row["scenario_spawn_surface"] = surface
	row["scenario_presentation"] = (contract.get("presentation", {}) as Dictionary).duplicate(true)
	return entity_id


func spawn_scenario_structure(
	object_id: String, team: int, at: Vector2, surface: String, requested_id: int = -1
) -> int:
	## Place one descriptor-backed neutral structure without registering a
	## faction kind, producer, command, or HUD row. `team` is placement-authored
	## ownership (-1 for unowned); this function never derives an owner from the
	## scenario role or from a player faction.
	var _sim = sim
	var contract := scenario_spawn_contract(object_id, surface)
	if String(contract.get("kind", "")) != "structure":
		return -1
	var document := contract.get("document", {}) as Dictionary
	var rule := _scenario_structure_instantiation_rule(document)
	if rule.is_empty():
		return -1
	var structure_id = requested_id if requested_id > 0 else _sim._next_dynamic_structure_id
	if _sim.structures.has(structure_id) or _sim.entities.has(structure_id):
		return -1
	if requested_id <= 0:
		_sim._next_dynamic_structure_id += 1
	var maximum_health := int(rule.get("maximum_health", 0))
	var row := {
		"id": structure_id,
		"team": team,
		"kind": "structure",
		"structure_kind": String(rule.get("structure_kind", "")),
		"name": String(document.get("objectId", object_id)),
		"source_object_id": String(document.get("objectId", object_id)),
		"object_id": String(document.get("objectId", object_id)),
		"scenario_source_object_id": String(document.get("objectId", object_id)),
		"scenario_game": String(document.get("game", _sim._rules.get("game", ""))).to_lower(),
		"position": at,
		"rally": at,
		"health": maximum_health,
		"maximum_health": maximum_health,
		"construction_progress": 1.0,
		"level": 1,
		"completed_upgrades": [],
		"upgrade_queue": [],
		"production": [],
		"queue": [],
		"damage_remainders": {},
		"income_per_payout": 0,
		"scenario_spawn_surface": surface,
		"scenario_admission_receipt": (rule.get("admission", {}) as Dictionary).duplicate(true),
		"scenario_lifecycle_receipt": (rule.get("lifecycle", {}) as Dictionary).duplicate(true),
		"scenario_presentation": (contract.get("presentation", {}) as Dictionary).duplicate(true),
		"scenario_gameplay": (rule.get("gameplay", {}) as Dictionary).duplicate(true),
	}
	var trained_sets := ((rule.get("gameplay", {}) as Dictionary).get("trainedCommandSets", []) as Array).duplicate(true)
	if not trained_sets.is_empty():
		row["scenario_trained_command_sets"] = trained_sets
		for set_value in trained_sets:
			if typeof(set_value) != TYPE_DICTIONARY:
				continue
			var command_set := set_value as Dictionary
			if String(command_set.get("kind", "")) == "direct":
				row["default_command_set_id"] = String(command_set.get("id", ""))
				row["command_set_id"] = String(command_set.get("id", ""))
				break
	var upgrade_effects_value: Variant = (rule.get("gameplay", {}) as Dictionary).get("upgradeEffects", {})
	if typeof(upgrade_effects_value) == TYPE_DICTIONARY:
		var accepted: Array[Dictionary] = []
		for effect_value in (upgrade_effects_value as Dictionary).get("effects", []) as Array:
			if typeof(effect_value) != TYPE_DICTIONARY:
				return -1
			var effect := effect_value as Dictionary
			if String(effect.get("kind", "")) != "command-set-transition":
				continue
			var normalized = _sim._normalized_command_set_upgrade_effect(effect)
			if normalized.is_empty() or String(normalized.get("game", "")) != String(row["scenario_game"]):
				return -1
			accepted.append(normalized)
		if not accepted.is_empty():
			row["scenario_command_set_upgrade_effects"] = accepted
	if team >= 0:
		row["scenario_authored_owner"] = team
	var bounty_value: Variant = rule.get("bounty_value")
	if typeof(bounty_value) == TYPE_INT:
		row["bounty_value"] = int(bounty_value)
	if rule.has("footprint_radius_source"):
		row["footprint_radius_source"] = float(rule.get("footprint_radius_source"))
	var module_contracts := rule.get("module_contracts", []) as Array
	if not module_contracts.is_empty():
		# The indexed table is derived from the same selected scenario document;
		# it enables the existing exact module consumers without adding the
		# structure to any faction manifest.
		register_structure_module_contracts(String(row["source_object_id"]), module_contracts)
	_sim._note_structure_table_mutation()
	_sim.structures[structure_id] = row
	_attach_structure_module_contracts(_sim.structures[structure_id] as Dictionary)
	if team >= 0 and team != _sim.CREEP_TEAM:
		_sim._apply_scenario_structure_faction_command_set(_sim.structures[structure_id] as Dictionary, team)
	return structure_id


func spawn_scenario_prop(object_id: String, at: Vector2, surface: String) -> int:
	## Passive props are deterministic world presentation records only. The
	## ContentDB contract admits IMMOBILE + INERT/OPTIMIZED_PROP objects and
	## rejects every combat/structure KindOf token, so no owner or body is made.
	var _sim = sim
	var contract := scenario_spawn_contract(object_id, surface)
	if String(contract.get("kind", "")) != "prop":
		return -1
	var document := contract.get("document", {}) as Dictionary
	if not _scenario_prop_is_passive(document):
		return -1
	var prop_id = _sim._next_scenario_prop_id
	_sim._next_scenario_prop_id += 1
	_sim.scenario_props[prop_id] = {
		"id": prop_id,
		"kind": "scenario-prop",
		"source_object_id": String(document.get("objectId", object_id)),
		"position": at,
		"scenario_spawn_surface": surface,
		"scenario_admission_receipt": (document.get("scenarioAdmission", {}) as Dictionary).duplicate(true),
		"scenario_presentation": (contract.get("presentation", {}) as Dictionary).duplicate(true),
		"geometry": document.get("geometry"),
		"geometry_contact_points": (document.get("geometryContactPoints", []) as Array).duplicate(true),
		"public_bones": (document.get("publicBones", []) as Array).duplicate(true),
		"kind_of": (document.get("kindOf", {}) as Dictionary).duplicate(true),
	}
	return prop_id


func launch_scenario_bezier_projectile(
	prop_id: int, target: Vector2, duration_ticks: int, bounce_duration_ticks: int = -1
) -> Dictionary:
	## Explicit activation boundary for authored projectile-capable props. The
	## caller owns flight duration; this lane owns only the sealed cubic envelope.
	## Arrival effects remain deferred and execute nothing here.
	var _sim = sim
	var registry_kind := ""
	var row: Dictionary = {}
	if _sim.scenario_props.has(prop_id):
		registry_kind = "prop"
		row = _sim.scenario_props[prop_id] as Dictionary
	elif _sim.entities.has(prop_id) and String((_sim.entities[prop_id] as Dictionary).get("scenario_source_object_id", "")) != "":
		registry_kind = "unit"
		row = _sim.entities[prop_id] as Dictionary
	else:
		return {"ok": false, "reason": "scenario-projectile-entity-missing"}
	if duration_ticks <= 0 or not is_finite(target.x) or not is_finite(target.y):
		return {"ok": false, "reason": "invalid-authored-flight"}
	if row.has("bezier_projectile"):
		return {"ok": false, "reason": "bezier-projectile-already-activated"}
	var object_id := String(row.get(
		"source_object_id", row.get("scenario_source_object_id", "")
	))
	var surface := String(row.get("scenario_spawn_surface", ""))
	var contract := scenario_spawn_contract(object_id, surface)
	if String(contract.get("kind", "")) != registry_kind:
		return {"ok": false, "reason": "bezier-contract-unavailable"}
	var document := contract.get("document", {}) as Dictionary
	var receipt = _sim.PlayableUnitAdapter.bezier_trajectory_contract(document)
	if receipt.is_empty():
		return {"ok": false, "reason": "bezier-contract-unavailable"}
	var trajectory := receipt.get("trajectory", {}) as Dictionary
	if String(trajectory.get("runtimeStatus", "")) != "executable":
		return {"ok": false, "reason": "bezier-contract-unavailable"}
	var arrival := receipt.get("arrival", {}) as Dictionary
	if (
		String(receipt.get("runtimeStatus", "")) == "executable"
		and int(arrival.get("bounceCount", 0)) > 0
		and bounce_duration_ticks <= 0
	):
		return {"ok": false, "reason": "authored-bounce-flight-duration-missing"}
	row["bezier_projectile"] = {
		"status": "airborne",
		"start_position": Vector2(row.get("position", Vector2.ZERO)),
		"target_position": target,
		"duration_ticks": duration_ticks,
		"elapsed_ticks": 0,
		"trajectory": trajectory.duplicate(true),
		"arrival": arrival.duplicate(true),
		"entity_kind": registry_kind,
		"bounce_duration_ticks": bounce_duration_ticks,
		"completed_bounces": 0,
		"presentation_requests": [],
		"deferred_blockers": (receipt.get("deferredBlockers", []) as Array).duplicate(true),
		"source_ini": String(receipt.get("sourceIni", "")),
		"line": int(receipt.get("line", 0)),
		"tag": String(receipt.get("tag", "")),
		"carrier": String(receipt.get("carrier", "")),
		"progress_authority": "external-authored-projectile-flight",
	}
	row["projectile_height_source"] = 0.0
	return {
		"ok": true,
		"propId": prop_id,
		"status": "airborne",
		"durationTicks": duration_ticks,
		"progressAuthority": "external-authored-projectile-flight",
		"sourceIni": String(receipt.get("sourceIni", "")),
		"line": int(receipt.get("line", 0)),
		"tag": String(receipt.get("tag", "")),
		"runtimeStatus": String(receipt.get("runtimeStatus", "deferred")),
	}


# De-staticed on extraction (instance sim access).
func sample_bezier_projectile_trajectory(
	trajectory: Dictionary, start: Vector2, target: Vector2, progress: float
) -> Vector3:
	## SAGE's two authored distance indents place cubic control points along the
	## ground segment; First/SecondHeight lift those points above it.
	if (
		String(trajectory.get("kind", "")) != "cubic-bezier-envelope"
		or String(trajectory.get("runtimeStatus", "")) != "executable"
		or String(trajectory.get("progressAuthority", "")) != "external-authored-projectile-flight"
	):
		return Vector3.INF
	for key in ["firstHeight", "secondHeight", "firstIndentRatio", "secondIndentRatio"]:
		if typeof(trajectory.get(key)) not in [TYPE_INT, TYPE_FLOAT] or not is_finite(float(trajectory.get(key))):
			return Vector3.INF
	var t := clampf(progress, 0.0, 1.0)
	var p0 := Vector3(start.x, 0.0, start.y)
	var p3 := Vector3(target.x, 0.0, target.y)
	var ground_delta := p3 - p0
	var p1 := p0 + ground_delta * float(trajectory.get("firstIndentRatio"))
	p1.y += float(trajectory.get("firstHeight"))
	var p2 := p0 + ground_delta * float(trajectory.get("secondIndentRatio"))
	p2.y += float(trajectory.get("secondHeight"))
	var inverse := 1.0 - t
	return (
		p0 * inverse * inverse * inverse
		+ p1 * 3.0 * inverse * inverse * t
		+ p2 * 3.0 * inverse * t * t
		+ p3 * t * t * t
	)


func _step_scenario_bezier_projectiles() -> void:
	var _sim = sim
	if _sim.scenario_props.is_empty() and _sim.entities.is_empty():
		return
	var carriers: Array[Dictionary] = []
	for prop_id in _sim.scenario_props.keys():
		carriers.append({"id": int(prop_id), "kind": "prop"})
	for entity_id in _sim.entities.keys():
		if typeof((_sim.entities[entity_id] as Dictionary).get("bezier_projectile")) == TYPE_DICTIONARY:
			carriers.append({"id": int(entity_id), "kind": "unit"})
	carriers.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		return int(a["id"]) < int(b["id"])
	)
	for carrier in carriers:
		var id_value := int(carrier["id"])
		var row := (
			_sim.scenario_props[id_value] as Dictionary
			if String(carrier["kind"]) == "prop"
			else _sim.entities[id_value] as Dictionary
		)
		var active_value: Variant = row.get("bezier_projectile")
		if typeof(active_value) != TYPE_DICTIONARY:
			continue
		var active := active_value as Dictionary
		if String(active.get("status", "")) != "airborne":
			continue
		var duration = int(active.get("duration_ticks", 0))
		if duration <= 0:
			active["status"] = "arrival-deferred"
			active["deferred_reason"] = "invalid-restored-authored-flight"
			continue
		var elapsed := mini(duration, int(active.get("elapsed_ticks", 0)) + 1)
		active["elapsed_ticks"] = elapsed
		var sampled := sample_bezier_projectile_trajectory(
			active.get("trajectory", {}) as Dictionary,
			Vector2(active.get("start_position", Vector2.ZERO)),
			Vector2(active.get("target_position", Vector2.ZERO)),
			float(elapsed) / float(duration)
		)
		if not sampled.is_finite():
			active["status"] = "arrival-deferred"
			active["deferred_reason"] = "trajectory-contract-invalid-after-restore"
			continue
		row["position"] = Vector2(sampled.x, sampled.z)
		row["projectile_height_source"] = sampled.y
		if elapsed == duration:
			active["arrival_tick"] = _sim.tick_index
			var arrival := active.get("arrival", {}) as Dictionary
			if String(arrival.get("runtimeStatus", "")) != "executable":
				active["status"] = "arrival-deferred"
				active["deferred_reason"] = "impact-semantics-not-oracle-accepted"
				continue
			var completed := int(active.get("completed_bounces", 0))
			var bounce_count := int(arrival.get("bounceCount", 0))
			var requests := active.get("presentation_requests", []) as Array
			if completed < bounce_count:
				requests.append({
					"kind": "bezier-impact-fx",
					"fxListId": String(arrival.get("groundBounceFxId", "")),
					"position": Vector2(row.get("position", Vector2.ZERO)),
					"tick": _sim.tick_index,
					"ordinal": requests.size(),
				})
				_sim.scenario_bezier_presentation_requests.append(
					(requests[requests.size() - 1] as Dictionary).duplicate(true)
				)
				var start := Vector2(row.get("position", Vector2.ZERO))
				var direction := Vector2(active.get("target_position", start)) - Vector2(active.get("start_position", start))
				if direction.is_zero_approx():
					active["status"] = "arrival-deferred"
					active["deferred_reason"] = "authored-bounce-direction-is-zero"
					continue
				active["start_position"] = start
				active["target_position"] = start + direction.normalized() * float(arrival.get("bounceDistance", 0.0))
				active["duration_ticks"] = int(active.get("bounce_duration_ticks", 0))
				active["elapsed_ticks"] = 0
				active["completed_bounces"] = completed + 1
				active["trajectory"] = {
					"kind": "cubic-bezier-envelope", "runtimeStatus": "executable",
					"firstHeight": float(arrival.get("bounceFirstHeight", 0.0)),
					"secondHeight": float(arrival.get("bounceSecondHeight", 0.0)),
					"firstIndentRatio": float(arrival.get("bounceFirstIndentRatio", 0.0)),
					"secondIndentRatio": float(arrival.get("bounceSecondIndentRatio", 0.0)),
					"progressAuthority": "external-authored-projectile-flight",
				}
				continue
			requests.append({
				"kind": "bezier-impact-fx",
				"fxListId": String(arrival.get("groundHitFxId", "")),
				"position": Vector2(row.get("position", Vector2.ZERO)),
				"tick": _sim.tick_index,
				"ordinal": requests.size(),
			})
			_sim.scenario_bezier_presentation_requests.append(
				(requests[requests.size() - 1] as Dictionary).duplicate(true)
			)
			active["status"] = "landed"
			active["terminal_policy"] = String(arrival.get("terminalPolicy", ""))
			if String(active["terminal_policy"]) == "remove-on-final-impact":
				if String(carrier["kind"]) == "prop":
					_sim.scenario_props.erase(id_value)
				else:
					_sim.delete_entity(id_value)


func spawn_scenario_object(
	object_id: String, team: int, at: Vector2, surface: String
) -> Dictionary:
	## Shared admission boundary for map placement, scenario scripts, OCL leaves,
	## and lair payloads. A caller receives an explicit kind/id receipt and never
	## has to guess which authoritative registry an admitted Object belongs to.
	var contract := scenario_spawn_contract(object_id, surface)
	match String(contract.get("kind", "")):
		"unit":
			var unit_id := spawn_scenario_unit(object_id, team, at, surface)
			return {"ok": unit_id > 0, "kind": "unit", "id": unit_id}
		"structure":
			var structure_id = spawn_scenario_structure(object_id, team, at, surface)
			return {"ok": structure_id > 0, "kind": "structure", "id": structure_id}
		"prop":
			var prop_id = spawn_scenario_prop(object_id, at, surface)
			return {"ok": prop_id > 0, "kind": "prop", "id": prop_id}
		"pickup":
			var pickup_id = spawn_scenario_pickup(object_id, at, surface)
			return {"ok": pickup_id > 0, "kind": "pickup", "id": pickup_id}
	return {"ok": false, "kind": "", "id": -1, "reason": "scenario-admission-rejected:%s:%s" % [object_id, surface]}


# De-staticed on extraction (instance sim access).
func _scenario_structure_instantiation_rule(document: Dictionary) -> Dictionary:
	var _sim = sim
	var registration_value: Variant = document.get("registration")
	if typeof(registration_value) != TYPE_DICTIONARY:
		return {}
	var registration := registration_value as Dictionary
	var gameplay_value: Variant = registration.get("gameplay")
	var presentation_value: Variant = registration.get("presentation")
	if typeof(gameplay_value) != TYPE_DICTIONARY or typeof(presentation_value) != TYPE_DICTIONARY:
		return {}
	var gameplay := gameplay_value as Dictionary
	var lifecycle_value: Variant = (presentation_value as Dictionary).get("buildingLifecycle")
	if typeof(lifecycle_value) != TYPE_DICTIONARY:
		return {}
	var lifecycle := lifecycle_value as Dictionary
	var facts_value: Variant = lifecycle.get("simulationFacts")
	var health_value: Variant = gameplay.get("health")
	if typeof(facts_value) != TYPE_DICTIONARY or typeof(health_value) != TYPE_DICTIONARY:
		return {}
	var maximum_value: Variant = (facts_value as Dictionary).get("maximumHealth")
	var primary_value: Variant = (health_value as Dictionary).get("primary")
	if typeof(maximum_value) not in [TYPE_INT, TYPE_FLOAT] or typeof(primary_value) != TYPE_DICTIONARY:
		return {}
	var max_health_value: Variant = (primary_value as Dictionary).get("maxHealth")
	if typeof(max_health_value) != TYPE_DICTIONARY:
		return {}
	var authored_maximum: Variant = (max_health_value as Dictionary).get("value")
	if (
		typeof(authored_maximum) not in [TYPE_INT, TYPE_FLOAT]
		or float(maximum_value) <= 0.0
		or not is_equal_approx(float(authored_maximum), float(maximum_value))
		or not is_equal_approx(float(maximum_value), float(roundi(float(maximum_value))))
	):
		return {}
	var admission := registration.get("scenarioAdmission", {}) as Dictionary
	var result = {
		"maximum_health": int(maximum_value),
		"role": String(admission.get("role", "")),
		"structure_kind": _sim._scenario_structure_kind(document),
		"admission": admission.duplicate(true),
		"lifecycle": lifecycle.duplicate(true),
		"gameplay": gameplay.duplicate(true),
		"module_contracts": _structure_contracts_with_passive_area_resolution(
			document, _sim.PlayableUnitAdapter.module_contracts(document)
		),
	}
	if String(result.get("structure_kind", "")) == "":
		return {}
	var geometry_value: Variant = gameplay.get("geometry", {})
	if typeof(geometry_value) == TYPE_DICTIONARY:
		var footprint_radius = _sim.SelectionPick.source_footprint_radius(geometry_value as Dictionary)
		if is_finite(footprint_radius) and footprint_radius > 0.0:
			result["footprint_radius_source"] = footprint_radius
	var scalar_fields := gameplay.get("scalarFields", {}) as Dictionary
	if scalar_fields.has("BountyValue"):
		var bounty_row: Variant = scalar_fields.get("BountyValue")
		if typeof(bounty_row) != TYPE_DICTIONARY:
			return {}
		var bounty: Variant = (bounty_row as Dictionary).get("value")
		if typeof(bounty) != TYPE_INT or int(bounty) < 0:
			return {}
		result["bounty_value"] = int(bounty)
	return result


# De-staticed on extraction (instance sim access).
func _scenario_prop_is_passive(document: Dictionary) -> bool:
	if document.get("production") != [] or typeof(document.get("moduleContracts")) != TYPE_ARRAY:
		return false
	var admission_value: Variant = document.get("scenarioAdmission")
	var kind_value: Variant = document.get("kindOf")
	if typeof(admission_value) != TYPE_DICTIONARY or typeof(kind_value) != TYPE_DICTIONARY:
		return false
	var admission := admission_value as Dictionary
	var effective_value: Variant = (kind_value as Dictionary).get("effective")
	if String(admission.get("kind", "")) != "authored-passive-prop" or typeof(effective_value) != TYPE_ARRAY:
		return false
	var effective := effective_value as Array
	if not effective.has("IMMOBILE") or (not effective.has("INERT") and not effective.has("OPTIMIZED_PROP")):
		return false
	for active in ["ARCHER", "CAVALRY", "CREEP", "GIANT", "HERO", "HORDE", "INFANTRY", "MACHINE", "MONSTER", "SHIP", "SIEGEENGINE", "STRUCTURE", "TRANSPORT", "TROLL"]:
		if effective.has(active):
			return false
	return true


func spawn_scenario_pickup(object_id: String, at: Vector2, surface: String) -> int:
	var _sim = sim
	var contract := scenario_spawn_contract(object_id, surface)
	if String(contract.get("kind", "")) != "pickup":
		return -1
	var document := contract.get("document", {}) as Dictionary
	if String(document.get("runtimeDomain", "")) != "active-pickup":
		return -1
	if not _salvage_oracle_receipt_valid(document.get("binaryOracleReceipt", {}) as Dictionary):
		return -1
	var pickup_value: Variant = document.get("pickupContract")
	if typeof(pickup_value) != TYPE_DICTIONARY:
		return -1
	var pickup_contract := pickup_value as Dictionary
	if String(pickup_contract.get("module", "")) != "SalvageCrateCollide" or String(pickup_contract.get("extraction", "")) != "typed":
		return -1
	var pickup_id = _sim._next_pickup_object_id
	_sim._next_pickup_object_id += 1
	_sim.pickup_objects[pickup_id] = {
		"id": pickup_id,
		"kind": "active-pickup",
		"object_id": String(document.get("objectId", object_id)),
		"source_object_id": String(document.get("objectId", object_id)),
		"position": at,
		"available": true,
		"scenario_spawn_surface": surface,
		"scenario_admission_receipt": (document.get("scenarioAdmission", {}) as Dictionary).duplicate(true),
		"scenario_presentation": (contract.get("presentation", {}) as Dictionary).duplicate(true),
		"geometry": document.get("geometry"),
		"kind_of": (document.get("kindOf", {}) as Dictionary).duplicate(true),
		"pickup_contract": pickup_contract.duplicate(true),
		"binary_oracle_receipt": (document.get("binaryOracleReceipt", {}) as Dictionary).duplicate(true),
	}
	return pickup_id


# De-staticed on extraction (instance sim access).
func _salvage_oracle_receipt_valid(receipt: Dictionary) -> bool:
	return (
		String(receipt.get("domain", "")) == "active-collision-pickup"
		and receipt.get("activeWhenAuthored", []) == ["AllowAIPickup", "LevelUpChance", "MaxResource", "MinResource", "Upgrade"]
		and receipt.get("deadBranchWhenAuthored", []) == ["LevelUpRadius"]
		and receipt.get("parsedIgnoredWhenAuthored", []) == ["BannerChance", "PorterChance", "ResourceChance"]
	)


func collect_salvage_crate(pickup_id: int, picker_id: int) -> Dictionary:
	## BFME2 1.06 game.dat (SHA-256 F008B5...56A7640), SalvageCrateCollide
	## 0x8BD314/0x8BD442. A refused pickup remains in the world and executes no
	## FX. A successful reward executes first, then FX, then consumes the crate.
	var _sim = sim
	if not _sim.pickup_objects.has(pickup_id):
		return {"ok": false, "reason": "pickup-missing"}
	if not _sim.entities.has(picker_id):
		return {"ok": false, "reason": "picker-missing"}
	var pickup := _sim.pickup_objects[pickup_id] as Dictionary
	if String(pickup.get("kind", "")) != "active-pickup":
		return {"ok": false, "reason": "not-active-pickup"}
	var picker := _sim.entities[picker_id] as Dictionary
	if int(picker.get("health", 0)) <= 0:
		return {"ok": false, "reason": "picker-defeated"}
	var contract := pickup.get("pickup_contract", {}) as Dictionary
	if String(contract.get("module", "")) != "SalvageCrateCollide" or String(contract.get("extraction", "")) != "typed":
		return {"ok": false, "reason": "typed-pickup-contract-missing"}
	var fields := contract.get("fields", {}) as Dictionary
	var picker_kind: Array = picker.get("kind_of", []) as Array
	for forbidden in _typed_contract_tokens(fields, "ForbiddenKindOf"):
		if picker_kind.has(forbidden):
			return {"ok": false, "reason": "picker-forbidden-kind-of:%s" % forbidden}
	var computer_controlled := bool(picker.get("computer_controlled", false)) or (
		picker.has("human_controlled") and not bool(picker.get("human_controlled", true))
	)
	if computer_controlled and not bool(_module_contract_value(fields, "AllowAIPickup", false)):
		return {"ok": false, "reason": "ai-pickup-disabled"}

	# The retail selector unconditionally consumes exactly one 0..1 logic draw,
	# then uses a strict comparison. Porter/Banner/ResourceChance are parsed but
	# have no references in the complete BFME2 1.06 class implementation.
	var level_field := fields.get("LevelUpChance", {}) as Dictionary
	var level_chance := float(level_field.get("ratio", float(level_field.get("percent", 0.0)) / 100.0))
	var roll = _sim.logic_random_real(0.0, 1.0)
	var reward := ""
	var amount := 0
	if roll < level_chance:
		_grant_one_authored_rank(picker)
		reward = "level"
	else:
		var upgrade_id := String(_module_contract_value(fields, "Upgrade", ""))
		if upgrade_id != "":
			var completed := picker.get("completed_upgrades", []) as Array
			if not completed.has(upgrade_id):
				completed.append(upgrade_id)
			picker["completed_upgrades"] = completed
			reward = "upgrade"
		else:
			var minimum := int(_module_contract_value(fields, "MinResource", 0))
			var maximum := int(_module_contract_value(fields, "MaxResource", minimum))
			amount = minimum if minimum == maximum else _sim.logic_random_int(minimum, maximum)
			if amount > 0:
				var team := int(picker.get("team", -1))
				_sim.team_resources[team] = _sim.resources_for_team(team) + amount
			reward = "resource"
	var execute_fx := String(_module_contract_value(fields, "ExecuteFX", ""))
	_sim._emit_event("pickup.salvage_collected", picker_id, pickup_id, {
		"reward": reward,
		"amount": amount,
		"roll": roll,
		"execute_fx": execute_fx,
	})
	_sim.pickup_objects.erase(pickup_id)
	return {"ok": true, "reason": "", "reward": reward, "amount": amount, "roll": roll, "execute_fx": execute_fx}


func _step_active_pickup_collisions() -> void:
	var _sim = sim
	var pickup_ids: Array[int] = []
	for value in _sim.pickup_objects.keys(): pickup_ids.append(int(value))
	pickup_ids.sort()
	for pickup_id in pickup_ids:
		if not _sim.pickup_objects.has(pickup_id): continue
		var pickup := _sim.pickup_objects[pickup_id] as Dictionary
		if String(pickup.get("kind", "")) != "active-pickup" or not bool(pickup.get("available", true)): continue
		var geometry := pickup.get("geometry", {}) as Dictionary;var footprint := geometry.get("footprint", {}) as Dictionary
		if typeof(footprint.get("radius")) not in [TYPE_INT, TYPE_FLOAT]: continue
		var crate_radius := float(footprint.get("radius", 0.0)) * float(_sim._rules.get("source_unit_scale", 0.1))
		if crate_radius <= 0.0: continue
		var origin := Vector2(pickup.get("position", Vector2.ZERO))
		for picker_id in _sim.entity_ids():
			var picker := _sim.entities[picker_id] as Dictionary
			if int(picker.get("health", 0)) <= 0: continue
			var collision_radius = crate_radius + _sim._target_footprint_radius(picker_id, "battalion")
			if origin.distance_to(Vector2(picker.get("position", Vector2.ZERO))) <= collision_radius:
				if bool(collect_salvage_crate(pickup_id, picker_id).get("ok", false)): break


func _grant_one_authored_rank(row: Dictionary) -> void:
	var _sim = sim
	var rule := _sim._unit_experience_rules.get(String(row.get("unit_type", "")), {}) as Dictionary
	if rule.is_empty() or int(row.get("health", 0)) <= 0:
		return
	var level := int(row.get("level", 1))
	for value in rule.get("levels", []) as Array:
		var next := value as Dictionary
		if int(next.get("rank", 0)) <= level:
			continue
		var needed := maxi(0, int(next.get("required_experience", 0)) - int(row.get("experience_xp", 0)))
		if needed > 0:
			_sim._award_experience(row, needed)
		return


func _scenario_document_for_kind(kind: String, object_id: String, surface: String) -> Dictionary:
	var registry_key := "scenario_%s_runtimes" % kind
	var registry_value: Variant = sim._rules.get(registry_key, {})
	if typeof(registry_value) != TYPE_DICTIONARY:
		return {}
	var document := _casefolded_scenario_document(registry_value as Dictionary, object_id)
	if document.is_empty():
		return {}
	return document if _scenario_document_admits(kind, document, surface) else {}


# De-staticed on extraction (instance sim access).
func _casefolded_scenario_document(registry: Dictionary, object_id: String) -> Dictionary:
	var folded := object_id.to_lower()
	for key_value in registry.keys():
		if String(key_value).to_lower() != folded or typeof(registry[key_value]) != TYPE_DICTIONARY:
			continue
		var candidate = registry[key_value] as Dictionary
		return candidate.duplicate(true) if String(candidate.get("objectId", "")).to_lower() == folded else {}
	return {}


func _scenario_runtime_tables_present() -> bool:
	for key in ["scenario_unit_runtimes", "scenario_structure_runtimes", "scenario_prop_runtimes", "scenario_pickup_runtimes"]:
		var value: Variant = sim._rules.get(key, {})
		if typeof(value) == TYPE_DICTIONARY and not (value as Dictionary).is_empty():
			return true
	return false


# De-staticed on extraction (instance sim access).
func _scenario_document_admits(kind: String, document: Dictionary, surface: String) -> bool:
	var allowed_surfaces: Array = []
	var allowed_roles: Array = []
	var admission: Dictionary = {}
	var production: Variant = null
	match kind:
		"unit":
			allowed_surfaces = ["map-placement", "script-spawn", "tutorial-script", "object-creation-list", "lair-spawn", "horde-payload"]
			allowed_roles = ["inheritance-template", "scenario-only", "creature", "horde", "summoned-hero"]
			var registration := document.get("registration", {}) as Dictionary
			admission = registration.get("scenarioAdmission", {}) as Dictionary
			production = registration.get("production", null)
			if typeof(production) != TYPE_ARRAY or not (production as Array).is_empty():
				return false
			if String(admission.get("kind", "")) != "authored-non-buildable" or String(admission.get("role", "")) not in allowed_roles:
				return false
		"structure":
			allowed_surfaces = ["map-placement", "script-spawn", "object-creation-list", "lair-spawn"]
			allowed_roles = ["lair", "neutral-structure"]
			var registration := document.get("registration", {}) as Dictionary
			admission = registration.get("scenarioAdmission", {}) as Dictionary
			production = registration.get("production", null)
			if typeof(production) != TYPE_DICTIONARY or String((production as Dictionary).get("evidence", "")) != "authored-neutral-map" or not ((production as Dictionary).get("routes", []) as Array).is_empty():
				return false
			if String(admission.get("kind", "")) != "authored-neutral-non-buildable" or String(admission.get("role", "")) not in allowed_roles:
				return false
		"prop":
			allowed_surfaces = ["map-placement", "script-spawn", "object-creation-list"]
			admission = document.get("scenarioAdmission", {}) as Dictionary
			production = document.get("production", null)
			if typeof(production) != TYPE_ARRAY or not (production as Array).is_empty() or String(admission.get("kind", "")) != "authored-passive-prop":
				return false
		"pickup":
			allowed_surfaces = ["object-creation-list"]
			admission = document.get("scenarioAdmission", {}) as Dictionary
			production = document.get("production", null)
			if (
				typeof(production) != TYPE_ARRAY or not (production as Array).is_empty()
				or String(document.get("runtimeDomain", "")) != "active-pickup"
				or String(admission.get("kind", "")) != "authored-ocl-pickup-leaf"
			):
				return false
		_:
			return false
	if surface not in allowed_surfaces or bool(admission.get("buildCommandExposed", true)):
		return false
	var surfaces_value: Variant = admission.get("surfaces", [])
	if typeof(surfaces_value) != TYPE_ARRAY:
		return false
	var surfaces := surfaces_value as Array
	var seen: Dictionary = {}
	for value in surfaces:
		var admitted_surface := String(value)
		if admitted_surface not in allowed_surfaces or seen.has(admitted_surface):
			return false
		seen[admitted_surface] = true
	return surfaces.has(surface)


func _attach_structure_module_contracts(row: Dictionary) -> void:
	## Attach structure moduleContracts for death consumers (CreateObjectDie).
	## Does NOT write into sim._unit_module_contracts (that table is unit-scoped).
	## Contracts-present rows carry the authoritative attached receipt, which
	## prevents duplicate object_creation_upgrades / passive_area_effect rows.
	## A no-contract lookup stays byte-inert: recording a derived "attempted"
	## memo on the structure moved the frozen state pin and also prevented a
	## later registry load from attaching the contract it had just supplied.
	var _sim = sim
	if bool(row.get("structure_module_contracts_attached", false)):
		return
	var keys: Array = [
		String(row.get("source_object_id", "")),
		String(row.get("object_id", "")),
		String(row.get("structure_kind", "")),
		String(row.get("kind", "")),
		String(row.get("building_type", "")),
	]
	var contracts: Array = []
	for key_value in keys:
		var key := String(key_value)
		if key != "" and _sim._structure_module_contracts.has(key):
			contracts = (_sim._structure_module_contracts[key] as Array).duplicate(true)
			break
	if contracts.is_empty():
		return
	row["module_contracts"] = contracts
	row["structure_module_contracts_attached"] = true
	for contract_value in contracts:
		if typeof(contract_value) != TYPE_DICTIONARY:
			continue
		var contract := contract_value as Dictionary
		var module_name := String(contract.get("module", ""))
		var fields: Dictionary = contract.get("fields", {}) as Dictionary
		var folded := module_name.to_lower()
		var executable := bool(contract.get("executable", false))
		if not executable and String(contract.get("runtime_status", "")) == "executable":
			executable = true
		if executable and folded.contains("createobjectdie"):
			var creation_list := ""
			var cl_raw: Variant = fields.get("CreationList", fields.get("creationList", {}))
			if typeof(cl_raw) == TYPE_DICTIONARY:
				creation_list = String((cl_raw as Dictionary).get("value", ""))
			elif typeof(cl_raw) == TYPE_STRING:
				creation_list = String(cl_raw)
			var death_types2 := String(fields.get("deathTypes", "ALL"))
			var excluded2: Array = []
			var excluded_raw2: Variant = fields.get("excludedDeathTypes", [])
			if typeof(excluded_raw2) == TYPE_ARRAY:
				for item2 in excluded_raw2 as Array:
					excluded2.append(String(item2).to_upper())
			var included2: Array = []
			var included_raw2: Variant = fields.get("includedDeathTypes", [])
			if typeof(included_raw2) == TYPE_ARRAY:
				for item3 in included_raw2 as Array:
					included2.append(String(item3).to_upper())
			if creation_list != "" and death_types2 in ["ALL", "NONE"]:
				row["create_object_die"] = true
				row["create_object_die_policy"] = {
					"creation_list": creation_list,
					"death_types": death_types2,
					"excluded_death_types": excluded2,
					"included_death_types": included2,
				}
		if executable and folded.contains("keepobjectdie"):
			var destroy_on_death := true
			if fields.has("destroyOnDeath"):
				destroy_on_death = bool(fields.get("destroyOnDeath", true))
			if not destroy_on_death:
				row["keep_object_die"] = true
				row["keep_object_die_policy"] = {
					"death_types": String(fields.get("deathTypes", "ALL")),
					"excluded_death_types": fields.get("excludedDeathTypes", []),
				}
		if folded == "hordetransportcontain":
			_sim._attach_horde_transport_contract(row, contract)
		elif folded in ["transportcontain", "tunnelcontain", "garrisoncontain", "hordegarrisoncontain"]:
			_sim._attach_container_family_contract(row, contract)
		elif folded == "productionqueuehordecontain":
			_sim._attach_container_family_contract(row, contract)
		elif folded == "siegeenginecontain":
			_sim._attach_siege_engine_contain_contract(row, contract)
		elif folded == "largegroupbonusupdate":
			_sim._attach_large_group_bonus_contract(row, contract)
		elif folded == "hitreactionbehavior":
			_sim._attach_hit_reaction_contract(row, contract)
		elif folded == "animalaiupdate":
			_sim._attach_animal_ai_contract(row, contract)
		elif folded == "threatfinderupdate":
			_sim._attach_threat_finder_contract(row, contract)
		elif folded == "radiatefearupdate":
			_sim._attach_radiate_fear_contract(row, contract)
		elif folded == "poisonedbehavior":
			_sim._attach_poisoned_contract(row, contract)
		elif folded == "damagefieldupdate":
			_sim._attach_damage_field_contract(row, contract)
		elif folded == "spawnunitbehavior":
			_sim._attach_spawn_unit_contract(row, contract)
		elif folded == "modelconditionsoundselectorclientbehavior":
			_sim._attach_model_condition_sound_selector(row, contract)
		elif folded == "randomsoundselectorclientbehavior":
			_sim._attach_random_sound_selector(row, contract)
		elif folded == "upgradesoundselectorclientbehavior":
			_sim._attach_upgrade_sound_selector(row, contract)
		elif folded == "largegroupaudioupdate":
			_sim._attach_large_group_audio_contract(row, contract)
		elif folded == "firespreadupdate":
			_sim._attach_fire_spread_contract(row, contract)
		elif folded == "shipslowdeathbehavior":
			_sim._attach_ship_slow_death_contract(row, contract)
		elif folded == "attributemodifierauraupdate":
			_sim._attach_attribute_modifier_aura_contract(row, contract)
		elif folded == "autohealbehavior":
			_attach_auto_heal_contract(row, contract)
		elif folded == "lifetimeupdate":
			_sim._attach_lifetime_update_contract(row, contract)
		elif folded == "stancesbehavior":
			_sim._attach_stances_contract(row, contract)
		elif folded == "aiupdateinterface":
			_attach_ai_update_contract(row, contract)
		elif folded == "hordeaiupdate":
			_attach_horde_ai_update_contract(row, contract)
		elif folded == "pickupstuffupdate":
			_sim._attach_pickup_stuff_update_contract(row, contract)
		elif folded == "autoabilitybehavior":
			_sim._attach_auto_ability_contract(row, contract)
		elif folded == "aispecialpowerupdate":
			_sim._attach_ai_special_power_contract(row, contract)
		elif folded == "weaponmodespecialpowerupdate":
			_sim._attach_weapon_mode_special_power_contract(row, contract)
		elif folded == "respawnupdate":
			_sim._attach_respawn_update_contract(row, contract)
		elif folded == "fireweaponupdate":
			_sim._attach_fire_weapon_update_contract(row, contract)
		elif folded == "deletionupdate":
			_sim._attach_deletion_update_contract(row, contract)
		elif folded == "productionupdate":
			_sim._attach_production_update_contract(row, contract)
		elif folded == "gettingbuiltbehavior":
			_sim._attach_getting_built_contract(row, contract)
		elif folded == "buildingbehavior":
			_sim._attach_building_behavior_contract(row, contract)
		elif folded == "queueproductionexitupdate":
			_sim._attach_queue_production_exit_contract(row, contract)
		elif folded == "rebuildholeexposeddie" or folded == "rebuildholeexposedie":
			_sim._attach_rebuild_hole_expose_contract(row, contract)
		elif folded == "rebuildholebehavior":
			_sim._attach_rebuild_hole_behavior_contract(row, contract)
		elif folded == "bannercarrierupdate":
			_sim._attach_banner_carrier_update_contract(row, contract)
		elif folded == "respawnbody":
			_sim._attach_respawn_body_contract(row, contract)
		elif folded == "giveupgradeupdate":
			_sim._attach_give_upgrade_contract(row, contract)
		elif folded == "gateopenandclosebehavior":
			_sim._attach_gate_open_close_contract(row, contract)
		elif folded == "aigateupdate":
			_sim._attach_ai_gate_contract(row, contract)
		elif folded == "fakepathfindportalbehaviour":
			_sim._attach_fake_pathfind_portal_contract(row, contract)
		elif folded == "stealthdetectorupdate":
			_sim._attach_stealth_detector_contract(row, contract)
		elif folded == "invisibilityupdate":
			_sim._attach_invisibility_update_contract(row, contract)
		elif folded == "slavedupdate":
			_sim._attach_slaved_update_contract(row, contract)
		elif folded == "castleupgrade":
			_sim._attach_castle_upgrade_contract(row, contract)
		elif folded == "spawnbehavior":
			_sim._attach_spawn_behavior_contract(row, contract)
		elif folded == "stealthupdate":
			_sim._attach_stealth_update_contract(row, contract)
		elif folded == "objectcreationupgrade":
			_sim._attach_object_creation_upgrade_contract(row, contract)
		elif folded == "attributemodifierupgrade":
			_sim._attach_attribute_modifier_upgrade_contract(row, contract)
		elif folded == "geometryupgrade":
			_sim._attach_geometry_upgrade_contract(row, contract)
		elif folded == "emotiontrackerupdate":
			_sim._attach_emotion_tracker_contract(row, contract)
		elif folded == "castlememberbehavior":
			_sim._attach_castle_member_contract(row, contract)
		elif folded == "inactivebody":
			_sim._attach_inactive_body_contract(row, contract)
		elif folded == "squishcollide":
			_sim._attach_squish_collide_contract(row, contract)
		elif folded == "hordemembercollide":
			_sim._attach_horde_member_collide_contract(row, contract)
		elif folded == "notifytargetsofimminentprobablecrushingupdate":
			_sim._attach_notify_crushing_contract(row, contract)
		elif folded == "flammableupdate":
			_sim._attach_flammable_update_contract(row, contract)
		elif folded == "dynamicportalbehaviour":
			_sim._attach_dynamic_portal_contract(row, contract)
		elif folded == "foundationaiupdate":
			_sim._attach_foundation_ai_contract(row, contract)
		elif folded == "monitorconditionupdate":
			_sim._attach_monitor_condition_contract(row, contract)
		elif folded == "refunddie":
			_sim._attach_refund_die_contract(row, contract)
		elif folded == "wallhubbehavior":
			_sim._attach_wall_hub_contract(row, contract)
		elif folded == "buildableherolistupgrade":
			_attach_buildable_hero_list_upgrade_contract(row, contract)
		elif folded == "allowbannerspawnupgrade":
			_attach_allow_banner_spawn_upgrade_contract(row, contract)
		elif folded == "spellrechargemodifierupgrade":
			_attach_spell_recharge_modifier_upgrade_contract(row, contract)
		elif folded == "replaceselfupgrade":
			_sim._attach_replace_self_contract(row, contract)
		elif folded == "citadelslaughterhordecontain":
			_sim._attach_citadel_slaughter_contract(row, contract)
		elif folded == "oclupdate":
			_sim._attach_ocl_update_contract(row, contract)
		elif folded == "hordecontain":
			_attach_horde_contain_contract(row, contract)
		elif folded == "stopspecialpower":
			_sim._attach_stop_special_power_contract(row, contract)
		elif folded == "unleashspecialpower":
			_sim._attach_unleash_special_power_contract(row, contract)
		elif folded == "specialenemysenseupdate":
			_sim._attach_special_enemy_sense_contract(row, contract)
		if folded == "fireweaponwhendeadbehavior":
			_sim._attach_fire_weapon_when_dead_contract(row, contract)
		# PassiveAreaEffectBehavior heal variant. The converter currently labels
		# the generic module contract deferred because its ModifierName leadership
		# variant still needs the ModifierList consumer. Healing is independent and
		# fully authored by these fields, so consume only that closed subset here.
		if folded == "passiveareaeffectbehavior":
			var heal_percent = _sim._passive_area_effect_percent(
				_passive_area_effect_field(fields, "HealPercentPerSecond")
			)
			var radius = _sim._passive_area_effect_number(fields, "EffectRadius")
			var ping_ms = _sim._passive_area_effect_number(fields, "PingDelay")
			if heal_percent > 0.0 and radius > 0.0 and ping_ms > 0.0:
				var rows: Array = row.get("passive_area_effect_heals", []) as Array
				rows.append({
					"radius_source": radius,
					"ping_ticks": maxi(1, roundi(ping_ms / (_sim.TICK_SECONDS * 1000.0))),
					"heal_fraction_per_second": heal_percent,
					"allow_filter": _passive_area_effect_field(fields, "AllowFilter"),
					"upgrade_required": _passive_area_effect_field(fields, "UpgradeRequired"),
					"non_stackable": _sim._passive_area_effect_yes(fields, "NonStackable"),
					"tag": String(contract.get("tag", "")),
					"source_ini": String(contract.get("source_ini", contract.get("sourceIni", ""))),
					# Lazy attachment occurs inside the current tick after sim.tick_index
					# advances. The structure existed for this tick interval already,
					# so cadence is measured from the preceding boundary.
					"next_ping_tick": _sim.tick_index - 1 + maxi(1, roundi(ping_ms / (_sim.TICK_SECONDS * 1000.0))),
				})
				row["passive_area_effect_heals"] = rows
			var modifier_value: Variant = fields.get("ResolvedModifier")
			if typeof(modifier_value) == TYPE_DICTIONARY and radius > 0.0:
				var modifier := modifier_value as Dictionary
				var effects: Array = modifier.get("effects", []) as Array
				var duration_ms = float(modifier.get("durationMs", 0.0))
				var modifier_ping_ms = ping_ms if ping_ms > 0.0 else duration_ms
				if not effects.is_empty() and duration_ms > 0.0 and modifier_ping_ms > 0.0:
					var modifier_rows: Array = row.get("passive_area_effect_modifiers", []) as Array
					modifier_rows.append({
						"id": String(modifier.get("id", "")),
						"category": String(modifier.get("category", "")),
						"effects": effects.duplicate(true),
						"duration_ticks": maxi(1, roundi(duration_ms / (_sim.TICK_SECONDS * 1000.0))),
						"ping_ticks": maxi(1, roundi(modifier_ping_ms / (_sim.TICK_SECONDS * 1000.0))),
						"radius_source": radius,
						"allow_filter": _passive_area_effect_field(fields, "AllowFilter"),
						"upgrade_required": _passive_area_effect_field(fields, "UpgradeRequired"),
						"non_stackable": _sim._passive_area_effect_yes(fields, "NonStackable"),
						"stacking": (modifier.get("stacking", {}) as Dictionary).duplicate(true),
						"next_ping_tick": _sim.tick_index,
					})
					row["passive_area_effect_modifiers"] = modifier_rows


# De-staticed on extraction (instance sim access).
func _passive_area_effect_field(fields: Dictionary, key: String) -> String:
	var raw: Variant = fields.get(key, fields.get(key.to_lower(), null))
	if typeof(raw) == TYPE_DICTIONARY:
		var authored := String((raw as Dictionary).get("authored", ""))
		if authored != "":
			return authored.strip_edges()
		return String((raw as Dictionary).get("value", "")).strip_edges()
	if typeof(raw) in [TYPE_STRING, TYPE_STRING_NAME, TYPE_INT, TYPE_FLOAT]:
		return String(raw).strip_edges()
	return ""


# De-staticed on extraction (instance sim access).
func _module_contract_value(fields: Dictionary, key: String, fallback: Variant = null) -> Variant:
	var raw: Variant = fields.get(key, fields.get(key.to_lower(), fallback))
	if typeof(raw) == TYPE_DICTIONARY:
		var row := raw as Dictionary
		if row.has("value"):
			return row.get("value")
		if row.has("milliseconds"):
			return row.get("milliseconds")
		return row.get("authored", fallback)
	return raw


# De-staticed on extraction (instance sim access).
func _typed_contract_tokens(fields: Dictionary, key: String) -> Array[String]:
	var value: Variant = _module_contract_value(fields, key, [])
	var output: Array[String] = []
	if typeof(value) != TYPE_ARRAY:
		return output
	for token_value in value as Array:
		var token := String(token_value).strip_edges().to_upper()
		if token != "":
			output.append(token)
	return output


# De-staticed on extraction (instance sim access).
func _typed_contract_raw_tokens(fields: Dictionary, key: String) -> Array[String]:
	## Geometry/subobject identifiers are case-insensitive at lookup but their
	## authored spelling is evidence and is preserved in authoritative receipts.
	var value: Variant = _module_contract_value(fields, key, [])
	var output: Array[String] = []
	if typeof(value) != TYPE_ARRAY:
		return output
	for token_value in value as Array:
		var token := String(token_value).strip_edges()
		if token != "":
			output.append(token)
	return output


func _attach_buildable_hero_list_upgrade_contract(row: Dictionary, contract: Dictionary) -> void:
	## This module is authored on GoodSpellBook/EvilSpellBook system objects, not
	## on a world producer. Preserve its exact trigger contract, but do not pretend
	## a local structure row is the retail spellbook object. Ring-hero production
	## remains gated by the separately compiled BuildableRingHeroesMP prerequisites.
	if String(contract.get("extraction", "")) != "typed" or row.has("buildable_hero_list_upgrade"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var triggers := _typed_contract_raw_tokens(fields, "TriggeredBy")
	if triggers.is_empty():
		return
	row["buildable_hero_list_upgrade"] = {
		"triggered_by": triggers,
		"unsupported_semantics": ["system-spellbook-object-not-instantiated"],
		"tag": String(contract.get("tag", "")),
		"source_ini": String(contract.get("sourceIni", contract.get("source_ini", ""))),
		"line": int(contract.get("line", 0)),
	}


func _attach_allow_banner_spawn_upgrade_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("allow_banner_spawn_upgrade"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var triggers := _typed_contract_raw_tokens(fields, "TriggeredBy")
	if triggers.is_empty():
		return
	row["allow_banner_spawn_upgrade"] = {
		"triggered_by": triggers,
		"tag": String(contract.get("tag", "")),
		"source_ini": String(contract.get("sourceIni", contract.get("source_ini", ""))),
		"line": int(contract.get("line", 0)),
	}


func structure_allows_banner_spawn(structure_id: int) -> bool:
	var _sim = sim
	if not _sim.structures.has(structure_id):
		return false
	var structure := _sim.structures[structure_id] as Dictionary
	if not structure.has("allow_banner_spawn_upgrade") and not bool(structure.get("structure_module_contracts_attached", false)):
		_attach_structure_module_contracts(structure)
	var policy := structure.get("allow_banner_spawn_upgrade", {}) as Dictionary
	if policy.is_empty():
		# No AllowBannerSpawnUpgrade means this module does not restrict the
		# ordinary horde-level BannerCarriersAllowed path.
		return true
	for upgrade_value in policy.get("triggered_by", []) as Array:
		if _sim._structure_has_completed_upgrade(structure, String(upgrade_value)):
			return true
	return false


func _attach_spell_recharge_modifier_upgrade_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("spell_recharge_modifier_upgrade"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var percentage_value: Variant = fields.get("Percentage", null)
	if typeof(percentage_value) != TYPE_ARRAY or (percentage_value as Array).is_empty():
		return
	var percentages: Array[int] = []
	for value in percentage_value as Array:
		if typeof(value) != TYPE_DICTIONARY or not (value as Dictionary).has("value"):
			return
		var percentage := int((value as Dictionary).get("value", 0))
		# A recharge multiplier at or below zero has no deterministic timer. The
		# typed importer accepts signed rows, so runtime alone must fail closed.
		if percentage <= -100:
			return
		percentages.append(percentage)
	var receipts: Array[String] = ["in-flight-cooldown-rescale-unresolved"]
	var label := String(_module_contract_value(fields, "LabelForPalantirString", "")).strip_edges()
	if label != "":
		receipts.append("presentation-label:%s" % label)
	row["spell_recharge_modifier_upgrade"] = {
		"percentages": percentages,
		"starts_active": bool(_module_contract_value(fields, "StartsActive", false)),
		"active": bool(_module_contract_value(fields, "StartsActive", false)),
		"unsupported_semantics": receipts,
		"tag": String(contract.get("tag", "")),
		"source_ini": String(contract.get("sourceIni", contract.get("source_ini", ""))),
		"line": int(contract.get("line", 0)),
	}


func spell_recharge_ticks_for_team(team: int, base_ticks: int) -> int:
	## Retail Percentage rows are ordered by the number of controlled Signal
	## Fires: first owned instance selects row one, the second row two, and so on;
	## counts beyond the authored table keep the final row.
	var _sim = sim
	var active_policies: Array[Dictionary] = []
	for structure_id in _sim.structure_ids(team):
		var structure := _sim.structures[structure_id] as Dictionary
		if int(structure.get("health", 0)) <= 0:
			continue
		if not structure.has("spell_recharge_modifier_upgrade") and not bool(structure.get("structure_module_contracts_attached", false)):
			_attach_structure_module_contracts(structure)
		var policy := structure.get("spell_recharge_modifier_upgrade", {}) as Dictionary
		if not policy.is_empty() and bool(policy.get("active", false)):
			active_policies.append(policy)
	if active_policies.is_empty():
		return maxi(1, base_ticks)
	var percentages := (active_policies[0] as Dictionary).get("percentages", []) as Array
	if percentages.is_empty():
		return maxi(1, base_ticks)
	var percentage := int(percentages[mini(active_policies.size(), percentages.size()) - 1])
	return maxi(1, roundi(float(base_ticks) * (100.0 + float(percentage)) / 100.0))


# De-staticed on extraction (instance sim access).
func _ship_contract_delay_ticks(milliseconds: float) -> int:
	return maxi(0, ceili(milliseconds / (sim.TICK_SECONDS * 1000.0)))


func _attach_horde_contain_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("horde_contain"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var slots := int(_module_contract_value(fields, "Slots", 0))
	if slots < 0:
		return
	var unsupported: Array[String] = []
	for key in ["ShowPips", "BannerCarrierPosition", "RankInfo", "RandomOffset"]:
		if fields.has(key):
			unsupported.append("model_or_hud_binding:%s" % key)
	# These fields are preserved by the typed contract, but this bounded consumer
	# has no authoritative movement/formation or Living World owner for them.
	# Receipt them instead of silently treating stored data as executed parity.
	for key in [
		"FrontAngle", "FlankedDelay", "ThisFormationIsTheMainFormation",
		"RanksToReleaseWhenAttacking", "RanksToJustFreeWhenAttacking",
		"AttributeModifiers", "IsPorcupineFormation", "MinimumHordeSize",
		"AlternateFormation", "VisionRearOverride", "VisionSideOverride",
		"NotComboFormation", "BannerCarriersAllowed", "MeleeAttackLeashDistance",
		"BackUpMinDelayTime", "BackUpMaxDelayTime", "BackUpMinDistance",
		"BackUpMaxDistance", "BackupPercentage", "RankSplit", "SplitHordeNumber",
		"SplitHorde", "UseSlowHordeMovement", "BannerCarrierMinLevel",
		"LivingWorldOverloadTemplate",
	]:
		if fields.has(key):
			unsupported.append("unsupported_horde_field:%s" % key)
	var payloads: Array = fields.get("InitialPayload", []) as Array
	var payload_counts := sim._rules.get("horde_payload_counts", {}) as Dictionary
	var contained_statuses := _typed_contract_tokens(fields, "ObjectStatusOfContained")
	var members: Array[Dictionary] = []
	for payload_value in payloads:
		var payload := payload_value as Dictionary
		var count_result := _resolve_horde_payload_count(payload, payload_counts)
		if not bool(count_result.get("ok", false)):
			unsupported.append("unresolved_payload_count:%s" % String(payload.get("countExpression", "")))
			continue
		var count := int(count_result.get("count", 0))
		for _member_index in maxi(0, count):
			var object_status := {}
			for status_value in contained_statuses:
				object_status[String(status_value)] = true
			members.append({"object_id": String(payload.get("objectId", "")), "rank": 0, "health": 1, "status": "contained", "object_status": object_status})
	if members.size() > slots:
		unsupported.append("initial_payload_exceeds_slots")
		members.resize(slots)
	row["horde_contain"] = {
		"slots": slots,
		"passenger_filter": _typed_contract_tokens(fields, "PassengerFilter"),
		"contained_statuses": contained_statuses,
		"front_angle": float(_module_contract_value(fields, "FrontAngle", 0.0)),
		"flanked_delay_ticks": _ship_contract_delay_ticks(float(_module_contract_value(fields, "FlankedDelay", 0.0))),
		"main_formation": bool(_module_contract_value(fields, "ThisFormationIsTheMainFormation", false)),
		"porcupine": bool(_module_contract_value(fields, "IsPorcupineFormation", false)),
		"minimum_horde_size": int(_module_contract_value(fields, "MinimumHordeSize", 0)),
		"ranks_release_attacking": _typed_contract_tokens(fields, "RanksToReleaseWhenAttacking"),
		"ranks_free_attacking": _typed_contract_tokens(fields, "RanksToJustFreeWhenAttacking"),
		"attribute_modifiers": _typed_contract_tokens(fields, "AttributeModifiers"),
		"melee_behavior": String(_module_contract_value(fields, "MeleeBehavior", "")),
		"facing_bonus": float(_module_contract_value(fields, "FacingBonus", 0.0)),
		"angle_limit_cos": float(_module_contract_value(fields, "AngleLimitCos", -1.0)),
		"inner_range": float(_module_contract_value(fields, "InnerRange", 0.0)),
		"outer_range": float(_module_contract_value(fields, "OuterRange", 0.0)),
		"outer_range_buildings": float(_module_contract_value(fields, "OuterRangeBuildings", 0.0)),
		"melee_attack_leash_source": float(_module_contract_value(fields, "MeleeAttackLeashDistance", 0.0)),
		"banner_destroy_horde_on_death": bool(_module_contract_value(fields, "BannerCarrierDestroyHordeOnDeath", false)),
		"banner_death_types": _typed_contract_tokens(fields, "BannerCarrierHordeDeathType"),
		"unsupported_semantics": unsupported,
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}
	row["horde_contained_members"] = members


# De-staticed on extraction (instance sim access).
func _resolve_horde_payload_count(payload: Dictionary, defines: Dictionary) -> Dictionary:
	var compiled: Variant = payload.get("count")
	if typeof(compiled) == TYPE_DICTIONARY:
		var count_contract := compiled as Dictionary
		match String(count_contract.get("kind", "")):
			"literal":
				return {"ok": true, "count": maxi(0, int(count_contract.get("value", 0)))}
			"define":
				var name := String(count_contract.get("name", ""))
				if typeof(defines.get(name)) == TYPE_INT:
					return {"ok": true, "count": maxi(0, int(defines[name]))}
			"multiply":
				var name := String(count_contract.get("name", ""))
				if typeof(defines.get(name)) == TYPE_INT:
					return {"ok": true, "count": maxi(0, int(defines[name]) * int(count_contract.get("factor", 0)))}
	var expression := String(payload.get("countExpression", ""))
	if expression == "":
		return {"ok": true, "count": 1}
	if expression.is_valid_int():
		return {"ok": true, "count": maxi(0, int(expression))}
	if typeof(defines.get(expression)) == TYPE_INT:
		return {"ok": true, "count": maxi(0, int(defines[expression]))}
	return {"ok": false, "count": 0}


func admit_horde_member(horde_id: int, object_id: String, kind_of: Array, rank: int = 0, health: int = 1) -> Dictionary:
	var _sim = sim
	if not _sim.entities.has(horde_id):
		return {"ok": false, "reason": "horde-missing"}
	var row := _sim.entities[horde_id] as Dictionary
	if not row.has("horde_contain"):
		_attach_module_contracts(row)
	if not row.has("horde_contain"):
		return {"ok": false, "reason": "typed-horde-contain-contract-missing"}
	var policy := row["horde_contain"] as Dictionary
	var members := row.get("horde_contained_members", []) as Array
	var slots := int(policy.get("slots", 0))
	if slots <= 0:
		return {"ok": false, "reason": "capacity-zero"}
	if members.size() >= slots:
		return {"ok": false, "reason": "capacity-full"}
	var probe := {"category": String(kind_of[0] if not kind_of.is_empty() else ""), "kind_of": kind_of}
	if not _sim._transport_filter_accepts(probe, policy.get("passenger_filter", []) as Array):
		return {"ok": false, "reason": "passenger-filter-refused"}
	var member := {"object_id": object_id, "kind_of": kind_of.duplicate(), "rank": rank, "health": maxi(1, health), "status": "contained", "object_status": {}}
	for status_value in policy.get("contained_statuses", []) as Array:
		(member["object_status"] as Dictionary)[String(status_value)] = true
	members.append(member)
	row["horde_contained_members"] = members
	return {"ok": true, "reason": "", "index": members.size() - 1}


func eject_horde_member(horde_id: int, member_index: int) -> Dictionary:
	var _sim = sim
	if not _sim.entities.has(horde_id):
		return {"ok": false, "reason": "horde-missing"}
	var row := _sim.entities[horde_id] as Dictionary
	var members := row.get("horde_contained_members", []) as Array
	if member_index < 0 or member_index >= members.size():
		return {"ok": false, "reason": "member-index-invalid"}
	var member := (members[member_index] as Dictionary).duplicate(true)
	member["status"] = "ejected"
	member["object_status"] = {}
	members.remove_at(member_index)
	row["horde_contained_members"] = members
	return {"ok": true, "reason": "", "member": member}


func apply_horde_contained_damage(horde_id: int, member_index: int, amount: int, death_type: String = "NORMAL") -> Dictionary:
	var _sim = sim
	if not _sim.entities.has(horde_id):
		return {"ok": false, "reason": "horde-missing"}
	var row := _sim.entities[horde_id] as Dictionary
	var members := row.get("horde_contained_members", []) as Array
	if member_index < 0 or member_index >= members.size():
		return {"ok": false, "reason": "member-index-invalid"}
	var member := members[member_index] as Dictionary
	member["health"] = maxi(0, int(member.get("health", 1)) - maxi(0, amount))
	if int(member["health"]) > 0:
		return {"ok": true, "reason": "", "killed": false}
	var policy := row.get("horde_contain", {}) as Dictionary
	var banner := (member.get("kind_of", []) as Array).has("BANNER")
	var kills_horde := banner and bool(policy.get("banner_destroy_horde_on_death", false))
	if kills_horde:
		var accepted := policy.get("banner_death_types", []) as Array
		kills_horde = accepted.is_empty() or accepted.has(death_type.to_upper())
	if kills_horde:
		_sim._expire_lifetime_entity(horde_id, row, death_type)
	members.remove_at(member_index)
	row["horde_contained_members"] = members
	return {"ok": true, "reason": "", "killed": true, "horde_killed": kills_horde}


func horde_amoeba_melee_reach(horde_id: int, target_position: Vector2, target_is_building: bool = false) -> Dictionary:
	var _sim = sim
	if not _sim.entities.has(horde_id):
		return {"ok": false, "reason": "horde-missing"}
	var row := _sim.entities[horde_id] as Dictionary
	var policy := row.get("horde_contain", {}) as Dictionary
	if String(policy.get("melee_behavior", "")).to_lower() != "amoeba":
		return {"ok": false, "reason": "amoeba-not-authored"}
	var scale := float(_sim._rules.get("source_map_transform_scale", 1.0))
	var outer := float(policy.get("outer_range_buildings" if target_is_building else "outer_range", 0.0)) * (scale if scale > 0.0 else 1.0)
	var distance := Vector2(row.get("position", Vector2.ZERO)).distance_to(target_position)
	return {"ok": true, "reason": "", "in_range": distance >= float(policy.get("inner_range", 0.0)) * scale and distance <= outer, "distance": distance, "outer_range": outer}


func _attach_ai_update_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("ai_update_interface"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var acquire_value: Variant = fields.get("AutoAcquireEnemiesWhenIdle")
	var enabled := false
	var flags: Array = []
	if typeof(acquire_value) == TYPE_DICTIONARY:
		enabled = bool((acquire_value as Dictionary).get("enabled", false))
		flags = ((acquire_value as Dictionary).get("flags", []) as Array).duplicate()
	var unsupported: Array[String] = []
	for unsupported_key in ["AILuaEventsList", "AttackPriority", "FadeOnPortals", "MinCowerTime", "MaxCowerTime", "RampageTime", "TimeToEjectPassengersOnRampage", "BurningDeathTime", "RampageRequiresAflame", "SpecialContactPoints"]:
		if fields.has(unsupported_key):
			unsupported.append("unsupported_ai_field:%s" % unsupported_key)
	if fields.has("Turrets"):
		unsupported.append("turret_weapon_slot_aim_requires_weapon_mount_runtime")
	var mood_ms := float(_module_contract_value(fields, "MoodAttackCheckRate", 0.0))
	row["auto_acquire_enabled"] = enabled
	row["auto_acquire_attack_buildings"] = flags.has("ATTACK_BUILDINGS")
	row["auto_acquire_while_stealthed"] = flags.has("STEALTHED")
	if mood_ms > 0.0:
		row["mood_attack_check_rate_ticks"] = maxi(1, _ship_contract_delay_ticks(mood_ms))
		row["mood_randomize_next_check"] = true
	row["ai_update_interface"] = {
		"can_attack_while_contained": bool(_module_contract_value(fields, "CanAttackWhileContained", false)),
		"hold_ground_close_range_source": float(_module_contract_value(fields, "HoldGroundCloseRangeDistance", 0.0)),
		"stop_chase_distance_source": float(_module_contract_value(fields, "StopChaseDistance", 0.0)),
		"unsupported_semantics": unsupported,
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}


func _attach_horde_ai_update_contract(row: Dictionary, contract: Dictionary) -> void:
	## HordeAIUpdate owns the same authored idle-acquisition cadence as the
	## general AI interface, plus horde cower timing and contained-fire policy.
	## Typed-only attachment keeps legacy opaque authored text fail-closed.
	if String(contract.get("extraction", "")) != "typed" or row.has("horde_ai_update"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var acquire_value: Variant = fields.get("AutoAcquireEnemiesWhenIdle")
	var enabled := false
	var flags: Array = []
	if typeof(acquire_value) == TYPE_DICTIONARY:
		enabled = bool((acquire_value as Dictionary).get("enabled", false))
		flags = ((acquire_value as Dictionary).get("flags", []) as Array).duplicate()
	row["auto_acquire_enabled"] = enabled
	row["auto_acquire_attack_buildings"] = flags.has("ATTACK_BUILDINGS")
	row["auto_acquire_while_stealthed"] = flags.has("STEALTHED")
	var mood_ms := float(_module_contract_value(fields, "MoodAttackCheckRate", 0.0))
	if mood_ms > 0.0:
		row["mood_attack_check_rate_ticks"] = maxi(1, _ship_contract_delay_ticks(mood_ms))
		row["mood_randomize_next_check"] = true
	var unsupported: Array[String] = []
	if fields.has("AILuaEventsList"):
		unsupported.append("script_hook_binding:AILuaEventsList")
	if fields.has("AttackPriority"):
		unsupported.append("target_classification_binding:AttackPriority")
	var lua_rows: Array = fields.get("AILuaEventsList", []) as Array
	var lua_lists: Array[String] = []
	for lua_value in lua_rows:
		if typeof(lua_value) == TYPE_DICTIONARY:
			lua_lists.append(String((lua_value as Dictionary).get("value", "")))
	row["horde_ai_update"] = {
		"can_attack_while_contained": bool(_module_contract_value(fields, "CanAttackWhileContained", false)),
		"minimum_cower_ticks": _ship_contract_delay_ticks(float(_module_contract_value(fields, "MinCowerTime", 0.0))),
		"maximum_cower_ticks": _ship_contract_delay_ticks(float(_module_contract_value(fields, "MaxCowerTime", 0.0))),
		"attack_priority": String(_module_contract_value(fields, "AttackPriority", "")),
		"ai_lua_event_lists": lua_lists,
		"unsupported_semantics": unsupported,
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}


func trigger_horde_cower(entity_id: int) -> Dictionary:
	var _sim = sim
	if not _sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity-missing"}
	var row := _sim.entities[entity_id] as Dictionary
	if not row.has("horde_ai_update"):
		_attach_module_contracts(row)
	if not row.has("horde_ai_update"):
		return {"ok": false, "reason": "typed-horde-ai-contract-missing"}
	var policy := row["horde_ai_update"] as Dictionary
	var low := maxi(0, int(policy.get("minimum_cower_ticks", 0)))
	var high := maxi(low, int(policy.get("maximum_cower_ticks", low)))
	var duration = low if low == high else _sim.logic_random_int(low, high)
	row["cower_until_tick"] = _sim.tick_index + duration
	row["state"] = "cower"
	row["current_speed"] = 0.0
	_sim._clear_pending_route(row, true)
	_sim._emit_event("horde_ai.cower", entity_id, 0, {"duration_ticks": duration})
	return {"ok": true, "reason": "", "duration_ticks": duration}


