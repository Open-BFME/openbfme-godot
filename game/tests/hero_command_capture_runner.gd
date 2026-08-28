extends SceneTree
## WHY DOES ONE HERO COMMAND ICON SIT ON A BARE SQUARE PANEL?
##
## Owner playtest 2026-08-27: with a hero selected, the icon in the top-right
## dish socket draws on a plain rounded panel instead of the authored black cup,
## while the two icons in the lower sockets look right. The men gate fixture
## fields no hero, so `hud_diagnostic_runner` cannot reach that state.
##
## This runner puts a REAL bound HUD (the selected Men pack, the same binding
## call the live game makes) into a 1920x1080 rendering viewport, gives it the
## owner's exact shape - a hero roster plus a THREE-command hero page seated in
## authored sockets 1, 4 and 5 - then reports, per button:
##   node path, rect, the seat it should occupy, the stylebox class actually in
##   effect for `normal`, and whether that stylebox carries a texture.
## It also names every Control whose rect contains socket 1's centre, so a
## foreign node drawn over the button cannot hide behind a coincidence.
##
## IT ASSERTS NOTHING. It is a microscope.
##
##   OPENBFME_CONTENT=<repo>\workspace\content-packs <godot> --path game \
##     --script res://tests/hero_command_capture_runner.gd -- --out <dir>

const PackCapability = preload("res://src/content/pack_capability.gd")
const SETTLE_FRAMES := 24
const VIEWPORT_SIZE := Vector2i(1920, 1080)
## The owner's page: three commands, in authored sockets 1, 4 and 5.
const OWNER_SLOTS := [2, 5, 6]

var HudScript: Script
var _viewport: SubViewport
var _hud: Control
var _out_dir := "res://"
var _frames := 0


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	for index in args.size():
		if args[index] == "--out" and index + 1 < args.size():
			_out_dir = args[index + 1]
	call_deferred("_run")


func _run() -> void:
	if DisplayServer.get_name() == "headless":
		print("HERO_COMMAND_CAPTURE needs a rendering window; rerun without --headless")
		quit(1)
		return
	DirAccess.make_dir_recursive_absolute(_out_dir)
	var content_db = root.get_node_or_null("ContentDB")
	if content_db == null:
		print("HERO_COMMAND_CAPTURE no ContentDB")
		quit(1)
		return
	HudScript = load("res://src/retail_slice/retail_hud.gd")
	var soldier: Dictionary = content_db.get_bundle_object("bfme2.object.gondor-fighter")
	var pack_root := String(soldier.get("_pack_root", ""))
	print("HERO_COMMAND_CAPTURE pack=%s" % pack_root)
	_viewport = SubViewport.new()
	_viewport.size = VIEWPORT_SIZE
	_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_viewport.transparent_bg = false
	root.add_child(_viewport)
	_hud = HudScript.new()
	# The live slice feeds the pack's playableUnit runtimes BEFORE build(), and
	# that is the ONLY way hero ability buttons come into existence.
	var runtimes: Dictionary = {}
	for key_value in content_db.get_playable_unit_runtimes().keys():
		var runtime: Dictionary = content_db.get_playable_unit_runtimes()[key_value]
		if String(runtime.get("_pack_root", "")) == pack_root:
			runtimes[key_value] = runtime
	print("HERO_COMMAND_CAPTURE runtimes=%d configure=%s" % [
		runtimes.size(), _hud.enable_playable_unit_content(runtimes, {})
	])
	_hud.set_anchors_preset(Control.PRESET_FULL_RECT)
	_hud.size = Vector2(VIEWPORT_SIZE)
	_viewport.add_child(_hud)
	_hud.build()
	var bind_error: String = _hud.bind_retail_train_commands(
		content_db, pack_root, true, Array(content_db.pack_roots)
	)
	print("HERO_COMMAND_CAPTURE bind=%s bound=%s" % [
		bind_error, str(_hud.retail_presentation_bound)
	])
	_hud.set_resources(300, 1, 200)
	_hud.set_dish_level("Level: 1", 0.0)
	_hud.sync_hero_bar([
		{"id": 1, "unit_type": "hero.a", "name": "Hero A", "level": 1,
			"health": 100, "maximum_health": 100, "selected": true},
	])
	var entries: Array = []
	for index in OWNER_SLOTS.size():
		entries.append({
			"command_kind": "train", "id": "hero.command.%d" % index,
			"icon": null, "text": "", "enabled": true,
			"label": "Hero command %d" % index, "tooltip": "",
			"slot": int(OWNER_SLOTS[index]),
		})
	_hud.sync_radial_commands(Vector2(VIEWPORT_SIZE) * Vector2(0.45, 0.35), entries)
	for i in SETTLE_FRAMES:
		await process_frame
	_report()
	var image := _viewport.get_texture().get_image()
	var path := _out_dir.path_join("hero-command-1920x1080.png")
	image.save_png(path)
	print("HERO_COMMAND_CAPTURE wrote %s" % path)
	quit(0)


func _report() -> void:
	var stage: Script = load("res://src/retail_slice/retail_hud_stage.gd")
	var panel: Control = _hud.command_panel
	for index in _hud._radial_buttons.size():
		var button: Button = _hud._radial_buttons[index]
		var seat := int(OWNER_SLOTS[index]) - 1
		var expected: Vector2 = panel.position + stage.command_slot_dock(
			seat, _hud.RETAIL_COMMAND_SLOT_SIZE
		) - Vector2(360.0, 0.0)
		var box: StyleBox = button.get_theme_stylebox("normal")
		var textured := box is StyleBoxTexture and (box as StyleBoxTexture).texture != null
		print("HERO_COMMAND_CAPTURE button %d path=%s rect=%s seat%d_expected=%s visible=%s icon=%s normal=%s textured=%s" % [
			index, str(button.get_path()), str(button.get_global_rect()), seat,
			str(expected), str(button.visible), str(button.icon != null),
			box.get_class() if box != null else "<null>", str(textured)
		])
	if _hud._radial_buttons.is_empty():
		return
	for unit_value in _hud.hero_ability_buttons.keys():
		for button_value in (_hud.hero_ability_buttons[unit_value] as Dictionary).values():
			var ability: Button = button_value
			var ability_box: StyleBox = ability.get_theme_stylebox("normal")
			print("HERO_COMMAND_CAPTURE ability %s rect=%s normal=%s textured=%s" % [
				String(ability.name), str(ability.get_global_rect()),
				ability_box.get_class() if ability_box != null else "<null>",
				str(ability_box is StyleBoxTexture and (ability_box as StyleBoxTexture).texture != null)
			])
	var probe: Vector2 = (_hud._radial_buttons[0] as Button).get_global_rect().get_center()
	print("HERO_COMMAND_CAPTURE probing socket-1 centre %s" % str(probe))
	_name_controls_at(_hud, probe)


func _name_controls_at(node: Node, point: Vector2) -> void:
	var control := node as Control
	if control != null and control.is_visible_in_tree() and control.get_global_rect().has_point(point):
		print("HERO_COMMAND_CAPTURE   under-point %s z=%d rect=%s" % [
			str(control.get_path()), control.z_index, str(control.get_global_rect())
		])
	for child in node.get_children():
		_name_controls_at(child, point)
