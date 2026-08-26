extends RefCounted
## Behavior-module contract subsystem extracted from retail_slice_sim.gd
## (Q81 strangler-fig extraction #13): every _attach_* contract binder
## plus its step/request/apply logic. Verbatim move, pin-verified.

var _sim_ref: WeakRef
var sim:
	get:
		return _sim_ref.get_ref()


func _init(owning_sim) -> void:
	_sim_ref = weakref(owning_sim)


func _attach_auto_heal_contract(row: Dictionary, contract: Dictionary) -> void:
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
	var tick_milliseconds = sim.TICK_SECONDS * 1000.0
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
		"armed_tick": sim.tick_index,
		"next_heal_tick": -1,
		"tag": String(contract.get("tag", "")),
		"source_ini": String(contract.get("sourceIni", contract.get("source_ini", ""))),
		"line": int(contract.get("line", 0)),
	}


func _step_auto_heal_updates() -> void:
	for id in sim.entity_ids():
		if not sim.entities.has(id):
			continue
		var row := sim.entities[id] as Dictionary
		if not row.has("auto_heal_behavior") and not row.has("module_contracts"):
			_attach_module_contracts(row)
		if row.has("auto_heal_behavior"):
			_apply_auto_heal_pulse(row, true)
	for id in sim.structure_ids():
		if not sim.structures.has(id):
			continue
		var row := sim.structures[id] as Dictionary
		if (
			not row.has("auto_heal_behavior")
			and not bool(row.get("structure_module_contracts_attached", false))
		):
			_attach_structure_module_contracts(row)
		if row.has("auto_heal_behavior"):
			_apply_auto_heal_pulse(row, false)


func _apply_auto_heal_pulse(row: Dictionary, battalion: bool) -> void:
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
		next_heal = maxi(anchor, sim.tick_index)
	if sim.tick_index < next_heal:
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
				effect["duration_ticks"] = maxi(1, roundi(float(effect.get("durationMs", 0.0)) / (sim.TICK_SECONDS * 1000.0)))
				effect["range_scaled"] = float(effect.get("range", 0.0)) * scale
			"leadership-aura":
				# AttributeModifierAuraUpdate: Range binds to map scale like every
				# other authored range; the modifier list itself is scale-free.
				effect["range_scaled"] = float(effect.get("range", 0.0)) * scale
			"terror":
				# FearNugget/TerrorSpecialPower: radius and the optional scatter
				# displacement are source units; FearDuration is milliseconds.
				effect["radius_scaled"] = float(effect.get("radius", 0.0)) * scale
				effect["duration_ticks"] = maxi(1, roundi(float(effect.get("durationMs", 0.0)) / (sim.TICK_SECONDS * 1000.0)))
				effect["scatter_strength_scaled"] = float(effect.get("scatterStrength", 0.0)) * scale
			"mount-toggle":
				# Mounted LocomotorSet speed is source units, like every speed.
				effect["mounted_speed_scaled"] = float(effect.get("mountedSpeed", 0.0)) * scale
			"capture-building":
				# StartAbilityRange gates the cast like an attack range; the
				# channel is the authored unpack + preparation + pack envelope.
				effect["range"] = float(effect.get("startAbilityRange", 0.0)) * scale
				var channel_ms := float(effect.get("unpackMs", 0.0)) + float(effect.get("preparationMs", 0.0)) + float(effect.get("packMs", 0.0))
				effect["channel_ticks"] = maxi(1, roundi(channel_ms / (sim.TICK_SECONDS * 1000.0)))
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
				effect["shot_interval_ticks"] = maxi(1, roundi(float(effect.get("persistentPrepMs", 0.0)) / (sim.TICK_SECONDS * 1000.0)))
			"stealth-toggle":
				# ToggleHiddenSpecialAbilityUpdate / InvisibilitySpecialPower:
				# EffectDuration is milliseconds; an authored BroadcastRadius
				# (ally cloak) binds to map scale. Zero stays zero: a duration
				# the converter could not resolve keeps the cast fail-closed,
				# while a leaf retail authors no duration for arrives flagged
				# `untimed` and cloaks until recast instead.
				var stealth_ms := float(effect.get("effectDurationMs", 0.0))
				effect["duration_ticks"] = maxi(1, roundi(stealth_ms / (sim.TICK_SECONDS * 1000.0))) if stealth_ms > 0.0 else 0
				effect["broadcast_radius_scaled"] = float(effect.get("broadcastRadius", 0.0)) * scale
			"teleport":
				# TeleportSpecialAbilityUpdate: an authored MaxDistance gates the
				# cast like a range; omission is unlimited. BusyForDuration holds
				# the hero after arrival. DestinationWeaponName fires at arrival.
				effect["range"] = float(effect.get("maxDistance", 0.0)) * scale
				effect["busy_ticks"] = maxi(0, roundi(float(effect.get("busyForDurationMs", 0.0)) / (sim.TICK_SECONDS * 1000.0)))
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
				effect["duration_ticks"] = maxi(1, roundi(strip_ms / (sim.TICK_SECONDS * 1000.0))) if strip_ms > 0.0 else 0
			"activate-module-graph":
				effect["range"] = float(effect.get("startAbilityRange", 0.0)) * scale
				effect["effect_range_scaled"] = float(effect.get("effectRange", 0.0)) * scale
				var timing := effect.get("timingMs", {}) as Dictionary
				var timing_ticks: Dictionary = {}
				for timing_key in ["StartDelay", "PreparationTime", "PersistentPrepTime", "UnpackTime", "PackTime", "SpecialPowerDuration"]:
					if timing.has(timing_key):
						timing_ticks[timing_key] = maxi(0, roundi(float(timing[timing_key]) / (sim.TICK_SECONDS * 1000.0)))
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
						timing_ticks[timing_key] = maxi(0, roundi(float(timing[timing_key]) / (sim.TICK_SECONDS * 1000.0)))
				effect["timing_ticks"] = timing_ticks
				if not bool(effect.get("permanentlyConvert", false)):
					var temporary_ms := float(effect.get("temporaryDefectDurationMs", 0.0))
					effect["temporary_defect_duration_ticks"] = maxi(1, roundi(temporary_ms / (sim.TICK_SECONDS * 1000.0))) if temporary_ms > 0.0 else 0
			"grab-passenger":
				var acquire := (effect.get("acquire", {}) as Dictionary).duplicate(true)
				effect["range"] = float(acquire.get("startAbilityRange", 0.0)) * scale
				var acquire_timing := acquire.get("timingMs", {}) as Dictionary
				var acquire_ticks: Dictionary = {}
				for timing_key in ["UnpackTime", "PreparationTime", "PersistentPrepTime", "PackTime"]:
					acquire_ticks[timing_key] = maxi(0, roundi(float(acquire_timing.get(timing_key, 0.0)) / (sim.TICK_SECONDS * 1000.0)))
				acquire["timing_ticks"] = acquire_ticks
				var animation := (acquire.get("animation", {}) as Dictionary).duplicate(true)
				animation["duration_ticks"] = maxi(0, roundi(float(animation.get("durationMs", 0.0)) / (sim.TICK_SECONDS * 1000.0)))
				animation["trigger_ticks"] = maxi(0, roundi(float(animation.get("triggerTimeMs", 0.0)) / (sim.TICK_SECONDS * 1000.0)))
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
					"UnpackTime": maxi(0, roundi(float(fling_timing.get("UnpackTime", 0.0)) / (sim.TICK_SECONDS * 1000.0))),
					"PackTime": maxi(0, roundi(float(fling_timing.get("PackTime", 0.0)) / (sim.TICK_SECONDS * 1000.0))),
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
	if row.has("module_contracts"):
		return
	var unit_type := String(row.get("unit_type", ""))
	var contracts: Array = sim._unit_module_contracts.get(unit_type, []) as Array
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
			sim._attach_fire_weapon_when_dead_contract(row, contract)
		elif folded == "hordetransportcontain":
			sim._attach_horde_transport_contract(row, contract)
		elif folded in ["transportcontain", "tunnelcontain", "garrisoncontain", "hordegarrisoncontain"]:
			sim._attach_container_family_contract(row, contract)
		elif folded == "productionqueuehordecontain":
			sim._attach_container_family_contract(row, contract)
		elif folded == "siegeenginecontain":
			sim._attach_siege_engine_contain_contract(row, contract)
		elif folded == "largegroupbonusupdate":
			_attach_large_group_bonus_contract(row, contract)
		elif folded == "hitreactionbehavior":
			_attach_hit_reaction_contract(row, contract)
		elif folded == "animalaiupdate":
			_attach_animal_ai_contract(row, contract)
		elif folded == "threatfinderupdate":
			_attach_threat_finder_contract(row, contract)
		elif folded == "radiatefearupdate":
			_attach_radiate_fear_contract(row, contract)
		elif folded == "poisonedbehavior":
			_attach_poisoned_contract(row, contract)
		elif folded == "damagefieldupdate":
			_attach_damage_field_contract(row, contract)
		elif folded == "spawnunitbehavior":
			_attach_spawn_unit_contract(row, contract)
		elif folded == "modelconditionsoundselectorclientbehavior":
			_attach_model_condition_sound_selector(row, contract)
		elif folded == "randomsoundselectorclientbehavior":
			_attach_random_sound_selector(row, contract)
		elif folded == "upgradesoundselectorclientbehavior":
			_attach_upgrade_sound_selector(row, contract)
		elif folded == "largegroupaudioupdate":
			_attach_large_group_audio_contract(row, contract)
		elif folded == "firespreadupdate":
			_attach_fire_spread_contract(row, contract)
		elif folded == "shipslowdeathbehavior":
			sim._attach_ship_slow_death_contract(row, contract)
		elif folded == "slowdeathbehavior":
			sim._attach_slow_death_core_contract(row, contract)
		elif folded == "attributemodifierauraupdate":
			_attach_attribute_modifier_aura_contract(row, contract)
		elif folded == "autohealbehavior":
			_attach_auto_heal_contract(row, contract)
		elif folded == "lifetimeupdate":
			_attach_lifetime_update_contract(row, contract)
		elif folded == "stancesbehavior":
			_attach_stances_contract(row, contract)
		elif folded == "aiupdateinterface":
			_attach_ai_update_contract(row, contract)
		elif folded == "hordeaiupdate":
			_attach_horde_ai_update_contract(row, contract)
		elif folded == "pickupstuffupdate":
			_attach_pickup_stuff_update_contract(row, contract)
		elif folded == "autoabilitybehavior":
			_attach_auto_ability_contract(row, contract)
		elif folded == "aispecialpowerupdate":
			_attach_ai_special_power_contract(row, contract)
		elif folded == "weaponmodespecialpowerupdate":
			sim._attach_weapon_mode_special_power_contract(row, contract)
		elif folded == "respawnupdate":
			_attach_respawn_update_contract(row, contract)
		elif folded == "fireweaponupdate":
			_attach_fire_weapon_update_contract(row, contract)
		elif folded == "deletionupdate":
			_attach_deletion_update_contract(row, contract)
		elif folded == "productionupdate":
			_attach_production_update_contract(row, contract)
		elif folded == "gettingbuiltbehavior":
			_attach_getting_built_contract(row, contract)
		elif folded == "buildingbehavior":
			_attach_building_behavior_contract(row, contract)
		elif folded == "queueproductionexitupdate":
			_attach_queue_production_exit_contract(row, contract)
		elif folded == "rebuildholeexposeddie" or folded == "rebuildholeexposedie":
			_attach_rebuild_hole_expose_contract(row, contract)
		elif folded == "rebuildholebehavior":
			_attach_rebuild_hole_behavior_contract(row, contract)
		elif folded == "bannercarrierupdate":
			_attach_banner_carrier_update_contract(row, contract)
		elif folded == "respawnbody":
			_attach_respawn_body_contract(row, contract)
		elif folded == "giveupgradeupdate":
			_attach_give_upgrade_contract(row, contract)
		elif folded == "gateopenandclosebehavior":
			_attach_gate_open_close_contract(row, contract)
		elif folded == "aigateupdate":
			_attach_ai_gate_contract(row, contract)
		elif folded == "fakepathfindportalbehaviour":
			_attach_fake_pathfind_portal_contract(row, contract)
		elif folded == "stealthdetectorupdate":
			_attach_stealth_detector_contract(row, contract)
		elif folded == "invisibilityupdate":
			_attach_invisibility_update_contract(row, contract)
		elif folded == "slavedupdate":
			_attach_slaved_update_contract(row, contract)
		elif folded == "castleupgrade":
			_attach_castle_upgrade_contract(row, contract)
		elif folded == "spawnbehavior":
			_attach_spawn_behavior_contract(row, contract)
		elif folded == "stealthupdate":
			_attach_stealth_update_contract(row, contract)
		elif folded == "objectcreationupgrade":
			_attach_object_creation_upgrade_contract(row, contract)
		elif folded == "attributemodifierupgrade":
			_attach_attribute_modifier_upgrade_contract(row, contract)
		elif folded == "geometryupgrade":
			_attach_geometry_upgrade_contract(row, contract)
		elif folded == "emotiontrackerupdate":
			_attach_emotion_tracker_contract(row, contract)
		elif folded == "castlememberbehavior":
			_attach_castle_member_contract(row, contract)
		elif folded == "inactivebody":
			_attach_inactive_body_contract(row, contract)
		elif folded == "squishcollide":
			_attach_squish_collide_contract(row, contract)
		elif folded == "hordemembercollide":
			_attach_horde_member_collide_contract(row, contract)
		elif folded == "notifytargetsofimminentprobablecrushingupdate":
			_attach_notify_crushing_contract(row, contract)
		elif folded == "flammableupdate":
			_attach_flammable_update_contract(row, contract)
		elif folded == "dynamicportalbehaviour":
			_attach_dynamic_portal_contract(row, contract)
		elif folded == "foundationaiupdate":
			_attach_foundation_ai_contract(row, contract)
		elif folded == "monitorconditionupdate":
			_attach_monitor_condition_contract(row, contract)
		elif folded == "refunddie":
			_attach_refund_die_contract(row, contract)
		elif folded == "dualweaponbehavior":
			_attach_dual_weapon_contract(row, contract)
		elif folded == "attachupdate":
			_attach_attach_update_contract(row, contract)
		elif folded == "replaceselfupgrade":
			_attach_replace_self_contract(row, contract)
		elif folded == "citadelslaughterhordecontain":
			_attach_citadel_slaughter_contract(row, contract)
		elif folded == "oclupdate":
			_attach_ocl_update_contract(row, contract)
		elif folded == "hordecontain":
			_attach_horde_contain_contract(row, contract)
		elif folded == "stopspecialpower":
			_attach_stop_special_power_contract(row, contract)
		elif folded == "unleashspecialpower":
			_attach_unleash_special_power_contract(row, contract)
		elif folded == "specialenemysenseupdate":
			_attach_special_enemy_sense_contract(row, contract)


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
	var runtimes_value: Variant = sim._rules.get("playable_structure_runtimes", {})
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
			document, sim.PlayableUnitAdapter.module_contracts(document)
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
		var rows: Array = sim._castle_upgrade_grants.get(trigger.to_lower(), []) as Array
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
		sim._castle_upgrade_grants[trigger.to_lower()] = rows


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
	if bool(apply_castle_upgrade_trigger(int(building.get("id",0)),trigger_upgrade_id).get("ok",false)):
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
			if not sim.structures.has(recipient_id):
				continue
			var recipient: Dictionary = sim.structures[recipient_id]
			var owned: Array = recipient.get("completed_upgrades", [])
			if owned.has(granted):
				continue
			owned.append(granted)
			recipient["completed_upgrades"] = owned
		sim._emit_event("upgrade.castle_granted", structure_id, 0, {
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
	var explicit_tables := false
	for key in ["scenario_unit_runtimes", "scenario_structure_runtimes", "scenario_prop_runtimes", "scenario_pickup_runtimes"]:
		if sim._rules.has(key):
			explicit_tables = true
			break
	# Merely installing/selecting a neutral pack is not match state. Direct sims
	# (including the owner pin) only inherit global registries when an active
	# scenario-map lane requests them; injected test/script tables remain valid.
	if not explicit_tables and not bool(sim._rules.get("enable_scenario_map_placements", false)):
		return
	var game := String(sim._rules.get("game", "")).to_lower()
	if game not in ["bfme2", "rotwk"]:
		sim._rules["_scenario_registry_error"] = "scenario runtime selection requires game=bfme2 or game=rotwk"
		return
	var db = _content_db_ref()
	if db == null and not explicit_tables:
		sim._rules["_scenario_registry_error"] = "scenario runtime selection has no ContentDB"
		return
	for key in ["scenario_unit_runtimes", "scenario_structure_runtimes", "scenario_prop_runtimes", "scenario_pickup_runtimes"]:
		if sim._rules.has(key):
			continue
		var getter := "get_%s" % key
		if not db.has_method(getter):
			sim._rules["_scenario_registry_error"] = "ContentDB missing edition-scoped %s" % getter
			return
		var value: Variant = db.call(getter, game)
		if typeof(value) == TYPE_DICTIONARY and not (value as Dictionary).is_empty():
			sim._rules[key] = (value as Dictionary).duplicate(true)


func scenario_spawn_contract(object_id: String, surface: String) -> Dictionary:
	## One fail-closed lookup for map placement, scripts, OCL leaves and lair
	## payloads. The three registries stay disjoint from faction production and
	## HUD tables; an identity admitted by more than one kind is ambiguous and is
	## therefore refused instead of selected by registry order.
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
		var source_rule = sim.PlayableUnitAdapter.simulation_rule(document, false)
		if not source_rule.is_empty():
			result["unit_rule"] = sim.PlayableUnitAdapter.normalized_unit_rule(
				source_rule, float(sim._rules.get("source_map_transform_scale", 0.0))
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
	var contract := scenario_spawn_contract(object_id, surface)
	if String(contract.get("kind", "")) != "unit":
		return -1
	var rule := contract.get("unit_rule", {}) as Dictionary
	var document := contract.get("document", {}) as Dictionary
	if rule.is_empty() or (requested_id <= 0 and not sim._next_dynamic_id.has(team)):
		return -1
	var entity_id := requested_id if requested_id > 0 else int(sim._next_dynamic_id[team])
	if sim.entities.has(entity_id) or sim.structures.has(entity_id):
		return -1
	if requested_id <= 0:
		sim._next_dynamic_id[team] = entity_id + 1
	sim._add_battalion(
		entity_id, team, at, String(rule.get("display_name", object_id)),
		String(rule.get("object_id", object_id)), String(rule.get("horde_id", object_id)),
		0, rule
	)
	if not sim.entities.has(entity_id):
		return -1
	var row := sim.entities[entity_id] as Dictionary
	# Scenario-only units never enter the faction unit registry, but their exact
	# module contracts still have to reach the instance. Registering after the
	# body is constructed avoids exposing the object as production while letting
	# SlavedUpdate bind the authored lair master on the same spawn tick.
	var scenario_contracts = sim.PlayableUnitAdapter.module_contracts(document)
	if not scenario_contracts.is_empty():
		var unit_type := String(row.get("unit_type", object_id))
		var registered := sim._unit_module_contracts.get(unit_type, []) as Array
		if registered.is_empty():
			sim._unit_module_contracts[unit_type] = scenario_contracts.duplicate(true)
		elif registered != scenario_contracts:
			sim.entities.erase(entity_id)
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
	var contract := scenario_spawn_contract(object_id, surface)
	if String(contract.get("kind", "")) != "structure":
		return -1
	var document := contract.get("document", {}) as Dictionary
	var rule := _scenario_structure_instantiation_rule(document)
	if rule.is_empty():
		return -1
	var structure_id = requested_id if requested_id > 0 else sim._next_dynamic_structure_id
	if sim.structures.has(structure_id) or sim.entities.has(structure_id):
		return -1
	if requested_id <= 0:
		sim._next_dynamic_structure_id += 1
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
		"scenario_game": String(document.get("game", sim._rules.get("game", ""))).to_lower(),
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
			var normalized = sim._normalized_command_set_upgrade_effect(effect)
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
	sim._note_structure_table_mutation()
	sim.structures[structure_id] = row
	_attach_structure_module_contracts(sim.structures[structure_id] as Dictionary)
	if team >= 0 and team != sim.CREEP_TEAM:
		sim._apply_scenario_structure_faction_command_set(sim.structures[structure_id] as Dictionary, team)
	return structure_id


func spawn_scenario_prop(object_id: String, at: Vector2, surface: String) -> int:
	## Passive props are deterministic world presentation records only. The
	## ContentDB contract admits IMMOBILE + INERT/OPTIMIZED_PROP objects and
	## rejects every combat/structure KindOf token, so no owner or body is made.
	var contract := scenario_spawn_contract(object_id, surface)
	if String(contract.get("kind", "")) != "prop":
		return -1
	var document := contract.get("document", {}) as Dictionary
	if not _scenario_prop_is_passive(document):
		return -1
	var prop_id = sim._next_scenario_prop_id
	sim._next_scenario_prop_id += 1
	sim.scenario_props[prop_id] = {
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
	var registry_kind := ""
	var row: Dictionary = {}
	if sim.scenario_props.has(prop_id):
		registry_kind = "prop"
		row = sim.scenario_props[prop_id] as Dictionary
	elif sim.entities.has(prop_id) and String((sim.entities[prop_id] as Dictionary).get("scenario_source_object_id", "")) != "":
		registry_kind = "unit"
		row = sim.entities[prop_id] as Dictionary
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
	var receipt = sim.PlayableUnitAdapter.bezier_trajectory_contract(document)
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
	if sim.scenario_props.is_empty() and sim.entities.is_empty():
		return
	var carriers: Array[Dictionary] = []
	for prop_id in sim.scenario_props.keys():
		carriers.append({"id": int(prop_id), "kind": "prop"})
	for entity_id in sim.entities.keys():
		if typeof((sim.entities[entity_id] as Dictionary).get("bezier_projectile")) == TYPE_DICTIONARY:
			carriers.append({"id": int(entity_id), "kind": "unit"})
	carriers.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		return int(a["id"]) < int(b["id"])
	)
	for carrier in carriers:
		var id_value := int(carrier["id"])
		var row := (
			sim.scenario_props[id_value] as Dictionary
			if String(carrier["kind"]) == "prop"
			else sim.entities[id_value] as Dictionary
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
			active["arrival_tick"] = sim.tick_index
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
					"tick": sim.tick_index,
					"ordinal": requests.size(),
				})
				sim.scenario_bezier_presentation_requests.append(
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
				"tick": sim.tick_index,
				"ordinal": requests.size(),
			})
			sim.scenario_bezier_presentation_requests.append(
				(requests[requests.size() - 1] as Dictionary).duplicate(true)
			)
			active["status"] = "landed"
			active["terminal_policy"] = String(arrival.get("terminalPolicy", ""))
			if String(active["terminal_policy"]) == "remove-on-final-impact":
				if String(carrier["kind"]) == "prop":
					sim.scenario_props.erase(id_value)
				else:
					sim.delete_entity(id_value)


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
		"structure_kind": sim._scenario_structure_kind(document),
		"admission": admission.duplicate(true),
		"lifecycle": lifecycle.duplicate(true),
		"gameplay": gameplay.duplicate(true),
		"module_contracts": _structure_contracts_with_passive_area_resolution(
			document, sim.PlayableUnitAdapter.module_contracts(document)
		),
	}
	if String(result.get("structure_kind", "")) == "":
		return {}
	var geometry_value: Variant = gameplay.get("geometry", {})
	if typeof(geometry_value) == TYPE_DICTIONARY:
		var footprint_radius = sim.SelectionPick.source_footprint_radius(geometry_value as Dictionary)
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
	var pickup_id = sim._next_pickup_object_id
	sim._next_pickup_object_id += 1
	sim.pickup_objects[pickup_id] = {
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
	if not sim.pickup_objects.has(pickup_id):
		return {"ok": false, "reason": "pickup-missing"}
	if not sim.entities.has(picker_id):
		return {"ok": false, "reason": "picker-missing"}
	var pickup := sim.pickup_objects[pickup_id] as Dictionary
	if String(pickup.get("kind", "")) != "active-pickup":
		return {"ok": false, "reason": "not-active-pickup"}
	var picker := sim.entities[picker_id] as Dictionary
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
	var roll = sim.logic_random_real(0.0, 1.0)
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
			amount = minimum if minimum == maximum else sim.logic_random_int(minimum, maximum)
			if amount > 0:
				var team := int(picker.get("team", -1))
				sim.team_resources[team] = sim.resources_for_team(team) + amount
			reward = "resource"
	var execute_fx := String(_module_contract_value(fields, "ExecuteFX", ""))
	sim._emit_event("pickup.salvage_collected", picker_id, pickup_id, {
		"reward": reward,
		"amount": amount,
		"roll": roll,
		"execute_fx": execute_fx,
	})
	sim.pickup_objects.erase(pickup_id)
	return {"ok": true, "reason": "", "reward": reward, "amount": amount, "roll": roll, "execute_fx": execute_fx}


func _step_active_pickup_collisions() -> void:
	var pickup_ids: Array[int] = []
	for value in sim.pickup_objects.keys(): pickup_ids.append(int(value))
	pickup_ids.sort()
	for pickup_id in pickup_ids:
		if not sim.pickup_objects.has(pickup_id): continue
		var pickup := sim.pickup_objects[pickup_id] as Dictionary
		if String(pickup.get("kind", "")) != "active-pickup" or not bool(pickup.get("available", true)): continue
		var geometry := pickup.get("geometry", {}) as Dictionary;var footprint := geometry.get("footprint", {}) as Dictionary
		if typeof(footprint.get("radius")) not in [TYPE_INT, TYPE_FLOAT]: continue
		var crate_radius := float(footprint.get("radius", 0.0)) * float(sim._rules.get("source_unit_scale", 0.1))
		if crate_radius <= 0.0: continue
		var origin := Vector2(pickup.get("position", Vector2.ZERO))
		for picker_id in sim.entity_ids():
			var picker := sim.entities[picker_id] as Dictionary
			if int(picker.get("health", 0)) <= 0: continue
			var collision_radius = crate_radius + sim._target_footprint_radius(picker_id, "battalion")
			if origin.distance_to(Vector2(picker.get("position", Vector2.ZERO))) <= collision_radius:
				if bool(collect_salvage_crate(pickup_id, picker_id).get("ok", false)): break


func _grant_one_authored_rank(row: Dictionary) -> void:
	var rule := sim._unit_experience_rules.get(String(row.get("unit_type", "")), {}) as Dictionary
	if rule.is_empty() or int(row.get("health", 0)) <= 0:
		return
	var level := int(row.get("level", 1))
	for value in rule.get("levels", []) as Array:
		var next := value as Dictionary
		if int(next.get("rank", 0)) <= level:
			continue
		var needed := maxi(0, int(next.get("required_experience", 0)) - int(row.get("experience_xp", 0)))
		if needed > 0:
			sim._award_experience(row, needed)
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
		if key != "" and sim._structure_module_contracts.has(key):
			contracts = (sim._structure_module_contracts[key] as Array).duplicate(true)
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
			sim._attach_horde_transport_contract(row, contract)
		elif folded in ["transportcontain", "tunnelcontain", "garrisoncontain", "hordegarrisoncontain"]:
			sim._attach_container_family_contract(row, contract)
		elif folded == "productionqueuehordecontain":
			sim._attach_container_family_contract(row, contract)
		elif folded == "siegeenginecontain":
			sim._attach_siege_engine_contain_contract(row, contract)
		elif folded == "largegroupbonusupdate":
			_attach_large_group_bonus_contract(row, contract)
		elif folded == "hitreactionbehavior":
			_attach_hit_reaction_contract(row, contract)
		elif folded == "animalaiupdate":
			_attach_animal_ai_contract(row, contract)
		elif folded == "threatfinderupdate":
			_attach_threat_finder_contract(row, contract)
		elif folded == "radiatefearupdate":
			_attach_radiate_fear_contract(row, contract)
		elif folded == "poisonedbehavior":
			_attach_poisoned_contract(row, contract)
		elif folded == "damagefieldupdate":
			_attach_damage_field_contract(row, contract)
		elif folded == "spawnunitbehavior":
			_attach_spawn_unit_contract(row, contract)
		elif folded == "modelconditionsoundselectorclientbehavior":
			_attach_model_condition_sound_selector(row, contract)
		elif folded == "randomsoundselectorclientbehavior":
			_attach_random_sound_selector(row, contract)
		elif folded == "upgradesoundselectorclientbehavior":
			_attach_upgrade_sound_selector(row, contract)
		elif folded == "largegroupaudioupdate":
			_attach_large_group_audio_contract(row, contract)
		elif folded == "firespreadupdate":
			_attach_fire_spread_contract(row, contract)
		elif folded == "shipslowdeathbehavior":
			sim._attach_ship_slow_death_contract(row, contract)
		elif folded == "attributemodifierauraupdate":
			_attach_attribute_modifier_aura_contract(row, contract)
		elif folded == "autohealbehavior":
			_attach_auto_heal_contract(row, contract)
		elif folded == "lifetimeupdate":
			_attach_lifetime_update_contract(row, contract)
		elif folded == "stancesbehavior":
			_attach_stances_contract(row, contract)
		elif folded == "aiupdateinterface":
			_attach_ai_update_contract(row, contract)
		elif folded == "hordeaiupdate":
			_attach_horde_ai_update_contract(row, contract)
		elif folded == "pickupstuffupdate":
			_attach_pickup_stuff_update_contract(row, contract)
		elif folded == "autoabilitybehavior":
			_attach_auto_ability_contract(row, contract)
		elif folded == "aispecialpowerupdate":
			_attach_ai_special_power_contract(row, contract)
		elif folded == "weaponmodespecialpowerupdate":
			sim._attach_weapon_mode_special_power_contract(row, contract)
		elif folded == "respawnupdate":
			_attach_respawn_update_contract(row, contract)
		elif folded == "fireweaponupdate":
			_attach_fire_weapon_update_contract(row, contract)
		elif folded == "deletionupdate":
			_attach_deletion_update_contract(row, contract)
		elif folded == "productionupdate":
			_attach_production_update_contract(row, contract)
		elif folded == "gettingbuiltbehavior":
			_attach_getting_built_contract(row, contract)
		elif folded == "buildingbehavior":
			_attach_building_behavior_contract(row, contract)
		elif folded == "queueproductionexitupdate":
			_attach_queue_production_exit_contract(row, contract)
		elif folded == "rebuildholeexposeddie" or folded == "rebuildholeexposedie":
			_attach_rebuild_hole_expose_contract(row, contract)
		elif folded == "rebuildholebehavior":
			_attach_rebuild_hole_behavior_contract(row, contract)
		elif folded == "bannercarrierupdate":
			_attach_banner_carrier_update_contract(row, contract)
		elif folded == "respawnbody":
			_attach_respawn_body_contract(row, contract)
		elif folded == "giveupgradeupdate":
			_attach_give_upgrade_contract(row, contract)
		elif folded == "gateopenandclosebehavior":
			_attach_gate_open_close_contract(row, contract)
		elif folded == "aigateupdate":
			_attach_ai_gate_contract(row, contract)
		elif folded == "fakepathfindportalbehaviour":
			_attach_fake_pathfind_portal_contract(row, contract)
		elif folded == "stealthdetectorupdate":
			_attach_stealth_detector_contract(row, contract)
		elif folded == "invisibilityupdate":
			_attach_invisibility_update_contract(row, contract)
		elif folded == "slavedupdate":
			_attach_slaved_update_contract(row, contract)
		elif folded == "castleupgrade":
			_attach_castle_upgrade_contract(row, contract)
		elif folded == "spawnbehavior":
			_attach_spawn_behavior_contract(row, contract)
		elif folded == "stealthupdate":
			_attach_stealth_update_contract(row, contract)
		elif folded == "objectcreationupgrade":
			_attach_object_creation_upgrade_contract(row, contract)
		elif folded == "attributemodifierupgrade":
			_attach_attribute_modifier_upgrade_contract(row, contract)
		elif folded == "geometryupgrade":
			_attach_geometry_upgrade_contract(row, contract)
		elif folded == "emotiontrackerupdate":
			_attach_emotion_tracker_contract(row, contract)
		elif folded == "castlememberbehavior":
			_attach_castle_member_contract(row, contract)
		elif folded == "inactivebody":
			_attach_inactive_body_contract(row, contract)
		elif folded == "squishcollide":
			_attach_squish_collide_contract(row, contract)
		elif folded == "hordemembercollide":
			_attach_horde_member_collide_contract(row, contract)
		elif folded == "notifytargetsofimminentprobablecrushingupdate":
			_attach_notify_crushing_contract(row, contract)
		elif folded == "flammableupdate":
			_attach_flammable_update_contract(row, contract)
		elif folded == "dynamicportalbehaviour":
			_attach_dynamic_portal_contract(row, contract)
		elif folded == "foundationaiupdate":
			_attach_foundation_ai_contract(row, contract)
		elif folded == "monitorconditionupdate":
			_attach_monitor_condition_contract(row, contract)
		elif folded == "refunddie":
			_attach_refund_die_contract(row, contract)
		elif folded == "wallhubbehavior":
			_attach_wall_hub_contract(row, contract)
		elif folded == "buildableherolistupgrade":
			_attach_buildable_hero_list_upgrade_contract(row, contract)
		elif folded == "allowbannerspawnupgrade":
			_attach_allow_banner_spawn_upgrade_contract(row, contract)
		elif folded == "spellrechargemodifierupgrade":
			_attach_spell_recharge_modifier_upgrade_contract(row, contract)
		elif folded == "replaceselfupgrade":
			_attach_replace_self_contract(row, contract)
		elif folded == "citadelslaughterhordecontain":
			_attach_citadel_slaughter_contract(row, contract)
		elif folded == "oclupdate":
			_attach_ocl_update_contract(row, contract)
		elif folded == "hordecontain":
			_attach_horde_contain_contract(row, contract)
		elif folded == "stopspecialpower":
			_attach_stop_special_power_contract(row, contract)
		elif folded == "unleashspecialpower":
			_attach_unleash_special_power_contract(row, contract)
		elif folded == "specialenemysenseupdate":
			_attach_special_enemy_sense_contract(row, contract)
		if folded == "fireweaponwhendeadbehavior":
			sim._attach_fire_weapon_when_dead_contract(row, contract)
		# PassiveAreaEffectBehavior heal variant. The converter currently labels
		# the generic module contract deferred because its ModifierName leadership
		# variant still needs the ModifierList consumer. Healing is independent and
		# fully authored by these fields, so consume only that closed subset here.
		if folded == "passiveareaeffectbehavior":
			var heal_percent := _passive_area_effect_percent(
				_passive_area_effect_field(fields, "HealPercentPerSecond")
			)
			var radius = _passive_area_effect_number(fields, "EffectRadius")
			var ping_ms := _passive_area_effect_number(fields, "PingDelay")
			if heal_percent > 0.0 and radius > 0.0 and ping_ms > 0.0:
				var rows: Array = row.get("passive_area_effect_heals", []) as Array
				rows.append({
					"radius_source": radius,
					"ping_ticks": maxi(1, roundi(ping_ms / (sim.TICK_SECONDS * 1000.0))),
					"heal_fraction_per_second": heal_percent,
					"allow_filter": _passive_area_effect_field(fields, "AllowFilter"),
					"upgrade_required": _passive_area_effect_field(fields, "UpgradeRequired"),
					"non_stackable": _passive_area_effect_yes(fields, "NonStackable"),
					"tag": String(contract.get("tag", "")),
					"source_ini": String(contract.get("source_ini", contract.get("sourceIni", ""))),
					# Lazy attachment occurs inside the current tick after sim.tick_index
					# advances. The structure existed for this tick interval already,
					# so cadence is measured from the preceding boundary.
					"next_ping_tick": sim.tick_index - 1 + maxi(1, roundi(ping_ms / (sim.TICK_SECONDS * 1000.0))),
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
						"duration_ticks": maxi(1, roundi(duration_ms / (sim.TICK_SECONDS * 1000.0))),
						"ping_ticks": maxi(1, roundi(modifier_ping_ms / (sim.TICK_SECONDS * 1000.0))),
						"radius_source": radius,
						"allow_filter": _passive_area_effect_field(fields, "AllowFilter"),
						"upgrade_required": _passive_area_effect_field(fields, "UpgradeRequired"),
						"non_stackable": _passive_area_effect_yes(fields, "NonStackable"),
						"stacking": (modifier.get("stacking", {}) as Dictionary).duplicate(true),
						"next_ping_tick": sim.tick_index,
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
	if not sim.structures.has(structure_id):
		return false
	var structure := sim.structures[structure_id] as Dictionary
	if not structure.has("allow_banner_spawn_upgrade") and not bool(structure.get("structure_module_contracts_attached", false)):
		_attach_structure_module_contracts(structure)
	var policy := structure.get("allow_banner_spawn_upgrade", {}) as Dictionary
	if policy.is_empty():
		# No AllowBannerSpawnUpgrade means this module does not restrict the
		# ordinary horde-level BannerCarriersAllowed path.
		return true
	for upgrade_value in policy.get("triggered_by", []) as Array:
		if sim._structure_has_completed_upgrade(structure, String(upgrade_value)):
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
	var active_policies: Array[Dictionary] = []
	for structure_id in sim.structure_ids(team):
		var structure := sim.structures[structure_id] as Dictionary
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
	if not sim.entities.has(horde_id):
		return {"ok": false, "reason": "horde-missing"}
	var row := sim.entities[horde_id] as Dictionary
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
	if not sim._transport_filter_accepts(probe, policy.get("passenger_filter", []) as Array):
		return {"ok": false, "reason": "passenger-filter-refused"}
	var member := {"object_id": object_id, "kind_of": kind_of.duplicate(), "rank": rank, "health": maxi(1, health), "status": "contained", "object_status": {}}
	for status_value in policy.get("contained_statuses", []) as Array:
		(member["object_status"] as Dictionary)[String(status_value)] = true
	members.append(member)
	row["horde_contained_members"] = members
	return {"ok": true, "reason": "", "index": members.size() - 1}


func eject_horde_member(horde_id: int, member_index: int) -> Dictionary:
	if not sim.entities.has(horde_id):
		return {"ok": false, "reason": "horde-missing"}
	var row := sim.entities[horde_id] as Dictionary
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
	if not sim.entities.has(horde_id):
		return {"ok": false, "reason": "horde-missing"}
	var row := sim.entities[horde_id] as Dictionary
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
		_expire_lifetime_entity(horde_id, row, death_type)
	members.remove_at(member_index)
	row["horde_contained_members"] = members
	return {"ok": true, "reason": "", "killed": true, "horde_killed": kills_horde}


func horde_amoeba_melee_reach(horde_id: int, target_position: Vector2, target_is_building: bool = false) -> Dictionary:
	if not sim.entities.has(horde_id):
		return {"ok": false, "reason": "horde-missing"}
	var row := sim.entities[horde_id] as Dictionary
	var policy := row.get("horde_contain", {}) as Dictionary
	if String(policy.get("melee_behavior", "")).to_lower() != "amoeba":
		return {"ok": false, "reason": "amoeba-not-authored"}
	var scale := float(sim._rules.get("source_map_transform_scale", 1.0))
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
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity-missing"}
	var row := sim.entities[entity_id] as Dictionary
	if not row.has("horde_ai_update"):
		_attach_module_contracts(row)
	if not row.has("horde_ai_update"):
		return {"ok": false, "reason": "typed-horde-ai-contract-missing"}
	var policy := row["horde_ai_update"] as Dictionary
	var low := maxi(0, int(policy.get("minimum_cower_ticks", 0)))
	var high := maxi(low, int(policy.get("maximum_cower_ticks", low)))
	var duration = low if low == high else sim.logic_random_int(low, high)
	row["cower_until_tick"] = sim.tick_index + duration
	row["state"] = "cower"
	row["current_speed"] = 0.0
	sim._clear_pending_route(row, true)
	sim._emit_event("horde_ai.cower", entity_id, 0, {"duration_ticks": duration})
	return {"ok": true, "reason": "", "duration_ticks": duration}


func _attach_pickup_stuff_update_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("pickup_stuff_update"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var interval_value: Variant = fields.get("ScanIntervalSeconds")
	var interval_ms := 0.0
	if typeof(interval_value) == TYPE_DICTIONARY:
		var interval_field := interval_value as Dictionary
		interval_ms = float(interval_field.get("milliseconds", float(interval_field.get("seconds", 0.0)) * 1000.0))
	row["pickup_stuff_update"] = {
		"skirmish_ai_only": bool(_module_contract_value(fields, "SkirmishAIOnly", false)),
		"filter": _typed_contract_tokens(fields, "StuffToPickUp"),
		"scan_range_source": maxf(0.0, float(_module_contract_value(fields, "ScanRange", 0.0))),
		"scan_interval_ticks": maxi(1, _ship_contract_delay_ticks(interval_ms)),
		"next_scan_tick": sim.tick_index,
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}


func register_pickup_object(kind_of: Array, position: Vector2, object_id: String = "") -> int:
	var id = sim._next_pickup_object_id
	sim._next_pickup_object_id += 1
	sim.pickup_objects[id] = {
		"id": id,
		"object_id": object_id,
		"kind_of": kind_of.duplicate(),
		"position": position,
		"available": true,
	}
	return id


func remove_pickup_object(pickup_id: int) -> void:
	sim.pickup_objects.erase(pickup_id)


func _step_pickup_stuff_updates() -> void:
	for entity_id in sim.entity_ids():
		var row := sim.entities[entity_id] as Dictionary
		if not row.has("pickup_stuff_update") and not row.has("module_contracts"):
			_attach_module_contracts(row)
		var policy := row.get("pickup_stuff_update", {}) as Dictionary
		if policy.is_empty() or int(row.get("health", 0)) <= 0:
			continue
		if bool(policy.get("skirmish_ai_only", false)) and (not sim.ai_enabled or not sim.team_is_ai(int(row.get("team", -1)))):
			continue
		if sim.tick_index < int(policy.get("next_scan_tick", sim.tick_index)):
			continue
		policy["next_scan_tick"] = sim.tick_index + maxi(1, int(policy.get("scan_interval_ticks", 1)))
		row["pickup_stuff_update"] = policy
		var nearest := _nearest_pickup_for(row, policy)
		if nearest == 0:
			continue
		var destination := Vector2((sim.pickup_objects[nearest] as Dictionary).get("position", row.get("position", Vector2.ZERO)))
		if sim._assign_route(row, destination):
			row["pickup_target_id"] = nearest
			row["order_kind"] = "pickup"
			row["state"] = "run"
			sim._emit_event("pickup_stuff.targeted", entity_id, nearest, {"destination": [destination.x, destination.y]})


func _nearest_pickup_for(row: Dictionary, policy: Dictionary) -> int:
	var origin := Vector2(row.get("position", Vector2.ZERO))
	var scale := float(sim._rules.get("source_map_transform_scale", 1.0))
	var maximum := float(policy.get("scan_range_source", 0.0)) * (scale if scale > 0.0 else 1.0)
	var best_id := 0
	var best_distance := maximum
	for pickup_id_value in sim.pickup_objects.keys():
		var pickup_id = int(pickup_id_value)
		var pickup := sim.pickup_objects[pickup_id] as Dictionary
		if not bool(pickup.get("available", true)):
			continue
		var probe := {"kind_of": pickup.get("kind_of", [])}
		if not sim._transport_filter_accepts(probe, policy.get("filter", []) as Array):
			continue
		var distance := origin.distance_to(Vector2(pickup.get("position", origin)))
		if distance < best_distance or (is_equal_approx(distance, best_distance) and (best_id == 0 or pickup_id < best_id)):
			best_distance = distance
			best_id = pickup_id
	return best_id


func _attach_auto_ability_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields := contract.get("fields", {}) as Dictionary
	var rows: Array = row.get("auto_ability_behaviors", []) as Array
	var key := "%s:%d" % [String(contract.get("tag", "")), int(contract.get("line", 0))]
	for existing_value in rows:
		if String((existing_value as Dictionary).get("key", "")) == key:
			return
	var idle_ms := 0.0
	var idle_field: Variant = fields.get("IdleTimeSeconds")
	if typeof(idle_field) == TYPE_DICTIONARY:
		idle_ms = float((idle_field as Dictionary).get("milliseconds", float((idle_field as Dictionary).get("seconds", 0.0)) * 1000.0))
	rows.append({
		"key": key,
		"special_ability": String(_module_contract_value(fields, "SpecialAbility", "")),
		"active": bool(_module_contract_value(fields, "StartsActive", false)),
		"base_max_range_from_start": bool(_module_contract_value(fields, "BaseMaxRangeFromStartPos", false)),
		"adjust_melee_position": bool(_module_contract_value(fields, "AdjustAttackMeleePosition", false)),
		"allow_self": bool(_module_contract_value(fields, "AllowSelf", false)),
		"maximum_range_source": _resolve_auto_ability_range(fields.get("MaxScanRange")),
		"minimum_range_source": _resolve_auto_ability_range(fields.get("MinScanRange")),
		"idle_ticks": _ship_contract_delay_ticks(idle_ms),
		"forbidden_status": _typed_contract_tokens(fields, "ForbiddenStatus"),
		"queries": (fields.get("Query", []) as Array).duplicate(true),
		"next_check_tick": sim.tick_index,
		"unsupported_semantics": ["melee_position_solver:AdjustAttackMeleePosition"] if bool(_module_contract_value(fields, "AdjustAttackMeleePosition", false)) else [],
	})
	row["auto_ability_behaviors"] = rows
	if not row.has("auto_ability_start_position"):
		row["auto_ability_start_position"] = row.get("position", Vector2.ZERO)


func _resolve_auto_ability_range(value: Variant) -> float:
	if typeof(value) != TYPE_DICTIONARY:
		return 0.0
	var field := value as Dictionary
	match String(field.get("kind", "literal")):
		"literal":
			return maxf(0.0, float(field.get("value", 0.0)))
		"define":
			return maxf(0.0, float((sim._rules.get("auto_ability_range_defines", {}) as Dictionary).get(String(field.get("name", "")), 0.0)))
		"subtract":
			return maxf(0.0, float((sim._rules.get("auto_ability_range_defines", {}) as Dictionary).get(String(field.get("name", "")), 0.0)) - float(field.get("amount", 0.0)))
	return 0.0


func set_auto_ability_active(entity_id: int, special_ability: String, active: bool) -> Dictionary:
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity-missing"}
	var row := sim.entities[entity_id] as Dictionary
	if not row.has("auto_ability_behaviors"):
		_attach_module_contracts(row)
	var behaviors: Array = row.get("auto_ability_behaviors", []) as Array
	for index in behaviors.size():
		var behavior := behaviors[index] as Dictionary
		if String(behavior.get("special_ability", "")) == special_ability:
			behavior["active"] = active
			behavior["next_check_tick"] = sim.tick_index
			behaviors[index] = behavior
			row["auto_ability_behaviors"] = behaviors
			return {"ok": true, "reason": ""}
	return {"ok": false, "reason": "ability-contract-missing"}


func _step_auto_abilities() -> void:
	var scale := float(sim._rules.get("source_map_transform_scale", 1.0))
	if scale <= 0.0:
		scale = 1.0
	for entity_id in sim.entity_ids():
		var row := sim.entities[entity_id] as Dictionary
		if not row.has("auto_ability_behaviors") and not row.has("module_contracts"):
			_attach_module_contracts(row)
		if int(row.get("health", 0)) <= 0:
			continue
		var behaviors: Array = row.get("auto_ability_behaviors", []) as Array
		for index in behaviors.size():
			var behavior := behaviors[index] as Dictionary
			if not bool(behavior.get("active", false)) or sim.tick_index < int(behavior.get("next_check_tick", 0)):
				continue
			behavior["next_check_tick"] = sim.tick_index + 1
			behaviors[index] = behavior
			var blocked := false
			for status_value in behavior.get("forbidden_status", []) as Array:
				if bool((row.get("object_status", {}) as Dictionary).get(String(status_value), false)):
					blocked = true
					break
			if blocked or sim.tick_index - int(row.get("last_action_tick", 0)) < int(behavior.get("idle_ticks", 0)):
				continue
			var max_range := float(behavior.get("maximum_range_source", 0.0)) * scale
			var min_range := float(behavior.get("minimum_range_source", 0.0)) * scale
			if bool(behavior.get("base_max_range_from_start", false)) and Vector2(row.get("position", Vector2.ZERO)).distance_to(Vector2(row.get("auto_ability_start_position", row.get("position", Vector2.ZERO)))) > max_range:
				continue
			var target := _auto_ability_query_target(row, behavior, min_range, max_range)
			if target < 0:
				continue
			var ability_id := _ability_id_for_special_power(row, String(behavior.get("special_ability", "")))
			if ability_id == "":
				continue
			var point = Vector2(row.get("position", Vector2.ZERO)) if target == int(row.get("id", 0)) else Vector2((sim.entities[target] as Dictionary).get("position", Vector2.ZERO))
			var result = sim.cast_ability(entity_id, ability_id, point, int(row.get("team", -1)))
			if bool(result.get("ok", false)):
				row["last_action_tick"] = sim.tick_index
		# Keep optional module state absent when this object has no authored
		# AutoAbilityBehavior.  Materializing an empty array mutates save/hash
		# state for every legacy battalion even though no behavior exists.
		if behaviors.is_empty():
			row.erase("auto_ability_behaviors")
		else:
			row["auto_ability_behaviors"] = behaviors


func _ability_id_for_special_power(row: Dictionary, special_power: String) -> String:
	for rule_value in sim._unit_ability_rules.get(String(row.get("unit_type", "")), []) as Array:
		var rule := rule_value as Dictionary
		if String(rule.get("special_power_id", "")) == special_power:
			return String(rule.get("ability_id", ""))
	return ""


func _auto_ability_query_target(source: Dictionary, behavior: Dictionary, minimum: float, maximum: float) -> int:
	var origin := Vector2(source.get("position", Vector2.ZERO))
	var queries: Array = behavior.get("queries", []) as Array
	if queries.is_empty():
		return int(source.get("id", 0)) if bool(behavior.get("allow_self", false)) else -1
	var chosen := -1
	for query_value in queries:
		var query := query_value as Dictionary
		var tokens: Array = query.get("filterTokens", []) as Array
		var matches: Array[int] = []
		for candidate_id in sim.entity_ids():
			if candidate_id == int(source.get("id", 0)) and not bool(behavior.get("allow_self", false)):
				continue
			var candidate = sim.entities[candidate_id] as Dictionary
			var distance := origin.distance_to(Vector2(candidate.get("position", origin)))
			if distance < minimum or (maximum > 0.0 and distance > maximum):
				continue
			if _auto_ability_filter_accepts(source, candidate, tokens):
				matches.append(candidate_id)
		if matches.size() < int(query.get("minimumMatches", 1)):
			return -1
		if chosen < 0 and not matches.is_empty():
			chosen = matches[0]
	return chosen


func _auto_ability_filter_accepts(source: Dictionary, candidate: Dictionary, tokens: Array) -> bool:
	var relation_filtered: Array = []
	for token_value in tokens:
		var token := String(token_value).to_upper()
		if token == "ENEMIES" and not sim._is_hostile(int(source.get("team", -1)), int(candidate.get("team", -1))):
			return false
		if token == "ALLIES" and int(source.get("team", -1)) != int(candidate.get("team", -1)):
			return false
		if token not in ["ENEMIES", "ALLIES"]:
			relation_filtered.append(token)
	return sim._ability_token_filter_accepts(candidate, relation_filtered)


# AISpecialPowerUpdate is an AI command router, not a second implementation of
# any power. It deterministically chooses a target from live simulation state,
# then enters the same sim.cast_ability()/sim.cast_power() path used by player commands.
# Consequently level, cooldown, ownership, activation filters, costs and the
# effect itself remain single-source-of-truth gameplay rules.


func _attach_ai_special_power_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields := contract.get("fields", {}) as Dictionary
	var command := String(_module_contract_value(fields, "CommandButtonName", "")).strip_edges()
	var ai_type := String(_module_contract_value(fields, "SpecialPowerAIType", "")).strip_edges().to_upper()
	if command == "" or ai_type == "":
		return
	var policies: Array = row.get("ai_special_power_updates", []) as Array
	var key := "%s:%d" % [String(contract.get("tag", "")), int(contract.get("line", 0))]
	for existing_value in policies:
		if String((existing_value as Dictionary).get("key", "")) == key:
			return
	var radius = _resolve_ai_special_power_expression(fields.get("SpecialPowerRadius"))
	var cast_range := _resolve_ai_special_power_expression(fields.get("SpecialPowerRange"))
	var unsupported: Array[String] = []
	if bool(radius.get("authored", false)) and not bool(radius.get("resolved", false)):
		unsupported.append("unresolved_radius_define:%s" % String(radius.get("expression", "")))
	if bool(cast_range.get("authored", false)) and not bool(cast_range.get("resolved", false)):
		unsupported.append("unresolved_range_define:%s" % String(cast_range.get("expression", "")))
	if not _ai_special_power_type_supported(ai_type):
		unsupported.append("unsupported_ai_type:%s" % ai_type)
	if ai_type == "AI_SPELLBOOK_TREE_KILLER":
		unsupported.append("terrain_tree_target_registry:unavailable")
	policies.append({
		"key": key,
		"command_button": command,
		"ai_type": ai_type,
		"radius_source": float(radius.get("value", 0.0)),
		"radius_authored": bool(radius.get("authored", false)),
		"range_source": float(cast_range.get("value", 0.0)),
		"range_authored": bool(cast_range.get("authored", false)),
		"spell_makes_structure": bool(_module_contract_value(fields, "SpellMakesAStructure", false)),
		"randomize_target_location": bool(_module_contract_value(fields, "RandomizeTargetLocation", false)),
		# No cadence field exists in the retail grammar. UpdateModule therefore
		# participates once per authoritative simulation update (one tick here).
		"next_check_tick": sim.tick_index,
		"attempt_count": 0,
		"cast_count": 0,
		"last_target_id": 0,
		"last_target_kind": "none",
		"last_target_point": Vector2(row.get("position", Vector2.ZERO)),
		"last_result": "never-attempted",
		"unsupported_semantics": unsupported,
	})
	row["ai_special_power_updates"] = policies


func _resolve_ai_special_power_expression(value: Variant) -> Dictionary:
	if typeof(value) != TYPE_DICTIONARY:
		return {"authored": false, "resolved": true, "value": 0.0, "expression": ""}
	var field := value as Dictionary
	var expression := String(field.get("expression", "")).strip_edges()
	if field.has("value"):
		return {"authored": true, "resolved": true, "value": maxf(0.0, float(field.get("value", 0.0))), "expression": expression}
	var defines := sim._rules.get("ai_special_power_defines", {}) as Dictionary
	if defines.has(expression) and typeof(defines[expression]) in [TYPE_INT, TYPE_FLOAT] and is_finite(float(defines[expression])) and float(defines[expression]) >= 0.0:
		return {"authored": true, "resolved": true, "value": float(defines[expression]), "expression": expression}
	return {"authored": true, "resolved": false, "value": 0.0, "expression": expression}


# De-staticed on extraction (instance sim access).
func _ai_special_power_type_supported(ai_type: String) -> bool:
	return ai_type in [
		"AI_SPECIAL_POWER_BASIC_SELF_BUFF", "AI_SPECIAL_POWER_CAPTURE_BUILDING",
		"AI_SPECIAL_POWER_CHARGE", "AI_SPECIAL_POWER_ELENDIL",
		"AI_SPECIAL_POWER_ENEMY_TYPE_KILLER", "AI_SPECIAL_POWER_ENEMY_TYPE_KILLER_RANGED",
		"AI_SPECIAL_POWER_ENEMY_TYPE_KILLER_STRUCTURES", "AI_SPECIAL_POWER_GANDALF_WIZARD_BLAST",
		"AI_SPECIAL_POWER_GIVEXP_AOE", "AI_SPECIAL_POWER_GOBLINKING_CALLOFTHEDEEP",
		"AI_SPECIAL_POWER_GOBLINKING_MOUNTED", "AI_SPECIAL_POWER_HEAL_AOE",
		"AI_SPECIAL_POWER_LEGOLAS_ARROWWIND", "AI_SPECIAL_POWER_LEGOLAS_TRAINARCHERS",
		"AI_SPECIAL_POWER_RANGED_AOE_ATTACK", "AI_SPECIAL_POWER_SELFAOEHEALHEROS",
		"AI_SPECIAL_POWER_STANCEAGGRESSIVE", "AI_SPECIAL_POWER_STANCEBATTLE",
		"AI_SPECIAL_POWER_STANCEHOLDGROUND", "AI_SPECIAL_POWER_TARGETAOE_SUMMON",
		"AI_SPECIAL_POWER_TOGGLE_MOUNTED", "AI_SPECIAL_POWER_TOGGLE_SIEGE",
		"AI_SPELLBOOK_ALWAYS_FIRE", "AI_SPELLBOOK_ARMY_BREAKER",
		"AI_SPELLBOOK_ASSIST_BATTLE_BUFF", "AI_SPELLBOOK_ASSIST_BATTLE_DEBUFF",
		"AI_SPELLBOOK_BUFFECONOMYBUILDING", "AI_SPELLBOOK_BUFFTERRAIN",
		"AI_SPELLBOOK_CALLTHEHORDE", "AI_SPELLBOOK_CAPTURE_CREEP", "AI_SPELLBOOK_CITADEL",
		"AI_SPELLBOOK_ENSHROUDINGMIST", "AI_SPELLBOOK_HEAL", "AI_SPELLBOOK_REBUILD",
		"AI_SPELLBOOK_SHROUD_REVEAL", "AI_SPELLBOOK_STRUCTURE_BASEKILL",
		"AI_SPELLBOOK_STRUCTURE_BREAKER", "AI_SPELLBOOK_STRUCTURE_BREAKER_PREF_WALLS",
		"AI_SPELLBOOK_TREE_KILLER",
	]


func _step_ai_special_power_updates() -> void:
	if not sim.ai_enabled:
		return
	for entity_id in sim.entity_ids():
		var row := sim.entities[entity_id] as Dictionary
		if not sim.team_is_ai(int(row.get("team", -1))) or int(row.get("health", 0)) <= 0:
			continue
		if not row.has("ai_special_power_updates") and row.has("module_contracts"):
			_attach_module_contracts(row)
		var policies: Array = row.get("ai_special_power_updates", []) as Array
		for index in policies.size():
			var policy := policies[index] as Dictionary
			if sim.tick_index < int(policy.get("next_check_tick", 0)):
				continue
			policy["next_check_tick"] = sim.tick_index + 1
			if not (policy.get("unsupported_semantics", []) as Array).is_empty():
				policy["last_result"] = String((policy.get("unsupported_semantics", []) as Array)[0])
				policies[index] = policy
				continue
			var ability_rule := _ai_special_power_ability_rule(
				row, String(policy.get("command_button", ""))
			)
			var blocked_condition := _ability_auto_blocked_model_condition(row, ability_rule)
			if blocked_condition != "":
				policy["last_result"] = "auto-ability-model-condition:%s" % blocked_condition
				policies[index] = policy
				continue
			var stance := _ai_special_power_stance(String(policy.get("ai_type", "")))
			if stance != "":
				if stance != _ai_special_power_desired_stance(row):
					policy["last_result"] = "stance-condition-not-met"
					policies[index] = policy
					continue
				policy["attempt_count"] = int(policy.get("attempt_count", 0)) + 1
				var stance_result := set_entity_stance(entity_id, stance)
				policy["last_result"] = String(stance_result.get("reason", ""))
				if bool(stance_result.get("ok", false)):
					policy["cast_count"] = int(policy.get("cast_count", 0)) + 1
				policies[index] = policy
				continue
			var target := _ai_special_power_target(row, policy)
			if not bool(target.get("ok", false)):
				policy["last_result"] = String(target.get("reason", "no-eligible-target"))
				policies[index] = policy
				continue
			policy["attempt_count"] = int(policy.get("attempt_count", 0)) + 1
			policy["last_target_id"] = int(target.get("id", 0))
			policy["last_target_kind"] = String(target.get("kind", "point"))
			policy["last_target_point"] = Vector2(target.get("point", row.get("position", Vector2.ZERO)))
			var result = _ai_special_power_cast(entity_id, row, policy, Vector2(policy["last_target_point"]))
			policy["last_result"] = String(result.get("reason", ""))
			if bool(result.get("ok", false)):
				policy["cast_count"] = int(policy.get("cast_count", 0)) + 1
				sim._emit_event("ai_special_power.cast", entity_id, int(policy.get("last_target_id", 0)), {"command": policy.get("command_button"), "ai_type": policy.get("ai_type"), "target_kind": policy.get("last_target_kind")})
			policies[index] = policy
			# Retail modules are authored in priority order; once one actionable
			# power succeeds, lower rows wait for the next UpdateModule pass.
			if bool(result.get("ok", false)):
				break
		row["ai_special_power_updates"] = policies


func _ability_auto_blocked_model_condition(row: Dictionary, rule: Dictionary) -> String:
	var effect := rule.get("effect", {}) as Dictionary
	if not bool(effect.get("autoAbility", false)):
		return ""
	var active: Dictionary = {}
	for condition_value in row.get("model_conditions", []) as Array:
		active[String(condition_value).to_upper()] = true
	for condition_value in (row.get("object_status", {}) as Dictionary).keys():
		if bool((row.get("object_status", {}) as Dictionary).get(condition_value, false)):
			active[String(condition_value).to_upper()] = true
	if not (row.get("route", []) as Array).is_empty() or float(row.get("current_speed", 0.0)) > 0.0:
		active["MOVING"] = true
	for blocked_value in effect.get("autoAbilityBlockedModelConditions", []) as Array:
		var blocked := String(blocked_value).to_upper()
		if active.has(blocked):
			return blocked
	return ""


func _ai_special_power_cast(entity_id: int, row: Dictionary, policy: Dictionary, point: Vector2) -> Dictionary:
	var command := String(policy.get("command_button", ""))
	if command.begins_with("Command_SpellBook"):
		return sim.cast_power(int(row.get("team", -1)), command.trim_prefix("Command_"), point)
	return sim.cast_ability(entity_id, command, point, int(row.get("team", -1)))


# De-staticed on extraction (instance sim access).
func _ai_special_power_stance(ai_type: String) -> String:
	match ai_type:
		"AI_SPECIAL_POWER_STANCEAGGRESSIVE": return "Aggressive"
		"AI_SPECIAL_POWER_STANCEBATTLE": return "Battle"
		"AI_SPECIAL_POWER_STANCEHOLDGROUND": return "HoldGround"
	return ""


# De-staticed on extraction (instance sim access).
func _ai_special_power_desired_stance(row: Dictionary) -> String:
	if bool(row.get("hold_ground", false)):
		return "HoldGround"
	if int(row.get("target_id", 0)) != 0 or String(row.get("state", "")) == "attack":
		return "Aggressive"
	return "Battle"


func _ai_special_power_target(source: Dictionary, policy: Dictionary) -> Dictionary:
	var ai_type := String(policy.get("ai_type", ""))
	var origin := Vector2(source.get("position", Vector2.ZERO))
	if _ai_special_power_is_self(ai_type):
		return {"ok": true, "id": int(source.get("id", 0)), "kind": "self", "point": origin}
	var scale := maxf(0.000001, float(sim._rules.get("source_map_transform_scale", 1.0)))
	var command_rule := _ai_special_power_ability_rule(source, String(policy.get("command_button", "")))
	var effect := command_rule.get("effect", {}) as Dictionary
	var search_range := float(policy.get("range_source", 0.0)) * scale
	if not bool(policy.get("range_authored", false)):
		search_range = float(effect.get("range", source.get("vision_range", 0.0)))
		if search_range <= 0.0:
			search_range = float(source.get("vision_range", 0.0))
	var radius = float(policy.get("radius_source", 0.0)) * scale
	if not bool(policy.get("radius_authored", false)):
		for radius_key in ["damage_radius", "radius_scaled", "target_radius_scaled"]:
			radius = maxf(radius, float(effect.get(radius_key, 0.0)))
	var candidates: Array[Dictionary] = []
	if _ai_special_power_targets_structure(ai_type):
		var allied_structure := ai_type in ["AI_SPELLBOOK_BUFFECONOMYBUILDING", "AI_SPELLBOOK_REBUILD", "AI_SPELLBOOK_CITADEL"]
		for structure_id in sim.structure_ids():
			var structure := sim.structures[structure_id] as Dictionary
			if int(structure.get("health", 0)) <= 0:
				continue
			var capture := ai_type == "AI_SPECIAL_POWER_CAPTURE_BUILDING"
			if capture and (int(structure.get("team", -1)) != sim.NEUTRAL_TEAM or not bool(structure.get("capturable", false))):
				continue
			if not capture and allied_structure != (int(source.get("team", -1)) == int(structure.get("team", -1))):
				continue
			if not capture and not allied_structure and not sim._is_hostile(int(source.get("team", -1)), int(structure.get("team", -1))):
				continue
			if ai_type == "AI_SPELLBOOK_REBUILD" and int(structure.get("health", 0)) >= int(structure.get("maximum_health", structure.get("health", 0))):
				continue
			if ai_type == "AI_SPELLBOOK_BUFFECONOMYBUILDING" and int(structure.get("income_per_payout", 0)) <= 0:
				continue
			if ai_type == "AI_SPELLBOOK_CITADEL" and String(structure.get("structure_kind", "")).to_lower() not in ["fortress", "citadel"]:
				continue
			var point = Vector2(structure.get("position", origin))
			var distance := origin.distance_to(point)
			if search_range > 0.0 and distance > search_range:
				continue
			var structure_score := _ai_special_power_cluster_score(source, point, radius, allied_structure)
			var structure_kind := String(structure.get("structure_kind", "")).to_lower()
			if ai_type == "AI_SPELLBOOK_STRUCTURE_BASEKILL" and structure_kind in ["fortress", "citadel"]:
				structure_score += 10000
			if ai_type == "AI_SPELLBOOK_STRUCTURE_BREAKER_PREF_WALLS" and "wall" in structure_kind:
				structure_score += 10000
			candidates.append({"id": structure_id, "kind": "structure", "point": point, "distance": distance, "score": structure_score})
	else:
		var allies := _ai_special_power_targets_allies(ai_type)
		for candidate_id in sim.entity_ids():
			var candidate = sim.entities[candidate_id] as Dictionary
			if int(candidate.get("health", 0)) <= 0:
				continue
			var same_team := int(candidate.get("team", -1)) == int(source.get("team", -1))
			if allies != same_team or (not allies and not sim._is_hostile(int(source.get("team", -1)), int(candidate.get("team", -1)))):
				continue
			if ai_type == "AI_SPECIAL_POWER_HEAL_AOE" and int(candidate.get("health", 0)) >= int(candidate.get("maximum_health", candidate.get("health", 0))):
				continue
			var point = Vector2(candidate.get("position", origin))
			var distance := origin.distance_to(point)
			if search_range > 0.0 and distance > search_range:
				continue
			if bool(policy.get("spell_makes_structure", false)) and not _ai_special_power_structure_site_clear(point, maxf(radius, 1.0)):
				continue
			candidates.append({"id": candidate_id, "kind": "battalion", "point": point, "distance": distance, "score": _ai_special_power_cluster_score(source, point, radius, allies)})
	if candidates.is_empty():
		return {"ok": false, "reason": "no-eligible-target"}
	candidates.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		var score_a := int(a.get("score", 0)); var score_b := int(b.get("score", 0))
		if score_a != score_b: return score_a > score_b
		var distance_a := float(a.get("distance", 0.0)); var distance_b := float(b.get("distance", 0.0))
		return distance_a < distance_b or (is_equal_approx(distance_a, distance_b) and int(a.get("id", 0)) < int(b.get("id", 0)))
	)
	var selected := 0
	if bool(policy.get("randomize_target_location", false)) and candidates.size() > 1:
		selected = sim.logic_random_int(0, candidates.size() - 1)
	var output := candidates[selected].duplicate(true)
	output["ok"] = true
	return output


func _ai_special_power_ability_rule(source: Dictionary, command: String) -> Dictionary:
	for rule_value in sim._unit_ability_rules.get(String(source.get("unit_type", "")), []) as Array:
		var rule := rule_value as Dictionary
		if String(rule.get("ability_id", "")) == command:
			return rule
	return {}


# De-staticed on extraction (instance sim access).
func _ai_special_power_is_self(ai_type: String) -> bool:
	return ai_type in ["AI_SPECIAL_POWER_BASIC_SELF_BUFF", "AI_SPECIAL_POWER_CHARGE", "AI_SPECIAL_POWER_ELENDIL", "AI_SPECIAL_POWER_GOBLINKING_MOUNTED", "AI_SPECIAL_POWER_SELFAOEHEALHEROS", "AI_SPECIAL_POWER_TOGGLE_MOUNTED", "AI_SPECIAL_POWER_TOGGLE_SIEGE", "AI_SPELLBOOK_ALWAYS_FIRE", "AI_SPELLBOOK_CALLTHEHORDE", "AI_SPELLBOOK_SHROUD_REVEAL"]


# De-staticed on extraction (instance sim access).
func _ai_special_power_targets_allies(ai_type: String) -> bool:
	return ai_type in ["AI_SPECIAL_POWER_GIVEXP_AOE", "AI_SPECIAL_POWER_HEAL_AOE", "AI_SPECIAL_POWER_LEGOLAS_TRAINARCHERS", "AI_SPELLBOOK_ASSIST_BATTLE_BUFF", "AI_SPELLBOOK_BUFFECONOMYBUILDING", "AI_SPELLBOOK_BUFFTERRAIN", "AI_SPELLBOOK_ENSHROUDINGMIST", "AI_SPELLBOOK_HEAL", "AI_SPELLBOOK_REBUILD"]


# De-staticed on extraction (instance sim access).
func _ai_special_power_targets_structure(ai_type: String) -> bool:
	return ai_type in ["AI_SPECIAL_POWER_CAPTURE_BUILDING", "AI_SPECIAL_POWER_ENEMY_TYPE_KILLER_STRUCTURES", "AI_SPELLBOOK_BUFFECONOMYBUILDING", "AI_SPELLBOOK_CITADEL", "AI_SPELLBOOK_REBUILD", "AI_SPELLBOOK_STRUCTURE_BASEKILL", "AI_SPELLBOOK_STRUCTURE_BREAKER", "AI_SPELLBOOK_STRUCTURE_BREAKER_PREF_WALLS"]


func _ai_special_power_cluster_score(source: Dictionary, point: Vector2, radius: float, allies: bool) -> int:
	if radius <= 0.0:
		return 1
	var score := 0
	for candidate_id in sim.entity_ids():
		var candidate = sim.entities[candidate_id] as Dictionary
		if int(candidate.get("health", 0)) <= 0 or Vector2(candidate.get("position", point)).distance_to(point) > radius:
			continue
		var same_team := int(candidate.get("team", -1)) == int(source.get("team", -1))
		if allies == same_team and (allies or sim._is_hostile(int(source.get("team", -1)), int(candidate.get("team", -1)))):
			score += 1
	return score


func _ai_special_power_structure_site_clear(point: Vector2, radius: float) -> bool:
	for structure_id in sim.structure_ids():
		var structure := sim.structures[structure_id] as Dictionary
		if int(structure.get("health", 0)) > 0 and Vector2(structure.get("position", point)).distance_to(point) <= radius:
			return false
	return true


func _attach_respawn_update_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("respawn_update"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var rules := fields.get("RespawnRules", {}) as Dictionary
	var receipts: Array[String] = []
	for key in ["DeathAnim", "DeathFX", "InitialSpawnFX", "RespawnAnim", "RespawnFX", "ButtonImage"]:
		if fields.has(key):
			receipts.append("presentation_binding:%s" % key)
	row["respawn_update"] = {
		"auto_spawn": bool(rules.get("autoSpawn", false)),
		"cost": maxi(0, int(rules.get("cost", 0))),
		"time_ticks": _ship_contract_delay_ticks(float(rules.get("timeMilliseconds", 0.0))),
		"health_fraction": clampf(float(rules.get("healthPercent", 100.0)) / 100.0, 0.0, 1.0),
		"anchor_filter": _typed_contract_tokens(fields, "AutoRespawnAtObjectFilter"),
		"respawn_as_template": String(_module_contract_value(fields, "RespawnAsTemplate", "")),
		"entries": (fields.get("RespawnEntry", []) as Array).duplicate(true),
		"death_animation_ticks": _ship_contract_delay_ticks(float(_module_contract_value(fields, "DeathAnimationTime", 0.0))),
		"respawn_animation_ticks": _ship_contract_delay_ticks(float(_module_contract_value(fields, "RespawnAnimationTime", 0.0))),
		"unsupported_semantics": receipts,
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}


func _schedule_respawn_update(entity_id: int, row: Dictionary, death_type:String="NORMAL", attacker_id:int=0) -> void:
	if not row.has("respawn_update"):
		_attach_module_contracts(row)
	var policy := row.get("respawn_update", {}) as Dictionary
	if policy.is_empty() or sim.respawn_schedules.has(entity_id):
		return
	var body:=row.get("respawn_body",{}) as Dictionary
	if not body.is_empty():
		if not bool(body.get("can_respawn",true)):
			sim._emit_event("respawn.blocked",entity_id,attacker_id,{"reason":"RespawnBody.CanRespawn","death_type":death_type});return
		var killer_filter:=body.get("permanently_killed_filter",[]) as Array
		if not killer_filter.is_empty() and sim.entities.has(attacker_id) and sim._transport_filter_accepts(sim.entities[attacker_id] as Dictionary,killer_filter):
			sim._emit_event("respawn.blocked",entity_id,attacker_id,{"reason":"RespawnBody.PermanentlyKilledByFilter","death_type":death_type});return
	var level := int(row.get("level", 1))
	var cost := int(policy.get("cost", 0))
	var delay := int(policy.get("time_ticks", 0))
	for entry_value in policy.get("entries", []) as Array:
		var entry := entry_value as Dictionary
		if int(entry.get("level", 0)) == level:
			cost = maxi(0, int(entry.get("cost", cost)))
			delay = _ship_contract_delay_ticks(float(entry.get("timeMilliseconds", delay * sim.TICK_SECONDS * 1000.0)))
			break
	sim.respawn_schedules[entity_id] = {
		"entity": row.duplicate(true),
		"ready_tick": sim.tick_index + maxi(0, delay),
		"cost": cost,
		"requested": bool(policy.get("auto_spawn", false)),
	}
	sim._emit_event("respawn.scheduled", entity_id, 0, {"ready_tick": sim.tick_index + maxi(0, delay), "cost": cost, "auto_spawn": bool(policy.get("auto_spawn", false))})


func request_respawn(entity_id: int) -> Dictionary:
	if not sim.respawn_schedules.has(entity_id):
		return {"ok": false, "reason": "respawn-not-scheduled"}
	var schedule := sim.respawn_schedules[entity_id] as Dictionary
	var row := schedule.get("entity", {}) as Dictionary
	var team := int(row.get("team", -1))
	var cost := int(schedule.get("cost", 0))
	if sim.resources_for_team(team) < cost:
		return {"ok": false, "reason": "insufficient-resources", "cost": cost}
	sim.team_resources[team] = sim.resources_for_team(team) - cost
	schedule["cost_paid"] = true
	schedule["requested"] = true
	sim.respawn_schedules[entity_id] = schedule
	return {"ok": true, "reason": ""}


func _step_respawn_updates() -> void:
	for entity_id_value in sim.respawn_schedules.keys().duplicate():
		var entity_id := int(entity_id_value)
		var schedule := sim.respawn_schedules[entity_id] as Dictionary
		if not bool(schedule.get("requested", false)) or sim.tick_index < int(schedule.get("ready_tick", 0)):
			continue
		var row := (schedule.get("entity", {}) as Dictionary).duplicate(true)
		var policy := row.get("respawn_update", {}) as Dictionary
		var team := int(row.get("team", -1))
		var anchor_id := _respawn_anchor(team, policy.get("anchor_filter", []) as Array)
		if anchor_id == 0:
			continue
		if not bool(schedule.get("cost_paid", false)):
			var cost := int(schedule.get("cost", 0))
			if sim.resources_for_team(team) < cost:
				continue
			sim.team_resources[team] = sim.resources_for_team(team) - cost
		var template := String(policy.get("respawn_as_template", ""))
		if template != "":
			row["unit_type"] = template
			row["object_id"] = template
		row["position"] = (sim.structures[anchor_id] as Dictionary).get("position", row.get("position", Vector2.ZERO))
		var member_max := maxi(1, int(row.get("member_maximum_health", 1)))
		var health_values: Array = row.get("member_health", []) as Array
		var revived_health := maxi(1, roundi(float(member_max) * float(policy.get("health_fraction", 1.0))))
		for index in health_values.size():
			health_values[index] = revived_health
		row["member_health"] = health_values
		row["health"] = revived_health * health_values.size()
		row["maximum_health"] = member_max * health_values.size()
		row["state"] = "idle"
		row["target_id"] = 0
		row["corpse_expire_tick"] = -1
		row.erase("death_tick")
		row["respawn_animation_until_tick"] = sim.tick_index + int(policy.get("respawn_animation_ticks", 0))
		sim.entities[entity_id] = row
		sim._spatial_sync(row)
		sim.respawn_schedules.erase(entity_id)
		sim._emit_event("respawn.completed", entity_id, anchor_id, {"template": template, "health": int(row.get("health", 0))})


func _respawn_anchor(team: int, filter: Array) -> int:
	for structure_id in sim.structure_ids(team):
		var structure := sim.structures[structure_id] as Dictionary
		if int(structure.get("health", 0)) <= 0:
			continue
		if filter.is_empty() or sim._transport_filter_accepts({"category":String(structure.get("structure_kind", "")), "kind_of":structure.get("kind_of", [])}, filter):
			return structure_id
	return 0


func _attach_fire_weapon_update_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("fire_weapon_updates"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var nuggets: Array[Dictionary] = []
	for nugget_value in fields.get("FireWeaponNugget", []) as Array:
		var nugget := nugget_value as Dictionary
		var delay := _ship_contract_delay_ticks(float(_module_contract_value(nugget, "FireDelay", 0.0)))
		var offset := Vector2.ZERO
		var offset_z := 0.0
		var offset_value: Variant = _module_contract_value(nugget, "Offset", null)
		if typeof(offset_value) == TYPE_DICTIONARY:
			var coord := offset_value as Dictionary
			offset = Vector2(float(coord.get("x", 0.0)), float(coord.get("y", 0.0)))
			offset_z = float(coord.get("z", 0.0))
		nuggets.append({"weapon":String(_module_contract_value(nugget,"WeaponName","")),"one_shot":bool(_module_contract_value(nugget,"OneShot",false)),"delay_ticks":maxi(1,delay),"next_fire_tick":sim.tick_index+delay,"offset_source":offset,"offset_z_source":offset_z,"fired":false})
	row["fire_weapon_updates"] = {
		"charging_trigger": bool(_module_contract_value(fields,"ChargingModeTrigger",false)),
		"alive_only": bool(_module_contract_value(fields,"AliveOnly",false)),
		"hero_mode_trigger": bool(_module_contract_value(fields,"HeroModeTrigger",false)),
		"nuggets": nuggets,
	}


func _step_fire_weapon_updates() -> void:
	for table_value in [sim.entities, sim.structures]:
		var table = table_value as Dictionary
		var ids: Array = table.keys()
		ids.sort()
		for id_value in ids:
			var id = int(id_value)
			var row := table[id] as Dictionary
			if not row.has("fire_weapon_updates") and not row.has("module_contracts"):
				if table == sim.entities:
					_attach_module_contracts(row)
				else:
					_attach_structure_module_contracts(row)
			var policy := row.get("fire_weapon_updates", {}) as Dictionary
			if policy.is_empty() or (bool(policy.get("alive_only",false)) and int(row.get("health",0))<=0):
				continue
			if bool(policy.get("charging_trigger",false)) and not bool(row.get("charging_mode",false)):
				continue
			if bool(policy.get("hero_mode_trigger",false)) and not bool(row.get("hero_mode",false)):
				continue
			var nuggets: Array = policy.get("nuggets",[]) as Array
			for index in nuggets.size():
				var nugget := nuggets[index] as Dictionary
				if (bool(nugget.get("one_shot",false)) and bool(nugget.get("fired",false))) or sim.tick_index<int(nugget.get("next_fire_tick",0)):
					continue
				var weapon := String(nugget.get("weapon",""))
				var facing := Vector2(row.get("facing",Vector2.RIGHT)).normalized()
				var local = sim._retail_source_to_sim_offset(Vector2(nugget.get("offset_source",Vector2.ZERO)))
				var point = Vector2(row.get("position",Vector2.ZERO))+local.rotated(facing.angle())
				sim._fire_death_weapon({"weapon_id":weapon,"weapon_rule":(sim._death_weapon_rules.get(weapon,{}) as Dictionary).duplicate(true),"point":point,"team":int(row.get("team",-1)),"source_id":id,"height_source":float(nugget.get("offset_z_source",0.0)),"death_type":"FIRE_WEAPON_UPDATE"})
				nugget["fired"] = true
				nugget["next_fire_tick"] = sim.tick_index + maxi(1,int(nugget.get("delay_ticks",1)))
				nuggets[index] = nugget
				sim._emit_event("module.fire_weapon_update",id,0,{"weapon_id":weapon,"one_shot":bool(nugget.get("one_shot",false))})
			policy["nuggets"] = nuggets
			row["fire_weapon_updates"] = policy


func _attach_deletion_update_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("deletion_update"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var low := _resolve_deletion_bound(fields.get("MinLifetime"))
	var high := _resolve_deletion_bound(fields.get("MaxLifetime"))
	if bool(low.get("indefinite",false)) or bool(high.get("indefinite",false)):
		row["deletion_update"]={"indefinite":true,"unsupported_semantics":[]}
		return
	if not bool(low.get("resolved",false)) or not bool(high.get("resolved",false)):
		row["deletion_update"]={"indefinite":false,"unsupported_semantics":["unresolved_lifetime_expression"],"expire_tick":-1}
		return
	var low_ticks:=_ship_contract_delay_ticks(float(low.get("milliseconds",0.0)))
	var high_ticks:=maxi(low_ticks,_ship_contract_delay_ticks(float(high.get("milliseconds",0.0))))
	var lifetime =low_ticks if low_ticks==high_ticks else sim.logic_random_int(low_ticks,high_ticks)
	row["deletion_update"]={"indefinite":false,"expire_tick":sim.tick_index+lifetime,"selected_ticks":lifetime,"unsupported_semantics":[]}


func _resolve_deletion_bound(value: Variant) -> Dictionary:
	if typeof(value)!=TYPE_DICTIONARY:return {"resolved":false}
	var bound:=value as Dictionary
	if bool(bound.get("indefinite",false)):return {"resolved":true,"indefinite":true}
	if bound.has("milliseconds"):return {"resolved":true,"indefinite":false,"milliseconds":float(bound.get("milliseconds",0.0))}
	var name:=String(bound.get("name",bound.get("expression","")))
	var defines:=sim._rules.get("deletion_lifetime_defines",{}) as Dictionary
	if typeof(defines.get(name)) in [TYPE_INT,TYPE_FLOAT]:return {"resolved":true,"indefinite":false,"milliseconds":float(defines[name])}
	return {"resolved":false,"indefinite":false}


func _step_deletion_updates() -> void:
	for id in sim.entity_ids():
		var row:=sim.entities[id] as Dictionary
		if not row.has("deletion_update") and not row.has("module_contracts"):_attach_module_contracts(row)
		var policy:=row.get("deletion_update",{}) as Dictionary
		if not bool(policy.get("indefinite",false)) and int(policy.get("expire_tick",-1))>=0 and sim.tick_index>=int(policy.get("expire_tick",-1)):
			_expire_lifetime_entity(id,row,"FADED")
	for id in sim.structure_ids():
		var row:=sim.structures[id] as Dictionary
		if not row.has("deletion_update") and not row.has("module_contracts"):_attach_structure_module_contracts(row)
		var policy:=row.get("deletion_update",{}) as Dictionary
		if not bool(policy.get("indefinite",false)) and int(policy.get("expire_tick",-1))>=0 and sim.tick_index>=int(policy.get("expire_tick",-1)):
			_expire_lifetime_structure(id,row,"FADED")


func _attach_production_update_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("production_update"): return
	var fields:=contract.get("fields",{}) as Dictionary
	var modifiers:Array[Dictionary]=[]
	for value in fields.get("ProductionModifier",[]) as Array:
		var source:=value as Dictionary
		modifiers.append({"required_upgrade":String(_module_contract_value(source,"RequiredUpgrade","")),"cost_multiplier":float(_module_contract_value(source,"CostMultiplier",1.0)),"time_multiplier":float(_module_contract_value(source,"TimeMultiplier",1.0)),"filter":_typed_contract_tokens(source,"ModifierFilter"),"hero_purchase":bool(_module_contract_value(source,"HeroPurchase",false)),"hero_revive":bool(_module_contract_value(source,"HeroRevive",false))})
	var receipts:Array[String]=[]
	for key in ["NumDoorAnimations","DoorOpeningTime","DoorWaitOpenTime","DoorCloseTime","ConstructionCompleteDuration","SetBonusModelConditionOnSpeedBonus","BonusForType","SpeedBonusAudioLoop"]:
		if fields.has(key):receipts.append("presentation_binding:%s"%key)
	for key in ["GiveNoXP","VeteranUnitsFromVeteranFactory","UnitInvulnerableTime"]:
		if fields.has(key):receipts.append("unsupported_production_semantic:%s"%key)
	for modifier in modifiers:
		if bool(modifier.get("hero_revive",false)):
			receipts.append("unsupported_production_semantic:ProductionModifier.HeroRevive")
	# MaxQueueEntries is authored on 2 of 423 retail ProductionUpdate blocks
	# (angmarthrallmaster.ini:587, dwarvenbattlewagon.ini:492, both `= 1`).
	# Absent must land as 0 = UNCAPPED, never as an invented default.
	row["production_update"]={"maximum_queue_entries":int(_module_contract_value(fields,"MaxQueueEntries",0)),"give_no_xp":bool(_module_contract_value(fields,"GiveNoXP",false)),"veteran_units":bool(_module_contract_value(fields,"VeteranUnitsFromVeteranFactory",false)),"unit_invulnerable_ticks":_ship_contract_delay_ticks(float(_module_contract_value(fields,"UnitInvulnerableTime",0.0))),"modifiers":modifiers,"unsupported_semantics":receipts}


func _attach_getting_built_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("getting_built_behavior"):return
	var fields:=contract.get("fields",{}) as Dictionary
	var spawn:=_resolve_build_seconds(fields.get("SpawnTimer"))
	var rebuild:=_resolve_build_seconds(fields.get("RebuildTimeSeconds"))
	var receipts:Array[String]=[]
	for key in ["WorkerName","EvilWorkerName","SelfBuildingLoop","SelfRepairFromDamageLoop","SelfRepairFromRubbleLoop","TestFaction"]:
		if fields.has(key):receipts.append("presentation_or_worker_binding:%s"%key)
	if not bool(spawn.get("resolved",false)) and not bool(spawn.get("disabled",false)):receipts.append("unresolved_define:SpawnTimer")
	if fields.has("RebuildTimeSeconds") and not bool(rebuild.get("resolved",false)):receipts.append("unresolved_define:RebuildTimeSeconds")
	if bool(spawn.get("resolved",false)) and not bool(spawn.get("disabled",false)):receipts.append("unsupported_construction_semantic:SpawnTimer")
	if bool(rebuild.get("resolved",false)):receipts.append("unsupported_construction_semantic:RebuildTimeSeconds")
	for key in ["RebuildWhenDead","UseSpawnTimerWithoutWorker","DisallowRebuildRange","DisallowRebuildFilter"]:
		if fields.has(key):receipts.append("unsupported_construction_semantic:%s"%key)
	row["getting_built_behavior"]={"spawn_disabled":bool(spawn.get("disabled",false)),"spawn_ticks":int(spawn.get("ticks",-1)),"rebuild_ticks":int(rebuild.get("ticks",-1)),"rebuild_when_dead":bool(_module_contract_value(fields,"RebuildWhenDead",false)),"use_timer_without_worker":bool(_module_contract_value(fields,"UseSpawnTimerWithoutWorker",false)),"disallow_range_source":float(_module_contract_value(fields,"DisallowRebuildRange",0.0)),"disallow_filter":_typed_contract_tokens(fields,"DisallowRebuildFilter"),"unsupported_semantics":receipts}


func _resolve_build_seconds(value:Variant)->Dictionary:
	if typeof(value)!=TYPE_DICTIONARY:return {"resolved":false,"disabled":false}
	var field:=value as Dictionary
	if bool(field.get("disabled",false)):return {"resolved":true,"disabled":true,"ticks":-1}
	if field.has("milliseconds"):return {"resolved":true,"disabled":false,"ticks":_ship_contract_delay_ticks(float(field.get("milliseconds",0.0)))}
	var name:=String(field.get("define",""));var defines:=sim._rules.get("getting_built_time_defines",{}) as Dictionary
	if typeof(defines.get(name)) in [TYPE_INT,TYPE_FLOAT]:return {"resolved":true,"disabled":false,"ticks":_ship_contract_delay_ticks(float(defines[name])*1000.0)}
	return {"resolved":false,"disabled":false,"ticks":-1}


func _attach_building_behavior_contract(row:Dictionary,contract:Dictionary)->void:
	if String(contract.get("extraction",""))!="typed" or row.has("building_behavior"):return
	var fields:=contract.get("fields",{}) as Dictionary;var receipts:Array[String]=[]
	for key in ["NightWindowName","FireWindowName","GlowWindowName","FireName"]:
		if fields.has(key):receipts.append("model_subobject_binding:%s"%key)
	var fire_names:Array[String]=[]
	for fire_value in fields.get("FireName",[]) as Array:
		var name:=String((fire_value as Dictionary).get("value","")).strip_edges()
		if name!="":fire_names.append(name)
	row["building_behavior"]={
		"night_windows":_typed_contract_tokens(fields,"NightWindowName"),
		"fire_windows":_typed_contract_tokens(fields,"FireWindowName"),
		"glow_windows":_typed_contract_tokens(fields,"GlowWindowName"),
		"fire_names":fire_names,
		# Rendering/model visibility is outside the deterministic sim.  Preserve
		# every authored ordered binding and emit an explicit consumer receipt.
		"unsupported_semantics":receipts,
	}


func _attach_queue_production_exit_contract(row:Dictionary,contract:Dictionary)->void:
	if String(contract.get("extraction",""))!="typed" or row.has("queue_production_exit_update"):return
	var fields:=contract.get("fields",{}) as Dictionary;var points:Array[Vector2]=[];var rallies:Array[Vector2]=[];var receipts:Array[String]=[]
	for value in fields.get("UnitCreatePoint",[]) as Array:
		var point =value as Dictionary
		if not bool(point.get("validNumeric",false)) or typeof(point.get("value"))!=TYPE_DICTIONARY:
			receipts.append("invalid_numeric_coordinate:UnitCreatePoint");continue
		var coord:=point.get("value",{}) as Dictionary;points.append(Vector2(float(coord.get("x",0.0)),float(coord.get("y",0.0))))
	for value in fields.get("NaturalRallyPoint",[]) as Array:
		var point =value as Dictionary
		if not bool(point.get("validNumeric",false)) or typeof(point.get("value"))!=TYPE_DICTIONARY:
			receipts.append("invalid_numeric_coordinate:NaturalRallyPoint");continue
		var coord:=point.get("value",{}) as Dictionary;rallies.append(Vector2(float(coord.get("x",0.0)),float(coord.get("y",0.0))))
	var delays:Array[int]=[]
	for value in fields.get("ExitDelay",[]) as Array:
		var delay:=value as Dictionary
		if typeof(delay.get("milliseconds")) in [TYPE_INT,TYPE_FLOAT]:delays.append(_ship_contract_delay_ticks(float(delay.get("milliseconds",0.0))))
		else:
			var expression:=String(delay.get("expression",""));var defines:=sim._rules.get("queue_exit_time_defines",{}) as Dictionary
			if typeof(defines.get(expression)) in [TYPE_INT,TYPE_FLOAT]:delays.append(_ship_contract_delay_ticks(float(defines[expression])))
			else:receipts.append("unresolved_exit_delay:%s"%expression)
	if fields.has("AllowAirborneCreation"):receipts.append("presentation_or_movement_binding:AllowAirborneCreation")
	if fields.has("UseReturnToFormation"):receipts.append("presentation_or_movement_binding:UseReturnToFormation")
	var executable:=not points.is_empty() and not receipts.any(func(receipt:String)->bool:return receipt.begins_with("invalid_numeric_coordinate:") or receipt.begins_with("unresolved_exit_delay:"))
	if fields.has("InitialBurst"):receipts.append("unsupported_exit_semantic:InitialBurst")
	row["queue_production_exit_update"]={"executable":executable,"create_points_source":points,"rally_points_source":rallies,"placement_angles":_typed_contract_numbers(fields,"PlacementViewAngle"),"exit_delay_ticks":delays,"initial_burst":int(_module_contract_value(fields,"InitialBurst",0)),"no_exit_path":bool(_module_contract_value(fields,"NoExitPath",false)),"next_index":0,"unsupported_semantics":receipts}


func _attach_rebuild_hole_expose_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("rebuild_hole_expose"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var hole_name := String(_module_contract_value(fields, "HoleName", ""))
	var hole_health := int(round(float(_module_contract_value(fields, "HoleMaxHealth", 0.0))))
	if hole_name == "" or hole_health <= 0:
		return
	var transfer_attackers := bool(_module_contract_value(fields, "TransferAttackers", false))
	var fade_in_ticks := maxi(0, _ship_contract_delay_ticks(float(_module_contract_value(fields, "FadeInTimeSeconds", 0.0)) * 1000.0))
	var receipts: Array[String] = []
	if fade_in_ticks > 0:
		receipts.append("presentation_binding:FadeInTimeSeconds")
	if transfer_attackers:
		receipts.append("unsupported_rebuild_semantic:TransferAttackers")
	row["rebuild_hole_expose"] = {
		"hole_object_id": hole_name,
		"hole_max_health": hole_health,
		"fade_in_ticks": fade_in_ticks,
		"exempt_statuses": _typed_contract_tokens(fields, "ExemptStatus"),
		"transfer_attackers": transfer_attackers,
		"unsupported_semantics": receipts,
		"exposed": false,
	}


func _attach_rebuild_hole_behavior_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("rebuild_hole_behavior"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var delay_value := fields.get("WorkerRespawnDelay", {}) as Dictionary
	var regen_value := fields.get("HoleHealthRegen%PerSecond", {}) as Dictionary
	if typeof(delay_value.get("milliseconds")) not in [TYPE_INT, TYPE_FLOAT] or typeof(regen_value.get("ratio")) not in [TYPE_INT, TYPE_FLOAT]:
		return
	row["rebuild_hole_behavior"] = {
		"worker_object_id": String(_module_contract_value(fields, "WorkerObjectName", "")),
		"respawn_ticks": _ship_contract_delay_ticks(float(delay_value.get("milliseconds", 0.0))),
		"health_regen_ratio_per_second": float(regen_value.get("ratio", 0.0)),
		"regen_remainder": 0.0,
	}


func _expose_rebuild_hole(owner_id: int, owner: Dictionary, attacker_id: int) -> int:
	var policy := owner.get("rebuild_hole_expose", {}) as Dictionary
	if policy.is_empty() or bool(policy.get("exposed", false)):
		return -1
	if bool(policy.get("transfer_attackers", false)):
		sim._emit_event("rebuild_hole.expose_refused", attacker_id, owner_id, {"reason": "TransferAttackers-unresolved"})
		return -1
	var statuses: Array[String] = []
	var status_value: Variant = owner.get("object_status", [])
	if typeof(status_value) == TYPE_ARRAY:
		for value in status_value as Array: statuses.append(String(value).to_upper())
	elif typeof(status_value) == TYPE_DICTIONARY:
		for value in (status_value as Dictionary).keys():
			if bool((status_value as Dictionary)[value]): statuses.append(String(value).to_upper())
	for status in policy.get("exempt_statuses", []) as Array:
		if statuses.has(String(status).to_upper()):
			return -1
	var hole_id := spawn_scenario_structure(
		String(policy.get("hole_object_id", "")), int(owner.get("team", -1)),
		Vector2(owner.get("position", Vector2.ZERO)), "object-creation-list"
	)
	if hole_id <= 0 or not sim.structures.has(hole_id):
		sim._emit_event("rebuild_hole.expose_refused", attacker_id, owner_id, {"hole_object_id": String(policy.get("hole_object_id", ""))})
		return -1
	var hole := sim.structures[hole_id] as Dictionary
	var maximum := int(policy.get("hole_max_health", 0))
	hole["maximum_health"] = maximum
	hole["health"] = maximum
	hole["rebuild_owner_object_id"] = String(owner.get("source_object_id", owner.get("object_id", "")))
	hole["rebuild_owner_team"] = int(owner.get("team", -1))
	hole["rebuild_owner_id"] = owner_id
	hole["rebuild_owner_position"] = Vector2(owner.get("position", Vector2.ZERO))
	hole["rebuild_transfer_attacker_id"] = 0
	var rebuild := hole.get("rebuild_hole_behavior", {}) as Dictionary
	if not rebuild.is_empty():
		rebuild["respawn_tick"] = sim.tick_index + int(rebuild.get("respawn_ticks", 0))
		hole["rebuild_hole_behavior"] = rebuild
	policy["exposed"] = true
	policy["hole_id"] = hole_id
	owner["rebuild_hole_expose"] = policy
	sim._emit_event("rebuild_hole.exposed", owner_id, hole_id, {"hole_object_id": String(policy.get("hole_object_id", "")), "maximum_health": maximum})
	return hole_id


func _step_rebuild_holes() -> void:
	for hole_id in sim.structure_ids():
		if not sim.structures.has(hole_id):
			continue
		var hole := sim.structures[hole_id] as Dictionary
		var policy := hole.get("rebuild_hole_behavior", {}) as Dictionary
		if policy.is_empty() or int(hole.get("health", 0)) <= 0 or not hole.has("rebuild_owner_object_id"):
			continue
		var maximum := maxi(1, int(hole.get("maximum_health", 1)))
		var regen = float(policy.get("health_regen_ratio_per_second", 0.0)) * float(maximum) * sim.TICK_SECONDS + float(policy.get("regen_remainder", 0.0))
		var applied := floori(regen)
		policy["regen_remainder"] = regen - float(applied)
		if applied > 0:
			hole["health"] = mini(maximum, int(hole.get("health", 0)) + applied)
		if sim.tick_index < int(policy.get("respawn_tick", sim.tick_index + 1)):
			hole["rebuild_hole_behavior"] = policy
			continue
		var rebuilt_id := spawn_scenario_structure(
			String(hole.get("rebuild_owner_object_id", "")), int(hole.get("rebuild_owner_team", -1)),
			Vector2(hole.get("rebuild_owner_position", hole.get("position", Vector2.ZERO))), "lair-spawn"
		)
		if rebuilt_id <= 0:
			policy["respawn_refused"] = "owner-scenario-admission-rejected"
			hole["rebuild_hole_behavior"] = policy
			continue
		sim.structures.erase(hole_id)
		sim._emit_event("rebuild_hole.rebuilt", hole_id, rebuilt_id, {"object_id": String(hole.get("rebuild_owner_object_id", "")), "worker_object_id": String(policy.get("worker_object_id", ""))})


func _typed_contract_numbers(fields:Dictionary,key:String)->Array[float]:
	var output:Array[float]=[]
	for value in fields.get(key,[]) as Array:
		var item:=value as Dictionary
		if typeof(item.get("value")) in [TYPE_INT,TYPE_FLOAT]:output.append(float(item.get("value")))
	return output


func _attach_banner_carrier_update_contract(row:Dictionary,contract:Dictionary)->void:
	if String(contract.get("extraction",""))!="typed" or row.has("banner_carrier_update"):return
	var fields:=contract.get("fields",{}) as Dictionary;var receipts:Array[String]=[]
	for key in ["BannerMorphFX","UnitSpawnFX","MorphCondition"]:
		if fields.has(key):receipts.append("presentation_binding:%s"%key)
	if fields.has("MeleeFreeUnitSpawnTime"):receipts.append("unsupported_banner_semantic:MeleeFreeUnitSpawnTime")
	var scan:=_resolve_respawn_body_expression(fields.get("ScanHordeDistance"))
	if fields.has("ScanHordeDistance") and not bool(scan.get("resolved",false)):receipts.append("unresolved_banner_scan_expression")
	row["banner_carrier_update"]={"idle_spawn_ticks":maxi(1,_ship_contract_delay_ticks(float(_module_contract_value(fields,"IdleSpawnRate",0.0)))),"melee_unit_spawn_ticks":_ship_contract_delay_ticks(float(_module_contract_value(fields,"MeleeFreeUnitSpawnTime",0.0))),"has_respawn_timer":fields.has("DiedRespawnTime") or fields.has("MeleeFreeBannerRespawnTime"),"died_respawn_ticks":_ship_contract_delay_ticks(float(_module_contract_value(fields,"DiedRespawnTime",0.0))),"melee_banner_respawn_ticks":_ship_contract_delay_ticks(float(_module_contract_value(fields,"MeleeFreeBannerRespawnTime",0.0))),"upgrade_required":String(_module_contract_value(fields,"UpgradeRequired","")),"replenish_nearby":bool(_module_contract_value(fields,"ReplenishNearbyHorde",false)),"replenish_all":bool(_module_contract_value(fields,"ReplenishAllNearbyHordes",false)),"scan_range_source":float(scan.get("value",0.0)) if bool(scan.get("resolved",false)) else -1.0,"next_replenish_tick":sim.tick_index,"unsupported_semantics":receipts}


func _resolve_respawn_body_expression(field:Variant)->Dictionary:
	if typeof(field)!=TYPE_DICTIONARY:return {"resolved":false}
	var value:=field as Dictionary
	if typeof(value.get("value")) in [TYPE_INT,TYPE_FLOAT]:return {"resolved":true,"value":float(value.get("value"))}
	var name:=String(value.get("define",""));var defines:=sim._rules.get("respawn_body_defines",{}) as Dictionary
	if typeof(defines.get(name)) in [TYPE_INT,TYPE_FLOAT]:return {"resolved":true,"value":float(defines[name])}
	return {"resolved":false,"define":name}


func _attach_respawn_body_contract(row:Dictionary,contract:Dictionary)->void:
	if String(contract.get("extraction",""))!="typed" or row.has("respawn_body"):return
	var fields:=contract.get("fields",{}) as Dictionary;var maximum:=_resolve_respawn_body_expression(fields.get("MaxHealth"));var recovery:=_resolve_respawn_body_expression(fields.get("RecoveryTime"));var damaged:=_resolve_respawn_body_expression(fields.get("MaxHealthDamaged"));var receipts:Array[String]=[]
	if not bool(maximum.get("resolved",false)):receipts.append("unresolved_body_define:MaxHealth:%s"%String(maximum.get("define","")))
	if fields.has("RecoveryTime") and not bool(recovery.get("resolved",false)):receipts.append("unresolved_body_define:RecoveryTime:%s"%String(recovery.get("define","")))
	for key in ["BurningDeathFX","HealingBuffFX","MaxHealthDamaged"]:
		if fields.has(key):receipts.append("presentation_binding:%s"%key)
	if fields.has("DodgePercent"):receipts.append("unsupported_combat_semantic:DodgePercent")
	if fields.has("BurningDeathBehavior"):receipts.append("unsupported_death_semantic:BurningDeathBehavior")
	if fields.has("RecoveryTime"):receipts.append("unsupported_body_semantic:RecoveryTime")
	if fields.has("CheerRadius"):receipts.append("presentation_or_audio_binding:CheerRadius")
	row["respawn_body"]={"max_health":int(maximum.get("value",0.0)) if bool(maximum.get("resolved",false)) else -1,"recovery_ticks":_ship_contract_delay_ticks(float(recovery.get("value",0.0))) if bool(recovery.get("resolved",false)) else -1,"max_health_damaged":int(damaged.get("value",0.0)) if bool(damaged.get("resolved",false)) else -1,"can_respawn":bool(_module_contract_value(fields,"CanRespawn",true)),"permanently_killed_filter":_typed_contract_tokens(fields,"PermanentlyKilledByFilter"),"cheer_radii":_typed_contract_numbers(fields,"CheerRadius"),"unsupported_semantics":receipts}
	if int((row["respawn_body"] as Dictionary).get("max_health",-1))>0:
		var member_health:=row.get("member_health",[]) as Array;var old_member_max:=maxi(1,int(row.get("member_maximum_health",1)));var new_member_max:=int((row["respawn_body"] as Dictionary).get("max_health"));var total:=0
		for index in member_health.size():member_health[index]=mini(new_member_max,roundi(float(member_health[index])*float(new_member_max)/float(old_member_max)));total+=int(member_health[index])
		row["member_health"]=member_health;row["member_maximum_health"]=new_member_max;row["maximum_health"]=new_member_max*member_health.size();row["health"]=total


func _resolve_contract_milliseconds(field:Variant,define_key:String)->Dictionary:
	if typeof(field)!=TYPE_DICTIONARY:return {"resolved":false}
	var value:=field as Dictionary
	if typeof(value.get("milliseconds")) in [TYPE_INT,TYPE_FLOAT]:return {"resolved":true,"ticks":_ship_contract_delay_ticks(float(value.get("milliseconds")))}
	var expression:=String(value.get("expression",value.get("define","")));var defines:=sim._rules.get(define_key,{}) as Dictionary
	if typeof(defines.get(expression)) in [TYPE_INT,TYPE_FLOAT]:return {"resolved":true,"ticks":_ship_contract_delay_ticks(float(defines[expression]))}
	return {"resolved":false,"expression":expression}


func _attach_give_upgrade_contract(row:Dictionary,contract:Dictionary)->void:
	if String(contract.get("extraction",""))!="typed":return
	var fields:=contract.get("fields",{}) as Dictionary;var receipts:Array[String]=[];var phases:Dictionary={}
	for key in ["UnpackTime","PreparationTime","PersistentPrepTime","PackTime"]:
		var resolved:=_resolve_contract_milliseconds(fields.get(key),"give_upgrade_time_defines")
		if not bool(resolved.get("resolved",false)):receipts.append("unresolved_upgrade_time:%s:%s"%[key,String(resolved.get("expression",""))])
		else:phases[key]=int(resolved.get("ticks",0))
	for key in ["SpawnOutFX","FadeOutSpeed"]:
		if fields.has(key):receipts.append("presentation_binding:%s"%key)
	if bool(_module_contract_value(fields,"ApproachRequiresLOS",false)):receipts.append("unsupported_targeting_semantic:ApproachRequiresLOS")
	var rows:=row.get("give_upgrade_updates",[]) as Array
	rows.append({"special_power":String(_module_contract_value(fields,"SpecialPowerTemplate","")),"range_source":float(_module_contract_value(fields,"StartAbilityRange",0.0)),"deliver_upgrade":bool(_module_contract_value(fields,"DeliverUpgrade",false)),"phase_ticks":phases,"unsupported_semantics":receipts,"tag":String(contract.get("tag","")),"line":int(contract.get("line",0))});row["give_upgrade_updates"]=rows


func request_give_upgrade(source_id:int,target_id:int,upgrade_id:String,special_power:String="")->Dictionary:
	if not sim.entities.has(source_id):return {"ok":false,"reason":"source-missing"}
	var target_table =sim.structures if sim.structures.has(target_id) else sim.entities
	if not target_table.has(target_id):return {"ok":false,"reason":"target-missing"}
	var source:=sim.entities[source_id] as Dictionary
	if not source.has("give_upgrade_updates"):_attach_module_contracts(source)
	if source.has("give_upgrade_action"):return {"ok":false,"reason":"upgrade-delivery-busy"}
	var selected:Dictionary={}
	for value in source.get("give_upgrade_updates",[]) as Array:
		var policy:=value as Dictionary
		if special_power=="" or String(policy.get("special_power",""))==special_power:selected=policy;break
	if selected.is_empty():return {"ok":false,"reason":"typed-give-upgrade-contract-missing"}
	if not (selected.get("unsupported_semantics",[]) as Array).filter(func(v):return String(v).begins_with("unresolved_upgrade_time:")).is_empty():return {"ok":false,"reason":"unresolved-upgrade-timing"}
	var target:=target_table[target_id] as Dictionary
	if int(target.get("team",-1))!=int(source.get("team",-1)):return {"ok":false,"reason":"target-not-allied"}
	var distance:=Vector2(source.get("position",Vector2.ZERO)).distance_to(Vector2(target.get("position",Vector2.ZERO)));var range_sim:=float(selected.get("range_source",0.0))*float(sim._rules.get("source_unit_scale",0.1))
	if distance>range_sim:return {"ok":false,"reason":"out-of-range"}
	if upgrade_id.strip_edges()=="" or not bool(selected.get("deliver_upgrade",false)):return {"ok":false,"reason":"upgrade-delivery-disabled"}
	var phases:=selected.get("phase_ticks",{}) as Dictionary;var delivery_tick =sim.tick_index+int(phases.get("UnpackTime",0))+int(phases.get("PreparationTime",0))+int(phases.get("PersistentPrepTime",0))
	source["give_upgrade_action"]={"target_id":target_id,"target_kind":"structure" if sim.structures.has(target_id) else "entity","upgrade_id":upgrade_id,"delivery_tick":delivery_tick,"complete_tick":delivery_tick+int(phases.get("PackTime",0))};return {"ok":true,"reason":"","delivery_tick":delivery_tick}


func _step_give_upgrade_updates()->void:
	for source_id in sim.entity_ids():
		var source:=sim.entities[source_id] as Dictionary;var action:=source.get("give_upgrade_action",{}) as Dictionary
		if action.is_empty():continue
		if int(source.get("health",0))<=0:source.erase("give_upgrade_action");continue
		if not bool(action.get("delivered",false)) and sim.tick_index>=int(action.get("delivery_tick",0)):
			var table =sim.structures if String(action.get("target_kind"))=="structure" else sim.entities;var target_id:=int(action.get("target_id",0))
			if table.has(target_id):
				var target:=table[target_id] as Dictionary
				if int(target.get("team",-1))==int(source.get("team",-2)):
					var upgrades:=target.get("completed_upgrades",[]) as Array;var upgrade:=String(action.get("upgrade_id",""));if not upgrades.has(upgrade):upgrades.append(upgrade);target["completed_upgrades"]=upgrades;sim._emit_event("upgrade.delivered",source_id,target_id,{"upgrade_id":upgrade})
			action["delivered"]=true;source["give_upgrade_action"]=action
		if sim.tick_index>=int(action.get("complete_tick",0)):source.erase("give_upgrade_action")


func _attach_gate_open_close_contract(row:Dictionary,contract:Dictionary)->void:
	if String(contract.get("extraction",""))!="typed" or row.has("gate_behavior"):return
	var fields:=contract.get("fields",{}) as Dictionary;var receipts:Array[String]=[]
	for key in ["SoundOpeningGateLoop","SoundClosingGateLoop","SoundFinishedOpeningGate","SoundFinishedClosingGate","TimeBeforePlayingOpenSound","TimeBeforePlayingClosedSound"]:
		if fields.has(key):receipts.append("audio_binding:%s"%key)
	var opened:=bool(_module_contract_value(fields,"OpenByDefault",false));row["gate_behavior"]={"open":opened,"pathing_open":opened,"open_fraction":1.0 if opened else 0.0,"reset_ticks":_ship_contract_delay_ticks(float(_module_contract_value(fields,"ResetTimeInMilliseconds",0.0))),"pathing_threshold":float(_module_contract_value(fields,"PercentOpenForPathing",100.0))/100.0,"repel_colliding":bool(_module_contract_value(fields,"RepelCollidingUnits",false)),"close_tick":-1,"unsupported_semantics":receipts}


func _attach_ai_gate_contract(row:Dictionary,contract:Dictionary)->void:
	if String(contract.get("extraction",""))!="typed" or row.has("ai_gate_update"):return
	var fields:=contract.get("fields",{}) as Dictionary;row["ai_gate_update"]={"trigger_width_source":Vector2(float(_module_contract_value(fields,"TriggerWidthX",0.0)),float(_module_contract_value(fields,"TriggerWidthY",0.0)))}


func _attach_fake_pathfind_portal_contract(row:Dictionary,contract:Dictionary)->void:
	if String(contract.get("extraction",""))!="typed" or row.has("fake_pathfind_portal"):return
	var fields:=contract.get("fields",{}) as Dictionary;row["fake_pathfind_portal"]={"allow_enemies":bool(_module_contract_value(fields,"AllowEnemies",false)),"allow_non_skirmish_ai":bool(_module_contract_value(fields,"AllowNonSkirmishAIUnits",false))}


func request_gate_open(structure_id:int,requester_id:int=0)->Dictionary:
	if not sim.structures.has(structure_id):return {"ok":false,"reason":"gate-missing"}
	var gate:=sim.structures[structure_id] as Dictionary
	if not gate.has("gate_behavior"):_attach_structure_module_contracts(gate)
	var policy:=gate.get("gate_behavior",{}) as Dictionary
	if policy.is_empty():return {"ok":false,"reason":"typed-gate-contract-missing"}
	if requester_id!=0 and not gate_portal_allows(structure_id,requester_id):return {"ok":false,"reason":"portal-denied"}
	policy["open"]=true;policy["open_fraction"]=1.0;policy["pathing_open"]=true;policy["close_tick"]=sim.tick_index+int(policy.get("reset_ticks",0));gate["gate_behavior"]=policy;sim._emit_event("gate.opened",requester_id,structure_id,{"close_tick":policy["close_tick"]});return {"ok":true,"reason":"","close_tick":policy["close_tick"]}
	_sync_gate_passage(structure_id)


func _sync_gate_passage(structure_id: int) -> void:
	## The gate's pathing state mirrored onto the ground grid (see
	## RetailMapData.set_gate_passage): open gates are passages through the
	## painted wall band; closed gates seal them again. Called at seeding and on
	## every pathing_open transition (open, timed close, manual close, death).
	if sim.route_provider == null or not sim.route_provider.has_method("set_gate_passage"):
		return
	if not sim.structures.has(structure_id):
		return
	var gate: Dictionary = sim.structures[structure_id]
	var policy: Dictionary = gate.get("gate_behavior", {})
	if policy.is_empty():
		return
	var geometries: Dictionary = gate.get("gate_geometries", {})
	var closed: Dictionary = geometries.get("Closed", {})
	# The authored closed box: MajorRadius along the facing (door thickness),
	# MinorRadius across it (the passage half-width).
	var half_width := float(closed.get("minorRadius", 0.0))
	if half_width <= 0.0:
		return
	var open := bool(policy.get("pathing_open", false)) or int(gate.get("health", 0)) <= 0
	var result: Dictionary = sim.route_provider.call("set_gate_passage", structure_id, Vector2(gate.get("position", Vector2.ZERO)), half_width, open)
	if bool(result.get("changed", false)):
		sim._emit_event("gate.passage", 0, structure_id, {"open": open, "cells": int(result.get("cells", 0)), "depth_fwd": result.get("depth_fwd"), "depth_back": result.get("depth_back")})


func gate_portal_allows(structure_id:int,requester_id:int)->bool:
	## The gate's AUTO-OPEN policy (AIGateUpdate rectangle / manual toggle):
	## pure geometry + ownership. FakePathfindPortalBehaviour's rules restrict
	## the PATHING PORTAL and live in _castle_gate_blocking_discs, not here —
	## a human owner's own gate always swings for his troops.
	if not sim.structures.has(structure_id) or not sim.entities.has(requester_id):return false
	var gate:=sim.structures[structure_id] as Dictionary;var requester:=sim.entities[requester_id] as Dictionary
	return int(requester.get("team",-1))==int(gate.get("team",-1))


func _step_gate_updates()->void:
	for structure_id in sim.structure_ids():
		var gate:=sim.structures[structure_id] as Dictionary;var policy:=gate.get("gate_behavior",{}) as Dictionary
		if policy.is_empty():continue
		var ai:=gate.get("ai_gate_update",{}) as Dictionary
		if not ai.is_empty():
			var half:=Vector2(ai.get("trigger_width_source",Vector2.ZERO))*float(sim._rules.get("source_map_transform_scale",0.1))*0.5;var origin:=Vector2(gate.get("position",Vector2.ZERO))
			for id in sim.entity_ids():
				var unit:=sim.entities[id] as Dictionary;var delta =Vector2(unit.get("position",Vector2.ZERO))-origin
				if int(unit.get("team",-1))==int(gate.get("team",-1)) and absf(delta.x)<=half.x and absf(delta.y)<=half.y:request_gate_open(structure_id,id);break
		if bool(policy.get("open",false)) and int(policy.get("close_tick",-1))>=0 and sim.tick_index>=int(policy.get("close_tick")):
			policy["open"]=false;policy["pathing_open"]=false;policy["open_fraction"]=0.0;policy["close_tick"]=-1;gate["gate_behavior"]=policy;sim._emit_event("gate.closed",structure_id,0)
			_sync_gate_passage(structure_id)


func _typed_effect_graph(contract: Dictionary, kind: String, target_mode: String) -> Dictionary:
	var graph_value: Variant = contract.get("effectGraph", contract.get("effect_graph", null))
	if typeof(graph_value) != TYPE_DICTIONARY:
		return {}
	var graph := graph_value as Dictionary
	if String(graph.get("kind", "")) != kind or String(graph.get("targetMode", "")) != target_mode:
		return {}
	return graph


func _attach_stop_special_power_contract(row: Dictionary, contract: Dictionary) -> void:
	## StopSpecialPower is a self command which cancels one exact, compiler-linked
	## special-power template.  The consumer never guesses a target from a command
	## name: both field ids must agree with the typed effect graph.
	if String(contract.get("extraction", "")) != "typed":
		return
	var graph := _typed_effect_graph(contract, "stop-special-power", "SELF")
	var fields := contract.get("fields", {}) as Dictionary
	var own_template := String(_module_contract_value(fields, "SpecialPowerTemplate", "")).strip_edges()
	var stop_template := String(_module_contract_value(fields, "StopPowerTemplate", "")).strip_edges()
	if (
		graph.is_empty() or own_template == "" or stop_template == ""
		or String(graph.get("specialPowerTemplateId", "")) != own_template
		or String(graph.get("stopPowerTemplateId", "")) != stop_template
		or not bool(graph.get("interruptsCurrentOrder", false))
		or typeof(graph.get("linkedModule")) != TYPE_DICTIONARY
	):
		return
	var linked := graph.get("linkedModule", {}) as Dictionary
	if String(linked.get("kind", "")).strip_edges() == "" or String(linked.get("sourceIni", "")).strip_edges() == "" or int(linked.get("line", 0)) <= 0:
		return
	var policies := row.get("stop_special_powers", []) as Array
	var key := "%s:%d" % [String(contract.get("tag", "")), int(contract.get("line", 0))]
	for existing_value in policies:
		if String((existing_value as Dictionary).get("key", "")) == key:
			return
	policies.append({
		"key": key,
		"special_power_template": own_template,
		"stop_power_template": stop_template,
		"interrupts_current_order": true,
		"linked_module": linked.duplicate(true),
		"unsupported_semantics": [],
		"tag": String(contract.get("tag", "")),
		"source_ini": String(contract.get("sourceIni", contract.get("source_ini", ""))),
		"line": int(contract.get("line", 0)),
	})
	row["stop_special_powers"] = policies


func _active_special_power_channel_key(row: Dictionary, target_template: String) -> String:
	## These are the authoritative scheduled channels which record a template id.
	## A subsystem without that identity is deliberately invisible to Stop: a
	## guessed ability-id/template mapping could cancel the wrong retail power.
	for key in [
		"activate_module_channel", "dominate_enemy_channel", "grab_passenger_channel",
		"fling_passenger_channel", "repair_structure_channel", "siege_deploy_channel",
	]:
		var channel_value: Variant = row.get(key)
		if typeof(channel_value) != TYPE_DICTIONARY:
			continue
		var channel := channel_value as Dictionary
		if String(channel.get("special_power_template_id", "")) == target_template:
			return key
	return ""


func activate_stop_special_power(entity_id: int, special_power_template: String, team: int = -1) -> Dictionary:
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity-missing"}
	var row := sim.entities[entity_id] as Dictionary
	if team >= 0 and int(row.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(row.get("health", 0)) <= 0:
		return {"ok": false, "reason": "unit-defeated"}
	if not row.has("stop_special_powers"):
		_attach_module_contracts(row)
	var policy: Dictionary = {}
	for policy_value in row.get("stop_special_powers", []) as Array:
		var candidate = policy_value as Dictionary
		if String(candidate.get("special_power_template", "")) == special_power_template:
			policy = candidate
			break
	if policy.is_empty():
		return {"ok": false, "reason": "typed-stop-special-power-missing"}
	if not (policy.get("unsupported_semantics", []) as Array).is_empty():
		return {"ok": false, "reason": "unsupported-semantics", "receipts": policy.get("unsupported_semantics")}
	var stop_template := String(policy.get("stop_power_template", ""))
	var channel_key := _active_special_power_channel_key(row, stop_template)
	if channel_key == "":
		return {"ok": false, "reason": "stop-power-not-active", "stop_power_template": stop_template}
	var stopped_channel := (row.get(channel_key, {}) as Dictionary).duplicate(true)
	if channel_key == "siege_deploy_channel":
		if String(stopped_channel.get("phase", "")) == "retracting":
			return {"ok": false, "reason": "stop-power-not-active", "stop_power_template": stop_template}
		stopped_channel["phase"] = "retracting"
		stopped_channel["phase_end_tick"] = sim.tick_index + int(stopped_channel.get("raise_delay_ticks", 0))
		row[channel_key] = stopped_channel
	else:
		row.erase(channel_key)
	if bool(policy.get("interrupts_current_order", false)):
		sim._clear_pending_route(row, true)
		sim._clear_member_targets(row)
		row["target_id"] = 0
		row["target_kind"] = ""
		row["attack_move"] = false
		row["order_kind"] = ""
		row["state"] = "ability" if channel_key == "siege_deploy_channel" else "idle"
	sim._emit_event("ability.special_power_stopped", entity_id, int(stopped_channel.get("current_target_id", stopped_channel.get("target_id", 0))), {
		"special_power_template": special_power_template,
		"stop_power_template": stop_template,
		"channel": channel_key,
		"linked_module": policy.get("linked_module", {}),
	})
	return {"ok": true, "reason": "", "stop_power_template": stop_template, "channel": channel_key}


func _attach_unleash_special_power_contract(row: Dictionary, contract: Dictionary) -> void:
	## The stable graph proves a single ObjectCreationUpgrade/SlaveWatcher chain.
	## Only retail's zero-time, instant release is executable here; the schema also
	## accepts other timings and XP awards, which remain explicit receipts.
	if String(contract.get("extraction", "")) != "typed":
		return
	var graph := _typed_effect_graph(contract, "unleash-special-power", "SELF_OWNED_SLAVE")
	var fields := contract.get("fields", {}) as Dictionary
	var template := String(_module_contract_value(fields, "SpecialPowerTemplate", "")).strip_edges()
	var unpack_ms := int(_module_contract_value(fields, "UnpackTime", -1))
	var award_xp := float(_module_contract_value(fields, "AwardXPForTriggering", -1.0))
	var instant_value: Variant = _module_contract_value(fields, "Instant", null)
	var timing := graph.get("timingMs", {}) as Dictionary
	var gate_value: Variant = graph.get("creationGateUpgradeIds")
	var watcher_value: Variant = graph.get("slaveWatcher")
	if (
		graph.is_empty() or template == "" or unpack_ms < 0 or award_xp < 0.0 or typeof(instant_value) != TYPE_BOOL
		or String(graph.get("specialPowerTemplateId", "")) != template
		or int(timing.get("UnpackTime", -1)) != unpack_ms
		or float(graph.get("awardXpForTriggering", -1.0)) != award_xp
		or bool(graph.get("instant", not bool(instant_value))) != bool(instant_value)
		or String(graph.get("spawnedObjectId", "")).strip_edges() == ""
		or typeof(gate_value) != TYPE_ARRAY or (gate_value as Array).is_empty()
		or typeof(watcher_value) != TYPE_DICTIONARY
	):
		return
	var gates: Array[String] = []
	for gate_value_item in gate_value as Array:
		var gate := String(gate_value_item).strip_edges()
		if gate == "":
			return
		gates.append(gate)
	var watcher := watcher_value as Dictionary
	if String(watcher.get("removeUpgradeId", "")).strip_edges() == "" or String(watcher.get("grantUpgradeId", "")).strip_edges() == "" or String(watcher.get("sourceIni", "")).strip_edges() == "" or int(watcher.get("line", 0)) <= 0:
		return
	var unsupported: Array[String] = []
	if unpack_ms != 0:
		unsupported.append("nonzero-unpack-time")
	if not bool(instant_value):
		unsupported.append("non-instant-release")
	if not is_zero_approx(award_xp):
		unsupported.append("nonzero-award-xp")
	var policies := row.get("unleash_special_powers", []) as Array
	var key := "%s:%d" % [String(contract.get("tag", "")), int(contract.get("line", 0))]
	for existing_value in policies:
		if String((existing_value as Dictionary).get("key", "")) == key:
			return
	policies.append({
		"key": key,
		"special_power_template": template,
		"unpack_ticks": _ship_contract_delay_ticks(float(unpack_ms)),
		"award_xp": award_xp,
		"instant": bool(instant_value),
		"spawned_object_id": String(graph.get("spawnedObjectId", "")),
		"creation_gate_upgrade_ids": gates,
		"slave_watcher": watcher.duplicate(true),
		"unsupported_semantics": unsupported,
		"tag": String(contract.get("tag", "")),
		"source_ini": String(contract.get("sourceIni", contract.get("source_ini", ""))),
		"line": int(contract.get("line", 0)),
	})
	row["unleash_special_powers"] = policies


func _unleash_owned_slave(owner_id: int, owner: Dictionary, policy: Dictionary) -> int:
	var expected_object := String(policy.get("spawned_object_id", ""))
	var required_gates := policy.get("creation_gate_upgrade_ids", []) as Array
	for creation_value in owner.get("object_creation_upgrades", []) as Array:
		var creation := creation_value as Dictionary
		if String(creation.get("thing_to_spawn", "")) != expected_object:
			continue
		var triggers := creation.get("triggers", []) as Array
		var gate_match := true
		for gate_value in required_gates:
			if not triggers.has(String(gate_value)):
				gate_match = false
				break
		if not gate_match:
			continue
		for slave_value in creation.get("spawned_ids", []) as Array:
			var slave_id := int(slave_value)
			if not sim.entities.has(slave_id):
				continue
			var slave := sim.entities[slave_id] as Dictionary
			var identity := String(slave.get("source_object_id", slave.get("object_id", slave.get("unit_type", ""))))
			var slaved := slave.get("slaved_update", {}) as Dictionary
			if identity == expected_object and int(slave.get("health", 0)) > 0 and int(slave.get("team", -1)) == int(owner.get("team", -2)) and int(slaved.get("master_id", 0)) == owner_id:
				return slave_id
	return 0


func activate_unleash_special_power(owner_id: int, special_power_template: String, team: int = -1) -> Dictionary:
	var table = sim.structures if sim.structures.has(owner_id) else sim.entities
	if not table.has(owner_id):
		return {"ok": false, "reason": "owner-missing"}
	var owner := table[owner_id] as Dictionary
	if team >= 0 and int(owner.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(owner.get("health", 0)) <= 0:
		return {"ok": false, "reason": "owner-defeated"}
	if not owner.has("unleash_special_powers"):
		if table == sim.structures: _attach_structure_module_contracts(owner)
		else: _attach_module_contracts(owner)
	var policy: Dictionary = {}
	for policy_value in owner.get("unleash_special_powers", []) as Array:
		var candidate = policy_value as Dictionary
		if String(candidate.get("special_power_template", "")) == special_power_template:
			policy = candidate
			break
	if policy.is_empty():
		return {"ok": false, "reason": "typed-unleash-special-power-missing"}
	if not (policy.get("unsupported_semantics", []) as Array).is_empty():
		return {"ok": false, "reason": "unsupported-semantics", "receipts": policy.get("unsupported_semantics")}
	var slave_id := _unleash_owned_slave(owner_id, owner, policy)
	if slave_id == 0:
		return {"ok": false, "reason": "owned-slave-not-found"}
	var slave := sim.entities[slave_id] as Dictionary
	var slaved := slave.get("slaved_update", {}) as Dictionary
	slaved["master_id"] = 0
	slaved.erase("master_kind")
	slave["slaved_update"] = slaved
	slave["unselectable"] = false
	slave["ignores_select_all"] = false
	# SlaveWatcher upgrades are death callbacks. Releasing a living slave must not
	# counterfeit that callback or make a replacement immediately purchasable.
	sim._emit_event("ability.slave_unleashed", owner_id, slave_id, {
		"slave_id": slave_id,
		"special_power_template": special_power_template,
		"spawned_object_id": policy.get("spawned_object_id"),
		"slave_watcher": policy.get("slave_watcher", {}),
	})
	return {"ok": true, "reason": "", "slave_id": slave_id}


func _attach_special_enemy_sense_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("special_enemy_sense"):
		return
	var graph := _typed_effect_graph(contract, "special-enemy-sense", "PERIODIC_ENEMY_RADIUS_SCAN")
	var fields := contract.get("fields", {}) as Dictionary
	var filter_value: Variant = _module_contract_value(fields, "SpecialEnemyFilter", null)
	var range_value: Variant = _module_contract_value(fields, "ScanRange", null)
	var interval_ms_value: Variant = _module_contract_value(fields, "ScanInterval", null)
	if graph.is_empty() or typeof(filter_value) != TYPE_ARRAY or typeof(range_value) not in [TYPE_INT, TYPE_FLOAT] or typeof(interval_ms_value) not in [TYPE_INT, TYPE_FLOAT]:
		return
	var filter: Array[String] = []
	for token_value in filter_value as Array:
		var token := String(token_value).strip_edges()
		if token == "":
			return
		filter.append(token)
	var range_source := float(range_value)
	var interval_ms := float(interval_ms_value)
	if filter.is_empty() or range_source <= 0.0 or interval_ms <= 0.0 or graph.get("specialEnemyFilter", []) != filter or float(graph.get("scanRange", -1.0)) != range_source or float(graph.get("scanIntervalMs", -1.0)) != interval_ms:
		return
	var unsupported: Array[String] = []
	for token in filter:
		var upper := token.to_upper()
		if upper in ["ANY", "ALL", "NONE"] or token.begins_with("+") or token.begins_with("-"):
			continue
		unsupported.append("unsupported-filter-token:%s" % token)
	row["special_enemy_sense"] = {
		"filter": filter,
		"scan_range_source": range_source,
		"scan_interval_ticks": maxi(1, _ship_contract_delay_ticks(interval_ms)),
		"next_scan_tick": sim.tick_index,
		"sensed_ids": [],
		"unsupported_semantics": unsupported,
		"tag": String(contract.get("tag", "")),
		"source_ini": String(contract.get("sourceIni", contract.get("source_ini", ""))),
		"line": int(contract.get("line", 0)),
	}
	row["special_enemy_sense_active"] = false
	row["special_enemy_sensed_ids"] = []


# De-staticed on extraction (instance sim access).
func _special_enemy_sense_filter_accepts(target: Dictionary, filter: Array) -> bool:
	var traits: Dictionary = {}
	for kind_value in target.get("kind_of", []) as Array:
		traits[String(kind_value).to_upper()] = true
	var category := String(target.get("category", "")).to_upper()
	if category != "":
		traits[category] = true
	for key in ["source_object_id", "object_id", "unit_type"]:
		var identity := String(target.get(key, "")).to_upper()
		if identity != "":
			traits[identity] = true
	var positives: Array[String] = []
	var require_all := false
	for token_value in filter:
		var token := String(token_value).to_upper()
		if token == "ALL":
			require_all = true
		elif token.begins_with("-"):
			if traits.has(token.substr(1)):
				return false
		elif token.begins_with("+"):
			positives.append(token.substr(1))
	if positives.is_empty():
		return false
	if require_all:
		for positive in positives:
			if not traits.has(positive):
				return false
		return true
	for positive in positives:
		if traits.has(positive):
			return true
	return false


func _step_special_enemy_sense_updates() -> void:
	for entity_id in sim.entity_ids():
		var source := sim.entities[entity_id] as Dictionary
		var policy := source.get("special_enemy_sense", {}) as Dictionary
		if policy.is_empty() or sim.tick_index < int(policy.get("next_scan_tick", 0)):
			continue
		policy["next_scan_tick"] = sim.tick_index + maxi(1, int(policy.get("scan_interval_ticks", 1)))
		var desired: Array[int] = []
		if int(source.get("health", 0)) > 0 and (policy.get("unsupported_semantics", []) as Array).is_empty():
			var origin := Vector2(source.get("position", Vector2.ZERO))
			var radius = float(policy.get("scan_range_source", 0.0)) * float(sim._rules.get("source_unit_scale", 0.1))
			for target_id in sim.entity_ids():
				if target_id == entity_id:
					continue
				var target := sim.entities[target_id] as Dictionary
				if int(target.get("health", 0)) <= 0 or not sim._is_hostile(int(source.get("team", -1)), int(target.get("team", -1))):
					continue
				if origin.distance_to(Vector2(target.get("position", origin))) <= radius and _special_enemy_sense_filter_accepts(target, policy.get("filter", []) as Array):
					desired.append(target_id)
		desired.sort()
		var prior := policy.get("sensed_ids", []) as Array
		policy["sensed_ids"] = desired
		source["special_enemy_sense"] = policy
		source["special_enemy_sensed_ids"] = desired.duplicate()
		source["special_enemy_sense_active"] = not desired.is_empty()
		if prior != desired:
			sim._emit_event("module.special_enemy_sense_changed", entity_id, int(desired[0]) if not desired.is_empty() else 0, {
				"active": not desired.is_empty(), "sensed_ids": desired.duplicate(),
				"filter": (policy.get("filter", []) as Array).duplicate(),
			})


func _resolve_invisibility_expression(field: Variant, key: String) -> Dictionary:
	if typeof(field) != TYPE_DICTIONARY:
		return {"resolved": false, "expression": ""}
	var value := field as Dictionary
	if typeof(value.get("value")) in [TYPE_INT, TYPE_FLOAT]:
		return {"resolved": true, "value": float(value.get("value")), "expression": String(value.get("expression", value.get("value", "")))}
	var name := String(value.get("define", value.get("name", value.get("expression", ""))))
	var defines := sim._rules.get("invisibility_defines", {}) as Dictionary
	if typeof(defines.get(name)) in [TYPE_INT, TYPE_FLOAT]:
		return {"resolved": true, "value": float(defines[name]), "expression": name}
	return {"resolved": false, "expression": name, "field": key}


func _attach_invisibility_update_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("invisibility_update"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var nuggets := fields.get("InvisibilityNugget", []) as Array
	if nuggets.size() != 1 or typeof(nuggets[0]) != TYPE_DICTIONARY:
		return
	var nugget := nuggets[0] as Dictionary
	var invisibility_type := String(_module_contract_value(nugget, "InvisibilityType", "")).to_upper()
	var update_ms := float(_module_contract_value(fields, "UpdatePeriod", 0.0))
	if invisibility_type not in ["CAMOUFLAGE", "STEALTH"] or update_ms <= 0.0:
		return
	var detection := _resolve_invisibility_expression(nugget.get("DetectionRange"), "DetectionRange")
	var broadcast_range := _resolve_invisibility_expression(fields.get("BroadcastRange"), "BroadcastRange")
	var broadcast := bool(_module_contract_value(fields, "Broadcast", false))
	var broadcast_filter := _typed_contract_raw_tokens(fields, "BroadcastObjectFilter")
	var broadcast_filter_resolved := not broadcast or (not broadcast_filter.is_empty() and not (broadcast_filter.size() == 1 and not String(broadcast_filter[0]).begins_with("+") and String(broadcast_filter[0]).to_upper() not in ["ANY", "ALL"]))
	var receipts: Array[String] = []
	if nugget.has("DetectionRange") and not bool(detection.get("resolved", false)):
		receipts.append("unresolved_invisibility_define:DetectionRange:%s" % String(detection.get("expression", "")))
	if broadcast and not bool(broadcast_range.get("resolved", false)):
		receipts.append("unresolved_invisibility_define:BroadcastRange:%s" % String(broadcast_range.get("expression", "")))
	if broadcast and (broadcast_filter.is_empty() or (broadcast_filter.size() == 1 and not String(broadcast_filter[0]).begins_with("+") and String(broadcast_filter[0]).to_upper() not in ["ANY", "ALL"])):
		receipts.append("unresolved_broadcast_filter:%s" % (String(broadcast_filter[0]) if not broadcast_filter.is_empty() else "<missing>"))
	var forbidden := _typed_contract_tokens(nugget, "ForbiddenConditions")
	if forbidden.has("AWAY_FROM_TREES"):
		receipts.append("environment-condition-unresolved:AWAY_FROM_TREES")
	if not _typed_contract_tokens(nugget, "HintDetectableConditions").is_empty():
		receipts.append("presentation-hint-detectable-conditions")
	for key in ["BecomeStealthedFX", "ExitStealthFX"]:
		if nugget.has(key):
			receipts.append("presentation-fx-binding:%s" % key)
	row["invisibility_update"] = {
		"enabled": bool(_module_contract_value(fields, "StartsActive", false)),
		"starts_active": bool(_module_contract_value(fields, "StartsActive", false)),
		"update_ticks": maxi(1, _ship_contract_delay_ticks(update_ms)),
		"next_update_tick": sim.tick_index,
		"required_upgrades": _typed_contract_raw_tokens(fields, "RequiredUpgrades"),
		"forbidden_upgrades": _typed_contract_raw_tokens(fields, "ForbiddenUpgrades"),
		"broadcast": broadcast,
		"broadcast_range_source": float(broadcast_range.get("value", -1.0)) if bool(broadcast_range.get("resolved", false)) else -1.0,
		"broadcast_filter": broadcast_filter,
		"invisibility_type": invisibility_type,
		"forbidden_conditions": forbidden,
		"forbidden_weapon_conditions": _typed_contract_tokens(nugget, "ForbiddenWeaponConditions"),
		"hint_detectable_conditions": _typed_contract_tokens(nugget, "HintDetectableConditions"),
		"options": _typed_contract_tokens(nugget, "Options"),
		"detection_range_source": float(detection.get("value", 0.0)) if bool(detection.get("resolved", false)) else -1.0,
		"self_executable": (not nugget.has("DetectionRange") or bool(detection.get("resolved", false))) and not forbidden.has("AWAY_FROM_TREES"),
		"broadcast_executable": not broadcast or (bool(broadcast_range.get("resolved", false)) and broadcast_filter_resolved),
		"become_fx_id": String(_module_contract_value(nugget, "BecomeStealthedFX", "")),
		"exit_fx_id": String(_module_contract_value(nugget, "ExitStealthFX", "")),
		"voice_move_role": String(_module_contract_value(fields, "UnitSpecificSoundNameToUseAsVoiceMoveToStealthyArea", "")),
		"voice_enter_role": String(_module_contract_value(fields, "UnitSpecificSoundNameToUseAsVoiceEnterStateMoveToStealthyArea", "")),
		"granted_ids": [],
		"unsupported_semantics": receipts,
		"tag": String(contract.get("tag", "")),
		"source_ini": String(contract.get("sourceIni", contract.get("source_ini", ""))),
		"line": int(contract.get("line", 0)),
	}


func set_invisibility_update_active(object_id: int, enabled: bool, tag: String = "") -> Dictionary:
	var row: Dictionary = {}
	if sim.entities.has(object_id): row = sim.entities[object_id] as Dictionary
	elif sim.structures.has(object_id): row = sim.structures[object_id] as Dictionary
	else: return {"ok": false, "reason": "object-missing"}
	if not row.has("invisibility_update"):
		if sim.structures.has(object_id): _attach_structure_module_contracts(row)
		else: _attach_module_contracts(row)
	var policy := row.get("invisibility_update", {}) as Dictionary
	if policy.is_empty(): return {"ok": false, "reason": "typed-invisibility-contract-missing"}
	if tag != "" and String(policy.get("tag", "")) != tag: return {"ok": false, "reason": "invisibility-tag-missing"}
	policy["enabled"] = enabled
	policy["next_update_tick"] = sim.tick_index
	row["invisibility_update"] = policy
	if not enabled:
		_revoke_invisibility_policy_sources(object_id, row, policy)
	return {"ok": true, "reason": "", "enabled": enabled}


func _invisibility_upgrade_gate(row: Dictionary, policy: Dictionary) -> bool:
	var team_owned := sim.team_upgrades.get(int(row.get("team", -1)), {}) as Dictionary
	for value in policy.get("required_upgrades", []) as Array:
		var upgrade := String(value)
		if not sim._structure_has_completed_upgrade(row, upgrade) and not team_owned.has(upgrade): return false
	for value in policy.get("forbidden_upgrades", []) as Array:
		var upgrade := String(value)
		if sim._structure_has_completed_upgrade(row, upgrade) or team_owned.has(upgrade): return false
	return true


func _invisibility_condition_set(row: Dictionary) -> Dictionary:
	var conditions := _audio_active_conditions(row)
	if Vector2(row.get("destination", row.get("position", Vector2.ZERO))).distance_to(Vector2(row.get("position", Vector2.ZERO))) > 0.001 or float(row.get("current_speed", 0.0)) > 0.001:
		conditions["MOVING"] = true
	if String(row.get("state", "")) in ["attack", "attack-windup"]:
		conditions["ATTACKING"] = true
	for value in row.get("weapon_conditions", []) as Array:
		conditions[String(value).to_upper()] = true
	return conditions


func _invisibility_source_key(object_id: int, policy: Dictionary, prefix: String = "module") -> String:
	return "%s:%d:%s" % [prefix, object_id, String(policy.get("tag", ""))]


func _invisibility_source_active(row: Dictionary, source_key: String) -> bool:
	return (row.get("invisibility_sources", {}) as Dictionary).has(source_key)


func _set_invisibility_source(target: Dictionary, source_key: String, policy: Dictionary, enabled: bool, source_id: int) -> void:
	var sources = target.get("invisibility_sources", {}) as Dictionary
	var was_source_active := sources.has(source_key)
	var was_hidden = sim._stealth_active(target)
	if enabled:
		sources[source_key] = {
			"forbidden": (policy.get("forbidden_conditions", []) as Array).duplicate(),
			"detection_range_source": float(policy.get("detection_range_source", 0.0)),
			"invisibility_type": String(policy.get("invisibility_type", "STEALTH")),
		}
	else:
		sources.erase(source_key)
	if sources.is_empty():
		target.erase("invisibility_sources")
		if String(target.get("stealth_origin", "")) == "InvisibilityUpdate":
			target.erase("stealth_origin")
			sim._clear_stealth(target)
	else:
		target["invisibility_sources"] = sources
		var union: Array[String] = []
		var detection_ranges: Array[float] = []
		var type := "CAMOUFLAGE"
		for source_value in sources.values():
			var source := source_value as Dictionary
			for condition_value in source.get("forbidden", []) as Array:
				var condition := String(condition_value); if not union.has(condition): union.append(condition)
			var range_source := float(source.get("detection_range_source", 0.0))
			if range_source >= 0.0: detection_ranges.append(range_source)
			if String(source.get("invisibility_type", "")) == "STEALTH": type = "STEALTH"
		target["stealth_until_tick"] = 0x3FFFFFFF
		target["stealth_forbidden"] = union
		target["stealth_origin"] = "InvisibilityUpdate"
		target["invisibility_type"] = type
		target["invisibility_detection_range_source"] = detection_ranges.min() if not detection_ranges.is_empty() else -1.0
	if was_source_active == enabled:
		return
	var fx_id := String(policy.get("become_fx_id" if enabled else "exit_fx_id", ""))
	sim._emit_event("module.invisibility_changed", source_id, int(target.get("id", 0)), {"engaged": enabled, "fx_id": fx_id, "invisibility_type": String(policy.get("invisibility_type", "")), "source_key": source_key})
	if enabled and not was_hidden and sim.entities.has(int(target.get("id", 0))):
		var voice_role := String(policy.get("voice_enter_role", ""))
		if voice_role != "": policy["last_audio_result"] = emit_typed_audio_intent(int(target.get("id", 0)), voice_role)


func _revoke_invisibility_policy_sources(object_id: int, row: Dictionary, policy: Dictionary) -> void:
	var own_key := _invisibility_source_key(object_id, policy, "structure" if sim.structures.has(object_id) else "entity")
	_set_invisibility_source(row, own_key, policy, false, object_id)
	var broadcast_key := _invisibility_source_key(object_id, policy, "broadcast")
	for target_id in policy.get("granted_ids", []) as Array:
		if sim.entities.has(int(target_id)): _set_invisibility_source(sim.entities[int(target_id)] as Dictionary, broadcast_key, policy, false, object_id)
	policy["granted_ids"] = []


func _step_invisibility_updates() -> void:
	for table_value in [sim.entities, sim.structures]:
		var table = table_value as Dictionary
		var ids := table.keys(); ids.sort()
		for id_value in ids:
			var object_id := int(id_value); var row := table[object_id] as Dictionary
			var policy := row.get("invisibility_update", {}) as Dictionary
			if policy.is_empty() or sim.tick_index < int(policy.get("next_update_tick", 0)): continue
			policy["next_update_tick"] = sim.tick_index + maxi(1, int(policy.get("update_ticks", 1)))
			var conditions := _invisibility_condition_set(row)
			var blocked := not bool(policy.get("enabled", false)) or not bool(policy.get("self_executable", true)) or not _invisibility_upgrade_gate(row, policy) or int(row.get("health", 0)) <= 0
			for condition_value in policy.get("forbidden_conditions", []) as Array:
				var condition := String(condition_value)
				if condition == "AWAY_FROM_TREES": continue # policy is fail-closed above; no authoritative sim prop geometry
				if conditions.has(condition): blocked = true; break
			for condition_value in policy.get("forbidden_weapon_conditions", []) as Array:
				if conditions.has(String(condition_value)): blocked = true; break
			row["invisibility_hint_detectable"] = false
			for condition_value in policy.get("hint_detectable_conditions", []) as Array:
				if conditions.has(String(condition_value)): row["invisibility_hint_detectable"] = true; break
			var own_key := _invisibility_source_key(object_id, policy, "structure" if table == sim.structures else "entity")
			var was_active := _invisibility_source_active(row, own_key)
			if blocked:
				_set_invisibility_source(row, own_key, policy, false, object_id)
				if was_active and (policy.get("options", []) as Array).has("UNTOGGLE_HIDDEN_WHEN_LEAVING_STEALTH"): policy["enabled"] = false
			else:
				_set_invisibility_source(row, own_key, policy, true, object_id)
			_step_invisibility_broadcast(object_id, row, policy, blocked)
			row["invisibility_update"] = policy


func _step_invisibility_broadcast(source_id: int, source: Dictionary, policy: Dictionary, blocked: bool) -> void:
	var source_key := _invisibility_source_key(source_id, policy, "broadcast")
	var prior := policy.get("granted_ids", []) as Array
	var desired: Array[int] = []
	var radius_source := float(policy.get("broadcast_range_source", -1.0))
	var filter_tokens := policy.get("broadcast_filter", []) as Array
	var unresolved_filter := filter_tokens.is_empty() or (filter_tokens.size() == 1 and not String(filter_tokens[0]).begins_with("+") and String(filter_tokens[0]).to_upper() not in ["ANY", "ALL"])
	if bool(policy.get("broadcast", false)) and bool(policy.get("broadcast_executable", true)) and not blocked and radius_source >= 0.0 and not unresolved_filter:
		var radius = radius_source * float(sim._rules.get("source_unit_scale", 0.1)); var origin := Vector2(source.get("position", Vector2.ZERO)); var team := int(source.get("team", -1))
		for target_id in sim.entity_ids():
			var target := sim.entities[target_id] as Dictionary
			if int(target.get("health", 0)) <= 0 or int(target.get("team", -1)) != team: continue
			if origin.distance_to(Vector2(target.get("position", Vector2.ZERO))) > radius: continue
			if not sim._transport_filter_accepts(target, filter_tokens): continue
			desired.append(target_id); _set_invisibility_source(target, source_key, policy, true, source_id)
	for target_id in prior:
		if not desired.has(int(target_id)) and sim.entities.has(int(target_id)): _set_invisibility_source(sim.entities[int(target_id)] as Dictionary, source_key, policy, false, source_id)
	policy["granted_ids"] = desired


func _attach_stealth_detector_contract(row:Dictionary,contract:Dictionary)->void:
	if String(contract.get("extraction",""))!="typed" or row.has("stealth_detector_update"):return
	var fields:=contract.get("fields",{}) as Dictionary;var rate:=_resolve_contract_milliseconds(fields.get("DetectionRate"),"stealth_detector_time_defines");var range:=_resolve_respawn_body_expression(fields.get("DetectionRange"));var receipts:Array[String]=[]
	if not bool(rate.get("resolved",false)):receipts.append("unresolved_detection_rate")
	if fields.has("DetectionRange") and not bool(range.get("resolved",false)):receipts.append("unresolved_detection_range")
	if bool(_module_contract_value(fields,"CancelOneRingEffect",false)):receipts.append("unsupported_ring_presentation:CancelOneRingEffect")
	row["stealth_detector_update"]={"rate_ticks":int(rate.get("ticks",-1)),"range_source":float(range.get("value",0.0)) if bool(range.get("resolved",false)) else 0.0,"required_upgrade":String(_module_contract_value(fields,"RequiredUpgrade","")),"while_garrisoned":bool(_module_contract_value(fields,"CanDetectWhileGarrisoned",false)),"while_contained":bool(_module_contract_value(fields,"CanDetectWhileContained",false)),"next_tick":sim.tick_index,"unsupported_semantics":receipts}


func _step_stealth_detectors()->void:
	var tables:Array=[sim.entities,sim.structures]
	for table in tables:
		for id_value in (table as Dictionary).keys():
			var detector:=(table as Dictionary)[id_value] as Dictionary;var policy:=detector.get("stealth_detector_update",{}) as Dictionary
			if policy.is_empty() or int(policy.get("rate_ticks",-1))<0 or sim.tick_index<int(policy.get("next_tick",0)):continue
			policy["next_tick"]=sim.tick_index+maxi(1,int(policy.get("rate_ticks",1)));detector["stealth_detector_update"]=policy
			var required:=String(policy.get("required_upgrade",""));if required!="" and not sim._structure_has_completed_upgrade(detector,required):continue
			if sim.entity_container.has(int(id_value)) and not bool(policy.get("while_contained",false)):continue
			if bool(detector.get("garrisoned",false)) and not bool(policy.get("while_garrisoned",false)):continue
			var radius =float(policy.get("range_source",0.0))*float(sim._rules.get("source_unit_scale",0.1));var origin:=Vector2(detector.get("position",Vector2.ZERO))
			for target_id in sim.entity_ids():
				var target:=sim.entities[target_id] as Dictionary
				if int(target.get("team",-1))==int(detector.get("team",-1)) or not sim._stealth_active(target):continue
				if origin.distance_to(Vector2(target.get("position",Vector2.ZERO)))<=radius:target["detected_until_tick"]=sim.tick_index+maxi(1,int(policy.get("rate_ticks",1)));sim._emit_event("stealth.detected",int(id_value),target_id,{"until_tick":target["detected_until_tick"]})


func _attach_slaved_update_contract(row:Dictionary,contract:Dictionary)->void:
	if String(contract.get("extraction",""))!="typed" or row.has("slaved_update"):return
	var fields:=contract.get("fields",{}) as Dictionary;var receipts:Array[String]=[];var offset:=Vector2.ZERO
	if typeof(fields.get("GuardPositionOffset"))==TYPE_DICTIONARY:
		var value:=(fields.get("GuardPositionOffset") as Dictionary).get("value",{}) as Dictionary;offset=Vector2(float(value.get("x",0.0)),float(value.get("y",0.0)))
	for key in ["UseSlaverAsControlForEvaObjectSightedEvents","FadeOutRange","FadeTime"]:
		if fields.has(key):receipts.append("presentation_binding:%s"%key)
	row["slaved_update"]={"master_id":0,"leash_range_source":float(_module_contract_value(fields,"LeashRange",0.0)),"guard_max_range_source":float(_module_contract_value(fields,"GuardMaxRange",0.0)),"guard_wander_range_source":float(_module_contract_value(fields,"GuardWanderRange",0.0)),"attack_range_source":float(_module_contract_value(fields,"AttackRange",0.0)),"guard_offset_source":offset,"die_on_master_death":bool(_module_contract_value(fields,"DieOnMastersDeath",false)),"mark_unselectable":bool(_module_contract_value(fields,"MarkUnselectable",false)),"unsupported_semantics":receipts}
	if bool((row["slaved_update"] as Dictionary).get("mark_unselectable",false)):row["ignores_select_all"]=true;row["unselectable"]=true;sim.selected_ids.erase(int(row.get("id",0)))


func bind_slave(slave_id:int,master_id:int)->Dictionary:
	if not sim.entities.has(slave_id):return {"ok":false,"reason":"slave-missing"}
	if not sim.entities.has(master_id) and not sim.structures.has(master_id):return {"ok":false,"reason":"master-missing"}
	var slave:=sim.entities[slave_id] as Dictionary
	if not slave.has("slaved_update"):_attach_module_contracts(slave)
	var policy:=slave.get("slaved_update",{}) as Dictionary
	if policy.is_empty():return {"ok":false,"reason":"typed-slaved-contract-missing"}
	var master:=(sim.structures[master_id] if sim.structures.has(master_id) else sim.entities[master_id]) as Dictionary;policy["master_id"]=master_id;policy["master_kind"]="structure" if sim.structures.has(master_id) else "entity"
	# BFME2 1.06 game.dat, SlavedUpdate.cpp bind path 0x8A1A69: one logic-RNG
	# angle is consumed and pinned at GuardMaxRange. Later wandering is a distinct
	# GuardWanderRange operation (0x8A1FDC); conflating the two changes both the
	# stream position and the authored radii.
	var guard_max =sim._retail_source_to_sim_offset(Vector2(float(policy.get("guard_max_range_source",0.0)),0.0)).x
	var pin_angle =sim.logic_random_real(0.0,TAU)
	policy["guard_pinned_offset"]=Vector2(cos(pin_angle),sin(pin_angle))*guard_max
	policy["guard_position"]=Vector2(master.get("position",Vector2.ZERO))+sim._retail_source_to_sim_offset(Vector2(policy.get("guard_offset_source",Vector2.ZERO)))+Vector2(policy.get("guard_pinned_offset",Vector2.ZERO));policy.erase("guard_wander_destination");slave["slaved_update"]=policy;return {"ok":true,"reason":""}


func _step_slaved_updates()->void:
	for slave_id in sim.entity_ids():
		if not sim.entities.has(slave_id):continue
		var slave:=sim.entities[slave_id] as Dictionary;var policy:=slave.get("slaved_update",{}) as Dictionary
		if policy.is_empty() or int(policy.get("master_id",0))==0:continue
		var master_id:=int(policy.get("master_id"));var table =sim.structures if String(policy.get("master_kind"))=="structure" else sim.entities
		if not table.has(master_id) or int((table[master_id] as Dictionary).get("health",0))<=0:
			if bool(policy.get("die_on_master_death",false)) and int(slave.get("health",0))>0:_kill_slave_for_master_death(slave_id,slave)
			else:policy["master_id"]=0;slave["slaved_update"]=policy
			continue
		var master:=table[master_id] as Dictionary;var guard =Vector2(master.get("position",Vector2.ZERO))+sim._retail_source_to_sim_offset(Vector2(policy.get("guard_offset_source",Vector2.ZERO)))+Vector2(policy.get("guard_pinned_offset",Vector2.ZERO));policy["guard_position"]=guard;slave["slaved_update"]=policy
		# Retail SlavedUpdate priority two: if the master has a current victim and
		# AttackRange is authored, move toward that victim but clamp the goal to a
		# circle around the MASTER. This deliberately does not assign the victim to
		# the slave; the original issues an AI move-to-position command.
		var master_target_id:=int(master.get("target_id",0));var master_target_kind:=String(master.get("target_kind","battalion"));var attack_range =sim._retail_source_to_sim_offset(Vector2(float(policy.get("attack_range_source",0.0)),0.0)).x
		if attack_range>0.0 and master_target_id!=0 and sim._target_alive(master_target_id,master_target_kind):
			var master_position:=Vector2(master.get("position",Vector2.ZERO));var victim_position =sim._target_position(master_target_id,master_target_kind);var attack_position =victim_position;var delta =victim_position-master_position
			if delta.length()>attack_range:attack_position=master_position+delta.normalized()*attack_range
			if sim._assign_route(slave,attack_position):slave["state"]="run"
			else:slave["position"]=attack_position;slave["state"]="idle";sim._spatial_sync(slave)
			continue
		var leash =sim._retail_source_to_sim_offset(Vector2(float(policy.get("leash_range_source",0.0)),0.0)).x;var guard_max =sim._retail_source_to_sim_offset(Vector2(float(policy.get("guard_max_range_source",0.0)),0.0)).x;var beyond =leash>0.0 and Vector2(slave.get("position",Vector2.ZERO)).distance_to(guard)>leash
		var target_id:=int(slave.get("target_id",0));var target_kind:=String(slave.get("target_kind","battalion"));if not beyond and target_id!=0 and guard_max>0.0 and sim._target_alive(target_id,target_kind):beyond=sim._target_position(target_id,target_kind).distance_to(guard)>guard_max
		if beyond:
			slave["target_id"]=0;slave["attack_move"]=false;slave["order_kind"]="";sim._clear_member_targets(slave);sim._clear_pending_route(slave,false)
			# BFME2 1.06 0x8A1FDC uses GuardWanderRange for the later
			# threshold/reroll radius, not GuardMaxRange. Pin one destination for
			# this return order so a per-tick sim step cannot consume extra draws.
			var return_point:=Vector2(policy.get("guard_wander_destination",guard))
			if not policy.has("guard_wander_destination"):
				var wander =sim._retail_source_to_sim_offset(Vector2(float(policy.get("guard_wander_range_source",0.0)),0.0)).x
				if wander>0.0:
					var wander_angle =sim.logic_random_real(0.0,TAU);return_point=guard+Vector2(cos(wander_angle),sin(wander_angle))*wander
				policy["guard_wander_destination"]=return_point;slave["slaved_update"]=policy
			if sim._assign_route(slave,return_point):slave["state"]="run"
			else:slave["position"]=return_point;slave["state"]="idle";sim._spatial_sync(slave)
		else:
			policy.erase("guard_wander_destination");slave["slaved_update"]=policy


func _kill_slave_for_master_death(slave_id:int,slave:Dictionary)->void:
	var members:=slave.get("member_health",[]) as Array;var defeated:Array[int]=[]
	for index in members.size():
		if int(members[index])>0:defeated.append(index);members[index]=0
	slave["member_health"]=members;slave["health"]=0;var verdict =sim._bookkeep_battalion_death(slave_id,slave,"NORMAL",defeated)
	sim._emit_event("slave.master_death",int((slave.get("slaved_update",{}) as Dictionary).get("master_id",0)),slave_id)
	if bool(verdict.get("destroy_object",false)):sim.entities.erase(slave_id)


func _attach_castle_upgrade_contract(row:Dictionary,contract:Dictionary)->void:
	if String(contract.get("extraction",""))!="typed":return
	var fields:=contract.get("fields",{}) as Dictionary;var trigger:=String(_module_contract_value(fields,"TriggeredBy","")).strip_edges();var upgrade:=String(_module_contract_value(fields,"Upgrade","")).strip_edges()
	if trigger=="" or upgrade=="":return
	var receipts:Array[String]=[];var radius =-1.0
	if fields.has("WallUpgradeRadius"):
		var radius_field:=fields.get("WallUpgradeRadius",{}) as Dictionary;var resolved:={"resolved":false,"define":String(radius_field.get("define",radius_field.get("expression","")))};var defines:=sim._rules.get("castle_upgrade_radius_defines",{}) as Dictionary
		if typeof(radius_field.get("value")) in [TYPE_INT,TYPE_FLOAT]:resolved={"resolved":true,"value":float(radius_field.get("value"))}
		elif typeof(defines.get(String(resolved.get("define","")))) in [TYPE_INT,TYPE_FLOAT]:resolved={"resolved":true,"value":float(defines[String(resolved.get("define"))])}
		if bool(resolved.get("resolved",false)):radius=float(resolved.get("value",-1.0))
		else:receipts.append("unresolved_wall_upgrade_radius:%s"%String(resolved.get("define","")))
	var rows:=row.get("castle_upgrade_contracts",[]) as Array
	for value in rows:
		var existing:=value as Dictionary
		if String(existing.get("triggered_by"))==trigger and String(existing.get("upgrade"))==upgrade:return
	rows.append({"triggered_by":trigger,"upgrade":upgrade,"wall_upgrade_radius_source":radius,"unsupported_semantics":receipts,"tag":String(contract.get("tag","")),"line":int(contract.get("line",0))});row["castle_upgrade_contracts"]=rows


func apply_castle_upgrade_trigger(structure_id:int,trigger_upgrade_id:String)->Dictionary:
	if not sim.structures.has(structure_id):return {"ok":false,"reason":"structure-missing"}
	var building:=sim.structures[structure_id] as Dictionary
	if not building.has("castle_upgrade_contracts"):_attach_structure_module_contracts(building)
	var matched:=0
	for value in building.get("castle_upgrade_contracts",[]) as Array:
		var policy:=value as Dictionary
		if String(policy.get("triggered_by",""))!=trigger_upgrade_id:continue
		var upgrade:=String(policy.get("upgrade",""));var recipients:Array[int]=[structure_id]
		for piece in building.get("castle_piece_structure_ids",[]) as Array:recipients.append(int(piece))
		for recipient_id in recipients:
			if not sim.structures.has(recipient_id):continue
			var recipient:=sim.structures[recipient_id] as Dictionary;var completed:=recipient.get("completed_upgrades",[]) as Array
			if not completed.has(upgrade):completed.append(upgrade);recipient["completed_upgrades"]=completed
		matched+=1;sim._emit_event("upgrade.castle_granted",structure_id,0,{"team":int(building.get("team",-1)),"trigger_upgrade_id":trigger_upgrade_id,"upgrade_id":upgrade,"recipient_count":recipients.size()})
	return {"ok":matched>0,"reason":"" if matched>0 else "trigger-not-authored","grants":matched}


func _attach_spawn_behavior_contract(row:Dictionary,contract:Dictionary)->void:
	if String(contract.get("extraction",""))!="typed" or row.has("spawn_behavior"):return
	var fields:=contract.get("fields",{}) as Dictionary;var receipts:Array[String]=[]
	for key in ["FadeInTime","KillSpawnsBasedOnModelConditionState","SpawnInsideBuilding"]:
		if fields.has(key):receipts.append("presentation_or_unresolved_spawn_semantic:%s"%key)
	# Compatibility receipt retained for already-cooked packs. The execution path
	# below is independently matched in BFME2 1.06 and RotWK 2.01 binaries; the
	# shipped compare quirk considers only the last distinct authored template.
	var can_reclaim:=bool(_module_contract_value(fields,"CanReclaimOrphans",false))
	if can_reclaim:receipts.append("deferred_binary_ambiguous:CanReclaimOrphans")
	if bool(_module_contract_value(fields,"RespectCommandLimit",false)):receipts.append("unsupported_spawn_semantic:RespectCommandLimit")
	var template_field:=fields.get("SpawnTemplateName",{}) as Dictionary;var templates:Array[String]=[]
	for template_value in template_field.get("value",[]) as Array:templates.append(String(template_value))
	row["spawn_behavior"]={"spawn_number":int(_module_contract_value(fields,"SpawnNumber",0)),"replace_ticks":_ship_contract_delay_ticks(float(_module_contract_value(fields,"SpawnReplaceDelay",0.0))),"templates":templates,"one_shot":bool(_module_contract_value(fields,"OneShot",false)),"can_reclaim_orphans":can_reclaim,"can_reclaim_runtime":"bfme2-rotwk-binary-proven" if can_reclaim else "disabled","require_spawner":bool(_module_contract_value(fields,"SpawnedRequireSpawner",false)),"share_upgrades":bool(_module_contract_value(fields,"ShareUpgrades",false)),"triggered_by":String(_module_contract_value(fields,"TriggeredBy","")),"initial_remaining":int(_module_contract_value(fields,"InitialBurst",_module_contract_value(fields,"SpawnNumber",0))),"spawned_ids":[],"spawn_serial":0,"spawn_count":-1,"next_spawn_tick":sim.tick_index,"unsupported_semantics":receipts}


func _step_spawn_behaviors()->void:
	var owners:Array=[]
	for id in sim.entity_ids():owners.append({"id":id,"kind":"entity"})
	for id in sim.structure_ids():owners.append({"id":id,"kind":"structure"})
	for owner_value in owners:
		var owner_ref:=owner_value as Dictionary;var table =sim.structures if String(owner_ref.get("kind"))=="structure" else sim.entities;var owner_id:=int(owner_ref.get("id"));if not table.has(owner_id):continue
		var owner:=table[owner_id] as Dictionary;var policy:=owner.get("spawn_behavior",{}) as Dictionary
		if policy.is_empty():continue
		var living:Array[int]=[];var lost_count:=0
		for spawned_value in policy.get("spawned_ids",[]) as Array:
			var spawned_id =int(spawned_value)
			if sim.entities.has(spawned_id) and int((sim.entities[spawned_id] as Dictionary).get("health",0))>0:living.append(spawned_id)
			else:lost_count+=1
		policy["spawned_ids"]=living
		if lost_count>0:policy["spawn_count"]=int(policy.get("spawn_count",-1))-lost_count
		if int(owner.get("health",0))<=0:
			if bool(policy.get("require_spawner",false)):
				for spawned_id in living:
					if sim.entities.has(spawned_id):_kill_slave_for_master_death(spawned_id,sim.entities[spawned_id] as Dictionary)
			else:
				# Proven source-relative onDie seam: surviving children lose their
				# producer and SlavedUpdate master. Whether a later spawner adopts one
				# is the separately receipted binary ambiguity above.
				for spawned_id in living:
					if not sim.entities.has(spawned_id):continue
					var orphan:=sim.entities[spawned_id] as Dictionary
					orphan.erase("spawn_behavior_parent_id");orphan.erase("spawn_behavior_parent_kind")
					orphan["spawn_behavior_orphaned_tick"]=sim.tick_index
					var slave_policy:=orphan.get("slaved_update",{}) as Dictionary
					if not slave_policy.is_empty():slave_policy["master_id"]=0;slave_policy.erase("master_kind");orphan["slaved_update"]=slave_policy
			policy["spawned_ids"]=[];owner["spawn_behavior"]=policy;continue
		var required:=String(policy.get("triggered_by",""));if required!="" and not sim._structure_has_completed_upgrade(owner,required):owner["spawn_behavior"]=policy;continue
		if lost_count>0 and int(policy.get("next_spawn_tick",0))<=sim.tick_index:policy["next_spawn_tick"]=sim.tick_index+int(policy.get("replace_ticks",0))
		var initial:=int(policy.get("initial_remaining",0));var capacity:=int(policy.get("spawn_number",0))-living.size();var due =initial>0 or (not bool(policy.get("one_shot",false)) and sim.tick_index>int(policy.get("next_spawn_tick",0)))
		while capacity>0 and due:
			var spawned_id =_spawn_behavior_member(owner_id,owner,policy);if spawned_id==0:break
			living.append(spawned_id);capacity-=1
			if initial>0:initial-=1;policy["initial_remaining"]=initial
			else:policy["next_spawn_tick"]=sim.tick_index+int(policy.get("replace_ticks",0));break
			due=initial>0
		policy["spawned_ids"]=living;owner["spawn_behavior"]=policy


func _spawn_behavior_member(owner_id:int,owner:Dictionary,policy:Dictionary)->int:
	var templates:=policy.get("templates",[]) as Array;if templates.is_empty():return 0
	# Both binaries reserve an exit door before the orphan scan. With no door the
	# due slot remains queued and neither reclaim nor allocation is attempted.
	if not bool(owner.get("spawn_behavior_exit_door_available",true)):return 0
	var reclaimed_id:=_spawn_behavior_reclaim_orphan(owner_id,owner,policy)
	if reclaimed_id>0:return reclaimed_id
	var serial:=int(policy.get("spawn_serial",0));var template:=String(templates[serial%templates.size()]);var unit_rules:=sim._rules.get("unit_rules",{}) as Dictionary
	var team:=int(owner.get("team",-1));if not sim._next_dynamic_id.has(team):sim._next_dynamic_id[team]=900000+team*1000
	var spawned_id =0
	if (unit_rules.get(template,{}) as Dictionary).is_empty():
		# Neutral SpawnBehavior children are deliberately absent from faction
		# production tables. Resolve them only through the selected scenario-unit
		# descriptor and its authored lair-spawn admission.
		spawned_id=spawn_scenario_unit(template,team,Vector2(owner.get("position",Vector2.ZERO)),"lair-spawn")
	if spawned_id<=0 and (unit_rules.get(template,{}) as Dictionary).is_empty():
		var receipts:=policy.get("unsupported_semantics",[]) as Array
		var receipt ="unresolved_spawn_template:%s"%template
		if not receipts.has(receipt):receipts.append(receipt)
		policy["unsupported_semantics"]=receipts
		return 0
	if spawned_id<=0:
		spawned_id=int(sim._next_dynamic_id[team]);sim._next_dynamic_id[team]=spawned_id+1;sim._add_battalion(spawned_id,team,Vector2(owner.get("position",Vector2.ZERO)),template,template,template,0)
	if not sim.entities.has(spawned_id):
		var receipts:=policy.get("unsupported_semantics",[]) as Array
		var receipt ="unresolved_spawn_template:%s"%template
		if not receipts.has(receipt):receipts.append(receipt)
		policy["unsupported_semantics"]=receipts
		return 0
	var child:=sim.entities[spawned_id] as Dictionary;child["spawn_behavior_parent_id"]=owner_id;child["spawn_behavior_parent_kind"]="structure" if sim.structures.has(owner_id) else "entity"
	if bool(policy.get("share_upgrades",false)):child["completed_upgrades"]=(owner.get("completed_upgrades",[]) as Array).duplicate()
	if child.has("slaved_update"):bind_slave(spawned_id,owner_id)
	policy["spawn_serial"]=serial+1;policy["spawn_count"]=maxi(0,int(policy.get("spawn_count",-1)))+1;sim._emit_event("spawn_behavior.spawned",owner_id,spawned_id,{"template":template});return spawned_id


func _spawn_behavior_reclaim_orphan(owner_id:int,owner:Dictionary,policy:Dictionary)->int:
	if not bool(policy.get("can_reclaim_orphans",false)):return 0
	var templates:=policy.get("templates",[]) as Array
	if templates.is_empty():return 0
	# The shipped loop resets its closest candidate for every distinct name and
	# skips only consecutive duplicates. It returns the final distinct template's
	# candidate, not the nearest candidate across the authored template list.
	var previous_template:="";var candidate_id:=0
	for template_value in templates:
		var template:=String(template_value)
		if template==previous_template:continue
		previous_template=template
		candidate_id=_spawn_behavior_closest_orphan(owner,template)
	if candidate_id<=0 or not sim.entities.has(candidate_id):return 0
	var child:=sim.entities[candidate_id] as Dictionary
	child["spawn_behavior_parent_id"]=owner_id;child["spawn_behavior_parent_kind"]="structure" if sim.structures.has(owner_id) else "entity"
	# The reclaim helper itself consumes no RNG. Canonical SlavedUpdate::onEnslave
	# is transitive behavior: it consumes exactly one logic draw and repins the
	# GuardMax offset without moving the child's physical position.
	if child.has("slaved_update"):bind_slave(candidate_id,owner_id)
	sim._emit_event("spawn_behavior.reclaimed",owner_id,candidate_id,{"template":previous_template,"binary_receipt":"bfme2-rotwk-create-spawn-matched"})
	return candidate_id


func _spawn_behavior_closest_orphan(owner:Dictionary,template:String)->int:
	var controlling_player:=int(owner.get("retail_controlling_player",owner.get("team",-1)))
	var candidates:Array[int]=[]
	for entity_id in sim.entity_ids():
		var candidate =sim.entities[entity_id] as Dictionary
		if int(candidate.get("retail_controlling_player",candidate.get("team",-1)))!=controlling_player:continue
		if not _spawn_behavior_template_equivalent(candidate,template):continue
		if int(candidate.get("spawn_behavior_parent_id",0))!=0 or int(candidate.get("production_producer_id",0))!=0:continue
		var burning:=false
		for condition_value in candidate.get("model_conditions",[]) as Array:
			if String(condition_value).to_upper()=="BURNINGDEATH":burning=true;break
		if burning:continue
		candidates.append(entity_id)
	candidates.sort_custom(func(left:int,right:int)->bool:
		var a:=sim.entities[left] as Dictionary;var b:=sim.entities[right] as Dictionary
		var a_prototype:=int(a.get("retail_team_prototype_ordinal",a.get("team",0)));var b_prototype:=int(b.get("retail_team_prototype_ordinal",b.get("team",0)))
		if a_prototype!=b_prototype:return a_prototype<b_prototype
		var a_instance:=int(a.get("retail_team_instance_ordinal",0));var b_instance:=int(b.get("retail_team_instance_ordinal",0))
		if a_instance!=b_instance:return a_instance>b_instance
		var a_member:=int(a.get("retail_team_member_ordinal",left));var b_member:=int(b.get("retail_team_member_ordinal",right))
		if a_member!=b_member:return a_member>b_member
		return left>right
	)
	var owner_position:=Vector2(owner.get("position",Vector2.ZERO));var maximum_distance =sim._retail_source_to_sim_offset(Vector2(10000.0,0.0)).x;var closest_squared =maximum_distance*maximum_distance;var closest_id:=0
	for entity_id in candidates:
		var candidate =sim.entities[entity_id] as Dictionary;var distance_squared:=owner_position.distance_squared_to(Vector2(candidate.get("position",Vector2.ZERO)))
		# Strict comparison preserves the first retail traversal member on a tie
		# and excludes the exact 10,000-source-unit sentinel boundary.
		if distance_squared<closest_squared:closest_squared=distance_squared;closest_id=entity_id
	return closest_id


func _spawn_behavior_template_equivalent(candidate:Dictionary,template:String)->bool:
	for key in ["object_id","source_object_id","unit_type"]:
		if String(candidate.get(key,"")).nocasecmp_to(template)==0:return true
	for equivalent_value in candidate.get("retail_equivalent_template_ids",[]) as Array:
		if String(equivalent_value).nocasecmp_to(template)==0:return true
	return false


func _attach_stealth_update_contract(row:Dictionary,contract:Dictionary)->void:
	if String(contract.get("extraction",""))!="typed" or row.has("stealth_update"):return
	var fields:=contract.get("fields",{}) as Dictionary;var receipts:Array[String]=[]
	for key in ["FriendlyOpacityMin","FriendlyOpacityMax","PulseFrequency","HintDetectableConditions","DisguisesAsTeam","DisguiseTransitionTime","DisguiseRevealTransitionTime","RemoveTerrainRestrictionOnUpgrade","OrderIdleEnemiesToAttackMeUponReveal"]:
		if fields.has(key):receipts.append("presentation_or_unresolved_stealth_semantic:%s"%key)
	var required_upgrades:Array[String]=[];var required_field:=fields.get("RequiredUpgradeNames",{}) as Dictionary
	for upgrade_value in required_field.get("value",[]) as Array:required_upgrades.append(String(upgrade_value))
	var enabled:=bool(_module_contract_value(fields,"StartsActive",false)) or bool(_module_contract_value(fields,"InnateStealth",false));var delay:=_ship_contract_delay_ticks(float(_module_contract_value(fields,"StealthDelay",0.0)))
	row["stealth_update"]={"enabled":enabled,"delay_ticks":delay,"activation_tick":sim.tick_index+delay,"forbidden":_typed_contract_tokens(fields,"StealthForbiddenConditions"),"reveal_weapon_sets":_typed_contract_tokens(fields,"RevealWeaponSets"),"required_upgrades":required_upgrades,"detected_range_source":float(_module_contract_value(fields,"DetectedByAnyoneRange",0.0)),"reveal_target_range_source":float(_module_contract_value(fields,"RevealDistanceFromTarget",0.0)),"unsupported_semantics":receipts}


func set_stealth_update_active(entity_id:int,enabled:bool)->Dictionary:
	if not sim.entities.has(entity_id):return {"ok":false,"reason":"entity-missing"}
	var row:=sim.entities[entity_id] as Dictionary;if not row.has("stealth_update"):_attach_module_contracts(row)
	var policy:=row.get("stealth_update",{}) as Dictionary;if policy.is_empty():return {"ok":false,"reason":"typed-stealth-contract-missing"}
	policy["enabled"]=enabled;policy["activation_tick"]=sim.tick_index+int(policy.get("delay_ticks",0));row["stealth_update"]=policy;if not enabled:sim._clear_stealth(row)
	return {"ok":true,"reason":"","activation_tick":policy["activation_tick"]}


func _step_stealth_updates()->void:
	for id in sim.entity_ids():
		var row:=sim.entities[id] as Dictionary;var policy:=row.get("stealth_update",{}) as Dictionary
		if policy.is_empty():continue
		var forbidden:=policy.get("forbidden",[]) as Array;var blocked:=false
		if forbidden.has("MOVING") and (Vector2(row.get("destination",row.get("position",Vector2.ZERO))).distance_to(Vector2(row.get("position",Vector2.ZERO)))>0.001 or float(row.get("current_speed",0.0))>0.001):blocked=true
		if forbidden.has("ATTACKING") and String(row.get("state","")) in ["attack","attack-windup"]:blocked=true
		var weapon_flags:=row.get("weapon_set_flags",[]) as Array
		for flag in policy.get("reveal_weapon_sets",[]) as Array:
			if weapon_flags.has(flag):blocked=true;break
		var required_ok:=true
		for upgrade in policy.get("required_upgrades",[]) as Array:
			if not sim._structure_has_completed_upgrade(row,String(upgrade)):required_ok=false;break
		var origin:=Vector2(row.get("position",Vector2.ZERO));var anyone_range =sim._retail_source_to_sim_offset(Vector2(float(policy.get("detected_range_source",0.0)),0.0)).x
		if anyone_range>0.0:
			for other_id in sim.entity_ids():
				if other_id==id:continue
				var other:=sim.entities[other_id] as Dictionary
				if int(other.get("team",-1))!=int(row.get("team",-1)) and origin.distance_to(Vector2(other.get("position",Vector2.ZERO)))<=anyone_range:blocked=true;break
		var target_id:=int(row.get("target_id",0));var reveal_range =sim._retail_source_to_sim_offset(Vector2(float(policy.get("reveal_target_range_source",0.0)),0.0)).x
		if target_id!=0 and reveal_range>0.0 and sim.entities.has(target_id) and origin.distance_to(Vector2((sim.entities[target_id] as Dictionary).get("position",Vector2.ZERO)))<=reveal_range:blocked=true
		if blocked or not bool(policy.get("enabled",false)) or not required_ok:
			if sim._stealth_active(row):sim._clear_stealth(row)
			policy["activation_tick"]=sim.tick_index+int(policy.get("delay_ticks",0));row["stealth_update"]=policy;continue
		if not sim._stealth_active(row) and sim.tick_index>=int(policy.get("activation_tick",0)):sim._grant_stealth(row,0x3fffffff,forbidden)


func _attach_object_creation_upgrade_contract(row:Dictionary,contract:Dictionary)->void:
	if String(contract.get("extraction",""))!="typed":return
	var fields:=contract.get("fields",{}) as Dictionary;var triggers:Array[String]=[];var conflicts:Array[String]=[]
	for value in (fields.get("TriggeredBy",{}) as Dictionary).get("value",[]) as Array:triggers.append(String(value))
	for value in (fields.get("ConflictsWith",{}) as Dictionary).get("value",[]) as Array:conflicts.append(String(value))
	var delay:=_resolve_contract_milliseconds(fields.get("Delay"),"object_creation_delay_defines");var receipts:Array[String]=[]
	if fields.has("Delay") and not bool(delay.get("resolved",false)):receipts.append("unresolved_creation_delay:%s"%String(delay.get("expression","")))
	for key in ["FadeInTime","DeathAnimAndDuration"]:
		if fields.has(key):receipts.append("presentation_binding:%s"%key)
	if bool(_module_contract_value(fields,"DestroyWhenSold",false)):receipts.append("unsupported_sale_semantic:DestroyWhenSold")
	if bool(_module_contract_value(fields,"UseBuildingProduction",false)):receipts.append("unsupported_creation_semantic:UseBuildingProduction")
	var offset:=Vector2.ZERO
	if typeof(fields.get("Offset"))==TYPE_DICTIONARY:
		var coord:=((fields.get("Offset") as Dictionary).get("value",{}) as Dictionary);offset=Vector2(float(coord.get("x",0.0)),float(coord.get("y",0.0)))
	var rows:=row.get("object_creation_upgrades",[]) as Array
	rows.append({"triggers":triggers,"requires_all":bool(_module_contract_value(fields,"RequiresAllTriggers",false)),"conflicts":conflicts,"delay_ticks":int(delay.get("ticks",0)) if bool(delay.get("resolved",false)) else -1,"offset_source":offset,"thing_to_spawn":String(_module_contract_value(fields,"ThingToSpawn","")),"grant_upgrade":String(_module_contract_value(fields,"GrantUpgrade","")),"remove_upgrade":String(_module_contract_value(fields,"RemoveUpgrade","")),"upgrade_object":String(_module_contract_value(fields,"UpgradeObject","")),"scheduled_tick":-1,"consumed":false,"spawned_ids":[],"unsupported_semantics":receipts,"tag":String(contract.get("tag","")),"line":int(contract.get("line",0))});row["object_creation_upgrades"]=rows


func _attach_attribute_modifier_upgrade_contract(row: Dictionary, contract: Dictionary) -> void:
	## AttributeModifierUpgrade is a persistent upgrade mux. The module remains
	## importer-deferred until independent acceptance; this consumer only accepts
	## the exact typed field shape and a fully resolved shared ModifierList.
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields := contract.get("fields", {}) as Dictionary
	var triggers := _typed_contract_tokens(fields, "TriggeredBy")
	var modifier_name := String(_module_contract_value(fields, "AttributeModifier", "")).strip_edges()
	if triggers.is_empty() or modifier_name == "":
		return
	var requires_all_value: Variant = _module_contract_value(fields, "RequiresAllTriggers", false)
	if typeof(requires_all_value) != TYPE_BOOL:
		return
	var modifier := ((sim._rules.get("attribute_modifier_rules", {}) as Dictionary).get(modifier_name, {}) as Dictionary).duplicate(true)
	var unsupported: Array[String] = []
	if modifier.is_empty() or (modifier.get("effects", []) as Array).is_empty():
		unsupported.append("unresolved_modifier_list:%s" % modifier_name)
	var presentation: Array[String] = []
	var deferred_value: Variant = fields.get("deferredFields", [])
	if typeof(deferred_value) != TYPE_ARRAY:
		return
	for deferred_row_value in deferred_value as Array:
		if typeof(deferred_row_value) != TYPE_DICTIONARY:
			return
		var deferred_row := deferred_row_value as Dictionary
		if String(deferred_row.get("name", "")) != "CustomAnimAndDuration":
			unsupported.append("unsupported_deferred_field:%s" % String(deferred_row.get("name", "")))
			continue
		presentation.append("CustomAnimAndDuration:%s" % String(deferred_row.get("authored", "")))
	var policies := row.get("attribute_modifier_upgrades", []) as Array
	var tag := String(contract.get("tag", ""))
	var line := int(contract.get("line", 0))
	for existing_value in policies:
		var existing := existing_value as Dictionary
		if String(existing.get("tag", "")) == tag and int(existing.get("line", -1)) == line:
			return
	policies.append({
		"triggers": triggers,
		"requires_all": bool(requires_all_value),
		"conflicts": _typed_contract_tokens(fields, "ConflictsWith"),
		"modifier_name": modifier_name,
		"modifier": modifier,
		"active": false,
		"unsupported_semantics": unsupported,
		"presentation_receipts": presentation,
		"tag": tag,
		"line": line,
	})
	row["attribute_modifier_upgrades"] = policies
	_reconcile_attribute_modifier_upgrades(row)


func _attribute_modifier_upgrade_owned(row: Dictionary, upgrade_id: String) -> bool:
	if _aura_has_upgrade(row.get("completed_upgrades", []) as Array, row.get("applied_upgrades", {}) as Dictionary, upgrade_id):
		return true
	return _aura_has_upgrade([], sim.team_upgrades.get(int(row.get("team", -1)), {}) as Dictionary, upgrade_id)


func _attribute_modifier_upgrade_should_activate(row: Dictionary, policy: Dictionary) -> bool:
	if not (policy.get("unsupported_semantics", []) as Array).is_empty():
		return false
	for conflict_value in policy.get("conflicts", []) as Array:
		if _attribute_modifier_upgrade_owned(row, String(conflict_value)):
			return false
	var triggers := policy.get("triggers", []) as Array
	if bool(policy.get("requires_all", false)):
		for trigger_value in triggers:
			if not _attribute_modifier_upgrade_owned(row, String(trigger_value)):
				return false
		return not triggers.is_empty()
	for trigger_value in triggers:
		if _attribute_modifier_upgrade_owned(row, String(trigger_value)):
			return true
	return false


func _attribute_modifier_upgrade_key(policy: Dictionary) -> String:
	var modifier := policy.get("modifier", {}) as Dictionary
	var category := String(modifier.get("category", ""))
	var stacking := modifier.get("stacking", {}) as Dictionary
	if category != "" and bool(stacking.get("replaceInCategoryIfLongest", false)):
		return "attribute-modifier-upgrade-category:%s" % category
	return "attribute-modifier-upgrade:%s:%d" % [String(policy.get("tag", "")), int(policy.get("line", 0))]


func _reconcile_attribute_modifier_upgrades(row: Dictionary) -> void:
	var policies := row.get("attribute_modifier_upgrades", []) as Array
	if policies.is_empty():
		return
	var table = row.get("timed_modifiers", {}) as Dictionary
	# Remove every entry owned by this module family, then deterministically
	# rebuild active entries. This makes conflict/removal and category replacement
	# independent of the order in which upgrade APIs were called.
	for key_value in table.keys().duplicate():
		if bool((table[key_value] as Dictionary).get("attribute_modifier_upgrade", false)):
			table.erase(key_value)
	for policy_value in policies:
		var policy := policy_value as Dictionary
		var active := _attribute_modifier_upgrade_should_activate(row, policy)
		policy["active"] = active
		if not active:
			continue
		var modifier := policy.get("modifier", {}) as Dictionary
		var category := String(modifier.get("category", ""))
		var stacking := modifier.get("stacking", {}) as Dictionary
		if (
			bool(stacking.get("ignoreIfAnticategoryActive", false))
			and category == "LEADERSHIP"
			and sim._refresh_leadership_suppression(row) > sim.tick_index
		):
			continue
		table[_attribute_modifier_upgrade_key(policy)] = {
			"modifiers": (modifier.get("effects", []) as Array).duplicate(true),
			# Persistent upgrades share the modifier-effect/category core but are
			# removed only by this lifecycle reconciler, never by wall/tick expiry.
			"persistent": true,
			"category": category,
			"modifier_id": String(policy.get("modifier_name", "")),
			"module_tag": String(policy.get("tag", "")),
			"attribute_modifier_upgrade": true,
		}
	row["timed_modifiers"] = table
	row["attribute_modifier_upgrades"] = policies


func _step_attribute_modifier_upgrades() -> void:
	for entity_id in sim.entity_ids():
		var row := sim.entities[entity_id] as Dictionary
		if not row.has("attribute_modifier_upgrades"):
			_attach_module_contracts(row)
		_reconcile_attribute_modifier_upgrades(row)
	for structure_id in sim.structure_ids():
		var row := sim.structures[structure_id] as Dictionary
		if not row.has("attribute_modifier_upgrades"):
			_attach_structure_module_contracts(row)
		_reconcile_attribute_modifier_upgrades(row)


func _attach_geometry_upgrade_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields := contract.get("fields", {}) as Dictionary
	var triggers := _typed_contract_tokens(fields, "TriggeredBy")
	if triggers.is_empty():
		return
	var requires_all_value: Variant = _module_contract_value(fields, "RequiresAllTriggers", false)
	if typeof(requires_all_value) != TYPE_BOOL:
		return
	var unsupported: Array[String] = []
	var deferred_value: Variant = fields.get("deferredFields", [])
	if typeof(deferred_value) != TYPE_ARRAY:
		return
	for receipt_value in deferred_value as Array:
		if typeof(receipt_value) != TYPE_DICTIONARY:
			return
		var receipt = receipt_value as Dictionary
		var name := String(receipt.get("name", ""))
		if name not in ["CustomAnimAndDuration", "WallBoundsMesh", "RampMesh1", "RampMesh2"]:
			unsupported.append("unsupported_deferred_field:%s" % name)
		else:
			unsupported.append("%s:%s" % [name, String(receipt.get("authored", ""))])
	var policies := row.get("geometry_upgrades", []) as Array
	var tag := String(contract.get("tag", ""))
	var line := int(contract.get("line", 0))
	for existing_value in policies:
		var existing := existing_value as Dictionary
		if String(existing.get("tag", "")) == tag and int(existing.get("line", -1)) == line:
			return
	policies.append({
		"triggers": triggers,
		"requires_all": bool(requires_all_value),
		"conflicts": _typed_contract_tokens(fields, "ConflictsWith"),
		"show_geometry": _typed_contract_raw_tokens(fields, "ShowGeometry"),
		"hide_geometry": _typed_contract_raw_tokens(fields, "HideGeometry"),
		"active": false,
		"unsupported_semantics": unsupported,
		"tag": tag,
		"line": line,
	})
	row["geometry_upgrades"] = policies
	_reconcile_geometry_upgrades(row)


func _geometry_upgrade_should_activate(row: Dictionary, policy: Dictionary) -> bool:
	for conflict_value in policy.get("conflicts", []) as Array:
		if _attribute_modifier_upgrade_owned(row, String(conflict_value)):
			return false
	var triggers := policy.get("triggers", []) as Array
	if bool(policy.get("requires_all", false)):
		for trigger_value in triggers:
			if not _attribute_modifier_upgrade_owned(row, String(trigger_value)):
				return false
		return not triggers.is_empty()
	for trigger_value in triggers:
		if _attribute_modifier_upgrade_owned(row, String(trigger_value)):
			return true
	return false


func _reconcile_geometry_upgrades(row: Dictionary) -> void:
	var policies := row.get("geometry_upgrades", []) as Array
	if policies.is_empty():
		return
	# Fold in source order. Hide follows show inside one module and therefore
	# wins an internally contradictory authored row without relying on hash order.
	var state_by_token: Dictionary = {}
	var authored_token: Dictionary = {}
	var operations: Array[Dictionary] = []
	for policy_value in policies:
		var policy := policy_value as Dictionary
		var active := _geometry_upgrade_should_activate(row, policy)
		policy["active"] = active
		if not active:
			continue
		for shown_value in policy.get("show_geometry", []) as Array:
			var shown := String(shown_value)
			var shown_key := shown.to_upper()
			state_by_token[shown_key] = true
			authored_token[shown_key] = shown
			operations.append({"token": shown, "visible": true, "tag": String(policy.get("tag", "")), "line": int(policy.get("line", 0))})
		for hidden_value in policy.get("hide_geometry", []) as Array:
			var hidden := String(hidden_value)
			var hidden_key := hidden.to_upper()
			state_by_token[hidden_key] = false
			authored_token[hidden_key] = hidden
			operations.append({"token": hidden, "visible": false, "tag": String(policy.get("tag", "")), "line": int(policy.get("line", 0))})
	var shown_tokens: Array[String] = []
	var hidden_tokens: Array[String] = []
	var keys := state_by_token.keys()
	keys.sort()
	for key_value in keys:
		var token := String(authored_token[key_value])
		if bool(state_by_token[key_value]):
			shown_tokens.append(token)
		else:
			hidden_tokens.append(token)
	row["geometry_upgrades"] = policies
	if shown_tokens.is_empty() and hidden_tokens.is_empty():
		row.erase("geometry_visibility")
	else:
		row["geometry_visibility"] = {"show": shown_tokens, "hide": hidden_tokens, "operations": operations}


func _step_geometry_upgrades() -> void:
	for entity_id in sim.entity_ids():
		var row := sim.entities[entity_id] as Dictionary
		if not row.has("geometry_upgrades"):
			_attach_module_contracts(row)
		_reconcile_geometry_upgrades(row)
	for structure_id in sim.structure_ids():
		var row := sim.structures[structure_id] as Dictionary
		if not row.has("geometry_upgrades"):
			_attach_structure_module_contracts(row)
		_reconcile_geometry_upgrades(row)


func _emotion_expression_value(fields: Dictionary, key: String, unsupported: Array[String]) -> float:
	var raw: Variant = fields.get(key, {})
	if typeof(raw) != TYPE_DICTIONARY:
		return 0.0
	var field := raw as Dictionary
	if typeof(field.get("value")) in [TYPE_INT, TYPE_FLOAT]:
		return float(field.get("value"))
	var expression := String(field.get("expression", ""))
	var defines := sim._rules.get("emotion_range_defines", {}) as Dictionary
	if typeof(defines.get(expression)) in [TYPE_INT, TYPE_FLOAT]:
		return float(defines[expression])
	if expression != "":
		unsupported.append("unresolved_expression:%s=%s" % [key, expression])
	return 0.0


func _attach_emotion_tracker_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("emotion_tracker"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var unsupported: Array[String] = []
	var emotions: Array[Dictionary] = []
	var emotion_value: Variant = fields.get("AddEmotion", [])
	if typeof(emotion_value) != TYPE_ARRAY:
		return
	for emotion_row_value in emotion_value as Array:
		if typeof(emotion_row_value) != TYPE_DICTIONARY:
			return
		var emotion_row := emotion_row_value as Dictionary
		var name := String(emotion_row.get("name", "")).strip_edges()
		if name == "":
			return
		var duration_ticks := -1
		if emotion_row.has("Duration"):
			var duration = emotion_row.get("Duration", {}) as Dictionary
			if typeof(duration.get("milliseconds")) not in [TYPE_INT, TYPE_FLOAT]:
				return
			duration_ticks = _ship_contract_delay_ticks(float(duration.get("milliseconds")))
		emotions.append({"name": name, "override": bool(emotion_row.get("override", false)), "duration_ticks": duration_ticks, "line": int(emotion_row.get("line", 0))})
	var quarrel := fields.get("QuarrelProbability", {}) as Dictionary
	var quarrel_fraction := float(quarrel.get("fraction", 0.0))
	if quarrel_fraction > 0.0:
		unsupported.append("quarrel_requires_retail_idle-social-pairing")
	if fields.has("TauntAndPointDistance") or fields.has("PointAt") or fields.has("TauntAndPointExcluded"):
		unsupported.append("taunt_and_point_requires_presentation_pairing")
	row["emotion_tracker"] = {
		"afraid_of": _typed_contract_tokens(fields, "AfraidOf"),
		"always_afraid_of": _typed_contract_tokens(fields, "AlwaysAfraidOf"),
		"fear_scan_distance_source": _emotion_expression_value(fields, "FearScanDistance", unsupported),
		"taunt_distance_source": _emotion_expression_value(fields, "TauntAndPointDistance", unsupported),
		"hero_scan_distance_source": _emotion_expression_value(fields, "HeroScanDistance", unsupported),
		"taunt_update_ticks": _ship_contract_delay_ticks(float(_module_contract_value(fields, "TauntAndPointUpdateDelay", 0.0))),
		"quarrel_fraction": quarrel_fraction,
		"immune_to_fear_level": int(_module_contract_value(fields, "ImmuneToFearLevel", 0)),
		"ignore_veterancy": bool(_module_contract_value(fields, "IgnoreVeterancy", false)),
		"emotions": emotions,
		"unsupported_semantics": unsupported,
		"tag": String(contract.get("tag", "")), "line": int(contract.get("line", 0)),
	}


func trigger_entity_emotion(entity_id: int, emotion_name: String, duration_ticks: int = -1) -> Dictionary:
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity-missing"}
	var row := sim.entities[entity_id] as Dictionary
	if not row.has("emotion_tracker"):
		_attach_module_contracts(row)
	var policy := row.get("emotion_tracker", {}) as Dictionary
	if policy.is_empty():
		return {"ok": false, "reason": "typed-emotion-tracker-missing"}
	var selected: Dictionary = {}
	for emotion_value in policy.get("emotions", []) as Array:
		var emotion := emotion_value as Dictionary
		if String(emotion.get("name", "")).to_upper() == emotion_name.to_upper():
			selected = emotion
			break
	if selected.is_empty():
		return {"ok": false, "reason": "emotion-not-authored:%s" % emotion_name}
	var authored_ticks := int(selected.get("duration_ticks", -1))
	var effective_ticks := authored_ticks if authored_ticks >= 0 else duration_ticks
	if effective_ticks < 0:
		return {"ok": false, "reason": "emotion-duration-unresolved"}
	row["active_emotion"] = String(selected.get("name", ""))
	row["active_emotion_until_tick"] = sim.tick_index + effective_ticks
	row["active_emotion_override"] = bool(selected.get("override", false))
	sim._emit_event("emotion.triggered", entity_id, 0, {"emotion": row["active_emotion"], "duration_ticks": effective_ticks, "presentation": "emotion-nugget-animation-unresolved"})
	return {"ok": true, "reason": "", "emotion": row["active_emotion"], "duration_ticks": effective_ticks}


func _step_emotion_trackers() -> void:
	for entity_id in sim.entity_ids():
		var row := sim.entities[entity_id] as Dictionary
		if not row.has("emotion_tracker"):
			_attach_module_contracts(row)
		if row.has("active_emotion_until_tick") and sim.tick_index >= int(row.get("active_emotion_until_tick", -1)):
			row.erase("active_emotion")
			row.erase("active_emotion_until_tick")
			row.erase("active_emotion_override")
			sim._emit_event("emotion.expired", entity_id, 0, {})


func _attach_castle_member_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("castle_member_behavior"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var counts_value: Variant = _module_contract_value(fields, "CountsForEvaCastleBreached", true)
	var store_value: Variant = _module_contract_value(fields, "StoreUpgradePrice", false)
	if typeof(counts_value) != TYPE_BOOL or typeof(store_value) != TYPE_BOOL:
		return
	var presentation: Dictionary = {}
	for key in ["BeingBuiltSound", "CampDestroyedOwnerEvaEvent", "CampDestroyedAllyEvaEvent", "CampDestroyedAttackerEvaEvent"]:
		var value := String(_module_contract_value(fields, key, "")).strip_edges()
		if value != "":
			presentation[key] = value
	var unsupported: Array[String] = []
	if bool(store_value):
		# Retail source says this overloads the refund price with purchased
		# upgrades. The sim has upgrade costs, but no typed CastleMember field
		# states whether the later refund is sale, capture, or destruction and at
		# what percentage; retaining the policy is safer than inventing money.
		unsupported.append("StoreUpgradePrice:refund-route-and-percentage-unresolved")
	if presentation.has("BeingBuiltSound"):
		unsupported.append("BeingBuiltSound:presentation-audio-route")
	row["castle_member_behavior"] = {
		"is_castle_member": true,
		"counts_for_eva_castle_breached": bool(counts_value),
		"store_upgrade_price": bool(store_value),
		"presentation": presentation,
		"unsupported_semantics": unsupported,
		"breach_dispatched": false,
		"tag": String(contract.get("tag", "")), "line": int(contract.get("line", 0)),
	}


func _dispatch_castle_member_destroyed(structure_id: int, member: Dictionary, attacker_id: int, reason: String) -> void:
	var policy := member.get("castle_member_behavior", {}) as Dictionary
	if policy.is_empty() or bool(policy.get("breach_dispatched", false)):
		return
	policy["breach_dispatched"] = true
	member["castle_member_behavior"] = policy
	var presentation := policy.get("presentation", {}) as Dictionary
	var attacker_team := int((sim.entities.get(attacker_id, {}) as Dictionary).get("team", -1))
	var owner_team := int(member.get("team", -1))
	var eva_routes := {
		"owner": String(presentation.get("CampDestroyedOwnerEvaEvent", "")),
		"ally": String(presentation.get("CampDestroyedAllyEvaEvent", "")),
		"attacker": String(presentation.get("CampDestroyedAttackerEvaEvent", "")),
	}
	sim._emit_event("castle.member_destroyed", attacker_id, structure_id, {"team": owner_team, "attacker_team": attacker_team, "reason": reason, "counts_for_breach": bool(policy.get("counts_for_eva_castle_breached", true)), "eva_routes": eva_routes, "presentation_dispatch": "retail_slice_audio_or_eva_binding_required"})
	if bool(policy.get("counts_for_eva_castle_breached", true)):
		sim._emit_event("castle.breached", attacker_id, structure_id, {"team": owner_team, "attacker_team": attacker_team, "reason": reason})


func _attach_inactive_body_contract(row: Dictionary, contract: Dictionary) -> void:
	## InactiveBody has no authored activation transition: presence is the whole
	## retail body policy and means no damage/body state changes are accepted.
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields := contract.get("fields", {}) as Dictionary
	if fields.size() != 1 or typeof(fields.get("indestructible")) != TYPE_BOOL or not bool(fields.get("indestructible")):
		return
	row["inactive_body"] = {"indestructible": true, "tag": String(contract.get("tag", "")), "line": int(contract.get("line", 0))}
	row["indestructible"] = true


func _attach_squish_collide_contract(row: Dictionary, contract: Dictionary) -> void:
	## Fieldless victim-side marker. Damage, levels, and speed are authored on
	## locomotor/scalar/weapon contracts and stay in the shared crush core.
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields: Variant = contract.get("fields", {})
	if typeof(fields) != TYPE_DICTIONARY or not (fields as Dictionary).is_empty():
		return
	row["squish_collide"] = {"admission": "authored-victim-collision", "tag": String(contract.get("tag", "")), "line": int(contract.get("line", 0))}


func _squish_collision_admitted(victim: Dictionary) -> bool:
	if not victim.has("squish_collide"):
		_attach_module_contracts(victim)
	if victim.has("squish_collide"):
		return true
	# Synthetic/legacy rows without any selected descriptor retain the historic
	# trample lane. Once a descriptor exists, absence of SquishCollide is an
	# authored refusal rather than a fallback.
	return not victim.has("module_contracts")


func _attach_horde_member_collide_contract(row: Dictionary, contract: Dictionary) -> void:
	## Retail's fieldless marker opts an individual member body into the horde
	## collision resolver. This sim currently integrates one battalion transform;
	## formation offsets are attack/presentation coordinates, not independent
	## collision bodies. Preserve the authored marker and exact missing seam, but
	## do not invent separation impulses or mutate the aggregate route.
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields: Variant = contract.get("fields", {})
	if typeof(fields) != TYPE_DICTIONARY or not (fields as Dictionary).is_empty():
		return
	row["horde_member_collide"] = {
		"enabled": true,
		"execution": "deferred-individual-member-collision-world",
		"unsupported_semantics": ["independent-member-body", "member-separation-impulse"],
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}


func _attach_notify_crushing_contract(row: Dictionary, contract: Dictionary) -> void:
	## This marker has no authored cadence, scan radius, probability, or target
	## response. The current crush core can prove actual contact but cannot infer
	## when retail considered a collision probable. Keep an executable-boundary
	## receipt rather than emitting a late or guessed warning.
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields: Variant = contract.get("fields", {})
	if typeof(fields) != TYPE_DICTIONARY or not (fields as Dictionary).is_empty():
		return
	row["notify_imminent_crushing"] = {
		"enabled": true,
		"execution": "deferred-engine-probability-scan",
		"unsupported_semantics": ["scan-cadence", "prediction-range", "target-evasion-response"],
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}


func _attach_flammable_update_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("flammable_update"):
		return
	var raw_fields: Variant = contract.get("fields", {})
	if typeof(raw_fields) != TYPE_DICTIONARY:
		return
	var fields := raw_fields as Dictionary
	var allowed := ["AflameDuration", "AflameDamageDelay", "FlameDamageExpiration", "BurnedDelay", "AflameDamageAmount", "FlameDamageLimit", "BurnContained", "SetBurnedStatus", "DamageType", "FireFXList", "BurningSoundName"]
	for key_value in fields.keys():
		if String(key_value) not in allowed:
			return
	for bool_key in ["BurnContained", "SetBurnedStatus"]:
		if fields.has(bool_key) and typeof(_module_contract_value(fields, bool_key, null)) != TYPE_BOOL:
			return
	if fields.has("FireFXList") and typeof(fields.get("FireFXList")) != TYPE_ARRAY:
		return
	var policy := {
		"aflame": false,
		"flame_damage_accumulated": 0.0,
		"flame_damage_expire_tick": -1,
		"aflame_until_tick": -1,
		"next_damage_tick": -1,
		"burned_tick": -1,
		"burn_contained": bool(_module_contract_value(fields, "BurnContained", false)),
		"set_burned_status": bool(_module_contract_value(fields, "SetBurnedStatus", false)),
		"damage_type": String(_module_contract_value(fields, "DamageType", "")),
		"fire_fx": (fields.get("FireFXList", []) as Array).duplicate(true),
		"burning_sound_id": String(_module_contract_value(fields, "BurningSoundName", "")),
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
		"unsupported_semantics": [],
	}
	var numeric_fields := {
		"AflameDuration": "aflame_duration_ticks",
		"AflameDamageDelay": "damage_delay_ticks",
		"FlameDamageExpiration": "flame_expiration_ticks",
		"BurnedDelay": "burned_delay_ticks",
		"AflameDamageAmount": "damage_amount",
		"FlameDamageLimit": "flame_damage_limit",
	}
	for field_name_value in numeric_fields:
		var field_name := String(field_name_value)
		if not fields.has(field_name):
			continue
		var value: Variant = _module_contract_value(fields, field_name, null)
		if typeof(value) not in [TYPE_INT, TYPE_FLOAT]:
			(policy["unsupported_semantics"] as Array).append("unresolved-expression:" + field_name)
			continue
		if float(value) < 0.0:
			return
		var target_key := String(numeric_fields[field_name])
		if field_name in ["AflameDuration", "AflameDamageDelay", "FlameDamageExpiration", "BurnedDelay"]:
			policy[target_key] = _ship_contract_delay_ticks(float(value))
		else:
			policy[target_key] = float(value)
	row["flammable_update"] = policy


func record_flame_damage(object_id: int, amount: float) -> Dictionary:
	var table = sim.structures if sim.structures.has(object_id) else sim.entities
	if not table.has(object_id):
		return {"ok": false, "reason": "object-missing"}
	var row := table[object_id] as Dictionary
	if not row.has("flammable_update"):
		if sim.structures.has(object_id): _attach_structure_module_contracts(row)
		else: _attach_module_contracts(row)
	var policy := row.get("flammable_update", {}) as Dictionary
	if policy.is_empty():
		return {"ok": false, "reason": "typed-flammable-contract-missing"}
	for required in ["aflame_duration_ticks", "damage_delay_ticks", "flame_expiration_ticks", "damage_amount", "flame_damage_limit"]:
		if not policy.has(required):
			return {"ok": false, "reason": "unresolved-flammable-field", "field": required}
	if sim.entities.has(object_id) and sim.entity_container.has(object_id) and not bool(policy.get("burn_contained", false)):
		return {"ok": false, "reason": "contained-burning-disabled"}
	if sim.tick_index > int(policy.get("flame_damage_expire_tick", -1)):
		policy["flame_damage_accumulated"] = 0.0
	policy["flame_damage_accumulated"] = float(policy.get("flame_damage_accumulated", 0.0)) + maxf(0.0, amount)
	policy["flame_damage_expire_tick"] = sim.tick_index + maxi(0, int(policy.get("flame_expiration_ticks", 0)))
	var ignited := false
	if not bool(policy.get("aflame", false)) and float(policy.get("flame_damage_accumulated", 0.0)) + 0.0001 >= float(policy.get("flame_damage_limit", INF)):
		ignited = true
		policy["aflame"] = true
		policy["aflame_until_tick"] = sim.tick_index + maxi(1, int(policy.get("aflame_duration_ticks", 0)))
		policy["next_damage_tick"] = sim.tick_index + maxi(1, int(policy.get("damage_delay_ticks", 0)))
		policy["burned_tick"] = sim.tick_index + maxi(0, int(policy.get("burned_delay_ticks", 0))) if bool(policy.get("set_burned_status", false)) else -1
		_set_row_object_status(row, "AFLAME", true)
		if row.has("fire_spread_update"):
			set_fire_spread_active(object_id, true)
		sim._emit_event("module.flammable_ignited", object_id, 0, {"aflame_until_tick": policy["aflame_until_tick"], "fire_fx": policy.get("fire_fx", []), "burning_sound_id": policy.get("burning_sound_id", "")})
	row["flammable_update"] = policy
	return {"ok": true, "reason": "", "ignited": ignited, "aflame": bool(policy.get("aflame", false)), "accumulated": float(policy.get("flame_damage_accumulated", 0.0))}


func _step_flammable_updates() -> void:
	var ids: Array[Dictionary] = []
	for id in sim.entity_ids(): ids.append({"id": id, "kind": "entity"})
	for id in sim.structure_ids(): ids.append({"id": id, "kind": "structure"})
	for value in ids:
		var item := value as Dictionary
		var object_id := int(item.get("id", 0))
		var is_structure := String(item.get("kind", "")) == "structure"
		var table = sim.structures if is_structure else sim.entities
		if not table.has(object_id): continue
		var row := table[object_id] as Dictionary
		var policy := row.get("flammable_update", {}) as Dictionary
		if policy.is_empty() or not bool(policy.get("aflame", false)): continue
		if bool(policy.get("set_burned_status", false)) and int(policy.get("burned_tick", -1)) >= 0 and sim.tick_index >= int(policy.get("burned_tick", -1)):
			_set_row_object_status(row, "BURNED", true)
			policy["burned_tick"] = -1
		if sim.tick_index >= int(policy.get("aflame_until_tick", -1)):
			policy["aflame"] = false
			policy["next_damage_tick"] = -1
			_set_row_object_status(row, "AFLAME", false)
			if row.has("fire_spread_update"): set_fire_spread_active(object_id, false)
			row["flammable_update"] = policy
			sim._emit_event("module.flammable_extinguished", object_id, 0, {})
			continue
		if sim.tick_index < int(policy.get("next_damage_tick", -1)): continue
		policy["next_damage_tick"] = sim.tick_index + maxi(1, int(policy.get("damage_delay_ticks", 1)))
		row["flammable_update"] = policy
		row["flammable_internal_damage"] = true
		var damage := maxi(0, roundi(float(policy.get("damage_amount", 0.0))))
		var damage_type := String(policy.get("damage_type", "FLAME"))
		if is_structure: sim._apply_structure_damage(-1, object_id, damage, damage_type)
		else: sim._apply_damage(-1, object_id, damage, "battalion", "BURNED", damage_type)
		row.erase("flammable_internal_damage")
		sim._emit_event("module.flammable_damage", object_id, object_id, {"amount": damage, "damage_type": damage_type})


# De-staticed on extraction (instance sim access).
func _set_row_object_status(row: Dictionary, status: String, enabled: bool) -> void:
	var statuses := row.get("object_status", {}) as Dictionary
	if enabled: statuses[status] = true
	else: statuses.erase(status)
	if statuses.is_empty(): row.erase("object_status")
	else: row["object_status"] = statuses


func _attach_dynamic_portal_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("dynamic_portal"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	for required in ["ObjectFilter", "BonePrefix", "NumberOfBones", "WayPoint", "Link"]:
		if not fields.has(required): return
	var filter_value: Variant = _module_contract_value(fields, "ObjectFilter", [])
	var prefix_value: Variant = _module_contract_value(fields, "BonePrefix", "")
	var bone_count_value: Variant = _module_contract_value(fields, "NumberOfBones", 0)
	var waypoint_value: Variant = fields.get("WayPoint")
	var link_value: Variant = fields.get("Link")
	if typeof(filter_value) != TYPE_ARRAY or (filter_value as Array).is_empty() or String(prefix_value) == "" or typeof(bone_count_value) != TYPE_INT or int(bone_count_value) <= 0 or typeof(waypoint_value) != TYPE_ARRAY or typeof(link_value) != TYPE_ARRAY:
		return
	var waypoints := (waypoint_value as Array).duplicate(true)
	var links := (link_value as Array).duplicate(true)
	if waypoints.is_empty() or links.is_empty(): return
	for waypoint_value_row in waypoints:
		if typeof(waypoint_value_row) != TYPE_DICTIONARY: return
		var waypoint := waypoint_value_row as Dictionary
		if typeof(waypoint.get("index")) != TYPE_INT or int(waypoint.get("index")) < 0 or String(waypoint.get("type", "")) == "": return
	for link_value_row in links:
		if typeof(link_value_row) != TYPE_DICTIONARY: return
		var link := link_value_row as Dictionary
		if typeof(link.get("from")) != TYPE_INT or typeof(link.get("to")) != TYPE_INT or typeof(link.get("via", [])) != TYPE_ARRAY: return
		var route_indices: Array = [int(link.get("from"))]
		route_indices.append_array(link.get("via", []) as Array)
		route_indices.append(int(link.get("to")))
		for route_index_value in route_indices:
			if typeof(route_index_value) != TYPE_INT or int(route_index_value) < 0 or int(route_index_value) >= waypoints.size(): return
	var delay_field: Variant = fields.get("ActivationDelaySeconds", {})
	var delay_value: Variant = 0.0
	if typeof(delay_field) == TYPE_DICTIONARY and not (delay_field as Dictionary).is_empty():
		if (delay_field as Dictionary).has("milliseconds"): delay_value = (delay_field as Dictionary).get("milliseconds")
		elif (delay_field as Dictionary).has("value"): delay_value = (delay_field as Dictionary).get("value")
		else: delay_value = null
	var delay_ticks := 0
	var unsupported: Array[String] = ["model-bone-world-transforms", "member-climb-locomotion"]
	if typeof(delay_value) in [TYPE_INT, TYPE_FLOAT] and float(delay_value) >= 0.0:
		delay_ticks = _ship_contract_delay_ticks(float(delay_value))
	elif fields.has("ActivationDelaySeconds"):
		unsupported.append("unresolved-expression:ActivationDelaySeconds")
	var generated := bool(_module_contract_value(fields, "GenerateNow", false))
	var triggered_by := String(_module_contract_value(fields, "TriggeredBy", ""))
	var custom_animation := fields.get("CustomAnimAndDuration", {}) as Dictionary
	if not custom_animation.is_empty(): unsupported.append("presentation-animation:" + String(custom_animation.get("animState", "")))
	row["dynamic_portal"] = {
		"active": generated and triggered_by == "" and not unsupported.has("unresolved-expression:ActivationDelaySeconds") and delay_ticks == 0,
		"activation_ready_tick": sim.tick_index + delay_ticks if generated and triggered_by == "" and delay_ticks > 0 else -1,
		"activation_delay_ticks": delay_ticks,
		"generate_now": generated,
		"triggered_by": triggered_by,
		"conflicts_with": Array(_module_contract_value(fields, "ConflictsWith", [])).duplicate(),
		"allow_enemies": bool(_module_contract_value(fields, "AllowEnemies", false)),
		"object_filter": (filter_value as Array).duplicate(),
		"bone_prefix": String(prefix_value),
		"number_of_bones": int(bone_count_value),
		"waypoints": waypoints,
		"links": links,
		"top_attack_position_source": _module_contract_value(fields, "TopAttackPos", {}),
		"top_attack_radius_source": _module_contract_value(fields, "TopAttackRadius", null),
		"custom_animation": custom_animation.duplicate(true),
		"unsupported_semantics": unsupported,
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}


func _step_dynamic_portals() -> void:
	for structure_id in sim.structure_ids():
		var row := sim.structures[structure_id] as Dictionary
		var portal := row.get("dynamic_portal", {}) as Dictionary
		if portal.is_empty(): continue
		var conflicts := false
		for upgrade_value in portal.get("conflicts_with", []) as Array:
			if sim._structure_has_completed_upgrade(row, String(upgrade_value)):
				conflicts = true
				break
		if conflicts:
			portal["active"] = false
			portal["activation_ready_tick"] = -1
			row["dynamic_portal"] = portal
			continue
		var trigger := String(portal.get("triggered_by", ""))
		var eligible = bool(portal.get("generate_now", false)) if trigger == "" else sim._structure_has_completed_upgrade(row, trigger)
		if not eligible or (portal.get("unsupported_semantics", []) as Array).has("unresolved-expression:ActivationDelaySeconds"):
			portal["active"] = false
			portal["activation_ready_tick"] = -1
		elif not bool(portal.get("active", false)):
			if int(portal.get("activation_ready_tick", -1)) < 0:
				portal["activation_ready_tick"] = sim.tick_index + int(portal.get("activation_delay_ticks", 0))
			if sim.tick_index >= int(portal.get("activation_ready_tick", -1)):
				portal["active"] = true
		row["dynamic_portal"] = portal


func request_dynamic_portal_route(portal_id: int, entity_id: int, from_index: int, to_index: int) -> Dictionary:
	if not sim.structures.has(portal_id) or not sim.entities.has(entity_id): return {"ok": false, "reason": "portal-or-entity-missing"}
	var portal_row := sim.structures[portal_id] as Dictionary
	if not portal_row.has("dynamic_portal"): _attach_structure_module_contracts(portal_row)
	_step_dynamic_portals()
	var portal := portal_row.get("dynamic_portal", {}) as Dictionary
	if portal.is_empty(): return {"ok": false, "reason": "typed-dynamic-portal-contract-missing"}
	if not bool(portal.get("active", false)): return {"ok": false, "reason": "portal-inactive"}
	var entity := sim.entities[entity_id] as Dictionary
	if not bool(portal.get("allow_enemies", false)) and sim._is_hostile(int(portal_row.get("team", -1)), int(entity.get("team", -2))): return {"ok": false, "reason": "enemy-refused"}
	if not sim._transport_filter_accepts(entity, portal.get("object_filter", []) as Array): return {"ok": false, "reason": "object-filter-refused"}
	var selected: Dictionary = {}
	for link_value in portal.get("links", []) as Array:
		var link := link_value as Dictionary
		if int(link.get("from", -1)) == from_index and int(link.get("to", -1)) == to_index:
			selected = link
			break
	if selected.is_empty(): return {"ok": false, "reason": "portal-link-missing"}
	var route_indices: Array = [from_index]
	route_indices.append_array(selected.get("via", []) as Array)
	route_indices.append(to_index)
	var route: Array[Dictionary] = []
	var waypoints := portal.get("waypoints", []) as Array
	for route_index_value in route_indices:
		var ordinal := int(route_index_value)
		var waypoint := waypoints[ordinal] as Dictionary
		route.append({"ordinal": ordinal, "bone": "%s%d" % [String(portal.get("bone_prefix", "")), int(waypoint.get("index", 0)) + 1], "type": String(waypoint.get("type", ""))})
	entity["dynamic_portal_route_receipt"] = {"portal_id": portal_id, "route": route.duplicate(true), "status": "resolved-awaiting-model-bone-world-transforms"}
	sim._emit_event("portal.route_resolved", portal_id, entity_id, {"route": route})
	return {"ok": true, "reason": "", "route": route, "movement_status": "deferred-model-bone-world-transforms"}


func _attach_foundation_ai_contract(row: Dictionary, contract: Dictionary) -> void:
	## FoundationAIUpdate is an authored foundation selector. The typed contract
	## contains no AI placement heuristic, so an empty marker stays explicit and
	## never guesses the engine's default variation.
	if String(contract.get("extraction", "")) != "typed" or row.has("foundation_ai_update"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var policy := {
		"build_variation": 0,
		"has_authored_build_variation": false,
		"unsupported_semantics": [],
		"source_ini": String(contract.get("sourceIni", "")),
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}
	if fields.has("BuildVariation"):
		var field_value: Variant = (fields.get("BuildVariation", {}) as Dictionary).get("value")
		if typeof(field_value) != TYPE_INT or int(field_value) < 1:
			return
		policy["build_variation"] = int(field_value)
		policy["has_authored_build_variation"] = true
		row["build_variation"] = int(field_value)
		(policy["unsupported_semantics"] as Array).append("foundation-construction-dispatch-unwired")
	else:
		(policy["unsupported_semantics"] as Array).append("engine-default-build-variation-unresolved")
	row["foundation_ai_update"] = policy


func foundation_build_variation(structure_id: int) -> Dictionary:
	if not sim.structures.has(structure_id):
		return {"ok": false, "reason": "structure-missing"}
	var row := sim.structures[structure_id] as Dictionary
	if not row.has("foundation_ai_update"):
		_attach_structure_module_contracts(row)
	var policy := row.get("foundation_ai_update", {}) as Dictionary
	if policy.is_empty():
		return {"ok": false, "reason": "typed-foundation-ai-contract-missing"}
	if not bool(policy.get("has_authored_build_variation", false)):
		return {"ok": false, "reason": "engine-default-build-variation-unresolved"}
	return {"ok": true, "reason": "", "value": int(policy.get("build_variation", 0))}


func _attach_dual_weapon_contract(row: Dictionary, contract: Dictionary) -> void:
	## DualWeaponBehavior authors only the distance boundary. Weapon identities
	## come from the object's already-compiled WeaponSet profiles; absence of a
	## close profile is therefore a hard refusal, never a synthesized weapon.
	if String(contract.get("extraction", "")) != "typed" or row.has("dual_weapon_behavior"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	if fields.size() != 1 or not fields.has("SwitchWeaponOnCloseRangeDistance"):
		return
	var distance_field: Variant = fields.get("SwitchWeaponOnCloseRangeDistance")
	if typeof(distance_field) != TYPE_DICTIONARY:
		return
	var distance := distance_field as Dictionary
	var expression := String(distance.get("expression", "")).strip_edges()
	if expression == "":
		return
	var policy := {
		"switch_distance_source": 0.0,
		"switch_distance": 0.0,
		"close_weapon_mode": String(row.get("close_weapon_mode", "")),
		"executable": false,
		"unsupported_semantics": [],
		"source_ini": String(contract.get("sourceIni", "")),
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
		"field_source_ini": String(distance.get("sourceIni", "")),
		"field_line": int(distance.get("line", 0)),
	}
	if not distance.has("value"):
		(policy["unsupported_semantics"] as Array).append("unresolved-switch-distance-define:%s" % expression)
		# Disable an older independently projected threshold: this typed consumer
		# cannot prove the define's numeric value and must not execute it.
		row["close_weapon_switch_distance"] = 0.0
		row["close_weapon_switch_distance_source"] = 0.0
		row["unsupported_close_weapon"] = false
		row["dual_weapon_behavior"] = policy
		return
	var numeric: Variant = distance.get("value")
	if typeof(numeric) not in [TYPE_INT, TYPE_FLOAT] or not is_finite(float(numeric)) or float(numeric) < 0.0:
		return
	var source_distance := float(numeric)
	var local_distance = sim._retail_source_to_sim_offset(Vector2(source_distance, 0.0)).x
	policy["switch_distance_source"] = source_distance
	policy["switch_distance"] = local_distance
	row["close_weapon_switch_distance_source"] = source_distance
	row["close_weapon_switch_distance"] = local_distance
	var close_mode := String(policy.get("close_weapon_mode", ""))
	if source_distance > 0.0 and (close_mode == "" or not (row.get("weapon_modes", {}) as Dictionary).has(close_mode)):
		(policy["unsupported_semantics"] as Array).append("close-weapon-profile-unresolved")
		row["unsupported_close_weapon"] = true
	else:
		policy["executable"] = true
		row["unsupported_close_weapon"] = false
	row["dual_weapon_behavior"] = policy


func _attach_refund_die_contract(row: Dictionary, contract: Dictionary) -> void:
	var executable := bool(contract.get("executable", false))
	if not executable:
		executable = String(contract.get("runtimeStatus", contract.get("runtime_status", ""))) == "executable"
	if not executable:
		return
	if String(contract.get("extraction", "")) != "typed":
		return
	_cache_refund_die_build_cost(row)
	var fields := contract.get("fields", {}) as Dictionary
	var allowed := {"RefundPercent": true, "UpgradeRequired": true, "BuildingRequired": true}
	for key_value in fields.keys():
		if not allowed.has(String(key_value)): return
	if not fields.has("RefundPercent"):
		return
	var refund_field: Variant = fields.get("RefundPercent")
	if typeof(refund_field) != TYPE_DICTIONARY:
		return
	var refund := refund_field as Dictionary
	var percent_value: Variant = refund.get("percent")
	var fraction_value: Variant = refund.get("fraction")
	if typeof(percent_value) not in [TYPE_INT, TYPE_FLOAT] or typeof(fraction_value) not in [TYPE_INT, TYPE_FLOAT]:
		return
	var percent := float(percent_value)
	var fraction := float(fraction_value)
	if not is_finite(percent) or not is_finite(fraction) or percent < 0.0 or percent > 100.0 or not is_equal_approx(fraction, percent / 100.0):
		return
	var upgrade_required := ""
	if fields.has("UpgradeRequired"):
		var upgrade_field: Variant = fields.get("UpgradeRequired")
		if typeof(upgrade_field) != TYPE_DICTIONARY: return
		upgrade_required = String((upgrade_field as Dictionary).get("value", "")).strip_edges()
		if upgrade_required == "": return
	var building_required: Array[String] = []
	if fields.has("BuildingRequired"):
		var building_field: Variant = fields.get("BuildingRequired")
		if typeof(building_field) != TYPE_DICTIONARY or typeof((building_field as Dictionary).get("value")) != TYPE_ARRAY: return
		for token_value in (building_field as Dictionary).get("value", []) as Array:
			var token := String(token_value).strip_edges()
			if token == "": return
			building_required.append(token)
		if building_required.is_empty(): return
	var policies := row.get("refund_die", []) as Array
	policies.append({
		"fraction": fraction,
		"percent": percent,
		"upgrade_required": upgrade_required,
		"building_required": building_required,
		"death_scope": "object-death-edge",
		"unsupported_semantics": [],
		"source_ini": String(contract.get("sourceIni", "")),
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	})
	row["refund_die"] = policies


func _cache_refund_die_build_cost(row: Dictionary) -> void:
	## Object::getBuildCost is cached per object in retail. Resolve it once when
	## the executable module attaches; later rule/owner changes cannot rewrite it.
	if row.has("cached_build_cost"):
		return
	var direct: Variant = row.get("build_cost")
	if typeof(direct) in [TYPE_INT, TYPE_FLOAT] and float(direct) >= 0.0:
		row["cached_build_cost"] = float(direct)
		return
	var structure_kind := String(row.get("structure_kind", ""))
	if structure_kind != "":
		var team := int(row.get("team", -1))
		var build_rules = sim.structure_build_rules_for_team(team)
		var structure_rule := build_rules.get(structure_kind, {}) as Dictionary
		if structure_rule.is_empty():
			structure_rule = sim._expansion_build_rules.get(structure_kind, {}) as Dictionary
		var structure_cost: Variant = structure_rule.get("cost")
		if typeof(structure_cost) in [TYPE_INT, TYPE_FLOAT] and float(structure_cost) >= 0.0:
			row["cached_build_cost"] = float(structure_cost)
		return
	var unit_type := String(row.get("unit_type", ""))
	var production_rule := sim._unit_production_rules.get(unit_type, {}) as Dictionary
	if not production_rule.is_empty():
		var unit_cost = sim._production_rule_value(unit_type, "cost_rule", "default_cost")
		if unit_cost >= 0:
			row["cached_build_cost"] = unit_cost


func _attach_wall_hub_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed": return
	var fields := contract.get("fields", {}) as Dictionary
	for required in ["Options", "MaxBuildoutDistance", "EffectiveMaxBuildoutDistance", "SegmentTemplateName", "HubCapTemplateName", "DefaultSegmentTemplateName"]:
		if not fields.has(required): return
	var option := String(_module_contract_value(fields, "Options", ""))
	if option not in ["OPTION_ONE", "OPTION_TWO", "OPTION_THREE"]: return
	var distance_value: Variant = fields.get("MaxBuildoutDistance")
	var segment_value: Variant = fields.get("SegmentTemplateName")
	if typeof(distance_value) != TYPE_ARRAY or (distance_value as Array).is_empty() or typeof(segment_value) != TYPE_ARRAY or (segment_value as Array).is_empty(): return
	var defines := sim._rules.get("wall_hub_distance_defines", {}) as Dictionary
	var distances: Array[Dictionary] = []
	for value in distance_value as Array:
		if typeof(value) != TYPE_DICTIONARY: return
		var resolved := _wall_hub_distance(value as Dictionary, defines)
		if bool(resolved.get("malformed", false)): return
		distances.append(resolved)
	var effective_field: Variant = fields.get("EffectiveMaxBuildoutDistance")
	if typeof(effective_field) != TYPE_DICTIONARY: return
	var effective := _wall_hub_distance(effective_field as Dictionary, defines)
	if bool(effective.get("malformed", false)): return
	var segments: Array[String] = []
	for value in segment_value as Array:
		if typeof(value) != TYPE_DICTIONARY: return
		var template := String((value as Dictionary).get("value", "")).strip_edges()
		if template == "": return
		segments.append(template)
	var builder_source := float(_module_contract_value(fields, "BuilderRadius", 0.0))
	if not is_finite(builder_source) or builder_source < 0.0: return
	var unsupported: Array[String] = ["segment-spacing-and-build-economy-unresolved"]
	if fields.has("StaggeredBuildFactor"): unsupported.append("staggered-build-factor-engine-define:%s" % String(_module_contract_value(fields, "StaggeredBuildFactor", "")))
	if not bool(effective.get("resolved", false)): unsupported.append("unresolved-max-buildout-distance:%s" % String(effective.get("define", "")))
	var policies := row.get("wall_hub_behaviors", []) as Array
	policies.append({"option":option,"max_buildout_distances":distances,"effective_distance_source":float(effective.get("source",0.0)),"effective_distance":float(effective.get("local",0.0)),"executable":bool(effective.get("resolved",false)),"runtime_scope":"plan-only","segment_templates":segments,"hub_cap_template":String(_module_contract_value(fields,"HubCapTemplateName","")),"default_segment_template":String(_module_contract_value(fields,"DefaultSegmentTemplateName","")),"cliff_cap_template":String(_module_contract_value(fields,"CliffCapTemplateName","")),"builder_radius_source":builder_source,"builder_radius":sim._retail_source_to_sim_offset(Vector2(builder_source,0.0)).x,"unsupported_semantics":unsupported,"source_ini":String(contract.get("sourceIni","")),"tag":String(contract.get("tag","")),"line":int(contract.get("line",0))})
	row["wall_hub_behaviors"] = policies


func _wall_hub_distance(field: Dictionary, defines: Dictionary) -> Dictionary:
	var output := {"source_ini":String(field.get("sourceIni","")),"line":int(field.get("line",0)),"resolved":false,"source":0.0,"local":0.0,"define":"","malformed":false}
	var numeric: Variant = field.get("value")
	if typeof(numeric) in [TYPE_INT, TYPE_FLOAT]:
		if not is_finite(float(numeric)) or float(numeric) < 0.0: output["malformed"] = true; return output
		output["resolved"] = true; output["source"] = float(numeric); output["local"] = sim._retail_source_to_sim_offset(Vector2(float(numeric),0.0)).x; return output
	var define := String(field.get("define", field.get("expression", ""))).strip_edges(); output["define"] = define
	if define == "": output["malformed"] = true; return output
	var define_value: Variant = defines.get(define)
	if typeof(define_value) in [TYPE_INT, TYPE_FLOAT] and is_finite(float(define_value)) and float(define_value) >= 0.0:
		output["resolved"] = true; output["source"] = float(define_value); output["local"] = sim._retail_source_to_sim_offset(Vector2(float(define_value),0.0)).x
	return output


func request_wall_hub_plan(structure_id: int, option: String, endpoint: Vector2) -> Dictionary:
	if not sim.structures.has(structure_id): return {"ok":false,"reason":"structure-missing"}
	var row := sim.structures[structure_id] as Dictionary
	if not row.has("wall_hub_behaviors"): _attach_structure_module_contracts(row)
	if int(row.get("health",0)) <= 0: return {"ok":false,"reason":"wall-hub-destroyed"}
	var selected: Dictionary = {}
	for value in row.get("wall_hub_behaviors", []) as Array:
		if String((value as Dictionary).get("option","")) == option: selected = value as Dictionary; break
	if selected.is_empty(): return {"ok":false,"reason":"wall-hub-option-missing"}
	if not bool(selected.get("executable",false)): return {"ok":false,"reason":"max-buildout-distance-unresolved"}
	var origin := Vector2(row.get("position",Vector2.ZERO)); var distance := origin.distance_to(endpoint)
	if distance > float(selected.get("effective_distance",0.0)) + 0.000001: return {"ok":false,"reason":"max-buildout-distance-exceeded","maximum":selected.get("effective_distance")}
	var plan := {"ok":true,"reason":"","option":option,"origin":origin,"endpoint":endpoint,"distance":distance,"maximum_distance":float(selected.get("effective_distance",0.0)),"segment_templates":(selected.get("segment_templates",[]) as Array).duplicate(),"hub_cap_template":String(selected.get("hub_cap_template","")),"default_segment_template":String(selected.get("default_segment_template","")),"cliff_cap_template":String(selected.get("cliff_cap_template","")),"materialization_status":"deferred-segment-spacing-and-build-economy"}
	var receipts := row.get("wall_hub_plan_receipts", []) as Array; receipts.append(plan.duplicate(true)); row["wall_hub_plan_receipts"] = receipts
	sim._emit_event("wall_hub.plan_resolved",structure_id,0,{"option":option,"distance":distance,"segment_templates":plan["segment_templates"]})
	return plan


func _attach_attach_update_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("attach_update"): return
	var fields := contract.get("fields", {}) as Dictionary
	var filter_value: Variant = _module_contract_value(fields, "ObjectFilter", null)
	if typeof(filter_value) != TYPE_ARRAY or (filter_value as Array).is_empty(): return
	var object_filter: Array[String] = []
	for value in filter_value as Array:
		var token := String(value).strip_edges(); if token == "": return
		object_filter.append(token)
	var scan_authored := fields.has("ScanRange")
	var scan_source := float(_module_contract_value(fields, "ScanRange", 0.0))
	if not is_finite(scan_source) or scan_source < 0.0: return
	var parent_status: Array[String] = []
	for value in _typed_contract_tokens(fields, "ParentStatus"): parent_status.append(String(value))
	var unsupported: Array[String] = []
	if bool(_module_contract_value(fields,"AnchorToTopOfGeometry",false)): unsupported.append("model-geometry-top-transform-unresolved")
	for key in ["ParentOwnerAttachmentEvaEvent","ParentEnemyAttachmentEvaEvent","ParentOwnerDiedEvaEvent"]:
		if fields.has(key): unsupported.append("presentation-eva-event:%s" % key)
	row["attach_update"] = {"object_filter":object_filter,"scan_range_authored":scan_authored,"scan_range_source":scan_source,"scan_range":sim._retail_source_to_sim_offset(Vector2(scan_source,0.0)).x,"parent_status":parent_status,"always_teleport":bool(_module_contract_value(fields,"AlwaysTeleport",false)),"anchor_to_top":bool(_module_contract_value(fields,"AnchorToTopOfGeometry",false)),"owner_attach_eva":String(_module_contract_value(fields,"ParentOwnerAttachmentEvaEvent","")),"enemy_attach_eva":String(_module_contract_value(fields,"ParentEnemyAttachmentEvaEvent","")),"parent_died_eva":String(_module_contract_value(fields,"ParentOwnerDiedEvaEvent","")),"unsupported_semantics":unsupported,"source_ini":String(contract.get("sourceIni","")),"tag":String(contract.get("tag","")),"line":int(contract.get("line",0))}
	row["attach_parent_id"] = 0; row["attach_parent_kind"] = ""


func request_attach_update(child_id: int, parent_id: int, parent_kind: String = "entity") -> Dictionary:
	if not sim.entities.has(child_id): return {"ok":false,"reason":"attach-child-missing"}
	var child := sim.entities[child_id] as Dictionary
	if not child.has("attach_update"): _attach_module_contracts(child)
	var policy := child.get("attach_update", {}) as Dictionary
	if policy.is_empty(): return {"ok":false,"reason":"typed-attach-update-missing"}
	var table = sim.structures if parent_kind == "structure" else sim.entities
	if not table.has(parent_id) or (parent_kind == "entity" and parent_id == child_id): return {"ok":false,"reason":"attach-target-missing"}
	var parent := table[parent_id] as Dictionary
	if int(parent.get("health",0)) <= 0: return {"ok":false,"reason":"attach-target-dead"}
	if not sim._transport_filter_accepts(_attach_filter_probe(parent,parent_kind), policy.get("object_filter",[]) as Array): return {"ok":false,"reason":"attach-target-filter-refused"}
	if bool(policy.get("scan_range_authored",false)):
		var distance := Vector2(child.get("position",Vector2.ZERO)).distance_to(Vector2(parent.get("position",Vector2.ZERO)))
		if distance > float(policy.get("scan_range",0.0)) + 0.000001: return {"ok":false,"reason":"attach-target-out-of-range"}
	_detach_attach_update(child,"replaced")
	child["attach_parent_id"] = parent_id; child["attach_parent_kind"] = parent_kind
	_set_attach_parent_status(parent,child_id,policy.get("parent_status",[]) as Array,true)
	if bool(policy.get("always_teleport",false)) and not bool(policy.get("anchor_to_top",false)): child["position"] = Vector2(parent.get("position",Vector2.ZERO))
	var eva := String(policy.get("owner_attach_eva","")) if int(child.get("team",-1)) == int(parent.get("team",-2)) else String(policy.get("enemy_attach_eva",""))
	sim._emit_event("module.attach_update_attached",child_id,parent_id,{"parent_kind":parent_kind,"eva_event_id":eva,"presentation_status":"receipt-only"})
	return {"ok":true,"reason":"","parent_id":parent_id,"parent_kind":parent_kind}


func _step_attach_updates() -> void:
	for child_id in sim.entity_ids():
		var child := sim.entities[child_id] as Dictionary; var policy := child.get("attach_update", {}) as Dictionary
		if policy.is_empty(): continue
		if int(child.get("health",0)) <= 0:
			_detach_attach_update(child,"child-died")
			continue
		var parent_id := int(child.get("attach_parent_id",0)); var parent_kind := String(child.get("attach_parent_kind",""))
		if parent_id != 0:
			var table = sim.structures if parent_kind == "structure" else sim.entities
			if not table.has(parent_id) or int((table[parent_id] as Dictionary).get("health",0)) <= 0:
				_detach_attach_update(child,"parent-died"); continue
			var parent := table[parent_id] as Dictionary
			if bool(policy.get("always_teleport",false)) and not bool(policy.get("anchor_to_top",false)): child["position"] = Vector2(parent.get("position",Vector2.ZERO))
			continue
		if not bool(policy.get("scan_range_authored",false)): continue
		var candidates: Array[Dictionary] = []; var origin := Vector2(child.get("position",Vector2.ZERO)); var radius = float(policy.get("scan_range",0.0))
		for target_id in sim.entity_ids():
			if target_id == child_id: continue
			var target := sim.entities[target_id] as Dictionary; var distance := origin.distance_to(Vector2(target.get("position",Vector2.ZERO)))
			if int(target.get("health",0)) > 0 and distance <= radius and sim._transport_filter_accepts(_attach_filter_probe(target,"entity"),policy.get("object_filter",[]) as Array): candidates.append({"id":target_id,"kind":"entity","distance":distance})
		for target_id in sim.structure_ids():
			var target := sim.structures[target_id] as Dictionary; var distance := origin.distance_to(Vector2(target.get("position",Vector2.ZERO)))
			if int(target.get("health",0)) > 0 and distance <= radius and sim._transport_filter_accepts(_attach_filter_probe(target,"structure"),policy.get("object_filter",[]) as Array): candidates.append({"id":target_id,"kind":"structure","distance":distance})
		candidates.sort_custom(func(a:Dictionary,b:Dictionary)->bool:
			if not is_equal_approx(float(a["distance"]),float(b["distance"])): return float(a["distance"]) < float(b["distance"])
			if String(a["kind"]) != String(b["kind"]): return String(a["kind"]) < String(b["kind"])
			return int(a["id"]) < int(b["id"]))
		if not candidates.is_empty(): request_attach_update(child_id,int(candidates[0]["id"]),String(candidates[0]["kind"]))


func _detach_attach_update(child: Dictionary, reason: String) -> void:
	var parent_id := int(child.get("attach_parent_id",0)); if parent_id == 0: return
	var parent_kind := String(child.get("attach_parent_kind","entity")); var table = sim.structures if parent_kind == "structure" else sim.entities; var policy := child.get("attach_update",{}) as Dictionary
	if table.has(parent_id): _set_attach_parent_status(table[parent_id] as Dictionary,int(child.get("id",0)),policy.get("parent_status",[]) as Array,false)
	child["attach_parent_id"] = 0; child["attach_parent_kind"] = ""
	if reason == "parent-died": sim._emit_event("module.attach_update_parent_died",int(child.get("id",0)),parent_id,{"eva_event_id":String(policy.get("parent_died_eva","")),"presentation_status":"receipt-only"})


# De-staticed on extraction (instance sim access).
func _attach_filter_probe(row: Dictionary, kind: String) -> Dictionary:
	var kinds := (row.get("kind_of",[]) as Array).duplicate(); kinds.append("STRUCTURE" if kind == "structure" else "UNIT")
	for value in [row.get("source_object_id",""),row.get("object_id",""),row.get("structure_kind","")]: if String(value) != "": kinds.append(String(value))
	return {"category":String(row.get("category","structure" if kind == "structure" else "")),"kind_of":kinds}


# De-staticed on extraction (instance sim access).
func _set_attach_parent_status(parent: Dictionary, child_id: int, statuses: Array, enabled: bool) -> void:
	var sources = parent.get("attach_status_sources",{}) as Dictionary
	for value in statuses:
		var status := String(value).trim_prefix("+"); if status == "" or status.begins_with("-") or status in ["ANY","NONE"]: continue
		var ids := sources.get(status,[]) as Array
		if enabled and not ids.has(child_id): ids.append(child_id)
		elif not enabled: ids.erase(child_id)
		if ids.is_empty(): sources.erase(status); _set_row_object_status(parent,status,false)
		else: sources[status] = ids; _set_row_object_status(parent,status,true)
	if sources.is_empty(): parent.erase("attach_status_sources")
	else: parent["attach_status_sources"] = sources


func _attach_monitor_condition_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("monitor_condition_update"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var model_route := _monitor_condition_route(fields.get("ModelConditionRoute"))
	var weapon_route := _monitor_condition_route(fields.get("WeaponSetRoute"))
	if model_route.is_empty() and weapon_route.is_empty():
		return
	# A present but malformed pair fails the whole typed row closed. Importer
	# validation normally prevents this; the runtime keeps the same boundary.
	if (fields.has("ModelConditionRoute") and model_route.is_empty()) or (fields.has("WeaponSetRoute") and weapon_route.is_empty()):
		return
	var default_set := String(row.get("command_set_id", row.get("default_command_set_id", ""))).strip_edges()
	var unsupported: Array[String] = ["command-surface-consumer-unwired"]
	if not model_route.is_empty(): unsupported.append("model-condition-producer-unwired")
	if default_set == "": unsupported.append("default-command-set-unresolved")
	row["monitor_condition_update"] = {
		"default_command_set": default_set,
		"active_command_set": default_set,
		"active_route": "default" if default_set != "" else "unresolved",
		"model_condition_route": model_route,
		"weapon_set_route": weapon_route,
		"transition_count": 0,
		"unsupported_semantics": unsupported,
		"source_ini": String(contract.get("sourceIni", "")),
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}


# De-staticed on extraction (instance sim access).
func _monitor_condition_route(value: Variant) -> Dictionary:
	if typeof(value) != TYPE_DICTIONARY:
		return {}
	var route := value as Dictionary
	var flags_field: Variant = route.get("flags")
	var command_field: Variant = route.get("commandSet")
	if typeof(flags_field) != TYPE_DICTIONARY or typeof(command_field) != TYPE_DICTIONARY:
		return {}
	var flags_value: Variant = (flags_field as Dictionary).get("value")
	var command_set := String((command_field as Dictionary).get("value", "")).strip_edges()
	if typeof(flags_value) != TYPE_ARRAY or (flags_value as Array).is_empty() or command_set == "":
		return {}
	var flags: Array[String] = []
	for flag_value in flags_value as Array:
		if typeof(flag_value) not in [TYPE_STRING, TYPE_STRING_NAME] or String(flag_value).strip_edges() == "":
			return {}
		flags.append(String(flag_value).to_upper())
	return {"flags": flags, "command_set": command_set}


func _step_monitor_condition_updates() -> void:
	for entity_id in sim.entity_ids():
		_step_monitor_condition_row(entity_id, sim.entities[entity_id] as Dictionary)
	for structure_id in sim.structure_ids():
		_step_monitor_condition_row(structure_id, sim.structures[structure_id] as Dictionary)


func _step_monitor_condition_row(object_id: int, row: Dictionary) -> void:
	var policy := row.get("monitor_condition_update", {}) as Dictionary
	if policy.is_empty() or (policy.get("unsupported_semantics", []) as Array).has("default-command-set-unresolved"):
		return
	var model_conditions := _upper_token_set(row.get("model_conditions", []))
	var weapon_flags := _upper_token_set(row.get("weapon_set_flags", []))
	var selected_set := String(policy.get("default_command_set", ""))
	var selected_route := "default"
	var model_route := policy.get("model_condition_route", {}) as Dictionary
	var weapon_route := policy.get("weapon_set_route", {}) as Dictionary
	# Retail rows such as Mountain Giant author both: ATTACKING_POSITION is the
	# transient stop-command surface and must override the broader weapon set.
	if not model_route.is_empty() and _monitor_flags_match(model_conditions, model_route.get("flags", []) as Array):
		selected_set = String(model_route.get("command_set", "")); selected_route = "model-condition"
	elif not weapon_route.is_empty() and _monitor_flags_match(weapon_flags, weapon_route.get("flags", []) as Array):
		selected_set = String(weapon_route.get("command_set", "")); selected_route = "weapon-set"
	if selected_set == "" or (selected_set == String(policy.get("active_command_set", "")) and selected_route == String(policy.get("active_route", ""))):
		return
	var prior := String(policy.get("active_command_set", ""))
	row["command_set_id"] = selected_set
	policy["active_command_set"] = selected_set
	policy["active_route"] = selected_route
	policy["transition_count"] = int(policy.get("transition_count", 0)) + 1
	row["monitor_condition_update"] = policy
	sim._emit_event("module.monitor_condition_command_set", object_id, 0, {"from": prior, "to": selected_set, "route": selected_route})


# De-staticed on extraction (instance sim access).
func _upper_token_set(value: Variant) -> Dictionary:
	var output := {}
	if typeof(value) == TYPE_ARRAY:
		for token in value as Array: output[String(token).to_upper()] = true
	return output


# De-staticed on extraction (instance sim access).
func _monitor_flags_match(active: Dictionary, required: Array) -> bool:
	if required.is_empty(): return false
	for flag_value in required:
		if not active.has(String(flag_value).to_upper()): return false
	return true


func set_entity_upgrade_state(entity_id: int, upgrade_id: String, installed: bool) -> Dictionary:
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity-missing"}
	if upgrade_id.strip_edges() == "":
		return {"ok": false, "reason": "upgrade-id-missing"}
	var row := sim.entities[entity_id] as Dictionary
	var applied := row.get("applied_upgrades", {}) as Dictionary
	var matched_key := ""
	for key_value in applied.keys():
		if String(key_value).to_upper() == upgrade_id.to_upper():
			matched_key = String(key_value)
			break
	if installed:
		if matched_key == "":
			applied[upgrade_id] = sim.tick_index
	else:
		if matched_key != "":
			applied.erase(matched_key)
	row["applied_upgrades"] = applied
	_reconcile_attribute_modifier_upgrades(row)
	_reconcile_geometry_upgrades(row)
	sim._emit_event("upgrade.entity_state", 0, entity_id, {"upgrade_id": upgrade_id, "installed": installed})
	return {"ok": true, "reason": "", "installed": installed}


func set_team_upgrade_state(team: int, upgrade_id: String, installed: bool) -> Dictionary:
	if team < 0 or upgrade_id.strip_edges() == "":
		return {"ok": false, "reason": "invalid-team-or-upgrade"}
	var owned := sim.team_upgrades.get(team, {}) as Dictionary
	var matched_key := ""
	for key_value in owned.keys():
		if String(key_value).to_upper() == upgrade_id.to_upper():
			matched_key = String(key_value)
			break
	if installed:
		if matched_key == "":
			owned[upgrade_id] = true
	else:
		if matched_key != "":
			owned.erase(matched_key)
	sim.team_upgrades[team] = owned
	sim._refresh_team_command_set_upgrades(team)
	for entity_id in sim.entity_ids():
		var row := sim.entities[entity_id] as Dictionary
		if int(row.get("team", -1)) == team:
			_reconcile_attribute_modifier_upgrades(row)
			_reconcile_geometry_upgrades(row)
	for structure_id in sim.structure_ids():
		var row := sim.structures[structure_id] as Dictionary
		if int(row.get("team", -1)) == team:
			_reconcile_attribute_modifier_upgrades(row)
			_reconcile_geometry_upgrades(row)
	sim._emit_event("upgrade.team_state", 0, 0, {"team": team, "upgrade_id": upgrade_id, "installed": installed})
	return {"ok": true, "reason": "", "installed": installed}


func _step_object_creation_upgrades()->void:
	var owners: Array = []
	for entity_id in sim.entity_ids():
		owners.append({"id": entity_id, "kind": "entity"})
	for structure_id in sim.structure_ids():
		owners.append({"id": structure_id, "kind": "structure"})
	for owner_ref_value in owners:
		var owner_ref := owner_ref_value as Dictionary
		var table = sim.structures if String(owner_ref.get("kind")) == "structure" else sim.entities
		var owner_id := int(owner_ref.get("id"))
		if not table.has(owner_id):
			continue
		var owner := table[owner_id] as Dictionary
		var rows := owner.get("object_creation_upgrades", []) as Array
		for policy_value in rows:
			var policy := policy_value as Dictionary
			if bool(policy.get("consumed", false)) or int(policy.get("delay_ticks", -1)) < 0:
				continue
			var upgrades := owner.get("completed_upgrades", []) as Array
			var trigger_count := 0
			for trigger in policy.get("triggers", []) as Array:
				if upgrades.has(trigger):
					trigger_count += 1
			var triggered := trigger_count == (policy.get("triggers", []) as Array).size() if bool(policy.get("requires_all", false)) else trigger_count > 0
			for conflict in policy.get("conflicts", []) as Array:
				if upgrades.has(conflict):
					triggered = false
					break
			if not triggered:
				policy["scheduled_tick"] = -1
				continue
			if int(policy.get("scheduled_tick", -1)) < 0:
				policy["scheduled_tick"] = sim.tick_index + int(policy.get("delay_ticks", 0))
			if sim.tick_index < int(policy.get("scheduled_tick")):
				continue
			_consume_object_creation_upgrade(owner_id, owner, policy)
			policy["consumed"] = true
		if rows.is_empty():
			owner.erase("object_creation_upgrades")
		else:
			owner["object_creation_upgrades"] = rows


func _consume_object_creation_upgrade(owner_id:int,owner:Dictionary,policy:Dictionary)->void:
	var thing:=String(policy.get("thing_to_spawn",""))
	if thing!="":
		var at =Vector2(owner.get("position",Vector2.ZERO))+sim._retail_source_to_sim_offset(Vector2(policy.get("offset_source",Vector2.ZERO)));var result =sim.script_spawn_entity(thing,int(owner.get("team",-1)),at)
		if bool(result.get("ok",false)):var ids:=policy.get("spawned_ids",[]) as Array;ids.append(int(result.get("entity_id",0)));policy["spawned_ids"]=ids
		else:var receipts:=policy.get("unsupported_semantics",[]) as Array;receipts.append("unresolved_creation_object:%s"%thing);policy["unsupported_semantics"]=receipts
	var upgrades:=owner.get("completed_upgrades",[]) as Array;var grant:=String(policy.get("grant_upgrade",""));var remove:=String(policy.get("remove_upgrade",""));if grant!="" and not upgrades.has(grant):upgrades.append(grant);if remove!="":upgrades.erase(remove);owner["completed_upgrades"]=upgrades
	var upgrade_object:=String(policy.get("upgrade_object",""))
	if upgrade_object!="":
		owner["object_id"]=upgrade_object
		owner["unit_type"]=upgrade_object
		var receipts:=policy.get("unsupported_semantics",[]) as Array
		receipts.append("unsupported_creation_semantic:UpgradeObjectTemplateRebuild")
		policy["unsupported_semantics"]=receipts
	sim._emit_event("object_creation_upgrade.consumed",owner_id,0,{"thing":thing,"grant_upgrade":grant,"remove_upgrade":remove,"upgrade_object":upgrade_object})


func _attach_replace_self_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields := contract.get("fields", {}) as Dictionary
	var replace_with := String(_module_contract_value(fields, "ReplaceWith", "")).strip_edges()
	var triggered_by := String(_module_contract_value(fields, "TriggeredBy", "")).strip_edges()
	var conflicts: Array[String] = []
	var conflict_value: Variant = _module_contract_value(fields, "ConflictsWith", [])
	if typeof(conflict_value) != TYPE_ARRAY:
		return
	for value in conflict_value as Array:
		var conflict := String(value).strip_edges()
		if conflict == "":
			return
		conflicts.append(conflict)
	if replace_with == "" or triggered_by == "" or conflicts.is_empty():
		return
	var additions: Array[String] = []
	var additions_value: Variant = fields.get("AndThenAddA", [])
	if typeof(additions_value) != TYPE_ARRAY:
		return
	for addition_value in additions_value as Array:
		if typeof(addition_value) != TYPE_DICTIONARY:
			return
		var addition := String((addition_value as Dictionary).get("value", "")).strip_edges()
		if addition == "":
			return
		additions.append(addition)
	# The stable compiler accepts either no AndThenAddA rows or exactly two.
	# Repeat the guard at runtime so hand-authored/old packs cannot partly apply.
	if additions.size() not in [0, 2]:
		return
	var policies := row.get("replace_self_upgrades", []) as Array
	policies.append({
		"replace_with": replace_with,
		"triggered_by": triggered_by,
		"conflicts_with": conflicts,
		"and_then_add": additions,
		"consumed": false,
		"unsupported_semantics": [],
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	})
	row["replace_self_upgrades"] = policies


func apply_replace_self_upgrade(structure_id: int, trigger_upgrade_id: String) -> Dictionary:
	if not sim.structures.has(structure_id):
		return {"ok": false, "reason": "structure-missing"}
	var row := sim.structures[structure_id] as Dictionary
	if not row.has("replace_self_upgrades"):
		_attach_structure_module_contracts(row)
	var upgrades := row.get("completed_upgrades", []) as Array
	if not upgrades.has(trigger_upgrade_id):
		upgrades.append(trigger_upgrade_id)
		row["completed_upgrades"] = upgrades
	return _apply_due_replace_self_policy(structure_id, row, trigger_upgrade_id)


func _step_replace_self_upgrades() -> void:
	for structure_id in sim.structure_ids():
		if not sim.structures.has(structure_id):
			continue
		var row := sim.structures[structure_id] as Dictionary
		if not row.has("replace_self_upgrades") and not bool(row.get("structure_module_contracts_attached", false)):
			_attach_structure_module_contracts(row)
		var policies := row.get("replace_self_upgrades", []) as Array
		for policy_value in policies:
			var policy := policy_value as Dictionary
			var trigger := String(policy.get("triggered_by", ""))
			if bool(policy.get("consumed", false)) or not (row.get("completed_upgrades", []) as Array).has(trigger):
				continue
			_apply_due_replace_self_policy(structure_id, row, trigger)
			# One object can become only one mutually-exclusive replacement in a
			# tick. A conflict or unresolved target also fails closed here.
			break


func _apply_due_replace_self_policy(structure_id: int, row: Dictionary, trigger_upgrade_id: String) -> Dictionary:
	for policy_value in row.get("replace_self_upgrades", []) as Array:
		var policy := policy_value as Dictionary
		if bool(policy.get("consumed", false)) or String(policy.get("triggered_by", "")) != trigger_upgrade_id:
			continue
		var upgrades := row.get("completed_upgrades", []) as Array
		for conflict_value in policy.get("conflicts_with", []) as Array:
			if upgrades.has(String(conflict_value)):
				# ReplaceSelfUpgrade ConflictsWith (campsandcastles.ini:4420-4447):
				# the five wall routes are mutually exclusive. Named event so a
				# refused route is observable, never silent.
				sim._emit_event("structure.replace_self_refused", 0, structure_id, {
					"trigger": trigger_upgrade_id,
					"reason": "conflicting-upgrade",
					"conflict": String(conflict_value),
				})
				policy["consumed"] = true
				return {"ok": false, "reason": "conflicting-upgrade", "conflict": String(conflict_value)}
		var replacement_id := String(policy.get("replace_with", ""))
		var primary_spec := _replace_self_structure_spec(int(row.get("team", -1)), replacement_id)
		if primary_spec.is_empty():
			_replace_self_receipt(policy, "unresolved_replacement_template:%s" % replacement_id)
			return {"ok": false, "reason": "replacement-template-unresolved", "object_id": replacement_id}
		var addition_specs: Array[Dictionary] = []
		for addition_value in policy.get("and_then_add", []) as Array:
			var addition_id := String(addition_value)
			var addition_spec := _replace_self_structure_spec(int(row.get("team", -1)), addition_id)
			if addition_spec.is_empty():
				_replace_self_receipt(policy, "unresolved_addition_template:%s" % addition_id)
				return {"ok": false, "reason": "addition-template-unresolved", "object_id": addition_id}
			addition_specs.append(addition_spec)
		var old_identity := String(row.get("source_object_id", row.get("structure_kind", "")))
		var replacement := _replacement_structure_row(structure_id, row, replacement_id, primary_spec, true)
		sim._note_structure_table_mutation()
		sim._structure_footprint_radius_cache.erase(structure_id)
		sim.structures[structure_id] = replacement
		_attach_structure_module_contracts(replacement)
		sim._apply_structure_create_grants(replacement, true, true)
		sim._apply_structure_inherit_upgrades(replacement)
		sim._initialize_structure_auto_deposit(replacement)
		_reconcile_replacement_containment(structure_id, replacement)
		var created_ids: Array[int] = []
		for index in addition_specs.size():
			while sim.structures.has(sim._next_dynamic_structure_id):
				sim._next_dynamic_structure_id += 1
			var created_id = sim._next_dynamic_structure_id
			sim._next_dynamic_structure_id += 1
			var addition_id := String((policy.get("and_then_add", []) as Array)[index])
			var addition := _replacement_structure_row(created_id, row, addition_id, addition_specs[index], false)
			sim._note_structure_table_mutation()
			sim.structures[created_id] = addition
			_attach_structure_module_contracts(addition)
			sim._apply_structure_create_grants(addition, true, true)
			sim._apply_structure_inherit_upgrades(addition)
			sim._initialize_structure_auto_deposit(addition)
			created_ids.append(created_id)
		policy["consumed"] = true
		sim._emit_event("upgrade.replace_self", structure_id, 0, {"old_object_id": old_identity, "replacement_object_id": replacement_id, "trigger_upgrade_id": trigger_upgrade_id, "created_object_ids": created_ids.duplicate(), "created_object_templates": (policy.get("and_then_add", []) as Array).duplicate()})
		return {"ok": true, "reason": "", "structure_id": structure_id, "created_object_ids": created_ids}
	return {"ok": false, "reason": "trigger-not-authored"}


func _replace_self_structure_spec(team: int, object_id: String) -> Dictionary:
	var configured := sim._rules.get("replace_self_structure_templates", {}) as Dictionary
	if typeof(configured.get(object_id)) == TYPE_DICTIONARY:
		return (configured[object_id] as Dictionary).duplicate(true)
	var sources = sim.structure_source_object_ids_for_team(team)
	for kind_value in sources.keys():
		var ids: Variant = sources[kind_value]
		var matches := String(ids) == object_id if typeof(ids) in [TYPE_STRING, TYPE_STRING_NAME] else (ids as Array).has(object_id) if typeof(ids) == TYPE_ARRAY else false
		if not matches:
			continue
		var kind := String(kind_value)
		var healths = sim.structure_max_health_for_team(team)
		if not healths.has(kind):
			return {}
		return {"structure_kind": kind, "maximum_health": int(healths[kind])}
	# Replacement targets include castle-wall pieces that are present in the
	# selected playable-structure registry but are intentionally not base-loop
	# build kinds. Resolve their exact object document instead of pretending an
	# absent faction-manifest alias means an absent retail template.
	var document := _playable_structure_runtime_document(object_id)
	if not document.is_empty():
		var registration := document.get("registration", {}) as Dictionary
		var gameplay := registration.get("gameplay", {}) as Dictionary
		var health := gameplay.get("health", {}) as Dictionary
		var primary := health.get("primary", {}) as Dictionary
		var maximum_field := primary.get("maxHealth", {}) as Dictionary
		var maximum: Variant = maximum_field.get("value")
		if typeof(maximum) not in [TYPE_INT, TYPE_FLOAT]:
			var lifecycle := (registration.get("presentation", {}) as Dictionary).get("buildingLifecycle", {}) as Dictionary
			maximum = (lifecycle.get("simulationFacts", {}) as Dictionary).get("maximumHealth")
		if typeof(maximum) in [TYPE_INT, TYPE_FLOAT] and float(maximum) > 0.0:
			var spec := {"structure_kind": String(document.get("slug", object_id)), "maximum_health": int(maximum)}
			var attack := _structure_attack_from_combat(gameplay.get("combat", {}) as Dictionary)
			if not attack.is_empty():
				spec["attack"] = attack
			return spec
	return {}


func _playable_structure_runtime_document(object_id: String) -> Dictionary:
	var runtimes_value: Variant = sim._rules.get("playable_structure_runtimes", {})
	if typeof(runtimes_value) == TYPE_DICTIONARY:
		var runtimes := runtimes_value as Dictionary
		if typeof(runtimes.get(object_id)) == TYPE_DICTIONARY:
			return (runtimes[object_id] as Dictionary).duplicate(true)
		for key_value in runtimes.keys():
			var candidate: Variant = runtimes[key_value]
			if typeof(candidate) == TYPE_DICTIONARY and String((candidate as Dictionary).get("objectId", "")).nocasecmp_to(object_id) == 0:
				return (candidate as Dictionary).duplicate(true)
	var db = _content_db_ref()
	if db != null and db.has_method("get_playable_structure_runtime"):
		return db.get_playable_structure_runtime(object_id)
	return {}


func _structure_attack_from_combat(combat: Dictionary) -> Dictionary:
	if combat.is_empty():
		return {}
	for field in ["attackRange", "delayBetweenShotsMs", "preAttackDelayMs", "damage"]:
		if typeof(combat.get(field)) != TYPE_DICTIONARY:
			return {}
	var range_value := float((combat["attackRange"] as Dictionary).get("value", -1.0))
	var pre_attack_ms := float((combat["preAttackDelayMs"] as Dictionary).get("value", -1.0))
	var damage := float((combat["damage"] as Dictionary).get("value", 0.0))
	var delay := combat["delayBetweenShotsMs"] as Dictionary
	var delay_ms := float(delay.get("value", -1.0))
	var minimum_ms := int(delay.get("minimumValue", -1))
	var maximum_ms := int(delay.get("maximumValue", -1))
	var interval := String(delay.get("distribution", "")) == "uniform-inclusive-integer"
	if range_value <= 0.0 or pre_attack_ms < 0.0 or damage <= 0.0:
		return {}
	if (not interval and delay_ms < 0.0) or (interval and (minimum_ms < 0 or maximum_ms < minimum_ms)):
		return {}
	var source_scale := float(sim._rules.get("source_map_transform_scale", 1.0))
	var attack := {
		"range": range_value * source_scale,
		"minimum_range": float((combat.get("minimumAttackRange", {}) as Dictionary).get("value", 0.0)) * source_scale,
		"damage": damage,
		"period_ticks": maxi(1, roundi((float(minimum_ms) if interval else delay_ms) / (sim.TICK_SECONDS * 1000.0))),
		"pre_attack_ticks": maxi(0, roundi(pre_attack_ms / (sim.TICK_SECONDS * 1000.0))),
		"projectile_speed": float((combat.get("projectileSpeed", {}) as Dictionary).get("value", 0.0)) * source_scale,
		"projectile_object_id": String(combat.get("projectileObjectId", "")),
		"weapon_id": String(combat.get("weaponId", "")),
	}
	if interval:
		attack["delay_between_shots_distribution"] = "uniform-inclusive-integer"
		attack["delay_between_shots_minimum_ms"] = minimum_ms
		attack["delay_between_shots_maximum_ms"] = maximum_ms
	return attack


func _replacement_structure_row(structure_id: int, previous: Dictionary, object_id: String, spec: Dictionary, preserve_state: bool) -> Dictionary:
	var maximum := maxi(1, int(spec.get("maximum_health", 1)))
	var health := maximum
	if preserve_state:
		var prior_maximum := maxi(1, int(previous.get("maximum_health", 1)))
		health = clampi(roundi(float(int(previous.get("health", 0))) * float(maximum) / float(prior_maximum)), 0, maximum)
	var result = {
		"id": structure_id,
		"team": int(previous.get("team", -1)),
		"kind": "structure",
		"structure_kind": String(spec.get("structure_kind", object_id)),
		"source_object_id": object_id,
		"object_id": object_id,
		"name": String(spec.get("name", object_id)),
		"position": Vector2(previous.get("position", Vector2.ZERO)),
		"rally": Vector2(previous.get("rally", previous.get("position", Vector2.ZERO))),
		"health": health,
		"maximum_health": maximum,
		"construction_progress": 1.0,
		"level": int(previous.get("level", 1)) if preserve_state else 1,
		"completed_upgrades": (previous.get("completed_upgrades", []) as Array).duplicate() if preserve_state else [],
		"upgrade_queue": [],
		"production": (spec.get("production", []) as Array).duplicate(),
		"queue": [],
		"damage_remainders": (previous.get("damage_remainders", {}) as Dictionary).duplicate(true) if preserve_state else {},
		"income_per_payout": int(spec.get("income_per_payout", 0)),
	}
	for key in ["facing", "facing_radians", "rotation", "orientation", "elevation", "castle_owner_structure_id", "castle_piece_of_fortress", "castle_piece_index", "castle_piece_structure_ids", "expansion_pad_id", "expansion_of_fortress", "expansion_pad_index", "build_plot_id", "wall_connection_ids"]:
		if previous.has(key):
			result[key] = previous[key].duplicate(true) if typeof(previous[key]) in [TYPE_ARRAY, TYPE_DICTIONARY] else previous[key]
	if typeof(spec.get("attack")) == TYPE_DICTIONARY:
		result["attack"] = (spec["attack"] as Dictionary).duplicate(true)
	return result


func _reconcile_replacement_containment(structure_id: int, replacement: Dictionary) -> void:
	if not sim.containment.has(structure_id):
		return
	var capacity := int(replacement.get("transport_capacity", 0)) if replacement.has("horde_transport") else 0
	var passengers := (sim.containment.get(structure_id, []) as Array).duplicate()
	for index in range(passengers.size() - 1, capacity - 1, -1):
		sim._finish_transport_exit(structure_id, int(passengers[index]))


# De-staticed on extraction (instance sim access).
func _replace_self_receipt(policy: Dictionary, receipt: String) -> void:
	var receipts := policy.get("unsupported_semantics", []) as Array
	if not receipts.has(receipt):
		receipts.append(receipt)
	policy["unsupported_semantics"] = receipts


func _attach_citadel_slaughter_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("citadel_slaughter"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var passenger_filter := _typed_contract_tokens(fields, "PassengerFilter")
	if passenger_filter == ["GENERIC_FACTION_SLAUGHTERABLE"]:
		# Exact retail gamedata.ini define (BFME2:79 / RotWK:81), expanded here
		# because module contracts intentionally preserve the authored macro token.
		passenger_filter = ["ANY", "+INFANTRY", "+CAVALRY", "-HERO", "-DOZER", "-SUMMONED"]
	var cashback := fields.get("CashBackPercent", {}) as Dictionary
	var ratio: Variant = cashback.get("ratio")
	var capacity: Variant = _module_contract_value(fields, "ContainMax", null)
	if passenger_filter.is_empty() or typeof(ratio) not in [TYPE_INT, TYPE_FLOAT] or typeof(capacity) != TYPE_INT or int(capacity) < 1:
		return
	var upgrades: Array[String] = []
	var upgrade_value: Variant = _module_contract_value(fields, "UpgradeForRingEntry", [])
	if typeof(upgrade_value) != TYPE_ARRAY:
		return
	for value in upgrade_value as Array:
		upgrades.append(String(value))
	var destroy_filter := _typed_contract_tokens(fields, "ObjectToDestroyForRingEntry")
	if destroy_filter.is_empty():
		return
	row["citadel_slaughter"] = {
		"passenger_filter": passenger_filter,
		"contained_statuses": _typed_contract_tokens(fields, "ObjectStatusOfContained"),
		"cashback_ratio": float(ratio),
		"capacity": int(capacity),
		"allow_enemies": bool(_module_contract_value(fields, "AllowEnemiesInside", false)),
		"allow_allies": bool(_module_contract_value(fields, "AllowAlliesInside", false)),
		"allow_neutral": bool(_module_contract_value(fields, "AllowNeutralInside", false)),
		"allow_own": bool(_module_contract_value(fields, "AllowOwnPlayerInsideOverride", false)),
		"enter_sound": String(_module_contract_value(fields, "EnterSound", "")),
		"entry_offset_source": sim._container_contract_offset(fields, "EntryOffset"),
		"entry_position_source": sim._container_contract_offset(fields, "EntryPosition"),
		"exit_offset_source": sim._container_contract_offset(fields, "ExitOffset"),
		"ring_status": String(_module_contract_value(fields, "StatusForRingEntry", "")),
		"ring_upgrades": upgrades,
		"ring_destroy_filter": destroy_filter,
		"ring_fx": String(_module_contract_value(fields, "FXForRingEntry", "")),
		"slaughter_count": 0,
		"cashback_total": 0,
		"ring_entry_count": 0,
		"unsupported_semantics": ["ring_fx_requires_presentation_binding"] if fields.has("FXForRingEntry") else [],
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}


func enter_citadel_slaughter(structure_id: int, entity_id: int) -> Dictionary:
	if not sim.structures.has(structure_id) or not sim.entities.has(entity_id):
		return {"ok": false, "reason": "citadel-or-passenger-missing"}
	var citadel := sim.structures[structure_id] as Dictionary
	if not citadel.has("citadel_slaughter"):
		_attach_structure_module_contracts(citadel)
	var policy := citadel.get("citadel_slaughter", {}) as Dictionary
	if policy.is_empty():
		return {"ok": false, "reason": "typed-citadel-slaughter-contract-missing"}
	if int(citadel.get("health", 0)) <= 0:
		return {"ok": false, "reason": "citadel-dead"}
	if sim.entity_container.has(entity_id):
		return {"ok": false, "reason": "entity-already-contained"}
	if sim.passenger_count(structure_id) >= int(policy.get("capacity", 0)):
		return {"ok": false, "reason": "capacity-full"}
	var passenger := sim.entities[entity_id] as Dictionary
	if not sim._transport_filter_accepts(passenger, policy.get("passenger_filter", []) as Array):
		return {"ok": false, "reason": "passenger-filter-refused"}
	var relation = sim.team_relationship(int(citadel.get("team", -1)), int(passenger.get("team", -2)))
	var admitted = (relation == "local" and (bool(policy.get("allow_own", false)) or bool(policy.get("allow_allies", false)))) or (relation == "allied" and bool(policy.get("allow_allies", false))) or (relation == "enemy" and bool(policy.get("allow_enemies", false))) or (relation == "unavailable" and bool(policy.get("allow_neutral", false)))
	if not admitted:
		return {"ok": false, "reason": "ownership-refused", "relationship": relation}
	var contained = sim.contain_entity(structure_id, entity_id)
	if not bool(contained.get("ok", false)):
		return contained
	passenger["transport_prior_status"] = (passenger.get("object_status", {}) as Dictionary).duplicate(true)
	var statuses := passenger.get("object_status", {}) as Dictionary
	for value in policy.get("contained_statuses", []) as Array:
		statuses[String(value)] = true
	passenger["object_status"] = statuses
	passenger["position"] = Vector2(citadel.get("position", Vector2.ZERO)) + sim._retail_source_to_sim_offset(Vector2(policy.get("entry_position_source", Vector2.ZERO)))
	var ring_status := String(policy.get("ring_status", ""))
	var ring_entry := ring_status != "" and bool(statuses.get(ring_status, false))
	if ring_entry:
		return _consume_citadel_ring_entry(structure_id, entity_id, citadel, passenger, policy)
	var cost := int(passenger.get("build_cost", -1))
	if cost < 0:
		var unit_type := String(passenger.get("unit_type", ""))
		if not sim._unit_production_rules.has(unit_type):
			cost = -1
		else:
			cost = sim._production_rule_value(unit_type, "cost_rule", "default_cost")
	if cost < 0:
		sim.exit_entity_container(entity_id)
		passenger["object_status"] = (passenger.get("transport_prior_status", {}) as Dictionary).duplicate(true)
		passenger.erase("transport_prior_status")
		return {"ok": false, "reason": "passenger-cost-unresolved"}
	var cashback := maxi(0, roundi(float(cost) * float(policy.get("cashback_ratio", 0.0))))
	var owner_team := int(citadel.get("team", -1))
	sim.team_resources[owner_team] = sim.resources_for_team(owner_team) + cashback
	sim.exit_entity_container(entity_id)
	passenger["health"] = 0
	passenger["member_health"] = []
	sim._bookkeep_battalion_death(entity_id, passenger, "FADED", [])
	sim.entities.erase(entity_id)
	policy["slaughter_count"] = int(policy.get("slaughter_count", 0)) + 1
	policy["cashback_total"] = int(policy.get("cashback_total", 0)) + cashback
	sim._emit_event("citadel.slaughtered", structure_id, entity_id, {"cashback": cashback, "cashback_ratio": float(policy.get("cashback_ratio", 0.0)), "enter_sound": String(policy.get("enter_sound", "")), "entry_offset_source": policy.get("entry_offset_source", Vector2.ZERO)})
	return {"ok": true, "reason": "", "result": "slaughtered", "cashback": cashback}


func _consume_citadel_ring_entry(structure_id: int, entity_id: int, citadel: Dictionary, passenger: Dictionary, policy: Dictionary) -> Dictionary:
	var team := int(citadel.get("team", -1))
	var upgrades := policy.get("ring_upgrades", []) as Array
	if not upgrades.is_empty():
		var team_owned := sim.team_upgrades.get(team, {}) as Dictionary
		team_owned[String(upgrades[0])] = true
		sim.team_upgrades[team] = team_owned
		var completed := citadel.get("completed_upgrades", []) as Array
		for index in range(1, upgrades.size()):
			var upgrade := String(upgrades[index])
			if not completed.has(upgrade): completed.append(upgrade)
		completed.sort(); citadel["completed_upgrades"] = completed
	var destroy_passenger = sim._transport_filter_accepts(passenger, policy.get("ring_destroy_filter", []) as Array)
	sim.exit_entity_container(entity_id)
	if destroy_passenger:
		passenger["health"] = 0; passenger["member_health"] = []
		sim._bookkeep_battalion_death(entity_id, passenger, "FADED", [])
		sim.entities.erase(entity_id)
	else:
		passenger["object_status"] = (passenger.get("transport_prior_status", {}) as Dictionary).duplicate(true)
		passenger.erase("transport_prior_status")
		passenger["position"] = Vector2(citadel.get("position", Vector2.ZERO)) + sim._retail_source_to_sim_offset(Vector2(policy.get("exit_offset_source", Vector2.ZERO)))
	policy["ring_entry_count"] = int(policy.get("ring_entry_count", 0)) + 1
	sim._emit_event("citadel.ring_entry", structure_id, entity_id, {"upgrades": upgrades.duplicate(), "destroyed": destroy_passenger, "fx": String(policy.get("ring_fx", "")), "enter_sound": String(policy.get("enter_sound", ""))})
	return {"ok": true, "reason": "", "result": "ring-entry", "destroyed": destroy_passenger, "upgrades": upgrades.duplicate()}


func _resolve_citadel_slaughter_death(structure_id: int, citadel: Dictionary) -> void:
	var policy := citadel.get("citadel_slaughter", {}) as Dictionary
	if policy.is_empty() or not sim.containment.has(structure_id):
		return
	for passenger_value in (sim.containment.get(structure_id, []) as Array).duplicate():
		var passenger_id := int(passenger_value)
		sim.exit_entity_container(passenger_id)
		if not sim.entities.has(passenger_id): continue
		var passenger := sim.entities[passenger_id] as Dictionary
		passenger["object_status"] = (passenger.get("transport_prior_status", {}) as Dictionary).duplicate(true)
		passenger.erase("transport_prior_status")
		passenger["position"] = Vector2(citadel.get("position", Vector2.ZERO)) + sim._retail_source_to_sim_offset(Vector2(policy.get("exit_offset_source", Vector2.ZERO)))
		sim._emit_event("citadel.passenger_ejected", structure_id, passenger_id, {"reason": "citadel-death"})


func _attach_ocl_update_contract(row:Dictionary,contract:Dictionary)->void:
	if String(contract.get("extraction",""))!="typed" or row.has("ocl_update"):return
	var fields:=contract.get("fields",{}) as Dictionary;var minimum:=_ship_contract_delay_ticks(float(_module_contract_value(fields,"MinDelay",0.0)));var maximum:=_ship_contract_delay_ticks(float(_module_contract_value(fields,"MaxDelay",0.0)))
	row["ocl_update"]={"ocl":String(_module_contract_value(fields,"OCL","")),"minimum_ticks":minimum,"maximum_ticks":maximum,"amount":int(_module_contract_value(fields,"Amount",1)),"next_tick":sim.tick_index+sim.logic_random_int(minimum,maximum),"emission_count":0,"unsupported_semantics":[]}


func _step_ocl_updates()->void:
	var owners:Array=[]
	for entity_id in sim.entity_ids():owners.append({"id":entity_id,"kind":"entity"})
	for structure_id in sim.structure_ids():owners.append({"id":structure_id,"kind":"structure"})
	for owner_ref_value in owners:
		var owner_ref:=owner_ref_value as Dictionary;var table =sim.structures if String(owner_ref.get("kind"))=="structure" else sim.entities;var owner_id:=int(owner_ref.get("id"));if not table.has(owner_id):continue
		var owner:=table[owner_id] as Dictionary;var policy:=owner.get("ocl_update",{}) as Dictionary
		if policy.is_empty() or int(owner.get("health",0))<=0 or sim.tick_index<int(policy.get("next_tick",0)):continue
		for index in int(policy.get("amount",1)):
			var entry:={"team":int(owner.get("team",-1)),"position":owner.get("position",Vector2.ZERO),"creation_list":String(policy.get("ocl","")),"source_entity":owner_id,"tick":sim.tick_index};var hatch =sim.hatch_create_object_die_entry(entry);sim._emit_event("ocl_update.emitted",owner_id,0,{"ocl":policy.get("ocl"),"ordinal":index,"ok":hatch.get("ok",false)})
		policy["emission_count"]=int(policy.get("emission_count",0))+1;policy["next_tick"]=sim.tick_index+sim.logic_random_int(int(policy.get("minimum_ticks",0)),int(policy.get("maximum_ticks",0)));owner["ocl_update"]=policy


func _attach_stances_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("stances_behavior"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var template := String(_module_contract_value(fields, "StanceTemplate", "")).strip_edges()
	if template == "":
		return
	var modifier_rules := sim._rules.get("attribute_modifier_rules", {}) as Dictionary
	var stance_rows: Dictionary = {}
	var unsupported: Array[String] = []
	for stance in ["Aggressive", "HoldGround", "Porcupine"]:
		var modifier_id := "%sStance%s" % [template, stance]
		if modifier_rules.has(modifier_id):
			stance_rows[stance] = (modifier_rules[modifier_id] as Dictionary).duplicate(true)
	if stance_rows.is_empty():
		unsupported.append("unresolved_stance_modifier_template:%s" % template)
	row["stances_behavior"] = {
		"template": template,
		"available_stances": ["HoldGround", "Battle", "Aggressive"] + (["Porcupine"] if stance_rows.has("Porcupine") else []),
		"modifiers": stance_rows,
		"unsupported_semantics": unsupported,
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}
	if not row.has("stance"):
		row["stance"] = "Battle"
	_apply_stance_modifier(row)


func set_entity_stance(entity_id: int, stance: String) -> Dictionary:
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity-missing"}
	var row := sim.entities[entity_id] as Dictionary
	if not row.has("stances_behavior"):
		_attach_module_contracts(row)
	if not row.has("stances_behavior"):
		return {"ok": false, "reason": "typed-stances-contract-missing"}
	var normalized = _normalize_stance_name(stance)
	var policy := row["stances_behavior"] as Dictionary
	if normalized == "" or not (policy.get("available_stances", []) as Array).has(normalized):
		return {"ok": false, "reason": "stance-not-available:%s" % stance}
	if not (policy.get("unsupported_semantics", []) as Array).is_empty():
		return {"ok": false, "reason": String((policy.get("unsupported_semantics", []) as Array)[0])}
	row["stance"] = normalized
	_apply_stance_modifier(row)
	sim._emit_event("stance.changed", entity_id, 0, {"stance": normalized, "template": String(policy.get("template", ""))})
	return {"ok": true, "reason": "", "stance": normalized}


func toggle_entity_stance(entity_id: int) -> Dictionary:
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity-missing"}
	var current := String((sim.entities[entity_id] as Dictionary).get("stance", "Battle"))
	var order := ["HoldGround", "Battle", "Aggressive"]
	var index := order.find(current)
	return set_entity_stance(entity_id, String(order[(index + 1) % order.size()]))


# De-staticed on extraction (instance sim access).
func _normalize_stance_name(stance: String) -> String:
	match stance.strip_edges().to_lower():
		"holdground", "hold_ground", "hold ground":
			return "HoldGround"
		"battle":
			return "Battle"
		"aggressive":
			return "Aggressive"
		"porcupine":
			return "Porcupine"
	return ""


func _apply_stance_modifier(row: Dictionary) -> void:
	var table = row.get("timed_modifiers", {}) as Dictionary
	table.erase("stance")
	var stance := String(row.get("stance", "Battle"))
	if stance == "Battle":
		if table.is_empty():
			row.erase("timed_modifiers")
		else:
			row["timed_modifiers"] = table
		return
	var policy := row.get("stances_behavior", {}) as Dictionary
	var modifier := (policy.get("modifiers", {}) as Dictionary).get(stance, {}) as Dictionary
	if modifier.is_empty():
		return
	table["stance"] = {
		"modifiers": (modifier.get("effects", []) as Array).duplicate(true),
		"expires_tick": 2147483647,
		"category": String(modifier.get("category", "STANCE")),
		"source_id": int(row.get("id", 0)),
		"modifier_id": "%sStance%s" % [String(policy.get("template", "")), stance],
	}
	row["timed_modifiers"] = table


func _attach_attribute_modifier_aura_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed":
		return
	for existing_value in row.get("attribute_modifier_auras", []) as Array:
		var existing := existing_value as Dictionary
		if String(existing.get("tag", "")) == String(contract.get("tag", "")) and int(existing.get("line", -1)) == int(contract.get("line", -2)):
			return
	var fields := contract.get("fields", {}) as Dictionary
	var starts: Variant = _module_contract_value(fields, "StartsActive", null)
	var bonus := String(_module_contract_value(fields, "BonusName", "")).strip_edges()
	if typeof(starts) != TYPE_BOOL or bonus == "":
		return
	var range_value: Variant = fields.get("Range", {})
	var range_source := -1.0
	var unsupported: Array[String] = []
	if typeof(range_value) == TYPE_DICTIONARY:
		var range_row := range_value as Dictionary
		if typeof(range_row.get("value")) in [TYPE_INT, TYPE_FLOAT]:
			range_source = float(range_row.get("value"))
		else:
			var expression := String(range_row.get("expression", ""))
			var defines := sim._rules.get("aura_range_defines", {}) as Dictionary
			if typeof(defines.get(expression)) in [TYPE_INT, TYPE_FLOAT]:
				range_source = float(defines[expression])
			else:
				unsupported.append("unresolved_range_expression:%s" % expression)
	if range_source < 0.0:
		return
	var modifier_rules := sim._rules.get("attribute_modifier_rules", {}) as Dictionary
	var modifier: Dictionary = modifier_rules.get(bonus, {}) as Dictionary
	if modifier.is_empty() or (modifier.get("effects", []) as Array).is_empty():
		unsupported.append("unresolved_modifier_list:%s" % bonus)
	var rows: Array = row.get("attribute_modifier_auras", []) as Array
	rows.append({
		"bonus_name": bonus,
		"starts_active": bool(starts),
		"triggered_by": _typed_contract_tokens(fields, "TriggeredBy"),
		"conflicts_with": _typed_contract_tokens(fields, "ConflictsWith"),
		"object_filter": _typed_contract_tokens(fields, "ObjectFilter"),
		"anti_categories": _typed_contract_tokens(fields, "AntiCategory"),
		"required_conditions": _typed_contract_tokens(fields, "RequiredConditions"),
		"target_enemy": bool(_module_contract_value(fields, "TargetEnemy", false)),
		"allow_self": bool(_module_contract_value(fields, "AllowSelf", false)),
		"run_while_dead": bool(_module_contract_value(fields, "RunWhileDead", false)),
		"affect_contained_only": bool(_module_contract_value(fields, "AffectContainedOnly", false)),
		"max_active_rank": int(_module_contract_value(fields, "MaxActiveRank", 2147483647)),
		"range_source": range_source,
		"refresh_ticks": maxi(1, _ship_contract_delay_ticks(float(_module_contract_value(fields, "RefreshDelay", 0.0)))),
		"next_refresh_tick": sim.tick_index,
		"modifier": modifier.duplicate(true),
		"unsupported_semantics": unsupported,
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	})
	row["attribute_modifier_auras"] = rows


func _resolve_lifetime_bound(field: Variant) -> Dictionary:
	if typeof(field) != TYPE_DICTIONARY:
		return {"ok": false, "reason": "missing-bound"}
	var value := field as Dictionary
	if typeof(value.get("milliseconds")) in [TYPE_INT, TYPE_FLOAT]:
		return {"ok": true, "milliseconds": int(value.get("milliseconds"))}
	var expression := String(value.get("expression", ""))
	var defines := sim._rules.get("lifetime_defines", {}) as Dictionary
	if typeof(defines.get(expression)) in [TYPE_INT, TYPE_FLOAT]:
		return {"ok": true, "milliseconds": int(defines[expression])}
	return {"ok": false, "reason": "unresolved-lifetime-expression:%s" % expression}


func _attach_lifetime_update_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("lifetime_update"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var wait_for_wakeup := bool(_module_contract_value(fields, "WaitForWakeUp", false))
	var minimum := _resolve_lifetime_bound(fields.get("MinLifetime"))
	var maximum := _resolve_lifetime_bound(fields.get("MaxLifetime"))
	var unsupported: Array[String] = []
	if not bool(minimum.get("ok", false)) or not bool(maximum.get("ok", false)):
		if fields.has("MinLifetime") or fields.has("MaxLifetime"):
			unsupported.append(String(minimum.get("reason", maximum.get("reason", "unresolved-lifetime"))))
		if not wait_for_wakeup:
			return
	var min_ms := int(minimum.get("milliseconds", 0))
	var max_ms := int(maximum.get("milliseconds", 0))
	if min_ms < 0 or max_ms < 0:
		return
	if max_ms < min_ms:
		var swap := min_ms
		min_ms = max_ms
		max_ms = swap
	row["lifetime_update"] = {
		"min_ms": min_ms,
		"max_ms": max_ms,
		"wait_for_wakeup": wait_for_wakeup,
		"awake": not wait_for_wakeup,
		"expire_tick": -1,
		"death_type": String(_module_contract_value(fields, "DeathType", "NORMAL")).to_upper(),
		"unsupported_semantics": unsupported,
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}
	if not wait_for_wakeup and unsupported.is_empty():
		_arm_lifetime(row)


func _arm_lifetime(row: Dictionary) -> bool:
	var lifetime = row.get("lifetime_update", {}) as Dictionary
	if lifetime.is_empty() or not (lifetime.get("unsupported_semantics", []) as Array).is_empty():
		return false
	if int(lifetime.get("expire_tick", -1)) >= 0:
		return true
	var duration_ms = sim.logic_random_int(int(lifetime.get("min_ms", 0)), int(lifetime.get("max_ms", 0)))
	lifetime["awake"] = true
	lifetime["selected_duration_ms"] = duration_ms
	lifetime["expire_tick"] = sim.tick_index + _ship_contract_delay_ticks(float(duration_ms))
	row["lifetime_update"] = lifetime
	return true


func wake_lifetime(object_id: int, object_kind: String = "entity") -> Dictionary:
	var table = sim.structures if object_kind == "structure" else sim.entities
	if not table.has(object_id):
		return {"ok": false, "reason": "object-missing"}
	var row := table[object_id] as Dictionary
	if not row.has("lifetime_update"):
		if object_kind == "structure":
			_attach_structure_module_contracts(row)
		else:
			_attach_module_contracts(row)
	if not row.has("lifetime_update"):
		return {"ok": false, "reason": "typed-lifetime-contract-missing"}
	if not bool((row["lifetime_update"] as Dictionary).get("wait_for_wakeup", false)):
		return {"ok": false, "reason": "lifetime-does-not-wait"}
	if not _arm_lifetime(row):
		return {"ok": false, "reason": "lifetime-bound-unresolved"}
	return {"ok": true, "reason": "", "expire_tick": int((row["lifetime_update"] as Dictionary).get("expire_tick", -1))}


func _step_lifetime_updates() -> void:
	var entity_keys = sim.entity_ids()
	for entity_id in entity_keys:
		if not sim.entities.has(entity_id):
			continue
		var row := sim.entities[entity_id] as Dictionary
		if not row.has("lifetime_update") and not row.has("module_contracts"):
			_attach_module_contracts(row)
		var lifetime = row.get("lifetime_update", {}) as Dictionary
		if int(lifetime.get("expire_tick", -1)) >= 0 and sim.tick_index >= int(lifetime.get("expire_tick", -1)):
			_expire_lifetime_entity(entity_id, row, String(lifetime.get("death_type", "NORMAL")))
	for structure_id in sim.structure_ids():
		if not sim.structures.has(structure_id):
			continue
		var row := sim.structures[structure_id] as Dictionary
		if not row.has("lifetime_update") and not bool(row.get("structure_module_contracts_attached", false)):
			_attach_structure_module_contracts(row)
		var lifetime = row.get("lifetime_update", {}) as Dictionary
		if int(lifetime.get("expire_tick", -1)) >= 0 and sim.tick_index >= int(lifetime.get("expire_tick", -1)):
			_expire_lifetime_structure(structure_id, row, String(lifetime.get("death_type", "NORMAL")))


func _expire_lifetime_entity(entity_id: int, row: Dictionary, death_type: String) -> void:
	if int(row.get("health", 0)) <= 0:
		return
	var health_values: Array = row.get("member_health", []) as Array
	for index in health_values.size():
		health_values[index] = 0
	row["member_health"] = health_values
	row["health"] = 0
	_schedule_respawn_update(entity_id, row, death_type, 0)
	sim._apply_playable_unit_death_policy(row, death_type, [])
	sim._consume_create_object_die(row, death_type)
	sim._schedule_fire_weapon_when_dead(row, death_type, "battalion")
	sim._emit_event("lifetime.expired", entity_id, 0, {"death_type": death_type})


func _expire_lifetime_structure(structure_id: int, row: Dictionary, death_type: String) -> void:
	if int(row.get("health", 0)) <= 0:
		return
	row["health"] = 0
	sim._schedule_fire_weapon_when_dead(row, death_type, "structure")
	sim._consume_create_object_die(row, death_type)
	sim._begin_ship_slow_death(structure_id, row, death_type)
	sim._emit_event("lifetime.expired", structure_id, 0, {"death_type": death_type})


func _step_attribute_modifier_auras() -> void:
	var source_rows: Array[Dictionary] = []
	for entity_id in sim.entity_ids():
		var row := sim.entities[entity_id] as Dictionary
		if not row.has("attribute_modifier_auras") and not row.has("module_contracts"):
			_attach_module_contracts(row)
		if row.has("attribute_modifier_auras"):
			source_rows.append(row)
	for structure_id in sim.structure_ids():
		var row := sim.structures[structure_id] as Dictionary
		if not row.has("attribute_modifier_auras") and not bool(row.get("structure_module_contracts_attached", false)):
			_attach_structure_module_contracts(row)
		if row.has("attribute_modifier_auras"):
			source_rows.append(row)
	for source in source_rows:
		_step_attribute_modifier_aura_source(source)


func _step_attribute_modifier_aura_source(source: Dictionary) -> void:
	var rules := source.get("attribute_modifier_auras", []) as Array
	for rule_index in rules.size():
		var rule := rules[rule_index] as Dictionary
		if sim.tick_index < int(rule.get("next_refresh_tick", 0)):
			continue
		var refresh_ticks := int(rule.get("refresh_ticks", 1))
		rule["next_refresh_tick"] = sim.tick_index + refresh_ticks
		rules[rule_index] = rule
		if not _aura_source_active(source, rule):
			continue
		var modifier := rule.get("modifier", {}) as Dictionary
		if modifier.is_empty() or not (rule.get("unsupported_semantics", []) as Array).is_empty():
			continue
		var origin := Vector2(source.get("position", Vector2.ZERO))
		var scale := float(sim._rules.get("source_map_transform_scale", 1.0))
		var radius = float(rule.get("range_source", 0.0)) * (scale if scale > 0.0 else 1.0)
		var source_id := int(source.get("id", 0))
		var source_team := int(source.get("team", -1))
		for target_id in sim.entity_ids():
			var target := sim.entities[target_id] as Dictionary
			if target_id == source_id and not bool(rule.get("allow_self", false)):
				continue
			var hostile = sim._is_hostile(source_team, int(target.get("team", -2)))
			if hostile != bool(rule.get("target_enemy", false)):
				continue
			if bool(rule.get("affect_contained_only", false)) and not sim.entity_container.has(target_id):
				continue
			if not sim._transport_filter_accepts(target, rule.get("object_filter", []) as Array):
				continue
			if Vector2(target.get("position", Vector2.ZERO)).distance_to(origin) > radius:
				continue
			_apply_typed_aura_to_target(source_id, target, rule, modifier, refresh_ticks)
	source["attribute_modifier_auras"] = rules


func _aura_source_active(source: Dictionary, rule: Dictionary) -> bool:
	if int(source.get("health", 0)) <= 0 and not bool(rule.get("run_while_dead", false)):
		return false
	if int(source.get("level", 1)) > int(rule.get("max_active_rank", 2147483647)):
		return false
	var upgrades: Array = source.get("completed_upgrades", []) as Array
	var applied := source.get("applied_upgrades", {}) as Dictionary
	var triggered := rule.get("triggered_by", []) as Array
	if not bool(rule.get("starts_active", false)):
		var triggered_now := false
		for upgrade_value in triggered:
			if _aura_has_upgrade(upgrades, applied, String(upgrade_value)):
				triggered_now = true
				break
		if not triggered_now:
			return false
	for conflict_value in rule.get("conflicts_with", []) as Array:
		if _aura_has_upgrade(upgrades, applied, String(conflict_value)):
			return false
	var statuses := source.get("object_status", {}) as Dictionary
	for condition_value in rule.get("required_conditions", []) as Array:
		if not bool(statuses.get(String(condition_value), false)):
			return false
	return true


# De-staticed on extraction (instance sim access).
func _aura_has_upgrade(upgrades: Array, applied: Dictionary, sought: String) -> bool:
	var folded := sought.to_upper()
	for upgrade_value in upgrades:
		if String(upgrade_value).to_upper() == folded:
			return true
	for upgrade_value in applied.keys():
		if String(upgrade_value).to_upper() == folded:
			return true
	return false


func _apply_typed_aura_to_target(source_id: int, target: Dictionary, rule: Dictionary, modifier: Dictionary, refresh_ticks: int) -> void:
	var table = target.get("timed_modifiers", {}) as Dictionary
	for anti_value in rule.get("anti_categories", []) as Array:
		var anti := String(anti_value)
		for key_value in table.keys().duplicate():
			if String((table[key_value] as Dictionary).get("category", "")) == anti:
				table.erase(key_value)
	target["timed_modifiers"] = table
	var category := String(modifier.get("category", ""))
	var stacking := modifier.get("stacking", {}) as Dictionary
	if bool(stacking.get("ignoreIfAnticategoryActive", false)) and (rule.get("anti_categories", []) as Array).has(category):
		return
	var duration_ticks := maxi(1, _ship_contract_delay_ticks(float(modifier.get("duration_ms", refresh_ticks * sim.TICK_SECONDS * 1000.0))))
	var key := "typed-aura:%s:%d" % [String(rule.get("bonus_name", "")), source_id]
	if bool(stacking.get("replaceInCategoryIfLongest", false)) and category != "":
		key = "typed-aura-category:%s" % category
		var current := table.get(key, {}) as Dictionary
		if int(current.get("expires_tick", -1)) > sim.tick_index + duration_ticks:
			return
	table[key] = {
		"modifiers": (modifier.get("effects", []) as Array).duplicate(true),
		"expires_tick": sim.tick_index + duration_ticks,
		"category": category,
		"source_id": source_id,
	}
	target["timed_modifiers"] = table
	sim._emit_event("module.attribute_modifier_aura", source_id, int(target.get("id", 0)), {"bonus_name": String(rule.get("bonus_name", "")), "category": category})


func _attach_model_condition_sound_selector(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed": return
	var states: Variant = (contract.get("fields", {}) as Dictionary).get("SoundState", [])
	if typeof(states) != TYPE_ARRAY or (states as Array).is_empty(): return
	var selectors := row.get("model_condition_sound_selectors", []) as Array
	selectors.append({"states": (states as Array).duplicate(true), "tag": String(contract.get("tag", "")), "line": int(contract.get("line", 0))})
	row["model_condition_sound_selectors"] = selectors


func _attach_random_sound_selector(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("random_sound_selector"): return
	var fields := contract.get("fields", {}) as Dictionary; var chance := fields.get("Chance", {}) as Dictionary
	var ratio: Variant = chance.get("ratio", chance.get("fraction"))
	if typeof(ratio) not in [TYPE_INT, TYPE_FLOAT]: return
	row["random_sound_selector"] = {"chance_fraction": float(ratio), "reroll_every_frame": bool(_module_contract_value(fields, "RerollOnEveryFrame", false)), "voice_priority": int(_module_contract_value(fields, "VoicePriority", 0)), "unsupported_semantics": ["reroll_every_frame_requires_presentation_frame_clock"] if bool(_module_contract_value(fields, "RerollOnEveryFrame", false)) else [], "rng_receipt": "presentation_sequence_hash_not_gameplay_rng"}


func _attach_upgrade_sound_selector(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or String(contract.get("runtimeStatus", "")) != "executable": return
	var fields := contract.get("fields", {}) as Dictionary
	var clauses_value: Variant = fields.get("SoundUpgrade", [])
	if typeof(clauses_value) != TYPE_ARRAY or (clauses_value as Array).is_empty(): return
	var byte_receipt_value: Variant = fields.get("wavSetByteIdentityReceipt", {})
	if typeof(byte_receipt_value) != TYPE_DICTIONARY: return
	var byte_receipt := byte_receipt_value as Dictionary
	if typeof(byte_receipt.get("logicalEventIds", [])) != TYPE_ARRAY or typeof(byte_receipt.get("leaves", [])) != TYPE_ARRAY or (byte_receipt.get("leaves", []) as Array).is_empty(): return
	for leaf_value in byte_receipt.get("leaves", []) as Array:
		if typeof(leaf_value) != TYPE_DICTIONARY: return
		var leaf := leaf_value as Dictionary
		if String(leaf.get("virtualPath", "")) == "" or String(leaf.get("cookedPath", "")) == "": return
		for sha_key in ["sha256", "cookedSha256"]:
			var sha := String(leaf.get(sha_key, ""))
			if sha.length() != 64 or sha != sha.to_lower(): return
			for index in sha.length():
				if not "0123456789abcdef".contains(sha.substr(index, 1)): return
	var clauses: Array[Dictionary] = []
	for clause_value in clauses_value as Array:
		if typeof(clause_value) != TYPE_DICTIONARY: return
		var clause := clause_value as Dictionary
		var required: Variant = clause.get("requiredUpgrades", [])
		var excluded: Variant = clause.get("excludedUpgrades", [])
		var sounds: Variant = clause.get("sounds", {})
		if typeof(required) != TYPE_ARRAY or (required as Array).is_empty() or typeof(excluded) != TYPE_ARRAY or typeof(sounds) != TYPE_DICTIONARY or (sounds as Dictionary).is_empty(): return
		for sound_ids_value in (sounds as Dictionary).values():
			if typeof(sound_ids_value) != TYPE_ARRAY or (sound_ids_value as Array).is_empty(): return
			for sound_id_value in sound_ids_value as Array:
				if String(sound_id_value) == "": return
		clauses.append({"required_upgrades": (required as Array).duplicate(), "excluded_upgrades": (excluded as Array).duplicate(), "sounds": (sounds as Dictionary).duplicate(true), "line": int(clause.get("line", 0))})
	var selectors := row.get("upgrade_sound_selectors", []) as Array
	var identity := "%s:%d" % [String(contract.get("tag", "")), int(contract.get("line", 0))]
	for selector_value in selectors:
		if String((selector_value as Dictionary).get("identity", "")) == identity: return
	selectors.append({"clauses": clauses, "identity": identity, "receipt": "retail-upgrade-sound-logical-ids-preserved", "wav_set_byte_identity_receipt": byte_receipt.duplicate(true)})
	row["upgrade_sound_selectors"] = selectors


func _attach_large_group_audio_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("large_group_audio"): return
	var fields := contract.get("fields", {}) as Dictionary; var key := _typed_contract_tokens(fields, "Key")
	if key.is_empty(): return
	row["large_group_audio"] = {"category_key": key, "unit_weight": int(_module_contract_value(fields, "UnitWeight", 1)), "rng_receipt": "presentation_sequence_hash_not_gameplay_rng"}


func emit_typed_audio_intent(entity_id: int, sound_role: String) -> Dictionary:
	if not sim.entities.has(entity_id): return {"ok": false, "reason": "entity-missing"}
	var row := sim.entities[entity_id] as Dictionary
	if not row.has("model_condition_sound_selectors"): _attach_module_contracts(row)
	var field := _audio_sound_field(sound_role); var active := _audio_active_conditions(row); var candidates: Array[Dictionary] = []
	var owned_upgrades: Dictionary = {}
	for upgrade_value in row.get("completed_upgrades", []) as Array: owned_upgrades[String(upgrade_value).to_lower()] = true
	for upgrade_value in (row.get("applied_upgrades", {}) as Dictionary).keys(): owned_upgrades[String(upgrade_value).to_lower()] = true
	for selector_value in row.get("upgrade_sound_selectors", []) as Array:
		for clause_value in (selector_value as Dictionary).get("clauses", []) as Array:
			var clause := clause_value as Dictionary; var matches := true
			for required_value in clause.get("required_upgrades", []) as Array:
				if not owned_upgrades.has(String(required_value).to_lower()): matches = false; break
			if not matches: continue
			for excluded_value in clause.get("excluded_upgrades", []) as Array:
				if owned_upgrades.has(String(excluded_value).to_lower()): matches = false; break
			if not matches: continue
			var ids_value: Variant = (clause.get("sounds", {}) as Dictionary).get(field, [])
			if typeof(ids_value) != TYPE_ARRAY or (ids_value as Array).is_empty(): continue
			var ids := (ids_value as Array).duplicate()
			if ids != ((selector_value as Dictionary).get("wav_set_byte_identity_receipt", {}) as Dictionary).get("logicalEventIds", []): continue
			candidates.append({"event_id": String(ids[0]), "logical_event_ids": ids, "priority": 2147483647, "line": int(clause.get("line", 0)), "selector_receipt": String((selector_value as Dictionary).get("receipt", "")), "wav_set_byte_identity_receipt": ((selector_value as Dictionary).get("wav_set_byte_identity_receipt", {}) as Dictionary).duplicate(true)})
	# An active upgrade selector overrides the ordinary model-condition route.
	var upgrade_candidates := not candidates.is_empty()
	for selector_value in row.get("model_condition_sound_selectors", []) as Array:
		if upgrade_candidates: break
		for state_value in (selector_value as Dictionary).get("states", []) as Array:
			var state := state_value as Dictionary; var matches := true
			for condition_value in state.get("conditions", []) as Array:
				if not active.has(String(condition_value).to_upper()): matches = false; break
			if not matches: continue
			var sounds := state.get("sounds", {}) as Dictionary; var specific := (state.get("unitSpecificSounds", {}) as Dictionary).get("sounds", {}) as Dictionary
			var sound: Variant = sounds.get(field, specific.get(field))
			if typeof(sound) != TYPE_DICTIONARY: continue
			var priority := int(_module_contract_value(sounds, "VoicePriority", 0)); candidates.append({"event_id": String((sound as Dictionary).get("value", "")), "priority": priority, "line": int(state.get("line", 0))})
	if candidates.is_empty(): return {"ok": false, "reason": "no-matching-model-condition-sound"}
	candidates.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return int(a.get("priority", 0)) > int(b.get("priority", 0)) if int(a.get("priority", 0)) != int(b.get("priority", 0)) else int(a.get("line", 0)) < int(b.get("line", 0)))
	var selected := candidates[0] as Dictionary; var random := row.get("random_sound_selector", {}) as Dictionary
	if not random.is_empty():
		if bool(random.get("reroll_every_frame", false)): return {"ok": false, "reason": "presentation-frame-reroll-unsupported", "rng_receipt": random.get("rng_receipt", "")}
		var roll = _audio_sequence_roll(entity_id, sound_role); if roll >= float(random.get("chance_fraction", 0.0)): return {"ok": false, "reason": "random-selector-suppressed", "roll": roll, "rng_receipt": random.get("rng_receipt", "")}
		selected["priority"] = maxi(int(selected.get("priority", 0)), int(random.get("voice_priority", 0))); selected["roll"] = roll; selected["rng_receipt"] = random.get("rng_receipt", "")
	sim._emit_event("audio.typed_selector", entity_id, 0, {"object_id": String(row.get("object_id", "")), "event_id": String(selected.get("event_id", "")), "logical_event_ids": Array(selected.get("logical_event_ids", [selected.get("event_id", "")])).duplicate(), "sound_role": sound_role, "priority": int(selected.get("priority", 0)), "rng_receipt": String(selected.get("rng_receipt", "model_condition_exact")), "selector_receipt": String(selected.get("selector_receipt", "")), "wav_set_byte_identity_receipt": (selected.get("wav_set_byte_identity_receipt", {}) as Dictionary).duplicate(true)})
	selected["ok"] = true; selected["reason"] = ""; return selected


func emit_large_group_audio_intent(entity_ids_value: Array, sound_role: String) -> Dictionary:
	var grouped: Dictionary = {}
	for value in entity_ids_value:
		var entity_id := int(value); if not sim.entities.has(entity_id): continue
		var row := sim.entities[entity_id] as Dictionary; if not row.has("large_group_audio"): _attach_module_contracts(row)
		var policy := row.get("large_group_audio", {}) as Dictionary; if policy.is_empty(): continue
		var key := " ".join(policy.get("category_key", []) as Array); var bucket := grouped.get(key, {"weight": 0, "members": []}) as Dictionary; bucket["weight"] = int(bucket.get("weight", 0)) + int(policy.get("unit_weight", 1)); (bucket["members"] as Array).append(entity_id); grouped[key] = bucket
	if grouped.is_empty(): return {"ok": false, "reason": "no-large-group-audio-category"}
	var keys := grouped.keys(); keys.sort(); var best_key := String(keys[0]); for key_value in keys: if int((grouped[key_value] as Dictionary).get("weight", 0)) > int((grouped[best_key] as Dictionary).get("weight", 0)): best_key = String(key_value)
	var members := (grouped[best_key] as Dictionary).get("members", []) as Array; members.sort(); var total := 0; for member_id in members: total += int(((sim.entities[int(member_id)] as Dictionary).get("large_group_audio", {}) as Dictionary).get("unit_weight", 1))
	var pick := int(floor(_audio_sequence_roll(total, sound_role + best_key) * total)); var selected_id := int(members[0]); var cursor := 0
	for member_id in members:
		cursor += int(((sim.entities[int(member_id)] as Dictionary).get("large_group_audio", {}) as Dictionary).get("unit_weight", 1)); if pick < cursor: selected_id = int(member_id); break
	var result = emit_typed_audio_intent(selected_id, sound_role); result["category_key"] = best_key; result["group_weight"] = int((grouped[best_key] as Dictionary).get("weight", 0)); result["selected_entity_id"] = selected_id; result["rng_receipt"] = "presentation_sequence_hash_not_gameplay_rng"; return result


func _audio_sequence_roll(entity_id: int, role: String) -> float:
	var hash := 2166136261
	for byte in ("%d:%d:%s" % [sim._typed_audio_roll_sequence, entity_id, role]).to_utf8_buffer(): hash = ((hash ^ int(byte)) * 16777619) & 0xFFFFFFFF
	sim._typed_audio_roll_sequence += 1
	return float(hash & 0xFFFFFF) / 16777216.0


func _attach_fire_spread_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("fire_spread_update"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var minimum := _ship_contract_delay_ticks(float(_module_contract_value(fields, "MinSpreadDelay", 0.0)))
	var maximum := _ship_contract_delay_ticks(float(_module_contract_value(fields, "MaxSpreadDelay", 0.0)))
	var spread_range := float(_module_contract_value(fields, "SpreadTryRange", -1.0))
	if minimum < 0 or maximum < minimum or spread_range < 0.0:
		return
	row["fire_spread_update"] = {
		"burning": false,
		"minimum_delay_ticks": minimum,
		"maximum_delay_ticks": maximum,
		"spread_range_source": spread_range,
		"next_spread_tick": -1,
		"spread_serial": 0,
		"rng_receipt": "deterministic_delay_hash_unproven_retail_rng_seed",
		# FireSpreadUpdate has no ignition/damage field. The fire/damage consumer
		# must explicitly start this scheduler instead of inventing a weapon.
		"unsupported_semantics": ["ignition_source_owned_by_fire_damage_consumer"],
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}


func set_fire_spread_active(object_id: int, active: bool) -> Dictionary:
	var table = sim.structures if sim.structures.has(object_id) else sim.entities
	if not table.has(object_id):
		return {"ok": false, "reason": "object-missing"}
	var row := table[object_id] as Dictionary
	if not row.has("fire_spread_update"):
		if sim.structures.has(object_id): _attach_structure_module_contracts(row)
		else: _attach_module_contracts(row)
	var policy := row.get("fire_spread_update", {}) as Dictionary
	if policy.is_empty():
		return {"ok": false, "reason": "typed-fire-spread-contract-missing"}
	policy["burning"] = active
	policy["next_spread_tick"] = sim.tick_index + _fire_spread_delay(object_id, policy) if active else -1
	row["fire_spread_update"] = policy
	return {"ok": true, "reason": "", "next_spread_tick": int(policy.get("next_spread_tick", -1)), "rng_receipt": String(policy.get("rng_receipt", ""))}


func _fire_spread_delay(object_id: int, policy: Dictionary) -> int:
	var minimum := int(policy.get("minimum_delay_ticks", 0))
	var maximum := int(policy.get("maximum_delay_ticks", minimum))
	var width := maximum - minimum + 1
	if width <= 1:
		return minimum
	var hash := 2166136261
	for byte in ("%d:%d" % [object_id, int(policy.get("spread_serial", 0))]).to_utf8_buffer():
		hash = ((hash ^ int(byte)) * 16777619) & 0xFFFFFFFF
	return minimum + int(hash % width)


func _step_fire_spread_updates() -> void:
	var sources: Array[Dictionary] = []
	for id in sim.entity_ids(): sources.append({"id": id, "kind": "entity"})
	for id in sim.structure_ids(): sources.append({"id": id, "kind": "structure"})
	for source_value in sources:
		var source := source_value as Dictionary
		var source_id := int(source.get("id", 0))
		var source_table = sim.structures if String(source.get("kind", "")) == "structure" else sim.entities
		if not source_table.has(source_id): continue
		var row := source_table[source_id] as Dictionary
		var policy := row.get("fire_spread_update", {}) as Dictionary
		if policy.is_empty() or not bool(policy.get("burning", false)) or sim.tick_index < int(policy.get("next_spread_tick", -1)): continue
		var origin := Vector2(row.get("position", Vector2.ZERO))
		var radius = sim._retail_source_to_sim_offset(Vector2(float(policy.get("spread_range_source", 0.0)), 0.0)).x
		var candidates: Array[Dictionary] = []
		for target_value in sources:
			var target := target_value as Dictionary
			var target_id := int(target.get("id", 0)); var target_kind := String(target.get("kind", ""))
			if target_id == source_id and target_kind == String(source.get("kind", "")): continue
			var target_table = sim.structures if target_kind == "structure" else sim.entities
			if not target_table.has(target_id): continue
			var target_row := target_table[target_id] as Dictionary
			var target_policy := target_row.get("fire_spread_update", {}) as Dictionary
			if target_policy.is_empty() or bool(target_policy.get("burning", false)) or int(target_row.get("health", 1)) <= 0: continue
			var distance := origin.distance_to(Vector2(target_row.get("position", Vector2.ZERO)))
			if distance <= radius: candidates.append({"id": target_id, "kind": target_kind, "distance": distance})
		candidates.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
			if not is_equal_approx(float(a.get("distance", 0.0)), float(b.get("distance", 0.0))): return float(a.get("distance", 0.0)) < float(b.get("distance", 0.0))
			if String(a.get("kind", "")) != String(b.get("kind", "")): return String(a.get("kind", "")) < String(b.get("kind", ""))
			return int(a.get("id", 0)) < int(b.get("id", 0)))
		policy["spread_serial"] = int(policy.get("spread_serial", 0)) + 1
		policy["next_spread_tick"] = sim.tick_index + _fire_spread_delay(source_id, policy)
		row["fire_spread_update"] = policy
		if candidates.is_empty(): continue
		var chosen := candidates[0] as Dictionary
		var chosen_table = sim.structures if String(chosen.get("kind", "")) == "structure" else sim.entities
		var chosen_row := chosen_table[int(chosen.get("id", 0))] as Dictionary
		var chosen_policy := chosen_row.get("fire_spread_update", {}) as Dictionary
		chosen_policy["burning"] = true
		chosen_policy["next_spread_tick"] = sim.tick_index + _fire_spread_delay(int(chosen.get("id", 0)), chosen_policy)
		chosen_row["fire_spread_update"] = chosen_policy
		sim._emit_event("module.fire_spread", source_id, int(chosen.get("id", 0)), {"target_kind": String(chosen.get("kind", "")), "distance": float(chosen.get("distance", 0.0)), "rng_receipt": String(policy.get("rng_receipt", ""))})


func _audio_active_conditions(row: Dictionary) -> Dictionary:
	var result = {}; result[String(row.get("state", "")).to_upper()] = true
	for value in row.get("model_conditions", []) as Array: result[String(value).to_upper()] = true
	for value in (row.get("object_status", {}) as Dictionary).keys(): if bool((row.get("object_status", {}) as Dictionary).get(value, false)): result[String(value).to_upper()] = true
	return result


# De-staticed on extraction (instance sim access).
func _audio_sound_field(role: String) -> String:
	var folded := role.replace("_", "").to_lower(); var map := {"move":"VoiceMove","select":"VoiceSelect","attack":"VoiceAttack","attackcharge":"VoiceAttackCharge","attackmachine":"VoiceAttackMachine","attackstructure":"VoiceAttackStructure","fear":"VoiceFear","guard":"VoiceGuard","impact":"SoundImpact","moveloop":"SoundMoveLoop","garrison":"VoiceGarrison","movetotrees":"VoiceMoveToTrees"}; return String(map.get(folded, role))


func _attach_radiate_fear_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("radiate_fear"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var radius_field := fields.get("EmotionPulseRadius", {}) as Dictionary
	var radius_source := -1.0
	var unsupported: Array[String] = []
	if typeof(radius_field.get("value")) in [TYPE_INT, TYPE_FLOAT]:
		radius_source = float(radius_field.get("value"))
	else:
		var define := String(radius_field.get("define", ""))
		var defines := sim._rules.get("fear_radius_defines", {}) as Dictionary
		if typeof(defines.get(define)) in [TYPE_INT, TYPE_FLOAT]:
			radius_source = float(defines[define])
		else:
			unsupported.append("unresolved_fear_radius:%s" % define)
	if radius_source < 0.0:
		return
	var fear_type := "UNCONTROLLABLE" if bool(_module_contract_value(fields, "GenerateUncontrollableFear", false)) else ("TERROR" if bool(_module_contract_value(fields, "GenerateTerror", false)) else "FEAR")
	row["radiate_fear"] = {"active": bool(_module_contract_value(fields, "InitiallyActive", false)), "triggered_by": String(_module_contract_value(fields, "TriggeredBy", "")), "special_power": int(_module_contract_value(fields, "WhichSpecialPower", -1)), "fear_type": fear_type, "radius_source": radius_source, "interval_ticks": maxi(1, _ship_contract_delay_ticks(float(_module_contract_value(fields, "EmotionPulseInterval", 0.0)))), "next_tick": sim.tick_index, "victim_filter": _typed_contract_tokens(fields, "VictimFilter"), "pulse_count": 0, "unsupported_semantics": unsupported, "tag": String(contract.get("tag", "")), "line": int(contract.get("line", 0))}


func activate_radiate_fear(entity_id: int, special_power: int) -> Dictionary:
	if not sim.entities.has(entity_id): return {"ok": false, "reason": "entity-missing"}
	var policy := (sim.entities[entity_id] as Dictionary).get("radiate_fear", {}) as Dictionary
	if policy.is_empty(): return {"ok": false, "reason": "typed-radiate-fear-contract-missing"}
	if int(policy.get("special_power", -1)) != special_power: return {"ok": false, "reason": "special-power-mismatch"}
	policy["active"] = true; policy["next_tick"] = sim.tick_index
	return {"ok": true, "reason": ""}


func _step_radiate_fear_updates() -> void:
	for source_id in sim.entity_ids():
		var source := sim.entities[source_id] as Dictionary; var policy := source.get("radiate_fear", {}) as Dictionary
		if policy.is_empty() or int(source.get("health", 0)) <= 0: continue
		var trigger := String(policy.get("triggered_by", ""))
		if trigger != "" and _aura_has_upgrade(source.get("completed_upgrades", []) as Array, source.get("applied_upgrades", {}) as Dictionary, trigger): policy["active"] = true
		if not bool(policy.get("active", false)) or sim.tick_index < int(policy.get("next_tick", 0)): continue
		policy["next_tick"] = sim.tick_index + int(policy.get("interval_ticks", 1)); policy["pulse_count"] = int(policy.get("pulse_count", 0)) + 1
		var origin := Vector2(source.get("position", Vector2.ZERO)); var radius = sim._retail_source_to_sim_offset(Vector2(float(policy.get("radius_source", 0.0)), 0.0)).x
		for target_id in sim.entity_ids():
			if target_id == source_id: continue
			var target := sim.entities[target_id] as Dictionary
			if not sim._is_hostile(int(source.get("team", -1)), int(target.get("team", -2))) or int(target.get("health", 0)) <= 0 or bool(target.get("fear_resistant", false)): continue
			var filter := policy.get("victim_filter", []) as Array
			if not _fear_victim_filter_accepts(target, filter): continue
			if Vector2(target.get("position", Vector2.ZERO)).distance_to(origin) > radius: continue
			target["fear_state"] = String(policy.get("fear_type", "FEAR")); target["fear_source_id"] = source_id; target["fear_pulse_tick"] = sim.tick_index
			if String(policy.get("fear_type", "")) in ["TERROR", "UNCONTROLLABLE"]: sim._apply_fear_scatter(origin, target, radius)
		source["radiate_fear"] = policy


func _fear_victim_filter_accepts(target: Dictionary, tokens: Array) -> bool:
	if tokens.is_empty(): return true
	var object_tokens: Array = []
	for value in tokens:
		if String(value).to_upper() not in ["ALL", "NONE", "ENEMIES", "ALLIES"]: object_tokens.append(value)
	return object_tokens.is_empty() or sim._transport_filter_accepts(target, object_tokens)


func _attach_poisoned_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("poisoned_behavior"): return
	var fields := contract.get("fields", {}) as Dictionary
	row["poisoned_behavior"] = {"interval_ticks": maxi(1, _ship_contract_delay_ticks(float(_module_contract_value(fields, "PoisonDamageInterval", 0.0)))), "duration_ticks": maxi(1, _ship_contract_delay_ticks(float(_module_contract_value(fields, "PoisonDuration", 0.0)))), "active": false, "damage_per_pulse": 0.0, "next_tick": -1, "expires_tick": -1, "pulse_count": 0, "unsupported_semantics": []}


func apply_poison(entity_id: int, damage_per_pulse: float) -> Dictionary:
	if not sim.entities.has(entity_id) or damage_per_pulse <= 0.0: return {"ok": false, "reason": "invalid-poison-target"}
	var row := sim.entities[entity_id] as Dictionary; var policy := row.get("poisoned_behavior", {}) as Dictionary
	if policy.is_empty(): return {"ok": false, "reason": "typed-poison-contract-missing"}
	policy["active"] = true; policy["damage_per_pulse"] = damage_per_pulse; policy["next_tick"] = sim.tick_index + int(policy.get("interval_ticks", 1)); policy["expires_tick"] = sim.tick_index + int(policy.get("duration_ticks", 1)); policy["pulse_count"] = 0
	return {"ok": true, "reason": "", "next_tick": policy["next_tick"], "expires_tick": policy["expires_tick"]}


func _step_poisoned_behaviors() -> void:
	for entity_id in sim.entity_ids():
		var row := sim.entities[entity_id] as Dictionary; var policy := row.get("poisoned_behavior", {}) as Dictionary
		if policy.is_empty() or not bool(policy.get("active", false)) or int(row.get("health", 0)) <= 0: continue
		if sim.tick_index > int(policy.get("expires_tick", -1)): policy["active"] = false; continue
		if sim.tick_index >= int(policy.get("next_tick", 0)):
			sim._apply_area_damage_to_battalion(entity_id, float(policy.get("damage_per_pulse", 0.0)), "POISON")
			policy["pulse_count"] = int(policy.get("pulse_count", 0)) + 1; policy["next_tick"] = sim.tick_index + int(policy.get("interval_ticks", 1))
		row["poisoned_behavior"] = policy


func _attach_damage_field_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("damage_field"): return
	var fields := contract.get("fields", {}) as Dictionary; var nugget := fields.get("FireWeaponNugget", {}) as Dictionary
	var weapon := String(_module_contract_value(nugget, "WeaponName", "")); var unsupported: Array[String] = []
	if not sim._death_weapon_rules.has(weapon): unsupported.append("unresolved_damage_field_weapon:%s" % weapon)
	for token in _typed_contract_tokens(fields, "ObjectFilter"):
		if token not in ["ALL", "NONE", "ENEMIES", "ALLIES"]:
			unsupported.append("unsupported_damage_field_filter_token:%s" % token)
	row["damage_field"] = {"radius_source": float(_module_contract_value(fields, "Radius", 0.0)), "object_filter": _typed_contract_tokens(fields, "ObjectFilter"), "required_upgrade": String(_module_contract_value(fields, "RequiredUpgrade", "")), "weapon": weapon, "delay_ticks": maxi(1, _ship_contract_delay_ticks(float(_module_contract_value(nugget, "FireDelay", 0.0)))), "one_shot": bool(_module_contract_value(nugget, "OneShot", false)), "next_tick": sim.tick_index + maxi(1, _ship_contract_delay_ticks(float(_module_contract_value(nugget, "FireDelay", 0.0)))), "fired": false, "unsupported_semantics": unsupported}


func _step_damage_fields() -> void:
	var sources: Array = []
	for entity_id in sim.entity_ids(): sources.append(sim.entities[entity_id])
	for structure_id in sim.structure_ids(): sources.append(sim.structures[structure_id])
	for source_value in sources:
		var source := source_value as Dictionary; var policy := source.get("damage_field", {}) as Dictionary
		if policy.is_empty() or int(source.get("health", 0)) <= 0 or sim.tick_index < int(policy.get("next_tick", 0)) or (bool(policy.get("one_shot", false)) and bool(policy.get("fired", false))): continue
		var required := String(policy.get("required_upgrade", "")); if required != "" and not _aura_has_upgrade(source.get("completed_upgrades", []) as Array, source.get("applied_upgrades", {}) as Dictionary, required): continue
		if not (policy.get("unsupported_semantics", []) as Array).is_empty(): continue
		var weapon := String(policy.get("weapon", "")); var rule := (sim._death_weapon_rules.get(weapon, {}) as Dictionary).duplicate(true); rule["radius_source"] = float(policy.get("radius_source", 0.0)); var filter := policy.get("object_filter", []) as Array; rule["affects"] = "ALLIES ENEMIES" if filter.has("ALLIES") and filter.has("ENEMIES") else ("ALLIES" if filter.has("ALLIES") else "ENEMIES")
		sim._fire_death_weapon({"weapon_id": weapon, "weapon_rule": rule, "point": source.get("position", Vector2.ZERO), "team": int(source.get("team", -1)), "source_id": int(source.get("id", 0)), "death_type": "DAMAGE_FIELD"}); policy["fired"] = true; policy["next_tick"] = sim.tick_index + int(policy.get("delay_ticks", 1)); source["damage_field"] = policy


func _attach_spawn_unit_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("spawn_unit_behavior"): return
	var fields := contract.get("fields", {}) as Dictionary
	row["spawn_unit_behavior"] = {"unit_name": String(_module_contract_value(fields, "UnitName", "")), "unit_command": String(_module_contract_value(fields, "UnitCommand", "")), "spawn_once": bool(_module_contract_value(fields, "SpawnOnce", false)), "spawned": false, "spawned_ids": [], "pending": not fields.has("UnitCommand"), "unsupported_semantics": []}


func request_spawn_unit_command(owner_id: int, command: String) -> Dictionary:
	var table = sim.structures if sim.structures.has(owner_id) else sim.entities
	if not table.has(owner_id): return {"ok": false, "reason": "owner-missing"}
	var policy := (table[owner_id] as Dictionary).get("spawn_unit_behavior", {}) as Dictionary
	if policy.is_empty(): return {"ok": false, "reason": "typed-spawn-unit-contract-missing"}
	if String(policy.get("unit_command", "")) != command: return {"ok": false, "reason": "command-mismatch"}
	if bool(policy.get("spawn_once", false)) and bool(policy.get("spawned", false)): return {"ok": false, "reason": "already-spawned"}
	policy["pending"] = true; return {"ok": true, "reason": ""}


func _step_spawn_unit_behaviors() -> void:
	var owners: Array = []
	for entity_id in sim.entity_ids(): owners.append(sim.entities[entity_id])
	for structure_id in sim.structure_ids(): owners.append(sim.structures[structure_id])
	for owner_value in owners:
		var owner := owner_value as Dictionary; var policy := owner.get("spawn_unit_behavior", {}) as Dictionary
		if policy.is_empty() or not bool(policy.get("pending", false)) or (bool(policy.get("spawn_once", false)) and bool(policy.get("spawned", false))): continue
		var spawned_id = sim.spawn_script_object(String(policy.get("unit_name", "")), int(owner.get("team", -1)), Vector2(owner.get("position", Vector2.ZERO)))
		if spawned_id > 0:
			var ids := policy.get("spawned_ids", []) as Array; ids.append(spawned_id); policy["spawned_ids"] = ids; policy["spawned"] = true; policy["pending"] = false
		else:
			var receipts := policy.get("unsupported_semantics", []) as Array; var receipt = "unresolved_spawn_unit:%s" % String(policy.get("unit_name", "")); if not receipts.has(receipt): receipts.append(receipt); policy["unsupported_semantics"] = receipts; policy["pending"] = false
		owner["spawn_unit_behavior"] = policy


func _attach_hit_reaction_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("hit_reaction"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var tiers: Array[Dictionary] = []
	for index in range(1, 4):
		var timer_key := "HitReactionLifeTimer%d" % index
		var threshold_key := "HitReactionThreshold%d" % index
		if not fields.has(timer_key):
			continue
		var timer: Variant = _module_contract_value(fields, timer_key, null)
		var threshold: Variant = _module_contract_value(fields, threshold_key, null)
		if typeof(timer) != TYPE_INT or typeof(threshold) not in [TYPE_INT, TYPE_FLOAT]:
			return
		tiers.append({"tier": index, "life_ticks": _ship_contract_delay_ticks(float(timer)), "threshold": float(threshold)})
	if tiers.size() not in [1, 3]:
		return
	row["hit_reaction"] = {"tiers": tiers, "fast_hits_reset": bool(_module_contract_value(fields, "FastHitsResetReaction", false)), "hits": [], "active_tier": 0, "expires_tick": -1, "unsupported_semantics": ["reaction_animation_requires_presentation_binding"], "tag": String(contract.get("tag", "")), "line": int(contract.get("line", 0))}


func record_hit_reaction(entity_id: int, damage: float) -> Dictionary:
	if damage <= 0.0 or not sim.entities.has(entity_id):
		return {"ok": false, "reason": "invalid-hit"}
	var row := sim.entities[entity_id] as Dictionary
	if not row.has("hit_reaction"):
		_attach_module_contracts(row)
	var policy := row.get("hit_reaction", {}) as Dictionary
	if policy.is_empty():
		return {"ok": false, "reason": "typed-hit-reaction-contract-missing"}
	var hits := policy.get("hits", []) as Array
	hits.append({"tick": sim.tick_index, "damage": damage})
	var highest := 0
	var selected_life := 0
	for tier_value in policy.get("tiers", []) as Array:
		var tier := tier_value as Dictionary
		var total := 0.0
		var earliest = sim.tick_index - int(tier.get("life_ticks", 0))
		for hit_value in hits:
			var hit := hit_value as Dictionary
			if int(hit.get("tick", 0)) >= earliest:
				total += float(hit.get("damage", 0.0))
		if total >= float(tier.get("threshold", 0.0)):
			highest = int(tier.get("tier", 0))
			selected_life = int(tier.get("life_ticks", 0))
	if highest > 0:
		if highest >= int(policy.get("active_tier", 0)) or bool(policy.get("fast_hits_reset", false)):
			policy["active_tier"] = highest
			policy["expires_tick"] = sim.tick_index + selected_life
			sim._emit_event("module.hit_reaction", entity_id, 0, {"tier": highest, "damage": damage})
	policy["hits"] = hits
	row["hit_reaction"] = policy
	return {"ok": true, "reason": "", "tier": int(policy.get("active_tier", 0)), "expires_tick": int(policy.get("expires_tick", -1))}


func _step_hit_reactions() -> void:
	for entity_id in sim.entity_ids():
		var row := sim.entities[entity_id] as Dictionary
		var policy := row.get("hit_reaction", {}) as Dictionary
		if policy.is_empty():
			continue
		if int(policy.get("expires_tick", -1)) >= 0 and sim.tick_index >= int(policy.get("expires_tick", -1)):
			policy["active_tier"] = 0
			policy["expires_tick"] = -1
		var max_life := 0
		for tier_value in policy.get("tiers", []) as Array:
			max_life = maxi(max_life, int((tier_value as Dictionary).get("life_ticks", 0)))
		var retained: Array = []
		for hit_value in policy.get("hits", []) as Array:
			if int((hit_value as Dictionary).get("tick", 0)) >= sim.tick_index - max_life:
				retained.append(hit_value)
		policy["hits"] = retained
		row["hit_reaction"] = policy


func _attach_animal_ai_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("animal_ai"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	row["animal_ai"] = {"flee_range_source": float(_module_contract_value(fields, "FleeRange", 0.0)), "flee_distance_source": float(_module_contract_value(fields, "FleeDistance", _module_contract_value(fields, "MaxWanderDistance", 0.0))), "wander_percent": float(_module_contract_value(fields, "WanderPercentage", 0.0)), "max_wander_distance_source": float(_module_contract_value(fields, "MaxWanderDistance", 0.0)), "max_wander_radius_source": float(_module_contract_value(fields, "MaxWanderRadius", 0.0)), "update_ticks": maxi(1, _ship_contract_delay_ticks(float(_module_contract_value(fields, "UpdateTimer", 1000.0)))), "next_tick": sim.tick_index, "home": Vector2(row.get("position", Vector2.ZERO)), "unsupported_semantics": [], "tag": String(contract.get("tag", "")), "line": int(contract.get("line", 0))}


func _step_animal_ai_updates() -> void:
	for entity_id in sim.entity_ids():
		var row := sim.entities[entity_id] as Dictionary
		var policy := row.get("animal_ai", {}) as Dictionary
		if policy.is_empty() or int(row.get("health", 0)) <= 0 or sim.tick_index < int(policy.get("next_tick", 0)):
			continue
		policy["next_tick"] = sim.tick_index + int(policy.get("update_ticks", 1))
		var origin := Vector2(row.get("position", Vector2.ZERO))
		var threat_id := _nearest_hostile_entity(entity_id, float(policy.get("flee_range_source", 0.0)))
		if threat_id != 0:
			var away := origin - Vector2((sim.entities[threat_id] as Dictionary).get("position", Vector2.ZERO))
			if away.length_squared() <= 0.000001:
				away = Vector2.RIGHT if entity_id % 2 == 0 else Vector2.LEFT
			row["destination"] = origin + away.normalized() * sim._retail_source_to_sim_offset(Vector2(float(policy.get("flee_distance_source", 0.0)), 0.0)).x
			row["state"] = "flee"
		elif sim.logic_random_int(1, 10000) <= int(round(float(policy.get("wander_percent", 0.0)) * 100.0)):
			var angle := TAU * float(sim.logic_random_int(0, 359)) / 360.0
			var distance_source := float(sim.logic_random_int(0, int(round(float(policy.get("max_wander_distance_source", 0.0))))))
			var candidate = origin + Vector2(cos(angle), sin(angle)) * sim._retail_source_to_sim_offset(Vector2(distance_source, 0.0)).x
			var home := Vector2(policy.get("home", origin))
			var max_radius = sim._retail_source_to_sim_offset(Vector2(float(policy.get("max_wander_radius_source", 0.0)), 0.0)).x
			if candidate.distance_to(home) > max_radius and max_radius > 0.0:
				candidate = home + (candidate - home).normalized() * max_radius
			row["destination"] = candidate
			row["state"] = "wander"
		row["animal_ai"] = policy


func _attach_threat_finder_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("threat_finder"):
		return
	var radius: Variant = _module_contract_value(contract.get("fields", {}) as Dictionary, "DefaultRadius", null)
	if typeof(radius) not in [TYPE_INT, TYPE_FLOAT] or float(radius) < 0.0:
		return
	row["threat_finder"] = {"radius_source": float(radius), "next_tick": sim.tick_index, "update_ticks": 1, "last_target_id": 0, "unsupported_semantics": [], "tag": String(contract.get("tag", "")), "line": int(contract.get("line", 0))}


func _step_threat_finders() -> void:
	for entity_id in sim.entity_ids():
		var row := sim.entities[entity_id] as Dictionary
		var policy := row.get("threat_finder", {}) as Dictionary
		if policy.is_empty() or int(row.get("health", 0)) <= 0 or sim.tick_index < int(policy.get("next_tick", 0)):
			continue
		policy["next_tick"] = sim.tick_index + int(policy.get("update_ticks", 1))
		var previous_target := int(policy.get("last_target_id", 0))
		var target_id := _nearest_hostile_entity(entity_id, float(policy.get("radius_source", 0.0)))
		policy["last_target_id"] = target_id
		if target_id != 0:
			row["target_id"] = target_id
			row["target_kind"] = "battalion"
		elif previous_target != 0 and int(row.get("target_id", 0)) == previous_target:
			row["target_id"] = 0
		row["threat_finder"] = policy


func _nearest_hostile_entity(source_id: int, radius_source: float) -> int:
	if not sim.entities.has(source_id):
		return 0
	var source := sim.entities[source_id] as Dictionary
	var origin := Vector2(source.get("position", Vector2.ZERO))
	var radius = sim._retail_source_to_sim_offset(Vector2(radius_source, 0.0)).x
	var best_id := 0
	var best_distance := INF
	for candidate_id in sim.entity_ids():
		if candidate_id == source_id:
			continue
		var candidate = sim.entities[candidate_id] as Dictionary
		if int(candidate.get("health", 0)) <= 0 or not sim._is_hostile(int(source.get("team", -1)), int(candidate.get("team", -2))):
			continue
		var distance := origin.distance_to(Vector2(candidate.get("position", Vector2.ZERO)))
		if distance <= radius and (distance < best_distance or (is_equal_approx(distance, best_distance) and candidate_id < best_id)):
			best_id = candidate_id
			best_distance = distance
	return best_id


func _attach_large_group_bonus_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("large_group_bonus"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var modifier_name := String(_module_contract_value(fields, "AttributeModifier", ""))
	var modifiers := sim._rules.get("attribute_modifier_rules", {}) as Dictionary
	var modifier := modifiers.get(modifier_name, {}) as Dictionary
	var unsupported: Array[String] = []
	if modifier.is_empty() or (modifier.get("effects", []) as Array).is_empty():
		unsupported.append("unresolved_modifier_list:%s" % modifier_name)
	row["large_group_bonus"] = {
		"update_ticks": maxi(1, _ship_contract_delay_ticks(float(_module_contract_value(fields, "UpdateRate", 0.0)))),
		"next_tick": sim.tick_index,
		"member_filter": _typed_contract_tokens(fields, "HordeMemberFilter"),
		"count": int(_module_contract_value(fields, "Count", 1)),
		"radius_source": float(_module_contract_value(fields, "Radius", 0.0)),
		"ruboff_radius_source": float(_module_contract_value(fields, "RubOffRadius", 0.0)),
		"allies_only": bool(_module_contract_value(fields, "AlliesOnly", false)),
		"modifier_name": modifier_name,
		"modifier": modifier.duplicate(true),
		"active": false,
		"unsupported_semantics": unsupported,
		"tag": String(contract.get("tag", "")), "line": int(contract.get("line", 0)),
	}


func _step_large_group_bonus_updates() -> void:
	for source_id in sim.entity_ids():
		var source := sim.entities[source_id] as Dictionary
		if not source.has("large_group_bonus"):
			_attach_module_contracts(source)
		var policy := source.get("large_group_bonus", {}) as Dictionary
		if policy.is_empty() or sim.tick_index < int(policy.get("next_tick", 0)):
			continue
		var update_ticks := int(policy.get("update_ticks", 1))
		policy["next_tick"] = sim.tick_index + update_ticks
		var radius_source := float(policy.get("ruboff_radius_source" if bool(policy.get("active", false)) else "radius_source", 0.0))
		var radius = sim._retail_source_to_sim_offset(Vector2(radius_source, 0.0)).x
		var count := 0
		for target_id in sim.entity_ids():
			var target := sim.entities[target_id] as Dictionary
			if int(target.get("health", 0)) <= 0:
				continue
			if bool(policy.get("allies_only", false)) and sim._is_hostile(int(source.get("team", -1)), int(target.get("team", -2))):
				continue
			if not sim._transport_filter_accepts(target, policy.get("member_filter", []) as Array):
				continue
			if Vector2(target.get("position", Vector2.ZERO)).distance_to(Vector2(source.get("position", Vector2.ZERO))) <= radius:
				count += 1
		policy["active"] = count >= int(policy.get("count", 1))
		var table = source.get("timed_modifiers", {}) as Dictionary
		var key := "large-group:%s" % String(policy.get("modifier_name", ""))
		if bool(policy.get("active", false)) and (policy.get("unsupported_semantics", []) as Array).is_empty():
			var modifier := policy.get("modifier", {}) as Dictionary
			table[key] = {"modifiers": (modifier.get("effects", []) as Array).duplicate(true), "expires_tick": sim.tick_index + update_ticks + 1, "category": String(modifier.get("category", "")), "source_id": source_id}
		else:
			table.erase(key)
		source["timed_modifiers"] = table
		source["large_group_bonus"] = policy


# De-staticed on extraction (family cohesion with _passive_area_effect_field).
func _passive_area_effect_number(fields: Dictionary, key: String) -> float:
	var raw: Variant = fields.get(key, fields.get(key.to_lower(), null))
	if typeof(raw) == TYPE_DICTIONARY:
		var row := raw as Dictionary
		if typeof(row.get("value")) in [TYPE_INT, TYPE_FLOAT]:
			return float(row.get("value"))
	var text := _passive_area_effect_field(fields, key)
	return float(text) if text.is_valid_float() else 0.0


# De-staticed on extraction (family cohesion with _passive_area_effect_field).
func _passive_area_effect_percent(text: String) -> float:
	var value := text.strip_edges()
	if not value.ends_with("%"):
		return 0.0
	value = value.trim_suffix("%").strip_edges()
	return float(value) / 100.0 if value.is_valid_float() else 0.0


# De-staticed on extraction (family cohesion with _passive_area_effect_field).
func _passive_area_effect_yes(fields: Dictionary, key: String) -> bool:
	return _passive_area_effect_field(fields, key).to_lower() in ["yes", "true", "1"]
