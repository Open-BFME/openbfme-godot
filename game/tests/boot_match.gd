extends SceneTree
## Prove match.tscn + scripts load with zero parse errors (real skirmish path).
## Avoid autoload identifiers at parse-time on -s entry scripts; resolve via /root.

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var hud_scr = load("res://src/ui/hud.gd")
	var match_scr = load("res://src/view/match_controller.gd")
	if hud_scr == null:
		printerr("BOOT_FAIL: hud.gd failed to load")
		quit(2)
		return
	if match_scr == null:
		printerr("BOOT_FAIL: match_controller.gd failed to load")
		quit(3)
		return
	# GDScript can_instantiate exists on GDScript in 4.x
	var hud_ok := true
	var match_ok := true
	if hud_scr.has_method("can_instantiate"):
		hud_ok = hud_scr.can_instantiate()
	if match_scr.has_method("can_instantiate"):
		match_ok = match_scr.can_instantiate()
	# Also try .new() on a temporary for Node scripts
	var hud_inst = hud_scr.new()
	var match_inst = match_scr.new()
	print("BOOT hud instance=", hud_inst != null, " match instance=", match_inst != null)
	if hud_inst == null or match_inst == null:
		printerr("BOOT_FAIL: could not instantiate scripts")
		quit(4)
		return
	if hud_inst is Node:
		hud_inst.free()
	if match_inst is Node:
		match_inst.free()
	print("BOOT hud can_instantiate=", hud_ok)
	print("BOOT match_controller can_instantiate=", match_ok)
	var gs = root.get_node_or_null("GameState")
	if gs == null:
		printerr("BOOT_FAIL: GameState autoload missing")
		quit(5)
		return
	gs.player_faction = "gondor"
	gs.enemy_faction = "mordor"
	gs.map_id = "anduin"
	gs.difficulty = "normal"
	var err := change_scene_to_file("res://scenes/match.tscn")
	print("BOOT change_scene err=", err)
	if err != OK:
		printerr("BOOT_FAIL: change_scene_to_file failed ", err)
		quit(6)
		return
	await process_frame
	await process_frame
	await process_frame
	await process_frame
	var scene := current_scene
	if scene == null:
		printerr("BOOT_FAIL: current_scene null")
		quit(7)
		return
	print("BOOT match scene name=", scene.name)
	print("BOOT match scene script=", scene.get_script())
	if scene.get_script() == null:
		printerr("BOOT_FAIL: match root has null script")
		quit(8)
		return
	var hud = scene.get_node_or_null("HUD")
	if hud == null:
		printerr("BOOT_FAIL: HUD node missing")
		quit(9)
		return
	print("BOOT HUD script=", hud.get_script())
	if hud.get_script() == null:
		printerr("BOOT_FAIL: HUD has null script")
		quit(10)
		return
	var w = scene.get("world")
	print("BOOT world set=", w != null)
	if w == null:
		printerr("BOOT_FAIL: match.world not set after ready")
		quit(11)
		return
	print("BOOT_OK match scripts attached and world initialized")
	print("BOOT_OK zero script load failures for match path")
	quit(0)
