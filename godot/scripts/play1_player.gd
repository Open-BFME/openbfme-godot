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
##   skirmish  a worker builds a barracks (the placement ghost is drawn, red on an illegal spot), the barracks trains, a drag box and a radar right click
##             send the soldiers to the enemy
##   tour      the rest of a player's first hour, each by input and judged by its effect: the minimap moves the view, the fortress recruits a hero
##             whose first ability is used, the barracks buys an upgrade, the spell store sells a power that the spell book casts, a worker builds a
##             wall line, the fortress builds an expansion, soldiers garrison a structure; one PLAY1 tour line per step, the broken ones in the FAIL
##   tribute   (with an ally: --opponents=2 --teams=1,1,2) after NumMinutesBeforePlayersCanTransferMoney the flag's PlayerTribute.apt, its tribute tab, a drag
##             on the ally's slider and Send move the local player's money to the ally (MSG_GIVE_MONEY)
##   ghosts    (lane PLAY-3) the builder is selected by a click; for every DOZER_CONSTRUCT button of its command set (every structure it builds freely) the
##             button is clicked, the pointer moves over the ground around the citadel: the placement ghost (placement_ghost.gd, RotWK RW 0x69C5E6 /
##             RW 0x6A2AE5) shows that structure under the pointer; a right click ends the placement. Run per faction with --faction
##   dragbox   (lane PLAY-3) a left drag around the local units on the screen: while the button is held the drag box is drawn (RotWK's
##             W3DInGameUI::drawSelectionRegion RW 0x48ECF4: the region from the press to the pointer, 0xBBFFBB33, lines of width 2) and follows the pointer;
##             the release selects the own units inside and no structure, and the box is gone
##   portraits (lane UI-4) a builder, a building, a hero and a horde of the local player, each selected with a left click: the Palantir's portrait
##             clip (CommandUI.Portrait) must draw the selection's SelectPortrait (PalantirCommandUI, RW 0x92FE33); play1-portrait-<kind>.png
extends RefCounted

var game: Node
var hud: Node
var world: Node
var shell: Node
var ok_count := 0
var tour_heroes: Array = []      # the heroes the tour recruited (the garrison step takes soldiers)
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


## lane PLAY-3: a headless run's root window is 64 x 64; the scenarios that aim at pixels size it as a window would be (the HUD reads the viewport's size
## every frame)
func _headless_window() -> void:
	if DisplayServer.get_name() == "headless" and window_size().x < 640:
		game.get_tree().root.size = Vector2i(1280, 720)
		await game._step(4)


func _scenario_ghosts() -> String:
	var fails: Array = []
	await _headless_window()
	# money for every structure (a test hook outside the command path, as create_object: a button the player cannot afford starts no placement)
	var local_index := -1
	var idx := 0
	for pl in world.get_report().get("players", []):
		if local_index < 0 and str(pl).contains(game._local_name):
			local_index = idx
		idx += 1
	if local_index >= 0:
		world.give_money(local_index, 50000)
	var base := Vector2()
	for o in own_objects():
		if o.commandcenter:
			base = Vector2(o.x, o.y)
	# the builder: an own unit whose command set offers DOZER_CONSTRUCT buttons
	var builder: Dictionary = {}
	var buttons: Array = []
	for o in own_objects():
		if o.structure:
			continue
		await look_by_radar(Vector2(o.x, o.y))
		if not await select_object(o):
			continue
		await game._step(4)
		buttons = hud.get_command_buttons().filter(func(b): return b.command == "DOZER_CONSTRUCT" and not str(b.template).is_empty())
		if not buttons.is_empty():
			builder = o
			break
	if builder.is_empty():
		return "no own unit offers a DOZER_CONSTRUCT button (%s)" % str(own_objects().map(func(x): return x.template))
	print("PLAY3 ghosts: %s, builder %s, %d structures: %s" % [game._faction, builder.template, buttons.size(), str(buttons.map(func(b): return b.template))])
	var shown := 0
	for b in buttons:
		await look_by_radar(base)
		if not hud.get_selection().has(builder.id):
			await look_by_radar(Vector2(world.get_object(builder.id).x, world.get_object(builder.id).y))
			await select_object(builder)
			await look_by_radar(base)
		var bp := palantir_button(b.frame)
		if bp.x < 0:
			fails.append("%s: its button %s is not on the screen" % [b.template, b.frame])
			continue
		await click(bp)
		await game._step(6)
		var pl: Dictionary = hud.get_placement()
		if not pl.get("placing", false):
			fails.append("%s: the button (state %d, cost %d) started no placement" % [b.template, int(b.state), int(b.cost)])
			continue
		var ghost: Node = game._placement_ghost
		var seen := ""
		var legal_seen := false
		var illegal_seen := false
		for k in 10:
			var a := TAU * float(k) / 10.0
			var p := base + Vector2(cos(a), sin(a)) * (70.0 + 45.0 * k)
			var px := to_pixel(p.x, p.y)
			if not on_screen(px):
				continue
			await move_to(px)
			await game._step(3)
			pl = hud.get_placement()
			if pl.get("has_ghost", false) and ghost.shown_template == b.template:
				seen = ghost.shown_template
				if int(pl.legal) == 0:
					legal_seen = legal_seen or ghost.shown_tint.a == 0.0
				else:
					illegal_seen = illegal_seen or ghost.shown_tint.a > 0.0
		print("PLAY3 ghosts: %s %s: ghost %s, legal (untinted) %s, illegal (tinted) %s" % [game._faction, b.template, seen if seen != "" else "NONE", legal_seen, illegal_seen])
		if seen == "":
			fails.append("%s: no ghost under the pointer (ghost node '%s', errors %s, placement %s)" % [b.template, ghost.shown_template, str(ghost.errors), str(pl)])
		else:
			shown += 1
			if shown == 1:
				shot("ghost-" + game._faction.to_lower())
			if ghost.shown_pieces > 0:
				shot("ghost-castle-" + game._faction.to_lower())
				print("PLAY3 ghosts: %s %s: a castle, %d layout pieces drawn" % [game._faction, b.template, ghost.shown_pieces])
		await click(window_size() * 0.5, MOUSE_BUTTON_RIGHT)
		await game._step(4)
		if hud.get_placement().get("placing", false):
			fails.append("%s: the right click did not end the placement" % b.template)
			await click(window_size() * 0.5, MOUSE_BUTTON_RIGHT)
	print("PLAY3 ghosts: %s: %d of %d structures showed their ghost" % [game._faction, shown, buttons.size()])
	return "" if fails.is_empty() else "; ".join(fails)


func _scenario_dragbox() -> String:
	await _headless_window()
	# the local units on the screen around one of them (a skirmish starts with the builders beside the citadel)
	var units := local_units()
	var inside: Array = []
	for u in units:
		hud.camera_look_at(Vector2(u.x, u.y))
		hud.camera_set_height(260.0)
		await game._step(10)
		inside.clear()
		print("PLAY3 dragbox: unit %s at (%.0f, %.0f) pixel %s, window %s" % [u.template, u.x, u.y, to_pixel(u.x, u.y), window_size()])
		for v in local_units():
			if on_screen(to_pixel(v.x, v.y)):
				inside.append(v)
		if not inside.is_empty():
			break
	if inside.is_empty():
		return "no local unit on the screen (%d units)" % units.size()
	var lo := Vector2(1e9, 1e9)
	var hi := Vector2(-1e9, -1e9)
	for v in inside:
		var p := to_pixel(v.x, v.y)
		lo = Vector2(minf(lo.x, p.x), minf(lo.y, p.y))
		hi = Vector2(maxf(hi.x, p.x), maxf(hi.y, p.y))
	lo = (lo - Vector2(40, 40)).floor()
	hi = (hi + Vector2(40, 30)).floor()
	print("PLAY3 dragbox: %d own units on the screen inside %s - %s" % [inside.size(), lo, hi])
	await move_to(lo)
	press(lo, MOUSE_BUTTON_LEFT, true)
	await game._step(2)
	var at_press: Dictionary = hud.get_state().get("drag_box", {})
	if at_press.get("shown", true):
		return "a box is drawn at the press, before the pointer moved (%s)" % str(at_press)
	var mid := lo.lerp(hi, 0.5).floor()
	for t in 4:
		await move_to(lo.lerp(mid, float(t + 1) / 4.0).floor())
	var half: Dictionary = hud.get_state().get("drag_box", {})
	print("PLAY3 dragbox: half way, the box %s" % str(half))
	if not half.get("shown", false):
		return "no box is drawn while the left button drags (%s)" % str(half)
	if half.rect != Rect2(lo, mid - lo):
		return "the box %s is not the region from the press %s to the pointer %s" % [str(half.rect), lo, mid]
	if int(half.color) != 0xBBFFBB33 or absf(float(half.width) - 2.0) > 0.001:
		return "the box's colour %x / width %s are not RotWK's 0xBBFFBB33 / 2" % [int(half.color), str(half.width)]
	for t in 4:
		await move_to(mid.lerp(hi, float(t + 1) / 4.0).floor())
	var full: Dictionary = hud.get_state().get("drag_box", {})
	shot("dragbox")
	if not full.get("shown", false) or full.rect != Rect2(lo, hi - lo):
		return "the box did not follow the pointer to %s (%s)" % [hi, str(full)]
	press(hi, MOUSE_BUTTON_LEFT, false)
	await game._step(8)
	var after: Dictionary = hud.get_state().get("drag_box", {})
	if after.get("shown", true):
		return "the box is still drawn after the release (%s)" % str(after)
	var sel: Array = hud.get_selection()
	print("PLAY3 dragbox: the release selected %s (box drawn in %d frames)" % [str(sel), int(full.frames)])
	var ids := {}
	for v in inside:
		ids[int(v.id)] = true
	var hit := 0
	for e in sel:
		var id := int(e.get("id", -1)) if e is Dictionary else int(e)
		if ids.has(id):
			hit += 1
		for o in world.get_player_objects(game._local_name):
			if int(o.id) == id and o.structure:
				return "the drag box selected the structure %s" % str(o.template)
	if hit == 0:
		return "the drag box selected none of the %d own units inside it" % inside.size()
	return ""


# lane UI-4: the first template of `names` the world creates for the local player beside `at` (its id, or -1)
func _create_first(names: Array, at: Vector2) -> Dictionary:
	for n in names:
		var id: int = world.create_object(n, game._local_index, at.x, at.y, 0.0)
		if id > 0:
			return {"id": id, "template": n}
	return {}


func _scenario_portraits() -> String:
	if game._local_index < 0:
		return "no local player index"
	var cases: Array = []
	for o in world.get_player_objects(game._local_name):
		if o.structure and cases.filter(func(c): return c.kind == "building").is_empty():
			cases.append({"kind": "building", "id": o.id, "template": o.template})
		elif not o.structure and cases.filter(func(c): return c.kind == "builder").is_empty():
			cases.append({"kind": "builder", "id": o.id, "template": o.template})
	var base: Dictionary = cases.filter(func(c): return c.kind == "builder")[0] if not cases.filter(func(c): return c.kind == "builder").is_empty() else {}
	if base.is_empty():
		return "no local builder"
	var spot := Vector2()
	for o in world.get_player_objects(game._local_name):
		if o.id == base.id:
			spot = Vector2(o.x, o.y)
	# a hero and a horde of the Men's lists (the default faction), else of the other sides
	var hero := _create_first(["GondorBoromir", "GondorFaramir", "GondorAragorn", "RohanTheoden", "MordorWitchKing", "IsengardSaruman"], spot + Vector2(260, 0))
	var horde := _create_first(["GondorFighterHorde", "RohanPeasantHorde", "MordorOrcFighterHorde", "IsengardUrukHorde"], spot + Vector2(-260, 120))
	if hero.is_empty() or horde.is_empty():
		return "could not create a hero (%s) or a horde (%s): %s" % [str(hero), str(horde), str(world.get_command_errors() if world.has_method("get_command_errors") else "")]
	hero.kind = "hero"
	horde.kind = "horde"
	cases.append(hero)
	cases.append(horde)
	await game._step(60)
	var failures: PackedStringArray = []
	for c in cases:
		var pos := Vector2()
		for o in world.get_player_objects(game._local_name):
			if o.id == c.id:
				pos = Vector2(o.x, o.y)
		if c.kind == "horde":
			# a horde is clicked on one of its members (the nearest object of another template, not the hero)
			var best := 1.0e9
			var centre := pos
			for o in world.get_player_objects(game._local_name):
				var d := Vector2(o.x, o.y).distance_to(centre)
				if o.id != c.id and o.id != hero.id and not o.structure and d < best and d < 200.0:
					best = d
					pos = Vector2(o.x, o.y)
		hud.camera_look_at(pos)
		await game._step(20)
		await click(to_pixel(pos.x, pos.y))
		await game._step(30)
		var st: Dictionary = hud.get_state()
		var sel: Array = hud.get_selection()
		print("PLAY1 portraits: %s %s (%d): selection %s, portrait '%s', drawn '%s' (%d draws)" % [c.kind, c.template, c.id, str(sel), st.get("portrait", "?"),
			st.get("portrait_drawn", "?"), st.get("portrait_draws", 0)])
		shot("portrait-" + c.kind)
		var others: Array = cases.filter(func(o): return o.kind != c.kind).map(func(o): return o.id)
		if sel.is_empty() or sel.any(func(i): return others.has(i)):
			failures.append("%s: the click selected %s" % [c.kind, str(sel)])
		elif String(st.get("portrait", "")).is_empty():
			failures.append("%s: no portrait for the selection" % c.kind)
		elif st.get("portrait_drawn", "") != st.get("portrait", ""):
			failures.append("%s: the portrait %s was not drawn (drew '%s')" % [c.kind, st.get("portrait"), st.get("portrait_drawn")])
	return "; ".join(failures)


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


func money_of(name: String) -> int:
	for p in world.get_economy().players:
		if p.name == name:
			return int(p.money)
	return -1


func _scenario_tribute() -> String:
	# RW 0x626087: NumMinutesBeforePlayersCanTransferMoney (5 minutes = 1500 logic frames) before the tribute page opens
	world.set_time_scale(8.0)
	var t0 := Time.get_ticks_msec()
	while int(world.get_economy().frame) < 1520 and Time.get_ticks_msec() - t0 < 900000:
		await game._wait_seconds(1.0)
	world.set_time_scale(1.0)
	if int(world.get_economy().frame) < 1520:
		return "the game did not reach the tribute delay (frame %d)" % int(world.get_economy().frame)
	var flag := palantir_button("PalantirButtons.Buttons.Objectives")
	await click(flag)
	await game._wait_seconds(2.0)
	if not shell.shell_stack().has("PlayerTribute.apt"):
		return "the flag did not open PlayerTribute.apt"
	var lvl: int = shell.shell_top_level()
	await click(shell_button(lvl, "OuterFrame.Pages.tributeTab"))
	await game._wait_seconds(1.5)
	shot("tribute-page")
	var row := "_level%d.OuterFrame.Pages.TributePage.enabledContent.Table.1" % lvl
	var r: Rect2 = shell.component_rect(row + ".ColoredItems.Slider.instance1")
	print("PLAY1 tribute: the ally row's slider %s" % r)
	if r.size.x <= 0:
		await game._press_escape()
		return "the tribute page shows no ally row slider (an ally: --opponents=2 --teams=1,1,2)"
	var others := {}
	for p in world.get_economy().players:
		if p.playable and p.name != game._local_name:
			others[p.name] = int(p.money)
	var mine := money_of(game._local_name)
	# a drag of the slider's thumb from its left end to a fifth of the way
	var a: Vector2 = shell.stage_to_window(Vector2(r.position.x + 4, r.position.y + r.size.y * 0.5))
	var b: Vector2 = shell.stage_to_window(Vector2(r.position.x + r.size.x * 0.2, r.position.y + r.size.y * 0.5))
	await move_to(a)
	press(a, MOUSE_BUTTON_LEFT, true)
	for t in 8:
		await move_to(a.lerp(b, float(t + 1) / 8.0))
	press(b, MOUSE_BUTTON_LEFT, false)
	await game._step(4)
	shot("tribute-slider")
	var mine_before := money_of(game._local_name)
	await click(shell_button(lvl, "OuterFrame.Pages.TributePage.enabledContent.MainButtons.Send"))
	await game._wait_seconds(3.0)
	var mine_after := money_of(game._local_name)
	var gained := 0
	var who := ""
	for n in others.keys():
		var d: int = money_of(n) - int(others[n])
		if d > gained:
			gained = d
			who = n
	print("PLAY1 tribute: local money %d -> %d -> %d; %s gained %d; shell %s" % [mine, mine_before, mine_after, who, gained, shell.shell_stack()])
	if shell.shell_stack().has("PlayerTribute.apt"):
		return "Send did not close the tribute screen"
	if mine_after >= mine_before - 50 or gained < 50:
		return "Send moved no money (local %d -> %d, best gain %d)" % [mine_before, mine_after, gained]
	return ""


# ---- tour ------------------------------------------------------------------------------------------------------------------------------------------------

const WORKERS := ["Porter", "Worker", "Builder", "Slave", "Peasant", "Thrall", "Serf"]


func is_worker(template: String) -> bool:
	if template.contains("ThrallMaster"): # Angmar's Thrall Master is infantry (KindOf INFANTRY CAN_ATTACK), not a builder
		return false
	for w in WORKERS:
		if template.contains(w):
			return true
	return false


func messages_since(n: int) -> Array:
	var log: PackedStringArray = hud.get_message_log()
	var out: Array = []
	for i in range(n, log.size()):
		out.append(log[i])
	return out


func command_button(pred: Callable) -> Dictionary:
	for b in hud.get_command_buttons():
		if pred.call(b):
			return b
	return {}


func button_list() -> String:
	return JSON.stringify(hud.get_command_buttons().map(func(b): return "%s:%s:%s:%d" % [b.command, b.template if b.template != "" else (b.upgrade if b.upgrade != "" else b.power), b.name, int(b.state)]))


## a page of the command set (RotWK's fortress: Command_SelectRevivables<Side>Fortress holds the heroes, Command_SelectUpgrades<Side>Fortress the
## expansions): its PUSH_VISIBLE_COMMAND_RANGE button whose name has `key`; true when it was clicked
func open_page(key: String) -> bool:
	var b := command_button(func(x): return x.command == "PUSH_VISIBLE_COMMAND_RANGE" and str(x.name).contains(key) and int(x.state) == 1)
	if b.is_empty():
		return false
	await click(palantir_button(b.frame))
	await game._step(6)
	return true


func is_garrison_template(t: String) -> bool:
	return t.contains("Garrison") or t.contains("SentryTower")


## the placement under way goes to the first legal site on rings around `center` (the HUD's own verdict); true when a click placed it
func place_near(center: Vector2) -> bool:
	for ring in range(2, 9):
		for k in 12:
			var p := center + Vector2(cos(TAU * k / 12.0), sin(TAU * k / 12.0)) * 90.0 * ring
			var px := to_pixel(p.x, p.y)
			if not on_screen(px):
				await look_by_radar(p)
				px = to_pixel(p.x, p.y)
				if not on_screen(px):
					continue
			await move_to(px)
			await game._step(2)
			var pl: Dictionary = hud.get_placement()
			if not pl.get("placing", false):
				return false
			if int(pl.get("legal", -1)) == 0:
				await click(px)
				await game._step(4)
				return true
	await click(window_size() * 0.5, MOUSE_BUTTON_RIGHT)
	return false


func select_own(pred: Callable) -> Dictionary:
	for o in own_objects():
		if pred.call(o):
			await look_by_radar(Vector2(o.x, o.y))
			if await select_object(o):
				return o
	return {}


func _scenario_tour() -> String:
	var fails: Array = []
	world.set_time_scale(4.0)
	var base := Vector2()
	for o in own_objects():
		if o.commandcenter:
			base = Vector2(o.x, o.y)
	# the minimap: a left click on the radar moves the view there
	var enemy := enemy_name()
	var enemy_base := Vector2()
	for o in world.get_player_objects(enemy):
		if o.commandcenter:
			enemy_base = Vector2(o.x, o.y)
	await look_by_radar(base)
	var c0: Dictionary = hud.get_camera()
	await look_by_radar(enemy_base)
	var c1: Dictionary = hud.get_camera()
	await look_by_radar(base)
	print("PLAY1 tour minimap: the view %s -> %s" % [JSON.stringify(c0), JSON.stringify(c1)])
	if JSON.stringify(c0) == JSON.stringify(c1):
		fails.append("minimap: a radar click did not move the view")
	for step in ["economy", "hero", "expansion", "garrison", "upgrade", "spell", "wall"]:
		print("PLAY1 tour %s: start (logic frame %d)" % [step, int(world.get_economy().frame)])
		var r: String = await call("_tour_" + step, base)
		print("PLAY1 tour %s: %s" % [step, "ok" if r == "" else r])
		if r != "" and not r.begins_with("n/a"):
			fails.append("%s: %s" % [step, r])
	world.set_time_scale(1.0)
	return "; ".join(fails)


## a worker builds the first two buildings of its set (every retail faction's economy building is its builder's first DOZER_CONSTRUCT button): the fortress
## alone brings no money, and the later steps (a unit to garrison, an upgrade) need an income
func _tour_economy(base: Vector2) -> String:
	var built := 0
	for n in 2:
		var worker := await select_own(func(o): return not o.structure and is_worker(str(o.template)))
		if worker.is_empty():
			return "no worker could be selected"
		var eb := command_button(func(x): return x.command == "DOZER_CONSTRUCT" and not str(x.template).is_empty())
		if eb.is_empty():
			return "the worker offers no building"
		for i in 120:
			eb = command_button(func(x): return x.command == "DOZER_CONSTRUCT" and x.template == eb.template)
			if int(eb.get("state", 0)) == 1:
				break
			await game._wait_seconds(0.5)
		await click(palantir_button(eb.frame))
		await game._step(6)
		var placed := await place_near(base)
		print("PLAY1 tour economy: the worker builds %s: placed %s" % [eb.template, placed])
		if placed:
			built += 1
	return "" if built > 0 else "no economy building could be placed"


## the fortress recruits a hero (its first affordable UNIT_BUILD that is not a worker), the hero is selected by a click and its first ready ability is used
func _tour_hero(base: Vector2) -> String:
	var fort := await select_own(func(o): return o.commandcenter)
	if fort.is_empty():
		return "the fortress could not be selected"
	var paged := await open_page("Revivables")
	print("PLAY1 tour hero: the fortress's heroes page (%s) %s" % [paged, button_list()])
	# RotWK recruits a hero through the fortress's revive slots (REVIVE, Command_GenericReviveSlotN); a hero whose abilities are all locked at its first
	# level (Gothmog) makes the next slot's hero the one to try
	var r := await _tour_one_hero(fort, 0)
	if r.begins_with("n/a"):
		await select_own(func(o): return o.commandcenter)
		await open_page("Revivables")
		var r2 := await _tour_one_hero(fort, 0) # the first hero's slot is taken now
		if r2 == "" or not r2.begins_with("n/a"):
			return r2
	return r


func _tour_one_hero(fort: Dictionary, skip: int) -> String:
	# a slot the player cannot pay for yet (state 3, CantAfford) is waited for: the economy runs at four times speed
	var ready: Array = []
	for i in 180:
		ready = hud.get_command_buttons().filter(func(x): return x.command == "REVIVE" and int(x.state) == 1)
		if ready.size() > skip or hud.get_command_buttons().filter(func(x): return x.command == "REVIVE" and int(x.state) == 3).is_empty():
			break
		await game._wait_seconds(0.5)
	var b: Dictionary = ready[skip] if skip < ready.size() else {}
	if b.is_empty():
		b = command_button(func(x): return x.command == "UNIT_BUILD" and not is_worker(str(x.template)) and int(x.state) == 1)
	if b.is_empty():
		return "the fortress offers no hero to recruit (%s)" % button_list()
	var before: Array = own_objects().map(func(x): return x.id)
	await click(palantir_button(b.frame))
	await game._step(4)
	var hero: Dictionary = {}
	for i in 240:
		for o in own_objects():
			if not before.has(o.id) and not o.structure and not is_worker(str(o.template)) and not str(o.template).contains("Horde"):
				hero = o
		if not hero.is_empty():
			break
		await game._wait_seconds(0.5)
	if hero.is_empty():
		return "the fortress did not recruit %s in 120 s" % b.template
	tour_heroes.append(int(hero.id))
	await game._wait_seconds(4.0)
	var recruited: Dictionary = hero
	hero = world.get_object(hero.id)
	if not hero.get("ok", false):
		return "n/a: the recruited %s is gone" % recruited.template # the next slot's hero is tried
	await look_by_radar(Vector2(hero.x, hero.y))
	if not await select_object(hero):
		return "the hero %s could not be selected by a click" % hero.template
	print("PLAY1 tour hero: %s's buttons %s" % [hero.template, button_list()])
	var ab := command_button(func(x): return str(x.command).begins_with("SPECIAL_POWER") and int(x.state) == 1)
	if ab.is_empty():
		ab = command_button(func(x): return x.command in ["TOGGLE_STANCE", "SET_STANCE", "HORDE_TOGGLE_FORMATION", "TOGGLE_WEAPON"] and int(x.state) == 1)
	if ab.is_empty():
		return "n/a: %s's abilities are all locked at its first level" % hero.template
	var n: int = hud.get_message_log().size()
	await click(palantir_button(ab.frame))
	await game._step(6)
	if messages_since(n).is_empty():
		# a targeted ability (Smite, ...): the next click on an enemy gives the target; the nearest enemy or creep unit, else structure
		var at: Dictionary = world.get_object(hero.id)
		var here := Vector2(at.x, at.y)
		var best: Dictionary = {}
		for who in [enemy_name(), "PlyrCreeps"]:
			for o in world.get_player_objects(who):
				var better: bool = best.is_empty() or (best.structure and not o.structure) or (best.structure == o.structure and here.distance_to(Vector2(o.x, o.y)) < here.distance_to(Vector2(best.x, best.y)))
				if better:
					best = o
		print("PLAY1 tour hero: armed %s (gui command %s), target %s" % [ab.name, hud.get_state().get("gui_command", "?"), JSON.stringify(best)])
		if not best.is_empty():
			await look_by_radar(Vector2(best.x, best.y))
			var tgt: Dictionary = world.get_object(best.id)
			await click(to_pixel(tgt.x, tgt.y))
			await game._step(6)
	var sent := messages_since(n)
	print("PLAY1 tour hero: %s's %s (%s) sent %s" % [hero.template, ab.name, ab.power, JSON.stringify(sent)])
	shot("tour-hero")
	if sent.is_empty():
		return "%s's ability %s sent nothing" % [hero.template, ab.name]
	return ""


## the barracks (or any structure with one) buys an upgrade
func _tour_upgrade(base: Vector2) -> String:
	for o in own_objects():
		if not o.structure or o.commandcenter:
			continue
		if not await select_object(o):
			await look_by_radar(Vector2(o.x, o.y))
			if not await select_object(o):
				continue
		# an upgrade it can afford (state 1); one short of the money (state 3) is waited for as the income comes in (four times speed)
		var b := {}
		for i in 120:
			b = command_button(func(x): return x.command in ["OBJECT_UPGRADE", "PLAYER_UPGRADE"] and int(x.state) == 1)
			if not b.is_empty() or command_button(func(x): return x.command in ["OBJECT_UPGRADE", "PLAYER_UPGRADE"] and int(x.state) == 3).is_empty():
				break
			await game._wait_seconds(0.5)
		if b.is_empty():
			continue
		# a click short of the cost is refused (GUI:NotEnoughMoneyToUpgrade): the click is repeated as the money comes in (four times speed)
		var n: int = hud.get_message_log().size()
		var m0 := money_of(game._local_name)
		for attempt in 12:
			await click(palantir_button(b.frame))
			await game._step(6)
			if not messages_since(n).is_empty():
				break
			await game._wait_seconds(5.0)
		var sent := messages_since(n)
		print("PLAY1 tour upgrade: %s's %s (%s): %s, money %d -> %d" % [o.template, b.name, b.upgrade, JSON.stringify(sent), m0, money_of(game._local_name)])
		shot("tour-upgrade")
		if sent.is_empty():
			return "%s's upgrade %s sent nothing" % [o.template, b.upgrade]
		return ""
	return "no structure offers an upgrade it can buy now"


## the spell store sells a power (the powers scenario's clicks) and the spell book's slot casts it
func _tour_spell(base: Vector2) -> String:
	var magic := palantir_button("PalantirButtons.Buttons.PlayerMagic")
	await click(magic)
	await game._wait_seconds(2.0)
	var st := store_state()
	if not st.get("open", false):
		return "the powers button did not open the store"
	var first: int = st.states.find("_active")
	if first < 0:
		await click(shell_button(st.level, "SpellStore.Buttons.ButtonsMain.Accept"))
		await game._wait_seconds(2.0)
		return "the store has no power to buy (%d points)" % st.points
	print("PLAY1 tour spell: store level %d, points %d, states %s, power %d at %s %s" % [st.level, st.points, JSON.stringify(st.states), first + 1, shell_button(st.level, "SpellStore.Buttons.Spell%d" % (first + 1)), JSON.stringify(shell.find_button(st.level, "SpellStore.Buttons.Spell%d" % (first + 1)))])
	shot("tour-spell-store")
	for attempt in 8: # a slow frame rate late in a game stretches the store's opening animation; its powers take clicks once it has played
		await click(shell_button(st.level, "SpellStore.Buttons.Spell%d" % (first + 1)))
		await game._wait_seconds(1.5) # the movie's generic button runs its callback when its _down animation ends
		if store_state().get("states", [])[first] != "_active":
			break
	print("PLAY1 tour spell: power %d picked: %s" % [first + 1, store_state().get("states", [])[first]])
	await click(shell_button(st.level, "SpellStore.Buttons.ButtonsMain.Accept"))
	await game._wait_seconds(3.0)
	var sb: Dictionary = hud.get_spellbook_state()
	var slots: Array = sb.get("slots", [])
	var k := slots.find("_up")
	if k < 0:
		# a bought power the book shows _disabled needs a COMMANDCENTER (the spell book's RequirementsFilterMPSkirmish = SPELL_BOOK_REQUIREMENTS_FILTER,
		# RW 0x8969E5): without one standing (the enemy took the citadel) that is retail's answer, not a failure of the store
		var keeps: Array = own_objects().filter(func(o): return o.commandcenter)
		print("PLAY1 tour spell: the bought power is %s; COMMANDCENTERs standing: %s" % [str(slots[0]) if not slots.is_empty() else "?", keeps.map(func(o): return o.template)])
		if slots.has("_disabled") and keeps.is_empty():
			return ""
		return "no spell book slot is ready after the purchase (%s)" % JSON.stringify(slots)
	var path: String = str(sb.get("path", ""))
	if path.begins_with("_level"):
		path = path.substr(path.find(".") + 1)
	var slot_px := Vector2(-1, -1)
	for cand in ["%s.Spell%d", "%s.Spells.Spell%d", "%s.Buttons.Spell%d", "%s.Spell%dClip"]:
		var px := palantir_button(cand % [path, k + 1])
		if px.x >= 0:
			slot_px = px
			break
	if slot_px.x < 0:
		print("PLAY1 tour spell: the spell book %s: %s" % [path, hud.dump_tree(8).split("\n").filter(func(l): return l.contains("pell")).slice(0, 40)])
		return "the spell book's slot %d was not found under %s" % [k + 1, path]
	var n: int = hud.get_message_log().size()
	await click(slot_px)
	await game._step(6)
	if hud.get_spellbook_state().get("targeting", false):
		var target := base.lerp(Vector2(base.x + 300, base.y), 1.0)
		await look_by_radar(target)
		await click(to_pixel(target.x, target.y))
		await game._step(6)
	var after: Array = hud.get_spellbook_state().get("slots", [])
	print("PLAY1 tour spell: slot %d at %s: %s -> %s, messages %s" % [k + 1, slot_px, slots[k], after[k] if k < after.size() else "?", JSON.stringify(messages_since(n))])
	shot("tour-spell")
	if k < after.size() and after[k] == "_up" and messages_since(n).is_empty():
		return "the spell book's slot %d did not cast" % (k + 1)
	return ""


## a worker builds a wall line: its wall button, a legal start and a click further along
func _tour_wall(base: Vector2) -> String:
	var worker := await select_own(func(o): return not o.structure and is_worker(str(o.template)))
	if worker.is_empty():
		print("PLAY1 tour wall: the workers %s" % JSON.stringify(own_objects().filter(func(o): return not o.structure and is_worker(str(o.template))).map(func(o): return [o.template, int(o.x), int(o.y), world.get_object(o.id).get("contained_by", 0), world.get_object(o.id).get("dozer_task", "")])))
		return "no worker could be selected"
	var b := command_button(func(x): return x.command == "DOZER_CONSTRUCT" and str(x.template).to_lower().contains("wall") and int(x.state) == 1)
	if b.is_empty():
		return "n/a: the worker offers no wall (RotWK gives walls to Men, Elves and Dwarves; the fortress's expansions are its walls)"
	await click(palantir_button(b.frame))
	await game._step(6)
	if not hud.get_placement().get("placing", false):
		return "the wall button did not start the placement"
	var a := Vector2(-1, -1)
	var aw := Vector2()
	for ring in range(3, 8):
		for k in 12:
			var p := base + Vector2(cos(TAU * k / 12.0), sin(TAU * k / 12.0)) * 100.0 * ring
			var px := to_pixel(p.x, p.y)
			if not on_screen(px):
				continue
			await move_to(px)
			await game._step(2)
			if int(hud.get_placement().get("legal", -1)) == 0:
				a = px
				aw = p
				break
		if a.x >= 0:
			break
	if a.x < 0:
		await click(window_size() * 0.5, MOUSE_BUTTON_RIGHT)
		return "no legal start for the wall on the screen"
	var before: Array = own_objects().map(func(x): return x.id)
	var n: int = hud.get_message_log().size()
	await click(a)
	await game._step(4)
	var dir := (aw - base).normalized().orthogonal()
	var bw := aw + dir * 160.0
	await move_to(to_pixel(bw.x, bw.y))
	await game._step(4)
	shot("tour-wall-line")
	await click(to_pixel(bw.x, bw.y))
	await game._step(6)
	var sent := messages_since(n)
	var walls: Array = []
	for i in 60:
		walls = own_objects().filter(func(o): return not before.has(o.id) and o.structure)
		if not walls.is_empty():
			break
		await game._wait_seconds(0.5)
	print("PLAY1 tour wall: %s from %s: messages %s, new structures %s" % [b.template, aw, JSON.stringify(sent), JSON.stringify(walls.map(func(o): return o.template))])
	shot("tour-wall")
	if walls.is_empty():
		return "the wall line placed nothing (messages %s)" % JSON.stringify(sent)
	return ""


## the fortress builds an expansion: its first ready button that is neither a unit nor an upgrade
func _tour_expansion(base: Vector2) -> String:
	# RotWK's fortress builds its expansions on its pads (<Side>FortressExpansionPad*): a garrisonable one first (the garrison step enters it)
	var pad := await select_own(func(o): return str(o.template).contains("ExpansionPad"))
	if not pad.is_empty():
		print("PLAY1 tour expansion: %s's buttons %s" % [pad.template, button_list()])
		var e := {}
		for i in 180:
			e = command_button(func(x): return int(x.state) == 1 and str(x.template).contains("Garrison"))
			if e.is_empty():
				e = command_button(func(x): return int(x.state) == 1 and x.command in ["DOZER_CONSTRUCT", "FOUNDATION_CONSTRUCT", "CASTLE_UPGRADE", "UNIT_BUILD"])
			if not e.is_empty():
				break
			await game._wait_seconds(0.5) # the money
		if e.is_empty():
			return "the pad %s offers no expansion it can build in 90 s (%s)" % [pad.template, button_list()]
		var before: Array = own_objects().map(func(x): return x.id)
		var n: int = hud.get_message_log().size()
		await click(palantir_button(e.frame))
		await game._step(6)
		if hud.get_placement().get("placing", false):
			await click(to_pixel(pad.x, pad.y))
			await game._step(6)
		var built: Array = []
		for i in 80:
			built = own_objects().filter(func(o): return not before.has(o.id) and o.structure)
			if not built.is_empty():
				break
			await game._wait_seconds(0.5)
		print("PLAY1 tour expansion: %s on %s: messages %s, new structures %s" % [e.template, pad.template, JSON.stringify(messages_since(n)), JSON.stringify(built.map(func(o): return o.template))])
		shot("tour-expansion")
		if messages_since(n).is_empty() and built.is_empty():
			return "the pad's %s did nothing" % e.name
		return ""
	var fort := await select_own(func(o): return o.commandcenter)
	if fort.is_empty():
		return "the fortress could not be selected"
	var paged := await open_page("Upgrades")
	print("PLAY1 tour expansion: the fortress's upgrades page (%s) %s" % [paged, button_list()])
	# a fortress expansion (CASTLE_UPGRADE) first, else the first ready upgrade of the page
	var b := command_button(func(x): return int(x.state) == 1 and x.command == "CASTLE_UPGRADE")
	if b.is_empty():
		b = command_button(func(x): return int(x.state) == 1 and (x.command in ["CASTLE_UPGRADE", "FOUNDATION_CONSTRUCT", "DOZER_CONSTRUCT", "OBJECT_UPGRADE", "PLAYER_UPGRADE"] or str(x.template).to_lower().contains("expansion")))
	if b.is_empty():
		return "the fortress offers no expansion it can build now (%s)" % button_list()
	var before: Array = own_objects().map(func(x): return x.id)
	var n: int = hud.get_message_log().size()
	await click(palantir_button(b.frame))
	await game._step(6)
	if hud.get_placement().get("placing", false):
		# a placed expansion: the first legal spot near the fortress
		for k in 24:
			var p := base + Vector2(cos(TAU * k / 24.0), sin(TAU * k / 24.0)) * 140.0
			await move_to(to_pixel(p.x, p.y))
			await game._step(2)
			if int(hud.get_placement().get("legal", -1)) == 0:
				await click(to_pixel(p.x, p.y))
				break
		await game._step(6)
	var sent := messages_since(n)
	var built: Array = []
	for i in 80:
		built = own_objects().filter(func(o): return not before.has(o.id) and o.structure)
		if not built.is_empty():
			break
		await game._wait_seconds(0.5)
	print("PLAY1 tour expansion: %s %s: messages %s, new structures %s" % [b.command, b.template if b.template != "" else b.upgrade, JSON.stringify(sent), JSON.stringify(built.map(func(o): return o.template))])
	shot("tour-expansion")
	if sent.is_empty():
		return "the fortress's %s sent nothing" % b.name
	return ""


## soldiers garrison one of the player's structures: the right click where the cursor says it can be entered
func _tour_garrison(base: Vector2) -> String:
	# a garrisonable structure being built (the expansion step's) is waited for
	for i in 120:
		var busy := false
		for o in own_objects():
			if o.structure and (str(o.template).contains("Garrison") or str(o.template).contains("Tower")) and world.get_object(o.id).get("under_construction", false):
				busy = true
		if not busy:
			break
		await game._wait_seconds(0.5)
	# the side's own garrisonable building when its worker has one and none stands (Angmar: AngmarSentryTower)
	if own_objects().filter(func(o): return is_garrison_template(str(o.template))).is_empty():
		var worker := await select_own(func(o): return not o.structure and is_worker(str(o.template)))
		var tb := command_button(func(x): return x.command == "DOZER_CONSTRUCT" and is_garrison_template(str(x.template)))
		if not worker.is_empty() and not tb.is_empty():
			for i in 180:
				tb = command_button(func(x): return x.command == "DOZER_CONSTRUCT" and is_garrison_template(str(x.template)))
				if int(tb.get("state", 0)) == 1:
					break
				await game._wait_seconds(0.5)
			await click(palantir_button(tb.frame))
			await game._step(6)
			var placed := await place_near(base)
			print("PLAY1 tour garrison: the worker builds %s: placed %s" % [tb.template, placed])
			for i in 360:
				var done := own_objects().filter(func(o): return o.template == tb.template and not world.get_object(o.id).get("under_construction", true))
				if not done.is_empty():
					break
				await game._wait_seconds(0.5)
	# a soldier (one marching elsewhere walks back to the building): else the barracks trains one
	var home := func(o): return not o.structure and not is_worker(str(o.template)) and not tour_heroes.has(int(o.id))
	var unit := await select_own(home)
	if unit.is_empty():
		# the army is away (fighting: a click on it misses): the barracks trains one at home
		var known: Array = own_objects().map(func(x): return x.id)
		var fresh := func(o): return not o.structure and not is_worker(str(o.template)) and not tour_heroes.has(int(o.id)) and not known.has(o.id)
		var barracks := await select_own(func(o): return o.structure and (str(o.template).contains("Barracks") or str(o.template).contains("OrcPit")))
		var ub := {}
		for i in 240: # the money for it (four times speed)
			ub = command_button(func(x): return x.command == "UNIT_BUILD" and not is_worker(str(x.template)) and int(x.state) == 1)
			if not ub.is_empty():
				break
			await game._wait_seconds(0.5)
		print("PLAY1 tour garrison: no soldier selected (%d candidates); the barracks %s trains %s; its buttons %s; money %s" % [own_objects().filter(home).size(), barracks.get("template", "?"), ub.get("template", "?"),
			JSON.stringify(hud.get_command_buttons().map(func(x): return "%s:%s:%d" % [x.command, x.template, int(x.state)])), str(hud.get_state().get("control_bar", {}).get("money", "?"))])
		if not barracks.is_empty() and not ub.is_empty():
			var n0: int = hud.get_message_log().size()
			for i in 12:
				await click(palantir_button(ub.frame))
				await game._step(6)
				if messages_since(n0).any(func(m): return str(m).begins_with("MSG_QUEUE_UNIT_CREATE")):
					break
				await game._wait_seconds(5.0)
			for i in 240:
				if not own_objects().filter(fresh).is_empty():
					break
				await game._wait_seconds(0.5)
			await game._wait_seconds(3.0)
		unit = await select_own(fresh)
	if unit.is_empty():
		return "no soldier could be selected"
	var candidates: Array = own_objects().filter(func(o): return o.structure)
	for neutral in ["", "PlyrCivilian", "PlyrCreeps"]:
		for o in world.get_player_objects(neutral):
			if o.structure and Vector2(o.x, o.y).distance_to(base) < 2500.0:
				candidates.append(o)
	var cursors := {}
	for o in candidates:
		await look_by_radar(Vector2(o.x, o.y))
		var px := to_pixel(o.x, o.y)
		if not on_screen(px):
			continue
		await move_to(px)
		await game._step(3)
		var cur: String = str(hud.get_state().get("cursor", ""))
		cursors[o.template] = cur
		if not (cur.to_lower().contains("garrison") or cur.to_lower().contains("enter")):
			continue
		var n: int = hud.get_message_log().size()
		await click(px, MOUSE_BUTTON_RIGHT)
		await game._step(6)
		var inside := false
		for i in 240:
			inside = int(world.get_object(unit.id).get("contained_by", 0)) != 0
			if inside:
				break
			await game._wait_seconds(0.5)
		print("PLAY1 tour garrison: %s into %s (cursor %s): messages %s, contained %s" % [unit.template, o.template, cur, JSON.stringify(messages_since(n)), inside])
		shot("tour-garrison")
		return "" if inside else "%s did not enter %s" % [unit.template, o.template]
	var any_own := false
	for o in own_objects():
		any_own = any_own or is_garrison_template(str(o.template))
	if not any_own:
		return "n/a: no garrisonable structure of this side or neutral near the base (cursors %s)" % JSON.stringify(cursors)
	if world.get_object(int(unit.id)).get("members", []).is_empty():
		# a single soldier, not a horde (Angmar's barracks trains only Thrall Masters at its first level): the garrisons take hordes
		return "n/a: %s is not a horde and no building takes it (cursors %s)" % [unit.template, JSON.stringify(cursors)]
	return "no structure near the base shows the garrison cursor for %s (cursors %s)" % [unit.template, JSON.stringify(cursors)]


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
	# a unit standing in front of a building (the click picks the building): a small drag box around it, which takes units only
	if not o.get("structure", false):
		var c := to_pixel(at.x, at.y)
		if on_screen(c):
			var lo := c - Vector2(28, 28)
			var hi := c + Vector2(28, 28)
			await move_to(lo)
			press(lo, MOUSE_BUTTON_LEFT, true)
			for t in 6:
				await move_to(lo.lerp(hi, float(t + 1) / 6.0))
			press(hi, MOUSE_BUTTON_LEFT, false)
			await game._step(8)
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
		var t := str(b.template).to_lower()
		if b.command == "DOZER_CONSTRUCT" and (t.contains("barracks") or t.contains("orcpit")) and build.is_empty():
			build = b # the faction's first infantry building (Men / Angmar: <Side>Barracks, Mordor: MordorOrcPit)
	if build.is_empty():
		return "the worker's command set has no barracks button: %s" % JSON.stringify(hud.get_command_buttons().map(func(x): return x.template))
	var bp := palantir_button(build.frame)
	print("PLAY1 skirmish: the %s button at %s" % [build.template, bp])
	await click(bp)
	await game._step(6)
	if not hud.get_placement().get("placing", false):
		return "the build button did not start the placement"
	var site := Vector2(-1, -1)
	var ghost: Node = game._placement_ghost
	var red_seen := false
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
			# the ghost (RW 0x69C5E6 / RW 0x6A2AE5): the structure under the cursor, red where the site is illegal
			if pl.get("placing", false) and int(pl.legal) != 0 and int(pl.legal) != 9 and ghost.shown_tint.r > 0.5 and not red_seen:
				red_seen = true
				shot("skirmish-ghost-illegal")
			if pl.get("placing", false) and int(pl.legal) == 0:
				site = px
				break
		if site.x >= 0:
			break
	print("PLAY1 skirmish: the placement ghost %s (tint %s), red on an illegal spot: %s, errors %s" % [ghost.shown_template, ghost.shown_tint, red_seen, ghost.errors])
	if ghost.shown_template != build.template:
		fails.append("the placement ghost shows '%s', not %s" % [ghost.shown_template, build.template])
	elif ghost.shown_tint.a > 0:
		fails.append("the ghost is tinted %s on a legal site" % ghost.shown_tint)
	shot("skirmish-ghost")
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
