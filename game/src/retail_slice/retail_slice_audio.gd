class_name RetailSliceAudio
extends Node
## Loads only contained assets from the selected private pack and consumes
## deterministic simulation intents. Nothing here depends on the retail install.

const UserSettingsScript = preload("res://src/ui/user_settings.gd")

var pack_root := ""
var playback_enabled := true
var music_player: AudioStreamPlayer
var voice_player: AudioStreamPlayer
var music_streams: Dictionary = {}
var voice_streams: Dictionary = {"select": [], "attack": []}
var intent_log: Array[Dictionary] = []
var current_music_state := ""
var _next_event_index := 0
var music_volume := UserSettingsScript.DEFAULT_MUSIC_VOLUME
var voice_sfx_volume := UserSettingsScript.DEFAULT_VOICE_SFX_VOLUME
var muted := UserSettingsScript.DEFAULT_MUTED


func configure(selected_pack_root: String, enable_playback: bool = true) -> bool:
	pack_root = selected_pack_root
	playback_enabled = enable_playback
	_ensure_players()
	music_streams.clear()
	voice_streams = {"select": [], "attack": []}
	_load_user_settings()
	_load_music()
	_load_voices()
	return has_complete_audio_closure()


func _ensure_players() -> void:
	if music_player == null or not is_instance_valid(music_player):
		music_player = AudioStreamPlayer.new()
		music_player.name = "RetailMusic"
		add_child(music_player)
	if voice_player == null or not is_instance_valid(voice_player):
		voice_player = AudioStreamPlayer.new()
		voice_player.name = "RetailVoice"
		add_child(voice_player)
	_apply_volume_levels()


func _load_user_settings() -> void:
	var settings: Dictionary = UserSettingsScript.load_audio()
	music_volume = float(settings["music_volume"])
	voice_sfx_volume = float(settings["voice_sfx_volume"])
	muted = bool(settings["muted"])
	_apply_volume_levels()


func set_music_volume(value: float, persist: bool = false) -> void:
	music_volume = clampf(value, 0.0, 1.0)
	_apply_volume_levels()
	if persist:
		UserSettingsScript.save_audio(music_volume, voice_sfx_volume, muted)


func get_music_volume() -> float:
	return music_volume


func set_voice_sfx_volume(value: float, persist: bool = false) -> void:
	voice_sfx_volume = clampf(value, 0.0, 1.0)
	_apply_volume_levels()
	if persist:
		UserSettingsScript.save_audio(music_volume, voice_sfx_volume, muted)


func get_voice_sfx_volume() -> float:
	return voice_sfx_volume


func set_voice_volume(value: float, persist: bool = false) -> void:
	set_voice_sfx_volume(value, persist)


func get_voice_volume() -> float:
	return get_voice_sfx_volume()


func set_muted(value: bool, persist: bool = false) -> void:
	muted = value
	_apply_volume_levels()
	if persist:
		UserSettingsScript.save_audio(music_volume, voice_sfx_volume, muted)


func is_muted() -> bool:
	return muted


func _apply_volume_levels() -> void:
	if music_player != null and is_instance_valid(music_player):
		music_player.volume_db = UserSettingsScript.volume_to_db(music_volume, muted)
	if voice_player != null and is_instance_valid(voice_player):
		voice_player.volume_db = UserSettingsScript.volume_to_db(voice_sfx_volume, muted)


func has_complete_audio_closure() -> bool:
	return music_streams.has("explore") and music_streams.has("battle") and music_streams.has("victory") and music_streams.has("defeat") and (voice_streams["select"] as Array).size() > 0 and (voice_streams["attack"] as Array).size() > 0


func _load_music() -> void:
	for state in ["explore", "battle", "victory", "defeat"]:
		var path := pack_root.path_join("assets/audio/music/%s.mp3" % state)
		var stream := _load_stream(path)
		if stream != null:
			music_streams[state] = stream


func _load_voices() -> void:
	var directory_path := pack_root.path_join("assets/audio/voice/gondor-soldier")
	var directory := DirAccess.open(directory_path)
	if directory == null:
		return
	var select_paths: Array[String] = []
	var attack_paths: Array[String] = []
	directory.list_dir_begin()
	var file_name := directory.get_next()
	while file_name != "":
		if not directory.current_is_dir() and file_name.to_lower().ends_with(".wav"):
			if file_name.to_lower().begins_with("gusoldg_voisel") or file_name.to_lower() == "gugoswo_voise2a.wav":
				select_paths.append(directory_path.path_join(file_name))
			elif file_name.to_lower().begins_with("gusoldg_voiat"):
				attack_paths.append(directory_path.path_join(file_name))
		file_name = directory.get_next()
	directory.list_dir_end()
	select_paths.sort()
	attack_paths.sort()
	for path in select_paths:
		var stream := _load_stream(path)
		if stream != null:
			(voice_streams["select"] as Array).append(stream)
	for path in attack_paths:
		var stream := _load_stream(path)
		if stream != null:
			(voice_streams["attack"] as Array).append(stream)


func sync_events(events: Array[Dictionary]) -> void:
	while _next_event_index < events.size():
		var event: Dictionary = events[_next_event_index]
		_consume_event(event)
		_next_event_index += 1


func _consume_event(event: Dictionary) -> void:
	var kind := String(event.get("kind", ""))
	if kind.begins_with("music."):
		_set_music(kind.trim_prefix("music."))
	elif kind == "voice.select":
		_play_voice("select", int(event.get("sequence", 0)))
	elif kind == "voice.attack":
		_play_voice("attack", int(event.get("sequence", 0)))
	intent_log.append(event.duplicate(true))


func _set_music(state: String) -> void:
	current_music_state = state
	if not playback_enabled or not music_streams.has(state) or music_player == null:
		return
	music_player.stream = music_streams[state]
	music_player.play()


func _play_voice(kind: String, sequence: int) -> void:
	var choices: Array = voice_streams.get(kind, [])
	if choices.is_empty() or not playback_enabled or voice_player == null:
		return
	var index := posmod(sequence - 1, choices.size())
	voice_player.stream = choices[index] as AudioStream
	voice_player.play()


func _load_stream(path: String) -> AudioStream:
	var content_db := get_node_or_null("/root/ContentDB")
	if content_db == null or not content_db.has_method("is_resolved_asset_path") or not bool(content_db.call("is_resolved_asset_path", path)):
		return null
	match path.get_extension().to_lower():
		"wav":
			return AudioStreamWAV.load_from_file(path)
		"ogg":
			return AudioStreamOggVorbis.load_from_file(path)
		"mp3":
			return AudioStreamMP3.load_from_file(path)
	return null


func count_voice_kind(kind: String) -> int:
	return (voice_streams.get(kind, []) as Array).size()


func stop_all() -> void:
	if music_player != null:
		music_player.stop()
		music_player.stream = null
	if voice_player != null:
		voice_player.stop()
		voice_player.stream = null
	music_streams.clear()
	voice_streams = {"select": [], "attack": []}


func dispose() -> void:
	stop_all()
	if music_player != null and is_instance_valid(music_player):
		music_player.free()
		music_player = null
	if voice_player != null and is_instance_valid(voice_player):
		voice_player.free()
		voice_player = null


func _exit_tree() -> void:
	stop_all()
