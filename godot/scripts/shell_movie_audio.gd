## BFME1's embedded videos use the same separate VideoPlayer audio events as
## movie_player.gd. WindowTransitions.ini MainMenuToBattleSchool requests a
## SOUNDFADE of TACTICAL with LeaveSilent=Yes; here menu music fades out for
## the book session. Its exact view-wide 30-frame transition remains unported.
extends Node

var shell: Node
var world: Node
var audio: Node
var menu_active: Callable
var errors: Array[String] = []
var unverified := PackedStringArray(["[S-5017] Battle School fades menu music; exact view-wide SOUNDFADE timing is unported"])
var _streams := {}
var _book_open := false
var _closing := false
var _music_before := ""

func _process(_delta: float) -> void:
	if menu_active.is_valid() and not menu_active.call():
		_clear(false)
		return
	sync(shell.get_movies())

func sync(movies: Array) -> void:
	var live := {}
	var closing_visible := false
	for movie in movies:
		if not str(movie.get("error", "")).is_empty():
			continue
		var callback := str(movie.get("completion", ""))
		if callback == "TutortialOpenBookDone" and not _book_open:
			_book_open = true
			_music_before = audio.get_music_track()
			audio.stop_music(true)
		if callback == "TutorialCloseBookDone":
			_closing = true
			closing_visible = true
		var path := str(movie.path)
		var title := str(movie.title)
		var frame := int(movie.frame)
		if frame == int(movie.frames) - 1 or bool(movie.get("completed", false)):
			continue
		live[path] = true
		if _streams.has(path):
			var previous: Dictionary = _streams[path]
			if previous.title != title or frame < int(previous.frame):
				_stop(path)
		if not _streams.has(path):
			var handles: Array = []
			var info: Dictionary = world.get_movie(title)
			if not info.get("ok", false):
				_error(title + ": " + str(info.get("error", "movie lookup failed")))
			else:
				for event in info.audio_events:
					var handle: int = audio.play_sound(event)
					# AudioHandleSpecialValues are 0..4, not playing voices.
					if handle <= 4:
						_error(title + ": could not play " + str(event))
					else:
						handles.append(handle)
			_streams[path] = {"title": title, "frame": frame, "handles": handles}
		_streams[path].frame = frame
	for path in _streams.keys():
		if not live.has(path):
			_stop(path)
	# Keep music silent between lessons/page flips, including movies without
	# narration. The close callback removes the clip only after it has finished.
	if _closing and not closing_visible:
		_clear(true)

func _stop(path: String) -> void:
	for handle in _streams[path].handles:
		audio.remove_event(handle)
	_streams.erase(path)

func _clear(restore_music: bool) -> void:
	for path in _streams.keys():
		_stop(path)
	if restore_music and _book_open and not _music_before.is_empty():
		audio.play_music(_music_before)
	_book_open = false
	_closing = false
	_music_before = ""

func _error(message: String) -> void:
	errors.append(message)
	printerr("MOVIE AUDIO ERROR ", message)

func _exit_tree() -> void:
	_clear(false)
