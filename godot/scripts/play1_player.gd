## Lane PLAY-1: the owner's first hands-on skirmish, replayed by input events. Every action here is an InputEvent pushed into the viewport (mouse motion,
## buttons, the wheel, keys), the path a player's mouse takes: no QA harness command and no HUD shortcut. game.gd runs it after the start with
## --play1=<scenario>[,<scenario>...] (with --auto); each scenario prints "PLAY1 <scenario>: ok" or "PLAY1 <scenario>: FAIL <why>", screenshots go to --screens as
## play1-<name>.png, and "PLAY1 RESULT: <n> ok, <m> failed" ends the run.
##   orders    a left click selects one of the local units, a right click on the ground orders the move (MSG_DO_MOVETO), the move hint is drawn there,
##             the unit walks toward the point
##   palantir  the three buttons above the minimap (the Palantir's options row) are clicked one after the other; what each opened is reported
##   powers    the Palantir's powers button opens SpellStore.apt; a click on a power that can be bought, RESET, a buy and ACCEPT; a click beside the
##             store closes it
##   camera    (with --free-camera) the wheel zooms out beyond retail's limit
extends RefCounted

var game: Node
var hud: Node
var world: Node
var shell: Node
var ok_count := 0
var fail_count := 0


func run(p_game: Node, scenarios: PackedStringArray) -> void:
	game = p_game
	hud = game._hud
	world = game._world
	shell = game._shell
	for s in scenarios:
		var r: String = await call("_scenario_" + s)
		if r == "":
			ok_count += 1
			print("PLAY1 %s: ok" % s)
		else:
			fail_count += 1
			print("PLAY1 %s: FAIL %s" % [s, r])
	print("PLAY1 RESULT: %d ok, %d failed" % [ok_count, fail_count])


# ---- input -----------------------------------------------------------------------------------------------------------------------------------------------

func _vp() -> Viewport:
	return game.get_viewport()


func move_to(p: Vector2) -> void:
	# the window's real pointer goes there too: on a desktop the system pointer's own motion events would otherwise land between the injected ones
	Input.warp_mouse(p)
	var m := InputEventMouseMotion.new()
	m.position = p
	m.global_position = p
	_vp().push_input(m)
	await game._step(2)


func press(p: Vector2, button: MouseButton, down: bool) -> void:
	var b := InputEventMouseButton.new()
	b.button_index = button
	b.pressed = down
	b.position = p
	b.global_position = p
	b.button_mask = MOUSE_BUTTON_MASK_LEFT if (down and button == MOUSE_BUTTON_LEFT) else (MOUSE_BUTTON_MASK_RIGHT if (down and button == MOUSE_BUTTON_RIGHT) else 0)
	_vp().push_input(b)


func click(p: Vector2, button: MouseButton = MOUSE_BUTTON_LEFT) -> void:
	await move_to(p)
	# a click is shorter than Mouse.ini's DragToleranceMS (150): one rendered frame between the press and the release (a slow windowed frame on the
	# Deck takes 60+ ms; three of them made the release a drag's)
	var t0 := Time.get_ticks_msec()
	press(p, button, true)
	await game.get_tree().process_frame
	press(p, button, false)
	var held := Time.get_ticks_msec() - t0
	if held > 140:
		print("PLAY1 note: a click was held %d ms (the frame took longer than a click)" % held)
	await game._step(3)


func wheel(p: Vector2, notches: int) -> void:
	await move_to(p)
	for i in absi(notches):
		var b := InputEventMouseButton.new()
		b.button_index = MOUSE_BUTTON_WHEEL_UP if notches > 0 else MOUSE_BUTTON_WHEEL_DOWN
		b.pressed = true
		b.position = p
		b.global_position = p
		_vp().push_input(b)
		var u := b.duplicate()
		u.pressed = false
		_vp().push_input(u)
		await game._step(1)


func shot(name: String) -> void:
	game._save_named("play1-" + name)


func window_size() -> Vector2:
	return _vp().get_visible_rect().size


# ---- helpers ----------------------------------------------------------------------------------------------------------------------------------------------

func local_units() -> Array:
	var out: Array = []
	for o in world.get_player_objects(game._local_name):
		if not o.structure:
			out.append(o)
	return out


func to_pixel(x: float, y: float) -> Vector2:
	return hud.world_to_pixel(Vector2(x, y))


func on_screen(p: Vector2) -> bool:
	var s := window_size()
	return p.x > 40 and p.y > 40 and p.x < s.x - 40 and p.y < s.y * 0.7


func log_count(prefix: String) -> int:
	var n := 0
	for l in hud.get_message_log():
		if str(l).begins_with(prefix):
			n += 1
	return n


func palantir_button(path: String) -> Vector2:
	var b: Dictionary = hud.find_button_window(path)
	return Vector2(b.x, b.y) if b.get("found", false) else Vector2(-1, -1)


# ---- scenarios ------------------------------------------------------------------------------------------------------------------------------------------

func _scenario_orders() -> String:
	var units := local_units()
	var unit: Dictionary = {}
	for u in units:
		hud.camera_look_at(Vector2(u.x, u.y))
		await game._step(10)
		var p := to_pixel(u.x, u.y)
		if on_screen(p):
			unit = u
			break
	if unit.is_empty():
		return "no local unit on the screen (%d units)" % units.size()
	var up := to_pixel(unit.x, unit.y)
	print("PLAY1 orders: unit %s (%d) at (%.0f, %.0f), pixel %s" % [unit.template, unit.id, unit.x, unit.y, up])
	await click(up)
	await game._step(20)
	var sel: Array = hud.get_selection()
	print("PLAY1 orders: selection after the left click: ", sel)
	if sel.is_empty():
		return "the left click on the unit selected nothing"
	# an empty piece of ground beside the unit (a click on one of the player's other units would be a selection, not an order)
	var target := Vector2(-1, -1)
	for off in [Vector2(220, 60), Vector2(-220, 60), Vector2(200, -80), Vector2(-200, -80), Vector2(300, 140), Vector2(-300, 140)]:
		var w: Vector2 = hud.pixel_to_world(up + off)
		var clear := w.x > -1.0e8
		for o in world.get_player_objects(game._local_name):
			if Vector2(o.x, o.y).distance_to(w) < 60.0:
				clear = false
		if clear and on_screen(up + off):
			target = up + off
			break
	if target.x < 0:
		return "no empty ground near the unit"
	var before := log_count("MSG_DO_MOVETO")
	await move_to(target)
	await game._step(10)
	var st: Dictionary = hud.get_state()
	print("PLAY1 orders: the cursor over the ground: %s, over_gui %s, gui_command %s, selecting %s" % [st.get("cursor", "?"), st.get("over_gui", "?"), st.get("gui_command", "?"), st.get("selecting", "?")])
	for o in world.get_player_objects(game._local_name):
		if o.id == unit.id:
			print("PLAY1 orders: the unit is at (%.0f, %.0f) = pixel %s; the target pixel %s is the ground point %s" % [o.x, o.y, to_pixel(o.x, o.y), target, hud.pixel_to_world(target)])
	await click(target, MOUSE_BUTTON_RIGHT)
	await game._step(4)
	var moved := log_count("MSG_DO_MOVETO") - before
	var hints: Dictionary = hud.get_move_hints()
	var drawn: int = game._move_hints.drawn_count() if game._move_hints != null else 0
	print("PLAY1 orders: MSG_DO_MOVETO %d, move hints %d (model %s), drawn %d" % [moved, hints.hints.size(), hints.model, drawn])
	shot("orders-hint")
	print("PLAY1 orders: click outcomes ", hud.get_state().get("click_outcomes", {}))
	var ml: PackedStringArray = hud.get_message_log()
	print("PLAY1 orders: the last messages: ", ml.slice(maxi(0, ml.size() - 6)))
	if moved != 1:
		return "the right click on the ground gave %d MSG_DO_MOVETO" % moved
	if hints.hints.is_empty() or drawn == 0:
		return "no move hint was drawn"
	var goal: Vector2 = hud.pixel_to_world(target)
	var d0 := Vector2(unit.x, unit.y).distance_to(goal)
	await game._wait_seconds(5.0)
	var now: Dictionary = {}
	for o in world.get_player_objects(game._local_name):
		if o.id == unit.id:
			now = o
	shot("orders-moved")
	if now.is_empty():
		return "the unit is gone"
	var d1 := Vector2(now.x, now.y).distance_to(goal)
	print("PLAY1 orders: distance to the goal %.0f -> %.0f; hints alive %d" % [d0, d1, hud.get_move_hints().hints.size()])
	if d1 > d0 - 40.0:
		return "the unit did not walk toward the point (%.0f -> %.0f)" % [d0, d1]
	if hud.get_move_hints().hints.size() != 0:
		return "the move hint outlived its 40 client frames"
	return ""


func _scenario_explore() -> String:
	shot("explore")
	print("PLAY1 palantir tree:\n", hud.dump_tree(6))
	print("PLAY1 shell stack: ", shell.shell_stack())
	return ""


func _scenario_camera() -> String:
	var c0: Dictionary = hud.get_camera()
	var centre := window_size() * Vector2(0.5, 0.4)
	await wheel(centre, -60)
	await game._step(90)
	var c1: Dictionary = hud.get_camera()
	print("PLAY1 camera: free %s, height %.0f -> %.0f (retail max %.0f, limit %.0f), far %.0f, fog shift %.0f" % [str(c1.free_camera), c0.height_above_ground,
		c1.height_above_ground, c1.max_height, c1.zoom_out_limit, c1.far, c1.fog_shift])
	shot("camera-zoomed-out")
	if c1.free_camera:
		if c1.height_above_ground <= c1.max_height * 1.5:
			return "the free camera did not zoom out beyond retail's limit"
	elif c1.height_above_ground > c1.max_height + 0.01:
		return "the retail camera zoomed out beyond its limit"
	await wheel(centre, 80)
	await game._step(60)
	return ""


func store_state() -> Dictionary:
	return hud.get_spellbook_state().get("store", {})


func shell_button(level: int, path: String) -> Vector2:
	var b: Dictionary = shell.find_button(level, path)
	if not b.get("found", false):
		return Vector2(-1, -1)
	return shell.stage_to_window(Vector2(b.x, b.y))


func _scenario_powers() -> String:
	var magic := palantir_button("PalantirButtons.Buttons.PlayerMagic")
	print("PLAY1 powers: the Palantir's powers button at ", magic)
	if magic.x < 0:
		return "no PlayerMagic button in the Palantir"
	await click(magic)
	await game._wait_seconds(2.0)
	var st := store_state()
	shot("powers-open")
	if not st.get("open", false):
		return "the powers button did not open the store (%s)" % st.get("error", "")
	var lvl: int = st.level
	var states: Array = st.states
	print("PLAY1 powers: %d points, states %s" % [st.points, states])
	var first := states.find("_active")
	if first < 0:
		return "no power can be bought"
	if states.count("_disabled") == 0:
		return "no power is locked (all would be highlighted)"
	var spell := shell_button(lvl, "SpellStore.Buttons.Spell%d" % (first + 1))
	print("PLAY1 powers: power %d at %s %s" % [first + 1, spell, JSON.stringify(shell.find_button(lvl, "SpellStore.Buttons.Spell%d" % (first + 1)))])
	await click(spell)
	await game._wait_seconds(1.5) # the movie's generic button runs its callback when its _down animation ends
	st = store_state()
	print("PLAY1 powers: after a click on power %d: %s, points %d" % [first + 1, st.states[first], st.points])
	shot("powers-picked")
	if st.states[first] == "_active":
		return "the click on a power did nothing"
	await click(shell_button(lvl, "SpellStore.Buttons.ButtonsMain.Reset"))
	await game._wait_seconds(1.5)
	st = store_state()
	print("PLAY1 powers: after RESET: %s, points %d" % [st.states[first], st.points])
	if st.states[first] != "_active":
		return "RESET did not undo the pick"
	await click(spell)
	await game._wait_seconds(1.5)
	await click(shell_button(lvl, "SpellStore.Buttons.ButtonsMain.Accept"))
	await game._wait_seconds(3.0)
	st = store_state()
	# the purchase goes to the logic when the store closes (RW 0x8232ED); the bought power then takes a slot of the Palantir's spell book
	var slots: Array = hud.get_spellbook_state().get("slots", [])
	print("PLAY1 powers: after ACCEPT: store open %s, spell book slots %s" % [str(st.open), JSON.stringify(slots)])
	shot("powers-accepted")
	if st.open:
		return "ACCEPT did not close the store"
	if slots.count("_unused") == slots.size():
		return "the accepted power is not in the spell book"
	# reopened, Escape closes it (AptSpellStore: OnBttnClose and Escape)
	await click(magic)
	await game._wait_seconds(2.0)
	if not store_state().get("open", false):
		return "the store did not open again"
	await game._press_escape()
	await game._wait_seconds(1.0)
	print("PLAY1 powers: after Escape: store open %s, quit menu %s" % [str(store_state().open), str(game._quit_open)])
	if store_state().open:
		return "Escape did not close the store"
	if game._quit_open:
		game._close_quit_menu()
		return "Escape over the store opened the quit menu"
	return ""


func _scenario_palantir() -> String:
	var lvl: int = shell.shell_top_level()
	var fails: Array = []
	# the key: the options button (AptPalantir::OnBttnOptions RW 0x6D40C1 -> ToggleQuitMenu RW 0x921C9D): the quit menu
	var key := palantir_button("PalantirButtons.Buttons.Options")
	print("PLAY1 palantir: key at %s %s" % [key, JSON.stringify(shell.find_button(lvl, "PalantirButtons.Buttons.Options"))])
	var opens0: int = game._quit_log.count("open")
	await click(key)
	await game._wait_seconds(1.5)
	var opens: int = game._quit_log.count("open") - opens0
	if opens != 1:
		fails.append("one press of the key asked to open the quit menu %d times" % opens)
	print("PLAY1 palantir: after the key: quit menu open %s, shell %s" % [str(game._quit_open), shell.shell_stack()])
	shot("palantir-key")
	if not game._quit_open:
		fails.append("the key did not open the quit menu")
	else:
		game._close_quit_menu()
		await game._wait_seconds(1.0)
	# the flag: OnBttnObjectives (RW 0x6D40C9): in a skirmish / multiplayer game (RW 0x625456) PlayerTribute.apt over the game (RW 0x914EF0)
	var flag := palantir_button("PalantirButtons.Buttons.Objectives")
	print("PLAY1 palantir: flag at %s %s" % [flag, JSON.stringify(shell.find_button(lvl, "PalantirButtons.Buttons.Objectives"))])
	await click(flag)
	await game._wait_seconds(2.0)
	var stack: PackedStringArray = shell.shell_stack()
	print("PLAY1 palantir: after the flag: shell %s; the Palantir's last commands %s" % [stack, hud.get_state().get("palantir", {}).get("last_commands", [])])
	shot("palantir-flag")
	if not stack.has("PlayerTribute.apt"):
		fails.append("the flag did not open PlayerTribute.apt")
	else:
		# Escape closes it (RW 0x914E91 -> RW 0x914C51)
		await game._press_escape()
		await game._wait_seconds(2.0)
		stack = shell.shell_stack()
		print("PLAY1 palantir: after Escape: shell %s, quit menu %s" % [stack, str(game._quit_open)])
		if stack.has("PlayerTribute.apt"):
			fails.append("Escape did not close the tribute screen")
		if game._quit_open:
			fails.append("Escape over the tribute screen opened the quit menu")
			game._close_quit_menu()
	return "; ".join(fails)


func own_objects() -> Array:
	return world.get_player_objects(game._local_name)


func enemy_name() -> String:
	for p in world.get_economy().players:
		if p.playable and p.name != game._local_name:
			return p.name
	return ""


## a left click on an object where it is drawn (a horde: one of its soldiers); true when the HUD selected it (or its horde)
func select_object(o: Dictionary) -> bool:
	var target_id: int = o.id
	var members: Array = world.get_object(o.id).get("members", [])
	if not members.is_empty():
		target_id = int(members[0])
	var pose: Dictionary = world.get_render_pose(target_id)
	var at := Vector2(pose.x, pose.y) if pose.get("ok", false) else Vector2(o.x, o.y)
	for offset in [Vector2(), Vector2(0, 15), Vector2(0, 30), Vector2(15, 20), Vector2(-15, 20)]:
		var px := to_pixel(at.x + offset.x, at.y + offset.y)
		if not on_screen(px):
			continue
		await click(px)
		await game._step(6)
		if hud.get_selection().has(o.id):
			return true
	return false


## the camera goes to a world point by a left click on the radar (InGameHud::radarClick: the view looks there)
func look_by_radar(p: Vector2) -> void:
	var rp: Vector2 = hud.world_to_radar_pixel(p)
	if rp.x >= 0:
		await click(rp)
		await game._wait_seconds(0.8)


## the whole skirmish by the player's input: a worker builds a barracks (the Palantir's button, the site by a left click), the barracks trains, the
## new soldiers are selected by a drag box and sent to the enemy's base by a right click on the radar; what happens is reported
func _scenario_skirmish() -> String:
	var fails: Array = []
	world.set_time_scale(4.0) # the build and train times at four times speed (the input is the player's)
	var base := Vector2()
	for o in own_objects():
		if o.commandcenter:
			base = Vector2(o.x, o.y)
	var worker: Dictionary = {}
	for o in own_objects():
		if not o.structure and str(o.template).contains("Porter"):
			worker = o
	if worker.is_empty():
		for o in own_objects():
			if not o.structure:
				worker = o
	await look_by_radar(Vector2(worker.x, worker.y))
	if not await select_object(worker):
		return "the worker %s could not be selected by a click" % worker.template
	# the barracks button of the worker
	var build: Dictionary = {}
	for b in hud.get_command_buttons():
		if b.command == "DOZER_CONSTRUCT" and str(b.template).to_lower().contains("barracks"):
			build = b
	if build.is_empty():
		return "the worker's command set has no barracks button: %s" % JSON.stringify(hud.get_command_buttons().map(func(x): return x.template))
	var bp := palantir_button(build.frame)
	print("PLAY1 skirmish: the %s button at %s" % [build.template, bp])
	await click(bp)
	await game._step(6)
	if not hud.get_placement().get("placing", false):
		return "the build button did not start the placement"
	var site := Vector2(-1, -1)
	for ring in range(2, 8):
		for k in 12:
			var a := TAU * float(k) / 12.0
			var p := base + Vector2(cos(a), sin(a)) * 90.0 * ring
			var px := to_pixel(p.x, p.y)
			if not on_screen(px):
				continue
			await move_to(px)
			await game._step(2)
			var pl: Dictionary = hud.get_placement()
			if pl.get("placing", false) and int(pl.legal) == 0:
				site = px
				break
		if site.x >= 0:
			break
	if site.x < 0:
		await click(window_size() * 0.5, MOUSE_BUTTON_RIGHT)
		return "no legal site for the barracks on the screen"
	await click(site)
	await game._step(6)
	shot("skirmish-site")
	var barracks: Dictionary = {}
	for i in 120:
		await game._wait_seconds(1.0)
		for o in own_objects():
			if o.template == build.template:
				barracks = world.get_object(o.id)
		if not barracks.is_empty() and not barracks.get("under_construction", true):
			break
	print("PLAY1 skirmish: barracks %s" % JSON.stringify({"template": barracks.get("template", ""), "under_construction": barracks.get("under_construction", "?"), "percent": barracks.get("construction_percent", -1)}))
	if barracks.is_empty() or barracks.get("under_construction", true):
		return "the barracks was not built"
	# train: the barracks' first unit button, three times
	await look_by_radar(Vector2(barracks.x, barracks.y))
	if not await select_object(barracks):
		return "the barracks could not be selected by a click"
	var train: Dictionary = {}
	for b in hud.get_command_buttons():
		if b.command == "UNIT_BUILD" and train.is_empty():
			train = b
	if train.is_empty():
		return "the barracks shows no unit button"
	var before: Array = own_objects().filter(func(x): return not x.structure).map(func(x): return x.id)
	for i in 3:
		await click(palantir_button(train.frame))
		await game._step(4)
	var made: Array = []
	for i in 120:
		await game._wait_seconds(1.0)
		made = own_objects().filter(func(x): return not x.structure and not before.has(x.id) and str(x.template) == str(train.template))
		if made.size() >= 2:
			break
	print("PLAY1 skirmish: trained %d of %s" % [made.size(), train.template])
	if made.is_empty():
		return "the barracks trained nothing"
	await game._wait_seconds(3.0)
	# a drag box over the new soldiers
	var lo := Vector2(1e9, 1e9)
	var hi := Vector2(-1e9, -1e9)
	await look_by_radar(Vector2(made[0].x, made[0].y))
	for m in made:
		var o: Dictionary = world.get_object(m.id)
		var px := to_pixel(o.x, o.y)
		lo = Vector2(minf(lo.x, px.x), minf(lo.y, px.y))
		hi = Vector2(maxf(hi.x, px.x), maxf(hi.y, px.y))
	lo -= Vector2(40, 40)
	hi += Vector2(40, 40)
	await move_to(lo)
	press(lo, MOUSE_BUTTON_LEFT, true)
	for t in 8:
		await move_to(lo.lerp(hi, float(t + 1) / 8.0))
	press(hi, MOUSE_BUTTON_LEFT, false)
	await game._step(8)
	var sel: Array = hud.get_selection()
	print("PLAY1 skirmish: the drag box selected %s" % str(sel))
	shot("skirmish-army")
	if sel.is_empty():
		return "the drag box selected nothing"
	# to the enemy's base by a right click on the radar
	var enemy := enemy_name()
	var target := Vector2()
	for o in world.get_player_objects(enemy):
		if o.commandcenter:
			target = Vector2(o.x, o.y)
	var start: Dictionary = world.get_object(int(sel[0]))
	var d0 := Vector2(start.x, start.y).distance_to(target)
	var moves := log_count("MSG_DO_MOVETO")
	await click(hud.world_to_radar_pixel(target), MOUSE_BUTTON_RIGHT)
	await game._step(6)
	if log_count("MSG_DO_MOVETO") == moves:
		fails.append("the right click on the radar sent no move")
	await game._wait_seconds(20.0)
	var now: Dictionary = world.get_object(int(sel[0]))
	var d1: float = Vector2(now.x, now.y).distance_to(target) if now.get("ok", false) else -1.0
	print("PLAY1 skirmish: the army's distance to the enemy base %.0f -> %.0f" % [d0, d1])
	await look_by_radar(Vector2(now.get("x", target.x), now.get("y", target.y)))
	shot("skirmish-march")
	if d1 >= 0 and d1 > d0 - 300.0:
		fails.append("the army did not march toward the enemy (%.0f -> %.0f)" % [d0, d1])
	world.set_time_scale(1.0)
	return "; ".join(fails)
