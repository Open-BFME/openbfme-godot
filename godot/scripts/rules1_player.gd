## Lane RULES-1: the proof run of the Legendary Heroes rule (game.gd --auto --rules-shot=<factor> --screens=<dir>). The lobby part (the Rules tab before and
## after the factor is chosen) is in game.gd's lobby step; here, in the game: the same hero for the local player (the rule applies: a playable side) and for
## the neutral player (it does not), side by side, the same 1400 damage dealt to both through the test hook, the local one selected and the pointer over
## the other so both health bars show. Prints "RULES1 <what>: ok / FAIL <why>" and "RULES1 RESULT: <n> ok, <m> failed"; screenshot rules1-heroes.png.
extends RefCounted

var game: Node
var hud: Node
var world: Node
var helper # scripts/hud5_player.gd: the camera and selection helpers
var ok_count := 0
var fail_count := 0


func run(p_game: Node, hero_template: String) -> void:
	game = p_game
	hud = game._hud
	world = game._world
	helper = load("res://scripts/hud5_player.gd").new()
	helper.game = game
	helper.hud = hud
	helper.world = world
	var r: String = await _heroes(hero_template)
	if r == "":
		ok_count += 1
		print("RULES1 heroes: ok")
	else:
		fail_count += 1
		print("RULES1 heroes: FAIL %s" % r)
	print("RULES1 RESULT: %d ok, %d failed" % [ok_count, fail_count])


func _heroes(template: String) -> String:
	var own: Array = world.get_player_objects(game._local_name)
	if own.is_empty():
		return "the local player owns nothing"
	var base := Vector2(own[0].x, own[0].y)
	var me: int = helper.local_index()
	var at := base + Vector2(220, 0)
	var legendary: int = world.create_object(template, me, at.x, at.y)
	var plain: int = world.create_object(template, 0, at.x + 90, at.y) # player 0: the neutral player (not a playable side)
	if legendary <= 0 or plain <= 0:
		return "heroes not made: %d %d" % [legendary, plain]
	await game._step(10)
	var a: Dictionary = world.get_object(legendary)
	var b: Dictionary = world.get_object(plain)
	print("RULES1 heroes: legendary %s health %s / %s, plain health %s / %s" % [template, str(a.get("health")), str(a.get("max_health")), str(b.get("health")), str(b.get("max_health"))])
	var da: Dictionary = world.debug_damage_object(legendary, 1400.0, 0)
	var db: Dictionary = world.debug_damage_object(plain, 1400.0, 0)
	if not da.get("ok", false) or not db.get("ok", false):
		return "the damage hook refused: %s %s" % [str(da), str(db)]
	a = world.get_object(legendary)
	b = world.get_object(plain)
	print("RULES1 heroes after 1400 damage: legendary %s / %s, plain %s / %s" % [str(a.get("health")), str(a.get("max_health")), str(b.get("health")), str(b.get("max_health"))])
	hud.camera_set_height(170.0)
	await helper.look(at.x + 45, at.y)
	await game._step(10)
	if not await helper.select_object(a):
		return "the legendary hero could not be selected"
	await helper.look(at.x + 45, at.y)
	hud.inject_mouse_move(hud.world_to_pixel(Vector2(b.x, b.y)) + Vector2(0, -25)) # the pointer over the plain hero: its bar shows too
	await game._step(15)
	# the numbers (a proof overlay, not game UI): each hero's health from the world, over its head
	var layer := CanvasLayer.new()
	layer.layer = 92
	game.add_child(layer)
	for pair in [[a, "Legendary 4x (Men)"], [b, "Plain 1x (neutral)"]]:
		var o: Dictionary = pair[0]
		var label := Label.new()
		label.text = "%s\nHP %d / %d" % [pair[1], int(o.health), int(o.max_health)]
		label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		label.add_theme_font_size_override("font_size", 20)
		label.add_theme_color_override("font_color", Color(1.0, 0.95, 0.6))
		label.add_theme_color_override("font_outline_color", Color.BLACK)
		label.add_theme_constant_override("outline_size", 6)
		label.size = Vector2(240, 60)
		label.position = hud.world_to_pixel(Vector2(o.x, o.y)) + Vector2(-120, -260)
		layer.add_child(label)
	await game._step(3)
	game._save_named("rules1-heroes")
	# the bars are small at the game's scale: the part around the two heroes, enlarged 3 x
	var image: Image = game._capture_image("rules1-heroes-zoom")
	if image != null and not game._screens.is_empty():
		var pa: Vector2 = hud.world_to_pixel(Vector2(a.x, a.y))
		var pb: Vector2 = hud.world_to_pixel(Vector2(b.x, b.y))
		var c := (pa + pb) * 0.5
		var w := maxf(absf(pb.x - pa.x) + 260.0, 420.0)
		var r := Rect2i(int(c.x - w * 0.5), int(c.y - 270.0), int(w), 340).intersection(Rect2i(Vector2i.ZERO, image.get_size()))
		var crop := image.get_region(r)
		crop.resize(crop.get_width() * 3, crop.get_height() * 3, Image.INTERPOLATE_NEAREST)
		var path: String = game._screens + "/rules1-heroes-zoom.png"
		print("GAME screenshot %s -> %s" % [path, error_string(crop.save_png(path))])
	if float(a.get("max_health", 0)) != float(game._rules_shot) * float(b.get("max_health", 0)):
		return "max health %s is not %d x %s" % [str(a.get("max_health")), game._rules_shot, str(b.get("max_health"))]
	return ""
