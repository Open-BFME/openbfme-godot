extends SceneTree

## Full launch-validation harness for the retail HUD lane.
##
## Unlike the isolated HUD runners, this instantiates the production vertical
## slice and waits through every named initialization phase. It boots both human
## factions in one process with a real, sandboxed created hero. The selected
## host pack supplies the contained APT document exactly as production does.

const BOOT_DEADLINE_MS := 300000
const CahHeroesScript := preload("res://src/content/cah_heroes.gd")
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
const REQUIRED_PHASES := [
	"ready", "environment", "hud", "content", "retail_command_ui", "map_art",
	"map_data", "battlefield", "simulation", "presentations", "audio", "ready_complete",
]

var passed := 0
var failed := 0
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_LAUNCH_VALIDATION_RUNNER")
	for env_name in ["OPENBFME_SLICE_MAP", "OPENBFME_MP", "OPENBFME_STARTER_ARMY", "OPENBFME_CONTROL_PORT"]:
		OS.set_environment(env_name, "")
	OS.set_environment(CahHeroesScript.PROFILE_DIR_ENV, "user://cah-heroes-launch-validation")
	call_deferred("_run")


func _run() -> void:
	var seed_error := _seed_created_hero()
	_check("created_hero_fixture_is_valid", seed_error == "", seed_error)
	for faction in ["men", "dwarves"]:
		await _run_faction(faction)
	_finish()


func _run_faction(faction: String) -> void:
	OS.set_environment("OPENBFME_SLICE_FACTION", faction)
	root.size = Vector2i(1920, 1080)
	var scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	if not _check("%s_launch_scene_loads" % faction, scene != null, "vertical slice scene did not load"):
		return
	var slice = scene.instantiate()
	root.add_child(slice)
	var deadline := Time.get_ticks_msec() + BOOT_DEADLINE_MS
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if bool(slice.ready_ok) or String(slice.failure_reason) != "":
			break
	_check("%s_launch_reaches_ready" % faction, bool(slice.ready_ok), String(slice.failure_reason))
	var metrics: Dictionary = slice.initialization_metrics_ms
	for phase in REQUIRED_PHASES:
		_check(
			"%s_launch_stage_%s" % [faction, phase],
			metrics.has(phase),
			"stage did not complete; completed=%s failure=%s" % [str(metrics.keys()), String(slice.failure_reason)]
		)
	var hud = slice.hud
	_check(
		"%s_launch_apt_uses_a_contained_pack_document" % faction,
		hud != null and hud.retail_apt_runtime != null and bool(hud.retail_apt_runtime.contract_declared),
		"APT contract was not declared: %s" % (String(hud.retail_apt_runtime.error) if hud != null and hud.retail_apt_runtime != null else "runtime missing")
	)
	var created_heroes: Array = []
	for object_id_value in slice.producible_unit_runtimes.keys():
		var object_id := String(object_id_value)
		if object_id.begins_with("CreateAHero__"):
			created_heroes.append(object_id)
	_check(
		"%s_launch_has_created_hero" % faction,
		not created_heroes.is_empty(),
		"created hero absent from human faction roster"
	)
	if hud != null:
		var missing_icon := "CodexMissingLaunchIcon"
		var broken: Dictionary = hud._validate_retail_command(
			root.get_node("ContentDB"), slice.selected_pack_root,
			{
				"image_id": missing_icon,
				"label_id": "CONTROLBAR:TrainUnitGondorFighter",
				"tooltip_id": "CONTROLBAR:ToolTipBuildGondorFighter",
				"action_id": "launch_fixture_missing_icon",
			},
			Vector2i.ZERO
		)
		var expected := "Required UI manifest image '%s' is missing." % missing_icon
		_check(
			"%s_direct_validator_pins_missing_icon_error_after_boot" % faction,
			String(broken.get("error", "")) == expected,
			"expected '%s', got '%s'" % [expected, String(broken.get("error", ""))]
		)
	root.remove_child(slice)
	slice.free()
	await process_frame


func _seed_created_hero() -> String:
	var content_db := root.get_node_or_null("ContentDB")
	if content_db == null:
		return "ContentDB autoload is unavailable"
	var system_value: Variant = content_db.get("cah_system_runtime")
	if typeof(system_value) != TYPE_DICTIONARY or (system_value as Dictionary).is_empty():
		return "mounted packs ship no Create-a-Hero system runtime"
	var system: Dictionary = system_value
	for existing_value in CahHeroesScript.load_profiles():
		CahHeroesScript.delete_profile(String((existing_value as Dictionary).get("heroId", "")))
	var profile := CahHeroesScript.new_profile(system, "Launch Gate Hero", 0, 0)
	var refusals: Array = CahHeroesScript.validate_profile(system, profile)
	if not refusals.is_empty():
		return "seeded profile is invalid: %s" % str(refusals)
	return CahHeroesScript.save_profile(profile)


func _check(name: String, ok: bool, detail: String) -> bool:
	if ok:
		passed += 1
		print("PASS %s" % name)
	else:
		failed += 1
		print("FAIL %s :: %s" % [name, detail])
	return ok


func _finish() -> void:
	print("RESULT retail_launch_validation passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
