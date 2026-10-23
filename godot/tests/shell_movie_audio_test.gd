extends SceneTree

class Audio extends Node:
	var played: Array = []
	var removed: Array = []
	var fades: Array = []
	var music := "MenuMusic"
	var next_handle := 5
	func get_music_track() -> String: return music
	func stop_music(fade: bool) -> void:
		fades.append(fade)
		music = ""
	func play_music(event: String) -> int:
		music = event
		return 99
	func play_sound(event: String) -> int:
		played.append(event)
		next_handle += 1
		return next_handle
	func remove_event(handle: int) -> void: removed.append(handle)

class World extends Node:
	func get_movie(title: String) -> Dictionary:
		return {"ok": true, "audio_events": [title + "_Music", title] if title in ["Moves", "Heroes"] else []}

func movie(title: String, callback := "", frame := 0) -> Dictionary:
	return {"path": "clip", "title": title, "completion": callback, "frame": frame, "frames": 100, "error": ""}

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
	player.free()
	audio.free()
	world.free()
	print("SHELL MOVIE AUDIO PASS")
	quit()
