extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Member attacks carved out of retail_slice_sim.gd (drawer 20): auto-targeting, member attack scheduling, projectile launches, weapon modes and condition tokens, member target assignment.
## State stays on the sim; the sim keeps one-line delegates under the original names.



func _nearest_attack_move_target(row: Dictionary) -> int:
	var _sim = sim
	var origin := Vector2(row.get("position", Vector2.ZERO))
	var limit = maxf(float(row.get("attack_range", 1.0)), float(row.get("vision_range", 17.5)) * _sim._ability_vision_multiplier(row))
	return _sim._spatial_nearest_hostile(
		row, int(row.get("team", _sim.PLAYER_TEAM)), origin, limit,
		_sim.SPATIAL_FILTER_ENGAGE | _sim.SPATIAL_FILTER_STEALTH
	)


func _nearest_auto_target(row: Dictionary) -> Dictionary:
	var _sim = sim
	if not bool(row.get("auto_acquire_enabled", true)):
		return {}
	if _sim._stealth_active(row) and not bool(row.get("auto_acquire_while_stealthed", true)):
		return {}
	var self_team = int(row.get("team", _sim.PLAYER_TEAM))
	var origin := Vector2(row.get("position", Vector2.ZERO))
	var stance := String(row.get("stance", "Battle"))
	var stance_state := _stance_state(row, stance)
	var limit = float(row.get("vision_range", 0.0)) * float(stance_state.get("visionMultiplier", 1.0)) * _sim._ability_vision_multiplier(row)
	if stance == "HoldGround":
		var modes: Dictionary = row.get("weapon_modes", {}) as Dictionary
		var default_mode: Dictionary = modes.get(String(row.get("default_weapon_mode", "default")), {}) as Dictionary
		limit = float(default_mode.get("attack_range", row.get("attack_range", 0.0)))
	if limit <= 0.0:
		return {}
	var best_id := 0
	var best_kind := ""
	var best_distance = limit
	# InvisibilityUpdate: a cloaked battalion is never auto-acquired, so the
	# stealth filter travels with the neighbourhood query.
	var nearest_battalion = _sim._spatial_nearest_hostile(
		row, self_team, origin, limit, _sim.SPATIAL_FILTER_ENGAGE | _sim.SPATIAL_FILTER_STEALTH
	)
	if nearest_battalion != 0:
		best_distance = origin.distance_to(
			Vector2((_sim.entities[nearest_battalion] as Dictionary).get("position", Vector2.ZERO))
		)
		best_id = nearest_battalion
		best_kind = "battalion"
	# Structures are not indexed: their count does not grow with army size, so
	# this loop is linear in map furniture rather than in units. It still runs
	# against the battalion result above, preserving the original precedence
	# where an equidistant structure examined later wins the tie.
	if not bool(row.get("auto_acquire_attack_buildings", true)):
		return {"id": best_id, "kind": best_kind} if best_id != 0 else {}
	for candidate in _sim._hostile_living_structure_ids(self_team):
		if bool((_sim.structures[candidate] as Dictionary).get("not_auto_acquirable", false)):
			# holes.ini NOT_AUTOACQUIRABLE: an exposed rebuild hole is only ever
			# destroyed by an explicit attack order, never by idle acquisition.
			continue
		# SURFACE-TO-SURFACE, the same semantic the RANGE gate uses. Round 20
		# made firing at a structure subtract the target's authored bounding
		# circle (SAGE getDistanceSquared(..., FROM_BOUNDINGSPHERE_2D); see
		# sim._target_footprint_radius and the range test in _step_attacks) but left
		# ACQUISITION centre-to-centre. The two halves then disagreed, and the
		# disagreement was not academic:
		#
		#   A HoldGround melee horde standing at a fortress wall clamps `limit`
		#   to its own AttackRange (~0.305 sim units, see the HoldGround branch
		#   above). The Men fortress footprint is 1.9604. Centre-to-centre, that
		#   horde is ~2.0 units from the fortress centre — SIX TIMES its
		#   acquisition limit — so it never acquired a building it was already
		#   in weapon range of and could hit the instant it was ordered to.
		#
		#   The same subtraction also decides ties. `distance <= best_distance`
		#   lets a structure win an equal-distance comparison against a
		#   battalion; measured centre-to-centre a structure's distance is
		#   inflated by its whole footprint, so a structure lost every tie it
		#   should have won.
		#
		# Only the CANDIDATE's radius is subtracted, never the acquirer's, for
		# exactly the reason spelled out at sim._target_footprint_radius: this sim's
		# unit position is the horde centre, not a soldier bounding sphere.
		var distance := maxf(
			0.0,
			origin.distance_to(Vector2((_sim.structures[candidate] as Dictionary).get("position", Vector2.ZERO)))
			- _sim._target_footprint_radius(candidate, "structure")
		)
		if distance <= best_distance:
			best_distance = distance
			best_id = candidate
			best_kind = "structure"
	return {"id": best_id, "kind": best_kind} if best_id != 0 else {}


func _rearm_mood_idle_cadence(row: Dictionary) -> void:
	if int(row.get("mood_attack_check_rate_ticks", 0)) <= 0:
		return
	row.erase("mood_next_check_tick")
	row["mood_randomize_next_check"] = true


func _step_member_attacks(attacker_id: int, row: Dictionary, target_id: int, target_kind: String) -> void:
	var _sim = sim
	var member_health_values: Array = row.get("member_health", [])
	var start_ticks: Array = row.get("member_attack_start_ticks", [])
	var hit_ticks: Array = row.get("member_attack_hit_ticks", [])
	var tokens: Array = row.get("member_attack_tokens", [])
	var target_indices: Array = row.get("member_target_indices", [])
	var weapon_modes: Array = row.get("member_weapon_modes", [])
	var release_tokens: Array = row.get("member_attack_release_tokens", [])
	if member_health_values.is_empty() or start_ticks.size() != member_health_values.size() or hit_ticks.size() != member_health_values.size() or tokens.size() != member_health_values.size() or target_indices.size() != member_health_values.size() or weapon_modes.size() != member_health_values.size() or release_tokens.size() != member_health_values.size():
		return
	if target_kind == "battalion":
		_ensure_member_target_assignments(row, _sim.entities[target_id] as Dictionary)
		target_indices = row.get("member_target_indices", [])
	if int(row.get("attack_cooldown", 0)) == 0:
		var pre_attack_ticks := maxi(0, int(row.get("pre_attack_ticks", 0)))
		var maximum_stagger = maxi(0, _sim.MEMBER_ATTACK_STAGGER_WINDOW_TICKS - 1)
		var coast_ticks := maxi(0, int(row.get("continuous_fire_coast_ticks", 0)))
		var expiration_tick := int(row.get("continuous_fire_expiration_tick", -1))
		if expiration_tick < 0 or _sim.tick_index >= expiration_tick:
			row["continuous_fire_count"] = 0
		var continuous_threshold := maxi(0, int(row.get("continuous_fire_one", 0)))
		var rate_multiplier := maxf(1.0, float(row.get("continuous_fire_rate_multiplier", 1.0)))
		var base_reload_or_delay_ms := float(row.get("delay_between_shots_ms", 0.0))
		if int(row.get("clip_size", 0)) == 1:
			base_reload_or_delay_ms = float(row.get("clip_reload_time_ms", base_reload_or_delay_ms))
		# SAGE captures the possible-next-shot frame before FiringTracker promotes
		# the shot count into the next continuous-fire tier.
		var coast_anchor_ms := base_reload_or_delay_ms
		if continuous_threshold > 0 and int(row.get("continuous_fire_count", 0)) > continuous_threshold:
			coast_anchor_ms = floorf(coast_anchor_ms / rate_multiplier)
		# PreAttackType (weapon.ini:4233 GondorArcherBow = PER_POSITION;
		# :10808 HaradrimBow = PER_SHOT). PER_POSITION charges PreAttackDelay
		# only when the attacker acquires a NEW target/attack position (and
		# on the first shot of an engagement). Sustained fire on a stationary
		# target cycles at firing + clip reload only. PER_SHOT (and PER_ATTACK
		# until it has its own rule) keeps the every-shot windup.
		# PreAttackRandomAmount is compiled but not applied (deferred).
		var pre_attack_type := String(row.get("pre_attack_type", "PER_SHOT")).to_upper()
		var last_target_id := int(row.get("pre_attack_last_target_id", 0))
		var last_target_kind := String(row.get("pre_attack_last_target_kind", ""))
		var same_engagement := (
			last_target_id == target_id
			and last_target_kind == target_kind
			and last_target_id != 0
		)
		var charge_pre_attack := pre_attack_type != "PER_POSITION" or not same_engagement
		var windup_ticks := pre_attack_ticks if charge_pre_attack else 0
		var windup_ms := float(row.get("pre_attack_delay_ms", 0.0)) if charge_pre_attack else 0.0
		row["attack_sequence"] = int(row.get("attack_sequence", 0)) + 1
		row["continuous_fire_count"] = int(row.get("continuous_fire_count", 0)) + 1
		# Persist last-target only for PER_POSITION. Writing these keys on the
		# pin harness (PER_SHOT default, synthetic rules) would move the
		# 3000-tick state hash.
		if pre_attack_type == "PER_POSITION":
			row["pre_attack_last_target_id"] = target_id
			row["pre_attack_last_target_kind"] = target_kind
		var attack_sequence := int(row["attack_sequence"])
		for member_index in range(member_health_values.size()):
			if int(member_health_values[member_index]) <= 0:
				start_ticks[member_index] = -1
				hit_ticks[member_index] = -1
				continue
			var stagger = posmod(attacker_id + member_index * 3 + attack_sequence, _sim.MEMBER_ATTACK_STAGGER_WINDOW_TICKS)
			weapon_modes[member_index] = String(row.get("active_weapon_mode", "default"))
			start_ticks[member_index] = _sim.tick_index + stagger
			# Every member owns its attack boundary. This avoids the old whole-
			# horde structure impact while preserving deterministic replay.
			hit_ticks[member_index] = _sim.tick_index + stagger + windup_ticks
		var reload_or_delay_ms := base_reload_or_delay_ms
		if continuous_threshold > 0 and int(row["continuous_fire_count"]) > continuous_threshold:
			reload_or_delay_ms = floorf(reload_or_delay_ms / rate_multiplier)
		var cadence_ms := (
			windup_ms
			+ float(row.get("firing_duration_ms", 0.0))
			+ reload_or_delay_ms
		)
		var cadence_ticks = maxi(1, roundi(cadence_ms / (_sim.TICK_SECONDS * 1000.0)))
		var coast_anchor_ticks = maxi(1, roundi(coast_anchor_ms / (_sim.TICK_SECONDS * 1000.0)))
		row["continuous_fire_expiration_tick"] = _sim.tick_index + coast_anchor_ticks + coast_ticks
		row["attack_cooldown"] = maxi(
			cadence_ticks,
			windup_ticks + maximum_stagger + 1
		)
		row["attack_windup"] = windup_ticks + maximum_stagger
		_sim._emit_event("combat.swing", attacker_id, target_id, {
			"attack_sequence": attack_sequence,
			"living_members": _living_member_count(row),
			"object_id": String(row.get("object_id", "")),
			"charged_pre_attack": charge_pre_attack,
		})
	for member_index in range(member_health_values.size()):
		if int(member_health_values[member_index]) <= 0:
			continue
		if int(start_ticks[member_index]) == _sim.tick_index:
			tokens[member_index] = int(tokens[member_index]) + 1
			start_ticks[member_index] = -1
			_sim._emit_event("combat.member_swing", attacker_id, target_id, {
				"member_index": member_index,
				"target_member_index": int(target_indices[member_index]),
				"weapon_mode": String(weapon_modes[member_index]),
				"member_attack_token": int(tokens[member_index]),
				"attack_sequence": int(row.get("attack_sequence", 0)),
			})
		if int(hit_ticks[member_index]) == _sim.tick_index:
			hit_ticks[member_index] = -1
			# The weapon-release instant for EVERY mode (a melee swing releases
			# too); the projectile-only bookkeeping below is a separate question.
			_mark_member_release(attacker_id, member_index)
			if String(weapon_modes[member_index]) != "close":
				release_tokens[member_index] = int(release_tokens[member_index]) + 1
				_sim._emit_event("combat.member_fire", attacker_id, target_id, {
					"member_index": member_index,
					"target_member_index": int(target_indices[member_index]),
					"weapon_mode": String(weapon_modes[member_index]),
					"member_release_token": int(release_tokens[member_index]),
				})
			if _sim._target_alive(target_id, target_kind):
				var forced_target := int(target_indices[member_index]) if target_kind == "battalion" else -1
				# A recorded WeaponSetUpgrade replaces the horde's base weapon
				# damage with the compiled upgraded damage (no invented
				# multipliers); its target-filtered DamageScalars apply per hit
				# inside sim._apply_member_damage.
				var weapon_effect = _sim._applied_weapon_effect(row)
				var outgoing_damage := float(row.get("member_damage", 1))
				if float(weapon_effect.get("damage", 0.0)) > 0.0:
					outgoing_damage = float(weapon_effect.get("damage"))
				var swing_damage = maxi(1, roundi(outgoing_damage * float(_stance_state(row).get("damageMultiplier", 1.0)) * _sim._ability_outgoing_multiplier(row)))
				if target_kind == "battalion" and _sim.entities.has(target_id):
					swing_damage = maxi(
						1,
						roundi(float(swing_damage) * _sim._flanking_outgoing_multiplier(row, _sim.entities[target_id]))
					)
				if _member_weapon_has_projectile(row):
					_launch_member_projectile(
						attacker_id,
						member_index,
						row,
						target_id,
						target_kind,
						forced_target,
						swing_damage,
						weapon_effect,
						int(release_tokens[member_index])
					)
				else:
					_sim._apply_member_damage(
						attacker_id,
						member_index,
						target_id,
						swing_damage,
						target_kind,
						int(row.get("attack_sequence", 0)),
						forced_target
					)
					# Upgrade-gated bonus nuggets remain instant only on an instant
					# weapon; projectile-capable weapons carry them to impact below.
					_apply_member_bonus_nuggets(
						attacker_id, member_index, row, target_id, target_kind,
						forced_target, weapon_effect
					)
	row["member_attack_start_ticks"] = start_ticks
	row["member_attack_hit_ticks"] = hit_ticks
	row["member_attack_tokens"] = tokens
	row["member_target_indices"] = target_indices
	row["member_weapon_modes"] = weapon_modes
	row["member_attack_release_tokens"] = release_tokens
	row["attack_windup"] = maxi(0, int(row.get("attack_windup", 0)) - 1)


func _member_weapon_has_projectile(row: Dictionary) -> bool:
	return sim._projectiles_subsystem().member_weapon_has_projectile(row)


func _scaled_projectile_components(components: Array, outgoing_amount: int) -> Array:
	return sim._projectiles_subsystem().scaled_projectile_components(components, outgoing_amount)


func _launch_member_projectile(
	attacker_id: int,
	member_index: int,
	row: Dictionary,
	target_id: int,
	target_kind: String,
	forced_target: int,
	swing_damage: int,
	weapon_effect: Dictionary,
	release_token: int
) -> void:
	sim._projectiles_subsystem().launch_member_projectile(attacker_id, member_index, row, target_id, target_kind, forced_target, swing_damage, weapon_effect, release_token)


func _apply_member_bonus_nuggets(
	attacker_id: int,
	member_index: int,
	row: Dictionary,
	target_id: int,
	target_kind: String,
	forced_target: int,
	weapon_effect: Dictionary
) -> void:
	sim._combat_subsystem()._apply_member_bonus_nuggets(attacker_id, member_index, row, target_id, target_kind, forced_target, weapon_effect)


func _clear_member_attack_schedule(row: Dictionary) -> void:
	var start_ticks: Array = row.get("member_attack_start_ticks", [])
	var hit_ticks: Array = row.get("member_attack_hit_ticks", [])
	for index in range(start_ticks.size()):
		start_ticks[index] = -1
	for index in range(hit_ticks.size()):
		hit_ticks[index] = -1
	row["member_attack_start_ticks"] = start_ticks
	row["member_attack_hit_ticks"] = hit_ticks
	# Leaving the engagement (or swapping weapon sets) ends the firing span, so
	# the derived FIRING_*/RELOADING window goes with the schedule.
	sim._member_fire_ticks.erase(int(row.get("id", 0)))
	# Leaving the firing engagement drops the PER_POSITION last-target so the
	# next acquire (including the same unit after an idle) charges windup.
	# Only touch the keys if they already exist — adding them on the pin
	# harness (which never fires) would move the state hash.
	if row.has("pre_attack_last_target_id"):
		row["pre_attack_last_target_id"] = 0
		row["pre_attack_last_target_kind"] = ""


func _clear_member_targets(row: Dictionary) -> void:
	var targets: Array = row.get("member_target_indices", [])
	for index in range(targets.size()):
		targets[index] = -1
	row["member_target_indices"] = targets


func _weapon_mode_for_distance(row: Dictionary, distance: float) -> String:
	# An engaged TOGGLE_WEAPONSET pins the battalion to its toggled compiled
	# profile: retail's WEAPONSET_TOGGLE_1 condition overrides the range-based
	# selection entirely until the player toggles back.
	var toggle_mode := String(row.get("weapon_toggle_mode", ""))
	if toggle_mode != "" and (row.get("weapon_modes", {}) as Dictionary).has(toggle_mode):
		return toggle_mode
	var close_mode := String(row.get("close_weapon_mode", ""))
	var switch_distance := float(row.get("close_weapon_switch_distance", 0.0))
	if bool(row.get("unsupported_close_weapon", false)) and switch_distance > 0.0 and distance <= switch_distance:
		return "unsupported-close"
	if close_mode != "" and switch_distance > 0.0 and distance <= switch_distance:
		return close_mode
	return String(row.get("default_weapon_mode", "default"))


# Formations carry real combat behavior, not just spacing (provisional
# magnitudes; retail per-formation modifiers are an M3 INI extraction item).
# Block reads as the braced shield-wall stance: tighter, tougher, slower, and
# resistant to cavalry impact.
const FORMATION_EFFECTS := {
	"Block": {
		"incoming_damage_multiplier": 0.85,
		"speed_multiplier": 0.85,
		"trample_damage_multiplier": 0.5,
	},
}


func _formation_effects(row: Dictionary) -> Dictionary:
	return FORMATION_EFFECTS.get(String(row.get("formation_mode", "Line")), {}) as Dictionary


func _stance_state(row: Dictionary, requested: String = "") -> Dictionary:
	var contract: Dictionary = row.get("stance_contract", {}) as Dictionary
	var states: Dictionary = contract.get("states", {}) as Dictionary
	var stance := requested if requested != "" else String(row.get("stance", "Battle"))
	var selected: Dictionary = states.get(stance, {}) as Dictionary
	if not selected.is_empty():
		return selected
	return {
		"damageMultiplier": 1.0,
		"incomingDamageMultiplier": 1.0,
		"visionMultiplier": 1.0,
		"speedMultiplier": 1.0,
	}


func _apply_weapon_mode(row: Dictionary, mode: String) -> bool:
	var modes: Dictionary = row.get("weapon_modes", {}) as Dictionary
	var selected: Dictionary = modes.get(mode, {}) as Dictionary
	if selected.is_empty():
		return false
	var prior := String(row.get("active_weapon_mode", ""))
	if (
		prior != ""
		and prior != mode
		and not (row.get("permanent_weapon_locks", []) as Array).is_empty()
	):
		# WeaponSet::updateWeaponSet implicitly releases even a permanent lock
		# before installing a different template set unless the incoming set
		# authors WeaponLockSharedAcrossSets. Neither BFME2 nor RotWK retail
		# authors that field, so every current-corpus mode transition releases.
		row["permanent_weapon_locks"] = []
	row["active_weapon_mode"] = mode
	for optional_projectile_field in [
		"projectile_object_id", "projectile_speed", "projectile_speed_source",
		"radius_damage_affects",
	]:
		if not selected.has(optional_projectile_field):
			row.erase(optional_projectile_field)
	# damage_components stay on the row unless the selected mode compiles
	# its own mix. Blanking here wiped the unit-rule mix on every attack tick.
	for field in [
		"attack_range", "attack_range_source", "minimum_attack_range",
		"minimum_attack_range_source", "delay_between_shots_ms",
		"pre_attack_delay_ms", "pre_attack_type", "pre_attack_random_amount_ms",
		"firing_duration_ms", "attack_period_ticks",
		"pre_attack_ticks", "firing_duration_ticks", "member_damage", "clip_size",
		"clip_reload_time_ms", "continuous_fire_one", "continuous_fire_coast_ticks",
		"continuous_fire_rate_multiplier", "projectile_object_id", "projectile_speed",
		"projectile_speed_source", "radius_damage_affects", "damage_components",
		"damage_type",
	]:
		if selected.has(field):
			row[field] = selected[field]
	if prior != "" and prior != mode:
		_clear_member_attack_schedule(row)
	return true


## Retail WeaponSlot -> the letter SAGE suffixes onto the weapon-cycle model
## conditions (PREATTACK_A, FIRING_B, ...). The retail corpus authors exactly
## three slots (playable_unit_compiler._WEAPON_SLOT_NAMES: PRIMARY, SECONDARY,
## TERTIARY), so the `_D` family has no source in this game's data and is never
## produced here — see `weapon_condition_deferred_reasons`.
const WEAPON_SLOT_CONDITION_LETTERS := {
	"primary": "A",
	"secondary": "B",
	"tertiary": "C",
}

## Live WeaponSet condition -> its model-condition token. The compiled weapon
## mode keys ARE the authored WeaponSet condition, lower-cased
## (playable_unit_compiler._conditional_weapon_modes), so this table only
## restates which of them retail also raises as a model condition. A live mode
## that is not listed is receipted, never uppercased into an invented token.
const LIVE_WEAPON_SET_CONDITION_MODES := {
	"weaponset_toggle_1": "WEAPONSET_TOGGLE_1",
	"weaponset_toggle_2": "WEAPONSET_TOGGLE_2",
	"weaponset_toggle_3": "WEAPONSET_TOGGLE_3",
	"weaponset_toggle_4": "WEAPONSET_TOGGLE_4",
	"mounted": "MOUNTED",
	"close_range": "CLOSE_RANGE",
}

## Tick each member last RELEASED its weapon: entity id -> member index -> tick.
##
## Everything else the weapon cycle needs is already authoritative —
## `member_attack_start_ticks` / `member_attack_hit_ticks` bracket the windup —
## but the release tick is destroyed the moment it is used: `_step_member_attacks`
## sets `member_attack_hit_ticks[i] = -1` on the firing tick, so afterwards
## "firing" and "idle" look identical on the row.
##
## That one fact is recorded HERE and not on the entity row on purpose: every row
## key is walked by `_authoritative_state()`, so a new per-member array would move
## the pinned 3000-tick hash (`tests/retail_state_pin_runner.gd`) for a value no
## rule reads. This table is tick-derived observation, exactly like `events`.
##
## Stated rather than hidden (AGENTS.md rule 5): `restore()` clears it, so a
## member that was inside its FiringDuration when the snapshot was taken reports
## no FIRING_* until its next release. PREATTACK_*, the pre-attack half of the
## composites and every weapon-set condition read authoritative keys and survive.


func member_weapon_condition_tokens(entity_id: int) -> Array:
	## Live SAGE weapon-cycle model conditions, one token Array per battalion
	## member, index-aligned with `member_health`. The presenter unions these into
	## the condition set it hands `AnimationStateSelect.select()`; retail binds
	## PREATTACK_A -> ATKF1 and FIRING_OR_RELOADING_A -> ATKF2 on the archer
	## (gondorarcher.ini:236-288).
	##
	## Derived on demand from the authoritative combat schedule — nothing here is
	## stored on the entity row. A member with no resolvable weapon slot, or a
	## battalion that is not attacking, yields the weapon-set conditions only;
	## `weapon_condition_deferred_reasons` says why.
	var _sim = sim
	var out: Array = []
	if not _sim.entities.has(entity_id):
		return out
	var row = _sim.entities[entity_id] as Dictionary
	var member_health_values: Array = row.get("member_health", [])
	var start_ticks: Array = row.get("member_attack_start_ticks", [])
	var hit_ticks: Array = row.get("member_attack_hit_ticks", [])
	var member_modes: Array = row.get("member_weapon_modes", [])
	var modes := row.get("weapon_modes", {}) as Dictionary
	var attacking := String(row.get("state", "")) == "attack"
	var set_tokens := _live_weapon_set_condition_tokens(row)
	var marks := _sim._member_fire_ticks.get(entity_id, {}) as Dictionary
	for member_index in range(member_health_values.size()):
		var tokens: Array = []
		if int(member_health_values[member_index]) <= 0:
			out.append(tokens)
			continue
		for token in set_tokens:
			tokens.append(token)
		if not attacking:
			out.append(tokens)
			continue
		var mode_key := String(row.get("active_weapon_mode", ""))
		if member_index < member_modes.size():
			# The mode this member's in-flight shot was scheduled with, which is
			# what its slot letter must name.
			mode_key = String(member_modes[member_index])
		var mode := modes.get(mode_key, {}) as Dictionary
		var letter := String(WEAPON_SLOT_CONDITION_LETTERS.get(String(mode.get("weapon_slot", "")), ""))
		if letter == "":
			out.append(tokens)
			continue
		var start := int(start_ticks[member_index]) if member_index < start_ticks.size() else -1
		var hit := int(hit_ticks[member_index]) if member_index < hit_ticks.size() else -1
		var firing_ticks := maxi(0, int(mode.get("firing_duration_ticks", row.get("firing_duration_ticks", 0))))
		var fire_tick := int(marks.get(member_index, -1))
		var since_release = _sim.tick_index - fire_tick if fire_tick >= 0 else -1
		# The swing has begun (its start tick was consumed) and the release tick
		# is still ahead: PreAttackDelay is running for this member.
		var preattack = start < 0 and hit > _sim.tick_index
		var firing = since_release >= 0 and since_release < firing_ticks
		# The third segment of the sim's cadence (windup + FiringDuration +
		# DelayBetweenShots-or-ClipReloadTime): released, done firing, waiting for
		# the next swing. Retail folds it into FIRING_OR_RELOADING.
		var reloading = since_release >= firing_ticks and fire_tick >= 0 and not preattack
		if preattack:
			tokens.append("PREATTACK_%s" % letter)
		if firing:
			tokens.append("FIRING_%s" % letter)
		if preattack or firing:
			tokens.append("FIRING_OR_PREATTACK_%s" % letter)
		if firing or reloading:
			tokens.append("FIRING_OR_RELOADING_%s" % letter)
		out.append(tokens)
	return out


func weapon_condition_deferred_reasons(entity_id: int) -> Array:
	## Why a weapon-cycle model condition is NOT being raised. Fail-loud
	## companion to `member_weapon_condition_tokens`: a consumer that sees no
	## PREATTACK_A must be able to tell "not winding up" from "this unit's data
	## cannot name a slot".
	var _sim = sim
	var out: Array = []
	if not _sim.entities.has(entity_id):
		return ["entity-missing"]
	var row = _sim.entities[entity_id] as Dictionary
	var active := String(row.get("active_weapon_mode", ""))
	var mode := (row.get("weapon_modes", {}) as Dictionary).get(active, {}) as Dictionary
	if not WEAPON_SLOT_CONDITION_LETTERS.has(String(mode.get("weapon_slot", ""))):
		# No authored WeaponSlot means no letter, and a guessed PRIMARY would be
		# an invented animation state.
		out.append("weapon-slot-unauthored:%s" % active)
	if maxi(0, int(mode.get("firing_duration_ticks", row.get("firing_duration_ticks", 0)))) <= 0:
		out.append("firing-duration-zero:%s" % active)
	if (
		active != ""
		and active != String(row.get("default_weapon_mode", ""))
		and active != String(row.get("close_weapon_mode", ""))
		and not LIVE_WEAPON_SET_CONDITION_MODES.has(active)
	):
		out.append("weapon-set-condition-unmapped:%s" % active)
	# Structural, not per-unit: `_apply_weapon_mode` installs a set in one tick
	# and clears the member schedule, so there is no swap-in-progress span for
	# SWAPPING_TO_WEAPONSET_* to describe.
	out.append("swapping-to-weaponset-not-modelled")
	# PRIMARY/SECONDARY/TERTIARY is the whole retail slot corpus.
	out.append("weapon-slot-d-absent-from-retail-corpus")
	return out


func _live_weapon_set_condition_tokens(row: Dictionary) -> Array:
	var out: Array = []
	var active := String(row.get("active_weapon_mode", ""))
	var close := String(row.get("close_weapon_mode", ""))
	if active != "" and active == close:
		# The close profile is built from the WeaponSet conditioned on
		# CLOSE_RANGE (retail_vertical_slice._retail_unit_rule), whatever the
		# rule chose to key it under.
		out.append("CLOSE_RANGE")
	elif LIVE_WEAPON_SET_CONDITION_MODES.has(active):
		out.append(String(LIVE_WEAPON_SET_CONDITION_MODES[active]))
	for flag_value in row.get("weapon_set_flags", []) as Array:
		var flag := String(flag_value).to_upper()
		if flag != "" and not out.has(flag):
			out.append(flag)
	return out


func _mark_member_release(attacker_id: int, member_index: int) -> void:
	var _sim = sim
	var marks := _sim._member_fire_ticks.get(attacker_id, {}) as Dictionary
	marks[member_index] = _sim.tick_index
	_sim._member_fire_ticks[attacker_id] = marks
	if _sim._member_fire_ticks.size() > _sim.entities.size():
		# Entities are removed from a dozen places with no shared hook, so the
		# observation table is pruned here instead. Self-limiting: after one pass
		# it cannot exceed the live entity count again until another id dies.
		for key in _sim._member_fire_ticks.keys():
			if not _sim.entities.has(int(key)):
				_sim._member_fire_ticks.erase(key)


func _ensure_member_target_assignments(attacker: Dictionary, target: Dictionary) -> void:
	var attacker_health: Array = attacker.get("member_health", [])
	var target_health: Array = target.get("member_health", [])
	var assignments: Array = attacker.get("member_target_indices", [])
	if assignments.size() != attacker_health.size() or target_health.is_empty():
		return
	var use_counts: Array[int] = []
	use_counts.resize(target_health.size())
	use_counts.fill(0)
	for member_index in range(assignments.size()):
		var candidate := int(assignments[member_index])
		if int(attacker_health[member_index]) <= 0 or candidate < 0 or candidate >= target_health.size() or int(target_health[candidate]) <= 0:
			assignments[member_index] = -1
		else:
			use_counts[candidate] += 1
	for member_index in range(assignments.size()):
		if int(attacker_health[member_index]) <= 0 or int(assignments[member_index]) >= 0:
			continue
		var attacker_position := _member_world_position(attacker, member_index)
		var best_index := -1
		var best_score := INF
		for target_index in range(target_health.size()):
			if int(target_health[target_index]) <= 0:
				continue
			var target_position := _member_world_position(target, target_index)
			var score := float(use_counts[target_index]) * 10000.0 + attacker_position.distance_squared_to(target_position)
			if score < best_score:
				best_score = score
				best_index = target_index
		if best_index >= 0:
			assignments[member_index] = best_index
			use_counts[best_index] += 1
	attacker["member_target_indices"] = assignments


func _member_world_position(row: Dictionary, member_index: int) -> Vector2:
	var origin := Vector2(row.get("position", Vector2.ZERO))
	var positions: Array = row.get("formation_positions", [])
	if member_index < 0 or member_index >= positions.size() or typeof(positions[member_index]) != TYPE_VECTOR3:
		return origin
	var slot: Vector3 = positions[member_index]
	var local := Vector2(slot.x, slot.z)
	var facing := Vector2(row.get("facing", Vector2.RIGHT))
	if facing.length_squared() <= 0.000001:
		return origin + local
	return origin + local.rotated(facing.angle())


func _living_member_count(row: Dictionary) -> int:
	var result := 0
	for health_value in Array(row.get("member_health", [])):
		if int(health_value) > 0:
			result += 1
	return result


# Movement blocking hugs the placement footprints (STRUCTURE_PLACEMENT_RADII
# + a step of walkway). Oversized rings strand builders parked at a finished
# site inside the "wall" of the building they just raised and block the
# approach to tightly-packed neighbor sites.


## Source-object-id -> authored footprint radius in retail SOURCE units. Pack
## documents never change inside a match, so this is a pure memo of a read-only
## lookup: identical on every lockstep peer, order-independent, never part of the
## hashed state.
## Structure id -> resolved footprint radius in SIM units. Same memo contract as
## the table above (pure function of read-only inputs, never hashed), one level
## further down so the per-tick attack path allocates no key string at all.
## Cleared wherever the structure table is replaced wholesale.
## Source object ids whose missing geometry has already been reported, so the
## fallback warning fires once per id instead of once per call.


