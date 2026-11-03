## An INI error raised inside the extension is reported, not a crash (MSVC: INIException must have one layout on both sides of the godot-cpp boundary).
## A synthetic mod folder adds an object INI with an unknown Weapon field; the map's object loader parses it through INI, which wraps the error in
## `catch (INIException &)` and rethrows it. No retail bytes are written.
extends SceneTree

const MAP := "map wor fangorn"

func _initialize() -> void:
	call_deferred("_run")

func _fail(message: String) -> void:
	push_error("FAIL: " + message)
	quit(1)

func _run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL")
		quit(77)
		return
	for cls in ["RetailFileSystem", "MapObjectBuilder"]:
		if not ClassDB.class_exists(cls):
			_fail(cls + " is not registered (is the openbfme extension built and loaded?)")
			return
	create_timer(300).timeout.connect(func(): _fail("ini error test timed out"))
	var temp := OS.get_environment("TMPDIR")
	if temp.is_empty():
		temp = OS.get_environment("TEMP")
	if temp.is_empty():
		temp = "/tmp"
	var mod_path := temp.path_join("openbfme-ini-error-" + str(OS.get_process_id()))
	var folder := mod_path.path_join("data/ini/object")
	if DirAccess.make_dir_recursive_absolute(folder) != OK:
		_fail("could not create " + folder)
		return
	var ini_path := folder.path_join("zzbad.ini")
	var file := FileAccess.open(ini_path, FileAccess.WRITE)
	if file == null:
		_fail("could not write " + ini_path + ": " + error_string(FileAccess.get_open_error()))
		return
	file.store_string("Weapon ZzBadWeapon\n  Bogus = 1\nEnd\n")
	file.close()
	var fs = ClassDB.instantiate("RetailFileSystem")
	fs.set_mods(PackedStringArray([mod_path]))
	var failure := ""
	if not fs.mount_retail().ok:
		failure = "mount_retail with the mod folder"
	else:
		var builder = ClassDB.instantiate("MapObjectBuilder")
		var root = builder.build_objects(fs, MAP, {}) # a process crash here is the failure this test exists for
		var reported := false
		for e in builder.get_report().get("errors", []):
			reported = reported or (String(e).contains("zzbad.ini") and String(e).contains("Unknown field 'Bogus' in block 'Weapon'"))
		if not reported:
			failure = "the rejected object INI was not reported: " + str(builder.get_report().get("errors", []))
		if root != null:
			root.free()
	DirAccess.remove_absolute(ini_path)
	for path in [folder, mod_path.path_join("data/ini"), mod_path.path_join("data"), mod_path]:
		DirAccess.remove_absolute(path)
	if not failure.is_empty():
		_fail(failure)
		return
	print("PASS: an INI error inside the extension is reported")
	quit(0)
