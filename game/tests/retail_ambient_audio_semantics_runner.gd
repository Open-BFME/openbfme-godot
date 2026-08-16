extends SceneTree
## Legal-safe proof for typed SAGE ambient-audio parameter translation.

const AudioScript := preload("res://src/retail_slice/retail_slice_audio.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var audio = AudioScript.new()
	var parameters := {
		"control": "loop",
		"priority": "lowest",
		"limit": "2",
		"pitchshift": "-5 5",
		"delay": "5000 15000",
		"minrange": "AMB_MIN_RANGE",
		"maxrange": "AMB_MAX_RANGE",
		"volume": "75",
		"volumeshift": "-15",
		"type": "world everyone",
		"submixslider": "Ambient",
	}
	var first: Dictionary = audio._ambient_runtime_parameters("Amb_BirdsAmonHen1", parameters, 17)
	var repeated: Dictionary = audio._ambient_runtime_parameters("Amb_BirdsAmonHen1", parameters, 17)
	_check("macro_min_range", is_equal_approx(float(first.min_range), 300.0))
	_check("macro_max_range", is_equal_approx(float(first.max_range), 800.0))
	_check("pitch_range", float(first.pitch_scale) >= 0.95 and float(first.pitch_scale) <= 1.05)
	_check("delay_range_ms", float(first.delay_ms) >= 5000.0 and float(first.delay_ms) <= 15000.0)
	_check("volume_shift_gain_range", float(first.linear_gain) >= 0.6375 and float(first.linear_gain) <= 0.75)
	_check("typed_priority_and_limit", String(first.priority) == "lowest" and int(first.limit) == 2)
	_check("typed_world_everyone", first.type_tokens == ["world", "everyone"])
	_check("typed_ambient_submix", String(first.submix) == "Ambient")
	_check("selection_is_deterministic", first == repeated)
	_check("substitute_is_honestly_labeled", String(first.selection) == "deterministic-substitute-unproven-retail-rng-seed")
	var gaps: Array[String] = audio._ambient_parameter_gaps("Amb_BirdsAmonHen1", {"parameters": _parameter_rows(parameters)})
	_check("typed_fields_no_longer_reported_unsupported", not _has_prefix(gaps, "unsupported-pitchshift:") and not _has_prefix(gaps, "unsupported-delay:") and not _has_prefix(gaps, "unsupported-volume:") and not _has_prefix(gaps, "unsupported-volumeshift:"))
	_check("renderer_and_scheduler_gaps_remain_closed", _has_prefix(gaps, "unsupported-attenuation-curve:") and _has_prefix(gaps, "unsupported-loop-scheduler:") and _has_prefix(gaps, "unproven-priority-arbitration:") and _has_prefix(gaps, "unproven-concurrent-limit-scheduler:") and _has_prefix(gaps, "unproven-retail-audio-rng-seed:") and _has_prefix(gaps, "unproven-miles-to-godot-gain-equivalence:") and _has_prefix(gaps, "unsupported-submixslider:"), str(gaps))
	audio.free()
	print("RETAIL_AMBIENT_AUDIO_SEMANTICS_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _parameter_rows(parameters: Dictionary) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	for key in parameters:
		result.append({"field": String(key), "value": String(parameters[key])})
	return result


func _has_prefix(values: Array[String], prefix: String) -> bool:
	for value in values:
		if value.begins_with(prefix):
			return true
	return false


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s" % label)
		return
	failed += 1
	push_error("FAIL %s%s" % [label, " :: %s" % detail if detail != "" else ""])
