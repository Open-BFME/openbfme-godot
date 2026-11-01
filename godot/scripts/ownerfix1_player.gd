## Lane OWNERFIX-1: the owner's problems of 2026-10-15 driven through the real game (input events into the viewport, the real menus), with captures.
## game.gd runs it with --ownerfix1=<scenario>[,<scenario>...] after the start (a skirmish with --auto, or a campaign mission with --campaign / --mission);
## every step prints an "OWNERFIX1 ..." line, screenshots go to --screens as ownerfix1-<name>.png and "OWNERFIX1 RESULT: <n> ok, <m> failed" ends the run.
##   cine    (campaign mission 0, from its start) the intro cutscene: DISABLE_INPUT has turned the HUD's input off, the pointer is hidden, a left click and a
##           drag box on the player's own units select nothing; after ENABLE_INPUT the same click selects (retail ScriptActions RW 0x7BC586 / 0x7BC5E9)
##   menus   health bars against the in-game menu, Options, Save and Load over the game (bars visible beforehand, the pixels the menu reaches measured, none of
##           them may show a bar; fails when nothing was measured): ownerfix1-menus-<menu>-open.png
##   store   own units with their health bars on the screen, then the Palantir's powers button: at every health bar's rectangle the pixels change from the
##           bar's own colour (drawn over the world) to the store's (the bars are drawn under the Apt screens, as retail's in-game UI)
##   aa      the same paused frame at every OpenBFMEAntiAliasing x OpenBFMEAnisotropic value: ownerfix1-aa-<aa>-<aniso>.png (tools/render/sharpness.py
##           compares them)
##   aapan   the same while the camera pans (TAA's history blur shows only in motion): ownerfix1-aapan(crop)-<aa>-<aniso>.png
##   healglow a Witch King beside the base, hurt and left to heal: each heal plays GameData's DefaultUnitHealingBuffFxList (the HealingBuff glow)
##   heroes  (campaign) the mission's heroes hurt, healing and levelled, with the FXLists played and crops (a diagnostic: they fight there, so no heal)
##   warchant (campaign) War Chant bought and cast on three orc warriors beside the heroes: its BuffNugget glow (heroes take no War Chant: -HERO)
##   powers  (campaign, lane B2SPELL-1) after the intro the Palantir's powers button opens the store (ownerfix1-powers-open.png) and the first power for
##           sale is bought by a click (ownerfix1-powers-picked.png): the owner's blank BFME2 Good campaign store (10-30)
extends "res://scripts/spell3_player.gd"


func _run(p_game: Node, scenarios: PackedStringArray) -> void:
	game = p_game
	hud = game._hud
	world = game._world
	shell = game._shell
	faction = str(game._faction) if "_faction" in game else ""
	await _run_scenarios(scenarios)


func shot(name: String) -> void:
	game._save_named("ownerfix1-%s" % name)


func input_enabled() -> bool:
	return bool(hud.get_state().get("keys", {}).get("input_enabled", true))


## the local player's units whose pixel is on the screen (away from the edges and the Palantir's lower left corner)
func units_on_screen() -> Array:
	var out: Array = []
	var s := window_size()
	for o in local_units():
		var p := to_pixel(o.x, o.y)
		if p.x > 40 and p.y > 40 and p.x < s.x - 40 and p.y < s.y - 60 and not (p.x < s.x * 0.35 and p.y > s.y * 0.6):
			out.append({"id": int(o.id), "p": p, "template": str(o.template)})
	return out


func _scenario_cine() -> String:
	# the intro's DISABLE_INPUT arrives within the first frames of the mission (MAP ANG Angmar's intro script)
	var waited := 0
	while input_enabled() and waited < 200:
		await game._step(1)
		waited += 1
	if input_enabled():
		return "the intro never disabled the input (no DISABLE_INPUT reached the HUD)"
	print("OWNERFIX1 cine: input disabled at logic frame %d" % world.get_frame())
	var units := units_on_screen()
	if units.is_empty():
		return "no own unit on the screen during the intro"
	var u: Dictionary = units[0]
	await move_to(u.p)
	await game._step(2)
	var cursor: Dictionary = hud.get_cursor_image()
	print("OWNERFIX1 cine: pointer over %s at %s: hidden %s (cursor '%s')" % [u.template, u.p, str(cursor.hidden), str(cursor.name)])
	shot("cine-intro")
	if not bool(cursor.hidden):
		return "the pointer shows during the cutscene (retail Mouse::setVisibility(false))"
	await click(u.p)
	await game._step(3)
	if not hud.get_selection().is_empty():
		return "a click selected %s during the cutscene" % JSON.stringify(hud.get_selection())
	# a drag box over every unit on the screen
	var lo := Vector2(1e9, 1e9)
	var hi := Vector2(-1e9, -1e9)
	for v in units:
		lo = Vector2(minf(lo.x, v.p.x), minf(lo.y, v.p.y))
		hi = Vector2(maxf(hi.x, v.p.x), maxf(hi.y, v.p.y))
	await press(lo - Vector2(30, 30), MOUSE_BUTTON_LEFT, true)
	await move_to((lo + hi) * 0.5)
	await move_to(hi + Vector2(30, 30))
	await press(hi + Vector2(30, 30), MOUSE_BUTTON_LEFT, false)
	await game._step(3)
	if not hud.get_selection().is_empty():
		return "a drag box selected %s during the cutscene" % JSON.stringify(hud.get_selection())
	print("OWNERFIX1 cine: during the cutscene a click and a drag box over %d own units selected nothing" % units.size())
	# the end of the intro: ENABLE_INPUT
	waited = 0
	while not input_enabled() and waited < 3000:
		await game._step(1)
		waited += 1
	if not input_enabled():
		return "the intro never enabled the input again"
	print("OWNERFIX1 cine: input enabled at logic frame %d" % world.get_frame())
	await game._wait_seconds(2.0)
	units = units_on_screen()
	if units.is_empty():
		# the Witch King (the mission's hero; the player's list also holds the spell book object, which is not on the map)
		for o in local_units():
			if str(o.template) == "AngmarWitchking":
				hud.camera_look_at(Vector2(o.x, o.y))
		await game._wait_seconds(3.0)
		units = units_on_screen()
		print("OWNERFIX1 cine: after the intro own units %s, on the screen %s" % [JSON.stringify(local_units().map(func(o): return [o.template, int(o.x), int(o.y)])),
			JSON.stringify(units)])
	if units.is_empty():
		return "no own unit on the screen after the intro"
	u = units[0]
	await move_to(u.p)
	await game._step(2)
	cursor = hud.get_cursor_image()
	if bool(cursor.hidden):
		return "the pointer stays hidden after ENABLE_INPUT"
	# the control: the same drag box as during the cutscene now selects (a click can miss a model at the map's lower edge)
	lo = Vector2(1e9, 1e9)
	hi = Vector2(-1e9, -1e9)
	for v in units:
		lo = Vector2(minf(lo.x, v.p.x), minf(lo.y, v.p.y))
		hi = Vector2(maxf(hi.x, v.p.x), maxf(hi.y, v.p.y))
	print("OWNERFIX1 cine: input enabled %s at the drag" % str(input_enabled()))
	await press(lo - Vector2(30, 30), MOUSE_BUTTON_LEFT, true)
	await move_to((lo + hi) * 0.5)
	await move_to(hi + Vector2(30, 30))
	await press(hi + Vector2(30, 30), MOUSE_BUTTON_LEFT, false)
	await game._step(3)
	shot("cine-after")
	var sel: Array = hud.get_selection()
	print("OWNERFIX1 cine: after the intro the drag box over %d own units selects %s" % [units.size(), JSON.stringify(sel)])
	if sel.is_empty():
		return "the drag box after the cutscene selected nothing (the control path is broken, not only the cutscene's)"
	return ""


## the logic paused over the player's base (the camera still), then every anti-aliasing x anisotropic value applied through the options' own apply_global
## and captured after 2 s (TAA's history settles): ownerfix1-aa-<aa>-<aniso>.png, and a centre crop of the 3D view (no Palantir) ownerfix1-aacrop-*.png
func _scenario_aa() -> String:
	var opts: Node = game._game_options
	if opts == null:
		return "no game options node"
	var base := base_position()
	if base == Vector2():
		var units := local_units()
		if units.is_empty():
			return "nothing of the player's to look at"
		base = Vector2(units[0].x, units[0].y)
	hud.camera_look_at(base)
	await game._wait_seconds(2.0)
	world.set_paused(true)
	await game._wait_seconds(1.0)
	var saved: Dictionary = opts.current()
	var size := window_size()
	var crop := Rect2i(int(size.x * 0.3), int(size.y * 0.15), int(size.x * 0.4), int(size.y * 0.45))
	for aniso in ["off", "16x"]:
		for aa in ["off", "fxaa", "msaa2", "msaa4", "taa"]:
			var v: Dictionary = saved.duplicate()
			v.OpenBFMEAntiAliasing = aa
			v.OpenBFMEAnisotropic = aniso
			opts.apply_global(v)
			await game._wait_seconds(2.0)
			var root: Viewport = game.get_tree().root
			print("OWNERFIX1 aa %s / %s: msaa_3d %d, screen_space_aa %d, use_taa %s, anisotropy %d, scaling %d x %.2f, mipmap bias %.2f" % [aa, aniso, root.msaa_3d,
				root.screen_space_aa, str(root.use_taa), root.anisotropic_filtering_level, root.scaling_3d_mode, root.scaling_3d_scale, root.texture_mipmap_bias])
			shot("aa-%s-%s" % [aa, aniso])
			var img: Image = game._capture_image("aa crop")
			if img != null:
				var path := "%s/ownerfix1-aacrop-%s-%s.png" % [game._screens, aa, aniso]
				img.get_region(crop).save_png(path)
	opts.apply_global(saved)
	world.set_paused(false)
	return ""


## health bars against the store: the HUD's health bars set to always show (the Interface tab's OpenBFMEHealthBars "always"), the camera on units of the
## map (any player's, the bars of a fight as in the owner's screenshot) until some bars lie inside the store's globe, the bars' rectangles and colours read
## from the HUD's decorations of the frame, then the store opened by the Palantir's button: every bar rectangle under the store's opaque globe must show the
## store's pixels, not the bar's colour
func _scenario_store() -> String:
	# out of the mission's cutscenes (DISABLE_INPUT ... ENABLE_INPUT, the HUD hidden by the letterbox): the input on for 5 s in a row
	var calm := 0.0
	var waited := 0.0
	while calm < 5.0 and waited < 240.0:
		await game._wait_seconds(0.5)
		waited += 0.5
		calm = calm + 0.5 if input_enabled() and hud.visible else 0.0
	if calm < 5.0:
		return "the mission's cutscenes did not end within 240 s"
	hud.set_view_options({"health_bar_mode": "always"})
	var s := window_size()
	var globe := Rect2(s.x * 0.3, s.y * 0.25, s.x * 0.4, s.y * 0.55)
	var cands: Array = []
	for pl in world.get_economy().players:
		for o in world.get_player_objects(pl.name):
			if not o.structure:
				cands.append(o)
	var bars: Array = []
	var deco: Dictionary = {}
	for o in cands.slice(0, 12):
		hud.camera_look_at(Vector2(o.x, o.y))
		await game._wait_seconds(2.0)
		world.set_paused(true)
		await game._wait_seconds(0.5)
		deco = hud.get_icon_ui()
		bars.clear()
		for f in deco.get("fills", []):
			if (f.color as Color).a > 0.99 and globe.encloses(f.rect):
				bars.append(f)
		print("OWNERFIX1 store: looking at %s: %d health bars, %d opaque fills inside the globe" % [o.template, int(deco.get("health_bars", 0)), bars.size()])
		if bars.size() >= 3:
			break
		world.set_paused(false)
	if float(deco.get("ui_scale", 1.0)) != 1.0:
		return "the UI scale option is not 100%% (the decorations' rectangles are not where they are drawn)"
	if bars.is_empty():
		world.set_paused(false)
		return "no health bar inside the store's globe (nothing to test against the store)"
	shot("store-closed")
	var before: Image = game._capture_image("store closed")
	var st := await open_store()
	if not st.get("open", false):
		return "the powers button did not open the store (%s)" % st.get("error", "")
	await game._wait_seconds(1.0)
	shot("store-open")
	var after: Image = game._capture_image("store open")
	if before == null or after == null:
		return "no capture (a headless run draws nothing: run windowed)"
	var tested := 0
	var shown := 0
	for b in bars:
		var r: Rect2 = b.rect
		if r.size.x < 3 or r.size.y < 1:
			continue
		tested += 1
		var c: Color = b.color
		var p := Vector2i(int(r.position.x + r.size.x * 0.5), int(r.position.y + r.size.y * 0.5))
		var was: Color = before.get_pixelv(p)
		var now: Color = after.get_pixelv(p)
		var bar_before := absf(was.r - c.r) + absf(was.g - c.g) + absf(was.b - c.b) < 0.06
		var bar_after := absf(now.r - c.r) + absf(now.g - c.g) + absf(now.b - c.b) < 0.06
		print("OWNERFIX1 store: bar %s colour %s: before %s (bar %s), with the store %s (bar %s)" % [r, c, was, str(bar_before), now, str(bar_after)])
		if not bar_before:
			tested -= 1 # not drawn where the HUD said before the store opened (hidden by another bar or a model): proves nothing either way
			continue
		if bar_after:
			shown += 1
	await click(shell_button(int(st.level), "SpellStore.Buttons.ButtonsMain.Accept"))
	await game._wait_seconds(2.0)
	world.set_paused(false)
	if tested == 0:
		return "no health bar lies under the store's globe (move the camera: nothing was tested)"
	if shown > 0:
		return "%d of %d health bars under the store are drawn over it" % [shown, tested]
	print("OWNERFIX1 store: %d health bars under the store's globe, none drawn over it" % tested)
	return ""


## the same as aa while the camera pans (the player scrolling): the look-at target walks along the map's x at a constant speed for 60 drawn frames and the
## capture is taken mid-pan; every value runs the same path: ownerfix1-aapan-<aa>-<aniso>.png and the crops ownerfix1-aapancrop-*.png
func _scenario_aapan() -> String:
	var opts: Node = game._game_options
	var base := base_position()
	if base == Vector2():
		return "no base to pan over"
	var saved: Dictionary = opts.current()
	var size := window_size()
	var crop := Rect2i(int(size.x * 0.3), int(size.y * 0.15), int(size.x * 0.4), int(size.y * 0.45))
	world.set_paused(true)
	var root: Viewport = game.get_tree().root
	var experiments: PackedStringArray = OS.get_environment("OWNERFIX1_AA_EXPERIMENTS").split(",", false)
	for aniso in ["off", "16x"]:
		for aa in ["off", "fxaa", "msaa4", "taa"] + Array(experiments):
			var v: Dictionary = saved.duplicate()
			v.OpenBFMEAntiAliasing = aa if not aa.begins_with("x_") else "off"
			v.OpenBFMEAnisotropic = aniso
			opts.apply_global(v)
			# diagnostic experiments (OWNERFIX1_AA_EXPERIMENTS): x_fsr2_<sharpness> FSR 2 at the native scale; x_taabias_<bias> TAA with a mip bias
			if aa.begins_with("x_fsr2_"):
				root.scaling_3d_mode = Viewport.SCALING_3D_MODE_FSR2
				root.scaling_3d_scale = 1.0
				root.fsr_sharpness = aa.substr(7).to_float()
			elif aa.begins_with("x_taabias_"):
				root.use_taa = true
				root.texture_mipmap_bias = aa.substr(10).to_float()
			hud.camera_look_at(base - Vector2(300, 0))
			await game._wait_seconds(2.0)
			for i in 60:
				hud.camera_look_at(base - Vector2(300, 0) + Vector2(5.0 * i, 0))
				await game.get_tree().process_frame
			shot("aapan-%s-%s" % [aa, aniso])
			var img: Image = game._capture_image("aa pan crop")
			if img != null:
				img.get_region(crop).save_png("%s/ownerfix1-aapancrop-%s-%s.png" % [game._screens, aa, aniso])
			root.scaling_3d_mode = Viewport.SCALING_3D_MODE_BILINEAR
			root.texture_mipmap_bias = 0.0
	opts.apply_global(saved)
	world.set_paused(false)
	return ""


func _fx_counts() -> Dictionary:
	var r: Dictionary = world.get_fx_report()
	return {"played": (r.get("played", {}) as Dictionary).duplicate(), "skipped": (r.get("skipped", {}) as Dictionary).duplicate()}


func _fx_diff(a: Dictionary, b: Dictionary) -> String:
	var out := PackedStringArray()
	for kind in ["played", "skipped"]:
		for k in b[kind]:
			var n := int(b[kind][k]) - int(a[kind].get(k, 0))
			if n > 0:
				out.append("%s %s x%d" % [kind, k, n])
	return ", ".join(out)


## a 512 x 512 capture around a map point (1:1 pixels): ownerfix1-<name>.png
func crop_at(name: String, x: float, y: float) -> void:
	var img: Image = game._capture_image(name)
	if img == null:
		return
	var p := to_pixel(x, y)
	var r := Rect2i(clampi(int(p.x) - 256, 0, img.get_width() - 512), clampi(int(p.y) - 320, 0, img.get_height() - 512), 512, 512)
	img.get_region(r).save_png("%s/ownerfix1-%s.png" % [game._screens, name])


## problem 3 (the owner: no particles around the heroes when they heal etc.): the mission's heroes (script names) moved by a logged test hook to a
## quiet spot (OWNERFIX1_QUIET="x,y", default the map's south middle) so nothing fights them, hurt by half (a logged test hook) and left to heal
## (AutoHealBehavior: StartHealingDelay, then HealingAmount a HealingDelay); then raised a rank (LevelUpFx). After each step the FXLists that played or
## were skipped and the buffs drawn are logged and 512 x 512 crops around the hero taken: ownerfix1-hero-<template>-<step>.png
func _scenario_heroes() -> String:
	var calm := 0.0
	var waited := 0.0
	while calm < 5.0 and waited < 240.0:
		await game._wait_seconds(0.5)
		waited += 0.5
		calm = calm + 0.5 if input_enabled() and hud.visible else 0.0
	var quiet := Vector2(1200, 900)
	var qe := OS.get_environment("OWNERFIX1_QUIET")
	if not qe.is_empty():
		quiet = Vector2(qe.get_slice(",", 0).to_float(), qe.get_slice(",", 1).to_float())
	var fails := PackedStringArray()
	var k := 0
	for script_name in ["Witch King", "Morgomir"]:
		var info: Dictionary = world.debug_script_unit(script_name)
		print("OWNERFIX1 heroes: %s: %s" % [script_name, JSON.stringify(info)])
		var place: Variant = world.debug_script_place_at(script_name, quiet.x + 120.0 * k, quiet.y)
		print("OWNERFIX1 heroes: TEST HOOK %s placed at %s: %s" % [script_name, quiet + Vector2(120.0 * k, 0), str(place)])
		k += 1
		var id := -1
		for o in local_units():
			if absf(float(o.x) - (quiet.x + 120.0 * (k - 1))) < 30.0 and absf(float(o.y) - quiet.y) < 30.0:
				id = int(o.id)
		if id < 0:
			fails.append("%s not found at the quiet spot" % script_name)
			continue
		var d: Dictionary = world.get_object(id)
		var name := str(d.get("template", script_name))
		hud.camera_look_at(Vector2(d.x, d.y))
		await game._wait_seconds(2.0)
		crop_at("hero-%s-idle" % name, d.x, d.y)
		var hp0 := float(d.get("health", 0.0))
		var f1 := _fx_counts()
		print("OWNERFIX1 heroes: %s TEST HOOK damage %.0f of %.0f: %s" % [name, hp0 * 0.5, hp0, JSON.stringify(world.debug_damage_object(id, hp0 * 0.5, 0))])
		var hurt := hp0 * 0.5
		for t in 12:
			await game._wait_seconds(2.0)
			var dd: Dictionary = world.get_object(id)
			hud.camera_look_at(Vector2(dd.x, dd.y))
			print("OWNERFIX1 heroes: %s %ds after: health %.0f, fx %s, buffs drawn %d" % [name, 2 * (t + 1), float(dd.get("health", 0.0)), _fx_diff(f1, _fx_counts()),
				int(world.get_buff_state().get("drawn", 0))])
			if t == 10:
				crop_at("hero-%s-healing" % name, dd.x, dd.y)
		var healed := float(world.get_object(id).get("health", 0.0)) - hurt
		print("OWNERFIX1 heroes: %s healed %.0f in 24 s" % [name, healed])
		if healed <= 0.0:
			fails.append("%s did not heal in 24 s at the quiet spot" % name)
		var f2 := _fx_counts()
		var rank: int = world.debug_set_object_rank(id, int(d.get("rank", 1)) + 1)
		for ms in [100, 300, 700]:
			await game._wait_seconds(0.1 if ms == 100 else (0.2 if ms == 300 else 0.4))
			var de: Dictionary = world.get_object(id)
			crop_at("hero-%s-levelup-%dms" % [name, ms], de.x, de.y)
		print("OWNERFIX1 heroes: %s TEST HOOK rank -> %d: fx %s" % [name, rank, _fx_diff(f2, _fx_counts())])
	return "; ".join(fails)


## problem 3's status glow: War Chant (Angmar's tier 1 power, ModifierList SpellBookWarChant: FX = FX_GenericBuffPersistent60, a BuffNugget of 60 s)
## bought through the Powers screen with points the logged test hook adds, cast from the Palantir's spell book on the mission's two heroes; the buffs
## the HUD then draws (get_buff_state) and 512 x 512 crops around three orc warriors beside the heroes (War Chant buffs no hero): ownerfix1-warchant-before.png / -after.png
func _scenario_warchant() -> String:
	var calm := 0.0
	var waited := 0.0
	while calm < 5.0 and waited < 240.0:
		await game._wait_seconds(0.5)
		waited += 0.5
		calm = calm + 0.5 if input_enabled() and hud.visible else 0.0
	var me: int = game._local_index
	print("OWNERFIX1 warchant: TEST HOOK power points -> %d" % world.debug_add_science_points(me, 10))
	var st := await open_store()
	if not st.get("open", false):
		return "the powers button did not open the store (%s)" % st.get("error", "")
	var idx := -1
	for i in (st.get("sciences", []) as Array).size():
		if str(st.sciences[i]) == "SCIENCE_WarChant":
			idx = i
	if idx < 0:
		return "no War Chant in the store (%s)" % JSON.stringify(st.get("sciences", []))
	await click(shell_button(int(st.level), "SpellStore.Buttons.Spell%d" % (idx + 1)))
	await game._wait_seconds(1.5)
	await click(shell_button(int(st.level), "SpellStore.Buttons.ButtonsMain.Accept"))
	await game._wait_seconds(3.0)
	shot("warchant-bought") # the Palantir's spell book after the purchase
	print("OWNERFIX1 warchant: bought; the spell book slots %s" % JSON.stringify(hud.get_spellbook_state().get("slots", [])))
	var hero := Vector2()
	for o in local_units():
		if str(o.template) == "AngmarWitchking":
			hero = Vector2(o.x, o.y)
	# War Chant buffs no hero (GENERIC_BUFF_RECIPIENT_OBJECT_FILTER: -HERO): three of the player's AngmarOrcWarrior beside the heroes receive it (logged test hook)
	var orcs: Array = []
	for i in 3:
		orcs.append(int(world.create_object("AngmarOrcWarrior", me, hero.x - 30.0 + 30.0 * i, hero.y + 110.0, 0.0)))
	print("OWNERFIX1 warchant: TEST HOOK three AngmarOrcWarrior created: %s" % str(orcs))
	hero = hero + Vector2(0, 110)
	hud.camera_look_at(hero)
	await game._wait_seconds(2.0)
	var orc0: Dictionary = world.get_object(orcs[0])
	crop_at("warchant-before", float(orc0.get("x", hero.x)), float(orc0.get("y", hero.y)))
	var state: Dictionary = hud.get_spellbook_state()
	var k := -1
	for i in (state.get("slot_buttons", []) as Array).size():
		if str(power_of_button(int(state.slot_buttons[i])).get("power", "")) == "SpellBookWarChant":
			k = i
	if k < 0 or str(state.slots[k]) != "_up":
		return "War Chant is not ready in the spell book (slot %d, %s)" % [k, JSON.stringify(state.get("slots", []))]
	var f0 := _fx_counts()
	await click(slot_pixel(k))
	await game._wait_seconds(0.5)
	print("OWNERFIX1 warchant: targeting after the slot click: %s" % JSON.stringify(hud.get_spellbook_state().get("radius_cursor", {})))
	# the heroes stand at the map's south edge, where the cursor is red: the first valid spot within War Chant's radius (100) north of them
	var aim := hero
	var valid := false
	for dy in [0.0, 40.0, 70.0, 95.0]:
		aim = hero + Vector2(0, dy)
		await move_to(to_pixel(aim.x, aim.y))
		await game._step(4)
		var rc: Dictionary = hud.get_spellbook_state().get("radius_cursor", {})
		print("OWNERFIX1 warchant: the cursor at %s: valid %s" % [aim, str(rc.get("valid", false))])
		if bool(rc.get("valid", false)):
			valid = true
			break
	if not valid:
		return "no valid War Chant spot near the heroes"
	await click(to_pixel(aim.x, aim.y))
	await game._wait_seconds(1.5)
	var buffs: Dictionary = world.get_buff_state()
	print("OWNERFIX1 warchant: after the cast: buffs %s" % JSON.stringify(buffs))
	print("OWNERFIX1 warchant: after the cast: slot %s, fx %s" % [str(hud.get_spellbook_state().slots[k]), _fx_diff(f0, _fx_counts())])
	for o in local_units():
		if str(o.template) in ["AngmarWitchking", "AngmarMorgramir", "AngmarOrcWarrior"]:
			var od: Dictionary = world.get_object(int(o.id))
			print("OWNERFIX1 warchant: %s modifiers %s conditions %s" % [o.template, JSON.stringify(od.get("modifiers", od.get("attribute_modifiers", "?"))), str(od.get("conditions", "?"))])
	var orc: Dictionary = world.get_object(orcs[0])
	crop_at("warchant-after", float(orc.get("x", hero.x)), float(orc.get("y", hero.y)))
	shot("warchant-after-full")
	if int(buffs.get("drawn", 0)) == 0:
		return "War Chant drew no buff (BuffNugget FX_GenericBuffPersistent60)"
	return ""


## problem 3's proof: a Witch King made beside the player's base (logged test hook), hurt by half (logged test hook) and left alone to heal
## (AutoHealBehavior: 15 s, then a heal a second); every heal plays GameData's DefaultUnitHealingBuffFxList (FX_DefaultUnitHealBuff: the HealingBuff
## glow) on him. Crops before the heals and while healing: ownerfix1-healglow-hurt.png / -healing.png; the buffs drawn are logged
func _scenario_healglow() -> String:
	var me: int = game._local_index
	var base := base_position()
	if base == Vector2():
		return "no base"
	var at := base + Vector2(150, -60)
	var id := int(world.create_object("AngmarWitchking", me, at.x, at.y, 0.0))
	print("OWNERFIX1 healglow: TEST HOOK AngmarWitchking created: %d" % id)
	if id <= 0:
		return "the hero was not created"
	# alone: he must not go hunting (HealOnlyIfNotInCombat: a hero with a live victim does not heal, RW 0x8554E4)
	print("OWNERFIX1 healglow: TEST HOOK auto-acquire off (every object): %s" % str(world.debug_set_auto_acquire(false)))
	hud.camera_look_at(at)
	await game._wait_seconds(2.0)
	var d: Dictionary = world.get_object(id)
	var hp0 := float(d.get("health", 0.0))
	print("OWNERFIX1 healglow: TEST HOOK damage %.0f: %s" % [hp0 * 0.5, JSON.stringify(world.debug_damage_object(id, hp0 * 0.5, 0))])
	await game._wait_seconds(1.0)
	d = world.get_object(id)
	crop_at("healglow-hurt", d.x, d.y)
	var last := float(d.get("health", 0.0))
	var healing := false
	for i in 180:
		await game._wait_seconds(0.5)
		d = world.get_object(id)
		var h := float(d.get("health", 0.0))
		if i % 10 == 0:
			print("OWNERFIX1 healglow: %.1f s: logic frame %d, health %.0f, state %s" % [0.5 * (i + 1), world.get_frame(), h, JSON.stringify(d).left(300)])
		var buffs: Dictionary = world.get_buff_state()
		if h > last and not healing:
			healing = true
			print("OWNERFIX1 healglow: healing from %.0f to %.0f at %.1f s: buffs %s" % [last, h, 0.5 * (i + 1), JSON.stringify(buffs)])
			await game._wait_seconds(1.0) # the glow's fade-in (TBuff::UpdateFade: 1/12 a drawn frame), and the next heal renews it
			d = world.get_object(id)
			crop_at("healglow-healing", d.x, d.y)
			shot("healglow-healing-full")
			if int(buffs.get("drawn", 0)) == 0 and int(world.get_buff_state().get("drawn", 0)) == 0:
				return "the hero heals but no healing glow is drawn"
			return ""
		last = h
	return "the hero did not heal in 90 s"


func _near(a: Color, b: Color) -> bool:
	return absf(a.r - b.r) + absf(a.g - b.g) + absf(a.b - b.b) < 0.06


## the opaque bar fills of the HUD's decorations whose rectangle lies inside `region` (window pixels)
func _bars_in(region: Rect2) -> Array:
	var out: Array = []
	for f in hud.get_icon_ui().get("fills", []):
		if (f.color as Color).a > 0.99 and region.encloses(f.rect) and f.rect.size.x >= 3 and f.rect.size.y >= 1:
			out.append(f)
	return out


## one menu against the unchanged frame behind it (the world paused): of the bars visible beforehand (their colour at their centre in `closed`), those the menu
## reaches (the pixels 6 beyond both ends of the bar changed) must not show their colour at their centre in `opened`. Fails closed: no bar visible, or none
## reached, is a failure, never a pass
func _measure_menu(menu: String, closed: Image, opened: Image, bars: Array) -> String:
	var visible_before := 0
	var reached := 0
	var over := 0
	for b in bars:
		var r: Rect2 = b.rect
		var c: Color = b.color
		var p := Vector2i(int(r.position.x + r.size.x * 0.5), int(r.position.y + r.size.y * 0.5))
		if not _near(closed.get_pixelv(p), c):
			continue
		visible_before += 1
		var changed := 0
		for q in [Vector2i(int(r.position.x) - 6, p.y), Vector2i(int(r.end.x) + 6, p.y)]:
			var d := closed.get_pixelv(q)
			var e := opened.get_pixelv(q)
			if absf(d.r - e.r) + absf(d.g - e.g) + absf(d.b - e.b) > 0.1:
				changed += 1
		if changed < 2:
			continue
		reached += 1
		if _near(opened.get_pixelv(p), c):
			over += 1
	print("OWNERFIX1 menus %s: %d bar fills visible beforehand, %d of them under the menu, %d drawn over it" % [menu, visible_before, reached, over])
	if visible_before == 0:
		return "%s: no health bar was visible before the menu opened (nothing to measure)" % menu
	if reached == 0:
		return "%s: the menu reached none of the %d visible bars (nothing was measured)" % [menu, visible_before]
	if over > 0:
		return "%s: %d of %d health bar fills under the menu are drawn over it" % [menu, over, reached]
	return ""


## problem 2 over every menu the player opens in a game: the in-game menu (QuitMenu.apt), Options, Save and Load (SaveLoad.apt over the game), each measured by
## _measure_menu against the same paused frame with its bars. The camera first goes to units whose bars lie in the middle of the screen
func _scenario_menus() -> String:
	var calm := 0.0
	var waited := 0.0
	while calm < 5.0 and waited < 240.0:
		await game._wait_seconds(0.5)
		waited += 0.5
		calm = calm + 0.5 if input_enabled() and hud.visible else 0.0
	if calm < 5.0:
		return "the mission's cutscenes did not end within 240 s"
	hud.set_view_options({"health_bar_mode": "always"})
	var s := window_size()
	var middle := Rect2(s.x * 0.3, s.y * 0.25, s.x * 0.4, s.y * 0.55)
	var cands: Array = []
	for pl in world.get_economy().players:
		for o in world.get_player_objects(pl.name):
			if not o.structure:
				cands.append(o)
	var bars: Array = []
	for o in cands.slice(0, 16):
		hud.camera_look_at(Vector2(o.x, o.y))
		await game._wait_seconds(2.0)
		world.set_paused(true)
		await game._wait_seconds(0.5)
		bars = _bars_in(middle)
		print("OWNERFIX1 menus: looking at %s: %d bar fills in the middle of the screen" % [o.template, bars.size()])
		if bars.size() >= 6:
			break
		world.set_paused(false)
	if bars.size() < 6:
		world.set_paused(false)
		return "no spot with health bars in the middle of the screen"
	await game._wait_seconds(0.5)
	var closed: Image = game._capture_image("menus closed")
	if closed == null:
		return "no capture (a headless run draws nothing: run windowed)"
	shot("menus-closed")
	var fails := PackedStringArray()
	for menu in ["quit", "options", "save", "load"]:
		game._toggle_quit_menu()
		await game._wait_seconds(1.5)
		if not game._quit_open:
			fails.append("%s: the in-game menu did not open" % menu)
			continue
		var pops := 0
		if menu == "options":
			game._quit_menu_action("QuitMenuOptions")
			pops = 1
		elif menu == "save":
			game._quit_menu_action("QuitMenuSave", "2")
			pops = 1
		elif menu == "load":
			game._quit_menu_action("QuitMenuLoad", "2")
			pops = 1
		await game._wait_seconds(2.0)
		shot("menus-%s-open" % menu)
		var opened: Image = game._capture_image("menus " + menu)
		var r := _measure_menu(menu, closed, opened, bars)
		if r != "":
			fails.append(r)
		for i in pops:
			game._shell.shell_pop()
			await game._wait_seconds(1.0)
		game._close_quit_menu()
		await game._wait_seconds(1.0)
		world.set_paused(true)
		await game._wait_seconds(0.5)
	world.set_paused(false)
	return "; ".join(fails)


func _scenario_powers() -> String:
	var waited := 0.0
	while not input_enabled() and waited < 240.0: # the mission's intro gives the input back first
		await game._wait_seconds(0.5)
		waited += 0.5
	await game._wait_seconds(2.0)
	var st := await open_store()
	if not st.get("open", false):
		return "the powers button did not open the store (%s)" % st.get("error", "")
	shot("powers-open")
	print("OWNERFIX1 powers: the store's states %s, points %d" % [JSON.stringify(st.get("states", [])), int(st.get("points", -1))])
	var fails: Array = await buy_all(st)
	shot("powers-picked")
	return "" if fails.is_empty() else "; ".join(fails)
