extends RefCounted
## Shared fixture for the ExperienceLevel signatures.
##
## The numbers are NOT hand-authored: every threshold, award and per-level
## modifier is read from a SELECTED pack's playable-unit descriptor through the
## live adapter projection, then driven through the sim's XP consumer. A pack
## whose descriptors carry no compiled chain fails the runner rather than
## falling back to a fixture.

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const ORACLE := "res://../.private/retail-work/editions/rotwk/cache/effective-assets/data/ini/experiencelevels.ini"


func run(tree: SceneTree, signature: String) -> Dictionary:
	var field := signature.get_slice(".", 1)
	var oracle := FileAccess.get_file_as_string(ProjectSettings.globalize_path(ORACLE))
	if oracle.is_empty() or not oracle.contains(field):
		return {"ok": false, "detail": "RotWK experiencelevels.ini lacks " + field}
	var content_db = tree.root.get_node_or_null("ContentDB")
	if content_db == null:
		return {"ok": false, "detail": "ContentDB missing"}
	var chosen := _chosen_chain(content_db, signature)
	if chosen.is_empty():
		return {"ok": false, "detail": "no selected descriptor carries a compiled chain for " + field}
	var rule: Dictionary = chosen.get("rule", {}) as Dictionary
	var object_id := String(chosen.get("object_id", ""))
	match signature:
		"field:experiencelevel.RequiredExperience":
			return _required_experience(rule, object_id)
		"field:experiencelevel.ExperienceAward":
			return _experience_award(rule, object_id)
		"field:experiencelevel.AttributeModifiers":
			return _attribute_modifiers(rule, object_id)
	return {"ok": false, "detail": "unknown signature: " + signature}


func _chosen_chain(content_db, signature: String) -> Dictionary:
	var documents: Dictionary = content_db.get_playable_unit_runtimes()
	var object_ids: Array = documents.keys()
	object_ids.sort()
	for object_id_value in object_ids:
		var object_id := String(object_id_value)
		var rule: Dictionary = Adapter.experience_rule(documents[object_id] as Dictionary)
		if rule.is_empty():
			continue
		if _usable(rule, signature):
			return {"object_id": object_id, "rule": rule}
	return {}


func _usable(rule: Dictionary, signature: String) -> bool:
	var initial := int(rule.get("initial_rank", 1))
	match signature:
		"field:experiencelevel.RequiredExperience":
			return not _next_level(rule, initial).is_empty()
		"field:experiencelevel.ExperienceAward":
			var entry := _level_row(rule, initial)
			return bool(entry.get("experience_award_known", false)) and int(entry.get("experience_award", 0)) > 0
		"field:experiencelevel.AttributeModifiers":
			var next_row := _next_level(rule, initial)
			return (
				not next_row.is_empty()
				and (
					roundi(float(next_row.get("health_add", 0.0))) != 0
					or roundi(float(next_row.get("damage_add", 0.0))) != 0
				)
			)
	return false


func _level_row(rule: Dictionary, rank: int) -> Dictionary:
	for row_value in Array(rule.get("levels", [])):
		var row := row_value as Dictionary
		if int(row.get("rank", 0)) == rank:
			return row
	return {}


func _next_level(rule: Dictionary, rank: int) -> Dictionary:
	for row_value in Array(rule.get("levels", [])):
		var row := row_value as Dictionary
		if int(row.get("rank", 0)) > rank and int(row.get("required_experience", 0)) > 0:
			return row
	return {}


func _required_experience(rule: Dictionary, object_id: String) -> Dictionary:
	## The authored cumulative threshold is exact: one point short keeps the
	## rank, the threshold itself promotes.
	var sim = _fixture(rule)
	var hero: Dictionary = sim.entities[1]
	var initial := int(hero.get("level", 0))
	var next_row := _next_level(rule, initial)
	var threshold := int(next_row.get("required_experience", 0))
	sim._award_experience(hero, threshold - 1)
	if int(hero.get("level", 0)) != initial:
		return {"ok": false, "detail": "%s promoted at %d of %d" % [object_id, threshold - 1, threshold]}
	sim._award_experience(hero, 1)
	if int(hero.get("level", 0)) != int(next_row.get("rank", 0)):
		return {"ok": false, "detail": "%s stayed at rank %d at %d XP" % [object_id, int(hero.get("level", 0)), threshold]}
	return {"ok": true, "detail": "%s rank %d -> %d at RequiredExperience %d" % [
		object_id, initial, int(next_row.get("rank", 0)), threshold,
	]}


func _experience_award(rule: Dictionary, object_id: String) -> Dictionary:
	## Killing a member pays the victim level's authored ExperienceAward to the
	## killer, and an unauthored victim pays nothing.
	var sim = _fixture(rule)
	var hero: Dictionary = sim.entities[1]
	var victim: Dictionary = sim.entities[2]
	var award := int(_level_row(rule, int(victim.get("level", 1))).get("experience_award", 0))
	sim._award_member_kill_experience(1, victim)
	if int(hero.get("experience_xp", -1)) != award:
		return {"ok": false, "detail": "%s killer holds %s XP, ExperienceAward is %d" % [
			object_id, str(hero.get("experience_xp")), award,
		]}
	var unauthored := victim.duplicate(true)
	unauthored["unit_type"] = "NoSuchUnitType"
	sim._award_member_kill_experience(1, unauthored)
	if int(hero.get("experience_xp", -1)) != award:
		return {"ok": false, "detail": "%s gained XP from an unauthored victim" % object_id}
	return {"ok": true, "detail": "%s paid ExperienceAward %d" % [object_id, award]}


func _attribute_modifiers(rule: Dictionary, object_id: String) -> Dictionary:
	## Level modifiers fold into the per-member base stats by exactly the
	## authored magnitude, never scaled.
	var sim = _fixture(rule)
	var hero: Dictionary = sim.entities[1]
	var next_row := _next_level(rule, int(hero.get("level", 1)))
	var health_add := roundi(float(next_row.get("health_add", 0.0)))
	var damage_add := roundi(float(next_row.get("damage_add", 0.0)))
	var health_before := int(hero.get("member_maximum_health", 0))
	var damage_before := int(hero.get("member_damage", 0))
	sim._award_experience(hero, int(next_row.get("required_experience", 0)))
	if int(hero.get("level", 0)) != int(next_row.get("rank", 0)):
		return {"ok": false, "detail": "%s did not reach rank %d" % [object_id, int(next_row.get("rank", 0))]}
	var health_delta := int(hero.get("member_maximum_health", 0)) - health_before
	var damage_delta := int(hero.get("member_damage", 0)) - damage_before
	if health_delta != health_add or damage_delta != damage_add:
		return {"ok": false, "detail": "%s applied health %+d damage %+d, authored health %+d damage %+d" % [
			object_id, health_delta, damage_delta, health_add, damage_add,
		]}
	return {"ok": true, "detail": "%s rank %d applied AttributeModifiers health %+d damage %+d" % [
		object_id, int(next_row.get("rank", 0)), health_add, damage_add,
	]}


func _fixture(rule: Dictionary):
	var sim = Sim.new()
	var rules := {}
	rules[Sim.SOLDIER_OBJECT_ID] = _rule()
	rules[Sim.SOLDIER_HORDE_ID] = _rule()
	sim.setup({}, {"unit_rules": rules})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	_spawn(sim, 1, 0)
	_spawn(sim, 2, 1)
	sim._unit_experience_rules[Sim.SOLDIER_HORDE_ID] = rule
	sim._attach_experience_state(sim.entities[1] as Dictionary)
	sim._attach_experience_state(sim.entities[2] as Dictionary)
	return sim


func _spawn(sim, id: int, team: int) -> void:
	sim._add_battalion(id, team, Vector2(id * 2, 0), "Fixture", Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, 0, _rule())


func _rule() -> Dictionary:
	return {
		"horde_id": Sim.SOLDIER_HORDE_ID, "category": "hero",
		"speed": 1.0, "speed_source": 10.0,
		"acceleration": 1.0, "acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0, "braking_source": 10.0,
		"attack_range": 1.0, "attack_range_source": 1.0,
		"minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0,
		"vision_range": 10.0, "vision_range_source": 10.0,
		"delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0,
		"firing_duration_ms": 0.0, "attack_period_ticks": 10,
		"pre_attack_ticks": 0, "firing_duration_ticks": 0,
		"member_damage": 10, "member_health": 100, "member_count": 1,
		"formation_positions": [Vector3.ZERO], "provenance": {},
	}
