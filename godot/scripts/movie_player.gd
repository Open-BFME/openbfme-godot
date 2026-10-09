## Lane CAMP-1H (owner feedback F6): a movie of TheVideoPlayer (GameWorld.get_movie, GameClient/VideoPlayer.h) as the campaign shows it: the campaign's
## intro (OverallCampaignIntroMovie), a mission's IntroMovie before its load screen and the maps' PLAY_MOVIE_IN_GAME. Retail's stream plays the audio
## events named after the movie file ("<file>_Music", "<file>": the narration DialogEvents of speech.ini, RW 0x49112C) for the movie's length (the VP6
## header's frames / rate). The picture is not decoded (stop S-1710): the screen stays black while the narration plays. Esc, Space, Enter or a click
## skips (INFERENCE: ZH's load screen movie skip; RotWK's input rule for movies was not read).
extends Node

signal finished(title: String)

var playing := ""          # the title on screen ("" none)
var skipped := false
var _layer: CanvasLayer
var _handles: Array = []
var _audio: Node
var _elapsed_ms := 0


## plays `title` and returns when it ended or was skipped; false when the movie cannot be played (its error is printed: a missing movie is not fatal in
## retail either, RW 0x490EE6 returns no stream)
func play(world: Node, audio: Node, title: String) -> bool:
	if title.is_empty():
		return false
	var info: Dictionary = world.get_movie(title)
	if not info.get("ok", false):
		printerr("MOVIE %s not played: %s" % [title, info.get("error", "?")])
		return false
	print("MOVIE %s: %s, %dx%d, %d frames, %.1f s, audio %s (picture not decoded: S-1710)" % [title, info.path, info.width, info.height, info.frames,
		float(info.duration_ms) / 1000.0, str(info.audio_events)])
	_audio = audio
	_show()
	playing = title
	skipped = false
	_handles.clear()
	if audio != null:
		for e in info.audio_events:
			_handles.append(audio.play_sound(e))
	# the movie runs on the wall clock; under Godot's Movie Maker (--write-movie: simulated time) on the frames' deltas
	var simulated := not Engine.get_write_movie_path().is_empty()
	var started := Time.get_ticks_msec()
	var waited := 0.0
	while not skipped:
		var ms := waited * 1000.0 if simulated else float(Time.get_ticks_msec() - started)
		if ms >= float(info.duration_ms):
			break
		await get_tree().process_frame
		waited += get_process_delta_time()
	_elapsed_ms = int(waited * 1000.0) if simulated else Time.get_ticks_msec() - started
	stop()
	return true


## ends the movie now (its audio events are removed, RW's stream close)
func stop() -> void:
	if _audio != null:
		for h in _handles:
			if int(h) > 0:
				_audio.remove_event(h)
	_handles.clear()
	if _layer != null:
		_layer.queue_free()
		_layer = null
	if not playing.is_empty():
		var t := playing
		playing = ""
		print("MOVIE %s %s after %.1f s" % [t, "skipped" if skipped else "ended", _elapsed_ms / 1000.0])
		finished.emit(t)


func _show() -> void:
	if _layer != null:
		return
	_layer = CanvasLayer.new()
	_layer.layer = 100
	var black := ColorRect.new()
	black.color = Color.BLACK
	black.set_anchors_preset(Control.PRESET_FULL_RECT)
	black.mouse_filter = Control.MOUSE_FILTER_STOP
	_layer.add_child(black)
	add_child(_layer)


func _input(event: InputEvent) -> void:
	if playing.is_empty():
		return
	var skip := false
	if event is InputEventKey and event.pressed and not event.echo:
		skip = event.keycode in [KEY_ESCAPE, KEY_SPACE, KEY_ENTER]
	elif event is InputEventMouseButton and event.pressed:
		skip = true
	if skip:
		skipped = true
		get_viewport().set_input_as_handled()
