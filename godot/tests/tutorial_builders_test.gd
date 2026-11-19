## Installed BFME2 data: the scripted command-button tower orders use real builders.
extends SceneTree
var _failures := 0
func _check(condition: bool, message := "") -> void:
	if not condition:
		_failures += 1
		printerr("MISSION CHECK FAIL: ", message, " ", get_stack())
func _finish() -> void:
	if _failures == 0: print("TUTORIAL BUILDERS PASS")
	quit(0 if _failures == 0 else 1)
func _initialize() -> void: call_deferred("_run")
func _run() -> void:
	create_timer(180).timeout.connect(func(): printerr("TUTORIAL BUILDERS TIMEOUT"); quit(1))
	var game = load("res://scripts/game.gd").new()
	game._intro = "off"
	root.add_child(game)
	while game._campaign_flow == null or game._state not in [game.State.MENU, game.State.FAILED]: await process_frame
	_check(game._state == game.State.MENU, game._failed)
	await game._wait_main_ready()
	var world: Node = game._world
	world.set_logic_thread(false)
	game._campaign_flow.hooks = true
	game._campaign_flow.start_tutorial("Basic")
	while game._state not in [game.State.PLAYING, game.State.FAILED]: await process_frame
	_check(game._state == game.State.PLAYING, game._failed)
	world.set_auto_advance(false)
	game._campaign_flow._cine.set_process(false)
	_check(world.debug_script_action("GATE_OPEN", [{"type":14,"string":"Gate 01"}]).ok)
	for i in 35: world.advance(0.2)
	var original: Array = world.get_object_ids()
	for i in [1, 2]:
		var porter: Dictionary = world.debug_script_unit("Porter %02d" % i)
		_check(porter.ok)
		_check(world.debug_script_action("NAMED_USE_COMMANDBUTTON_ABILITY_AT_WAYPOINT", [
			{"type":14,"string":"Porter %02d" % i},
			{"type":34,"string":"Command_PorterConstructMenSentryTower"},
			{"type":7,"string":"WP - Build Tower %02d" % i}]).ok)
	var towers: Array = []
	for id in world.get_object_ids():
		if not original.has(id):
			var obj: Dictionary = world.get_object(id)
			if obj.template == "GondorBattleTower": towers.append(id)
	_check(towers.size() == 2, "Both tutorial porters must create their construction targets")
	if towers.size() != 2:
		game.free()
		_finish()
		return
	for i in 2:
		var obj: Dictionary = world.get_object(towers[i])
		var waypoint: Dictionary = world.get_waypoint("WP - Build Tower %02d" % (i + 1))
		_check(Vector2(obj.x, obj.y) == Vector2(waypoint.x, waypoint.y))
		_check(obj.angle == 0.0)
		var builder: Dictionary = world.debug_script_unit("Porter %02d" % (i + 1))
		_check(obj.owner == world.get_object(builder.id).owner)
		_check(not obj.under_construction) # retail first creates an unplaced phantom; placement starts construction
	_check(not world.get_script_report().unported_actions.has("NAMED_USE_COMMANDBUTTON_ABILITY_AT_WAYPOINT"))
	var started := {}
	var completed := false
	for frame in 900:
		world.advance(0.2)
		completed = true
		for id in towers:
			var status: Dictionary = world.get_object(id)
			if status.under_construction: started[id] = true
			if not started.has(id) or status.construction_percent != -1.0: completed = false
		if completed: break
	print("TUTORIAL TOWERS completed: ", completed, " frame: ", world.get_frame())
	_check(completed, "The porters must reach their sites and finish both towers")
	for id in towers: _check(world.get_object(id).construction_percent == -1.0)
	game.free()
	_finish()
