class_name RetailRallyIndicator
extends Node3D
## Presentation-only rally banner for the selected production structure.
##
## Retail plants a flag on a production building's rally point so the player can
## see where the units it trains will walk. The simulation already owns the
## point: every structure row carries `rally`, `set_structure_rally` moves it
## (retail_slice_sim.gd), and `_step_production` routes the finished battalion
## there. This node adds no simulation state whatsoever — it only draws the
## point the sim already holds, so the cross-platform state pin is untouched.
##
## PACK GAP (named, not papered over): no mounted content pack ships retail's
## rally-flag art. The whole content-packs tree carries exactly one world model
## in this family, `assets/models/system/scmovehint.glb` (retail's SCMoveHint,
## already consumed by RetailOrderIndicator), and the interface-art index
## carries no rally marker image at all — the only "rally" ids are the
## Rallying Call SPELL icon (`SBGood_RallyingCall`) and unrelated banner
## upgrades (`BGFortress_Banner`). Until a republish compiles retail's rally
## flag, this draws a NAMED synthetic banner and says so via
## `art_source_is_synthetic`; `configure()` will prefer the retail model the
## moment a pack resolves one by id.
##
## The asset the republish must carry, transcribed from the pure RotWK 2.01
## tree (cache/effective-assets/data/ini/object/system/system.ini:1201):
##
##     Object RallyPointMarker
##       KindOf = DRAWABLE_ONLY
##       Draw = W3DScriptedModelDraw ModuleTag_01
##         DefaultModelConditionState
##           Model = RallyFlag_SKN
##         End
##         IdleAnimationState
##           Animation = IDLE
##             AnimationName = RallyFlag_SKL.RallyFlag_WAVA
##             AnimationMode = LOOP
##
## The compiled path below follows the convention the sibling object already
## demonstrates in the shipped packs: `Object MoveHint` -> `Model = SCMoveHint`
## (system.ini:1221) -> `assets/models/system/scmovehint.glb`, i.e. the MODEL
## name lowercased. RallyFlag_SKN therefore lands at rallyflag_skn.glb.

const AssetFactory = preload("res://src/view/asset_factory.gd")

## The retail model this indicator WANTS. Named here so the pack gap is
## greppable and so a future pack that ships it binds without a code change.
const RETAIL_RALLY_MODEL_PATH := "assets/models/system/rallyflag_skn.glb"
## Retail's authored model and idle-wave animation names, kept verbatim so the
## republish target is greppable from the code that wants it.
const RETAIL_RALLY_MODEL_NAME := "RallyFlag_SKN"
const RETAIL_RALLY_IDLE_ANIMATION := "RallyFlag_SKL.RallyFlag_WAVA"

## Banner geometry, in battlefield world units.
const POLE_HEIGHT := 1.15
const POLE_RADIUS := 0.035
const BANNER_WIDTH := 0.62
const BANNER_HEIGHT := 0.34
const RING_INNER := 0.30
const RING_OUTER := 0.38
## House colours read too close to the selection decal; retail's rally flag is a
## pale cloth on a dark pole.
const BANNER_COLOR := Color(0.86, 0.80, 0.56)
const POLE_COLOR := Color(0.19, 0.15, 0.11)
const RING_COLOR := Color(0.86, 0.80, 0.56, 0.55)

var showing_rally := false
var rally_point := Vector2.ZERO
var art_source_is_synthetic := true
var retail_model_path := ""
var _pack_root := ""
var _banner_root: Node3D
var _retail_model: Node3D


func configure(pack_root: String) -> void:
	## Bind retail rally art if the mounted pack has any; otherwise stay
	## synthetic and keep `art_source_is_synthetic` true so a gate can see it.
	_pack_root = pack_root
	if pack_root == "":
		return
	# The ModLoader autoload is looked up dynamically rather than by identifier:
	# this class is compiled during Godot's global-class scan, before autoloads
	# are registered, so a direct `ModLoader.` reference fails to compile.
	# resolve_pack_path is the containment-enforcing resolver — a pack must not
	# be able to point this at a file outside its own root.
	var loop := Engine.get_main_loop()
	var mod_loader: Node = null
	if loop is SceneTree:
		mod_loader = (loop as SceneTree).root.get_node_or_null("ModLoader")
	if mod_loader == null or not mod_loader.has_method("resolve_pack_path"):
		return
	var resolved := String(mod_loader.call("resolve_pack_path", pack_root, RETAIL_RALLY_MODEL_PATH))
	if resolved == "" or not FileAccess.file_exists(resolved):
		return
	retail_model_path = resolved
	art_source_is_synthetic = false


func _ready() -> void:
	_build_visuals()
	clear_rally()


func show_rally(point: Vector2, height: float) -> void:
	rally_point = point
	position = Vector3(point.x, height, point.y)
	_set_showing(true)


func clear_rally() -> void:
	_set_showing(false)


func _set_showing(value: bool) -> void:
	# Visibility is written ONLY on a real transition. Per-frame visibility
	# churn on HUD/world nodes is a known click- and hover-killing hazard in
	# this project, and _sync_presentation calls into here every frame.
	if showing_rally == value and visible == value:
		return
	showing_rally = value
	visible = value


func _build_visuals() -> void:
	if _banner_root != null:
		return
	if retail_model_path != "":
		var model := AssetFactory.make_explicit_model_visual(retail_model_path, 0)
		if model != null:
			_retail_model = model
			add_child(model)
			return
		# The pack named a model we could not instance: fall back to the
		# synthetic banner rather than drawing nothing, and say so.
		art_source_is_synthetic = true
	_banner_root = Node3D.new()
	_banner_root.name = "RallyBanner"
	add_child(_banner_root)

	var pole := MeshInstance3D.new()
	pole.name = "Pole"
	var pole_mesh := CylinderMesh.new()
	pole_mesh.top_radius = POLE_RADIUS
	pole_mesh.bottom_radius = POLE_RADIUS
	pole_mesh.height = POLE_HEIGHT
	pole_mesh.radial_segments = 6
	pole.mesh = pole_mesh
	pole.position = Vector3(0.0, POLE_HEIGHT * 0.5, 0.0)
	pole.material_override = _unshaded(POLE_COLOR)
	pole.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_banner_root.add_child(pole)

	var banner := MeshInstance3D.new()
	banner.name = "Cloth"
	var cloth := QuadMesh.new()
	cloth.size = Vector2(BANNER_WIDTH, BANNER_HEIGHT)
	banner.mesh = cloth
	banner.position = Vector3(BANNER_WIDTH * 0.5, POLE_HEIGHT - BANNER_HEIGHT * 0.6, 0.0)
	var cloth_material := _unshaded(BANNER_COLOR)
	cloth_material.cull_mode = BaseMaterial3D.CULL_DISABLED
	banner.material_override = cloth_material
	banner.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_banner_root.add_child(banner)

	var ring := MeshInstance3D.new()
	ring.name = "GroundRing"
	var torus := TorusMesh.new()
	torus.inner_radius = RING_INNER
	torus.outer_radius = RING_OUTER
	ring.mesh = torus
	ring.position = Vector3(0.0, 0.025, 0.0)
	var ring_material := _unshaded(RING_COLOR)
	ring_material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	ring.material_override = ring_material
	ring.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_banner_root.add_child(ring)


func _unshaded(color: Color) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = color
	return material
