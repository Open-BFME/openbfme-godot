extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Victory resolution carved out of retail_slice_sim.gd (drawer 19): team-defeat detection, surviving teams, CAH match awards, victory/defeat events.
## State stays on the sim; the sim keeps one-line delegates under the original names.



func _team_defeated(team: int) -> bool:
	## A team is eliminated when its fortress is razed (base loop) or it has no
	## living battalions (non-base). Exactly the old per-team fortress/liveness
	## test: a team that never held a fortress (harness sims with none seeded) is
	## NOT counted as defeated in base-loop mode, so those sims never spuriously
	## resolve — the pinned battle, whose fortresses persist at 0 health when
	## razed, still resolves the instant one falls.
	if sim.base_loop_enabled:
		var fortress = sim.fortress_id(team)
		return fortress != 0 and int((sim.structures[fortress] as Dictionary).get("health", 0)) <= 0
	return sim.living_ids(team).is_empty()


func _surviving_teams() -> Array:
	## Roster teams that are not yet eliminated, in roster order.
	var survivors: Array = []
	for team in sim._roster_team_ids():
		if not _team_defeated(team):
			survivors.append(team)
	return survivors


func _evaluate_cah_match_awards() -> void:
	var unit_types: Array = sim._cah_award_contracts.keys()
	unit_types.sort_custom(func(a, b): return String(a) < String(b))
	for unit_type_value in unit_types:
		var unit_type := String(unit_type_value)
		if sim.cah_award_results.has(unit_type):
			continue
		var contract = sim._cah_award_contracts[unit_type] as Dictionary
		var tally = sim._cah_tally_for(unit_type)
		var owner_team = int(contract.get("ownerTeam", sim._created_hero_owner_teams.get(unit_type, -1)))
		var mode_suffix = "OPENPLAY_MP" if sim._cah_openplay_multiplayer() else "SKIRMISH"
		var result_stat = "HERO_%s_COUNT_%s" % ["VICTORY" if owner_team == sim.winner else "DEFEAT", mode_suffix]
		tally[result_stat] = int(tally.get(result_stat, 0)) + 1
		var eligible: Dictionary = {}
		for award_value in contract.get("eligibleAwards", []) as Array:
			eligible[String(award_value)] = true
		var awards: Array = (contract.get("ownedAwards", []) as Array).duplicate()
		var new_awards: Array = []
		for definition_value in contract.get("awardDefinitions", []) as Array:
			var definition := definition_value as Dictionary
			var award_id := String(definition.get("awardId", ""))
			if award_id == "" or not eligible.has(award_id) or awards.has(award_id):
				continue
			var earned := true
			for trigger_value in definition.get("triggers", []) as Array:
				var trigger := trigger_value as Dictionary
				var total := 0
				for stat_value in trigger.get("statIds", []) as Array:
					total += int(tally.get(String(stat_value), 0))
				if total < int(trigger.get("threshold", 0)):
					earned = false
					break
			if earned:
				awards.append(award_id)
				new_awards.append(award_id)
		var result := {
			"heroId": String(contract.get("heroId", unit_type.trim_prefix("CreateAHero__"))),
			"objectId": unit_type,
			"ownerTeam": owner_team,
			"trackingStats": tally.duplicate(true),
			"awards": awards,
			"newAwards": new_awards,
		}
		sim.cah_award_results[unit_type] = result
		sim._emit_event("cah.awards_resolved", 0, 0, result)


func _resolve_victory() -> void:
	## Last-alliance-standing over the whole roster. The match ends when no two
	## surviving teams are mutually hostile (a single alliance, or one team, or
	## none remain). `sim.winner` stays a single int for snapshot compat: the LOWEST
	## surviving team id (documented tie-break); with no survivors it is the
	## lowest rostered team so a mutual wipe still terminates deterministically.
	## For the historical {0,1} roster this reduces to the old team0-vs-team1
	## comparison and emits the identical observer-frame events, so the pinned
	## battle signature does not move.
	# A delayed FireWeaponWhenDeadBehavior is part of the lethal callback that
	# produced it. Do not freeze the match before that authored consequence has
	# fired; otherwise killing the last carrier would strand the schedule forever
	# behind tick()'s sim.winner gate.
	for effect in sim._pending_power_effects:
		if String(effect.get("kind", "")) == "death_weapon":
			return
	# A thrown/knocked-back body must finish its deterministic impact/recovery
	# consequence before the sim.winner gate freezes gameplay. Recovered bodies are
	# inert evidence and do not hold the match open.
	for physics_value in sim.physics_objects.values():
		if String((physics_value as Dictionary).get("phase", "")) != "recovered":
			return
	for entity_value in sim.entities.values():
		var lifetime := (entity_value as Dictionary).get("lifetime_update", {}) as Dictionary
		if int(lifetime.get("expire_tick", -1)) > sim.tick_index:
			return
	for structure_value in sim.structures.values():
		var structure := structure_value as Dictionary
		var lifetime := structure.get("lifetime_update", {}) as Dictionary
		if int(lifetime.get("expire_tick", -1)) > sim.tick_index:
			return
		if String(structure.get("ship_death_phase", "")) == "sinking":
			return
	var survivors := _surviving_teams()
	for i in survivors.size():
		for j in range(i + 1, survivors.size()):
			if sim._is_hostile(int(survivors[i]), int(survivors[j])):
				return
	if survivors.is_empty():
		var roster = sim._roster_team_ids()
		sim.winner = int(roster[0]) if not roster.is_empty() else sim.PLAYER_TEAM
	else:
		sim.winner = int(survivors[0])
	_evaluate_cah_match_awards()
	for id in sim.living_ids(sim.winner):
		var row: Dictionary = sim.entities[id]
		row["target_id"] = 0
		sim._clear_pending_route(row, true)
		row["state"] = "victory"
	# EVA/music are framed off the sim's local observer (sim.PLAYER_TEAM): victorious
	# when the observer's alliance is the surviving one, defeated otherwise. A
	# true per-observer HUD frame is a HUD-packet concern; the sim carries the
	# single-observer frame the default path has always emitted.
	var observer_won = sim.winner != -1 and not sim._is_hostile(sim.PLAYER_TEAM, sim.winner)
	if observer_won:
		sim._emit_event("match.victory", 0, 0)
		sim._emit_event("eva.enemy_defeated", 0, 0, {"team": sim.PLAYER_TEAM})
		sim._emit_music("victory")
	else:
		sim._emit_event("match.defeat", 0, 0)
		sim._emit_event("eva.ally_defeated", 0, 0, {"team": sim.PLAYER_TEAM})
		sim._emit_music("defeat")




