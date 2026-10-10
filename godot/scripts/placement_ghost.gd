## Lane PLAY-1: the building placement ghost, the structure drawn under the cursor while a building waits for its site. The HUD keeps the placement
## (InGameUI's placeBuildAvailable / the placement translator: the template, the site under the cursor, the angle and the legality code of BuildPlacement);
## this node draws it as RotWK does. TARGET FACTS (RotWK game.dat, caveat S-001):
##   * placeBuildAvailable (RW 0x69C5E6) makes a drawable of the template (RW 0x6CFE8C) with the builder's house colour (RW 0x674504), the model condition
##     BUILD_PLACEMENT_CURSOR (RW 0x69C708, bit 0x6C) and the opacity 0.45 (drawable + 0xB0 = RW 0xC12408);
##   * the placement update (RW 0x6A2AE5) moves it to the site and, every other client frame (RW 0x6A3939), asks BuildAssistant::isLocationLegalToBuild
##     (vslot 0x44): legal -> no tint (RW 0x6757BB(null)); code 9 -> (0, 0, 0.5), (0, 0, 1.7) every fourth client frame (RW 0x6A3A86 .. 0x6A3AB7); any other
##     code -> IllegalBuildColor (1, 0, 0) (RW 0xDA0B28).
## The look comes from GameWorld.get_placement_ghost (the BUILD_PLACEMENT_CURSOR state's model and the sub objects its script hides).
## INFERENCE / NOT PORTED (stop S-3302): the tint is drawn as an additive overlay (the drawable tint's own shader term was not read); the house colour and the
## anchor / arrow models of a rotatable placement (W3DInGameUI + 0xAB8 / 0xABC) are not drawn; the model is in its bind pose.
## Lane PLAY-3 (the owner: no preview when a builder places a fortress): a castle (a template with CastleBehavior, e.g. MenFortress, whose own
## BUILD_PLACEMENT_CURSOR model is None) is drawn as its layout for the local faction, one ghost per layout entry (RW 0x6A2AE5's castle branch, see
## GameClient/PlacementGhost.h castleLookOf): the entry turned by the ghost's angle about the site, on the ground at its own point (RW 0x6A36A8).
## INFERENCE (stop S-3302): every piece takes the site's tint (RotWK tests the pieces of some templates one by one, RW 0x6A39A0 .. 0x6A3B0F, not ported).
## A bare-mesh model (no HLod, e.g. GondorStatue's GPHealstue) builds on the default one pivot hierarchy (W3DModelBuilder).
extends Node3D

var hud: Node
var world: Node
var fs: RefCounted
var _builder: RefCounted
var _template := ""
var _ghost: Node3D
var _meshes: Array = []          # the ghost's MeshInstance3Ds
var _overlay: StandardMaterial3D
var _tint := Color(0, 0, 0, 0)
var _frames := 0
var shown_template := ""         # tests: the template drawn now ("" none)
var shown_tint := Color(0, 0, 0, 0)
var shown_pieces := 0            # tests: the castle layout pieces drawn now
var errors: Array = []
var _pieces: Array = []          # lane PLAY-3: [{node, x, y, angle}] of a castle's layout


func setup(p_hud: Node, p_world: Node, p_fs: RefCounted) -> void:
	hud = p_hud
	world = p_world
	fs = p_fs
	_builder = ClassDB.instantiate("W3DModelBuilder")
	_overlay = StandardMaterial3D.new()
	_overlay.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	_overlay.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	_overlay.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	_overlay.cull_mode = BaseMaterial3D.CULL_DISABLED


func _process(_delta: float) -> void:
	if hud == null:
		return
	_frames += 1
	var p: Dictionary = hud.get_placement()
	var placing: bool = p.get("placing", false) and p.get("has_ghost", false)
	if not placing:
		_clear()
		return
	var tmpl: String = p.get("template", "")
	if tmpl != _template:
		_build(tmpl)
	if _ghost == null:
		return
	# SAGE (x, y, z) -> Godot (x, z, -y); the angle about SAGE z is the same angle about Godot y
	var angle := float(p.get("angle", 0.0))
	_ghost.transform = Transform3D(Basis(Vector3.UP, angle), Vector3(p.x, p.z, -p.y))
	# lane PLAY-3: the castle's pieces, each on the ground at its own point (RW 0x6A359E .. 0x6A36A8)
	var c := cos(angle)
	var sn := sin(angle)
	for pc in _pieces:
		var wx: float = p.x + pc.x * c - pc.y * sn
		var wy: float = p.y + pc.x * sn + pc.y * c
		var wz: float = world.get_ground_height(wx, wy)
		pc.node.global_transform = Transform3D(Basis(Vector3.UP, angle + pc.angle), Vector3(wx, wz, -wy))
	var legal: int = p.get("legal", 0)
	var tint := Color(0, 0, 0, 0)
	if legal == 9:
		tint = Color(0, 0, 1.7 if (_frames & 4) != 0 else 0.5, 1)
	elif legal != 0:
		tint = Color(1, 0, 0, 1)
	if tint != _tint:
		_tint = tint
		_overlay.albedo_color = Color(minf(tint.r, 1.0), minf(tint.g, 1.0), minf(tint.b, 1.0), 0.6 if tint.a > 0 else 0.0)
		for mi in _meshes:
			mi.material_overlay = _overlay if tint.a > 0 else null
	shown_tint = _tint


func _build(tmpl: String) -> void:
	_clear()
	_template = tmpl
	var look: Dictionary = world.get_placement_ghost(tmpl)
	var model: String = look.get("model", "")
	var castle: bool = look.get("castle", false)
	if castle and not str(look.get("castle_error", "")).is_empty():
		errors.append("%s: castle layout: %s" % [tmpl, look.castle_error])
	if model.is_empty() and not castle:
		errors.append("[S-3302] %s has no model for BUILD_PLACEMENT_CURSOR: no ghost" % tmpl)
		return
	# the root at the site; the castle's own drawable (its BUILD_PLACEMENT_CURSOR model, MenFortress: None) and its pieces
	_ghost = Node3D.new()
	add_child(_ghost)
	if not model.is_empty():
		var body: Node3D = _builder.build_model(fs, model)
		if body == null:
			errors.append("ghost model %s: %s" % [model, _builder.get_report()])
		else:
			_ghost.add_child(body)
			_collect(body, _upper(look.get("hidden", [])))
	for e in look.get("pieces", []):
		if str(e.model).is_empty():
			continue # a piece whose BUILD_PLACEMENT_CURSOR model is None draws nothing (as its drawable)
		var node: Node3D = _builder.build_model(fs, str(e.model))
		if node == null:
			errors.append("castle piece %s model %s: %s" % [e.template, e.model, _builder.get_report()])
			continue
		_ghost.add_child(node)
		_collect(node, _upper(e.hidden))
		_pieces.append({"node": node, "x": float(e.x), "y": float(e.y), "angle": float(e.angle)})
	if _meshes.is_empty():
		errors.append("%s: the ghost has nothing to draw (model '%s', %d layout pieces)" % [tmpl, model, look.get("pieces", []).size()])
		_ghost.queue_free()
		_ghost = null
		_pieces.clear()
		return
	shown_template = tmpl
	shown_pieces = _pieces.size()


func _upper(names: Array) -> Array:
	var out: Array = []
	for h in names:
		out.append(str(h).to_upper())
	return out


## the MeshInstance3Ds of the ghost: 0.45 opacity (RW 0xC12408), the script's hidden sub objects removed (the node of "MODEL.SUBOBJECT" is "MODEL_SUBOBJECT")
func _collect(n: Node, hidden: Array) -> void:
	for c in n.get_children():
		if c is MeshInstance3D:
			var name_up := String(c.name).to_upper()
			var hide := false
			for h in hidden:
				hide = hide or name_up == h or name_up.ends_with("_" + h)
			if hide:
				c.visible = false
			else:
				c.transparency = 0.55
				c.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
				_meshes.append(c)
		_collect(c, hidden)


func _clear() -> void:
	if _ghost != null:
		_ghost.queue_free()
	_ghost = null
	_meshes.clear()
	_pieces.clear()
	shown_pieces = 0
	_template = ""
	_tint = Color(0, 0, 0, 0)
	shown_template = ""
	shown_tint = Color(0, 0, 0, 0)
