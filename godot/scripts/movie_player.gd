## Lane CAMP-1H (owner feedback F6): a movie of TheVideoPlayer (GameWorld.get_movie, GameClient/VideoPlayer.h) as the campaign shows it: the campaign's
## intro (OverallCampaignIntroMovie), a mission's IntroMovie before its load screen and the maps' PLAY_MOVIE_IN_GAME. Retail's stream plays the audio
## events named after the movie file ("<file>_Music", "<file>": the narration DialogEvents of speech.ini, RW 0x49112C) for the movie's length (the VP6
## header's frames / rate). Esc, Space, Enter or a click skips (INFERENCE: ZH's load screen movie skip; RotWK's input rule for movies was not read).
## Lane CAMP-2: the picture. VP6MovieStream (GodotDevice/GodotVideoStream.h, GameClient/VP6Decoder.h) decodes the file's VP6 frames on the wall clock
## at the header's rate, drawn over black, centred and scaled to the window keeping the movie's aspect (INFERENCE: retail's display of a movie was not
## read); the start-up movies (game.gd) use the same player. A movie that cannot be found, opened or decoded is an error: printed (MOVIE ERROR, the
## report) and shown to the player for a few seconds, never skipped silently.
extends Node

signal finished(title: String)

const ERROR_SHOW_MS := 4000

var playing := ""          # the title on screen ("" none)
var skipped := false
var errors: Array = []     # lane CAMP-2: "<title>: <error>" of every movie that could not be played
var last: Dictionary = {}  # lane CAMP-2: the last movie's playback (title, frames, frames_decoded, duration_ms, elapsed_ms, luma_first, luma_last, skipped)
var _layer: CanvasLayer
var _picture: TextureRect
var _handles: Array = []
var _audio: Node
var _elapsed_ms := 0
var _stream: RefCounted
var path_overrides := {}  # lane CAMP-2 (tests, game.gd --movie-file=TITLE=PATH): the file a title's stream opens instead of the found one
var skip_after_ms := -1    # lane CAMP-2 (tests, game.gd --movie-skip-after): the movie is skipped this long after its start, as a player's Esc


## plays `title` and returns when it ended or was skipped; false when the movie cannot be played (the error is printed and shown)
func play(world: Node, audio: Node, title: String) -> bool:
	if title.is_empty():
		return false
	var info: Dictionary = world.get_movie(title)
	if path_overrides.has(title):
		info["ok"] = true
		info["full_path"] = path_overrides[title]
		info["path"] = path_overrides[title]
		info["audio_events"] = info.get("audio_events", [])
	if not info.get("ok", false):
		await _error(title, str(info.get("error", "?")))
		return false
	if not ClassDB.class_exists("VP6MovieStream"):
		await _error(title, "the openbfme extension has no VP6MovieStream class (too old)")
		return false
	_stream = ClassDB.instantiate("VP6MovieStream")
	var opened: Dictionary = _stream.open(String(info.get("full_path", "")))
	if not opened.get("ok", false):
		await _error(title, str(opened.get("error", "?")))
		return false
	print("MOVIE %s: %s, %dx%d, %d frames at %.3f fps, %.1f s, audio %s" % [title, info.path, opened.width, opened.height, opened.frames,
		opened.fps, float(opened.duration_ms) / 1000.0, str(info.audio_events)])
	_audio = audio
	_show(opened)
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
	var fps: float = opened.fps
	var frames: int = opened.frames
	var luma_first := -1
	var luma_values := {}
	var failed := ""
	while not skipped:
		var ms := waited * 1000.0 if simulated else float(Time.get_ticks_msec() - started)
		if ms >= float(opened.duration_ms):
			break
		if skip_after_ms >= 0 and ms >= skip_after_ms:
			skipped = true
			break
		var target := mini(int(ms * fps / 1000.0), frames - 1)
		if target > _stream.get_frame() and not _stream.advance_to(target):
			failed = _stream.get_error()
			break
		if _stream.get_frame() >= 0:
			var luma := int(_stream.get_stats().luma_sum)
			if luma_first < 0:
				luma_first = luma
			luma_values[luma] = true
		await get_tree().process_frame
		waited += get_process_delta_time()
	_elapsed_ms = int(waited * 1000.0) if simulated else Time.get_ticks_msec() - started
	var stats: Dictionary = _stream.get_stats()
	last = {"title": title, "frames": frames, "frame": _stream.get_frame(), "frames_decoded": stats.frames_decoded, "decode_ms": stats.decode_ms,
		"duration_ms": opened.duration_ms, "elapsed_ms": _elapsed_ms, "luma_first": luma_first, "luma_last": stats.luma_sum, "skipped": skipped,
		"error": failed, "distinct_pictures": luma_values.size(), "audio_events": info.audio_events, "audio_started": _handles.filter(func(h): return int(h) > 0).size()}
	print("MOVIE %s picture: %d of %d frames shown (decoded %d in %.0f ms), luma first %d last %d" % [title, _stream.get_frame() + 1, frames,
		stats.frames_decoded, stats.decode_ms, luma_first, stats.luma_sum])
	stop()
	if not failed.is_empty():
		await _error(title, failed)
		return false
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
		_picture = null
	_stream = null
	if not playing.is_empty():
		var t := playing
		playing = ""
		print("MOVIE %s %s after %.1f s" % [t, "skipped" if skipped else "ended", _elapsed_ms / 1000.0])
		finished.emit(t)


func _show(opened: Dictionary) -> void:
	if _layer != null:
		return
	_layer = CanvasLayer.new()
	_layer.layer = 100
	var black := ColorRect.new()
	black.color = Color.BLACK
	black.set_anchors_preset(Control.PRESET_FULL_RECT)
	black.mouse_filter = Control.MOUSE_FILTER_STOP
	_layer.add_child(black)
	_picture = TextureRect.new()
	_picture.name = "MoviePicture"
	_picture.texture = _stream.get_texture()
	_picture.set_anchors_preset(Control.PRESET_FULL_RECT)
	_picture.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_picture.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_picture.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR
	_picture.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_layer.add_child(_picture)
	add_child(_layer)


## lane CAMP-2: a movie that cannot be played reaches the report (MOVIE ERROR) and the player (the message for a few seconds)
func _error(title: String, error: String) -> void:
	var line := "%s: %s" % [title, error]
	errors.append(line)
	printerr("MOVIE ERROR ", line)
	var layer := CanvasLayer.new()
	layer.layer = 100
	var label := Label.new()
	label.set_meta("game_text", true) # an error the player must see (game.gd _dev_text_on_screen)
	label.text = "The movie %s could not be played:\n%s" % [title, error]
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.set_anchors_preset(Control.PRESET_FULL_RECT)
	label.add_theme_font_size_override("font_size", 20)
	label.add_theme_color_override("font_color", Color(1.0, 0.85, 0.6))
	var black := ColorRect.new()
	black.color = Color.BLACK
	black.set_anchors_preset(Control.PRESET_FULL_RECT)
	layer.add_child(black)
	layer.add_child(label)
	add_child(layer)
	await get_tree().process_frame
	print("MOVIE ERROR shown to the player: %s (visible %s)" % [label.text.replace("\n", " "), str(label.is_visible_in_tree())])
	var until := Time.get_ticks_msec() + ERROR_SHOW_MS
	while Time.get_ticks_msec() < until and not skipped:
		await get_tree().process_frame
	skipped = false
	layer.queue_free()


func _input(event: InputEvent) -> void:
	if playing.is_empty() and _layer == null:
		return
	var skip := false
	if event is InputEventKey and event.pressed and not event.echo:
		skip = event.keycode in [KEY_ESCAPE, KEY_SPACE, KEY_ENTER]
	elif event is InputEventMouseButton and event.pressed:
		skip = true
	if skip:
		skipped = true
		get_viewport().set_input_as_handled()
