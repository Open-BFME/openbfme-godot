## OpenBFME. GPL-3.0.
extends SceneTree


func _init() -> void:
	var identity: GDScript = load("res://scripts/online_identity.gd")
	var path := "user://identity-test.txt"
	if FileAccess.file_exists(path):
		DirAccess.remove_absolute(path)
	var first: Dictionary = identity.install_id(path)
	assert(not first.has("error"))
	assert(RegEx.create_from_string("^[0-9a-f]{32}$").search(first.install) != null)
	assert(identity.install_id(path) == first)
	# ONLINEMENU-1: a lock directory an older build left behind (a crash while it held it) does not stop the sign-in: the same id is read
	var lock_path := "user://.install_id.lock"
	if not DirAccess.dir_exists_absolute(lock_path):
		assert(DirAccess.make_dir_absolute(lock_path) == OK)
	assert(identity.install_id(path) == first)
	assert(FileAccess.get_file_as_string(path).strip_edges() == first.install)
	assert(DirAccess.remove_absolute(lock_path) == OK)
	assert(identity.device_hash("test-machine") == "e1335feb0e81f4fb77a4d54489b0573532c99105ce3d01de8080d04d18b0a0cb")
	assert(identity.device_hash("") == "")
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_string("not-an-install")
	file.close()
	var damaged: Dictionary = identity.install_id(path)
	assert(damaged.has("error"))
	assert(str(damaged.error).contains(ProjectSettings.globalize_path(path)) and str(damaged.error).contains("delete that file")) # DEVINPR-1
	assert(FileAccess.get_file_as_string(path) == "not-an-install")
	# DEVINPR-1: the id is written beside the file and renamed into place: nothing but the id file is left behind
	DirAccess.remove_absolute(path)
	var made: Dictionary = identity.install_id(path)
	assert(not made.has("error"))
	for name in DirAccess.get_files_at("user://"):
		assert(not (name.begins_with("identity-test.txt.") and name.ends_with(".tmp")))
	assert(FileAccess.get_file_as_string(path).strip_edges() == made.install)
	assert(identity.install_id("user://missing-parent/identity.txt").has("error"))
	DirAccess.remove_absolute(path)
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--lobby="):
			_signin.call_deferred(arg.substr(8))
			return
	print("IDENTITY PASS")
	quit(0)


func _signin(url: String) -> void:
	if not ClassDB.class_exists("AptMenuPlayer"):
		assert(load("res://openbfme.gdextension") != null)
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
	assert(notices == ["A pending warning"])
	assert(failures == ["Test complete (suspended)"])
	print("IDENTITY SIGNIN PASS")
	quit(0)
