## Original APT/GPU checks for strategic controls. Requires the installed games and a GPU.
## Optional WOTR_HUD_CAPTURE_DIR writes private captures; no game assets belong in git.
extends SceneTree

const Wotr := preload("res://scripts/wotr.gd")

class CampaignHost extends Node:
	var _world: Node
	var _shell: Node

var failures := 0
var baseline := OS.get_environment("WOTR_HUD_BASELINE") == "1"

func _initialize() -> void:
	run.call_deferred()

func check(value: bool, description: String) -> void:
	print("STRATEGIC CONTROLS ", "ok " if value else "FAIL ", description)
	if not value:
		failures += 1

func tick(shell: Node, frames := 90) -> Array:
	var requests := []
	for i in frames:
		shell.tick(1.0 / 30.0)
		requests.append_array(shell.strategic_hud({}).get("requests", []))
	return requests

func click(shell: Node, level: int, path: String) -> Array:
	var button: Dictionary = shell.find_button(level, path)
	check(bool(button.get("found", false)), "input target " + path)
	if not button.get("found", false):
		return []
	var position: Vector2 = shell.stage_to_window(Vector2(float(button.x), float(button.y)))
	var motion := InputEventMouseMotion.new()
	motion.position = position
	shell.get_viewport().push_input(motion, true)
	for down in [true, false]:
		var event := InputEventMouseButton.new()
		event.position = position
		event.button_index = MOUSE_BUTTON_LEFT
		event.pressed = down
		shell.get_viewport().push_input(event, true)
	return tick(shell)

func capture(shell: Node, viewport: SubViewport, name: String) -> void:
	shell.render(true)
	await process_frame
	await process_frame
	await RenderingServer.frame_post_draw
	var folder := OS.get_environment("WOTR_HUD_CAPTURE_DIR")
	if not folder.is_empty():
		DirAccess.make_dir_recursive_absolute(folder)
		check(viewport.get_texture().get_image().save_png(folder.path_join(name + ".png")) == OK, "capture " + name)

func make_live_battle(world: Node) -> Dictionary:
	var logic = ClassDB.instantiate("LivingWorldGame")
	var data: Dictionary = logic.setup(world)
	check(bool(data.get("ok", false)), "native LivingWorld setup")
	if not data.get("ok", false):
		return {}
	var started: Dictionary = logic.start({"campaign": "WOTRScenario001", "seed": 5, "slots": [
		{"name": "Local player", "faction": "FactionMen", "human": true, "team": 1, "color": 0, "start_region": "Amon_Sul"},
		{"name": "Enemy player", "faction": "FactionMordor", "human": true, "team": 2, "color": 1, "start_region": "Rhudaur"}]})
	check(bool(started.ok), "native campaign starts: " + str(started.error))
	if not started.ok:
		return {}
	var state: Dictionary = logic.get_strategic_state()
	check(bool(logic.order_move(0, 1, int(state.players[1].capital)).ok), "normal army order attacks adjacent enemy capital")
	for player in 2:
		check(bool(logic.end_turn(player).ok), "native END PHASE for player %d" % player)
	logic.update()
	state = logic.get_strategic_state()
	check(int(state.pending_battle) == 0, "native strategic logic produces pending battle zero")
	if int(state.pending_battle) != 0:
		return {}
	check(state.battles[0].sides == [[{"player": 0, "veterancy": 0, "lost": 0, "destroyed": 0, "heroes_lost": 0}],
		[{"player": 1, "veterancy": 0, "lost": 0, "destroyed": 0, "heroes_lost": 0}]], "native battle sides are arrays of player records")
	check(int(logic.resolution_options(0, 0)) == 14, "native battle permits Auto-resolve, Real-time and Retreat")
	return {"logic": logic, "data": data, "regions": data.region_campaigns[0].regions}

func exercise_live_battle(shell: Node, world: Node, fixture: Dictionary) -> void:
	if fixture.is_empty():
		return
	var host := CampaignHost.new()
	host._world = world
	host._shell = shell
	var controller := Wotr.new()
	controller.init(host)
	controller.lw = fixture.logic
	controller.data = fixture.data
	controller.regions = fixture.regions
	controller.state = fixture.logic.get_strategic_state()
	controller.apt_hud = true
	# Exercise the complete native snapshot -> wotr.gd -> original HUD path. A second
	# sync also covers the pending-battle cache which previously hid the first error.
	controller._sync_apt_hud()
	tick(shell, 180)
	controller._sync_apt_hud()
	var state: Dictionary = shell.strategic_hud({})
	check(bool(state.battle_prompt_open) and bool(state.battle_prompt_loaded), "native pending battle reaches the HUD on repeated sync")
	check(state.prompt_pages[0].size() == 1 and state.prompt_pages[1].size() == 1, "native battle player sides populate both HUD pages")
	for button in [["AutoResolve", "battle_autoresolve"], ["RealTime", "battle_realtime"], ["Retreat", "battle_retreat"]]:
		var requests := click(shell, int(state.level), "dlgFrame.content.main." + str(button[0]))
		check(requests == [button[1]], "native battle exposes working " + str(button[0]) + " input")
	controller.free()
	host.free()
	shell.strategic_hud({"kind": "close"})
	shell.strategic_hud({"kind": "open"})
	tick(shell)

func exercise(fs: RefCounted, world: Node, size: Vector2i, folder: String, live_battle: Dictionary) -> void:
	var viewport := SubViewport.new()
	viewport.size = size
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(viewport)
	var shell = ClassDB.instantiate("AptMenuPlayer")
	shell.auto_process = false
	viewport.add_child(shell)
	check(shell.boot_shell(fs, world, {"options_file": folder.path_join("Options.ini"), "skirmish_ini": folder.path_join("Skirmish.ini")}).ok, "shell boot")
	var state: Dictionary = shell.strategic_hud({"kind": "open"})
	check(state.ok, "retail strategic HUD opens")
	shell.shell_show_shell_map(false)
	shell.shell_hide_background()
	tick(shell)
	if not baseline:
		exercise_live_battle(shell, world, live_battle)
	state = shell.strategic_hud({})
	var level := int(state.level)
	var rows := []
	for i in 30:
		rows.append({"key": "retreat:%d" % i, "label": "STRATEGICHUD:PlanRetreatChecklistItem",
			"args": [{"text": "Territory %d" % i}, {"text": "Army %d" % i}, {"text": "Territory %d" % i}], "importance": 1})
	shell.strategic_hud({"checklist": rows, "turn": 3, "phase": 0,
		"stats": {"treasury": 3000, "income": 450, "resource_multiplier": 1.0, "power_points": 2, "bonus": [0, 0, 0]},
		"selection_cp": {"current": 120, "maximum": 500, "portrait": "UPGondor_Army"}})
	tick(shell)
	if not baseline:
		check(shell.instance_info(level, "main.globe.selectionUI").frame == 19, "CP text belongs to a shown selection panel")
	click(shell, level, "main.checklist.expandButton")
	await capture(shell, viewport, "%dx%d-checklist" % [size.x, size.y])
	var before: Dictionary = shell.instance_info(level, "main.checklist.it.ems")
	click(shell, level, "main.checklist.scrollBar.theBar.downButton")
	var after: Dictionary = shell.instance_info(level, "main.checklist.it.ems")
	if not baseline:
		# tolerance: real scrollbar input must move content upward; row heights depend on the installed font metrics.
		check(float(after.y) < float(before.y), "scrollbar click moves the actual list")
	await capture(shell, viewport, "%dx%d-checklist-scrolled" % [size.x, size.y])
	var army := {"name": "Garrison", "portrait": "UPGondor_Army", "garrison": true, "categories": [4, 2, 0, 0, 0, 0, 1, 0, 0]}
	var local := {"name": "Local player", "faction": "IconMenStrategic", "color": 0x2266ff, "local": true, "armies": [army]}
	var ally := {"name": "Allied player", "faction": "IconMenStrategic", "color": 0x22cc44, "armies": [army]}
	var enemy := {"name": "Enemy player", "faction": "IconMordorStrategic", "color": 0xcc2222, "armies": [army]}
	shell.strategic_hud({"battle_prompt": {"region": "Strategic HUD check", "ally": "Local player", "enemy": "Enemy player",
		"auto_resolve": true, "real_time": true, "retreat": true, "allies": [local, ally], "enemies": [enemy]}})
	tick(shell, 180)
	state = shell.strategic_hud({})
	if not baseline:
		check(state.prompt_pages[0].size() == 2 and state.prompt_pages[1].size() == 1, "both player sides load their pages")
	await capture(shell, viewport, "%dx%d-prompt" % [size.x, size.y])
	if not baseline:
		click(shell, level, str(state.prompt_tabs[0]) + ".1")
		check(shell.strategic_hud({}).prompt_selected[0] == 1, "ally tab switches through real input")
		await capture(shell, viewport, "%dx%d-prompt-ally" % [size.x, size.y])
	if not baseline:
		# No empty prompt between these battles: the old player pages must be replaced.
		shell.strategic_hud({"battle_prompt": {"region": "Next battle", "auto_resolve": true, "real_time": true,
			"allies": [local], "enemies": [enemy, ally]}})
		tick(shell, 180)
		state = shell.strategic_hud({})
		check(state.prompt_pages[0].size() == 1 and state.prompt_pages[1].size() == 2, "consecutive battle replaces old player pages")
		click(shell, level, str(state.prompt_tabs[1]) + ".1")
		check(shell.strategic_hud({}).prompt_selected[1] == 1, "replacement enemy tab receives real input")
	shell.strategic_hud({"battle_prompt": {}})
	tick(shell)
	var status := {"scenario": "Strategic HUD check", "objectives": "Review the strategic situation.", "fiction": "Original retail interface.", "game_type": "War of the Ring",
		"players": [{"name": "Local player", "faction": "IconMenStrategic", "color": 0x2266ff, "allied": true, "territories": 4, "regions": 12},
			{"name": "Enemy player", "faction": "IconMordorStrategic", "color": 0xcc2222, "territories": 2, "regions": 7, "defeated": true}]}
	shell.strategic_hud({"player_status": status})
	tick(shell)
	state = shell.strategic_hud({})
	if not baseline:
		check(state.get("player_status_open", false), "status screen opens")
		var blocked := click(shell, level, "main.endTurnButton")
		check(not blocked.has("end_turn"), "status screen blocks the underlying END PHASE button")
	await capture(shell, viewport, "%dx%d-status" % [size.x, size.y])
	if not baseline:
		click(shell, int(state.player_status_level), "Main.TabsMain.TabObjectives")
		await capture(shell, viewport, "%dx%d-objectives" % [size.x, size.y])
		click(shell, int(state.player_status_level), "Main.Tray.Resume")
		check(not shell.strategic_hud({}).player_status_open, "RESUME closes the status screen through input")
		# Leave another status screen open at destruction to catch lifetime ordering.
		shell.strategic_hud({"player_status": status})
		tick(shell)
	shell.strategic_hud({"kind": "close"})
	# A caller can also destroy its shell without explicitly closing its overlay.
	if not baseline:
		shell.strategic_hud({"kind": "open", "player_status": status})
		tick(shell)
	viewport.free()

func run() -> void:
	if DisplayServer.get_name() == "headless" or OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("STRATEGIC CONTROLS SKIP: GPU and game installations required")
		quit(77)
		return
	create_timer(180.0).timeout.connect(func(): printerr("STRATEGIC CONTROLS timeout"); quit(1))
	var fs = ClassDB.instantiate("RetailFileSystem")
	check(fs.mount_retail().ok, "retail mount")
	var world = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	check(world.setup(fs).ok, "world setup")
	var folder := OS.get_temp_dir().path_join("openbfme-strategic-controls-%d" % OS.get_process_id())
	DirAccess.make_dir_recursive_absolute(folder)
	var live_battle := make_live_battle(world) if not baseline else {}
	for size in [Vector2i(1024, 768), Vector2i(1280, 720), Vector2i(1680, 720)]:
		await exercise(fs, world, size, folder, live_battle)
	live_battle.clear()
	world.free()
	print("STRATEGIC CONTROLS ", "PASS" if failures == 0 else "FAIL", ": ", failures, " failures")
	quit(0 if failures == 0 else 1)
