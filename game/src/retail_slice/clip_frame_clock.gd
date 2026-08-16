class_name ClipFrameClock
extends RefCounted
## Authored-frame clock for a playing AnimationPlayer.
##
## Retail animation frames are the same 30 FPS clock already proven for
## AnimationBlendTime (`frames / 30`). This reads the player's playback
## position — not wall time — so blends, seeks, RANDOMSTART, and backwards
## modes stay on the authored frame.

const AnimationStateSelectScript := preload("res://src/retail_slice/animation_state_select.gd")
const MODULE := "ClipFrameClock"


static func retail_fps() -> float:
	return float(AnimationStateSelectScript.RETAIL_ANIMATION_FPS)


static func frames_from_seconds(seconds: float) -> float:
	if not is_finite(seconds) or seconds < 0.0:
		return -1.0
	return seconds * retail_fps()


static func seconds_from_frames(frames: float) -> float:
	return float(AnimationStateSelectScript.blend_seconds(frames))


static func sample(player: AnimationPlayer) -> Dictionary:
	if player == null:
		return _empty_sample()
	var seconds := float(player.current_animation_position)
	var length := float(player.current_animation_length)
	return {
		"source": "clip-frame-clock",
		"clip": String(player.current_animation),
		"seconds": seconds,
		"frame": frames_from_seconds(seconds),
		"lengthFrames": frames_from_seconds(length),
		"playing": player.is_playing(),
		"backwards": float(player.speed_scale) < 0.0,
	}


static func prime(player: AnimationPlayer) -> Dictionary:
	## First sample after play/seek. Cues already behind the playhead do not
	## fire — RANDOMSTART and START_FRAME_LAST land past early footsteps.
	## Authored FireWhenSkipped is the opt-in exception, applied by FXEvent.
	var sampled: Dictionary = sample(player)
	sampled["primed"] = true
	sampled["skippedCuePolicy"] = "ignore"
	sampled["crossed"] = []
	return sampled


static func crossed(
	previous_frame: float,
	current_frame: float,
	cue_frame: float,
	length_frames: float = -1.0,
	backwards: bool = false
) -> bool:
	if previous_frame < 0.0 or current_frame < 0.0 or cue_frame < 0.0:
		return false
	if is_equal_approx(previous_frame, current_frame):
		return false
	if backwards:
		if current_frame > previous_frame and length_frames > 0.0:
			return cue_frame < previous_frame or cue_frame >= current_frame
		return previous_frame > cue_frame and cue_frame >= current_frame
	if current_frame < previous_frame and length_frames > 0.0:
		return cue_frame > previous_frame or cue_frame <= current_frame
	return previous_frame < cue_frame and cue_frame <= current_frame


static func seek_manual(player: AnimationPlayer, frame: float) -> Dictionary:
	## MANUAL mode: an external driver sets the authored frame. The player
	## is not started; the clock still reads the seeked position.
	if player == null:
		return _empty_sample()
	var seconds := seconds_from_frames(frame)
	if seconds < 0.0:
		return _empty_sample()
	var playable := String(player.current_animation)
	if playable == "" and player.get_animation_list().size() > 0:
		playable = String(player.get_animation_list()[0])
		player.current_animation = playable
	if playable != "":
		if not player.is_playing():
			player.play(playable)
		player.pause()
		player.speed_scale = 0.0
	player.seek(seconds, true)
	var sampled: Dictionary = sample(player)
	sampled["manualSeekable"] = true
	sampled["manualDeferred"] = false
	return sampled


static func _empty_sample() -> Dictionary:
	return {
		"source": "clip-frame-clock",
		"clip": "",
		"seconds": -1.0,
		"frame": -1.0,
		"lengthFrames": -1.0,
		"playing": false,
		"backwards": false,
	}
