extends SceneTree

## PHOTOGRAPH THE IN-GAME HUD, so a claim about where the money, the command
## points, the palantir sockets and the hero roster SIT can be checked by
## looking at them (Q37/Q38/Q39).
##
## Same reasoning as `cah_capture_runner.gd`: headless Godot does not render, so
## `retail_four_unit_hud_runner` can prove every coordinate equals the authored
## `Palantir.apt` translation and still cannot prove the bar looks right. This
## one opens a real rendering context, builds the HUD with a hero roster and a
## structure radial up, and writes a PNG.
##
## IT ASSERTS NOTHING. It is a camera, not a test.
##
## Usage:
##   Godot_v4.7 --path game --script tests/hud_layout_capture_runner.gd \
##     -- --out <dir> --tag <before|after>

const SETTLE_FRAMES := 20
const CAPTURE_SIZES := [Vector2i(1920, 1080), Vector2i(2560, 1440)]
const HOST_WINDOW_SIZE := Vector2i(960, 540)

var _viewport: SubViewport
var _hud: Control
var _out_dir := "res://"
var _tag := "after"
var _size := Vector2i.ZERO
var _shot := 0
var _frames := 0
var _applied := false


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	for index in args.size():
		if args[index] == "--out" and index + 1 < args.size():
			_out_dir = args[index + 1]
		elif args[index] == "--tag" and index + 1 < args.size():
			_tag = args[index + 1]
	DirAccess.make_dir_recursive_absolute(_out_dir)
	root.size = HOST_WINDOW_SIZE
	_viewport = SubViewport.new()
	_viewport.transparent_bg = false
	_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(_viewport)


func _process(_delta: float) -> bool:
	if _shot >= CAPTURE_SIZES.size():
		print("[hud-capture] wrote %d image(s). This runner asserts nothing." % CAPTURE_SIZES.size())
		return true
	if not _applied:
		_size = CAPTURE_SIZES[_shot]
		_viewport.size = _size
		_stand_up_hud()
		_applied = true
		_frames = 0
		return false
	_frames += 1
	if _frames < SETTLE_FRAMES:
		return false
	_frames = 0
	_applied = false
	var image: Image = _viewport.get_texture().get_image()
	var path := "%s/hud-%s-%dx%d.png" % [_out_dir, _tag, _size.x, _size.y]
	image.save_png(path)
	print("[hud-capture] %s" % path)
	_shot += 1
	return false


func _stand_up_hud() -> void:
	if _hud != null:
		_viewport.remove_child(_hud)
		_hud.queue_free()
	var backdrop := ColorRect.new()
	backdrop.color = Color(0.22, 0.26, 0.17)
	backdrop.set_anchors_preset(Control.PRESET_FULL_RECT)
	var hud_script: Script = load("res://src/retail_slice/retail_hud.gd")
	_hud = hud_script.new()
	_hud.set_anchors_preset(Control.PRESET_FULL_RECT)
	_hud.add_child(backdrop)
	_hud.move_child(backdrop, 0)
	_viewport.add_child(_hud)
	_hud.build()
	_hud.set_resources(1100, 0, 200)
	_hud.set_dish_level("Level: 2", 0.45)
	_hud.sync_hero_bar([
		{"id": 1, "unit_type": "hero.a", "name": "Hero A", "level": 2,
			"health": 62, "maximum_health": 100, "selected": true},
		{"id": 2, "unit_type": "hero.b", "name": "Hero B", "level": 1,
			"health": 100, "maximum_health": 100, "selected": false},
	])
	# TEN entries: the fortress hero page `Command_SelectRevivablesMenFortress`
	# reveals (commandbutton.ini:11003-11004, CommandRangeStart 14 /
	# CommandRangeCount 10). That is the page in the owner's 1920x1080 v0.2.8
	# capture, so it is the page this camera has to point at.
	var entries: Array = []
	for index in 10:
		entries.append({
			"command_kind": "train", "id": "capture.%d" % index, "icon": null,
			"text": "C%d" % index, "enabled": true, "label": "Command %d" % index,
			"tooltip": "", "slot": index + 1,
		})
	_hud.sync_radial_commands(Vector2(_size) * Vector2(0.45, 0.35), entries)
