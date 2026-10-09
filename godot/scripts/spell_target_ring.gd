## Lane SPELL-2: the radius preview of a spell book power waiting for its target. The spell book itself is the retail movies (InGameSpellBook.apt in the
## Palantir, SpellStore.apt; GameClient/InGameHud.h): this node only draws, on the ground under the cursor, a ring of the power's RadiusCursorRadius while
## the HUD's cast bar is targeting (InGameHudNode.get_spellbook_state). It never changes the game.
extends Node3D

var hud: Node2D
var world: Node3D
var _ring: MeshInstance3D
var mouse := Vector2.ZERO   # a harness may set the cursor it simulates


func setup(p_hud: Node2D, p_world: Node3D) -> void:
	hud = p_hud
	world = p_world
	_ring = MeshInstance3D.new()
	var torus := TorusMesh.new()
	torus.inner_radius = 0.96
	torus.outer_radius = 1.0
	torus.rings = 64
	torus.ring_segments = 4
	_ring.mesh = torus
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.albedo_color = Color(0.4, 1.0, 0.5, 0.9)
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.no_depth_test = true
	_ring.material_override = mat
	_ring.visible = false
	add_child(_ring)


func _input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		mouse = event.position


func _process(_delta: float) -> void:
	if hud == null:
		return
	var st: Dictionary = hud.get_spellbook_state()
	var targeting: bool = st.get("targeting", false)
	_ring.visible = targeting
	if not targeting:
		return
	var radius: float = st.get("radius", 0.0)
	if radius <= 0.0:
		radius = 60.0
	var w: Vector2 = hud.pixel_to_world(mouse)
	if w.x < -1e8:
		_ring.visible = false
		return
	var h: float = world.get_ground_height(w.x, w.y)
	# SAGE (x, y, z) -> Godot (x, z, -y)
	_ring.position = Vector3(w.x, h + 2.0, -w.y)
	_ring.scale = Vector3(radius, radius * 0.2, radius)
