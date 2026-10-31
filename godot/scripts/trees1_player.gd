## Lane TREES1: the map's trees in the real client. game.gd runs it after the start with --trees1=<scenario>[,...] (with --auto and --faction); screenshots
## go to --screens as trees1-<name>.png; every scenario prints "TREES1 <scenario>: ok" or "TREES1 <scenario>: FAIL <why>" and "TREES1 RESULT: <n> ok,
## <m> failed" ends the run. The scenarios place their objects with the logged test hook create_object and order them like a player (order_move).
##   harvest  a built lumber mill of the local faction (IsengardLumberMill / MordorLumberMill) 160 units from the forest nearest the local base: its
##            workers fell trees and take their lumber (the logic's trees: toppled, value down) and the player's money rises; the camera on the forest.
##            FAIL: no tree felled or no money in 60 s
##   crush    a crusher (a MACHINE siege engine or a crusher level 2 unit) driven through the forest nearest the local base: the trees it walks into fall
##            away from it (RW 0x6858BE). FAIL: no tree toppled
extends "res://scripts/scenario_player.gd"

var game: Node
var hud: Node
var world: Node


func _run(p_game: Node, scenarios: PackedStringArray) -> void:
	game = p_game
	hud = game._hud
	world = game._world
	await _run_scenarios(scenarios)


func _seconds(s: float) -> void:
	await game.get_tree().create_timer(s).timeout


func _local_base() -> Vector2:
	for o in world.get_player_objects(game._local_name):
		if o.get("commandcenter", false):
			return Vector2(o.x, o.y)
	for o in world.get_player_objects(game._local_name):
		if o.get("structure", false):
			return Vector2(o.x, o.y)
	return Vector2()


## the standing trees within `radius` of `at`
func _standing(at: Vector2, radius: float) -> Array:
	var out := []
	for t in world.get_logic_trees(at.x, at.y, radius):
		if not t.toppled:
			out.append(t)
	return out


## the centre of the densest group of standing trees (the most within 120) nearest the local base
func _forest() -> Vector2:
	var base := _local_base()
	var trees: Array = _standing(base, 4000.0)
	var best := Vector2()
	var best_score := -1.0
	for t in trees:
		var p := Vector2(t.x, t.y)
		var n := 0
		for o in trees:
			if p.distance_to(Vector2(o.x, o.y)) < 120.0:
				n += 1
		var score := float(n) * 1000.0 - p.distance_to(base) * 0.2
		if n >= 6 and score > best_score:
			best_score = score
			best = p
	return best


func _create_first(names: Array, at: Vector2) -> Dictionary:
	for n in names:
		var id: int = world.create_object(n, game._local_index, at.x, at.y, 0.0)
		if id > 0:
			return {"id": id, "template": n}
	return {}


func _money() -> int:
	for p in world.get_economy().players:
		if p.name == game._local_name:
			return int(p.earned)
	return 0


func _changed_trees(at: Vector2, radius: float, before: Dictionary) -> Dictionary:
	var toppled := 0
	var harvested := 0
	for t in world.get_logic_trees(at.x, at.y, radius):
		var was: Dictionary = before.get(int(t.id), {})
		if t.toppled and not was.get("toppled", false):
			toppled += 1
		if int(t.value) < int(was.get("value", t.value)):
			harvested += 1
	var gone := 0
	for id in before:
		var found := false
		for t in world.get_logic_trees(at.x, at.y, radius):
			found = found or int(t.id) == int(id)
		if not found:
			gone += 1
	return {"toppled": toppled, "harvested": harvested, "gone": gone}


func _snapshot(at: Vector2, radius: float) -> Dictionary:
	var out := {}
	for t in world.get_logic_trees(at.x, at.y, radius):
		out[int(t.id)] = t
	return out


func _scenario_harvest() -> String:
	var forest := _forest()
	if forest == Vector2():
		return "no forest near the local base"
	var base := _local_base()
	var site := forest + (base - forest).normalized() * 160.0
	var mill := _create_first(["IsengardLumberMill", "MordorLumberMill"], site)
	if mill.is_empty():
		return "no lumber mill could be placed at (%d, %d)" % [site.x, site.y]
	print("TREES1 harvest: %s #%d at (%d, %d), forest at (%d, %d)" % [mill.template, mill.id, site.x, site.y, forest.x, forest.y])
	var before := _snapshot(forest, 400.0)
	var money0 := _money()
	hud.camera_look_at((forest + site) * 0.5)
	hud.camera_set_height(260.0)
	hud.camera_look_at((forest + site) * 0.5)
	var shots := 0
	var ch := {}
	for i in 60: # game seconds (the movie writer's fixed frame rate is not real time)
		await _seconds(1.0)
		ch = _changed_trees(forest, 400.0, before)
		if ch.toppled > 0 and shots == 0:
			game._save_named("trees1-harvest-felled")
			shots = 1
		if ch.harvested + ch.gone > 0 and _money() > money0 and shots == 1:
			game._save_named("trees1-harvest-lumber")
			shots = 2
			break
	print("TREES1 harvest: trees toppled %d, lumber taken from %d, gone %d; money %d -> %d" % [ch.toppled, ch.harvested, ch.gone, money0, _money()])
	if ch.get("toppled", 0) == 0:
		return "no tree felled in 60 s"
	if _money() <= money0:
		return "no lumber income in 60 s"
	return ""


func _scenario_crush() -> String:
	var forest := _forest()
	if forest == Vector2():
		return "no forest near the local base"
	var base := _local_base()
	var dir := (forest - base).normalized()
	var start := forest - dir * 130.0
	var unit := _create_first(["IsengardBatteringRam", "MordorSiegeTower", "MordorCatapult", "MordorMumakil", "MenTrebuchet", "IsengardBallista"], start)
	if unit.is_empty():
		return "no crusher could be made"
	var before := _snapshot(forest, 400.0)
	hud.camera_look_at(forest)
	hud.camera_set_height(300.0)
	hud.camera_look_at(forest)
	await _seconds(1.0)
	var target := forest + dir * 200.0
	var moved: Dictionary = world.order_move([unit.id], target.x, target.y)
	if not moved.get("ok", false):
		return "the move order was refused: %s" % moved.get("error", "")
	print("TREES1 crush: %s #%d from (%d, %d) through the forest at (%d, %d)" % [unit.template, unit.id, start.x, start.y, forest.x, forest.y])
	var ch := {}
	var shot := false
	for i in 40:
		await _seconds(1.0)
		var o: Dictionary = world.get_object(unit.id)
		if o.get("ok", false):
			hud.camera_look_at(Vector2(o.x, o.y))
		ch = _changed_trees(forest, 400.0, before)
		if ch.toppled > 0 and not shot:
			await _seconds(1.5)
			game._save_named("trees1-crush")
			shot = true
		elif shot and ch.toppled >= 3:
			await _seconds(4.0) # the last ones finish falling
			break
	print("TREES1 crush: trees toppled %d" % ch.get("toppled", 0))
	if ch.get("toppled", 0) == 0:
		return "%s toppled no tree" % unit.template
	return ""
