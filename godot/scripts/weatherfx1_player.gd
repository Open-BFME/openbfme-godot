## Lane WEATHERFX-1: the weather of a real skirmish (game.gd --auto through the menus) for the retail check and the Original / Enhanced captures. game.gd
## runs it after the start with --weatherfx1=<scenario>[,...] (with --auto, --map, --faction); --weatherfx-look=original|enhanced sets Options "Look"
## for the whole run (the same seed and frames give the same game: the two looks' shots pair up). Every step prints a "WEATHERFX1 ..." line,
## screenshots go to --screens as weatherfx1-<scenario>-<look>-<name>.png and "WEATHERFX1 RESULT: <n> ok, <m> failed" ends the run.
##   snow      a map whose map.ini snows (map mp withered heath): the retail snow is drawn (the snow manager draws, particles > 0, the Snow node shown)
##   rain      Isengard's Freezing Rain bought and cast through the spell book (power points by the logged test hook): the logic weather RAINY, the
##             snow manager's spell ramp draws the drops, the rain sound starts; the run waits for a lightning (the cloud effect's LightningFX)
##   darkness  Mordor's Darkness: the logic weather CLOUDY, the cloud effect darkens (phase 1 .. 2) and relights the terrain
##   menu      (lane WEATHERFX-2, the owner's "washout on the menus with the snow") after a weather scenario (--weatherfx1=snow,menu): the game is left
##             by input (Esc, Exit, Yes) to the score screen, then its Continue (the lobby) and its way back to the main menu; on each screen the Enhanced weather must be gone (no
##             WeatherFX node, every weather shader global 0, the tone mapping's AgX at its defaults) and the screen drawn the same in the Original and the
##             Enhanced look (weather_readability.gd screens_match): "WEATHERFX1 MENU <screen> ok|FAIL ..."
##   panels    (lane WEATHERFX-2, the owner's pause / powers shots) the pause menu and the powers store over the playfield, each shot in both looks
##             (weatherfx2-panels-<panel>-<look>.png); lane INGAMEBG-1: each must sit on the in-game background, the battlefield dimmed
##             (weather_readability.gd panel_backdrop): "WEATHERFX1 PANEL <panel> <look> ok|FAIL ..."
## --weatherfx-seconds=<s> (default 40) how long each scenario watches the weather after the cast.
## Every scenario (with the enhanced look) then checks READABILITY (weather_readability.gd, the owner 2026-10-29: "weather never hurts
## readability"): the same frame drawn with the Original look and with the Enhanced weather, three times each, alternating; the medians of the
## playfield's mean luma, contrast and detail and of the boxes around the units on screen must stay in the module's BANDS of the Original (the
## fine detail against the Enhanced look without weather, a third sample: see weather_readability.gd):
## "WEATHERFX1 READABILITY <scenario> ok|FAIL ratios {...}" (a FAIL fails the scenario).
## Every scenario prints "WEATHERFX1 HASH <scenario> frame F hash H" at the end of its watch: the two looks' runs must print the same (the weather is
## presentation only).
## --weatherfx-measure (with the enhanced look) then measures the GPU time of the frame (RenderingServer's measured render time of the viewport) with no
## Enhanced weather effect, all of them and each alone: interleaved windows of 60 frames, 4 rounds, the median per setting:
## "WEATHERFX1 COST <setting> gpu <ms> extra <ms over none>".
extends "res://scripts/play1_player.gd"

var look := "original"
var seconds := 40.0
var measure := false # --weatherfx-measure: after the watch, the GPU time of each Enhanced effect (see cost())


func _run(p_game: Node, scenarios: PackedStringArray) -> void:
	game = p_game
	hud = game._hud
	world = game._world
	shell = game._shell
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--weatherfx-look="):
			look = arg.substr(17)
		elif arg.begins_with("--weatherfx-seconds="):
			seconds = float(arg.substr(20))
		elif arg == "--weatherfx-measure":
			measure = true
	# (tools/qa/test_scenario_players.py runs this player against a fake game without Options: no look to set there)
	if game.get("_game_options") != null:
		game._game_options.apply_look(look)
		print("WEATHERFX1 look %s (enhanced weather %s)" % [look, WeatherFXScript().look_enhanced])
	await _run_scenarios(scenarios)


func WeatherFXScript() -> GDScript:
	return load("res://scripts/weather_fx.gd")


func shot(name: String) -> void:
	game._save_named("weatherfx1-%s-%s" % [name, look])


func base_position() -> Vector2:
	for o in world.get_player_objects(game._local_name):
		if o.commandcenter:
			return Vector2(o.x, o.y)
	return Vector2()


func state_line(tag: String) -> Dictionary:
	var wr: Dictionary = world.get_weather_report()
	var fx: Dictionary = world.get_weather_fx_state()
	var snow: Node = world.get_node_or_null("Weather/Snow")
	var efx: Dictionary = game._weather_fx.report() if game._weather_fx != null else {}
	print("WEATHERFX1 %s frame %d: weather %d snow draw %s phase %d particles %d node %s | cloud phase %d counter %d lightning %s fx %d relights %d | sound weather %d | enhanced %s" % [
		tag, world.get_frame(), int(fx.get("weather", -1)), fx.get("snow_draw", false), int(wr.get("snow", {}).get("phase", -1)),
		int(wr.get("snow", {}).get("particles", 0)), snow.visible if snow else "none", int(wr.cloud.phase), int(wr.cloud.counter),
		wr.cloud.lightning_on, int(wr.cloud.lightning_fx_played), int(wr.cloud.terrain_relights), int(wr.sound.weather),
		JSON.stringify({"active": efx.get("active", false), "rain": snappedf(efx.get("rain", 0.0), 0.01), "wet": snappedf(efx.get("wet", 0.0), 0.01),
			"snow": snappedf(efx.get("snow", 0.0), 0.01), "cover": snappedf(efx.get("cover", 0.0), 0.01), "gloom": snappedf(efx.get("gloom", 0.0), 0.01),
			"strikes": efx.get("strikes", 0)})])
	return {"report": wr, "fx": fx}


func watch(name: String, total: float, shots_at: Array) -> Dictionary:
	var t := 0.0
	var seen := {"snow_draw": false, "particles": 0, "rainy": false, "cloudy": false, "cloud_phase": 0, "fx": 0, "relights": 0, "sound": 0}
	var next_shot := 0
	while t < total:
		await game._wait_seconds(1.0)
		t += 1.0
		var s := state_line("%s t %.0f" % [name, t])
		var wr: Dictionary = s.report
		seen.snow_draw = seen.snow_draw or bool(s.fx.get("snow_draw", false))
		seen.particles = maxi(seen.particles, int(wr.snow.particles))
		seen.rainy = seen.rainy or int(s.fx.get("weather", 0)) == 2
		seen.cloudy = seen.cloudy or int(s.fx.get("weather", 0)) == 1
		seen.cloud_phase = maxi(seen.cloud_phase, int(wr.cloud.phase))
		seen.fx = maxi(seen.fx, int(wr.cloud.lightning_fx_played))
		seen.relights = maxi(seen.relights, int(wr.cloud.terrain_relights))
		seen.sound = maxi(seen.sound, int(wr.sound.weather))
		if next_shot < shots_at.size() and t >= float(shots_at[next_shot]):
			shot("%s-%02d" % [name, int(shots_at[next_shot])])
			next_shot += 1
	return seen


func _gpu_window(frames: int) -> float:
	var vp := game.get_viewport().get_viewport_rid()
	var total := 0.0
	await game.get_tree().process_frame
	await game.get_tree().process_frame
	for i in frames:
		await RenderingServer.frame_post_draw
		total += RenderingServer.viewport_get_measured_render_time_gpu(vp)
	return total / frames


func cost(scenario: String, effects: Array) -> void:
	if not measure or game._weather_fx == null:
		return
	var fx: Node = game._weather_fx
	var keep: Dictionary = fx.effects.duplicate()
	RenderingServer.viewport_set_measure_render_time(game.get_viewport().get_viewport_rid(), true)
	var settings := ["none", "all"] + effects
	var samples := {}
	for s in settings:
		samples[s] = []
	for round in 8:
		for s in settings:
			for e in keep:
				fx.effects[e] = s == "all" or s == e or (s == "ripples" and e == "wet") # the ripples are drawn in the wet ground's puddles
			samples[s].append(await _gpu_window(45))
	# a still per setting (the same camera, seconds apart): no effect, all, each alone
	for s in settings:
		for e in keep:
			fx.effects[e] = s == "all" or s == e or (s == "ripples" and e == "wet")
		await game._step(8)
		shot("%s-effect-%s" % [scenario, s])
		await game._step(2)
	fx.effects = keep
	# the host's GPU shares memory with a loaded CPU: the median and the lower quartile of the 8 windows (the quartile is the least disturbed)
	var med := {}
	var q1 := {}
	for s in settings:
		var a: Array = samples[s]
		a.sort()
		med[s] = (a[3] + a[4]) * 0.5
		q1[s] = (a[1] + a[2]) * 0.5
	for s in settings:
		print("WEATHERFX1 COST %s gpu %.3f extra %.3f ms (q1 extra %.3f) (%s)" % [s, med[s], med[s] - med.none, q1[s] - q1.none,
			", ".join(samples[s].map(func(v): return "%.2f" % v))])


## the drawn frame of a moment of the scenario: under the movie writer it is the movie's frame (the captures cut their clips there)
## the readability measures, loaded when used (the scenario-runner regression copies only the players)
func readability_module() -> GDScript:
	return load("res://scripts/weather_readability.gd")


## the readability check (see the header): the playfield and the units, Original look against the Enhanced weather, on the same game moment
func readability(scenario: String) -> String:
	if look != "enhanced":
		return ""
	var Readability := readability_module()
	var units: Array = []
	for o in world.get_player_objects(game._local_name):
		units.append(Vector2(o.x, o.y))
	var samples := {"original": [], "enhanced": [], "look_only": []}
	var fx: Node = game._weather_fx
	var keep: Dictionary = fx.effects.duplicate()
	for round in 3:
		for lk in ["original", "enhanced", "look_only"]:
			# look_only: the Enhanced look with every weather effect off (how much of a change is the tone mapping's, not the weather's)
			game._game_options.apply_look("original" if lk == "original" else "enhanced")
			for e in keep:
				fx.effects[e] = keep[e] and lk != "look_only"
			await game._step(4)
			await RenderingServer.frame_post_draw
			var img: Image = game.get_viewport().get_texture().get_image()
			var px: Array = []
			for u in units:
				px.append(to_pixel(u.x, u.y))
			samples[lk].append(Readability.measure(img, px))
			if round == 0:
				shot("%s-readability-%s" % [scenario, lk])
	game._game_options.apply_look(look)
	fx.effects = keep
	var med := {}
	for lk in samples:
		med[lk] = _median_measure(samples[lk])
	var r: Dictionary = Readability.compare(med.original, med.enhanced, med.look_only)
	var ratios := {}
	for k in r.ratios:
		ratios[k] = snappedf(r.ratios[k], 0.001)
	var lo: Dictionary = Readability.compare(med.original, med.look_only)
	var lratios := {}
	for k in lo.ratios:
		lratios[k] = snappedf(lo.ratios[k], 0.001)
	print("WEATHERFX1 READABILITY %s %s ratios %s detail_vs_original %.3f boxes %d (the Enhanced look without weather: %s) original %s enhanced %s" % [scenario,
		"ok" if r.fails.is_empty() else "FAIL " + "; ".join(r.fails), JSON.stringify(ratios), r.info.detail_vs_original, int(med.enhanced.boxes), JSON.stringify(lratios),
		JSON.stringify(med.original.playfield), JSON.stringify(med.enhanced.playfield)])
	return "" if r.fails.is_empty() else "readability: " + "; ".join(r.fails)


func _median_measure(list: Array) -> Dictionary:
	var out := {"playfield": {}, "units": {}, "boxes": 0}
	for part in ["playfield", "units"]:
		for k in list[0][part]:
			var v: Array = list.map(func(m): return float(m[part][k]))
			v.sort()
			out[part][k] = v[v.size() / 2]
	out.boxes = list[0].boxes
	return out


func print_hash(scenario: String) -> void:
	print("WEATHERFX1 HASH %s frame %d hash %d" % [scenario, world.get_frame(), world.get_state_hash()])


func mark(what: String) -> void:
	print("WEATHERFX1 MARK %s drawn frame %d" % [what, Engine.get_frames_drawn()])


func cast(power: String, buy: Array) -> String:
	mark("cast")
	var me: int = game._local_index
	var pts: int = world.debug_add_science_points(me, 500)
	print("WEATHERFX1 TEST HOOK: player %d given 500 power points -> %d" % [me, pts])
	for s in buy:
		var r = world.purchase_science(me, s)
		print("WEATHERFX1 bought %s: %s" % [s, r])
		await game._wait_seconds(0.4)
	var at := base_position() + Vector2(150, -150)
	for b in world.get_spell_bar(me).get("buttons", []):
		if String(b.power) == power and b.usable:
			var ok: bool = world.spell_bar_press(me, int(b.index))
			if ok and b.needs_position:
				ok = world.spell_bar_click_world(me, at.x, at.y)
			print("WEATHERFX1 cast %s at %s: %s" % [power, at, ok])
			return "" if ok else "the cast of %s was refused" % power
	return "%s is not usable on the spell bar (%s)" % [power, JSON.stringify(world.get_spell_bar(me))]


## the view: from the local base a little toward the middle of the map (the mean of the objects), where the map is, not its edge
func frame_view() -> void:
	var base := base_position()
	var mid := Vector2.ZERO
	var n := 0
	for id in world.get_object_ids():
		var o: Dictionary = world.get_object(id)
		if o.get("ok", false):
			mid += Vector2(o.x, o.y)
			n += 1
		if n >= 400:
			break
	mid = mid / maxf(n, 1)
	var dir := (mid - base).normalized() if (mid - base).length() > 1.0 else Vector2(1, -1).normalized()
	hud.camera_look_at(base + dir * 120.0) # the base and its workers stay in the playfield (the readability check measures them)
	await game._step(30)


func _scenario_snow() -> String:
	await frame_view()
	var s := state_line("snow start")
	if not bool(s.fx.get("map_snowing", false)):
		return "the map does not snow (map.ini IsSnowing): %s" % [s.report.get("files", [])]
	mark("watch")
	var seen := await watch("snow", seconds, [2, 10, 30, 60])
	print_hash("snow")
	var fails := []
	var rd := await readability("snow")
	if not rd.is_empty():
		fails.append(rd)
	await cost("snow", ["snow", "snowcover", "mist", "clouds"])
	if not seen.snow_draw or seen.particles <= 0:
		fails.append("the retail snow was not drawn: %s" % JSON.stringify(seen))
	return "; ".join(fails)


func _scenario_rain() -> String:
	await frame_view()
	await game._wait_seconds(2.0)
	state_line("rain before")
	shot("rain-before")
	var err := await cast("SpellBookFreezingRain", ["SCIENCE_PalantirVision", "SCIENCE_WildMenAllies", "SCIENCE_FreezingRain"])
	if not err.is_empty():
		return err
	var seen := await watch("rain", seconds, [3, 8, 15, 25, 40, 60])
	print_hash("rain")
	var fails := []
	var rd := await readability("rain")
	if not rd.is_empty():
		fails.append(rd)
	await cost("rain", ["rain", "wet", "ripples", "storm", "clouds", "mist", "wind"])
	if not seen.rainy:
		fails.append("the logic weather never became RAINY")
	if not seen.snow_draw or seen.particles <= 0:
		fails.append("the retail rain drops were not drawn")
	if seen.sound != 2:
		fails.append("the rain sound did not follow the weather")
	print("WEATHERFX1 rain seen %s" % JSON.stringify(seen))
	return "; ".join(fails)


func _scenario_darkness() -> String:
	await frame_view()
	await game._wait_seconds(2.0)
	state_line("darkness before")
	shot("darkness-before")
	var err := await cast("SpellBookDarkness", ["SCIENCE_EyeofSauron", "SCIENCE_Barricade", "SCIENCE_Darkness"])
	if not err.is_empty():
		return err
	var seen := await watch("darkness", seconds, [3, 10, 20, 30, 45])
	print_hash("darkness")
	var fails := []
	var rd := await readability("darkness")
	if not rd.is_empty():
		fails.append(rd)
	await cost("darkness", ["clouds", "mist"])
	if not seen.cloudy:
		fails.append("the logic weather never became CLOUDY")
	if seen.cloud_phase < 1 or seen.relights <= 0:
		fails.append("the cloud effect did not darken / relight the terrain")
	print("WEATHERFX1 darkness seen %s" % JSON.stringify(seen))
	return "; ".join(fails)


## lane WEATHERFX-2: what of the Enhanced weather is left in the game's scene (the node, its shader globals, the tone mapping): [] when nothing
func weather_leftovers() -> Array:
	var out: Array = []
	var fx = game.get("_weather_fx")
	if fx != null and is_instance_valid(fx):
		out.append("a WeatherFX node is still in the scene (active %s)" % str(fx.report().get("active", false)))
	for g in WeatherFXScript().GLOBALS:
		var v = RenderingServer.global_shader_parameter_get(g)
		if v != null and ((v is float and absf(v) > 0.0) or (v is Vector2 and v != Vector2.ZERO)):
			out.append("shader global %s = %s" % [g, str(v)])
	var env: Environment = game._env.environment if game.get("_env") != null else null
	if env != null and (not is_equal_approx(env.tonemap_agx_white, WeatherFXScript().AGX_WHITE) or not is_equal_approx(env.tonemap_agx_contrast, WeatherFXScript().AGX_CONTRAST)):
		out.append("the tone mapping keeps the snow grade (AgX white %.2f contrast %.2f)" % [env.tonemap_agx_white, env.tonemap_agx_contrast])
	return out


## lane WEATHERFX-2: one screen (no game on it) in both looks: Original, Enhanced, Original again, each 6 frames after the switch; the Enhanced frame
## against the two Original ones (the menus animate: a pixel counts as changed only if it differs from both)
func menu_check(screen: String) -> String:
	var Readability := readability_module()
	var imgs := {}
	for lk in ["original", "enhanced", "original2"]:
		game._game_options.apply_look("enhanced" if lk == "enhanced" else "original")
		await game._step(6)
		await RenderingServer.frame_post_draw
		imgs[lk] = game.get_viewport().get_texture().get_image()
		if lk != "original2":
			game._save_named("weatherfx2-%s-%s" % [screen, lk])
	game._game_options.apply_look(look)
	var fails := weather_leftovers()
	var m: Dictionary = Readability.screens_match(imgs.original, imgs.enhanced, imgs.original2)
	if not m.ok:
		fails.append(m.why)
	print("WEATHERFX1 MENU %s %s %s" % [screen, "ok" if fails.is_empty() else "FAIL " + "; ".join(fails), JSON.stringify(m.stats)])
	return "" if fails.is_empty() else "%s: %s" % [screen, "; ".join(fails)]


func _scenario_menu() -> String:
	var fails := []
	shot("menu-ingame")
	await game._press_escape()
	await game._wait_seconds(2.0)
	if not await game._click_quit("ExitMission"):
		return "no ExitMission button in the quit menu"
	await game._wait_seconds(1.0)
	if not await game._click_quit("Yes"):
		return "no confirmation of the Exit"
	var w := 0.0
	while game._state != game.State.SCORE and w < 15.0:
		await game._wait_seconds(0.25)
		w += 0.25
	if game._state != game.State.SCORE:
		return "the Exit did not reach the score screen"
	await game._wait_seconds(4.0)
	var r := await menu_check("score")
	if not r.is_empty():
		fails.append(r)
	var lvl: int = shell.shell_top_level()
	for attempt in 6:
		if shell.find_button(lvl, "Main.MainButtons.Skip").found:
			await game._click_button(lvl, "Main.MainButtons.Skip")
			await game._wait_seconds(1.0)
		await game._click_button(lvl, "Main.MainButtons.Continue")
		await game._wait_seconds(1.5)
		if game._state != game.State.SCORE:
			break
	w = 0.0
	while game._state != game.State.MENU and w < 15.0:
		await game._wait_seconds(0.25)
		w += 0.25
	if game._state != game.State.MENU:
		fails.append("Continue did not return to the main menu")
		return "; ".join(fails)
	await game._wait_seconds(4.0)
	r = await menu_check("lobby")
	if not r.is_empty():
		fails.append(r)
	# the screen Continue restores (Skirmish), then its way back to the main menu, where the front-end background fades in again (RW 0x815507)
	for attempt in 3:
		var st: PackedStringArray = shell.shell_stack()
		if st.size() > 0 and st[-1] == "MainMenu.apt":
			break
		await game._walk_back("weatherfx2")
		await game._wait_seconds(2.0)
	var top: PackedStringArray = shell.shell_stack()
	if top.is_empty() or top[-1] != "MainMenu.apt":
		fails.append("no way back to the main menu (%s)" % str(top))
		return "; ".join(fails)
	await game._wait_seconds(5.0)
	r = await menu_check("mainmenu")
	if not r.is_empty():
		fails.append(r)
	return "; ".join(fails)


## lane WEATHERFX-2 (the owner's pause / powers washout shots): the in-game panels over the playfield, each in both looks. Lane INGAMEBG-1: each panel
## must sit on the in-game background, which dims the battlefield (weather_readability.gd panel_backdrop against the same look's frame with no panel):
## "WEATHERFX1 PANEL <panel> <look> ok|FAIL <why> <stats>"
func _scenario_panels() -> String:
	var Readability := readability_module()
	var fails := []
	var bare := {}
	await frame_view()
	await game._wait_seconds(2.0)
	for lk in ["original", "enhanced"]:
		bare[lk] = await panel_shot("none", lk)
	await game._press_escape()
	await game._wait_seconds(3.0)
	print("WEATHERFX1 panels pause menu: %s" % JSON.stringify(shell.quit_menu({"kind": "state"})))
	for lk in ["original", "enhanced"]:
		fails.append_array(panel_check(Readability, "pause", lk, bare[lk], await panel_shot("pause", lk)))
	await game._click_quit("Resume")
	await game._wait_seconds(6.0) # the in-game background's own fade out (Background.apt _hide_IG_BG)
	var magic := palantir_button("PalantirButtons.Buttons.PlayerMagic")
	if magic.x < 0:
		return "no PlayerMagic button"
	await click(magic)
	await game._wait_seconds(4.0)
	for lk in ["original", "enhanced"]:
		fails.append_array(panel_check(Readability, "powers", lk, bare[lk], await panel_shot("powers", lk)))
	game._game_options.apply_look(look)
	return "; ".join(fails)


func panel_shot(panel: String, lk: String) -> Image:
	game._game_options.apply_look(lk)
	await game._step(6)
	await RenderingServer.frame_post_draw
	var img: Image = game.get_viewport().get_texture().get_image()
	game._save_named("weatherfx2-panels-%s-%s" % [panel, lk])
	return img


func panel_check(Readability: GDScript, panel: String, lk: String, bare: Image, img: Image) -> Array:
	var m: Dictionary = Readability.panel_backdrop(bare, img, panel)
	print("WEATHERFX1 PANEL %s %s %s %s" % [panel, lk, "ok" if m.ok else "FAIL " + m.why, JSON.stringify(m.stats)])
	return [] if m.ok else ["%s %s: %s" % [panel, lk, m.why]]
