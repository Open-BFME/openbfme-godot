## Lane HUD-7: the HUD items of a Discord tester's report (GitGut, v0.3.0-preview.6), scripted for proof screenshots. Every scenario prints
## "HUD7 <name>: ok" or "HUD7 <name>: FAIL <why>", saves hud7-<name>*.png in --screens and "HUD7 RESULT: <n> ok, <m> failed" ends the run. game.gd runs it with
## --auto --faction=FactionMen --hud7=radar,deselect,sell,upgrade,herocancel
##   radar       the drawn minimap against the camera, a unit's blip and a click on it, for the camera default / zoomed out / turned / tilted / at the map's edges
##               (the click rectangle is the drawn picture, the blip is drawn where the click maps it, the shown ground is inside the drawn view box)
##   deselect    a unit selected by a click, then a left click on empty ground (RotWK's SelectionTranslator RW 0x83C44F: the selection is dropped)
##   sell        a barracks the builder buys and builds, selected by a click, its Sell button clicked: half the price back (income excluded), then it is gone
##   upgrade     the fortress's first affordable upgrade bubble clicked: the button shows the purchased state while it is researched (RW 0x943C40 availability 8)
##   herocancel  the fortress's hero ring, a hero recruited by a click, then a right click on the same bubble cancels it with the refund (RW 0x940C57)
extends RefCounted

var game: Node
var hud: Node
var world: Node
var ok_count := 0
var fail_count := 0


func run(p_game: Node, scenarios: PackedStringArray) -> void:
	game = p_game
	hud = game._hud
	world = game._world
	for s in scenarios:
		var r: String = await call("_scenario_" + s)
		if r == "":
			ok_count += 1
			print("HUD7 %s: ok" % s)
		else:
			fail_count += 1
			print("HUD7 %s: FAIL %s" % [s, r])
	print("HUD7 RESULT: %d ok, %d failed" % [ok_count, fail_count])


# ---- helpers ----------------------------------------------------------------------------------------------------------------------------------------------

func shot(name: String) -> void:
	game._save_named("hud7-" + name)


func own() -> Array:
	return world.get_player_objects(game._local_name)


func money() -> int:
	for p in world.get_economy().get("players", []):
		if str(p.name) == game._local_name:
			return int(p.money)
	return -1


func look(x: float, y: float) -> void:
	hud.camera_look_at(Vector2(x, y))
	await game._step(20)


func click_pixel(p: Vector2, right: bool = false) -> void:
	hud.inject_mouse_move(p)
	await game._step(2)
	hud.inject_mouse_button(2 if right else 1, true, p)
	await game._step(1)
	hud.inject_mouse_button(2 if right else 1, false, p)
	await game._step(4)


func find_own(pred: Callable) -> Dictionary:
	for o in own():
		if pred.call(o):
			return o
	return {}


# a click where the object shows (the real pick); the id hook only as a logged fallback
func select_object(o: Dictionary) -> bool:
	await look(o.x, o.y)
	var centre: Vector2 = hud.world_to_pixel(Vector2(o.x, o.y))
	for dy in range(-120, 41, 10):
		for dx in [0, -20, 20, -40, 40]:
			var px := centre + Vector2(dx, dy)
			var probe: Dictionary = hud.pick_probe(px, int(o.id))
			if int(probe.get("picked", 0)) == int(o.id):
				await click_pixel(px)
				await game._step(6)
				if hud.get_selection().has(int(o.id)):
					print("HUD7 select: %s %d selected by a click at (%d, %d)" % [o.template, int(o.id), int(px.x), int(px.y)])
					return true
	print("HUD7 select: no click selected %s %d; selecting it by id (GAME TEST HOOK)" % [o.template, int(o.id)])
	return hud.select_object(int(o.id))


func radial() -> Dictionary:
	return hud.get_hud6_state().get("radial", {})


func bubble_centre(b: Dictionary) -> Vector2:
	var r: Rect2 = b.rect
	return r.position + r.size * 0.5


func fortress() -> Dictionary:
	return find_own(func(o): return str(o.template).find("Fortress") >= 0 and str(o.template).find("Citadel") >= 0)


# a pixel of the view that picks nothing and is not over the HUD: the ground beside the fortress
func empty_ground_pixel() -> Vector2:
	var size: Vector2 = game.get_viewport().get_visible_rect().size
	for fy in [0.3, 0.4, 0.25, 0.45]:
		for fx in [0.2, 0.3, 0.7, 0.8, 0.5]:
			var px := Vector2(size.x * fx, size.y * fy)
			var probe: Dictionary = hud.pick_probe(px, 0)
			if int(probe.get("picked", 0)) == 0:
				return px
	return Vector2(-1, -1)


# the fortress's first ring, then the sub-ring whose PUSH_VISIBLE_COMMAND_RANGE bubble is named *<part>* (Command_SelectUpgradesMenFortress, ...Revivables...)
func open_ring(part: String) -> bool:
	for i in 3:
		var back := {}
		for b in radial().get("buttons", []):
			if str(b.command) == "POP_VISIBLE_COMMAND_RANGE":
				back = b
		if back.is_empty():
			break
		await click_pixel(bubble_centre(back))
		hud.inject_mouse_move(Vector2(60, 60))
		await game._step(20)
	for b in radial().get("buttons", []):
		if str(b.command) == "PUSH_VISIBLE_COMMAND_RANGE" and str(b.name).find(part) >= 0:
			await click_pixel(bubble_centre(b))
			hud.inject_mouse_move(Vector2(60, 60))
			await game._step(30)
			return true
	return false


func button_by(command: String) -> Dictionary:
	for b in hud.get_command_buttons():
		if str(b.command) == command:
			return b
	return {}


func click_button(b: Dictionary, right: bool = false) -> bool:
	var w: Dictionary = hud.find_button_window(b.frame)
	if not w.get("found", false):
		return false
	await click_pixel(Vector2(w.x, w.y), right)
	return true


# ---- scenarios ------------------------------------------------------------------------------------------------------------------------------------------

# ---- radar (item 7): what the minimap draws agrees with the camera, the units and a click on it ----------------------------------------------------------

# the window pixel of SAGE (x, y) in the picture the radar drew (Radar::worldToRadar over the drawn rectangle: north up)
func radar_pixel(st: Dictionary, w: Vector2) -> Vector2:
	var pic: Rect2 = st.picture
	var ext: Vector2 = st.extent
	return Vector2(pic.position.x + w.x / ext.x * pic.size.x, pic.position.y + (1.0 - w.y / ext.y) * pic.size.y)


func in_quad(p: Vector2, q: PackedVector2Array, slack: float) -> bool:
	# inside the convex quad (either winding), or within `slack` pixels of an edge
	var sign := 0.0
	for i in 4:
		var a: Vector2 = q[i]
		var b: Vector2 = q[(i + 1) % 4]
		var e := b - a
		var cross := e.x * (p.y - a.y) - e.y * (p.x - a.x)
		var dist := cross / maxf(e.length(), 0.001)
		if absf(dist) <= slack:
			continue
		if sign == 0.0:
			sign = signf(dist)
		elif signf(dist) != sign:
			return false
	return true


func convex(q: PackedVector2Array) -> bool:
	var sign := 0.0
	for i in 4:
		var a: Vector2 = q[i]
		var b: Vector2 = q[(i + 1) % 4]
		var c: Vector2 = q[(i + 2) % 4]
		var cross := (b - a).cross(c - b)
		if absf(cross) < 0.001:
			continue
		if sign == 0.0:
			sign = signf(cross)
		elif signf(cross) != sign:
			return false
	return true


func res_tag() -> String:
	var size: Vector2 = game.get_viewport().get_visible_rect().size
	return "%dx%d-%s" % [int(size.x), int(size.y), "wider" if bool(hud.get_camera().get("widescreen_wider", false)) else "original"]


# the checks of one camera pose; "" when everything agrees
func radar_checks(setup: String, own_obj: Dictionary) -> String:
	hud.inject_mouse_move(Vector2(60, 60))
	await game._step(10)
	var st_a: Dictionary = hud.get_radar_state()
	await game._step(30)
	var st: Dictionary = hud.get_radar_state()
	var cam: Dictionary = hud.get_camera()
	var size: Vector2 = game.get_viewport().get_visible_rect().size
	print("HUD7 radar %s %s: camera %s angle %.3f pitch %.3f height %.0f, state %s" % [res_tag(), setup, str(cam.position), float(cam.angle), float(cam.pitch),
		float(cam.height_above_ground), JSON.stringify(st)])
	shot("radar-%s-%s" % [res_tag(), setup])
	if not bool(st.get("ready", false)) or int(st.get("draws", 0)) == 0:
		return "%s: the radar was not drawn" % setup
	# the view box is drawn with every radar draw (W3DRadar::draw always draws it, RW 0x44FDF3 -> 0x50434F)
	var drawn := int(st.get("draws", 0)) - int(st_a.get("draws", 0))
	var boxes := int(st.get("view_box_draws", 0)) - int(st_a.get("view_box_draws", 0))
	if drawn <= 0 or boxes != drawn:
		return "%s: the view box was drawn %d times in the radar's last %d draws" % [setup, boxes, drawn]
	# 1. the rectangle the clicks map through is the picture the radar drew
	var sq: Rect2 = st.square
	var pic: Rect2 = st.picture
	if (sq.position - pic.position).length() > 1.0 or (sq.size - pic.size).length() > 1.0:
		return "%s: the click rectangle %s is not the drawn picture %s" % [setup, str(sq), str(pic)]
	# 2. the unit's blip: its overlay cell is drawn, and the click mapping puts it where the picture shows it (the unit's position now)
	var now_obj: Dictionary = world.get_object(int(own_obj.id))
	own_obj = {"id": own_obj.id, "template": own_obj.template, "x": now_obj.get("x", own_obj.x), "y": now_obj.get("y", own_obj.y)}
	var probe: Dictionary = hud.radar_probe(Vector2(own_obj.x, own_obj.y))
	var click_px: Vector2 = hud.world_to_radar_pixel(Vector2(own_obj.x, own_obj.y))
	var drawn_px := radar_pixel(st, Vector2(own_obj.x, own_obj.y))
	print("HUD7 radar %s: %s %d blip cell %s alpha %d, drawn at %s, the click mapping at %s" % [setup, own_obj.template, int(own_obj.id), str(probe.get("cell")),
		int(probe.get("alpha", 0)), str(drawn_px), str(click_px)])
	if int(probe.get("alpha", 0)) == 0:
		return "%s: no blip drawn at %s's cell %s" % [setup, own_obj.template, str(probe.get("cell"))]
	if (click_px - drawn_px).length() > 1.5:
		return "%s: the click mapping of %s (%s) is not its drawn place (%s)" % [setup, own_obj.template, str(click_px), str(drawn_px)]
	# 3. the ground the camera shows is inside the drawn view box: the screen's centre and four inner points that hit the ground
	var box: PackedVector2Array = st.view_box
	if not convex(box):
		print("HUD7 radar %s: the drawn box %s is not convex (a corner's ray meets the plane behind the eye, as RotWK draws it): containment not tested" % [setup, str(box)])
		return ""
	var tested := 0
	for f in [Vector2(0.5, 0.5), Vector2(0.3, 0.4), Vector2(0.7, 0.4), Vector2(0.3, 0.6), Vector2(0.7, 0.6)]:
		var g: Vector2 = hud.pixel_to_world(Vector2(size.x * f.x, size.y * f.y))
		if g.x < -1e8:
			continue
		tested += 1
		var rp := radar_pixel(st, g)
		if not in_quad(rp, box, 2.0):
			return "%s: the ground at screen %s (world %s, radar %s) is outside the drawn view box %s" % [setup, str(f), str(g), str(rp), str(box)]
	if tested == 0:
		return "%s: no screen point hits the ground" % setup
	return ""


# a left click on the minimap at the drawn pixel of `target`: the camera then looks at it (LeftHUDInput RW 0x803CE4 -> lookAt)
var click_unit := {}

func radar_click_check(setup: String, target: Vector2) -> String:
	var st: Dictionary = hud.get_radar_state()
	var px := radar_pixel(st, target)
	await click_pixel(px)
	await game._step(30)
	var size: Vector2 = game.get_viewport().get_visible_rect().size
	var centre: Vector2 = hud.pixel_to_world(size * 0.5)
	var pos: Vector2 = hud.get_camera().position
	var cell: float = maxf(st.extent.x, st.extent.y) / 128.0
	var tol := cell * 1.5 + 30.0
	var st2: Dictionary = hud.get_radar_state()
	var box: PackedVector2Array = st2.view_box
	print("HUD7 radar %s: clicked the minimap at %s for %s: the camera looks at %s, the screen's centre shows %s (tolerance %.0f), the box %s" % [setup, str(px),
		str(target), str(pos), str(centre), tol, str(box)])
	if (pos - target).length() > tol:
		return "%s: a click on the minimap at %s for %s left the camera looking at %s" % [setup, str(px), str(target), str(pos)]
	# the redrawn box holds the ground the screen now shows (the clicked point itself may sit just outside: the box lies on the terrain's average height,
	# RW 0x44D25B, the point on its ground)
	return await radar_checks(setup + "-after-click", click_unit)


func _scenario_radar() -> String:
	var f := fortress()
	if f.is_empty():
		return "no own fortress citadel"
	var st0: Dictionary = hud.get_radar_state()
	await game._step(10)
	st0 = hud.get_radar_state()
	var ext: Vector2 = st0.get("extent", Vector2.ZERO)
	if ext.x <= 0:
		return "the radar has no extent"
	# the blip to check: an own unit (a structure's mark is a circle outline around its cell)
	var u := find_own(func(o): return not bool(o.structure))
	if u.is_empty():
		return "no own unit for the blip check"
	click_unit = u
	var fails := PackedStringArray()
	var r := ""
	# the default camera on the own fortress
	hud.camera_look_at(Vector2(f.x, f.y))
	r = await radar_checks("default", u)
	if r != "": fails.append(r)
	r = await radar_click_check("default", ext * 0.5)
	if r != "": fails.append(r)
	# zoomed out as far as the player may
	hud.camera_look_at(Vector2(f.x, f.y))
	var cam: Dictionary = hud.get_camera()
	hud.camera_set_height(float(cam.get("zoom_out_limit", cam.get("max_height", 400.0))))
	r = await radar_checks("zoomed-out", u)
	if r != "": fails.append(r)
	# turned and tilted (TacticalCamera::setAngle / setPitch, GAME TEST HOOK, as the rotate keys and the tilt do), at the default height and zoomed out
	var h0 := float(cam.get("height_above_ground", 300.0))
	var a0 := float(cam.get("angle", 0.0))
	var p0 := float(cam.get("pitch", 0.0))
	var zoomed := float(cam.get("zoom_out_limit", cam.get("max_height", 400.0)))
	for pose in [["turned", 0.9, 0.0, h0], ["tilted-up", 0.0, 0.3, h0], ["tilted-down", 0.0, -0.3, h0], ["turned-zoomed-out", -1.2, 0.0, zoomed],
			["turned-tilted-zoomed-out", 0.9, 0.5, zoomed]]:
		print("HUD7 radar: camera angle %.2f, pitch %.2f, height %.0f (GAME TEST HOOK)" % [pose[1], pose[2], pose[3]])
		hud.camera_set_angle_pitch(pose[1], pose[2])
		hud.camera_set_height(pose[3])
		hud.camera_look_at(Vector2(f.x, f.y))
		r = await radar_checks(pose[0], u)
		if r != "": fails.append(r)
		r = await radar_click_check(pose[0], Vector2(ext.x * 0.3, ext.y * 0.7))
		if r != "": fails.append(r)
	hud.camera_set_angle_pitch(a0, p0)
	hud.camera_set_height(h0)
	# the map's borders: a click on the minimap's corner and edge (the camera stops at the map's limits; the box follows it)
	for t in [Vector2(ext.x * 0.02, ext.y * 0.02), Vector2(ext.x * 0.98, ext.y * 0.5)]:
		await radar_click_check("border", t) # the camera may stop short of the edge: only the box agreement is asserted here
		r = await radar_checks("border-%d-%d" % [int(t.x), int(t.y)], u)
		if r != "": fails.append(r)
	# along the south edge (as a player scrolls there): the box follows the camera at every stop
	for k in 4:
		var x := ext.x * (0.2 + 0.2 * k)
		hud.camera_look_at(Vector2(x, ext.y * 0.03))
		r = await radar_checks("south-edge-%d" % k, u)
		if r != "": fails.append(r)
	hud.camera_look_at(Vector2(f.x, f.y))
	await game._step(20)
	if fails.size() > 0:
		return "; ".join(fails)
	return ""


func _scenario_deselect() -> String:
	var u := find_own(func(o): return not bool(o.structure) and bool(o.get("dozer", false)))
	if u.is_empty():
		return "no own unit"
	if not await select_object(u):
		return "the unit could not be selected"
	hud.inject_mouse_move(Vector2(60, 60))
	await game._step(10)
	shot("deselect-1-selected")
	var px := empty_ground_pixel()
	if px.x < 0:
		return "no empty ground pixel"
	await click_pixel(px)
	await game._step(10)
	var sel: Array = hud.get_selection()
	var logic_sel: Array = hud.get_logic_selection()
	print("HUD7 deselect: left click on empty ground at (%d, %d): client selection %s, logic selection %s" % [int(px.x), int(px.y), str(sel), str(logic_sel)])
	shot("deselect-2-after-ground-click")
	if not sel.is_empty():
		return "the selection stayed after a left click on empty ground: " + str(sel)
	return ""


func eco() -> Dictionary:
	for p in world.get_economy().get("players", []):
		if str(p.name) == game._local_name:
			return p
	return {}


func _scenario_sell() -> String:
	var f := fortress()
	if f.is_empty():
		return "no own fortress citadel"
	var porter := find_own(func(o): return bool(o.get("dozer", false)) and not bool(o.structure))
	if porter.is_empty():
		return "no own builder"
	if not await select_object(porter):
		return "the builder could not be selected"
	hud.inject_mouse_move(Vector2(60, 60))
	await game._step(20)
	var build := {}
	for b in hud.get_command_buttons():
		if str(b.command) == "DOZER_CONSTRUCT" and str(b.template).find("Barracks") >= 0:
			build = b
	if build.is_empty():
		return "the builder shows no barracks button: " + JSON.stringify(hud.get_command_buttons())
	# bought: the builder's barracks button, then a click on a legal site beside the fortress (a ring search reading the HUD's own verdict, as the QA player)
	var e0 := eco()
	if not await click_button(build):
		return "the barracks button's window was not found"
	await game._step(4)
	if not bool(hud.get_placement().get("placing", false)):
		return "the barracks button did not start the placement"
	var site := Vector2()
	var found := false
	for ring in range(3, 10):
		for k in 12:
			var a := TAU * float(k) / 12.0
			var p := Vector2(f.x, f.y) + Vector2(cos(a), sin(a)) * 70.0 * ring
			await look(p.x, p.y)
			hud.inject_mouse_move(hud.world_to_pixel(p))
			await game._step(3)
			var pl: Dictionary = hud.get_placement()
			if bool(pl.get("placing", false)) and int(pl.get("legal", -1)) == 0:
				site = p
				found = true
				break
		if found:
			break
	if not found:
		return "no legal site for the barracks around the fortress"
	await click_pixel(hud.world_to_pixel(site))
	var barracks := {}
	for i in 60:
		await game._step(10)
		barracks = find_own(func(o): return str(o.template) == str(build.template) and Vector2(o.x, o.y).distance_to(site) < 120.0)
		if not barracks.is_empty():
			break
	if barracks.is_empty():
		return "no barracks appeared at the site"
	var e1 := eco()
	# the price: what left the account, the buildings' income (Economy's income log) added back
	var paid := (int(e0.money) - int(e1.money)) + (int(e1.income_logged) - int(e0.income_logged))
	print("HUD7 sell: bought %s %d at %s: paid %d (money %d -> %d, income %d)" % [build.template, int(barracks.id), str(site), paid, int(e0.money), int(e1.money), int(e1.income_logged) - int(e0.income_logged)])
	if paid <= 0:
		return "the barracks was not paid for (%d)" % paid
	var id := int(barracks.id)
	for i in 240:
		var o: Dictionary = world.get_object(id)
		if not bool(o.get("under_construction", true)):
			break
		await game._step(30)
	var full: Dictionary = world.get_object(id)
	if bool(full.get("under_construction", true)):
		return "the barracks did not finish"
	if float(full.get("health", 0)) != float(full.get("max_health", -1)):
		return "the barracks is damaged (%s / %s): the refund would scale" % [str(full.health), str(full.max_health)]
	if not await select_object(full):
		return "the barracks could not be selected"
	hud.inject_mouse_move(Vector2(60, 60))
	await game._step(20)
	shot("sell-1-purchased-barracks-selected")
	var sell := button_by("SELL")
	if sell.is_empty():
		return "the barracks shows no Sell button"
	# the control window: with nothing bought or sold, every change of the money is logged income (so the log isolates the refund below)
	var c0 := eco()
	await game._step(300)
	var c1 := eco()
	print("HUD7 sell: control window: money %d -> %d, logged income %d" % [int(c0.money), int(c1.money), int(c1.income_logged) - int(c0.income_logged)])
	if int(c1.money) - int(c0.money) != int(c1.income_logged) - int(c0.income_logged):
		return "the income log does not account for the money's change in the control window"
	var e2 := eco()
	if not await click_button(sell):
		return "the Sell button's window was not found"
	await game._step(60)
	var e3 := eco()
	shot("sell-2-sell-clicked")
	# the refund: the money's change without the buildings' income (Economy's income log; the sale's deposit is not income)
	var refund := (int(e3.money) - int(e2.money)) - (int(e3.income_logged) - int(e2.income_logged))
	print("HUD7 sell: pressed %s by a click: money %d -> %d, income %d, refund %d of %d paid" % [str(sell.name), int(e2.money), int(e3.money), int(e3.income_logged) - int(e2.income_logged), refund, paid])
	if refund != paid / 2:
		return "the refund is %d, not half of the %d paid (SellPercentage 50%%, full health)" % [refund, paid]
	var gone := false
	for i in 120:
		var o: Dictionary = world.get_object(id)
		if not bool(o.get("ok", false)):
			gone = true
			break
		await game._step(15)
	shot("sell-3-barracks-gone")
	if not gone:
		return "the sold barracks is still in the world"
	return ""


func _scenario_upgrade() -> String:
	var f := fortress()
	if f.is_empty():
		return "no own fortress citadel"
	if not await select_object(f):
		return "the fortress could not be selected"
	hud.camera_set_height(330.0)
	await look(f.x, f.y)
	hud.inject_mouse_move(Vector2(60, 60))
	await game._step(30)
	world.give_money(game._local_index, 5000)
	print("HUD7 upgrade: gave 5000 (GAME TEST HOOK)")
	await game._step(10)
	if not await open_ring("Upgrades"):
		return "the fortress has no upgrade ring bubble: " + JSON.stringify(radial().get("buttons", []))
	var target := {}
	for b in radial().get("buttons", []):
		if str(b.command) == "OBJECT_UPGRADE" and int(b.state) == 1:
			target = b
			break
	if target.is_empty():
		return "no enabled OBJECT_UPGRADE bubble: " + JSON.stringify(radial().get("buttons", []))
	shot("upgrade-1-before")
	var before := money()
	await click_pixel(bubble_centre(target))
	hud.inject_mouse_move(Vector2(60, 60))
	await game._step(30)
	var now := {}
	for b in radial().get("buttons", []):
		if int(b.slot) == int(target.slot):
			now = b
	var prod: Dictionary = world.get_production(game._local_index, int(f.id))
	print("HUD7 upgrade: pressed %s: money %d -> %d, bubble now %s, production %s" % [str(target.name), before, money(), JSON.stringify(now), JSON.stringify(prod)])
	shot("upgrade-2-in-progress")
	var listed := false
	for b in hud.get_command_buttons():
		if str(b.name) == str(target.name):
			listed = true
			print("HUD7 upgrade: the control bar's button %s" % JSON.stringify(b))
	if now.is_empty():
		return "the upgrade's bubble disappeared after the press (listed by the bar: %s)" % str(listed)
	# ControlBar ButtonState: 1 Enabled, 2 Restricted (RotWK availability 0), 6 Purchased (RotWK availability 8: dimmed colour, grey rim)
	if int(now.state) != 6:
		return "the upgrade's bubble shows state %d after the press, not RotWK's availability 8 (Purchased, 6)" % int(now.state)
	return ""


func _scenario_herocancel() -> String:
	var f := fortress()
	if f.is_empty():
		return "no own fortress citadel"
	if not await select_object(f):
		return "the fortress could not be selected"
	hud.camera_set_height(330.0)
	await look(f.x, f.y)
	hud.inject_mouse_move(Vector2(60, 60))
	await game._step(30)
	world.give_money(game._local_index, 5000)
	print("HUD7 herocancel: gave 5000 (GAME TEST HOOK)")
	await game._step(10)
	if not await open_ring("Revivables"):
		return "the fortress has no hero ring bubble: " + JSON.stringify(radial().get("buttons", []))
	var hero := {}
	for b in radial().get("buttons", []):
		if str(b.command) == "REVIVE" and int(b.state) == 1:
			hero = b
			break
	if hero.is_empty():
		return "no enabled hero bubble: " + JSON.stringify(radial().get("buttons", []))
	var e0 := eco()
	var before := int(e0.money)
	await click_pixel(bubble_centre(hero))
	hud.inject_mouse_move(Vector2(60, 60))
	await game._step(20)
	var e1 := eco()
	var paid := int(e1.money)
	var prod: Dictionary = world.get_production(game._local_index, int(f.id))
	var queued_before := hero_entries(prod)
	print("HUD7 herocancel: recruited %s: money %d -> %d, production %s" % [str(hero.name), before, paid, JSON.stringify(prod)])
	shot("herocancel-1-recruiting")
	# the price without the buildings' income meanwhile (Economy's income log)
	var price := (before - paid) + (int(e1.income_logged) - int(e0.income_logged))
	if price <= 0:
		return "the hero was not recruited (money %d -> %d)" % [before, paid]
	await click_pixel(bubble_centre(hero), true)
	hud.inject_mouse_move(Vector2(60, 60))
	await game._step(20)
	var e2 := eco()
	var back := int(e2.money)
	prod = world.get_production(game._local_index, int(f.id))
	print("HUD7 herocancel: right click on %s: money %d -> %d, production %s" % [str(hero.name), paid, back, JSON.stringify(prod)])
	shot("herocancel-2-cancelled")
	var refund := (back - paid) - (int(e2.income_logged) - int(e1.income_logged))
	print("HUD7 herocancel: price %d, refund %d (income excluded)" % [price, refund])
	if refund != price:
		return "the right click did not refund the recruit's price (price %d, refund %d; money %d -> %d -> %d)" % [price, refund, before, paid, back]
	if queued_before != 1:
		return "the recruit made %d hero entries in the queue, not 1" % queued_before
	if hero_entries(prod) != 0:
		return "the queue still holds the hero: " + JSON.stringify(prod.get("queue"))
	return ""


# the queue entries that produce a unit (a hero), not an upgrade (an upgrade entry has quantity_total 0)
func hero_entries(prod: Dictionary) -> int:
	var n := 0
	for e in prod.get("queue", []):
		if int(e.quantity_total) > 0:
			n += 1
	return n
