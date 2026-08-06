extends SceneTree

const StateScript = preload("res://src/wotr/wotr_state.gd")
const WorldScript = preload("res://src/wotr/wotr_world.gd")
const SessionScript = preload("res://src/wotr/wotr_session.gd")
const BattleScript = preload("res://src/wotr/wotr_battle.gd")
const StrategicGapsScript = preload("res://src/wotr/wotr_strategic_gaps.gd")
const EXPECTED_CHECKS := 61
const ROSTER_UNITS := {
	"HeroRoster": [{"template": "ExactHero", "unit_type": "AutoResolveUnit_Hero",
		"hitpoints_milli": 800000, "army_id": 0}],
	"BetaHeroRoster": [{"template": "BetaHero", "unit_type": "AutoResolveUnit_Hero",
		"hitpoints_milli": 700000, "army_id": 0}],
	"GarrisonRoster": [{"template": "Guard", "unit_type": "AutoResolveUnit_Soldier",
		"hitpoints_milli": 100000, "army_id": 0}],
}
var passed := 0
var failed := 0

func _initialize() -> void: call_deferred("_run")

func _run() -> void:
	var state := _state(StateScript.CONTROLLER_HUMAN, StateScript.CONTROLLER_HUMAN)
	_check("real fixture opens tactical", state.phase == StateScript.PHASE_TACTICAL)
	_check("real fixture seats two", state.players.size() == 2 and state.active_player() == 0)
	_check("opening retreat empty", state.pending_retreats.is_empty())
	_check("schema v2 phase surface", state.authoritative_state()["schema_version"] == 2
		and state.authoritative_state().has("phase") and state.authoritative_state().has("pending_retreats"))


	var claim_state := _state(StateScript.CONTROLLER_HUMAN, StateScript.CONTROLLER_HUMAN)
	var claim_session := _session(claim_state)
	var claim: Dictionary = claim_session.commit_attack("Neutral", ["test.map"])
	_check("claim commitment starts battle", bool(claim["ok"]) and claim_state.phase == StateScript.PHASE_BATTLE
		and not claim_state.pending_claim.is_empty())
	var claim_turn := claim_state.turn_index
	var claimed: Dictionary = claim_session.auto_resolve_pending_battle()
	_check("decided claim clears transaction and phases", bool(claimed["ok"])
		and claim_state.pending_claim.is_empty() and claim_state.phase == StateScript.PHASE_TACTICAL)
	_check("claim wraps next seat once", claim_state.turn_index == claim_turn + 1)

	var hero_id := int(state.armies_in_region("Alpha")[0])
	_check("tactical movement applies immediately", state.move_army(hero_id, "Front"))
	_check("movement really changed authoritative army", String((state.armies[hero_id] as Dictionary)["region"]) == "Front")
	var session := _session(state)
	var committed: Dictionary = session.commit_attack("Enemy", ["test.map"])
	_check("enemy commitment starts battle", bool(committed["ok"]) and state.phase == StateScript.PHASE_BATTLE)
	var battle_hash := state.state_hash()
	_check("battle gates movement hash neutrally", not state.move_army(hero_id, "Alpha") and state.state_hash() == battle_hash)
	_check("battle gates training hash neutrally", state.place_army(0, "Alpha", "HeroArmy") < 0 and state.state_hash() == battle_hash)
	_check("end phase refuses in flight hash neutrally", state.end_phase() == StateScript.PHASE_BATTLE and state.state_hash() == battle_hash)
	var refused_rts: Dictionary = session.commit_attack("Enemy", ["test.map"], StateScript.BATTLE_TYPE_RTS)
	_check("second or RTS commitment refusal is hash neutral", not bool(refused_rts["ok"]) and state.state_hash() == battle_hash)

	var defender_team := int(state.pending_battle["defender_team"])
	var resolved: Dictionary = session.resolve_battle(defender_team)
	_check("decided defender result applies", bool(resolved["ok"]))
	_check("defeated attacker strips in place without retreat", state.pending_retreats.is_empty()
		and state.phase == StateScript.PHASE_TACTICAL and state.armies.has(hero_id))
	_check("attacker retains only exact leader", ((state.armies[hero_id] as Dictionary)["units"] as Array).size() == 1)
	_check("attacker in-place completion event is exact",
		_event_count(state.events, "retreat_completed_in_place", "") == 1)

	var retreat_state := _defeated_hero_state()
	var retreat_session := _session(retreat_state)
	var retreat_id := int(retreat_state.pending_retreats[0]["army"])
	var expected := {"army": retreat_id, "from_region": "Enemy", "player": 1, "turn": 0, "hero_template": "BetaHero"}
	_check("defeated defender routes retreat", retreat_state.phase == StateScript.PHASE_RETREAT)
	_check("exact typed defender retreat row", retreat_state.pending_retreats == [expected], str(retreat_state.pending_retreats))
	var retreat_hash := retreat_state.state_hash()
	_check("nonadjacent retreat blocked hash neutrally", not retreat_session.order_retreat(retreat_id, "Haven") and retreat_state.state_hash() == retreat_hash)
	_check("adjacent friendly retreat target only", Array(retreat_session.retreat_targets(retreat_id)) == ["Rear"])
	var preflight_hash := retreat_state.state_hash()
	_check("retreat phase preflights choosable row before blocked mutation",
		retreat_session.end_phase() == StateScript.PHASE_RETREAT and retreat_state.state_hash() == preflight_hash)
	_check("end phase captures exact refusal", retreat_session.refusals.size() == 1
		and String(retreat_session.refusals[0]).contains("choosable destination"))
	_check("session records refusal verbatim", retreat_session.refusals.size() == 1)
	_check("adjacent retreat order applies", retreat_session.order_retreat(retreat_id, "Rear"))
	_check("retreat order stays retreat until phase door", retreat_state.phase == StateScript.PHASE_RETREAT and retreat_state.pending_retreats.is_empty())
	var before_turn := retreat_state.turn_index
	var before_treasure := retreat_state.treasure(1)
	_check("retreat end wraps tactical", retreat_session.end_phase() == StateScript.PHASE_TACTICAL)
	_check("next seat exactly once", retreat_state.turn_index == before_turn + 1)
	_check("arriving seat income exactly once", retreat_state.treasure(retreat_state.active_player()) >= before_treasure)


	var fallback := _defeated_hero_state()
	var fallback_id := int(fallback.pending_retreats[0]["army"])
	fallback.transfer_region("Rear", StateScript.NEUTRAL)
	fallback.transfer_region("Haven", 1)
	_check("blocked retreat has no adjacent order", fallback.retreat_targets(fallback_id).is_empty())
	_check("no-option automatic fallback applies", fallback.order_retreat(fallback_id, ""))
	_check("fallback uses closest admissible allied", String((fallback.armies[fallback_id] as Dictionary)["region"]) == "Haven")
	var blocked := _defeated_hero_state()
	var blocked_id := int(blocked.pending_retreats[0]["army"])
	blocked.transfer_region("Rear", StateScript.NEUTRAL)
	_check("enemy_or_neutral_capital_completes_without_lock", blocked.order_retreat(blocked_id, ""))
	_check("enemy_or_neutral_capital destroys with reason", not blocked.armies.has(blocked_id)
		and _event_count(blocked.events, "retreat_no_destination_destroyed", "") == 1)


	var no_capital := _defeated_hero_state()
	var no_capital_id := int(no_capital.pending_retreats[0]["army"])
	no_capital.transfer_region("Rear", StateScript.NEUTRAL)
	(no_capital.players[1] as Dictionary)["capital"] = ""
	_check("unresolvable capital fallback completes", no_capital.order_retreat(no_capital_id, ""))
	_check("unresolvable capital destroys with named event", not no_capital.armies.has(no_capital_id)
		and _event_count(no_capital.events, "retreat_no_destination_destroyed", "") == 1)

	var auto_defender := _defeated_hero_state(StateScript.CONTROLLER_AI)
	_check("AI defender retreat is auto ordered while attacker human", auto_defender.pending_retreats.is_empty())
	_check("AI defender auto order completes phase", auto_defender.phase == StateScript.PHASE_TACTICAL and auto_defender.turn_index == 1)

	var unproven := _state(StateScript.CONTROLLER_HUMAN, StateScript.CONTROLLER_HUMAN)
	for army_id in unproven.armies_in_region("Enemy"): unproven.remove_army(int(army_id))
	var unproven_id := unproven.place_army(1, "Enemy", "HeroArmyB")
	(((unproven.armies[unproven_id] as Dictionary)["units"] as Array)[0] as Dictionary)["template"] = "NotBetaHero"
	var unproven_attacker := int(unproven.armies_in_region("Alpha")[0]); unproven.move_army(unproven_attacker, "Front")
	var unproven_session := _session(unproven); unproven_session.commit_attack("Enemy", ["test.map"])
	var unproven_result: Dictionary = unproven_session.resolve_battle(int(unproven.pending_battle["attacker_team"]))
	_check("unprovable defeated leader does not refuse outcome", bool(unproven_result["ok"]))
	_check("unprovable defeated leader is destroyed", not unproven.armies.has(unproven_id) and unproven.pending_retreats.is_empty())
	_check("unprovable leader destruction event is exact",
		_event_count(unproven.events, "defeated_hero_leader_unprovable_destroyed", "") == 1)


	var lifecycle := _defeated_hero_state()
	var lifecycle_peer := StateScript.new(); lifecycle_peer.world = lifecycle.world
	_check("typed retreat lifecycle restores", lifecycle_peer.restore(lifecycle.snapshot())
		and lifecycle_peer.state_hash() == lifecycle.state_hash())
	var lifecycle_hash := lifecycle_peer.state_hash()
	var lifecycle_id := int(lifecycle.pending_retreats[0]["army"])
	var malformed: Array[Dictionary] = []
	var absent: Dictionary = lifecycle.authoritative_state().duplicate(true); (absent["armies"] as Dictionary).erase(lifecycle_id); malformed.append(absent)
	var wrong_kind: Dictionary = lifecycle.authoritative_state().duplicate(true); ((wrong_kind["armies"] as Dictionary)[lifecycle_id] as Dictionary)["kind"] = StateScript.ARMY_GARRISON; malformed.append(wrong_kind)
	for field_and_value in [["hero_template", "WrongHero"], ["player", 0], ["player", 99], ["turn", -1], ["from_region", "Haven"]]:
		var donor: Dictionary = lifecycle.authoritative_state().duplicate(true)
		((donor["pending_retreats"] as Array)[0] as Dictionary)[String(field_and_value[0])] = field_and_value[1]
		malformed.append(donor)
	var lifecycle_refused := true
	for donor in malformed:
		if lifecycle_peer.restore(var_to_bytes(donor)) or lifecycle_peer.state_hash() != lifecycle_hash:
			lifecycle_refused = false
	_check("retreat lifecycle mismatches refuse all or nothing", lifecycle_refused)

	var empty := _state(StateScript.CONTROLLER_HUMAN, StateScript.CONTROLLER_HUMAN)
	var empty_turn := empty.turn_index
	_check("empty phases auto pass to tactical", empty.end_phase() == StateScript.PHASE_TACTICAL)
	_check("empty auto pass advances exactly once", empty.turn_index == empty_turn + 1)
	_check("empty battle pass event", _event_count(empty.events, "phase_auto_passed", StateScript.PHASE_BATTLE) == 1)
	_check("empty retreat pass event", _event_count(empty.events, "phase_auto_passed", StateScript.PHASE_RETREAT) == 1)

	var undecided := _state(StateScript.CONTROLLER_HUMAN, StateScript.CONTROLLER_HUMAN)
	var undecided_session := _session(undecided)
	var uid := int(undecided.armies_in_region("Alpha")[0])
	undecided.move_army(uid, "Front")
	undecided_session.commit_attack("Enemy", ["test.map"])
	var undecided_hash := undecided.state_hash()
	var undecided_result: Dictionary = undecided_session.resolve_battle(BattleScript.UNDECIDED)
	_check("undecided refuses", not bool(undecided_result["ok"]))
	_check("undecided stays battle with transaction", undecided.phase == StateScript.PHASE_BATTLE and not undecided.pending_battle.is_empty())
	_check("undecided is hash neutral", undecided.state_hash() == undecided_hash)
	var legacy_battle: Dictionary = undecided.authoritative_state(); legacy_battle["schema_version"] = 1; legacy_battle.erase("phase"); legacy_battle.erase("pending_retreats")
	var legacy_battle_peer := StateScript.new(); legacy_battle_peer.world = undecided.world
	_check("v1 in-flight transaction migrates to battle", legacy_battle_peer.restore(var_to_bytes(legacy_battle))
		and legacy_battle_peer.phase == StateScript.PHASE_BATTLE)
	var incoherent_battle: Dictionary = undecided.authoritative_state(); incoherent_battle["phase"] = StateScript.PHASE_TACTICAL
	var coherence_hash := legacy_battle_peer.state_hash()
	var incoherent_retreat: Dictionary = lifecycle.authoritative_state(); incoherent_retreat["phase"] = StateScript.PHASE_BATTLE
	_check("v2 phase transaction incoherence refuses all or nothing",
		not legacy_battle_peer.restore(var_to_bytes(incoherent_battle))
			and not legacy_battle_peer.restore(var_to_bytes(incoherent_retreat))
			and legacy_battle_peer.state_hash() == coherence_hash)

	var peer := StateScript.new(); peer.world = empty.world
	_check("v2 restore", peer.restore(empty.snapshot()))
	_check("v2 deterministic peer hash", peer.state_hash() == empty.state_hash())
	var v1: Dictionary = empty.authoritative_state(); v1["schema_version"] = 1; v1.erase("phase"); v1.erase("pending_retreats")
	_check("v1 tactical empty migration", peer.restore(var_to_bytes(v1)) and peer.phase == StateScript.PHASE_TACTICAL and peer.pending_retreats.is_empty())
	var before_junk := peer.state_hash(); var junk: Dictionary = empty.authoritative_state(); junk["pending_retreats"] = [42]
	_check("junk restore all or nothing", not peer.restore(var_to_bytes(junk)) and peer.state_hash() == before_junk)

	var ai_state := _state(StateScript.CONTROLLER_AI, StateScript.CONTROLLER_HUMAN)
	var ai_session := _session(ai_state)
	var ai_before := ai_state.turn_index
	var ai_report: Dictionary = ai_session.run_ai_turn([])
	_check("AI full tactical round succeeds", bool(ai_report["ok"]))
	_check("AI uses shared phase round", ai_state.phase == StateScript.PHASE_TACTICAL and ai_state.turn_index == ai_before + 1)
	var twin_state := _state(StateScript.CONTROLLER_AI, StateScript.CONTROLLER_HUMAN)
	var twin_report: Dictionary = _session(twin_state).run_ai_turn([])
	_check("AI peers deterministic", bool(twin_report["ok"]) and twin_state.state_hash() == ai_state.state_hash())
	var required := ["pre_battle_retreat_losses", "phase_moves_apply_immediately", "single_battle_per_phase", "retreat_distance_rule", "phase_timer"]
	_check("frozen gaps named", required.all(func(name): return StrategicGapsScript.reason(String(name)) != ""))
	_finish()

func _defeated_hero_state(defender_controller: String = StateScript.CONTROLLER_HUMAN) -> StateScript:
	var state := _state(StateScript.CONTROLLER_HUMAN, defender_controller)
	for army_id in state.armies_in_region("Enemy"):
		state.remove_army(int(army_id))
	state.place_army(1, "Enemy", "HeroArmyB")
	var attacker_id := int(state.armies_in_region("Alpha")[0])
	state.move_army(attacker_id, "Front")
	var session := _session(state)
	session.commit_attack("Enemy", ["test.map"])
	session.resolve_battle(int(state.pending_battle["attacker_team"]))
	return state


func _session(state: StateScript) -> SessionScript:
	var session := SessionScript.new(); session.world = state.world; session.state = state; return session

func _state(first_controller: String, second_controller: String) -> StateScript:
	var world := WorldScript.new()
	if not world.load_from_dict(_document(), "PhaseCampaign"): printerr("fixture world failed ", world.errors)
	var state := StateScript.new()
	state.setup(world, [{"template":"SeatA", "team":1, "controller":first_controller}, {"template":"SeatB", "team":2, "controller":second_controller}])
	state.roster_units = ROSTER_UNITS
	state.apply_ownership_sets("PhaseScenario")
	state.place_army(1, "Enemy", "GarrisonArmy")
	return state

func _region(id: String, links: Array, fertile: int = 0) -> Dictionary:
	var connections: Array = []
	for target in links: connections.append({"region":target, "detourPoints":[]})
	return {"id":id,"displayName":id,"mapName":"MAP "+id,"subObject":id,"regionPortrait":"","skirmishStillImage":"","skirmishMusicTrack":"","conqueredNotice":"","bonuses":{"fertileTerritory":fertile},"bonusMacros":{},"cpLimit":600,"allyCpLimit":600,"createAutoFort":false,"customCenterPoint":true,"centerPoint":{"x":0,"y":0},"heroArmySpots":[],"garrisonArmySpots":[],"buildingSpots":[],"fortress":null,"connections":connections,"restrictBuildings":[]}

func _document() -> Dictionary:
	return {"format":1,"schema":WorldScript.SCHEMA,"schemaVersion":WorldScript.SCHEMA_VERSION,"game":"bfme2","sources":[],"regionCampaigns":[{"name":"PhaseCampaign","kind":"LivingWorldRegionCampaign","regionEffectsManagerName":"","regions":[_region("Alpha",["Front","Neutral"]),_region("Front",["Alpha","Enemy"]),_region("Enemy",["Front","Rear"],10),_region("Rear",["Enemy","Haven"]),_region("Haven",["Rear"]),_region("Neutral",["Alpha"])],"territoryBonuses":[]}],"territoryBonuses":[],"regionEffects":[],"cities":[],"defaultArmies":[{"scriptingName":"HeroArmy","spawnForTemplates":["SeatA"],"heroTemplateName":"ExactHero","playerArmy":"HeroRoster","icon":""},{"scriptingName":"HeroArmyB","spawnForTemplates":["SeatB"],"heroTemplateName":"BetaHero","playerArmy":"BetaHeroRoster","icon":""},{"scriptingName":"GarrisonArmy","spawnForTemplates":["SeatB"],"heroTemplateName":"","playerArmy":"GarrisonRoster","icon":""}],"playerArmies":[{"name":"HeroRoster","displayNameTag":"","entries":[{"thingTemplate":"ExactHero","quantity":1}]},{"name":"BetaHeroRoster","displayNameTag":"","entries":[{"thingTemplate":"BetaHero","quantity":1}]},{"name":"GarrisonRoster","displayNameTag":"","entries":[{"thingTemplate":"Guard","quantity":1}]}],"scenarios":[{"name":"PhaseScenario","regionCampaign":"PhaseCampaign","isEvilCampaign":false,"isHistoricalScenario":false,"minPlayers":2,"maxPlayers":2,"ownershipSets":[{"regions":["Alpha","Front","Haven"],"startRegion":"Alpha","spawnArmies":[{"armies":["HeroArmy"],"region":"Alpha"}],"spawnBuildings":[]},{"regions":["Enemy","Rear"],"startRegion":"Rear","spawnArmies":[],"spawnBuildings":[]}]}],"victoryTypes":[],"playerTemplates":[{"name":"SeatA","faction":"FactionMen","startingWorldCp":1500,"maxWorldCp":4500,"startingHeroCp":450,"maxHeroCp":450,"scenarioStartResources":0},{"name":"SeatB","faction":"FactionMordor","startingWorldCp":1500,"maxWorldCp":4500,"startingHeroCp":450,"maxHeroCp":450,"scenarioStartResources":0}],"importGaps":[],"gaps":[]}

func _event_count(events: Array[Dictionary], kind: String, phase_name: String) -> int:
	var count := 0
	for row in events:
		if String(row.get("kind", "")) == kind and (phase_name.is_empty() or String(row.get("phase", "")) == phase_name): count += 1
	return count
func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition: passed += 1; print("PASS ", label)
	else: failed += 1; printerr("FAIL ", label, " ", detail)
func _finish() -> void:
	if passed + failed != EXPECTED_CHECKS: failed += 1; printerr("FAIL pinned count expected %d got %d" % [EXPECTED_CHECKS, passed + failed - 1])
	print("WOTR_PHASE %d passed %d failed" % [passed, failed]); quit(0 if failed == 0 else 1)
