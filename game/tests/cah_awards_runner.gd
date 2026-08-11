extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	var sim = Sim.new()
	var hero_id := "CreateAHero__0123456789abcdef01234567"
	sim._cah_award_contracts[hero_id] = {
		"heroId": "0123456789abcdef01234567",
		"ownerTeam": 0,
		"trackingStats": {},
		"ownedAwards": [],
		"eligibleAwards": ["Vanquisher", "HeroHunter", "SiegeMaster"],
		"awardDefinitions": [
			{"awardId": "Vanquisher", "triggers": [{"statIds": ["ENEMIES_KILLED"], "threshold": 1}]},
			{"awardId": "HeroHunter", "triggers": [{"statIds": ["HEROS_KILLED"], "threshold": 1}]},
			{"awardId": "SiegeMaster", "triggers": [{"statIds": ["MP_KEEPS_DESTROYED"], "threshold": 1}]},
		],
	}
	sim.entities[1] = {"unit_type": hero_id, "team": 0, "health": 100}
	# No ExperienceLevel contract exists for the victim: the award count must
	# happen before the XP path returns for unauthored evidence.
	sim._award_member_kill_experience(1, {"unit_type": "enemy.hero", "category": "hero", "team": 1})
	sim._record_cah_structure_kill(1, {"structure_kind": "fortress", "team": 1})
	var stats: Dictionary = sim._cah_award_tallies[hero_id]
	_check(stats.get("ENEMIES_KILLED") == 1, "member kill increments ENEMIES_KILLED")
	_check(stats.get("HEROS_KILLED") == 1, "hero category increments retail-spelled HEROS_KILLED")
	_check(stats.get("MP_KEEPS_DESTROYED") == 1, "fortress path increments MP_KEEPS_DESTROYED")
	sim.winner = 0
	sim._evaluate_cah_match_awards()
	var result: Dictionary = sim.cah_award_results[hero_id]
	_check((result.get("awards", []) as Array).size() == 3, "eligible award triggers evaluate at match end")
	var empty = Sim.new()
	_check(not empty._authoritative_state().has("cah_award_tallies"), "hero-less snapshots keep the pinned byte shape")
	print("CAH_AWARDS_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("CAH_AWARDS_FAIL %s" % label)
