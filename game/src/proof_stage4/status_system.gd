class_name Stage4StatusSystem
extends RefCounted
## Deterministic fear/terror, recovery, and cell-safe knockback.

const DEFINITION_KEYS: Array[String] = [
	"champion_fear_immune",
	"fear_power_floor",
	"flee_steps_per_tick",
	"rank_immunity_threshold",
	"terror_power_bonus",
]

var fear_power_floor: int = 1
var flee_steps_per_tick: int = 1
var terror_power_bonus: int = 0
var champion_fear_immune: bool = false
var rank_immunity_threshold: int = 0


func configure(definition: Dictionary) -> String:
	var key_error: String = _require_exact_keys(definition, DEFINITION_KEYS, "status")
	if key_error != "":
		return key_error
	for field: String in ["fear_power_floor", "flee_steps_per_tick", "rank_immunity_threshold", "terror_power_bonus"]:
		if not _is_integer_value(definition[field]):
			return "status.%s must be an integer" % field
	if typeof(definition["champion_fear_immune"]) != TYPE_BOOL:
		return "status.champion_fear_immune must be a boolean"
	fear_power_floor = int(definition["fear_power_floor"])
	flee_steps_per_tick = int(definition["flee_steps_per_tick"])
	terror_power_bonus = int(definition["terror_power_bonus"])
	champion_fear_immune = bool(definition["champion_fear_immune"])
	rank_immunity_threshold = int(definition["rank_immunity_threshold"])
	if fear_power_floor < 1:
		return "status.fear_power_floor must be positive"
	if flee_steps_per_tick < 1 or flee_steps_per_tick > 8:
		return "status.flee_steps_per_tick must be in 1..8"
	if terror_power_bonus < 0:
		return "status.terror_power_bonus must not be negative"
	if rank_immunity_threshold < 0:
		return "status.rank_immunity_threshold must not be negative"
	return ""


func apply_fear(world: Object, source_id: int, target_id: int, power: int, duration_ticks: int, terror: bool = false) -> Dictionary:
	var source: Dictionary = world.entity(source_id)
	var target: Dictionary = world.entity(target_id)
	if source.is_empty() or target.is_empty() or not bool(source.get("alive", false)) or not bool(target.get("alive", false)):
		return {"ok": false, "reason": "invalid_entity"}
	if int(source.get("team", -1)) == int(target.get("team", -1)):
		return {"ok": false, "reason": "friendly_target"}
	var immunity: String = immunity_reason(target)
	if immunity != "":
		return {"ok": false, "reason": "fear_immune", "immunity": immunity}
	if duration_ticks <= 0:
		return {"ok": false, "reason": "invalid_duration"}
	var effective_power: int = power + (terror_power_bonus if terror else 0)
	if effective_power < fear_power_floor or effective_power <= int(target.get("fear_resistance", 0)):
		return {"ok": false, "reason": "fear_resisted"}
	var direction: Vector2i = _direction_away(Vector2i(target["position"]), Vector2i(source["position"]), target_id, source_id)
	target["status"] = "flee"
	target["flee_source_id"] = source_id
	target["flee_direction"] = direction
	target["flee_until_tick"] = int(world.tick_index) + duration_ticks
	return {"ok": true, "reason": "", "terror": terror, "effective_power": effective_power}


func tick_entity(world: Object, target: Dictionary) -> void:
	if String(target.get("status", "normal")) != "flee":
		return
	if not bool(target.get("alive", false)):
		_clear_fear(target, "dead")
		return
	if int(world.tick_index) >= int(target.get("flee_until_tick", 0)):
		_clear_fear(target, "normal")
		return
	var direction: Vector2i = Vector2i(target.get("flee_direction", Vector2i.RIGHT))
	var source: Dictionary = world.entity(int(target.get("flee_source_id", 0)))
	if not source.is_empty() and bool(source.get("alive", false)):
		direction = _direction_away(Vector2i(target["position"]), Vector2i(source["position"]), int(target["id"]), int(source["id"]))
		target["flee_direction"] = direction
	for _step: int in range(flee_steps_per_tick):
		if not bool(world.try_step_entity(int(target["id"]), direction)):
			break


func apply_knockback(world: Object, target_id: int, source_position: Vector2i, distance_cells: int) -> Dictionary:
	var target: Dictionary = world.entity(target_id)
	if target.is_empty() or not bool(target.get("alive", false)):
		return {"ok": false, "reason": "invalid_target", "moved_cells": 0}
	if distance_cells < 0:
		return {"ok": false, "reason": "invalid_distance", "moved_cells": 0}
	var direction: Vector2i = _direction_away(Vector2i(target["position"]), source_position, target_id, 0)
	var moved: int = 0
	for _step: int in range(distance_cells):
		if not bool(world.try_step_entity(target_id, direction)):
			break
		moved += 1
	return {"ok": true, "reason": "", "moved_cells": moved, "direction": direction}


func definition_snapshot() -> Dictionary:
	return {
		"champion_fear_immune": champion_fear_immune,
		"fear_power_floor": fear_power_floor,
		"flee_steps_per_tick": flee_steps_per_tick,
		"rank_immunity_threshold": rank_immunity_threshold,
		"terror_power_bonus": terror_power_bonus,
	}


func immunity_reason(target: Dictionary) -> String:
	if bool(target.get("fear_immune", false)):
		return "explicit"
	if champion_fear_immune and String(target.get("kind", "")) == "champion":
		return "champion_rule"
	if rank_immunity_threshold > 0 and int(target.get("rank", 1)) >= rank_immunity_threshold:
		return "rank_rule"
	return ""


func _clear_fear(target: Dictionary, next_status: String) -> void:
	target["status"] = next_status
	target["flee_source_id"] = 0
	target["flee_direction"] = Vector2i.ZERO
	target["flee_until_tick"] = 0


func _direction_away(target_position: Vector2i, source_position: Vector2i, target_id: int, source_id: int) -> Vector2i:
	var offset: Vector2i = target_position - source_position
	if offset == Vector2i.ZERO:
		return Vector2i.RIGHT if target_id >= source_id else Vector2i.LEFT
	if absi(offset.x) >= absi(offset.y):
		return Vector2i(signi(offset.x), 0)
	return Vector2i(0, signi(offset.y))


static func _require_exact_keys(value: Dictionary, expected: Array[String], path: String) -> String:
	if value.size() != expected.size():
		return "%s must contain exactly: %s" % [path, ",".join(expected)]
	for key: String in expected:
		if not value.has(key):
			return "%s is missing %s" % [path, key]
	return ""


static func _is_integer_value(value: Variant) -> bool:
	return typeof(value) == TYPE_INT or (typeof(value) == TYPE_FLOAT and float(value) == floorf(float(value)))
