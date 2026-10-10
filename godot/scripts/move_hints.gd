## Lane PLAY-1: the move hint, the marker on the ground where a move order was given. The HUD keeps the hints (InGameUI::createMoveHint, fed by its
## HintSpy step, RotWK RW 0x838225: every move / attack move / force move order that leaves the input stream; one marker at a time, RW 0x69F54F);
## this node draws them as RotWK's W3DInGameUI::drawMoveHints (RW 0x48EDED) does: GameData's MoveHintName model (retail "SCMoveHint") while the hint
## is younger than 41 client frames, one instance per hint slot. It never changes the game.
## RotWK's draw (RW 0x48EDED) places the model with TerrainLogic::alignOnTerrain (vslot 0x48): the model's up axis is the terrain normal at the point,
## taken here from the logic's ground heights 5 units around it; the lift to the water surface (vslot 0x4C) is not ported (stop S-1920). SCMoveHint.w3d
## carries no animation of its own (its "%s.%s" clip does not exist), so its bind pose with the material's own texture animation is the picture.
extends Node3D

var hud: Node
var world: Node
var _inst: Node3D
var _model := -1
var _model_name := ""
var _slots := {}         # hint slot -> { id: instance id, frame: the hint's client frame }
var drawn_total := 0     # instances created (tests, reports)
var errors: Array = []


func setup(p_hud: Node, fs: RefCounted, p_world: Node = null) -> Dictionary:
	hud = p_hud
	world = p_world
	var state: Dictionary = hud.get_move_hints()
	_model_name = str(state.get("model", ""))
	if _model_name == "":
		errors.append("[S-1920] GameData has no MoveHintName: no move hint is drawn")
		return {"ok": false, "errors": errors}
	_inst = ClassDB.instantiate("W3DInstancer")
	add_child(_inst)
	var r: Dictionary = _inst.setup(fs)
	if not r.get("ok", false):
		errors.append("move hint instancer: %s" % [r.get("errors", [])])
		return {"ok": false, "errors": errors}
	_model = _inst.add_model(_model_name)
	if _model < 0:
		errors.append("move hint model %s: %s" % [_model_name, _inst.get_model_report(_model)])
		return {"ok": false, "errors": errors}
	return {"ok": true, "errors": errors, "model": _model_name}


func _process(_delta: float) -> void:
	if hud == null or _model < 0:
		return
	var state: Dictionary = hud.get_move_hints()
	var live := {}
	for h in state.get("hints", []):
		var slot: int = h.slot
		live[slot] = true
		var t := Transform3D(_terrain_basis(h.x, h.y), h.position)
		var cur = _slots.get(slot)
		if cur != null and cur.frame != h.frame:
			_inst.remove_instance(cur.id)
			cur = null
		if cur == null:
			var id: int = _inst.add_instance(_model, t, "", 0.0, 1.0)
			if id < 0:
				continue
			_slots[slot] = {"id": id, "frame": h.frame}
			drawn_total += 1
		else:
			_inst.set_instance_transform(cur.id, t)
	for slot in _slots.keys():
		if not live.has(slot):
			_inst.remove_instance(_slots[slot].id)
			_slots.erase(slot)


## the terrain's normal at SAGE (x, y) as the model's up axis (Godot axes: x, z, -y); identity without the world
func _terrain_basis(x: float, y: float) -> Basis:
	if world == null:
		return Basis.IDENTITY
	const D := 5.0
	var dx: float = world.get_ground_height(x + D, y) - world.get_ground_height(x - D, y)
	var dy: float = world.get_ground_height(x, y + D) - world.get_ground_height(x, y - D)
	var n_sage := Vector3(-dx, -dy, 2.0 * D).normalized()
	var up := Vector3(n_sage.x, n_sage.z, -n_sage.y)
	var right := (Vector3.RIGHT - up * up.dot(Vector3.RIGHT)).normalized()
	var back := right.cross(up)
	return Basis(right, up, back)


## the hints drawn this frame (tests)
func drawn_count() -> int:
	return _slots.size()
