extends SceneTree
## Remappable InputMap bindings persist and apply. Controller family is Xbox
## or the 2026 Steam Controller. Icons for rebind / families / face buttons
## must exist on disk.

const Settings = preload("res://src/ui/user_settings.gd")

const EXPECTED_CHECKS := 17
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _watchdog := RunnerWatchdogScript.new()
var passed := 0
var failed := 0


func _initialize() -> void:
	_watchdog.start(self, "INPUT_BINDINGS")
	call_deferred("_run")


func _run() -> void:
	var original := Settings.load_bindings()
	var original_family := Settings.load_controller_family()
	_test_defaults_cover_camera_and_pause()
	_test_save_load_roundtrip()
	_test_apply_registers_actions()
	_test_glyphs_and_tokens()
	_test_icons_exist()
	Settings.save_bindings(original, original_family)
	Settings.apply_bindings_to_input_map(original)
	_finish()


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("INPUT_BINDINGS PASS %s" % label)
	else:
		failed += 1
		printerr("INPUT_BINDINGS FAIL %s%s" % [label, "" if detail == "" else " (%s)" % detail])


func _test_defaults_cover_camera_and_pause() -> void:
	var defaults := Settings.default_bindings()
	_check("defaults_include_pause_and_camera",
		defaults.has("pause_menu") and defaults.has("cam_forward") and defaults.has("attack_move"))
	_check("pause_defaults_to_escape",
		int((defaults["pause_menu"] as Dictionary).get("key", 0)) == KEY_ESCAPE)
	_check("steam_and_xbox_families_exist",
		Settings.CONTROLLER_FAMILIES.has("xbox") and Settings.CONTROLLER_FAMILIES.has("steam"))


func _test_save_load_roundtrip() -> void:
	var bindings := Settings.default_bindings()
	(bindings["attack_move"] as Dictionary)["key"] = KEY_G
	(bindings["stop_units"] as Dictionary)["joy"] = JOY_BUTTON_A
	var err := Settings.save_bindings(bindings, "steam")
	_check("save_bindings_ok", err == OK, error_string(err))
	var loaded := Settings.load_bindings()
	_check("rebind_survives_reload",
		int((loaded["attack_move"] as Dictionary).get("key", 0)) == KEY_G
			and int((loaded["stop_units"] as Dictionary).get("joy", -2)) == JOY_BUTTON_A)
	_check("controller_family_survives_reload", Settings.load_controller_family() == "steam")


func _test_apply_registers_actions() -> void:
	var bindings := Settings.default_bindings()
	(bindings["hide_hud"] as Dictionary)["key"] = KEY_F3
	Settings.apply_bindings_to_input_map(bindings)
	_check("hide_hud_action_exists_after_apply", InputMap.has_action("hide_hud"))
	var has_f3 := false
	for event in InputMap.action_get_events("hide_hud"):
		if event is InputEventKey and int((event as InputEventKey).physical_keycode) == KEY_F3:
			has_f3 = true
	_check("applied_rebind_is_on_input_map", has_f3)
	var has_stick := false
	for event in InputMap.action_get_events("cam_left"):
		if event is InputEventJoypadMotion:
			has_stick = true
	_check("camera_has_left_stick_axis", has_stick)
	var has_rotate := false
	for event in InputMap.action_get_events("cam_rotate_left"):
		if event is InputEventJoypadMotion:
			has_rotate = true
	_check("rotate_has_right_stick_axis", has_rotate)
	var has_joy := false
	for event in InputMap.action_get_events("attack_move"):
		if event is InputEventJoypadButton:
			has_joy = true
	_check("attack_move_has_joy_button", has_joy)


func _test_glyphs_and_tokens() -> void:
	_check("joy_token_maps_face_and_shoulders",
		Settings.joy_token(JOY_BUTTON_A) == "a"
			and Settings.joy_token(JOY_BUTTON_LEFT_SHOULDER) == "lb"
			and Settings.joy_token(JOY_BUTTON_START) == "start")
	var xbox_a := Settings.joy_glyph_path(JOY_BUTTON_A, "xbox")
	var steam_a := Settings.joy_glyph_path(JOY_BUTTON_A, "steam")
	var steam_pad := Settings.glyph_path("controller", "steam")
	_check("glyph_path_resolves_xbox_and_steam",
		xbox_a.ends_with("xbox_a.jpg")
			and steam_a.ends_with("steam_a.jpg")
			and steam_pad.ends_with("controller_steam.jpg"),
		"xbox=%s steam=%s pad=%s" % [xbox_a, steam_a, steam_pad])


func _test_icons_exist() -> void:
	var missing: Array[String] = []
	for path in [
		"res://assets/ui/input/controller_xbox.jpg",
		"res://assets/ui/input/controller_steam.jpg",
		"res://assets/ui/input/key_blank.jpg",
		"res://assets/ui/input/rebind_listen.jpg",
		"res://assets/ui/input/xbox_a.jpg",
		"res://assets/ui/input/xbox_b.jpg",
		"res://assets/ui/input/xbox_x.jpg",
		"res://assets/ui/input/xbox_y.jpg",
		"res://assets/ui/input/xbox_lb.jpg",
		"res://assets/ui/input/xbox_rb.jpg",
		"res://assets/ui/input/xbox_start.jpg",
		"res://assets/ui/input/xbox_back.jpg",
		"res://assets/ui/input/steam_a.jpg",
		"res://assets/ui/input/steam_start.jpg",
		"res://assets/ui/input/steam_back.jpg",
	]:
		if not ResourceLoader.exists(path) and not FileAccess.file_exists(path):
			missing.append(path)
	_check("input_icons_are_on_disk", missing.is_empty(), ", ".join(missing))
	var tex := Settings.load_glyph_texture("res://assets/ui/input/controller_steam.jpg")
	_check("glyph_textures_load", tex != null and tex.get_width() > 0)
	var pic_w := Settings.key_picture(KEY_W)
	var pic_a := Settings.key_picture(KEY_A)
	var img_w := pic_w.get_image() if pic_w != null else null
	var img_a := pic_a.get_image() if pic_a != null else null
	var blank := Settings.load_glyph_texture(Settings.glyph_path("key", "blank"))
	var blank_img := blank.get_image() if blank != null else null
	_check(
		"letter_keys_have_unique_pictures",
		img_w != null and img_a != null and img_w.get_data() != img_a.get_data(),
		"w=%s a=%s" % [str(img_w != null), str(img_a != null)]
	)
	_check(
		"letter_picture_is_not_shared_blank",
		img_w != null and blank_img != null and img_w.get_data() != blank_img.get_data()
	)


func _finish() -> void:
	if passed + failed != EXPECTED_CHECKS:
		failed += 1
		printerr("INPUT_BINDINGS FAIL expected_checks passed=%d failed=%d expected=%d" % [passed, failed - 1, EXPECTED_CHECKS])
	print("INPUT_BINDINGS_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
