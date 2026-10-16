extends SceneTree
## Local BFME2Lua assets and the Lua graphics adapter; no retail install needed.
var failures := 0
var world: Node3D

func _initialize() -> void:
	call_deferred("_run")

func check(ok: bool, message: String) -> void:
	if not ok:
		push_error(message)
		failures += 1

func lua(code: String) -> String:
	var result: Dictionary = world.execute_lua(code)
	check(result.ok, str(result.output))
	return str(result.output).strip_edges()

func _run() -> void:
	world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var script := FileAccess.get_file_as_string("res://dev/04_gondor_plots.lua")
	check(not script.is_empty(), "plot loader exists")
	lua(script)
	world.update_lua()
	check(lua("for _,t in ipairs(gondorPlots.tickets) do assert(bfme.mutations.getStatus(t)=='applied', bfme.mutations.getStatus(t)) end; return #gondorPlots.tickets") == "7", "all seven DDS files decode and apply")
	lua("restore = gondorPlots.enable(false)")
	world.update_lua()
	check(lua("for _,t in ipairs(restore) do assert(bfme.mutations.getStatus(t)=='applied') end; return #restore") == "7", "all seven overrides restore")
	lua("missing = bfme.graphics.setTextureOverride('gbfarmt', 'res://mods/gondor_plots/missing.dds')")
	world.update_lua()
	check(lua("return bfme.mutations.getStatus(missing)").contains("file not found"), "missing assets report failure")
	world.queue_free()
	await process_frame
	print("GONDOR PLOTS ", "PASS" if failures == 0 else "FAIL")
	quit(0 if failures == 0 else 1)
