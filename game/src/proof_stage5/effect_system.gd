class_name Stage5EffectSystem
extends RefCounted
## Deterministic target validation and stable-ID effect application.


func resolve_target(world: RefCounted, team: int, power: Dictionary, target_spec: Dictionary) -> Dictionary:
	var mode: String = String(power.get("targetMode", ""))
	match mode:
		"global":
			if not target_spec.is_empty():
				return {"ok": false, "reason": "unexpected_target"}
			return {"ok": true, "reason": ""}
		"position":
			if target_spec.size() != 1 or not target_spec.has("position") or not target_spec["position"] is Vector2i:
				return {"ok": false, "reason": "invalid_position_target"}
			var position: Vector2i = target_spec["position"]
			if not bool(world.contains_cell(position)):
				return {"ok": false, "reason": "target_out_of_bounds"}
			return {"ok": true, "reason": "", "position": position}
		"friendly_entity", "hostile_entity", "hostile_building":
			if target_spec.size() != 1 or not target_spec.has("entity_id") or not target_spec["entity_id"] is int:
				return {"ok": false, "reason": "invalid_entity_target"}
			var target_id: int = int(target_spec["entity_id"])
			var target: Dictionary = world.entity(target_id)
			if target.is_empty() or int(target.get("health", 0)) <= 0:
				return {"ok": false, "reason": "target_unavailable"}
			var target_team: int = int(target.get("team", -1))
			var kind: String = String(target.get("kind", ""))
			if mode == "friendly_entity" and target_team != team:
				return {"ok": false, "reason": "target_not_friendly"}
			if mode == "hostile_entity" and (target_team == team or kind != "unit"):
				return {"ok": false, "reason": "target_not_hostile_unit"}
			if mode == "hostile_building" and (target_team == team or kind != "building"):
				return {"ok": false, "reason": "target_not_hostile_building"}
			return {"ok": true, "reason": "", "entity_id": target_id}
	return {"ok": false, "reason": "unsupported_target_mode"}


func apply(world: RefCounted, team: int, power: Dictionary, target: Dictionary) -> Dictionary:
	var affected: Array[int] = []
	var total_damage: int = 0
	var total_healed: int = 0
	var points_before: int = int(world.team_state(team).get("available_points", 0))
	for raw_effect: Variant in power.get("effects", []):
		var effect: Dictionary = raw_effect
		match String(effect.get("type", "")):
			"heal_entity":
				var healed: int = int(world.apply_heal(int(target["entity_id"]), int(effect["amount"])))
				total_healed += healed
				_append_unique(affected, int(target["entity_id"]))
			"damage_entity":
				var damaged: int = int(world.apply_damage(int(target["entity_id"]), int(effect["amount"]), team))
				total_damage += damaged
				_append_unique(affected, int(target["entity_id"]))
			"damage_building":
				var building_damage: int = int(world.apply_damage(int(target["entity_id"]), int(effect["amount"]), team))
				total_damage += building_damage
				_append_unique(affected, int(target["entity_id"]))
			"area_damage":
				var area_damage: Dictionary = _apply_area(world, team, target["position"], effect, false)
				total_damage += int(area_damage["total"])
				_merge_ids(affected, area_damage["ids"])
			"area_heal":
				var area_heal: Dictionary = _apply_area(world, team, target["position"], effect, true)
				total_healed += int(area_heal["total"])
				_merge_ids(affected, area_heal["ids"])
			"weather":
				world.activate_weather(team, effect)
			"global_heal":
				for entity_id: int in world.entity_ids():
					var row: Dictionary = world.entity(entity_id)
					if int(row.get("team", -1)) != team or int(row.get("health", 0)) <= 0:
						continue
					var amount: int = int(effect["buildingAmount"]) if String(row.get("kind", "")) == "building" else int(effect["amount"])
					var global_healed: int = int(world.apply_heal(entity_id, amount))
					total_healed += global_healed
					_append_unique(affected, entity_id)
	affected.sort()
	return {
		"affected_ids": affected,
		"damage": total_damage,
		"healed": total_healed,
		"power_points_earned": int(world.team_state(team).get("available_points", 0)) - points_before,
		"weather_active": not Dictionary(world.weather).is_empty(),
	}


func _apply_area(world: RefCounted, team: int, center: Vector2i, effect: Dictionary, healing: bool) -> Dictionary:
	var radius: int = int(effect["radiusCells"])
	var radius_squared: int = radius * radius
	var affected: Array[int] = []
	var total: int = 0
	for entity_id: int in world.entity_ids():
		var row: Dictionary = world.entity(entity_id)
		if int(row.get("health", 0)) <= 0:
			continue
		var same_team: bool = int(row.get("team", -1)) == team
		if healing != same_team:
			continue
		if not healing and String(row.get("kind", "")) != "unit":
			continue
		var delta: Vector2i = Vector2i(row["position"]) - center
		if delta.x * delta.x + delta.y * delta.y > radius_squared:
			continue
		var amount: int = int(effect.get("buildingAmount", effect["amount"])) if String(row.get("kind", "")) == "building" else int(effect["amount"])
		var applied: int = int(world.apply_heal(entity_id, amount)) if healing else int(world.apply_damage(entity_id, amount, team))
		total += applied
		_append_unique(affected, entity_id)
	affected.sort()
	return {"ids": affected, "total": total}


func _append_unique(values: Array[int], value: int) -> void:
	if not values.has(value):
		values.append(value)


func _merge_ids(target: Array[int], source: Array) -> void:
	for raw_id: Variant in source:
		_append_unique(target, int(raw_id))
