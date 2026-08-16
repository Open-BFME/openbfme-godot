extends SceneTree
## Typed AnimationSoundClientBehavior picks by authored clip/frame.
##
## Retail binds each bodyfall to a specific animation
## (`AnimationSound = Sound:BodyFallGenericNoArmor Animation:GUBoromir_SKL.GUBoromir_DTHA`).
## Lowest-id is only the authored-clip-absent fallback.

const Watchdog := preload("res://tests/runner_watchdog.gd")
const AudioScript := preload("res://src/retail_slice/retail_slice_audio.gd")
const EXPECTED_CHECKS := 8

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "ANIMATION_SOUND_CLIENT_BEHAVIOR_RUNTIME", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	await process_frame
	var audio: RetailSliceAudio = AudioScript.new()
	audio.playback_enabled = false
	get_root().add_child(audio)
	_check("AnimationSoundClientBehavior consumer loads", audio != null)
	audio.bind_animation_sound_contracts("hero", [_typed_row([
		{"eventId": "BodyFallAardvark", "animation": "GUHero_SKL.GUHero_DTHA", "frames": [8]},
		{"eventId": "BodyFallGenericNoArmor", "animation": "GUHero_SKL.GUHero_DTHB", "frames": [12]},
		{"eventId": "BodyFallGeneric1", "animation": "GUHero_SKL.GUHero_LNDA", "frames": [1]},
		{"eventId": "BodyFallGenericNoArmor", "animation": "GUHero_SKL.GUHero_LNDA", "frames": [14]},
	])])
	var dthb: Dictionary = audio.select_animation_sound("hero", "GUHero_SKL.GUHero_DTHB")
	_check("clip pick is not the lowest bodyfall id", String(dthb.get("eventId", "")) == "BodyFallGenericNoArmor")
	_check("clip leaf matches the authored Animation token", String(dthb.get("source", "")) == "typed-animation-sound")
	var land_early: Dictionary = audio.select_animation_sound("hero", "GUHero_LNDA", 1)
	_check("frame 1 on LNDA picks Generic1", String(land_early.get("eventId", "")) == "BodyFallGeneric1")
	var land_late: Dictionary = audio.select_animation_sound("hero", "GUHero_LNDA", 14)
	_check("frame 14 on the same clip picks NoArmor", String(land_late.get("eventId", "")) == "BodyFallGenericNoArmor")
	var miss: Dictionary = audio.select_animation_sound("hero", "GUHero_ATTA")
	_check("unknown clip fails closed instead of lowest-id", miss.is_empty())
	audio.audio_event_routes["bodyfallgenericnoarmor"] = {
		"event_id": "BodyFallGenericNoArmor", "source": "test", "leaves": [],
	}
	var routed: Dictionary = audio._route_bodyfall("hero", 1, "GUHero_SKL.GUHero_DTHB")
	_check("live bodyfall route uses the clip pick", String(routed.get("event_id", routed.get("id", ""))) == "BodyFallGenericNoArmor" or String(routed.get("event_id", "")) == "BodyFallGenericNoArmor")
	audio.playable_unit_bodyfall["hero"] = "BodyFallAardvark"
	var fallback: Dictionary = audio.select_animation_sound("hero", "")
	_check("empty clip does not invent a typed pick", fallback.is_empty())
	audio.queue_free()
	_finish()


func _typed_row(sounds: Array) -> Dictionary:
	return {
		"module": "AnimationSoundClientBehavior",
		"runtimeStatus": "executable",
		"extraction": "typed",
		"fields": {"AnimationSound": sounds},
	}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error(label)


func _finish() -> void:
	print("ANIMATION_SOUND_CLIENT_BEHAVIOR_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		push_error("check-count mismatch: expected=%d actual=%d" % [EXPECTED_CHECKS, passed + failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
