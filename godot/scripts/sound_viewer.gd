## Plays any retail audio event by name (AUDIO-1): every AudioEvent, DialogEvent, MusicTrack, AmbientStream, StreamedSound and Multisound of the
## mounted RotWK 2.01 INI, through the real audio manager (GameAudio): limits, priorities, volume sliders, distance falloff and all.
##
##   godot --path godot res://scenes/sound_viewer.tscn -- [options]
##
## Options (after `--`):
##   --play=<Name>[,<Name>]   play these events at start (at the listener; --distance moves 3D events away from it)
##   --distance=<units>       the distance of positional events to the right of the listener (default 0)
##   --seconds=<s>            with --play / --selftest: run this long, print what played, then quit
##   --selftest               play one event of every type (the first of each whose files exist), check they start, then quit; exit 1 on a failure
##   --report                 print the manager's report and stats before quitting
##
## Window: type filter, name filter, the event list, Play / Play 3D / Stop, music stop, the four options sliders, and a live line of what plays.
extends Control

const TYPE_NAMES := ["MusicTrack", "DialogEvent", "AudioEvent", "AmbientStream", "StreamedSound", "Multisound"]

var _fs: RefCounted
var _audio: Node
var _list: ItemList
var _filter: LineEdit
var _type: OptionButton
var _info: RichTextLabel
var _status: Label
var _distance := 0.0
var _distance_slider: HSlider
var _auto_play: PackedStringArray = []
var _seconds := 0.0
var _selftest := false
var _report := false
var _names: PackedStringArray = []
var _started_ms := 0
var _handles: Array[int] = []


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--play="):
			_auto_play = arg.substr(7).split(",", false)
		elif arg.begins_with("--distance="):
			_distance = float(arg.substr(11))
		elif arg.begins_with("--seconds="):
			_seconds = float(arg.substr(10))
		elif arg == "--selftest":
			_selftest = true
		elif arg == "--report":
			_report = true
	if not ClassDB.class_exists("GameAudio"):
		_fail("the openbfme extension has no GameAudio class; run build.bat")
		return
	_fs = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = _fs.mount_retail()
	if not mount.ok:
		_fail("mount failed:\n" + "\n".join(mount.errors))
		return
	_audio = ClassDB.instantiate("GameAudio")
	add_child(_audio)
	var booted: Dictionary = _audio.boot(_fs, {"seed": 1})
	if not booted.ok:
		_fail("audio boot failed:\n" + "\n".join(booted.errors))
		return
	print("SOUNDVIEW audio booted: %d events, voice pool %d" % [booted.events, booted.pool_size])
	_audio.set_listener(Vector3.ZERO, Vector3(0, 1, 0))
	if _selftest or not _auto_play.is_empty():
		_run_automation()
	else:
		_build_ui()


func _fail(message: String) -> void:
	printerr("SOUNDVIEW FAIL: ", message)
	get_tree().quit(1)


func _build_ui() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	var box := VBoxContainer.new()
	box.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(box)
	var top := HBoxContainer.new()
	box.add_child(top)
	_type = OptionButton.new()
	_type.add_item("All types", 0)
	for i in TYPE_NAMES.size():
		_type.add_item(TYPE_NAMES[i], i + 1)
	_type.item_selected.connect(func(_i): _refill())
	top.add_child(_type)
	_filter = LineEdit.new()
	_filter.placeholder_text = "filter (substring)"
	_filter.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_filter.text_changed.connect(func(_t): _refill())
	top.add_child(_filter)
	var play := Button.new()
	play.text = "Play"
	play.pressed.connect(func(): _play_selected(false))
	top.add_child(play)
	var play3d := Button.new()
	play3d.text = "Play 3D"
	play3d.pressed.connect(func(): _play_selected(true))
	top.add_child(play3d)
	var stop := Button.new()
	stop.text = "Stop all"
	stop.pressed.connect(func():
		_audio.stop_all("all")
		_audio.stop_music(false))
	top.add_child(stop)
	var split := HSplitContainer.new()
	split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	box.add_child(split)
	_list = ItemList.new()
	_list.custom_minimum_size = Vector2(420, 0)
	_list.item_selected.connect(_on_selected)
	_list.item_activated.connect(func(_i): _play_selected(false))
	split.add_child(_list)
	_info = RichTextLabel.new()
	_info.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_info.bbcode_enabled = false
	split.add_child(_info)
	var sliders := HBoxContainer.new()
	box.add_child(sliders)
	for name in ["sound", "voice", "music", "ambient"]:
		var l := Label.new()
		l.text = name
		sliders.add_child(l)
		var s := HSlider.new()
		s.min_value = 0.0
		s.max_value = 1.0
		s.step = 0.01
		s.value = _audio.get_volume(name)
		s.custom_minimum_size = Vector2(120, 0)
		s.value_changed.connect(func(v): _audio.set_volume(name, v))
		sliders.add_child(s)
	var dl := Label.new()
	dl.text = "3D distance"
	sliders.add_child(dl)
	_distance_slider = HSlider.new()
	_distance_slider.min_value = 0
	_distance_slider.max_value = 2000
	_distance_slider.step = 10
	_distance_slider.value = _distance
	_distance_slider.custom_minimum_size = Vector2(240, 0)
	_distance_slider.value_changed.connect(func(v): _distance = v)
	sliders.add_child(_distance_slider)
	_status = Label.new()
	box.add_child(_status)
	_refill()


func _refill() -> void:
	_list.clear()
	var type_id := _type.get_selected_id() - 1
	var needle := _filter.text.to_lower()
	_names = _audio.get_event_names(type_id)
	var shown := 0
	for n in _names:
		if needle.is_empty() or n.to_lower().contains(needle):
			_list.add_item(n)
			shown += 1
	_status.text = "%d of %d events" % [shown, _names.size()]


func _on_selected(index: int) -> void:
	var info: Dictionary = _audio.get_event_info(_list.get_item_text(index))
	var lines: Array[String] = []
	lines.append("%s  (%s)" % [info.name, TYPE_NAMES[info.sound_type]])
	lines.append("volume %.2f  priority %d  limit %d  range %.0f..%.0f" % [info.volume, info.priority, info.limit, info.min_range, info.max_range])
	lines.append("type bits 0x%x  control bits 0x%x" % [info.type_bits, info.control_bits])
	if not info.filename.is_empty():
		lines.append("Filename " + info.filename)
	lines.append("files:")
	for f in info.files:
		lines.append("  %s %s" % ["ok " if _fs.file_exists(f) else "MISSING", f])
	_info.text = "\n".join(lines)


func _play_selected(positional: bool) -> void:
	var sel := _list.get_selected_items()
	if sel.is_empty():
		return
	var name := _list.get_item_text(sel[0])
	var handle: int
	if positional:
		handle = _audio.play_sound_at(name, Vector3(_distance, 0, 0))
	else:
		handle = _audio.play_sound(name)
	_status.text = "%s -> handle %d" % [name, handle]


func _process(_delta: float) -> void:
	if _status != null and _audio != null:
		var stats: Dictionary = _audio.get_stats()
		_status.text = "playing: %s   (2D %d, 3D %d, streams %d, pool voices %d)" % [", ".join(_audio.get_playing()), stats.playing_2d, stats.playing_3d, stats.playing_streams, stats.voices_active]


func _wait(seconds: float) -> void:
	var until := Time.get_ticks_msec() + int(seconds * 1000.0)
	while Time.get_ticks_msec() < until:
		await get_tree().process_frame


# ---- automation: --play and --selftest -----------------------------------------------------------------------------------------------

func _first_playable(type_id: int) -> String:
	# the first event of the type that is not a default / fake and whose files all exist
	for n in _audio.get_event_names(type_id):
		var info: Dictionary = _audio.get_event_info(n)
		if info.files.is_empty():
			continue
		if info.type_bits & 0x200 != 0:
			continue
		var all_exist := true
		for f in info.files:
			if not _fs.file_exists(f):
				all_exist = false
				break
		if all_exist:
			return n
	return ""


func _run_automation() -> void:
	var failures := 0
	if _selftest:
		for type_id in [2, 1, 0, 3, 4]:
			var name := _first_playable(type_id)
			if name.is_empty():
				printerr("SOUNDVIEW FAIL: no playable %s" % TYPE_NAMES[type_id])
				failures += 1
				continue
			var handle: int = _audio.play_music(name) if type_id == 0 else _audio.play_sound(name)
			await _wait(0.4)
			var playing: bool = _audio.is_playing(handle)
			print("SOUNDVIEW selftest %-14s %-40s handle %d playing=%s" % [TYPE_NAMES[type_id], name, handle, playing])
			if not playing:
				failures += 1
		# the shell music and a MiscAudio sound
		var shell: int = _audio.play_shell_music(false)
		await _wait(0.4)
		print("SOUNDVIEW selftest shell music handle %d playing=%s track=%s" % [shell, _audio.is_music_playing(), _audio.get_music_track()])
		if not _audio.is_music_playing():
			failures += 1
		var click: int = _audio.play_misc("NoCanDoSound")
		await _wait(0.3)
		print("SOUNDVIEW selftest NoCanDoSound handle %d" % click)
	else:
		for n in _auto_play:
			var info: Dictionary = _audio.get_event_info(n)
			if info.is_empty():
				printerr("SOUNDVIEW FAIL: unknown event %s" % n)
				failures += 1
				continue
			var handle: int
			if info.sound_type == 0 or info.sound_type == 5:
				handle = _audio.play_music(n)
			elif _distance > 0.0:
				handle = _audio.play_sound_at(n, Vector3(_distance, 0, 0))
			else:
				handle = _audio.play_sound(n)
			print("SOUNDVIEW play %s (type %d) -> handle %d" % [n, info.sound_type, handle])
		await _wait(maxf(_seconds, 0.5))
		print("SOUNDVIEW playing now: ", _audio.get_playing())
	var report: Dictionary = _audio.get_report()
	if report.play_failures > 0 or report.unknown_events > 0:
		printerr("SOUNDVIEW FAIL: %d play failures, %d unknown events: %s" % [report.play_failures, report.unknown_events, str(report.errors)])
		failures += 1
	if _report:
		print("SOUNDVIEW report ", JSON.stringify(report, "  "))
		print("SOUNDVIEW stats ", JSON.stringify(_audio.get_stats(), "  "))
	print("SOUNDVIEW %s" % ["PASS" if failures == 0 else "FAIL"])
	_audio.shutdown()
	await _wait(0.3) # the engine releases stopped playbacks on its next mixes
	get_tree().quit(0 if failures == 0 else 1)
