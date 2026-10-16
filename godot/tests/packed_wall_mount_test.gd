extends SceneTree
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	assert(FileAccess.file_exists("res://mods/wall_towers.big"))
	var fs = ClassDB.instantiate("RetailFileSystem")
	var mounted = fs.mount_retail()
	assert(mounted.ok, str(mounted))
	var inst = ClassDB.instantiate("W3DInstancer")
	root.add_child(inst)
	assert(inst.setup(fs).ok)
	assert(inst.add_model("EBWallTower") >= 0)
	assert(inst.add_model("IBWallTower") >= 0)
	inst.queue_free()
	await process_frame
	print("PACKED WALL MOUNT PASS")
	quit()
