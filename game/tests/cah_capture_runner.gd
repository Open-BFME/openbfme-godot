extends SceneTree

## SCREENSHOT THE CREATE-A-HERO SCREENS, so a claim about how they LOOK can be
## checked by looking at them.
##
## Same reasoning as `wotr_setup_capture_runner.gd`: headless Godot does not
## render, so `cah_create_a_hero_runner.gd` can prove every number and every
## refusal and still cannot prove the screen is worth looking at. This one opens
## a real rendering context, walks the four retail pages the way a player does,
## and writes a PNG of each.
##
## IT ASSERTS NOTHING. It is a camera, not a test.
##
## Usage:
##   Godot_v4.7 --path game --script tests/cah_capture_runner.gd -- --out <dir>
##
## The screen reads the mounted pack's `cah.system` through ContentDB, so what
## these images show is the real compiled class table, the real icons the pack
## carries (or the named refusals where it carries none) and the real mesh.

const MyHeroesScreen = preload("res://src/ui/my_heroes_screen.gd")

const SETTLE_FRAMES := 24
## The reference screenshots are 2560x1440; matching them makes the two
## directly comparable side by side.
const CAPTURE_SIZE := Vector2i(2560, 1440)
const HOST_WINDOW_SIZE := Vector2i(960, 540)

var _out_dir := ""
var _screen: Control = null
var _viewport: SubViewport = null
var _frames := 0
var _shot := 0
var _applied := false
var _configured := false
var _plan: Array[Dictionary] = []


func _initialize() -> void:
	_out_dir = _argument("--out", "user://cah-capture")
	DirAccess.make_dir_recursive_absolute(_out_dir)

	var window := root
	window.borderless = true
	window.size = HOST_WINDOW_SIZE
	window.title = "OpenBFME - Create-a-Hero capture"

	_viewport = SubViewport.new()
	_viewport.size = CAPTURE_SIZE
	_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_viewport.transparent_bg = false
	window.add_child(_viewport)

	var backdrop := ColorRect.new()
	backdrop.color = Color(0.02, 0.03, 0.045)
	backdrop.set_anchors_preset(Control.PRESET_FULL_RECT)
	_viewport.add_child(backdrop)

	_screen = MyHeroesScreen.new()
	_screen.set_anchors_preset(Control.PRESET_FULL_RECT)
	_viewport.add_child(_screen)

	# The screen is configured on the FIRST FRAME, not here: the ContentDB
	# autoload mounts its packs after `_initialize` returns, so configuring now
	# would photograph the no-table refusal on a machine that has the table.
	_plan = [
		{"name": "01-select-hero", "action": ""},
		{"name": "02-select-class-and-type", "action": "class"},
		{"name": "03-customize-attributes", "action": "attributes"},
		{"name": "04-customize-hero-powers", "action": "powers"},
		{"name": "05-powers-with-a-chain-selected", "action": "pick_powers"},
	]
	print("[cah-capture] writing to %s" % _out_dir)


func _mounted_system() -> Dictionary:
	var db := root.get_node_or_null("ContentDB")
	if db == null:
		return {}
	var value: Variant = db.get("cah_system_runtime")
	return (value as Dictionary) if typeof(value) == TYPE_DICTIONARY else {}


func _process(_delta: float) -> bool:
	if not _configured:
		_configured = true
		var system := _mounted_system()
		_screen.configure(system)
		if system.is_empty():
			print("[cah-capture] NO cah.system MOUNTED - the screen draws its refusal, which is also worth a picture.")
		else:
			var registration: Dictionary = system.get("registration", {}) as Dictionary
			print("[cah-capture] classes=%d powerTrees=%d maxLevel=%s" % [
				(registration.get("classes", []) as Array).size(),
				(registration.get("powerCatalog", []) as Array).size(),
				str((registration.get("experience", {}) as Dictionary).get("maxLevel", "?")),
			])
		return false
	_frames += 1
	if _frames < SETTLE_FRAMES:
		return false
	_frames = 0
	if _shot >= _plan.size():
		print("[cah-capture] wrote %d image(s). This runner asserts nothing; it is a camera." % _plan.size())
		return true
	var step := _plan[_shot]
	# The action lands a whole settle period BEFORE the shot: `_process` runs
	# before the frame is drawn, so capturing in the same visit photographs the
	# state the screen was in beforehand.
	if not _applied:
		_apply(String(step["action"]))
		_applied = true
		return false
	_applied = false
	var image: Image = _viewport.get_texture().get_image()
	var path := "%s/%s.png" % [_out_dir, String(step["name"])]
	image.save_png(path)
	print("[cah-capture] %s" % path)
	_shot += 1
	return false


func _apply(action: String) -> void:
	match action:
		"class":
			_screen._on_new_hero_pressed()
		"attributes":
			_screen._show_page(_screen.PAGE_ATTRIBUTES)
		"powers":
			_screen._show_page(_screen.PAGE_POWERS)
		"pick_powers":
			# Walk one prerequisite chain so the arrows, the numbered Current
			# Powers list and the running Build Cost all have something to show.
			var trees: Array = _screen.CahHeroes.power_trees_for_class(
				_screen._system, _screen._selected_class
			)
			for tree_value in trees:
				var levels: Array = (tree_value as Dictionary).get("levels", []) as Array
				if levels.size() < 2:
					continue
				for level_value in levels:
					_screen._toggle_power(String((level_value as Dictionary).get("powerId", "")))
				break
			_screen._show_page(_screen.PAGE_POWERS)
		_:
			pass


func _argument(flag: String, fallback: String) -> String:
	var args := OS.get_cmdline_user_args()
	for index in range(args.size() - 1):
		if args[index] == flag:
			return args[index + 1]
	return fallback
