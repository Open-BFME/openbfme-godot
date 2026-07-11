extends Node
## Lightweight audio bus: UI + combat SFX + adaptive music states.

var music_player: AudioStreamPlayer
var sfx_player: AudioStreamPlayer
var enabled: bool = true
var music_enabled: bool = true
var _streams: Dictionary = {}
var _music_state: String = ""
var combat_sfx_calls: int = 0
var _last_combat_sfx_at: int = 0

func _ready() -> void:
	music_player = AudioStreamPlayer.new()
	music_player.name = "Music"
	add_child(music_player)
	sfx_player = AudioStreamPlayer.new()
	sfx_player.name = "Sfx"
	add_child(sfx_player)
	_load_pack_audio()

func _load_pack_audio() -> void:
	for root in ContentDB.asset_roots:
		for key in ["explore", "battle", "victory"]:
			var p := root.path_join("audio/music/%s.wav" % key)
			if FileAccess.file_exists(p):
				var s: AudioStream = load(p) as AudioStream
				if s:
					_streams["music_%s" % key] = s
		var sfx_dir := root.path_join("audio/sfx")
		var dir := DirAccess.open(sfx_dir)
		if dir:
			dir.list_dir_begin()
			var n := dir.get_next()
			while n != "":
				if n.ends_with(".wav") or n.ends_with(".ogg"):
					var full := sfx_dir.path_join(n)
					var s2: AudioStream = load(full) as AudioStream
					if s2:
						_streams[n.get_basename()] = s2
				n = dir.get_next()
			dir.list_dir_end()

func play_sfx(key: String, pitch: float = 1.0) -> void:
	if not enabled:
		return
	var s: AudioStream = _streams.get(key, null)
	if s == null:
		for k in _streams.keys():
			if String(k).contains(key):
				s = _streams[k]
				break
	if s == null:
		return
	sfx_player.stream = s
	sfx_player.pitch_scale = pitch
	sfx_player.play()

func set_music_state(state: String) -> void:
	if not music_enabled:
		return
	if state == _music_state:
		return
	_music_state = state
	var s: AudioStream = _streams.get("music_%s" % state, null)
	if s == null:
		return
	music_player.stream = s
	music_player.play()

func play_ui_click() -> void:
	play_sfx("click-ui-01", 1.0)

func play_combat() -> void:
	combat_sfx_calls += 1
	_last_combat_sfx_at = Time.get_ticks_msec()
	if not enabled:
		return
	if _streams.has("sword-hit-01"):
		play_sfx("sword-hit-01", randf_range(0.9, 1.1))
		return
	for k in _streams.keys():
		var ks := String(k)
		if ks.contains("sword") or ks.contains("hit") or ks.contains("impact") or ks.contains("clang"):
			play_sfx(ks, randf_range(0.9, 1.1))
			return
