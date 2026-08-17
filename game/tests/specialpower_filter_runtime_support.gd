extends RefCounted
## Shared fixture for the SpecialPower targeting-admission signatures.
##
## Retail authors these three fields on SpecialPower blocks and the sim gates a
## cast on them: ObjectFilter admits the target, ForbiddenObjectFilter plus
## ForbiddenObjectRange refuse a cast with a forbidden object standing inside
## the authored radius. The fixture proves the RotWK oracle authors the field,
## then drives the compiled gate in both directions.

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const ORACLE := "res://../.private/retail-work/editions/rotwk/cache/effective-assets/data/ini/specialpower.ini"
const SOURCE_SCALE := 0.1
const FORBIDDEN_RANGE_SOURCE := 60.0


func run(signature: String) -> Dictionary:
	var field := signature.get_slice(".", 1)
	var oracle := FileAccess.get_file_as_string(ProjectSettings.globalize_path(ORACLE))
	if oracle.is_empty() or not oracle.contains(field):
		return {"ok": false, "detail": "RotWK specialpower.ini lacks " + field}
	match signature:
		"field:specialpower.ObjectFilter":
			return _object_filter()
		"field:specialpower.ForbiddenObjectFilter":
			return _forbidden_object_filter()
		"field:specialpower.ForbiddenObjectRange":
			return _forbidden_object_range()
	return {"ok": false, "detail": "unknown signature: " + signature}


func _object_filter() -> Dictionary:
	## `-STRUCTURE` must refuse a structure-kind target, and a hero-kind target
	## at the same point must not be refused by the filter.
	var sim = _fixture()
	(sim.entities[2] as Dictionary)["kind_of"] = ["STRUCTURE"]
	var refused: Dictionary = sim.cast_ability(1, "Fixture", Vector2(20, 0), 0)
	if String(refused.get("reason", "")) != "object-filter-refused":
		return {"ok": false, "detail": "a -STRUCTURE target was admitted: %s" % str(refused)}
	var admitted_sim = _fixture()
	(admitted_sim.entities[2] as Dictionary)["kind_of"] = ["HERO"]
	var admitted: Dictionary = admitted_sim.cast_ability(1, "Fixture", Vector2(20, 0), 0)
	if String(admitted.get("reason", "")) == "object-filter-refused":
		return {"ok": false, "detail": "a +HERO target was refused: %s" % str(admitted)}
	return {"ok": true, "detail": "structure refused, hero admitted"}


func _forbidden_object_filter() -> Dictionary:
	## A MACHINE beside the destination refuses the cast even though it is far
	## from the caster; the same object outside the filter does not.
	var sim = _fixture()
	(sim.entities[2] as Dictionary)["kind_of"] = ["HERO"]
	(sim.entities[3] as Dictionary)["kind_of"] = ["MACHINE"]
	(sim.entities[3] as Dictionary)["position"] = Vector2(20.5, 0)
	sim._spatial_sync(sim.entities[3] as Dictionary)
	var refused: Dictionary = sim.cast_ability(1, "Fixture", Vector2(20, 0), 0)
	if String(refused.get("reason", "")) != "forbidden-object-nearby":
		return {"ok": false, "detail": "a nearby MACHINE did not refuse the cast: %s" % str(refused)}
	if int(refused.get("object_id", 0)) != 3:
		return {"ok": false, "detail": "the refusal named no forbidden object: %s" % str(refused)}
	var allowed_sim = _fixture()
	(allowed_sim.entities[2] as Dictionary)["kind_of"] = ["HERO"]
	(allowed_sim.entities[3] as Dictionary)["kind_of"] = ["HERO"]
	var allowed: Dictionary = allowed_sim.cast_ability(1, "Fixture", Vector2(20, 0), 0)
	if String(allowed.get("reason", "")) == "forbidden-object-nearby":
		return {"ok": false, "detail": "a non-MACHINE object refused the cast: %s" % str(allowed)}
	return {"ok": true, "detail": "MACHINE refused at range, HERO ignored"}


func _forbidden_object_range() -> Dictionary:
	## The radius is authored in source units and binds to map scale like every
	## other range: 60 source units is 6.0 sim units at this fixture's scale.
	var radius := FORBIDDEN_RANGE_SOURCE * SOURCE_SCALE
	var sim = _fixture()
	(sim.entities[2] as Dictionary)["kind_of"] = ["HERO"]
	(sim.entities[3] as Dictionary)["kind_of"] = ["MACHINE"]
	(sim.entities[3] as Dictionary)["position"] = Vector2(20.0 + radius - 0.5, 0)
	sim._spatial_sync(sim.entities[3] as Dictionary)
	var inside: Dictionary = sim.cast_ability(1, "Fixture", Vector2(20, 0), 0)
	if String(inside.get("reason", "")) != "forbidden-object-nearby":
		return {"ok": false, "detail": "a MACHINE inside %.1f did not refuse: %s" % [radius, str(inside)]}
	var outside_sim = _fixture()
	(outside_sim.entities[2] as Dictionary)["kind_of"] = ["HERO"]
	(outside_sim.entities[3] as Dictionary)["kind_of"] = ["MACHINE"]
	(outside_sim.entities[3] as Dictionary)["position"] = Vector2(20.0 + radius + 0.5, 0)
	outside_sim._spatial_sync(outside_sim.entities[3] as Dictionary)
	var outside: Dictionary = outside_sim.cast_ability(1, "Fixture", Vector2(20, 0), 0)
	if String(outside.get("reason", "")) == "forbidden-object-nearby":
		return {"ok": false, "detail": "a MACHINE outside %.1f still refused: %s" % [radius, str(outside)]}
	return {"ok": true, "detail": "refused inside %.1f, admitted outside" % radius}


func _fixture():
	var sim = Sim.new()
	var rules := {}
	rules[Sim.SOLDIER_OBJECT_ID] = _rule()
	rules[Sim.SOLDIER_HORDE_ID] = _rule()
	sim.setup({}, {"unit_rules": rules, "source_map_transform_scale": SOURCE_SCALE})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	_spawn(sim, 1, 0, Vector2.ZERO)
	_spawn(sim, 2, 1, Vector2(20, 0))
	_spawn(sim, 3, 1, Vector2(100, 0))
	var contract := {
		"objectFilter": ["NONE", "+HERO", "-STRUCTURE"],
		"forbiddenObjectFilter": ["NONE", "+MACHINE"],
		"forbiddenObjectRange": FORBIDDEN_RANGE_SOURCE,
	}
	var ability := {
		"ability_id": "Fixture",
		"special_power_id": "SpecialFixture",
		"targeting": "enemy-object",
		"cooldown_ticks": 10,
		"required_level": 1,
		"level_gate_resolved": true,
		"castable": true,
		"effect": {"kind": "heal", "radius": 20.0, "amount": 10.0, "amountKind": "flat"},
		"special_power_contract": contract,
	}
	sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = sim._scaled_ability_rules([ability], SOURCE_SCALE)
	sim._attach_hero_ability_state(sim.entities[1] as Dictionary)
	return sim


func _spawn(sim, id: int, team: int, point: Vector2) -> void:
	sim._add_battalion(id, team, point, "Fixture", Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, 0, _rule())


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
