extends CanvasLayer
## Lane WOTR-2: the War of the Ring setup's map preview. Retail draws TheLivingWorld's 3D display in preview mode (RW 0x976877: + 0x44 = 1, the region
## colours from the setup, RW 0x975E04) inside AptMapPreview's "LivingWorld" gadget; the regions are picked on it (AptSkirmish::clickRegion: RW 0x845830's
## start position branch with RW 0x975C3A) and the region under the mouse fills the territory description (RW 0x976D64). Here LivingMap and the LMR_Fill
## region fills are drawn in a SubViewport of their own world, shown in the gadget's rectangle (presentation, S-1335): a region a slot holds is filled
## with that slot's LivingWorldColor, a free selectable region under the mouse with the next slot's (RW 0x975E04 with region + 0x160 == -1).

var game: Node
var lw: RefCounted           # LivingWorldGame (the static map data)
var data := {}
var built_campaign := ""
var regions: Array = []
var fills := {}              # region index -> MeshInstance3D (LMR_Fill: the pick bodies)
var region_fx: Node3D        # wotr_region_fx.gd
var shell_start_color := Color(0.95, 0.95, 0.95)
var vp: SubViewport
var view: TextureRect
var cam: Camera3D
var hover := -1
var last_state := {}
var pressed_button := -1


func init(game_node: Node) -> void:
	game = game_node
	layer = 1 # over the shell's movie (its map frame is an opaque panel), under every other overlay; the rectangle is the LivingWorld clip's
	name = "WotrSetupPreview"
	view = TextureRect.new()
	view.stretch_mode = TextureRect.STRETCH_SCALE
	view.expand_mode = TextureRect.EXPAND_IGNORE_SIZE # the gadget's size, never the texture's
	view.mouse_filter = Control.MOUSE_FILTER_STOP
	view.visible = false
	view.gui_input.connect(_on_view_input)
	view.mouse_exited.connect(func(): _set_hover(-1))
	add_child(view)


## game.gd's frame: show the preview while the strategic setup's LivingWorld gadget is on screen
func tick() -> void:
	var st: Dictionary = game._shell.strategic_setup({"kind": "state"})
	if not st.get("found", false) or not st.get("visible", false):
		if view.visible:
			view.visible = false
			_set_hover(-1)
		return
	if st.region_campaign != built_campaign:
		if not _build(str(st.region_campaign)):
			return
	var r: Dictionary = st.rect
	# the gadget's rectangle is in the shell's stage coordinates; the overlay is in window pixels
	var lo: Vector2 = game._shell.stage_to_window(Vector2(r.x, r.y))
	var hi: Vector2 = game._shell.stage_to_window(Vector2(r.x + r.w, r.y + r.h))
	view.position = lo
	view.size = Vector2(maxf(1.0, hi.x - lo.x), maxf(1.0, hi.y - lo.y))
	var want := Vector2i(maxi(64, int(view.size.x)), maxi(64, int(view.size.y)))
	if vp.size != want:
		vp.size = want
		_frame_camera()
	if not view.visible:
		lw.apply_map_lighting() # LivingWorldMapInfo's lights as the W3D shaders' light set (a battle's map set its own)
	view.visible = true
	region_fx.update_gamma()
	_apply_colors(st)
	last_state = st


func _build(campaign_name: String) -> bool:
	if lw == null:
		lw = ClassDB.instantiate("LivingWorldGame")
		data = lw.setup(game._world)
		if not data.get("ok", false):
			push_error("WotR preview: " + str(data.get("error", "")))
			return false
	regions = []
	for rc in data.region_campaigns:
		if rc.name == campaign_name:
			regions = rc.regions
	if vp != null:
		vp.queue_free()
	fills.clear()
	vp = SubViewport.new()
	vp.own_world_3d = false
	vp.world_3d = World3D.new() # the preview's own scene and physics space (the strategic map's world stays apart)
	vp.transparent_bg = false
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	vp.size = Vector2i(512, 384)
	add_child(vp)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color.BLACK
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	var amb: Color = data.map_info.get("ambient", Color(0.35, 0.35, 0.35))
	env.environment.ambient_light_color = amb # LivingWorldMapInfo's Ambient
	vp.add_child(env)
	var mi: Dictionary = data.map_info
	var instancer: Node3D = ClassDB.instantiate("W3DInstancer")
	vp.add_child(instancer)
	instancer.setup(game._fs)
	var model: int = instancer.add_model(mi.map_object)
	if model >= 0:
		var rep: Dictionary = instancer.get_model_report(model)
		var inst: int = instancer.add_instance(model, Transform3D.IDENTITY)
		var hidden := PackedStringArray()
		var cloud_border := str(mi.get("cloud_border_sub_object", "")).to_upper()
		var text_layer := str(mi.get("text_layer_sub_object", "")).to_upper()
		for so in rep.sub_objects:
			var sub := str(so.name).to_upper()
			# as the strategic map (wotr.gd _build_map, S-1335): the IMPASSABLE overlays and the border cloud are hidden; the preview also leaves out
			# TextLayerSubObject ("text plane": the names and the territory lines), which retail's setup preview does not show (the owner's RotWK 2.01
			# screenshot) [presentation; the preview mode's own rule was not read]
			if sub.contains("IMPASSABLE") or sub.ends_with(".PLANE03") or (not cloud_border.is_empty() and sub.get_slice(".", 1) == cloud_border) \
					or (not text_layer.is_empty() and sub.get_slice(".", 1) == text_layer):
				hidden.append(str(so.name))
		instancer.set_instance_hidden_subobjects(inst, hidden)
	var fx: Dictionary = data.region_effects[0] if data.region_effects.size() > 0 else {}
	region_fx = preload("res://scripts/wotr_region_fx.gd").new()
	vp.add_child(region_fx)
	region_fx.build(game._fs, regions, fx)
	shell_start_color = fx.get("shell_start_color", Color(0.95, 0.95, 0.95))
	_bind_fills(region_fx.fill_meshes())
	cam = Camera3D.new()
	cam.fov = 45
	cam.near = 5.0
	cam.far = 40000.0
	vp.add_child(cam)
	cam.current = true
	_frame_camera()
	view.texture = vp.get_texture()
	built_campaign = campaign_name
	applied_key = ""
	return true


var map_bounds := AABB()

func _frame_camera() -> void:
	# retail's setup preview looks straight down on the map and fills the frame with it (the owner's reference screenshot of RotWK 2.01): an orthographic
	# camera above the regions' centre, sized so 84% of the regions' width spans the gadget (as that screenshot) [presentation; retail's preview camera
	# was not read]
	if cam == null or data.is_empty():
		return
	var b := map_bounds
	if b.size == Vector3.ZERO:
		var mi: Dictionary = data.map_info
		b = AABB(Vector3(mi.center.x - mi.extent.x * 0.5, 0.0, -mi.center.y - mi.extent.y * 0.5), Vector3(mi.extent.x, 1.0, mi.extent.y))
	var center := b.get_center()
	var aspect := float(vp.size.x) / maxf(1.0, float(vp.size.y))
	cam.projection = Camera3D.PROJECTION_ORTHOGONAL
	cam.size = b.size.x * 0.84 / aspect # Camera3D.size is the vertical extent (keep_aspect height); retail's frame cuts the far west coast and the east
	cam.position = center + Vector3(0, 8000.0, 0)
	cam.look_at(center, Vector3(0, 0, -1))


## the region pick bodies on LMR_Fill's sub-objects (RegionObject: "the name of W3D object that has regions pick-boxes"); their bounds frame the camera
func _bind_fills(fill_meshes: Dictionary) -> void:
	fills = fill_meshes
	var first := true
	for i in fills.keys():
		var mesh: MeshInstance3D = fills[i]
		var body := StaticBody3D.new()
		body.set_meta("region", i)
		var shape := CollisionShape3D.new()
		shape.shape = mesh.mesh.create_trimesh_shape()
		body.add_child(shape)
		mesh.add_child(body)
		var ab: AABB = mesh.global_transform * mesh.get_aabb()
		map_bounds = ab if first else map_bounds.merge(ab)
		first = false


func _color(ci: int) -> Color:
	var colors: Array = data.get("colors", [])
	if ci < 0 or ci >= colors.size():
		return shell_start_color # a random colour: LivingWorldRegionEffects' ShellStartPositionColor ("start positions when shown in the shell")
	var rgb: int = int(colors[ci].living_world)
	if rgb == 0:
		rgb = int(colors[ci].rgb)
	return Color8((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255)


## a held start region: FilledOwnershipEffect and HomeRegionHighlight in the holder's colour (RW 0x9770CF, the colour from RW 0x975E04); a free selectable
## region under the mouse: the fill in the next slot's colour (region + 0x160 == -1: RW 0x975C3A's slot)
var applied_key := ""

func _apply_colors(st: Dictionary) -> void:
	var key := JSON.stringify([st.holders, hover, st.next_slot, st.next_color])
	if key == applied_key:
		return
	applied_key = key
	region_fx.clear_regions()
	var none := Color(0, 0, 0, 0)
	var held := {}
	for h in st.holders:
		held[int(h.region)] = true
		var c := _color(int(h.color))
		region_fx.set_region(int(h.region), c, c, none) # RegionSelectionEffect (LMR_Edge) has LoadInShell = No
	if hover >= 0 and not held.has(hover) and int(st.next_slot) >= 0:
		var sel: Array = st.get("selectable", [])
		if hover < sel.size() and sel[hover]:
			region_fx.set_region(hover, _color(int(st.next_color)), none, none)


func pick(local: Vector2) -> int:
	if cam == null:
		return -1
	var p := local * Vector2(vp.size) / view.size
	var from := cam.project_ray_origin(p)
	var to := from + cam.project_ray_normal(p) * 60000.0
	var hit := vp.find_world_3d().direct_space_state.intersect_ray(PhysicsRayQueryParameters3D.create(from, to))
	if hit.is_empty() or not hit.collider.has_meta("region"):
		return -1
	return int(hit.collider.get_meta("region"))


## the region the preview shows at a stage position (the scripted run's check of its own click), -1 none
func pick_stage(stage: Vector2) -> int:
	var w: Vector2 = game._shell.stage_to_window(stage)
	return pick(w - view.position)


func _set_hover(r: int) -> void:
	if r == hover:
		return
	hover = r
	game._shell.strategic_setup({"kind": "hover", "region": r})


func _on_view_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		_set_hover(pick(event.position))
	elif event is InputEventMouseButton and (event.button_index == MOUSE_BUTTON_LEFT or event.button_index == MOUSE_BUTTON_RIGHT):
		# a click is a press and a release on the same region (the gadget's GBM_SELECTED / GBM_SELECTED_RIGHT on the release)
		var r := pick(event.position)
		if event.pressed:
			pressed_button = event.button_index if r >= 0 else -1
		elif pressed_button == event.button_index and r >= 0:
			pressed_button = -1
			var res: Dictionary = game._shell.strategic_setup({"kind": "click", "region": r, "right": event.button_index == MOUSE_BUTTON_RIGHT})
			print("WOTR setup: region %d (%s) clicked: %s" % [r, regions[r].name if r < regions.size() else "?", "taken" if res.get("taken", false) else "ignored"])


## the scripted run: a click on region `r` through the same path as the mouse
func click_region(r: int, right: bool = false) -> Dictionary:
	return game._shell.strategic_setup({"kind": "click", "region": r, "right": right})


## the stage position of region `r`'s centre in the preview (for the scripted run's real mouse clicks); Vector2(-1, -1) when it is not drawn
func region_screen_position(r: int) -> Vector2:
	if cam == null or not view.visible or r < 0 or r >= regions.size():
		return Vector2(-1, -1)
	var reg: Dictionary = regions[r]
	var c: Vector2 = reg.center if (reg.get("custom_center", false) or reg.hero_spots.is_empty()) else reg.hero_spots[0]
	var p := cam.unproject_position(Vector3(c.x, 0.0, -c.y))
	return game._shell.window_to_stage(view.position + p * view.size / Vector2(vp.size)) # stage coordinates, as game.gd's _mouse_event takes
