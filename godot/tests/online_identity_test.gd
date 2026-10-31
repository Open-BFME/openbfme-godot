## OpenBFME. GPL-3.0.
extends SceneTree
## GDScript assert() only logs a SCRIPT ERROR and the run carries on, so a failed one still ended in PASS / exit 0. Every check goes through
## _check: a failure is recorded and printed as FAIL, and _finish quits 1 when any failed.
var _failures: Array[String] = []

func _check(ok: bool, reason := "") -> bool:
	if not ok:
		_failures.append(reason)
		var stack := get_stack()
		printerr("FAIL: ", reason, " (line ", stack[1].line if stack.size() > 1 else 0, ")")
	return ok

func _finish(pass_message: String) -> void:
	if _failures.is_empty():
		print(pass_message)
		quit()
		return
	printerr(_failures.size(), " check(s) failed")
	quit(1)


func _init() -> void:
	var identity: GDScript = load("res://scripts/online_identity.gd")
	var path := "user://identity-test.txt"
	if FileAccess.file_exists(path):
		DirAccess.remove_absolute(path)
	var first: Dictionary = identity.install_id(path)
	_check(not first.has("error"))
	_check(RegEx.create_from_string("^[0-9a-f]{32}$").search(first.install) != null)
	_check(identity.install_id(path) == first)
	# ONLINEMENU-1: a lock directory an older build left behind (a crash while it held it) does not stop the sign-in: the same id is read
	var lock_path := "user://.install_id.lock"
	if not DirAccess.dir_exists_absolute(lock_path):
		_check(DirAccess.make_dir_absolute(lock_path) == OK)
	_check(identity.install_id(path) == first)
	_check(FileAccess.get_file_as_string(path).strip_edges() == first.install)
	_check(DirAccess.remove_absolute(lock_path) == OK)
	_check(identity.device_hash("test-machine") == "e1335feb0e81f4fb77a4d54489b0573532c99105ce3d01de8080d04d18b0a0cb")
	_check(identity.device_hash("") == "")
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_string("not-an-install")
	file.close()
	var damaged: Dictionary = identity.install_id(path)
	_check(damaged.has("error"))
	_check(str(damaged.error).contains(ProjectSettings.globalize_path(path)) and str(damaged.error).contains("delete that file")) # DEVINPR-1
	_check(FileAccess.get_file_as_string(path) == "not-an-install")
	# DEVINPR-1: the id is written beside the file and renamed into place: nothing but the id file is left behind
	DirAccess.remove_absolute(path)
	var made: Dictionary = identity.install_id(path)
	_check(not made.has("error"))
	for name in DirAccess.get_files_at("user://"):
		_check(not (name.begins_with("identity-test.txt.") and name.ends_with(".tmp")))
	_check(FileAccess.get_file_as_string(path).strip_edges() == made.install)
	_check(identity.install_id("user://missing-parent/identity.txt").has("error"))
	DirAccess.remove_absolute(path)
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--lobby="):
			_signin.call_deferred(arg.substr(8))
			return
	_finish("IDENTITY PASS")


func _signin(url: String) -> void:
	if not ClassDB.class_exists("AptMenuPlayer"):
		_check(load("res://openbfme.gdextension") != null)
	var client: Node = load("res://scripts/online_lobby.gd").new()
	client.lobby_url = url
	client.player_name = "IdentityTest"
	client.join_id = "0123456789abcdef"
	client.profile = "0".repeat(32)
	var notices := []
	var failures := []
	client.notice.connect(func(message): notices.append(message))
	client.failed.connect(func(message): failures.append(message))
	root.add_child(client)
	client.begin()
	for attempt in range(200):
		if not failures.is_empty():
			break
		await create_timer(0.05).timeout
	_check(notices == ["A pending warning"])
	_check(failures == ["Test complete (suspended)"])
	_finish("IDENTITY SIGNIN PASS")
