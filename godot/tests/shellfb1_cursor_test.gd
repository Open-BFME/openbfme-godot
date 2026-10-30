## Initial native cursor diagnostics for lane SHELLFB1.
## Run with ROTWK_INSTALL and BFME2_INSTALL set:
## godot --headless --path godot --script res://tests/shellfb1_cursor_test.gd
extends SceneTree

const EXIT_SKIP := 77

var _failures := 0


func _initialize() -> void:
	var code := _run()
	print("SHELLFB1 CURSOR %s" % ["PASS" if code == 0 else ("SKIP" if code == EXIT_SKIP else "FAIL")])
	quit(code)


func _check(ok: bool, message: String) -> void:
	print("  ", "ok   " if ok else "FAIL ", message)
	if not ok:
		_failures += 1


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL")
		return EXIT_SKIP
	for type_name in ["RetailFileSystem", "GameWorld", "AptMenuPlayer"]:
		if not ClassDB.class_exists(type_name):
			_check(false, "the native extension registers %s" % type_name)
			return 1

	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mounted: Dictionary = fs.mount_retail()
	_check(mounted.get("ok", false), "normal RotWK/BFME2 mount: %s" % str(mounted.get("errors", [])))
	if not mounted.get("ok", false):
		return 1
	var world: Node3D = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	_check(setup.get("ok", false), "world setup: %s" % str(setup.get("errors", [])))
	if not setup.get("ok", false):
		return 1

	var player: Node = ClassDB.instantiate("AptMenuPlayer")
	player.set_auto_process(false)
	root.add_child(player)
	var boot: Dictionary = player.boot_shell(fs, world, {"seed": 7})
	_check(boot.get("ok", false), "shell boot before a gameplay HUD: %s" % str(boot.get("errors", [])))
	if not boot.get("ok", false):
		return 1

	var cursor: Dictionary = player.get_cursor_image() if player.has_method("get_cursor_image") else {}
	_check(cursor.get("name", "") == "Arrow", "the first shell cursor is Arrow")
	_check(cursor.get("texture") != null, "the first shell cursor has an image")
	_check(not cursor.get("hidden", true), "the first shell cursor is visible")
	var report: Dictionary = player.get_report()
	var cursor_errors: Array = report.get("cursor_errors", [])
	_check(cursor_errors.is_empty(), "the first shell cursor has no resource errors: %s" % str(cursor_errors))

	var old_rotwk_install := OS.get_environment("ROTWK_INSTALL")
	var missing_root := ProjectSettings.globalize_path("user://shellfb1-missing-install-%d" % OS.get_process_id())
	OS.set_environment("ROTWK_INSTALL", missing_root)
	var bad_fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var bad_mount: Dictionary = bad_fs.mount_retail()
	_check(not bad_mount.get("ok", false) and not bad_mount.get("errors", []).is_empty(),
		"a bad configured install root is reported by the mount")
	var bad_player: Node = ClassDB.instantiate("AptMenuPlayer")
	bad_player.set_auto_process(false)
	root.add_child(bad_player)
	var bad_boot: Dictionary = bad_player.boot_shell(bad_fs, world, {})
	_check(not bad_boot.get("ok", false) and not bad_boot.get("errors", []).is_empty(),
		"a shell with a bad configured install root fails explicitly")
	OS.set_environment("ROTWK_INSTALL", old_rotwk_install)

	var bfme1_install := OS.get_environment("BFME1_INSTALL")
	if not bfme1_install.is_empty():
		# Reusing a BFME2 presentation source must not leave its install root or role behind.
		var reused_fs: RefCounted = fs.create_bfme2_shell()
		_check(reused_fs != null, "BFME2 presentation source is available for remount")
		if reused_fs != null:
			var first_mount: Dictionary = reused_fs.mount_bfme1_path(bfme1_install)
			_check(first_mount.get("ok", false), "BFME1 remount: %s" % str(first_mount.get("errors", [])))
			_check(reused_fs.install_root() == bfme1_install, "BFME1 remount uses its own install root")
			_check(not reused_fs.is_shell_source(), "BFME1 remount is a normal game source")
			_check(reused_fs.create_bfme2_shell() == null, "BFME1 remount retains no BFME2 presentation root")
			var failed_mount: Dictionary = reused_fs.mount_bfme1_path(missing_root)
			_check(not failed_mount.get("ok", false), "missing BFME1 remount fails")
			_check(reused_fs.install_root().is_empty(), "failed BFME1 remount clears the install root")

	return 0 if _failures == 0 else 1
