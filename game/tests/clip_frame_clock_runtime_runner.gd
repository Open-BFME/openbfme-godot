extends SceneTree
## Clip/frame clock: authored frame = AnimationPlayer position * 30 FPS.
##
## Reads playback position, not wall time. RANDOMSTART seeks past a cue do
## not fire that cue (FireWhenSkipped is the authored opt-in, FXEvent slice).
## MANUAL is seekable: an external driver sets the frame.

const Watchdog := preload("res://tests/runner_watchdog.gd")
const ClipFrameClockScript := preload("res://src/retail_slice/clip_frame_clock.gd")
const AnimationStateSelectScript := preload("res://src/retail_slice/animation_state_select.gd")
const EXPECTED_CHECKS := 10

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "CLIP_FRAME_CLOCK_RUNTIME", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	await process_frame
	_check(
		"ClipFrameClock frames compose with AnimationBlendTime frames/30",
		is_equal_approx(float(ClipFrameClockScript.frames_from_seconds(0.5)), 15.0)
		and is_equal_approx(float(AnimationStateSelectScript.blend_seconds(15)), 0.5)
	)
	var player := _make_player(1.0)
	get_root().add_child(player)
	player.play("CLIP")
	player.seek(0.4, true)
	var sampled: Dictionary = ClipFrameClockScript.sample(player)
	_check("sample frame is position * 30", is_equal_approx(float(sampled.get("frame", -1.0)), 12.0))
	_check("forward cross fires once at the authored frame", ClipFrameClockScript.crossed(11.2, 12.0, 12.0))
	_check("same-state re-tick does not re-cross", not ClipFrameClockScript.crossed(12.0, 12.4, 12.0))
	var primed: Dictionary = ClipFrameClockScript.prime(player)
	_check(
		"RANDOMSTART seek past a cue primes without firing",
		is_equal_approx(float(primed.get("frame", -1.0)), 12.0)
		and String(primed.get("skippedCuePolicy", "")) == "ignore"
		and (primed.get("crossed", ["x"]) as Array).is_empty()
		and not ClipFrameClockScript.crossed(12.0, 12.0, 5.0)
	)
	_check("backwards cross uses the playhead, not wall time", ClipFrameClockScript.crossed(18.0, 11.5, 12.0, 30.0, true))
	_check("loop wrap still crosses a cue after the previous playhead", ClipFrameClockScript.crossed(29.0, 1.0, 0.0, 30.0, false))
	var manual: Dictionary = ClipFrameClockScript.seek_manual(player, 7.0)
	_check(
		"MANUAL seek sets the authored frame without autoplay",
		is_equal_approx(float(manual.get("frame", -1.0)), 7.0)
		and bool(manual.get("manualSeekable", false))
		and is_equal_approx(player.current_animation_position, 7.0 / 30.0)
		and is_equal_approx(player.speed_scale, 0.0)
	)
	var battalion_script: GDScript = load("res://src/retail_slice/retail_battalion.gd") as GDScript
	var battalion = battalion_script.new()
	get_root().add_child(battalion)
	battalion.member_count = 1
	battalion.member_animation_players[0] = [player]
	battalion.member_current_clips[0] = "CLIP"
	battalion._prime_member_clip_frame_clock(0)
	var clock: Dictionary = battalion.member_clip_frame(0)
	_check(
		"battalion clock follows the seeked playhead",
		is_equal_approx(float(clock.get("frame", -1.0)), 7.0)
		and String(clock.get("clip", "")) == "CLIP"
	)
	battalion.set_member_manual_frame(0, 21.0)
	_check(
		"external MANUAL driver advances the battalion clock",
		is_equal_approx(float(battalion.member_clip_frame(0).get("frame", -1.0)), 21.0)
	)
	battalion.free()
	player.free()
	_finish()


func _make_player(length_seconds: float) -> AnimationPlayer:
	var animation := Animation.new()
	animation.length = length_seconds
	var library := AnimationLibrary.new()
	library.add_animation("CLIP", animation)
	var player := AnimationPlayer.new()
	player.add_animation_library("", library)
	return player


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error(label)


func _finish() -> void:
	print("CLIP_FRAME_CLOCK_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		push_error("check-count mismatch: expected=%d actual=%d" % [EXPECTED_CHECKS, passed + failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
