## A water.ini the INI parse rejects is reported and the rivers that need its WaterSet DiffuseColor are not built (no fallback colour).
## Uses a synthetic mod folder with a malformed data\ini\water.ini over the retail map "map wor fangorn" (9 river areas); no retail bytes are written.
extends SceneTree

const MAP := "map wor fangorn"

func _initialize() -> void:
	call_deferred("_run")

func _fail(message: String) -> void:
	push_error("FAIL: " + message)
	quit(1)

func _rivers(root: Node3D) -> Array:
	var rivers := root.get_node_or_null("Rivers")
	return [] if rivers == null else rivers.find_children("*", "MeshInstance3D", true, false)

func _run() -> void:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: set ROTWK_INSTALL and BFME2_INSTALL")
		quit(77)
		return
	create_timer(300).timeout.connect(func(): _fail("map water ini error test timed out"))

	# control: the retail water.ini reads, the rivers are built
	var fs = ClassDB.instantiate("RetailFileSystem")
	if not fs.mount_retail().ok:
		_fail("mount_retail")
		return
	var builder = ClassDB.instantiate("MapTerrainBuilder")
	var root: Node3D = builder.build_map(fs, MAP, {"fog": false})
	if root == null:
		_fail("control build: " + str(builder.get_report().get("errors", [])))
		return
	var built := _rivers(root).size()
	var rendered: int = builder.get_report().rivers.rendered
	for e in builder.get_report().errors:
		if String(e).contains("water.ini"):
			_fail("the retail water.ini reported: " + String(e))
			return
	if built == 0 or built != rendered:
		_fail("control: %d river meshes, report says %d" % [built, rendered])
		return
	root.free()

	# a mod folder whose water.ini names a colour without its blue channel (INI::parseRGBAColorInt: "can't omit value for color B")
	var temp := OS.get_environment("TMPDIR")
	if temp.is_empty():
		temp = OS.get_environment("TEMP")
	if temp.is_empty():
		temp = "/tmp"
	var mod_path := temp.path_join("openbfme-water-ini-error-" + str(OS.get_process_id()))
	var folder := mod_path.path_join("data/ini")
	if DirAccess.make_dir_recursive_absolute(folder) != OK:
		_fail("could not create " + folder)
		return
	var ini_path := folder.path_join("water.ini")
	var file := FileAccess.open(ini_path, FileAccess.WRITE)
	file.store_string("WaterSet AFTERNOON\n  DiffuseColor = R:1 G:2\nEnd\n")
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
		if bad_root == null:
			failure = "the map did not build: " + str(bad_builder.get_report().get("errors", []))
		else:
			var reported := false
			for e in bad_builder.get_report().errors:
				reported = reported or (String(e).contains("water.ini") and String(e).contains("can't omit value for color B"))
			if not reported:
				failure = "the failed water.ini was not reported: " + str(bad_builder.get_report().errors)
			elif _rivers(bad_root).size() != 0:
				failure = "%d river meshes were built from a colour that was never read" % _rivers(bad_root).size()
			elif int(bad_builder.get_report().rivers.rendered) != 0:
				failure = "the report counts %d rivers rendered" % int(bad_builder.get_report().rivers.rendered)
			elif int(bad_builder.get_report().rivers.areas_in_map) != 9:
				failure = "the report lost the map's river areas"
			bad_root.free()
	DirAccess.remove_absolute(ini_path)
	for path in [folder, mod_path.path_join("data"), mod_path]:
		DirAccess.remove_absolute(path)
	if not failure.is_empty():
		_fail(failure)
		return
	print("PASS: a rejected water.ini is reported and builds no river (control built %d)" % built)
	quit(0)
