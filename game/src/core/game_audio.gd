extends Node
## Lightweight audio bus: UI + combat SFX + adaptive music states.

const BootProfile = preload("res://src/core/boot_profile.gd")
const MusicDirectorScript = preload("res://src/core/music_director.gd")
const UserSettingsScript = preload("res://src/ui/user_settings.gd")

## The OUT-OF-MATCH music states. They are NOT the four in-match states below:
## the in-match ladder is per-faction and lives in RetailSliceAudio, whereas
## every state here is a single global declaration in the imported misc-audio
## table. This dictionary is the whole of the binding: it maps our state name
## onto the camelCase key the importer emits into `data/music.json`.
##
## The importer owns the source-side contract (which misc-audio entries are
## carried over, and which playlist each one resolves to) in
## importer/openbfme_importer/music_import.py -> MISC_AUDIO_MUSIC_KEYS.
## Nothing here decides WHICH track plays; it only decides which key to ask for.
const SHELL_MUSIC_STATE := "shell"
const SHELL_MUSIC_KEYS := {
	"shell": "shellLowLod",
	"submenu": "fullScreenSubMenu",
	"score": "scoreScreen",
	"credits": "credits",
}

var music_player: AudioStreamPlayer
var sfx_player: AudioStreamPlayer
var enabled: bool = true
var music_enabled: bool = true
var _streams: Dictionary = {}
var _music_state: String = ""
var combat_sfx_calls: int = 0
var _last_combat_sfx_at: int = 0

## Shell-music binding, resolved from the installed music pack through the same
## MusicDirector the in-match slice uses. Kept as public state because the
## runner asserts on the RESOLVED BINDING rather than on audible playback.
var shell_music_playlist: String = ""
var shell_music_paths: Array[String] = []
var shell_music_diagnostics: Array[String] = []
var shell_music_shuffles: bool = false
var shell_music_loops: bool = false
var _shell_director: RefCounted = null
var _shell_index: int = -1
var _shell_rng := RandomNumberGenerator.new()

func _ready() -> void:
	music_player = AudioStreamPlayer.new()
	music_player.name = "Music"
	add_child(music_player)
	music_player.finished.connect(_on_music_finished)
	sfx_player = AudioStreamPlayer.new()
	sfx_player.name = "Sfx"
	add_child(sfx_player)
	if not Events.content_reloaded.is_connected(_on_content_reloaded):
		Events.content_reloaded.connect(_on_content_reloaded)
	_load_pack_audio()
	_apply_music_volume()
	BootProfile.mark("autoload:GameAudio+GameState")

func _load_pack_audio() -> void:
	_streams.clear()
	for root in ContentDB.asset_roots:
		for key in ["explore", "battle", "victory", "defeat"]:
			for extension in ["wav", "ogg", "mp3"]:
				var p := root.path_join("audio/music/%s.%s" % [key, extension])
				var s := _load_audio_stream(p)
				if s:
					_streams["music_%s" % key] = s
					break
		var sfx_dir := root.path_join("audio/sfx")
		var dir := DirAccess.open(sfx_dir)
		if dir:
			dir.list_dir_begin()
			var n := dir.get_next()
			while n != "":
				if n.ends_with(".wav") or n.ends_with(".ogg") or n.ends_with(".mp3"):
					var full := sfx_dir.path_join(n)
					var s2 := _load_audio_stream(full)
					if s2:
						_streams[n.get_basename()] = s2
				n = dir.get_next()
			dir.list_dir_end()


func _load_audio_stream(path: String) -> AudioStream:
	if not FileAccess.file_exists(path) and not ResourceLoader.exists(path):
		return null
	if ResourceLoader.exists(path):
		return load(path) as AudioStream
	var absolute := ProjectSettings.globalize_path(path) if path.begins_with("res://") or path.begins_with("user://") else path
	match path.get_extension().to_lower():
		"wav":
			return AudioStreamWAV.load_from_file(absolute)
		"ogg":
			return AudioStreamOggVorbis.load_from_file(absolute)
		"mp3":
			return AudioStreamMP3.load_from_file(absolute)
	return null

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

func _on_content_reloaded() -> void:
	_load_pack_audio()
	# The shell binding is resolved from whatever music pack is installed, so a
	# content reload invalidates it. If the menu is currently playing shell
	# music, it restarts against the new pack rather than keeping a stream that
	# came from a pack that is no longer selected.
	var was_shell := SHELL_MUSIC_KEYS.has(_music_state)
	var previous := _music_state
	_shell_director = null
	shell_music_playlist = ""
	shell_music_paths.clear()
	if was_shell:
		_music_state = ""
		set_music_state(previous)


func set_music_state(state: String) -> void:
	if not music_enabled:
		return
	if state == _music_state:
		return
	_music_state = state
	if SHELL_MUSIC_KEYS.has(state):
		_start_shell_music(String(SHELL_MUSIC_KEYS[state]))
		return
	_shell_index = -1
	if state == "":
		music_player.stop()
		music_player.stream = null
		return
	var s: AudioStream = _streams.get("music_%s" % state, null)
	if s == null:
		return
	music_player.stream = s
	music_player.play()


func stop_music() -> void:
	## Hand the music off. Called when a match takes over (RetailSliceAudio owns
	## the in-match ladder and has its own players), so the shell playlist does
	## not keep playing underneath the battle. Setting the state to "" also means
	## a later return to the menu re-arms `set_music_state("shell")` rather than
	## being swallowed by the same-state guard.
	set_music_state("")


func is_playing_shell_music() -> bool:
	return (
		SHELL_MUSIC_KEYS.has(_music_state)
		and music_player != null
		and music_player.playing
	)


func music_state() -> String:
	return _music_state


func _resolve_shell_music(misc_audio_key: String = "shellLowLod") -> bool:
	## Bind the authored shell playlist out of the installed music pack.
	##
	## WHY THIS IS A LOOKUP rather than a filename convention: the source data
	## names the shell theme indirectly. A misc-audio entry names a playlist, and
	## a separate playlist entry names the ordered subsound list plus its play
	## mode. The importer resolves both hops
	## (importer/openbfme_importer/music_import.py) into `data/music.json`'s
	## `shell` object, so the menu plays whatever the imported edition authored
	## instead of a track this project picked.
	##
	## Fails CLOSED and says why: with no music pack installed the menu is
	## silent, and `shell_music_diagnostics` states that, rather than
	## substituting some other track and calling it the shell theme.
	shell_music_diagnostics.clear()
	shell_music_playlist = ""
	shell_music_paths = []
	shell_music_shuffles = false
	shell_music_loops = false
	_shell_director = null
	var content_db: Node = get_node_or_null("/root/ContentDB")
	if content_db == null:
		shell_music_diagnostics.append("shell music: ContentDB is not available")
		return false
	var document: Dictionary = content_db.get("music_document")
	if document == null or document.is_empty():
		shell_music_diagnostics.append("shell music: no music pack is installed")
		return false
	var director: RefCounted = MusicDirectorScript.new()
	if not director.configure(document):
		shell_music_diagnostics.append_array(director.diagnostics)
		return false
	_shell_director = director
	shell_music_playlist = String(director.shell_playlist_id(misc_audio_key))
	if shell_music_playlist == "":
		shell_music_diagnostics.append(
			"shell music: the music document declares no MiscAudio '%s' playlist" % misc_audio_key
		)
		return false
	shell_music_shuffles = bool(director.shell_shuffles(misc_audio_key))
	shell_music_loops = bool(director.shell_loops(misc_audio_key))
	var paths: Array = director.shell_track_paths(misc_audio_key)
	for path in paths:
		shell_music_paths.append(String(path))
	if shell_music_paths.is_empty():
		shell_music_diagnostics.append(
			"shell music: playlist '%s' resolved no playable track file" % shell_music_playlist
		)
		return false
	return true


func _start_shell_music(misc_audio_key: String = "shellLowLod") -> void:
	if not _resolve_shell_music(misc_audio_key):
		music_player.stop()
		music_player.stream = null
		return
	_apply_music_volume()
	# PLAY_ONE means "start ONE of these", so the first entry is picked from the
	# pool rather than always being subsound zero. Without the flag the list is
	# an ordered sequence and starts at its head.
	_shell_index = (
		_shell_rng.randi_range(0, shell_music_paths.size() - 1) if shell_music_shuffles
		else 0
	)
	_play_shell_index(_shell_index)


func _play_shell_index(index: int) -> void:
	if index < 0 or index >= shell_music_paths.size():
		return
	var stream := _load_audio_stream(shell_music_paths[index])
	if stream == null:
		shell_music_diagnostics.append(
			"shell music: track failed to load: %s" % shell_music_paths[index]
		)
		return
	# A LOOP playlist repeats through this autoload's `finished` handler, not
	# through the stream's own loop point, so a PLAY_ONE+LOOP pool re-picks and
	# a plain ordered list advances. Stream-level looping would pin it to one
	# leaf forever.
	if stream is AudioStreamMP3:
		(stream as AudioStreamMP3).loop = false
	elif stream is AudioStreamOggVorbis:
		(stream as AudioStreamOggVorbis).loop = false
	_shell_index = index
	music_player.stream = stream
	music_player.play()


func _on_music_finished() -> void:
	if not SHELL_MUSIC_KEYS.has(_music_state):
		return
	if shell_music_paths.is_empty():
		return
	if shell_music_shuffles:
		# PLAY_ONE + LOOP over a one-entry pool is retail's ROTWK shell, so a
		# single-track playlist simply repeats; a larger pool re-picks.
		_play_shell_index(_shell_rng.randi_range(0, shell_music_paths.size() - 1))
		return
	var next := _shell_index + 1
	if next >= shell_music_paths.size():
		if not shell_music_loops:
			return
		next = 0
	_play_shell_index(next)


func _apply_music_volume() -> void:
	if music_player == null or not is_instance_valid(music_player):
		return
	var settings: Dictionary = UserSettingsScript.load_audio()
	music_player.volume_db = UserSettingsScript.volume_to_db(
		float(settings["music_volume"]), bool(settings["muted"])
	)


func set_music_volume(value: float, muted: bool = false) -> void:
	## Live volume/mute for the shell player, so the options screen affects the
	## menu music the same way it affects the in-match one.
	if music_player == null or not is_instance_valid(music_player):
		return
	music_player.volume_db = UserSettingsScript.volume_to_db(clampf(value, 0.0, 1.0), muted)

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

## See events.gd:_init - per-autoload compile attribution for the boot profiler.
## No-op unless boot profiling is on.
func _init() -> void:
	BootProfile.mark("autoload_compiled:GameAudio")
