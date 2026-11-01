## Lane PLAY-1: the owner's first hands-on skirmish, replayed by input events. Every action here is an InputEvent pushed into the viewport (mouse motion,
## buttons, the wheel, keys), the path a player's mouse takes: no QA harness command and no HUD shortcut. game.gd runs it after the start with
## --play1=<scenario>[,<scenario>...] (with --auto); each scenario prints "PLAY1 <scenario>: ok" or "PLAY1 <scenario>: FAIL <why>", screenshots go to --screens as
## play1-<name>.png, and "PLAY1 RESULT: <n> ok, <m> failed" ends the run.
##   orders    a left click selects one of the local units, a right click on the ground orders the move (MSG_DO_MOVETO), the move hint is drawn there,
##             the unit walks toward the point
##   palantir  the three buttons above the minimap (the Palantir's options row) are clicked one after the other; what each opened is reported
##   powers    the Palantir's powers button opens SpellStore.apt; a click on a power that can be bought, RESET, a buy and ACCEPT; a click beside the
##             store closes it
##   camera    the wheel zooms out to the limit: retail's max height times the Max zoom-out (lane ZOOM-1), beyond it with --free-camera
##   herobar   (lane UI-4) the Palantir's hero bar (InGameHeroSelect): the idle builder's button and, after a hero and a second hero are created, their buttons
##             with portrait, health and rank; a click on a hero button selects that hero, a second click moves the camera to it, a click on the builder button
##             selects the idle builder, select-all selects both heroes; play1-herobar-<step>.png
##   skirmish  a worker builds a barracks (the placement ghost is drawn, red on an illegal spot), the barracks trains, a drag box and a radar right click
##             send the soldiers to the enemy
##   tour      the rest of a player's first hour, each by input and judged by its effect: the minimap moves the view, the fortress recruits a hero
##             whose first ability is used, the barracks buys an upgrade, the spell store sells a power that the spell book casts, a worker builds a
##             wall line, the fortress builds an expansion, soldiers garrison a structure; one PLAY1 tour line per step, the broken ones in the FAIL
##   tribute   (with an ally: --opponents=2 --teams=1,1,2) after NumMinutesBeforePlayersCanTransferMoney the flag's PlayerTribute.apt, its tribute tab, a drag
##             on the ally's slider and Send move the local player's money to the ally (MSG_GIVE_MONEY)
##   ghosts    (lane PLAY-3) the builder is selected by a click; for every DOZER_CONSTRUCT button of its command set (every structure it builds freely) the
##             button is clicked, the pointer moves over the ground around the citadel: the placement ghost (the HUD's PlacementGhostRenderer, RotWK RW 0x69C5E6 /
##             RW 0x6A2AE5) shows that structure under the pointer; a right click ends the placement. Run per faction with --faction
##   buildperf (lane FOGFX-1, FB-0055) the main thread's CPU time of the frames that start a building placement (first and second selection of the
##             builder's first structures) against the idle frames before; see _scenario_buildperf
##   selperf   (lane SELPERF-1, the owner's "select a bunch of troops, fps drops to 25-12"; a tester: "only while they move") 150 hordes of the local
##             player in a block; for 1, 10, 50 and 150 selected, pairs of 1 s windows with nothing and with n selected (alternating which goes first,
##             --selperf-reps=5 pairs) in each phase: idle, pan (the camera circles), march (before every window Q selects the army and a right click
##             orders it on; the window is the frames the move hint is drawn in, 41 rendered frames; the camera following) and waypoints (the same with Alt held, RotWK's ORDERMODE_WAYPOINT: three
##             queued clicks; the logic's queue of them is stop S-281's). Per phase and n, medians of the pairs: the main thread's CPU time per frame
##             (FAIL above x1.10, --selperf-budget), the GPU's extra time (FAIL above 10% of the unselected frame) and, with --selperf-gate-wall=1 (a
##             quiet machine), the wall time per frame (FAIL above x1.10). A window counts only with n selected from its start to its end, the army moving
##             in it (marching), the hint drawn in 20 of its frames (a fresh order) and the CPU time present in 80% of its frames (else it is done again
##             with a new order, or the run fails). --selperf-gate-cpu=0 (Windows: thread time in 15.6 ms steps), --selperf-deadline=<s> (a FAIL, not a kill).
##             --selperf-at=x,y places the block, --selperf-phases=a,b / --selperf-counts=1,50 pick; play1-selperf-<phase>-150.png with --screens
##   dragbox   (lane PLAY-3) a left drag around the local units on the screen: while the button is held the drag box is drawn (RotWK's
##             W3DInGameUI::drawSelectionRegion RW 0x48ECF4: the region from the press to the pointer, 0xBBFFBB33, lines of width 2) and follows the pointer;
##             the release selects the own units inside and no structure, and the box is gone
##   selection (lane UI-4) two hordes and a hero drag-selected together: one merged jagged outline around the hordes' members and the hero's rune ring
##             (SelectionDecals.h, the owner's retail shot owner-shots/ui4/3); the hero alone; a hovered unit gets no marker; play1-selection-<step>.png
##   build5    (lane BUILD-5) RotWK's placement by input: the builder is selected, the barracks button clicked; the ghost on a legal site (no tint, no bib, no
##             pointer: the Build cursor has no Mouse.ini block), on an illegal one (red, the TBRedBib bib), anchored and turned by a left drag (W_Arrow);
##             a right drag held past DragToleranceMS scrolls the view during the placement with the directional Scroll cursor (frames
##             play1-build5-scroll-<n>.png) and the placement stays; a quick right click then cancels it (the screen edge does not scroll in a window: GlobalData Windowed, as retail).
##             The pointer is drawn into the shots from the HUD's own cursor image (a capture omits the OS pointer)
##   portraits (lane UI-4) a builder, a building, a hero and a horde of the local player, each selected with a left click: the Palantir's portrait
##             clip (CommandUI.Portrait) must draw the selection's SelectPortrait (PalantirCommandUI, RW 0x92FE33); play1-portrait-<kind>.png
extends "res://scripts/scenario_player.gd"

var game: Node
var hud: Node
var world: Node
var shell: Node
var tour_heroes: Array = []      # the heroes the tour recruited (the garrison step takes soldiers)


func _run(p_game: Node, scenarios: PackedStringArray) -> void:
	game = p_game
	hud = game._hud
	world = game._world
	shell = game._shell
	await _run_scenarios(scenarios)


# ---- input -----------------------------------------------------------------------------------------------------------------------------------------------

func _vp() -> Viewport:
	return game.get_viewport()


func move_to(p: Vector2) -> void:
	# the window's real pointer goes there too: on a desktop the system pointer's own motion events would otherwise land between the injected ones.
	# PLAY1_NO_WARP=1 (lane SPELLRC-1: a recording window on a workspace nobody shows): no warp, the injected events are the only pointer
	if OS.get_environment("PLAY1_NO_WARP") != "1":
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


## lane BUILD-5 r2: the placement ghost as the HUD draws it (InGameHudNode.get_placement_ghost_state)
func ghost_state() -> Dictionary:
	return hud.get_placement_ghost_state()


func ghost_icon0() -> Dictionary:
	var icons: Array = ghost_state().get("icons", [])
	return icons[0] if not icons.is_empty() else {}


## the first icon's tint (a 0: none), the template drawn ("" none: no icon has a drawn instance), the castle pieces drawn
func ghost_tint() -> Color:
	return ghost_icon0().get("tint", Color(0, 0, 0, 0))


func ghost_template() -> String:
	var gs: Dictionary = ghost_state()
	for ic in gs.get("icons", []):
		if int(ic.get("instances", 0)) > 0:
			return str(gs.get("template", ""))
	return ""


func ghost_pieces() -> int:
	var n := 0
	for ic in ghost_state().get("icons", []):
		if ic.get("piece", false) and int(ic.get("instances", 0)) > 0:
			n += 1
	return n


## lane BUILD-5: a screenshot with the pointer the HUD shows drawn at `p` (its hot spot there)
func shot_with_cursor(name: String, p: Vector2) -> void:
	var c: Dictionary = hud.get_cursor_image()
	var layer := CanvasLayer.new()
	layer.layer = 128
	var tr: TextureRect = null
	if c.get("texture") != null:
		tr = TextureRect.new()
		tr.texture = c.texture
		tr.position = p - Vector2(c.hot)
		layer.add_child(tr)
	game.add_child(layer)
	await game.get_tree().process_frame
	await game.get_tree().process_frame
	print("BUILD5 shot %s: cursor %s direction %d hidden %s" % [name, str(c.get("name", "")), int(c.get("direction", 0)), str(c.get("hidden", false))])
	shot(name)
	layer.queue_free()


func _scenario_build5() -> String:
	var fails: Array = []
	await _headless_window()
	var base := Vector2()
	for o in own_objects():
		if o.commandcenter:
			base = Vector2(o.x, o.y)
	# the builder and its barracks button
	var builder: Dictionary = {}
	var button: Dictionary = {}
	for o in own_objects():
		if o.structure:
			continue
		await look_by_radar(Vector2(o.x, o.y))
		if not await select_object(o):
			continue
		await game._step(4)
		var bs: Array = hud.get_command_buttons().filter(func(b): return b.command == "DOZER_CONSTRUCT" and not str(b.template).is_empty())
		for b in bs:
			if button.is_empty() or str(b.template).to_lower().contains("barracks"):
				button = b
		if not button.is_empty():
			builder = o
			break
	if builder.is_empty():
		return "no own unit offers a DOZER_CONSTRUCT button"
	var start_placing := func() -> bool:
		if not hud.get_selection().has(builder.id):
			await look_by_radar(Vector2(world.get_object(builder.id).x, world.get_object(builder.id).y))
			await select_object(builder)
		await look_by_radar(base + Vector2(250, -150))
		var bp := palantir_button(button.frame)
		if bp.x < 0:
			return false
		await click(bp)
		await game._step(6)
		return bool(hud.get_placement().get("placing", false))
	if not await start_placing.call():
		return "the %s button started no placement" % button.template
	print("BUILD5: builder %s places %s" % [builder.template, button.template])
	# 1. a legal site near the middle of the screen, and an illegal one: against the fortress (objects in the way), the view looking at it
	var legal_px := Vector2(-1, -1)
	var illegal_px := Vector2(-1, -1)
	var mid := window_size() * Vector2(0.6, 0.5)
	var offsets: Array = []
	for k in 30:
		offsets.append(Vector2((k % 6) - 2.5, int(k / 6) - 2.0) * Vector2(70, 45))
	offsets.sort_custom(func(a, b): return a.length() < b.length())
	for off in offsets:
		var px: Vector2 = mid + off
		await move_to(px)
		await game._step(4)
		if int(hud.get_placement().get("legal", -1)) == 0:
			legal_px = px
			break
	if legal_px.x < 0:
		return "no legal site near the middle of the screen"
	await move_to(legal_px)
	await game._step(6)
	await shot_with_cursor("build5-ghost-legal", legal_px)
	print("BUILD5 legal: tint %s bib '%s' cursor %s" % [ghost_tint(), str(ghost_icon0().get("bib", "")), str(hud.get_cursor_image().name)])
	if ghost_tint().a != 0.0 or str(ghost_icon0().get("bib", "")) != "":
		fails.append("the legal ghost is tinted or has a bib")
	# 2. anchored and turned: the left press, a drag of 70 pixels
	await move_to(legal_px)
	await game._step(4)
	press(legal_px, MOUSE_BUTTON_LEFT, true)
	await game._step(3)
	for t in 6:
		await move_to(legal_px + Vector2(-10 * (t + 1), 10 * (t + 1)))
	await game._step(4)
	var pl: Dictionary = hud.get_placement()
	await shot_with_cursor("build5-ghost-rotating", legal_px + Vector2(-60, 60))
	print("BUILD5 rotating: anchored %s angle %.3f arrow %s" % [str(pl.get("anchored", false)), float(pl.angle), str(bool(ghost_state().get("arrow", false)))])
	if not pl.get("anchored", false) or not bool(ghost_state().get("arrow", false)):
		fails.append("the anchored ghost shows no W_Arrow (placement %s)" % str(pl))
	press(legal_px + Vector2(-60, 60), MOUSE_BUTTON_LEFT, false)
	await game._step(6)
	if hud.get_placement().get("placing", false):
		fails.append("the click on the turned site did not place the building (messages %s)" % str(hud.get_report().get("messages", [])))
	# 3. an illegal site: against the fortress (objects in the way), with the view looking at it
	if not await start_placing.call():
		return "the placement for the illegal site did not start; " + "; ".join(fails)
	await look_by_radar(base + Vector2(0, -120))
	for k in 8:
		var w := base + Vector2(cos(TAU * k / 8.0), sin(TAU * k / 8.0)) * 110.0
		var px := to_pixel(w.x, w.y)
		if not on_screen(px):
			continue
		await move_to(px)
		await game._step(4)
		if int(hud.get_placement().get("legal", 0)) != 0:
			illegal_px = px
			break
	if illegal_px.x < 0:
		return "no illegal site beside the fortress; " + "; ".join(fails)
	await move_to(illegal_px)
	await game._step(6)
	await shot_with_cursor("build5-ghost-illegal", illegal_px)
	print("BUILD5 illegal: legal %d tint %s bib '%s'" % [int(hud.get_placement().legal), ghost_tint(), str(ghost_icon0().get("bib", ""))])
	if ghost_tint().a == 0.0 or str(ghost_icon0().get("bib", "")) != "TBRedBib.tga":
		fails.append("the illegal ghost is not red with the TBRedBib bib (tint %s, bib '%s')" % [ghost_tint(), str(ghost_icon0().get("bib", ""))])
	await click(window_size() * 0.5, MOUSE_BUTTON_RIGHT)
	await game._step(4)
	# 4. a right drag during a new placement: the view scrolls, the Scroll cursor faces the drag, the placement stays
	if not await start_placing.call():
		return "the second placement did not start; " + "; ".join(fails)
	var c0 := window_size() * 0.5
	await move_to(c0)
	await game._step(4)
	var cam0: Dictionary = hud.get_camera()
	var t0 := Time.get_ticks_msec()
	press(c0, MOUSE_BUTTON_RIGHT, true)
	await game._step(2)
	var dirs: Array = []
	for n in 12:
		var p := c0 + Vector2(10.0 * (n + 1), 4.0 * (n + 1))
		var m := InputEventMouseMotion.new()
		m.position = p
		m.global_position = p
		m.button_mask = MOUSE_BUTTON_MASK_RIGHT
		Input.warp_mouse(p)
		_vp().push_input(m)
		await game._step(2)
		dirs.append(int(hud.get_cursor_image().direction))
		if n % 3 == 2:
			await shot_with_cursor("build5-scroll-%02d" % n, p)
	await game._wait_seconds(0.3)
	var held := Time.get_ticks_msec() - t0
	var c_end := c0 + Vector2(120, 48)
	var cur_name := str(hud.get_cursor_image().name)
	press(c_end, MOUSE_BUTTON_RIGHT, false)
	await game._step(6)
	var cam1: Dictionary = hud.get_camera()
	var placing_after: bool = hud.get_placement().get("placing", false)
	print("BUILD5 scroll: held %d ms, cursor %s, directions %s, camera %s -> %s, still placing %s" % [held, cur_name, str(dirs), str(cam0.get("target")), str(cam1.get("target")), str(placing_after)])
	await shot_with_cursor("build5-scroll-after", c_end)
	if cur_name != "Scroll":
		fails.append("the cursor during the right drag was %s" % cur_name)
	if not placing_after:
		fails.append("the right drag ended the placement")
	if cam0.get("target") == cam1.get("target"):
		fails.append("the right drag did not scroll the view")
	# 5. a quick right click cancels the placement
	await click(c0, MOUSE_BUTTON_RIGHT)
	await game._step(4)
	if hud.get_placement().get("placing", false):
		fails.append("a right click did not cancel the placement")
	return "; ".join(fails)


## lane BUILD-5 r3: the retail capture of r2 replayed on the same terms: the camera stays where the game starts it (no radar click, no scroll), the window is the
## retail client's 16:9 (--res=1280x720 on the Deck: its screen is 1280x800), and the pointer goes to the very pixels the retail run used, scaled by
## window width / 1600, (/tmp/b5 scripts of r2): the legal site (340, 310), the turn
## from there through (360, 330), (390, 360), (420, 390) and back to (340, 395), the release; then a second barracks against the fortress at (1110, 570).
## Shots play1-match-*.png; crops at the retail references' rectangles are made outside (the retail frames are not committed)
func _scenario_build5match() -> String:
	var fails: Array = []
	# the retail client was 1600 x 900; the camera keeps its horizontal field of view (KEEP_WIDTH, RW 0x48B7B1's 50 degrees), so a retail pixel maps about
	# the screen centre scaled by the width ratio, whatever the window's aspect (the Deck's screen is 1280 x 800)
	var k := window_size().x / 1600.0
	var rp := func(p: Vector2) -> Vector2: return window_size() * 0.5 + (p - Vector2(800, 450)) * k
	await game._step(30)
	await game._wait_seconds(10.0) # the retail run clicked well into the game (the map's start scripts may hold the input)
	shot("match-start")
	var builder: Dictionary = {}
	var best := 1e9
	for o in own_objects():
		if not o.structure and str(o.template).to_lower().contains("porter"):
			var d := to_pixel(o.x, o.y).distance_to(rp.call(Vector2(1190, 735))) # the porter the retail run clicked
			if d < best:
				best = d
				builder = o
	if builder.is_empty():
		return "no porter"
	var press_barracks := func() -> bool:
		# the hero bar's builder button selects an idle builder without moving the view (InGameHeroSelect; the click on the drawn porter does not pick
		# it on this map: picking is not this lane's)
		var hb := _hero_button(0)
		if hb.x >= 0:
			await click(hb)
			await game._step(6)
		if hud.get_selection().is_empty() and not await select_object(builder):
			# on Harad no click selects a unit in OpenBFME (a picking defect outside this lane, reported): the HUD's test hook selects the porter; the
			# placement that is compared stays driven by pointer input
			hud.select_object(int(builder.id))
			await game._step(6)
			print("BUILD5 match: the porter was selected by the HUD hook (selection %s)" % str(hud.get_selection()))
			if hud.get_selection().is_empty():
				return false
		await game._step(4)
		var b: Dictionary = {}
		for x in hud.get_command_buttons():
			if x.command == "DOZER_CONSTRUCT" and str(x.template) == "GondorBarracks":
				b = x
		if b.is_empty():
			print("BUILD5 match: no barracks button among %s" % str(hud.get_command_buttons().map(func(x): return [x.command, x.template])))
			return false
		var bp := palantir_button(b.frame)
		if bp.x < 0:
			print("BUILD5 match: the barracks button %s is not on the screen" % b.frame)
			return false
		await click(bp)
		await game._step(4)
		if not hud.get_placement().get("placing", false):
			hud.press_command_button("GondorBarracks") # the same hook path for a button click the map's input does not take
			print("BUILD5 match: the barracks button was pressed by the HUD hook")
		await game._step(6)
		return bool(hud.get_placement().get("placing", false))
	if not await press_barracks.call():
		return "the barracks placement did not start"
	await move_to(rp.call(Vector2(330, 300)))
	await move_to(rp.call(Vector2(340, 310)))
	await game._step(12)
	shot("match-legal")
	press(rp.call(Vector2(340, 310)), MOUSE_BUTTON_LEFT, true)
	await game._step(4)
	for p in [rp.call(Vector2(360, 330)), rp.call(Vector2(390, 360)), rp.call(Vector2(420, 390))]:
		await move_to(p)
	await game._step(12)
	shot("match-rotating")
	await move_to(rp.call(Vector2(340, 395)))
	await game._step(12)
	shot("match-rotating2")
	press(rp.call(Vector2(340, 395)), MOUSE_BUTTON_LEFT, false)
	await game._step(8)
	if hud.get_placement().get("placing", false):
		fails.append("the turned barracks was not placed")
	if not await press_barracks.call():
		return "the second placement did not start; " + "; ".join(fails)
	await move_to(rp.call(Vector2(1100, 560)))
	await move_to(rp.call(Vector2(1110, 570)))
	await game._step(12)
	shot("match-illegal")
	var gs: Dictionary = ghost_state()
	print("BUILD5 match: illegal ghost %s, errors %s" % [str(gs.get("icons", [])), str(gs.get("errors", []))])
	await click(rp.call(Vector2(1110, 570)), MOUSE_BUTTON_RIGHT)
	return "; ".join(fails)


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
			if pl.get("has_ghost", false) and ghost_template() == b.template:
				seen = ghost_template()
				if int(pl.legal) == 0:
					legal_seen = legal_seen or ghost_tint().a == 0.0
				else:
					illegal_seen = illegal_seen or ghost_tint().a > 0.0
		print("PLAY3 ghosts: %s %s: ghost %s, legal (untinted) %s, illegal (tinted) %s" % [game._faction, b.template, seen if seen != "" else "NONE", legal_seen, illegal_seen])
		if seen == "":
			fails.append("%s: no ghost under the pointer (ghost node '%s', errors %s, placement %s)" % [b.template, ghost_template(), str(ghost_state().get("errors", [])), str(pl)])
		else:
			shown += 1
			if shown == 1:
				shot("ghost-" + game._faction.to_lower())
			if ghost_pieces() > 0:
				shot("ghost-castle-" + game._faction.to_lower())
				print("PLAY3 ghosts: %s %s: a castle, %d layout pieces drawn" % [game._faction, b.template, ghost_pieces()])
		await click(window_size() * 0.5, MOUSE_BUTTON_RIGHT)
		await game._step(4)
		if hud.get_placement().get("placing", false):
			fails.append("%s: the right click did not end the placement" % b.template)
			await click(window_size() * 0.5, MOUSE_BUTTON_RIGHT)
	print("PLAY3 ghosts: %s: %d of %d structures showed their ghost" % [game._faction, shown, buttons.size()])
	return "" if fails.is_empty() else "; ".join(fails)


## lane FBSIM-1 (FB-0039): the builder's site is RotWK's phantom until the builder arrives (drawn at 0.4 for its owner, RW 0x88D272), then a
## construction site (RW 0x88D44F): a farm placed a few hundred away, screenshots while the builder walks and after it arrived
func _scenario_phantom() -> String:
	await _headless_window()
	var base := Vector2()
	for o in own_objects():
		if o.commandcenter:
			base = Vector2(o.x, o.y)
	var builder: Dictionary = {}
	var button: Dictionary = {}
	for o in own_objects():
		if o.structure:
			continue
		await look_by_radar(Vector2(o.x, o.y))
		if not await select_object(o):
			continue
		await game._step(4)
		for b in hud.get_command_buttons():
			if b.command == "DOZER_CONSTRUCT" and str(b.template).contains("Farm"):
				button = b
		if not button.is_empty():
			builder = o
			break
	if builder.is_empty():
		return "no own builder offers a farm"
	await look_by_radar(base)
	await click(palantir_button(button.frame))
	await game._step(6)
	var placed := false
	for k in 16:
		var a := TAU * float(k) / 16.0 + 0.3
		var p := base + Vector2(cos(a), sin(a)) * 330.0
		var px := to_pixel(p.x, p.y)
		if not on_screen(px):
			continue
		await move_to(px)
		await game._step(3)
		var pl: Dictionary = hud.get_placement()
		if pl.get("has_ghost", false) and int(pl.legal) == 0:
			await click(px)
			placed = true
			print("PLAY1 phantom: %s ordered at (%.0f, %.0f)" % [button.template, p.x, p.y])
			break
	if not placed:
		return "no legal farm site on the screen"
	await game._step(2)
	# a placed order already ends the placement mode; a right click on the ground then would be a move order that cancels the phantom (Sol review of FBSIM-1)
	if hud.get_placement().get("has_ghost", false):
		await click(window_size() * 0.5, MOUSE_BUTTON_RIGHT) # leave the placement mode
	await game._step(4)
	for i in 14:
		await game._step(10)
		shot("phantom-%02d" % i)
	return ""


## lane FOGFX-1 (FB-0055, "a brief 30-40% FPS drop when selecting a building for the builder to construct"): the frames around starting a placement.
## The builder is selected; for each of its first --buildperf-count DOZER_CONSTRUCT buttons (default 4): an idle window (30 frames), the button
## clicked and the pointer moved over the ground beside the citadel (20 frames: the frames that make the ghost), a right click ends it, and the same
## button again (the second selection). Per frame the main thread's CPU time (GameWorld main_cpu_ms, not inflated by other processes) and the wall time.
## BUILDPERF lines per structure and selection: the idle median, the placement's median and worst frame, the parts, the ghost renderer's counters.
## FAIL (the pins): a selection's worst frame more than --buildperf-budget ms of CPU over the idle median (default 50); its median frame above
## --buildperf-ratio x the idle one (default 1.75; before the fix a farm read x2.0 - 2.1); the Palantir part above --buildperf-palantir ms a frame (default
## 2.5; the farm's claim feedback read 6.3 before); a second selection that makes instancers or loads models again; more than 2 bib materials.
func _scenario_buildperf() -> String:
	var fails: Array = []
	await _headless_window()
	var budget := float(_selperf_arg("buildperf-budget", "50"))
	var count := int(_selperf_arg("buildperf-count", "4"))
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
		return "no own unit offers a DOZER_CONSTRUCT button"
	var worst_all := 0.0
	var ratio_budget := float(_selperf_arg("buildperf-ratio", "1.75"))
	var palantir_budget := float(_selperf_arg("buildperf-palantir", "2.5"))
	for b in buttons.slice(0, count):
		var made_first := {}
		for pass_name in ["first", "again"]:
			await look_by_radar(base)
			if not hud.get_selection().has(builder.id):
				await look_by_radar(Vector2(world.get_object(builder.id).x, world.get_object(builder.id).y))
				await select_object(builder)
				await look_by_radar(base)
			# the same pointer circle without a placement (the builder selected), then with it
			var idle: Dictionary = await _buildperf_window(base, 30, "")
			var bp := palantir_button(b.frame)
			if bp.x < 0:
				fails.append("%s: its button %s is not on the screen" % [b.template, b.frame])
				break
			await click(bp)
			var placing: Dictionary = await _buildperf_window(base, 30, b.template)
			var idle_med := float(idle.cpu_p50)
			var worst := float(placing.cpu_max)
			worst_all = maxf(worst_all, worst - idle_med)
			print("BUILDPERF %s %s: ghost %s, pointer moving: idle cpu p50 %.2f ms, placing cpu p50 %.2f ms (x%.2f), worst %.2f ms (+%.2f) wall %.2f ms" % [b.template,
				pass_name, placing.ghost, idle_med, float(placing.cpu_p50), float(placing.cpu_p50) / maxf(idle_med, 0.01), worst, worst - idle_med, float(placing.wall_max)])
			print("BUILDPERF %s %s: mean parts idle %s" % [b.template, pass_name, JSON.stringify(idle.parts)])
			print("BUILDPERF %s %s: mean parts placing %s" % [b.template, pass_name, JSON.stringify(placing.parts)])
			print("BUILDPERF %s %s: cpu per frame %s" % [b.template, pass_name, str(placing.cpu.map(func(x): return snappedf(x, 0.1)))])
			var gs: Dictionary = ghost_state()
			print("BUILDPERF %s %s: ghost renderer: %d instancers made, %d models built, %d templates parked, %d bib materials" % [b.template, pass_name,
				int(gs.get("instancers_made", -1)), int(gs.get("models_built", -1)), int(gs.get("parked", -1)), int(gs.get("bib_materials", -1))])
			# the pins (lane FOGFX-1): a structure placed again takes its kept instancers (no model loads again); the bib materials are shared (one per
			# texture); the placement's median frame against the idle one; the farm's claim feedback (Palantir part) stays cheap
			if not (gs.has("instancers_made") and gs.has("models_built") and gs.has("bib_materials")):
				fails.append("%s %s: the ghost renderer reports no instancer / model / bib material counters" % [b.template, pass_name])
			elif pass_name == "first":
				made_first = {"instancers": int(gs.get("instancers_made", -1)), "models": int(gs.get("models_built", -1))}
			elif int(gs.get("instancers_made", -1)) != made_first.instancers or int(gs.get("models_built", -1)) != made_first.models:
				fails.append("%s again: the ghost made instancers / models again (%s -> %d / %d)" % [b.template, str(made_first), int(gs.get("instancers_made", -1)), int(gs.get("models_built", -1))])
			if int(gs.get("bib_materials", -1)) > 2:
				fails.append("%s %s: %d bib materials (one per texture expected)" % [b.template, pass_name, int(gs.get("bib_materials", -1))])
			var ratio := float(placing.cpu_p50) / maxf(idle_med, 0.01)
			if ratio > ratio_budget:
				fails.append("%s %s: the placement's median frame is x%.2f the idle one (budget x%.2f)" % [b.template, pass_name, ratio, ratio_budget])
			var palantir := float(placing.parts.get("hud_palantir_ms", 0.0))
			if palantir > palantir_budget:
				fails.append("%s %s: the Palantir part costs %.2f ms a frame while placing (budget %.1f)" % [b.template, pass_name, palantir, palantir_budget])
			if not placing.ghost:
				fails.append("%s %s: no ghost under the pointer" % [b.template, pass_name])
			if worst - idle_med > budget:
				fails.append("%s %s: a placement frame cost %.1f ms of CPU over the idle median (budget %.0f)" % [b.template, pass_name, worst - idle_med, budget])
			await click(window_size() * 0.5, MOUSE_BUTTON_RIGHT)
			await game._step(4)
			if hud.get_placement().get("placing", false):
				await click(window_size() * 0.5, MOUSE_BUTTON_RIGHT)
				await game._step(4)
	print("BUILDPERF worst excess %.2f ms (budget %.0f)" % [worst_all, budget])
	return "" if fails.is_empty() else "; ".join(fails)


## buildperf's window: `frames` render frames with the pointer on a circle of 110 px around `base` on the screen, one step a frame; per frame the main thread's CPU
## time and the C++ timing parts (their means over the window); `template`: the ghost expected under the pointer ("" none)
func _buildperf_window(base: Vector2, frames: int, template: String) -> Dictionary:
	var cpu: Array = []
	var wall := 0.0
	var sums := {}
	var per_frame: Array = []
	var ghost := false
	var prev := Time.get_ticks_usec()
	for k in frames:
		# a circle of 110 px around the screen point of `base` (the camera looks at it): always over the ground, never over the Palantir
		var a := TAU * float(k) / float(frames)
		var px := to_pixel(base.x, base.y) + Vector2(cos(a), sin(a)) * 110.0
		if on_screen(px):
			# as move_to, without its two-frame wait (every frame here is measured)
			if OS.get_environment("PLAY1_NO_WARP") != "1":
				Input.warp_mouse(px)
			var mm := InputEventMouseMotion.new()
			mm.position = px
			mm.global_position = px
			_vp().push_input(mm)
		await game.get_tree().process_frame
		var now := Time.get_ticks_usec()
		wall = maxf(wall, (now - prev) / 1000.0)
		prev = now
		var w: Dictionary = world.get_frame_timings()
		cpu.append(float(w.get("main_cpu_ms", 0.0)))
		var fp := {}
		for src in [w, hud.get_frame_timings()]:
			for key in src.keys():
				if str(key).ends_with("_ms") and not str(key).begins_with("logic_wait") and not str(key).begins_with("worker"):
					sums[key] = float(sums.get(key, 0.0)) + float(src[key])
					if float(src[key]) >= 0.4:
						fp[key] = snappedf(float(src[key]), 0.1)
		per_frame.append(fp)
		if not template.is_empty():
			ghost = ghost or (hud.get_placement().get("has_ghost", false) and ghost_template() == template)
	if _selperf_arg("buildperf-frames", "0") == "1" and not template.is_empty():
		for k in per_frame.size():
			print("BUILDPERF frame %d: %s" % [k, JSON.stringify(per_frame[k])])
	var parts := {}
	for key in sums.keys():
		if float(sums[key]) / frames >= 0.1:
			parts[key] = snappedf(float(sums[key]) / frames, 0.01)
	var v := cpu.duplicate()
	v.sort()
	return {"cpu": cpu, "cpu_p50": float(v[int(0.5 * (v.size() - 1))]), "cpu_max": float(v.back()), "wall_max": wall, "parts": parts, "ghost": ghost}


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


func _hero_bar() -> Dictionary:
	return hud.get_state().get("hero_select", {})


func _hero_button(index: int) -> Vector2:
	var hs := _hero_bar()
	return palantir_button(String(hs.get("name", "HeroSelectUI")) + ".Hero%d" % index)


func _scenario_herobar() -> String:
	if game._local_index < 0:
		return "no local player index"
	await game._step(60)
	var hs := _hero_bar()
	print("PLAY1 herobar: at the start ", JSON.stringify({"attached": hs.get("attached"), "name": hs.get("name"), "shown": hs.get("shown"),
		"builders": hs.get("builders"), "heroes": hs.get("heroes"), "slot1": hs.get("slots", [{}])[0]}))
	if not hs.get("attached", false):
		return "the hero bar's movie never loaded (AptPalantir::OnHeroSelectLoaded)"
	if not hs.get("shown", false) or not hs.slots[0].builder:
		return "the idle builder has no button at the start: " + str(hs.get("slots", [{}])[0])
	shot("herobar-builder")
	var spot := Vector2()
	for o in world.get_player_objects(game._local_name):
		if not o.structure:
			spot = Vector2(o.x, o.y)
			break
	var hero1 := _create_first(["GondorBoromir", "GondorFaramir", "RohanTheoden", "MordorWitchKing", "IsengardSaruman"], spot + Vector2(300, -150))
	var hero2 := _create_first(["GondorFaramir", "RohanEowyn", "MordorMouthOfSauron", "IsengardLurtz"], spot + Vector2(-300, -150))
	if hero1.is_empty() or hero2.is_empty():
		return "could not create two heroes"
	await game._step(60)
	hs = _hero_bar()
	var s2: Dictionary = hs.slots[1]
	var s3: Dictionary = hs.slots[2]
	print("PLAY1 herobar: after two heroes ", JSON.stringify({"heroes": hs.heroes, "slot2": s2, "slot3": s3, "select_all": hs.select_all}))
	if s2.hero != hero1.id and s2.hero != hero2.id:
		return "slot 2 shows no created hero: " + str(s2)
	if String(s2.image).is_empty() or s2.health < 1:
		return "slot 2 has no portrait or health: " + str(s2)
	if not hs.select_all:
		return "the select-all button did not show"
	shot("herobar-heroes")
	var b2 := _hero_button(2)
	if b2.x < 0:
		return "no button HeroSelectUI.Hero2 on the screen"
	var cam0: Dictionary = hud.get_camera()
	await click(b2)
	await game._step(20)
	var sel: Array = hud.get_selection()
	print("PLAY1 herobar: a click on Hero2 at %s selected %s (slot hero %d)" % [b2, sel, s2.hero])
	if sel != [s2.hero]:
		return "the click on Hero2 selected %s, not hero %d" % [sel, s2.hero]
	shot("herobar-hero-selected")
	hud.camera_look_at(spot + Vector2(-2000, -2000))
	await game._step(20)
	await click(b2)
	await game._step(20)
	var cam1: Dictionary = hud.get_camera()
	var target: Vector3 = cam1.target
	var hero_pos := Vector2()
	for o in world.get_player_objects(game._local_name):
		if o.id == s2.hero:
			hero_pos = Vector2(o.x, o.y)
	var dist := Vector2(target.x, -target.z).distance_to(hero_pos)
	print("PLAY1 herobar: the second click moved the camera to %s, the hero is at %s (%.0f away)" % [target, hero_pos, dist])
	if dist > 150.0:
		return "the second click did not move the camera to the hero (%.0f away)" % dist
	shot("herobar-camera")
	await click(_hero_button(1))
	await game._step(20)
	sel = hud.get_selection()
	print("PLAY1 herobar: a click on Hero1 (the builder) selected ", sel)
	if sel.size() != 1 or not hs.builders.has(sel[0]):
		return "the builder button selected %s, not one of the builders %s" % [sel, hs.builders]
	var all_btn := palantir_button(String(hs.name) + ".SelectAllHeroesBttn")
	if all_btn.x >= 0:
		await click(all_btn)
		await game._step(20)
		sel = hud.get_selection()
		print("PLAY1 herobar: select all heroes selected ", sel)
		if not (sel.has(hero1.id) and sel.has(hero2.id)):
			return "select all heroes selected %s" % [sel]
		shot("herobar-select-all")
	else:
		return "no SelectAllHeroesBttn on the screen"
	return ""


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
		print("PLAY1 selection markers: %s: %s (%d drawn)" % [c.kind, JSON.stringify(st.get("selection_decals", [])), st.get("selection_decals_drawn", 0)])
		print("PLAY1 selection merge: %s: %s" % [c.kind, JSON.stringify(st.get("selection_merge", {}))])
		shot("portrait-" + c.kind)
		var others: Array = cases.filter(func(o): return o.kind != c.kind).map(func(o): return o.id)
		if sel.is_empty() or sel.any(func(i): return others.has(i)):
			failures.append("%s: the click selected %s" % [c.kind, str(sel)])
		elif String(st.get("portrait", "")).is_empty():
			failures.append("%s: no portrait for the selection" % c.kind)
		elif st.get("portrait_drawn", "") != st.get("portrait", ""):
			failures.append("%s: the portrait %s was not drawn (drew '%s')" % [c.kind, st.get("portrait"), st.get("portrait_drawn")])
	return "; ".join(failures)


# lane UI-4: the selection markers as the owner's retail shot shows them (owner-shots/ui4/3-retail-selection-indicators.png): two hordes and a hero selected
# together by a drag box (one merged outline around both hordes' members, the hero's rune ring), then the hero alone, then a unit only hovered (no marker)
func _scenario_selection() -> String:
	if game._local_index < 0:
		return "no local player index"
	var spot := Vector2()
	for o in world.get_player_objects(game._local_name):
		if not o.structure:
			spot = Vector2(o.x, o.y)
			break
	var centre := spot + Vector2(-60, 260)
	var horde1 := _create_first(["GondorFighterHorde", "RohanPeasantHorde", "MordorOrcFighterHorde"], centre + Vector2(-70, 0))
	var horde2 := _create_first(["GondorArcherHorde", "GondorFighterHorde", "RohanPeasantHorde"], centre + Vector2(40, -30))
	var hero := _create_first(["GondorBoromir", "GondorFaramir", "RohanTheoden", "MordorWitchKing"], centre + Vector2(170, 20))
	if horde1.is_empty() or horde2.is_empty() or hero.is_empty():
		return "could not create the hordes (%s, %s) or the hero (%s)" % [str(horde1), str(horde2), str(hero)]
	await game._step(60)
	hud.camera_look_at(centre + Vector2(40, 0))
	await game._step(20)
	await wheel(to_pixel(centre.x + 40, centre.y), 6)
	await game._step(20)
	# the drag box around all three
	var ids := [horde1.id, horde2.id, hero.id]
	var lo := Vector2(1e9, 1e9)
	var hi := Vector2(-1e9, -1e9)
	for o in world.get_player_objects(game._local_name):
		if o.structure:
			continue
		var p := Vector2(o.x, o.y)
		if p.distance_to(centre) < 320.0:
			var px := to_pixel(o.x, o.y)
			lo = Vector2(min(lo.x, px.x), min(lo.y, px.y))
			hi = Vector2(max(hi.x, px.x), max(hi.y, px.y))
	lo -= Vector2(30, 40)
	hi += Vector2(30, 30)
	await move_to(lo)
	press(lo, MOUSE_BUTTON_LEFT, true)
	for i in 8:
		await move_to(lo.lerp(hi, (i + 1) / 8.0))
	press(hi, MOUSE_BUTTON_LEFT, false)
	await game._step(30)
	var sel: Array = hud.get_selection()
	var st: Dictionary = hud.get_state()
	print("PLAY1 selection: box selected %s (wanted %s)" % [str(sel), str(ids)])
	print("PLAY1 selection merge: all: %s" % JSON.stringify(st.get("selection_merge", {})))
	shot("selection-all")
	for i in ids:
		if not sel.has(i):
			return "the box did not select %s: %s" % [str(i), str(sel)]
	var merge: Dictionary = st.get("selection_merge", {})
	if int(merge.get("decals", 0)) < 2 or int(merge.get("opaque_texels", 0)) == 0 or bool(merge.get("simple", true)):
		return "no merged outline: %s" % JSON.stringify(merge)
	# the hero alone: the rune ring, no outline
	var hp := Vector2()
	for o in world.get_player_objects(game._local_name):
		if o.id == hero.id:
			hp = Vector2(o.x, o.y)
	await click(to_pixel(hp.x, hp.y))
	await game._step(30)
	st = hud.get_state()
	print("PLAY1 selection: hero alone %s: %s" % [str(hud.get_selection()), JSON.stringify(st.get("selection_decals", []))])
	shot("selection-hero")
	# hover a horde member without clicking: retail draws no marker for a hovered unit
	var mp := Vector2()
	for o in world.get_player_objects(game._local_name):
		if not o.structure and o.id != hero.id and Vector2(o.x, o.y).distance_to(centre) < 150.0 and o.id != horde1.id and o.id != horde2.id:
			mp = Vector2(o.x, o.y)
			break
	await move_to(to_pixel(mp.x, mp.y))
	await game._step(20)
	st = hud.get_state()
	var hov: Array = st.get("selection_decals", [])
	print("PLAY1 selection: hovering a horde member, the markers are %s" % JSON.stringify(hov))
	shot("selection-hover")
	for e in hov:
		if int(e.get("object", 0)) != hero.id:
			return "a hovered unit got a marker: %s" % JSON.stringify(hov)
	return ""


# ---- lane SELPERF-1: the frame time of a large selection ----------------------------------------------------------------------------------------------

const SELPERF_COUNTS := [1, 10, 50, 150]
const SELPERF_HORDES := ["GondorFighterHorde", "RohanPeasantHorde", "MordorOrcFighterHorde", "AngmarRhudaurAxeThrowerHorde", "IsengardUrukHaiHorde"]
const SELPERF_PHASES := ["idle", "pan", "march", "waypoints"]

var _selperf_ids := PackedInt64Array()
var _selperf_centre := Vector2()
var _selperf_target := Vector2()
var _selperf_dir := 1.0 # the march's direction along the map's x
var _selperf_stalled := false # the army stopped: the next march window orders it on
var _selperf_bad_selection := 0 # windows in a row whose selection changed
var _selperf_deadline := 0     # --selperf-deadline=<s>: the msec the run stops by (0: none)
var _selperf_gate_cpu := true  # --selperf-gate-cpu=0: CPU time not judged (Windows: its thread time steps by 15.6 ms)
var _selperf_orders := 0


func _selperf_arg(name: String, default: String) -> String:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--%s=" % name):
			return a.substr(name.length() + 3)
	return default


## the army's middle (three hordes of the block: the camera follows it while it marches)
func _selperf_army() -> Vector2:
	var sum := Vector2()
	var k := 0
	for i in [0, _selperf_ids.size() / 2, _selperf_ids.size() - 1]:
		var o: Dictionary = world.get_object(_selperf_ids[i])
		if not o.is_empty():
			sum += Vector2(float(o.get("x", 0.0)), float(o.get("y", 0.0)))
			k += 1
	return sum / maxf(k, 1)


func _key_event(code: Key, down: bool, alt: bool) -> InputEventKey:
	var e := InputEventKey.new()
	e.keycode = code
	e.physical_keycode = code
	e.pressed = down
	e.alt_pressed = alt
	return e


## a player's order to the whole army: Q (SELECT_ALL, RotWK's CommandMap: the logic's group of every unit), then a right click on empty ground at the
## target (a move order and its hint), or with Alt held (ORDERMODE_WAYPOINT, RW 0x820249: MSG_CHANGE_ORDERMODE, the clicks queued orders) three clicks in
## a row; the army walks
## back and forth along the map's x
func _selperf_order(waypoints: bool) -> String:
	_selperf_orders += 1
	Input.parse_input_event(_key_event(KEY_Q, true, false))
	await game._step(1)
	Input.parse_input_event(_key_event(KEY_Q, false, false))
	await game._step(2)
	# the targets: the far end of the march's leg along the map's x, --selperf-band=1300 either side of the block's place (Evendim: the open grass strip along the
	# northern edge, the citadel to the south; a target just ahead of an 825-wide army stops its front row and jams the rest), empty ground (no horde
	# within 70: a right click on an own unit selects it); the army turns round when its leading edge is within 350 of the end. Some ground takes no
	# order (an obstacle, impassable ground: no move message): the next candidate is tried
	var mid := _selperf_middle()
	var lead := -1.0e9
	for id in _selperf_ids:
		var o: Dictionary = world.get_object(id)
		lead = maxf(lead, _selperf_dir * float(o.get("x", 0.0)))
	if _selperf_dir * _selperf_centre.x + float(_selperf_arg("selperf-band", "1300")) - lead < 350.0:
		_selperf_dir = -_selperf_dir
	var cands: Array = []
	var band := float(_selperf_arg("selperf-band", "1300"))
	for reach in [band, band - 200.0, band - 400.0]:
		for dy in [0.0, -120.0, 120.0, -240.0]:
			cands.append([Vector2(_selperf_centre.x + _selperf_dir * reach, _selperf_centre.y + dy), _selperf_dir])
	var tried := 0
	for c in cands:
		var cand: Vector2 = c[0]
		var clear := true
		for id in _selperf_ids:
			var o: Dictionary = world.get_object(id)
			if Vector2(float(o.get("x", 0.0)), float(o.get("y", 0.0))).distance_to(cand) < 70.0:
				clear = false
				break
		if not clear:
			continue
		hud.camera_look_at(cand)
		await game._step(2)
		var px := to_pixel(cand.x, cand.y).floor()
		if not on_screen(px):
			continue
		for attempt in 2:
			tried += 1
			if attempt > 0:
				# just after a logic frame finished (a long frame between the press and the release makes a click a hold: Mouse.ini DragToleranceMS 150)
				var f := int(world.get_frame())
				var waited := 0
				while int(world.get_frame()) == f and waited < 300:
					await game.get_tree().process_frame
					waited += 1
			var before := log_count("MSG_DO_MOVETO") + log_count("MSG_ADD_WAYPOINT")
			# a pointer that moved since the last click (each order clicks a few pixels from the last)
			var j := Vector2(float((_selperf_orders * 7 + tried * 13) % 31) - 15.0, float((_selperf_orders * 5 + tried * 11) % 21) - 10.0)
			if waypoints:
				Input.parse_input_event(_key_event(KEY_ALT, true, true))
				await game._step(2)
				for off in [Vector2(-90, 0), Vector2(0, 40), Vector2(90, 0)]:
					await move_to(px + off + j)
					await _selperf_click(px + off + j)
				Input.parse_input_event(_key_event(KEY_ALT, false, false))
				await game._step(1)
			else:
				await move_to(px + j + Vector2(40, 40))
				await move_to(px + j)
				await _selperf_click(px + j)
			# the input waits while the logic worker computes a frame: up to 3 s for the message
			for t in 90:
				if waypoints and log_count("MSG_CHANGE_ORDERMODE i1 i0") == 0 and log_count("MSG_DO_MOVETO") > before:
					return "Alt held gave no queued order mode (MSG_CHANGE_ORDERMODE 1, 0: RotWK's ORDERMODE_WAYPOINT, RW 0x820249)"
				if log_count("MSG_DO_MOVETO") + log_count("MSG_ADD_WAYPOINT") > before:
					_selperf_target = cand
					_selperf_dir = float(c[1])
					return ""
				await game._step(2)
	return "no %s order was taken at any of %d places ahead of the army (%d clicks; the army's middle %s)" % ["waypoint" if waypoints else "move", cands.size(),
		tried, mid]


## a right click: the press, one rendered frame, the release (the HUD sees the press in a frame of its own). A marching army's long frame between the two
## makes it a hold (Mouse.ini DragToleranceMS 150): the order is clicked just after a logic frame finished, and tried again when no order came
func _selperf_click(p: Vector2) -> void:
	press(p, MOUSE_BUTTON_RIGHT, true)
	await game.get_tree().process_frame
	press(p, MOUSE_BUTTON_RIGHT, false)
	await game._step(3)


## one window of render frames: the wall time of each, the GPU's time (0 headless) and the C++ parts the HUD and the world report; `camera`: "", "pan"
## (a circle around the block) or "follow" (over the marching army)
## hint_only: the window is the frames in which the order's move hint is drawn (InGameUI: younger than 41 client frames, one client frame per HUD
## update, i.e. per rendered frame: RW 0x48EDFF): it ends when the hint dies; fewer than 20 such frames is an error
func _selperf_window(phase: String, n: int, seconds: float, camera: String, hint_only := false) -> Dictionary:
	var times: Array = []
	var live_frames := 0
	var cpu_valid := 0
	var gpu: Array = []
	var cpu: Array = []
	var hud_times: Array = []
	var parts := {}
	var vp_rid: RID = _vp().get_viewport_rid()
	RenderingServer.viewport_set_measure_render_time(vp_rid, true)
	var start := _selperf_middle()
	var selected_start: int = hud.get_selection().size()
	if shell != null and shell.has_method("reset_timing"):
		shell.reset_timing() # the Palantir movie's display list rebuilds (AptMenuPlayer.get_stats)
	var t0 := Time.get_ticks_usec()
	var prev := t0
	var i := 0
	while (hint_only and times.size() < 600) or (not hint_only and (Time.get_ticks_usec() - t0 < int(seconds * 1.0e6) or times.size() < 8)):
		if camera == "pan":
			hud.camera_look_at(_selperf_centre + Vector2(cos(i * 0.02), sin(i * 0.02)) * 220.0)
		elif camera == "follow":
			hud.camera_look_at(_selperf_army())
		await game.get_tree().process_frame
		var now := Time.get_ticks_usec()
		times.append((now - prev) / 1000.0)
		prev = now
		i += 1
		var h: Dictionary = hud.get_frame_timings()
		var w: Dictionary = world.get_frame_timings()
		if hint_only:
			var live := false
			for e in hud.get_move_hints().get("hints", []):
				live = live or int(e.get("age", 99)) < 41
			if not live:
				times.pop_back() # the frame after the hint's last: not the hint's
				break
			live_frames += 1
		if float(w.get("main_cpu_ms", 0.0)) > 0.0:
			cpu_valid += 1
		hud_times.append(float(h.get("hud_update_ms", 0.0)))
		gpu.append(RenderingServer.viewport_get_measured_render_time_gpu(vp_rid))
		cpu.append(float(w.get("main_cpu_ms", 0.0)))
		for k in ["hud_update_ms", "hud_palantir_ms", "hud_seldecals_ms", "hud_decals_ms", "hud_iconui_ms"]:
			parts[k] = parts.get(k, 0.0) + float(h.get(k, 0.0))
		for k in ["logic_ms", "sync_ms", "main_cpu_ms"]:
			parts[k] = parts.get(k, 0.0) + float(w.get(k, 0.0))
	var med := func(a: Array) -> float:
		var v := a.duplicate()
		v.sort()
		return float(v[int(0.5 * (v.size() - 1))])
	var m := times.size()
	var apt_ms := 0.0
	var apt_rebuilds := 0
	if shell != null and shell.has_method("get_stats"):
		var st: Dictionary = shell.get_stats()
		apt_rebuilds = int(st.get("rebuilds", 0))
		apt_ms = (float(st.get("list_us", 0.0)) + float(st.get("canvas_us", 0.0)) + float(st.get("submit_us", 0.0))) * apt_rebuilds / 1000.0 / maxf(m, 1) \
			+ float(st.get("steps_us", 0.0)) / 1000.0
		parts["apt_ms"] = apt_ms * m
		parts["apt_rebuilds"] = float(apt_rebuilds)
		parts["apt_ops"] = float(st.get("last_ops", 0)) * m
		for k in ["steps_us", "list_us", "canvas_us", "submit_us"]:
			parts["apt_" + k] = float(st.get(k, 0.0)) * m
		parts["apt_unchanged"] = float(st.get("unchanged_frames", 0))
	var selected_end: int = hud.get_selection().size()
	var out := {"phase": phase, "n": n, "selected": selected_end, "decals": int(hud.get_frame_timings().get("sel_decals", 0)), "frames": m,
		"p50": med.call(times) if m > 0 else 0.0, "gpu_p50": med.call(gpu), "cpu_p50": med.call(cpu), "hud_p50": med.call(hud_times),
		"moved": _selperf_middle().distance_to(start), "live_frames": live_frames, "cpu_valid": cpu_valid,
		"selected_ok": selected_start == n and selected_end == n,
		"max": times.max() if m > 0 else 0.0, "mean": (Time.get_ticks_usec() - t0) / 1000.0 / maxf(m, 1)}
	var line := "SELPERF %s selected=%d decals=%d frames=%d frame p50 %.2f main cpu p50 %.2f (%d valid) gpu p50 %.2f hud p50 %.3f moved in the window %.1f live hint frames %d max %.1f |" % [
		phase, out.selected, out.decals, m, out.p50, out.cpu_p50, cpu_valid, out.gpu_p50, out.hud_p50, out.moved, live_frames, out.max]
	for k in parts.keys():
		line += " %s=%.3f" % [k, parts[k] / m]
	print(line)
	return out


## the middle of every horde of the block (did the army march)
func _selperf_middle() -> Vector2:
	var mid := Vector2()
	for id in _selperf_ids:
		var o: Dictionary = world.get_object(id)
		mid += Vector2(float(o.get("x", 0.0)), float(o.get("y", 0.0)))
	return mid / maxf(_selperf_ids.size(), 1)


## one window of `n` selected in a phase. A march window whose army did not move is done again with a new order, up to six times (150 hordes that
## turn round at the band's end jam for a few orders as they pass through each other, then walk on)
func _selperf_measure(phase: String, n: int) -> Dictionary:
	var w: Dictionary = {}
	for attempt in 7:
		w = await _selperf_measure_once(phase, n)
		if not w.has("stalled"):
			return w
		print("SELPERF %s: the window does not count (selected %d: the army did not move in it, or its move hint was drawn in too few frames), again with a new order" % [phase, n])
	return {"error": "the army did not march after seven %s orders (selected %d)" % [phase, n]}


## one window of `n` selected in a phase: the march phases give the army a fresh order first (its move hint is live through the window)
func _selperf_measure_once(phase: String, n: int) -> Dictionary:
	if _selperf_deadline > 0 and Time.get_ticks_msec() > _selperf_deadline:
		return {"error": "out of time (--selperf-deadline): the run stops before %s %d" % [phase, n]}
	var marching := phase == "march" or phase == "waypoints"
	var once := _selperf_arg("selperf-march-once", "0") == "1"
	if marching:
		# --selperf-march-once=1: the army walks on from one order (given again only when it stopped): no click per window (a build whose frames
		# take a second makes every click a hold: the before of this lane)
		if not once or _selperf_stalled:
			var r: String = await _selperf_order(phase == "waypoints")
			if r != "":
				return {"error": r}
		_selperf_stalled = false
	hud.select_objects(_selperf_ids.slice(0, n))
	var fresh := marching and not once
	if not fresh:
		# a held selection: the window starts once the command bar the selection brought up has settled (1 s)
		if not marching:
			hud.camera_look_at(_selperf_centre)
		var settle := Time.get_ticks_msec()
		while Time.get_ticks_msec() - settle < 1000:
			await game.get_tree().process_frame
	# a fresh order: the window is the frames its move hint is drawn in (41 rendered frames from the order)
	var w: Dictionary = await _selperf_window(phase, n, 1.0, {"idle": "", "pan": "pan", "march": "follow", "waypoints": "follow"}[phase], fresh)
	# the window is valid: the selection is n from its start to its end; the army moved during it (marching); the hint was drawn in 20 frames or more
	# (a fresh order); the main thread's CPU time was there in 80% of its frames (judged on it)
	if not w.selected_ok:
		# the selection changed during the window (a windowed run: the system pointer's own events): the window does not count, it is done again
		var sel: Array = hud.get_selection()
		var what: Array = []
		for id in sel.slice(0, 4):
			what.append("%s %s" % [str(id), str(world.get_object(int(id)).get("template", "?"))])
		print("SELPERF %s %d: the selection was not %d through the window (%d at its end: %s), again" % [phase, n, n, int(w.selected), ", ".join(what)])
		_selperf_bad_selection += 1
		if _selperf_bad_selection >= 3:
			return {"error": "%s %d: the selection was not %d through three windows in a row (%d at the end: %s)" % [phase, n, n, int(w.selected), ", ".join(what)]}
		w["stalled"] = true
		return w
	_selperf_bad_selection = 0
	if _selperf_gate_cpu and int(w.cpu_valid) < int(0.8 * float(w.frames)):
		return {"error": "%s %d: the main thread's CPU time was missing in %d of %d frames" % [phase, n, int(w.frames) - int(w.cpu_valid), int(w.frames)]}
	if fresh and int(w.live_frames) < 20:
		w["stalled"] = true
		_selperf_stalled = true
		print("SELPERF %s: the move hint was drawn in %d frames of the window only" % [phase, int(w.live_frames)])
		return w
	if marching and float(w.moved) < 2.0:
		w["stalled"] = true
		_selperf_stalled = true
	return w


func _scenario_selperf() -> String:
	await _headless_window()
	if game._local_index < 0:
		return "no local player index"
	var spot := Vector2()
	for o in world.get_player_objects(game._local_name):
		if not o.structure:
			spot = Vector2(o.x, o.y)
			break
	_selperf_centre = spot + Vector2(0, 420)
	var at := _selperf_arg("selperf-at", "")
	if at != "":
		_selperf_centre = Vector2(float(at.split(",")[0]), float(at.split(",")[1]))
	var reps := int(_selperf_arg("selperf-reps", "5"))
	_selperf_gate_cpu = _selperf_arg("selperf-gate-cpu", "1") == "1"
	var deadline_s := float(_selperf_arg("selperf-deadline", "0"))
	_selperf_deadline = Time.get_ticks_msec() + int(deadline_s * 1000.0) if deadline_s > 0.0 else 0
	var budget := float(_selperf_arg("selperf-budget", "0.10"))
	var phases := _selperf_arg("selperf-phases", ",".join(SELPERF_PHASES)).split(",")
	var counts: Array = []
	for c in _selperf_arg("selperf-counts", ",".join(SELPERF_COUNTS.map(func(x): return str(x)))).split(","):
		counts.append(int(c))
	_selperf_ids = PackedInt64Array()
	for r in 10:
		for c in 15:
			var h := _create_first(SELPERF_HORDES, _selperf_centre + Vector2((c - 7) * 55.0, (r - 4.5) * 55.0))
			if not h.is_empty():
				_selperf_ids.append(h.id)
	if _selperf_ids.size() < SELPERF_COUNTS.back():
		return "created only %d of %d hordes" % [_selperf_ids.size(), SELPERF_COUNTS.back()]
	print("SELPERF %d hordes of %s around %s, %d objects" % [_selperf_ids.size(), str(world.get_object(_selperf_ids[0]).get("template", "")), _selperf_centre,
		world.get_object_count()])
	await game._step(150) # the hordes form up
	if _selperf_arg("selperf-overview", "0") == "1":
		# --selperf-overview=1 (with --screens): the ground around the block from high above (choosing a march route on another map)
		hud.set_free_camera(true)
		hud.camera_look_at(_selperf_centre)
		hud.camera_set_height(3000.0)
		await game._step(30)
		shot("selperf-overview")
		print("SELPERF overview: centre %s; pixel of the centre %s, of +1000 x %s, of +1000 y %s" % [_selperf_centre, to_pixel(_selperf_centre.x, _selperf_centre.y),
			to_pixel(_selperf_centre.x + 1000, _selperf_centre.y), to_pixel(_selperf_centre.x, _selperf_centre.y + 1000)])

	# one camera height for every window: the whole block on the screen
	hud.set_free_camera(true)
	var height := 0.0
	var fits := false
	for hgt in [500.0, 650.0, 800.0, 1000.0, 1300.0]:
		height = hgt
		hud.camera_look_at(_selperf_centre)
		hud.camera_set_height(height)
		await game._step(10)
		fits = true
		for id in _selperf_ids:
			var o: Dictionary = world.get_object(id)
			fits = fits and on_screen(to_pixel(float(o.get("x", 0.0)), float(o.get("y", 0.0))))
		if fits:
			break
	if not fits:
		return "the block does not fit on the screen"
	print("SELPERF the camera %.0f above the ground; %d repetitions; phases %s" % [height, reps, ",".join(phases)])
	_selperf_target = _selperf_centre
	# pairs of windows, nothing selected and n selected, the order alternating from one repetition to the next (the machine's drift cancels). Judged per
	# pair, each the median of the pairs: the main thread's CPU time per frame (other load on a shared machine does not inflate it as it does the wall
	# time) within +10%; the GPU's extra time per frame within 10% of the unselected frame (the GPU works beside the CPU: its time adds to the frame
	# only where it is the longer); with --selperf-gate-wall=1 (a quiet machine) the wall time per frame within +10%
	var gate_wall := _selperf_arg("selperf-gate-wall", "0") == "1"
	var fails: Array = []
	var table := {}
	var med := func(a: Array) -> float:
		var v := a.duplicate()
		v.sort()
		return float(v[v.size() / 2])
	for phase in phases:
		if phase == "march" or phase == "waypoints":
			# the army gets going first (the first order's searches, the hordes' start)
			var r0: String = await _selperf_order(phase == "waypoints")
			if r0 != "":
				return r0
			await game._step(120)
		for n in counts:
			var cost_ratios: Array = []
			var wall_ratios: Array = []
			var cost0: Array = []
			var costn: Array = []
			var wall0: Array = []
			var walln: Array = []
			var gpu0: Array = []
			var gpun: Array = []
			var mean0: Array = []
			var meann: Array = []
			for r in reps:
				var w0: Dictionary
				var wn: Dictionary
				if r % 2 == 0:
					w0 = await _selperf_measure(phase, 0)
					wn = await _selperf_measure(phase, n)
				else:
					wn = await _selperf_measure(phase, n)
					w0 = await _selperf_measure(phase, 0)
				for w in [w0, wn]:
					if w.has("error"):
						return w.error
				var c0: float = w0.cpu_p50
				var cn: float = wn.cpu_p50
				cost_ratios.append(cn / maxf(c0, 0.001))
				wall_ratios.append(wn.p50 / maxf(w0.p50, 0.001))
				cost0.append(c0)
				costn.append(cn)
				wall0.append(w0.p50)
				walln.append(wn.p50)
				gpu0.append(w0.gpu_p50)
				gpun.append(wn.gpu_p50)
				mean0.append(w0.mean)
				meann.append(wn.mean)
				if r == 0 and n == counts.back() and phase in ["idle", "march"]:
					shot("selperf-%s-%d" % [phase, n])
			var cost_ratio: float = med.call(cost_ratios)
			var gpu_extra: Array = []
			for r in gpu0.size():
				gpu_extra.append((gpun[r] - gpu0[r]) / maxf(wall0[r], 0.001))
			var gpu_cost: float = med.call(gpu_extra)
			var wall_ratio: float = med.call(wall_ratios)
			table["%s/%d" % [phase, n]] = cost_ratio
			var over := (_selperf_gate_cpu and cost_ratio > 1.0 + budget) or gpu_cost > budget or (gate_wall and wall_ratio > 1.0 + budget)
			print("SELPERF RESULT %s %d selected: main CPU %.2f -> %.2f ms x%.3f; GPU %.2f -> %.2f ms (+%.1f%% of the frame); wall %.2f -> %.2f ms x%.3f (means %.2f -> %.2f); medians of %d pairs%s" % [
				phase, n, med.call(cost0), med.call(costn), cost_ratio, med.call(gpu0), med.call(gpun), gpu_cost * 100.0, med.call(wall0), med.call(walln), wall_ratio,
				med.call(mean0), med.call(meann), reps,
				"  OVER BUDGET" if over else ""])
			if _selperf_gate_cpu and cost_ratio > 1.0 + budget:
				fails.append("%s %d: main CPU x%.3f" % [phase, n, cost_ratio])
			if gpu_cost > budget:
				fails.append("%s %d: GPU +%.1f%%" % [phase, n, gpu_cost * 100.0])
			if gate_wall and wall_ratio > 1.0 + budget:
				fails.append("%s %d: wall x%.3f" % [phase, n, wall_ratio])
	hud.select_objects(PackedInt64Array())
	print("SELPERF orders: %d move orders, %d switches to the queued order mode (Alt: MSG_CHANGE_ORDERMODE 1, 0)" % [log_count("MSG_DO_MOVETO"),
		log_count("MSG_CHANGE_ORDERMODE i1 i0")])
	print("SELPERF BUDGET %s (frame time within +%.0f%% of the same scene with nothing selected, %d checks)" % ["ok" if fails.is_empty() else "FAILED",
		budget * 100.0, table.size()])
	return "" if fails.is_empty() else "over the budget: " + ", ".join(fails)


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
	elif c1.height_above_ground > c1.zoom_out_limit + 0.01:
		return "the camera zoomed out beyond its limit (lane ZOOM-1: the map's max height times the Max zoom-out, %.2f)" % float(c1.get("zoom_out_factor", 1.0))
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
			if pl.get("placing", false) and int(pl.legal) != 0 and int(pl.legal) != 9 and ghost_tint().r > 0.5 and not red_seen:
				red_seen = true
				shot("skirmish-ghost-illegal")
			if pl.get("placing", false) and int(pl.legal) == 0:
				site = px
				break
		if site.x >= 0:
			break
	print("PLAY1 skirmish: the placement ghost %s (tint %s), red on an illegal spot: %s, errors %s" % [ghost_template(), ghost_tint(), red_seen, ghost_state().get("errors", [])])
	if ghost_template() != build.template:
		fails.append("the placement ghost shows '%s', not %s" % [ghost_template(), build.template])
	elif ghost_tint().a > 0:
		fails.append("the ghost is tinted %s on a legal site" % ghost_tint())
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
