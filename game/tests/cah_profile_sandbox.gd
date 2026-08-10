extends RefCounted
## Keeps a runner's Create-a-Hero heroes out of the player's store.
##
## HEADLESS RUNNERS SHARE ONE `user://` WITH THE INSTALLED GAME. A gate that
## saved and deleted heroes was therefore saving and deleting the PLAYER'S
## heroes, and it did exactly that - a full night of runs left a real store
## empty. Nothing about the gates was wrong except where they were pointed.
##
## `open()` points the profile store at a scratch directory named for the runner
## and empties it, so a run starts from a known state and can never reach the
## real one. It also records what the real store held, so `assert_untouched()`
## can prove afterwards that the run did not go near it - the property that was
## being assumed rather than checked.
##
## Crash-safe by construction: the override is set BEFORE the first profile call,
## so a run that dies half way through has still never named the real directory.

const CahHeroes = preload("res://src/content/cah_heroes.gd")

var _real_listing: PackedStringArray = []
var _scratch := ""


func open(runner_name: String) -> void:
	_real_listing = _listing(CahHeroes.PROFILE_DIR)
	_scratch = "user://cah-heroes-sandbox-%s" % runner_name
	OS.set_environment(CahHeroes.PROFILE_DIR_ENV, _scratch)
	_empty(_scratch)


func close() -> void:
	## The scratch store is emptied, not kept: a stale hero from an earlier run
	## is the same contamination in a different directory.
	_empty(_scratch)
	OS.set_environment(CahHeroes.PROFILE_DIR_ENV, "")


func real_store_untouched() -> bool:
	return _listing(CahHeroes.PROFILE_DIR) == _real_listing


func real_store_description() -> String:
	var now := _listing(CahHeroes.PROFILE_DIR)
	return "real store held %d files (%s), now %d (%s)" % [
		_real_listing.size(), ", ".join(_real_listing), now.size(), ", ".join(now)
	]


func scratch_dir() -> String:
	return _scratch


func _listing(path: String) -> PackedStringArray:
	## NAMES ARE NOT ENOUGH. A run that rewrote a hero in place - same file, new
	## contents - would leave the listing identical and the player's hero
	## replaced, so each entry carries the bytes' own fingerprint.
	var dir := DirAccess.open(path)
	if dir == null:
		return PackedStringArray()
	var names := dir.get_files()
	names.sort()
	var fingerprints := PackedStringArray()
	for file_name in names:
		fingerprints.append("%s:%s" % [file_name, _fingerprint("%s/%s" % [path, file_name])])
	return fingerprints


func _fingerprint(path: String) -> String:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return "unreadable"
	var bytes := file.get_buffer(file.get_length())
	file.close()
	return "%d/%s" % [bytes.size(), bytes.get_string_from_utf8().sha256_text()]


func _empty(path: String) -> void:
	var dir := DirAccess.open(path)
	if dir == null:
		return
	for file_name in dir.get_files():
		dir.remove(file_name)
