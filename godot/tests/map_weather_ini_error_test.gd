## A weather.ini the INI parse rejects fails the map build with the error reported: no terrain with invented fog or cloud settings (retail unwinds out
## of the map start on an INIException).
## Uses a synthetic mod folder with a malformed data\ini\weather.ini over the retail map "map wor fangorn"; no retail bytes are written.
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
	create_timer(300).timeout.connect(func(): _fail("map weather ini error test timed out"))

	# control: the retail weather.ini reads, the map builds with its terrain
	var fs = ClassDB.instantiate("RetailFileSystem")
	if not fs.mount_retail().ok:
		_fail("mount_retail")
		return
	var builder = ClassDB.instantiate("MapTerrainBuilder")
	var root: Node3D = builder.build_map(fs, MAP, {"fog": false})
	if root == null:
		_fail("control build: " + str(builder.get_report().get("errors", [])))
		return
	if root.get_node_or_null("Terrain") == null:
		_fail("control: no Terrain node")
		return
	root.free()

	# a mod folder whose weather.ini gives a Real field a value INI::parseReal rejects
	var temp := OS.get_environment("TMPDIR")
	if temp.is_empty():
		temp = OS.get_environment("TEMP")
	if temp.is_empty():
		temp = "/tmp"
	var mod_path := temp.path_join("openbfme-weather-ini-error-" + str(OS.get_process_id()))
	var folder := mod_path.path_join("data/ini")
	if DirAccess.make_dir_recursive_absolute(folder) != OK:
		_fail("could not create " + folder)
		return
	var ini_path := folder.path_join("weather.ini")
	var file := FileAccess.open(ini_path, FileAccess.WRITE)
	file.store_string("Weather\n  HardwareFogStart = notanumber\nEnd\n")
	file.close()
	var bad_fs = ClassDB.instantiate("RetailFileSystem")
	bad_fs.set_mods(PackedStringArray([mod_path]))
	var mounted: bool = bad_fs.mount_retail().ok
	var failure := ""
	if not mounted:
		failure = "mount_retail with the mod folder"
	else:
		var bad_builder = ClassDB.instantiate("MapTerrainBuilder")
		var bad_root: Node3D = bad_builder.build_map(bad_fs, MAP, {"fog": false})
		if bad_root != null:
			failure = "the map built with the weather the INI parse rejected"
			bad_root.free()
		else:
			var reported := false
			for e in bad_builder.get_report().errors:
				reported = reported or (String(e).contains("weather") and String(e).contains("weather.ini"))
			if not reported:
				failure = "the failed weather.ini was not reported: " + str(bad_builder.get_report().errors)
	DirAccess.remove_absolute(ini_path)
	for path in [folder, mod_path.path_join("data"), mod_path]:
		DirAccess.remove_absolute(path)
	if not failure.is_empty():
		_fail(failure)
		return
	print("PASS: a rejected weather.ini is reported and the map is not built with invented weather")
	quit(0)
