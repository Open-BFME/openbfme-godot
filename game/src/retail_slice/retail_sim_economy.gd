extends RefCounted
## Economy/income subsystem extracted from retail_slice_sim.gd (Q81
## strangler-fig extraction #2). Pure code move: authoritative state
## (team_resources, per-structure auto_deposit_* rows) stays on the sim;
## this class is stateless logic through the sim back-reference, and the
## sim keeps one-line delegates under the original names so call sites and
## tick order are byte-identical.
##
## Scope: the authoritative economy step (AutoDepositUpdate cadence + legacy
## farm timer), auto-deposit binding/capture bonus, income upgrade bonuses,
## AI difficulty handicap, scavenger kill bounty. NOT here: command points
## (production-entangled), spellbook economy powers, build/refund costs.

# Weak back-reference: a strong ref would form a RefCounted cycle with the
# sim (which holds this subsystem), leaking freed sims as zombies — the
# script_wiring orphan-refusal contracts catch exactly that. The getter
# keeps plain `sim.` syntax working everywhere below.
var _sim_ref: WeakRef
var sim:
	get:
		return _sim_ref.get_ref()


func _init(owning_sim) -> void:
	_sim_ref = weakref(owning_sim)


func step_economy() -> void:
	# AutoDepositUpdate shares the authoritative economy step but retains each
	# module's own source-authored cadence. This is not the legacy farm timer:
	# descriptor-backed structures carry their next-payment frame in hashed
	# structure state, just as the SAGE module xfers m_depositOnFrame.
	step_auto_deposit_updates()

	var legacy_interval := maxi(1, int(sim._rules.get("farm_payout_ticks", 50)))
	var legacy_payout_due: bool = (sim.tick_index % legacy_interval == 0)

	for id in sim.structure_ids():
		var row: Dictionary = sim.structures[id]
		var income := int(row.get("income_per_payout", 0))
		if income <= 0 or int(row.get("health", 0)) <= 0 or float(row.get("construction_progress", 0.0)) < 1.0:
			continue

		# Determine if this structure is due for payout
		var payout_due := false
		if row.has("income_interval_ticks") and row.has("next_income_tick"):
			# Descriptor-authored per-structure cadence (retail IncomeInterval)
			var next_payout: int = int(row.get("next_income_tick", -1))
			if sim.tick_index >= next_payout:
				var interval_ticks: int = int(row.get("income_interval_ticks", 1))
				row["next_income_tick"] = sim.tick_index + interval_ticks
				payout_due = true
		else:
			# Legacy global farm_payout_ticks cadence
			payout_due = legacy_payout_due

		if not payout_due:
			continue

		var team := int(row.get("team", -1))
		income = income_with_upgrade_bonus(team, row, income)
		income = ai_resource_handicap(team, income)
		sim.team_resources[team] = sim.resources_for_team(team) + income
		sim._emit_event("economy.payout", id, 0, {"team": team, "amount": income})


func initialize_structure_auto_deposit(structure: Dictionary) -> void:
	var team := int(structure.get("team", -1))
	var kind := String(structure.get("structure_kind", ""))
	var descriptor_team := team
	if structure.has("auto_deposit_descriptor_team"):
		descriptor_team = int(structure.get("auto_deposit_descriptor_team", -1))
	elif team == sim.NEUTRAL_TEAM:
		# A map-authored neutral object has no controlling-player manifest from
		# which module data can be reconstructed. Bind only when every roster
		# manifest that defines this kind agrees on the exact authored rules;
		# cross-faction ambiguity must be resolved by the map/loader setting the
		# immutable auto_deposit_descriptor_team contract before creation.
		var resolved_rules: Array = []
		var resolved_team := -1
		for candidate_team in sim._roster_team_ids():
			var candidate: Array = (
				sim.structure_auto_deposit_updates_for_team(candidate_team)
				.get(kind, []) as Array
			)
			if candidate.is_empty():
				continue
			if resolved_team < 0:
				resolved_team = candidate_team
				resolved_rules = candidate
			elif candidate != resolved_rules:
				sim.configuration_error = (
					"neutral structure %d AutoDepositUpdate descriptor is ambiguous"
					% int(structure.get("id", 0))
				)
				structure.erase("auto_deposit_rules")
				structure.erase("auto_deposit_state")
				return
		descriptor_team = resolved_team
	if descriptor_team >= 0 and not sim._roster_team_ids().has(descriptor_team):
		sim.configuration_error = (
			"structure %d AutoDepositUpdate descriptor team %d is not rostered"
			% [int(structure.get("id", 0)), descriptor_team]
		)
		structure.erase("auto_deposit_rules")
		structure.erase("auto_deposit_state")
		return
	var rules: Array = []
	if descriptor_team >= 0:
		rules = (
			sim.structure_auto_deposit_updates_for_team(descriptor_team)
				.get(kind, []) as Array
		)
		# Fortress composite pieces: retail authors the keep's income on the
		# piece object (fortress.ini:983 MenFortressCitadel AutoDepositUpdate
		# GENERIC_KEEP_MONEY_AMOUNT 25 / GENERIC_KEEP_MONEY_TIME 6000 ms), and
		# the manifest files those rows under deferred_structure_auto_deposit_
		# updates[object_id] because the piece is engine-spawned, not built.
		# The piece IS a live castle_piece row now, so its authored rows bind
		# here. Owner 2026-08-19: fortresses never earned passive income.
		if rules.is_empty():
			var deferred: Dictionary = sim.team_manifest_for(descriptor_team).get(
				"deferred_structure_auto_deposit_updates", {}
			) as Dictionary
			# The manifest keys composite pieces by the retail source object
			# name (MenFortressCitadel); the row carries both spellings.
			for key in [String(structure.get("source_object_id", "")), String(structure.get("object_id", ""))]:
				if key == "" or not deferred.has(key):
					continue
				var executable: Array = []
				for row_value in deferred.get(key, []) as Array:
					if String((row_value as Dictionary).get("runtimeStatus", "")) == "executable":
						executable.append(row_value)
				if not executable.is_empty():
					rules = executable
					break
	if rules.is_empty():
		structure.erase("auto_deposit_rules")
		structure.erase("auto_deposit_state")
		return
	structure["auto_deposit_descriptor_team"] = descriptor_team
	structure["auto_deposit_rules"] = rules.duplicate(true)
	# The historical tiny-slice farm timer is a fallback for structures with no
	# executable descriptor. Once real AutoDeposit module data is bound, leaving
	# this non-zero would pay both clocks for the same structure.
	structure["income_per_payout"] = 0
	var states: Array[Dictionary] = []
	for rule_value in structure["auto_deposit_rules"] as Array:
		var rule := rule_value as Dictionary
		var timing := rule.get("depositTiming", {}) as Dictionary
		var interval := maxi(1, int(timing.get("simulationTicks", 0)))
		states.append(
			{
				"next_tick": sim.tick_index + interval,
				"initialized": false,
				"capture_bonus_armed": false,
			}
		)
	structure["auto_deposit_state"] = states


func auto_deposit_upgrade_boost(team: int, rule: Dictionary) -> int:
	var completed: Dictionary = sim.team_upgrades.get(team, {}) as Dictionary
	# The retail module returns the first authored matching pair, not a sum.
	for boost_value in rule.get("upgradedBoosts", []) as Array:
		var boost := boost_value as Dictionary
		if String(boost.get("upgradeType", "")) != "PLAYER":
			sim.configuration_error = (
				"AutoDepositUpdate boost '%s' is not a PLAYER upgrade"
				% String(boost.get("upgradeId", ""))
			)
			return 0
		if completed.has(String(boost.get("upgradeId", ""))):
			return int(boost.get("boost", 0))
	return 0


func step_auto_deposit_updates() -> void:
	for structure_id in sim.structure_ids():
		var structure: Dictionary = sim.structures[structure_id]
		var team := int(structure.get("team", -1))
		var rules: Array = structure.get("auto_deposit_rules", []) as Array
		var states_value: Variant = structure.get("auto_deposit_state")
		if rules.is_empty() or typeof(states_value) != TYPE_ARRAY:
			continue
		var states := states_value as Array
		if states.size() != rules.size():
			sim.configuration_error = (
				"structure %d AutoDepositUpdate state/rule count drifted"
				% structure_id
			)
			continue
		for index in rules.size():
			var rule := rules[index] as Dictionary
			var state := states[index] as Dictionary
			if sim.tick_index < int(state.get("next_tick", sim.tick_index + 1)):
				continue
			var interval := maxi(
				1,
				int(
					(rule.get("depositTiming", {}) as Dictionary)
					.get("simulationTicks", 0)
				)
			)
			state["next_tick"] = sim.tick_index + interval
			if not bool(state.get("initialized", false)):
				state["initialized"] = true
				state["capture_bonus_armed"] = true
			var amount := int(
				(rule.get("depositAmount", {}) as Dictionary).get("value", 0)
			)
			if (
				not sim.team_resources.has(team)
				or amount <= 0
				or int(structure.get("health", 0)) <= 0
				or float(structure.get("construction_progress", 0.0)) < 1.0
				or not bool(
					(rule.get("actualMoney", {}) as Dictionary)
					.get("value", true)
				)
			):
				continue
			amount += auto_deposit_upgrade_boost(team, rule)
			sim.team_resources[team] = sim.resources_for_team(team) + amount
			sim._emit_event(
				"economy.auto_deposit",
				structure_id,
				0,
				{"team": team, "amount": amount, "module_index": index}
			)


func award_auto_deposit_capture(
	structure: Dictionary, new_team: int
) -> int:
	var rules: Array = structure.get("auto_deposit_rules", []) as Array
	var states_value: Variant = structure.get("auto_deposit_state")
	if rules.is_empty() or typeof(states_value) != TYPE_ARRAY:
		return 0
	var states := states_value as Array
	if states.size() != rules.size():
		sim.configuration_error = "captured AutoDepositUpdate state/rule count drifted"
		return 0
	var awarded := 0
	for index in rules.size():
		var rule := rules[index] as Dictionary
		var state := states[index] as Dictionary
		var interval := maxi(
			1,
			int(
				(rule.get("depositTiming", {}) as Dictionary)
				.get("simulationTicks", 0)
			)
		)
		# SAGE awardInitialCaptureBonus always restarts the regular cadence,
		# even when the one-shot is not armed or its amount is zero.
		state["next_tick"] = sim.tick_index + interval
		var amount := int(
			(rule.get("initialCaptureBonus", {}) as Dictionary).get("value", 0)
		)
		if (
			not bool(state.get("capture_bonus_armed", false))
			or amount <= 0
			or not sim.team_resources.has(new_team)
		):
			continue
		state["capture_bonus_armed"] = false
		sim.team_resources[new_team] = sim.resources_for_team(new_team) + amount
		awarded += amount
		sim._emit_event(
			"economy.auto_deposit_capture_bonus",
			int(structure.get("id", 0)),
			0,
			{"team": new_team, "amount": amount, "module_index": index}
		)
	return awarded


func ai_resource_handicap(team: int, income: int) -> int:
	## Per-difficulty economy handicap. Non-AI teams and any tier at the neutral
	## 1000 permille (the legacy/default "medium") return the income untouched, so
	## the default match's resource curve — and the pinned signature — never move.
	## Higher tiers gain a deterministic integer bonus, lower tiers a penalty.
	if not sim._team_ai_state.has(team):
		return income
	var permille := int(sim._difficulty_profile(team).get("resource_permille", 1000))
	if permille == 1000:
		return income
	return int(income * permille / 1000)


func income_with_upgrade_bonus(team: int, building: Dictionary, base_income: int) -> int:
	## Authored TerrainResourceBehavior upgrade bonuses (Grand Harvest): the
	## resource structure's own document declares the percent while the team
	## owns the technology and maintains the required building.
	var bundle: Dictionary = sim.structure_upgrade_effects_for_team(team).get(String(building.get("structure_kind", "")), {})
	var owned: Dictionary = sim.team_upgrades.get(team, {}) as Dictionary
	var income := base_income
	for effect_value in Array(bundle.get("effects", [])):
		var effect := effect_value as Dictionary
		if String(effect.get("kind", "")) != "income-bonus":
			continue
		if not owned.has(String(effect.get("upgrade_id", ""))):
			continue
		if not sim._team_has_required_object(team, String(effect.get("upgrade_must_be_present", ""))):
			continue
		income = roundi(income * float(effect.get("bonus_percent", 100.0)) / 100.0)
	return income


func award_scavenger_bounty(attacker_id: int, victim: Dictionary, victim_kind: String) -> int:
	# This hook is called only at a lethal transition. It still proves ownership
	# and hostility locally so script/self/friendly deaths can never mint money.
	if not sim.entities.has(attacker_id):
		return 0
	var attacker := sim.entities[attacker_id] as Dictionary
	if int(attacker.get("health", 0)) <= 0:
		return 0
	var killer_team := int(attacker.get("team", -1))
	if not sim._is_combatant_team(killer_team) or not sim._is_hostile(killer_team, int(victim.get("team", -1))):
		return 0
	var percent := float(sim._scavenger_bounty_percent.get(killer_team, 0.0))
	if percent <= 0.0:
		return 0
	var authored_value := -1
	if victim_kind == "structure":
		authored_value = int(sim.structure_bounty_values_for_team(int(victim.get("team", -1))).get(String(victim.get("structure_kind", "")), -1))
	elif victim.has("bounty_value"):
		authored_value = int(victim.get("bounty_value", -1))
	if authored_value < 0:
		return 0
	var award := maxi(0, floori(float(authored_value) * percent))
	if award <= 0:
		return 0
	sim.team_resources[killer_team] = sim.resources_for_team(killer_team) + award
	sim._emit_event("economy.scavenger_bounty", attacker_id, int(victim.get("id", 0)), {
		"team": killer_team,
		"victim_kind": victim_kind,
		"bounty_value": authored_value,
		"bounty_percent": percent,
		"amount": award,
	})
	return award
