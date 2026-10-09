## Headless test of the stealth look of static draws (lane STEALTH-1, review r1 fix 2): a stealth postern gate (AngmarWallPosternGateSmall: an InvisibilityUpdate
## CAMOUFLAGE nugget and a W3DFloorDraw) is not drawn for an enemy while camouflaged, pulses for its owner, and its draws return to full opacity and are drawn
## for the enemy once a detector reveals it.
##
##   godot --headless --path godot --script res://tests/stealth_gate_test.gd
##
## Needs ROTWK_INSTALL and BFME2_INSTALL (prints SKIP and exits 77 without). Exit codes: 0 pass, 1 fail, 77 skip.
extends SceneTree

const MAP := "map mp fall back 4p"
const GATE := "AngmarWallPosternGateSmall"

var _failures := 0


func _initialize() -> void:
	var code: int = await _run()
	print("STEALTHGATE %s" % ["PASS" if code == 0 else ("SKIP" if code == 77 else "FAIL")])
	quit(code)


func _check(ok: bool, what: String) -> void:
	print("  %s %s" % ["ok  " if ok else "FAIL", what])
	if not ok:
		_failures += 1


func _run() -> int:
	if OS.get_environment("ROTWK_INSTALL").is_empty() or OS.get_environment("BFME2_INSTALL").is_empty():
		print("SKIP: ROTWK_INSTALL / BFME2_INSTALL not set")
		return 77
	var fs: RefCounted = ClassDB.instantiate("RetailFileSystem")
	var mount: Dictionary = fs.mount_retail()
	if not mount.ok:
		print("  mount failed ", mount.errors)
		return 1
	var world: Node3D = ClassDB.instantiate("GameWorld")
	root.add_child(world)
	var setup: Dictionary = world.setup(fs)
	if not setup.ok:
		print("  setup failed ", setup.errors)
		return 1
	var slots := [
		{"player": "Player_1", "faction": "FactionMen", "human": true, "team": 0, "start_index": 0},
		{"player": "Player_2", "faction": "FactionAngmar", "human": false, "team": 1, "start_index": 1},
	]
	var rep: Dictionary = world.load_map(MAP, {"slots": slots, "seed": 4711})
	if not rep.ok:
		print("  load_map failed ", rep.errors.slice(0, 5))
		return 1
	var men := -1
	var angmar := -1
	var idx := 0
	for pl in rep.players:
		if "Player_1" in str(pl) and men < 0:
			men = idx
		if "Player_2" in str(pl) and angmar < 0:
			angmar = idx
		idx += 1
	world.set_logic_thread(false)
	world.set_shroud_drawn(false)
	# far from every start: the middle of the map's objects
	var lo := Vector2(1e9, 1e9)
	var hi := Vector2(-1e9, -1e9)
	for id in world.get_object_ids():
		var o: Dictionary = world.get_object(id)
		lo = Vector2(minf(lo.x, o.x), minf(lo.y, o.y))
		hi = Vector2(maxf(hi.x, o.x), maxf(hi.y, o.y))
	var c := (lo + hi) * 0.5
	world.set_local_player(men)
	var gate: int = world.create_object(GATE, angmar, c.x, c.y, 0.0)
	_check(gate > 0, "%s created" % GATE)
	if gate <= 0:
		return 1
	var inv: Dictionary = {}
	for k in 30:
		world.advance(0.2)
		inv = world.get_invisibility(gate, men)
		if inv.get("type", 2) == 1:
			break
	world.advance(0.2)
	_check(inv.get("type", 2) == 1 and inv.get("look", 0) == 5, "the gate is camouflaged and an enemy's view hides it: %s" % [inv])
	var g: Dictionary = world.get_object(gate)
	_check(g.instances == 0, "no draw of the gate is shown to the enemy, its floor draw included (%d instances)" % g.instances)
	# the owner's view: the draws are shown and pulse (InvisibilityOpacityMin 0.4)
	world.set_local_player(angmar)
	var lowest := 1.0
	var shown := 0
	for k in 40:
		world.advance(0.05)
		g = world.get_object(gate)
		shown = maxi(shown, g.instances)
		for op in g.instance_opacities:
			lowest = minf(lowest, op)
	_check(shown >= 2, "the owner sees the gate's draws (%d instances)" % shown)
	_check(lowest < 0.99, "the owner's view pulses (lowest opacity %.2f)" % lowest)
	# a detector of the enemy reveals it: every draw, the static floor included, back to full opacity; the enemy sees it again
	var totem: int = world.create_object("WildSkullTotem", men, c.x + 150.0, c.y, 0.0)
	_check(totem > 0, "a skull totem of the enemy is placed")
	for k in 20:
		world.advance(0.2)
		inv = world.get_invisibility(gate, men)
		if inv.get("type", 2) == 2:
			break
	_check(inv.get("type", 2) == 2 and inv.get("detected", false), "the totem revealed the gate: %s" % [inv])
	# the owner sees a detected gate pulse too (look 4, RW 0x6760F9 state 4); once the detection status clears (ReinvisibityDelay) while the totem keeps it
	# visible, the look is normal (0) and every draw must be back at full opacity
	for k in 40:
		world.advance(0.2)
		inv = world.get_invisibility(gate, men)
		if inv.get("look", -1) == 0:
			break
	_check(inv.get("look", -1) == 0 and inv.get("type", 2) == 2, "the gate is visible with the normal look for its owner: %s" % [inv])
	world.advance(0.05)
	g = world.get_object(gate)
	var all_full: bool = not g.instance_opacities.is_empty()
	for op in g.instance_opacities:
		all_full = all_full and is_equal_approx(op, 1.0)
	_check(all_full, "the owner's view returns every draw to full opacity %s" % [g.instance_opacities])
	world.set_local_player(men)
	world.advance(0.2)
	g = world.get_object(gate)
	_check(g.instances >= 2, "the enemy sees the revealed gate again (%d instances)" % g.instances)
	return 0 if _failures == 0 else 1
