extends SceneTree

var checks := 0
var failures: Array[String] = []


func _check(condition: bool, label: String) -> void:
	checks += 1
	if not condition:
		failures.append(label)


func _init() -> void:
	call_deferred("_run")


func _run() -> void:
	await process_frame
	await process_frame
	var timing := load("res://src/retail_slice/retail_animation_timing.gd") as GDScript
	var first := float(timing.speed_factor([0.9, 1.1], 17, 3, 12, "GUArcher_RUNA"))
	var repeat := float(timing.speed_factor([0.9, 1.1], 17, 3, 12, "GUArcher_RUNA"))
	var next_entry := float(timing.speed_factor([0.9, 1.1], 17, 3, 13, "GUArcher_RUNA"))
	_check(first >= 0.9 and first <= 1.1, "variable authored range bounds")
	_check(is_equal_approx(first, repeat), "same state entry is deterministic")
	_check(not is_equal_approx(first, next_entry), "next state entry resamples")
	_check(is_equal_approx(float(timing.speed_factor([1.5, 1.5], 17, 0, 1, "GUArcher_DIEA")), 1.5), "fixed authored range is exact")
	_check(is_equal_approx(float(timing.speed_factor([], 17, 0, 1, "GUArcher_IDLA")), 1.0), "absent authored range uses SAGE default one")
	_run_packaged_visual_property_checks()
	if failures.is_empty():
		print("ANIMATION_SPEED_FACTOR_RANGE_OK checks=%d" % checks)
		quit(0)
	else:
		for failure in failures:
			push_error("ANIMATION_SPEED_FACTOR_RANGE_FAIL %s" % failure)
		quit(1)


func _run_packaged_visual_property_checks() -> void:
	var content_db := root.get_node_or_null("ContentDB")
	_check(content_db != null, "ContentDB is available")
	if content_db == null:
		return
	var source: Dictionary = {}
	for value in content_db.playable_unit_runtimes.values():
		var document := value as Dictionary
		var visual := (document.get("registration", {}) as Dictionary).get("visual", {}) as Dictionary
		var core := visual.get("coreAnimations", {}) as Dictionary
		if core.has("idle") and typeof(core.idle) == TYPE_ARRAY and not (core.idle as Array).is_empty():
			source = document
			break
	_check(not source.is_empty(), "selected pack exposes a clip-backed unit")
	if source.is_empty():
		return
	var document := source.duplicate(true)
	var registration := document.registration as Dictionary
	var visual := registration.visual as Dictionary
	var binding := (visual.coreAnimations.idle as Array)[0] as Dictionary
	var provenance := {
		"definingObject": String(document.objectId),
		"virtualPath": "data/ini/object/fixture.ini",
		"inheritanceDistance": 0,
		"scopePath": ["Draw:ModuleTag_Draw", "IdleAnimationState"],
		"line": 41,
	}
	binding["AnimationBlendTime"] = 15
	binding["AnimationPriority"] = 6
	var priority_provenance := provenance.duplicate(true)
	priority_provenance["line"] = 42
	binding["authoredProperties"] = [
		{"key": "AnimationBlendTime", "value": 15, "provenance": provenance.duplicate(true)},
		{"key": "AnimationPriority", "value": 6, "provenance": priority_provenance},
	]
	var label_provenance := provenance.duplicate(true)
	label_provenance["line"] = 40
	visual["authoredStateLabels"] = [{
		"StateName": "STATE_Idle",
		"provenance": label_provenance,
		"linkedAnimations": [{"identifier": String(binding.identifier), "conditions": binding.get("conditions", [])}],
		"runtimeSupport": "packaged-unimplemented",
	}]
	var pack_root := String(document.get("_pack_root", ""))
	_check(content_db._validate_playable_unit_runtime(pack_root, document), "typed visual properties validate")
	var projection := content_db._playable_unit_projection(document) as Dictionary
	var capability := projection.get("capability", {}) as Dictionary
	var idle := (capability.get("states", {}) as Dictionary).get("idle", {}) as Dictionary
	var projected := (idle.get("clipProperties", {}) as Dictionary).get(String(binding.identifier), {}) as Dictionary
	_check(int(projected.get("animationBlendTimeRaw", -1)) == 15, "raw blend value survives projection")
	_check(int(projected.get("animationPriority", -1)) == 6, "priority survives projection")
	_check((capability.get("authoredStateLabels", []) as Array).size() == 1, "state alias survives projection")
	var malformed := document.duplicate(true)
	var malformed_binding := ((((malformed.registration as Dictionary).visual as Dictionary).coreAnimations.idle as Array)[0] as Dictionary)
	malformed_binding["AnimationPriority"] = -1
	_check(not content_db._validate_playable_unit_runtime(pack_root, malformed), "negative priority fails closed")
	var unknown_link := document.duplicate(true)
	var unknown_visual := (unknown_link.registration as Dictionary).visual as Dictionary
	(unknown_visual.authoredStateLabels as Array)[0]["linkedAnimations"] = [{"identifier": "MISSING_CLIP", "conditions": []}]
	_check(not content_db._validate_playable_unit_runtime(pack_root, unknown_link), "unknown state-label clip fails closed")
