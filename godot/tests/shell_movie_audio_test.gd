extends SceneTree

class Audio extends Node:
	var played: Array = []
	var removed: Array = []
	var fades: Array = []
	var music := "MenuMusic"
	var next_handle := 5
	var forced_handle := -1
	func get_music_track() -> String: return music
	func stop_music(fade: bool) -> void:
		fades.append(fade)
		music = ""
	func play_music(event: String) -> int:
		music = event
		return 99
	func play_sound(event: String) -> int:
		played.append(event)
		if forced_handle != -1: return forced_handle
		next_handle += 1
		return next_handle
	func remove_event(handle: int) -> void: removed.append(handle)

class World extends Node:
	func get_movie(_title: String) -> Dictionary:
		assert(false, "Per-frame movie audio must never query the waiting world accessor")
		return {}

func movie(title: String, callback := "", frame := 0) -> Dictionary:
	return {"path": "clip", "title": title, "completion": callback, "frame": frame, "frames": 100, "error": "", "audio_events": PackedStringArray([title + "_Music", title]) if title in ["Moves", "Heroes"] else PackedStringArray()}

func _initialize() -> void:
	var audio := Audio.new()
	var world := World.new()
	var player = load("res://scripts/shell_movie_audio.gd").new()
	player.audio = audio
	player.world = world
	player.sync([movie("Open", "TutortialOpenBookDone")])
	assert(audio.fades == [true] and audio.music == "")
	player.sync([]) # book is open, no movie: do not resume music between lessons
	assert(audio.music == "")
	player.sync([movie("Moves", "TurtorialMovieDone")])
	player.sync([movie("Moves", "TurtorialMovieDone", 25)])
	assert(audio.played == ["Moves_Music", "Moves"])
	player.sync([movie("Heroes", "TurtorialMovieDone")])
	assert(audio.removed == [6, 7])
	assert(audio.played == ["Moves_Music", "Moves", "Heroes_Music", "Heroes"])
	player.sync([movie("Heroes", "TurtorialMovieDone", 99)])
	assert(audio.removed == [6, 7, 8, 9])
	player.sync([movie("Close", "TutorialCloseBookDone")])
	player.sync([])
	assert(audio.music == "MenuMusic")
	assert(audio.fades == [true])
	assert(player.errors.is_empty())
	assert(player.unverified == PackedStringArray(["[S-5017] Battle School fades menu music; exact view-wide SOUNDFADE timing is unported"]))
	# Leaving for gameplay stops narration without restarting menu music.
	player.sync([movie("Open", "TutortialOpenBookDone")])
	player.sync([movie("Moves", "TurtorialMovieDone")])
	player._clear(false)
	assert(audio.removed == [6, 7, 8, 9, 10, 11])
	assert(audio.music == "")
	# Reserved results are intentional skips, never playing voices or errors.
	for handle in [1, 2, 3, 4]:
		audio.forced_handle = handle
		player.sync([movie("Moves")])
		player.sync([])
		assert(player.errors.is_empty())
	assert(audio.removed == [6, 7, 8, 9, 10, 11])
	# AHSV_Error still surfaces a real playback failure for each audio event.
	audio.forced_handle = 0
	player.sync([movie("Moves")])
	assert(player.errors == ["Moves: could not play Moves_Music", "Moves: could not play Moves"])
	player.free()
	audio.free()
	world.free()
	print("SHELL MOVIE AUDIO PASS")
	quit()
