class_name Stage7AiPlanner
extends RefCounted
## Pure decision layer for the declared build/train/attack plan.

var plan_definition: Dictionary = {}


func configure(definition: Dictionary) -> String:
	if definition.is_empty() or typeof(definition.get("steps")) != TYPE_ARRAY or Array(definition["steps"]).is_empty():
		return "invalid_plan"
	plan_definition = definition.duplicate(true)
	return ""


func step_count() -> int:
	return Array(plan_definition.get("steps", [])).size()


func step(index: int) -> Dictionary:
	var steps: Array = plan_definition.get("steps", [])
	if index < 0 or index >= steps.size():
		return {}
	return Dictionary(steps[index]).duplicate(true)


func decide(plan_index: int, resources: int, army_size: int, can_gain_resources: bool) -> Dictionary:
	var current := step(plan_index)
	if current.is_empty():
		return {"decision": "idle", "reason": "plan_complete"}
	var action := String(current["action"])
	if action == "build" or action == "train":
		if resources >= int(current["cost"]):
			return {"decision": "start_job", "step": current}
		if can_gain_resources:
			return {"decision": "wait", "reason": "awaiting_finite_income"}
		return {"decision": "skip", "reason": "finite_resources_exhausted", "step": current}
	if action == "attack":
		if army_size >= int(current["minimumArmy"]):
			return {"decision": "attack", "fallback": false, "step": current}
		if can_gain_resources:
			return {"decision": "wait", "reason": "awaiting_army"}
		return {"decision": "attack", "fallback": true, "step": current}
	return {"decision": "idle", "reason": "unsupported_action"}
