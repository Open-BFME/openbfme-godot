## Lane PLAY-1: the move hint, the marker on the ground where a move order was given. The HUD keeps the hints (InGameUI::createMoveHint, fed by its
## HintSpy step: every move / attack move / force move / waypoint order that leaves the input stream); this node draws them as ZH's
## W3DInGameUI::drawMoveHints does: GameData's MoveHintName model (retail "SCMoveHint") at every hint younger than 40 client frames, one instance per
## hint slot. It never changes the game.
## INFERENCE (stop S-1920): the model is drawn upright at the hint's ground point (ZH aligns it with the terrain normal, TerrainLogic::alignOnTerrain, and lifts
## it to the water surface); SCMoveHint.w3d carries no animation of its own (its "%s.%s" clip does not exist), so its bind pose with the material's own
## texture animation is the picture.
extends Node3D

var hud: Node
var _inst: Node3D
var _model := -1
var _model_name := ""
var _slots := {}         # hint slot -> { id: instance id, frame: the hint's client frame }
var drawn_total := 0     # instances created (tests, reports)
var errors: Array = []


func setup(p_hud: Node, fs: RefCounted) -> Dictionary:
	hud = p_hud
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
		var t := Transform3D(Basis.IDENTITY, h.position)
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


## the hints drawn this frame (tests)
func drawn_count() -> int:
	return _slots.size()
