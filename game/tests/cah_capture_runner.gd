extends SceneTree

## SCREENSHOT THE CREATE-A-HERO SCREENS, so a claim about how they LOOK can be
## checked by looking at them.
##
## Same reasoning as `wotr_setup_capture_runner.gd`: headless Godot does not
## render, so `cah_create_a_hero_runner.gd` can prove every number and every
## refusal and still cannot prove the screen is worth looking at. This one opens
## a real rendering context, walks the pages the way a player does, and writes a
## PNG of each - AT BOTH SHIPPING RESOLUTIONS, because "it fills the window" is
## a claim about a particular window and the old single 2560x1440 pass could not
## catch a card that only overflows at 1920x1080.
##
## IT ALSO PUTS THE MENU'S OWN BACKDROP BEHIND THE SCREEN. Without it the shots
## were of a screen on flat black, which is not what ships and which hides every
## contrast problem the real art creates.
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

const SETTLE_FRAMES := 20
## Both resolutions the screen has to be composed at. 2560x1440 matches the
## retail reference captures; 1920x1080 is what most players are on.
const CAPTURE_SIZES := [Vector2i(2560, 1440), Vector2i(1920, 1080)]
const HOST_WINDOW_SIZE := Vector2i(960, 540)
## The shell's own menu plates, tried in order. `main_menu.gd` resolves the
## converted retail art when a pack ships it and falls back to these.
const BACKDROP_CANDIDATES := [
	"res://data/base/assets/ui/menu/backdrop_rivendell_vale.png",
	"res://data/base/assets/ui/menu/backdrop_gorge_dawn.png",
	"res://data/base/assets/ui/menu/backdrop_misty_pass.png",
]

## THE BREASTPLATE, PHOTOGRAPHED CHANGING.
##
## Every Body option in the shipped table repaints the body mesh rather than
## swapping a sub-object, so "the breastplate changed" is a claim about a texture
## and cannot be proved by a runner that only counts sub-objects. These walk the
## whole of one Body group on two subclasses and shoot each option:
##
##   THE CAPTAIN OF GONDOR is the hero the bug was reported against - four
##   breastplates, three of them images no mesh in his skin draws, which is what
##   the importer now publishes beside the models.
##   THE CORRUPT MAN is the control: both of his photographed skins were already
##   embedded in his own GLB, so his armour changed before the pack shipped a
##   single texture and still has to.
##
## Option 4 of the Captain's group paints him in the skin his mesh was cooked
## with, so it is the one option that looks the same as the bare model - which is
## the correct answer rather than a miss.
const PAINTED_GROUP := "CreateAHero_Body"
const PAINTED_SUBJECTS := [
	{
		"name": "corrupt-man",
		"class": 5,
		"sub": 0,
		"body": "CHCM_CM",
		"options": ["Upgrade_CM01_CHBOD07", "Upgrade_CM01_CHBOD04"],
	},
	{
		"name": "captain-of-gondor",
		"class": 0,
		"sub": 0,
		"body": "CHHW_SMN",
		"options": [
			"Upgrade_CAPG_CHBOD01",
			"Upgrade_CAPG_CHBOD02",
			"Upgrade_CAPG_CHBOD03",
			"Upgrade_CAPG_CHBOD04",
		],
	},
]

## THE SHIELD THE DEFAULT KIT WAS SHOWING, and the row that says he has none.
##
## The Captain's `defaultSubObjects` show-set carries `Shld_02` because his
## authored default shield option shows it, and the plan used to put that part in
## the show-set and the hide-set at once - so the equipment list read "No shield"
## over a hero holding one. Both states are photographed: the option that really
## does dress him in it, and the row that says he is without.
const KIT_SHOTS := [
	{
		"name": "captain-of-gondor-shield-default",
		"class": 0,
		"sub": 0,
		"body": "CHHW_SMN",
		"group": "CreateAHero_Shield",
		"option": "Upgrade_CAPG_CHS02",
	},
	{
		"name": "captain-of-gondor-no-shield",
		"class": 0,
		"sub": 0,
		"body": "CHHW_SMN",
		"group": "CreateAHero_Shield",
		"option": "Upgrade_NoShield",
	},
]

var _out_dir := ""
var _screen: Control = null
var _viewport: SubViewport = null
var _frames := 0
var _shot := 0
var _applied := false
var _configured := false
var _plan: Array[Dictionary] = []
var _size := Vector2i.ZERO


func _initialize() -> void:
	if DisplayServer.get_name() == "headless":
		# A camera needs a window. Under --headless the SubViewport never
		# presents and `get_image()` waits for a frame that never comes: this
		# runner sat for hours twice on 2026-08-22 and had to be killed. Refuse
		# loudly instead.
		printerr("CAH_CAPTURE REFUSED: run WINDOWED (drop --headless); this runner photographs the screen and cannot under the headless DisplayServer")
		quit(2)
		return
	_out_dir = _argument("--out", "user://cah-capture")
	DirAccess.make_dir_recursive_absolute(_out_dir)

	var window := root
	window.borderless = true
	window.size = HOST_WINDOW_SIZE
	window.title = "OpenBFME - Create-a-Hero capture"

	_viewport = SubViewport.new()
	_viewport.size = CAPTURE_SIZES[0]
	_viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	_viewport.transparent_bg = false
	window.add_child(_viewport)

	var backdrop := ColorRect.new()
	backdrop.color = Color(0.02, 0.03, 0.045)
	backdrop.set_anchors_preset(Control.PRESET_FULL_RECT)
	_viewport.add_child(backdrop)

	# The screen is stood up on the FIRST FRAME, not here: the ContentDB autoload
	# mounts its packs after `_initialize` returns, so configuring now would
	# photograph the no-table refusal on a machine that has the table.
	var steps := [
		{"name": "01-my-heroes", "action": ""},
		{"name": "02-select-class-and-type", "action": "class"},
		{"name": "03-appearance-garments", "action": "garments"},
		{"name": "04-appearance-attributes", "action": "attributes_tab"},
		{"name": "05-customize-hero-powers", "action": "powers"},
		{"name": "06-powers-with-a-chain-selected", "action": "pick_powers"},
	]
	for subject_index in range(PAINTED_SUBJECTS.size()):
		var subject: Dictionary = PAINTED_SUBJECTS[subject_index]
		for option_index in range((subject["options"] as Array).size()):
			steps.append({
				"name": "%02d-body-%s-%d" % [
					steps.size() + 1, String(subject["name"]), option_index + 1
				],
				"action": "body:%d:%d" % [subject_index, option_index],
			})
	for shot_index in range(KIT_SHOTS.size()):
		steps.append({
			"name": "%02d-kit-%s" % [
				steps.size() + 1, String((KIT_SHOTS[shot_index] as Dictionary)["name"])
			],
			"action": "kit:%d" % shot_index,
		})
	_plan = []
	for size in CAPTURE_SIZES:
		for step in steps:
			_plan.append({
				"name": "%dx%d-%s" % [size.x, size.y, String(step["name"])],
				"action": String(step["action"]),
				"size": size,
			})
	print("[cah-capture] writing to %s" % _out_dir)


func _mounted_system() -> Dictionary:
	var db := root.get_node_or_null("ContentDB")
	if db == null:
		return {}
	var value: Variant = db.get("cah_system_runtime")
	return (value as Dictionary) if typeof(value) == TYPE_DICTIONARY else {}


func _process(_delta: float) -> bool:
	if _shot >= _plan.size():
		print("[cah-capture] wrote %d image(s). This runner asserts nothing; it is a camera." % _plan.size())
		return true
	var step := _plan[_shot]
	# The action lands a whole settle period BEFORE the shot: `_process` runs
	# before the frame is drawn, so capturing in the same visit photographs the
	# state the screen was in beforehand.
	if not _applied:
		var wanted: Vector2i = step["size"]
		if wanted != _size or _screen == null:
			# A NEW SCREEN PER RESOLUTION. The second pass used to walk the same
			# instance the first pass had finished editing, so `1920x1080-01` was a
			# photograph of a roster page with two powers equipped and a build cost
			# of 1000 on it - a state no player reaching that screen can be in.
			_size = wanted
			_viewport.size = wanted
			_stand_up_screen()
		_apply(String(step["action"]))
		_applied = true
		_frames = 0
		return false
	_frames += 1
	if _frames < SETTLE_FRAMES:
		return false
	_frames = 0
	_applied = false
	var image: Image = _viewport.get_texture().get_image()
	var path := "%s/%s.png" % [_out_dir, String(step["name"])]
	image.save_png(path)
	print("[cah-capture] %s" % path)
	_shot += 1
	return false


func _stand_up_screen() -> void:
	if _screen != null:
		_viewport.remove_child(_screen)
		_screen.queue_free()
	_screen = MyHeroesScreen.new()
	_screen.set_anchors_preset(Control.PRESET_FULL_RECT)
	_viewport.add_child(_screen)
	var system := _mounted_system()
	_screen.configure(system)
	_apply_backdrop()
	if _configured:
		return
	_configured = true
	if system.is_empty():
		print("[cah-capture] NO cah.system MOUNTED - the screen draws its refusal, which is also worth a picture.")
		return
	var registration: Dictionary = system.get("registration", {}) as Dictionary
	print("[cah-capture] classes=%d powerTrees=%d maxLevel=%s garments=%s" % [
		(registration.get("classes", []) as Array).size(),
		(registration.get("powerCatalog", []) as Array).size(),
		str((registration.get("experience", {}) as Dictionary).get("maxLevel", "?")),
		_screen.garment_status(),
	])


func _apply_backdrop() -> void:
	for candidate in BACKDROP_CANDIDATES:
		if not ResourceLoader.exists(candidate):
			continue
		var texture := load(candidate) as Texture2D
		if texture != null:
			_screen.set_backdrop_texture(texture)
			print("[cah-capture] backdrop %s" % candidate)
			return
	print("[cah-capture] no menu backdrop found; the screen paints its own scrim only")


func _apply(action: String) -> void:
	match action:
		"class":
			_screen._on_new_hero_pressed()
		"garments":
			_screen._show_page(_screen.PAGE_ATTRIBUTES)
			_screen._show_custom_tab(_screen.CUSTOM_TAB_GARMENTS)
		"attributes_tab":
			_screen._show_page(_screen.PAGE_ATTRIBUTES)
			_screen._show_custom_tab(_screen.CUSTOM_TAB_ATTRIBUTES)
		"powers":
			_screen._show_page(_screen.PAGE_POWERS)
		"pick_powers":
			# Walk one prerequisite chain so the connectors, the numbered Current
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
			if action.begins_with("body:"):
				var parts := action.split(":")
				var subject: Dictionary = PAINTED_SUBJECTS[int(parts[1])]
				_wear(
					subject,
					PAINTED_GROUP,
					String((subject["options"] as Array)[int(parts[2])])
				)
			elif action.begins_with("kit:"):
				var shot: Dictionary = KIT_SHOTS[int(action.split(":")[1])]
				_wear(shot, String(shot["group"]), String(shot["option"]))
			else:
				_screen._show_page(_screen.PAGE_SELECT)
	_report_idles(action)


func _report_idles(action: String) -> void:
	## WHETHER THE HERO IS DOING ANYTHING, printed beside the picture.
	##
	## `cah_create_a_hero_runner.gd` proves the idle playback over synthetic clips,
	## which is the only way to prove it on a machine whose packs carry no creation
	## animations yet. What it cannot say is whether THIS pack ships any - and a
	## still photograph cannot show motion either - so the reading is logged: the
	## clip the hero is on, the specials he can break into, and retail's chance.
	if _screen == null or _screen._preview_model == null:
		return
	var report: Dictionary = _screen.preview_idle_report()
	if String(report.get("base", "")) == "":
		print("[cah-capture] %s -> no creation idles on this subclass; the hero stands still" % action)
		return
	print("[cah-capture] %s -> idling %s (playing %s, specials %s at %s%%)" % [
		action,
		String(report["base"]),
		String(report["playing"]),
		str(report["specials"]),
		str(report["chance"]),
	])


func _wear(subject: Dictionary, group: String, upgrade: String) -> void:
	## Stand one subclass on the garment tab wearing one option of one group, and
	## say in the log what the hero is actually made of - so "the breastplate
	## changed" and "he is not holding a shield" are a photograph AND a reading off
	## the model, not an impression of two similar pictures.
	_screen._on_new_hero_pressed()
	_screen.set_class_selection(int(subject["class"]), int(subject["sub"]))
	_screen._appearance[group] = upgrade
	_screen._rebuild_appearance_rows()
	_screen._show_page(_screen.PAGE_ATTRIBUTES)
	_screen._show_custom_tab(_screen.CUSTOM_TAB_GARMENTS)
	_screen._update_preview()
	print("[cah-capture] %s %s -> shields %s" % [
		String(subject["name"]), upgrade, str(_worn_parts("SHLD"))
	])
	print("[cah-capture] %s %s -> body painted %s, garments %s" % [
		String(subject["name"]),
		upgrade,
		_body_paint(String(subject["body"])),
		_screen.garment_status(),
	])


func _worn_parts(prefix: String) -> Array[String]:
	## The parts of one family the hero is actually drawing, read off the model.
	var out: Array[String] = []
	var model: Node3D = _screen._preview_model
	if model == null:
		return out
	var stack: Array[Node] = [model]
	while not stack.is_empty():
		var node: Node = stack.pop_back()
		for child in node.get_children():
			stack.append(child)
		if not (node is MeshInstance3D) or not (node as MeshInstance3D).visible:
			continue
		var name := String(node.name).to_upper()
		if name.begins_with(prefix) and not out.has(name):
			out.append(name)
	out.sort()
	return out


func _body_paint(part: String) -> String:
	var model: Node3D = _screen._preview_model
	if model == null:
		return "<no model>"
	var stack: Array[Node] = [model]
	while not stack.is_empty():
		var node: Node = stack.pop_back()
		for child in node.get_children():
			stack.append(child)
		if not (node is MeshInstance3D) or String(node.name).to_upper() != part.to_upper():
			continue
		var material := (node as MeshInstance3D).get_active_material(0) as BaseMaterial3D
		if material == null or material.albedo_texture == null:
			return "<no texture>"
		return String(material.albedo_texture.resource_name)
	return "<no %s mesh>" % part


func _argument(flag: String, fallback: String) -> String:
	var args := OS.get_cmdline_user_args()
	for index in range(args.size() - 1):
		if args[index] == flag:
			return args[index + 1]
	return fallback
